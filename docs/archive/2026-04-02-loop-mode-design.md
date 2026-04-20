> ⚠️ **OBSOLETE — NE PAS LIRE COMME CONTEXTE**
>
> Document **obsolète** (pré-refactor Phase 0 / 0.1). Ne doit pas être consommé par un agent ou LLM pour planning, implementation, ou audit.
>
> **Source de vérité LOOP actuelle** : [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](../superpowers/specs/2026-04-19-loop-mode-design.md) (qui consolide ce document + [`2026-04-06-loop-slot-drive-design.md`](2026-04-06-loop-slot-drive-design.md)).
>
> Conservé pour archive historique uniquement.

---

# LOOP Mode — Design Document

**Status**: VALIDATED DRAFT — audited against codebase 2026-03-25, updated 2026-04-02
**Predecessor**: `loop-mode-design-draft.md` (7 critical issues found and resolved)
**2026-04-02**: Replaced overloaded play/stop pad with 3 dedicated LOOP control pads (REC, PLAY/STOP, CLEAR). CLEAR protected by long press + LED ramp. Eliminates double-tap/long-press ambiguity.

---

## Pre-Implementation Refactoring

Refactorings to apply BEFORE implementing LoopEngine. The LOOP mode adds a 3rd bank type — every `if (NORMAL) / else (ARPEG)` pattern in the codebase becomes a 3-branch switch. Extracting these now keeps each branch isolated and reviewable. Without this, the LOOP integration creates 3+ monolithic functions with deeply nested type/state/fg/bg combinations.

### Must (blocking) — DONE (commit `0f31838`)

| What | Where | Status |
|---|---|---|
| Extract `SHUFFLE_TEMPLATES[5][16]` to shared header | `ArpEngine.cpp` → `midi/GrooveTemplates.h` | **DONE** — `GrooveTemplates.h` created, `ArpEngine.h` includes it, local constants removed. |

### Should (prevents monoliths) — DONE (commits `ed1788e` + `0f31838`)

| What | Where | Status |
|---|---|---|
| Extract `renderBankNormal()` / `renderBankArpeg()` from `renderNormalDisplay()` | `LedController.cpp` | **DONE** — `renderNormalDisplay()` dispatches via `switch(slot.type)`. Each type is a separate method. Adding LOOP = new `renderBankLoop()` beside existing functions. |
| Extract `processNormalMode()` / `processArpMode()` from `handlePadInput()` | `main.cpp` | **DONE** — `handlePadInput()` dispatches via `switch(slot.type)`. Static locals (`s_lastHoldState`, `s_wasHolding`) migrated to their respective functions. `handleLeftReleaseCleanup()` also extracted. |
| Extract bank-switch param loading into a function | `main.cpp` | **DONE** — `reloadPerBankParams(BankSlot&)` extracted from `handleManagerUpdates()`. LOOP will add its own params beside the existing arp block. |
| Extract `pushParamsToEngine()` from `handlePotPipeline()` | `main.cpp` | **DONE** — arp param push isolated. LOOP will add `pushParamsToLoop()` beside it. |
| Add `ClockManager::getSmoothedBPMFloat()` | `ClockManager.h/.cpp` | **DONE** — returns `_pllBPM` as float. LoopEngine needs float BPM for proportional position scaling. |

### Nice (coherence) — not yet done

| What | Where | Why |
|---|---|---|
| Share `PendingEvent` struct | `ArpEngine.h:35-40` → `KeyboardData.h` | LoopEngine defines `PendingNoteOn` with identical fields (fireTimeUs, note, velocity, active). One struct, two consumers. Can be done at implementation time. |

---

## Overview

A third bank type (alongside NORMAL and ARPEG) for percussion looping. Records pad events with free timing, plays them back in a loop synced to the clock. No pitch, no scale, no aftertouch.

## Key Decisions

- **Max 2 LOOP banks** (18.8 KB SRAM, ~6% of 320 KB)
- **Ephemeral loop content** — recorded events lost on reboot, no NVS persistence for loop data
- **Persistent params** — per-bank effect params (shuffle, chaos, vel pattern) saved in NVS via `LoopPotStore` (same pattern as `ArpPotStore`)
- **Background playback** — continues when switching banks (like arp)
- **Microsecond timestamps** — events stored as `micros()` offsets (NOT tick-based — see Timing Model)
- **Loop duration**: free recording, snapped to nearest bar on close
- **Live play during playback** — pads play live on top of the loop
- **Separate from ArpScheduler** — LoopEngine has own `tick()`/`processEvents()` (different playback model)
- **LED feedback**: SK6812 RGBW NeoPixel — red/magenta color family (distinct from white=NORMAL, blue=ARPEG)

---

## Timing Model

### Why not 480 PPQN ticks?

The existing clock runs at **24 PPQN** (`ClockManager::getCurrentTick()`, `ArpScheduler.cpp:10-19`). Storing loop events at 480 PPQN would require:
- A 20x interpolation layer between ClockManager and LoopEngine
- Converting back to real-time for shuffle/chaos offsets
- uint16_t overflow at 34 bars (65535 / 1920)

### Solution: micros()-relative timestamps

Record events as `uint32_t offsetUs` from loop start. Events are stored in **recording-time
coordinates** (the BPM at which they were recorded) and never rescaled. Playback uses
**proportional position scaling** to map live tempo into recording timebase:
- Unlimited recording resolution (~1us)
- No PPQN conversion needed
- Immediate tempo tracking — arp and loop stay in sync, no wrap delay
- Events never need rescaling on tempo change
- One float multiply + divide per tick (~20 cycles at 240MHz, negligible)

```cpp
// Recording: store offset from first event (in recording timebase)
uint32_t offsetUs = micros() - _recordStartUs;

// Playback: proportional position scaling
// _recordBpm is set once at stopRecording(), never changes.
// Events live in recording-time coordinates forever.
uint32_t liveDurationUs   = calcLoopDurationUs(currentBPM);    // current tempo
uint32_t recordDurationUs = calcLoopDurationUs(_recordBpm);    // recording tempo
uint32_t elapsedUs   = (micros() - _playStartUs) % liveDurationUs;
uint32_t positionUs  = (uint32_t)((float)elapsedUs
                       * (float)recordDurationUs / (float)liveDurationUs);
// positionUs is in recording timebase → matches event offsets directly
```

### Bar snap (at recording close)

```cpp
_recordBpm = currentBPM;  // latch — reference timebase for all event offsets
uint32_t barDurationUs = (uint32_t)(4.0f * 60000000.0f / _recordBpm);
uint32_t recordedDurationUs = micros() - _recordStartUs;
uint16_t bars = (recordedDurationUs + barDurationUs / 2) / barDurationUs;
if (bars == 0) bars = 1;
if (bars > 64) bars = 64;  // Safety clamp
_loopLengthBars = bars;
```

After snap, all event offsets are normalized to `[0, loopDurationUs)`:
```cpp
float scale = (float)(bars * barDurationUs) / (float)recordedDurationUs;
for (uint16_t i = 0; i < _eventCount; i++) {
    _events[i].offsetUs = (uint32_t)((float)_events[i].offsetUs * scale);
}
```

---

## Data Structures

### BankType enum (KeyboardData.h:120)

```cpp
// Add to existing enum:
    BANK_LOOP   = 2,
```

### BankSlot (KeyboardData.h:135)

```cpp
// Add to existing struct:
    LoopEngine* loopEngine;           // non-null if LOOP
```

Impact: +4 bytes per BankSlot (pointer). 8 banks x 4 = 32 bytes. Negligible.

### LoopEvent (8 bytes)

```cpp
struct LoopEvent {
    uint32_t offsetUs;    // Microseconds from loop start
    uint8_t  padIndex;    // 0-47
    uint8_t  velocity;    // 0 = noteOff, >0 = noteOn
    uint8_t  _pad[2];     // Alignment padding
};
static_assert(sizeof(LoopEvent) == 8, "LoopEvent must be 8 bytes");
```

Changed from draft: `uint16_t tick` (4B) -> `uint32_t offsetUs` (8B). Eliminates PPQN mismatch and overflow.

### LoopEngine

```cpp
static const uint16_t MAX_LOOP_EVENTS   = 1024;  // 8 KB (1024 x 8 bytes)
static const uint16_t MAX_OVERDUB_EVENTS = 128;   // 1 KB
static const uint8_t  MAX_LOOP_BARS     = 64;

class LoopEngine {
    // --- Event buffer (sorted by offsetUs) ---
    LoopEvent _events[MAX_LOOP_EVENTS];       // 8 KB
    uint16_t  _eventCount;

    // --- Overdub temp buffer ---
    LoopEvent _overdubBuf[MAX_OVERDUB_EVENTS]; // 1 KB
    uint16_t  _overdubCount;

    // --- Loop dimensions ---
    uint16_t _loopLengthBars;        // 1-64, set at recording close
    float    _recordBpm;             // BPM at recording close — reference timebase for all event offsets

    // --- Playback ---
    uint16_t _cursorIdx;             // Next event to check
    uint32_t _playStartUs;           // micros() when playback started
    uint32_t _lastPositionUs;        // Previous loop position (wrap detection)

    // --- Reference-counted note tracking (live + playback unified) ---
    // Mirrors ArpEngine pattern (ArpEngine.h:130-136).
    // MIDI noteOn only sent when refcount goes 0→1.
    // MIDI noteOff only sent when refcount goes 1→0.
    // Prevents duplicate noteOn when live press coincides with loop playback,
    // and prevents premature noteOff when one source releases while the other holds.
    uint8_t  _noteRefCount[128];     // per MIDI note (not per pad)
    uint8_t  _activeNotes[48];       // velocity per pad, 0 = off (playback tracking)

    // --- State machine ---
    enum State : uint8_t { EMPTY, RECORDING, PLAYING, OVERDUBBING, STOPPED };
    State    _state;

    // --- Recording ---
    uint32_t _recordStartUs;
    bool     _waitingForFirstHit;    // true until first pad press

    // --- Effects ---
    float    _shuffleDepth;          // 0.0-1.0
    uint8_t  _shuffleTemplate;       // 0-4 (reuses arp shuffle LUTs)
    float    _chaosAmount;           // 0.0-1.0
    uint8_t  _velPatternIdx;         // 0-3 (LUT index)
    float    _velPatternDepth;       // 0.0-1.0 mix

    // --- Config ---
    uint8_t  _channel;
    const uint8_t* _padOrder;        // Pointer to global padOrder[48]
    uint8_t  _quantizeMode;          // ArpStartMode (0=immediate, 1=beat, 2=bar)

    // --- Pending events (for shuffle/chaos time offsets) ---
    struct PendingNoteOn {
        uint32_t fireTimeUs;         // micros() when to send
        uint8_t  note;
        uint8_t  velocity;
        bool     active;
    };
    static const uint8_t MAX_PENDING = 16;
    PendingNoteOn _pending[MAX_PENDING];

    // --- LED feedback ---
    bool _tickFlash;                 // Fires at loop wrap

public:
    void begin(uint8_t channel);
    void clear();

    // State transitions
    void startRecording();
    void stopRecording(const bool* keyIsPressed, const uint8_t* padOrder, float currentBPM);  // flush held pads + bar-snap -> PLAYING
    void startOverdub();
    void stopOverdub(const bool* keyIsPressed, const uint8_t* padOrder, float currentBPM);  // flush held pads + merge -> PLAYING
    void play(const ClockManager& clock, float currentBPM);  // quantized start
    void stop(MidiTransport& transport);   // flush active notes
    void flushLiveNotes(MidiTransport& transport, uint8_t channel);  // CC123 + zero refcount (bank switch)

    // Core playback (called every loop iteration)
    void tick(MidiTransport& transport, float currentBPM);
    void processEvents(MidiTransport& transport);

    // Recording input (silently drops events when buffer full)
    void recordNoteOn(uint8_t padIndex, uint8_t velocity);
    void recordNoteOff(uint8_t padIndex);

    // Context
    void setPadOrder(const uint8_t* padOrder);
    void setQuantizeMode(uint8_t mode);    // ArpStartMode enum

    // Queries
    State    getState() const;
    bool     isPlaying() const;     // PLAYING or OVERDUBBING
    uint16_t getEventCount() const;
    bool     consumeTickFlash();    // For LedController (fires at loop wrap)

    // DAW transport
    void onMidiStart(MidiTransport& transport);
    void onMidiStop(MidiTransport& transport);
};
```

Memory per engine: ~9.4 KB. Two engines = ~18.8 KB (5.9% of 320 KB SRAM).

### LoopPotStore (8 bytes, NVS per-bank)

Same pattern as `ArpPotStore` (`illpad_apot`, `arp_0`..`arp_7`): raw blob per bank, no magic/version.
Stored in `illpad_lpot`, keys `loop_0`..`loop_7`.

```cpp
struct LoopPotStore {
    uint16_t shuffleDepthRaw;   // 0-4095 (maps to 0.0-1.0)
    uint8_t  shuffleTemplate;   // 0-9 (index into shared groove templates)
    uint8_t  velPatternIdx;     // 0-3 (LUT index)
    uint16_t chaosRaw;          // 0-4095 (maps to 0.0-1.0)
    uint16_t velPatternDepthRaw; // 0-4095 (maps to 0.0-1.0)
};
static_assert(sizeof(LoopPotStore) == 8, "LoopPotStore must be 8 bytes");
```

Loaded at boot by `NvsManager::loadAll()`. Saved at runtime via dirty flags (same async
pattern as `ArpPotStore`). `baseVelocity` and `velocityVariation` are already per-bank
in `illpad_bvel` — shared by NORMAL, ARPEG, and LOOP, no duplication needed.

---

## Playback Engine — Two-Phase Model

### Why the draft's cursor scan was broken

The draft used a linear forward scan: `while (events[cursor].tick <= currentTick)`. Shuffle and chaos apply **time offsets** including negative ones — meaning an event should fire *before* its stored position. The cursor would have already passed it.

The arp system solves this with `PendingEvent` (`ArpEngine.h:21-26`): events are scheduled with a `fireTimeUs`, and `processEvents()` fires them when time arrives. The loop engine uses the same pattern.

### Phase 1 — `tick()`: proportional position scaling + scan cursor

Proportional playback: events stay in recording-time coordinates, cursor position
is scaled from live tempo into recording timebase. Tempo changes take effect
immediately — arp and loop stay in sync with no wrap delay.

```cpp
void LoopEngine::tick(MidiTransport& transport, float currentBPM) {
    if (_state != PLAYING && _state != OVERDUBBING) return;

    // Quantized start guard: _playStartUs may be in the future
    uint32_t now = micros();
    if ((int32_t)(now - _playStartUs) < 0) return;

    // Proportional position: live tempo → recording timebase
    uint32_t liveDurationUs   = calcLoopDurationUs(currentBPM);
    uint32_t recordDurationUs = calcLoopDurationUs(_recordBpm);
    uint32_t elapsedUs  = (now - _playStartUs) % liveDurationUs;
    uint32_t positionUs = (uint32_t)((float)elapsedUs
                          * (float)recordDurationUs / (float)liveDurationUs);

    // Detect wrap (position jumped backward in recording timebase)
    if (positionUs < _lastPositionUs) {
        flushActiveNotes(transport);  // noteOff for all refcount>0, then zero refcounts
        _cursorIdx = 0;
        _tickFlash = true;
        // No rebase: _playStartUs stays fixed from play(). Unsigned modulus handles
        // cycling correctly (even after micros() overflow at ~71 min). Removing the
        // rebase eliminates sub-ms inter-loop phase drift that accumulated on every
        // wrap when two loops rebased independently at slightly different times.
    }
    _lastPositionUs = positionUs;

    // Scan events — offsets are in recording timebase, positionUs matches
    while (_cursorIdx < _eventCount &&
           _events[_cursorIdx].offsetUs <= positionUs) {
        const LoopEvent& ev = _events[_cursorIdx];
        // Shuffle/chaos offsets are in real time (live tempo) — feels consistent at any BPM
        int32_t shuffleUs = calcShuffleOffsetUs(ev.offsetUs, recordDurationUs);
        int32_t chaosUs   = calcChaosOffsetUs(_cursorIdx);
        uint8_t vel       = applyVelocityPattern(ev.velocity, ev.offsetUs, recordDurationUs);

        if (ev.velocity > 0) {
            schedulePending(now + shuffleUs + chaosUs, ev.padIndex, vel);
        } else {
            uint8_t note = padToNote(ev.padIndex);
            // Refcount: only send MIDI noteOff on 1→0 transition
            if (_noteRefCount[note] > 0) {
                _noteRefCount[note]--;
                if (_noteRefCount[note] == 0) {
                    transport.sendNoteOff(note, 0, _channel);
                }
            }
            _activeNotes[ev.padIndex] = 0;
        }
        _cursorIdx++;
    }
}
```

### Phase 2 — `processEvents()`: fire pending noteOns (refcounted)

Uses `(int32_t)` cast pattern consistent with ArpEngine (`ArpEngine.cpp:448`).

```cpp
void LoopEngine::processEvents(MidiTransport& transport) {
    uint32_t now = micros();
    for (uint8_t i = 0; i < MAX_PENDING; i++) {
        if (_pending[i].active && (int32_t)(now - _pending[i].fireTimeUs) >= 0) {
            uint8_t note = _pending[i].note;
            uint8_t vel  = _pending[i].velocity;
            // Refcount: only send MIDI noteOn on 0→1 transition
            if (_noteRefCount[note] == 0) {
                transport.sendNoteOn(note, vel, _channel);
            }
            _noteRefCount[note]++;
            _activeNotes[note - LOOP_NOTE_OFFSET] = vel;
            _pending[i].active = false;
        }
    }
}
```

### Note mapping (fixed percussion)

```cpp
static const uint8_t LOOP_NOTE_OFFSET = 36; // GM kick drum
uint8_t padToNote(uint8_t padIndex) const {
    return _padOrder[padIndex] + LOOP_NOTE_OFFSET;
}
```

Requires pointer to global `padOrder[48]`, same pattern as ArpEngine.

---

## State Machine

3 dedicated LOOP control pads (assigned in Tool 3, active only on LOOP banks):

```
     REC pad            PLAY/STOP pad         CLEAR pad
     ───────            ─────────────         ─────────
       │                      │                   │
       ▼                      ▼                   ▼
     EMPTY ──[REC]──► RECORDING               [long press]
                          │                    + LED ramp
                       [REC]                      │
                          ▼                       ▼
     ┌──► PLAYING ──[REC]──► OVERDUBBING       EMPTY
     │       │                   │
     │    [REC]◄─────[REC]──────┘
     │       │
     │   [P/S]
     │       ▼
     └── STOPPED
              │
           [P/S]
              ▼
           PLAYING
```

REC and PLAY/STOP = single tap = one action. CLEAR = long press (500ms) with LED ramp feedback.

### Control pad actions

| Pad | State | Gesture | Result |
|---|---|---|---|
| **REC** | EMPTY | tap | → RECORDING |
| **REC** | RECORDING | tap | → PLAYING (bar-snap, flush held pads) |
| **REC** | PLAYING | tap | → OVERDUBBING |
| **REC** | OVERDUBBING | tap | → PLAYING (merge, flush held pads) |
| **REC** | STOPPED | tap | (ignored) |
| **PLAY/STOP** | PLAYING | tap | → STOPPED |
| **PLAY/STOP** | OVERDUBBING | tap | → STOPPED (abort overdub, discard) |
| **PLAY/STOP** | STOPPED | tap | → PLAYING (quantized start) |
| **PLAY/STOP** | EMPTY/RECORDING | tap | (ignored) |
| **CLEAR** | any except EMPTY | long press (500ms) | → EMPTY (flush all notes, clear buffer) |
| **CLEAR** | EMPTY | any | (ignored) |

### CLEAR pad safety: long press + LED ramp

Clearing a loop is destructive (ephemeral data, no undo). A single tap must NOT clear.
The CLEAR pad requires a 500ms hold. During the hold, the current bank LED shows a
red ramp-up (0% → 100% over 500ms). If the user releases early, nothing happens and
the LED returns to normal. At 500ms the clear fires and the LED flashes white (confirmation).

```cpp
// In handleLoopControls(), CLEAR pad:
bool clearPressed = state.keyIsPressed[s_clearPad];
if (clearPressed && !s_lastClearState) {
    _clearPressStart = now;  // start timing
}
if (clearPressed) {
    uint32_t held = now - _clearPressStart;
    if (held < CLEAR_LONG_PRESS_MS) {
        // Ramp: red intensity proportional to hold duration
        uint8_t ramp = (uint8_t)((uint32_t)held * 100 / CLEAR_LONG_PRESS_MS);
        s_leds.showClearRamp(ramp);  // red ramp on current bank LED
    } else if (!_clearFired) {
        eng->clear(s_transport);
        s_leds.triggerConfirm(CONFIRM_STOP);
        _clearFired = true;
    }
} else {
    if (s_lastClearState) {
        // Released — cancel ramp if clear didn't fire
        _clearFired = false;
    }
}
s_lastClearState = clearPressed;
```

### Why 3 pads instead of 1 overloaded play/stop

The original draft crammed 5 transitions (rec, stop rec, overdub, stop overdub, clear)
onto the existing play/stop pad using double-tap and long-press. Problems:
- Long-press on capacitive pads is ambiguous (no mechanical click, finger surface varies)
- Double-tap conflicts with the arp hold system (same gesture, different meaning)
- The state machine required timing windows, making it fragile on stage

With 3 dedicated pads:
- REC and PLAY/STOP = single tap edge detect. Same code pattern as bank switch.
- CLEAR = protected long press with visual feedback. The only timed gesture, and it's safe.
- Both hands free for drumming — no hold modifier needed (critical for percussion)
- `handlePlayStopPad()` for arp is UNCHANGED — zero regression risk
- The pads only consume pad slots on LOOP banks. On NORMAL/ARPEG they're regular music pads.

### Pad assignment in Tool 3

Tool 3 (Pad Roles) gains a 4th category:

```
Current:  BANK(8) + SCALE(15) + ARP(6: hold + play/stop + 4 octave) = 29 pads
New:      BANK(8) + SCALE(15) + ARP(6) + LOOP(3: rec + play/stop + clear) = 32 pads
```

16 pads remain for music. A drum kit rarely exceeds 16 sounds — this is generous.
The 3 LOOP pads should be physically grouped for ergonomics (adjacent on the circular layout).

NVS storage: new namespace `illpad_lpad`, key `"pads"`. Same pattern as `ArpPadStore`.

```cpp
#define LOOP_PAD_NVS_NAMESPACE "illpad_lpad"
#define LOOPPAD_NVS_KEY        "pads"
#define LOOPPAD_VERSION        1

struct LoopPadStore {
    uint16_t magic;        // EEPROM_MAGIC
    uint8_t  version;      // LOOPPAD_VERSION
    uint8_t  reserved;
    uint8_t  recPad;       // pad index for REC (0xFF = unassigned)
    uint8_t  playStopPad;  // pad index for PLAY/STOP
    uint8_t  clearPad;     // pad index for CLEAR
    uint8_t  _pad;         // alignment to 8 bytes
};
static_assert(sizeof(LoopPadStore) == 8, "LoopPadStore must be 8 bytes");

inline void validateLoopPadStore(LoopPadStore& s) {
    if (s.recPad >= NUM_KEYS)      s.recPad = 0xFF;
    if (s.playStopPad >= NUM_KEYS) s.playStopPad = 0xFF;
    if (s.clearPad >= NUM_KEYS)    s.clearPad = 0xFF;
}
```

### Bank switch denied during RECORDING/OVERDUBBING

Bank switching is **blocked** while any foreground LOOP bank is in RECORDING or OVERDUBBING state.
The user must close the recording (tap → PLAYING) or stop (double-tap → STOPPED) before switching.
This prevents dangling recording state and ambiguous background behavior.

Guard in `BankManager::switchToBank()` (BankManager.cpp:117):

```cpp
void BankManager::switchToBank(uint8_t newBank) {
  if (newBank >= NUM_BANKS || newBank == _currentBank) return;

  // --- LOOP recording lock: deny bank switch while recording/overdubbing ---
  BankSlot& cur = _banks[_currentBank];
  if (cur.type == BANK_LOOP && cur.loopEngine) {
    LoopEngine::State ls = cur.loopEngine->getState();
    if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
      return;  // Silently deny — user must close recording first
    }
  }

  // --- Flush live LOOP notes on outgoing bank ---
  // LOOP bypasses MidiEngine (uses s_transport.sendNoteOn/Off directly),
  // so _engine->allNotesOff() below does NOT reach live LOOP notes.
  // Without this flush, held pads on a LOOP bank produce stuck notes
  // on bank switch because processLoopMode() stops running for that bank.
  if (cur.type == BANK_LOOP && cur.loopEngine && _transport) {
    cur.loopEngine->flushLiveNotes(*_transport, _currentBank);
  }

  // All notes off on old bank's channel (handles NORMAL + ARPEG)
  // ... (existing code unchanged)
}
```

**`flushLiveNotes()`** sends noteOff for any note with refcount > 0 that came from
live play (not loop playback). Since we can't distinguish the source of each refcount
increment, the simplest safe approach is to send CC123 (All Notes Off) on the outgoing
channel — this is the same panic message `MidiEngine::allNotesOff()` sends for NORMAL banks,
but routed through `MidiTransport` directly:

```cpp
void LoopEngine::flushLiveNotes(MidiTransport& transport, uint8_t channel) {
    // CC123 All Notes Off — brute-force silence for this channel
    transport.sendAllNotesOff(channel);
    // Zero the refcount array so playback doesn't see stale live-play state
    memset(_noteRefCount, 0, sizeof(_noteRefCount));
}
```

This fires once on bank switch. The background loop playback continues — its next noteOn
will increment refcount from 0 (fresh start), and the corresponding noteOff will decrement
to 0. No orphan notes.

**Note:** MIDI transport messages (Start/Stop/Continue) are ignored system-wide. See DAW transport section.

### Playback start quantize

Reuses `ArpStartMode` enum (`HardwareConfig.h:201-208`). Per-bank, set in Tool 4 alongside arp quantize.

| Mode | Behavior |
|---|---|
| Immediate | `_playStartUs = micros()` — play now |
| Beat | Delay start to next beat (24 ticks = 1 quarter note) |
| Bar | Delay start to next bar (96 ticks = 4 quarter notes) |

Applies to: STOPPED -> PLAYING transition. Recording close (RECORDING -> PLAYING) always uses Immediate — the loop should start playing back what you just recorded without waiting.

```cpp
void LoopEngine::play(const ClockManager& clock, float currentBPM) {
    if (_state != STOPPED && _state != RECORDING) return;

    if (_quantizeMode == ARP_START_IMMEDIATE || _state == RECORDING) {
        _playStartUs = micros();
    } else {
        // Compute delay to next boundary using 24 PPQN clock
        uint32_t currentTick = clock.getCurrentTick();
        uint32_t boundary = (_quantizeMode == ARP_START_BEAT) ? 24 : 96;
        uint32_t ticksToNext = boundary - (currentTick % boundary);
        if (ticksToNext == boundary) ticksToNext = 0;  // Already on boundary

        uint32_t usPerTick = 60000000UL / ((uint32_t)currentBPM * 24);
        _playStartUs = micros() + (ticksToNext * usPerTick);
    }

    _cursorIdx = 0;
    _lastPositionUs = 0;
    _state = PLAYING;
}
```

Stop is always immediate (same as arp — `ArpStartMode` only gates start, not stop).

### Recording flow

**First recording (EMPTY -> RECORDING):**
1. `_waitingForFirstHit = true` — tick counter does NOT start on tap
2. First pad press: `_recordStartUs = micros()`, first event at offsetUs = 0
3. Subsequent noteOn/noteOff stored with `micros() - _recordStartUs`
4. Tap play/stop to close -> flush held pads (inject noteOff) -> bar-snap -> sort by offsetUs -> PLAYING

**Overdub (PLAYING -> OVERDUBBING):**
1. Loop continues playing normally
2. New events into `_overdubBuf[]` with offsetUs relative to current loop position
3. Tap to close -> `flushHeldPadsToOverdub()` -> sorted merge into `_events[]`
4. If `_eventCount + _overdubCount > MAX_LOOP_EVENTS`: newest overdub events dropped

**Critical: flush held pads on overdub close.** If the user is still holding pads
when they tap play/stop to close the overdub, the noteOn was recorded but the noteOff
never was. Without the flush, the merged loop has orphan noteOns → stuck notes until
loop wrap.

```cpp
// Called by stopOverdub() BEFORE mergeOverdub()
// Injects noteOff for any pad whose noteOn was recorded during this overdub
void LoopEngine::flushHeldPadsToOverdub(const bool* keyIsPressed,
                                         const uint8_t* padOrder,
                                         float currentBPM) {
    uint32_t liveDurationUs   = calcLoopDurationUs(currentBPM);
    uint32_t recordDurationUs = calcLoopDurationUs(_recordBpm);
    uint32_t elapsedUs = (micros() - _playStartUs) % liveDurationUs;
    uint32_t positionUs = (uint32_t)((float)elapsedUs
                          * (float)recordDurationUs / (float)liveDurationUs);

    for (uint8_t i = 0; i < 48; i++) {
        if (keyIsPressed[i] && _overdubCount < MAX_OVERDUB_EVENTS) {
            _overdubBuf[_overdubCount++] = {positionUs, padOrder[i], 0, false};
            // false = noteOff (isNoteOn field)
        }
    }
}
```

The matching live noteOff will still fire later via `processLoopMode()` when the
user releases the pad, but the refcount handles this: the playback noteOff from
the injected event decrements to 0 (MIDI sent), and the later live release
decrements from 0 → clamped (no double noteOff).

### Record methods (with buffer overflow guard)

```cpp
void LoopEngine::recordNoteOn(uint8_t padIndex, uint8_t velocity) {
    // Guard: silently drop events when buffer full.
    // Without this, writing past _events[MAX_LOOP_EVENTS-1] corrupts adjacent memory.
    uint16_t maxEvents = (_state == OVERDUBBING) ? MAX_OVERDUB_EVENTS : MAX_LOOP_EVENTS;
    uint16_t& count    = (_state == OVERDUBBING) ? _overdubCount : _eventCount;
    LoopEvent* buf     = (_state == OVERDUBBING) ? _overdubBuf : _events;
    if (count >= maxEvents) return;

    uint32_t offsetUs = currentPositionUs();  // recording-timebase offset
    buf[count++] = {offsetUs, padIndex, velocity, true};  // true = noteOn
}

void LoopEngine::recordNoteOff(uint8_t padIndex) {
    uint16_t maxEvents = (_state == OVERDUBBING) ? MAX_OVERDUB_EVENTS : MAX_LOOP_EVENTS;
    uint16_t& count    = (_state == OVERDUBBING) ? _overdubCount : _eventCount;
    LoopEvent* buf     = (_state == OVERDUBBING) ? _overdubBuf : _events;
    if (count >= maxEvents) return;

    uint32_t offsetUs = currentPositionUs();
    buf[count++] = {offsetUs, padIndex, 0, false};  // false = noteOff
}
```

### Overdub merge

**Why not naive insertion sort?** The old approach appended overdub events then
insertion-sorted the full array. At max capacity (1024 + 128 = 1152 elements),
insertion sort is O(n²) ≈ 83ms — an audible gap in the main loop.

**The key insight:** `_events[]` is already sorted. `_overdubBuf[]` is NOT sorted
(loop wrap resets position to 0 mid-overdub), but it is small (max 128 elements).

**Two-step approach (~0.75ms total):**
1. Sort `_overdubBuf` with insertion sort — O(m²) where m ≤ 128 ≈ 0.7ms
2. Reverse merge two sorted arrays into `_events[]` — O(n+m) ≈ 50µs

The reverse merge writes from the back of `_events[]`, so it never overwrites
unread base elements (output index k starts at i+toMerge, always ahead of read index i).

```cpp
void LoopEngine::mergeOverdub() {
    uint16_t toMerge = _overdubCount;
    if (_eventCount + toMerge > MAX_LOOP_EVENTS) {
        toMerge = MAX_LOOP_EVENTS - _eventCount;
    }
    if (toMerge == 0) { _overdubCount = 0; return; }

    // Step 1: sort overdub buffer (may be unsorted due to loop wrap mid-overdub)
    // Insertion sort on ≤128 elements: ~0.7ms at 240MHz. Negligible.
    for (uint16_t i = 1; i < toMerge; i++) {
        LoopEvent tmp = _overdubBuf[i];
        int16_t j = i - 1;
        while (j >= 0 && _overdubBuf[j].offsetUs > tmp.offsetUs) {
            _overdubBuf[j + 1] = _overdubBuf[j];
            j--;
        }
        _overdubBuf[j + 1] = tmp;
    }

    // Step 2: reverse merge — write from back to front
    // Both _events[0.._eventCount-1] and _overdubBuf[0..toMerge-1] are sorted.
    // Output fills _events[0..newCount-1] in-place without overwriting unread data.
    uint16_t newCount = _eventCount + toMerge;
    int16_t i = (int16_t)_eventCount - 1;   // last base element
    int16_t j = (int16_t)toMerge - 1;       // last overdub element
    int16_t k = (int16_t)newCount - 1;      // last output position

    while (i >= 0 && j >= 0) {
        if (_events[i].offsetUs >= _overdubBuf[j].offsetUs) {
            _events[k--] = _events[i--];
        } else {
            _events[k--] = _overdubBuf[j--];
        }
    }
    // Drain remaining overdub elements (base elements are already in place)
    while (j >= 0) {
        _events[k--] = _overdubBuf[j--];
    }

    _eventCount = newCount;
    _overdubCount = 0;
}
```

---

## Effects

### Shuffle (reuses arp LUTs)

Currently `SHUFFLE_TEMPLATES[5][16]` is `static const` in `ArpEngine.cpp:17` — not accessible from LoopEngine. **Must extract to shared header** (see Refactoring section).

```cpp
int32_t LoopEngine::calcShuffleOffsetUs(uint32_t eventOffsetUs, uint32_t loopDurationUs) {
    uint32_t barDurationUs = loopDurationUs / _loopLengthBars;
    uint32_t posInBar = eventOffsetUs % barDurationUs;
    uint32_t stepDurationUs = barDurationUs / 16;
    uint8_t stepInBar = posInBar / stepDurationUs;
    if (stepInBar > 15) stepInBar = 15;

    int8_t templateVal = SHUFFLE_TEMPLATES[_shuffleTemplate][stepInBar];
    return (int32_t)((float)templateVal * _shuffleDepth * (float)stepDurationUs / 100.0f);
}
```

### Chaos / Jitter (deterministic per-event)

```cpp
int32_t LoopEngine::calcChaosOffsetUs(uint16_t eventIndex) {
    if (_chaosAmount < 0.001f) return 0;

    uint32_t hash = eventIndex * 7919;
    hash ^= (hash >> 13);
    hash *= 0x5bd1e995;
    hash ^= (hash >> 15);

    uint32_t barDurationUs = calcLoopDurationUs(_recordBpm) / _loopLengthBars;
    uint32_t stepDurationUs = barDurationUs / 16;
    int32_t maxOffsetUs = stepDurationUs / 2;

    int32_t offset = (int32_t)(hash % (maxOffsetUs * 2 + 1)) - maxOffsetUs;
    return (int32_t)((float)offset * _chaosAmount);
}
```

Deterministic: same event index always gets same jitter, repeatable across loop iterations.

### Velocity Patterns

```cpp
static const uint8_t VEL_PATTERNS[4][16] = {
    {100,60,80,60, 100,60,80,60, 100,60,80,60, 100,60,80,60}, // accent 1&3
    {100,40,70,40, 90,40,70,40, 100,40,70,40, 90,40,70,40},   // strong downbeat
    {80,80,80,100, 80,80,80,100, 80,80,80,100, 80,80,80,100}, // backbeat
    {100,70,85,55, 95,65,80,50, 100,70,85,55, 95,65,80,50},   // swing feel
};

uint8_t LoopEngine::applyVelocityPattern(uint8_t origVel, uint32_t offsetUs, uint32_t loopDurationUs) {
    if (_velPatternDepth < 0.001f) return origVel;
    uint32_t barDurationUs = loopDurationUs / _loopLengthBars;
    uint8_t step = (offsetUs % barDurationUs) / (barDurationUs / 16);
    if (step > 15) step = 15;
    uint8_t patternVel = VEL_PATTERNS[_velPatternIdx][step];
    uint8_t scaledPattern = (uint8_t)((float)origVel * (float)patternVel / 100.0f);
    uint8_t result = (uint8_t)((float)origVel * (1.0f - _velPatternDepth) +
                               (float)scaledPattern * _velPatternDepth);
    return (result < 1) ? 1 : (result > 127) ? 127 : result;
}
```

---

## Pot Mapping

### New PotTargets (add to PotRouter.h:11, before TARGET_MIDI_CC)

```cpp
    // LOOP per-bank
    TARGET_CHAOS,             // 0.0-1.0
    TARGET_VEL_PATTERN,       // 0-3 discrete
    TARGET_VEL_PATTERN_DEPTH, // 0.0-1.0
```

Existing targets reused: `TARGET_TEMPO_BPM`, `TARGET_BASE_VELOCITY`, `TARGET_VELOCITY_VARIATION`, `TARGET_SHUFFLE_DEPTH`, `TARGET_SHUFFLE_TEMPLATE`.

### PotRouter new members and applyBinding cases

Add members to PotRouter (alongside existing `_shuffleDepth`, `_gateLength`, etc.):
```cpp
    // LOOP-specific params (PotRouter.h)
    float   _chaosAmount;        // 0.0-1.0
    uint8_t _velPatternIdx;      // 0-3
    float   _velPatternDepth;    // 0.0-1.0
```

Add cases to `applyBinding()` switch (PotRouter.cpp:455+):
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

Add getters:
```cpp
    float   getChaosAmount() const       { return _chaosAmount; }
    uint8_t getVelPatternIdx() const     { return _velPatternIdx; }
    float   getVelPatternDepth() const   { return _velPatternDepth; }
```

Add to `isPerBankTarget()` (PotRouter.cpp:577):
```cpp
    case TARGET_CHAOS:
    case TARGET_VEL_PATTERN:
    case TARGET_VEL_PATTERN_DEPTH:
        return true;
```

Add to `getRangeForTarget()` and `seedCatchValues()` following existing patterns.

### Bank switch: loadStoredPerBank for LOOP (main.cpp, after existing arp block ~line 468)

The existing `loadStoredPerBank()` at `main.cpp:468-479` only loads ARPEG params (gate,
shuffleDepth, division, pattern, shuffleTemplate). When switching TO a LOOP bank, these
arp-specific values are irrelevant, and LOOP-specific params (chaos, velPattern,
velPatternDepth) have no load path. The catch system will reseed from stale PotRouter
values, causing incorrect catch targets.

**Fix**: add a LOOP branch alongside the ARPEG branch:

```cpp
    // existing ARPEG block (lines 468-474) — unchanged
    if (newSlot.type == BANK_ARPEG && newSlot.arpEngine) {
      gate      = newSlot.arpEngine->getGateLength();
      // ... etc
    }

    // NEW: LOOP block — load LOOP-specific per-bank params
    float chaos = 0.0f;
    uint8_t velPat = 0;
    float velPatDepth = 0.0f;
    if (newSlot.type == BANK_LOOP && newSlot.loopEngine) {
      chaos       = newSlot.loopEngine->getChaosAmount();
      velPat      = newSlot.loopEngine->getVelPatternIdx();
      velPatDepth = newSlot.loopEngine->getVelPatternDepth();
      // Shared params: shuffleDepth and shuffleTemplate are also per-bank for LOOP
      shufDepth   = newSlot.loopEngine->getShuffleDepth();
      shufTmpl    = newSlot.loopEngine->getShuffleTemplate();
    }

    s_potRouter.loadStoredPerBank(
      newSlot.baseVelocity, newSlot.velocityVariation, newSlot.pitchBendOffset,
      gate, shufDepth, div, pat, shufTmpl
    );
    // NEW: load LOOP-specific params into PotRouter
    s_potRouter.loadStoredPerBankLoop(chaos, velPat, velPatDepth);
    s_potRouter.resetPerBankCatch();
```

**`loadStoredPerBankLoop()`** is a new PotRouter method:

```cpp
void PotRouter::loadStoredPerBankLoop(float chaos, uint8_t velPat, float velPatDepth) {
    _chaosAmount      = chaos;
    _velPatternIdx    = velPat;
    _velPatternDepth  = velPatDepth;
}
```

When the foreground bank is NOT LOOP, the LOOP params default to 0 (harmless — they are
only read by the pot-to-LoopEngine routing block, which is gated by `BANK_LOOP`).

### Main loop: pot-to-LoopEngine routing (main.cpp, after arp routing block ~line 707)

Mirrors the existing arp routing pattern at `main.cpp:698-707`:

```cpp
  // --- Musical params: push live to LoopEngine (if LOOP bank) ---
  // Same pattern as arp routing above (main.cpp:698-707)
  if (potSlot.type == BANK_LOOP && potSlot.loopEngine) {
    potSlot.loopEngine->setShuffleDepth(s_potRouter.getShuffleDepth());
    potSlot.loopEngine->setShuffleTemplate(s_potRouter.getShuffleTemplate());
    potSlot.loopEngine->setChaosAmount(s_potRouter.getChaosAmount());
    potSlot.loopEngine->setVelPatternIdx(s_potRouter.getVelPatternIdx());
    potSlot.loopEngine->setVelPatternDepth(s_potRouter.getVelPatternDepth());
    potSlot.loopEngine->setBaseVelocity(s_potRouter.getBaseVelocity());
    potSlot.loopEngine->setVelocityVariation(s_potRouter.getVelocityVariation());
  }
```

**Shared targets** (`TARGET_SHUFFLE_DEPTH`, `TARGET_SHUFFLE_TEMPLATE`) write to PotRouter's
single `_shuffleDepth`/`_shuffleTemplate` members. The main loop routes them to the correct
engine based on the foreground bank type. This works because:
- PotRouter is the intermediary — it stores the value
- Main loop reads PotRouter getters and writes to the active engine
- Bank switch triggers `resetPerBankCatch()` which reseeds from stored values

### LOOP default mapping

| Pot | Alone | + hold left |
|---|---|---|
| Right 1 | Tempo | Chaos |
| Right 2 | Base Velocity | Shuffle Depth |
| Right 3 | Vel Pattern (4 discrete) | Shuffle Template (5 discrete) |
| Right 4 | Vel Pattern Depth | Velocity Variation |

8 params for 8 slots. Fits existing slot architecture.

### PotMappingStore rewrite (PotRouter.h:60)

No backward compatibility — rewrite from scratch. Old NVS data is discarded on first boot.

```cpp
// New: normalMap[8] + arpegMap[8] + loopMap[8] = 52 bytes
struct PotMappingStore {
    uint16_t   magic;
    uint8_t    version;        // 1 (fresh format, no migration)
    uint8_t    reserved;
    PotMapping normalMap[8];
    PotMapping arpegMap[8];
    PotMapping loopMap[8];     // <-- ADD
};
```

On load, if size mismatch (old 36B vs new 52B): `loadBlob()` returns false → defaults applied. No migration code needed.

### ToolPotMapping refactor (ToolPotMapping.h)

```cpp
// Current: bool _contextNormal (true/false toggle)
// Change to:
uint8_t _contextIdx;  // 0=NORMAL, 1=ARPEG, 2=LOOP

// ENTER cycles: _contextIdx = (_contextIdx + 1) % 3;
static const PotTarget LOOP_PARAMS[] = {
    TARGET_TEMPO_BPM, TARGET_BASE_VELOCITY, TARGET_VEL_PATTERN,
    TARGET_VEL_PATTERN_DEPTH, TARGET_SHUFFLE_DEPTH,
    TARGET_SHUFFLE_TEMPLATE, TARGET_CHAOS, TARGET_VELOCITY_VARIATION
};
```

---

## Pad Roles in LOOP Mode

| Pad role | In LOOP mode |
|---|---|
| Scale pads (15) | Free as music pads |
| Octave pads (4) | Free as music pads |
| Hold pad | Free as music pad (no function in LOOP mode) |
| Arp play/stop pad | Free as music pad (LOOP has its own play/stop) |
| Bank pads (8) | Unchanged |
| **LOOP rec pad** | Toggle REC/OVERDUB (LOOP only) |
| **LOOP play/stop pad** | Toggle PLAY/STOP (LOOP only) |
| **LOOP clear pad** | Long press → EMPTY (LOOP only) |

On NORMAL/ARPEG banks, the 3 LOOP pads behave as regular music pads.
On LOOP banks, the arp hold pad, arp play/stop pad, scale pads, and octave pads
are freed as music pads (their functions are meaningless for percussion loops).

### ScaleManager impact

**Root/mode/chromatic pads (ScaleManager.cpp:123-187):** Currently process unconditionally
for all bank types. On a LOOP bank, scale changes are meaningless (LOOP bypasses
ScaleResolver) but would still modify `slot.scale`, fire a yellow confirmation blink,
and trigger a wasted NVS write. **Must guard**: skip root/mode/chromatic processing
when `slot.type == BANK_LOOP`.

```cpp
// Add at top of processScalePads(), before root pad loop:
if (slot.type == BANK_LOOP) {
    // LOOP bypasses scale resolution — scale pads are dead in control mode.
    // Still update _lastScaleKeys to prevent phantom edges on bank switch.
    for (uint8_t r = 0; r < 7; r++) {
        if (_rootPads[r] < NUM_KEYS) _lastScaleKeys[_rootPads[r]] = keyIsPressed[_rootPads[r]];
        if (_modePads[r] < NUM_KEYS) _lastScaleKeys[_modePads[r]] = keyIsPressed[_modePads[r]];
    }
    if (_chromaticPad < NUM_KEYS) _lastScaleKeys[_chromaticPad] = keyIsPressed[_chromaticPad];
    // Fall through to hold/octave section (hold pad also excluded by BANK_ARPEG guard)
    goto scaleSkipToHold;  // or restructure with an if/else
}
```

Alternative (cleaner, no goto): wrap the three loops (root, mode, chromatic) in
`if (slot.type != BANK_LOOP) { ... }` and keep the `_lastScaleKeys` sync outside the guard.

**Hold/octave pads (ScaleManager.cpp:189-226):** Already guarded by `slot.type == BANK_ARPEG`.
No changes needed — the existing guard excludes LOOP banks.

---

## LED Feedback — SK6812 RGBW NeoPixel

LOOP uses **red/magenta** color family — visually distinct from white (NORMAL) and blue (ARPEG).

### New colors (add to HardwareConfig.h)

```cpp
// LOOP colors (red/magenta family) — RGBW, W=0 for pure chromatic
const RGBW COL_LOOP        = {255,   0,  60,  0};  // LOOP foreground — hot magenta
const RGBW COL_LOOP_DIM    = { 40,   0,  10,  0};  // LOOP background
const RGBW COL_LOOP_REC    = {255,   0,  40,  0};  // Recording — red-magenta (distinct from COL_ERROR {255,0,0,0})
```

### Intensity constants (add to HardwareConfig.h)

```cpp
// Foreground LOOP (modulate R channel)
const uint8_t LED_FG_LOOP_STOP_MIN     = 51;   // Stopped + EMPTY: sine 20%
const uint8_t LED_FG_LOOP_STOP_MAX     = 128;  // Stopped + EMPTY: sine 50%
const uint8_t LED_FG_LOOP_PLAY_MIN     = 77;   // Playing: sine 30%
const uint8_t LED_FG_LOOP_PLAY_MAX     = 230;  // Playing: sine 90%
const uint8_t LED_FG_LOOP_PLAY_FLASH   = 255;  // Wrap flash spike 100%

// Background LOOP
const uint8_t LED_BG_LOOP_STOP_MIN     = 13;   // Stopped: sine 5%
const uint8_t LED_BG_LOOP_STOP_MAX     = 40;   // Stopped: sine 16%
const uint8_t LED_BG_LOOP_PLAY_MIN     = 20;   // Playing: sine 8%
const uint8_t LED_BG_LOOP_PLAY_MAX     = 51;   // Playing: sine 20%
const uint8_t LED_BG_LOOP_PLAY_FLASH   = 64;   // Wrap flash 25%
```

### Multi-bank display states

| State | Color | Pattern | Foreground | Background |
|---|---|---|---|---|
| EMPTY | `COL_LOOP` (magenta) | Slow sine pulse | 25%-30% (matches ARPEG stopped weight) | Off (0%) |
| RECORDING | `COL_LOOP_REC` (red-magenta) | Fast blink 200ms | 0%/100% | N/A (bank switch denied) |
| PLAYING | `COL_LOOP` (magenta) | Sine pulse + wrap flash | 30%-90%, spike 100% | 8%-20%, spike 25% |
| OVERDUBBING | `COL_LOOP_REC` (red-magenta) | Fast pulse 150ms + sine | 50%/100% | N/A (bank switch denied) |
| STOPPED | `COL_LOOP` (magenta) | Slow sine | 20%-50% | 5%-16% |

### New ConfirmType values (add to LedController.h:16)

```cpp
    CONFIRM_LOOP_REC     = 10,  // Recording/overdub started
```

### Confirmation rendering specs

| ConfirmType | Color | Pattern | Duration |
|---|---|---|---|
| `CONFIRM_LOOP_REC` | `COL_LOOP_REC` (red-magenta) | Double blink current LED | 200ms (2 × LED_CONFIRM_UNIT_MS) |

### LedController.cpp changes

In the normal bank display section (`LedController.cpp:530+`), add LOOP branch alongside NORMAL/ARPEG:

```cpp
} else if (slot.type == BANK_LOOP && slot.loopEngine) {
    LoopEngine::State ls = slot.loopEngine->getState();
    bool playing = (ls == LoopEngine::PLAYING || ls == LoopEngine::OVERDUBBING);

    if (isFg) {
        if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
            // Fast red blink
            bool on = ((millis() / 150) % 2 == 0);
            setPixelScaled(i, COL_LOOP_REC, on ? 255 : 0);
        } else if (playing) {
            // Magenta sine pulse + wrap flash
            uint8_t sineVal = mapSine(LED_FG_LOOP_PLAY_MIN, LED_FG_LOOP_PLAY_MAX);
            if (slot.loopEngine->consumeTickFlash()) _flashStartTime[i] = millis();
            if (_flashStartTime[i] && (millis() - _flashStartTime[i]) < LED_TICK_FLASH_DURATION_MS) {
                sineVal = LED_FG_LOOP_PLAY_FLASH;
            }
            setPixelScaled(i, COL_LOOP, sineVal);
        } else if (ls == LoopEngine::STOPPED) {
            uint8_t sineVal = mapSine(LED_FG_LOOP_STOP_MIN, LED_FG_LOOP_STOP_MAX);
            setPixelScaled(i, COL_LOOP, sineVal);
        } else { // EMPTY — slow sine pulse, visible like ARPEG stopped
            uint8_t sineVal = mapSine(LED_FG_LOOP_STOP_MIN, LED_FG_LOOP_STOP_MAX);
            setPixelScaled(i, COL_LOOP, sineVal);
        }
    } else {
        // Background
        if (playing) {
            uint8_t sineVal = mapSine(LED_BG_LOOP_PLAY_MIN, LED_BG_LOOP_PLAY_MAX);
            if (slot.loopEngine->consumeTickFlash()) _flashStartTime[i] = millis();
            if (_flashStartTime[i] && (millis() - _flashStartTime[i]) < LED_TICK_FLASH_DURATION_MS) {
                sineVal = LED_BG_LOOP_PLAY_FLASH;
            }
            setPixelScaled(i, COL_LOOP, sineVal);
        } else if (ls == LoopEngine::STOPPED) {
            uint8_t sineVal = mapSine(LED_BG_LOOP_STOP_MIN, LED_BG_LOOP_STOP_MAX);
            setPixelScaled(i, COL_LOOP, sineVal);
        }
        // EMPTY background = off (default)
    }
}
```

Bargraph and bank-switch blink colors also need LOOP context:
```cpp
// Bargraph bar color (LedController.cpp:281)
if (_slots && _slots[_currentBank].type == BANK_LOOP) {
    barColor = COL_LOOP;
    barDim   = COL_LOOP_DIM;
}

// Bank switch blink color (LedController.cpp:625)
if (_slots && _slots[_currentBank].type == BANK_LOOP) {
    blinkColor = COL_LOOP;
}
```

---

## System Integration

### Static instantiation (main.cpp)

```cpp
#if ENABLE_LOOP_MODE
static LoopEngine s_loopEngines[2];  // Max 2 LOOP banks — 18.8 KB static SRAM
#endif

// At end of setup(), verify headroom:
// #if DEBUG_SERIAL
// Serial.printf("[HEAP] Free: %u bytes\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
// #endif

// In setup(), after bank type loading:
uint8_t loopIdx = 0;
for (uint8_t i = 0; i < NUM_BANKS && loopIdx < 2; i++) {
    if (s_banks[i].type == BANK_LOOP) {
        s_loopEngines[loopIdx].begin(i);
        s_loopEngines[loopIdx].setPadOrder(s_padOrder);
        s_banks[i].loopEngine = &s_loopEngines[loopIdx];
        loopIdx++;
    }
}
```

### Main loop integration

No ArpScheduler rename. LoopEngine tick/processEvents called directly:

```
 8.  handlePlayStopPad()                   // ARPEG only — UNCHANGED
 8b. handleLoopControls()                  // NEW — 3 dedicated LOOP pads
 9.  processNormalMode() or processArpMode() or processLoopMode()
10.  ArpScheduler.tick()                    // unchanged
10b. ArpScheduler.processEvents()           // unchanged
10c. Loop engines: tick()                   // NEW
10d. Loop engines: processEvents()          // NEW
11.  MidiEngine.flush()
```

**Critical**: `handleLoopControls()` is a separate function from `handlePlayStopPad()`.
The arp play/stop code is UNTOUCHED — zero regression risk. LOOP controls run at the
same priority level, outside the `isHolding()` guard, so drumming with both hands works.

New statics needed in main.cpp:
```cpp
static uint8_t  s_recPad       = 0xFF;    // pad index for LOOP REC (from Tool 3 / NVS)
static uint8_t  s_loopPlayPad  = 0xFF;    // pad index for LOOP PLAY/STOP
static uint8_t  s_clearPad     = 0xFF;    // pad index for LOOP CLEAR
static bool     s_lastRecState = false;
static bool     s_lastLoopPlayState = false;
static bool     s_lastClearState = false;
static uint32_t s_clearPressStart = 0;
static bool     s_clearFired = false;
static const uint32_t CLEAR_LONG_PRESS_MS = 500;
```

```cpp
static void handleLoopControls(const SharedKeyboardState& state, uint32_t now) {
    BankSlot& slot = s_bankManager.getCurrentSlot();
    if (slot.type != BANK_LOOP || !slot.loopEngine) {
      // Not a LOOP bank — reset edge states, pads act as music
      s_lastRecState = (s_recPad < NUM_KEYS) ? state.keyIsPressed[s_recPad] : false;
      s_lastLoopPlayState = (s_loopPlayPad < NUM_KEYS) ? state.keyIsPressed[s_loopPlayPad] : false;
      s_lastClearState = (s_clearPad < NUM_KEYS) ? state.keyIsPressed[s_clearPad] : false;
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

### processLoopMode() (inline in loop())

**Important**: LOOP bypasses ScaleResolver — uses `s_transport.sendNoteOn/Off()` directly
(not `s_midiEngine.noteOn()` which takes padIndex+scale). Live presses go through the
same `_noteRefCount[]` as loop playback to prevent duplicate noteOn/premature noteOff.

**Refcount during overdub**: A live noteOn that is suppressed by refcount (>0 because
loop playback already sounding that note) is still recorded in the overdub buffer.
This is correct — on next pure playback, both events fire through refcount and balance:
original noteOn (0→1, MIDI), overdub noteOn (1→2, no MIDI), original noteOff (2→1,
no MIDI), overdub noteOff (1→0, MIDI). No stuck notes. The refcount handles all
overlap cases. See `flushHeldPadsToOverdub()` for the held-pad edge case.

```cpp
if (slot.type == BANK_LOOP && slot.loopEngine) {
    LoopEngine* eng = slot.loopEngine;
    for (uint8_t p = 0; p < NUM_KEYS; p++) {
        if (p == s_recPad || p == s_loopPlayPad || p == s_clearPad) continue;
        bool pressed = keyIsPressed[p];
        bool wasPressed = lastKeys[p];
        if (pressed && !wasPressed) {
            uint8_t note = eng->padToNote(p);
            uint8_t vel = slot.baseVelocity; // +/- variation applied here
            // Refcount: only send MIDI noteOn on 0→1
            if (eng->noteRefIncrement(note)) {
                s_transport.sendNoteOn(note, vel, slot.channel);
            }
            if (eng->getState() == LoopEngine::RECORDING || eng->getState() == LoopEngine::OVERDUBBING) {
                eng->recordNoteOn(p, vel);
            }
        } else if (!pressed && wasPressed) {
            uint8_t note = eng->padToNote(p);
            // Refcount: only send MIDI noteOff on 1→0
            if (eng->noteRefDecrement(note)) {
                s_transport.sendNoteOff(note, 0, slot.channel);
            }
            if (eng->getState() == LoopEngine::RECORDING || eng->getState() == LoopEngine::OVERDUBBING) {
                eng->recordNoteOff(p);
            }
        }
    }
}
```

Refcount helpers (in LoopEngine):
```cpp
// Returns true if this is the 0→1 transition (caller should send noteOn)
bool noteRefIncrement(uint8_t note) {
    return (_noteRefCount[note]++ == 0);
}
// Returns true if this is the 1→0 transition (caller should send noteOff)
bool noteRefDecrement(uint8_t note) {
    if (_noteRefCount[note] > 0) {
        return (--_noteRefCount[note] == 0);
    }
    return false;
}
```

### DAW transport

MIDI Start (0xFA), Stop (0xFC), and Continue (0xFB) are **ignored** by the ILLPAD.
The clock (0xF8) is always received for tempo sync, but transport commands do not
affect loop or arp state. This simplification was a deliberate design decision —
the ILLPAD is an instrument, not a transport follower.

### ToolBankConfig (ToolBankConfig.cpp:151)

3-way cycle: NORMAL -> ARPEG -> LOOP -> NORMAL. Independent pool constraints: max 4 ARPEG, max 2 LOOP. Quantize mode shown for both ARPEG and LOOP banks (reuses `ArpStartMode` enum).

```cpp
if (wkTypes[cursor] == BANK_NORMAL) {
    if (countType(wkTypes, BANK_ARPEG) < MAX_ARP_BANKS)
        wkTypes[cursor] = BANK_ARPEG;
    else if (countType(wkTypes, BANK_LOOP) < 2)
        wkTypes[cursor] = BANK_LOOP;
    else errorShown = true;
} else if (wkTypes[cursor] == BANK_ARPEG) {
    if (countType(wkTypes, BANK_LOOP) < 2)
        wkTypes[cursor] = BANK_LOOP;
    else wkTypes[cursor] = BANK_NORMAL;
} else { // BANK_LOOP
    wkTypes[cursor] = BANK_NORMAL;
}
```

---

## Shared Code Refactoring

### Shuffle LUTs -> shared header

```
Current:  src/arp/ArpEngine.cpp:17 (static const, inaccessible)
Move to:  src/core/ShuffleLUT.h   (extern const declaration)
          src/core/ShuffleLUT.cpp  (definition)
```

Both ArpEngine and LoopEngine include ShuffleLUT.h. Zero-risk refactor.

---

## Limits

| Constraint | Value | Reason |
|---|---|---|
| Max LOOP banks | 2 | Memory: 2 x 9.4 KB = 18.8 KB |
| Max events per loop | 1024 | 8 KB buffer (8 bytes/event) |
| Max overdub temp | 128 events | 1 KB, merge on close |
| Max pending noteOns | 16 | For shuffle/chaos scheduling |
| Max loop duration | 64 bars | Clamped at bar-snap |
| Min loop duration | 1 bar | Snap rounds up |
| Max total sequencer banks | 4 ARP + 2 LOOP | Independent pools |
| Loop content persistence | None | Ephemeral, lost on reboot |
| Effect params persistence | NVS per-bank | `LoopPotStore` in `illpad_lpot` (same pattern as `ArpPotStore`) |
| Static SRAM cost | 18.8 KB always allocated | Even with 0 LOOP banks configured |

### Compile-time guard (optional)

`#define ENABLE_LOOP_MODE 1` in HardwareConfig.h. When 0, `s_loopEngines[2]` is not
instantiated, saving 18.8 KB. Recommended for BLE-heavy configurations where SRAM
margins are tighter. Default: ON. Guard wraps: static LoopEngine array, processLoopMode(),
loop tick/processEvents, BANK_LOOP enum usage, ToolBankConfig 3-way cycle.

## What LOOP Does NOT Do

- No aftertouch
- No scale / root / mode (pads map to fixed GM percussion notes)
- No mute/unmute — use per-bank velocity (acts as volume) instead. Mute was removed because it's difficult to surface through the current hardware/LED system.
- No **recording** quantize (free timing, microsecond resolution) — playback start quantize IS supported (Immediate/Beat/Bar)
- No rest before first note — recording clock starts on first pad press, so beat 1 is always where the first hit lands. Patterns that enter mid-bar (e.g., pickup on "&" of 4) will shift left. This is a deliberate zero-friction choice (no count-in needed). Future enhancement: clock-synced record start using `_quantizeMode` for record-start alignment (Beat/Bar boundary).
- No "armed" state before recording — tapping play/stop on EMPTY immediately starts recording. This matches Boss RC looper behavior. Beginners may accidentally start recording; the red LED blink is the only cue.
- No NVS persistence of loop content (recorded events are ephemeral). Effect params (shuffle, chaos, vel pattern) DO persist via `LoopPotStore`.
- No pitch bend
- No octave range control
- No buffer-full feedback — when `_events[]` reaches 1024 entries, new events are silently dropped. Live play still sounds (MIDI sent immediately) but won't be captured. The buffer overflow guard prevents memory corruption.
- No background mute — must bank-switch to the target bank to control it. The single-foreground model is a hardware constraint (8 LEDs, 2 buttons, no screen).

## Known Limitations & Future Enhancements

### LED brightness tunability
All LOOP LED intensity constants (`LED_FG_LOOP_*`, `LED_BG_LOOP_*`) are compile-time
values in HardwareConfig.h. A future **Tool 7 (LED Config)** should expose these as
runtime-configurable parameters, allowing users to adjust foreground/background brightness
per bank type. This applies to NORMAL and ARPEG intensities too.

### Tempo pot in slave mode
When synced to external clock (USB/BLE), the tempo pot still shows a bargraph on turn but
has no musical effect (internal BPM is overridden by PLL). **TODO**: suppress bargraph
display when `ClockManager::isExternalSync()` returns true, or show external BPM as a
read-only indicator.

---

## Files Impacted

| File | Change | Scope |
|---|---|---|
| `core/KeyboardData.h` | `BANK_LOOP` enum, `loopEngine*` in BankSlot | Small |
| `core/HardwareConfig.h` | LOOP RGBW colors, LED intensity constants | Small |
| `core/KeyboardData.h` | `LoopPotStore` struct, `LoopPadStore` struct + validate, NVS defines | Small |
| `core/ShuffleLUT.h/.cpp` | **New** — extracted from ArpEngine.cpp | Small |
| `loop/LoopEngine.h/.cpp` | **New** — entire engine | Large |
| `arp/ArpEngine.cpp` | Remove local SHUFFLE_TEMPLATES, include ShuffleLUT.h | Small |
| `managers/PotRouter.h/.cpp` | 3 new PotTargets, LOOP context, `loadStoredPerBankLoop()`, PotMappingStore rewrite (no migration — fresh) | Medium |
| `managers/ScaleManager.cpp` | Guard root/mode/chromatic pads to skip BANK_LOOP (prevent misleading blinks + wasted NVS writes) | Small |
| `managers/NvsManager` | BankType value 2, `LoopPotStore` load/save (per-bank, `illpad_lpot`), `LoopPadStore` load (`illpad_lpad`), PotMappingStore rewrite (no migration — fresh), quantize for LOOP banks | Medium |
| `setup/ToolBankConfig` | 3-way type cycle, max 2 LOOP constraint | Medium |
| `setup/ToolPotMapping` | `_contextIdx` replaces `_contextNormal`, LOOP pool | Medium |
| `setup/ToolPadRoles` | 4th category LOOP (3 pads: rec, play/stop, clear), color grid update | Medium |
| `main.cpp` | Static LoopEngine[2], `handleLoopControls()`, processLoopMode(), tick/processEvents | Medium |
| `core/LedController.h` | 2 new ConfirmType values, forward-declare LoopEngine | Small |
| `core/LedController.cpp` | LOOP RGB display states, bargraph/blink colors | Medium |
| `managers/BankManager.cpp` | Recording lock + `flushLiveNotes()` in `switchToBank()` (line 117), needs `_transport` pointer, debug print ternary → 3-way (line 146) | Small |

### Files NOT impacted

| File | Why |
|---|---|
| `arp/ArpScheduler` | No rename. Loop has own tick/processEvents |
| `handlePlayStopPad()` | ARPEG play/stop is untouched. LOOP uses separate `handleLoopControls()` with 3 dedicated pads |
| `arp/ArpEngine.h` | Only .cpp changes (shuffle LUT extraction) |
| `core/CapacitiveKeyboard` | DO NOT MODIFY |
| `midi/ClockManager` | Add `float getSmoothedBPMFloat()` — Loop needs float precision for proportional scaling. Existing `uint16_t getSmoothedBPM()` truncates PLL float (e.g., 120.7 → 120). Arp can keep using the uint16_t version. | Small |
| `midi/MidiEngine` | Already supports noteOn/noteOff on any channel |
| `midi/ScaleResolver` | LOOP bypasses scale resolution entirely |

---

## Known Medium Issues (to resolve during implementation)

### M1. PotRouter `rebuildBindings()` — expand from 2 to 3 contexts

The context loop at `PotRouter.cpp:186` is hardcoded to 2:

```cpp
// CURRENT (PotRouter.cpp:186-188):
for (uint8_t ctx = 0; ctx < 2; ctx++) {
  BankType btype = (ctx == 0) ? BANK_NORMAL : BANK_ARPEG;
  const PotMapping* map = (ctx == 0) ? _mapping.normalMap : _mapping.arpegMap;
  // ... iterates 8 slots per context, builds _bindings[]
}
```

**5 edits required** (rest of PotRouter is context-agnostic — resolveBindings, seedCatchValues,
resetPerBankCatch, applyBinding all loop `_numBindings` and filter by BankType):

```cpp
// FIX 1: PotRouter.h:60 — add loopMap field
struct PotMappingStore {
  uint16_t   magic;
  uint8_t    version;         // 1 (fresh format, no migration)
  uint8_t    reserved;
  PotMapping normalMap[POT_MAPPING_SLOTS];
  PotMapping arpegMap[POT_MAPPING_SLOTS];
  PotMapping loopMap[POT_MAPPING_SLOTS];   // +16 bytes (8 × 2)
};

// FIX 2: PotRouter.cpp:21-47 — add LOOP defaults after ARPEG block
static const PotMappingStore DEFAULT_MAPPING = {
  POT_MAPPING_MAGIC, 1, 0,
  { /* normalMap[8] — unchanged */ },
  { /* arpegMap[8]  — unchanged */ },
  { // loopMap[8]:
    {TARGET_TEMPO_BPM, 0},        // R1 alone
    {TARGET_BASE_VELOCITY, 0},    // R2 alone
    {TARGET_VEL_PATTERN, 0},      // R3 alone
    {TARGET_VEL_PATTERN_DEPTH,0}, // R4 alone
    {TARGET_CHAOS, 0},            // R1 + hold
    {TARGET_SHUFFLE_DEPTH, 0},    // R2 + hold
    {TARGET_SHUFFLE_TEMPLATE, 0}, // R3 + hold
    {TARGET_VELOCITY_VARIATION,0} // R4 + hold
  }
};

// FIX 3: PotRouter.cpp:186-188 — expand context loop
static const struct { BankType type; const PotMapping* map; } CTX[] = {
  { BANK_NORMAL, _mapping.normalMap },
  { BANK_ARPEG,  _mapping.arpegMap  },
  { BANK_LOOP,   _mapping.loopMap   },
};
for (uint8_t ctx = 0; ctx < 3; ctx++) {
  BankType btype = CTX[ctx].type;
  const PotMapping* map = CTX[ctx].map;
  // ... rest unchanged
}

// FIX 4: PotRouter.cpp:577 — add LOOP targets to isPerBankTarget()
case TARGET_CHAOS:
case TARGET_VEL_PATTERN:
case TARGET_VEL_PATTERN_DEPTH:
    return true;

// FIX 5: main.cpp — add LOOP pot routing block (already in doc, see Pot Mapping section)
```

No changes needed to: `seedCatchValues()`, `resolveBindings()`, `applyBinding()`, `resetPerBankCatch()` — all iterate `_numBindings` and filter by `bind.bankType`, fully context-agnostic.

---

### M2. Overdub merge — replace O(n²) insertion sort with O(n+m) merge

Current design (doc lines 391-399) uses full-array insertion sort after appending overdub events.
Worst case on 1024 elements: ~83ms at 240MHz — audible MIDI gap during OVERDUBBING→PLAYING.

Both `_events[0.._eventCount-1]` and `_overdubBuf[0.._overdubCount-1]` are already sorted by
`offsetUs` (recording order = time order). Use a standard two-pointer merge:

```cpp
void LoopEngine::mergeOverdub() {
    // Both arrays are already sorted by offsetUs.
    // Merge into _overdubBuf as scratch (1 KB), then copy back.
    // Follows the sorted-insert pattern from ArpEngine::addPadPosition()
    // (ArpEngine.cpp:109-118) but at larger scale.

    uint16_t toMerge = _overdubCount;
    if (_eventCount + toMerge > MAX_LOOP_EVENTS) {
        toMerge = MAX_LOOP_EVENTS - _eventCount;
    }

    // Phase 1: copy base events to scratch (overdub buf is free after we read it)
    // Since overdub buf is only 128 entries (1 KB) and base is 8 KB,
    // we merge in-place from the END to avoid needing a full 8 KB scratch buffer.
    uint16_t totalCount = _eventCount + toMerge;
    int16_t i = _eventCount - 1;    // end of base
    int16_t j = toMerge - 1;        // end of overdub
    int16_t k = totalCount - 1;     // end of merged output

    // Merge backward: largest-first into the tail of _events[]
    while (i >= 0 && j >= 0) {
        if (_events[i].offsetUs >= _overdubBuf[j].offsetUs) {
            _events[k--] = _events[i--];
        } else {
            _events[k--] = _overdubBuf[j--];
        }
    }
    // Remaining overdub events (base events are already in place)
    while (j >= 0) {
        _events[k--] = _overdubBuf[j--];
    }

    _eventCount = totalCount;
    _overdubCount = 0;
}
```

**Backward merge trick**: since both arrays live in `_events[]` (base) and `_overdubBuf[]` (separate),
merging backward into the tail of `_events[]` is safe — base elements are read before being
overwritten. No temp buffer needed. O(n+m) = ~1152 iterations × 8-byte copy = **~40 µs**.
This is the same pattern as `std::inplace_merge` but without the library.

---

### M3. ToolBankConfig — 3-way type cycle + LOOP quantize + header

Current implementation at `ToolBankConfig.cpp` has binary NORMAL↔ARPEG assumptions in 8 locations.
Key changes grounded against actual code:

```cpp
// FIX 1: Type cycling (ToolBankConfig.cpp:154-171)
// CURRENT: binary toggle
//   NORMAL → ARPEG (if < 4), ARPEG → NORMAL
// CHANGE TO: 3-way cycle
if (wkTypes[cursor] == BANK_NORMAL) {
    if (countType(wkTypes, BANK_ARPEG) < MAX_ARP_BANKS)
        wkTypes[cursor] = BANK_ARPEG;
    else if (countType(wkTypes, BANK_LOOP) < MAX_LOOP_BANKS)
        wkTypes[cursor] = BANK_LOOP;
    else { errorShown = true; errorTime = millis(); }
} else if (wkTypes[cursor] == BANK_ARPEG) {
    if (countType(wkTypes, BANK_LOOP) < MAX_LOOP_BANKS)
        wkTypes[cursor] = BANK_LOOP;
    else
        wkTypes[cursor] = BANK_NORMAL;
} else { // BANK_LOOP
    wkTypes[cursor] = BANK_NORMAL;
}

// FIX 2: Quantize guard (ToolBankConfig.cpp:173)
// CURRENT: } else if (ev.type == NAV_DOWN && wkTypes[cursor] == BANK_ARPEG) {
// CHANGE TO:
} else if (ev.type == NAV_DOWN &&
           (wkTypes[cursor] == BANK_ARPEG || wkTypes[cursor] == BANK_LOOP)) {

// FIX 3: Header counter (ToolBankConfig.cpp:220-226)
// CURRENT: "%d/4 ARPEG" with single arpCount
// CHANGE TO:
uint8_t arpCount = countType(wkTypes, BANK_ARPEG);
uint8_t loopCount = countType(wkTypes, BANK_LOOP);
snprintf(headerRight, sizeof(headerRight), "%dA %dL", arpCount, loopCount);

// FIX 4: Defaults (ToolBankConfig.cpp:96-99)
// CURRENT: (i < 4) ? BANK_NORMAL : BANK_ARPEG
// CHANGE TO: keep same defaults (no LOOP by default — user adds via cycling)
for (uint8_t i = 0; i < NUM_BANKS; i++) {
  wkTypes[i] = (i < 4) ? BANK_NORMAL : BANK_ARPEG;  // unchanged
  wkQuantize[i] = ARP_START_IMMEDIATE;
}

// FIX 5: Rendering (ToolBankConfig.cpp:247-271)
// CURRENT: binary isArpeg ? CYAN "ARPEG" : "NORMAL"
// CHANGE TO:
const char* typeName;
const char* typeColor;
if (wkTypes[i] == BANK_ARPEG)     { typeName = "ARPEG";  typeColor = VT_CYAN; }
else if (wkTypes[i] == BANK_LOOP) { typeName = "LOOP";   typeColor = VT_MAGENTA; }
else                               { typeName = "NORMAL"; typeColor = ""; }

// FIX 6: Quantize display (ToolBankConfig.cpp:264)
// CURRENT: if (isArpeg) { ... show quantize ... }
// CHANGE TO:
if (wkTypes[i] == BANK_ARPEG || wkTypes[i] == BANK_LOOP) {

// FIX 7: Error message (ToolBankConfig.cpp:283)
// CURRENT: "Max 4 ARPEG banks!"
// CHANGE TO: "Max reached!" (generic — both ARPEG and LOOP limits hit)

// FIX 8: Description help text (ToolBankConfig.cpp:48-54)
// Add LOOP description: "LOOP enables drum looper (max 2)"
```

## Implementation Order

1. **ShuffleLUT extraction** — zero-risk refactor, unblocks LoopEngine
2. **KeyboardData.h + HardwareConfig.h** — enum, colors, constants
3. **LoopEngine core** — state machine + recording + playback (no effects)
4. **main.cpp wiring** — static instances + processLoopMode + tick/processEvents
5. **ToolBankConfig** — 3-way type cycle + constraint
6. **ScaleManager** — guard root/mode/chromatic pads to skip BANK_LOOP
7. **LedController** — LOOP RGB display patterns
8. **Effects** — shuffle, chaos, velocity patterns
9. **PotRouter + PotMappingStore v2** — new targets + NVS migration
10. **ToolPotMapping** — 3-context UI

Steps 1-7 = playable LOOP mode. Steps 8-10 = polish.

---

## Issues Resolved from Draft

| # | Draft Issue | Resolution |
|---|---|---|
| 1 | 480 PPQN vs 24 PPQN clock mismatch | Use `micros()` timestamps, not PPQN ticks |
| 2 | "NeoPixel RGB" — wrong description | Correct: SK6812 RGBW NeoPixel, red/magenta color family |
| 3 | Shuffle/chaos negative offsets break linear cursor | Two-phase PendingNoteOn queue (matches ArpEngine pattern) |
| 4 | ArpScheduler rename to EventScheduler | Keep ArpScheduler. LoopEngine has own tick/processEvents |
| 5 | `bool _contextNormal` can't handle 3 contexts | Replace with `uint8_t _contextIdx` |
| 6 | PotMappingStore layout change loses NVS data | No backward compat — rewrite from scratch, size mismatch triggers defaults |
| 7 | uint16_t tick overflow at 34 bars | Eliminated — uint32_t micros() has no practical limit |
