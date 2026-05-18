#include "LoopEngine.h"
#include "../core/MidiTransport.h"
#include "../midi/ClockManager.h"
#include "../core/HardwareConfig.h"
#include "../viewer/ViewerSerial.h"   // viewer::emitLoopBufferFull (m9 audit)
#include <Arduino.h>
#include <string.h>

// =================================================================
// Constructor
// =================================================================
LoopEngine::LoopEngine()
  : _channel(0)
  , _quantize(LOOP_QUANT_BAR)
  , _padOrder(nullptr)
  , _clock(nullptr)
  , _recPad(0xFF)
  , _playStopPad(0xFF)
  , _clearPad(0xFF)
  , _baseVelocity(DEFAULT_BASE_VELOCITY)
  , _velocityVariation(DEFAULT_VELOCITY_VARIATION)
  , _clearLoopTimerMs(500)
  , _state(LoopState::EMPTY)
  , _recordStartUs(0)
  , _recordEndUs(0)
  , _recordBpm(120)
  , _recordFirstPressDone(false)
  , _loopDurationUs(0)
  , _loopBars(0)
  , _eventCount(0)
  , _overdubCount(0)
  , _playStartUs(0)
  , _scaledElapsedUs(0)        // B1: cumulative scaled position (uint64)
  , _lastUpdateUs(0)            // B1: previous update() tick timestamp
  , _playPositionUs(0)
  , _playNextEventIdx(0)
  , _lastBarIndex(0)
  , _barFlash(false)
  , _wrapFlash(false)
  , _waitingTargetTick(0)
  , _clearPressStartMs(0)
  , _clearFired(false)
{
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) {
    _events[i].active = false;
  }
  for (uint8_t i = 0; i < MAX_LOOP_OVERDUB_EVENTS; i++) {
    _overdubEvents[i].active = false;
  }
  for (uint8_t i = 0; i < MAX_LOOP_PENDING_NOTEOFFS; i++) {
    _pendingNoteOffs[i].active = false;
  }
  memset(_noteRefCount, 0, sizeof(_noteRefCount));
  memset(_padHeldLive, 0, sizeof(_padHeldLive));
}

// =================================================================
// Configuration setters
// =================================================================
void LoopEngine::setChannel(uint8_t ch)              { _channel = ch; }
void LoopEngine::setQuantize(LoopQuantize q)         { _quantize = (q <= LOOP_QUANT_BAR) ? q : LOOP_QUANT_BAR; }
void LoopEngine::setPadOrder(const uint8_t* padOrder){ _padOrder = padOrder; }
void LoopEngine::setClockManager(ClockManager* clock){ _clock = clock; }
void LoopEngine::setBaseVelocity(uint8_t vel)        { _baseVelocity = vel; }
void LoopEngine::setVelocityVariation(uint8_t pct)   { _velocityVariation = pct; }
void LoopEngine::setClearLoopTimerMs(uint16_t ms)    { _clearLoopTimerMs = ms; }

void LoopEngine::setControlPads(uint8_t recPad, uint8_t playStopPad, uint8_t clearPad) {
  _recPad      = recPad;
  _playStopPad = playStopPad;
  _clearPad    = clearPad;
}

bool LoopEngine::isLoopControlPad(uint8_t padIndex) const {
  return padIndex == _recPad
      || padIndex == _playStopPad
      || padIndex == _clearPad;
}

// =================================================================
// LED flash hooks
// =================================================================
bool LoopEngine::consumeBarFlash() {
  bool v = _barFlash;
  _barFlash = false;
  return v;
}

bool LoopEngine::consumeWrapFlash() {
  bool v = _wrapFlash;
  _wrapFlash = false;
  return v;
}

// =================================================================
// CLEAR press tracking
// =================================================================
void LoopEngine::notifyClearPressStart(uint32_t nowMs) {
  if (_clearPressStartMs == 0) {
    _clearPressStartMs = nowMs;
    _clearFired = false;
  }
}

void LoopEngine::notifyClearPressEnd() {
  _clearPressStartMs = 0;
  _clearFired = false;
}

bool LoopEngine::isClearHoldFired(uint32_t nowMs) const {
  if (_clearPressStartMs == 0 || _clearFired) return false;
  return (nowMs - _clearPressStartMs) >= _clearLoopTimerMs;
}

// --- Stubs (real impl Tasks 3-46) ---

// =================================================================
// tapRec — transport action, state machine dispatch (spec §7 §8 + Q5 §28)
// =================================================================
void LoopEngine::tapRec(MidiTransport& transport) {
  #if DEBUG_SERIAL
  Serial.printf("[LOOP] tapRec ch=%u state=%u\n", _channel, (unsigned)_state);
  #endif
  switch (_state) {
    case LoopState::EMPTY:
      startRecording(transport);
      break;
    case LoopState::RECORDING:
      stopRecording(transport);  // bar-snap + → PLAYING via startPlayback interne
      break;
    case LoopState::PLAYING:
      // Enter OVERDUBBING — main buffer continues playback, overdub captures additions
      _overdubCount = 0;
      _state = LoopState::OVERDUBBING;
      break;
    case LoopState::OVERDUBBING:
      // M2 / M4 : mergeOverdub retourne false si capacité dépassée → abandon atomique silent.
      // Si OK : main buffer merged, → PLAYING. Si échec : overdub déjà reset, stay PLAYING.
      mergeOverdub();
      _state = LoopState::PLAYING;
      break;
    case LoopState::STOPPED:
      // Q5 §28 : tap REC on STOPPED-loaded → PLAYING + OVERDUBBING simultaneously
      startPlayback(transport, micros());   // B3 : nowUs capturé localement
      _overdubCount = 0;
      _state = LoopState::OVERDUBBING;
      break;
    case LoopState::WAITING_PLAY:
      // Spec §17 : REC during WAITING_PLAY ignored (REC senseless on STOPPED-pending-play)
      break;
    case LoopState::WAITING_STOP:
      // Spec §17 : REC during WAITING_STOP cancels stop and enters OVERDUBBING.
      // M1 fix : pas de double-assignment _state ici, directement OVERDUBBING.
      _overdubCount = 0;
      _state = LoopState::OVERDUBBING;
      break;
  }
  #if DEBUG_SERIAL
  Serial.printf("[LOOP] tapRec -> state=%u\n", (unsigned)_state);
  #endif
}

// =================================================================
// tapPlayStop — transport action, state machine dispatch (spec §9 §17)
// currentKeys = keyIsPressed (FG context) or nullptr (BG context).
// Param non utilisé actuellement (réservé), peut être ignoré par l'impl.
// =================================================================
void LoopEngine::tapPlayStop(MidiTransport& transport, const uint8_t* /*currentKeys*/) {
  #if DEBUG_SERIAL
  Serial.printf("[LOOP] tapPlayStop ch=%u state=%u\n", _channel, (unsigned)_state);
  #endif
  switch (_state) {
    case LoopState::EMPTY:
      // No-op : nothing to play
      break;
    case LoopState::STOPPED:
      if (_quantize == LOOP_QUANT_FREE) {
        startPlayback(transport, micros());     // B3 : nowUs capturé localement (hors update())
      } else {
        _waitingTargetTick = computeNextBoundaryTick(_quantize);
        _state = LoopState::WAITING_PLAY;
      }
      break;
    case LoopState::PLAYING:
      if (_quantize == LOOP_QUANT_FREE) {
        stopPlayback(transport, /*flushNotes=*/true);
      } else {
        _waitingTargetTick = computeNextBoundaryTick(_quantize);
        _state = LoopState::WAITING_STOP;
      }
      break;
    case LoopState::OVERDUBBING:
      // Spec §8 : abandon overdub, stay PLAYING. A second tap then stops normally.
      abandonOverdub();
      _state = LoopState::PLAYING;
      break;
    case LoopState::RECORDING:
      // No-op : PLAY/STOP during RECORDING ignored (REC is the only way out)
      break;
    case LoopState::WAITING_PLAY:
      // Spec §17 : cancel waiting, return STOPPED
      _state = LoopState::STOPPED;
      break;
    case LoopState::WAITING_STOP:
      // Spec §17 : cancel waiting, return PLAYING
      _state = LoopState::PLAYING;
      break;
  }
  #if DEBUG_SERIAL
  Serial.printf("[LOOP] tapPlayStop -> state=%u\n", (unsigned)_state);
  #endif
}

// =================================================================
// longPressClear — destructive wipe (spec §9). Called once threshold fired.
// Refused during RECORDING/OVERDUBBING (caller filters via isLocked()).
// B2 fix : set _clearFired=true à la fin pour bloquer re-fires tant que CLEAR tenu.
// =================================================================
void LoopEngine::longPressClear(MidiTransport& transport) {
  #if DEBUG_SERIAL
  Serial.printf("[LOOP] longPressClear ch=%u state=%u\n", _channel, (unsigned)_state);
  #endif
  // B2 fix généralisé : armer _clearFired AVANT la garde isLocked, pour bloquer
  // les re-fires que CLEAR soit accepté (wipe) ou refusé (REC/OD). Sans cette
  // ligne en amont, le hold CLEAR pendant RECORDING/OVERDUBBING re-fire à chaque
  // frame (~3 ms) et flood le serial. notifyClearPressEnd reset à la release.
  _clearFired = true;
  if (isLocked()) return;  // refus silencieux pendant REC/OD (caller should filter aussi)
  // Wipe buffer + flush MIDI notes + state → EMPTY
  flushPendingNoteOffs(transport);
  _eventCount = 0;
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) _events[i].active = false;
  _overdubCount = 0;
  _loopDurationUs = 0;
  _loopBars = 0;
  _state = LoopState::EMPTY;
  #if DEBUG_SERIAL
  Serial.printf("[LOOP] longPressClear -> state=%u (EMPTY)\n", (unsigned)_state);
  #endif
}
// =================================================================
// capturePadEvent — décisions actées post-audit :
//   M3 (Q8) : velocity stocké = baseVelocity STRICT (passé par processLoopMode).
//             Variation appliquée uniquement au playback dans update().
//   M2 (Q4) : insertion live-sorted dans _events[] (RECORDING) ou _overdubEvents[]
//             (OVERDUBBING). Buffer toujours trié, mergeOverdub devient O(n+m).
//   M8 (Q1) : MIDI live monitor émis dans TOUS les états (spec §18 "percussion fixe").
//             EMPTY/STOPPED/WAITING_* → live monitor seul, pas de capture buffer.
//             RECORDING/OVERDUBBING → live monitor + capture buffer.
// velocity == 0 → noteOff event ; > 0 → noteOn event.
// =================================================================
void LoopEngine::capturePadEvent(uint8_t padIndex, uint8_t velocity, MidiTransport& transport) {
  if (padIndex >= NUM_KEYS) return;

  bool isNoteOn = (velocity > 0);
  uint8_t midiNote = resolvePadToMidiNote(padIndex);

  // M8 Q1 : live monitor MIDI émis dans TOUS les états (spec §18 "percussion fixe").
  // Velocity strict baseVelocity (sans variation, M3 Q8). Pas d'aftertouch (spec §24).
  if (isNoteOn) {
    refCountNoteOn(transport, midiNote, velocity);
  } else {
    refCountNoteOff(transport, midiNote);
  }

  // Audit-fix B-N1 / B-N2 / R-N1 : tracker live press (TOUS états).
  // Set true à chaque rising edge, false à chaque falling edge. Consommé par :
  //   - stopRecording flushHeldPadsAsNoteOffs (fin de loop).
  //   - mergeOverdub flush held (inject noteOff à _playPositionUs).
  //   - onBackgroundTransition (refCountNoteOff direct au bank switch out).
  // Invariant : _padHeldLive[pad] == true ssi pad physiquement enfoncé ET live
  // monitor a émis noteOn (refCountNoteOn ci-dessus). Set/reset symétrique.
  _padHeldLive[padIndex] = isNoteOn ? 1 : 0;

  // Capture buffer écriture conditionnelle (RECORDING ou OVERDUBBING seuls).
  if (_state == LoopState::RECORDING) {
    uint32_t nowUs = micros();

    // m6 Q7 : noteOff avant 1st press musical ignoré (pad tenu avant tapRec).
    if (!_recordFirstPressDone && !isNoteOn) {
      return;
    }
    // First-press latches recordStart + recordBpm (invariant §23.5)
    if (!_recordFirstPressDone && isNoteOn) {
      _recordStartUs = nowUs;
      _recordBpm = _clock ? _clock->getSmoothedBPM() : 120;
      if (_recordBpm == 0) _recordBpm = 120;
      _recordFirstPressDone = true;
    }

    uint32_t timestampUs = nowUs - _recordStartUs;
    // M2 live-sort : insertion triée dans _events[].
    bool ok = insertEventSorted(_events, _eventCount, MAX_LOOP_EVENTS,
                                 timestampUs, padIndex, midiNote, velocity);
    if (!ok) {
      // m9 telemetry : buffer plein, drop silent per spec §8 + emit viewer.
      viewer::emitLoopBufferFull(_channel, "main");
    }

    // Audit-fix : _padHeldLive[padIndex] déjà set en haut de capturePadEvent (tracker unifié).
    return;
  }

  if (_state == LoopState::OVERDUBBING) {
    // B1 fix audit : utilise _playPositionUs maintenu par update() (precision ~1 ms),
    // pas d'appel à computeLoopPositionUs (méthode supprimée).
    uint32_t posInLoop = _playPositionUs;

    // M2 live-sort : insertion triée dans _overdubEvents[].
    bool ok = insertEventSorted(_overdubEvents, _overdubCount, MAX_LOOP_OVERDUB_EVENTS,
                                 posInLoop, padIndex, midiNote, velocity);
    if (!ok) {
      // m9 telemetry overdub buffer full.
      viewer::emitLoopBufferFull(_channel, "overdub");
    }
    return;
  }

  // EMPTY / STOPPED / PLAYING / WAITING_* : live monitor seul (déjà émis ci-dessus).
  // Pas de buffer write — la musique live n'altère pas le loop sans tap REC explicite.
  // Conformité spec §5 "Le buffer LOOP est sacré".
}
// =================================================================
// update — called every main loop iteration (Phase 2 LOOP, B1+B3 fixes audit)
// =================================================================
// Phases :
//   1. WAITING_PLAY / WAITING_STOP boundary check (ClockManager ticks)
//      → commitWaitingAction(transport, nowUs) — B3 propage nowUs.
//   2. Drain pending noteOff queue (gates, stuck-note safety).
//   3. PLAYING / OVERDUBBING : intégration incrémentale BPM (B1) + walk events.
//   4. Wrap detection sur _scaledElapsedUs ≥ _loopDurationUs.
//   5. Bar crossing detection (sur _playPositionUs).
// =================================================================
void LoopEngine::update(MidiTransport& transport) {
  uint32_t nowUs = micros();

  // (1) WAITING_* → commit when boundary tick reached (B3 : propager nowUs)
  if (_state == LoopState::WAITING_PLAY || _state == LoopState::WAITING_STOP) {
    if (_clock && _clock->getCurrentTick() >= _waitingTargetTick) {
      commitWaitingAction(transport, nowUs);
      // Si on vient de transitioner vers PLAYING, _lastUpdateUs == nowUs (startPlayback l'a set),
      // donc le delta de la phase (3) ci-dessous sera 0. Pas de saut.
    }
  }

  // (2) Drain pending noteOffs (gates, etc.)
  drainPendingNoteOffs(transport, nowUs);

  // (3) Playback walk (B1 intégration incrémentale + audit-fix B1 WAITING_STOP)
  // WAITING_STOP inclus dans la garde : spec §17 "la LOOP continue de jouer
  // jusqu'au boundary tick commit". Sans WAITING_STOP ici, le tap PLAY/STOP en
  // quantize Beat/Bar silencerait la LOOP instantanément (silence immédiat puis
  // commit silencieux au boundary = quantize inaudible).
  if (_state == LoopState::PLAYING || _state == LoopState::OVERDUBBING
      || _state == LoopState::WAITING_STOP) {
    if (_eventCount == 0 || _loopDurationUs == 0) {
      _lastUpdateUs = nowUs;  // garder ancre cohérente même si rien à jouer
      return;
    }

    // B1 : accumulate scaled delta cumulatif. uint64 anti-overflow longues sessions.
    uint16_t liveBpm = _clock ? _clock->getSmoothedBPM() : _recordBpm;
    if (liveBpm == 0) liveBpm = _recordBpm;
    uint32_t deltaUs = nowUs - _lastUpdateUs;     // unsigned wraparound-safe sur micros() overflow
    _lastUpdateUs = nowUs;
    _scaledElapsedUs += (uint64_t)deltaUs * liveBpm / _recordBpm;

    // (4) Wrap detection : while-loop pour absorber catch-up sur freeze potentiel (insertion sort).
    while (_scaledElapsedUs >= (uint64_t)_loopDurationUs) {
      // Fire tail events (du playNextEventIdx jusqu'à eventCount) avant de wrap.
      while (_playNextEventIdx < _eventCount && _events[_playNextEventIdx].active) {
        const LoopEvent& e = _events[_playNextEventIdx];
        if (e.velocity > 0) {
          refCountNoteOn(transport, e.midiNote, applyVelocityVariation(e.velocity));
        } else {
          refCountNoteOff(transport, e.midiNote);
        }
        _playNextEventIdx++;
      }
      // Wrap : soustrait loopDuration de l'accumulateur (reste borné).
      _scaledElapsedUs -= _loopDurationUs;
      _playNextEventIdx = 0;
      _lastBarIndex = 0;
      _wrapFlash = true;
    }
    _playPositionUs = (uint32_t)_scaledElapsedUs;

    // Fire events whose timestamp <= current position
    while (_playNextEventIdx < _eventCount
           && _events[_playNextEventIdx].active
           && _events[_playNextEventIdx].timestampUs <= _playPositionUs) {
      const LoopEvent& e = _events[_playNextEventIdx];
      if (e.velocity > 0) {
        // M3 fix Q8 : applyVelocityVariation appliquée seulement au playback (ici).
        refCountNoteOn(transport, e.midiNote, applyVelocityVariation(e.velocity));
      } else {
        refCountNoteOff(transport, e.midiNote);
      }
      _playNextEventIdx++;
    }

    // (5) Bar crossing detection (sur position courante)
    uint32_t barDurUs = (_loopBars > 0) ? (_loopDurationUs / _loopBars) : _loopDurationUs;
    uint16_t currentBarIdx = (barDurUs > 0) ? (uint16_t)(_playPositionUs / barDurUs) : 0;
    if (currentBarIdx != _lastBarIndex) {
      _barFlash = true;
      _lastBarIndex = currentBarIdx;
    }
    return;
  }

  // États non-playback : garder _lastUpdateUs synchronisé pour éviter delta géant au prochain PLAYING.
  _lastUpdateUs = nowUs;
}
// =================================================================
// resolvePadToMidiNote — pad → MIDI note (spec §1, no scale, no padOrder if null)
// =================================================================
// Convention GM : pad 0 = kick C2 = MIDI 36. With padOrder applied, the user's
// "ordered position" of the pad determines the note offset.
uint8_t LoopEngine::resolvePadToMidiNote(uint8_t padIndex) const {
  if (padIndex >= NUM_KEYS) return MIDI_BASE_NOTE;
  uint8_t pos = _padOrder ? _padOrder[padIndex] : padIndex;
  uint16_t note = (uint16_t)MIDI_BASE_NOTE + pos;
  return (note > 127) ? 127 : (uint8_t)note;
}

// =================================================================
// applyVelocityVariation — symmetric processNormalMode main.cpp:676-680
// =================================================================
uint8_t LoopEngine::applyVelocityVariation(uint8_t baseVel) const {
  if (_velocityVariation == 0) return baseVel;
  int16_t range = (int16_t)_velocityVariation * 127 / 200;
  int16_t offset = (int16_t)(random(-range, range + 1));
  int16_t result = (int16_t)baseVel + offset;
  if (result < 1) result = 1;
  if (result > 127) result = 127;
  return (uint8_t)result;
}
// =================================================================
// insertEventSorted — M2 Q4 décision (live-sort) :
// Insère un event à sa position triée (par timestampUs croissant) dans buffer[].
// Retourne true si inséré, false si buffer plein (drop silent per spec §8).
// Complexité : O(n) par insertion (binary search + shift).
// =================================================================
bool LoopEngine::insertEventSorted(LoopEvent* buffer, uint16_t& count, uint16_t cap,
                                     uint32_t timestampUs, uint8_t padIndex,
                                     uint8_t midiNote, uint8_t velocity) {
  if (count >= cap) return false;  // buffer full
  // Binary search position d'insertion (upper_bound : 1er index avec timestamp > new).
  // Permet de garder l'ordre stable pour events au même timestamp (push back ↦ ordre d'arrivée).
  int32_t lo = 0;
  int32_t hi = (int32_t)count;
  while (lo < hi) {
    int32_t mid = (lo + hi) / 2;
    if (buffer[mid].timestampUs <= timestampUs) lo = mid + 1;
    else hi = mid;
  }
  // lo = position d'insertion (peut être == count si append en fin).
  // Shift right.
  for (int32_t j = (int32_t)count; j > lo; j--) {
    buffer[j] = buffer[j - 1];
  }
  buffer[lo].timestampUs = timestampUs;
  buffer[lo].padIndex    = padIndex;
  buffer[lo].midiNote    = midiNote;
  buffer[lo].velocity    = velocity;
  buffer[lo].active      = true;
  count++;
  return true;
}
// =================================================================
// startRecording — EMPTY → RECORDING. Arms capture. recordStartUs set on 1st pad press.
// =================================================================
void LoopEngine::startRecording(MidiTransport& transport) {
  (void)transport;  // no MIDI flush needed here (EMPTY = nothing playing)
  _eventCount = 0;
  _overdubCount = 0;
  _recordStartUs = 0;
  _recordEndUs = 0;
  _recordBpm = _clock ? _clock->getSmoothedBPM() : 120;
  if (_recordBpm == 0) _recordBpm = 120;
  _recordFirstPressDone = false;
  memset(_padHeldLive, 0, sizeof(_padHeldLive));
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) _events[i].active = false;
  _state = LoopState::RECORDING;
}
// =================================================================
// stopRecording — close RECORDING with bar-snap (spec §7)
// 1. Compute raw recorded duration (now - recordStartUs)
// 2. Compute barDuration_us at recordBpm (4 beats × 60s/bpm × 1e6 = 240e6/bpm)
// 3. Snap with deadzone 25% : if elapsed within 0.25×barDuration after a bar line, snap down
//    else snap up. Min 1 bar, max 64 bars.
// 4. Rescale event timestamps proportionally to fill snapped duration
// 5. Flush pads held → noteOff events at snapped duration
// 6. State → PLAYING (via startPlayback)
// =================================================================
void LoopEngine::stopRecording(MidiTransport& transport) {
  uint32_t nowUs = micros();
  _recordEndUs = nowUs;

  if (!_recordFirstPressDone || _eventCount == 0) {
    // No content — back to EMPTY
    _state = LoopState::EMPTY;
    return;
  }

  uint32_t rawDurUs = nowUs - _recordStartUs;
  uint32_t barDurUs = (uint32_t)(240000000UL / _recordBpm);  // 240e6 / bpm = bar duration µs (4 beats)
  if (barDurUs == 0) barDurUs = 2000000;  // safety : 120 BPM = 2s bar

  // Bar-snap with 25% deadzone
  uint32_t barsFloor = rawDurUs / barDurUs;
  uint32_t remainder = rawDurUs - (barsFloor * barDurUs);
  uint32_t deadzone  = barDurUs / 4;  // 25%

  uint32_t snappedBars;
  if (remainder <= deadzone) {
    snappedBars = barsFloor;  // snap down (deadzone absorbs overshoot)
  } else {
    snappedBars = barsFloor + 1;  // round up
  }
  if (snappedBars < 1)  snappedBars = 1;
  if (snappedBars > 64) snappedBars = 64;
  _loopBars = (uint16_t)snappedBars;
  uint32_t snappedDurUs = snappedBars * barDurUs;

  // Rescale event timestamps proportionally
  if (rawDurUs > 0 && snappedDurUs != rawDurUs) {
    uint64_t scaleNum = snappedDurUs;
    uint64_t scaleDen = rawDurUs;
    for (uint16_t i = 0; i < _eventCount; i++) {
      _events[i].timestampUs = (uint32_t)((uint64_t)_events[i].timestampUs * scaleNum / scaleDen);
    }
  }
  _loopDurationUs = snappedDurUs;

  // Flush held pads as noteOff events at snapped duration.
  // flushHeldPadsAsNoteOffs utilise insertEventSorted (M2 live-sort) → buffer reste trié.
  flushHeldPadsAsNoteOffs(snappedDurUs);

  // M5 fix : pas de re-sort ici (live-sort dans capturePadEvent maintient l'ordre,
  // et flushHeldPadsAsNoteOffs insert également via insertEventSorted). Le buffer
  // est invariant sorted dès la fin du recording.

  // M6 fix : validation timestamp < loopDuration (defense in depth contre rescale buggy
  // ou edge case capture juste à rawDurUs). Clamp _loopDurationUs - 1 si dépassement.
  uint16_t clampedCount = 0;
  for (uint16_t i = 0; i < _eventCount; i++) {
    if (_events[i].timestampUs >= _loopDurationUs) {
      _events[i].timestampUs = _loopDurationUs - 1;
      clampedCount++;
    }
  }
  #if DEBUG_SERIAL
  if (clampedCount > 0) {
    Serial.printf("[LOOP WARN] stopRecording clamped %u events to loopDur=%lu us\n",
                  clampedCount, (unsigned long)_loopDurationUs);
  }
  #endif

  // Flush refcount + transition to PLAYING (B3 fix : passer nowUs capturé).
  flushPendingNoteOffs(transport);
  startPlayback(transport, micros());
}
// =================================================================
// flushHeldPadsAsNoteOffs — inject noteOff into main buffer for pads still held at stopRecording
// =================================================================
// M2 fix : utilise insertEventSorted pour maintenir buffer trié (live-sort).
void LoopEngine::flushHeldPadsAsNoteOffs(uint32_t timestampUs) {
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    if (!_padHeldLive[pad]) continue;
    // insertion triée (M2). insertEventSorted retourne false si buffer plein → silent drop.
    insertEventSorted(_events, _eventCount, MAX_LOOP_EVENTS,
                       timestampUs, pad, resolvePadToMidiNote(pad), /*velocity=*/0);
    _padHeldLive[pad] = false;
  }
}
// =================================================================
// startPlayback — STOPPED/EMPTY → PLAYING (B1+B3 fixes audit)
// nowUs param : timestamp capturé par le caller (update() phase 1 commitWaitingAction,
// OR tapPlayStop/tapRec où micros() est safe). Évite l'underflow uint32 sur enchaînement
// commitWaitingAction → startPlayback (B3 audit).
// =================================================================
void LoopEngine::startPlayback(MidiTransport& transport, uint32_t nowUs) {
  (void)transport;
  _playStartUs = nowUs;                  // debug / timestamp PLAYING entry
  _scaledElapsedUs = 0;                  // B1 : reset intégration cumulative
  _lastUpdateUs = nowUs;                 // B1 : ancrage premier delta = 0 au prochain update
  _playPositionUs = 0;
  _playNextEventIdx = 0;
  _lastBarIndex = 0;
  _state = LoopState::PLAYING;
}

// =================================================================
// stopPlayback — PLAYING/OVERDUBBING → STOPPED. flushNotes=true emits noteOff for refcount > 0
// =================================================================
void LoopEngine::stopPlayback(MidiTransport& transport, bool flushNotes) {
  if (flushNotes) {
    flushPendingNoteOffs(transport);
  }
  _state = LoopState::STOPPED;
}
// computeLoopPositionUs stub supprimé (B1 fix : méthode supprimée du plan)
// =================================================================
// scheduleNoteOff — queue a future noteOff (gate length or stuck flush)
// Returns false if queue full (silent drop, no crash).
// =================================================================
bool LoopEngine::scheduleNoteOff(uint32_t fireTimeUs, uint8_t note) {
  for (uint8_t i = 0; i < MAX_LOOP_PENDING_NOTEOFFS; i++) {
    if (!_pendingNoteOffs[i].active) {
      _pendingNoteOffs[i].fireTimeUs = fireTimeUs;
      _pendingNoteOffs[i].note       = note;
      _pendingNoteOffs[i].active     = true;
      return true;
    }
  }
  return false;  // queue full — note will rely on next flushPendingNoteOffs for safety
}

// =================================================================
// drainPendingNoteOffs — fire pending noteOffs whose time has arrived
// =================================================================
void LoopEngine::drainPendingNoteOffs(MidiTransport& transport, uint32_t nowUs) {
  for (uint8_t i = 0; i < MAX_LOOP_PENDING_NOTEOFFS; i++) {
    if (!_pendingNoteOffs[i].active) continue;
    if ((int32_t)(nowUs - _pendingNoteOffs[i].fireTimeUs) >= 0) {
      refCountNoteOff(transport, _pendingNoteOffs[i].note);
      _pendingNoteOffs[i].active = false;
    }
  }
}

// =================================================================
// Refcounted MIDI noteOn / noteOff (symetric ArpEngine pattern Q2 §28)
// =================================================================
void LoopEngine::refCountNoteOn(MidiTransport& transport, uint8_t note, uint8_t velocity) {
  if (_noteRefCount[note] == 0) {
    transport.sendNoteOn(_channel, note, velocity);
  }
  if (_noteRefCount[note] < 255) {
    _noteRefCount[note]++;
  }
}

void LoopEngine::refCountNoteOff(MidiTransport& transport, uint8_t note) {
  if (_noteRefCount[note] == 0) return;
  _noteRefCount[note]--;
  if (_noteRefCount[note] == 0) {
    transport.sendNoteOn(_channel, note, 0);  // velocity 0 = noteOff convention
  }
}

// =================================================================
// flushPendingNoteOffs — emergency silence + state → STOPPED.
// Symetric ArpEngine::flushPendingNoteOffs (ArpEngine.cpp:864 fait `_playing = false`).
// Audit-fix B2 : sans la transition d'état, midiPanic() laisse les LOOP banks en
// PLAYING/OVERDUBBING ; au prochain update() phase 3, refCountNoteOn repart de 0→1
// et MIDI noteOn est ré-émis → panic inefficace. Engine self-stop ferme l'invariant
// "flush = stop" par construction.
// Callers cross-check (tous neutres ou voulus) :
//   - stopRecording   : flush → STOPPED, puis startPlayback → PLAYING. Net = PLAYING.
//   - longPressClear  : flush → STOPPED, puis _state = EMPTY en fin. Net = EMPTY.
//   - stopPlayback(_,true) : flush → STOPPED, puis set STOPPED. Net = STOPPED (idempotent).
//   - midiPanic       : flush → STOPPED. Net = STOPPED (cible voulue, plus de resume audio).
// =================================================================
void LoopEngine::flushPendingNoteOffs(MidiTransport& transport) {
  // Phase 1 : cancel pending noteOff queue
  for (uint8_t i = 0; i < MAX_LOOP_PENDING_NOTEOFFS; i++) {
    _pendingNoteOffs[i].active = false;
  }
  // Phase 2 : sweep refcount — any note > 0 gets a hard noteOff
  for (uint8_t n = 0; n < 128; n++) {
    if (_noteRefCount[n] > 0) {
      transport.sendNoteOn(_channel, n, 0);
      _noteRefCount[n] = 0;
    }
  }
  // Phase 3 (audit-fix B2) : engine self-stop. Voir block comment ci-dessus.
  _state = LoopState::STOPPED;
}
bool LoopEngine::mergeOverdub() { return true; }
void LoopEngine::abandonOverdub() {}
uint32_t LoopEngine::computeNextBoundaryTick(LoopQuantize) const { return 0; }
void LoopEngine::commitWaitingAction(MidiTransport&, uint32_t) {}   // B3 : signature étendue
