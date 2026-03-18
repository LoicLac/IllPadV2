#include <Arduino.h>
#include <Wire.h>

// Core
#include "core/HardwareConfig.h"
#include "core/KeyboardData.h"
#include "core/CapacitiveKeyboard.h"
#include "core/MidiTransport.h"

// MIDI
#include "midi/MidiEngine.h"

// =================================================================
// Double Buffer (lock-free Core 0 → Core 1)
// =================================================================
static SharedKeyboardState s_buffers[2];
static volatile uint8_t s_writeIndex = 0;
static volatile uint8_t s_readIndex  = 1;

// Slow parameters (Core 1 writes, Core 0 reads)
static volatile float    s_responseShape = RESPONSE_SHAPE_DEFAULT;
static volatile uint16_t s_slewRate      = SLEW_RATE_DEFAULT;

// =================================================================
// Global Objects
// =================================================================
static CapacitiveKeyboard s_keyboard;
static MidiTransport      s_transport;
static MidiEngine         s_midiEngine;

// Edge detection: previous frame's key state (Core 1 only)
static uint8_t s_lastKeys[NUM_KEYS];

// Boot stabilization
static const uint32_t BOOT_SETTLE_MS = 300;
static uint32_t s_bootTimestamp = 0;

// =================================================================
// Core 0 — Sensing Task
// =================================================================
static void sensingTask(void* param) {
  (void)param;

  for (;;) {
    s_keyboard.setResponseShape(s_responseShape);
    s_keyboard.setSlewRate(s_slewRate);
    s_keyboard.update();

    SharedKeyboardState& buf = s_buffers[s_writeIndex];
    for (int i = 0; i < NUM_KEYS; i++) {
      buf.keyIsPressed[i] = s_keyboard.isPressed(i) ? 1 : 0;
      buf.pressure[i]     = s_keyboard.getPressure(i);
    }

    uint8_t tmp  = s_writeIndex;
    s_writeIndex = s_readIndex;
    s_readIndex  = tmp;

    vTaskDelay(1);
  }
}

// =================================================================
// Arduino setup() — runs on Core 1
// =================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== ILLPAD48 V2 — MIDI Test ===");

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN, I2C_CLOCK_HZ);
  Serial.println("[INIT] I2C OK.");

  // Keyboard
  bool kbOk = s_keyboard.begin();
  if (!kbOk) {
    Serial.println("[INIT] FATAL: Keyboard init failed!");
    for (;;) { delay(1000); }
  }
  Serial.println("[INIT] Keyboard OK.");

  // MIDI Transport (USB + BLE)
  s_transport.begin();
  Serial.println("[INIT] MIDI Transport OK.");

  // MIDI Engine
  s_midiEngine.begin(&s_transport);
  Serial.println("[INIT] MIDI Engine OK.");

  // Clear state
  memset(s_buffers, 0, sizeof(s_buffers));
  memset(s_lastKeys, 0, sizeof(s_lastKeys));

  // Sensing task on Core 0
  xTaskCreatePinnedToCore(sensingTask, "sensing", 4096, nullptr, 1, nullptr, 0);
  s_bootTimestamp = millis();

  Serial.println("[INIT] Ready. Touch pads → MIDI out (USB + BLE).");
  Serial.println();
}

// =================================================================
// Arduino loop() — Core 1
// =================================================================
void loop() {
  const SharedKeyboardState& state = s_buffers[s_readIndex];
  uint32_t now = millis();

  // Boot settle — absorb state silently
  if (now - s_bootTimestamp < BOOT_SETTLE_MS) {
    for (int i = 0; i < NUM_KEYS; i++) {
      s_lastKeys[i] = state.keyIsPressed[i];
    }
    vTaskDelay(1);
    return;
  }

  // Edge detection + MIDI
  for (int i = 0; i < NUM_KEYS; i++) {
    bool pressed    = state.keyIsPressed[i];
    bool wasPressed = s_lastKeys[i];

    if (pressed && !wasPressed) {
      // Note ON — fixed velocity 127, chromatic from C2
      s_midiEngine.noteOn(i, MIDI_NOTE_ON_VELOCITY);
    } else if (!pressed && wasPressed) {
      // Note OFF
      s_midiEngine.noteOff(i);
    }

    // Aftertouch for held pads
    if (pressed) {
      s_midiEngine.updateAftertouch(i, state.pressure[i]);
    }

    s_lastKeys[i] = state.keyIsPressed[i];
  }

  // Drain aftertouch queue + BLE housekeeping
  s_midiEngine.flush();
  s_transport.update();

  vTaskDelay(1);
}
