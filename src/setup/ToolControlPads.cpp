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
    _uiMode(UI_GRID_NAV), _cursorPad(0), _fieldIdx(0),
    _screenDirty(true), _nvsSaved(false), _flashExpireMs(0) {
  memset(&_wk, 0, sizeof(_wk));
  _wk.magic   = CONTROLPAD_MAGIC;
  _wk.version = CONTROLPAD_VERSION;
  _flashMsg[0] = '\0';
  memset(_refBaselines, 0, sizeof(_refBaselines));
}

void ToolControlPads::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                            SetupUI* ui, NvsManager* nvs) {
  _keyboard = keyboard;
  _leds     = leds;
  _ui       = ui;
  _nvs      = nvs;
}

void ToolControlPads::run() {
  if (!_ui || !_leds || !_nvs || !_keyboard) return;

  _load();
  _refreshBadge();
  _uiMode        = UI_GRID_NAV;
  _cursorPad     = 0;
  _fieldIdx      = 0;
  _screenDirty   = true;
  _flashMsg[0]   = '\0';
  _flashExpireMs = 0;

  _leds->startSetupComet();

  // Capture baselines for pad-tap-select (pattern from ToolPadRoles)
  captureBaselines(*_keyboard, _refBaselines);

  // InputParser has no reset() — default-construct state is fine for re-entry.

  while (true) {
    _leds->update();
    _keyboard->pollAllSensorData();

    NavEvent ev = _input.update();

    // Handle flash expiry
    if (_flashActive() && millis() > _flashExpireMs) {
      _flashMsg[0] = '\0';
      _flashExpireMs = 0;
      _screenDirty = true;
    }

    // Dispatch input
    switch (_uiMode) {
      case UI_GRID_NAV:         _handleGridNav(ev);         break;
      case UI_PROP_EDIT:        _handlePropEdit(ev);        break;
      case UI_CONFIRM_REMOVE:   _handleConfirmRemove(ev);   break;
      case UI_CONFIRM_DEFAULTS: _handleConfirmDefaults(ev); break;
    }

    if (ev.type == NAV_QUIT && _uiMode == UI_GRID_NAV) {
      break;
    }

    if (_screenDirty) {
      _draw();
      _screenDirty = false;
    }

    delay(5);
  }

  _leds->stopSetupComet();
}

// ------------------------------------------------------------
// Stubs for Task 5 — filled in Tasks 6-8
// ------------------------------------------------------------
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
      int8_t s = _findSlot(_cursorPad);
      if (s < 0) {
        if (!_addSlot(_cursorPad)) {
          _setFlash("Cap reached (12/12). Remove a pad first.");
          break;
        }
      }
      _uiMode = UI_PROP_EDIT;
      _fieldIdx = 0;
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
void ToolControlPads::_handlePropEdit(const NavEvent& ev) {
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

  _save();
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
  if (e.channel == 0) snprintf(rows[1].valBuf, sizeof(rows[1].valBuf), "follow");
  else                snprintf(rows[1].valBuf, sizeof(rows[1].valBuf), "%u", (unsigned)e.channel);
  rows[1].desc = "0=follow bank, 1-16=fixed MIDI channel";
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
    bool selected = (_uiMode == UI_PROP_EDIT) && (_fieldIdx == i);
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

  // Nixie vedette when editing a numeric field (pattern Tool 5)
  if (_uiMode == UI_PROP_EDIT) {
    if (_fieldIdx == 0) {
      _ui->drawSection("CC NUMBER");
      _ui->drawSegmentedValue("", (uint32_t)e.ccNumber, 3, "");
    } else if (_fieldIdx == 3 && e.mode == CTRL_MODE_CONTINUOUS) {
      _ui->drawSection("DEADZONE");
      _ui->drawSegmentedValue("", (uint32_t)e.deadzone, 3, "");
    }
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

  int8_t s = _findSlot(_cursorPad);
  if (s < 0) {
    _ui->drawFrameLine(VT_DIM "Pad #%d : unassigned. [RET] to create." VT_RESET,
                       (int)_cursorPad + 1);
  } else {
    const ControlPadEntry& e = _wk.entries[s];
    const char* modeName = (e.mode == CTRL_MODE_MOMENTARY)  ? "momentary"
                          : (e.mode == CTRL_MODE_LATCH)      ? "latch"
                                                             : "continuous";
    char chBuf[12];
    if (e.channel == 0) snprintf(chBuf, sizeof(chBuf), "follow");
    else                snprintf(chBuf, sizeof(chBuf), "ch %u", (unsigned)e.channel);
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET
                       " : CC %u, %s, %s",
                       (int)_cursorPad + 1, (unsigned)e.ccNumber, chBuf, modeName);
  }
  _ui->drawFrameEmpty();
}
void ToolControlPads::_drawControlBar() {
  switch (_uiMode) {
    case UI_GRID_NAV:
      _ui->drawControlBar(
        VT_DIM "[^v<>] GRID  [RET] EDIT  [TAP] SELECT" CBAR_SEP
               "[x] REMOVE  [d] DFLT" CBAR_SEP
               "[q] EXIT" VT_RESET);
      break;
    case UI_PROP_EDIT:
      _ui->drawControlBar(
        VT_DIM "[^v] FIELD  [</>] VALUE" CBAR_SEP
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

  _ui->drawSection("SELECTED");
  _drawSelected();

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
    _wk.magic   = CONTROLPAD_MAGIC;
    _wk.version = CONTROLPAD_VERSION;
    _wk.count   = 0;
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
