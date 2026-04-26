#ifndef ARP_ENGINE_H
#define ARP_ENGINE_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../midi/GrooveTemplates.h"

class MidiTransport;

// =================================================================
// ArpState — named states derived from runtime flags
// =================================================================
// Read-only classification. Does NOT replace the flags —
// just names the current combination for clearer control flow.

enum class ArpState {
  IDLE,                // _positionCount == 0
  WAITING_QUANTIZE,    // waiting for beat boundary before first step
  PLAYING,             // actively stepping (OFF auto-play or ON captured)
};

// =================================================================
// EngineMode — selects sequence semantics
// =================================================================
// CLASSIC    : ARPEG bank — uses _sequence[] built by rebuildSequence() from _pattern.
// GENERATIVE : ARPEG_GEN bank — uses _sequenceGen[] built from walk + mutation
//              (Phase 5 Tasks 8-12 implement the algo). Phase 4 ships the field
//              and boot wiring only ; tick() branching arrives in Task 12.

enum class EngineMode : uint8_t {
  CLASSIC    = 0,
  GENERATIVE = 1
};

// =================================================================
// Event Queue — time-based noteOn/noteOff scheduling
// =================================================================

static const uint8_t MAX_PENDING_EVENTS = 64;

struct PendingEvent {
  uint32_t fireTimeUs;
  uint8_t  note;
  uint8_t  velocity;     // 0 = noteOff, >0 = noteOn
  bool     active;
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
  void setShuffleTemplate(uint8_t tmpl);       // 0-9, index into groove templates
  void setBaseVelocity(uint8_t vel);           // 1-127
  void setVelocityVariation(uint8_t pct);      // 0-100
  void setStartMode(uint8_t mode);             // ArpStartMode (0=immediate, 1=beat)

  // --- Engine mode (CLASSIC = ARPEG, GENERATIVE = ARPEG_GEN) ---
  // Set at boot from BankType. D4 (plan §0) : Tool 5 type switch takes effect at next reboot,
  // so runtime engine-mode reassignment is not needed.
  void       setEngineMode(BankType type);
  EngineMode getEngineMode() const;

  // --- ARPEG_GEN per-bank params (Phase 4 stubs ; Phase 5 Tasks 9-11 consume these) ---
  void setBonusPile(uint8_t x10);              // 10..20 (bonus_pile x10), defaults 15
  void setMarginWalk(uint8_t margin);          // 3..12 degres, defaults 7

  // --- Pile management ---
  // Stores padOrder positions (0-47), NOT MIDI notes.
  // Resolution to MIDI notes happens at tick time via current ScaleConfig.
  void addPadPosition(uint8_t padOrderPos);
  void removePadPosition(uint8_t padOrderPos);
  void clearAllNotes(MidiTransport& transport);

  // --- Scale/pad context (set by main loop, used at tick for note resolution) ---
  void setScaleConfig(const ScaleConfig& scale);
  void setPadOrder(const uint8_t* padOrder);

  // --- Play/Stop toggle (hold pad OR LEFT + double-tap bank pad) ---
  // Stop → Play: if paused pile has notes, relaunch; clears paused flag.
  // Play → Stop: fingers down (excl. holdPad) → pile cleared, live mode.
  //              no fingers → pile kept, paused flag armed.
  // keyIsPressed == nullptr → treated as "no fingers" (BG bank: pads feed FG).
  void setCaptured(bool captured, MidiTransport& transport,
                   const uint8_t* keyIsPressed, uint8_t holdPadIdx);
  bool isCaptured() const;
  bool isPlaying() const;
  bool isPaused() const;

  // --- Core tick (called by ArpScheduler when a step fires) ---
  void tick(MidiTransport& transport, uint32_t stepDurationUs,
            uint32_t currentTick, uint32_t globalTick);

  // --- Event processing (called every loop iteration by ArpScheduler) ---
  void processEvents(MidiTransport& transport);

  // --- Emergency flush (called on stop, clear, etc.) ---
  // Immediately fires all pending noteOffs, sweeps refcount, sets _playing=false.
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
  bool        consumeTickFlash();

private:
  // --- Configuration ---
  uint8_t     _channel;
  ArpPattern  _pattern;
  uint8_t     _octaveRange;
  ArpDivision _division;
  float       _gateLength;
  float       _shuffleDepth;
  uint8_t     _shuffleTemplate;
  uint8_t     _baseVelocity;
  uint8_t     _velocityVariation;

  // --- Note pile (stores padOrder positions 0-47, NOT MIDI notes) ---
  uint8_t _positions[MAX_ARP_NOTES];
  uint8_t _positionCount;
  uint8_t _positionOrder[MAX_ARP_NOTES];
  uint8_t _orderCount;

  // --- Built sequence (positions x octaves, resolved at tick time) ---
  uint8_t _sequence[MAX_ARP_SEQUENCE];
  uint8_t _sequenceLen;
  bool    _sequenceDirty;

  // --- Scale context for tick-time resolution ---
  ScaleConfig    _scale;
  const uint8_t* _padOrder;

  // --- Playback state ---
  int16_t _stepIndex;
  bool    _playing;
  bool    _captured;      // true = ON (pile frozen), false = OFF (live)
  bool    _pausedPile;    // pile preserved after ON→OFF with no fingers

  // --- Event queue ---
  PendingEvent _events[MAX_PENDING_EVENTS];

  // --- Reference counting for overlapping notes ---
  uint8_t _noteRefCount[128];

  // --- Shuffle state ---
  uint16_t _shuffleStepCounter;
  bool    _tickFlash;
  bool    _waitingForQuantize;
  uint8_t _quantizeMode;        // ArpStartMode (0=immediate, 1=beat)

  // Boundary crossing detection for quantized start.
  // Value 0xFFFFFFFF = sentinel: first tick initializes tracking.
  uint32_t _lastDispatchedGlobalTick;

  // --- Engine mode (CLASSIC vs GENERATIVE) ---
  EngineMode _engineMode;
  bool       _sequenceGenDirty;   // marked true on CLASSIC->GENERATIVE transition (Phase 5 Task 10 consumes)
  uint8_t    _bonusPilex10;       // ARPEG_GEN per-bank, 10..20 (defaults 15)
  uint8_t    _marginWalk;         // ARPEG_GEN per-bank, 3..12 (defaults 7)

  // --- State classification ---
  ArpState currentState() const;

  // --- Step execution (core of tick) ---
  void executeStep(MidiTransport& transport, uint32_t stepDurationUs);

  // --- Sequence building ---
  void rebuildSequence();

  // --- Event helpers ---
  bool scheduleEvent(uint32_t fireTimeUs, uint8_t note, uint8_t velocity);
  void refCountNoteOn(MidiTransport& transport, uint8_t note, uint8_t velocity);
  void refCountNoteOff(MidiTransport& transport, uint8_t note);
};

#endif // ARP_ENGINE_H
