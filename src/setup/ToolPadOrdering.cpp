#include "ToolPadOrdering.h"
#include "SetupCommon.h"
#include "SetupUI.h"
#include "InputParser.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../core/KeyboardData.h"
#include "../managers/NvsManager.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

// =================================================================
// Step indicator — Apollo breadcrumb across every phase
// =================================================================
static const char* ORD_STEPS[] = { "REVIEW", "MEAS", "RECAP" };
static const uint8_t ORD_STEP_COUNT = sizeof(ORD_STEPS) / sizeof(ORD_STEPS[0]);

// =================================================================
// Constructor
// =================================================================

ToolPadOrdering::ToolPadOrdering()
  : _keyboard(nullptr), _leds(nullptr),
    _ui(nullptr), _padOrder(nullptr) {}

void ToolPadOrdering::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                             SetupUI* ui, uint8_t* padOrder) {
  _keyboard = keyboard;
  _leds = leds;
  _ui = ui;
  _padOrder = padOrder;
}

bool ToolPadOrdering::saveOrder(const uint8_t* orderMap) {
  NoteMapStore nms;
  nms.magic = EEPROM_MAGIC;
  nms.version = NOTEMAP_VERSION;
  nms.reserved = 0;
  memcpy(nms.noteMap, orderMap, NUM_KEYS);
  if (!NvsManager::saveBlob(NOTEMAP_NVS_NAMESPACE, NOTEMAP_NVS_KEY, &nms, sizeof(nms)))
    return false;
  memcpy(_padOrder, orderMap, NUM_KEYS);
  return true;
}

// =================================================================
// run() — V2 Pad Ordering (NASA console, touch=auto-assign, review state)
// =================================================================

void ToolPadOrdering::run() {
  if (!_keyboard || !_leds || !_ui || !_padOrder) return;
  Serial.print(ITERM_RESIZE);

  enum OrdState {
    ORD_REVIEW,       // Show current config (NEW)
    ORD_MEASUREMENT,
    ORD_RECAP,
    ORD_SAVE,
    ORD_DONE
  };

  // Check if we have existing NVS data
  bool hasExistingOrder = false;
  uint8_t existingOrder[NUM_KEYS];
  {
    NoteMapStore tmp;
    if (NvsManager::loadBlob(NOTEMAP_NVS_NAMESPACE, NOTEMAP_NVS_KEY,
                              EEPROM_MAGIC, NOTEMAP_VERSION, &tmp, sizeof(tmp))) {
      memcpy(existingOrder, tmp.noteMap, NUM_KEYS);
      for (uint8_t i = 0; i < NUM_KEYS; i++) {
        if (existingOrder[i] >= NUM_KEYS) existingOrder[i] = i;
      }
      hasExistingOrder = true;
    }
  }

  OrdState state = hasExistingOrder ? ORD_REVIEW : ORD_MEASUREMENT;
  bool nvsSaved = hasExistingOrder;

  // Capture reference baselines
  uint16_t referenceBaselines[NUM_KEYS];
  captureBaselines(*_keyboard, referenceBaselines);

  // Ordering state
  uint8_t orderMap[NUM_KEYS];
  bool    assigned[NUM_KEYS];
  uint8_t assignHistory[NUM_KEYS];
  int     assignedCount = 0;
  int     activeKey = -1;
  int     lastActiveKey = -1;
  bool    confirmDefaults = false;
  bool    confirmReset = false;

  memset(orderMap, 0xFF, sizeof(orderMap));
  memset(assigned, 0, sizeof(assigned));

  unsigned long lastRefresh = 0;
  bool screenDirty = true;

  InputParser input;

  _ui->vtClear();

  while (state != ORD_DONE) {
    _leds->update();
    NavEvent ev = input.update();

    switch (state) {

    // =============================================================
    // REVIEW — show current ordering (NEW state)
    // =============================================================
    case ORD_REVIEW: {
      if (screenDirty) {
        screenDirty = false;

        // Build display: show existing order in green
        bool reviewDone[NUM_KEYS];
        for (int i = 0; i < NUM_KEYS; i++) {
          reviewDone[i] = true;  // All pads shown as "done" (green)
        }

        _ui->vtFrameStart();
        _ui->drawConsoleHeader("TOOL 2: PAD ORDERING", nvsSaved);
        _ui->drawFrameEmpty();
        _ui->drawStepIndicator(ORD_STEPS, ORD_STEP_COUNT, 0);  // REVIEW
        _ui->drawFrameEmpty();
        _ui->drawSection("CURRENT ORDERING");
        _ui->drawFrameEmpty();

        _ui->drawCellGrid(GRID_ORDERING, 0, nullptr, nullptr, reviewDone, -1, 0, false, existingOrder);

        _ui->drawFrameEmpty();
        _ui->drawSection("INFO");
        if (confirmDefaults) {
          _ui->drawFrameLine(VT_YELLOW "Reset to identity ordering? Pad 1=rank 1, Pad 2=rank 2, ... (y/n)" VT_RESET);
          _ui->drawFrameEmpty();
          _ui->drawFrameEmpty();
        } else {
          _ui->drawFrameLine(VT_BRIGHT_GREEN "Current pad ordering loaded from NVS." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Each number = rank position (1=lowest pitch, 48=highest)." VT_RESET);
          _ui->drawFrameLine(VT_DIM "This defines which pad plays which note in the scale." VT_RESET);
        }
        _ui->drawFrameEmpty();

        if (confirmDefaults) {
          _ui->drawControlBar(CBAR_CONFIRM_ANY);
        } else {
          _ui->drawControlBar(VT_DIM "[RET] RE-ORDER FROM SCRATCH  [d] DFLT" CBAR_SEP "[q] KEEP CURRENT" VT_RESET);
        }
        _ui->vtFrameEnd();
      }

      // --- Defaults confirmation ---
      if (confirmDefaults) {
        ConfirmResult r = SetupUI::parseConfirm(ev);
        if (r == CONFIRM_YES) {
          for (int i = 0; i < NUM_KEYS; i++) {
            existingOrder[i] = (uint8_t)i;
          }
          if (saveOrder(existingOrder)) {
            nvsSaved = true;
            _ui->flashSaved();
          }
          confirmDefaults = false;
          screenDirty = true;
        } else if (r == CONFIRM_NO) {
          confirmDefaults = false;
          screenDirty = true;
        }
        break;
      }

      if (ev.type == NAV_ENTER) {
        // Start re-ordering
        memset(orderMap, 0xFF, sizeof(orderMap));
        memset(assigned, 0, sizeof(assigned));
        assignedCount = 0;
        activeKey = -1;
        lastActiveKey = -1;
        captureBaselines(*_keyboard, referenceBaselines);
        _ui->vtClear();
        lastRefresh = 0;
        state = ORD_MEASUREMENT;
      }
      else if (ev.type == NAV_DEFAULTS) {
        confirmDefaults = true;
        screenDirty = true;
      }
      else if (ev.type == NAV_QUIT) {
        _ui->vtClear();
        state = ORD_DONE;
      }
      break;
    }

    // =============================================================
    // MEASUREMENT — touch-to-assign (auto-assign, no Enter needed)
    // =============================================================
    case ORD_MEASUREMENT: {
      _keyboard->pollAllSensorData();

      int detected = detectActiveKey(*_keyboard, referenceBaselines);

      if (detected >= 0) {
        if (detected != lastActiveKey) {
          activeKey = detected;

          // --- AUTO-ASSIGN: touch = immediate assignment ---
          if (!assigned[activeKey]) {
            assignHistory[assignedCount] = (uint8_t)activeKey;
            orderMap[activeKey] = (uint8_t)assignedCount;
            assigned[activeKey] = true;
            assignedCount++;
            _leds->playValidation();

            // Refresh baselines for remaining unassigned pads
            _keyboard->pollAllSensorData();
            uint16_t freshBl[NUM_KEYS];
            _keyboard->getBaselineData(freshBl);
            for (int i = 0; i < NUM_KEYS; i++) {
              if (!assigned[i]) referenceBaselines[i] = freshBl[i];
            }

            if (assignedCount >= NUM_KEYS) {

              _ui->vtClear();
              state = ORD_RECAP;
              screenDirty = true;
              break;
            }
          }
        }
        lastActiveKey = detected;
      }

      // Refresh display at interval
      if (millis() - lastRefresh >= 200) {
        lastRefresh = millis();

        bool activeIsDone = (activeKey >= 0) && assigned[activeKey];

        _ui->vtFrameStart();

        char info[32];
        snprintf(info, sizeof(info), "TOOL 2: PAD ORDERING  %d/%d", assignedCount, NUM_KEYS);
        _ui->drawConsoleHeader(info, nvsSaved);
        _ui->drawFrameEmpty();
        _ui->drawStepIndicator(ORD_STEPS, ORD_STEP_COUNT, 1);  // MEAS
        _ui->drawFrameEmpty();

        _ui->drawSection("TOUCH PADS FROM LOWEST TO HIGHEST");
        _ui->drawFrameEmpty();

        _ui->drawCellGrid(GRID_ORDERING, 0, nullptr, nullptr, assigned,
                 activeKey, (uint16_t)assignedCount, activeIsDone, orderMap);

        _ui->drawFrameEmpty();

        // Info section
        _ui->drawSection("INFO");

        if (detected == -2) {
          _ui->drawFrameLine(VT_YELLOW "Multiple pads detected!" VT_RESET VT_DIM " Lift off and touch ONE pad at a time." VT_RESET);
        } else if (activeKey >= 0 && activeIsDone) {
          int sensor = activeKey / CHANNELS_PER_SENSOR;
          int channel = activeKey % CHANNELS_PER_SENSOR;
          _ui->drawFrameLine(VT_MAGENTA "Key %d" VT_RESET VT_DIM " (Sensor %c, Ch%d) -- already assigned at position %d." VT_RESET,
                             activeKey, 'A' + sensor, channel, orderMap[activeKey] + 1);
        } else if (activeKey >= 0) {
          int sensor = activeKey / CHANNELS_PER_SENSOR;
          int channel = activeKey % CHANNELS_PER_SENSOR;
          _ui->drawFrameLine(VT_CYAN "Key %d" VT_RESET VT_DIM " (Sensor %c, Ch%d) -- assigned position %d." VT_RESET,
                             activeKey, 'A' + sensor, channel, assignedCount);
        } else {
          _ui->drawFrameLine(VT_DIM "Touch pad for position %d / %d. Auto-assigns on contact." VT_RESET,
                             assignedCount + 1, NUM_KEYS);
        }

        _ui->drawFrameLine(VT_DIM "Each pad touch assigns the next rank (lowest pitch to highest)." VT_RESET);
        _ui->drawFrameEmpty();

        // Control bar
        if (confirmDefaults) {
          _ui->drawControlBar(VT_DIM "Reset to linear order 0-47? [y/n]" VT_RESET);
        } else if (confirmReset) {
          _ui->drawControlBar(VT_DIM "Clear all assignments and start over? [y/n]" VT_RESET);
        } else {
          _ui->drawControlBar(VT_DIM "[TOUCH] auto-assign" CBAR_SEP "[u] UNDO  [r] RESET  [d] DFLT  [s] SAVE" CBAR_SEP "[q] ABORT" VT_RESET);
        }
        _ui->vtFrameEnd();
      }

      // Handle keyboard input
      if (confirmDefaults) {
        ConfirmResult r = SetupUI::parseConfirm(ev);
        if (r == CONFIRM_YES) {
          for (int i = 0; i < NUM_KEYS; i++) {
            orderMap[i] = (uint8_t)i;
            assigned[i] = true;
            assignHistory[i] = (uint8_t)i;
          }
          assignedCount = NUM_KEYS;
          if (saveOrder(orderMap)) {
            nvsSaved = true;
            _ui->flashSaved();
          }
          _ui->setProgress(-1);
          _ui->vtClear();
          state = ORD_DONE;
          confirmDefaults = false;
        } else if (r == CONFIRM_NO) {
          confirmDefaults = false;
          lastRefresh = 0;
        }
      }
      else if (confirmReset) {
        ConfirmResult r = SetupUI::parseConfirm(ev);
        if (r == CONFIRM_YES) {
          memset(orderMap, 0xFF, sizeof(orderMap));
          memset(assigned, 0, sizeof(assigned));
          assignedCount = 0;
          activeKey = -1;
          lastActiveKey = -1;
          confirmReset = false;
          lastRefresh = 0;
        } else if (r == CONFIRM_NO) {
          confirmReset = false;
          lastRefresh = 0;
        }
      }
      else if (ev.type == NAV_CHAR && (ev.ch == 'u' || ev.ch == 'U') && assignedCount > 0) {
        assignedCount--;
        uint8_t undoneKey = assignHistory[assignedCount];
        assigned[undoneKey] = false;
        orderMap[undoneKey] = 0xFF;
        activeKey = -1;
        lastActiveKey = -1;
      }
      else if (ev.type == NAV_CHAR && (ev.ch == 'r' || ev.ch == 'R') && assignedCount > 0) {
        confirmReset = true;
        lastRefresh = 0;
      }
      else if (ev.type == NAV_DEFAULTS) {
        confirmDefaults = true;
        lastRefresh = 0;
      }
      else if (ev.type == NAV_CHAR && (ev.ch == 's' || ev.ch == 'S')) {
        _ui->setProgress(-1);
        _ui->vtClear();
        state = ORD_RECAP;
        screenDirty = true;
      }
      else if (ev.type == NAV_QUIT) {
        _ui->setProgress(-1);
        _ui->vtClear();
        state = ORD_DONE;
      }
      break;
    }

    // =============================================================
    // RECAP
    // =============================================================
    case ORD_RECAP: {
      int uniqueAssigned = 0;
      for (int i = 0; i < NUM_KEYS; i++) {
        if (assigned[i]) uniqueAssigned++;
      }

      if (screenDirty) {
        screenDirty = false;

        _ui->vtFrameStart();
        _ui->drawConsoleHeader("TOOL 2: PAD ORDERING  COMPLETE", nvsSaved);
        _ui->drawFrameEmpty();
        _ui->drawStepIndicator(ORD_STEPS, ORD_STEP_COUNT, 2);  // RECAP
        _ui->drawFrameEmpty();
        _ui->drawSection("FINAL ORDERING");
        _ui->drawFrameEmpty();

        _ui->drawCellGrid(GRID_ORDERING, 0, nullptr, nullptr, assigned, -1, 0, false, orderMap);

        _ui->drawFrameEmpty();
        _ui->drawSection("INFO");
        if (uniqueAssigned == NUM_KEYS) {
          _ui->drawFrameLine(VT_BRIGHT_GREEN "All %d pads assigned." VT_RESET, NUM_KEYS);
        } else {
          _ui->drawFrameLine(VT_YELLOW "%d/%d pads assigned (partial)." VT_RESET, uniqueAssigned, NUM_KEYS);
          _ui->drawFrameLine(VT_DIM "Unassigned pads get sequential positions after the last assigned pad." VT_RESET);
        }
        _ui->drawFrameEmpty();

        _ui->drawControlBar(VT_DIM "[RET] SAVE  [r] REDO ALL" CBAR_SEP "[q] BACK TO MENU" VT_RESET);
        _ui->vtFrameEnd();
      }

      if (ev.type == NAV_ENTER) {
        state = ORD_SAVE;
      }
      else if (ev.type == NAV_CHAR && (ev.ch == 'r' || ev.ch == 'R')) {
        memset(orderMap, 0xFF, sizeof(orderMap));
        memset(assigned, 0, sizeof(assigned));
        assignedCount = 0;
        activeKey = -1;
        lastActiveKey = -1;
        captureBaselines(*_keyboard, referenceBaselines);
        _ui->vtClear();
        lastRefresh = 0;
        screenDirty = true;
        state = ORD_MEASUREMENT;
      }
      else if (ev.type == NAV_QUIT) {
        _ui->vtClear();
        state = ORD_DONE;
      }
      break;
    }

    // =============================================================
    // SAVE
    // =============================================================
    case ORD_SAVE: {
      // Fill unassigned pads
      uint8_t assignedTotal = 0;
      for (uint8_t i = 0; i < NUM_KEYS; i++) {
        if (orderMap[i] != 0xFF) assignedTotal++;
      }
      if (assignedTotal < NUM_KEYS) {
        uint8_t nextPos = assignedTotal;
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
          if (orderMap[i] == 0xFF) {
            orderMap[i] = nextPos++;
          }
        }
      }

      if (saveOrder(orderMap)) {
        _ui->flashSaved();
      }
      delay(800);
      _ui->vtClear();
      state = ORD_DONE;
      break;
    }

    case ORD_DONE:
      break;

    } // switch

    delay(5);
  } // while
}
