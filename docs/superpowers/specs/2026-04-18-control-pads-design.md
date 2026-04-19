# Control Pads — Design Spec

**Date** : 2026-04-18 (V1), 2026-04-19 (V2 delta)
**Status** : V1 + V2 shipped ; hardware bring-up pending
**Authors** : Loïc + Claude (brainstorming session)

---

## 1. Overview

Add **control pads** : a new class of pad assignments that emit MIDI CC
(instead of notes), cross-bank, gated by the LEFT button.

- A control pad replaces the pad's musical output on all banks (no note,
  no aftertouch, no arp pile contribution).
- It only emits CC when LEFT = off. When LEFT is held, the role layer
  (Tool 3) takes over as today; control pads are fully inactive.
- The cross-bank MIDI channel is either **follow bank** (the current
  foreground bank's channel) or **fixed 1-16**.
- Three modes : **MOMENTARY** (press=127 / release=0), **LATCH** (press
  flips 0↔127, fixed channel only), **CONTINUOUS** (pressure-driven,
  per-pad deadzone, per-pad release policy).
- Maximum **12** control pads, sparse, stored in a new NVS blob.
- Configured in a new **Tool 4 : Control Pads** (pushes existing Tools
  4-7 to 5-8).

The feature is a prototype addition ; zero backward compatibility is
expected (per project policy). No LED runtime feedback, no boot-time CC
emission, no collision validation on CC#.

---

## 2. Decisions log (from brainstorming)

| # | Question | Decision |
|---|---|---|
| Q1 | Note output when pad is a control pad | **A** — Replacement. No note, no AT, cross-bank. Pile of all ARPEG banks skips the position. |
| Q2 | Pressure → CC mapping (continuous) | **C** — Per-pad deadzone (0-126). Linear scaling above deadzone, 0 below. |
| Q3 | Pad modes | **C** — Three modes : MOMENTARY, LATCH, CONTINUOUS. |
| Q4 | Latch state scope + persistence | Latch **forbidden on follow-bank** (fixed channel only) → scope collapses to a single scalar state. Boot = 0 (no NVS persistence). |
| Q5 | Runtime LED feedback | **A** — None. Bank display remains the only LED semantic. |
| Q6 | Tool placement + overlap with Tool 3 role layer | **A** — Overlap allowed (no collision check, layers disjoint by LEFT). New **Tool 4** inserted after Tool 3 (renumbers ex-4..7 → 5..8). |
| Q7 | LEFT hold + bank switch handoff | **X2** — Per-mode. Gate family (MOMENTARY, CONTINUOUS+RETURN_TO_ZERO) does clean-up (CC=0 on LEFT press edge, CC=0 on old channel + re-sync new channel on bank switch). Setter (CONTINUOUS+HOLD_LAST) freezes and preserves. LATCH has no transition logic. |
| Q8 | Storage cap | **12** pads max. Sparse layout 6 bytes/entry, 76 bytes total. Fits `NVS_BLOB_MAX_SIZE=128`. |
| Q9 | CC# collision + boot emission | **V1 permissive** (no collision check in Tool). **E1 silent at boot** (no CC=0 storm). |
| — | Architecture | New `ControlPadManager` class (pattern BankManager / ScaleManager). |

Additional param added mid-brainstorm : `releaseMode` (per-pad) for
CONTINUOUS — **RETURN_TO_ZERO** (release → CC=0) or **HOLD_LAST**
(release → CC stays at last pressure value).

---

## 3. Data model

### 3.1 Enums + constants

```c
// src/core/KeyboardData.h
const uint16_t CONTROLPAD_MAGIC   = 0xBEEF;
const uint8_t  CONTROLPAD_VERSION = 1;
const uint8_t  MAX_CONTROL_PADS   = 12;

enum ControlPadMode : uint8_t {
  CTRL_MODE_MOMENTARY  = 0,
  CTRL_MODE_LATCH      = 1,
  CTRL_MODE_CONTINUOUS = 2,
};

enum ControlPadRelease : uint8_t {
  CTRL_RELEASE_TO_ZERO = 0,   // release → CC=0 (gate)
  CTRL_RELEASE_HOLD    = 1,   // release → CC stays at last value (setter)
};
```

### 3.2 Persisted struct

```c
struct ControlPadEntry {          // 6 bytes, unpacked for readability
  uint8_t padIndex;               // 0-47
  uint8_t ccNumber;               // 0-127
  uint8_t channel;                // 0=follow bank, 1-16=fixed MIDI channel
  uint8_t mode;                   // ControlPadMode
  uint8_t deadzone;               // 0-126 (continuous only)
  uint8_t releaseMode;            // ControlPadRelease (continuous only)
};

struct ControlPadStore {          // v2 — bumped from v1 (76 B → 82 B)
  uint16_t magic;                 // CONTROLPAD_MAGIC (0xBEEF)
  uint8_t  version;               // CONTROLPAD_VERSION (2)
  uint8_t  count;                 // 0..MAX_CONTROL_PADS
  uint16_t smoothMs;              // V2 : EMA tau for CONTINUOUS pressed (0..500, default 10)
  uint16_t sampleHoldMs;          // V2 : ring-buffer look-back for HOLD_LAST (0..31, default 15)
  uint16_t releaseMs;             // V2 : linear fade-out for RETURN_TO_ZERO (0..2000, default 50)
  ControlPadEntry entries[MAX_CONTROL_PADS];
};
// sizeof = 10 header + 12*6 entries = 82 bytes (fits NVS_BLOB_MAX_SIZE=128)
static_assert(sizeof(ControlPadStore) <= NVS_BLOB_MAX_SIZE,
              "ControlPadStore > 128");

#define CONTROLPAD_NVS_NAMESPACE "illpad_ctrl"
#define CONTROLPAD_NVS_KEY       "pads"
```

### 3.3 Runtime slot (non persisted)

```c
// src/managers/ControlPadManager.h
struct ControlPadSlot {
  ControlPadEntry cfg;            // copied from store at apply
  uint8_t lastCcValue;            // last emitted CC value (dedup + cleanup)
  uint8_t lastChannel;            // channel of last emission
                                  //   (meaningful when follow-bank,
                                  //    for RETURN_TO_ZERO cleanup on bank switch)
  bool    latchState;             // LATCH mode: current toggle state
                                  //   (reset to false at boot)
  bool    wasPressed;             // for edge detection, kept separate
                                  //   from global s_lastKeys[]
};
```

### 3.4 Validator

```c
// src/core/KeyboardData.h
const uint8_t CTRL_PAD_INVALID = 0xFF;  // sentinel for invalid/skipped entry

inline void validateControlPadStore(ControlPadStore& s) {
  if (s.count > MAX_CONTROL_PADS) s.count = MAX_CONTROL_PADS;
  for (uint8_t i = 0; i < s.count; i++) {
    auto& e = s.entries[i];
    if (e.padIndex >= NUM_KEYS)  e.padIndex = CTRL_PAD_INVALID;  // skip on apply
    if (e.ccNumber > 127)        e.ccNumber = 127;
    if (e.channel > 16)          e.channel = 0;     // garbage → follow
    if (e.mode > 2)              e.mode = CTRL_MODE_MOMENTARY;
    if (e.deadzone > 126)        e.deadzone = 0;
    if (e.releaseMode > 1)       e.releaseMode = CTRL_RELEASE_TO_ZERO;

    // Invariant: LATCH requires fixed channel
    // Tool enforces at edit time; validator is the safety net.
    if (e.mode == CTRL_MODE_LATCH && e.channel == 0) {
      e.mode = CTRL_MODE_MOMENTARY;
    }
  }
}
```

`ControlPadManager::applyStore(store)` iterates `store.entries[0..count-1]`
and **skips** entries where `padIndex == CTRL_PAD_INVALID`. Internal
`_slots[]` is packed (no tombstones) — the runtime `_count` may be less
than `store.count` if the validator flagged entries.

### 3.6 DSP Pipeline (V2 — CONTINUOUS only)

Three stages applied to CONTINUOUS pads between raw pressure and MIDI CC
emission. Params are global (`ControlPadStore` header), DSP state lives
per-slot in `ControlPadSlot` (V2 fields).

**Stage 1 — EMA smooth** (attack filtering, fresh attack on each press)
`smoothedCc = α × rawCc + (1 - α) × prevSmoothed`, with
`α_Q16 = 65535 / smoothMs` (per-ms step in Q16 fixed-point).
`smoothMs = 0` bypasses. Rising edge resets `emaAccum = 0` so each press
starts without bias from the previous press's plateau.

**Stage 2 — Ring buffer sample-and-hold** (for HOLD_LAST)
Per slot : `ring[CTRL_RING_SIZE=32]` holds last 32 frames of smoothed CC.
`ringWrIdx` advances each pressed frame. `pressedFrames` counter tracks
valid data in the buffer. On HOLD_LAST falling edge, capture
`ring[ringWrIdx - 1 - min(sampleHoldMs, pressedFrames - 1)]` — the value
from `sampleHoldMs` frames ago, bounded by the actual press duration.

**Stage 3 — Linear release envelope** (for RETURN_TO_ZERO)
On falling edge : `envStartValue = lastCcValue`, `envFramesTotal =
envFramesRemaining = releaseMs`. Each subsequent frame (in
`_tickReleaseEnvelopes()` called from `update()`) emits
`newCc = envStartValue × envFramesRemaining / envFramesTotal`. Freezes
at 0 when envelope completes. Cancelled on : re-press (rising edge on
same slot), LEFT press edge (virtual release), or bank switch edge
(follow-bank slots only).

**Hardware fix intent** : V1 HOLD_LAST captured a near-zero value at the
release edge due to the fast release slew of `CapacitiveKeyboard`. V2's
sample-and-hold ring buffer captures a stable pre-release plateau value.
V1 RETURN_TO_ZERO emitted an abrupt CC=0 on release ; V2 fades smoothly.

---

### 3.5 Channel resolution at runtime

Storage convention in `ControlPadEntry.channel` is **user-facing** :
`0` = follow bank, `1..16` = fixed MIDI channel (1-indexed).
`MidiTransport::sendCC(ch, cc, val)` expects the **wire-format** channel
(`0..15`, masked with `0x0F`), and `BankSlot.channel` is also stored
0-7. The manager converts at emission :

```c
uint8_t resolveChannel(const ControlPadSlot& slot,
                       uint8_t currentBankChannel /* 0-7 */) {
  if (slot.cfg.channel == 0) {
    return currentBankChannel;          // follow — already 0-7
  }
  return (uint8_t)(slot.cfg.channel - 1);  // fixed 1-16 → wire 0-15
}
```

`currentBankChannel` is passed in by `main.cpp` from
`s_bankManager.getCurrentSlot().channel` (0-7). `resolveChannel()`
returns a value in `0..15` ready for `sendCC`.

---

## 4. Runtime pipeline

### 4.1 In-frame emission (LEFT = off)

For each active slot :

| Mode | Pad edge | Emission |
|---|---|---|
| MOMENTARY | rising (press) | `CC = 127` on `targetCh` |
| MOMENTARY | falling (release) | `CC = 0` on `targetCh` |
| MOMENTARY | steady | nothing |
| LATCH | rising | `latchState ^= true` → `CC = latchState ? 127 : 0` on `cfg.channel` (fixed) |
| LATCH | falling / steady | nothing |
| CONTINUOUS | pressed | `ccVal = scalePressure(pressure, deadzone)` ; emit if `ccVal != lastCcValue` |
| CONTINUOUS | falling (RETURN_TO_ZERO) | emit `CC = 0` on `lastChannel` if `lastCcValue > 0` |
| CONTINUOUS | falling (HOLD_LAST) | nothing |
| CONTINUOUS | released (steady) | nothing |

Every emission goes through `emit(slot, ch, val)` which :
1. calls `MidiTransport::sendCC(ch, cfg.ccNumber, val)`
2. sets `slot.lastCcValue = val`
3. sets `slot.lastChannel = ch`

`scalePressure()` is the same linear formula as the aftertouch scaling
in `processNormalMode`, with the per-pad deadzone.

### 4.2 Transition handlers (per-mode, Q7-X2)

**Gate family** = `MOMENTARY` ∪ `CONTINUOUS+RETURN_TO_ZERO`
**Setter family** = `CONTINUOUS+HOLD_LAST`
**Latch** = isolated (fixed channel, no transition emission)

| Event | Gate family | Setter family | Latch |
|---|---|---|---|
| **LEFT press edge** | Emit `CC=0` on `lastChannel` if `lastCcValue > 0` (virtual release). | Nothing (freeze). | Nothing. |
| **LEFT release edge** | If pad currently pressed → re-emit current value on `targetCh` (127 for momentary, `scalePressure` for continuous). Else: nothing (already 0). | If pad currently pressed → re-emit current pressure on `targetCh`. Else: nothing. | Nothing. |
| **Bank switch edge** (follow-bank only) | Emit `CC=0` on OLD channel if `lastCcValue > 0`. If pad still pressed → emit current value on NEW channel. | Nothing (old channel keeps its setter value). | N/A (fixed channel). |

### 4.3 `ControlPadManager::update()` flow

```c
void ControlPadManager::update(const SharedKeyboardState& state,
                               bool leftHeld,
                               uint8_t currentChannel) {
  bool leftPressEdge   = leftHeld  && !_lastLeftHeld;
  bool leftReleaseEdge = !leftHeld && _lastLeftHeld;
  bool bankSwitchEdge  = (currentChannel != _lastChannel)
                         && _lastChannel != 0;

  if (leftPressEdge)   _handleLeftPress(state);
  if (leftReleaseEdge) _handleLeftRelease(state, currentChannel);
  if (bankSwitchEdge)  _handleBankSwitch(_lastChannel, currentChannel, state);

  if (!leftHeld) {
    for (uint8_t s = 0; s < _count; s++) {
      _processSlot(s, state, currentChannel);
    }
  }

  // Sync per-slot wasPressed (even during LEFT held)
  for (uint8_t s = 0; s < _count; s++) {
    _slots[s].wasPressed = state.keyIsPressed[_slots[s].cfg.padIndex];
  }
  _lastLeftHeld = leftHeld;
  _lastChannel  = currentChannel;
}
```

### 4.4 Invariants

1. **Dedup** : no emission if `newVal == lastCcValue`.
2. **CC=0 on release only if needed** : RETURN_TO_ZERO release emits CC=0
   only if `lastCcValue > 0`. Prevents redundant 0s for pads that never
   crossed their deadzone.
3. **Cross-bank strict** : a control pad is never seen as "musical" on
   any bank (foreground or background), in any mode (NORMAL or ARPEG).
4. **LATCH state never persisted** : boot always starts `latchState = false`.
5. **wasPressed tracked per slot** : decoupled from the global
   `s_lastKeys[]` used by the music block.

---

## 5. Loop integration

### 5.1 Insertion point

New step **7b** in the Core 1 loop, between `handleHoldPad` (step 7) and
`handlePadInput` (step 8) :

```
...
7.  Hold pad
7b. ControlPadManager.update(state, leftHeld, currentBankChannel)   // NEW
── handlePadInput ──
8.  processNormalMode / processArpMode
8b. handleLeftReleaseCleanup
8c. s_lastKeys sync
...
```

Runs AFTER bank switching is resolved (step 4) → `currentBankChannel` is
up to date for follow-bank targetCh resolution. Runs BEFORE pad input
processing → music block can consult `isControlPad(i)` to skip pads.

### 5.2 Music suppression

`ControlPadManager::isControlPad(padIndex) → bool` is an O(1) lookup
backed by a LUT `bool _isControlPadLut[NUM_KEYS]` rebuilt on every
`applyStore()`.

**Call sites** (main.cpp) :

```c
// processNormalMode
for (int i = 0; i < NUM_KEYS; i++) {
  if (s_controlPadManager.isControlPad(i)) continue;  // NEW
  // ... existing edge detection + noteOn/Off + aftertouch
}

// processArpMode
for (int i = 0; i < NUM_KEYS; i++) {
  if (i == s_holdPad) continue;
  if (s_controlPadManager.isControlPad(i)) continue;  // NEW
  // ... existing add/removePadPosition
}

// handleLeftReleaseCleanup (both NORMAL and ARPEG branches)
for (int i = 0; i < NUM_KEYS; i++) {
  if (s_controlPadManager.isControlPad(i)) continue;  // NEW (clarity)
  // ... existing noteOff or removePadPosition
}
```

The `s_lastKeys[i] = keyIsPressed[i]` sync in step 8c is untouched —
control pads keep their own `wasPressed` inside `ControlPadManager`.

### 5.3 Boot sequence

No dedicated LED step (silent failure tolerated, per project
zero-migration policy). `NvsManager::loadAll()` reads the blob, calls
`validateControlPadStore()`, then `controlPadManager.applyStore(store)`.
The LUT is built, latch states zeroed, `lastCcValue` / `lastChannel`
zeroed.

### 5.4 No hot-reload

Setup mode is boot-only (main loop paused during setup). Tool 4 writes
NVS and sets a dirty flag. On setup exit, the user reboots ; `loadAll`
reinitializes the manager from NVS. No code path applies a new store to
a running manager.

---

## 6. Tool 4 — Control Pads (VT100 setup)

### 6.1 Menu position

Inserted after Tool 3 (Pad Roles). Ex-Tools 4-7 renumbered to 5-8.

```
[1] Pressure Calibration
[2] Pad Ordering
[3] Pad Roles
[4] Control Pads            ← NEW
[5] Bank Config             (ex-Tool 4)
[6] Settings                (ex-Tool 5)
[7] Pot Mapping             (ex-Tool 6)
[8] LED Settings            (ex-Tool 7)
[0] Reboot
```

Impact : `TOOL_NVS_FIRST[]` / `TOOL_NVS_LAST[]` and `SetupManager` menu
dispatch shifted.

### 6.2 Layout (conforms to `docs/reference/vt100-design-guide.md`)

```
╔══◉═══════════════════════════════════════════════════════════════════════════════════════════◉══╗
║  [ILLPAD48 SETUP CONSOLE]     TOOL 4: CONTROL PADS                               [NVS:OK]      ║
╠══◉═══════════════════════════════════════════════════════════════════════════════════════════◉══╣
║                                                                                                  ║
╟── PAD GRID ────────────────────────────────────────────────────────────────────────────────────╢
║                                                                                                  ║
║    ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐                    ║
║    │ --- │ --- │ --- │ 74c │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │                    ║
║    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤                    ║
║    │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │                    ║
║    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤                    ║
║    │ --- │ 07m │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │                    ║
║    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤                    ║
║    │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │ --- │                    ║
║    └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘                    ║
║                                                                                                  ║
╟── POOL ───────────────────────────────────────────────────────────────────────────────────────╢
║ Mode :   [MOM ]  [LATCH]  [RET0 ]  [HOLD ]  [--- ]                                              ║
║          yellow  magenta  orange   white    dim                                                 ║
║                                                                                                  ║
╟── GLOBALS ────────────────────────────────────────────────────────────────────────────────────╢
║    Smooth          [010] ms   EMA filter time constant (attack smoothing)                       ║
║    Sample & hold   [015] ms   look-back for HOLD_LAST plateau capture                          ║
║    Release fade    [050] ms   RETURN_TO_ZERO linear fade-out duration                          ║
║                                                                                                  ║
╟── SELECTED : PAD #04 ──────────────────────────────────────────────────────────────────────────╢
║    CC number    [074]       standard MIDI CC 0-127                                               ║
║  ▸ Channel      [follow]    0=follow bank, 1-16=fixed MIDI channel                               ║
║    Deadzone     [016]       continuous only — pressure threshold 0-126                           ║
║    Slots used   2 / 12                                                                           ║
║                                                                                                  ║
╟── INFO ────────────────────────────────────────────────────────────────────────────────────────╢
║  Channel: MIDI routing. "follow" = CC emitted on current bank channel (1-8).                     ║
║  Overlap: Pad #04 also BANK 1 in Tool 3 — informational, layers disjoint by LEFT.                ║
║                                                                                                  ║
╠══◉═══════════════════════════════════════════════════════════════════════════════════════════◉══╣
║  [^v<>] GRID  [RET] MODE  [e] EDIT  [g] GLOBALS  [TAP] SELECT  │  [x] REMOVE  [d] DFLT  │  [q] EXIT ║
╚══◉═══════════════════════════════════════════════════════════════════════════════════════════◉══╝
```

### 6.3 SetupUI primitives used

| Primitive | Role |
|---|---|
| `vtFrameStart()` / `vtFrameEnd()` | DEC 2026 atomic frame |
| `drawConsoleHeader("TOOL 4: CONTROL PADS", nvsSaved)` | Header + NVS badge |
| `drawFrameEmpty()` | Vertical breathing room |
| `drawSection("PAD GRID" / "SELECTED : PAD #NN" / "INFO")` | Tape labels cyan |
| `drawCellGrid(GRID_CONTROLPAD, …, roleLabels, roleMap)` | **New** GridMode value ; the two trailing optional params already declared for `GRID_ROLES` are reused as-is — no signature change. |
| `drawFrameLine(fmt, …)` | Property rows, info text |
| `drawControlBar(ctrlBuf)` | Bottom bar with `CBAR_SEP` between nav / action / exit groups |
| `flashSaved()` | 120ms header pulse on every NVS save |
| `parseConfirm(ev)` | y/any-cancel for `[x] remove` and `[d] reset all` |
| `CBAR_CONFIRM_ANY` template | Control bar during confirmation |
| `showPadFeedback(pad)` | LED flash on hardware pad touch (identify a pad) |
| `showToolActive(4)` | Setup comet while Tool 4 runs |

### 6.4 GridMode extension

New enum value in `SetupUI.h` :

```c
enum GridMode : uint8_t {
  GRID_BASELINE,
  GRID_MEASUREMENT,
  GRID_ORDERING,
  GRID_ROLES,
  GRID_CONTROLPAD   // NEW
};
```

`drawCellGrid()` branch for `GRID_CONTROLPAD` (V2) :
- Cell label = `roleLabels[i]` (max 4 chars : `"74m"`, `"07l"`, `"12z"`, `"99h"`, or `"---"`)
- `roleMap[i]` encoding :
  - 0 → unassigned (VT_DIM)
  - 1 → MOMENTARY (VT_BRIGHT_YELLOW, label suffix "m")
  - 2 → LATCH (VT_MAGENTA, label suffix "l")
  - 3 → CONTINUOUS + RETURN_TO_ZERO (VT_ORANGE, label suffix "z")
  - 4 → CONTINUOUS + HOLD_LAST (VT_BRIGHT_WHITE, label suffix "h")
- Cursor overlay : `VT_REVERSE` on top of the mode color

### 6.5 Navigation states (V3)

**UI_GRID_NAV** (default) :
- Arrow keys + TAP hardware = move cursor on 4×12 grid
- `[RET]` → UI_MODE_PICK (pool-driven mode selection)
- `[e]` → UI_VALUE_EDIT (numeric fields only, assigned pad)
- `[g]` → UI_GLOBAL_EDIT (DSP params)
- `[x]` → UI_CONFIRM_REMOVE (remove current pad, with confirm)
- `[d]` → UI_CONFIRM_DEFAULTS (reset all, with confirm)
- `[q]` → exit tool

**UI_MODE_PICK** (activated by `[RET]` on grid) :
- Pool cursor on current pad's mode (defaults to MOM if unassigned)
- `[<>]` cycle the 5 pool options (MOM / LATCH / RET0 / HOLD / clear)
- `[RET]` commit : create slot if needed, apply mode (LATCH auto-promotes
  channel to 1 if was follow), single NVS save, return to GRID_NAV
- `[q]` cancel, return to GRID_NAV without modifying the slot
- Selecting `[---]` (idx=4) then `[RET]` removes the slot (no confirm,
  matches Tool 3 "[---] clear role" convention)

**UI_VALUE_EDIT** (activated by `[e]` on grid, only if pad assigned) :
- Numeric field editor : CC number (0-127), Channel (0=follow / 1-16),
  Deadzone (0-126, CONTINUOUS only)
- Mode and Release rows are read-only greyed — pool-driven
- `[^v]` navigate fields, skipping greyed ones
- `[<>]` adjust value (accelerate ×10 on repeat)
- Save-on-exit via `[RET]` or `[q]` — dirty flag tracked, single save if
  changed, no NVS write per arrow press

**UI_GLOBAL_EDIT** (activated by `[g]` on grid) :
- Field editor for smoothMs / sampleHoldMs / releaseMs
- Same save-on-exit policy as VALUE_EDIT

**UI_CONFIRM_REMOVE / UI_CONFIRM_DEFAULTS** : unchanged (y/n prompt)

### 6.6 Mode / channel invariant enforcement

When user changes **Mode** in PROP_EDIT :
- `→ LATCH` and current `channel == 0` (follow) → force `channel = 1`
  with a flash message in INFO : `CHANNEL forced to 1 (LATCH requires
  fixed channel)`. INFO refreshes next frame back to normal context.
- `→ MOMENTARY` or `→ CONTINUOUS` → no change to channel.

When user changes **Channel** in PROP_EDIT :
- `→ 0 (follow)` and current `mode == LATCH` → refuse change, flash
  `LATCH requires fixed channel — change mode first`. Channel stays.

### 6.7 Save policy

- Discrete actions (mode commit via pool, remove, reset-all, create-on-ENTER, global commit) : save fires once, flashSaved pulses
- Numeric edits (UI_VALUE_EDIT, UI_GLOBAL_EDIT) : dirty flag set on each
  arrow press, single save + flashSaved on state exit
- Tool 3 convention : save-on-commit, not save-on-change

### 6.8 `screenDirty` flag

All render gated by `screenDirty` (pattern from the guide 1.4). Every
state mutation sets `screenDirty = true`, render block clears and
redraws. No time-based refresh needed (no live sensor data).

### 6.9 Overlap info with Tool 3

If the selected pad is also assigned as a Tool 3 role
(bank/scale/mode/octave/hold), INFO shows a secondary line :

> `ⓘ This pad is also BANK 1 in Tool 3 — informational, layers disjoint by LEFT.`

Informational only, no blocking. Per Q6-A, overlap is allowed.

### 6.10 No pot input

Tool 4 is full-keyboard navigation. `SetupPotInput` is not used. If
added in a future iteration, it would bind CC# adjustment to a pot —
out of scope for v1.

---

## 7. NVS integration

### 7.1 Descriptor + namespace

```c
// KeyboardData.h — added at end of NVS_DESCRIPTORS[]
{ "illpad_ctrl", "pads", CONTROLPAD_MAGIC, CONTROLPAD_VERSION,
  sizeof(ControlPadStore) }
```

### 7.2 Tool / descriptor range mapping (renumbered)

Arrays `TOOL_NVS_FIRST[]` and `TOOL_NVS_LAST[]` grow from size **7 → 8**
in `KeyboardData.h`. The menu loop iterating these (`SetupManager` and
the main-menu status cluster) must update its count constant to match.

| Tool | FIRST descriptor | LAST descriptor | Spans |
|---|---|---|---|
| 1 Calibration | `cal` | `cal` | 1 |
| 2 Pad Ordering | `nmap` | `nmap` | 1 |
| 3 Pad Roles | `bpad` | `apad` | 3 |
| **4 Control Pads** | `ctrl` | `ctrl` | **1 (NEW)** |
| 5 Bank Config | `btype` | `btype` | 1 |
| 6 Settings | `set` | `set` | 1 |
| 7 Pot Mapping | `pmap` | `pflt` | 2 |
| 8 LED Settings | `lset_s` | `lset_c` | 2 |

### 7.3 Loader

New block in `NvsManager::loadAll()` :
1. `NvsManager::loadBlob(CONTROLPAD_NVS_NAMESPACE, CONTROLPAD_NVS_KEY,
   CONTROLPAD_MAGIC, CONTROLPAD_VERSION, &store, sizeof(store))`.
2. On success → `validateControlPadStore(store)` → `controlPadManager.applyStore(store)`.
3. On failure (missing, wrong size, wrong magic/version) → silent ;
   compile-time defaults apply (empty store, `count = 0`). One
   `Serial.printf` warning under `DEBUG_SERIAL`, per zero-migration policy.

---

## 8. Files touched

### 8.1 New files

- `src/managers/ControlPadManager.h`
- `src/managers/ControlPadManager.cpp`
- `src/setup/ToolControlPads.h`
- `src/setup/ToolControlPads.cpp`

### 8.2 Modified files

- `src/core/KeyboardData.h` :
  - `ControlPadEntry`, `ControlPadStore`, `ControlPadMode`, `ControlPadRelease`,
    `CONTROLPAD_*` constants
  - `NVS_DESCRIPTORS[]` : +1 entry
  - `TOOL_NVS_FIRST[]` / `TOOL_NVS_LAST[]` : shifted
  - `validateControlPadStore()` inline function
  - `static_assert(sizeof(ControlPadStore) <= NVS_BLOB_MAX_SIZE)`
- `src/managers/NvsManager.cpp` / `.h` :
  - New load block in `loadAll()`
  - Getter `getLoadedControlPadStore()` if needed (to match existing pattern)
- `src/setup/SetupUI.h` : new `GRID_CONTROLPAD` enum value
- `src/setup/SetupUI.cpp` : new branch in `drawCellGrid()` for
  `GRID_CONTROLPAD` (colors + labels)
- `src/setup/SetupManager.cpp` : menu entry 4 added, dispatch, renumber
  the rest
- `src/main.cpp` :
  - `s_controlPadManager` instance
  - `begin()` call in `setup()`
  - `s_controlPadManager.update(state, leftHeld, currentBankChannel)`
    in `loop()` at step 7b
  - `isControlPad(i)` guard in `processNormalMode`, `processArpMode`,
    `handleLeftReleaseCleanup`

### 8.3 Doc updates (same commit, per CLAUDE.md protocol F)

- `docs/reference/architecture-briefing.md` :
  - §2 Flow "Pad Touch → MIDI NoteOn" : add the `isControlPad(i) continue`
    guard
  - §2 Flow "Bank Switch" : add step for
    `s_controlPadManager.onBankSwitch*()` handoff
  - §4 Table 1 : new row "Control pad assignments | ControlPadStore |
    Tool 4 ToolControlPads | `illpad_ctrl` / `pads` | …"
  - §8 Domain Entry Points : new row "Control pads →
    `ControlPadManager::update`"
- `docs/reference/vt100-design-guide.md` :
  - §2.2 Tool Structure : add Tool 4 description, renumber 5-8
  - §2.4 Grid Color Rules : new row `GRID_CONTROLPAD` — `VT_DIM`
    unassigned, `VT_ORANGE` assigned, cursor reverse
- `docs/reference/nvs-reference.md` :
  - Add `ControlPadStore` to the catalog
  - Add namespace `illpad_ctrl` to the table
  - Example validator
- `CLAUDE.md` :
  - Setup Mode section : update menu `[1]..[7]` → `[1]..[8]` with
    Control Pads entry
  - Source Files section : add `ControlPadManager`, `ToolControlPads`
  - NVS Namespace table : add `illpad_ctrl`

### 8.4 Python terminal script

No changes. New Tool uses standard VT100 output.

---

## 9. Non-goals (out-of-scope v1)

1. No pot input in Tool 4 (full-keyboard navigation).
2. No runtime LED feedback for control pad press / state.
3. No Tool-side CC# collision validation (permissive, V1).
4. No NVS persistence of latch state (boot = 0, P1).
5. No boot-time CC emission to sync receivers (E1, silent).
6. No hot-reload of control pad config (setup = boot-only).
7. No multi-pad assignment (one pad at a time in Tool 4).
8. No factory / preset layouts (`[d]` resets to empty).
9. No serial export / import of control pad config.
10. No MIDI Learn for CC# (manual entry).

These are left for future iterations if needed.

---

## 10. Test plan (runtime validation, not automated)

1. **Golden path** : assign pad X = continuous CC 74 on fixed ch 5, press
   → CC 74 rises on ch 5 in a DAW / MIDI Monitor.
2. **Release modes** : RETURN_TO_ZERO → CC=0 on release. HOLD_LAST →
   CC stays at last pressure value.
3. **Latch** : press 1 → CC=127 ; press 2 → CC=0 ; press 3 → CC=127.
4. **Cross-bank follow** : momentary follow-bank on pad Y, switch banks,
   press → CC on the new bank's channel.
5. **LEFT press mid-press** :
   - Gate family (momentary, cont+ret0) → CC=0 emitted immediately.
   - Setter (cont+hold_last) → no emission, receiver stays at last value.
6. **Bank switch mid-press in follow-bank** :
   - Gate family → CC=0 on OLD channel + current value on NEW channel.
   - Setter → nothing, OLD channel keeps its value.
7. **Cap enforcement** : Tool 4 refuses a 13th slot — INFO shows
   `Cap reached (12/12)`, no slot created.
8. **NVS validation** : corrupt the blob manually (external tool),
   reboot → silent fallback to defaults.
9. **Invariant LATCH + fixed** : try LATCH on follow-bank pad in Tool
   → auto-force channel=1 + flash message.
10. **Overlap with Tool 3** : pad = bank 1 + control pad CC 74. LEFT held
    → bank switch. LEFT off → CC 74 emitted, no note played.
11. **Dedup** : continuous pad slowly moved across pressure steps → one
    CC per distinct value, no flood.
12. **Pile integrity** : assign control pad on a pad that was part of an
    ARPEG bank's pile → pile rebuild skips the position, other banks'
    arps unaffected.
13. **V2 — HOLD_LAST plateau capture** : with `smoothMs=30, sampleHoldMs=20`,
    press a HOLD_LAST CONTINUOUS pad at pressure ~100 for >50 ms, release.
    Verify captured CC value is close to the plateau (~95-100), not near zero.
14. **V2 — RETURN_TO_ZERO fade** : with `releaseMs=200`, press + release a
    RET0 pad. Verify CC descends linearly over 200 ms, not abrupt zero.
15. **V2 — EMA attack reset** : press a HOLD_LAST pad hard (CC=120), release
    (capture 120), wait 2 s, press softly (pressure ~30). Verify new CC
    starts near 30, not bleeding down from 120.
16. **V2 — Short-press HOLD_LAST** : press a HOLD_LAST pad for <10 ms with
    `sampleHoldMs=20`. Verify captured value is the earliest available
    (not zero-stale).
17. **V2 — Envelope cancelled by LEFT press** : trigger a RET0 release fade,
    press LEFT mid-fade. Verify CC goes to 0 immediately (virtual release)
    and no further envelope ticks arrive while LEFT held.
18. **V2 — Tool 4 'g' key** : press 'g' in GRID_NAV. Verify entry into
    UI_GLOBAL_EDIT with cursor on smoothMs. Arrow-adjust values, save,
    reboot. Verify persistence.

---

## 11. Open questions / future iterations

- **V2** : optional Tool-side CC# collision warning (yellow badge, not
  blocking)
- **V2** : optional latch state persistence (NVS, per-pad)
- **V2** : optional boot-time CC state broadcast (opt-in in Settings)
- **V2** : MIDI Learn mode for CC# (press pad + send CC from DAW → auto-fill)
- **V2** : preset layouts (MPC-like, APC-like) via a pool selector
- **V2** : pot binding for faster CC# / deadzone adjustment in Tool 4

None of these affect v1 design.
