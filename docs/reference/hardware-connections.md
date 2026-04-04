# ILLPAD48 V2 — Hardware Connections

---

## What's Inside the Box

| # | Component | Quantity | Role |
|---|-----------|----------|------|
| 1 | ESP32-S3-N8R16 dev board | 1 | Brain (runs all code) |
| 2 | Adafruit BQ25185 charger board | 1 | Charges battery, provides 5V power |
| 3 | LiPo battery (3.7V) | 1 | Power when unplugged from USB |
| 4 | MPR121 touch sensor breakout | 4 | Reads the 48 conductive pads |
| 5 | Standard LED (3mm or 5mm) | 8 | Status display (circle layout) |
| 6 | Momentary push button | 2 | Left (bank+scale+arp) + Rear (battery/setup) |
| 7 | 10kΩ linear potentiometer | 5 | 4 right (musical params) + 1 rear (brightness/sensitivity) |
| 8 | USB-C passthrough socket | 1 | Single port on the enclosure |
| 9 | Resistors (various) | several | See wiring details below |

---

## Two Power Buses

There are two voltage levels in this project. Every component uses one or the other.

### 5V Bus

Source: the charger board's **5V OUT** pad (comes from USB when plugged in, or from the battery when unplugged).

| What gets 5V | Where it connects |
|--------------|-------------------|
| ESP32-S3 | **5V** pin on the dev board |

That's it. Only the ESP32 board gets 5V. The ESP32 board has its own built-in voltage regulator that converts 5V down to 3.3V for everything else.

### 3.3V Bus

Source: the ESP32-S3's **3V3** pin (the board's built-in regulator output).

| What gets 3.3V | Where it connects |
|-----------------|-------------------|
| 4× MPR121 sensors | VIN pin on each breakout |
| I2C pull-up resistors (2×) | One end of each 4.7kΩ resistor |
| 5× Potentiometers | One outer leg of each |

### GND Bus

All grounds connect together: charger G pad, ESP32 GND, all sensor GND pins, all LED resistors, all button legs, all pot legs, voltage divider, CC resistors.

---

## USB-C Passthrough Cable (6 wires)

One USB-C socket is exposed on the enclosure. A 6-wire cable connects it to the inside. This single port handles **charging**, **USB MIDI**, and **Serial debug** at the same time.

| Wire | USB Signal | Goes to | Why |
|------|-----------|---------|-----|
| **Red** | VBUS (+5V) | Charger board **VU** pad | Brings USB power to the charger |
| **Black** | GND | Charger board **G** pad (and the shared GND bus) | Common ground |
| **White** | D− (data minus) | ESP32 **GPIO19** | USB data line |
| **blue** | D+ (data plus) | ESP32 **GPIO20** | USB data line |
| **green** | CC1 | 5.1kΩ resistor, then to **GND** | Tells the computer "a USB device is here" |
| **Yellow** | CC2 | 5.1kΩ resistor, then to **GND** | Same (needed for both orientations of USB-C plug) |

The two 5.1kΩ resistors on CC1/CC2 are mandatory. Without them, the computer will not recognize the device.

**Do NOT connect the Red wire directly to the ESP32.** It must go to the charger's VU pad. The charger then outputs regulated 5V to the ESP32.

---

## Charger Board — Adafruit BQ25185 (Product 6106)

The charger sits between USB power and the ESP32. It charges the battery and provides 5V output.

| Charger Pad | Wire / Component | What it does |
|-------------|-----------------|--------------|
| **VU** | Red wire from USB-C passthrough | Receives 5V USB power |
| **G** | Black wire from USB-C + GND bus | Ground |
| **BAT** (JST connector) | LiPo battery (JST plug) | Charges and discharges the battery |
| **5V OUT (+)** | ESP32 **5V** pin | Sends 5V power to the ESP32 |
| **5V OUT (−)** | ESP32 **GND** pin | Ground |

Notes:
- The charger's own USB-C connector is **not used** — power comes from the passthrough cable to VU
- Charge rate: 1A default (can reduce to 500mA by cutting the rear jumper on the board)
- When USB is plugged in: ESP32 runs from USB power, battery charges
- When USB is unplugged: ESP32 runs from battery automatically

---

## ESP32-S3 — Complete Pin Map

Every GPIO pin used in this project, and what connects to it.

| GPIO | Direction | Connects to | Notes |
|------|-----------|-------------|-------|
| **1** | Input (analog) | Pot Rear wiper | ADC1_CH0 — LED brightness / Pad sensitivity |
| **4** | Input (analog) | Pot Right 1 wiper | ADC1_CH3 — Tempo / Division |
| **5** | Input (analog) | Pot Right 2 wiper | ADC1_CH4 — Shape/Gate, Deadzone/Swing |
| **6** | Input (analog) | Pot Right 3 wiper | ADC1_CH5 — Slew/Pattern, PitchBend/Octave |
| **7** | Input (analog) | Pot Right 4 wiper | ADC1_CH6 — Base velocity / Velocity variation |
| **8** | Bidirectional | I2C SDA line (to 4× MPR121) | Needs 4.7kΩ pull-up to 3.3V |
| **9** | Output | I2C SCL line (to 4× MPR121) | Needs 4.7kΩ pull-up to 3.3V |
| **10** | Input (analog) | Battery voltage divider output | ADC1_CH9 — reads battery voltage |
| **12** | Input (digital) | Left button → GND | Active LOW, internal pull-up enabled |
| **13** | Output | NeoPixel data (SK6812 RGBW ×8) | Single data pin, GRBW wire order |
| **19** | Bidirectional | USB D− (White wire) | Native USB — do not use for anything else |
| **20** | Bidirectional | USB D+ (BLUE wire) | Native USB — do not use for anything else |
| **21** | Input (digital) | Rear button → GND | Active LOW, internal pull-up enabled |

All analog inputs use **ADC1 (GPIO 1–10)** for reliable operation with BLE active. ADC2 (GPIO 11–20) is blocked by the BLE radio driver.

Power pins (not GPIO):

| Pin | Connects to |
|-----|-------------|
| **5V** | Charger 5V OUT (+) |
| **GND** | Charger 5V OUT (−) and shared GND bus |
| **3V3** | 3.3V bus (sensors, pull-ups, pots) |

### GPIOs You Must NOT Use

These pins are taken by hardware on the dev board. Do not connect anything to them.

| GPIO | Why |
|------|-----|
| 0 | BOOT button (strapping pin — used during flashing) |
| 19, 20 | Native USB (TinyUSB MIDI + CDC serial + upload) |
| 26–32 | Connected to internal flash (QIO) |
| 33–37 | Connected to PSRAM chip (OPI, enabled in this project) |
| 43, 44 | UART bridge hardware (COM port, unused but reserved) |
| 45, 46 | Strapping pins (VDD_SPI / boot mode) |

### GPIOs Available But Unused

| GPIO | Notes |
|------|-------|
| 2, 3 | Free (formerly buttons, now reassigned) |
| 11, 14–18 | Free digital GPIO |
| 38–42, 47, 48 | Free digital GPIO |

---

## Wiring — Component by Component

### 1. MPR121 Touch Sensors (×4)

All four sensors share the same I2C bus (GPIO8 and GPIO9). Each sensor has a different address set by its ADDR pin.

| Sensor | I2C Address | ADDR pin wired to | Pad keys |
|--------|-------------|-------------------|----------|
| A | 0x5A | GND | 0–11 |
| B | 0x5B | 3.3V (VCC) | 12–23 |
| C | 0x5C | SDA line (GPIO8) | 24–35 |
| D | 0x5D | SCL line (GPIO9) | 36–47 |

Each MPR121 breakout board has these connections:

| MPR121 pin | Connects to |
|------------|-------------|
| VIN | ESP32 **3V3** pin |
| GND | GND bus |
| SDA | ESP32 **GPIO8** |
| SCL | ESP32 **GPIO9** |
| ADDR | See table above (different for each sensor) |
| ELE0–ELE11 | 12 conductive aluminium pads |

### I2C Pull-Up Resistors (×2)

The SDA and SCL lines each need one 4.7kΩ pull-up resistor. Skip these if your MPR121 breakout boards already have pull-ups built in (check the board's documentation).

```
ESP32 3V3 pin ──┤4.7kΩ├──► SDA line (GPIO8)
ESP32 3V3 pin ──┤4.7kΩ├──► SCL line (GPIO9)
```

I2C bus speed: 400 kHz.

---

### 2. LEDs — SK6812 RGBW NeoPixel Stick (×8)

A single Adafruit SK6812 RGBW NeoPixel Stick (8 LEDs). Only one data wire from the ESP32.

**Wiring:**

```
ESP32 GPIO13 ──► NeoPixel DIN (data in)
ESP32 3V3    ──► NeoPixel VCC (or 5V from charger if available)
         GND ──► NeoPixel GND
```

- Wire order: NEO_GRBW (set in software)
- No resistors needed between GPIO and DIN (short wire)
- All 8 LEDs are on the stick, no per-LED GPIO assignment

#### What the LEDs Display

The 8 LEDs have multiple display modes. Only one mode is active at a time (priority-based).

**Normal — Bank indicator (default at runtime):**

A single LED shows the current bank (MIDI channel 1–8). All other LEDs are off. Brightness is controlled by the rear pot.

If battery drops below 20%, the active bank LED does 3 rapid blinks every 3 seconds.

**Pot bargraph (after pot movement):**

LEDs become a solid bar graph showing the current pot value (0–8 LEDs). Lasts 5 seconds after last movement.

**Battery gauge (after pressing rear button):**

All 8 LEDs become a heartbeat-pulsing bar graph showing battery level. Lasts 3 seconds, then returns to normal mode.

| Battery level | LEDs lit |
|---------------|----------|
| 100% | all 8 |
| 75% | 6 |
| 50% | 4 |
| 25% | 2 |
| 0% | none |

**Error — All 8 LEDs blink in unison (~1 Hz).**

---

### 3. Left Button (GPIO 12)

A simple momentary push button. One leg goes to the GPIO, the other leg goes to GND. Nothing else needed — the ESP32 enables an internal pull-up resistor in software.

```
ESP32 GPIO 12 ──► button leg 1 ── [BUTTON] ── button leg 2 ──► GND
```

Functions:
- **Hold + pad**: single-layer control — bank select (8 pads), root note (7 pads), scale mode (7 pads), chromatic toggle (1 pad), HOLD toggle (1 pad, ARPEG only)
- **Hold + right pot**: modifier slot — accesses secondary pot params (division, deadzone/swing, pitch bend/octave, velocity variation)

---

### 4. Rear Button (GPIO 21)

Same wiring as the left button, on a different GPIO.

```
ESP32 GPIO 21 ──► button leg 1 ── [BUTTON] ── button leg 2 ──► GND
```

Functions:
- **Press**: reads battery voltage, shows battery gauge on LEDs for 3 seconds
- **Hold 3s at boot**: enters setup mode (VT100 terminal)
- **Hold + rear pot**: modifier for rear pot (pad sensitivity instead of LED brightness)

---

### 5. Potentiometers (×5)

Five 10kΩ linear pots. All wired identically:

```
ESP32 3V3 pin ──► pot outer leg 1
                  pot wiper (middle leg) ──► ESP32 GPIO (see table)
           GND ──► pot outer leg 2
```

Turning a pot sweeps its GPIO from 0V to 3.3V. The software reads 12-bit ADC values (0–4095).

| Pot | GPIO | ADC | Physical position | Function |
|-----|------|-----|-------------------|----------|
| Right 1 | 4 | ADC1_CH3 | Right side, top | Tempo (alone) / Division (hold left, ARPEG) |
| Right 2 | 5 | ADC1_CH4 | Right side | Shape or Gate (alone) / Deadzone or Swing (hold left) |
| Right 3 | 6 | ADC1_CH5 | Right side | Slew or Pattern (alone) / Pitch bend or Octave (hold left) |
| Right 4 | 7 | ADC1_CH6 | Right side, bottom | Base velocity (alone) / Velocity variation (hold left) |
| Rear | 1 | ADC1_CH0 | Rear | LED brightness (alone) / Pad sensitivity (hold rear) |

The PotRouter software handles:
- **Catch system**: pot must pass through the stored value before taking effect (prevents value jumps on bank switch)
- **Smoothing**: EMA filter on ADC reads to suppress noise
- **Button modifiers**: left button modifies right pots, rear button modifies rear pot

---

### 6. Battery Voltage Divider

The LiPo battery voltage (3.0V–4.2V) is too high for the ESP32 ADC to read safely. A voltage divider made of two 100kΩ resistors cuts the voltage in half.

```
Charger BAT pad ──┤100kΩ├──┬──► ESP32 GPIO10
                            │
                          100kΩ
                            │
                           GND
```

- Input: 3.0V–4.2V from battery → Output: 1.5V–2.1V to GPIO10 (safe for ADC)
- Total resistance: 200kΩ → draws only ~21µA from the battery (negligible)
- The "Charger BAT pad" is the same point as the battery terminal on the charger

---

### 7. CC Resistors (×2)

Two 5.1kΩ resistors tell the USB host that a USB device is connected. They go between the CC wires from the USB-C passthrough and GND.

```
green wire (CC1) ──┤5.1kΩ├──► GND
Yellow wire (CC2) ──┤5.1kΩ├──► GND
```

These are mandatory for USB-C to work.

---

## Current Budget

| Component | Typical current |
|-----------|----------------|
| ESP32-S3 (active, BLE on) | ~150 mA |
| 4× MPR121 sensors | ~4 mA |
| 8× LEDs (worst case, all on) | ~80 mA |
| **Total** | **~240 mA** |

The charger's 5V output supports up to 1A — well within budget.

---

## Complete Wiring Diagram

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
        │  │          │      │  │  │  │  │  │  │              │  │
        │  │ VU ◄─────┼─ Red─┘  │  │  │  │  │  │ 5V ◄────────┼──┤
        │  │ G  ◄─────┼─ Black──┘  │  │  │  │  │ GND ◄───────┼──┤
        │  │          │      │     │  │  │  │  │ 3V3 ────────┼──┼─► 3.3V bus
        │  │ 5V OUT + ┼──────┼─────┼──┼──┼──┼─►│              │  │
        │  │ 5V OUT - ┼──────┼─ GND┘  │  │  │  │              │  │
        │  │          │      │     │  │  │  │  │ GPIO19 ◄─────┼──┼─ White (D-)
        │  │ BAT ─────┼──►🔋│     │  │  │  │  │ GPIO20 ◄─────┼──┼─ blue (D+)
        │  │   │      │      │     │  │  │  │  │              │  │
        │  └───┼──────┘      │     │  │  │  │  │ GPIO8 (SDA) ─┼──┼─► MPR121 ×4
        │      │             │     │  │  │  │  │ GPIO9 (SCL) ─┼──┼─► MPR121 ×4
        │      ├─ 100kΩ ─┬───┼─────┼──┼──┼──┼─►│ GPIO10 (ADC)│  │
        │      │       100kΩ │     │  │  │  │  │              │  │
        │      │         │   │     │  │  │  │  │ GPIO13 ──────┼──┼─► NeoPixel DIN (8× SK6812)
        │      │        GND  │     │  │  │  │  │              │  │
        │      │             │     │  │  │  │  │ GPIO12   ◄───┼──┼─ Left button ─► GND
        │      │             │     │  │  │  │  │ GPIO21   ◄───┼──┼─ Rear button ─► GND
        │      │             │     │  │  │  │  │              │  │
        │      │             │     │  │  │  │  │ GPIO 4   ◄───┼──┼─ Pot Right 1 (ADC1)
        │      │             │     │  │  │  │  │ GPIO 5   ◄───┼──┼─ Pot Right 2 (ADC1)
        │      │             │     │  │  │  │  │ GPIO 6   ◄───┼──┼─ Pot Right 3 (ADC1)
        │      │             │     │  │  │  │  │ GPIO 7   ◄───┼──┼─ Pot Right 4 (ADC1)
        │      │             │     │  │  │  │  │ GPIO 1   ◄───┼──┼─ Pot Rear (ADC1)
        │      │             │     │  │  │  │  │              │  │
        │      │             │     │  │  │  │  └──────────────┘  │
        │      │             │     │  │  │  │                    │
        │    green ──┤5.1kΩ├──► GND │  │  │  │                    │
        │   Yellow ─┤5.1kΩ├──► GND │  │  │  │                    │
        │                          │  │  │  │                    │
        └──────────────────────────┼──┼──┼──┼────────────────────┘
                                                ENCLOSURE
```
