#ifndef ARP_ENGINE_H
#define ARP_ENGINE_H

#include <stdint.h>
#include "../core/KeyboardData.h"

class MidiTransport;

// =================================================================
// Event Queue — time-based noteOn/noteOff scheduling
// =================================================================
// Enables gate length (noteOff delayed), shuffle (noteOn delayed),
// and note overlap (multiple notes ringing simultaneously).
// Max 36 pending events per engine allows extreme overlap scenarios
// (e.g. long gate at fast divisions = many notes ringing at once).

static const uint8_t MAX_PENDING_EVENTS = 36;
static const uint8_t NUM_SHUFFLE_TEMPLATES = 5;
static const uint8_t SHUFFLE_TEMPLATE_LEN = 16;

struct PendingEvent {
  uint32_t fireTimeUs;   // micros() timestamp when this event should fire
  uint8_t  note;         // MIDI note number (0-127)
  uint8_t  velocity;     // 0 = noteOff, >0 = noteOn
  bool     active;       // true = slot in use, false = free for reuse
};

// =================================================================
// ArpEngine — one arpeggiator instance (max 4 in system)
// =================================================================

class ArpEngine {
public:
  ArpEngine();

  // --- Configuration (set from PotRouter via main loop) ---
  void setChannel(uint8_t ch);
  void setPattern(ArpPattern pattern);
  void setOctaveRange(uint8_t range);          // 1-4
  void setDivision(ArpDivision div);
  void setGateLength(float gate);              // 0.0-1.0 (or beyond for overlap)
  void setShuffleDepth(float depth);           // 0.0-1.0
  void setShuffleTemplate(uint8_t tmpl);       // 0-4, index into groove templates
  void setBaseVelocity(uint8_t vel);           // 1-127
  void setVelocityVariation(uint8_t pct);      // 0-100
  void setStartMode(uint8_t mode);             // ArpStartMode (0=immediate, 1=beat, 2=bar)

  // --- Pile management ---
  // Stores padOrder positions (0-47), NOT MIDI notes.
  // Resolution to MIDI notes happens at tick time via current ScaleConfig.
  void addPadPosition(uint8_t padOrderPos);
  void removePadPosition(uint8_t padOrderPos);
  void clearAllNotes(MidiTransport& transport);

  // --- Scale/pad context (set by main loop, used at tick for note resolution) ---
  void setScaleConfig(const ScaleConfig& scale);
  void setPadOrder(const uint8_t* padOrder);

  // --- Hold + Transport ---
  void setHold(bool on);
  bool isHoldOn() const;
  void playStop(MidiTransport& transport);     // Toggle, restart from beginning
  void resetStepIndex();                       // Restart sequence from beginning (MIDI Start sync)
  bool isPlaying() const;

  // --- Core tick (called by ArpScheduler when a step fires) ---
  // stepDurationUs = real-time duration of one step in microseconds,
  // computed by ArpScheduler from BPM and division.
  void tick(MidiTransport& transport, uint32_t stepDurationUs, uint32_t currentTick);

  // --- Event processing (called every loop iteration by ArpScheduler) ---
  // Fires any pending noteOn/noteOff events whose time has arrived.
  // This is the real-time heart of the gate/shuffle system.
  void processEvents(MidiTransport& transport);

  // --- Emergency flush (called on stop, clear, etc.) ---
  // Immediately fires all pending noteOffs and sweeps refcount to zero.
  // Guarantees no stuck notes in the DAW.
  void flushPendingNoteOffs(MidiTransport& transport);

  // --- Queries ---
  uint8_t     getNoteCount() const;
  bool        hasNotes() const;
  ArpDivision getDivision() const;
  ArpPattern  getPattern() const;
  float       getGateLength() const;
  float       getShuffleDepth() const;
  uint8_t     getShuffleTemplate() const;
  uint8_t     getBaseVelocity() const;
  uint8_t     getVelocityVariation() const;
  bool        consumeTickFlash();   // Returns true once after each arp step, then resets

private:
  // --- Configuration ---
  uint8_t     _channel;
  ArpPattern  _pattern;
  uint8_t     _octaveRange;
  ArpDivision _division;
  float       _gateLength;
  float       _shuffleDepth;
  uint8_t     _shuffleTemplate;    // 0-4, index into SHUFFLE_TEMPLATES[]
  uint8_t     _baseVelocity;
  uint8_t     _velocityVariation;

  // --- Note pile (stores padOrder positions 0-47, NOT MIDI notes) ---
  uint8_t _positions[MAX_ARP_NOTES];      // Sorted by padOrder position
  uint8_t _positionCount;
  uint8_t _positionOrder[MAX_ARP_NOTES];  // Chronological add order (for ARP_ORDER)
  uint8_t _orderCount;

  // --- Built sequence (positions × octaves, resolved at tick time) ---
  uint8_t _sequence[MAX_ARP_SEQUENCE];    // Encoded: pos + octave * 48
  uint8_t _sequenceLen;
  bool    _sequenceDirty;

  // --- Scale context for tick-time resolution ---
  ScaleConfig    _scale;
  const uint8_t* _padOrder;   // Pointer to global padOrder[48]

  // --- Playback state ---
  int16_t _stepIndex;
  bool    _playing;
  bool    _holdOn;

  // --- Event queue (static array, no heap allocation) ---
  // Each tick schedules up to 2 events (1 noteOn + 1 noteOff).
  // With long gate or fast division, many events coexist = overlap.
  PendingEvent _events[MAX_PENDING_EVENTS];

  // --- Reference counting for overlapping notes ---
  // Tracks how many active "instances" of each MIDI note are ringing.
  // MIDI noteOn is only sent when refcount goes 0→1 (first instance).
  // MIDI noteOff is only sent when refcount goes 1→0 (last instance ends).
  // This prevents the classic overlap bug where a late noteOff kills
  // a note that was just re-triggered by a newer step.
  uint8_t _noteRefCount[128];

  // --- Shuffle state ---
  // Step counter increments each arp step. Resets on:
  //   - play/stop toggle
  //   - pile goes from 0 to 1 note
  //   - pattern change
  // Used as index into groove template: template[counter % 16]
  uint8_t _shuffleStepCounter;   // Only used modulo 16, wraps at 255 (harmless)
  bool    _tickFlash;            // Set true on each tick(), consumed by LedController
  bool    _waitingForQuantize;  // True after play until quantize boundary reached
  uint8_t _quantizeMode;        // ArpStartMode (0=immediate, 1=beat, 2=bar)

  // --- Sequence building ---
  void rebuildSequence();

  // --- Event helpers ---
  // Find a free slot in _events[] and schedule. Returns false if queue full.
  bool scheduleEvent(uint32_t fireTimeUs, uint8_t note, uint8_t velocity);

  // Reference-counted MIDI send. Only sends actual MIDI message
  // on refcount transitions (0→1 for noteOn, 1→0 for noteOff).
  void refCountNoteOn(MidiTransport& transport, uint8_t note, uint8_t velocity);
  void refCountNoteOff(MidiTransport& transport, uint8_t note);
};

#endif // ARP_ENGINE_H
