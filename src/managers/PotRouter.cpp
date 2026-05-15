#include "PotRouter.h"
#include "../core/PotFilter.h"
#include "../midi/GrooveTemplates.h"
#include <Arduino.h>
#include <string.h>

// Pot reads via MCP3208 SPI in PotFilter (channels 0-4, see HardwareConfig.h)

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
    {TARGET_PATTERN,            0},  // R2+hold
    {TARGET_SHUFFLE_DEPTH,      0},  // R3 alone
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
  , _genPosition(5)         // ARPEG_GEN default (spec §13)
  , _genPosLastZone(0xFF)   // hysteresis : not yet captured
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
  , _bargraphLevel(0.0f)
  , _bargraphPotLevel(0)
  , _bargraphCaught(false)            // F-CODE-1: align init order with .h declaration
  , _dirty(false)
{
  // Init per-pot state (ADC arrays now in PotFilter)
  for (uint8_t i = 0; i < NUM_POTS; i++) {
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
                                   ArpPattern pat, uint8_t shuffleTmpl,
                                   uint8_t genPosition) {
  _baseVelocity      = baseVel;
  _velocityVariation = velVar;
  _pitchBend         = pitchBend;
  _gateLength        = gate;
  _shuffleDepth      = shuffleDepth;
  _division          = div;
  _pattern           = pat;
  _shuffleTemplate   = shuffleTmpl;
  if (genPosition >= NUM_GEN_POSITIONS) genPosition = NUM_GEN_POSITIONS - 1;
  _genPosition       = genPosition;
  _genPosLastZone    = 0xFF;  // bank switch -> recapture hysteresis on next pot move
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
    case TARGET_PATTERN:            lo = 0; hi = NUM_ARP_PATTERNS - 1; break;
    case TARGET_GEN_POSITION:       lo = 0; hi = NUM_GEN_POSITIONS - 1; break;
    case TARGET_SHUFFLE_TEMPLATE:   lo = 0; hi = NUM_SHUFFLE_TEMPLATES - 1; break;
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

  // --- Two-binding strategy (D3, plan §0) — mirror ARPEG slots into ARPEG_GEN context ---
  // For each non-empty slot in arpegMap, add a parallel binding tagged bankType=BANK_ARPEG_GEN.
  // TARGET_PATTERN is substituted with TARGET_GEN_POSITION ; all other targets keep semantics.
  // « Un seul caractere d'arp » cote musicien : si l'utilisateur mappe Tool 7 le slot R2+hold
  // ARPEG context vers MIDI CC, plus de TARGET_PATTERN ni TARGET_GEN_POSITION (coherent).
  for (uint8_t slot = 0; slot < POT_MAPPING_SLOTS; slot++) {
    PotTarget target = _mapping.arpegMap[slot].target;
    if (target == TARGET_EMPTY || target == TARGET_NONE) continue;
    if (_numBindings >= MAX_BINDINGS) break;

    uint8_t potIdx  = slot / 2;
    uint8_t btnMask = (slot & 1) ? 0x01 : 0x00;
    PotTarget effective = (target == TARGET_PATTERN) ? TARGET_GEN_POSITION : target;
    uint16_t lo, hi;
    getRangeForTarget(effective, lo, hi);

    PotBinding& b = _bindings[_numBindings];
    b.potIndex   = potIdx;
    b.buttonMask = btnMask;
    b.bankType   = BANK_ARPEG_GEN;
    b.target     = effective;
    b.rangeMin   = lo;
    b.rangeMax   = hi;
    b.ccNumber   = _mapping.arpegMap[slot].ccNumber;

    if (effective == TARGET_MIDI_CC && _ccSlotCount < MAX_CC_SLOTS) {
      _ccNumber[_ccSlotCount]     = _mapping.arpegMap[slot].ccNumber;
      _ccValue[_ccSlotCount]      = 0;
      _ccDirty[_ccSlotCount]      = false;
      _ccBindingIdx[_ccSlotCount] = _numBindings;
      _ccSlotCount++;
    }

    _numBindings++;
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
void PotRouter::seedCatchValues(bool keepGlobalCatch) {
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
        // Inverse of piecewise: gate 0.005-1.0 → 0-50%, gate 1.0-8.0 → 50-100%
        if (_gateLength <= 1.0f)
          norm = (_gateLength - 0.005f) / 0.995f * 0.5f;
        else
          norm = 0.5f + (_gateLength - 1.0f) / 7.0f * 0.5f;
        break;
      case TARGET_SHUFFLE_DEPTH:
        norm = _shuffleDepth;
        break;
      case TARGET_DIVISION:
        norm = (float)_division / 8.0f;
        break;
      case TARGET_PATTERN:
        norm = (float)_pattern / (float)(NUM_ARP_PATTERNS - 1);
        break;
      case TARGET_GEN_POSITION:
        norm = (float)_genPosition / (float)(NUM_GEN_POSITIONS - 1);
        break;
      case TARGET_SHUFFLE_TEMPLATE:
        norm = (float)_shuffleTemplate / (float)(NUM_SHUFFLE_TEMPLATES - 1);
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
    if (!keepGlobalCatch || isPerBankTarget(_bindings[i].target)) {
      _catch[i].caught = false;
    }
  }
}

// =================================================================
// begin — seed catch values (PotFilter::begin() must be called first)
// =================================================================
void PotRouter::begin() {
  // PotFilter::begin() already called — GPIO init and initial reads done there
  seedCatchValues();

  #if DEBUG_SERIAL
  Serial.printf("[POT] %d bindings, %d pots initialized\n", _numBindings, NUM_POTS);
  #endif
}

// =================================================================
// update — main entry point, called every loop iteration
// =================================================================
void PotRouter::update(bool btnLeft, bool btnRear, BankType currentType) {
  PotFilter::updateAll();
  resolveBindings(btnLeft, btnRear, currentType);

  for (uint8_t p = 0; p < NUM_POTS; p++) {
    if (_activeIdx[p] >= 0 && PotFilter::hasMoved(p)) {
      applyBinding(p);
    }
  }
}

// readAndSmooth() removed — ADC reads now in PotFilter::updateAll()

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

    // Only reset catch when binding changes to a DIFFERENT valid binding
    // (not when going to -1 / no match, e.g. rear button held on right pots)
    if (bestIdx != _activeIdx[p]) {
      if (_activeIdx[p] >= 0 && bestIdx >= 0) {
        _catch[_activeIdx[p]].caught = false;
      }
      if (bestIdx >= 0) {
        _catch[bestIdx].caught = false;
      }
      _activeIdx[p] = bestIdx;
    }
  }
}

// =================================================================
// applyBinding — catch check + value conversion + output write
// =================================================================
void PotRouter::applyBinding(uint8_t potIndex) {
  int8_t idx = _activeIdx[potIndex];
  if (idx < 0) return;

  CatchState& cs = _catch[idx];
  float adc = (float)PotFilter::getStable(potIndex);
  const PotBinding& bind = _bindings[idx];

  // --- Brightness: no catch, no bargraph — direct real-time update ---
  if (bind.target == TARGET_LED_BRIGHTNESS) {
    _ledBrightness = (uint8_t)adcToRange(adc, bind.rangeMin, bind.rangeMax);
    cs.storedValue = (uint16_t)adc;
    cs.caught = true;
    _dirty = true;
    return;
  }

  // --- Catch system ---
  if (!cs.caught) {
    float diff = adc - (float)cs.storedValue;
    if (diff < 0) diff = -diff;
    if (diff <= POT_CATCH_WINDOW) {
      cs.caught = true;
    } else {
      // Bargraph: discrete targets snap to step positions, continuous is smooth
      uint8_t dSteps = getDiscreteSteps(bind.target);
      if (dSteps > 0) {
        uint8_t step = (uint8_t)adcToRange((float)cs.storedValue, 0, dSteps);
        _bargraphLevel = step * 8.0f / (float)dSteps;
      } else {
        _bargraphLevel = cs.storedValue * 8.0f / 4095.0f;
      }
      _bargraphPotLevel = (uint8_t)(adc * 7.0f / 4095.0f + 0.5f);
      _bargraphCaught = false;
      _bargraphDirty = true;
      return;
    }
  }

  // --- Caught: convert ADC to target value and write ---

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
      _gateLength = adcToGate(adc);
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
      uint8_t pat = (uint8_t)adcToRange(adc, 0, NUM_ARP_PATTERNS - 1);
      if (pat >= NUM_ARP_PATTERNS) pat = NUM_ARP_PATTERNS - 1;
      _pattern = (ArpPattern)pat;
      break;
    }
    case TARGET_GEN_POSITION: {
      // Convertir ADC -> zone candidate (0..NUM_GEN_POSITIONS-1).
      uint8_t candidate = (uint8_t)adcToRange(adc, 0, NUM_GEN_POSITIONS - 1);
      if (candidate >= NUM_GEN_POSITIONS) candidate = NUM_GEN_POSITIONS - 1;

      // Task 14 — hysteresis : ne basculer que si l'ADC depasse la frontiere de zone
      // courante de ±HYSTERESIS_LSB (~1.5 % × 4095). Empeche le flicker de zone aux frontieres.
      if (_genPosLastZone == 0xFF) {
        // Premier passage post-bank-switch ou boot : accepte direct.
        _genPosition    = candidate;
        _genPosLastZone = candidate;
      } else if (candidate != _genPosLastZone) {
        static constexpr float HYSTERESIS_LSB = 61.0f;             // ±1.5 % × 4095
        static constexpr float ZONE_WIDTH = 4095.0f / (float)NUM_GEN_POSITIONS;
        static constexpr float ZONE_HALF  = ZONE_WIDTH * 0.5f;
        float zoneCenter   = ((float)_genPosLastZone + 0.5f) * ZONE_WIDTH;
        float distFromCtr  = adc - zoneCenter;
        if (distFromCtr < 0.0f) distFromCtr = -distFromCtr;
        if (distFromCtr > (ZONE_HALF + HYSTERESIS_LSB)) {
          _genPosition    = candidate;
          _genPosLastZone = candidate;
        }
      }
      break;
    }
    case TARGET_SHUFFLE_TEMPLATE: {
      uint8_t tmpl = (uint8_t)adcToRange(adc, 0, NUM_SHUFFLE_TEMPLATES - 1);
      if (tmpl >= NUM_SHUFFLE_TEMPLATES) tmpl = NUM_SHUFFLE_TEMPLATES - 1;
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
    // TARGET_LED_BRIGHTNESS handled by early-return bypass above (no catch, no bargraph)
    case TARGET_PAD_SENSITIVITY:
      _padSensitivity = (uint8_t)adcToRange(adc, bind.rangeMin, bind.rangeMax);
      break;

    // --- MIDI CC: only mark dirty on value change (no flood) ---
    // B1 fix: lookup by binding index, not ccNumber (avoids cross-context collision)
    case TARGET_MIDI_CC: {
      uint8_t val = (uint8_t)adcToRange(adc, 0, 127);
      for (uint8_t s = 0; s < _ccSlotCount; s++) {
        if (_ccBindingIdx[s] == (uint8_t)idx) {
          // Hysteresis: only update when value changes by ≥2 to prevent ADC noise flood
          int8_t diff = (int8_t)val - (int8_t)_ccValue[s];
          if (diff > 1 || diff < -1 || (val == 0 && _ccValue[s] > 0) || (val == 127 && _ccValue[s] < 127)) {
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

  // Bargraph: discrete targets snap to step positions, continuous is smooth
  {
    uint8_t dSteps = getDiscreteSteps(bind.target);
    if (dSteps > 0) {
      uint8_t step = (uint8_t)adcToRange(adc, 0, dSteps);
      _bargraphLevel = step * 8.0f / (float)dSteps;
    } else {
      _bargraphLevel = adc * 8.0f / 4095.0f;
    }
  }
  _bargraphPotLevel = (uint8_t)(adc * 7.0f / 4095.0f + 0.5f);
  _bargraphCaught = true;
  _bargraphDirty = true;
  // Only set NVS dirty for non-volatile targets (CC/PB are volatile, not saved)
  if (bind.target != TARGET_MIDI_CC && bind.target != TARGET_MIDI_PITCHBEND) {
    _dirty = true;
  }
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
  // Clear stale PB dirty flag — prevents old bank's PB firing on new channel
  _midiPbDirty = false;
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

// Piecewise gate mapping: 0-50% pot = 0.005-1.0, 50-100% pot = 1.0-8.0
float PotRouter::adcToGate(float adc) const {
  float norm = adc / 4095.0f;
  if (norm < 0.0f) norm = 0.0f;
  if (norm > 1.0f) norm = 1.0f;
  if (norm <= 0.5f) {
    return 0.005f + norm * (0.995f / 0.5f);  // 0.005 → 1.0
  }
  return 1.0f + (norm - 0.5f) * (7.0f / 0.5f);  // 1.0 → 8.0
}

bool PotRouter::isPerBankTarget(PotTarget t) const {
  switch (t) {
    case TARGET_PITCH_BEND:
    case TARGET_GATE_LENGTH:
    case TARGET_SHUFFLE_DEPTH:
    case TARGET_DIVISION:
    case TARGET_PATTERN:
    case TARGET_GEN_POSITION:     // ARPEG_GEN per-bank grid position
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

// Returns max discrete steps for bargraph snapping, or 0 for continuous targets.
// Uses getRangeForTarget so adding steps only requires changing one place.
uint8_t PotRouter::getDiscreteSteps(PotTarget t) {
  switch (t) {
    case TARGET_DIVISION:
    case TARGET_PATTERN:
    case TARGET_GEN_POSITION:
    case TARGET_SHUFFLE_TEMPLATE: {
      uint16_t lo, hi;
      getRangeForTarget(t, lo, hi);
      return (uint8_t)(hi - lo);
    }
    default:
      return 0;
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
uint8_t     PotRouter::getGenPosition() const          { return _genPosition; }
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

float     PotRouter::getBargraphLevel() const     { return _bargraphLevel; }
uint8_t   PotRouter::getBargraphPotLevel() const { return _bargraphPotLevel; }
bool      PotRouter::isBargraphCaught() const    { return _bargraphCaught; }

bool PotRouter::isDirty() const   { return _dirty; }
void PotRouter::clearDirty()      { _dirty = false; }

uint8_t PotRouter::getSlotForTarget(PotTarget t, bool isArpContext) const {
  const PotMapping* map = isArpContext ? _mapping.arpegMap : _mapping.normalMap;
  for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
    if (map[i].target == t) return i;
  }
  return 0xFF;  // not found
}
