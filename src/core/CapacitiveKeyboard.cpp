#include "CapacitiveKeyboard.h"
#include "HardwareConfig.h"
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <string.h>
#include <math.h>

// MPR121 register addresses
#define MPR121_TOUCH_THRESHOLD_BASE  0x41
#define MPR121_ACCR0        0x5B
#define MPR121_FILTER_CFG   0x5D  // Filter Configuration: CDT[7:5], SFI[4:3], ESI[2:0]
#define MPR121_ECR          0x5E
#define MPR121_AUTOCONFIG0  0x7B
#define MPR121_USL          0x7D
#define MPR121_LSL          0x7E
#define MPR121_TL           0x7F
#define MPR121_OOR_STATUS0  0x02  // Out-of-Range status ELE0-7
#define MPR121_OOR_STATUS1  0x03  // Out-of-Range status ELE8-11 + ACFF/ARFF flags

// Baseline tracking filter registers
#define MPR121_MHDR  0x2B  // Max Half Delta Rising
#define MPR121_NHDR  0x2C  // Noise Half Delta Rising
#define MPR121_NCLR  0x2D  // Noise Count Limit Rising
#define MPR121_FDLR  0x2E  // Filter Delay Count Limit Rising
#define MPR121_MHDF  0x2F  // Max Half Delta Falling
#define MPR121_NHDF  0x30  // Noise Half Delta Falling
#define MPR121_NCLF  0x31  // Noise Count Limit Falling
#define MPR121_FDLF  0x32  // Filter Delay Count Limit Falling
#define MPR121_NHDT  0x33  // Noise Half Delta Touched
#define MPR121_NCLT  0x34  // Noise Count Limit Touched
#define MPR121_FDLT  0x35  // Filter Delay Count Limit Touched

// MPR121 autoconfig: USL/TL/LSL derived from target baseline per NXP AN3889.
// Old ad-hoc scaling (1.1x/0.7x) removed — now uses VDD-derived limits from HardwareConfig.h.

// MPR121 default touch/release thresholds (per-channel)
static const uint8_t MPR121_DEFAULT_TOUCH_THRESH   = 12;
static const uint8_t MPR121_DEFAULT_RELEASE_THRESH  = 6;
static const uint8_t MPR121_CHANNELS = 12;

// =================================================================
// Baseline Filter Profiles (MPR121 registers 0x2B–0x35)
// =================================================================
//
// The MPR121 baseline is a live tracking filter that follows ambient
// capacitance while ignoring touches. It operates under 3 conditions,
// each with its own register set:
//
//   Rising  (filtered > baseline): capacitance decreased
//           — after finger release, or humidity/temperature drop
//   Falling (filtered < baseline): capacitance increased
//           — touch approach direction, or humidity/temperature rise
//   Touched (MPR121 internal touch bit set, delta > 12):
//           — pad is being actively pressed
//
// Each condition has up to 4 parameters:
//   MHD (Max Half Delta):     changes <= 2*MHD pass through instantly
//   NHD (Noise Half Delta):   step size when larger changes persist
//   NCL (Noise Count Limit):  consecutive samples beyond MHD before
//                              NHD increment/decrement applies
//   FDL (Filter Delay Limit): averages N samples before filtering
//                              (global slowdown)
//
// When the condition changes (e.g. rising -> falling), all filter
// counters reset to zero. This is how noise (oscillating data) gets
// rejected by the baseline system.
//
// All profiles freeze the baseline during touch (NHDT=0). This is
// non-negotiable for pressure sensing: without it, the baseline
// would creep toward the pressed value, shrinking the delta over
// time and causing sustained aftertouch to decay artificially.
// The difference between profiles is behavior BETWEEN touches.
// =================================================================

struct BaselineFilterParams {
  uint8_t mhdr, nhdr, nclr, fdlr;  // Rising
  uint8_t mhdf, nhdf, nclf, fdlf;  // Falling
  uint8_t nhdt, nclt, fdlt;         // Touched
  const char* name;
};

// ---------------------------------------------------------------
// ADAPTIVE (default)
//
// Character: Fast environmental adaptation, moderate recovery.
// The baseline drops quickly when ambient capacitance increases
// (NCLF=1, NHDF=5 = 5-count drop after just 1 sample beyond MHD),
// and recovers conservatively after release (NCLR=14 = 14
// consecutive samples before incrementing by 1).
//
// Best for: Changing environments — live performance, varying
// humidity/temperature, shared instruments where different hands
// bring different baseline capacitance. The baseline keeps up
// with the world around it.
//
// Trade-off: A very slow finger approach could chase the baseline
// down slightly before the touch threshold freezes it, costing
// a few counts of delta range.
// ---------------------------------------------------------------
// EXPRESSIVE
//
// Character: Resistant to falling, moderate recovery. The baseline
// needs 10 consecutive samples before it drops by just 1 count
// (vs Adaptive: 1 sample, drops 5). Rising recovery is slightly
// faster (NCL=6 vs 14).
//
// Best for: Expressive aftertouch work — sustained pads, slow
// dynamics, gentle approaches. A player who eases into a pad
// slowly gets the full calibrated delta range because the baseline
// refuses to chase the approach. Slightly faster rising recovery
// ensures the pad is ready after a long sustain.
//
// Trade-off: Slower adaptation to environmental changes. If
// humidity shifts or the player switches, it takes longer for the
// baseline to settle. Not ideal for unpredictable conditions.
// ---------------------------------------------------------------
// PERCUSSIVE
//
// Character: Fastest recovery, most fall-resistant. Rising needs
// only 2 consecutive samples to increment — baseline snaps back
// almost instantly after release. Falling needs 16 samples to
// decrement by 1 — baseline barely moves downward at all.
//
// Best for: Drum pads, rapid retriggering, fast rhythmic playing.
// Ultra-fast rising recovery means the pad is at full sensitivity
// for the next hit within milliseconds of release. Very slow
// falling means rapid hits don't erode the baseline between
// strikes, even at high BPM.
//
// Trade-off: Slowest environmental adaptation. If conditions
// change, the baseline takes a while to catch up. Best in a
// stable environment (studio, controlled stage).
// ---------------------------------------------------------------

static const BaselineFilterParams s_baselineProfiles[NUM_BASELINE_PROFILES] = {
  //                  --- Rising ---          --- Falling ---         --- Touched ---
  //                  MHD  NHD  NCL  FDL      MHD  NHD  NCL  FDL     NHD  NCL  FDL    Name
  /* ADAPTIVE   */ {  0x01,0x01,0x0E,0x00,    0x01,0x05,0x01,0x00,   0x00,0x00,0x00,  "Adaptive"   },
  /* EXPRESSIVE */ {  0x01,0x01,0x06,0x00,    0x01,0x01,0x0A,0x00,   0x00,0x00,0x00,  "Expressive" },
  /* PERCUSSIVE */ {  0x01,0x01,0x02,0x00,    0x01,0x01,0x10,0x00,   0x00,0x00,0x00,  "Percussive" },
};

// =================================================================
// I2C Register Access
// =================================================================

void CapacitiveKeyboard::writeRegister(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t CapacitiveKeyboard::readRegister(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, (uint8_t)1);
  return Wire.read();
}

// =================================================================
// Constructor
// =================================================================

CapacitiveKeyboard::CapacitiveKeyboard() {
  isInitialized = false;
  responseShape = RESPONSE_SHAPE_DEFAULT;
  currentTargetBaseline = CAL_DEFAULT_TARGET_BASELINE;
  aftertouchDeadzoneOffset = 0;
  _baselineProfile = DEFAULT_BASELINE_PROFILE;
  _pressThresholdPct = PRESS_THRESHOLD_PERCENT;
  _releaseThresholdPct = RELEASE_THRESHOLD_PERCENT;
  _slewRateLimit = SLEW_RATE_DEFAULT;

  for (int i = 0; i < NUM_KEYS; i++) {
    filteredData[i] = 0;
    baselineData[i] = 0;
    smoothedPressure[i] = 0.0f;
    slewedPressure[i] = 0.0f;
    keyIsPressed[i] = false;
    lastKeyIsPressed[i] = false;
    calibrationMaxDelta[i] = 400;
    pressDeltaStart[i] = 0;
    historyIndex[i] = 0;
    for (int j = 0; j < AFTERTOUCH_SMOOTHING_WINDOW_SIZE; j++) {
      pressureHistory[i][j] = 0.0f;
    }
  }

  for (int s = 0; s < NUM_SENSORS; s++) {
    _i2cFailCount[s] = 0;
    _sensorFailed[s] = false;
  }
}

// =================================================================
// Initialization
// =================================================================

bool CapacitiveKeyboard::begin() {
  #if DEBUG_SERIAL
  Serial.println("[KB] Starting capacitive keyboard init...");
  #endif

  loadCalibrationData();
  #if DEBUG_SERIAL
  Serial.println("[KB] Calibration data loaded.");
  #endif
  delay(50);

  #if DEBUG_SERIAL
  Serial.print("[KB] Running MPR121 autoconfig (target: ");
  Serial.print(currentTargetBaseline);
  Serial.println(")...");
  #endif

  bool success = runAutoconfiguration(currentTargetBaseline);
  delay(100);

  #if DEBUG_SERIAL
  if (success) {
    Serial.println("[KB] Capacitive sensors initialized OK.");
  } else {
    Serial.println("[KB] FATAL: Sensor init failed!");
  }
  #endif
  return success;
}

// =================================================================
// Runtime Controls
// =================================================================

void CapacitiveKeyboard::setResponseShape(float shape) {
  if (shape < 0.0f) shape = 0.0f;
  if (shape > 1.0f) shape = 1.0f;
  responseShape = shape;
}

void CapacitiveKeyboard::setAftertouchDeadzone(int offset) {
  aftertouchDeadzoneOffset = constrain(offset, 0, AFTERTOUCH_DEADZONE_MAX_OFFSET);
}

void CapacitiveKeyboard::setBaselineProfile(uint8_t profile) {
  if (profile < NUM_BASELINE_PROFILES) {
    _baselineProfile = profile;
  }
}

uint8_t CapacitiveKeyboard::getBaselineProfile() const {
  return _baselineProfile;
}

const char* CapacitiveKeyboard::getBaselineProfileName() const {
  return s_baselineProfiles[_baselineProfile].name;
}

void CapacitiveKeyboard::setPadSensitivity(uint8_t percent) {
  percent = constrain(percent, PAD_SENSITIVITY_MIN, PAD_SENSITIVITY_MAX);
  _pressThresholdPct = percent / 100.0f;
  _releaseThresholdPct = _pressThresholdPct * (RELEASE_THRESHOLD_PERCENT / PRESS_THRESHOLD_PERCENT);
  calculateAdaptiveThresholds();
}

uint8_t CapacitiveKeyboard::getPadSensitivity() const {
  return (uint8_t)(_pressThresholdPct * 100.0f + 0.5f);
}

void CapacitiveKeyboard::setSlewRate(uint16_t limit) {
  _slewRateLimit = constrain(limit, SLEW_RATE_MIN, SLEW_RATE_MAX);
}

uint16_t CapacitiveKeyboard::getSlewRate() const {
  return _slewRateLimit;
}

// =================================================================
// Main Update — Pressure Pipeline
// =================================================================

void CapacitiveKeyboard::update() {
  if (!isInitialized) return;
  memcpy(lastKeyIsPressed, keyIsPressed, sizeof(keyIsPressed));
  pollAllSensorData();

  for (int i = 0; i < NUM_KEYS; i++) {
    // Skip keys on failed sensors (already forced off in pollAllSensorData)
    if (_sensorFailed[i / CHANNELS_PER_SENSOR]) continue;

    uint16_t delta = (baselineData[i] > filteredData[i])
                     ? (baselineData[i] - filteredData[i]) : 0;

    // --- Note On/Off State Machine ---
    if (!keyIsPressed[i] && delta > pressThresholds[i]) {
      keyIsPressed[i] = true;
      pressDeltaStart[i] = delta;  // Capture relative zero
    }
    else if (keyIsPressed[i] && delta < releaseThresholds[i]) {
      keyIsPressed[i] = false;
      // Force-reset all pressure state
      slewedPressure[i] = 0.0f;
      smoothedPressure[i] = 0.0f;
      for (int j = 0; j < AFTERTOUCH_SMOOTHING_WINDOW_SIZE; j++) {
        pressureHistory[i][j] = 0.0f;
      }
    }

    // --- Pressure computation (only for pressed keys) ---
    float targetPressure = 0.0f;
    if (keyIsPressed[i]) {
      uint16_t maxD = calibrationMaxDelta[i];
      uint16_t pressD = pressDeltaStart[i] + aftertouchDeadzoneOffset;
      float aftertouch_norm = 0.0f;

      if (maxD > pressD) {
        aftertouch_norm = (float)((delta > pressD) ? (delta - pressD) : 0)
                        / (float)(maxD - pressD);
      }
      if (aftertouch_norm > 1.0f) aftertouch_norm = 1.0f;

      // Response shaping (exponential / sigmoid blend)
      float shaped_norm = 0.0f;
      float x = aftertouch_norm;

      if (responseShape < 0.5f) {
        float x2 = x * x;
      float y_exp = x2 * x2;  // x^4 — must match AFTERTOUCH_CURVE_EXP_INTENSITY
        float y_lin = x;
        float t = 1.0f - (responseShape * 2.0f);
        shaped_norm = y_lin * (1.0f - t) + y_exp * t;
      } else {
        float y_sig = x * x * (3.0f - 2.0f * x);
        for (int j = 1; j < AFTERTOUCH_CURVE_SIG_INTENSITY; j++) {
          y_sig = y_sig * y_sig * (3.0f - 2.0f * y_sig);
        }
        float y_lin = x;
        float t = (responseShape - 0.5f) * 2.0f;
        shaped_norm = y_lin * (1.0f - t) + y_sig * t;
      }

      // Scale to MIDI range 0-127 (was CV_OUTPUT_RESOLUTION=4095)
      targetPressure = shaped_norm * 127.0f;
    }

    // --- Smoothing Stage 1: Slew Rate Limiter ---
    float diff = targetPressure - slewedPressure[i];
    float slewLimit = (float)_slewRateLimit;
    if (diff > slewLimit) {
      slewedPressure[i] += slewLimit;
    } else if (diff < -slewLimit) {
      slewedPressure[i] -= slewLimit;
    } else {
      slewedPressure[i] = targetPressure;
    }

    // --- Smoothing Stage 2: Moving Average ---
    pressureHistory[i][historyIndex[i]] = slewedPressure[i];
    historyIndex[i] = (historyIndex[i] + 1) % AFTERTOUCH_SMOOTHING_WINDOW_SIZE;

    float sum = 0.0f;
    for (int j = 0; j < AFTERTOUCH_SMOOTHING_WINDOW_SIZE; j++) {
      sum += pressureHistory[i][j];
    }
    smoothedPressure[i] = sum / (float)AFTERTOUCH_SMOOTHING_WINDOW_SIZE;
  }
}

// =================================================================
// Note State Getters
// =================================================================

bool CapacitiveKeyboard::isPressed(uint8_t note) {
  if (note >= NUM_KEYS) return false;
  return keyIsPressed[note];
}

bool CapacitiveKeyboard::noteOn(uint8_t note) {
  if (note >= NUM_KEYS) return false;
  return keyIsPressed[note] && !lastKeyIsPressed[note];
}

bool CapacitiveKeyboard::noteOff(uint8_t note) {
  if (note >= NUM_KEYS) return false;
  return !keyIsPressed[note] && lastKeyIsPressed[note];
}

uint8_t CapacitiveKeyboard::getPressure(uint8_t note) {
  if (note >= NUM_KEYS) return 0;
  int val = (int)(smoothedPressure[note] + 0.5f);  // Round
  if (val < 0) val = 0;
  if (val > 127) val = 127;
  return (uint8_t)val;
}

const bool* CapacitiveKeyboard::getPressedKeysState() const {
  return keyIsPressed;
}

// =================================================================
// MPR121 Autoconfiguration — 4 sensors
// =================================================================

bool CapacitiveKeyboard::runAutoconfiguration(uint16_t targetBaseline) {
  #if DEBUG_SERIAL
  Serial.println("[KB] Computing autoconfig params...");
  #endif

  // Derive autoconfig limits from target baseline using NXP-recommended ratios
  // USL must not exceed MPR121_VDD_USL (safe ADC ceiling for this VDD)
  uint8_t tl  = (targetBaseline >> 2);
  uint8_t usl = min((uint8_t)(tl * 1.05f + 0.5f), MPR121_VDD_USL);  // 5% above target, capped
  uint8_t lsl = (uint8_t)(tl * 0.65f);                                // 65% of target per NXP

  #if DEBUG_SERIAL
  Serial.print("[KB] TL: "); Serial.print(tl);
  Serial.print(", USL: "); Serial.print(usl);
  Serial.print(", LSL: "); Serial.println(lsl);
  #endif

  const uint8_t sensor_addrs[] = { ADDR_MPR121_A, ADDR_MPR121_B,
                                    ADDR_MPR121_C, ADDR_MPR121_D };

  for (int sensorIndex = 0; sensorIndex < NUM_SENSORS; sensorIndex++) {
    uint8_t addr = sensor_addrs[sensorIndex];

    #if DEBUG_SERIAL
    Serial.print("[KB] Configuring MPR121 #");
    Serial.print(sensorIndex + 1);
    Serial.print(" at 0x");
    Serial.println(addr, HEX);
    #endif

    // Test I2C connection (retry transient errors; only code 2 = address NACK = absent)
    {
      bool deviceFound = false;
      for (int attempt = 0; attempt < 3; attempt++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
          deviceFound = true;
          break;
        }
        if (err == 2) break;  // Address NACK — device not present, no point retrying
        delay(10);  // Transient error (1=data too long, 3=data NACK, 4=other) — retry
      }
      if (!deviceFound) {
        #if DEBUG_SERIAL
        Serial.print("[KB] FATAL: MPR121 at 0x");
        Serial.print(addr, HEX);
        Serial.println(" not found!");
        #endif
        return false;
      }
    }

    // Stop config mode
    writeRegister(addr, MPR121_ECR, 0x00);
    delay(20);

    // Pre-seed baselines to target level so they start near the correct value.
    // With CL=00 (below), the MPR121 uses these as initial baselines.
    // BVA=10 will fine-tune after autoconfig finds exact CDC/CDT settings.
    for (uint8_t i = 0; i < MPR121_CHANNELS; i++) {
      writeRegister(addr, 0x1E + i, tl);
    }

    // Zero per-electrode CDC registers so the completion poll works correctly
    // on repeated calls. Without this, CDC registers retain values from a
    // previous autoconfig run, and the poll would pass immediately.
    for (uint8_t e = 0; e < MPR121_CHANNELS; e++) {
      writeRegister(addr, 0x5F + e, 0);
    }

    // Baseline tracking filter — apply current profile
    const BaselineFilterParams& bp = s_baselineProfiles[_baselineProfile];
    writeRegister(addr, MPR121_MHDR, bp.mhdr);   // Rising
    writeRegister(addr, MPR121_NHDR, bp.nhdr);
    writeRegister(addr, MPR121_NCLR, bp.nclr);
    writeRegister(addr, MPR121_FDLR, bp.fdlr);
    writeRegister(addr, MPR121_MHDF, bp.mhdf);   // Falling
    writeRegister(addr, MPR121_NHDF, bp.nhdf);
    writeRegister(addr, MPR121_NCLF, bp.nclf);
    writeRegister(addr, MPR121_FDLF, bp.fdlf);
    writeRegister(addr, MPR121_NHDT, bp.nhdt);   // Touched
    writeRegister(addr, MPR121_NCLT, bp.nclt);
    writeRegister(addr, MPR121_FDLT, bp.fdlt);

    // Touch/release thresholds for all channels
    for (uint8_t i = 0; i < MPR121_CHANNELS; i++) {
      writeRegister(addr, MPR121_TOUCH_THRESHOLD_BASE + 2 * i, MPR121_DEFAULT_TOUCH_THRESH);
      writeRegister(addr, MPR121_TOUCH_THRESHOLD_BASE + 1 + 2 * i, MPR121_DEFAULT_RELEASE_THRESH);
    }
    delay(10);

    // Filter configuration: CDT=0 (per-electrode via autoconfig), SFI=4 samples, ESI=16ms
    // Per NXP QuickStart AN3944 recommendation (register default 0x00 leaves CDT invalid)
    writeRegister(addr, MPR121_FILTER_CFG, 0x04);

    // Autoconfig parameters
    writeRegister(addr, MPR121_ACCR0, 0);     // Clear autoconfig control
    writeRegister(addr, MPR121_USL, usl);
    writeRegister(addr, MPR121_LSL, lsl);
    writeRegister(addr, MPR121_TL, tl);
    delay(10);

    // Enable autoconfig: AFES=00 (must match FFI in reg 0x5C, default=00=6 samples),
    // BVA=10 (baseline = autoconfig value, lower 3 bits cleared), ARE+ACE enabled.
    // Was 0x4B (AFES=01=10 samples) — mismatch caused wrong CDC/CDT calculation.
    writeRegister(addr, MPR121_AUTOCONFIG0, 0x0B);
    delay(20);

    // Start run mode: 12 electrodes, CL=00 (initial baseline = current value
    // in baseline register, which is pre-seeded to TL above). CL=10 doesn't
    // help because it fires before autoconfig finishes finding CDC/CDT values.
    writeRegister(addr, MPR121_ECR, 0x0C);

    // Wait for autoconfig to complete by polling per-electrode CDC registers.
    // Autoconfig fills these (0x5F-0x6A) as it finds valid CDC for each channel.
    // Before autoconfig: 0 (zeroed above). After: the found charge current (1-63 µA).
    {
      unsigned long acStart = millis();
      const unsigned long AUTOCONFIG_TIMEOUT_MS = 5000;
      bool allDone = false;
      while (!allDone && (millis() - acStart < AUTOCONFIG_TIMEOUT_MS)) {
        allDone = true;
        for (uint8_t e = 0; e < MPR121_CHANNELS; e++) {
          if ((readRegister(addr, 0x5F + e) & 0x3F) == 0) {
            allDone = false;
            break;
          }
        }
        if (!allDone) delay(50);
      }
      #if DEBUG_SERIAL
      Serial.print("[KB] MPR121 #"); Serial.print(sensorIndex + 1);
      Serial.print(" autoconfig ");
      Serial.println(allDone ? "complete" : "TIMEOUT");
      Serial.print("[KB]   Duration: "); Serial.print(millis() - acStart); Serial.println("ms");
      #endif
    }

    // Check autoconfig Out-of-Range status
    uint8_t oor0 = readRegister(addr, MPR121_OOR_STATUS0);
    uint8_t oor1 = readRegister(addr, MPR121_OOR_STATUS1);
    if (oor0 || (oor1 & 0x0F)) {
      #if DEBUG_SERIAL
      Serial.print("[KB] WARNING: MPR121 #");
      Serial.print(sensorIndex + 1);
      Serial.print(" autoconfig OOR! ELE0-7: 0x");
      Serial.print(oor0, HEX);
      Serial.print(", ELE8-11: 0x");
      Serial.println(oor1 & 0x0F, HEX);
      if (oor1 & 0x40) Serial.println("[KB]   ACFF: initial config failed");
      if (oor1 & 0x80) Serial.println("[KB]   ARFF: reconfig failed");
      #endif
    }

    #if DEBUG_SERIAL
    Serial.print("[KB] MPR121 #");
    Serial.print(sensorIndex + 1);
    Serial.println(" configured OK");
    // Per-electrode CDC/CDT readback
    Serial.print("[KB]   CDC: ");
    for (uint8_t e = 0; e < MPR121_CHANNELS; e++) {
      Serial.print(readRegister(addr, 0x5F + e) & 0x3F);
      Serial.print(e < MPR121_CHANNELS - 1 ? "," : "\n");
    }
    Serial.print("[KB]   CDT: ");
    for (uint8_t e = 0; e < MPR121_CHANNELS; e++) {
      // CDT registers: 0x6C-0x72, two channels per byte (bits 2:0 and 7:5)
      uint8_t reg = readRegister(addr, 0x6C + e / 2);
      uint8_t cdt = (e % 2 == 0) ? (reg & 0x07) : ((reg >> 4) & 0x07);
      Serial.print(cdt);
      Serial.print(e < MPR121_CHANNELS - 1 ? "," : "\n");
    }
    #endif
  }

  currentTargetBaseline = targetBaseline;
  isInitialized = true;

  delay(100);  // Final stabilization
  pollAllSensorData();
  delay(50);

  #if DEBUG_SERIAL
  Serial.println("[KB] Autoconfig complete.");
  #endif
  return true;
}

// =================================================================
// Sensor Data Polling — 4 sensors, 48 keys
// =================================================================

void CapacitiveKeyboard::pollAllSensorData() {
  const uint8_t START_REGISTER = 0x04;
  const uint8_t BYTES_TO_READ  = 38;
  const uint8_t sensor_addrs[] = { ADDR_MPR121_A, ADDR_MPR121_B,
                                    ADDR_MPR121_C, ADDR_MPR121_D };
  int keyOffset = 0;

  for (int s = 0; s < NUM_SENSORS; s++) {
    uint8_t addr = sensor_addrs[s];
    bool success = false;

    // Try up to 2 attempts (1 retry on failure)
    for (int attempt = 0; attempt < 2 && !success; attempt++) {
      Wire.beginTransmission(addr);
      Wire.write(START_REGISTER);
      uint8_t txErr = Wire.endTransmission(false);
      if (txErr != 0) continue;

      if (Wire.requestFrom(addr, BYTES_TO_READ) == BYTES_TO_READ) {
        for (int i = 0; i < 12; i++) {
          filteredData[i + keyOffset] = Wire.read() | (Wire.read() << 8);
        }
        Wire.read();
        Wire.read();
        for (int i = 0; i < 12; i++) {
          baselineData[i + keyOffset] = Wire.read() << 2;
        }
        success = true;
      }
    }

    if (success) {
      _i2cFailCount[s] = 0;
      _sensorFailed[s] = false;
    } else {
      if (_i2cFailCount[s] < 255) _i2cFailCount[s]++;
      if (_i2cFailCount[s] >= I2C_MAX_CONSECUTIVE_FAILURES) {
        _sensorFailed[s] = true;
        // Force all 12 keys for this sensor to "released"
        for (int i = 0; i < CHANNELS_PER_SENSOR; i++) {
          keyIsPressed[i + keyOffset] = false;
          slewedPressure[i + keyOffset] = 0.0f;
          smoothedPressure[i + keyOffset] = 0.0f;
          for (int j = 0; j < AFTERTOUCH_SMOOTHING_WINDOW_SIZE; j++) {
            pressureHistory[i + keyOffset][j] = 0.0f;
          }
        }
      }
    }

    keyOffset += CHANNELS_PER_SENSOR;
  }
}

// =================================================================
// Calibration Persistence — ESP32 Preferences (NVS)
// =================================================================

void CapacitiveKeyboard::saveCalibrationData() {
  Preferences prefs;
  prefs.begin(CAL_PREFERENCES_NAMESPACE, false);  // Read-write

  CalDataStore data;
  data.magic = EEPROM_MAGIC;
  data.version = EEPROM_VERSION;
  data.reserved = 0;
  data.target_baseline = currentTargetBaseline;
  memcpy(data.maxDelta, calibrationMaxDelta, sizeof(calibrationMaxDelta));

  size_t written = prefs.putBytes(CAL_PREFERENCES_KEY, &data, sizeof(CalDataStore));
  prefs.end();

  #if DEBUG_SERIAL
  if (written == sizeof(CalDataStore)) {
    Serial.println("[KB] Calibration saved to NVS.");
  } else {
    Serial.print("[KB] ERROR: NVS write failed! Wrote ");
    Serial.print(written);
    Serial.print(" of ");
    Serial.print(sizeof(CalDataStore));
    Serial.println(" bytes.");
  }
  #endif
}

void CapacitiveKeyboard::loadCalibrationData() {
  Preferences prefs;
  prefs.begin(CAL_PREFERENCES_NAMESPACE, true);  // Read-only

  CalDataStore data;
  size_t len = prefs.getBytes(CAL_PREFERENCES_KEY, &data, sizeof(CalDataStore));
  prefs.end();

  if (len == sizeof(CalDataStore) && data.magic == EEPROM_MAGIC
      && data.version == EEPROM_VERSION) {
    memcpy(calibrationMaxDelta, data.maxDelta, sizeof(calibrationMaxDelta));
    currentTargetBaseline = data.target_baseline;
    #if DEBUG_SERIAL
    Serial.println("[KB] Valid calibration loaded from NVS.");
    #endif
  } else {
    #if DEBUG_SERIAL
    Serial.println("------------------------------------------------------------");
    Serial.println("[KB] WARNING: No valid calibration found in NVS.");
    Serial.println("[KB] Using default settings. Calibration recommended.");
    Serial.println("------------------------------------------------------------");
    #endif
  }
  calculateAdaptiveThresholds();
}

// =================================================================
// Adaptive Thresholds
// =================================================================

void CapacitiveKeyboard::calculateAdaptiveThresholds() {
  for (int i = 0; i < NUM_KEYS; i++) {
    uint16_t pressT = (uint16_t)(calibrationMaxDelta[i] * _pressThresholdPct);
    uint16_t releaseT = (uint16_t)(calibrationMaxDelta[i] * _releaseThresholdPct);
    pressThresholds[i] = max(pressT, MIN_PRESS_THRESHOLD);
    releaseThresholds[i] = max(releaseT, MIN_RELEASE_THRESHOLD);
    if (releaseThresholds[i] >= pressThresholds[i]) {
      releaseThresholds[i] = pressThresholds[i] > 1 ? pressThresholds[i] - 1 : 0;
    }
  }
}

// =================================================================
// Auto-Reconfiguration Control
// =================================================================

void CapacitiveKeyboard::setAutoReconfigEnabled(bool enabled) {
  const uint8_t sensor_addrs[] = { ADDR_MPR121_A, ADDR_MPR121_B,
                                    ADDR_MPR121_C, ADDR_MPR121_D };
  // enabled:  0x0B = AFES=00, RETRY=00, BVA=10, ARE=1, ACE=1
  // disabled: 0x08 = AFES=00, RETRY=00, BVA=10, ARE=0, ACE=0
  // ACE must be 0 when disabling, otherwise stop→run transition
  // triggers a full autoconfig cycle (NXP datasheet: "Autoconfiguration
  // operates each time the MPR121 transitions from Stop Mode to Run Mode").
  uint8_t val = enabled ? 0x0B : 0x08;
  for (int s = 0; s < NUM_SENSORS; s++) {
    writeRegister(sensor_addrs[s], MPR121_ECR, 0x00);
    writeRegister(sensor_addrs[s], MPR121_AUTOCONFIG0, val);
    writeRegister(sensor_addrs[s], MPR121_ECR, 0x0C);
  }
}

// =================================================================
// Debug / Calibration Helpers
// =================================================================

void CapacitiveKeyboard::logFullBaselineTable() {
  Serial.println("\n--- Current Baselines ---");
  for (int i = 0; i < NUM_KEYS; ++i) {
    Serial.print(baselineData[i]);
    if (i != NUM_KEYS - 1) {
      Serial.print((i % 12 == 11) ? "\n" : "\t");
    }
  }
  Serial.println("\n-------------------------");
}

void CapacitiveKeyboard::getBaselineData(uint16_t* destArray) {
  memcpy(destArray, baselineData, sizeof(uint16_t) * NUM_KEYS);
}

uint16_t CapacitiveKeyboard::getFilteredData(int key) {
  if (key >= 0 && key < NUM_KEYS) {
    return filteredData[key];
  }
  return 0;
}

void CapacitiveKeyboard::setCalibrationMaxDelta(int key, uint16_t delta) {
  if (key >= 0 && key < NUM_KEYS) {
    calibrationMaxDelta[key] = delta;
  }
}

uint16_t CapacitiveKeyboard::getTargetBaseline() const {
  return currentTargetBaseline;
}
