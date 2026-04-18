#include "SetupManager.h"
#include "SetupUI.h"
#include "InputParser.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../core/PotFilter.h"
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
                          uint8_t& chromaticPad, uint8_t& holdPad,
                          uint8_t* octavePads, PotRouter* potRouter) {
  _keyboard = keyboard;
  _leds = leds;
  _nvs = nvs;
  _banks = banks;
  _padOrder = padOrder;
  _bankPads = bankPads;
  _ui.begin(leds);
  _toolCal.begin(keyboard, leds, &_ui);
  _toolOrdering.begin(keyboard, leds, &_ui, padOrder);
  _toolRoles.begin(keyboard, leds, &_ui,
                   bankPads, rootPads, modePads,
                   chromaticPad, holdPad,
                   octavePads);
  _toolControlPads.begin(keyboard, leds, &_ui, nvs);
  _toolBankConfig.begin(leds, nvs, &_ui, banks);
  _toolSettings.begin(keyboard, leds, &_ui);
  _toolPotMapping.begin(leds, &_ui, potRouter);
  _toolLedSettings.begin(leds, &_ui);
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

  // Apply setup-specific PotFilter config (finer for UI navigation).
  // No restore needed — setup always ends with ESP.restart().
  {
    PotFilterStore setupCfg = PotFilter::getConfig();
    setupCfg.deadband = 2;    // finer for setup UI (runtime default 3)
    setupCfg.sleepEn  = 0;    // no sleep in setup
    PotFilter::setConfig(setupCfg);
  }

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
        _toolControlPads.run();
        _ui.vtClear();
        screenDirty = true;
        break;

      case '5':
        _toolBankConfig.run();
        _ui.vtClear();
        screenDirty = true;
        break;

      case '6':
        _toolSettings.run();
        _ui.vtClear();
        screenDirty = true;
        break;

      case '7':
        _toolPotMapping.run();
        _ui.vtClear();
        screenDirty = true;
        break;

      case '8':
        _toolLedSettings.run();
        _leds->startSetupComet();  // Restart comet (stopped by previewBegin)
        _ui.vtClear();
        screenDirty = true;
        break;

      case '0': {
        _ui.vtFrameStart();
        _ui.drawConsoleHeader("REBOOT", false);
        _ui.drawFrameEmpty();
        _ui.drawFrameLine(VT_YELLOW "Reboot and exit setup?" VT_RESET);
        _ui.drawFrameEmpty();
        _ui.drawControlBar(CBAR_CONFIRM_STRICT);
        _ui.vtFrameEnd();
        // Wait for y/n
        InputParser confirmInput;
        bool answered = false;
        while (!answered) {
          _leds->update();
          NavEvent cev = confirmInput.update();
          if (cev.type == NAV_CHAR && (cev.ch == 'y' || cev.ch == 'Y')) {
            _leds->stopSetupComet();
            _leds->allOff();
            _ui.vtClear();
            Serial.println("  Rebooting...");
            Serial.flush();
            delay(300);
            ESP.restart();
          } else if (cev.type == NAV_CHAR || cev.type == NAV_QUIT) {
            answered = true;  // any other key = cancel
          }
          delay(5);
        }
        screenDirty = true;
        break;
      }

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

