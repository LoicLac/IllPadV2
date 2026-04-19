#include "ToolControlPads.h"
#include "SetupUI.h"
#include "SetupCommon.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include <Arduino.h>
#include <string.h>

ToolControlPads::ToolControlPads()
  : _keyboard(nullptr), _leds(nullptr), _ui(nullptr), _nvs(nullptr),
    _banks(nullptr),
    _uiMode(UI_GRID_NAV), _cursorPad(0), _fieldIdx(0),
    _poolIdx(0), _globalFieldIdx(0),
    _propEditDirty(false), _globalEditDirty(false),
    _screenDirty(true), _nvsSaved(false), _wkDirty(false), _flashExpireMs(0) {
  memset(&_wk, 0, sizeof(_wk));
  _wk.magic   = CONTROLPAD_MAGIC;
  _wk.version = CONTROLPAD_VERSION;
  _flashMsg[0] = '\0';
  memset(_refBaselines, 0, sizeof(_refBaselines));
}

void ToolControlPads::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                            SetupUI* ui, NvsManager* nvs, BankSlot* banks) {
  _keyboard = keyboard;
  _leds     = leds;
  _ui       = ui;
  _nvs      = nvs;
  _banks    = banks;
}

void ToolControlPads::run() {
  if (!_ui || !_leds || !_nvs || !_keyboard) return;

  _load();
  _refreshBadge();
  _uiMode         = UI_GRID_NAV;
  _cursorPad      = 0;
  _fieldIdx       = 0;
  _poolIdx        = 0;
  _globalFieldIdx = 0;
  _propEditDirty  = false;   // V3.A
  _globalEditDirty = false;  // V3.A
  _screenDirty    = true;
  _flashMsg[0]    = '\0';
  _flashExpireMs  = 0;
  _wkDirty        = false;

  // Capture baselines for pad-tap-select (pattern from ToolPadRoles)
  captureBaselines(*_keyboard, _refBaselines);

  // InputParser has no reset() — default-construct state is fine for re-entry.

  while (true) {
    _leds->update();
    _keyboard->pollAllSensorData();

    NavEvent ev = _input.update();
    UIMode modeAtStart = _uiMode;

    // Handle flash expiry
    if (_flashActive() && millis() > _flashExpireMs) {
      _flashMsg[0] = '\0';
      _flashExpireMs = 0;
      _screenDirty = true;
    }

    // Dispatch input
    switch (_uiMode) {
      case UI_GRID_NAV:         _handleGridNav(ev);         break;
      case UI_MODE_PICK:        _handleModePick(ev);        break;
      case UI_VALUE_EDIT:       _handleValueEdit(ev);       break;
      case UI_CONFIRM_REMOVE:   _handleConfirmRemove(ev);   break;
      case UI_CONFIRM_DEFAULTS: _handleConfirmDefaults(ev); break;
      case UI_GLOBAL_EDIT:      _handleGlobalEdit(ev);      break;
    }

    if (ev.type == NAV_QUIT && modeAtStart == UI_GRID_NAV) {
      break;
    }

    if (_screenDirty) {
      _draw();
      _screenDirty = false;
    }

    delay(5);
  }
}

// ------------------------------------------------------------
// Stubs for Task 5 — filled in Tasks 6-8
// ------------------------------------------------------------
// --- Pool helpers (V3.B stubs) ---
uint8_t ToolControlPads::_poolIdxFromEntry(const ControlPadEntry& e) const {
  // TODO: implement pool index lookup from entry
  return 1;  // Default to MOMENTARY
}

void ToolControlPads::_applyPoolIdxToEntry(uint8_t idx, ControlPadEntry& e) const {
  // TODO: implement pool index to entry application
}

void ToolControlPads::_handleModePick(const NavEvent& ev) {
  // TODO: V3.B pool picker implementation
}

void ToolControlPads::_drawPool() {
  // TODO: V3.B pool render implementation
}

void ToolControlPads::_handleGridNav(const NavEvent& ev) {
  // Hardware pad touch → jump cursor (pattern from ToolPadRoles.cpp)
  int detected = detectActiveKey(*_keyboard, _refBaselines);
  if (detected >= 0 && detected != (int)_cursorPad) {
    _cursorPad = (uint8_t)detected;
    _screenDirty = true;
    _ui->showPadFeedback((uint8_t)detected);
  }

  switch (ev.type) {
    case NAV_UP:
      if (_cursorPad >= 12) { _cursorPad -= 12; _screenDirty = true; }
      break;
    case NAV_DOWN:
      if (_cursorPad + 12 < NUM_KEYS) { _cursorPad += 12; _screenDirty = true; }
      break;
    case NAV_LEFT:
      if (_cursorPad % 12 > 0) { _cursorPad--; _screenDirty = true; }
      break;
    case NAV_RIGHT:
      if (_cursorPad % 12 < 11) { _cursorPad++; _screenDirty = true; }
      break;

    case NAV_ENTER: {
      // V3.B : ENTER opens the mode pool selector (does NOT create slot yet).
      // Pool cursor starts on the pad's current mode if assigned, else MOM (idx 1).
      int8_t s = _findSlot(_cursorPad);
      _poolIdx = (s >= 0) ? _poolIdxFromEntry(_wk.entries[s]) : 1;
      _propEditDirty = false;   // V3.A : fresh edit session
      _uiMode = UI_MODE_PICK;
      _screenDirty = true;
      break;
    }

    case NAV_CHAR:
      if (ev.ch == 'x') {
        int8_t s = _findSlot(_cursorPad);
        if (s >= 0) {
          _uiMode = UI_CONFIRM_REMOVE;
          _screenDirty = true;
        }
      } else if (ev.ch == 'd') {
        _uiMode = UI_CONFIRM_DEFAULTS;
        _screenDirty = true;
      } else if (ev.ch == 'e') {
        // V3.B : enter numeric value edit (only if pad is assigned)
        int8_t s = _findSlot(_cursorPad);
        if (s >= 0) {
          _uiMode = UI_VALUE_EDIT;
          _fieldIdx = 0;
          _screenDirty = true;
        } else {
          _setFlash("No slot. Press [RET] to create first.");
        }
      } else if (ev.ch == 'g') {
        _uiMode = UI_GLOBAL_EDIT;
        _globalFieldIdx = 0;
        _globalEditDirty = false;   // V3.A : fresh edit session
        _screenDirty = true;
      }
      break;

    case NAV_DEFAULTS:
      _uiMode = UI_CONFIRM_DEFAULTS;
      _screenDirty = true;
      break;

    default:
      break;
  }
}
void ToolControlPads::_handleValueEdit(const NavEvent& ev) {
  int8_t s = _findSlot(_cursorPad);
  if (s < 0) {
    // Slot vanished (defensive) — back to grid
    _uiMode = UI_GRID_NAV;
    _screenDirty = true;
    return;
  }

  switch (ev.type) {
    case NAV_UP:
      do {
        if (_fieldIdx == 0) _fieldIdx = 4; else _fieldIdx--;
      } while (_isFieldGreyed(_fieldIdx));
      _screenDirty = true;
      break;

    case NAV_DOWN:
      do {
        if (_fieldIdx == 4) _fieldIdx = 0; else _fieldIdx++;
      } while (_isFieldGreyed(_fieldIdx));
      _screenDirty = true;
      break;

    case NAV_LEFT:
      _adjustField(ev.accelerated ? -10 : -1);
      break;
    case NAV_RIGHT:
      _adjustField(ev.accelerated ? +10 : +1);
      break;

    case NAV_ENTER:
    case NAV_QUIT:
      if (_propEditDirty) {
        _save();
        _propEditDirty = false;
      }
      _uiMode = UI_GRID_NAV;
      _screenDirty = true;
      break;

    default:
      break;
  }
}
void ToolControlPads::_handleConfirmRemove(const NavEvent& ev) {
  ConfirmResult r = SetupUI::parseConfirm(ev);
  if (r == CONFIRM_YES) {
    _removeSlotForPad(_cursorPad);
    _uiMode = UI_GRID_NAV;
    _screenDirty = true;
  } else if (r == CONFIRM_NO) {
    _uiMode = UI_GRID_NAV;
    _screenDirty = true;
  }
  // PENDING → stay
}

void ToolControlPads::_handleConfirmDefaults(const NavEvent& ev) {
  ConfirmResult r = SetupUI::parseConfirm(ev);
  if (r == CONFIRM_YES) {
    _resetAll();
    _uiMode = UI_GRID_NAV;
    _cursorPad = 0;
    _screenDirty = true;
  } else if (r == CONFIRM_NO) {
    _uiMode = UI_GRID_NAV;
    _screenDirty = true;
  }
}

void ToolControlPads::_handleGlobalEdit(const NavEvent& ev) {
  switch (ev.type) {
    case NAV_UP:
      if (_globalFieldIdx == 0) _globalFieldIdx = 2; else _globalFieldIdx--;
      _screenDirty = true;
      break;
    case NAV_DOWN:
      if (_globalFieldIdx == 2) _globalFieldIdx = 0; else _globalFieldIdx++;
      _screenDirty = true;
      break;
    case NAV_LEFT:
      _adjustGlobalField(ev.accelerated ? -10 : -1);
      break;
    case NAV_RIGHT:
      _adjustGlobalField(ev.accelerated ? +10 : +1);
      break;
    case NAV_ENTER:
    case NAV_QUIT:
      if (_globalEditDirty) {
        _save();
        _globalEditDirty = false;
      }
      _uiMode = UI_GRID_NAV;
      _screenDirty = true;
      break;
    default:
      break;
  }
}

void ToolControlPads::_adjustGlobalField(int8_t delta) {
  switch (_globalFieldIdx) {
    case 0: {  // smoothMs 0..500
      int32_t v = (int32_t)_wk.smoothMs + delta;
      if (v < 0)   v = 0;
      if (v > 500) v = 500;
      _wk.smoothMs = (uint16_t)v;
      break;
    }
    case 1: {  // sampleHoldMs 0..31 (bounded by CTRL_RING_SIZE - 1)
      int32_t v = (int32_t)_wk.sampleHoldMs + delta;
      if (v < 0)  v = 0;
      if (v > 31) v = 31;
      _wk.sampleHoldMs = (uint16_t)v;
      break;
    }
    case 2: {  // releaseMs 0..2000
      int32_t v = (int32_t)_wk.releaseMs + delta;
      if (v < 0)    v = 0;
      if (v > 2000) v = 2000;
      _wk.releaseMs = (uint16_t)v;
      break;
    }
    default:
      return;
  }
  _globalEditDirty = true;
  _screenDirty = true;
}

uint8_t ToolControlPads::_currentBankFromBanks() const {
  if (!_banks) return 0;
  for (uint8_t b = 0; b < NUM_BANKS; b++) {
    if (_banks[b].isForeground) return b;
  }
  return 0;
}

int8_t ToolControlPads::_findSlot(uint8_t padIdx) const {
  for (uint8_t i = 0; i < _wk.count; i++) {
    if (_wk.entries[i].padIndex == padIdx) return (int8_t)i;
  }
  return -1;
}

bool ToolControlPads::_addSlot(uint8_t padIdx) {
  if (_wk.count >= MAX_CONTROL_PADS) return false;
  if (padIdx >= NUM_KEYS) return false;
  if (_findSlot(padIdx) >= 0) return true;  // already exists, idempotent

  ControlPadEntry& e = _wk.entries[_wk.count];
  e.padIndex    = padIdx;
  e.ccNumber    = 0;
  e.channel     = 0;   // follow bank
  e.mode        = CTRL_MODE_MOMENTARY;
  e.deadzone    = 0;
  e.releaseMode = CTRL_RELEASE_TO_ZERO;
  _wk.count++;
  _save();
  return true;
}

void ToolControlPads::_removeSlotForPad(uint8_t padIdx) {
  int8_t s = _findSlot(padIdx);
  if (s < 0) return;
  // Sparse compaction: shift entries after s down by one
  for (uint8_t i = (uint8_t)s; i + 1 < _wk.count; i++) {
    _wk.entries[i] = _wk.entries[i + 1];
  }
  _wk.count--;
  memset(&_wk.entries[_wk.count], 0, sizeof(ControlPadEntry));
  _save();
}

void ToolControlPads::_resetAll() {
  _wk.count = 0;
  memset(_wk.entries, 0, sizeof(_wk.entries));
  _save();
}

void ToolControlPads::_adjustField(int8_t delta) {
  int8_t sSigned = _findSlot(_cursorPad);
  if (sSigned < 0) return;
  uint8_t s = (uint8_t)sSigned;
  ControlPadEntry& e = _wk.entries[s];

  switch (_fieldIdx) {
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
        return;  // no save
      }
      e.channel = (uint8_t)v;
      break;
    }
    case 2: {  // Mode cycle 0..2
      int16_t v = (int16_t)e.mode + delta;
      while (v < 0) v += 3;
      while (v > 2) v -= 3;
      uint8_t newMode = (uint8_t)v;
      // Invariant: switching TO LATCH with channel=0 forces channel=1
      if (newMode == CTRL_MODE_LATCH && e.channel == 0) {
        e.channel = 1;
        _setFlash("CHANNEL forced to 1 (LATCH requires fixed channel)");
      }
      e.mode = newMode;
      break;
    }
    case 3: {  // Deadzone 0..126 (continuous only)
      if (e.mode != CTRL_MODE_CONTINUOUS) return;
      int16_t v = (int16_t)e.deadzone + delta;
      if (v < 0)   v = 0;
      if (v > 126) v = 126;
      e.deadzone = (uint8_t)v;
      break;
    }
    case 4: {  // Release cycle 0..1 (continuous only)
      if (e.mode != CTRL_MODE_CONTINUOUS) return;
      e.releaseMode = (e.releaseMode == CTRL_RELEASE_TO_ZERO)
                    ? CTRL_RELEASE_HOLD
                    : CTRL_RELEASE_TO_ZERO;
      break;
    }
    default:
      return;
  }

  _propEditDirty = true;
  _screenDirty = true;
}

bool ToolControlPads::_isFieldGreyed(uint8_t fieldIdx) const {
  int8_t s = _findSlot(_cursorPad);
  if (s < 0) return false;
  const ControlPadEntry& e = _wk.entries[s];
  // Deadzone (3) and Release (4) only for continuous
  if ((fieldIdx == 3 || fieldIdx == 4) && e.mode != CTRL_MODE_CONTINUOUS) {
    return true;
  }
  return false;
}

void ToolControlPads::_drawGrid() {
  char labels[NUM_KEYS][6];
  uint8_t map[NUM_KEYS];
  memset(map, 0, sizeof(map));

  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    int8_t s = _findSlot(i);
    if (s < 0) {
      strncpy(labels[i], "---", 5);
      labels[i][5] = '\0';
      map[i] = 0;
    } else {
      const ControlPadEntry& e = _wk.entries[s];
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
            // CTRL_RELEASE_TO_ZERO (default)
            suffix = 'z';
            mapVal = 3;
          }
          break;
      }
      snprintf(labels[i], sizeof(labels[i]), "%02u%c",
               (unsigned)(e.ccNumber % 100), suffix);
      map[i] = mapVal;
    }
  }

  _ui->drawCellGrid(GRID_CONTROLPAD,
                    0, nullptr, nullptr, nullptr,
                    (int)_cursorPad, 0, false,
                    nullptr, labels, map);
}
void ToolControlPads::_drawSelected() {
  int8_t sSigned = _findSlot(_cursorPad);

  // Unassigned pad : only show slot count
  if (sSigned < 0) {
    _ui->drawFrameLine(VT_DIM "Slots used   %u / %u" VT_RESET,
                       (unsigned)_wk.count, (unsigned)MAX_CONTROL_PADS);
    _ui->drawFrameEmpty();
    return;
  }

  const ControlPadEntry& e = _wk.entries[sSigned];

  // Build all 5 field rows
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
    uint8_t curBank = _currentBankFromBanks();
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
    bool selected = (_uiMode == UI_VALUE_EDIT) && (_fieldIdx == i);
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
                     (unsigned)_wk.count, (unsigned)MAX_CONTROL_PADS);
  _ui->drawFrameEmpty();
}

void ToolControlPads::_drawGlobals() {
  struct GRow {
    const char* label;
    uint16_t    value;
    const char* unit;
    const char* desc;
  };
  GRow rows[3] = {
    { "Smooth        ", _wk.smoothMs,     "ms", "EMA filter time constant (attack smoothing)" },
    { "Sample & hold ", _wk.sampleHoldMs, "ms", "look-back for HOLD_LAST plateau capture" },
    { "Release fade  ", _wk.releaseMs,    "ms", "RETURN_TO_ZERO linear fade-out duration" },
  };

  for (uint8_t i = 0; i < 3; i++) {
    bool selected = (_uiMode == UI_GLOBAL_EDIT) && (_globalFieldIdx == i);
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

void ToolControlPads::_drawInfo() {
  if (_uiMode == UI_CONFIRM_REMOVE) {
    _ui->drawFrameLine(VT_YELLOW "Remove control pad #%d? (y/n)" VT_RESET,
                       (int)_cursorPad + 1);
    _ui->drawFrameEmpty();
    return;
  }
  if (_uiMode == UI_CONFIRM_DEFAULTS) {
    _ui->drawFrameLine(VT_YELLOW "Reset ALL control pads to empty? (y/n)" VT_RESET);
    _ui->drawFrameEmpty();
    return;
  }

  if (_flashActive()) {
    _ui->drawFrameLine(VT_YELLOW "%s" VT_RESET, _flashMsg);
    _ui->drawFrameEmpty();
    return;
  }

  if (_uiMode == UI_GLOBAL_EDIT) {
    const char* desc;
    switch (_globalFieldIdx) {
      case 0: desc = "Smooth : longer = more filtering on CC output. 0 = bypass. Affects attack."; break;
      case 1: desc = "Sample & hold : at HOLD_LAST release, capture CC value from N ms ago."; break;
      case 2: desc = "Release fade : at RETURN_TO_ZERO release, linear fade to 0 over N ms."; break;
      default: desc = ""; break;
    }
    _ui->drawFrameLine(VT_DIM "%s" VT_RESET, desc);
    _ui->drawFrameEmpty();
    return;
  }

  int8_t s = _findSlot(_cursorPad);
  if (s < 0) {
    _ui->drawFrameLine(VT_DIM "Pad #%d : unassigned. [RET] to create. [g] edit globals." VT_RESET,
                       (int)_cursorPad + 1);
    _ui->drawFrameEmpty();
    return;
  }

  const ControlPadEntry& e = _wk.entries[s];
  const char* modeName = (e.mode == CTRL_MODE_MOMENTARY)  ? "momentary"
                        : (e.mode == CTRL_MODE_LATCH)     ? "latch"
                                                          : "continuous";
  char chBuf[12];
  if (e.channel == 0) snprintf(chBuf, sizeof(chBuf), "follow");
  else                snprintf(chBuf, sizeof(chBuf), "ch %u", (unsigned)e.channel);

  _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : CC %u, %s, %s",
                     (int)_cursorPad + 1, (unsigned)e.ccNumber, chBuf, modeName);

  // Line 2 : mode semantic
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
}
void ToolControlPads::_drawControlBar() {
  switch (_uiMode) {
    case UI_GRID_NAV:
      _ui->drawControlBar(
        VT_DIM "[^v<>] GRID  [RET] EDIT  [TAP] SELECT" CBAR_SEP
               "[x] REMOVE  [d] DFLT  [g] GLOBALS" CBAR_SEP
               "[q] EXIT" VT_RESET);
      break;
    case UI_VALUE_EDIT:
      _ui->drawControlBar(
        VT_DIM "[^v] FIELD  [</>] VALUE" CBAR_SEP
               "[RET] BACK  [q] CANCEL" VT_RESET);
      break;
    case UI_GLOBAL_EDIT:
      _ui->drawControlBar(
        VT_DIM "[^v] PARAM  [</>] VALUE" CBAR_SEP
               "[RET] BACK  [q] CANCEL" VT_RESET);
      break;
    case UI_CONFIRM_REMOVE:
    case UI_CONFIRM_DEFAULTS:
      _ui->drawControlBar(CBAR_CONFIRM_ANY);
      break;
  }
}

void ToolControlPads::_draw() {
  _ui->vtFrameStart();
  _ui->drawConsoleHeader("TOOL 4: CONTROL PADS", _nvsSaved);
  _ui->drawFrameEmpty();

  _ui->drawSection("PAD GRID");
  _drawGrid();
  _ui->drawFrameEmpty();

  _ui->drawSection("SELECTED");
  _drawSelected();

  _ui->drawSection("GLOBALS");
  _drawGlobals();
  _ui->drawFrameEmpty();

  _ui->drawSection("INFO");
  _drawInfo();

  _drawControlBar();
  _ui->vtFrameEnd();
}

void ToolControlPads::_load() {
  if (NvsManager::loadBlob(CONTROLPAD_NVS_NAMESPACE, CONTROLPAD_NVS_KEY,
                           CONTROLPAD_MAGIC, CONTROLPAD_VERSION,
                           &_wk, sizeof(_wk))) {
    validateControlPadStore(_wk);
  } else {
    memset(&_wk, 0, sizeof(_wk));
    _wk.magic        = CONTROLPAD_MAGIC;
    _wk.version      = CONTROLPAD_VERSION;
    _wk.count        = 0;
    _wk.smoothMs     = 10;
    _wk.sampleHoldMs = 15;
    _wk.releaseMs    = 50;
  }
}

void ToolControlPads::_save() {
  bool ok = NvsManager::saveBlob(CONTROLPAD_NVS_NAMESPACE, CONTROLPAD_NVS_KEY,
                                 &_wk, sizeof(_wk));
  if (ok) {
    _ui->flashSaved();
    _refreshBadge();
  }
}

void ToolControlPads::_refreshBadge() {
  _nvsSaved = NvsManager::checkBlob(CONTROLPAD_NVS_NAMESPACE, CONTROLPAD_NVS_KEY,
                                    CONTROLPAD_MAGIC, CONTROLPAD_VERSION,
                                    sizeof(_wk));
}

void ToolControlPads::_setFlash(const char* msg) {
  strncpy(_flashMsg, msg, sizeof(_flashMsg) - 1);
  _flashMsg[sizeof(_flashMsg) - 1] = '\0';
  _flashExpireMs = millis() + 1500;
  _screenDirty = true;
}

bool ToolControlPads::_flashActive() const {
  return _flashMsg[0] != '\0';
}
