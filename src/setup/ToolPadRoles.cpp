#include "ToolPadRoles.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"

ToolPadRoles::ToolPadRoles()
  : _keyboard(nullptr), _leds(nullptr), _nvs(nullptr), _ui(nullptr),
    _bankPads(nullptr), _rootPads(nullptr), _modePads(nullptr),
    _chromaticPad(nullptr), _patternPads(nullptr),
    _octavePad(nullptr), _holdPad(nullptr), _playStopPad(nullptr) {}

void ToolPadRoles::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                          NvsManager* nvs, SetupUI* ui,
                          uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
                          uint8_t& chromaticPad, uint8_t* patternPads,
                          uint8_t& octavePad, uint8_t& holdPad, uint8_t& playStopPad) {
  _keyboard = keyboard;
  _leds = leds;
  _nvs = nvs;
  _ui = ui;
  _bankPads = bankPads;
  _rootPads = rootPads;
  _modePads = modePads;
  _chromaticPad = &chromaticPad;
  _patternPads = patternPads;
  _octavePad = &octavePad;
  _holdPad = &holdPad;
  _playStopPad = &playStopPad;
}

void ToolPadRoles::run() {
  // TODO: sub-menu (bank pads, scale pads, arp pads, view/collisions)
}

bool ToolPadRoles::checkCollisions() const {
  // TODO: check no pad has multiple roles
  return false;
}
