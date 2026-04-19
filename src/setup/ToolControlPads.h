#ifndef TOOL_CONTROL_PADS_H
#define TOOL_CONTROL_PADS_H

#include <stdint.h>
#include "../core/HardwareConfig.h"
#include "../core/KeyboardData.h"
#include "InputParser.h"

class CapacitiveKeyboard;
class LedController;
class SetupUI;
class NvsManager;
struct BankSlot;

class ToolControlPads {
public:
  ToolControlPads();

  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             SetupUI* ui, NvsManager* nvs, BankSlot* banks);
  void run();  // Blocking main loop

private:
  CapacitiveKeyboard* _keyboard;
  LedController*      _leds;
  SetupUI*            _ui;
  NvsManager*         _nvs;
  BankSlot*           _banks;   // for follow-bank channel resolution in SELECTED panel

  // UI state
  enum UIMode : uint8_t {
    UI_GRID_NAV,
    UI_PROP_EDIT,
    UI_CONFIRM_REMOVE,
    UI_CONFIRM_DEFAULTS,
    UI_GLOBAL_EDIT,   // V2 : editing smoothMs / sampleHoldMs / releaseMs
  };

  UIMode _uiMode;
  uint8_t _cursorPad;       // 0-47, selected cell in grid
  uint8_t _fieldIdx;        // 0-4 in PROP_EDIT (CC/Channel/Mode/Deadzone/Release)
  uint8_t _globalFieldIdx;  // 0=smoothMs, 1=sampleHoldMs, 2=releaseMs
  bool    _screenDirty;
  bool    _nvsSaved;

  // Working copy (committed via saveBlob on every edit)
  ControlPadStore _wk;

  // Flash message (e.g., "LATCH requires fixed channel")
  char    _flashMsg[80];
  uint32_t _flashExpireMs;  // 0 = no message

  // Baselines captured at tool start — used by detectActiveKey for pad-tap-select.
  // Pattern from ToolPadRoles / ToolCalibration (see SetupCommon.h helpers).
  uint16_t _refBaselines[NUM_KEYS];

  InputParser _input;

  // --- Navigation (filled by Tasks 6-8) ---
  void _handleGridNav(const NavEvent& ev);
  void _handlePropEdit(const NavEvent& ev);
  void _handleConfirmRemove(const NavEvent& ev);
  void _handleConfirmDefaults(const NavEvent& ev);
  void _handleGlobalEdit(const NavEvent& ev);

  // --- Global DSP field edit helpers ---
  void    _adjustGlobalField(int8_t delta);
  uint8_t _currentBankFromBanks() const;

  // --- Slot ops ---
  int8_t _findSlot(uint8_t padIdx) const;
  bool   _addSlot(uint8_t padIdx);
  void   _removeSlotForPad(uint8_t padIdx);
  void   _resetAll();

  // --- Field edit helpers ---
  void _adjustField(int8_t delta);
  bool _isFieldGreyed(uint8_t fieldIdx) const;

  // --- Render ---
  void _draw();
  void _drawGrid();
  void _drawSelected();
  void _drawGlobals();
  void _drawInfo();
  void _drawControlBar();

  // --- Persistence ---
  void _save();
  void _load();
  void _refreshBadge();

  // --- Flash helpers ---
  void _setFlash(const char* msg);
  bool _flashActive() const;
};

#endif // TOOL_CONTROL_PADS_H
