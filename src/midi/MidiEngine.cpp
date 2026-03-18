#include "MidiEngine.h"
#include "../core/MidiTransport.h"
#include <Arduino.h>
#include <string.h>

// =================================================================
// Constructor
// =================================================================

MidiEngine::MidiEngine()
  : _transport(nullptr)
  , _atHead(0)
  , _atTail(0)
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

// =================================================================
// noteOn — chromatic from MIDI_BASE_NOTE (C2 = 36)
// =================================================================

void MidiEngine::noteOn(uint8_t padIndex, uint8_t velocity) {
  if (padIndex >= NUM_KEYS || !_transport) return;

  uint8_t note = MIDI_BASE_NOTE + padIndex;
  if (note > 127) note = 127;

  _lastResolvedNote[padIndex] = note;
  _lastSentPressure[padIndex] = 0;
  _transport->sendNoteOn(MIDI_CHANNEL, note, velocity);
}

// =================================================================
// noteOff — uses the note stored at noteOn time (never re-resolves)
// =================================================================

void MidiEngine::noteOff(uint8_t padIndex) {
  if (padIndex >= NUM_KEYS || !_transport) return;

  uint8_t note = _lastResolvedNote[padIndex];
  if (note == 0xFF) return;  // No active note on this pad

  _transport->sendNoteOn(MIDI_CHANNEL, note, 0);  // velocity 0 = noteOff
  _lastResolvedNote[padIndex] = 0xFF;
  _lastSentPressure[padIndex] = 0;
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
  if (now - _lastAftertouchMs[padIndex] < AFTERTOUCH_UPDATE_INTERVAL_MS) return;

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
    _transport->sendPolyAftertouch(MIDI_CHANNEL, evt.note, evt.pressure);
    _atTail = (_atTail + 1) % AT_RING_SIZE;
    count++;
  }
}
