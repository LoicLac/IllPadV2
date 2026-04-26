// ═══════════════════════════════════════════════
// DEBUG I2C — MPR121 ×4 chain test (ILLPAD V2 hardware)
//
// Bench utility, NOT the runtime firmware. Tests presence,
// identification, init and basic communication of each MPR121
// on the I2C bus. Tolerant to any partial population (0..4 chips,
// any subset of addresses, swapped sockets, dead chip, etc.).
//
// At boot: prompt to choose scan mode
//   1 → full I2C bus scan (0x03..0x77)
//   2 → MPR121 range only (0x5A..0x5D)
//
// Commands (Serial @ 115200, send + newline):
//   r / R   → run full diagnostic (scan + per-chip init/comm)
//   t / T   → toggle live touch display on/off
//   h / H   → help
// ═══════════════════════════════════════════════
#include <Arduino.h>
#include <Wire.h>

// --- Hardware (mirrors src/core/HardwareConfig.h) ---
static const uint8_t SDA_PIN      = 8;
static const uint8_t SCL_PIN      = 9;
static const uint32_t I2C_CLOCK   = 400000;

struct ChipDef {
    const char* label;   // socket sticker (A/B/C/D)
    uint8_t     addr;    // expected I2C address
    const char* range;   // pad range
    const char* addrPin; // ADDR pin tie
};

static const ChipDef EXPECTED[] = {
    { "A", 0x5A, "pads  0-11", "GND" },
    { "B", 0x5B, "pads 12-23", "VCC" },
    { "C", 0x5C, "pads 24-35", "SDA" },
    { "D", 0x5D, "pads 36-47", "SCL" },
};
static const uint8_t NUM_EXPECTED = sizeof(EXPECTED) / sizeof(EXPECTED[0]);

// --- MPR121 register map (subset needed for this test) ---
#define MPR121_TOUCH_LO     0x00  // touch status ELE0..7
#define MPR121_TOUCH_HI     0x01  // touch status ELE8..11 + flags
#define MPR121_FILTERED_LO  0x04  // filtered data ELE0 (LSB)  — 24 bytes total (LSB,MSB × 12)
#define MPR121_BASELINE     0x1E  // baseline ELE0  — 12 bytes
#define MPR121_ECR          0x5E  // Electrode Config Register
#define MPR121_CONFIG2      0x5D  // Filter / Global CDT (signature-ish on power-up = 0x24)
#define MPR121_SOFT_RESET   0x80  // write 0x63 to reset

// --- Per-chip diagnostic state ---
enum Verdict : uint8_t {
    V_UNKNOWN = 0,
    V_MISSING,
    V_PRESENT_NOT_MPR121,
    V_INIT_FAILED,
    V_COMM_ERROR,
    V_OK,
};

struct ChipState {
    bool     present;
    Verdict  verdict;
    uint8_t  config2_raw;     // raw CONFIG2 read after reset (default = 0x24 on MPR121)
    uint8_t  ecr_writeback;   // ECR readback after enabling 12 electrodes
    uint16_t baseline[12];    // ×4 to compare to filtered (baseline << 2)
    uint16_t filtered[12];
    uint16_t touch_bits;      // 12-bit mask, bit N = ELE_N touched
    uint8_t  err_step;        // first failing step (for diagnosis)
    const char* err_msg;
};

static ChipState g_state[NUM_EXPECTED];
static bool g_liveTouch = true;
static bool g_initialized = false;  // any chip ever passed init? (gates touch live)

// Scan mode: full bus (0x03..0x77) vs MPR121 range only (0x5A..0x5D).
// Chosen at boot, persists until reboot.
enum ScanMode : uint8_t { SCAN_ALL = 1, SCAN_MPR_ONLY = 2 };
static ScanMode g_scanMode = SCAN_ALL;

// --- I2C helpers (return true on success) ---
static bool i2c_ping(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

static bool i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

static bool i2c_read(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;  // repeated start
    size_t got = Wire.requestFrom((int)addr, (int)len);
    if (got != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

static bool i2c_read_u8(uint8_t addr, uint8_t reg, uint8_t* val) {
    return i2c_read(addr, reg, val, 1);
}

// --- I2C bus scan (full 0x03..0x77, or MPR121 range 0x5A..0x5D) ---
static void scanBus() {
    Serial.println();
    if (g_scanMode == SCAN_ALL) {
        Serial.println(F("─── I2C bus scan (FULL 0x03..0x77) ──────────"));
        Serial.print(F("    "));
        for (uint8_t col = 0; col < 16; col++) Serial.printf(" %X ", col);
        Serial.println();
        uint8_t total = 0;
        for (uint8_t row = 0; row < 8; row++) {
            Serial.printf("%02X: ", row * 16);
            for (uint8_t col = 0; col < 16; col++) {
                uint8_t addr = row * 16 + col;
                if (addr < 0x03 || addr > 0x77) { Serial.print(F(" . ")); continue; }
                if (i2c_ping(addr)) { Serial.printf("%02X ", addr); total++; }
                else                { Serial.print(F(" . ")); }
            }
            Serial.println();
        }
        Serial.printf("Total: %u device(s) responding.\n", total);
    } else {
        Serial.println(F("─── I2C bus scan (MPR121 range 0x5A..0x5D) ──"));
        uint8_t total = 0;
        for (uint8_t addr = 0x5A; addr <= 0x5D; addr++) {
            bool ack = i2c_ping(addr);
            Serial.printf("  0x%02X : %s\n", addr, ack ? "ACK" : " .");
            if (ack) total++;
        }
        Serial.printf("Total: %u device(s) responding in range.\n", total);
    }
}

// --- Per-chip diagnostic ---
//   Step 1: ping (presence)
//   Step 2: read CONFIG2 (= 0x24 on a fresh MPR121 → identification)
//   Step 3: soft reset (0x80 ← 0x63), wait, re-check CONFIG2
//   Step 4: enable 12 electrodes via ECR (0x5E ← 0x0C), readback
//   Step 5: read 12 baselines + 12 filtered values
static void diagnoseChip(uint8_t i) {
    ChipState& st = g_state[i];
    const ChipDef& def = EXPECTED[i];
    st = {};  // reset

    // Step 1 — presence
    st.present = i2c_ping(def.addr);
    if (!st.present) { st.verdict = V_MISSING; return; }

    // Step 2 — identification: read CONFIG2 (default 0x24 on MPR121)
    if (!i2c_read_u8(def.addr, MPR121_CONFIG2, &st.config2_raw)) {
        st.verdict = V_COMM_ERROR; st.err_step = 2; st.err_msg = "CONFIG2 read failed";
        return;
    }

    // Step 3 — soft reset
    if (!i2c_write_reg(def.addr, MPR121_SOFT_RESET, 0x63)) {
        st.verdict = V_COMM_ERROR; st.err_step = 3; st.err_msg = "soft-reset write failed";
        return;
    }
    delay(2);
    uint8_t cfg2_after_reset = 0;
    if (!i2c_read_u8(def.addr, MPR121_CONFIG2, &cfg2_after_reset)) {
        st.verdict = V_COMM_ERROR; st.err_step = 3; st.err_msg = "CONFIG2 read after reset failed";
        return;
    }
    st.config2_raw = cfg2_after_reset;
    if (cfg2_after_reset != 0x24) {
        // Default after reset on MPR121 = 0x24. Anything else → not an MPR121
        // (or chip in a wedged state).
        st.verdict = V_PRESENT_NOT_MPR121;
        st.err_step = 3;
        st.err_msg = "CONFIG2 != 0x24 after reset";
        return;
    }

    // Step 4 — enable 12 electrodes (ECR = 0x0C: ELE_EN=12, no proximity)
    if (!i2c_write_reg(def.addr, MPR121_ECR, 0x0C)) {
        st.verdict = V_INIT_FAILED; st.err_step = 4; st.err_msg = "ECR write failed";
        return;
    }
    if (!i2c_read_u8(def.addr, MPR121_ECR, &st.ecr_writeback)) {
        st.verdict = V_INIT_FAILED; st.err_step = 4; st.err_msg = "ECR readback failed";
        return;
    }
    if (st.ecr_writeback != 0x0C) {
        st.verdict = V_INIT_FAILED; st.err_step = 4; st.err_msg = "ECR readback mismatch";
        return;
    }

    delay(5);  // let baseline settle

    // Step 5 — read baselines (12 × u8, left-shifted ×4 to compare to filtered)
    uint8_t bl_raw[12] = {0};
    if (!i2c_read(def.addr, MPR121_BASELINE, bl_raw, 12)) {
        st.verdict = V_COMM_ERROR; st.err_step = 5; st.err_msg = "baseline read failed";
        return;
    }
    for (uint8_t k = 0; k < 12; k++) st.baseline[k] = (uint16_t)bl_raw[k] << 2;

    // filtered: 24 bytes, LSB then MSB per electrode
    uint8_t fd_raw[24] = {0};
    if (!i2c_read(def.addr, MPR121_FILTERED_LO, fd_raw, 24)) {
        st.verdict = V_COMM_ERROR; st.err_step = 5; st.err_msg = "filtered read failed";
        return;
    }
    for (uint8_t k = 0; k < 12; k++) {
        st.filtered[k] = (uint16_t)fd_raw[2*k] | ((uint16_t)fd_raw[2*k + 1] << 8);
    }

    st.verdict = V_OK;
    g_initialized = true;
}

static const char* verdictStr(Verdict v) {
    switch (v) {
        case V_OK:                  return "OK";
        case V_MISSING:             return "MISSING";
        case V_PRESENT_NOT_MPR121:  return "PRESENT_NOT_MPR121";
        case V_INIT_FAILED:         return "INIT_FAILED";
        case V_COMM_ERROR:          return "COMM_ERROR";
        default:                    return "UNKNOWN";
    }
}

static void printChipReport(uint8_t i) {
    const ChipDef& def = EXPECTED[i];
    const ChipState& st = g_state[i];

    Serial.println();
    Serial.printf("─── MPR121 [%s] @ 0x%02X (%s, ADDR→%s) ────\n",
                  def.label, def.addr, def.range, def.addrPin);
    Serial.printf("  verdict      : %s\n", verdictStr(st.verdict));

    if (st.verdict == V_MISSING) {
        Serial.println(F("  → no ACK on bus. Check power, ADDR pin, solder."));
        return;
    }

    Serial.printf("  CONFIG2 (0x5D) read : 0x%02X  (expected 0x24 after reset)\n", st.config2_raw);

    if (st.verdict == V_PRESENT_NOT_MPR121) {
        Serial.printf("  → device ACKs but doesn't behave like MPR121 (%s).\n", st.err_msg);
        return;
    }
    if (st.verdict == V_COMM_ERROR || st.verdict == V_INIT_FAILED) {
        Serial.printf("  ECR readback       : 0x%02X\n", st.ecr_writeback);
        Serial.printf("  → step %u: %s\n", st.err_step, st.err_msg ? st.err_msg : "?");
        return;
    }

    // OK
    Serial.printf("  ECR (0x5E) readback : 0x%02X  (expected 0x0C — 12 electrodes)\n", st.ecr_writeback);
    Serial.println(F("  electrode | baseline | filtered | delta"));
    for (uint8_t k = 0; k < 12; k++) {
        int diff = (int)st.baseline[k] - (int)st.filtered[k];
        Serial.printf("    ELE%-2u   |  %4u    |  %4u    | %+d\n",
                      k, st.baseline[k], st.filtered[k], diff);
    }
}

static void runFullDiagnostic() {
    g_initialized = false;
    Serial.println();
    Serial.println(F("════════════════════════════════════════════"));
    Serial.println(F(" FULL DIAGNOSTIC"));
    Serial.println(F("════════════════════════════════════════════"));

    scanBus();

    for (uint8_t i = 0; i < NUM_EXPECTED; i++) diagnoseChip(i);
    for (uint8_t i = 0; i < NUM_EXPECTED; i++) printChipReport(i);

    Serial.println();
    Serial.println(F("─── Summary ─────────────────────────────────"));
    uint8_t okCount = 0;
    for (uint8_t i = 0; i < NUM_EXPECTED; i++) {
        Serial.printf("  [%s] 0x%02X : %s\n",
                      EXPECTED[i].label, EXPECTED[i].addr, verdictStr(g_state[i].verdict));
        if (g_state[i].verdict == V_OK) okCount++;
    }
    Serial.printf("  → %u / %u chips OK.\n", okCount, NUM_EXPECTED);
    Serial.println();
    if (g_liveTouch && g_initialized) {
        Serial.println(F("Live touch display ON (toggle with 't', rescan with 'r')"));
    } else if (!g_initialized) {
        Serial.println(F("No chip initialized — live touch disabled."));
    }
    Serial.println();
}

// --- Live touch display: read TOUCH_LO/HI on every OK chip ---
static void updateLiveTouch() {
    static uint32_t lastUpdate = 0;
    static uint16_t lastBits[NUM_EXPECTED] = {0};
    uint32_t now = millis();
    if ((now - lastUpdate) < 50) return;  // 20 Hz refresh
    lastUpdate = now;

    bool anyChange = false;
    for (uint8_t i = 0; i < NUM_EXPECTED; i++) {
        if (g_state[i].verdict != V_OK) continue;
        uint8_t lo = 0, hi = 0;
        if (!i2c_read_u8(EXPECTED[i].addr, MPR121_TOUCH_LO, &lo)) continue;
        if (!i2c_read_u8(EXPECTED[i].addr, MPR121_TOUCH_HI, &hi)) continue;
        uint16_t bits = ((uint16_t)hi << 8) | lo;
        bits &= 0x0FFF;  // 12 electrodes only
        g_state[i].touch_bits = bits;
        if (bits != lastBits[i]) { anyChange = true; lastBits[i] = bits; }
    }
    if (!anyChange) return;

    // Print compact line: A:............ B:.X.....X.... C:............ D:............
    Serial.print(F("touch  "));
    for (uint8_t i = 0; i < NUM_EXPECTED; i++) {
        Serial.printf("%s:", EXPECTED[i].label);
        if (g_state[i].verdict != V_OK) {
            Serial.print(F("---no--init- "));
            continue;
        }
        for (uint8_t k = 0; k < 12; k++) {
            bool t = (g_state[i].touch_bits >> k) & 1;
            Serial.print(t ? 'X' : '.');
        }
        Serial.print(' ');
    }
    Serial.println();
}

static void printHelp() {
    Serial.println();
    Serial.println(F("─── Commands ────────────────────────────────"));
    Serial.println(F("  r  → run full diagnostic (scan + init each chip)"));
    Serial.println(F("  t  → toggle live touch display"));
    Serial.println(F("  h  → this help"));
    Serial.printf  ("  (scan mode chosen at boot: %s)\n",
                    g_scanMode == SCAN_ALL ? "FULL bus" : "MPR121 range only");
    Serial.println();
}

// --- Boot prompt: choose scan mode (blocks until '1' or '2') ---
static void promptScanMode() {
    Serial.println();
    Serial.println(F("─── Choose I2C scan mode ────────────────────"));
    Serial.println(F("  1) scan ALL bus (0x03..0x77)"));
    Serial.println(F("  2) scan ONLY MPR121 range (0x5A..0x5D)"));
    Serial.print  (F("  press 1 or 2 : "));
    while (true) {
        if (Serial.available()) {
            int c = Serial.read();
            if (c == '1') { g_scanMode = SCAN_ALL;      Serial.println(F("1  → FULL bus")); break; }
            if (c == '2') { g_scanMode = SCAN_MPR_ONLY; Serial.println(F("2  → MPR121 range only")); break; }
            // ignore other chars (newlines, etc.)
        }
        delay(10);
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);  // let USB-CDC come up
    Serial.println();
    Serial.println(F("════════════════════════════════════════════"));
    Serial.println(F(" ILLPAD V2 — DEBUG I2C / MPR121 chain test"));
    Serial.println(F("════════════════════════════════════════════"));
    Serial.printf("SDA=GPIO%u  SCL=GPIO%u  clock=%lu Hz\n",
                  SDA_PIN, SCL_PIN, (unsigned long)I2C_CLOCK);
    Serial.println(F("Expected chips:"));
    for (uint8_t i = 0; i < NUM_EXPECTED; i++) {
        Serial.printf("  [%s] 0x%02X  %s  (ADDR→%s)\n",
                      EXPECTED[i].label, EXPECTED[i].addr,
                      EXPECTED[i].range, EXPECTED[i].addrPin);
    }

    Wire.begin(SDA_PIN, SCL_PIN, I2C_CLOCK);

    promptScanMode();
    printHelp();
    runFullDiagnostic();
}

void loop() {
    // Manual command parsing
    while (Serial.available()) {
        int c = Serial.read();
        if (c == 'r' || c == 'R') runFullDiagnostic();
        else if (c == 't' || c == 'T') {
            g_liveTouch = !g_liveTouch;
            Serial.printf("Live touch display: %s\n", g_liveTouch ? "ON" : "OFF");
        }
        else if (c == 'h' || c == 'H' || c == '?') printHelp();
        // ignore newlines / other chars
    }

    if (g_liveTouch && g_initialized) updateLiveTouch();
}
