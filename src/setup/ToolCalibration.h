#ifndef TOOL_CALIBRATION_H
#define TOOL_CALIBRATION_H

#include <stdint.h>

class CapacitiveKeyboard;
class LedController;
class SetupUI;

class ToolCalibration {
public:
  ToolCalibration();

  void begin(CapacitiveKeyboard* keyboard, LedController* leds, SetupUI* ui);
  void run();  // Blocking — returns when done

private:
  CapacitiveKeyboard* _keyboard;
  LedController*      _leds;
  SetupUI*            _ui;
};

#endif // TOOL_CALIBRATION_H
