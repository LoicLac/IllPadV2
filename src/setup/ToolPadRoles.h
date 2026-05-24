#ifndef TOOL_PAD_ROLES_H
#define TOOL_PAD_ROLES_H

#include <stdint.h>
#include "../core/HardwareConfig.h"
#include "../core/KeyboardData.h"   // Phase 3 — LoopPadStore for _wkLoopPad
#include "InputParser.h"

class CapacitiveKeyboard;
class LedController;
class SetupUI;
class NvsManager;   // Phase 3 — for cross-store lookup (LoopPadStore + ControlPadStore)
struct BankSlot;    // Phase 3.C — page CC needs banks for follow-bank channel resolution

// Role codes for grid coloring
enum PadRoleCode : uint8_t {
  ROLE_NONE      = 0,
  ROLE_BANK      = 1,
  ROLE_ROOT      = 2,
  ROLE_MODE      = 3,
  ROLE_OCTAVE    = 4,
  ROLE_PLAY_STOP = 5,
  ROLE_CC        = 6,    // Phase 3.B — consumed by page CC cell display (3.C.2+)
  ROLE_COLLISION = 0xFF
};

// Unified role identifier (pool item)
struct PadRole {
  uint8_t line;   // 0=none, 1=bank, 2=root, 3=mode, 4=octave, 5=play/stop
  uint8_t index;  // index within that line
};

// Phase 3 — sous-page contexte (Tool PAD ROLE 4-page refactor)
enum SubPage : uint8_t {
  SUB_BANK  = 0,   // 8 bank pads assignment
  SUB_ARPEG = 1,   // Root × 7, Mode × 7, Chromatic, Octave × 4, PL/S ARPEG
  SUB_LOOP  = 2,   // REC, PL/S, CLR, Slots × 16
  SUB_CC    = 3,   // CC MIDI ControlPads (Tool 4 absorbed Phase 3.C)
  SUB_COUNT = 4
};

class ToolPadRoles {
public:
  ToolPadRoles();

  // Phase 3 — `nvs` parameter added (NvsManager*) for LoopPadStore + ControlPadStore lookup.
  // Phase 3.C — `banks` parameter added (BankSlot*) for page CC follow-bank channel resolution.
  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             SetupUI* ui, NvsManager* nvs, BankSlot* banks,
             uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
             uint8_t& chromaticPad, uint8_t& arpPlayStopPad,
             uint8_t* octavePads);
  void run();  // Blocking — grid + pool driven

private:
  CapacitiveKeyboard* _keyboard;
  LedController*      _leds;
  SetupUI*            _ui;
  NvsManager*         _nvs;   // Phase 3 — for cross-store lookup (LoopPadStore + ControlPadStore)
  BankSlot*           _banks; // Phase 3.C — page CC follow-bank channel resolution (SELECTED panel)

  // Pointers to live pad assignment arrays (owned by caller)
  uint8_t* _bankPads;      // [NUM_BANKS]
  uint8_t* _rootPads;      // [7]
  uint8_t* _modePads;      // [7]
  uint8_t* _chromaticPad;  // single
  uint8_t* _arpPlayStopPad;       // single
  uint8_t* _octavePads;    // [4]

  // Working copies (edited during tool, committed on save)
  uint8_t      _wkBankPads[NUM_BANKS];
  uint8_t      _wkRootPads[7];
  uint8_t      _wkModePads[7];
  uint8_t      _wkChromPad;
  uint8_t      _wkArpPlayStopPad;
  uint8_t      _wkOctavePads[4];
  LoopPadStore _wkLoopPad;            // Phase 3 — working copy for sub-page LOOP
  ControlPadStore _wkCc;              // Phase 3.C — working copy for sub-page CC (ex Tool 4 _wk)

  // Phase 3 — sub-page state + flash msg infrastructure
  SubPage      _activeSubPage;        // current sub-page (BANK/ARPEG/LOOP/CC)
  char         _flashMsg[80];         // flash msg buffer (pattern Tool 4)
  uint32_t     _flashExpireMs;        // flash msg expiry timestamp

  // Grid state (rebuilt before each draw)
  uint8_t _roleMap[NUM_KEYS];        // PadRoleCode per pad
  char    _roleLabels[NUM_KEYS][6];  // 5-char + null

  // Navigation state
  InputParser _input;
  uint8_t _gridRow;       // 0-3 (4 rows of 12)
  uint8_t _gridCol;       // 0-11
  bool    _editing;       // true = pool navigation mode
  uint8_t _poolLine;      // 0=clear, 1=bank, 2=root, 3=mode, 4=octave, 5=play/stop
  uint8_t _poolIdx;       // index within current pool line
  bool    _confirmDefaults;  // true = waiting for y/n defaults confirmation
  bool    _confirmClearAll;  // true = waiting for y/n clear-all confirmation
  bool    _nvsSaved;         // NVS status for header badge

  // Touch detection baselines
  uint16_t _refBaselines[NUM_KEYS];

  // Helpers
  void buildRoleMap();
  PadRole getRoleForPad(uint8_t pad) const;
  uint8_t findPadWithRole(uint8_t line, uint8_t index) const;
  void    assignRole(uint8_t pad, uint8_t line, uint8_t index);
  void    clearRole(uint8_t pad);
  void    clearAllRoles();
  void    resetToDefaults();
  bool    saveAll();

  // Pool line sizes
  static const uint8_t POOL_BANK_COUNT     = 8;
  static const uint8_t POOL_ROOT_COUNT     = 7;
  static const uint8_t POOL_MODE_COUNT     = 8;   // 7 modes + chromatic
  static const uint8_t POOL_OCTAVE_COUNT   = 4;
  static const uint8_t POOL_PLAY_STOP_COUNT     = 1;
  static const uint8_t POOL_LINE_COUNT     = 6;   // 0=clear, 1-5=categories

  uint8_t poolLineSize(uint8_t line) const;

  // Display
  void drawScreen();
  void drawGrid();
  void drawPool();
  void drawInfoPanel();
  void drawControlBar();

  // Description helpers
  const char* poolItemLabel(uint8_t line, uint8_t index) const;
  void printRoleDescription(uint8_t line, uint8_t index);
  void printPadDescription(uint8_t pad);

  // Phase 3 — helpers
  void _handleTab();                  // cycle sub-page BANK -> ARPEG -> LOOP -> CC -> BANK
  void _drawSubPageHeader();          // affiche "[BANK|ARPEG|LOOP|CC]" highlighted
  void _setFlash(const char* msg);    // pattern Tool 4 (ToolControlPads.cpp:854)
  bool _flashActive() const;
  void _drawFlash();                  // render flash line between info panel and control bar

  // Phase 3 — factorisation buildRoleMap (m14 v2 : cible buildRoleMap, pas drawGrid).
  // Phase 3.B livre 4 stubs delegating à _buildRoleMapLegacy. Chaque sous-phase
  // 3.C-3.F remplace son stub par la vraie impl ; _buildRoleMapLegacy retiré en 3.H.2.
  void _buildRoleMapLegacy();         // body original — fallback pour stubs Phase 3.B-3.F
  void _buildRoleMapBank();           // Phase 3.D — sous-page BANK (defined ToolPadRoles_Bank.cpp)
  void _buildRoleMapCc();             // Phase 3.C — sous-page CC   (defined ToolPadRoles_Cc.cpp)
  void _buildRoleMapArpeg();          // Phase 3.E — sous-page ARPEG (defined ToolPadRoles_Arpeg.cpp)
  void _buildRoleMapLoop();           // Phase 3.F — sous-page LOOP (defined ToolPadRoles_Loop.cpp)

  // =================================================================
  // Phase 3.C — page CC (MIDI CC ControlPads, absorbs Tool 4).
  // 3.C.1a : migration mécanique pure, code dormant (non câblé sur run()).
  // 3.C.1b : câblage orchestrateur (dispatch run() + drawScreen).
  // 3.C.2  : cell display §8.1 + helpers cross-store.
  // =================================================================

  // Sub-state machine (formerly ToolControlPads::UIMode)
  enum CcUiMode : uint8_t {
    UI_CC_GRID_NAV         = 0,
    UI_CC_MODE_PICK        = 1,
    UI_CC_VALUE_EDIT       = 2,
    UI_CC_CONFIRM_REMOVE   = 3,
    UI_CC_CONFIRM_DEFAULTS = 4,
    UI_CC_GLOBAL_EDIT      = 5,
  };

  // Page CC state members (ex Tool 4 — preserved semantics, prefixed _cc*)
  CcUiMode _ccUiMode;
  uint8_t  _ccFieldIdx;        // 0..4 in UI_CC_VALUE_EDIT (CC/Channel/Mode/Deadzone/Release)
  uint8_t  _ccPoolIdx;         // 0..4 in UI_CC_MODE_PICK (MOM/LATCH/RET0/HOLD/clear)
  uint8_t  _ccGlobalFieldIdx;  // 0=smoothMs, 1=sampleHoldMs, 2=releaseMs
  bool     _ccPropEditDirty;   // VALUE_EDIT dirty tracking (save on state exit)
  bool     _ccGlobalEditDirty; // GLOBAL_EDIT dirty tracking
  bool     _ccWkDirty;         // _wkCc modified since last _saveCc()
  bool     _ccScreenDirty;     // member dédié (ne pas confondre avec local screenDirty de run())

  // Page CC methods (defined in ToolPadRoles_Cc.cpp)
  void _handleGridNavCc(const NavEvent& ev);
  void _handleModePickCc(const NavEvent& ev);
  void _handleValueEditCc(const NavEvent& ev);
  void _handleConfirmRemoveCc(const NavEvent& ev);
  void _handleConfirmDefaultsCc(const NavEvent& ev);
  void _handleGlobalEditCc(const NavEvent& ev);
  void _drawPageCc();
  void _drawGridCc();
  void _drawPoolCc();
  void _drawSelectedCc();
  void _drawGlobalsCc();
  void _drawInfoCc();
  void _drawControlBarCc();
  uint8_t _poolIdxFromEntryCc(const ControlPadEntry& e) const;
  void    _applyPoolIdxToEntryCc(uint8_t idx, ControlPadEntry& e) const;
  void    _adjustGlobalFieldCc(int8_t delta);
  uint8_t _currentBankFromBanksCc() const;
  int8_t  _findSlotCc(uint8_t padIdx) const;
  bool    _addSlotCc(uint8_t padIdx);
  void    _removeSlotForPadCc(uint8_t padIdx);
  void    _resetAllCc();
  void    _adjustFieldCc(int8_t delta);
  bool    _isFieldGreyedCc(uint8_t fieldIdx) const;
  void    _saveCc();
  void    _loadCc();
  void    _refreshBadgeCc();
};

#endif // TOOL_PAD_ROLES_H
