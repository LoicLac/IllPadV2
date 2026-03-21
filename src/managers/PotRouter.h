#ifndef POT_ROUTER_H
#define POT_ROUTER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

// =================================================================
// PotTarget — all possible pot-controlled parameters
// =================================================================
enum PotTarget : uint8_t {
  // NORMAL global
  TARGET_RESPONSE_SHAPE,
  TARGET_SLEW_RATE,
  TARGET_AT_DEADZONE,
  // NORMAL per-bank
  TARGET_PITCH_BEND,
  // ARPEG per-bank
  TARGET_GATE_LENGTH,
  TARGET_SHUFFLE_DEPTH,
  TARGET_DIVISION,
  TARGET_PATTERN,
  TARGET_SHUFFLE_TEMPLATE,
  // Shared per-bank (NORMAL + ARPEG)
  TARGET_BASE_VELOCITY,
  TARGET_VELOCITY_VARIATION,
  // Global
  TARGET_TEMPO_BPM,
  TARGET_LED_BRIGHTNESS,
  TARGET_PAD_SENSITIVITY,
  // Empty slot
  TARGET_NONE
};

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
};

// =================================================================
// PotRouter — 5 pots, 2 buttons, declarative binding table
// =================================================================
class PotRouter {
public:
  PotRouter();

  void begin();
  void update(bool btnLeft, bool btnRear, BankType currentType);

  // Reset catch for per-bank params (call on bank switch)
  void resetPerBankCatch();

  // Load NVS-saved values (call BEFORE begin(), so catch seeds are correct).
  // These set the internal output values that begin() uses for catch seeding.
  void loadStoredGlobals(float shape, uint16_t slew, uint16_t deadzone,
                         uint16_t tempo, uint8_t ledBright, uint8_t padSens);
  void loadStoredPerBank(uint8_t baseVel, uint8_t velVar, uint16_t pitchBend,
                         float gate, float shuffleDepth, ArpDivision div,
                         ArpPattern pat, uint8_t shuffleTmpl);

  // Getters — consumers call these
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

  // LED bargraph — caller reads and shows on LEDs
  bool    hasBargraphUpdate();
  uint8_t getBargraphLevel() const;

  // Dirty flag for NVS save debounce
  bool isDirty() const;
  void clearDirty();

private:
  static const PotBinding BINDINGS[];
  static const uint8_t    NUM_BINDINGS;

  // ADC pin mapping
  static const uint8_t POT_PINS[NUM_POTS];

  // Hardware ADC
  uint16_t _rawAdc[NUM_POTS];
  float    _smoothedAdc[NUM_POTS];
  bool     _moved[NUM_POTS];  // True if pot moved beyond deadzone this frame

  // Catch per-binding
  struct CatchState {
    uint16_t storedValue;  // NVS or default value (in ADC space)
    bool     caught;       // True once physical pot passed through storedValue
  };
  static const uint8_t MAX_BINDINGS = 20;
  CatchState _catch[MAX_BINDINGS];

  // Active binding index per pot (-1 = no match)
  int8_t _activeIdx[NUM_POTS];

  // Output values
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

  // Bargraph
  bool    _bargraphDirty;
  uint8_t _bargraphLevel;

  // Dirty (any output changed since last clearDirty)
  bool _dirty;

  void resolveBindings(bool btnLeft, bool btnRear, BankType type);
  void readAndSmooth();
  void applyBinding(uint8_t potIndex);

  // Helpers
  uint16_t adcToRange(float adc, uint16_t lo, uint16_t hi) const;
  float    adcToFloat(float adc) const;
  bool     isPerBankTarget(PotTarget t) const;
};

#endif // POT_ROUTER_H
