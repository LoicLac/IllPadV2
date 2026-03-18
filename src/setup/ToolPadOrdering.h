#ifndef TOOL_PAD_ORDERING_H
#define TOOL_PAD_ORDERING_H

#include <stdint.h>
#include "../core/HardwareConfig.h"

class CapacitiveKeyboard;
class LedController;
class NvsManager;
class SetupUI;

class ToolPadOrdering {
public:
  ToolPadOrdering();

  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             NvsManager* nvs, SetupUI* ui, uint8_t* padOrder);
  void run();  // Blocking

private:
  CapacitiveKeyboard* _keyboard;
  LedController*      _leds;
  NvsManager*         _nvs;
  SetupUI*            _ui;
  uint8_t*            _padOrder;
};

#endif // TOOL_PAD_ORDERING_H
