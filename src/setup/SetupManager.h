#ifndef SETUP_MANAGER_H
#define SETUP_MANAGER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "SetupUI.h"
#include "ToolCalibration.h"
#include "ToolPadOrdering.h"
#include "ToolPadRoles.h"
#include "ToolBankConfig.h"
#include "ToolSettings.h"
#include "ToolPotMapping.h"
#include "ToolLedSettings.h"

class CapacitiveKeyboard;
class LedController;
class PotRouter;
class NvsManager;

class SetupManager {
public:
  SetupManager();

  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             NvsManager* nvs, BankSlot* banks,
             uint8_t* padOrder, uint8_t* bankPads,
             uint8_t* rootPads, uint8_t* modePads,
             uint8_t& chromaticPad, uint8_t& holdPad,
             uint8_t* octavePads, PotRouter* potRouter);

  // Enter setup mode (blocking — returns when user exits)
  void run();

private:
  CapacitiveKeyboard* _keyboard;
  LedController*      _leds;
  NvsManager*         _nvs;
  BankSlot*           _banks;
  uint8_t*            _padOrder;
  uint8_t*            _bankPads;
  SetupUI             _ui;
  ToolCalibration     _toolCal;
  ToolPadOrdering     _toolOrdering;
  ToolPadRoles        _toolRoles;
  ToolBankConfig      _toolBankConfig;
  ToolSettings        _toolSettings;
  ToolPotMapping      _toolPotMapping;
  ToolLedSettings     _toolLedSettings;

};

#endif // SETUP_MANAGER_H
