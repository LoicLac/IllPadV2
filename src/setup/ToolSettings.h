#ifndef TOOL_SETTINGS_H
#define TOOL_SETTINGS_H

#include <stdint.h>

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
};

#endif // TOOL_SETTINGS_H
