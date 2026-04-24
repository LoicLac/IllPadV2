# Pot Reference — ILLPAD V2

End-to-end view of the potentiometer subsystem : hardware, filter pipeline,
routing logic, catch system, mapping UX. Read this when touching any pot
code or adding a new parameter controlled by a pot.

**Source of truth** :
- `src/core/HardwareConfig.h` (MCP3208 pins, NUM_POTS)
- `src/core/PotFilter.h` + `.cpp` (MCP3208 SPI read + deadband + sleep/wake)
- `src/managers/PotRouter.h` + `.cpp` (binding table, catch, dispatch)
- `src/setup/ToolPotMapping.*` (Tool 7 UX)

Related flows : see [`runtime-flows.md`](runtime-flows.md) §5 for the
per-frame pipeline. Hardware wiring details : see
[`hardware-connections.md`](hardware-connections.md).

---

## 1. Hardware

5 × 10 kΩ linear potentiometers, all read through an **MCP3208 external
ADC over SPI**. The older ESP32 native ADC1 path has been retired — the
MCP3208 was introduced because the BLE radio blocks ADC2, and ADC1 shared
pin usage with other peripherals caused readback glitches.

### Wiring summary

| Pot | MCP3208 channel | Physical location | Default function |
|---|---|---|---|
| Right 1 | CH0 | Right side, top | Tempo |
| Right 2 | CH1 | Right side | Shape / Gate |
| Right 3 | CH2 | Right side | Slew / Shuffle depth |
| Right 4 | CH3 | Right side, bottom | Base velocity |
| Rear | CH4 | Rear | LED brightness |

SPI pins (set in `HardwareConfig.h`) :

| Signal | ESP32 GPIO |
|---|---|
| SCK | 18 |
| MISO (DOUT) | 16 |
| MOSI (DIN) | 15 |
| /CS | 17 |

CH5–CH7 of the MCP3208 are unconnected (spare). All 10 kΩ pot wipers feed
their CHx through an RC filter (100 Ω + 100 nF to GND, ~16 kHz low-pass)
that bounds source impedance well below the MCP3208's 1 kΩ @ 1 MHz
requirement.

Voltage reference is 3.3 V (VDD = VREF tied together, 100 nF + 10 µF
decoupling). Full-scale reading = 4095.

---

## 2. PotFilter pipeline (runtime)

**There is no EMA and no oversampling.** MCP3208 residual noise after the
NeoPixel bulk cap + 10 µF VREF hardware fix sits around 6–10 LSB dmax with
occasional 12–15 LSB spikes. A deadband of 10 LSB eliminates >95 % of false
events while keeping ~410 effective positions (3× MIDI CC resolution).

### Per-frame chain (Core 1)

```
SPI read (MCP3208)
  → direction invert (4095 - v, so CW = up)   [readPotRaw]
  → clamp [0..4095]
  → inter-frame delta sanity check (reject >300 LSB/frame, pots 0-3 only)
  → deadband gate (|raw - stable| >= perPotDeadband[i])
  → edge snap (stable < edgeSnap → 0; stable > 4095-edgeSnap → 4095)
  → sleep transition check (pot 4 only, if idle > sleepMs)
```

### State machine

Only two states (no SETTLING since no EMA to converge) :

- **POT_ACTIVE** — normal read, full pipeline each frame.
- **POT_SLEEPING** — cheap periodic peek every 50 ms. Wake when
  `|raw - sleepBaseline| > wakeThresh`.

**Sleep applies to pot 4 (rear) only.** Pots 0–3 are live-performance
knobs and stay ACTIVE permanently — their residual noise would cause
false wakes, and a user constantly touching them defeats the energy
savings.

### Rear pot special treatment

- **Rate-limited to ~50 Hz** : modulo counter (`REAR_DIVISOR = 20`)
  skips 19 out of 20 cycles at the 1 kHz Core 1 loop rate.
- **Median-of-3 reads** per update : the rear pot's ADC peak-to-peak
  sits at the deadband edge, so three reads + in-place median sorting
  (~60 µs extra, fired every 20 ms) stabilize it.
- **Sleep enabled** : wakes on a threshold aligned with its deadband
  (`wakeThresh = 8`).
- **Deadband = 8** (vs 10 for pots 0–3) : cleaner post-HW-fix, UX
  privilegiée (brightness knob is the most fiddled).

### Boot sequence (`PotFilter::begin()`)

1. SPI peripheral init + 10 ms rail settle.
2. MCP3208 library `begin(CS_PIN)` + 10 ms for RC filter settle (5τ ≈
   8 ms) + MCP internal state.
3. **Discard first 2 conversions per channel** — the MCP3208 sample
   capacitor holds whatever was on the pin before CS toggle; the first
   two reads blend previous voltage with real input.
4. **Seed each pot with median-of-5 reads** (~150 µs total) — rejects
   any single-read outlier caused by a transient on VDD/VREF during
   boot.

Result : each pot's `stable` value is trustworthy from the first loop
iteration.

### Configuration

`PotFilterStore` (NVS `illpad_pflt` / `cfg`, v1, 12 B) holds :
`perPotDeadband[5]`, `edgeSnap`, `sleepEn`, `sleepMs`, `wakeThresh`,
`snap`, `actThresh`. Loaded at boot via `NvsManager::loadBlob()`. No
Tool UI currently — values are tuned in code. The descriptor exists in
`NVS_DESCRIPTORS[]` for the "Monitor in T7" slot that is still
unimplemented.

---

## 3. PotRouter — binding table and dispatch

`PotRouter` owns the runtime table of **bindings** that map
`(potIdx, buttonMask, bankType) → PotTarget`. The table is rebuilt from
the user-configurable `PotMappingStore` (Tool 7) on :
- boot (via `loadMapping()`),
- Tool 7 save (via `applyMapping()`).

Rebuild flow : `loadMapping()` / `applyMapping()` → `rebuildBindings()`
→ `seedCatchValues()`. The binding table, catch states, and CC slot
tracking are all regenerated.

### Per-frame update

```
PotRouter::update()
  → PotFilter::updateAll()
  → resolveBindings(buttonMask, bankType) → best binding per pot
  → for each pot where hasMoved(): applyBinding()
```

### applyBinding()

1. Read `adc = PotFilter::getStable(potIndex)`.
2. If target is `TARGET_LED_BRIGHTNESS` → bypass catch, apply immediately.
3. If not caught → compare `adc` vs `storedValue`, show uncaught bargraph,
   **wait** (no parameter write).
4. If caught → convert ADC → parameter range, write output, mark dirty
   (for NVS), show bargraph.
5. If target is a **global target** → propagate `storedValue` across all
   contexts/bindings sharing this target (catch-propagation invariant).

---

## 4. Global vs per-bank targets

Parameter scope is a design decision baked into the `PotTarget` enum and
`isPerBankTarget()` helper. It determines catch behavior and NVS channel.

### Global targets

Value shared across banks. Catch persists across bank switches — once
caught, stays caught. Catch-propagation ensures both NORMAL and ARPEG
bindings referencing the same global target stay in sync after a bank
type change.

- Tempo (BPM)
- Response shape (pressure curve)
- Slew rate
- AT deadzone
- LED brightness
- Pad sensitivity

### Per-bank targets

Value stored per bank slot. Catch **resets** on bank switch (pot must
physically cross the new bank's stored value before writing).

- Gate length
- Shuffle depth
- Shuffle template
- Division
- Pattern
- Octave range
- Base velocity (both NORMAL + ARPEG per bank)
- Velocity variation (both)
- Pitch bend offset (NORMAL only)

### Volatile outputs (no NVS)

- MIDI CC (sends on foreground bank's channel, no recall).
- MIDI Pitchbend (max one per context, sends on foreground channel).

Only writes on value change (dirty flag pattern — no MIDI flood). CC slot
lookup uses **binding index** (not CC number) to avoid cross-context
collisions when the same CC is assigned in both NORMAL and ARPEG
contexts.

---

## 5. Catch system (P4)

Prevents parameter jumps when the physical pot position doesn't match
the stored value at catch-reset time (bank switch, context change,
mapping edit).

Per-binding `CatchState{caught, storedValue}`. Apply rules :

| `caught` | `adc` crosses `storedValue` ? | Action |
|---|---|---|
| false | no | show dim "uncaught" bargraph at pot position, **wait** |
| false | yes | `caught = true`, normal apply |
| true | — | normal apply, update `storedValue` |

Bank switch triggers `resetPerBankCatch()` : per-bank targets become
uncaught, global targets keep their catch (seeded from stored value).

`TARGET_LED_BRIGHTNESS` is the only target that **bypasses catch** : its
output is immediate (no visual waste waiting for the pot to cross).

---

## 6. Button modifiers (P7)

Held buttons change the active binding layer :

| Button | Modifies | Scope |
|---|---|---|
| LEFT | 4 right pots (R1–R4) | adds "2nd slot" binding |
| REAR | rear pot only | swaps brightness → pad sensitivity |

They **never cross** — LEFT modifiers do not affect the rear pot, REAR
modifier does not affect the right pots. `resolveBindings()` enforces
this via the `buttonMask` field.

---

## 7. Default pot mapping

Shipped defaults, overwritten by user via Tool 7.

| Pot | NORMAL alone | NORMAL + hold left | ARPEG alone | ARPEG + hold left |
|---|---|---|---|---|
| R1 | Tempo (10–260 BPM) | *— empty —* | Tempo | Division (9 binary) |
| R2 | Response shape | AT deadzone | Gate length | Pattern (5 discrete) |
| R3 | Slew rate | Pitch bend (per-bank) | Shuffle depth (0.0–1.0) | Shuffle template (10) |
| R4 | Base velocity | Velocity variation | Base velocity | Velocity variation |

| Rear pot | Alone | + hold rear |
|---|---|---|
| Rear | LED brightness | Pad sensitivity |

The empty slot (R1, NORMAL + hold left) is reserved for future use.

---

## 8. User-configurable mapping (Tool 7)

4 right pots × 2 button layers = **8 slots per context**. Context =
NORMAL or ARPEG, independent. The rear pot is fixed (not user-configurable).

Each slot can be assigned :
- any parameter from the context's pool (type-dependent),
- MIDI CC (with CC#),
- MIDI Pitchbend (max one per context, auto-steals if a second is
  assigned).

`PotMappingStore` (NVS `illpad_pmap` / `mapping`) stores two arrays of 8
`PotMapping` entries. Each entry = `{PotTarget, ccNumber}`.

### Tool 7 UX

Two context pages (NORMAL / ARPEG), toggle with `t`. Physical pot detection :
turn a pot (or hold-left + turn) to select a slot. Pool line always
visible showing all assignable parameters, color-coded :

- GREEN = available
- DIM = already assigned (one per context)

`<` / `>` cycles the pool, ENTER confirms assignment and auto-saves to
NVS. **Steal logic** : picking an already-assigned param orphans the
source slot to "empty".

CC flow : picking CC enters CC# sub-mode immediately (`<` / `>` adjusts
number, ENTER confirms, `q` cancels and restores previous assignment).

`d` resets current context to defaults. `q` exits.

Setup-tool behavioral conventions (save policy, flashSaved cost) are in
[`setup-tools-conventions.md`](setup-tools-conventions.md).

---

## 9. Adding a new pot target

Minimum steps :

1. `PotRouter.h` — add entry to `enum PotTarget`.
2. `PotRouter.cpp` :
   - add case in `applyBinding()` switch (ADC → value, write output).
   - register scope in `isPerBankTarget()`.
   - register discrete step count in `getDiscreteSteps()` (for bargraph).
3. `ToolPotMapping.cpp` — add entry to the pool (label + color).
4. Consumer : add a getter in `PotRouter` if needed, wire into
   `main.cpp::handlePotPipeline()` or `pushParamsToEngine()`.
5. If the value persists per-bank, add a field to the relevant Store
   (e.g. `ArpPotStore`) and a `queueXxxWrite()` path in `NvsManager`.

See also [`patterns-catalog.md`](patterns-catalog.md) P4 (catch) and P9
(debounced NVS write).

---

## 10. Bug patterns (pot-specific)

| Pattern | Example | Where to look |
|---|---|---|
| MIDI flood from noisy ADC | CC toggling ±1 at boundary | `PotRouter.cpp` CC dirty check ; hysteresis / deadband sizing in `PotFilter.cpp::applyDefaults()` |
| Stale catch after bank switch | Per-bank target feels "frozen" | `PotRouter::resetPerBankCatch` ; binding rebuild after `applyMapping()` |
| Button combo confusion | LEFT + REAR held simultaneously | `PotRouter::resolveBindings` mask checks |
| Pot "jumps" after update | Struct change triggered NVS reset | Zero-migration policy : user re-enters values. Confirm `PotMappingStore` version bumped if struct changed. |
| Pot 4 false wake | Rear pot wakes without being touched | `wakeThresh` vs `perPotDeadband[4]` alignment — they must match |
| Rear pot sluggish | User expects instant brightness response | By design : `REAR_DIVISOR = 20` → ~50 Hz. `LED_BRIGHTNESS` bypasses catch but reads rate-limited. |
