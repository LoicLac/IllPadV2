# Boot Sequence — ILLPAD V2

Firmware boot flow, LED visual feedback, failure modes, setup mode entry.
Read this when debugging a boot hang, adding a step, or adjusting the
visual cues.

**Source of truth in code** : `src/main.cpp` (lines noted below),
`src/core/LedController.cpp` (`showBootProgress`, `endBoot`).

---

## 1. Steps at a glance

LEDs fill progressively (1 LED per step). On failure, the failed step's
LED **blinks rapidly** in red while prior steps stay solid white.

```
Step 1: ●○○○○○○○  LED hardware ready
Step 2: ●●○○○○○○  I2C bus ready
Step 3: ●●●○○○○○  Keyboard (MPR121 ×4) OK
        ●●◉○○○○○  ← Step 3 BLINKS = keyboard/MPR121 FAILED (halts forever)
        [setup mode detection: 3 s window to press rear button,
         then hold 3 s — chase pattern during hold]
Step 4: ●●●●○○○○  MIDI Transport (USB + BLE) started
Step 5: ●●●●●○○○  NVS loaded
Step 6: ●●●●●●○○  Arp system ready
Step 7: ●●●●●●●○  Managers ready (Bank, Scale, Pot)
Step 8: ●●●●●●●●  All systems go (200 ms full bar)
        → endBoot() → normal bank display
```

The 8-step fill is implemented by `LedController::showBootProgress(n)`
(1 ≤ n ≤ 8). Call sites in `main.cpp` :

| Step | File location | Completed when |
|---|---|---|
| 1 | `main.cpp:155` | NeoPixel `begin()` + initial clear |
| 2 | `main.cpp:161` | Wire.begin + I2C speed set |
| 3 | `main.cpp:174` | 4× MPR121 initialized (halts if fail) |
| 4 | `main.cpp:295` | USB MIDI + BLE MIDI transport started |
| 5 | `main.cpp:326` | `NvsManager::loadAll()` completed |
| 6 | `main.cpp:389` | All `s_arpEngines[4]` constructed + bound |
| 7 | `main.cpp:438` | BankManager + ScaleManager + PotRouter ready |
| 8 | `main.cpp:459` | Full bar at 200 ms, then `endBoot()` |

---

## 2. Per-step detail

### Step 1 — LED hardware

`Adafruit_NeoPixel.begin()` + initial `clear()`. The first LED lights up
— if the stick is dead or miswired, nothing visible → halt before Step 2.

### Step 2 — I2C bus

`Wire.begin(PIN_SDA, PIN_SCL)` + `setClock(400_000)`. If the bus is
physically broken (short, missing pull-ups), subsequent MPR121 probes
will fail at Step 3.

### Step 3 — Keyboard (critical)

Probes 4× MPR121 at addresses 0x5A, 0x5B, 0x5C, 0x5D. Configures
thresholds (TOU/REL) and touch debounce. If any sensor fails to respond,
**the firmware halts at Step 3 with a rapid red blink on LED 3**.

This is the primary hard-failure mode. Most common causes :
- Missing I2C pull-ups (4.7 kΩ to 3.3 V on SDA + SCL).
- Wrong ADDR pin wiring (two sensors on same address).
- Power issue on the 3.3 V rail (unstable VIN to sensors).

### Setup mode detection window

Between steps 3 and 4, a **two-phase trigger** opens :

1. **Press window** : `CAL_WAIT_WINDOW_MS = 3000 ms` — user must press
   the rear button within 3 s of Step 3 completing.
2. **Hold window** : after press is detected, user must hold
   `CAL_HOLD_DURATION_MS = 3000 ms` of continuous press to commit to
   setup mode.

During the hold, LEDs run a **chase pattern** (white sweep) to confirm
the hold is being sensed. Release early → cancel, resume normal boot.

If setup mode commits, `SetupManager::run()` takes over (VT100 terminal
session). The rest of the boot sequence (Steps 4–8) is skipped ; boot
resumes from Step 4 only after the user exits setup (Tool [0] Reboot).

### Step 4 — MIDI Transport

`MidiTransport::begin()` starts USB MIDI (TinyUSB) and BLE MIDI
(ESP32-BLE-MIDI) simultaneously. A failure here is rare : TinyUSB
initialization is on the host-side cable, and BLE MIDI can still start
even without an active connection.

### Step 5 — NVS

`NvsManager::loadAll()` iterates every store blob defined in
`NVS_DESCRIPTORS[]`. Each blob is validated (magic/version/size) —
**failed validation logs a warning and applies compile-time defaults**
(zero-migration policy, see `nvs-reference.md`).

Boot proceeds even if every blob is rejected ; the user will have to
re-configure via setup mode.

### Step 6 — Arp system

Constructs `s_arpEngines[4]` and binds each to its scheduler slot. Loads
per-bank `ArpPotStore` (gate, shuffle, pattern, division, octave range)
into each engine.

### Step 7 — Managers

Instantiates `BankManager`, `ScaleManager`, `PotRouter`. Each reads its
Store from the loaded NVS data and sets up runtime state.

`PotFilter::begin()` is called here too — see
[`pot-reference.md`](pot-reference.md) §2 for its internal boot (SPI
init, rail settle, median-of-5 seed).

### Step 8 — All systems go

Full 8-LED bar lit for 200 ms as a deliberate "ready" confirmation.
`endBoot()` transitions the LedController priority machine out of boot
mode and into normal bank display.

---

## 3. Failure modes

### Hard halts

| Step | Symptom | Likely cause |
|---|---|---|
| 1 | No LEDs at all | LED stick dead, data pin miswired, 3.3 V rail down |
| 3 | LED 3 blinks red continuously | One or more MPR121 not responding on I2C |

### Soft failures (boot continues)

| Step | Symptom | Consequence |
|---|---|---|
| 5 | "NVS: xxx reset to defaults" log lines | User re-saisies settings in setup mode |
| 4 | BLE MIDI doesn't advertise | USB MIDI still works ; BLE may need power cycle |
| 7 | Pot seed outliers | `PotFilter::begin()` median-of-5 rejects them — no user impact |

### Unrelated to these LEDs

- ESP32 bootloader errors : handled by ESP-IDF before any user code
  runs. Usually visible only via serial (CDC) at upload time.
- Flash/PSRAM issues : trigger ESP-IDF panic before `setup()` is called,
  no visual indication on the NeoPixel stick.

---

## 4. Tunables (HardwareConfig.h)

| Constant | Default | Effect |
|---|---|---|
| `CAL_WAIT_WINDOW_MS` | 3000 ms | Press window after Step 3 |
| `CAL_HOLD_DURATION_MS` | 3000 ms | Hold window to commit setup mode |
| `CAL_AUTOCONFIG_COUNTDOWN_MS` | 1000 ms | Auto-config prompt delay inside Tool 1 |
| `CHASE_STEP_MS` | 80 ms | Per-LED step duration of the chase pattern |

Changing these affects how fast / slow setup mode is reachable. The
hold is deliberately slow (3 s) to avoid accidental entry from a pocket
press or transport bump.

---

## 5. Post-boot transition

`endBoot()` at Step 8 hands control to the priority state machine
(level 1 → level 9, see [`led-reference.md`](led-reference.md) §5).
The normal bank display starts rendering immediately, with the current
bank loaded from NVS (or default = bank 0).

No MIDI is emitted during boot (Steps 1–7) — `MidiEngine::flush()` is
only called from `loop()`, not from `setup()`.
