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
#include "core/PotFilter.h"

// MIDI
#include "midi/MidiEngine.h"
#include "midi/ClockManager.h"

// Arp
#include "arp/ArpEngine.h"
#include "arp/ArpScheduler.h"

// Loop
#include "loop/LoopEngine.h"
#include "loop/LoopTestConfig.h"  // defines LOOP_TEST_ENABLED (to be removed in Phase 3)

// Managers
#include "managers/BankManager.h"
#include "managers/ScaleManager.h"
#include "managers/PotRouter.h"
#include "managers/BatteryMonitor.h"
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
static BatteryMonitor     s_batteryMonitor;
static NvsManager         s_nvsManager;
static SetupManager       s_setupManager;
static ClockManager       s_clockManager;
static ArpScheduler       s_arpScheduler;

// 8 banks — all NORMAL for now
static BankSlot s_banks[NUM_BANKS];

// Static ArpEngine pool (max 4 ARPEG banks)
static ArpEngine s_arpEngines[4];

// Static LoopEngine pool (max MAX_LOOP_BANKS = 2 LOOP banks). ~10 KB per
// instance (mainly _events[1024] = 8 KB). Always alive regardless of type.
static LoopEngine s_loopEngines[MAX_LOOP_BANKS];

// Pad ordering: sequential 0..47 (Tool 2 will customize later)
static uint8_t s_padOrder[NUM_KEYS];

// Edge detection: previous frame's key state (Core 1 only)
static uint8_t s_lastKeys[NUM_KEYS];

// Play/Stop pad (ARPEG only, always active — not gated by button hold)
static uint8_t s_playStopPad = 24;  // Default, overwritten by NVS
static bool    s_lastPlayStopState = false;

// LOOP control pads (Phase 2: loaded from LoopTestConfig.h; Phase 3 will
// source them from LoopPadStore via ToolPadRoles).
// s_loopSlotPads[16] is declared for the Phase 6 slot drive but unused in
// Phase 2 — initialised to 0xFF in setup() (Phase 1 Step 7c-5 cross-phase ref).
static uint8_t  s_recPad             = 0xFF;
static uint8_t  s_loopPlayPad        = 0xFF;
static uint8_t  s_clearPad           = 0xFF;
static uint8_t  s_loopSlotPads[LOOP_SLOT_COUNT];  // Phase 6 — initialised 0xFF below
static bool     s_lastRecState       = false;
static bool     s_lastLoopPlayState  = false;
static bool     s_lastClearState     = false;
static uint32_t s_clearPressStart    = 0;
static bool     s_clearFired         = false;
static const uint32_t CLEAR_LONG_PRESS_MS = 500;

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
  uint32_t bootStartMs = millis();  // True boot time for setup window
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== ILLPAD48 V2 ===");

  // LEDs first — needed for boot progress and error feedback
  s_leds.begin();
  s_leds.showBootProgress(1);  // Step 1: LED hardware ready
  s_leds.update();

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN, I2C_CLOCK_HZ);
  s_leds.showBootProgress(2);  // Step 2: I2C bus ready
  s_leds.update();
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
  s_leds.update();
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
    s_banks[i].loopEngine         = nullptr;
    s_banks[i].isForeground       = false;
    s_banks[i].baseVelocity       = DEFAULT_BASE_VELOCITY;
    s_banks[i].velocityVariation  = DEFAULT_VELOCITY_VARIATION;
    s_banks[i].pitchBendOffset    = DEFAULT_PITCH_BEND_OFFSET;
    bankPads[i] = i;  // defaults
  }

  // LOOP slot pads default unassigned (Phase 6 will populate from LoopPadStore)
  memset(s_loopSlotPads, 0xFF, sizeof(s_loopSlotPads));

  // Scale/arp pad defaults (declared here so setup mode can access them)
  uint8_t rootPads[7], modePads[7];
  for (uint8_t i = 0; i < 7; i++) { rootPads[i] = 8 + i; modePads[i] = 15 + i; }
  uint8_t chromaticPad = 22;
  uint8_t holdPad = 23;
  uint8_t octavePads[4] = {25, 26, 27, 28};

  // =================================================================
  // PotFilter early init — needed in both runtime AND setup mode
  // (Phase 2: pot nav in setup tools reads PotFilter::getStable())
  // begin() has an internal guard so the second call at normal boot is a no-op.
  // =================================================================
  PotFilter::begin();
  {
    PotFilterStore pfs;
    if (NvsManager::loadBlob(POTFILTER_NVS_NAMESPACE, POTFILTER_NVS_KEY,
                             EEPROM_MAGIC, POT_FILTER_VERSION,
                             &pfs, sizeof(pfs))) {
      validatePotFilterStore(pfs);
      PotFilter::setConfig(pfs);
    }
  }

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
        s_leds.endBoot();     // Exit boot display so chase is visible
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
          break;
        }
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
      // F-CODE-7: setupManager.run() never returns — its only exit path is
      // ESP.restart() in Tool 0 confirmation. Code below this point was dead.
    }
  }

  // MIDI Transport (USB + BLE) — after setup, before normal boot
  // Load BLE interval early (before begin) so BLE_OFF can skip BLE init entirely.
  // F-CODE-8 fix: use NvsManager::loadBlob + validate (was: ad-hoc Preferences read).
  {
    SettingsStore tmp;
    if (NvsManager::loadBlob(SETTINGS_NVS_NAMESPACE, SETTINGS_NVS_KEY,
                             EEPROM_MAGIC, SETTINGS_VERSION, &tmp, sizeof(tmp))) {
      validateSettingsStore(tmp);
      s_transport.setBleInterval(tmp.bleInterval);
    }
  }
  s_leds.showBootProgress(4);  // Step 4: starting MIDI
  s_leds.update();
  s_transport.begin();
  #if DEBUG_SERIAL
  Serial.println("[INIT] MIDI Transport OK.");
  #endif

  // Clock Manager — wire MIDI clock reception callbacks
  s_clockManager.begin(&s_transport);
  s_transport.setClockCallback([](uint8_t src) { s_clockManager.onMidiClockTick(src); });
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
  s_leds.update();
  #if DEBUG_SERIAL
  Serial.printf("[INIT] NVS loaded. Bank=%d\n", currentBank);
  #endif

  // Apply loaded settings
  s_keyboard.setBaselineProfile(s_settings.baselineProfile);
  s_midiEngine.setAftertouchRate(s_settings.aftertouchRate);
  s_transport.setBleInterval(s_settings.bleInterval);
  s_clockManager.setMasterMode(s_settings.clockMode == CLOCK_MASTER);
  s_doubleTapMs = s_settings.doubleTapMs;
  s_leds.setPotBarDuration(s_settings.potBarDurationMs);
  s_leds.loadLedSettings(s_nvsManager.getLoadedLedSettings());
  s_leds.loadColorSlots(s_nvsManager.getLoadedColorSlots());
  s_panicOnReconnect = (s_settings.panicOnReconnect != 0);
  s_batteryMonitor.setAdcAtFull(s_settings.batAdcAtFull);

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

  // Assign LoopEngines to LOOP banks (after NVS load).
  // AUDIT FIX A1: reads the loop quantize mode from NvsManager (populated by
  // loadAll() from BankTypeStore.loopQuantize[]). Defaults to LOOP_QUANT_FREE
  // when the store was fresh (set by NvsManager constructor — Phase 1 Step 3e).
  {
    uint8_t loopIdx = 0;
    for (uint8_t i = 0; i < NUM_BANKS && loopIdx < MAX_LOOP_BANKS; i++) {
      if (s_banks[i].type == BANK_LOOP) {
        s_loopEngines[loopIdx].begin(i);
        s_loopEngines[loopIdx].setPadOrder(s_padOrder);
        s_loopEngines[loopIdx].setLoopQuantizeMode(
            s_nvsManager.getLoadedLoopQuantizeMode(i));
        s_banks[i].loopEngine = &s_loopEngines[loopIdx];
        loopIdx++;
        #if DEBUG_SERIAL
        Serial.printf("[INIT] Bank %d: LOOP, LoopEngine assigned (quantize=%u)\n",
                      i + 1, (unsigned)s_nvsManager.getLoadedLoopQuantizeMode(i));
        #endif
      }
    }
  }

  // LoopTestConfig override (Phase 2 only — removed in Phase 3).
  // Forces LOOP_TEST_BANK as LOOP and assigns the 3 control pads, so the
  // engine can be exercised on-device before Tool 4/Tool 3 gain LOOP support.
  // Safe assumption: NVS cannot contain a LOOP bank yet (no writer in Phase 1),
  // so s_loopEngines[0] is guaranteed available for reuse here.
  #if LOOP_TEST_ENABLED
  s_banks[LOOP_TEST_BANK].type = BANK_LOOP;
  if (!s_banks[LOOP_TEST_BANK].loopEngine) {
      s_loopEngines[0].begin(LOOP_TEST_BANK);
      s_loopEngines[0].setPadOrder(s_padOrder);
      s_loopEngines[0].setLoopQuantizeMode(LOOP_QUANT_FREE);  // test = no quantize
      s_banks[LOOP_TEST_BANK].loopEngine = &s_loopEngines[0];
  }
  s_recPad      = LOOP_TEST_REC_PAD;
  s_loopPlayPad = LOOP_TEST_PLAYSTOP_PAD;
  s_clearPad    = LOOP_TEST_CLEAR_PAD;
  #if DEBUG_SERIAL
  Serial.printf("[INIT] LoopTestConfig: bank %d forced LOOP, pads rec=%u ps=%u clr=%u\n",
                LOOP_TEST_BANK + 1, s_recPad, s_loopPlayPad, s_clearPad);
  #endif
  #endif

  // ArpScheduler — register engines + set scale/padOrder context
  s_arpScheduler.begin(&s_transport, &s_clockManager);
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (s_banks[i].type == BANK_ARPEG && s_banks[i].arpEngine) {
      s_arpScheduler.registerArp(i, s_banks[i].arpEngine);
      s_banks[i].arpEngine->setScaleConfig(s_banks[i].scale);
      s_banks[i].arpEngine->setPadOrder(s_padOrder);

      // Apply NVS-loaded arp params to each engine
      const ArpPotStore& arp = s_nvsManager.getLoadedArpParams(i);
      s_banks[i].arpEngine->setGateLength(max(0.05f, (float)arp.gateRaw / 4095.0f));
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
  s_leds.update();
  #if DEBUG_SERIAL
  Serial.println("[INIT] ArpScheduler OK.");
  #endif

  // Give LedController access to bank states for multi-bank display
  s_leds.setBankSlots(s_banks);

  // Store hold pad for ARPEG toggle detection in loop
  s_holdPad = holdPad;

  // Bank Manager
  s_bankManager.begin(&s_midiEngine, &s_leds, s_banks, s_lastKeys, &s_transport);
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

  // Battery Monitor
  s_batteryMonitor.begin(&s_leds);

  // ── Boot order matters ──
  // 1. loadAll()          → params loaded into PotRouter (done above)
  // 2. PotFilter::begin() → GPIO + initial ADC reads
  // 3. PotRouter::begin() → seedCatchValues() uses both
  PotFilter::begin();
  s_potRouter.begin();
  s_leds.showBootProgress(7);  // Step 7: managers ready
  s_leds.update();
  #if DEBUG_SERIAL
  Serial.println("[INIT] PotFilter + PotRouter OK.");
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
  s_leds.update();
  delay(200);                   // Brief full-bar display before switching to bank mode
  s_leds.endBoot();             // Exit boot mode, switch to normal bank display

  #if DEBUG_SERIAL
  Serial.println("[INIT] Ready.");
  #endif
}

// =================================================================
// Extracted helpers (called by loop(), same file — access globals directly)
// =================================================================

// --- Pad input: NORMAL noteOn/Off/AT + ARPEG add/remove + stuck-note cleanup ---
// --- Per-type pad processing (extracted for LOOP extensibility) ---

static void processNormalMode(const SharedKeyboardState& state, BankSlot& slot) {
  const ScaleConfig& scale = slot.scale;
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
      uint8_t dz = (uint8_t)min((uint16_t)126, s_potRouter.getAtDeadzone());
      if (p > dz) {
        uint8_t range = 127 - dz;
        uint8_t scaled = (range > 0)
          ? (uint8_t)((uint16_t)(p - dz) * 127 / range)
          : 127;
        s_midiEngine.updateAftertouch(i, scaled);
      } else {
        s_midiEngine.updateAftertouch(i, 0);
      }
    }
  }
}

static void processArpMode(const SharedKeyboardState& state, BankSlot& slot, uint32_t now) {
  static bool s_lastHoldState[NUM_BANKS] = {};

  // Detect HOLD toggle ON→OFF: sync pile with physical pad state
  uint8_t curBank = s_bankManager.getCurrentBank();
  bool holdNow = slot.arpEngine->isHoldOn();
  if (s_lastHoldState[curBank] && !holdNow) {
    for (int j = 0; j < NUM_KEYS; j++) {
      if (j == s_holdPad || j == s_playStopPad) continue;
      if (!state.keyIsPressed[j]) {
        slot.arpEngine->removePadPosition(s_padOrder[j]);
      }
    }
  }
  s_lastHoldState[curBank] = holdNow;

  for (int i = 0; i < NUM_KEYS; i++) {
    if (i == s_holdPad) continue;
    if (i == s_playStopPad && slot.arpEngine->isHoldOn()) continue;

    bool pressed    = state.keyIsPressed[i];
    bool wasPressed = s_lastKeys[i];
    uint8_t pos = s_padOrder[i];

    if (pressed && !wasPressed) {
      if (slot.arpEngine->isHoldOn()) {
        if (s_lastPressTime[i] > 0 &&
            (now - s_lastPressTime[i]) < (uint32_t)s_doubleTapMs) {
          slot.arpEngine->removePadPosition(pos);
          s_lastPressTime[i] = 0;
        } else {
          slot.arpEngine->addPadPosition(pos);
          s_lastPressTime[i] = now;
        }
      } else {
        slot.arpEngine->addPadPosition(pos);
      }

    } else if (!pressed && wasPressed) {
      if (!slot.arpEngine->isHoldOn()) {
        slot.arpEngine->removePadPosition(pos);
      }
    }
  }
}

// --- LOOP mode pad input: live play via refcount + routing into record buffer ---
// AUDIT FIX (B1, 2026-04-06): uses setLiveNote() + releaseLivePad() instead of
// direct noteRefDecrement + manual sendNoteOn at release. The release path is
// symmetric with the handleLeftReleaseCleanup sweep, which uses the same
// idempotent releaseLivePad() pattern.
//
// LOOP bypasses MidiEngine (no ScaleResolver — padToNote() is a direct
// offset). Refcount + _liveNote[] dedupe live presses against any loop
// playback event already ringing on the same MIDI note.
static void processLoopMode(const SharedKeyboardState& state, BankSlot& slot, uint32_t now) {
  (void)now;  // kept for signature symmetry with processArpMode
  LoopEngine* eng = slot.loopEngine;
  if (!eng) return;

  LoopEngine::State ls = eng->getState();

  for (uint8_t p = 0; p < NUM_KEYS; p++) {
    // Skip LOOP control pads (REC / PLAY-STOP / CLEAR) — handled in handleLoopControls
    if (p == s_recPad || p == s_loopPlayPad || p == s_clearPad) continue;

    bool pressed    = state.keyIsPressed[p];
    bool wasPressed = s_lastKeys[p];

    if (pressed && !wasPressed) {
      uint8_t note = eng->padToNote(p);
      if (note == 0xFF) continue;  // unmapped pad
      uint8_t vel  = slot.baseVelocity;
      if (slot.velocityVariation > 0) {
        int16_t range  = (int16_t)slot.velocityVariation * 127 / 200;
        int16_t offset = (int16_t)(random(-range, range + 1));
        int16_t result = (int16_t)vel + offset;
        vel = (uint8_t)constrain(result, 1, 127);
      }
      // Refcount: only send MIDI noteOn on 0→1 transition
      if (eng->noteRefIncrement(note)) {
        s_transport.sendNoteOn(slot.channel, note, vel);
      }
      // B1 fix: track per-pad live note for idempotent cleanup at hold-release
      // and bank-switch sweeps (mirrors MidiEngine._lastResolvedNote pattern).
      eng->setLiveNote(p, note);
      // Record during RECORDING or OVERDUBBING
      if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
        eng->recordNoteOn(p, vel);
      }
    } else if (!pressed && wasPressed) {
      // B1 fix: releaseLivePad is idempotent — handles refcount decrement AND
      // MIDI noteOff internally AND clears _liveNote[p]. No manual padToNote
      // / noteRefDecrement / sendNoteOn needed here.
      eng->releaseLivePad(p, s_transport);
      if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
        eng->recordNoteOff(p);
      }
    }
  }
}

static void handleLeftReleaseCleanup(const SharedKeyboardState& state) {
  static bool s_wasHolding = false;
  bool holdingNow = s_bankManager.isHolding() || s_scaleManager.isHolding();
  if (s_wasHolding && !holdingNow) {
    BankSlot& relSlot = s_bankManager.getCurrentSlot();
    if (relSlot.type == BANK_NORMAL) {
      for (int i = 0; i < NUM_KEYS; i++) {
        if (!state.keyIsPressed[i]) {
          s_midiEngine.noteOff(i);
        }
      }
    } else if (relSlot.type == BANK_ARPEG && relSlot.arpEngine
               && !relSlot.arpEngine->isHoldOn()) {
      for (int i = 0; i < NUM_KEYS; i++) {
        if (i == s_holdPad) continue;
        if (!state.keyIsPressed[i]) {
          relSlot.arpEngine->removePadPosition(s_padOrder[i]);
        }
      }
    }
  }
  s_wasHolding = holdingNow;
}

static void handlePadInput(const SharedKeyboardState& state, uint32_t now) {
  // MIDI processing (skip when button held — single-layer control)
  if (!s_bankManager.isHolding() && !s_scaleManager.isHolding()) {
    BankSlot& slot = s_bankManager.getCurrentSlot();
    switch (slot.type) {
      case BANK_NORMAL:
        processNormalMode(state, slot);
        break;
      case BANK_ARPEG:
        if (slot.arpEngine) processArpMode(state, slot, now);
        break;
      case BANK_LOOP:
        if (slot.loopEngine) processLoopMode(state, slot, now);
        break;
      default: break;  // BANK_ANY (sentinel) — ignored
    }
  }

  handleLeftReleaseCleanup(state);
}

// --- Reload per-bank pot params after bank switch (extracted for LOOP extensibility) ---
static void reloadPerBankParams(BankSlot& newSlot) {
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
  // LOOP: shuffle depth/template are shared with ARPEG slots; gate/div/pattern
  // are ARPEG-only. Pull what LoopEngine exposes so the PotRouter catch state
  // reflects the loop's per-bank params on bank switch.
  if (newSlot.type == BANK_LOOP && newSlot.loopEngine) {
    shufDepth = newSlot.loopEngine->getShuffleDepth();
    shufTmpl  = newSlot.loopEngine->getShuffleTemplate();
  }

  s_potRouter.loadStoredPerBank(
    newSlot.baseVelocity, newSlot.velocityVariation, newSlot.pitchBendOffset,
    gate, shufDepth, div, pat, shufTmpl
  );
  s_potRouter.seedCatchValues(true);
  s_potRouter.resetPerBankCatch();
}

// --- Manager updates: bank/scale switch, flag consumption, clock ---
static bool handleManagerUpdates(const SharedKeyboardState& state, bool leftHeld) {
  // Managers (both use left button — single-layer control)
  bool bankSwitched = s_bankManager.update(state.keyIsPressed, leftHeld);
  s_scaleManager.update(state.keyIsPressed, leftHeld, s_bankManager.getCurrentSlot());

  // On bank switch: reload per-bank pot values from the new bank, then reset catch.
  if (bankSwitched) {
    s_nvsManager.queueBankWrite(s_bankManager.getCurrentBank());
    reloadPerBankParams(s_bankManager.getCurrentSlot());
  }

  // Consume all ScaleManager change flags once (auto-clearing)
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

  // LED confirmation on hold toggle + reset double-tap timestamps
  if (holdToggled) {
    BankSlot& holdSlot = s_bankManager.getCurrentSlot();
    bool holdIsOn = (holdSlot.type == BANK_ARPEG && holdSlot.arpEngine && holdSlot.arpEngine->isHoldOn());
    s_leds.triggerConfirm(holdIsOn ? CONFIRM_HOLD_ON : CONFIRM_HOLD_OFF);
    if (holdIsOn) {
      memset(s_lastPressTime, 0, sizeof(s_lastPressTime));
    }
  }

  // Clock: process ticks (PLL + tick generation)
  s_clockManager.update();

  return bankSwitched;
}

// --- Play/Stop pad edge detection (ARPEG + HOLD ON only) ---
static void handlePlayStopPad(const SharedKeyboardState& state,
                               bool holdBeforeUpdate, bool bankSwitched) {
  BankSlot& psSlot = s_bankManager.getCurrentSlot();
  // Use pre-toggle hold state when same bank (prevents same-frame hold+playStop loss).
  // After bank switch, use live state (ScaleManager toggled hold on the new bank, not old).
  bool holdForPS = bankSwitched
      ? (psSlot.type == BANK_ARPEG && psSlot.arpEngine && psSlot.arpEngine->isHoldOn())
      : holdBeforeUpdate;
  if (s_playStopPad < NUM_KEYS && psSlot.type == BANK_ARPEG
      && psSlot.arpEngine && holdForPS) {
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

// --- LOOP control pads (REC / PLAY-STOP / CLEAR) ---
// Runs OUTSIDE the isHolding() guard — both hands free for drumming.
// REC: simple tap edge, dispatches on current state (EMPTY/RECORDING/PLAYING/
//      OVERDUBBING → next state, quantizable per-bank).
// PLAY/STOP: simple tap edge. PLAYING → stop() (quantizable soft flush),
//            OVERDUBBING → abortOverdub() (ALWAYS immediate, hard flush,
//            discards overdub), STOPPED → play() (quantizable).
// CLEAR: 500ms long-press with LED ramp. OVERDUBBING → cancelOverdub() (undo
//        overdub pass, keep loop), else → clear() (hard flush, state → EMPTY).
//        ALWAYS immediate — the 500ms hold IS the human quantize.
static void handleLoopControls(const SharedKeyboardState& state, uint32_t now) {
  BankSlot& slot = s_bankManager.getCurrentSlot();
  if (slot.type != BANK_LOOP || !slot.loopEngine) {
    // Not a LOOP bank — reset edge states so pads act as regular music pads
    s_lastRecState      = (s_recPad      < NUM_KEYS) ? state.keyIsPressed[s_recPad]      : false;
    s_lastLoopPlayState = (s_loopPlayPad < NUM_KEYS) ? state.keyIsPressed[s_loopPlayPad] : false;
    s_lastClearState    = (s_clearPad    < NUM_KEYS) ? state.keyIsPressed[s_clearPad]    : false;
    return;
  }

  LoopEngine* eng = slot.loopEngine;
  LoopEngine::State ls = eng->getState();

  // --- REC pad: simple tap edge, state-dispatched transition ---
  if (s_recPad < NUM_KEYS) {
    bool pressed = state.keyIsPressed[s_recPad];
    if (pressed && !s_lastRecState) {
      switch (ls) {
        case LoopEngine::EMPTY:
          eng->startRecording();
          s_leds.triggerConfirm(CONFIRM_LOOP_REC);
          break;
        case LoopEngine::RECORDING:
          eng->stopRecording(state.keyIsPressed, s_padOrder,
                             s_clockManager.getSmoothedBPMFloat());
          s_leds.triggerConfirm(CONFIRM_PLAY);
          break;
        case LoopEngine::PLAYING:
          eng->startOverdub();
          s_leds.triggerConfirm(CONFIRM_LOOP_REC);
          break;
        case LoopEngine::OVERDUBBING:
          eng->stopOverdub(state.keyIsPressed, s_padOrder,
                           s_clockManager.getSmoothedBPMFloat());
          s_leds.triggerConfirm(CONFIRM_PLAY);
          break;
        default: break;  // STOPPED: REC ignored
      }
    }
    s_lastRecState = pressed;
  }

  // --- PLAY/STOP pad: simple tap edge ---
  // No CONFIRM_PLAY/CONFIRM_STOP triggered for LOOP — the Phase 4 LED
  // rendering already gives distinct feedback (waiting quantize blink,
  // instant color change, magenta→dim abort transition).
  if (s_loopPlayPad < NUM_KEYS) {
    bool pressed = state.keyIsPressed[s_loopPlayPad];
    if (pressed && !s_lastLoopPlayState) {
      switch (ls) {
        case LoopEngine::PLAYING:
          // Quantizable soft stop — trailing pending events finish naturally
          eng->stop(s_transport);
          break;
        case LoopEngine::OVERDUBBING:
          // Abort — ALWAYS immediate, hard flush, discard overdub
          eng->abortOverdub(s_transport);
          break;
        case LoopEngine::STOPPED:
          // Quantizable play — snap to next boundary per loopQuantize mode
          eng->play(s_clockManager.getSmoothedBPMFloat());
          break;
        default: break;  // EMPTY, RECORDING: ignored
      }
    }
    s_lastLoopPlayState = pressed;
  }

  // --- CLEAR pad: long press (500ms) + LED ramp ---
  // ALWAYS immediate (no quantize snap — the 500ms hold IS the human quantize).
  //
  // IMPORTANT edge case: when ls == EMPTY we still update s_lastClearState
  // below so the rising-edge detection stays coherent across state changes.
  // Without this, a held CLEAR pad during an EMPTY→RECORDING transition
  // would produce a false rising edge and start the clear timer mid-recording.
  bool clearPressed = (s_clearPad < NUM_KEYS) ? state.keyIsPressed[s_clearPad] : false;
  if (s_clearPad < NUM_KEYS && ls != LoopEngine::EMPTY) {
    if (clearPressed && !s_lastClearState) {
      s_clearPressStart = now;
      s_clearFired = false;
    }
    if (clearPressed && !s_clearFired) {
      uint32_t held = now - s_clearPressStart;
      if (held < CLEAR_LONG_PRESS_MS) {
        uint8_t ramp = (uint8_t)((uint32_t)held * 100 / CLEAR_LONG_PRESS_MS);
        s_leds.showClearRamp(ramp);
      } else {
        // Ramp complete — dispatch based on current state
        if (ls == LoopEngine::OVERDUBBING) {
          // Undo overdub pass, keep loop. No confirm (color transition
          // magenta→base is instant and visible).
          eng->cancelOverdub();
        } else {
          // Hard flush, state → EMPTY. CONFIRM_STOP here: the transition from
          // a colored state to dim EMPTY is subtle, and clear is destructive
          // enough to warrant a distinct flash.
          eng->clear(s_transport);
          s_leds.triggerConfirm(CONFIRM_STOP);
        }
        s_clearFired = true;
      }
    }
  }
  // Always update clear edge state — even when ls == EMPTY — to avoid false
  // rising edges when state transitions while the pad is held.
  s_lastClearState = clearPressed;
}

// --- Push live pot params to engine (extracted for LOOP extensibility) ---
static void pushParamsToEngine(BankSlot& slot) {
  if (slot.type == BANK_ARPEG && slot.arpEngine) {
    slot.arpEngine->setGateLength(s_potRouter.getGateLength());
    slot.arpEngine->setShuffleDepth(s_potRouter.getShuffleDepth());
    slot.arpEngine->setDivision(s_potRouter.getDivision());
    slot.arpEngine->setPattern(s_potRouter.getPattern());
    slot.arpEngine->setShuffleTemplate(s_potRouter.getShuffleTemplate());
    slot.arpEngine->setBaseVelocity(s_potRouter.getBaseVelocity());
    slot.arpEngine->setVelocityVariation(s_potRouter.getVelocityVariation());
  }
}

// --- Push live pot params to LOOP engine (separate from pushParamsToEngine
// because LoopEngine has a distinct param surface: no gate/division/pattern,
// shares shuffle + velocity with ARPEG). Chaos, velPattern and velPatternDepth
// are Phase 5 stubs with no PotRouter target yet.
static void pushParamsToLoop(BankSlot& slot) {
  if (slot.type != BANK_LOOP || !slot.loopEngine) return;
  slot.loopEngine->setShuffleDepth(s_potRouter.getShuffleDepth());
  slot.loopEngine->setShuffleTemplate(s_potRouter.getShuffleTemplate());
  slot.loopEngine->setBaseVelocity(s_potRouter.getBaseVelocity());
  slot.loopEngine->setVelocityVariation(s_potRouter.getVelocityVariation());
  // Phase 5 stubs (no pot target yet): setChaosAmount, setVelPatternIdx,
  // setVelPatternDepth.
}

// --- Pot pipeline: ADC read, param push, CC/PB, bargraph, battery, LEDs ---
static void handlePotPipeline(bool leftHeld, bool rearHeld) {
  BankSlot& potSlot = s_bankManager.getCurrentSlot();
  s_potRouter.update(leftHeld, rearHeld, potSlot.type);

  // Update internal tempo from pot
  s_clockManager.setInternalBPM(s_potRouter.getTempoBPM());

  // Push live to BankSlot
  potSlot.baseVelocity      = s_potRouter.getBaseVelocity();
  potSlot.velocityVariation  = s_potRouter.getVelocityVariation();
  {
    uint16_t newPB = s_potRouter.getPitchBend();
    if (newPB != potSlot.pitchBendOffset) {
      potSlot.pitchBendOffset = newPB;
      s_midiEngine.sendPitchBend(newPB);
    }
  }

  // Push live params to engine (per bank type)
  pushParamsToEngine(potSlot);
  pushParamsToLoop(potSlot);

  // Global params: Core 0 atomics
  s_responseShape.store(s_potRouter.getResponseShape(), std::memory_order_relaxed);
  s_slewRate.store(s_potRouter.getSlewRate(), std::memory_order_relaxed);
  s_padSensitivity.store(s_potRouter.getPadSensitivity(), std::memory_order_relaxed);

  // Non-musical params
  s_leds.setBrightness(s_potRouter.getLedBrightness());

  // MIDI CC/PB from user-assigned pot mappings
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
    if (s_potRouter.getBargraphTarget() == TARGET_TEMPO_BPM) {
      s_leds.showTempoBargraph(
        s_potRouter.getBargraphLevel(),
        s_potRouter.getBargraphPotLevel(),
        s_potRouter.isBargraphCaught(),
        s_potRouter.getTempoBPM()
      );
    } else {
      s_leds.showPotBargraph(
        s_potRouter.getBargraphLevel(),
        s_potRouter.getBargraphPotLevel(),
        s_potRouter.isBargraphCaught()
      );
    }
  }

  // Battery monitor + low battery LED flag
  s_batteryMonitor.update(rearHeld);
  s_leds.setBatteryLow(s_batteryMonitor.isLow());

  s_leds.update();
}

// --- Panic: BLE reconnect + manual triple-click rear ---
static void handlePanicChecks(uint32_t now, bool rearHeld) {
  // BLE reconnect detection
  {
    bool bleNow = s_transport.isBleConnected();
    if (bleNow && !s_lastBleConnected && s_panicOnReconnect) {
      midiPanic();
    }
    s_lastBleConnected = bleNow;
  }

  // Manual trigger: triple-click rear button within 600ms
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
}

// --- Debug output: pot parameter changes + hardware state ---
static void debugOutput(bool leftHeld, bool rearHeld) {
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

  #if DEBUG_HARDWARE
  {
    static bool s_hwInit = false;
    static bool s_lastLeft = false;
    static bool s_lastRear = false;
    static int  s_lastPot[NUM_POTS] = {0, 0, 0, 0, 0};

    int potNow[NUM_POTS];
    for (uint8_t i = 0; i < NUM_POTS; i++) {
      potNow[i] = (int)PotFilter::getStable(i);
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

  // Snapshot hold state BEFORE ScaleManager may toggle it (for play/stop check below)
  bool holdBeforeUpdate = false;
  {
    BankSlot& snap = s_bankManager.getCurrentSlot();
    if (snap.type == BANK_ARPEG && snap.arpEngine)
      holdBeforeUpdate = snap.arpEngine->isHoldOn();
  }

  // --- CRITICAL PATH ---
  bool bankSwitched = handleManagerUpdates(state, leftHeld);

  handlePlayStopPad(state, holdBeforeUpdate, bankSwitched);

  // LOOP control pads (REC / PLAY-STOP / CLEAR) — before handlePadInput so
  // that a REC rising-edge transitions the state into RECORDING in the same
  // frame, letting processLoopMode capture the same frame's pad edges.
  handleLoopControls(state, now);

  handlePadInput(state, now);

  // Always sync edge state — prevents ghost notes when button releases
  for (int i = 0; i < NUM_KEYS; i++) {
    s_lastKeys[i] = state.keyIsPressed[i];
  }

  // --- ArpScheduler: dispatch clock ticks to all active arps ---
  s_arpScheduler.tick();
  s_arpScheduler.processEvents();  // Fire pending gate noteOff + shuffled noteOn

  // --- LoopEngine: tick + processEvents for all LOOP banks (foreground AND
  // background). Unlike ArpScheduler (which manages all arp engines
  // internally), LoopEngine tick/processEvents are called directly per bank
  // because LOOP playback is microsecond-based, not tick-based. globalTick
  // is only used by the pending action dispatcher for quantize boundary
  // crossing detection (not for step scheduling).
  {
    uint32_t globalTick  = s_clockManager.getCurrentTick();
    float    smoothedBpm = s_clockManager.getSmoothedBPMFloat();
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (s_banks[i].type == BANK_LOOP && s_banks[i].loopEngine) {
        s_banks[i].loopEngine->tick(s_transport, smoothedBpm, globalTick);
        s_banks[i].loopEngine->processEvents(s_transport);
      }
    }
  }

  // --- CRITICAL PATH END ---
  s_midiEngine.flush();

  // --- SECONDARY: Pots + params + CC/PB + bargraph + battery + LEDs ---
  handlePotPipeline(leftHeld, rearHeld);

  handlePanicChecks(now, rearHeld);

  // --- NVS: pot debounce (10s after last change) + signal task ---
  bool potDirty = s_potRouter.isDirty();
  s_nvsManager.tickPotDebounce(now, potDirty, s_potRouter,
                                s_bankManager.getCurrentBank(), s_bankManager.getCurrentSlot().type);
  if (potDirty) s_potRouter.clearDirty();

  // Check if any pad is pressed (NVS won't write during play)
  bool anyPressed = false;
  for (int i = 0; i < NUM_KEYS; i++) {
    if (state.keyIsPressed[i]) { anyPressed = true; break; }
  }
  s_nvsManager.setAnyPadPressed(anyPressed);
  s_nvsManager.notifyIfDirty();

  debugOutput(leftHeld, rearHeld);

  vTaskDelay(1);
}
