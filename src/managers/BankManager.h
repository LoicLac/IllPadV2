#ifndef BANK_MANAGER_H
#define BANK_MANAGER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class MidiEngine;
class LedController;

class BankManager {
public:
  BankManager();

  void begin(MidiEngine* engine, LedController* leds, BankSlot* banks,
             uint8_t* lastKeys);

  // Call every loop iteration with current key state and button state.
  // Returns true if a bank switch occurred this frame.
  bool update(const uint8_t* keyIsPressed, bool btnLeftHeld);

  uint8_t  getCurrentBank() const;
  BankSlot& getCurrentSlot();
  bool     isHolding() const;

  // Set initial bank (from NVS, called before loop starts)
  void setCurrentBank(uint8_t bank);

  // Bank select pads (default 0-7, loadable from NVS later)
  void setBankPads(const uint8_t* pads);

private:
  MidiEngine*    _engine;
  LedController* _leds;
  BankSlot*      _banks;
  uint8_t*       _lastKeys;    // main loop's s_lastKeys — reset on switch

  uint8_t _currentBank;
  uint8_t _bankPads[NUM_BANKS];

  // Button state
  bool _holding;              // true while left button is held
  bool _lastBtnState;         // previous frame's button state

  // Arm/disarm debounce
  bool _armed;                // true = ready to accept a bank pad press
  bool _switchedDuringHold;   // true if we switched at least once this hold

  void switchToBank(uint8_t newBank);
};

#endif // BANK_MANAGER_H
