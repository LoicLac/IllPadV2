#ifndef TOOL_LED_SETTINGS_H
#define TOOL_LED_SETTINGS_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "InputParser.h"

class LedController;
class NvsManager;
class SetupUI;

class ToolLedSettings {
public:
  ToolLedSettings();
  void begin(LedController* leds, NvsManager* nvs, SetupUI* ui);
  void run();

private:
  LedController* _leds;
  NvsManager*    _nvs;
  SetupUI*       _ui;

  LedSettingsStore _wk;

  uint8_t _page;        // 0=DISPLAY, 1=CONFIRM
  uint8_t _cursor;      // param index within current page
  bool    _editing;
  bool    _nvsSaved;
  bool    _confirmDefaults;

  uint8_t pageParamCount() const;
  void adjustParam(int8_t dir, bool accel);
  bool saveSettings();
  void drawDescription();
};

#endif // TOOL_LED_SETTINGS_H
