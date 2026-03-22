#include "SetupManager.h"
#include "SetupUI.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include "../managers/PotRouter.h"
#include <Arduino.h>

SetupManager::SetupManager()
  : _keyboard(nullptr), _leds(nullptr), _nvs(nullptr),
    _banks(nullptr), _padOrder(nullptr), _bankPads(nullptr) {}

void SetupManager::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                          NvsManager* nvs, BankSlot* banks,
                          uint8_t* padOrder, uint8_t* bankPads,
                          uint8_t* rootPads, uint8_t* modePads,
                          uint8_t& chromaticPad, uint8_t& holdPad, uint8_t& playStopPad,
                          uint8_t* octavePads, PotRouter* potRouter) {
  _keyboard = keyboard;
  _leds = leds;
  _nvs = nvs;
  _banks = banks;
  _padOrder = padOrder;
  _bankPads = bankPads;
  _ui.begin(leds);
  _toolCal.begin(keyboard, leds, &_ui);
  _toolOrdering.begin(keyboard, leds, nvs, &_ui, padOrder);
  _toolRoles.begin(keyboard, leds, nvs, &_ui,
                   bankPads, rootPads, modePads,
                   chromaticPad, holdPad, playStopPad,
                   octavePads);
  _toolBankConfig.begin(nvs, &_ui, banks);
  _toolSettings.begin(keyboard, nvs, &_ui);
  _toolPotMapping.begin(leds, &_ui, potRouter);
}

// =================================================================
// run() — Main setup mode loop (blocking)
// =================================================================
void SetupManager::run() {
  if (!_keyboard || !_leds) return;

  // Enter setup LED mode
  _leds->allOff();
  _leds->startSetupComet();
  _leds->update();

  // Wait for button release (user held it 3s to enter setup)
  while (digitalRead(BTN_REAR_PIN) == LOW) {
    delay(10);
    _leds->update();
  }
  delay(50);

  _ui.vtClear();
  bool screenDirty = true;
  unsigned long lastRefresh = 0;

  while (true) {
    _leds->update();
    char input = _ui.readInput();

    switch (input) {
      case '1':
        _toolCal.run();
        _ui.vtClear();
        screenDirty = true;
        break;

      case '2':
        _toolOrdering.run();
        _ui.vtClear();
        screenDirty = true;
        break;

      case '3':
        _toolRoles.run();
        _ui.vtClear();
        screenDirty = true;
        break;

      case '4':
        _toolBankConfig.run();
        _ui.vtClear();
        screenDirty = true;
        break;

      case '5':
        _toolSettings.run();
        _ui.vtClear();
        screenDirty = true;
        break;

      case '6':
        _toolPotMapping.run();
        _ui.vtClear();
        screenDirty = true;
        break;

      case '0':
        _leds->stopSetupComet();
        _leds->allOff();
        _ui.vtClear();
        Serial.println("  Rebooting...");
        Serial.flush();
        delay(300);
        ESP.restart();
        break;

      default:
        break;
    }

    if (screenDirty || millis() - lastRefresh >= 500) {
      _ui.printMainMenu();
      screenDirty = false;
      lastRefresh = millis();
    }
    delay(5);
  }
}

