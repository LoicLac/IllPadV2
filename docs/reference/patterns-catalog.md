# Reusable Patterns — ILLPAD V2

API + callsites + reuse scenarios. Grab these instead of inventing a new
mechanism. Each entry : what it solves, the API, where it's used, when to
reuse, gotchas.

**Companion refs** :
- Data flows using these patterns → [`runtime-flows.md`](runtime-flows.md).
- Setup-tool-specific conventions → [`setup-tools-conventions.md`](setup-tools-conventions.md).
- Navigation index (when to reach for a pattern) → [`architecture-briefing.md`](architecture-briefing.md).

---

## Runtime patterns (P1–P9)

### P1 — Refcount noteOn/Off

Multiple overlapping noteOn on the same MIDI note without duplicate messages.
MIDI fires only on 0→1 and 1→0 transitions.

API : `refCountNoteOn(transport, note, vel)` /
`refCountNoteOff(transport, note)`.

Used : `ArpEngine.cpp`.

**Reuse** for any engine that can overlap notes (loop mode, chord sequencer).

**Pair with P3** : schedule noteOff BEFORE noteOn (atomic pair).

---

### P2 — Dirty flag → consume (auto-clear)

One-shot producer → consumer signal. `consumeXxx()` returns state + clears.

Used : `consumeTickFlash`, `consumeScaleChange`, `hasOctaveChanged`,
`hasHoldToggled`.

**Reuse** for "X happened this frame, tell someone". Single consumer per flag.

---

### P3 — Scheduled event queue

Fire a MIDI event when `micros() >= fireTimeUs`. Fixed-size per-engine queue
(`MAX_PENDING_EVENTS = 64`).

API : `scheduleEvent(time, note, vel)` + `processEvents(transport)` every
frame.

Used : `ArpEngine` for shuffle + gate.

**Reuse** for any sub-frame timed MIDI.

**Gotcha** : queue full → schedule fails → cancel the paired event manually.

---

### P4 — Pot catch

Prevent parameter jumps when the physical pot position doesn't match the
stored value. `CatchState{caught, storedValue}` + catch window check before
writing the parameter.

Used : `PotRouter::applyBinding`.

**Reuse** for every new user-facing continuous parameter shared across
contexts. Define the per-bank vs global policy via `isPerBankTarget()`.

Details : [`pot-reference.md`](pot-reference.md).

---

### P5 — Event overlay (LED grammar, Phase 0 refactor 2026-04-19)

Temporary event visualization on top of the normal LED display via a unified
3-layer pattern grammar :

1. **Patterns** — palette of 9 fixed behaviors (`SOLID`, `PULSE_SLOW`,
   `CROSSFADE_COLOR`, `BLINK_SLOW`, `BLINK_FAST`, `FADE`, `FLASH`,
   `RAMP_HOLD`, `SPARK`) declared in `src/core/LedGrammar.h`.
2. **Color slots** — 16 `ColorSlotId` entries (MODE_*, VERB_*, SETUP/NAV,
   CONFIRM_OK, VERB_STOP) in `ColorSlotStore` v5.
3. **Events** — each `EventId` maps to `{patternId, colorSlot, fgPct}`.
   Per-event NVS override in `LedSettingsStore.eventOverrides[]`,
   compile-time fallback in `EVENT_RENDER_DEFAULT[]`.

API : `triggerEvent(EventId, ledMask)` preempts the single-slot
`_eventOverlay` `PatternInstance`. Auto-expires per pattern math.
`renderPattern(inst, now)` dispatches on `patternId`. Tick ARPEG rendering
shares `renderFlashOverlay()` with the pattern engine (FLASH visual logic
in one place).

Public wrapper `renderPreviewPattern(inst, now)` exposes the private
dispatch to Tool 8 preview (via `ToolLedPreview` helper) with zero
duplication.

**Reuse** for new visual events : add an `EventId` entry + row in
`EVENT_RENDER_DEFAULT` (LedGrammar.cpp). Tunable params live in Tool 8.
Legacy `ConfirmType` / `triggerConfirm` were removed in step 0.9.

Details : [`led-reference.md`](led-reference.md).

---

### P6 — Store + validate + version (NVS)

Zero-migration-policy persistence : struct `{magic, version, fields}` +
`validateXxxStore()` clamp helper + `static_assert(sizeof ≤ 128)` +
`NVS_DESCRIPTORS[]` entry. Load via `NvsManager::loadBlob()`. Version bump
OR size change → silent reject → compile-time defaults apply (one Serial
warning).

**Reuse** for every new persisted config.

**Never** write migration code.

Details : [`nvs-reference.md`](nvs-reference.md).

---

### P7 — Button modifier + chord

Held button changes pot bindings context. LEFT → 4 right pots.
REAR → rear pot only.

Data : `PotBinding{potIdx, buttonMask, bankType, target, …}`,
`resolveBindings()` picks the best match per frame.

**Reuse** for new "modifier layer" for pots/pads.

**Don't cross layers** — LEFT and REAR modifiers must not overlap scope.

---

### P8 — Rising edge detection

`s_lastKeys[NUM_KEYS]`, `pressed && !wasPressed` = rising,
`!pressed && wasPressed` = falling. Synced at end of frame, AFTER pad
processing, BEFORE arp tick (order matters — see
[`runtime-flows.md`](runtime-flows.md) for the frame execution order).

Bank switch snapshots `s_lastKeys` on LEFT release to kill phantom events
on the new bank.

**Reuse** for any pad event detection.

---

### P9 — Debounced NVS write

Pot dirty → 10s debounce → background FreeRTOS task writes.

One-shot events (bank switch, scale change) → `queueXxxWrite()` sets
immediate dirty flag, background task debounces.

API : `isDirty / clearDirty` on the data, `notifyIfDirty` to signal the
task.

**Reuse** for continuous values that need eventual persistence.

**Never save on every change** — flash wear.

---

## Setup patterns (P10–P14)

### P10 — VT100 aesthetic kit

Macros in `SetupUI.h` give the full palette : box drawing
(`UNI_TL/TR/BL/BR/V/H`, `UNI_CTL/CTR/…` single-line), colors
(`VT_GREEN/CYAN/DIM/…`), ANSI controls (`VT_CLEAR`, `VT_HOME`, sync
markers), iTerm2 extensions, cockpit accents (`UNI_RIVET`, `UNI_LED_ON/OFF`,
`UNI_BAR_FULL/EMPTY`).

High-level primitives in `SetupUI` :

- `drawConsoleHeader(toolName, nvsSaved)` — reverse-video title + save badge
- `drawFrameTop/Bottom`, `drawSection("LABEL")`, `drawFrameLine(fmt, …)`,
  `drawFrameEmpty`
- `drawControlBar(controls)` — fixed bottom bar, `CBAR_SEP` between groups,
  `CBAR_CONFIRM_ANY/STRICT` templates
- `drawCellGrid(mode, …)` — 4×12 pad grid, 4 modes
  (BASELINE/MEASUREMENT/ORDERING/ROLES)
- Cockpit widgets : `drawStepIndicator`, `drawGaugeLine`,
  `drawSegmentedValue`, `drawStatusCluster`
- `drawSubMenu`, `printPrompt`, `printConfirm`, `printError`

**Reuse** every time you write a Tool.

**Never** emit raw ANSI escapes outside these macros.

**Never** print outside the 120-char console width (`CONSOLE_W`,
`CONSOLE_INNER = 116`).

Details : [`vt100-design-guide.md`](vt100-design-guide.md).

---

### P11 — Input pipeline (InputParser → NavEvent)

Serial chars → `InputParser::update()` → `NavEvent{type, accelerated, ch}`.

Semantic types : `NAV_UP/DOWN/LEFT/RIGHT`, `NAV_ENTER`, `NAV_QUIT` (q),
`NAV_DEFAULTS` (d), `NAV_TOGGLE` (t), `NAV_CHAR` (everything else).

Acceleration flag set on rapid LEFT/RIGHT repeats (<120 ms) → use for
×10 value steps. CR/LF debounced.

Confirmation unified via `SetupUI::parseConfirm(ev) →
ConfirmResult{PENDING | YES | NO}`. Y/y = YES, any other key = NO (loose),
or use `CBAR_CONFIRM_STRICT` template for strict y/n.

**Reuse** every Tool.

**Never** read `Serial.available()` directly in tool code.

---

### P12 — Pot navigation (SetupPotInput)

Two pot channels (right 1, right 2) as Tool input. Modes :

- **RELATIVE** (Bresenham-like delta accumulator) — slower scrolling, no
  anchor needed.
- **ABSOLUTE** (differential + re-anchor within `ANCHOR_WINDOW`) — faster,
  "snaps" to pot position after a full turn.

API : `seed(ch, &target, min, max, mode, stepsHint)` on cursor move →
`update()` every frame (after `PotFilter::updateAll()`) →
`getMove(ch)` to read edge.

**Reuse** for Tools where a pot drives a cursor or numeric value.

**Gotcha** : must call `PotFilter::updateAll()` BEFORE
`SetupPotInput::update()` in the Tool loop.

---

### P13 — Save feedback (flashSaved + NVS badge)

Visual confirmation of NVS write success :

- `SetupUI::flashSaved()` — 120 ms reverse-video header pulse + LED flash.
  Call after `saveBlob()` returns true.
- `drawConsoleHeader(tool, nvsSaved)` — permanent badge on header. `nvsSaved`
  comes from `NvsManager::checkBlob()` on menu enter.

**Reuse** in every Tool that writes to NVS.

**Don't** replace with a text "saved" line — the flash is part of the
aesthetic.

Pair with `NvsManager::checkBlob()` to determine the initial badge state.

**Cost** : `flashSaved()` is blocking ~300 ms. Never call per-arrow. See
[`setup-tools-conventions.md`](setup-tools-conventions.md) §2.

---

### P14 — LED preview during tool editing

Tools that edit a visual parameter show a live LED preview on a dedicated
pair (e.g. LEDs 3-4). Bypass LedController's priority logic via direct
`_leds->setPixel()` calls while the tool is active. LedController returns
to normal on tool exit.

Companion : `SetupUI::showToolActive(idx)` / `showPadFeedback(pad)` /
`showCollision(pad)` for generic LED cues (setup comet, pad hit feedback,
role collision warning).

**Reuse** for any Tool whose parameter has a visual side effect (LED,
potential buzzer, haptic).

**Don't** re-render the whole state machine — just override the relevant
pixels.

Phase 0.1 : Tool 8 ships a richer preview via
[`ToolLedPreview`](../../src/setup/ToolLedPreview.h). Tool 8 calls
`begin(leds, potRouter->getTempoBPM())` at `run()` entry and
`setContext(ctx, params)` on cursor / value change. Pattern rendering
dispatches to `LedController::renderPreviewPattern` (public wrapper, zero
runtime duplication). Rate-capped at 50 Hz via internal `_lastUpdateMs`
gate to spare Core 1 under burst arrow-key edits.
