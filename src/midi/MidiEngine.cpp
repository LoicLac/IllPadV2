#include "MidiEngine.h"
#include "ScaleResolver.h"
#include "../core/MidiTransport.h"
#include <Arduino.h>
#include <string.h>

// =================================================================
// Constructor
// =================================================================

MidiEngine::MidiEngine()
  : _transport(nullptr)
  , _channel(MIDI_CHANNEL)
  , _atHead(0)
  , _atTail(0)
  , _aftertouchIntervalMs(AT_RATE_DEFAULT)
{
  memset(_lastResolvedNote, 0xFF, sizeof(_lastResolvedNote));
  memset(_lastSentPressure, 0, sizeof(_lastSentPressure));
  memset(_lastAftertouchMs, 0, sizeof(_lastAftertouchMs));
}

// =================================================================
// Init
// =================================================================

void MidiEngine::begin(MidiTransport* transport) {
  _transport = transport;
}

void MidiEngine::setAftertouchRate(uint8_t ms) {
  _aftertouchIntervalMs = constrain(ms, AT_RATE_MIN, AT_RATE_MAX);
}

void MidiEngine::setChannel(uint8_t ch) {
  _channel = (ch <= 15) ? ch : 15;
}

uint8_t MidiEngine::getChannel() const {
  return _channel;
}

// =================================================================
// noteOn — resolve note via ScaleResolver, then send
// =================================================================

void MidiEngine::noteOn(uint8_t padIndex, uint8_t velocity,
                         const uint8_t* padOrder, const ScaleConfig& scale) {
  if (padIndex >= NUM_KEYS || !_transport) return;

  uint8_t note = ScaleResolver::resolve(padIndex, padOrder, scale);
  if (note == 0xFF) return;  // Unmapped or out of range

  _lastResolvedNote[padIndex] = note;
  _lastSentPressure[padIndex] = 0;
  _transport->sendNoteOn(_channel, note, velocity);
}

// =================================================================
// noteOff — uses the note stored at noteOn time (never re-resolves)
// =================================================================

void MidiEngine::noteOff(uint8_t padIndex) {
  if (padIndex >= NUM_KEYS || !_transport) return;

  uint8_t note = _lastResolvedNote[padIndex];
  if (note == 0xFF) return;  // No active note on this pad

  _transport->sendNoteOn(_channel, note, 0);  // velocity 0 = noteOff
  _lastResolvedNote[padIndex] = 0xFF;
  _lastSentPressure[padIndex] = 0;
}

// =================================================================
// sendPitchBend — forward to transport on current channel
// =================================================================

void MidiEngine::sendPitchBend(uint16_t value) {
  if (!_transport) return;
  _transport->sendPitchBend(_channel, value);
}

// =================================================================
// allNotesOff — kill every active note, drain aftertouch queue
// =================================================================

void MidiEngine::allNotesOff() {
  if (!_transport) return;

  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    if (_lastResolvedNote[i] != 0xFF) {
      _transport->sendNoteOn(_channel, _lastResolvedNote[i], 0);
      _lastResolvedNote[i] = 0xFF;
      _lastSentPressure[i] = 0;
    }
  }
  // Drain aftertouch queue — stale events from old channel
  _atHead = 0;
  _atTail = 0;
}

// =================================================================
// updateAftertouch — rate-limited, change-threshold, queued
// =================================================================

void MidiEngine::updateAftertouch(uint8_t padIndex, uint8_t pressure) {
  if (padIndex >= NUM_KEYS) return;

  uint8_t note = _lastResolvedNote[padIndex];
  if (note == 0xFF) return;  // Not an active note

  // --- Change threshold: skip if pressure hasn't changed enough ---
  int16_t delta = (int16_t)pressure - (int16_t)_lastSentPressure[padIndex];
  if (delta < 0) delta = -delta;
  if (delta < AFTERTOUCH_CHANGE_THRESHOLD) return;

  // --- Rate limit per pad ---
  uint32_t now = millis();
  if (now - _lastAftertouchMs[padIndex] < _aftertouchIntervalMs) return;

  // --- Queue into ring buffer ---
  uint8_t nextHead = (_atHead + 1) % AT_RING_SIZE;
  if (nextHead == _atTail) return;  // Ring full — drop this event

  _atRing[_atHead] = { padIndex, note, pressure };
  _atHead = nextHead;

  _lastSentPressure[padIndex] = pressure;
  _lastAftertouchMs[padIndex] = now;
}

// =================================================================
// flush — drain up to FLUSH_BATCH aftertouch events
// =================================================================

void MidiEngine::flush() {
  if (!_transport) return;

  uint8_t count = 0;
  while (_atTail != _atHead && count < FLUSH_BATCH) {
    const AftertouchEvent& evt = _atRing[_atTail];
    _transport->sendPolyAftertouch(_channel, evt.note, evt.pressure);
    _atTail = (_atTail + 1) % AT_RING_SIZE;
    count++;
  }
}
