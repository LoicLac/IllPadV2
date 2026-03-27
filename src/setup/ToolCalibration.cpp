#include "ToolCalibration.h"
#include "SetupCommon.h"
#include "SetupUI.h"
#include "InputParser.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include <Arduino.h>

// =================================================================
// Sensitivity Presets (exact V1 values)
// =================================================================
// Max safe target: MPR121_VDD_TL << 2 = 720 (USL register 189, well below 201)
static const uint16_t sensitivityTargets[] = { 520, 620, 720 };
static const char* sensitivityNames[] = {
  "Conservative", "Standard", "Max Range"
};
static const int NUM_SENSITIVITY_LEVELS = sizeof(sensitivityTargets) / sizeof(uint16_t);

// =================================================================
// Constructor
// =================================================================

ToolCalibration::ToolCalibration()
  : _keyboard(nullptr), _leds(nullptr), _ui(nullptr) {}

void ToolCalibration::begin(CapacitiveKeyboard* keyboard, LedController* leds, SetupUI* ui) {
  _keyboard = keyboard;
  _leds = leds;
  _ui = ui;
}

// =================================================================
// Stabilization Phase — exact port from V1 runStabilizationPhase()
// Runs autoconfig, then shows live baseline grid.
// Returns true if user proceeded (ENTER), false if aborted (q).
// =================================================================
static bool runStabilizationPhase(
  CapacitiveKeyboard& keyboard,
  LedController& leds,
  SetupUI& ui,
  uint16_t targetBaseline,
  const char* sensitivityLabel,
  uint16_t referenceBaselines[],
  const char* title
) {
  // Run autoconfig
  ui.vtClear();
  Serial.printf("  Sensitivity: %s (target: %u)" VT_CL "\n", sensitivityLabel, targetBaseline);
  Serial.printf(VT_CL "\n");
  Serial.printf("  Running MPR121 autoconfiguration..." VT_CL "\n");
  keyboard.runAutoconfiguration(targetBaseline);
  delay(CAL_AUTOCONFIG_COUNTDOWN_MS);
  Serial.printf("  Done." VT_CL "\n");
  delay(500);
  ui.vtClear();

  // Stabilization loop — show live baseline grid
  unsigned long lastRefresh = 0;
  InputParser stab;
  while (true) {
    leds.update();
    keyboard.pollAllSensorData();
    NavEvent ev = stab.update();

    if (ev.type == NAV_QUIT) return false;

    if (millis() - lastRefresh >= 250) {
      lastRefresh = millis();

      uint16_t currentBaselines[NUM_KEYS];
      keyboard.getBaselineData(currentBaselines);

      uint16_t tol = targetBaseline / 10;
      int stableCount = 0;
      for (int i = 0; i < NUM_KEYS; i++) {
        int d = (int)currentBaselines[i] - (int)targetBaseline;
        if (d < 0) d = -d;
        if ((uint16_t)d <= tol) stableCount++;
      }

      ui.vtFrameStart();
      char info[40];
      snprintf(info, sizeof(info), "Sensitivity: %s", sensitivityLabel);
      ui.drawHeader(title, info);
      Serial.printf(VT_CL "\n");
      Serial.printf("  === BASELINE STABILIZATION === (target: %u)" VT_CL "\n", targetBaseline);
      Serial.printf(VT_CL "\n");

      ui.drawGrid(GRID_BASELINE, targetBaseline, currentBaselines, nullptr, nullptr, -1, 0, false, nullptr);

      Serial.printf(VT_CL "\n");
      if (stableCount == NUM_KEYS) {
        Serial.printf("  Status: " VT_GREEN "%d/%d stable" VT_RESET VT_CL "\n", stableCount, NUM_KEYS);
      } else {
        Serial.printf("  Status: " VT_YELLOW "Settling... %d/%d" VT_RESET VT_CL "\n", stableCount, NUM_KEYS);
      }
      Serial.printf(VT_CL "\n");
      Serial.printf("  Press Enter to continue  [q] Abort" VT_CL "\n");
      ui.vtFrameEnd();
    }

    if (ev.type == NAV_ENTER) {
      // Capture reference baselines
      keyboard.pollAllSensorData();
      delay(50);
      keyboard.pollAllSensorData();
      keyboard.getBaselineData(referenceBaselines);
      return true;
    }
    delay(5);
  }
}

// =================================================================
// run() — Main entry point, exact V1 Tool 1 flow
// =================================================================

void ToolCalibration::run() {
  if (!_keyboard || !_leds || !_ui) return;

  // Top-level states (same as V1 STATE_TOOL1_*)
  enum CalState {
    CAL_SENSITIVITY,
    CAL_MEASUREMENT,
    CAL_RECAP,
    CAL_SAVE,
    CAL_DONE
  };

  CalState state = CAL_SENSITIVITY;

  // Shared state
  uint16_t referenceBaselines[NUM_KEYS];
  unsigned long lastRefresh = 0;
  bool screenDirty = true;

  // Tool 1 state
  int sensitivityIndex = 0;
  uint16_t measuredDeltas[NUM_KEYS];
  bool     calibrated[NUM_KEYS];
  uint16_t currentMaxDelta = 0;
  int      activeKey = -1;
  int      lastActiveKey = -1;

  memset(calibrated, 0, sizeof(calibrated));
  memset(measuredDeltas, 0, sizeof(measuredDeltas));

  InputParser input;

  _ui->vtClear();

  while (state != CAL_DONE) {
    _leds->update();
    NavEvent ev = input.update();

    switch (state) {

    // =============================================================
    // SENSITIVITY SELECTION (exact V1 lines 531-583)
    // =============================================================
    case CAL_SENSITIVITY: {
      if (ev.type == NAV_CHAR && ev.ch >= '1' && ev.ch <= '3') {
        sensitivityIndex = ev.ch - '1';
        screenDirty = true;
      }
      else if (ev.type == NAV_ENTER) {
        // Run stabilization phase (autoconfig + live grid + baseline capture)
        if (runStabilizationPhase(*_keyboard, *_leds, *_ui,
              sensitivityTargets[sensitivityIndex],
              sensitivityNames[sensitivityIndex],
              referenceBaselines, "CALIBRATION")) {
          // Disable auto-reconfiguration during measurement to prevent
          // ARE from disrupting baselines while user presses pads
          _keyboard->setAutoReconfigEnabled(false);
          activeKey = -1;
          lastActiveKey = -1;
          currentMaxDelta = 0;
          lastRefresh = 0;
          _ui->vtClear();
          state = CAL_MEASUREMENT;
        } else {
          // User aborted — return to setup menu
          _ui->vtClear();
          state = CAL_DONE;
        }
        break;
      }
      else if (ev.type == NAV_QUIT) {
        _ui->vtClear();
        state = CAL_DONE;
        break;
      }

      if (screenDirty || millis() - lastRefresh >= 500) {
        lastRefresh = millis();
        screenDirty = false;
        _ui->vtFrameStart();
        Serial.printf(VT_BOLD "========================================================" VT_RESET VT_CL "\n");
        Serial.printf(VT_BOLD "  ILLPAD48 -- PRESSURE CALIBRATION" VT_RESET VT_CL "\n");
        Serial.printf(VT_BOLD "========================================================" VT_RESET VT_CL "\n");
        Serial.printf(VT_CL "\n");
        Serial.printf("  Select sensitivity preset:" VT_CL "\n");
        Serial.printf(VT_CL "\n");
        for (int i = 0; i < NUM_SENSITIVITY_LEVELS; i++) {
          if (i == sensitivityIndex) {
            Serial.printf(VT_REVERSE "   [%d] %-16s (target: %u)" VT_RESET VT_CL "\n",
                          i + 1, sensitivityNames[i], sensitivityTargets[i]);
          } else {
            Serial.printf("   [%d] %-16s (target: %u)" VT_CL "\n",
                          i + 1, sensitivityNames[i], sensitivityTargets[i]);
          }
        }
        Serial.printf(VT_CL "\n");
        Serial.printf("  Type 1-3 to select, Enter to confirm  [q] Back" VT_CL "\n");
        _ui->vtFrameEnd();
      }
      break;
    }

    // =============================================================
    // TOUCH-TO-SELECT MEASUREMENT (exact V1 lines 588-737)
    // =============================================================
    case CAL_MEASUREMENT: {
      _keyboard->pollAllSensorData();

      int detected = detectActiveKey(*_keyboard, referenceBaselines);

      if (detected >= 0) {
        if (detected != lastActiveKey) {
          activeKey = detected;
          currentMaxDelta = 0;
        }
        uint16_t f = _keyboard->getFilteredData(activeKey);
        uint16_t delta = (referenceBaselines[activeKey] > f)
                       ? (referenceBaselines[activeKey] - f) : 0;
        if (delta > currentMaxDelta) currentMaxDelta = delta;
        lastActiveKey = detected;
      }

      // detected == -1: pad released — keep activeKey + currentMaxDelta
      // so user can lift finger then press ENTER/button to validate

      // Refresh display
      if (millis() - lastRefresh >= 200) {
        lastRefresh = millis();

        int doneCount = 0;
        for (int i = 0; i < NUM_KEYS; i++) {
          if (calibrated[i]) doneCount++;
        }

        _ui->vtFrameStart();
        char info[32];
        snprintf(info, sizeof(info), "Done: %d/%d", doneCount, NUM_KEYS);
        _ui->drawHeader("CALIBRATION", info);
        Serial.printf(VT_CL "\n");
        Serial.printf("  === PRESS EACH PAD WITH MAXIMUM FORCE ===" VT_CL "\n");
        Serial.printf(VT_CL "\n");

        bool activeIsDone = (activeKey >= 0) && calibrated[activeKey];
        _ui->drawGrid(GRID_MEASUREMENT, 0, referenceBaselines, measuredDeltas, calibrated,
                 activeKey, currentMaxDelta, activeIsDone, nullptr);

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
          uint16_t f = _keyboard->getFilteredData(activeKey);
          uint16_t bl = referenceBaselines[activeKey];
          uint16_t delta = (bl > f) ? (bl - f) : 0;

          if (activeIsDone) {
            Serial.printf("  +-- Key %d (%c:Ch%d) " VT_MAGENTA "ALREADY CALIBRATED" VT_RESET " -----+" VT_CL "\n",
                          activeKey, sc, channel);
            Serial.printf("  |  Previous: %-5u   Current delta: %-5u       |" VT_CL "\n",
                          measuredDeltas[activeKey], delta);
            Serial.printf("  |  [Enter] Overwrite   or touch another         |" VT_CL "\n");
            Serial.printf("  +---------------------------------------------+" VT_CL "\n");
          } else {
            Serial.printf("  +-- Active: Key %d (Sensor %c, Channel %d) ------+" VT_CL "\n",
                          activeKey, sc, channel);
            Serial.printf("  |  Baseline: %-5u  Filtered: %-5u  Delta: %-5u|" VT_CL "\n",
                          bl, f, delta);
            if (currentMaxDelta > 0 && currentMaxDelta < CAL_PRESSURE_MIN_DELTA_TO_VALIDATE) {
              Serial.printf("  |  Max delta: " VT_YELLOW "%-5u" VT_RESET "  (low -- press harder)      |" VT_CL "\n",
                            currentMaxDelta);
            } else {
              Serial.printf("  |  Max delta: %-5u                             |" VT_CL "\n",
                            currentMaxDelta);
            }
            Serial.printf("  +---------------------------------------------+" VT_CL "\n");
          }
        }
        else {
          Serial.printf("  +-- Waiting... --------------------------------+" VT_CL "\n");
          Serial.printf("  |  Press any uncalibrated pad with MAX force     |" VT_CL "\n");
          Serial.printf("  +---------------------------------------------+" VT_CL "\n");
          Serial.printf(VT_CL "\n");
        }

        Serial.printf(VT_CL "\n");
        CalStats st = computeStats(measuredDeltas, calibrated);
        if (st.count > 0) {
          Serial.printf("  Progress: %d/%d   Min: %u  Max: %u  Avg: %u" VT_CL "\n",
                        st.count, NUM_KEYS, st.minVal, st.maxVal, st.avgVal);
        } else {
          Serial.printf("  Progress: 0/%d" VT_CL "\n", NUM_KEYS);
        }
        Serial.printf(VT_CL "\n");
        Serial.printf("  [Enter] Validate   [s] Save   [q] Abort" VT_CL "\n");
        _ui->vtFrameEnd();
      }

      // Handle input
      if (ev.type == NAV_ENTER && activeKey >= 0) {
        measuredDeltas[activeKey] = currentMaxDelta;
        calibrated[activeKey] = true;
        _keyboard->setCalibrationMaxDelta(activeKey, currentMaxDelta);
        _leds->playValidation();

        // Refresh reference baselines for uncalibrated pads to compensate
        // for drift during long calibration sessions
        _keyboard->pollAllSensorData();
        uint16_t freshBl[NUM_KEYS];
        _keyboard->getBaselineData(freshBl);
        for (int i = 0; i < NUM_KEYS; i++) {
          if (!calibrated[i]) referenceBaselines[i] = freshBl[i];
        }

        int doneCount = 0;
        for (int i = 0; i < NUM_KEYS; i++) {
          if (calibrated[i]) doneCount++;
        }
        if (doneCount == NUM_KEYS) {
          _keyboard->setAutoReconfigEnabled(true);
          _ui->vtClear();
          state = CAL_RECAP;
          screenDirty = true;
        } else {
          activeKey = -1;
          lastActiveKey = -1;
          currentMaxDelta = 0;
        }
      }
      else if (ev.type == NAV_CHAR && (ev.ch == 's' || ev.ch == 'S')) {
        _keyboard->setAutoReconfigEnabled(true);
        _ui->vtClear();
        state = CAL_RECAP;
        screenDirty = true;
      }
      else if (ev.type == NAV_QUIT) {
        _keyboard->setAutoReconfigEnabled(true);
        _ui->vtClear();
        state = CAL_DONE;
      }
      break;
    }

    // =============================================================
    // RECAP (exact V1 lines 742-790)
    // =============================================================
    case CAL_RECAP: {
      if (screenDirty) {
        screenDirty = false;
        _ui->vtFrameStart();
        _ui->drawHeader("CALIBRATION", "COMPLETE");
        Serial.printf(VT_CL "\n");
        Serial.printf("  === FINAL RESULTS ===   Sensitivity: %s (%u)" VT_CL "\n",
                      sensitivityNames[sensitivityIndex], sensitivityTargets[sensitivityIndex]);
        Serial.printf(VT_CL "\n");

        _ui->drawGrid(GRID_MEASUREMENT, 0, referenceBaselines, measuredDeltas, calibrated, -1, 0, false, nullptr);

        Serial.printf(VT_CL "\n");
        CalStats st = computeStats(measuredDeltas, calibrated);
        if (st.count > 0) {
          Serial.printf("  Min: %u   Max: %u   Avg: %u   ", st.minVal, st.maxVal, st.avgVal);
          if (st.warnings > 0) {
            Serial.printf(VT_RED "Warnings: %d keys below %u" VT_RESET, st.warnings, CAL_PRESSURE_MIN_DELTA_TO_VALIDATE);
          } else {
            Serial.printf(VT_GREEN "All keys OK" VT_RESET);
          }
          Serial.printf(VT_CL "\n");
        } else {
          Serial.printf(VT_RED "  No keys calibrated." VT_RESET VT_CL "\n");
        }
        Serial.printf(VT_CL "\n");
        if (st.count > 0) {
          Serial.printf("  [Enter] Save   [r] Redo all   [q] Back to menu" VT_CL "\n");
        } else {
          Serial.printf("  [r] Redo all   [q] Back to menu" VT_CL "\n");
        }
        _ui->vtFrameEnd();
      }

      if (ev.type == NAV_ENTER) {
        // Only allow save if at least one pad was calibrated
        int calCount = 0;
        for (int i = 0; i < NUM_KEYS; i++) { if (calibrated[i]) calCount++; }
        if (calCount > 0) state = CAL_SAVE;
      }
      else if (ev.type == NAV_CHAR && (ev.ch == 'r' || ev.ch == 'R')) {
        memset(calibrated, 0, sizeof(calibrated));
        memset(measuredDeltas, 0, sizeof(measuredDeltas));
        sensitivityIndex = 0;
        _ui->vtClear();
        lastRefresh = 0;
        screenDirty = true;
        state = CAL_SENSITIVITY;
      }
      else if (ev.type == NAV_QUIT) {
        _ui->vtClear();
        state = CAL_DONE;
      }
      break;
    }

    // =============================================================
    // SAVE (exact V1 lines 795-808)
    // =============================================================
    case CAL_SAVE: {
      _ui->vtClear();
      Serial.printf(VT_BOLD "  Saving calibration data..." VT_RESET "\n");
      _keyboard->calculateAdaptiveThresholds();
      _keyboard->saveCalibrationData();
      _leds->playValidation();
      Serial.printf(VT_GREEN "  Calibration saved successfully." VT_RESET "\n");
      Serial.printf("  Returning to menu...\n");
      delay(800);
      _ui->vtClear();
      state = CAL_DONE;
      break;
    }

    case CAL_DONE:
      break;

    } // switch

    delay(5);
  } // while
}
