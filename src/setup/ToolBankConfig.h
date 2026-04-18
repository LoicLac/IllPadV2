#ifndef TOOL_BANK_CONFIG_H
#define TOOL_BANK_CONFIG_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class LedController;
class NvsManager;
class SetupUI;

class ToolBankConfig {
public:
  ToolBankConfig();

  void begin(LedController* leds, NvsManager* nvs, SetupUI* ui, BankSlot* banks);
  void run();  // Blocking — unified arrow navigation for bank type + quantize

private:
  bool saveConfig(const BankType* types, const uint8_t* quantize, const uint8_t* groups);
  void drawDescription(uint8_t cursor, bool isArpeg);

  LedController* _leds;
  NvsManager*    _nvs;
  SetupUI*       _ui;
  BankSlot*      _banks;
};

#endif // TOOL_BANK_CONFIG_H
