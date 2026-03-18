#include "SetupManager.h"
#include "SetupUI.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"

SetupManager::SetupManager()
  : _keyboard(nullptr), _leds(nullptr), _nvs(nullptr),
    _banks(nullptr), _padOrder(nullptr), _bankPads(nullptr) {}

void SetupManager::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                          NvsManager* nvs, BankSlot* banks,
                          uint8_t* padOrder, uint8_t* bankPads) {
  _keyboard = keyboard;
  _leds = leds;
  _nvs = nvs;
  _banks = banks;
  _padOrder = padOrder;
  _bankPads = bankPads;
  _ui.begin(leds);
}

void SetupManager::run() {
  // TODO: main menu loop, dispatch to tools, handle exit/reboot
}

bool SetupManager::shouldEnterSetup() {
  // TODO: check rear button held for CAL_HOLD_DURATION_MS at boot
  return false;
}

void SetupManager::runTool(uint8_t toolIndex) {
  (void)toolIndex;
  // TODO: dispatch to appropriate tool
}
