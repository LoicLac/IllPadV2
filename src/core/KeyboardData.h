#ifndef KEYBOARD_DATA_H
#define KEYBOARD_DATA_H

#include <stdint.h>
#include <stddef.h>
#include "HardwareConfig.h"

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

const uint8_t SETTINGS_VERSION = 10;  // Bumped: 9→10 (added batAdcAtFull)

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

struct ArpPotStore {
  uint16_t gateRaw;       // gate × 4095 (range 0-32760, i.e. 0.0-8.0; floor 0.005 on load)
  uint16_t shuffleDepthRaw;  // 0-4095 (maps to 0.0-1.0)
  uint8_t  division;      // ArpDivision enum (0-8)
  uint8_t  pattern;       // ArpPattern enum (0-4)
  uint8_t  octaveRange;      // 1-4
  uint8_t  shuffleTemplate;  // 0-9 (index into groove templates)
};

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
// Color Slots (Tool 7 COLOR page)
// =================================================================
#define COLOR_SLOT_COUNT       12
#define COLOR_SLOT_NVS_KEY     "ledcolors"
#define COLOR_SLOT_MAGIC       0xC010

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
  CSLOT_HOLD_ON,
  CSLOT_HOLD_OFF,
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
// LED Settings (Tool 7 DISPLAY + CONFIRM pages)
// All intensities are 0-100 (perceptual %). Fresh v1 — no migration.
// =================================================================
#define LED_SETTINGS_NVS_NAMESPACE "illpad_lset"
#define LED_SETTINGS_NVS_KEY       "ledsettings"
#define LED_SETTINGS_VERSION       5

struct LedSettingsStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  // --- Intensities (0-100, perceptual %) ---
  uint8_t  normalFgIntensity;     // default 85
  uint8_t  normalBgIntensity;     // default 10
  uint8_t  fgArpStopMin;          // default 30
  uint8_t  fgArpStopMax;          // default 100
  uint8_t  fgArpPlayMin;          // default 30  — LEGACY v2: unused (no pulse on playing), kept for NVS compat
  uint8_t  fgArpPlayMax;          // default 80  — solid intensity FG arpeg playing (between tick flashes)
  uint8_t  bgArpStopMin;          // default 8   — solid dim intensity BG arpeg stopped/idle
  uint8_t  bgArpStopMax;          // default 25  — LEGACY v2: unused (no pulse on BG), kept for NVS compat
  uint8_t  bgArpPlayMin;          // default 8   — solid dim intensity BG arpeg playing (between tick flashes)
  uint8_t  bgArpPlayMax;          // default 20  — LEGACY v2: unused (no pulse on BG playing), kept for NVS compat
  uint8_t  tickFlashFg;           // default 100
  uint8_t  tickFlashBg;           // default 25
  // --- Timing ---
  uint16_t pulsePeriodMs;         // default 1472 — FG arpeg stopped-loaded breathing only
  uint8_t  tickFlashDurationMs;   // default 30
  uint8_t  gammaTenths;           // 10-30 → gamma 1.0-3.0, default 20 (2.0). Reboot-only.
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
  uint16_t holdOnFadeMs;          // default 500, 0-1000 (fade IN on capture OFF->ON)
  uint16_t holdOffFadeMs;         // default 500, 0-1000 (fade OUT on release ON->OFF)
  uint8_t  octaveBlinks;          // default 3
  uint16_t octaveDurationMs;      // default 300
};

// =================================================================
// V2 — Bank Types & Scale Config
// =================================================================

enum BankType : uint8_t {
  BANK_NORMAL = 0,
  BANK_ARPEG  = 1,
  BANK_ANY    = 0xFF  // Used in PotRouter bindings (matches any type)
};

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
  ARP_UP             = 0,
  ARP_DOWN           = 1,
  ARP_UP_DOWN        = 2,
  ARP_RANDOM         = 3,
  ARP_ORDER          = 4,
  ARP_CASCADE        = 5,
  ARP_CONVERGE       = 6,
  ARP_DIVERGE        = 7,
  ARP_PEDAL_UP       = 8,
  ARP_UP_OCTAVE      = 9,
  ARP_DOWN_OCTAVE    = 10,
  ARP_CHORD          = 11,
  ARP_OCTAVE_WAVE    = 12,
  ARP_OCTAVE_BOUNCE  = 13,
  ARP_PROBABILITY    = 14,
  NUM_ARP_PATTERNS   = 15
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

#define BANKTYPE_NVS_KEY_V2  "config"
#define BANKTYPE_VERSION     2   // 1->2 : ajout scaleGroup[]

#define NUM_SCALE_GROUPS     4   // A, B, C, D (0 = none / banque independante)

struct BankTypeStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  uint8_t  types[NUM_BANKS];       // BankType enum cast
  uint8_t  quantize[NUM_BANKS];    // ArpStartMode enum
  uint8_t  scaleGroup[NUM_BANKS];  // 0 = none, 1..NUM_SCALE_GROUPS = A..D
};
static_assert(sizeof(BankTypeStore) <= NVS_BLOB_MAX_SIZE, "BankTypeStore exceeds NVS blob max");

#define COLOR_SLOT_VERSION  3

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
#define POTMAP_VERSION 1

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
}

inline void validateBankTypeStore(BankTypeStore& s) {
  uint8_t arpCount = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (s.types[i] > BANK_ARPEG) s.types[i] = BANK_NORMAL;
    if (s.types[i] == BANK_ARPEG) arpCount++;
    if (arpCount > MAX_ARP_BANKS) s.types[i] = BANK_NORMAL;
    if (s.quantize[i] >= NUM_ARP_START_MODES) s.quantize[i] = DEFAULT_ARP_START_MODE;
    if (s.scaleGroup[i] > NUM_SCALE_GROUPS) s.scaleGroup[i] = 0;
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
  // Intensity cross-validation (min <= max for all pulse ranges)
  if (s.fgArpStopMin > s.fgArpStopMax) s.fgArpStopMax = s.fgArpStopMin;
  if (s.fgArpPlayMin > s.fgArpPlayMax) s.fgArpPlayMax = s.fgArpPlayMin;
  if (s.bgArpStopMin > s.bgArpStopMax) s.bgArpStopMax = s.bgArpStopMin;
  if (s.bgArpPlayMin > s.bgArpPlayMax) s.bgArpPlayMax = s.bgArpPlayMin;
  // Timing ranges
  if (s.pulsePeriodMs < 500)  s.pulsePeriodMs = 500;
  if (s.pulsePeriodMs > 4000) s.pulsePeriodMs = 4000;
  if (s.tickFlashDurationMs < 10)  s.tickFlashDurationMs = 10;
  if (s.tickFlashDurationMs > 100) s.tickFlashDurationMs = 100;
  if (s.gammaTenths < 10) s.gammaTenths = 10;
  if (s.gammaTenths > 30) s.gammaTenths = 30;
  // Confirmation blink counts and durations (from ToolLedSettings adjustConfirmParam)
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

#define TEMPO_NVS_NAMESPACE     "illpad_tempo"  // Tempo BPM (global)
#define TEMPO_NVS_KEY           "bpm"

#define POTMAP_NVS_NAMESPACE    "illpad_pmap"   // User pot mapping (both contexts)
#define POTMAP_NVS_KEY          "mapping"

// ── Pot Filter ──────────────────────────────────
#define POTFILTER_NVS_NAMESPACE "illpad_pflt"
#define POTFILTER_NVS_KEY       "cfg"
const uint8_t POT_FILTER_VERSION = 2;  // v2: MCP3208 — removed snap100/actThresh10 (no EMA)

struct PotFilterStore {
    uint16_t magic;        // EEPROM_MAGIC
    uint8_t  version;      // POT_FILTER_VERSION
    uint8_t  sleepEn;      // 0=off, 1=on (default 1)
    uint16_t sleepMs;      // sleep delay ms (100-2000, default 500)
    uint8_t  deadband;     // ADC units (1-10, default 3)
    uint8_t  edgeSnap;     // ADC units from edges (0-10, default 3)
    uint8_t  wakeThresh;   // ADC units to wake from sleep (3-30, default 8)
};
static_assert(sizeof(PotFilterStore) <= NVS_BLOB_MAX_SIZE, "PotFilterStore too large");

inline void validatePotFilterStore(PotFilterStore& s) {
    if (s.sleepEn > 1)                             s.sleepEn = 1;
    if (s.sleepMs < 100 || s.sleepMs > 2000)       s.sleepMs = 500;
    if (s.deadband < 1 || s.deadband > 10)         s.deadband = 4;
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
};
static constexpr uint8_t NVS_DESCRIPTOR_COUNT = sizeof(NVS_DESCRIPTORS) / sizeof(NVS_DESCRIPTORS[0]);

// Tool-to-descriptor mapping: each tool checks descriptors in range [first, last] inclusive
// T3 spans 3 (bankpad+scalepad+arppad), T7 spans 2 (potmapping+potfilter), T8 spans 2 (ledsettings+colorslots)
static constexpr uint8_t TOOL_NVS_FIRST[] = { 0, 1, 2, 5, 6, 7, 8, 10 };   // T1..T8
static constexpr uint8_t TOOL_NVS_LAST[]  = { 0, 1, 4, 5, 6, 7, 9, 11 };   // T4=5 (ctrl single), shifts +1 after

#endif // KEYBOARD_DATA_H
