# RGB LED (SK6812 RGBW) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace 8 single-color PWM LEDs with an 8x SK6812 RGBW NeoPixel Stick, adding color-coded display states.

**Architecture:** Swap the output layer in LedController from per-pin `analogWrite`/`digitalWrite` to a pixel buffer + single `strip.show()`. All state machine logic stays, only output encoding changes (uint8_t brightness → RGBW color). Upstream callers (ScaleManager, PotRouter) gain finer-grained change types.

**Tech Stack:** ESP32-S3, Arduino framework, Adafruit NeoPixel library, PlatformIO

**Spec:** `docs/superpowers/specs/2026-03-22-rgb-led-design.md`

**Branch:** `feature/rgb-leds` (already created)

**Build command:** `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

**No unit tests** — this is an embedded project with no test framework. Verification is build + visual confirmation on hardware. Each task ends with a successful build.

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `platformio.ini` | Modify | Add Adafruit NeoPixel lib_deps |
| `src/core/HardwareConfig.h` | Modify | Replace LED pins with single data pin, add RGBW color palette, add new timing constants |
| `src/core/LedController.h` | Modify | NeoPixel strip object, extended ConfirmType enum (10 types), new members for fade/comet/play-flash, updated showPotBargraph signature |
| `src/core/LedController.cpp` | Modify | Full rewrite of output layer: pixel buffer writes + strip.show(), color state machine, fade-outs, comet chase, catch visualization |
| `src/managers/ScaleManager.h` | Modify | Add ScaleChangeType enum, split _scaleChanged into typed change |
| `src/managers/ScaleManager.cpp` | Modify | Set specific scale change type (root/mode/chromatic) |
| `src/managers/PotRouter.h` | Modify | Add getBargraphPotLevel(), isBargraphCaught() getters |
| `src/managers/PotRouter.cpp` | Modify | Expose pot position and catch state for bargraph |
| `src/main.cpp` | Modify | Update triggerConfirm calls with new enum types, update showPotBargraph call with 3 args, add play/stop confirmations |
| `src/setup/SetupManager.cpp` | Modify | Replace setCalibrationMode with startSetupComet/stopSetupComet |

---

## Task 1: Add NeoPixel Library Dependency

**Files:**
- Modify: `platformio.ini`

- [ ] **Step 1: Add Adafruit NeoPixel to lib_deps**

In `platformio.ini`, add the library to the existing `lib_deps`:

```ini
lib_deps =
  max22/ESP32-BLE-MIDI
  adafruit/Adafruit NeoPixel
```

- [ ] **Step 2: Build to verify library resolves**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: BUILD SUCCESS (library downloaded and compiled)

- [ ] **Step 3: Commit**

```bash
git add platformio.ini
git commit -m "Add Adafruit NeoPixel library dependency"
```

---

## Task 2: Update HardwareConfig.h — Pin & Color Constants

**Files:**
- Modify: `src/core/HardwareConfig.h`

- [ ] **Step 1: Replace 8 LED pin constants with single data pin**

Replace lines 33-42:
```cpp
// --- LEDs (x8) — Bank indicator LEDs ---
const uint8_t LED_PIN_1 = 4;   // Bank 1
const uint8_t LED_PIN_2 = 5;   // Bank 2
const uint8_t LED_PIN_3 = 6;   // Bank 3
const uint8_t LED_PIN_4 = 7;   // Bank 4
const uint8_t LED_PIN_5 = 15;  // Bank 5
const uint8_t LED_PIN_6 = 16;  // Bank 6
const uint8_t LED_PIN_7 = 17;  // Bank 7
const uint8_t LED_PIN_8 = 18;  // Bank 8
const int NUM_LEDS = 8;
```

With:
```cpp
// --- LEDs — 8x SK6812 RGBW NeoPixel Stick (Adafruit product 2868) ---
const uint8_t LED_DATA_PIN = 4;  // Single GPIO for NeoPixel data line
const int NUM_LEDS = 8;
```

- [ ] **Step 2: Add RGBW color type and palette**

Add after the `NUM_LEDS` line, still inside section 2:
```cpp
// --- RGBW Color Type ---
struct RGBW {
  uint8_t r, g, b, w;
};

// --- Color Palette ---
// Base colors
const RGBW COL_WHITE       = {  0,   0,   0, 255};  // NORMAL foreground
const RGBW COL_WHITE_DIM   = {  0,   0,   0,  40};  // NORMAL background
const RGBW COL_BLUE        = {  0,   0, 255,   0};  // ARPEG foreground
const RGBW COL_BLUE_DIM    = {  0,   0,  40,   0};  // ARPEG background

// Scale confirmations (yellow, 3 saturations)
const RGBW COL_SCALE_ROOT  = {255, 200,   0,   0};  // Root — vivid yellow
const RGBW COL_SCALE_MODE  = {200, 160,   0,  60};  // Mode — pale yellow
const RGBW COL_SCALE_CHROM = {255, 140,   0,   0};  // Chromatic — golden yellow

// Arp confirmations (blue, 3 variations)
const RGBW COL_ARP_HOLD    = {  0,   0, 255,   0};  // Hold — deep blue
const RGBW COL_ARP_PLAY    = {  0,  80, 255,   0};  // Play/Stop — blue-cyan
const RGBW COL_PLAY_ACK    = {  0, 255,   0,   0};  // Play ack — green "go"
const RGBW COL_ARP_OCTAVE  = { 80,   0, 255,   0};  // Octave — blue-violet

// System
const RGBW COL_ERROR       = {255,   0,   0,   0};  // Error — red
const RGBW COL_BOOT        = {  0,   0,   0, 255};  // Boot — white
const RGBW COL_BOOT_FAIL   = {255,   0,   0,   0};  // Boot fail — red
const RGBW COL_SETUP       = {128,   0, 255,   0};  // Setup comet — violet

// Battery gauge gradient (LED 0 = red, LED 7 = green)
const RGBW COL_BATTERY[NUM_LEDS] = {
  {255,   0, 0, 0}, {255,  36, 0, 0}, {255,  73, 0, 0}, {255, 145, 0, 0},
  {200, 200, 0, 0}, {145, 255, 0, 0}, { 73, 255, 0, 0}, {  0, 255, 0, 0}
};
```

- [ ] **Step 3: Replace brightness constants with RGBW sine range constants**

Replace the entire section 5 "LED DISPLAY" (lines 186-223) with:
```cpp
// =================================================================
// 5. LED DISPLAY — RGBW Sine Ranges & Timing
// =================================================================
// Sine range values define the min/max of the modulated channel (B or W).
// All other channels in the pixel stay at 0 during sine modulation.

// --- Foreground ARPEG (modulate B channel) ---
const uint8_t LED_FG_ARP_STOP_MIN          = 77;   // Stopped: sine 30%
const uint8_t LED_FG_ARP_STOP_MAX          = 255;  // Stopped: sine 100%
const uint8_t LED_FG_ARP_PLAY_MIN          = 77;   // Playing: sine 30%
const uint8_t LED_FG_ARP_PLAY_MAX          = 204;  // Playing: sine 80%

// --- Background ARPEG (modulate B channel, dimmed) ---
const uint8_t LED_BG_ARP_STOP_MIN          = 20;   // Stopped: sine 8%
const uint8_t LED_BG_ARP_STOP_MAX          = 64;   // Stopped: sine 25%
const uint8_t LED_BG_ARP_PLAY_MIN          = 20;   // Playing: sine 8%
const uint8_t LED_BG_ARP_PLAY_MAX          = 51;   // Playing: sine 20%
const uint8_t LED_BG_ARP_PLAY_FLASH        = 64;   // Playing: tick flash spike 25%

// --- Pulse & Flash Timing ---
const uint16_t LED_PULSE_PERIOD_MS         = 1472; // Sine pulse period (~1.5s)
const uint8_t  LED_TICK_FLASH_DURATION_MS  = 30;   // Tick flash spike duration

// --- Confirmation Blinks ---
const uint8_t  LED_CONFIRM_UNIT_MS         = 50;   // Base phase unit
const uint8_t  LED_CONFIRM_BANK_PHASES     = 6;    // Bank switch: triple blink = 300ms
const uint8_t  LED_CONFIRM_SCALE_PHASES    = 4;    // Scale change: double blink = 200ms
const uint8_t  LED_CONFIRM_HOLD_ON_MS      = 150;  // Hold ON: blink on phase
const uint8_t  LED_CONFIRM_HOLD_TOTAL_MS   = 250;  // Hold ON: total duration
const uint16_t LED_CONFIRM_FADE_MS         = 300;  // Hold OFF + Stop: fade-out duration
const uint8_t  LED_CONFIRM_PLAY_STEPS      = 4;    // Play: total flashes (1 ack + 3 beat-synced)
const uint8_t  LED_CONFIRM_OCTAVE_PHASES   = 6;    // Octave: triple blink = 300ms
const uint8_t  LED_CONFIRM_BRIGHTNESS_PCT  = 50;   // Bank switch blink brightness (% of global)

// --- Setup Comet Chase ---
const uint8_t  LED_SETUP_CHASE_SPEED_MS    = 180;  // Time per step (~2.5s round trip)

// --- Bargraph Duration (configurable via Tool 5) ---
const uint16_t LED_BARGRAPH_DURATION_MIN     = 1000;
const uint16_t LED_BARGRAPH_DURATION_MAX     = 10000;
const uint16_t LED_BARGRAPH_DURATION_DEFAULT = 3000;
```

- [ ] **Step 4: Build to verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: Errors from LedController.cpp (references removed constants like `LED_PIN_1`, `LED_FG_NORMAL_BRIGHTNESS`). This is expected — we fix those in Task 4.

- [ ] **Step 5: Commit**

```bash
git add src/core/HardwareConfig.h
git commit -m "HardwareConfig: replace 8 LED pins with NeoPixel data pin, add RGBW palette"
```

---

## Task 3: Update ScaleManager — Split Scale Change Types

**Files:**
- Modify: `src/managers/ScaleManager.h`
- Modify: `src/managers/ScaleManager.cpp`

- [ ] **Step 1: Add ScaleChangeType enum and replace _scaleChanged bool**

In `ScaleManager.h`, add the enum before the class:
```cpp
enum ScaleChangeType : uint8_t {
  SCALE_CHANGE_NONE     = 0,
  SCALE_CHANGE_ROOT     = 1,
  SCALE_CHANGE_MODE     = 2,
  SCALE_CHANGE_CHROMATIC = 3,
};
```

In the class, replace:
```cpp
bool hasScaleChanged();  // True if scale was modified this frame (auto-clears)
```
With:
```cpp
ScaleChangeType consumeScaleChange();  // Returns change type, auto-clears to NONE
```

In private members, replace:
```cpp
bool    _scaleChanged;     // Set by processScalePads, cleared by hasScaleChanged()
```
With:
```cpp
ScaleChangeType _scaleChangeType;  // Set by processScalePads, cleared by consumeScaleChange()
```

- [ ] **Step 2: Update ScaleManager.cpp**

In constructor, replace `_scaleChanged(false)` with `_scaleChangeType(SCALE_CHANGE_NONE)`.

Replace the `hasScaleChanged()` method:
```cpp
ScaleChangeType ScaleManager::consumeScaleChange() {
  ScaleChangeType t = _scaleChangeType;
  _scaleChangeType = SCALE_CHANGE_NONE;
  return t;
}
```

In `processScalePads()`, replace each `_scaleChanged = true;` with the specific type:
- Root pad pressed (around line 142): `_scaleChangeType = SCALE_CHANGE_ROOT;`
- Mode pad pressed (around line 164): `_scaleChangeType = SCALE_CHANGE_MODE;`
- Chromatic pad pressed (around line 182): `_scaleChangeType = SCALE_CHANGE_CHROMATIC;`

- [ ] **Step 3: Update main.cpp caller**

In `main.cpp` around line 489, replace:
```cpp
bool scaleChanged  = s_scaleManager.hasScaleChanged();
```
With:
```cpp
ScaleChangeType scaleChange = s_scaleManager.consumeScaleChange();
bool scaleChanged = (scaleChange != SCALE_CHANGE_NONE);
```

Around line 504, replace:
```cpp
s_leds.triggerConfirm(CONFIRM_SCALE);
```
With:
```cpp
switch (scaleChange) {
  case SCALE_CHANGE_ROOT:     s_leds.triggerConfirm(CONFIRM_SCALE_ROOT); break;
  case SCALE_CHANGE_MODE:     s_leds.triggerConfirm(CONFIRM_SCALE_MODE); break;
  case SCALE_CHANGE_CHROMATIC: s_leds.triggerConfirm(CONFIRM_SCALE_CHROM); break;
  default: break;
}
```

- [ ] **Step 4: Update hold toggle in main.cpp**

Around line 517, replace:
```cpp
s_leds.triggerConfirm(CONFIRM_HOLD);
```
With:
```cpp
{
  BankSlot& holdSlot = s_bankManager.getCurrentSlot();
  bool holdIsOn = (holdSlot.type == BANK_ARPEG && holdSlot.arpEngine && holdSlot.arpEngine->isHoldOn());
  s_leds.triggerConfirm(holdIsOn ? CONFIRM_HOLD_ON : CONFIRM_HOLD_OFF);
}
```

- [ ] **Step 5: Commit (won't build yet — CONFIRM_SCALE_ROOT etc. not defined in LedController.h)**

```bash
git add src/managers/ScaleManager.h src/managers/ScaleManager.cpp src/main.cpp
git commit -m "ScaleManager: split scale change into root/mode/chromatic types"
```

---

## Task 4: Update PotRouter — Expose Catch State for Bargraph

**Files:**
- Modify: `src/managers/PotRouter.h`
- Modify: `src/managers/PotRouter.cpp`

- [ ] **Step 1: Add bargraph pot level and catch getters to PotRouter.h**

Add alongside existing `getBargraphLevel()` declaration:
```cpp
uint8_t getBargraphPotLevel() const;   // Physical pot position mapped to 0-7
bool    isBargraphCaught() const;      // True if active binding is caught
```

Add private members:
```cpp
uint8_t _bargraphPotLevel;   // Physical pot position (0-7)
bool    _bargraphCaught;     // Catch state of active binding
```

- [ ] **Step 2: Implement in PotRouter.cpp**

Initialize in constructor:
```cpp
, _bargraphPotLevel(0)
, _bargraphCaught(false)
```

Add getter implementations alongside existing `getBargraphLevel()`:
```cpp
uint8_t PotRouter::getBargraphPotLevel() const { return _bargraphPotLevel; }
bool    PotRouter::isBargraphCaught() const    { return _bargraphCaught; }
```

In `applyBinding()`, where `_bargraphLevel` is set (two places):
- **Before catch** (around line 413, the early return path): also set `_bargraphPotLevel`:
  ```cpp
  _bargraphPotLevel = (uint8_t)(adc * 7.0f / 4095.0f);
  _bargraphCaught = false;
  ```
- **After catch** (around line 522): also set pot level and caught state:
  ```cpp
  _bargraphPotLevel = (uint8_t)(adc * 7.0f / 4095.0f);
  _bargraphCaught = true;
  ```

- [ ] **Step 3: Update main.cpp showPotBargraph call**

Around line 717, replace:
```cpp
s_leds.showPotBargraph(s_potRouter.getBargraphLevel());
```
With:
```cpp
s_leds.showPotBargraph(
  s_potRouter.getBargraphLevel(),
  s_potRouter.getBargraphPotLevel(),
  s_potRouter.isBargraphCaught()
);
```

- [ ] **Step 4: Commit (won't build yet — LedController signature not updated)**

```bash
git add src/managers/PotRouter.h src/managers/PotRouter.cpp src/main.cpp
git commit -m "PotRouter: expose pot position and catch state for bargraph"
```

---

## Task 5: Rewrite LedController.h — NeoPixel + Extended Enum

**Files:**
- Modify: `src/core/LedController.h`

- [ ] **Step 1: Replace header with NeoPixel-based version**

Replace the entire file content with:
```cpp
#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "HardwareConfig.h"
#include <Adafruit_NeoPixel.h>
#include <stdint.h>

// Forward declarations
struct BankSlot;
class ArpEngine;
class ClockManager;

// =================================================================
// Confirmation blink types
// =================================================================
enum ConfirmType : uint8_t {
  CONFIRM_NONE         = 0,
  CONFIRM_BANK_SWITCH  = 1,
  CONFIRM_SCALE_ROOT   = 2,
  CONFIRM_SCALE_MODE   = 3,
  CONFIRM_SCALE_CHROM  = 4,
  CONFIRM_HOLD_ON      = 5,
  CONFIRM_HOLD_OFF     = 6,
  CONFIRM_PLAY         = 7,
  CONFIRM_STOP         = 8,
  CONFIRM_OCTAVE       = 9,
};

class LedController {
public:
  LedController();
  void begin();
  void update();

  // Brightness (0-255)
  void setBrightness(uint8_t brightness);

  // Bank display
  void setCurrentBank(uint8_t bank);
  void setBatteryLow(bool low);

  // Multi-bank state
  void setBankSlots(const BankSlot* slots);

  // Clock manager (for play confirmation beat sync)
  void setClockManager(const ClockManager* clock);

  // Confirmations
  void triggerConfirm(ConfirmType type, uint8_t param = 0);

  // Bargraph persistence
  void setPotBarDuration(uint16_t ms);

  // Boot
  void showBootProgress(uint8_t step);
  void showBootFailure(uint8_t step);
  void endBoot();

  // I2C error halt
  void haltI2CError();

  // Chase (calibration entry)
  void startChase();
  void stopChase();

  // Setup comet (active during Tools 1-6)
  void startSetupComet();
  void stopSetupComet();

  // Error
  void setError(bool error);

  // Battery gauge
  void showBatteryGauge(uint8_t percent);

  // Pot bargraph with catch visualization
  void showPotBargraph(uint8_t realLevel, uint8_t potLevel, bool caught);

  // Calibration
  void setCalibrationMode(bool active);
  void playValidation();

  // All off
  void allOff();

private:
  Adafruit_NeoPixel _strip;

  // Helper: set pixel from RGBW struct
  void setPixel(uint8_t i, const RGBW& color);
  void setPixelScaled(uint8_t i, const RGBW& color, uint8_t scale);
  void clearPixels();

  // Brightness
  uint8_t _brightness;

  // Bank display
  uint8_t _currentBank;
  bool _batteryLow;

  // Multi-bank state
  const BankSlot* _slots;
  uint8_t _sineTable[64];
  unsigned long _flashStartTime[NUM_LEDS];

  // Clock manager (for play beat detection)
  const ClockManager* _clock;

  // Confirmation state
  ConfirmType   _confirmType;
  unsigned long _confirmStart;
  uint8_t       _confirmParam;

  // Fade-out state (hold-off, stop)
  unsigned long _fadeStartTime;
  RGBW          _fadeColor;

  // Play confirmation state
  uint8_t       _playFlashPhase;    // 0=ack done, 1-3=beat flashes
  uint32_t      _playLastBeatTick;  // Clock tick at last beat flash

  // Bargraph
  uint16_t _potBarDurationMs;
  bool _showingPotBar;
  uint8_t _potBarRealLevel;
  uint8_t _potBarPotLevel;
  bool    _potBarCaught;
  unsigned long _potBarStart;

  // Boot
  bool _bootMode;
  uint8_t _bootStep;
  uint8_t _bootFailStep;

  // Chase (calibration entry)
  bool _chaseActive;
  uint8_t _chasePos;
  unsigned long _chaseLastStep;

  // Setup comet
  bool _setupComet;
  uint8_t _cometPos;          // 0-13 (ping-pong: 0-7 forward, 8-13 = 6 down to 1)
  unsigned long _cometLastStep;

  // Calibration
  bool _calibrationMode;
  bool _validationFlashing;
  unsigned long _validationFlashStart;

  // Error
  bool _error;

  // Blink timer
  unsigned long _lastBlinkTime;
  bool _blinkState;

  // Battery gauge
  bool _showingBattery;
  uint8_t _batteryLeds;
  unsigned long _batteryDisplayStart;

  // Battery low blink
  unsigned long _batLowLastBurstTime;
};

#endif // LED_CONTROLLER_H
```

- [ ] **Step 2: Commit (won't build — .cpp not updated yet)**

```bash
git add src/core/LedController.h
git commit -m "LedController.h: NeoPixel strip, extended ConfirmType enum, new API"
```

---

## Task 6: Rewrite LedController.cpp — Core Output Layer

This is the largest task. Rewrite the entire .cpp file to use the pixel buffer.

**Files:**
- Modify: `src/core/LedController.cpp`

- [ ] **Step 1: Rewrite constructor, begin(), helpers, and allOff()**

Replace the entire file. Start with the core infrastructure:

```cpp
#include "LedController.h"
#include "HardwareConfig.h"
#include "KeyboardData.h"
#include "../arp/ArpEngine.h"
#include "../midi/ClockManager.h"
#include <Arduino.h>
#include <math.h>

LedController::LedController()
  : _strip(NUM_LEDS, LED_DATA_PIN, NEO_GRBW + NEO_KHZ800),
    _brightness(255),
    _currentBank(0),
    _batteryLow(false),
    _slots(nullptr),
    _clock(nullptr),
    _confirmType(CONFIRM_NONE),
    _confirmStart(0),
    _confirmParam(0),
    _fadeStartTime(0),
    _fadeColor{0,0,0,0},
    _playFlashPhase(0),
    _playLastBeatTick(0),
    _potBarDurationMs(LED_BARGRAPH_DURATION_DEFAULT),
    _showingPotBar(false),
    _potBarRealLevel(0),
    _potBarPotLevel(0),
    _potBarCaught(false),
    _potBarStart(0),
    _bootMode(false),
    _bootStep(0),
    _bootFailStep(0),
    _chaseActive(false),
    _chasePos(0),
    _chaseLastStep(0),
    _setupComet(false),
    _cometPos(0),
    _cometLastStep(0),
    _calibrationMode(false),
    _validationFlashing(false),
    _validationFlashStart(0),
    _error(false),
    _lastBlinkTime(0),
    _blinkState(false),
    _showingBattery(false),
    _batteryLeds(0),
    _batteryDisplayStart(0),
    _batLowLastBurstTime(0)
{
  for (uint8_t i = 0; i < 64; i++) {
    _sineTable[i] = (uint8_t)(127.5f + 127.5f * sinf((float)i * 6.2831853f / 64.0f));
  }
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    _flashStartTime[i] = 0;
  }
}

void LedController::begin() {
  _strip.begin();
  _strip.show();  // All off. No setBrightness() — scaling is per-pixel.
}

// All pixel writes apply _brightness scaling manually.
// Do NOT use strip.setBrightness() — it's lossy (only rescales on change,
// and we rewrite all pixels every frame).
void LedController::setPixel(uint8_t i, const RGBW& c) {
  _strip.setPixelColor(i, _strip.Color(
    (uint8_t)((uint16_t)c.r * _brightness / 255),
    (uint8_t)((uint16_t)c.g * _brightness / 255),
    (uint8_t)((uint16_t)c.b * _brightness / 255),
    (uint8_t)((uint16_t)c.w * _brightness / 255)
  ));
}

void LedController::setPixelScaled(uint8_t i, const RGBW& c, uint8_t scale) {
  // Apply both the per-pixel scale AND global brightness
  uint16_t combinedScale = (uint16_t)scale * _brightness / 255;
  _strip.setPixelColor(i, _strip.Color(
    (uint8_t)((uint16_t)c.r * combinedScale / 255),
    (uint8_t)((uint16_t)c.g * combinedScale / 255),
    (uint8_t)((uint16_t)c.b * combinedScale / 255),
    (uint8_t)((uint16_t)c.w * combinedScale / 255)
  ));
}

void LedController::clearPixels() {
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    _strip.setPixelColor(i, 0);
  }
}
```

- [ ] **Step 2: Write haltI2CError() and chase/comet methods**

Append to the file:
```cpp
void LedController::haltI2CError() {
  while (true) {
    for (uint8_t flash = 0; flash < 3; flash++) {
      for (int i = 0; i < NUM_LEDS; i++) setPixel(i, COL_ERROR);
      _strip.show();
      delay(80);
      clearPixels();
      _strip.show();
      delay(80);
    }
    delay(1000);
  }
}

void LedController::startChase() {
  _chaseActive = true;
  _chasePos = 0;
  _chaseLastStep = millis();
  clearPixels();
  setPixel(0, COL_SETUP);
  _strip.show();
}

void LedController::stopChase() {
  _chaseActive = false;
  clearPixels();
  _strip.show();
}

void LedController::startSetupComet() {
  _setupComet = true;
  _cometPos = 0;
  _cometLastStep = millis();
}

void LedController::stopSetupComet() {
  _setupComet = false;
  clearPixels();
  _strip.show();
}
```

- [ ] **Step 3: Write the update() method — priority states (boot through error)**

Append:
```cpp
void LedController::update() {
  unsigned long now = millis();

  // Shared 500ms blink timer
  if (now - _lastBlinkTime >= 500) {
    _blinkState = !_blinkState;
    _lastBlinkTime = now;
  }

  // NOTE: Do NOT use _strip.setBrightness() — it's lossy and only rescales
  // when brightness changes. Since we rewrite all pixels every frame,
  // brightness is applied per-pixel in setPixel()/setPixelScaled() instead.

  // === Boot mode ===
  if (_bootMode) {
    clearPixels();
    if (_bootFailStep > 0) {
      bool fastBlink = ((now / 150) % 2) == 0;
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i < _bootFailStep - 1) {
          setPixel(i, COL_BOOT);
        } else if (i == _bootFailStep - 1 && fastBlink) {
          setPixel(i, COL_BOOT_FAIL);
        }
      }
    } else {
      for (int i = 0; i < _bootStep; i++) {
        setPixel(i, COL_BOOT);
      }
    }
    _strip.show();
    return;
  }

  // === Setup comet (sci-fi violet) ===
  if (_setupComet && !_calibrationMode) {
    if (now - _cometLastStep >= LED_SETUP_CHASE_SPEED_MS) {
      _cometPos++;
      if (_cometPos >= 14) _cometPos = 0;  // 0-7 forward, 8-13 backward
      _cometLastStep = now;
    }
    clearPixels();
    // Convert ping-pong position to LED index
    uint8_t headIdx = (_cometPos < 8) ? _cometPos : (14 - _cometPos);
    setPixel(headIdx, COL_SETUP);
    // Trail -1
    int8_t t1 = (_cometPos < 8) ? (headIdx - 1) : (headIdx + 1);
    if (t1 >= 0 && t1 < NUM_LEDS) setPixelScaled(t1, COL_SETUP, 102);  // 40%
    // Trail -2
    int8_t t2 = (_cometPos < 8) ? (headIdx - 2) : (headIdx + 2);
    if (t2 >= 0 && t2 < NUM_LEDS) setPixelScaled(t2, COL_SETUP, 26);   // 10%
    _strip.show();
    return;
  }

  // === Chase pattern (calibration entry) ===
  if (_chaseActive) {
    if (now - _chaseLastStep >= CHASE_STEP_MS) {
      _chasePos = (_chasePos + 1) % NUM_LEDS;
      _chaseLastStep = now;
    }
    clearPixels();
    setPixel(_chasePos, COL_SETUP);
    _strip.show();
    return;
  }

  // === Error (LEDs 4-5 blink red) ===
  if (_error) {
    clearPixels();
    if (_blinkState) {
      setPixel(3, COL_ERROR);  // LED 4 (0-indexed)
      setPixel(4, COL_ERROR);  // LED 5 (0-indexed)
    }
    _strip.show();
    return;
  }
```

- [ ] **Step 4: Continue update() — battery gauge and pot bargraph**

Append inside `update()`:
```cpp
  // === Battery gauge (gradient bar, solid) ===
  if (_showingBattery) {
    if (now - _batteryDisplayStart < BAT_DISPLAY_DURATION_MS) {
      clearPixels();
      for (int i = 0; i < _batteryLeds; i++) {
        setPixel(i, COL_BATTERY[i]);
      }
      _strip.show();
      return;
    }
    _showingBattery = false;
  }

  // === Pot bargraph (catch visualization) ===
  if (_showingPotBar) {
    if (now - _potBarStart >= _potBarDurationMs) {
      _showingPotBar = false;
    } else {
      clearPixels();
      // Determine context color
      RGBW colFull = COL_WHITE;
      RGBW colDim  = COL_WHITE_DIM;
      if (_slots && _slots[_currentBank].type == BANK_ARPEG) {
        colFull = COL_BLUE;
        colDim  = COL_BLUE_DIM;
      }

      if (_potBarCaught) {
        // After catch: full-color bar
        for (int i = 0; i < _potBarRealLevel; i++) {
          setPixel(i, colFull);
        }
      } else {
        // Before catch: dim bar (real value) + bright cursor (pot position)
        for (int i = 0; i < _potBarRealLevel; i++) {
          setPixel(i, colDim);
        }
        if (_potBarPotLevel < NUM_LEDS) {
          setPixel(_potBarPotLevel, colFull);  // Cursor overrides dim if overlapping
        }
      }
      _strip.show();
      return;
    }
  }
```

- [ ] **Step 5: Continue update() — confirmations**

Append inside `update()`:
```cpp
  // === Confirmations ===
  if (_confirmType != CONFIRM_NONE) {
    unsigned long elapsed = now - _confirmStart;
    bool handled = true;

    switch (_confirmType) {
      case CONFIRM_BANK_SWITCH: {
        uint16_t totalMs = (uint16_t)LED_CONFIRM_BANK_PHASES * LED_CONFIRM_UNIT_MS;
        if (elapsed < totalMs) {
          uint8_t phase = elapsed / LED_CONFIRM_UNIT_MS;
          bool on = (phase % 2 == 0);
          // Normal display for all LEDs first (handled below), then overlay bank LED
          // For simplicity: clear and show only the blink
          // Actually: we need normal display underneath. Build normal first, then overlay.
          // Let's fall through to normal display but override the bank LED.
          // Simpler approach: just blink the destination LED, others at normal
          goto normalDisplay;  // Render normal, then overlay in post
        }
        _confirmType = CONFIRM_NONE;
        handled = false;
        break;
      }

      case CONFIRM_SCALE_ROOT:
      case CONFIRM_SCALE_MODE:
      case CONFIRM_SCALE_CHROM: {
        uint16_t totalMs = (uint16_t)LED_CONFIRM_SCALE_PHASES * LED_CONFIRM_UNIT_MS;
        if (elapsed >= totalMs) {
          _confirmType = CONFIRM_NONE;
          handled = false;
          break;
        }
        uint8_t phase = elapsed / LED_CONFIRM_UNIT_MS;
        bool on = (phase % 2 == 0);
        // Pick color based on type
        RGBW col = COL_SCALE_ROOT;
        if (_confirmType == CONFIRM_SCALE_MODE) col = COL_SCALE_MODE;
        else if (_confirmType == CONFIRM_SCALE_CHROM) col = COL_SCALE_CHROM;
        clearPixels();
        if (on) setPixel(_currentBank, col);
        _strip.show();
        return;
      }

      case CONFIRM_HOLD_ON: {
        if (elapsed >= LED_CONFIRM_HOLD_TOTAL_MS) {
          _confirmType = CONFIRM_NONE;
          handled = false;
          break;
        }
        bool on = (elapsed < LED_CONFIRM_HOLD_ON_MS);
        clearPixels();
        if (on) setPixel(_currentBank, COL_ARP_HOLD);
        _strip.show();
        return;
      }

      case CONFIRM_HOLD_OFF: {
        if (elapsed >= LED_CONFIRM_FADE_MS) {
          _confirmType = CONFIRM_NONE;
          handled = false;
          break;
        }
        uint8_t fadeScale = 255 - (uint8_t)((uint32_t)elapsed * 255 / LED_CONFIRM_FADE_MS);
        clearPixels();
        setPixelScaled(_currentBank, COL_ARP_HOLD, fadeScale);
        _strip.show();
        return;
      }

      case CONFIRM_PLAY: {
        // Phase 0: immediate green ack (already fired at triggerConfirm time)
        // Phase 1-3: beat-synced blue-cyan flashes
        if (_playFlashPhase == 0) {
          // Ack flash
          if (elapsed < LED_TICK_FLASH_DURATION_MS) {
            clearPixels();
            setPixel(_currentBank, COL_PLAY_ACK);
            _strip.show();
            return;
          }
          _playFlashPhase = 1;  // Move to waiting for beat flashes
          _playLastBeatTick = _clock ? _clock->getCurrentTick() : 0;
        }
        // Wait for arp to start playing, then count beats
        if (_playFlashPhase >= 1 && _playFlashPhase <= 3) {
          if (_clock) {
            uint32_t currentTick = _clock->getCurrentTick();
            uint32_t ticksSinceLast = currentTick - _playLastBeatTick;
            if (ticksSinceLast >= 24) {
              // Beat boundary — fire flash
              uint8_t intensity = (uint8_t)((uint16_t)_playFlashPhase * 255 / 3);  // 85, 170, 255
              clearPixels();
              setPixelScaled(_currentBank, COL_ARP_PLAY, intensity);
              _strip.show();
              _playLastBeatTick = currentTick;
              _playFlashPhase++;
              if (_playFlashPhase > 3) {
                _confirmType = CONFIRM_NONE;
              }
              return;
            }
          }
          // Between beats: show nothing (fall through to normal display)
          handled = false;
        }
        if (_playFlashPhase > 3) {
          _confirmType = CONFIRM_NONE;
          handled = false;
        }
        break;
      }

      case CONFIRM_STOP: {
        if (elapsed >= LED_CONFIRM_FADE_MS) {
          _confirmType = CONFIRM_NONE;
          handled = false;
          break;
        }
        uint8_t fadeScale = 255 - (uint8_t)((uint32_t)elapsed * 255 / LED_CONFIRM_FADE_MS);
        clearPixels();
        setPixelScaled(_currentBank, COL_ARP_PLAY, fadeScale);
        _strip.show();
        return;
      }

      case CONFIRM_OCTAVE: {
        uint16_t totalMs = (uint16_t)LED_CONFIRM_OCTAVE_PHASES * LED_CONFIRM_UNIT_MS;
        if (elapsed >= totalMs) {
          _confirmType = CONFIRM_NONE;
          handled = false;
          break;
        }
        uint8_t phase = elapsed / LED_CONFIRM_UNIT_MS;
        bool on = (phase % 2 == 0);
        clearPixels();
        if (on && _confirmParam >= 1 && _confirmParam <= 4) {
          uint8_t startLed = (_confirmParam - 1) * 2;
          setPixel(startLed, COL_ARP_OCTAVE);
          setPixel(startLed + 1, COL_ARP_OCTAVE);
        }
        _strip.show();
        return;
      }

      default:
        _confirmType = CONFIRM_NONE;
        handled = false;
        break;
    }
    if (handled && _confirmType != CONFIRM_NONE) return;
  }
```

- [ ] **Step 6: Continue update() — calibration and normal display**

Append inside `update()`:
```cpp
  // === Calibration mode ===
  if (_calibrationMode) {
    if (_validationFlashing) {
      unsigned long elapsed = now - _validationFlashStart;
      if (elapsed >= 150) {
        _validationFlashing = false;
      } else {
        uint8_t phase = elapsed / 25;
        bool on = (phase < 6) && (phase % 2 == 0);
        clearPixels();
        if (on) {
          for (int i = 0; i < NUM_LEDS; i++) setPixel(i, COL_BOOT);
        }
        _strip.show();
        return;
      }
    }
    clearPixels();
    _strip.show();
    return;
  }

  // === Normal bank display ===
normalDisplay:
  if (_slots) {
    const uint8_t lutStep = LED_PULSE_PERIOD_MS / 64;
    uint8_t sineIdx = (uint8_t)((now / lutStep) % 64);
    uint8_t sineRaw = _sineTable[sineIdx];

    for (int i = 0; i < NUM_LEDS; i++) {
      const BankSlot& slot = _slots[i];
      bool isFg = (i == _currentBank);

      if (slot.type == BANK_NORMAL) {
        if (isFg) {
          RGBW col = COL_WHITE;
          // Battery low override
          if (_batteryLow) {
            unsigned long elapsed = now - _batLowLastBurstTime;
            if (elapsed >= BAT_LOW_BLINK_INTERVAL_MS) {
              _batLowLastBurstTime = now;
              elapsed = 0;
            }
            uint32_t burstDuration = (uint32_t)BAT_LOW_BLINK_SPEED_MS * 6;
            if (elapsed < burstDuration) {
              uint8_t phase = elapsed / BAT_LOW_BLINK_SPEED_MS;
              if (phase % 2 != 0) col = {0, 0, 0, 0};
            }
          }
          setPixel(i, col);
        } else {
          setPixel(i, COL_WHITE_DIM);
        }
      } else {
        // ARPEG bank
        bool playing = slot.arpEngine && slot.arpEngine->isPlaying() && slot.arpEngine->hasNotes();

        if (slot.arpEngine && slot.arpEngine->consumeTickFlash()) {
          _flashStartTime[i] = now;
        }
        bool flashing = (_flashStartTime[i] != 0) &&
                         ((now - _flashStartTime[i]) < LED_TICK_FLASH_DURATION_MS);

        if (isFg) {
          if (playing) {
            if (flashing) {
              setPixel(i, COL_WHITE);  // White flash on foreground
            } else {
              uint8_t bVal = LED_FG_ARP_PLAY_MIN + (uint8_t)((uint16_t)sineRaw *
                       (LED_FG_ARP_PLAY_MAX - LED_FG_ARP_PLAY_MIN) / 255);
              RGBW c = {0, 0, bVal, 0};
              setPixel(i, c);
            }
          } else {
            uint8_t bVal = LED_FG_ARP_STOP_MIN + (uint8_t)((uint16_t)sineRaw *
                     (LED_FG_ARP_STOP_MAX - LED_FG_ARP_STOP_MIN) / 255);
            RGBW c = {0, 0, bVal, 0};
            setPixel(i, c);
          }
        } else {
          if (playing) {
            if (flashing) {
              RGBW c = {0, 0, LED_BG_ARP_PLAY_FLASH, 0};
              setPixel(i, c);
            } else {
              uint8_t bVal = LED_BG_ARP_PLAY_MIN + (uint8_t)((uint16_t)sineRaw *
                       (LED_BG_ARP_PLAY_MAX - LED_BG_ARP_PLAY_MIN) / 255);
              RGBW c = {0, 0, bVal, 0};
              setPixel(i, c);
            }
          } else {
            uint8_t bVal = LED_BG_ARP_STOP_MIN + (uint8_t)((uint16_t)sineRaw *
                     (LED_BG_ARP_STOP_MAX - LED_BG_ARP_STOP_MIN) / 255);
            RGBW c = {0, 0, bVal, 0};
            setPixel(i, c);
          }
        }
      }
    }

    // Bank switch confirmation overlay (destination LED only)
    if (_confirmType == CONFIRM_BANK_SWITCH) {
      unsigned long elapsed = now - _confirmStart;
      uint16_t totalMs = (uint16_t)LED_CONFIRM_BANK_PHASES * LED_CONFIRM_UNIT_MS;
      if (elapsed < totalMs) {
        uint8_t phase = elapsed / LED_CONFIRM_UNIT_MS;
        bool on = (phase % 2 == 0);
        RGBW bankCol = (_slots[_currentBank].type == BANK_ARPEG) ? COL_BLUE : COL_WHITE;
        if (on) {
          setPixelScaled(_currentBank, bankCol, (uint8_t)((uint16_t)255 * LED_CONFIRM_BRIGHTNESS_PCT / 100));
        } else {
          _strip.setPixelColor(_currentBank, 0);
        }
      } else {
        _confirmType = CONFIRM_NONE;
      }
    }
  } else {
    clearPixels();
    setPixel(_currentBank, COL_WHITE);
  }

  _strip.show();
}
```

- [ ] **Step 7: Write remaining methods (setters, boot, battery, bargraph, calibration, allOff)**

Append:
```cpp
void LedController::setBrightness(uint8_t brightness) {
  _brightness = brightness;
}

void LedController::setCurrentBank(uint8_t bank) {
  if (bank < NUM_BANKS) _currentBank = bank;
}

void LedController::setBatteryLow(bool low) {
  _batteryLow = low;
}

void LedController::setBankSlots(const BankSlot* slots) {
  _slots = slots;
}

void LedController::setClockManager(const ClockManager* clock) {
  _clock = clock;
}

void LedController::triggerConfirm(ConfirmType type, uint8_t param) {
  _confirmType = type;
  _confirmStart = millis();
  _confirmParam = param;
  if (type == CONFIRM_PLAY) {
    _playFlashPhase = 0;
    _playLastBeatTick = _clock ? _clock->getCurrentTick() : 0;
  }
}

void LedController::setPotBarDuration(uint16_t ms) {
  _potBarDurationMs = ms;
}

void LedController::showBootProgress(uint8_t step) {
  _bootStep = step;
  _bootMode = true;
}

void LedController::showBootFailure(uint8_t step) {
  _bootFailStep = step;
  _bootMode = true;
}

void LedController::endBoot() {
  _bootMode = false;
  _bootStep = 0;
  _bootFailStep = 0;
}

void LedController::setError(bool error) {
  _error = error;
}

void LedController::showBatteryGauge(uint8_t percent) {
  if (percent > 100) percent = 100;
  _batteryLeds = (percent * 8 + 50) / 100;
  _showingBattery = true;
  _batteryDisplayStart = millis();
}

void LedController::showPotBargraph(uint8_t realLevel, uint8_t potLevel, bool caught) {
  uint8_t newReal = (realLevel > NUM_LEDS) ? NUM_LEDS : realLevel;
  uint8_t newPot = (potLevel >= NUM_LEDS) ? (NUM_LEDS - 1) : potLevel;

  // Reset timer on any change
  _potBarStart = millis();
  _potBarRealLevel = newReal;
  _potBarPotLevel = newPot;
  _potBarCaught = caught;
  _showingPotBar = true;
}

void LedController::setCalibrationMode(bool active) {
  _calibrationMode = active;
}

void LedController::playValidation() {
  _validationFlashStart = millis();
  _validationFlashing = true;
}

void LedController::allOff() {
  clearPixels();
  _strip.show();
  _currentBank = 0;
  _batteryLow = false;
  _bootMode = false;
  _bootStep = 0;
  _bootFailStep = 0;
  _chaseActive = false;
  _setupComet = false;
  _calibrationMode = false;
  _validationFlashing = false;
  _error = false;
  _showingPotBar = false;
  _showingBattery = false;
  _confirmType = CONFIRM_NONE;
  for (uint8_t i = 0; i < NUM_LEDS; i++) _flashStartTime[i] = 0;
}
```

- [ ] **Step 8: Build**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: May have errors from callers not yet updated. Fix any remaining compile errors.

- [ ] **Step 9: Commit**

```bash
git add src/core/LedController.cpp
git commit -m "LedController.cpp: full NeoPixel rewrite with RGBW color state machine"
```

---

## Task 7: Update Callers — main.cpp and SetupManager

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/setup/SetupManager.cpp`

- [ ] **Step 1: Add ClockManager binding in main.cpp**

After `s_leds.setBankSlots(s_banks)` in the boot sequence, add:
```cpp
s_leds.setClockManager(&s_clockManager);
```

- [ ] **Step 2: Add play/stop confirmations in main.cpp**

Find the play/stop pad handling (around line 554). After the `playStop()` call, add confirmation:
```cpp
// After: slot.arpEngine->playStop(s_transport);
if (psSlot.arpEngine->isPlaying()) {
  s_leds.triggerConfirm(CONFIRM_PLAY);
} else {
  s_leds.triggerConfirm(CONFIRM_STOP);
}
```

- [ ] **Step 3: Update SetupManager for setup comet**

In `SetupManager.cpp`, replace:
```cpp
_leds->allOff();
_leds->setCalibrationMode(true);
```
With:
```cpp
_leds->allOff();
_leds->startSetupComet();
```

And on exit (before reboot), replace:
```cpp
_leds->setCalibrationMode(false);
_leds->allOff();
```
With:
```cpp
_leds->stopSetupComet();
_leds->allOff();
```

Note: individual tools that need `setCalibrationMode(true)` (like ToolCalibration) can still call it — it preempts the comet per the priority state machine.

- [ ] **Step 4: Build**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: BUILD SUCCESS

- [ ] **Step 5: Fix any remaining compile errors**

Check for any remaining references to removed constants (`LED_PIN_*`, `LED_FG_NORMAL_BRIGHTNESS`, `LED_BG_NORMAL_BRIGHTNESS`, `LED_FG_ARP_PLAY_FLASH`). Search and fix.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp src/setup/SetupManager.cpp
git commit -m "Wire up NeoPixel LED controller: clock binding, play/stop confirms, setup comet"
```

---

## Task 8: Final Build Verification & Cleanup

**Files:**
- All modified files

- [ ] **Step 1: Full clean build**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 --clean`
Expected: BUILD SUCCESS with no warnings related to LED code

- [ ] **Step 2: Review for any leftover references to old LED system**

Search for: `LED_PIN_`, `analogWrite`, `ledcDetachPin`, `LED_FG_NORMAL`, `LED_BG_NORMAL`, `CONFIRM_SCALE ` (with trailing space, to catch the old unsplit version), `CONFIRM_HOLD ` (same).

All should return zero matches (except possibly in comments or spec docs).

- [ ] **Step 3: Commit any fixes**

```bash
git add -A
git commit -m "Cleanup: remove leftover references to old single-color LED system"
```

---

## Execution Notes

- Tasks 1-2 can be done independently (library + config).
- Tasks 3-4 (ScaleManager split + PotRouter catch) can be done in parallel — they don't depend on each other.
- Task 5 (header) must come before Task 6 (implementation).
- Task 6 is the largest (~400 lines). The code above is complete — copy it.
- Task 7 wires everything together.
- Task 8 verifies.

**The project has no unit tests.** Verification is: does it compile? Hardware testing is done manually after upload.
