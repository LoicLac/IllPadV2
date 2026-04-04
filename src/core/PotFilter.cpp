#include "PotFilter.h"
#include "HardwareConfig.h"
#include <Arduino.h>
#include <cmath>

// ── PotFilter — Core 1 only, not thread-safe ──
// Never call from Core 0 (sensing task). All state is static, no atomics.

namespace {

// --- State machine ---
enum PotState : uint8_t { POT_SLEEPING, POT_ACTIVE, POT_SETTLING };

struct PotData {
    uint16_t raw;               // Last single ADC read (or oversample avg)
    float    smoothed;          // Post-adaptive-EMA
    uint16_t stable;            // Post-deadband (output)
    float    activity;          // |raw - smoothed|
    PotState state;
    bool     moved;             // Crossed deadband this cycle
    uint32_t lastMoveMs;        // For sleep timeout
    uint32_t lastPeekMs;        // For sleep peek interval
    uint16_t sleepBaseline;     // Raw ADC when entering sleep
};

static PotData s_pots[NUM_POTS];
static PotFilterStore s_cfg;
static uint8_t s_rearCounter;   // Modulo counter for ~50 Hz rear

// --- Internal constants ---
// OVERSAMPLE_SHIFT: number of reads = 2^shift, output = sum >> shift.
// MUST match the number of analogRead() calls in oversampleRead().
//   shift=0 → 1 read  (no averaging, raw noise ~20 LSB)
//   shift=1 → 2 reads (noise ÷√2 ≈ ~14 LSB, ~20µs/pot)
//   shift=2 → 4 reads (noise ÷2   ≈ ~10 LSB, ~40µs/pot)  ← default
//   shift=3 → 8 reads (noise ÷√8  ≈  ~7 LSB, ~80µs/pot)
//   shift=4 → 16 reads(noise ÷4   ≈  ~5 LSB, ~160µs/pot)
// Output is always 0-4095 when reads and shift match.
static const uint8_t  OVERSAMPLE_SHIFT  = 4;    // 16× — measured NOISE 12-16 LSB with hardware RC filter
static const uint8_t  REAR_DIVISOR      = 20;   // ~50 Hz rear pot (1000 Hz / 50)
static const uint32_t PEEK_INTERVAL_MS  = 50;   // Sleep peek period
static const float    SETTLE_ALPHA      = 0.001f; // Near-frozen alpha in SETTLING

// --- Default config (used before NVS loads) ---
// ESP32-S3 ADC noise with hardware RC (220Ω+470nF): ~150 LSB raw, ~12-16 LSB after 16×.
// BLE impact: +1-2 LSB (negligible). Defaults tuned for 16× oversampling.
static void applyDefaults() {
    s_cfg.magic       = EEPROM_MAGIC;
    s_cfg.version     = POT_FILTER_VERSION;
    s_cfg.snap100     = 5;     // 0.05 snap multiplier
    s_cfg.actThresh10 = 200;   // 20.0 activity threshold (must be > raw noise to avoid feedback loop)
    s_cfg.sleepEn     = 1;     // sleep enabled
    s_cfg.sleepMs     = 500;   // 500ms to sleep
    s_cfg.deadband    = 20;    // 20 ADC units (25% margin above 16× noise of 12-16 LSB, ~205 positions)
    s_cfg.edgeSnap    = 12;    // 12 ADC units from edges
    s_cfg.wakeThresh  = 40;    // 40 ADC units to wake from sleep (well above noise)
}

static uint16_t oversampleRead(uint8_t pin) {
    const uint8_t count = 1 << OVERSAMPLE_SHIFT;  // 2^shift reads
    uint32_t sum = 0;
    for (uint8_t j = 0; j < count; j++) {
        sum += analogRead(pin);
    }
    return (uint16_t)(sum >> OVERSAMPLE_SHIFT);
}

} // anonymous namespace

// =================================================================
// Public API
// =================================================================

void PotFilter::begin() {
    applyDefaults();
    s_rearCounter = 0;

    for (uint8_t i = 0; i < NUM_POTS; i++) {
        pinMode(POT_PINS[i], INPUT);

        // Initial read: oversample for game pots, single for rear
        uint16_t initial;
        if (i < 4) {
            initial = oversampleRead(POT_PINS[i]);
        } else {
            initial = analogRead(POT_PINS[i]);
        }

        PotData& p = s_pots[i];
        p.raw       = initial;
        p.smoothed  = (float)initial;
        p.stable    = initial;
        p.activity  = 0.0f;
        p.state     = POT_ACTIVE;
        p.moved     = false;
        p.lastMoveMs  = millis();
        p.lastPeekMs  = millis();
        p.sleepBaseline = initial;
    }
}

void PotFilter::updateAll() {
    const uint32_t now = millis();

    for (uint8_t i = 0; i < NUM_POTS; i++) {
        // Rear pot: skip 19 cycles out of 20 (~50 Hz)
        if (i == 4) {
            if (++s_rearCounter % REAR_DIVISOR != 0) continue;
        }

        PotData& p = s_pots[i];

        // ── SLEEPING: periodic peek ──
        if (p.state == POT_SLEEPING) {
            if ((now - p.lastPeekMs) < PEEK_INTERVAL_MS) continue;
            p.lastPeekMs = now;
            p.raw = oversampleRead(POT_PINS[i]);  // oversampled peek (must match active reads)

            int16_t delta = (int16_t)p.raw - (int16_t)p.sleepBaseline;
            if (delta < 0) delta = -delta;

            if ((uint16_t)delta > s_cfg.wakeThresh) {
                p.state = POT_ACTIVE;
                p.smoothed = (float)p.raw;  // Reset EMA on wake
                p.lastMoveMs = now;
                // fall through to ACTIVE processing below
            } else {
                p.moved = false;
                continue;
            }
        }

        // ── ACTIVE / SETTLING: read + filter ──

        // Oversampling (4× game pots, 1× rear)
        if (i < 4) {
            p.raw = oversampleRead(POT_PINS[i]);
        } else {
            p.raw = analogRead(POT_PINS[i]);
        }

        // Activity = distance from smoothed
        p.activity = fabsf((float)p.raw - p.smoothed);
        const float snapMul   = (float)s_cfg.snap100 / 100.0f;
        const float actThresh = (float)s_cfg.actThresh10 / 10.0f;

        // Adaptive alpha
        float alpha;
        if (p.activity > actThresh) {
            // Active movement: snap toward raw
            float snapFactor = 1.0f - (actThresh / p.activity);
            alpha = snapFactor * snapMul;
            p.state = POT_ACTIVE;
            p.lastMoveMs = now;
        } else {
            // Settling: near-frozen EMA
            alpha = SETTLE_ALPHA;
            if (p.state == POT_ACTIVE) p.state = POT_SETTLING;

            // Sleep transition
            if (s_cfg.sleepEn && (now - p.lastMoveMs) > s_cfg.sleepMs) {
                p.state = POT_SLEEPING;
                p.sleepBaseline = p.raw;
                p.lastPeekMs = now;
                p.moved = false;
                continue;
            }
        }

        // Adaptive EMA
        p.smoothed += alpha * ((float)p.raw - p.smoothed);

        // Deadband gate
        float diff = fabsf(p.smoothed - (float)p.stable);
        if (diff >= (float)s_cfg.deadband) {
            p.stable = (uint16_t)(p.smoothed + 0.5f);
            p.moved = true;
        } else {
            p.moved = false;
        }

        // Edge snap (applied on stable output, NOT on smoothed)
        // Avoids discontinuous jumps when raw oscillates around edge threshold
        if (p.stable < s_cfg.edgeSnap) {
            if (p.stable != 0) p.moved = true;
            p.stable = 0;
        } else if (p.stable > (4095 - s_cfg.edgeSnap)) {
            if (p.stable != 4095) p.moved = true;
            p.stable = 4095;
        }
    }
}

void PotFilter::setConfig(const PotFilterStore& cfg) {
    s_cfg = cfg;
    validatePotFilterStore(s_cfg);
}

const PotFilterStore& PotFilter::getConfig() {
    return s_cfg;
}

// --- Output getters ---
uint16_t PotFilter::getStable(uint8_t pot)   { return s_pots[pot].stable; }
bool     PotFilter::hasMoved(uint8_t pot)     { return s_pots[pot].moved; }

// --- Monitor / debug getters ---
uint16_t PotFilter::getRaw(uint8_t pot)       { return s_pots[pot].raw; }
float    PotFilter::getSmoothed(uint8_t pot)   { return s_pots[pot].smoothed; }
float    PotFilter::getActivity(uint8_t pot)   { return s_pots[pot].activity; }
bool     PotFilter::isSleeping(uint8_t pot)    { return s_pots[pot].state == POT_SLEEPING; }
