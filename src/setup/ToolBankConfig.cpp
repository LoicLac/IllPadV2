#include "ToolBankConfig.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"

ToolBankConfig::ToolBankConfig()
  : _nvs(nullptr), _ui(nullptr), _banks(nullptr) {}

void ToolBankConfig::begin(NvsManager* nvs, SetupUI* ui, BankSlot* banks) {
  _nvs = nvs;
  _ui = ui;
  _banks = banks;
}

void ToolBankConfig::run() {
  // TODO: display bank types, toggle NORMAL/ARPEG, enforce max 4 ARPEG
}
