# LED Reference — ILLPAD V2

End-to-end view of the LED subsystem : hardware, rendering engine, event
grammar, configuration, state machine, bug patterns. Read this when
touching any LED code or adding a new visual event.

**Source of truth** :
- `src/core/LedController.h` + `.cpp` (rendering engine + state machine)
- `src/core/LedGrammar.h` + `.cpp` (pattern palette, event mapping)
- `src/core/HardwareConfig.h` (LED pins, NUM_LEDS, sine LUT, gamma default)
- `src/core/KeyboardData.h` (`LedSettingsStore`, `ColorSlotStore`)
- `src/setup/ToolLedSettings.*` (Tool 8 UX)
- `src/setup/ToolLedPreview.*` (Tool 8 preview helper)

Related : [`patterns-catalog.md`](patterns-catalog.md) P5 (event overlay),
[`runtime-flows.md`](runtime-flows.md) for the per-frame position of
`LedController::update()`.

---

## 1. Hardware

Single Adafruit **SK6812 RGBW NeoPixel Stick** (8 LEDs, GRBW wire order)
driven by `Adafruit_NeoPixel` on GPIO 13. Powered from the 3.3 V rail
(or 5 V if needed). No per-LED GPIO — one data pin, serial protocol.

The 8 LEDs represent **banks 1–8** in normal display. All other modes
(boot, bargraph, battery, confirmations) overlay this mapping.

Brightness is controlled by the rear pot (`TARGET_LED_BRIGHTNESS`, bypasses
catch).

---

## 2. Rendering engine (LedController)

### `setPixel(led, rgbw, intensityPct)`

Single source of truth for LED output. Combines :

1. `intensityPct` (0–100, perceptual) × master brightness × 255 in one
   32-bit multiply (no truncation at low values).
2. Gamma correction via runtime LUT (`_gammaLut[256]`, rebuilt at boot
   from configurable `gammaTenths` in `LedSettingsStore`, default
   gamma 2.0, range 1.0–3.0).

All call sites use this — no direct NeoPixel writes outside the controller
(except Tool 8 preview which has its own routed entry, see `ToolLedPreview`).

### Master brightness (rear pot)

Rear pot drives `POT_BRIGHTNESS_CURVE[]` (compile-time selectable :
LOW_BIASED / LINEAR / SIGMOID). Acts as a perceptual master fader
(0–100 %). Applied uniformly to all LED outputs that go through
`setPixel`.

### Gamma LUT

Rebuilt on boot from `LedSettingsStore.gammaTenths`. Applied after
brightness math, before the NeoPixel output byte. Default 2.0
(γ = tenths / 10), range clamped [1.0, 3.0].

---

## 3. Pattern grammar (Phase 0 refactor 2026-04-19)

3-layer architecture declared in `LedGrammar.h` :

### Layer 1 — Patterns

9 fixed pattern behaviors (palette). Each has typed parameters via a
discriminated union `PatternParams` (≤ 16 bytes).

| ID | Name | Behavior | Typical params |
|---|---|---|---|
| 0 | `PTN_SOLID` | Steady brightness | `pct` |
| 1 | `PTN_PULSE_SLOW` | Sine breathing, slow (≥ 2000 ms) | `minPct`, `maxPct`, `periodMs` |
| 2 | `PTN_CROSSFADE_COLOR` | Continuous fade between two color slots | `periodMs` |
| 3 | `PTN_BLINK_SLOW` | On/off, ~800 ms total | `onMs`, `offMs`, `cycles`, `blackoutOff` |
| 4 | `PTN_BLINK_FAST` | On/off, ~300–450 ms total | same as BLINK_SLOW |
| 5 | `PTN_FADE` | Linear ramp start → end | `durationMs`, `startPct`, `endPct` |
| 6 | `PTN_FLASH` | Short tick (30–80 ms), fg preserves bg intensity | `durationMs`, `fgPct`, `bgPct` |
| 7 | `PTN_RAMP_HOLD` | Long-press progression + SPARK suffix | `rampMs`, `suffixOnMs/GapMs/Cycles` |
| 8 | `PTN_SPARK` | Double-flash (universal confirm) | `onMs`, `gapMs`, `cycles` |

Sentinel `PTN_NONE = 0xFF` means "no override, use compile-time default".

### Layer 2 — Color slots

16 `ColorSlotId` entries in `ColorSlotStore` v5 (NVS `illpad_lset` / key
`ledcolors`, magic `0xC010`). Each slot = `(preset, hueOffset)` resolved
at load time via `resolveColorSlot()` → RGBW tuple.

Slots group semantically : mode colors (NORMAL/ARPEG/LOOP), verb colors
(PLAY/STOP/REC/OVERDUB/CLEAR…), setup/nav, confirm OK. `CSLOT_VERB_STOP`
was added in Phase 0.1 (slot 15) to decouple Stop fade-out from Play's
color.

System colors (error, boot progress, battery gauge) are hardcoded RGBW
in `HardwareConfig.h` — they bypass the slot table.

### Layer 3 — Events

Each `EventId` maps to an `EventRenderEntry{patternId, colorSlot, fgPct}`.

17 events defined (`EVT_COUNT = 17`) ; 10 wired in Phase 0, 7 LOOP events
reserved for Phase 1+ (currently `PTN_NONE` → skipped by renderer).

| EventId | Pattern | Color slot | fgPct |
|---|---|---|---|
| `EVT_BANK_SWITCH` | BLINK_SLOW | CSLOT_BANK_SWITCH | 100 |
| `EVT_SCALE_ROOT` | BLINK_FAST | CSLOT_SCALE_ROOT | 100 |
| `EVT_SCALE_MODE` | BLINK_FAST | CSLOT_SCALE_MODE | 100 |
| `EVT_SCALE_CHROM` | BLINK_FAST | CSLOT_SCALE_CHROM | 100 |
| `EVT_OCTAVE` | BLINK_FAST | CSLOT_OCTAVE | 100 |
| `EVT_PLAY` | FADE | CSLOT_VERB_PLAY | 100 (0 → 100) |
| `EVT_STOP` | FADE | CSLOT_VERB_STOP | 100 (100 → 0) |
| `EVT_WAITING` | CROSSFADE_COLOR | CSLOT_MODE_ARPEG | 100 |
| `EVT_REFUSE` | BLINK_FAST | CSLOT_VERB_REC | 100 |
| `EVT_CONFIRM_OK` | SPARK | CSLOT_CONFIRM_OK | 100 |
| `EVT_LOOP_*` | PTN_NONE (placeholder) | various | 0 |

NVS override : `LedSettingsStore.eventOverrides[EVT_COUNT]` lets the user
swap any event's rendering via Tool 8.

### API

- `triggerEvent(EventId, ledMask)` — preempts the single-slot
  `_eventOverlay` `PatternInstance`. Auto-expires via pattern math.
- `renderPattern(inst, now)` — internal dispatch on `patternId`.
- `renderPreviewPattern(inst, now)` — public wrapper for Tool 8 preview
  (via `ToolLedPreview` helper). Zero runtime duplication.
- `renderFlashOverlay()` — shared between event `PTN_FLASH` and ARPEG
  per-step tick flash.

**Legacy `ConfirmType` / `triggerConfirm` were removed in step 0.9** (Phase
0 refactor). All visual events now go through `triggerEvent(EventId,
ledMask)`.

---

## 4. Normal display (multi-bank state)

Each of the 8 LEDs encodes its bank's state simultaneously :

| State | Color | Pattern | Intensity | Rate |
|---|---|---|---|---|
| Current NORMAL | White (W channel) | Solid | `normalFgIntensity` (default 85 %) | — |
| Current ARPEG idle (pile empty) | Blue | Solid dim | `fgArpStopMin` | — |
| Current ARPEG stopped (notes loaded) | Blue | Sine pulse | `fgArpStopMin ↔ fgArpStopMax` | ~1.5 s period |
| Current ARPEG playing | Blue | Solid + white tick flash | `fgArpPlayMax`, spike `tickFlashFg` on step | 30 ms flash |
| BG NORMAL | White dim | Solid | `normalFgIntensity × bgFactor%` (derived) | — |
| BG ARPEG (all states) | Blue dim | Solid (+ tick flash if playing) | `fgArpStopMin × bgFactor%` or `fgArpPlayMax × bgFactor%` | 30 ms flash if playing |

**Background is derived from foreground via `bgFactor`** (v8 post-audit
cleanup, 2026-04-23). The 3 zombie fields `normalBgIntensity`,
`bgArpStopMin`, `bgArpPlayMin` were retired — BG = FG × bgFactor%.

### Sine pulse (FG stopped-loaded only)

- `LED_SINE_LUT[256]` in `HardwareConfig.h`, shared with Tool 8 preview.
- 16-bit phase + linear interpolation.
- **Only** used for FG ARPEG stopped with notes loaded ("breathing = ready
  to play"). All other states are solid.

### Tick flash (ARPEG playing only)

`ArpEngine::consumeTickFlash()` returns true once per arp step (P2 dirty
flag). `LedController` stores `_flashStartTime[i]` and holds the flash
for `tickBeatDurationMs` (v7, default 30 ms, range [5, 500]). Only fires
during playback.

Two other tick durations are stored but consumed only in Phase 1+ by
LoopEngine :
- `tickBarDurationMs` — on bar wrap.
- `tickWrapDurationMs` — on loop wrap.

---

## 5. Priority state machine (`LedController::update`)

Ladder-resolved each frame. Higher priority preempts lower :

```
1. Boot mode          (progressive white fill / red failure blink)
2. Setup comet        (violet comet during Tools 1-8)
3. Chase pattern      (calibration entry — white chase)
4. Error              (LEDs 3-4 blink red 500 ms — sensing task stall)
5. Battery gauge      (8-LED red → green gradient bar, 3 s)
6. Pot bargraph       (solid bar + catch visualization, unified for all pots)
7. Event overlay tracking (17 EventId, per-pattern auto-expire timers)
8. Calibration mode   (all off + validation flash)
9. Normal bank display + event overlay (multi-bank solid/pulse/tick + overlay)
```

Event overlays **do not blank** the bar — `renderConfirmation()` tracks
state/expiry, `renderNormalDisplay()` draws the overlay on top of the
normal bank display. No confirmation ever calls `clearPixels()`. The
normal display (including tick flashes) runs underneath at all times.

---

## 6. Pot bargraph

`showPotBargraph(realLevel, potLevel, caught)` — 3 params. **Unified**
bargraph for all pots (tempo included, no pulsed variant).

- `realLevel` = target value (what the parameter is now).
- `potLevel` = physical pot position indicator.
- `caught` = boolean, gates the visualization.

Catch state visualized :
- **Uncaught** : pot position shown dimly, target level shown as solid bar.
  User knows "turn the pot until the dim marker reaches the bar".
- **Caught** : solid bar only.

Duration configurable in Tool 6 Settings (`potBarDurationMs`), range
1–10 s in 500 ms steps, default 3 s. Stored in `SettingsStore` (NVS
`illpad_set`).

---

## 7. Configuration (Tool 8)

Tool 8 ships a single-view layout with 6 musician-facing sections
(Phase 0.1 respec 2026-04-20, commit cc379f5). The 3-page legacy layout
(PATTERNS / COLORS / EVENTS) is retired.

### Sections

1. **NORMAL** — base color + foreground/background intensity.
2. **ARPEG** — base color + FG/BG intensities + breathing (stopped-loaded pulse).
3. **LOOP** — base color + intensities (reserved for Phase 1+).
4. **TRANSPORT** — play/stop/waiting/breathing + tick common FG/BG +
   tick verb colors (PLAY/REC/OVERDUB) + tick durations (BEAT / BAR / WRAP).
5. **CONFIRMATIONS** — bank / scale / octave durations + blink counts +
   SPARK params.
6. **GLOBAL** — bgFactor + gamma.

### Configuration mechanics

- `LedSettingsStore` v8 (NVS `illpad_lset` / `ledsettings`, magic
  `0xBEEF`) holds intensities, timings, per-event overrides, gamma.
  v8 removed `normalBgIntensity` / `bgArpStopMin` / `bgArpPlayMin`
  (derived from FG via bgFactor).
- `ColorSlotStore` v5 (NVS same namespace, key `ledcolors`, magic
  `0xC010`) holds the 16 color slots (preset + hueOffset).
- Save applies immediately via `LedController::loadLedSettings()`.

### Navigation paradigm

§4.4 of [`setup-tools-conventions.md`](setup-tools-conventions.md) —
geometric visual navigation :
- `←→` = focus horizontally between multi-value fields on a row.
- `↑↓` = adjust the focused field (±1 fine, ×10 with acceleration).
- `d` resets current line to default (no y/n confirm — line-scoped).

Live preview via [`ToolLedPreview`](../../src/setup/ToolLedPreview.h)
(rate-capped at 50 Hz). Routes through `renderPreviewPattern` for zero
runtime duplication.

---

## 8. Boot and battery overlays

Handled by hardcoded RGBW in `HardwareConfig.h`, bypass slot table and
bgFactor.

- **Boot progress** : white fill LEDs 1..8 step-by-step, red blink on
  failure. See [`boot-sequence.md`](boot-sequence.md).
- **Battery gauge** : 8-LED red → green gradient, 3 s on rear-button
  press.
- **Low battery warning** : foreground bank LED does 3 rapid blinks
  every 3 s when SOC < 20 %.

---

## 9. Adding a new LED event

Minimum steps :

1. `LedGrammar.h` — add entry to `enum EventId` (increment `EVT_COUNT`).
2. `LedGrammar.cpp` — add row in `EVENT_RENDER_DEFAULT[]` pointing to a
   pattern + color slot.
3. If a new pattern is required, add a `PatternId` entry and extend
   `PatternParams` union (watch the 16-byte static_assert).
4. If tunable by user : add a `LineId` in Tool 8 (`ToolLedSettings`) and
   wire metadata in `shapeForLine` / `colorSlotForLine` /
   `readNumericField` / `writeNumericField` / `descriptionForLine`.
5. Trigger from the metier flow via `triggerEvent(EventId, ledMask)`.
6. Update `LedSettingsStore.eventOverrides[]` size if new event count
   changed the struct layout (version bump + validator).

See [`patterns-catalog.md`](patterns-catalog.md) P5.

---

## 10. Bug patterns (LED-specific)

| Pattern | Example | Where to look |
|---|---|---|
| LED flashes wrong color | Event wired to wrong color slot | `EVENT_RENDER_DEFAULT[]` or user override in Tool 8 EVENTS |
| LED blinks but wrong duration | `bankDurationMs`, `scaleRootDurationMs` etc. out of range | `validateLedSettingsStore()` clamps [5, 500] ms |
| BG dim not proportional to FG | Zombie field still referenced somewhere | Since v8, BG = FG × bgFactor. Check that old fields aren't referenced — all consumers must use `bgFactor`. |
| Sine breathing too fast/slow | Pulse period fixed at ~1.5 s historically | Editable in Tool 8 TRANSPORT → breathing period |
| Tick flash missing | `ArpEngine::consumeTickFlash()` not called that frame | Arp not playing, or `tickBeatDurationMs = 0` (clamped ≥ 5) |
| NVS reset each boot | `LedSettingsStore` version mismatch | Expected on version bump (zero-migration policy). User re-sets in Tool 8. |
| Tool 8 live preview flickers | Preview rate too high | `ToolLedPreview` rate-capped at 50 Hz via `_lastUpdateMs` |
| LEDs stay dark at low brightness | Truncation in old intensity math | Since Phase 0.5, `setPixel()` uses 32-bit multiply — low values survive. |
| Confirmation blanks the bar | Old `clearPixels()` code path | All events are overlays now. No `clearPixels()` in confirmation render. |
