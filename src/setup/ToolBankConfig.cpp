#include "ToolBankConfig.h"
#include "../core/LedController.h"
#include "../core/PotFilter.h"
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
  BankTypeStore bts;
  bts.magic = EEPROM_MAGIC;
  bts.version = BANKTYPE_VERSION;
  bts.reserved = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) bts.types[i] = (uint8_t)types[i];
  memcpy(bts.quantize, quantize, NUM_BANKS);

  if (!NvsManager::saveBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2, &bts, sizeof(bts)))
    return false;

  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _banks[i].type = types[i];
    if (_nvs) _nvs->setLoadedQuantizeMode(i, quantize[i]);
  }
  return true;
}

// =================================================================
// drawDescription — expanded info panel
// =================================================================
void ToolBankConfig::drawDescription(uint8_t cursor, bool isArpeg) {
  if (isArpeg) {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank %d" VT_RESET VT_DIM "  --  ARPEG  --  MIDI channel %d" VT_RESET, cursor + 1, cursor + 1);
    _ui->drawFrameLine(VT_DIM "Arpeggiator on this channel. No aftertouch. Pile-based note input:" VT_RESET);
    _ui->drawFrameLine(VT_DIM "press=add, release=remove (HOLD OFF) or double-tap=remove (HOLD ON)." VT_RESET);
    _ui->drawFrameLine(VT_DIM "Gate, shuffle, pattern, division, velocity are per-bank via pot mapping." VT_RESET);
  } else {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank %d" VT_RESET VT_DIM "  --  NORMAL  --  MIDI channel %d" VT_RESET, cursor + 1, cursor + 1);
    _ui->drawFrameLine(VT_DIM "Pads play notes directly with polyphonic aftertouch." VT_RESET);
    _ui->drawFrameLine(VT_DIM "Velocity = baseVelocity +/- variation (per-bank). Pitch bend offset per-bank." VT_RESET);
    _ui->drawFrameLine(VT_DIM "Shape, slew, AT deadzone are global (affect all banks)." VT_RESET);
  }
}

// =================================================================
// run() — Unified arrow navigation with cursor + editing
// =================================================================
void ToolBankConfig::run() {
  if (!_ui || !_banks) return;

  BankType wkTypes[NUM_BANKS];
  uint8_t  wkQuantize[NUM_BANKS];
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    wkTypes[i] = _banks[i].type;
    wkQuantize[i] = _nvs ? _nvs->getLoadedQuantizeMode(i) : DEFAULT_ARP_START_MODE;
  }

  // Override from NVS if valid v2 store exists
  bool nvsSaved = false;
  {
    BankTypeStore bts;
    if (NvsManager::loadBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2,
                              EEPROM_MAGIC, BANKTYPE_VERSION, &bts, sizeof(bts))) {
      nvsSaved = true;
      validateBankTypeStore(bts);
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        wkTypes[i] = (BankType)bts.types[i];
        wkQuantize[i] = bts.quantize[i];
      }
    }
  }

  // Snapshot NVS-loaded values for cancel-edit restore
  BankType savedTypes[NUM_BANKS];
  uint8_t  savedQuantize[NUM_BANKS];
  memcpy(savedTypes, wkTypes, sizeof(savedTypes));
  memcpy(savedQuantize, wkQuantize, sizeof(savedQuantize));

  Serial.print(ITERM_RESIZE);

  InputParser input;
  uint8_t cursor = 0;
  bool editing = false;
  bool screenDirty = true;
  bool confirmDefaults = false;
  bool errorShown = false;
  unsigned long errorTime = 0;

  // Seed pot for bank navigation
  _potBankIdx = cursor;
  _pots.seed(0, &_potBankIdx, 0, NUM_BANKS - 1, POT_RELATIVE, 16);

  _ui->vtClear();

  while (true) {
    PotFilter::updateAll();
    _pots.update();
    if (_leds) _leds->update();

    // --- Pot navigation ---
    if (!confirmDefaults) {
      if (!editing && _pots.getMove(0)) {
        cursor = (uint8_t)_potBankIdx;
        screenDirty = true;
      } else if (editing && _pots.getMove(0)) {
        // Combined state pot: apply type/quantize
        if (_potComboState > 0) {
          // Check 4-ARPEG limit (exclude current bank)
          uint8_t arpCount = 0;
          for (uint8_t i = 0; i < NUM_BANKS; i++)
            if (i != cursor && wkTypes[i] == BANK_ARPEG) arpCount++;
          if (arpCount >= 4) {
            _potComboState = 0;  // Force back to NORMAL
            errorShown = true;
            errorTime = millis();
          }
        }
        if (_potComboState == 0) {
          wkTypes[cursor] = BANK_NORMAL;
          wkQuantize[cursor] = DEFAULT_ARP_START_MODE;
        } else {
          wkTypes[cursor] = BANK_ARPEG;
          wkQuantize[cursor] = _potComboState - 1;
        }
        screenDirty = true;
      }
    }

    NavEvent ev = input.update();

    if (errorShown && (millis() - errorTime) > 2000) {
      errorShown = false;
      screenDirty = true;
    }

    // --- Defaults confirmation ---
    if (confirmDefaults) {
      if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
          wkTypes[i] = (i < 4) ? BANK_NORMAL : BANK_ARPEG;
          wkQuantize[i] = ARP_START_IMMEDIATE;
        }
        if (saveConfig(wkTypes, wkQuantize)) {
          memcpy(savedTypes, wkTypes, sizeof(savedTypes));
          memcpy(savedQuantize, wkQuantize, sizeof(savedQuantize));
          nvsSaved = true;
          _ui->flashSaved();
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
        wkTypes[cursor] = savedTypes[cursor];
        wkQuantize[cursor] = savedQuantize[cursor];
        editing = false;
        // Re-seed pot for nav mode
        _potBankIdx = cursor;
        _pots.seed(0, &_potBankIdx, 0, NUM_BANKS - 1, POT_RELATIVE, 16);
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
        _potBankIdx = cursor;  // Sync pot target
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        if (cursor < NUM_BANKS - 1) cursor++;
        else cursor = 0;
        _potBankIdx = cursor;  // Sync pot target
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        editing = true;
        // Seed pot for combined state cycle
        _potComboState = (wkTypes[cursor] == BANK_NORMAL) ? 0 : 1 + wkQuantize[cursor];
        _pots.seed(0, &_potComboState, 0, 3, POT_RELATIVE, 8);
        screenDirty = true;
      }
    } else {
      // Flat cycle: NORMAL → ARPEG-Immediate → ARPEG-Beat → ARPEG-Bar → NORMAL
      if (ev.type == NAV_RIGHT) {
        if (wkTypes[cursor] == BANK_NORMAL) {
          // NORMAL → ARPEG-Immediate (check 4-limit)
          uint8_t arpCount = 0;
          for (uint8_t i = 0; i < NUM_BANKS; i++) {
            if (wkTypes[i] == BANK_ARPEG) arpCount++;
          }
          if (arpCount >= 4) {
            errorShown = true;
            errorTime = millis();
          } else {
            wkTypes[cursor] = BANK_ARPEG;
            wkQuantize[cursor] = ARP_START_IMMEDIATE;
            errorShown = false;
          }
        } else if (wkQuantize[cursor] < ARP_START_BAR) {
          // ARPEG: Immediate → Beat → Bar
          wkQuantize[cursor]++;
          errorShown = false;
        } else {
          // ARPEG-Bar → NORMAL
          wkTypes[cursor] = BANK_NORMAL;
          wkQuantize[cursor] = DEFAULT_ARP_START_MODE;
          errorShown = false;
        }
        _potComboState = (wkTypes[cursor] == BANK_NORMAL) ? 0 : 1 + wkQuantize[cursor];
        screenDirty = true;
      } else if (ev.type == NAV_LEFT) {
        if (wkTypes[cursor] == BANK_NORMAL) {
          // NORMAL → ARPEG-Bar (check 4-limit)
          uint8_t arpCount = 0;
          for (uint8_t i = 0; i < NUM_BANKS; i++) {
            if (wkTypes[i] == BANK_ARPEG) arpCount++;
          }
          if (arpCount >= 4) {
            errorShown = true;
            errorTime = millis();
          } else {
            wkTypes[cursor] = BANK_ARPEG;
            wkQuantize[cursor] = ARP_START_BAR;
            errorShown = false;
          }
        } else if (wkQuantize[cursor] > ARP_START_IMMEDIATE) {
          // ARPEG: Bar → Beat → Immediate
          wkQuantize[cursor]--;
          errorShown = false;
        } else {
          // ARPEG-Immediate → NORMAL
          wkTypes[cursor] = BANK_NORMAL;
          wkQuantize[cursor] = DEFAULT_ARP_START_MODE;
          errorShown = false;
        }
        _potComboState = (wkTypes[cursor] == BANK_NORMAL) ? 0 : 1 + wkQuantize[cursor];
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        if (saveConfig(wkTypes, wkQuantize)) {
          memcpy(savedTypes, wkTypes, sizeof(savedTypes));
          memcpy(savedQuantize, wkQuantize, sizeof(savedQuantize));
          editing = false;
          nvsSaved = true;
          _ui->flashSaved();
          // Re-seed pot for nav mode
          _potBankIdx = cursor;
          _pots.seed(0, &_potBankIdx, 0, NUM_BANKS - 1, POT_RELATIVE, 16);
          screenDirty = true;
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

      char headerRight[32];
      snprintf(headerRight, sizeof(headerRight), "TOOL 4: BANK CONFIG  %d/4 ARPEG", arpCount);

      _ui->vtFrameStart();
      _ui->drawConsoleHeader(headerRight, nvsSaved);
      _ui->drawFrameEmpty();

      // Banks section
      _ui->drawSection("BANKS");
      _ui->drawFrameEmpty();

      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        bool selected = (cursor == i);
        bool isEditing = selected && editing;
        bool isArpeg = (wkTypes[i] == BANK_ARPEG);

        char line[128];
        int pos = 0;

        // Selection indicator
        if (selected) {
          pos += snprintf(line + pos, sizeof(line) - pos, VT_CYAN VT_BOLD "> " VT_RESET);
        } else {
          pos += snprintf(line + pos, sizeof(line) - pos, "  ");
        }

        // Bank label
        pos += snprintf(line + pos, sizeof(line) - pos, "Bank %d    ", i + 1);

        // Type + quantize — editing shows [brackets] + cyan
        if (isEditing) {
          if (isArpeg) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            VT_CYAN VT_BOLD "[ARPEG ─ %s]" VT_RESET, QUANTIZE_NAMES[wkQuantize[i]]);
          } else {
            pos += snprintf(line + pos, sizeof(line) - pos, VT_CYAN VT_BOLD "[NORMAL]" VT_RESET);
          }
        } else {
          if (isArpeg) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            selected ? VT_CYAN "ARPEG" VT_RESET "  " : "ARPEG   ");
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "    Quantize: %s", QUANTIZE_NAMES[wkQuantize[i]]);
          } else {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            selected ? VT_CYAN "NORMAL" VT_RESET " " : "NORMAL  ");
          }
        }

        _ui->drawFrameLine("%s", line);
      }

      _ui->drawFrameEmpty();

      // Info section
      _ui->drawSection("INFO");

      if (confirmDefaults) {
        _ui->drawFrameLine(VT_YELLOW "Reset to defaults? Banks 1-4 NORMAL, 5-8 ARPEG, all Immediate. (y/n)" VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
      } else {
        drawDescription(cursor, wkTypes[cursor] == BANK_ARPEG);

        // Quantize description (when editing an ARPEG bank)
        if (editing && wkTypes[cursor] == BANK_ARPEG) {
          _ui->drawFrameEmpty();
          switch (wkQuantize[cursor]) {
            case 0:
              _ui->drawFrameLine(VT_DIM "Immediate: arp starts on next clock division boundary. Lowest latency." VT_RESET);
              break;
            case 1:
              _ui->drawFrameLine(VT_DIM "Beat: arp starts synced to next quarter note (24 ticks). Musical alignment." VT_RESET);
              break;
            case 2:
              _ui->drawFrameLine(VT_DIM "Bar: arp starts synced to next bar (96 ticks). Perfect for loop-aligned sets." VT_RESET);
              break;
          }
        }
      }

      if (errorShown) {
        _ui->drawFrameEmpty();
        _ui->drawFrameLine(VT_BRIGHT_RED "Max 4 ARPEG banks!" VT_RESET);
      }

      _ui->drawFrameEmpty();

      // Control bar
      if (confirmDefaults) {
        _ui->drawControlBar(VT_DIM "[y] confirm  [any] cancel" VT_RESET);
      } else if (editing) {
        _ui->drawControlBar(VT_DIM "[</>] CHANGE  [RET] SAVE  [q] CANCEL" VT_RESET);
      } else {
        _ui->drawControlBar(VT_DIM "[^v] NAV  [RET] EDIT  [d] DFLT  [q] EXIT" VT_RESET);
      }

      _ui->vtFrameEnd();
    }

    delay(5);
  }
}
