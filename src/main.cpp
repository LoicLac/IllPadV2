#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <atomic>

// Core
#include "core/HardwareConfig.h"
#include "core/KeyboardData.h"
#include "core/CapacitiveKeyboard.h"
#include "core/MidiTransport.h"
#include "core/LedController.h"

// MIDI
#include "midi/MidiEngine.h"
#include "midi/ClockManager.h"

// Arp
#include "arp/ArpEngine.h"
#include "arp/ArpScheduler.h"

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
static ClockManager       s_clockManager;
static ArpScheduler       s_arpScheduler;

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

// Double-tap detection (ARPEG HOLD mode)
static uint32_t s_lastPressTime[NUM_KEYS];
static uint8_t  s_doubleTapMs = DOUBLE_TAP_MS_DEFAULT;

// Hold pad (ARPEG)
static uint8_t s_holdPad = 23;  // Default, overwritten by NVS

// Panic: BLE reconnect detection + settings
static bool    s_lastBleConnected = false;
static bool    s_panicOnReconnect = DEFAULT_PANIC_ON_RECONNECT;

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
// Panic — brute force silence on all channels
// =================================================================
// Flushes all arp engines (pending events + refcounts) then sends
// CC 123 (All Notes Off) on all 8 channels. Guarantees no stuck notes
// in the DAW regardless of internal tracking state.

static void midiPanic() {
  // Phase 1: flush all arp engines (pending events + refcounts)
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (s_banks[i].type == BANK_ARPEG && s_banks[i].arpEngine) {
      s_banks[i].arpEngine->flushPendingNoteOffs(s_transport);
    }
  }
  // Phase 2: clear MidiEngine tracked notes (NORMAL mode)
  s_midiEngine.allNotesOff();
  // Phase 3: CC 123 on all 8 channels (catches anything we missed)
  for (uint8_t ch = 0; ch < NUM_BANKS; ch++) {
    s_transport.sendAllNotesOff(ch);
  }
  #if DEBUG_SERIAL
  Serial.println("[PANIC] All notes off on all channels");
  #endif
}

// =================================================================
// Arduino setup() — runs on Core 1
// =================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== ILLPAD48 V2 ===");

  // LEDs first — needed for boot progress and error feedback
  s_leds.begin();
  s_leds.showBootProgress(1);  // Step 1: LED hardware ready

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN, I2C_CLOCK_HZ);
  s_leds.showBootProgress(2);  // Step 2: I2C bus ready
  #if DEBUG_SERIAL
  Serial.println("[INIT] I2C OK.");
  #endif

  // Keyboard (4× MPR121) — can fail if I2C bus or sensors are broken
  bool kbOk = s_keyboard.begin();
  if (!kbOk) {
    Serial.println("[INIT] FATAL: Keyboard init failed!");
    s_leds.showBootFailure(3);  // Step 3 blinks = keyboard failed
    for (;;) { s_leds.update(); delay(10); }
  }
  s_leds.showBootProgress(3);  // Step 3: keyboard OK
  #if DEBUG_SERIAL
  Serial.println("[INIT] Keyboard OK.");
  #endif

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
    bankPads[i] = i;  // defaults
  }

  // Scale/arp pad defaults (declared here so setup mode can access them)
  uint8_t rootPads[7], modePads[7];
  for (uint8_t i = 0; i < 7; i++) { rootPads[i] = 8 + i; modePads[i] = 15 + i; }
  uint8_t chromaticPad = 22;
  uint8_t holdPad = 23;
  uint8_t octavePads[4] = {25, 26, 27, 28};

  // =================================================================
  // Setup Mode Detection (hold rear button 3s at boot)
  // Must happen BEFORE sensing task starts (needs direct keyboard access)
  // =================================================================
  {
    #if DEBUG_SERIAL
    Serial.println("[INIT] Hold rear button to enter setup mode...");
    #endif
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
      #if DEBUG_SERIAL
      Serial.println("[SETUP] Entering setup mode...");
      #endif
      s_setupManager.begin(&s_keyboard, &s_leds, &s_nvsManager,
                           s_banks, s_padOrder, bankPads,
                           rootPads, modePads, chromaticPad,
                           holdPad, s_playStopPad, octavePads,
                           &s_potRouter);
      s_setupManager.run();

      // Reload calibration data after setup (may have been changed by Tool 1)
      s_keyboard.loadCalibrationData();
      s_keyboard.calculateAdaptiveThresholds();
      #if DEBUG_SERIAL
      Serial.println("[SETUP] Exited setup mode. Continuing boot...");
      #endif
    }
  }

  // MIDI Transport (USB + BLE) — after setup, before normal boot
  // Load BLE interval early (before begin) so BLE_OFF can skip BLE init entirely
  {
    Preferences prefs;
    if (prefs.begin(SETTINGS_NVS_NAMESPACE, true)) {
      size_t len = prefs.getBytesLength(SETTINGS_NVS_KEY);
      if (len == sizeof(SettingsStore)) {
        SettingsStore tmp;
        prefs.getBytes(SETTINGS_NVS_KEY, &tmp, sizeof(SettingsStore));
        if (tmp.magic == EEPROM_MAGIC && tmp.version == SETTINGS_VERSION) {
          s_transport.setBleInterval(tmp.bleInterval);
        }
      }
      prefs.end();
    }
  }
  s_leds.showBootProgress(4);  // Step 4: starting MIDI
  s_transport.begin();
  #if DEBUG_SERIAL
  Serial.println("[INIT] MIDI Transport OK.");
  #endif

  // Clock Manager — wire MIDI clock reception callbacks
  s_clockManager.begin(&s_transport);
  s_transport.setClockCallback([](uint8_t src) { s_clockManager.onMidiClockTick(src); });
  s_transport.setTransportCallback([](uint8_t status, uint8_t src) {
    if (status == 0xFA) s_clockManager.onMidiStart();
    if (status == 0xFB) s_clockManager.onMidiContinue();
    if (status == 0xFC) s_clockManager.onMidiStop();
  });
  #if DEBUG_SERIAL
  Serial.println("[INIT] ClockManager OK.");
  #endif

  // MIDI Engine
  s_midiEngine.begin(&s_transport);
  #if DEBUG_SERIAL
  Serial.println("[INIT] MIDI Engine OK.");
  #endif

  // NVS — load all persisted data (overwrites defaults where saved)
  uint8_t currentBank = DEFAULT_BANK;
  // bankPads[], rootPads[], modePads[], chromaticPad, holdPad initialized above (before setup check)

  SettingsStore s_settings;
  s_nvsManager.loadAll(s_banks, currentBank, s_padOrder, bankPads,
                        rootPads, modePads, chromaticPad, holdPad,
                        s_playStopPad, octavePads, s_potRouter, s_settings);

  // Apply loaded bank
  s_banks[currentBank].isForeground = true;
  s_leds.showBootProgress(5);  // Step 5: NVS loaded
  #if DEBUG_SERIAL
  Serial.printf("[INIT] NVS loaded. Bank=%d\n", currentBank);
  #endif

  // Apply loaded settings
  s_keyboard.setBaselineProfile(s_settings.baselineProfile);
  s_midiEngine.setAftertouchRate(s_settings.aftertouchRate);
  s_transport.setBleInterval(s_settings.bleInterval);
  s_clockManager.setMasterMode(s_settings.clockMode == CLOCK_MASTER);
  s_clockManager.setFollowTransport(s_settings.followTransport != 0);
  s_doubleTapMs = s_settings.doubleTapMs;
  s_leds.setPotBarDuration(s_settings.potBarDurationMs);
  s_panicOnReconnect = (s_settings.panicOnReconnect != 0);

  // Apply loaded tempo to ClockManager (loadAll set it on PotRouter,
  // but ClockManager was initialized before loadAll with default 120)
  s_clockManager.setInternalBPM(s_potRouter.getTempoBPM());

  // Assign ArpEngines to ARPEG banks (after NVS load)
  {
    uint8_t arpIdx = 0;
    for (uint8_t i = 0; i < NUM_BANKS && arpIdx < 4; i++) {
      if (s_banks[i].type == BANK_ARPEG) {
        s_arpEngines[arpIdx].setChannel(i);
        s_banks[i].arpEngine = &s_arpEngines[arpIdx];
        arpIdx++;
        #if DEBUG_SERIAL
        Serial.printf("[INIT] Bank %d: ARPEG, ArpEngine assigned\n", i + 1);
        #endif
      }
    }
    #if DEBUG_SERIAL
    if (arpIdx == 0) {
      Serial.println("[INIT] No ARPEG banks configured.");
    }
    #endif
  }

  // ArpScheduler — register engines + set scale/padOrder context
  s_arpScheduler.begin(&s_transport, &s_clockManager);
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (s_banks[i].type == BANK_ARPEG && s_banks[i].arpEngine) {
      s_arpScheduler.registerArp(i, s_banks[i].arpEngine);
      s_banks[i].arpEngine->setScaleConfig(s_banks[i].scale);
      s_banks[i].arpEngine->setPadOrder(s_padOrder);

      // Apply NVS-loaded arp params to each engine
      const ArpPotStore& arp = s_nvsManager.getLoadedArpParams(i);
      s_banks[i].arpEngine->setGateLength((float)arp.gateRaw / 4095.0f);
      s_banks[i].arpEngine->setShuffleDepth((float)arp.shuffleDepthRaw / 4095.0f);
      s_banks[i].arpEngine->setDivision((ArpDivision)arp.division);
      s_banks[i].arpEngine->setPattern((ArpPattern)arp.pattern);
      s_banks[i].arpEngine->setOctaveRange(arp.octaveRange);
      s_banks[i].arpEngine->setShuffleTemplate(arp.shuffleTemplate);
      s_banks[i].arpEngine->setBaseVelocity(s_banks[i].baseVelocity);
      s_banks[i].arpEngine->setVelocityVariation(s_banks[i].velocityVariation);
      s_banks[i].arpEngine->setStartMode(s_nvsManager.getLoadedQuantizeMode(i));
    }
  }
  s_leds.showBootProgress(6);  // Step 6: Arp system ready
  #if DEBUG_SERIAL
  Serial.println("[INIT] ArpScheduler OK.");
  #endif

  // Give LedController access to bank states for multi-bank display
  s_leds.setBankSlots(s_banks);
  s_leds.setClockManager(&s_clockManager);

  // Store hold pad for ARPEG toggle detection in loop
  s_holdPad = holdPad;

  // Bank Manager
  s_bankManager.begin(&s_midiEngine, &s_leds, s_banks, s_lastKeys);
  s_bankManager.setBankPads(bankPads);
  s_bankManager.setCurrentBank(currentBank);
  #if DEBUG_SERIAL
  Serial.println("[INIT] BankManager OK.");
  #endif

  // Scale Manager
  s_scaleManager.begin(s_banks, &s_midiEngine, s_lastKeys);
  s_scaleManager.setRootPads(rootPads);
  s_scaleManager.setModePads(modePads);
  s_scaleManager.setChromaticPad(chromaticPad);
  s_scaleManager.setHoldPad(holdPad);
  s_scaleManager.setOctavePads(octavePads);
  #if DEBUG_SERIAL
  Serial.println("[INIT] ScaleManager OK.");
  #endif

  // Pot Router
  s_potRouter.begin();
  s_leds.showBootProgress(7);  // Step 7: managers ready
  #if DEBUG_SERIAL
  Serial.println("[INIT] PotRouter OK.");
  #endif

  // NVS Manager — start task (after all loading done)
  s_nvsManager.begin();
  #if DEBUG_SERIAL
  Serial.println("[INIT] NvsManager OK.");
  #endif

  // Clear state
  memset(s_buffers, 0, sizeof(s_buffers));
  memset(s_lastKeys, 0, sizeof(s_lastKeys));
  memset(s_lastPressTime, 0, sizeof(s_lastPressTime));

  // Sensing task on Core 0
  xTaskCreatePinnedToCore(sensingTask, "sensing", 4096, nullptr, 1, nullptr, 0);
  s_bootTimestamp = millis();

  s_leds.showBootProgress(8);  // Step 8: all systems go (full bar)
  delay(200);                   // Brief full-bar display before switching to bank mode
  s_leds.endBoot();             // Exit boot mode, switch to normal bank display

  #if DEBUG_SERIAL
  Serial.println("[INIT] Ready.");
  #endif
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

  // --- Poll USB MIDI (clock ticks arrive here) ---
  s_transport.update();

  // --- Read buttons (active LOW) ---
  bool leftHeld = (digitalRead(BTN_LEFT_PIN) == LOW);
  bool rearHeld = (digitalRead(BTN_REAR_PIN) == LOW);

  // --- Managers (both use left button — single-layer control) ---
  bool bankSwitched = s_bankManager.update(state.keyIsPressed, leftHeld);
  s_scaleManager.update(state.keyIsPressed, leftHeld, s_bankManager.getCurrentSlot());

  // On bank switch: reload per-bank pot values from the new bank, then reset catch.
  // Without this, PotRouter getters return the PREVIOUS bank's values, which get
  // pushed to the new bank every frame until the pot physically crosses catch.
  if (bankSwitched) {
    s_nvsManager.queueBankWrite(s_bankManager.getCurrentBank());
    BankSlot& newSlot = s_bankManager.getCurrentSlot();

    // Read per-bank values from new bank (BankSlot + ArpEngine)
    float gate = 0.5f, shufDepth = 0.0f;
    ArpDivision div = DIV_1_8;
    ArpPattern pat = ARP_UP;
    uint8_t shufTmpl = 0;
    if (newSlot.type == BANK_ARPEG && newSlot.arpEngine) {
      gate      = newSlot.arpEngine->getGateLength();
      shufDepth = newSlot.arpEngine->getShuffleDepth();
      div       = newSlot.arpEngine->getDivision();
      pat       = newSlot.arpEngine->getPattern();
      shufTmpl  = newSlot.arpEngine->getShuffleTemplate();
    }

    s_potRouter.loadStoredPerBank(
      newSlot.baseVelocity, newSlot.velocityVariation, newSlot.pitchBendOffset,
      gate, shufDepth, div, pat, shufTmpl
    );
    s_potRouter.resetPerBankCatch();
  }

  // Consume all ScaleManager change flags once (auto-clearing: can't read twice)
  ScaleChangeType scaleChange = s_scaleManager.consumeScaleChange();
  bool scaleChanged = (scaleChange != SCALE_CHANGE_NONE);
  bool octaveChanged = s_scaleManager.hasOctaveChanged();
  bool holdToggled   = s_scaleManager.hasHoldToggled();

  // Queue NVS save on scale change + sync arp engine scale + LED confirmation
  if (scaleChanged) {
    uint8_t bank = s_bankManager.getCurrentBank();
    BankSlot& scSlot = s_bankManager.getCurrentSlot();
    s_nvsManager.queueScaleWrite(bank, scSlot.scale);
    if (scSlot.type == BANK_ARPEG && scSlot.arpEngine) {
      // Flush old notes before scale change — prevents stuck notes when
      // a pile position resolves to a different MIDI note in the new scale
      scSlot.arpEngine->flushPendingNoteOffs(s_transport);
      scSlot.arpEngine->setScaleConfig(scSlot.scale);
    }
    switch (scaleChange) {
      case SCALE_CHANGE_ROOT:      s_leds.triggerConfirm(CONFIRM_SCALE_ROOT); break;
      case SCALE_CHANGE_MODE:      s_leds.triggerConfirm(CONFIRM_SCALE_MODE); break;
      case SCALE_CHANGE_CHROMATIC: s_leds.triggerConfirm(CONFIRM_SCALE_CHROM); break;
      default: break;
    }
  }

  // Queue NVS save on octave change + LED confirmation
  if (octaveChanged) {
    uint8_t bank = s_bankManager.getCurrentBank();
    uint8_t newOct = s_scaleManager.getNewOctaveRange();
    s_nvsManager.queueArpOctaveWrite(bank, newOct);
    s_leds.triggerConfirm(CONFIRM_OCTAVE, newOct);
  }

  // LED confirmation on hold toggle
  if (holdToggled) {
    BankSlot& holdSlot = s_bankManager.getCurrentSlot();
    bool holdIsOn = (holdSlot.type == BANK_ARPEG && holdSlot.arpEngine && holdSlot.arpEngine->isHoldOn());
    s_leds.triggerConfirm(holdIsOn ? CONFIRM_HOLD_ON : CONFIRM_HOLD_OFF);
  }

  // --- Clock: process ticks (PLL + tick generation) ---
  s_clockManager.update();

  // --- DAW transport: flush all arps on MIDI Stop (slave + followTransport) ---
  if (s_clockManager.consumeStopReceived()) {
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (s_banks[i].type == BANK_ARPEG && s_banks[i].arpEngine
          && s_banks[i].arpEngine->isPlaying()) {
        s_banks[i].arpEngine->flushPendingNoteOffs(s_transport);
      }
    }
  }
  // Start: tick counter already reset by ClockManager.
  // Must also reset ArpScheduler sync (prevents unsigned wrap burst)
  // and restart playing engines from bar 1.
  if (s_clockManager.consumeStartReceived()) {
    // Flush old events FIRST — prevents stale noteOn/noteOff from firing
    // after restart. Then reset step index for bar 1 sync.
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (s_banks[i].type == BANK_ARPEG && s_banks[i].arpEngine
          && s_banks[i].arpEngine->isPlaying()) {
        s_banks[i].arpEngine->flushPendingNoteOffs(s_transport);
        s_banks[i].arpEngine->resetStepIndex();
      }
    }
    s_arpScheduler.resetSync();
  }

  // --- Play/Stop pad (ARPEG + HOLD ON only — in HOLD OFF this pad plays a note) ---
  {
    BankSlot& psSlot = s_bankManager.getCurrentSlot();
    if (s_playStopPad < NUM_KEYS && psSlot.type == BANK_ARPEG
        && psSlot.arpEngine && psSlot.arpEngine->isHoldOn()) {
      bool psPressed = state.keyIsPressed[s_playStopPad];
      if (psPressed && !s_lastPlayStopState) {
        psSlot.arpEngine->playStop(s_transport);
        if (psSlot.arpEngine->isPlaying()) {
          s_leds.triggerConfirm(CONFIRM_PLAY);
        } else {
          s_leds.triggerConfirm(CONFIRM_STOP);
        }
      }
      s_lastPlayStopState = psPressed;
    } else {
      // Not ARPEG or HOLD OFF: reset edge detection so it triggers correctly when switching
      s_lastPlayStopState = (s_playStopPad < NUM_KEYS) ? state.keyIsPressed[s_playStopPad] : false;
    }
  }

  // --- MIDI processing (skip when button held — single-layer control) ---
  if (!s_bankManager.isHolding() && !s_scaleManager.isHolding()) {
    BankSlot& slot = s_bankManager.getCurrentSlot();
    const ScaleConfig& scale = slot.scale;

    if (slot.type == BANK_NORMAL) {
      // === NORMAL MODE ===
      for (int i = 0; i < NUM_KEYS; i++) {
        bool pressed    = state.keyIsPressed[i];
        bool wasPressed = s_lastKeys[i];

        if (pressed && !wasPressed) {
          uint8_t vel = slot.baseVelocity;
          if (slot.velocityVariation > 0) {
            int16_t range = (int16_t)slot.velocityVariation * 127 / 200;
            int16_t offset = (int16_t)(random(-range, range + 1));
            int16_t result = (int16_t)vel + offset;
            vel = (uint8_t)constrain(result, 1, 127);
          }
          s_midiEngine.noteOn(i, vel, s_padOrder, scale);
        } else if (!pressed && wasPressed) {
          s_midiEngine.noteOff(i);
        }

        if (pressed) {
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

      }

    } else if (slot.type == BANK_ARPEG && slot.arpEngine) {
      // === ARPEG MODE ===

      // Detect HOLD toggle ON→OFF: sync pile with physical pad state
      static bool s_lastHoldState[NUM_BANKS] = {};
      uint8_t curBank = s_bankManager.getCurrentBank();
      bool holdNow = slot.arpEngine->isHoldOn();
      if (s_lastHoldState[curBank] && !holdNow) {
        for (int j = 0; j < NUM_KEYS; j++) {
          if (!state.keyIsPressed[j]) {
            slot.arpEngine->removePadPosition(s_padOrder[j]);
          }
        }
      }
      s_lastHoldState[curBank] = holdNow;

      for (int i = 0; i < NUM_KEYS; i++) {
        // Skip hold pad (handled by ScaleManager) and play/stop pad when HOLD is ON
        if (i == s_holdPad) continue;
        if (i == s_playStopPad && slot.arpEngine->isHoldOn()) continue;

        bool pressed    = state.keyIsPressed[i];
        bool wasPressed = s_lastKeys[i];
        uint8_t pos = s_padOrder[i];

        if (pressed && !wasPressed) {
          if (slot.arpEngine->isHoldOn()) {
            // HOLD ON: double-tap = remove, single tap = add
            if (s_lastPressTime[i] > 0 &&
                (now - s_lastPressTime[i]) < (uint32_t)s_doubleTapMs) {
              slot.arpEngine->removePadPosition(pos);
            } else {
              slot.arpEngine->addPadPosition(pos);
            }
          } else {
            // HOLD OFF: press = add
            slot.arpEngine->addPadPosition(pos);
          }
          s_lastPressTime[i] = now;

        } else if (!pressed && wasPressed) {
          if (!slot.arpEngine->isHoldOn()) {
            // HOLD OFF: release = remove
            slot.arpEngine->removePadPosition(pos);
          }
        }
      }
    }
  }

  // Always sync edge state — prevents ghost notes when button releases
  for (int i = 0; i < NUM_KEYS; i++) {
    s_lastKeys[i] = state.keyIsPressed[i];
  }

  // --- ArpScheduler: dispatch clock ticks to all active arps ---
  s_arpScheduler.tick();
  s_arpScheduler.processEvents();  // Fire pending gate noteOff + shuffled noteOn

  // --- CRITICAL PATH END ---
  s_midiEngine.flush();

  // --- SECONDARY: Pots ---
  BankSlot& potSlot = s_bankManager.getCurrentSlot();
  s_potRouter.update(leftHeld, rearHeld, potSlot.type);

  // Update internal tempo from pot (slow-changing, OK in secondary section)
  s_clockManager.setInternalBPM(s_potRouter.getTempoBPM());

  // --- Musical params: push live to BankSlot ---
  potSlot.baseVelocity      = s_potRouter.getBaseVelocity();
  potSlot.velocityVariation  = s_potRouter.getVelocityVariation();
  {
    uint16_t newPB = s_potRouter.getPitchBend();
    if (newPB != potSlot.pitchBendOffset) {
      potSlot.pitchBendOffset = newPB;
      s_midiEngine.sendPitchBend(newPB);
    }
  }

  // --- Musical params: push live to ArpEngine (if ARPEG bank) ---
  if (potSlot.type == BANK_ARPEG && potSlot.arpEngine) {
    potSlot.arpEngine->setGateLength(s_potRouter.getGateLength());
    potSlot.arpEngine->setShuffleDepth(s_potRouter.getShuffleDepth());
    potSlot.arpEngine->setDivision(s_potRouter.getDivision());
    potSlot.arpEngine->setPattern(s_potRouter.getPattern());
    potSlot.arpEngine->setShuffleTemplate(s_potRouter.getShuffleTemplate());
    potSlot.arpEngine->setBaseVelocity(s_potRouter.getBaseVelocity());
    potSlot.arpEngine->setVelocityVariation(s_potRouter.getVelocityVariation());
  }

  // --- Global params: Core 0 atomics ---
  s_responseShape.store(s_potRouter.getResponseShape(), std::memory_order_relaxed);
  s_slewRate.store(s_potRouter.getSlewRate(), std::memory_order_relaxed);
  s_padSensitivity.store(s_potRouter.getPadSensitivity(), std::memory_order_relaxed);

  // --- Non-musical params ---
  s_leds.setBrightness(s_potRouter.getLedBrightness());

  // --- MIDI CC/PB from user-assigned pot mappings (secondary priority) ---
  {
    uint8_t ccNum, ccVal;
    while (s_potRouter.consumeCC(ccNum, ccVal)) {
      s_transport.sendCC(potSlot.channel, ccNum, ccVal);
    }
    uint16_t pbVal;
    if (s_potRouter.consumePitchBend(pbVal)) {
      s_transport.sendPitchBend(potSlot.channel, pbVal);
    }
  }

  // LED bargraph from pot movement
  if (s_potRouter.hasBargraphUpdate()) {
    s_leds.showPotBargraph(
      s_potRouter.getBargraphLevel(),
      s_potRouter.getBargraphPotLevel(),
      s_potRouter.isBargraphCaught()
    );
  }

  s_leds.update();

  // --- Panic: BLE reconnect detection ---
  {
    bool bleNow = s_transport.isBleConnected();
    if (bleNow && !s_lastBleConnected && s_panicOnReconnect) {
      midiPanic();
    }
    s_lastBleConnected = bleNow;
  }

  // --- Panic: manual trigger (triple-click rear button within 600ms) ---
  {
    static bool    s_lastRearState = false;
    static uint8_t s_rearClickCount = 0;
    static uint32_t s_rearFirstClickTime = 0;

    bool rearNow = rearHeld;
    // Detect rising edge (press)
    if (rearNow && !s_lastRearState) {
      if (s_rearClickCount == 0) {
        s_rearFirstClickTime = now;
      }
      s_rearClickCount++;
      if (s_rearClickCount >= 3) {
        midiPanic();
        s_rearClickCount = 0;
      }
    }
    // Reset if window expired
    if (s_rearClickCount > 0 && (now - s_rearFirstClickTime) > 600) {
      s_rearClickCount = 0;
    }
    s_lastRearState = rearNow;
  }

  // --- NVS: pot debounce (10s after last change) + signal task ---
  bool potDirty = s_potRouter.isDirty();
  s_nvsManager.tickPotDebounce(now, potDirty, s_potRouter,
                                s_bankManager.getCurrentBank(), potSlot.type);
  if (potDirty) s_potRouter.clearDirty();

  // --- Debug: print pot parameters on change ---
  #if DEBUG_SERIAL
  {
    static float    s_dbgShape    = -1.0f;
    static uint16_t s_dbgSlew     = 0xFFFF;
    static uint16_t s_dbgAtDz     = 0xFFFF;
    static uint16_t s_dbgPB       = 0xFFFF;
    static uint8_t  s_dbgBaseVel  = 0xFF;
    static uint8_t  s_dbgVelVar   = 0xFF;
    static uint16_t s_dbgTempo    = 0xFFFF;
    static uint8_t  s_dbgLedBr    = 0xFF;
    static uint8_t  s_dbgPadSens  = 0xFF;
    static float    s_dbgGate     = -1.0f;
    static float    s_dbgShufDep  = -1.0f;
    static uint8_t  s_dbgDiv      = 0xFF;
    static uint8_t  s_dbgPat      = 0xFF;
    static uint8_t  s_dbgShufTpl  = 0xFF;

    static const char* s_divNames[] = {"4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/32","1/64"};
    static const char* s_patNames[] = {"Up","Down","UpDown","Random","Order"};

    float    shape   = s_potRouter.getResponseShape();
    uint16_t slew    = s_potRouter.getSlewRate();
    uint16_t atDz    = s_potRouter.getAtDeadzone();
    uint16_t pb      = s_potRouter.getPitchBend();
    uint8_t  baseVel = s_potRouter.getBaseVelocity();
    uint8_t  velVar  = s_potRouter.getVelocityVariation();
    uint16_t tempo   = s_potRouter.getTempoBPM();
    uint8_t  ledBr   = s_potRouter.getLedBrightness();
    uint8_t  padSens = s_potRouter.getPadSensitivity();
    float    gate    = s_potRouter.getGateLength();
    float    shufDep = s_potRouter.getShuffleDepth();
    uint8_t  div     = (uint8_t)s_potRouter.getDivision();
    uint8_t  pat     = (uint8_t)s_potRouter.getPattern();
    uint8_t  shufTpl = s_potRouter.getShuffleTemplate();

    // Global params
    if ((int)(shape * 100) != (int)(s_dbgShape * 100)) { Serial.printf("[POT] Shape=%.2f\n", shape); s_dbgShape = shape; }
    if (slew != s_dbgSlew)       { Serial.printf("[POT] Slew=%u\n", slew); s_dbgSlew = slew; }
    if (atDz != s_dbgAtDz)       { Serial.printf("[POT] AT_Deadzone=%u\n", atDz); s_dbgAtDz = atDz; }
    if (tempo != s_dbgTempo)     { Serial.printf("[POT] Tempo=%u BPM\n", tempo); s_dbgTempo = tempo; }
    if (ledBr != s_dbgLedBr)     { Serial.printf("[POT] LED_Bright=%u\n", ledBr); s_dbgLedBr = ledBr; }
    if (padSens != s_dbgPadSens) { Serial.printf("[POT] PadSens=%u\n", padSens); s_dbgPadSens = padSens; }

    // Per-bank params (always tracked, foreground bank)
    if (baseVel != s_dbgBaseVel) { Serial.printf("[POT] BaseVel=%u\n", baseVel); s_dbgBaseVel = baseVel; }
    if (velVar != s_dbgVelVar)   { Serial.printf("[POT] VelVar=%u\n", velVar); s_dbgVelVar = velVar; }
    if (pb != s_dbgPB)           { Serial.printf("[POT] PitchBend=%u\n", pb); s_dbgPB = pb; }

    // Arp params
    if ((int)(gate * 100) != (int)(s_dbgGate * 100))       { Serial.printf("[POT] Gate=%.2f\n", gate); s_dbgGate = gate; }
    if ((int)(shufDep * 100) != (int)(s_dbgShufDep * 100)) { Serial.printf("[POT] ShufDepth=%.2f\n", shufDep); s_dbgShufDep = shufDep; }
    if (div != s_dbgDiv)     { Serial.printf("[POT] Division=%s\n", div < 9 ? s_divNames[div] : "?"); s_dbgDiv = div; }
    if (pat != s_dbgPat)     { Serial.printf("[POT] Pattern=%s\n", pat < 5 ? s_patNames[pat] : "?"); s_dbgPat = pat; }
    if (shufTpl != s_dbgShufTpl) { Serial.printf("[POT] ShufTpl=%u\n", shufTpl); s_dbgShufTpl = shufTpl; }
  }
  #endif

  // Check if any pad is pressed (NVS won't write during play)
  bool anyPressed = false;
  for (int i = 0; i < NUM_KEYS; i++) {
    if (state.keyIsPressed[i]) { anyPressed = true; break; }
  }
  s_nvsManager.setAnyPadPressed(anyPressed);
  s_nvsManager.notifyIfDirty();

  // --- Hardware debug: log only on button/pot changes ---
  #if DEBUG_HARDWARE
  {
    static bool s_hwInit = false;
    static bool s_lastLeft = false;
    static bool s_lastRear = false;
    static int  s_lastPot[NUM_POTS] = {0, 0, 0, 0, 0};

    const float* smoothed = s_potRouter.getSmoothedAdc();
    int potNow[NUM_POTS];
    for (uint8_t i = 0; i < NUM_POTS; i++) {
      potNow[i] = (int)smoothed[i];
    }

    if (!s_hwInit) {
      s_hwInit = true;
      s_lastLeft = leftHeld;
      s_lastRear = rearHeld;
      for (uint8_t i = 0; i < NUM_POTS; i++) {
        s_lastPot[i] = potNow[i];
      }
      Serial.printf("[HW] BTN left=%d rear=%d | POT R1=%d R2=%d R3=%d R4=%d Rear=%d\n",
                    leftHeld ? 1 : 0, rearHeld ? 1 : 0,
                    potNow[0], potNow[1], potNow[2], potNow[3], potNow[4]);
    } else {
      if (leftHeld != s_lastLeft || rearHeld != s_lastRear) {
        Serial.printf("[HW] BTN left=%d rear=%d\n",
                      leftHeld ? 1 : 0, rearHeld ? 1 : 0);
        s_lastLeft = leftHeld;
        s_lastRear = rearHeld;
      }

      bool potChanged = false;
      for (uint8_t i = 0; i < NUM_POTS; i++) {
        if (abs(potNow[i] - s_lastPot[i]) >= POT_DEADZONE) {
          potChanged = true;
          break;
        }
      }
      if (potChanged) {
        Serial.printf("[HW] POT R1=%d R2=%d R3=%d R4=%d Rear=%d\n",
                      potNow[0], potNow[1], potNow[2], potNow[3], potNow[4]);
        for (uint8_t i = 0; i < NUM_POTS; i++) {
          s_lastPot[i] = potNow[i];
        }
      }
    }
  }
  #endif

  vTaskDelay(1);
}
