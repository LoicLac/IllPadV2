# tools/

Standalone bench / diagnostic sketches for ILLPAD V2 hardware. Each
subfolder is an **independent PlatformIO project** with its own
`platformio.ini` and env. They share **zero code** with the main ILLPAD
firmware in `src/` and have **no effect** on the main build.

All tools target the assembled ILLPAD V2 hardware. None is intended for
generic use.

---

## ───── For agents (Claude, Copilot, etc.) ─────

**DO NOT load these files into context by default.** Each tool is several
hundred lines of test logic that has no bearing on the runtime firmware,
its invariants, the pad pipeline, or the NVS layout. Reading them costs
context for no benefit and risks contaminating reasoning about the main
firmware with bench-only patterns (BLE NimBLE server, raw `Wire.h`, ad-hoc
loops, no-NVS, etc.).

**Load one only when both conditions are met:**
1. The user is explicitly debugging the matching domain on real hardware
   (ADC noise, MPR121 chain integrity, pot jitter under runtime load…),
   AND
2. The user asks to run, modify, or interpret output from that specific
   tool by name.

For all other tasks (feature work, refactor, audit of `src/`, doc work),
treat this folder as opaque.

---

## ───── Inventory ─────

Flat list, no priority order. Each tool covers a different diagnostic
question; pick the one that matches the bug.

### `debug-adc/`
- **Purpose** : compare 1× / 4× / 16× oversampling on the 5 pots, with an
  optional BLE advertising load to gauge RF-induced jitter.
- **When to use** : tuning the oversampling ratio for `PotFilter`,
  isolating BLE-induced ADC noise from other sources.
- **Build** : `~/.platformio/penv/bin/pio run -d tools/debug-adc -e debug-adc -t upload`
- **Monitor** : `~/.platformio/penv/bin/pio device monitor -d tools/debug-adc -b 115200`

### `debug-i2c/`
- **Purpose** : test presence, identification, init and basic
  communication of all 4 MPR121 on the I2C bus. Tolerant to any partial
  population (0..4 chips, missing sockets, swapped ADDR pins, dead chip).
  Live touch display per chip.
- **When to use** : suspected dead chip, bad solder, ADDR-pin miswiring,
  validating a freshly assembled board, debugging the I2C chain before
  trusting the main firmware's pad pipeline.
- **Build** : `~/.platformio/penv/bin/pio run -d tools/debug-i2c -e debug-i2c -t upload`
- **Monitor** : `~/.platformio/penv/bin/pio device monitor -d tools/debug-i2c -b 115200`
- **Boot prompt** : `1` = full I2C scan (0x03..0x77), `2` = MPR121 range only.
- **Live commands** : `r` rescan, `t` toggle live touch, `h` help.

### `pot-diag-v2/`
- **Purpose** : characterize pot ADC jitter under the **3 real runtime
  stressors** of the main firmware (NeoPixel refresh storm + BLE notify
  + I2C MPR121 polling), each independently toggleable. Outputs
  statistical samples (min/max/mean/stddev/p2p + delta histogram) over
  2000 readings per pot, in CSV format (`POTDIAG,...`) for log analysis.
  Includes a windowed aggregation mode (1 report / 5 s).
- **When to use** : pot jitter bug — identifying *which* of the 3
  stressors is the culprit. More heavy-duty than `debug-adc/`.
- **Build** : `~/.platformio/penv/bin/pio run -d tools/pot-diag-v2 -e pot-diag-v2 -t upload`
- **Monitor** : `~/.platformio/penv/bin/pio device monitor -d tools/pot-diag-v2 -b 115200`
- **Terminal commands** (sketch-internal) : `n`/`b`/`i` toggle stressors,
  `a` all on, `0` all off, `c` toggle windowed mode, `ENTER` sample burst,
  `l` print load state, `h` help.

---

## ───── Conventions for adding a new tool ─────

- One folder per tool: `tools/<name>/` with `platformio.ini` + `src/main.cpp`.
- Env name = folder name (e.g. `[env:debug-foo]` in `tools/debug-foo/`).
- Same board (`esp32-s3-devkitc-1`) and same USB-CDC flags as the main
  firmware — these sketches must run on the assembled ILLPAD V2 hardware.
- No dependency on `src/` of the main firmware.
- Add an entry to the inventory above (alphabetical or by domain — no
  priority ranking).
