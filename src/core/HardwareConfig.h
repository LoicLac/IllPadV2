#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <stdint.h>
#include <math.h>

// =================================================================
// 1. DEBUG & PRODUCTION
// =================================================================
#define DEBUG_SERIAL 1   // Set to 0 to disable Serial debug output (init, cal, transport)
#define PRODUCTION_MODE 0  // When 1: no Serial in main (setup/loop); calibration still uses Serial

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

// --- LEDs (x8) — Bank indicator LEDs ---
const uint8_t LED_PIN_1 = 4;   // Bank 1
const uint8_t LED_PIN_2 = 5;   // Bank 2
const uint8_t LED_PIN_3 = 6;   // Bank 3
const uint8_t LED_PIN_4 = 7;   // Bank 4
const uint8_t LED_PIN_5 = 15;  // Bank 5
const uint8_t LED_PIN_6 = 16;  // Bank 6
const uint8_t LED_PIN_7 = 17;  // Bank 7
const uint8_t LED_PIN_8 = 18;  // Bank 8
const int NUM_LEDS = 8;

// --- Onboard addressable RGB (WS2812-type, D6 on ESP32-S3-DevKitC-1) ---
#define INT_LED 1  // Set to 0 to disable onboard RGB LED (GPIO48) at compile time
const uint8_t RGB_LED_PIN = 48;  // v1.1; use 48 for v1.0
const uint8_t RGB_LED_BRIGHTNESS = 48;  // 0–255

// --- Buttons (V2: 3 buttons, all active LOW with internal pull-up) ---
const uint8_t BTN_LEFT_PIN  = 2;   // GPIO??? — Left side, bank select (hold + pad)
const uint8_t BTN_RIGHT_PIN = 3;   // GPIO??? — Right side, scale/arp controls (hold + pad)
const uint8_t BTN_REAR_PIN  = 11;  // GPIO??? — Rear, battery gauge + setup mode entry

const uint16_t CAL_WAIT_WINDOW_MS = 3000;   // Time after boot to press button
const uint16_t CAL_HOLD_DURATION_MS = 3000; // Hold 3s after press to enter calibration
const uint16_t CAL_AUTOCONFIG_COUNTDOWN_MS = 1000;
const uint8_t  CHASE_STEP_MS = 80;          // Speed of chase pattern (ms per LED step)

// --- Analog Pots (V2: 3 potentiometers) ---
const uint8_t POT_LEFT_PIN  = 1;   // GPIO??? — Left side, feel/sound (contextual NORMAL/ARPEG)
const uint8_t POT_RIGHT_PIN = 12;  // GPIO??? — Right side, notes/rhythm (contextual NORMAL/ARPEG)
const uint8_t POT_REAR_PIN  = 13;  // GPIO??? — Rear, config (tempo, etc.)
const uint8_t NUM_POTS = 3;

const int POT_DEADZONE = 30;     // ADC change threshold to register movement (ESP32 ADC noise ~10-20 LSB)
const float POT_SMOOTHING_ALPHA = 0.05f;

// --- Pot Catch & Bargraph ---
const int      POT_CATCH_WINDOW          = 100;   // ADC units (±2.4% of 4095) to catch stored value
const uint32_t POT_BARGRAPH_DURATION_MS  = 5000;  // Show bargraph for 5s after last movement
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

// =================================================================
// 5. MIDI
// =================================================================
const uint8_t MIDI_BASE_NOTE = 36;          // C2
const uint8_t MIDI_CHANNEL = 0;             // Channel 1 (zero-indexed), default at boot
const uint8_t MIDI_NOTE_ON_VELOCITY = 127;  // Fixed velocity

// --- Bank System ---
// Hold bank button + press one of 8 pads to select bank (MIDI channel 1–8).
// Each constant is a pad index (0–47), configurable at compile time.
const uint8_t NUM_BANKS = 8;
const uint8_t DEFAULT_BANK = 0;  // Bank 1 (MIDI channel 1)
#define BANK_NVS_NAMESPACE "illpad_bank"
#define BANK_NVS_KEY       "bank"

#define BANK_SELECT_PAD_FOR_BANK1  0
#define BANK_SELECT_PAD_FOR_BANK2  1
#define BANK_SELECT_PAD_FOR_BANK3  2
#define BANK_SELECT_PAD_FOR_BANK4  3
#define BANK_SELECT_PAD_FOR_BANK5  4
#define BANK_SELECT_PAD_FOR_BANK6  5
#define BANK_SELECT_PAD_FOR_BANK7  6
#define BANK_SELECT_PAD_FOR_BANK8  7

// --- Poly-aftertouch rate limiting ---
// Max poly-aftertouch update rate per pad. I2C supports ~250 polls/s; this caps MIDI event rate
// per key. Lower interval = smoother response, more BLE/USB traffic. Higher = fewer events.
const uint16_t AFTERTOUCH_UPDATE_INTERVAL_MS = 25;  // Min ms between two aftertouch messages per pad (25 = ~40 Hz). Use 20 for ~50 Hz, 30 for ~33 Hz.
const uint8_t  AFTERTOUCH_CHANGE_THRESHOLD = 1;    // Min pressure delta (0–127) to send an update; avoids sending unchanged values

// =================================================================
// 6. DEVICE IDENTITY
// =================================================================
// Base logical device name (no transport suffix)
#define DEVICE_NAME "ILLPAD48"

// Per-transport advertised names (compile-time configurable)
#define DEVICE_NAME_BLE  DEVICE_NAME " BLE"
#define DEVICE_NAME_USB  DEVICE_NAME " USB"

#endif // HARDWARE_CONFIG_H
