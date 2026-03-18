#include "ToolSettings.h"
#include "../core/CapacitiveKeyboard.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"

ToolSettings::ToolSettings()
  : _keyboard(nullptr), _nvs(nullptr), _ui(nullptr) {}

void ToolSettings::begin(CapacitiveKeyboard* keyboard, NvsManager* nvs, SetupUI* ui) {
  _keyboard = keyboard;
  _nvs = nvs;
  _ui = ui;
}

void ToolSettings::run() {
  // TODO: edit baseline profile, pad sensitivity, AT rate, AT deadzone, BLE interval
}
