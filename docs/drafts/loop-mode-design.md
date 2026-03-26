# LOOP Mode — Design Document

**Status**: VALIDATED DRAFT — audited against codebase 2026-03-25
**Predecessor**: `loop-mode-design-draft.md` (7 critical issues found and resolved)

---

## Overview

A third bank type (alongside NORMAL and ARPEG) for percussion looping. Records pad events with free timing, plays them back in a loop synced to the clock. No pitch, no scale, no aftertouch.

## Key Decisions

- **Max 2 LOOP banks** (18.8 KB SRAM, ~6% of 320 KB)
- **Ephemeral** — loops lost on reboot, no NVS persistence
- **Background playback** — continues when switching banks (like arp)
- **Microsecond timestamps** — events stored as `micros()` offsets (NOT tick-based — see Timing Model)
- **Loop duration**: free recording, snapped to nearest bar on close
- **Live play during playback** — pads play live on top of the loop
- **Separate from ArpScheduler** — LoopEngine has own `tick()`/`processEvents()` (different playback model)
- **LED feedback**: WS2812 RGB NeoPixel — red/magenta color family (distinct from white=NORMAL, blue=ARPEG)

---

## Timing Model

### Why not 480 PPQN ticks?

The existing clock runs at **24 PPQN** (`ClockManager::getCurrentTick()`, `ArpScheduler.cpp:10-19`). Storing loop events at 480 PPQN would require:
- A 20x interpolation layer between ClockManager and LoopEngine
- Converting back to real-time for shuffle/chaos offsets
- uint16_t overflow at 34 bars (65535 / 1920)

### Solution: micros()-relative timestamps

Record events as `uint32_t offsetUs` from loop start. At playback, convert BPM to loop duration and scale proportionally:
- Unlimited recording resolution (~1us)
- No PPQN conversion needed
- Tempo changes scale the loop naturally
- uint32_t supports loops up to ~71 minutes

```cpp
// Recording: store offset from first event
uint32_t offsetUs = micros() - _recordStartUs;

// Playback: cursor position in microseconds
uint32_t loopDurationUs = (uint32_t)((float)_loopLengthBars * 4.0f * 60000000.0f / _bpm);
uint32_t positionUs = (micros() - _playStartUs) % loopDurationUs;
```

### Bar snap (at recording close)

```cpp
uint32_t barDurationUs = (uint32_t)(4.0f * 60000000.0f / _bpm);
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

### BankSlot (KeyboardData.h:130)

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
    float    _bpm;                   // BPM at recording time

    // --- Playback ---
    uint16_t _cursorIdx;             // Next event to check
    uint32_t _playStartUs;           // micros() when playback started
    uint32_t _lastPositionUs;        // Previous loop position (wrap detection)

    // --- Active notes (clean noteOff at wrap) ---
    uint8_t  _activeNotes[48];       // velocity per pad, 0 = off

    // --- State machine ---
    enum State : uint8_t { EMPTY, RECORDING, PLAYING, OVERDUBBING, STOPPED };
    State    _state;
    bool     _muted;

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
    void stopRecording(float currentBPM);  // bar-snap -> PLAYING
    void startOverdub();
    void stopOverdub();                     // merge -> PLAYING
    void play(const ClockManager& clock, float currentBPM);  // quantized start
    void stop(MidiTransport& transport);   // flush active notes
    void toggleMute();

    // Core playback (called every loop iteration)
    void tick(MidiTransport& transport, float currentBPM);
    void processEvents(MidiTransport& transport);

    // Recording input
    void recordNoteOn(uint8_t padIndex, uint8_t velocity);
    void recordNoteOff(uint8_t padIndex);

    // Context
    void setPadOrder(const uint8_t* padOrder);
    void setQuantizeMode(uint8_t mode);    // ArpStartMode enum

    // Queries
    State    getState() const;
    bool     isMuted() const;
    bool     isPlaying() const;     // PLAYING or OVERDUBBING
    uint16_t getEventCount() const;
    bool     consumeTickFlash();    // For LedController (fires at loop wrap)

    // DAW transport
    void onMidiStart(MidiTransport& transport);
    void onMidiStop(MidiTransport& transport);
};
```

Memory per engine: ~9.4 KB. Two engines = ~18.8 KB (5.9% of 320 KB SRAM).

---

## Playback Engine — Two-Phase Model

### Why the draft's cursor scan was broken

The draft used a linear forward scan: `while (events[cursor].tick <= currentTick)`. Shuffle and chaos apply **time offsets** including negative ones — meaning an event should fire *before* its stored position. The cursor would have already passed it.

The arp system solves this with `PendingEvent` (`ArpEngine.h:21-26`): events are scheduled with a `fireTimeUs`, and `processEvents()` fires them when time arrives. The loop engine uses the same pattern.

### Phase 1 — `tick()`: scan cursor, schedule pending noteOns

```cpp
void LoopEngine::tick(MidiTransport& transport, float currentBPM) {
    if (_state != PLAYING && _state != OVERDUBBING) return;

    uint32_t loopDurationUs = calcLoopDurationUs(currentBPM);
    uint32_t now = micros();
    uint32_t positionUs = (now - _playStartUs) % loopDurationUs;

    // Detect wrap (position jumped backward)
    if (positionUs < _lastPositionUs) {
        flushActiveNotes(transport);  // noteOff safety
        _cursorIdx = 0;
        _tickFlash = true;
    }
    _lastPositionUs = positionUs;

    if (_muted) return;  // cursor advances, no scheduling

    // Scan events within current position window
    while (_cursorIdx < _eventCount &&
           _events[_cursorIdx].offsetUs <= positionUs) {
        const LoopEvent& ev = _events[_cursorIdx];
        int32_t shuffleUs = calcShuffleOffsetUs(ev.offsetUs, loopDurationUs);
        int32_t chaosUs   = calcChaosOffsetUs(_cursorIdx);
        uint8_t vel       = applyVelocityPattern(ev.velocity, ev.offsetUs, loopDurationUs);

        if (ev.velocity > 0) {
            schedulePending(now + shuffleUs + chaosUs, ev.padIndex, vel);
        } else {
            uint8_t note = padToNote(ev.padIndex);
            if (_activeNotes[ev.padIndex] > 0) {
                transport.sendNoteOff(note, 0, _channel);
                _activeNotes[ev.padIndex] = 0;
            }
        }
        _cursorIdx++;
    }
}
```

### Phase 2 — `processEvents()`: fire pending noteOns

```cpp
void LoopEngine::processEvents(MidiTransport& transport) {
    uint32_t now = micros();
    for (uint8_t i = 0; i < MAX_PENDING; i++) {
        if (_pending[i].active && (now - _pending[i].fireTimeUs) < 0x80000000UL) {
            transport.sendNoteOn(_pending[i].note, _pending[i].velocity, _channel);
            _activeNotes[_pending[i].note - LOOP_NOTE_OFFSET] = _pending[i].velocity;
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

```
                [long press]
     +-------- EMPTY <--------------+
     |           |                   |
     |        [tap]                  |
     |           v                   |
     |      RECORDING               |
     |           |                   |
     |        [tap] (snap to bar)    |
     |           v                   |
+--> PLAYING <--------+             |
|       |             |             |
|    [tap]         [tap]            |
|       v             |             |
|   OVERDUBBING ------+             |
|                                   |
|    [double-tap from PLAYING       |
|     or OVERDUBBING]               |
|       v                           |
+-- STOPPED -------------------------+
       [tap] -> PLAYING
       [long press] -> EMPTY
```

### Play/Stop pad transitions

| State | Tap | Double-tap | Long press |
|---|---|---|---|
| EMPTY | RECORDING | -- | -- |
| RECORDING | PLAYING (bar-snap) | -- | -- |
| PLAYING | OVERDUBBING | STOPPED | -- |
| OVERDUBBING | PLAYING (merge) | STOPPED | -- |
| STOPPED | PLAYING | -- | EMPTY (clear) |

Reuses configurable `doubleTapMs` from settings (100-250ms, default 150ms).

### Playback start quantize

Reuses `ArpStartMode` enum (`HardwareConfig.h:201-208`). Per-bank, set in Tool 4 alongside arp quantize.

| Mode | Behavior |
|---|---|
| Immediate | `_playStartUs = micros()` — play now |
| Beat | Delay start to next beat (24 ticks = 1 quarter note) |
| Bar | Delay start to next bar (96 ticks = 4 quarter notes) |

Applies to: STOPPED -> PLAYING transition and MIDI Start (0xFA). Recording close (RECORDING -> PLAYING) always uses Immediate — the loop should start playing back what you just recorded without waiting.

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

### Hold pad = Mute toggle

Works in PLAYING, OVERDUBBING, and STOPPED states. Muted = cursor advances, pending events not scheduled, no MIDI sent. Unmute resumes at current position.

### Recording flow

**First recording (EMPTY -> RECORDING):**
1. `_waitingForFirstHit = true` — tick counter does NOT start on tap
2. First pad press: `_recordStartUs = micros()`, first event at offsetUs = 0
3. Subsequent noteOn/noteOff stored with `micros() - _recordStartUs`
4. Tap play/stop to close -> bar-snap -> sort by offsetUs -> PLAYING

**Overdub (PLAYING -> OVERDUBBING):**
1. Loop continues playing normally
2. New events into `_overdubBuf[]` with offsetUs relative to current loop position
3. Tap to close -> sorted merge into `_events[]`
4. If `_eventCount + _overdubCount > MAX_LOOP_EVENTS`: oldest overdub events dropped

### Overdub merge

```cpp
void LoopEngine::mergeOverdub() {
    uint16_t toMerge = _overdubCount;
    if (_eventCount + toMerge > MAX_LOOP_EVENTS) {
        toMerge = MAX_LOOP_EVENTS - _eventCount;
    }
    for (uint16_t i = 0; i < toMerge; i++) {
        _events[_eventCount++] = _overdubBuf[i];
    }
    // Insertion sort (small overdub count, already-sorted base)
    for (uint16_t i = 1; i < _eventCount; i++) {
        LoopEvent tmp = _events[i];
        int16_t j = i - 1;
        while (j >= 0 && _events[j].offsetUs > tmp.offsetUs) {
            _events[j + 1] = _events[j];
            j--;
        }
        _events[j + 1] = tmp;
    }
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

    uint32_t barDurationUs = calcLoopDurationUs(_bpm) / _loopLengthBars;
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

### New PotTargets (add to PotRouter.h:11)

```cpp
    TARGET_CHAOS,             // LOOP per-bank, 0.0-1.0
    TARGET_VEL_PATTERN,       // LOOP per-bank, 0-3 discrete
    TARGET_VEL_PATTERN_DEPTH, // LOOP per-bank, 0.0-1.0
```

Existing targets reused: `TARGET_TEMPO_BPM`, `TARGET_BASE_VELOCITY`, `TARGET_VELOCITY_VARIATION`, `TARGET_SHUFFLE_DEPTH`, `TARGET_SHUFFLE_TEMPLATE`.

### LOOP default mapping

| Pot | Alone | + hold left |
|---|---|---|
| Right 1 | Tempo | Chaos |
| Right 2 | Base Velocity | Shuffle Depth |
| Right 3 | Vel Pattern (4 discrete) | Shuffle Template (5 discrete) |
| Right 4 | Vel Pattern Depth | Velocity Variation |

8 params for 8 slots. Fits existing slot architecture.

### PotMappingStore migration (PotRouter.h:60)

```cpp
// Current (v1): normalMap[8] + arpegMap[8] = 36 bytes
// New (v2):     normalMap[8] + arpegMap[8] + loopMap[8] = 52 bytes
struct PotMappingStore {
    uint16_t   magic;
    uint8_t    version;        // bump 1 -> 2
    uint8_t    reserved;
    PotMapping normalMap[8];
    PotMapping arpegMap[8];
    PotMapping loopMap[8];     // <-- ADD
};
```

Migration: on load, if `version == 1`: copy normalMap + arpegMap, fill loopMap with defaults, write back as v2. Add to `PotRouter::loadMapping()`.

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
| Hold pad | Mute/unmute toggle |
| Play/stop pad | Rec/play/overdub/stop/clear |
| Bank pads (8) | Unchanged |

### ScaleManager impact (ScaleManager.cpp:191-210)

Currently guards hold/octave with `slot.type == BANK_ARPEG`. Add LOOP branch for hold pad mute:

```cpp
if (_holdPad < NUM_KEYS && (slot.type == BANK_ARPEG || slot.type == BANK_LOOP)) {
    bool pressed = keyIsPressed[_holdPad];
    bool wasPressed = _lastScaleKeys[_holdPad];
    if (pressed && !wasPressed) {
        if (slot.type == BANK_ARPEG && slot.arpEngine) {
            slot.arpEngine->setHold(!slot.arpEngine->isHoldOn());
        } else if (slot.type == BANK_LOOP && slot.loopEngine) {
            slot.loopEngine->toggleMute();
        }
    }
}
```

---

## LED Feedback — WS2812 RGB NeoPixel

LOOP uses **red/magenta** color family — visually distinct from white (NORMAL) and blue (ARPEG).

### New colors (add to HardwareConfig.h)

```cpp
// LOOP colors (red/magenta family)
const RGB COL_LOOP        = {255,   0,  60};  // LOOP foreground — hot magenta
const RGB COL_LOOP_DIM    = { 40,   0,  10};  // LOOP background
const RGB COL_LOOP_REC    = {255,   0,   0};  // Recording — pure red
const RGB COL_LOOP_MUTE   = {100,   0,  30};  // Muted indicator — dim magenta
```

### Intensity constants (add to HardwareConfig.h)

```cpp
// Foreground LOOP (modulate R channel)
const uint8_t LED_FG_LOOP_STOP_MIN     = 51;   // Stopped: sine 20%
const uint8_t LED_FG_LOOP_STOP_MAX     = 128;  // Stopped: sine 50%
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
| EMPTY | -- | Off | Dim magenta steady (10%) | Off (0%) |
| RECORDING | `COL_LOOP_REC` (red) | Fast blink 200ms | 0%/100% | -- (fg only) |
| PLAYING | `COL_LOOP` (magenta) | Sine pulse + wrap flash | 30%-90%, spike 100% | 8%-20%, spike 25% |
| OVERDUBBING | `COL_LOOP_REC` (red) | Fast pulse 150ms + sine | 50%/100% | -- (fg only) |
| STOPPED | `COL_LOOP` (magenta) | Slow sine | 20%-50% | 5%-16% |
| MUTED (any) | `COL_LOOP_MUTE` | Single dim flash every 2s | 10% base + flash | Same dimmer |

### New ConfirmType values (add to LedController.h:16)

```cpp
    CONFIRM_LOOP_REC     = 10,  // Recording started — red flash
    CONFIRM_LOOP_MUTE    = 11,  // Mute toggled
```

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
        } else { // EMPTY
            setPixelScaled(i, COL_LOOP_DIM, 26); // 10%
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
static LoopEngine s_loopEngines[2];  // Max 2 LOOP banks

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
 9.  processNormalMode() or processArpMode() or processLoopMode()
10.  ArpScheduler.tick()                    // unchanged
10b. ArpScheduler.processEvents()           // unchanged
10c. Loop engines: tick()                   // NEW
10d. Loop engines: processEvents()          // NEW
11.  MidiEngine.flush()
```

### processLoopMode() (inline in loop())

```cpp
if (slot.type == BANK_LOOP && slot.loopEngine) {
    LoopEngine* eng = slot.loopEngine;
    for (uint8_t p = 0; p < NUM_KEYS; p++) {
        if (isControlPad(p)) continue;
        bool pressed = keyIsPressed[p];
        bool wasPressed = lastKeys[p];
        if (pressed && !wasPressed) {
            uint8_t note = eng->padToNote(p);
            uint8_t vel = slot.baseVelocity; // +/- variation applied here
            s_midiEngine.noteOn(note, vel, slot.channel);
            if (eng->getState() == LoopEngine::OVERDUBBING) {
                eng->recordNoteOn(p, vel);
            }
        } else if (!pressed && wasPressed) {
            uint8_t note = eng->padToNote(p);
            s_midiEngine.noteOff(note, 0, slot.channel);
            if (eng->getState() == LoopEngine::OVERDUBBING) {
                eng->recordNoteOff(p);
            }
        }
    }
}
```

### DAW transport

| Event | LOOP behavior |
|---|---|
| MIDI Start (0xFA) | Reset cursor to 0, restart from bar 1 |
| MIDI Stop (0xFC) | Flush active noteOffs (immediate silence) |
| MIDI Continue (0xFB) | Resume at current position |

### ToolBankConfig (ToolBankConfig.cpp:156)

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
| Persistence | None | Ephemeral, lost on reboot |

## What LOOP Does NOT Do

- No aftertouch
- No scale / root / mode (pads map to fixed GM percussion notes)
- No **recording** quantize (free timing, microsecond resolution) — playback start quantize IS supported (Immediate/Beat/Bar)
- No NVS persistence of loop content
- No pitch bend
- No octave range control

---

## Files Impacted

| File | Change | Scope |
|---|---|---|
| `core/KeyboardData.h` | `BANK_LOOP` enum, `loopEngine*` in BankSlot | Small |
| `core/HardwareConfig.h` | LOOP RGB colors, LED intensity constants | Small |
| `core/ShuffleLUT.h/.cpp` | **New** — extracted from ArpEngine.cpp | Small |
| `loop/LoopEngine.h/.cpp` | **New** — entire engine | Large |
| `arp/ArpEngine.cpp` | Remove local SHUFFLE_TEMPLATES, include ShuffleLUT.h | Small |
| `managers/PotRouter.h/.cpp` | 3 new PotTargets, LOOP context, PotMappingStore v2 + migration | Medium |
| `managers/ScaleManager.cpp` | Hold pad: add BANK_LOOP branch for mute toggle | Small |
| `managers/NvsManager` | BankType value 2, PotMappingStore v2 migration, quantize for LOOP banks (already in `illpad_btype` qmode array) | Small |
| `setup/ToolBankConfig` | 3-way type cycle, max 2 LOOP constraint | Medium |
| `setup/ToolPotMapping` | `_contextIdx` replaces `_contextNormal`, LOOP pool | Medium |
| `main.cpp` | Static LoopEngine[2], processLoopMode(), tick/processEvents | Medium |
| `core/LedController.h` | 2 new ConfirmType values, forward-declare LoopEngine | Small |
| `core/LedController.cpp` | LOOP RGB display states, bargraph/blink colors | Medium |

### Files NOT impacted

| File | Why |
|---|---|
| `arp/ArpScheduler` | No rename. Loop has own tick/processEvents |
| `arp/ArpEngine.h` | Only .cpp changes (shuffle LUT extraction) |
| `core/CapacitiveKeyboard` | DO NOT MODIFY |
| `midi/ClockManager` | Loop uses `getSmoothedBPM()` + `micros()`, no changes |
| `midi/MidiEngine` | Already supports noteOn/noteOff on any channel |
| `midi/ScaleResolver` | LOOP bypasses scale resolution entirely |
| `managers/BankManager` | Bank switch is already type-agnostic |

---

## Implementation Order

1. **ShuffleLUT extraction** — zero-risk refactor, unblocks LoopEngine
2. **KeyboardData.h + HardwareConfig.h** — enum, colors, constants
3. **LoopEngine core** — state machine + recording + playback (no effects)
4. **main.cpp wiring** — static instances + processLoopMode + tick/processEvents
5. **ToolBankConfig** — 3-way type cycle + constraint
6. **ScaleManager** — hold pad mute branch
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
| 2 | "NeoPixel RGB" — wrong description | Correct: WS2812 RGB NeoPixel, red/magenta color family |
| 3 | Shuffle/chaos negative offsets break linear cursor | Two-phase PendingNoteOn queue (matches ArpEngine pattern) |
| 4 | ArpScheduler rename to EventScheduler | Keep ArpScheduler. LoopEngine has own tick/processEvents |
| 5 | `bool _contextNormal` can't handle 3 contexts | Replace with `uint8_t _contextIdx` |
| 6 | PotMappingStore layout change loses NVS data | v1->v2 migration in loadMapping() |
| 7 | uint16_t tick overflow at 34 bars | Eliminated — uint32_t micros() has no practical limit |
