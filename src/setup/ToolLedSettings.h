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

  // Working copies
  LedSettingsStore _wk;
  ColorSlotStore   _cwk;

  // Navigation
  uint8_t _page;          // 0=COLOR+TIMING, 1=CONFIRM
  uint8_t _cursor;        // row index within current page
  uint8_t _colorField;    // COLOR rows: 0=Preset, 1=Hue, 2=Intensity
  bool    _editing;
  bool    _nvsSaved;
  bool    _confirmDefaults;

  // Preview state
  enum PreviewState : uint8_t { PREV_IDLE, PREV_CONTINUOUS, PREV_EVENT };
  PreviewState  _prevState;
  unsigned long _prevStart;
  uint8_t       _prevEventRow;

  // Page counts
  uint8_t pageParamCount() const;

  // COLOR row helpers
  void adjustColorField(int8_t dir, bool accel);
  void adjustTimingParam(int8_t dir, bool accel);
  uint8_t getRowIntensity(uint8_t row) const;
  void setRowIntensity(uint8_t row, uint8_t val);
  bool rowHasEditableIntensity(uint8_t row) const;

  // CONFIRM page
  void adjustConfirmParam(int8_t dir, bool accel);

  // Save
  bool saveLedSettings();
  bool saveColorSlots();

  // Drawing
  void drawDescription();
  void drawColorRow(uint8_t row, bool selected, bool editing);

  // Preview
  void updatePreview(unsigned long now);
  void startEventPreview(uint8_t row);
  void renderContinuousPreview(unsigned long now);
  void renderEventPreview(unsigned long now);
  uint8_t mapCursorToPreviewRow() const;
};

#endif // TOOL_LED_SETTINGS_H
