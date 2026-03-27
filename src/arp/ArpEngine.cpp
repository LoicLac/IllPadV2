#include "ArpEngine.h"
#include "../core/MidiTransport.h"
#include "../midi/ScaleResolver.h"
#include <Arduino.h>

// =================================================================
// Shuffle Groove Templates
// =================================================================
// 5 templates × 16 steps. Each value is a percentage (0-100) that
// determines how much a step is delayed relative to stepDuration.
//   offset = template[step % 16] × depth × stepDuration / 100
//
// At depth 0.0 → no shuffle (all offsets are 0).
// At depth 1.0 → full template effect.
// Template is selected via setShuffleTemplate(0-4).

static const int8_t SHUFFLE_TEMPLATES[NUM_SHUFFLE_TEMPLATES][SHUFFLE_TEMPLATE_LEN] = {
  // 0: Classic swing — every other step pushed back
  {0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50},
  // 1: Light/heavy alternating — subtler groove
  {0, 33, 0, 66, 0, 33, 0, 66, 0, 33, 0, 66, 0, 33, 0, 66},
  // 2: Backbeat push — every 3rd step delayed
  {0, 0, 50, 0, 0, 0, 50, 0, 0, 0, 50, 0, 0, 0, 50, 0},
  // 3: Ramp — progressive delay within each group of 4
  {0, 25, 50, 75, 0, 25, 50, 75, 0, 25, 50, 75, 0, 25, 50, 75},
  // 4: Triplet feel — 2/3 + 1/3 grouping
  {0, 66, 33, 66, 0, 66, 33, 66, 0, 66, 33, 66, 0, 66, 33, 66},
};

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
    _stepIndex(-1), _playing(false), _holdOn(false),
    _shuffleStepCounter(0), _tickFlash(false),
    _waitingForQuantize(false), _quantizeMode(ARP_START_IMMEDIATE) {
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
}

// =================================================================
// Pile management — padOrder positions, NOT MIDI notes
// =================================================================

void ArpEngine::addPadPosition(uint8_t padOrderPos) {
  // Detect pile 0→1 transition for shuffle reset
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

  // Reset shuffle groove when pile goes from empty to first note
  if (wasEmpty) _shuffleStepCounter = 0;

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

  _sequenceDirty = true;

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
  _playing = false;
  _shuffleStepCounter = 0;
}

// =================================================================
// Scale context
// =================================================================

void ArpEngine::setScaleConfig(const ScaleConfig& scale) {
  _scale = scale;
  // No need to mark dirty — resolution happens live at tick time
}

void ArpEngine::setPadOrder(const uint8_t* padOrder) {
  _padOrder = padOrder;
}

// =================================================================
// Hold + Transport
// =================================================================

void ArpEngine::setHold(bool on) { _holdOn = on; }
bool ArpEngine::isHoldOn() const { return _holdOn; }

void ArpEngine::playStop(MidiTransport& transport) {
  _playing = !_playing;
  if (_playing) {
    _stepIndex = -1;              // Restart from beginning on next tick
    _shuffleStepCounter = 0;      // Reset groove on play
    _waitingForQuantize = (_quantizeMode != ARP_START_IMMEDIATE);
  } else {
    flushPendingNoteOffs(transport);  // Silence all ringing notes
  }
  #if DEBUG_SERIAL
  Serial.printf("[ARP] Bank %d: %s\n", _channel + 1, _playing ? "Play" : "Stop");
  #endif
}

bool ArpEngine::isPlaying() const { return _playing; }

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

  // Source: sorted positions (UP/DOWN/UP_DOWN/RANDOM) or chronological (ORDER)
  const uint8_t* source = (_pattern == ARP_ORDER) ? _positionOrder : _positions;
  uint8_t sourceCount = (_pattern == ARP_ORDER) ? _orderCount : _positionCount;

  // Build base sequence with octave transpositions
  for (uint8_t oct = 0; oct < _octaveRange; oct++) {
    for (uint8_t i = 0; i < sourceCount; i++) {
      uint8_t encoded = source[i] + oct * 48;
      if (encoded > 191) continue;
      _sequence[_sequenceLen++] = encoded;
      if (_sequenceLen >= MAX_ARP_SEQUENCE) goto done;
    }
  }
  done:

  // Reverse for DOWN
  if (_pattern == ARP_DOWN) {
    for (uint8_t i = 0; i < _sequenceLen / 2; i++) {
      uint8_t tmp = _sequence[i];
      _sequence[i] = _sequence[_sequenceLen - 1 - i];
      _sequence[_sequenceLen - 1 - i] = tmp;
    }
  }

  // UP_DOWN: append reversed middle (without repeating extremes)
  if (_pattern == ARP_UP_DOWN && _sequenceLen > 2) {
    uint8_t upLen = _sequenceLen;
    for (int16_t i = upLen - 2; i >= 1; i--) {
      if (_sequenceLen >= MAX_ARP_SEQUENCE) break;
      _sequence[_sequenceLen++] = _sequence[i];
    }
  }

  // RANDOM: shuffle (Fisher-Yates) — guard against empty sequence
  if (_pattern == ARP_RANDOM && _sequenceLen > 1) {
    for (uint8_t i = _sequenceLen - 1; i > 0; i--) {
      uint8_t j = random(0, i + 1);
      uint8_t tmp = _sequence[i];
      _sequence[i] = _sequence[j];
      _sequence[j] = tmp;
    }
  }

  // Clamp step index to new length
  if (_stepIndex >= (int16_t)_sequenceLen) {
    _stepIndex = -1;
  }
}

// =================================================================
// Tick — called by ArpScheduler when a rhythmic step fires
// =================================================================
// This is where notes are SCHEDULED, not sent immediately.
// The actual MIDI messages are fired by processEvents() which
// runs every loop iteration and checks timestamps.
//
// Flow:
//   1. Resolve which MIDI note to play (from pile + scale + octave)
//   2. Calculate shuffle offset for this step
//   3. Schedule noteOn (immediate or delayed by shuffle)
//   4. Schedule noteOff (at noteOnTime + gate duration)
//   5. Advance step counter and shuffle counter

void ArpEngine::tick(MidiTransport& transport, uint32_t stepDurationUs,
                     uint32_t currentTick, uint32_t globalTick) {
  // --- Empty pile: flush everything and return ---
  if (_positionCount == 0) {
    flushPendingNoteOffs(transport);
    if (!_holdOn) _playing = false;  // HOLD OFF: pile empty = arp stops naturally
    return;
  }

  // --- HOLD ON + stopped: do nothing (waiting for play/stop pad) ---
  if (_holdOn && !_playing) return;

  // --- Non-hold: always playing if notes exist ---
  if (!_holdOn && _positionCount > 0) {
    if (!_playing) {
      _playing = true;
      // HOLD OFF auto-play: respect quantize on first note (0→1 transition)
      _waitingForQuantize = (_quantizeMode != ARP_START_IMMEDIATE);
    }
  }

  // --- Quantized start: wait for beat/bar boundary ---
  if (_waitingForQuantize) {
    uint16_t boundary = (_quantizeMode == ARP_START_BAR) ? 96 : 24;
    if (globalTick % boundary != 0) return;  // Not on boundary yet
    _waitingForQuantize = false;               // Boundary reached, proceed
  }

  // --- Rebuild sequence if dirty ---
  if (_sequenceDirty) rebuildSequence();
  if (_sequenceLen == 0) return;

  // --- Advance step ---
  _stepIndex++;
  if (_stepIndex >= (int16_t)_sequenceLen) {
    _stepIndex = 0;
    // RANDOM: re-shuffle at loop boundary for variety
    if (_pattern == ARP_RANDOM) {
      for (uint8_t i = _sequenceLen - 1; i > 0; i--) {
        uint8_t j = random(0, i + 1);
        uint8_t tmp = _sequence[i];
        _sequence[i] = _sequence[j];
        _sequence[j] = tmp;
      }
    }
  }

  // --- Decode position + octave offset ---
  uint8_t encoded = _sequence[_stepIndex];
  uint8_t pos = encoded % 48;
  uint8_t octOffset = encoded / 48;

  // --- Resolve to MIDI note via ScaleResolver ---
  if (!_padOrder) return;

  // Reverse lookup: find padIndex from padOrder position
  uint8_t padIndex = 0xFF;
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    if (_padOrder[i] == pos) { padIndex = i; break; }
  }
  if (padIndex == 0xFF) return;

  uint8_t midiNote = ScaleResolver::resolve(padIndex, _padOrder, _scale);
  if (midiNote == 0xFF) return;

  // Apply octave transposition — fold down if out of range
  uint8_t finalNote = midiNote + octOffset * 12;
  if (finalNote > 127) finalNote -= 12;
  if (finalNote > 127) return;  // Still out of range, skip this step

  // --- Calculate velocity with variation ---
  uint8_t vel = _baseVelocity;
  if (_velocityVariation > 0) {
    int16_t range = (int16_t)_velocityVariation * 127 / 200;
    int16_t offset = (int16_t)(random(-range, range + 1));
    int16_t result = (int16_t)vel + offset;
    vel = (uint8_t)constrain(result, 1, 127);
  }

  // --- Calculate shuffle offset for this step ---
  // The shuffle template provides a percentage delay per step position.
  // The actual delay = template_value × depth × stepDuration / 100.
  // At depth 0.0: no delay. At depth 1.0: full template effect.
  uint32_t shuffleOffsetUs = 0;
  if (_shuffleDepth > 0.001f) {
    int8_t pct = SHUFFLE_TEMPLATES[_shuffleTemplate][_shuffleStepCounter % SHUFFLE_TEMPLATE_LEN];
    if (pct > 0) {
      shuffleOffsetUs = (uint32_t)((float)pct * _shuffleDepth * (float)stepDurationUs / 100.0f);
    }
  }

  // --- Calculate timing ---
  uint32_t nowUs = micros();
  uint32_t noteOnTime = nowUs + shuffleOffsetUs;

  // Gate duration: how long the note rings after noteOn.
  // gateLength 0.5 = half step, 1.0 = full step, >1.0 = overlap into next step.
  // Minimum 1ms to prevent zero-length notes.
  uint32_t gateUs = (uint32_t)((float)stepDurationUs * _gateLength);
  if (gateUs < 1000) gateUs = 1000;
  uint32_t noteOffTime = noteOnTime + gateUs;

  // --- Schedule noteOff FIRST (reserve the slot) ---
  // We schedule noteOff before noteOn to guarantee the pair is atomic:
  // if the queue is nearly full, we'd rather drop both events than
  // schedule a noteOn without its matching noteOff (= stuck note).
  bool noteOffOk = scheduleEvent(noteOffTime, finalNote, 0);
  if (!noteOffOk) {
    // Queue full — skip this step entirely (no noteOn without noteOff)
    return;
  }

  // --- Schedule noteOn ---
  if (shuffleOffsetUs == 0) {
    // No shuffle delay: fire noteOn immediately (saves a queue slot)
    refCountNoteOn(transport, finalNote, vel);
  } else {
    // Shuffle delay: queue noteOn for later
    if (!scheduleEvent(noteOnTime, finalNote, vel)) {
      // noteOn couldn't be scheduled but noteOff was — cancel the noteOff.
      // Find and deactivate it (it's the most recently added event).
      for (int8_t i = MAX_PENDING_EVENTS - 1; i >= 0; i--) {
        if (_events[i].active && _events[i].note == finalNote
            && _events[i].velocity == 0 && _events[i].fireTimeUs == noteOffTime) {
          _events[i].active = false;
          break;
        }
      }
    }
  }

  // --- Signal tick to LedController ---
  _tickFlash = true;

  // --- Advance shuffle step counter ---
  _shuffleStepCounter++;
}

// =================================================================
// processEvents — fire pending events whose time has arrived
// =================================================================
// Called every loop iteration (~1kHz) by ArpScheduler.
// Scans the event queue and fires any event where now >= fireTime.
//
// Uses (int32_t) cast on the unsigned difference to handle
// micros() wraparound correctly. Safe as long as events are
// not scheduled more than ~35 minutes ahead (they're not —
// max step duration is ~96 seconds at 10 BPM, DIV_4_1).

void ArpEngine::processEvents(MidiTransport& transport) {
  uint32_t nowUs = micros();
  for (uint8_t i = 0; i < MAX_PENDING_EVENTS; i++) {
    if (!_events[i].active) continue;

    // Signed comparison handles micros() wrap correctly
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
// flushPendingNoteOffs — emergency silence
// =================================================================
// Called by playStop(stop) and clearAllNotes().
// Two-phase flush for absolute safety:
//   1. Cancel all pending events (noteOn AND noteOff)
//   2. Sweep refcount: any note with refcount > 0 gets a MIDI noteOff
// This guarantees zero stuck notes even if the event queue was
// in an inconsistent state.

void ArpEngine::flushPendingNoteOffs(MidiTransport& transport) {
  // Phase 1: cancel all pending events
  for (uint8_t i = 0; i < MAX_PENDING_EVENTS; i++) {
    _events[i].active = false;
  }

  // Phase 2: sweep refcount — silence any note still "on"
  for (uint8_t n = 0; n < 128; n++) {
    if (_noteRefCount[n] > 0) {
      transport.sendNoteOn(_channel, n, 0);  // velocity 0 = noteOff
      _noteRefCount[n] = 0;
    }
  }
}

// =================================================================
// scheduleEvent — queue a noteOn or noteOff for future firing
// =================================================================
// Returns false if the queue is full (all 36 slots occupied).
// This can happen with extreme gate + fast division.
// The caller (tick) ensures noteOn/noteOff pairs are atomic:
// noteOff is reserved first, and if noteOn fails, noteOff is cancelled.

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
// Reference-counted MIDI noteOn
// =================================================================
// Only sends actual MIDI noteOn when refcount goes from 0 to 1
// (first instance of this note). If the note is already ringing
// from a previous overlapping step, we just increment the count.
//
// This prevents the overlap bug where two steps play the same note
// and the first noteOff kills the second one prematurely.

void ArpEngine::refCountNoteOn(MidiTransport& transport, uint8_t note, uint8_t velocity) {
  if (_noteRefCount[note] == 0) {
    // First instance — send actual MIDI noteOn
    transport.sendNoteOn(_channel, note, velocity);
  }
  if (_noteRefCount[note] < 255) {
    _noteRefCount[note]++;
  }
}

// =================================================================
// Reference-counted MIDI noteOff
// =================================================================
// Only sends actual MIDI noteOff when refcount goes from 1 to 0
// (last instance of this note released). If other steps are still
// holding this note, we just decrement.

void ArpEngine::refCountNoteOff(MidiTransport& transport, uint8_t note) {
  if (_noteRefCount[note] == 0) return;  // Already silent — nothing to do
  _noteRefCount[note]--;
  if (_noteRefCount[note] == 0) {
    // Last instance released — send actual MIDI noteOff
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
