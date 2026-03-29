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

  // Load saved bank config from NVS (setup mode may run before NvsManager::loadAll)
  bool nvsSaved = false;
  {
    Preferences prefs;
    if (prefs.begin(BANKTYPE_NVS_NAMESPACE, true)) {
      size_t len = prefs.getBytesLength(BANKTYPE_NVS_KEY);
      nvsSaved = (len == NUM_BANKS);
      if (nvsSaved) {
        uint8_t rawTypes[NUM_BANKS];
        prefs.getBytes(BANKTYPE_NVS_KEY, rawTypes, NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++)
          wkTypes[i] = ((BankType)rawTypes[i] <= BANK_ARPEG) ? (BankType)rawTypes[i] : BANK_NORMAL;
      }
      size_t qlen = prefs.getBytesLength("qmode");
      if (qlen == NUM_BANKS) {
        uint8_t rawQ[NUM_BANKS];
        prefs.getBytes("qmode", rawQ, NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++)
          wkQuantize[i] = (rawQ[i] < NUM_ARP_START_MODES) ? rawQ[i] : DEFAULT_ARP_START_MODE;
      }
      prefs.end();
    }
  }

  Serial.print(ITERM_RESIZE);

  InputParser input;
  uint8_t cursor = 0;
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
        editField = 0;
        screenDirty = true;
      }
    } else {
      if (editField == 0) {
        if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
          if (wkTypes[cursor] == BANK_NORMAL) {
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
          editField = 1;
          screenDirty = true;
        } else if (ev.type == NAV_ENTER) {
          if (saveConfig(wkTypes, wkQuantize)) {
            editing = false;
            nvsSaved = true;
            _ui->flashSaved();
            screenDirty = true;
          }
        }
      } else {
        // editField == 1: quantize
        if (ev.type == NAV_LEFT) {
          wkQuantize[cursor] = (wkQuantize[cursor] + NUM_ARP_START_MODES - 1) % NUM_ARP_START_MODES;
          screenDirty = true;
        } else if (ev.type == NAV_RIGHT) {
          wkQuantize[cursor] = (wkQuantize[cursor] + 1) % NUM_ARP_START_MODES;
          screenDirty = true;
        } else if (ev.type == NAV_UP) {
          editField = 0;
          screenDirty = true;
        } else if (ev.type == NAV_ENTER) {
          if (saveConfig(wkTypes, wkQuantize)) {
            editing = false;
            nvsSaved = true;
            _ui->flashSaved();
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

        // Type field — active field gets [brackets] + cyan
        bool editingType = isEditing && (editField == 0);
        bool editingQuantize = isEditing && (editField == 1);

        if (editingType) {
          if (isArpeg) {
            pos += snprintf(line + pos, sizeof(line) - pos, VT_CYAN VT_BOLD "[ARPEG]" VT_RESET);
          } else {
            pos += snprintf(line + pos, sizeof(line) - pos, VT_CYAN VT_BOLD "[NORMAL]" VT_RESET);
          }
        } else {
          if (isArpeg) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            selected ? VT_CYAN "ARPEG" VT_RESET "  " : "ARPEG   ");
          } else {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            selected ? VT_CYAN "NORMAL" VT_RESET " " : "NORMAL  ");
          }
        }

        // Quantize field — only for ARPEG
        if (isArpeg) {
          if (editingQuantize) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "    Quantize: " VT_CYAN VT_BOLD "[%s]" VT_RESET, QUANTIZE_NAMES[wkQuantize[i]]);
          } else if (isEditing) {
            // Editing type but quantize visible as dim hint
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "    " VT_DIM "Quantize: %s" VT_RESET, QUANTIZE_NAMES[wkQuantize[i]]);
          } else {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "    Quantize: %s", QUANTIZE_NAMES[wkQuantize[i]]);
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

        // Quantize descriptions (when editing quantize)
        if (editing && editField == 1) {
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
        if (editField == 0 && wkTypes[cursor] == BANK_ARPEG) {
          _ui->drawControlBar(VT_DIM "[</>] TYPE  [v] QUANTIZE  [RET] SAVE  [q] CANCEL" VT_RESET);
        } else if (editField == 1) {
          _ui->drawControlBar(VT_DIM "[</>] QUANTIZE  [^] TYPE  [RET] SAVE  [q] CANCEL" VT_RESET);
        } else {
          _ui->drawControlBar(VT_DIM "[</>] TYPE  [RET] SAVE  [q] CANCEL" VT_RESET);
        }
      } else {
        _ui->drawControlBar(VT_DIM "[^v] NAV  [RET] EDIT  [d] DFLT  [q] EXIT" VT_RESET);
      }

      _ui->vtFrameEnd();
    }

    delay(5);
  }
}
