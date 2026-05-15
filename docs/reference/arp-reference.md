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

## 6. Patterns (CLASSIC mode)

6 patterns via hold-left + R2 pot (discrete 6 values, post ARPEG_GEN
reduction 15→6) :

| ID | Name | Position walk |
|---|---|---|
| 0 | Up | low → high, wrap |
| 1 | Down | high → low, wrap |
| 2 | UpDown | low → high → low, no endpoint repeat |
| 3 | Order | as-entered into pile |
| 4 | PedalUp | basse pédale + arpège ascendant |
| 5 | Converge | zigzag low/high vers centre |

Octave range : 1–4 littérales (pad oct dédiés). `rebuildSequence()`
expands pile → sequence (up to 192 entries = 48 positions × 4 octaves)
lazily via `_sequenceDirty` flag (P2).

Dirty flag set by : `addPadPosition`, `removePadPosition`, `setPattern`,
`setOctaveRange`.

Patterns dropped from V1 (Random, Cascade, Diverge, UpOctave, DownOctave,
Chord, OctaveWave, OctaveBounce, Probability) : retired during ARPEG_GEN
plumbing because their behavior is subsumed by GENERATIVE mode (see §13).

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

## 11. Adding a new arp pattern (CLASSIC mode)

Patterns apply to CLASSIC mode only — ARPEG_GEN bank doesn't expose
patterns (its `pattern` NVS slot stores `_genPosition` instead, see §13).

Minimum steps :

1. `ArpEngine.h` — add entry to `enum ArpPattern`. Bump
   `NUM_ARP_PATTERNS` if referenced elsewhere.
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

---

## 13. Generative mode (ARPEG_GEN)

Bank type `BANK_ARPEG_GEN` shares the engine pool with classic ARPEG
(at most 4 cumulative arp banks via `MAX_ARP_BANKS`). The engine
selects its semantic via `_engineMode = EngineMode::GENERATIVE` set
at boot from `BankType` (cf [arpeg-gen-design.md](../superpowers/specs/2026-04-25-arpeg-gen-design.md) §10).

### Architecture

Two parallel sequence buffers per `ArpEngine` :
- `_sequence[MAX_ARP_SEQUENCE]` (192 entries) — CLASSIC pattern walk.
- `_sequenceGen[MAX_ARP_GEN_SEQUENCE]` (96 entries, `int8_t` degrees) — GENERATIVE walk.

`executeStep()` branches on `_engineMode` :
- CLASSIC → walks `_sequence[]`, resolves pos+oct via `ScaleResolver::resolve`.
- GENERATIVE → walks `_sequenceGen[]`, resolves degree → MIDI via
  `ScaleResolver::degreeToMidi(degree, scale)`. Common output via
  `executeStepNote(transport, stepDurationUs, finalNote)`.

### Pile model (degrees, not pad positions)

Pad presses populate `_positions[]` (CLASSIC pile, padOrder values),
then `recomputePileDegrees()` mirrors them as signed scale-relative
degrees in `_pileDegrees[]` plus cached `_pileLo`/`_pileHi`. Hooked from
`addPadPosition` / `removePadPosition` / `setScaleConfig` /
`setPadOrder`. Spec §17 : pile additions beyond first don't reseed.

### Walk : pickNextDegree (§12, §16)

```
pickNextDegree(prev, E, useScalePool) :
  walkMin = pile_lo - margin_walk
  walkMax = pile_hi + margin_walk
  pool = filter(pile + (useScalePool ? scale_window[prev-E..prev+E] : ∅))
         keeping only candidates in [walkMin, walkMax] AND |d - prev| ≤ E
  weights : w(Δ) = exp(-|Δ| / (E × 0.4))   [proximity factor compile-time]
            × bonus_pile if candidate ∈ pile (mutation only)
  return uniform-weighted pick from pool
  empty pool → return prev (spec §37 fallback)
```

### Generation triggers (§14, §17)

| Trigger | Action |
|---|---|
| Pile 0 → 1 | Full `seedSequenceGen()` — initial walk from pile uniform start |
| `clearAllNotes` | `_sequenceGenDirty = true`, regen on next pile add |
| Pile 1 → N (Option B refinement) | Force 3 ciblées mutations to integrate new pile note audibly |
| `setGenPosition(newPos)` extension | Walk-extend `_sequenceGen[oldLen..newLen-1]` continu depuis dernier step (avoid monotone trail) |
| Scale change | `recomputePileDegrees()` only — degrees stay valid, transpose via new `degreeToMidi` mapping |
| Pile shrink | `recomputePileDegrees()` only — adapts pool naturally on next mutation |

### Mutation (§15, §20)

`maybeMutate(globalStepCount)` called per executed step. Lookup
`_mutationLevel` (1=lock no-op, 2=1/16, 3=1/8, 4=1/4 step rate),
modular check on `globalStepCount`, picks one random index, rewrites
via `pickNextDegree(prev, E_pot, useScalePool=true)`.

`_mutationLevel` driven by pad oct 1-4 via `ScaleManager` (Task 19).
No memory of pre-mutation state (§20).

### Grid position (§13)

`TABLE_GEN_POSITION[15]` = 15 discrete `{seqLen, ecart}` entries
(8/1, 8/2, 16/2, …, 96/12). `_genPosition` ∈ [0..14] selected via
R2+hold pot (`TARGET_GEN_POSITION`, two-binding strategy in
`PotRouter::rebuildBindings`, plan §0 D3). Hysteresis ±1.5%×4095 in
`applyBinding` to prevent zone flicker. `_seqLenGen` follows
`TABLE_GEN_POSITION[_genPosition].seqLen` ; `ecart` (E_pot) drives
mutation walk amplitude.

### Per-bank parameters (§21)

Stored in `BankTypeStore` v3 (44 octets, magic+ver+reserved + 5×8B
arrays) :
- `bonusPilex10[bank]` (10..20 = 1.0..2.0, default 15)
- `marginWalk[bank]` (3..12, default 7)

`ArpPotStore` v1 (12 octets) `pattern` field is dual-semantic :
- Bank `BANK_ARPEG` : `ArpPattern` enum (0..5)
- Bank `BANK_ARPEG_GEN` : `_genPosition` (0..14)
- `octaveRange` : same dual-semantic (octave range vs mutation level).

Boot path routes the dual-semantic via `bank.type` check (see
`main.cpp` engine init).

### Tool 5 UI

Bank type cycle 5 states (NORMAL → ARPEG-Imm → ARPEG-Beat →
ARPEG_GEN-Imm → ARPEG_GEN-Beat → NORMAL). ARPEG_GEN banks render on
2 lines (line 2 = Bonus pile + Margin). Edit mode field-focus 4
sub-fields (TYPE → GROUP → BONUS → MARGIN cycle linéaire). ←→ cycles
focus, ↑↓ adjusts focused field value (§4.4 universal convention,
plan §0 D5).
