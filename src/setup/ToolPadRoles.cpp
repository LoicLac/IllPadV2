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
// Short labels for pool items (4 chars max for grid, longer for pool)
// =================================================================

// Grid labels: 4-char, used in the 4x12 grid display
static const char* GRID_BANK_LABELS[] = {
  " Bk1", " Bk2", " Bk3", " Bk4", " Bk5", " Bk6", " Bk7", " Bk8"
};

static const char* GRID_SCALE_LABELS[] = {
  " RtC", "RtC#", " RtD", "RtD#", " RtE", " RtF", " RtG",
  "MdIo", "MdDo", "MdPh", "MdLy", "MdMx", "MdLo", "MdAe", " Chr"
};

static const char* GRID_ARP_LABELS[] = {
  " Hld", " P/S", " Oc1", " Oc2", " Oc3", " Oc4"
};

// Pool labels: used in the pool line display
static const char* POOL_BANK_LABELS[] = {
  "Bk1", "Bk2", "Bk3", "Bk4", "Bk5", "Bk6", "Bk7", "Bk8"
};

static const char* POOL_SCALE_LABELS[] = {
  "RtC", "RtC#", "RtD", "RtD#", "RtE", "RtF", "RtG",
  "MdIo", "MdDo", "MdPh", "MdLy", "MdMx", "MdLo", "MdAe", "Chr"
};

static const char* POOL_ARP_LABELS[] = {
  "Hld", "P/S", "Oc1", "Oc2", "Oc3", "Oc4"
};

// Description strings for each pool item
static const char* BANK_DESCS[] = {
  "Bank 1 selector pad", "Bank 2 selector pad", "Bank 3 selector pad", "Bank 4 selector pad",
  "Bank 5 selector pad", "Bank 6 selector pad", "Bank 7 selector pad", "Bank 8 selector pad"
};

static const char* SCALE_DESCS[] = {
  "Root note: C",  "Root note: C#", "Root note: D",  "Root note: D#",
  "Root note: E",  "Root note: F",  "Root note: G",
  "Mode: Ionian (Major)",  "Mode: Dorian",  "Mode: Phrygian",
  "Mode: Lydian",  "Mode: Mixolydian",  "Mode: Locrian",  "Mode: Aeolian (Minor)",
  "Chromatic toggle"
};

static const char* ARP_DESCS[] = {
  "Hold toggle (ARPEG only)", "Play/Stop (ARPEG + HOLD ON)",
  "Octave range 1", "Octave range 2", "Octave range 3", "Octave range 4"
};

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
    _confirmDefaults(false)
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
    case 2: return POOL_SCALE_COUNT;
    case 3: return POOL_ARP_COUNT;
    default: return 0;  // line 0 = "none", no items
  }
}

const char* ToolPadRoles::poolItemLabel(uint8_t line, uint8_t index) const {
  switch (line) {
    case 1: return (index < POOL_BANK_COUNT)  ? POOL_BANK_LABELS[index]  : "???";
    case 2: return (index < POOL_SCALE_COUNT) ? POOL_SCALE_LABELS[index] : "???";
    case 3: return (index < POOL_ARP_COUNT)   ? POOL_ARP_LABELS[index]   : "???";
    default: return "none";
  }
}

const char* ToolPadRoles::poolItemColor(uint8_t line) const {
  switch (line) {
    case 1: return VT_BLUE;
    case 2: return VT_GREEN;
    case 3: return VT_YELLOW;
    default: return VT_DIM;
  }
}

// =================================================================
// buildRoleMap — scan all assignments, populate _roleMap + _roleLabels
// =================================================================

void ToolPadRoles::buildRoleMap() {
  memset(_roleMap, ROLE_NONE, NUM_KEYS);
  for (int i = 0; i < NUM_KEYS; i++) {
    memcpy(_roleLabels[i], "  -- ", 6);
  }

  // Bank pads
  for (int i = 0; i < NUM_BANKS; i++) {
    uint8_t pad = _wkBankPads[i];
    if (pad < NUM_KEYS) {
      _roleMap[pad] = ROLE_BANK;
      snprintf(_roleLabels[pad], 6, "%s", GRID_BANK_LABELS[i]);
    }
  }

  // Root pads
  for (int i = 0; i < 7; i++) {
    uint8_t pad = _wkRootPads[i];
    if (pad < NUM_KEYS) {
      _roleMap[pad] = ROLE_SCALE;
      snprintf(_roleLabels[pad], 6, "%s", GRID_SCALE_LABELS[i]);
    }
  }

  // Mode pads
  for (int i = 0; i < 7; i++) {
    uint8_t pad = _wkModePads[i];
    if (pad < NUM_KEYS) {
      _roleMap[pad] = ROLE_SCALE;
      snprintf(_roleLabels[pad], 6, "%s", GRID_SCALE_LABELS[7 + i]);
    }
  }

  // Chromatic pad
  if (_wkChromPad < NUM_KEYS) {
    _roleMap[_wkChromPad] = ROLE_SCALE;
    snprintf(_roleLabels[_wkChromPad], 6, "%s", GRID_SCALE_LABELS[14]);
  }

  // Hold pad
  if (_wkHoldPad < NUM_KEYS) {
    _roleMap[_wkHoldPad] = ROLE_ARP;
    snprintf(_roleLabels[_wkHoldPad], 6, "%s", GRID_ARP_LABELS[0]);
  }

  // Play/Stop pad
  if (_wkPlayStopPad < NUM_KEYS) {
    _roleMap[_wkPlayStopPad] = ROLE_ARP;
    snprintf(_roleLabels[_wkPlayStopPad], 6, "%s", GRID_ARP_LABELS[1]);
  }

  // Octave pads (1-4)
  for (int i = 0; i < 4; i++) {
    uint8_t pad = _wkOctavePads[i];
    if (pad < NUM_KEYS) {
      _roleMap[pad] = ROLE_ARP;
      snprintf(_roleLabels[pad], 6, "%s", GRID_ARP_LABELS[2 + i]);
    }
  }
}

// =================================================================
// getRoleForPad — returns {line, index} for a pad's current role
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
    if (_wkModePads[i] == pad) return {2, (uint8_t)(7 + i)};
  }
  if (_wkChromPad == pad) return {2, 14};
  if (_wkHoldPad == pad) return {3, 0};
  if (_wkPlayStopPad == pad) return {3, 1};
  for (uint8_t i = 0; i < 4; i++) {
    if (_wkOctavePads[i] == pad) return {3, (uint8_t)(2 + i)};
  }
  return {0, 0};  // none
}

// =================================================================
// findPadWithRole — which pad has this role? 0xFF = none
// =================================================================

uint8_t ToolPadRoles::findPadWithRole(uint8_t line, uint8_t index) const {
  switch (line) {
    case 1:  // Bank
      if (index < NUM_BANKS) return _wkBankPads[index];
      break;
    case 2:  // Scale
      if (index < 7) return _wkRootPads[index];
      if (index < 14) return _wkModePads[index - 7];
      if (index == 14) return _wkChromPad;
      break;
    case 3:  // Arp
      if (index == 0) return _wkHoldPad;
      if (index == 1) return _wkPlayStopPad;
      if (index >= 2 && index <= 5) return _wkOctavePads[index - 2];
      break;
  }
  return 0xFF;
}

// =================================================================
// assignRole — write a role into the working arrays
// =================================================================

void ToolPadRoles::assignRole(uint8_t pad, uint8_t line, uint8_t index) {
  switch (line) {
    case 1:  // Bank
      if (index < NUM_BANKS) _wkBankPads[index] = pad;
      break;
    case 2:  // Scale
      if (index < 7) _wkRootPads[index] = pad;
      else if (index < 14) _wkModePads[index - 7] = pad;
      else if (index == 14) _wkChromPad = pad;
      break;
    case 3:  // Arp
      if (index == 0) _wkHoldPad = pad;
      else if (index == 1) _wkPlayStopPad = pad;
      else if (index >= 2 && index <= 5) _wkOctavePads[index - 2] = pad;
      break;
  }
}

// =================================================================
// clearRole — remove whatever role a pad currently has
// =================================================================

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

// =================================================================
// resetToDefaults — factory default pad assignments
// =================================================================

void ToolPadRoles::resetToDefaults() {
  for (uint8_t i = 0; i < NUM_BANKS; i++) _wkBankPads[i] = i;         // pads 0-7
  for (uint8_t i = 0; i < 7; i++) _wkRootPads[i] = 8 + i;            // pads 8-14
  for (uint8_t i = 0; i < 7; i++) _wkModePads[i] = 15 + i;           // pads 15-21
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

  // Bank pads
  BankPadStore bps;
  bps.magic    = EEPROM_MAGIC;
  bps.version  = BANKPAD_VERSION;
  bps.reserved = 0;
  memcpy(bps.bankPads, _wkBankPads, NUM_BANKS);
  if (prefs.begin(BANKPAD_NVS_NAMESPACE, false)) {
    prefs.putBytes(BANKPAD_NVS_KEY, &bps, sizeof(BankPadStore));
    prefs.end();
  } else { ok = false; }

  // Scale pads
  if (prefs.begin(SCALE_PAD_NVS_NAMESPACE, false)) {
    prefs.putBytes(SCALE_PAD_ROOT_KEY, _wkRootPads, 7);
    prefs.putBytes(SCALE_PAD_MODE_KEY, _wkModePads, 7);
    prefs.putUChar(SCALE_PAD_CHROM_KEY, _wkChromPad);
    prefs.end();
  } else { ok = false; }

  // Arp pads
  if (prefs.begin(ARP_PAD_NVS_NAMESPACE, false)) {
    prefs.putUChar(ARP_PAD_HOLD_KEY, _wkHoldPad);
    prefs.putUChar(ARP_PAD_PS_KEY, _wkPlayStopPad);
    prefs.putBytes(ARP_PAD_OCT_KEY, _wkOctavePads, 4);
    prefs.end();
  } else { ok = false; }

  if (!ok) return false;

  // Update live values only after all writes succeeded
  memcpy(_bankPads, _wkBankPads, NUM_BANKS);
  memcpy(_rootPads, _wkRootPads, 7);
  memcpy(_modePads, _wkModePads, 7);
  *_chromaticPad = _wkChromPad;
  *_holdPad      = _wkHoldPad;
  *_playStopPad  = _wkPlayStopPad;
  if (_octavePads) memcpy(_octavePads, _wkOctavePads, 4);

  return true;
}

// =================================================================
// drawGrid — 4x12 grid with pad numbers and role labels
// =================================================================

void ToolPadRoles::drawGrid() {
  int selectedPad = _gridRow * 12 + _gridCol;

  // Column headers
  Serial.printf("      ");
  for (int col = 0; col < 12; col++) {
    Serial.printf(" %02d  ", col + 1);
  }
  Serial.printf(VT_CL "\n");

  for (int row = 0; row < 4; row++) {
    Serial.printf("  ");
    for (int col = 0; col < 12; col++) {
      int pad = row * 12 + col;

      if (pad == selectedPad) {
        // Selected cell: cyan brackets
        // Trim leading space from label for bracket display
        const char* lbl = _roleLabels[pad];
        Serial.printf(VT_CYAN "[%s]" VT_RESET, lbl);
      } else {
        const char* color;
        switch (_roleMap[pad]) {
          case ROLE_BANK:      color = VT_BLUE;   break;
          case ROLE_SCALE:     color = VT_GREEN;  break;
          case ROLE_ARP:       color = VT_YELLOW; break;
          case ROLE_COLLISION: color = VT_RED;    break;
          default:             color = VT_DIM;    break;
        }
        Serial.printf("%s %s" VT_RESET, color, _roleLabels[pad]);
      }
    }
    Serial.printf(VT_CL "\n");
  }
}

// =================================================================
// drawPool — 3 pool lines + "none" option, with highlighting
// =================================================================

void ToolPadRoles::drawPool() {
  int selectedPad = _gridRow * 12 + _gridCol;
  PadRole currentRole = getRoleForPad((uint8_t)selectedPad);

  // "none" line (poolLine 0)
  {
    bool isSelectedLine = _editing && (_poolLine == 0);
    if (isSelectedLine) {
      Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " ");
      Serial.printf(VT_REVERSE " none " VT_RESET);
    } else if (currentRole.line == 0) {
      // Current pad has no role — highlight "none"
      Serial.printf("    " VT_REVERSE " none " VT_RESET);
    } else {
      Serial.printf("    " VT_DIM "none" VT_RESET);
    }
    Serial.printf(VT_CL "\n");
  }

  // Bank line (poolLine 1)
  {
    bool isSelectedLine = _editing && (_poolLine == 1);
    if (isSelectedLine) {
      Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " " VT_DIM "Bank: " VT_RESET " ");
    } else {
      Serial.printf("    " VT_DIM "Bank: " VT_RESET " ");
    }
    for (uint8_t i = 0; i < POOL_BANK_COUNT; i++) {
      bool isCursor = isSelectedLine && (_poolIdx == i);
      bool isCurrentRole = (currentRole.line == 1 && currentRole.index == i);
      uint8_t owner = findPadWithRole(1, i);
      bool assignedElsewhere = (owner < NUM_KEYS && owner != (uint8_t)selectedPad);

      if (isCursor) {
        Serial.printf(VT_REVERSE VT_BOLD " %s " VT_RESET " ", POOL_BANK_LABELS[i]);
      } else if (isCurrentRole) {
        Serial.printf(VT_BLUE VT_BOLD "%s" VT_RESET " ", POOL_BANK_LABELS[i]);
      } else if (assignedElsewhere) {
        Serial.printf(VT_DIM "%s" VT_RESET " ", POOL_BANK_LABELS[i]);
      } else if (_editing) {
        Serial.printf(VT_BLUE "%s" VT_RESET " ", POOL_BANK_LABELS[i]);
      } else {
        Serial.printf(VT_DIM "%s" VT_RESET " ", POOL_BANK_LABELS[i]);
      }
    }
    Serial.printf(VT_CL "\n");
  }

  // Scale line (poolLine 2)
  {
    bool isSelectedLine = _editing && (_poolLine == 2);
    if (isSelectedLine) {
      Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " " VT_DIM "Scale:" VT_RESET " ");
    } else {
      Serial.printf("    " VT_DIM "Scale:" VT_RESET " ");
    }
    for (uint8_t i = 0; i < POOL_SCALE_COUNT; i++) {
      bool isCursor = isSelectedLine && (_poolIdx == i);
      bool isCurrentRole = (currentRole.line == 2 && currentRole.index == i);
      uint8_t owner = findPadWithRole(2, i);
      bool assignedElsewhere = (owner < NUM_KEYS && owner != (uint8_t)selectedPad);

      if (isCursor) {
        Serial.printf(VT_REVERSE VT_BOLD " %s " VT_RESET " ", POOL_SCALE_LABELS[i]);
      } else if (isCurrentRole) {
        Serial.printf(VT_GREEN VT_BOLD "%s" VT_RESET " ", POOL_SCALE_LABELS[i]);
      } else if (assignedElsewhere) {
        Serial.printf(VT_DIM "%s" VT_RESET " ", POOL_SCALE_LABELS[i]);
      } else if (_editing) {
        Serial.printf(VT_GREEN "%s" VT_RESET " ", POOL_SCALE_LABELS[i]);
      } else {
        Serial.printf(VT_DIM "%s" VT_RESET " ", POOL_SCALE_LABELS[i]);
      }
    }
    Serial.printf(VT_CL "\n");
  }

  // Arp line (poolLine 3)
  {
    bool isSelectedLine = _editing && (_poolLine == 3);
    if (isSelectedLine) {
      Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " " VT_DIM "Arp:  " VT_RESET " ");
    } else {
      Serial.printf("    " VT_DIM "Arp:  " VT_RESET " ");
    }
    for (uint8_t i = 0; i < POOL_ARP_COUNT; i++) {
      bool isCursor = isSelectedLine && (_poolIdx == i);
      bool isCurrentRole = (currentRole.line == 3 && currentRole.index == i);
      uint8_t owner = findPadWithRole(3, i);
      bool assignedElsewhere = (owner < NUM_KEYS && owner != (uint8_t)selectedPad);

      if (isCursor) {
        Serial.printf(VT_REVERSE VT_BOLD " %s " VT_RESET " ", POOL_ARP_LABELS[i]);
      } else if (isCurrentRole) {
        Serial.printf(VT_YELLOW VT_BOLD "%s" VT_RESET " ", POOL_ARP_LABELS[i]);
      } else if (assignedElsewhere) {
        Serial.printf(VT_DIM "%s" VT_RESET " ", POOL_ARP_LABELS[i]);
      } else if (_editing) {
        Serial.printf(VT_YELLOW "%s" VT_RESET " ", POOL_ARP_LABELS[i]);
      } else {
        Serial.printf(VT_DIM "%s" VT_RESET " ", POOL_ARP_LABELS[i]);
      }
    }
    Serial.printf(VT_CL "\n");
  }
}

// =================================================================
// drawDescription — context-sensitive help for highlighted item
// =================================================================

void ToolPadRoles::drawDescription() {
  Serial.printf(VT_CL "\n");

  if (_confirmSteal) {
    Serial.printf(VT_YELLOW "  Already assigned to pad %d. Replace? (y/n)" VT_RESET VT_CL "\n",
                  _stealFromPad + 1);
    return;
  }

  if (_confirmDefaults) {
    Serial.printf(VT_YELLOW "  Reset all roles to defaults? (y/n)" VT_RESET VT_CL "\n");
    return;
  }

  if (_editing) {
    if (_poolLine == 0) {
      Serial.printf(VT_DIM "  Remove role from this pad." VT_RESET VT_CL "\n");
    } else {
      const char* desc = "";
      switch (_poolLine) {
        case 1: desc = (_poolIdx < POOL_BANK_COUNT)  ? BANK_DESCS[_poolIdx]  : ""; break;
        case 2: desc = (_poolIdx < POOL_SCALE_COUNT) ? SCALE_DESCS[_poolIdx] : ""; break;
        case 3: desc = (_poolIdx < POOL_ARP_COUNT)   ? ARP_DESCS[_poolIdx]   : ""; break;
      }
      Serial.printf(VT_DIM "  %s" VT_RESET VT_CL "\n", desc);
    }
  } else {
    // Show current pad info
    int pad = _gridRow * 12 + _gridCol;
    PadRole role = getRoleForPad((uint8_t)pad);
    if (role.line == 0) {
      Serial.printf(VT_DIM "  Pad %d: no role assigned" VT_RESET VT_CL "\n", pad + 1);
    } else {
      const char* desc = "";
      switch (role.line) {
        case 1: desc = (role.index < POOL_BANK_COUNT)  ? BANK_DESCS[role.index]  : ""; break;
        case 2: desc = (role.index < POOL_SCALE_COUNT) ? SCALE_DESCS[role.index] : ""; break;
        case 3: desc = (role.index < POOL_ARP_COUNT)   ? ARP_DESCS[role.index]   : ""; break;
      }
      Serial.printf(VT_DIM "  Pad %d: %s" VT_RESET VT_CL "\n", pad + 1, desc);
    }
  }
}

// =================================================================
// drawHelpLine — bottom help text
// =================================================================

void ToolPadRoles::drawHelpLine() {
  Serial.printf(VT_CL "\n");

  if (_confirmSteal || _confirmDefaults) {
    // No extra help — prompt is in description area
    return;
  }

  if (_editing) {
    Serial.printf(VT_DIM "  [Up/Down] pool line  [Left/Right] cycle  [Enter] assign  [q] cancel" VT_RESET VT_CL "\n");
  } else {
    Serial.printf(VT_DIM "  [Arrows] navigate  [Enter] edit  [Touch] jump  [d] defaults  [q] quit" VT_RESET VT_CL "\n");
  }
}

// =================================================================
// drawScreen — full VT100 redraw
// =================================================================

void ToolPadRoles::drawScreen() {
  _ui->vtFrameStart();
  _ui->drawHeader("PAD ROLES", "Grid + Pool");
  Serial.printf(VT_CL "\n");

  drawGrid();
  Serial.printf(VT_CL "\n");
  drawPool();
  drawDescription();
  drawHelpLine();

  // Legend
  Serial.printf(VT_CL "\n");
  Serial.printf("  " VT_BLUE "Bank" VT_RESET "  "
                VT_GREEN "Scale" VT_RESET "  "
                VT_YELLOW "Arp" VT_RESET "  "
                VT_DIM "-- none" VT_RESET VT_CL "\n");

  _ui->vtFrameEnd();
}

// =================================================================
// run() — main tool loop (blocking)
// =================================================================

void ToolPadRoles::run() {
  if (!_keyboard || !_leds || !_ui) return;

  // Copy live values into working copies
  memcpy(_wkBankPads, _bankPads, NUM_BANKS);
  memcpy(_wkRootPads, _rootPads, 7);
  memcpy(_wkModePads, _modePads, 7);
  _wkChromPad    = *_chromaticPad;
  _wkHoldPad     = *_holdPad;
  _wkPlayStopPad = *_playStopPad;
  if (_octavePads) memcpy(_wkOctavePads, _octavePads, 4);

  // Reset navigation state
  _gridRow = 0;
  _gridCol = 0;
  _editing = false;
  _poolLine = 0;
  _poolIdx = 0;
  _confirmSteal = false;
  _stealFromPad = 0xFF;
  _confirmDefaults = false;

  // Capture baselines for touch detection
  captureBaselines(*_keyboard, _refBaselines);

  _ui->vtClear();
  bool screenDirty = true;

  while (true) {
    _leds->update();
    _keyboard->pollAllSensorData();

    // --- Touch detection (jump to cell) ---
    if (!_confirmSteal && !_confirmDefaults) {
      int detected = detectActiveKey(*_keyboard, _refBaselines);
      if (detected >= 0) {
        uint8_t newRow = (uint8_t)(detected / 12);
        uint8_t newCol = (uint8_t)(detected % 12);
        if (newRow != _gridRow || newCol != _gridCol) {
          if (_editing) {
            // Exit edit mode, discard, jump to new cell
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
          _ui->showSaved();
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

    // --- Steal confirmation sub-mode ---
    if (_confirmSteal) {
      if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
        // Steal: clear the role from the other pad, assign to current
        clearRole(_stealFromPad);
        int pad = _gridRow * 12 + _gridCol;
        clearRole((uint8_t)pad);  // clear current pad's old role if any
        assignRole((uint8_t)pad, _poolLine, _poolIdx);
        if (saveAll()) {
          _ui->showSaved();
          _editing = false;
        }
        _confirmSteal = false;
        screenDirty = true;
      } else if (ev.type != NAV_NONE) {
        // Cancel steal — stay in edit mode
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
        // Cancel editing
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
        // Enter edit mode
        _editing = true;
        int pad = _gridRow * 12 + _gridCol;
        PadRole role = getRoleForPad((uint8_t)pad);
        _poolLine = role.line;
        _poolIdx = role.index;
        // If none (line 0), start on line 0 idx 0
        screenDirty = true;
      }
    } else {
      // --- Pool navigation (edit mode) ---
      if (ev.type == NAV_UP) {
        if (_poolLine == 0) _poolLine = 3;
        else _poolLine--;
        // Clamp poolIdx to new line size
        uint8_t sz = poolLineSize(_poolLine);
        if (sz > 0 && _poolIdx >= sz) _poolIdx = sz - 1;
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        if (_poolLine == 3) _poolLine = 0;
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
          // Assign "none" — remove role
          clearRole((uint8_t)pad);
          if (saveAll()) {
            _ui->showSaved();
            _editing = false;
          }
          screenDirty = true;
        } else {
          // Check if this role is already assigned to another pad
          uint8_t owner = findPadWithRole(_poolLine, _poolIdx);
          if (owner < NUM_KEYS && owner != (uint8_t)pad) {
            // Enter steal confirmation
            _confirmSteal = true;
            _stealFromPad = owner;
          } else {
            // Direct assign
            clearRole((uint8_t)pad);  // remove old role first
            assignRole((uint8_t)pad, _poolLine, _poolIdx);
            if (saveAll()) {
              _ui->showSaved();
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
