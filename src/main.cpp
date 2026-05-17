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

// Managers
#include "managers/BankManager.h"
#include "managers/ScaleManager.h"
#include "managers/PotRouter.h"
#include "managers/BatteryMonitor.h"
#include "managers/NvsManager.h"
#include "managers/ControlPadManager.h"

// Setup
#include "setup/SetupManager.h"

// Viewer
#include "viewer/ViewerSerial.h"

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
static ControlPadManager  s_controlPadManager;
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

// Double-tap detection (ARPEG captured mode)
static uint32_t s_lastPressTime[NUM_KEYS];
static uint8_t  s_doubleTapMs = DOUBLE_TAP_MS_DEFAULT;

// Hold pad (ARPEG OFF/ON switch, always exposed — not gated by button hold)
static uint8_t s_holdPad = 23;  // Default, overwritten by NVS

// Panic: BLE reconnect detection + settings
static bool    s_lastBleConnected = false;
static bool    s_panicOnReconnect = DEFAULT_PANIC_ON_RECONNECT;

// Boot stabilization
static const uint32_t BOOT_SETTLE_MS = 300;
static uint32_t s_bootTimestamp = 0;

// Slot-name lookup for viewer-API [POT] log annotation.
// Used by debugOutput() to prefix each [POT] line with the slot of origin
// (R1/R1H/R2/.../R4H) derived from the user's current pot mapping.
static const char* potSlotName(uint8_t slot) {
  static const char* NAMES[POT_MAPPING_SLOTS] = {
    "R1", "R1H", "R2", "R2H", "R3", "R3H", "R4", "R4H"
  };
  return slot < POT_MAPPING_SLOTS ? NAMES[slot] : "--";
}

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
    if (isArpType(s_banks[i].type) && s_banks[i].arpEngine) {
      s_banks[i].arpEngine->flushPendingNoteOffs(s_transport);
    }
  }
  // Phase 2: clear MidiEngine tracked notes (NORMAL mode)
  s_midiEngine.allNotesOff();
  // Phase 3: CC 123 on all 8 channels (catches anything we missed)
  for (uint8_t ch = 0; ch < NUM_BANKS; ch++) {
    s_transport.sendAllNotesOff(ch);
  }
  // Phase 4: re-emit bank-select Note On on canal 16 to resync the DAW.
  s_bankManager.emitBankSelectNote();
  #if DEBUG_SERIAL
  Serial.println("[PANIC] All notes off on all channels");
  #endif
}

// =================================================================
// Viewer API helpers — boot dump, per-bank state dump, ?STATE/?BANKS/?BOTH
// runtime command handler, [READY] marker. All gated by DEBUG_SERIAL.
// Plan: docs/superpowers/plans/2026-05-15-illpad-firmware-viewer-api.md
// =================================================================
#if DEBUG_SERIAL

// Emit [BANKS] count=8 + 8× [BANK] idx=N ... headers at boot.
// Per-bank metadata (type, channel, group, division+playing+octave for ARPEG,
// division+playing+mutationLevel for ARPEG_GEN). NORMAL/LOOP have only the
// minimal fields. Plan task A.3.
static void dumpBanksGlobal() {
  // ORDER MATCHES enum BankType in KeyboardData.h:324-329
  // BANK_NORMAL=0, BANK_ARPEG=1, BANK_LOOP=2, BANK_ARPEG_GEN=3
  static const char* TYPE_NAMES[] = { "NORMAL", "ARPEG", "LOOP", "ARPEG_GEN" };
  static const char* DIV_NAMES[]  = { "4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/32","1/64" };

  Serial.printf("[BANKS] count=%d\n", NUM_BANKS);
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    BankSlot& b = s_banks[i];
    uint8_t group = s_nvsManager.getLoadedScaleGroup(i);
    char groupChar = (group == 0) ? '0' : (char)('A' + group - 1);
    uint8_t typeIdx = (uint8_t)b.type;
    const char* typeName = (typeIdx < 4) ? TYPE_NAMES[typeIdx] : "?";

    Serial.printf("[BANK] idx=%d type=%s ch=%d group=%c",
                  i + 1, typeName, i + 1, groupChar);

    bool isArp = (b.type == BANK_ARPEG) || (b.type == BANK_ARPEG_GEN);
    if (isArp && b.arpEngine) {
      ArpDivision d = b.arpEngine->getDivision();
      uint8_t dIdx = (uint8_t)d;
      Serial.printf(" division=%s playing=%s",
                    dIdx < 9 ? DIV_NAMES[dIdx] : "?",
                    b.arpEngine->isPlaying() ? "true" : "false");
      if (b.type == BANK_ARPEG) {
        Serial.printf(" octave=%d", b.arpEngine->getOctaveRange());
      } else { // ARPEG_GEN
        Serial.printf(" mutationLevel=%d", b.arpEngine->getMutationLevel());
      }
    }
    Serial.println();
  }
}

// Format the "target:value" string for a given target, reading from the
// per-bank storage (s_banks[bankIdx] / arpEngine) for per-bank values, or
// from PotRouter for global values. Plan task A.4.
static void formatTargetValueForBank(char* buf, size_t bufSize,
                                     PotTarget t, uint8_t bankIdx,
                                     uint8_t mappingCcNumber,
                                     bool isForeground) {
  (void)isForeground;  // V1: same emit for foreground or not (CC live value
                       // delivered via [POT] events). Param kept for V2.
  static const char* DIV_NAMES[] = { "4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/32","1/64" };
  static const char* PAT_NAMES[] = { "Up","Down","UpDown","Order","PedalUp","Converge" };

  BankSlot& b = s_banks[bankIdx];
  ArpEngine* eng = b.arpEngine;

  switch (t) {
    // --- Global params (PotRouter is the single source of truth) ---
    case TARGET_RESPONSE_SHAPE:
      snprintf(buf, bufSize, "Shape:%.2f", s_potRouter.getResponseShape()); break;
    case TARGET_SLEW_RATE:
      snprintf(buf, bufSize, "Slew:%u", s_potRouter.getSlewRate()); break;
    case TARGET_AT_DEADZONE:
      snprintf(buf, bufSize, "AT_Deadzone:%u", s_potRouter.getAtDeadzone()); break;
    case TARGET_TEMPO_BPM:
      snprintf(buf, bufSize, "Tempo:%u", s_potRouter.getTempoBPM()); break;
    case TARGET_LED_BRIGHTNESS:
      snprintf(buf, bufSize, "LED_Bright:%u", s_potRouter.getLedBrightness()); break;
    case TARGET_PAD_SENSITIVITY:
      snprintf(buf, bufSize, "PadSens:%u", s_potRouter.getPadSensitivity()); break;

    // --- Per-bank static params (stored in BankSlot directly) ---
    case TARGET_BASE_VELOCITY:
      snprintf(buf, bufSize, "BaseVel:%u", b.baseVelocity); break;
    case TARGET_VELOCITY_VARIATION:
      snprintf(buf, bufSize, "VelVar:%u", b.velocityVariation); break;
    case TARGET_PITCH_BEND:
      snprintf(buf, bufSize, "PitchBend:%u", b.pitchBendOffset); break;

    // --- Per-bank arp params (stored in ArpEngine) ---
    case TARGET_GATE_LENGTH:
      if (eng) snprintf(buf, bufSize, "Gate:%.2f", eng->getGateLength());
      else     snprintf(buf, bufSize, "Gate:-");
      break;
    case TARGET_SHUFFLE_DEPTH:
      if (eng) snprintf(buf, bufSize, "ShufDepth:%.2f", eng->getShuffleDepth());
      else     snprintf(buf, bufSize, "ShufDepth:-");
      break;
    case TARGET_DIVISION: {
      if (eng) {
        uint8_t d = (uint8_t)eng->getDivision();
        snprintf(buf, bufSize, "Division:%s", d < 9 ? DIV_NAMES[d] : "?");
      } else {
        snprintf(buf, bufSize, "Division:-");
      }
      break;
    }
    case TARGET_PATTERN: {
      if (eng) {
        uint8_t p = (uint8_t)eng->getPattern();
        snprintf(buf, bufSize, "Pattern:%s", p < 6 ? PAT_NAMES[p] : "?");
      } else {
        snprintf(buf, bufSize, "Pattern:-");
      }
      break;
    }
    case TARGET_GEN_POSITION:
      if (eng) snprintf(buf, bufSize, "GenPos:%u", eng->getGenPosition());
      else     snprintf(buf, bufSize, "GenPos:-");
      break;
    case TARGET_SHUFFLE_TEMPLATE:
      if (eng) snprintf(buf, bufSize, "ShufTpl:%u", eng->getShuffleTemplate());
      else     snprintf(buf, bufSize, "ShufTpl:-");
      break;

    // --- MIDI CC/PB (mapping-driven). V1 emits ':?' ; live value comes via
    //     [POT] CCnn= / PB= events in the loop. ---
    case TARGET_MIDI_CC:
      snprintf(buf, bufSize, "CC%u:?", mappingCcNumber); break;
    case TARGET_MIDI_PITCHBEND:
      snprintf(buf, bufSize, "PB:?"); break;

    case TARGET_EMPTY:
    case TARGET_NONE:
    default:
      snprintf(buf, bufSize, "---"); break;
  }
}

// Dump the [STATE] line for a specific bank (any bank, not just foreground).
// At boot, called 8× ; after bank switch, called 1× for the new foreground.
// Plan task A.4.
void dumpBankState(uint8_t bankIdx) {  // non-static : referenced by BankManager via extern
  static const char* TYPE_NAMES[] = { "NORMAL", "ARPEG", "LOOP", "ARPEG_GEN" };
  static const char* SLOT_NAMES[] = { "R1","R1H","R2","R2H","R3","R3H","R4","R4H" };
  static const char* ROOT_NAMES[] = { "A","B","C","D","E","F","G" };
  static const char* MODE_NAMES[] = {
    "Ionian","Dorian","Phrygian","Lydian","Mixolydian","Aeolian","Locrian"
  };

  if (bankIdx >= NUM_BANKS) return;
  BankSlot& b = s_banks[bankIdx];
  uint8_t typeIdx = (uint8_t)b.type;
  const char* typeName = (typeIdx < 4) ? TYPE_NAMES[typeIdx] : "?";
  bool isForeground = (bankIdx == s_bankManager.getCurrentBank());

  Serial.printf("[STATE] bank=%d mode=%s ch=%d",
                bankIdx + 1, typeName, bankIdx + 1);

  // Scale
  if (b.scale.chromatic) {
    Serial.printf(" scale=Chromatic:%s",
                  b.scale.root < 7 ? ROOT_NAMES[b.scale.root] : "?");
  } else {
    Serial.printf(" scale=%s:%s",
                  b.scale.root < 7 ? ROOT_NAMES[b.scale.root] : "?",
                  b.scale.mode < 7 ? MODE_NAMES[b.scale.mode] : "?");
  }

  // Octave (ARPEG) / mutationLevel (ARPEG_GEN)
  if (b.type == BANK_ARPEG && b.arpEngine) {
    Serial.printf(" octave=%d", b.arpEngine->getOctaveRange());
  } else if (b.type == BANK_ARPEG_GEN && b.arpEngine) {
    Serial.printf(" mutationLevel=%d", b.arpEngine->getMutationLevel());
  }

  // 8 slots — LOOP has no PotRouter binding at runtime, emit "---" everywhere.
  if (b.type == BANK_LOOP) {
    for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
      Serial.printf(" %s=---", SLOT_NAMES[i]);
    }
    Serial.println();
    return;
  }

  // Iterate the 8 user-visible slots and ask PotRouter for the effective
  // target wired to (slot, b.type) in the runtime bindings. Consulting
  // bindings (not the mapping store) ensures that ARPEG_GEN slots driven by
  // the PATTERN→GEN_POSITION mirror are correctly reported as `GenPos:N`
  // (not `Pattern:Up`). Future LOOP / other context-substitution bindings
  // will be picked up here without any change to dumpBankState.
  char valBuf[32];
  for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
    uint8_t cc = 0;
    PotTarget t = s_potRouter.getEffectiveTargetForSlot(i, b.type, &cc);
    formatTargetValueForBank(valBuf, sizeof(valBuf), t, bankIdx, cc, isForeground);
    Serial.printf(" %s=%s", SLOT_NAMES[i], valBuf);
  }
  Serial.println();
}

// =================================================================
// Persistent settings dumps (v9, 2026-05-16) — paste-friendly snippets
// pour mettre les valeurs runtime comme defaults en code.
// =================================================================
// Workflow : flash → tune via Tool 7/8 → exit setup (reboot pour que
// NvsManager recharge depuis NVS) → ?ALL via serial monitor → coller
// la sortie pour mise à jour des defaults en code.
// =================================================================

static void dumpLedSettings() {
  const LedSettingsStore& s = s_nvsManager.getLoadedLedSettings();
  Serial.printf("[LED_DUMP v=%u]\n", s.version);
  Serial.printf("  fgIntensity = %u;\n", s.fgIntensity);
  Serial.printf("  breathDepth = %u;\n", s.breathDepth);
  Serial.printf("  tickFlashFg = %u;\n", s.tickFlashFg);
  Serial.printf("  tickFlashBg = %u;\n", s.tickFlashBg);
  Serial.printf("  bgFactor = %u;\n", s.bgFactor);
  Serial.printf("  pulsePeriodMs = %u;\n", s.pulsePeriodMs);
  Serial.printf("  tickBeatDurationMs = %u;\n", s.tickBeatDurationMs);
  Serial.printf("  tickBarDurationMs = %u;\n", s.tickBarDurationMs);
  Serial.printf("  tickWrapDurationMs = %u;\n", s.tickWrapDurationMs);
  Serial.printf("  gammaTenths = %u;\n", s.gammaTenths);
  Serial.printf("  sparkOnMs = %u;\n", s.sparkOnMs);
  Serial.printf("  sparkGapMs = %u;\n", s.sparkGapMs);
  Serial.printf("  sparkCycles = %u;\n", s.sparkCycles);
  Serial.printf("  bankBlinks = %u;\n", s.bankBlinks);
  Serial.printf("  bankDurationMs = %u;\n", s.bankDurationMs);
  Serial.printf("  bankBrightnessPct = %u;\n", s.bankBrightnessPct);
  Serial.printf("  scaleRootBlinks = %u;\n", s.scaleRootBlinks);
  Serial.printf("  scaleRootDurationMs = %u;\n", s.scaleRootDurationMs);
  Serial.printf("  scaleModeBlinks = %u;\n", s.scaleModeBlinks);
  Serial.printf("  scaleModeDurationMs = %u;\n", s.scaleModeDurationMs);
  Serial.printf("  scaleChromBlinks = %u;\n", s.scaleChromBlinks);
  Serial.printf("  scaleChromDurationMs = %u;\n", s.scaleChromDurationMs);
  Serial.printf("  holdOnFadeMs = %u;\n", s.holdOnFadeMs);
  Serial.printf("  holdOffFadeMs = %u;\n", s.holdOffFadeMs);
  Serial.printf("  octaveBlinks = %u;\n", s.octaveBlinks);
  Serial.printf("  octaveDurationMs = %u;\n", s.octaveDurationMs);
  uint8_t overrideCount = 0;
  for (uint8_t i = 0; i < EVT_COUNT; i++) {
    if (s.eventOverrides[i].patternId != PTN_NONE) overrideCount++;
  }
  Serial.printf("  // eventOverrides : %u non-default entries\n", overrideCount);
  for (uint8_t i = 0; i < EVT_COUNT; i++) {
    const EventRenderEntry& e = s.eventOverrides[i];
    if (e.patternId != PTN_NONE) {
      Serial.printf("  eventOverrides[%u] = { .patternId=%u, .colorSlot=%u, .fgPct=%u };\n",
                    i, e.patternId, e.colorSlot, e.fgPct);
    }
  }
  Serial.println("[/LED_DUMP]");
}

static void dumpColorSlots() {
  const ColorSlotStore& s = s_nvsManager.getLoadedColorSlots();
  Serial.printf("[COLORS_DUMP v=%u]\n", s.version);
  for (uint8_t i = 0; i < COLOR_SLOT_COUNT; i++) {
    const ColorSlot& slot = s.slots[i];
    const char* presetName = (slot.presetId < COLOR_PRESET_COUNT)
                               ? COLOR_PRESET_NAMES[slot.presetId] : "?";
    Serial.printf("  slots[%2u] = { .presetId=%2u, .hueOffset=%+4d };  // %s\n",
                  i, slot.presetId, (int)slot.hueOffset, presetName);
  }
  Serial.println("[/COLORS_DUMP]");
}

static void dumpPotMapping() {
  // Order MUST match enum PotTarget in KeyboardData.h (0..18)
  static const char* const TARGET_NAMES[] = {
    "RESPONSE_SHAPE", "SLEW_RATE", "AT_DEADZONE",
    "PITCH_BEND", "GATE_LENGTH", "SHUFFLE_DEPTH",
    "DIVISION", "PATTERN", "SHUFFLE_TEMPLATE",
    "BASE_VELOCITY", "VELOCITY_VARIATION",
    "TEMPO_BPM", "LED_BRIGHTNESS", "PAD_SENSITIVITY",
    "MIDI_CC", "MIDI_PITCHBEND",
    "GEN_POSITION",
    "EMPTY", "NONE"
  };
  constexpr uint8_t TARGET_NAMES_COUNT = sizeof(TARGET_NAMES)/sizeof(TARGET_NAMES[0]);
  static const char* const SLOT_LABELS[] = {"R1 ","R1H","R2 ","R2H","R3 ","R3H","R4 ","R4H"};

  const PotMappingStore& m = s_potRouter.getMapping();
  Serial.printf("[POTMAP_DUMP v=%u]\n", m.version);
  Serial.println("  // NORMAL context");
  for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
    uint8_t t = (uint8_t)m.normalMap[i].target;
    const char* tn = (t < TARGET_NAMES_COUNT) ? TARGET_NAMES[t] : "?";
    Serial.printf("  normalMap[%u] = { TARGET_%-20s, %3u };  // %s\n",
                  i, tn, m.normalMap[i].ccNumber, SLOT_LABELS[i]);
  }
  Serial.println("  // ARPEG context");
  for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
    uint8_t t = (uint8_t)m.arpegMap[i].target;
    const char* tn = (t < TARGET_NAMES_COUNT) ? TARGET_NAMES[t] : "?";
    Serial.printf("  arpegMap[%u]  = { TARGET_%-20s, %3u };  // %s\n",
                  i, tn, m.arpegMap[i].ccNumber, SLOT_LABELS[i]);
  }
  Serial.println("[/POTMAP_DUMP]");
}

// Emit "[READY] current=N" — boot dump completion marker, also at the end
// of each ?STATE/?BANKS/?BOTH/?ALL response. N is the 1-based foreground
// bank index — viewer uses this to set Model.current.idx after the boot's
// 8× [STATE] sequence (which alone doesn't identify the foreground).
static void emitReady() {
  Serial.printf("[READY] current=%u\n", s_bankManager.getCurrentBank() + 1);
}

// Non-blocking Serial poll for viewer-API runtime commands. Recognizes
// ?STATE / ?BANKS / ?BOTH ; each emits the corresponding dump then [READY].
// Plan task A.5.
static void pollRuntimeCommands() {
  static char  cmdBuf[16];
  static uint8_t cmdLen = 0;

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      cmdBuf[cmdLen] = '\0';
      if (strcmp(cmdBuf, "?STATE") == 0) {
        dumpBankState(s_bankManager.getCurrentBank());
        emitReady();
      } else if (strcmp(cmdBuf, "?BANKS") == 0) {
        dumpBanksGlobal();
        emitReady();
      } else if (strcmp(cmdBuf, "?BOTH") == 0) {
        dumpBanksGlobal();
        for (uint8_t i = 0; i < NUM_BANKS; i++) dumpBankState(i);
        emitReady();
      } else if (strcmp(cmdBuf, "?ALL") == 0) {
        dumpBanksGlobal();
        for (uint8_t i = 0; i < NUM_BANKS; i++) dumpBankState(i);
        dumpLedSettings();
        dumpColorSlots();
        dumpPotMapping();
        emitReady();
      }
      cmdLen = 0;
    } else if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    } else {
      // overflow — discard buffer
      cmdLen = 0;
    }
  }
}

#endif  // DEBUG_SERIAL

// =================================================================
// Arduino setup() — runs on Core 1
// =================================================================
void setup() {
  uint32_t bootStartMs = millis();  // True boot time for setup window
  // NOTE: setTxBufferSize() existe sur HWCDC mais PAS sur USBCDC. Ce projet
  // utilise USBCDC (ARDUINO_USB_MODE=0 + ARDUINO_USB_CDC_ON_BOOT=1, TinyUSB
  // composite avec USB MIDI). Le tx ring TinyUSB est dimensionne via
  // CFG_TUD_CDC_TX_BUFSIZE au niveau sdkconfig (hors scope Phase 1.A).
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);       // non-blocking writes — bypass USBCDC default timeout
  delay(800);  // Laisse monter le rail d'alim (cold boot apres longue pause)
  #if DEBUG_SERIAL
  Serial.println();
  Serial.println("[BOOT] === ILLPAD48 V2 ===");
  #endif

  // LEDs first — needed for boot progress and error feedback
  s_leds.begin();
  s_leds.showBootProgress(1);  // Step 1: LED hardware ready
  s_leds.update();

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN, I2C_CLOCK_HZ);
  delay(200);  // Stabilisation 3.3V + power-up MPR121 avant premiere transaction
  s_leds.showBootProgress(2);  // Step 2: I2C bus ready
  s_leds.update();
  #if DEBUG_SERIAL
  Serial.println("[BOOT] I2C OK.");
  #endif

  // Keyboard (4× MPR121) — can fail if I2C bus or sensors are broken
  bool kbOk = s_keyboard.begin();
  if (!kbOk) {
    // [FATAL] event : ALWAYS-on (intentional UNGATED) for diag terrain.
    // Parser-recognized par le viewer → overlay critique. Emis raw avant
    // viewer::begin() (module pas encore cree). Serial.flush garantit
    // emission complete avant la boucle infinie.
    Serial.println("[FATAL] Keyboard init failed");
    Serial.flush();
    s_leds.showBootFailure(3);
    for (;;) { s_leds.update(); delay(10); }
  }
  s_leds.showBootProgress(3);  // Step 3: keyboard OK
  s_leds.update();
  #if DEBUG_SERIAL
  Serial.println("[BOOT] Keyboard OK.");
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
    } else {
      // Save defaults so menu badge shows "ok" (stale blob after version bump)
      PotFilterStore defaults = PotFilter::getConfig();
      NvsManager::saveBlob(POTFILTER_NVS_NAMESPACE, POTFILTER_NVS_KEY,
                           &defaults, sizeof(defaults));
    }
  }

  // =================================================================
  // Setup Mode Detection (hold rear button 3s at boot)
  // Must happen BEFORE sensing task starts (needs direct keyboard access)
  // =================================================================
  {
    #if DEBUG_SERIAL
    Serial.println("[BOOT] Hold rear button to enter setup mode...");
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
                           holdPad, octavePads, &s_potRouter);
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
  Serial.println("[BOOT] MIDI Transport OK.");
  #endif

  // Clock Manager — wire MIDI clock reception callbacks
  s_clockManager.begin(&s_transport);
  s_transport.setClockCallback([](uint8_t src) { s_clockManager.onMidiClockTick(src); });
  #if DEBUG_SERIAL
  Serial.println("[BOOT] ClockManager OK.");
  #endif

  // MIDI Engine
  s_midiEngine.begin(&s_transport);
  #if DEBUG_SERIAL
  Serial.println("[BOOT] MIDI Engine OK.");
  #endif

  // NVS — load all persisted data (overwrites defaults where saved)
  uint8_t currentBank = DEFAULT_BANK;
  // bankPads[], rootPads[], modePads[], chromaticPad, holdPad initialized above (before setup check)

  SettingsStore s_settings;
  s_nvsManager.loadAll(s_banks, currentBank, s_padOrder, bankPads,
                        rootPads, modePads, chromaticPad, holdPad,
                        octavePads, s_potRouter, s_settings);

  // Apply loaded bank
  s_banks[currentBank].isForeground = true;
  s_leds.showBootProgress(5);  // Step 5: NVS loaded
  s_leds.update();
  #if DEBUG_SERIAL
  Serial.printf("[BOOT] NVS loaded. Bank=%d\n", currentBank);
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

  // Assign ArpEngines to ARPEG/ARPEG_GEN banks (after NVS load).
  // setEngineMode derives CLASSIC vs GENERATIVE from BankType ; Phase 5 Task 12 will branch tick().
  {
    uint8_t arpIdx = 0;
    for (uint8_t i = 0; i < NUM_BANKS && arpIdx < 4; i++) {
      if (isArpType(s_banks[i].type)) {
        s_arpEngines[arpIdx].setChannel(i);
        s_arpEngines[arpIdx].setEngineMode(s_banks[i].type);
        s_banks[i].arpEngine = &s_arpEngines[arpIdx];
        arpIdx++;
        #if DEBUG_SERIAL
        Serial.printf("[BOOT] Bank %d: %s, ArpEngine assigned\n", i + 1,
                      s_banks[i].type == BANK_ARPEG_GEN ? "ARPEG_GEN" : "ARPEG");
        #endif
      }
    }
    #if DEBUG_SERIAL
    if (arpIdx == 0) {
      Serial.println("[BOOT] No ARPEG banks configured.");
    }
    #endif
  }

  // ArpScheduler — register engines + set scale/padOrder context
  s_arpScheduler.begin(&s_transport, &s_clockManager);
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (isArpType(s_banks[i].type) && s_banks[i].arpEngine) {
      s_arpScheduler.registerArp(i, s_banks[i].arpEngine);
      s_banks[i].arpEngine->setScaleConfig(s_banks[i].scale);
      s_banks[i].arpEngine->setPadOrder(s_padOrder);

      // Apply NVS-loaded arp params to each engine.
      // ArpPotStore.pattern is dual-semantic (Task 3 plan note) :
      //   - BANK_ARPEG     : ArpPattern enum (0..NUM_ARP_PATTERNS-1)
      //   - BANK_ARPEG_GEN : _genPosition (0..NUM_GEN_POSITIONS-1)
      // Routed via the engine's mode here (already set above by setEngineMode).
      const ArpPotStore& arp = s_nvsManager.getLoadedArpParams(i);
      s_banks[i].arpEngine->setGateLength(max(0.005f, (float)arp.gateRaw / 4095.0f));
      s_banks[i].arpEngine->setShuffleDepth((float)arp.shuffleDepthRaw / 4095.0f);
      s_banks[i].arpEngine->setDivision((ArpDivision)arp.division);
      if (s_banks[i].type == BANK_ARPEG_GEN) {
        s_banks[i].arpEngine->setGenPosition(arp.pattern);
        // Mutation level (= former octaveRange semantic for ARPEG_GEN, spec §7).
        s_banks[i].arpEngine->setMutationLevel(arp.octaveRange);
      } else {
        s_banks[i].arpEngine->setPattern((ArpPattern)arp.pattern);
        s_banks[i].arpEngine->setOctaveRange(arp.octaveRange);
      }
      s_banks[i].arpEngine->setShuffleTemplate(arp.shuffleTemplate);
      s_banks[i].arpEngine->setBaseVelocity(s_banks[i].baseVelocity);
      s_banks[i].arpEngine->setVelocityVariation(s_banks[i].velocityVariation);
      s_banks[i].arpEngine->setStartMode(s_nvsManager.getLoadedQuantizeMode(i));
      // ARPEG_GEN per-bank params (no-op on engines that stay CLASSIC).
      s_banks[i].arpEngine->setBonusPile(s_nvsManager.getLoadedBonusPile(i));
      s_banks[i].arpEngine->setMarginWalk(s_nvsManager.getLoadedMarginWalk(i));
      // V4 Task 22 : per-bank walk tuning Tool 5.
      s_banks[i].arpEngine->setProximityFactor(s_nvsManager.getLoadedProximityFactor(i));
      s_banks[i].arpEngine->setEcart(s_nvsManager.getLoadedEcart(i));
    }
  }
  s_leds.showBootProgress(6);  // Step 6: Arp system ready
  s_leds.update();
  #if DEBUG_SERIAL
  Serial.println("[BOOT] ArpScheduler OK.");
  #endif

  // Give LedController access to bank states for multi-bank display
  s_leds.setBankSlots(s_banks);

  // Store hold pad for ARPEG toggle detection in loop
  s_holdPad = holdPad;

  // Bank Manager
  s_bankManager.begin(&s_midiEngine, &s_leds, s_banks, s_lastKeys, &s_transport);
  s_bankManager.setBankPads(bankPads);
  s_bankManager.setCurrentBank(currentBank);
  s_bankManager.setDoubleTapMs(s_doubleTapMs);
  s_bankManager.setHoldPad(holdPad);
  // Boot-time bank-select notification on canal 16 (USB receivers only —
  // BLE clients receive it on their first connect via panicOnReconnect).
  s_bankManager.emitBankSelectNote();
  #if DEBUG_SERIAL
  Serial.println("[BOOT] BankManager OK.");
  #endif

  // Scale Manager
  s_scaleManager.begin(s_banks, &s_midiEngine, s_lastKeys);
  s_scaleManager.setRootPads(rootPads);
  s_scaleManager.setModePads(modePads);
  s_scaleManager.setChromaticPad(chromaticPad);
  s_scaleManager.setHoldPad(holdPad);
  s_scaleManager.setOctavePads(octavePads);
  #if DEBUG_SERIAL
  Serial.println("[BOOT] ScaleManager OK.");
  #endif

  // Control Pad Manager (Tool 4)
  s_controlPadManager.begin(&s_transport);
  s_controlPadManager.applyStore(s_nvsManager.getLoadedControlPadStore());
  #if DEBUG_SERIAL
  Serial.println("[BOOT] ControlPadManager OK.");
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
  Serial.println("[BOOT] PotFilter + PotRouter OK.");
  #endif

  // NVS Manager — start task (after all loading done)
  s_nvsManager.begin();
  #if DEBUG_SERIAL
  Serial.println("[BOOT] NvsManager OK.");
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
  Serial.println("[BOOT] Ready.");
  viewer::begin();   // Phase 1.A : create queue + task before boot dump
  // Viewer-API boot dump : 1× [BANKS] + 8× [BANK] + 8× [STATE] + 1× [READY].
  // Allows the JUCE viewer to populate its UI from a single boot dump.
  // Plan tasks A.3 + A.4 + A.5 step 3.
  dumpBanksGlobal();
  for (uint8_t i = 0; i < NUM_BANKS; i++) dumpBankState(i);
  emitReady();
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
    if (s_controlPadManager.isControlPad(i)) continue;
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
  for (int i = 0; i < NUM_KEYS; i++) {
    if (i == s_holdPad) continue;  // Hold pad is never a music pad
    if (s_controlPadManager.isControlPad(i)) continue;

    bool pressed    = state.keyIsPressed[i];
    bool wasPressed = s_lastKeys[i];
    uint8_t pos = s_padOrder[i];

    if (pressed && !wasPressed) {
      if (slot.arpEngine->isCaptured()) {
        // Play: double-tap removes, single tap adds to persistent pile
        if (s_lastPressTime[i] > 0 &&
            (now - s_lastPressTime[i]) < (uint32_t)s_doubleTapMs) {
          slot.arpEngine->removePadPosition(pos);
          s_lastPressTime[i] = 0;
        } else {
          slot.arpEngine->addPadPosition(pos);
          s_lastPressTime[i] = now;
        }
      } else {
        // Stop: 1er press musical wipe la paused pile (si non vide), réactive
        // Play automatiquement, ajoute la note. Cf spec gesture §13.2 amendée
        // 2026-05-15 (Option 3) : Stop est une commande momentanée, un geste
        // musical post-Stop re-engage Play. La pile précédente est wipée pour
        // permettre "table rase" — pour conserver la pile, le user doit Play
        // via hold pad / double-tap bank pad (toggle explicite).
        // LED : EVT_PLAY pour signaler le passage automatique en Play,
        // cohérent avec handleHoldPad et BankManager double-tap.
        if (slot.arpEngine->isPaused() && slot.arpEngine->hasNotes()) {
          slot.arpEngine->clearAllNotes(s_transport);
        }
        slot.arpEngine->setCaptured(true, s_transport, nullptr, s_holdPad);
        s_leds.triggerEvent(EVT_PLAY);
        slot.arpEngine->addPadPosition(pos);
      }

    } else if (!pressed && wasPressed) {
      // Stop comme Play : release ignoré, pile persistante (fix F8 du
      // 2026-05-15). Le "live remove" en Stop est caduque depuis le fix
      // §13.2 auto-Play (4799918) : un press musical en Stop transite
      // immédiatement vers Play, donc on ne reste plus jamais "en Stop
      // avec doigts sur les pads en live mode". La branche destructive
      // était activée seulement à la transition Play→Stop avec pads
      // tenus, scenario qui n'a aucune sémantique musicale légitime.
      (void)pos;
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
        if (s_controlPadManager.isControlPad(i)) continue;
        if (!state.keyIsPressed[i]) {
          s_midiEngine.noteOff(i);
        }
      }
    }
    // ARPEG-OFF : pas de sweep au release LEFT (pile sacrée Q3, spec gesture §9).
    // Ce sweep historique itérait les 48 pads et retirait de la pile tout pad
    // non pressé physiquement au release LEFT — provoquant un wipe complet de
    // la pile à chaque cycle LEFT press/release. Supprimé suite au diagnostic
    // 2026-05-15 (bug "pile s'efface au LEFT release").
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
      case BANK_ARPEG_GEN:
        if (slot.arpEngine) processArpMode(state, slot, now);
        break;
      default:
        // BANK_LOOP : Phase 1 LOOP wires processLoopMode here
        break;
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
  uint8_t genPos = 5;  // ARPEG_GEN default (spec §13)
  if (isArpType(newSlot.type) && newSlot.arpEngine) {
    gate      = newSlot.arpEngine->getGateLength();
    shufDepth = newSlot.arpEngine->getShuffleDepth();
    div       = newSlot.arpEngine->getDivision();
    pat       = newSlot.arpEngine->getPattern();
    shufTmpl  = newSlot.arpEngine->getShuffleTemplate();
    genPos    = newSlot.arpEngine->getGenPosition();
  }

  s_potRouter.loadStoredPerBank(
    newSlot.baseVelocity, newSlot.velocityVariation, newSlot.pitchBendOffset,
    gate, shufDepth, div, pat, shufTmpl, genPos
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

  // Queue NVS save on scale change + sync arp engine scale + LED confirmation.
  // Scale group propagation : si la banque courante appartient a un groupe,
  // toutes les autres banques du groupe recoivent la meme ScaleConfig.
  if (scaleChanged) {
    uint8_t bank = s_bankManager.getCurrentBank();
    BankSlot& scSlot = s_bankManager.getCurrentSlot();

    // Save + sync arp pour la banque courante
    s_nvsManager.queueScaleWrite(bank, scSlot.scale);
    if (isArpType(scSlot.type) && scSlot.arpEngine) {
      scSlot.arpEngine->setScaleConfig(scSlot.scale);
    }

    // Propagation aux membres du meme groupe + bitmask pour LED confirmation
    uint8_t group = s_nvsManager.getLoadedScaleGroup(bank);
    uint8_t ledMask = (uint8_t)(1 << bank);
    if (group > 0) {
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        if (i == bank) continue;
        if (s_nvsManager.getLoadedScaleGroup(i) != group) continue;
        s_banks[i].scale = scSlot.scale;
        s_nvsManager.queueScaleWrite(i, scSlot.scale);
        if (isArpType(s_banks[i].type) && s_banks[i].arpEngine) {
          s_banks[i].arpEngine->setScaleConfig(scSlot.scale);
        }
        ledMask |= (uint8_t)(1 << i);
      }
    }

    EventId evt = (scaleChange == SCALE_CHANGE_ROOT) ? EVT_SCALE_ROOT
                : (scaleChange == SCALE_CHANGE_MODE) ? EVT_SCALE_MODE
                : EVT_SCALE_CHROM;
    s_leds.triggerEvent(evt, ledMask);
  }

  // Queue NVS save on octave change + LED confirmation
  if (octaveChanged) {
    uint8_t bank = s_bankManager.getCurrentBank();
    uint8_t newOct = s_scaleManager.getNewOctaveRange();
    s_nvsManager.queueArpOctaveWrite(bank, newOct);
    s_leds.triggerEvent(EVT_OCTAVE);
  }

  // Clock: process ticks (PLL + tick generation)
  s_clockManager.update();

  return bankSwitched;
}

// --- Toggle play/stop globalement sur toutes les banks ARPEG/ARPEG_GEN ---
// Geste LEFT held + hold pad simple tap (spec gesture §10 amendée 2026-05-15).
// Comportement symétrique :
// - Au moins une bank en Play → Stop sur toutes les Play (pile préservée
//   via nullptr, branche "no fingers" de setCaptured).
// - Toutes en Stop avec paused pile non vide → Play sur toutes (relaunch).
// - Sinon (toutes vides ou Stop sans paused) → no-op silencieux.
// LED : EVT_PLAY ou EVT_STOP avec mask multi-bank (1 trigger pour toutes).
// Futur LOOP : étendre la boucle pour inclure isLoopType + LoopEngine.
static void toggleAllArps() {
  bool anyPlaying = false;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (isArpType(s_banks[i].type) && s_banks[i].arpEngine
        && s_banks[i].arpEngine->isCaptured()) {
      anyPlaying = true;
      break;
    }
  }
  uint8_t mask = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (!isArpType(s_banks[i].type) || !s_banks[i].arpEngine) continue;
    if (anyPlaying && s_banks[i].arpEngine->isCaptured()) {
      // Stop : nullptr → branche "no fingers" → pile préservée (Q3)
      s_banks[i].arpEngine->setCaptured(false, s_transport, nullptr, s_holdPad);
      mask |= (uint8_t)(1 << i);
    } else if (!anyPlaying && s_banks[i].arpEngine->isPaused()
                              && s_banks[i].arpEngine->hasNotes()) {
      // Play : relaunch chaque paused pile non vide
      s_banks[i].arpEngine->setCaptured(true, s_transport, nullptr, s_holdPad);
      mask |= (uint8_t)(1 << i);
    }
  }
  if (mask != 0) s_leds.triggerEvent(anyPlaying ? EVT_STOP : EVT_PLAY, mask);
}

// --- Hold pad edge detection (ARPEG OFF/ON switch, always exposed) ---
// Sans LEFT : toggle FG bank (comportement classique).
// Avec LEFT : toggle global toutes banks (cf toggleAllArps).
static void handleHoldPad(const SharedKeyboardState& state, bool leftHeld) {
  static bool s_lastHoldPadState = false;
  if (s_holdPad >= NUM_KEYS) { s_lastHoldPadState = false; return; }

  bool pressed = state.keyIsPressed[s_holdPad];
  bool risingEdge = pressed && !s_lastHoldPadState;
  s_lastHoldPadState = pressed;
  if (!risingEdge) return;

  if (leftHeld) {
    // LEFT + hold pad = scope étendu (toutes banks)
    toggleAllArps();
    return;
  }

  // Hors LEFT : toggle FG uniquement, requiert FG ARPEG/ARPEG_GEN
  BankSlot& slot = s_bankManager.getCurrentSlot();
  if (!isArpType(slot.type) || !slot.arpEngine) return;

  bool wasCaptured = slot.arpEngine->isCaptured();
  slot.arpEngine->setCaptured(!wasCaptured, s_transport, state.keyIsPressed, s_holdPad);
  s_leds.triggerEvent(slot.arpEngine->isCaptured() ? EVT_PLAY : EVT_STOP);
  if (slot.arpEngine->isCaptured()) {
    memset(s_lastPressTime, 0, sizeof(s_lastPressTime));
  }
}

// --- Push live pot params to engine (extracted for LOOP extensibility) ---
// Phase 6 Task 15 : branche setPattern (ARPEG) vs setGenPosition (ARPEG_GEN) selon _engineMode.
// Tous les autres setters restent communs (gate, shuffle, division, template, velocity).
static void pushParamsToEngine(BankSlot& slot) {
  if (!isArpType(slot.type) || !slot.arpEngine) return;

  slot.arpEngine->setGateLength(s_potRouter.getGateLength());
  slot.arpEngine->setShuffleDepth(s_potRouter.getShuffleDepth());
  slot.arpEngine->setDivision(s_potRouter.getDivision());
  slot.arpEngine->setShuffleTemplate(s_potRouter.getShuffleTemplate());
  slot.arpEngine->setBaseVelocity(s_potRouter.getBaseVelocity());
  slot.arpEngine->setVelocityVariation(s_potRouter.getVelocityVariation());

  if (slot.arpEngine->getEngineMode() == EngineMode::GENERATIVE) {
    slot.arpEngine->setGenPosition(s_potRouter.getGenPosition());
  } else {
    slot.arpEngine->setPattern(s_potRouter.getPattern());
  }
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

  // Global params: Core 0 atomics
  s_responseShape.store(s_potRouter.getResponseShape(), std::memory_order_relaxed);
  s_slewRate.store(s_potRouter.getSlewRate(), std::memory_order_relaxed);
  s_padSensitivity.store(s_potRouter.getPadSensitivity(), std::memory_order_relaxed);

  // Non-musical params
  s_leds.setBrightness(s_potRouter.getLedBrightness());

  // MIDI CC/PB from user-assigned pot mappings.
  // Viewer-API Task A.2.b : emit a [POT] log on each CC/PB tweak so the
  // viewer learns the live value (previously silent — the viewer was blind
  // to pots mapped to MIDI CC/PB).
  {
    BankType ccCurType = s_banks[s_bankManager.getCurrentBank()].type;

    uint8_t ccNum, ccVal;
    while (s_potRouter.consumeCC(ccNum, ccVal)) {
      s_transport.sendCC(potSlot.channel, ccNum, ccVal);
      #if DEBUG_SERIAL
      Serial.printf("[POT] %s: CC%u=%u\n",
                    potSlotName(s_potRouter.getSlotForCcNumber(ccNum, ccCurType)),
                    ccNum, ccVal);
      #endif
    }
    uint16_t pbVal;
    if (s_potRouter.consumePitchBend(pbVal)) {
      s_transport.sendPitchBend(potSlot.channel, pbVal);
      #if DEBUG_SERIAL
      Serial.printf("[POT] %s: PB=%u\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_MIDI_PITCHBEND, ccCurType)),
                    pbVal);
      #endif
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
    static uint8_t  s_dbgGenPos   = 0xFF;
    static uint8_t  s_dbgShufTpl  = 0xFF;

    // First call after boot forces emission of every param regardless of
    // its initial value — same pattern as the s_hwInit gate below. Guards
    // against sentinel collision (e.g. LED_Bright=255 == uint8_t 0xFF init)
    // and gives the viewer a complete state dump in the [READY] window
    // without requiring the user to wiggle each pot.
    static bool s_firstEmit = true;

    static const char* s_divNames[] = {"4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/32","1/64"};
    static const char* s_patNames[] = {
      "Up","Down","UpDown","Order","PedalUp","Converge"
    };

    // Foreground bank type — passed directly to PotRouter slot lookups.
    // The runtime bindings carry per-BankType resolution (incl. ARPEG_GEN
    // two-binding mirror), so we don't collapse to a bool isArpContext.
    BankType curType = s_banks[s_bankManager.getCurrentBank()].type;

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
    uint8_t  genPos  = s_potRouter.getGenPosition();
    uint8_t  shufTpl = s_potRouter.getShuffleTemplate();

    // Global params — slot prefix derived from current mapping & context.
    // Format: "[POT] <SLOT>: <param>=<value> [unit]" (viewer-API §A.2).
    if (s_firstEmit || (int)(shape * 100) != (int)(s_dbgShape * 100)) {
      Serial.printf("[POT] %s: Shape=%.2f\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_RESPONSE_SHAPE, curType)), shape);
      s_dbgShape = shape;
    }
    if (s_firstEmit || slew != s_dbgSlew) {
      Serial.printf("[POT] %s: Slew=%u\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_SLEW_RATE, curType)), slew);
      s_dbgSlew = slew;
    }
    if (s_firstEmit || atDz != s_dbgAtDz) {
      Serial.printf("[POT] %s: AT_Deadzone=%u\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_AT_DEADZONE, curType)), atDz);
      s_dbgAtDz = atDz;
    }
    if (s_firstEmit || tempo != s_dbgTempo) {
      Serial.printf("[POT] %s: Tempo=%u BPM\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_TEMPO_BPM, curType)), tempo);
      s_dbgTempo = tempo;
    }
    if (s_firstEmit || ledBr != s_dbgLedBr) {
      Serial.printf("[POT] %s: LED_Bright=%u\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_LED_BRIGHTNESS, curType)), ledBr);
      s_dbgLedBr = ledBr;
    }
    if (s_firstEmit || padSens != s_dbgPadSens) {
      Serial.printf("[POT] %s: PadSens=%u\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_PAD_SENSITIVITY, curType)), padSens);
      s_dbgPadSens = padSens;
    }

    // Per-bank params (always tracked, foreground bank)
    if (s_firstEmit || baseVel != s_dbgBaseVel) {
      Serial.printf("[POT] %s: BaseVel=%u\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_BASE_VELOCITY, curType)), baseVel);
      s_dbgBaseVel = baseVel;
    }
    if (s_firstEmit || velVar != s_dbgVelVar) {
      Serial.printf("[POT] %s: VelVar=%u\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_VELOCITY_VARIATION, curType)), velVar);
      s_dbgVelVar = velVar;
    }
    if (s_firstEmit || pb != s_dbgPB) {
      Serial.printf("[POT] %s: PitchBend=%u\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_PITCH_BEND, curType)), pb);
      s_dbgPB = pb;
    }

    // Arp params
    if (s_firstEmit || (int)(gate * 100) != (int)(s_dbgGate * 100)) {
      Serial.printf("[POT] %s: Gate=%.2f\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_GATE_LENGTH, curType)), gate);
      s_dbgGate = gate;
    }
    if (s_firstEmit || (int)(shufDep * 100) != (int)(s_dbgShufDep * 100)) {
      Serial.printf("[POT] %s: ShufDepth=%.2f\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_SHUFFLE_DEPTH, curType)), shufDep);
      s_dbgShufDep = shufDep;
    }
    if (s_firstEmit || div != s_dbgDiv) {
      Serial.printf("[POT] %s: Division=%s\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_DIVISION, curType)),
                    div < 9 ? s_divNames[div] : "?");
      s_dbgDiv = div;
    }
    if (s_firstEmit || pat != s_dbgPat) {
      Serial.printf("[POT] %s: Pattern=%s\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_PATTERN, curType)),
                    pat < NUM_ARP_PATTERNS ? s_patNames[pat] : "?");
      s_dbgPat = pat;
    }
    // V4 Task 22 : R2+hold sur ARPEG_GEN pilote _genPosition (TARGET_GEN_POSITION binding),
    // distinct de _pattern. Trace pour observabilite live du sweep 0..NUM_GEN_POSITIONS-1.
    if (s_firstEmit || genPos != s_dbgGenPos) {
      Serial.printf("[POT] %s: GenPos=%u\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_GEN_POSITION, curType)), genPos);
      s_dbgGenPos = genPos;
    }
    if (s_firstEmit || shufTpl != s_dbgShufTpl) {
      Serial.printf("[POT] %s: ShufTpl=%u\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_SHUFFLE_TEMPLATE, curType)), shufTpl);
      s_dbgShufTpl = shufTpl;
    }

    s_firstEmit = false;
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
  #if DEBUG_SERIAL
  // Viewer-API runtime command poll (?STATE/?BANKS/?BOTH). Non-blocking,
  // cheap (Serial.available() == 0 path is just a register read).
  pollRuntimeCommands();
  viewer::pollCommands();   // Phase 1.A : stub. Will replace pollRuntimeCommands in 1.D.
  #endif

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

  // --- Read buttons (active LOW) with 20ms software debounce ---
  // Defense-in-depth alongside the HW RC debounce: covers a faulty cap
  // or worn switch contact. 20ms latency is imperceptible for a button.
  static uint32_t s_leftChangeMs = 0, s_rearChangeMs = 0;
  static bool s_leftRaw = false, s_rearRaw = false;
  static bool s_leftStable = false, s_rearStable = false;
  {
    bool raw = (digitalRead(BTN_LEFT_PIN) == LOW);
    if (raw != s_leftRaw) { s_leftRaw = raw; s_leftChangeMs = millis(); }
    if (s_leftRaw != s_leftStable && (millis() - s_leftChangeMs) >= 20)
      s_leftStable = s_leftRaw;
  }
  {
    bool raw = (digitalRead(BTN_REAR_PIN) == LOW);
    if (raw != s_rearRaw) { s_rearRaw = raw; s_rearChangeMs = millis(); }
    if (s_rearRaw != s_rearStable && (millis() - s_rearChangeMs) >= 20)
      s_rearStable = s_rearRaw;
  }
  bool leftHeld = s_leftStable;
  bool rearHeld = s_rearStable;

  // --- CRITICAL PATH ---
  handleManagerUpdates(state, leftHeld);

  handleHoldPad(state, leftHeld);

  // --- Control pads (step 7b): after bank switch resolution + hold pad,
  //     before music block. Emits CC and handles LEFT/bank edges.
  s_controlPadManager.update(state, leftHeld,
                             s_bankManager.getCurrentSlot().channel);

  handlePadInput(state, now);

  // Always sync edge state — prevents ghost notes when button releases
  for (int i = 0; i < NUM_KEYS; i++) {
    s_lastKeys[i] = state.keyIsPressed[i];
  }

  // --- ArpScheduler: dispatch clock ticks to all active arps ---
  s_arpScheduler.tick();
  s_arpScheduler.processEvents();  // Fire pending gate noteOff + shuffled noteOn

  // --- CRITICAL PATH END ---
  s_midiEngine.flush();

  // --- SECONDARY: Pots + params + CC/PB + bargraph + battery + LEDs ---
  handlePotPipeline(leftHeld, rearHeld);

  handlePanicChecks(now, rearHeld);

  // --- NVS: pot debounce (rear=2s, right=10s) + signal task ---
  bool rearDirty  = s_potRouter.isRearDirty();
  bool rightDirty = s_potRouter.isRightDirty();
  s_nvsManager.tickPotDebounce(now, rearDirty, rightDirty, s_potRouter,
                                s_bankManager.getCurrentBank(), s_bankManager.getCurrentSlot().type);
  if (rearDirty)  s_potRouter.clearRearDirty();
  if (rightDirty) s_potRouter.clearRightDirty();

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
