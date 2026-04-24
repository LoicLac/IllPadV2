# ILLPAD48 V2 — Hardware Connections

Full wiring reference for the enclosure : components, power buses, GPIO
pin map, component-by-component wiring, MCP3208 ADC details. Read this
when building/repairing hardware or touching pin assignments in
`HardwareConfig.h`.

**Source of truth in code** : `src/core/HardwareConfig.h`.

---

## 1. What's inside the box

| # | Component | Qty | Role |
|---|---|---|---|
| 1 | ESP32-S3-N8R16 dev board | 1 | Brain (runs all code) |
| 2 | Adafruit BQ25185 charger board | 1 | Charges battery, provides 5 V power |
| 3 | LiPo battery (3.7 V) | 1 | Power when unplugged |
| 4 | MPR121 touch sensor breakout | 4 | Reads the 48 conductive pads |
| 5 | **MCP3208 external ADC (DIP-16)** | 1 | Reads all 5 potentiometers via SPI |
| 6 | SK6812 RGBW NeoPixel Stick (8 LEDs) | 1 | Status display |
| 7 | Momentary push button | 2 | Left + Rear |
| 8 | 10 kΩ linear potentiometer | 5 | 4 right (musical params) + 1 rear (brightness/sensitivity) |
| 9 | USB-C passthrough socket | 1 | Single port on the enclosure |
| 10 | Resistors, caps (various) | several | See wiring details |

---

## 2. Power buses

### 5 V bus

Source : charger board **5V OUT** pad (from USB when plugged, or battery
when unplugged).

| Consumer | Connects to |
|---|---|
| ESP32-S3 | **5V** pin on the dev board |

Only the ESP32 board takes 5 V. Its onboard regulator produces 3.3 V for
the rest.

### 3.3 V bus

Source : ESP32-S3's **3V3** pin.

| Consumer | Connects to |
|---|---|
| 4× MPR121 sensors | VIN on each breakout |
| 2× I2C pull-ups (4.7 kΩ each) | one end of each resistor |
| **MCP3208** | VDD + VREF (tied together) |
| 5× Potentiometers | CCW outer leg |

### GND bus

All grounds common : charger G, ESP32 GND, sensor GNDs, MCP3208
AGND+DGND, LED GND, pot CW legs, voltage divider, CC resistors.

---

## 3. USB-C passthrough cable (6 wires)

One USB-C socket exposed on the enclosure. A 6-wire cable carries
charge, USB MIDI, and serial debug over this single port.

| Wire | USB signal | Goes to | Why |
|---|---|---|---|
| Red | VBUS (+5 V) | Charger **VU** pad | USB power → charger |
| Black | GND | Charger **G** pad (and GND bus) | Common ground |
| White | D− | ESP32 **GPIO 19** | USB data line |
| Blue | D+ | ESP32 **GPIO 20** | USB data line |
| Green | CC1 | 5.1 kΩ → GND | Tells host "device is here" |
| Yellow | CC2 | 5.1 kΩ → GND | Same, both plug orientations |

Both 5.1 kΩ resistors on CC1/CC2 are mandatory (USB-C host detection).

**Never connect the Red wire directly to the ESP32 5 V pin.** It must go
through the charger's VU pad (charger outputs regulated 5 V to ESP32
5 V).

---

## 4. Charger — Adafruit BQ25185 (Product 6106)

Sits between USB power and the ESP32. Charges battery and provides 5 V.

| Pad | Connects to | Role |
|---|---|---|
| VU | Red wire from USB-C | Receives 5 V from USB |
| G | Black wire + GND bus | Ground |
| BAT (JST) | LiPo (3.7 V) | Charge + discharge |
| 5V OUT (+) | ESP32 5V pin | Power ESP32 |
| 5V OUT (−) | ESP32 GND pin | Ground |

Notes :
- Charger's own USB-C is **unused** — power comes from the passthrough
  cable to VU.
- Default charge rate 1 A ; cut rear jumper for 500 mA.
- USB plugged → ESP32 runs from USB, battery charges.
- USB unplugged → ESP32 runs from battery automatically.

---

## 5. ESP32-S3 — complete pin map

Every GPIO used in this project, matched to `HardwareConfig.h`.

| GPIO | Direction | Connects to | Notes |
|---|---|---|---|
| **1** | — | *(unused, formerly pot rear ADC)* | Freed by MCP3208 migration |
| **4** | — | *(unused, formerly pot R1 ADC)* | Freed by MCP3208 migration |
| **5** | — | *(unused, formerly pot R2 ADC)* | Freed |
| **6** | — | *(unused, formerly pot R3 ADC)* | Freed |
| **7** | — | *(unused, formerly pot R4 ADC)* | Freed |
| **8** | Bidirectional | I2C SDA (4× MPR121) | 4.7 kΩ pull-up to 3.3 V |
| **9** | Output | I2C SCL (4× MPR121) | 4.7 kΩ pull-up to 3.3 V |
| **10** | Input (analog) | Battery voltage divider | ADC1_CH9 — battery SOC |
| **12** | Input (digital) | Left button → GND | Active LOW, internal pull-up |
| **13** | Output | NeoPixel DIN (SK6812 ×8) | GRBW wire order |
| **15** | Output | MCP3208 DIN (MOSI) | SPI |
| **16** | Input | MCP3208 DOUT (MISO) | SPI |
| **17** | Output | MCP3208 /CS | SPI (active LOW) |
| **18** | Output | MCP3208 SCK | SPI clock |
| **19** | Bidirectional | USB D− (White) | Native USB — do not repurpose |
| **20** | Bidirectional | USB D+ (Blue) | Native USB — do not repurpose |
| **21** | Input (digital) | Rear button → GND | Active LOW, internal pull-up |

Power pins :

| Pin | Connects to |
|---|---|
| 5V | Charger 5V OUT (+) |
| GND | Charger 5V OUT (−) and shared GND bus |
| 3V3 | 3.3 V bus (sensors, pull-ups, MCP3208, pots) |

### GPIOs you must NOT use

| GPIO | Why |
|---|---|
| 0 | BOOT button (strapping pin — flashing) |
| 19, 20 | Native USB (TinyUSB MIDI + CDC + upload) |
| 26–32 | Internal flash (QIO) |
| 33–37 | PSRAM (OPI) |
| 43, 44 | UART bridge hardware (unused but reserved) |
| 45, 46 | Strapping pins (VDD_SPI / boot mode) |

### GPIOs available but unused

GPIO 1, 2, 3, 4, 5, 6, 7 (ex-pots — 5 freed by MCP3208 migration),
GPIO 11, 14, 38–42, 47, 48.

---

## 6. MPR121 touch sensors (×4)

All four share the I2C bus (GPIO 8 SDA, GPIO 9 SCL). Each has a
different address set by its ADDR pin.

| Sensor | I2C address | ADDR pin wired to | Pads |
|---|---|---|---|
| A | 0x5A | GND | 0–11 |
| B | 0x5B | 3.3 V (VCC) | 12–23 |
| C | 0x5C | SDA line (GPIO 8) | 24–35 |
| D | 0x5D | SCL line (GPIO 9) | 36–47 |

Per-breakout connections :

| MPR121 pin | Connects to |
|---|---|
| VIN | ESP32 3V3 |
| GND | GND bus |
| SDA | GPIO 8 |
| SCL | GPIO 9 |
| ADDR | Per table above |
| ELE0–ELE11 | 12 conductive aluminium pads |

I2C bus speed : 400 kHz.

### I2C pull-ups (×2)

```
ESP32 3V3 ──┤4.7 kΩ├──► SDA (GPIO 8)
ESP32 3V3 ──┤4.7 kΩ├──► SCL (GPIO 9)
```

Skip these if your MPR121 breakouts already have pull-ups built in
(Adafruit boards do).

---

## 7. LEDs — SK6812 RGBW NeoPixel Stick (×8)

Single data wire :

```
ESP32 GPIO 13 ──► NeoPixel DIN
ESP32 3V3     ──► NeoPixel VCC (or 5V if needed)
          GND ──► NeoPixel GND
```

- Wire order : NEO_GRBW (configured in `HardwareConfig.h`).
- No DIN resistor (short wire).
- All 8 LEDs on the stick, no per-LED GPIO.

Visual behavior : see [`led-reference.md`](led-reference.md) and
[`boot-sequence.md`](boot-sequence.md).

---

## 8. MCP3208 external ADC — pot readout

Introduced because the ESP32's internal ADC2 is blocked by the BLE
radio, and ADC1's shared pin usage caused readback glitches on the
original wiring. MCP3208 moves the analog chain off-chip : clean 12-bit
reads, deterministic SPI timing.

### Pinout (chip, notch left)

```
                   notch
                 ┌────╨────┐
  POT_RIGHT1 CH0│ 1    16 │VDD  ── 3.3 V
  POT_RIGHT2 CH1│ 2    15 │VREF ── 3.3 V
  POT_RIGHT3 CH2│ 3    14 │AGND ── GND
  POT_RIGHT4 CH3│ 4    13 │CLK  ── GPIO 18
    POT_REAR CH4│ 5    12 │DOUT ── GPIO 16
       spare CH5│ 6    11 │DIN  ── GPIO 15
       spare CH6│ 7    10 │/CS  ── GPIO 17
       spare CH7│ 8     9 │DGND ── GND
                 └─────────┘
```

CH5–CH7 unconnected (spare for future sensors).

### SPI connections

| Chip pin | Signal | ESP32 GPIO |
|---|---|---|
| 13 | CLK | 18 |
| 12 | DOUT | 16 (MISO) |
| 11 | DIN | 15 (MOSI) |
| 10 | /CS | 17 |

### Power

| Chip pin | Net | Notes |
|---|---|---|
| 16 (VDD) | 3.3 V | 100 nF decoupling to GND, close to chip |
| 15 (VREF) | 3.3 V | 100 nF + 10 µF to GND, close to chip |
| 14 (AGND) | GND | Same star point as DGND |
| 9 (DGND) | GND | Same star point as AGND |

### RC filter per channel

Between each pot wiper and the MCP3208 channel input :

```
Wiper ──[100 Ω]──┬── CHx
                 │
            [100 nF]
                 │
                GND
```

Low-pass at ~16 kHz. Bounds source impedance to ~100 Ω
(MCP3208 wants < 1 kΩ @ 1 MHz for full precision). Blocks RF
interference.

### Pot → channel mapping

| Pot | MCP3208 channel | Function (default) |
|---|---|---|
| Right 1 | CH0 | Tempo |
| Right 2 | CH1 | Shape / Gate |
| Right 3 | CH2 | Slew / Shuffle depth |
| Right 4 | CH3 | Base velocity |
| Rear | CH4 | LED brightness |

All pots are 10 kΩ linear, 3-pin wiring :

```
3.3 V ─── CCW outer leg
  GND ─── CW outer leg
  CHx ─── wiper (middle leg)
```

If rotation feels inverted, swap CCW ↔ CW at the wiring (not in code).
The firmware inverts the reading in software by default (`readPotRaw` :
`4095 - v`) so CW matches user-up expectation.

### Consumption

| Part | Typ current |
|---|---|
| MCP3208 | ~175 µA |

Negligible vs ESP32 (~150 mA).

### Details & filter pipeline

Pot filter behavior (no EMA, no oversampling — deadband + edge snap +
sleep/wake for rear pot only), boot sequence (median-of-5 seed, discard
first 2 conversions), per-pot tuning : see
[`pot-reference.md`](pot-reference.md).

---

## 9. Buttons (×2)

Simple momentary pushes, one leg to GPIO, other leg to GND. ESP32
enables internal pull-up ; no external resistors.

```
ESP32 GPIO 12 ──► [LEFT BUTTON] ──► GND
ESP32 GPIO 21 ──► [REAR BUTTON] ──► GND
```

### Left button functions

- **Hold + pad** : single-layer control (bank 8, scale 15 roots+modes+chrom,
  arp 5 incl. hold + 4 octave). All visible at once.
- **Hold + right pot** : modifier layer for 4 right pots (R1–R4).

### Rear button functions

- **Press** : reads battery voltage, shows gauge on LEDs for 3 s.
- **Hold 3 s at boot** (two-phase : press within 3 s of boot, then hold
  3 s) : enters setup mode.
- **Hold + rear pot** : modifier for rear pot (→ pad sensitivity
  instead of LED brightness).

### Tunables

`CAL_WAIT_WINDOW_MS = 3000`, `CAL_HOLD_DURATION_MS = 3000` in
`HardwareConfig.h`.

---

## 10. Battery voltage divider

LiPo voltage (3.0–4.2 V) is too high for ESP32 ADC. Two 100 kΩ in series
divide by 2.

```
Charger BAT pad ──┤100 kΩ├──┬── GPIO 10
                            │
                       100 kΩ
                            │
                           GND
```

- Input 3.0–4.2 V → Output 1.5–2.1 V (safe for ADC1_CH9).
- Total 200 kΩ → ~21 µA drain from battery (negligible).
- "Charger BAT pad" = battery terminal on the charger board.

Calibration constants : `BAT_VOLTAGE_FULL = 4.2 V`, `BAT_VOLTAGE_EMPTY =
3.3 V`, `BAT_LOW_THRESHOLD_PCT = 20 %` — all in `HardwareConfig.h`.

---

## 11. Current budget

| Component | Typical |
|---|---|
| ESP32-S3 (active, BLE on) | ~150 mA |
| 4× MPR121 | ~4 mA |
| MCP3208 | ~0.2 mA |
| 8× LEDs (worst case) | ~80 mA |
| **Total** | **~240 mA** |

Charger 5V OUT supports up to 1 A → comfortable headroom.

---

## 12. Complete wiring diagram

```
                          ┌─────────────────┐
                          │   USB-C SOCKET   │  (the only port on the enclosure)
                          └──┬──┬──┬──┬──┬──┘
                             │  │  │  │  │  │
                   Red ──────┘  │  │  │  │  └── Yellow
                   Black ───────┘  │  │  └───── Blue
                   White ──────────┘  └──────── Green
                             │  │  │  │  │  │
        ┌────────────────────┼──┼──┼──┼──┼──┼───────────────────┐
        │                    │  │  │  │  │  │                    │
        │  ┌─────────┐      │  │  │  │  │  │  ┌──────────────┐  │
        │  │ CHARGER  │      │  │  │  │  │  │  │  ESP32-S3    │  │
        │  │ BQ25185  │      │  │  │  │  │  │  │              │  │
        │  │ VU ◄─────┼─ Red─┘  │  │  │  │  │  │ 5V ◄────────┼──┤
        │  │ G  ◄─────┼─ Black──┘  │  │  │  │  │ GND ◄───────┼──┤
        │  │ 5V OUT+ ─┼──────► ESP32 5V               │ 3V3 ────────┼──► 3.3V bus
        │  │ 5V OUT- ─┼──────► ESP32 GND              │              │
        │  │ BAT ─────┼──► 🔋                          │ GPIO19 ◄─────┼── White (D−)
        │  │   │      │                                │ GPIO20 ◄─────┼── Blue (D+)
        │  └───┼──────┘                                │              │
        │      ├─ 100 kΩ ─┬───► GPIO10                 │ GPIO8  (SDA) ┼──► 4× MPR121
        │      │       100 kΩ                          │ GPIO9  (SCL) ┼──► 4× MPR121
        │      │          │                            │              │
        │      │         GND                           │ GPIO13       ┼──► NeoPixel stick DIN
        │      │                                       │              │
        │      │                                       │ GPIO12   ◄───┼── LEFT button → GND
        │      │                                       │ GPIO21   ◄───┼── REAR button → GND
        │      │                                       │              │
        │      │                                       │ GPIO15 (MOSI)┼──► MCP3208 DIN
        │      │                                       │ GPIO16 (MISO)┼──◄ MCP3208 DOUT
        │      │                                       │ GPIO17 (/CS) ┼──► MCP3208 /CS
        │      │                                       │ GPIO18 (SCK) ┼──► MCP3208 CLK
        │      │                                       │              │
        │      │                         ┌─────────────┴──────────────┘
        │      │                         │                    MCP3208
        │      │                         │         (5 pots → CH0-CH4 via RC filters)
        │      │                         │
        │    Green ──┤5.1 kΩ├──► GND     │
        │   Yellow ──┤5.1 kΩ├──► GND     │
        │                                │
        └────────────────────────────────┘
                              ENCLOSURE
```
