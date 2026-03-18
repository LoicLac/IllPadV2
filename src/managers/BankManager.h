#ifndef BANK_MANAGER_H
#define BANK_MANAGER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class MidiTransport;

class BankManager {
public:
  BankManager();

  void begin(MidiTransport* transport, BankSlot* banks);
  void update(const uint8_t* keyIsPressed, bool btnLeftHeld);

  uint8_t getCurrentBank() const;
  BankSlot& getCurrentSlot();
  bool isHolding() const;

private:
  MidiTransport* _transport;
  BankSlot*      _banks;
  uint8_t        _currentBank;
  bool           _holding;
  bool           _lastBtnState;

  void switchToBank(uint8_t newBank);
};

#endif // BANK_MANAGER_H
