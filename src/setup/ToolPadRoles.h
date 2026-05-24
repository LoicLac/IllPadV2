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
  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             SetupUI* ui, NvsManager* nvs,
             uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
             uint8_t& chromaticPad, uint8_t& arpPlayStopPad,
             uint8_t* octavePads);
  void run();  // Blocking — grid + pool driven

private:
  CapacitiveKeyboard* _keyboard;
  LedController*      _leds;
  SetupUI*            _ui;
  NvsManager*         _nvs;   // Phase 3 — for cross-store lookup (LoopPadStore + ControlPadStore)

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
};

#endif // TOOL_PAD_ROLES_H
