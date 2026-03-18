#include "ToolCalibration.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "SetupUI.h"

ToolCalibration::ToolCalibration()
  : _keyboard(nullptr), _leds(nullptr), _ui(nullptr) {}

void ToolCalibration::begin(CapacitiveKeyboard* keyboard, LedController* leds, SetupUI* ui) {
  _keyboard = keyboard;
  _leds = leds;
  _ui = ui;
}

void ToolCalibration::run() {
  // TODO: touch-to-measure calibration (same as V1)
}
