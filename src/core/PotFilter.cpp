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
static const uint8_t  REAR_DIVISOR     = 20;   // ~50 Hz rear pot (1000 Hz / 20)
static const uint32_t PEEK_INTERVAL_MS = 50;   // Sleep peek period

// --- Default config (MCP3208, ~2 LSB noise at 3.3V) ---
static void applyDefaults() {
    s_cfg.magic     = EEPROM_MAGIC;
    s_cfg.version   = POT_FILTER_VERSION;
    s_cfg.sleepEn   = 1;     // sleep enabled
    s_cfg.sleepMs   = 500;   // 500ms to sleep
    s_cfg.deadband  = 3;     // 3 ADC units (~1365 positions on 4096)
    s_cfg.edgeSnap  = 3;     // 3 ADC units from edges
    s_cfg.wakeThresh = 8;    // 8 ADC units to wake from sleep
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

    // Init SPI bus with custom pins, then MCP3208
    SPI.begin(MCP3208_SCK_PIN, MCP3208_MISO_PIN,
              MCP3208_MOSI_PIN, MCP3208_CS_PIN);
    s_mcp.begin(MCP3208_CS_PIN);

    for (uint8_t i = 0; i < NUM_POTS; i++) {
        uint16_t initial = (uint16_t)s_mcp.read(i);

        PotData& p = s_pots[i];
        p.raw           = initial;
        p.stable        = initial;
        p.state         = POT_ACTIVE;
        p.moved         = false;
        p.lastMoveMs    = millis();
        p.lastPeekMs    = millis();
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
            p.raw = (uint16_t)s_mcp.read(i);

            int16_t delta = (int16_t)p.raw - (int16_t)p.sleepBaseline;
            if (delta < 0) delta = -delta;

            if ((uint16_t)delta > s_cfg.wakeThresh) {
                p.state = POT_ACTIVE;
                p.lastMoveMs = now;
                // fall through to ACTIVE below
            } else {
                p.moved = false;
                continue;
            }
        }

        // ── ACTIVE: read + deadband ──
        p.raw = (uint16_t)s_mcp.read(i);

        // Deadband gate (direct integer compare, no EMA)
        int16_t diff = (int16_t)p.raw - (int16_t)p.stable;
        if (diff < 0) diff = -diff;

        if (diff >= (int16_t)s_cfg.deadband) {
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

        // Sleep transition
        if (s_cfg.sleepEn && (now - p.lastMoveMs) > s_cfg.sleepMs) {
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
