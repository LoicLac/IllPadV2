#include "ControlPadManager.h"
#include "../core/MidiTransport.h"
#include "../core/KeyboardData.h"  // SharedKeyboardState definition
#include <string.h>  // memset

ControlPadManager::ControlPadManager()
  : _transport(nullptr), _count(0),
    _lastLeftHeld(false), _lastChannel(0xFF),
    _smoothMs(0), _sampleHoldMs(0), _releaseMs(0) {
  memset(_isControlPadLut, 0, sizeof(_isControlPadLut));
  memset(_slots, 0, sizeof(_slots));
}

void ControlPadManager::begin(MidiTransport* transport) {
  _transport = transport;
}

void ControlPadManager::applyStore(const ControlPadStore& store) {
  _count = 0;
  memset(_isControlPadLut, 0, sizeof(_isControlPadLut));

  // V2 : copy globals (clamped by validator already, defensive min)
  _smoothMs     = store.smoothMs;
  _sampleHoldMs = store.sampleHoldMs;
  _releaseMs    = store.releaseMs;

  uint8_t n = store.count;
  if (n > MAX_CONTROL_PADS) n = MAX_CONTROL_PADS;

  for (uint8_t i = 0; i < n; i++) {
    const ControlPadEntry& e = store.entries[i];
    if (e.padIndex == CTRL_PAD_INVALID) continue;
    if (e.padIndex >= NUM_KEYS)         continue;

    ControlPadSlot& slot = _slots[_count];
    slot.cfg         = e;
    slot.lastCcValue = 0;
    slot.lastChannel = 0;
    slot.latchState  = false;
    slot.wasPressed  = false;
    // V2 DSP state reset
    slot.emaAccum            = 0;
    memset(slot.ring, 0, sizeof(slot.ring));
    slot.ringWrIdx           = 0;
    slot.pressedFrames       = 0;
    slot.envStartValue       = 0;
    slot.envFramesRemaining  = 0;
    slot.envFramesTotal      = 0;

    _isControlPadLut[e.padIndex] = true;
    _count++;
  }
}

bool ControlPadManager::isControlPad(uint8_t padIndex) const {
  if (padIndex >= NUM_KEYS) return false;
  return _isControlPadLut[padIndex];
}

uint8_t ControlPadManager::getCount() const { return _count; }
const ControlPadSlot* ControlPadManager::getSlots() const { return _slots; }

int8_t ControlPadManager::findSlotForPad(uint8_t padIndex) const {
  for (uint8_t s = 0; s < _count; s++) {
    if (_slots[s].cfg.padIndex == padIndex) return (int8_t)s;
  }
  return -1;
}

// -------------------------------------------------------------
// update : edge detection + transition handling + per-slot emission
// -------------------------------------------------------------
void ControlPadManager::update(const SharedKeyboardState& state,
                               bool leftHeld, uint8_t currentBankChannel) {
  bool firstFrame      = (_lastChannel == 0xFF);
  bool leftPressEdge   =  leftHeld  && !_lastLeftHeld;
  bool leftReleaseEdge = !leftHeld  &&  _lastLeftHeld;
  bool bankSwitchEdge  = !firstFrame && (currentBankChannel != _lastChannel);

  if (leftPressEdge)   _handleLeftPress(state);
  if (leftReleaseEdge) _handleLeftRelease(state, currentBankChannel);
  // Bank-switch handoff only fires when LEFT is fully off (not during LEFT-held
  // instant NORMAL switches, not duplicating LEFT-release re-sync). See
  // ControlPadManager design review C1+C2 : concurrent edges produce ghost CCs
  // otherwise.
  if (bankSwitchEdge && !leftHeld && !leftReleaseEdge) {
    _handleBankSwitch(_lastChannel, currentBankChannel, state);
  }

  // V2 : tick any active release envelopes (regardless of leftHeld — envelope
  // continues even if user presses LEFT mid-fade ; user can cancel by re-pressing
  // the pad which resets envFramesRemaining in _processSlot).
  _tickReleaseEnvelopes();

  if (!leftHeld) {
    for (uint8_t s = 0; s < _count; s++) {
      _processSlot(s, state, currentBankChannel);
    }
  }

  // Sync per-slot wasPressed (always, even during LEFT held)
  for (uint8_t s = 0; s < _count; s++) {
    _slots[s].wasPressed = state.keyIsPressed[_slots[s].cfg.padIndex];
  }
  _lastLeftHeld = leftHeld;
  _lastChannel  = currentBankChannel;
}

// -------------------------------------------------------------
// _processSlot : per-mode in-frame emission (LEFT = off)
// -------------------------------------------------------------
void ControlPadManager::_processSlot(uint8_t s,
                                     const SharedKeyboardState& state,
                                     uint8_t currentBankChannel) {
  ControlPadSlot& slot = _slots[s];
  uint8_t pad          = slot.cfg.padIndex;
  bool    pressed      = state.keyIsPressed[pad];
  bool    wasPressed   = slot.wasPressed;
  uint8_t targetCh     = _resolveChannel(slot, currentBankChannel);

  switch (slot.cfg.mode) {
    case CTRL_MODE_MOMENTARY: {
      if (pressed && !wasPressed) {
        _emit(slot, targetCh, 127);
      } else if (!pressed && wasPressed) {
        if (slot.lastCcValue > 0) _emit(slot, targetCh, 0);
      }
      break;
    }
    case CTRL_MODE_LATCH: {
      if (pressed && !wasPressed) {
        slot.latchState = !slot.latchState;
        _emit(slot, targetCh, slot.latchState ? 127 : 0);
      }
      break;
    }
    case CTRL_MODE_CONTINUOUS: {
      if (pressed) {
        // C3/C4 : on rising edge, reset DSP state so every fresh press starts
        // with a clean attack. Avoids false-high initial CC (previous plateau
        // bleeding down through EMA) and stale/zero HOLD_LAST reads.
        if (!wasPressed) {
          slot.emaAccum = 0;
          memset(slot.ring, 0, sizeof(slot.ring));
          slot.ringWrIdx     = 0;
          slot.pressedFrames = 0;
        }

        uint8_t rawCc = _scalePressure(state.pressure[pad], slot.cfg.deadzone);

        // Stage 1 : EMA smooth
        uint8_t smoothedCc;
        if (_smoothMs == 0) {
          smoothedCc = rawCc;
          slot.emaAccum = (uint16_t)rawCc << 8;  // keep accumulator aligned
        } else {
          uint16_t alpha = _emaAlpha();  // Q16 factor in [1..65535]
          // new = alpha*raw + (1-alpha)*prev,  everything Q16
          // I4 : + 32768 = rounding instead of truncation (prevents 1-3 unit
          // steady-state undershoot at full pressure).
          uint32_t rawQ16    = (uint32_t)rawCc << 8;
          uint32_t prevQ16   = (uint32_t)slot.emaAccum;
          uint32_t mixedQ16  = ((uint64_t)alpha * rawQ16 + (uint64_t)(65535 - alpha) * prevQ16 + 32768ULL) >> 16;
          slot.emaAccum = (uint16_t)mixedQ16;
          smoothedCc = (uint8_t)(mixedQ16 >> 8);
        }

        // Stage 2 : push into ring buffer (for HOLD_LAST look-back)
        slot.ring[slot.ringWrIdx] = smoothedCc;
        slot.ringWrIdx = (slot.ringWrIdx + 1) % CTRL_RING_SIZE;

        // C4 : track frames since rising edge so HOLD_LAST lookback clamps
        // to valid (actually-written) ring cells on short presses.
        if (slot.pressedFrames < 0xFFFF) slot.pressedFrames++;

        // Emit if changed
        if (smoothedCc != slot.lastCcValue) {
          _emit(slot, targetCh, smoothedCc);
        }
        // Cancel any active release envelope (re-press)
        slot.envFramesRemaining = 0;
      } else if (wasPressed) {
        // Falling edge
        if (slot.cfg.releaseMode == CTRL_RELEASE_TO_ZERO) {
          // Start linear release envelope from lastCcValue -> 0 over releaseMs frames
          if (_releaseMs == 0) {
            // Immediate zero (fallback to V1 behavior if release disabled)
            if (slot.lastCcValue > 0) _emit(slot, slot.lastChannel, 0);
          } else {
            slot.envStartValue      = slot.lastCcValue;
            slot.envFramesTotal     = _releaseMs;    // ~ 1 frame ~ 1ms
            slot.envFramesRemaining = _releaseMs;
          }
        } else {
          // CTRL_RELEASE_HOLD : sample-and-hold from ring buffer
          uint8_t lookback = _sampleHoldLookback(slot);
          // ringWrIdx points to next write position ; last written = wrIdx-1
          uint8_t readIdx = (uint8_t)((slot.ringWrIdx + CTRL_RING_SIZE - 1 - lookback) % CTRL_RING_SIZE);
          uint8_t heldValue = slot.ring[readIdx];
          if (heldValue != slot.lastCcValue) {
            _emit(slot, targetCh, heldValue);
          }
          // After capture, freeze — no further emission until re-press
        }
      } else {
        // Not pressed, not rising/falling : tick any active release envelope
        // (handled globally in _tickReleaseEnvelopes, called from update())
      }
      break;
    }
  }
}

// -------------------------------------------------------------
// Transition handlers (per-mode, spec §4.2)
// -------------------------------------------------------------
void ControlPadManager::_handleLeftPress(const SharedKeyboardState& state) {
  (void)state;  // unused — virtual release just uses stored state
  // Gate family only : virtual release (CC=0) on lastChannel
  for (uint8_t s = 0; s < _count; s++) {
    ControlPadSlot& slot = _slots[s];
    if (!_isGate(slot)) continue;
    if (slot.lastCcValue > 0) {
      _emit(slot, slot.lastChannel, 0);
    }
    // C1 : cancel any in-flight release envelope. Otherwise the next
    // _tickReleaseEnvelopes() would emit the next envelope value and
    // overwrite the CC=0 we just sent (staircase 127, 0, 126, 125, ...).
    slot.envFramesRemaining = 0;
  }
}

void ControlPadManager::_handleLeftRelease(const SharedKeyboardState& state,
                                           uint8_t currentBankChannel) {
  for (uint8_t s = 0; s < _count; s++) {
    ControlPadSlot& slot = _slots[s];
    uint8_t pad     = slot.cfg.padIndex;
    bool    pressed = state.keyIsPressed[pad];
    if (!pressed) continue;  // nothing to re-sync if pad is released
    uint8_t targetCh = _resolveChannel(slot, currentBankChannel);

    switch (slot.cfg.mode) {
      case CTRL_MODE_MOMENTARY:
        _emit(slot, targetCh, 127);
        break;
      case CTRL_MODE_CONTINUOUS: {
        uint8_t ccVal = _scalePressure(state.pressure[pad], slot.cfg.deadzone);
        _emit(slot, targetCh, ccVal);
        break;
      }
      case CTRL_MODE_LATCH:
      default:
        break;  // latch state unchanged, nothing to emit
    }
  }
}

void ControlPadManager::_handleBankSwitch(uint8_t oldCh, uint8_t newCh,
                                          const SharedKeyboardState& state) {
  for (uint8_t s = 0; s < _count; s++) {
    ControlPadSlot& slot = _slots[s];
    // Only follow-bank slots are affected
    if (slot.cfg.channel != 0) continue;
    if (!_isGate(slot))       continue;

    // CC=0 on OLD channel if we had emitted a non-zero value
    if (slot.lastCcValue > 0) {
      _emit(slot, oldCh, 0);
    }
    uint8_t pad     = slot.cfg.padIndex;
    bool    pressed = state.keyIsPressed[pad];

    // C2 : cancel in-flight release envelope. It would otherwise keep
    // ticking on slot.lastChannel (= OLD ch) and overwrite the CC=0
    // cleanup with a descending fade, confusing DAWs.
    slot.envFramesRemaining = 0;

    // Re-sync on NEW channel if pad still pressed
    if (!pressed) continue;

    if (slot.cfg.mode == CTRL_MODE_MOMENTARY) {
      _emit(slot, newCh, 127);
    } else if (slot.cfg.mode == CTRL_MODE_CONTINUOUS) {
      uint8_t ccVal = _scalePressure(state.pressure[pad], slot.cfg.deadzone);
      _emit(slot, newCh, ccVal);
    }
  }
}

// -------------------------------------------------------------
// Helpers
// -------------------------------------------------------------
void ControlPadManager::_emit(ControlPadSlot& slot, uint8_t ch, uint8_t val) {
  if (_transport) _transport->sendCC(ch, slot.cfg.ccNumber, val);
  slot.lastCcValue = val;
  slot.lastChannel = ch;
}

uint8_t ControlPadManager::_scalePressure(uint8_t pressure,
                                          uint8_t deadzone) const {
  if (pressure > deadzone) {
    uint8_t range = 127 - deadzone;
    return (range > 0)
         ? (uint8_t)(((uint16_t)(pressure - deadzone) * 127) / range)
         : 127;
  }
  return 0;
}

uint8_t ControlPadManager::_resolveChannel(const ControlPadSlot& slot,
                                           uint8_t currentBankChannel) const {
  if (slot.cfg.channel == 0) return currentBankChannel;  // follow (0-7)
  return (uint8_t)(slot.cfg.channel - 1);                // fixed 1-16 → 0-15
}

bool ControlPadManager::_isGate(const ControlPadSlot& slot) const {
  if (slot.cfg.mode == CTRL_MODE_MOMENTARY) return true;
  if (slot.cfg.mode == CTRL_MODE_CONTINUOUS
      && slot.cfg.releaseMode == CTRL_RELEASE_TO_ZERO) return true;
  return false;
}

// -------------------------------------------------------------
// V2 DSP helpers
// -------------------------------------------------------------
uint16_t ControlPadManager::_emaAlpha() const {
  // Convert smoothMs (tau in ms) to alpha in Q16 such that per-frame filtering
  // approximates a first-order low-pass with time constant ~smoothMs.
  // alpha ~ 1 / smoothMs (per-ms step) expressed in Q16.
  // alpha_Q16 = 65535 / smoothMs (clamp to valid range).
  if (_smoothMs == 0) return 65535;  // alpha=1 -> pass-through
  uint32_t a = 65535u / _smoothMs;
  if (a > 65535) a = 65535;
  if (a < 1)     a = 1;
  return (uint16_t)a;
}

uint8_t ControlPadManager::_sampleHoldLookback(const ControlPadSlot& slot) const {
  // sampleHoldMs / frameMs (~1ms) = #frames. Clamp to RING_SIZE-1 max.
  uint16_t lookback = _sampleHoldMs;
  if (lookback >= CTRL_RING_SIZE) lookback = CTRL_RING_SIZE - 1;
  // C4 : also bound by frames actually pressed. Short presses (fewer frames
  // than sampleHoldMs) would otherwise read unwritten/stale ring cells.
  if (slot.pressedFrames == 0) return 0;
  if (lookback >= slot.pressedFrames) lookback = slot.pressedFrames - 1;
  return (uint8_t)lookback;
}

void ControlPadManager::_tickReleaseEnvelopes() {
  // For each slot with active envelope, emit next intermediate value.
  for (uint8_t s = 0; s < _count; s++) {
    ControlPadSlot& slot = _slots[s];
    if (slot.envFramesRemaining == 0) continue;
    slot.envFramesRemaining--;
    uint16_t remaining = slot.envFramesRemaining;
    uint16_t total     = slot.envFramesTotal;
    // Linear interpolation : ccVal = envStartValue * remaining / total
    uint8_t newCc;
    if (total == 0 || remaining == 0) {
      newCc = 0;
    } else {
      newCc = (uint8_t)(((uint32_t)slot.envStartValue * remaining) / total);
    }
    if (newCc != slot.lastCcValue) {
      _emit(slot, slot.lastChannel, newCc);
    }
    // When envelope finishes (remaining hits 0), lastCcValue == 0. Freeze.
  }
}
