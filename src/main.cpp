#include <Arduino.h>
#include <Wire.h>
#include <atomic>

// Core
#include "core/HardwareConfig.h"
#include "core/KeyboardData.h"
#include "core/CapacitiveKeyboard.h"
#include "core/MidiTransport.h"
#include "core/LedController.h"

// MIDI
#include "midi/MidiEngine.h"

// Arp
#include "arp/ArpEngine.h"

// Managers
#include "managers/BankManager.h"
#include "managers/ScaleManager.h"
#include "managers/PotRouter.h"
#include "managers/NvsManager.h"

// Setup
#include "setup/SetupManager.h"

// =================================================================
// Double Buffer (lock-free Core 0 → Core 1)
// =================================================================
// Core 0 writes to s_buffers[w], then atomically publishes w via s_active.
// Core 1 reads s_active to know which buffer has the latest data.
// No mutex, no torn reads: a single std::atomic<uint8_t> is the sync point.
static SharedKeyboardState s_buffers[2];
static std::atomic<uint8_t> s_active{0};  // Index of the buffer Core 1 should READ

// Slow parameters (Core 1 writes, Core 0 reads) — std::atomic, NEVER volatile
static std::atomic<float>    s_responseShape{RESPONSE_SHAPE_DEFAULT};
static std::atomic<uint16_t> s_slewRate{SLEW_RATE_DEFAULT};
static std::atomic<uint8_t>  s_padSensitivity{PAD_SENSITIVITY_DEFAULT};

// =================================================================
// Global Objects
// =================================================================
static CapacitiveKeyboard s_keyboard;
static MidiTransport      s_transport;
static MidiEngine         s_midiEngine;
static LedController      s_leds;
static BankManager        s_bankManager;
static ScaleManager       s_scaleManager;
static PotRouter          s_potRouter;
static NvsManager         s_nvsManager;
static SetupManager       s_setupManager;

// 8 banks — all NORMAL for now
static BankSlot s_banks[NUM_BANKS];

// Static ArpEngine pool (max 4 ARPEG banks)
static ArpEngine s_arpEngines[4];

// Pad ordering: sequential 0..47 (Tool 2 will customize later)
static uint8_t s_padOrder[NUM_KEYS];

// Edge detection: previous frame's key state (Core 1 only)
static uint8_t s_lastKeys[NUM_KEYS];

// Play/Stop pad (ARPEG only, always active — not gated by button hold)
static uint8_t s_playStopPad = 24;  // Default, overwritten by NVS
static bool    s_lastPlayStopState = false;

// Boot stabilization
static const uint32_t BOOT_SETTLE_MS = 300;
static uint32_t s_bootTimestamp = 0;

// =================================================================
// Core 0 — Sensing Task
// =================================================================
static void sensingTask(void* param) {
  (void)param;

  for (;;) {
    s_keyboard.setResponseShape(s_responseShape.load(std::memory_order_relaxed));
    s_keyboard.setSlewRate(s_slewRate.load(std::memory_order_relaxed));
    s_keyboard.setPadSensitivity(s_padSensitivity.load(std::memory_order_relaxed));
    s_keyboard.update();

    // Write into the buffer Core 1 is NOT currently reading
    uint8_t writeIdx = 1 - s_active.load(std::memory_order_acquire);
    SharedKeyboardState& buf = s_buffers[writeIdx];
    for (int i = 0; i < NUM_KEYS; i++) {
      buf.keyIsPressed[i] = s_keyboard.isPressed(i) ? 1 : 0;
      buf.pressure[i]     = s_keyboard.getPressure(i);
    }

    // Atomically publish — Core 1 will see the new data on next loop()
    s_active.store(writeIdx, std::memory_order_release);

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

  // LEDs (needed early for setup chase animation)
  s_leds.begin();
  Serial.println("[INIT] LEDs OK.");

  // Buttons — active LOW, internal pull-up (needed early for setup detection)
  pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
  pinMode(BTN_REAR_PIN, INPUT_PULLUP);

  // Pad ordering: sequential (0,1,2,...47)
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    s_padOrder[i] = i;
  }

  // Init banks — all NORMAL, chromatic, root C, mode Ionian (defaults)
  uint8_t bankPads[NUM_BANKS];
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    s_banks[i].channel            = i;
    s_banks[i].type               = BANK_NORMAL;
    s_banks[i].scale              = {true, 2, 0};  // chromatic=true, root=C(2), mode=Ionian(0)
    s_banks[i].arpEngine          = nullptr;
    s_banks[i].isForeground       = false;
    s_banks[i].baseVelocity       = DEFAULT_BASE_VELOCITY;
    s_banks[i].velocityVariation  = DEFAULT_VELOCITY_VARIATION;
    s_banks[i].pitchBendOffset    = DEFAULT_PITCH_BEND_OFFSET;
    memset(s_banks[i].lastResolvedNote, 0xFF, NUM_KEYS);
    bankPads[i] = i;  // defaults
  }

  // Scale/arp pad defaults (declared here so setup mode can access them)
  uint8_t rootPads[7], modePads[7];
  for (uint8_t i = 0; i < 7; i++) { rootPads[i] = 8 + i; modePads[i] = 15 + i; }
  uint8_t chromaticPad = 22;
  uint8_t holdPad = 23;

  // =================================================================
  // Setup Mode Detection (hold rear button 3s at boot)
  // Must happen BEFORE sensing task starts (needs direct keyboard access)
  // =================================================================
  {
    Serial.println("[INIT] Hold rear button to enter setup mode...");
    uint32_t windowStart = millis();
    bool setupRequested = false;

    // Wait for button press within CAL_WAIT_WINDOW_MS
    while (millis() - windowStart < CAL_WAIT_WINDOW_MS) {
      if (digitalRead(BTN_REAR_PIN) == LOW) {
        // Button pressed — now wait for CAL_HOLD_DURATION_MS hold
        uint32_t holdStart = millis();
        s_leds.startChase();  // Chase LED animation while holding
        bool held = true;

        while (millis() - holdStart < CAL_HOLD_DURATION_MS) {
          s_leds.update();
          if (digitalRead(BTN_REAR_PIN) != LOW) {
            held = false;
            break;
          }
          delay(10);
        }

        s_leds.stopChase();

        if (held) {
          setupRequested = true;
        }
        break;
      }
      delay(10);
    }

    if (setupRequested) {
      Serial.println("[SETUP] Entering setup mode...");
      s_setupManager.begin(&s_keyboard, &s_leds, &s_nvsManager,
                           s_banks, s_padOrder, bankPads,
                           rootPads, modePads, chromaticPad,
                           holdPad, s_playStopPad);
      s_setupManager.run();

      // Reload calibration data after setup (may have been changed by Tool 1)
      s_keyboard.loadCalibrationData();
      s_keyboard.calculateAdaptiveThresholds();
      Serial.println("[SETUP] Exited setup mode. Continuing boot...");
    }
  }

  // MIDI Transport (USB + BLE) — after setup, before normal boot
  s_transport.begin();
  Serial.println("[INIT] MIDI Transport OK.");

  // MIDI Engine
  s_midiEngine.begin(&s_transport);
  Serial.println("[INIT] MIDI Engine OK.");

  // NVS — load all persisted data (overwrites defaults where saved)
  uint8_t currentBank = DEFAULT_BANK;
  // bankPads[], rootPads[], modePads[], chromaticPad, holdPad initialized above (before setup check)

  SettingsStore s_settings;
  s_nvsManager.loadAll(s_banks, currentBank, s_padOrder, bankPads,
                        rootPads, modePads, chromaticPad, holdPad,
                        s_playStopPad, s_potRouter, s_settings);

  // Apply loaded bank
  s_banks[currentBank].isForeground = true;
  Serial.printf("[INIT] NVS loaded. Bank=%d\n", currentBank);

  // Apply loaded settings
  s_keyboard.setBaselineProfile(s_settings.baselineProfile);
  s_midiEngine.setAftertouchRate(s_settings.aftertouchRate);
  s_transport.setBleInterval(s_settings.bleInterval);

  // Assign ArpEngines to ARPEG banks (after NVS load)
  {
    uint8_t arpIdx = 0;
    for (uint8_t i = 0; i < NUM_BANKS && arpIdx < 4; i++) {
      if (s_banks[i].type == BANK_ARPEG) {
        s_arpEngines[arpIdx].setChannel(i);
        s_banks[i].arpEngine = &s_arpEngines[arpIdx];
        arpIdx++;
        #if !PRODUCTION_MODE
        Serial.printf("[INIT] Bank %d: ARPEG, ArpEngine assigned\n", i + 1);
        #endif
      }
    }
    #if !PRODUCTION_MODE
    if (arpIdx == 0) {
      Serial.println("[INIT] No ARPEG banks configured.");
    }
    #endif
  }

  // Bank Manager
  s_bankManager.begin(&s_midiEngine, &s_leds, s_banks, s_lastKeys);
  s_bankManager.setBankPads(bankPads);
  s_bankManager.setCurrentBank(currentBank);
  Serial.println("[INIT] BankManager OK.");

  // Scale Manager
  s_scaleManager.begin(s_banks, &s_midiEngine, s_lastKeys);
  s_scaleManager.setRootPads(rootPads);
  s_scaleManager.setModePads(modePads);
  s_scaleManager.setChromaticPad(chromaticPad);
  s_scaleManager.setHoldPad(holdPad);
  Serial.println("[INIT] ScaleManager OK.");

  // Pot Router
  s_potRouter.begin();
  Serial.println("[INIT] PotRouter OK (5 pots, 17 bindings).");

  // NVS Manager — start task (after all loading done)
  s_nvsManager.begin();
  Serial.println("[INIT] NvsManager OK.");

  // Clear state
  memset(s_buffers, 0, sizeof(s_buffers));
  memset(s_lastKeys, 0, sizeof(s_lastKeys));

  // Sensing task on Core 0
  xTaskCreatePinnedToCore(sensingTask, "sensing", 4096, nullptr, 1, nullptr, 0);
  s_bootTimestamp = millis();

  Serial.println("[INIT] Ready.");
  Serial.println("[INIT] LEFT btn + pad 0-7 = bank switch");
  Serial.println("[INIT] LEFT btn + pad 8-14 = root, 15-21 = mode, 22 = chromatic (single-layer)");
  Serial.println();
}

// =================================================================
// Arduino loop() — Core 1
// =================================================================
void loop() {
  const SharedKeyboardState& state = s_buffers[s_active.load(std::memory_order_acquire)];
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
  bool leftHeld = (digitalRead(BTN_LEFT_PIN) == LOW);
  bool rearHeld = (digitalRead(BTN_REAR_PIN) == LOW);

  // --- Managers (both use left button — single-layer control) ---
  bool bankSwitched = s_bankManager.update(state.keyIsPressed, leftHeld);
  s_scaleManager.update(state.keyIsPressed, leftHeld, s_bankManager.getCurrentSlot());

  // Queue NVS saves on bank switch
  if (bankSwitched) {
    s_nvsManager.queueBankWrite(s_bankManager.getCurrentBank());
    s_potRouter.resetPerBankCatch();
  }

  // Queue NVS save on scale change
  if (s_scaleManager.hasScaleChanged()) {
    uint8_t bank = s_bankManager.getCurrentBank();
    s_nvsManager.queueScaleWrite(bank, s_bankManager.getCurrentSlot().scale);
  }

  // --- Play/Stop pad (ARPEG only, always active — not gated by button hold) ---
  {
    BankSlot& psSlot = s_bankManager.getCurrentSlot();
    if (s_playStopPad < NUM_KEYS && psSlot.type == BANK_ARPEG && psSlot.arpEngine) {
      bool psPressed = state.keyIsPressed[s_playStopPad];
      if (psPressed && !s_lastPlayStopState) {
        psSlot.arpEngine->playStop();
        #if DEBUG_SERIAL
        Serial.printf("[ARP] Play/Stop toggled (bank %d)\n", s_bankManager.getCurrentBank() + 1);
        #endif
      }
      s_lastPlayStopState = psPressed;
    } else {
      // Not ARPEG: reset edge detection so it triggers correctly when switching back
      s_lastPlayStopState = (s_playStopPad < NUM_KEYS) ? state.keyIsPressed[s_playStopPad] : false;
    }
  }

  // --- MIDI processing (skip when button held — single-layer control) ---
  if (!s_bankManager.isHolding() && !s_scaleManager.isHolding()) {
    BankSlot& slot = s_bankManager.getCurrentSlot();
    const ScaleConfig& scale = slot.scale;

    for (int i = 0; i < NUM_KEYS; i++) {
      // Skip play/stop pad on ARPEG banks (not a music pad)
      if (i == s_playStopPad && slot.type == BANK_ARPEG) {
        s_lastKeys[i] = state.keyIsPressed[i];
        continue;
      }

      bool pressed    = state.keyIsPressed[i];
      bool wasPressed = s_lastKeys[i];

      if (pressed && !wasPressed) {
        // Per-bank velocity with random variation
        uint8_t vel = slot.baseVelocity;
        if (slot.velocityVariation > 0) {
          int16_t range = (int16_t)slot.velocityVariation * 127 / 200;  // ±range
          int16_t offset = (int16_t)(random(-range, range + 1));
          int16_t result = (int16_t)vel + offset;
          vel = (uint8_t)constrain(result, 1, 127);
        }
        s_midiEngine.noteOn(i, vel, s_padOrder, scale);
      } else if (!pressed && wasPressed) {
        s_midiEngine.noteOff(i);
      }

      if (pressed) {
        // Apply AT deadzone: suppress aftertouch below threshold
        uint8_t p = state.pressure[i];
        uint8_t dz = (uint8_t)s_potRouter.getAtDeadzone();
        if (p > dz) {
          uint8_t range = 255 - dz;
          uint8_t scaled = (range > 0)
            ? (uint8_t)((uint16_t)(p - dz) * 127 / range)
            : p;
          s_midiEngine.updateAftertouch(i, scaled);
        } else {
          s_midiEngine.updateAftertouch(i, 0);
        }
      }

      s_lastKeys[i] = state.keyIsPressed[i];
    }
  }

  // --- CRITICAL PATH END ---
  s_midiEngine.flush();

  // --- SECONDARY: Pots ---
  BankSlot& potSlot = s_bankManager.getCurrentSlot();
  s_potRouter.update(leftHeld, rearHeld, potSlot.type);

  // --- Musical params: push live to BankSlot ---
  potSlot.baseVelocity      = s_potRouter.getBaseVelocity();
  potSlot.velocityVariation  = s_potRouter.getVelocityVariation();
  potSlot.pitchBendOffset    = s_potRouter.getPitchBend();
  // Send pitch bend to MIDI output immediately (live feel)
  s_midiEngine.sendPitchBend(potSlot.pitchBendOffset);

  // --- Musical params: push live to ArpEngine (if ARPEG bank) ---
  if (potSlot.type == BANK_ARPEG && potSlot.arpEngine) {
    potSlot.arpEngine->setGateLength(s_potRouter.getGateLength());
    potSlot.arpEngine->setSwing(s_potRouter.getSwing());
    potSlot.arpEngine->setDivision(s_potRouter.getDivision());
    potSlot.arpEngine->setPattern(s_potRouter.getPattern());
    potSlot.arpEngine->setOctaveRange(s_potRouter.getOctaveRange());
    potSlot.arpEngine->setBaseVelocity(s_potRouter.getBaseVelocity());
    potSlot.arpEngine->setVelocityVariation(s_potRouter.getVelocityVariation());
  }

  // --- Global params: Core 0 atomics ---
  s_responseShape.store(s_potRouter.getResponseShape(), std::memory_order_relaxed);
  s_slewRate.store(s_potRouter.getSlewRate(), std::memory_order_relaxed);
  s_padSensitivity.store(s_potRouter.getPadSensitivity(), std::memory_order_relaxed);

  // --- Non-musical params ---
  s_leds.setBrightness(s_potRouter.getLedBrightness());

  // LED bargraph from pot movement
  if (s_potRouter.hasBargraphUpdate()) {
    s_leds.showPotBargraph(s_potRouter.getBargraphLevel());
  }

  s_leds.update();
  s_transport.update();

  // --- NVS: pot debounce (10s after last change) + signal task ---
  bool potDirty = s_potRouter.isDirty();
  s_nvsManager.tickPotDebounce(now, potDirty, s_potRouter,
                                s_bankManager.getCurrentBank(), potSlot.type);
  if (potDirty) s_potRouter.clearDirty();

  // Check if any pad is pressed (NVS won't write during play)
  bool anyPressed = false;
  for (int i = 0; i < NUM_KEYS; i++) {
    if (state.keyIsPressed[i]) { anyPressed = true; break; }
  }
  s_nvsManager.setAnyPadPressed(anyPressed);
  s_nvsManager.notifyIfDirty();

  vTaskDelay(1);
}
