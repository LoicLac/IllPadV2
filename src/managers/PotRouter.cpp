#include "PotRouter.h"
#include <Arduino.h>

// =================================================================
// ADC pin mapping: pot index → GPIO
// =================================================================
const uint8_t PotRouter::POT_PINS[NUM_POTS] = {
  POT_RIGHT1_PIN,  // 0 = Right 1
  POT_RIGHT2_PIN,  // 1 = Right 2
  POT_RIGHT3_PIN,  // 2 = Right 3
  POT_RIGHT4_PIN,  // 3 = Right 4
  POT_REAR_PIN     // 4 = Rear
};

// =================================================================
// Binding Table — declarative pot routing
// Pot indices: 0=Right1, 1=Right2, 2=Right3, 3=Right4, 4=Rear
// Button mask: bit0=left, bit1=rear
// =================================================================
const PotBinding PotRouter::BINDINGS[] = {
  // --- Pot Right 1 (tempo / division) ---
  // [0] NORMAL alone → Tempo
  {0, 0b00, BANK_NORMAL, TARGET_TEMPO_BPM,          TEMPO_BPM_MIN, TEMPO_BPM_MAX},
  // [1] ARPEG alone → Tempo
  {0, 0b00, BANK_ARPEG,  TARGET_TEMPO_BPM,          TEMPO_BPM_MIN, TEMPO_BPM_MAX},
  // [2] ARPEG + hold left → Division
  {0, 0b01, BANK_ARPEG,  TARGET_DIVISION,            0, 8},

  // --- Pot Right 2 (shape-gate / deadzone-swing) ---
  // [3] NORMAL alone → Response shape
  {1, 0b00, BANK_NORMAL, TARGET_RESPONSE_SHAPE,      0, 4095},
  // [4] NORMAL + hold left → AT deadzone
  {1, 0b01, BANK_NORMAL, TARGET_AT_DEADZONE,         AT_DEADZONE_MIN, AT_DEADZONE_MAX},
  // [5] ARPEG alone → Gate length
  {1, 0b00, BANK_ARPEG,  TARGET_GATE_LENGTH,         0, 4095},
  // [6] ARPEG + hold left → Swing
  {1, 0b01, BANK_ARPEG,  TARGET_SWING,               0, 4095},

  // --- Pot Right 3 (slew-pattern / pitchbend-octave) ---
  // [7] NORMAL alone → Slew rate
  {2, 0b00, BANK_NORMAL, TARGET_SLEW_RATE,           SLEW_RATE_MIN, SLEW_RATE_MAX},
  // [8] NORMAL + hold left → Pitch bend
  {2, 0b01, BANK_NORMAL, TARGET_PITCH_BEND,          0, 16383},
  // [9] ARPEG alone → Pattern
  {2, 0b00, BANK_ARPEG,  TARGET_PATTERN,             0, 4},
  // [10] ARPEG + hold left → Octave range
  {2, 0b01, BANK_ARPEG,  TARGET_OCTAVE_RANGE,        1, 4},

  // --- Pot Right 4 (base velocity / velocity variation) ---
  // [11] NORMAL alone → Base velocity
  {3, 0b00, BANK_NORMAL, TARGET_BASE_VELOCITY,       1, 127},
  // [12] NORMAL + hold left → Velocity variation
  {3, 0b01, BANK_NORMAL, TARGET_VELOCITY_VARIATION,  0, 100},
  // [13] ARPEG alone → Base velocity
  {3, 0b00, BANK_ARPEG,  TARGET_BASE_VELOCITY,       1, 127},
  // [14] ARPEG + hold left → Velocity variation
  {3, 0b01, BANK_ARPEG,  TARGET_VELOCITY_VARIATION,  0, 100},

  // --- Pot Rear (LED brightness / pad sensitivity) ---
  // [15] Alone → LED brightness
  {4, 0b00, BANK_ANY,    TARGET_LED_BRIGHTNESS,      0, 255},
  // [16] + hold rear → Pad sensitivity
  {4, 0b10, BANK_ANY,    TARGET_PAD_SENSITIVITY,     PAD_SENSITIVITY_MIN, PAD_SENSITIVITY_MAX},
};

const uint8_t PotRouter::NUM_BINDINGS = sizeof(BINDINGS) / sizeof(BINDINGS[0]);

// =================================================================
// Constructor
// =================================================================
PotRouter::PotRouter()
  : _responseShape(RESPONSE_SHAPE_DEFAULT)
  , _slewRate(SLEW_RATE_DEFAULT)
  , _atDeadzone(AT_DEADZONE_DEFAULT)
  , _pitchBend(DEFAULT_PITCH_BEND_OFFSET)
  , _gateLength(0.5f)
  , _swing(0.5f)
  , _division(DIV_1_8)
  , _pattern(ARP_UP)
  , _octaveRange(1)
  , _baseVelocity(DEFAULT_BASE_VELOCITY)
  , _velocityVariation(DEFAULT_VELOCITY_VARIATION)
  , _tempoBPM(TEMPO_BPM_DEFAULT)
  , _ledBrightness(128)
  , _padSensitivity(PAD_SENSITIVITY_DEFAULT)
  , _bargraphDirty(false)
  , _bargraphLevel(0)
  , _dirty(false)
{
  for (uint8_t i = 0; i < NUM_POTS; i++) {
    _rawAdc[i] = 0;
    _smoothedAdc[i] = 0.0f;
    _moved[i] = false;
    _activeIdx[i] = -1;
  }
  for (uint8_t i = 0; i < MAX_BINDINGS; i++) {
    _catch[i].storedValue = 2048;  // Mid-range default
    _catch[i].caught = false;
  }
}

// =================================================================
// begin — configure ADC, seed smoothed values
// =================================================================
void PotRouter::begin() {
  for (uint8_t i = 0; i < NUM_POTS; i++) {
    pinMode(POT_PINS[i], INPUT);
    uint16_t raw = analogRead(POT_PINS[i]);
    _rawAdc[i] = raw;
    _smoothedAdc[i] = (float)raw;
  }

  // Seed stored values from current output defaults (converted to ADC space)
  // Tempo: binding [0] and [1]
  _catch[0].storedValue = (uint16_t)(((float)(_tempoBPM - TEMPO_BPM_MIN) / (TEMPO_BPM_MAX - TEMPO_BPM_MIN)) * 4095.0f);
  _catch[1].storedValue = _catch[0].storedValue;  // Same param
  // Division: binding [2]
  _catch[2].storedValue = (uint16_t)(((float)_division / 8.0f) * 4095.0f);
  // Response shape: binding [3]
  _catch[3].storedValue = (uint16_t)(_responseShape * 4095.0f);
  // AT deadzone: binding [4]
  _catch[4].storedValue = (uint16_t)(((float)(_atDeadzone - AT_DEADZONE_MIN) / (AT_DEADZONE_MAX - AT_DEADZONE_MIN)) * 4095.0f);
  // Gate length: binding [5]
  _catch[5].storedValue = (uint16_t)(_gateLength * 4095.0f);
  // Swing: binding [6]
  _catch[6].storedValue = (uint16_t)(_swing * 4095.0f);
  // Slew rate: binding [7]
  _catch[7].storedValue = (uint16_t)(((float)(_slewRate - SLEW_RATE_MIN) / (SLEW_RATE_MAX - SLEW_RATE_MIN)) * 4095.0f);
  // Pitch bend: binding [8]
  _catch[8].storedValue = (uint16_t)(((float)_pitchBend / 16383.0f) * 4095.0f);
  // Pattern: binding [9]
  _catch[9].storedValue = (uint16_t)(((float)_pattern / 4.0f) * 4095.0f);
  // Octave range: binding [10]
  _catch[10].storedValue = (uint16_t)(((float)(_octaveRange - 1) / 3.0f) * 4095.0f);
  // Base velocity: bindings [11] and [13]
  _catch[11].storedValue = (uint16_t)(((float)(_baseVelocity - 1) / 126.0f) * 4095.0f);
  _catch[13].storedValue = _catch[11].storedValue;
  // Velocity variation: bindings [12] and [14]
  _catch[12].storedValue = (uint16_t)(((float)_velocityVariation / 100.0f) * 4095.0f);
  _catch[14].storedValue = _catch[12].storedValue;
  // LED brightness: binding [15]
  _catch[15].storedValue = (uint16_t)(((float)_ledBrightness / 255.0f) * 4095.0f);
  // Pad sensitivity: binding [16]
  _catch[16].storedValue = (uint16_t)(((float)(_padSensitivity - PAD_SENSITIVITY_MIN) / (PAD_SENSITIVITY_MAX - PAD_SENSITIVITY_MIN)) * 4095.0f);

  #if DEBUG_SERIAL
  Serial.printf("[POT] %d bindings, %d pots initialized\n", NUM_BINDINGS, NUM_POTS);
  #endif
}

// =================================================================
// update — main entry point, called every loop iteration
// =================================================================
void PotRouter::update(bool btnLeft, bool btnRear, BankType currentType) {
  readAndSmooth();
  resolveBindings(btnLeft, btnRear, currentType);

  for (uint8_t p = 0; p < NUM_POTS; p++) {
    if (_activeIdx[p] >= 0 && _moved[p]) {
      applyBinding(p);
    }
  }
}

// =================================================================
// readAndSmooth — read ADCs, EMA smooth, detect movement
// =================================================================
void PotRouter::readAndSmooth() {
  for (uint8_t i = 0; i < NUM_POTS; i++) {
    _rawAdc[i] = analogRead(POT_PINS[i]);
    float prev = _smoothedAdc[i];
    _smoothedAdc[i] += POT_SMOOTHING_ALPHA * ((float)_rawAdc[i] - _smoothedAdc[i]);

    float delta = _smoothedAdc[i] - prev;
    if (delta < 0) delta = -delta;
    _moved[i] = (delta > 1.0f);  // Meaningful movement (not just noise)
  }
}

// =================================================================
// resolveBindings — find best matching binding for each pot
// =================================================================
void PotRouter::resolveBindings(bool btnLeft, bool btnRear, BankType type) {
  uint8_t currentMask = 0;
  if (btnLeft) currentMask |= 0x01;
  if (btnRear) currentMask |= 0x02;

  for (uint8_t p = 0; p < NUM_POTS; p++) {
    int8_t bestIdx = -1;
    uint8_t bestScore = 0;

    for (uint8_t b = 0; b < NUM_BINDINGS; b++) {
      const PotBinding& bind = BINDINGS[b];
      if (bind.potIndex != p) continue;

      // Button mask must match exactly
      if (bind.buttonMask != currentMask) {
        // Exception: rear pot uses only bit1, right pots use only bit0
        // Rear pot (index 4): check bit1 only
        if (p == 4) {
          if ((bind.buttonMask & 0x02) != (currentMask & 0x02)) continue;
        } else {
          // Right pots (0-3): check bit0 only
          if ((bind.buttonMask & 0x01) != (currentMask & 0x01)) continue;
        }
      }

      // Bank type must match or be BANK_ANY
      if (bind.bankType != BANK_ANY && bind.bankType != type) continue;

      // Score: prefer exact bank type match over ANY, prefer button match
      uint8_t score = 1;
      if (bind.bankType == type) score += 2;
      if (bind.buttonMask == currentMask) score += 1;

      if (score > bestScore) {
        bestScore = score;
        bestIdx = (int8_t)b;
      }
    }

    // If active binding changed, reset catch for old one
    if (bestIdx != _activeIdx[p] && _activeIdx[p] >= 0) {
      _catch[_activeIdx[p]].caught = false;
    }
    _activeIdx[p] = bestIdx;
  }
}

// =================================================================
// applyBinding — catch check + value conversion + output write
// =================================================================
void PotRouter::applyBinding(uint8_t potIndex) {
  int8_t idx = _activeIdx[potIndex];
  if (idx < 0) return;

  CatchState& cs = _catch[idx];
  float adc = _smoothedAdc[potIndex];

  // --- Catch system ---
  if (!cs.caught) {
    float diff = adc - (float)cs.storedValue;
    if (diff < 0) diff = -diff;
    if (diff <= POT_CATCH_WINDOW) {
      cs.caught = true;
      #if DEBUG_SERIAL
      Serial.printf("[POT] Binding %d caught at ADC %d\n", idx, (int)adc);
      #endif
    } else {
      // Show uncaught bargraph at stored position
      _bargraphLevel = (uint8_t)((cs.storedValue * 8 + 2048) / 4096);
      _bargraphDirty = true;
      return;  // Not caught yet — don't apply
    }
  }

  // --- Caught: convert ADC to target value and write ---
  const PotBinding& bind = BINDINGS[idx];

  switch (bind.target) {
    case TARGET_RESPONSE_SHAPE:
      _responseShape = adcToFloat(adc);
      break;
    case TARGET_SLEW_RATE:
      _slewRate = adcToRange(adc, bind.rangeMin, bind.rangeMax);
      break;
    case TARGET_AT_DEADZONE:
      _atDeadzone = adcToRange(adc, bind.rangeMin, bind.rangeMax);
      break;
    case TARGET_PITCH_BEND:
      _pitchBend = adcToRange(adc, bind.rangeMin, bind.rangeMax);
      break;
    case TARGET_GATE_LENGTH:
      _gateLength = adcToFloat(adc);
      break;
    case TARGET_SWING: {
      // 0.5 to 0.75
      float norm = adcToFloat(adc);
      _swing = 0.5f + norm * 0.25f;
      break;
    }
    case TARGET_DIVISION: {
      // 9 discrete values (0-8) mapped from ADC range
      uint8_t div = (uint8_t)adcToRange(adc, 0, 8);
      if (div > 8) div = 8;
      _division = (ArpDivision)div;
      break;
    }
    case TARGET_PATTERN: {
      // 5 discrete values (0-4) mapped from ADC range
      uint8_t pat = (uint8_t)adcToRange(adc, 0, 4);
      if (pat > 4) pat = 4;
      _pattern = (ArpPattern)pat;
      break;
    }
    case TARGET_OCTAVE_RANGE: {
      uint8_t oct = (uint8_t)adcToRange(adc, 1, 4);
      _octaveRange = oct;
      break;
    }
    case TARGET_BASE_VELOCITY:
      _baseVelocity = (uint8_t)adcToRange(adc, bind.rangeMin, bind.rangeMax);
      break;
    case TARGET_VELOCITY_VARIATION:
      _velocityVariation = (uint8_t)adcToRange(adc, bind.rangeMin, bind.rangeMax);
      break;
    case TARGET_TEMPO_BPM:
      _tempoBPM = adcToRange(adc, bind.rangeMin, bind.rangeMax);
      break;
    case TARGET_LED_BRIGHTNESS:
      _ledBrightness = (uint8_t)adcToRange(adc, bind.rangeMin, bind.rangeMax);
      break;
    case TARGET_PAD_SENSITIVITY:
      _padSensitivity = (uint8_t)adcToRange(adc, bind.rangeMin, bind.rangeMax);
      break;
    case TARGET_NONE:
      return;
  }

  // Update stored value for this binding (for future catch)
  cs.storedValue = (uint16_t)adc;

  // Bargraph: show current level (0-8 LEDs)
  _bargraphLevel = (uint8_t)(adc * 8.0f / 4096.0f);
  if (_bargraphLevel > 8) _bargraphLevel = 8;
  _bargraphDirty = true;
  _dirty = true;
}

// =================================================================
// resetPerBankCatch — uncatch all per-bank bindings (on bank switch)
// =================================================================
void PotRouter::resetPerBankCatch() {
  for (uint8_t i = 0; i < NUM_BINDINGS; i++) {
    if (isPerBankTarget(BINDINGS[i].target)) {
      _catch[i].caught = false;
    }
  }
}

// =================================================================
// Helpers
// =================================================================

uint16_t PotRouter::adcToRange(float adc, uint16_t lo, uint16_t hi) const {
  float norm = adc / 4095.0f;
  if (norm < 0.0f) norm = 0.0f;
  if (norm > 1.0f) norm = 1.0f;
  return lo + (uint16_t)(norm * (float)(hi - lo) + 0.5f);
}

float PotRouter::adcToFloat(float adc) const {
  float v = adc / 4095.0f;
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

bool PotRouter::isPerBankTarget(PotTarget t) const {
  switch (t) {
    case TARGET_PITCH_BEND:
    case TARGET_GATE_LENGTH:
    case TARGET_SWING:
    case TARGET_DIVISION:
    case TARGET_PATTERN:
    case TARGET_OCTAVE_RANGE:
    case TARGET_BASE_VELOCITY:
    case TARGET_VELOCITY_VARIATION:
      return true;
    default:
      return false;
  }
}

// =================================================================
// Getters
// =================================================================
float       PotRouter::getResponseShape() const       { return _responseShape; }
uint16_t    PotRouter::getSlewRate() const             { return _slewRate; }
uint16_t    PotRouter::getAtDeadzone() const           { return _atDeadzone; }
uint16_t    PotRouter::getPitchBend() const            { return _pitchBend; }
float       PotRouter::getGateLength() const           { return _gateLength; }
float       PotRouter::getSwing() const                { return _swing; }
ArpDivision PotRouter::getDivision() const             { return _division; }
ArpPattern  PotRouter::getPattern() const              { return _pattern; }
uint8_t     PotRouter::getOctaveRange() const          { return _octaveRange; }
uint8_t     PotRouter::getBaseVelocity() const         { return _baseVelocity; }
uint8_t     PotRouter::getVelocityVariation() const    { return _velocityVariation; }
uint16_t    PotRouter::getTempoBPM() const             { return _tempoBPM; }
uint8_t     PotRouter::getLedBrightness() const        { return _ledBrightness; }
uint8_t     PotRouter::getPadSensitivity() const       { return _padSensitivity; }

bool PotRouter::hasBargraphUpdate() {
  if (_bargraphDirty) {
    _bargraphDirty = false;
    return true;
  }
  return false;
}

uint8_t PotRouter::getBargraphLevel() const { return _bargraphLevel; }

bool PotRouter::isDirty() const   { return _dirty; }
void PotRouter::clearDirty()      { _dirty = false; }
