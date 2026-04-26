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
// saveConfig — write types + quantize + scaleGroup + bonusPilex10 + marginWalk to NVS,
// update live banks + NvsManager loaded snapshot
// =================================================================
bool ToolBankConfig::saveConfig(const BankType* types, const uint8_t* quantize, const uint8_t* groups,
                                const uint8_t* bonusPilex10, const uint8_t* marginWalk) {
  BankTypeStore bts;
  bts.magic = EEPROM_MAGIC;
  bts.version = BANKTYPE_VERSION;
  bts.reserved = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) bts.types[i] = (uint8_t)types[i];
  memcpy(bts.quantize,     quantize,     NUM_BANKS);
  memcpy(bts.scaleGroup,   groups,       NUM_BANKS);
  memcpy(bts.bonusPilex10, bonusPilex10, NUM_BANKS);
  memcpy(bts.marginWalk,   marginWalk,   NUM_BANKS);
  validateBankTypeStore(bts);

  if (!NvsManager::saveBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2, &bts, sizeof(bts)))
    return false;

  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _banks[i].type = (BankType)bts.types[i];
    if (_nvs) {
      _nvs->setLoadedQuantizeMode(i, bts.quantize[i]);
      _nvs->setLoadedScaleGroup(i, bts.scaleGroup[i]);
      _nvs->setLoadedBonusPile(i, bts.bonusPilex10[i]);
      _nvs->setLoadedMarginWalk(i, bts.marginWalk[i]);
    }
  }
  return true;
}

// =================================================================
// drawDescription — expanded info panel (3 cas : NORMAL / ARPEG / ARPEG_GEN)
// Phase 4 ship le cas ARPEG_GEN en placeholder ; Phase 7 Task 17 finalise le INFO panel §25.
// =================================================================
void ToolBankConfig::drawDescription(uint8_t cursor, BankType type) {
  if (type == BANK_ARPEG_GEN) {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank %d" VT_RESET VT_DIM "  --  ARPEG_GEN  --  MIDI channel %d" VT_RESET, cursor + 1, cursor + 1);
    _ui->drawFrameLine(VT_DIM "Generative arpeggiator. Pile vivante + walk pondere + mutation par pad oct." VT_RESET);
    _ui->drawFrameLine(VT_DIM "R2+hold balaye 15 positions de grille. Pad oct 1 = lock. Bonus/Margin per-bank." VT_RESET);
    _ui->drawFrameLine(VT_DIM "Gate, shuffle, division, template, velocity : pot mapping comme ARPEG classique." VT_RESET);
  } else if (type == BANK_ARPEG) {
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
  uint8_t  wkBonusPilex10[NUM_BANKS];
  uint8_t  wkMarginWalk[NUM_BANKS];
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    wkTypes[i]         = _banks[i].type;
    wkQuantize[i]      = _nvs ? _nvs->getLoadedQuantizeMode(i) : DEFAULT_ARP_START_MODE;
    wkGroups[i]        = _nvs ? _nvs->getLoadedScaleGroup(i) : 0;
    wkBonusPilex10[i]  = _nvs ? _nvs->getLoadedBonusPile(i) : 15;
    wkMarginWalk[i]    = _nvs ? _nvs->getLoadedMarginWalk(i) : 7;
  }

  // Override from NVS if valid v3 store exists
  bool nvsSaved = false;
  {
    BankTypeStore bts;
    if (NvsManager::loadBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2,
                              EEPROM_MAGIC, BANKTYPE_VERSION, &bts, sizeof(bts))) {
      nvsSaved = true;
      validateBankTypeStore(bts);
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        wkTypes[i]         = (BankType)bts.types[i];
        wkQuantize[i]      = bts.quantize[i];
        wkGroups[i]        = bts.scaleGroup[i];
        wkBonusPilex10[i]  = bts.bonusPilex10[i];
        wkMarginWalk[i]    = bts.marginWalk[i];
      }
    }
  }

  // Snapshot NVS-loaded values for cancel-edit restore
  BankType savedTypes[NUM_BANKS];
  uint8_t  savedQuantize[NUM_BANKS];
  uint8_t  savedGroups[NUM_BANKS];
  uint8_t  savedBonusPilex10[NUM_BANKS];
  uint8_t  savedMarginWalk[NUM_BANKS];
  memcpy(savedTypes,         wkTypes,         sizeof(savedTypes));
  memcpy(savedQuantize,      wkQuantize,      sizeof(savedQuantize));
  memcpy(savedGroups,        wkGroups,        sizeof(savedGroups));
  memcpy(savedBonusPilex10,  wkBonusPilex10,  sizeof(savedBonusPilex10));
  memcpy(savedMarginWalk,    wkMarginWalk,    sizeof(savedMarginWalk));

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
          wkTypes[i]         = (i < 4) ? BANK_NORMAL : BANK_ARPEG;
          wkQuantize[i]      = ARP_START_IMMEDIATE;
          // Group A : 2 premieres NORMAL (0,1) + 2 premieres ARPEG (4,5). Autres = -
          wkGroups[i]        = (i == 0 || i == 1 || i == 4 || i == 5) ? 1 : 0;
          wkBonusPilex10[i]  = 15;   // spec §23 : defaults bonus_pile = 1.5
          wkMarginWalk[i]    = 7;    // spec §23 : defaults margin = 7
        }
        if (saveConfig(wkTypes, wkQuantize, wkGroups, wkBonusPilex10, wkMarginWalk)) {
          memcpy(savedTypes,         wkTypes,         sizeof(savedTypes));
          memcpy(savedQuantize,      wkQuantize,      sizeof(savedQuantize));
          memcpy(savedGroups,        wkGroups,        sizeof(savedGroups));
          memcpy(savedBonusPilex10,  wkBonusPilex10,  sizeof(savedBonusPilex10));
          memcpy(savedMarginWalk,    wkMarginWalk,    sizeof(savedMarginWalk));
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
        wkTypes[cursor]         = savedTypes[cursor];
        wkQuantize[cursor]      = savedQuantize[cursor];
        wkGroups[cursor]        = savedGroups[cursor];
        wkBonusPilex10[cursor]  = savedBonusPilex10[cursor];
        wkMarginWalk[cursor]    = savedMarginWalk[cursor];
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
      // Flat cycle 5-etats : NORMAL -> ARPEG-Imm -> ARPEG-Beat -> ARPEG_GEN-Imm -> ARPEG_GEN-Beat -> NORMAL
      // arpCount cumule ARPEG + ARPEG_GEN (cap MAX_ARP_BANKS = 4) via isArpType.
      if (ev.type == NAV_RIGHT) {
        BankType cur = wkTypes[cursor];
        if (cur == BANK_NORMAL) {
          // NORMAL -> ARPEG-Imm (check arp limit 4)
          uint8_t arpCount = 0;
          for (uint8_t i = 0; i < NUM_BANKS; i++) {
            if (isArpType((BankType)wkTypes[i])) arpCount++;
          }
          if (arpCount >= MAX_ARP_BANKS) {
            errorShown = true;
            errorTime = millis();
          } else {
            wkTypes[cursor]    = BANK_ARPEG;
            wkQuantize[cursor] = ARP_START_IMMEDIATE;
            errorShown = false;
          }
        } else if (cur == BANK_ARPEG && wkQuantize[cursor] == ARP_START_IMMEDIATE) {
          wkQuantize[cursor] = ARP_START_BEAT;
          errorShown = false;
        } else if (cur == BANK_ARPEG /* && Beat */) {
          // ARPEG-Beat -> ARPEG_GEN-Imm (already counted, no cap check)
          wkTypes[cursor]    = BANK_ARPEG_GEN;
          wkQuantize[cursor] = ARP_START_IMMEDIATE;
          errorShown = false;
        } else if (cur == BANK_ARPEG_GEN && wkQuantize[cursor] == ARP_START_IMMEDIATE) {
          wkQuantize[cursor] = ARP_START_BEAT;
          errorShown = false;
        } else {
          // ARPEG_GEN-Beat -> NORMAL
          wkTypes[cursor]    = BANK_NORMAL;
          wkQuantize[cursor] = DEFAULT_ARP_START_MODE;
          errorShown = false;
        }
        screenDirty = true;
      } else if (ev.type == NAV_LEFT) {
        BankType cur = wkTypes[cursor];
        if (cur == BANK_NORMAL) {
          // NORMAL -> ARPEG_GEN-Beat (sens inverse, check arp limit 4)
          uint8_t arpCount = 0;
          for (uint8_t i = 0; i < NUM_BANKS; i++) {
            if (isArpType((BankType)wkTypes[i])) arpCount++;
          }
          if (arpCount >= MAX_ARP_BANKS) {
            errorShown = true;
            errorTime = millis();
          } else {
            wkTypes[cursor]    = BANK_ARPEG_GEN;
            wkQuantize[cursor] = ARP_START_BEAT;
            errorShown = false;
          }
        } else if (cur == BANK_ARPEG_GEN && wkQuantize[cursor] == ARP_START_BEAT) {
          wkQuantize[cursor] = ARP_START_IMMEDIATE;
          errorShown = false;
        } else if (cur == BANK_ARPEG_GEN /* && Imm */) {
          // ARPEG_GEN-Imm -> ARPEG-Beat
          wkTypes[cursor]    = BANK_ARPEG;
          wkQuantize[cursor] = ARP_START_BEAT;
          errorShown = false;
        } else if (cur == BANK_ARPEG && wkQuantize[cursor] == ARP_START_BEAT) {
          wkQuantize[cursor] = ARP_START_IMMEDIATE;
          errorShown = false;
        } else {
          // ARPEG-Imm -> NORMAL
          wkTypes[cursor]    = BANK_NORMAL;
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
        if (saveConfig(wkTypes, wkQuantize, wkGroups, wkBonusPilex10, wkMarginWalk)) {
          memcpy(savedTypes,         wkTypes,         sizeof(savedTypes));
          memcpy(savedQuantize,      wkQuantize,      sizeof(savedQuantize));
          memcpy(savedGroups,        wkGroups,        sizeof(savedGroups));
          memcpy(savedBonusPilex10,  wkBonusPilex10,  sizeof(savedBonusPilex10));
          memcpy(savedMarginWalk,    wkMarginWalk,    sizeof(savedMarginWalk));
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
        if (isArpType((BankType)wkTypes[i])) arpCount++;
      }

      char headerRight[32];
      snprintf(headerRight, sizeof(headerRight), "TOOL 4: BANK CONFIG  %d/4 ARP", arpCount);

      _ui->vtFrameStart();
      _ui->drawConsoleHeader(headerRight, nvsSaved);
      _ui->drawFrameEmpty();

      // Banks section
      _ui->drawSection("BANKS");
      _ui->drawFrameEmpty();

      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        bool selected = (cursor == i);
        bool isEditing = selected && editing;
        BankType bt = wkTypes[i];

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

        // Type + quantize — editing shows [brackets] + cyan.
        // Phase 4 : ARPEG_GEN affiche "ARPEG_GEN" sur 1 ligne (Phase 7 Task 16 ajoute la 2e ligne Bonus/Margin).
        if (isEditing) {
          if (bt == BANK_ARPEG_GEN) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            VT_CYAN VT_BOLD "[ARPEG_GEN ─ %s]" VT_RESET, QUANTIZE_NAMES[wkQuantize[i]]);
            visibleLen += 14 + (int)strlen(QUANTIZE_NAMES[wkQuantize[i]]);  // "[ARPEG_GEN ─ X]" : ─ = 1 char
          } else if (bt == BANK_ARPEG) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            VT_CYAN VT_BOLD "[ARPEG ─ %s]" VT_RESET, QUANTIZE_NAMES[wkQuantize[i]]);
            visibleLen += 10 + (int)strlen(QUANTIZE_NAMES[wkQuantize[i]]);  // "[ARPEG ─ X]"
          } else {
            pos += snprintf(line + pos, sizeof(line) - pos, VT_CYAN VT_BOLD "[NORMAL]" VT_RESET);
            visibleLen += 8;  // "[NORMAL]"
          }
        } else {
          if (bt == BANK_ARPEG_GEN) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            selected ? VT_CYAN "ARPEG_GEN" VT_RESET : "ARPEG_GEN");
            visibleLen += 9;
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "   Quantize: %s", QUANTIZE_NAMES[wkQuantize[i]]);
            visibleLen += 13 + (int)strlen(QUANTIZE_NAMES[wkQuantize[i]]);  // "   Quantize: X"
          } else if (bt == BANK_ARPEG) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            selected ? VT_CYAN "ARPEG" VT_RESET "   " : "ARPEG   ");
            visibleLen += 8;
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "    Quantize: %s", QUANTIZE_NAMES[wkQuantize[i]]);
            visibleLen += 14 + (int)strlen(QUANTIZE_NAMES[wkQuantize[i]]);
          } else {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            selected ? VT_CYAN "NORMAL" VT_RESET "  " : "NORMAL  ");
            visibleLen += 8;
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
        drawDescription(cursor, wkTypes[cursor]);

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
        _ui->drawFrameLine(VT_BRIGHT_RED "Max 4 ARP banks!" VT_RESET);
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
