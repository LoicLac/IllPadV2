# Cal Tool Free Mode — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor Tool 1 calibration from "touch pad + ENTER per pad" to free-play mode with auto-capture, double-tap reset, delta-colored grid, and single ENTER global validation.

**Architecture:** The change is scoped to `CAL_MEASUREMENT` state in `ToolCalibration.cpp` (auto-capture + double-tap + confirm prompts) and `GRID_MEASUREMENT` rendering in `SetupUI.cpp` (delta-based coloring). No new files, no header changes, no NVS changes.

**Tech Stack:** C++17, Arduino framework, PlatformIO (ESP32-S3), VT100 terminal UI

**Build command:** `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

**Spec:** `docs/superpowers/specs/2026-04-04-cal-tool-free-mode-design.md`

---

## File Map

| File | Role | Change |
|------|------|--------|
| `src/setup/SetupUI.h` | VT100 macros | Add `VT_ORANGE` define |
| `src/setup/SetupUI.cpp:442-457` | `GRID_MEASUREMENT` cell rendering | Replace 2-color logic with 4-tier delta coloring |
| `src/setup/ToolCalibration.cpp:236-374` | `CAL_MEASUREMENT` state handler | Full rewrite: auto-capture, double-tap, confirm prompts |

---

## Task 1: Grid delta coloring in SetupUI

**Files:**
- Modify: `src/setup/SetupUI.h:25` (add VT_ORANGE macro)
- Modify: `src/setup/SetupUI.cpp:442-457` (GRID_MEASUREMENT branch)

- [ ] **Step 1: Add VT_ORANGE macro to SetupUI.h**

After line 25 (`#define VT_WHITE "\033[37m"`), add:

```cpp
#define VT_ORANGE     "\033[38;5;208m"
```

- [ ] **Step 2: Replace GRID_MEASUREMENT cell coloring in SetupUI.cpp**

Replace lines 442-457 (the entire `else if (mode == GRID_MEASUREMENT)` block) with:

```cpp
      } else if (mode == GRID_MEASUREMENT) {
        if (key == activeKey) {
          // Active pad: show live delta with cyan highlight
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                          VT_CYAN " %4u" VT_RESET, activeDelta);
        } else if (done && done[key]) {
          // Calibrated pad: color by delta value
          uint16_t d = measuredDeltas ? measuredDeltas[key] : 0;
          const char* color;
          if      (d < 100) color = VT_RED;
          else if (d < 300) color = VT_ORANGE;
          else if (d < 500) color = VT_YELLOW;
          else              color = VT_GREEN;
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos, "%s %4u" VT_RESET, color, d);
        } else {
          // Uncalibrated pad: dim placeholder
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos, VT_DIM "  -- " VT_RESET);
        }
      }
```

Key changes vs current code:
- Active pad shows numeric delta (was `>XX<` / `*XX*` with 2-digit truncation)
- Calibrated pads use 4-tier coloring (was: green if >= 50, red otherwise)
- `activeIsDone` parameter is no longer used in this branch (still passed by caller, harmless)

- [ ] **Step 3: Build**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: BUILD SUCCESS (grid change is self-contained, no API change)

- [ ] **Step 4: Commit**

```bash
git add src/setup/SetupUI.h src/setup/SetupUI.cpp
git commit -m "feat(cal): 4-tier delta coloring in calibration grid

Red (<100), orange (100-299), yellow (300-499), green (>=500).
Active pad shows numeric delta instead of bracketed format."
```

---

## Task 2: Auto-capture + double-tap reset

**Files:**
- Modify: `src/setup/ToolCalibration.cpp:236-374` (CAL_MEASUREMENT state)

This is the core rewrite. The state handler changes from "detect → track → ENTER to validate one pad" to "detect → auto-capture → double-tap to reset".

- [ ] **Step 1: Add state variables**

In `ToolCalibration::run()`, after the existing variable declarations (line 155, after `memset(measuredDeltas, 0, ...)`), add:

```cpp
  static const unsigned long CAL_DOUBLETAP_WINDOW_MS = 200;

  unsigned long lastReleaseTime[NUM_KEYS];
  memset(lastReleaseTime, 0, sizeof(lastReleaseTime));
  int prevDetected = -1;
  bool confirmPending = false;
  char confirmType = 0;  // 'v' = validate incomplete, 'q' = abort with data
  bool justReset = false; // true for 1 display cycle after double-tap reset
  int resetKey = -1;       // which pad was just reset (for flash message)
```

- [ ] **Step 2: Rewrite CAL_MEASUREMENT detection + auto-capture**

Replace the entire `case CAL_MEASUREMENT:` block (lines 236-374) with:

```cpp
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
          // Show confirm prompt
          if (confirmType == 'v') {
            _ui->drawFrameLine(VT_YELLOW "Only %d/%d calibrated. Continue?" VT_RESET, doneCount, NUM_KEYS);
          } else {
            _ui->drawFrameLine(VT_YELLOW "Discard %d calibrated pads?" VT_RESET, doneCount);
          }
          _ui->drawFrameEmpty();
          _ui->drawControlBar(VT_BOLD "[y] YES  [n] NO" VT_RESET);
        } else {
          // Normal info display
          if (justReset) {
            _ui->drawFrameLine(VT_CYAN "Key %d" VT_RESET VT_YELLOW " reset — touch again to recalibrate" VT_RESET, resetKey);
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
              // Already calibrated, current touch not improving
              _ui->drawFrameLine(VT_CYAN "Key %d" VT_RESET " (%c:Ch%d)  Stored: %-5u  Current: %-5u",
                                 activeKey, sc, channel, measuredDeltas[activeKey], currentMaxDelta);
              _ui->drawFrameLine(VT_DIM "Double-tap to reset. Press harder to improve." VT_RESET);
            } else {
              // New capture or improving
              _ui->drawFrameLine(VT_CYAN "Key %d" VT_RESET " (%c:Ch%d)  Delta: %-5u  Max: " VT_GREEN "%-5u" VT_RESET,
                                 activeKey, sc, channel,
                                 (uint16_t)((referenceBaselines[activeKey] > _keyboard->getFilteredData(activeKey))
                                   ? (referenceBaselines[activeKey] - _keyboard->getFilteredData(activeKey)) : 0),
                                 currentMaxDelta);
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
            // Apply calibration and go to RECAP
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
            // Abort — discard
            _keyboard->setAutoReconfigEnabled(true);
            _ui->vtClear();
            state = CAL_DONE;
          }
          confirmPending = false;
        } else if (ev.type == NAV_CHAR && (ev.ch == 'n' || ev.ch == 'N')) {
          confirmPending = false;
        }
        // All other input ignored during confirm
      } else {
        if (ev.type == NAV_ENTER) {
          int doneCount = 0;
          for (int i = 0; i < NUM_KEYS; i++) {
            if (calibrated[i]) doneCount++;
          }
          if (doneCount == 0) {
            // Nothing calibrated — ignore ENTER
          } else if (doneCount < NUM_KEYS) {
            // Incomplete — ask confirmation
            confirmPending = true;
            confirmType = 'v';
          } else {
            // All 48 done — go directly
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
```

- [ ] **Step 3: Build**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add src/setup/ToolCalibration.cpp
git commit -m "feat(cal): free-play mode with auto-capture + double-tap reset

CAL_MEASUREMENT rewritten:
- Touch auto-captures max delta (only-if-new-max rule)
- Double-tap within 200ms resets a pad for redo
- ENTER validates all pads globally (confirm prompt if <48)
- q prompts before discarding calibrated data
- Baseline refresh removed (drift negligible)"
```

---

## Task 3: Cleanup + final verification

**Files:**
- Modify: `src/setup/ToolCalibration.cpp` (remove dead variable)
- Modify: `docs/drafts/bug-audit-worklog.md` (mark bug 13 done)

- [ ] **Step 1: Remove unused lastActiveKey variable**

In `ToolCalibration::run()`, the variable `lastActiveKey` (declared around line 154) was used by the old flow. The new flow uses `prevDetected` instead. Remove:

```cpp
  int      lastActiveKey = -1;
```

And verify no remaining references to `lastActiveKey` in the file.

- [ ] **Step 2: Build**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: BUILD SUCCESS (confirms no stale references)

- [ ] **Step 3: Update bug-audit-worklog.md**

Move Bug 13 from "A FAIRE — Session dediee" to the DONE table. Add row:

```markdown
| 13 | Cal tool validation globale | Free-play mode: auto-capture, double-tap 200ms reset, 4-tier delta coloring, ENTER global validate with confirm prompt. |
```

Remove the entire "Bug 13" section from "A FAIRE — Session dediee".

- [ ] **Step 4: Commit**

```bash
git add src/setup/ToolCalibration.cpp docs/drafts/bug-audit-worklog.md
git commit -m "chore(cal): cleanup dead variable + mark bug 13 done"
```
