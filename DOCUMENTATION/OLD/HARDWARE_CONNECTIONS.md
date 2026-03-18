# ILLPAD 48 — Hardware Connections

Pinout image: `DOCUMENTATION/PINIOUT_IMAGE.png`

---

## What's Inside the Box

| # | Component | Quantity | Role |
|---|-----------|----------|------|
| 1 | ESP32-S3-N8R16 dev board | 1 | Brain (runs all code) |
| 2 | Adafruit BQ25185 charger board | 1 | Charges battery, provides 5V power |
| 3 | LiPo battery (3.7V) | 1 | Power when unplugged from USB |
| 4 | MPR121 touch sensor breakout | 4 | Reads the 48 conductive pads |
| 5 | Standard LED (3mm or 5mm) | 8 | Status display |
| 6 | Momentary push button | 2 | Bank select + battery/calibration |
| 7 | 10kΩ linear potentiometer | 1 | Aftertouch response curve |
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
| Potentiometer | One outer leg |

### GND Bus

All grounds connect together: charger G pad, ESP32 GND, all sensor GND pins, all LED resistors, all button legs, pot leg, voltage divider, CC resistors.

---

## USB-C Passthrough Cable (6 wires)

One USB-C socket is exposed on the enclosure. A 6-wire cable connects it to the inside. This single port handles **charging**, **USB MIDI**, and **Serial debug** at the same time.

| Wire | USB Signal | Goes to | Why |
|------|-----------|---------|-----|
| **Red** | VBUS (+5V) | Charger board **VU** pad | Brings USB power to the charger |
| **Black** | GND | Charger board **G** pad (and the shared GND bus) | Common ground |
| **White** | D− (data minus) | ESP32 **GPIO19** | USB data line |
| **Green** | D+ (data plus) | ESP32 **GPIO20** | USB data line |
| **Blue** | CC1 | 5.1kΩ resistor, then to **GND** | Tells the computer "a USB device is here" |
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
| **1** | Input (analog) | Potentiometer wiper | ADC1_CH0 — reads pot position |
| **2** | Input (digital) | Bank button → GND | Active LOW, internal pull-up enabled |
| **3** | Input (digital) | Battery/Calibration button → GND | Active LOW, internal pull-up enabled |
| **4** | Output | LED 1 → 220Ω → GND | Status LED |
| **5** | Output | LED 2 → 220Ω → GND | Status LED |
| **6** | Output | LED 3 → 220Ω → GND | Status LED |
| **7** | Output | LED 4 → 220Ω → GND | Status LED |
| **8** | Bidirectional | I2C SDA line (to 4× MPR121) | Needs 4.7kΩ pull-up to 3.3V |
| **9** | Output | I2C SCL line (to 4× MPR121) | Needs 4.7kΩ pull-up to 3.3V |
| **10** | Input (analog) | Battery voltage divider output | ADC1_CH9 — reads battery voltage |
| **15** | Output | LED 5 → 220Ω → GND | Status LED |
| **16** | Output | LED 6 → 220Ω → GND | Status LED |
| **17** | Output | LED 7 → 220Ω → GND | Status LED |
| **18** | Output | LED 8 → 220Ω → GND | Status LED |
| **19** | Bidirectional | USB D− (White wire) | Native USB — do not use for anything else |
| **20** | Bidirectional | USB D+ (Green wire) | Native USB — do not use for anything else |
| **48** | Output | Onboard RGB LED (WS2812) | Built into the dev board, active on v1.0; v1.1 uses GPIO38 |

Power pins (not GPIO):

| Pin | Connects to |
|-----|-------------|
| **5V** | Charger 5V OUT (+) |
| **GND** | Charger 5V OUT (−) and shared GND bus |
| **3V3** | 3.3V bus (sensors, pull-ups, pot) |

### GPIOs You Must NOT Use

These pins are taken by hardware on the dev board. Do not connect anything to them.

| GPIO | Why |
|------|-----|
| 0 | BOOT button (strapping pin — used during flashing) |
| 35, 36, 37 | Connected to PSRAM chip (PSRAM is enabled in this project) |

### GPIOs Available But Unused

| GPIO | Notes |
|------|-------|
| 43, 44 | UART0 TX/RX — free because Serial uses native USB, not UART |

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
| ELE0–ELE11 | 12 conductive pads (copper tape, foil, etc.) |

### I2C Pull-Up Resistors (×2)

The SDA and SCL lines each need one 4.7kΩ pull-up resistor. Skip these if your MPR121 breakout boards already have pull-ups built in (check the board's documentation).

```
ESP32 3V3 pin ──┤4.7kΩ├──► SDA line (GPIO8)
ESP32 3V3 pin ──┤4.7kΩ├──► SCL line (GPIO9)
```

I2C bus speed: 400 kHz.

---

### 2. LEDs (×8)

Each LED is wired the same way. The ESP32 GPIO pin drives the LED directly (3.3V output when on, 0V when off).

**Wiring path for each LED:**

```
ESP32 GPIO pin ──► LED long leg (+) ── LED ── LED short leg (-) ──► 220Ω resistor ──► GND
```

- The **long leg** of the LED is the + side (anode) — it goes toward the GPIO pin
- The **short leg** of the LED is the - side (cathode) — it goes toward the resistor
- Use 220Ω to 330Ω resistors (220Ω = brighter, 330Ω = dimmer, both are safe)
- Any standard 3mm or 5mm LED works (any color you like)

| LED # | GPIO | Header side |
|-------|------|-------------|
| 1 | 4 | Left |
| 2 | 5 | Left |
| 3 | 6 | Left |
| 4 | 7 | Left |
| 5 | 15 | Left |
| 6 | 16 | Left |
| 7 | 17 | Left |
| 8 | 18 | Left |

#### What the LEDs Display

The 8 LEDs have **four display modes**. Only one mode is active at a time.

**Boot — Startup diagnostic (progressive fill):**

During startup, LEDs light up one by one as each subsystem initializes:

| LED | Boot step |
|-----|-----------|
| 1 | I2C bus initialized |
| 2 | LED subsystem ready |
| 3 | GPIO buttons + pot configured |
| 4 | Calibration detection window passed |
| 5 | Keyboard (MPR121 sensors) initialized |
| 6 | MIDI transport (USB + BLE) ready |
| 7 | FreeRTOS mutex created |
| 8 | Sensing task launched |

If a step fails, LEDs 1 through (step-1) stay solid and the failing LED blinks rapidly. The device halts.

**Normal — Bank indicator (default at runtime):**

A single LED shows the current bank (MIDI channel). All other LEDs are off.

Example: Bank 3 selected → only LED 3 is on.

If battery drops below 20%, the active bank LED pulses (~1 Hz) instead of being solid.

**Battery gauge (after pressing battery button):**

All 8 LEDs become a bar graph showing battery level. Lasts 3 seconds, then returns to normal mode.

| Battery level | LEDs lit |
|---------------|----------|
| 100% | all 8 |
| 75% | 6 |
| 50% | 4 |
| 25% | 2 |
| 0% | none |

**Error — All LEDs blink in unison (~1 Hz).**

---

### 3. Onboard RGB LED (built into the ESP32-S3 dev board)

This is the small addressable LED (WS2812) soldered onto the dev board itself. No external wiring needed.

- **GPIO48** (DevKitC-1 v1.0) or **GPIO38** (v1.1) — set `RGB_LED_PIN` in `HardwareConfig.h`
- On v1.1 boards, the 0Ω resistor R24 must be populated to enable this LED
- Can be disabled at compile time: set `#define INT_LED 0` in `HardwareConfig.h`

| Color | Meaning |
|-------|---------|
| Blue (solid) | Boot in progress |
| Red (solid) | Boot failure (halted) |
| Orange (pulsing) | Calibration mode active |
| Green (solid) | Running normally |
| Green/Yellow/Red (during battery gauge) | Battery level (green > 50%, yellow 20–50%, red < 20%) |
| Red (blinking ~1 Hz) | Fatal error |

---

### 4. Bank Button (GPIO2)

A simple momentary push button. One leg goes to GPIO2, the other leg goes to GND. Nothing else needed — the ESP32 enables an internal pull-up resistor in software.

```
ESP32 GPIO2 ──► button leg 1 ── [BUTTON] ── button leg 2 ──► GND
```

- During normal play: hold button + press pad 0–7 to select bank 1–8 (= MIDI channel 1–8)
- The selected bank is saved to NVS and restored on reboot
- No other function

---

### 5. Battery / Calibration Button (GPIO3)

Same wiring as the bank button, but on GPIO3.

```
ESP32 GPIO3 ──► button leg 1 ── [BUTTON] ── button leg 2 ──► GND
```

- At boot: press within 3 seconds, then hold for 3 more seconds → enters calibration mode
- During calibration: short press to validate each key measurement
- During normal play: press once → reads battery voltage, shows battery level on LEDs for 3 seconds
- GPIO3 is a JTAG strapping pin but only affects JTAG source selection (unused). The 500ms Serial init delay ensures the strapping window has passed before it is read

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

### 7. Potentiometer

A 10kΩ linear pot controls the aftertouch response curve at runtime.

```
ESP32 3V3 pin ──► pot outer leg 1
                  pot wiper (middle leg) ──► ESP32 GPIO1
           GND ──► pot outer leg 2
```

- Turning the pot sweeps GPIO1 from 0V to 3.3V
- The software reads this as a value from 0.0 to 1.0

---

### 8. CC Resistors (×2)

Two 5.1kΩ resistors tell the USB host that a USB device is connected. They go between the CC wires from the USB-C passthrough and GND.

```
Blue wire (CC1) ──┤5.1kΩ├──► GND
Yellow wire (CC2) ──┤5.1kΩ├──► GND
```

These are mandatory for USB-C to work.

---

## Current Budget

| Component | Typical current |
|-----------|----------------|
| ESP32-S3 (active, WiFi/BLE on) | ~150 mA |
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
        │  │ BAT ─────┼──►🔋│     │  │  │  │  │ GPIO20 ◄─────┼──┼─ Green (D+)
        │  │   │      │      │     │  │  │  │  │              │  │
        │  └───┼──────┘      │     │  │  │  │  │ GPIO8 (SDA) ─┼──┼─► MPR121 ×4
        │      │             │     │  │  │  │  │ GPIO9 (SCL) ─┼──┼─► MPR121 ×4
        │      ├─ 100kΩ ─┬───┼─────┼──┼──┼──┼─►│ GPIO10 (ADC)│  │
        │      │       100kΩ │     │  │  │  │  │              │  │
        │      │         │   │     │  │  │  │  │ GPIO4  ──────┼──┼─► LED 1
        │      │        GND  │     │  │  │  │  │ GPIO5  ──────┼──┼─► LED 2
        │      │             │     │  │  │  │  │ GPIO6  ──────┼──┼─► LED 3
        │      │             │     │  │  │  │  │ GPIO7  ──────┼──┼─► LED 4
        │      │             │     │  │  │  │  │ GPIO15 ──────┼──┼─► LED 5
        │      │             │     │  │  │  │  │ GPIO16 ──────┼──┼─► LED 6
        │      │             │     │  │  │  │  │ GPIO17 ──────┼──┼─► LED 7
        │      │             │     │  │  │  │  │ GPIO18 ──────┼──┼─► LED 8
        │      │             │     │  │  │  │  │              │  │
        │      │             │     │  │  │  │  │ GPIO2  ◄─────┼──┼─ Bank button ─► GND
        │      │             │     │  │  │  │  │ GPIO3  ◄─────┼──┼─ Battery/Cal button ─► GND
        │      │             │     │  │  │  │  │ GPIO1  ◄─────┼──┼─ Pot wiper
        │      │             │     │  │  │  │  │              │  │
        │      │             │     │  │  │  │  └──────────────┘  │
        │      │             │     │  │  │  │                    │
        │    Blue ──┤5.1kΩ├──► GND │  │  │  │                    │
        │   Yellow ─┤5.1kΩ├──► GND │  │  │  │                    │
        │                          │  │  │  │                    │
        └──────────────────────────┼──┼──┼──┼────────────────────┘
                                                ENCLOSURE
```
