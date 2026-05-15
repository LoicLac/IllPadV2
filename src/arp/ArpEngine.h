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

// =================================================================
// ARPEG_GEN — generative sequence buffer constants (spec §11, §13)
// NUM_GEN_POSITIONS lives in KeyboardData.h (shared with PotRouter).
// =================================================================
static const uint16_t MAX_ARP_GEN_SEQUENCE = 96;  // longest grid position seqLen

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
  void setProximityFactor(uint8_t x10);        // V4 : 4..20 (proximity x10 = 0.4..2.0), default 4
  void setEcart(uint8_t e);                    // V4 : 1..12 (Tool 5 override de TABLE_GEN_SEQ_LEN ecart), default 5

  // --- ARPEG_GEN runtime params (Phase 5 Task 8 + V4 Task 22 retune) ---
  // Grid position 0..NUM_GEN_POSITIONS-1 (8 zones V4) — pot R2+hold balaye via PotRouter.
  // Lookup TABLE_GEN_SEQ_LEN[pos] -> seqLen. Ecart vient désormais de _ecart (Tool 5 override).
  void setGenPosition(uint8_t pos);
  // Mutation level 1..4 — alias on _octaveRange semantic when ARPEG_GEN. Pad oct change.
  // 1 = lock, 2 = 1/16, 3 = 1/8, 4 = 1/4 mutation step rate.
  void setMutationLevel(uint8_t level);

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
  uint8_t     getGenPosition() const;          // ARPEG_GEN grid position 0..14
  uint8_t     getMutationLevel() const;        // ARPEG_GEN mutation level 1..4
  uint8_t     getProximityFactor() const;      // V4 : prox_factor x10 (4..20)
  uint8_t     getEcart() const;                // V4 : walk ecart (1..12)

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
  uint8_t    _proximityFactorx10; // V4 : ARPEG_GEN per-bank proximity walk factor x10 (4..20), default 4
  uint8_t    _ecart;              // V4 : ARPEG_GEN per-bank walk ecart (1..12, override TABLE), default 5

  // --- ARPEG_GEN sequence buffers + cached pile metrics (spec §11, §14) ---
  int8_t     _sequenceGen[MAX_ARP_GEN_SEQUENCE];  // generative sequence (signed degrees)
  int8_t     _pileDegrees[MAX_ARP_NOTES];         // cached pile degrees, recomputed on pile/scale change
  uint8_t    _pileDegreeCount;                    // valid entries in _pileDegrees[]
  int8_t     _pileLo;                             // min(_pileDegrees[0..count-1])
  int8_t     _pileHi;                             // max(_pileDegrees[0..count-1])
  uint16_t   _seqLenGen;                          // current seqLen from TABLE_GEN_SEQ_LEN[_genPosition]
  uint8_t    _genPosition;                        // 0..14 (grid index, R2+hold via Phase 6)
  uint8_t    _mutationLevel;                      // 1..4 (1 = lock, 2 = 1/16, 3 = 1/8, 4 = 1/4)
  int16_t    _stepIndexGen;                       // read pointer into _sequenceGen[]
  uint16_t   _mutationStepCounter;                // global counter, used by maybeMutate (Task 11)

  // --- State classification ---
  ArpState currentState() const;

  // --- Step execution (core of tick) ---
  void executeStep(MidiTransport& transport, uint32_t stepDurationUs);

  // Common note-output helper : velocity + gate + shuffle offset + scheduleEvent +
  // refCountNoteOn. Shared by CLASSIC and GENERATIVE paths (DRY).
  void executeStepNote(MidiTransport& transport, uint32_t stepDurationUs, uint8_t finalNote);

  // --- Sequence building ---
  void rebuildSequence();

  // --- ARPEG_GEN helpers (Phase 5) ---
  // Recompute _pileDegrees[] / _pileLo / _pileHi from _positions + _padOrder + _scale.
  // Called on addPadPosition / removePadPosition / setScaleConfig.
  void recomputePileDegrees();

  // Pick next degree via weighted walk (spec §12, §16). Returns prev (fallback) if pool empty.
  //   prev          = previous degree in sequence (signed).
  //   E             = max ecart |d - prev|. Initial generation uses E_init = max(E_pot, pile_span).
  //   useScalePool  = true  : pool = pile ∪ scale_window[prev-E..prev+E], pile bonus active (mutation path).
  //                   false : pool = pile only, no bonus (initial generation path).
  int8_t pickNextDegree(int8_t prev, uint8_t E, bool useScalePool);

  // (Re)generate _sequenceGen[0.._seqLenGen-1] from the pile + grid position (spec §14, §17).
  // Pre-condition : _pileDegreeCount > 0 — otherwise sets _seqLenGen=0 and returns.
  // Triggered by _sequenceGenDirty on : pile 0->1, clearAllNotes, CLASSIC->GENERATIVE switch.
  void seedSequenceGen();

  // Mutation step (spec §15, §20). Called per executed step in GENERATIVE mode.
  // Lookup mutation rate from _mutationLevel : 1=lock (no-op), 2=1/16, 3=1/8, 4=1/4.
  // When triggered, picks a uniform random index in _sequenceGen[] and rewrites it via
  // pickNextDegree(prev, E_pot, useScalePool=true). E_pot = _ecart (Tool 5 override, V4).
  void maybeMutate(uint16_t globalStepCount);

  // --- Event helpers ---
  bool scheduleEvent(uint32_t fireTimeUs, uint8_t note, uint8_t velocity);
  void refCountNoteOn(MidiTransport& transport, uint8_t note, uint8_t velocity);
  void refCountNoteOff(MidiTransport& transport, uint8_t note);
};

#endif // ARP_ENGINE_H
