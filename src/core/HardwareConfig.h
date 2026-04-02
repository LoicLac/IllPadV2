#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <stdint.h>
#include <math.h>

// =================================================================
// 1. DEBUG & PRODUCTION
// =================================================================
#define DEBUG_SERIAL 1   // 1 = all debug messages (boot + runtime). 0 = complete silence, zero overhead.
#define DEBUG_HARDWARE  0 // 1 = print pot ADC values + button states every 500ms (loop). 0 = off.

// =================================================================
// 2. PIN ASSIGNMENTS — ESP32-S3-N8R16
// =================================================================
// See DOCUMENTATION/HARDWARE_CONNECTIONS.md for full wiring reference.

// --- I2C Bus ---
const uint8_t SDA_PIN = 8;   // GPIO8 — left header
const uint8_t SCL_PIN = 9;   // GPIO9 — left header
#define I2C_CLOCK_HZ 400000   // 100 kHz; use 400000 if bus has strong pull-ups and short wires

// --- MPR121 Capacitive Touch Sensors (x4) ---
const uint8_t ADDR_MPR121_A = 0x5A;  // Keys 0-11  (ADDR pin -> GND)
const uint8_t ADDR_MPR121_B = 0x5B;  // Keys 12-23 (ADDR pin -> VCC)
const uint8_t ADDR_MPR121_C = 0x5C;  // Keys 24-35 (ADDR pin -> SDA)
const uint8_t ADDR_MPR121_D = 0x5D;  // Keys 36-47 (ADDR pin -> SCL)

const int NUM_SENSORS = 4;
const int CHANNELS_PER_SENSOR = 12;
const int NUM_KEYS = NUM_SENSORS * CHANNELS_PER_SENSOR;  // 48

// --- LEDs — 8x SK6812 RGBW NeoPixel Stick (GRBW wire order) ---
const uint8_t LED_DATA_PIN = 13;  // GPIO13 — Single GPIO for NeoPixel data line
const int NUM_LEDS = 8;

// --- RGBW Color Type ---
struct RGBW {
  uint8_t r, g, b, w;
};

// --- System Colors (hardcoded, not editable via Tool 7) ---
static constexpr RGBW COL_ERROR     = {255,   0,   0,   0};  // Error — red
static constexpr RGBW COL_BOOT      = {  0,   0,   0, 255};  // Boot — clean W white
static constexpr RGBW COL_BOOT_FAIL = {255,   0,   0,   0};  // Boot fail — red
static constexpr RGBW COL_SETUP     = {128,   0, 255,   0};  // Setup comet — violet

// Battery gauge gradient (LED 0 = red, LED 7 = green) — no W channel
static constexpr RGBW COL_BATTERY[NUM_LEDS] = {
  {255,   0, 0, 0}, {255,  36, 0, 0}, {255,  73, 0, 0}, {255, 145, 0, 0},
  {200, 200, 0, 0}, {145, 255, 0, 0}, { 73, 255, 0, 0}, {  0, 255, 0, 0}
};

// --- Gamma Correction LUT (256 entries, gamma 2.0, floor=1 for i>=1) ---
// Applied per-channel (R,G,B,W) at final output stage in setPixel().
// Gamma 2.0 gives good low-intensity resolution on SK6812 RGBW LEDs.
// Floor=1 ensures any non-zero input produces at least minimal light.
// Generated: max(1, round(255 * pow(i/255, 2.0))) for i>=1, [0]=0
static const uint8_t GAMMA_LUT[256] = {
      0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   4,   4,
      4,   4,   5,   5,   5,   5,   6,   6,   6,   7,   7,   7,   8,   8,   8,   9,
      9,   9,  10,  10,  11,  11,  11,  12,  12,  13,  13,  14,  14,  15,  15,  16,
     16,  17,  17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  23,  23,  24,  24,
     25,  26,  26,  27,  28,  28,  29,  30,  30,  31,  32,  32,  33,  34,  35,  35,
     36,  37,  38,  38,  39,  40,  41,  42,  42,  43,  44,  45,  46,  47,  47,  48,
     49,  50,  51,  52,  53,  54,  55,  56,  56,  57,  58,  59,  60,  61,  62,  63,
     64,  65,  66,  67,  68,  69,  70,  71,  73,  74,  75,  76,  77,  78,  79,  80,
     81,  82,  84,  85,  86,  87,  88,  89,  91,  92,  93,  94,  95,  97,  98,  99,
    100, 102, 103, 104, 105, 107, 108, 109, 111, 112, 113, 115, 116, 117, 119, 120,
    121, 123, 124, 126, 127, 128, 130, 131, 133, 134, 136, 137, 139, 140, 142, 143,
    145, 146, 148, 149, 151, 152, 154, 155, 157, 158, 160, 162, 163, 165, 166, 168,
    170, 171, 173, 175, 176, 178, 180, 181, 183, 185, 186, 188, 190, 192, 193, 195,
    197, 199, 200, 202, 204, 206, 207, 209, 211, 213, 215, 217, 218, 220, 222, 224,
    226, 228, 230, 232, 233, 235, 237, 239, 241, 243, 245, 247, 249, 251, 253, 255
};

// Note: PERCEPTUAL_TO_LINEAR removed — the gamma LUT handles perceptual→LED
// conversion directly. The 0-100% user values are scaled to 0-255 linearly,
// then gamma LUT does the rest. No inverse-gamma step needed.

// --- Brightness Pot Response Curve ---
#define POT_CURVE_LOW_BIASED  0
#define POT_CURVE_LINEAR      1
#define POT_CURVE_SIGMOID     2

// Select curve (change to switch behavior):
//   LOW_BIASED  — bottom half covers 0-25% perceived, ideal for dark stages
//   LINEAR      — uniform perceptual steps
//   SIGMOID     — precision at both extremes
#define BRIGHTNESS_POT_CURVE  POT_CURVE_LOW_BIASED

// 256-entry LUT: ADC value (0-255) -> perceived brightness (0-100)
#if BRIGHTNESS_POT_CURVE == POT_CURVE_LOW_BIASED
static const uint8_t POT_BRIGHTNESS_CURVE[256] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      2,   2,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   3,   3,
      4,   4,   4,   4,   4,   4,   4,   5,   5,   5,   5,   5,   6,   6,   6,   6,
      6,   6,   7,   7,   7,   7,   8,   8,   8,   8,   8,   9,   9,   9,   9,  10,
     10,  10,  10,  11,  11,  11,  11,  12,  12,  12,  12,  13,  13,  13,  14,  14,
     14,  14,  15,  15,  15,  16,  16,  16,  17,  17,  17,  18,  18,  18,  19,  19,
     19,  20,  20,  20,  21,  21,  21,  22,  22,  23,  23,  23,  24,  24,  24,  25,
     25,  26,  26,  26,  27,  27,  28,  28,  28,  29,  29,  30,  30,  31,  31,  31,
     32,  32,  33,  33,  34,  34,  35,  35,  36,  36,  36,  37,  37,  38,  38,  39,
     39,  40,  40,  41,  41,  42,  42,  43,  43,  44,  44,  45,  45,  46,  47,  47,
     48,  48,  49,  49,  50,  50,  51,  52,  52,  53,  53,  54,  54,  55,  56,  56,
     57,  57,  58,  58,  59,  60,  60,  61,  62,  62,  63,  63,  64,  65,  65,  66,
     67,  67,  68,  68,  69,  70,  70,  71,  72,  72,  73,  74,  74,  75,  76,  76,
     77,  78,  79,  79,  80,  81,  81,  82,  83,  83,  84,  85,  86,  86,  87,  88,
     89,  89,  90,  91,  92,  92,  93,  94,  95,  95,  96,  97,  98,  98,  99, 100
};
#elif BRIGHTNESS_POT_CURVE == POT_CURVE_LINEAR
static const uint8_t POT_BRIGHTNESS_CURVE[256] = {
      0,   0,   1,   1,   2,   2,   2,   3,   3,   4,   4,   4,   5,   5,   5,   6,
      6,   7,   7,   7,   8,   8,   9,   9,   9,  10,  10,  11,  11,  11,  12,  12,
     13,  13,  13,  14,  14,  15,  15,  15,  16,  16,  16,  17,  17,  18,  18,  18,
     19,  19,  20,  20,  20,  21,  21,  22,  22,  22,  23,  23,  24,  24,  24,  25,
     25,  25,  26,  26,  27,  27,  27,  28,  28,  29,  29,  29,  30,  30,  31,  31,
     31,  32,  32,  33,  33,  33,  34,  34,  35,  35,  35,  36,  36,  36,  37,  37,
     38,  38,  38,  39,  39,  40,  40,  40,  41,  41,  42,  42,  42,  43,  43,  44,
     44,  44,  45,  45,  45,  46,  46,  47,  47,  47,  48,  48,  49,  49,  49,  50,
     50,  51,  51,  51,  52,  52,  53,  53,  53,  54,  54,  55,  55,  55,  56,  56,
     56,  57,  57,  58,  58,  58,  59,  59,  60,  60,  60,  61,  61,  62,  62,  62,
     63,  63,  64,  64,  64,  65,  65,  65,  66,  66,  67,  67,  67,  68,  68,  69,
     69,  69,  70,  70,  71,  71,  71,  72,  72,  73,  73,  73,  74,  74,  75,  75,
     75,  76,  76,  76,  77,  77,  78,  78,  78,  79,  79,  80,  80,  80,  81,  81,
     82,  82,  82,  83,  83,  84,  84,  84,  85,  85,  85,  86,  86,  87,  87,  87,
     88,  88,  89,  89,  89,  90,  90,  91,  91,  91,  92,  92,  93,  93,  93,  94,
     94,  95,  95,  95,  96,  96,  96,  97,  97,  98,  98,  98,  99,  99, 100, 100
};
#elif BRIGHTNESS_POT_CURVE == POT_CURVE_SIGMOID
static const uint8_t POT_BRIGHTNESS_CURVE[256] = {
      0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   3,   3,   3,   3,   3,   3,   3,   3,   4,   4,   4,   4,   4,
      4,   4,   5,   5,   5,   5,   5,   6,   6,   6,   6,   6,   7,   7,   7,   7,
      8,   8,   8,   9,   9,   9,   9,  10,  10,  11,  11,  11,  12,  12,  13,  13,
     13,  14,  14,  15,  15,  16,  16,  17,  18,  18,  19,  19,  20,  21,  21,  22,
     23,  23,  24,  25,  25,  26,  27,  28,  28,  29,  30,  31,  32,  33,  33,  34,
     35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,
     50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,
     66,  67,  67,  68,  69,  70,  71,  72,  72,  73,  74,  75,  75,  76,  77,  77,
     78,  79,  79,  80,  81,  81,  82,  82,  83,  84,  84,  85,  85,  86,  86,  87,
     87,  87,  88,  88,  89,  89,  89,  90,  90,  91,  91,  91,  91,  92,  92,  92,
     93,  93,  93,  93,  94,  94,  94,  94,  94,  95,  95,  95,  95,  95,  96,  96,
     96,  96,  96,  96,  96,  97,  97,  97,  97,  97,  97,  97,  97,  98,  98,  98,
     98,  98,  98,  98,  98,  98,  98,  98,  98,  98,  98,  99,  99,  99,  99,  99,
     99,  99,  99,  99,  99,  99,  99,  99,  99,  99,  99,  99,  99,  99,  99, 100
};
#endif

// --- Buttons (V2: 2 buttons, all active LOW with internal pull-up) ---
const uint8_t BTN_LEFT_PIN  = 12;  // GPIO12 — Left side, bank+scale+arp single-layer (hold + pad), modifier for right pots
const uint8_t BTN_REAR_PIN  = 21;  // GPIO21 — Rear, battery gauge + setup mode entry + modifier for rear pot

const uint16_t CAL_WAIT_WINDOW_MS = 3000;   // Time after boot to press button
const uint16_t CAL_HOLD_DURATION_MS = 3000; // Hold 3s after press to enter calibration
const uint16_t CAL_AUTOCONFIG_COUNTDOWN_MS = 1000;
const uint8_t  CHASE_STEP_MS = 80;          // Speed of chase pattern (ms per LED step)

// --- Analog Pots (V2: 5 potentiometers — 4 right + 1 rear) ---
// ALL pots on ADC1 (GPIO 1–10) for reliable reads with BLE active.
const uint8_t POT_RIGHT1_PIN = 4;   // GPIO4  — ADC1_CH3 — Right side pot 1 (tempo / division)
const uint8_t POT_RIGHT2_PIN = 5;   // GPIO5  — ADC1_CH4 — Right side pot 2 (shape-gate / deadzone-shuffle)
const uint8_t POT_RIGHT3_PIN = 6;   // GPIO6  — ADC1_CH5 — Right side pot 3 (slew-pattern / pitchbend-shuffletemplate)
const uint8_t POT_RIGHT4_PIN = 7;   // GPIO7  — ADC1_CH6 — Right side pot 4 (base velocity / velocity variation)
const uint8_t POT_REAR_PIN   = 1;   // GPIO1  — ADC1_CH0 — Rear (LED brightness / pad sensitivity)
const uint8_t NUM_POTS = 5;

const int   POT_DEADZONE        = 50;    // ADC change threshold for debug display
const float POT_SMOOTHING_ALPHA = 0.02f; // EMA filter (lower = smoother, 0.02 = heavy smoothing)
const float POT_MOVE_THRESHOLD  = 4.0f;  // Smoothed ADC delta to register movement (higher = less jitter)
const int   POT_OUTPUT_DEADBAND = 15;     // Output deadband: smoothed must move ≥ this from last stable value

// --- Pot Catch & Bargraph ---
const int      POT_CATCH_WINDOW          = 100;   // ADC units (±2.4% of 4095) to catch stored value
const uint32_t POT_BARGRAPH_DURATION_MS  = 3000;  // Show bargraph for 3s after last movement (legacy, used as fallback)
const uint32_t POT_NVS_SAVE_DEBOUNCE_MS  = 10000; // Save to NVS 10s after last pot change

// --- Battery Monitoring ---
const uint8_t BAT_ADC_PIN = 10;              // GPIO10 — ADC1_CH9, left header
const float BAT_DIVIDER_RATIO = 2.0f;        // 100kΩ/100kΩ voltage divider
const float BAT_VOLTAGE_FULL = 4.2f;         // LiPo full charge
const float BAT_VOLTAGE_EMPTY = 3.3f;        // Conservative low cutoff
const uint32_t BAT_DISPLAY_DURATION_MS = 3000; // LED gauge display time
const uint32_t BAT_CHECK_INTERVAL_MS = 20000;  // Periodic battery check (20s)
const uint8_t  BAT_LOW_THRESHOLD_PCT = 20;     // Below this: bank LED blinks
const uint32_t BAT_LOW_BLINK_INTERVAL_MS = 3000; // Time between battery-low blink bursts
const uint8_t  BAT_LOW_BLINK_SPEED_MS = 50;      // On/off speed per rapid blink (3 blinks = 6 × this)

// =================================================================
// 3. PRESSURE RESPONSE TUNING
// =================================================================
// These constants are ported from the original Nano R4 project.
// They control the feel of the capacitive pads and are musically calibrated.

// --- Note On/Off Thresholds (adaptive, per-key) ---
const float PRESS_THRESHOLD_PERCENT  = 0.15f;
const float RELEASE_THRESHOLD_PERCENT = 0.08f;
const uint16_t MIN_PRESS_THRESHOLD   = 20;
const uint16_t MIN_RELEASE_THRESHOLD = 10;

// --- Aftertouch Response Curve ---
#define AFTERTOUCH_CURVE_EXP_INTENSITY  4.0f
#define AFTERTOUCH_CURVE_SIG_INTENSITY  2

// --- Aftertouch Smoothing ---
#define AFTERTOUCH_SMOOTHING_WINDOW_SIZE 4
#define AFTERTOUCH_SLEW_RATE_LIMIT       150

// --- Aftertouch Deadzone ---
const int AFTERTOUCH_DEADZONE_MAX_OFFSET = 250;

// --- Response Shape (pot-controlled at runtime) ---
const float RESPONSE_SHAPE_DEFAULT = 0.5f;
const uint16_t RESPONSE_SHAPE_DEFAULT_RAW = (uint16_t)(RESPONSE_SHAPE_DEFAULT * 4095.0f); // 2047

// =================================================================
// 4. CALIBRATION
// =================================================================

// --- MPR121 Supply Voltage & Autoconfig Limits (NXP AN3889) ---
// These define the safe ADC operating range for autoconfig at this VDD.
// USL = maximum safe baseline register value before non-linear region.
const float   MPR121_VDD     = 3.3f;
const uint8_t MPR121_VDD_USL = (uint8_t)((MPR121_VDD - 0.7f) / MPR121_VDD * 256.0f);  // 201
const uint8_t MPR121_VDD_TL  = (uint8_t)(MPR121_VDD_USL * 0.9f);                       // 180
const uint8_t MPR121_VDD_LSL = (uint8_t)(MPR121_VDD_USL * 0.65f);                      // 130

const uint16_t CAL_PRESSURE_MIN_DELTA_TO_VALIDATE = 300;
const uint16_t CAL_DEFAULT_TARGET_BASELINE = ((uint16_t)MPR121_VDD_TL << 2);  // 720

// --- Baseline Filter Profiles ---
// Controls how the MPR121 baseline tracking adapts between touches.
// All profiles freeze the baseline during touch (essential for pressure sensing).
// The difference is how the baseline behaves BETWEEN touches:
// how fast it recovers after release (rising) and how resistant it is to
// drifting downward before a touch is detected (falling).
enum BaselineProfile : uint8_t {
  BASELINE_ADAPTIVE   = 0,
  BASELINE_EXPRESSIVE = 1,
  BASELINE_PERCUSSIVE = 2,
  NUM_BASELINE_PROFILES = 3
};

const uint8_t DEFAULT_BASELINE_PROFILE = BASELINE_ADAPTIVE;

// --- Settings Ranges ---
const uint8_t  PAD_SENSITIVITY_MIN     = 5;
const uint8_t  PAD_SENSITIVITY_MAX     = 30;
const uint8_t  PAD_SENSITIVITY_DEFAULT = 15;   // = PRESS_THRESHOLD_PERCENT * 100
const uint16_t SLEW_RATE_MIN           = 10;
const uint16_t SLEW_RATE_MAX           = 500;
const uint16_t SLEW_RATE_DEFAULT       = 150;  // = AFTERTOUCH_SLEW_RATE_LIMIT
const uint8_t  AT_RATE_MIN             = 10;
const uint8_t  AT_RATE_MAX             = 100;
const uint8_t  AT_RATE_DEFAULT         = 25;   // = AFTERTOUCH_UPDATE_INTERVAL_MS
const uint16_t AT_DEADZONE_MIN         = 0;
const uint16_t AT_DEADZONE_MAX         = 250;  // = AFTERTOUCH_DEADZONE_MAX_OFFSET
const uint16_t AT_DEADZONE_DEFAULT     = 0;

// --- BLE Connection Interval Presets ---
enum BleInterval : uint8_t {
  BLE_LOW_LATENCY   = 0,   // 7.5ms (best response, more battery)
  BLE_NORMAL        = 1,   // 15ms (balanced, Apple compatible)
  BLE_BATTERY_SAVER = 2,   // 30ms (saves battery, higher latency)
  BLE_OFF           = 3,   // BLE disabled — USB only (saves ~35KB RAM, faster boot)
  NUM_BLE_INTERVALS = 4
};
const uint8_t DEFAULT_BLE_INTERVAL = BLE_NORMAL;

// --- Clock Mode ---
enum ClockMode : uint8_t {
  CLOCK_SLAVE  = 0,   // Sync to external clock (DAW)
  CLOCK_MASTER = 1,   // Generate clock from pot tempo
  NUM_CLOCK_MODES = 2
};
const uint8_t  DEFAULT_CLOCK_MODE         = CLOCK_SLAVE;
const uint8_t  DEFAULT_PANIC_ON_RECONNECT = 1;  // yes — CC123 on all channels when BLE reconnects
const uint16_t DEFAULT_BAT_ADC_AT_FULL    = 0;  // 0 = uncalibrated, uses theoretical value
// Theoretical ADC at full (4.2V): 4.2 / BAT_DIVIDER_RATIO / 3.3 * 4095 ≈ 2606
const uint16_t BAT_ADC_FULL_THEORETICAL   = 2606;

// --- Arp Start Quantize (per-bank, set in Tool 4) ---
enum ArpStartMode : uint8_t {
  ARP_START_IMMEDIATE = 0,   // Fire on next division boundary (current behavior)
  ARP_START_BEAT      = 1,   // Snap to next beat (24 ticks)
  ARP_START_BAR       = 2,   // Snap to next bar (96 ticks, 4/4)
  NUM_ARP_START_MODES = 3
};
const uint8_t DEFAULT_ARP_START_MODE = ARP_START_IMMEDIATE;

// --- Double-Tap Window (ARPEG HOLD mode) ---
const uint8_t DOUBLE_TAP_MS_MIN     = 100;
const uint8_t DOUBLE_TAP_MS_MAX     = 250;
const uint8_t DOUBLE_TAP_MS_DEFAULT = 150;

// =================================================================
// 5. LED DISPLAY — Setup-mode timing (not user-configurable)
// =================================================================

// --- Setup Comet Chase ---
const uint8_t  LED_SETUP_CHASE_SPEED_MS     = 180;  // Time per step (~2.5s round trip)

// --- Bargraph Duration (configurable via Tool 5) ---
const uint16_t LED_BARGRAPH_DURATION_MIN     = 1000;
const uint16_t LED_BARGRAPH_DURATION_MAX     = 10000;
const uint16_t LED_BARGRAPH_DURATION_DEFAULT = 3000;

// --- Play Ack Timing (fixed, not in LedSettingsStore) ---
const uint8_t  LED_CONFIRM_UNIT_MS           = 50;   // Base phase unit for play ack

// =================================================================
// 6. MIDI
// =================================================================
const uint8_t MIDI_BASE_NOTE = 36;          // C2
// Velocity is now per-bank (baseVelocity + velocityVariation in BankSlot)
const uint8_t DEFAULT_BASE_VELOCITY      = 100;  // Default base velocity for new banks
const uint8_t DEFAULT_VELOCITY_VARIATION = 0;     // Default variation (0 = fixed)
const uint16_t DEFAULT_PITCH_BEND_OFFSET = 8192;  // Center (no bend)

// --- Tempo ---
const uint16_t TEMPO_BPM_MIN = 10;
const uint16_t TEMPO_BPM_MAX = 260;
const uint16_t TEMPO_BPM_DEFAULT = 120;

// --- Bank System ---
// Hold bank button + press one of 8 pads to select bank (MIDI channel 1–8).
// Each constant is a pad index (0–47), configurable at compile time.
const uint8_t NUM_BANKS = 8;
const uint8_t DEFAULT_BANK = 0;  // Bank 1 (MIDI channel 1)
#define BANK_NVS_NAMESPACE "illpad_bank"
#define BANK_NVS_KEY       "bank"

// --- Poly-aftertouch rate limiting ---
// Max poly-aftertouch update rate per pad. I2C supports ~250 polls/s; this caps MIDI event rate
// per key. Lower interval = smoother response, more BLE/USB traffic. Higher = fewer events.
const uint16_t AFTERTOUCH_UPDATE_INTERVAL_MS = 25;  // Min ms between two aftertouch messages per pad (25 = ~40 Hz). Use 20 for ~50 Hz, 30 for ~33 Hz.
const uint8_t  AFTERTOUCH_CHANGE_THRESHOLD = 1;    // Min pressure delta (0–127) to send an update; avoids sending unchanged values

// =================================================================
// 7. DEVICE IDENTITY
// =================================================================
// Base logical device name (no transport suffix)
#define DEVICE_NAME "ILLPAD48"

// Per-transport advertised names (compile-time configurable)
#define DEVICE_NAME_BLE  DEVICE_NAME " BLE"
#define DEVICE_NAME_USB  DEVICE_NAME " USB"

#endif // HARDWARE_CONFIG_H
