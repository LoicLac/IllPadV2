#include "LoopEngine.h"
#include "../core/MidiTransport.h"
#include "../midi/ClockManager.h"
#include "../core/HardwareConfig.h"
#include "../viewer/ViewerSerial.h"   // viewer::emitLoopBufferFull (m9 audit)
#include <Arduino.h>
#include <string.h>

// =================================================================
// OD-Sync swap temp buffer (spec Illpad_OD_Sync.md §6.2, B1 fix post-review)
// =================================================================
// Static global shared par tous les LoopEngine plutôt que stack alloc.
// Default Arduino-ESP32 main loop stack = 8 KB ; un LoopEvent temp[1024] (8 KB)
// sur stack provoquerait overflow. Invariant 11 (≤ 1 LOOP en REC/OD à instant t)
// + execution single-threaded main loop garantissent qu'un seul swap est actif
// à la fois, donc le static global est safe single-usage.
// Coût : +8 KB SRAM permanent (vs +8 KB par engine si static membre).
// =================================================================
static LoopEvent g_swapTemp[MAX_LOOP_EVENTS];

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
  , _playStartUs(0)
  , _scaledElapsedUs(0)        // B1: cumulative scaled position (uint64)
  , _lastUpdateUs(0)            // B1: previous update() tick timestamp
  , _playPositionUs(0)
  , _playNextEventIdx(0)
  , _lastBarIndex(0)
  , _barFlash(false)
  , _wrapFlash(false)
  , _waitingExit(WaitingExit::NONE)
  , _waitingTargetTick(0)
  , _clearPressStartMs(0)
  , _clearFired(false)
  , _recordingPendingClose(false)
  , _recordingPendingCloseTick(0)
  , _eventsAlternateCount(0)
  , _alternateValid(false)
{
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) {
    _events[i].active = false;
  }
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) {
    _eventsAlternate[i].active = false;
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

WaitingExit LoopEngine::consumeWaitingExit() {
  WaitingExit v = _waitingExit;
  _waitingExit = WaitingExit::NONE;
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
      // Master Sync (spec §3.2) : Auto-Stop dispatch selon quantize.
      // FREE → close immédiat tap-to-tap (closeRecordingImmediate).
      // BEAT/BAR → arme PENDING_CLOSE, capture continue jusqu'au boundary,
      //            update() phase 0 commitera via commitRecordingClose.
      if (_quantize == LOOP_QUANT_FREE) {
        closeRecordingImmediate(transport);
      } else {
        // BS-9 : 2e tap REC pendant PENDING_CLOSE déjà actif → ignoré (commit ferme).
        if (_recordingPendingClose) break;
        _recordingPendingCloseTick = computeNextBoundaryTick(_quantize);
        _recordingPendingClose = true;
      }
      break;
    case LoopState::PLAYING:
      // OD-Sync (spec Illpad_OD_Sync.md §3.1) : snapshot _events → _eventsAlternate.
      // Sera utilisé pour Cancel (CLEAR pendant OD) ou Undo/Redo (CLEAR court post-exit).
      memcpy(_eventsAlternate, _events, sizeof(_events));
      _eventsAlternateCount = _eventCount;
      _alternateValid = true;
      _state = LoopState::OVERDUBBING;
      break;
    case LoopState::OVERDUBBING:
      // OD-Sync (spec §3.3) : exit commit. Held pads inject (B-N2 réincarnée).
      // _eventsAlternate préservé pour post-Undo via CLEAR court PLAYING.
      commitOverdubExit(transport);
      break;
    case LoopState::STOPPED:
      // Q5 §28 : tap REC on STOPPED-loaded → PLAYING + OVERDUBBING simultaneously
      // OD-Sync : snapshot avant startPlayback (capture l'état figé pré-reprise).
      memcpy(_eventsAlternate, _events, sizeof(_events));
      _eventsAlternateCount = _eventCount;
      _alternateValid = true;
      startPlayback(transport, micros());   // B3 : nowUs capturé localement
      _state = LoopState::OVERDUBBING;
      break;
    case LoopState::WAITING_PLAY:
      // Spec §17 : REC during WAITING_PLAY ignored (REC senseless on STOPPED-pending-play)
      break;
    case LoopState::WAITING_STOP:
      // Spec §17 : REC during WAITING_STOP cancels stop and enters OVERDUBBING.
      // M1 fix : pas de double-assignment _state ici, directement OVERDUBBING.
      // OD-Sync (B2 fix post-review) : snapshot identique aux autres entry points.
      // Sans ça : _eventsAlternate stale → Undo/Redo après ce chemin = undefined.
      memcpy(_eventsAlternate, _events, sizeof(_events));
      _eventsAlternateCount = _eventCount;
      _alternateValid = true;
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
      // OD-Sync (décision OD-3 spec §3, §5) : tap PLAY/STOP pendant OD est no-op.
      // L'abandon se fait désormais via tap CLEAR (cancelOverdub, cf C3).
      // Cf BS-6 analogue Master Sync PENDING_CLOSE no-op.
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
  _loopDurationUs = 0;
  _loopBars = 0;
  _state = LoopState::EMPTY;
  // OD-Sync (spec §4.2) : wipe reset aussi _eventsAlternate + invalide.
  // Snapshot précédent (s'il y en avait un) perdu. _alternateValid = false
  // garantit que swapForUndoRedo retournera early sans toucher au buffer.
  _eventsAlternateCount = 0;
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) _eventsAlternate[i].active = false;
  _alternateValid = false;
  #if DEBUG_SERIAL
  Serial.printf("[LOOP] longPressClear -> state=%u (EMPTY)\n", (unsigned)_state);
  #endif
}
// =================================================================
// capturePadEvent — décisions actées post-audit :
//   M3 (Q8) : velocity stocké = baseVelocity STRICT (passé par processLoopMode).
//             Variation appliquée uniquement au playback dans update().
//   M2 (Q4) : insertion live-sorted dans _events[] (RECORDING ET OVERDUBBING
//             post OD-Sync immediate-merge). Buffer toujours trié.
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
  //   - commitRecordingClose / closeRecordingImmediate flushHeldPadsAsNoteOffs (fin de loop, Master Sync §3.2 / §3.3).
  //   - commitOverdubExit (B-N2 réincarnée OD-Sync §3.3 : inject noteOff à _playPositionUs).
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
    // First-press latches recordStart + recordBpm (invariant §23.5).
    // Master Sync (spec Illpad_Master_Sync.md §3.1) : _recordStartUs ancré
    // selon quantize sur la grille master tick. Effet : event[0] reçoit
    // timestamp = phase Δ du hit dans son beat/bar courant. Loop wraps
    // tomberont sur master ticks (cf §4.4 démo BPM scaling cohérente).
    if (!_recordFirstPressDone && isNoteOn) {
      if (_quantize == LOOP_QUANT_FREE || !_clock) {
        _recordStartUs = nowUs;                              // tap-to-tap, hors grille
      } else if (_quantize == LOOP_QUANT_BEAT) {
        _recordStartUs = _clock->getLastBeatWallTimeUs();    // anchor master beat
      } else {  // LOOP_QUANT_BAR
        _recordStartUs = _clock->getLastBarWallTimeUs();     // anchor master bar
      }
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
    // OD-Sync (spec Illpad_OD_Sync.md §3.2) : immediate-merge.
    // Insert direct dans _events[] (pas de buffer temp). L'event sera audible
    // au prochain wrap → "live loop growth" caractéristique du modèle.
    uint32_t posInLoop = _playPositionUs;

    bool ok = insertEventSorted(_events, _eventCount, MAX_LOOP_EVENTS,
                                 posInLoop, padIndex, midiNote, velocity);
    if (!ok) {
      // m9 telemetry main buffer full.
      viewer::emitLoopBufferFull(_channel, "main");
      return;
    }

    // _playNextEventIdx adjustment : binary search depuis _playPositionUs.
    // Le M8 live monitor a déjà émis le noteOn live ; on ne veut pas que le
    // walk fire loop re-déclenche cet event ce cycle. Le binary search avance
    // _playNextEventIdx APRÈS l'event inséré (premier idx avec timestamp > pos).
    int32_t lo = 0;
    int32_t hi = (int32_t)_eventCount;
    while (lo < hi) {
      int32_t mid = (lo + hi) / 2;
      if (_events[mid].timestampUs <= _playPositionUs) lo = mid + 1;
      else hi = mid;
    }
    _playNextEventIdx = (uint16_t)lo;
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

  // (0) Master Sync : commit PENDING_CLOSE au boundary tick (spec Illpad_Master_Sync.md §3.2).
  // RECORDING + flag pending → close + startPlayback au tick master boundary.
  // Doit s'exécuter avant phase (1) pour que la transition vers PLAYING soit
  // visible avant tout WAITING_* check (cohérence état machine).
  if (_state == LoopState::RECORDING && _recordingPendingClose) {
    if (_clock && _clock->getCurrentTick() >= _recordingPendingCloseTick) {
      commitRecordingClose(transport);
      // commitRecordingClose set _state = PLAYING via startPlayback.
      // _lastUpdateUs ancré au boundary wall time. Phase (3) ci-dessous
      // tournera avec deltaUs = nowUs - boundaryWallTime, position avance
      // normalement à partir du boundary.
    }
  }

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
// commitRecordingClose — Master Sync Auto-Stop boundary commit
//                       (spec Illpad_Master_Sync.md §3.2)
// =================================================================
// Appelé par update() phase 0 quand _recordingPendingCloseTick atteint.
// Calcule loopDur depuis le master tick boundary (multiple entier de
// quantize unit par construction), flush held pads, startPlayback ancré
// au boundary wall time exact (→ position 0 du loop = master tick).
// =================================================================
void LoopEngine::commitRecordingClose(MidiTransport& transport) {
  if (!_clock) {
    // Safety fallback : pas de clock → close tap-to-tap (path FREE).
    closeRecordingImmediate(transport);
    return;
  }

  if (!_recordFirstPressDone || _eventCount == 0) {
    // Buffer vide (cas pathologique : PENDING_CLOSE armé sans 1er pad press)
    // → revert EMPTY, reset flag pending.
    _recordingPendingClose = false;
    _recordingPendingCloseTick = 0;
    _state = LoopState::EMPTY;
    return;
  }

  // 1. Calculer le wall time exact du boundary tick (spec §3.4.1 catch-up).
  uint32_t currentTick = _clock->getCurrentTick();
  uint32_t diff = currentTick - _recordingPendingCloseTick;   // 0 si pile, > 0 si catch-up tick
  float    tickInterval = _clock->getTickIntervalUs();
  uint32_t boundaryWallTime = _clock->getLastTickWallTimeUs()
                              - (uint32_t)((float)diff * tickInterval);

  // 2. Loop length = boundary - anchor.
  // Par construction multiple entier de quantize unit ticks au recordBpm.
  _loopDurationUs = boundaryWallTime - _recordStartUs;

  // _loopBars : nombre d'unités quantize (beats pour BEAT, bars pour BAR).
  // Conservé pour bar-crossing detection dans update() phase 3.
  uint32_t snapUnitTicks = (_quantize == LOOP_QUANT_BAR) ? TICKS_PER_BAR : TICKS_PER_BEAT;
  uint32_t snapUnitUs = (uint32_t)((float)snapUnitTicks * tickInterval);
  _loopBars = (snapUnitUs > 0) ? (uint16_t)(_loopDurationUs / snapUnitUs) : 1;
  if (_loopBars < 1) _loopBars = 1;

  // 3. Flush held pads → noteOff inject à _loopDurationUs - 1.
  if (_loopDurationUs > 0) {
    flushHeldPadsAsNoteOffs(_loopDurationUs - 1);
  }

  // 4. M6 clamp defense in depth : tout event >= _loopDurationUs ramené.
  uint16_t clampedCount = 0;
  for (uint16_t i = 0; i < _eventCount; i++) {
    if (_events[i].timestampUs >= _loopDurationUs) {
      _events[i].timestampUs = _loopDurationUs > 0 ? _loopDurationUs - 1 : 0;
      clampedCount++;
    }
  }
  #if DEBUG_SERIAL
  if (clampedCount > 0) {
    Serial.printf("[LOOP WARN] commitRecordingClose clamped %u events to loopDur=%lu us\n",
                  clampedCount, (unsigned long)_loopDurationUs);
  }
  #endif

  // 5. Reset pending close flag.
  _recordingPendingClose = false;
  _recordingPendingCloseTick = 0;

  // 6. Flush refcount + startPlayback ancré au boundary wall time exact.
  flushPendingNoteOffs(transport);
  startPlayback(transport, boundaryWallTime);

  #if DEBUG_SERIAL
  Serial.printf("[LOOP] commitRecordingClose ch=%u loopDur=%lu us bars=%u events=%u\n",
                _channel, (unsigned long)_loopDurationUs, _loopBars, _eventCount);
  #endif
}

// =================================================================
// closeRecordingImmediate — Master Sync FREE path
//                          (spec Illpad_Master_Sync.md §3.3)
// =================================================================
// Path FREE strict tap-to-tap : pas de boundary, pas de PENDING_CLOSE.
// loopDur = rawDur exact, events conservés tels quels. Aucun snap, aucun
// rescale. startPlayback immédiat ancré sur nowUs (hors grille master).
// Aussi safety fallback de commitRecordingClose si !_clock.
// =================================================================
void LoopEngine::closeRecordingImmediate(MidiTransport& transport) {
  uint32_t nowUs = micros();
  _recordEndUs = nowUs;

  if (!_recordFirstPressDone || _eventCount == 0) {
    // No content — revert EMPTY.
    _state = LoopState::EMPTY;
    return;
  }

  // 1. Loop length = rawDur exact (pas de snap, pas de rescale).
  _loopDurationUs = nowUs - _recordStartUs;
  _loopBars = 1;   // sémantique FREE : pas de bar logique, structure-only.

  // 2. Flush held pads → noteOff inject à _loopDurationUs - 1.
  if (_loopDurationUs > 0) {
    flushHeldPadsAsNoteOffs(_loopDurationUs - 1);
  }

  // 3. M6 clamp defense in depth.
  for (uint16_t i = 0; i < _eventCount; i++) {
    if (_events[i].timestampUs >= _loopDurationUs) {
      _events[i].timestampUs = _loopDurationUs > 0 ? _loopDurationUs - 1 : 0;
    }
  }

  // 4. Flush refcount + startPlayback. Pas d'ancrage master tick (FREE = hors grille).
  flushPendingNoteOffs(transport);
  startPlayback(transport, micros());

  #if DEBUG_SERIAL
  Serial.printf("[LOOP] closeRecordingImmediate (FREE) ch=%u loopDur=%lu us events=%u\n",
                _channel, (unsigned long)_loopDurationUs, _eventCount);
  #endif
}
// =================================================================
// flushHeldPadsAsNoteOffs — inject noteOff dans main buffer pour pads encore tenus
// =================================================================
// Appelé par commitRecordingClose (BEAT/BAR) et closeRecordingImmediate (FREE)
// à la fermeture du recording, avec timestamp = _loopDurationUs - 1.
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
// onBackgroundTransition — appelé au bank switch quand cette bank passe FG → BG
// =================================================================
// Audit-fix B-N1 / R-N1 (audit adversarial 2026-05-18) :
//   B-N1 : pads physiquement tenus (live press refCount > 0) ne reçoivent
//          jamais de noteOff naturel parce que processLoopMode n'est plus appelé
//          pour cette bank en BG. Stuck note au DAW. Solution : refCountNoteOff
//          direct pour chaque pad du tracker _padHeldLive[].
//   R-N1 : _clearPressStartMs stale (figé à la valeur du moment où la bank était
//          FG). Au retour FG, isClearHoldFired retourne true instantanément si
//          assez de temps a passé en BG → wipe sans seuil 500 ms. Solution :
//          notifyClearPressEnd() reset le tracker (et _clearFired).
// IMPORTANT : ne PAS changer `_state`. La bank LOOP en BG doit pouvoir continuer
// à jouer son buffer normalement (spec §14 multi-bank LOOP).
// =================================================================
void LoopEngine::onBackgroundTransition(MidiTransport& transport) {
  // Phase 1 (B-N1) : flush live press refCount pour chaque pad encore physiquement tenu.
  // refCountNoteOff décrémente uniquement la contribution live press ; si le buffer
  // playback avait aussi incrémenté la même note (refCount = 2), le noteOff MIDI
  // ne fire pas ici (1→1, return early sur 0→0), mais le buffer fire naturellement
  // son noteOff matching plus tard → MIDI noteOff cohérent au DAW.
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    if (_padHeldLive[pad]) {
      refCountNoteOff(transport, resolvePadToMidiNote(pad));
      _padHeldLive[pad] = 0;
    }
  }
  // Phase 2 (R-N1) : reset CLEAR press tracker. Au retour FG, notifyClearPressStart
  // détectera la nouvelle frame avec _clearPressStartMs == 0 et armera le timer
  // fresh (500ms à attendre depuis le retour, pas depuis le press d'origine).
  notifyClearPressEnd();
}

// =================================================================
// flushPendingNoteOffs — emergency silence + state → STOPPED.
// Symetric ArpEngine::flushPendingNoteOffs (ArpEngine.cpp:864 fait `_playing = false`).
// Audit-fix B2 : sans la transition d'état, midiPanic() laisse les LOOP banks en
// PLAYING/OVERDUBBING ; au prochain update() phase 3, refCountNoteOn repart de 0→1
// et MIDI noteOn est ré-émis → panic inefficace. Engine self-stop ferme l'invariant
// "flush = stop" par construction.
// Callers cross-check (tous neutres ou voulus) :
//   - commitRecordingClose / closeRecordingImmediate : flush → STOPPED, puis startPlayback → PLAYING. Net = PLAYING.
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
// =================================================================
// commitOverdubExit — OD-Sync exit commit (spec Illpad_OD_Sync.md §3.3)
// =================================================================
// Appelé par tapRec sur OVERDUBBING. Held pads injection (B-N2 réincarnée de
// l'ancienne mergeOverdub supprimée par OD-Sync C2) : pour chaque pad encore
// tenu physiquement, inject noteOff dans _events à _playPositionUs.
// Sans ça : noteOn capturé pendant OD sans noteOff matching → refcount
// grows unbounded au wrap si user release après exit OD.
// État → PLAYING. _eventsAlternate intact (pour post-Undo via CLEAR court).
// =================================================================
void LoopEngine::commitOverdubExit(MidiTransport& transport) {
  (void)transport;  // pas de MIDI émis ici, juste buffer manipulation + state

  // (1) Held pads inject (B-N2 logic, déplacée de mergeOverdub).
  // IMPORTANT : ne PAS reset _padHeldLive ici. Pad encore physiquement tenu —
  // le release naturel (capturePadEvent en PLAYING) appellera refCountNoteOff.
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    if (!_padHeldLive[pad]) continue;
    bool ok = insertEventSorted(_events, _eventCount, MAX_LOOP_EVENTS,
                                 _playPositionUs, pad, resolvePadToMidiNote(pad), /*velocity=*/0);
    if (!ok) {
      // B4 fix post-review : préserver télémétrie comme mergeOverdub actuel.
      // Live press refcount silencera la note au release naturel via M8 refCountNoteOff.
      viewer::emitLoopBufferFull(_channel, "od_exit_flush");
    }
  }

  // (2) Recompute _playNextEventIdx après les injects (binary search depuis _playPositionUs).
  int32_t lo = 0;
  int32_t hi = (int32_t)_eventCount;
  while (lo < hi) {
    int32_t mid = (lo + hi) / 2;
    if (_events[mid].timestampUs <= _playPositionUs) lo = mid + 1;
    else hi = mid;
  }
  _playNextEventIdx = (uint16_t)lo;

  _state = LoopState::PLAYING;

  #if DEBUG_SERIAL
  Serial.printf("[LOOP] commitOverdubExit ch=%u eventCount=%u alternateCount=%u\n",
                _channel, _eventCount, _eventsAlternateCount);
  #endif
}
// =================================================================
// computeNextBoundaryTick — return next tick that matches the quantize boundary
// FREE : 0 (caller checks for FREE before this call)
// BEAT : next multiple of 24 ticks
// BAR  : next multiple of 96 ticks
// =================================================================
uint32_t LoopEngine::computeNextBoundaryTick(LoopQuantize q) const {
  if (!_clock) return 0;
  uint32_t now = _clock->getCurrentTick();
  uint32_t mod = (q == LOOP_QUANT_BAR) ? TICKS_PER_BAR : TICKS_PER_BEAT;
  uint32_t boundary = ((now / mod) + 1) * mod;
  return boundary;
}

// =================================================================
// commitWaitingAction — called by update() when waitingTargetTick reached
// B3 audit fix : signature étendue (transport, nowUs) pour propager le timestamp
// de update() à startPlayback (évite underflow uint32 sur enchaînement même tick).
// =================================================================
void LoopEngine::commitWaitingAction(MidiTransport& transport, uint32_t nowUs) {
  if (_state == LoopState::WAITING_PLAY) {
    startPlayback(transport, nowUs);  // → PLAYING (B3 : nowUs propagé)
    _waitingExit = WaitingExit::TO_PLAY;  // signal symétrique de EVT_WAITING (clear overlay)
  } else if (_state == LoopState::WAITING_STOP) {
    stopPlayback(transport, /*flushNotes=*/true);  // → STOPPED + flush
    _waitingExit = WaitingExit::TO_STOP;
  }
}

// =================================================================
// OD-Sync helpers (spec Illpad_OD_Sync.md §6.3)
// =================================================================
// isNoteOnAt — état "on" d'une note à position pos dans un buffer trié.
// Walk les events ≤ pos en suivant les transitions noteOn (vel > 0) /
// noteOff (vel == 0). Le dernier event matching détermine l'état audible.
// Coût : O(count). Utilisé au swap (Cancel / Undo / Redo), geste rare.
// =================================================================
bool LoopEngine::isNoteOnAt(LoopEvent* buf, uint16_t count, uint8_t note, uint32_t pos) const {
  bool on = false;
  for (uint16_t i = 0; i < count; i++) {
    if (!buf[i].active) break;
    if (buf[i].timestampUs > pos) break;       // events triés, on dépasse
    if (buf[i].midiNote != note) continue;
    on = (buf[i].velocity > 0);                // dernier event matching avant pos
  }
  return on;
}

// =================================================================
// findLatestVelAt — velocity du dernier noteOn matching note ≤ pos.
// Fallback DEFAULT_BASE_VELOCITY si aucun event matching trouvé.
// =================================================================
uint8_t LoopEngine::findLatestVelAt(LoopEvent* buf, uint16_t count, uint8_t note, uint32_t pos) const {
  uint8_t vel = DEFAULT_BASE_VELOCITY;
  for (uint16_t i = 0; i < count; i++) {
    if (!buf[i].active) break;
    if (buf[i].timestampUs > pos) break;
    if (buf[i].midiNote != note) continue;
    if (buf[i].velocity > 0) vel = buf[i].velocity;
  }
  return vel;
}

// =================================================================
// swapForUndoRedo — OD-Sync diff swap musical (spec Illpad_OD_Sync.md §6.2)
// =================================================================
// Échange _events ↔ _eventsAlternate avec MIDI ciblé : seules les notes dont
// l'état audible change firent/coupent. Couche base et live press préservées
// par construction (cf §6.4 scénario K+SN+HH).
//
// Appelé par cancelOverdub (CLEAR pendant OD, rising edge) et par
// processLoopMode CLEAR falling-edge-short-tap (PLAYING/STOPPED).
// =================================================================
void LoopEngine::swapForUndoRedo(MidiTransport& transport) {
  if (!_alternateValid) return;   // pas de snapshot, no-op safe

  // Étape 1 : compute states "before" (dans _events) et "after" (dans _eventsAlternate).
  bool before[128], after[128];
  for (uint8_t n = 0; n < 128; n++) {
    before[n] = isNoteOnAt(_events,          _eventCount,          n, _playPositionUs);
    after[n]  = isNoteOnAt(_eventsAlternate, _eventsAlternateCount, n, _playPositionUs);
  }

  // Étape 2 : MIDI ciblé pour notes où l'état change, sauf si live press tient.
  for (uint8_t n = 0; n < 128; n++) {
    if (before[n] == after[n]) continue;   // pas de change → couche base préservée

    // Live press protection : check si un pad mappant vers cette note est tenu.
    bool liveOn = false;
    for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
      if (_padHeldLive[pad] && resolvePadToMidiNote(pad) == n) {
        liveOn = true;
        break;
      }
    }

    if (before[n] && !after[n]) {
      // Note disparait après swap. NoteOff seulement si pas live-press.
      if (!liveOn) transport.sendNoteOn(_channel, n, 0);   // vel 0 = noteOff
    } else {
      // !before && after : note apparait après swap.
      if (!liveOn) {
        uint8_t vel = findLatestVelAt(_eventsAlternate, _eventsAlternateCount, n, _playPositionUs);
        transport.sendNoteOn(_channel, n, vel);
      }
    }
  }

  // Étape 3 : swap buffers via g_swapTemp static global (B1 fix : pas stack 8 KB).
  memcpy(g_swapTemp,        _events,          sizeof(_events));
  memcpy(_events,           _eventsAlternate, sizeof(_eventsAlternate));
  memcpy(_eventsAlternate,  g_swapTemp,       sizeof(g_swapTemp));
  uint16_t tempCount = _eventCount;
  _eventCount = _eventsAlternateCount;
  _eventsAlternateCount = tempCount;

  // Étape 4 : recompute _noteRefCount depuis nouveau _events à _playPositionUs + live press.
  // after[] représente le nouvel état attendu (post-swap).
  memset(_noteRefCount, 0, sizeof(_noteRefCount));
  for (uint8_t n = 0; n < 128; n++) {
    if (after[n]) _noteRefCount[n] = 1;   // buffer attendu on
  }
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    if (_padHeldLive[pad]) {
      uint8_t note = resolvePadToMidiNote(pad);
      if (_noteRefCount[note] < 255) _noteRefCount[note]++;   // live press contribution
    }
  }

  // Étape 5 : recompute _playNextEventIdx par binary search (events à fire au reste du cycle).
  int32_t lo = 0;
  int32_t hi = (int32_t)_eventCount;
  while (lo < hi) {
    int32_t mid = (lo + hi) / 2;
    if (_events[mid].timestampUs <= _playPositionUs) lo = mid + 1;
    else hi = mid;
  }
  _playNextEventIdx = (uint16_t)lo;

  #if DEBUG_SERIAL
  Serial.printf("[LOOP] swapForUndoRedo ch=%u eventCount=%u alternateCount=%u\n",
                _channel, _eventCount, _eventsAlternateCount);
  #endif
}

// =================================================================
// cancelOverdub — OD-Sync Cancel pendant OD (spec Illpad_OD_Sync.md §3.4)
// =================================================================
// Rising edge CLEAR pendant OVERDUBBING : swap diff musical + state → PLAYING.
// _alternateValid reste true (Redo possible juste après le retour PLAYING).
// Décision OD-16 : toujours retour à PLAYING, même si pré-OD state était
// STOPPED (chemin Q5). Asymétrie acceptée.
// =================================================================
void LoopEngine::cancelOverdub(MidiTransport& transport) {
  if (_state != LoopState::OVERDUBBING) return;   // safety
  swapForUndoRedo(transport);
  _state = LoopState::PLAYING;   // OD-16 : toujours PLAYING (pas de tracker pre-OD state)
  #if DEBUG_SERIAL
  Serial.printf("[LOOP] cancelOverdub ch=%u → PLAYING\n", _channel);
  #endif
}
