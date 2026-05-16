#ifndef KEYBOARD_DATA_H
#define KEYBOARD_DATA_H

#include <stdint.h>
#include <stddef.h>
#include "HardwareConfig.h"
#include "LedGrammar.h"   // EventRenderEntry, EVT_COUNT (consumed by LedSettingsStore v6)
#include "../midi/GrooveTemplates.h"  // NUM_SHUFFLE_TEMPLATES (consumed by validateArpPotStore)

// =================================================================
// Calibration Data — stored in ESP32 NVS via Preferences
// =================================================================

const uint16_t EEPROM_MAGIC   = 0xBEEF;
const uint8_t  EEPROM_VERSION = 5;  // Bumped: 3→4 (48 keys), 4→5 (VDD-safe autoconfig)

struct CalDataStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  uint16_t target_baseline;
  uint16_t maxDelta[NUM_KEYS];  // 48 keys
};

// Preferences namespace for NVS storage
#define CAL_PREFERENCES_NAMESPACE "illpad_cal"
#define CAL_PREFERENCES_KEY       "caldata"

// =================================================================
// Pad Ordering Data — stored in NVS
// NOTE: struct/key names are V1 legacy ("NoteMap"). In V2 this stores
// pad ordering (position indices 0-47), NOT MIDI note numbers.
// Names kept for NVS backward compatibility.
// =================================================================

const uint8_t NOTEMAP_UNMAPPED = 0xFF;  // Pad produces no MIDI output
const uint8_t NOTEMAP_VERSION  = 1;

struct NoteMapStore {
  uint16_t magic;              // EEPROM_MAGIC (0xBEEF)
  uint8_t  version;            // NOTEMAP_VERSION
  uint8_t  reserved;
  uint8_t  noteMap[NUM_KEYS];  // 48 bytes: padIndex → MIDI note (0xFF = unmapped)
};

#define NOTEMAP_NVS_NAMESPACE "illpad_nmap"
#define NOTEMAP_NVS_KEY       "map"

// =================================================================
// Bank Pad Data — pad-to-bank assignment, stored in NVS
// =================================================================

const uint8_t BANKPAD_VERSION = 1;

struct BankPadStore {
  uint16_t magic;              // EEPROM_MAGIC (0xBEEF)
  uint8_t  version;            // BANKPAD_VERSION
  uint8_t  reserved;
  uint8_t  bankPads[NUM_BANKS]; // 8 bytes: bankIndex → padIndex
};

#define BANKPAD_NVS_NAMESPACE "illpad_bpad"
#define BANKPAD_NVS_KEY       "map"

// =================================================================
// Settings Data — runtime-tunable parameters, stored in NVS
// =================================================================

const uint8_t SETTINGS_VERSION = 11;  // Bumped: 10→11 (added LOOP RAMP_HOLD timers)

struct SettingsStore {
  uint16_t magic;               // EEPROM_MAGIC (0xBEEF)
  uint8_t  version;             // SETTINGS_VERSION
  uint8_t  baselineProfile;     // 0-2 (BaselineProfile enum)
  uint8_t  aftertouchRate;      // 10-100 (ms between aftertouch msgs)
  uint8_t  bleInterval;         // 0-3 (BleInterval enum: Normal, Fast, Slow, Off)
  uint8_t  clockMode;           // 0=Slave, 1=Master (ClockMode enum)
  uint8_t  doubleTapMs;         // 100-250 (ms), double-tap window for ARPEG HOLD
  uint16_t potBarDurationMs;    // 1000-10000 (ms), bargraph display persistence
  uint8_t  panicOnReconnect;    // 0=No, 1=Yes — send CC123 on BLE reconnect
  uint8_t  reserved2;           // explicit padding (alignment for batAdcAtFull)
  uint16_t batAdcAtFull;        // Raw ADC reading at full charge (0 = uncalibrated, use default)
  // --- LOOP timers (v11) — coupled to RAMP_HOLD LED pattern (see LED spec §13, LOOP spec §20) ---
  // These timers drive both the user gesture hold duration AND the visual ramp duration.
  // Single source of truth : changes here reflect live in the LED ramp animation.
  // Tool 6 exposes these for editing (step 0.7).
  uint16_t clearLoopTimerMs;    // 200-1500 (ms), default 500 — CLEAR long-press to empty a LOOP bank
  uint16_t slotSaveTimerMs;     // 500-2000 (ms), default 1000 — slot pad long-press to save a loop
  uint16_t slotClearTimerMs;    // 400-1500 (ms), default 800  — visual animation for slot delete combo (not a user hold)
};

#define SETTINGS_NVS_NAMESPACE "illpad_set"
#define SETTINGS_NVS_KEY       "settings"

// =================================================================
// Pot Parameters — response shape + slew rate, stored in NVS
// Saved with 10s debounce from runtime pot control.
// =================================================================

const uint8_t POT_PARAMS_VERSION = 2;  // Bumped: 1→2 (added atDeadzone)

struct PotParamsStore {
  uint16_t magic;              // EEPROM_MAGIC (0xBEEF)
  uint8_t  version;            // POT_PARAMS_VERSION
  uint8_t  reserved;
  uint16_t responseShapeRaw;   // 0-4095 (ADC space, maps to 0.0-1.0)
  uint16_t slewRate;           // SLEW_RATE_MIN-SLEW_RATE_MAX
  uint16_t atDeadzone;         // AT_DEADZONE_MIN-AT_DEADZONE_MAX
};

#define POT_PARAMS_NVS_NAMESPACE "illpad_pot"
#define POT_PARAMS_NVS_KEY       "params"

// =================================================================
// Arp Pot Parameters — per-bank arp params, stored in NVS
// =================================================================

const uint16_t ARPPOT_MAGIC   = EEPROM_MAGIC;  // 0xBEEF (defined in HardwareConfig.h)
const uint8_t  ARPPOT_VERSION = 1;             // 1 = post pattern reduction (15->6) + ARPEG_GEN cohabit

// `pattern` field interpretation depends on owning bank type :
//   - BANK_ARPEG     : ArpPattern enum index (0..NUM_ARP_PATTERNS-1 = 0..5)
//   - BANK_ARPEG_GEN : _genPosition (0..7, 8 positions de grille — V4 Task 22 retune)
// Same byte, different semantics. The owning BankType is the discriminator.
struct ArpPotStore {
  uint16_t magic;             // ARPPOT_MAGIC
  uint8_t  version;           // ARPPOT_VERSION
  uint8_t  reserved;
  uint16_t gateRaw;           // gate × 4095 (range 0-32760, i.e. 0.0-8.0; floor 0.005 on load)
  uint16_t shuffleDepthRaw;   // 0-4095 (maps to 0.0-1.0)
  uint8_t  division;          // ArpDivision enum (0-8)
  uint8_t  pattern;           // ArpPattern (0-5) OR _genPosition (0-14) — see comment above
  uint8_t  octaveRange;       // 1-4 (semantically = mutationLevel for ARPEG_GEN)
  uint8_t  shuffleTemplate;   // 0-9 (index into groove templates)
};
// Total : 4 (header) + 8 = 12 octets.

// =================================================================
// LoopPotStore — LOOP pot params per-bank (5 effets)
// =================================================================
// Phase 1 : declared (struct + validator). No writer Phase 1 — Phase 5 wires
// the pot router targets pour shuffle/chaos/velocity pattern.
// Per-bank pattern : 8 keys "loop_0".."loop_7" sous le namespace
// LOOP_POT_NVS_NAMESPACE (défini plus bas, ligne ~786). Pas de descriptor
// entry dans NVS_DESCRIPTORS[] (idem ArpPotStore : multi-key non descripteurisé).
const uint16_t LOOPPOT_MAGIC   = EEPROM_MAGIC;
const uint8_t  LOOPPOT_VERSION = 1;

struct __attribute__((packed)) LoopPotStore {
  uint16_t magic;              // LOOPPOT_MAGIC
  uint8_t  version;            // LOOPPOT_VERSION
  uint8_t  reserved;
  uint16_t shuffleDepthRaw;    // 0-4095 (maps 0.0-1.0)
  uint8_t  shuffleTemplate;    // 0..NUM_SHUFFLE_TEMPLATES-1 (shared with ArpEngine via GrooveTemplates.h)
  uint16_t chaosRaw;           // 0-4095 (maps 0.0-1.0)
  uint8_t  velPattern;         // 0..3 (4 LUTs : accent, downbeat, backbeat, swing)
  uint16_t velPatternDepthRaw; // 0-4095 (maps 0.0-1.0)
};
// Total packed : 4 (header) + 2+1+2+1+2 = 12 octets. static_assert groupé plus bas.

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

// =================================================================
// Color Slots (Tool 8 COLORS page) — v4 unified grammar
// =================================================================
// v4 grammar (LED spec §11) : "nom au fond, verbe en surface".
//   MODE_* : bank identity colors (the "nouns")
//   VERB_* : transport action colors (the "verbs")
//   SETUP/NAV : bank switch, scale family, octave
//   CONFIRM_OK : universal white suffix (SPARK)
//
// BG colors retired : BG rendering derives from the FG color applied
// to bgFactor% intensity (step 0.6). HOLD_OFF slot retired : STOP uses
// VERB_PLAY color with inverted FADE direction (LED spec option γ).
// =================================================================
#define COLOR_SLOT_COUNT       16
#define COLOR_SLOT_NVS_KEY     "ledcolors"
#define COLOR_SLOT_MAGIC       0xC010

enum ColorSlotId : uint8_t {
  // Mode identity colors (the "nouns")
  CSLOT_MODE_NORMAL      = 0,   // Warm White  — NORMAL bank base color
  CSLOT_MODE_ARPEG       = 1,   // Ice Blue    — ARPEG bank base color
  CSLOT_MODE_LOOP        = 2,   // Gold        — LOOP bank base color (Phase 1+)
  // Verb / action colors (the "verbs")
  CSLOT_VERB_PLAY        = 3,   // Green       — PLAY, tick ARPEG step, HOLD on (merged)
  CSLOT_VERB_REC         = 4,   // Coral       — RECORDING tick (LOOP Phase 1+), REFUSE blink
  CSLOT_VERB_OVERDUB     = 5,   // Amber       — OVERDUBBING tick (LOOP Phase 1+)
  CSLOT_VERB_CLEAR_LOOP  = 6,   // Cyan        — CLEAR long-press ramp (LOOP Phase 1+)
  CSLOT_VERB_SLOT_CLEAR  = 7,   // Amber+hue   — slot delete ramp (LOOP Phase 1+)
  CSLOT_VERB_SAVE        = 8,   // Magenta     — slot save ramp (LOOP Phase 1+)
  // Setup / navigation
  CSLOT_BANK_SWITCH      = 9,   // Pure White  — bank switch blink
  CSLOT_SCALE_ROOT       = 10,  // Amber       — scale root change
  CSLOT_SCALE_MODE       = 11,  // Gold        — scale mode change
  CSLOT_SCALE_CHROM      = 12,  // Coral       — scale chromatic toggle
  CSLOT_OCTAVE           = 13,  // Violet      — octave change
  // Confirmation
  CSLOT_CONFIRM_OK       = 14,  // Pure White  — SPARK suffix universal (LOOP Phase 1+)
  // Phase 0.1 respec — decouple STOP color from PLAY (option γ abandoned)
  CSLOT_VERB_STOP        = 15,  // Coral       — Stop fade-out (Phase 0.1)
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

// =================================================================
// Color Utility — Hue Shift Resolution
// =================================================================
// Resolves a ColorSlot to its final RGBW: look up preset, apply hue rotation.
// W channel is unchanged by hue rotation (it's the "body" of the color).
inline RGBW resolveColorSlot(const ColorSlot& slot) {
  RGBW base = COLOR_PRESETS[slot.presetId < COLOR_PRESET_COUNT ? slot.presetId : 0];
  if (slot.hueOffset == 0) return base;

  // RGB -> HSV (H in 0-360, S/V in 0-255)
  uint8_t maxC = base.r > base.g ? (base.r > base.b ? base.r : base.b)
                                  : (base.g > base.b ? base.g : base.b);
  uint8_t minC = base.r < base.g ? (base.r < base.b ? base.r : base.b)
                                  : (base.g < base.b ? base.g : base.b);
  uint8_t delta = maxC - minC;
  uint8_t s = (maxC == 0) ? 0 : (uint8_t)((uint16_t)delta * 255 / maxC);
  uint8_t v = maxC;
  int16_t h = 0;

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

// =================================================================
// LED Settings (Tool 8 PATTERNS + COLORS + EVENTS pages)
// All intensities are 0-100 (perceptual %). v6 : unified LED grammar.
// See docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md
// and docs/superpowers/plans/2026-04-19-phase0-led-refactor-plan.md §0.2.
// =================================================================
#define LED_SETTINGS_NVS_NAMESPACE "illpad_lset"
#define LED_SETTINGS_NVS_KEY       "ledsettings"
#define LED_SETTINGS_VERSION       9   // v8 -> v9 : unify FG intensity (single fgIntensity for all bank types/states), add breathDepth, remove fgArpStopMin/Max/PlayMax + normalFgIntensity. NVS reset on update (zero-migration policy).

struct LedSettingsStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  // --- Intensity (v9 : unified, 0-100 perceptual %) ---
  uint8_t  fgIntensity;           // default 80  — SOLID intensity FG for any bank type/state (NORMAL/ARPEG/LOOP, idle/playing). Range [10,100].
  uint8_t  breathDepth;           // default 50  — breathing dip depth % (FG oscillates fgIntensity*(1-depth/100) → fgIntensity). Range [0,80] (clamped runtime by bgFactor to keep FG > BG).
  uint8_t  tickFlashFg;           // default 100 — FLASH pattern fgPct (absolute, independent of fgIntensity)
  uint8_t  tickFlashBg;           // default 25  — FLASH pattern bgPct (absolute, independent of fgIntensity)
  // --- Global background factor ---
  uint8_t  bgFactor;              // default 25  — BG = FG × bgFactor%. Range [10, 50].
  // --- Timing ---
  uint16_t pulsePeriodMs;         // default 1472 — PULSE_SLOW period FG ARPEG stopped-loaded
  uint16_t tickBeatDurationMs;    // default 30  — FLASH pattern durationMs for ARPEG step / LOOP beat ticks (v7 rename + widen)
  uint16_t tickBarDurationMs;     // default 60  — FLASH pattern durationMs for LOOP bar ticks (v7 new, consumed Phase 1+)
  uint16_t tickWrapDurationMs;    // default 100 — FLASH pattern durationMs for LOOP wrap ticks (v7 new, consumed Phase 1+)
  uint8_t  gammaTenths;           // 10-30 -> gamma 1.0-3.0, default 20 (2.0). Reboot-only.
  // --- SPARK params (v6 new — used by RAMP_HOLD suffix and CONFIRM_OK) ---
  uint16_t sparkOnMs;             // default 50  — SPARK pattern on duration per flash
  uint16_t sparkGapMs;            // default 70  — SPARK pattern gap between flashes
  uint8_t  sparkCycles;           // default 2   — SPARK pattern flash count
  // --- Confirmations (legacy v5, still used in Phase 0 steps 0.3-0.5 until event engine takes over in 0.4-0.5) ---
  uint8_t  bankBlinks;            // 1-3, default 3
  uint16_t bankDurationMs;        // default 300
  uint8_t  bankBrightnessPct;     // default 80
  uint8_t  scaleRootBlinks;       // default 2
  uint16_t scaleRootDurationMs;   // default 200
  uint8_t  scaleModeBlinks;       // default 2
  uint16_t scaleModeDurationMs;   // default 200
  uint8_t  scaleChromBlinks;      // default 2
  uint16_t scaleChromDurationMs;  // default 200
  uint16_t holdOnFadeMs;          // default 500, 0-1000 (fade IN on capture OFF->ON)
  uint16_t holdOffFadeMs;         // default 500, 0-1000 (fade OUT on release ON->OFF)
  uint8_t  octaveBlinks;          // default 3
  uint16_t octaveDurationMs;      // default 300
  // --- Event overrides (v6 new — per-event {pattern, color, fgPct} override) ---
  // patternId == PTN_NONE (0xFF) -> fallback on EVENT_RENDER_DEFAULT[i].
  EventRenderEntry eventOverrides[EVT_COUNT];
};
static_assert(sizeof(LedSettingsStore) <= 128, "LedSettingsStore exceeds NVS blob max (128)");

// =================================================================
// V2 — Bank Types & Scale Config
// =================================================================

enum BankType : uint8_t {
  BANK_NORMAL    = 0,
  BANK_ARPEG     = 1,
  BANK_LOOP      = 2,   // RESERVED : LOOP Phase 1 (not yet implemented)
  BANK_ARPEG_GEN = 3,   // ARPEG génératif (this plan)
  BANK_ANY       = 0xFF // Used in PotRouter bindings (matches any type)
};

// Helper : true si la bank utilise un ArpEngine (ARPEG classique OU ARPEG_GEN).
// Tous les call-sites qui filtraient sur BANK_ARPEG strict doivent passer par ce helper.
inline bool isArpType(BankType t) {
  return t == BANK_ARPEG || t == BANK_ARPEG_GEN;
}

struct ScaleConfig {
  bool    chromatic;  // true = chromatic, false = scale mode
  uint8_t root;      // 0-6 (A, B, C, D, E, F, G)
  uint8_t mode;      // 0-6 (Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian)
};

// Forward declaration
class ArpEngine;

struct BankSlot {
  uint8_t     channel;                    // 0-7 (fixed, = bank index)
  BankType    type;                       // NORMAL or ARPEG
  ScaleConfig scale;
  ArpEngine*  arpEngine;                  // non-null if ARPEG
  bool        isForeground;
  uint8_t     baseVelocity;              // 1-127, per-bank (NORMAL + ARPEG)
  uint8_t     velocityVariation;         // 0-100%, per-bank (NORMAL + ARPEG)
  uint16_t    pitchBendOffset;           // 0-16383, center=8192 (NORMAL only)
};

// =================================================================
// Control Pads — Tool 4 (CC output via cross-bank pads)
// =================================================================

const uint16_t CONTROLPAD_MAGIC   = 0xBEEF;
const uint8_t  CONTROLPAD_VERSION = 2;  // v1 -> v2 : added smoothMs/sampleHoldMs/releaseMs globals
const uint8_t  MAX_CONTROL_PADS   = 12;
const uint8_t  CTRL_PAD_INVALID   = 0xFF;  // sentinel for corrupted/skipped entry

enum ControlPadMode : uint8_t {
  CTRL_MODE_MOMENTARY  = 0,
  CTRL_MODE_LATCH      = 1,
  CTRL_MODE_CONTINUOUS = 2,
};

enum ControlPadRelease : uint8_t {
  CTRL_RELEASE_TO_ZERO = 0,   // release → CC=0 (gate semantic)
  CTRL_RELEASE_HOLD    = 1,   // release → CC stays (setter semantic)
};

struct ControlPadEntry {
  uint8_t padIndex;     // 0-47, or CTRL_PAD_INVALID when validator flagged
  uint8_t ccNumber;     // 0-127
  uint8_t channel;      // 0 = follow bank, 1-16 = fixed MIDI channel (user-facing)
  uint8_t mode;         // ControlPadMode
  uint8_t deadzone;     // 0-126, CONTINUOUS only
  uint8_t releaseMode;  // ControlPadRelease, CONTINUOUS only
};

struct ControlPadStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  count;
  uint16_t smoothMs;         // V2 : EMA tau for CONTINUOUS (0 = bypass, 1..200 typical)
  uint16_t sampleHoldMs;     // V2 : ring-buffer look-back for HOLD_LAST capture (5..30 typical)
  uint16_t releaseMs;        // V2 : linear fade-out duration for RETURN_TO_ZERO (10..500 typical)
  ControlPadEntry entries[MAX_CONTROL_PADS];
};
// sizeof = 4 + 6 + 12*6 = 82 bytes (still within NVS_BLOB_MAX_SIZE=128)

#define CONTROLPAD_NVS_NAMESPACE "illpad_ctrl"
#define CONTROLPAD_NVS_KEY       "pads"

inline void validateControlPadStore(ControlPadStore& s) {
  if (s.count > MAX_CONTROL_PADS) s.count = MAX_CONTROL_PADS;

  // V2 globals — clamp to sensible ranges
  if (s.smoothMs      > 500)  s.smoothMs     = 500;
  if (s.sampleHoldMs  > 31)   s.sampleHoldMs = 31;   // bounded by ring buffer size (CTRL_RING_SIZE-1)
  if (s.releaseMs     > 2000) s.releaseMs    = 2000;

  for (uint8_t i = 0; i < s.count; i++) {
    auto& e = s.entries[i];
    if (e.padIndex >= NUM_KEYS)  e.padIndex = CTRL_PAD_INVALID;
    if (e.ccNumber > 127)        e.ccNumber = 127;
    if (e.channel > 16)          e.channel = 0;
    if (e.mode > 2)              e.mode = CTRL_MODE_MOMENTARY;
    if (e.deadzone > 126)        e.deadzone = 0;
    if (e.releaseMode > 1)       e.releaseMode = CTRL_RELEASE_TO_ZERO;
    if (e.mode == CTRL_MODE_LATCH && e.channel == 0) {
      e.mode = CTRL_MODE_MOMENTARY;  // LATCH needs fixed channel
    }
  }
}

// =================================================================
// V2 — Arp Enums
// =================================================================

enum ArpPattern : uint8_t {
  ARP_UP             = 0,   // (kept) low -> high
  ARP_DOWN           = 1,   // (kept) high -> low
  ARP_UP_DOWN        = 2,   // (kept) UP puis indices descendants
  ARP_ORDER          = 3,   // (was 4) chronologique
  ARP_PEDAL_UP       = 4,   // (was 8) basse pedale + arpege
  ARP_CONVERGE       = 5,   // (was 6) zigzag low/high vers centre
  NUM_ARP_PATTERNS   = 6
};

enum ArpDivision : uint8_t {
  DIV_4_1   = 0,   // Quadruple whole
  DIV_2_1   = 1,   // Double whole
  DIV_1_1   = 2,   // Whole note
  DIV_1_2   = 3,   // Half note
  DIV_1_4   = 4,   // Quarter note
  DIV_1_8   = 5,   // Eighth note
  DIV_1_16  = 6,   // Sixteenth note
  DIV_1_32  = 7,   // Thirty-second note
  DIV_1_64  = 8,   // Sixty-fourth note
  NUM_ARP_DIVISIONS = 9
};

// =================================================================
// V2 — NVS Namespaces (new)
// =================================================================

#define BANKTYPE_NVS_NAMESPACE  "illpad_btype"

#define SCALE_NVS_NAMESPACE     "illpad_scale"
// Keys: "cfg_0" through "cfg_7" (per bank)

#define SCALE_PAD_NVS_NAMESPACE "illpad_spad"

#define ARP_PAD_NVS_NAMESPACE   "illpad_apad"

// =================================================================
// NVS blob size limit — all Store structs must fit in a stack buffer
// =================================================================
static constexpr size_t NVS_BLOB_MAX_SIZE = 128;

// --- V2 Store structs for previously-raw NVS data ---

#define SCALEPAD_NVS_KEY    "pads"
#define SCALEPAD_VERSION    1

struct ScalePadStore {
  uint16_t magic;        // EEPROM_MAGIC
  uint8_t  version;      // SCALEPAD_VERSION
  uint8_t  reserved;
  uint8_t  rootPads[7];
  uint8_t  modePads[7];
  uint8_t  chromaticPad;
  uint8_t  _pad;         // alignment to 20 bytes
};
static_assert(sizeof(ScalePadStore) <= NVS_BLOB_MAX_SIZE, "ScalePadStore exceeds NVS blob max");

#define ARPPAD_NVS_KEY      "pads"
#define ARPPAD_VERSION      2

struct ArpPadStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  uint8_t  holdPad;
  uint8_t  octavePads[4];
  uint8_t  _pad[3];      // alignment to 12 bytes
};
static_assert(sizeof(ArpPadStore) <= NVS_BLOB_MAX_SIZE, "ArpPadStore exceeds NVS blob max");

// =================================================================
// LoopPadStore — LOOP control pads (REC/PLAY/CLEAR) + slot pads 0..15
// =================================================================
// Phase 1 : declared (struct + validator + descriptor). No runtime writer
// yet — Tool 3 b1 contextual refactor (Phase 3) will wire UI editing.
// Strict 23 B packed per spec LOOP §28 Q1 (pas d'alignement de remplissage).
#define LOOPPAD_NVS_NAMESPACE  "illpad_lpad"
#define LOOPPAD_NVS_KEY        "pads"
#define LOOPPAD_VERSION        1

struct __attribute__((packed)) LoopPadStore {
  uint16_t magic;          // 2 B  -> EEPROM_MAGIC
  uint8_t  version;        // 1 B  -> LOOPPAD_VERSION
  uint8_t  reserved;       // 1 B  -> alignement, 0
  uint8_t  recPad;         // 1 B  -> control pad REC,         0xFF si non assigné
  uint8_t  playStopPad;    // 1 B  -> control pad PLAY/STOP,   0xFF si non assigné
  uint8_t  clearPad;       // 1 B  -> control pad CLEAR,       0xFF si non assigné
  uint8_t  slotPads[16];   // 16 B -> slot pads 0..15,         0xFF si non assigné
};
static_assert(sizeof(LoopPadStore) == 23, "LoopPadStore must be exactly 23 B (Q1 §28 spec LOOP)");
static_assert(sizeof(LoopPadStore) <= NVS_BLOB_MAX_SIZE, "LoopPadStore exceeds NVS blob max");

#define BANKTYPE_NVS_KEY_V2  "config"
#define BANKTYPE_VERSION     4   // 3->4 : ajout proximityFactorx10[] + ecart[] (ARPEG_GEN walk tuning)

#define NUM_SCALE_GROUPS     4   // A, B, C, D (0 = none / banque independante)

struct BankTypeStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  uint8_t  types[NUM_BANKS];               // BankType enum cast (0..3)
  uint8_t  quantize[NUM_BANKS];            // ArpStartMode enum
  uint8_t  scaleGroup[NUM_BANKS];          // 0 = none, 1..NUM_SCALE_GROUPS = A..D
  uint8_t  bonusPilex10[NUM_BANKS];        // V3 : 10..20 (bonus_pile x 10), defaults 15. ARPEG_GEN only.
  uint8_t  marginWalk[NUM_BANKS];          // V3 : 3..12 (degres). Defaults 7. ARPEG_GEN only.
  uint8_t  proximityFactorx10[NUM_BANKS];  // V4 : 4..20 (prox_factor x 10 = 0.4..2.0), defaults 4 (=0.4). ARPEG_GEN only.
  uint8_t  ecart[NUM_BANKS];               // V4 : 1..12 (walk ecart override Tool 5). Defaults 5. ARPEG_GEN only.
};
static_assert(sizeof(BankTypeStore) <= NVS_BLOB_MAX_SIZE, "BankTypeStore exceeds NVS blob max");
// Total v4 : 4 (header) + 8 x 7 = 60 octets. Confortable sous le cap NVS_BLOB_MAX_SIZE = 128.

#define COLOR_SLOT_VERSION  5

// =================================================================
// PotTarget — all possible pot-controlled parameters
// =================================================================
enum PotTarget : uint8_t {
  // NORMAL global
  TARGET_RESPONSE_SHAPE,
  TARGET_SLEW_RATE,
  TARGET_AT_DEADZONE,
  // NORMAL per-bank
  TARGET_PITCH_BEND,
  // ARPEG per-bank
  TARGET_GATE_LENGTH,
  TARGET_SHUFFLE_DEPTH,
  TARGET_DIVISION,
  TARGET_PATTERN,
  TARGET_SHUFFLE_TEMPLATE,
  // Shared per-bank (NORMAL + ARPEG)
  TARGET_BASE_VELOCITY,
  TARGET_VELOCITY_VARIATION,
  // Global
  TARGET_TEMPO_BPM,
  TARGET_LED_BRIGHTNESS,
  TARGET_PAD_SENSITIVITY,
  // MIDI output (user-assignable via Tool 6)
  TARGET_MIDI_CC,
  TARGET_MIDI_PITCHBEND,
  // ARPEG_GEN per-bank (NEW — runtime-only, not user-mappable in Tool 7).
  // Substitutes TARGET_PATTERN on banks of type BANK_ARPEG_GEN via two-binding strategy
  // in PotRouter::rebuildBindings (D3, plan §0).
  TARGET_GEN_POSITION,
  // Empty slot (explicit "no parameter here")
  TARGET_EMPTY,
  // Sentinel (used internally, not assignable)
  TARGET_NONE,
  // Count (for iteration)
  TARGET_COUNT = TARGET_NONE
};

// =================================================================
// PotMapping — user-configurable slot assignment
// One entry per slot (8 slots = 4 pots × 2 layers: alone + hold-left)
// =================================================================
struct PotMapping {
  PotTarget target;     // What this slot controls
  uint8_t   ccNumber;   // CC# when target == TARGET_MIDI_CC (0-127)
};

// 8 slots per context: [0]=R1 alone, [1]=R1+hold, [2]=R2 alone, ...
// [6]=R4 alone, [7]=R4+hold
static const uint8_t POT_MAPPING_SLOTS = 8;

// =================================================================
// PotMappingStore — NVS-serializable pot mapping (both contexts)
// =================================================================
// v2 : Tempo retiré des pools Tool 7 (déplacé sur LEFT + rear pot, binding fixe).
//      Reset des user mappings au reload (politique zero-NVS-migration).
#define POTMAP_VERSION 2

struct PotMappingStore {
  uint16_t   magic;    // Must match EEPROM_MAGIC
  uint8_t    version;  // POTMAP_VERSION
  uint8_t    reserved;
  PotMapping normalMap[POT_MAPPING_SLOTS];
  PotMapping arpegMap[POT_MAPPING_SLOTS];
};

// --- Defensive static_asserts for all Store structs ---
static_assert(sizeof(CalDataStore) <= NVS_BLOB_MAX_SIZE, "CalDataStore exceeds NVS blob max");
static_assert(sizeof(NoteMapStore) <= NVS_BLOB_MAX_SIZE, "NoteMapStore exceeds NVS blob max");
static_assert(sizeof(BankPadStore) <= NVS_BLOB_MAX_SIZE, "BankPadStore exceeds NVS blob max");
static_assert(sizeof(ControlPadStore) <= NVS_BLOB_MAX_SIZE, "ControlPadStore exceeds NVS blob max");
static_assert(sizeof(SettingsStore) <= NVS_BLOB_MAX_SIZE, "SettingsStore exceeds NVS blob max");
static_assert(sizeof(PotParamsStore) <= NVS_BLOB_MAX_SIZE, "PotParamsStore exceeds NVS blob max");  // F-CODE-4
static_assert(sizeof(ArpPotStore) <= NVS_BLOB_MAX_SIZE, "ArpPotStore exceeds NVS blob max");        // F-CODE-4
static_assert(sizeof(LoopPotStore) <= NVS_BLOB_MAX_SIZE, "LoopPotStore exceeds NVS blob max");
static_assert(sizeof(PotMappingStore) <= NVS_BLOB_MAX_SIZE, "PotMappingStore exceeds NVS blob max");
static_assert(sizeof(LedSettingsStore) <= NVS_BLOB_MAX_SIZE, "LedSettingsStore exceeds NVS blob max");
static_assert(sizeof(ColorSlotStore) <= NVS_BLOB_MAX_SIZE, "ColorSlotStore exceeds NVS blob max");

// SettingsStore byte 3 is baselineProfile, NOT reserved — defend against generic zeroing
static_assert(offsetof(SettingsStore, baselineProfile) == 3,
              "SettingsStore layout changed — byte 3 must be baselineProfile (not reserved)");

// =================================================================
// V2 — Arp Constants
// =================================================================

const uint8_t MAX_ARP_BANKS    = 4;
const uint8_t MAX_ARP_NOTES    = 48;
const uint8_t MAX_ARP_SEQUENCE = 192;  // 48 notes × 4 octaves
const uint8_t MAX_ARP_OCTAVES  = 4;

// ARPEG_GEN — discrete grid positions (spec §13, retuned V4 Task 22).
// 8 valeurs uniques seqLen : 2, 3, 4, 8, 12, 16, 32, 64 (cf TABLE_GEN_SEQ_LEN).
// Shared between ArpEngine (TABLE_GEN_SEQ_LEN lookup) and PotRouter
// (TARGET_GEN_POSITION clamp/range + hysteresis zone width).
const uint8_t NUM_GEN_POSITIONS = 8;

// =================================================================
// Validation functions — shared by Tools, loadAll, WiFi handler
// Single source of truth for all field bounds. Called after every
// loadBlob and before every saveBlob from external input (WiFi JSON).
// =================================================================

inline void validateSettingsStore(SettingsStore& s) {
  if (s.baselineProfile >= NUM_BASELINE_PROFILES) s.baselineProfile = DEFAULT_BASELINE_PROFILE;
  if (s.aftertouchRate < AT_RATE_MIN || s.aftertouchRate > AT_RATE_MAX) s.aftertouchRate = AT_RATE_DEFAULT;
  if (s.bleInterval >= NUM_BLE_INTERVALS) s.bleInterval = DEFAULT_BLE_INTERVAL;
  if (s.clockMode >= NUM_CLOCK_MODES) s.clockMode = DEFAULT_CLOCK_MODE;
  if (s.doubleTapMs < DOUBLE_TAP_MS_MIN || s.doubleTapMs > DOUBLE_TAP_MS_MAX) s.doubleTapMs = DOUBLE_TAP_MS_DEFAULT;
  if (s.potBarDurationMs < LED_BARGRAPH_DURATION_MIN || s.potBarDurationMs > LED_BARGRAPH_DURATION_MAX)
    s.potBarDurationMs = LED_BARGRAPH_DURATION_DEFAULT;
  if (s.panicOnReconnect > 1) s.panicOnReconnect = DEFAULT_PANIC_ON_RECONNECT;
  if (s.batAdcAtFull > 4095) s.batAdcAtFull = 4095;  // ADC 12-bit max
  // LOOP timers (v11) : clamp to documented ranges
  if (s.clearLoopTimerMs < 200  || s.clearLoopTimerMs > 1500) s.clearLoopTimerMs = 500;
  if (s.slotSaveTimerMs  < 500  || s.slotSaveTimerMs  > 2000) s.slotSaveTimerMs  = 1000;
  if (s.slotClearTimerMs < 400  || s.slotClearTimerMs > 1500) s.slotClearTimerMs = 800;
}

inline void validateArpPotStore(ArpPotStore& s) {
  // pattern range etendu pour couvrir les 2 semantiques :
  //   - ARPEG classique : 0..NUM_ARP_PATTERNS-1 (= 0..5 apres Task 4)
  //   - ARPEG_GEN     : 0..7 (8 positions de grille — V4 Task 22 retune)
  // Le validate clampe au max des deux pour ne pas casser un pattern stocke pour une bank ARPEG_GEN.
  if (s.pattern > 7) s.pattern = 0;
  if (s.division >= NUM_ARP_DIVISIONS) s.division = DIV_1_8;
  if (s.octaveRange < 1 || s.octaveRange > 4) s.octaveRange = 1;
  if (s.shuffleTemplate >= NUM_SHUFFLE_TEMPLATES) s.shuffleTemplate = 0;
  // gateRaw / shuffleDepthRaw : pas de clamp strict (uint16 native), clampe a load par les getters float.
}

inline void validateLoopPotStore(LoopPotStore& s) {
  if (s.shuffleDepthRaw    > 4095) s.shuffleDepthRaw    = 0;
  if (s.shuffleTemplate    >= NUM_SHUFFLE_TEMPLATES) s.shuffleTemplate = 0;
  if (s.chaosRaw           > 4095) s.chaosRaw           = 0;
  if (s.velPattern         >= 4)   s.velPattern         = 0;
  if (s.velPatternDepthRaw > 4095) s.velPatternDepthRaw = 0;
}

inline void validateBankTypeStore(BankTypeStore& s) {
  uint8_t arpCount = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    // Type clamp : tout > BANK_ARPEG_GEN (sauf BANK_ANY=0xFF) -> NORMAL
    if (s.types[i] > BANK_ARPEG_GEN && s.types[i] != BANK_ANY) s.types[i] = BANK_NORMAL;
    if (isArpType((BankType)s.types[i])) arpCount++;
    if (arpCount > MAX_ARP_BANKS) s.types[i] = BANK_NORMAL;
    if (s.quantize[i] >= NUM_ARP_START_MODES) s.quantize[i] = DEFAULT_ARP_START_MODE;
    if (s.scaleGroup[i] > NUM_SCALE_GROUPS) s.scaleGroup[i] = 0;
    // V3 : nouveaux champs (clamp aux ranges declares en spec §21)
    if (s.bonusPilex10[i] < 10 || s.bonusPilex10[i] > 20) s.bonusPilex10[i] = 15;
    if (s.marginWalk[i]  < 3  || s.marginWalk[i]  > 12) s.marginWalk[i]  = 7;
    // V4 : walk tuning (Tool 5 override de la constante compile-time et de TABLE_GEN_POSITION ecart)
    if (s.proximityFactorx10[i] < 4 || s.proximityFactorx10[i] > 20) s.proximityFactorx10[i] = 4;
    if (s.ecart[i] < 1 || s.ecart[i] > 12) s.ecart[i] = 5;
  }
}

inline void validateScalePadStore(ScalePadStore& s) {
  for (uint8_t i = 0; i < 7; i++) {
    if (s.rootPads[i] >= NUM_KEYS) s.rootPads[i] = 8 + i;
    if (s.modePads[i] >= NUM_KEYS) s.modePads[i] = 15 + i;
  }
  if (s.chromaticPad >= NUM_KEYS) s.chromaticPad = 22;
}

inline void validateArpPadStore(ArpPadStore& s) {
  if (s.holdPad >= NUM_KEYS) s.holdPad = 23;
  for (uint8_t i = 0; i < 4; i++) {
    if (s.octavePads[i] >= NUM_KEYS) s.octavePads[i] = 25 + i;
  }
}

inline void validateLoopPadStore(LoopPadStore& s) {
  // Clamp out-of-range pad indices à 0xFF (sentinel "non assigné").
  // Collision validation (rec != playStop != clear != slots != banks/scale/arp)
  // est de la responsabilité de Tool 3 b1 (Phase 3), pas de ce validator.
  if (s.recPad      != 0xFF && s.recPad      >= NUM_KEYS) s.recPad      = 0xFF;
  if (s.playStopPad != 0xFF && s.playStopPad >= NUM_KEYS) s.playStopPad = 0xFF;
  if (s.clearPad    != 0xFF && s.clearPad    >= NUM_KEYS) s.clearPad    = 0xFF;
  for (uint8_t i = 0; i < 16; i++) {
    if (s.slotPads[i] != 0xFF && s.slotPads[i] >= NUM_KEYS) s.slotPads[i] = 0xFF;
  }
}

inline void validateBankPadStore(BankPadStore& s) {
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (s.bankPads[i] >= NUM_KEYS) s.bankPads[i] = i;
  }
}

inline void validateNoteMapStore(NoteMapStore& s) {
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    if (s.noteMap[i] >= NUM_KEYS) s.noteMap[i] = i;
  }
}

inline void validateLedSettingsStore(LedSettingsStore& s) {
  // v9 : unified intensity + breathing depth.
  if (s.fgIntensity < 10)  s.fgIntensity = 10;
  if (s.fgIntensity > 100) s.fgIntensity = 100;
  if (s.breathDepth > 80)  s.breathDepth = 80;
  // Note : breathDepth final floor is runtime-dependent (clamped against
  // bgFactor in renderBankArpeg to keep invariant FG > BG strict).
  // bgFactor range [10, 50]
  if (s.bgFactor < 10) s.bgFactor = 10;
  if (s.bgFactor > 50) s.bgFactor = 50;
  // Timing ranges
  if (s.pulsePeriodMs < 500)  s.pulsePeriodMs = 500;
  if (s.pulsePeriodMs > 4000) s.pulsePeriodMs = 4000;
  if (s.tickBeatDurationMs < 5)   s.tickBeatDurationMs = 5;
  if (s.tickBeatDurationMs > 500) s.tickBeatDurationMs = 500;
  if (s.tickBarDurationMs  < 5)   s.tickBarDurationMs  = 5;
  if (s.tickBarDurationMs  > 500) s.tickBarDurationMs  = 500;
  if (s.tickWrapDurationMs < 5)   s.tickWrapDurationMs = 5;
  if (s.tickWrapDurationMs > 500) s.tickWrapDurationMs = 500;
  if (s.gammaTenths < 10) s.gammaTenths = 10;
  if (s.gammaTenths > 30) s.gammaTenths = 30;
  // SPARK timing (range loose enough to support experimentation)
  if (s.sparkOnMs < 20)   s.sparkOnMs = 20;
  if (s.sparkOnMs > 200)  s.sparkOnMs = 200;
  if (s.sparkGapMs < 20)  s.sparkGapMs = 20;
  if (s.sparkGapMs > 300) s.sparkGapMs = 300;
  if (s.sparkCycles < 1 || s.sparkCycles > 4) s.sparkCycles = 2;
  // Confirmation blink counts and durations
  if (s.bankBlinks < 1 || s.bankBlinks > 3) s.bankBlinks = 3;
  if (s.bankDurationMs < 100 || s.bankDurationMs > 500) s.bankDurationMs = 300;
  if (s.bankBrightnessPct > 100) s.bankBrightnessPct = 80;
  if (s.scaleRootBlinks < 1 || s.scaleRootBlinks > 3) s.scaleRootBlinks = 2;
  if (s.scaleRootDurationMs < 100 || s.scaleRootDurationMs > 500) s.scaleRootDurationMs = 200;
  if (s.scaleModeBlinks < 1 || s.scaleModeBlinks > 3) s.scaleModeBlinks = 2;
  if (s.scaleModeDurationMs < 100 || s.scaleModeDurationMs > 500) s.scaleModeDurationMs = 200;
  if (s.scaleChromBlinks < 1 || s.scaleChromBlinks > 3) s.scaleChromBlinks = 2;
  if (s.scaleChromDurationMs < 100 || s.scaleChromDurationMs > 500) s.scaleChromDurationMs = 200;
  if (s.holdOnFadeMs > 1000) s.holdOnFadeMs = 500;
  if (s.holdOffFadeMs > 1000) s.holdOffFadeMs = 500;
  if (s.octaveBlinks < 1 || s.octaveBlinks > 3) s.octaveBlinks = 3;
  if (s.octaveDurationMs < 100 || s.octaveDurationMs > 500) s.octaveDurationMs = 300;
  // Event overrides : clamp patternId to valid range or PTN_NONE, clamp fgPct to 0-100.
  // colorSlot clamped against COLOR_SLOT_COUNT (Phase 0 = 12 v3, step 0.6 = 16 v4).
  for (uint8_t i = 0; i < EVT_COUNT; i++) {
    EventRenderEntry& e = s.eventOverrides[i];
    if (e.patternId != PTN_NONE && e.patternId >= PTN_COUNT) e.patternId = PTN_NONE;
    if (e.fgPct > 100) e.fgPct = 100;
    if (e.colorSlot >= COLOR_SLOT_COUNT) e.colorSlot = 0;
  }
}

// --- V2 New NVS Namespaces ---
#define VELOCITY_NVS_NAMESPACE  "illpad_bvel"   // baseVelocity[8] + velocityVariation[8]
#define PITCHBEND_NVS_NAMESPACE "illpad_pbnd"   // pitchBendOffset[8]
#define LED_NVS_NAMESPACE       "illpad_led"    // LED brightness (global)
#define LED_NVS_KEY             "brightness"
#define SENSITIVITY_NVS_NAMESPACE "illpad_sens" // Pad sensitivity (global)
#define SENSITIVITY_NVS_KEY     "sensitivity"
#define ARP_POT_NVS_NAMESPACE   "illpad_apot"   // Arp pot params per bank (gate, shuffle, div, pattern, oct)
// Keys: "arp_0" through "arp_7" (per bank)
#define LOOP_POT_NVS_NAMESPACE  "illpad_lpot"   // Loop pot params per bank (shuffleDepth, shuffleTpl, chaos, velPattern, velPatternDepth)
// Keys: "loop_0" through "loop_7" (per bank)

#define TEMPO_NVS_NAMESPACE     "illpad_tempo"  // Tempo BPM (global)
#define TEMPO_NVS_KEY           "bpm"

#define POTMAP_NVS_NAMESPACE    "illpad_pmap"   // User pot mapping (both contexts)
#define POTMAP_NVS_KEY          "mapping"

// ── Pot Filter ──────────────────────────────────
#define POTFILTER_NVS_NAMESPACE "illpad_pflt"
#define POTFILTER_NVS_KEY       "cfg"
const uint8_t POT_FILTER_VERSION = 5;  // v5: pot 4 back to deadband=8 (brightness UX), pots 0-3 stay at 10

struct PotFilterStore {
    uint16_t magic;                        // EEPROM_MAGIC
    uint8_t  version;                      // POT_FILTER_VERSION
    uint8_t  sleepEn;                      // 0=off, 1=on (default 1, applied to pot 4 only at runtime)
    uint16_t sleepMs;                      // sleep delay ms (100-2000, default 500)
    uint8_t  perPotDeadband[NUM_POTS];     // per-pot deadband LSB (defaults: 5/5/5/5/8)
    uint8_t  edgeSnap;                     // ADC units from edges (0-10, default 3)
    uint8_t  wakeThresh;                   // ADC units to wake from sleep (3-30, default 8)
};
static_assert(sizeof(PotFilterStore) <= NVS_BLOB_MAX_SIZE, "PotFilterStore too large");

inline void validatePotFilterStore(PotFilterStore& s) {
    if (s.sleepEn > 1)                             s.sleepEn = 1;
    if (s.sleepMs < 100 || s.sleepMs > 2000)       s.sleepMs = 500;
    for (uint8_t i = 0; i < NUM_POTS; i++) {
        if (s.perPotDeadband[i] < 1 || s.perPotDeadband[i] > 20) {
            s.perPotDeadband[i] = (i == 4) ? 8 : 10;
        }
    }
    if (s.edgeSnap > 10)                           s.edgeSnap = 3;
    if (s.wakeThresh < 3 || s.wakeThresh > 30)    s.wakeThresh = 8;
}

// =================================================================
// V2 — Shared Double Buffer (replaces mutex)
// =================================================================

struct SharedKeyboardState {
  uint8_t keyIsPressed[NUM_KEYS];
  uint8_t pressure[NUM_KEYS];
};

// =================================================================
// NVS Descriptor Table — unified status checks for all Store blobs
// =================================================================

struct NvsDescriptor {
  const char* ns;
  const char* key;
  uint16_t    magic;
  uint8_t     version;
  uint16_t    size;       // uint16_t, not uint8_t — room for future growth
};

// NOTE: This table is static constexpr in a header, so each TU gets its own copy
// (~300 bytes). Acceptable for ESP32-S3 with 8MB flash. If flash becomes tight,
// move to extern const in a .cpp file.
static constexpr NvsDescriptor NVS_DESCRIPTORS[] = {
  { CAL_PREFERENCES_NAMESPACE, CAL_PREFERENCES_KEY,    EEPROM_MAGIC,    EEPROM_VERSION,       (uint16_t)sizeof(CalDataStore)      },  // 0: T1
  { NOTEMAP_NVS_NAMESPACE,     NOTEMAP_NVS_KEY,        EEPROM_MAGIC,    NOTEMAP_VERSION,      (uint16_t)sizeof(NoteMapStore)      },  // 1: T2
  { BANKPAD_NVS_NAMESPACE,     BANKPAD_NVS_KEY,        EEPROM_MAGIC,    BANKPAD_VERSION,      (uint16_t)sizeof(BankPadStore)      },  // 2: T3a
  { SCALE_PAD_NVS_NAMESPACE,   SCALEPAD_NVS_KEY,       EEPROM_MAGIC,    SCALEPAD_VERSION,     (uint16_t)sizeof(ScalePadStore)     },  // 3: T3b
  { ARP_PAD_NVS_NAMESPACE,     ARPPAD_NVS_KEY,         EEPROM_MAGIC,    ARPPAD_VERSION,       (uint16_t)sizeof(ArpPadStore)       },  // 4: T3c
  { CONTROLPAD_NVS_NAMESPACE,  CONTROLPAD_NVS_KEY,     CONTROLPAD_MAGIC,CONTROLPAD_VERSION,   (uint16_t)sizeof(ControlPadStore)   },  // 5: T4 NEW
  { BANKTYPE_NVS_NAMESPACE,    BANKTYPE_NVS_KEY_V2,    EEPROM_MAGIC,    BANKTYPE_VERSION,     (uint16_t)sizeof(BankTypeStore)     },  // 6: T5
  { SETTINGS_NVS_NAMESPACE,    SETTINGS_NVS_KEY,       EEPROM_MAGIC,    SETTINGS_VERSION,     (uint16_t)sizeof(SettingsStore)     },  // 7: T6
  { POTMAP_NVS_NAMESPACE,      POTMAP_NVS_KEY,         EEPROM_MAGIC,    POTMAP_VERSION,       (uint16_t)sizeof(PotMappingStore)   },  // 8: T7a
  { POTFILTER_NVS_NAMESPACE,   POTFILTER_NVS_KEY,      EEPROM_MAGIC,    POT_FILTER_VERSION,   (uint16_t)sizeof(PotFilterStore)    },  // 9: T7b (Monitor in T7)
  { LED_SETTINGS_NVS_NAMESPACE,LED_SETTINGS_NVS_KEY,   EEPROM_MAGIC,    LED_SETTINGS_VERSION, (uint16_t)sizeof(LedSettingsStore)  },  // 10: T8a
  { LED_SETTINGS_NVS_NAMESPACE,COLOR_SLOT_NVS_KEY,     COLOR_SLOT_MAGIC,COLOR_SLOT_VERSION,   (uint16_t)sizeof(ColorSlotStore)    },  // 11: T8b
  { LOOPPAD_NVS_NAMESPACE,     LOOPPAD_NVS_KEY,        EEPROM_MAGIC,    LOOPPAD_VERSION,      (uint16_t)sizeof(LoopPadStore)      },  // 12: T3 LOOP (Phase 1 declared, T3 b1 wires Phase 3)
};
static constexpr uint8_t NVS_DESCRIPTOR_COUNT = sizeof(NVS_DESCRIPTORS) / sizeof(NVS_DESCRIPTORS[0]);

// Tool-to-descriptor mapping: each tool checks descriptors in range [first, last] inclusive
// T3 spans 3 (bankpad+scalepad+arppad), T7 spans 2 (potmapping+potfilter), T8 spans 2 (ledsettings+colorslots)
static constexpr uint8_t TOOL_NVS_FIRST[] = { 0, 1, 2, 5, 6, 7, 8, 10 };   // T1..T8
static constexpr uint8_t TOOL_NVS_LAST[]  = { 0, 1, 4, 5, 6, 7, 9, 11 };   // T4=5 (ctrl single), shifts +1 after

#endif // KEYBOARD_DATA_H
