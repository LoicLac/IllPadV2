#include <Arduino.h>
#include <Wire.h>

// Core
#include "core/HardwareConfig.h"
#include "core/KeyboardData.h"
#include "core/CapacitiveKeyboard.h"
#include "core/MidiTransport.h"
#include "core/LedController.h"

// MIDI
#include "midi/MidiEngine.h"

// Managers
#include "managers/BankManager.h"
#include "managers/ScaleManager.h"

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
static LedController      s_leds;
static BankManager        s_bankManager;
static ScaleManager       s_scaleManager;

// 8 banks — all NORMAL for now
static BankSlot s_banks[NUM_BANKS];

// Pad ordering: sequential 0..47 (Tool 2 will customize later)
static uint8_t s_padOrder[NUM_KEYS];

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
  Serial.println("=== ILLPAD48 V2 — Scale + Bank Test ===");

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

  // LEDs
  s_leds.begin();
  Serial.println("[INIT] LEDs OK.");

  // Pad ordering: sequential (0,1,2,...47)
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    s_padOrder[i] = i;
  }

  // Init banks — all NORMAL, chromatic, root C, mode Ionian
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    s_banks[i].channel      = i;
    s_banks[i].type         = BANK_NORMAL;
    s_banks[i].scale        = {true, 2, 0};  // chromatic=true, root=C(2), mode=Ionian(0)
    s_banks[i].arpEngine    = nullptr;
    s_banks[i].isForeground = (i == DEFAULT_BANK);
    memset(s_banks[i].lastResolvedNote, 0xFF, NUM_KEYS);
  }

  // Bank Manager
  s_bankManager.begin(&s_midiEngine, &s_leds, s_banks, s_lastKeys);
  Serial.println("[INIT] BankManager OK.");

  // Scale Manager
  s_scaleManager.begin(s_banks, &s_midiEngine, s_lastKeys);
  Serial.println("[INIT] ScaleManager OK.");
  Serial.println("[INIT] Scale pads: root=8-14(A-G), mode=15-21, chromatic=22");

  // Buttons — active LOW, internal pull-up
  pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);

  // Clear state
  memset(s_buffers, 0, sizeof(s_buffers));
  memset(s_lastKeys, 0, sizeof(s_lastKeys));

  // Sensing task on Core 0
  xTaskCreatePinnedToCore(sensingTask, "sensing", 4096, nullptr, 1, nullptr, 0);
  s_bootTimestamp = millis();

  Serial.println("[INIT] Ready.");
  Serial.println("[INIT] LEFT btn + pad 0-7 = bank switch");
  Serial.println("[INIT] RIGHT btn + pad 8-14 = root, 15-21 = mode, 22 = chromatic");
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

  // --- Read buttons (active LOW) ---
  bool leftHeld  = (digitalRead(BTN_LEFT_PIN) == LOW);
  bool rightHeld = (digitalRead(BTN_RIGHT_PIN) == LOW);

  // --- Managers ---
  s_bankManager.update(state.keyIsPressed, leftHeld);
  s_scaleManager.update(state.keyIsPressed, rightHeld, s_bankManager.getCurrentSlot());

  // --- MIDI processing (skip when either button held) ---
  if (!s_bankManager.isHolding() && !s_scaleManager.isHolding()) {
    const ScaleConfig& scale = s_bankManager.getCurrentSlot().scale;

    for (int i = 0; i < NUM_KEYS; i++) {
      bool pressed    = state.keyIsPressed[i];
      bool wasPressed = s_lastKeys[i];

      if (pressed && !wasPressed) {
        s_midiEngine.noteOn(i, MIDI_NOTE_ON_VELOCITY, s_padOrder, scale);
      } else if (!pressed && wasPressed) {
        s_midiEngine.noteOff(i);
      }

      if (pressed) {
        s_midiEngine.updateAftertouch(i, state.pressure[i]);
      }

      s_lastKeys[i] = state.keyIsPressed[i];
    }
  }

  // Drain aftertouch queue + LED update + BLE housekeeping
  s_midiEngine.flush();
  s_leds.update();
  s_transport.update();

  vTaskDelay(1);
}
