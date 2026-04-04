#ifndef TOOL_SETTINGS_H
#define TOOL_SETTINGS_H

#include <stdint.h>
#include "SetupPotInput.h"

class CapacitiveKeyboard;
class LedController;
class SetupUI;
struct SettingsStore;

class ToolSettings {
public:
  ToolSettings();

  void begin(CapacitiveKeyboard* keyboard, LedController* leds, SetupUI* ui);
  void run();  // Blocking — unified arrow navigation, immediate save per param

private:
  void adjustParam(SettingsStore& wk, uint8_t param, int dir, bool accelerated);
  bool saveSettings(const SettingsStore& wk);
  void drawDescription(uint8_t param);

  CapacitiveKeyboard* _keyboard;
  LedController*      _leds;
  SetupUI*            _ui;

  // Pot navigation
  SetupPotInput _pots;
  int32_t _potCursorIdx;     // 0-7 param index (nav mode)
  int32_t _potEditVal;       // Value being edited (edit mode)

  void seedPotForEdit(const SettingsStore& wk, uint8_t param);
  void applyPotEdit(SettingsStore& wk, uint8_t param);
};

#endif // TOOL_SETTINGS_H
