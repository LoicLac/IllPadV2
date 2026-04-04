#ifndef TOOL_PAD_ORDERING_H
#define TOOL_PAD_ORDERING_H

#include <stdint.h>
#include "../core/HardwareConfig.h"

class CapacitiveKeyboard;
class LedController;
class SetupUI;

class ToolPadOrdering {
public:
  ToolPadOrdering();

  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             SetupUI* ui, uint8_t* padOrder);
  void run();  // Blocking

private:
  bool saveOrder(const uint8_t* orderMap);

  CapacitiveKeyboard* _keyboard;
  LedController*      _leds;
  SetupUI*            _ui;
  uint8_t*            _padOrder;
};

#endif // TOOL_PAD_ORDERING_H
