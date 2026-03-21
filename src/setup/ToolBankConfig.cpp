#include "ToolBankConfig.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"
#include <Arduino.h>
#include <Preferences.h>

ToolBankConfig::ToolBankConfig()
  : _nvs(nullptr), _ui(nullptr), _banks(nullptr) {}

void ToolBankConfig::begin(NvsManager* nvs, SetupUI* ui, BankSlot* banks) {
  _nvs = nvs;
  _ui = ui;
  _banks = banks;
}

// =================================================================
// run() — Toggle NORMAL/ARPEG per bank + set quantize mode for ARPEG
// =================================================================
// [1]-[8]: toggle bank type
// [a]-[h]: cycle quantize mode for bank 1-8 (ARPEG only)

static const char* QUANTIZE_NAMES[] = {"Immediate", "Beat", "Bar"};

void ToolBankConfig::run() {
  if (!_ui || !_banks) return;

  // Working copies
  BankType wkTypes[NUM_BANKS];
  uint8_t  wkQuantize[NUM_BANKS];
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    wkTypes[i] = _banks[i].type;
    wkQuantize[i] = _nvs ? _nvs->getLoadedQuantizeMode(i) : DEFAULT_ARP_START_MODE;
  }

  _ui->vtClear();
  bool screenDirty = true;
  bool errorShown = false;
  unsigned long errorTime = 0;

  while (true) {
    char input = _ui->readInput();

    // Toggle bank type [1]-[8]
    if (input >= '1' && input <= '8') {
      uint8_t idx = input - '1';

      if (wkTypes[idx] == BANK_NORMAL) {
        // Count current ARPEG slots
        uint8_t arpCount = 0;
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
          if (wkTypes[i] == BANK_ARPEG) arpCount++;
        }
        if (arpCount >= 4) {
          errorShown = true;
          errorTime = millis();
        } else {
          wkTypes[idx] = BANK_ARPEG;
          errorShown = false;
        }
      } else {
        wkTypes[idx] = BANK_NORMAL;
        wkQuantize[idx] = DEFAULT_ARP_START_MODE;  // Reset quantize on type change
        errorShown = false;
      }
      screenDirty = true;
    }

    // Cycle quantize mode [a]-[h] for bank 1-8 (ARPEG only)
    if (input >= 'a' && input <= 'h') {
      uint8_t idx = input - 'a';
      if (wkTypes[idx] == BANK_ARPEG) {
        wkQuantize[idx] = (wkQuantize[idx] + 1) % NUM_ARP_START_MODES;
        screenDirty = true;
      }
    }

    // Save [ENTER]
    if (input == '\r' || input == '\n') {
      // Direct NVS write (setup mode — NVS task not running)
      Preferences prefs;
      if (prefs.begin(BANKTYPE_NVS_NAMESPACE, false)) {
        uint8_t types[NUM_BANKS];
        for (uint8_t i = 0; i < NUM_BANKS; i++) types[i] = (uint8_t)wkTypes[i];
        prefs.putBytes(BANKTYPE_NVS_KEY, types, NUM_BANKS);
        prefs.putBytes("qmode", wkQuantize, NUM_BANKS);
        prefs.end();

        // Update live bank slots only after successful NVS write
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
          _banks[i].type = wkTypes[i];
        }

        _ui->showSaved();
        _ui->vtClear();
        Serial.printf(VT_GREEN "  Bank config saved." VT_RESET "\n");
        delay(600);
      } else {
        _ui->vtClear();
        Serial.printf(VT_RED "  NVS write failed! Config not saved." VT_RESET "\n");
        delay(1500);
      }
      _ui->vtClear();
      return;
    }

    // Quit [q]
    if (input == 'q' || input == 'Q') {
      _ui->vtClear();
      return;
    }

    // Clear error after 2s
    if (errorShown && millis() - errorTime > 2000) {
      errorShown = false;
      screenDirty = true;
    }

    // Refresh display
    if (screenDirty) {
      screenDirty = false;

      uint8_t arpCount = 0;
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        if (wkTypes[i] == BANK_ARPEG) arpCount++;
      }

      _ui->vtFrameStart();
      _ui->drawHeader("BANK CONFIG", "");
      Serial.printf(VT_CL "\n");

      // Display banks in 2 columns (1-4 left, 5-8 right)
      for (uint8_t row = 0; row < 4; row++) {
        uint8_t left  = row;
        uint8_t right = row + 4;

        // Left column
        if (wkTypes[left] == BANK_ARPEG) {
          Serial.printf("    [%d] Bank %d:  " VT_CYAN VT_BOLD "ARPEG" VT_RESET " [%c]%-9s",
                        left + 1, left + 1, 'a' + left, QUANTIZE_NAMES[wkQuantize[left]]);
        } else {
          Serial.printf("    [%d] Bank %d:  NORMAL              ", left + 1, left + 1);
        }

        Serial.printf("  ");

        // Right column
        if (wkTypes[right] == BANK_ARPEG) {
          Serial.printf("[%d] Bank %d:  " VT_CYAN VT_BOLD "ARPEG" VT_RESET " [%c]%-9s",
                        right + 1, right + 1, 'a' + right, QUANTIZE_NAMES[wkQuantize[right]]);
        } else {
          Serial.printf("[%d] Bank %d:  NORMAL              ", right + 1, right + 1);
        }

        Serial.printf(VT_CL "\n");
      }

      Serial.printf(VT_CL "\n");
      Serial.printf("    Arpeg slots: %d/4 used" VT_CL "\n", arpCount);
      Serial.printf(VT_CL "\n");

      if (errorShown) {
        Serial.printf(VT_RED "    Max 4 arpeg banks!" VT_RESET VT_CL "\n");
      }

      Serial.printf(VT_CL "\n");
      Serial.printf("    [1-8] Toggle type   [a-h] Cycle quantize" VT_CL "\n");
      Serial.printf("    [ENTER] Save   [q] Back" VT_CL "\n");
      _ui->vtFrameEnd();
    }

    delay(10);
  }
}
