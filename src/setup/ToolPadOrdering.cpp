#include "ToolPadOrdering.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"

ToolPadOrdering::ToolPadOrdering()
  : _keyboard(nullptr), _leds(nullptr), _nvs(nullptr),
    _ui(nullptr), _padOrder(nullptr) {}

void ToolPadOrdering::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                             NvsManager* nvs, SetupUI* ui, uint8_t* padOrder) {
  _keyboard = keyboard;
  _leds = leds;
  _nvs = nvs;
  _ui = ui;
  _padOrder = padOrder;
}

void ToolPadOrdering::run() {
  // TODO: "touch pads from low to high" — positions 0-47, partial save OK
}
