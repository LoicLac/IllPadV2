#include "PotFilter.h"
#include "HardwareConfig.h"
#include <Arduino.h>
#include <SPI.h>
#include <MCP_ADC.h>

// ── PotFilter — Core 1 only, not thread-safe ──
// MCP3208 external ADC via SPI. ~2 LSB noise at 3.3V → no oversampling, no EMA.
// Chain: SPI read → deadband gate → edge snap → moved flag.

namespace {

// --- State machine (2 states, no SETTLING — no EMA to settle) ---
enum PotState : uint8_t { POT_SLEEPING, POT_ACTIVE };

struct PotData {
    uint16_t raw;               // Last SPI read (12-bit, 0-4095)
    uint16_t stable;            // Post-deadband (output)
    PotState state;
    bool     moved;             // Crossed deadband this cycle
    uint32_t lastMoveMs;        // For sleep timeout
    uint32_t lastPeekMs;        // For sleep peek interval
    uint16_t sleepBaseline;     // Raw ADC when entering sleep
};

static MCP3208   s_mcp;                    // Hardware SPI (default &SPI)
static PotData   s_pots[NUM_POTS];
static PotFilterStore s_cfg;
static uint8_t   s_rearCounter;            // Modulo counter for ~50 Hz rear

// --- Internal constants ---
static const uint8_t  REAR_DIVISOR           = 20;    // ~50 Hz rear pot (1000 Hz / 20)
static const uint32_t PEEK_INTERVAL_MS       = 50;    // Sleep peek period
static const uint16_t DELTA_MAX_PER_FRAME    = 300;   // Opt2: max legitimate inter-frame jump (bit-error reject)
static const uint8_t  REAR_POT_INDEX         = 4;     // Pot 4 = rear (distinct treatment)

// --- Default config (deadband sized for residual NeoPixel coupling post HW fix) ---
// Measurements after NeoPixel bulk cap + 10uF VREF : dmax typical 6-10 LSB,
// occasional 12-15 LSB spikes. Deadband=10 eliminates >95% of false events
// while keeping 410 effective positions (3x MIDI CC resolution).
static void applyDefaults() {
    s_cfg.magic      = EEPROM_MAGIC;
    s_cfg.version    = POT_FILTER_VERSION;
    s_cfg.sleepEn    = 1;                     // sleep enabled (pot 4 only at runtime)
    s_cfg.sleepMs    = 500;
    s_cfg.perPotDeadband[0] = 10;             // R1 live (noise mid-course ~dmax 8)
    s_cfg.perPotDeadband[1] = 10;             // R2 live
    s_cfg.perPotDeadband[2] = 10;             // R3 live
    s_cfg.perPotDeadband[3] = 10;             // R4 live
    s_cfg.perPotDeadband[4] = 8;              // rear — cleaner post HW fix, UX privilegiee
    s_cfg.edgeSnap   = 3;
    s_cfg.wakeThresh = 8;                     // aligned with pot 4 deadband (pot 4 = only sleeping pot)
}

// --- Read helpers ---
// Opt2 component: single read with range clamp + invert (CCW/CW swap).
static inline uint16_t readPotRaw(uint8_t i) {
    int16_t v = s_mcp.read(i);
    if (v < 0)    v = 0;
    if (v > 4095) v = 4095;
    return (uint16_t)(4095 - v);
}

// Opt4: median-of-3 reads for pot 4 only. ~90 us extra per rear cycle.
static inline uint16_t readPotRawMedian3(uint8_t i) {
    uint16_t r0 = readPotRaw(i);
    uint16_t r1 = readPotRaw(i);
    uint16_t r2 = readPotRaw(i);
    // Sort 3 elements in place
    if (r0 > r1) { uint16_t t = r0; r0 = r1; r1 = t; }
    if (r1 > r2) { uint16_t t = r1; r1 = r2; r2 = t; }
    if (r0 > r1) { uint16_t t = r0; r0 = r1; r1 = t; }
    return r1;  // median
}

} // anonymous namespace

// =================================================================
// Public API
// =================================================================

void PotFilter::begin() {
    static bool s_begun = false;
    if (s_begun) return;
    s_begun = true;

    applyDefaults();
    s_rearCounter = 0;

    // ── Boot hardening : SPI peripheral + rail stabilization ─────────
    SPI.begin(MCP3208_SCK_PIN, MCP3208_MISO_PIN,
              MCP3208_MOSI_PIN, MCP3208_CS_PIN);
    delay(10);  // Let ESP32 SPI peripheral + 3.3V rail settle

    s_mcp.begin(MCP3208_CS_PIN);
    delay(10);  // RC filter (16k x 100nF -> 5tau = 8ms) + MCP internal state

    // Discard first 2 conversions per channel : the MCP3208 sample
    // capacitor holds whatever was on the pin before CS toggle. First
    // conversions blend previous voltage with real input. Throw away.
    for (uint8_t i = 0; i < NUM_POTS; i++) {
        (void)s_mcp.read(i);
        (void)s_mcp.read(i);
    }

    // Seed each pot with MEDIAN of 5 consecutive reads : rejects any
    // single-read outlier caused by transient on VDD/VREF during boot.
    // Cost : ~150 us total for 5 pots x 5 reads at 1 MHz SPI. Negligible.
    for (uint8_t i = 0; i < NUM_POTS; i++) {
        uint16_t samples[5];
        for (uint8_t k = 0; k < 5; k++) {
            samples[k] = readPotRaw(i);   // Clamp + invert in helper
        }
        // Insertion sort 5 elements -> median = samples[2]
        for (uint8_t a = 1; a < 5; a++) {
            uint16_t v = samples[a];
            int8_t b = a - 1;
            while (b >= 0 && samples[b] > v) {
                samples[b + 1] = samples[b];
                b--;
            }
            samples[b + 1] = v;
        }
        uint16_t initial = samples[2];

        PotData& p = s_pots[i];
        p.raw           = initial;
        p.stable        = initial;
        p.state         = POT_ACTIVE;
        p.moved         = false;
        p.lastMoveMs    = millis();
        p.lastPeekMs    = millis();
        p.sleepBaseline = initial;

        #if DEBUG_SERIAL
        Serial.printf("[BOOT POT] Seed %u: median=%u (sorted=%u,%u,%u,%u,%u)\n",
                      i, initial,
                      samples[0], samples[1], samples[2],
                      samples[3], samples[4]);
        #endif
    }

    #if DEBUG_SERIAL
    Serial.println("[BOOT POT] MCP3208 boot OK.");
    #endif
}

void PotFilter::updateAll() {
    const uint32_t now = millis();

    for (uint8_t i = 0; i < NUM_POTS; i++) {
        // Rear pot: skip 19 cycles out of 20 (~50 Hz)
        if (i == REAR_POT_INDEX) {
            if (++s_rearCounter % REAR_DIVISOR != 0) continue;
        }

        PotData& p = s_pots[i];

        // ── SLEEPING: periodic peek (pot 4 only, enforced by Opt3 below) ──
        if (p.state == POT_SLEEPING) {
            if ((now - p.lastPeekMs) < PEEK_INTERVAL_MS) continue;
            p.lastPeekMs = now;
            p.raw = readPotRaw(i);   // Peek = cheap single read

            int16_t delta = (int16_t)p.raw - (int16_t)p.sleepBaseline;
            if (delta < 0) delta = -delta;

            if ((uint16_t)delta > s_cfg.wakeThresh) {
                p.state = POT_ACTIVE;
                p.lastMoveMs = now;
                // fall through to ACTIVE
            } else {
                p.moved = false;
                continue;
            }
        }

        // ── ACTIVE: read ──
        // Opt4: median-of-3 for pot 4 only (its ADC p2p sits at deadband edge).
        //       Cost ~60 us extra, fired every 20 ms -> negligible CPU.
        uint16_t newRaw = (i == REAR_POT_INDEX) ? readPotRawMedian3(i)
                                                : readPotRaw(i);

        // Opt2: delta-rate sanity check — reject impossibly fast jumps.
        //       Applied only to pots 0-3 (1 kHz loop, frame = 1 ms, 300 LSB = 75x
        //       the max human rotation speed). Skipped for pot 4: rear divisor
        //       makes its frame = 20 ms, so 300 LSB/frame would reject legitimate
        //       fast rotation (user twisting the brightness knob).
        if (i != REAR_POT_INDEX) {
            int16_t rawDelta = (int16_t)newRaw - (int16_t)p.raw;
            if (rawDelta < 0) rawDelta = -rawDelta;
            if ((uint16_t)rawDelta > DELTA_MAX_PER_FRAME) {
                p.moved = false;
                continue;
            }
        }
        p.raw = newRaw;

        // Opt1: per-pot deadband (pot 4 = 8, pots 0-3 = 5).
        int16_t diff = (int16_t)p.raw - (int16_t)p.stable;
        if (diff < 0) diff = -diff;

        if (diff >= (int16_t)s_cfg.perPotDeadband[i]) {
            p.stable = p.raw;
            p.moved = true;
            p.lastMoveMs = now;
        } else {
            p.moved = false;
        }

        // Edge snap
        if (p.stable < s_cfg.edgeSnap) {
            if (p.stable != 0) p.moved = true;
            p.stable = 0;
        } else if (p.stable > (4095 - s_cfg.edgeSnap)) {
            if (p.stable != 4095) p.moved = true;
            p.stable = 4095;
        }

        // Opt3: sleep transition for pot 4 only. Pots 0-3 stay in ACTIVE
        //       permanently -- they are live-performance knobs, never idle
        //       long enough to benefit from sleep, and the wake threshold
        //       would trigger false wakes on their residual ADC noise.
        if (i == REAR_POT_INDEX
            && s_cfg.sleepEn
            && (now - p.lastMoveMs) > s_cfg.sleepMs) {
            p.state = POT_SLEEPING;
            p.sleepBaseline = p.raw;
            p.lastPeekMs = now;
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
uint16_t PotFilter::getRaw(uint8_t pot)       { return s_pots[pot].raw; }
