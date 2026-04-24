# ILLPAD V2 — Architecture Briefing

**Navigation index** for agents and new sessions. Mental models + task
index + entry points + cross-cutting concerns. Points to specialized refs
for depth — does not duplicate their content.

**This is not a tutorial.** It is a navigation tool. Read §0 first to
choose your scope, then load only what you need.

---

## 0. Scope Triage (answer before reading)

**Q1 — Are you MODIFYING code or EXPLORING / DESIGNING?**

- **Exploring** ("can we do X?", "what would Y require?", open-ended
  design) → Load : §1 Mental Models (all) + relevant domain ref(s) +
  §4 Setup↔Runtime + [`patterns-catalog.md`](patterns-catalog.md) +
  §5 Affinity Matrix. Read broadly. Use §6 SKIP list to avoid wasted
  reads. Don't look for a "task recipe" — synthesis required.

- **Modifying** → go to Q2.

**Q2 — Is the change TIGHT or BROAD?**

- **Tight** (one parameter, one bug, one param-type added) → Q3.
- **Broad** (new mode of play, new role category, redesign of a
  subsystem) → treat as EXPLORING. Example : "add a loop mode" =
  broad. "add a new ARP pattern" = tight.

**Q3 — Is there an exact match in §2 Task Index?**

- **Yes** → follow the listed files + function. Don't over-read.
- **No** → find the closest domain in §7 Domain Entry Points. Read
  the entry function + the relevant ref (led / pot / arp / nvs). Stop
  there unless the edit reveals cross-cutting impact → escalate to
  Q2 "broad".

---

## Keep-in-sync protocol

Maintaining this briefing is part of every commit. If you modify a
function or pattern referenced in §1 (Mental Models), §2 (Task Index),
§3 (Setup↔Runtime), §4 (Affinity Matrix), or §7 (Entry Points), update
the relevant entry in the same commit.

Same rule for the specialized refs referenced from here. A stale ref is
a bug, not tech debt. Exceptions : typo fixes, comment edits, log-only
changes, refactors internal to a function (signature unchanged). Only
semantic or structural changes require doc updates.

---

## 1. Mental Models

How each core system *thinks*. Load these for any broad or exploratory
work.

### MM1 — Sensing / inter-core

Core 0 owns MPR121 polling + pressure pipeline → publishes into one of
two buffers via `s_active.store(release)`. Core 1 reads via
`s_active.load(acquire)`. Lock-free, never torn. Slow params go back
Core 1 → Core 0 via `std::atomic<…>` relaxed.

**Don't** : introduce any mutex on this path. Full inter-core table :
[`runtime-flows.md`](runtime-flows.md) "Inter-core communication".

### MM2 — Bank slots always alive

8 `BankSlot` = 8 MIDI channels, always allocated, always running.
`isForeground` flag gates pad input only. Background ARPEG engines
keep ticking ; BG NORMAL banks preserve pitchBend/scale/velocity.
Bank switch = move foreground marker + kill old channel's active notes.

**Don't** : treat BG as dormant.

### MM3 — Pad → note (3 stacked layers)

`physical pad → padOrder[48] → rank → ScaleResolver(root, mode) →
MIDI note`. padOrder set in Tool 2 (runtime-static). Scale per-bank,
runtime-switchable. Control roles (bank/scale/arp pads) filtered
*before* the music block.

**Invariant** : `_lastResolvedNote[pad]` stored at noteOn, reused at
noteOff — never re-resolves.

### MM4 — Arp : pile + sequence + scheduler

Pile = padOrder positions held/captured. Sequence = pattern expansion
(with octave repeats, up to 192 entries), rebuilt lazy on
`_sequenceDirty`. Scheduler = per-engine tick accumulator → fires step
→ schedules noteOff+noteOn (atomic pair, P1 + P3) → `processEvents()`
drains by wall clock. States : `IDLE` / `WAITING_QUANTIZE` / `PLAYING`.

**Don't** : couple a new rhythm engine to a single bank —
`MAX_ARP_BANKS = 4` engines share the scheduler. Details :
[`arp-reference.md`](arp-reference.md).

### MM5 — Clock cascade

External 0xF8 ticks per source (USB/BLE) → atomic counters → PLL IIR
(α_USB = 0.3, α_BLE = 0.15). Priority : USB > BLE > last known
(5 s memory) > internal (tempo pot). Master mode generates ticks from
internal BPM.

**Don't** : expect Start/Stop/Continue (ignored by design — ILLPAD =
instrument, not a transport follower or leader).

### MM6 — Pot catch

Pot must physically reach `storedValue` before it can write. Global
targets (tempo, shape, …) keep catch across bank switches + propagate
`storedValue` across contexts. Per-bank targets reset catch on switch.
Brightness bypasses catch.

**Decide on new target** : global or per-bank ? Flag via
`isPerBankTarget()`. Details : [`pot-reference.md`](pot-reference.md).

### MM7 — LED priority + overlays

9-level priority : Boot > Setup > Chase > Error > Battery > Bargraph >
[Event overlay tracking] > Calibration > Normal display + event overlay.
Event overlays are *overlays* (blit on top via `renderPattern()`, never
clear). Normal display encodes multi-bank state (FG solid / BG dim via
`_bgFactor` / tick flashes via `renderFlashOverlay()` / sine pulse for
stopped-loaded ARPEG). The 3-layer LED grammar (patterns × color slots
× event mapping) is the single source of truth for all visual events
since Phase 0 refactor 2026-04-19.

**Don't** : design a new visual by writing pixels — declare a pattern +
color slot + event entry first (or add a new `PatternId` to the palette
if truly novel). Details : [`led-reference.md`](led-reference.md).

### MM8 — Setup is a config mirror

Setup Tools are the UI surface of runtime config. Every persisted
user-param follows the 4-link chain Runtime↔Store↔Tool↔NVS (see §3).
Boot-only (main loop paused) → safe to cache resolved values. VT100
aesthetic is a product feature, not a convenience — never simplify.

Conventions : [`setup-tools-conventions.md`](setup-tools-conventions.md).
Aesthetic : [`vt100-design-guide.md`](vt100-design-guide.md).

---

## 2. Task Index (tight only)

Only use this for truly standardized additions. If your task is not
listed, it is probably **broad** — go back to §0 Q2 and treat as
exploration.

| Task | Files to read | Function to touch | Don't need to touch |
|---|---|---|---|
| Add a new shuffle template | `midi/GrooveTemplates.h` | `SHUFFLE_TEMPLATES[]` array, bump `NUM_SHUFFLE_TEMPLATES` | ArpEngine, PotRouter (auto pick-up) |
| Add a new scale mode | `ScaleResolver.cpp` (`scaleIntervals[][7]`) | `ScaleResolver::resolve()` table | MidiEngine, ArpEngine (re-resolve auto) |
| Add a new ARP pattern | `ArpEngine.h` (enum `ArpPattern`), `ArpEngine.cpp` (`rebuildSequence()`), `main.cpp` (`s_patNames[]`) | `ArpEngine::rebuildSequence()` switch | ScaleResolver, ArpScheduler, NVS, LED |
| Add a new pot target | `PotRouter.h` (enum `PotTarget`), `PotRouter.cpp` (`applyBinding`, `isPerBankTarget`, `getDiscreteSteps`), `ToolPotMapping.cpp` (pool + label + color) | `PotRouter::applyBinding` switch + getter | MidiEngine, ArpEngine (unless they consume it) |
| Add a new LED event | `LedGrammar.h` (enum `EventId`), `LedGrammar.cpp` (`EVENT_RENDER_DEFAULT[]` row), `LedSettingsStore` field if pattern needs a new global, `ToolLedSettings` if user-tunable | `LedController::triggerEvent` callsite in metier flow | Metier flow (bank/scale/arp) except to trigger |

Full step-by-step recipes in the corresponding domain refs
([`arp-reference.md`](arp-reference.md) §11,
[`pot-reference.md`](pot-reference.md) §9,
[`led-reference.md`](led-reference.md) §9).

---

## 3. Setup ↔ Runtime contracts

**The central source of friction** when runtime work goes out of sync
with setup Tools. Walk through these tables before ANY user-configurable
change.

### Table 1 — Setup Tool-managed configs (VT100 → NVS)

| Runtime concept | Store struct | Setup Tool | NVS ns/key | Sync points when you modify runtime |
|---|---|---|---|---|
| Pressure calibration | `CalDataStore` | Tool 1 | `illpad_cal` / `cal` | New field → validator + calibration loop + pressure pipeline. Rarely touched. |
| Pad ordering (physical → rank 0-47) | `NoteMapStore` | Tool 2 | `illpad_nmap` / `pads` | `padOrder[48]` consumed everywhere — static after setup. |
| Bank pads (8 control pads) | `BankPadStore` | Tool 3 | `illpad_bpad` / `pads` | New role → Tool 3 collision check + category color ; runtime consumer (BankManager) |
| Scale pads (7 root + 7 mode + 1 chrom) | `ScalePadStore` | Tool 3 | `illpad_spad` / `pads` | Extend category → Tool 3 grid + ScaleManager |
| Control pad assignments + 3 global DSP params | `ControlPadStore` v2 | Tool 4 | `illpad_ctrl` / `pads` | Add entry : new slot + Tool 4 edit ; globals via 'g' key. Consumed at boot via `ControlPadManager::applyStore` |
| Arp pads (1 hold + 4 octave) | `ArpPadStore` | Tool 3 | `illpad_apad` / `pads` | New ARPEG control pad → Tool 3 + `main.cpp::handleHoldPad` + ScaleManager |
| Bank types + quantize + scaleGroup | `BankTypeStore` | Tool 5 | `illpad_btype` / `config` | New `BankType` → Tool 5 cycle + ArpEngine assignment + LED state machine + validator |
| Global settings | `SettingsStore` | Tool 6 | `illpad_set` / `settings` | New field → Tool 6 case + validator + bump version + apply in `main.cpp` setup |
| Pot bindings (user-configurable) | `PotMappingStore` | Tool 7 | `illpad_pmap` / `mapping` | New `PotTarget` → pool line, label+color, `getDiscreteSteps()`, `isPerBankTarget()`, `applyBinding` case |
| Pot filter tuning | `PotFilterStore` | none (descriptor slot reserved "Monitor in T7", unimplemented) | `illpad_pflt` / `cfg` | **Friction zone** : only editable via code + flash. |
| LED pattern globals + event overrides + gamma | `LedSettingsStore` v8 | Tool 8 | `illpad_lset` / `ledsettings` | New event → `EventId` + `EVENT_RENDER_DEFAULT[]` + optional Tool 8 line ; new global → Store field + line + apply in renderPattern |
| Color slots (14 preset + hue, 16 slot IDs) | `ColorSlotStore` v5 | Tool 8 | `illpad_lset` / `ledcolors` | Add slot → `COLOR_SLOT_COUNT` + Tool 8 LineId + `resolveColorSlot()` + default in NvsManager.cpp |

### Table 2 — Runtime-managed persisted state (no Setup Tool)

No VT100 UI. Written by `NvsManager::queue*Write()` from runtime. Not
in `NVS_DESCRIPTORS[]`.

| Runtime concept | Store / type | Namespace / key | Written by | Loaded by |
|---|---|---|---|---|
| Current bank (0-7) | scalar `uint8_t` | `illpad_bank` / `current` | `queueBankWrite()` on bank switch | `loadAll()` boot |
| ScaleConfig per bank | `ScaleConfig` | `illpad_scale` / `cfg_0..cfg_7` | `queueScaleWrite()` on scale change | `loadAll()` (leader-wins propagation for scale groups) |
| Arp pot params per bank | `ArpPotStore` | `illpad_apot` / `bank_0..7` | Pot debounce | `loadAll()` → `getLoadedArpParams()` |
| Base velocity + variation per bank | struct | `illpad_bvel` / `bank_0..7` | Pot debounce | `loadAll()` |
| Pitch bend offset per bank (NORMAL) | `uint16_t` | `illpad_pbnd` / `bank_0..7` | Pot debounce | `loadAll()` |
| Tempo BPM | `uint16_t` | `illpad_tempo` / `bpm` | Pot debounce | `loadAll()` |
| LED brightness | `uint8_t` | `illpad_led` / `bright` | Pot debounce | `loadAll()` |
| Pad sensitivity | `uint8_t` | `illpad_sens` / `level` | Pot debounce | `loadAll()` |
| Response shape / slew / AT deadzone | `PotParamsStore` | `illpad_pot` / `params` | Pot debounce | `loadAll()` |

**Implicit rule** : new pot-accessible parameters persist via this
channel automatically — no new namespace. But a getter in PotRouter, a
push in `handlePotPipeline()` or `pushParamsToEngine()`, and the
appropriate dirty flag are required.

Full NVS catalog : [`nvs-reference.md`](nvs-reference.md).

---

## 4. Cross-cutting Affinity Matrix

For broad / creative tasks. What domains will you likely touch, what
to skip, what existing system to read as reference.

| Creative task family | Domains touched | Skip | Reference system |
|---|---|---|---|
| **New mode of play** (loop, beat, sequencer, drum pad) | Clock, BankType, state machine, scheduling, pile, LED, PotMapping, Tool 4, NVS | Pressure pipeline, ScaleResolver (unless notes), Battery, BLE detail | ArpEngine + ArpScheduler (complete pattern) + BankManager |
| **New pad role category** (loop controls, macro pads) | Tool 3 collision check, new runtime manager, LED state, NVS new Store, main.cpp dispatch | ArpEngine internals, MIDI transport, Clock | ScaleManager (role-based template) + BankManager (pad-detection template) + Tool 3 |
| **Rethink visual feedback** (new pattern, animation, priority) | LedController (pattern engine + renderFlashOverlay + renderPreviewPattern), LedGrammar (PatternId/EventId/defaults), LedSettingsStore v8, ColorSlotStore v5 (16 slots), Tool 8 + ToolLedPreview, all `triggerEvent()` callsites | Runtime metier (consumes API but not animation) | `LedController::update()` + renderPattern + Tool 8 LineId enum + ToolLedPreview |
| **New MIDI output class** (MPE, MCU, program change) | MidiTransport, MidiEngine, PotRouter (if trigger), setup Tool 5 (if user-config) | Arp internals, LED, Battery | `MidiTransport::sendCC` / `sendPitchBend` / `sendPolyAftertouch` |
| **Redesign clock/tempo** (tap tempo, per-bank tempo, global swing) | ClockManager, ArpScheduler tick loop, PotRouter tempo target, setup Tool 5 | Pressure, Battery, LED | ClockManager + every consumer of `getCurrentTick()` / `getSmoothedBPM()` |
| **Refactor pot/catch system** | PotFilter, PotRouter (rebuild + catch + bargraph), main.cpp handlePotPipeline, Tool 7 | Everything else | `PotRouter::applyBinding` full |

---

## 5. Reusable patterns

14 patterns P1–P14 (runtime P1–P9, setup P10–P14) catalogued in
[`patterns-catalog.md`](patterns-catalog.md). Grab them instead of
inventing a new mechanism.

Quick index :
- **P1** Refcount noteOn/Off · **P2** Dirty flag → consume ·
  **P3** Scheduled event queue · **P4** Pot catch
- **P5** Event overlay (LED grammar) · **P6** Store + validate +
  version (NVS) · **P7** Button modifier + chord
- **P8** Rising edge detection · **P9** Debounced NVS write
- **P10** VT100 aesthetic kit · **P11** InputParser → NavEvent ·
  **P12** Pot navigation
- **P13** Save feedback (flashSaved) · **P14** LED preview during edit

---

## 6. Explicit SKIP list

Files that are **almost never useful** to reread when designing :

- `CapacitiveKeyboard.cpp/.h` — DO NOT MODIFY. Calibrated pressure
  pipeline, black box.
- `midi/GrooveTemplates.h` — template array. Consult for values, not
  design.
- `setup/SetupCommon.h`, `SetupUI.cpp`, `InputParser.cpp`,
  `SetupPotInput.cpp` — VT100 render primitives. Needed only when
  writing a new Tool.
- `ItermCode/vt100_serial_terminal.py` — terminal client, not
  firmware. Only touch when changing a serial/escape protocol.
- `HardwareConfig.h` — constants, not logic. Consult as needed, not
  as preload.

---

## 7. Domain Entry Points

Single "start here" function per domain. Secondary files mentioned
only if the task spans multiple files.

| Domain | Start here | Scope |
|---|---|---|
| **Pad sensing** | `CapacitiveKeyboard` (DO NOT MODIFY) | Pressure pipeline, I2C polling — black box |
| **Inter-core handoff** | `main.cpp::sensingTask` [92-114] + `loop()` [919-980] | Double buffer + atomic index |
| **MIDI output** | `MidiEngine::noteOn` [47-57] / `noteOff` [63-72] / `flush` [138-150] | Transport wrapping + aftertouch ring |
| **Note resolution** | `ScaleResolver::resolve` | 3-layer mapping + `_lastResolvedNote` invariant |
| **Bank switching** | `BankManager::switchToBank` [181-210] | All side effects of a bank change. See [`runtime-flows.md`](runtime-flows.md) §3 |
| **Scale/hold/octave** | `ScaleManager::processScalePads` [114-201] | Pad → change flag + group propagation |
| **Control pads** | `ControlPadManager::update` | Edge detection + per-mode CC emission + gate-vs-setter handoff + CONTINUOUS DSP pipeline |
| **Arpeggiator core** | `ArpEngine::tick` [536-566] → `executeStep` [572-654] | State dispatch + note scheduling. Details : [`arp-reference.md`](arp-reference.md) |
| **Arp scheduling** | `ArpScheduler::tick` [98-131] + `processEvents` [140-146] | Per-engine tick accumulator + event dispatch |
| **Clock/PLL** | `ClockManager::update` [45-63] → `processIncomingTicks` [66-116], `generateTicks` [181-203] | Source cascade + PLL smoothing |
| **Pot → parameter** | `PotRouter::applyBinding` [386-546] | Catch + value conversion + dirty flags. Details : [`pot-reference.md`](pot-reference.md) |
| **LEDs** | `LedController::update` | 9-level priority ladder + event overlay. Details : [`led-reference.md`](led-reference.md) |
| **NVS** | `NvsManager::loadBlob/saveBlob/checkBlob` | Persistence API. Details : [`nvs-reference.md`](nvs-reference.md) |
| **Battery** | `BatteryMonitor::update` | Voltage divider + thresholds + low flag |
| **Setup mode** | `SetupManager::run` + `ToolX::run` | VT100 menu + Tool dispatch. Conventions : [`setup-tools-conventions.md`](setup-tools-conventions.md) |
| **Boot** | `main.cpp::setup()` | 8 steps + LED feedback + setup mode entry. Details : [`boot-sequence.md`](boot-sequence.md) |

---

## 8. Where to find what

| Looking for | Ref |
|---|---|
| Invariants (7 absolutes) | `CLAUDE.md` |
| 5 runtime data flows (detailed) | [`runtime-flows.md`](runtime-flows.md) |
| Reusable patterns P1–P14 | [`patterns-catalog.md`](patterns-catalog.md) |
| NVS API + stores + namespaces | [`nvs-reference.md`](nvs-reference.md) |
| LED display + grammar + bugs | [`led-reference.md`](led-reference.md) |
| Arp pile + Play/Stop + shuffle + bugs | [`arp-reference.md`](arp-reference.md) |
| Pot pipeline (MCP3208) + catch + bugs | [`pot-reference.md`](pot-reference.md) |
| Hardware wiring + pins + MCP3208 | [`hardware-connections.md`](hardware-connections.md) |
| Boot sequence + failure modes | [`boot-sequence.md`](boot-sequence.md) |
| Setup tool behavioral rules | [`setup-tools-conventions.md`](setup-tools-conventions.md) |
| VT100 aesthetic + primitives | [`vt100-design-guide.md`](vt100-design-guide.md) |
