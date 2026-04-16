#include "ArpEngine.h"
#include "../core/MidiTransport.h"
#include "../midi/ScaleResolver.h"
#include <Arduino.h>

// Shuffle templates now in midi/GrooveTemplates.h (shared with LoopEngine)

// Fibonacci sequences for OCTAVE_BOUNCE and PROBABILITY patterns
static const uint8_t FIB_BOUNCE[]   = {1, 2, 3, 5, 8, 13, 21, 13, 8, 5, 3, 2};
static const uint8_t FIB_BOUNCE_LEN = 12;
static const uint8_t FIB_SPIRAL[]   = {1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89};
static const uint8_t FIB_SPIRAL_LEN = 11;

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
    _waitingForQuantize(false), _quantizeMode(ARP_START_IMMEDIATE),
    _lastDispatchedGlobalTick(0xFFFFFFFF) {  // B-001/B-CODE-1 fix sentinel
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
    _lastDispatchedGlobalTick = 0xFFFFFFFF;  // B-001 fix: mark fresh wait
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
// effectiveOctaveRange — octave patterns override minimum range
// =================================================================

uint8_t ArpEngine::effectiveOctaveRange() const {
  switch (_pattern) {
    case ARP_UP_OCTAVE:
    case ARP_DOWN_OCTAVE:    return max((uint8_t)2, _octaveRange);
    case ARP_OCTAVE_WAVE:
    case ARP_OCTAVE_BOUNCE:  return max((uint8_t)3, _octaveRange);
    case ARP_CHORD:
    case ARP_PROBABILITY:    return max((uint8_t)4, _octaveRange);
    default:                 return _octaveRange;
  }
}

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
  uint8_t octaves = effectiveOctaveRange();

  switch (_pattern) {

    // ---------------------------------------------------------------
    // GROUP A: Build ascending, then post-process
    // UP, ORDER, UP_OCTAVE, CHORD = ascending as-is
    // DOWN, DOWN_OCTAVE = ascending then reverse
    // UP_DOWN = ascending then append reversed middle
    // RANDOM  = ascending then Fisher-Yates shuffle
    // ---------------------------------------------------------------
    case ARP_UP:
    case ARP_DOWN:
    case ARP_UP_DOWN:
    case ARP_RANDOM:
    case ARP_ORDER:
    case ARP_UP_OCTAVE:
    case ARP_DOWN_OCTAVE:
    case ARP_CHORD:
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

      // DOWN / DOWN_OCTAVE: reverse entire sequence
      if (_pattern == ARP_DOWN || _pattern == ARP_DOWN_OCTAVE) {
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

      // RANDOM: Fisher-Yates shuffle
      if (_pattern == ARP_RANDOM && _sequenceLen > 1) {
        for (uint8_t i = _sequenceLen - 1; i > 0; i--) {
          uint8_t j = random(0, i + 1);
          uint8_t tmp = _sequence[i];
          _sequence[i] = _sequence[j];
          _sequence[j] = tmp;
        }
      }
      break;
    }

    // ---------------------------------------------------------------
    // CASCADE: Each note played twice [C C D D E E]
    // ---------------------------------------------------------------
    case ARP_CASCADE:
    {
      for (uint8_t oct = 0; oct < octaves; oct++) {
        for (uint8_t i = 0; i < sourceCount; i++) {
          uint8_t encoded = source[i] + oct * 48;
          if (encoded > 191) continue;
          _sequence[_sequenceLen++] = encoded;
          if (_sequenceLen >= MAX_ARP_SEQUENCE) goto done;
          _sequence[_sequenceLen++] = encoded;
          if (_sequenceLen >= MAX_ARP_SEQUENCE) goto done;
        }
      }
      break;
    }

    // ---------------------------------------------------------------
    // CONVERGE: Outside-in [0, n-1, 1, n-2, 2, ...]
    // ---------------------------------------------------------------
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

    // ---------------------------------------------------------------
    // DIVERGE: Center-out [mid, mid-1, mid+1, mid-2, mid+2, ...]
    // ---------------------------------------------------------------
    case ARP_DIVERGE:
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
      if (tempLen > 0) {
        uint8_t center = tempLen / 2;
        _sequence[_sequenceLen++] = temp[center];
        for (uint8_t offset = 1; offset < tempLen && _sequenceLen < MAX_ARP_SEQUENCE; offset++) {
          if (center >= offset) {
            _sequence[_sequenceLen++] = temp[center - offset];
          }
          if (_sequenceLen >= MAX_ARP_SEQUENCE) break;
          if (center + offset < tempLen) {
            _sequence[_sequenceLen++] = temp[center + offset];
          }
        }
      }
      break;
    }

    // ---------------------------------------------------------------
    // PEDAL_UP: Alternate lowest note with ascending others
    // [A B A C A D | A A' A B' A C' A D']
    // Pedal = source[0] at octave 0 (always lowest note)
    // ---------------------------------------------------------------
    case ARP_PEDAL_UP:
    {
      uint8_t pedal = source[0]; // lowest sorted position, oct 0
      for (uint8_t oct = 0; oct < octaves; oct++) {
        for (uint8_t i = 0; i < sourceCount; i++) {
          uint8_t encoded = source[i] + oct * 48;
          if (encoded > 191) continue;
          if (oct == 0 && i == 0) continue; // skip pedal itself
          if (_sequenceLen + 2 > MAX_ARP_SEQUENCE) goto done;
          _sequence[_sequenceLen++] = pedal;
          _sequence[_sequenceLen++] = encoded;
        }
      }
      // Edge case: single note
      if (_sequenceLen == 0 && sourceCount > 0) {
        _sequence[_sequenceLen++] = source[0];
      }
      break;
    }

    // ---------------------------------------------------------------
    // OCTAVE_WAVE: Ping-pong through octaves
    // Oct sequence: 0, 1, ..., max-1, max-2, ..., 1
    // ---------------------------------------------------------------
    case ARP_OCTAVE_WAVE:
    {
      uint8_t octSeq[MAX_ARP_OCTAVES * 2];
      uint8_t octSeqLen = 0;
      // Up: 0 to octaves-1
      for (uint8_t o = 0; o < octaves; o++) {
        octSeq[octSeqLen++] = o;
      }
      // Down: octaves-2 to 1 (skip endpoints to avoid repeat at loop boundary)
      if (octaves > 2) {
        for (int8_t o = octaves - 2; o >= 1; o--) {
          octSeq[octSeqLen++] = (uint8_t)o;
        }
      }
      for (uint8_t oi = 0; oi < octSeqLen; oi++) {
        for (uint8_t i = 0; i < sourceCount; i++) {
          uint8_t encoded = source[i] + octSeq[oi] * 48;
          if (encoded > 191) continue;
          _sequence[_sequenceLen++] = encoded;
          if (_sequenceLen >= MAX_ARP_SEQUENCE) goto done;
        }
      }
      break;
    }

    // ---------------------------------------------------------------
    // OCTAVE_BOUNCE: Fibonacci stepping + octave ping-pong
    // Pre-built by simulating the NANO R4 state machine.
    // ---------------------------------------------------------------
    case ARP_OCTAVE_BOUNCE:
    {
      int16_t simIndex = 0;
      int8_t  simOct   = 0;
      bool    simDir   = true;  // true = forward
      uint8_t simFib   = 0;

      for (uint16_t step = 0; step < MAX_ARP_SEQUENCE; step++) {
        uint8_t encoded = source[simIndex] + (uint8_t)simOct * 48;
        if (encoded <= 191) {
          _sequence[_sequenceLen++] = encoded;
          if (_sequenceLen >= MAX_ARP_SEQUENCE) break;
        }

        uint8_t stepSize = FIB_BOUNCE[simFib];
        if (simDir) {
          simIndex += stepSize;
          while (simIndex >= (int16_t)sourceCount) {
            simIndex -= sourceCount;
            simOct++;
            if (simOct >= (int8_t)octaves) {
              simOct = octaves - 1;
              simDir = false;
              break;
            }
          }
        } else {
          simIndex -= stepSize;
          while (simIndex < 0) {
            simIndex += sourceCount;
            simOct--;
            if (simOct < 0) {
              simOct = 0;
              simDir = true;
              break;
            }
          }
        }
        simFib = (simFib + 1) % FIB_BOUNCE_LEN;
      }
      break;
    }

    // ---------------------------------------------------------------
    // PROBABILITY: Fibonacci stepping + octave spiral
    // Pre-built by simulating the NANO R4 state machine.
    // ---------------------------------------------------------------
    case ARP_PROBABILITY:
    {
      int16_t simIndex = 0;
      uint8_t simFib   = 0;
      uint8_t simOct   = 0;

      for (uint16_t step = 0; step < MAX_ARP_SEQUENCE; step++) {
        uint8_t encoded = source[simIndex] + simOct * 48;
        if (encoded <= 191) {
          _sequence[_sequenceLen++] = encoded;
          if (_sequenceLen >= MAX_ARP_SEQUENCE) break;
        }

        uint8_t stepSize = FIB_SPIRAL[simFib];
        simIndex = ((int16_t)simIndex + stepSize) % (int16_t)sourceCount;
        simFib++;
        if (simFib >= FIB_SPIRAL_LEN) {
          simFib = 0;
          simOct++;
          if (simOct >= octaves) simOct = 0;
        }
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
  if (_positionCount == 0)  return ArpState::IDLE;
  if (_waitingForQuantize)  return ArpState::WAITING_QUANTIZE;
  if (_playing)             return ArpState::PLAYING;
  if (_holdOn)              return ArpState::HELD_STOPPED;
  return ArpState::PLAYING;  // HOLD OFF + notes present = auto-play
}

// =================================================================
// Tick — called by ArpScheduler when a rhythmic step fires
// =================================================================
// Classifies the current state, handles transitions, then delegates
// to executeStep() for the actual note scheduling.

void ArpEngine::tick(MidiTransport& transport, uint32_t stepDurationUs,
                     uint32_t currentTick, uint32_t globalTick) {
  switch (currentState()) {
    case ArpState::IDLE:
      flushPendingNoteOffs(transport);
      if (!_holdOn) _playing = false;  // HOLD OFF: pile empty = arp stops naturally
      return;

    case ArpState::HELD_STOPPED:
      return;

    case ArpState::WAITING_QUANTIZE: {
      uint16_t boundary = (_quantizeMode == ARP_START_BAR) ? 96 : 24;
      // B-001 fix (2026-04-07): crossing detection. Sentinel 0xFFFFFFFF means
      // "fresh wait, fire on next observed tick". Subsequent waits within the
      // same play session use the strict crossing check, robust against
      // ClockManager::generateTicks() multi-tick catch-up bursts (up to 4).
      bool crossed = (_lastDispatchedGlobalTick == 0xFFFFFFFF)
                   || ((globalTick / boundary) > (_lastDispatchedGlobalTick / boundary));
      if (!crossed) return;
      _waitingForQuantize = false;
      _lastDispatchedGlobalTick = globalTick;
      break;
    }

    case ArpState::PLAYING:
      // HOLD OFF auto-play: activate if not yet playing
      if (!_playing) {
        _playing = true;
        _waitingForQuantize = (_quantizeMode != ARP_START_IMMEDIATE);
        if (_waitingForQuantize) {
          // B-CODE-1 fix (2026-04-07): mark fresh wait + defer to next tick
          // via WAITING_QUANTIZE branch (single source of truth for boundary
          // crossing detection — no logic duplication with the case above).
          _lastDispatchedGlobalTick = 0xFFFFFFFF;
          return;
        }
      }
      break;
  }

  executeStep(transport, stepDurationUs);
}

// =================================================================
// executeStep — resolve note, calculate timing, schedule events
// =================================================================
// Called by tick() after state guards have passed.
// Flow:
//   1. Rebuild sequence if dirty
//   2. Advance step index (re-shuffle on loop for RANDOM)
//   3. Resolve padOrder position → MIDI note via ScaleResolver
//   4. Calculate velocity with variation
//   5. Calculate shuffle offset from groove template
//   6. Schedule noteOn (immediate or delayed) + noteOff (gate duration)

void ArpEngine::executeStep(MidiTransport& transport, uint32_t stepDurationUs) {
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

  // Apply octave transposition — fold down until in range
  uint8_t finalNote = midiNote + octOffset * 12;
  while (finalNote > 127) finalNote -= 12;
  if (finalNote > 127) return;  // Safety: should not happen after fold

  // --- Calculate velocity with variation ---
  uint8_t vel = _baseVelocity;
  if (_velocityVariation > 0) {
    int16_t range = (int16_t)_velocityVariation * 127 / 200;
    int16_t offset = (int16_t)(random(-range, range + 1));
    int16_t result = (int16_t)vel + offset;
    vel = (uint8_t)constrain(result, 1, 127);
  }

  // --- Calculate shuffle offset for this step (bipolar: positive = drag, negative = rush) ---
  int32_t shuffleOffsetUs = 0;
  if (_shuffleDepth > 0.001f) {
    int8_t pct = SHUFFLE_TEMPLATES[_shuffleTemplate][_shuffleStepCounter % SHUFFLE_TEMPLATE_LEN];
    if (pct != 0) {
      shuffleOffsetUs = (int32_t)((float)pct * _shuffleDepth * (float)stepDurationUs / 100.0f);
    }
  }

  // --- Calculate timing ---
  uint32_t nowUs = micros();
  uint32_t noteOnTime = nowUs + (uint32_t)shuffleOffsetUs;  // unsigned wrap handles negative offsets

  // Gate duration: gateLength 0.5 = half step, 1.0 = full, >1.0 = overlap.
  // Minimum 1ms to prevent zero-length notes.
  uint32_t gateUs = (uint32_t)((float)stepDurationUs * _gateLength);
  if (gateUs < 1000) gateUs = 1000;
  uint32_t noteOffTime = noteOnTime + gateUs;

  // Guard: if negative shuffle pushed noteOff into the past, clamp to now + 1ms
  if (shuffleOffsetUs < 0 && (int32_t)(noteOffTime - nowUs) < 1000) {
    noteOffTime = nowUs + 1000;
  }

  // --- Schedule noteOff FIRST (reserve the slot) ---
  // Guarantees atomic noteOn/noteOff pair: if queue is nearly full,
  // we drop both rather than schedule noteOn without matching noteOff.
  bool noteOffOk = scheduleEvent(noteOffTime, finalNote, 0);
  if (!noteOffOk) return;  // Queue full — skip this step entirely

  // --- Schedule noteOn ---
  if (shuffleOffsetUs <= 0) {
    // No delay or negative offset (rush): fire noteOn immediately (saves a queue slot)
    refCountNoteOn(transport, finalNote, vel);
  } else {
    // Shuffle delay: queue noteOn for later
    if (!scheduleEvent(noteOnTime, finalNote, vel)) {
      // noteOn couldn't be scheduled — cancel the orphaned noteOff
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
// Returns false if the queue is full (all 64 slots occupied).
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
