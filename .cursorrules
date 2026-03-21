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
- **USB**: Single USB-C on GPIO 19/20 (native USB, no UART bridge)
- **2 Buttons**: left (bank+scale+arp single-layer control), rear (battery/setup/modifier pot rear). GPIOs TBD.
- **5 Pots**: 4 right (tempo, shape/gate, slew/pattern, velocity), 1 rear (LED brightness/sensitivity). GPIOs TBD.
- **8 LEDs** (circle, PWM brightness, no RGB)
- **Battery**: LiPo 3.7V, BQ25185 charger, ADC voltage divider

Pads measure **skin contact surface area**, not mechanical force. Aftertouch = yes. Velocity from pressure = unreliable.

## Architecture

### Dual-Core FreeRTOS

- **Core 0** — `sensingTask`: I2C poll 4× MPR121, pressure pipeline, publish to double buffer.
- **Core 1** — `loop()`: read double buffer, edge detection, MIDI, arp ticks, pots, buttons, LEDs, NVS.

### Double Buffer (NOT mutex)

Inter-core sync uses `std::atomic<uint8_t>` for the read index — not mutex, not volatile.
- Core 0 owns `s_writeIndex` (plain uint8_t). Core 0 publishes via `s_readIndex.store(memory_order_release)`.
- Core 1 reads via `s_readIndex.load(memory_order_acquire)`. Never blocks.
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
3. **Control Pad Assignment**: 29 pads with control roles (bank 8 + scale 15 + arp 6 + octave 4). One role per pad. Set in Tool 3.

### Note Resolution

`ScaleResolver::resolve(padIndex, padOrder, scaleConfig)` → MIDI note or 0xFF.
- Chromatic: `rootBase + padOrder[padIndex]`
- Scale: `rootBase + (order/7)*12 + scaleIntervals[mode][order%7]`

**Critical**: `lastResolvedNote[padIndex]` stores the note sent at noteOn. noteOff ALWAYS uses this stored value, never re-resolves. Prevents orphan notes on scale change during play.

### Loop Execution Order (Core 1)

Critical path first, secondary after. MIDI latency depends on this order.

```
1. Read double buffer (instant)              ← CRITICAL PATH START
2. Read buttons (left + rear)
3. USB MIDI transport update (clock polling)
4. BankManager.update()                       ← left button
5. ScaleManager.update()                      ← left button (same as bank)
5b. Consume scale/octave/hold flags + LED confirmations
    (ARPEG scale change: flush noteOffs before re-resolve)
6. ClockManager.update()                      ← PLL + tick generation
7. DAW transport: consume Start/Stop flags
    Stop → flush all playing arps (immediate silence)
    Start → flush + resetStepIndex + resetSync (bar 1 restart)
8. Play/Stop pad (ARPEG + HOLD ON only)
9. processNormalMode() or processArpMode()
10. ArpScheduler.tick()                       ← all background arps
10b. ArpScheduler.processEvents()             ← gate noteOff + shuffle noteOn
11. MidiEngine.flush()                        ← CRITICAL PATH END
12. PotRouter.update()                        ← SECONDARY (5 pots)
13. BatteryMonitor.update()
14. LedController.update()                    ← multi-bank state + confirmations
15. NvsManager.notifyIfDirty()                ← non-blocking signal to NVS task
16. vTaskDelay(1)
```

## Boot Sequence

LEDs show progressive fill during boot (1 LED per step). On failure, the failed step blinks rapidly while prior steps stay solid.

```
Step 1: ●○○○○○○○  LED hardware ready
Step 2: ●●○○○○○○  I2C bus ready
Step 3: ●●●○○○○○  Keyboard OK
        ●●◉○○○○○  ← Step 3 BLINKS = keyboard/MPR121 FAILED (halts forever)
        [setup mode detection window — chase pattern if rear button held 3s]
Step 4: ●●●●○○○○  MIDI Transport (USB + BLE) started
Step 5: ●●●●●○○○  NVS loaded
Step 6: ●●●●●●○○  Arp system ready
Step 7: ●●●●●●●○  Managers ready (Bank, Scale, Pot)
Step 8: ●●●●●●●●  All systems go (200ms full bar)
        → endBoot() → normal bank display
```

## LED Display

### Normal Display (multi-bank state)

LedController reads `BankSlot[]` to show all 8 banks simultaneously. All brightness values are compile-time constants in HardwareConfig.h (`LED_FG_*`, `LED_BG_*`), scaled by the global brightness pot.

| State | Pattern | Brightness | Rate |
|---|---|---|---|
| Current NORMAL | Solid | 100% | — |
| Current ARPEG stopped | Sine pulse | 30%↔100% | ~1.5s period |
| Current ARPEG playing | Sine pulse + tick flash | 30%↔80%, spike 100% on beat | Pulse ~1.5s, flash 30ms |
| Background NORMAL | Off | 0% | — |
| Background ARPEG stopped | Sine pulse | 8%↔25% | ~1.5s period |
| Background ARPEG playing | Sine pulse + tick flash | 8%↔20%, spike 25% on beat | Pulse ~1.5s, flash 30ms |

**Sine pulse**: 64-entry precomputed LUT, index = `(millis() / (LED_PULSE_PERIOD_MS/64)) % 64`. Integer math only in `update()`.

**Tick flash**: `ArpEngine::consumeTickFlash()` returns true once per arp step. LedController stores `_flashStartTime[i]` and holds the flash for `LED_TICK_FLASH_DURATION_MS`.

### Confirmation Blinks (auto-expiring overlays)

| Event | Pattern | Duration |
|---|---|---|
| Bank switch | Triple blink ALL 8 at 50% | 300ms |
| Scale change (root/mode/chromatic) | Double blink current LED | 200ms |
| Hold toggle | Single long blink current LED | 250ms (150ms on + 100ms off) |
| Octave change | Single blink of N LEDs (1-4) | 100ms |

Timing derived from `LED_CONFIRM_UNIT_MS` (50ms) × phase count. Priority: below error/battery/bargraph, above normal display. New confirmation preempts active one.

### Priority State Machine (in `update()`)

```
1. Boot mode          (progressive fill / failure blink)
2. Chase pattern      (calibration entry)
3. Error              (all 8 blink 500ms — sensing task stall)
4. Battery gauge      (8-LED bar with heartbeat pulse, 3s)
5. Pot bargraph       (solid bar, configurable duration via Tool 5)
6. Confirmation blinks (bank/scale/hold/octave, auto-expire)
7. Calibration mode   (all off + validation flash)
8. Normal bank display (multi-bank state with sine pulse + tick flash)
```

### Bargraph Persistence

Configurable via Tool 5 Settings (parameter 8). Range 1-10s, default 3s, steps of 500ms. Stored in NVS (`illpad_set`, field `potBarDurationMs`).

## Buttons

- **Left (hold + pad)**: single-layer control. 8 bank pads + 15 scale pads (7 root + 7 mode + 1 chromatic) + 1 HOLD toggle (ARPEG only) + 4 octave pads (ARPEG only, 1-4 octaves). All visible in one hold. BankManager and ScaleManager share this button — no conflict (pad roles are distinct, checked by Tool 3). All notes off for NORMAL on bank switch. ARPEG: nothing (arp lives/dies by its own logic). Scale change on ARPEG: no allNotesOff — arp re-resolves at next tick.
- **Left (hold + pot)**: modifier for 4 right pots (2nd slot: division, AT deadzone/shuffle depth, pitch bend/shuffle template, velocity variation).
- **Play/Stop pad**: ARPEG + HOLD ON only — toggles arp transport. In HOLD OFF mode, this pad is a regular music pad (enters the arp pile like any other pad). On NORMAL banks, always a regular music pad.
- **Rear (press)**: battery gauge (3s).
- **Rear (hold 3s at boot)**: setup mode.
- **Rear (hold + pot rear)**: modifier for rear pot (pad sensitivity).

## Arpeggiator

**Pile stores padOrder positions, NOT MIDI notes.** Resolution happens at each tick via current ScaleConfig. Changing scale on a background arp = immediate effect at next tick, no interruption.

**HOLD OFF (live)**: press=add position, release=remove. All fingers up = arp stops naturally. BankManager has NO arp stop logic.

**HOLD ON (persistent)**: press=add, double-tap(configurable 100-250ms, default 150ms)=remove. Play/stop pad controls transport (restart from beginning). Bank switch = arp continues in background.

5 patterns (Up/Down/UpDown/Random/Order) via pot right 3. 1-4 octaves via 4 pads in single-layer control (hold left + octave pad). Up to 48 positions (192 steps with 4 oct). Division via hold+pot right 1 (9 binary values: 4/1→1/64). Gate/shuffle/velocity per-bank via pots.

**Quantized start** (per-bank, set in Tool 4): Immediate (fire on next division boundary), Beat (snap to next 1/4 note, 24 ticks), or Bar (snap to next bar, 96 ticks). Stop is always immediate. MIDI Start (0xFA) bypasses quantize. HOLD OFF auto-play also respects quantize on 0→1 finger transition.

**Shuffle**: 5 groove templates (16 steps each), depth 0.0–1.0 via pot (extreme: notes can overlap across steps). Template selected via hold+pot right 3 (5 discrete values). Depth controls intensity, template controls groove shape. Shuffle offset = template[step%16] × depth × stepDuration / 100. Gate and shuffle use a unified time-based event system with reference counting (up to 36 pending events per engine): ArpScheduler.tick() schedules noteOn (with shuffle delay) and noteOff (at noteOnTime + stepDuration × gateLength), ArpScheduler.processEvents() fires them in real time. Overlapping notes handled via per-note refcount (MIDI noteOn only on 0→1, noteOff only on 1→0). Shuffle step counter resets on: play/stop toggle, pile 0→1 note, pattern change.

## Pots — PotRouter

PotRouter reads 5 ADCs (4 right + 1 rear), resolves button combos, applies catch system, exposes getters. Declarative binding table — add a param = add a line.

**Button modifiers**: Left button = modifier for 4 right pots. Rear button = modifier for rear pot only. They never cross.

**Pot values**: NORMAL params (shape, slew, AT deadzone) = **GLOBAL**. ARPEG params (gate, shuffle depth, shuffle template, division, pattern) = **PER BANK**. Velocity params (base, variation) = **PER BANK** (NORMAL + ARPEG). Pitch bend offset = **PER BANK** (NORMAL only). Tempo, LED brightness, pad sensitivity = **GLOBAL**. Catch resets on bank switch for per-bank params.

| Pot | NORMAL alone | NORMAL + hold left | ARPEG alone | ARPEG + hold left |
|---|---|---|---|---|
| Right 1 | Tempo (10-260 BPM) | — empty — | Tempo (10-260 BPM) | Division (9 binary) |
| Right 2 | Response shape | AT deadzone | Gate length | Shuffle depth (0.0-1.0) |
| Right 3 | Slew rate | Pitch bend (per-bank) | Pattern (5 discrete) | Shuffle template (5) |
| Right 4 | Base velocity | Velocity variation | Base velocity | Velocity variation |
| Rear | LED brightness | — | LED brightness | — |

| Rear pot | Alone | + hold rear |
|---|---|---|
| Rear | LED brightness | Pad sensitivity |

**Empty slot** (Right 1, NORMAL + hold left): reserved for future parameter.

## MIDI Clock & Transport

ClockManager receives 0xF8/0xFA/0xFB/0xFC via USB+BLE. Priority: USB > BLE > last known > internal (pot right 1).
**PLL** smooths BLE jitter (±15ms → ±1-2ms). ArpEngines sync to smoothed clock.

**ILLPAD never sends Start/Stop/Continue** — only clock ticks (0xF8) in master mode. The ILLPAD is an instrument, not a transport controller.

### Transport Behavior by Mode

| Mode | Clock ticks | Start (0xFA) | Continue (0xFB) | Stop (0xFC) | Sends |
|---|---|---|---|---|---|
| **Slave, follow=ON** | PLL sync | Reset tick counter (re-sync to bar 1) | Resume (no tick reset, arps continue from position) | Flush all arp noteOffs (immediate silence) | Nothing |
| **Slave, follow=OFF** | PLL sync | Ignore | Ignore | Ignore | Nothing |
| **Master** | Generate from pot tempo | Ignore | Ignore | Ignore | Ticks only (0xF8) |

**Follow Transport** is configurable via Tool 5 Settings ("Follow Transport: Yes/No"). Default: Yes. Only applies in Slave mode. Clock ticks are always received regardless of this setting — only Start/Continue/Stop are gated.

## Setup Mode (hold rear button 3s at boot)

VT100 terminal, serial input + button = ENTER.

```
[1] Pressure Calibration  — unchanged from V1
[2] Pad Ordering           — touch low→high, positions 1-48, no base note
[3] Pad Roles              — bank(8) + scale(15) + arp(6: hold+play/stop+4 octave), color grid, collision check
[4] Bank Config            — NORMAL/ARPEG per bank (max 4 ARPEG), quantize mode per ARPEG (Immediate/Beat/Bar)
[5] Settings               — profile, AT rate, BLE interval, clock, follow transport, double-tap, bargraph duration
[0] Reboot
```

## Source Files

```
src/
├── main.cpp, HardwareConfig.h
├── core/       CapacitiveKeyboard†, MidiTransport, LedController, KeyboardData.h
├── managers/   BankManager, ScaleManager, PotRouter, BatteryMonitor, NvsManager
├── midi/       MidiEngine, ScaleResolver, ClockManager
├── arp/        ArpEngine, ArpScheduler
└── setup/      SetupManager, ToolCalibration, ToolPadOrdering, ToolPadRoles,
                ToolBankConfig, ToolSettings, SetupUI
```

† = DO NOT MODIFY (ported from V1, musically calibrated)

## NVS (via NvsManager only)

| Namespace | Content |
|---|---|
| `illpad_cal` | CalDataStore (maxDelta[48]) |
| `illpad_nmap` | padOrder[48] (positions, NOT MIDI notes) |
| `illpad_bpad` | bankPads[8] |
| `illpad_bank` | current bank (0-7) |
| `illpad_set` | settings (profile, AT rate, BLE interval, clock mode, follow transport, double-tap, bargraph duration) |
| `illpad_pot` | global pot params (shape, slew, AT deadzone) |
| `illpad_tempo` | tempo BPM (global) |
| `illpad_btype` | bank types[8] (NORMAL/ARPEG) + quantize modes[8] (Immediate/Beat/Bar) |
| `illpad_scale` | scale config per bank (chromatic, root, mode) |
| `illpad_spad` | scale pads (7 root + 7 mode + 1 chrom) |
| `illpad_apad` | arp pads (1 hold + 1 play/stop + 4 octave) |
| `illpad_apot` | arp pot params per bank (gate, shuffle depth, shuffle template, div, pattern, octave range) |
| `illpad_bvel` | base velocity + velocity variation per bank (NORMAL + ARPEG) |
| `illpad_pbnd` | pitch bend offset per bank (NORMAL) |
| `illpad_led` | LED brightness (global) |
| `illpad_sens` | pad sensitivity (global) |

NVS writes happen in a **dedicated FreeRTOS task** (low priority). Loop never blocks on flash.

## CRITICAL — DO NOT MODIFY

- **CapacitiveKeyboard.cpp/.h** — pressure pipeline is musically calibrated. Do not change.
- **Pressure tuning constants** in HardwareConfig.h (thresholds, smoothing, slew, I2C clock).
- **platformio.ini** (unless adding lib_deps).

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
| SRAM | ~5% | ~16KB / 320KB |
