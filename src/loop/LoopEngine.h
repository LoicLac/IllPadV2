#ifndef LOOP_ENGINE_H
#define LOOP_ENGINE_H

#include <stdint.h>
#include "../core/KeyboardData.h"

class MidiTransport;
class ClockManager;

// =================================================================
// LoopState — full state machine (spec §3 + §17)
// =================================================================
// Phase 2 implements: EMPTY/RECORDING/PLAYING/OVERDUBBING/STOPPED
//                     + transients WAITING_PLAY/WAITING_STOP
// Phase 6 adds:       WAITING_LOAD (slot load with quantize)
enum class LoopState : uint8_t {
  EMPTY        = 0,
  RECORDING    = 1,
  PLAYING      = 2,
  OVERDUBBING  = 3,
  STOPPED      = 4,
  WAITING_PLAY = 5,
  WAITING_STOP = 6,
};

// =================================================================
// LoopQuantize — values stored in BankTypeStore::quantize[bank]
//                when type == BANK_LOOP (validator KeyboardData.h:725-726)
// =================================================================
enum LoopQuantize : uint8_t {
  LOOP_QUANT_FREE = 0,   // No quantize, immediate action
  LOOP_QUANT_BEAT = 1,   // Next beat boundary (24 ticks)
  LOOP_QUANT_BAR  = 2,   // Next bar boundary (96 ticks) — default
};

// =================================================================
// Buffer caps (spec §8, §25 + symetric ArpEngine MAX_PENDING_EVENTS = 64)
// =================================================================
static const uint16_t MAX_LOOP_EVENTS              = 1024;  // main buffer per bank
static const uint8_t  MAX_LOOP_OVERDUB_EVENTS      = 128;   // overdub temp buffer
static const uint8_t  MAX_LOOP_PENDING_NOTEOFFS    = 64;    // scheduled noteOff queue (gates)

// =================================================================
// Tick boundaries (24 PPQN)
// =================================================================
static const uint32_t TICKS_PER_BEAT = 24;
static const uint32_t TICKS_PER_BAR  = 96;

// =================================================================
// LoopEvent — single captured pad action (noteOn or noteOff)
// =================================================================
// Stored in _events[] (main buffer) and _eventsAlternate[] (OD-Sync 1-level snapshot).
// Layout naturel ARM 32-bit : 8 B (uint32 timestampUs + 4× uint8). Live-sorted
// dans capturePadEvent (M2 décision Q4) — pas de compaction nécessaire.
// `velocity` strict baseVelocity au capture (M3 décision Q8), randomisation
// appliquée seulement au playback dans update().
struct LoopEvent {
  uint32_t timestampUs;  // µs offset from loop position 0 (event timeline within loop)
  uint8_t  padIndex;     // 0..NUM_KEYS-1 (raw key index, padOrder applied at play time)
  uint8_t  midiNote;     // pre-resolved MIDI note (MIDI_BASE_NOTE + padOrder[padIndex])
  uint8_t  velocity;     // 0 = noteOff event, 1..127 = noteOn event (baseVelocity strict)
  bool     active;       // false = slot vacant (live-sort sentinel, end-of-buffer marker)
};
static_assert(sizeof(LoopEvent) == 8, "LoopEvent must be exactly 8 B (ARM natural alignment)");

// =================================================================
// LoopPendingNoteOff — scheduled future noteOff (gate length or stuck-note flush)
// =================================================================
// Symmetric to ArpEngine::PendingEvent. Used by refCountNoteOn to schedule
// safety noteOff at end of loop, and by overdub clear-on-tap-REC.
struct LoopPendingNoteOff {
  uint32_t fireTimeUs;   // micros() absolute timestamp
  uint8_t  note;
  bool     active;
};

// =================================================================
// WaitingExit — one-shot signal émis par commitWaitingAction quand un transient
// WAITING_PLAY/STOP commit (boundary tick atteint). Consommé par main.cpp pour
// trigger EVT_PLAY/STOP qui clear l'event overlay EVT_WAITING (PTN_CROSSFADE_COLOR
// continuous — caller must clear, cf LedController.cpp:791-796).
// Symétrique aux flash flags consumeBarFlash/consumeWrapFlash (one-shot pattern).
// =================================================================
enum class WaitingExit : uint8_t { NONE = 0, TO_PLAY = 1, TO_STOP = 2 };

// =================================================================
// LoopEngine — one loop instance per BANK_LOOP slot (max MAX_LOOP_BANKS)
// =================================================================
class LoopEngine {
public:
  LoopEngine();

  // --- Configuration (set at boot from BankManager / NvsManager) ---
  void setChannel(uint8_t ch);
  void setQuantize(LoopQuantize q);              // from BankTypeStore::quantize[bank]
  void setPadOrder(const uint8_t* padOrder);     // for MIDI note resolution
  void setClockManager(ClockManager* clock);     // for getSmoothedBPM + getCurrentTick
  void setControlPads(uint8_t recPad, uint8_t playStopPad, uint8_t clearPad);
  void setBaseVelocity(uint8_t vel);
  void setVelocityVariation(uint8_t pct);
  void setClearLoopTimerMs(uint16_t ms);         // from SettingsStore::clearLoopTimerMs (Tool 6)

  // --- Transport actions (called by processLoopMode) ---
  // Tap REC : state machine per spec §7 §8 + Q5 §28 + Master Sync §3.2
  //   EMPTY    → RECORDING (arms capture, _recordStartUs set on first pad press
  //              avec anchor master tick selon quantize per Master Sync §3.1)
  //   RECORDING→ FREE   : closeRecordingImmediate() (tap-to-tap, → PLAYING)
  //              BEAT/BAR: arme PENDING_CLOSE, capture continue jusqu'au master
  //              boundary tick, puis commitRecordingClose() (→ PLAYING)
  //   PLAYING  → OVERDUBBING (snapshot _eventsAlternate + immediate-merge capture
  //              direct dans _events[], cf Illpad_OD_Sync.md §3)
  //   STOPPED  → PLAYING + OVERDUBBING simultaneously (Q5 — reprise + arm + snapshot)
  //   OVERDUB  → commitOverdubExit (B-N2 held pads inject, _eventsAlternate
  //              préservé pour Undo)
  void tapRec(MidiTransport& transport);

  // Tap PLAY/STOP : state machine per spec §9 §17
  //   EMPTY      → no-op
  //   STOPPED    → quantized PLAY (or immediate if FREE) → PLAYING / WAITING_PLAY
  //   PLAYING    → quantized STOP (or immediate if FREE) → STOPPED / WAITING_STOP
  //   OVERDUBBING→ abandon overdub, stay PLAYING (spec §8)
  //   WAITING_*  → cancel waiting (per §17 concurrent gestures table)
  // currentKeys = keyIsPressed (FG context) or nullptr (BG context, multi-bank).
  // Param actuellement non utilisé (réservé pour usages futurs ; ne pas dépendre du contenu).
  void tapPlayStop(MidiTransport& transport, const uint8_t* currentKeys = nullptr);

  // Long-press CLEAR : state machine per spec §9
  //   Any except RECORDING/OVERDUBBING with held-for-clearLoopTimerMs → EMPTY (wipe buffer)
  // Called by processLoopMode tracker (rising edge starts timer, sustained press fires this).
  void longPressClear(MidiTransport& transport);

  // --- Recording (called by processLoopMode on musical pad edges) ---
  // Capture a noteOn (rising edge) or noteOff (falling edge) into main buffer
  // (RECORDING) or overdub buffer (OVERDUBBING). No-op if state != REC/OD.
  // velocity = 0 for noteOff capture, > 0 for noteOn capture.
  void capturePadEvent(uint8_t padIndex, uint8_t velocity, MidiTransport& transport);

  // --- Update (called from main loop EVERY iteration, µs-driven) ---
  // Walks main buffer firing events whose scaled timestamp <= current loop position.
  // Handles wrap (consumeWrapFlash) and bar crossing (consumeBarFlash).
  // Resolves WAITING_PLAY/WAITING_STOP boundaries via ClockManager ticks.
  // Drains pending noteOff queue (gates + stuck-note safety).
  void update(MidiTransport& transport);

  // --- Emergency flush ---
  // Cancel all pending events + sweep refcount → noteOff + state → STOPPED.
  // Called by midiPanic() and by bank switch on REC/OD lock (spec §23.2).
  void flushPendingNoteOffs(MidiTransport& transport);

  // --- Background transition hook (Audit-fix B-N1 / R-N1) ---
  // Appelé depuis main.cpp quand cette bank LOOP passe de FG à BG (bank switch out).
  // 1. Pour chaque pad encore physiquement enfoncé (_padHeldLive[pad] == true) :
  //    refCountNoteOff + reset flag. Évite stuck note au DAW (invariant §23.1).
  // 2. Reset CLEAR press tracker (_clearPressStartMs = 0, _clearFired = false)
  //    pour éviter wipe instantané au retour FG si CLEAR encore tenu (spec §9).
  // NE CHANGE PAS `_state` — la bank LOOP en BG doit pouvoir continuer son playback.
  void onBackgroundTransition(MidiTransport& transport);

  // --- Queries ---
  LoopState getState() const          { return _state; }
  bool      isPlaying() const         { return _state == LoopState::PLAYING || _state == LoopState::OVERDUBBING; }
  bool      isRecording() const       { return _state == LoopState::RECORDING; }
  bool      isOverdubbing() const     { return _state == LoopState::OVERDUBBING; }
  bool      isWaiting() const         { return _state == LoopState::WAITING_PLAY || _state == LoopState::WAITING_STOP; }
  bool      isLocked() const          { return _state == LoopState::RECORDING || _state == LoopState::OVERDUBBING; }
  bool      hasContent() const        { return _eventCount > 0; }
  uint16_t  getEventCount() const     { return _eventCount; }
  uint32_t  getLoopDurationUs() const { return _loopDurationUs; }
  uint16_t  getRecordBpm() const      { return _recordBpm; }

  // --- LED flash hooks (consumed once by LedController::renderBankLoop) ---
  // consumeBarFlash : set true by update() on bar crossing within loop (every 96 ticks scaled)
  // consumeWrapFlash : set true by update() on loop wrap (position → 0)
  bool consumeBarFlash();
  bool consumeWrapFlash();

  // --- WAITING exit signal (consumed once par main.cpp pour trigger EVT_PLAY/STOP) ---
  // Set par commitWaitingAction quand WAITING_PLAY → PLAYING ou WAITING_STOP → STOPPED.
  // Symétrique du EVT_WAITING émis à l'entrée du WAITING_* (clear l'event overlay).
  WaitingExit consumeWaitingExit();

  // --- Control pad lookup (used by processLoopMode) ---
  bool isLoopControlPad(uint8_t padIndex) const;
  uint8_t getRecPad() const       { return _recPad; }
  uint8_t getPlayStopPad() const  { return _playStopPad; }
  uint8_t getClearPad() const     { return _clearPad; }

  // --- CLEAR press tracking (called by processLoopMode on CLEAR pad edges) ---
  void notifyClearPressStart(uint32_t nowMs);   // rising edge
  void notifyClearPressEnd();                   // falling edge (cancels timer)
  bool isClearHoldFired(uint32_t nowMs) const;  // true once threshold passed

private:
  // --- Configuration ---
  uint8_t          _channel;
  LoopQuantize     _quantize;
  const uint8_t*   _padOrder;
  ClockManager*    _clock;
  uint8_t          _recPad;       // 0xFF = unassigned (LoopPadStore)
  uint8_t          _playStopPad;  // 0xFF = unassigned
  uint8_t          _clearPad;     // 0xFF = unassigned
  uint8_t          _baseVelocity;
  uint8_t          _velocityVariation;
  uint16_t         _clearLoopTimerMs;  // from SettingsStore (default 500, range 200-1500)

  // --- State machine ---
  LoopState        _state;

  // --- Recording timebase ---
  uint32_t         _recordStartUs;     // micros() at first pad press in RECORDING (offset 0)
  uint32_t         _recordEndUs;       // micros() at REC tap (un-snapped end)
  uint16_t         _recordBpm;         // latched at first pad press (invariant §23.5)
  bool             _recordFirstPressDone;  // false until first capturePadEvent in RECORDING

  // --- Loop structure (set at commitRecordingClose / closeRecordingImmediate) ---
  uint32_t         _loopDurationUs;    // bar-snapped duration (after deadzone snap + rescale)
  uint16_t         _loopBars;          // 1..64 (post snap)

  // --- Main event buffer (committed loop content) ---
  LoopEvent        _events[MAX_LOOP_EVENTS];
  uint16_t         _eventCount;        // number of active events (compacted, contiguous 0.._eventCount-1)

  // --- Playback timeline (intégration incrémentale BPM, M-fix B1) ---
  // _playStartUs : conservé pour compat / debug (timestamp d'entrée en PLAYING), pas utilisé pour position.
  // _scaledElapsedUs : position cumulative en µs scalée par BPM ratio. Pas modulée.
  //   À chaque update() : delta = nowUs - _lastUpdateUs ; _scaledElapsedUs += delta × liveBpm / recordBpm.
  //   Wrap détecté quand _scaledElapsedUs >= _loopDurationUs → soustraire loopDuration + _wrapFlash + reset playNextEventIdx.
  // _playPositionUs : projection actuelle dans le loop (0.._loopDurationUs-1), dérivée de _scaledElapsedUs.
  uint32_t         _playStartUs;       // debug / timestamp PLAYING entry
  uint64_t         _scaledElapsedUs;   // cumulative scaled µs since PLAYING entry (uint64 anti-overflow long sessions)
  uint32_t         _lastUpdateUs;      // micros() at previous update() call, for delta computation
  uint32_t         _playPositionUs;    // current scaled position within loop (0.._loopDurationUs)
  uint16_t         _playNextEventIdx;  // next event to fire (sorted by timestampUs — live-sort invariant)
  uint16_t         _lastBarIndex;      // for bar crossing detection (which bar of loop we last reported)

  // --- Pending noteOff queue (gate length + stuck-note safety) ---
  LoopPendingNoteOff _pendingNoteOffs[MAX_LOOP_PENDING_NOTEOFFS];

  // --- Refcount per MIDI note (symmetric ArpEngine pattern Q2 §28) ---
  uint8_t          _noteRefCount[128];

  // --- LED flash flags ---
  bool             _barFlash;
  bool             _wrapFlash;

  // --- WAITING exit signal (set par commitWaitingAction, consommé par main.cpp) ---
  WaitingExit      _waitingExit;

  // --- Quantize transient ---
  uint32_t         _waitingTargetTick;   // ClockManager tick at which WAITING_PLAY/STOP commits

  // --- CLEAR press tracker (set by notifyClearPressStart) ---
  uint32_t         _clearPressStartMs;   // 0 = not pressing
  bool             _clearFired;          // true once threshold fired (avoid re-fire)

  // --- Per-pad live-press tracker (Audit-fix B-N1 / B-N2 / R-N1) ---
  // Set true à chaque live monitor noteOn (peu importe le state : EMPTY / STOPPED /
  // PLAYING / RECORDING / OVERDUBBING / WAITING_*). Reset au falling edge.
  // Trois consumers :
  //   - commitRecordingClose / closeRecordingImmediate → flushHeldPadsAsNoteOffs : inject noteOff fin de loop.
  //   - commitOverdubExit → inject noteOff dans _events à _playPositionUs (B-N2 fix, déplacée de mergeOverdub).
  //   - onBackgroundTransition → refCountNoteOff direct + reset flag (B-N1 fix).
  // Remplace l'ancien _padHeldLive[] qui ne couvrait que RECORDING (insuffisant).
  bool             _padHeldLive[NUM_KEYS];

  // --- Auto-Stop PENDING_CLOSE (spec Illpad_Master_Sync.md §3.2) ---
  // Set true par tapRec sur RECORDING quand quantize != FREE.
  // Consommé par update() phase 0 au boundary tick → commitRecordingClose.
  // L'état reste RECORDING pendant la fenêtre (LED Coral solide, capture continue α).
  bool             _recordingPendingClose;
  uint32_t         _recordingPendingCloseTick;   // tick master cible (boundary)

  // --- OD-Sync : snapshot 1-level Undo/Redo toggle (spec Illpad_OD_Sync.md §2) ---
  // _eventsAlternate : "l'autre version" du buffer pour swap Undo/Redo.
  // - Au tap REC sur PLAYING/STOPPED/WAITING_STOP → snapshot du pré-OD content.
  // - Au swap (Cancel pendant OD via CLEAR, ou Undo/Redo court CLEAR en PLAYING/STOPPED) :
  //   échange _events ↔ _eventsAlternate avec diff musical par note (§6).
  // _alternateValid : gating — false si aucun OD ne s'est produit depuis wipe/boot.
  LoopEvent        _eventsAlternate[MAX_LOOP_EVENTS];
  uint16_t         _eventsAlternateCount;
  bool             _alternateValid;

  // --- Helpers ---
  // Recording (Master Sync spec Illpad_Master_Sync.md §3.2 + §3.3)
  void startRecording(MidiTransport& transport);
  // BEAT/BAR quantize : tapRec REC sur RECORDING set _recordingPendingClose,
  // update() phase 0 commit au boundary tick via commitRecordingClose.
  void commitRecordingClose(MidiTransport& transport);
  // FREE quantize : tapRec REC sur RECORDING close immédiat tap-to-tap.
  // Aussi fallback safety si !_clock dans commitRecordingClose.
  void closeRecordingImmediate(MidiTransport& transport);
  void flushHeldPadsAsNoteOffs(uint32_t timestampUs);
  // Live-sort insertion (M2 décision Q4) — buffer maintenu trié à l'insertion.
  // Retourne true si inséré, false si buffer plein (drop silent per spec §8).
  bool insertEventSorted(LoopEvent* buffer, uint16_t& count, uint16_t cap,
                          uint32_t timestampUs, uint8_t padIndex, uint8_t midiNote, uint8_t velocity);

  // Playback / scheduler (B1 intégration incrémentale)
  // startPlayback(transport, nowUs) : signature étendue avec nowUs capturé en début d'update
  // pour éviter underflow uint32 sur enchaînement commitWaitingAction → startPlayback (B3 fix).
  // Position de playback maintenue par accumulation incrementale dans update() (B1 audit fix).
  void startPlayback(MidiTransport& transport, uint32_t nowUs);
  void stopPlayback(MidiTransport& transport, bool flushNotes);
  bool scheduleNoteOff(uint32_t fireTimeUs, uint8_t note);
  void drainPendingNoteOffs(MidiTransport& transport, uint32_t nowUs);
  void refCountNoteOn(MidiTransport& transport, uint8_t note, uint8_t velocity);
  void refCountNoteOff(MidiTransport& transport, uint8_t note);

  // OD-Sync (spec Illpad_OD_Sync.md §3.3) : exit commit (tap REC pendant OVERDUBPER).
  // Held pads → noteOff inject dans _events à _playPositionUs (B-N2 déplacée de
  // l'ancienne mergeOverdub). _eventsAlternate préservé pour post-Undo via CLEAR
  // court en PLAYING/STOPPED. État → PLAYING.
  void commitOverdubExit(MidiTransport& transport);

  // Quantize boundary detection
  uint32_t computeNextBoundaryTick(LoopQuantize q) const;  // ClockManager tick of next boundary
  // commitWaitingAction(transport, nowUs) : nowUs propagé pour B3 fix (passé à startPlayback).
  void commitWaitingAction(MidiTransport& transport, uint32_t nowUs);

  // MIDI note resolution
  uint8_t resolvePadToMidiNote(uint8_t padIndex) const;

  // Velocity randomization (symmetric processNormalMode main.cpp:676-680).
  // Appliquée uniquement au playback dans update() (M3 décision Q8 : pas au capture).
  uint8_t applyVelocityVariation(uint8_t baseVel) const;

  // --- OD-Sync helpers (spec Illpad_OD_Sync.md §6.3) ---
  // isNoteOnAt : état "audible" d'une note à position pos dans un buffer trié.
  // Walk les events ≤ pos, suit les transitions noteOn (vel > 0) / noteOff (vel == 0).
  // Le dernier event matching détermine l'état audible à pos.
  bool isNoteOnAt(LoopEvent* buf, uint16_t count, uint8_t note, uint32_t pos) const;
  // findLatestVelAt : velocity du dernier noteOn matching note ≤ pos.
  // Fallback DEFAULT_BASE_VELOCITY si aucun event matching.
  uint8_t findLatestVelAt(LoopEvent* buf, uint16_t count, uint8_t note, uint32_t pos) const;
};

#endif // LOOP_ENGINE_H
