# Arpeggiator Reference — ILLPAD V2

End-to-end view of the arpeggiator subsystem : pile semantics, Play/Stop
state machine, patterns, shuffle, quantize, bug patterns. Read this when
touching any arp code, adding a new pattern, or debugging a timing issue.

**Source of truth** :
- `src/arp/ArpEngine.h` + `.cpp` (per-bank state, pile, sequence, gate)
- `src/arp/ArpScheduler.h` + `.cpp` (tick accumulator, event dispatch)
- `src/midi/ClockManager.h` + `.cpp` (clock source + PLL)
- `src/midi/GrooveTemplates.h` (shuffle templates)
- `src/main.cpp` — `processArpMode`, `handleHoldPad`

Related : [`runtime-flows.md`](runtime-flows.md) §2 for the per-tick
chain. [`patterns-catalog.md`](patterns-catalog.md) P1 (refcount), P3
(scheduled event queue).

---

## 1. Core concept — pile of positions, not MIDI notes

**The pile stores `padOrder` positions**, not resolved MIDI notes.
Resolution happens at every tick via the current `ScaleConfig` attached
to the engine.

Each arp engine keeps its own `ScaleConfig` copy (set via
`setScaleConfig()` when the bank becomes foreground). **Background arps
retain their last-set ScaleConfig** — scale changes only affect the
foreground bank's engine.

Consequence : changing the scale mid-play on the foreground bank
transposes the running arp immediately at the next tick. Background
ARPEG banks keep playing in their previous scale.

Pending events store **already-resolved MIDI notes** (not positions),
so a scale change does not strand pending noteOffs on stale notes.

---

## 2. Engine lifecycle

- Up to **4 ARPEG banks** simultaneously (`MAX_ARP_BANKS = 4`). Enforced
  in Tool 5 Bank Config.
- Engines are statically instantiated (`s_arpEngines[4]`) — no runtime
  `new`/`delete`.
- Bank slots of type `BANK_ARPEG` hold a pointer into the engine pool via
  `BankManager::setHoldPad()` at boot.
- Bank type changes (Tool 5) rebind the pool ; foreground move
  (BankManager double-tap, LEFT + bank pad) does NOT reassign engines.
- Background engines tick continuously — they are not dormant.

---

## 3. Play/Stop semantic

Toggled via :
- **Hold pad** (dedicated ARPEG-only control, configurable in Tool 3).
- **LEFT + double-tap on the target bank pad** (FG or BG ; BG target
  means `keys = nullptr`, treated as "no fingers"). Never switches bank.

Both triggers go through a single event chain :
`ArpEngine::setCaptured(newState, transport, keys, holdPad)`.

### Stop mode (`_captured = false`) — live

- Pad press = add position to pile.
- Pad release = remove position from pile.
- All fingers up → arp stops naturally.
- If a **paused pile** exists (Play → Stop with no fingers), the first
  press wipes it before entering live mode. Handled in `processArpMode`
  (`main.cpp`).

### Play mode (`_captured = true`) — persistent

- Pad press = add position to **persistent** pile.
- Double-tap (same pad within `_doubleTapMs`, configurable 100–250 ms,
  default 200 ms) = remove position from persistent pile.
- Pile frozen on release — arp keeps playing as long as notes are in
  the pile.
- Pile becomes empty → arp stops.

### Play → Stop transition

- **Fingers down (excl. holdPad)** : pile cleared, live mode takes over
  (`clearAllNotes()`).
- **No fingers** (or BG target, `keys == nullptr`) : pile kept, arp stops,
  `_pausedPile = true` armed. Next Play relaunches from step 0.

### Stop → Play transition

- **Paused pile with notes** : relaunch from step 0, `_waitingForQuantize`
  if needed.
- **Empty pile** : state flips, LED confirms, arp silent until notes
  added.

### `_pausedPile` invariant

`_pausedPile = true` means "Stop with pile kept — next Play relaunches
from step 0". Reset by :
- `clearAllNotes()`,
- `setCaptured(true)` (effectively),
- `addPadPosition()`.

### Bank switch does not affect Play/Stop

Bank switch leaves every engine's `_captured`, pile, and `_pausedPile`
state untouched. Background arps keep running.

---

## 4. Scheduler state machine

Per `ArpEngine`, 3 states :

| State | Meaning | Transition out |
|---|---|---|
| `IDLE` | Pile empty, nothing scheduled | Add to pile → `WAITING_QUANTIZE` (or `PLAYING` if quantize=Immediate) |
| `WAITING_QUANTIZE` | Pile has notes, waiting for the next quantize boundary | Boundary reached → `PLAYING` ; pile cleared → `IDLE` |
| `PLAYING` | Generating steps | Pile cleared → `IDLE` ; Stop toggle → `IDLE` with `_pausedPile` |

### Quantized start

Per-bank, configured in Tool 5 :

| Mode | Start fires on | Ticks to wait (24 PPQN) |
|---|---|---|
| **Immediate** | Next division boundary | 0 |
| **Beat** | Next 1/4 note | up to 24 |
| **Bar** | Next bar | up to 96 |

Stop is **always immediate** (no quantize on stop).

Stop-mode auto-play (pile 0 → 1 transition) also respects quantize.

---

## 5. Per-step execution

`ArpScheduler::tick()` drives `tickAccum` per engine. When
`tickAccum >= divisor`, `ArpEngine::tick()` runs `executeStep()` :

1. Resolve current position → MIDI note via engine's `ScaleConfig`.
2. **Schedule noteOff FIRST** (atomic pair with the upcoming noteOn).
3. If shuffle enabled → schedule noteOn with delay; else → `refCountNoteOn()`
   immediate send.
4. Advance position index per the current pattern.

### Overlapping-note safety (P1)

All MIDI emission goes through `refCountNoteOn` / `refCountNoteOff`.
MIDI noteOn fires only on 0 → 1 refcount transition; noteOff on 1 → 0.
Overlapping notes (shuffle pushing a noteOn past the next step's
noteOff) don't produce duplicate or missing MIDI messages.

### Event queue (P3)

Per-engine `MAX_PENDING_EVENTS = 64`. Drained by
`ArpScheduler::processEvents()` each frame based on `micros() >= fireTime`.

Overflow handling :
- noteOff schedule fails → skip the entire step (safe : noteOn never
  schedules without its noteOff).
- noteOn schedule fails → cancel the orphaned noteOff (atomic-pair
  invariant).

---

## 6. Patterns

5 patterns via hold-left + R2 pot (discrete 5 values) :

| ID | Name | Position walk |
|---|---|---|
| 0 | Up | low → high, wrap |
| 1 | Down | high → low, wrap |
| 2 | UpDown | low → high → low, no endpoint repeat |
| 3 | Random | uniform pick each step |
| 4 | Order | as-entered into pile |

Octave range : 1–4 (hold-left + dedicated octave pads). `rebuildSequence()`
expands pile → sequence (up to 192 entries = 48 positions × 4 octaves)
lazily via `_sequenceDirty` flag (P2).

Dirty flag set by : `addPadPosition`, `removePadPosition`, `setPattern`,
`setOctaveRange`.

---

## 7. Division

Hold-left + R1 pot, 9 **binary** values (not linear sweep) :

```
4/1  2/1  1/1  1/2  1/4  1/8  1/16  1/32  1/64
```

Each halves the step duration from the previous. Default : 1/16.

The scheduler's `divisor` = ticks per step, computed from division and
24-PPQN clock.

---

## 8. Shuffle / groove

### Templates

10 groove templates (16 steps each) in `GrooveTemplates.h` :

- Templates 0–4 : positive-only classic (delays alternate steps).
- Templates 5–9 : bipolar (some early, some late).

Template selected via hold-left + R3 pot (10 discrete values).

### Depth

`ArpPotStore.shuffleDepth` (0.0–1.0) via R3 alone (in ARPEG context). At
extreme depth, notes can overlap across steps — handled by P1 refcount.

### Offset formula

```
shuffle offset = template[step % 16] × depth × stepDuration / 100
```

Scheduler applies the offset as a delay on the noteOn schedule
(`micros() + offset`). Gate length is preserved : noteOff stays anchored
to `noteOnTime + stepDuration × gateLength`.

### Step counter reset

The `step % 16` index resets on :
- Play/Stop toggle.
- Pile 0 → 1 transition (auto-play in Stop mode).
- Pattern change.

Reset prevents drift when the pile is repopulated after a gap.

---

## 9. Gate

Hold-left + R2 pot (continuous 0.0–1.0) controls fractional step
duration that the note sustains. noteOff is scheduled at
`noteOnTime + stepDuration × gateLength`.

NORMAL pads are unaffected — gate only applies to arp steps.

---

## 10. Velocity

Per-bank, configured via R4 alone (base) and R4 + hold-left (variation) :

```
velocity = baseVelocity + random(-variation, +variation)
```

Clamped to [1, 127]. Both NORMAL and ARPEG banks use the same per-bank
velocity params (separate storage : `base + variation` per bank for
NORMAL, same scheme for ARPEG).

---

## 11. Adding a new arp pattern

Minimum steps :

1. `ArpEngine.h` — add entry to `enum ArpPattern`. Bump
   `NUM_PATTERNS` if referenced elsewhere.
2. `ArpEngine.cpp` — add a case in `rebuildSequence()` switch.
3. `main.cpp` — add human-readable name in `s_patNames[]`.

Not touched :
- `ScaleResolver` (re-resolves automatically per position).
- `ArpScheduler` (agnostic to pattern).
- NVS (pattern is an index, stored in `ArpPotStore.pattern`).
- LED (no per-pattern LED behavior).

See also [`architecture-briefing.md`](architecture-briefing.md) §3 Task
Index.

---

## 12. Bug patterns (arp-specific)

| Pattern | Example | Where to look |
|---|---|---|
| Timing burst after clock glitch | Scheduler fires many steps in one frame | `ArpScheduler.cpp` — `ticksElapsed` capped at 24 (= 1 quarter note) |
| Stuck note after scale change | noteOff re-resolves on new scale → wrong note | Invariant : pending events carry resolved notes. Check `executeStep()` and the event queue. |
| Orphan noteOn after schedule fail | noteOff scheduled but noteOn queue full | `ArpScheduler::scheduleEvent` must cancel the paired noteOff. Check the atomic-pair invariant. |
| Paused pile lost on bank switch | Expected : bank switch does NOT change arp state | Verify no hidden reset in `BankManager::switchToBank`. |
| Shuffle drift over time | `_shuffleStep` counter not reset | Reset on Play/Stop, pile 0 → 1, pattern change. Any new reset trigger must update all three sites. |
| Background arp silent | ScaleConfig was null or wrong | `setScaleConfig` is called per-engine at foreground entry. BG keeps last value — that's intended. |
| `_pausedPile` stuck true | Add-to-pile path missed a reset | `addPadPosition` must clear `_pausedPile = false`. |
| Step 0 replay on every Play | Correct — paused pile → relaunch from 0 | By design. |
| Shuffle overlapping kills notes | Refcount missing somewhere | P1 : ALL noteOn/noteOff must go through `refCountNoteOn/Off`. |
