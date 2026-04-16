#ifndef TOOL_BANK_CONFIG_H
#define TOOL_BANK_CONFIG_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"
#include "SetupPotInput.h"

class LedController;
class NvsManager;
class SetupUI;

class ToolBankConfig {
public:
  ToolBankConfig();

  void begin(LedController* leds, NvsManager* nvs, SetupUI* ui, BankSlot* banks);
  void run();  // Blocking — unified arrow navigation for bank type + quantize

private:
  bool saveConfig(const BankType* types, const uint8_t* quantize);
  void drawDescription(uint8_t cursor, bool isArpeg);

  LedController* _leds;
  NvsManager*    _nvs;
  SetupUI*       _ui;
  BankSlot*      _banks;

  // Pot navigation
  SetupPotInput _pots;
  int32_t _potBankIdx;       // 0-7 (nav mode)
  int32_t _potComboState;    // 0-2 combined (edit mode)
};

#endif // TOOL_BANK_CONFIG_H
