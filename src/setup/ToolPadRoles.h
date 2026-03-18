#ifndef TOOL_PAD_ROLES_H
#define TOOL_PAD_ROLES_H

#include <stdint.h>
#include "../core/HardwareConfig.h"

class CapacitiveKeyboard;
class LedController;
class NvsManager;
class SetupUI;

class ToolPadRoles {
public:
  ToolPadRoles();

  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             NvsManager* nvs, SetupUI* ui,
             uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
             uint8_t& chromaticPad, uint8_t* patternPads,
             uint8_t& octavePad, uint8_t& holdPad, uint8_t& playStopPad);
  void run();  // Blocking — sub-menu: bank/scale/arp pads + collision check

private:
  CapacitiveKeyboard* _keyboard;
  LedController*      _leds;
  NvsManager*         _nvs;
  SetupUI*            _ui;

  uint8_t* _bankPads;
  uint8_t* _rootPads;
  uint8_t* _modePads;
  uint8_t* _chromaticPad;
  uint8_t* _patternPads;
  uint8_t* _octavePad;
  uint8_t* _holdPad;
  uint8_t* _playStopPad;

  bool checkCollisions() const;
};

#endif // TOOL_PAD_ROLES_H
