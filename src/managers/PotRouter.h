#ifndef POT_ROUTER_H
#define POT_ROUTER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

enum PotTarget : uint8_t {
  // NORMAL
  TARGET_RESPONSE_SHAPE,
  TARGET_SLEW_RATE,
  TARGET_AT_DEADZONE,
  // ARPEG
  TARGET_GATE_LENGTH,
  TARGET_SWING,
  TARGET_DIVISION,
  TARGET_VELOCITY_VARIATION,
  TARGET_BASE_VELOCITY,
  // Global
  TARGET_TEMPO_BPM,
  TARGET_PAD_SENSITIVITY,
  // Libre
  TARGET_NONE
};

struct PotBinding {
  uint8_t   potIndex;    // 0=left, 1=right, 2=rear
  uint8_t   buttonMask;  // 0=none, bit0=left, bit1=right, bit2=rear
  BankType  bankType;    // NORMAL, ARPEG, or BANK_ANY
  PotTarget target;
  uint16_t  rangeMin;
  uint16_t  rangeMax;
};

class PotRouter {
public:
  PotRouter();

  void begin();
  void update(bool btnL, bool btnR, bool btnRear, BankType currentType);

  // Getters — consumers call these
  float       getResponseShape() const;
  uint16_t    getSlewRate() const;
  uint16_t    getAtDeadzone() const;
  float       getGateLength() const;
  float       getSwing() const;
  ArpDivision getDivision() const;
  uint8_t     getVelocityVariation() const;
  uint8_t     getBaseVelocity() const;
  uint16_t    getTempoBPM() const;
  uint8_t     getPadSensitivity() const;

  // LED bargraph
  bool    hasBargraphUpdate() const;
  uint8_t getBargraphLevel() const;

private:
  static const PotBinding BINDINGS[];
  static const uint8_t    NUM_BINDINGS;

  // Hardware
  uint16_t _rawAdc[NUM_POTS];
  float    _smoothedAdc[NUM_POTS];

  // Catch per-binding
  struct CatchState {
    uint16_t storedValue;
    bool     caught;
  };
  CatchState _catch[12];

  // Active binding per pot
  const PotBinding* _active[NUM_POTS];

  // Output values
  float       _responseShape;
  uint16_t    _slewRate;
  uint16_t    _atDeadzone;
  float       _gateLength;
  float       _swing;
  ArpDivision _division;
  uint8_t     _velocityVariation;
  uint8_t     _baseVelocity;
  uint16_t    _tempoBPM;
  uint8_t     _padSensitivity;

  // Bargraph
  bool    _bargraphDirty;
  uint8_t _bargraphLevel;

  void resolveBindings(bool btnL, bool btnR, bool btnRear, BankType type);
  void updateCatch(uint8_t potIndex);
};

#endif // POT_ROUTER_H
