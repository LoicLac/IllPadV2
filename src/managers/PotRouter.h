#ifndef POT_ROUTER_H
#define POT_ROUTER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

// PotTarget, PotMapping, PotMappingStore are defined in KeyboardData.h

// =================================================================
// PotBinding — declarative: pot + button combo + bank type → target
// =================================================================
struct PotBinding {
  uint8_t   potIndex;    // 0-3 = right 1-4, 4 = rear
  uint8_t   buttonMask;  // 0=none, bit0=left, bit1=rear
  BankType  bankType;    // NORMAL, ARPEG, or BANK_ANY
  PotTarget target;
  uint16_t  rangeMin;
  uint16_t  rangeMax;
  uint8_t   ccNumber;    // Only used when target == TARGET_MIDI_CC
};

// =================================================================
// PotRouter — 5 pots, 2 buttons, configurable binding table
// =================================================================
class PotRouter {
public:
  PotRouter();

  void begin();
  void update(bool btnLeft, bool btnRear, BankType currentType);

  // Reset catch for per-bank params (call on bank switch)
  void resetPerBankCatch();

  // Re-seed catch targets from current output values (call after loadStoredPerBank on bank switch)
  // keepGlobalCatch=true: preserve catch state for global targets (bank switch path)
  void seedCatchValues(bool keepGlobalCatch = false);

  // Load NVS-saved values (call BEFORE begin(), so catch seeds are correct).
  void loadStoredGlobals(float shape, uint16_t slew, uint16_t deadzone,
                         uint16_t tempo, uint8_t ledBright, uint8_t padSens);
  void loadStoredPerBank(uint8_t baseVel, uint8_t velVar, uint16_t pitchBend,
                         float gate, float shuffleDepth, ArpDivision div,
                         ArpPattern pat, uint8_t shuffleTmpl);

  // Load user pot mapping from NVS (call BEFORE begin())
  void loadMapping(const PotMappingStore& store);

  // Get current mapping (for Tool 6 display / NVS save)
  const PotMappingStore& getMapping() const;

  // Apply a new mapping and rebuild binding table (called by Tool 6 on save)
  void applyMapping(const PotMappingStore& store);

  // Getters — internal params
  float       getResponseShape() const;
  uint16_t    getSlewRate() const;
  uint16_t    getAtDeadzone() const;
  uint16_t    getPitchBend() const;
  float       getGateLength() const;
  float       getShuffleDepth() const;
  ArpDivision getDivision() const;
  ArpPattern  getPattern() const;
  uint8_t     getShuffleTemplate() const;
  uint8_t     getBaseVelocity() const;
  uint8_t     getVelocityVariation() const;
  uint16_t    getTempoBPM() const;
  uint8_t     getLedBrightness() const;
  uint8_t     getPadSensitivity() const;

  // Getters — MIDI CC/PB output (Phase 2)
  // Returns true if there's a pending CC to send, fills slot/cc/value
  bool    consumeCC(uint8_t& ccNumber, uint8_t& ccValue);
  bool    consumePitchBend(uint16_t& pbValue);

  // LED bargraph — caller reads and shows on LEDs
  bool    hasBargraphUpdate();
  float   getBargraphLevel() const;      // 0.0-8.0 (fractional = tip LED brightness)
  uint8_t getBargraphPotLevel() const;   // Physical pot position mapped to 0-7
  bool    isBargraphCaught() const;      // True if active binding is caught

  // Dirty flag for NVS save debounce
  bool isDirty() const;
  void clearDirty();

  // POT_PINS[] now in HardwareConfig.h (shared globally)
  // ADC reads now in PotFilter (getStable/hasMoved)

  // Default mappings (public for Tool 6 reset-to-defaults)
  static const PotMappingStore DEFAULT_MAPPING;

private:
  // Runtime binding table (rebuilt from mapping)
  static const uint8_t MAX_BINDINGS = 24;
  PotBinding _bindings[MAX_BINDINGS];
  uint8_t    _numBindings;

  // User-configurable mapping
  PotMappingStore _mapping;

  // Rebuild _bindings[] from _mapping + fixed rear bindings
  void rebuildBindings();

  // Range lookup for a target
  static void getRangeForTarget(PotTarget t, uint16_t& lo, uint16_t& hi);

  // Hardware ADC now in PotFilter (removed: _rawAdc, _smoothedAdc, _stableAdc, _moved)

  // Catch per-binding
  struct CatchState {
    uint16_t storedValue;
    bool     caught;
  };
  CatchState _catch[MAX_BINDINGS];

  // Active binding index per pot (-1 = no match)
  int8_t _activeIdx[NUM_POTS];

  // Output values — internal params
  float       _responseShape;
  uint16_t    _slewRate;
  uint16_t    _atDeadzone;
  uint16_t    _pitchBend;
  float       _gateLength;
  float       _shuffleDepth;
  ArpDivision _division;
  ArpPattern  _pattern;
  uint8_t     _shuffleTemplate;
  uint8_t     _baseVelocity;
  uint8_t     _velocityVariation;
  uint16_t    _tempoBPM;
  uint8_t     _ledBrightness;
  uint8_t     _padSensitivity;

  // Output values — MIDI CC/PB (Phase 2)
  static const uint8_t MAX_CC_SLOTS = 16;  // 8 NORMAL + 8 ARPEG slots can all be CC
  uint8_t  _ccNumber[MAX_CC_SLOTS];
  uint8_t  _ccValue[MAX_CC_SLOTS];
  bool     _ccDirty[MAX_CC_SLOTS];
  uint8_t  _ccBindingIdx[MAX_CC_SLOTS]; // Which binding index maps to this CC slot
  uint8_t  _ccSlotCount;
  uint16_t _midiPitchBend;
  bool     _midiPbDirty;

  // Bargraph
  bool    _bargraphDirty;
  float   _bargraphLevel;       // 0.0-8.0 (fractional tip for continuous, snapped for discrete)
  uint8_t _bargraphPotLevel;   // Physical pot position (0-7)
  bool    _bargraphCaught;     // Catch state of active binding

  // Dirty (any output changed since last clearDirty)
  bool _dirty;

  void resolveBindings(bool btnLeft, bool btnRear, BankType type);
  void applyBinding(uint8_t potIndex);

  // Helpers
  uint16_t adcToRange(float adc, uint16_t lo, uint16_t hi) const;
  float    adcToFloat(float adc) const;
  float    adcToGate(float adc) const;
  bool     isPerBankTarget(PotTarget t) const;
  static uint8_t getDiscreteSteps(PotTarget t);
};

#endif // POT_ROUTER_H
