#ifndef SCALE_MANAGER_H
#define SCALE_MANAGER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class ScaleManager {
public:
  ScaleManager();

  void begin(BankSlot* banks);
  void update(const uint8_t* keyIsPressed, bool btnRightHeld, BankSlot& currentSlot);

  bool isHolding() const;

private:
  BankSlot* _banks;
  bool      _holding;
  bool      _lastBtnState;

  // Pad assignments (loaded from NVS)
  uint8_t _rootPads[7];
  uint8_t _modePads[7];
  uint8_t _chromaticPad;
  uint8_t _patternPads[5];
  uint8_t _octavePad;
  uint8_t _holdPad;
  uint8_t _playStopPad;

  void processScalePads(const uint8_t* keyIsPressed, BankSlot& slot);
  void processArpPads(const uint8_t* keyIsPressed, BankSlot& slot);
};

#endif // SCALE_MANAGER_H
