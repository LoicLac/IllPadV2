#include "ArpEngine.h"
#include "../core/MidiTransport.h"

ArpEngine::ArpEngine()
  : _channel(0), _pattern(ARP_UP), _octaveRange(1), _division(DIV_1_8),
    _gateLength(0.5f), _swing(0.5f), _baseVelocity(100), _velocityVariation(0),
    _positionCount(0), _orderCount(0),
    _sequenceLen(0), _sequenceDirty(false),
    _scale{true, 2, 0},  // chromatic, root C, Ionian
    _padOrder(nullptr),
    _stepIndex(-1), _direction(1), _lastPlayedNote(0xFF),
    _playing(false), _holdOn(false) {
  for (uint8_t i = 0; i < MAX_ARP_NOTES; i++) {
    _positions[i] = 0xFF;
    _positionOrder[i] = 0xFF;
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

void ArpEngine::addPadPosition(uint8_t padOrderPos) {
  (void)padOrderPos;
  // TODO: add to _positions sorted, add to _positionOrder chronological, mark dirty
}

void ArpEngine::removePadPosition(uint8_t padOrderPos) {
  (void)padOrderPos;
  // TODO: remove from _positions and _positionOrder, mark dirty
}

void ArpEngine::clearAllNotes() {
  _positionCount = 0;
  _orderCount = 0;
  _sequenceDirty = true;
}

void ArpEngine::setScaleConfig(const ScaleConfig& scale) {
  _scale = scale;
  // No need to mark dirty — resolution happens live at tick time
}

void ArpEngine::setPadOrder(const uint8_t* padOrder) {
  _padOrder = padOrder;
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
  // TODO: advance step, resolve position→MIDI note via ScaleResolver at each tick,
  //       send noteOn/Off, handle gate, swing, velocity
  // Key: _sequence[step] is a padOrder position, resolve to MIDI note using
  //       ScaleResolver::resolve() with _padOrder and _scale at tick time.
  //       This enables live scale changes on background arps.
}

uint8_t ArpEngine::getNoteCount() const { return _positionCount; }
bool    ArpEngine::hasNotes() const     { return _positionCount > 0; }

void ArpEngine::rebuildSequence() {
  // TODO: build _sequence from _positions × _octaveRange according to _pattern
  // _sequence stores padOrder positions, NOT MIDI notes
  _sequenceDirty = false;
}
