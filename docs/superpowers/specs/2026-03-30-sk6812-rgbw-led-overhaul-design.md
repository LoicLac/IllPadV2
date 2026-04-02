# SK6812 RGBW LED Overhaul — Design Spec

**Date:** 2026-03-30
**Status:** Draft
**Hardware change:** WS2812 RGB (NEO_GRB) -> SK6812 RGBW (NEO_GRBW, 32 bits/pixel)

---

## 1. Goals

- Exploit the dedicated W channel for clean whites and subtle RGBW color mixing
- Gamma correction for perceptually uniform brightness, especially usable low-intensity range
- User-configurable colors via named presets + hue shift
- Unified brightness pipeline — one rendering path, brightness pot as perceptual master fader
- Tool 7 redesign: 3 pages with live preview on 2 central LEDs

---

## 2. Infrastructure

### 2.1 RGBW Color Type

Replace `struct RGB { uint8_t r, g, b }` with:

```cpp
struct RGBW { uint8_t r, g, b, w; };
```

All color definitions, rendering methods, and preset tables use RGBW. The RGB type is removed entirely.

### 2.2 NeoPixel Initialization

```cpp
Adafruit_NeoPixel _strip(NUM_LEDS, LED_DATA_PIN, NEO_GRBW + NEO_KHZ800);
```

All `setPixelColor()` calls pass 4 channels (r, g, b, w).

### 2.3 Gamma LUT

256-entry compile-time constant, gamma 2.8:

```cpp
static const uint8_t GAMMA_LUT[256] = { 0, 0, 0, 0, 0, 1, 1, 1, ... 255 };
```

Applied once per channel (R, G, B, W independently) at the final output stage inside `setPixel()`, just before `_strip.setPixelColor()`. All upstream code works in linear space.

### 2.4 Perceptual-to-Linear Conversion

All user-facing intensities are 0-100 (perceptual %). Conversion to linear 0-255:

```cpp
uint8_t perceptualToLinear(uint8_t percent) {
    return (uint8_t)(255.0f * powf(percent / 100.0f, 1.0f / 2.8f));  // inverse gamma
}
```

Can be a 101-entry LUT for zero runtime cost.

---

## 3. Color Preset System

### 3.1 Palette (const, in flash)

14 named RGBW presets, optimized for SK6812:

| # | Name | R | G | B | W | Character |
|---|------|---|---|---|---|-----------|
| 0 | Pure White | 0 | 0 | 0 | 255 | W channel only, clean white |
| 1 | Warm White | 40 | 20 | 0 | 200 | Warm tint via R+G, mostly W |
| 2 | Cool White | 0 | 10 | 30 | 220 | Slightly blue-shifted W |
| 3 | Ice Blue | 0 | 20 | 180 | 40 | Rich blue with W softening |
| 4 | Deep Blue | 0 | 0 | 255 | 0 | Pure B, no W |
| 5 | Cyan | 0 | 180 | 200 | 20 | |
| 6 | Amber | 200 | 80 | 0 | 60 | Warm confirmation color |
| 7 | Gold | 255 | 140 | 0 | 30 | |
| 8 | Coral | 255 | 60 | 30 | 20 | |
| 9 | Violet | 100 | 0 | 255 | 0 | |
| 10 | Magenta | 200 | 0 | 180 | 10 | |
| 11 | Green | 0 | 255 | 0 | 0 | |
| 12 | Soft Peach | 180 | 60 | 40 | 80 | Pastel RGBW example |
| 13 | Mint | 60 | 200 | 60 | 40 | |

Exact values will be tuned visually once the strip is wired. Adding presets at the end of the list does not break stored indexes.

### 3.2 Hue Shift

`int8_t hue_offset` (-128 to +127 degrees). Applied to the RGB channels of the preset:

1. Convert R,G,B to HSV
2. Rotate H by offset
3. Convert back to RGB
4. Keep W unchanged (W is the "body" of the preset, not affected by hue rotation)

### 3.3 Color Slots

13 configurable slots. Each stored as `{ uint8_t presetId, int8_t hueOffset }` = 2 bytes.

| Slot | Effect | Default Preset |
|------|--------|----------------|
| 0 | NORMAL fg | Pure White |
| 1 | NORMAL bg | Warm White |
| 2 | ARPEG fg | Ice Blue |
| 3 | ARPEG bg | Deep Blue |
| 4 | Tick flash | Pure White |
| 5 | Confirm: bank switch | Pure White |
| 6 | Confirm: scale root | Amber |
| 7 | Confirm: scale mode | Gold |
| 8 | Confirm: scale chromatic | Gold |
| 9 | Confirm: hold | Deep Blue |
| 10 | Confirm: play ack | Green |
| 11 | Confirm: stop | Cyan |
| 12 | Confirm: octave | Violet |

System colors (error, boot, battery gauge) remain hardcoded — no slots.

**Note on ARPEG slots vs Tool 7 lines:** Slots 2 (ARPEG fg) and 3 (ARPEG bg) each store one color preset + hue. In Tool 7, each slot appears as 2 lines (min/max intensity) for a total of 4 ARPEG lines. Changing the preset on any ARPEG fg line propagates to the other ARPEG fg line (same color, different intensity). Same for bg. The ColorSlotStore stores 13 color slots; the LedSettingsStore stores the 16 intensity values (including the 4 ARPEG min/max pairs).

---

## 4. Unified Brightness Pipeline

### 4.1 Single Rendering Method

The 4 current methods (`setPixel`, `setPixelScaled`, `setPixelAbsolute`, `setPixelAbsoluteScaled`) are replaced by a single method:

```cpp
void setPixel(uint8_t led, RGBW color, uint8_t intensity);
```

Where `intensity` is the combined result of the slot's configured intensity scaled by the brightness pot.

### 4.2 Brightness Pot as Master Perceptual Fader

The rear pot controls global perceived brightness (0-100%). All LEDs scale through it uniformly. Ratios between elements are preserved at any pot position.

**Zero = off.** No floor, no minimum. The pot can extinguish all LEDs completely for concert use. Error blinks also respect this — diagnostics happen via MIDI or serial, not LEDs.

Example at pot = 20%:

| Element | Tool 7 intensity | Combined | Perceived |
|---------|-----------------|----------|-----------|
| ARPEG fg max | 100% | 20% | Brightest |
| ARPEG fg min | 30% | 6% | Subtle glow |
| Tick flash | 100% | 20% | Pierces the pulse |
| ARPEG bg | 12% | 2.4% | Dim backdrop |

### 4.3 Pot Response Curve (compile-time)

The brightness pot ADC (0-255 after PotRouter) maps to perceived brightness (0-100%) through a configurable curve:

```cpp
// In HardwareConfig.h
// Brightness pot response curve:
//   POT_CURVE_LOW_BIASED  — more resolution in low intensities,
//                           bottom half of pot travel covers 0-25% perceived,
//                           ideal for dark stage fine-tuning
//   POT_CURVE_LINEAR      — uniform perceptual steps across full range,
//                           each pot increment = same perceived change
//   POT_CURVE_SIGMOID     — resolution at both extremes (near 0% and 100%),
//                           fast sweep through the middle
#define BRIGHTNESS_POT_CURVE  POT_CURVE_LOW_BIASED
```

Implemented as a 256-entry LUT (pot value -> 0-100 perceived):

| Pot position | LOW_BIASED | LINEAR | SIGMOID |
|---|---|---|---|
| 0% | 0 | 0 | 0 |
| 10% | 1 | 10 | 2 |
| 25% | 6 | 25 | 10 |
| 50% | 25 | 50 | 50 |
| 75% | 56 | 75 | 90 |
| 100% | 100 | 100 | 100 |

Default: `POT_CURVE_LOW_BIASED` — optimized for dark stage work where fine-tuning subtle glows matters most.

### 4.4 Full Rendering Pipeline

```
  Color slot (NVS)              Intensity (NVS, 0-100%)
       |                               |
  PRESETS[id] -> RGBW base      Brightness pot (0-100%)
       |                               |
  hue_offset -> rotate H        potCurve[adcValue] -> potPercent
       |                               |
  RGBW final color              intensity * potPercent / 100
                                        |
                                perceptualToLinear(combined)
                                        |
                    setPixel(led, rgbw, linearScale)
                                        |
                  R,G,B,W each -> GAMMA_LUT[channel]
                                        |
                              _strip.setPixelColor()
```

---

## 5. Tool 7 Redesign — 3 Pages

### 5.1 Page Structure

| Page | Name | Content | Toggle |
|------|------|---------|--------|
| 0 | DISPLAY | Timing parameters only (pulse period, tick flash duration) | `t` |
| 1 | COLOR | 13 color slots with preset + hue + intensity — unified view | `t` |
| 2 | CONFIRM | Blink counts, durations, fades | `t` |

### 5.2 Page COLOR — Layout

Standard VT100 navigation: up/down = slot, left/right = adjust active field, TAB = cycle fields (Preset / Hue / Intensity), `t` = next page, `d` = reset defaults, `q` = exit, `b` = preview blink/flash.

```
+============================================================================+
|  [ILLPAD48 SETUP CONSOLE]   TOOL 7: LED SETTINGS - COLOR        [NVS:OK]  |
+============================================================================+
|                                                                            |
+-- DISPLAY ----------------------------------------------------------------+
|                                                                            |
|     Slot              Preset          Hue    Intensity                     |
|     -------------------------------------------------                     |
|  >  NORMAL fg         Pure White       +0'     85%                        |
|     NORMAL bg         Warm White       +0'     12%                        |
|     ARPEG fg min      Ice Blue         +0'     30%                        |
|     ARPEG fg max      Ice Blue         +0'    100%                        |
|     ARPEG bg min      Deep Blue        +0'      8%                        |
|     ARPEG bg max      Deep Blue        +0'     25%                        |
|     Tick flash fg     Pure White       +0'    100%                        |
|     Tick flash bg     Pure White       +0'     25%                        |
|     -------------------------------------------------                     |
|     Bank switch       Pure White       +0'     80%                        |
|     Scale root        Amber            +0'    100%                        |
|     Scale mode        Gold             +0'    100%                        |
|     Scale chromatic   Gold             +0'    100%                        |
|     Hold              Deep Blue        +0'    100%                        |
|     Play ack          Green            +0'    100%                        |
|     Stop              Cyan             +0'    100%                        |
|     Octave            Violet           +0'    100%                        |
|                                                                            |
+-- INFO -------------------------------------------------------------------+
|                                                                            |
|  NORMAL foreground - solid color of the active bank's LED.                 |
|  This is the color you see when playing on a NORMAL bank. Brightness sets  |
|  how strong it appears. Lower values (5-15%) give a subtle glow for dark   |
|  stages. Use the rear pot to control the overall master brightness.        |
|                                                                            |
+============================================================================+
|  [<>] adjust   [TAB] field   [b] preview   [t] page   [d] defaults        |
+============================================================================+
```

(ASCII approximation — actual rendering uses Unicode box drawing per VT100 design guide.)

ARPEG slots are doubled: fg min/max and bg min/max as separate lines. Changing the color preset on one propagates to its partner (same color, different intensities). Total: 16 lines.

The color swatch (block character) next to each preset name uses ANSI 24-bit color (`\e[38;2;r;g;bm`) for a terminal approximation. The strip is the authoritative reference.

### 5.3 INFO Section — Mini User Manual

The INFO section follows the cursor and acts as an integrated user manual. Each slot has a 2-3 line description written in functional/musical terms, not technical terms.

Examples:

- **NORMAL fg:** "Solid color of the active bank's LED. This is what you see when playing on a NORMAL bank. Lower intensities (5-15%) give a subtle glow for dark stages."
- **ARPEG fg min:** "Lowest point of the breathing pulse on the active arpeggiator. Set low (3-8%) for a subtle breath that barely glows, higher (20-30%) for an always-visible pulse."
- **ARPEG fg max:** "Peak of the breathing pulse on the active arpeggiator. The difference between min and max defines the depth of the breathing effect."
- **Tick flash fg:** "Brief white spike on each arpeggiator step, foreground bank. Punctuates the rhythm. Try Amber or Coral for a warmer rhythmic feel."
- **Bank switch:** "Blink pattern when switching banks. Color, number of blinks, and duration are configured across COLOR and CONFIRM pages."
- **Hold:** "Flash when toggling HOLD mode on an arpeggiator bank. A quick visual acknowledgment of the hold state change."

### 5.4 Live Preview on LEDs 3-4

During editing, only LEDs 3 and 4 (center of the 8-LED stick) are active. All other LEDs are off. The strip returns to normal display when leaving Tool 7.

#### Continuous preview (no `b` needed)

For slots with static or continuous rendering, the preview runs automatically:

| Cursor on | LED 3 (bg) | LED 4 (fg) |
|---|---|---|
| NORMAL fg | NORMAL bg, solid | NORMAL fg, solid |
| NORMAL bg | NORMAL bg, solid | NORMAL fg, solid |
| ARPEG fg min/max | ARPEG bg, sine pulse (real params) | ARPEG fg, sine pulse (real params) |
| ARPEG bg min/max | ARPEG bg, sine pulse (real params) | ARPEG fg, sine pulse (real params) |

Both LEDs always show the fg/bg couple. When adjusting any parameter (color, hue, intensity), the LEDs update in real time. The sine pulse uses the actual configured period and min/max values.

#### Preview with `b` (event-based slots)

For slots that represent one-shot events (confirmations, tick flash), LEDs are off until `b` is pressed. Then one cycle plays with full context:

| Slot | LED 3 | LED 4 | What `b` plays |
|---|---|---|---|
| Tick flash fg | ARPEG bg pulse (context) | ARPEG fg pulse + flash spike | Single flash on the fg pulse |
| Tick flash bg | ARPEG bg pulse + flash spike | ARPEG fg pulse (context) | Single flash on the bg pulse |
| Bank switch | Old bank fg -> fades to bg | New bank blinks | Transition: old dims, new blinks |
| Scale root/mode/chrom | -- | Blink on solid NORMAL fg | Blink overlaid on normal display |
| Hold ON | ARPEG bg pulse (context) | Blink -> ARPEG fg pulse resumes | Hold flash interrupts the pulse |
| Hold OFF | ARPEG bg pulse (context) | Fade-out -> ARPEG fg pulse resumes | Fade overlaid on the pulse |
| Play ack | ARPEG bg pulse (other bank) | Green ack -> beat flashes -> fg pulse starts | Full play sequence |
| Stop | ARPEG bg pulse (other bank) | Fg pulse running -> fade-out -> stopped pulse | Transition playing->stopped |
| Octave | Blink together | Blink together | 2-LED group blink (natural fit) |

After `b` completes, LEDs return to off (event slots) or resume continuous preview (if cursor moves to a continuous slot).

#### Tick flash simulation

In setup mode, no ArpEngine is running. Tick flash preview uses a simulated timer derived from stored tempo and division:

```
tickIntervalMs = (60000 / bpm) * (4 / division)
```

Fires one flash per `b` press at the configured duration and intensity.

#### Play ack simulation

Uses the existing code fallback for missing ClockManager: 200ms per beat, no real MIDI sync. Sequence: green ack flash (100ms) -> pause -> N beat-synced flashes with rising intensity.

---

## 6. NVS Storage

### 6.1 ColorSlotStore (new)

```cpp
struct ColorSlotStore {
    uint16_t magic;        // 0xC010
    uint8_t  version;      // 1
    uint8_t  reserved;
    struct Slot {
        uint8_t presetId;  // 0-13 (index into PRESETS[])
        int8_t  hueOffset; // -128 to +127 degrees
    } slots[13];
};
// 30 bytes total
```

NVS namespace: `illpad_lset`, key: `"ledcolors"`.

### 6.2 LedSettingsStore (rewritten, fresh v1)

All intensities stored as 0-100 (perceptual %). No backward compatibility with previous versions.

```cpp
struct LedSettingsStore {
    uint16_t magic;              // 0xBEEF
    uint8_t  version;            // 1
    uint8_t  reserved;
    // Intensities (0-100, perceptual %)
    uint8_t normalFgIntensity;   // default: 85
    uint8_t normalBgIntensity;   // default: 10
    uint8_t fgArpStopMin;        // default: 30
    uint8_t fgArpStopMax;        // default: 100
    uint8_t fgArpPlayMin;        // default: 30
    uint8_t fgArpPlayMax;        // default: 80
    uint8_t bgArpStopMin;        // default: 8
    uint8_t bgArpStopMax;        // default: 25
    uint8_t bgArpPlayMin;        // default: 8
    uint8_t bgArpPlayMax;        // default: 20
    uint8_t tickFlashFg;         // default: 100
    uint8_t tickFlashBg;         // default: 25
    // Timing
    uint16_t pulsePeriodMs;      // default: 1472
    uint8_t  tickFlashDurationMs;// default: 30
    // Confirmations
    uint8_t  bankBlinks;         // default: 3 (1-3)
    uint16_t bankDurationMs;     // default: 300
    uint8_t  bankBrightnessPct;  // default: 80
    uint8_t  scaleRootBlinks;    // default: 2
    uint16_t scaleRootDurationMs;// default: 200
    uint8_t  scaleModeBlinks;    // default: 2
    uint16_t scaleModeDurationMs;// default: 200
    uint8_t  scaleChromBlinks;   // default: 2
    uint16_t scaleChromDurationMs;// default: 200
    uint8_t  holdOnFlashMs;      // default: 150
    uint16_t holdFadeMs;         // default: 300
    uint8_t  playBeatCount;      // default: 3
    uint16_t stopFadeMs;         // default: 300
    uint8_t  octaveBlinks;       // default: 3
    uint16_t octaveDurationMs;   // default: 300
};
```

NVS namespace: `illpad_lset`, key: `"ledsettings"`.

### 6.3 What Is NOT in NVS

- Preset palette (const in flash, indexed by ID)
- Gamma LUT (const in flash)
- Brightness pot curve LUT (const in flash)
- System colors: error, boot, battery (hardcoded)

---

## 7. What Gets Removed

| Removed | Reason |
|---------|--------|
| `struct RGB` | Replaced by `RGBW` |
| `setPixelAbsolute()` | Unified into single `setPixel()` |
| `setPixelAbsoluteScaled()` | Unified into single `setPixel()` |
| `_absoluteMax` parameter | No longer needed — brightness pot scales everything |
| "Absolute Max" in Tool 7 | Removed from UI |
| NVS migration code | Fresh start, no backward compat |
| `NEO_GRB` | Replaced by `NEO_GRBW` |

---

## 8. What Stays Unchanged

- LED data pin (GPIO 13), pixel count (8)
- Sine LUT (256 entries, precomputed at boot)
- Priority state machine in `update()` (boot > setup > chase > error > battery > bargraph > confirm > normal)
- Confirmation trigger API (`triggerConfirm()`)
- Flash-on-save header pulse
- Tool 7 pages DISPLAY (timing only now) and CONFIRM (unchanged)
- Setup comet, chase pattern, calibration mode rendering

---

## 9. Hardcoded System Colors

These are not editable and bypass the preset system:

| Use | Color | Reason |
|-----|-------|--------|
| Error (sensing stall) | Red RGBW(255,0,0,0) | Safety — must be unmistakable |
| Boot progress | White RGBW(0,0,0,255) | Clean W channel white |
| Boot failure | Red RGBW(255,0,0,0) | Matches error |
| Battery gauge | Red-to-green gradient | Semantic (universal convention) |
| Setup comet | Violet RGBW(128,0,255,0) | Distinct from all user colors |
