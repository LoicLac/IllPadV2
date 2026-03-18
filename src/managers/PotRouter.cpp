#include "PotRouter.h"

const PotBinding PotRouter::BINDINGS[] = {
  // Pot Left
  {0, 0b000, BANK_NORMAL, TARGET_RESPONSE_SHAPE,      0, 4095},
  {0, 0b010, BANK_NORMAL, TARGET_SLEW_RATE,           SLEW_RATE_MIN, SLEW_RATE_MAX},
  {0, 0b000, BANK_ARPEG,  TARGET_GATE_LENGTH,         0, 4095},
  {0, 0b010, BANK_ARPEG,  TARGET_SWING,               0, 4095},
  // Pot Right
  {1, 0b000, BANK_ARPEG,  TARGET_DIVISION,            0, 4095},
  {1, 0b001, BANK_ARPEG,  TARGET_VELOCITY_VARIATION,  0, 100},
  {1, 0b010, BANK_ARPEG,  TARGET_BASE_VELOCITY,       1, 127},
  {1, 0b001, BANK_NORMAL, TARGET_AT_DEADZONE,         0, 250},
  // Pot Rear
  {2, 0b000, BANK_ANY,    TARGET_TEMPO_BPM,           40, 240},
  {2, 0b001, BANK_ANY,    TARGET_PAD_SENSITIVITY,     5, 30},
};

const uint8_t PotRouter::NUM_BINDINGS = sizeof(BINDINGS) / sizeof(BINDINGS[0]);

PotRouter::PotRouter()
  : _responseShape(RESPONSE_SHAPE_DEFAULT),
    _slewRate(SLEW_RATE_DEFAULT),
    _atDeadzone(AT_DEADZONE_DEFAULT),
    _gateLength(0.5f),
    _swing(0.5f),
    _division(DIV_1_8),
    _velocityVariation(0),
    _baseVelocity(100),
    _tempoBPM(120),
    _padSensitivity(PAD_SENSITIVITY_DEFAULT),
    _bargraphDirty(false),
    _bargraphLevel(0) {
  for (uint8_t i = 0; i < NUM_POTS; i++) {
    _rawAdc[i] = 0;
    _smoothedAdc[i] = 0.0f;
    _active[i] = nullptr;
  }
  for (uint8_t i = 0; i < 12; i++) {
    _catch[i].storedValue = 0;
    _catch[i].caught = false;
  }
}

void PotRouter::begin() {
  // TODO: configure ADC pins, load stored values from NVS
}

void PotRouter::update(bool btnL, bool btnR, bool btnRear, BankType currentType) {
  (void)btnL; (void)btnR; (void)btnRear; (void)currentType;
  // TODO: read ADCs, resolve bindings, apply catch, update output values
}

float       PotRouter::getResponseShape() const       { return _responseShape; }
uint16_t    PotRouter::getSlewRate() const             { return _slewRate; }
uint16_t    PotRouter::getAtDeadzone() const           { return _atDeadzone; }
float       PotRouter::getGateLength() const           { return _gateLength; }
float       PotRouter::getSwing() const                { return _swing; }
ArpDivision PotRouter::getDivision() const             { return _division; }
uint8_t     PotRouter::getVelocityVariation() const    { return _velocityVariation; }
uint8_t     PotRouter::getBaseVelocity() const         { return _baseVelocity; }
uint16_t    PotRouter::getTempoBPM() const             { return _tempoBPM; }
uint8_t     PotRouter::getPadSensitivity() const       { return _padSensitivity; }

bool    PotRouter::hasBargraphUpdate() const { return _bargraphDirty; }
uint8_t PotRouter::getBargraphLevel() const  { return _bargraphLevel; }

void PotRouter::resolveBindings(bool btnL, bool btnR, bool btnRear, BankType type) {
  (void)btnL; (void)btnR; (void)btnRear; (void)type;
  // TODO: match active bindings per pot
}

void PotRouter::updateCatch(uint8_t potIndex) {
  (void)potIndex;
  // TODO: catch logic
}
