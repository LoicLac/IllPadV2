# SetupPotInput — Pot Helper for Setup Tools

**Date:** 2026-03-30
**Status:** Implemented
**Files:** `src/setup/SetupPotInput.h`, `src/setup/SetupPotInput.cpp`

---

## Goal

A reusable, type-agnostic pot input system for **all** setup tools. Two physical pots (Right 1 and Right 2) control any numeric parameter during setup mode. The pot modifies a value via pointer — the tool only needs to seed the pointer and range, the helper does the rest.

**Principle:** any field with a numeric range in any tool can be pot-controlled by adding 3 lines of code (seed on cursor change, update in loop, check if changed).

---

## Hardware

- **Pot 1**: GPIO 11 (`POT_RIGHT1_PIN`) — primary value
- **Pot 2**: GPIO 12 (`POT_RIGHT2_PIN`) — secondary value (when applicable)
- ADC: 12-bit (0-4095), `analogRead()` direct (PotRouter is not running in setup mode)

---

## How It Works

### API — Pointer-Based, Type-Agnostic

```cpp
// The pot modifies *target directly. No getValue() needed.
void seed(uint8_t ch, int32_t* target, int32_t minVal, int32_t maxVal);
void disable(uint8_t ch);
bool update();         // returns true if any target changed
bool isActive(uint8_t ch) const;
bool isEnabled(uint8_t ch) const;
```

The tool provides a `int32_t` intermediary variable, loads the field value into it before seed, and copies back to the typed field after update. This decouples the pot helper from the field's actual type (uint8_t, int8_t, uint16_t...).

### Integration Pattern (3 lines per tool)

```cpp
// In tool header:
SetupPotInput _pots;
int32_t _potVal[2];     // intermediary for pot targets

// On cursor change (▲▼ or page toggle):
_potVal[0] = myField;
_pots.seed(0, &_potVal[0], minVal, maxVal);

// In loop (every ~30ms):
if (_pots.update()) {
  myField = (uint8_t)_potVal[0];  // copy back to typed field
  screenDirty = true;
}
```

This pattern works for ANY numeric field in ANY tool. The pot helper doesn't know about LedSettingsStore, ColorSlotStore, SettingsStore — it only knows about a pointer and a range.

---

## Catch Mode: Differential + Re-Anchor

### Phase 1 — Idle
Pot not moving. No effect on value. Re-seeds on cursor change (▲▼ navigation or page toggle).

### Phase 2 — Activation
Pot must move **30 raw ADC units** (~0.7% of full range) from its seed position to activate. This avoids accidental changes when navigating — the pot is ignored until you intentionally turn it.

### Phase 3 — Differential
Once active, each ADC movement maps to a value delta relative to the current value. The absolute position of the pot doesn't matter — only the direction and amount of movement.

**Accumulator:** Small movements are accumulated across update cycles. For a range of 100 (0-100%), it takes ~41 ADC units per step. The accumulator preserves the fractional remainder so small movements add up:

```
cycle 1: pot moves +15 ADC → accum = 15, delta = 15*100/4095 = 0 (not enough)
cycle 2: pot moves +20 ADC → accum = 35, delta = 35*100/4095 = 0 (still not enough)
cycle 3: pot moves +10 ADC → accum = 45, delta = 45*100/4095 = 1 → value changes!
         accum residual = 45 - 1*4095/100 = 4 (carried to next cycle)
```

### Phase 4 — Re-Anchor (automatic)
When the pot's mapped position naturally coincides with the current value (within 60 ADC units), the system switches to **absolute mode**: pot position = value, 1:1. This happens gradually as you turn the pot and the value converges with the pot position.

### Phase 5 — Absolute
Pot position maps directly to value. No accumulator needed. Stays absolute until cursor moves to another param (→ back to Phase 1).

---

## ADC Pipeline

```
analogRead() → jitter filter (±6 ADC) → activation check (30 ADC) → differential or absolute
```

- **No EMA smoothing** — raw ADC for instant response. The jitter filter (6 ADC deadzone) is sufficient for the ESP32-S3's ~30-50 LSB noise.
- **No float at runtime** — all integer math with `int64_t` for multiplication to avoid overflow.
- **Update rate:** ~30ms (called from tool's main loop, throttled externally).

---

## Current Integrations

### Tool 7 — LED Settings (implemented)

**Page 0 (COLOR+TIMING):**

| Cursor | Pot 1 | Pot 2 |
|--------|-------|-------|
| Rows 0-15 (COLOR) | Hue offset (-128..+127) | Intensity (0-100%) |
| Row 16 (Pulse period) | Period (500-4000 ms) | — |
| Row 17 (Tick flash) | Duration (10-100 ms) | — |

**Page 1 (CONFIRM):**

| Cursor | Pot 1 | Pot 2 |
|--------|-------|-------|
| All params | Value (blinks 1-3, duration 100-600ms, etc.) | — |

- Pots active without pressing Enter — seed on cursor ▲▼ movement
- Value updates in real time, LEDs 3-4 preview follows, screen redraws
- Control bar shows [P1]/[P2] indicators (dim=idle, bright=active)

### Tool 5 — Settings (planned)

| Param | Pot 1 | Range |
|-------|-------|-------|
| AT Rate | Value | 10-100 ms |
| Double-Tap Window | Value | 100-250 ms |
| Bargraph Duration | Value | 1000-10000 ms |
| Enums/booleans | — (disabled) | — |

### Tool 6 — Pot Mapping (planned)

| Context | Pot 1 | Range |
|---------|-------|-------|
| CC# sub-editor | CC number | 0-127 |

---

## Per-Channel State

```cpp
struct Channel {
  uint8_t  pin;           // GPIO
  int32_t* target;        // pointer to value being controlled
  int32_t  minVal, maxVal;
  int32_t  lastRaw;       // last raw ADC (no smoothing)
  int32_t  baseline;      // raw ADC at seed time
  int32_t  accumDelta;    // accumulated fractional ADC delta
  bool     enabled;
  bool     active;        // past initial deadzone (30 ADC)
  bool     anchored;      // in absolute mode
};
```

---

## Constants

| Constant | Value | Purpose |
|----------|-------|---------|
| `MOVE_THRESHOLD` | 30 ADC | Activation: intentional pot turn (~0.7% of range) |
| `JITTER_DZ` | 6 ADC | Noise filter: ignore ESP32-S3 ADC jitter |
| `ANCHOR_WINDOW` | 60 ADC | Snap to absolute when pot position ≈ value |
| `ADC_MAX` | 4095 | ESP32 12-bit ADC full scale |
