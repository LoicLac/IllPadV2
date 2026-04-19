#ifndef CONTROL_PAD_MANAGER_H
#define CONTROL_PAD_MANAGER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

// Per-slot DSP state buffer size. Covers ~32 frames = ~32ms of look-back
// at a 1ms frame interval. sampleHoldMs settings > 32 clamp to max look-back.
#ifndef CTRL_RING_SIZE
#define CTRL_RING_SIZE 32
#endif

class MidiTransport;
struct SharedKeyboardState;

// Runtime slot : persisted config + dynamic state
struct ControlPadSlot {
  ControlPadEntry cfg;
  uint8_t lastCcValue;   // dedup + cleanup decisions
  uint8_t lastChannel;   // channel of last emission (for follow-bank handoff)
  bool    latchState;    // LATCH: current toggle state (boot = false)
  bool    wasPressed;    // per-slot edge tracking (independent of s_lastKeys)

  // V2 DSP state (CONTINUOUS only) :
  uint16_t emaAccum;              // fixed-point 8.8 accumulator for smooth EMA
  uint8_t  ring[CTRL_RING_SIZE];  // ring buffer of smoothed ccVal over last N frames
  uint8_t  ringWrIdx;             // write position modulo CTRL_RING_SIZE
  uint8_t  envStartValue;         // RETURN_TO_ZERO envelope start (0 = inactive)
  uint16_t envFramesRemaining;    // frames left in envelope
  uint16_t envFramesTotal;        // initial total (for linear interpolation)
};

class ControlPadManager {
public:
  ControlPadManager();

  // Wire up. Call once at boot, before loop().
  void begin(MidiTransport* transport);

  // Replace config from a loaded store. Rebuilds _slots[] + _isControlPadLut.
  // Skips entries where padIndex == CTRL_PAD_INVALID.
  // Resets runtime state (latchState=false, lastCcValue=0, etc.).
  void applyStore(const ControlPadStore& store);

  // Per-frame update. Call from Core 1 loop between handleHoldPad and
  // handlePadInput. currentBankChannel must be 0-7 (BankSlot.channel).
  void update(const SharedKeyboardState& state, bool leftHeld,
              uint8_t currentBankChannel);

  // O(1) query for music block : is this pad assigned as a control pad?
  bool isControlPad(uint8_t padIndex) const;

  // --- Tool 4 API (read-only access for screen rendering) ---
  uint8_t getCount() const;                        // 0..MAX_CONTROL_PADS
  const ControlPadSlot* getSlots() const;          // _slots, _count valid
  int8_t findSlotForPad(uint8_t padIndex) const;   // -1 if pad not assigned

private:
  MidiTransport* _transport;

  ControlPadSlot _slots[MAX_CONTROL_PADS];
  uint8_t        _count;
  bool           _isControlPadLut[NUM_KEYS];

  // Edge detection state (per-update)
  bool    _lastLeftHeld;
  uint8_t _lastChannel;  // 0-7, or 0xFF at boot (skip first-frame bank edge)

  // V2 : global DSP params (copied from store at applyStore)
  uint16_t _smoothMs;
  uint16_t _sampleHoldMs;
  uint16_t _releaseMs;

  // --- In-frame per-slot processing ---
  void _processSlot(uint8_t s, const SharedKeyboardState& state,
                    uint8_t currentBankChannel);

  // --- Transition handlers (per-mode handoff, spec §4.2) ---
  void _handleLeftPress(const SharedKeyboardState& state);
  void _handleLeftRelease(const SharedKeyboardState& state,
                          uint8_t currentBankChannel);
  void _handleBankSwitch(uint8_t oldCh, uint8_t newCh,
                         const SharedKeyboardState& state);

  // --- Helpers ---
  void    _emit(ControlPadSlot& slot, uint8_t ch, uint8_t val);
  uint8_t _scalePressure(uint8_t pressure, uint8_t deadzone) const;
  uint8_t _resolveChannel(const ControlPadSlot& slot,
                          uint8_t currentBankChannel) const;

  // --- Gate family predicate (MOMENTARY OR CONTINUOUS+RETURN_TO_ZERO) ---
  bool _isGate(const ControlPadSlot& slot) const;

  // --- V2 DSP helpers ---
  void     _tickReleaseEnvelopes();
  uint16_t _emaAlpha() const;                              // derive alpha (Q16) from _smoothMs
  uint8_t  _sampleHoldLookback(const ControlPadSlot& slot) const;
};

#endif // CONTROL_PAD_MANAGER_H
