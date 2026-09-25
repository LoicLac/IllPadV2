#include "ToolPadRoles.h"
#include "SetupCommon.h"
#include "SetupUI.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../core/KeyboardData.h"
#include "../managers/NvsManager.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

// =================================================================
// Short labels for pool items
// =================================================================

static const char* GRID_BANK_LABELS[] = {
  " Bk1", " Bk2", " Bk3", " Bk4", " Bk5", " Bk6", " Bk7", " Bk8"
};

// Grid labels now split: root, mode, octave, hold, play/stop
static const char* GRID_ROOT_LABELS[] = {
  " RtA", " RtB", " RtC", " RtD", " RtE", " RtF", " RtG"
};

static const char* GRID_MODE_LABELS[] = {
  "MdIo", "MdDo", "MdPh", "MdLy", "MdMx", "MdAe", "MdLo", " Chr"
};

static const char* GRID_OCTAVE_LABELS[] = {
  " Oc1", " Oc2", " Oc3", " Oc4"
};

static const char* GRID_PLAY_STOP_LABELS[] = { " Hld" };

// Pool display labels (no leading space)
static const char* POOL_BANK_LABELS[] = {
  "Bk1", "Bk2", "Bk3", "Bk4", "Bk5", "Bk6", "Bk7", "Bk8"
};

static const char* POOL_ROOT_LABELS[] = {
  "A", "B", "C", "D", "E", "F", "G"
};

static const char* POOL_MODE_LABELS[] = {
  "Ion", "Dor", "Phr", "Lyd", "Mix", "Aeo", "Loc", "Chr"
};

static const char* POOL_OCTAVE_LABELS[] = {
  "1", "2", "3", "4"
};

static const char* POOL_PLAY_STOP_LABELS[] = { "Hld" };

// =================================================================
// Constructor
// =================================================================

ToolPadRoles::ToolPadRoles()
  : _keyboard(nullptr), _leds(nullptr), _ui(nullptr),
    _nvs(nullptr),                     // Phase 3
    _banks(nullptr),                   // Phase 3.C — page CC follow-bank resolution
    _bankPads(nullptr), _rootPads(nullptr), _modePads(nullptr),
    _chromaticPad(nullptr), _arpPlayStopPad(nullptr),
    _octavePads(nullptr),
    _wkChromPad(0xFF), _wkArpPlayStopPad(0xFF),
    _activeSubPage(SUB_BANK),          // Phase 3 — default sub-page (4 pages : BANK/ARPEG/LOOP/CC)
    _flashExpireMs(0),                 // Phase 3 — no flash at construction
    _gridRow(0), _gridCol(0), _editing(false),
    _poolLine(0), _poolIdx(0),
    _confirmDefaults(false), _confirmClearAll(false), _nvsSaved(false),
    // Phase 3.G — modale d'écrasement state
    _pendingOverwrite{OVERWRITE_ACTION_NONE, 0xFF, 0, 0},
    _confirmOverwrite(false),
    // Phase 3.C — page CC state (ex Tool 4 members, prefixed _cc*)
    _ccUiMode(UI_CC_GRID_NAV),
    _ccFieldIdx(0), _ccPoolIdx(0), _ccGlobalFieldIdx(0),
    _ccPropEditDirty(false), _ccGlobalEditDirty(false),
    _ccWkDirty(false), _ccScreenDirty(false)
{
  memset(_wkBankPads, 0xFF, sizeof(_wkBankPads));
  memset(_wkRootPads, 0xFF, sizeof(_wkRootPads));
  memset(_wkModePads, 0xFF, sizeof(_wkModePads));
  memset(_wkOctavePads, 0xFF, sizeof(_wkOctavePads));
  memset(_refBaselines, 0, sizeof(_refBaselines));

  // Phase 3 — _wkLoopPad init to sentinel 0xFF (will be loaded from NvsManager in begin)
  memset(&_wkLoopPad, 0, sizeof(_wkLoopPad));
  _wkLoopPad.magic       = EEPROM_MAGIC;
  _wkLoopPad.version     = LOOPPAD_VERSION;
  _wkLoopPad.recPad      = 0xFF;
  _wkLoopPad.playStopPad = 0xFF;
  _wkLoopPad.clearPad    = 0xFF;
  for (uint8_t i = 0; i < 16; i++) _wkLoopPad.slotPads[i] = 0xFF;

  // Phase 3.C — _wkCc init to empty (will be loaded from NvsManager cache in begin)
  memset(&_wkCc, 0, sizeof(_wkCc));
  _wkCc.magic   = CONTROLPAD_MAGIC;
  _wkCc.version = CONTROLPAD_VERSION;

  _flashMsg[0] = '\0';
}

void ToolPadRoles::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                          SetupUI* ui, NvsManager* nvs, BankSlot* banks,
                          uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
                          uint8_t& chromaticPad, uint8_t& arpPlayStopPad,
                          uint8_t* octavePads) {
  _keyboard     = keyboard;
  _leds         = leds;
  _ui           = ui;
  _nvs          = nvs;                 // Phase 3
  _banks        = banks;               // Phase 3.C — page CC follow-bank channel resolution
  _bankPads     = bankPads;
  _rootPads     = rootPads;
  _modePads     = modePads;
  _chromaticPad = &chromaticPad;
  _arpPlayStopPad = &arpPlayStopPad;
  _octavePads   = octavePads;

  // Phase 3 — load working copies from NvsManager cached state (peuplé par loadAll au boot)
  if (_nvs) {
    _wkLoopPad = _nvs->getLoadedLoopPadStore();
    _wkCc      = _nvs->getLoadedControlPadStore();  // Phase 3.C — page CC
    validateControlPadStore(_wkCc);                 // defensive
  }
}

// =================================================================
// Phase 3 — Flash msg infrastructure (pattern Tool 4 — ToolControlPads.cpp:854)
// =================================================================

void ToolPadRoles::_setFlash(const char* msg) {
  strncpy(_flashMsg, msg, sizeof(_flashMsg) - 1);
  _flashMsg[sizeof(_flashMsg) - 1] = '\0';
  _flashExpireMs = millis() + 2500;   // 2.5s timeout (aligned Tool 4 pattern)
}

bool ToolPadRoles::_flashActive() const {
  return _flashExpireMs > millis() && _flashMsg[0] != '\0';
}

void ToolPadRoles::_drawFlash() {
  if (_flashActive()) {
    _ui->drawFrameLine(VT_YELLOW "%s" VT_RESET, _flashMsg);
  }
}

// =================================================================
// Pool helpers
// =================================================================

uint8_t ToolPadRoles::poolLineSize(uint8_t line) const {
  switch (line) {
    case 1: return POOL_BANK_COUNT;
    case 2: return POOL_ROOT_COUNT;
    case 3: return POOL_MODE_COUNT;
    case 4: return POOL_OCTAVE_COUNT;
    case 5: return POOL_PLAY_STOP_COUNT;
    // Phase 3.F.1 fix HW Gate G5 — lignes LOOP 6-9.
    // Sans cette extension, LEFT/RIGHT dans pool LOOP (ligne Slots
    // particulièrement) ne défilent pas car sz=0 bloque le if (sz > 0).
    case 6: return 1;                  // REC LOOP (1 entry)
    case 7: return 1;                  // PS LOOP (1 entry)
    case 8: return 1;                  // CLR LOOP (1 entry)
    case 9: return POOL_SLOT_COUNT;    // Slots LOOP (16 entries S0..S15)
    default: return 0;
  }
}

const char* ToolPadRoles::poolItemLabel(uint8_t line, uint8_t index) const {
  switch (line) {
    case 1: return (index < POOL_BANK_COUNT)     ? POOL_BANK_LABELS[index]     : "???";
    case 2: return (index < POOL_ROOT_COUNT)     ? POOL_ROOT_LABELS[index]     : "???";
    case 3: return (index < POOL_MODE_COUNT)     ? POOL_MODE_LABELS[index]     : "???";
    case 4: return (index < POOL_OCTAVE_COUNT)   ? POOL_OCTAVE_LABELS[index]   : "???";
    case 5: return (index < POOL_PLAY_STOP_COUNT)     ? POOL_PLAY_STOP_LABELS[index]     : "???";
    default: return "---";
  }
}

// =================================================================
// buildRoleMap — Phase 3 dispatcher (4 pages : BANK / ARPEG / LOOP / CC).
// Phase 3.B livre 4 stubs delegating à _buildRoleMapLegacy ; chaque sous-phase
// 3.C-3.F remplace son stub par la vraie impl. _buildRoleMapLegacy retiré en
// 3.H.2 quand toutes les pages sont incarnées.
// =================================================================

void ToolPadRoles::buildRoleMap() {
  switch (_activeSubPage) {
    case SUB_BANK:  _buildRoleMapBank();  break;
    case SUB_ARPEG: _buildRoleMapArpeg(); break;
    case SUB_LOOP:  _buildRoleMapLoop();  break;
    case SUB_CC:    _buildRoleMapCc();    break;
    default:        _buildRoleMapLegacy(); break;
  }
}

// _buildRoleMapLegacy — body original Tool 3 (avant Phase 3 refacto).
// Fallback pour les stubs des 4 pages jusqu'à leur incarnation respective.
void ToolPadRoles::_buildRoleMapLegacy() {
  memset(_roleMap, ROLE_NONE, NUM_KEYS);
  for (int i = 0; i < NUM_KEYS; i++) {
    memcpy(_roleLabels[i], " -- ", 5);
  }

  auto setRole = [&](uint8_t pad, uint8_t role, const char* label) {
    if (pad >= NUM_KEYS) return;
    if (_roleMap[pad] != ROLE_NONE) {
      _roleMap[pad] = ROLE_COLLISION;
      snprintf(_roleLabels[pad], 6, " !! ");
    } else {
      _roleMap[pad] = role;
      snprintf(_roleLabels[pad], 6, "%s", label);
    }
  };

  for (int i = 0; i < NUM_BANKS; i++)
    setRole(_wkBankPads[i], ROLE_BANK, GRID_BANK_LABELS[i]);
  for (int i = 0; i < 7; i++)
    setRole(_wkRootPads[i], ROLE_ROOT, GRID_ROOT_LABELS[i]);
  for (int i = 0; i < 7; i++)
    setRole(_wkModePads[i], ROLE_MODE, GRID_MODE_LABELS[i]);
  setRole(_wkChromPad, ROLE_MODE, GRID_MODE_LABELS[7]);  // Chr is last mode label
  setRole(_wkArpPlayStopPad, ROLE_PLAY_STOP, GRID_PLAY_STOP_LABELS[0]);
  for (int i = 0; i < 4; i++)
    setRole(_wkOctavePads[i], ROLE_OCTAVE, GRID_OCTAVE_LABELS[i]);
}

// Phase 3.B — stubs _buildRoleMapBank/Cc/Arpeg/Loop déplacés vers fichiers
// dédiés (ToolPadRoles_Bank.cpp, ToolPadRoles_Cc.cpp, ToolPadRoles_Arpeg.cpp,
// ToolPadRoles_Loop.cpp). Chaque sous-phase 3.C-3.F y incarne sa page propre
// sans toucher cet orchestrateur.

// =================================================================
// Phase 3 — sub-page navigation (Tasks 9 + 10)
// =================================================================

void ToolPadRoles::_handleTab() {
  _activeSubPage = (SubPage)((_activeSubPage + 1) % SUB_COUNT);
  _gridRow = 0; _gridCol = 0;
  _editing = false;
  _poolLine = 0; _poolIdx = 0;
  buildRoleMap();
}

// _drawSubPageHeader — affiche "Sub-page  [BANK|ARPEG|LOOP|CC]" avec sous-page
// active en VT_REVERSE+VT_BOLD, autres dim. Utilise drawFrameLine + escapes
// VT100 inline (M14 v2 audit indé : SetupUI n'a pas setInverse/moveCursor).
void ToolPadRoles::_drawSubPageHeader() {
  const char* labels[SUB_COUNT] = { "BANK", "ARPEG", "LOOP", "CC" };
  char buf[128];
  int pos = 0;
  pos += snprintf(buf + pos, sizeof(buf) - pos, "Sub-page  [");
  for (uint8_t i = 0; i < SUB_COUNT; i++) {
    if (i == _activeSubPage) {
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                       VT_REVERSE VT_BOLD "%s" VT_RESET, labels[i]);
    } else {
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                       VT_DIM "%s" VT_RESET, labels[i]);
    }
    if (i < SUB_COUNT - 1) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, "|");
    }
  }
  pos += snprintf(buf + pos, sizeof(buf) - pos, "]   " VT_DIM "[TAB] cycle" VT_RESET);
  _ui->drawFrameLine("%s", buf);
}

// =================================================================
// getRoleForPad / findPadWithRole / assignRole / clearRole
// =================================================================

PadRole ToolPadRoles::getRoleForPad(uint8_t pad) const {
  if (pad >= NUM_KEYS) return {0, 0};
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (_wkBankPads[i] == pad) return {1, i};
  }
  for (uint8_t i = 0; i < 7; i++) {
    if (_wkRootPads[i] == pad) return {2, i};
  }
  for (uint8_t i = 0; i < 7; i++) {
    if (_wkModePads[i] == pad) return {3, i};
  }
  if (_wkChromPad == pad) return {3, 7};
  for (uint8_t i = 0; i < 4; i++) {
    if (_wkOctavePads[i] == pad) return {4, i};
  }
  if (_wkArpPlayStopPad == pad) return {5, 0};
  return {0, 0};
}

uint8_t ToolPadRoles::findPadWithRole(uint8_t line, uint8_t index) const {
  switch (line) {
    case 1:
      if (index < NUM_BANKS) return _wkBankPads[index];
      break;
    case 2:
      if (index < 7) return _wkRootPads[index];
      break;
    case 3:
      if (index < 7) return _wkModePads[index];
      if (index == 7) return _wkChromPad;
      break;
    case 4:
      if (index < 4) return _wkOctavePads[index];
      break;
    case 5:
      if (index == 0) return _wkArpPlayStopPad;
      break;
  }
  return 0xFF;
}

void ToolPadRoles::assignRole(uint8_t pad, uint8_t line, uint8_t index) {
  switch (line) {
    case 1:
      if (index < NUM_BANKS) _wkBankPads[index] = pad;
      break;
    case 2:
      if (index < 7) _wkRootPads[index] = pad;
      break;
    case 3:
      if (index < 7) _wkModePads[index] = pad;
      else if (index == 7) _wkChromPad = pad;
      break;
    case 4:
      if (index < 4) _wkOctavePads[index] = pad;
      break;
    case 5:
      if (index == 0) _wkArpPlayStopPad = pad;
      break;
  }
}

void ToolPadRoles::clearRole(uint8_t pad) {
  if (pad >= NUM_KEYS) return;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (_wkBankPads[i] == pad) _wkBankPads[i] = 0xFF;
  }
  for (uint8_t i = 0; i < 7; i++) {
    if (_wkRootPads[i] == pad) _wkRootPads[i] = 0xFF;
  }
  for (uint8_t i = 0; i < 7; i++) {
    if (_wkModePads[i] == pad) _wkModePads[i] = 0xFF;
  }
  if (_wkChromPad == pad) _wkChromPad = 0xFF;
  if (_wkArpPlayStopPad == pad) _wkArpPlayStopPad = 0xFF;
  for (uint8_t i = 0; i < 4; i++) {
    if (_wkOctavePads[i] == pad) _wkOctavePads[i] = 0xFF;
  }
}

void ToolPadRoles::clearAllRoles() {
  memset(_wkBankPads, 0xFF, sizeof(_wkBankPads));
  memset(_wkRootPads, 0xFF, sizeof(_wkRootPads));
  memset(_wkModePads, 0xFF, sizeof(_wkModePads));
  _wkChromPad    = 0xFF;
  _wkArpPlayStopPad     = 0xFF;
  memset(_wkOctavePads, 0xFF, sizeof(_wkOctavePads));
}

void ToolPadRoles::resetToDefaults() {
  for (uint8_t i = 0; i < NUM_BANKS; i++) _wkBankPads[i] = i;
  for (uint8_t i = 0; i < 7; i++) _wkRootPads[i] = 8 + i;
  for (uint8_t i = 0; i < 7; i++) _wkModePads[i] = 15 + i;
  _wkChromPad    = 22;
  _wkArpPlayStopPad     = 23;
  _wkOctavePads[0] = 25;
  _wkOctavePads[1] = 26;
  _wkOctavePads[2] = 27;
  _wkOctavePads[3] = 28;
}

// =================================================================
// saveAll — direct NVS write (setup mode, NVS task not running)
// =================================================================

bool ToolPadRoles::saveAll() {
  bool allOk = true;

  // 1. BankPadStore
  BankPadStore bps;
  bps.magic = EEPROM_MAGIC;
  bps.version = BANKPAD_VERSION;
  bps.reserved = 0;
  memcpy(bps.bankPads, _wkBankPads, NUM_BANKS);
  if (NvsManager::saveBlob(BANKPAD_NVS_NAMESPACE, BANKPAD_NVS_KEY, &bps, sizeof(bps))) {
    memcpy(_bankPads, _wkBankPads, NUM_BANKS);
  } else {
    allOk = false;
  }

  // 2. ScalePadStore
  ScalePadStore sps;
  sps.magic = EEPROM_MAGIC;
  sps.version = SCALEPAD_VERSION;
  sps.reserved = 0;
  memcpy(sps.rootPads, _wkRootPads, 7);
  memcpy(sps.modePads, _wkModePads, 7);
  sps.chromaticPad = _wkChromPad;
  sps._pad = 0;
  if (NvsManager::saveBlob(SCALE_PAD_NVS_NAMESPACE, SCALEPAD_NVS_KEY, &sps, sizeof(sps))) {
    memcpy(_rootPads, _wkRootPads, 7);
    memcpy(_modePads, _wkModePads, 7);
    *_chromaticPad = _wkChromPad;
  } else {
    allOk = false;
  }

  // 3. ArpPadStore
  ArpPadStore aps;
  aps.magic = EEPROM_MAGIC;
  aps.version = ARPPAD_VERSION;
  aps.reserved = 0;
  aps.arpPlayStopPad = _wkArpPlayStopPad;
  memcpy(aps.octavePads, _wkOctavePads, 4);
  memset(aps._pad, 0, sizeof(aps._pad));
  if (NvsManager::saveBlob(ARP_PAD_NVS_NAMESPACE, ARPPAD_NVS_KEY, &aps, sizeof(aps))) {
    *_arpPlayStopPad      = _wkArpPlayStopPad;
    if (_octavePads) memcpy(_octavePads, _wkOctavePads, 4);
  } else {
    allOk = false;
  }

  // 4. LoopPadStore (Phase 3.F.1 + §12.12 audit M6) — extension critique :
  // sans cette persistance, les edits page LOOP sont perdus au reboot car
  // _wkLoopPad n'était synchronisé qu'au boot via getLoadedLoopPadStore().
  if (_nvs) {
    _nvs->setLoadedLoopPad(_wkLoopPad);
    if (!_nvs->saveLoopPad()) {
      allOk = false;
    }
  }

  _nvsSaved = allOk;
  return allOk;
}

// =================================================================
// drawGrid — 4x12 grid with pad numbers and role labels
// =================================================================

void ToolPadRoles::drawGrid() {
  int selectedPad = _gridRow * 12 + _gridCol;
  _ui->drawCellGrid(GRID_ROLES, 0, nullptr, nullptr, nullptr, selectedPad,
                     0, false, nullptr, _roleLabels, _roleMap);
}

// =================================================================
// drawPool — 3 pool lines + "none" option
// BUG FIX: When _editing == false, pool is a STATIC INVENTORY.
//          No reverse, no bold tracking of currentRole.
//          When _editing == true, pool is an ACTIVE SELECTOR.
// =================================================================

void ToolPadRoles::drawPool() {
  int selectedPad = _gridRow * 12 + _gridCol;

  // Helper to draw one pool category line
  auto drawPoolLine = [&](uint8_t lineNum, const char* label,
                          const char* const* labels, uint8_t count,
                          const char* lineColor) {
    bool isSelectedLine = _editing && (_poolLine == lineNum);
    char buf[256];
    int pos = 0;

    if (isSelectedLine) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, VT_CYAN VT_BOLD "> " VT_RESET "%-11s ", label);
    } else {
      pos += snprintf(buf + pos, sizeof(buf) - pos, "  %-11s ", label);
    }

    for (uint8_t i = 0; i < count; i++) {
      bool isCursor = isSelectedLine && (_poolIdx == i);
      uint8_t owner = findPadWithRole(lineNum, i);
      bool assignedElsewhere = (owner < NUM_KEYS && owner != (uint8_t)selectedPad);

      if (isCursor) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, VT_REVERSE VT_BOLD " %s " VT_RESET " ", labels[i]);
      } else if (_editing) {
        if (assignedElsewhere) {
          pos += snprintf(buf + pos, sizeof(buf) - pos, VT_DIM "%s" VT_RESET " ", labels[i]);
        } else {
          pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%s" VT_RESET " ", lineColor, labels[i]);
        }
      } else {
        // BUG FIX preserved: grid-nav mode = STATIC inventory
        if (owner < NUM_KEYS) {
          pos += snprintf(buf + pos, sizeof(buf) - pos, VT_DIM "%s" VT_RESET " ", labels[i]);
        } else {
          pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%s" VT_RESET " ", lineColor, labels[i]);
        }
      }
    }
    _ui->drawFrameLine("%s", buf);
  };

  // 6 category lines with distinct colors
  drawPoolLine(1, "Bank:",      POOL_BANK_LABELS,     POOL_BANK_COUNT,     VT_BLUE);
  drawPoolLine(2, "Root:",      POOL_ROOT_LABELS,     POOL_ROOT_COUNT,     VT_GREEN);
  drawPoolLine(3, "Mode:",      POOL_MODE_LABELS,     POOL_MODE_COUNT,     VT_CYAN);
  drawPoolLine(4, "Octave:",    POOL_OCTAVE_LABELS,   POOL_OCTAVE_COUNT,   VT_YELLOW);
  drawPoolLine(5, "Hold:",      POOL_PLAY_STOP_LABELS,     POOL_PLAY_STOP_COUNT,     VT_MAGENTA);

  // Clear action at the bottom
  {
    bool isSelectedLine = _editing && (_poolLine == 0);
    if (isSelectedLine) {
      _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET VT_DIM "[---] clear role" VT_RESET);
    } else {
      _ui->drawFrameLine("  " VT_DIM "[---] clear role" VT_RESET);
    }
  }
}

// =================================================================
// printRoleDescription — detailed info for a role (pool item)
// =================================================================

void ToolPadRoles::printRoleDescription(uint8_t line, uint8_t index) {
  switch (line) {
    case 0:
      _ui->drawFrameLine(VT_DIM "Remove role from this pad. It will play music notes in all modes." VT_RESET);
      break;
    case 1:  // Bank
      _ui->drawFrameLine(VT_BLUE "Bank %d selector" VT_RESET "  " VT_DIM "--  MIDI channel %d" VT_RESET, index + 1, index + 1);
      _ui->drawFrameLine(VT_DIM "Hold LEFT + press this pad to switch foreground bank." VT_RESET);
      _ui->drawFrameLine(VT_DIM "AllNotesOff sent on previous bank. Arp banks continue in background." VT_RESET);
      break;
    case 2: {  // Root
      static const char* noteNames[] = {"A", "B", "C", "D", "E", "F", "G"};
      _ui->drawFrameLine(VT_GREEN "Root note: %s" VT_RESET "  " VT_DIM "--  sets base pitch for scale resolution" VT_RESET, noteNames[index]);
      _ui->drawFrameLine(VT_DIM "Hold LEFT + press to change root. Applies to current bank's scale." VT_RESET);
      _ui->drawFrameLine(VT_DIM "In chromatic mode, root = lowest note. In scale mode, root = tonic." VT_RESET);
      break;
    }
    case 3:  // Mode
      if (index < 7) {
        static const char* modeNames[] = {"Ionian (Major)", "Dorian", "Phrygian", "Lydian",
                                           "Mixolydian", "Aeolian (Minor)", "Locrian"};
        static const char* modeIntervals[] = {"1 2 3 4 5 6 7", "1 2 b3 4 5 6 b7", "1 b2 b3 4 5 b6 b7",
                                               "1 2 3 #4 5 6 7", "1 2 3 4 5 6 b7", "1 2 b3 4 5 b6 b7",
                                               "1 b2 b3 4 b5 b6 b7"};
        _ui->drawFrameLine(VT_CYAN "Mode: %s" VT_RESET "  " VT_DIM "--  intervals: %s" VT_RESET, modeNames[index], modeIntervals[index]);
        _ui->drawFrameLine(VT_DIM "Hold LEFT + press to set mode. 7 pads mapped to padOrder positions." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Scale change on NORMAL bank: allNotesOff. On ARPEG: re-resolves at next tick." VT_RESET);
      } else {
        _ui->drawFrameLine(VT_CYAN "Chromatic toggle" VT_RESET "  " VT_DIM "--  switches between chromatic and scale mode" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Chromatic = all semitones from root. Scale = filtered through mode intervals." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Hold LEFT + press to toggle. Per-bank setting, saved to NVS." VT_RESET);
      }
      break;
    case 4:  // Octave
      _ui->drawFrameLine(VT_YELLOW "Octave range %d" VT_RESET "  " VT_DIM "--  ARPEG banks only" VT_RESET, index + 1);
      _ui->drawFrameLine(VT_DIM "Sets arp octave span. 1 = original notes, 4 = 4 octaves." VT_RESET);
      _ui->drawFrameLine(VT_DIM "48 pile positions x 4 octaves = up to 192 steps per cycle." VT_RESET);
      break;
    case 5:  // Hold
      _ui->drawFrameLine(VT_MAGENTA "HOLD toggle" VT_RESET "  " VT_DIM "--  ARPEG banks only" VT_RESET);
      _ui->drawFrameLine(VT_DIM "HOLD OFF: press=add to pile, release=remove. Arp stops when all fingers up." VT_RESET);
      _ui->drawFrameLine(VT_DIM "HOLD ON: press=add, double-tap=remove. Pile persists." VT_RESET);
      break;
  }
}

// =================================================================
// printPadDescription — info for a specific pad (in grid-nav mode)
// =================================================================

void ToolPadRoles::printPadDescription(uint8_t pad) {
  PadRole role = getRoleForPad(pad);
  if (role.line == 0) {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad %d" VT_RESET VT_DIM "  --  no role assigned" VT_RESET, pad + 1);
    _ui->drawFrameLine(VT_DIM "This pad is free. It will play music notes in all modes." VT_RESET);
    _ui->drawFrameLine(VT_DIM "Press [RET] to assign a role from the pool." VT_RESET);
  } else {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad %d" VT_RESET, pad + 1);
    printRoleDescription(role.line, role.index);
  }
}

// =================================================================
// drawInfoPanel — context-sensitive info (ALWAYS follows cursor)
// =================================================================

void ToolPadRoles::drawInfoPanel() {
  if (_confirmDefaults) {
    _ui->drawFrameLine(VT_YELLOW "Reset all roles to factory defaults? (y/n)" VT_RESET);
    return;
  }

  if (_confirmClearAll) {
    _ui->drawFrameLine(VT_YELLOW "Clear ALL roles from all 48 pads? (y/n)" VT_RESET);
    return;
  }

  if (_editing) {
    // Pool mode: info follows pool cursor
    printRoleDescription(_poolLine, _poolIdx);
  } else {
    // Grid mode: info follows grid cursor
    uint8_t pad = _gridRow * 12 + _gridCol;
    printPadDescription(pad);
  }
}

// =================================================================
// drawControlBar
// =================================================================

void ToolPadRoles::drawControlBar() {
  if (_confirmDefaults || _confirmClearAll) {
    _ui->drawControlBar(CBAR_CONFIRM_ANY);
    return;
  }

  if (_editing) {
    _ui->drawControlBar(VT_DIM "[^v<>] BROWSE" CBAR_SEP "[RET] ASSIGN" CBAR_SEP "[q] CANCEL" VT_RESET);
  } else {
    _ui->drawControlBar(VT_DIM "[^v<>] NAV  [TOUCH] JUMP" CBAR_SEP "[RET] EDIT  [d] DFLT  [r] CLEAR" CBAR_SEP "[q] EXIT" VT_RESET);
  }
}

// =================================================================
// drawScreen — full NASA console redraw
// =================================================================

void ToolPadRoles::drawScreen() {
  _ui->vtFrameStart();
  _ui->drawConsoleHeader("TOOL 3: PAD ROLE", _nvsSaved);

  // Phase 3 — sub-page header [BANK|ARPEG|LOOP|CC] highlighted
  _drawSubPageHeader();
  _ui->drawFrameEmpty();

  if (_activeSubPage == SUB_CC) {
    // Phase 3.C.1b — page CC dispatch (sections + control bar rendered inside).
    _drawPageCc();
  } else if (_activeSubPage == SUB_BANK) {
    // Phase 3.D.1 — page BANK dispatch (sections + control bar rendered inside).
    _drawPageBank();
  } else if (_activeSubPage == SUB_ARPEG) {
    // Phase 3.E.1 — page ARPEG dispatch.
    _drawPageArpeg();
  } else if (_activeSubPage == SUB_LOOP) {
    // Phase 3.F.1 — page LOOP dispatch.
    _drawPageLoop();
    // (overlay modale rendu plus bas, après la branche fallback)
  } else {
    // Legacy fallback (default switch — code mort post-3.F, retiré 3.H.2).
    // Grid section
    _ui->drawSection("GRID");
    drawGrid();
    _ui->drawFrameEmpty();

    // Pool section
    _ui->drawSection("POOL");
    drawPool();
    _ui->drawFrameEmpty();

    // Info section
    _ui->drawSection("INFO");
    drawInfoPanel();
    _ui->drawFrameEmpty();

    // Phase 3 — flash msg line (between info and control bar — m16 v2)
    _drawFlash();

    // Control bar
    drawControlBar();
  }

  // Phase 3.G — overlay modale d'écrasement §10 (pattern "inline INFO" §8.2 :
  // la page courante est rendue normalement, la question modale s'ajoute en
  // ligne sous le rendu — l'user garde le contexte grid + pool + info).
  if (_confirmOverwrite) {
    _drawOverwriteModale();
  }

  _ui->vtFrameEnd();
}

// =================================================================
// run() — main tool loop (blocking)
// =================================================================

void ToolPadRoles::run() {
  if (!_keyboard || !_leds || !_ui) return;
  Serial.print(ITERM_RESIZE);

  // Copy live values into working copies
  memcpy(_wkBankPads, _bankPads, NUM_BANKS);
  memcpy(_wkRootPads, _rootPads, 7);
  memcpy(_wkModePads, _modePads, 7);
  _wkChromPad    = *_chromaticPad;
  _wkArpPlayStopPad     = *_arpPlayStopPad;
  if (_octavePads) memcpy(_wkOctavePads, _octavePads, 4);

  // Load saved pad roles from NVS
  {
    BankPadStore bps;
    bool bpOk = NvsManager::loadBlob(BANKPAD_NVS_NAMESPACE, BANKPAD_NVS_KEY,
                                      EEPROM_MAGIC, BANKPAD_VERSION, &bps, sizeof(bps));
    if (bpOk) {
      validateBankPadStore(bps);
      memcpy(_wkBankPads, bps.bankPads, NUM_BANKS);
    }

    ScalePadStore sps;
    bool spOk = NvsManager::loadBlob(SCALE_PAD_NVS_NAMESPACE, SCALEPAD_NVS_KEY,
                                      EEPROM_MAGIC, SCALEPAD_VERSION, &sps, sizeof(sps));
    if (spOk) {
      validateScalePadStore(sps);
      memcpy(_wkRootPads, sps.rootPads, 7);
      memcpy(_wkModePads, sps.modePads, 7);
      _wkChromPad = sps.chromaticPad;
    }

    ArpPadStore aps;
    bool apOk = NvsManager::loadBlob(ARP_PAD_NVS_NAMESPACE, ARPPAD_NVS_KEY,
                                      EEPROM_MAGIC, ARPPAD_VERSION, &aps, sizeof(aps));
    if (apOk) {
      validateArpPadStore(aps);
      _wkArpPlayStopPad = aps.arpPlayStopPad;
      memcpy(_wkOctavePads, aps.octavePads, 4);
    }

    _nvsSaved = bpOk && spOk && apOk;
  }

  // Reset navigation state
  _gridRow = 0;
  _gridCol = 0;
  _editing = false;
  _poolLine = 0;
  _poolIdx = 0;
  _confirmDefaults = false;
  _confirmClearAll = false;

  // Phase 3.G — reset modale d'écrasement à l'entrée tool (re-entry safe)
  _confirmOverwrite        = false;
  _pendingOverwrite.action = OVERWRITE_ACTION_NONE;

  // Phase 3.C.2 + fix HW Gate G2 — reset state page CC à l'entrée tool
  // (re-entry safe : évite écran vide quand on rouvre Tool 3 sur SUB_CC
  // après un edit précédent, car _ccScreenDirty member persistait à false).
  _ccUiMode          = UI_CC_GRID_NAV;
  _ccFieldIdx        = 0;
  _ccPoolIdx         = 0;
  _ccGlobalFieldIdx  = 0;
  _ccPropEditDirty   = false;
  _ccGlobalEditDirty = false;
  _ccScreenDirty     = true;  // force redraw initial sur page CC

  captureBaselines(*_keyboard, _refBaselines);

  _ui->vtClear();
  bool screenDirty = true;

  while (true) {
    _leds->update();
    _keyboard->pollAllSensorData();
    NavEvent ev = _input.update();

    // =================================================================
    // Phase 3.G — dispatch modale d'écrasement (§10). Focus unique : tant
    // que la modale est active, tous les autres dispatchs (CC early-branch,
    // legacy, TAB) sont skippés. parseConfirm legacy : y = apply, any other
    // key = cancel.
    // =================================================================
    if (_confirmOverwrite) {
      ConfirmResult r = SetupUI::parseConfirm(ev);
      if (r == CONFIRM_YES) {
        _handleOverwriteModaleApply();
        _confirmOverwrite = false;
        _pendingOverwrite.action = OVERWRITE_ACTION_NONE;
        _editing = false;
        screenDirty = true;
      } else if (r == CONFIRM_NO) {
        _confirmOverwrite = false;
        _pendingOverwrite.action = OVERWRITE_ACTION_NONE;
        _editing = false;
        screenDirty = true;
      }
      // CONFIRM_PENDING : reste dans modale
      if (screenDirty) {
        screenDirty = false;
        buildRoleMap();
        drawScreen();
      }
      delay(5);
      continue;  // skip autres dispatchs tant que modale active
    }

    // =================================================================
    // Phase 3.C.1b — early-branch dispatch page CC (skip legacy si SUB_CC).
    // Le code legacy below ne s'exécute jamais quand SUB_CC est active.
    // Touch detection : gérée à l'intérieur de _handleGridNavCc.
    // =================================================================
    if (_activeSubPage == SUB_CC) {
      // Cleanup flash msg expiré (sinon reste visible jusqu'au prochain redraw).
      if (_flashMsg[0] != '\0' && millis() > _flashExpireMs) {
        _flashMsg[0] = '\0';
        _flashExpireMs = 0;
        _ccScreenDirty = true;
      }

      // TAB cycle (blocked durant sub-edit Cc)
      bool inCcSubEdit = (_ccUiMode != UI_CC_GRID_NAV);
      if (ev.type == NAV_CHAR && ev.ch == '\t' && !inCcSubEdit) {
        _handleTab();
        _ccScreenDirty = true;
      } else {
        // 2-step exit snapshot : préserve "q sort de sub-edit vers grid-nav,
        // q sur grid-nav sort du tool" (pattern setup-tools-conventions §6.4).
        CcUiMode modeAtStart = _ccUiMode;
        switch (_ccUiMode) {
          case UI_CC_GRID_NAV:         _handleGridNavCc(ev);         break;
          case UI_CC_MODE_PICK:        _handleModePickCc(ev);        break;
          case UI_CC_VALUE_EDIT:       _handleValueEditCc(ev);       break;
          case UI_CC_CONFIRM_DEFAULTS: _handleConfirmDefaultsCc(ev); break;
          case UI_CC_GLOBAL_EDIT:      _handleGlobalEditCc(ev);      break;
        }
        if (ev.type == NAV_QUIT && modeAtStart == UI_CC_GRID_NAV) {
          _ui->vtClear();
          return;
        }
      }

      if (_ccScreenDirty) {
        _ccScreenDirty = false;
        buildRoleMap();
        drawScreen();
      }
      delay(5);
      continue;
    }
    // =================================================================
    // Fin dispatch page CC — code legacy BANK/ARPEG/LOOP ci-dessous.
    // =================================================================

    // --- Touch detection (jump to cell) — legacy only ---
    if (!_confirmDefaults && !_confirmClearAll) {
      int detected = detectActiveKey(*_keyboard, _refBaselines);
      if (detected >= 0) {
        uint8_t newRow = (uint8_t)(detected / 12);
        uint8_t newCol = (uint8_t)(detected % 12);
        if (newRow != _gridRow || newCol != _gridCol) {
          if (_editing) {
            _editing = false;
          }
          _gridRow = newRow;
          _gridCol = newCol;
          screenDirty = true;
        }
      }
    }

    // --- Defaults confirmation sub-mode ---
    if (_confirmDefaults) {
      ConfirmResult r = SetupUI::parseConfirm(ev);
      if (r == CONFIRM_YES) {
        // Phase 3.D.1 / 3.E.3 / 3.F.3 — routing page-scoped (§15.1).
        // Chaque page utilise son propre _applyDefaults*().
        // SUB_CC : dispatcher interne (_handleConfirmDefaultsCc) via early-branch.
        // Legacy fallback resetToDefaults() = mort code post-3.F, retiré 3.H.2.
        if (_activeSubPage == SUB_BANK) {
          _applyDefaultsBank();
        } else if (_activeSubPage == SUB_ARPEG) {
          _applyDefaultsArpeg();
        } else if (_activeSubPage == SUB_LOOP) {
          _applyDefaultsLoop();
        } else {
          resetToDefaults();
        }
        if (saveAll()) {
          _ui->flashSaved();
        }
        _confirmDefaults = false;
        screenDirty = true;
      } else if (r == CONFIRM_NO) {
        _confirmDefaults = false;
        screenDirty = true;
      }
      if (screenDirty) {
        buildRoleMap();
        drawScreen();
        screenDirty = false;
      }
      delay(5);
      continue;
    }

    // --- Clear-all confirmation sub-mode ---
    if (_confirmClearAll) {
      ConfirmResult r = SetupUI::parseConfirm(ev);
      if (r == CONFIRM_YES) {
        clearAllRoles();
        if (saveAll()) {
          _ui->flashSaved();
        }
        _confirmClearAll = false;
        screenDirty = true;
      } else if (r == CONFIRM_NO) {
        _confirmClearAll = false;
        screenDirty = true;
      }
      if (screenDirty) {
        buildRoleMap();
        drawScreen();
        screenDirty = false;
      }
      delay(5);
      continue;
    }

    // --- Main navigation ---
    if (ev.type == NAV_QUIT) {
      if (_editing) {
        _editing = false;
        screenDirty = true;
      } else {
        // Phase 3.D.2 — §6.5 hard-constraint exit : les 8 banks doivent être
        // assignées pour pouvoir sortir du tool. Globale (toutes pages),
        // car les banks sont la fondation de toute la session musicale.
        bool allBanksAssigned = true;
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
          if (_wkBankPads[i] >= NUM_KEYS) {
            allBanksAssigned = false;
            break;
          }
        }
        if (!allBanksAssigned) {
          _setFlash("Les 8 banks doivent etre assignees pour sortir");
          screenDirty = true;
          // do NOT return — reste in tool jusqu'à ce que user assigne
        } else {
          _ui->vtClear();
          return;
        }
      }
    }

    if (ev.type == NAV_DEFAULTS && !_editing) {
      _confirmDefaults = true;
      screenDirty = true;
    }

    // [r] = Clear All legacy (§15.1 supprimé toutes pages incarnées
    // post-3.F). Conservation du code legacy = défense en profondeur si
    // future page non incarnée. Retiré définitivement 3.H.2.
    if (ev.type == NAV_CHAR && (ev.ch == 'r' || ev.ch == 'R')
        && !_editing
        && _activeSubPage != SUB_BANK
        && _activeSubPage != SUB_ARPEG
        && _activeSubPage != SUB_LOOP) {
      _confirmClearAll = true;
      screenDirty = true;
    }

    // Phase 3 — TAB cycle sub-page (NORM -> ARPEG -> LOOP -> NORM)
    // Only when not in pool edit mode (avoid mid-edit context switch).
    if (ev.type == NAV_CHAR && ev.ch == '\t' && !_editing) {
      _handleTab();
      screenDirty = true;
    }

    if (!_editing) {
      // --- Grid navigation ---
      bool arrowMoved = false;
      if (ev.type == NAV_UP) {
        if (_gridRow == 0) _gridRow = 3;
        else _gridRow--;
        arrowMoved = true;
      } else if (ev.type == NAV_DOWN) {
        if (_gridRow == 3) _gridRow = 0;
        else _gridRow++;
        arrowMoved = true;
      } else if (ev.type == NAV_RIGHT) {
        if (_gridCol == 11) {
          _gridCol = 0;
          if (_gridRow == 3) _gridRow = 0;
          else _gridRow++;
        } else {
          _gridCol++;
        }
        arrowMoved = true;
      } else if (ev.type == NAV_LEFT) {
        if (_gridCol == 0) {
          _gridCol = 11;
          if (_gridRow == 0) _gridRow = 3;
          else _gridRow--;
        } else {
          _gridCol--;
        }
        arrowMoved = true;
      } else if (ev.type == NAV_ENTER) {
        // Phase 3.D.1 / 3.E.2 / 3.F.2 — routing page-scoped (§7.4 strict uniforme).
        if (_activeSubPage == SUB_BANK) {
          _handleEnterBank();
        } else if (_activeSubPage == SUB_ARPEG) {
          _handleEnterArpeg();
        } else if (_activeSubPage == SUB_LOOP) {
          _handleEnterLoop();
        } else {
          // Legacy fallback (mort code post-3.F, retiré 3.H.2).
          _editing = true;
          int pad = _gridRow * 12 + _gridCol;
          PadRole role = getRoleForPad((uint8_t)pad);
          _poolLine = role.line;
          _poolIdx = role.index;
        }
        screenDirty = true;
      }
      if (arrowMoved) {
        screenDirty = true;
      }
    } else {
      // --- Pool navigation (edit mode) ---
      // Phase 3.D.1 / 3.E.2 / 3.F.2 — page-scoped pool nav range :
      //   SUB_BANK  : range [0..1] (clear/Bank)
      //   SUB_ARPEG : range [2..5] (Root/Mode/Octave/PL/S, pas de [---] clear §15.1)
      //   SUB_LOOP  : range [6..9] (REC/PS/CLR/Slots, pas de [---] clear §15.1)
      //   legacy    : range [0..9] (mort code post-3.F)
      uint8_t poolLineMin, poolLineMax;
      if (_activeSubPage == SUB_BANK) {
        poolLineMin = 0; poolLineMax = 1;
      } else if (_activeSubPage == SUB_ARPEG) {
        poolLineMin = 2; poolLineMax = 5;
      } else if (_activeSubPage == SUB_LOOP) {
        poolLineMin = 6; poolLineMax = 9;
      } else {
        poolLineMin = 0; poolLineMax = POOL_LINE_COUNT - 1;
      }

      bool poolArrowMoved = false;
      if (ev.type == NAV_UP) {
        if (_poolLine <= poolLineMin) _poolLine = poolLineMax;
        else if (_poolLine > poolLineMax) _poolLine = poolLineMax;  // safety après TAB
        else _poolLine--;
        uint8_t sz = poolLineSize(_poolLine);
        if (sz > 0 && _poolIdx >= sz) _poolIdx = sz - 1;
        poolArrowMoved = true;
      } else if (ev.type == NAV_DOWN) {
        if (_poolLine >= poolLineMax) _poolLine = poolLineMin;
        else if (_poolLine < poolLineMin) _poolLine = poolLineMin;  // safety après TAB
        else _poolLine++;
        uint8_t sz = poolLineSize(_poolLine);
        if (sz > 0 && _poolIdx >= sz) _poolIdx = sz - 1;
        poolArrowMoved = true;
      } else if (ev.type == NAV_LEFT) {
        uint8_t sz = poolLineSize(_poolLine);
        if (sz > 0) {
          if (_poolIdx == 0) _poolIdx = sz - 1;
          else _poolIdx--;
        }
        poolArrowMoved = true;
      } else if (ev.type == NAV_RIGHT) {
        uint8_t sz = poolLineSize(_poolLine);
        if (sz > 0) {
          if (_poolIdx >= sz - 1) _poolIdx = 0;
          else _poolIdx++;
        }
        poolArrowMoved = true;
      } else if (ev.type == NAV_ENTER) {
        // Phase 3.D.1 / 3.E.2 / 3.F.2 — routing page-scoped (§9.2 strict).
        if (_activeSubPage == SUB_BANK) {
          _handleEnterPoolBank();
          screenDirty = true;
        } else if (_activeSubPage == SUB_ARPEG) {
          _handleEnterPoolArpeg();
          screenDirty = true;
        } else if (_activeSubPage == SUB_LOOP) {
          _handleEnterPoolLoop();
          screenDirty = true;
        } else {
        int pad = _gridRow * 12 + _gridCol;

        if (_poolLine == 0) {
          clearRole((uint8_t)pad);
          if (saveAll()) {
            _ui->flashSaved();
            _editing = false;
          }
          screenDirty = true;
        } else {
          // Phase 3 — Task 12 v2 : bank slot move → refus si destination occupée par
          // rôle cross-store (LoopPadStore ou ControlPadStore), qui ne sont pas gérés
          // par clearRole(). R1 sacré spec §5 : Bank pad ne peut pas coexister avec
          // n'importe quel autre rôle. S'applique dans TOUTES les sous-pages (HW gate
          // G2 fix : ancien check sub-page-scoped trop restrictif).
          // Note : ARPEG roles (root/mode/etc) sont swap-able silencieusement via
          // clearRole(pad) ci-dessous — pas de refus pour ces cas (cohérence pattern
          // existing "Steal silencieux", modulo M1 v1 audit flash msg ajouté plus tard
          // Phase 3.D).
          bool refusedCross = false;
          if (_poolLine == 1 /* bank */ && _nvs) {
            const LoopPadStore& lp = _nvs->getLoadedLoopPadStore();
            if (isLoopControlPad(lp, (uint8_t)pad)) {
              _setFlash("Pad has LOOP control - move in Tool 3 LOOP first");
              refusedCross = true;
            } else if (findLoopSlotIdx(lp, (uint8_t)pad) >= 0) {
              _setFlash("Pad has LOOP slot - clear in Tool 3 LOOP first");
              refusedCross = true;
            } else if (findControlPadEntryIdx(_nvs->getLoadedControlPadStore(), (uint8_t)pad) >= 0) {
              _setFlash("Pad has ControlPad - delete in Tool 4 first");
              refusedCross = true;
            }
          }

          if (refusedCross) {
            _editing = false;
            screenDirty = true;
          } else {
            // Steal silencieux : si le role est deja pris par un autre pad,
            // on le libere directement sans demander confirmation.
            uint8_t owner = findPadWithRole(_poolLine, _poolIdx);
            if (owner < NUM_KEYS && owner != (uint8_t)pad) {
              clearRole(owner);
            }
            clearRole((uint8_t)pad);
            assignRole((uint8_t)pad, _poolLine, _poolIdx);
            if (saveAll()) {
              _ui->flashSaved();
              _editing = false;
            }
            screenDirty = true;
          }
        }
        }  // close `} else {` du routing SUB_BANK (Phase 3.D.1)
      }
      if (poolArrowMoved) {
        screenDirty = true;
      }
    }

    // --- Render ---
    if (screenDirty) {
      screenDirty = false;
      buildRoleMap();
      drawScreen();
    }

    delay(5);
  }
}

// =================================================================
// Phase 3.C.2 — cross-store helpers (cell display §8.1 + info panel +
// modale d'écrasement future 3.G). Consultés par toutes les pages
// (BANK/ARPEG/LOOP/CC) — implémentation unique dans ToolPadRoles.cpp.
// =================================================================

PadNeighborInfo ToolPadRoles::_padNeighborInfo(uint8_t pad) const {
  PadNeighborInfo info{};
  info.bankIdx        = findBankIdxForPad(_wkBankPads, pad);
  info.scaleRole      = scaleRoleAtPad(_wkRootPads, _wkModePads, _wkChromPad, pad);
  info.arpRole        = arpRoleAtPad(_wkArpPlayStopPad, _wkOctavePads, pad);
  info.loopSlotIdx    = findLoopSlotIdx(_wkLoopPad, pad);
  info.isLoopRec      = (_wkLoopPad.recPad      == pad);
  info.isLoopPlayStop = (_wkLoopPad.playStopPad == pad);
  info.isLoopClear    = (_wkLoopPad.clearPad    == pad);
  info.hasCc          = (findControlPadEntryIdx(_wkCc, pad) >= 0);
  return info;
}

// _formatRoleNameMusician — produit une phrase courte langue musicien pour
// les rôles CONTEXTUELs portés par un pad (consommée par info panel page CC
// 3.C.2, étendue verbose pour modale 3.G.1). Le rôle ABSORBANT BANK est
// traité séparément par le caller (cf §15.2 spec — naming Ion/Dor/etc).
void ToolPadRoles::_formatRoleNameMusician(const PadNeighborInfo& info,
                                            char* out, size_t cap) const {
  if (!out || cap == 0) return;
  out[0] = '\0';
  if (cap < 2) return;

  static const char* ROOT_NAMES[7] = {"A","B","C","D","E","F","G"};
  static const char* MODE_NAMES[7] = {"Ion","Dor","Phr","Lyd","Mix","Aeo","Loc"};

  // Helper : append " + " then text (space-separated cumulative naming).
  auto append = [&](const char* text) {
    if (!text || !text[0]) return;
    size_t cur = strlen(out);
    if (cur > 0) {
      if (cur + 3 < cap) {
        strcat(out, " + ");
        cur += 3;
      } else {
        return;
      }
    }
    size_t remain = cap - cur - 1;
    strncat(out, text, remain);
  };

  char buf[24];

  // Scale roles (Root / Mode / Chrom)
  switch (info.scaleRole.kind) {
    case ScaleRoleKind::ROOT:
      if (info.scaleRole.idx < 7) {
        snprintf(buf, sizeof(buf), "Root %s", ROOT_NAMES[info.scaleRole.idx]);
        append(buf);
      }
      break;
    case ScaleRoleKind::MODE:
      if (info.scaleRole.idx < 7) {
        snprintf(buf, sizeof(buf), "Mode %s", MODE_NAMES[info.scaleRole.idx]);
        append(buf);
      }
      break;
    case ScaleRoleKind::CHROM:
      append("Chromatic");
      break;
    case ScaleRoleKind::NONE:
    default:
      break;
  }

  // Arp roles (PL/S ARPEG / Octave)
  switch (info.arpRole.kind) {
    case ArpRoleKind::PLAY_STOP:
      append("PL/S ARPEG");
      break;
    case ArpRoleKind::OCTAVE:
      snprintf(buf, sizeof(buf), "Octave %u", (unsigned)(info.arpRole.idx + 1));
      append(buf);
      break;
    case ArpRoleKind::NONE:
    default:
      break;
  }

  // LOOP controls (REC / PS / CLR)
  if (info.isLoopRec)       append("LOOP REC");
  if (info.isLoopPlayStop)  append("LOOP PS");
  if (info.isLoopClear)     append("LOOP CLR");

  // LOOP slot
  if (info.loopSlotIdx >= 0) {
    snprintf(buf, sizeof(buf), "Slot %u", (unsigned)(info.loopSlotIdx + 1));
    append(buf);
  }
}

// =================================================================
// Phase 3.G — modale d'écrasement §10. Un absorbant (BANK/CC) veut occuper
// un pad portant 1-4 rôles CONTEXTUELs. Wording français langue musicien
// §10.2-10.3 ; effet 'y' = clear contextuels (retour pools) + assign.
// =================================================================

// _formatOverwriteWording — construit la question §10.2 selon les rôles
// contextuels du pad cible (1 à 4 rôles, accord singulier/pluriel).
void ToolPadRoles::_formatOverwriteWording(char* out, size_t cap) {
  PadNeighborInfo info = _padNeighborInfo(_pendingOverwrite.pad);

  // Static buffers locaux pour 4 rôles max (§4.2)
  char roleBufs[4][32];
  uint8_t roleCount = 0;

  // Modificateurs ARPEG (Root/Mode/Chrom) — suffixe " de ARPEG" §10.2
  if (info.scaleRole.kind == ScaleRoleKind::ROOT && roleCount < 4) {
    static const char* rootNames[7] = {"A","B","C","D","E","F","G"};
    snprintf(roleBufs[roleCount], 32, "ROOT %s de ARPEG", rootNames[info.scaleRole.idx]);
    roleCount++;
  } else if (info.scaleRole.kind == ScaleRoleKind::MODE && roleCount < 4) {
    static const char* modeNames[7] = {"Ion","Dor","Phr","Lyd","Mix","Aeo","Loc"};  // §15.2
    snprintf(roleBufs[roleCount], 32, "MODE %s de ARPEG", modeNames[info.scaleRole.idx]);
    roleCount++;
  } else if (info.scaleRole.kind == ScaleRoleKind::CHROM && roleCount < 4) {
    snprintf(roleBufs[roleCount], 32, "CHROMATIC de ARPEG");
    roleCount++;
  }

  // Octave + PL/S ARPEG
  if (info.arpRole.kind == ArpRoleKind::OCTAVE && roleCount < 4) {
    snprintf(roleBufs[roleCount], 32, "OCTAVE %u de ARPEG", (unsigned)(info.arpRole.idx + 1));
    roleCount++;
  }
  if (info.arpRole.kind == ArpRoleKind::PLAY_STOP && roleCount < 4) {
    snprintf(roleBufs[roleCount], 32, "PLAY/STOP de ARPEG");
    roleCount++;
  }

  // Transports LOOP + Slot (sans numéro de slot §10.3)
  if (info.isLoopRec && roleCount < 4)       { snprintf(roleBufs[roleCount], 32, "REC de LOOP");       roleCount++; }
  if (info.isLoopPlayStop && roleCount < 4)  { snprintf(roleBufs[roleCount], 32, "PLAY/STOP de LOOP"); roleCount++; }
  if (info.isLoopClear && roleCount < 4)     { snprintf(roleBufs[roleCount], 32, "CLEAR de LOOP");     roleCount++; }
  if (info.loopSlotIdx >= 0 && roleCount < 4) { snprintf(roleBufs[roleCount], 32, "SLOT de LOOP");     roleCount++; }

  // Label absorbant à assigner
  char absorbantLabel[16];
  if (_pendingOverwrite.action == OVERWRITE_ACTION_BANK_ASSIGN) {
    snprintf(absorbantLabel, sizeof(absorbantLabel), "B%u", (unsigned)(_pendingOverwrite.bankIdx + 1));
  } else {  // CC_CREATE
    snprintf(absorbantLabel, sizeof(absorbantLabel), "CC");
  }

  // Construction phrase selon roleCount (accord singulier/pluriel §10.2)
  if (roleCount == 1) {
    snprintf(out, cap,
             "En placant %s sur ce pad, \"%s\" devra etre reattribue. Y/N ?",
             absorbantLabel, roleBufs[0]);
  } else if (roleCount == 2) {
    snprintf(out, cap,
             "En placant %s sur ce pad, \"%s\" et \"%s\" devront etre reattribues. Y/N ?",
             absorbantLabel, roleBufs[0], roleBufs[1]);
  } else if (roleCount == 3) {
    snprintf(out, cap,
             "En placant %s sur ce pad, \"%s\", \"%s\" et \"%s\" devront etre reattribues. Y/N ?",
             absorbantLabel, roleBufs[0], roleBufs[1], roleBufs[2]);
  } else if (roleCount == 4) {
    snprintf(out, cap,
             "En placant %s sur ce pad, \"%s\", \"%s\", \"%s\" et \"%s\" devront etre reattribues. Y/N ?",
             absorbantLabel, roleBufs[0], roleBufs[1], roleBufs[2], roleBufs[3]);
  } else {
    // Edge case roleCount == 0 (ne devrait pas arriver — modale appelée
    // seulement quand contextuels présents)
    snprintf(out, cap, "Assigner %s ici ? Y/N ?", absorbantLabel);
  }
}

// _handleOverwriteModaleApply — effet 'y' : les rôles contextuels du pad
// retournent à leurs pools respectifs, l'absorbant est assigné, save NVS.
void ToolPadRoles::_handleOverwriteModaleApply() {
  PadNeighborInfo info = _padNeighborInfo(_pendingOverwrite.pad);

  // Étape 1 : retirer rôles contextuels → retournent à leurs pools respectifs
  if (info.scaleRole.kind != ScaleRoleKind::NONE
      || info.arpRole.kind != ArpRoleKind::NONE) {
    _clearRolesArpegOnly(_pendingOverwrite.pad);
  }
  if (info.loopSlotIdx >= 0 || info.isLoopRec
      || info.isLoopPlayStop || info.isLoopClear) {
    _clearRolesLoopOnly(_pendingOverwrite.pad);
  }

  // Étape 2 : assigner l'absorbant selon contexte
  switch (_pendingOverwrite.action) {
    case OVERWRITE_ACTION_BANK_ASSIGN:
      // Silent steal acté §9.4 : la modale a été acceptée explicitement,
      // libérer le pad cible et la bank cible de leurs assignments préalables.
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        if (_wkBankPads[i] == _pendingOverwrite.pad) _wkBankPads[i] = 0xFF;
      }
      _wkBankPads[_pendingOverwrite.bankIdx] = _pendingOverwrite.pad;
      break;
    case OVERWRITE_ACTION_CC_CREATE:
      // Crée CC entry avec defaults puis applique le mode choisi en
      // MODE_PICK (préservé via _pendingOverwrite.ccPoolIdx — sans cela,
      // un choix LATCH retomberait silencieusement sur MOM).
      if (_addSlotCc(_pendingOverwrite.pad)) {
        int8_t s = _findSlotCc(_pendingOverwrite.pad);
        if (s >= 0) {
          _applyPoolIdxToEntryCc(_pendingOverwrite.ccPoolIdx, _wkCc.entries[s]);
          _saveCc();
        }
      }
      break;
    case OVERWRITE_ACTION_NONE:
      break;
  }

  // Étape 3 : save NVS (BankPad + ScalePad + ArpPad + LoopPad ; CC déjà
  // persisté via _saveCc ci-dessus, pattern save-per-commit §14.1)
  if (saveAll()) _ui->flashSaved();
}

// _drawOverwriteModale — overlay INFO §8.2 (la page reste rendue, la
// question s'ajoute en ligne — l'user garde le contexte visuel complet).
void ToolPadRoles::_drawOverwriteModale() {
  char wording[256];
  _formatOverwriteWording(wording, sizeof(wording));
  _ui->drawFrameLine(VT_YELLOW "%s" VT_RESET, wording);
}
