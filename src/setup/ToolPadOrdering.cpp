#include "ToolPadOrdering.h"
#include "SetupCommon.h"
#include "SetupUI.h"
#include "InputParser.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../core/KeyboardData.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

// =================================================================
// Constructor
// =================================================================

ToolPadOrdering::ToolPadOrdering()
  : _keyboard(nullptr), _leds(nullptr), _nvs(nullptr),
    _ui(nullptr), _padOrder(nullptr) {}

void ToolPadOrdering::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                             NvsManager* nvs, SetupUI* ui, uint8_t* padOrder) {
  _keyboard = keyboard;
  _leds = leds;
  _nvs = nvs;
  _ui = ui;
  _padOrder = padOrder;
}

// =================================================================
// run() — V2 Pad Ordering Tool (NEW design, not a V1 port)
//
// Touch pads from lowest to highest pitch.
// Assigns rank positions 0-47 (displayed as 1-48).
// No base note question — root is set at runtime via ScaleManager.
// Partial save allowed via [s].
// =================================================================

void ToolPadOrdering::run() {
  if (!_keyboard || !_leds || !_ui || !_padOrder) return;

  enum OrdState {
    ORD_MEASUREMENT,
    ORD_RECAP,
    ORD_SAVE,
    ORD_DONE
  };

  OrdState state = ORD_MEASUREMENT;

  // Capture reference baselines (quick snapshot)
  uint16_t referenceBaselines[NUM_KEYS];
  captureBaselines(*_keyboard, referenceBaselines);

  // Ordering state
  uint8_t orderMap[NUM_KEYS];
  bool    assigned[NUM_KEYS];
  uint8_t assignHistory[NUM_KEYS];  // stack: assignHistory[i] = key assigned at step i
  int     assignedCount = 0;
  int     activeKey = -1;
  int     lastActiveKey = -1;

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
    // MEASUREMENT — touch-to-assign positions
    // =============================================================
    case ORD_MEASUREMENT: {
      _keyboard->pollAllSensorData();

      int detected = detectActiveKey(*_keyboard, referenceBaselines);

      if (detected >= 0) {
        if (detected != lastActiveKey) {
          activeKey = detected;
        }
        lastActiveKey = detected;
      }
      // detected == -1: pad released — keep activeKey for ENTER validation

      // Refresh display
      if (millis() - lastRefresh >= 200) {
        lastRefresh = millis();

        _ui->vtFrameStart();
        char info[32];
        snprintf(info, sizeof(info), "Assigned: %d/%d", assignedCount, NUM_KEYS);
        _ui->drawHeader("PAD ORDERING", info);
        Serial.printf(VT_CL "\n");
        Serial.printf("  === TOUCH PADS FROM LOWEST TO HIGHEST ===" VT_CL "\n");
        Serial.printf(VT_CL "\n");

        bool activeIsDone = (activeKey >= 0) && assigned[activeKey];
        _ui->drawGrid(GRID_ORDERING, 0, nullptr, nullptr, assigned,
                 activeKey, (uint16_t)assignedCount, activeIsDone, orderMap);

        Serial.printf(VT_CL "\n");

        // Detail box
        if (detected == -2) {
          Serial.printf("  +-- " VT_YELLOW "Multiple pads detected" VT_RESET " -----------------+" VT_CL "\n");
          Serial.printf("  |  Lift off and touch ONE pad at a time       |" VT_CL "\n");
          Serial.printf("  +---------------------------------------------+" VT_CL "\n");
          Serial.printf(VT_CL "\n");
        }
        else if (activeKey >= 0) {
          int sensor = activeKey / CHANNELS_PER_SENSOR;
          int channel = activeKey % CHANNELS_PER_SENSOR;
          char sc = 'A' + sensor;

          if (activeIsDone) {
            Serial.printf("  +-- Key %d (%c:Ch%d) " VT_MAGENTA "ALREADY ASSIGNED" VT_RESET " -------+" VT_CL "\n",
                          activeKey, sc, channel);
            Serial.printf("  |  Position: %-3d   (touch another pad)           |" VT_CL "\n",
                          orderMap[activeKey] + 1);
            Serial.printf("  |  Use [r] in recap to redo all                  |" VT_CL "\n");
            Serial.printf("  +---------------------------------------------+" VT_CL "\n");
          } else {
            Serial.printf("  +-- Active: Key %d (Sensor %c, Channel %d) ------+" VT_CL "\n",
                          activeKey, sc, channel);
            Serial.printf("  |  Will be assigned position: %-3d               |" VT_CL "\n",
                          assignedCount + 1);
            Serial.printf("  |  [Enter] to confirm                           |" VT_CL "\n");
            Serial.printf("  +---------------------------------------------+" VT_CL "\n");
          }
        }
        else {
          Serial.printf("  +-- Waiting... --------------------------------+" VT_CL "\n");
          Serial.printf("  |  Touch pad for position %-3d                   |" VT_CL "\n",
                        assignedCount + 1);
          Serial.printf("  +---------------------------------------------+" VT_CL "\n");
          Serial.printf(VT_CL "\n");
        }

        Serial.printf(VT_CL "\n");
        Serial.printf("  Next position: %d/%d" VT_CL "\n", assignedCount + 1, NUM_KEYS);
        Serial.printf(VT_CL "\n");
        Serial.printf("  [Enter] Assign   [u] Undo   [d] Defaults   [s] Save   [q] Abort" VT_CL "\n");
        _ui->vtFrameEnd();
      }

      // Handle input
      if (ev.type == NAV_ENTER && activeKey >= 0) {
        if (assigned[activeKey]) {
          // Already assigned — ignore (use [r] to redo all)
          break;
        }
        assignHistory[assignedCount] = (uint8_t)activeKey;
        orderMap[activeKey] = (uint8_t)assignedCount;
        assigned[activeKey] = true;
        assignedCount++;
        _leds->playValidation();

        // Refresh baselines for unassigned pads (compensate drift)
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
        } else {
          activeKey = -1;
          lastActiveKey = -1;
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
      else if (ev.type == NAV_DEFAULTS) {
        // Reset to linear order 0-47
        _ui->vtClear();
        Serial.printf("  Reset to default order (0-47)? [y/n]\n");
        while (true) {
          NavEvent ce = input.update();
          if (ce.type == NAV_CHAR && (ce.ch == 'y' || ce.ch == 'Y')) {
            for (int i = 0; i < NUM_KEYS; i++) {
              orderMap[i] = (uint8_t)i;
              assigned[i] = true;
              assignHistory[i] = (uint8_t)i;
            }
            assignedCount = NUM_KEYS;
            // Direct NVS write
            NoteMapStore nms;
            nms.magic = EEPROM_MAGIC;
            nms.version = NOTEMAP_VERSION;
            nms.reserved = 0;
            memcpy(nms.noteMap, orderMap, NUM_KEYS);
            Preferences prefs;
            if (prefs.begin(NOTEMAP_NVS_NAMESPACE, false)) {
              prefs.putBytes(NOTEMAP_NVS_KEY, &nms, sizeof(NoteMapStore));
              prefs.end();
            }
            memcpy(_padOrder, orderMap, NUM_KEYS);
            _ui->showSaved();
            _ui->vtClear();
            state = ORD_DONE;
            break;
          }
          if (ce.type == NAV_CHAR && (ce.ch == 'n' || ce.ch == 'N')) {
            _ui->vtClear();
            screenDirty = true;
            lastRefresh = 0;
            break;
          }
          _leds->update();
          delay(5);
        }
      }
      else if (ev.type == NAV_CHAR && (ev.ch == 's' || ev.ch == 'S')) {
        _ui->vtClear();
        state = ORD_RECAP;
        screenDirty = true;
      }
      else if (ev.type == NAV_QUIT) {
        _ui->vtClear();
        state = ORD_DONE;
      }
      break;
    }

    // =============================================================
    // RECAP
    // =============================================================
    case ORD_RECAP: {
      // Count actually assigned (at case scope so ENTER handler can access it)
      int uniqueAssigned = 0;
      for (int i = 0; i < NUM_KEYS; i++) {
        if (assigned[i]) uniqueAssigned++;
      }

      if (screenDirty) {
        screenDirty = false;

        _ui->vtFrameStart();
        _ui->drawHeader("PAD ORDERING", "COMPLETE");
        Serial.printf(VT_CL "\n");
        Serial.printf("  === FINAL ORDERING ===" VT_CL "\n");
        Serial.printf(VT_CL "\n");

        _ui->drawGrid(GRID_ORDERING, 0, nullptr, nullptr, assigned, -1, 0, false, orderMap);

        Serial.printf(VT_CL "\n");
        if (uniqueAssigned == NUM_KEYS) {
          Serial.printf(VT_GREEN "  All %d pads assigned." VT_RESET VT_CL "\n", NUM_KEYS);
        } else {
          Serial.printf(VT_YELLOW "  %d/%d pads assigned (partial)." VT_RESET VT_CL "\n",
                        uniqueAssigned, NUM_KEYS);
        }
        Serial.printf(VT_CL "\n");
        Serial.printf("  [Enter] Save   [r] Redo all   [q] Back to menu" VT_CL "\n");
        _ui->vtFrameEnd();
      }

      if (ev.type == NAV_ENTER) {
        // Partial save warning: explain consequences before proceeding
        if (uniqueAssigned < NUM_KEYS) {
          _ui->vtClear();
          Serial.printf(VT_YELLOW VT_BOLD "  Only %d/%d pads assigned." VT_RESET "\n", uniqueAssigned, NUM_KEYS);
          Serial.printf(VT_CL "\n");
          Serial.printf("  Unassigned pads will get sequential positions\n");
          Serial.printf("  after the last assigned pad.\n");
          Serial.printf(VT_CL "\n");
          Serial.printf("  [Enter] Save anyway   [q] Cancel\n");

          // Wait for confirmation
          while (true) {
            NavEvent ce = input.update();
            if (ce.type == NAV_ENTER) break;  // Confirmed
            if (ce.type == NAV_QUIT) { screenDirty = true; break; }
            _leds->update();
            delay(5);
          }
          if (screenDirty) break;  // User cancelled, back to recap
        }
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
    // SAVE — direct Preferences write (NVS task not running)
    // =============================================================
    case ORD_SAVE: {
      _ui->vtClear();
      Serial.printf(VT_BOLD "  Saving pad ordering..." VT_RESET "\n");

      // Fill unassigned pads with sequential positions after the assigned ones
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

      // Build NoteMapStore
      NoteMapStore nms;
      nms.magic = EEPROM_MAGIC;
      nms.version = NOTEMAP_VERSION;
      nms.reserved = 0;
      memcpy(nms.noteMap, orderMap, NUM_KEYS);

      // Direct NVS write
      Preferences prefs;
      if (prefs.begin(NOTEMAP_NVS_NAMESPACE, false)) {
        prefs.putBytes(NOTEMAP_NVS_KEY, &nms, sizeof(NoteMapStore));
        prefs.end();
        Serial.printf(VT_GREEN "  Pad ordering saved successfully." VT_RESET "\n");
      } else {
        Serial.printf(VT_RED "  Error: could not open NVS namespace!" VT_RESET "\n");
      }

      // Update live padOrder array
      memcpy(_padOrder, orderMap, NUM_KEYS);

      _leds->playValidation();
      Serial.printf("  Returning to menu...\n");
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
