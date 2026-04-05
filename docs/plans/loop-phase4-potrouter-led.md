# LOOP Mode — Phase 4: PotRouter + LED

**Goal**: Pots control LOOP params. LED shows LOOP state visually. LoopPotStore persists per-bank params in NVS.

**Prerequisite**: Phase 3 (setup tools) applied. LOOP banks assignable, pad roles saved.

---

## Part A — PotRouter Integration

### Step 1 — KeyboardData.h: New PotTargets

Add 3 LOOP-specific targets to the PotTarget enum (line 410, before TARGET_BASE_VELOCITY):

```cpp
  TARGET_SHUFFLE_TEMPLATE,
  // LOOP per-bank
  TARGET_CHAOS,              // <-- ADD (0.0-1.0)
  TARGET_VEL_PATTERN,        // <-- ADD (0-3 discrete)
  TARGET_VEL_PATTERN_DEPTH,  // <-- ADD (0.0-1.0)
  // Shared per-bank (NORMAL + ARPEG + LOOP)
  TARGET_BASE_VELOCITY,
```

**Note**: TARGET_SHUFFLE_DEPTH and TARGET_SHUFFLE_TEMPLATE are reused — they already exist. LOOP shares the templates but each engine has independent values.

---

### Step 2 — PotRouter.h: New members + MAX_CC_SLOTS

#### 2a. Bump MAX_CC_SLOTS (line 138)

```cpp
static const uint8_t MAX_CC_SLOTS = 24;  // was 16 — 8 NORMAL + 8 ARPEG + 8 LOOP
```

#### 2b. Add LOOP param members (after existing _shuffleTemplate, around line ~120)

```cpp
  // LOOP-specific per-bank params
  float   _chaosAmount      = 0.0f;
  uint8_t _velPatternIdx    = 0;
  float   _velPatternDepth  = 0.0f;
```

#### 2c. Add getters (after existing getShuffleTemplate)

```cpp
  float   getChaosAmount() const       { return _chaosAmount; }
  uint8_t getVelPatternIdx() const     { return _velPatternIdx; }
  float   getVelPatternDepth() const   { return _velPatternDepth; }
```

#### 2d. Add loadStoredPerBankLoop() declaration

```cpp
  void loadStoredPerBankLoop(float chaos, uint8_t velPat, float velPatDepth);
```

---

### Step 3 — PotRouter.cpp: Switch cases + bindings

#### 3a. applyBinding() — add 3 cases (before TARGET_EMPTY, line ~507)

```cpp
    case TARGET_CHAOS:
        _chaosAmount = adcToFloat(adc);
        break;
    case TARGET_VEL_PATTERN: {
        uint8_t pat = (uint8_t)adcToRange(adc, 0, 3);
        if (pat > 3) pat = 3;
        _velPatternIdx = pat;
        break;
    }
    case TARGET_VEL_PATTERN_DEPTH:
        _velPatternDepth = adcToFloat(adc);
        break;
```

#### 3b. isPerBankTarget() — add 3 cases (after TARGET_SHUFFLE_TEMPLATE, line ~597)

```cpp
    case TARGET_CHAOS:
    case TARGET_VEL_PATTERN:
    case TARGET_VEL_PATTERN_DEPTH:
        return true;
```

#### 3c. getRangeForTarget() — add 3 cases (before default, line ~155)

```cpp
    case TARGET_CHAOS:             lo = 0; hi = 4095; break;
    case TARGET_VEL_PATTERN:       lo = 0; hi = 3; break;
    case TARGET_VEL_PATTERN_DEPTH: lo = 0; hi = 4095; break;
```

#### 3d. getDiscreteSteps() — add VEL_PATTERN (after TARGET_SHUFFLE_TEMPLATE, line ~614)

```cpp
    case TARGET_VEL_PATTERN: {
        uint16_t lo, hi;
        getRangeForTarget(t, lo, hi);
        return (uint8_t)(hi - lo);
    }
```

#### 3e. seedCatchValues() — add 3 cases (before default, line ~291)

```cpp
    case TARGET_CHAOS:
        norm = _chaosAmount;
        break;
    case TARGET_VEL_PATTERN:
        norm = (float)_velPatternIdx / 3.0f;
        break;
    case TARGET_VEL_PATTERN_DEPTH:
        norm = _velPatternDepth;
        break;
```

#### 3f. loadStoredPerBankLoop() — add method (after loadStoredPerBank, line ~114)

```cpp
void PotRouter::loadStoredPerBankLoop(float chaos, uint8_t velPat, float velPatDepth) {
    _chaosAmount      = chaos;
    _velPatternIdx    = velPat;
    _velPatternDepth  = velPatDepth;
}
```

---

### Step 4 — PotRouter.cpp: rebuildBindings() 3 contexts

#### 4a. Replace context loop (line 176-178)

```cpp
  // Was: for (uint8_t ctx = 0; ctx < 2; ctx++)
  struct CtxInfo { BankType type; const PotMapping* map; };
  const CtxInfo ctxs[] = {
      { BANK_NORMAL, _mapping.normalMap },
      { BANK_ARPEG,  _mapping.arpegMap  },
      { BANK_LOOP,   _mapping.loopMap   },
  };
  for (uint8_t ctx = 0; ctx < 3; ctx++) {
      BankType btype = ctxs[ctx].type;
      const PotMapping* map = ctxs[ctx].map;
      // ... rest of inner loop unchanged
  }
```

---

### Step 5 — PotRouter.cpp: DEFAULT_MAPPING + PotMappingStore

#### 5a. Rewrite DEFAULT_MAPPING (line 14-40)

Add loopMap block after arpegMap:

```cpp
  // LOOP context (8 slots)
  {
    {TARGET_TEMPO_BPM, 0},             // R1 alone
    {TARGET_CHAOS, 0},                 // R1 + hold
    {TARGET_BASE_VELOCITY, 0},         // R2 alone
    {TARGET_SHUFFLE_DEPTH, 0},         // R2 + hold
    {TARGET_VEL_PATTERN, 0},           // R3 alone
    {TARGET_SHUFFLE_TEMPLATE, 0},      // R3 + hold
    {TARGET_VEL_PATTERN_DEPTH, 0},     // R4 alone
    {TARGET_VELOCITY_VARIATION, 0},    // R4 + hold
  }
```

#### 5b. PotMappingStore is already extended in Phase 1 (KeyboardData.h)

The struct has `loopMap[8]` added. The version stays 1 (fresh format). Old NVS data triggers defaults on size mismatch.

---

### Step 6 — main.cpp: reloadPerBankParams LOOP + pushParamsToLoop

#### 6a. Extend reloadPerBankParams() LOOP branch (already stubbed in Phase 2)

Complete the stub:

```cpp
  if (newSlot.type == BANK_LOOP && newSlot.loopEngine) {
      shufDepth = newSlot.loopEngine->getShuffleDepth();
      shufTmpl  = newSlot.loopEngine->getShuffleTemplate();
  }

  s_potRouter.loadStoredPerBank(
      newSlot.baseVelocity, newSlot.velocityVariation, newSlot.pitchBendOffset,
      gate, shufDepth, div, pat, shufTmpl
  );

  // LOOP-specific params
  float chaos = 0.0f;
  uint8_t velPat = 0;
  float velPatDepth = 0.0f;
  if (newSlot.type == BANK_LOOP && newSlot.loopEngine) {
      chaos       = newSlot.loopEngine->getChaosAmount();
      velPat      = newSlot.loopEngine->getVelPatternIdx();
      velPatDepth = newSlot.loopEngine->getVelPatternDepth();
  }
  s_potRouter.loadStoredPerBankLoop(chaos, velPat, velPatDepth);
```

#### 6b. Complete pushParamsToLoop() (already stubbed in Phase 2)

```cpp
static void pushParamsToLoop(BankSlot& slot) {
    if (slot.type != BANK_LOOP || !slot.loopEngine) return;
    slot.loopEngine->setShuffleDepth(s_potRouter.getShuffleDepth());
    slot.loopEngine->setShuffleTemplate(s_potRouter.getShuffleTemplate());
    slot.loopEngine->setChaosAmount(s_potRouter.getChaosAmount());
    slot.loopEngine->setVelPatternIdx(s_potRouter.getVelPatternIdx());
    slot.loopEngine->setVelPatternDepth(s_potRouter.getVelPatternDepth());
    slot.loopEngine->setBaseVelocity(s_potRouter.getBaseVelocity());
    slot.loopEngine->setVelocityVariation(s_potRouter.getVelocityVariation());
}
```

---

### Step 7 — NvsManager: LoopPotStore load/save

#### 7a. Add LoopPotStore per-bank storage (NvsManager.h + .cpp)

Follow the exact ArpPotStore pattern:

```cpp
// NvsManager.h — add member
LoopPotStore _pendingLoopPot[NUM_BANKS];

// NvsManager.h — add getter
const LoopPotStore& getLoadedLoopParams(uint8_t bankIdx) const;

// NvsManager.cpp — queueLoopPotWrite() + dirty flag
// NvsManager.cpp — loadAll(): load from illpad_lpot, loop_0..loop_7
// NvsManager.cpp — background task: write dirty LoopPotStore entries
```

#### 7b. Wire in main.cpp

In the pot debounce section (line ~984), add LoopPotStore dirty check (same pattern as ArpPotStore).

---

## Part B — LED Feedback

### Step 8 — LedController.h: New ConfirmType + forward-declare

#### 8a. Add CONFIRM_LOOP_REC (after CONFIRM_OCTAVE, line 26)

```cpp
  CONFIRM_OCTAVE       = 9,
  CONFIRM_LOOP_REC     = 10,  // <-- ADD
```

#### 8b. Forward-declare LoopEngine (near top of .h)

```cpp
class LoopEngine;
```

---

### Step 9 — LedController.cpp: renderBankLoop()

#### 9a. Add private method declaration (LedController.h)

```cpp
  void renderBankLoop(uint8_t led, bool isFg, unsigned long now);
```

#### 9b. Add dispatch case (renderNormalDisplay, line ~476)

```cpp
  case BANK_LOOP:  renderBankLoop(i, isFg, now); break;
```

#### 9c. Implement renderBankLoop() (after renderBankArpeg, line ~466)

```cpp
void LedController::renderBankLoop(uint8_t led, bool isFg, unsigned long now) {
    if (!_slots) return;
    const BankSlot& slot = _slots[led];
    if (!slot.loopEngine) {
        // LOOP bank with no engine — dim magenta
        setPixel(led, COL_LOOP_DIM, 5);
        return;
    }

    LoopEngine::State ls = slot.loopEngine->getState();
    bool playing = (ls == LoopEngine::PLAYING || ls == LoopEngine::OVERDUBBING);

    if (isFg) {
        if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
            // Fast red-magenta blink (150ms period)
            bool on = ((now / 150) % 2 == 0);
            setPixel(led, COL_LOOP_REC, on ? 100 : 0);
        } else if (ls == LoopEngine::PLAYING) {
            // Magenta sine pulse + wrap flash
            uint16_t period = _pulsePeriodMs > 0 ? _pulsePeriodMs : 1;
            uint16_t phase = (uint16_t)((now * 65536UL / period) % 65536);
            uint8_t idx  = phase >> 8;
            uint8_t frac = phase & 0xFF;
            uint16_t sine16 = (uint16_t)LED_SINE_LUT[idx] * (256 - frac)
                            + (uint16_t)LED_SINE_LUT[(idx + 1) & 0xFF] * frac;
            uint8_t intensity = LED_FG_LOOP_PLAY_MIN
                + (uint8_t)((uint32_t)sine16 * (LED_FG_LOOP_PLAY_MAX - LED_FG_LOOP_PLAY_MIN) / 65280);

            // Wrap flash (same pattern as arp tick flash)
            if (slot.loopEngine->consumeTickFlash()) _flashStartTime[led] = now;
            if (_flashStartTime[led] && (now - _flashStartTime[led]) < _tickFlashDurationMs) {
                intensity = LED_FG_LOOP_PLAY_FLASH;
            } else if (_flashStartTime[led] && (now - _flashStartTime[led]) >= _tickFlashDurationMs) {
                _flashStartTime[led] = 0;
            }
            setPixel(led, COL_LOOP, intensity);
        } else if (ls == LoopEngine::STOPPED) {
            // Slow sine
            uint16_t period = _pulsePeriodMs > 0 ? _pulsePeriodMs : 1;
            uint16_t phase = (uint16_t)((now * 65536UL / period) % 65536);
            uint8_t idx  = phase >> 8;
            uint8_t frac = phase & 0xFF;
            uint16_t sine16 = (uint16_t)LED_SINE_LUT[idx] * (256 - frac)
                            + (uint16_t)LED_SINE_LUT[(idx + 1) & 0xFF] * frac;
            uint8_t intensity = LED_FG_LOOP_STOP_MIN
                + (uint8_t)((uint32_t)sine16 * (LED_FG_LOOP_STOP_MAX - LED_FG_LOOP_STOP_MIN) / 65280);
            setPixel(led, COL_LOOP, intensity);
        } else {
            // EMPTY — same slow sine as STOPPED
            uint16_t period = _pulsePeriodMs > 0 ? _pulsePeriodMs : 1;
            uint16_t phase = (uint16_t)((now * 65536UL / period) % 65536);
            uint8_t idx  = phase >> 8;
            uint8_t frac = phase & 0xFF;
            uint16_t sine16 = (uint16_t)LED_SINE_LUT[idx] * (256 - frac)
                            + (uint16_t)LED_SINE_LUT[(idx + 1) & 0xFF] * frac;
            uint8_t intensity = LED_FG_LOOP_STOP_MIN
                + (uint8_t)((uint32_t)sine16 * (LED_FG_LOOP_STOP_MAX - LED_FG_LOOP_STOP_MIN) / 65280);
            setPixel(led, COL_LOOP, intensity);
        }
    } else {
        // Background
        if (playing) {
            uint16_t period = _pulsePeriodMs > 0 ? _pulsePeriodMs : 1;
            uint16_t phase = (uint16_t)((now * 65536UL / period) % 65536);
            uint8_t idx  = phase >> 8;
            uint8_t frac = phase & 0xFF;
            uint16_t sine16 = (uint16_t)LED_SINE_LUT[idx] * (256 - frac)
                            + (uint16_t)LED_SINE_LUT[(idx + 1) & 0xFF] * frac;
            uint8_t intensity = LED_BG_LOOP_PLAY_MIN
                + (uint8_t)((uint32_t)sine16 * (LED_BG_LOOP_PLAY_MAX - LED_BG_LOOP_PLAY_MIN) / 65280);

            if (slot.loopEngine->consumeTickFlash()) _flashStartTime[led] = now;
            if (_flashStartTime[led] && (now - _flashStartTime[led]) < _tickFlashDurationMs) {
                intensity = LED_BG_LOOP_PLAY_FLASH;
            } else if (_flashStartTime[led] && (now - _flashStartTime[led]) >= _tickFlashDurationMs) {
                _flashStartTime[led] = 0;
            }
            setPixel(led, COL_LOOP, intensity);
        } else if (ls == LoopEngine::STOPPED) {
            uint16_t period = _pulsePeriodMs > 0 ? _pulsePeriodMs : 1;
            uint16_t phase = (uint16_t)((now * 65536UL / period) % 65536);
            uint8_t idx  = phase >> 8;
            uint8_t frac = phase & 0xFF;
            uint16_t sine16 = (uint16_t)LED_SINE_LUT[idx] * (256 - frac)
                            + (uint16_t)LED_SINE_LUT[(idx + 1) & 0xFF] * frac;
            uint8_t intensity = LED_BG_LOOP_STOP_MIN
                + (uint8_t)((uint32_t)sine16 * (LED_BG_LOOP_STOP_MAX - LED_BG_LOOP_STOP_MIN) / 65280);
            setPixel(led, COL_LOOP, intensity);
        }
        // EMPTY background = off (default clearPixels already handled)
    }
}
```

**Implementation note**: The sine pulse code is duplicated from renderBankArpeg. A future refactor could extract a `calcSineIntensity(min, max, now)` helper. Not done here to minimize Phase 4 scope.

---

### Step 10 — LedController.cpp: Bargraph + bank switch colors

#### 10a. Bargraph bar color (line 313-314)

Replace the ternary with 3-way:

```cpp
  RGBW barColor, barDim;
  if (_slots && _slots[_currentBank].type == BANK_LOOP) {
      barColor = COL_LOOP;     barDim = COL_LOOP_DIM;
  } else if (_slots && _slots[_currentBank].type == BANK_ARPEG) {
      barColor = _colArpFg;    barDim = _colArpBg;
  } else {
      barColor = _colNormalFg; barDim = _colNormalBg;
  }
```

---

### Step 11 — LedController.cpp: showClearRamp()

#### 11a. Add method and member (LedController.h)

```cpp
  // Public
  void showClearRamp(uint8_t pct);  // 0-100, red ramp on current bank LED

  // Private
  uint8_t _clearRampPct = 0;
  bool    _showingClearRamp = false;
```

#### 11b. Implement (LedController.cpp)

```cpp
void LedController::showClearRamp(uint8_t pct) {
    _clearRampPct = pct;
    _showingClearRamp = (pct > 0);
}
```

#### 11c. Render in renderNormalDisplay() (after confirmation overlay, before strip.show)

```cpp
  // Clear ramp overlay (LOOP only)
  if (_showingClearRamp) {
      setPixel(_currentBank, COL_LOOP_REC, _clearRampPct);
      _showingClearRamp = false;  // auto-reset — handleLoopControls calls every frame during hold
  }
```

---

### Step 12 — renderConfirmation: CONFIRM_LOOP_REC

Add case in renderConfirmation() switch (line ~393):

```cpp
  case CONFIRM_LOOP_REC: {
      // Double blink red-magenta on current LED — 200ms
      uint16_t unitMs = 50;  // 4 phases × 50ms = 200ms
      uint8_t phase = (uint8_t)(elapsed / unitMs) % 4;
      if (phase == 0 || phase == 2) {
          setPixel(_currentBank, COL_LOOP_REC, 100);
      }
      // else: off (normal display shows through)
      if (elapsed >= 200) _confirmType = CONFIRM_NONE;
      break;
  }
```

---

## Build Verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

## Test Verification (hardware)

**PotRouter:**
1. Switch to LOOP bank, turn pot R1 → tempo bargraph in magenta
2. Hold left + turn pot R1 → chaos (no audible effect yet — Phase 5)
3. Turn pot R3 → vel pattern cycles 0-3 (discrete steps in bargraph)
4. Switch to ARPEG bank and back → catch system resets, params reload
5. Reboot → LOOP params persist (LoopPotStore)

**LED:**
1. LOOP bank EMPTY → slow magenta sine pulse
2. Tap REC → fast red-magenta blink (recording)
3. Tap REC again → magenta sine pulse + white wrap flash at loop boundary
4. Tap PLAY/STOP → slow magenta sine (stopped)
5. Hold CLEAR → red ramp-up on current LED over 500ms → flash on clear
6. Switch banks → background LOOP shows dim magenta + wrap flash if playing
7. Bank switch blink → uses normal _colBankSwitch (unchanged)

---

## Files Modified

| File | Changes |
|---|---|
| `src/core/KeyboardData.h` | 3 new PotTargets in enum |
| `src/managers/PotRouter.h` | MAX_CC_SLOTS→24, LOOP members+getters, loadStoredPerBankLoop() |
| `src/managers/PotRouter.cpp` | applyBinding, isPerBank, getRange, getDiscrete, seedCatch, rebuildBindings 3-ctx, DEFAULT_MAPPING loopMap, loadStoredPerBankLoop |
| `src/managers/NvsManager.h` | _pendingLoopPot[8], getLoadedLoopParams() |
| `src/managers/NvsManager.cpp` | LoopPotStore load/save/dirty pattern |
| `src/core/LedController.h` | CONFIRM_LOOP_REC, renderBankLoop decl, showClearRamp, _clearRampPct |
| `src/core/LedController.cpp` | renderBankLoop(), bargraph 3-way color, showClearRamp(), CONFIRM_LOOP_REC |
| `src/main.cpp` | reloadPerBankParams complete, pushParamsToLoop complete, LoopPotStore dirty check |
