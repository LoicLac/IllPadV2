#include "ArpEngine.h"
#include "../core/MidiTransport.h"
#include "../midi/ScaleResolver.h"
#include <Arduino.h>

// Shuffle templates now in midi/GrooveTemplates.h (shared with LoopEngine)

// =================================================================
// Constructor
// =================================================================
ArpEngine::ArpEngine()
  : _channel(0), _pattern(ARP_UP), _octaveRange(1), _division(DIV_1_8),
    _gateLength(0.5f), _shuffleDepth(0.0f), _shuffleTemplate(0),
    _baseVelocity(100), _velocityVariation(0),
    _positionCount(0), _orderCount(0),
    _sequenceLen(0), _sequenceDirty(false),
    _scale{true, 2, 0},  // chromatic, root C, Ionian
    _padOrder(nullptr),
    _stepIndex(-1), _playing(false), _captured(false), _pausedPile(false),
    _shuffleStepCounter(0), _tickFlash(false),
    _waitingForQuantize(false), _quantizeMode(ARP_START_IMMEDIATE),
    _lastDispatchedGlobalTick(0xFFFFFFFF),
    _engineMode(EngineMode::CLASSIC), _sequenceGenDirty(false),
    _bonusPilex10(15), _marginWalk(7) {
  // Init pile arrays
  for (uint8_t i = 0; i < MAX_ARP_NOTES; i++) {
    _positions[i] = 0xFF;
    _positionOrder[i] = 0xFF;
  }
  for (uint16_t i = 0; i < MAX_ARP_SEQUENCE; i++) {
    _sequence[i] = 0xFF;
  }
  // Init event queue — all slots inactive
  for (uint8_t i = 0; i < MAX_PENDING_EVENTS; i++) {
    _events[i].active = false;
  }
  // Init reference counts — no notes ringing
  memset(_noteRefCount, 0, sizeof(_noteRefCount));
}

// =================================================================
// Configuration setters
// =================================================================
// Guards prevent unnecessary dirty flags and shuffle resets when
// main.cpp pushes the same value every loop iteration.

void ArpEngine::setChannel(uint8_t ch) { _channel = ch; }

void ArpEngine::setPattern(ArpPattern pattern) {
  if (_pattern != pattern) {
    _pattern = pattern;
    _sequenceDirty = true;
    _shuffleStepCounter = 0;  // Groove restarts on pattern change
  }
}

void ArpEngine::setOctaveRange(uint8_t range) {
  if (range < 1) range = 1;
  if (range > 4) range = 4;
  if (_octaveRange != range) {
    _octaveRange = range;
    _sequenceDirty = true;
  }
}

void ArpEngine::setDivision(ArpDivision div)         { _division = div; }
void ArpEngine::setGateLength(float gate)             { _gateLength = gate; }
void ArpEngine::setShuffleDepth(float depth)          { _shuffleDepth = depth; }
void ArpEngine::setShuffleTemplate(uint8_t tmpl)      { if (tmpl < NUM_SHUFFLE_TEMPLATES) _shuffleTemplate = tmpl; }
void ArpEngine::setBaseVelocity(uint8_t vel)          { _baseVelocity = vel; }
void ArpEngine::setVelocityVariation(uint8_t pct)     { _velocityVariation = pct; }
void ArpEngine::setStartMode(uint8_t mode) {
  _quantizeMode = (mode < NUM_ARP_START_MODES) ? mode : ARP_START_IMMEDIATE;
  if (_quantizeMode == ARP_START_IMMEDIATE) _waitingForQuantize = false;
}

// --- Engine mode ---
// Boot path : derives mode from BankType (ARPEG -> CLASSIC, ARPEG_GEN -> GENERATIVE).
// CLASSIC->GENERATIVE transition flags _sequenceGenDirty so Phase 5 Task 10 forces a regen on next seed.
void ArpEngine::setEngineMode(BankType type) {
  EngineMode newMode = (type == BANK_ARPEG_GEN) ? EngineMode::GENERATIVE : EngineMode::CLASSIC;
  if (_engineMode == EngineMode::CLASSIC && newMode == EngineMode::GENERATIVE) {
    _sequenceGenDirty = true;
  }
  _engineMode = newMode;
}

EngineMode ArpEngine::getEngineMode() const { return _engineMode; }

// --- ARPEG_GEN per-bank params ---
// Phase 4 stubs : just store. Phase 5 Tasks 9-11 consume in walk + mutation logic.
void ArpEngine::setBonusPile(uint8_t x10) {
  if (x10 >= 10 && x10 <= 20) _bonusPilex10 = x10;
}

void ArpEngine::setMarginWalk(uint8_t margin) {
  if (margin >= 3 && margin <= 12) _marginWalk = margin;
}

// =================================================================
// Pile management — padOrder positions, NOT MIDI notes
// =================================================================

void ArpEngine::addPadPosition(uint8_t padOrderPos) {
  // Detect pile 0->1 transition for shuffle reset
  bool wasEmpty = (_positionCount == 0);

  // Ignore if already present
  for (uint8_t i = 0; i < _positionCount; i++) {
    if (_positions[i] == padOrderPos) return;
  }
  if (_positionCount >= MAX_ARP_NOTES) return;

  // Insert sorted in _positions[]
  uint8_t insertAt = _positionCount;
  for (uint8_t i = 0; i < _positionCount; i++) {
    if (padOrderPos < _positions[i]) { insertAt = i; break; }
  }
  for (uint8_t i = _positionCount; i > insertAt; i--) {
    _positions[i] = _positions[i - 1];
  }
  _positions[insertAt] = padOrderPos;
  _positionCount++;

  // Append to chronological order
  _positionOrder[_orderCount++] = padOrderPos;

  _sequenceDirty = true;

  // Start or resume playback.
  //  - wasEmpty: fresh 0->1 transition (any mode)
  //  - !_playing: resume from pause or post-panic
  if (wasEmpty || !_playing) {
    if (wasEmpty) _shuffleStepCounter = 0;
    _playing = true;
    _pausedPile = false;
    if (_quantizeMode != ARP_START_IMMEDIATE) {
      _waitingForQuantize = true;
      _lastDispatchedGlobalTick = 0xFFFFFFFF;
    }
  }

  #if DEBUG_SERIAL
  Serial.printf("[ARP] Bank %d: +note (%d total)\n", _channel + 1, _positionCount);
  #endif
}

void ArpEngine::removePadPosition(uint8_t padOrderPos) {
  // Remove from sorted _positions[]
  bool found = false;
  for (uint8_t i = 0; i < _positionCount; i++) {
    if (_positions[i] == padOrderPos) found = true;
    if (found && i + 1 < _positionCount) _positions[i] = _positions[i + 1];
  }
  if (found) {
    _positionCount--;
    _positions[_positionCount] = 0xFF;
  }

  // Remove from _positionOrder[]
  found = false;
  for (uint8_t i = 0; i < _orderCount; i++) {
    if (_positionOrder[i] == padOrderPos) found = true;
    if (found && i + 1 < _orderCount) _positionOrder[i] = _positionOrder[i + 1];
  }
  if (found) {
    _orderCount--;
    _positionOrder[_orderCount] = 0xFF;
  }

  if (found) _sequenceDirty = true;

  #if DEBUG_SERIAL
  if (found) {
    Serial.printf("[ARP] Bank %d: -note (%d total)\n", _channel + 1, _positionCount);
  }
  #endif
}

void ArpEngine::clearAllNotes(MidiTransport& transport) {
  flushPendingNoteOffs(transport);
  _positionCount = 0;
  _orderCount = 0;
  _sequenceLen = 0;
  _sequenceDirty = false;
  _stepIndex = -1;
  _shuffleStepCounter = 0;
  _playing = false;
  _waitingForQuantize = false;
  _pausedPile = false;
}

// =================================================================
// Scale context
// =================================================================

void ArpEngine::setScaleConfig(const ScaleConfig& scale) {
  _scale = scale;
}

void ArpEngine::setPadOrder(const uint8_t* padOrder) {
  _padOrder = padOrder;
}

// =================================================================
// Capture (OFF/ON switch)
// =================================================================

void ArpEngine::setCaptured(bool captured, MidiTransport& transport,
                             const uint8_t* keyIsPressed, uint8_t holdPadIdx) {
  if (captured == _captured) return;
  _captured = captured;

  if (captured) {
    // Stop → Play: if paused pile has notes, relaunch from step 0.
    if (_pausedPile && _positionCount > 0) {
      _playing = true;
      _stepIndex = -1;
      _shuffleStepCounter = 0;
      _waitingForQuantize = (_quantizeMode != ARP_START_IMMEDIATE);
      if (_waitingForQuantize) _lastDispatchedGlobalTick = 0xFFFFFFFF;
      #if DEBUG_SERIAL
      Serial.printf("[ARP] Bank %d: Play — relaunch paused pile (%d notes)\n", _channel + 1, _positionCount);
      #endif
    } else {
      #if DEBUG_SERIAL
      Serial.printf("[ARP] Bank %d: Play (pile %d notes)\n", _channel + 1, _positionCount);
      #endif
    }
    _pausedPile = false;
  } else {
    // Play → Stop: check fingers down on FG bank only.
    // keyIsPressed == nullptr → BG bank, no fingers possible.
    bool anyFingerDown = false;
    if (keyIsPressed) {
      for (uint8_t i = 0; i < NUM_KEYS; i++) {
        if (i == holdPadIdx) continue;
        if (keyIsPressed[i]) { anyFingerDown = true; break; }
      }
    }

    if (anyFingerDown) {
      // Fingers down: wipe pile, live mode takes over.
      clearAllNotes(transport);
      #if DEBUG_SERIAL
      Serial.printf("[ARP] Bank %d: Stop — fingers down, pile cleared\n", _channel + 1);
      #endif
    } else {
      // No fingers: keep pile, arm pause for next Play.
      flushPendingNoteOffs(transport);
      _playing = false;
      _waitingForQuantize = false;
      _pausedPile = true;
      #if DEBUG_SERIAL
      Serial.printf("[ARP] Bank %d: Stop — pile kept (%d notes)\n", _channel + 1, _positionCount);
      #endif
    }
  }
}

bool ArpEngine::isCaptured() const { return _captured; }
bool ArpEngine::isPlaying() const  { return _playing; }
bool ArpEngine::isPaused() const   { return _pausedPile; }

// =================================================================
// Sequence building
// =================================================================
// _sequence[] encodes padOrderPos + octaveOffset * 48.
// Decode at tick: pos = seq % 48, octOffset = seq / 48.
// Max value = 47 + 3*48 = 191, fits in uint8_t.

void ArpEngine::rebuildSequence() {
  _sequenceDirty = false;
  _sequenceLen = 0;

  if (_positionCount == 0) return;

  // Source: sorted positions (most patterns) or chronological (ORDER)
  const uint8_t* source = (_pattern == ARP_ORDER) ? _positionOrder : _positions;
  uint8_t sourceCount = (_pattern == ARP_ORDER) ? _orderCount : _positionCount;
  uint8_t octaves = _octaveRange;

  switch (_pattern) {

    // ---------------------------------------------------------------
    // GROUP A: Build ascending, then post-process
    // ---------------------------------------------------------------
    case ARP_UP:
    case ARP_DOWN:
    case ARP_UP_DOWN:
    case ARP_ORDER:
    default:
    {
      for (uint8_t oct = 0; oct < octaves; oct++) {
        for (uint8_t i = 0; i < sourceCount; i++) {
          uint8_t encoded = source[i] + oct * 48;
          if (encoded > 191) continue;
          _sequence[_sequenceLen++] = encoded;
          if (_sequenceLen >= MAX_ARP_SEQUENCE) goto done;
        }
      }

      if (_pattern == ARP_DOWN) {
        for (uint8_t i = 0; i < _sequenceLen / 2; i++) {
          uint8_t tmp = _sequence[i];
          _sequence[i] = _sequence[_sequenceLen - 1 - i];
          _sequence[_sequenceLen - 1 - i] = tmp;
        }
      }

      if (_pattern == ARP_UP_DOWN && _sequenceLen > 2) {
        uint8_t upLen = _sequenceLen;
        for (int16_t i = upLen - 2; i >= 1; i--) {
          if (_sequenceLen >= MAX_ARP_SEQUENCE) break;
          _sequence[_sequenceLen++] = _sequence[i];
        }
      }
      break;
    }

    case ARP_CONVERGE:
    {
      uint8_t temp[MAX_ARP_SEQUENCE];
      uint8_t tempLen = 0;
      for (uint8_t oct = 0; oct < octaves; oct++) {
        for (uint8_t i = 0; i < sourceCount; i++) {
          uint8_t encoded = source[i] + oct * 48;
          if (encoded > 191) continue;
          temp[tempLen++] = encoded;
          if (tempLen >= MAX_ARP_SEQUENCE) break;
        }
        if (tempLen >= MAX_ARP_SEQUENCE) break;
      }
      uint8_t lo = 0, hi = (tempLen > 0) ? tempLen - 1 : 0;
      while (lo <= hi && _sequenceLen < MAX_ARP_SEQUENCE) {
        _sequence[_sequenceLen++] = temp[lo];
        if (lo != hi && _sequenceLen < MAX_ARP_SEQUENCE) {
          _sequence[_sequenceLen++] = temp[hi];
        }
        lo++;
        if (hi == 0) break;
        hi--;
      }
      break;
    }

    case ARP_PEDAL_UP:
    {
      uint8_t pedal = source[0];
      for (uint8_t oct = 0; oct < octaves; oct++) {
        for (uint8_t i = 0; i < sourceCount; i++) {
          uint8_t encoded = source[i] + oct * 48;
          if (encoded > 191) continue;
          if (oct == 0 && i == 0) continue;
          if (_sequenceLen + 2 > MAX_ARP_SEQUENCE) goto done;
          _sequence[_sequenceLen++] = pedal;
          _sequence[_sequenceLen++] = encoded;
        }
      }
      if (_sequenceLen == 0 && sourceCount > 0) {
        _sequence[_sequenceLen++] = source[0];
      }
      break;
    }

  } // end switch

  done:
  // Clamp step index to new length
  if (_stepIndex >= (int16_t)_sequenceLen) {
    _stepIndex = -1;
  }
}

// =================================================================
// State classification — read-only, observes flags
// =================================================================

ArpState ArpEngine::currentState() const {
  // _playing is the authoritative engine-running flag.
  // IDLE covers: empty pile, paused pile (ON->OFF no fingers), post-panic.
  if (_positionCount == 0 || !_playing)  return ArpState::IDLE;
  if (_waitingForQuantize)               return ArpState::WAITING_QUANTIZE;
  return ArpState::PLAYING;
}

// =================================================================
// Tick — called by ArpScheduler when a rhythmic step fires
// =================================================================

void ArpEngine::tick(MidiTransport& transport, uint32_t stepDurationUs,
                     uint32_t currentTick, uint32_t globalTick) {
  switch (currentState()) {
    case ArpState::IDLE:
      flushPendingNoteOffs(transport);
      return;

    case ArpState::WAITING_QUANTIZE: {
      uint16_t boundary = 24;  // Beat = 24 ticks (only mode remaining)
      // Sentinel: first tick initializes tracking point, does NOT fire.
      // Subsequent ticks use crossing detection (robust against multi-tick bursts).
      if (_lastDispatchedGlobalTick == 0xFFFFFFFF) {
        _lastDispatchedGlobalTick = globalTick;
        return;
      }
      bool crossed = (globalTick / boundary) > (_lastDispatchedGlobalTick / boundary);
      if (!crossed) {
        _lastDispatchedGlobalTick = globalTick;
        return;
      }
      _waitingForQuantize = false;
      _lastDispatchedGlobalTick = globalTick;
      break;
    }

    case ArpState::PLAYING:
      break;
  }

  executeStep(transport, stepDurationUs);
}

// =================================================================
// executeStep — resolve note, calculate timing, schedule events
// =================================================================

void ArpEngine::executeStep(MidiTransport& transport, uint32_t stepDurationUs) {
  if (_sequenceDirty) rebuildSequence();
  if (_sequenceLen == 0) return;

  _stepIndex++;
  if (_stepIndex >= (int16_t)_sequenceLen) {
    _stepIndex = 0;
  }

  uint8_t encoded = _sequence[_stepIndex];
  uint8_t pos = encoded % 48;
  uint8_t octOffset = encoded / 48;

  if (!_padOrder) return;

  uint8_t padIndex = 0xFF;
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    if (_padOrder[i] == pos) { padIndex = i; break; }
  }
  if (padIndex == 0xFF) return;

  uint8_t midiNote = ScaleResolver::resolve(padIndex, _padOrder, _scale);
  if (midiNote == 0xFF) return;

  uint8_t finalNote = midiNote + octOffset * 12;
  while (finalNote > 127) finalNote -= 12;
  if (finalNote > 127) return;

  uint8_t vel = _baseVelocity;
  if (_velocityVariation > 0) {
    int16_t range = (int16_t)_velocityVariation * 127 / 200;
    int16_t offset = (int16_t)(random(-range, range + 1));
    int16_t result = (int16_t)vel + offset;
    vel = (uint8_t)constrain(result, 1, 127);
  }

  int32_t shuffleOffsetUs = 0;
  if (_shuffleDepth > 0.001f) {
    int8_t pct = SHUFFLE_TEMPLATES[_shuffleTemplate][_shuffleStepCounter % SHUFFLE_TEMPLATE_LEN];
    if (pct != 0) {
      shuffleOffsetUs = (int32_t)((float)pct * _shuffleDepth * (float)stepDurationUs / 100.0f);
    }
  }

  uint32_t nowUs = micros();
  uint32_t noteOnTime = nowUs + (uint32_t)shuffleOffsetUs;

  uint32_t gateUs = (uint32_t)((float)stepDurationUs * _gateLength);
  if (gateUs < 1000) gateUs = 1000;
  uint32_t noteOffTime = noteOnTime + gateUs;

  if (shuffleOffsetUs < 0 && (int32_t)(noteOffTime - nowUs) < 1000) {
    noteOffTime = nowUs + 1000;
  }

  bool noteOffOk = scheduleEvent(noteOffTime, finalNote, 0);
  if (!noteOffOk) return;

  if (shuffleOffsetUs <= 0) {
    refCountNoteOn(transport, finalNote, vel);
  } else {
    if (!scheduleEvent(noteOnTime, finalNote, vel)) {
      for (int8_t i = MAX_PENDING_EVENTS - 1; i >= 0; i--) {
        if (_events[i].active && _events[i].note == finalNote
            && _events[i].velocity == 0 && _events[i].fireTimeUs == noteOffTime) {
          _events[i].active = false;
          break;
        }
      }
    }
  }

  _tickFlash = true;
  _shuffleStepCounter++;
}

// =================================================================
// processEvents — fire pending events whose time has arrived
// =================================================================

void ArpEngine::processEvents(MidiTransport& transport) {
  uint32_t nowUs = micros();
  for (uint8_t i = 0; i < MAX_PENDING_EVENTS; i++) {
    if (!_events[i].active) continue;
    if ((int32_t)(nowUs - _events[i].fireTimeUs) >= 0) {
      if (_events[i].velocity > 0) {
        refCountNoteOn(transport, _events[i].note, _events[i].velocity);
      } else {
        refCountNoteOff(transport, _events[i].note);
      }
      _events[i].active = false;
    }
  }
}

// =================================================================
// flushPendingNoteOffs — emergency silence + stop playback
// =================================================================
// Two-phase flush:
//   1. Cancel all pending events (noteOn AND noteOff)
//   2. Sweep refcount: any note with refcount > 0 gets a MIDI noteOff
//   3. Set _playing = false (stops the arp engine)

void ArpEngine::flushPendingNoteOffs(MidiTransport& transport) {
  for (uint8_t i = 0; i < MAX_PENDING_EVENTS; i++) {
    _events[i].active = false;
  }

  for (uint8_t n = 0; n < 128; n++) {
    if (_noteRefCount[n] > 0) {
      transport.sendNoteOn(_channel, n, 0);
      _noteRefCount[n] = 0;
    }
  }

  _playing = false;
}

// =================================================================
// scheduleEvent
// =================================================================

bool ArpEngine::scheduleEvent(uint32_t fireTimeUs, uint8_t note, uint8_t velocity) {
  for (uint8_t i = 0; i < MAX_PENDING_EVENTS; i++) {
    if (!_events[i].active) {
      _events[i].fireTimeUs = fireTimeUs;
      _events[i].note       = note;
      _events[i].velocity   = velocity;
      _events[i].active     = true;
      return true;
    }
  }
  #if DEBUG_SERIAL
  Serial.println("[ARP] WARNING: Event queue full — event dropped");
  #endif
  return false;
}

// =================================================================
// Reference-counted MIDI noteOn/noteOff
// =================================================================

void ArpEngine::refCountNoteOn(MidiTransport& transport, uint8_t note, uint8_t velocity) {
  if (_noteRefCount[note] == 0) {
    transport.sendNoteOn(_channel, note, velocity);
  }
  if (_noteRefCount[note] < 255) {
    _noteRefCount[note]++;
  }
}

void ArpEngine::refCountNoteOff(MidiTransport& transport, uint8_t note) {
  if (_noteRefCount[note] == 0) return;
  _noteRefCount[note]--;
  if (_noteRefCount[note] == 0) {
    transport.sendNoteOn(_channel, note, 0);
  }
}

// =================================================================
// Queries
// =================================================================

uint8_t     ArpEngine::getNoteCount() const { return _positionCount; }
bool        ArpEngine::hasNotes() const     { return _positionCount > 0; }
ArpDivision ArpEngine::getDivision() const          { return _division; }
ArpPattern  ArpEngine::getPattern() const           { return _pattern; }
float       ArpEngine::getGateLength() const        { return _gateLength; }
float       ArpEngine::getShuffleDepth() const      { return _shuffleDepth; }
uint8_t     ArpEngine::getShuffleTemplate() const   { return _shuffleTemplate; }
uint8_t     ArpEngine::getBaseVelocity() const      { return _baseVelocity; }
uint8_t     ArpEngine::getVelocityVariation() const { return _velocityVariation; }
bool ArpEngine::consumeTickFlash() {
  if (_tickFlash) { _tickFlash = false; return true; }
  return false;
}
