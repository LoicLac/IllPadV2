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
- **8 LEDs** (circle) + 1 RGB onboard (GPIO48, `INT_LED` compile flag)
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
           lastResolvedNote[48], baseVelocity, velocityVariation, pitchBendOffset }
```

- **NORMAL**: pads → notes + poly-aftertouch. Only plays when foreground. Velocity = baseVelocity ± random(velocityVariation), per-bank. Pitch bend offset per-bank.
- **ARPEG**: arpeggiator. No aftertouch. Velocity = baseVelocity ± variation, per-bank. Max 4 ARPEG banks.

### 3 Mapping Layers

1. **Pad Ordering** `padOrder[48]`: physical pad → rank 0-47 (low to high). Set once in Tool 2. All banks share it.
2. **Scale Config** per bank: chromatic or scale (root + mode). Root = base note in both modes. Runtime-switchable via left button hold.
3. **Control Pad Assignment**: 25 pads with control roles (bank 8 + scale 15 + arp 2). One role per pad. Set in Tool 3.

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
3. BankManager.update()                       ← left button
4. ScaleManager.update()                      ← left button (same as bank)
5. Play/Stop pad (ARPEG only, always active)
6. processNormalMode() or processArpMode()
7. ArpScheduler.tick()                        ← all background arps
8. MidiEngine.flush()                         ← CRITICAL PATH END
9. PotRouter.update()                         ← SECONDARY (5 pots)
10. BatteryMonitor.update()
11. LedController.update()
12. NvsManager.notifyIfDirty()                ← non-blocking signal to NVS task
13. vTaskDelay(1)
```

## Buttons

- **Left (hold + pad)**: single-layer control. 8 bank pads + 15 scale pads (7 root + 7 mode + 1 chromatic) + 1 HOLD toggle (ARPEG only). All visible in one hold. BankManager and ScaleManager share this button — no conflict (pad roles are distinct, checked by Tool 3). All notes off for NORMAL on bank switch. ARPEG: nothing (arp lives/dies by its own logic). Scale change on ARPEG: no allNotesOff — arp re-resolves at next tick.
- **Left (hold + pot)**: modifier for 4 right pots (2nd slot: division, AT deadzone/swing, pitch bend/octave, velocity variation).
- **Play/Stop pad**: ARPEG only — always active (with or without left button held). Toggles arp transport. On NORMAL banks, this pad is a regular music pad (plays a note).
- **Rear (press)**: battery gauge (3s).
- **Rear (hold 3s at boot)**: setup mode.
- **Rear (hold + pot rear)**: modifier for rear pot (pad sensitivity).

## Arpeggiator

**Pile stores padOrder positions, NOT MIDI notes.** Resolution happens at each tick via current ScaleConfig. Changing scale on a background arp = immediate effect at next tick, no interruption.

**HOLD OFF (live)**: press=add position, release=remove. All fingers up = arp stops naturally. BankManager has NO arp stop logic.

**HOLD ON (persistent)**: press=add, double-tap(<300ms)=remove. Play/stop pad controls transport (restart from beginning). Bank switch = arp continues in background.

5 patterns (Up/Down/UpDown/Random/Order) via pot right 3, 1-4 octaves via hold+pot right 3, up to 48 positions (192 steps with 4 oct). Division via hold+pot right 1 (9 binary values: 4/1→1/64). Gate/swing/velocity per-bank via pots.

## Pots — PotRouter

PotRouter reads 5 ADCs (4 right + 1 rear), resolves button combos, applies catch system, exposes getters. Declarative binding table — add a param = add a line.

**Button modifiers**: Left button = modifier for 4 right pots. Rear button = modifier for rear pot only. They never cross.

**Pot values**: NORMAL params (shape, slew, AT deadzone) = **GLOBAL**. ARPEG params (gate, swing, division, pattern, octave) = **PER BANK**. Velocity params (base, variation) = **PER BANK** (NORMAL + ARPEG). Pitch bend offset = **PER BANK** (NORMAL only). Tempo, LED brightness, pad sensitivity = **GLOBAL**. Catch resets on bank switch for per-bank params.

| Pot | NORMAL alone | NORMAL + hold left | ARPEG alone | ARPEG + hold left |
|---|---|---|---|---|
| Right 1 | Tempo (10-260 BPM) | — empty — | Tempo (10-260 BPM) | Division (9 binary) |
| Right 2 | Response shape | AT deadzone | Gate length | Swing |
| Right 3 | Slew rate | Pitch bend (per-bank) | Pattern (5 discrete) | Octave (1-4) |
| Right 4 | Base velocity | Velocity variation | Base velocity | Velocity variation |
| Rear | LED brightness | — | LED brightness | — |

| Rear pot | Alone | + hold rear |
|---|---|---|
| Rear | LED brightness | Pad sensitivity |

## MIDI Clock

ClockManager receives 0xF8/0xFA/0xFC via USB+BLE. Priority: USB > BLE > last known > internal (pot right 1).
**PLL** smooths BLE jitter (±15ms → ±1-2ms). ArpEngines sync to smoothed clock.

## Setup Mode (hold rear button 3s at boot)

VT100 terminal, serial input + button = ENTER.

```
[1] Pressure Calibration  — unchanged from V1
[2] Pad Ordering           — touch low→high, positions 1-48, no base note
[3] Pad Roles              — bank(8) + scale(15) + arp(2), color grid, collision check
[4] Bank Config            — NORMAL/ARPEG per bank (max 4 ARPEG)
[5] Settings               — profile, AT rate, BLE interval
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
| `illpad_set` | settings (profile, AT rate, BLE interval) |
| `illpad_pot` | global pot params (shape, slew, AT deadzone) |
| `illpad_tempo` | tempo BPM (global) |
| `illpad_btype` | bank types[8] (NORMAL/ARPEG) |
| `illpad_scale` | scale config per bank (chromatic, root, mode) |
| `illpad_spad` | scale pads (7 root + 7 mode + 1 chrom) |
| `illpad_apad` | arp pads (1 hold + 1 play/stop) |
| `illpad_apot` | arp pot params per bank (gate, swing, div, pattern, octave) |
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
- `#if !PRODUCTION_MODE` for debug output
- No `new`/`delete` at runtime — static instantiation only
- Unsigned arithmetic for `millis()` (overflow-safe)
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
