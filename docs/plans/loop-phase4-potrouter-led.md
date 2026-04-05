# LOOP Mode — Phase 4: PotRouter + LED

**Goal**: Pots control LOOP params. LED shows LOOP state visually. LoopPotStore persists per-bank params in NVS.

**Prerequisite**: Phase 3 (setup tools) applied. LOOP banks assignable, pad roles saved.

---

## Part A — PotRouter Integration

### Step 1 — KeyboardData.h: New PotTargets

Append 3 LOOP-specific targets **before TARGET_EMPTY** (end of assignable range),
NOT in the middle of the enum. PotTarget is auto-numbered — inserting in the middle
shifts all subsequent values, which would invalidate any NVS-stored PotMapping blob
(even though Zero Migration Policy handles size mismatches, future additions without
size change would corrupt silently).

```cpp
  TARGET_MIDI_PITCHBEND,
  // LOOP per-bank (appended — preserves existing numeric values 0-15)
  TARGET_CHAOS,              // <-- ADD (0.0-1.0)
  TARGET_VEL_PATTERN,        // <-- ADD (0-3 discrete)
  TARGET_VEL_PATTERN_DEPTH,  // <-- ADD (0.0-1.0)
  // Empty slot (explicit "no parameter here")
  TARGET_EMPTY,
  // Sentinel
  TARGET_NONE,
  TARGET_COUNT = TARGET_NONE
};
```

**Note**: TARGET_SHUFFLE_DEPTH and TARGET_SHUFFLE_TEMPLATE are reused — they already exist. LOOP shares the templates but each engine has independent values.

---

### Step 2 — PotRouter.h: New members + MAX_CC_SLOTS

#### 2a. Bump MAX_CC_SLOTS and MAX_BINDINGS (line 96, 138)

```cpp
static const uint8_t MAX_BINDINGS = 26;  // was 24 — 3 contexts × 8 user + 2 rear
static const uint8_t MAX_CC_SLOTS = 24;  // was 16 — 8 NORMAL + 8 ARPEG + 8 LOOP
```

Without bumping MAX_BINDINGS, the rear pot bindings (brightness + sensitivity) are
silently dropped because the guard `if (_numBindings >= MAX_BINDINGS)` fires at 24
before the 2 rear bindings are added (rebuildBindings lines 214-222).

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

### Step 7 — NvsManager: LoopPotStore persistence (8 sub-steps, mirrors ArpPotStore exactly)

#### 7a. NvsManager.h — add dirty flags + pending data (after _arpPotDirty, line 88)

```cpp
  std::atomic<bool> _loopPotDirty[NUM_BANKS];   // <-- ADD
```

After `_pendingArpPot` (line 101):
```cpp
  LoopPotStore _pendingLoopPot[NUM_BANKS];       // <-- ADD
```

#### 7b. NvsManager.h — add queue method + getter (after queueArpPotWrite, line 25)

```cpp
  void queueLoopPotWrite(uint8_t bankIdx, float shuffleDepth, uint8_t shuffleTmpl,
                         uint8_t velPat, float chaos, float velPatDepth);
  const LoopPotStore& getLoadedLoopParams(uint8_t bankIdx) const;
```

#### 7c. NvsManager.cpp — constructor init (in the for loop, after _arpPotDirty init, line ~84)

```cpp
    _loopPotDirty[i] = false;
    _pendingLoopPot[i] = {0, 0, 0, 0, 0};  // all zeros = no shuffle, no chaos, no vel pattern
```

#### 7d. NvsManager.cpp — tickPotDebounce: add BANK_LOOP branch (after BANK_ARPEG block, line ~225)

```cpp
  // === Per-bank: loop pot params (LOOP only) ===
  if (currentType == BANK_LOOP) {
    LoopPotStore newLoop;
    newLoop.shuffleDepthRaw   = (uint16_t)(potRouter.getShuffleDepth() * 4095.0f);
    newLoop.shuffleTemplate   = potRouter.getShuffleTemplate();
    newLoop.velPatternIdx     = potRouter.getVelPatternIdx();
    newLoop.chaosRaw          = (uint16_t)(potRouter.getChaosAmount() * 4095.0f);
    newLoop.velPatternDepthRaw = (uint16_t)(potRouter.getVelPatternDepth() * 4095.0f);

    const LoopPotStore& cur = _pendingLoopPot[currentBank];
    if (newLoop.shuffleDepthRaw != cur.shuffleDepthRaw
        || newLoop.shuffleTemplate != cur.shuffleTemplate
        || newLoop.velPatternIdx != cur.velPatternIdx
        || newLoop.chaosRaw != cur.chaosRaw
        || newLoop.velPatternDepthRaw != cur.velPatternDepthRaw) {
      _pendingLoopPot[currentBank] = newLoop;
      _loopPotDirty[currentBank] = true;
      _anyDirty = true;
    }
  }
```

#### 7e. NvsManager.cpp — queueLoopPotWrite (after queueArpPotWrite, line ~289)

```cpp
void NvsManager::queueLoopPotWrite(uint8_t bankIdx, float shuffleDepth, uint8_t shuffleTmpl,
                                    uint8_t velPat, float chaos, float velPatDepth) {
  if (bankIdx >= NUM_BANKS) return;
  _pendingLoopPot[bankIdx].shuffleDepthRaw    = (uint16_t)(shuffleDepth * 4095.0f);
  _pendingLoopPot[bankIdx].shuffleTemplate    = shuffleTmpl;
  _pendingLoopPot[bankIdx].velPatternIdx      = velPat;
  _pendingLoopPot[bankIdx].chaosRaw           = (uint16_t)(chaos * 4095.0f);
  _pendingLoopPot[bankIdx].velPatternDepthRaw = (uint16_t)(velPatDepth * 4095.0f);
  _loopPotDirty[bankIdx] = true;
  _anyDirty = true;
}
```

#### 7f. NvsManager.cpp — commitAll: batch LoopPotStore writes (after ArpPotStore batch, line ~398)

```cpp
  // Batch loop pot saves (one namespace open)
  {
    bool anyLoop = false;
    for (uint8_t i = 0; i < NUM_BANKS; i++) { if (_loopPotDirty[i]) { anyLoop = true; break; } }
    if (anyLoop) {
      Preferences prefs;
      if (prefs.begin(LOOPPOT_NVS_NAMESPACE, false)) {
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
          if (_loopPotDirty[i]) {
            char key[8];
            snprintf(key, sizeof(key), "loop_%d", i);
            prefs.putBytes(key, &_pendingLoopPot[i], sizeof(LoopPotStore));
            _loopPotDirty[i] = false;
          }
        }
        prefs.end();
      }
    }
  }
```

#### 7g. NvsManager.cpp — loadAll: load LoopPotStore per-bank (after ArpPotStore load section)

```cpp
  // Load LOOP pot params per bank
  {
    Preferences prefs;
    if (prefs.begin(LOOPPOT_NVS_NAMESPACE, true)) {
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        char key[8];
        snprintf(key, sizeof(key), "loop_%d", i);
        size_t len = prefs.getBytesLength(key);
        if (len == sizeof(LoopPotStore)) {
          prefs.getBytes(key, &_pendingLoopPot[i], sizeof(LoopPotStore));
          // Validate
          if (_pendingLoopPot[i].shuffleTemplate >= NUM_SHUFFLE_TEMPLATES)
            _pendingLoopPot[i].shuffleTemplate = 0;
          if (_pendingLoopPot[i].velPatternIdx > 3)
            _pendingLoopPot[i].velPatternIdx = 0;
        }
      }
      prefs.end();
    }
  }
```

#### 7h. NvsManager.cpp — getLoadedLoopParams (after getLoadedArpParams)

```cpp
const LoopPotStore& NvsManager::getLoadedLoopParams(uint8_t bankIdx) const {
  static const LoopPotStore defaultLoop = {0, 0, 0, 0, 0};
  if (bankIdx >= NUM_BANKS) return defaultLoop;
  return _pendingLoopPot[bankIdx];
}
```

**No main.cpp wiring needed** — `tickPotDebounce()` already receives `currentType`
(line 985 of main.cpp) and handles the BANK_LOOP branch internally (Step 7d).
The boot-time seed of LoopEngine params from NVS happens in Phase 2's setup() wiring,
calling `getLoadedLoopParams()` after engine assignment.

---

## Part B — LED Feedback

### Step 8 — LedController.h: ConfirmType + forward-declare (already in Phase 1)

#### 8a. CONFIRM_LOOP_REC — already added in Phase 1 Step 4c-1

Verify it exists: `CONFIRM_LOOP_REC = 10` after `CONFIRM_OCTAVE = 9`. No action needed.

#### 8b. LoopEngine forward-declare — already added in Phase 1 Step 4c-3

Verify `class LoopEngine;` exists near top of LedController.h. No action needed.

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

Follows renderBankArpeg pattern exactly — same structure, same sine math, different colors.
Sine pulse ONLY on "FG stopped + events loaded" (equivalent of arp "FG stopped + hasNotes").
All other states are solid. Recording/Overdubbing get a fast blink (unique to LOOP, no arp equivalent).

```cpp
void LedController::renderBankLoop(uint8_t led, bool isFg, unsigned long now) {
    if (!_slots) return;
    const BankSlot& slot = _slots[led];
    if (!slot.loopEngine) {
        setPixel(led, COL_LOOP_DIM, LED_BG_LOOP_DIM);
        return;
    }

    LoopEngine::State ls = slot.loopEngine->getState();
    bool playing  = (ls == LoopEngine::PLAYING || ls == LoopEngine::OVERDUBBING);
    bool hasEvents = slot.loopEngine->getEventCount() > 0;

    // Tick flash (wrap): consume flag, track timer — same pattern as arp
    if (slot.loopEngine->consumeTickFlash()) {
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

    // --- Recording/Overdubbing: fast blink (LOOP-specific, no arp equivalent) ---
    if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
        // Only visible when FG (bank switch denied during rec/overdub, but defensive)
        bool on = ((now / 150) % 2 == 0);
        setPixel(led, COL_LOOP_REC, on ? 100 : 0);
        return;
    }

    // --- From here: EMPTY, STOPPED, or PLAYING — mirrors arp exactly ---
    const RGBW& col = COL_LOOP;  // arp uses _colArpFg/_colArpBg; LOOP uses single COL_LOOP

    if (flashing && playing) {
        // Wrap flash overrides during playback
        setPixel(led, _colTickFlash, isFg ? LED_FG_LOOP_FLASH : LED_BG_LOOP_FLASH);
    } else if (playing) {
        // Playing: solid bright (FG) or solid dim (BG) — no pulse
        setPixel(led, col, isFg ? LED_FG_LOOP_PLAY : LED_BG_LOOP_PLAY);
    } else if (isFg && hasEvents) {
        // FG stopped with events loaded: slow sine pulse (the ONLY pulse in LOOP)
        uint16_t period = _pulsePeriodMs > 0 ? _pulsePeriodMs : 1;
        uint16_t phase = (uint16_t)((now * 65536UL / period) % 65536);
        uint8_t  idx   = phase >> 8;
        uint8_t  frac  = phase & 0xFF;
        uint16_t sine16 = (uint16_t)LED_SINE_LUT[idx] * (256 - frac)
                        + (uint16_t)LED_SINE_LUT[(idx + 1) & 0xFF] * frac;
        uint8_t intensity = LED_FG_LOOP_STOP_MIN
                          + (uint8_t)((uint32_t)sine16 * (LED_FG_LOOP_STOP_MAX - LED_FG_LOOP_STOP_MIN) / 65280);
        setPixel(led, col, intensity);
    } else {
        // BG (all states) or FG idle (no events): solid dim
        setPixel(led, col, isFg ? LED_FG_LOOP_IDLE : LED_BG_LOOP_DIM);
    }
}
```

**Compared to renderBankArpeg line-by-line:**
- Tick flash setup: identical pattern (lines 431-441 → same)
- Recording blink: NEW (arp has no equivalent — arp never "records")
- flash + playing → wrap flash with `_colTickFlash` (identical)
- playing → solid `LED_FG_LOOP_PLAY` / `LED_BG_LOOP_PLAY` (arp uses `_fgArpPlayMax` / `_bgArpPlayMin`)
- FG stopped + events → sine pulse `STOP_MIN↔STOP_MAX` (arp uses `_fgArpStopMin↔Max`)
- else → solid dim (identical structure)

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

### Step 11 — LedController: showClearRamp() (replace Phase 1 stub)

#### 11a. Replace stub with real declaration + add private members (LedController.h)

Phase 1 added an inline stub: `void showClearRamp(uint8_t pct) { (void)pct; }`.
Replace it with a real declaration + add private state:

```cpp
  // Public — replace the Phase 1 inline stub
  void showClearRamp(uint8_t pct);  // 0-100, red ramp on current bank LED

  // Private — add
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

### Step 12 — CONFIRM_LOOP_REC: expiry + rendering (TWO locations)

Confirmation has two parts in separate functions — both must be updated:

**12a. Expiry in `renderConfirmation()`** (line 388, after CONFIRM_OCTAVE case):

```cpp
    case CONFIRM_LOOP_REC:
      if (elapsed >= 200) { _confirmType = CONFIRM_NONE; return false; }
      return true;
```

Without this, the `default:` branch kills CONFIRM_LOOP_REC immediately
(sets CONFIRM_NONE, returns false) and the overlay in renderNormalDisplay never fires.

**12b. Rendering in `renderNormalDisplay()` overlay section** (line 562, after CONFIRM_OCTAVE case):

```cpp
      case CONFIRM_LOOP_REC: {
        // Double blink red-magenta on current LED — 200ms total
        uint16_t unitMs = 50;  // 4 phases × 50ms = 200ms
        bool on = ((elapsed / unitMs) % 2 == 0);
        if (on) setPixel(_currentBank, COL_LOOP_REC, 100);
        // else: off (normal display shows through)
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

**LED** (follows arp pattern — sine pulse only on FG stopped+events):
1. LOOP bank EMPTY → solid dim magenta (like arp FG idle no notes)
2. Tap REC → fast red-magenta blink (recording — LOOP-specific)
3. Tap REC again → solid bright magenta + white wrap flash (like arp FG playing)
4. Tap PLAY/STOP → stopped with events: slow magenta sine pulse (like arp FG stopped+notes)
5. Tap PLAY/STOP again → cleared events, no events: solid dim (like arp FG idle)
6. Hold CLEAR → red ramp-up on current LED over 500ms → confirm blink on clear
7. Switch banks → background LOOP: dim magenta solid + wrap flash if playing (like arp BG)
8. Bank switch blink → uses normal _colBankSwitch (unchanged)

---

## Files Modified

| File | Changes |
|---|---|
| `src/core/KeyboardData.h` | 3 new PotTargets appended before TARGET_EMPTY |
| `src/managers/PotRouter.h` | MAX_BINDINGS→26, MAX_CC_SLOTS→24, LOOP members+getters, loadStoredPerBankLoop() |
| `src/managers/PotRouter.cpp` | applyBinding, isPerBank, getRange, getDiscrete, seedCatch, rebuildBindings 3-ctx, DEFAULT_MAPPING loopMap, loadStoredPerBankLoop |
| `src/managers/NvsManager.h` | _loopPotDirty[8], _pendingLoopPot[8], queueLoopPotWrite(), getLoadedLoopParams() |
| `src/managers/NvsManager.cpp` | Constructor init, tickPotDebounce BANK_LOOP branch, queueLoopPotWrite, commitAll batch, loadAll section, getLoadedLoopParams |
| `src/core/LedController.h` | CONFIRM_LOOP_REC, LoopEngine forward-declare, renderBankLoop decl, showClearRamp, _clearRampPct |
| `src/core/LedController.cpp` | renderBankLoop (arp-pattern), renderConfirmation LOOP_REC expiry, renderNormalDisplay LOOP_REC overlay, bargraph 3-way color, showClearRamp |
| `src/main.cpp` | reloadPerBankParams complete, pushParamsToLoop complete |
