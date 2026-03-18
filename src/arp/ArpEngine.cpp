#include "ArpEngine.h"
#include "../core/MidiTransport.h"

ArpEngine::ArpEngine()
  : _channel(0), _pattern(ARP_UP), _octaveRange(1), _division(DIV_1_8),
    _gateLength(0.5f), _swing(0.5f), _baseVelocity(100), _velocityVariation(0),
    _noteCount(0), _orderCount(0),
    _sequenceLen(0), _sequenceDirty(false),
    _stepIndex(-1), _direction(1), _lastPlayedNote(0xFF),
    _playing(false), _holdOn(false) {
  for (uint8_t i = 0; i < MAX_ARP_NOTES; i++) {
    _notes[i] = 0xFF;
    _noteOrder[i] = 0xFF;
  }
  for (uint16_t i = 0; i < MAX_ARP_SEQUENCE; i++) {
    _sequence[i] = 0xFF;
  }
}

void ArpEngine::setChannel(uint8_t ch)              { _channel = ch; }
void ArpEngine::setPattern(ArpPattern pattern)       { _pattern = pattern; _sequenceDirty = true; }
void ArpEngine::setOctaveRange(uint8_t range)        { _octaveRange = range; _sequenceDirty = true; }
void ArpEngine::setDivision(ArpDivision div)         { _division = div; }
void ArpEngine::setGateLength(float gate)            { _gateLength = gate; }
void ArpEngine::setSwing(float swing)               { _swing = swing; }
void ArpEngine::setBaseVelocity(uint8_t vel)         { _baseVelocity = vel; }
void ArpEngine::setVelocityVariation(uint8_t pct)    { _velocityVariation = pct; }

void ArpEngine::addNote(uint8_t midiNote) {
  (void)midiNote;
  // TODO: add to _notes sorted, add to _noteOrder chronological, mark dirty
}

void ArpEngine::removeNote(uint8_t midiNote) {
  (void)midiNote;
  // TODO: remove from _notes and _noteOrder, mark dirty
}

void ArpEngine::clearAllNotes() {
  _noteCount = 0;
  _orderCount = 0;
  _sequenceDirty = true;
}

void ArpEngine::setHold(bool on) { _holdOn = on; }
bool ArpEngine::isHoldOn() const { return _holdOn; }

void ArpEngine::playStop() {
  _playing = !_playing;
  if (_playing) {
    _stepIndex = -1;  // Will start from beginning on next tick
  }
}

bool ArpEngine::isPlaying() const { return _playing; }

void ArpEngine::tick(MidiTransport& transport) {
  (void)transport;
  // TODO: advance step, send noteOn/Off, handle gate, swing, velocity
}

uint8_t ArpEngine::getNoteCount() const { return _noteCount; }
bool    ArpEngine::hasNotes() const     { return _noteCount > 0; }

void ArpEngine::rebuildSequence() {
  // TODO: build _sequence from _notes × _octaveRange according to _pattern
  _sequenceDirty = false;
}
