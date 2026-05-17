// src/viewer/ViewerSerial.cpp
#include "ViewerSerial.h"
#include "../core/HardwareConfig.h"  // DEBUG_SERIAL
#include "../core/KeyboardData.h"    // BankSlot, BankType, NUM_BANKS, PotTarget enums
#include "../arp/ArpEngine.h"
#include "../managers/PotRouter.h"
#include "../managers/NvsManager.h"
#include "../managers/BankManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdarg.h>
#include <stdio.h>

// Forward declarations of firmware globals (defined in main.cpp)
extern BankSlot     s_banks[NUM_BANKS];
extern PotRouter    s_potRouter;
extern NvsManager   s_nvsManager;
extern BankManager  s_bankManager;

namespace viewer {

namespace {

// Event in queue : 1 byte prio + 1 byte len + 254 bytes payload = 256 bytes
struct QueuedEvent {
  uint8_t prio;
  uint8_t len;
  char    line[254];
};

// Queue sizing per spec §6 : 32 slots × 256 bytes = 8 KB total
constexpr UBaseType_t QUEUE_DEPTH      = 32;
constexpr UBaseType_t TASK_PRIORITY    = 0;     // idle priority — safe vs main loop prio 1
constexpr uint32_t    TASK_STACK_BYTES = 4096;  // 4 KB stack — convention projet (cf. NvsManager.cpp:170, main.cpp:834)
constexpr BaseType_t  TASK_CORE        = 1;     // Core 1 (Core 0 saturé par sensingTask)

QueueHandle_t       s_queue           = nullptr;
TaskHandle_t        s_task            = nullptr;
std::atomic<bool>   s_viewerConnected{false};

// Internal emit : format ligne complete + push queue avec backpressure.
// Drop si viewer absent, queue pleine (HIGH), ou backpressure 70% (LOW).
void emit(Priority prio, const char* fmt, ...) {
  if (!s_viewerConnected.load(std::memory_order_acquire)) return;
  if (!s_queue) return;

  QueuedEvent ev;
  ev.prio = (uint8_t)prio;

  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(ev.line, sizeof(ev.line), fmt, args);
  va_end(args);
  if (n <= 0) return;
  if (n > (int)sizeof(ev.line)) n = sizeof(ev.line);
  ev.len = (uint8_t)n;

  // Backpressure : si LOW et moins de 30% libre, drop
  UBaseType_t freeSlots = uxQueueSpacesAvailable(s_queue);
  if (prio == PRIO_LOW && freeSlots < (QUEUE_DEPTH * 3 / 10)) return;

  xQueueSend(s_queue, &ev, 0);  // timeout 0 = drop si pleine
}

void taskBody(void* /*arg*/) {
  QueuedEvent ev;
  for (;;) {
    bool nowConnected = (bool)Serial;
    bool wasConnected = s_viewerConnected.exchange(nowConnected, std::memory_order_acq_rel);

    if (!wasConnected && nowConnected) {
      // Auto-resync : viewer vient de connecter (cold open apres firmware
      // deja boote). Re-emit le boot dump complet pour populer le viewer.
      // Anticipe Phase 1.D (qui ajoutera emitGlobals/emitSettings ici).
      // Push les events dans la queue — ils seront draines par les iters
      // suivants de cette meme task.
      emitBanksHeader(NUM_BANKS);
      for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
      for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
      emitReady(s_bankManager.getCurrentBank() + 1);
    }

    if (!nowConnected) {
      // Viewer absent — drain silently
      while (xQueueReceive(s_queue, &ev, 0) == pdPASS) { /* discard */ }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Drain with 10ms wait — if no event in 10ms, loop and re-check connection
    if (xQueueReceive(s_queue, &ev, pdMS_TO_TICKS(10)) == pdPASS) {
      const uint8_t* buf = reinterpret_cast<const uint8_t*>(ev.line);
      // Wait jusqu'a 100ms pour que le TX ring USB CDC se vide. Le default
      // TinyUSB ring est ~256B et un [STATE] line fait ~200B — sans wait,
      // le boot dump (18 events ×200B = 3.6KB) sature et drop. Avec wait,
      // le host (Mac) draine continument et le boot dump passe en ~50-200ms.
      uint32_t deadline = millis() + 100;
      while (Serial.availableForWrite() < ev.len &&
             (int32_t)(deadline - millis()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(2));
      }
      if (Serial.availableForWrite() >= ev.len) {
        Serial.write(buf, ev.len);
      }
      // else : drop apres 100ms wait (CDC stalled severement, viewer freeze)
    }
  }
}

// Format the "target:value" string for a given target, reading from the
// per-bank storage (s_banks[bankIdx] / arpEngine) for per-bank values, or
// from PotRouter for global values. Helper for emitState().
void formatTargetValueForBank(char* buf, size_t bufSize,
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

}  // namespace

void begin() {
  s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(QueuedEvent));
  if (!s_queue) {
    // [FATAL] always-on (parser-recognized). Le module ne pourra rien emettre
    // ensuite (s_queue null = tous les emit() return immediatement). Le firmware
    // reste fonctionnel cote MIDI/LED/jeu live, juste invisible au viewer.
    Serial.println("[FATAL] viewer queue create failed");
    Serial.flush();
    return;
  }
  // NOTE: xTaskCreatePinnedToCore sur Arduino ESP32 attend la stack size **en bytes**
  // (le wrapper convertit vers words côté IDF). Cf. NvsManager.cpp:170 et main.cpp:834
  // qui passent 4096 directement. NE PAS diviser par sizeof(StackType_t).
  BaseType_t taskOk = xTaskCreatePinnedToCore(
    taskBody, "viewer",
    TASK_STACK_BYTES,                  // 4096 bytes direct
    nullptr, TASK_PRIORITY, &s_task, TASK_CORE);
  if (taskOk != pdPASS) {
    Serial.println("[FATAL] viewer task create failed");
    Serial.flush();
  }
  // Seed atomic synchronement avant que begin() retourne. Sinon race au boot :
  // le premier debugOutput() (loop iter #1) ou le boot dump appelle viewer::emit*()
  // avant que la task drain n'ait eu son premier slot CPU → s_viewerConnected
  // encore false → events droppes silencieusement. Phase 1.C.2 a amplifie ce bug
  // car TOUT le boot dump passe maintenant par le module. La task continue de
  // updater l'atomic sur changes ulterieurs (disconnect/reconnect runtime).
  s_viewerConnected.store((bool)Serial, std::memory_order_release);
}

void pollCommands() {
  // Phase 1.A : stub. Phase 1.D migrera pollRuntimeCommands ici.
  // L'existant pollRuntimeCommands() reste dans main.cpp pendant Phase 1.A-1.C.
}

bool isConnected() {
  return s_viewerConnected.load(std::memory_order_acquire);
}

void emitPot(const char* slot, const char* target, const char* valueStr, const char* unit) {
  #if DEBUG_SERIAL
  if (unit && unit[0] != '\0') {
    emit(PRIO_LOW, "[POT] %s: %s=%s %s\n", slot, target, valueStr, unit);
  } else {
    emit(PRIO_LOW, "[POT] %s: %s=%s\n", slot, target, valueStr);
  }
  #else
  (void)slot; (void)target; (void)valueStr; (void)unit;
  #endif
}

// =================================================================
// Phase 1.C.2 : [BANK]/[STATE]/[READY] events
// Migrated from main.cpp dumpBanksGlobal/dumpBankState/emitReady.
// Format strictly identical — JUCE viewer parser unchanged.
// =================================================================

void emitBanksHeader(uint8_t count) {
  #if DEBUG_SERIAL
  emit(PRIO_HIGH, "[BANKS] count=%u\n", count);
  #else
  (void)count;
  #endif
}

void emitBank(uint8_t idx) {
  #if DEBUG_SERIAL
  // ORDER MATCHES enum BankType in KeyboardData.h:324-329
  // BANK_NORMAL=0, BANK_ARPEG=1, BANK_LOOP=2, BANK_ARPEG_GEN=3
  static const char* TYPE_NAMES[] = { "NORMAL", "ARPEG", "LOOP", "ARPEG_GEN" };
  static const char* DIV_NAMES[]  = { "4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/32","1/64" };

  if (idx >= NUM_BANKS) return;
  BankSlot& b = s_banks[idx];
  uint8_t group = s_nvsManager.getLoadedScaleGroup(idx);
  char groupChar = (group == 0) ? '0' : (char)('A' + group - 1);
  uint8_t typeIdx = (uint8_t)b.type;
  const char* typeName = (typeIdx < 4) ? TYPE_NAMES[typeIdx] : "?";

  char line[256];
  int n = snprintf(line, sizeof(line), "[BANK] idx=%d type=%s ch=%d group=%c",
                   idx + 1, typeName, idx + 1, groupChar);
  if (n <= 0 || n >= (int)sizeof(line)) return;

  bool isArp = (b.type == BANK_ARPEG) || (b.type == BANK_ARPEG_GEN);
  if (isArp && b.arpEngine) {
    ArpDivision d = b.arpEngine->getDivision();
    uint8_t dIdx = (uint8_t)d;
    int m = snprintf(line + n, sizeof(line) - n, " division=%s playing=%s",
                     dIdx < 9 ? DIV_NAMES[dIdx] : "?",
                     b.arpEngine->isPlaying() ? "true" : "false");
    if (m > 0 && m < (int)(sizeof(line) - n)) n += m;
    if (b.type == BANK_ARPEG) {
      m = snprintf(line + n, sizeof(line) - n, " octave=%d", b.arpEngine->getOctaveRange());
    } else { // ARPEG_GEN
      m = snprintf(line + n, sizeof(line) - n, " mutationLevel=%d", b.arpEngine->getMutationLevel());
    }
    if (m > 0 && m < (int)(sizeof(line) - n)) n += m;
  }
  // Append trailing newline (drop final char if buffer would overflow)
  if (n < (int)(sizeof(line) - 1)) {
    line[n++] = '\n';
    line[n] = '\0';
  } else {
    line[sizeof(line) - 2] = '\n';
    line[sizeof(line) - 1] = '\0';
  }
  emit(PRIO_HIGH, "%s", line);
  #else
  (void)idx;
  #endif
}

void emitState(uint8_t bankIdx) {
  #if DEBUG_SERIAL
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

  char line[384];
  int n = snprintf(line, sizeof(line), "[STATE] bank=%d mode=%s ch=%d",
                   bankIdx + 1, typeName, bankIdx + 1);
  if (n <= 0 || n >= (int)sizeof(line)) return;

  // Scale
  int m;
  if (b.scale.chromatic) {
    m = snprintf(line + n, sizeof(line) - n, " scale=Chromatic:%s",
                 b.scale.root < 7 ? ROOT_NAMES[b.scale.root] : "?");
  } else {
    m = snprintf(line + n, sizeof(line) - n, " scale=%s:%s",
                 b.scale.root < 7 ? ROOT_NAMES[b.scale.root] : "?",
                 b.scale.mode < 7 ? MODE_NAMES[b.scale.mode] : "?");
  }
  if (m > 0 && m < (int)(sizeof(line) - n)) n += m;

  // Octave (ARPEG) / mutationLevel (ARPEG_GEN)
  if (b.type == BANK_ARPEG && b.arpEngine) {
    m = snprintf(line + n, sizeof(line) - n, " octave=%d", b.arpEngine->getOctaveRange());
    if (m > 0 && m < (int)(sizeof(line) - n)) n += m;
  } else if (b.type == BANK_ARPEG_GEN && b.arpEngine) {
    m = snprintf(line + n, sizeof(line) - n, " mutationLevel=%d", b.arpEngine->getMutationLevel());
    if (m > 0 && m < (int)(sizeof(line) - n)) n += m;
  }

  // 8 slots — LOOP has no PotRouter binding at runtime, emit "---" everywhere.
  if (b.type == BANK_LOOP) {
    for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
      m = snprintf(line + n, sizeof(line) - n, " %s=---", SLOT_NAMES[i]);
      if (m > 0 && m < (int)(sizeof(line) - n)) n += m;
    }
  } else {
    // Iterate the 8 user-visible slots and ask PotRouter for the effective
    // target wired to (slot, b.type) in the runtime bindings. Consulting
    // bindings (not the mapping store) ensures that ARPEG_GEN slots driven by
    // the PATTERN→GEN_POSITION mirror are correctly reported as `GenPos:N`
    // (not `Pattern:Up`). Future LOOP / other context-substitution bindings
    // will be picked up here without any change to emitState.
    char valBuf[32];
    for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
      uint8_t cc = 0;
      PotTarget t = s_potRouter.getEffectiveTargetForSlot(i, b.type, &cc);
      formatTargetValueForBank(valBuf, sizeof(valBuf), t, bankIdx, cc, isForeground);
      m = snprintf(line + n, sizeof(line) - n, " %s=%s", SLOT_NAMES[i], valBuf);
      if (m > 0 && m < (int)(sizeof(line) - n)) n += m;
    }
  }

  // Append trailing newline
  if (n < (int)(sizeof(line) - 1)) {
    line[n++] = '\n';
    line[n] = '\0';
  } else {
    line[sizeof(line) - 2] = '\n';
    line[sizeof(line) - 1] = '\0';
  }
  emit(PRIO_HIGH, "%s", line);
  #else
  (void)bankIdx;
  #endif
}

void emitReady(uint8_t currentBank1Based) {
  #if DEBUG_SERIAL
  emit(PRIO_HIGH, "[READY] current=%u\n", currentBank1Based);
  #else
  (void)currentBank1Based;
  #endif
}

void emitBankSwitch(uint8_t newBankIdx) {
  #if DEBUG_SERIAL
  static const char* TYPE_NAMES[] = { "NORMAL", "ARPEG", "LOOP", "ARPEG_GEN" };
  if (newBankIdx >= NUM_BANKS) return;
  BankSlot& b = s_banks[newBankIdx];
  uint8_t typeIdx = (uint8_t)b.type;
  const char* typeName = (typeIdx < 4) ? TYPE_NAMES[typeIdx] : "?";
  emit(PRIO_HIGH, "[BANK] Bank %d (ch %d, %s)\n", newBankIdx + 1, newBankIdx + 1, typeName);
  emitState(newBankIdx);
  #else
  (void)newBankIdx;
  #endif
}

// =================================================================
// Phase 1.C.3 : [ARP]/[GEN] events
// Migrated from src/arp/ArpEngine.cpp. Format strictly identical —
// em-dash (\xe2\x80\x94 = U+2014) preserved exactly for parser
// recognition (spec §3.6).
// =================================================================

void emitArpNoteAdd(uint8_t bankIdx, uint8_t pileCount) {
  #if DEBUG_SERIAL
  emit(PRIO_LOW, "[ARP] Bank %d: +note (%d total)\n", bankIdx + 1, pileCount);
  #else
  (void)bankIdx; (void)pileCount;
  #endif
}

void emitArpNoteRemove(uint8_t bankIdx, uint8_t pileCount) {
  #if DEBUG_SERIAL
  emit(PRIO_LOW, "[ARP] Bank %d: -note (%d total)\n", bankIdx + 1, pileCount);
  #else
  (void)bankIdx; (void)pileCount;
  #endif
}

void emitArpPlay(uint8_t bankIdx, uint8_t pileCount, bool relaunchPaused) {
  #if DEBUG_SERIAL
  if (relaunchPaused) {
    emit(PRIO_HIGH, "[ARP] Bank %d: Play \xe2\x80\x94 relaunch paused pile (%d notes)\n",
         bankIdx + 1, pileCount);
  } else {
    emit(PRIO_HIGH, "[ARP] Bank %d: Play (pile %d notes)\n", bankIdx + 1, pileCount);
  }
  #else
  (void)bankIdx; (void)pileCount; (void)relaunchPaused;
  #endif
}

void emitArpStop(uint8_t bankIdx, uint8_t pileCount) {
  #if DEBUG_SERIAL
  emit(PRIO_HIGH, "[ARP] Bank %d: Stop \xe2\x80\x94 pile kept (%d notes)\n",
       bankIdx + 1, pileCount);
  #else
  (void)bankIdx; (void)pileCount;
  #endif
}

void emitArpQueueFull() {
  #if DEBUG_SERIAL
  emit(PRIO_LOW, "[ARP] WARNING: Event queue full \xe2\x80\x94 event dropped\n");
  #endif
}

void emitGenSeed(uint16_t seqLen, uint8_t eInit, uint8_t pileCount,
                 int8_t lo, int8_t hi) {
  #if DEBUG_SERIAL
  emit(PRIO_LOW, "[GEN] seed seqLen=%u E_init=%u pile=%u lo=%d hi=%d\n",
       seqLen, eInit, pileCount, lo, hi);
  #else
  (void)seqLen; (void)eInit; (void)pileCount; (void)lo; (void)hi;
  #endif
}

void emitGenSeedDegenerate(uint16_t seqLen, int8_t singleDegree) {
  #if DEBUG_SERIAL
  emit(PRIO_LOW, "[GEN] seed seqLen=%u (pile=1 note %d, repetition)\n",
       seqLen, singleDegree);
  #else
  (void)seqLen; (void)singleDegree;
  #endif
}

// =================================================================
// Phase 1.C.4 : [SCALE]/[ARP_GEN] events
// Migrated from src/managers/ScaleManager.cpp. Format strictly identical —
// JUCE viewer parser unchanged.
// =================================================================

void emitScale(ScaleEventKind kind, uint8_t rootIdx, uint8_t modeIdx) {
  #if DEBUG_SERIAL
  static const char* ROOT_NAMES[7] = {"A", "B", "C", "D", "E", "F", "G"};
  static const char* MODE_NAMES[7] = {
    "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian"
  };
  const char* root = (rootIdx < 7) ? ROOT_NAMES[rootIdx] : "?";
  const char* mode = (modeIdx < 7) ? MODE_NAMES[modeIdx] : "?";
  switch (kind) {
    case SCALE_ROOT:
      emit(PRIO_HIGH, "[SCALE] Root %s (mode %s)\n", root, mode);
      break;
    case SCALE_MODE:
      emit(PRIO_HIGH, "[SCALE] Mode %s (root %s)\n", mode, root);
      break;
    case SCALE_CHROMATIC:
      emit(PRIO_HIGH, "[SCALE] Chromatic (root %s)\n", root);
      break;
  }
  #else
  (void)kind; (void)rootIdx; (void)modeIdx;
  #endif
}

void emitArpOctave(uint8_t octave) {
  #if DEBUG_SERIAL
  emit(PRIO_HIGH, "[ARP] Octave %d\n", octave);
  #else
  (void)octave;
  #endif
}

void emitArpGenMutation(uint8_t mutationLevel) {
  #if DEBUG_SERIAL
  emit(PRIO_HIGH, "[ARP_GEN] MutationLevel %d\n", mutationLevel);
  #else
  (void)mutationLevel;
  #endif
}

// =================================================================
// Phase 1.C.5 : [CLOCK]/[MIDI] events
// Migrated from src/midi/ClockManager.cpp (5 sites) and
// src/core/MidiTransport.cpp (3 sites). Format strictly identical —
// JUCE viewer parser unchanged.
// =================================================================

void emitClockSource(const char* srcLabel, float bpm) {
  #if DEBUG_SERIAL
  if (bpm > 0.0f) {
    emit(PRIO_HIGH, "[CLOCK] Source: last known (%.0f BPM)\n", bpm);
  } else {
    emit(PRIO_HIGH, "[CLOCK] Source: %s\n", srcLabel);
  }
  #else
  (void)srcLabel; (void)bpm;
  #endif
}

void emitMidiTransport(const char* transport, const char* state) {
  #if DEBUG_SERIAL
  emit(PRIO_HIGH, "[MIDI] %s %s\n", transport, state);
  #else
  (void)transport; (void)state;
  #endif
}

// =================================================================
// Phase 1.C.6 — [PANIC] event
// =================================================================

void emitPanic() {
  #if DEBUG_SERIAL
  emit(PRIO_HIGH, "[PANIC] All notes off on all channels\n");
  #endif
}

}  // namespace viewer
