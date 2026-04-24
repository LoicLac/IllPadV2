/*
 * POT_DIAG v2 — MCP3208 + 5 pots diagnostic with runtime load simulation.
 *
 * Reproduces the 3 runtime stressors of the main firmware so we can measure
 * how much each one amplifies pot ADC jitter :
 *   - NeoPixel refresh storm (8 LEDs, random colors, 500 Hz refresh)
 *     -> GPIO13 toggles at 800 kHz for ~300 us every 2 ms
 *     -> 60-300 mA current spike on 3V3/5V rails
 *   - BLE "sauvage" (NimBLE server, max TX power, notify 1 kHz with 16-byte payload)
 *     -> continuous RF bursts, 2.4 GHz PA on/off
 *   - I2C MPR121 polling (4 sensors, 38 bytes each, 1 kHz)
 *     -> saturates I2C bus at 400 kHz on Core 1 (no Core 0 task here)
 *
 * Each stressor is independently toggleable so you can isolate which one
 * (if any) causes pot jitter.
 *
 * === BUILD / UPLOAD / MONITOR ===
 *   ~/.platformio/penv/bin/pio run -e pot-diag -t upload
 *   ~/.platformio/penv/bin/pio device monitor -b 115200
 *
 * === TERMINAL COMMANDS ===
 *   ENTER  sample 2000 pot readings per pot under current load, dump CSV
 *   n      toggle NeoPixel stress
 *   b      toggle BLE notify stress
 *   i      toggle I2C MPR121 polling stress
 *   a      all stressors ON
 *   0      all stressors OFF (quiet baseline)
 *   c      toggle WINDOWED aggregation mode (1 report / 5 s, ~6 lines per window)
 *   l      print current load state
 *   h      help
 *
 * === OUTPUT FORMAT ===
 * Every useful line starts with "POTDIAG,<type>,..." :
 *   POTDIAG,LOAD,neo=0/1,ble=0/1,i2c=0/1
 *   POTDIAG,SAMPLE,n=...,us=...,hz_per_pot=...        (ENTER burst)
 *   POTDIAG,HDR,pot,min,max,mean,stddev,p2p,d_le1,d_le5,d_le20,d_le100,d_gt100
 *   POTDIAG,DAT,<csv per pot>
 *   POTDIAG,END
 *   POTDIAG,WIN,id=...,t_ms=...,dur_ms=...,n=...,load=N.B.I.   (windowed mode, 1/5s)
 *   POTDIAG,WHDR,pot,min,max,p2p,mean,stddev,dmax,dgt5
 *   POTDIAG,W,<csv per pot>
 *
 * Column meaning for windowed mode :
 *   dmax = largest delta between 2 consecutive samples in window (detects spikes)
 *   dgt5 = count of consecutive deltas > 5 LSB (would trigger deadband in firmware)
 *
 * Values are RAW 12-bit, NO invert, NO filter.
 *
 * Restore main firmware when done :
 *   ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <MCP_ADC.h>
#include <Adafruit_NeoPixel.h>
#include <NimBLEDevice.h>
#include <math.h>

// ── Pin / constant defs — MUST match HardwareConfig.h ──
static const uint8_t  MCP_SCK_PIN  = 18;
static const uint8_t  MCP_MISO_PIN = 16;
static const uint8_t  MCP_MOSI_PIN = 15;
static const uint8_t  MCP_CS_PIN   = 17;
static const uint8_t  LED_PIN      = 13;
static const uint8_t  N_LEDS       = 8;
static const uint8_t  SDA_PIN      = 8;
static const uint8_t  SCL_PIN      = 9;
static const uint32_t I2C_HZ       = 400000;
static const uint8_t  MPR121_ADDRS[4] = {0x5A, 0x5B, 0x5C, 0x5D};
static const uint8_t  NPOTS        = 5;
static const uint8_t  POT_CH[NPOTS] = {0, 1, 2, 3, 4};

// ── Sampling + stress intervals ──
static const uint16_t N_SAMPLES             = 2000;
static const uint32_t WINDOW_MS             = 5000; // Aggregation window for windowed mode
static const uint32_t NEO_REFRESH_MS         = 2;   // 500 Hz aggressive refresh
static const uint32_t BLE_NOTIFY_MS          = 1;   // 1 kHz notify
static const uint32_t I2C_POLL_MS            = 1;   // 1 kHz poll (mimic sensing task)

// ── Global state ──
static MCP3208            s_mcp;
static Adafruit_NeoPixel  s_strip(N_LEDS, LED_PIN, NEO_GRBW + NEO_KHZ800);
static NimBLECharacteristic* s_bleChar = nullptr;

static bool s_neoStress  = false;
static bool s_bleStress  = false;
static bool s_i2cStress  = false;
static bool s_windowMode = false;   // Windowed aggregation (replaces old per-sample stream)

static uint32_t s_lastNeoTs  = 0;
static uint32_t s_lastBleTs  = 0;
static uint32_t s_lastI2cTs  = 0;

// ── Windowed statistics state ──
struct PotWinStats {
    uint16_t minV;
    uint16_t maxV;
    uint32_t sum;
    uint64_t sumSq;
    uint32_t n;
    uint16_t prev;    // Last sampled value (for delta tracking)
    uint16_t dmax;    // Max consecutive delta seen in window
    uint16_t dgt5;    // Count of consecutive deltas > 5 LSB
};
static PotWinStats s_win[NPOTS];
static uint32_t    s_winId       = 0;
static uint32_t    s_winStartMs  = 0;

// ── Pot reader (no invert, clamp range) ──
static inline uint16_t readPotRaw(uint8_t i) {
    int16_t v = s_mcp.read(i);
    if (v < 0)    v = 0;
    if (v > 4095) v = 4095;
    return (uint16_t)v;
}

// ── Stressors ──
static void neoTick(uint32_t now) {
    if (!s_neoStress) return;
    if (now - s_lastNeoTs < NEO_REFRESH_MS) return;
    s_lastNeoTs = now;
    for (uint8_t i = 0; i < N_LEDS; i++) {
        // Random RGBW at full brightness — maximum current draw
        uint8_t r = (uint8_t)random(256);
        uint8_t g = (uint8_t)random(256);
        uint8_t b = (uint8_t)random(256);
        uint8_t w = (uint8_t)random(256);
        s_strip.setPixelColor(i, s_strip.Color(r, g, b, w));
    }
    s_strip.show();
}

static void bleTick(uint32_t now) {
    if (!s_bleStress || s_bleChar == nullptr) return;
    if (now - s_lastBleTs < BLE_NOTIFY_MS) return;
    s_lastBleTs = now;
    uint8_t payload[16];
    for (uint8_t k = 0; k < 16; k++) payload[k] = (uint8_t)random(256);
    s_bleChar->setValue(payload, sizeof(payload));
    s_bleChar->notify();
}

static void i2cTick(uint32_t now) {
    if (!s_i2cStress) return;
    if (now - s_lastI2cTs < I2C_POLL_MS) return;
    s_lastI2cTs = now;
    // Mimic sensing task : read filtered data register from each of 4 MPR121s.
    // Chips are in stop-mode default state (no autoconfig done here), but the
    // I2C bus activity is what matters for this stress test.
    for (uint8_t s = 0; s < 4; s++) {
        Wire.beginTransmission(MPR121_ADDRS[s]);
        Wire.write(0x04);  // Filtered data base register
        if (Wire.endTransmission(false) != 0) continue;
        uint8_t got = Wire.requestFrom(MPR121_ADDRS[s], (uint8_t)38);
        for (uint8_t b = 0; b < got; b++) (void)Wire.read();
    }
}

// ── Output helpers ──
static void printLoad() {
    Serial.printf("POTDIAG,LOAD,neo=%d,ble=%d,i2c=%d\n",
                  s_neoStress ? 1 : 0,
                  s_bleStress ? 1 : 0,
                  s_i2cStress ? 1 : 0);
}

static void printHelp() {
    Serial.println("POTDIAG,HELP,commands=ENTER:sample|n:neo|b:ble|i:i2c|a:allON|0:allOFF|c:window|l:load|h:help");
    Serial.println("POTDIAG,HELP,samples_per_run=2000|window_ms=5000");
    Serial.println("POTDIAG,HELP,neo_refresh_hz=500|ble_notify_hz=1000|i2c_poll_hz=1000");
    Serial.println("POTDIAG,HELP,values_are_RAW_12bit_no_invert_no_filter");
    Serial.println("POTDIAG,HELP,window_mode=1_report_per_5s=6_lines_per_pot_block");
}

// ── Sampling ──
static void sampleOnce() {
    uint16_t minV[NPOTS], maxV[NPOTS];
    uint32_t sum[NPOTS];
    uint64_t sumSq[NPOTS];
    uint16_t dHist[NPOTS][5];
    uint16_t prev[NPOTS];

    for (uint8_t p = 0; p < NPOTS; p++) {
        minV[p]  = 0xFFFF;
        maxV[p]  = 0;
        sum[p]   = 0;
        sumSq[p] = 0;
        for (uint8_t k = 0; k < 5; k++) dHist[p][k] = 0;
        prev[p]  = readPotRaw(POT_CH[p]);
    }

    // Keep stressors running during sample acquisition
    uint32_t tStart = micros();
    for (uint16_t n = 0; n < N_SAMPLES; n++) {
        uint32_t now = millis();
        neoTick(now);
        bleTick(now);
        i2cTick(now);
        for (uint8_t p = 0; p < NPOTS; p++) {
            uint16_t v = readPotRaw(POT_CH[p]);
            if (v < minV[p]) minV[p] = v;
            if (v > maxV[p]) maxV[p] = v;
            sum[p]   += v;
            sumSq[p] += (uint32_t)v * (uint32_t)v;
            uint16_t d = (v > prev[p]) ? (uint16_t)(v - prev[p])
                                       : (uint16_t)(prev[p] - v);
            if      (d <= 1)   dHist[p][0]++;
            else if (d <= 5)   dHist[p][1]++;
            else if (d <= 20)  dHist[p][2]++;
            else if (d <= 100) dHist[p][3]++;
            else               dHist[p][4]++;
            prev[p] = v;
        }
    }
    uint32_t tEl = micros() - tStart;
    uint32_t rateHz = (tEl > 0)
        ? (uint32_t)((uint64_t)N_SAMPLES * 1000000ULL / (uint64_t)tEl)
        : 0;

    Serial.printf("POTDIAG,SAMPLE,n=%u,us=%lu,hz_per_pot=%lu\n",
                  N_SAMPLES, tEl, rateHz);
    Serial.println("POTDIAG,HDR,pot,min,max,mean,stddev,p2p,d_le1,d_le5,d_le20,d_le100,d_gt100");
    for (uint8_t p = 0; p < NPOTS; p++) {
        float mean = (float)sum[p] / (float)N_SAMPLES;
        float var  = (float)sumSq[p] / (float)N_SAMPLES - mean * mean;
        if (var < 0.0f) var = 0.0f;
        float stddev = sqrtf(var);
        uint16_t p2p = (uint16_t)(maxV[p] - minV[p]);
        Serial.printf("POTDIAG,DAT,%u,%u,%u,%.2f,%.2f,%u,%u,%u,%u,%u,%u\n",
                      p, minV[p], maxV[p], mean, stddev, p2p,
                      dHist[p][0], dHist[p][1], dHist[p][2],
                      dHist[p][3], dHist[p][4]);
    }
    Serial.println("POTDIAG,END");
}

// ── Windowed aggregation (replaces per-sample stream) ──
// Each window accumulates stats over WINDOW_MS ms and emits a compact report
// (1 header + 5 pot rows). Designed for prolonged tests without flooding Serial.

static void winReset(uint32_t now) {
    s_winStartMs = now;
    for (uint8_t p = 0; p < NPOTS; p++) {
        s_win[p].minV  = 0xFFFF;
        s_win[p].maxV  = 0;
        s_win[p].sum   = 0;
        s_win[p].sumSq = 0;
        s_win[p].n     = 0;
        s_win[p].prev  = readPotRaw(POT_CH[p]);  // seed for first delta
        s_win[p].dmax  = 0;
        s_win[p].dgt5  = 0;
    }
}

static void winEmit(uint32_t now) {
    s_winId++;
    uint32_t dur = now - s_winStartMs;
    Serial.printf("POTDIAG,WIN,id=%lu,t_ms=%lu,dur_ms=%lu,n=%lu,load=N%dB%dI%d\n",
                  s_winId, s_winStartMs, dur,
                  (unsigned long)s_win[0].n,
                  s_neoStress ? 1 : 0,
                  s_bleStress ? 1 : 0,
                  s_i2cStress ? 1 : 0);
    Serial.println("POTDIAG,WHDR,pot,min,max,p2p,mean,stddev,dmax,dgt5");
    for (uint8_t p = 0; p < NPOTS; p++) {
        PotWinStats& ps = s_win[p];
        if (ps.n == 0) continue;
        float mean = (float)ps.sum / (float)ps.n;
        float var  = (float)ps.sumSq / (float)ps.n - mean * mean;
        if (var < 0.0f) var = 0.0f;
        float std  = sqrtf(var);
        uint16_t p2p = (ps.maxV >= ps.minV) ? (uint16_t)(ps.maxV - ps.minV) : 0;
        Serial.printf("POTDIAG,W,%u,%u,%u,%u,%.2f,%.2f,%u,%u\n",
                      p, ps.minV, ps.maxV, p2p, mean, std, ps.dmax, ps.dgt5);
    }
}

static void winTick(uint32_t now) {
    // Accumulate one sample set
    for (uint8_t p = 0; p < NPOTS; p++) {
        uint16_t v = readPotRaw(POT_CH[p]);
        PotWinStats& ps = s_win[p];
        if (v < ps.minV) ps.minV = v;
        if (v > ps.maxV) ps.maxV = v;
        ps.sum   += v;
        ps.sumSq += (uint32_t)v * (uint32_t)v;
        uint16_t d = (v > ps.prev) ? (uint16_t)(v - ps.prev)
                                   : (uint16_t)(ps.prev - v);
        if (d > ps.dmax) ps.dmax = d;
        if (d > 5)       ps.dgt5++;
        ps.prev = v;
        ps.n++;
    }
    // Emit + reset if window elapsed
    if (now - s_winStartMs >= WINDOW_MS) {
        winEmit(now);
        winReset(now);
    }
}

// ── Setup / loop ──
void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.println();
    Serial.println("POTDIAG,BOOT,version=2,pots=5,samples_per_run=2000");
    Serial.println("POTDIAG,BOOT,stressors=neo+ble+i2c_toggleable");
    Serial.println("POTDIAG,BOOT,values_are_RAW_12bit_no_invert_no_filter");

    // ── MCP3208 SPI (hardened init matching main firmware) ──
    SPI.begin(MCP_SCK_PIN, MCP_MISO_PIN, MCP_MOSI_PIN, MCP_CS_PIN);
    delay(10);
    s_mcp.begin(MCP_CS_PIN);
    delay(10);
    for (uint8_t i = 0; i < NPOTS; i++) {
        (void)s_mcp.read(i);
        (void)s_mcp.read(i);
    }

    // ── NeoPixel (init, idle black until 'n') ──
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    delay(2);
    s_strip.begin();
    s_strip.clear();
    s_strip.show();

    // ── I2C bus (init, idle until 'i') ──
    Wire.begin(SDA_PIN, SCL_PIN, I2C_HZ);
    delay(50);

    // ── BLE NimBLE server (always advertising, notify only on 'b') ──
    NimBLEDevice::init("ILLPAD_POT_DIAG");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);   // Max TX power
    auto* server = NimBLEDevice::createServer();
    auto* svc = server->createService("12345678-1234-1234-1234-000000000001");
    s_bleChar = svc->createCharacteristic(
        "12345678-1234-1234-1234-000000000002",
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    svc->start();
    auto* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(svc->getUUID());
    adv->setMinInterval(32);                  // 20 ms (unit 0.625 ms)
    adv->setMaxInterval(48);                  // 30 ms
    adv->start();
    Serial.println("POTDIAG,BOOT,ble_advertising_as=ILLPAD_POT_DIAG_max_TX_power");

    // Seed RNG from micros + ADC
    randomSeed(micros() ^ s_mcp.read(0) ^ (s_mcp.read(1) << 12));

    printHelp();
    printLoad();
    Serial.println("POTDIAG,READY,press_ENTER_to_sample");
}

void loop() {
    while (Serial.available()) {
        int c = Serial.read();
        if (c == '\r' || c == '\n') {
            printLoad();
            sampleOnce();
        } else if (c == 'n' || c == 'N') {
            s_neoStress = !s_neoStress;
            if (!s_neoStress) { s_strip.clear(); s_strip.show(); }
            printLoad();
        } else if (c == 'b' || c == 'B') {
            s_bleStress = !s_bleStress;
            printLoad();
        } else if (c == 'i' || c == 'I') {
            s_i2cStress = !s_i2cStress;
            printLoad();
        } else if (c == 'a' || c == 'A') {
            s_neoStress = s_bleStress = s_i2cStress = true;
            printLoad();
        } else if (c == '0') {
            s_neoStress = s_bleStress = s_i2cStress = false;
            s_strip.clear();
            s_strip.show();
            printLoad();
        } else if (c == 'c' || c == 'C') {
            s_windowMode = !s_windowMode;
            Serial.printf("POTDIAG,MODE,windowed=%s,window_ms=%lu\n",
                          s_windowMode ? "ON" : "OFF",
                          (unsigned long)WINDOW_MS);
            if (s_windowMode) {
                printLoad();
                winReset(millis());
            }
        } else if (c == 'l' || c == 'L') {
            printLoad();
        } else if (c == 'h' || c == 'H') {
            printHelp();
            printLoad();
        }
    }
    uint32_t now = millis();
    neoTick(now);
    bleTick(now);
    i2cTick(now);
    if (s_windowMode) winTick(now);
}
