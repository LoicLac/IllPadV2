// ═══════════════════════════════════════════════
// DEBUG ADC — Raw vs oversampled noise comparison
// Standalone sketch, does not use PotFilter.
// ═══════════════════════════════════════════════
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>

static bool bleActive = false;
static BLEServer* pServer = nullptr;

static void toggleBLE() {
    if (!bleActive) {
        if (!pServer) {
            BLEDevice::init("ILLPAD-ADC-TEST");
            pServer = BLEDevice::createServer();
            // Start advertising to simulate real BLE MIDI load
            BLEAdvertising* adv = BLEDevice::getAdvertising();
            adv->start();
        } else {
            BLEDevice::getAdvertising()->start();
        }
        bleActive = true;
    } else {
        BLEDevice::getAdvertising()->stop();
        bleActive = false;
    }
}

// --- Pot definitions (from HardwareConfig.h) ---
struct PotDef {
    const char* name;
    uint8_t     gpio;
};

static const PotDef POTS[] = {
    { "RIGHT 1", 4 },   // ADC1_CH3
    { "RIGHT 2", 5 },   // ADC1_CH4
    { "RIGHT 3", 6 },   // ADC1_CH5
    { "RIGHT 4", 7 },   // ADC1_CH6
    { "REAR   ", 1 },   // ADC1_CH0
};
static const uint8_t NUM_POTS = sizeof(POTS) / sizeof(POTS[0]);

// --- Oversampling modes ---
// Each mode: number of reads = 2^shift
static const uint8_t MODES[] = { 0, 2, 4 };  // 1×, 4×, 16×
static const char* MODE_LABELS[] = { "1x", "4x", "16x" };
static const uint8_t NUM_MODES = 3;
static uint8_t currentMode = 0;  // start at 1×

// --- State per pot ---
static uint16_t val[NUM_POTS];
static uint16_t baseline[NUM_POTS];
static uint16_t minVal[NUM_POTS];
static uint16_t maxVal[NUM_POTS];

static uint16_t oversampleRead(uint8_t pin, uint8_t shift) {
    if (shift == 0) return analogRead(pin);
    const uint8_t count = 1 << shift;
    uint32_t sum = 0;
    for (uint8_t j = 0; j < count; j++) {
        sum += analogRead(pin);
    }
    return (uint16_t)(sum >> shift);
}

static void resetStats() {
    for (uint8_t i = 0; i < NUM_POTS; i++) {
        baseline[i] = val[i];
        minVal[i]   = val[i];
        maxVal[i]   = val[i];
    }
}

static void readAll() {
    uint8_t shift = MODES[currentMode];
    for (uint8_t i = 0; i < NUM_POTS; i++) {
        val[i] = oversampleRead(POTS[i].gpio, shift);
        if (val[i] < minVal[i]) minVal[i] = val[i];
        if (val[i] > maxVal[i]) maxVal[i] = val[i];
    }
}

static const char* noiseColor(uint16_t noise) {
    if (noise <= 10) return "\033[32m";       // green
    else if (noise <= 30) return "\033[33m";   // yellow
    return "\033[31m";                         // red
}

static void drawTable() {
    Serial.print("\033[H");  // cursor home

    Serial.println("\033[1;33m═══════════════════════════════════════════════════════════════════\033[0m");
    Serial.println("\033[1;37m  DEBUG ADC — Noise analysis     [r] reset [m] mode [b] BLE toggle\033[0m");
    Serial.println("\033[1;33m═══════════════════════════════════════════════════════════════════\033[0m");
    Serial.println();

    // Show current mode with highlight
    Serial.print("  Oversampling: ");
    for (uint8_t m = 0; m < NUM_MODES; m++) {
        if (m == currentMode) {
            Serial.printf("\033[1;7m %s \033[0m  ", MODE_LABELS[m]);
        } else {
            Serial.printf("\033[90m %s \033[0m  ", MODE_LABELS[m]);
        }
    }
    uint8_t reads = 1 << MODES[currentMode];
    Serial.printf("   (%d reads, ~%dµs/pot)", reads, reads * 10);
    Serial.println("\033[K");

    // BLE status line
    Serial.printf("  BLE: %s\033[K\n",
        bleActive ? "\033[1;31mON (advertising)\033[0m" : "\033[32mOFF\033[0m");
    Serial.println();

    Serial.println("\033[1;36m  POT        GPIO    VALUE    Δbase     MIN     MAX   NOISE \033[0m");
    Serial.println("\033[36m  ────────────────────────────────────────────────────────── \033[0m");

    for (uint8_t i = 0; i < NUM_POTS; i++) {
        int16_t delta = (int16_t)val[i] - (int16_t)baseline[i];
        uint16_t noise = maxVal[i] - minVal[i];

        const char* dCol = noiseColor((delta < 0) ? -delta : delta);
        const char* nCol = noiseColor(noise);

        Serial.printf("  %s   GPIO%-2d   %4d   %s%+5d\033[0m    %4d    %4d   %s%4d\033[0m\n",
                      POTS[i].name,
                      POTS[i].gpio,
                      val[i],
                      dCol, delta,
                      minVal[i], maxVal[i],
                      nCol, noise);
    }

    Serial.println("\033[36m  ────────────────────────────────────────────────────────── \033[0m");
    Serial.println();
    Serial.println("  \033[90mNOISE = MAX-MIN since last [r]. Green ≤10, Yellow ≤30, Red >30.\033[0m");
    Serial.println("  \033[90mPress [m] to switch 1x/4x/16x, [b] to toggle BLE on/off.\033[0m");
    Serial.println("  \033[90mPress [r] to reset, then don't touch any pot — watch NOISE grow.\033[0m");
    Serial.println("  \033[90mCompare NOISE with BLE OFF vs ON to measure radio impact.\033[0m");
    Serial.print("\033[J");  // clear below
}

void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(10); }

    for (uint8_t i = 0; i < NUM_POTS; i++) {
        pinMode(POTS[i].gpio, INPUT);
    }

    Serial.print("\033[2J");  // clear screen

    readAll();
    resetStats();
}

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'r' || c == 'R') {
            readAll();
            resetStats();
        } else if (c == 'm' || c == 'M') {
            currentMode = (currentMode + 1) % NUM_MODES;
            readAll();
            resetStats();
        } else if (c == 'b' || c == 'B') {
            toggleBLE();
            readAll();
            resetStats();
        }
    }

    readAll();
    drawTable();
    delay(50);  // ~20 Hz refresh
}
