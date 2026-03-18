#ifndef SETUP_MANAGER_H
#define SETUP_MANAGER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "SetupUI.h"

class CapacitiveKeyboard;
class LedController;
class NvsManager;

class SetupManager {
public:
  SetupManager();

  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             NvsManager* nvs, BankSlot* banks,
             uint8_t* padOrder, uint8_t* bankPads);

  // Enter setup mode (blocking — returns when user exits)
  void run();

  // Check if setup should be entered (button held at boot)
  static bool shouldEnterSetup();

private:
  CapacitiveKeyboard* _keyboard;
  LedController*      _leds;
  NvsManager*         _nvs;
  BankSlot*           _banks;
  uint8_t*            _padOrder;
  uint8_t*            _bankPads;
  SetupUI             _ui;

  void runTool(uint8_t toolIndex);
};

#endif // SETUP_MANAGER_H
