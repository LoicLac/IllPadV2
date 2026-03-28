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

static const char* GRID_HOLD_LABELS[] = { " Hld" };
static const char* GRID_PLAYSTOP_LABELS[] = { " P/S" };

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

static const char* POOL_HOLD_LABELS[] = { "Hld" };
static const char* POOL_PLAYSTOP_LABELS[] = { "P/S" };

// =================================================================
// Constructor
// =================================================================

ToolPadRoles::ToolPadRoles()
  : _keyboard(nullptr), _leds(nullptr), _nvs(nullptr), _ui(nullptr),
    _bankPads(nullptr), _rootPads(nullptr), _modePads(nullptr),
    _chromaticPad(nullptr), _holdPad(nullptr), _playStopPad(nullptr),
    _octavePads(nullptr),
    _wkChromPad(0xFF), _wkHoldPad(0xFF), _wkPlayStopPad(0xFF),
    _gridRow(0), _gridCol(0), _editing(false),
    _poolLine(0), _poolIdx(0),
    _confirmSteal(false), _stealFromPad(0xFF),
    _confirmDefaults(false), _confirmClearAll(false), _nvsSaved(false)
{
  memset(_wkBankPads, 0xFF, sizeof(_wkBankPads));
  memset(_wkRootPads, 0xFF, sizeof(_wkRootPads));
  memset(_wkModePads, 0xFF, sizeof(_wkModePads));
  memset(_wkOctavePads, 0xFF, sizeof(_wkOctavePads));
  memset(_refBaselines, 0, sizeof(_refBaselines));
}

void ToolPadRoles::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                          NvsManager* nvs, SetupUI* ui,
                          uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
                          uint8_t& chromaticPad, uint8_t& holdPad, uint8_t& playStopPad,
                          uint8_t* octavePads) {
  _keyboard     = keyboard;
  _leds         = leds;
  _nvs          = nvs;
  _ui           = ui;
  _bankPads     = bankPads;
  _rootPads     = rootPads;
  _modePads     = modePads;
  _chromaticPad = &chromaticPad;
  _holdPad      = &holdPad;
  _playStopPad  = &playStopPad;
  _octavePads   = octavePads;
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
    case 5: return POOL_HOLD_COUNT;
    case 6: return POOL_PLAYSTOP_COUNT;
    default: return 0;
  }
}

const char* ToolPadRoles::poolItemLabel(uint8_t line, uint8_t index) const {
  switch (line) {
    case 1: return (index < POOL_BANK_COUNT)     ? POOL_BANK_LABELS[index]     : "???";
    case 2: return (index < POOL_ROOT_COUNT)     ? POOL_ROOT_LABELS[index]     : "???";
    case 3: return (index < POOL_MODE_COUNT)     ? POOL_MODE_LABELS[index]     : "???";
    case 4: return (index < POOL_OCTAVE_COUNT)   ? POOL_OCTAVE_LABELS[index]   : "???";
    case 5: return (index < POOL_HOLD_COUNT)     ? POOL_HOLD_LABELS[index]     : "???";
    case 6: return (index < POOL_PLAYSTOP_COUNT) ? POOL_PLAYSTOP_LABELS[index] : "???";
    default: return "---";
  }
}

// =================================================================
// buildRoleMap — scan all assignments, populate _roleMap + _roleLabels
// =================================================================

void ToolPadRoles::buildRoleMap() {
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
  setRole(_wkHoldPad, ROLE_HOLD, GRID_HOLD_LABELS[0]);
  setRole(_wkPlayStopPad, ROLE_PLAYSTOP, GRID_PLAYSTOP_LABELS[0]);
  for (int i = 0; i < 4; i++)
    setRole(_wkOctavePads[i], ROLE_OCTAVE, GRID_OCTAVE_LABELS[i]);
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
  if (_wkHoldPad == pad) return {5, 0};
  if (_wkPlayStopPad == pad) return {6, 0};
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
      if (index == 0) return _wkHoldPad;
      break;
    case 6:
      if (index == 0) return _wkPlayStopPad;
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
      if (index == 0) _wkHoldPad = pad;
      break;
    case 6:
      if (index == 0) _wkPlayStopPad = pad;
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
  if (_wkHoldPad == pad) _wkHoldPad = 0xFF;
  if (_wkPlayStopPad == pad) _wkPlayStopPad = 0xFF;
  for (uint8_t i = 0; i < 4; i++) {
    if (_wkOctavePads[i] == pad) _wkOctavePads[i] = 0xFF;
  }
}

void ToolPadRoles::clearAllRoles() {
  memset(_wkBankPads, 0xFF, sizeof(_wkBankPads));
  memset(_wkRootPads, 0xFF, sizeof(_wkRootPads));
  memset(_wkModePads, 0xFF, sizeof(_wkModePads));
  _wkChromPad    = 0xFF;
  _wkHoldPad     = 0xFF;
  _wkPlayStopPad = 0xFF;
  memset(_wkOctavePads, 0xFF, sizeof(_wkOctavePads));
}

void ToolPadRoles::resetToDefaults() {
  for (uint8_t i = 0; i < NUM_BANKS; i++) _wkBankPads[i] = i;
  for (uint8_t i = 0; i < 7; i++) _wkRootPads[i] = 8 + i;
  for (uint8_t i = 0; i < 7; i++) _wkModePads[i] = 15 + i;
  _wkChromPad    = 22;
  _wkHoldPad     = 23;
  _wkPlayStopPad = 24;
  _wkOctavePads[0] = 25;
  _wkOctavePads[1] = 26;
  _wkOctavePads[2] = 27;
  _wkOctavePads[3] = 28;
}

// =================================================================
// saveAll — direct NVS write (setup mode, NVS task not running)
// =================================================================

bool ToolPadRoles::saveAll() {
  Preferences prefs;
  bool ok = true;

  BankPadStore bps;
  bps.magic    = EEPROM_MAGIC;
  bps.version  = BANKPAD_VERSION;
  bps.reserved = 0;
  memcpy(bps.bankPads, _wkBankPads, NUM_BANKS);
  if (prefs.begin(BANKPAD_NVS_NAMESPACE, false)) {
    prefs.putBytes(BANKPAD_NVS_KEY, &bps, sizeof(BankPadStore));
    prefs.end();
  } else { ok = false; }

  if (prefs.begin(SCALE_PAD_NVS_NAMESPACE, false)) {
    prefs.putBytes(SCALE_PAD_ROOT_KEY, _wkRootPads, 7);
    prefs.putBytes(SCALE_PAD_MODE_KEY, _wkModePads, 7);
    prefs.putUChar(SCALE_PAD_CHROM_KEY, _wkChromPad);
    prefs.end();
  } else { ok = false; }

  if (prefs.begin(ARP_PAD_NVS_NAMESPACE, false)) {
    prefs.putUChar(ARP_PAD_HOLD_KEY, _wkHoldPad);
    prefs.putUChar(ARP_PAD_PS_KEY, _wkPlayStopPad);
    prefs.putBytes(ARP_PAD_OCT_KEY, _wkOctavePads, 4);
    prefs.end();
  } else { ok = false; }

  if (!ok) return false;

  memcpy(_bankPads, _wkBankPads, NUM_BANKS);
  memcpy(_rootPads, _wkRootPads, 7);
  memcpy(_modePads, _wkModePads, 7);
  *_chromaticPad = _wkChromPad;
  *_holdPad      = _wkHoldPad;
  *_playStopPad  = _wkPlayStopPad;
  if (_octavePads) memcpy(_octavePads, _wkOctavePads, 4);

  _nvsSaved = true;
  return true;
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
  drawPoolLine(5, "Hold:",      POOL_HOLD_LABELS,     POOL_HOLD_COUNT,     VT_MAGENTA);
  drawPoolLine(6, "Play/Stop:", POOL_PLAYSTOP_LABELS, POOL_PLAYSTOP_COUNT, VT_BRIGHT_RED);

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
      _ui->drawFrameLine(VT_DIM "HOLD ON: press=add, double-tap=remove. Pile persists. Use Play/Stop for transport." VT_RESET);
      break;
    case 6:  // Play/Stop
      _ui->drawFrameLine(VT_BRIGHT_RED "Play/Stop" VT_RESET "  " VT_DIM "--  ARPEG + HOLD ON only" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Toggles arp transport. Restarts sequence from beginning on Play." VT_RESET);
      _ui->drawFrameLine(VT_DIM "In HOLD OFF mode, this pad plays as a regular music pad." VT_RESET);
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
  if (_confirmSteal) {
    _ui->drawFrameLine(VT_YELLOW "Already assigned to pad %d. Replace? (y/n)" VT_RESET,
                       _stealFromPad + 1);
    return;
  }

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
  if (_confirmSteal || _confirmDefaults || _confirmClearAll) {
    _ui->drawControlBar(VT_DIM "[y] confirm  [any] cancel" VT_RESET);
    return;
  }

  if (_editing) {
    _ui->drawControlBar(VT_DIM "[^v] pool line  [<>] cycle  [RET] assign  [q] cancel" VT_RESET);
  } else {
    _ui->drawControlBar(VT_DIM "[^v<>] NAV  [RET] EDIT  [TOUCH] JUMP  [d] DFLT  [r] CLEAR  [q] EXIT" VT_RESET);
  }
}

// =================================================================
// drawScreen — full NASA console redraw
// =================================================================

void ToolPadRoles::drawScreen() {
  _ui->vtFrameStart();
  _ui->drawConsoleHeader("TOOL 3: PAD ROLES", _nvsSaved);

  _ui->drawFrameEmpty();

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

  // Control bar
  drawControlBar();

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
  _wkHoldPad     = *_holdPad;
  _wkPlayStopPad = *_playStopPad;
  if (_octavePads) memcpy(_wkOctavePads, _octavePads, 4);

  // Check initial NVS status
  {
    Preferences prefs;
    if (prefs.begin(BANKPAD_NVS_NAMESPACE, true)) {
      _nvsSaved = (prefs.getBytesLength(BANKPAD_NVS_KEY) > 0);
      prefs.end();
    }
  }

  // Reset navigation state
  _gridRow = 0;
  _gridCol = 0;
  _editing = false;
  _poolLine = 0;
  _poolIdx = 0;
  _confirmSteal = false;
  _stealFromPad = 0xFF;
  _confirmDefaults = false;
  _confirmClearAll = false;

  captureBaselines(*_keyboard, _refBaselines);

  _ui->vtClear();
  bool screenDirty = true;

  while (true) {
    _leds->update();
    _keyboard->pollAllSensorData();


    // --- Touch detection (jump to cell) ---
    if (!_confirmSteal && !_confirmDefaults && !_confirmClearAll) {
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

    NavEvent ev = _input.update();

    // --- Defaults confirmation sub-mode ---
    if (_confirmDefaults) {
      if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
        resetToDefaults();
        if (saveAll()) {
          _ui->flashSaved();
        }
        _confirmDefaults = false;
        screenDirty = true;
      } else if (ev.type != NAV_NONE) {
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
      if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
        clearAllRoles();
        if (saveAll()) {
          _ui->flashSaved();
        }
        _confirmClearAll = false;
        screenDirty = true;
      } else if (ev.type != NAV_NONE) {
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

    // --- Steal confirmation sub-mode ---
    if (_confirmSteal) {
      if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
        clearRole(_stealFromPad);
        int pad = _gridRow * 12 + _gridCol;
        clearRole((uint8_t)pad);
        assignRole((uint8_t)pad, _poolLine, _poolIdx);
        if (saveAll()) {
          _ui->flashSaved();
          _editing = false;
        }
        _confirmSteal = false;
        screenDirty = true;
      } else if (ev.type != NAV_NONE) {
        _confirmSteal = false;
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
        _ui->vtClear();
        return;
      }
    }

    if (ev.type == NAV_DEFAULTS && !_editing) {
      _confirmDefaults = true;
      screenDirty = true;
    }

    // [r] = Clear All
    if (ev.type == NAV_CHAR && (ev.ch == 'r' || ev.ch == 'R') && !_editing) {
      _confirmClearAll = true;
      screenDirty = true;
    }

    if (!_editing) {
      // --- Grid navigation ---
      if (ev.type == NAV_UP) {
        if (_gridRow == 0) _gridRow = 3;
        else _gridRow--;
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        if (_gridRow == 3) _gridRow = 0;
        else _gridRow++;
        screenDirty = true;
      } else if (ev.type == NAV_RIGHT) {
        if (_gridCol == 11) {
          _gridCol = 0;
          if (_gridRow == 3) _gridRow = 0;
          else _gridRow++;
        } else {
          _gridCol++;
        }
        screenDirty = true;
      } else if (ev.type == NAV_LEFT) {
        if (_gridCol == 0) {
          _gridCol = 11;
          if (_gridRow == 0) _gridRow = 3;
          else _gridRow--;
        } else {
          _gridCol--;
        }
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        _editing = true;
        int pad = _gridRow * 12 + _gridCol;
        PadRole role = getRoleForPad((uint8_t)pad);
        _poolLine = role.line;
        _poolIdx = role.index;
        screenDirty = true;
      }
    } else {
      // --- Pool navigation (edit mode) ---
      if (ev.type == NAV_UP) {
        if (_poolLine == 0) _poolLine = POOL_LINE_COUNT - 1;
        else _poolLine--;
        uint8_t sz = poolLineSize(_poolLine);
        if (sz > 0 && _poolIdx >= sz) _poolIdx = sz - 1;
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        if (_poolLine == POOL_LINE_COUNT - 1) _poolLine = 0;
        else _poolLine++;
        uint8_t sz = poolLineSize(_poolLine);
        if (sz > 0 && _poolIdx >= sz) _poolIdx = sz - 1;
        screenDirty = true;
      } else if (ev.type == NAV_LEFT) {
        uint8_t sz = poolLineSize(_poolLine);
        if (sz > 0) {
          if (_poolIdx == 0) _poolIdx = sz - 1;
          else _poolIdx--;
        }
        screenDirty = true;
      } else if (ev.type == NAV_RIGHT) {
        uint8_t sz = poolLineSize(_poolLine);
        if (sz > 0) {
          if (_poolIdx >= sz - 1) _poolIdx = 0;
          else _poolIdx++;
        }
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        int pad = _gridRow * 12 + _gridCol;

        if (_poolLine == 0) {
          clearRole((uint8_t)pad);
          if (saveAll()) {
            _ui->flashSaved();
            _editing = false;
          }
          screenDirty = true;
        } else {
          uint8_t owner = findPadWithRole(_poolLine, _poolIdx);
          if (owner < NUM_KEYS && owner != (uint8_t)pad) {
            _confirmSteal = true;
            _stealFromPad = owner;
          } else {
            clearRole((uint8_t)pad);
            assignRole((uint8_t)pad, _poolLine, _poolIdx);
            if (saveAll()) {
              _ui->flashSaved();
              _editing = false;
            }
          }
          screenDirty = true;
        }
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
