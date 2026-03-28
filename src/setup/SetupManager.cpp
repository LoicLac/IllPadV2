#include "SetupManager.h"
#include "SetupUI.h"
#include "InputParser.h"
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
  _toolBankConfig.begin(leds, nvs, &_ui, banks);
  _toolSettings.begin(keyboard, leds, nvs, &_ui);
  _toolPotMapping.begin(leds, &_ui, potRouter);
}

static InputParser s_input;

// =================================================================
// run() — Main setup mode loop (blocking)
// =================================================================
void SetupManager::run() {
  if (!_keyboard || !_leds) return;

  // Enter setup LED mode
  _leds->allOff();
  _leds->startSetupComet();
  _leds->update();

  // Debounce: wait for entry conditions to settle
  delay(200);

  _ui.vtClear();          // sends ESC[H — Python script boot sync triggers on this
  Serial.flush();         // ensure vtClear reaches Python before iTerm2 sequences
  delay(100);             // let Python script complete boot sync and enter main loop
  _ui.initTerminal();     // iTerm2 sequences: palette, badge, title, resize
  Serial.flush();
  delay(50);              // let iTerm2 process the sequences before first frame
  bool screenDirty = true;
  unsigned long lastRefresh = 0;

  while (true) {
    _leds->update();
    NavEvent ev = s_input.update();
    char input = 0;
    if (ev.type == NAV_CHAR) input = ev.ch;
    else if (ev.type == NAV_QUIT) input = '0';  // q = reboot from main menu

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

