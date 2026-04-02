# SK6812 RGBW LED Overhaul — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace WS2812 RGB with SK6812 RGBW, add gamma correction, named color presets with hue shift, perceptual intensity scale, unified brightness pipeline, and redesigned Tool 7 with live preview.

**Architecture:** RGBW struct replaces RGB everywhere. A gamma LUT (256 entries, gamma 2.8) is applied at the final output stage. All user-facing intensities are 0-100% perceptual. A single `setPixel()` method replaces the 4 current methods. Color presets (14 RGBW values in flash) are referenced by index + hue offset, stored in NVS as 2 bytes per slot (13 slots). Tool 7 gains a COLOR page with live preview on LEDs 3-4.

**Tech Stack:** C++17, Arduino framework, PlatformIO, ESP32-S3, Adafruit_NeoPixel (NEO_GRBW), FreeRTOS

**Spec:** `docs/superpowers/specs/2026-03-30-sk6812-rgbw-led-overhaul-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `src/core/HardwareConfig.h` | Rewrite | RGBW struct, RGBW color constants, gamma LUT, perceptual LUT, pot curve LUT, remove old LED_* intensity constants |
| `src/core/KeyboardData.h` | Rewrite LED section | LedSettingsStore v1 fresh (0-100%), ColorSlotStore, preset palette + names |
| `src/core/LedController.h` | Rewrite | Single `setPixel()`, RGBW members, remove absolute methods, add color slot resolution |
| `src/core/LedController.cpp` | Rewrite | NEO_GRBW init, unified pipeline, gamma output, updated renderNormalDisplay with color slots, updated renderConfirmation with color slots |
| `src/setup/ToolLedSettings.h` | Rewrite | 3-page state, color working copy, preview state machine |
| `src/setup/ToolLedSettings.cpp` | Rewrite | 3 pages (DISPLAY/COLOR/CONFIRM), live preview, `b` key preview, info-as-manual |
| `src/managers/NvsManager.h` | Modify | Add ColorSlotStore member + getter |
| `src/managers/NvsManager.cpp` | Modify | Load/save ColorSlotStore in loadAll(), update LED settings defaults to 0-100 |
| `src/main.cpp` | Modify | Pass color slots to LedController at boot, pot curve for brightness |

---

## Task 1: RGBW Type + Color Constants + LUTs (HardwareConfig.h)

**Files:**
- Modify: `src/core/HardwareConfig.h:38-70` (RGB struct + COL_* constants)
- Modify: `src/core/HardwareConfig.h:224-262` (LED_* intensity constants)

- [ ] **Step 1: Replace struct RGB with struct RGBW**

Replace lines 38-40:

```cpp
struct RGBW {
  uint8_t r, g, b, w;
};
```

- [ ] **Step 2: Rewrite all COL_* constants as RGBW**

Replace lines 44-70 with RGBW versions. System colors stay hardcoded (these are used by boot, error, battery — not editable):

```cpp
// System colors (hardcoded, not editable via Tool 7)
static constexpr RGBW COL_ERROR      = {255,   0,   0,   0};
static constexpr RGBW COL_BOOT       = {  0,   0,   0, 255};  // Clean W white
static constexpr RGBW COL_BOOT_FAIL  = {255,   0,   0,   0};
static constexpr RGBW COL_SETUP      = {128,   0, 255,   0};  // Violet comet

// Battery gauge gradient (8 LEDs, red->green) — no W channel
static constexpr RGBW COL_BATTERY[NUM_LEDS] = {
  {255,   0, 0, 0}, {255,  36, 0, 0}, {255,  73, 0, 0}, {255, 145, 0, 0},
  {200, 200, 0, 0}, {145, 255, 0, 0}, { 73, 255, 0, 0}, {  0, 255, 0, 0}
};
```

Remove all `COL_WHITE`, `COL_WHITE_DIM`, `COL_BLUE`, `COL_BLUE_DIM`, `COL_SCALE_*`, `COL_ARP_*`, `COL_PLAY_ACK` — these are now resolved from color slots at runtime.

- [ ] **Step 3: Add gamma LUT (compile-time, gamma 2.8)**

Add after the color constants:

```cpp
// Gamma correction LUT — gamma 2.8, applied per-channel at output stage
// Generated: round(255 * (i/255)^2.8) for i in 0..255
static const uint8_t GAMMA_LUT[256] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   3,
    3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,   7,
    7,   7,   8,   8,   9,   9,   9,  10,  10,  11,  11,  12,  12,  13,  13,  14,
   14,  15,  15,  16,  17,  17,  18,  18,  19,  20,  20,  21,  22,  23,  23,  24,
   25,  26,  26,  27,  28,  29,  30,  31,  31,  32,  33,  34,  35,  36,  37,  38,
   39,  40,  41,  42,  43,  44,  46,  47,  48,  49,  50,  51,  53,  54,  55,  56,
   58,  59,  60,  62,  63,  64,  66,  67,  69,  70,  72,  73,  75,  76,  78,  79,
   81,  82,  84,  86,  87,  89,  91,  92,  94,  96,  98,  99, 101, 103, 105, 107,
  109, 110, 112, 114, 116, 118, 120, 122, 124, 126, 128, 130, 133, 135, 137, 139,
  141, 143, 146, 148, 150, 152, 155, 157, 159, 162, 164, 166, 169, 171, 174, 176,
  179, 181, 184, 186, 189, 191, 194, 196, 199, 202, 204, 207, 210, 212, 215, 218,
  220, 223, 226, 229, 232, 234, 237, 240, 243, 246, 249, 252, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};
```

NOTE: The exact values above are approximate. The implementer MUST generate correct values with: `round(255 * pow(i / 255.0, 2.8))` for i in 0..255, and verify `GAMMA_LUT[0]=0`, `GAMMA_LUT[255]=255`.

- [ ] **Step 4: Add perceptual-to-linear LUT (101 entries, 0-100% -> 0-255 linear)**

```cpp
// Perceptual % (0-100) to linear (0-255), inverse gamma 2.8
// Generated: round(255 * pow(i / 100.0, 1.0 / 2.8)) for i in 0..100
static const uint8_t PERCEPTUAL_TO_LINEAR[101] = {
    0,  21,  28,  33,  37,  41,  44,  47,  50,  52,  55,  57,  59,  61,  63,  65,
   67,  69,  71,  72,  74,  76,  77,  79,  80,  82,  83,  85,  86,  88,  89,  90,
   92,  93,  94,  96,  97,  98, 100, 101, 102, 103, 105, 106, 107, 108, 109, 111,
  112, 113, 114, 115, 116, 117, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128,
  129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144,
  145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160,
  161, 162, 163, 164, 255
};
```

NOTE: Same as gamma — the implementer MUST generate correct values and verify `[0]=0`, `[100]=255`.

- [ ] **Step 5: Add brightness pot curve LUT + compile-time selector**

```cpp
// Brightness pot response curve
#define POT_CURVE_LOW_BIASED  0
#define POT_CURVE_LINEAR      1
#define POT_CURVE_SIGMOID     2

// Select curve (change this to switch behavior):
//   LOW_BIASED  — bottom half covers 0-25% perceived, ideal for dark stages
//   LINEAR      — uniform perceptual steps
//   SIGMOID     — precision at both extremes
#define BRIGHTNESS_POT_CURVE  POT_CURVE_LOW_BIASED

// 256-entry LUT: ADC value (0-255) -> perceived brightness (0-100)
// Generated per curve type. Only the selected curve is compiled.
#if BRIGHTNESS_POT_CURVE == POT_CURVE_LOW_BIASED
static const uint8_t POT_BRIGHTNESS_CURVE[256] = { /* low-biased values */ };
#elif BRIGHTNESS_POT_CURVE == POT_CURVE_LINEAR
static const uint8_t POT_BRIGHTNESS_CURVE[256] = { /* linear values */ };
#elif BRIGHTNESS_POT_CURVE == POT_CURVE_SIGMOID
static const uint8_t POT_BRIGHTNESS_CURVE[256] = { /* sigmoid values */ };
#endif
```

The implementer must generate all 3 LUTs:
- LOW_BIASED: `100 * pow(i/255.0, 2.0)` (quadratic — bottom half → 0-25%)
- LINEAR: `round(i * 100.0 / 255.0)`
- SIGMOID: `100 / (1 + exp(-10 * (i/255.0 - 0.5)))` (logistic, steepness 10)

- [ ] **Step 6: Remove old LED_* intensity constants**

Delete lines 224-262 (all `LED_FG_ARP_*`, `LED_BG_ARP_*`, `LED_PULSE_PERIOD_MS`, `LED_TICK_FLASH_DURATION_MS`, `LED_CONFIRM_*`, `LED_ABSOLUTE_MAX`, `LED_SETUP_CHASE_SPEED_MS`, `LED_BARGRAPH_DURATION_*`).

Keep only `LED_DATA_PIN` and `NUM_LEDS`.

Re-add timing/chase/bargraph as simple defaults (these move to the settings store or stay as compile-time defaults):

```cpp
// Setup-mode LED timing (not user-configurable)
static constexpr uint16_t LED_SETUP_CHASE_SPEED_MS = 80;
static constexpr uint16_t LED_BARGRAPH_DEFAULT_DURATION_MS = 3000;
```

- [ ] **Step 7: Compile check**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | head -30`

Expected: Compilation errors in LedController.cpp (references removed types/constants). This is expected — Task 1 is infrastructure only. Proceed to Task 2.

- [ ] **Step 8: Commit**

```bash
git add src/core/HardwareConfig.h
git commit -m "feat(led): RGBW type, gamma LUT, perceptual LUT, pot curve — infrastructure for SK6812 overhaul"
```

---

## Task 2: Data Structures (KeyboardData.h)

**Files:**
- Modify: `src/core/KeyboardData.h:118-162` (LedSettingsStore + defines)

- [ ] **Step 1: Add color preset palette and names**

Add above the LED Settings section:

```cpp
// =================================================================
// Color Preset Palette (const, in flash — not NVS)
// =================================================================
static constexpr uint8_t COLOR_PRESET_COUNT = 14;

static constexpr RGBW COLOR_PRESETS[COLOR_PRESET_COUNT] = {
  {  0,   0,   0, 255},  //  0: Pure White
  { 40,  20,   0, 200},  //  1: Warm White
  {  0,  10,  30, 220},  //  2: Cool White
  {  0,  20, 180,  40},  //  3: Ice Blue
  {  0,   0, 255,   0},  //  4: Deep Blue
  {  0, 180, 200,  20},  //  5: Cyan
  {200,  80,   0,  60},  //  6: Amber
  {255, 140,   0,  30},  //  7: Gold
  {255,  60,  30,  20},  //  8: Coral
  {100,   0, 255,   0},  //  9: Violet
  {200,   0, 180,  10},  // 10: Magenta
  {  0, 255,   0,   0},  // 11: Green
  {180,  60,  40,  80},  // 12: Soft Peach
  { 60, 200,  60,  40},  // 13: Mint
};

static const char* const COLOR_PRESET_NAMES[COLOR_PRESET_COUNT] = {
  "Pure White", "Warm White", "Cool White", "Ice Blue",
  "Deep Blue", "Cyan", "Amber", "Gold", "Coral",
  "Violet", "Magenta", "Green", "Soft Peach", "Mint"
};
```

- [ ] **Step 2: Add ColorSlotStore struct**

```cpp
// =================================================================
// Color Slots (Tool 7 COLOR page)
// =================================================================
#define COLOR_SLOT_COUNT       13
#define COLOR_SLOT_NVS_KEY     "ledcolors"
#define COLOR_SLOT_MAGIC       0xC010

// Slot index enum for readability
enum ColorSlotId : uint8_t {
  CSLOT_NORMAL_FG = 0,
  CSLOT_NORMAL_BG,
  CSLOT_ARPEG_FG,
  CSLOT_ARPEG_BG,
  CSLOT_TICK_FLASH,
  CSLOT_BANK_SWITCH,
  CSLOT_SCALE_ROOT,
  CSLOT_SCALE_MODE,
  CSLOT_SCALE_CHROM,
  CSLOT_HOLD,
  CSLOT_PLAY_ACK,
  CSLOT_STOP,
  CSLOT_OCTAVE
};

struct ColorSlot {
  uint8_t presetId;   // 0..(COLOR_PRESET_COUNT-1)
  int8_t  hueOffset;  // -128..+127 degrees
};

struct ColorSlotStore {
  uint16_t  magic;     // COLOR_SLOT_MAGIC
  uint8_t   version;   // 1
  uint8_t   reserved;
  ColorSlot slots[COLOR_SLOT_COUNT];
};
```

- [ ] **Step 3: Rewrite LedSettingsStore — fresh v1, all 0-100%**

Replace lines 118-162:

```cpp
// =================================================================
// LED Settings (Tool 7 DISPLAY + CONFIRM pages)
// =================================================================
#define LED_SETTINGS_NVS_NAMESPACE "illpad_lset"
#define LED_SETTINGS_NVS_KEY       "ledsettings"
#define LED_SETTINGS_VERSION       1   // Fresh start — no migration

struct LedSettingsStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  // --- Intensities (0-100, perceptual %) ---
  uint8_t  normalFgIntensity;     // default 85
  uint8_t  normalBgIntensity;     // default 10
  uint8_t  fgArpStopMin;          // default 30
  uint8_t  fgArpStopMax;          // default 100
  uint8_t  fgArpPlayMin;          // default 30
  uint8_t  fgArpPlayMax;          // default 80
  uint8_t  bgArpStopMin;          // default 8
  uint8_t  bgArpStopMax;          // default 25
  uint8_t  bgArpPlayMin;          // default 8
  uint8_t  bgArpPlayMax;          // default 20
  uint8_t  tickFlashFg;           // default 100
  uint8_t  tickFlashBg;           // default 25
  // --- Timing ---
  uint16_t pulsePeriodMs;         // default 1472
  uint8_t  tickFlashDurationMs;   // default 30
  // --- Confirmations ---
  uint8_t  bankBlinks;            // 1-3, default 3
  uint16_t bankDurationMs;        // default 300
  uint8_t  bankBrightnessPct;     // default 80
  uint8_t  scaleRootBlinks;       // default 2
  uint16_t scaleRootDurationMs;   // default 200
  uint8_t  scaleModeBlinks;       // default 2
  uint16_t scaleModeDurationMs;   // default 200
  uint8_t  scaleChromBlinks;      // default 2
  uint16_t scaleChromDurationMs;  // default 200
  uint8_t  holdOnFlashMs;         // default 150
  uint16_t holdFadeMs;            // default 300
  uint8_t  playBeatCount;         // default 3
  uint16_t stopFadeMs;            // default 300
  uint8_t  octaveBlinks;          // default 3
  uint16_t octaveDurationMs;      // default 300
};
```

- [ ] **Step 4: Add HSV<->RGB conversion utility (for hue shift)**

Add at the end of KeyboardData.h (or a new `ColorUtils.h` — see note):

```cpp
// =================================================================
// Color utilities — HSV rotation for hue shift
// =================================================================

// Resolve a color slot to its final RGBW value
// 1. Look up preset RGBW
// 2. If hueOffset != 0: convert RGB to HSV, rotate H, convert back
// 3. W channel is unchanged by hue rotation
inline RGBW resolveColorSlot(const ColorSlot& slot) {
  RGBW base = COLOR_PRESETS[slot.presetId < COLOR_PRESET_COUNT ? slot.presetId : 0];
  if (slot.hueOffset == 0) return base;

  // RGB -> HSV (H in 0-360, S/V in 0-255)
  uint8_t maxC = max(max(base.r, base.g), base.b);
  uint8_t minC = min(min(base.r, base.g), base.b);
  uint8_t delta = maxC - minC;
  int16_t h = 0;
  uint8_t s = (maxC == 0) ? 0 : (uint8_t)((uint16_t)delta * 255 / maxC);
  uint8_t v = maxC;

  if (delta > 0) {
    if (maxC == base.r)      h = 60 * (int16_t)(base.g - base.b) / delta;
    else if (maxC == base.g) h = 120 + 60 * (int16_t)(base.b - base.r) / delta;
    else                     h = 240 + 60 * (int16_t)(base.r - base.g) / delta;
    if (h < 0) h += 360;
  }

  // Rotate hue
  h = (h + (int16_t)slot.hueOffset + 360) % 360;

  // HSV -> RGB
  uint8_t hi = h / 60;
  uint8_t f = (uint8_t)((uint32_t)(h % 60) * 255 / 60);
  uint8_t p = (uint8_t)((uint16_t)v * (255 - s) / 255);
  uint8_t q = (uint8_t)((uint16_t)v * (255 - (uint16_t)s * f / 255) / 255);
  uint8_t t = (uint8_t)((uint16_t)v * (255 - (uint16_t)s * (255 - f) / 255) / 255);

  RGBW result = {0, 0, 0, base.w};  // W unchanged
  switch (hi) {
    case 0: result.r = v; result.g = t; result.b = p; break;
    case 1: result.r = q; result.g = v; result.b = p; break;
    case 2: result.r = p; result.g = v; result.b = t; break;
    case 3: result.r = p; result.g = q; result.b = v; break;
    case 4: result.r = t; result.g = p; result.b = v; break;
    default: result.r = v; result.g = p; result.b = q; break;
  }
  return result;
}
```

Note: This is `inline` in the header for simplicity. If the implementer prefers, it can live in a separate `ColorUtils.h` included where needed.

- [ ] **Step 5: Commit**

```bash
git add src/core/KeyboardData.h
git commit -m "feat(led): ColorSlotStore, LedSettingsStore v1 fresh, preset palette, hue shift resolver"
```

---

## Task 3: LedController — Unified Pipeline (Header + Core Rendering)

**Files:**
- Modify: `src/core/LedController.h:90-157` (private members)
- Modify: `src/core/LedController.cpp:14` (NEO_GRB -> NEO_GRBW)
- Modify: `src/core/LedController.cpp:91-125` (setPixel methods)
- Modify: `src/core/LedController.cpp:558-677` (renderNormalDisplay)
- Modify: `src/core/LedController.cpp:350-534` (renderConfirmation)
- Modify: `src/core/LedController.cpp:726-758` (loadLedSettings)

- [ ] **Step 1: Update LedController.h — single setPixel, RGBW members, color slot storage**

Replace the private section (lines 90-157):

```cpp
private:
  Adafruit_NeoPixel _strip;

  // Unified pixel setter: takes perceptual intensity (0-100%),
  // combines with master brightness pot, converts to linear, applies gamma
  void setPixel(uint8_t i, const RGBW& color, uint8_t intensityPct);
  void clearPixels();

  // Render helpers (priority-based, called from update())
  void renderBoot(unsigned long now);
  void renderComet(unsigned long now);
  void renderChase(unsigned long now);
  void renderError(unsigned long now);
  bool renderBattery(unsigned long now);
  bool renderBargraph(unsigned long now);
  bool renderConfirmation(unsigned long now);
  void renderCalibration(unsigned long now);
  void renderNormalDisplay(unsigned long now);

  // Master brightness (from pot, 0-100 perceptual via pot curve)
  uint8_t _brightnessPct;  // 0-100

  // Bank display
  uint8_t _currentBank;
  bool _batteryLow;

  // Multi-bank state
  const BankSlot* _slots;

  // Resolved colors (from ColorSlotStore, resolved at load time)
  RGBW _colNormalFg, _colNormalBg;
  RGBW _colArpFg, _colArpBg;
  RGBW _colTickFlash;
  RGBW _colBankSwitch, _colScaleRoot, _colScaleMode, _colScaleChrom;
  RGBW _colHold, _colPlayAck, _colStop, _colOctave;

  // LED settings (0-100 perceptual, converted to linear at use)
  uint8_t  _normalFgIntensity;
  uint8_t  _normalBgIntensity;
  uint8_t  _fgArpStopMin, _fgArpStopMax;
  uint8_t  _fgArpPlayMin, _fgArpPlayMax;
  uint8_t  _bgArpStopMin, _bgArpStopMax;
  uint8_t  _bgArpPlayMin, _bgArpPlayMax;
  uint8_t  _tickFlashFg, _tickFlashBg;
  uint16_t _pulsePeriodMs;
  uint8_t  _tickFlashDurationMs;
  uint8_t  _bankBlinks;
  uint16_t _bankDurationMs;
  uint8_t  _bankBrightnessPct;
  uint8_t  _scaleRootBlinks;
  uint16_t _scaleRootDurationMs;
  uint8_t  _scaleModeBlinks;
  uint16_t _scaleModeDurationMs;
  uint8_t  _scaleChromBlinks;
  uint16_t _scaleChromDurationMs;
  uint8_t  _holdOnFlashMs;
  uint16_t _holdFadeMs;
  uint16_t _stopFadeMs;
  uint8_t  _playBeatCount;
  uint8_t  _octaveBlinks;
  uint16_t _octaveDurationMs;

  uint8_t _sineTable[256];
  unsigned long _flashStartTime[NUM_LEDS];

  const ClockManager* _clock;
```

Update the public interface:
- Change `setBrightness(uint8_t brightness)` to `setBrightness(uint8_t potValue)` — applies pot curve internally
- Add `loadColorSlots(const ColorSlotStore& store)` alongside existing `loadLedSettings()`

- [ ] **Step 2: Rewrite NeoPixel init + setPixel (LedController.cpp)**

Line 14 — change `NEO_GRB` to `NEO_GRBW`:

```cpp
: _strip(NUM_LEDS, LED_DATA_PIN, NEO_GRBW + NEO_KHZ800),
```

Replace the 4 setPixel methods (lines 91-125) with one:

```cpp
void LedController::setPixel(uint8_t i, const RGBW& c, uint8_t intensity) {
  // Combine intensity with master brightness
  uint16_t combinedPct = (uint16_t)intensity * _brightnessPct / 100;
  // Convert perceptual to linear (clamp to 100)
  uint8_t linear = PERCEPTUAL_TO_LINEAR[combinedPct > 100 ? 100 : combinedPct];
  // Scale color channels and apply gamma
  _strip.setPixelColor(i,
    GAMMA_LUT[(uint16_t)c.r * linear / 255],
    GAMMA_LUT[(uint16_t)c.g * linear / 255],
    GAMMA_LUT[(uint16_t)c.b * linear / 255],
    GAMMA_LUT[(uint16_t)c.w * linear / 255]
  );
}
```

Update `setBrightness()`:

```cpp
void LedController::setBrightness(uint8_t potValue) {
  _brightnessPct = POT_BRIGHTNESS_CURVE[potValue];
}
```

- [ ] **Step 3: Rewrite renderNormalDisplay() with color slots**

Replace lines 558-677. The logic is the same but uses `_colNormalFg`/`_colArpFg` etc. instead of hardcoded `COL_WHITE`/`COL_BLUE`:

```cpp
void LedController::renderNormalDisplay(unsigned long now) {
  clearPixels();
  if (!_slots) return;

  const uint8_t lutStep = _pulsePeriodMs / 256;
  uint8_t sineIdx = (uint8_t)((now / (lutStep > 0 ? lutStep : 1)) % 256);
  uint8_t sineRaw = _sineTable[sineIdx];

  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    const BankSlot& slot = _slots[i];
    bool isFg = (i == _currentBank);

    if (slot.type == BANK_NORMAL) {
      // NORMAL: solid color at configured intensity
      setPixel(i, isFg ? _colNormalFg : _colNormalBg,
               isFg ? _normalFgIntensity : _normalBgIntensity);
    } else {
      // ARPEG: sine pulse + optional tick flash
      bool playing = slot.arpEngine && slot.arpEngine->isPlaying() && slot.arpEngine->hasNotes();

      if (slot.arpEngine && slot.arpEngine->consumeTickFlash()) {
        _flashStartTime[i] = now;
      }
      bool flashing = (_flashStartTime[i] != 0) &&
                       ((now - _flashStartTime[i]) < _tickFlashDurationMs);

      // Pick min/max and color based on fg/bg and play state
      uint8_t pMin, pMax;
      RGBW col;
      if (isFg) {
        col = _colArpFg;
        pMin = playing ? _fgArpPlayMin : _fgArpStopMin;
        pMax = playing ? _fgArpPlayMax : _fgArpStopMax;
      } else {
        col = _colArpBg;
        pMin = playing ? _bgArpPlayMin : _bgArpStopMin;
        pMax = playing ? _bgArpPlayMax : _bgArpStopMax;
      }

      if (flashing) {
        // Tick flash overrides pulse
        uint8_t flashInt = isFg ? _tickFlashFg : _tickFlashBg;
        setPixel(i, _colTickFlash, flashInt);
      } else {
        // Sine-modulated intensity between min and max
        uint8_t intensity = pMin + (uint8_t)((uint16_t)sineRaw * (pMax - pMin) / 255);
        setPixel(i, col, intensity);
      }
    }
  }
  // ... battery low override and bank switch overlay remain (update their colors too)
}
```

The implementer must also update the battery low blink and bank switch overlay sections to use `_colNormalFg`/`_colArpFg` with `setPixel()` instead of the old methods.

- [ ] **Step 4: Update renderConfirmation() with color slots**

In `renderConfirmation()` (lines 350-534), replace all hardcoded `COL_SCALE_ROOT`, `COL_ARP_HOLD`, etc. with the resolved color members:

| Old | New |
|-----|-----|
| `COL_WHITE` / `COL_BLUE` (bank switch context) | `_colBankSwitch` |
| `COL_SCALE_ROOT` | `_colScaleRoot` |
| `COL_SCALE_MODE` | `_colScaleMode` |
| `COL_SCALE_CHROM` | `_colScaleChrom` |
| `COL_ARP_HOLD` | `_colHold` |
| `COL_PLAY_ACK` | `_colPlayAck` |
| `COL_ARP_PLAY` (stop fade) | `_colStop` |
| `COL_ARP_OCTAVE` | `_colOctave` |

Replace all `setPixelAbsolute()` / `setPixelAbsoluteScaled()` calls with `setPixel()` using the slot's configured intensity. The old absolute brightness bypass is gone — everything goes through the unified pipeline.

- [ ] **Step 5: Update renderBoot, renderComet, renderError, renderBattery, renderBargraph**

These use system colors (hardcoded). Update them to:
- Use `RGBW` type instead of `RGB`
- Use `setPixel(i, color, intensity)` instead of old methods
- `renderError()`: use `COL_ERROR` with intensity 100 (no floor — respects pot)
- `renderBoot()`: use `COL_BOOT` (W channel white)
- `renderBattery()`: use `COL_BATTERY[i]`
- `renderComet()`: use `COL_SETUP`

- [ ] **Step 6: Rewrite loadLedSettings() and add loadColorSlots()**

```cpp
void LedController::loadLedSettings(const LedSettingsStore& s) {
  _normalFgIntensity = s.normalFgIntensity;
  _normalBgIntensity = s.normalBgIntensity;
  _fgArpStopMin = s.fgArpStopMin;
  _fgArpStopMax = s.fgArpStopMax;
  _fgArpPlayMin = s.fgArpPlayMin;
  _fgArpPlayMax = s.fgArpPlayMax;
  _bgArpStopMin = s.bgArpStopMin;
  _bgArpStopMax = s.bgArpStopMax;
  _bgArpPlayMin = s.bgArpPlayMin;
  _bgArpPlayMax = s.bgArpPlayMax;
  _tickFlashFg = s.tickFlashFg;
  _tickFlashBg = s.tickFlashBg;
  _pulsePeriodMs = s.pulsePeriodMs;
  _tickFlashDurationMs = s.tickFlashDurationMs;
  _bankBlinks = s.bankBlinks;
  _bankDurationMs = s.bankDurationMs;
  _bankBrightnessPct = s.bankBrightnessPct;
  _scaleRootBlinks = s.scaleRootBlinks;
  _scaleRootDurationMs = s.scaleRootDurationMs;
  _scaleModeBlinks = s.scaleModeBlinks;
  _scaleModeDurationMs = s.scaleModeDurationMs;
  _scaleChromBlinks = s.scaleChromBlinks;
  _scaleChromDurationMs = s.scaleChromDurationMs;
  _holdOnFlashMs = s.holdOnFlashMs;
  _holdFadeMs = s.holdFadeMs;
  _playBeatCount = s.playBeatCount;
  _stopFadeMs = s.stopFadeMs;
  _octaveBlinks = s.octaveBlinks;
  _octaveDurationMs = s.octaveDurationMs;

  // Guard min/max pairs
  if (_fgArpStopMin > _fgArpStopMax) _fgArpStopMax = _fgArpStopMin;
  if (_fgArpPlayMin > _fgArpPlayMax) _fgArpPlayMax = _fgArpPlayMin;
  if (_bgArpStopMin > _bgArpStopMax) _bgArpStopMax = _bgArpStopMin;
  if (_bgArpPlayMin > _bgArpPlayMax) _bgArpPlayMax = _bgArpPlayMin;
}

void LedController::loadColorSlots(const ColorSlotStore& store) {
  _colNormalFg   = resolveColorSlot(store.slots[CSLOT_NORMAL_FG]);
  _colNormalBg   = resolveColorSlot(store.slots[CSLOT_NORMAL_BG]);
  _colArpFg      = resolveColorSlot(store.slots[CSLOT_ARPEG_FG]);
  _colArpBg      = resolveColorSlot(store.slots[CSLOT_ARPEG_BG]);
  _colTickFlash  = resolveColorSlot(store.slots[CSLOT_TICK_FLASH]);
  _colBankSwitch = resolveColorSlot(store.slots[CSLOT_BANK_SWITCH]);
  _colScaleRoot  = resolveColorSlot(store.slots[CSLOT_SCALE_ROOT]);
  _colScaleMode  = resolveColorSlot(store.slots[CSLOT_SCALE_MODE]);
  _colScaleChrom = resolveColorSlot(store.slots[CSLOT_SCALE_CHROM]);
  _colHold       = resolveColorSlot(store.slots[CSLOT_HOLD]);
  _colPlayAck    = resolveColorSlot(store.slots[CSLOT_PLAY_ACK]);
  _colStop       = resolveColorSlot(store.slots[CSLOT_STOP]);
  _colOctave     = resolveColorSlot(store.slots[CSLOT_OCTAVE]);
}
```

- [ ] **Step 7: Compile check**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -20`

Expected: Errors in ToolLedSettings.cpp (old param ranges, old constants) and possibly NvsManager (old defaults). Main.cpp may fail too. This is expected — the core pipeline compiles, consumers are next.

- [ ] **Step 8: Commit**

```bash
git add src/core/LedController.h src/core/LedController.cpp
git commit -m "feat(led): unified RGBW pipeline — single setPixel with gamma, color slots, perceptual intensity"
```

---

## Task 4: NvsManager — Load/Save Color Slots + Updated Defaults

**Files:**
- Modify: `src/managers/NvsManager.h:98` (add ColorSlotStore member)
- Modify: `src/managers/NvsManager.cpp:32-64` (update LED defaults to 0-100)
- Modify: `src/managers/NvsManager.cpp:661-666` (add ColorSlotStore load)

- [ ] **Step 1: Add ColorSlotStore member and getter to NvsManager.h**

After the existing `LedSettingsStore _ledSettings;` member:

```cpp
ColorSlotStore _colorSlots;
```

Add public getter:

```cpp
const ColorSlotStore& getLoadedColorSlots() const { return _colorSlots; }
```

- [ ] **Step 2: Update LED settings defaults in NvsManager constructor**

Replace the defaults (lines 32-64) to use 0-100 perceptual values:

```cpp
// LED settings defaults (0-100 perceptual %)
_ledSettings.magic = EEPROM_MAGIC;
_ledSettings.version = LED_SETTINGS_VERSION;
_ledSettings.reserved = 0;
_ledSettings.normalFgIntensity = 85;
_ledSettings.normalBgIntensity = 10;
_ledSettings.fgArpStopMin = 30;
_ledSettings.fgArpStopMax = 100;
_ledSettings.fgArpPlayMin = 30;
_ledSettings.fgArpPlayMax = 80;
_ledSettings.bgArpStopMin = 8;
_ledSettings.bgArpStopMax = 25;
_ledSettings.bgArpPlayMin = 8;
_ledSettings.bgArpPlayMax = 20;
_ledSettings.tickFlashFg = 100;
_ledSettings.tickFlashBg = 25;
_ledSettings.pulsePeriodMs = 1472;
_ledSettings.tickFlashDurationMs = 30;
_ledSettings.bankBlinks = 3;
_ledSettings.bankDurationMs = 300;
_ledSettings.bankBrightnessPct = 80;
_ledSettings.scaleRootBlinks = 2;
_ledSettings.scaleRootDurationMs = 200;
_ledSettings.scaleModeBlinks = 2;
_ledSettings.scaleModeDurationMs = 200;
_ledSettings.scaleChromBlinks = 2;
_ledSettings.scaleChromDurationMs = 200;
_ledSettings.holdOnFlashMs = 150;
_ledSettings.holdFadeMs = 300;
_ledSettings.playBeatCount = 3;
_ledSettings.stopFadeMs = 300;
_ledSettings.octaveBlinks = 3;
_ledSettings.octaveDurationMs = 300;
```

Add ColorSlotStore defaults:

```cpp
// Color slot defaults
_colorSlots.magic = COLOR_SLOT_MAGIC;
_colorSlots.version = 1;
_colorSlots.reserved = 0;
// Default presets per slot
static const uint8_t defaultPresets[COLOR_SLOT_COUNT] = {
  0,  // NORMAL fg: Pure White
  1,  // NORMAL bg: Warm White
  3,  // ARPEG fg: Ice Blue
  4,  // ARPEG bg: Deep Blue
  0,  // Tick flash: Pure White
  0,  // Bank switch: Pure White
  6,  // Scale root: Amber
  7,  // Scale mode: Gold
  7,  // Scale chrom: Gold
  4,  // Hold: Deep Blue
  11, // Play ack: Green
  5,  // Stop: Cyan
  9   // Octave: Violet
};
for (uint8_t i = 0; i < COLOR_SLOT_COUNT; i++) {
  _colorSlots.slots[i].presetId = defaultPresets[i];
  _colorSlots.slots[i].hueOffset = 0;
}
```

- [ ] **Step 3: Add ColorSlotStore load in loadAll()**

After the LED settings load (line 666), add:

```cpp
loadValidatedBlob(LED_SETTINGS_NVS_NAMESPACE, COLOR_SLOT_NVS_KEY,
                   1, &_colorSlots, sizeof(_colorSlots));
```

This reuses the existing `loadValidatedBlob()` helper which checks magic + version.

- [ ] **Step 4: Commit**

```bash
git add src/managers/NvsManager.h src/managers/NvsManager.cpp
git commit -m "feat(led): NvsManager loads ColorSlotStore + updated 0-100% LED defaults"
```

---

## Task 5: main.cpp — Wire Color Slots + Pot Curve

**Files:**
- Modify: `src/main.cpp:329` (loadLedSettings call)
- Modify: `src/main.cpp:706` (setBrightness call)

- [ ] **Step 1: Load color slots at boot**

After line 329 (`s_leds.loadLedSettings(...)`), add:

```cpp
s_leds.loadColorSlots(s_nvsManager.getLoadedColorSlots());
```

- [ ] **Step 2: Verify setBrightness call passes raw pot value**

Line 706 already passes `s_potRouter.getLedBrightness()` which returns 0-255. The new `setBrightness()` applies `POT_BRIGHTNESS_CURVE[]` internally. No change needed here — just verify the pot value range is 0-255.

- [ ] **Step 3: Full compile + upload test**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

Expected: Clean compile. If ToolLedSettings.cpp has errors (old constant references), those are fixed in Task 6.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(led): wire color slots + pot curve at boot"
```

---

## Task 6: ToolLedSettings — 3-Page Rewrite with Live Preview

**Files:**
- Rewrite: `src/setup/ToolLedSettings.h`
- Rewrite: `src/setup/ToolLedSettings.cpp`

This is the largest task. It rewrites Tool 7 with 3 pages (DISPLAY/COLOR/CONFIRM) and adds live LED preview on LEDs 3-4.

- [ ] **Step 1: Rewrite ToolLedSettings.h**

```cpp
#pragma once
#include "../core/KeyboardData.h"

class LedController;
class NvsManager;
class SetupUI;

class ToolLedSettings {
public:
  ToolLedSettings();
  void begin(LedController* leds, NvsManager* nvs, SetupUI* ui);
  void run();

private:
  LedController* _leds;
  NvsManager* _nvs;
  SetupUI* _ui;

  // State
  uint8_t _page;        // 0=DISPLAY, 1=COLOR, 2=CONFIRM
  uint8_t _cursor;
  uint8_t _field;       // 0=preset, 1=hue, 2=intensity (COLOR page)
  bool _editing;
  bool _nvsSaved;
  bool _confirmDefaults;

  // Working copies
  LedSettingsStore _wk;
  ColorSlotStore _wkColors;

  // Preview state
  bool _previewActive;
  unsigned long _previewStart;

  // Helpers
  uint8_t pageParamCount() const;
  void adjustParam(int8_t dir, bool accel);
  void adjustColorParam(int8_t dir, bool accel);
  bool saveSettings();
  bool saveColors();
  void drawDescription();
  void drawColorRow(uint8_t row, bool selected);
  void updatePreview(unsigned long now);
  void triggerBlinkPreview();

  // Preview rendering on LEDs 3-4
  void renderPreviewSolid(unsigned long now);
  void renderPreviewPulse(unsigned long now);
  void renderPreviewBlink(unsigned long now);
};
```

- [ ] **Step 2: Implement page structure and navigation (ToolLedSettings.cpp)**

Core structure of `run()`:

```cpp
void ToolLedSettings::run() {
  if (!_ui) return;

  // Load working copies (from NVS or defaults)
  _wk = ledSettingsDefaults();       // helper returning fresh default struct
  _wkColors = colorSlotDefaults();   // helper returning fresh default struct

  // Try loading from NVS...
  { Preferences prefs;
    if (prefs.begin(LED_SETTINGS_NVS_NAMESPACE, true)) {
      size_t sz = prefs.getBytesLength(LED_SETTINGS_NVS_KEY);
      if (sz == sizeof(LedSettingsStore)) {
        LedSettingsStore tmp;
        prefs.getBytes(LED_SETTINGS_NVS_KEY, &tmp, sz);
        if (tmp.magic == EEPROM_MAGIC && tmp.version == LED_SETTINGS_VERSION) {
          _wk = tmp;
          _nvsSaved = true;
        }
      }
      sz = prefs.getBytesLength(COLOR_SLOT_NVS_KEY);
      if (sz == sizeof(ColorSlotStore)) {
        ColorSlotStore tmp;
        prefs.getBytes(COLOR_SLOT_NVS_KEY, &tmp, sz);
        if (tmp.magic == COLOR_SLOT_MAGIC && tmp.version == 1) {
          _wkColors = tmp;
        }
      }
      prefs.end();
    }
  }

  _page = 0; _cursor = 0; _field = 0; _editing = false;
  bool screenDirty = true;

  static const char* pageNames[] = {"DISPLAY", "COLOR", "CONFIRM"};
  static const uint8_t pageCounts[] = {2, 16, 15};
  // Page 0 DISPLAY: 2 params (pulse period, tick flash duration)
  // Page 1 COLOR: 16 rows (see spec: 8 display + 8 confirm slots)
  // Page 2 CONFIRM: 15 params (unchanged from old page 1)

  while (true) {
    unsigned long now = millis();

    // Update live preview on LEDs 3-4
    updatePreview(now);

    // Input handling
    int key = _ui->readKey();
    if (key == 'q') break;
    if (key == 't') { _page = (_page + 1) % 3; _cursor = 0; _field = 0; screenDirty = true; }
    // ... arrow keys, TAB (field cycle on COLOR page), 'b' preview, 'd' defaults, Enter save
    // Navigation follows VT100 design guide: up/down=slot, left/right=adjust, TAB=field

    if (key == 'b') { triggerBlinkPreview(); }

    // ... standard navigation + adjustment logic per page

    if (screenDirty) {
      screenDirty = false;
      // Full frame redraw
      _ui->drawFrameTop();
      // ... header with page name and NVS badge
      // ... section with params or color rows
      // ... INFO section with cursor-following description
      // ... control bar
      _ui->drawFrameBottom();
    }

    vTaskDelay(1);
  }

  // Restore normal LED display
  if (_leds) {
    _leds->loadLedSettings(_wk);
    _leds->loadColorSlots(_wkColors);
  }
}
```

The implementer must fill in the complete navigation logic following the existing Tool 5/6/7 patterns in the codebase. The key points:
- Page 0 (DISPLAY): 2 timing params only (pulse period, tick flash duration)
- Page 1 (COLOR): 16 rows, TAB cycles Preset/Hue/Intensity fields, left/right adjusts active field
- Page 2 (CONFIRM): 15 confirmation params (same as current page 1)

- [ ] **Step 3: Implement COLOR page — row drawing with ANSI 24-bit color swatch**

```cpp
void ToolLedSettings::drawColorRow(uint8_t row, bool selected) {
  // Row names for the 16 COLOR page rows
  static const char* rowNames[] = {
    "NORMAL fg", "NORMAL bg",
    "ARPEG fg min", "ARPEG fg max", "ARPEG bg min", "ARPEG bg max",
    "Tick flash fg", "Tick flash bg",
    "Bank switch", "Scale root", "Scale mode", "Scale chromatic",
    "Hold", "Play ack", "Stop", "Octave"
  };

  // Map row to color slot index (ARPEG fg min/max share slot 2, etc.)
  static const uint8_t rowToSlot[] = {
    0, 1, 2, 2, 3, 3, 4, 4,  // display slots
    5, 6, 7, 8, 9, 10, 11, 12 // confirm slots
  };

  // Map row to intensity field in LedSettingsStore
  // (implemented as a switch or lookup returning pointer to _wk field)

  uint8_t slotIdx = rowToSlot[row];
  ColorSlot& slot = _wkColors.slots[slotIdx];
  RGBW resolved = resolveColorSlot(slot);

  const char* presetName = COLOR_PRESET_NAMES[slot.presetId];
  int8_t hue = slot.hueOffset;
  uint8_t intensity = getIntensityForRow(row);  // reads from _wk

  // Color swatch using ANSI 24-bit
  char swatch[32];
  snprintf(swatch, sizeof(swatch), "\033[38;2;%d;%d;%dm\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\033[0m",
           resolved.r, resolved.g, resolved.b);
  // Note: W channel can't be shown in terminal — RGB approximation is fine

  char line[120];
  snprintf(line, sizeof(line), "%s%-15s  %s %-12s  %+4d\xc2\xb0   %3d%%",
           selected ? VT_CYAN : "", rowNames[row], swatch, presetName, hue, intensity);

  _ui->drawFrameLine(line);
}
```

- [ ] **Step 4: Implement live preview — renderPreviewSolid() and renderPreviewPulse()**

```cpp
void ToolLedSettings::updatePreview(unsigned long now) {
  if (!_leds || _page != 1) {
    // Only preview on COLOR page; other pages: all LEDs off
    _leds->allOff();
    return;
  }

  // Clear all LEDs first
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    _leds->setPixelDirect(i, {0,0,0,0}, 0);  // need a direct method for setup mode
  }

  // Determine what to show based on current cursor row
  uint8_t row = _cursor;

  if (row <= 1) {
    // NORMAL fg/bg: solid couple on LEDs 3-4
    RGBW fgCol = resolveColorSlot(_wkColors.slots[CSLOT_NORMAL_FG]);
    RGBW bgCol = resolveColorSlot(_wkColors.slots[CSLOT_NORMAL_BG]);
    // LED 3 = bg, LED 4 = fg
    _leds->setPixelDirect(3, bgCol, _wk.normalBgIntensity);
    _leds->setPixelDirect(4, fgCol, _wk.normalFgIntensity);
  }
  else if (row >= 2 && row <= 5) {
    // ARPEG fg/bg: sine pulse couple on LEDs 3-4
    renderPreviewPulse(now);
  }
  else if (row >= 6 && row <= 7) {
    // Tick flash: LEDs off (use 'b' to trigger)
    // But if blink preview is active, show it
    if (_previewActive) renderPreviewBlink(now);
  }
  else {
    // Confirmation slots: LEDs off (use 'b' to trigger)
    if (_previewActive) renderPreviewBlink(now);
  }

  _leds->showStrip();
}
```

The `renderPreviewPulse()` method uses the same sine LUT logic as `renderNormalDisplay()` but only drives LEDs 3 (bg) and 4 (fg) with the working copy params.

The `renderPreviewBlink()` method implements the contextual blink preview per the spec (bank switch transition, scale blink on solid bg, hold/play/stop on pulse bg, octave on both LEDs).

- [ ] **Step 5: Implement `b` key blink preview — triggerBlinkPreview()**

```cpp
void ToolLedSettings::triggerBlinkPreview() {
  if (_page != 1) return;  // 'b' only active on COLOR page...
  // Actually 'b' is active on ALL pages per spec
  _previewActive = true;
  _previewStart = millis();
}
```

The `renderPreviewBlink()` method checks elapsed time since `_previewStart` and renders the appropriate animation based on current cursor:
- Tick flash: single flash at configured duration
- Bank switch: LED 3 fade bg, LED 4 blink
- Scale/Hold/Play/Stop/Octave: contextual blink per spec section 5.4

Preview auto-expires when the animation completes (`_previewActive = false`).

**`b` is available on all 3 pages.** On DISPLAY and CONFIRM pages, it assembles the full rendering (color from COLOR page + timing from current param) to give a realistic preview.

- [ ] **Step 6: Implement INFO descriptions — drawDescription()**

Rewrite `drawDescription()` with 3 pages of cursor-following info. Each description is written as a mini user manual entry (functional/musical terms, not technical):

For COLOR page (16 rows), example:

```cpp
case 0: // NORMAL fg
  _ui->drawFrameLine(VT_BRIGHT_WHITE "Normal Foreground" VT_RESET);
  _ui->drawFrameLine(VT_DIM "Solid color of the active bank's LED when playing on a NORMAL" VT_RESET);
  _ui->drawFrameLine(VT_DIM "bank. Lower intensities (5-15%) give a subtle glow for dark" VT_RESET);
  _ui->drawFrameLine(VT_DIM "stages. The rear pot controls the master brightness on top." VT_RESET);
  break;
case 2: // ARPEG fg min
  _ui->drawFrameLine(VT_BRIGHT_WHITE "Arpeg Foreground — Pulse Low" VT_RESET);
  _ui->drawFrameLine(VT_DIM "Lowest point of the breathing pulse on the active arpeggiator." VT_RESET);
  _ui->drawFrameLine(VT_DIM "Set low (3-8%) for a subtle breath that barely glows, higher" VT_RESET);
  _ui->drawFrameLine(VT_DIM "(20-30%) for an always-visible pulse. Watch LEDs 3-4 live." VT_RESET);
  break;
// ... all 16 rows, all 2 DISPLAY rows, all 15 CONFIRM rows
```

The implementer must write all descriptions. CONFIRM page descriptions can be adapted from the existing ones (lines 392-482 of current code) with updated wording (remove references to "absolute brightness", mention "color configured in COLOR page").

- [ ] **Step 7: Implement saveSettings() and saveColors()**

```cpp
bool ToolLedSettings::saveSettings() {
  _wk.magic = EEPROM_MAGIC;
  _wk.version = LED_SETTINGS_VERSION;
  Preferences prefs;
  if (!prefs.begin(LED_SETTINGS_NVS_NAMESPACE, false)) return false;
  prefs.putBytes(LED_SETTINGS_NVS_KEY, &_wk, sizeof(LedSettingsStore));
  prefs.end();
  if (_leds) _leds->loadLedSettings(_wk);
  return true;
}

bool ToolLedSettings::saveColors() {
  _wkColors.magic = COLOR_SLOT_MAGIC;
  _wkColors.version = 1;
  Preferences prefs;
  if (!prefs.begin(LED_SETTINGS_NVS_NAMESPACE, false)) return false;
  prefs.putBytes(COLOR_SLOT_NVS_KEY, &_wkColors, sizeof(ColorSlotStore));
  prefs.end();
  if (_leds) _leds->loadColorSlots(_wkColors);
  return true;
}
```

Save is triggered by Enter on any page. Both stores are saved together (they share the NVS namespace).

- [ ] **Step 8: Compile + test**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

Expected: Clean compile.

- [ ] **Step 9: Commit**

```bash
git add src/setup/ToolLedSettings.h src/setup/ToolLedSettings.cpp
git commit -m "feat(led): Tool 7 rewrite — 3 pages (DISPLAY/COLOR/CONFIRM) with live preview on LEDs 3-4"
```

---

## Task 7: Update CLAUDE.md + Architecture Briefing

**Files:**
- Modify: `.claude/CLAUDE.md`
- Modify: `docs/architecture-briefing.md`

- [ ] **Step 1: Update CLAUDE.md**

Update the following sections:
- **Hardware**: Change "8 LEDs: WS2812 RGB NeoPixel Stick" → "8 LEDs: SK6812 RGBW NeoPixel Stick"
- **LED Display**: Update references to NEO_GRB → NEO_GRBW, mention RGBW struct, gamma LUT, perceptual 0-100% scale
- **LED Display table**: Colors now come from configurable presets (not hardcoded)
- **Setup Mode Tool 7**: Update to 3 pages (DISPLAY/COLOR/CONFIRM)
- **NVS table**: Add `illpad_lset/ledcolors` (ColorSlotStore)

- [ ] **Step 2: Update docs/architecture-briefing.md**

Update the LED rendering flow to document:
- Unified `setPixel()` pipeline (perceptual → linear → gamma → strip)
- Color slot resolution (preset + hue shift → RGBW)
- Brightness pot curve
- Removal of absolute brightness path

- [ ] **Step 3: Commit**

```bash
git add .claude/CLAUDE.md docs/architecture-briefing.md
git commit -m "docs: update CLAUDE.md and architecture briefing for SK6812 RGBW overhaul"
```

---

## Task Summary

| Task | Description | Est. |
|------|-------------|------|
| 1 | RGBW type + color constants + LUTs (HardwareConfig.h) | 5 min |
| 2 | Data structures (KeyboardData.h) | 5 min |
| 3 | LedController unified pipeline (header + rendering) | 15 min |
| 4 | NvsManager load/save color slots | 5 min |
| 5 | main.cpp wiring | 3 min |
| 6 | Tool 7 rewrite (3 pages + live preview) | 20 min |
| 7 | Documentation updates | 5 min |

Tasks 1-2 can run in parallel (no dependencies between them).
Tasks 3-5 are sequential (each depends on the previous).
Task 6 depends on tasks 1-5.
Task 7 can run in parallel with task 6.
