#ifndef TOOL_BANK_CONFIG_H
#define TOOL_BANK_CONFIG_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class NvsManager;
class SetupUI;

class ToolBankConfig {
public:
  ToolBankConfig();

  void begin(NvsManager* nvs, SetupUI* ui, BankSlot* banks);
  void run();  // Blocking — toggle NORMAL/ARPEG per bank (max 4 ARPEG)

private:
  NvsManager* _nvs;
  SetupUI*    _ui;
  BankSlot*   _banks;
};

#endif // TOOL_BANK_CONFIG_H
