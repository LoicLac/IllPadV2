# ILLPAD V2 — Architecture Briefing

Index + mental model reference for agents and new sessions. Complements
CLAUDE.md (the spec) with *how the system thinks* and *where to look*.

**This is not a tutorial.** It is a navigation tool. Read §0 first to choose
your scope, then load only what you need.

---

## 0. Scope Triage (answer before reading)

**Q1 — Are you MODIFYING code or EXPLORING / DESIGNING?**

- **Exploring** ("can we do X?", "what would Y require?", open-ended design)
  → Load: §1 Mental Models (all) + §2 Flows + §4 Setup↔Runtime + §5 Reusable
    Patterns + §6 Affinity Matrix. Read broadly in the affected domains.
  → Use §7 SKIP list to avoid wasted reads.
  → Do NOT look for a "task recipe". The answer requires synthesis.

- **Modifying** → go to Q2.

**Q2 — Is the change TIGHT or BROAD?**

- **Tight** (one parameter, one bug, one param-type added) → go to Q3.
- **Broad** (new mode of play, new category of role, redesign of a subsystem)
  → treat as EXPLORING (Q1 broad path). You are redesigning, not editing.
  → Example: "add a loop mode" = broad. "add a new ARP pattern" = tight.

**Q3 — Is there an exact match in §3 Task Index?**

- **Yes** → follow the listed files + function. Don't overread.
- **No** → find the closest domain in §8 Domain Entry Points. Read the entry
  function, then the relevant §2 Flow. Stop there unless the edit reveals a
  cross-cutting impact → escalate to Q2 "broad".

**Maintaining this document is part of every commit.** If you modify a function
or pattern referenced in §2 / §4 / §5 / §8, update the relevant entry in the
same commit. A stale briefing is a bug.

---

## 1. Mental Models

How each core system *thinks*. Load these for any broad or exploratory work.

### MM1 — Sensing / inter-core
Core 0 owns MPR121 polling + pressure pipeline → publishes into one of two
buffers via `s_active.store(release)`. Core 1 reads via `s_active.load(acquire)`.
Lock-free, never torn. Slow params go back Core 1→Core 0 via `std::atomic<…>`
relaxed. **Don't**: introduce any mutex on this path.

### MM2 — Bank slots always alive
8 `BankSlot` = 8 MIDI channels, always allocated, always running. `isForeground`
flag gates pad input only. Background ARPEG engines keep ticking; BG NORMAL
banks preserve pitchBend/scale/velocity. Bank switch = move foreground marker
+ kill old channel's active notes. **Don't**: treat BG as dormant.

### MM3 — Pad → note (3 stacked layers)
`physical pad → padOrder[48] → rank → ScaleResolver(root, mode) → MIDI note`.
padOrder set in Tool 2 (runtime-static). Scale per-bank, runtime-switchable.
Control roles (bank/scale/arp pads) filtered *before* the music block.
**Invariant**: `_lastResolvedNote[pad]` stored at noteOn, reused at noteOff —
never re-resolves.

### MM4 — Arp: pile + sequence + scheduler
Pile = padOrder positions held/captured. Sequence = pattern expansion (with
octave repeats, up to 192 entries), rebuilt lazy on `_sequenceDirty`. Scheduler
= per-engine tick accumulator → fires step → schedules noteOff+noteOn (atomic
pair, P1 + P3) → `processEvents()` drains by wall clock. States: `IDLE` /
`WAITING_QUANTIZE` / `PLAYING`. **Don't**: couple a new rhythm engine to a
single bank — `MAX_ARP_BANKS = 4` engines share the scheduler.

### MM5 — Clock cascade
External 0xF8 ticks per source (USB/BLE) → atomic counters → PLL IIR
(α_USB=0.3, α_BLE=0.15). Priority: USB > BLE > last known (5s memory) >
internal (tempo pot). Master mode generates ticks from internal BPM. **Don't**:
expect Start/Stop/Continue (ignored by design — ILLPAD = instrument).

### MM6 — Pot catch
Pot must physically reach `storedValue` before it can write. Global targets
(tempo, shape, …) keep catch across bank switches + propagate `storedValue`
across contexts. Per-bank targets reset catch on switch. Brightness bypasses
catch. **Decide on new target**: global or per-bank? Flag via
`isPerBankTarget()`.

### MM7 — LED priority + overlays
9-level priority: Boot > Setup > Chase > Error > Battery > Bargraph >
[Event overlay tracking] > Calibration > Normal display + event overlay.
Event overlays are *overlays* (blit on top via `renderPattern()`, never
clear). Normal display encodes multi-bank state (FG solid / BG dim via
`_bgFactor` / tick flashes via `renderFlashOverlay()` / sine pulse for
stopped-loaded ARPEG). The 3-layer LED grammar (patterns × color slots
× event mapping, see P5) is the single source of truth for all visual
events since Phase 0 refactor 2026-04-19. Phase 0.1 respec (2026-04-20)
adds `CSLOT_VERB_STOP` (16th slot) and 3 tick duration caches
(BEAT/BAR/WRAP) without changing the grammar structure ; BEAT is wired
now for ARPEG step, BAR/WRAP are stored but consumed Phase 1+ by
LoopEngine. **Don't**: design a new visual by writing pixels — declare
a pattern + color slot + event entry first (or add a new PatternId to
the palette if truly novel).

### MM8 — Setup is a config mirror
Setup Tools are the UI surface of runtime config. Every persisted user-param
follows the 4-link chain Runtime↔Store↔Tool↔NVS (see §4). Boot-only (main
loop paused) → safe to cache resolved values. VT100 aesthetic is a product
feature, not a convenience — never simplify.

---

## 2. Five Critical Data Flows

### Pad Touch → MIDI NoteOn
```
Core 0: MPR121 I2C poll → pressure pipeline → s_buffers[writeIdx]
        → s_active.store(writeIdx, release)                    [INTER-CORE]
Core 1: state = s_buffers[s_active.load(acquire)]
        → MIDI block runs ONLY if !bankManager.isHolding() && !scaleManager.isHolding()
        → edge detect (pressed && !wasPressed)
        → skip if s_controlPadManager.isControlPad(i) (control pads emit CC, not notes)
          (skip also in pile rebuild for ARPEG)
        → velocity = baseVelocity ± random(variation)
        → MidiEngine::noteOn(padIndex, vel, padOrder, scale)
          → ScaleResolver::resolve() → MIDI note
          → _lastResolvedNote[padIndex] = note                 [STORED FOR NOTEOFF]
          → MidiTransport::sendNoteOn (USB + BLE simultaneously)

Left-button release safety (detect s_wasHolding → !holding edge):
  NORMAL:      for all pads not pressed: MidiEngine::noteOff(i)        [idempotent]
  ARPEG HOLD OFF: for all pads not pressed (skip holdPad):
                  ArpEngine::removePadPosition(s_padOrder[i])          [idempotent]
```
Entry points: `main.cpp::handlePadInput()` [562-577] (dispatches to
`processNormalMode` [466-499] and `processArpMode` [501-536]),
`handleLeftReleaseCleanup()` [538-560], `MidiEngine::noteOn/noteOff` [47-72],
`ScaleResolver::resolve`, `MidiTransport::sendNoteOn` [196-214].

### Arp Tick → MIDI NoteOn
```
ClockManager::update() → _currentTick++
→ ArpScheduler::tick()
  → tickAccum += ticksElapsed
  → while (tickAccum >= divisor):
      synthTick = currentTick - tickAccum
      ArpEngine::tick(synthTick, globalTick=currentTick)
        → switch (currentState()):             [state dispatch]
            IDLE: flush + stop, return              (pile empty)
            WAITING_QUANTIZE: check boundary, return or proceed
            PLAYING: Play-mode captured or Stop-mode auto-play
        → executeStep():                       [note scheduling]
            → resolve position → MIDI note via ScaleResolver
            → schedule noteOff FIRST (atomic pair)
            → if shuffle: schedule noteOn (delayed)
              else: refCountNoteOn() (immediate send)
→ ArpScheduler::processEvents()
  → for each pending event where time arrived:
      refCountNoteOn() or refCountNoteOff()
      → MIDI send only on refcount transitions (0→1, 1→0)
```
Entry points: `ClockManager::generateTicks` [181-203], `ArpScheduler::tick`
[98-131] + `processEvents` [140-146], `ArpEngine::currentState` [524-530],
`ArpEngine::tick` [536-566], `executeStep` [572-654], `processEvents`
[660-673].

### Bank Switch (all side effects in order)
```
switchToBank() calls [BankManager.cpp:181-210]:
1. sendPitchBend(8192) on OLD channel         [BankManager.cpp:186]
2. allNotesOff() on OLD channel               [BankManager.cpp:187]
   — internally drains AT ring buffer + resets AT rate limiter
     [MidiEngine.cpp:98-101]
3. Update foreground flags                    [BankManager.cpp:190-192]
4. setChannel(newBank)                        [BankManager.cpp:195]
5. sendPitchBend(newBank.pitchBendOffset)     [BankManager.cpp:196-197]
   — NORMAL banks only. ARPEG banks: no pitch bend sent (no aftertouch, spec)
6. LED: setCurrentBank + triggerEvent(EVT_BANK_SWITCH)  [BankManager.cpp:200-203]
— back in handleManagerUpdates() [main.cpp:602] —
7. queueBankWrite() to NVS                    [main.cpp:609]
8. reloadPerBankParams(newSlot)               [main.cpp:610]
   → loadStoredPerBank() into PotRouter       [reloadPerBankParams:593-596]
9. seedCatchValues(keepGlobalCatch=true)      [reloadPerBankParams:597]
    — reseeds stored values; global targets keep catch state (tempo, shape…)
    — per-bank targets lose catch (will be uncaught by step 10)
10. resetPerBankCatch() — uncatch per-bank only [reloadPerBankParams:598]
11. ControlPadManager.update() detects bank switch edge on next frame,
    runs per-mode handoff (gate-family : CC=0 on old channel + re-sync
    on new channel ; setter : silent preserve of old-channel value).
```

#### LEFT + Bank Pad: deferral and double-tap Play/Stop toggle
`BankManager::update()` uses rising-edge detection on bank pads while LEFT is
held, plus a per-pad timestamp (`_lastBankPadPressTime[b]`) for double-tap
tracking. Window = `_doubleTapMs` (100–250ms, settings default 200ms, same
value as Play-mode note double-tap).

```
Rising edge on bank pad b (while LEFT held):
  if 2nd tap within window AND _banks[b].type == BANK_ARPEG:
    // Same event chain as Hold pad. Only difference: BG target → keys=nullptr.
    ArpEngine::setCaptured(!wasCaptured, transport,
                           keys = (b == _currentBank) ? keyIsPressed : nullptr,
                           _holdPad):
      Stop → Play (captured=true):
        _pausedPile && pile>0 → relaunch from step 0, waitForQuantize if needed
        else                  → just flip _captured, _pausedPile=false
      Play → Stop (captured=false):
        anyFingerDown (excl. holdPad) → clearAllNotes() (live mode takes over)
        no fingers (or BG: keys==nullptr)
                                       → flushPendingNoteOffs, _playing=false,
                                         _waitingForQuantize=false, _pausedPile=true
    Toggle always fires — LED always updates.
    Always: consume press (timestamp=0), cancel pending switch, continue.
    LED: triggerEvent(EVT_STOP|EVT_PLAY, mask=1<<b) — FADE overlay on target
         pad LED (may be BG bank). Double-tap NEVER switches bank.

  _pausedPile semantics (persistent):
    _pausedPile=true means "Stop with pile kept — next Play relaunches from step 0".
    In Stop mode, first pad press wipes the paused pile before entering live mode
    (processArpMode in main.cpp).
    clearAllNotes(), setCaptured(true), and addPadPosition() all reset _pausedPile=false.

  2nd tap on NORMAL bank (wasRecent but no play/stop semantics):
    ignore — preserve pending switch and 1st-tap timestamp so the natural
    timeout still commits at T0 + _doubleTapMs. Re-arming would postpone the
    switch on every repeat tap.

  Else (1st tap):
    _lastBankPadPressTime[b] = now
    if b == _currentBank: continue
    arm pending switch: _pendingSwitchBank = b, _pendingSwitchTime = now
    (switch deferred ~doubleTapMs for ALL bank types — uniform tactile feel ;
     ARPEG also uses this window to detect the 2nd tap for Play/Stop toggle)
```

Pending switch resolution:
- **Natural timeout** (`now - _pendingSwitchTime >= _doubleTapMs`): commit `switchToBank(target)`.
- **LEFT release while pending**: fast-forward — commit `switchToBank(target)` on release edge.

`_switchedDuringHold` flag: set when a switch occurs while LEFT is held. On
LEFT release, `s_lastKeys` is snapshotted from current `keyIsPressed` to
prevent phantom noteOn/noteOff when resuming play on the new bank.

Entry points: `BankManager::update/switchToBank` (`_holdPad` set at boot via
`setHoldPad()`), `ArpEngine::setCaptured` (called by both Hold pad in
`main.cpp::handleHoldPad` and BankManager double-tap path), `LedController`
CONFIRM_HOLD_ON/OFF (honors `_confirmLedMask` → fades the target bank's LED,
not just foreground).

### Scale Change
```
ScaleManager::processScalePads() detects root/mode/chromatic pad press
  → NORMAL: allNotesOff() immediately (prevents orphan notes)
  → ARPEG: NO flush — pending events carry resolved notes, next tick re-resolves
  → Set flag: _scaleChangeType = ROOT|MODE|CHROMATIC
handleManagerUpdates() consumes flag:
  → NVS queue + ArpEngine::setScaleConfig() + LED confirm
  → Group propagation: if currentBank.scaleGroup > 0, iterate all other banks
    in the same group: copy scSlot.scale, queueScaleWrite(i, scale),
    setScaleConfig() if ARPEG. No allNotesOff on propagated banks
    (NORMAL bg = no active notes, ARPEG re-resolves on next tick).
  → LED confirmation: triggerEvent(EVT_SCALE_*, ledMask) with bitmask of
    all group members — all group LEDs blink together (not just foreground).
```
Entry points: `ScaleManager::processScalePads` [114-201],
`main.cpp::handleManagerUpdates` [602-665], `reloadPerBankParams` [580-599].
Scale groups stored in `BankTypeStore.scaleGroup[]` (0=none, 1..NUM_SCALE_GROUPS=A..D),
accessed via `NvsManager::getLoadedScaleGroup()`. Leader-wins propagation at
boot in `NvsManager::loadAll()` ensures consistency across NVS per-bank scale
blobs.

### Pot → Parameter
```
PotFilter::updateAll(): oversample 16× → adaptive EMA → deadband gate → edge snap → sleep/wake
  State machine per pot: ACTIVE → SETTLING → SLEEPING (peek every 50ms)
  Output: getStable() (0-4095), hasMoved() (bool)
PotRouter::update():
  PotFilter::updateAll()
  resolveBindings(): button state + bank type → find best binding
  for each pot with hasMoved(): applyBinding()
applyBinding():
  adc = PotFilter::getStable(potIndex)
  if TARGET_LED_BRIGHTNESS: bypass catch, apply immediately
  if !caught: compare adc vs storedValue, show uncaught bargraph, WAIT
  if caught: convert ADC → parameter range, write output, show bargraph
  → Global targets: propagate storedValue across contexts
handlePotPipeline(): read getters → write to BankSlot/ArpEngine/atomics
  → consumeCC/consumePitchBend → MidiTransport sends
```
Entry points: `PotFilter::updateAll`, `PotRouter::update` [318-327] +
`resolveBindings` [334-381] + `applyBinding` [386-544],
`main.cpp::handlePotPipeline` [715-770] + `pushParamsToEngine` [702-714].
Bargraph unifié : `PotRouter::hasBargraphUpdate()` déclenche systématiquement
`LedController::showPotBargraph()` (tempo inclus — pas de variante pulsée).

---

## 3. Task Index (tight only)

Only use this for truly standardized additions. If your task is not listed,
it is probably **broad** — go back to §0 Q2 and treat as exploration.

| Task | Files to read | Function to touch | Don't need to touch |
|---|---|---|---|
| Add a new shuffle template | `midi/GrooveTemplates.h` | `SHUFFLE_TEMPLATES[]` array, bump `NUM_SHUFFLE_TEMPLATES` | ArpEngine, PotRouter (auto pick-up) |
| Add a new scale mode | `ScaleResolver.cpp` (`scaleIntervals[][7]`) | `ScaleResolver::resolve()` table | MidiEngine, ArpEngine (re-résolvent auto) |
| Add a new ARP pattern | `ArpEngine.h` (enum `ArpPattern`, array sizes), `ArpEngine.cpp` (`rebuildSequence()`), `main.cpp` (`s_patNames[]`) | `ArpEngine::rebuildSequence()` switch case | ScaleResolver, ArpScheduler, NVS, LED |
| Add a new pot target (pure param, no new UX) | `PotRouter.h` (enum `PotTarget`), `PotRouter.cpp` (`applyBinding()`, `isPerBankTarget()`, `getDiscreteSteps()`), `ToolPotMapping.cpp` (pool + label + color) | `PotRouter::applyBinding()` switch + getter | MidiEngine, ArpEngine (sauf si cible les alimente) |
| Add a new LED event (unified grammar) | `LedGrammar.h` (enum `EventId` entry), `LedGrammar.cpp` (`EVENT_RENDER_DEFAULT[]` row pointing to a pattern + color slot), `LedSettingsStore` field if the pattern needs a new global, `ToolLedSettings` if the event should be tunable by the user (add a new `LineId` in the relevant section ; wire metadata in `shapeForLine`/`colorSlotForLine`/`readNumericField`/`writeNumericField`/`descriptionForLine`) | `LedController::triggerEvent` callsite in metier flow | Flow métier (bank/scale/arp) sauf pour déclencher |

---

## 4. Setup ↔ Runtime Contracts

**The central source of friction** when runtime work goes out of sync with
setup Tools. Walk through these tables before ANY user-configurable change.

### Table 1 — Setup Tool-managed configs (VT100 → NVS)

| Runtime concept | Store struct | Setup Tool | NVS ns/key | Sync points quand tu modifies le runtime |
|---|---|---|---|---|
| Pressure calibration | `CalDataStore` | Tool 1 ToolCalibration | `illpad_cal` / `cal` | Nouveau field → `validateCalDataStore()` + calibration loop + pressure pipeline. Rarely touched. |
| Pad ordering (physique → rank 0-47) | `NoteMapStore` | Tool 2 ToolPadOrdering | `illpad_nmap` / `pads` | `padOrder[48]` consommé partout — statique après setup. |
| Bank pads (8 control pads) | `BankPadStore` | Tool 3 ToolPadRoles | `illpad_bpad` / `pads` | Nouveau rôle → Tool 3 collision check + category color ; consommateur runtime (BankManager) |
| Scale pads (7 root + 7 mode + 1 chrom) | `ScalePadStore` | Tool 3 ToolPadRoles | `illpad_spad` / `pads` | Étendre catégorie → Tool 3 grid + ScaleManager |
| Control pad assignments (12 sparse entries + 3 global DSP params) | `ControlPadStore` v2 | Tool 4 ToolControlPads | `illpad_ctrl` / `pads` | Add entry : new slot + Tool 4 edit ; edit globals via 'g' key → UI_GLOBAL_EDIT. Consumed at boot via ControlPadManager::applyStore. |
| Arp pads (1 hold + 4 octave) | `ArpPadStore` | Tool 3 ToolPadRoles | `illpad_apad` / `pads` | Ajouter un pad de contrôle ARPEG → Tool 3 + main.cpp handleHoldPad + ScaleManager |
| Bank types + quantize mode + scaleGroup | `BankTypeStore` | Tool 5 ToolBankConfig | `illpad_btype` / `config` | Nouveau `BankType` (ex: LOOP) → Tool 5 type cycle + ArpEngine assignment ([main.cpp:345-362](../../src/main.cpp:345)) + LED state machine + `validateBankTypeStore()` |
| Global settings (profile, AT rate, BLE, clock, doubleTap, bargraph, panic, batCal) | `SettingsStore` | Tool 6 ToolSettings | `illpad_set` / `settings` | Nouveau field → Tool 6 case switch + validator + bump version + apply dans `main.cpp` setup() ([main.cpp:329-339](../../src/main.cpp:329)) |
| Pot bindings (user-configurable pot → parameter) | `PotMappingStore` (normalMap + arpegMap) | Tool 7 ToolPotMapping | `illpad_pmap` / `mapping` | Nouveau `PotTarget` enum → pool line, label+color, `getDiscreteSteps()`, `isPerBankTarget()`, `applyBinding()` case |
| Pot filter tuning (snap, deadband, sleep, wake) | `PotFilterStore` | **none** (descriptor in T7 "Monitor" range but no UI) | `illpad_pflt` / `config` | **Friction zone** : modifiable only via code + flash. Intent "Monitor in T7" unimplemented. |
| LED pattern globals + event overrides + intensities + gamma | `LedSettingsStore` v8 | Tool 8 ToolLedSettings (single-view 6 sections : NORMAL / ARPEG / LOOP / TRANSPORT / CONFIRMATIONS / GLOBAL, Phase 0.1 respec) | `illpad_lset` / `settings` | Nouveau event → `EventId` + `EVENT_RENDER_DEFAULT[]` + optional line in Tool 8 LineId enum ; nouveau global pattern param → field in v8 Store + line in Tool 8 + apply dans `renderPattern()` |
| Color slots (14 preset + hue, 16 slot IDs) | `ColorSlotStore` v5 | Tool 8 ToolLedSettings | `illpad_lset` / `colors` | Ajouter un slot → `COLOR_SLOT_COUNT` + Tool 8 LineId (nouvelle ligne COLOR) + `resolveColorSlot()` + default dans NvsManager.cpp |

### Table 2 — Runtime-managed persisted state (no Setup Tool, written via pot/pad gestures)

No VT100 UI. Written by `NvsManager::queue*Write()` from runtime. Not in
`NVS_DESCRIPTORS[]`.

| Runtime concept | Store / type | Namespace / key | Écrit par | Chargé par |
|---|---|---|---|---|
| Current bank (0-7) | scalar `uint8_t` | `illpad_bank` / `current` | `queueBankWrite()` on bank switch | `loadAll()` boot |
| ScaleConfig per bank | `ScaleConfig` | `illpad_scale` / `cfg_0..cfg_7` | `queueScaleWrite()` on scale change | `loadAll()` boot (leader-wins propagation for scale groups) |
| Arp pot params per bank (gate/shuffle/div/pattern/oct) | `ArpPotStore` | `illpad_apot` / `bank_0..7` | Pot debounce + `queueArpOctaveWrite()` on octave | `loadAll()` → `getLoadedArpParams()` |
| Base velocity + variation per bank | struct | `illpad_bvel` / `bank_0..7` | Pot debounce | `loadAll()` |
| Pitch bend offset per bank (NORMAL) | `uint16_t` | `illpad_pbnd` / `bank_0..7` | Pot debounce | `loadAll()` |
| Tempo BPM (global) | `uint16_t` | `illpad_tempo` / `bpm` | Pot debounce | `loadAll()` |
| LED brightness (global) | `uint8_t` | `illpad_led` / `bright` | Pot debounce | `loadAll()` |
| Pad sensitivity (global) | `uint8_t` | `illpad_sens` / `level` | Pot debounce | `loadAll()` |
| Response shape / slew / AT deadzone (global) | `PotParamsStore` | `illpad_pot` / `params` | Pot debounce | `loadAll()` |

**Implicit rule**: new pot-accessible parameters persist via this channel
automatically — no new namespace. But a getter in PotRouter, a push in
`handlePotPipeline()` or `pushParamsToEngine()`, and the appropriate dirty
flag are required.

---

## 5. Reusable Patterns

API + callsites + reuse scenarios. Grab these instead of inventing.

### Runtime patterns (P1-P9)

#### P1 — Refcount noteOn/Off
Multiple overlapping noteOn on same MIDI note without duplicate messages. MIDI
fires only on 0→1 and 1→0. `refCountNoteOn(transport, note, vel)` /
`refCountNoteOff(transport, note)`. Used: `ArpEngine.cpp`. **Reuse** for any
engine that can overlap notes (loop mode, chord sequencer). Pair with P3:
schedule noteOff BEFORE noteOn (atomic pair).

#### P2 — Dirty flag → consume (auto-clear)
One-shot producer→consumer signal. `consumeXxx()` returns state + clears.
Used: `consumeTickFlash`, `consumeScaleChange`, `hasOctaveChanged`. **Reuse**
for "X happened this frame, tell someone". Single consumer per flag.

#### P3 — Scheduled event queue
Fire a MIDI event when `micros() >= fireTimeUs`. Fixed-size per-engine queue
(MAX_PENDING_EVENTS=64). `scheduleEvent(time, note, vel)` + `processEvents(transport)`
every frame. Used: `ArpEngine` for shuffle + gate. **Reuse** for any sub-frame
timed MIDI. **Gotcha**: queue full → schedule fails → cancel the paired event
manually.

#### P4 — Pot catch
Prevent param jumps when pot vs stored value mismatch. `CatchState{caught,
storedValue}` + catch window check before writing. Used:
`PotRouter::applyBinding`. **Reuse** for every new user-facing continuous
param shared across contexts. Define per-bank vs global policy.

#### P5 — Event overlay (LED grammar, Phase 0 refactor 2026-04-19)
Temporary event visualization on top of normal display via a unified
3-layer pattern grammar (LED spec `2026-04-19-led-feedback-unified-design.md`) :
  1. **Patterns** — palette of 9 fixed behaviors (SOLID, PULSE_SLOW,
     CROSSFADE_COLOR, BLINK_SLOW, BLINK_FAST, FADE, FLASH, RAMP_HOLD,
     SPARK) declared in `src/core/LedGrammar.h`.
  2. **Color slots** — 16 `ColorSlotId` entries (MODE_*, VERB_*,
     SETUP/NAV, CONFIRM_OK, VERB_STOP) in `ColorSlotStore` v5.
  3. **Events** — each `EventId` maps to `{patternId, colorSlot, fgPct}`.
     Per-event NVS override in `LedSettingsStore.eventOverrides[]` ;
     compile-time fallback in `EVENT_RENDER_DEFAULT[]`.

API : `triggerEvent(EventId, ledMask)` preempts the single-slot
`_eventOverlay` PatternInstance. Auto-expires per pattern math.
`renderPattern(inst, now)` dispatches on `patternId`. Tick ARPEG rendering
shares `renderFlashOverlay()` with the pattern engine (FLASH visual logic
in one place). Public wrapper `renderPreviewPattern(inst, now)` exposes
the private dispatch to Tool 8 preview (via `ToolLedPreview` helper) with
zero duplication.

**Reuse** for new visual events : add `EventId` entry + row in
`EVENT_RENDER_DEFAULT` (LedGrammar.cpp). Tunable params — Tool 8 single-view
(Phase 0.1 respec) groups them under musician-facing sections : NORMAL /
ARPEG / LOOP base colors + brightness ; TRANSPORT play/stop/waiting/breathing
+ tick common FG/BG / tick verb colors (PLAY/REC/OVERDUB) / tick durations
(BEAT/BAR/WRAP) ; CONFIRMATIONS bank/scale/octave/SPARK ; GLOBAL bg factor
+ gamma. Legacy `ConfirmType` / `triggerConfirm` removed in step 0.9.

Phase 0.1 note : Tool 8 now uses a single scrollable view with 6 sections
and a new horizontal-focus edit paradigm (see
[setup-tools-conventions §4.4](setup-tools-conventions.md#44-multi-value-row--geometric-visual-navigation-phase-01--tool-8-canonical)).
Live preview is decoupled into [`ToolLedPreview`](../../src/setup/ToolLedPreview.h)
which routes pattern previews through `LedController::renderPreviewPattern` —
zero runtime duplication.

#### P6 — Store + validate + version (NVS)
Zero-migration-policy persistence: struct `{magic, version, fields}` +
`validateXxxStore()` clamp helper + `static_assert(sizeof ≤ 128)` +
`NVS_DESCRIPTORS[]` entry. Load via `NvsManager::loadBlob()`. Version bump OR
size change → silent reject → compile-time defaults apply (one Serial
warning). **Reuse** for every new persisted config. **Never** write migration
code.

#### P7 — Button modifier + chord
Held button changes pot bindings context. LEFT → 4 right pots. REAR → rear
pot only. `PotBinding{potIdx, buttonMask, bankType, target, …}`,
`resolveBindings()` picks best match per frame. **Reuse** for new "modifier
layer" for pots/pads. **Don't cross layers**.

#### P8 — Rising edge detection
`s_lastKeys[NUM_KEYS]`, `pressed && !wasPressed` = rising, `!pressed &&
wasPressed` = falling. Synced at end of frame, AFTER pad processing, BEFORE
arp tick (order matters). Bank switch snapshots `s_lastKeys` on LEFT release
to kill phantom events. **Reuse** for any pad event detection.

#### P9 — Debounced NVS write
Pot dirty → 10s debounce → background task writes. One-shot events (bank
switch, scale change) → `queueXxxWrite()` immediate dirty, task debounces.
`isDirty/clearDirty` + `notifyIfDirty`. **Reuse** for continuous values that
need eventual persistence. Never save on every change.

### Setup patterns (P10-P14)

#### P10 — VT100 aesthetic kit
Macros in `SetupUI.h` give the full palette: box drawing (`UNI_TL/TR/BL/BR/V/H`,
`UNI_CTL/CTR/…` single-line), colors (`VT_GREEN/CYAN/DIM/…`), ANSI controls
(`VT_CLEAR`, `VT_HOME`, sync markers), iTerm2 extensions, cockpit accents
(`UNI_RIVET`, `UNI_LED_ON/OFF`, `UNI_BAR_FULL/EMPTY`).

High-level primitives in `SetupUI`:
- `drawConsoleHeader(toolName, nvsSaved)` — reverse-video title + save badge
- `drawFrameTop/Bottom`, `drawSection("LABEL")`, `drawFrameLine(fmt, …)`, `drawFrameEmpty`
- `drawControlBar(controls)` — fixed bottom bar, `CBAR_SEP` between groups, `CBAR_CONFIRM_ANY/STRICT` templates
- `drawCellGrid(mode, …)` — 4×12 pad grid, 4 modes (BASELINE/MEASUREMENT/ORDERING/ROLES)
- Cockpit widgets: `drawStepIndicator`, `drawGaugeLine`, `drawSegmentedValue`, `drawStatusCluster`
- `drawSubMenu`, `printPrompt`, `printConfirm`, `printError`

**Reuse** every time you write a Tool. **Never** emit raw ANSI escapes
outside these macros. **Never** print outside the 120-char console width
(`CONSOLE_W`, `CONSOLE_INNER=116`). Keep the aesthetic uniform — see
`docs/reference/vt100-design-guide.md`.

#### P11 — Input pipeline (InputParser → NavEvent)
Serial chars → `InputParser::update()` → `NavEvent{type, accelerated, ch}`.
Semantic types: `NAV_UP/DOWN/LEFT/RIGHT`, `NAV_ENTER`, `NAV_QUIT` (q),
`NAV_DEFAULTS` (d), `NAV_TOGGLE` (t), `NAV_CHAR` (everything else).
Acceleration flag set on rapid LEFT/RIGHT repeats (<120ms) → use for x10
value steps. CR/LF debounced.

Confirmation unified via `SetupUI::parseConfirm(ev) →
ConfirmResult{PENDING|YES|NO}`. Y/y = YES, any other key = NO (loose), or use
`CBAR_CONFIRM_STRICT` template for y/n strict.

**Reuse** every Tool. **Never** read `Serial.available()` directly in tool
code.

#### P12 — Pot navigation (SetupPotInput)
Two pot channels (right 1, right 2) as Tool input. Modes:
- **RELATIVE** (Bresenham-like delta accumulator) — slower scrolling, no anchor needed.
- **ABSOLUTE** (differential + re-anchor within `ANCHOR_WINDOW`) — faster, "snaps" to pot position after a full turn.

API: `seed(ch, &target, min, max, mode, stepsHint)` on cursor move →
`update()` every frame (after `PotFilter::updateAll()`) → `getMove(ch)` to
read edge. **Reuse** for Tools where a pot drives a cursor or numeric value.
**Gotcha**: must call `PotFilter::updateAll()` BEFORE `SetupPotInput::update()`
in the Tool loop.

#### P13 — Save feedback (flashSaved + NVS badge)
Visual confirmation of NVS write success:
- `SetupUI::flashSaved()` — 120ms reverse-video header pulse + LED flash. Call after `saveBlob()` returns true.
- `drawConsoleHeader(tool, nvsSaved)` — permanent badge on header. `nvsSaved` comes from `NvsManager::checkBlob()` on menu enter.

**Reuse** in every Tool that writes to NVS. **Don't** replace with a text
"saved" line — the flash is part of the aesthetic. Pair with
`NvsManager::checkBlob()` to determine the initial badge state.

#### P14 — LED preview during tool editing
Tools that edit a visual parameter show a live LED preview on a dedicated
pair (Tool 7 uses LED 3-4). Bypass LedController's priority logic via direct
`_leds->setPixel()` calls while the tool is active. LedController returns to
normal on tool exit.

Companion: `SetupUI::showToolActive(idx)` / `showPadFeedback(pad)` /
`showCollision(pad)` for generic LED cues (setup comet, pad hit feedback,
role collision warning).

**Reuse** for any Tool whose parameter has a visual side effect (LED,
potential buzzer, haptic). **Don't**: re-render the whole state machine —
just override the relevant pixels.

Phase 0.1 : Tool 8 ships a richer preview via the helper
[`ToolLedPreview`](../../src/setup/ToolLedPreview.h). Tool 8 owns it, calls
`begin(leds, potRouter->getTempoBPM())` at `run()` entry and
`setContext(ctx, params)` on cursor / value change. The helper encapsulates
mono-FG mockup, LOOP-ticks mockup (tempo-synced), one-shot replay with black
timer (§6.4 formula), breathing, and crossfade. Pattern rendering dispatches
to `LedController::renderPreviewPattern` (public wrapper, zero runtime
duplication). Rate-capped at 50 Hz via internal `_lastUpdateMs` gate to spare
Core 1 under burst arrow-key edits.

---

## 6. Cross-cutting Affinity Matrix

For broad / creative tasks. What domains will you likely touch, what to
skip, what existing system to read as reference.

| Creative task family | Domains impliqués (à lire) | Domains NON impliqués (skip) | Systèmes existants à lire comme référence |
|---|---|---|---|
| **New mode of play** (loop, beat, sequencer, drum pad) | Clock, BankType, State machine, Scheduling, Pile, LED, PotMapping, Setup Tool 4, NVS | Pressure pipeline, ScaleResolver (sauf si notes), Battery, BLE transport detail | ArpEngine + ArpScheduler (pattern complet) + BankManager |
| **New pad role category** (loop controls, macro pads, etc.) | Tool 3 collision check, nouveau manager runtime, LED state, NVS nouveau Store, main.cpp dispatch | ArpEngine internals, MIDI transport, Clock | ScaleManager (template role-based) + BankManager (template pad-detection) + Tool 3 |
| **Rethink visual feedback** (nouveau pattern, animation, priorité) | LedController (pattern engine + renderFlashOverlay + `renderPreviewPattern` wrapper), LedGrammar (PatternId/EventId/defaults), LedSettingsStore v7 (globals + eventOverrides[] + 3 tick durations), ColorSlotStore v5 (16 slots incl. CSLOT_VERB_STOP), Tool 8 (single-view 6 sections, Phase 0.1 respec) + ToolLedPreview helper, tous les callsites de `triggerEvent()` | Runtime métier (consomment l'API mais pas l'animation) | LedController::update() + renderPattern + Tool 8 LineId enum + ToolLedPreview context dispatch |
| **New MIDI output class** (MPE, MCU, program change, etc.) | MidiTransport, MidiEngine, PotRouter (si déclencheur), setup Tool 5 (si user-config) | Arp internals, LED, Battery | MidiTransport::sendCC / sendPitchBend / sendPolyAftertouch |
| **Redesign clock/tempo** (tap tempo, tempo per bank, swing global) | ClockManager complet, ArpScheduler tick loop, PotRouter tempo target, setup Tool 5 | Pressure, Battery, LED | ClockManager + tout consommateur de `getCurrentTick()` / `getSmoothedBPM()` |
| **Refactor pot/catch system** | PotFilter, PotRouter (rebuild + catch + bargraph), main.cpp handlePotPipeline, Tool 6 | Tout sauf ça | PotRouter::applyBinding full |

---

## 7. Explicit SKIP list

Files that are **almost never useful** to reread when designing :

- `CapacitiveKeyboard.cpp/.h` — DO NOT MODIFY. Calibrated pressure pipeline, black box.
- `midi/GrooveTemplates.h` — just a template array. Consult for values, not for design.
- `setup/SetupCommon.h`, `SetupUI.cpp`, `InputParser.cpp`, `SetupPotInput.cpp` — VT100 render primitives. Needed if you write a new Tool, useless otherwise.
- `ItermCode/vt100_serial_terminal.py` — terminal client, not firmware. Consult only if changing a serial/escape protocol.
- `HardwareConfig.h` — constants, not logic. Consult as needed, not as preload.

---

## 8. Domain Entry Points

Single "start here" function per domain. Secondary files mentioned only if
the task spans multiple files.

| Domain | Start here | Scope |
|---|---|---|
| **Pad sensing** | `CapacitiveKeyboard` (DO NOT MODIFY) | Pressure pipeline, I2C polling — black box. |
| **Inter-core handoff** | `main.cpp::sensingTask` [92-114] + loop() [919-980] | Double buffer + atomic index. |
| **MIDI output** | `MidiEngine::noteOn` [47-57] / `noteOff` [63-72] / `flush` [138-150] | Transport wrapping + aftertouch ring. |
| **Note resolution** | `ScaleResolver::resolve` | 3-layer mapping, `_lastResolvedNote` invariant. |
| **Bank switching** | `BankManager::switchToBank` [181-210] | All side effects of a bank change. See §2 Flow 3. |
| **Scale/hold/octave** | `ScaleManager::processScalePads` [114-201] | Pad→change flag + group propagation. |
| **Control pads** | `ControlPadManager::update` | Edge detection (LEFT press/release, bank switch) + per-mode CC emission (MOMENTARY / LATCH / CONTINUOUS) + gate-vs-setter handoff. CONTINUOUS has a 3-stage DSP pipeline (EMA smooth → ring-buffer sample-and-hold → linear release envelope) driven by global `smoothMs`/`sampleHoldMs`/`releaseMs`. Music block skips via `isControlPad(i)`. |
| **Arpeggiator core** | `ArpEngine::tick` [536-566] → `executeStep` [572-654] | State dispatch + note scheduling. `rebuildSequence` is a callee. |
| **Arp scheduling** | `ArpScheduler::tick` [98-131] + `processEvents` [140-146] | Per-engine tick accumulator + event dispatch. |
| **Clock/PLL** | `ClockManager::update` [45-63] → `processIncomingTicks` [66-116], `generateTicks` [181-203] | Source cascade + PLL smoothing. |
| **Pot → parameter** | `PotRouter::applyBinding` [386-546] | Catch system + value conversion + dirty flags. |
| **LEDs** | `LedController::update` | 9-level priority ladder + event overlay. Event grammar : `triggerEvent(EventId, ledMask)` -> `renderPattern(_eventOverlay, now)`. Tick ARPEG uses shared `renderFlashOverlay()`. See `LedGrammar.h` for palette. Preview wrapper `renderPreviewPattern(inst, now)` is the public entry for Tool 8 via [`ToolLedPreview`](../../src/setup/ToolLedPreview.h) (Phase 0.1). |
| **NVS** | `NvsManager::loadBlob/saveBlob/checkBlob` | Persistence API + descriptor table. |
| **Battery** | `BatteryMonitor::update` | Voltage divider + thresholds + low flag. |
| **Setup mode** | `SetupManager::run` + individual `ToolX::run` | VT100 menu + Tool dispatch. |

---

## 9. Invariants (things that MUST stay true)

1. **No orphan notes**: noteOff ALWAYS uses `_lastResolvedNote[padIndex]`,
   never re-resolves. Scale changes cannot produce stuck notes. Left-button
   release sweeps all pads on the `s_wasHolding → !holding` edge: NORMAL
   banks call `noteOff(i)` for every unpressed pad; ARPEG HOLD OFF banks
   call `removePadPosition(s_padOrder[i])` for every unpressed pad except
   the holdPad.

2. **Arp refcount atomicity**: noteOff is scheduled BEFORE noteOn. If noteOn
   fails (queue full), noteOff is cancelled. MIDI noteOn sent only on
   refcount 0→1, noteOff only on 1→0.

3. **No blocking on Core 1**: NVS writes happen in a background FreeRTOS
   task. Core 1 only sets dirty flags.

4. **Core 0 never writes MIDI**: All MIDI output happens on Core 1. Core 0
   only reads slow params (relaxed atomics).

5. **Catch system**: pot must physically reach the stored value before it
   can change a parameter. Prevents jumps on bank switch or context change.

6. **Bank slots always alive**: all 8 banks exist, only foreground receives
   pad input. ARPEG engines run in background regardless of which bank is
   selected.

7. **Setup/Runtime coherence**: every runtime parameter that persists across
   reboots has a 4-link chain Runtime ↔ Store ↔ Tool ↔ NVS (see §4). If any
   link is modified (field added, range widened, enum extended), ALL links
   must be updated in the same commit. An orphan link (Store field with no
   Tool access, or Tool case for a removed target) is a bug, not tech debt.

---

## 10. Where Things Break (common bug patterns)

### Runtime

| Pattern | Example | Where to look |
|---------|---------|---------------|
| Stale state after bank switch | Catch targets from old bank | `main.cpp` handleManagerUpdates(), `PotRouter::resetPerBankCatch` |
| Orphan notes on mode change | noteOff uses wrong note number | `_lastResolvedNote` usage in `MidiEngine.cpp` |
| MIDI flood from noisy ADC | CC toggling ±1 at boundary | `PotRouter.cpp` CC dirty check, hysteresis |
| Timing burst after clock glitch | ArpScheduler fires many steps | `ArpScheduler.cpp` ticksElapsed guard (capped at 24 ticks = 1 quarter note) |
| BLE/USB tick contamination | PLL mixes intervals from 2 sources | `ClockManager.cpp` per-source atomic counters (`_pendingUsbTicks`, `_pendingBleTicks`) + per-source timestamps — only the active source feeds the PLL |
| Button combo confusion | Rear+left held simultaneously | `PotRouter::resolveBindings` mask checks |
| BLE noteOff drop under AT congestion | 8+ pads with AT released simultaneously — BLE buffer may be full of AT from previous cycle, noteOff dropped → stuck notes. USB unaffected. Safety net: `midiPanic()` (triple-click rear). | `MidiTransport.cpp` BLE send path, `MidiEngine.cpp:138` flush() |
| Rapid bank switch with held pad | 4+ bank switches in <500ms while pad held — each switch sends allNotesOff + re-triggers noteOn, producing rapid click artifacts. Physically unlikely (hold+switch = same hand). | `BankManager.cpp:181` switchToBank(), `main.cpp:947-949` s_lastKeys sync. **To verify on hardware.** |

### Setup ↔ Runtime sync

| Pattern | Example | Where to look |
|---------|---------|---------------|
| New runtime feature not exposed in UI | New `PotTarget` value, works at runtime but not settable | The corresponding Tool (§4 Table 1) wasn't updated |
| NVS badge "ok" but Tool shows defaults | Version bump of Store without clearing NVS | Zero-migration policy: blob rejected silently, compile-time defaults reapply. Normal, accepted. |
| Tool crash/freeze on rare option | Tool case not covered for extended enum | Check Tool switch vs current enum |
| Stale value after Tool save + reboot | Validator too strict → clamps on reload | Align Tool bounds ↔ `validateXxxStore()` ↔ runtime bounds |
| Orphan Store field | Field added to Store but no Tool UI to set it | Violation of invariant §9.7 |

---

## 11. Inter-Core Communication

All lock-free. No mutex anywhere in runtime code.

| What | Type | Writer | Reader | Order |
|------|------|--------|--------|-------|
| Double buffer index | `atomic<uint8_t>` | Core 0 | Core 1 | release/acquire |
| Response shape | `atomic<float>` | Core 1 | Core 0 | relaxed |
| Slew rate | `atomic<uint16_t>` | Core 1 | Core 0 | relaxed |
| Pad sensitivity | `atomic<uint8_t>` | Core 1 | Core 0 | relaxed |
| USB tick count | `atomic<uint8_t>` `_pendingUsbTicks` | USB callback | Core 1 | release/acquire |
| BLE tick count | `atomic<uint8_t>` `_pendingBleTicks` | NimBLE task | Core 1 | release/acquire |
| Last USB tick time | `atomic<uint32_t>` `_lastUsbTickUs` | USB callback | Core 1 | release/acquire (hot), relaxed (timeout) |
| Last BLE tick time | `atomic<uint32_t>` `_lastBleTickUs` | NimBLE task | Core 1 | release/acquire (hot), relaxed (timeout) |

---

## 12. Dirty Flags & Event Queues

### Dirty flags (set → consumed pattern)
| Flag | Set by | Consumed by | Purpose |
|------|--------|-------------|---------|
| `_sequenceDirty` | addPad, removePad, setPattern, setOctaveRange | `rebuildSequence()` at next tick | Arp sequence rebuild |
| `_tickFlash` | `ArpEngine::tick()` | `LedController::update()` via `consumeTickFlash()` | LED beat flash |
| `hasMoved(p)` | `PotFilter::updateAll()` deadband | `applyBinding()` per frame | Pot movement gate |
| `_ccDirty[s]` | `applyBinding()` CC value change | `consumeCC()` in main loop | MIDI CC send |
| `_midiPbDirty` | `applyBinding()` PB value change | `consumePitchBend()` in main loop | MIDI PB send |
| `_dirty` (pot) | Any parameter write | `clearDirty()` after NVS debounce | NVS save trigger |
| `_scaleChangeType` | `processScalePads()` | `consumeScaleChange()` (auto-clear) | Scale change flag |
| `_octaveChanged` | Octave pad press | `hasOctaveChanged()` (auto-clear) | Octave change flag |
| `_holdToggled` | Hold pad press | `hasHoldToggled()` (auto-clear) | Hold toggle flag |

### Event queues
| Queue | Size | Producer | Consumer | Overflow behavior |
|-------|------|----------|----------|-------------------|
| Aftertouch ring | 64 entries | `updateAftertouch()` | `flush()` (16/frame max) | Silent drop. **Capacity math**: at default 25ms rate, N pads generate N events/25ms. flush drains 16/frame × ~40 frames/25ms = 640 drain capacity. Safe up to ~48 pads. At minimum rate (10ms): N events/10ms vs 16×10=160 drain → safe up to ~40 pads. Real-world: ≤10 fingers, never saturates. |
| Arp events | 64 per engine | `tick()` (noteOn/Off pairs) | `processEvents()` every frame | noteOff fail → skip entire step (safe); noteOn fail → cancel orphaned noteOff (safe) |
| NVS writes | per-field dirty flags | Main loop | Background FreeRTOS task | Coalesced (latest wins) |
