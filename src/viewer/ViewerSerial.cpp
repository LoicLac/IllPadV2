// src/viewer/ViewerSerial.cpp
#include "ViewerSerial.h"
#include "../core/HardwareConfig.h"  // DEBUG_SERIAL
#include "../core/KeyboardData.h"    // BankSlot, BankType, NUM_BANKS, PotTarget enums, SettingsStore
#include "../arp/ArpEngine.h"
#include "../midi/ClockManager.h"
#include "../managers/PotRouter.h"
#include "../managers/NvsManager.h"
#include "../managers/BankManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Forward declarations of firmware globals (defined in main.cpp)
extern BankSlot      s_banks[NUM_BANKS];
extern PotRouter     s_potRouter;
extern NvsManager    s_nvsManager;
extern BankManager   s_bankManager;
extern SettingsStore s_settings;
extern ClockManager  s_clockManager;

// Forward declarations of debug dump helpers (defined in main.cpp, ?ALL command).
extern void dumpLedSettings();
extern void dumpColorSlots();
extern void dumpPotMapping();

// Forward declaration of debugOutput sentinel reset (defined in main.cpp).
// Forces re-emit of all [POT] params at next debugOutput() iteration.
extern void resetDbgSentinels();

namespace viewer {

namespace {

// Phase 2 : enum tagging the 4 ARPEG_GEN per-bank arguments. Used by
// handleArpGenParam to dispatch range check + setter + setLoadedX without
// 4 copies of the same code.
enum ArpGenArg : uint8_t {
  ARG_BONUS  = 0,   // x10 [10..20]
  ARG_MARGIN = 1,   // [3..12]
  ARG_PROX   = 2,   // x10 [4..20]
  ARG_ECART  = 3,   // [1..12]
};

// Phase 2 forward declarations (definitions below).
static void dispatchWriteCommand(const char* cmd);
static void handleClockMode(const char* valStr, const char* origCmd);
static void handleArpGenParam(ArpGenArg arg, const char* valStr, int bank1, const char* origCmd);

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
      // deja boote). Re-emit le boot dump complet + [GLOBALS]/[SETTINGS] +
      // resetDbgSentinels pour forcer le re-emit des [POT] params au tick
      // debugOutput() suivant. Resout les races du boot dump (LED_Bright /
      // PadSens stuck sur sentinel 0xFF, viewer cells partial).
      // Push les events dans la queue — ils seront draines par les iters
      // suivants de cette meme task.
      emitBanksHeader(NUM_BANKS);
      for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
      for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
      // Phase 2 : [BANK_SETTINGS] pour chaque bank ARPEG_GEN (no-op autre type).
      for (uint8_t i = 0; i < NUM_BANKS; i++) emitBankSettings(i);
      emitGlobals();
      emitSettings();
      resetDbgSentinels();
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
      // Chunked write : ecrit ce que le ring USB CDC accepte, attend, ecrit
      // le reste. Resout le drop des longues lignes pendant la fenetre
      // cold-USB-connect (~1.5s post-flash) ou le host Mac n'a pas encore
      // demarre son read continu : availableForWrite() retourne des values
      // trop petites pour passer un [STATE] (~200B) ou [BANK] ARPEG (~85B)
      // en une fois → ancien drop-after-100ms loosait systematiquement.
      //
      // Contrainte cle : toujours passer toWrite ≤ availableForWrite() a
      // Serial.write() pour eviter le spin lock interne USBCDC::write
      // (boucle non-timeouted sur tud_cdc_n_write_available si DTR=1 mais
      // host frozen — cf. USBCDC.cpp:388-411 framework).
      //
      // Timeout ultimate 5s par ligne : si on n'arrive pas a tout ecrire
      // (host vraiment freeze), inject \n pour resync le stream — la
      // ligne corrompue devient UnknownEvent cote viewer sans cascade.
      size_t remaining = ev.len;
      size_t offset    = 0;
      uint32_t deadline = millis() + 5000;
      while (remaining > 0 && (int32_t)(deadline - millis()) > 0) {
        if (!Serial) break;   // DTR dropped mid-write → abort fast
        size_t avail = Serial.availableForWrite();
        if (avail == 0) {
          vTaskDelay(pdMS_TO_TICKS(5));
          continue;
        }
        size_t toWrite = (remaining < avail) ? remaining : avail;
        size_t written = Serial.write(buf + offset, toWrite);
        if (written == 0) {
          vTaskDelay(pdMS_TO_TICKS(5));
          continue;
        }
        offset    += written;
        remaining -= written;
      }
      // Stream resync : ecriture partielle apres timeout → inject \n pour
      // que la ligne suivante soit parsable malgre la corruption.
      if (remaining > 0 && offset > 0) {
        Serial.write((const uint8_t*)"\n", 1);
      }
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

// =================================================================
// Phase 2 — Write commands dispatcher
// =================================================================
// Format : !KEY=VALUE[ BANK=K]\n (cf spec §5).
// Buffer reçu garanti <= 23 chars + '\0' (Task 13 path too_long).
// Strategy : copy → strtok_r split on space → split on '=' → dispatch by key.
// Le tampon scratch (tmp[24]) est mutable par strtok_r. cmd reste intact
// pour les emits d'erreur.

static void dispatchWriteCommand(const char* cmd) {
  // 1. Copy to scratch buffer (strtok_r mutates).
  char tmp[24];
  size_t n = strlen(cmd);
  if (n >= sizeof(tmp)) {
    // Should not happen (Task 13 path too_long catches before we arrive here),
    // mais defense-in-depth pour ne pas overflow strtok_r.
    emit(PRIO_HIGH, "[ERROR] cmd=%.20s... code=too_long\n", cmd);
    return;
  }
  memcpy(tmp, cmd, n + 1);

  // 2. Tokenize on space : tok1 = "!KEY=VAL", tok2 = "BANK=K" or nullptr.
  char* save = nullptr;
  char* tok1 = strtok_r(tmp, " ", &save);
  char* tok2 = strtok_r(nullptr, " ", &save);

  // 3. Split tok1 on '=' : key (after '!'), valStr.
  if (!tok1) return;  // empty command, drop silently
  char* eq = strchr(tok1, '=');
  if (!eq) {
    emit(PRIO_HIGH, "[ERROR] cmd=%s code=parse_error\n", cmd);
    return;
  }
  *eq = '\0';
  const char* key = tok1 + 1;   // skip '!'
  const char* valStr = eq + 1;

  // 4. Parse optional BANK=K (per-bank commands).
  int bank1 = -1;   // -1 = absent
  if (tok2) {
    char* eq2 = strchr(tok2, '=');
    if (!eq2 || strncmp(tok2, "BANK", 4) != 0) {
      emit(PRIO_HIGH, "[ERROR] cmd=%s code=parse_error\n", cmd);
      return;
    }
    bank1 = atoi(eq2 + 1);
  }

  // 5. Dispatch by key.
  if      (strcmp(key, "CLOCKMODE") == 0) handleClockMode(valStr, cmd);
  else if (strcmp(key, "BONUS")     == 0) handleArpGenParam(ARG_BONUS,  valStr, bank1, cmd);
  else if (strcmp(key, "MARGIN")    == 0) handleArpGenParam(ARG_MARGIN, valStr, bank1, cmd);
  else if (strcmp(key, "PROX")      == 0) handleArpGenParam(ARG_PROX,   valStr, bank1, cmd);
  else if (strcmp(key, "ECART")     == 0) handleArpGenParam(ARG_ECART,  valStr, bank1, cmd);
  else {
    emit(PRIO_HIGH, "[ERROR] cmd=%s code=unknown_command\n", cmd);
  }
}

// =================================================================
// Phase 2 — Handler stubs (filled by Tasks 15 + 16)
// =================================================================
// Stubs émettent unknown_command pour qu'on puisse compiler + tester le
// dispatcher avant de remplir les bodies.

static void handleClockMode(const char* valStr, const char* origCmd) {
  (void)valStr;
  emit(PRIO_HIGH, "[ERROR] cmd=%s code=unknown_command\n", origCmd);
}

static void handleArpGenParam(ArpGenArg arg, const char* valStr, int bank1, const char* origCmd) {
  (void)arg; (void)valStr; (void)bank1;
  emit(PRIO_HIGH, "[ERROR] cmd=%s code=unknown_command\n", origCmd);
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
  #if DEBUG_SERIAL
  // Phase 1.D : migrated from main.cpp pollRuntimeCommands(). Non-blocking
  // Serial poll for viewer-API runtime commands. ?STATE / ?BANKS / ?BOTH /
  // ?ALL emit the corresponding dump then [READY]. ?BOTH/?ALL/?STATE also
  // call resetDbgSentinels() so the next debugOutput() iteration re-emits
  // every [POT] param (fixes stuck-sentinel races, e.g. LED_Bright/PadSens).
  static char    cmdBuf[24];
  static uint8_t cmdLen = 0;
  static bool    s_cmdOverflow = false;  // Phase 2 : flag set when cmdLen overflows
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      cmdBuf[cmdLen] = '\0';
      // Phase 2 : si overflow détecté pendant la réception, émettre too_long
      // avant le dispatch normal. Le cmd tronqué donne au viewer le contexte
      // pour son toast. Reset état + skip le dispatch (cmdBuf est tronqué,
      // pas exploitable).
      if (s_cmdOverflow) {
        emit(PRIO_HIGH, "[ERROR] cmd=%.20s... code=too_long\n", cmdBuf);
        cmdLen = 0;
        s_cmdOverflow = false;
        continue;
      }
      if (strcmp(cmdBuf, "?STATE") == 0) {
        emitState(s_bankManager.getCurrentBank());
        // Phase 2 : émet [BANK_SETTINGS] si foreground ARPEG_GEN (no-op sinon).
        emitBankSettings(s_bankManager.getCurrentBank());
        emitReady(s_bankManager.getCurrentBank() + 1);
      } else if (strcmp(cmdBuf, "?BANKS") == 0) {
        emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
        emitReady(s_bankManager.getCurrentBank() + 1);
      } else if (strcmp(cmdBuf, "?BOTH") == 0) {
        emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
        // Phase 2 : [BANK_SETTINGS] pour chaque bank ARPEG_GEN.
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBankSettings(i);
        emitGlobals();
        emitSettings();
        resetDbgSentinels();
        emitReady(s_bankManager.getCurrentBank() + 1);
      } else if (strcmp(cmdBuf, "?ALL") == 0) {
        emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
        // Phase 2 : [BANK_SETTINGS] pour chaque bank ARPEG_GEN.
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBankSettings(i);
        emitGlobals();
        emitSettings();
        resetDbgSentinels();
        dumpLedSettings();
        dumpColorSlots();
        dumpPotMapping();
        emitReady(s_bankManager.getCurrentBank() + 1);
      } else if (cmdBuf[0] == '!') {
        dispatchWriteCommand(cmdBuf);
      }
      cmdLen = 0;
    } else if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    } else {
      // Phase 2 : overflow — set flag, keep cmdBuf comme tronqué (le \n
      // déclenchera l'emit [ERROR] too_long avec les 20 premiers chars).
      s_cmdOverflow = true;
    }
  }
  #endif
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

// =================================================================
// Phase 1.D — [GLOBALS]/[SETTINGS] events
// New events introduced by Phase 1.D. Boot dump + auto-resync emit
// these once after [STATE], and ?BOTH/?ALL also include them.
// =================================================================

void emitGlobals() {
  #if DEBUG_SERIAL
  emit(PRIO_HIGH, "[GLOBALS] Tempo=%u LED_Bright=%u PadSens=%u ClockSource=%s\n",
       s_potRouter.getTempoBPM(),
       s_potRouter.getLedBrightness(),
       s_potRouter.getPadSensitivity(),
       s_clockManager.getActiveSourceLabel());
  #endif
}

void emitSettings() {
  #if DEBUG_SERIAL
  // BleInterval emis en numerique (0..3) pour matcher le viewer pre-code
  // qui parse std::stoi(*v). Mapping interne :
  //   0=BLE_OFF, 1=BLE_LOW_LATENCY, 2=BLE_NORMAL, 3=BLE_BATTERY_SAVER.
  emit(PRIO_HIGH, "[SETTINGS] ClockMode=%s PanicReconnect=%u DoubleTapMs=%u "
                  "AftertouchRate=%u BleInterval=%u BatAdcFull=%u\n",
       s_settings.clockMode == CLOCK_MASTER ? "master" : "slave",
       s_settings.panicOnReconnect,
       s_settings.doubleTapMs,
       s_settings.aftertouchRate,
       s_settings.bleInterval,
       s_settings.batAdcAtFull);
  #endif
}

// =================================================================
// Phase 2 — [BANK_SETTINGS] event
// Émet bank=N bonus=X margin=Y prox=Z ecart=W pour les banks ARPEG_GEN.
// No-op silencieux pour les autres bank types (le firmware n'émet jamais
// [BANK_SETTINGS] pour une bank non-ARPEG_GEN — cf spec §6.2).
// =================================================================

void emitBankSettings(uint8_t bankIdx) {
  #if DEBUG_SERIAL
  if (bankIdx >= NUM_BANKS) return;
  if (s_banks[bankIdx].type != BANK_ARPEG_GEN) return;
  emit(PRIO_HIGH,
       "[BANK_SETTINGS] bank=%u bonus=%u margin=%u prox=%u ecart=%u\n",
       bankIdx + 1,
       s_nvsManager.getLoadedBonusPile(bankIdx),
       s_nvsManager.getLoadedMarginWalk(bankIdx),
       s_nvsManager.getLoadedProximityFactor(bankIdx),
       s_nvsManager.getLoadedEcart(bankIdx));
  #else
  (void)bankIdx;
  #endif
}

// =================================================================
// Phase 1.E — [CLOCK] BPM= debounced (external sync updates)
// =================================================================

void emitClockBpm(float bpm, const char* srcLabel) {
  #if DEBUG_SERIAL
  emit(PRIO_HIGH, "[CLOCK] BPM=%.0f src=%s\n", bpm, srcLabel);
  #else
  (void)bpm; (void)srcLabel;
  #endif
}

}  // namespace viewer
