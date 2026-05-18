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
}

// =================================================================
// tapPlayStop — transport action, state machine dispatch (spec §9 §17)
// currentKeys = keyIsPressed (FG context) or nullptr (BG context).
// Param non utilisé actuellement (réservé), peut être ignoré par l'impl.
// =================================================================
void LoopEngine::tapPlayStop(MidiTransport& transport, const uint8_t* /*currentKeys*/) {
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
}

// =================================================================
// longPressClear — destructive wipe (spec §9). Called once threshold fired.
// Refused during RECORDING/OVERDUBBING (caller filters via isLocked()).
// B2 fix : set _clearFired=true à la fin pour bloquer re-fires tant que CLEAR tenu.
// =================================================================
void LoopEngine::longPressClear(MidiTransport& transport) {
  if (isLocked()) return;  // safety net (caller should not invoke when locked)
  // Wipe buffer + flush MIDI notes + state → EMPTY
  flushPendingNoteOffs(transport);
  _eventCount = 0;
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) _events[i].active = false;
  _overdubCount = 0;
  _loopDurationUs = 0;
  _loopBars = 0;
  _state = LoopState::EMPTY;
  // B2 fix : armé true. isClearHoldFired retourne false jusqu'au release CLEAR
  // ou nouveau press (notifyClearPressStart / End reset _clearFired).
  _clearFired = true;
}
void LoopEngine::capturePadEvent(uint8_t, uint8_t, MidiTransport&) {}
void LoopEngine::update(MidiTransport&) {}
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
bool LoopEngine::insertEventSorted(LoopEvent*, uint16_t&, uint16_t, uint32_t, uint8_t, uint8_t, uint8_t) { return false; }
void LoopEngine::startRecording(MidiTransport&) {}
void LoopEngine::stopRecording(MidiTransport&) {}
void LoopEngine::flushHeldPadsAsNoteOffs(uint32_t) {}
void LoopEngine::startPlayback(MidiTransport&, uint32_t) {}   // B3 : signature étendue avec nowUs
void LoopEngine::stopPlayback(MidiTransport&, bool) {}
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
