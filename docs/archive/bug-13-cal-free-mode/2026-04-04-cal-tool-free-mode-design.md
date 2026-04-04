# Bug 13 — Cal Tool Free Mode

**Date**: 2026-04-04
**Source**: bug-audit-worklog.md, issue 13
**Scope**: `ToolCalibration.cpp` CAL_MEASUREMENT state + `SetupUI` grid coloring

## Summary

Refactor the calibration measurement phase from "touch pad + ENTER per pad" to a free-play mode where touching auto-captures max delta, double-tap resets a pad, and a single ENTER validates all at once.

## Current Behavior

1. User touches one pad, sees live delta
2. Presses ENTER to validate that pad (`measuredDeltas[pad] = maxDelta`)
3. Repeats 48 times
4. After 48/48, transitions to RECAP
5. Shortcut 's' jumps to RECAP with partial calibration

## New Behavior

### Auto-Capture

- Touching a pad automatically records its `maxDelta`
- Rule: "only if new max" — re-touching can only improve the stored value
- Pad marked `calibrated = true` on first touch above `TOUCH_DETECT_THRESHOLD` (50)
- No minimum gate for validation — any captured delta is accepted

### Double-Tap Reset (200ms)

- Re-touching a calibrated pad within 200ms of release = reset
- Reset: `calibrated[pad] = false`, `measuredDeltas[pad] = 0`
- Detection via `lastReleaseTime[NUM_KEYS]` array, updated when `detectActiveKey()` transitions from pad X to something else (-1 or different pad)
- After reset, the ongoing touch recaptures from 0 (same press, no need to lift again)
- Ordering on new touch: check double-tap FIRST (reset if triggered), THEN start delta tracking
- Window: 200ms hardcoded constant `CAL_DOUBLETAP_WINDOW_MS`

### Delta Warning Colors (non-blocking)

Grid cells for calibrated pads are color-coded by delta value:

| Delta     | Color  | Meaning          |
|-----------|--------|------------------|
| < 100     | Red    | Almost no range  |
| 100 - 299 | Orange | Low range        |
| 300 - 499 | Yellow | Acceptable       |
| >= 500    | Green  | Good             |

No threshold prevents validation. The user sees the color and decides.

`CAL_PRESSURE_MIN_DELTA_TO_VALIDATE` (300) stays in code for `computeStats()` warning count at RECAP, but gates nothing during measurement.

### Validation (ENTER)

- ENTER validates all calibrated pads globally
- Calls `setCalibrationMaxDelta()` in a loop for all `calibrated[i] == true`
- If `calibratedCount < 48`: inline prompt `"Only X/48 calibrated. Continue? (y/n)"`
  - `y` → proceed to RECAP
  - `n` → return to measurement
- If 48/48: proceed directly to RECAP
- 's' removed as alias — ENTER is the only validation key

### Abort (q)

- If at least 1 pad calibrated: prompt `"Discard X calibrated pads? (y/n)"`
  - `y` → discard, return to menu (CAL_DONE)
  - `n` → return to measurement
- If 0 pads calibrated: exit directly

### Baseline Refresh

Removed. The snapshot captured during stabilization phase is sufficient — thermal drift during a calibration session is negligible.

The existing baseline refresh code (lines 340-345 in current CAL_MEASUREMENT) is deleted entirely.

## Unchanged

- `CAL_SENSITIVITY`: preset selection + stabilization phase + baseline capture
- `CAL_RECAP`: final grid, stats, save/redo/quit
- `CAL_SAVE`: NVS write flow
- `runStabilizationPhase()`: autoconfig + live grid + baseline capture
- `detectActiveKey()` in SetupCommon.h: highest-delta detection with ambiguity rejection
- `computeStats()` in SetupCommon.h: min/max/avg/warnings calculation
- `CalDataStore` struct, NVS namespace `illpad_cal`, ToolCalibration.h header

## State Variables

### Existing (role change)

- `activeKey`: tracks currently-touched pad for visual feedback (no longer "pad to validate with ENTER")
- `currentMaxDelta`: live delta of current touch (visual feedback only)
- `measuredDeltas[NUM_KEYS]`: accumulated max deltas per pad (now updated automatically, not on ENTER)
- `calibrated[NUM_KEYS]`: per-pad calibration flag (now set automatically on touch)

### New

```cpp
static const unsigned long CAL_DOUBLETAP_WINDOW_MS = 200;

unsigned long lastReleaseTime[NUM_KEYS];  // millis() at release, for double-tap detection
int prevDetected;                          // detectActiveKey() result from previous cycle
bool confirmPending;                       // true when y/n prompt is shown
char confirmType;                          // 'v' = validate incomplete, 'q' = abort with data
```

`lastReleaseTime` initialized to 0. `prevDetected` initialized to -1. Confirm state initialized to false.

## Display Changes

### MEASUREMENT Screen

Layout unchanged (header, grid, info, stats, control bar). Content changes:

- **Info box**: shows active pad details in real-time. No "Press ENTER to validate" message. Instead:
  - Uncalibrated pad being touched: `"Key X (A:Ch3)  Delta: 450  Max: 520"`
  - Calibrated pad being touched: `"Key X (A:Ch3)  Stored: 520  Current: 430"` (shows stored vs live)
  - Double-tap reset: brief flash message `"Key X reset"` (1 refresh cycle)
  - No pad touched: `"Touch pads with MAX force. Double-tap to redo."`
- **Control bar**: `[RET] VALIDATE ALL  [q] ABORT`
- **Grid**: calibrated cells use delta-based coloring (red/orange/yellow/green). Uncalibrated cells remain dim.
- **Stats**: live progress count + min/max/avg (already exists, no change)

### Confirm Prompt

When `confirmPending == true`, the control bar is replaced by the prompt:
- Validate incomplete: `"Only X/48 calibrated. Continue? [y/n]"`
- Abort with data: `"Discard X calibrated pads? [y/n]"`

All other input ignored while confirm is pending. Only 'y' and 'n' are accepted. Pad auto-capture continues during confirm (harmless — user may press 'n' and keep calibrating).

## Grid Coloring Implementation

`SetupUI::drawGrid()` with `GRID_MEASUREMENT` mode already receives `measuredDeltas[]` and `calibrated[]`. The coloring logic is added inside the existing grid rendering for calibrated cells:

```
if calibrated[pad]:
    delta = measuredDeltas[pad]
    if delta < 100:        cell color = VT_RED
    else if delta < 300:   cell color = VT_YELLOW (208 = orange via 256-color)
    else if delta < 500:   cell color = VT_YELLOW
    else:                  cell color = VT_GREEN
```

Uses existing VT100 escape sequences. Orange via `\033[38;5;208m` (256-color mode, already used elsewhere in setup UI).

## Files Impacted

| File | Change | Complexity |
|------|--------|-----------|
| `ToolCalibration.cpp` | Refactor CAL_MEASUREMENT: auto-capture loop, double-tap detection, ENTER global validation, confirm prompts, remove baseline refresh | High |
| `SetupUI.cpp` | Add delta-based coloring in `drawGrid(GRID_MEASUREMENT)` | Low |
| `HardwareConfig.h` | Add `CAL_DOUBLETAP_WINDOW_MS` constant (or in ToolCalibration.cpp locally) | Trivial |
