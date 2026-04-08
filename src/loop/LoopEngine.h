#ifndef LOOP_ENGINE_H
#define LOOP_ENGINE_H

#include <stdint.h>
#include <string.h>
#include "../core/HardwareConfig.h"  // NUM_KEYS, LoopQuantMode, NUM_LOOP_QUANT_MODES, DEFAULT_LOOP_QUANT_MODE

class MidiTransport;

// =================================================================
// Loop constants — file scope
// =================================================================
static const uint8_t  LOOP_NOTE_OFFSET   = 36;    // GM kick drum base (padOrder 0 → note 36)
static const uint16_t MAX_LOOP_EVENTS    = 1024;  // Main event buffer (8 KB)
static const uint8_t  MAX_OVERDUB_EVENTS = 128;   // Overdub buffer (1 KB)
static const uint8_t  MAX_PENDING        = 48;    // Pending note queue (shuffle/chaos — generous per Budget Philosophy)
static const uint16_t TICKS_PER_BEAT     = 24;    // MIDI PPQN standard
static const uint16_t TICKS_PER_BAR      = 96;    // 4/4 assumption

// =================================================================
// LoopEvent — recorded pad event (noteOn or noteOff)
// =================================================================
// Stored in _events[] and _overdubBuf[]. Offset is in the RECORD
// timebase (microseconds from _recordStartUs). noteOn vs noteOff is
// encoded via velocity: velocity == 0 → noteOff.
struct LoopEvent {
    uint32_t offsetUs;    // Microseconds from loop start (record timebase)
    uint8_t  padIndex;    // 0-47
    uint8_t  velocity;    // 0 = noteOff, >0 = noteOn
    uint8_t  _pad[2];     // Alignment to 8 bytes
};
static_assert(sizeof(LoopEvent) == 8, "LoopEvent must be 8 bytes");

// =================================================================
// PendingNote — scheduled note emission with time offset
// =================================================================
// Used by the pending queue for shuffle/chaos scheduling. Both
// noteOn and noteOff flow through the queue, so gate length is
// preserved under shuffle (B1 design invariant).
struct PendingNote {
    uint32_t fireTimeUs;  // micros() timestamp when this event should fire
    uint8_t  note;        // MIDI note number (0-127)
    uint8_t  velocity;    // 0 = noteOff, >0 = noteOn (MIDI convention)
    bool     active;      // true = slot in use, false = free for reuse
};

// =================================================================
// LoopEngine — one loop instance (max MAX_LOOP_BANKS in system)
// =================================================================
class LoopEngine {
public:
    // --- State machine ---
    // EMPTY       : fresh engine, no events recorded
    // RECORDING   : capturing first pass into _events[]
    // PLAYING     : looping _events[] proportionally to live BPM
    // OVERDUBBING : playing AND capturing new events into _overdubBuf[]
    // STOPPED     : _events[] intact but not playing
    enum State : uint8_t {
        EMPTY       = 0,
        RECORDING   = 1,
        PLAYING     = 2,
        OVERDUBBING = 3,
        STOPPED     = 4
    };

    // --- Pending action for quantized transitions ---
    // Set by public transition methods when _quantizeMode != LOOP_QUANT_FREE,
    // consumed by tick() when the boundary is crossed.
    enum PendingAction : uint8_t {
        PENDING_NONE             = 0,
        PENDING_START_RECORDING  = 1,   // EMPTY       → RECORDING
        PENDING_STOP_RECORDING   = 2,   // RECORDING   → PLAYING (close + bar-snap)
        PENDING_START_OVERDUB    = 3,   // PLAYING     → OVERDUBBING
        PENDING_STOP_OVERDUB     = 4,   // OVERDUBBING → PLAYING (merge)
        PENDING_PLAY             = 5,   // STOPPED     → PLAYING
        PENDING_STOP             = 6    // PLAYING     → STOPPED (soft flush on boundary)
    };

    LoopEngine();

    // --- Config ---
    void begin(uint8_t channel);
    void clear(MidiTransport& transport);           // Hard flush — refcount + pending + events + overdub
    void setPadOrder(const uint8_t* padOrder);
    void setLoopQuantizeMode(uint8_t mode);         // LoopQuantMode: FREE/BEAT/BAR
    void setChannel(uint8_t ch);

    // --- State transitions (quantizable — snap to beat/bar when mode != FREE) ---
    void startRecording();
    void stopRecording(const uint8_t* keyIsPressed, const uint8_t* padOrder, float currentBPM);
    void startOverdub();
    void stopOverdub(const uint8_t* keyIsPressed, const uint8_t* padOrder, float currentBPM);
    void play(float currentBPM);
    void stop(MidiTransport& transport);            // PLAYING → STOPPED (soft flush; trailing pending ok)

    // --- State transitions (ALWAYS immediate, never quantized) ---
    void abortOverdub(MidiTransport& transport);    // OVERDUBBING → STOPPED (hard flush, discard overdub)
    void cancelOverdub();                           // OVERDUBBING → PLAYING (keep loop, discard overdub pass)
    void flushLiveNotes(MidiTransport& transport, uint8_t channel);

    // --- Core playback (called every loop iteration, from main.cpp) ---
    void tick(MidiTransport& transport, float currentBPM, uint32_t globalTick);
    void processEvents(MidiTransport& transport);

    // --- Recording input (called by processLoopMode when RECORDING or OVERDUBBING) ---
    void recordNoteOn(uint8_t padIndex, uint8_t velocity);
    void recordNoteOff(uint8_t padIndex);

    // --- Refcount helpers (called by processLoopMode for live-play deduplication) ---
    // Returns true on 0→1 (noteOn) / 1→0 (noteOff) — caller sends the actual MIDI.
    bool noteRefIncrement(uint8_t note);
    bool noteRefDecrement(uint8_t note);

    // --- Per-pad live-press tracking (AUDIT FIX B1 2026-04-06) ---
    // Mirror of MidiEngine::_lastResolvedNote[] pattern. Enables idempotent
    // cleanup sweeps (hold-release, bank-switch, panic) without depending on
    // s_lastKeys edge detection.
    void setLiveNote(uint8_t padIndex, uint8_t note);
    void releaseLivePad(uint8_t padIndex, MidiTransport& transport);

    // --- Note mapping ---
    // Returns 0xFF for unmapped pads (padOrder[i] == 0xFF).
    uint8_t padToNote(uint8_t padIndex) const;

    // --- Param setters (stubs — filled in Phase 5) ---
    void setShuffleDepth(float depth);
    void setShuffleTemplate(uint8_t tmpl);
    void setChaosAmount(float amount);
    void setVelPatternIdx(uint8_t idx);
    void setVelPatternDepth(float depth);
    void setBaseVelocity(uint8_t vel);
    void setVelocityVariation(uint8_t pct);

    // --- Getters ---
    State    getState() const;
    bool     isPlaying() const;
    bool     isRecording() const;            // RECORDING or OVERDUBBING (lock for bank switch)
    bool     hasPendingAction() const;       // For LED feedback: waiting quantize visual
    uint8_t  getLoopQuantizeMode() const;    // For LED feedback: FREE vs QUANTIZED color
    uint16_t getEventCount() const;

    // Tick flash flags (consume-on-read). Three independent flags let the LED
    // hierarchy render beat/bar/wrap distinctly.
    //   PLAYING/OVERDUBBING → derived from positionUs + _loopLengthBars
    //                         (only set when _quantizeMode != LOOP_QUANT_FREE)
    //   RECORDING           → derived from globalTick (no loop structure yet)
    //   EMPTY / STOPPED     → never set
    bool     consumeBeatFlash();
    bool     consumeBarFlash();
    bool     consumeWrapFlash();

    // Param getters (stubs — filled in Phase 5)
    float    getShuffleDepth() const;
    uint8_t  getShuffleTemplate() const;
    float    getChaosAmount() const;
    uint8_t  getVelPatternIdx() const;
    float    getVelPatternDepth() const;

private:
    // --- Config ---
    uint8_t        _channel      = 0;
    uint8_t        _quantizeMode = LOOP_QUANT_FREE;
    const uint8_t* _padOrder     = nullptr;

    // --- State machine ---
    State         _state         = EMPTY;
    PendingAction _pendingAction = PENDING_NONE;

    // --- Event buffers ---
    LoopEvent _events[MAX_LOOP_EVENTS];
    LoopEvent _overdubBuf[MAX_OVERDUB_EVENTS];
    uint16_t  _eventCount   = 0;
    uint16_t  _overdubCount = 0;
    uint16_t  _cursorIdx    = 0;

    // --- Playback timing anchors ---
    uint16_t _loopLengthBars = 0;
    uint32_t _recordStartUs  = 0;
    uint32_t _playStartUs    = 0;
    uint32_t _lastPositionUs = 0;
    float    _recordBpm      = 120.0f;   // BPM latched at stopRecording for proportional playback

    // --- Refcount + per-pad live-press tracking ---
    uint8_t _noteRefCount[128];
    uint8_t _liveNote[NUM_KEYS];   // Per-pad live note (0xFF = none). B1 fix.

    // --- Pending note queue (shuffle/chaos scheduling — noteOn AND noteOff) ---
    PendingNote _pending[MAX_PENDING];

    // --- Overdub active-pad tracking ---
    // True for pads that had a recordNoteOn during the current overdub session.
    // Used by doStopOverdub() to only inject noteOff for pads actually recorded
    // during this overdub (not for pads held from before — would be orphan).
    bool _overdubActivePads[48];

    // --- Pending action dispatcher stash (AUDIT FIX D-PLAN-1 2026-04-07) ---
    // Captured at the transition call site (stopRecording/stopOverdub/play),
    // consumed by the dispatcher in tick() when the quantize boundary crosses.
    // keyIsPressed is uint8_t[] (0/1) to match SharedKeyboardState.keyIsPressed
    // — bool[] would require an unsafe type-punned cast at every call site.
    const uint8_t* _pendingKeyIsPressed = nullptr;
    const uint8_t* _pendingPadOrder     = nullptr;
    float          _pendingBpm          = 120.0f;

    // --- Tick flash state (hierarchy: beat < bar < wrap) ---
    bool     _beatFlash          = false;
    bool     _barFlash           = false;
    bool     _wrapFlash          = false;
    uint32_t _lastBeatIdx        = 0;
    uint32_t _lastRecordBeatTick = 0xFFFFFFFF;   // Force first beat flash at first RECORDING tick

    // --- B1 pass 2: sentinel-based boundary crossing detection ---
    // Replaces the (globalTick % boundary == 0) exact-equality check that could
    // miss a boundary when ClockManager::generateTicks() catches up multiple
    // ticks in one call (up to 4 — see ClockManager.cpp:181-203). Parallel to
    // ArpEngine's own _lastDispatchedGlobalTick (B-001/B-CODE-1 fix).
    uint32_t _lastDispatchedGlobalTick = 0xFFFFFFFF;

    // --- Velocity params (per-bank, set via setters) ---
    uint8_t _baseVelocity      = 100;
    uint8_t _velocityVariation = 0;

    // --- Phase 5 effect stubs ---
    float   _shuffleDepth    = 0.0f;
    uint8_t _shuffleTemplate = 0;
    float   _chaosAmount     = 0.0f;
    uint8_t _velPatternIdx   = 0;
    float   _velPatternDepth = 0.0f;

    // --- Private transition implementations (the real work) ---
    void doStartRecording();
    void doStopRecording(const uint8_t* keyIsPressed, const uint8_t* padOrder, float currentBPM);
    void doStartOverdub();
    void doStopOverdub(const uint8_t* keyIsPressed, const uint8_t* padOrder, float currentBPM);
    void doPlay(float currentBPM);
    void doStop(MidiTransport& transport);

    // --- Private helpers ---
    uint32_t calcLoopDurationUs(float bpm) const;
    int32_t  calcShuffleOffsetUs(uint32_t eventOffsetUs, uint32_t recordDurationUs);
    int32_t  calcChaosOffsetUs(uint32_t eventOffsetUs);
    uint8_t  applyVelocityPattern(uint8_t origVel, uint32_t eventOffsetUs, uint32_t recordDurationUs);
    void     schedulePending(uint32_t fireTimeUs, uint8_t note, uint8_t velocity);
    void     flushActiveNotes(MidiTransport& transport, bool hard);
    void     sortEvents(LoopEvent* buf, uint16_t count);
    void     mergeOverdub();
};

#endif // LOOP_ENGINE_H
