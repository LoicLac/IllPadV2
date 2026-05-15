#include "ToolBankConfig.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"
#include "InputParser.h"
#include <Arduino.h>
#include <Preferences.h>

static const char* QUANTIZE_NAMES[] = {"Immediate", "Beat"};
static const char  GROUP_LABELS[NUM_SCALE_GROUPS + 1] = {'-', 'A', 'B', 'C', 'D'};

// Tool 5 ARPEG_GEN edit mode : cycle lineaire 6-champs TYPE -> GROUP -> BONUS -> MARGIN -> PROX -> ECART -> TYPE.
// V4 Task 22 : ajout SF_PROX (proximity_factor 0.4..2.0) + SF_ECART (1..12 override TABLE).
// Exception §4.4 strict (qui demande <-/-> horizontal sur la ligne, ^v change ligne) :
// le cycle lineaire est plus court (6 keypress max) et preserve la regle universelle ^v = adjust.
// Plan ARPEG_GEN §0 D5.
enum SubField : uint8_t {
  SF_TYPE = 0,
  SF_GROUP = 1,
  SF_BONUS = 2,
  SF_MARGIN = 3,
  SF_PROX = 4,
  SF_ECART = 5
};

ToolBankConfig::ToolBankConfig()
  : _leds(nullptr), _nvs(nullptr), _ui(nullptr), _banks(nullptr) {}

void ToolBankConfig::begin(LedController* leds, NvsManager* nvs, SetupUI* ui, BankSlot* banks) {
  _leds = leds;
  _nvs = nvs;
  _ui = ui;
  _banks = banks;
}

// =================================================================
// saveConfig — write types + quantize + scaleGroup + bonusPilex10 + marginWalk +
// proximityFactorx10 + ecart (V4) to NVS, update live banks + NvsManager loaded snapshot.
// =================================================================
bool ToolBankConfig::saveConfig(const BankType* types, const uint8_t* quantize, const uint8_t* groups,
                                const uint8_t* bonusPilex10, const uint8_t* marginWalk,
                                const uint8_t* proximityFactorx10, const uint8_t* ecart) {
  BankTypeStore bts;
  bts.magic = EEPROM_MAGIC;
  bts.version = BANKTYPE_VERSION;
  bts.reserved = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) bts.types[i] = (uint8_t)types[i];
  memcpy(bts.quantize,            quantize,            NUM_BANKS);
  memcpy(bts.scaleGroup,          groups,              NUM_BANKS);
  memcpy(bts.bonusPilex10,        bonusPilex10,        NUM_BANKS);
  memcpy(bts.marginWalk,          marginWalk,          NUM_BANKS);
  memcpy(bts.proximityFactorx10,  proximityFactorx10,  NUM_BANKS);
  memcpy(bts.ecart,               ecart,               NUM_BANKS);
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
      _nvs->setLoadedProximityFactor(i, bts.proximityFactorx10[i]);
      _nvs->setLoadedEcart(i, bts.ecart[i]);
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
  uint8_t  wkProximityx10[NUM_BANKS];
  uint8_t  wkEcart[NUM_BANKS];
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    wkTypes[i]         = _banks[i].type;
    wkQuantize[i]      = _nvs ? _nvs->getLoadedQuantizeMode(i) : DEFAULT_ARP_START_MODE;
    wkGroups[i]        = _nvs ? _nvs->getLoadedScaleGroup(i) : 0;
    wkBonusPilex10[i]  = _nvs ? _nvs->getLoadedBonusPile(i) : 15;
    wkMarginWalk[i]    = _nvs ? _nvs->getLoadedMarginWalk(i) : 7;
    wkProximityx10[i]  = _nvs ? _nvs->getLoadedProximityFactor(i) : 4;
    wkEcart[i]         = _nvs ? _nvs->getLoadedEcart(i) : 5;
  }

  // Override from NVS if valid v4 store exists
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
        wkProximityx10[i]  = bts.proximityFactorx10[i];
        wkEcart[i]         = bts.ecart[i];
      }
    }
  }

  // Snapshot NVS-loaded values for cancel-edit restore
  BankType savedTypes[NUM_BANKS];
  uint8_t  savedQuantize[NUM_BANKS];
  uint8_t  savedGroups[NUM_BANKS];
  uint8_t  savedBonusPilex10[NUM_BANKS];
  uint8_t  savedMarginWalk[NUM_BANKS];
  uint8_t  savedProximityx10[NUM_BANKS];
  uint8_t  savedEcart[NUM_BANKS];
  memcpy(savedTypes,         wkTypes,         sizeof(savedTypes));
  memcpy(savedQuantize,      wkQuantize,      sizeof(savedQuantize));
  memcpy(savedGroups,        wkGroups,        sizeof(savedGroups));
  memcpy(savedBonusPilex10,  wkBonusPilex10,  sizeof(savedBonusPilex10));
  memcpy(savedMarginWalk,    wkMarginWalk,    sizeof(savedMarginWalk));
  memcpy(savedProximityx10,  wkProximityx10,  sizeof(savedProximityx10));
  memcpy(savedEcart,         wkEcart,         sizeof(savedEcart));

  Serial.print(ITERM_RESIZE);

  InputParser input;
  uint8_t cursor = 0;
  bool editing = false;
  SubField cursorSubField = SF_TYPE;  // current focused field in edit mode
  bool screenDirty = true;
  bool confirmDefaults = false;
  bool errorShown = false;
  unsigned long errorTime = 0;

  // --- Sub-field cycling helpers (Phase 7 Task 16 + V4 Task 22) ---
  // ARPEG_GEN : 6 fields cycle (TYPE -> GROUP -> BONUS -> MARGIN -> PROX -> ECART -> TYPE).
  // NORMAL / ARPEG : 2 fields cycle (TYPE -> GROUP -> TYPE) — autres champs n'existent pas.
  auto nextSubField = [](SubField cur, BankType bt) -> SubField {
    if (bt == BANK_ARPEG_GEN) {
      switch (cur) {
        case SF_TYPE:   return SF_GROUP;
        case SF_GROUP:  return SF_BONUS;
        case SF_BONUS:  return SF_MARGIN;
        case SF_MARGIN: return SF_PROX;
        case SF_PROX:   return SF_ECART;
        case SF_ECART:  return SF_TYPE;
      }
    } else {
      return (cur == SF_TYPE) ? SF_GROUP : SF_TYPE;
    }
    return SF_TYPE;
  };
  auto prevSubField = [](SubField cur, BankType bt) -> SubField {
    if (bt == BANK_ARPEG_GEN) {
      switch (cur) {
        case SF_TYPE:   return SF_ECART;
        case SF_GROUP:  return SF_TYPE;
        case SF_BONUS:  return SF_GROUP;
        case SF_MARGIN: return SF_BONUS;
        case SF_PROX:   return SF_MARGIN;
        case SF_ECART:  return SF_PROX;
      }
    } else {
      return (cur == SF_TYPE) ? SF_GROUP : SF_TYPE;
    }
    return SF_TYPE;
  };

  // --- Type cycling helpers (Phase 4 Task 7 logic, factorise pour SF_TYPE NAV_UP/DOWN) ---
  // 5-states forward : NORMAL -> ARPEG-Imm -> ARPEG-Beat -> ARPEG_GEN-Imm -> ARPEG_GEN-Beat -> NORMAL
  // backward = inverse. Returns true if change applied, false if arp cap rejected.
  auto cycleTypeForward = [&](uint8_t c) -> bool {
    BankType cur = wkTypes[c];
    if (cur == BANK_NORMAL) {
      uint8_t arpCount = 0;
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        if (isArpType((BankType)wkTypes[i])) arpCount++;
      }
      if (arpCount >= MAX_ARP_BANKS) return false;
      wkTypes[c]    = BANK_ARPEG;
      wkQuantize[c] = ARP_START_IMMEDIATE;
    } else if (cur == BANK_ARPEG && wkQuantize[c] == ARP_START_IMMEDIATE) {
      wkQuantize[c] = ARP_START_BEAT;
    } else if (cur == BANK_ARPEG) {
      wkTypes[c]    = BANK_ARPEG_GEN;
      wkQuantize[c] = ARP_START_IMMEDIATE;
    } else if (cur == BANK_ARPEG_GEN && wkQuantize[c] == ARP_START_IMMEDIATE) {
      wkQuantize[c] = ARP_START_BEAT;
    } else {
      wkTypes[c]    = BANK_NORMAL;
      wkQuantize[c] = DEFAULT_ARP_START_MODE;
    }
    return true;
  };
  auto cycleTypeBackward = [&](uint8_t c) -> bool {
    BankType cur = wkTypes[c];
    if (cur == BANK_NORMAL) {
      uint8_t arpCount = 0;
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        if (isArpType((BankType)wkTypes[i])) arpCount++;
      }
      if (arpCount >= MAX_ARP_BANKS) return false;
      wkTypes[c]    = BANK_ARPEG_GEN;
      wkQuantize[c] = ARP_START_BEAT;
    } else if (cur == BANK_ARPEG_GEN && wkQuantize[c] == ARP_START_BEAT) {
      wkQuantize[c] = ARP_START_IMMEDIATE;
    } else if (cur == BANK_ARPEG_GEN) {
      wkTypes[c]    = BANK_ARPEG;
      wkQuantize[c] = ARP_START_BEAT;
    } else if (cur == BANK_ARPEG && wkQuantize[c] == ARP_START_BEAT) {
      wkQuantize[c] = ARP_START_IMMEDIATE;
    } else {
      wkTypes[c]    = BANK_NORMAL;
      wkQuantize[c] = DEFAULT_ARP_START_MODE;
    }
    return true;
  };

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
          wkBonusPilex10[i]  = 15;   // defaults bonus_pile = 1.5
          wkMarginWalk[i]    = 7;    // defaults margin = 7
          wkProximityx10[i]  = 4;    // V4 defaults proximity_factor = 0.4
          wkEcart[i]         = 5;    // V4 defaults ecart = 5
        }
        if (saveConfig(wkTypes, wkQuantize, wkGroups, wkBonusPilex10, wkMarginWalk,
                       wkProximityx10, wkEcart)) {
          memcpy(savedTypes,         wkTypes,         sizeof(savedTypes));
          memcpy(savedQuantize,      wkQuantize,      sizeof(savedQuantize));
          memcpy(savedGroups,        wkGroups,        sizeof(savedGroups));
          memcpy(savedBonusPilex10,  wkBonusPilex10,  sizeof(savedBonusPilex10));
          memcpy(savedMarginWalk,    wkMarginWalk,    sizeof(savedMarginWalk));
          memcpy(savedProximityx10,  wkProximityx10,  sizeof(savedProximityx10));
          memcpy(savedEcart,         wkEcart,         sizeof(savedEcart));
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
        wkProximityx10[cursor]  = savedProximityx10[cursor];
        wkEcart[cursor]         = savedEcart[cursor];
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
        cursorSubField = SF_TYPE;  // always start on TYPE (D5)
        screenDirty = true;
      }
    } else {
      // Edit mode field-focus (Phase 7 Task 16, plan §0 D5).
      // <-/->: cycle cursorSubField. ^/v: adjust value of focused field.
      if (ev.type == NAV_RIGHT) {
        cursorSubField = nextSubField(cursorSubField, wkTypes[cursor]);
        screenDirty = true;
      } else if (ev.type == NAV_LEFT) {
        cursorSubField = prevSubField(cursorSubField, wkTypes[cursor]);
        screenDirty = true;
      } else if (ev.type == NAV_UP || ev.type == NAV_DOWN) {
        bool up = (ev.type == NAV_UP);
        switch (cursorSubField) {
          case SF_TYPE: {
            bool ok = up ? cycleTypeForward(cursor) : cycleTypeBackward(cursor);
            if (!ok) {
              errorShown = true;
              errorTime = millis();
            } else {
              errorShown = false;
              // If type leaves ARPEG_GEN while focused on BONUS/MARGIN/PROX/ECART, jump back to TYPE.
              if (wkTypes[cursor] != BANK_ARPEG_GEN
                  && (cursorSubField == SF_BONUS || cursorSubField == SF_MARGIN
                      || cursorSubField == SF_PROX  || cursorSubField == SF_ECART)) {
                cursorSubField = SF_TYPE;
              }
            }
            break;
          }
          case SF_GROUP: {
            // Cycle group : -, A, B, C, D (5 entries via NUM_SCALE_GROUPS+1).
            uint8_t mod = NUM_SCALE_GROUPS + 1;
            wkGroups[cursor] = up
              ? (uint8_t)((wkGroups[cursor] + 1) % mod)
              : (uint8_t)((wkGroups[cursor] + mod - 1) % mod);
            break;
          }
          case SF_BONUS: {
            // bonus_pile x10 : range 10..20 (= 1.0..2.0). +1=0.1, accelerated +5 (large jump).
            uint8_t step = ev.accelerated ? 5 : 1;
            int16_t v = (int16_t)wkBonusPilex10[cursor] + (up ? step : -step);
            if (v < 10) v = 10;
            if (v > 20) v = 20;
            wkBonusPilex10[cursor] = (uint8_t)v;
            break;
          }
          case SF_MARGIN: {
            // margin_walk : range 3..12. +1, accelerated +3.
            uint8_t step = ev.accelerated ? 3 : 1;
            int16_t v = (int16_t)wkMarginWalk[cursor] + (up ? step : -step);
            if (v < 3) v = 3;
            if (v > 12) v = 12;
            wkMarginWalk[cursor] = (uint8_t)v;
            break;
          }
          case SF_PROX: {
            // V4 proximity_factor x10 : range 4..20 (= 0.4..2.0). +1=0.1, accelerated +5 (=0.5).
            uint8_t step = ev.accelerated ? 5 : 1;
            int16_t v = (int16_t)wkProximityx10[cursor] + (up ? step : -step);
            if (v < 4) v = 4;
            if (v > 20) v = 20;
            wkProximityx10[cursor] = (uint8_t)v;
            break;
          }
          case SF_ECART: {
            // V4 ecart : range 1..12 (Tool 5 override de TABLE). +1, accelerated +3.
            uint8_t step = ev.accelerated ? 3 : 1;
            int16_t v = (int16_t)wkEcart[cursor] + (up ? step : -step);
            if (v < 1) v = 1;
            if (v > 12) v = 12;
            wkEcart[cursor] = (uint8_t)v;
            break;
          }
        }
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        if (saveConfig(wkTypes, wkQuantize, wkGroups, wkBonusPilex10, wkMarginWalk,
                       wkProximityx10, wkEcart)) {
          memcpy(savedTypes,         wkTypes,         sizeof(savedTypes));
          memcpy(savedQuantize,      wkQuantize,      sizeof(savedQuantize));
          memcpy(savedGroups,        wkGroups,        sizeof(savedGroups));
          memcpy(savedBonusPilex10,  wkBonusPilex10,  sizeof(savedBonusPilex10));
          memcpy(savedMarginWalk,    wkMarginWalk,    sizeof(savedMarginWalk));
          memcpy(savedProximityx10,  wkProximityx10,  sizeof(savedProximityx10));
          memcpy(savedEcart,         wkEcart,         sizeof(savedEcart));
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
        bool isGen = (bt == BANK_ARPEG_GEN);

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

        // --- Type + quantize ---
        // Edit + cursorSubField=SF_TYPE -> [brackets] cyan-bold (highlighted).
        // Edit autre champ -> sans brackets, normal cyan (selected).
        // Hors edit : selected = cyan ; autres = normal.
        bool typeFocus = isEditing && (cursorSubField == SF_TYPE);

        if (typeFocus) {
          if (isGen) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            VT_CYAN VT_BOLD "[ARPEG_GEN ─ %s]" VT_RESET, QUANTIZE_NAMES[wkQuantize[i]]);
            visibleLen += 14 + (int)strlen(QUANTIZE_NAMES[wkQuantize[i]]);
          } else if (bt == BANK_ARPEG) {
            pos += snprintf(line + pos, sizeof(line) - pos,
                            VT_CYAN VT_BOLD "[ARPEG ─ %s]" VT_RESET, QUANTIZE_NAMES[wkQuantize[i]]);
            visibleLen += 10 + (int)strlen(QUANTIZE_NAMES[wkQuantize[i]]);
          } else {
            pos += snprintf(line + pos, sizeof(line) - pos, VT_CYAN VT_BOLD "[NORMAL]" VT_RESET);
            visibleLen += 8;
          }
        } else {
          // Non-focused (edit autre champ, ou hors edit). Selected highlight via cyan dim.
          const char* tColor = (selected ? VT_CYAN : "");
          const char* tReset = (selected ? VT_RESET : "");
          if (isGen) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%sARPEG_GEN%s", tColor, tReset);
            visibleLen += 9;
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "   Quantize: %s", QUANTIZE_NAMES[wkQuantize[i]]);
            visibleLen += 13 + (int)strlen(QUANTIZE_NAMES[wkQuantize[i]]);
          } else if (bt == BANK_ARPEG) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%sARPEG%s   ", tColor, tReset);
            visibleLen += 8;
            pos += snprintf(line + pos, sizeof(line) - pos,
                            "    Quantize: %s", QUANTIZE_NAMES[wkQuantize[i]]);
            visibleLen += 14 + (int)strlen(QUANTIZE_NAMES[wkQuantize[i]]);
          } else {
            pos += snprintf(line + pos, sizeof(line) - pos, "%sNORMAL%s  ", tColor, tReset);
            visibleLen += 8;
          }
        }

        // Padding jusqu'a colonne Group (position visible 50)
        const int GROUP_COL = 50;
        while (visibleLen < GROUP_COL && pos < (int)sizeof(line) - 32) {
          line[pos++] = ' ';
          visibleLen++;
        }

        // --- Colonne Group ---
        bool groupFocus = isEditing && (cursorSubField == SF_GROUP);
        if (groupFocus) {
          pos += snprintf(line + pos, sizeof(line) - pos,
                          "Group: " VT_CYAN VT_BOLD "[%c]" VT_RESET,
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

        // --- Ligne 2 ARPEG_GEN : Bonus pile + Margin (indented col 14) ---
        // Spec §22 : 2-lignes layout. Indent 14 = sous le label "ARPEG_GEN" de la ligne 1.
        if (isGen) {
          char line2[160];
          int p2 = 0;
          int v2 = 0;
          for (int k = 0; k < 14; k++) { line2[p2++] = ' '; v2++; }

          float bonusF      = (float)wkBonusPilex10[i] / 10.0f;
          bool  bonusFocus  = isEditing && (cursorSubField == SF_BONUS);
          bool  marginFocus = isEditing && (cursorSubField == SF_MARGIN);

          if (bonusFocus) {
            p2 += snprintf(line2 + p2, sizeof(line2) - p2,
                           "Bonus pile: " VT_CYAN VT_BOLD "[%.1f]" VT_RESET, bonusF);
            v2 += 12 + 5;  // "Bonus pile: " (12) + "[1.5]" (5)
          } else if (selected) {
            p2 += snprintf(line2 + p2, sizeof(line2) - p2,
                           "Bonus pile: " VT_CYAN "%.1f" VT_RESET, bonusF);
            v2 += 12 + 3;
          } else {
            p2 += snprintf(line2 + p2, sizeof(line2) - p2,
                           VT_DIM "Bonus pile: %.1f" VT_RESET, bonusF);
            v2 += 12 + 3;
          }

          // Padding entre Bonus et Margin (visible col 40)
          const int MARGIN_COL = 40;
          while (v2 < MARGIN_COL && p2 < (int)sizeof(line2) - 32) {
            line2[p2++] = ' ';
            v2++;
          }

          if (marginFocus) {
            p2 += snprintf(line2 + p2, sizeof(line2) - p2,
                           "Margin: " VT_CYAN VT_BOLD "[%d]" VT_RESET, wkMarginWalk[i]);
          } else if (selected) {
            p2 += snprintf(line2 + p2, sizeof(line2) - p2,
                           "Margin: " VT_CYAN "%d" VT_RESET, wkMarginWalk[i]);
          } else {
            p2 += snprintf(line2 + p2, sizeof(line2) - p2,
                           VT_DIM "Margin: %d" VT_RESET, wkMarginWalk[i]);
          }

          _ui->drawFrameLine("%s", line2);

          // --- Ligne 3 ARPEG_GEN (V4 Task 22) : Prox + Ecart, alignement identique a ligne 2 ---
          char line3[160];
          int p3 = 0;
          int v3 = 0;
          for (int k = 0; k < 14; k++) { line3[p3++] = ' '; v3++; }

          float proxF      = (float)wkProximityx10[i] / 10.0f;
          bool  proxFocus  = isEditing && (cursorSubField == SF_PROX);
          bool  ecartFocus = isEditing && (cursorSubField == SF_ECART);

          // Prox (label 12 chars : "Prox:       ")
          if (proxFocus) {
            p3 += snprintf(line3 + p3, sizeof(line3) - p3,
                           "Prox:       " VT_CYAN VT_BOLD "[%.1f]" VT_RESET, proxF);
            v3 += 12 + 5;
          } else if (selected) {
            p3 += snprintf(line3 + p3, sizeof(line3) - p3,
                           "Prox:       " VT_CYAN "%.1f" VT_RESET, proxF);
            v3 += 12 + 3;
          } else {
            p3 += snprintf(line3 + p3, sizeof(line3) - p3,
                           VT_DIM "Prox:       %.1f" VT_RESET, proxF);
            v3 += 12 + 3;
          }

          // Padding entre Prox et Ecart (visible col 40, identique ligne 2)
          while (v3 < 40 && p3 < (int)sizeof(line3) - 32) {
            line3[p3++] = ' ';
            v3++;
          }

          // Ecart
          if (ecartFocus) {
            p3 += snprintf(line3 + p3, sizeof(line3) - p3,
                           "Ecart:  " VT_CYAN VT_BOLD "[%d]" VT_RESET, wkEcart[i]);
          } else if (selected) {
            p3 += snprintf(line3 + p3, sizeof(line3) - p3,
                           "Ecart:  " VT_CYAN "%d" VT_RESET, wkEcart[i]);
          } else {
            p3 += snprintf(line3 + p3, sizeof(line3) - p3,
                           VT_DIM "Ecart:  %d" VT_RESET, wkEcart[i]);
          }

          _ui->drawFrameLine("%s", line3);
        }
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

        // Sub-field description (in edit mode) — shown below bank list to clarify focused field.
        // Spec §25 : INFO panel descriptions par sous-champ.
        if (editing) {
          _ui->drawFrameEmpty();
          switch (cursorSubField) {
            case SF_TYPE: {
              // Quantize description (only when ARPEG/ARPEG_GEN — quantize doesn't apply to NORMAL)
              if (isArpType(wkTypes[cursor])) {
                switch (wkQuantize[cursor]) {
                  case 0:
                    _ui->drawFrameLine(VT_DIM "Immediate: arp starts on next clock division boundary. Lowest latency." VT_RESET);
                    break;
                  case 1:
                    _ui->drawFrameLine(VT_DIM "Beat: arp starts synced to next quarter note (24 ticks). Musical alignment." VT_RESET);
                    break;
                }
              } else {
                _ui->drawFrameLine(VT_DIM "Type: NORMAL/ARPEG/ARPEG_GEN. ^v cycles 5 states (ARPEG/ARPEG_GEN x Imm/Beat)." VT_RESET);
              }
              break;
            }
            case SF_GROUP:
              _ui->drawFrameLine(VT_DIM "Scale group: -, A, B, C, D. Banks in same group share scale changes." VT_RESET);
              break;
            case SF_BONUS:
              _ui->drawFrameLine(VT_DIM "Bonus pile (1.0-2.0): walk weight on pile degrees during mutation." VT_RESET);
              _ui->drawFrameLine(VT_DIM "  Higher = mutations stay anchored to pile. Lower = wider scale exploration." VT_RESET);
              break;
            case SF_MARGIN:
              _ui->drawFrameLine(VT_DIM "Margin walk (3-12): how far the walk can drift above/below pile range." VT_RESET);
              _ui->drawFrameLine(VT_DIM "  Smaller = melody hugs pile. Larger = melody drifts to neighboring degrees." VT_RESET);
              break;
            case SF_PROX:
              _ui->drawFrameLine(VT_DIM "Proximity factor (0.4-2.0): exponential falloff steepness of the walk weights." VT_RESET);
              _ui->drawFrameLine(VT_DIM "  Smaller = step-wise melodic motion. Larger = more erratic, freer leaps." VT_RESET);
              break;
            case SF_ECART:
              _ui->drawFrameLine(VT_DIM "Ecart (1-12): max degree jump between consecutive steps. Overrides R2+hold ecart." VT_RESET);
              _ui->drawFrameLine(VT_DIM "  R2+hold pilote uniquement la longueur de sequence (8-96). Ecart = ce param." VT_RESET);
              break;
          }
        }
      }

      if (errorShown) {
        _ui->drawFrameEmpty();
        _ui->drawFrameLine(VT_BRIGHT_RED "Max 4 ARP banks!" VT_RESET);
      }

      _ui->drawFrameEmpty();

      // Control bar — uniformised per Phase 7 Task 17 (plan §0 D5 + §26).
      // Edit : <-/-> cycle field, ^v adjust value of focused field. Universal §4.4 convention.
      if (confirmDefaults) {
        _ui->drawControlBar(CBAR_CONFIRM_ANY);
      } else if (editing) {
        _ui->drawControlBar(VT_DIM "[</>] FIELD  [^v] VALUE" CBAR_SEP "[RET] SAVE" CBAR_SEP "[q] CANCEL" VT_RESET);
      } else {
        _ui->drawControlBar(VT_DIM "[^v] NAV" CBAR_SEP "[RET] EDIT  [d] DFLT" CBAR_SEP "[q] EXIT" VT_RESET);
      }

      _ui->vtFrameEnd();
    }

    delay(5);
  }
}
