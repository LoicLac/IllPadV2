#include "ToolCalibration.h"
#include "SetupCommon.h"
#include "SetupUI.h"
#include "InputParser.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../core/KeyboardData.h"
#include "../managers/NvsManager.h"
#include <Arduino.h>
#include <Preferences.h>

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
  ui.vtFrameStart();
  char headerBuf[64];
  snprintf(headerBuf, sizeof(headerBuf), "TOOL 1: %s  %s (%u)", title, sensitivityLabel, targetBaseline);
  ui.drawConsoleHeader(headerBuf, true);
  ui.drawFrameEmpty();
  ui.drawFrameLine("Running MPR121 autoconfiguration...");
  ui.drawFrameEmpty();
  ui.drawFrameBottom();
  ui.vtFrameEnd();
  keyboard.runAutoconfiguration(targetBaseline);
  delay(CAL_AUTOCONFIG_COUNTDOWN_MS);
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
      snprintf(headerBuf, sizeof(headerBuf), "TOOL 1: %s  %s (%u)", title, sensitivityLabel, targetBaseline);
      ui.drawConsoleHeader(headerBuf, true);
      ui.drawFrameEmpty();
      ui.drawSection("GRID");

      ui.drawGrid(GRID_BASELINE, targetBaseline, currentBaselines, nullptr, nullptr, -1, 0, false, nullptr);

      ui.drawFrameEmpty();
      ui.drawSection("STATUS");
      if (stableCount == NUM_KEYS) {
        ui.drawFrameLine("Stability: " VT_GREEN "%d/%d stable" VT_RESET, stableCount, NUM_KEYS);
      } else {
        ui.drawFrameLine("Stability: " VT_YELLOW "Settling... %d/%d" VT_RESET, stableCount, NUM_KEYS);
      }
      ui.drawFrameEmpty();
      ui.drawControlBar(VT_DIM "[RET] CONTINUE  [q] ABORT" VT_RESET);
      ui.vtFrameEnd();
    }

    if (ev.type == NAV_ENTER && lastRefresh > 0) {
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
  Serial.print(ITERM_RESIZE);

  // Top-level states (same as V1 STATE_TOOL1_*)
  enum CalState {
    CAL_SENSITIVITY,
    CAL_MEASUREMENT,
    CAL_RECAP,
    CAL_SAVE,
    CAL_DONE
  };

  CalState state = CAL_SENSITIVITY;

  // Check NVS for existing calibration data
  bool nvsSaved = NvsManager::checkBlob(CAL_PREFERENCES_NAMESPACE, CAL_PREFERENCES_KEY,
                                         EEPROM_MAGIC, EEPROM_VERSION, sizeof(CalDataStore));

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

  memset(calibrated, 0, sizeof(calibrated));
  memset(measuredDeltas, 0, sizeof(measuredDeltas));

  // Free-mode state
  static const unsigned long CAL_DOUBLETAP_WINDOW_MS = 200;
  unsigned long lastReleaseTime[NUM_KEYS];
  memset(lastReleaseTime, 0, sizeof(lastReleaseTime));
  int prevDetected = -1;
  bool confirmPending = false;
  char confirmType = 0;   // 'v' = validate incomplete, 'q' = abort with data
  bool justReset = false;  // true for 1 display cycle after double-tap reset
  int resetKey = -1;

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
          prevDetected = -1;
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
        _ui->drawConsoleHeader("TOOL 1: PRESSURE CALIBRATION", nvsSaved);
        _ui->drawFrameEmpty();
        _ui->drawSection("SENSITIVITY PRESETS");
        _ui->drawFrameEmpty();
        for (int i = 0; i < NUM_SENSITIVITY_LEVELS; i++) {
          if (i == sensitivityIndex) {
            _ui->drawFrameLine(VT_REVERSE " [%d] %-16s (target: %u)" VT_RESET,
                               i + 1, sensitivityNames[i], sensitivityTargets[i]);
          } else {
            _ui->drawFrameLine(" [%d] %-16s (target: %u)",
                               i + 1, sensitivityNames[i], sensitivityTargets[i]);
          }
        }
        _ui->drawFrameEmpty();
        _ui->drawSection("INFO");
        _ui->drawFrameLine(VT_DIM "Select a sensitivity preset, then press Enter to start." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Higher sensitivity = more range but may clip on thick pads." VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawControlBar(VT_DIM "[1-3] SELECT  [RET] CONFIRM  [q] BACK" VT_RESET);
        _ui->vtFrameEnd();
      }
      break;
    }

    // =============================================================
    // FREE-PLAY MEASUREMENT — auto-capture + double-tap reset
    // =============================================================
    case CAL_MEASUREMENT: {
      _keyboard->pollAllSensorData();
      unsigned long now = millis();

      int detected = detectActiveKey(*_keyboard, referenceBaselines);

      // --- Release detection: update lastReleaseTime ---
      if (prevDetected >= 0 && (detected != prevDetected)) {
        lastReleaseTime[prevDetected] = now;
      }

      // --- New touch on a pad ---
      if (detected >= 0 && detected != prevDetected) {
        // Double-tap check: pad already calibrated + touched within window
        if (calibrated[detected] &&
            lastReleaseTime[detected] > 0 &&
            (now - lastReleaseTime[detected]) < CAL_DOUBLETAP_WINDOW_MS) {
          // Reset this pad
          calibrated[detected] = false;
          measuredDeltas[detected] = 0;
          justReset = true;
          resetKey = detected;
        }
        activeKey = detected;
        currentMaxDelta = 0;
      }

      // --- Live delta tracking + auto-capture ---
      if (detected >= 0) {
        uint16_t f = _keyboard->getFilteredData(detected);
        uint16_t delta = (referenceBaselines[detected] > f)
                       ? (referenceBaselines[detected] - f) : 0;
        if (delta > currentMaxDelta) currentMaxDelta = delta;

        // Auto-capture: update stored max if improved
        if (currentMaxDelta > measuredDeltas[detected]) {
          measuredDeltas[detected] = currentMaxDelta;
          if (!calibrated[detected]) calibrated[detected] = true;
        }
      }

      prevDetected = detected;

      // --- Display refresh ---
      if (now - lastRefresh >= 200) {
        lastRefresh = now;

        int doneCount = 0;
        for (int i = 0; i < NUM_KEYS; i++) {
          if (calibrated[i]) doneCount++;
        }

        _ui->vtFrameStart();
        char info[48];
        snprintf(info, sizeof(info), "TOOL 1: CALIBRATION  %d/%d", doneCount, NUM_KEYS);
        _ui->drawConsoleHeader(info, nvsSaved);
        _ui->drawFrameEmpty();
        _ui->drawSection("GRID");

        _ui->drawGrid(GRID_MEASUREMENT, 0, referenceBaselines, measuredDeltas, calibrated,
                       (detected >= 0) ? activeKey : -1,
                       currentMaxDelta, false, nullptr);

        _ui->drawFrameEmpty();
        _ui->drawSection("INFO");

        if (confirmPending) {
          if (confirmType == 'v') {
            _ui->drawFrameLine(VT_YELLOW "Only %d/%d calibrated. Continue?" VT_RESET, doneCount, NUM_KEYS);
          } else {
            _ui->drawFrameLine(VT_YELLOW "Discard %d calibrated pads?" VT_RESET, doneCount);
          }
          _ui->drawFrameEmpty();
          _ui->drawControlBar(VT_BOLD "[y] YES  [n] NO" VT_RESET);
        } else {
          if (justReset) {
            _ui->drawFrameLine(VT_CYAN "Key %d" VT_RESET VT_YELLOW " reset -- touch again to recalibrate" VT_RESET, resetKey);
            _ui->drawFrameEmpty();
            justReset = false;
          } else if (detected == -2) {
            _ui->drawFrameLine(VT_YELLOW "Multiple pads detected!" VT_RESET);
            _ui->drawFrameLine("Lift off and touch ONE pad at a time.");
            _ui->drawFrameEmpty();
          } else if (detected >= 0) {
            int sensor = activeKey / CHANNELS_PER_SENSOR;
            int channel = activeKey % CHANNELS_PER_SENSOR;
            char sc = 'A' + sensor;

            if (calibrated[activeKey] && measuredDeltas[activeKey] >= currentMaxDelta) {
              _ui->drawFrameLine(VT_CYAN "Key %d" VT_RESET " (%c:Ch%d)  Stored: %-5u  Current: %-5u",
                                 activeKey, sc, channel, measuredDeltas[activeKey], currentMaxDelta);
              _ui->drawFrameLine(VT_DIM "Double-tap to reset. Press harder to improve." VT_RESET);
            } else {
              uint16_t f = _keyboard->getFilteredData(activeKey);
              uint16_t liveDelta = (referenceBaselines[activeKey] > f)
                                 ? (referenceBaselines[activeKey] - f) : 0;
              _ui->drawFrameLine(VT_CYAN "Key %d" VT_RESET " (%c:Ch%d)  Delta: %-5u  Max: " VT_GREEN "%-5u" VT_RESET,
                                 activeKey, sc, channel, liveDelta, currentMaxDelta);
            }
          } else {
            _ui->drawFrameLine(VT_DIM "Touch pads with MAX force. Double-tap to redo." VT_RESET);
            _ui->drawFrameEmpty();
          }

          _ui->drawFrameEmpty();
          _ui->drawSection("STATS");
          CalStats st = computeStats(measuredDeltas, calibrated);
          if (st.count > 0) {
            _ui->drawFrameLine("Progress: %d/%d   Min: %u  Max: %u  Avg: %u",
                               st.count, NUM_KEYS, st.minVal, st.maxVal, st.avgVal);
          } else {
            _ui->drawFrameLine("Progress: 0/%d", NUM_KEYS);
          }
          _ui->drawFrameEmpty();
          _ui->drawControlBar(VT_DIM "[RET] VALIDATE ALL  [q] ABORT" VT_RESET);
        }
        _ui->vtFrameEnd();
      }

      // --- Input handling ---
      if (confirmPending) {
        if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
          if (confirmType == 'v') {
            for (int i = 0; i < NUM_KEYS; i++) {
              if (calibrated[i]) {
                _keyboard->setCalibrationMaxDelta(i, measuredDeltas[i]);
              }
            }
            _keyboard->setAutoReconfigEnabled(true);
            _ui->vtClear();
            state = CAL_RECAP;
            screenDirty = true;
          } else {
            _keyboard->setAutoReconfigEnabled(true);
            _ui->vtClear();
            state = CAL_DONE;
          }
          confirmPending = false;
        } else if (ev.type == NAV_CHAR && (ev.ch == 'n' || ev.ch == 'N')) {
          confirmPending = false;
        }
      } else {
        if (ev.type == NAV_ENTER) {
          int doneCount = 0;
          for (int i = 0; i < NUM_KEYS; i++) {
            if (calibrated[i]) doneCount++;
          }
          if (doneCount == 0) {
            // Nothing calibrated — ignore ENTER
          } else if (doneCount < NUM_KEYS) {
            confirmPending = true;
            confirmType = 'v';
          } else {
            for (int i = 0; i < NUM_KEYS; i++) {
              _keyboard->setCalibrationMaxDelta(i, measuredDeltas[i]);
            }
            _keyboard->setAutoReconfigEnabled(true);
            _ui->vtClear();
            state = CAL_RECAP;
            screenDirty = true;
          }
        }
        else if (ev.type == NAV_QUIT) {
          int doneCount = 0;
          for (int i = 0; i < NUM_KEYS; i++) {
            if (calibrated[i]) doneCount++;
          }
          if (doneCount > 0) {
            confirmPending = true;
            confirmType = 'q';
          } else {
            _keyboard->setAutoReconfigEnabled(true);
            _ui->vtClear();
            state = CAL_DONE;
          }
        }
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
        _ui->drawConsoleHeader("TOOL 1: CALIBRATION COMPLETE", nvsSaved);
        _ui->drawFrameEmpty();
        _ui->drawSection("GRID");

        _ui->drawGrid(GRID_MEASUREMENT, 0, referenceBaselines, measuredDeltas, calibrated, -1, 0, false, nullptr);

        _ui->drawFrameEmpty();
        _ui->drawSection("STATS");
        _ui->drawFrameLine("Sensitivity: %s (%u)",
                           sensitivityNames[sensitivityIndex], sensitivityTargets[sensitivityIndex]);
        CalStats st = computeStats(measuredDeltas, calibrated);
        if (st.count > 0) {
          if (st.warnings > 0) {
            _ui->drawFrameLine("Min: %u   Max: %u   Avg: %u   " VT_RED "Warnings: %d keys below %u" VT_RESET,
                               st.minVal, st.maxVal, st.avgVal, st.warnings, CAL_PRESSURE_MIN_DELTA_TO_VALIDATE);
          } else {
            _ui->drawFrameLine("Min: %u   Max: %u   Avg: %u   " VT_GREEN "All keys OK" VT_RESET,
                               st.minVal, st.maxVal, st.avgVal);
          }
        } else {
          _ui->drawFrameLine(VT_RED "No keys calibrated." VT_RESET);
        }
        _ui->drawFrameEmpty();
        if (st.count > 0) {
          _ui->drawControlBar(VT_DIM "[RET] SAVE  [r] REDO ALL  [q] BACK" VT_RESET);
        } else {
          _ui->drawControlBar(VT_DIM "[r] REDO ALL  [q] BACK" VT_RESET);
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
      _ui->vtFrameStart();
      _ui->drawConsoleHeader("TOOL 1: SAVING", false);
      _ui->drawFrameEmpty();
      _ui->drawFrameLine(VT_BOLD "Saving calibration data..." VT_RESET);
      _ui->drawFrameEmpty();
      _ui->drawFrameBottom();
      _ui->vtFrameEnd();
      _keyboard->calculateAdaptiveThresholds();
      _keyboard->saveCalibrationData();
      _leds->playValidation();
      nvsSaved = true;
      _ui->vtClear();
      _ui->vtFrameStart();
      _ui->drawConsoleHeader("TOOL 1: SAVING", true);
      _ui->drawFrameEmpty();
      _ui->drawFrameLine(VT_GREEN "Calibration saved successfully." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Returning to menu..." VT_RESET);
      _ui->drawFrameEmpty();
      _ui->drawFrameBottom();
      _ui->vtFrameEnd();
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
