# RGB LED Display Design — SK6812 RGBW NeoPixel Stick

**Date**: 2026-03-22
**Branch**: `feature/rgb-leds`
**Hardware**: Adafruit NeoPixel Stick 8x SK6812 RGBW (product 2868)

## 1. Hardware & Driver Layer

### Hardware

Adafruit NeoPixel Stick 8x SK6812 RGBW. Linear orientation, LED 1 = left, LED 8 = right.

### Wiring

3 wires: 5V, GND, DIN on a single ESP32-S3 GPIO. Replaces the current 8 GPIO pins (`LED_PIN_1` through `LED_PIN_8`).

### Library

Adafruit NeoPixel (`NEO_GRBW + NEO_KHZ800`). Chosen over FastLED for native RGBW support without workarounds.

**platformio.ini**: add `adafruit/Adafruit NeoPixel` to `lib_deps`.

### Pixel Buffer

Array of 8 RGBW pixels in RAM. All logic writes to this buffer. A single `strip.show()` at the end of `update()` pushes to hardware. The ESP32-S3 RMT peripheral handles the transfer (~320us, non-blocking for CPU).

### Brightness

Global scaler (`_brightness` / rear pot) applied via `strip.setBrightness()` before `show()`. Same API as before, single control point.

## 2. Color Palette

All colors defined as RGBW constants in `HardwareConfig.h`. The W channel is the SK6812's dedicated white LED.

### Base Colors

| Constant | R | G | B | W | Usage |
|---|---|---|---|---|---|
| `COL_WHITE` | 0 | 0 | 0 | 255 | NORMAL foreground, tick flash fg |
| `COL_WHITE_DIM` | 0 | 0 | 0 | 40 | NORMAL background |
| `COL_BLUE` | 0 | 0 | 255 | 0 | ARPEG foreground |
| `COL_BLUE_DIM` | 0 | 0 | 40 | 0 | ARPEG background |

### Scale Confirmations (yellow, 3 saturations)

| Constant | R | G | B | W | Usage |
|---|---|---|---|---|---|
| `COL_SCALE_ROOT` | 255 | 200 | 0 | 0 | Root change — vivid saturated yellow |
| `COL_SCALE_MODE` | 200 | 160 | 0 | 60 | Mode change — pale yellow (+W) |
| `COL_SCALE_CHROM` | 255 | 140 | 0 | 0 | Chromatic toggle — golden yellow (+R) |

### Arp Confirmations (blue, 3 variations)

| Constant | R | G | B | W | Usage |
|---|---|---|---|---|---|
| `COL_ARP_HOLD` | 0 | 0 | 255 | 0 | Hold toggle — deep blue |
| `COL_ARP_PLAY` | 0 | 80 | 255 | 0 | Play/Stop — blue-cyan |
| `COL_ARP_OCTAVE` | 80 | 0 | 255 | 0 | Octave — blue-violet |

### System Colors

| Constant | R | G | B | W | Usage |
|---|---|---|---|---|---|
| `COL_ERROR` | 255 | 0 | 0 | 0 | Error (LEDs 4-5) |
| `COL_BOOT` | 0 | 0 | 0 | 255 | Boot fill (= white) |
| `COL_BOOT_FAIL` | 255 | 0 | 0 | 0 | Boot failure blink (= red) |
| `COL_SETUP` | 128 | 0 | 255 | 0 | Setup comet (violet) |

### Battery Gauge Gradient (8 pre-computed values)

| LED | R | G | B | W |
|---|---|---|---|---|
| 1 | 255 | 0 | 0 | 0 |
| 2 | 255 | 36 | 0 | 0 |
| 3 | 255 | 73 | 0 | 0 |
| 4 | 255 | 145 | 0 | 0 |
| 5 | 200 | 200 | 0 | 0 |
| 6 | 145 | 255 | 0 | 0 |
| 7 | 73 | 255 | 0 | 0 |
| 8 | 0 | 255 | 0 | 0 |

Dim values are obtained by scaling the base color (not separate constants per dim level). Exception: `COL_WHITE_DIM` and `COL_BLUE_DIM` are explicit constants since dimming a single W or B channel is more predictable.

## 3. Normal Display (multi-bank state)

Same priority-based state machine as current implementation. Same 64-entry sine LUT. Output changes from `uint8_t` to RGBW pixels.

### NORMAL Banks

- **Foreground**: `COL_WHITE` solid (W channel = 255)
- **Background**: `COL_WHITE_DIM` solid (W channel = 40)

### ARPEG Banks

| State | Base Color | Tick Flash Color | Sine Range |
|---|---|---|---|
| Foreground stopped | `COL_BLUE` | — | 30%-100% of B channel |
| Foreground playing | `COL_BLUE` | `COL_WHITE` (white flash) | 30%-80% of B, spike white 100% |
| Background stopped | `COL_BLUE_DIM` | — | 8%-25% of B channel |
| Background playing | `COL_BLUE_DIM` | `COL_BLUE_DIM` (blue dim flash) | 8%-20% of B, spike 25% |

### Sine Modulation

Identical to current code. `sineRaw` (0-255) maps between min and max. Instead of modulating a single `uint8_t`, modulate the B channel (for blue) or W channel (for white). Other channels stay at 0.

### Tick Flash

Same mechanism: `consumeTickFlash()`, `_flashStartTime[i]`, duration `LED_TICK_FLASH_DURATION_MS` (30ms). Tick flash is a **complete pixel override** (not a blend):

- **Foreground**: pixel set to `{0,0,0,255}` (pure white), completely replacing the blue sine value
- **Background**: pixel set to `COL_BLUE_DIM` at spike brightness (25%), replacing the dimmed sine value

### Global Brightness

Applied via `strip.setBrightness()` before `show()`. Entire buffer scaled uniformly.

## 4. Confirmations

All confirmations are auto-expiring overlays. Same priority as current (above normal display, below battery/bargraph/error). A new confirmation preempts the active one.

### Timing Constants (in HardwareConfig.h)

All reuse or adapt existing constants. Configurable for a future Tool LED.

```
LED_CONFIRM_UNIT_MS          = 50    // Base phase unit
LED_CONFIRM_BANK_PHASES      = 6     // Triple blink = 300ms
LED_CONFIRM_SCALE_PHASES     = 4     // Double blink = 200ms
LED_CONFIRM_HOLD_ON_MS       = 150   // Hold ON: blink on phase
LED_CONFIRM_HOLD_TOTAL_MS    = 250   // Hold ON: total
LED_CONFIRM_FADE_MS          = 300   // Hold OFF + Stop: fade-out duration (new)
LED_CONFIRM_PLAY_STEPS       = 4     // Play: number of rising beats
LED_CONFIRM_OCTAVE_PHASES    = 6     // Triple blink = 300ms (was 2)
LED_CONFIRM_BRIGHTNESS_PCT   = 50    // Bank switch blink intensity
```

### Bank Switch

- Triple blink all 8 LEDs in context color (white if NORMAL, blue if ARPEG)
- 6 phases x 50ms = 300ms

### Scale Root

- Double blink current LED in `COL_SCALE_ROOT` (vivid saturated yellow)
- 4 phases x 50ms = 200ms

### Scale Mode

- Double blink current LED in `COL_SCALE_MODE` (pale yellow)
- 4 phases x 50ms = 200ms

### Scale Chromatic

- Double blink current LED in `COL_SCALE_CHROM` (golden yellow)
- 4 phases x 50ms = 200ms

### Hold ON

- Sharp blink current LED in `COL_ARP_HOLD` (deep blue)
- 150ms on + 100ms off = 250ms

### Hold OFF

- Fade-out current LED from `COL_ARP_HOLD` to off
- ~300ms (`LED_CONFIRM_FADE_MS`), linear decay of B channel

### Play

- 4 rising flashes in `COL_ARP_PLAY` (blue-cyan) on current LED
- Synchronized to the next 4 tick flashes from the arp engine
- Each flash is a sharp spike (same duration as tick flash: `LED_TICK_FLASH_DURATION_MS`)
- Intensity per flash: 25% → 50% → 75% → 100% of `COL_ARP_PLAY` brightness
- After 4th flash, transition to normal display (confirmation expires)
- Fallback: if arp not ticking yet (first play after hold), use fixed interval = current step duration from `ArpEngine::getStepDurationMs()`

**Sync mechanism**: LedController tracks a `_playFlashCount` (starts at 0, incremented each time `consumeTickFlash()` fires on the current bank's arp engine during CONFIRM_PLAY). When count reaches `LED_CONFIRM_PLAY_STEPS` (4), the confirmation expires. Between ticks, the LED stays off (no sustained glow — just discrete flashes).

### Stop

- Fade-out current LED from `COL_ARP_PLAY` to off
- ~300ms (`LED_CONFIRM_FADE_MS`), linear decay

### Octave

- Triple blink in `COL_ARP_OCTAVE` (blue-violet)
- 2 LEDs of selected group only, all others off
- Groups: param 1 → LEDs 0-1, param 2 → LEDs 2-3, param 3 → LEDs 4-5, param 4 → LEDs 6-7 (0-indexed)
- LED indices: `startLed = (param - 1) * 2`, light `startLed` and `startLed + 1`
- 6 phases x 50ms = 300ms

### ConfirmType Enum Extension

Split existing types for color differentiation:

```
CONFIRM_NONE, CONFIRM_BANK_SWITCH,
CONFIRM_SCALE_ROOT, CONFIRM_SCALE_MODE, CONFIRM_SCALE_CHROM,
CONFIRM_HOLD_ON, CONFIRM_HOLD_OFF,
CONFIRM_PLAY, CONFIRM_STOP,
CONFIRM_OCTAVE
```

## 5. Pot Bargraph & Catch Visualization

### Before Catch (pot not caught)

- **Dim bargraph**: N LEDs in context dim color (`COL_WHITE_DIM` or `COL_BLUE_DIM`) — represents the real parameter value
- **Bright cursor**: 1 LED in context full color (`COL_WHITE` or `COL_BLUE`) — represents physical pot position
- Both visible simultaneously; the gap between them shows the catch distance

### At Catch

- Cursor meets bargraph — everything transitions to full context color
- No flash or extra confirmation

### After Catch

- Classic bargraph in full context color, follows pot in real time

### Persistence

- Bargraph appears as soon as pot moves
- Stays visible as long as pot moves
- Disappears after `potBarDurationMs` of inactivity (existing NVS variable in `illpad_set`, configurable via Tool 5, range 1-10s, default 3s)
- Each pot movement resets the persistence timer

### API Change

```cpp
// Old:
void showPotBargraph(uint8_t level);

// New:
void showPotBargraph(uint8_t realLevel, uint8_t potLevel, bool caught);
```

- `realLevel`: current parameter value mapped to 0-8 LEDs
- `potLevel`: physical pot position mapped to 0-7 (cursor position)
- `caught`: if true, display as unified full-color bar; if false, show dim bar + bright cursor
- PotRouter provides both values + catch state via existing getters (add `getBargraphPotLevel()` and `isBargraphCaught()` alongside existing `getBargraphLevel()`)

**Context color resolution**: LedController already has `_currentBank` and `_slots` pointer. It reads `_slots[_currentBank].type` to determine NORMAL (white) or ARPEG (blue). No extra parameter needed.

### Cursor/Bargraph Overlap

If the cursor falls on a LED that's part of the dim bargraph, that LED displays at full brightness instead of dim.

## 6. Battery Gauge

- Solid bar, no pulse (simplified from current asymmetric triangle heartbeat)
- Each LED has its fixed color from the pre-computed gradient (LED 1 = red -> LED 8 = green)
- N LEDs lit corresponding to battery percentage, each in its gradient color
- Mapping formula: `N = (percent * 8 + 50) / 100` (e.g., 50% → 4 LEDs, 88% → 7 LEDs, 100% → 8 LEDs)
- Display duration: `BAT_DISPLAY_DURATION_MS` (3s, unchanged)
- Trigger: rear button press (unchanged)

## 7. Boot Sequence

- Progressive fill in pure white (`COL_BOOT`)
- 8 steps, 1 more LED per step
- Failure: previous LEDs solid white, failed LED blinks red (`COL_BOOT_FAIL`) rapidly
- `endBoot()` transitions to normal display (unchanged)

## 8. Error (Sensing Task Stall)

- LEDs 4-5 only, blink red (`COL_ERROR`)
- 500ms on/off (existing `_lastBlinkTime` timer)
- All 6 other LEDs off

## 9. Setup Mode (Sci-Fi Violet Comet)

- Ping-pong chase across 8 LEDs in `COL_SETUP` (violet)
- Head: 100%, trail -1: 40%, trail -2: 10%
- Speed: ~2-3s per round trip, constant `LED_SETUP_CHASE_SPEED_MS` (time per step, ~180ms for 14 steps per round trip)
- Active during entire setup duration (Tools 1-6)
- Separate state flag `_setupComet` (distinct from `_chaseActive` used by calibration entry)
- Tools that need LED feedback (calibration validation flash) temporarily preempt the comet

## 10. Files Changed

### Modified (3 files)

1. **`HardwareConfig.h`**: Replace 8 `LED_PIN_x` with single `LED_DATA_PIN`. All brightness constants become RGBW color constants. Add new constants: `LED_CONFIRM_FADE_MS` (300), `LED_CONFIRM_PLAY_STEPS` (4), `LED_SETUP_CHASE_SPEED_MS` (~180). Update `LED_CONFIRM_OCTAVE_PHASES` from 2 to 6.

2. **`LedController.h`**: Replace `_pins[NUM_LEDS]` with NeoPixel strip object. Extend `ConfirmType` enum (10 types, split from 5). Update `showPotBargraph()` signature. Add `_setupComet` flag (separate from `_chaseActive`). Add `_playFlashCount` for play confirmation sync. Add `_fadeStartTime` + `_fadeColor` for fade-out confirmations.

3. **`LedController.cpp`**: Swap all `analogWrite`/`digitalWrite` calls to pixel buffer writes + single `strip.show()` at end of `update()`. Implement color-based state machine. Add fade-out logic (hold-off, stop). Add catch visualization (dim bar + cursor). Add setup comet chase with trail. Reduce error to LEDs 4-5 only. Remove battery heartbeat pulse. Add play rising flash logic with tick sync.

4. **`PotRouter.h/cpp`**: Add `getBargraphPotLevel()` and `isBargraphCaught()` getters alongside existing `getBargraphLevel()`.

5. **`main.cpp`**: Update `showPotBargraph()` call to pass 3 args from PotRouter.

6. **`platformio.ini`**: Add `adafruit/Adafruit NeoPixel` to `lib_deps`.

### Added (0 files)

No new files. Library added via `platformio.ini` `lib_deps`.

### Unchanged

- Public API surface (except `showPotBargraph` signature and `ConfirmType` enum extension)
- State machine priorities and timing logic
- Sine LUT mechanism
- `CapacitiveKeyboard` (DO NOT MODIFY)

### Breaking Changes

- `ConfirmType` enum: 5 values → 10 values. Callers using `CONFIRM_SCALE` must switch to `CONFIRM_SCALE_ROOT` / `CONFIRM_SCALE_MODE` / `CONFIRM_SCALE_CHROM`. Callers using `CONFIRM_HOLD` must switch to `CONFIRM_HOLD_ON` / `CONFIRM_HOLD_OFF`. New types: `CONFIRM_PLAY`, `CONFIRM_STOP`.
- `showPotBargraph()`: 1 param → 3 params. All callers must update.

## 11. Priority State Machine (updated)

```
1. Boot mode           (progressive white fill / red failure blink)
2. Setup comet         (violet sci-fi chase — new, replaces chase for setup)
3. Chase pattern       (violet ping-pong — calibration entry only)
4. Error               (LEDs 4-5 blink red)
5. Battery gauge       (gradient bar, solid, 3s)
6. Pot bargraph        (catch visualization: dim bar + bright cursor / full bar)
7. Confirmation blinks (bank/scale/hold/play/stop/octave, auto-expire)
8. Calibration mode    (all off + validation flash)
9. Normal bank display (multi-bank RGBW state with sine pulse + tick flash)
```
