#ifndef MIDI_ENGINE_H
#define MIDI_ENGINE_H

#include <stdint.h>
#include "../core/HardwareConfig.h"

class MidiTransport;

// =================================================================
// Aftertouch ring buffer entry
// =================================================================
struct AftertouchEvent {
  uint8_t pad;
  uint8_t note;
  uint8_t pressure;
};

// =================================================================
// MidiEngine — pad-level noteOn/Off + rate-limited poly-aftertouch
// =================================================================
// No scale resolver, no banks. Simple chromatic mapping from C2.
// Edge detection lives in the caller (main loop).

class MidiEngine {
public:
  MidiEngine();

  void begin(MidiTransport* transport);

  // --- Note events (called on edges by the main loop) ---
  void noteOn(uint8_t padIndex, uint8_t velocity);
  void noteOff(uint8_t padIndex);

  // --- Aftertouch (called every cycle for held pads) ---
  // Rate-limited per pad. Queues events into the ring buffer.
  void updateAftertouch(uint8_t padIndex, uint8_t pressure);

  // Drain the aftertouch queue (max FLUSH_BATCH per call).
  void flush();

private:
  MidiTransport* _transport;

  // Per-pad state
  uint8_t  _lastResolvedNote[NUM_KEYS];   // MIDI note sent on noteOn (0xFF = inactive)
  uint8_t  _lastSentPressure[NUM_KEYS];   // Last aftertouch value sent (for change threshold)
  uint32_t _lastAftertouchMs[NUM_KEYS];   // Timestamp of last aftertouch send

  // Aftertouch ring buffer
  static const uint8_t AT_RING_SIZE = 64;
  static const uint8_t FLUSH_BATCH  = 16;   // Max events drained per flush()
  AftertouchEvent _atRing[AT_RING_SIZE];
  uint8_t _atHead;  // write position
  uint8_t _atTail;  // read position
};

#endif // MIDI_ENGINE_H
