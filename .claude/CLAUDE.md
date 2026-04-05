# ILLPAD48 V2 — 48-Pad Capacitive MIDI Controller

**Prototype — fresh start, no backward compatibility. Change freely.**

## Build

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1          # build
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload # upload
~/.platformio/penv/bin/pio device monitor -b 115200             # monitor
```

`pio` is NOT in PATH — always use full path.

## Hardware

- **MCU**: ESP32-S3-N8R16 (8MB QIO flash, 16MB OPI PSRAM), dual-core 240MHz
- **Sensors**: 4× MPR121 capacitive touch (I2C 0x5A–0x5D) → 48 aluminium pads, circular/radial layout
- **MIDI**: USB MIDI (TinyUSB composite) + BLE MIDI (ESP32-BLE-MIDI), simultaneous
- **USB**: Single USB-C on GPIO 19/20 (native USB, no UART bridge). ALL traffic on this one port: MIDI, CDC serial, upload, VT100 setup. Board has a 2nd USB-C (COM/UART bridge GPIO 43/44) but it is unused. JTAG builtin shares GPIO 19/20 — conflicts with TinyUSB.
- **2 Buttons**: left GPIO 12 (bank+scale+arp single-layer control), rear GPIO 21 (battery/setup/modifier pot rear). Active LOW, internal pull-up.
- **5 Pots**: 4 right GPIO 4/5/6/7 (tempo, shape/gate, slew/pattern, velocity), 1 rear GPIO 1 (LED brightness/sensitivity). All on ADC1 for BLE compatibility.
- **8 LEDs**: SK6812 RGBW NeoPixel Stick, single data pin GPIO 13, Adafruit_NeoPixel driver (NEO_GRBW)
- **Battery**: LiPo 3.7V, BQ25185 charger, ADC voltage divider

Pads measure **skin contact surface area**, not mechanical force. Aftertouch = yes. Velocity from pressure = unreliable.

## Architecture

### Dual-Core FreeRTOS

- **Core 0** — `sensingTask`: I2C poll 4× MPR121, pressure pipeline, publish to double buffer.
- **Core 1** — `loop()`: read double buffer, edge detection, MIDI, arp ticks, pots, buttons, LEDs, NVS.

### Double Buffer (NOT mutex)

Inter-core sync uses `std::atomic<uint8_t>` `s_active` — not mutex, not volatile.
- Core 0 computes `writeIdx = 1 - s_active.load()` locally, writes to `s_buffers[writeIdx]`, then publishes via `s_active.store(writeIdx, memory_order_release)`.
- Core 1 reads via `s_active.load(memory_order_acquire)` to get the latest buffer. Never blocks.
- Slow params (pots → Core 0): `std::atomic<float>` / `std::atomic<uint16_t>` with `memory_order_relaxed`.

### 8 Banks Always Alive

Every bank (= fixed MIDI channel 1-8) is a `BankSlot` that stays alive. Bank select only changes which slot receives pad input ("foreground"). ARPEG engines run in background.

```
BankSlot { channel, type(NORMAL|ARPEG), scaleConfig, arpEngine*, isForeground,
           baseVelocity, velocityVariation, pitchBendOffset }
```

- **NORMAL**: pads → notes + poly-aftertouch. Only plays when foreground. Velocity = baseVelocity ± random(velocityVariation), per-bank. Pitch bend offset per-bank.
- **ARPEG**: arpeggiator. No aftertouch. Velocity = baseVelocity ± variation, per-bank. Max 4 ARPEG banks.

### 3 Mapping Layers

1. **Pad Ordering** `padOrder[48]`: physical pad → rank 0-47 (low to high). Set once in Tool 2. All banks share it.
2. **Scale Config** per bank: chromatic or scale (root + mode). Root = base note in both modes. Runtime-switchable via left button hold.
3. **Control Pad Assignment**: 29 pads with control roles (bank 8 + scale 15 + arp 6 incl. 4 octave). One role per pad. Set in Tool 3.

### Note Resolution

`ScaleResolver::resolve(padIndex, padOrder, scaleConfig)` → MIDI note or 0xFF.
- Chromatic: `rootBase + padOrder[padIndex]`
- Scale: `rootBase + (order/7)*12 + scaleIntervals[mode][order%7]`

**Critical**: `lastResolvedNote[padIndex]` stores the note sent at noteOn. noteOff ALWAYS uses this stored value, never re-resolves. Prevents orphan notes on scale change during play.

### Loop Execution Order (Core 1)

Critical path first, secondary after. MIDI latency depends on this order.

```
1. Read double buffer (instant)              ← CRITICAL PATH START
2. USB MIDI transport update (clock polling)
3. Read buttons (left + rear)
── handleManagerUpdates(state, leftHeld) ──
4. BankManager.update()                       ← left button
5. ScaleManager.update()                      ← left button (same as bank)
5b. Consume scale/octave/hold flags + LED confirmations
    (ARPEG scale change: flush noteOffs before re-resolve)
6. ClockManager.update()                      ← PLL + tick generation
── handlePlayStopPad(state, holdBeforeUpdate, bankSwitched) ──
7. Play/Stop pad (ARPEG + HOLD ON only)
── handlePadInput(state, now) ──
8. processNormalMode() or processArpMode()
8b. Stuck-note cleanup on left-release edge
9. ArpScheduler.tick()                       ← all background arps
9b. ArpScheduler.processEvents()             ← gate noteOff + shuffle noteOn
10. MidiEngine.flush()                        ← CRITICAL PATH END
── handlePotPipeline(leftHeld, rearHeld) ──
11. PotRouter.update()                        ← SECONDARY (PotFilter::updateAll + 5 pots)
11b. Send MIDI CC/PB if dirty                 ← from user-assigned pot mappings
12. BatteryMonitor.update()
13. LedController.update()                    ← multi-bank state + confirmations
── handlePanicChecks(now, rearHeld) ──
14. NvsManager.notifyIfDirty()                ← non-blocking signal to NVS task
── debugOutput(leftHeld, rearHeld) ──
15. vTaskDelay(1)
```

## Boot Sequence

LEDs show progressive fill during boot (1 LED per step). On failure, the failed step blinks rapidly while prior steps stay solid.

```
Step 1: ●○○○○○○○  LED hardware ready
Step 2: ●●○○○○○○  I2C bus ready
Step 3: ●●●○○○○○  Keyboard OK
        ●●◉○○○○○  ← Step 3 BLINKS = keyboard/MPR121 FAILED (halts forever)
        [setup mode detection: 3s window to press rear button, then hold 3s — chase pattern during hold]
Step 4: ●●●●○○○○  MIDI Transport (USB + BLE) started
Step 5: ●●●●●○○○  NVS loaded
Step 6: ●●●●●●○○  Arp system ready
Step 7: ●●●●●●●○  Managers ready (Bank, Scale, Pot)
Step 8: ●●●●●●●●  All systems go (200ms full bar)
        → endBoot() → normal bank display
```

## LED Display

### Normal Display (multi-bank state)

LedController drives a SK6812 RGBW NeoPixel strip via `Adafruit_NeoPixel` (NEO_GRBW). Colors come from 13 configurable color slots (preset + hue offset), resolved at load time via `resolveColorSlot()`. System colors (error, boot, battery) are hardcoded RGBW in HardwareConfig.h. A single `setPixel(led, rgbw, intensityPct)` method combines intensity × master brightness × 255 in one 32-bit multiply (no truncation at low values), then applies gamma correction via runtime LUT (`_gammaLut[256]`, rebuilt at boot from configurable `gammaTenths` in LedSettingsStore, default gamma 2.0, range 1.0-3.0). Brightness pot acts as master perceptual fader (0-100%) via `POT_BRIGHTNESS_CURVE[]` (compile-time selectable: LOW_BIASED/LINEAR/SIGMOID). All intensities are 0-100% perceptual in `LedSettingsStore`.

| State | Color | Pattern | Intensity | Rate |
|---|---|---|---|---|
| Current NORMAL | White (W channel) | Solid | 85% | — |
| Current ARPEG idle (pile empty) | Blue | Solid dim | fgArpStopMin | — |
| Current ARPEG stopped (notes loaded) | Blue | Sine pulse | fgArpStopMin↔fgArpStopMax | ~1.5s period |
| Current ARPEG playing | Blue | Solid + white tick flash | fgArpPlayMax, spike tickFlashFg on step | flash 30ms |
| Background NORMAL | White dim | Solid | ~10% | — |
| Background ARPEG (all states) | Blue dim | Solid (+ tick flash if playing) | bgArpStopMin or bgArpPlayMin | flash 30ms if playing |

**Sine pulse (FG stopped-loaded only)**: 256-entry precomputed `LED_SINE_LUT[256]` in HardwareConfig.h, shared with ToolLedSettings. 16-bit phase + linear interpolation. Only used for FG arpeg stopped with notes loaded ("breathing = ready to play"). All other states are solid.

**Tick flash**: `ArpEngine::consumeTickFlash()` returns true once per arp step. LedController stores `_flashStartTime[i]` and holds the flash for `tickFlashDurationMs` (default 30ms). Only fires during playback.

**Legacy fields**: `fgArpPlayMin`, `bgArpStopMax`, `bgArpPlayMax` exist in LedSettingsStore for NVS compatibility but are unused at runtime (no pulse on playing/BG states). Hidden in Tool 7 UI.

### Confirmation Blinks (ALL overlay — bar never blanks)

All 10 `ConfirmType` values are **overlay-only**: `renderConfirmation()` tracks state/expiry, `renderNormalDisplay()` renders the overlay on top of the normal bank display. No confirmation ever calls `clearPixels()`. The normal display (including tick flashes) runs underneath at all times.

| Event | ConfirmType | Color | Pattern | Duration |
|---|---|---|---|---|
| Bank switch | `CONFIRM_BANK_SWITCH` | White | Blink on/off destination LED | bankDurationMs (300ms) |
| Scale root | `CONFIRM_SCALE_ROOT` | Vivid yellow | Blink on/off current LED | scaleRootDurationMs (200ms) |
| Scale mode | `CONFIRM_SCALE_MODE` | Pale yellow | Blink on/off current LED | scaleModeDurationMs (200ms) |
| Scale chromatic | `CONFIRM_SCALE_CHROM` | Golden yellow | Blink on/off current LED | scaleChromDurationMs (200ms) |
| Hold ON | `CONFIRM_HOLD_ON` | Deep blue | Fade IN 0%→100% (latch engage) | holdFadeMs (300ms) |
| Hold OFF | `CONFIRM_HOLD_OFF` | Deep blue | Fade OUT 100%→0% (latch release) | holdFadeMs (300ms) |
| Play | `CONFIRM_PLAY` | Green | Blink overlay on current LED | playDurationMs (200ms) |
| Stop | `CONFIRM_STOP` | Cyan | Blink overlay on current LED | stopDurationMs (200ms) |
| Octave | `CONFIRM_OCTAVE` | Blue-violet | Blink overlay on current LED (1 LED, not 2) | octaveDurationMs (300ms) |

New confirmation preempts active one. `LedController` no longer depends on `ClockManager` (beat-sync removed). All blink counts and durations configurable in Tool 7.

### Priority State Machine (in `update()`)

```
1. Boot mode          (progressive white fill / red failure blink)
2. Setup comet        (violet comet during Tools 1-7)
3. Chase pattern      (calibration entry — white chase)
4. Error              (LEDs 3-4 blink red 500ms — sensing task stall)
5. Battery gauge      (8-LED red→green gradient bar, 3s)
6. Pot bargraph       (solid bar + catch visualization; tempo bargraph adds BPM pulse on tip LED)
7. Confirmation state tracking (10 types, auto-expire timers)
8. Calibration mode   (all off + validation flash)
9. Normal bank display + confirmation overlays (multi-bank solid/pulse/tick + overlay blinks/fades)
```

### Pot Bargraph

`showPotBargraph(realLevel, potLevel, caught)` — 3 params. Shows target level as solid bar + physical pot position indicator. Catch state visualized: uncaught pots show pot position dimly until caught. Configurable duration via Tool 5 Settings (parameter 6, 0-indexed as case 5). Range 1-10s, default 3s, steps of 500ms. Stored in NVS (`illpad_set`, field `potBarDurationMs`).

`showTempoBargraph(realLevel, potLevel, caught, bpm)` — 4 params. Same as pot bargraph but with **tempo pulse**: the tip LED (highest lit) blinks on/off at the displayed BPM rate (period = 60000/bpm, duty 50%). Only pulses when caught. Called from `handlePotPipeline()` when `PotRouter::getBargraphTarget() == TARGET_TEMPO_BPM`.

## Buttons

- **Left (hold + pad)**: single-layer control. 8 bank pads + 15 scale pads (7 root + 7 mode + 1 chromatic) + 1 HOLD toggle (ARPEG only) + 4 octave pads (ARPEG only, 1-4 octaves). All visible in one hold. BankManager and ScaleManager share this button — no conflict (pad roles are distinct, checked by Tool 3). All notes off for NORMAL on bank switch. ARPEG: nothing (arp lives/dies by its own logic). Scale change on ARPEG: no allNotesOff — arp re-resolves at next tick.
- **Left (hold + pot)**: modifier for 4 right pots (2nd slot: division, AT deadzone/shuffle depth, pitch bend/shuffle template, velocity variation).
- **Play/Stop pad**: ARPEG + HOLD ON only — toggles arp transport. In HOLD OFF mode, this pad is a regular music pad (enters the arp pile like any other pad). On NORMAL banks, always a regular music pad.
- **Rear (press)**: battery gauge (3s).
- **Rear (boot, two-phase)**: press within 3s of boot, then hold 3s → setup mode (chase LED during hold).
- **Rear (hold + pot rear)**: modifier for rear pot (pad sensitivity).

## Arpeggiator

**Pile stores padOrder positions, NOT MIDI notes.** Resolution happens at each tick via current ScaleConfig. Changing scale on a background arp = immediate effect at next tick, no interruption.

**HOLD OFF (live)**: press=add position, release=remove. All fingers up = arp stops naturally. BankManager has NO arp stop logic.

**HOLD ON (persistent)**: press=add, double-tap(configurable 100-250ms, default 150ms)=remove. Play/stop pad controls transport (restart from beginning). Bank switch = arp continues in background.

5 patterns (Up/Down/UpDown/Random/Order) via pot right 3. 1-4 octaves via 4 pads in single-layer control (hold left + octave pad). Up to 48 positions (192 steps with 4 oct). Division via hold+pot right 1 (9 binary values: 4/1→1/64). Gate/shuffle/velocity per-bank via pots.

**Quantized start** (per-bank, set in Tool 4): Immediate (fire on next division boundary), Beat (snap to next 1/4 note, 24 ticks), or Bar (snap to next bar, 96 ticks). Stop is always immediate. HOLD OFF auto-play also respects quantize on 0→1 finger transition.

**Shuffle**: 5 groove templates (16 steps each), depth 0.0–1.0 via pot (extreme: notes can overlap across steps). Template selected via hold+pot right 3 (5 discrete values). Depth controls intensity, template controls groove shape. Shuffle offset = template[step%16] × depth × stepDuration / 100. Gate and shuffle use a unified time-based event system with reference counting (up to 36 pending events per engine): ArpScheduler.tick() schedules noteOn (with shuffle delay) and noteOff (at noteOnTime + stepDuration × gateLength), ArpScheduler.processEvents() fires them in real time. Overlapping notes handled via per-note refcount (MIDI noteOn only on 0→1, noteOff only on 1→0). Shuffle step counter resets on: play/stop toggle, pile 0→1 note, pattern change.

## Pots — PotRouter

PotFilter reads 5 ADCs via 16× oversampling + adaptive EMA + deadband gate + sleep/wake. PotRouter consumes `PotFilter::getStable()` / `hasMoved()`, resolves button combos, applies catch system, exposes getters. Binding table is **runtime data** rebuilt from a user-configurable `PotMappingStore`.

**Button modifiers**: Left button = modifier for 4 right pots. Rear button = modifier for rear pot only. They never cross.

**Pot values**: NORMAL params (shape, slew, AT deadzone) = **GLOBAL**. ARPEG params (gate, shuffle depth, shuffle template, division, pattern) = **PER BANK**. Velocity params (base, variation) = **PER BANK** (NORMAL + ARPEG). Pitch bend offset = **PER BANK** (NORMAL only). Tempo, LED brightness, pad sensitivity = **GLOBAL**. MIDI CC/PB = **volatile per-bank** (sends on foreground channel, no NVS recall). Catch resets on bank switch for per-bank params.

### Default Pot Mapping

| Pot | NORMAL alone | NORMAL + hold left | ARPEG alone | ARPEG + hold left |
|---|---|---|---|---|
| Right 1 | Tempo (10-260 BPM) | — empty — | Tempo (10-260 BPM) | Division (9 binary) |
| Right 2 | Response shape | AT deadzone | Gate length | Shuffle depth (0.0-1.0) |
| Right 3 | Slew rate | Pitch bend (per-bank) | Pattern (5 discrete) | Shuffle template (5) |
| Right 4 | Base velocity | Velocity variation | Base velocity | Velocity variation |

| Rear pot | Alone | + hold rear |
|---|---|---|
| Rear | LED brightness | Pad sensitivity |

**Empty slot** (Right 1, NORMAL + hold left): reserved for future parameter.

### User-Configurable Mapping (Tool 6)

The 4 right pots × 2 layers = **8 slots per context** (NORMAL and ARPEG independently). Rear pot is fixed (not configurable). Each slot can be assigned any parameter from that context's pool, plus MIDI CC (with CC#) or MIDI Pitchbend.

**PotMappingStore** (NVS `illpad_pmap`): two arrays of 8 `PotMapping` entries (one per context). Each entry = `{PotTarget, ccNumber}`. Has magic/version for NVS compatibility.

**Rebuild flow**: `loadMapping()` or `applyMapping()` → `rebuildBindings()` → `seedCatchValues()`. The binding table, catch states, and CC slot tracking are all regenerated.

**MIDI CC output**: multiple CCs allowed (different CC numbers). CC slot lookup uses binding index (not CC number) to avoid cross-context collision. Only sends on value change (dirty flag pattern, no MIDI flood). Sends on foreground bank's channel.

**MIDI Pitchbend output**: max one per context. Auto-steals if a second is assigned. Sends on foreground bank's channel. Only sends on value change.

**Global target catch propagation**: when a global target (e.g., Tempo) exists in both NORMAL and ARPEG contexts on the same pot, writing to one propagates `storedValue` to all same-target bindings. Prevents stale catch after bank type switch.

## MIDI Clock & Transport

ClockManager receives 0xF8 (clock ticks) via USB+BLE. Priority: USB > BLE > last known > internal (pot right 1). Start/Stop/Continue (0xFA/0xFB/0xFC) are intentionally ignored — the ILLPAD is an instrument, not a transport follower.
**PLL** smooths BLE jitter (±15ms → ±1-2ms). ArpEngines sync to smoothed clock.

**ILLPAD never sends Start/Stop/Continue** — only clock ticks (0xF8) in master mode. The ILLPAD is an instrument, not a transport controller.

### Transport Behavior by Mode

| Mode | Clock ticks (0xF8) | Start/Stop/Continue | Sends |
|---|---|---|---|
| **Slave** | PLL sync | Ignored | Nothing |
| **Master** | Generate from pot tempo | Ignored | Ticks only (0xF8) |

## Setup Mode (press rear within 3s of boot, then hold 3s)

VT100 terminal, serial keyboard input only (no physical button in setup mode).

```
[1] Pressure Calibration  — unchanged from V1
[2] Pad Ordering           — touch low→high, positions 1-48, no base note
[3] Pad Roles              — bank(8) + scale(15) + arp(6: hold+play/stop+4 octave), color grid, collision check
[4] Bank Config            — NORMAL/ARPEG per bank (max 4 ARPEG), quantize mode per ARPEG (Immediate/Beat/Bar)
[5] Settings               — profile, AT rate, BLE interval, clock, double-tap, bargraph duration, panic-on-reconnect, battery cal
[6] Pot Mapping            — user-configurable pot parameter assignments (per context: NORMAL/ARPEG)
[7] LED Settings           — color presets + hue + intensity + timing + gamma, confirmation blinks (2 pages: COLOR+TIMING/CONFIRM, toggle with 't', live preview on LEDs 3-4, 'b' preview blink). COLOR page: STOPPED(5 rows)/PLAYING(2 rows)/EVENTS(10 rows)/TIMING(3 rows) = 20 visible params (3 legacy rows hidden). CONFIRM page: 15 params (hold fade, play/stop blinks+duration, octave blinks+duration).
[0] Reboot
```

### Tool 6 — Pot Mapping UX

Two context pages (NORMAL / ARPEG), toggle with `t`. Physical pot detection: turn a pot (or hold-left + turn) to select a slot. Pool line always visible showing all assignable parameters, color-coded: GREEN = available, DIM = already assigned. `< >` cycles through pool, Enter confirms assignment and auto-saves to NVS. Steal logic: picking an already-assigned param orphans the source slot to "empty". CC enters CC# sub-mode immediately (`< >` adjusts number, ENTER confirms, `q` cancels and restores previous assignment). PB: max one per context, auto-steals. `d` resets current context to defaults. `q` exits.

## Source Files

```
src/
├── main.cpp, HardwareConfig.h
├── core/       CapacitiveKeyboard†, MidiTransport, LedController, PotFilter, KeyboardData.h
├── managers/   BankManager, ScaleManager, PotRouter, BatteryMonitor, NvsManager
├── midi/       MidiEngine, ScaleResolver, ClockManager, GrooveTemplates.h
├── arp/        ArpEngine, ArpScheduler
└── setup/      SetupManager, ToolCalibration, ToolPadOrdering, ToolPadRoles,
                ToolBankConfig, ToolSettings, ToolPotMapping, ToolLedSettings,
                SetupUI, SetupPotInput
```

† = DO NOT MODIFY (ported from V1, musically calibrated)

## NVS (via NvsManager only)

### Unified API — 3 static helpers

All setup Tools and `loadAll()` use these 3 static methods on `NvsManager`:

- **`loadBlob(ns, key, magic, version, out, size)`** — read + validate magic/version. Returns false on missing/corrupt.
- **`saveBlob(ns, key, data, size)`** — write blob. Warns (`DEBUG_SERIAL`) if magic==0. Returns false on write failure.
- **`checkBlob(ns, key, magic, version, size)`** — validate only (no data copy). Used by menu status badges.

Internally, `loadBlob` and `checkBlob` share `readAndValidateBlob()` (anonymous namespace, no duplication). All error paths log under `DEBUG_SERIAL`.

### Descriptor table + validation

`NVS_DESCRIPTORS[10]` in `KeyboardData.h` — one entry per Store blob (ns, key, magic, version, size). `TOOL_NVS_FIRST[7]`/`TOOL_NVS_LAST[7]` map Tools 1-7 to descriptor ranges (T3 spans 3, T7 spans 2). Menu uses a loop over these to check all stores.

`NVS_BLOB_MAX_SIZE` (128) — all Store structs must fit. `static_assert` on every Store struct enforces this at compile time. `static_assert(offsetof(SettingsStore, baselineProfile) == 3)` guards the byte-3 layout.

7 `inline validate*()` functions in `KeyboardData.h` — shared by `loadAll()`, setup Tools, and future WiFi handler. Single source of truth for field bounds.

### Store structs (V2 — replace raw formats)

| Struct | Namespace | Key | Size | Replaces |
|---|---|---|---|---|
| `ScalePadStore` | `illpad_spad` | `"pads"` | 20B | 3 separate keys (root_pads, mode_pads, chrom_pad) |
| `ArpPadStore` | `illpad_apad` | `"pads"` | 12B | 3 separate keys (hold_pad, ps_pad, oct_pads) |
| `BankTypeStore` | `illpad_btype` | `"config"` | 20B | raw types[8] + qmode[8] (2 blobs, desync risk) |

### Namespace table

| Namespace | Content |
|---|---|
| `illpad_cal` | CalDataStore (maxDelta[48]) |
| `illpad_nmap` | NoteMapStore — padOrder[48] (positions, NOT MIDI notes) |
| `illpad_bpad` | BankPadStore — bankPads[8] |
| `illpad_bank` | current bank (0-7) — scalar, not blob |
| `illpad_set` | SettingsStore (profile, AT rate, BLE interval, clock mode, double-tap, bargraph duration, panic-on-reconnect, battery ADC cal) |
| `illpad_pot` | PotParamsStore (shape, slew, AT deadzone) |
| `illpad_tempo` | tempo BPM (global) — scalar, not blob |
| `illpad_btype` | BankTypeStore — types[8] + quantize[8] (key `"config"`) |
| `illpad_scale` | ScaleConfig per bank (keys `"cfg_0"` through `"cfg_7"`) |
| `illpad_spad` | ScalePadStore — 7 root + 7 mode + 1 chrom (key `"pads"`) |
| `illpad_apad` | ArpPadStore — 1 hold + 1 play/stop + 4 octave (key `"pads"`) |
| `illpad_apot` | ArpPotStore per bank (gate, shuffle depth, shuffle template, div, pattern, octave range) |
| `illpad_bvel` | base velocity + velocity variation per bank (NORMAL + ARPEG) |
| `illpad_pbnd` | pitch bend offset per bank (NORMAL) |
| `illpad_led` | LED brightness (global) — scalar, not blob |
| `illpad_sens` | pad sensitivity (global) — scalar, not blob |
| `illpad_pmap` | PotMappingStore (both NORMAL + ARPEG contexts) |
| `illpad_pflt` | PotFilterStore (snap, activity threshold, sleep, deadband, edge snap, wake threshold) |
| `illpad_lset` | LedSettingsStore (intensities, timings, confirmations, gammaTenths) + ColorSlotStore (13 preset+hue slots, magic 0xC010) |

NVS writes happen in a **dedicated FreeRTOS task** (low priority). Loop never blocks on flash.

## CRITICAL — DO NOT MODIFY

- **CapacitiveKeyboard.cpp/.h** — pressure pipeline is musically calibrated. Do not change.
- **Pressure tuning constants** in HardwareConfig.h (thresholds, smoothing, slew, I2C clock).
- **platformio.ini** (unless adding lib_deps).

## CRITICAL — KEEP IN SYNC

- **`docs/reference/architecture-briefing.md`** — runtime data flows, inter-core sync points, invariants, dirty flags. Used by subagents (testmusicien, audit) and new sessions for quick context. **Update when you change a function in any of the 5 documented flows** (pad→MIDI, arp tick, bank switch, scale change, pot→param).
- **`docs/reference/vt100-design-guide.md`** — VT100 terminal aesthetic spec: Unicode box drawing, color palette, navigation patterns, frame primitives, grid system, save feedback, Python script conventions. Part 1 is generic (reusable across instruments), Part 2 is ILLPAD-specific (tool layouts, role categories, amber palette). **Read before touching any setup UI code. Update when adding new visual patterns or changing the aesthetic.**
- **`docs/reference/nvs-reference.md`** — NVS Store struct catalog, load/save/check patterns, how to add a namespace. **Read before touching any NVS code. Update when adding a new Store struct or namespace.**
- **`ItermCode/vt100_serial_terminal.py`** — the Python serial terminal is the only way to interact with setup mode. When setup tools (`src/setup/`) change input handling, escape sequences, line endings, or VT100 rendering, **always verify and update the terminal script**. The two must stay synchronised (e.g., arrow key atomic send, line ending normalization, DEC 2026 sync support).

## Conventions

- `s_` static globals, `_` members, `SCREAMING_SNAKE_CASE` constants
- `#if DEBUG_SERIAL` for debug output (1 = all messages, 0 = complete silence, zero overhead)
- `#if DEBUG_HARDWARE` for runtime pot/button state logging (separate from DEBUG_SERIAL)
- No `new`/`delete` at runtime — static instantiation only
- Unsigned arithmetic for `millis()` (overflow-safe subtraction pattern: `(now - startTime) < duration`)
- Small stack allocations (FreeRTOS task stacks are limited)
- C++17, Arduino framework, PlatformIO
- `std::atomic` for inter-core sync — NEVER `volatile`

## Performance Budget

| Resource | Usage | Note |
|---|---|---|
| Core 0 | ~92% | Sensing — unchanged, this is the bottleneck |
| Core 1 | ~16% | Plenty of headroom |
| BLE MIDI | 30-50% worst case | noteOn/Off bypass queue. Aftertouch overflow tolerated. |
| SRAM | ~16% | ~51KB / 320KB |
