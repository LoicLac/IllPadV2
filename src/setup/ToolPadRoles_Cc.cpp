#include "ToolPadRoles.h"
#include "SetupCommon.h"
#include "SetupUI.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../core/KeyboardData.h"
#include "../managers/NvsManager.h"
#include <Arduino.h>
#include <string.h>

// =================================================================
// Phase 3.C — page CC (MIDI CC ControlPads, absorbs Tool 4).
//
// 3.C.1a (this commit) : migration mécanique pure ToolControlPads::* →
// ToolPadRoles::*Cc. Comportement runtime strictement identique à Tool 4
// pour les méthodes migrées. Code DORMANT — non câblé sur run()
// orchestrateur (vient en 3.C.1b). Tool 4 actuel reste accessible.
//
// Renames mécaniques (vs Tool 4) :
//   _uiMode → _ccUiMode, UI_X enum → UI_CC_X (6 valeurs)
//   _fieldIdx → _ccFieldIdx, _poolIdx → _ccPoolIdx
//   _globalFieldIdx → _ccGlobalFieldIdx
//   _propEditDirty → _ccPropEditDirty, _globalEditDirty → _ccGlobalEditDirty
//   _wkDirty → _ccWkDirty, _screenDirty → _ccScreenDirty
//   _wk → _wkCc
//   _findSlot/_addSlot/_removeSlotForPad/_resetAll/_handle*/_draw*/etc.
//     → tous suffixés Cc
//   _cursorPad → (uint8_t)(_gridRow*12+_gridCol) [chirurgical, ~20 sites]
//
// Partagés (existing ToolPadRoles members, pas dupliqués) :
//   _keyboard, _leds, _ui, _nvs, _input, _refBaselines, _flashMsg,
//   _flashExpireMs, _setFlash, _flashActive, _nvsSaved
//
// Note _setFlash : ToolPadRoles::_setFlash (2500ms timeout) diffère de
// Tool 4::_setFlash (1500ms + _screenDirty=true en fin). Pour préserver
// le redraw forcé Tool 4, ajouter `_ccScreenDirty = true;` après chaque
// appel `_setFlash(...)` dans le code Cc migré.
// =================================================================

// =================================================================
// Phase 3.B stub — kept until Phase 3.C.2 livres cell display §8.1.
// =================================================================
void ToolPadRoles::_buildRoleMapCc() {
  _buildRoleMapLegacy();
}

// =================================================================
// Pool helpers (V3.B Tool 4)
// =================================================================
uint8_t ToolPadRoles::_poolIdxFromEntryCc(const ControlPadEntry& e) const {
  switch (e.mode) {
    case CTRL_MODE_MOMENTARY: return 0;
    case CTRL_MODE_LATCH:     return 1;
    case CTRL_MODE_CONTINUOUS:
    default:
      return (e.releaseMode == CTRL_RELEASE_HOLD) ? 3 : 2;
  }
}

void ToolPadRoles::_applyPoolIdxToEntryCc(uint8_t idx, ControlPadEntry& e) const {
  switch (idx) {
    case 0:
      e.mode = CTRL_MODE_MOMENTARY;
      break;
    case 1:
      // LATCH requires fixed channel — auto-promote follow-bank to ch 1.
      if (e.channel == 0) e.channel = 1;
      e.mode = CTRL_MODE_LATCH;
      break;
    case 2:
      e.mode        = CTRL_MODE_CONTINUOUS;
      e.releaseMode = CTRL_RELEASE_TO_ZERO;
      break;
    case 3:
      e.mode        = CTRL_MODE_CONTINUOUS;
      e.releaseMode = CTRL_RELEASE_HOLD;
      break;
    default:
      break;  // idx=4 (clear) handled by caller via slot removal
  }
}

// =================================================================
// Input handlers (UI_CC_* state machine)
// =================================================================

void ToolPadRoles::_handleModePickCc(const NavEvent& ev) {
  switch (ev.type) {
    case NAV_LEFT:
      if (_ccPoolIdx == 0) _ccPoolIdx = 4; else _ccPoolIdx--;
      _ccScreenDirty = true;
      break;
    case NAV_RIGHT:
      if (_ccPoolIdx == 4) _ccPoolIdx = 0; else _ccPoolIdx++;
      _ccScreenDirty = true;
      break;

    case NAV_ENTER: {
      uint8_t cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);
      int8_t s = _findSlotCc(cursorPad);
      if (_ccPoolIdx == 4) {
        // Clear : remove slot if it exists. No confirmation — matches Tool 3.
        if (s >= 0) _removeSlotForPadCc(cursorPad);
        _ccUiMode = UI_CC_GRID_NAV;
        _ccScreenDirty = true;
        break;
      }

      // Create slot if needed
      if (s < 0) {
        PadNeighborInfo info = _padNeighborInfo(cursorPad);

        // Phase 3.G.2 — BANK absorbant : refus permanent (§4 exclusif, jamais modale).
        if (info.bankIdx >= 0) {
          _ccUiMode = UI_CC_GRID_NAV;
          _ccScreenDirty = true;
          break;
        }

        // Phase 3.G.2 — contextuels (ARPEG/LOOP incl. LOOP control) → modale §10.
        // Le mode choisi en MODE_PICK est préservé via ccPoolIdx (appliqué post-'y').
        bool hasContextual = (info.scaleRole.kind != ScaleRoleKind::NONE)
                          || (info.arpRole.kind   != ArpRoleKind::NONE)
                          || (info.loopSlotIdx >= 0)
                          || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear;
        if (hasContextual) {
          _pendingOverwrite.action    = OVERWRITE_ACTION_CC_CREATE;
          _pendingOverwrite.pad       = cursorPad;
          _pendingOverwrite.ccPoolIdx = _ccPoolIdx;
          _confirmOverwrite = true;
          _ccUiMode = UI_CC_GRID_NAV;  // sortie MODE_PICK, modale prend le relais
          _ccScreenDirty = true;
          break;
        }

        if (!_addSlotCc(cursorPad)) {
          _setFlash("Cap reached (12/12). Remove a pad first.");
          _ccScreenDirty = true;
          _ccUiMode = UI_CC_GRID_NAV;
          break;
        }
        s = _findSlotCc(cursorPad);
      }

      if (s >= 0) {
        ControlPadEntry& e = _wkCc.entries[s];
        _applyPoolIdxToEntryCc(_ccPoolIdx, e);
        _saveCc();
      }
      _ccUiMode = UI_CC_GRID_NAV;
      _ccScreenDirty = true;
      break;
    }

    case NAV_QUIT:
      _ccUiMode = UI_CC_GRID_NAV;
      _ccScreenDirty = true;
      break;

    default:
      break;
  }
}

void ToolPadRoles::_handleGridNavCc(const NavEvent& ev) {
  // Hardware pad touch → jump cursor (pattern from ToolPadRoles legacy)
  uint8_t cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);
  int detected = detectActiveKey(*_keyboard, _refBaselines);
  if (detected >= 0 && detected != (int)cursorPad) {
    _gridRow = (uint8_t)(detected / 12);
    _gridCol = (uint8_t)(detected % 12);
    _ccScreenDirty = true;
    _ui->showPadFeedback((uint8_t)detected);
    cursorPad = (uint8_t)detected;
  }

  switch (ev.type) {
    case NAV_UP:
      if (_gridRow > 0) { _gridRow--; _ccScreenDirty = true; }
      break;
    case NAV_DOWN:
      if (_gridRow < 3) { _gridRow++; _ccScreenDirty = true; }
      break;
    case NAV_LEFT:
      if (_gridCol > 0) { _gridCol--; _ccScreenDirty = true; }
      break;
    case NAV_RIGHT:
      if (_gridCol < 11) { _gridCol++; _ccScreenDirty = true; }
      break;

    case NAV_ENTER: {
      cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);

      // Phase 3.D + 3.G.2 — convention §7.4 uniforme cross-page :
      //   - BANK absorbant → refus permanent no-op (§4 exclusif, jamais modale).
      //   - CC propre → dégage direct (§7.4 strict).
      //   - Pad libre OU contextuels (ARPEG/LOOP) → ouvre MODE_PICK ; la
      //     modale d'écrasement §10 apparaîtra à l'ENTER pool si contextuels.
      PadNeighborInfo info = _padNeighborInfo(cursorPad);
      if (info.bankIdx >= 0) {
        // No-op silencieux permanent : ABSORBANT × ABSORBANT interdit.
        break;
      }

      // §7.4 strict : ENTER sur CC propre → dégage direct (no MODE_PICK ouvert).
      if (info.hasCc) {
        _removeSlotForPadCc(cursorPad);
        _ccScreenDirty = true;
        break;
      }

      // Pad libre ou contextuel : ouvre MODE_PICK pool selector. Cursor MOM (idx 0).
      _ccPoolIdx = 0;
      _ccPropEditDirty = false;
      _ccUiMode = UI_CC_MODE_PICK;
      _ccScreenDirty = true;
      break;
    }

    case NAV_CHAR:
      // Phase 3.D — raccourci 'x' (CONFIRM_REMOVE) retiré : redondant avec
      // ENTER §7.4 dégage direct (convention uniforme cross-page).
      if (ev.ch == 'd') {
        _ccUiMode = UI_CC_CONFIRM_DEFAULTS;
        _ccScreenDirty = true;
      } else if (ev.ch == 'e') {
        cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);
        int8_t s = _findSlotCc(cursorPad);
        if (s >= 0) {
          _ccUiMode = UI_CC_VALUE_EDIT;
          _ccFieldIdx = 0;
          _ccScreenDirty = true;
        } else {
          _setFlash("No slot. Press [RET] to create first.");
          _ccScreenDirty = true;
        }
      } else if (ev.ch == 'g') {
        _ccUiMode = UI_CC_GLOBAL_EDIT;
        _ccGlobalFieldIdx = 0;
        _ccGlobalEditDirty = false;
        _ccScreenDirty = true;
      }
      break;

    case NAV_DEFAULTS:
      _ccUiMode = UI_CC_CONFIRM_DEFAULTS;
      _ccScreenDirty = true;
      break;

    default:
      break;
  }
}

void ToolPadRoles::_handleValueEditCc(const NavEvent& ev) {
  uint8_t cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);
  int8_t s = _findSlotCc(cursorPad);
  if (s < 0) {
    // Slot vanished (defensive) — back to grid
    _ccUiMode = UI_CC_GRID_NAV;
    _ccScreenDirty = true;
    return;
  }

  switch (ev.type) {
    case NAV_UP:
      do {
        if (_ccFieldIdx == 0) _ccFieldIdx = 4; else _ccFieldIdx--;
      } while (_isFieldGreyedCc(_ccFieldIdx));
      _ccScreenDirty = true;
      break;

    case NAV_DOWN:
      do {
        if (_ccFieldIdx == 4) _ccFieldIdx = 0; else _ccFieldIdx++;
      } while (_isFieldGreyedCc(_ccFieldIdx));
      _ccScreenDirty = true;
      break;

    case NAV_LEFT:
      _adjustFieldCc(ev.accelerated ? -10 : -1);
      break;
    case NAV_RIGHT:
      _adjustFieldCc(ev.accelerated ? +10 : +1);
      break;

    case NAV_ENTER:
    case NAV_QUIT:
      if (_ccPropEditDirty) {
        _saveCc();
        _ccPropEditDirty = false;
      }
      _ccUiMode = UI_CC_GRID_NAV;
      _ccScreenDirty = true;
      break;

    default:
      break;
  }
}

// Phase 3.D — _handleConfirmRemoveCc retiré (convention §7.4 uniforme : ENTER
// sur CC propre = dégage direct via _removeSlotForPadCc dans _handleGridNavCc).

void ToolPadRoles::_handleConfirmDefaultsCc(const NavEvent& ev) {
  ConfirmResult r = SetupUI::parseConfirm(ev);
  if (r == CONFIRM_YES) {
    _resetAllCc();
    _ccUiMode = UI_CC_GRID_NAV;
    _gridRow = 0;
    _gridCol = 0;
    _ccScreenDirty = true;
  } else if (r == CONFIRM_NO) {
    _ccUiMode = UI_CC_GRID_NAV;
    _ccScreenDirty = true;
  }
}

void ToolPadRoles::_handleGlobalEditCc(const NavEvent& ev) {
  switch (ev.type) {
    case NAV_UP:
      if (_ccGlobalFieldIdx == 0) _ccGlobalFieldIdx = 2; else _ccGlobalFieldIdx--;
      _ccScreenDirty = true;
      break;
    case NAV_DOWN:
      if (_ccGlobalFieldIdx == 2) _ccGlobalFieldIdx = 0; else _ccGlobalFieldIdx++;
      _ccScreenDirty = true;
      break;
    case NAV_LEFT:
      _adjustGlobalFieldCc(ev.accelerated ? -10 : -1);
      break;
    case NAV_RIGHT:
      _adjustGlobalFieldCc(ev.accelerated ? +10 : +1);
      break;
    case NAV_ENTER:
    case NAV_QUIT:
      if (_ccGlobalEditDirty) {
        _saveCc();
        _ccGlobalEditDirty = false;
      }
      _ccUiMode = UI_CC_GRID_NAV;
      _ccScreenDirty = true;
      break;
    default:
      break;
  }
}

void ToolPadRoles::_adjustGlobalFieldCc(int8_t delta) {
  switch (_ccGlobalFieldIdx) {
    case 0: {  // smoothMs 0..500
      int32_t v = (int32_t)_wkCc.smoothMs + delta;
      if (v < 0)   v = 0;
      if (v > 500) v = 500;
      _wkCc.smoothMs = (uint16_t)v;
      break;
    }
    case 1: {  // sampleHoldMs 0..31 (bounded by CTRL_RING_SIZE - 1)
      int32_t v = (int32_t)_wkCc.sampleHoldMs + delta;
      if (v < 0)  v = 0;
      if (v > 31) v = 31;
      _wkCc.sampleHoldMs = (uint16_t)v;
      break;
    }
    case 2: {  // releaseMs 0..2000
      int32_t v = (int32_t)_wkCc.releaseMs + delta;
      if (v < 0)    v = 0;
      if (v > 2000) v = 2000;
      _wkCc.releaseMs = (uint16_t)v;
      break;
    }
    default:
      return;
  }
  _ccGlobalEditDirty = true;
  _ccScreenDirty = true;
}

uint8_t ToolPadRoles::_currentBankFromBanksCc() const {
  if (!_banks) return 0;
  for (uint8_t b = 0; b < NUM_BANKS; b++) {
    if (_banks[b].isForeground) return b;
  }
  return 0;
}

// =================================================================
// Slot ops
// =================================================================

int8_t ToolPadRoles::_findSlotCc(uint8_t padIdx) const {
  for (uint8_t i = 0; i < _wkCc.count; i++) {
    if (_wkCc.entries[i].padIndex == padIdx) return (int8_t)i;
  }
  return -1;
}

bool ToolPadRoles::_addSlotCc(uint8_t padIdx) {
  if (_wkCc.count >= MAX_CONTROL_PADS) return false;
  if (padIdx >= NUM_KEYS) return false;
  if (_findSlotCc(padIdx) >= 0) return true;  // already exists, idempotent

  ControlPadEntry& e = _wkCc.entries[_wkCc.count];
  e.padIndex    = padIdx;
  e.ccNumber    = 0;
  e.channel     = 0;   // follow bank
  e.mode        = CTRL_MODE_MOMENTARY;
  e.deadzone    = 0;
  e.releaseMode = CTRL_RELEASE_TO_ZERO;
  _wkCc.count++;
  _saveCc();
  return true;
}

void ToolPadRoles::_removeSlotForPadCc(uint8_t padIdx) {
  int8_t s = _findSlotCc(padIdx);
  if (s < 0) return;
  // Sparse compaction: shift entries after s down by one
  for (uint8_t i = (uint8_t)s; i + 1 < _wkCc.count; i++) {
    _wkCc.entries[i] = _wkCc.entries[i + 1];
  }
  _wkCc.count--;
  memset(&_wkCc.entries[_wkCc.count], 0, sizeof(ControlPadEntry));
  _saveCc();
}

void ToolPadRoles::_resetAllCc() {
  _wkCc.count = 0;
  memset(_wkCc.entries, 0, sizeof(_wkCc.entries));
  _saveCc();
}

void ToolPadRoles::_adjustFieldCc(int8_t delta) {
  uint8_t cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);
  int8_t sSigned = _findSlotCc(cursorPad);
  if (sSigned < 0) return;
  uint8_t s = (uint8_t)sSigned;
  ControlPadEntry& e = _wkCc.entries[s];

  switch (_ccFieldIdx) {
    case 0: {  // CC number 0-127
      int16_t v = (int16_t)e.ccNumber + delta;
      if (v < 0)   v = 0;
      if (v > 127) v = 127;
      e.ccNumber = (uint8_t)v;
      break;
    }
    case 1: {  // Channel 0 (follow) .. 16
      int16_t v = (int16_t)e.channel + delta;
      if (v < 0)  v = 0;
      if (v > 16) v = 16;
      // Invariant: LATCH + follow (ch 0) forbidden — refuse
      if (e.mode == CTRL_MODE_LATCH && v == 0) {
        _setFlash("LATCH requires fixed channel - change mode first");
        _ccScreenDirty = true;
        return;  // no save
      }
      e.channel = (uint8_t)v;
      break;
    }
    case 2:   // Mode — pool-driven, ignored in VALUE_EDIT
    case 4:   // Release — pool-driven, ignored in VALUE_EDIT
      return;
    case 3: {  // Deadzone 0..126 (continuous only)
      if (e.mode != CTRL_MODE_CONTINUOUS) return;
      int16_t v = (int16_t)e.deadzone + delta;
      if (v < 0)   v = 0;
      if (v > 126) v = 126;
      e.deadzone = (uint8_t)v;
      break;
    }
    default:
      return;
  }

  _ccPropEditDirty = true;
  _ccScreenDirty = true;
}

bool ToolPadRoles::_isFieldGreyedCc(uint8_t fieldIdx) const {
  uint8_t cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);
  int8_t s = _findSlotCc(cursorPad);
  if (s < 0) return false;
  const ControlPadEntry& e = _wkCc.entries[s];
  // V3.B : Mode (2) and Release (4) are pool-driven — always greyed in VALUE_EDIT
  if (fieldIdx == 2 || fieldIdx == 4) return true;
  // Deadzone (3) is continuous-only
  if (fieldIdx == 3 && e.mode != CTRL_MODE_CONTINUOUS) return true;
  return false;
}

// =================================================================
// Rendering
// =================================================================

void ToolPadRoles::_drawGridCc() {
  uint8_t cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);
  char labels[NUM_KEYS][6];
  uint8_t map[NUM_KEYS];
  memset(map, 0, sizeof(map));

  // Phase 3.C.2 — cell display §8.1. Ordre des checks :
  //   1. CC propre (priorité absolue page CC)
  //   2. LOOP control REC/PS/CLEAR (3.B preserved : labels R/P/C, map=0)
  //   3. BANK absorbant cross-page → "Bk<n>", map=7 (ambre+ saturé)
  //   4. Contextuel ARPEG (Root/Mode/Chrom/Octave/PL/S) OU LOOP slot → " :: ", map=8
  //   5. Vide → "---", map=0
  // Refus dur silencieux placeholder pour cases 3+4 (modale arbitrage en 3.G.2).
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    PadNeighborInfo info = _padNeighborInfo(i);

    // 1. CC propre
    if (info.hasCc) {
      int8_t s = _findSlotCc(i);
      if (s >= 0) {
        const ControlPadEntry& e = _wkCc.entries[s];
        char suffix;
        uint8_t mapVal;
        switch (e.mode) {
          case CTRL_MODE_MOMENTARY:
            suffix = 'm';
            mapVal = 1;
            break;
          case CTRL_MODE_LATCH:
            suffix = 'l';
            mapVal = 2;
            break;
          case CTRL_MODE_CONTINUOUS:
          default:
            if (e.releaseMode == CTRL_RELEASE_HOLD) {
              suffix = 'h';
              mapVal = 4;
            } else {
              suffix = 'z';
              mapVal = 3;
            }
            break;
        }
        snprintf(labels[i], sizeof(labels[i]), "%02u%c",
                 (unsigned)(e.ccNumber % 100), suffix);
        map[i] = mapVal;
        continue;
      }
    }

    // 2. LOOP control REC/PS/CLEAR (preserved 3.B affichage)
    if (info.isLoopRec || info.isLoopPlayStop || info.isLoopClear) {
      const char* lbl = info.isLoopRec      ? " R "
                      : info.isLoopPlayStop ? " P "
                                            : " C ";
      strncpy(labels[i], lbl, 5);
      labels[i][5] = '\0';
      map[i] = 0;
      continue;
    }

    // 3. BANK absorbant cross-page
    if (info.bankIdx >= 0) {
      snprintf(labels[i], sizeof(labels[i]), "Bk%d", info.bankIdx + 1);
      map[i] = 7;
      continue;
    }

    // 4. Contextuel ARPEG ou LOOP slot
    bool hasContextuel = (info.scaleRole.kind != ScaleRoleKind::NONE)
                       || (info.arpRole.kind  != ArpRoleKind::NONE)
                       || (info.loopSlotIdx >= 0);
    if (hasContextuel) {
      // VT_NEUTRAL_BAR = " :: " placeholder ASCII (4 bytes) — fit buffer [6].
      // Refonte UTF-8 " ■■ " + extension buffer en 3.H.1 (cf §13 placeholders).
      strncpy(labels[i], VT_NEUTRAL_BAR, 5);
      labels[i][5] = '\0';
      map[i] = 8;
      continue;
    }

    // 5. Vide
    strncpy(labels[i], "---", 5);
    labels[i][5] = '\0';
    map[i] = 0;
  }

  _ui->drawCellGrid(GRID_CONTROLPAD,
                    0, nullptr, nullptr, nullptr,
                    (int)cursorPad, 0, false,
                    nullptr, labels, map);
}

void ToolPadRoles::_drawPoolCc() {
  bool inPick = (_ccUiMode == UI_CC_MODE_PICK);
  uint8_t cursor = _ccPoolIdx;

  struct PoolItem {
    const char* label;
    const char* color;
  };
  const PoolItem items[5] = {
    { "MOM ",  VT_BRIGHT_YELLOW },
    { "LATCH", VT_MAGENTA },
    { "RET0 ", VT_ORANGE },
    { "HOLD ", VT_BRIGHT_WHITE },
    { "--- ",  VT_DIM },
  };

  char buf[256];
  int pos = 0;
  pos += snprintf(buf + pos, sizeof(buf) - pos, VT_DIM "Mode :  " VT_RESET);

  for (uint8_t i = 0; i < 5; i++) {
    const bool isCursor = inPick && (i == cursor);
    if (isCursor) {
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                      VT_REVERSE " %s[%s]" VT_RESET "  ",
                      items[i].color, items[i].label);
    } else {
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                      " %s[%s]" VT_RESET "  ",
                      items[i].color, items[i].label);
    }
  }

  _ui->drawFrameLine("%s", buf);
}

void ToolPadRoles::_drawSelectedCc() {
  uint8_t cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);
  int8_t sSigned = _findSlotCc(cursorPad);

  // Unassigned pad : only show slot count
  if (sSigned < 0) {
    _ui->drawFrameLine(VT_DIM "Slots used   %u / %u" VT_RESET,
                       (unsigned)_wkCc.count, (unsigned)MAX_CONTROL_PADS);
    _ui->drawFrameEmpty();
    return;
  }

  const ControlPadEntry& e = _wkCc.entries[sSigned];

  struct FieldRow {
    const char* label;
    char        valBuf[24];
    const char* desc;
    bool        greyed;
  };
  FieldRow rows[5];

  rows[0].label = "CC number ";
  snprintf(rows[0].valBuf, sizeof(rows[0].valBuf), "%03u", (unsigned)e.ccNumber);
  rows[0].desc = "standard MIDI CC 0-127";
  rows[0].greyed = false;

  rows[1].label = "Channel   ";
  char chDescBuf[64];
  if (e.channel == 0) {
    snprintf(rows[1].valBuf, sizeof(rows[1].valBuf), "follow");
    uint8_t curBank = _currentBankFromBanksCc();
    uint8_t curCh   = _banks ? (uint8_t)(_banks[curBank].channel + 1)
                             : (uint8_t)(curBank + 1);  // 1-indexed for user
    snprintf(chDescBuf, sizeof(chDescBuf),
             "0=follow bank (now: ch %u, bank %c)",
             (unsigned)curCh, (char)('A' + curBank));
    rows[1].desc = chDescBuf;
  } else {
    snprintf(rows[1].valBuf, sizeof(rows[1].valBuf), "%u", (unsigned)e.channel);
    snprintf(chDescBuf, sizeof(chDescBuf), "1-16=fixed MIDI channel");
    rows[1].desc = chDescBuf;
  }
  rows[1].greyed = false;

  rows[2].label = "Mode      ";
  snprintf(rows[2].valBuf, sizeof(rows[2].valBuf), "%s",
           (e.mode == CTRL_MODE_MOMENTARY)  ? "momentary"
           : (e.mode == CTRL_MODE_LATCH)     ? "latch"
                                             : "continuous");
  rows[2].desc = "momentary / latch / continuous";
  rows[2].greyed = false;

  rows[3].label = "Deadzone  ";
  snprintf(rows[3].valBuf, sizeof(rows[3].valBuf), "%03u", (unsigned)e.deadzone);
  rows[3].desc = "continuous only - pressure threshold 0-126";
  rows[3].greyed = (e.mode != CTRL_MODE_CONTINUOUS);

  rows[4].label = "Release   ";
  snprintf(rows[4].valBuf, sizeof(rows[4].valBuf), "%s",
           (e.releaseMode == CTRL_RELEASE_TO_ZERO) ? "return-0" : "hold-last");
  rows[4].desc = "continuous only - return-to-zero / hold-last";
  rows[4].greyed = (e.mode != CTRL_MODE_CONTINUOUS);

  for (uint8_t i = 0; i < 5; i++) {
    bool selected = (_ccUiMode == UI_CC_VALUE_EDIT) && (_ccFieldIdx == i);
    const char* cursor = selected
        ? "  " VT_CYAN VT_BOLD "\xe2\x96\xb8 "
        : "    ";
    const char* valCol = rows[i].greyed ? VT_DIM
                       : selected        ? VT_CYAN
                                         : VT_BRIGHT_WHITE;
    const char* val = rows[i].greyed ? "---" : rows[i].valBuf;
    _ui->drawFrameLine("%s%s[%s%-10s%s]   " VT_DIM "%s" VT_RESET,
                       cursor, rows[i].label,
                       valCol, val, VT_RESET,
                       rows[i].desc);
  }

  _ui->drawFrameEmpty();
  _ui->drawFrameLine(VT_DIM "Slots used   %u / %u" VT_RESET,
                     (unsigned)_wkCc.count, (unsigned)MAX_CONTROL_PADS);
  _ui->drawFrameEmpty();
}

void ToolPadRoles::_drawGlobalsCc() {
  struct GRow {
    const char* label;
    uint16_t    value;
    const char* unit;
    const char* desc;
  };
  GRow rows[3] = {
    { "Smooth        ", _wkCc.smoothMs,     "ms", "EMA filter time constant (attack smoothing)" },
    { "Sample & hold ", _wkCc.sampleHoldMs, "ms", "look-back for HOLD_LAST plateau capture" },
    { "Release fade  ", _wkCc.releaseMs,    "ms", "RETURN_TO_ZERO linear fade-out duration" },
  };

  for (uint8_t i = 0; i < 3; i++) {
    bool selected = (_ccUiMode == UI_CC_GLOBAL_EDIT) && (_ccGlobalFieldIdx == i);
    const char* cursor = selected
        ? "  " VT_CYAN VT_BOLD "\xe2\x96\xb8 "
        : "    ";
    const char* valCol = selected ? VT_CYAN : VT_BRIGHT_WHITE;
    char valBuf[12];
    snprintf(valBuf, sizeof(valBuf), "%03u", (unsigned)rows[i].value);
    _ui->drawFrameLine("%s%s[%s%s%s] %s   " VT_DIM "%s" VT_RESET,
                       cursor, rows[i].label,
                       valCol, valBuf, VT_RESET,
                       rows[i].unit,
                       rows[i].desc);
  }
}

void ToolPadRoles::_drawInfoCc() {
  uint8_t cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);

  if (_ccUiMode == UI_CC_CONFIRM_DEFAULTS) {
    _ui->drawFrameLine(VT_YELLOW "Reset ALL control pads to empty? (y/n)" VT_RESET);
    _ui->drawFrameEmpty();
    return;
  }

  if (_flashActive()) {
    _ui->drawFrameLine(VT_YELLOW "%s" VT_RESET, _flashMsg);
    _ui->drawFrameEmpty();
    return;
  }

  if (_ccUiMode == UI_CC_MODE_PICK) {
    const char* desc;
    switch (_ccPoolIdx) {
      case 0: desc = "MOMENTARY : press=127, release=0 (binary gate)"; break;
      case 1: desc = "LATCH : each press toggles 0 <-> 127 (needs fixed channel)"; break;
      case 2: desc = "CONT+RET0 : pressure-driven, release fades to 0 (gate expression)"; break;
      case 3: desc = "CONT+HOLD : pressure-driven, release freezes last value (setter)"; break;
      case 4: desc = "clear : remove this pad's CC assignment"; break;
      default: desc = ""; break;
    }
    _ui->drawFrameLine(VT_DIM "%s" VT_RESET, desc);
    _ui->drawFrameEmpty();
    return;
  }

  if (_ccUiMode == UI_CC_GLOBAL_EDIT) {
    const char* desc;
    switch (_ccGlobalFieldIdx) {
      case 0: desc = "Smooth : longer = more filtering on CC output. 0 = bypass. Affects attack."; break;
      case 1: desc = "Sample & hold : at HOLD_LAST release, capture CC value from N ms ago."; break;
      case 2: desc = "Release fade : at RETURN_TO_ZERO release, linear fade to 0 over N ms."; break;
      default: desc = ""; break;
    }
    _ui->drawFrameLine(VT_DIM "%s" VT_RESET, desc);
    _ui->drawFrameEmpty();
    return;
  }

  // Phase 3.C.2 — section grid-nav unifiée : info panel langue musicien §15.2
  // + voisins cross-page (BANK absorbant ou contextuels ARPEG/LOOP).
  PadNeighborInfo info = _padNeighborInfo(cursorPad);

  // Cas 1 : pad porte BANK (absorbant cross-page) — interdit pour CC.
  if (info.bankIdx >= 0) {
    _ui->drawFrameLine(VT_YELLOW "Pad #%d : Bank %d (page BANK, absorbant) - cannot assign CC here." VT_RESET,
                       (int)cursorPad + 1, info.bankIdx + 1);
    _ui->drawFrameLine(VT_DIM "Move Bank %d in page BANK first to free this pad." VT_RESET,
                       info.bankIdx + 1);
    return;
  }

  // Cas 2 (Phase 3.G.2) : LOOP control = CONTEXTUEL — traité par le cas
  // générique contextuel ci-dessous (modale d'écrasement à l'assign §10).

  // Cas 3 : pad porte CC propre (assigned slot) — détails MIDI CC.
  if (info.hasCc) {
    int8_t s = _findSlotCc(cursorPad);
    if (s >= 0) {
      const ControlPadEntry& e = _wkCc.entries[s];
      const char* modeName = (e.mode == CTRL_MODE_MOMENTARY)  ? "momentary"
                            : (e.mode == CTRL_MODE_LATCH)     ? "latch"
                                                              : "continuous";
      char chBuf[12];
      if (e.channel == 0) snprintf(chBuf, sizeof(chBuf), "follow");
      else                snprintf(chBuf, sizeof(chBuf), "ch %u", (unsigned)e.channel);

      _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : CC %u, %s, %s",
                         (int)cursorPad + 1, (unsigned)e.ccNumber, chBuf, modeName);

      const char* modeSem;
      if (e.mode == CTRL_MODE_MOMENTARY) {
        modeSem = "MOMENTARY : press=127, release=0 (binary gate)";
      } else if (e.mode == CTRL_MODE_LATCH) {
        modeSem = "LATCH : each press toggles CC 0 <-> 127 (needs fixed channel)";
      } else if (e.releaseMode == CTRL_RELEASE_TO_ZERO) {
        modeSem = "CONT+RET0 : pressure-driven, release fades to 0 (gate expression)";
      } else {
        modeSem = "CONT+HOLD : pressure-driven, release freezes last value (setter)";
      }
      _ui->drawFrameLine(VT_DIM "%s" VT_RESET, modeSem);
      _ui->drawFrameLine(VT_DIM "[RET] to clear this CC assignment (§7.4 dégage direct)." VT_RESET);
      return;
    }
  }

  // Cas 4 : pad porte un/des rôles CONTEXTUEL (ARPEG mod / LOOP slot/control).
  // Langue musicien via _formatRoleNameMusician — la modale d'écrasement §10
  // confirmera à l'assign.
  bool hasContextuel = (info.scaleRole.kind != ScaleRoleKind::NONE)
                     || (info.arpRole.kind  != ArpRoleKind::NONE)
                     || (info.loopSlotIdx >= 0)
                     || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear;
  if (hasContextuel) {
    char roleName[80] = {0};
    _formatRoleNameMusician(info, roleName, sizeof(roleName));
    _ui->drawFrameLine(VT_YELLOW "Pad #%d : %s (contextuel)" VT_RESET,
                       (int)cursorPad + 1, roleName);
    _ui->drawFrameLine(VT_DIM "[RET] ouvre le pool — la modale d'ecrasement confirmera (§10)." VT_RESET);
    return;
  }

  // Cas 5 : pad libre — invite à créer.
  _ui->drawFrameLine(VT_DIM "Pad #%d : unassigned. [RET] to create. [g] edit globals." VT_RESET,
                     (int)cursorPad + 1);
  _ui->drawFrameEmpty();
}

void ToolPadRoles::_drawControlBarCc() {
  switch (_ccUiMode) {
    case UI_CC_GRID_NAV:
      // Phase 3.D — [x] RM retiré : ENTER sur CC propre = dégage direct (§7.4).
      _ui->drawControlBar(
        VT_DIM "[^v<>] GRID  [RET] CREATE/CLEAR  [e] VALUES  [TAP] SELECT" CBAR_SEP
               "[d] DFLT  [g] GLOBS" CBAR_SEP
               "[q] EXIT" VT_RESET);
      break;
    case UI_CC_MODE_PICK:
      _ui->drawControlBar(
        VT_DIM "[<>] CYCLE MODE" CBAR_SEP
               "[RET] APPLY  [q] CANCEL" VT_RESET);
      break;
    case UI_CC_VALUE_EDIT:
      _ui->drawControlBar(
        VT_DIM "[^v] FIELD  [</>] VALUE" CBAR_SEP
               "[RET] BACK  [q] CANCEL" VT_RESET);
      break;
    case UI_CC_GLOBAL_EDIT:
      _ui->drawControlBar(
        VT_DIM "[^v] PARAM  [</>] VALUE" CBAR_SEP
               "[RET] BACK  [q] CANCEL" VT_RESET);
      break;
    case UI_CC_CONFIRM_DEFAULTS:
      _ui->drawControlBar(CBAR_CONFIRM_ANY);
      break;
  }
}

// _drawPageCc — called by orchestrateur (ToolPadRoles::drawScreen dispatch
// when _activeSubPage == SUB_CC, wiring en Phase 3.C.1b). PAS de
// vtFrameStart/vtFrameEnd ni drawConsoleHeader : déjà émis par drawScreen.
void ToolPadRoles::_drawPageCc() {
  _ui->drawSection("PAD GRID");
  _drawGridCc();
  _ui->drawFrameEmpty();

  _ui->drawSection("POOL");
  _drawPoolCc();
  _ui->drawFrameEmpty();

  _ui->drawSection("SELECTED");
  _drawSelectedCc();

  _ui->drawSection("GLOBALS");
  _drawGlobalsCc();
  _ui->drawFrameEmpty();

  _ui->drawSection("INFO");
  _drawInfoCc();

  _drawControlBarCc();
}

// =================================================================
// Persistence
// =================================================================

void ToolPadRoles::_loadCc() {
  // Phase 3.C — utilise NvsManager cached state (peuplé par loadAll au boot).
  // Économise une IO flash vs loadBlob direct (équivalent dans le résultat,
  // le cache étant validé en boot via validateControlPadStore).
  if (_nvs) {
    _wkCc = _nvs->getLoadedControlPadStore();
    validateControlPadStore(_wkCc);
  } else {
    memset(&_wkCc, 0, sizeof(_wkCc));
    _wkCc.magic        = CONTROLPAD_MAGIC;
    _wkCc.version      = CONTROLPAD_VERSION;
    _wkCc.count        = 0;
    _wkCc.smoothMs     = 10;
    _wkCc.sampleHoldMs = 15;
    _wkCc.releaseMs    = 50;
  }
}

void ToolPadRoles::_saveCc() {
  bool ok = NvsManager::saveBlob(CONTROLPAD_NVS_NAMESPACE, CONTROLPAD_NVS_KEY,
                                 &_wkCc, sizeof(_wkCc));
  if (ok) {
    _ui->flashSaved();
    _refreshBadgeCc();
  }
}

void ToolPadRoles::_refreshBadgeCc() {
  _nvsSaved = NvsManager::checkBlob(CONTROLPAD_NVS_NAMESPACE, CONTROLPAD_NVS_KEY,
                                    CONTROLPAD_MAGIC, CONTROLPAD_VERSION,
                                    sizeof(_wkCc));
}
