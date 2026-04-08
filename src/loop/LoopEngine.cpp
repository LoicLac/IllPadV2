#include "LoopEngine.h"
#include "../core/MidiTransport.h"
#include <Arduino.h>

// =================================================================
// Debug helpers (compiled out when DEBUG_SERIAL == 0)
// =================================================================
#if DEBUG_SERIAL
static const char* LOOP_STATE_NAMES[] = {
    "EMPTY", "REC", "PLAY", "OVD", "STOP"
};
static const char* LOOP_PENDING_NAMES[] = {
    "NONE", "START_REC", "STOP_REC", "START_OVD",
    "STOP_OVD", "PLAY", "STOP"
};
#endif

// =================================================================
// Constructor — defensive init, begin() does the full wiring
// =================================================================
// All scalar members have default initializers in the header (C++11).
// The constructor only handles the arrays that cannot be default-init'd
// inline, guaranteeing a sane state even if begin() is never called.
LoopEngine::LoopEngine() {
    memset(_noteRefCount, 0, sizeof(_noteRefCount));
    memset(_liveNote, 0xFF, sizeof(_liveNote));
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    for (uint8_t i = 0; i < MAX_PENDING; i++) _pending[i].active = false;
}

// =================================================================
// Config
// =================================================================

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
    _lastRecordBeatTick       = 0xFFFFFFFF;
    _lastDispatchedGlobalTick = 0xFFFFFFFF;   // B1 pass 2: sentinel "never dispatched"
    _pendingKeyIsPressed = nullptr;           // D-PLAN-1: stash members init
    _pendingPadOrder     = nullptr;
    _pendingBpm          = 120.0f;
    _padOrder       = nullptr;
    memset(_noteRefCount, 0, sizeof(_noteRefCount));
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    memset(_liveNote, 0xFF, sizeof(_liveNote));   // B1: no live notes at init
    for (uint8_t i = 0; i < MAX_PENDING; i++) _pending[i].active = false;
}

void LoopEngine::clear(MidiTransport& transport) {
    #if DEBUG_SERIAL
    State oldState = _state;
    uint16_t discardedEvents  = _eventCount;
    uint16_t discardedOverdub = _overdubCount;
    #endif
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
    memset(_liveNote, 0xFF, sizeof(_liveNote));   // B1: any outstanding live notes are now gone
    #if DEBUG_SERIAL
    Serial.printf("[LOOP] ch%u CLEAR from %s: %u events + %u overdub discarded -> EMPTY\n",
                  _channel + 1,
                  (oldState < 5) ? LOOP_STATE_NAMES[oldState] : "?",
                  (unsigned)discardedEvents, (unsigned)discardedOverdub);
    #endif
}

void LoopEngine::setPadOrder(const uint8_t* padOrder) {
    _padOrder = padOrder;
}

void LoopEngine::setLoopQuantizeMode(uint8_t mode) {
    if (mode >= NUM_LOOP_QUANT_MODES) mode = DEFAULT_LOOP_QUANT_MODE;
    _quantizeMode = mode;
    // If mode drops to FREE while a pending action is waiting, leave the pending
    // intact — the boundary check in tick() will fire on the very next tick
    // (globalTick / boundary crossing is cheap and harmless).
}

void LoopEngine::setChannel(uint8_t ch) {
    _channel = ch;
}

// =================================================================
// padToNote — unmapped pad guard
// =================================================================
// WITHOUT this guard: 0xFF + 36 = 35 (uint8_t overflow), wrong MIDI note.
uint8_t LoopEngine::padToNote(uint8_t padIndex) const {
    if (_padOrder == nullptr || padIndex >= NUM_KEYS) return 0xFF;
    uint8_t order = _padOrder[padIndex];
    if (order == 0xFF) return 0xFF;
    return order + LOOP_NOTE_OFFSET;
}

// =================================================================
// Refcount helpers
// =================================================================

// noteRefIncrement — returns true on 0→1 transition (caller sends MIDI noteOn)
bool LoopEngine::noteRefIncrement(uint8_t note) {
    if (note >= 128) return false;   // guard against 0xFF or any invalid note
    return (_noteRefCount[note]++ == 0);
}

// noteRefDecrement — returns true on 1→0 (caller sends MIDI noteOff).
// MUST guard refcount==0: flushActiveNotes(hard) zeros all refcounts,
// then a subsequent pad release would underflow to 255 without the guard.
bool LoopEngine::noteRefDecrement(uint8_t note) {
    if (note >= 128) return false;
    if (_noteRefCount[note] > 0) {
        return (--_noteRefCount[note] == 0);
    }
    return false;
}

// =================================================================
// calcLoopDurationUs
// =================================================================
// Duration in us for the current loop length at a given BPM.
// Clamps bpm to 10.0f minimum (matches pot range) — prevents both
// division by zero AND uint32_t overflow on long loops at very low BPM.
// At bpm=10, 64 bars = 1,536,000,000 us (~25 min) — fits uint32_t.
uint32_t LoopEngine::calcLoopDurationUs(float bpm) const {
    if (bpm < 10.0f) bpm = 10.0f;
    return (uint32_t)(_loopLengthBars * 4.0f * 60000000.0f / bpm);
}

// =================================================================
// Phase 5 effect stubs — filled in Phase 5
// =================================================================

int32_t LoopEngine::calcShuffleOffsetUs(uint32_t, uint32_t) { return 0; }
int32_t LoopEngine::calcChaosOffsetUs(uint32_t)             { return 0; }
uint8_t LoopEngine::applyVelocityPattern(uint8_t origVel, uint32_t, uint32_t) { return origVel; }

// =================================================================
// State transitions — public API with quantize gating
// =================================================================
// Each public transition checks _quantizeMode. If FREE, execute now
// (private doXxx()). If BEAT/BAR, set _pendingAction and return —
// tick() will dispatch when the boundary crosses.

void LoopEngine::startRecording() {
    if (_state != EMPTY) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doStartRecording();
    } else {
        _pendingAction = PENDING_START_RECORDING;
    }
}

void LoopEngine::stopRecording(const uint8_t* keyIsPressed, const uint8_t* padOrder, float currentBPM) {
    if (_state != RECORDING) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doStopRecording(keyIsPressed, padOrder, currentBPM);
    } else {
        // Stash the args we need later — the dispatcher has no access to the
        // caller's stack state when the boundary finally arrives.
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

void LoopEngine::stopOverdub(const uint8_t* keyIsPressed, const uint8_t* padOrder, float currentBPM) {
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
        // Playback continues until boundary — visible via hasPendingAction().
    }
}

// Abort overdub (PLAY/STOP pad during OVERDUBBING): ALWAYS immediate, hard flush.
// Quantize mode is ignored — abort means "stop now, throw away overdub".
void LoopEngine::abortOverdub(MidiTransport& transport) {
    if (_state != OVERDUBBING) return;
    #if DEBUG_SERIAL
    uint16_t discarded = _overdubCount;
    #endif
    _overdubCount = 0;
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    flushActiveNotes(transport, /*hard=*/true);
    _state         = STOPPED;
    _pendingAction = PENDING_NONE;
    #if DEBUG_SERIAL
    Serial.printf("[LOOP] ch%u OVD ABORT (%u events discarded, hard flush -> STOP)\n",
                  _channel + 1, (unsigned)discarded);
    #endif
}

// Cancel overdub (CLEAR long-press during OVERDUBBING): discard only the
// events captured during the current overdub pass, keep the loop playing
// with its previous content. This is the "undo overdub" path.
//
// ALWAYS immediate (the 500ms long-press IS the "human quantize"). No flush
// of active notes: the main loop keeps running via _events[] and the pending
// queue continues firing naturally. Any live pad the user is still holding
// will release normally via processLoopMode on the next frame.
void LoopEngine::cancelOverdub() {
    if (_state != OVERDUBBING) return;
    #if DEBUG_SERIAL
    uint16_t discarded = _overdubCount;
    #endif
    _overdubCount = 0;
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    _state         = PLAYING;
    _pendingAction = PENDING_NONE;
    // _playStartUs, _cursorIdx, _lastPositionUs untouched → loop continues
    // exactly where it was. No audible gap, no retrigger.
    #if DEBUG_SERIAL
    Serial.printf("[LOOP] ch%u OVD CANCEL (undo pass, %u events discarded -> PLAY)\n",
                  _channel + 1, (unsigned)discarded);
    #endif
}

// =================================================================
// Private transition implementations — the real work
// =================================================================

void LoopEngine::doStartRecording() {
    _eventCount    = 0;
    _recordStartUs = micros();
    _lastRecordBeatTick = 0xFFFFFFFF;  // force first beat flash at first tick
    _state         = RECORDING;
    #if DEBUG_SERIAL
    Serial.printf("[LOOP] ch%u REC start\n", _channel + 1);
    #endif
}

void LoopEngine::doStopRecording(const uint8_t* keyIsPressed, const uint8_t* padOrder, float currentBPM) {
    (void)padOrder;  // unused directly — padToNote uses _padOrder member

    // 1. Flush held pads — we haven't tracked "recorded pads" during the first
    //    recording, so we inject noteOff for every currently pressed pad. The
    //    semantic is correct: any note held at close must end at close time
    //    (otherwise the note is orphan).
    uint32_t closeUs    = micros();
    uint32_t positionUs = closeUs - _recordStartUs;
    for (uint8_t i = 0; i < 48; i++) {
        if (keyIsPressed[i] && _eventCount < MAX_LOOP_EVENTS) {
            _events[_eventCount++] = { positionUs, i, 0, {0, 0} };
        }
    }

    // 2. Latch recording BPM for proportional playback
    _recordBpm = (currentBPM < 10.0f) ? 10.0f : currentBPM;

    // 3. Bar-snap: round recorded duration to nearest integer bar count
    uint32_t barDurationUs      = (uint32_t)(4.0f * 60000000.0f / _recordBpm);
    uint32_t recordedDurationUs = closeUs - _recordStartUs;
    // AUDIT FIX B7 2026-04-06: defensive floor at 1 ms to prevent division
    // by zero in the scale computation below. In normal operation, REC pad
    // rising-edge + falling-edge sequence guarantees at least a few ms, but
    // a micros() wraparound or single-frame tap could theoretically produce
    // a zero duration.
    if (recordedDurationUs < 1000) recordedDurationUs = 1000;
    uint16_t bars = (recordedDurationUs + barDurationUs / 2) / barDurationUs;
    if (bars == 0)  bars = 1;
    if (bars > 64)  bars = 64;
    _loopLengthBars = bars;

    // 4. Normalize event offsets to [0, bars * barDurationUs) via proportional scale
    float scale = (float)(bars * barDurationUs) / (float)recordedDurationUs;
    for (uint16_t i = 0; i < _eventCount; i++) {
        _events[i].offsetUs = (uint32_t)((float)_events[i].offsetUs * scale);
    }

    // 5. Sort events by offsetUs (insertion sort — N typically small, cheap)
    sortEvents(_events, _eventCount);

    // 6. Transition directly to PLAYING. No call to play() — we're already on
    //    the boundary (pending dispatcher guarantees it) or in FREE mode where
    //    no snap is needed.
    _playStartUs    = micros();
    _cursorIdx      = 0;
    _lastPositionUs = 0;
    _lastBeatIdx    = 0xFFFFFFFF;   // force beat flash at first beat of playback
    _state          = PLAYING;
    #if DEBUG_SERIAL
    Serial.printf("[LOOP] ch%u REC close: %u events, %u bars @ %.1f BPM -> PLAY\n",
                  _channel + 1, (unsigned)_eventCount, (unsigned)_loopLengthBars, _recordBpm);
    #endif
}

void LoopEngine::doStartOverdub() {
    _overdubCount = 0;
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    _state        = OVERDUBBING;
    #if DEBUG_SERIAL
    Serial.printf("[LOOP] ch%u OVD start (base %u events)\n",
                  _channel + 1, (unsigned)_eventCount);
    #endif
}

void LoopEngine::doStopOverdub(const uint8_t* keyIsPressed, const uint8_t* padOrder, float currentBPM) {
    (void)padOrder;  // unused — kept for signature symmetry with stopRecording

    // 1. Flush held pads — but ONLY pads that had a recordNoteOn during this
    //    overdub session (tracked via _overdubActivePads). Pads held from before
    //    the overdub must NOT receive an injected noteOff (would be orphan).
    uint32_t closeUs           = micros();
    uint32_t liveDurationUs    = calcLoopDurationUs(currentBPM);
    uint32_t recordDurationUs  = calcLoopDurationUs(_recordBpm);
    if (liveDurationUs == 0 || recordDurationUs == 0) {
        _overdubCount = 0;
        _state = PLAYING;
        #if DEBUG_SERIAL
        Serial.printf("[LOOP] ch%u OVD close: zero duration guard -> PLAY\n", _channel + 1);
        #endif
        return;
    }
    uint32_t elapsedUs  = (closeUs - _playStartUs) % liveDurationUs;
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
    #if DEBUG_SERIAL
    uint16_t beforeMerge = _eventCount;
    uint16_t overdubSize = _overdubCount;
    #endif
    mergeOverdub();
    #if DEBUG_SERIAL
    uint16_t mergedCount = _eventCount - beforeMerge;  // 0 if B-PLAN-1 refused
    Serial.printf("[LOOP] ch%u OVD close: +%u/%u merged, total %u -> PLAY\n",
                  _channel + 1, (unsigned)mergedCount, (unsigned)overdubSize,
                  (unsigned)_eventCount);
    #endif

    // 4. Back to PLAYING — no change to _playStartUs (loop already running)
    _state = PLAYING;
}

void LoopEngine::doPlay(float currentBPM) {
    (void)currentBPM;   // BPM read live in tick() via parameter
    // Direct start. If we got here via pending dispatcher, we're already on a
    // boundary — cursor restarts from the top of the loop.
    _playStartUs    = micros();
    _cursorIdx      = 0;
    _lastPositionUs = 0;
    _lastBeatIdx    = 0xFFFFFFFF;   // force beat flash at first beat
    _state          = PLAYING;
    #if DEBUG_SERIAL
    Serial.printf("[LOOP] ch%u PLAY (%u events, %u bars)\n",
                  _channel + 1, (unsigned)_eventCount, (unsigned)_loopLengthBars);
    #endif
}

void LoopEngine::doStop(MidiTransport& transport) {
    // Soft flush — noteOff the currently active notes, but let the pending
    // queue finish its scheduled events (trailing notes allowed).
    flushActiveNotes(transport, /*hard=*/false);
    _state = STOPPED;
    #if DEBUG_SERIAL
    Serial.printf("[LOOP] ch%u STOP (soft flush, %u events preserved)\n",
                  _channel + 1, (unsigned)_eventCount);
    #endif
}

// =================================================================
// tick() — pending action dispatcher + proportional playback
// =================================================================
// AUDIT FIX (B1, 2026-04-06 pass 2): the original draft used
// (globalTick % boundary == 0) for the boundary check. That fails when
// ClockManager::generateTicks() catches up multiple ticks in one update()
// call (up to 4 — see ClockManager.cpp:181-203). Fix: track the last
// dispatched globalTick and detect a boundary CROSSING via integer division.
// Same bug pattern as B-001/B-CODE-1 in ArpEngine (resolved commit c23eea4).
void LoopEngine::tick(MidiTransport& transport, float currentBPM, uint32_t globalTick) {
    // ---- 1. Pending action dispatcher (quantize boundary CROSSING check) ----
    if (_pendingAction != PENDING_NONE) {
        uint16_t boundary = (_quantizeMode == LOOP_QUANT_BAR) ? TICKS_PER_BAR : TICKS_PER_BEAT;
        // Crossing detection: a boundary is crossed when (globalTick / boundary)
        // strictly increases vs the last dispatched value. First tick after
        // pending is set always crosses (sentinel 0xFFFFFFFF / boundary > any
        // valid value).
        bool crossed = (_lastDispatchedGlobalTick == 0xFFFFFFFF)
                     || ((globalTick / boundary) > (_lastDispatchedGlobalTick / boundary));
        if (crossed) {
            #if DEBUG_SERIAL
            Serial.printf("[LOOP] ch%u pending %s dispatched @globalTick=%u (boundary=%u)\n",
                          _channel + 1,
                          (_pendingAction < 7) ? LOOP_PENDING_NAMES[_pendingAction] : "?",
                          (unsigned)globalTick, (unsigned)boundary);
            #endif
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
            _pendingAction            = PENDING_NONE;
            _lastDispatchedGlobalTick = globalTick;
        }
        // Fall through to playback logic — loop continues running while
        // PENDING_STOP waits for boundary, and PENDING_START_OVERDUB plays
        // back normally while waiting.
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

    // Wrap detection: position jumped backward relative to last tick. DO NOT
    // flush active notes here — refcount + pending queue handle long notes that
    // cross the wrap naturally (Budget Philosophy: overlaps allowed).
    bool wrapped = (positionUs < _lastPositionUs);
    if (wrapped) {
        _cursorIdx   = 0;
        _wrapFlash   = true;
        _lastBeatIdx = 0xFFFFFFFF;   // reset so first beat of new cycle flashes
    }

    // ---- 4. PLAYING/OVERDUBBING tick flash (positionUs-based, QUANTIZED only) ----
    // FREE mode: no tick flashes during playback (solid render). QUANTIZED
    // modes: derive beat/bar from the loop's own structure.
    if (_quantizeMode != LOOP_QUANT_FREE && _loopLengthBars > 0) {
        uint32_t barDurationUs  = recordDurationUs / _loopLengthBars;
        uint32_t beatDurationUs = barDurationUs / 4;
        if (beatDurationUs > 0) {
            uint32_t beatIdxNow = positionUs / beatDurationUs;
            if (beatIdxNow != _lastBeatIdx) {
                _beatFlash = true;
                // Bar = every 4th beat (beat 1 of a new bar). Note: wrap
                // already sets _wrapFlash above; we don't set bar on wrap
                // because wrap is a strictly stronger event in the hierarchy.
                if ((beatIdxNow % 4) == 0 && !wrapped) _barFlash = true;
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

    // Suppress unused-parameter warning — transport is only used by doStop()
    // path via the pending dispatcher above.
    (void)transport;
}

// =================================================================
// processEvents — fire pending notes via refcount
// =================================================================
// noteOn: refcount increment — MIDI only on 0→1 transition
// noteOff: refcount decrement — MIDI only on 1→0 transition
// Mirror of ArpEngine::processEvents (src/arp/ArpEngine.cpp).
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

// =================================================================
// schedulePending — queue a note for later firing
// =================================================================
// Store in the first free slot. Silent drop on overflow — MAX_PENDING = 48 is
// sized generously to make this practically unreachable. Under DEBUG_SERIAL,
// log a warning so overflow shows up in dev but costs nothing in release.
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

// =================================================================
// flushActiveNotes — soft or hard flush
// =================================================================
// hard = true  → noteOff every active note AND empty the pending queue.
//                Used by clear(), abortOverdub(), hard reset paths.
// hard = false → noteOff every active note but LEAVE the pending queue running.
//                Used by doStop() — trailing shuffle/chaos events finish their course.
void LoopEngine::flushActiveNotes(MidiTransport& transport, bool hard) {
    for (uint8_t n = 0; n < 128; n++) {
        if (_noteRefCount[n] > 0) {
            transport.sendNoteOn(_channel, n, 0);  // noteOff via velocity=0
            _noteRefCount[n] = 0;
        }
    }
    if (hard) {
        for (uint8_t i = 0; i < MAX_PENDING; i++) _pending[i].active = false;
    }
}

// =================================================================
// recordNoteOn / recordNoteOff — capture input during RECORDING/OVERDUBBING
// =================================================================
// AUDIT FIX (B2, 2026-04-06): the original OVERDUBBING branch computed
// elapsedUs = (now - _playStartUs) % liveDurationUs where liveDurationUs was
// actually calculated from _recordBpm (misleading variable name) — mixing
// LIVE and RECORD timebases. With a tempo change between recording and
// playback, events ended up misaligned by tens of ms. Fix: read
// _lastPositionUs (already in RECORD timebase, written by tick() at the end
// of each frame). Latency ≤ 1 frame (~1 ms), musically imperceptible.

void LoopEngine::recordNoteOn(uint8_t padIndex, uint8_t velocity) {
    if (padIndex >= 48) return;
    if (_state == RECORDING) {
        if (_eventCount >= MAX_LOOP_EVENTS) return;
        uint32_t offsetUs = micros() - _recordStartUs;   // real-time during RECORDING
        _events[_eventCount++] = { offsetUs, padIndex, velocity, {0, 0} };
    } else if (_state == OVERDUBBING) {
        if (_overdubCount >= MAX_OVERDUB_EVENTS) return;
        // B2 fix: use _lastPositionUs (written by tick() each frame) which is
        // already in RECORD timebase. Same value tick() uses for the cursor
        // scan, so overdub events are perfectly aligned with the main _events[]
        // timebase regardless of live BPM divergence.
        uint32_t offsetUs = _lastPositionUs;
        _overdubBuf[_overdubCount++] = { offsetUs, padIndex, velocity, {0, 0} };
        _overdubActivePads[padIndex] = true;
    }
}

void LoopEngine::recordNoteOff(uint8_t padIndex) {
    if (padIndex >= 48) return;
    if (_state == RECORDING) {
        if (_eventCount >= MAX_LOOP_EVENTS) return;
        uint32_t offsetUs = micros() - _recordStartUs;
        _events[_eventCount++] = { offsetUs, padIndex, 0, {0, 0} };
    } else if (_state == OVERDUBBING) {
        if (_overdubCount >= MAX_OVERDUB_EVENTS) return;
        // B2 fix: use _lastPositionUs (see recordNoteOn above).
        uint32_t offsetUs = _lastPositionUs;
        _overdubBuf[_overdubCount++] = { offsetUs, padIndex, 0, {0, 0} };
        // NOTE: We do NOT clear _overdubActivePads[padIndex] here. The bitmask
        // tracks "was this pad recordNoteOn'd during this overdub session",
        // used solely by doStopOverdub() at close time to decide which held
        // pads deserve an injected noteOff. Once the user has released the pad
        // naturally, the bitmask entry doesn't matter — it's only read for
        // pads still held at close.
    }
}

// =================================================================
// sortEvents — insertion sort, in place
// =================================================================
// N is small (< MAX_OVERDUB_EVENTS for overdub buf, typically <200 events for
// main buf). Stable, no allocation.
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

// =================================================================
// mergeOverdub — reverse merge O(n+m), in place in _events[]
// =================================================================
// Assumes _events has capacity for _eventCount + _overdubCount. Both buffers
// must already be sorted by offsetUs. Algorithm: reverse merge from the end.
void LoopEngine::mergeOverdub() {
    if (_overdubCount == 0) return;
    uint16_t totalCount = _eventCount + _overdubCount;
    if (totalCount > MAX_LOOP_EVENTS) {
        // B-PLAN-1 fix (audit 2026-04-07): refuse the entire overdub merge
        // rather than silently truncate arbitrary events. The original
        // truncation kept _overdubCount = (MAX - eventCount), which dropped
        // the LATEST overdub events from the buffer tail (musically
        // counter-intuitive — overdub usually accumulates at the end).
        // Refusing the whole merge preserves the existing loop and signals
        // via DEBUG log that the user has hit the buffer ceiling.
        #if DEBUG_SERIAL
        Serial.printf("[LOOP] mergeOverdub refused: total %u > max %u, %u overdub events lost\n",
                      totalCount, MAX_LOOP_EVENTS, _overdubCount);
        #endif
        _overdubCount = 0;
        return;
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

// =================================================================
// flushLiveNotes — bank switch cleanup
// =================================================================
// Called by BankManager on bank switch (outgoing bank) and by midiPanic().
// Sends CC123 (All Notes Off) on this engine's channel and zeroes refcounts +
// live-note tracking. Does NOT touch the pending queue — the engine keeps
// running in background and its pending events will fire on the correct
// channel (still _channel).
void LoopEngine::flushLiveNotes(MidiTransport& transport, uint8_t channel) {
    (void)channel;  // signature kept for symmetry; we always use our own _channel
    transport.sendAllNotesOff(_channel);
    memset(_noteRefCount, 0, sizeof(_noteRefCount));
    memset(_liveNote, 0xFF, sizeof(_liveNote));   // B1: clear live-press tracking
    // Pending queue NOT cleared — trailing events will fire normally on _channel.
    // New live presses on the incoming bank (different channel) won't collide
    // because refcount is per-engine and only manages this engine's channel.
}

// =================================================================
// setLiveNote / releaseLivePad — per-pad live-press tracking (AUDIT FIX B1)
// =================================================================
// Called by processLoopMode at rising edge (after noteRefIncrement sent MIDI
// noteOn). Tracks which MIDI note this pad currently holds live so that
// releaseLivePad() can clean up later without re-resolving padToNote(). Since
// padOrder is runtime-immutable (setup tools are boot-only), the stored note
// remains valid for the entire live-press lifetime.
void LoopEngine::setLiveNote(uint8_t padIndex, uint8_t note) {
    if (padIndex >= NUM_KEYS) return;
    _liveNote[padIndex] = note;
}

// Idempotent per-pad noteOff: releases whatever live note was attached to
// this pad. No-op if the pad has no live note (0xFF). Callers:
//   1. processLoopMode at falling edge (replaces direct noteRefDecrement)
//   2. handleLeftReleaseCleanup sweep on LOOP banks (replaces the obsolete
//      s_lastKeys edge check — idempotent sweep pattern, parallel to
//      MidiEngine::noteOff() for NORMAL banks)
// Clears _liveNote[padIndex] BEFORE calling noteRefDecrement to prevent
// re-entry if noteRefDecrement somehow triggers a callback (defensive).
void LoopEngine::releaseLivePad(uint8_t padIndex, MidiTransport& transport) {
    if (padIndex >= NUM_KEYS) return;
    uint8_t note = _liveNote[padIndex];
    if (note == 0xFF) return;                     // already released — idempotent no-op
    _liveNote[padIndex] = 0xFF;                   // clear first (re-entry safety)
    if (noteRefDecrement(note)) {
        transport.sendNoteOn(_channel, note, 0);  // velocity 0 = noteOff
    }
}

// =================================================================
// Param setters — stubs filled in Phase 5 (simple member store only)
// =================================================================

void LoopEngine::setShuffleDepth(float depth)       { _shuffleDepth = depth; }
void LoopEngine::setShuffleTemplate(uint8_t tmpl)   { _shuffleTemplate = tmpl; }
void LoopEngine::setChaosAmount(float amount)       { _chaosAmount = amount; }
void LoopEngine::setVelPatternIdx(uint8_t idx)      { _velPatternIdx = idx; }
void LoopEngine::setVelPatternDepth(float depth)    { _velPatternDepth = depth; }
void LoopEngine::setBaseVelocity(uint8_t vel)       { _baseVelocity = vel; }
void LoopEngine::setVelocityVariation(uint8_t pct)  { _velocityVariation = pct; }

// =================================================================
// Getters
// =================================================================

LoopEngine::State LoopEngine::getState() const      { return _state; }
bool     LoopEngine::isPlaying() const              { return _state == PLAYING; }
bool     LoopEngine::isRecording() const            { return _state == RECORDING || _state == OVERDUBBING; }
bool     LoopEngine::hasPendingAction() const       { return _pendingAction != PENDING_NONE; }
uint8_t  LoopEngine::getLoopQuantizeMode() const    { return _quantizeMode; }
uint16_t LoopEngine::getEventCount() const          { return _eventCount; }

// Consume-on-read pattern: flag is cleared as it is returned. Each flag is
// consumed independently by LedController each frame, so if multiple flags
// are set (e.g. beat + bar + wrap at the same sample), they all get delivered.
// LedController then picks the hierarchy winner (wrap > bar > beat) for render.
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

// Param getters (stubs — filled in Phase 5)
float    LoopEngine::getShuffleDepth() const     { return _shuffleDepth; }
uint8_t  LoopEngine::getShuffleTemplate() const  { return _shuffleTemplate; }
float    LoopEngine::getChaosAmount() const      { return _chaosAmount; }
uint8_t  LoopEngine::getVelPatternIdx() const    { return _velPatternIdx; }
float    LoopEngine::getVelPatternDepth() const  { return _velPatternDepth; }
