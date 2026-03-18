#ifndef KEYBOARD_DATA_H
#define KEYBOARD_DATA_H

#include <stdint.h>
#include "HardwareConfig.h"

// =================================================================
// Musical Data Structures
// =================================================================

const int NOTE_STACK_SIZE = 48;  // Full polyphony

struct Note {
  uint8_t pitch;
  uint16_t value;  // Pressure or velocity
};

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
// Note Map Data — pad-to-MIDI-note mapping, stored in NVS
// =================================================================

const uint8_t NOTEMAP_UNMAPPED = 0xFF;  // Pad produces no MIDI output
const uint8_t NOTEMAP_VERSION  = 1;
const uint8_t NOTEMAP_MAX_BASE_NOTE = 70;  // Highest allowed base note

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

const uint8_t SETTINGS_VERSION = 2;  // Bumped: 1→2 (slewRate moved to PotParamsStore)

struct SettingsStore {
  uint16_t magic;               // EEPROM_MAGIC (0xBEEF)
  uint8_t  version;             // SETTINGS_VERSION
  uint8_t  baselineProfile;     // 0-2 (BaselineProfile enum)
  uint8_t  padSensitivity;      // 5-30 (press threshold percent)
  uint8_t  aftertouchRate;      // 10-100 (ms between aftertouch msgs)
  uint16_t aftertouchDeadzone;  // 0-250 (delta offset before AT starts)
};

#define SETTINGS_NVS_NAMESPACE "illpad_set"
#define SETTINGS_NVS_KEY       "settings"

// =================================================================
// Pot Parameters — response shape + slew rate, stored in NVS
// Saved with 10s debounce from runtime pot control.
// =================================================================

const uint8_t POT_PARAMS_VERSION = 1;

struct PotParamsStore {
  uint16_t magic;              // EEPROM_MAGIC (0xBEEF)
  uint8_t  version;            // POT_PARAMS_VERSION
  uint8_t  reserved;
  uint16_t responseShapeRaw;   // 0-4095 (ADC space, maps to 0.0-1.0)
  uint16_t slewRate;           // SLEW_RATE_MIN-SLEW_RATE_MAX
};

#define POT_PARAMS_NVS_NAMESPACE "illpad_pot"
#define POT_PARAMS_NVS_KEY       "params"

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
  uint8_t     lastResolvedNote[NUM_KEYS]; // 0xFF = no active note
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
  DIV_1_1   = 0,   // Whole note
  DIV_1_2   = 1,   // Half note
  DIV_1_4   = 2,   // Quarter note
  DIV_1_8   = 3,   // Eighth note
  DIV_1_16  = 4,   // Sixteenth note
  DIV_1_8T  = 5,   // Eighth triplet
  DIV_1_16T = 6,   // Sixteenth triplet
  DIV_1_32  = 7,   // Thirty-second note
  NUM_ARP_DIVISIONS = 8
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
#define ARP_PAD_PATTERN_KEY     "pat_pads"
#define ARP_PAD_OCT_KEY         "oct_pad"
#define ARP_PAD_HOLD_KEY        "hold_pad"
#define ARP_PAD_PS_KEY          "ps_pad"

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
