#ifndef TOOL_SETTINGS_H
#define TOOL_SETTINGS_H

#include <stdint.h>

class CapacitiveKeyboard;
class NvsManager;
class SetupUI;

class ToolSettings {
public:
  ToolSettings();

  void begin(CapacitiveKeyboard* keyboard, NvsManager* nvs, SetupUI* ui);
  void run();  // Blocking — edit profile, sensitivity, AT rate, deadzone, BLE interval

private:
  CapacitiveKeyboard* _keyboard;
  NvsManager*         _nvs;
  SetupUI*            _ui;
};

#endif // TOOL_SETTINGS_H
