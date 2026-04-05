# LED Feedback Redesign — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Simplify the 8-LED feedback system by removing sine pulse on playing states, converting all confirmations to overlays, and adding a tempo bargraph.

**Architecture:** Five independent chunks that each compile. Data layer first (struct v3), then arpeg display (remove play pulse), then confirmations (all overlay), then tempo bargraph (new feature), then Tool 7 UI. Each chunk can be committed and tested independently.

**Tech Stack:** C++17, Arduino framework, ESP32-S3, PlatformIO. No test framework — verification is compile + flash + manual check.

**Build command:** `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

**Spec:** `docs/superpowers/specs/2026-04-05-led-feedback-redesign.md`

---

## Task 1: Data Layer — LedSettingsStore v3

**Files:**
- Modify: `src/core/KeyboardData.h:235-274` (struct + version + validation)
- Modify: `src/core/LedController.h:130-225` (member variables)
- Modify: `src/core/LedController.cpp:13-72` (constructor defaults)
- Modify: `src/core/LedController.cpp:692-730` (loadLedSettings)
- Modify: `src/managers/NvsManager.cpp:30-65` (defaults)

### 1a. KeyboardData.h — Bump version, replace struct fields

- [ ] **Step 1: Bump LED_SETTINGS_VERSION**

In `src/core/KeyboardData.h:235`, change:
```cpp
#define LED_SETTINGS_VERSION       3
```

- [ ] **Step 2: Replace fields in LedSettingsStore**

In `src/core/KeyboardData.h:237-274`, replace the confirmations section:
```cpp
struct LedSettingsStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  // --- Intensities (0-100, perceptual %) ---
  uint8_t  normalFgIntensity;     // default 85
  uint8_t  normalBgIntensity;     // default 10
  uint8_t  fgArpStopMin;          // default 30
  uint8_t  fgArpStopMax;          // default 100
  uint8_t  fgArpPlayMin;          // default 30  (unused in v3 — kept for struct compat)
  uint8_t  fgArpPlayMax;          // default 80  (solid intensity between tick flashes)
  uint8_t  bgArpStopMin;          // default 8   (solid dim intensity)
  uint8_t  bgArpStopMax;          // default 25  (unused in v3 — kept for struct compat)
  uint8_t  bgArpPlayMin;          // default 8   (solid dim intensity)
  uint8_t  bgArpPlayMax;          // default 20  (unused in v3 — kept for struct compat)
  uint8_t  tickFlashFg;           // default 100
  uint8_t  tickFlashBg;           // default 25
  // --- Timing ---
  uint16_t pulsePeriodMs;         // default 1472 (FG arp stopped-loaded pulse only)
  uint8_t  tickFlashDurationMs;   // default 30
  uint8_t  gammaTenths;           // 10-30 -> gamma 1.0-3.0, default 20 (2.0)
  // --- Confirmations ---
  uint8_t  bankBlinks;            // 1-3, default 3
  uint16_t bankDurationMs;        // default 300
  uint8_t  bankBrightnessPct;     // default 80
  uint8_t  scaleRootBlinks;       // default 2
  uint16_t scaleRootDurationMs;   // default 200
  uint8_t  scaleModeBlinks;       // default 2
  uint16_t scaleModeDurationMs;   // default 200
  uint8_t  scaleChromBlinks;      // default 2
  uint16_t scaleChromDurationMs;  // default 200
  uint16_t holdFadeMs;            // default 300 (fade IN for ON, fade OUT for OFF)
  uint8_t  playBlinks;            // 1-3, default 2
  uint16_t playDurationMs;        // 100-500, default 200
  uint8_t  stopBlinks;            // 1-3, default 2
  uint16_t stopDurationMs;        // 100-500, default 200
  uint8_t  octaveBlinks;          // default 3
  uint16_t octaveDurationMs;      // default 300
};
```

Note: `holdOnFlashMs`, `playBeatCount`, `stopFadeMs` are removed. `playBlinks`, `playDurationMs`, `stopBlinks`, `stopDurationMs` are added. `holdFadeMs` is kept (now shared for ON and OFF fades).

- [ ] **Step 3: Update validateLedSettingsStore**

In `src/core/KeyboardData.h:532-561`, replace the validation function:
```cpp
inline void validateLedSettingsStore(LedSettingsStore& s) {
  // Intensity cross-validation (min <= max for pulse range — FG stopped only)
  if (s.fgArpStopMin > s.fgArpStopMax) s.fgArpStopMax = s.fgArpStopMin;
  // Timing ranges
  if (s.pulsePeriodMs < 500)  s.pulsePeriodMs = 500;
  if (s.pulsePeriodMs > 4000) s.pulsePeriodMs = 4000;
  if (s.tickFlashDurationMs < 10)  s.tickFlashDurationMs = 10;
  if (s.tickFlashDurationMs > 100) s.tickFlashDurationMs = 100;
  if (s.gammaTenths < 10) s.gammaTenths = 10;
  if (s.gammaTenths > 30) s.gammaTenths = 30;
  // Confirmations
  if (s.bankBlinks < 1 || s.bankBlinks > 3) s.bankBlinks = 3;
  if (s.bankDurationMs < 100 || s.bankDurationMs > 500) s.bankDurationMs = 300;
  if (s.bankBrightnessPct > 100) s.bankBrightnessPct = 80;
  if (s.scaleRootBlinks < 1 || s.scaleRootBlinks > 3) s.scaleRootBlinks = 2;
  if (s.scaleRootDurationMs < 100 || s.scaleRootDurationMs > 500) s.scaleRootDurationMs = 200;
  if (s.scaleModeBlinks < 1 || s.scaleModeBlinks > 3) s.scaleModeBlinks = 2;
  if (s.scaleModeDurationMs < 100 || s.scaleModeDurationMs > 500) s.scaleModeDurationMs = 200;
  if (s.scaleChromBlinks < 1 || s.scaleChromBlinks > 3) s.scaleChromBlinks = 2;
  if (s.scaleChromDurationMs < 100 || s.scaleChromDurationMs > 500) s.scaleChromDurationMs = 200;
  if (s.holdFadeMs < 100 || s.holdFadeMs > 600) s.holdFadeMs = 300;
  if (s.playBlinks < 1 || s.playBlinks > 3) s.playBlinks = 2;
  if (s.playDurationMs < 100 || s.playDurationMs > 500) s.playDurationMs = 200;
  if (s.stopBlinks < 1 || s.stopBlinks > 3) s.stopBlinks = 2;
  if (s.stopDurationMs < 100 || s.stopDurationMs > 500) s.stopDurationMs = 200;
  if (s.octaveBlinks < 1 || s.octaveBlinks > 3) s.octaveBlinks = 3;
  if (s.octaveDurationMs < 100 || s.octaveDurationMs > 500) s.octaveDurationMs = 300;
}
```

### 1b. LedController.h — Update members

- [ ] **Step 4: Replace confirmation members**

In `src/core/LedController.h`, replace the settings members (lines 147-161):
```cpp
  uint8_t  _bankBlinks;
  uint16_t _bankDurationMs;
  uint8_t  _bankBrightnessPct;
  uint8_t  _scaleRootBlinks;
  uint16_t _scaleRootDurationMs;
  uint8_t  _scaleModeBlinks;
  uint16_t _scaleModeDurationMs;
  uint8_t  _scaleChromBlinks;
  uint16_t _scaleChromDurationMs;
  uint16_t _holdFadeMs;
  uint8_t  _playBlinks;
  uint16_t _playDurationMs;
  uint8_t  _stopBlinks;
  uint16_t _stopDurationMs;
  uint8_t  _octaveBlinks;
  uint16_t _octaveDurationMs;
```

- [ ] **Step 5: Replace play/hold state with new members**

In `src/core/LedController.h`, replace the play confirmation state (lines 174-177) and add hold fade direction:
```cpp
  // Hold fade direction (for overlay)
  bool _holdFadeIn;  // true = fade IN (ON), false = fade OUT (OFF)

  // Tempo bargraph
  bool     _potBarIsTempo;
  uint16_t _potBarBpm;
```

Remove these lines entirely (lines 166-167 and 174-177):
```
  // Clock manager (for play beat detection)
  const ClockManager* _clock;
  ...
  // Play confirmation state
  unsigned long _fadeStartTime;
  uint8_t       _playFlashPhase;
  uint32_t      _playLastBeatTick;
```

- [ ] **Step 6: Update renderBankArpeg signature**

In `src/core/LedController.h:118`, change the signature:
```cpp
  void renderBankArpeg(uint8_t led, bool isFg, unsigned long now);
```

The `uint16_t sine16` parameter is removed — sine computation moves inside for FG stopped-loaded only.

- [ ] **Step 7: Remove setClockManager and showTempoBargraph API**

In `src/core/LedController.h`, remove (around line 47):
```cpp
  // Clock manager (for play confirmation beat sync)
  void setClockManager(const ClockManager* clock);
```

Add new public method (near showPotBargraph):
```cpp
  // Tempo bargraph (level bar + BPM pulse on tip LED)
  void showTempoBargraph(float realLevel, uint8_t potLevel, bool caught, uint16_t bpm);
```

### 1c. LedController.cpp — Constructor + loadLedSettings

- [ ] **Step 8: Update constructor defaults**

In `src/core/LedController.cpp` constructor initializer list, replace the confirmation defaults:
```cpp
    _holdFadeMs(300),
    _playBlinks(2), _playDurationMs(200),
    _stopBlinks(2), _stopDurationMs(200),
    _octaveBlinks(3), _octaveDurationMs(300),
```

Remove from initializer list:
```
    _holdOnFlashMs(150),
    _stopFadeMs(300),
    _playBeatCount(3),
    _clock(nullptr),
    _fadeStartTime(0),
    _playFlashPhase(0),
    _playLastBeatTick(0),
```

Add to initializer list:
```
    _holdFadeIn(true),
    _potBarIsTempo(false),
    _potBarBpm(120),
```

- [ ] **Step 9: Update loadLedSettings**

In `src/core/LedController.cpp` `loadLedSettings()`, replace the confirmation loading section:
```cpp
  _holdFadeMs = s.holdFadeMs;
  _playBlinks = (s.playBlinks > 0) ? s.playBlinks : 2;
  _playDurationMs = s.playDurationMs;
  _stopBlinks = (s.stopBlinks > 0) ? s.stopBlinks : 2;
  _stopDurationMs = s.stopDurationMs;
  _octaveBlinks = s.octaveBlinks;
  _octaveDurationMs = s.octaveDurationMs;
```

Remove:
```
  _holdOnFlashMs = s.holdOnFlashMs;
  _stopFadeMs = s.stopFadeMs;
  _playBeatCount = s.playBeatCount;
```

- [ ] **Step 10: Remove setClockManager implementation**

Delete `LedController::setClockManager()` implementation. Remove `#include "../midi/ClockManager.h"` from LedController.cpp if no other usage remains.

### 1d. NvsManager.cpp — Defaults

- [ ] **Step 11: Update NvsManager defaults**

In `src/managers/NvsManager.cpp`, replace the confirmation defaults:
```cpp
  _ledSettings.holdFadeMs = 300;
  _ledSettings.playBlinks = 2;
  _ledSettings.playDurationMs = 200;
  _ledSettings.stopBlinks = 2;
  _ledSettings.stopDurationMs = 200;
  _ledSettings.octaveBlinks = 3;
  _ledSettings.octaveDurationMs = 300;
```

Remove:
```
  _ledSettings.holdOnFlashMs = 150;
  _ledSettings.stopFadeMs = 300;
  _ledSettings.playBeatCount = 3;
```

### 1e. main.cpp — Remove setClockManager call

- [ ] **Step 12: Remove clock wiring**

In `src/main.cpp`, remove the line (around line 404):
```cpp
  s_leds.setClockManager(&s_clockManager);
```

- [ ] **Step 13: Build and verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: Compilation succeeds. Behavior unchanged at runtime (code paths not yet modified).

- [ ] **Step 14: Commit**

```bash
git add src/core/KeyboardData.h src/core/LedController.h src/core/LedController.cpp src/managers/NvsManager.cpp src/main.cpp
git commit -m "refactor(led): LedSettingsStore v3 — replace beat-sync/fade fields with blink fields"
```

---

## Task 2: Arpeg Display — Remove Play Pulse, Add Idle Distinction

**Files:**
- Modify: `src/core/LedController.cpp:557-600` (renderBankArpeg + renderNormalDisplay)

- [ ] **Step 1: Rewrite renderBankArpeg**

Replace the entire `renderBankArpeg` method in `src/core/LedController.cpp`:

```cpp
void LedController::renderBankArpeg(uint8_t led, bool isFg, unsigned long now) {
  const BankSlot& slot = _slots[led];
  bool playing = slot.arpEngine && slot.arpEngine->isPlaying() && slot.arpEngine->hasNotes();
  bool hasNotes = slot.arpEngine && slot.arpEngine->hasNotes();

  // Tick flash: consume flag, track flash timer
  if (slot.arpEngine && slot.arpEngine->consumeTickFlash()) {
    _flashStartTime[led] = now;
  }
  bool flashing = false;
  if (_flashStartTime[led] != 0) {
    if ((now - _flashStartTime[led]) < _tickFlashDurationMs) {
      flashing = true;
    } else {
      _flashStartTime[led] = 0;
    }
  }

  const RGBW& col = isFg ? _colArpFg : _colArpBg;

  if (flashing && playing) {
    // Tick flash overrides during playback
    setPixel(led, _colTickFlash, isFg ? _tickFlashFg : _tickFlashBg);
  } else if (playing) {
    // Playing: solid bright (FG) or solid dim (BG)
    setPixel(led, col, isFg ? _fgArpPlayMax : _bgArpPlayMin);
  } else if (isFg && hasNotes) {
    // FG stopped with notes loaded: slow pulse (the ONLY remaining pulse)
    uint16_t period = _pulsePeriodMs > 0 ? _pulsePeriodMs : 1;
    uint16_t phase = (uint16_t)((now * 65536UL / period) % 65536);
    uint8_t  idx   = phase >> 8;
    uint8_t  frac  = phase & 0xFF;
    uint16_t sine16 = (uint16_t)LED_SINE_LUT[idx] * (256 - frac)
                    + (uint16_t)LED_SINE_LUT[(idx + 1) & 0xFF] * frac;
    uint8_t intensity = _fgArpStopMin
                      + (uint8_t)((uint32_t)sine16 * (_fgArpStopMax - _fgArpStopMin) / 65280);
    setPixel(led, col, intensity);
  } else {
    // BG (all states) or FG idle (no notes): solid dim
    setPixel(led, col, isFg ? _fgArpStopMin : _bgArpStopMin);
  }
}
```

- [ ] **Step 2: Update renderNormalDisplay call site**

In `renderNormalDisplay()`, remove the sine16 computation block and update the call:

Replace the block from `uint16_t period = ...` through the for loop dispatch:
```cpp
  if (_slots) {
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      bool isFg = (i == _currentBank);
      switch (_slots[i].type) {
        case BANK_NORMAL: renderBankNormal(i, isFg); break;
        case BANK_ARPEG:  renderBankArpeg(i, isFg, now); break;
      }

      // Battery low override: 3-blink burst on foreground bank
      if (isFg && _batteryLow) {
        // ... (keep existing battery low code unchanged)
      }
    }
  }
```

The sine16 variable, `period`, `phase`, `idx`, `frac` computation block is deleted from renderNormalDisplay — it now lives inside renderBankArpeg for the FG stopped-loaded case only.

- [ ] **Step 3: Build and verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: Compiles. Arpeg playing banks show solid color + tick flash. FG stopped-loaded pulses. BG and FG idle are solid dim.

- [ ] **Step 4: Commit**

```bash
git add src/core/LedController.cpp
git commit -m "feat(led): remove sine pulse on playing — solid + tick flash only"
```

---

## Task 3: Confirmations — All Overlay

**Files:**
- Modify: `src/core/LedController.cpp:184-206` (update)
- Modify: `src/core/LedController.cpp:350-520` (renderConfirmation)
- Modify: `src/core/LedController.cpp:638-665` (renderNormalDisplay overlays)
- Modify: `src/core/LedController.cpp:673-686` (triggerConfirm)

- [ ] **Step 1: Rewrite update() confirmation gate**

In `src/core/LedController.cpp:update()`, replace the confirmation check:
```cpp
  if (renderConfirmation(now)) {
    // All confirmations are overlays — fall through to renderNormalDisplay
  }
```

All confirmation types now fall through. No early return.

- [ ] **Step 2: Rewrite renderConfirmation — state-only for all types**

Replace the entire `renderConfirmation` method. Every case is state-tracking only (no clearPixels, no setPixel, no _strip.show). Each returns true while active, false when expired:

```cpp
bool LedController::renderConfirmation(unsigned long now) {
  if (_confirmType == CONFIRM_NONE) return false;
  unsigned long elapsed = now - _confirmStart;

  switch (_confirmType) {
    case CONFIRM_BANK_SWITCH:
      if (elapsed >= _bankDurationMs) { _confirmType = CONFIRM_NONE; return false; }
      return true;

    case CONFIRM_SCALE_ROOT:
      if (elapsed >= _scaleRootDurationMs) { _confirmType = CONFIRM_NONE; return false; }
      return true;

    case CONFIRM_SCALE_MODE:
      if (elapsed >= _scaleModeDurationMs) { _confirmType = CONFIRM_NONE; return false; }
      return true;

    case CONFIRM_SCALE_CHROM:
      if (elapsed >= _scaleChromDurationMs) { _confirmType = CONFIRM_NONE; return false; }
      return true;

    case CONFIRM_HOLD_ON:
    case CONFIRM_HOLD_OFF:
      if (elapsed >= _holdFadeMs) { _confirmType = CONFIRM_NONE; return false; }
      return true;

    case CONFIRM_PLAY:
      if (elapsed >= _playDurationMs) { _confirmType = CONFIRM_NONE; return false; }
      return true;

    case CONFIRM_STOP:
      if (elapsed >= _stopDurationMs) { _confirmType = CONFIRM_NONE; return false; }
      return true;

    case CONFIRM_OCTAVE:
      if (elapsed >= _octaveDurationMs) { _confirmType = CONFIRM_NONE; return false; }
      return true;

    default:
      _confirmType = CONFIRM_NONE;
      return false;
  }
}
```

- [ ] **Step 3: Rewrite overlay rendering in renderNormalDisplay**

In `renderNormalDisplay()`, replace the overlay section (after the bank loop, before `_strip.show()`). This replaces ALL previous overlay code (bank switch + play + stop):

```cpp
  // --- Confirmation overlays (all types, applied on current bank LED) ---
  if (_confirmType != CONFIRM_NONE) {
    unsigned long elapsed = now - _confirmStart;

    switch (_confirmType) {
      case CONFIRM_BANK_SWITCH: {
        uint16_t unitMs = _bankDurationMs / (_bankBlinks * 2);
        bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
        if (on) {
          setPixel(_currentBank, _colBankSwitch, _bankBrightnessPct);
        } else {
          _strip.setPixelColor(_currentBank, 0);
        }
        break;
      }
      case CONFIRM_SCALE_ROOT: {
        uint16_t unitMs = _scaleRootDurationMs / (_scaleRootBlinks * 2);
        bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
        if (on) setPixel(_currentBank, _colScaleRoot, 100);
        break;
      }
      case CONFIRM_SCALE_MODE: {
        uint16_t unitMs = _scaleModeDurationMs / (_scaleModeBlinks * 2);
        bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
        if (on) setPixel(_currentBank, _colScaleMode, 100);
        break;
      }
      case CONFIRM_SCALE_CHROM: {
        uint16_t unitMs = _scaleChromDurationMs / (_scaleChromBlinks * 2);
        bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
        if (on) setPixel(_currentBank, _colScaleChrom, 100);
        break;
      }
      case CONFIRM_HOLD_ON: {
        // Fade IN: 0% -> 100% over holdFadeMs
        uint8_t fadePct = (uint8_t)((uint32_t)elapsed * 100 / _holdFadeMs);
        if (fadePct > 100) fadePct = 100;
        setPixel(_currentBank, _colHold, fadePct);
        break;
      }
      case CONFIRM_HOLD_OFF: {
        // Fade OUT: 100% -> 0% over holdFadeMs
        uint8_t fadePct = (uint8_t)((uint32_t)(_holdFadeMs - elapsed) * 100 / _holdFadeMs);
        setPixel(_currentBank, _colHold, fadePct);
        break;
      }
      case CONFIRM_PLAY: {
        uint16_t unitMs = _playDurationMs / (_playBlinks * 2);
        bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
        if (on) setPixel(_currentBank, _colPlayAck, 100);
        break;
      }
      case CONFIRM_STOP: {
        uint16_t unitMs = _stopDurationMs / (_stopBlinks * 2);
        bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
        if (on) setPixel(_currentBank, _colStop, 100);
        break;
      }
      case CONFIRM_OCTAVE: {
        // Same blink pattern as scale, on current bank LED (not 2 LEDs)
        uint16_t unitMs = _octaveDurationMs / (_octaveBlinks * 2);
        bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
        if (on) setPixel(_currentBank, _colOctave, 100);
        break;
      }
      default: break;
    }
  }

  _strip.show();
```

- [ ] **Step 4: Simplify triggerConfirm**

In `src/core/LedController.cpp` `triggerConfirm()`, simplify (remove play beat-sync init):
```cpp
void LedController::triggerConfirm(ConfirmType type, uint8_t param) {
  _confirmType = type;
  _confirmStart = millis();
  _confirmParam = param;
}
```

- [ ] **Step 5: Build and verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: Compiles. All confirmations are overlays. Scale/Hold/Octave/Play/Stop no longer blank the bar.

- [ ] **Step 6: Commit**

```bash
git add src/core/LedController.cpp
git commit -m "feat(led): all confirmations are overlays — bar never blanks"
```

---

## Task 4: Tempo Bargraph

**Files:**
- Modify: `src/core/LedController.h` (showTempoBargraph already added in Task 1)
- Modify: `src/core/LedController.cpp:310-350` (renderBargraph + showTempoBargraph)
- Modify: `src/managers/PotRouter.h` (add bargraph target getter)
- Modify: `src/managers/PotRouter.cpp` (expose last bargraph target)
- Modify: `src/main.cpp:759-766` (call path)

- [ ] **Step 1: Add bargraph target tracking to PotRouter**

In `src/managers/PotRouter.h`, add to private section:
```cpp
  PotTarget _bargraphTarget;
```

In `src/managers/PotRouter.h`, add to public section:
```cpp
  PotTarget getBargraphTarget() const;
```

In `src/managers/PotRouter.cpp`, initialize `_bargraphTarget(TARGET_EMPTY)` in constructor. Add getter:
```cpp
PotTarget PotRouter::getBargraphTarget() const { return _bargraphTarget; }
```

In `applyBinding()`, wherever `_bargraphDirty = true` is set, also set:
```cpp
  _bargraphTarget = bind.target;
```

(Two locations: uncaught bargraph ~line 421, and caught bargraph ~line 538)

- [ ] **Step 2: Implement showTempoBargraph**

In `src/core/LedController.cpp`, add after `showPotBargraph()`:
```cpp
void LedController::showTempoBargraph(float realLevel, uint8_t potLevel, bool caught, uint16_t bpm) {
  _potBarRealLevel = (realLevel > (float)NUM_LEDS) ? (float)NUM_LEDS : (realLevel < 0.0f ? 0.0f : realLevel);
  _potBarPotLevel = (potLevel >= NUM_LEDS) ? (NUM_LEDS - 1) : potLevel;
  _potBarCaught = caught;
  _potBarIsTempo = true;
  _potBarBpm = bpm;
  _potBarStart = millis();
  _showingPotBar = true;
}
```

Also update `showPotBargraph()` to clear the tempo flag:
```cpp
void LedController::showPotBargraph(float realLevel, uint8_t potLevel, bool caught) {
  // ... existing code ...
  _potBarIsTempo = false;  // Add this line
  _showingPotBar = true;
}
```

- [ ] **Step 3: Add tempo pulse to renderBargraph**

In `renderBargraph()`, after the existing bargraph rendering (before `_strip.show()`), add tempo pulse:

```cpp
  // Tempo pulse: tip LED blinks at BPM rate
  if (_potBarIsTempo && _potBarCaught && _potBarBpm > 0) {
    uint32_t periodMs = 60000UL / _potBarBpm;
    bool beatOn = ((now % periodMs) < (periodMs / 2));
    uint8_t tipLed = (uint8_t)_potBarRealLevel;
    if (tipLed >= NUM_LEDS) tipLed = NUM_LEDS - 1;
    if (!beatOn) {
      _strip.setPixelColor(tipLed, 0);  // Off phase of pulse
    }
  }

  _strip.show();
  return true;
```

- [ ] **Step 4: Update main.cpp call path**

In `src/main.cpp` `handlePotPipeline()`, replace the bargraph call:
```cpp
  if (s_potRouter.hasBargraphUpdate()) {
    if (s_potRouter.getBargraphTarget() == TARGET_TEMPO_BPM) {
      s_leds.showTempoBargraph(
        s_potRouter.getBargraphLevel(),
        s_potRouter.getBargraphPotLevel(),
        s_potRouter.isBargraphCaught(),
        s_potRouter.getTempoBPM()
      );
    } else {
      s_leds.showPotBargraph(
        s_potRouter.getBargraphLevel(),
        s_potRouter.getBargraphPotLevel(),
        s_potRouter.isBargraphCaught()
      );
    }
  }
```

Note: `getTempoBPM()` should already exist on PotRouter. If not, add a getter for `_tempoBPM`.

- [ ] **Step 5: Build and verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: Compiles. Tempo pot shows bargraph with pulsing tip LED at BPM rate.

- [ ] **Step 6: Commit**

```bash
git add src/core/LedController.h src/core/LedController.cpp src/managers/PotRouter.h src/managers/PotRouter.cpp src/main.cpp
git commit -m "feat(led): add tempo bargraph with BPM pulse on tip LED"
```

---

## Task 5: Tool 7 UI Updates

**Files:**
- Modify: `src/setup/ToolLedSettings.cpp` (CONFIRM page rendering, adjustConfirmParam, descriptions, previews, defaults)

- [ ] **Step 1: Update s_ledDefaults()**

In `s_ledDefaults()`, replace the confirmation defaults:
```cpp
  d.holdFadeMs         = 300;
  d.playBlinks         = 2;
  d.playDurationMs     = 200;
  d.stopBlinks         = 2;
  d.stopDurationMs     = 200;
  d.octaveBlinks       = 3;
  d.octaveDurationMs   = 300;
```

Remove:
```
  d.holdOnFlashMs      = 150;
  d.stopFadeMs         = 300;
  d.playBeatCount      = 3;
```

- [ ] **Step 2: Rewrite adjustConfirmParam cases 8-13**

Replace cases 8-13 in `adjustConfirmParam()`:
```cpp
    case 8: { // holdFadeMs 100-600
      int step = accel ? 100 : 50;
      int val = (int)_wk.holdFadeMs + dir * step;
      if (val < 100) val = 100; if (val > 600) val = 600;
      _wk.holdFadeMs = (uint16_t)val;
      break;
    }
    case 9: { // playBlinks 1-3 wrap
      int val = (int)_wk.playBlinks + dir;
      if (val < 1) val = 3; if (val > 3) val = 1;
      _wk.playBlinks = (uint8_t)val;
      break;
    }
    case 10: { // playDurationMs 100-500
      int step = accel ? 100 : 50;
      int val = (int)_wk.playDurationMs + dir * step;
      if (val < 100) val = 100; if (val > 500) val = 500;
      _wk.playDurationMs = (uint16_t)val;
      break;
    }
    case 11: { // stopBlinks 1-3 wrap
      int val = (int)_wk.stopBlinks + dir;
      if (val < 1) val = 3; if (val > 3) val = 1;
      _wk.stopBlinks = (uint8_t)val;
      break;
    }
    case 12: { // stopDurationMs 100-500
      int step = accel ? 100 : 50;
      int val = (int)_wk.stopDurationMs + dir * step;
      if (val < 100) val = 100; if (val > 500) val = 500;
      _wk.stopDurationMs = (uint16_t)val;
      break;
    }
    case 13: { // octaveBlinks 1-3 wrap
      int val = (int)_wk.octaveBlinks + dir;
      if (val < 1) val = 3; if (val > 3) val = 1;
      _wk.octaveBlinks = (uint8_t)val;
      break;
    }
    case 14: { // octaveDurationMs 100-500
      int step = accel ? 100 : 50;
      int val = (int)_wk.octaveDurationMs + dir * step;
      if (val < 100) val = 100; if (val > 500) val = 500;
      _wk.octaveDurationMs = (uint16_t)val;
      break;
    }
```

Note: The CONFIRM page now has 15 params (0-14) instead of 14 (0-13). Cursor max must be updated accordingly.

- [ ] **Step 3: Rewrite CONFIRM page drawing**

Replace the HOLD, PLAY/STOP, and OCTAVE sections in the CONFIRM page rendering:
```cpp
        _ui->drawSection("HOLD");
        snprintf(buf, sizeof(buf), "%d ms", _wk.holdFadeMs);
        drawParam(8, "Fade duration:", buf);
        _ui->drawFrameEmpty();

        _ui->drawSection("PLAY");
        snprintf(buf, sizeof(buf), "%d", _wk.playBlinks);
        drawParam(9, "Blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.playDurationMs);
        drawParam(10, "Duration:", buf);
        _ui->drawFrameEmpty();

        _ui->drawSection("STOP");
        snprintf(buf, sizeof(buf), "%d", _wk.stopBlinks);
        drawParam(11, "Blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.stopDurationMs);
        drawParam(12, "Duration:", buf);
        _ui->drawFrameEmpty();

        _ui->drawSection("OCTAVE");
        snprintf(buf, sizeof(buf), "%d", _wk.octaveBlinks);
        drawParam(13, "Blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.octaveDurationMs);
        drawParam(14, "Duration:", buf);
```

- [ ] **Step 4: Update description text for cases 16-19**

Replace cases 16-19 in the description switch:
```cpp
        case 16:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Hold" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Overlay when toggling HOLD on an arp bank." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Fade IN = lock, fade OUT = unlock. Direction" VT_RESET);
          _ui->drawFrameLine(VT_DIM "tells you the action at a glance." VT_RESET);
          break;
        case 17:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Play" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Blink overlay when arp starts. Tick flashes" VT_RESET);
          _ui->drawFrameLine(VT_DIM "take over immediately after. Brief 'go' signal." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Press [b] to preview." VT_RESET);
          break;
        case 18:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Stop" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Blink overlay when arp stops. Same pattern as" VT_RESET);
          _ui->drawFrameLine(VT_DIM "play but distinct color. Press [b] to preview." VT_RESET);
          _ui->drawFrameEmpty();
          break;
        case 19:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Octave" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Blink overlay on current bank LED when changing" VT_RESET);
          _ui->drawFrameLine(VT_DIM "arp octave range (1-4). Press [b] to preview." VT_RESET);
          _ui->drawFrameEmpty();
          break;
```

- [ ] **Step 5: Update CONFIRM page description text for params**

Update the CONFIRM page description switch (cases by cursor index) to match new params. Replace Hold (8-9), Play (10-11), Stop (12-13), Octave (13-14) with:

```cpp
      case 8:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Hold \xe2\x80\x94 Fade Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Shared for ON (fade in) and OFF (fade out)." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 100-600ms. Shorter = snappier toggle." VT_RESET);
        _ui->drawFrameEmpty();
        break;
      case 9:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Play \xe2\x80\x94 Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Number of flashes on play. 1-3, default 2." VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        break;
      case 10:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Play \xe2\x80\x94 Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total duration of play blink overlay. 100-500ms." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Shorter = tick flashes take over faster." VT_RESET);
        _ui->drawFrameEmpty();
        break;
      case 11:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Stop \xe2\x80\x94 Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Number of flashes on stop. 1-3, default 2." VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        break;
      case 12:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Stop \xe2\x80\x94 Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total duration of stop blink overlay. 100-500ms." VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        break;
      case 13:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Octave \xe2\x80\x94 Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Number of flashes on octave change. 1-3." VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        break;
      case 14:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Octave \xe2\x80\x94 Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total duration of octave blink overlay. 100-500ms." VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        break;
```

- [ ] **Step 6: Update event previews (cases 16-19)**

Replace the preview cases 16-19:

```cpp
    case 16: {
      // Hold: fade IN then fade OUT on LED 4
      RGBW holdCol = resolveColorSlot(_cwk.slots[CSLOT_HOLD]);
      RGBW arpCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_FG]);
      uint16_t totalDur = _wk.holdFadeMs * 2 + 200;  // fade in + pause + fade out
      if (elapsed >= totalDur) { _prevState = PREV_IDLE; break; }
      _leds->previewSetPixel(3, arpCol, getRowIntensity(2));  // Static FG arp context
      if (elapsed < _wk.holdFadeMs) {
        // Fade IN
        uint8_t pct = (uint8_t)((uint32_t)elapsed * 100 / _wk.holdFadeMs);
        _leds->previewSetPixel(4, holdCol, pct);
      } else if (elapsed < _wk.holdFadeMs + 200) {
        // Brief pause at full
        _leds->previewSetPixel(4, holdCol, 100);
      } else {
        // Fade OUT
        uint16_t fadeElapsed = elapsed - _wk.holdFadeMs - 200;
        uint8_t pct = (uint8_t)((uint32_t)(_wk.holdFadeMs - fadeElapsed) * 100 / _wk.holdFadeMs);
        _leds->previewSetPixel(4, holdCol, pct);
      }
      break;
    }
    case 17: {
      // Play: double blink overlay
      RGBW playCol = resolveColorSlot(_cwk.slots[CSLOT_PLAY_ACK]);
      RGBW arpCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_FG]);
      if (elapsed >= _wk.playDurationMs) { _prevState = PREV_IDLE; break; }
      _leds->previewSetPixel(3, arpCol, getRowIntensity(6));  // Static FG arp playing context
      uint16_t unitMs = _wk.playDurationMs / (_wk.playBlinks * 2);
      bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
      if (on) _leds->previewSetPixel(4, playCol, 100);
      break;
    }
    case 18: {
      // Stop: double blink overlay
      RGBW stopCol = resolveColorSlot(_cwk.slots[CSLOT_STOP]);
      RGBW arpCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_FG]);
      if (elapsed >= _wk.stopDurationMs) { _prevState = PREV_IDLE; break; }
      _leds->previewSetPixel(3, arpCol, getRowIntensity(6));
      uint16_t unitMs = _wk.stopDurationMs / (_wk.stopBlinks * 2);
      bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
      if (on) _leds->previewSetPixel(4, stopCol, 100);
      break;
    }
    case 19: {
      // Octave: blink on single LED (not 2 LEDs)
      RGBW octCol = resolveColorSlot(_cwk.slots[CSLOT_OCTAVE]);
      RGBW normCol = resolveColorSlot(_cwk.slots[CSLOT_NORMAL_FG]);
      if (elapsed >= _wk.octaveDurationMs) { _prevState = PREV_IDLE; break; }
      _leds->previewSetPixel(3, normCol, getRowIntensity(0));  // Normal FG context
      uint16_t unitMs = _wk.octaveDurationMs / (_wk.octaveBlinks * 2);
      bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
      if (on) _leds->previewSetPixel(4, octCol, 100);
      break;
    }
```

- [ ] **Step 7: Update cursor max for CONFIRM page**

Find where the CONFIRM page cursor maximum is set (likely in navigation handling). Update from 13 to 14 (15 params: 0-14).

- [ ] **Step 8: Build and verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: Compiles. Tool 7 CONFIRM page shows updated parameters. Previews work.

- [ ] **Step 9: Commit**

```bash
git add src/setup/ToolLedSettings.cpp
git commit -m "ui(setup): Tool 7 CONFIRM page — hold fade, play/stop blinks, octave single LED"
```

---

## Task 6: Cleanup and Final Verification

**Files:**
- Modify: `src/core/LedController.cpp` (allOff reset)
- Modify: `src/core/LedController.h` (remove ClockManager forward decl)

- [ ] **Step 1: Update allOff()**

In `LedController::allOff()`, remove play-related state resets and add new ones:
```cpp
  _holdFadeIn = true;
  _potBarIsTempo = false;
```

Remove:
```
  // No _playFlashPhase, _playLastBeatTick, _fadeStartTime — they no longer exist
```

- [ ] **Step 2: Remove ClockManager forward declaration**

In `src/core/LedController.h`, remove:
```cpp
class ClockManager;
```

- [ ] **Step 3: Full build**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: Clean compile, no warnings. RAM/Flash similar to before.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "chore(led): cleanup — remove ClockManager dep, update allOff"
```
