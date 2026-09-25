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
  ROLE_PLAY_STOP = 5,    // PL/S ARPEG ET LOOP unifié §11.1 (vert §14.1)
  ROLE_CC        = 6,    // Phase 3.B — consumed by page CC cell display (3.C.2+)
  // Codes 7/8 réservés pour cell display §8.1 cross-page (case 7 ABSORBANT, case 8 CONTEXTUEL).
  ROLE_REC       = 9,    // Phase 3.F.1 — LOOP REC (rouge §11.1)
  ROLE_CLR       = 10,   // Phase 3.F.1 — LOOP CLEAR (bleu foncé §11.1)
  ROLE_SLOT      = 11,   // Phase 3.F.1 — LOOP Slot (jaune §11.1)
  ROLE_COLLISION = 0xFF
};

// Unified role identifier (pool item)
struct PadRole {
  uint8_t line;   // 0=none, 1=bank, 2=root, 3=mode, 4=octave, 5=play/stop
  uint8_t index;  // index within that line
};

// Phase 3.C.2 — agrégateur cross-store pour cell display §8.1 + info panel
// + future modale d'écrasement 3.G. Consulté par toutes les pages (BANK/ARPEG/
// LOOP/CC) pour décider de l'affichage et de la légalité des assignments.
struct PadNeighborInfo {
  int8_t          bankIdx;         // -1 si pas BANK, sinon 0..7
  bool            hasCc;
  ScaleRoleResult scaleRole;       // .kind == NONE si pas de rôle scale
  ArpRoleResult   arpRole;         // .kind == NONE si pas de rôle arp
  int8_t          loopSlotIdx;     // -1 si pas slot, sinon 0..15
  bool            isLoopRec;
  bool            isLoopPlayStop;
  bool            isLoopClear;
};

// Phase 3.G — modale d'écrasement §10 : un absorbant (BANK/CC) veut occuper
// un pad portant 1-4 rôles CONTEXTUELs (ARPEG mod / LOOP). L'user arbitre y/n.
enum OverwriteAction : uint8_t {
  OVERWRITE_ACTION_NONE = 0,
  OVERWRITE_ACTION_BANK_ASSIGN,   // page BANK : assign bank N sur pad
  OVERWRITE_ACTION_CC_CREATE,     // page CC : créer CC entry sur pad
};

struct PendingOverwrite {
  OverwriteAction action;
  uint8_t         pad;
  uint8_t         bankIdx;    // valide si action == BANK_ASSIGN
  uint8_t         ccPoolIdx;  // valide si action == CC_CREATE — mode choisi en
                              // MODE_PICK (0=MOM/1=LATCH/2=RET0/3=HOLD), préservé
                              // à travers la modale (sinon retombée silencieuse MOM)
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

  // Phase 3.G — modale d'écrasement state (§10)
  PendingOverwrite _pendingOverwrite;
  bool             _confirmOverwrite;  // true = modale active, focus unique

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
  static const uint8_t POOL_SLOT_COUNT     = 16;  // Phase 3.F.1 — LOOP Slots
  // Phase 3.F.1 — POOL_LINE_COUNT 6 → 10 : ajout lignes 6-9 page LOOP.
  // Layout : 0=clear (legacy), 1=Bank, 2-5=ARPEG (Root/Mode/Octave/PL/S),
  // 6=LOOP REC, 7=LOOP PS, 8=LOOP CLR, 9=LOOP Slots.
  static const uint8_t POOL_LINE_COUNT     = 10;

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
  // Phase 3.D — UI_CC_CONFIRM_REMOVE retiré (convention §7.4 uniforme : ENTER
  // sur CC propre = dégage direct, raccourci 'x' redondant supprimé).
  enum CcUiMode : uint8_t {
    UI_CC_GRID_NAV         = 0,
    UI_CC_MODE_PICK        = 1,
    UI_CC_VALUE_EDIT       = 2,
    UI_CC_CONFIRM_DEFAULTS = 3,
    UI_CC_GLOBAL_EDIT      = 4,
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

  // =================================================================
  // Phase 3.C.2 — cross-store helpers (réutilisés par toutes les pages
  // 3.D/3.E/3.F/3.G pour cell display §8.1, info panel langue musicien,
  // et la modale d'écrasement future).
  // =================================================================
  PadNeighborInfo _padNeighborInfo(uint8_t pad) const;
  void _formatRoleNameMusician(const PadNeighborInfo& info,
                                char* out, size_t cap) const;

  // =================================================================
  // Phase 3.G — modale d'écrasement §10 (defined ToolPadRoles.cpp).
  // =================================================================
  void _handleOverwriteModaleApply();   // effet 'y' : clear contextuels + assign absorbant
  void _formatOverwriteWording(char* out, size_t cap);  // wording français §10.2-10.3
  void _drawOverwriteModale();          // overlay INFO section quand _confirmOverwrite

  // =================================================================
  // Phase 3.D — page BANK (8 bank slots assignment).
  // Spec §7.4 strict (dégage direct) + §9.2 strict (no silent steal) +
  // §6.5 hard-constraint exit (8 banks obligatoires) + §8.1 cell display
  // cross-page (CC absorbant ambre+, contextuels neutres).
  // =================================================================
  void _drawPageBank();
  void _drawGridBank();
  void _drawPoolBank();
  void _drawInfoBank();
  void _drawControlBarBank();
  void _handleEnterBank();          // ENTER from grid nav (§7.4 strict)
  void _handleEnterPoolBank();      // ENTER from pool nav (§9.2 strict)
  void _applyDefaultsBank();        // §15.4 + §15.5 skip silencieux
  void _clearRolesBankOnly(uint8_t pad);  // §15.3 page-scoped clear

  // =================================================================
  // Phase 3.E — page ARPEG (Root × 7, Mode × 7, Chromatic, Octave × 4,
  // PL/S ARPEG). §7.4 strict uniforme adopté (Loïc post HW Gate G3) :
  // ENTER sur rôle propre = dégage direct, pas de [---] clear pool.
  // §9.2 strict pool (no silent steal). Couleurs §11.1.
  // =================================================================
  void _drawPageArpeg();
  void _drawGridArpeg();
  void _drawPoolArpeg();
  void _drawInfoArpeg();
  void _drawControlBarArpeg();
  void _handleEnterArpeg();         // ENTER grid (§7.4 strict)
  void _handleEnterPoolArpeg();     // ENTER pool (§9.2 strict)
  void _applyDefaultsArpeg();       // §15.4 + §15.5 skip silencieux
  void _clearRolesArpegOnly(uint8_t pad);  // §15.3 page-scoped

  // =================================================================
  // Phase 3.F — page LOOP (REC × 1, PL/S × 1, CLR × 1, Slots × 16).
  // §7.4 strict uniforme + §9.2 strict pool + couleurs §11.1.
  // PL/S unifié ARPEG+LOOP §14.1 (geste musical commun).
  // Coexistence cross-AC ARPEG-LOOP §7.3 (Slot LOOP + Root ARPEG OK).
  // =================================================================
  void _drawPageLoop();
  void _drawGridLoop();
  void _drawPoolLoop();
  void _drawInfoLoop();
  void _drawControlBarLoop();
  void _handleEnterLoop();          // ENTER grid (§7.4 strict)
  void _handleEnterPoolLoop();      // ENTER pool (§9.2 strict)
  void _applyDefaultsLoop();        // §15.4 + §15.5 skip silencieux (REC=32/PS=33/CLR=34)
  void _clearRolesLoopOnly(uint8_t pad);   // §15.3 page-scoped
};

#endif // TOOL_PAD_ROLES_H
