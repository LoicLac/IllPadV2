#include "ToolBankConfig.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"
#include "InputParser.h"
#include <Arduino.h>
#include <Preferences.h>

static const char* QUANTIZE_NAMES[] = {"Immediate", "Beat"};
static const char  GROUP_LABELS[NUM_SCALE_GROUPS + 1] = {'-', 'A', 'B', 'C', 'D'};

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
bool ToolBankConfig::saveConfig(const BankType* types, const uint8_t* quantize, const uint8_t* groups) {
  BankTypeStore bts;
  bts.magic = EEPROM_MAGIC;
  bts.version = BANKTYPE_VERSION;
  bts.reserved = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) bts.types[i] = (uint8_t)types[i];
  memcpy(bts.quantize, quantize, NUM_BANKS);
  memcpy(bts.scaleGroup, groups, NUM_BANKS);
  // V3 ARPEG_GEN params : Phase 2 ecrit les defaults (Phase 7 etendra la signature pour recevoir
  // les valeurs editees dans Tool 5).
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    bts.bonusPilex10[i] = 15;
    bts.marginWalk[i]   = 7;
  }
  validateBankTypeStore(bts);

  if (!NvsManager::saveBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2, &bts, sizeof(bts)))
    return false;

  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _banks[i].type = (BankType)bts.types[i];
    if (_nvs) {
      _nvs->setLoadedQuantizeMode(i, bts.quantize[i]);
      _nvs->setLoadedScaleGroup(i, bts.scaleGroup[i]);
    }
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
  uint8_t  wkGroups[NUM_BANKS];
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    wkTypes[i] = _banks[i].type;
    wkQuantize[i] = _nvs ? _nvs->getLoadedQuantizeMode(i) : DEFAULT_ARP_START_MODE;
    wkGroups[i] = _nvs ? _nvs->getLoadedScaleGroup(i) : 0;
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
        wkTypes[i]    = (BankType)bts.types[i];
        wkQuantize[i] = bts.quantize[i];
        wkGroups[i]   = bts.scaleGroup[i];
      }
    }
  }

  // Snapshot NVS-loaded values for cancel-edit restore
  BankType savedTypes[NUM_BANKS];
  uint8_t  savedQuantize[NUM_BANKS];
  uint8_t  savedGroups[NUM_BANKS];
  memcpy(savedTypes,    wkTypes,    sizeof(savedTypes));
  memcpy(savedQuantize, wkQuantize, sizeof(savedQuantize));
  memcpy(savedGroups,   wkGroups,   sizeof(savedGroups));

  Serial.print(ITERM_RESIZE);

  InputParser input;
  uint8_t cursor = 0;
  bool editing = false;
  bool screenDirty = true;
  bool confirmDefaults = false;
  bool errorShown = false;
  unsigned long errorTime = 0;

  _ui->vtClear();

  while (true) {
    if (_leds) _leds->update();

    NavEvent ev = input.update();

    if (errorShown && (millis() - errorTime) > 2000) {
      errorShown = false;
      screenDirty = true;
    }

    // --- Defaults confirmation ---
    if (confirmDefaults) {
      ConfirmResult r = SetupUI::parseConfirm(ev);
      if (r == CONFIRM_YES) {
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
          wkTypes[i]    = (i < 4) ? BANK_NORMAL : BANK_ARPEG;
          wkQuantize[i] = ARP_START_IMMEDIATE;
          // Group A : 2 premieres NORMAL (0,1) + 2 premieres ARPEG (4,5). Autres = -
          wkGroups[i]   = (i == 0 || i == 1 || i == 4 || i == 5) ? 1 : 0;
        }
        if (saveConfig(wkTypes, wkQuantize, wkGroups)) {
          memcpy(savedTypes,    wkTypes,    sizeof(savedTypes));
          memcpy(savedQuantize, wkQuantize, sizeof(savedQuantize));
          memcpy(savedGroups,   wkGroups,   sizeof(savedGroups));
          nvsSaved = true;
          _ui->flashSaved();
        }
        confirmDefaults = false;
        editing = false;
        screenDirty = true;
      } else if (r == CONFIRM_NO) {
        confirmDefaults = false;
        screenDirty = true;
      }
      delay(5);
      continue;
    }

    // --- Main navigation ---
    if (ev.type == NAV_QUIT) {
      if (editing) {
        wkTypes[cursor]    = savedTypes[cursor];
        wkQuantize[cursor] = savedQuantize[cursor];
        wkGroups[cursor]   = savedGroups[cursor];
        editing = false;
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
        screenDirty = true;
      }
    } else {
      // Flat cycle: NORMAL → ARPEG-Immediate → ARPEG-Beat → NORMAL
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
        } else if (wkQuantize[cursor] < ARP_START_BEAT) {
          // ARPEG: Immediate → Beat
          wkQuantize[cursor]++;
          errorShown = false;
        } else {
          // ARPEG-Beat → NORMAL
          wkTypes[cursor] = BANK_NORMAL;
          wkQuantize[cursor] = DEFAULT_ARP_START_MODE;
          errorShown = false;
        }
        screenDirty = true;
      } else if (ev.type == NAV_LEFT) {
        if (wkTypes[cursor] == BANK_NORMAL) {
          // NORMAL → ARPEG-Beat (check 4-limit)
          uint8_t arpCount = 0;
          for (uint8_t i = 0; i < NUM_BANKS; i++) {
            if (wkTypes[i] == BANK_ARPEG) arpCount++;
          }
          if (arpCount >= 4) {
            errorShown = true;
            errorTime = millis();
          } else {
            wkTypes[cursor] = BANK_ARPEG;
            wkQuantize[cursor] = ARP_START_BEAT;
            errorShown = false;
          }
        } else if (wkQuantize[cursor] > ARP_START_IMMEDIATE) {
          // ARPEG: Beat → Immediate
          wkQuantize[cursor]--;
          errorShown = false;
        } else {
          // ARPEG-Immediate → NORMAL
          wkTypes[cursor] = BANK_NORMAL;
          wkQuantize[cursor] = DEFAULT_ARP_START_MODE;
          errorShown = false;
        }
        screenDirty = true;
      } else if (ev.type == NAV_UP) {
        // Cycle group forward : -, A, B, C, D, -
        wkGroups[cursor] = (wkGroups[cursor] + 1) % (NUM_SCALE_GROUPS + 1);
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        // Cycle group backward : -, D, C, B, A, -
        wkGroups[cursor] = (wkGroups[cursor] + NUM_SCALE_GROUPS) % (NUM_SCALE_GROUPS + 1);
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        if (saveConfig(wkTypes, wkQuantize, wkGroups)) {
          memcpy(savedTypes,    wkTypes,    sizeof(savedTypes));
          memcpy(savedQuantize, wkQuantize, sizeof(savedQuantize));
          memcpy(savedGroups,   wkGroups,   sizeof(savedGroups));
          editing = false;
          nvsSaved = true;
          _ui->flashSaved();
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
        // Phase 3 : cycle Tool 5 reste 3-etats (Phase 4 Task 7 introduit ARPEG_GEN).
        // isArpType couvre BANK_ARPEG_GEN au cas ou — rendering ARPEG-style commun.
        bool isArpeg = isArpType(wkTypes[i]);

        char line[160];
        int pos = 0;
        int visibleLen = 0;  // caracteres visibles (hors codes VT)

        // Selection indicator (2 chars visibles)
        if (selected) {
          pos += snprintf(line + pos, sizeof(line) - pos, VT_CYAN VT_BOLD "> " VT_RESET);
        } else {
          pos += snprintf(line + pos, sizeof(line) - pos, "  ");
        }
        visibleLen += 2;

        // Bank label (10 chars visibles : "Bank N    ")
        pos += snprintf(line + pos, sizeof(line) - pos, "Bank %d    ", i + 1);
        visibleLen += 10;

        // Type + quantize — editing shows [brackets] + cyan
        if (isEditing) {
          if (isArpeg) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            VT_CYAN VT_BOLD "[ARPEG ─ %s]" VT_RESET, QUANTIZE_NAMES[wkQuantize[i]]);
            visibleLen += 10 + (int)strlen(QUANTIZE_NAMES[wkQuantize[i]]);  // "[ARPEG ─ X]" : ─ = 1 char visible
          } else {
            pos += snprintf(line + pos, sizeof(line) - pos, VT_CYAN VT_BOLD "[NORMAL]" VT_RESET);
            visibleLen += 8;  // "[NORMAL]"
          }
        } else {
          if (isArpeg) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            selected ? VT_CYAN "ARPEG" VT_RESET "   " : "ARPEG   ");
            visibleLen += 8;  // "ARPEG   "
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "    Quantize: %s", QUANTIZE_NAMES[wkQuantize[i]]);
            visibleLen += 14 + (int)strlen(QUANTIZE_NAMES[wkQuantize[i]]);  // "    Quantize: X"
          } else {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            selected ? VT_CYAN "NORMAL" VT_RESET "  " : "NORMAL  ");
            visibleLen += 8;  // "NORMAL  "
          }
        }

        // Padding jusqu'a colonne Group (position visible 50)
        const int GROUP_COL = 50;
        while (visibleLen < GROUP_COL && pos < (int)sizeof(line) - 32) {
          line[pos++] = ' ';
          visibleLen++;
        }

        // Colonne Group : "Group: X" avec letter highlighted en edit mode
        if (isEditing) {
          pos += snprintf(line + pos, sizeof(line) - pos,
                          "Group: " VT_CYAN VT_BOLD "%c" VT_RESET,
                          GROUP_LABELS[wkGroups[i]]);
        } else if (selected) {
          pos += snprintf(line + pos, sizeof(line) - pos,
                          "Group: " VT_CYAN "%c" VT_RESET,
                          GROUP_LABELS[wkGroups[i]]);
        } else {
          pos += snprintf(line + pos, sizeof(line) - pos,
                          "Group: %c", GROUP_LABELS[wkGroups[i]]);
        }

        _ui->drawFrameLine("%s", line);
      }

      _ui->drawFrameEmpty();

      // Info section
      _ui->drawSection("INFO");

      if (confirmDefaults) {
        _ui->drawFrameLine(VT_YELLOW "Reset to defaults? Banks 1-4 NORMAL, 5-8 ARPEG, Immediate, group A = banks 1/2/5/6. (y/n)" VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
      } else {
        drawDescription(cursor, isArpType(wkTypes[cursor]));

        // Quantize description (when editing an ARPEG/ARPEG_GEN bank)
        if (editing && isArpType(wkTypes[cursor])) {
          _ui->drawFrameEmpty();
          switch (wkQuantize[cursor]) {
            case 0:
              _ui->drawFrameLine(VT_DIM "Immediate: arp starts on next clock division boundary. Lowest latency." VT_RESET);
              break;
            case 1:
              _ui->drawFrameLine(VT_DIM "Beat: arp starts synced to next quarter note (24 ticks). Musical alignment." VT_RESET);
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
        _ui->drawControlBar(CBAR_CONFIRM_ANY);
      } else if (editing) {
        _ui->drawControlBar(VT_DIM "[</>] TYPE  [^v] GROUP" CBAR_SEP "[RET] SAVE" CBAR_SEP "[q] CANCEL" VT_RESET);
      } else {
        _ui->drawControlBar(VT_DIM "[^v] NAV" CBAR_SEP "[RET] EDIT  [d] DFLT" CBAR_SEP "[q] EXIT" VT_RESET);
      }

      _ui->vtFrameEnd();
    }

    delay(5);
  }
}
