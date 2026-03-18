# MIDI 48-Pad Pressure Controller — Project Specification

## 1. Goal

Build a **48-pad capacitive MIDI controller** with pressure sensitivity, outputting MIDI over both **USB** and **Bluetooth Low Energy (BLE)** simultaneously.

### Target Hardware
- **MCU:** ESP32-S3-N16R8 (generic Ali module, dual USB ports)
- **Sensors:** 4× MPR121 capacitive touch sensors (12 channels each = 48 pads)
- **LEDs:** 8 LEDs (new meanings, see below)
- **Button:** 1 single button (calibration entry + future use)
- **Connectivity:** USB MIDI (native USB OTG) + BLE MIDI (concurrent)
- **PlatformIO board:** `esp32-s3-devkitc-1` with 16MB flash / 8MB PSRAM overrides

### What this device does
- 48 velocity-sensitive pads with **polyphonic aftertouch** (per-note pressure)
- Full **polyphonic** note handling (no mono/priority logic needed)
- Dual MIDI output: USB MIDI device + BLE MIDI peripheral, active at the same time
- 8 LEDs for status feedback (connection state, activity, calibration)
- 1 button: hold at boot → calibration mode
- No CV/gate, no DAC, no modes, no arpeggiator, no potentiometers, no encoder

### MIDI Output Specification
| Event | MIDI Message | Details |
|---|---|---|
| Pad press | Note On (0x90) | Velocity from initial pressure delta |
| Pad release | Note Off (0x80) | Velocity 0 or 64 |
| Continuous pressure | Poly Aftertouch (0xA0) | Per-note, 30–50 Hz update rate, change-only |

- **Note range:** 48 notes starting from a configurable base note (e.g. MIDI 36 = C2)
- **MIDI channel:** Configurable (default: 1)
- **Aftertouch rate:** 30–50 Hz max, only sent when value changes (bandwidth-safe for BLE)

---

## 2. Architecture (New Codebase)

The new project should be a **clean, flat structure** — not a modification of the original Nano R4 project.

```
src/
  main.cpp              — setup() + loop(), wires everything together
  HardwareConfig.h      — Pin definitions, constants (ESP32-S3 specific)
  CapacitiveKeyboard.h/cpp  — MPR121 scanning, pressure, note on/off detection
  KeyboardData.h        — Calibration data structures, EEPROM constants
  KeyboardCalibrator.h/cpp  — Interactive calibration routine
  MidiEngine.h/cpp      — Note stack, velocity, poly aftertouch generation
  MidiTransport.h/cpp   — Dual output: USB MIDI + BLE MIDI (same events to both)
  LedController.h/cpp   — 8-LED feedback (simplified, new meanings)
```

### Main Loop (simplified)
```
loop():
  keyboard.update()
  for each of 48 pads:
    if noteOn  → midiEngine.noteOn(pitch, velocity)
    if noteOff → midiEngine.noteOff(pitch)
    if pressed → midiEngine.updateAftertouch(pitch, pressure)
  midiEngine.flush() → sends queued events to MidiTransport
  ledController.update()
```

No modes, no mode switching, no CV rendering, no arpeggiator.

---

## 3. What to Reuse from Original Code

### 3.1 — CapacitiveKeyboard (REUSE, adapt)

**Source:** `src/CapacitiveKeyboard.cpp` / `.h`

This is the core of the device and should be **ported with minimal changes**:

- **MPR121 register-level driver** (writeRegister, readRegister, runAutoconfiguration, pollAllSensorData) — copy and adapt from `Wire1` to `Wire` with explicit `Wire.begin(SDA, SCL)` for ESP32-S3.
- **Pressure pipeline** — the full chain: delta calculation → response shaping → slew limiter → moving average smoothing. This is well-tuned and should be kept as-is.
- **Note on/off state machine** — threshold-based with hysteresis (pressThresholds / releaseThresholds), adaptive per-key. Keep this logic.
- **Aftertouch deadzone** — `setAftertouchDeadzone()` and `pressDeltaStart[]` mechanism.

**Changes needed:**
- `NUM_KEYS` goes from 24 to **48** (4× MPR121 instead of 2×)
- Add two more MPR121 addresses (e.g. `0x5C`, `0x5D`) and extend `pollAllSensorData()` to scan 4 sensors
- Replace all `Wire1` references with `Wire` (ESP32-S3 uses `Wire` with configurable pins)
- Pressure output should be scaled to **0–127** (MIDI) instead of `CV_OUTPUT_RESOLUTION` (4095)

### 3.2 — KeyboardCalibrator (REUSE, simplify)

**Source:** `src/KeyboardCalibrator.cpp` / `.h`

- **Calibration flow** — hold button at boot → enter calibration → scan all pads → save to EEPROM. Keep this concept.
- **Simplify the signature**: remove `DACManager&` param, reduce from 4 buttons to 1, adapt LED feedback to 8 LEDs instead of 5.
- The calibration data structure (`CalDataStore` in `KeyboardData.h`) should be extended from 24 to 48 keys.

### 3.3 — KeyboardData.h (REUSE, extend)

**Source:** `src/KeyboardData.h`

- `CalDataStore` — keep the EEPROM magic/version/maxDelta pattern; extend arrays to 48 keys.
- `Note` struct and `NOTE_STACK_SIZE` — increase stack size to **48** (full polyphony) or keep a reasonable cap.

### 3.4 — HardwareConfig.h constants (REUSE selectively)

**Source:** `src/HardwareConfig.h`

Keep these tuning constants (they are musically meaningful and already calibrated):

| Constant | Purpose | Keep? |
|---|---|---|
| `PRESS_THRESHOLD_PERCENT` (0.15) | Note-on sensitivity | Yes |
| `RELEASE_THRESHOLD_PERCENT` (0.08) | Note-off hysteresis | Yes |
| `MIN_PRESS_THRESHOLD` / `MIN_RELEASE_THRESHOLD` | Safety floors | Yes |
| `AFTERTOUCH_CURVE_EXP_INTENSITY` (4.0) | Pressure response curve | Yes |
| `AFTERTOUCH_CURVE_SIG_INTENSITY` (2) | Pressure response curve | Yes |
| `AFTERTOUCH_SMOOTHING_WINDOW_SIZE` (4) | Pressure smoothing | Yes |
| `AFTERTOUCH_SLEW_RATE_LIMIT` (150) | Pressure rate limiter | Yes |
| `CAL_PRESSURE_MIN_DELTA_TO_VALIDATE` (300) | Calibration threshold | Yes |
| `I2C_CLOCK_HZ` (400000) | I2C speed | Yes |

**Remove everything related to:** modes/GameMode, CV/DAC, gate/trigger, potentiometers, encoder, arpeggiator, shuffle templates, BPM, glide, octave shift.

---

## 4. What to Write New

### 4.1 — MidiEngine
- Receives note on/off/pressure from the keyboard scan loop
- Converts pressure (0–4095 or 0.0–1.0) to MIDI velocity (0–127) and poly aftertouch (0–127)
- Manages a simple note stack (for potential future features)
- Rate-limits aftertouch to ~30–50 Hz per note
- Only sends aftertouch when value actually changes (delta threshold)
- Queues events and flushes to MidiTransport

### 4.2 — MidiTransport
- Initializes **USB MIDI** via TinyUSB (ESP32-S3 native USB)
- Initializes **BLE MIDI** service (ESP32 BLE stack)
- Exposes `sendNoteOn()`, `sendNoteOff()`, `sendPolyAftertouch()` — each call writes to both USB and BLE
- Handles BLE connection/disconnection gracefully

### 4.3 — LedController (new, simpler)
- 8 LEDs with new meanings (to be defined, e.g.):
  - LED 1: USB MIDI connected
  - LED 2: BLE MIDI connected
  - LED 3–6: Activity / pad pressure visualization
  - LED 7: Calibration active
  - LED 8: Error / status
- No mode display, no octave breathing, no arpeggiator patterns

### 4.4 — Main sketch (main.cpp)
- `setup()`: Init I2C with explicit pins, init keyboard (4× MPR121), init MidiTransport (USB + BLE), init LEDs, check calibration button
- `loop()`: Scan keyboard → generate MIDI → send to both transports → update LEDs
- No mode switching, no CV rendering, no encoder handling

---

## 5. ESP32-S3 Specific Notes

### PlatformIO Configuration
```ini
[env:midi48pad]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
board_build.flash_size = 16MB
board_build.psram_type = opi
; USB MIDI via TinyUSB
build_flags =
  -DARDUINO_USB_MODE=0
  -DARDUINO_USB_CDC_ON_BOOT=0
```

### I2C
- Use `Wire.begin(SDA_PIN, SCL_PIN)` with your chosen GPIOs
- 4× MPR121 at addresses `0x5A`, `0x5B`, `0x5C`, `0x5D`
- Keep `I2C_CLOCK_HZ = 400000`

### USB (Native)
- The "USB" port (GPIO19/20) is the native USB OTG — use this for USB MIDI device via TinyUSB
- The "UART" port is for programming/serial debug

### Voltage
- ESP32-S3 is **3.3 V only** — MPR121 is compatible; verify LED resistors and button pull-ups

---

## 6. Summary of Differences: Original vs New

| Aspect | Original (Nano R4) | New (ESP32-S3) |
|---|---|---|
| Keys | 24 (2× MPR121) | 48 (4× MPR121) |
| Output | CV/Gate/Trigger + MIDI | MIDI only (USB + BLE) |
| Modes | 3 (Pressure/Glide, Interval/Arp, MIDI) | None (single mode) |
| Buttons | 4 (Hold, Mode, Oct+, Oct-) | 1 (calibration) |
| Pots/Encoder | 1 pot + 1 encoder | None |
| LEDs | 5 (mode/octave display) | 8 (connection/status) |
| DAC | GP8403 I2C DAC | Removed |
| Wireless | None | BLE MIDI |
| MCU | Renesas RA4M1 (48 MHz) | ESP32-S3 (240 MHz dual-core) |
| Polyphony | Mono/Duo (engine-dependent) | Full poly (48 notes) |
