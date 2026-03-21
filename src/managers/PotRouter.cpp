#include "PotRouter.h"
#include <Arduino.h>
#include <string.h>

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
// Default pot mapping — matches original hardcoded binding table.
// Slot index: [0]=R1 alone, [1]=R1+hold, [2]=R2 alone, [3]=R2+hold,
//             [4]=R3 alone, [5]=R3+hold, [6]=R4 alone, [7]=R4+hold
// =================================================================
const PotMappingStore PotRouter::DEFAULT_MAPPING = {
  EEPROM_MAGIC,   // magic
  POTMAP_VERSION,  // version
  0,               // reserved
  // NORMAL context
  {
    {TARGET_TEMPO_BPM,          0},  // R1 alone
    {TARGET_EMPTY,              0},  // R1+hold (reserved for future)
    {TARGET_RESPONSE_SHAPE,     0},  // R2 alone
    {TARGET_AT_DEADZONE,        0},  // R2+hold
    {TARGET_SLEW_RATE,          0},  // R3 alone
    {TARGET_PITCH_BEND,         0},  // R3+hold
    {TARGET_BASE_VELOCITY,      0},  // R4 alone
    {TARGET_VELOCITY_VARIATION, 0},  // R4+hold
  },
  // ARPEG context
  {
    {TARGET_TEMPO_BPM,          0},  // R1 alone
    {TARGET_DIVISION,           0},  // R1+hold
    {TARGET_GATE_LENGTH,        0},  // R2 alone
    {TARGET_SHUFFLE_DEPTH,      0},  // R2+hold
    {TARGET_PATTERN,            0},  // R3 alone
    {TARGET_SHUFFLE_TEMPLATE,   0},  // R3+hold
    {TARGET_BASE_VELOCITY,      0},  // R4 alone
    {TARGET_VELOCITY_VARIATION, 0},  // R4+hold
  }
};

// =================================================================
// Constructor
// =================================================================
PotRouter::PotRouter()
  : _numBindings(0)
  , _responseShape(RESPONSE_SHAPE_DEFAULT)
  , _slewRate(SLEW_RATE_DEFAULT)
  , _atDeadzone(AT_DEADZONE_DEFAULT)
  , _pitchBend(DEFAULT_PITCH_BEND_OFFSET)
  , _gateLength(0.5f)
  , _shuffleDepth(0.0f)
  , _division(DIV_1_8)
  , _pattern(ARP_UP)
  , _shuffleTemplate(0)
  , _baseVelocity(DEFAULT_BASE_VELOCITY)
  , _velocityVariation(DEFAULT_VELOCITY_VARIATION)
  , _tempoBPM(TEMPO_BPM_DEFAULT)
  , _ledBrightness(128)
  , _padSensitivity(PAD_SENSITIVITY_DEFAULT)
  , _ccSlotCount(0)
  , _midiPitchBend(8192)
  , _midiPbDirty(false)
  , _bargraphDirty(false)
  , _bargraphLevel(0)
  , _dirty(false)
{
  // Init hardware state
  for (uint8_t i = 0; i < NUM_POTS; i++) {
    _rawAdc[i] = 0;
    _smoothedAdc[i] = 0.0f;
    _moved[i] = false;
    _activeIdx[i] = -1;
  }
  for (uint8_t i = 0; i < MAX_BINDINGS; i++) {
    _catch[i].storedValue = 2048;
    _catch[i].caught = false;
  }
  for (uint8_t i = 0; i < MAX_CC_SLOTS; i++) {
    _ccNumber[i] = 0;
    _ccValue[i] = 0;
    _ccDirty[i] = false;
  }

  // Start with default mapping
  memcpy(&_mapping, &DEFAULT_MAPPING, sizeof(PotMappingStore));
  rebuildBindings();
}

// =================================================================
// loadStoredGlobals / loadStoredPerBank — set output values from NVS
// =================================================================
void PotRouter::loadStoredGlobals(float shape, uint16_t slew, uint16_t deadzone,
                                   uint16_t tempo, uint8_t ledBright, uint8_t padSens) {
  _responseShape  = shape;
  _slewRate       = slew;
  _atDeadzone     = deadzone;
  _tempoBPM       = tempo;
  _ledBrightness  = ledBright;
  _padSensitivity = padSens;
}

void PotRouter::loadStoredPerBank(uint8_t baseVel, uint8_t velVar, uint16_t pitchBend,
                                   float gate, float shuffleDepth, ArpDivision div,
                                   ArpPattern pat, uint8_t shuffleTmpl) {
  _baseVelocity      = baseVel;
  _velocityVariation = velVar;
  _pitchBend         = pitchBend;
  _gateLength        = gate;
  _shuffleDepth      = shuffleDepth;
  _division          = div;
  _pattern           = pat;
  _shuffleTemplate   = shuffleTmpl;
}

// =================================================================
// loadMapping — set user pot mapping from NVS (before begin())
// =================================================================
void PotRouter::loadMapping(const PotMappingStore& store) {
  memcpy(&_mapping, &store, sizeof(PotMappingStore));
  rebuildBindings();
}

const PotMappingStore& PotRouter::getMapping() const {
  return _mapping;
}

void PotRouter::applyMapping(const PotMappingStore& store) {
  memcpy(&_mapping, &store, sizeof(PotMappingStore));
  rebuildBindings();
  seedCatchValues();  // R2 fix: re-seed catch for new binding layout
}

// =================================================================
// getRangeForTarget — static lookup of min/max for each target
// =================================================================
void PotRouter::getRangeForTarget(PotTarget t, uint16_t& lo, uint16_t& hi) {
  switch (t) {
    case TARGET_TEMPO_BPM:          lo = TEMPO_BPM_MIN; hi = TEMPO_BPM_MAX; break;
    case TARGET_RESPONSE_SHAPE:     lo = 0; hi = 4095; break;
    case TARGET_SLEW_RATE:          lo = SLEW_RATE_MIN; hi = SLEW_RATE_MAX; break;
    case TARGET_AT_DEADZONE:        lo = AT_DEADZONE_MIN; hi = AT_DEADZONE_MAX; break;
    case TARGET_PITCH_BEND:         lo = 0; hi = 16383; break;
    case TARGET_GATE_LENGTH:        lo = 0; hi = 4095; break;
    case TARGET_SHUFFLE_DEPTH:      lo = 0; hi = 4095; break;
    case TARGET_DIVISION:           lo = 0; hi = 8; break;
    case TARGET_PATTERN:            lo = 0; hi = 4; break;
    case TARGET_SHUFFLE_TEMPLATE:   lo = 0; hi = 4; break;
    case TARGET_BASE_VELOCITY:      lo = 1; hi = 127; break;
    case TARGET_VELOCITY_VARIATION: lo = 0; hi = 100; break;
    case TARGET_LED_BRIGHTNESS:     lo = 0; hi = 255; break;
    case TARGET_PAD_SENSITIVITY:    lo = PAD_SENSITIVITY_MIN; hi = PAD_SENSITIVITY_MAX; break;
    case TARGET_MIDI_CC:            lo = 0; hi = 127; break;
    case TARGET_MIDI_PITCHBEND:     lo = 0; hi = 16383; break;
    default:                        lo = 0; hi = 4095; break;
  }
}

// =================================================================
// rebuildBindings — generate binding table from user mapping
// =================================================================
void PotRouter::rebuildBindings() {
  _numBindings = 0;
  _ccSlotCount = 0;

  // R1 fix: clear all CC state to prevent stale dirty flags
  for (uint8_t i = 0; i < MAX_CC_SLOTS; i++) {
    _ccNumber[i] = 0;
    _ccValue[i] = 0;
    _ccDirty[i] = false;
    _ccBindingIdx[i] = 0xFF;
  }
  _midiPbDirty = false;

  // --- User-configurable right pots (from mapping) ---
  for (uint8_t ctx = 0; ctx < 2; ctx++) {
    BankType btype = (ctx == 0) ? BANK_NORMAL : BANK_ARPEG;
    const PotMapping* map = (ctx == 0) ? _mapping.normalMap : _mapping.arpegMap;

    for (uint8_t slot = 0; slot < POT_MAPPING_SLOTS; slot++) {
      PotTarget target = map[slot].target;
      if (target == TARGET_EMPTY || target == TARGET_NONE) continue;

      uint8_t potIdx    = slot / 2;       // 0-3
      uint8_t btnMask   = (slot & 1) ? 0x01 : 0x00;  // odd=hold-left, even=alone
      uint16_t lo, hi;
      getRangeForTarget(target, lo, hi);

      PotBinding& b = _bindings[_numBindings];
      b.potIndex   = potIdx;
      b.buttonMask = btnMask;
      b.bankType   = btype;
      b.target     = target;
      b.rangeMin   = lo;
      b.rangeMax   = hi;
      b.ccNumber   = map[slot].ccNumber;

      // B1 fix: track CC slot by binding index (not by ccNumber)
      if (target == TARGET_MIDI_CC && _ccSlotCount < MAX_CC_SLOTS) {
        _ccNumber[_ccSlotCount]     = map[slot].ccNumber;
        _ccValue[_ccSlotCount]      = 0;
        _ccDirty[_ccSlotCount]      = false;
        _ccBindingIdx[_ccSlotCount] = _numBindings;
        _ccSlotCount++;
      }

      _numBindings++;
      if (_numBindings >= MAX_BINDINGS) return;
    }
  }

  // --- Fixed rear pot bindings (not user-configurable) ---
  // Rear alone → LED brightness
  if (_numBindings < MAX_BINDINGS) {
    PotBinding& b = _bindings[_numBindings++];
    b = {4, 0b00, BANK_ANY, TARGET_LED_BRIGHTNESS, 0, 255, 0};
  }
  // Rear + hold rear → Pad sensitivity
  if (_numBindings < MAX_BINDINGS) {
    PotBinding& b = _bindings[_numBindings++];
    b = {4, 0b10, BANK_ANY, TARGET_PAD_SENSITIVITY, PAD_SENSITIVITY_MIN, PAD_SENSITIVITY_MAX, 0};
  }

  #if DEBUG_SERIAL
  Serial.printf("[POT] Rebuilt %d bindings from mapping (%d CC slots)\n",
                _numBindings, _ccSlotCount);
  #endif
}

// =================================================================
// seedCatchValues — seed catch from current output values
// Extracted for use by both begin() and applyMapping().
// =================================================================
void PotRouter::seedCatchValues() {
  for (uint8_t i = 0; i < _numBindings; i++) {
    const PotBinding& bind = _bindings[i];
    float norm = 0.5f;  // fallback mid-range

    switch (bind.target) {
      case TARGET_TEMPO_BPM:
        norm = (float)(_tempoBPM - TEMPO_BPM_MIN) / (TEMPO_BPM_MAX - TEMPO_BPM_MIN);
        break;
      case TARGET_RESPONSE_SHAPE:
        norm = _responseShape;
        break;
      case TARGET_SLEW_RATE:
        norm = (float)(_slewRate - SLEW_RATE_MIN) / (SLEW_RATE_MAX - SLEW_RATE_MIN);
        break;
      case TARGET_AT_DEADZONE:
        norm = (float)(_atDeadzone - AT_DEADZONE_MIN) / (AT_DEADZONE_MAX - AT_DEADZONE_MIN);
        break;
      case TARGET_PITCH_BEND:
        norm = (float)_pitchBend / 16383.0f;
        break;
      case TARGET_GATE_LENGTH:
        norm = _gateLength;
        break;
      case TARGET_SHUFFLE_DEPTH:
        norm = _shuffleDepth;
        break;
      case TARGET_DIVISION:
        norm = (float)_division / 8.0f;
        break;
      case TARGET_PATTERN:
        norm = (float)_pattern / 4.0f;
        break;
      case TARGET_SHUFFLE_TEMPLATE:
        norm = (float)_shuffleTemplate / 4.0f;
        break;
      case TARGET_BASE_VELOCITY:
        norm = (float)(_baseVelocity - 1) / 126.0f;
        break;
      case TARGET_VELOCITY_VARIATION:
        norm = (float)_velocityVariation / 100.0f;
        break;
      case TARGET_LED_BRIGHTNESS:
        norm = (float)_ledBrightness / 255.0f;
        break;
      case TARGET_PAD_SENSITIVITY:
        norm = (float)(_padSensitivity - PAD_SENSITIVITY_MIN) / (PAD_SENSITIVITY_MAX - PAD_SENSITIVITY_MIN);
        break;
      case TARGET_MIDI_CC:
        norm = 0.0f;
        break;
      case TARGET_MIDI_PITCHBEND:
        norm = 0.5f;
        break;
      default:
        break;
    }

    _catch[i].storedValue = (uint16_t)(norm * 4095.0f);
    _catch[i].caught = false;
  }
}

// =================================================================
// begin — configure ADC, seed catch values
// =================================================================
void PotRouter::begin() {
  for (uint8_t i = 0; i < NUM_POTS; i++) {
    pinMode(POT_PINS[i], INPUT);
    uint16_t raw = analogRead(POT_PINS[i]);
    _rawAdc[i] = raw;
    _smoothedAdc[i] = (float)raw;
  }

  seedCatchValues();

  #if DEBUG_SERIAL
  Serial.printf("[POT] %d bindings, %d pots initialized\n", _numBindings, NUM_POTS);
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
    _moved[i] = (delta > 1.0f);
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

    for (uint8_t b = 0; b < _numBindings; b++) {
      const PotBinding& bind = _bindings[b];
      if (bind.potIndex != p) continue;

      // Rear modifier only applies to rear pot; right pots ignore rear button
      if (p == 4) {
        if ((bind.buttonMask & 0x02) != (currentMask & 0x02)) continue;
      } else {
        if (currentMask & 0x02) continue;
        if ((bind.buttonMask & 0x01) != (currentMask & 0x01)) continue;
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
    } else {
      _bargraphLevel = (uint8_t)((cs.storedValue * 8 + 2048) / 4096);
      _bargraphDirty = true;
      return;
    }
  }

  // --- Caught: convert ADC to target value and write ---
  const PotBinding& bind = _bindings[idx];

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
    case TARGET_SHUFFLE_DEPTH:
      _shuffleDepth = adcToFloat(adc);
      break;
    case TARGET_DIVISION: {
      uint8_t div = (uint8_t)adcToRange(adc, 0, 8);
      if (div > 8) div = 8;
      _division = (ArpDivision)div;
      break;
    }
    case TARGET_PATTERN: {
      uint8_t pat = (uint8_t)adcToRange(adc, 0, 4);
      if (pat > 4) pat = 4;
      _pattern = (ArpPattern)pat;
      break;
    }
    case TARGET_SHUFFLE_TEMPLATE: {
      uint8_t tmpl = (uint8_t)adcToRange(adc, 0, 4);
      if (tmpl > 4) tmpl = 4;
      _shuffleTemplate = tmpl;
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

    // --- MIDI CC: only mark dirty on value change (no flood) ---
    // B1 fix: lookup by binding index, not ccNumber (avoids cross-context collision)
    case TARGET_MIDI_CC: {
      uint8_t val = (uint8_t)adcToRange(adc, 0, 127);
      for (uint8_t s = 0; s < _ccSlotCount; s++) {
        if (_ccBindingIdx[s] == (uint8_t)idx) {
          if (val != _ccValue[s]) {
            _ccValue[s] = val;
            _ccDirty[s] = true;
          }
          break;
        }
      }
      break;
    }

    // --- MIDI Pitchbend: only mark dirty on value change ---
    case TARGET_MIDI_PITCHBEND: {
      uint16_t val = adcToRange(adc, 0, 16383);
      if (val != _midiPitchBend) {
        _midiPitchBend = val;
        _midiPbDirty = true;
      }
      break;
    }

    case TARGET_EMPTY:
    case TARGET_NONE:
      return;  // No output, no bargraph
  }

  cs.storedValue = (uint16_t)adc;

  // B2 fix: for global targets that appear in both NORMAL and ARPEG (e.g., TEMPO),
  // propagate storedValue to all bindings with the same target on the same pot+button.
  // This prevents stale catch after bank type switch.
  if (!isPerBankTarget(bind.target)) {
    for (uint8_t i = 0; i < _numBindings; i++) {
      if (i != (uint8_t)idx &&
          _bindings[i].target == bind.target &&
          _bindings[i].potIndex == bind.potIndex &&
          _bindings[i].buttonMask == bind.buttonMask) {
        _catch[i].storedValue = (uint16_t)adc;
      }
    }
  }

  _bargraphLevel = (uint8_t)(adc * 8.0f / 4096.0f);
  if (_bargraphLevel > 8) _bargraphLevel = 8;
  _bargraphDirty = true;
  _dirty = true;
}

// =================================================================
// resetPerBankCatch — uncatch all per-bank bindings (on bank switch)
// =================================================================
void PotRouter::resetPerBankCatch() {
  for (uint8_t i = 0; i < _numBindings; i++) {
    if (isPerBankTarget(_bindings[i].target)) {
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
    case TARGET_SHUFFLE_DEPTH:
    case TARGET_DIVISION:
    case TARGET_PATTERN:
    case TARGET_SHUFFLE_TEMPLATE:
    case TARGET_BASE_VELOCITY:
    case TARGET_VELOCITY_VARIATION:
    case TARGET_MIDI_CC:          // CC is per-bank (sends on bank channel)
    case TARGET_MIDI_PITCHBEND:   // PB is per-bank (sends on bank channel)
      return true;
    default:
      return false;
  }
}

// =================================================================
// Getters — internal params
// =================================================================
float       PotRouter::getResponseShape() const       { return _responseShape; }
uint16_t    PotRouter::getSlewRate() const             { return _slewRate; }
uint16_t    PotRouter::getAtDeadzone() const           { return _atDeadzone; }
uint16_t    PotRouter::getPitchBend() const            { return _pitchBend; }
float       PotRouter::getGateLength() const           { return _gateLength; }
float       PotRouter::getShuffleDepth() const         { return _shuffleDepth; }
ArpDivision PotRouter::getDivision() const             { return _division; }
ArpPattern  PotRouter::getPattern() const              { return _pattern; }
uint8_t     PotRouter::getShuffleTemplate() const      { return _shuffleTemplate; }
uint8_t     PotRouter::getBaseVelocity() const         { return _baseVelocity; }
uint8_t     PotRouter::getVelocityVariation() const    { return _velocityVariation; }
uint16_t    PotRouter::getTempoBPM() const             { return _tempoBPM; }
uint8_t     PotRouter::getLedBrightness() const        { return _ledBrightness; }
uint8_t     PotRouter::getPadSensitivity() const       { return _padSensitivity; }

// =================================================================
// MIDI CC/PB consumers — returns true + fills values, clears dirty
// =================================================================
bool PotRouter::consumeCC(uint8_t& ccNumber, uint8_t& ccValue) {
  for (uint8_t s = 0; s < _ccSlotCount; s++) {
    if (_ccDirty[s]) {
      ccNumber = _ccNumber[s];
      ccValue  = _ccValue[s];
      _ccDirty[s] = false;
      return true;
    }
  }
  return false;
}

bool PotRouter::consumePitchBend(uint16_t& pbValue) {
  if (_midiPbDirty) {
    pbValue = _midiPitchBend;
    _midiPbDirty = false;
    return true;
  }
  return false;
}

// =================================================================
// Bargraph + dirty
// =================================================================
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
