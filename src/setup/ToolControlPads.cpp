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
void ToolControlPads::_handleGridNav(const NavEvent&)         { }
void ToolControlPads::_handlePropEdit(const NavEvent&)        { }
void ToolControlPads::_handleConfirmRemove(const NavEvent&)   { }
void ToolControlPads::_handleConfirmDefaults(const NavEvent&) { }

int8_t ToolControlPads::_findSlot(uint8_t padIdx) const {
  for (uint8_t i = 0; i < _wk.count; i++) {
    if (_wk.entries[i].padIndex == padIdx) return (int8_t)i;
  }
  return -1;
}

bool ToolControlPads::_addSlot(uint8_t)       { return false; }
void ToolControlPads::_removeSlotForPad(uint8_t) { }
void ToolControlPads::_resetAll() { }

void ToolControlPads::_adjustField(int8_t) { }
bool ToolControlPads::_isFieldGreyed(uint8_t) const { return false; }

void ToolControlPads::_drawGrid()        { _ui->drawFrameEmpty(); }
void ToolControlPads::_drawSelected()    { _ui->drawFrameEmpty(); }
void ToolControlPads::_drawInfo()        { _ui->drawFrameEmpty(); }
void ToolControlPads::_drawControlBar()  {
  _ui->drawControlBar(VT_DIM "[q] EXIT" VT_RESET);
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
