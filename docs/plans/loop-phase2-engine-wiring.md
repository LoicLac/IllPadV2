# LOOP Mode — Phase 2: LoopEngine + Wiring

**Goal**: First MIDI sound from LOOP mode. Record pads, play them back in a loop, overdub, clear. No effects, no LED, no setup UI. Hardcoded test config.

**Prerequisite**: Phase 1 (skeleton + guards) applied and building clean.

---

## Overview

This phase creates two things:
1. **LoopEngine** — self-contained class: state machine, recording, proportional playback, bar-snap, overdub merge, refcount, pending events
2. **main.cpp wiring** — static instances, `processLoopMode()`, `handleLoopControls()`, tick/processEvents in loop order

The design doc (`docs/drafts/loop-mode-design.md`) contains the full implementation code. This plan references it by section and adds integration specifics.

---

## Step 1 — Create LoopEngine files

### 1a. Create `src/loop/LoopEngine.h`

Class definition. Reference: design doc sections "Data Structures > LoopEngine" and "Playback Engine".

**Public API** (mirror ArpEngine patterns where applicable):

```
// Config
void begin(uint8_t channel);
void clear(MidiTransport& transport);
void setPadOrder(const uint8_t* padOrder);
void setQuantizeMode(uint8_t mode);
void setChannel(uint8_t ch);

// State transitions
void startRecording();
void stopRecording(const bool* keyIsPressed, const uint8_t* padOrder, float currentBPM);
void startOverdub();
void stopOverdub(const bool* keyIsPressed, const uint8_t* padOrder, float currentBPM);
void play(const ClockManager& clock, float currentBPM);
void stop(MidiTransport& transport);
void flushLiveNotes(MidiTransport& transport, uint8_t channel);

// Core playback (called every loop iteration)
void tick(MidiTransport& transport, float currentBPM);
void processEvents(MidiTransport& transport);

// Recording input
void recordNoteOn(uint8_t padIndex, uint8_t velocity);
void recordNoteOff(uint8_t padIndex);

// Refcount helpers (called by processLoopMode)
bool noteRefIncrement(uint8_t note);
bool noteRefDecrement(uint8_t note);

// Note mapping
uint8_t padToNote(uint8_t padIndex) const;

// Setters for params (stubs — no effects in Phase 2)
void setShuffleDepth(float depth);
void setShuffleTemplate(uint8_t tmpl);
void setChaosAmount(float amount);
void setVelPatternIdx(uint8_t idx);
void setVelPatternDepth(float depth);
void setBaseVelocity(uint8_t vel);
void setVelocityVariation(uint8_t pct);

// Getters
State    getState() const;
bool     isPlaying() const;
bool     isRecording() const;  // RECORDING or OVERDUBBING
uint16_t getEventCount() const;
bool     consumeTickFlash();
float    getShuffleDepth() const;
uint8_t  getShuffleTemplate() const;
float    getChaosAmount() const;
uint8_t  getVelPatternIdx() const;
float    getVelPatternDepth() const;
```

**State enum**:
```cpp
enum State : uint8_t { EMPTY, RECORDING, PLAYING, OVERDUBBING, STOPPED };
```

**Private members**: See design doc "Data Structures > LoopEngine" for the full list. Key buffers:
- `LoopEvent _events[1024]` — 8 KB
- `LoopEvent _overdubBuf[128]` — 1 KB
- `uint8_t _noteRefCount[128]` — per MIDI note
- `PendingNoteOn _pending[16]`

### 1b. Create `src/loop/LoopEngine.cpp`

Implementation. Reference design doc sections by name:

| Method | Design doc section | Key logic |
|---|---|---|
| `begin()` | Data Structures | Zero buffers, set channel |
| `clear()` | State Machine | Flush all notes, reset to EMPTY |
| `startRecording()` | Recording flow | Set RECORDING, `_waitingForFirstHit = true` |
| `stopRecording()` | Recording flow + Bar snap | Flush held pads, latch `_recordBpm`, bar-snap, sort, → PLAYING |
| `startOverdub()` | Recording flow | Reset overdub count, → OVERDUBBING |
| `stopOverdub()` | Recording flow | `flushHeldPadsToOverdub()`, `mergeOverdub()`, → PLAYING |
| `play()` | Playback start quantize | Compute `_playStartUs` with quantize delay |
| `stop()` | State Machine | Flush active notes, discard overdub (`_overdubCount = 0`), → STOPPED |
| `tick()` | Playback Engine Phase 1 | Proportional position scaling, cursor scan, wrap detection |
| `processEvents()` | Playback Engine Phase 2 | Fire pending noteOns via refcount |
| `recordNoteOn/Off()` | Record methods | Buffer overflow guard, store in events or overdubBuf |
| `mergeOverdub()` | Overdub merge | Sort overdub buf, reverse merge O(n+m) |
| `flushHeldPadsToOverdub()` | Recording flow | Inject noteOff for held pads at current position |
| `flushLiveNotes()` | Bank switch | CC123 + zero refcount |
| `padToNote()` | Note mapping | `_padOrder[padIndex] + 36` (GM kick) |

**Phase 2 simplification — NO EFFECTS**: `calcShuffleOffsetUs()`, `calcChaosOffsetUs()`, `applyVelocityPattern()` return 0 / identity. The method bodies exist but are stubs:

```cpp
int32_t LoopEngine::calcShuffleOffsetUs(uint32_t, uint32_t) { return 0; }
int32_t LoopEngine::calcChaosOffsetUs(uint16_t) { return 0; }
uint8_t LoopEngine::applyVelocityPattern(uint8_t origVel, uint32_t, uint32_t) { return origVel; }
```

These stubs will be filled in Phase 5.

---

## Step 2 — Temporary test config

### 2a. Create `src/loop/LoopTestConfig.h`

Hardcoded values for testing without setup UI. Will be removed in Phase 3.

```cpp
#pragma once

// Temporary — remove when ToolBankConfig + ToolPadRoles support LOOP
#define LOOP_TEST_ENABLED     1
#define LOOP_TEST_BANK        7     // Bank 8 = LOOP (channel 8)
#define LOOP_TEST_REC_PAD     30    // Pick 3 adjacent pads on your layout
#define LOOP_TEST_PLAYSTOP_PAD 31
#define LOOP_TEST_CLEAR_PAD   32
```

**Note**: The pad indices (30, 31, 32) should be adjusted to match your physical layout. Pick 3 pads that are adjacent and not assigned to bank/scale/arp roles.

---

## Step 3 — main.cpp: Static instances

### 3a. Add includes (near line 1-20)

```cpp
#include "loop/LoopEngine.h"
#if LOOP_TEST_ENABLED
#include "loop/LoopTestConfig.h"
#endif
```

### 3b. Add static LoopEngine array (after s_arpEngines, line ~66)

```cpp
static LoopEngine s_loopEngines[MAX_LOOP_BANKS];  // 2 engines, ~18.8 KB
```

### 3c. Add LOOP control pad statics (after s_playStopPad/s_lastPlayStopState, line ~76)

```cpp
// LOOP control pads (from LoopPadStore NVS or test config)
static uint8_t  s_recPad           = 0xFF;
static uint8_t  s_loopPlayPad      = 0xFF;
static uint8_t  s_clearPad         = 0xFF;
static bool     s_lastRecState     = false;
static bool     s_lastLoopPlayState = false;
static bool     s_lastClearState   = false;
static uint32_t s_clearPressStart  = 0;
static bool     s_clearFired       = false;
static const uint32_t CLEAR_LONG_PRESS_MS = 500;
```

---

## Step 4 — main.cpp: setup() LoopEngine assignment

### 4a. Assign LoopEngines to LOOP banks (after ArpEngine assignment, line ~367)

```cpp
// --- Assign LoopEngines to LOOP banks ---
uint8_t loopIdx = 0;
for (uint8_t i = 0; i < NUM_BANKS && loopIdx < MAX_LOOP_BANKS; i++) {
    if (s_banks[i].type == BANK_LOOP) {
        s_loopEngines[loopIdx].begin(i);
        s_loopEngines[loopIdx].setPadOrder(s_padOrder);
        s_banks[i].loopEngine = &s_loopEngines[loopIdx];
        loopIdx++;
    }
}
```

### 4b. Test config: force bank type + load pad indices (after assignment, gated by #if)

```cpp
#if LOOP_TEST_ENABLED
  // Override bank 8 as LOOP for testing
  s_banks[LOOP_TEST_BANK].type = BANK_LOOP;
  if (!s_banks[LOOP_TEST_BANK].loopEngine) {
      // Re-run assignment for the test bank
      s_loopEngines[0].begin(LOOP_TEST_BANK);
      s_loopEngines[0].setPadOrder(s_padOrder);
      s_banks[LOOP_TEST_BANK].loopEngine = &s_loopEngines[0];
  }
  s_recPad      = LOOP_TEST_REC_PAD;
  s_loopPlayPad = LOOP_TEST_PLAYSTOP_PAD;
  s_clearPad    = LOOP_TEST_CLEAR_PAD;
#endif
```

---

## Step 5 — main.cpp: processLoopMode()

### 5a. Add function (after processArpMode, line ~554)

Reference: design doc section "processLoopMode() (inline in loop())".

```cpp
static void processLoopMode(const SharedKeyboardState& state, BankSlot& slot, uint32_t now) {
    LoopEngine* eng = slot.loopEngine;
    if (!eng) return;

    for (uint8_t p = 0; p < NUM_KEYS; p++) {
        // Skip LOOP control pads
        if (p == s_recPad || p == s_loopPlayPad || p == s_clearPad) continue;
        // Skip bank pads (always active)
        // Skip scale/arp pads only if left is held (handled by handlePadInput guard)

        bool pressed    = state.keyIsPressed[p];
        bool wasPressed = s_lastKeys[p];

        if (pressed && !wasPressed) {
            uint8_t note = eng->padToNote(p);
            uint8_t vel  = slot.baseVelocity;
            if (slot.velocityVariation > 0) {
                int16_t range = (int16_t)slot.velocityVariation * 127 / 200;
                int16_t offset = (int16_t)(random(-range, range + 1));
                vel = (uint8_t)constrain((int16_t)vel + offset, 1, 127);
            }
            // Refcount: only send MIDI noteOn on 0→1
            if (eng->noteRefIncrement(note)) {
                s_transport.sendNoteOn(note, vel, slot.channel);
            }
            // Record during overdub
            if (eng->getState() == LoopEngine::OVERDUBBING) {
                eng->recordNoteOn(p, vel);
            }
        } else if (!pressed && wasPressed) {
            uint8_t note = eng->padToNote(p);
            if (eng->noteRefDecrement(note)) {
                s_transport.sendNoteOff(note, 0, slot.channel);
            }
            if (eng->getState() == LoopEngine::OVERDUBBING) {
                eng->recordNoteOff(p);
            }
        }
    }
}
```

**Key difference from processNormalMode**: LOOP bypasses MidiEngine (no ScaleResolver). Uses `s_transport.sendNoteOn/Off()` directly. Refcount prevents duplicate noteOn/premature noteOff when live play coincides with loop playback.

---

## Step 6 — main.cpp: handleLoopControls()

### 6a. Add function (after handlePlayStopPad, line ~701)

Reference: design doc section "handleLoopControls()".

Full implementation as shown in design doc lines 1199-1282. The 3 control pads (REC, PLAY/STOP, CLEAR) each use simple tap edge detection. CLEAR uses 500ms long press.

**Important**: This function runs OUTSIDE the `isHolding()` guard — both hands free for drumming.

---

## Step 7 — main.cpp: handlePadInput() dispatch

### 7a. Add BANK_LOOP case (in handlePadInput, line ~587)

```cpp
switch (slot.type) {
  case BANK_NORMAL:
    processNormalMode(state, slot);
    break;
  case BANK_ARPEG:
    if (slot.arpEngine) processArpMode(state, slot, now);
    break;
  case BANK_LOOP:                                    // <-- ADD
    if (slot.loopEngine) processLoopMode(state, slot, now);
    break;
}
```

---

## Step 8 — main.cpp: loop() execution order

### 8a. Add handleLoopControls() call (after handlePlayStopPad, line ~962)

```cpp
  handlePlayStopPad(state, holdBeforeUpdate, bankSwitched);    // line 962
  handleLoopControls(state, now);                               // <-- ADD
  handlePadInput(state, now);                                   // line 964
```

### 8b. Add LoopEngine tick/processEvents (after ArpScheduler, line ~973)

```cpp
  s_arpScheduler.tick();                          // line 972
  s_arpScheduler.processEvents();                 // line 973

  // LOOP engines: tick + processEvents (all banks, not just foreground)
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (s_banks[i].type == BANK_LOOP && s_banks[i].loopEngine) {
          s_banks[i].loopEngine->tick(s_transport,
              s_clockManager.getSmoothedBPMFloat());
          s_banks[i].loopEngine->processEvents(s_transport);
      }
  }

  s_midiEngine.flush();                           // line 976 (CRITICAL PATH END)
```

**Why loop all banks?** Background LOOP banks continue playing. Unlike ArpScheduler which manages all engines internally, LoopEngine tick/processEvents are called directly per bank.

---

## Step 9 — main.cpp: reloadPerBankParams() LOOP branch

### 9a. Add LOOP branch (in reloadPerBankParams, after ARPEG block, line ~610)

```cpp
  // Existing ARPEG block (lines 602-610)
  if (newSlot.type == BANK_ARPEG && newSlot.arpEngine) {
      gate      = newSlot.arpEngine->getGateLength();
      shufDepth = newSlot.arpEngine->getShuffleDepth();
      // ... etc
  }

  // NEW: LOOP block — load shared params from LoopEngine
  if (newSlot.type == BANK_LOOP && newSlot.loopEngine) {
      shufDepth = newSlot.loopEngine->getShuffleDepth();
      shufTmpl  = newSlot.loopEngine->getShuffleTemplate();
  }
```

### 9b. Add pushParamsToLoop() (after pushParamsToEngine, line ~714)

```cpp
static void pushParamsToLoop(BankSlot& slot) {
    if (slot.type != BANK_LOOP || !slot.loopEngine) return;
    slot.loopEngine->setShuffleDepth(s_potRouter.getShuffleDepth());
    slot.loopEngine->setShuffleTemplate(s_potRouter.getShuffleTemplate());
    slot.loopEngine->setBaseVelocity(s_potRouter.getBaseVelocity());
    slot.loopEngine->setVelocityVariation(s_potRouter.getVelocityVariation());
    // Chaos, velPattern, velPatternDepth — stubs, no PotRouter targets yet (Phase 4)
}
```

### 9c. Call pushParamsToLoop in handlePotPipeline (after pushParamsToEngine call, line ~736)

```cpp
  pushParamsToEngine(potSlot);     // existing
  pushParamsToLoop(potSlot);       // <-- ADD
```

---

## Step 10 — main.cpp: Recording noteOn during first recording

In `processLoopMode()`, recording noteOn/noteOff should also fire during RECORDING state (not just OVERDUBBING):

```cpp
  // Record during RECORDING or OVERDUBBING
  LoopEngine::State ls = eng->getState();
  if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
      eng->recordNoteOn(p, vel);
  }
  // ... and for noteOff:
  if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
      eng->recordNoteOff(p);
  }
```

(Corrects the design doc which only checks OVERDUBBING in processLoopMode. During RECORDING, noteOn/Off must also be captured.)

---

## Step 11 — main.cpp: LOOP panic in midiPanic()

### 11a. Find midiPanic() (around line 130) and add LOOP flush

```cpp
  // Existing: flush arp engines
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (s_banks[i].type == BANK_ARPEG && s_banks[i].arpEngine) {
          s_banks[i].arpEngine->clearAllNotes(s_transport);
      }
      // ADD: flush LOOP engines
      if (s_banks[i].type == BANK_LOOP && s_banks[i].loopEngine) {
          s_banks[i].loopEngine->stop(s_transport);
      }
  }
```

---

## Build Verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected: clean build. LoopEngine compiles with ArpEngine-like patterns. Test config forces bank 8 as LOOP.

## Test Verification (hardware)

1. **Flash** and open MIDI monitor on channel 8
2. **Switch to bank 8** (hold left + bank pad 8)
3. **Tap REC pad** → state = RECORDING (no LED change yet — Phase 4)
4. **Tap some pads** → noteOn/noteOff on ch8 in MIDI monitor
5. **Tap REC pad again** → bar-snap, loop starts playing back. You should hear the pattern repeat.
6. **Change tempo** (pot R1) → playback follows tempo proportionally
7. **Tap REC pad during playback** → OVERDUBBING. Tap more pads → overdubbed.
8. **Tap REC pad** → merge, back to PLAYING with new events
9. **Tap PLAY/STOP pad** → silence (STOPPED)
10. **Tap PLAY/STOP pad** → resumes playback
11. **Hold CLEAR pad 500ms** → loop cleared, back to EMPTY
12. **Switch to another bank and back** → loop continues in background (you hear it on ch8)
13. **MIDI panic** (rear hold if applicable) → all LOOP notes silenced

---

## Files Created

| File | Content |
|---|---|
| `src/loop/LoopEngine.h` | Class definition, State enum, all public/private members |
| `src/loop/LoopEngine.cpp` | Full implementation (state machine, recording, playback, overdub, refcount) |
| `src/loop/LoopTestConfig.h` | Temporary hardcoded test values (removed in Phase 3) |

## Files Modified

| File | Changes |
|---|---|
| `src/main.cpp` | Includes, static instances, processLoopMode(), handleLoopControls(), BANK_LOOP case in handlePadInput, loop tick/processEvents, reloadPerBankParams LOOP branch, pushParamsToLoop, midiPanic LOOP flush, test config block in setup() |

## Files NOT Modified

| File | Why |
|---|---|
| `LedController` | Phase 4 |
| `PotRouter` | Phase 4 |
| `ToolBankConfig` | Phase 3 |
| `ToolPadRoles` | Phase 3 |
| `NvsManager` | Phase 4 (LoopPotStore) |
| `BankManager` | Already prepared in Phase 1 (guards are stubs, activated by loopEngine pointer) |

---

## Design Doc Correction Found

Step 10 corrects a gap in the design doc: `processLoopMode()` only checks `OVERDUBBING` for recording noteOn/Off, but `RECORDING` also needs it (first recording captures events too). The design doc `handleLoopControls()` correctly routes `startRecording()` → RECORDING state, but `processLoopMode()` must record events in both RECORDING and OVERDUBBING states.
