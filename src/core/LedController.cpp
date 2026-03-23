#include "LedController.h"
#include "HardwareConfig.h"
#include "KeyboardData.h"
#include "../arp/ArpEngine.h"
#include <Arduino.h>
#include <math.h>

// LED pin mapping (indices 0-7 correspond to LEDs 1-8, arranged in a circle)
const uint8_t LedController::_pins[NUM_LEDS] = {
  LED_PIN_1, LED_PIN_2, LED_PIN_3, LED_PIN_4,
  LED_PIN_5, LED_PIN_6, LED_PIN_7, LED_PIN_8
};

LedController::LedController()
  : _brightness(255),
    _currentBank(0),
    _batteryLow(false),
    _slots(nullptr),
    _confirmType(CONFIRM_NONE),
    _confirmStart(0),
    _confirmParam(0),
    _potBarDurationMs(LED_BARGRAPH_DURATION_DEFAULT),
    _bootMode(false),
    _bootStep(0),
    _bootFailStep(0),
    _chaseActive(false),
    _chasePos(0),
    _chaseLastStep(0),
    _calibrationMode(false),
    _validationFlashing(false),
    _validationFlashStart(0),
    _error(false),
    _lastBlinkTime(0),
    _blinkState(false),
    _showingBattery(false),
    _batteryLeds(0),
    _batteryDisplayStart(0),
    _showingPotBar(false),
    _potBarLevel(0),
    _potBarStart(0),
    _batLowLastBurstTime(0)
{
  // Precompute sine LUT — 64 entries covering one full period (0-255 output range)
  // Only runs once at boot, no float in update()
  for (uint8_t i = 0; i < 64; i++) {
    _sineTable[i] = (uint8_t)(127.5f + 127.5f * sinf((float)i * 6.2831853f / 64.0f));
  }
  // Init tick flash timers
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    _flashStartTime[i] = 0;
  }
}

void LedController::begin() {
  for (int i = 0; i < NUM_LEDS; i++) {
    analogWrite(_pins[i], 0);  // Allocates LEDC channel per pin (8 pins = 8 channels on ESP32-S3)
  }
}

// ---------------------------------------------------------------
// I2C error halt — 3 rapid flashes + 1s off, loops forever
// ---------------------------------------------------------------

void LedController::haltI2CError() {
  while (true) {
    for (uint8_t flash = 0; flash < 3; flash++) {
      for (int i = 0; i < NUM_LEDS; i++) analogWrite(_pins[i], 255);
      delay(80);
      for (int i = 0; i < NUM_LEDS; i++) analogWrite(_pins[i], 0);
      delay(80);
    }
    delay(1000);
  }
}

// ---------------------------------------------------------------
// Chase pattern — single LED circling (calibration entry feedback)
// ---------------------------------------------------------------

void LedController::startChase() {
  _chaseActive = true;
  _chasePos = 0;
  _chaseLastStep = millis();
  for (int i = 0; i < NUM_LEDS; i++) analogWrite(_pins[i], 0);
  analogWrite(_pins[0], _brightness);
}

void LedController::stopChase() {
  _chaseActive = false;
  for (int i = 0; i < NUM_LEDS; i++) analogWrite(_pins[i], 0);
}

// ---------------------------------------------------------------
// Main update — call from loop() every cycle
// ---------------------------------------------------------------
// Priority-based state machine. Highest active mode wins, others skip.
// All timing via millis() — never blocks.

void LedController::update() {
  unsigned long now = millis();

  // Shared 500ms blink timer (used by error mode)
  if (now - _lastBlinkTime >= 500) {
    _blinkState = !_blinkState;
    _lastBlinkTime = now;
  }

  // === Boot mode (progressive fill / failure blink) ===
  if (_bootMode) {
    if (_bootFailStep > 0) {
      bool fastBlink = ((now / 150) % 2) == 0;
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i < _bootFailStep - 1) {
          analogWrite(_pins[i], _brightness);
        } else if (i == _bootFailStep - 1) {
          analogWrite(_pins[i], fastBlink ? _brightness : 0);
        } else {
          analogWrite(_pins[i], 0);
        }
      }
    } else {
      for (int i = 0; i < NUM_LEDS; i++) {
        analogWrite(_pins[i], (i < _bootStep) ? _brightness : 0);
      }
    }
    return;
  }

  // === Chase pattern (calibration entry) ===
  if (_chaseActive) {
    if (now - _chaseLastStep >= CHASE_STEP_MS) {
      _chasePos = (_chasePos + 1) % NUM_LEDS;
      _chaseLastStep = now;
    }
    for (int i = 0; i < NUM_LEDS; i++) {
      analogWrite(_pins[i], (i == _chasePos) ? _brightness : 0);
    }
    return;
  }

  // === Priority 1: Error (all 8 LEDs blink 500ms unison) ===
  if (_error) {
    uint8_t val = _blinkState ? _brightness : 0;
    for (int i = 0; i < NUM_LEDS; i++) {
      analogWrite(_pins[i], val);
    }
    return;
  }

  // === Priority 2: Battery gauge (8-LED bar, heartbeat pulse, 3s) ===
  if (_showingBattery) {
    if (now - _batteryDisplayStart < BAT_DISPLAY_DURATION_MS) {
      // Asymmetric triangle: fast rise (200ms) + slow fall (800ms) = 1s period
      unsigned long phase = now % 1000;
      uint8_t brightness;
      if (phase < 200) {
        brightness = (uint8_t)((uint32_t)phase * 255 / 200);
      } else {
        brightness = (uint8_t)((uint32_t)(1000 - phase) * 255 / 800);
      }
      for (int i = 0; i < NUM_LEDS; i++) {
        analogWrite(_pins[i], (i < _batteryLeds) ? brightness : 0);
      }
      return;
    }
    _showingBattery = false;
  }

  // === Priority 3: Pot bargraph (solid bar, configurable duration) ===
  if (_showingPotBar) {
    if (now - _potBarStart >= _potBarDurationMs) {
      _showingPotBar = false;
    } else {
      for (int i = 0; i < NUM_LEDS; i++) {
        analogWrite(_pins[i], (i < _potBarLevel) ? _brightness : 0);
      }
      return;
    }
  }

  // === Priority 4: Confirmation blinks (auto-expiring feedback) ===
  if (_confirmType != CONFIRM_NONE) {
    unsigned long elapsed = now - _confirmStart;
    bool active = true;

    switch (_confirmType) {
      case CONFIRM_BANK_SWITCH: {
        // Triple blink all 8 LEDs at reduced brightness
        uint16_t totalMs = (uint16_t)LED_CONFIRM_BANK_PHASES * LED_CONFIRM_UNIT_MS;
        if (elapsed < totalMs) {
          uint8_t phase = elapsed / LED_CONFIRM_UNIT_MS;
          bool on = (phase < LED_CONFIRM_BANK_PHASES) && (phase % 2 == 0);
          uint8_t val = on ? (uint8_t)((uint16_t)_brightness * LED_CONFIRM_BRIGHTNESS_PCT / 100) : 0;
          for (int i = 0; i < NUM_LEDS; i++) analogWrite(_pins[i], val);
        } else {
          _confirmType = CONFIRM_NONE;
          active = false;
        }
        break;
      }
      case CONFIRM_SCALE: {
        // Double blink current bank LED
        uint16_t totalMs = (uint16_t)LED_CONFIRM_SCALE_PHASES * LED_CONFIRM_UNIT_MS;
        if (elapsed < totalMs) {
          uint8_t phase = elapsed / LED_CONFIRM_UNIT_MS;
          bool on = (phase < LED_CONFIRM_SCALE_PHASES) && (phase % 2 == 0);
          for (int i = 0; i < NUM_LEDS; i++) {
            analogWrite(_pins[i], (i == _currentBank && on) ? _brightness : 0);
          }
        } else {
          _confirmType = CONFIRM_NONE;
          active = false;
        }
        break;
      }
      case CONFIRM_HOLD: {
        // Single long blink: on phase + off phase
        if (elapsed < LED_CONFIRM_HOLD_TOTAL_MS) {
          bool on = (elapsed < LED_CONFIRM_HOLD_ON_MS);
          for (int i = 0; i < NUM_LEDS; i++) {
            analogWrite(_pins[i], (i == _currentBank && on) ? _brightness : 0);
          }
        } else {
          _confirmType = CONFIRM_NONE;
          active = false;
        }
        break;
      }
      case CONFIRM_OCTAVE: {
        // Single blink of N LEDs (N = _confirmParam, 1-4)
        uint16_t totalMs = (uint16_t)LED_CONFIRM_OCTAVE_PHASES * LED_CONFIRM_UNIT_MS;
        if (elapsed < totalMs) {
          bool on = (elapsed < LED_CONFIRM_UNIT_MS);
          for (int i = 0; i < NUM_LEDS; i++) {
            analogWrite(_pins[i], (i < _confirmParam && on) ? _brightness : 0);
          }
        } else {
          _confirmType = CONFIRM_NONE;
          active = false;
        }
        break;
      }
      default:
        _confirmType = CONFIRM_NONE;
        active = false;
        break;
    }
    if (active && _confirmType != CONFIRM_NONE) return;
  }

  // === Priority 5: Calibration mode ===
  if (_calibrationMode) {
    if (_validationFlashing) {
      unsigned long elapsed = now - _validationFlashStart;
      if (elapsed >= 150) {
        _validationFlashing = false;
      } else {
        uint8_t phase = elapsed / 25;
        bool on = (phase < 6) && (phase % 2 == 0);
        uint8_t val = on ? _brightness : 0;
        for (int i = 0; i < NUM_LEDS; i++) {
          analogWrite(_pins[i], val);
        }
        return;
      }
    }
    for (int i = 0; i < NUM_LEDS; i++) {
      analogWrite(_pins[i], 0);
    }
    return;
  }

  // === Priority 6: Normal bank display (multi-bank state) ===
  if (_slots) {
    // Sine LUT index: divide period into 64 steps
    const uint8_t lutStep = LED_PULSE_PERIOD_MS / 64;  // ~23ms
    uint8_t sineIdx = (uint8_t)((now / lutStep) % 64);
    uint8_t sineRaw = _sineTable[sineIdx];

    for (int i = 0; i < NUM_LEDS; i++) {
      const BankSlot& slot = _slots[i];
      bool isFg = (i == _currentBank);
      uint8_t ledVal = 0;

      if (slot.type == BANK_NORMAL) {
        if (isFg) {
          // Foreground NORMAL: solid
          ledVal = LED_FG_NORMAL_BRIGHTNESS;

          // Battery low override: 3-blink burst every BAT_LOW_BLINK_INTERVAL_MS
          if (_batteryLow) {
            unsigned long elapsed = now - _batLowLastBurstTime;
            if (elapsed >= BAT_LOW_BLINK_INTERVAL_MS) {
              _batLowLastBurstTime = now;
              elapsed = 0;
            }
            uint32_t burstDuration = (uint32_t)BAT_LOW_BLINK_SPEED_MS * 6;
            if (elapsed < burstDuration) {
              uint8_t phase = elapsed / BAT_LOW_BLINK_SPEED_MS;
              if (phase % 2 != 0) ledVal = 0;
            }
          }
        } else {
          // Background NORMAL: off
          ledVal = LED_BG_NORMAL_BRIGHTNESS;
        }
      } else {
        // ARPEG bank — check play state and tick flash
        bool playing = slot.arpEngine && slot.arpEngine->isPlaying() && slot.arpEngine->hasNotes();

        // Consume tick flash: record start time on fresh tick
        if (slot.arpEngine && slot.arpEngine->consumeTickFlash()) {
          _flashStartTime[i] = now;
        }
        bool flashing = (_flashStartTime[i] != 0) &&
                         ((now - _flashStartTime[i]) < LED_TICK_FLASH_DURATION_MS);

        if (isFg) {
          if (playing) {
            // Foreground ARPEG playing: sine pulse + tick flash spike
            if (flashing) {
              ledVal = LED_FG_ARP_PLAY_FLASH;
            } else {
              ledVal = LED_FG_ARP_PLAY_MIN + (uint8_t)((uint16_t)sineRaw *
                       (LED_FG_ARP_PLAY_MAX - LED_FG_ARP_PLAY_MIN) / 255);
            }
          } else {
            // Foreground ARPEG stopped: sine pulse (wider range)
            ledVal = LED_FG_ARP_STOP_MIN + (uint8_t)((uint16_t)sineRaw *
                     (LED_FG_ARP_STOP_MAX - LED_FG_ARP_STOP_MIN) / 255);
          }
        } else {
          if (playing) {
            // Background ARPEG playing: dimmed sine pulse + dimmed tick flash
            if (flashing) {
              ledVal = LED_BG_ARP_PLAY_FLASH;
            } else {
              ledVal = LED_BG_ARP_PLAY_MIN + (uint8_t)((uint16_t)sineRaw *
                       (LED_BG_ARP_PLAY_MAX - LED_BG_ARP_PLAY_MIN) / 255);
            }
          } else {
            // Background ARPEG stopped: dimmed sine pulse
            ledVal = LED_BG_ARP_STOP_MIN + (uint8_t)((uint16_t)sineRaw *
                     (LED_BG_ARP_STOP_MAX - LED_BG_ARP_STOP_MIN) / 255);
          }
        }
      }

      // Apply global brightness scaling
      uint8_t finalVal = (uint8_t)((uint16_t)ledVal * _brightness / 255);
      analogWrite(_pins[i], finalVal);
    }
  } else {
    // Fallback: no slots pointer, simple single-bank display (pre-init or legacy)
    for (int i = 0; i < NUM_LEDS; i++) {
      analogWrite(_pins[i], (i == _currentBank) ? _brightness : 0);
    }
  }
}

// =================================================================
// Bank Display
// =================================================================

void LedController::setBrightness(uint8_t brightness) {
  _brightness = brightness;
}

void LedController::setCurrentBank(uint8_t bank) {
  if (bank < NUM_BANKS) _currentBank = bank;
}

void LedController::setBatteryLow(bool low) {
  _batteryLow = low;
}

void LedController::setBankSlots(const BankSlot* slots) {
  _slots = slots;
}

// =================================================================
// Confirmation Blinks
// =================================================================

void LedController::triggerConfirm(ConfirmType type, uint8_t param) {
  _confirmType = type;
  _confirmStart = millis();
  _confirmParam = param;
}

void LedController::setPotBarDuration(uint16_t ms) {
  _potBarDurationMs = ms;
}

// =================================================================
// Boot Sequence
// =================================================================

void LedController::showBootProgress(uint8_t step) {
  _bootStep = step;
  _bootMode = true;
}

void LedController::showBootFailure(uint8_t step) {
  _bootFailStep = step;
  _bootMode = true;
}

void LedController::endBoot() {
  _bootMode = false;
  _bootStep = 0;
  _bootFailStep = 0;
}

// =================================================================
// Error
// =================================================================

void LedController::setError(bool error) {
  _error = error;
}

// =================================================================
// Battery Gauge
// =================================================================

void LedController::showBatteryGauge(uint8_t percent) {
  if (percent > 100) percent = 100;
  _batteryLeds = (percent * 8 + 50) / 100;
  _showingBattery = true;
  _batteryDisplayStart = millis();
}

// =================================================================
// Pot Bargraph
// =================================================================

void LedController::showPotBargraph(uint8_t level) {
  uint8_t newLevel = (level > NUM_LEDS) ? NUM_LEDS : level;

  // Ignore calls with identical level — prevents ADC jitter from keeping bar alive
  if (newLevel == _potBarLevel && _showingPotBar) {
    return;
  }

  _potBarStart = millis();
  _potBarLevel = newLevel;
  _showingPotBar = true;
}

// =================================================================
// Calibration Feedback
// =================================================================

void LedController::setCalibrationMode(bool active) {
  _calibrationMode = active;
}

void LedController::playValidation() {
  _validationFlashStart = millis();
  _validationFlashing = true;
}

// =================================================================
// All Off
// =================================================================

void LedController::allOff() {
  for (int i = 0; i < NUM_LEDS; i++) {
    analogWrite(_pins[i], 0);
  }
  _currentBank = 0;
  _batteryLow = false;
  _bootMode = false;
  _bootStep = 0;
  _bootFailStep = 0;
  _chaseActive = false;
  _calibrationMode = false;
  _validationFlashing = false;
  _error = false;
  _showingPotBar = false;
  _showingBattery = false;
  _confirmType = CONFIRM_NONE;
  for (uint8_t i = 0; i < NUM_LEDS; i++) _flashStartTime[i] = 0;
}
