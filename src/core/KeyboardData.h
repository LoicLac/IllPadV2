#ifndef KEYBOARD_DATA_H
#define KEYBOARD_DATA_H

#include <stdint.h>
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
  uint8_t  bleInterval;         // 0-2 (BleInterval enum)
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
  uint16_t gateRaw;       // 0-4095 (maps to 0.0-1.0)
  uint16_t shuffleDepthRaw;  // 0-4095 (maps to 0.0-1.0)
  uint8_t  division;      // ArpDivision enum (0-8)
  uint8_t  pattern;       // ArpPattern enum (0-4)
  uint8_t  octaveRange;      // 1-4
  uint8_t  shuffleTemplate;  // 0-4 (index into groove templates)
};

// =================================================================
// LED Settings (Tool 7)
// =================================================================
#define LED_SETTINGS_NVS_NAMESPACE "illpad_lset"
#define LED_SETTINGS_NVS_KEY       "ledsettings"
#define LED_SETTINGS_VERSION       2

struct LedSettingsStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  // NORMAL banks
  uint8_t  normalFgIntensity;     // default 255
  uint8_t  normalBgIntensity;     // default 40
  // ARPEG banks
  uint8_t  fgArpStopMin;          // default 77
  uint8_t  fgArpStopMax;          // default 255
  uint8_t  fgArpPlayMin;          // default 77
  uint8_t  fgArpPlayMax;          // default 204
  uint8_t  fgTickFlash;           // default 255 (absolute)
  uint8_t  bgArpStopMin;          // default 20
  uint8_t  bgArpStopMax;          // default 64
  uint8_t  bgArpPlayMin;          // default 20
  uint8_t  bgArpPlayMax;          // default 51
  uint8_t  bgTickFlash;           // default 64 (absolute)
  uint8_t  absoluteMax;           // default 255
  // TIMING
  uint16_t pulsePeriodMs;         // default 1472
  uint8_t  tickFlashDurationMs;   // default 30
  // CONFIRMATIONS
  uint8_t  bankBlinks;            // 1-3, default 3
  uint16_t bankDurationMs;        // default 300
  uint8_t  bankBrightnessPct;     // default 50
  uint8_t  scaleRootBlinks;       // default 2
  uint16_t scaleRootDurationMs;   // default 200
  uint8_t  scaleModeBlinks;       // default 2
  uint16_t scaleModeDurationMs;   // default 200
  uint8_t  scaleChromBlinks;      // default 2
  uint16_t scaleChromDurationMs;  // default 200
  uint8_t  holdOnFlashMs;         // default 150
  uint16_t holdFadeMs;            // default 300 (HOLD OFF fade-out)
  uint16_t stopFadeMs;            // default 300 (STOP fade-out, independent)
  uint8_t  playBeatCount;         // 1-4, default 3
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
// V2 — Arp Enums
// =================================================================

enum ArpPattern : uint8_t {
  ARP_UP       = 0,
  ARP_DOWN     = 1,
  ARP_UP_DOWN  = 2,
  ARP_RANDOM   = 3,
  ARP_ORDER    = 4,
  NUM_ARP_PATTERNS = 5
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
#define BANKTYPE_NVS_KEY        "types"

#define SCALE_NVS_NAMESPACE     "illpad_scale"
// Keys: "cfg_0" through "cfg_7" (per bank)

#define SCALE_PAD_NVS_NAMESPACE "illpad_spad"
#define SCALE_PAD_ROOT_KEY      "root_pads"
#define SCALE_PAD_MODE_KEY      "mode_pads"
#define SCALE_PAD_CHROM_KEY     "chrom_pad"

#define ARP_PAD_NVS_NAMESPACE   "illpad_apad"
// pat_pads and oct_pad removed — pattern/octave now on pots
#define ARP_PAD_HOLD_KEY        "hold_pad"
#define ARP_PAD_PS_KEY          "ps_pad"
#define ARP_PAD_OCT_KEY         "oct_pads"  // uint8_t[4], octave pads 1-4

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

// =================================================================
// V2 — Shared Double Buffer (replaces mutex)
// =================================================================

struct SharedKeyboardState {
  uint8_t keyIsPressed[NUM_KEYS];
  uint8_t pressure[NUM_KEYS];
};

// =================================================================
// V2 — Arp Constants
// =================================================================

const uint8_t MAX_ARP_BANKS    = 4;
const uint8_t MAX_ARP_NOTES    = 48;
const uint8_t MAX_ARP_SEQUENCE = 192;  // 48 notes × 4 octaves
const uint8_t MAX_ARP_OCTAVES  = 4;

#endif // KEYBOARD_DATA_H
