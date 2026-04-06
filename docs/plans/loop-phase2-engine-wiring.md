# LOOP Mode — Phase 2: LoopEngine + Wiring

**Goal**: First MIDI sound from LOOP mode. Record pads, play them back in a loop, overdub, clear. No effects, no LED, no setup UI. Hardcoded test config.

**Prerequisite**: Phase 1 (skeleton + guards) applied and building clean.

---

## Overview

This phase creates two things:
1. **LoopEngine** — self-contained class: state machine, recording, proportional playback, bar-snap, overdub merge, refcount, pending events, quantized transitions
2. **main.cpp wiring** — static instances, `processLoopMode()`, `handleLoopControls()`, tick/processEvents in loop order

**This plan is the source of truth.** All method implementations are inline below. No external reference needed.

---

## Step 1 — Create LoopEngine files

### 1a. Create `src/loop/LoopEngine.h`

Class definition — mirrors ArpEngine patterns where applicable (refcount, pending queue, per-bank lifecycle).

**Public API**:

```cpp
// Config
void begin(uint8_t channel);
void clear(MidiTransport& transport);          // Hard flush — vides tout (refcount + pending + events + overdub)
void setPadOrder(const uint8_t* padOrder);
void setLoopQuantizeMode(uint8_t mode);         // LoopQuantMode: FREE/BEAT/BAR
void setChannel(uint8_t ch);

// State transitions — each may be quantized based on _quantizeMode
// FREE: executes synchronously (no snap)
// BEAT/BAR: sets _pendingAction, actual execution deferred until tick() reaches boundary
void startRecording();
void stopRecording(const bool* keyIsPressed, const uint8_t* padOrder, float currentBPM);
void startOverdub();
void stopOverdub(const bool* keyIsPressed, const uint8_t* padOrder, float currentBPM);
void play(float currentBPM);
void stop(MidiTransport& transport);            // PLAYING->STOPPED: soft flush (trailing pending ok)
void abortOverdub(MidiTransport& transport);    // OVERDUBBING->STOPPED: ALWAYS immediate, hard flush
void cancelOverdub();                           // OVERDUBBING->PLAYING: discard current overdub pass, keep the loop
void flushLiveNotes(MidiTransport& transport, uint8_t channel);

// Core playback (called every loop iteration, from main.cpp)
void tick(MidiTransport& transport, float currentBPM, uint32_t globalTick);
void processEvents(MidiTransport& transport);

// Recording input (called by processLoopMode when state == RECORDING || OVERDUBBING)
void recordNoteOn(uint8_t padIndex, uint8_t velocity);
void recordNoteOff(uint8_t padIndex);

// Refcount helpers (called by processLoopMode for live-play deduplication)
bool noteRefIncrement(uint8_t note);
bool noteRefDecrement(uint8_t note);

// Note mapping
uint8_t padToNote(uint8_t padIndex) const;

// Setters for params (stubs — filled in Phase 5)
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
bool     hasPendingAction() const;     // for LED feedback: waiting quantize visual
uint8_t  getLoopQuantizeMode() const;   // for LED feedback: FREE vs QUANTIZED color
uint16_t getEventCount() const;

// Tick flash flags — consumed once by LedController per frame.
// Three separate flags let the LED hierarchy render beat / bar / wrap distinctly.
// Detection source:
//   PLAYING or OVERDUBBING → derived from positionUs + _loopLengthBars
//   RECORDING              → derived from globalTick (no loop structure yet)
//   EMPTY / STOPPED        → never set
// In FREE quantize mode, during PLAYING/OVERDUBBING, NONE of these flags are
// set (the loop has no master-clock-aligned grid — solid render only).
// During RECORDING the flags ARE set in FREE mode (using globalTick).
bool     consumeBeatFlash();   // any beat crossing (including beat 1)
bool     consumeBarFlash();    // bar boundary (beat 1 of a new bar)
bool     consumeWrapFlash();   // loop wrap (end of cycle)

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

**PendingAction enum** (quantized transitions — set by public methods, executed in `tick()` when boundary reached):
```cpp
enum PendingAction : uint8_t {
    PENDING_NONE             = 0,
    PENDING_START_RECORDING  = 1,   // EMPTY → RECORDING
    PENDING_STOP_RECORDING   = 2,   // RECORDING → PLAYING (close + bar-snap + enchaîne playback)
    PENDING_START_OVERDUB    = 3,   // PLAYING → OVERDUBBING
    PENDING_STOP_OVERDUB     = 4,   // OVERDUBBING → PLAYING (close + merge)
    PENDING_PLAY             = 5,   // STOPPED → PLAYING
    PENDING_STOP             = 6    // PLAYING → STOPPED (playback continues until boundary, then soft flush)
};
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

**Private members — key buffers and state**:
- `LoopEvent _events[MAX_LOOP_EVENTS]` — 8 KB (`MAX_LOOP_EVENTS = 1024`)
- `LoopEvent _overdubBuf[MAX_OVERDUB_EVENTS]` — 1 KB (`MAX_OVERDUB_EVENTS = 128`)
- `uint8_t _noteRefCount[128]` — per MIDI note
- `PendingNote _pending[MAX_PENDING]` — **48 entries** (`MAX_PENDING = 48`) — handles both noteOn and noteOff events simultaneously. Sized generously: 48 × 8 bytes = 384 bytes per engine × `MAX_LOOP_BANKS`(2) = 768 bytes SRAM total. Budget philosophy: prefer safe over economical (see CLAUDE.md Budget Philosophy).
- `bool _overdubActivePads[48]` — tracks which pads had a `recordNoteOn` during the current overdub session. Used by `flushHeldPadsToOverdub()` to only inject noteOff for pads actually recorded during the overdub, not for pads held since before. Reset in `startOverdub()`.
- `PendingAction _pendingAction` — set by public methods when quantize is BEAT/BAR, executed in `tick()` at boundary
- `uint8_t _quantizeMode` — `LoopQuantMode` value, set by `setLoopQuantizeMode()` from per-bank config
- `uint16_t _loopLengthBars` — set in `stopRecording()` via bar-snap
- `uint32_t _recordStartUs, _playStartUs, _lastPositionUs` — timing anchors
- `float _recordBpm` — BPM latched at `stopRecording()` for proportional playback
- `uint16_t _eventCount, _overdubCount, _cursorIdx`
- `bool _beatFlash, _barFlash, _wrapFlash` — consumed once per frame by LedController (hierarchy: beat < bar < wrap)
- `uint32_t _lastBeatIdx` — last beat index detected in PLAYING/OVERDUBBING (from positionUs)
- `uint32_t _lastRecordBeatTick` — last globalTick modulo 24 checkpoint during RECORDING (for beat flash detection when no loop structure exists yet)

### 1b. Create `src/loop/LoopEngine.cpp`

Full implementation. All code inline below. No external reference needed.

#### Constants

```cpp
static const uint8_t  LOOP_NOTE_OFFSET     = 36;   // GM kick drum
static const uint16_t MAX_LOOP_EVENTS      = 1024; // Main event buffer
static const uint8_t  MAX_OVERDUB_EVENTS   = 128;  // Overdub buffer
static const uint8_t  MAX_PENDING          = 48;   // Pending note queue (shuffle/chaos scheduling)
static const uint16_t TICKS_PER_BEAT       = 24;   // MIDI PPQN standard
static const uint16_t TICKS_PER_BAR        = 96;   // 4/4 assumption
```

#### Helper: `padToNote` — unmapped pad guard

```cpp
// Returns 0xFF for unmapped pads (padOrder[i] == 0xFF).
// WITHOUT this guard: 0xFF + 36 = 35 (uint8_t overflow), wrong MIDI note.
uint8_t LoopEngine::padToNote(uint8_t padIndex) const {
    uint8_t order = _padOrder[padIndex];
    if (order == 0xFF) return 0xFF;
    return order + LOOP_NOTE_OFFSET;
}
```

#### Helpers: refcount

```cpp
// noteRefIncrement — returns true on 0→1 transition (caller sends MIDI noteOn)
bool LoopEngine::noteRefIncrement(uint8_t note) {
    if (note >= 128) return false;  // guard against 0xFF or any invalid note
    return (_noteRefCount[note]++ == 0);
}

// noteRefDecrement — returns true on 1→0 (caller sends MIDI noteOff)
// MUST guard refcount==0: flushActiveNotes(hard) zeros all refcounts,
// then a subsequent pad release would underflow to 255 without the guard.
bool LoopEngine::noteRefDecrement(uint8_t note) {
    if (note >= 128) return false;
    if (_noteRefCount[note] > 0) {
        return (--_noteRefCount[note] == 0);
    }
    return false;
}
```

#### Helper: `calcLoopDurationUs`

```cpp
// Duration in us for the current loop length at a given BPM.
// Guard clamps bpm to 10.0f minimum (matches pot range) — prevents
// both division by zero AND uint32_t overflow on long loops at very low BPM.
// At bpm=10, 64 bars = 1,536,000,000 us (~25 min) — fits uint32_t.
uint32_t LoopEngine::calcLoopDurationUs(float bpm) const {
    if (bpm < 10.0f) bpm = 10.0f;
    return (uint32_t)(_loopLengthBars * 4.0f * 60000000.0f / bpm);
}
```

#### Phase 2 effect stubs (filled in Phase 5)

```cpp
int32_t LoopEngine::calcShuffleOffsetUs(uint32_t, uint32_t) { return 0; }
int32_t LoopEngine::calcChaosOffsetUs(uint32_t) { return 0; }
uint8_t LoopEngine::applyVelocityPattern(uint8_t origVel, uint32_t, uint32_t) { return origVel; }
```

#### `begin`, `clear`, `setLoopQuantizeMode`

```cpp
void LoopEngine::begin(uint8_t channel) {
    _channel        = channel;
    _state          = EMPTY;
    _pendingAction  = PENDING_NONE;
    _quantizeMode   = LOOP_QUANT_FREE;
    _eventCount     = 0;
    _overdubCount   = 0;
    _cursorIdx      = 0;
    _loopLengthBars = 0;
    _recordStartUs  = 0;
    _playStartUs    = 0;
    _lastPositionUs = 0;
    _recordBpm      = 120.0f;
    _beatFlash      = false;
    _barFlash       = false;
    _wrapFlash      = false;
    _lastBeatIdx    = 0;
    _lastRecordBeatTick = 0xFFFFFFFF;
    _padOrder       = nullptr;
    memset(_noteRefCount, 0, sizeof(_noteRefCount));
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    for (uint8_t i = 0; i < MAX_PENDING; i++) _pending[i].active = false;
}

void LoopEngine::clear(MidiTransport& transport) {
    flushActiveNotes(transport, /*hard=*/true);  // noteOff + vide pending
    _state          = EMPTY;
    _pendingAction  = PENDING_NONE;
    _eventCount     = 0;
    _overdubCount   = 0;
    _cursorIdx      = 0;
    _loopLengthBars = 0;
    _lastPositionUs = 0;
    _beatFlash      = false;
    _barFlash       = false;
    _wrapFlash      = false;
    _lastBeatIdx    = 0;
    _lastRecordBeatTick = 0xFFFFFFFF;
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
}

void LoopEngine::setLoopQuantizeMode(uint8_t mode) {
    if (mode >= NUM_LOOP_QUANT_MODES) mode = DEFAULT_LOOP_QUANT_MODE;
    _quantizeMode = mode;
    // If mode drops to FREE while a pending action is waiting,
    // leave the pending intact — the boundary check in tick() will fire on the
    // very next tick (globalTick % 24 is cheap and harmless).
}
```

#### State transitions — public API with quantize gating

Each public transition checks `_quantizeMode`. If FREE, execute now (private `doXxx()`). If BEAT/BAR, set `_pendingAction` and return — `tick()` will dispatch when the boundary arrives.

```cpp
void LoopEngine::startRecording() {
    if (_state != EMPTY) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doStartRecording();
    } else {
        _pendingAction = PENDING_START_RECORDING;
    }
}

void LoopEngine::stopRecording(const bool* keyIsPressed, const uint8_t* padOrder, float currentBPM) {
    if (_state != RECORDING) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doStopRecording(keyIsPressed, padOrder, currentBPM);
    } else {
        // Stash the args we need later — kept in members because the pending
        // dispatcher has no access to the caller's stack state.
        _pendingKeyIsPressed = keyIsPressed;
        _pendingPadOrder     = padOrder;
        _pendingBpm          = currentBPM;
        _pendingAction       = PENDING_STOP_RECORDING;
    }
}

void LoopEngine::startOverdub() {
    if (_state != PLAYING) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doStartOverdub();
    } else {
        _pendingAction = PENDING_START_OVERDUB;
    }
}

void LoopEngine::stopOverdub(const bool* keyIsPressed, const uint8_t* padOrder, float currentBPM) {
    if (_state != OVERDUBBING) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doStopOverdub(keyIsPressed, padOrder, currentBPM);
    } else {
        _pendingKeyIsPressed = keyIsPressed;
        _pendingPadOrder     = padOrder;
        _pendingBpm          = currentBPM;
        _pendingAction       = PENDING_STOP_OVERDUB;
    }
}

void LoopEngine::play(float currentBPM) {
    if (_state != STOPPED) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doPlay(currentBPM);
    } else {
        _pendingBpm    = currentBPM;
        _pendingAction = PENDING_PLAY;
    }
}

void LoopEngine::stop(MidiTransport& transport) {
    if (_state != PLAYING) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doStop(transport);
    } else {
        _pendingAction = PENDING_STOP;
        // Playback continues until boundary — visible to LedController via hasPendingAction()
    }
}

// Abort overdub (PLAY/STOP pad during OVERDUBBING): ALWAYS immediate, hard flush.
// Quantize mode is ignored — abort means "stop now, throw away overdub".
void LoopEngine::abortOverdub(MidiTransport& transport) {
    if (_state != OVERDUBBING) return;
    _overdubCount = 0;
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    flushActiveNotes(transport, /*hard=*/true);
    _state         = STOPPED;
    _pendingAction = PENDING_NONE;
}

// Cancel overdub (CLEAR long-press during OVERDUBBING): discard only the
// events captured during the current overdub pass, keep the loop playing
// with its previous content. This is the "undo overdub" path — the user
// made a mistake during overdub and wants to try again without losing the
// underlying loop.
//
// ALWAYS immediate (the 500ms long-press IS the "human quantize"). No flush
// of active notes: the main loop keeps running via _events[] and the
// pending queue continues firing naturally. Any live pad the user is still
// holding will release normally via processLoopMode on the next frame.
void LoopEngine::cancelOverdub() {
    if (_state != OVERDUBBING) return;
    _overdubCount = 0;
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    _state         = PLAYING;
    _pendingAction = PENDING_NONE;
    // _playStartUs, _cursorIdx, _lastPositionUs untouched → loop continues
    // exactly where it was. No audible gap, no retrigger.
}
```

#### Private transition implementations (the real work)

```cpp
void LoopEngine::doStartRecording() {
    _eventCount    = 0;
    _recordStartUs = micros();
    _lastRecordBeatTick = 0xFFFFFFFF;  // force first beat flash at first tick
    _state         = RECORDING;
}

void LoopEngine::doStopRecording(const bool* keyIsPressed, const uint8_t* padOrder, float currentBPM) {
    // 1. Flush held pads (inject noteOff for pads still pressed) — but we haven't
    //    tracked "recorded pads" during first recording, so we inject noteOff for
    //    every pressed pad. The semantic is correct: any note held at close must
    //    end at close time (otherwise the note is orphan).
    uint32_t closeUs   = micros();
    uint32_t positionUs = closeUs - _recordStartUs;
    for (uint8_t i = 0; i < 48; i++) {
        if (keyIsPressed[i] && _eventCount < MAX_LOOP_EVENTS) {
            _events[_eventCount++] = { positionUs, i, 0, {0, 0} };
        }
    }

    // 2. Latch recording BPM for proportional playback
    _recordBpm = (currentBPM < 10.0f) ? 10.0f : currentBPM;

    // 3. Bar-snap: round recorded duration to nearest integer bar count
    uint32_t barDurationUs = (uint32_t)(4.0f * 60000000.0f / _recordBpm);
    uint32_t recordedDurationUs = closeUs - _recordStartUs;
    uint16_t bars = (recordedDurationUs + barDurationUs / 2) / barDurationUs;
    if (bars == 0) bars = 1;
    if (bars > 64) bars = 64;
    _loopLengthBars = bars;

    // 4. Normalize event offsets to [0, bars * barDurationUs) via proportional scale
    float scale = (float)(bars * barDurationUs) / (float)recordedDurationUs;
    for (uint16_t i = 0; i < _eventCount; i++) {
        _events[i].offsetUs = (uint32_t)((float)_events[i].offsetUs * scale);
    }

    // 5. Sort events by offsetUs (insertion sort — N < 128 typical, cheap enough)
    sortEvents(_events, _eventCount);

    // 6. Initialize playback state — we transition directly to PLAYING.
    //    No call to play() — we're already at the boundary (pending dispatcher
    //    guarantees it) or in FREE mode where no snap is needed.
    _playStartUs    = micros();
    _cursorIdx      = 0;
    _lastPositionUs = 0;
    _lastBeatIdx    = 0xFFFFFFFF;  // force beat flash at first beat of playback
    _state          = PLAYING;
}

void LoopEngine::doStartOverdub() {
    _overdubCount = 0;
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    _state        = OVERDUBBING;
}

void LoopEngine::doStopOverdub(const bool* keyIsPressed, const uint8_t* padOrder, float currentBPM) {
    (void)padOrder;  // unused — kept for signature symmetry with stopRecording
    (void)currentBPM;

    // 1. Flush held pads — but ONLY pads that had a recordNoteOn during this
    //    overdub session (tracked via _overdubActivePads). Pads held from before
    //    the overdub must NOT receive an injected noteOff (would be orphan).
    uint32_t closeUs = micros();
    // Compute current loop position in recording timebase
    uint32_t liveDurationUs = calcLoopDurationUs(currentBPM);
    uint32_t recordDurationUs = calcLoopDurationUs(_recordBpm);
    uint32_t elapsedUs = (closeUs - _playStartUs) % liveDurationUs;
    uint32_t positionUs = (uint32_t)((float)elapsedUs * (float)recordDurationUs / (float)liveDurationUs);

    for (uint8_t i = 0; i < 48; i++) {
        if (_overdubActivePads[i] && keyIsPressed[i] && _overdubCount < MAX_OVERDUB_EVENTS) {
            _overdubBuf[_overdubCount++] = { positionUs, i, 0, {0, 0} };
        }
    }
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));

    // 2. Sort overdub buffer by offsetUs
    sortEvents(_overdubBuf, _overdubCount);

    // 3. Reverse merge O(n+m) into _events[] — no auxiliary buffer
    mergeOverdub();

    // 4. Back to PLAYING — no change to _playStartUs (loop already running)
    _state = PLAYING;
}

void LoopEngine::doPlay(float currentBPM) {
    (void)currentBPM;  // BPM read live in tick() via parameter
    // Direct start. If we got here via pending dispatcher, we're already on a
    // boundary — cursor restarts from the top of the loop.
    _playStartUs    = micros();
    _cursorIdx      = 0;
    _lastPositionUs = 0;
    _lastBeatIdx    = 0xFFFFFFFF;  // force beat flash at first beat
    _state          = PLAYING;
}

void LoopEngine::doStop(MidiTransport& transport) {
    // Soft flush — noteOff the currently active notes, but let the pending
    // queue finish its scheduled events (trailing notes allowed).
    flushActiveNotes(transport, /*hard=*/false);
    _state = STOPPED;
}
```

#### `tick()` — pending action dispatcher + proportional playback

```cpp
void LoopEngine::tick(MidiTransport& transport, float currentBPM, uint32_t globalTick) {
    // ---- 1. Pending action dispatcher (quantize boundary check) ----
    if (_pendingAction != PENDING_NONE) {
        uint16_t boundary = (_quantizeMode == LOOP_QUANT_BAR) ? TICKS_PER_BAR : TICKS_PER_BEAT;
        if (globalTick % boundary == 0) {
            // Boundary reached — execute and clear
            switch (_pendingAction) {
                case PENDING_START_RECORDING:
                    doStartRecording();
                    break;
                case PENDING_STOP_RECORDING:
                    doStopRecording(_pendingKeyIsPressed, _pendingPadOrder, _pendingBpm);
                    break;
                case PENDING_START_OVERDUB:
                    doStartOverdub();
                    break;
                case PENDING_STOP_OVERDUB:
                    doStopOverdub(_pendingKeyIsPressed, _pendingPadOrder, _pendingBpm);
                    break;
                case PENDING_PLAY:
                    doPlay(_pendingBpm);
                    break;
                case PENDING_STOP:
                    doStop(transport);
                    break;
                default: break;
            }
            _pendingAction = PENDING_NONE;
        }
        // Fall through to playback logic — loop continues running while PENDING_STOP
        // waits for boundary, and PENDING_START_OVERDUB plays back normally while waiting.
    }

    // ---- 2. RECORDING tick flash (globalTick-based, no loop structure yet) ----
    // Runs BEFORE the playback logic gate so it fires while state == RECORDING.
    // Both FREE and QUANTIZED modes get tick flashes during recording — the
    // recording blink needs rhythmic feedback from the master clock.
    if (_state == RECORDING) {
        uint32_t currentBeatTick = globalTick / TICKS_PER_BEAT;  // beat index since boot
        if (currentBeatTick != _lastRecordBeatTick) {
            _beatFlash = true;
            // Bar = every 4th beat from an arbitrary reference (globalTick % 96 == 0)
            if ((globalTick % TICKS_PER_BAR) < TICKS_PER_BEAT) _barFlash = true;
            _lastRecordBeatTick = currentBeatTick;
        }
        return;  // RECORDING does not run playback logic
    }

    // ---- 3. Playback logic (only PLAYING and OVERDUBBING produce scheduled events) ----
    if (_state != PLAYING && _state != OVERDUBBING) return;

    uint32_t now = micros();

    uint32_t liveDurationUs   = calcLoopDurationUs(currentBPM);
    uint32_t recordDurationUs = calcLoopDurationUs(_recordBpm);
    if (liveDurationUs == 0 || recordDurationUs == 0) return;  // defense in depth

    uint32_t elapsedUs  = (now - _playStartUs) % liveDurationUs;
    uint32_t positionUs = (uint32_t)((float)elapsedUs * (float)recordDurationUs / (float)liveDurationUs);

    // Wrap detection: position jumped backward relative to last tick.
    // DO NOT flush active notes here — refcount + pending queue handle long notes
    // that cross the wrap naturally (see Budget Philosophy: overlaps allowed).
    bool wrapped = (positionUs < _lastPositionUs);
    if (wrapped) {
        _cursorIdx    = 0;
        _wrapFlash    = true;
        _lastBeatIdx  = 0xFFFFFFFF;  // reset so first beat of new cycle flashes
    }

    // ---- 4. PLAYING/OVERDUBBING tick flash (positionUs-based, only in QUANTIZED mode) ----
    // FREE mode: no tick flashes during playback (solid render, no internal
    // beat grid exposed). QUANTIZED modes: derive beat/bar from the loop's
    // own structure so FREE-recorded loops still pulse at their intrinsic rate
    // when switched to a QUANTIZED mode — but per user decision, FREE stays
    // visually solid during playback.
    if (_quantizeMode != LOOP_QUANT_FREE && _loopLengthBars > 0) {
        uint32_t barDurationUs  = recordDurationUs / _loopLengthBars;
        uint32_t beatDurationUs = barDurationUs / 4;
        if (beatDurationUs > 0) {
            uint32_t beatIdxNow = positionUs / beatDurationUs;
            if (beatIdxNow != _lastBeatIdx) {
                _beatFlash = true;
                // Bar = every 4th beat (beat 1 of a new bar)
                if ((beatIdxNow % 4) == 0 && !wrapped) _barFlash = true;
                // Note: wrap already sets _wrapFlash above; we don't set bar on wrap
                // because wrap is a strictly stronger event than bar (hierarchy).
                _lastBeatIdx = beatIdxNow;
            }
        }
    }

    _lastPositionUs = positionUs;

    // ---- 5. Cursor scan — both noteOn AND noteOff go through schedulePending ----
    while (_cursorIdx < _eventCount &&
           _events[_cursorIdx].offsetUs <= positionUs) {
        const LoopEvent& ev = _events[_cursorIdx];
        int32_t shuffleUs = calcShuffleOffsetUs(ev.offsetUs, recordDurationUs);
        int32_t chaosUs   = calcChaosOffsetUs(ev.offsetUs);

        uint8_t note = padToNote(ev.padIndex);
        if (note != 0xFF) {
            if (ev.velocity > 0) {
                uint8_t vel = applyVelocityPattern(ev.velocity, ev.offsetUs, recordDurationUs);
                schedulePending(now + shuffleUs + chaosUs, note, vel);
            } else {
                // noteOff: ALSO through pending with same shuffle/chaos offset
                schedulePending(now + shuffleUs + chaosUs, note, 0);
            }
        }
        _cursorIdx++;
    }
}
```

#### `processEvents()` — fire pending notes via refcount

```cpp
// Fire pending events whose fireTimeUs has arrived.
// noteOn: refcount increment — MIDI only on 0→1 transition
// noteOff: refcount decrement — MIDI only on 1→0 transition
// Mirrors ArpEngine.processEvents (src/arp/ArpEngine.cpp:443-447).
void LoopEngine::processEvents(MidiTransport& transport) {
    uint32_t now = micros();
    for (uint8_t i = 0; i < MAX_PENDING; i++) {
        if (!_pending[i].active) continue;
        if ((int32_t)(now - _pending[i].fireTimeUs) < 0) continue;  // not yet

        uint8_t note = _pending[i].note;
        if (note < 128) {
            if (_pending[i].velocity > 0) {
                // noteOn: only send MIDI on 0→1
                if (_noteRefCount[note] == 0) {
                    transport.sendNoteOn(_channel, note, _pending[i].velocity);
                }
                _noteRefCount[note]++;
            } else {
                // noteOff: only send MIDI on 1→0
                if (_noteRefCount[note] > 0) {
                    _noteRefCount[note]--;
                    if (_noteRefCount[note] == 0) {
                        transport.sendNoteOn(_channel, note, 0);
                    }
                }
            }
        }
        _pending[i].active = false;
    }
}
```

#### `schedulePending()` — queue a note for later firing

```cpp
// Store a pending note in the first free slot. Silent drop on overflow —
// MAX_PENDING = 48 is sized generously to make this practically unreachable.
// Under DEBUG_SERIAL, log a warning so overflow shows up in dev but costs
// nothing in release.
void LoopEngine::schedulePending(uint32_t fireTimeUs, uint8_t note, uint8_t velocity) {
    for (uint8_t i = 0; i < MAX_PENDING; i++) {
        if (!_pending[i].active) {
            _pending[i].fireTimeUs = fireTimeUs;
            _pending[i].note       = note;
            _pending[i].velocity   = velocity;
            _pending[i].active     = true;
            return;
        }
    }
    #if DEBUG_SERIAL
    Serial.printf("[LOOP] pending queue overflow on bank %u — note %u dropped\n",
                  _channel, note);
    #endif
}
```

#### `flushActiveNotes(hard)` — soft or hard flush

```cpp
// hard = true  → noteOff every active note AND empty the pending queue.
//                Used by clear(), abortOverdub(), flushLiveNotes()-equivalent paths.
// hard = false → noteOff every active note but LEAVE the pending queue running.
//                Used by doStop() — trailing shuffle/chaos events finish their course.
void LoopEngine::flushActiveNotes(MidiTransport& transport, bool hard) {
    for (uint8_t n = 0; n < 128; n++) {
        if (_noteRefCount[n] > 0) {
            transport.sendNoteOn(_channel, n, 0);  // noteOff
            _noteRefCount[n] = 0;
        }
    }
    if (hard) {
        for (uint8_t i = 0; i < MAX_PENDING; i++) _pending[i].active = false;
    }
}
```

#### `recordNoteOn` / `recordNoteOff` — capture input during RECORDING or OVERDUBBING

```cpp
void LoopEngine::recordNoteOn(uint8_t padIndex, uint8_t velocity) {
    if (padIndex >= 48) return;
    uint32_t offsetUs;
    if (_state == RECORDING) {
        if (_eventCount >= MAX_LOOP_EVENTS) return;
        offsetUs = micros() - _recordStartUs;
        _events[_eventCount++] = { offsetUs, padIndex, velocity, {0, 0} };
    } else if (_state == OVERDUBBING) {
        if (_overdubCount >= MAX_OVERDUB_EVENTS) return;
        // Compute current loop position (recording timebase)
        uint32_t now = micros();
        uint32_t liveDurationUs = calcLoopDurationUs(_recordBpm);  // same timebase as events
        uint32_t elapsedUs  = (now - _playStartUs) % liveDurationUs;
        offsetUs = elapsedUs;  // loop already normalized to recording timebase
        _overdubBuf[_overdubCount++] = { offsetUs, padIndex, velocity, {0, 0} };
        _overdubActivePads[padIndex] = true;
    }
}

void LoopEngine::recordNoteOff(uint8_t padIndex) {
    if (padIndex >= 48) return;
    uint32_t offsetUs;
    if (_state == RECORDING) {
        if (_eventCount >= MAX_LOOP_EVENTS) return;
        offsetUs = micros() - _recordStartUs;
        _events[_eventCount++] = { offsetUs, padIndex, 0, {0, 0} };
    } else if (_state == OVERDUBBING) {
        if (_overdubCount >= MAX_OVERDUB_EVENTS) return;
        uint32_t now = micros();
        uint32_t liveDurationUs = calcLoopDurationUs(_recordBpm);
        uint32_t elapsedUs  = (now - _playStartUs) % liveDurationUs;
        offsetUs = elapsedUs;
        _overdubBuf[_overdubCount++] = { offsetUs, padIndex, 0, {0, 0} };
        // NOTE: We do NOT clear _overdubActivePads[padIndex] here. The bitmask
        // tracks "was this pad recordNoteOn'd during this overdub session", used
        // solely by flushHeldPadsToOverdub at close time to decide which held
        // pads deserve an injected noteOff. Once the user has released the pad
        // naturally, the bitmask entry doesn't matter — it's only read for pads
        // still held at close.
    }
}
```

#### `sortEvents` — insertion sort, in place

```cpp
// Insertion sort by offsetUs. N is small (< MAX_OVERDUB_EVENTS for overdub buf,
// typically <200 events for main buf). Stable, no allocation.
void LoopEngine::sortEvents(LoopEvent* buf, uint16_t count) {
    for (uint16_t i = 1; i < count; i++) {
        LoopEvent key = buf[i];
        int16_t j = (int16_t)i - 1;
        while (j >= 0 && buf[j].offsetUs > key.offsetUs) {
            buf[j + 1] = buf[j];
            j--;
        }
        buf[j + 1] = key;
    }
}
```

#### `mergeOverdub` — reverse merge O(n+m), in place in `_events[]`

```cpp
// Merge sorted _overdubBuf[] into sorted _events[] in place.
// Algorithm: reverse merge from the end. Assumes _events has capacity for
// _eventCount + _overdubCount. Both buffers must already be sorted by offsetUs.
void LoopEngine::mergeOverdub() {
    if (_overdubCount == 0) return;
    uint16_t totalCount = _eventCount + _overdubCount;
    if (totalCount > MAX_LOOP_EVENTS) {
        // Overflow guard — drop overdub events that won't fit (rare edge case
        // with very long overdubs on an already dense loop). Defensive; the
        // sort below still works on the truncated count.
        totalCount = MAX_LOOP_EVENTS;
        _overdubCount = totalCount - _eventCount;
    }

    int32_t i = (int32_t)_eventCount - 1;     // tail of main buffer
    int32_t j = (int32_t)_overdubCount - 1;   // tail of overdub buffer
    int32_t k = (int32_t)totalCount - 1;      // write position

    while (j >= 0) {
        if (i >= 0 && _events[i].offsetUs > _overdubBuf[j].offsetUs) {
            _events[k--] = _events[i--];
        } else {
            _events[k--] = _overdubBuf[j--];
        }
    }
    // Remaining _events[i..] already in place.

    _eventCount   = totalCount;
    _overdubCount = 0;
}
```

#### `flushLiveNotes` — bank switch cleanup

```cpp
// Called by BankManager on bank switch, outgoing bank.
// Sends CC123 (All Notes Off) on this engine's channel and zeroes refcounts.
// Does NOT touch the pending queue — the engine keeps running in background
// and its pending events will fire on the correct channel (still _channel).
void LoopEngine::flushLiveNotes(MidiTransport& transport, uint8_t channel) {
    (void)channel;  // signature kept for symmetry; we always use our own _channel
    transport.sendAllNotesOff(_channel);
    memset(_noteRefCount, 0, sizeof(_noteRefCount));
    // Pending queue NOT cleared — trailing events will fire normally on _channel.
    // New live presses on the incoming bank (different channel) won't collide
    // because refcount is per-engine and only manages this engine's channel.
}
```

#### Getters

```cpp
LoopEngine::State LoopEngine::getState() const        { return _state; }
bool     LoopEngine::isPlaying() const                { return _state == PLAYING; }
bool     LoopEngine::isRecording() const              { return _state == RECORDING || _state == OVERDUBBING; }
bool     LoopEngine::hasPendingAction() const         { return _pendingAction != PENDING_NONE; }
uint8_t  LoopEngine::getLoopQuantizeMode() const      { return _quantizeMode; }
uint16_t LoopEngine::getEventCount() const            { return _eventCount; }

// Consume-on-read pattern: flag is cleared as it is returned.
// Each flag is consumed independently by LedController each frame, so if
// multiple flags are set (e.g. beat + bar + wrap at the same sample), they
// all get delivered. LedController then picks the hierarchy winner
// (wrap > bar > beat) for the render.
bool LoopEngine::consumeBeatFlash() {
    bool tmp = _beatFlash;
    _beatFlash = false;
    return tmp;
}
bool LoopEngine::consumeBarFlash() {
    bool tmp = _barFlash;
    _barFlash = false;
    return tmp;
}
bool LoopEngine::consumeWrapFlash() {
    bool tmp = _wrapFlash;
    _wrapFlash = false;
    return tmp;
}
```

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

> **DESIGN REF (Slot Drive prereq)**: see `docs/superpowers/specs/2026-04-06-loop-slot-drive-design.md` §2.4. The `s_loopSlotPads[LOOP_SLOT_COUNT]` array is declared here in Phase 2 even though it is unused until Phase 6. Phase 1 Step 7c-5 documents this cross-phase dependency. Initialised to 0xFF in setup() (Step 4 below).

```cpp
// LOOP control pads (from LoopPadStore NVS or test config)
static uint8_t  s_recPad             = 0xFF;
static uint8_t  s_loopPlayPad        = 0xFF;
static uint8_t  s_clearPad           = 0xFF;
static uint8_t  s_loopSlotPads[LOOP_SLOT_COUNT];  // Phase 6 — initialised 0xFF below
static bool     s_lastRecState       = false;
static bool     s_lastLoopPlayState  = false;
static bool     s_lastClearState     = false;
static uint32_t s_clearPressStart    = 0;
static bool     s_clearFired         = false;
static const uint32_t CLEAR_LONG_PRESS_MS = 500;
```

Add the corresponding `memset` in setup(), in the Phase 2 Step 4 wiring section, right after the `BankSlot` init loop:

```cpp
// LOOP slot pads default unassigned (Phase 6 will populate from LoopPadStore)
memset(s_loopSlotPads, 0xFF, sizeof(s_loopSlotPads));
```

---

## Step 4 — main.cpp: setup() LoopEngine assignment

### 4a. Assign LoopEngines to LOOP banks (after ArpEngine assignment, line ~367)

```cpp
// --- Assign LoopEngines to LOOP banks ---
// Reads BankTypeStore.loopQuantize[] (loaded by NvsManager::loadAll()) and
// pushes it into each engine. If loadAll() was never called or the store is
// fresh, loopQuantize[] is all zeros = LOOP_QUANT_FREE (default).
uint8_t loopIdx = 0;
for (uint8_t i = 0; i < NUM_BANKS && loopIdx < MAX_LOOP_BANKS; i++) {
    if (s_banks[i].type == BANK_LOOP) {
        s_loopEngines[loopIdx].begin(i);
        s_loopEngines[loopIdx].setPadOrder(s_padOrder);
        s_loopEngines[loopIdx].setLoopQuantizeMode(s_bankTypeStore.loopQuantize[i]);
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
      s_loopEngines[0].setLoopQuantizeMode(LOOP_QUANT_FREE);  // test = no quantize
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

Pad input dispatch for LOOP banks. Handles live play via refcount (so live press
does not retrigger a loop note that is already sounding) and routes presses
into the engine's record buffer when state is RECORDING or OVERDUBBING.

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

**State transition table** (all transitions respect the per-bank `loopQuantize` setting
— except **abort** and **clear** which are always immediate):

| Pad | Current State | Action | New State | Quantizable |
|---|---|---|---|---|
| REC | EMPTY | startRecording() | RECORDING | YES |
| REC | RECORDING | stopRecording() + bar-snap | PLAYING | YES |
| REC | PLAYING | startOverdub() | OVERDUBBING | YES |
| REC | OVERDUBBING | stopOverdub() + merge | PLAYING | YES |
| REC | STOPPED | *(ignored)* | STOPPED | — |
| P/S | PLAYING | stop() + soft flush | STOPPED | YES |
| P/S | OVERDUBBING | abortOverdub() + hard flush *(discard overdub)* | STOPPED | **NO (abort = immediate)** |
| P/S | STOPPED | play() | PLAYING | YES |
| P/S | EMPTY/RECORDING | *(ignored)* | unchanged | — |
| CLR | OVERDUBBING | cancelOverdub() *(500ms hold, discard only current overdub pass)* | PLAYING | **NO (hold = human quantize)** |
| CLR | any other except EMPTY | clear() *(500ms hold + hard flush)* | EMPTY | **NO (destructive)** |

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
    // No CONFIRM_PLAY/CONFIRM_STOP triggered for LOOP — the new LED rendering
    // already gives distinct feedback:
    //   - In QUANTIZED mode, the waiting quantize blink (hasPendingAction)
    //     confirms the tap immediately.
    //   - In FREE mode, the instant color change (stopped→playing or
    //     playing→stopped) is the visible feedback.
    //   - Abort during OVERDUBBING is clearly visible (magenta→dim transition).
    // Removing these confirms declutters the LED for LOOP without losing info.
    if (s_loopPlayPad < NUM_KEYS) {
        bool pressed = state.keyIsPressed[s_loopPlayPad];
        if (pressed && !s_lastLoopPlayState) {
            switch (ls) {
                case LoopEngine::PLAYING:
                    // Quantizable soft stop — playback continues until boundary,
                    // trailing pending events finish naturally.
                    eng->stop(s_transport);
                    break;
                case LoopEngine::OVERDUBBING:
                    // Abort — ALWAYS immediate, hard flush, discard overdub.
                    eng->abortOverdub(s_transport);
                    break;
                case LoopEngine::STOPPED:
                    // Quantizable play — snap to next boundary per loopQuantize mode.
                    eng->play(s_clockManager.getSmoothedBPMFloat());
                    break;
                default: break;  // EMPTY, RECORDING: ignored
            }
        }
        s_lastLoopPlayState = pressed;
    }

    // --- CLEAR pad: long press (500ms) + LED ramp ---
    //
    // ALWAYS immediate (no quantize snap — the 500ms hold IS the "human quantize").
    //
    // Behavior depends on state AT RAMP COMPLETION:
    //   OVERDUBBING → cancelOverdub() — discards ONLY the current overdub
    //                                   pass, loop keeps playing with its
    //                                   previous content. "Undo overdub."
    //   anything else (except EMPTY) → clear() — hard flush, state → EMPTY.
    //
    // IMPORTANT edge case: when ls == EMPTY we still update s_lastClearState
    // below so the rising-edge detection stays coherent across state changes.
    // Without this, a held CLEAR pad during EMPTY→RECORDING transition would
    // produce a false rising edge and start the clear timer mid-recording.
    bool clearPressed = (s_clearPad < NUM_KEYS) ? state.keyIsPressed[s_clearPad] : false;
    if (s_clearPad < NUM_KEYS && ls != LoopEngine::EMPTY) {
        if (clearPressed && !s_lastClearState) {
            s_clearPressStart = now;
            s_clearFired = false;
        }
        if (clearPressed && !s_clearFired) {
            uint32_t held = now - s_clearPressStart;
            if (held < CLEAR_LONG_PRESS_MS) {
                uint8_t ramp = (uint8_t)((uint32_t)held * 100 / CLEAR_LONG_PRESS_MS);
                s_leds.showClearRamp(ramp);
            } else {
                // Ramp complete — dispatch based on current state
                if (ls == LoopEngine::OVERDUBBING) {
                    // Undo overdub pass, keep loop. No confirm needed:
                    // the color transition magenta→base is instant and visible.
                    eng->cancelOverdub();
                } else {
                    // Hard flush, state → EMPTY. Keep CONFIRM_STOP here: the
                    // transition from a colored state to dim EMPTY is subtle,
                    // and clear is destructive enough to warrant a distinct flash.
                    eng->clear(s_transport);
                    s_leds.triggerConfirm(CONFIRM_STOP);
                }
                s_clearFired = true;
            }
        }
    }
    // Always update edge state — even when ls == EMPTY — to avoid false rising
    // edges when state transitions while the pad is held.
    s_lastClearState = clearPressed;
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

  // LOOP engines: tick + processEvents (all banks, not just foreground).
  // tick() receives globalTick (from ClockManager) so the pending action
  // dispatcher can check beat/bar boundaries for quantized transitions.
  uint32_t globalTick = s_clockManager.getCurrentTick();
  float    smoothedBpm = s_clockManager.getSmoothedBPMFloat();
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (s_banks[i].type == BANK_LOOP && s_banks[i].loopEngine) {
          s_banks[i].loopEngine->tick(s_transport, smoothedBpm, globalTick);
          s_banks[i].loopEngine->processEvents(s_transport);
      }
  }

  s_midiEngine.flush();                           // line 976 (CRITICAL PATH END)
```

**Why loop all banks?** Background LOOP banks continue playing. Unlike ArpScheduler which manages all engines internally, LoopEngine tick/processEvents are called directly per bank.

**Why per-bank tick() and not a scheduler?** LOOP playback is microsecond-based (proportional position via `micros()`), not tick-based. There is no per-engine tick accumulator to dispatch. `globalTick` is passed only for the quantize boundary check, not for step scheduling.

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

## Audit History

This plan incorporates the resolution of audit findings from 2026-04-05 (5 parallel agents vs early draft) and 2026-04-06 (3 parallel agents, deep audit of quantize behavior and noteOff pending). The full inline implementations above already contain:

- `sendNoteOn(channel, note, velocity)` with correct parameter order (never `sendNoteOff`, which does not exist in `MidiTransport`)
- `padToNote()` guard against unmapped pads returning `0xFF` (prevents `0xFF + 36 = 35` silent corruption)
- `noteRefDecrement()` guard against underflow (required because `flushActiveNotes(hard=true)` zeros all refcounts)
- `recordNoteOn/Off` called in **both** `RECORDING` **and** `OVERDUBBING` states (first recording captures events too)
- `calcLoopDurationUs()` clamps BPM at 10.0f minimum (prevents uint32_t overflow on long loops at very low BPM)
- `handleLeftReleaseCleanup()` LOOP branch (prevents stuck notes when left button releases after a pad release mid-hold)
- `BankManager::switchToBank()` LOOP recording lock + `flushLiveNotes` (Phase 1 stubs replaced with the real guards in Step 10 below)
- `handleLoopControls()` CLEAR edge-state update moved out of the `ls != EMPTY` guard (prevents false rising edge causing accidental clear on state transitions)
- **noteOn AND noteOff both routed through `schedulePending`** (Design #1 redesign — shuffle/chaos offsets applied to both, preserving gate length on sustained notes)
- **`processEvents()` handles both** via refcount increment (noteOn) and decrement (noteOff)
- **Six quantized transitions** + two always-immediate (abort, clear), driven by the per-bank `LoopQuantMode` field
- **Pending action dispatcher** in `tick()` — waits for `globalTick % boundary == 0` before executing the deferred transition
- **`flushActiveNotes(hard)` with soft mode** — refcount flush without clearing pending queue, so trailing shuffle/chaos events finish their course when the user stops the loop. At wrap, `flushActiveNotes` is **NOT** called — refcount naturally handles notes that cross the wrap boundary (overlaps allowed by design, per Budget Philosophy).
- **`_overdubActivePads[48]` bitmask** — ensures `stopOverdub()` only injects noteOff for pads that actually received a `recordNoteOn` during the current overdub session (prevents orphan noteOffs for pads held from before the overdub).
- **`MAX_PENDING = 48`** (was 16) — sized generously per the "prefer safe over economical" Budget Philosophy now in CLAUDE.md.
- **Decoupling from `docs/drafts/loop-mode-design.md`** — this plan is now fully self-contained. The draft is kept for historical reference only.
