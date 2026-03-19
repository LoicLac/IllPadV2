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
- **3 Buttons**: left (bank select), right (scale/arp controls), rear (battery/setup). GPIOs TBD.
- **3 Pots**: left (feel/sound), right (notes/rhythm), rear (config/tempo). GPIOs TBD.
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
BankSlot { channel, type(NORMAL|ARPEG), scaleConfig, arpEngine*, isForeground, lastResolvedNote[48] }
```

- **NORMAL**: pads → notes + poly-aftertouch. Only plays when foreground.
- **ARPEG**: arpeggiator. No aftertouch. Fixed velocity with variation. Max 4 ARPEG banks.

### 3 Mapping Layers

1. **Pad Ordering** `padOrder[48]`: physical pad → rank 0-47 (low to high). Set once in Tool 2. All banks share it.
2. **Scale Config** per bank: chromatic or scale (root + mode). Root = base note in both modes. Runtime-switchable via right button.
3. **Control Pad Assignment**: 31 pads with control roles (bank 8 + scale 15 + arp 8). One role per pad. Set in Tool 3.

### Note Resolution

`ScaleResolver::resolve(padIndex, padOrder, scaleConfig)` → MIDI note or 0xFF.
- Chromatic: `rootBase + padOrder[padIndex]`
- Scale: `rootBase + (order/7)*12 + scaleIntervals[mode][order%7]`

**Critical**: `lastResolvedNote[padIndex]` stores the note sent at noteOn. noteOff ALWAYS uses this stored value, never re-resolves. Prevents orphan notes on scale change during play.

### Loop Execution Order (Core 1)

Critical path first, secondary after. MIDI latency depends on this order.

```
1. Read double buffer (instant)              ← CRITICAL PATH START
2. Read buttons
3. BankManager.update()
4. ScaleManager.update()
5. processNormalMode() or processArpMode()
6. ArpScheduler.tick()                        ← all background arps
7. MidiEngine.flush()                         ← CRITICAL PATH END
8. PotRouter.update()                         ← SECONDARY
9. BatteryMonitor.update()
10. LedController.update()
11. NvsManager.notifyIfDirty()                ← non-blocking signal to NVS task
12. vTaskDelay(1)
```

## Buttons

- **Left (hold + pad)**: bank select. 8 pads. All notes off for NORMAL on switch. ARPEG: nothing (arp lives/dies by its own logic).
- **Right (hold + pad)**: 15 scale pads (7 root + 7 mode + 1 chromatic) + 7 arp pads on ARPEG banks (5 patterns + 1 octave cycle + 1 HOLD toggle). No MIDI notes during hold.
- **Play/Stop pad**: the ONLY control pad active during normal play. Contextual: note on NORMAL, note on ARPEG HOLD-OFF, play/stop toggle on ARPEG HOLD-ON.
- **Rear**: battery gauge (press), setup mode (hold 3s at boot).

## Arpeggiator

**HOLD OFF (live)**: press=add note, release=remove. All fingers up = arp stops naturally. BankManager has NO arp stop logic.

**HOLD ON (persistent)**: press=add, double-tap(<300ms)=remove. Play/stop pad controls transport (restart from beginning). Bank switch = arp continues in background.

5 patterns (Up/Down/UpDown/Random/Order), 1-4 octaves, up to 48 notes (192 steps with 4 oct). Division from pot right. Gate/swing/velocity per-bank.

## Pots — PotRouter

PotRouter reads 3 ADCs, resolves button combos, applies catch system, exposes getters. Declarative binding table — add a param = add a line.

**Pot values**: NORMAL params (shape, slew, AT deadzone) = **GLOBAL**. ARPEG params (gate, swing, division, velocity base, velocity variation) = **PER BANK**. Catch resets on bank switch for per-bank params.

| Pot | Alone | + opposite btn | + same-side btn (non-live) |
|---|---|---|---|
| Left NORMAL | Response shape | Slew rate | — |
| Left ARPEG | Gate length | Swing | — |
| Right NORMAL | — | AT deadzone | — |
| Right ARPEG | Division | Velocity variation | Base velocity |
| Rear | Tempo BPM | Pad sensitivity | — |

## MIDI Clock

ClockManager receives 0xF8/0xFA/0xFC via USB+BLE. Priority: USB > BLE > last known > internal (pot rear).
**PLL** smooths BLE jitter (±15ms → ±1-2ms). ArpEngines sync to smoothed clock.

## Setup Mode (hold rear button 3s at boot)

VT100 terminal, serial input + button = ENTER.

```
[1] Pressure Calibration  — unchanged from V1
[2] Pad Ordering           — touch low→high, positions 1-48, no base note
[3] Pad Roles              — unified (bank+scale+arp), color grid, collision check
[4] Bank Config            — NORMAL/ARPEG per bank (max 4 ARPEG)
[5] Settings               — profile, sensitivity, AT rate, deadzone, BLE interval
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
| `illpad_set` | settings (profile, sens, AT rate, deadzone, BLE interval) |
| `illpad_pot` | global pot params (shape, slew, AT deadzone) |
| `illpad_btype` | bank types[8] (NORMAL/ARPEG) |
| `illpad_scale` | scale config per bank (chromatic, root, mode) |
| `illpad_spad` | scale pads (7 root + 7 mode + 1 chrom) |
| `illpad_apad` | arp pads (5 pattern + 1 oct + 1 hold + 1 play/stop) |
| `illpad_apot` | arp pot params per bank (gate, swing, div, vel base, vel var) |

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
