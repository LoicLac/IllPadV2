#include "ControlPadManager.h"
#include "../core/MidiTransport.h"
#include "../core/KeyboardData.h"  // SharedKeyboardState definition
#include <string.h>  // memset

ControlPadManager::ControlPadManager()
  : _transport(nullptr), _count(0),
    _lastLeftHeld(false), _lastChannel(0xFF) {
  memset(_isControlPadLut, 0, sizeof(_isControlPadLut));
  memset(_slots, 0, sizeof(_slots));
}

void ControlPadManager::begin(MidiTransport* transport) {
  _transport = transport;
}

void ControlPadManager::applyStore(const ControlPadStore& store) {
  _count = 0;
  memset(_isControlPadLut, 0, sizeof(_isControlPadLut));

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
        uint8_t ccVal = _scalePressure(state.pressure[pad], slot.cfg.deadzone);
        if (ccVal != slot.lastCcValue) {
          _emit(slot, targetCh, ccVal);
        }
      } else if (wasPressed) {
        // release edge
        if (slot.cfg.releaseMode == CTRL_RELEASE_TO_ZERO
            && slot.lastCcValue > 0) {
          _emit(slot, slot.lastChannel, 0);
        }
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
    // Re-sync on NEW channel if pad still pressed
    uint8_t pad     = slot.cfg.padIndex;
    bool    pressed = state.keyIsPressed[pad];
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
