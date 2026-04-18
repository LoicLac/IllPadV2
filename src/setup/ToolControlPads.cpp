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
void ToolControlPads::_handlePropEdit(const NavEvent&)        { }
void ToolControlPads::_handleConfirmRemove(const NavEvent&)   { }
void ToolControlPads::_handleConfirmDefaults(const NavEvent&) { }

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

void ToolControlPads::_adjustField(int8_t) { }
bool ToolControlPads::_isFieldGreyed(uint8_t) const { return false; }

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
      char suffix = (e.mode == CTRL_MODE_MOMENTARY)  ? 'm'
                   : (e.mode == CTRL_MODE_LATCH)      ? 'l'
                                                      : 'c';
      snprintf(labels[i], sizeof(labels[i]), "%02u%c",
               (unsigned)(e.ccNumber % 100), suffix);
      map[i] = e.mode + 1;  // 1/2/3
    }
  }

  _ui->drawCellGrid(GRID_CONTROLPAD,
                    0, nullptr, nullptr, nullptr,
                    (int)_cursorPad, 0, false,
                    nullptr, labels, map);
}
void ToolControlPads::_drawSelected() {
  _ui->drawFrameLine(VT_DIM "Slots used   %u / %u" VT_RESET,
                     (unsigned)_wk.count, (unsigned)MAX_CONTROL_PADS);
  _ui->drawFrameEmpty();
}
void ToolControlPads::_drawInfo() {
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
