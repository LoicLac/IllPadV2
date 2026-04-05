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

**LoopEvent struct** (define in LoopEngine.h, before the class):

```cpp
struct LoopEvent {
    uint32_t offsetUs;    // Microseconds from loop start (recording timebase)
    uint8_t  padIndex;    // 0-47
    uint8_t  velocity;    // 0 = noteOff, >0 = noteOn (no separate isNoteOn field)
    uint8_t  _pad[2];     // Alignment to 8 bytes
};
static_assert(sizeof(LoopEvent) == 8, "LoopEvent must be 8 bytes");
```

**PendingNote struct** (for shuffle/chaos time offsets on BOTH noteOn and noteOff,
mirrors ArpEngine's PendingEvent which also carries both via velocity field):

```cpp
struct PendingNote {
    uint32_t fireTimeUs;  // micros() when to send
    uint8_t  note;
    uint8_t  velocity;    // > 0 = noteOn, == 0 = noteOff (MIDI convention)
    bool     active;
};
```

NoteOff events go through the same pending queue as noteOn. This ensures shuffle/chaos
offsets are applied to BOTH, preserving gate length. Without this, a shuffled noteOn
with an unshuffled noteOff would shorten the gate — audible on sustained sounds (bass, pads).

**Private members — key buffers**:
- `LoopEvent _events[MAX_LOOP_EVENTS]` — 8 KB (MAX_LOOP_EVENTS = 1024)
- `LoopEvent _overdubBuf[MAX_OVERDUB_EVENTS]` — 1 KB (MAX_OVERDUB_EVENTS = 128)
- `uint8_t _noteRefCount[128]` — per MIDI note
- `PendingNote _pending[MAX_PENDING]` — 16 entries (MAX_PENDING = 16, handles both noteOn and noteOff)

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
| `tick()` | Playback Engine Phase 1 | Proportional position: `elapsedUs = (micros() - _playStartUs) % liveDurationUs; positionUs = elapsedUs * recordDurationUs / liveDurationUs;` Wrap when `positionUs < _lastPositionUs` → flushActiveNotes + cursor=0 + tickFlash. Quantize guard: `if ((int32_t)(now - _playStartUs) < 0) return;` **Both noteOn AND noteOff go through schedulePending** — shuffle/chaos applied to all events so gate length is preserved (mirrors ArpEngine pending pattern). |
| `processEvents()` | Playback Engine Phase 2 | Fire pending notes via refcount — velocity > 0 = noteOn (increment), velocity == 0 = noteOff (decrement). Mirrors ArpEngine.processEvents (line 443-447). |
| `recordNoteOn/Off()` | Record methods | Buffer overflow guard, store in events or overdubBuf |
| `mergeOverdub()` | Overdub merge | Sort overdub buf, reverse merge O(n+m) |
| `flushHeldPadsToOverdub()` | Recording flow | Inject noteOff for held pads at current position |
| `flushLiveNotes()` | Bank switch | CC123 + zero refcount |
| `padToNote()` | Note mapping | Returns `_padOrder[padIndex] + 36` (GM kick), or `0xFF` if pad unmapped — callers MUST check for 0xFF |

**Critical helper implementations** (must be exactly right for MIDI safety):

```cpp
// padToNote — guard unmapped pads (0xFF + 36 would overflow to 35 = wrong note)
static const uint8_t LOOP_NOTE_OFFSET = 36;  // GM kick drum
uint8_t LoopEngine::padToNote(uint8_t padIndex) const {
    uint8_t order = _padOrder[padIndex];
    if (order == 0xFF) return 0xFF;  // unmapped
    return order + LOOP_NOTE_OFFSET;
}

// noteRefIncrement — returns true on 0→1 (caller sends MIDI noteOn)
bool LoopEngine::noteRefIncrement(uint8_t note) {
    return (_noteRefCount[note]++ == 0);
}

// noteRefDecrement — returns true on 1→0 (caller sends MIDI noteOff)
// MUST guard refcount==0 to prevent underflow (stop() zeros all refcounts,
// then pad release calls this — without guard, would underflow to 255)
bool LoopEngine::noteRefDecrement(uint8_t note) {
    if (_noteRefCount[note] > 0) {
        return (--_noteRefCount[note] == 0);
    }
    return false;
}

// calcLoopDurationUs — guard BPM==0 (ClockManager can briefly return 0 in edge case)
uint32_t LoopEngine::calcLoopDurationUs(float bpm) const {
    if (bpm < 1.0f) bpm = 1.0f;  // safety clamp — prevents division by zero
    return (uint32_t)(_loopLengthBars * 4.0f * 60000000.0f / bpm);
}
```

**Phase 2 simplification — NO EFFECTS**: `calcShuffleOffsetUs()`, `calcChaosOffsetUs()`, `applyVelocityPattern()` return 0 / identity. The method bodies exist but are stubs:

```cpp
int32_t LoopEngine::calcShuffleOffsetUs(uint32_t, uint32_t) { return 0; }
int32_t LoopEngine::calcChaosOffsetUs(uint32_t) { return 0; }
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

    LoopEngine::State ls = eng->getState();

    for (uint8_t p = 0; p < NUM_KEYS; p++) {
        // Skip LOOP control pads
        if (p == s_recPad || p == s_loopPlayPad || p == s_clearPad) continue;

        bool pressed    = state.keyIsPressed[p];
        bool wasPressed = s_lastKeys[p];

        if (pressed && !wasPressed) {
            uint8_t note = eng->padToNote(p);
            if (note == 0xFF) continue;  // unmapped pad (BUG #4 fix)
            uint8_t vel  = slot.baseVelocity;
            if (slot.velocityVariation > 0) {
                int16_t range = (int16_t)slot.velocityVariation * 127 / 200;
                int16_t offset = (int16_t)(random(-range, range + 1));
                vel = (uint8_t)constrain((int16_t)vel + offset, 1, 127);
            }
            // Refcount: only send MIDI noteOn on 0→1
            if (eng->noteRefIncrement(note)) {
                s_transport.sendNoteOn(slot.channel, note, vel);  // (channel, note, vel)
            }
            // Record during RECORDING or OVERDUBBING (BUG #2 fix — was OVERDUBBING only)
            if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
                eng->recordNoteOn(p, vel);
            }
        } else if (!pressed && wasPressed) {
            uint8_t note = eng->padToNote(p);
            if (note == 0xFF) continue;  // unmapped pad
            if (eng->noteRefDecrement(note)) {
                s_transport.sendNoteOn(slot.channel, note, 0);  // velocity 0 = noteOff
            }
            if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
                eng->recordNoteOff(p);
            }
        }
    }
}
```

**Key differences from processNormalMode**:
- LOOP bypasses MidiEngine — uses `s_transport.sendNoteOn()` directly (no ScaleResolver)
- `sendNoteOn(channel, note, 0)` for noteOff (MidiTransport has no sendNoteOff method)
- Refcount prevents duplicate noteOn/premature noteOff when live play coincides with loop playback
- `padToNote()` returns 0xFF for unmapped pads — must skip

---

## Step 6 — main.cpp: handleLoopControls()

### 6a. Add function (after handlePlayStopPad, line ~701)

The 3 control pads (REC, PLAY/STOP, CLEAR) each use simple tap edge detection. CLEAR uses 500ms long press. This function runs OUTSIDE the `isHolding()` guard — both hands free for drumming.

**State transition table** (verified against design doc):

| Pad | Current State | Action | New State |
|---|---|---|---|
| REC | EMPTY | startRecording() | RECORDING |
| REC | RECORDING | stopRecording() + bar-snap | PLAYING |
| REC | PLAYING | startOverdub() | OVERDUBBING |
| REC | OVERDUBBING | stopOverdub() + merge | PLAYING |
| REC | STOPPED | *(ignored)* | STOPPED |
| P/S | PLAYING | stop() | STOPPED |
| P/S | OVERDUBBING | stop() *(discard overdub)* | STOPPED |
| P/S | STOPPED | play() *(quantized)* | PLAYING |
| P/S | EMPTY/RECORDING | *(ignored)* | unchanged |
| CLR | any except EMPTY | clear() *(500ms hold)* | EMPTY |

```cpp
static void handleLoopControls(const SharedKeyboardState& state, uint32_t now) {
    BankSlot& slot = s_bankManager.getCurrentSlot();
    if (slot.type != BANK_LOOP || !slot.loopEngine) {
        // Not a LOOP bank — reset edge states so pads act as music
        s_lastRecState      = (s_recPad < NUM_KEYS) ? state.keyIsPressed[s_recPad] : false;
        s_lastLoopPlayState = (s_loopPlayPad < NUM_KEYS) ? state.keyIsPressed[s_loopPlayPad] : false;
        s_lastClearState    = (s_clearPad < NUM_KEYS) ? state.keyIsPressed[s_clearPad] : false;
        return;
    }

    LoopEngine* eng = slot.loopEngine;
    LoopEngine::State ls = eng->getState();

    // --- REC pad: simple tap edge ---
    if (s_recPad < NUM_KEYS) {
        bool pressed = state.keyIsPressed[s_recPad];
        if (pressed && !s_lastRecState) {
            switch (ls) {
                case LoopEngine::EMPTY:
                    eng->startRecording();
                    s_leds.triggerConfirm(CONFIRM_LOOP_REC);
                    break;
                case LoopEngine::RECORDING:
                    eng->stopRecording(state.keyIsPressed, s_padOrder,
                                       s_clockManager.getSmoothedBPMFloat());
                    s_leds.triggerConfirm(CONFIRM_PLAY);
                    break;
                case LoopEngine::PLAYING:
                    eng->startOverdub();
                    s_leds.triggerConfirm(CONFIRM_LOOP_REC);
                    break;
                case LoopEngine::OVERDUBBING:
                    eng->stopOverdub(state.keyIsPressed, s_padOrder,
                                     s_clockManager.getSmoothedBPMFloat());
                    s_leds.triggerConfirm(CONFIRM_PLAY);
                    break;
                default: break;  // STOPPED: REC ignored
            }
        }
        s_lastRecState = pressed;
    }

    // --- PLAY/STOP pad: simple tap edge ---
    if (s_loopPlayPad < NUM_KEYS) {
        bool pressed = state.keyIsPressed[s_loopPlayPad];
        if (pressed && !s_lastLoopPlayState) {
            switch (ls) {
                case LoopEngine::PLAYING:
                case LoopEngine::OVERDUBBING:
                    eng->stop(s_transport);
                    s_leds.triggerConfirm(CONFIRM_STOP);
                    break;
                case LoopEngine::STOPPED:
                    eng->play(s_clockManager, s_clockManager.getSmoothedBPMFloat());
                    s_leds.triggerConfirm(CONFIRM_PLAY);
                    break;
                default: break;  // EMPTY, RECORDING: ignored
            }
        }
        s_lastLoopPlayState = pressed;
    }

    // --- CLEAR pad: long press (500ms) + LED ramp ---
    if (s_clearPad < NUM_KEYS && ls != LoopEngine::EMPTY) {
        bool pressed = state.keyIsPressed[s_clearPad];
        if (pressed && !s_lastClearState) {
            s_clearPressStart = now;
            s_clearFired = false;
        }
        if (pressed && !s_clearFired) {
            uint32_t held = now - s_clearPressStart;
            if (held < CLEAR_LONG_PRESS_MS) {
                uint8_t ramp = (uint8_t)((uint32_t)held * 100 / CLEAR_LONG_PRESS_MS);
                s_leds.showClearRamp(ramp);
            } else {
                eng->clear(s_transport);
                s_leds.triggerConfirm(CONFIRM_STOP);
                s_clearFired = true;
            }
        }
        s_lastClearState = pressed;
    }
}
```

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

## Step 10 — BankManager: Activate LOOP guards (Phase 1 stubs)

### 10a. Add `#include` in BankManager.cpp (line 1)

```cpp
#include "../loop/LoopEngine.h"  // for isRecording(), flushLiveNotes()
```

### 10b. Replace recording lock stub (BankManager.cpp, in switchToBank)

Phase 1 Step 5d left a commented stub. Replace with the real guard:

```cpp
  // LOOP recording lock: deny switch while recording/overdubbing
  if (_banks[_currentBank].type == BANK_LOOP && _banks[_currentBank].loopEngine) {
    LoopEngine::State ls = _banks[_currentBank].loopEngine->getState();
    if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
      return;  // Silently deny — user must close recording first
    }
  }
```

### 10c. Replace flushLiveNotes stub (BankManager.cpp, before foreground swap)

```cpp
  // Flush LOOP live notes on outgoing bank (CC123 + zero refcount)
  if (_banks[_currentBank].type == BANK_LOOP && _banks[_currentBank].loopEngine && _transport) {
    _banks[_currentBank].loopEngine->flushLiveNotes(*_transport, _currentBank);
  }
```

---

## Step 10b — handleLeftReleaseCleanup: LOOP branch

### 10b-1. Add BANK_LOOP case in handleLeftReleaseCleanup() (main.cpp)

When left button releases on a LOOP bank, pads pressed before the hold and
released during it have their noteOff edge missed (processLoopMode is gated
by `!isHolding()`). Without cleanup, these notes stay stuck.

Find the existing switch in `handleLeftReleaseCleanup()` and add after the
BANK_ARPEG case:

```cpp
  case BANK_LOOP:
    if (slot.loopEngine) {
      for (int i = 0; i < NUM_KEYS; i++) {
        if (s_lastKeys[i] && !state.keyIsPressed[i]) {
          uint8_t note = slot.loopEngine->padToNote(i);
          if (note != 0xFF && slot.loopEngine->noteRefDecrement(note)) {
            s_transport.sendNoteOn(slot.channel, note, 0);  // noteOff
          }
        }
      }
    }
    break;
```

---

## ~~Step 10~~ — MERGED INTO STEP 5a

The RECORDING + OVERDUBBING check is now directly in Step 5a's code. No separate step needed.

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
| `src/main.cpp` | Includes, static instances, processLoopMode(), handleLoopControls(), handleLeftReleaseCleanup LOOP branch, BANK_LOOP case in handlePadInput, loop tick/processEvents, reloadPerBankParams LOOP branch, pushParamsToLoop, midiPanic LOOP flush, test config block in setup() |
| `src/managers/BankManager.cpp` | Activate recording lock + flushLiveNotes (Phase 1 stubs → real guards), add `#include LoopEngine.h` |

## Files NOT Modified

| File | Why |
|---|---|
| `LedController` | Phase 4 |
| `PotRouter` | Phase 4 |
| `ToolBankConfig` | Phase 3 |
| `ToolPadRoles` | Phase 3 |
| `NvsManager` | Phase 4 (LoopPotStore) |

---

## Design Doc Correction Found

Step 10 corrects a gap in the design doc: `processLoopMode()` only checks `OVERDUBBING` for recording noteOn/Off, but `RECORDING` also needs it (first recording captures events too). The design doc `handleLoopControls()` correctly routes `startRecording()` → RECORDING state, but `processLoopMode()` must record events in both RECORDING and OVERDUBBING states.

---

## Audit Notes (2026-04-05) — Cross-checked by 5 parallel agents + design doc

### BUG #1 — `sendNoteOn` / `sendNoteOff` parameter order (**CRITICAL**)

Step 5a uses `s_transport.sendNoteOn(note, vel, slot.channel)` but `MidiTransport::sendNoteOn`
signature is `(uint8_t channel, uint8_t note, uint8_t velocity)`. **Parameter order is
(channel, note, velocity), not (note, vel, channel).** Compiles without warning (all uint8_t)
but sends notes on wrong channel with swapped note/velocity values.

Also: `sendNoteOff()` does not exist in MidiTransport. ArpEngine uses
`sendNoteOn(channel, note, 0)` for noteOff (velocity 0). The design doc confirms this
at line 1319: `s_transport.sendNoteOff(note, 0, slot.channel)` — but this also has wrong
param order.

**Fix for Step 5a**: Replace ALL transport calls in processLoopMode:
```cpp
// noteOn:
s_transport.sendNoteOn(slot.channel, note, vel);
// noteOff:
s_transport.sendNoteOn(slot.channel, note, 0);
```

The design doc (line 1310-1319) has the SAME bug and must also be corrected.

### BUG #2 — Step 5a vs Step 10 inconsistency (**merged**)

Step 5a only records during OVERDUBBING. Step 10 corrects this to include RECORDING.
**The implementer should use Step 10's version directly in Step 5a** to avoid writing
incorrect code then fixing it 5 steps later. The code in Step 5a should read:

```cpp
    LoopEngine::State ls = eng->getState();
    if (pressed && !wasPressed) {
        // ... noteOn + refcount ...
        if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
            eng->recordNoteOn(p, vel);
        }
    } else if (!pressed && wasPressed) {
        // ... noteOff + refcount ...
        if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
            eng->recordNoteOff(p);
        }
    }
```

### BUG #3 — BankManager recording lock not activated in Phase 2

Phase 1 adds a stub guard in `switchToBank()` with a comment saying
"Phase 2 will replace this with `isRecording()`". **Phase 2 never mentions
BankManager in its modified files.** The guard remains dead.

**Fix**: Add a step to Phase 2 (or a note in Step 4a) to uncomment the
BankManager guard:
```cpp
  // BankManager.cpp switchToBank() — Phase 1 stub → Phase 2 real guard:
  if (cur.type == BANK_LOOP && cur.loopEngine) {
    LoopEngine::State ls = cur.loopEngine->getState();
    if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
      return;  // Deny bank switch during recording/overdubbing
    }
  }
```

And add `flushLiveNotes` call (confirmed by design doc line 526-533):
```cpp
  if (cur.type == BANK_LOOP && cur.loopEngine && _transport) {
    cur.loopEngine->flushLiveNotes(*_transport, _currentBank);
  }
```

Both require `#include "loop/LoopEngine.h"` in BankManager.cpp.

### BUG #4 — `padToNote()` overflow on unmapped pads

`padToNote()` returns `_padOrder[padIndex] + 36`. If padOrder[i] == 0xFF
(NOTEMAP_UNMAPPED), result is 0xFF + 36 = 35 (uint8_t overflow) — a valid
but wrong MIDI note. ArpEngine avoids this because ScaleResolver checks for
0xFF; LOOP bypasses ScaleResolver.

**Fix**: Guard in padToNote:
```cpp
uint8_t padToNote(uint8_t padIndex) const {
    uint8_t order = _padOrder[padIndex];
    if (order == 0xFF) return 0xFF;  // unmapped
    return order + LOOP_NOTE_OFFSET;
}
```
Callers must check for 0xFF before using the note:
```cpp
uint8_t note = eng->padToNote(p);
if (note == 0xFF) continue;  // skip unmapped pad
```

### BUG #5 — `handleLeftReleaseCleanup` missing LOOP branch

`handleLeftReleaseCleanup()` in main.cpp handles NORMAL (noteOff) and ARPEG
(remove pile). When left button releases on a LOOP bank, pads pressed before
the hold and released during the hold get no cleanup — their noteOff edge was
missed (processLoopMode is gated by `!isHolding()`). Result: stuck notes.

**Fix**: Add LOOP branch in handleLeftReleaseCleanup:
```cpp
  case BANK_LOOP:
    if (slot.loopEngine) {
      for (int i = 0; i < NUM_KEYS; i++) {
        if (s_lastKeys[i] && !state.keyIsPressed[i]) {
          uint8_t note = slot.loopEngine->padToNote(i);
          if (note != 0xFF && slot.loopEngine->noteRefDecrement(note)) {
            s_transport.sendNoteOn(slot.channel, note, 0);  // noteOff
          }
        }
      }
    }
    break;
```

### GAP #6 — `handleLoopControls()` full code now available

The plan said "Full implementation as shown in design doc" without code.
The design doc (lines 1199-1282) contains the complete implementation with:
- REC pad: switch(state) → EMPTY→startRecording, RECORDING→stopRecording,
  PLAYING→startOverdub, OVERDUBBING→stopOverdub, STOPPED→ignored
- PLAY/STOP pad: PLAYING/OVERDUBBING→stop, STOPPED→play, EMPTY/RECORDING→ignored
- CLEAR pad: long press 500ms + LED ramp, any state except EMPTY→clear

**Key state transition table** (verified against design doc lines 406-418):

| Pad | Current State | Action | New State |
|---|---|---|---|
| REC | EMPTY | startRecording() | RECORDING |
| REC | RECORDING | stopRecording() + bar-snap | PLAYING |
| REC | PLAYING | startOverdub() | OVERDUBBING |
| REC | OVERDUBBING | stopOverdub() + merge | PLAYING |
| REC | STOPPED | ignored | STOPPED |
| P/S | PLAYING | stop() | STOPPED |
| P/S | OVERDUBBING | stop() (discard overdub) | STOPPED |
| P/S | STOPPED | play() (quantized) | PLAYING |
| P/S | EMPTY/RECORDING | ignored | unchanged |
| CLR | any except EMPTY | clear() (500ms hold) | EMPTY |

### GAP #7 — LoopEvent structure (from design doc)

```cpp
struct LoopEvent {
    uint32_t offsetUs;    // Microseconds from loop start (recording timebase)
    uint8_t  padIndex;    // 0-47
    uint8_t  velocity;    // 0 = noteOff, >0 = noteOn
    uint8_t  _pad[2];     // Alignment to 8 bytes
};
static_assert(sizeof(LoopEvent) == 8, "LoopEvent must be 8 bytes");
```

No `isNoteOn` field — velocity == 0 discriminates noteOff from noteOn.

### GAP #8 — Proportional playback formula (from design doc)

```cpp
// In tick():
uint32_t liveDurationUs   = calcLoopDurationUs(currentBPM);
uint32_t recordDurationUs = calcLoopDurationUs(_recordBpm);
uint32_t elapsedUs  = (micros() - _playStartUs) % liveDurationUs;
uint32_t positionUs = (uint32_t)((float)elapsedUs
                      * (float)recordDurationUs / (float)liveDurationUs);
// positionUs is in recording timebase → matches event offsets directly

// Helper:
uint32_t calcLoopDurationUs(float bpm) const {
    return (uint32_t)(_loopLengthBars * 4.0f * 60000000.0f / bpm);
}
```

**Bar snap formula** (stopRecording):
```cpp
uint32_t barDurationUs = (uint32_t)(4.0f * 60000000.0f / _recordBpm);
uint32_t recordedDurationUs = micros() - _recordStartUs;
uint16_t bars = (recordedDurationUs + barDurationUs / 2) / barDurationUs;  // round nearest
if (bars == 0) bars = 1;
if (bars > 64) bars = 64;
_loopLengthBars = bars;
// Then normalize all event offsets to [0, bars * barDurationUs)
float scale = (float)(bars * barDurationUs) / (float)recordedDurationUs;
for (uint16_t i = 0; i < _eventCount; i++) {
    _events[i].offsetUs = (uint32_t)((float)_events[i].offsetUs * scale);
}
```

**Wrap detection**: `if (positionUs < _lastPositionUs)` — position jumped backward.
On wrap: `flushActiveNotes()` + `_cursorIdx = 0` + `_tickFlash = true`.
No rebase of `_playStartUs` — unsigned modulus handles overflow.

**Cursor scan — noteOn AND noteOff both go through pending** (Design #1 redesign):

The design doc tick() code has noteOn via `schedulePending` and noteOff inline.
**This must be changed**: noteOff must also go through `schedulePending` with velocity=0,
so shuffle/chaos offsets are applied to both. Otherwise the gate shortens on shuffled
notes (audible on sustained sounds like bass).

Corrected cursor scan inner loop:
```cpp
    while (_cursorIdx < _eventCount &&
           _events[_cursorIdx].offsetUs <= positionUs) {
        const LoopEvent& ev = _events[_cursorIdx];
        int32_t shuffleUs = calcShuffleOffsetUs(ev.offsetUs, recordDurationUs);
        int32_t chaosUs   = calcChaosOffsetUs(ev.offsetUs);

        if (ev.velocity > 0) {
            // noteOn: apply velocity pattern, schedule via pending
            uint8_t vel = applyVelocityPattern(ev.velocity, ev.offsetUs, recordDurationUs);
            schedulePending(now + shuffleUs + chaosUs, padToNote(ev.padIndex), vel);
        } else {
            // noteOff: ALSO schedule via pending (same shuffle/chaos offset)
            // This preserves gate length — mirrors ArpEngine pending pattern
            schedulePending(now + shuffleUs + chaosUs, padToNote(ev.padIndex), 0);
        }
        _cursorIdx++;
    }
```

And `processEvents()` must handle both (mirrors ArpEngine.cpp:443-447):
```cpp
void LoopEngine::processEvents(MidiTransport& transport) {
    uint32_t now = micros();
    for (uint8_t i = 0; i < MAX_PENDING; i++) {
        if (_pending[i].active && (int32_t)(now - _pending[i].fireTimeUs) >= 0) {
            uint8_t note = _pending[i].note;
            if (_pending[i].velocity > 0) {
                // noteOn: refcount increment, MIDI only on 0→1
                if (_noteRefCount[note] == 0) {
                    transport.sendNoteOn(_channel, note, _pending[i].velocity);
                }
                _noteRefCount[note]++;
            } else {
                // noteOff: refcount decrement, MIDI only on 1→0
                if (_noteRefCount[note] > 0) {
                    _noteRefCount[note]--;
                    if (_noteRefCount[note] == 0) {
                        transport.sendNoteOn(_channel, note, 0);  // velocity 0 = noteOff
                    }
                }
            }
            _pending[i].active = false;
        }
    }
}
```

### GAP #9 — Quantized start: tick() signature vs ClockManager

The plan's `tick(transport, currentBPM)` lacks `globalTick`. Design doc resolves
this differently from ArpEngine: `play()` pre-computes `_playStartUs` with the
quantize delay (using ClockManager at call time), and `tick()` checks
`if ((int32_t)(now - _playStartUs) < 0) return;`. No tick boundary check needed
inside `tick()`. This is consistent and intentional.

### OBSERVATION #10 — `noteRefDecrement` must guard refcount==0

The design doc (line 1337-1340) confirms the guard:
```cpp
bool noteRefDecrement(uint8_t note) {
    if (_noteRefCount[note] > 0) {
        return (--_noteRefCount[note] == 0);
    }
    return false;
}
```
Without this, stop() followed by pad release would cause uint8_t underflow.

### OBSERVATION #11 — `stop()` during OVERDUBBING discards partial overdub

Design doc (line 414): `P/S during OVERDUBBING → STOPPED (abort, discard)`.
This is intentional — the user presses PLAY/STOP to abort, not REC to commit.
Documented, not a bug.

### OBSERVATION #12 — BPM == 0 division by zero risk

`calcLoopDurationUs(bpm)` divides by bpm. ClockManager can briefly return 0.0f
from `_lastKnownBPM` in a narrow race window. **Add guard**:
```cpp
uint32_t calcLoopDurationUs(float bpm) const {
    if (bpm < 1.0f) bpm = 1.0f;  // safety clamp
    return (uint32_t)(_loopLengthBars * 4.0f * 60000000.0f / bpm);
}
```

### OBSERVATION #13 — 18.8 KB SRAM always allocated

`s_loopEngines[2]` is statically allocated even if no LOOP banks exist.
The design doc suggests `#define ENABLE_LOOP_MODE 1` compile guard (line 1402-1407).
Not critical (22% SRAM usage with LOOP vs 16% without), but noted.

### FILES table correction

BankManager.cpp MUST be listed as modified in Phase 2 (recording lock + flushLiveNotes
activation + LoopEngine.h include).
