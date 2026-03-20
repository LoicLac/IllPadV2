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
// Label constants
// =================================================================

static const char* BANK_LABELS[] = {
  "BNK 1", "BNK 2", "BNK 3", "BNK 4",
  "BNK 5", "BNK 6", "BNK 7", "BNK 8"
};

static const char* SCALE_LABELS[] = {
  "RootA", "RootB", "RootC", "RootD", "RootE", "RootF", "RootG",
  "ModIo", "ModDo", "ModPh", "ModLy", "ModMx", "ModAe", "ModLo",
  "CHROM"
};

static const char* ARP_LABELS[] = {
  " HOLD", "PL/ST"
};

// =================================================================
// Constructor
// =================================================================

ToolPadRoles::ToolPadRoles()
  : _keyboard(nullptr), _leds(nullptr), _nvs(nullptr), _ui(nullptr),
    _bankPads(nullptr), _rootPads(nullptr), _modePads(nullptr),
    _chromaticPad(nullptr), _holdPad(nullptr), _playStopPad(nullptr),
    _wkChromPad(0xFF), _wkHoldPad(0xFF), _wkPlayStopPad(0xFF) {
  memset(_wkBankPads, 0xFF, sizeof(_wkBankPads));
  memset(_wkRootPads, 0xFF, sizeof(_wkRootPads));
  memset(_wkModePads, 0xFF, sizeof(_wkModePads));
}

void ToolPadRoles::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                          NvsManager* nvs, SetupUI* ui,
                          uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
                          uint8_t& chromaticPad, uint8_t& holdPad, uint8_t& playStopPad) {
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
}

// =================================================================
// buildRoleMap() — scan all assignments, populate _roleMap + _roleLabels
// =================================================================

void ToolPadRoles::buildRoleMap() {
  // Clear
  memset(_roleMap, ROLE_NONE, NUM_KEYS);
  for (int i = 0; i < NUM_KEYS; i++) {
    memcpy(_roleLabels[i], " --- ", 6);
  }

  // Hit count for collision detection
  uint8_t hitCount[NUM_KEYS];
  memset(hitCount, 0, sizeof(hitCount));

  // Bank pads
  for (int i = 0; i < NUM_BANKS; i++) {
    uint8_t pad = _wkBankPads[i];
    if (pad < NUM_KEYS) {
      hitCount[pad]++;
      _roleMap[pad] = ROLE_BANK;
      memcpy(_roleLabels[pad], BANK_LABELS[i], 6);
    }
  }

  // Root pads
  for (int i = 0; i < 7; i++) {
    uint8_t pad = _wkRootPads[i];
    if (pad < NUM_KEYS) {
      hitCount[pad]++;
      _roleMap[pad] = ROLE_SCALE;
      memcpy(_roleLabels[pad], SCALE_LABELS[i], 6);
    }
  }

  // Mode pads
  for (int i = 0; i < 7; i++) {
    uint8_t pad = _wkModePads[i];
    if (pad < NUM_KEYS) {
      hitCount[pad]++;
      _roleMap[pad] = ROLE_SCALE;
      memcpy(_roleLabels[pad], SCALE_LABELS[7 + i], 6);
    }
  }

  // Chromatic pad
  if (_wkChromPad < NUM_KEYS) {
    hitCount[_wkChromPad]++;
    _roleMap[_wkChromPad] = ROLE_SCALE;
    memcpy(_roleLabels[_wkChromPad], SCALE_LABELS[14], 6);
  }

  // Hold pad
  if (_wkHoldPad < NUM_KEYS) {
    hitCount[_wkHoldPad]++;
    _roleMap[_wkHoldPad] = ROLE_ARP;
    memcpy(_roleLabels[_wkHoldPad], ARP_LABELS[0], 6);
  }

  // Play/Stop pad
  if (_wkPlayStopPad < NUM_KEYS) {
    hitCount[_wkPlayStopPad]++;
    _roleMap[_wkPlayStopPad] = ROLE_ARP;
    memcpy(_roleLabels[_wkPlayStopPad], ARP_LABELS[1], 6);
  }

  // Mark collisions
  for (int i = 0; i < NUM_KEYS; i++) {
    if (hitCount[i] > 1) {
      _roleMap[i] = ROLE_COLLISION;
      memcpy(_roleLabels[i], "!!XX!", 6);
    }
  }
}

int ToolPadRoles::countCollisions() const {
  int count = 0;
  for (int i = 0; i < NUM_KEYS; i++) {
    if (_roleMap[i] == ROLE_COLLISION) count++;
  }
  return count;
}

// =================================================================
// run() — sub-menu
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

  _ui->vtClear();
  bool screenDirty = true;
  unsigned long lastRefresh = 0;

  while (true) {
    _leds->update();
    char input = _ui->readInput();

    switch (input) {
      case '1':
        runBankPads();
        _ui->vtClear();
        screenDirty = true;
        break;

      case '2':
        runScalePads();
        _ui->vtClear();
        screenDirty = true;
        break;

      case '3':
        runArpPads();
        _ui->vtClear();
        screenDirty = true;
        break;

      case '4':
        viewAll();
        _ui->vtClear();
        screenDirty = true;
        break;

      case 's':
      case 'S':
        if (saveAll()) {
          _ui->vtClear();
          Serial.printf(VT_GREEN "  Pad roles saved successfully." VT_RESET "\n");
          Serial.printf("  Returning to menu...\n");
          delay(800);
          _ui->vtClear();
          return;
        }
        screenDirty = true;
        break;

      case 'q':
      case 'Q':
        _ui->vtClear();
        return;

      default:
        break;
    }

    if (screenDirty || millis() - lastRefresh >= 500) {
      buildRoleMap();
      int collisions = countCollisions();

      _ui->vtFrameStart();
      _ui->drawHeader("PAD ROLES", "Sub-menu");
      Serial.printf(VT_CL "\n");

      _ui->drawRolesGrid(_roleMap, _roleLabels, -1);

      Serial.printf(VT_CL "\n");
      Serial.printf("   [1] Bank Pads (8)" VT_CL "\n");
      Serial.printf("   [2] Scale Pads (15)  -- 7 root + 7 mode + 1 chromatic" VT_CL "\n");
      Serial.printf("   [3] Arp Pads (2)     -- 1 HOLD + 1 play/stop" VT_CL "\n");
      Serial.printf("   [4] View All / Check Collisions" VT_CL "\n");
      Serial.printf(VT_CL "\n");

      if (collisions > 0) {
        Serial.printf(VT_RED "  !! %d COLLISION(S) DETECTED !!" VT_RESET VT_CL "\n", collisions);
      }

      Serial.printf("   [s] Save all   [q] Back to main menu" VT_CL "\n");
      _ui->vtFrameEnd();

      screenDirty = false;
      lastRefresh = millis();
    }

    delay(5);
  }
}

// =================================================================
// assignSection() — reusable touch-to-assign loop
// =================================================================

void ToolPadRoles::assignSection(const char* sectionTitle,
                                  const char* const* labels,
                                  uint8_t* targets, uint8_t count) {
  // Capture reference baselines
  uint16_t referenceBaselines[NUM_KEYS];
  captureBaselines(*_keyboard, referenceBaselines);

  int currentIndex = 0;
  int activeKey = -1;
  int lastActiveKey = -1;
  unsigned long lastRefresh = 0;

  _ui->vtClear();

  while (currentIndex < count) {
    _leds->update();
    _keyboard->pollAllSensorData();

    int detected = detectActiveKey(*_keyboard, referenceBaselines);

    if (detected >= 0) {
      if (detected != lastActiveKey) {
        activeKey = detected;
      }
      lastActiveKey = detected;
    }

    // Refresh display
    if (millis() - lastRefresh >= 200) {
      lastRefresh = millis();

      // Rebuild role map with current working state
      buildRoleMap();

      _ui->vtFrameStart();
      char info[32];
      snprintf(info, sizeof(info), "%d/%d assigned", currentIndex, count);
      _ui->drawHeader(sectionTitle, info);
      Serial.printf(VT_CL "\n");

      _ui->drawRolesGrid(_roleMap, _roleLabels, activeKey);
      Serial.printf(VT_CL "\n");

      // Detail box
      if (detected == -2) {
        Serial.printf("  " VT_YELLOW "Multiple pads detected — touch ONE pad at a time" VT_RESET VT_CL "\n");
        Serial.printf(VT_CL "\n");
      } else if (activeKey >= 0) {
        int sensor = activeKey / CHANNELS_PER_SENSOR;
        int channel = activeKey % CHANNELS_PER_SENSOR;
        Serial.printf("  Touch pad for: " VT_BOLD "%s" VT_RESET "  (%d/%d)" VT_CL "\n",
                      labels[currentIndex], currentIndex + 1, count);
        Serial.printf("  Active: Key %d (Sensor %c, Ch %d)" VT_CL "\n",
                      activeKey, 'A' + sensor, channel);

        // Warn if this pad already has a role
        if (_roleMap[activeKey] != ROLE_NONE) {
          Serial.printf(VT_RED "  WARNING: This pad already has a role!" VT_RESET VT_CL "\n");
        }

        Serial.printf("  [ENTER/BTN] Confirm" VT_CL "\n");
      } else {
        Serial.printf("  Touch pad for: " VT_BOLD "%s" VT_RESET "  (%d/%d)" VT_CL "\n",
                      labels[currentIndex], currentIndex + 1, count);
        Serial.printf("  Waiting for touch..." VT_CL "\n");
      }

      Serial.printf(VT_CL "\n");
      Serial.printf("  [ENTER/BTN] Assign   [n] Skip   [u] Undo   [q] Back" VT_CL "\n");
      _ui->vtFrameEnd();
    }

    // Handle input
    char input = _ui->readInput();

    if ((input == '\r' || input == '\n') && activeKey >= 0) {
      targets[currentIndex] = (uint8_t)activeKey;
      _leds->playValidation();

      // Refresh baselines only for pads not yet assigned (compensate drift)
      _keyboard->pollAllSensorData();
      uint16_t freshBl[NUM_KEYS];
      _keyboard->getBaselineData(freshBl);
      for (int i = 0; i < NUM_KEYS; i++) {
        // Skip pads already assigned in this section
        bool alreadyUsed = false;
        for (int j = 0; j <= currentIndex; j++) {
          if (targets[j] == i) { alreadyUsed = true; break; }
        }
        if (!alreadyUsed) referenceBaselines[i] = freshBl[i];
      }

      currentIndex++;
      activeKey = -1;
      lastActiveKey = -1;
    }
    else if (input == 'n' || input == 'N') {
      // Skip — keep existing value, advance
      currentIndex++;
      activeKey = -1;
      lastActiveKey = -1;
    }
    else if ((input == 'u' || input == 'U') && currentIndex > 0) {
      currentIndex--;
      targets[currentIndex] = 0xFF;  // Reset to unassigned
      activeKey = -1;
      lastActiveKey = -1;
    }
    else if (input == 'q' || input == 'Q') {
      return;  // Back to sub-menu (partial edits kept in working copy)
    }

    delay(5);
  }

  // All items assigned
  _ui->vtClear();
  Serial.printf(VT_GREEN "  %s — all %d pads assigned." VT_RESET "\n", sectionTitle, count);
  delay(600);
}

// =================================================================
// Section runners
// =================================================================

void ToolPadRoles::runBankPads() {
  assignSection("BANK PADS", BANK_LABELS, _wkBankPads, NUM_BANKS);
}

void ToolPadRoles::runScalePads() {
  // Build flat array of 15 targets from root[7] + mode[7] + chrom
  uint8_t targets[15];
  memcpy(targets, _wkRootPads, 7);
  memcpy(targets + 7, _wkModePads, 7);
  targets[14] = _wkChromPad;

  assignSection("SCALE PADS", SCALE_LABELS, targets, 15);

  // Copy back
  memcpy(_wkRootPads, targets, 7);
  memcpy(_wkModePads, targets + 7, 7);
  _wkChromPad = targets[14];
}

void ToolPadRoles::runArpPads() {
  uint8_t targets[2] = { _wkHoldPad, _wkPlayStopPad };

  assignSection("ARP PADS", ARP_LABELS, targets, 2);

  _wkHoldPad     = targets[0];
  _wkPlayStopPad = targets[1];
}

// =================================================================
// viewAll() — display all assignments + collision report
// =================================================================

void ToolPadRoles::viewAll() {
  buildRoleMap();
  int collisions = countCollisions();

  _ui->vtClear();
  _ui->vtFrameStart();
  _ui->drawHeader("PAD ROLES", "VIEW ALL");
  Serial.printf(VT_CL "\n");

  _ui->drawRolesGrid(_roleMap, _roleLabels, -1);
  Serial.printf(VT_CL "\n");

  // Text list of all assignments
  Serial.printf("  " VT_BOLD "Bank pads:" VT_RESET "  ");
  for (int i = 0; i < NUM_BANKS; i++) {
    if (_wkBankPads[i] < NUM_KEYS)
      Serial.printf("B%d=%d  ", i + 1, _wkBankPads[i]);
    else
      Serial.printf("B%d=--  ", i + 1);
  }
  Serial.printf(VT_CL "\n");

  Serial.printf("  " VT_BOLD "Root pads:" VT_RESET "  ");
  static const char rootNames[] = "ABCDEFG";
  for (int i = 0; i < 7; i++) {
    if (_wkRootPads[i] < NUM_KEYS)
      Serial.printf("%c=%d  ", rootNames[i], _wkRootPads[i]);
    else
      Serial.printf("%c=--  ", rootNames[i]);
  }
  Serial.printf(VT_CL "\n");

  Serial.printf("  " VT_BOLD "Mode pads:" VT_RESET "  ");
  static const char* modeNames[] = {"Io","Do","Ph","Ly","Mx","Ae","Lo"};
  for (int i = 0; i < 7; i++) {
    if (_wkModePads[i] < NUM_KEYS)
      Serial.printf("%s=%d  ", modeNames[i], _wkModePads[i]);
    else
      Serial.printf("%s=--  ", modeNames[i]);
  }
  Serial.printf(VT_CL "\n");

  Serial.printf("  " VT_BOLD "Chrom pad:" VT_RESET "  ");
  if (_wkChromPad < NUM_KEYS) Serial.printf("%d", _wkChromPad);
  else Serial.printf("--");
  Serial.printf(VT_CL "\n");

  Serial.printf("  " VT_BOLD "Hold pad:" VT_RESET "   ");
  if (_wkHoldPad < NUM_KEYS) Serial.printf("%d", _wkHoldPad);
  else Serial.printf("--");
  Serial.printf(VT_CL "\n");

  Serial.printf("  " VT_BOLD "PL/ST pad:" VT_RESET "  ");
  if (_wkPlayStopPad < NUM_KEYS) Serial.printf("%d", _wkPlayStopPad);
  else Serial.printf("--");
  Serial.printf(VT_CL "\n");

  Serial.printf(VT_CL "\n");

  if (collisions > 0) {
    Serial.printf(VT_RED "  !! %d COLLISION(S) DETECTED — cannot save !!" VT_RESET VT_CL "\n", collisions);
  } else {
    Serial.printf(VT_GREEN "  No collisions. Ready to save." VT_RESET VT_CL "\n");
  }

  Serial.printf(VT_CL "\n");
  Serial.printf("  Press any key to return..." VT_CL "\n");
  _ui->vtFrameEnd();

  // Wait for keypress
  while (true) {
    _leds->update();
    if (_ui->readInput()) break;
    delay(10);
  }
}

// =================================================================
// saveAll() — direct NVS write (setup mode, NVS task not running)
// =================================================================

bool ToolPadRoles::saveAll() {
  buildRoleMap();
  if (countCollisions() > 0) {
    _ui->printError("Cannot save: collisions exist! Use [4] to view.");
    delay(1500);
    return false;
  }

  Preferences prefs;

  // Bank pads
  BankPadStore bps;
  bps.magic    = EEPROM_MAGIC;
  bps.version  = BANKPAD_VERSION;
  bps.reserved = 0;
  memcpy(bps.bankPads, _wkBankPads, NUM_BANKS);
  if (prefs.begin(BANKPAD_NVS_NAMESPACE, false)) {
    prefs.putBytes(BANKPAD_NVS_KEY, &bps, sizeof(BankPadStore));
    prefs.end();
  }

  // Scale pads
  if (prefs.begin(SCALE_PAD_NVS_NAMESPACE, false)) {
    prefs.putBytes(SCALE_PAD_ROOT_KEY, _wkRootPads, 7);
    prefs.putBytes(SCALE_PAD_MODE_KEY, _wkModePads, 7);
    prefs.putUChar(SCALE_PAD_CHROM_KEY, _wkChromPad);
    prefs.end();
  }

  // Arp pads
  if (prefs.begin(ARP_PAD_NVS_NAMESPACE, false)) {
    prefs.putUChar(ARP_PAD_HOLD_KEY, _wkHoldPad);
    prefs.putUChar(ARP_PAD_PS_KEY, _wkPlayStopPad);
    prefs.end();
  }

  // Update live values
  memcpy(_bankPads, _wkBankPads, NUM_BANKS);
  memcpy(_rootPads, _wkRootPads, 7);
  memcpy(_modePads, _wkModePads, 7);
  *_chromaticPad = _wkChromPad;
  *_holdPad      = _wkHoldPad;
  *_playStopPad  = _wkPlayStopPad;

  _leds->playValidation();
  return true;
}
