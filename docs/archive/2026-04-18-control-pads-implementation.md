# Control Pads Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add cross-bank control pads that emit MIDI CC (instead of notes), with 3 modes (momentary/latch/continuous), configured in a new Tool 4.

**Architecture:** New `ControlPadManager` class (pattern BankManager/ScaleManager) owns a sparse 12-slot config from NVS (`illpad_ctrl`), detects press/release/LEFT/bank-switch edges in Core 1 loop, emits MIDI CC via `MidiTransport::sendCC`. Music output (`processNormalMode`, `processArpMode`, `handleLeftReleaseCleanup`) skips pads marked as control pads via O(1) LUT. New `ToolControlPads` (pattern ToolPadRoles) with grid `GRID_CONTROLPAD` + property editor. Tool menu inserts entry 4, renumbering ex-4..7 to 5..8.

**Tech Stack:** C++17, Arduino framework, PlatformIO, ESP-IDF NVS, TinyUSB MIDI, ESP32-BLE-MIDI, Adafruit_NeoPixel.

**Spec:** [docs/superpowers/specs/2026-04-18-control-pads-design.md](../specs/2026-04-18-control-pads-design.md)

**Verification philosophy:** This is firmware with no unit-test harness. Each task's verification is `pio run -e esp32-s3-devkitc-1` returning **SUCCESS** (compile + link clean, no new warnings). Runtime validation is manual hardware bring-up after Task 14.

**Build command:** `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1` (pio not in PATH)

**Commit policy:** per `CLAUDE.md` — every `git commit` requires explicit user authorization. The plan shows the commit command at each milestone ; the executor proposes the command and waits for OK before running it.

---

## File Structure

### New files

| Path | Responsibility |
|---|---|
| `src/managers/ControlPadManager.h` | Class declaration : store copy, slot array, edge detection state, LUT `_isControlPadLut[NUM_KEYS]`, public API (`begin`, `applyStore`, `update`, `isControlPad`, getters for Tool) |
| `src/managers/ControlPadManager.cpp` | Implementation : `applyStore` rebuilds slots + LUT ; `update` handles edges + per-mode emission ; transition handlers (`_handleLeftPress`, `_handleLeftRelease`, `_handleBankSwitch`) ; helper `_emit(slot, ch, val)` + `_scalePressure` + `_resolveChannel` |
| `src/setup/ToolControlPads.h` | Class declaration : working copy of `ControlPadStore`, UI state machine (GRID_NAV / PROP_EDIT / CONFIRM_REMOVE / CONFIRM_DEFAULTS), cursor + field index, dirty flag, NVS badge |
| `src/setup/ToolControlPads.cpp` | Tool implementation : `run()` blocking loop, `drawScreen()` (header + grid + selected + info + control bar), nav handling, auto-save via `NvsManager::saveBlob` + `flashSaved` |

### Modified files

| Path | Change |
|---|---|
| `src/core/KeyboardData.h` | Add `ControlPadMode`/`ControlPadRelease` enums, `ControlPadEntry`/`ControlPadStore` structs, `CONTROLPAD_MAGIC`/`_VERSION`/`MAX_CONTROL_PADS`/`CTRL_PAD_INVALID` constants, `validateControlPadStore()` inline, `static_assert`, new entry in `NVS_DESCRIPTORS[]`, bump `TOOL_NVS_FIRST[]`/`TOOL_NVS_LAST[]` to size 8 |
| `src/managers/NvsManager.h` | Add `getLoadedControlPadStore()` accessor (pattern matching other `getLoaded*`) |
| `src/managers/NvsManager.cpp` | Add load block in `loadAll()` : `loadBlob` + `validateControlPadStore` + stash in member ; any `queueControlPadWrite(...)` if runtime writes needed (none in v1 ; Tool writes directly via `saveBlob`) |
| `src/setup/SetupUI.h` | Add `GRID_CONTROLPAD` to `GridMode` enum |
| `src/setup/SetupUI.cpp` | Add branch in `drawCellGrid()` for `GRID_CONTROLPAD` : labels from `roleLabels`, colors from `roleMap` (0=dim unassigned, 1/2/3=orange assigned), cursor reverse |
| `src/setup/SetupManager.h` | Add `ToolControlPads _toolControlPads` member |
| `src/setup/SetupManager.cpp` | Include `ToolControlPads.h` ; call `_toolControlPads.begin(...)` in `begin()` ; insert `case '4'` dispatch, shift 4→5, 5→6, 6→7, 7→8 in `run()` switch |
| `src/main.cpp` | Add `#include "managers/ControlPadManager.h"` ; declare `s_controlPadManager` ; add `s_controlPadManager.begin(...)` in `setup()` ; call `s_controlPadManager.update(state, leftHeld, s_bankManager.getCurrentSlot().channel)` in `loop()` between `handleHoldPad` (line ~942) and `handlePadInput` (line ~944) ; add `if (s_controlPadManager.isControlPad(i)) continue;` guards in `processNormalMode` (line ~468), `processArpMode` (line ~502), `handleLeftReleaseCleanup` (lines ~544, ~551) |
| `docs/reference/architecture-briefing.md` | §2 Flow "Pad Touch → MIDI NoteOn" : add `isControlPad` guard ; §2 Flow "Bank Switch" : add `ControlPadManager::_handleBankSwitch` step ; §4 Table 1 : new row Control Pads ; §8 Domain Entry Points : new row |
| `docs/reference/vt100-design-guide.md` | §2.2 : add Tool 4 description, renumber 5-8 ; §2.4 : new row `GRID_CONTROLPAD` colors |
| `docs/reference/nvs-reference.md` | Add `ControlPadStore` catalog entry + namespace `illpad_ctrl` + example validator |
| `CLAUDE.md` | Setup Mode menu `[1]..[8]` with Control Pads ; Source Files add `ControlPadManager` + `ToolControlPads` ; NVS Namespace table add `illpad_ctrl` |

### Not touched (explicitly)

- `src/core/CapacitiveKeyboard.{cpp,h}` — DO NOT MODIFY (CLAUDE.md rule)
- `HardwareConfig.h` pressure constants — DO NOT MODIFY
- `platformio.ini` — no new dependencies needed
- `ItermCode/vt100_serial_terminal.py` — new Tool uses standard VT100, no protocol change

---

## Task Order Rationale

Tasks 1-9 build **inert machinery** : data model, manager, Tool UI. Each compiles clean but nothing is wired yet — runtime behavior unchanged. Task 10 (NVS wiring) activates persistence. Task 11 (SetupManager menu) activates the Tool in the setup menu. Task 12 (main.cpp) activates runtime behavior. Task 13 docs, Task 14 hardware bring-up. This ordering keeps every intermediate commit shippable and makes bisection trivial.

---

## Task 1: Data model + validator (KeyboardData.h)

**Files:**
- Modify: `src/core/KeyboardData.h`

- [ ] **Step 1.1 : Add enums, constants, structs after the existing BankSlot definitions (around line 301, after the closing `};` of `BankSlot`)**

Insert :

```c
// =================================================================
// Control Pads — Tool 4 (CC output via cross-bank pads)
// =================================================================

const uint16_t CONTROLPAD_MAGIC   = 0xBEEF;
const uint8_t  CONTROLPAD_VERSION = 1;
const uint8_t  MAX_CONTROL_PADS   = 12;
const uint8_t  CTRL_PAD_INVALID   = 0xFF;  // sentinel for corrupted/skipped entry

enum ControlPadMode : uint8_t {
  CTRL_MODE_MOMENTARY  = 0,
  CTRL_MODE_LATCH      = 1,
  CTRL_MODE_CONTINUOUS = 2,
};

enum ControlPadRelease : uint8_t {
  CTRL_RELEASE_TO_ZERO = 0,   // release → CC=0 (gate semantic)
  CTRL_RELEASE_HOLD    = 1,   // release → CC stays (setter semantic)
};

struct ControlPadEntry {
  uint8_t padIndex;     // 0-47, or CTRL_PAD_INVALID when validator flagged
  uint8_t ccNumber;     // 0-127
  uint8_t channel;      // 0 = follow bank, 1-16 = fixed MIDI channel (user-facing)
  uint8_t mode;         // ControlPadMode
  uint8_t deadzone;     // 0-126, CONTINUOUS only
  uint8_t releaseMode;  // ControlPadRelease, CONTINUOUS only
};

struct ControlPadStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  count;
  ControlPadEntry entries[MAX_CONTROL_PADS];
};

#define CONTROLPAD_NVS_NAMESPACE "illpad_ctrl"
#define CONTROLPAD_NVS_KEY       "pads"

static_assert(sizeof(ControlPadStore) <= NVS_BLOB_MAX_SIZE,
              "ControlPadStore > NVS_BLOB_MAX_SIZE");

inline void validateControlPadStore(ControlPadStore& s) {
  if (s.count > MAX_CONTROL_PADS) s.count = MAX_CONTROL_PADS;
  for (uint8_t i = 0; i < s.count; i++) {
    auto& e = s.entries[i];
    if (e.padIndex >= NUM_KEYS)  e.padIndex = CTRL_PAD_INVALID;
    if (e.ccNumber > 127)        e.ccNumber = 127;
    if (e.channel > 16)          e.channel = 0;
    if (e.mode > 2)              e.mode = CTRL_MODE_MOMENTARY;
    if (e.deadzone > 126)        e.deadzone = 0;
    if (e.releaseMode > 1)       e.releaseMode = CTRL_RELEASE_TO_ZERO;
    if (e.mode == CTRL_MODE_LATCH && e.channel == 0) {
      e.mode = CTRL_MODE_MOMENTARY;  // LATCH needs fixed channel
    }
  }
}
```

- [ ] **Step 1.2 : Add descriptor entry to `NVS_DESCRIPTORS[]` (search for the existing array)**

Append one element BEFORE the closing `};` of `NVS_DESCRIPTORS[]` :

```c
{ CONTROLPAD_NVS_NAMESPACE, CONTROLPAD_NVS_KEY,
  CONTROLPAD_MAGIC, CONTROLPAD_VERSION, sizeof(ControlPadStore) },
```

- [ ] **Step 1.3 : Insert descriptor at position 5 and shift `TOOL_NVS_FIRST[]` / `TOOL_NVS_LAST[]`**

The codebase uses **raw integer indices** (no `IDX_*` named constants). Current layout in `KeyboardData.h` (around line 636-654) :

```c
static constexpr NvsDescriptor NVS_DESCRIPTORS[] = {
  { CAL, ... },                // 0 : T1
  { NOTEMAP, ... },            // 1 : T2
  { BANKPAD, ... },            // 2 : T3a
  { SCALEPAD, ... },           // 3 : T3b
  { ARPPAD, ... },             // 4 : T3c
  { BANKTYPE, ... },           // 5 : T4 (ex)
  { SETTINGS, ... },           // 6 : T5 (ex)
  { POTMAP, ... },             // 7 : T6a (ex)
  { POTFILTER, ... },          // 8 : T6b (ex)
  { LED_SETTINGS, ... },       // 9 : T7a (ex)
  { LED_SETTINGS (colors), ... }, // 10 : T7b (ex)
};
static constexpr uint8_t TOOL_NVS_FIRST[] = { 0, 1, 2, 5, 6, 7, 9  };  // T1..T7
static constexpr uint8_t TOOL_NVS_LAST[]  = { 0, 1, 4, 5, 6, 8, 10 };
```

**Insert the new control pad descriptor at index 5** (between T3c=arppad at index 4 and ex-T4=banktype at ex-index 5). All descriptors from index 5 onward shift +1. New layout :

```c
static constexpr NvsDescriptor NVS_DESCRIPTORS[] = {
  { CAL, ... },                // 0 : T1
  { NOTEMAP, ... },            // 1 : T2
  { BANKPAD, ... },            // 2 : T3a
  { SCALEPAD, ... },           // 3 : T3b
  { ARPPAD, ... },             // 4 : T3c
  { CONTROLPAD_NVS_NAMESPACE,  CONTROLPAD_NVS_KEY,  CONTROLPAD_MAGIC,
    CONTROLPAD_VERSION,        (uint16_t)sizeof(ControlPadStore) },   // 5 : T4 NEW
  { BANKTYPE, ... },           // 6 : T5
  { SETTINGS, ... },           // 7 : T6
  { POTMAP, ... },             // 8 : T7a
  { POTFILTER, ... },          // 9 : T7b
  { LED_SETTINGS, ... },       // 10 : T8a
  { LED_SETTINGS (colors), ... }, // 11 : T8b
};
static constexpr uint8_t TOOL_NVS_FIRST[] = { 0, 1, 2, 5, 6, 7, 8,  10 };  // T1..T8
static constexpr uint8_t TOOL_NVS_LAST[]  = { 0, 1, 4, 5, 6, 7, 9,  11 };
```

Changes vs. current :
- `NVS_DESCRIPTORS[]` : new element at index 5, all subsequent shift +1.
- `TOOL_NVS_FIRST[]` : `{0, 1, 2, 5, 6, 7, 9}` → `{0, 1, 2, 5, 6, 7, 8, 10}` (new `5` for T4, then +1 on all following).
- `TOOL_NVS_LAST[]` : `{0, 1, 4, 5, 6, 8, 10}` → `{0, 1, 4, 5, 6, 7, 9, 11}` (new `5` for T4, then +1 on all following).
- `NVS_DESCRIPTOR_COUNT` is derived (`sizeof / sizeof`) so updates automatically.

- [ ] **Step 1.4 : Verify compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS, no new warnings. (`static_assert` succeeds → struct fits in 128 bytes ; enums + validator are purely additive.)

- [ ] **Step 1.5 : Commit**

```bash
git add src/core/KeyboardData.h
git commit -m "$(cat <<'EOF'
feat(ctrl): add ControlPadStore data model + validator

New sparse 12-slot store for Tool 4 control pads. Includes magic/version,
per-entry padIndex/ccNumber/channel/mode/deadzone/releaseMode. Validator
clamps + enforces LATCH-requires-fixed-channel invariant. NVS descriptor
added, TOOL_NVS_FIRST/LAST bumped 7→8.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: ControlPadManager header

**Files:**
- Create: `src/managers/ControlPadManager.h`

- [ ] **Step 2.1 : Create the header**

```c
#ifndef CONTROL_PAD_MANAGER_H
#define CONTROL_PAD_MANAGER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class MidiTransport;
struct SharedKeyboardState;

// Runtime slot : persisted config + dynamic state
struct ControlPadSlot {
  ControlPadEntry cfg;
  uint8_t lastCcValue;   // dedup + cleanup decisions
  uint8_t lastChannel;   // channel of last emission (for follow-bank handoff)
  bool    latchState;    // LATCH: current toggle state (boot = false)
  bool    wasPressed;    // per-slot edge tracking (independent of s_lastKeys)
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
};

#endif // CONTROL_PAD_MANAGER_H
```

- [ ] **Step 2.2 : Verify compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. Header only, no new instantiations — purely declarative.

- [ ] **Step 2.3 : Commit**

```bash
git add src/managers/ControlPadManager.h
git commit -m "$(cat <<'EOF'
feat(ctrl): add ControlPadManager header

Class API for control pad runtime: applyStore rebuilds slots + LUT,
update runs per-frame edge detection + CC emission, isControlPad for
music block suppression. Private helpers for per-slot processing and
transition handlers (LEFT edges, bank switch).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: ControlPadManager implementation

**Files:**
- Create: `src/managers/ControlPadManager.cpp`

- [ ] **Step 3.1 : Write full implementation**

```c
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
  if (bankSwitchEdge)  _handleBankSwitch(_lastChannel, currentBankChannel, state);

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
        _emit(slot, targetCh, 0);
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
```

- [ ] **Step 3.2 : Verify compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. Class implementation, no instantiation yet — unused globally.

- [ ] **Step 3.3 : Commit**

```bash
git add src/managers/ControlPadManager.cpp
git commit -m "$(cat <<'EOF'
feat(ctrl): implement ControlPadManager runtime

Full implementation: applyStore filters invalid entries + rebuilds LUT,
update() runs edge detection (LEFT press/release + bank switch) and
per-mode in-frame emission (momentary/latch/continuous). Transition
handlers follow spec §4.2 per-mode handoff (gate cleanup vs setter
freeze). Helper _resolveChannel converts user-facing 1-16 to wire 0-15.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: SetupUI GRID_CONTROLPAD extension

**Files:**
- Modify: `src/setup/SetupUI.h`
- Modify: `src/setup/SetupUI.cpp`

- [ ] **Step 4.1 : Add enum value in `SetupUI.h`**

Find the `GridMode` enum (around line 113) and add `GRID_CONTROLPAD` as the last value :

```c
enum GridMode : uint8_t {
  GRID_BASELINE,
  GRID_MEASUREMENT,
  GRID_ORDERING,
  GRID_ROLES,
  GRID_CONTROLPAD   // NEW: Tool 4 — CC slot cells
};
```

- [ ] **Step 4.2 : Extend `drawCellGrid()` in `SetupUI.cpp` with the new mode**

Locate the `switch (mode)` (or chain of `if (mode == ...)`) inside `drawCellGrid`. Add a branch for `GRID_CONTROLPAD`.

Behavior :
- `roleLabels[i]` provides the visible 4-char string (`"74c"`, `"07m"`, `"---"`, etc.).
- `roleMap[i]` is 0 = unassigned, 1/2/3 = momentary/latch/continuous.
- Color per cell : `roleMap[i] == 0` → `VT_DIM`, otherwise `VT_ORANGE`.
- Cursor overlay : if cell index equals `activeKey`, wrap the label with `VT_REVERSE ... VT_RESET`.

Insert the branch following the existing pattern for `GRID_ROLES`. Exact code depends on current `drawCellGrid` structure — adapt to preserve alignment and `visibleLen()` correctness (5-char cell width).

Reference : re-read the existing `GRID_ROLES` branch and mirror its structure.

- [ ] **Step 4.3 : Verify compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. `drawCellGrid` still callable, new branch unreachable until a Tool calls with `GRID_CONTROLPAD`.

- [ ] **Step 4.4 : Commit**

```bash
git add src/setup/SetupUI.h src/setup/SetupUI.cpp
git commit -m "$(cat <<'EOF'
feat(ctrl): add GRID_CONTROLPAD mode to drawCellGrid

New GridMode value for Tool 4. Colors: VT_DIM unassigned, VT_ORANGE
assigned (all 3 modes share the color, label suffix m/l/c distinguishes).
Cursor via VT_REVERSE overlay. Reuses roleLabels/roleMap params (no
signature change).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: ToolControlPads skeleton

**Files:**
- Create: `src/setup/ToolControlPads.h`
- Create: `src/setup/ToolControlPads.cpp`

- [ ] **Step 5.1 : Create header `ToolControlPads.h`**

```c
#ifndef TOOL_CONTROL_PADS_H
#define TOOL_CONTROL_PADS_H

#include <stdint.h>
#include "../core/HardwareConfig.h"
#include "../core/KeyboardData.h"
#include "InputParser.h"

class CapacitiveKeyboard;
class LedController;
class SetupUI;
class NvsManager;

class ToolControlPads {
public:
  ToolControlPads();

  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             SetupUI* ui, NvsManager* nvs);
  void run();  // Blocking main loop

private:
  CapacitiveKeyboard* _keyboard;
  LedController*      _leds;
  SetupUI*            _ui;
  NvsManager*         _nvs;

  // UI state
  enum UIMode : uint8_t {
    UI_GRID_NAV,
    UI_PROP_EDIT,
    UI_CONFIRM_REMOVE,
    UI_CONFIRM_DEFAULTS,
  };

  UIMode _uiMode;
  uint8_t _cursorPad;     // 0-47, selected cell in grid
  uint8_t _fieldIdx;      // 0-4 in PROP_EDIT (CC/Channel/Mode/Deadzone/Release)
  bool    _screenDirty;
  bool    _nvsSaved;

  // Working copy (committed via saveBlob on every edit)
  ControlPadStore _wk;

  // Flash message (e.g., "LATCH requires fixed channel")
  char    _flashMsg[80];
  uint32_t _flashExpireMs;  // 0 = no message

  // Baselines captured at tool start — used by detectActiveKey for pad-tap-select.
  // Pattern from ToolPadRoles / ToolCalibration (see SetupCommon.h helpers).
  uint16_t _refBaselines[NUM_KEYS];

  InputParser _input;

  // --- Navigation ---
  void _handleGridNav(const NavEvent& ev);
  void _handlePropEdit(const NavEvent& ev);
  void _handleConfirmRemove(const NavEvent& ev);
  void _handleConfirmDefaults(const NavEvent& ev);

  // --- Slot ops ---
  int8_t _findSlot(uint8_t padIdx) const;   // -1 if none
  bool   _addSlot(uint8_t padIdx);          // returns false if cap reached
  void   _removeSlotForPad(uint8_t padIdx);
  void   _resetAll();

  // --- Field edit helpers ---
  void _adjustField(int8_t delta);          // </> in PROP_EDIT
  bool _isFieldGreyed(uint8_t fieldIdx) const;  // true if Deadzone/Release hidden

  // --- Render ---
  void _draw();
  void _drawGrid();
  void _drawSelected();
  void _drawInfo();
  void _drawControlBar();

  // --- Persistence ---
  void _save();        // NvsManager::saveBlob + flashSaved + badge refresh
  void _load();        // Initial read at begin()
  void _refreshBadge();

  // --- Flash helpers ---
  void _setFlash(const char* msg);  // stored, expires ~1500ms
  bool _flashActive() const;
};

#endif // TOOL_CONTROL_PADS_H
```

- [ ] **Step 5.2 : Create `ToolControlPads.cpp` with minimal run() that renders and exits on 'q'**

```c
#include "ToolControlPads.h"
#include "SetupUI.h"
#include "SetupCommon.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include <Arduino.h>
#include <string.h>

ToolControlPads::ToolControlPads()
  : _keyboard(nullptr), _leds(nullptr), _ui(nullptr), _nvs(nullptr),
    _uiMode(UI_GRID_NAV), _cursorPad(0), _fieldIdx(0),
    _screenDirty(true), _nvsSaved(false), _flashExpireMs(0) {
  memset(&_wk, 0, sizeof(_wk));
  _wk.magic   = CONTROLPAD_MAGIC;
  _wk.version = CONTROLPAD_VERSION;
  _flashMsg[0] = '\0';
}

void ToolControlPads::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                            SetupUI* ui, NvsManager* nvs) {
  _keyboard = keyboard;
  _leds     = leds;
  _ui       = ui;
  _nvs      = nvs;
}

void ToolControlPads::run() {
  if (!_ui || !_leds || !_nvs) return;

  _load();
  _refreshBadge();
  _uiMode      = UI_GRID_NAV;
  _cursorPad   = 0;
  _fieldIdx    = 0;
  _screenDirty = true;
  _flashMsg[0] = '\0';
  _flashExpireMs = 0;

  _leds->showToolActive(4);

  // Capture baselines for pad-tap-select (pattern from ToolPadRoles)
  captureBaselines(*_keyboard, _refBaselines);

  // InputParser has no reset() — default-construct state is fine for re-entry.

  while (true) {
    _leds->update();
    _keyboard->pollAllSensorData();

    NavEvent ev = _input.update();

    // Handle flash expiry
    if (_flashActive() && millis() > _flashExpireMs) {
      _flashMsg[0] = '\0';
      _flashExpireMs = 0;
      _screenDirty = true;
    }

    // Dispatch input
    switch (_uiMode) {
      case UI_GRID_NAV:         _handleGridNav(ev);         break;
      case UI_PROP_EDIT:        _handlePropEdit(ev);        break;
      case UI_CONFIRM_REMOVE:   _handleConfirmRemove(ev);   break;
      case UI_CONFIRM_DEFAULTS: _handleConfirmDefaults(ev); break;
    }

    if (ev.type == NAV_QUIT && _uiMode == UI_GRID_NAV) {
      break;  // exit tool
    }

    if (_screenDirty) {
      _draw();
      _screenDirty = false;
    }

    delay(5);
  }

  // Teardown : nothing — SetupManager redraws menu after return
}

// ------------------------------------------------------------
// Stubs for Task 5 compile — filled in subsequent tasks
// ------------------------------------------------------------
void ToolControlPads::_handleGridNav(const NavEvent&)         { }
void ToolControlPads::_handlePropEdit(const NavEvent&)        { }
void ToolControlPads::_handleConfirmRemove(const NavEvent&)   { }
void ToolControlPads::_handleConfirmDefaults(const NavEvent&) { }

int8_t ToolControlPads::_findSlot(uint8_t padIdx) const {
  for (uint8_t i = 0; i < _wk.count; i++) {
    if (_wk.entries[i].padIndex == padIdx) return (int8_t)i;
  }
  return -1;
}

bool ToolControlPads::_addSlot(uint8_t)       { return false; }
void ToolControlPads::_removeSlotForPad(uint8_t) { }
void ToolControlPads::_resetAll() { }

void ToolControlPads::_adjustField(int8_t) { }
bool ToolControlPads::_isFieldGreyed(uint8_t) const { return false; }

void ToolControlPads::_drawGrid()        { _ui->drawFrameEmpty(); }
void ToolControlPads::_drawSelected()    { _ui->drawFrameEmpty(); }
void ToolControlPads::_drawInfo()        { _ui->drawFrameEmpty(); }
void ToolControlPads::_drawControlBar()  {
  _ui->drawControlBar(VT_DIM "[q] EXIT" VT_RESET);
}

void ToolControlPads::_draw() {
  _ui->vtFrameStart();
  _ui->drawConsoleHeader("TOOL 4: CONTROL PADS", _nvsSaved);
  _ui->drawFrameEmpty();

  _ui->drawSection("PAD GRID");
  _drawGrid();

  _ui->drawSection("SELECTED");
  _drawSelected();

  _ui->drawSection("INFO");
  _drawInfo();

  _drawControlBar();
  _ui->vtFrameEnd();
}

void ToolControlPads::_load() {
  if (NvsManager::loadBlob(CONTROLPAD_NVS_NAMESPACE, CONTROLPAD_NVS_KEY,
                           CONTROLPAD_MAGIC, CONTROLPAD_VERSION,
                           &_wk, sizeof(_wk))) {
    validateControlPadStore(_wk);
  } else {
    // Defaults: empty
    memset(&_wk, 0, sizeof(_wk));
    _wk.magic   = CONTROLPAD_MAGIC;
    _wk.version = CONTROLPAD_VERSION;
    _wk.count   = 0;
  }
}

void ToolControlPads::_save() {
  bool ok = NvsManager::saveBlob(CONTROLPAD_NVS_NAMESPACE, CONTROLPAD_NVS_KEY,
                                 &_wk, sizeof(_wk));
  if (ok) {
    _ui->flashSaved();
    _refreshBadge();
  }
}

void ToolControlPads::_refreshBadge() {
  _nvsSaved = NvsManager::checkBlob(CONTROLPAD_NVS_NAMESPACE, CONTROLPAD_NVS_KEY,
                                    CONTROLPAD_MAGIC, CONTROLPAD_VERSION,
                                    sizeof(_wk));
}

void ToolControlPads::_setFlash(const char* msg) {
  strncpy(_flashMsg, msg, sizeof(_flashMsg) - 1);
  _flashMsg[sizeof(_flashMsg) - 1] = '\0';
  _flashExpireMs = millis() + 1500;
  _screenDirty = true;
}

bool ToolControlPads::_flashActive() const {
  return _flashMsg[0] != '\0';
}
```

- [ ] **Step 5.3 : Verify compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. Class compiles with stubbed handlers. Not yet wired to SetupManager — unused.

- [ ] **Step 5.4 : Commit**

```bash
git add src/setup/ToolControlPads.h src/setup/ToolControlPads.cpp
git commit -m "$(cat <<'EOF'
feat(ctrl): scaffold ToolControlPads skeleton

Class shell with UI state machine (GRID_NAV/PROP_EDIT/CONFIRM_REMOVE/
CONFIRM_DEFAULTS), load/save via NvsManager::loadBlob/saveBlob,
flashSaved feedback, flash message system, input loop. Handlers are
stubs — filled in subsequent tasks.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Tool 4 GRID_NAV mode

**Files:**
- Modify: `src/setup/ToolControlPads.cpp`

- [ ] **Step 6.1 : Implement `_handleGridNav`**

Replace the empty stub with :

```c
void ToolControlPads::_handleGridNav(const NavEvent& ev) {
  // Hardware pad touch → jump cursor (pattern from ToolPadRoles.cpp:594)
  int detected = detectActiveKey(*_keyboard, _refBaselines);
  if (detected >= 0 && detected != (int)_cursorPad) {
    _cursorPad = (uint8_t)detected;
    _screenDirty = true;
    _ui->showPadFeedback((uint8_t)detected);
  }

  switch (ev.type) {
    case NAV_UP:
      if (_cursorPad >= 12) { _cursorPad -= 12; _screenDirty = true; }
      break;
    case NAV_DOWN:
      if (_cursorPad + 12 < NUM_KEYS) { _cursorPad += 12; _screenDirty = true; }
      break;
    case NAV_LEFT:
      if (_cursorPad % 12 > 0) { _cursorPad--; _screenDirty = true; }
      break;
    case NAV_RIGHT:
      if (_cursorPad % 12 < 11) { _cursorPad++; _screenDirty = true; }
      break;

    case NAV_ENTER: {
      int8_t s = _findSlot(_cursorPad);
      if (s < 0) {
        // Create new slot with defaults
        if (!_addSlot(_cursorPad)) {
          _setFlash("Cap reached (12/12). Remove a pad first.");
          break;
        }
      }
      _uiMode = UI_PROP_EDIT;
      _fieldIdx = 0;
      _screenDirty = true;
      break;
    }

    case NAV_CHAR:
      if (ev.ch == 'x') {
        int8_t s = _findSlot(_cursorPad);
        if (s >= 0) {
          _uiMode = UI_CONFIRM_REMOVE;
          _screenDirty = true;
        }
      } else if (ev.ch == 'd' || ev.type == NAV_DEFAULTS) {
        _uiMode = UI_CONFIRM_DEFAULTS;
        _screenDirty = true;
      }
      break;

    default:
      break;
  }
}
```

- [ ] **Step 6.2 : Implement `_addSlot`, `_removeSlotForPad`, `_resetAll`**

Replace the stubs :

```c
bool ToolControlPads::_addSlot(uint8_t padIdx) {
  if (_wk.count >= MAX_CONTROL_PADS) return false;
  if (padIdx >= NUM_KEYS) return false;
  if (_findSlot(padIdx) >= 0) return true;  // already exists

  ControlPadEntry& e = _wk.entries[_wk.count];
  e.padIndex    = padIdx;
  e.ccNumber    = 0;
  e.channel     = 0;   // follow bank
  e.mode        = CTRL_MODE_MOMENTARY;
  e.deadzone    = 0;
  e.releaseMode = CTRL_RELEASE_TO_ZERO;
  _wk.count++;
  _save();
  return true;
}

void ToolControlPads::_removeSlotForPad(uint8_t padIdx) {
  int8_t s = _findSlot(padIdx);
  if (s < 0) return;
  // Sparse compaction: shift entries after s down by one
  for (uint8_t i = (uint8_t)s; i + 1 < _wk.count; i++) {
    _wk.entries[i] = _wk.entries[i + 1];
  }
  _wk.count--;
  memset(&_wk.entries[_wk.count], 0, sizeof(ControlPadEntry));
  _save();
}

void ToolControlPads::_resetAll() {
  _wk.count = 0;
  memset(_wk.entries, 0, sizeof(_wk.entries));
  _save();
}
```

- [ ] **Step 6.3 : Implement `_drawGrid` and `_drawControlBar` for GRID_NAV**

Replace the stubs. The grid uses the existing `drawCellGrid(GRID_CONTROLPAD, ...)` helper with `roleLabels` and `roleMap` arrays we build inline :

```c
void ToolControlPads::_drawGrid() {
  char labels[NUM_KEYS][6];
  uint8_t map[NUM_KEYS];
  memset(map, 0, sizeof(map));

  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    int8_t s = _findSlot(i);
    if (s < 0) {
      strncpy(labels[i], "---", 5);
      map[i] = 0;
    } else {
      const ControlPadEntry& e = _wk.entries[s];
      char suffix = (e.mode == CTRL_MODE_MOMENTARY)  ? 'm'
                   : (e.mode == CTRL_MODE_LATCH)      ? 'l'
                                                      : 'c';
      snprintf(labels[i], sizeof(labels[i]), "%02u%c", e.ccNumber % 100, suffix);
      map[i] = e.mode + 1;  // 1/2/3
    }
  }

  // drawCellGrid signature:
  // (mode, target, baselines[], measuredDeltas[], done[], activeKey,
  //  activeDelta, activeIsDone, orderMap[], roleLabels[][6], roleMap[])
  _ui->drawCellGrid(GRID_CONTROLPAD,
                    0, nullptr, nullptr, nullptr,
                    (int)_cursorPad, 0, false,
                    nullptr, labels, map);
}

void ToolControlPads::_drawControlBar() {
  switch (_uiMode) {
    case UI_GRID_NAV: {
      _ui->drawControlBar(
        VT_DIM "[^v<>] GRID  [RET] EDIT  [TAP] SELECT" CBAR_SEP
               "[x] REMOVE  [d] DFLT" CBAR_SEP
               "[q] EXIT" VT_RESET);
      break;
    }
    case UI_PROP_EDIT: {
      _ui->drawControlBar(
        VT_DIM "[^v] FIELD  [</>] VALUE" CBAR_SEP
               "[RET] BACK  [q] CANCEL" VT_RESET);
      break;
    }
    case UI_CONFIRM_REMOVE:
    case UI_CONFIRM_DEFAULTS:
      _ui->drawControlBar(CBAR_CONFIRM_ANY);
      break;
  }
}
```

- [ ] **Step 6.4 : Implement `_drawInfo` with grid-nav context + overlap info**

```c
void ToolControlPads::_drawInfo() {
  if (_flashActive()) {
    _ui->drawFrameLine(VT_YELLOW "%s" VT_RESET, _flashMsg);
    _ui->drawFrameEmpty();
    return;
  }

  int8_t s = _findSlot(_cursorPad);
  if (s < 0) {
    _ui->drawFrameLine(VT_DIM "Pad #%d : unassigned. [RET] to create." VT_RESET,
                       (int)_cursorPad + 1);
  } else {
    const ControlPadEntry& e = _wk.entries[s];
    const char* modeName = (e.mode == CTRL_MODE_MOMENTARY)  ? "momentary"
                          : (e.mode == CTRL_MODE_LATCH)      ? "latch"
                                                             : "continuous";
    char chBuf[12];
    if (e.channel == 0) snprintf(chBuf, sizeof(chBuf), "follow");
    else                snprintf(chBuf, sizeof(chBuf), "ch %d", e.channel);
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET
                       " : CC %d, %s, %s",
                       (int)_cursorPad + 1, e.ccNumber, chBuf, modeName);
  }
  _ui->drawFrameEmpty();
}
```

- [ ] **Step 6.5 : Implement `_drawSelected` (just count line in GRID_NAV ; fuller layout in PROP_EDIT comes next)**

```c
void ToolControlPads::_drawSelected() {
  _ui->drawFrameLine(VT_DIM "Slots used   %u / %u" VT_RESET,
                     (unsigned)_wk.count, (unsigned)MAX_CONTROL_PADS);
  _ui->drawFrameEmpty();
}
```

- [ ] **Step 6.6 : Verify compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. GRID_NAV functional (nav + create/remove slots + flash messages), PROP_EDIT still stubbed.

- [ ] **Step 6.7 : Commit**

```bash
git add src/setup/ToolControlPads.cpp
git commit -m "$(cat <<'EOF'
feat(ctrl): implement Tool 4 GRID_NAV mode

Full grid navigation: arrow keys + hardware pad-tap-select, [RET]
creates/enters PROP_EDIT, [x] arms remove confirmation, [d] arms
reset-all confirmation, cap-reached flash message. Slot compaction on
remove (no tombstone). drawGrid uses new GRID_CONTROLPAD mode with
'NNm'/'NNl'/'NNc' labels.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Tool 4 PROP_EDIT mode + field invariants

**Files:**
- Modify: `src/setup/ToolControlPads.cpp`

- [ ] **Step 7.1 : Implement `_handlePropEdit`**

Replace the empty stub :

```c
void ToolControlPads::_handlePropEdit(const NavEvent& ev) {
  int8_t s = _findSlot(_cursorPad);
  if (s < 0) {
    // Slot vanished (shouldn't happen, defensive) — back to grid
    _uiMode = UI_GRID_NAV;
    _screenDirty = true;
    return;
  }

  switch (ev.type) {
    case NAV_UP:
      do {
        if (_fieldIdx == 0) _fieldIdx = 4; else _fieldIdx--;
      } while (_isFieldGreyed(_fieldIdx));
      _screenDirty = true;
      break;

    case NAV_DOWN:
      do {
        if (_fieldIdx == 4) _fieldIdx = 0; else _fieldIdx++;
      } while (_isFieldGreyed(_fieldIdx));
      _screenDirty = true;
      break;

    case NAV_LEFT:
      _adjustField(ev.accelerated ? -10 : -1);
      break;
    case NAV_RIGHT:
      _adjustField(ev.accelerated ? +10 : +1);
      break;

    case NAV_ENTER:
    case NAV_QUIT:
      _uiMode = UI_GRID_NAV;
      _screenDirty = true;
      break;

    default:
      break;
  }
}
```

- [ ] **Step 7.2 : Implement `_adjustField` with mode/channel invariants**

```c
void ToolControlPads::_adjustField(int8_t delta) {
  int8_t sSigned = _findSlot(_cursorPad);
  if (sSigned < 0) return;
  uint8_t s = (uint8_t)sSigned;
  ControlPadEntry& e = _wk.entries[s];

  switch (_fieldIdx) {
    case 0: {  // CC number 0-127
      int16_t v = (int16_t)e.ccNumber + delta;
      if (v < 0)   v = 0;
      if (v > 127) v = 127;
      e.ccNumber = (uint8_t)v;
      break;
    }
    case 1: {  // Channel 0 (follow) .. 16
      int16_t v = (int16_t)e.channel + delta;
      if (v < 0)  v = 0;
      if (v > 16) v = 16;
      // Invariant: LATCH + follow (ch 0) forbidden
      if (e.mode == CTRL_MODE_LATCH && v == 0) {
        // Refuse: keep current channel
        _setFlash("LATCH requires fixed channel - change mode first");
        return;  // no save
      }
      e.channel = (uint8_t)v;
      break;
    }
    case 2: {  // Mode cycle 0..2
      int16_t v = (int16_t)e.mode + delta;
      // Clamp to 0..2 (no wrap, since delta can be ±1 or ±10)
      while (v < 0) v += 3;
      while (v > 2) v -= 3;
      uint8_t newMode = (uint8_t)v;
      // Invariant: switching TO LATCH with channel=0 forces channel=1
      if (newMode == CTRL_MODE_LATCH && e.channel == 0) {
        e.channel = 1;
        _setFlash("CHANNEL forced to 1 (LATCH requires fixed channel)");
      }
      e.mode = newMode;
      break;
    }
    case 3: {  // Deadzone 0..126 (continuous only)
      if (e.mode != CTRL_MODE_CONTINUOUS) return;
      int16_t v = (int16_t)e.deadzone + delta;
      if (v < 0)   v = 0;
      if (v > 126) v = 126;
      e.deadzone = (uint8_t)v;
      break;
    }
    case 4: {  // Release 0..1 (continuous only)
      if (e.mode != CTRL_MODE_CONTINUOUS) return;
      e.releaseMode = (e.releaseMode == CTRL_RELEASE_TO_ZERO)
                    ? CTRL_RELEASE_HOLD
                    : CTRL_RELEASE_TO_ZERO;
      break;
    }
    default:
      return;
  }

  _save();
  _screenDirty = true;
}

bool ToolControlPads::_isFieldGreyed(uint8_t fieldIdx) const {
  int8_t s = _findSlot(_cursorPad);
  if (s < 0) return false;
  const ControlPadEntry& e = _wk.entries[s];
  // Deadzone (3) and Release (4) only for continuous
  if ((fieldIdx == 3 || fieldIdx == 4) && e.mode != CTRL_MODE_CONTINUOUS) {
    return true;
  }
  return false;
}
```

- [ ] **Step 7.3 : Expand `_drawSelected` for PROP_EDIT (field view + vedette Nixie for numerics)**

Replace the current `_drawSelected` :

```c
void ToolControlPads::_drawSelected() {
  int8_t sSigned = _findSlot(_cursorPad);

  // GRID_NAV unassigned-or-nothing-to-show case
  if (sSigned < 0) {
    _ui->drawFrameLine(VT_DIM "Slots used   %u / %u" VT_RESET,
                       (unsigned)_wk.count, (unsigned)MAX_CONTROL_PADS);
    _ui->drawFrameEmpty();
    return;
  }

  const ControlPadEntry& e = _wk.entries[sSigned];

  struct FieldRow {
    const char* label;
    char valBuf[24];
    const char* desc;
    bool greyed;
  };
  FieldRow rows[5];

  // CC number
  rows[0].label = "CC number ";
  snprintf(rows[0].valBuf, sizeof(rows[0].valBuf), "%03u", e.ccNumber);
  rows[0].desc = "standard MIDI CC 0-127";
  rows[0].greyed = false;

  // Channel
  rows[1].label = "Channel   ";
  if (e.channel == 0) snprintf(rows[1].valBuf, sizeof(rows[1].valBuf), "follow");
  else                snprintf(rows[1].valBuf, sizeof(rows[1].valBuf), "%u", e.channel);
  rows[1].desc = "0=follow bank, 1-16=fixed MIDI channel";
  rows[1].greyed = false;

  // Mode
  rows[2].label = "Mode      ";
  snprintf(rows[2].valBuf, sizeof(rows[2].valBuf), "%s",
           (e.mode == CTRL_MODE_MOMENTARY)  ? "momentary"
           : (e.mode == CTRL_MODE_LATCH)     ? "latch"
                                             : "continuous");
  rows[2].desc = "momentary / latch / continuous";
  rows[2].greyed = false;

  // Deadzone
  rows[3].label = "Deadzone  ";
  snprintf(rows[3].valBuf, sizeof(rows[3].valBuf), "%03u", e.deadzone);
  rows[3].desc = "continuous only - pressure threshold 0-126";
  rows[3].greyed = (e.mode != CTRL_MODE_CONTINUOUS);

  // Release
  rows[4].label = "Release   ";
  snprintf(rows[4].valBuf, sizeof(rows[4].valBuf), "%s",
           (e.releaseMode == CTRL_RELEASE_TO_ZERO) ? "return-0" : "hold-last");
  rows[4].desc = "continuous only - return-to-zero / hold-last";
  rows[4].greyed = (e.mode != CTRL_MODE_CONTINUOUS);

  for (uint8_t i = 0; i < 5; i++) {
    bool selected = (_uiMode == UI_PROP_EDIT) && (_fieldIdx == i);
    const char* cursor = selected ? "  " VT_CYAN VT_BOLD "\xe2\x96\xb8 " : "    ";
    const char* valCol =
        rows[i].greyed ? VT_DIM
        : selected     ? VT_CYAN
                       : VT_BRIGHT_WHITE;
    const char* val = rows[i].greyed ? "---" : rows[i].valBuf;
    _ui->drawFrameLine("%s%s[%s%-10s%s]" VT_RESET "   " VT_DIM "%s" VT_RESET,
                       cursor, rows[i].label,
                       valCol, val, VT_RESET,
                       rows[i].desc);
  }

  _ui->drawFrameEmpty();
  _ui->drawFrameLine(VT_DIM "Slots used   %u / %u" VT_RESET,
                     (unsigned)_wk.count, (unsigned)MAX_CONTROL_PADS);

  // Overlap info with Tool 3 : TODO ? v1 leaves out (avoid coupling
  // ToolControlPads to ToolPadRoles internals). Spec §6.9 calls for it
  // but only if we can read bankPads/rootPads/etc. without new wiring.
  // Skip for v1 unless trivial — document in comments below.
  _ui->drawFrameEmpty();

  // Vedette Nixie when editing a numeric field
  if (_uiMode == UI_PROP_EDIT) {
    if (_fieldIdx == 0) {
      _ui->drawSection("CC NUMBER");
      _ui->drawSegmentedValue("", (uint32_t)e.ccNumber, 3, "");
    } else if (_fieldIdx == 3 && e.mode == CTRL_MODE_CONTINUOUS) {
      _ui->drawSection("DEADZONE");
      _ui->drawSegmentedValue("", (uint32_t)e.deadzone, 3, "");
    }
  }
}
```

(Note : the "overlap info with Tool 3" per spec §6.9 is explicitly
deferred to v2 above — wiring `ToolControlPads` to `ToolPadRoles` for
read-only overlap detection requires a shared accessor that doesn't
exist yet. Ship v1 without this line ; revisit if the user requests it.)

- [ ] **Step 7.4 : Verify compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. PROP_EDIT fully functional : field nav + value adjust + invariants + grey-out.

- [ ] **Step 7.5 : Commit**

```bash
git add src/setup/ToolControlPads.cpp
git commit -m "$(cat <<'EOF'
feat(ctrl): implement Tool 4 PROP_EDIT mode

Field editor for 5 properties (CC/Channel/Mode/Deadzone/Release) with
arrow nav + </> value adjust + acceleration. Invariants enforced:
LATCH forbids follow-bank (refuse channel=0 with flash), switching to
LATCH from follow auto-forces channel=1. Deadzone/Release greyed when
mode != continuous. Nixie vedette injected for CC/Deadzone editing.

v1 defers the Tool-3-overlap info line (§6.9) — adds a cross-tool
accessor footprint, revisit in v2 if requested.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Tool 4 confirmation states

**Files:**
- Modify: `src/setup/ToolControlPads.cpp`

- [ ] **Step 8.1 : Implement `_handleConfirmRemove` and `_handleConfirmDefaults`**

Replace the empty stubs :

```c
void ToolControlPads::_handleConfirmRemove(const NavEvent& ev) {
  ConfirmResult r = SetupUI::parseConfirm(ev);
  if (r == CONFIRM_YES) {
    _removeSlotForPad(_cursorPad);
    _uiMode = UI_GRID_NAV;
    _screenDirty = true;
  } else if (r == CONFIRM_NO) {
    _uiMode = UI_GRID_NAV;
    _screenDirty = true;
  }
  // PENDING → stay
}

void ToolControlPads::_handleConfirmDefaults(const NavEvent& ev) {
  ConfirmResult r = SetupUI::parseConfirm(ev);
  if (r == CONFIRM_YES) {
    _resetAll();
    _uiMode = UI_GRID_NAV;
    _cursorPad = 0;
    _screenDirty = true;
  } else if (r == CONFIRM_NO) {
    _uiMode = UI_GRID_NAV;
    _screenDirty = true;
  }
}
```

- [ ] **Step 8.2 : Extend `_drawInfo` to render confirmation prompts**

Insert at the top of `_drawInfo` (before the `_flashActive` check) :

```c
if (_uiMode == UI_CONFIRM_REMOVE) {
  _ui->drawFrameLine(VT_YELLOW "Remove control pad #%d? (y/n)" VT_RESET,
                     (int)_cursorPad + 1);
  _ui->drawFrameEmpty();
  return;
}
if (_uiMode == UI_CONFIRM_DEFAULTS) {
  _ui->drawFrameLine(VT_YELLOW "Reset ALL control pads to empty? (y/n)" VT_RESET);
  _ui->drawFrameEmpty();
  return;
}
```

- [ ] **Step 8.3 : Verify compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. Tool now handles all 4 UI states fully.

- [ ] **Step 8.4 : Commit**

```bash
git add src/setup/ToolControlPads.cpp
git commit -m "$(cat <<'EOF'
feat(ctrl): implement Tool 4 confirmation states

[x] removes current pad's slot after y/n confirm. [d] resets all slots
after y/n confirm. Prompts render in INFO in VT_YELLOW. Uses existing
SetupUI::parseConfirm loose semantic (y=yes, anything else=cancel).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: NvsManager integration

**Files:**
- Modify: `src/managers/NvsManager.h`
- Modify: `src/managers/NvsManager.cpp`

- [ ] **Step 9.1 : Add `getLoadedControlPadStore()` accessor in header**

Open `src/managers/NvsManager.h`. Find the existing `getLoaded*` accessors (e.g., `getLoadedArpParams`, `getLoadedScaleGroup`). Add :

```c
// Control pads (Tool 4) — loaded once at boot
const ControlPadStore& getLoadedControlPadStore() const { return _ctrlStore; }
```

Add the private member :

```c
ControlPadStore _ctrlStore;
```

- [ ] **Step 9.2 : Add load block in `loadAll()`**

Open `src/managers/NvsManager.cpp`. Find `loadAll()` implementation. Near the end (after other blob loads, before the scalar loads like tempo/bank), add :

```c
// Control pads
memset(&_ctrlStore, 0, sizeof(_ctrlStore));
_ctrlStore.magic   = CONTROLPAD_MAGIC;
_ctrlStore.version = CONTROLPAD_VERSION;
if (NvsManager::loadBlob(CONTROLPAD_NVS_NAMESPACE, CONTROLPAD_NVS_KEY,
                         CONTROLPAD_MAGIC, CONTROLPAD_VERSION,
                         &_ctrlStore, sizeof(_ctrlStore))) {
  validateControlPadStore(_ctrlStore);
#if DEBUG_SERIAL
  Serial.printf("[NVS] loaded %u control pad(s)\n", (unsigned)_ctrlStore.count);
#endif
} else {
#if DEBUG_SERIAL
  Serial.println("[NVS] control pads: defaults (empty)");
#endif
}
```

- [ ] **Step 9.3 : Initialize the member in the constructor**

In the constructor of `NvsManager` (check `NvsManager.cpp`), add `memset(&_ctrlStore, 0, sizeof(_ctrlStore))` or equivalent to keep the member well-defined before `loadAll`.

- [ ] **Step 9.4 : Verify compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS.

- [ ] **Step 9.5 : Commit**

```bash
git add src/managers/NvsManager.h src/managers/NvsManager.cpp
git commit -m "$(cat <<'EOF'
feat(ctrl): NvsManager loads ControlPadStore at boot

Stashes the loaded + validated store as _ctrlStore, exposed via
getLoadedControlPadStore() for main.cpp to feed into
ControlPadManager::applyStore at setup time. Silent fallback to empty
defaults on magic/version mismatch (zero-migration policy).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: SetupManager menu renumbering + dispatch

**Files:**
- Modify: `src/setup/SetupManager.h`
- Modify: `src/setup/SetupManager.cpp`

- [ ] **Step 10.1 : Add member in `SetupManager.h`**

Include `ToolControlPads.h` (near other Tool includes) and add :

```c
ToolControlPads _toolControlPads;
```

- [ ] **Step 10.2 : Wire `begin()` in `SetupManager.cpp`**

After the existing `_toolRoles.begin(...)` call, add :

```c
_toolControlPads.begin(keyboard, leds, &_ui, nvs);
```

- [ ] **Step 10.3 : Renumber dispatch in `run()` switch**

Find the switch near line 80-120 in `SetupManager.cpp`. Current :

```c
case '4': _toolBankConfig.run();   _ui.vtClear(); break;
case '5': _toolSettings.run();     _ui.vtClear(); break;
case '6': _toolPotMapping.run();   _ui.vtClear(); break;
case '7': _toolLedSettings.run();  _leds->startSetupComet(); ...
```

After :

```c
case '4': _toolControlPads.run(); _ui.vtClear(); break;
case '5': _toolBankConfig.run();   _ui.vtClear(); break;
case '6': _toolSettings.run();     _ui.vtClear(); break;
case '7': _toolPotMapping.run();   _ui.vtClear(); break;
case '8': _toolLedSettings.run();  _leds->startSetupComet(); ...
```

Preserve the existing body of each case when shifting.

- [ ] **Step 10.4 : Update menu rendering in `src/setup/SetupUI.cpp::printMainMenu()` — ALL of the following together**

**Why all together** : Task 1 already bumped `TOOL_NVS_FIRST/LAST` from 7 → 8 elements, which shifted descriptor indices 5-11. Between Task 1 and this step, any setup-mode menu entry will show the wrong NVS-saved badge (e.g. "Bank Config" label but "Control Pads" badge). This step fixes the intermediate regression.

Concrete changes in `printMainMenu()` :

(a) Change `char toolStatus[7]` → `char toolStatus[8]` (or whichever array name holds per-tool badges).
(b) Change the loop bound `for (... < 7 ...)` → `for (... < 8 ...)` (or replace with `sizeof(TOOL_NVS_FIRST)/sizeof(TOOL_NVS_FIRST[0])`).
(c) Add the `[4] Control Pads` menu text line; shift the existing `[4]..[7]` labels down to `[5]..[8]`.
(d) Update `drawStatusCluster` : `StatusItem cluster[7]` → `[8]` with the new `{ "CTL", nvsOk }` entry at index 3 (T4 position), subsequent entries shifted.

After this step, the setup menu correctly displays 8 tools with 8 correct NVS badges.

- [ ] **Step 10.5 : Verify compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. Setup mode now shows 8 tools ; `[4]` dispatches to ToolControlPads.

- [ ] **Step 10.6 : Commit**

```bash
git add src/setup/SetupManager.h src/setup/SetupManager.cpp
git commit -m "$(cat <<'EOF'
feat(ctrl): insert Tool 4 Control Pads, renumber 5-8

SetupManager owns a ToolControlPads instance, initialized in begin().
Menu dispatch case '4' routes to it; ex-Tools 4-7 shift to 5-8
(BankConfig, Settings, PotMapping, LedSettings). Main menu text
updated, SYSTEM CHECK cluster bumped to 8 voyants.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: main.cpp runtime wiring

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 11.1 : Add include and instance**

Near the top of `src/main.cpp` (with other manager includes) :

```c
#include "managers/ControlPadManager.h"
```

Near the other static manager instances (grep for `static BankManager s_bankManager;`) :

```c
static ControlPadManager s_controlPadManager;
```

- [ ] **Step 11.2 : Initialize in `setup()`**

Find `setup()`. After `s_nvsManager.loadAll(...)` but before `loop()` starts, add :

```c
s_controlPadManager.begin(&s_transport);
s_controlPadManager.applyStore(s_nvsManager.getLoadedControlPadStore());
```

- [ ] **Step 11.3 : Call `update()` in `loop()` between handleHoldPad and handlePadInput (currently around line 942-944)**

After `handleHoldPad(state);` and before `handlePadInput(state, now);`, add :

```c
// --- Control pads (step 7b): runs after bank switch resolution,
//     before music block. Gates CC output + LEFT/bank edges.
s_controlPadManager.update(state, leftHeld,
                           s_bankManager.getCurrentSlot().channel);
```

- [ ] **Step 11.4 : Add `isControlPad` skip in `processNormalMode` (around line 468)**

Inside the `for (int i = 0; i < NUM_KEYS; i++) {` loop, immediately after the opening `{` :

```c
if (s_controlPadManager.isControlPad(i)) continue;
```

- [ ] **Step 11.5 : Add `isControlPad` skip in `processArpMode` (around line 502-506)**

After the existing `if (i == s_holdPad) continue;`, add :

```c
if (s_controlPadManager.isControlPad(i)) continue;
```

- [ ] **Step 11.6 : Add `isControlPad` skip in `handleLeftReleaseCleanup` (both branches)**

In the NORMAL branch (around line 544) inside the `for` loop, before `if (!state.keyIsPressed[i])` :

```c
if (s_controlPadManager.isControlPad(i)) continue;
```

In the ARPEG branch (around line 551), after the existing `if (i == s_holdPad) continue;` :

```c
if (s_controlPadManager.isControlPad(i)) continue;
```

- [ ] **Step 11.7 : Verify compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS.

- [ ] **Step 11.8 : Commit**

```bash
git add src/main.cpp
git commit -m "$(cat <<'EOF'
feat(ctrl): wire ControlPadManager into main.cpp runtime

Instantiated, begin() + applyStore() called in setup after NVS load.
update() runs in loop step 7b between handleHoldPad and handlePadInput,
receiving current bank channel. isControlPad(i) guards added in
processNormalMode, processArpMode, handleLeftReleaseCleanup (both
branches) to gate pads assigned as CC-only.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: Documentation updates

**Files:**
- Modify: `docs/reference/architecture-briefing.md`
- Modify: `docs/reference/vt100-design-guide.md`
- Modify: `docs/reference/nvs-reference.md`
- Modify: `CLAUDE.md`

- [ ] **Step 12.1 : Update `architecture-briefing.md`**

- §2 Flow "Pad Touch → MIDI NoteOn" : add line before edge detect :
  `→ skip if s_controlPadManager.isControlPad(i)` (under both NORMAL and ARPEG branches).

- §2 Flow "Bank Switch" : add step 11 :
  `11. s_controlPadManager.update() detects bank switch edge on next frame,
       handles per-mode CC handoff (gate family cleanup on old channel,
       setter freeze).`

- §4 Table 1 : add row :
  `| Control pad assignments | ControlPadStore | Tool 4 ToolControlPads | illpad_ctrl / pads | New PotTarget equivalent: add entry + Tool ; consumed at boot via ControlPadManager::applyStore |`

- §8 Domain Entry Points : add row :
  `| **Control pads** | ControlPadManager::update | Edge detection + per-mode CC emission + LEFT/bank handoff. |`

- [ ] **Step 12.2 : Update `vt100-design-guide.md`**

- §2.2 Tool Structure : insert a "Tool 4 — Control Pads" subsection describing grid + property editor, reference ToolControlPads. Renumber existing Tool 4-7 descriptions to Tool 5-8 headings.

- §2.4 Grid Color Rules : add row to the table :
  `| **GRID_CONTROLPAD** | — | — | — | — | — | Unassigned | (assigned = VT_ORANGE — new column if the table needs a "Orange" column, else mention in text)`

Adjust the table header if needed ; simpler is to add a textual note below the table :
  `GRID_CONTROLPAD : VT_DIM unassigned, VT_ORANGE assigned (all 3 modes), cursor reverse overlay.`

- [ ] **Step 12.3 : Update `nvs-reference.md`**

Add a section for `ControlPadStore` :
- namespace `illpad_ctrl`, key `pads`
- struct layout (magic/version/count/entries[12] of 6 bytes each)
- validator highlights (LATCH+follow invariant, padIndex sentinel)
- example `loadBlob` / `saveBlob` use

- [ ] **Step 12.4 : Update `CLAUDE.md`**

- "Setup Mode" section : rewrite the menu listing `[1]..[7]` into `[1]..[8]` with `[4] Control Pads` inserted. Update the descriptions : `[5] Bank Config (ex-4)`, etc.

- "Source Files" section : add `src/managers/ControlPadManager.{cpp,h}` and `src/setup/ToolControlPads.{cpp,h}`.

- "NVS Namespace table" : add row `| illpad_ctrl | ControlPadStore — 12 sparse entries, cross-bank CC pads |`.

- [ ] **Step 12.5 : Verify compile (docs shouldn't affect build)**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS.

- [ ] **Step 12.6 : Commit**

```bash
git add docs/reference/architecture-briefing.md \
        docs/reference/vt100-design-guide.md \
        docs/reference/nvs-reference.md \
        CLAUDE.md
git commit -m "$(cat <<'EOF'
docs(ctrl): propagate Tool 4 Control Pads across reference docs

architecture-briefing: add isControlPad skip to flow §2, new Store row
in §4 Table 1, new domain entry in §8. vt100-design-guide: renumber
Tools 5-8, document GRID_CONTROLPAD colors. nvs-reference: catalog
entry for ControlPadStore + illpad_ctrl namespace. CLAUDE.md: menu
updated [1]..[8], source files listed, NVS namespace added.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 13: Regression sanity check (no-op verification)

**Files:** none

- [ ] **Step 13.1 : Verify existing tests / behaviors unchanged**

Goal : ensure none of the pre-existing tools / runtime flows regressed. Since no unit tests exist, this is a code review sweep :

- [ ] Read `git diff` for `processNormalMode`, `processArpMode`, `handleLeftReleaseCleanup`. Confirm the only change is the `isControlPad` guard ; no existing logic altered.
- [ ] Read `git diff` for `SetupManager::run` switch. Confirm every existing case body is intact (only shifted).
- [ ] Read `git diff` for `SetupUI::drawCellGrid`. Confirm the new `GRID_CONTROLPAD` branch doesn't share variables with `GRID_ROLES` / other modes in ways that could leak state.
- [ ] Grep for `TOOL_NVS_FIRST` and `TOOL_NVS_LAST` : confirm every usage iterates `< 8` (not `< 7`).
- [ ] Confirm `NvsManager::_ctrlStore` is initialized before `loadAll` is called (look for stack garbage risk).

- [ ] **Step 13.2 : Full rebuild clean**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t clean && ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. Full clean build succeeds, final RAM + flash usage reported, no linker warnings.

- [ ] **Step 13.3 : Note firmware size delta**

Compare flash/RAM numbers with the pre-feature baseline (if known) and log to the handoff notes. Expected delta : a few KB flash, a few hundred bytes RAM.

(No commit — review-only task.)

---

## Task 14: Hardware bring-up (user)

**Files:** none

- [ ] **Step 14.1 : Upload firmware (user-authorized)**

User executes : `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload`

- [ ] **Step 14.2 : Enter setup mode, verify Tool 4 exists**

Procedure : press rear button within 3s of boot, hold 3s. In the menu, confirm `[4] Control Pads` is visible. Confirm ex-Tools 5-8 are correctly labeled.

- [ ] **Step 14.3 : Run spec §10 test plan**

Work through the test plan in the spec :
1. Golden path : assign continuous CC 74 fixed ch 5, press pad → CC 74 rises on ch 5 in DAW.
2. Release modes : RETURN_TO_ZERO → CC=0 on release ; HOLD_LAST → stays.
3. Latch : press 1 → 127, press 2 → 0, press 3 → 127.
4. Cross-bank follow : assign momentary follow-bank, switch banks, press → CC on new bank's channel.
5. LEFT press mid-press : gate family → CC=0 immediate, setter → freeze.
6. Bank switch mid-press follow-bank : gate family → CC=0 on old + current on new ; setter → nothing.
7. Cap enforcement : try 13th slot → INFO shows `Cap reached (12/12)`.
8. NVS validation : intentionally break blob (if there's a tool), reboot → silent defaults.
9. LATCH+follow invariant : try LATCH on follow in Tool → auto-force ch=1 + flash.
10. Overlap with Tool 3 : assign pad as bank AND control → LEFT held = bank switch, LEFT off = CC emit.
11. Dedup : continuous slow sweep → one CC per distinct value.
12. Pile integrity : assign control pad on a pad in ARPEG pile → pile skips the position, other banks unaffected.

- [ ] **Step 14.4 : Log findings**

Create or append `docs/known_issues/2026-04-18-control-pads-bringup.md` with :
- Pass/fail per test
- Any unexpected behavior
- Items for v2 iteration

(No commit until bring-up is green — firmware code is already committed from Tasks 1-12.)

---

## Self-Review (for plan author)

### Spec coverage check

| Spec section | Covered by tasks |
|---|---|
| §1 Overview | Implicitly covered throughout |
| §2 Decisions log | Encoded in Task 1 (data model), Task 3 (runtime), Tasks 6-7 (Tool invariants) |
| §3 Data model (3.1-3.5) | Task 1 (structs/enums/validator) + Task 3 (_resolveChannel) |
| §4 Runtime pipeline (4.1 in-frame, 4.2 transitions, 4.3 update flow, 4.4 invariants) | Task 3 |
| §5 Loop integration (5.1 insertion, 5.2 music suppression, 5.3 boot, 5.4 no hot-reload) | Task 11 (steps 11.3-11.6) + Task 9 (boot load) |
| §6 Tool 4 UX (6.1-6.10 layout, primitives, nav, invariants, save) | Tasks 4 (GridMode), 5-8 (Tool states), 10 (dispatch) |
| §7 NVS integration | Tasks 1 (descriptor + arrays) + 9 (loader) |
| §8 Files touched | Every task lists concrete files |
| §9 Non-goals | Task 7 explicitly defers §6.9 overlap info to v2, matching non-goals |
| §10 Test plan | Task 14 step 14.3 enumerates the 12 tests |

### Placeholder scan

- Searched for "TBD", "TODO", "implement later", "fill in details" → none remaining in the plan.
- Searched for "similar to Task N" → Task 8's confirm handlers duplicate the y/n pattern but inline the code (no cross-task reference).
- All code blocks are complete (no `// ... existing ...` except where explicitly indicating unchanged context for a point edit).

### Type consistency check

- `ControlPadEntry` fields used consistently : `padIndex`, `ccNumber`, `channel`, `mode`, `deadzone`, `releaseMode` (Tasks 1, 3, 5, 7).
- `ControlPadMode` values `CTRL_MODE_MOMENTARY/LATCH/CONTINUOUS` used consistently in Tasks 1, 3, 7.
- `ControlPadRelease` values `CTRL_RELEASE_TO_ZERO/HOLD` consistent Tasks 1, 3, 7.
- `ControlPadManager::applyStore/isControlPad/update/getCount/getSlots` API matches between Task 2 header and Task 3 impl and Task 11 main.cpp call site.
- Channel semantics : storage `0` = follow, `1-16` = fixed (user-facing). Wire via `_resolveChannel` → 0-15. Consistent across Tasks 1, 3, 7.
- `MAX_CONTROL_PADS = 12` referenced in Tasks 1 (data model), 5 (Tool init), 6 (cap check), 7 (Slots used line).
- `GRID_CONTROLPAD` added in Task 4, consumed in Task 6's `_drawGrid`.

### Known deferral (documented in plan, not a bug)

- Spec §6.9 "overlap info with Tool 3" is explicitly deferred in Task 7 Step 7.3 comment and commit message. Surfacing this in the Tool requires a cross-tool accessor to read Tool 3's bank/scale/arp pad assignments. Not in v1 scope.

---

## Execution Handoff

Plan complete and saved to [docs/superpowers/plans/2026-04-18-control-pads-implementation.md](2026-04-18-control-pads-implementation.md). Two execution options :

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration. Best if you want me to drive the whole thing with checkpoints.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with your manual checkpoints. Best if you want to stay close to each step.

Which approach?
