#include "ToolBankConfig.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"
#include "InputParser.h"
#include <Arduino.h>
#include <Preferences.h>

static const char* QUANTIZE_NAMES[] = {"Immediate", "Beat", "Bar"};

ToolBankConfig::ToolBankConfig()
  : _leds(nullptr), _nvs(nullptr), _ui(nullptr), _banks(nullptr) {}

void ToolBankConfig::begin(LedController* leds, NvsManager* nvs, SetupUI* ui, BankSlot* banks) {
  _leds = leds;
  _nvs = nvs;
  _ui = ui;
  _banks = banks;
}

// =================================================================
// saveConfig — write types + quantize to NVS, update live banks
// =================================================================
bool ToolBankConfig::saveConfig(const BankType* types, const uint8_t* quantize) {
  Preferences prefs;
  if (!prefs.begin(BANKTYPE_NVS_NAMESPACE, false)) return false;

  uint8_t rawTypes[NUM_BANKS];
  for (uint8_t i = 0; i < NUM_BANKS; i++) rawTypes[i] = (uint8_t)types[i];
  prefs.putBytes(BANKTYPE_NVS_KEY, rawTypes, NUM_BANKS);
  prefs.putBytes("qmode", quantize, NUM_BANKS);
  prefs.end();

  // Update live bank slots + quantize cache after successful NVS write
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _banks[i].type = types[i];
    if (_nvs) _nvs->setLoadedQuantizeMode(i, quantize[i]);
  }
  return true;
}

// =================================================================
// drawDescription — context-sensitive help for the selected bank
// =================================================================
void ToolBankConfig::drawDescription(uint8_t cursor, bool isArpeg) {
  (void)cursor;
  Serial.printf(VT_CL "\n");
  if (isArpeg) {
    Serial.printf(VT_DIM "  Bank type. ARPEG enables arpeggiator (max 4)." VT_RESET VT_CL "\n");
    Serial.printf(VT_DIM "  Quantize: when the arp starts after play." VT_RESET VT_CL "\n");
    Serial.printf(VT_DIM "  Immediate = next tick, Beat = next 1/4, Bar = next bar." VT_RESET VT_CL "\n");
  } else {
    Serial.printf(VT_DIM "  Bank type. ARPEG enables arpeggiator (max 4)." VT_RESET VT_CL "\n");
    Serial.printf(VT_DIM "  NORMAL banks play notes directly with aftertouch." VT_RESET VT_CL "\n");
  }
}

// =================================================================
// run() — Unified arrow navigation with cursor + editing
// =================================================================
void ToolBankConfig::run() {
  if (!_ui || !_banks) return;

  // Working copies
  BankType wkTypes[NUM_BANKS];
  uint8_t  wkQuantize[NUM_BANKS];
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    wkTypes[i] = _banks[i].type;
    wkQuantize[i] = _nvs ? _nvs->getLoadedQuantizeMode(i) : DEFAULT_ARP_START_MODE;
  }

  InputParser input;
  uint8_t cursor = 0;        // 0-7: which bank
  bool editing = false;
  uint8_t editField = 0;     // 0 = type, 1 = quantize
  bool screenDirty = true;
  bool confirmDefaults = false;
  bool errorShown = false;
  unsigned long errorTime = 0;

  _ui->vtClear();

  while (true) {
    if (_leds) _leds->update();

    NavEvent ev = input.update();

    // Clear error after 2s
    if (errorShown && (millis() - errorTime) > 2000) {
      errorShown = false;
      screenDirty = true;
    }

    // --- Defaults confirmation sub-mode ---
    if (confirmDefaults) {
      if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
        // Banks 1-4 NORMAL, 5-8 ARPEG, all Quantize = Immediate
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
          wkTypes[i] = (i < 4) ? BANK_NORMAL : BANK_ARPEG;
          wkQuantize[i] = ARP_START_IMMEDIATE;
        }
        if (saveConfig(wkTypes, wkQuantize)) {
          _ui->showSaved();
        }
        confirmDefaults = false;
        editing = false;
        screenDirty = true;
      } else if (ev.type != NAV_NONE) {
        confirmDefaults = false;
        screenDirty = true;
      }
      delay(5);
      continue;
    }

    // --- Main navigation ---
    if (ev.type == NAV_QUIT) {
      if (editing) {
        // Cancel edit — revert working copy from live state
        wkTypes[cursor] = _banks[cursor].type;
        if (_nvs) wkQuantize[cursor] = _nvs->getLoadedQuantizeMode(cursor);
        editing = false;
        editField = 0;
        screenDirty = true;
      } else {
        _ui->vtClear();
        return;
      }
    }

    if (ev.type == NAV_DEFAULTS && !editing) {
      confirmDefaults = true;
      screenDirty = true;
    }

    if (!editing) {
      if (ev.type == NAV_UP) {
        if (cursor > 0) cursor--;
        else cursor = NUM_BANKS - 1;
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        if (cursor < NUM_BANKS - 1) cursor++;
        else cursor = 0;
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        editing = true;
        editField = 0;  // Start on type field
        screenDirty = true;
      }
    } else {
      // --- Editing mode ---
      if (editField == 0) {
        // Editing type field
        if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
          if (wkTypes[cursor] == BANK_NORMAL) {
            // Try to switch to ARPEG
            uint8_t arpCount = 0;
            for (uint8_t i = 0; i < NUM_BANKS; i++) {
              if (wkTypes[i] == BANK_ARPEG) arpCount++;
            }
            if (arpCount >= 4) {
              errorShown = true;
              errorTime = millis();
            } else {
              wkTypes[cursor] = BANK_ARPEG;
              errorShown = false;
            }
          } else {
            wkTypes[cursor] = BANK_NORMAL;
            wkQuantize[cursor] = DEFAULT_ARP_START_MODE;
            errorShown = false;
          }
          screenDirty = true;
        } else if (ev.type == NAV_DOWN && wkTypes[cursor] == BANK_ARPEG) {
          // Move to quantize sub-field
          editField = 1;
          screenDirty = true;
        } else if (ev.type == NAV_ENTER) {
          // Save on confirm
          if (saveConfig(wkTypes, wkQuantize)) {
            editing = false;
            _ui->showSaved();
            screenDirty = true;
          } else {
            Serial.printf("\r\n" VT_RED "  NVS write failed!" VT_RESET);
            delay(1500);
            screenDirty = true;
          }
        }
      } else {
        // editField == 1: editing quantize field
        if (ev.type == NAV_LEFT) {
          wkQuantize[cursor] = (wkQuantize[cursor] + NUM_ARP_START_MODES - 1) % NUM_ARP_START_MODES;
          screenDirty = true;
        } else if (ev.type == NAV_RIGHT) {
          wkQuantize[cursor] = (wkQuantize[cursor] + 1) % NUM_ARP_START_MODES;
          screenDirty = true;
        } else if (ev.type == NAV_UP) {
          // Move back to type field
          editField = 0;
          screenDirty = true;
        } else if (ev.type == NAV_ENTER) {
          // Save on confirm
          if (saveConfig(wkTypes, wkQuantize)) {
            editing = false;
            _ui->showSaved();
            screenDirty = true;
          } else {
            Serial.printf("\r\n" VT_RED "  NVS write failed!" VT_RESET);
            delay(1500);
            screenDirty = true;
          }
        }
      }
    }

    // --- Render ---
    if (screenDirty) {
      screenDirty = false;

      uint8_t arpCount = 0;
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        if (wkTypes[i] == BANK_ARPEG) arpCount++;
      }

      char headerRight[16];
      snprintf(headerRight, sizeof(headerRight), "%d/4 ARPEG", arpCount);

      _ui->vtFrameStart();
      _ui->drawHeader("BANK CONFIG", headerRight);
      Serial.printf(VT_CL "\n");

      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        bool selected = (cursor == i);
        bool isEditing = selected && editing;
        bool isArpeg = (wkTypes[i] == BANK_ARPEG);

        // Selection indicator
        if (selected) {
          Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " ");
        } else {
          Serial.printf("    ");
        }

        // Bank label
        Serial.printf("Bank %d    ", i + 1);

        // Type field
        bool editingType = isEditing && (editField == 0);
        if (editingType) {
          if (isArpeg) {
            Serial.printf(VT_CYAN "[ARPEG]" VT_RESET);
          } else {
            Serial.printf("[NORMAL]");
          }
        } else {
          if (isArpeg) {
            Serial.printf(VT_CYAN "ARPEG" VT_RESET " ");
          } else {
            Serial.printf("NORMAL ");
          }
        }

        // Quantize field (ARPEG only)
        if (isArpeg) {
          bool editingQuantize = isEditing && (editField == 1);
          if (editingQuantize) {
            Serial.printf("    Quantize: " VT_CYAN "[%s]" VT_RESET, QUANTIZE_NAMES[wkQuantize[i]]);
          } else {
            Serial.printf("    Quantize: %s", QUANTIZE_NAMES[wkQuantize[i]]);
          }
        }

        Serial.printf(VT_CL "\n");
      }

      // Description
      drawDescription(cursor, wkTypes[cursor] == BANK_ARPEG);

      Serial.printf(VT_CL "\n");

      // Error message
      if (errorShown) {
        Serial.printf(VT_RED "  Max 4 ARPEG banks!" VT_RESET VT_CL "\n");
      }

      // Status line
      if (confirmDefaults) {
        Serial.printf(VT_YELLOW "  Reset to defaults? (y/n)" VT_RESET VT_CL "\n");
      } else if (editing) {
        if (editField == 0 && wkTypes[cursor] == BANK_ARPEG) {
          Serial.printf(VT_DIM "  [Left/Right] change  [Down] quantize  [Enter] save" VT_RESET VT_CL "\n");
        } else if (editField == 1) {
          Serial.printf(VT_DIM "  [Left/Right] change  [Up] type  [Enter] save" VT_RESET VT_CL "\n");
        } else {
          Serial.printf(VT_DIM "  [Left/Right] change  [Enter] save" VT_RESET VT_CL "\n");
        }
      } else {
        Serial.printf(VT_DIM "  [Up/Down] navigate  [Enter] edit  [d] defaults  [q] quit" VT_RESET VT_CL "\n");
      }

      _ui->vtFrameEnd();
    }

    delay(5);
  }
}
