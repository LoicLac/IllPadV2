# Viewer Serial Centralization — Phase 1 Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Centraliser tous les serial outputs runtime vers le viewer dans un
module dédié `ViewerSerial`, ajouter le tagging `[BOOT *]` pour les boot debug,
résoudre les findings audit 2026-05-17 (R1 blocking, R2-M1 [GLOBALS], R3
[CLOCK] BPM externe, M3 ?BOTH self-sufficient, I3-I6 cleanup UNGATED) et
préparer les hooks bidirectionnels pour Phase 2.

**Architecture:** Module `src/viewer/ViewerSerial.{cpp,h}` avec FreeRTOS queue
(xQueueHandle, 32 slots × 256 bytes = 8 KB), background task Core 1 priorité 0
(strictement inférieure au main loop priorité 1, posture safe d'entrée).
Dormance via `if (Serial)` + atomic flag. Émissions runtime via `viewer::emit*()`
en hot-path (push queue non-bloquant, drop sous backpressure 70%). Boot debug
reste raw `Serial.print` mais re-tagué `[BOOT *]`.

**Tech Stack:** ESP32-S3 Arduino framework, FreeRTOS xQueue + task pinned Core
1, USB CDC (TinyUSB), PlatformIO build (`pio run -e esp32-s3-devkitc-1`).

**Spec source:** [`docs/superpowers/specs/2026-05-17-viewer-serial-centralization-design.md`](../specs/2026-05-17-viewer-serial-centralization-design.md).

**Prérequis** : viewer-juce pré-codé selon la spec (plan séparé
[`2026-05-17-viewer-juce-phase1-precode-plan.md`](2026-05-17-viewer-juce-phase1-precode-plan.md)).
Tous les parser branches nouveaux doivent être en place côté `../ILLPAD_V2-viewer/`
avant Task 2 de ce plan.

---

## File Structure

**Files to create:**
- `src/viewer/ViewerSerial.h` — module public API (`begin`, `pollCommands`,
  `isConnected`, typed `emit*` functions).
- `src/viewer/ViewerSerial.cpp` — implémentation : queue, task drain, dormance,
  auto-resync, formatage typed → emit interne.

**Files to modify (firmware) :**
- `src/main.cpp` — Serial config order, viewer::begin/pollCommands intégration,
  migration `dumpBanksGlobal`/`dumpBankState`/`emitReady` vers le module,
  debugOutput → emitPot ×16, handlePotPipeline CC/PB, midiPanic, boot debug
  `[INIT] *` → `[BOOT] *`, banner gated, `[INIT] FATAL` → `[FATAL]`, s_settings
  promu module-level.
- `src/managers/BankManager.cpp` — bank switch emit via `viewer::emitBankSwitch`.
- `src/managers/ScaleManager.cpp` — scale/octave emit via `viewer::emitScale`/
  `emitArpOctave`/`emitArpGenMutation`.
- `src/managers/NvsManager.cpp` — `[NVS]` loadAll lines → `[BOOT NVS]`, removal
  UNGATED ArpPotStore v0 warning gate.
- `src/managers/PotRouter.cpp` — `[POT] Rebuilt/initialized` → `[BOOT POT]`.
- `src/arp/ArpEngine.cpp` — `[ARP] +/-note/Play/Stop/queue full` + `[GEN] seed`
  via `viewer::emitArp*`/`emitGenSeed`.
- `src/midi/ClockManager.cpp/.h` — source change emit + new `getActiveSourceLabel()`
  accessor + new `[CLOCK] BPM=` emit debounced.
- `src/core/MidiTransport.cpp` — boot init `[MIDI]` → `[BOOT MIDI]`, runtime
  connect/disconnect via `viewer::emitMidiTransport`.
- `src/core/CapacitiveKeyboard.cpp/.h` — `[KB]` → `[BOOT KB]`, removal
  `logFullBaselineTable()` (dead code).
- `src/core/PotFilter.cpp` — `[POT] Seed/MCP3208` → `[BOOT POT]`.

**Files to update (prerequisite, in viewer worktree `../ILLPAD_V2-viewer/`) :**
- `ILLPADViewer/Source/serial/RuntimeParser.{cpp,h}` — new branches.
- `ILLPADViewer/Source/model/Model.{cpp,h}` — `applyGlobals/applySettings`.
- `ILLPADViewer/Source/...` — ModeDetector, SerialReader CDC handling, UI overlay
  `[FATAL]`, boot log panel (optional), setup mode state.
- `ILLPADViewer/docs/firmware-viewer-protocol.md` — updated only at sous-phase 1.G.

---

## Task 1: Pre-flight — Viewer pre-coding verification

**No firmware code change. Manual verification before starting Task 2.**

**Files:**
- Verify: `../ILLPAD_V2-viewer/ILLPADViewer/Source/serial/RuntimeParser.cpp`
- Verify: `../ILLPAD_V2-viewer/ILLPADViewer/Source/model/Model.cpp`

- [ ] **Step 1: Confirm viewer parser has new branches**

Run:
```bash
grep -nE '\[GLOBALS\]|\[SETTINGS\]|\[FATAL\]|\[SETUP\]|\[BOOT' \
  ../ILLPAD_V2-viewer/ILLPADViewer/Source/serial/RuntimeParser.cpp
```
Expected: at least 5 matches (one per new event prefix).

- [ ] **Step 2: Confirm Model has applyGlobals/applySettings**

Run:
```bash
grep -nE 'applyGlobals|applySettings' \
  ../ILLPAD_V2-viewer/ILLPADViewer/Source/model/Model.cpp
```
Expected: function definitions present.

- [ ] **Step 3: Confirm ModeDetector guettes `[BOOT] Ready.`**

Run:
```bash
grep -nE '\[BOOT\] Ready\.|\[INIT\] Ready\.' \
  ../ILLPAD_V2-viewer/ILLPADViewer/Source/serial/ModeDetector.cpp
```
Expected: `[BOOT] Ready.` present. `[INIT] Ready.` either absent OR kept as
fallback (acceptable transitional state).

- [ ] **Step 4: Compile viewer**

Run:
```bash
cd ../ILLPAD_V2-viewer/ILLPADViewer && cmake --build build
```
Expected: build succeeds. If fails, halt — fix viewer side before proceeding.

- [ ] **Step 5: Run viewer once against current firmware (sanity check)**

Boot firmware (current `main` branch, unchanged), launch viewer. Should connect,
hydrate as before. The new parser branches are dormant (no events sent yet).
Expected: viewer functions identically to today.

- [ ] **Step 6: Document verification**

Verify the above 5 checks pass. No commit. Proceed to Task 2.

---

## Task 2: Phase 1.A — Plomberie module (ViewerSerial skeleton)

**Files:**
- Create: `src/viewer/ViewerSerial.h`
- Create: `src/viewer/ViewerSerial.cpp`
- Modify: `src/main.cpp` (Serial.begin order, viewer::begin/pollCommands calls)

- [ ] **Step 1: Create `src/viewer/ViewerSerial.h`**

```cpp
// src/viewer/ViewerSerial.h
#pragma once

#include <Arduino.h>
#include <atomic>

namespace viewer {

// NOTE: PRIO_LOW / PRIO_HIGH instead of LOW / HIGH to avoid collision with
// Arduino's `#define LOW 0x0` / `#define HIGH 0x1` (esp32-hal-gpio.h:41-42).
// Les macros sont substituees par le preprocesseur avant le scoping C++, donc
// meme `Priority::LOW` est casse. Renommer les enumerators est le workaround
// standard en Arduino-land. (Decouverte HW post-implementation Task 2.)
enum Priority : uint8_t {
  PRIO_LOW  = 0,
  PRIO_HIGH = 1,
};

// Lifecycle — to be called from main.cpp setup() / loop()
void begin();          // create queue + task, call after Serial.begin in setup()
void pollCommands();   // non-blocking, call in tete de loop()

// Connection state — cheap atomic load
bool isConnected();

// Phase 1.A : pas d'emit_xxx() encore. Ajoutés au fur et à mesure des
// sous-phases 1.C.*, 1.D, 1.E, 1.F.

}  // namespace viewer
```

- [ ] **Step 2: Create `src/viewer/ViewerSerial.cpp`**

```cpp
// src/viewer/ViewerSerial.cpp
#include "ViewerSerial.h"
#include "../core/HardwareConfig.h"  // DEBUG_SERIAL
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

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

void taskBody(void* /*arg*/) {
  QueuedEvent ev;
  for (;;) {
    bool nowConnected = (bool)Serial;
    bool wasConnected = s_viewerConnected.exchange(nowConnected, std::memory_order_acq_rel);

    if (!wasConnected && nowConnected) {
      // Phase 1.A : auto-resync hook empty. Phase 1.D wires it.
    }

    if (!nowConnected) {
      // Viewer absent — drain silently
      while (xQueueReceive(s_queue, &ev, 0) == pdPASS) { /* discard */ }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Drain with 10ms wait — if no event in 10ms, loop and re-check connection
    if (xQueueReceive(s_queue, &ev, pdMS_TO_TICKS(10)) == pdPASS) {
      // Serial.write peut retourner < ev.len si le ring buffer USB CDC est
      // plein malgre setTxTimeoutMs(0) (viewer host slow). Drop la ligne
      // entiere plutot qu'emettre une ligne tronquee qui confondrait le
      // parser viewer. Le viewer perdra cet event mais restera coherent.
      const uint8_t* buf = reinterpret_cast<const uint8_t*>(ev.line);
      if (Serial.availableForWrite() >= ev.len) {
        Serial.write(buf, ev.len);
      }
      // else : drop silencieux. La prochaine resync (auto-resync ou ?BOTH)
      // re-emettra l'etat complet.
    }
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
    // Queue exists but no drain — emit() saturera la queue puis stagnera en
    // backpressure permanent. Pas un crash mais degradation silencieuse.
    // Cas extreme (RAM saturée au boot) — rare. Signalisation FATAL pour diag.
    Serial.println("[FATAL] viewer task create failed");
    Serial.flush();
  }
}

void pollCommands() {
  // Phase 1.A : stub. Phase 1.D migrera pollRuntimeCommands ici.
  // L'existant pollRuntimeCommands() reste dans main.cpp pendant Phase 1.A-1.C.
}

bool isConnected() {
  return s_viewerConnected.load(std::memory_order_acquire);
}

}  // namespace viewer
```

- [ ] **Step 3: Modify `src/main.cpp` Serial setup order**

**Decouverte HW post-implementation Task 2** : le projet utilise `USBCDC`
(TinyUSB composite, cf. `platformio.ini` `ARDUINO_USB_MODE=0 + ARDUINO_USB_CDC_ON_BOOT=1`),
PAS `HWCDC`. La classe `USBCDC` n'expose **pas** `setTxBufferSize()` — compile
error "no member named setTxBufferSize". Le tx ring TinyUSB est dimensionné
via `CFG_TUD_CDC_TX_BUFSIZE` au niveau sdkconfig, hors scope d'un appel
runtime. Le default (~256 bytes) reste actif en Phase 1.A — le drop-on-full
logic dans `taskBody` gère les overflow.

Locate `Serial.begin(115200);` at line ~504 in `setup()`. Replace the block:

```cpp
  Serial.begin(115200);
  delay(800);  // Laisse monter le rail d'alim (cold boot apres longue pause)
```

with:

```cpp
  // NOTE: setTxBufferSize() existe sur HWCDC mais PAS sur USBCDC. Ce projet
  // utilise USBCDC (TinyUSB composite). Le tx ring TinyUSB est dimensionne
  // via CFG_TUD_CDC_TX_BUFSIZE au niveau sdkconfig (hors scope Phase 1.A).
  // Si bursts > 256 bytes saturent le ring -> drop ligne dans taskBody.
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);       // non-blocking writes — bypass USBCDC default timeout
  delay(800);  // Laisse monter le rail d'alim (cold boot apres longue pause)
```

**Reserve possible Phase 2** : si le HW gate Task 2 revele des drops visibles
cote viewer pendant les bursts (boot dump ou ?ALL), ajouter
`-DCFG_TUD_CDC_TX_BUFSIZE=8192` aux `build_flags` de `platformio.ini`. Modif
forbidden en Phase 1 (cf. CLAUDE.md projet "platformio.ini DO NOT MODIFY
unless adding lib_deps"). Necessite override explicit utilisateur.

- [ ] **Step 4: Add `viewer::begin()` call to `src/main.cpp` setup()**

Add at the top of main.cpp with the existing includes :
```cpp
#include "viewer/ViewerSerial.h"
```

In setup(), locate the boot dump section near line 843 :
```cpp
  #if DEBUG_SERIAL
  Serial.println("[INIT] Ready.");
  // Viewer-API boot dump : 1× [BANKS] + 8× [BANK] + 8× [STATE] + 1× [READY].
  // Allows the JUCE viewer to populate its UI from a single boot dump.
  // Plan tasks A.3 + A.4 + A.5 step 3.
  dumpBanksGlobal();
  for (uint8_t i = 0; i < NUM_BANKS; i++) dumpBankState(i);
  emitReady();
  #endif
}
```

Add `viewer::begin();` between `[INIT] Ready.` and `dumpBanksGlobal();` :
```cpp
  #if DEBUG_SERIAL
  Serial.println("[INIT] Ready.");
  viewer::begin();   // Phase 1.A : create queue + task before boot dump
  dumpBanksGlobal();
  for (uint8_t i = 0; i < NUM_BANKS; i++) dumpBankState(i);
  emitReady();
  #endif
```

- [ ] **Step 5: Add `viewer::pollCommands()` call to `src/main.cpp` loop()**

Locate the existing `pollRuntimeCommands()` call near line 1473 in `loop()` :
```cpp
  #if DEBUG_SERIAL
  // Viewer-API runtime command poll (?STATE/?BANKS/?BOTH). Non-blocking,
  // cheap (Serial.available() == 0 path is just a register read).
  pollRuntimeCommands();
  #endif
```

Add `viewer::pollCommands();` AFTER it (both coexist during Phase 1.A) :
```cpp
  #if DEBUG_SERIAL
  pollRuntimeCommands();
  viewer::pollCommands();   // Phase 1.A : stub. Will replace pollRuntimeCommands in 1.D.
  #endif
```

- [ ] **Step 6: Compile gate**

Run :
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: Exit 0, zéro nouveau warning.

If build fails on `freertos/queue.h` not found, try alternative includes:
- `#include "freertos/queue.h"` (without `<>`)
- Verify `framework-arduinoespressif32` includes path.

- [ ] **Step 7: HW gate — smoke test + CPU bench (spec §3b)**

1. Flash firmware : `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload`
2. Open serial monitor : `~/.platformio/penv/bin/pio device monitor -b 115200`
3. Verify boot output is unchanged (all `[INIT] *` lines + boot dump appears).
   Pas de `[FATAL]` au boot (= queue + task créées OK).
4. Close serial monitor, open viewer JUCE app. Verify viewer hydrates.
5. Close viewer, re-open. Verify firmware doesn't crash.
6. Disconnect USB cable for 5s, reconnect. Verify firmware still responsive
   (LED bargraph reacts to pot moves), viewer reconnects.
7. **CPU bench (spec §3b)** : avec une source MIDI clock externe à 120 BPM,
   jouer 10 pads simultanément en ARPEG sur 30s, viewer connecté ET déconnecté.
   Observer en parallèle dans le serial monitor (ou en ajoutant temporairement
   un compteur `uxTaskGetSystemState`) que :
   - Aucun MIDI jitter audible côté DAW
   - Pas de "[ARP] WARNING: Event queue full" répétitifs
   - LED bargraph reste fluide
   Si différence comportementale viewer absent vs présent → reporter dans le
   commit, ouvrir une issue, ne pas commit.
8. **Wait for user OK before commit.**

- [ ] **Step 8: Commit**

```bash
git add src/viewer/ViewerSerial.h src/viewer/ViewerSerial.cpp src/main.cpp
git commit -m "$(cat <<'EOF'
feat(viewer): Phase 1.A plomberie module ViewerSerial — queue + task + dormance

Creation du module src/viewer/ViewerSerial.{cpp,h} : FreeRTOS xQueue 8 KB
(32 slots de 256 bytes), task Core 1 priorite 0 (strict inferior main loop,
posture safe), atomic flag s_viewerConnected pour dormance via if (Serial).
La task draine la queue vers Serial avec setTxTimeoutMs(0) en non-blocking.

Configuration HWCDC ajustee dans setup() : setTxBufferSize(8192) AVANT
Serial.begin(), setTxTimeoutMs(0) APRES — bypass HWCDC default 100ms qui
bloquait Core 1 sous load viewer (R1 audit 2026-05-17).

Phase 1.A : module est cree mais aucun emit_xxx() encore. La queue reste
inactive. viewer::pollCommands() est un stub. pollRuntimeCommands existant
inchange. Verification : zero regression visible, firmware boot + viewer
hydrate comme avant.

Refs design spec docs/superpowers/specs/2026-05-17-viewer-serial-centralization-design.md
EOF
)"
```

---

## Task 3: Phase 1.B — Boot debug tagging + UNGATED cleanup

**Goal :** Renommer tous les boot debug `[INIT/KB/NVS/POT/MIDI init]` en
`[BOOT *]` uniforme, gater le banner UNGATED, promouvoir `[INIT] FATAL` en
`[FATAL]` event, supprimer dead code `logFullBaselineTable()`.

**Files:**
- Modify: `src/main.cpp` (banner + 15 `[INIT]` sites + `[INIT] FATAL`)
- Modify: `src/core/CapacitiveKeyboard.cpp` (~25 `[KB]` sites)
- Modify: `src/core/CapacitiveKeyboard.h` (remove `logFullBaselineTable` decl)
- Modify: `src/managers/NvsManager.cpp` (~25 loadAll `[NVS]` sites + 1 v0 warning ungate)
- Modify: `src/managers/PotRouter.cpp` (2 sites)
- Modify: `src/core/PotFilter.cpp` (2 sites)
- Modify: `src/core/MidiTransport.cpp` (3 boot sites)

- [ ] **Step 1: Banner gated + renamed**

Modify `src/main.cpp` line 506-507. Replace :
```cpp
  Serial.println();
  Serial.println("=== ILLPAD48 V2 ===");
```
with :
```cpp
  #if DEBUG_SERIAL
  Serial.println();
  Serial.println("[BOOT] === ILLPAD48 V2 ===");
  #endif
```

- [ ] **Step 2: Migrate `[INIT] *` → `[BOOT] *` in `src/main.cpp`**

15 sites at lines 520, 533, 593, 655, 662, 668, 685, 715, 722, 767, 786, 797,
804, 819, 825, 843. Pattern :
- Replace literal `"[INIT] "` with `"[BOOT] "` in each `Serial.println` and
  `Serial.printf`.
- Keep the surrounding `#if DEBUG_SERIAL` blocks intact.

Use a sed-style approach (cautious, verify each match) :
```bash
grep -n '"\[INIT\] ' src/main.cpp
```
For each match, edit `[INIT] ` → `[BOOT] `. Watch out for L526 `[INIT] FATAL` —
**ne pas** la renommer ici, elle sera traitée en Step 4 séparément.

Concretely, ALL these become `[BOOT] *` :
- L520 : `[INIT] I2C OK.` → `[BOOT] I2C OK.`
- L533 : `[INIT] Keyboard OK.` → `[BOOT] Keyboard OK.`
- L593 : `[INIT] Hold rear button to enter setup mode...` → `[BOOT] Hold rear button to enter setup mode...`
- L655 : `[INIT] MIDI Transport OK.` → `[BOOT] MIDI Transport OK.`
- L662 : `[INIT] ClockManager OK.` → `[BOOT] ClockManager OK.`
- L668 : `[INIT] MIDI Engine OK.` → `[BOOT] MIDI Engine OK.`
- L685 : `[INIT] NVS loaded. Bank=%d` → `[BOOT] NVS loaded. Bank=%d`
- L715 : `[INIT] Bank %d: %s, ArpEngine assigned` → `[BOOT] Bank %d: %s, ArpEngine assigned`
- L722 : `[INIT] No ARPEG banks configured.` → `[BOOT] No ARPEG banks configured.`
- L767 : `[INIT] ArpScheduler OK.` → `[BOOT] ArpScheduler OK.`
- L786 : `[INIT] BankManager OK.` → `[BOOT] BankManager OK.`
- L797 : `[INIT] ScaleManager OK.` → `[BOOT] ScaleManager OK.`
- L804 : `[INIT] ControlPadManager OK.` → `[BOOT] ControlPadManager OK.`
- L819 : `[INIT] PotFilter + PotRouter OK.` → `[BOOT] PotFilter + PotRouter OK.`
- L825 : `[INIT] NvsManager OK.` → `[BOOT] NvsManager OK.`
- L843 : `[INIT] Ready.` → `[BOOT] Ready.` (**MARKER ModeDetector** — viewer pre-codé pour reconnaître)

- [ ] **Step 3: `[SETUP]` line stays unchanged**

main.cpp:628 `Serial.println("[SETUP] Entering setup mode...");` — **NE PAS
RENOMMER**. C'est un parser-recognized marker (spec §14b). Le viewer pre-codé
le reconnaît pour passer en état "setup mode active".

- [ ] **Step 4: Promote `[INIT] FATAL` → `[FATAL]` event**

Modify main.cpp:526. Replace :
```cpp
  if (!kbOk) {
    Serial.println("[INIT] FATAL: Keyboard init failed!");
    s_leds.showBootFailure(3);  // Step 3 blinks = keyboard failed
    for (;;) { s_leds.update(); delay(10); }
  }
```
with :
```cpp
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
```

- [ ] **Step 5: Migrate `[KB] *` → `[BOOT KB] *` in `src/core/CapacitiveKeyboard.cpp`**

~25 sites between L204 and L705. Use grep to find :
```bash
grep -nE 'Serial\.(print|println).*"\[KB\]' src/core/CapacitiveKeyboard.cpp
```

For each match, edit `[KB] ` → `[BOOT KB] `. Keep `#if DEBUG_SERIAL` guards
intact.

Examples :
- L204 : `Serial.println("[KB] Starting capacitive keyboard init...");` → `Serial.println("[BOOT KB] Starting capacitive keyboard init...");`
- L209 : `Serial.println("[KB] Calibration data loaded.");` → `Serial.println("[BOOT KB] Calibration data loaded.");`
- L226 : `Serial.println("[KB] FATAL: Sensor init failed!");` → **stays** as `[KB]` since this is also a fatal (NB: redirect to [FATAL] handled in main.cpp Step 4 via `kbOk = false` path).

Wait — verify : if sensor init fails inside `s_keyboard.begin()`, returns false,
main.cpp:524 `kbOk = false` → main.cpp:526 emits `[FATAL]`. So the
CapacitiveKeyboard.cpp:226 `[KB] FATAL: Sensor init failed!` is a redundant log
just before returning. Le `[FATAL]` de main.cpp est le canonical. La ligne
[KB] FATAL est juste un détail interne — devient `[BOOT KB] FATAL: Sensor
init failed!`.

So **all** L204, L209, L214-216, L224, L226, L407, L417-419, L429-432, L452-454,
L539-558, L563-578, L591, L674, L676-680, L698, L702-705 → rename to `[BOOT KB] `.

- [ ] **Step 6: Remove `logFullBaselineTable()` dead code**

Modify `src/core/CapacitiveKeyboard.h` — remove the declaration :
```cpp
  void logFullBaselineTable();
```

Modify `src/core/CapacitiveKeyboard.cpp` — remove L751-760 entire definition :
```cpp
void CapacitiveKeyboard::logFullBaselineTable() {
  Serial.println("\n--- Current Baselines ---");
  for (int i = 0; i < NUM_KEYS; ++i) {
    Serial.print(baselineData[i]);
    if (i != NUM_KEYS - 1) {
      Serial.print((i % 12 == 11) ? "\n" : "\t");
    }
  }
  Serial.println("\n-------------------------");
}
```

Verify zero callers in `src/` after removal :
```bash
grep -rn "logFullBaselineTable" src/
```
Expected: zero matches (besides the now-deleted lines).

- [ ] **Step 7: Migrate `[NVS] *` → `[BOOT NVS] *` in loadAll only**

Modify `src/managers/NvsManager.cpp`. Sites à renommer (boot context — toutes
appelées via `loadAll()` ou helpers boot-only) :
- L172 : `[NVS] Task created (Core 1, priority 1).` → `[BOOT NVS]`
- L610, L626, L658, L674, L692 : Bank-related loads → `[BOOT NVS]`
- L710, L723 : Velocity / pitch bend → `[BOOT NVS]`
- L754 : Arp pot params loaded → `[BOOT NVS]`
- L769, L782, L793, L808 : Tempo / pot / LED / sens → `[BOOT NVS]`
- L820, L835, L850, L864 : Pad orders / bank pads / scale pads / arp pads → `[BOOT NVS]`
- L882, L902 : Settings + LED settings → `[BOOT NVS]`
- L920, L924, L936, L949, L980 : Control pads / mapping / filter / PotRouter → `[BOOT NVS]`

Sites à **PAS** renommer (ambigu boot/runtime) :
- L513, L520, L539, L541, L547, L574, L580, L588 : helpers `loadBlob`/`saveBlob`
  appelés depuis boot ET runtime — restent `[NVS]`.
- L1120 : `Serial.println("[NVS] Saved pad order.");` — worker task save
  callback (runtime).

- [ ] **Step 8: Gate `[NVS] ArpPotStore raw/v0`**

Modify `src/managers/NvsManager.cpp` line 751. The line is currently UNGATED
(I5 audit finding) :
```cpp
      Serial.println("[NVS] ArpPotStore raw/v0 detecte - reset v1 applique (defaults compile-time). User doit re-regler gate/shuffle/division/oct/template.");
```

Wrap with `#if DEBUG_SERIAL` AND rename to `[BOOT NVS]` :
```cpp
      #if DEBUG_SERIAL
      Serial.println("[BOOT NVS] ArpPotStore raw/v0 detecte - reset v1 applique (defaults compile-time). User doit re-regler gate/shuffle/division/oct/template.");
      #endif
```

- [ ] **Step 9: Migrate `[POT] Seed/MCP3208` → `[BOOT POT]`**

Modify `src/core/PotFilter.cpp` lines 136-144 :
```cpp
        #if DEBUG_SERIAL
        Serial.printf("[BOOT POT] Seed %u: median=%u (sorted=%u,%u,%u,%u,%u)\n",
                      i, initial,
                      samples[0], samples[1], samples[2],
                      samples[3], samples[4]);
        #endif
    }

    #if DEBUG_SERIAL
    Serial.println("[BOOT POT] MCP3208 boot OK.");
    #endif
```

- [ ] **Step 10: Migrate `[POT] Rebuilt/initialized` → `[BOOT POT]`**

Modify `src/managers/PotRouter.cpp` lines 275 + 364 :
```cpp
  #if DEBUG_SERIAL
  Serial.printf("[BOOT POT] Rebuilt %d bindings from mapping (%d CC slots)\n",
                _numBindings, _ccSlotCount);
  #endif
```
and
```cpp
  #if DEBUG_SERIAL
  Serial.printf("[BOOT POT] %d bindings, %d pots initialized\n", _numBindings, NUM_POTS);
  #endif
```

- [ ] **Step 11: Migrate `[MIDI] init` → `[BOOT MIDI]`**

Modify `src/core/MidiTransport.cpp`. Only the **boot init** lines (NOT the
runtime connect/disconnect) :
- L92 : `Serial.println("[MIDI] USB MIDI initialized.");` → `Serial.println("[BOOT MIDI] USB MIDI initialized.");`
- L103 : `Serial.println("[MIDI] BLE MIDI initialized.");` → `Serial.println("[BOOT MIDI] BLE MIDI initialized.");`
- L107 : `Serial.println("[MIDI] BLE disabled (USB only).");` → `Serial.println("[BOOT MIDI] BLE disabled (USB only).");`

**NE PAS RENOMMER** :
- L62, L69 : `[MIDI] BLE connected/disconnected` (runtime, parser §1.14, restent `[MIDI]`)
- L123 : `[MIDI] USB %s\n` (runtime, parser §1.14, reste `[MIDI]`)

- [ ] **Step 12: Compile gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: Exit 0.

- [ ] **Step 13: HW gate**

1. Flash : `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload`
2. Open serial monitor.
3. Verify boot output : all lines start with `[BOOT *]` or `[CLOCK]` (init).
4. Verify `[BOOT] Ready.` appears at end of boot.
5. Verify NO `[INIT]` prefixes remain (except possibly `[INIT] FATAL` should
   now be `[FATAL]` — only emitted in failure case).
6. Open viewer JUCE app. Verify ModeDetector picks up `[BOOT] Ready.`,
   viewer transitions to runtime state, hydrates normally.
7. **Wait for user OK before commit.**

- [ ] **Step 14: Commit**

```bash
git add src/main.cpp src/core/CapacitiveKeyboard.cpp src/core/CapacitiveKeyboard.h \
        src/managers/NvsManager.cpp src/managers/PotRouter.cpp \
        src/core/PotFilter.cpp src/core/MidiTransport.cpp
git commit -m "$(cat <<'EOF'
refactor(viewer): Phase 1.B boot debug tagging + cleanup UNGATED

Renommage uniforme des boot debug lines vers prefix [BOOT *] :
- main.cpp [INIT] * -> [BOOT] * (15 sites, dont [BOOT] Ready. marker)
- CapacitiveKeyboard.cpp [KB] * -> [BOOT KB] * (~25 sites)
- NvsManager.cpp [NVS] * dans loadAll -> [BOOT NVS] * (~25 sites,
  loadBlob/saveBlob/worker helpers restent [NVS] generic)
- PotFilter.cpp [POT] Seed/MCP3208 -> [BOOT POT] (2 sites)
- PotRouter.cpp [POT] Rebuilt/initialized -> [BOOT POT] (2 sites)
- MidiTransport.cpp [MIDI] init -> [BOOT MIDI] (3 sites, runtime
  connect/disconnect inchanges)

Cleanup UNGATED (audit findings I4/I5/I6) :
- Banner === ILLPAD48 V2 === gated DEBUG_SERIAL + renomme [BOOT] ===
- [NVS] ArpPotStore v0 warning gated DEBUG_SERIAL + renomme [BOOT NVS]
- [INIT] FATAL: Keyboard init failed! promu en [FATAL] event
  always-on (parser-recognized, Serial.flush avant boucle infinie)
- logFullBaselineTable() dead code supprime (header + impl)

Viewer ModeDetector pre-code pour reconnaitre [BOOT] Ready. transition.
Refs design spec docs/superpowers/specs/2026-05-17-viewer-serial-centralization-design.md
EOF
)"
```

---

## Task 4: Phase 1.C.1 — Migrate [POT] events to module

**Goal :** Premier user du module. Migrer les ~18 sites `[POT] *` (16 dans
debugOutput + 2 dans handlePotPipeline) vers `viewer::emitPot()`.

**Files:**
- Modify: `src/viewer/ViewerSerial.h` (add `emitPot` declaration)
- Modify: `src/viewer/ViewerSerial.cpp` (add `emitPot` implementation + emit helper)
- Modify: `src/main.cpp` (migrate 18 Serial.printf sites)

- [ ] **Step 1: Add internal `emit()` helper + `emitPot()` in module .h**

Modify `src/viewer/ViewerSerial.h`. Add after the `bool isConnected();` line :
```cpp
// --- Phase 1.C.1 : [POT] events ---
// slot : "R1", "R1H", ..., "R4H", or "--" for rear pot / global no-slot.
// target : printable target name (e.g., "Tempo", "LED_Bright", "CC74").
// valueStr : pre-formatted value ("120", "0.50", "Dorian", ...).
// unit : optional unit suffix ("BPM"), pass "" or nullptr for none.
void emitPot(const char* slot, const char* target, const char* valueStr, const char* unit);
```

- [ ] **Step 2: Add internal emit() + emitPot() in module .cpp**

Modify `src/viewer/ViewerSerial.cpp`. Inside the anonymous namespace, before
`taskBody`, add :
```cpp
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
  if (prio == LOW && freeSlots < (QUEUE_DEPTH * 3 / 10)) return;

  xQueueSend(s_queue, &ev, 0);  // timeout 0 = drop si pleine
}
```

Then, AFTER the `bool isConnected()` definition (outside the anonymous namespace,
inside `namespace viewer { ... }`), add :
```cpp
void emitPot(const char* slot, const char* target, const char* valueStr, const char* unit) {
  #if DEBUG_SERIAL
  if (unit && unit[0] != '\0') {
    emit(LOW, "[POT] %s: %s=%s %s\n", slot, target, valueStr, unit);
  } else {
    emit(LOW, "[POT] %s: %s=%s\n", slot, target, valueStr);
  }
  #else
  (void)slot; (void)target; (void)valueStr; (void)unit;
  #endif
}
```

- [ ] **Step 3: Migrate handlePotPipeline CC/PB sites in `src/main.cpp`**

Locate lines 1198-1218 :
```cpp
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
```

Replace by :
```cpp
    uint8_t ccNum, ccVal;
    while (s_potRouter.consumeCC(ccNum, ccVal)) {
      s_transport.sendCC(potSlot.channel, ccNum, ccVal);
      char ccTarget[8]; snprintf(ccTarget, sizeof(ccTarget), "CC%u", ccNum);
      char ccValueStr[8]; snprintf(ccValueStr, sizeof(ccValueStr), "%u", ccVal);
      viewer::emitPot(potSlotName(s_potRouter.getSlotForCcNumber(ccNum, ccCurType)),
                      ccTarget, ccValueStr, nullptr);
    }
    uint16_t pbVal;
    if (s_potRouter.consumePitchBend(pbVal)) {
      s_transport.sendPitchBend(potSlot.channel, pbVal);
      char pbValueStr[8]; snprintf(pbValueStr, sizeof(pbValueStr), "%u", pbVal);
      viewer::emitPot(potSlotName(s_potRouter.getSlotForTarget(TARGET_MIDI_PITCHBEND, ccCurType)),
                      "PB", pbValueStr, nullptr);
    }
```

- [ ] **Step 4: Migrate debugOutput() 16 sites in `src/main.cpp`**

Locate lines 1276-1413 (the `debugOutput()` function with 16 `Serial.printf` for
pot params).

Replace **each** printf inside `#if DEBUG_SERIAL` with a `viewer::emitPot()` call.
Pattern by example for `[POT] %s: Shape=%.2f` (L1330) :

Before :
```cpp
    if (s_firstEmit || (int)(shape * 100) != (int)(s_dbgShape * 100)) {
      Serial.printf("[POT] %s: Shape=%.2f\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_RESPONSE_SHAPE, curType)), shape);
      s_dbgShape = shape;
    }
```

After :
```cpp
    if (s_firstEmit || (int)(shape * 100) != (int)(s_dbgShape * 100)) {
      char valStr[16]; snprintf(valStr, sizeof(valStr), "%.2f", shape);
      viewer::emitPot(potSlotName(s_potRouter.getSlotForTarget(TARGET_RESPONSE_SHAPE, curType)),
                      "Shape", valStr, nullptr);
      s_dbgShape = shape;
    }
```

Apply this pattern to **all 16 sites** : Shape, Slew, AT_Deadzone, Tempo (with
"BPM" unit), LED_Bright, PadSens, BaseVel, VelVar, PitchBend, Gate, ShufDepth,
Division (s_divNames lookup), Pattern (s_patNames lookup), GenPos, ShufTpl.

For integer values, use `snprintf(valStr, sizeof(valStr), "%u", val)`.
For float values, use `snprintf(valStr, sizeof(valStr), "%.2f", val)`.
For string values (Division, Pattern), use `snprintf(valStr, sizeof(valStr), "%s", s_divNames[div])`.
For Tempo, pass unit "BPM" instead of nullptr.

**Note** : Le `#if DEBUG_SERIAL` autour de la fonction `debugOutput` peut rester
intact — `viewer::emitPot` est aussi gated DEBUG_SERIAL en interne, donc
double gating sans dommage.

- [ ] **Step 5: Compile gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: Exit 0.

- [ ] **Step 6: HW gate**

1. Flash + monitor.
2. Open viewer JUCE.
3. Tourner chaque pot Rx (R1, R1H, R2, R2H, R3, R3H, R4, R4H) → viewer
   reflète la valeur du target wirede au slot.
4. Tourner rear pot pour Tempo, LED_Bright, PadSens — viewer header reflète
   (les 3 fields).
5. Quick rotation d'un pot CC mapped → viewer reçoit les CC events sans
   blockage perceptible.
6. **Wait for user OK before commit.**

**Bug connu Phase 1.C.1 — LED_Bright + PadSens "stuck" au boot** :
race condition entre first-emit (boot) et task drain qui set
`s_viewerConnected`. Le `viewer::emit()` check `s_viewerConnected` AVANT
d'enqueue. Au premier `debugOutput()`, la task n'a pas encore eu son slot
CPU → atomic encore `false` → 16 events `[POT]` droppes silencieusement.

Conséquence visible : valeurs de pots **non touchés en live** (LED_Bright et
PadSens typiquement, configures au setup mode et jamais bouges) restent
invisibles cote viewer. Shape/Slew/Tempo/Gate/etc. apparaissent normalement
quand l'utilisateur tourne le pot correspondant.

**Resolution Phase 1.D** (Task 10) : l'auto-resync hook au task first-tick
(false→true transition) inclut `resetDbgSentinels()` qui force le prochain
`debugOutput()` a ré-émettre TOUS les params, peu importe la valeur. Le bug
disparait des que Phase 1.D commit.

**Workaround alternatif (NON applique)** : 1 ligne dans `viewer::begin()`
post-task-creation : `s_viewerConnected.store((bool)Serial,
std::memory_order_release);` — seed l'atomic synchronement avant que
`begin()` retourne. Utilisateur a choisi d'attendre Phase 1.D (B).

- [ ] **Step 7: Commit**

```bash
git add src/viewer/ViewerSerial.h src/viewer/ViewerSerial.cpp src/main.cpp
git commit -m "$(cat <<'EOF'
feat(viewer): Phase 1.C.1 migrate [POT] events vers ViewerSerial module

Ajout API publique viewer::emitPot(slot, target, valueStr, unit) +
helper interne emit() avec backpressure 70% sur LOW priority.

Migration de 18 sites Serial.printf -> viewer::emitPot :
- main.cpp:1205, 1214 (handlePotPipeline CC/PB)
- main.cpp:1330-1408 (debugOutput 16 sites : Shape, Slew, AT_Deadzone,
  Tempo, LED_Bright, PadSens, BaseVel, VelVar, PitchBend, Gate,
  ShufDepth, Division, Pattern, GenPos, ShufTpl)

[POT] events passent maintenant par la queue en LOW priority (droppable
sous backpressure). Format ASCII inchange pour le parser viewer. Le
first-emit guard s_dbg* existant est preserve - skip-if-unchanged
toujours actif.

Refs design spec §6/§7.
EOF
)"
```

---

## Task 5: Phase 1.C.2 — Migrate [BANK] + [STATE]

**Goal :** Déplacer `dumpBanksGlobal/dumpBankState/emitReady` de main.cpp vers
le module. Wire `BankManager::switchToBank` vers `viewer::emitBankSwitch`.

**Files:**
- Modify: `src/viewer/ViewerSerial.h` (add `emitBanksHeader`, `emitBank`,
  `emitState`, `emitReady`, `emitBankSwitch`)
- Modify: `src/viewer/ViewerSerial.cpp` (move logic from main.cpp)
- Modify: `src/main.cpp` (remove local dump fns, call module instead)
- Modify: `src/managers/BankManager.cpp` (use `viewer::emitBankSwitch`)

- [ ] **Step 1: Add emit functions to module .h**

Modify `src/viewer/ViewerSerial.h`. Add after `emitPot` declaration :
```cpp
// --- Phase 1.C.2 : [BANK]/[STATE]/[READY] events ---
// Boot dump uses these directly. Runtime bank switch uses emitBankSwitch.
void emitBanksHeader(uint8_t count);          // [BANKS] count=N
void emitBank(uint8_t idx);                    // [BANK] idx=N type=... (reads s_banks)
void emitState(uint8_t bankIdx);               // [STATE] bank=N ... (reads s_banks + s_potRouter)
void emitReady(uint8_t currentBank1Based);     // [READY] current=N
void emitBankSwitch(uint8_t newBankIdx);       // [BANK] Bank N + [STATE] bank=N
```

- [ ] **Step 2: Move dumpBanksGlobal/dumpBankState/emitReady logic to module .cpp**

The bodies of `dumpBanksGlobal` (main.cpp:166-198), `formatTargetValueForBank`
(main.cpp:203-286), and `dumpBankState` (main.cpp:291-348) get moved into
`src/viewer/ViewerSerial.cpp` with the following adaptation :

- Replace each `Serial.printf(...)` with a `char buf[256] + snprintf` followed
  by an `emit(HIGH, "%s", buf)` call.
- Or simpler : build the full line in a local buffer per-bank, then one emit
  call per logical event (one for `[BANKS]`, one per `[BANK]`, one per `[STATE]`).

The module needs access to firmware globals. Declare extern references at the
top of ViewerSerial.cpp :
```cpp
// Forward declarations of firmware globals (defined in main.cpp)
#include "../core/KeyboardData.h"   // BankSlot, BankType, NUM_BANKS
#include "../arp/ArpEngine.h"
#include "../managers/PotRouter.h"
#include "../managers/NvsManager.h"
#include "../managers/BankManager.h"

extern BankSlot       s_banks[NUM_BANKS];
extern PotRouter      s_potRouter;
extern NvsManager     s_nvsManager;
extern BankManager    s_bankManager;
```

Implementation of `emitBanksHeader` :
```cpp
void emitBanksHeader(uint8_t count) {
  #if DEBUG_SERIAL
  emit(HIGH, "[BANKS] count=%u\n", count);
  #endif
}
```

Implementation of `emitBank(idx)` (replicates dumpBanksGlobal body for one bank):
```cpp
void emitBank(uint8_t idx) {
  #if DEBUG_SERIAL
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
    n += snprintf(line + n, sizeof(line) - n, " division=%s playing=%s",
                  dIdx < 9 ? DIV_NAMES[dIdx] : "?",
                  b.arpEngine->isPlaying() ? "true" : "false");
    if (b.type == BANK_ARPEG) {
      n += snprintf(line + n, sizeof(line) - n, " octave=%d", b.arpEngine->getOctaveRange());
    } else {
      n += snprintf(line + n, sizeof(line) - n, " mutationLevel=%d", b.arpEngine->getMutationLevel());
    }
  }
  snprintf(line + n, sizeof(line) - n, "\n");
  emit(HIGH, "%s", line);
  #endif
}
```

Implementation of `emitState(bankIdx)` — replicates dumpBankState body, building
the line into a buffer before `emit(HIGH, ...)`.

Implementation of `emitReady` :
```cpp
void emitReady(uint8_t currentBank1Based) {
  #if DEBUG_SERIAL
  emit(HIGH, "[READY] current=%u\n", currentBank1Based);
  #endif
}
```

Implementation of `emitBankSwitch` :
```cpp
void emitBankSwitch(uint8_t newBankIdx) {
  #if DEBUG_SERIAL
  static const char* TYPE_NAMES[] = { "NORMAL", "ARPEG", "LOOP", "ARPEG_GEN" };
  if (newBankIdx >= NUM_BANKS) return;
  BankSlot& b = s_banks[newBankIdx];
  uint8_t typeIdx = (uint8_t)b.type;
  const char* typeName = (typeIdx < 4) ? TYPE_NAMES[typeIdx] : "?";
  emit(HIGH, "[BANK] Bank %d (ch %d, %s)\n", newBankIdx + 1, newBankIdx + 1, typeName);
  emitState(newBankIdx);
  #endif
}
```

For `formatTargetValueForBank` : move it to ViewerSerial.cpp as a static
internal helper used by `emitState`. Full body copied from main.cpp:203-286
unchanged (no Serial.printf inside — just string formatting).

- [ ] **Step 3: Remove old `dumpBanksGlobal/dumpBankState/emitReady` from `src/main.cpp`**

Delete lines 166-198 (`dumpBanksGlobal`), 203-286 (`formatTargetValueForBank`),
291-348 (`dumpBankState`), 450-456 (`emitReady`). These are now in the module.

- [ ] **Step 4: Update boot dump call in main.cpp setup()**

Locate lines 843-849 :
```cpp
  #if DEBUG_SERIAL
  Serial.println("[BOOT] Ready.");
  viewer::begin();
  dumpBanksGlobal();
  for (uint8_t i = 0; i < NUM_BANKS; i++) dumpBankState(i);
  emitReady();
  #endif
```

Replace by :
```cpp
  #if DEBUG_SERIAL
  Serial.println("[BOOT] Ready.");
  viewer::begin();
  viewer::emitBanksHeader(NUM_BANKS);
  for (uint8_t i = 0; i < NUM_BANKS; i++) viewer::emitBank(i);
  for (uint8_t i = 0; i < NUM_BANKS; i++) viewer::emitState(i);
  viewer::emitReady(s_bankManager.getCurrentBank() + 1);
  #endif
```

- [ ] **Step 5: Update BankManager.cpp**

Modify `src/managers/BankManager.cpp` lines 228-240. Replace :
```cpp
  #if DEBUG_SERIAL
  const char* typeLabel = "?";
  switch (_banks[_currentBank].type) {
    case BANK_NORMAL:    typeLabel = "NORMAL";    break;
    case BANK_ARPEG:     typeLabel = "ARPEG";     break;
    case BANK_ARPEG_GEN: typeLabel = "ARPEG_GEN"; break;
    case BANK_LOOP:      typeLabel = "LOOP";      break;
    default:             typeLabel = "?";         break;
  }
  Serial.printf("[BANK] Bank %d (ch %d, %s)\n",
                _currentBank + 1, _currentBank + 1, typeLabel);
  dumpBankState(_currentBank);
  #endif
```

with :
```cpp
  #if DEBUG_SERIAL
  viewer::emitBankSwitch(_currentBank);
  #endif
```

Add `#include "../viewer/ViewerSerial.h"` to BankManager.cpp at the top.

Remove the `extern void dumpBankState(uint8_t bankIdx);` declaration at L13 (no
longer needed since the module owns it).

- [ ] **Step 6: Handle the `pollRuntimeCommands` dump calls in main.cpp**

Locate `pollRuntimeCommands()` definition lines 461-495. The internal calls
`dumpBanksGlobal()`, `dumpBankState(i)`, `emitReady()` need to be replaced :

```cpp
      if (strcmp(cmdBuf, "?STATE") == 0) {
        viewer::emitState(s_bankManager.getCurrentBank());
        viewer::emitReady(s_bankManager.getCurrentBank() + 1);
      } else if (strcmp(cmdBuf, "?BANKS") == 0) {
        viewer::emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) viewer::emitBank(i);
        viewer::emitReady(s_bankManager.getCurrentBank() + 1);
      } else if (strcmp(cmdBuf, "?BOTH") == 0) {
        viewer::emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) viewer::emitBank(i);
        for (uint8_t i = 0; i < NUM_BANKS; i++) viewer::emitState(i);
        viewer::emitReady(s_bankManager.getCurrentBank() + 1);
      } else if (strcmp(cmdBuf, "?ALL") == 0) {
        viewer::emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) viewer::emitBank(i);
        for (uint8_t i = 0; i < NUM_BANKS; i++) viewer::emitState(i);
        dumpLedSettings();
        dumpColorSlots();
        dumpPotMapping();
        viewer::emitReady(s_bankManager.getCurrentBank() + 1);
      }
```

Note: `dumpLedSettings/Colors/PotMapping` stay in main.cpp (they emit
`[LED_DUMP]`/`[COLORS_DUMP]`/`[POTMAP_DUMP]` blocks — not parser-recognized,
debug only, per spec §16).

- [ ] **Step 7: Compile gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: Exit 0.

- [ ] **Step 8: HW gate**

1. Flash + monitor.
2. Boot — verify `[BANKS] count=8 + 8× [BANK] + 8× [STATE] + [READY]` appears.
3. Open viewer JUCE — verify hydration normale.
4. Switch banks via LEFT + bank pad — verify viewer foreground change + cells
   update for new bank.
5. Click Resync viewer (sends `?BOTH`) — verify full re-hydration.
6. **Wait for user OK before commit.**

- [ ] **Step 9: Commit**

```bash
git add src/viewer/ViewerSerial.h src/viewer/ViewerSerial.cpp \
        src/main.cpp src/managers/BankManager.cpp
git commit -m "$(cat <<'EOF'
feat(viewer): Phase 1.C.2 migrate [BANK]/[STATE]/[READY] vers module

Move dumpBanksGlobal/dumpBankState/formatTargetValueForBank/emitReady
de main.cpp vers src/viewer/ViewerSerial.cpp. Nouvelle API :
- viewer::emitBanksHeader(count)
- viewer::emitBank(idx)
- viewer::emitState(bankIdx)
- viewer::emitReady(currentBank1Based)
- viewer::emitBankSwitch(newBankIdx) = [BANK] Bank N + emitState

Migration sites :
- main.cpp setup() boot dump : appel module direct
- BankManager.cpp:237 switchToBank : viewer::emitBankSwitch
- main.cpp pollRuntimeCommands : dispatch via module emit*

Helpers [LED_DUMP]/[COLORS_DUMP]/[POTMAP_DUMP] restent en main.cpp
(debug uniquement, pas parser-recognized).

Refs design spec §8, §16.
EOF
)"
```

---

## Task 6: Phase 1.C.3 — Migrate [ARP] + [GEN]

**Files:**
- Modify: `src/viewer/ViewerSerial.h` (add `emitArp*` + `emitGenSeed`)
- Modify: `src/viewer/ViewerSerial.cpp` (impl)
- Modify: `src/arp/ArpEngine.cpp` (8 sites)

- [ ] **Step 1: Add API in module .h**

```cpp
// --- Phase 1.C.3 : [ARP]/[GEN] events ---
// kind : "+note", "-note" — for pile changes. count = pile size.
void emitArpNoteAdd(uint8_t bankIdx, uint8_t pileCount);
void emitArpNoteRemove(uint8_t bankIdx, uint8_t pileCount);
// Play/Stop variants
void emitArpPlay(uint8_t bankIdx, uint8_t pileCount, bool relaunchPaused);
void emitArpStop(uint8_t bankIdx, uint8_t pileCount);
void emitArpQueueFull();
// GEN seed (ARPEG_GEN). pileCount=1 triggers the degenerate form.
void emitGenSeed(uint16_t seqLen, uint8_t eInit, uint8_t pileCount,
                 int8_t lo, int8_t hi);
void emitGenSeedDegenerate(uint16_t seqLen, int8_t singleDegree);
```

- [ ] **Step 2: Add impl in module .cpp**

```cpp
void emitArpNoteAdd(uint8_t bankIdx, uint8_t pileCount) {
  #if DEBUG_SERIAL
  emit(LOW, "[ARP] Bank %d: +note (%d total)\n", bankIdx + 1, pileCount);
  #endif
}
void emitArpNoteRemove(uint8_t bankIdx, uint8_t pileCount) {
  #if DEBUG_SERIAL
  emit(LOW, "[ARP] Bank %d: -note (%d total)\n", bankIdx + 1, pileCount);
  #endif
}
void emitArpPlay(uint8_t bankIdx, uint8_t pileCount, bool relaunchPaused) {
  #if DEBUG_SERIAL
  if (relaunchPaused) {
    emit(HIGH, "[ARP] Bank %d: Play \xe2\x80\x94 relaunch paused pile (%d notes)\n",
         bankIdx + 1, pileCount);
  } else {
    emit(HIGH, "[ARP] Bank %d: Play (pile %d notes)\n", bankIdx + 1, pileCount);
  }
  #endif
}
void emitArpStop(uint8_t bankIdx, uint8_t pileCount) {
  #if DEBUG_SERIAL
  emit(HIGH, "[ARP] Bank %d: Stop \xe2\x80\x94 pile kept (%d notes)\n",
       bankIdx + 1, pileCount);
  #endif
}
void emitArpQueueFull() {
  #if DEBUG_SERIAL
  emit(LOW, "[ARP] WARNING: Event queue full \xe2\x80\x94 event dropped\n");
  #endif
}
void emitGenSeed(uint16_t seqLen, uint8_t eInit, uint8_t pileCount,
                 int8_t lo, int8_t hi) {
  #if DEBUG_SERIAL
  emit(LOW, "[GEN] seed seqLen=%u E_init=%u pile=%u lo=%d hi=%d\n",
       seqLen, eInit, pileCount, lo, hi);
  #endif
}
void emitGenSeedDegenerate(uint16_t seqLen, int8_t singleDegree) {
  #if DEBUG_SERIAL
  emit(LOW, "[GEN] seed seqLen=%u (pile=1 note %d, repetition)\n",
       seqLen, singleDegree);
  #endif
}
```

**Note** : Les em-dash UTF-8 `\xe2\x80\x94` doivent rester identiques pour
préserver la reconnaissance parser (spec §3.6).

- [ ] **Step 3: Migrate ArpEngine.cpp sites**

Add `#include "../viewer/ViewerSerial.h"` at top of `src/arp/ArpEngine.cpp`.

L323 (seedSequenceGen pile=1) :
```cpp
    viewer::emitGenSeedDegenerate(seqLen, d);
```

L344 (seedSequenceGen normal) :
```cpp
  viewer::emitGenSeed(seqLen, eInit, _pileDegreeCount, _pileLo, _pileHi);
```

L436 (addPadPosition) :
```cpp
  viewer::emitArpNoteAdd(_channel, _positionCount);
```

L473 (removePadPosition) — note : conditionally emitted only if `found` :
```cpp
  if (found) {
    viewer::emitArpNoteRemove(_channel, _positionCount);
  }
```

L536 (Play relaunch) :
```cpp
      viewer::emitArpPlay(_channel, _positionCount, /*relaunchPaused*/ true);
```

L540 (Play normal) :
```cpp
      viewer::emitArpPlay(_channel, _positionCount, /*relaunchPaused*/ false);
```

L556 (Stop) :
```cpp
    viewer::emitArpStop(_channel, _positionCount);
```

L896 (queue full) :
```cpp
  viewer::emitArpQueueFull();
```

Remove the surrounding `#if DEBUG_SERIAL ... #endif` blocks (the module is
gated internally).

- [ ] **Step 4: Compile gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: Exit 0.

- [ ] **Step 5: HW gate**

1. Flash + monitor + viewer.
2. Switch to ARPEG bank.
3. Press ARPEG hold pad — viewer shows Play state.
4. Press 3 ARPEG pads — viewer pile counter goes 1, 2, 3.
5. Press hold pad again — viewer shows Stop, pile preserved at 3.
6. Press hold pad — Play with relaunch, viewer shows "Play (3 notes)".
7. Switch to ARPEG_GEN bank, add a pad — verify `[GEN] seed` arrives (visible
   in viewer event log or serial monitor side-by-side).
8. **Wait for user OK before commit.**

- [ ] **Step 6: Commit**

```bash
git add src/viewer/ViewerSerial.h src/viewer/ViewerSerial.cpp src/arp/ArpEngine.cpp
git commit -m "$(cat <<'EOF'
feat(viewer): Phase 1.C.3 migrate [ARP]/[GEN] events vers module

Nouvelle API viewer:: pour les 8 sites ArpEngine.cpp :
- emitArpNoteAdd / emitArpNoteRemove (LOW droppable, pas de coalesce)
- emitArpPlay (avec flag relaunchPaused)
- emitArpStop
- emitArpQueueFull
- emitGenSeed / emitGenSeedDegenerate

Em-dash UTF-8 \xe2\x80\x94 preserve dans les format strings pour
parser-recognition (Play -- relaunch, Stop -- pile kept).

Pas de coalescing 50ms applique (decision Q7b uniformisee aux ARP
events). +/-note en LOW, droppable sous backpressure. Reversible si
usage live revele un besoin.

Refs design spec §7, §16.
EOF
)"
```

---

## Task 7: Phase 1.C.4 — Migrate [SCALE] + [ARP_GEN] octave

**Files:**
- Modify: `src/viewer/ViewerSerial.h` (add `emitScale`, `emitArpOctave`,
  `emitArpGenMutation`)
- Modify: `src/viewer/ViewerSerial.cpp` (impl)
- Modify: `src/managers/ScaleManager.cpp` (5 sites)

- [ ] **Step 1: Add API in module .h**

```cpp
// --- Phase 1.C.4 : [SCALE]/[ARP_GEN] events ---
enum ScaleEventKind : uint8_t {
  SCALE_ROOT, SCALE_MODE, SCALE_CHROMATIC,
};
void emitScale(ScaleEventKind kind, uint8_t rootIdx, uint8_t modeIdx);
void emitArpOctave(uint8_t octave);                   // [ARP] Octave N
void emitArpGenMutation(uint8_t mutationLevel);       // [ARP_GEN] MutationLevel N
```

- [ ] **Step 2: Add impl in module .cpp**

```cpp
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
      emit(HIGH, "[SCALE] Root %s (mode %s)\n", root, mode);
      break;
    case SCALE_MODE:
      emit(HIGH, "[SCALE] Mode %s (root %s)\n", mode, root);
      break;
    case SCALE_CHROMATIC:
      emit(HIGH, "[SCALE] Chromatic (root %s)\n", root);
      break;
  }
  #endif
}
void emitArpOctave(uint8_t octave) {
  #if DEBUG_SERIAL
  emit(HIGH, "[ARP] Octave %d\n", octave);
  #endif
}
void emitArpGenMutation(uint8_t mutationLevel) {
  #if DEBUG_SERIAL
  emit(HIGH, "[ARP_GEN] MutationLevel %d\n", mutationLevel);
  #endif
}
```

- [ ] **Step 3: Migrate ScaleManager.cpp sites**

Add `#include "../viewer/ViewerSerial.h"` at top.

L140 (root) :
```cpp
      viewer::emitScale(viewer::SCALE_ROOT, r, slot.scale.mode);
```

L162 (mode) :
```cpp
      viewer::emitScale(viewer::SCALE_MODE, slot.scale.root, m);
```

L180 (chromatic) :
```cpp
      viewer::emitScale(viewer::SCALE_CHROMATIC, slot.scale.root, slot.scale.mode);
```

L206 (mutation) :
```cpp
          viewer::emitArpGenMutation(o + 1);
```

L211 (octave) :
```cpp
          viewer::emitArpOctave(o + 1);
```

Remove the surrounding `#if DEBUG_SERIAL ... #endif` blocks. Note: the
`#if DEBUG_SERIAL` block at the top of ScaleManager.cpp (L7-14) defining
`ROOT_NAMES`/`MODE_NAMES` devient unused **après** migration des 3 sites
[SCALE] — supprimer ce bloc explicitement à ce moment-là :
```cpp
// Remove these lines from ScaleManager.cpp:
//   #if DEBUG_SERIAL
//   static const char* ROOT_NAMES[7] = {"A", "B", "C", "D", "E", "F", "G"};
//   static const char* MODE_NAMES[7] = {
//     "Ionian", "Dorian", "Phrygian", "Lydian",
//     "Mixolydian", "Aeolian", "Locrian"
//   };
//   #endif
```
Les tables vivent désormais uniquement dans ViewerSerial.cpp `emitScale()`.

- [ ] **Step 4: Compile + HW + commit**

Compile :
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

HW gate :
1. Press root pad (LEFT + pad 8) → viewer scale root changes.
2. Press mode pad → viewer mode changes.
3. Press chromatic pad → viewer enters chromatic.
4. Press octave pad on ARPEG bank → viewer arp octave changes.
5. Switch to ARPEG_GEN bank, press octave pad → viewer mutation level changes.
6. Wait user OK.

Commit :
```bash
git add src/viewer/ViewerSerial.h src/viewer/ViewerSerial.cpp src/managers/ScaleManager.cpp
git commit -m "$(cat <<'EOF'
feat(viewer): Phase 1.C.4 migrate [SCALE]/[ARP]/[ARP_GEN] octave events

API viewer::emitScale(kind, root, mode) + emitArpOctave +
emitArpGenMutation. Migration 5 sites ScaleManager.cpp.

ROOT_NAMES/MODE_NAMES lookup deplace dans le module (ne duplique pas
celui de ScaleManager.cpp dont le #if DEBUG_SERIAL header est supprime).

All scale/octave events sont HIGH priority - changements rares, doivent
arriver au viewer pour synchronisation header + cells.

Refs design spec §8.
EOF
)"
```

---

## Task 8: Phase 1.C.5 — Migrate [CLOCK] source + [MIDI] transport

**Files:**
- Modify: `src/viewer/ViewerSerial.h` (add `emitClockSource`, `emitMidiTransport`)
- Modify: `src/viewer/ViewerSerial.cpp` (impl)
- Modify: `src/midi/ClockManager.cpp` (5 sites)
- Modify: `src/core/MidiTransport.cpp` (3 sites)

- [ ] **Step 1: Add API in module .h**

```cpp
// --- Phase 1.C.5 : [CLOCK]/[MIDI] events ---
// srcLabel : "USB", "BLE", "internal", "BLE (USB timed out)",
//            "internal (no external BPM)", "internal (last known timed out)"
// bpm : > 0.0f means include "last known (X BPM)" form, 0 means just source name
void emitClockSource(const char* srcLabel, float bpm);
// transport : "USB" or "BLE". state : "connected" or "disconnected"
void emitMidiTransport(const char* transport, const char* state);
```

- [ ] **Step 2: Add impl in module .cpp**

```cpp
void emitClockSource(const char* srcLabel, float bpm) {
  #if DEBUG_SERIAL
  if (bpm > 0.0f) {
    emit(HIGH, "[CLOCK] Source: last known (%.0f BPM)\n", bpm);
  } else {
    emit(HIGH, "[CLOCK] Source: %s\n", srcLabel);
  }
  #endif
}
void emitMidiTransport(const char* transport, const char* state) {
  #if DEBUG_SERIAL
  emit(HIGH, "[MIDI] %s %s\n", transport, state);
  #endif
}
```

- [ ] **Step 3: Migrate ClockManager.cpp sites**

Add `#include "../viewer/ViewerSerial.h"` at top.

L98 (USB/BLE active source) :
```cpp
      viewer::emitClockSource(_activeSource == SRC_USB ? "USB" : "BLE", 0.0f);
```

L135 (BLE fallback from USB) :
```cpp
          viewer::emitClockSource("BLE (USB timed out)", 0.0f);
```

L156 (last known) :
```cpp
        viewer::emitClockSource("last known", _pllBPM);  // bpm > 0 → format with BPM
```

L161 (internal no external) :
```cpp
        viewer::emitClockSource("internal (no external BPM)", 0.0f);
```

L175 (internal last known timed out) :
```cpp
    viewer::emitClockSource("internal (last known timed out)", 0.0f);
```

Remove the surrounding `#if DEBUG_SERIAL ... #endif` blocks.

**Note** : `[CLOCK] ClockManager initialized (internal clock).` at L24 stays
raw Serial.print — émis en `begin()` AVANT `viewer::begin()` (le module n'existe
pas encore).

- [ ] **Step 4: Migrate MidiTransport.cpp runtime sites**

Add `#include "../viewer/ViewerSerial.h"` at top.

L62 (BLE connected) :
```cpp
  viewer::emitMidiTransport("BLE", "connected");
```

L69 (BLE disconnected) :
```cpp
  viewer::emitMidiTransport("BLE", "disconnected");
```

L123 (USB connect/disconnect) :
```cpp
      viewer::emitMidiTransport("USB", mounted ? "connected" : "disconnected");
```

Remove the surrounding `#if DEBUG_SERIAL ... #endif` blocks.

- [ ] **Step 5: Compile + HW + commit**

Compile.

HW gate :
1. Plug USB cable connecting Mac with USBMIDI input — viewer header USB indicator ON.
2. Unplug — indicator OFF.
3. BLE MIDI app on phone, connect to ILLPAD — viewer BLE indicator ON.
4. Disconnect BLE app — BLE indicator OFF.
5. Start firmware with external clock from DAW USB → viewer "Source: USB".
6. Stop DAW → viewer "Source: last known (X BPM)" puis "internal" après 5s.
7. Wait user OK.

Commit :
```bash
git add src/viewer/ViewerSerial.h src/viewer/ViewerSerial.cpp \
        src/midi/ClockManager.cpp src/core/MidiTransport.cpp
git commit -m "feat(viewer): Phase 1.C.5 migrate [CLOCK] source + [MIDI] transport vers module"
```

---

## Task 9: Phase 1.C.6 — Migrate [PANIC]

**Files:**
- Modify: `src/viewer/ViewerSerial.h` (add `emitPanic`)
- Modify: `src/viewer/ViewerSerial.cpp` (impl)
- Modify: `src/main.cpp` (migrate midiPanic L151)

- [ ] **Step 1: Add API + impl**

H :
```cpp
// --- Phase 1.C.6 : [PANIC] event ---
void emitPanic();
```

Cpp :
```cpp
void emitPanic() {
  #if DEBUG_SERIAL
  emit(HIGH, "[PANIC] All notes off on all channels\n");
  #endif
}
```

- [ ] **Step 2: Migrate main.cpp midiPanic L151**

```cpp
  viewer::emitPanic();
```

Remove `#if DEBUG_SERIAL ... #endif` around it.

- [ ] **Step 3: Compile + HW + commit**

Compile.

HW : triple-click rear button → viewer overlay rouge 2.5s.

Commit :
```bash
git add src/viewer/ViewerSerial.h src/viewer/ViewerSerial.cpp src/main.cpp
git commit -m "feat(viewer): Phase 1.C.6 migrate [PANIC] vers module"
```

---

## Task 10: Phase 1.D — [GLOBALS] + [SETTINGS] + sentinel reset + pollCommands

**Goal :** Nouveau code — pas de migration. Ajout des events `[GLOBALS]` et
`[SETTINGS]` au boot dump + `?BOTH/?ALL` + auto-resync. Reset des sentinels
`s_dbg*` sur Resync. Migration finale de `pollRuntimeCommands` vers le module.

**Files:**
- Modify: `src/midi/ClockManager.h` (add `getActiveSourceLabel`)
- Modify: `src/midi/ClockManager.cpp` (impl)
- Modify: `src/main.cpp` (promote `s_settings` to global, add `resetDbgSentinels`)
- Modify: `src/viewer/ViewerSerial.h` (add `emitGlobals`, `emitSettings`,
  remove `pollCommands` stub indicator)
- Modify: `src/viewer/ViewerSerial.cpp` (full pollCommands impl + emitGlobals +
  emitSettings + auto-resync wiring)

- [ ] **Step 1: Add `ClockManager::getActiveSourceLabel()`**

Modify `src/midi/ClockManager.h`. Add public method declaration after
`bool isExternalSync() const;` :
```cpp
  const char* getActiveSourceLabel() const;
```

Modify `src/midi/ClockManager.cpp`. Add at end of file :
```cpp
const char* ClockManager::getActiveSourceLabel() const {
  switch (_activeSource) {
    case SRC_USB:        return "usb";
    case SRC_BLE:        return "ble";
    case SRC_LAST_KNOWN: return "last";
    case SRC_INTERNAL:
    default:             return "internal";
  }
}
```

- [ ] **Step 2: Promote globals to external linkage in main.cpp**

**Problème de linkage à résoudre** : ViewerSerial.cpp doit lire 5 globals
définis dans main.cpp (`s_banks`, `s_potRouter`, `s_nvsManager`, `s_bankManager`,
`s_clockManager`) plus `s_settings`. Or main.cpp:50-65 les déclare tous `static`
(internal linkage) — `extern` ne peut pas y accéder, link fail garantie.

Fix : **retirer le mot-clé `static`** de ces 5 globals + ajouter `s_settings`
au file-scope sans `static`. Les autres globals (s_keyboard, s_transport,
s_midiEngine, s_leds, s_scaleManager, s_batteryMonitor, s_controlPadManager,
s_setupManager, s_arpEngines) gardent leur `static` (pas référencés par
ViewerSerial).

Modifier `src/main.cpp` lignes 50-65 :

AVANT :
```cpp
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
// ...
static BankSlot s_banks[NUM_BANKS];
```

APRÈS :
```cpp
static CapacitiveKeyboard s_keyboard;
static MidiTransport      s_transport;
static MidiEngine         s_midiEngine;
static LedController      s_leds;
       BankManager        s_bankManager;       // external linkage (ViewerSerial reads)
static ScaleManager       s_scaleManager;
       PotRouter          s_potRouter;         // external linkage (ViewerSerial reads)
static BatteryMonitor     s_batteryMonitor;
       NvsManager         s_nvsManager;        // external linkage (ViewerSerial reads)
static ControlPadManager  s_controlPadManager;
static SetupManager       s_setupManager;
       ClockManager       s_clockManager;      // external linkage (ViewerSerial reads)
// ...
       BankSlot s_banks[NUM_BANKS];             // external linkage (ViewerSerial reads)
```

Ajouter en plus la déclaration `s_settings` (qui était locale dans setup) au
file-scope, sans `static` :
```cpp
       SettingsStore s_settings;                // external linkage (ViewerSerial reads)
```

Dans setup() à L675, supprimer la déclaration locale `SettingsStore s_settings;`.
Le `loadAll(... s_settings)` utilise la global automatiquement.

**Rationale** : C'est un override conscient de la convention `static` projet,
documenté ici. Justification : aucun couplage circulaire (le module dépend de
main.cpp à sens unique), pas de risque de double définition (un seul TU
définit les symboles), et c'est le pattern accepté en C/C++ pour des globaux
partagés sans fichier d'en-tête dédié.

- [ ] **Step 3: Add `resetDbgSentinels()` function in main.cpp**

The `s_dbg*` sentinels in `debugOutput()` are function-local statics. To reset
them externally, refactor : either move them to file scope OR add a function
that flips an internal flag.

Cleanest : add a file-scope `std::atomic<bool> s_forceNextEmitAll{false};` and modify
`debugOutput()` to check it.

**Vérifier que `#include <atomic>` est présent en haut de main.cpp** (pour
`std::atomic`). Si pas déjà inclus, l'ajouter avec les autres includes système
en début de fichier :
```cpp
#include <atomic>
```

Add near other file-scope globals in main.cpp (PAS static — pas d'extern requis
depuis ailleurs, mais on garde local-linkage uniquement si la fonction
`resetDbgSentinels` reste dans main.cpp) :
```cpp
static std::atomic<bool> s_forceNextEmitAll{false};
```

Add a public reset function :
```cpp
// Called by viewer module on ?BOTH/?ALL/?STATE to force re-emit of all [POT] params.
void resetDbgSentinels() {
  s_forceNextEmitAll.store(true, std::memory_order_release);
}
```

Modify `debugOutput()` near `static bool s_firstEmit = true;` (line ~1299) :
```cpp
    static bool s_firstEmit = true;
    bool forceEmit = s_firstEmit || s_forceNextEmitAll.exchange(false, std::memory_order_acq_rel);
```

Then in all 16 conditions, replace `s_firstEmit ||` with `forceEmit ||`. Example :
```cpp
    if (forceEmit || (int)(shape * 100) != (int)(s_dbgShape * 100)) {
```

At end of debugOutput() before the closing brace : `s_firstEmit = false;` stays.

- [ ] **Step 4: Add `emitGlobals` + `emitSettings` to module**

ViewerSerial.h :
```cpp
// --- Phase 1.D : [GLOBALS]/[SETTINGS] events ---
void emitGlobals();
void emitSettings();
```

ViewerSerial.cpp — add extern refs :
```cpp
extern SettingsStore s_settings;
extern ClockManager  s_clockManager;
```

Then :
```cpp
void emitGlobals() {
  #if DEBUG_SERIAL
  emit(HIGH, "[GLOBALS] Tempo=%u LED_Bright=%u PadSens=%u ClockSource=%s\n",
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
  emit(HIGH, "[SETTINGS] ClockMode=%s PanicReconnect=%u DoubleTapMs=%u "
             "AftertouchRate=%u BleInterval=%u BatAdcFull=%u\n",
       s_settings.clockMode == CLOCK_MASTER ? "master" : "slave",
       s_settings.panicOnReconnect,
       s_settings.doubleTapMs,
       s_settings.aftertouchRate,
       s_settings.bleInterval,
       s_settings.batAdcAtFull);
  #endif
}
```

- [ ] **Step 5: Migrate pollRuntimeCommands fully into module**

In `src/viewer/ViewerSerial.cpp`, expand `pollCommands()` :

```cpp
extern void resetDbgSentinels();  // from main.cpp
extern void dumpLedSettings();
extern void dumpColorSlots();
extern void dumpPotMapping();

void pollCommands() {
  static char  cmdBuf[16];
  static uint8_t cmdLen = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      cmdBuf[cmdLen] = '\0';
      if (strcmp(cmdBuf, "?STATE") == 0) {
        emitState(s_bankManager.getCurrentBank());
        emitReady(s_bankManager.getCurrentBank() + 1);
      } else if (strcmp(cmdBuf, "?BANKS") == 0) {
        emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
        emitReady(s_bankManager.getCurrentBank() + 1);
      } else if (strcmp(cmdBuf, "?BOTH") == 0) {
        emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
        emitGlobals();
        emitSettings();
        resetDbgSentinels();
        emitReady(s_bankManager.getCurrentBank() + 1);
      } else if (strcmp(cmdBuf, "?ALL") == 0) {
        emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
        emitGlobals();
        emitSettings();
        resetDbgSentinels();
        dumpLedSettings();
        dumpColorSlots();
        dumpPotMapping();
        emitReady(s_bankManager.getCurrentBank() + 1);
      } else if (cmdBuf[0] == '!') {
        // Phase 2 hook : write commands
        emit(HIGH, "[ERROR] write commands not yet implemented (Phase 2)\n");
      }
      cmdLen = 0;
    } else if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    } else {
      cmdLen = 0;  // overflow → discard
    }
  }
}
```

- [ ] **Step 6: Remove `pollRuntimeCommands` from main.cpp**

Delete lines 461-495 (`pollRuntimeCommands` definition).

In loop() at line ~1473, remove the call :
```cpp
  #if DEBUG_SERIAL
  pollRuntimeCommands();        // REMOVE THIS LINE
  viewer::pollCommands();
  #endif
```
keep only `viewer::pollCommands();`.

- [ ] **Step 7: Wire auto-resync in module task**

In `src/viewer/ViewerSerial.cpp`, expand the `taskBody` :

```cpp
void taskBody(void* /*arg*/) {
  QueuedEvent ev;
  for (;;) {
    bool nowConnected = (bool)Serial;
    bool wasConnected = s_viewerConnected.exchange(nowConnected, std::memory_order_acq_rel);

    if (!wasConnected && nowConnected) {
      // Auto-resync (X1) : silent re-dump comme un ?BOTH interne.
      emitBanksHeader(NUM_BANKS);
      for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
      for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
      emitGlobals();
      emitSettings();
      resetDbgSentinels();
      emitReady(s_bankManager.getCurrentBank() + 1);
    }

    // ... rest of drain logic same as before
  }
}
```

- [ ] **Step 8: Add `emitGlobals` + `emitSettings` to boot dump in main.cpp**

In setup() at line ~847-849 :
```cpp
  Serial.println("[BOOT] Ready.");
  viewer::begin();
  viewer::emitBanksHeader(NUM_BANKS);
  for (uint8_t i = 0; i < NUM_BANKS; i++) viewer::emitBank(i);
  for (uint8_t i = 0; i < NUM_BANKS; i++) viewer::emitState(i);
  viewer::emitGlobals();
  viewer::emitSettings();
  viewer::emitReady(s_bankManager.getCurrentBank() + 1);
```

- [ ] **Step 9: Compile + HW + commit**

Compile.

HW gate :
1. Boot firmware. Verify viewer header shows Tempo/LED_Bright/PadSens immediately
   (sans tourner aucun pot).
2. Verify ClockMode displays "slave" (or "master" if Tool 8 setting is master).
3. Disconnect viewer, reconnect. Verify auto-resync : viewer re-hydrates without
   needing Resync button.
4. Click Resync (?BOTH). Verify all repopulates including pot values.
5. Wait user OK.

Commit :
```bash
git add src/midi/ClockManager.h src/midi/ClockManager.cpp \
        src/main.cpp src/viewer/ViewerSerial.h src/viewer/ViewerSerial.cpp
git commit -m "$(cat <<'EOF'
feat(viewer): Phase 1.D [GLOBALS] + [SETTINGS] + sentinel reset + pollCommands

Resolution findings audit M1/M2/M3 :
- Nouveau event [GLOBALS] Tempo LED_Bright PadSens ClockSource au boot,
  auto-resync, ?BOTH/?ALL (G3 split runtime/persistent)
- Nouveau event [SETTINGS] ClockMode PanicReconnect DoubleTapMs
  AftertouchRate BleInterval BatAdcFull au boot, auto-resync, ?BOTH/?ALL
- resetDbgSentinels() force re-emit complet des [POT] params sur Resync
  (?BOTH/?ALL). Fix M3 - ?BOTH self-sufficient.
- ClockManager::getActiveSourceLabel() public accessor (internal/usb/
  ble/last). Fix M2.

Migration pollRuntimeCommands -> viewer::pollCommands :
- viewer::pollCommands owne le canal bidirectionnel complet
- Hook Phase 2 !CMD=val present (stub error response)
- s_settings promu file-scope global pour acces module

Auto-resync (X1) wired dans taskBody : transition false->true emet
banks + states + globals + settings + reset sentinels + ready.

Refs design spec §3.1, §3.4, §5, §12, §13.
EOF
)"
```

---

## Task 11: Phase 1.E — [CLOCK] BPM= debounced

**Goal :** Émettre la PLL BPM externe sur change ±1 BPM (audit R3).

**Files:**
- Modify: `src/viewer/ViewerSerial.h` (add `emitClockBpm`)
- Modify: `src/viewer/ViewerSerial.cpp` (impl)
- Modify: `src/midi/ClockManager.cpp` (debounced emit in `updatePLL`)

- [ ] **Step 1: Add API + impl in module**

H :
```cpp
// Phase 1.E : [CLOCK] BPM= event (external sync BPM update, debounced ±1 BPM)
void emitClockBpm(float bpm, const char* srcLabel);
```

Cpp :
```cpp
void emitClockBpm(float bpm, const char* srcLabel) {
  #if DEBUG_SERIAL
  emit(HIGH, "[CLOCK] BPM=%.0f src=%s\n", bpm, srcLabel);
  #endif
}
```

- [ ] **Step 2: Add debounced emit in ClockManager.cpp**

**Décision design** : `s_lastEmittedBpm` doit être **file-scope** (pas static
locale dans updatePLL) pour pouvoir être resetté lors d'un changement de
source. Sinon edge case : USB stabilisé à 100 BPM → switch BLE à 100 BPM → 
`|100-100| < 1` → aucun event émis, viewer ne voit jamais la source change
reflétée dans la BPM.

In `src/midi/ClockManager.cpp`, add at file-scope (after the existing `static`
ROOT_NAMES if any, or near top of file) :
```cpp
// Last BPM value emitted via viewer::emitClockBpm() — debounce reference.
// Reset to 0 on source change (processIncomingTicks) to force re-emit
// even si la BPM est identique entre les deux sources.
static float s_lastEmittedBpm = 0.0f;
```

In `updatePLL` (line ~205), add at end :
```cpp
void ClockManager::updatePLL(uint32_t intervalUs, uint8_t source) {
  // ... existing logic that updates _pllBPM ...

  // Debounced emit on ±1 BPM change (Phase 1.E, audit R3)
  #if DEBUG_SERIAL
  if (_activeSource == SRC_USB || _activeSource == SRC_BLE) {
    if (fabsf(_pllBPM - s_lastEmittedBpm) >= 1.0f) {
      viewer::emitClockBpm(_pllBPM, _activeSource == SRC_USB ? "usb" : "ble");
      s_lastEmittedBpm = _pllBPM;
    }
  }
  #endif
}
```

In `processIncomingTicks` (line ~66), au bloc de transition source change
(L93-100 où `_activeSource != prevSource`), ajouter le reset :
```cpp
    if (_activeSource != prevSource) {
      _prevTickUs = 0;
      _tickIntervalCount = 0;
      s_lastEmittedBpm = 0.0f;   // Force re-emit BPM même si valeur identique
      #if DEBUG_SERIAL
      viewer::emitClockSource(_activeSource == SRC_USB ? "USB" : "BLE", 0.0f);
      #endif
    }
```

Pareil pour les autres source transitions dans `resolveTimeouts` (L121-178) :
ajouter `s_lastEmittedBpm = 0.0f;` à chaque transition vers une nouvelle source.

Add `#include "../viewer/ViewerSerial.h"` at top of ClockManager.cpp (déjà
fait en Task 8 Step 3, donc déjà présent à ce stade).

- [ ] **Step 3: Compile + HW + commit**

Compile.

HW : DAW à 100 BPM, change à 130 BPM → viewer tempo widget reflète. Si pas de
source externe disponible, skip ce HW gate, note dans le commit message.

Commit :
```bash
git add src/viewer/ViewerSerial.h src/viewer/ViewerSerial.cpp src/midi/ClockManager.cpp
git commit -m "feat(viewer): Phase 1.E [CLOCK] BPM= debounced emit (audit R3)"
```

---

## Task 12: Phase 1.F — [FATAL] event verification + extension

**Goal :** Vérifier que `[FATAL]` event est bien parser-recognized + tester
overlay viewer.

Note : la promotion `[INIT] FATAL` → `[FATAL]` a été faite en Task 3 Step 4.
Cette Task 12 est juste la validation HW + ajout d'autres sites FATAL si jugé
utile.

**Files:** None additional (already done in Task 3).

- [ ] **Step 1: HW gate test fail-init**

Temporarily modify `src/main.cpp` line 524 to force fail :
```cpp
  bool kbOk = false;  // FORCE FAIL for HW gate test — REVERT after test
  // bool kbOk = s_keyboard.begin();
```

Compile, flash, reboot. Viewer should :
1. Receive `[FATAL] Keyboard init failed`.
2. Display critical overlay (red, persistent).
3. Header shows "Firmware FATAL".

- [ ] **Step 2: Restore main.cpp**

```cpp
  bool kbOk = s_keyboard.begin();
```

Re-compile, re-flash, verify normal boot.

- [ ] **Step 3: Commit (only if any code change is needed beyond Task 3)**

If no code change needed, no commit. Phase 1.F is just HW validation of Task 3
changes.

---

## Task 13: Phase 1.G — Sync spec viewer doc

**Files:**
- Modify: `../ILLPAD_V2-viewer/ILLPADViewer/docs/firmware-viewer-protocol.md`

**Note** : This is a doc edit in the viewer worktree. Cross-worktree commit.

- [ ] **Step 1: Open the spec doc in viewer worktree**

```bash
cd ../ILLPAD_V2-viewer
git status -sb       # verify on viewer-juce branch
```

- [ ] **Step 2: Update §1 catalogue with new events**

Add to the §1 events table :
- `[BOOT *]` family — informational boot debug, no parsing required, optional
  boot log panel UI.
- `[GLOBALS]` — runtime instantané (Tempo, LED_Bright, PadSens, ClockSource).
- `[SETTINGS]` — config NVS (ClockMode, PanicReconnect, DoubleTapMs, AftertouchRate,
  BleInterval, BatAdcFull).
- `[FATAL]` — critical failure, always-on, parser-recognized, persistent overlay.
- `[CLOCK] BPM=` — external sync BPM change, debounced ±1 BPM.
- `[SETUP]` mode marker — `[SETUP] Entering setup mode...` triggers viewer
  freeze + setup mode UI state.

- [ ] **Step 3: Mark §3.2 sentinel collision DONE**

Add note at top of §3.2 :
```
**STATUT** : ✅ FIXED par commit 6b86fcb sur firmware main. Le pattern
per-target first-emit flag (`static bool s_firstEmit = true;`) est appliqué
uniformément à tous les sentinels dans `debugOutput()`. Issue fermée.
```

- [ ] **Step 4: Mark §3.1, §3.3, §3.4 DONE**

Similar note at top of each, pointing to firmware Phase 1.D / 1.E commits.

- [ ] **Step 5: Add reference to firmware design spec**

Add a paragraph at top of `firmware-viewer-protocol.md` :
```markdown
> **Note** : ce protocole évolue en miroir du firmware. Pour le design
> centralisation Phase 1 (2026-05-17), voir
> [`../../../ILLPAD_V2/docs/superpowers/specs/2026-05-17-viewer-serial-centralization-design.md`](...).
```

- [ ] **Step 6: Commit in viewer worktree**

```bash
cd ../ILLPAD_V2-viewer
git add ILLPADViewer/docs/firmware-viewer-protocol.md
git commit -m "$(cat <<'EOF'
docs(protocol): Phase 1 firmware completion - new events + spec fixes DONE

Mise a jour de la spec parser viewer apres completion Phase 1 firmware :
- §1 catalogue etendu : [BOOT *], [GLOBALS], [SETTINGS], [FATAL],
  [CLOCK] BPM=, [SETUP] mode marker
- §3.1 [GLOBALS] - FIXED (firmware Phase 1.D)
- §3.2 sentinel collision - FIXED par 6b86fcb (firmware main)
- §3.3 [CLOCK] BPM change - FIXED (firmware Phase 1.E)
- §3.4 ?BOTH self-sufficient - FIXED (firmware Phase 1.D)

Cross-reference vers le design spec firmware
docs/superpowers/specs/2026-05-17-viewer-serial-centralization-design.md
EOF
)"
```

- [ ] **Step 7: Return to firmware worktree**

```bash
cd ../ILLPAD_V2
git status
```
Verify clean tree.

---

## Plan Self-Review

**Spec coverage check** : Compare Tasks 1-13 vs spec §16 inventory + §18
sub-phases :
- ✅ Pre-Phase 1.A viewer pre-coding → Task 1
- ✅ 1.A plomberie → Task 2
- ✅ 1.B boot tagging + cleanup → Task 3
- ✅ 1.C.1 [POT] → Task 4
- ✅ 1.C.2 [BANK]+[STATE] → Task 5
- ✅ 1.C.3 [ARP]+[GEN] → Task 6
- ✅ 1.C.4 [SCALE]+octave → Task 7
- ✅ 1.C.5 [CLOCK]+[MIDI] → Task 8
- ✅ 1.C.6 [PANIC] → Task 9
- ✅ 1.D [GLOBALS]+[SETTINGS]+sentinel → Task 10
- ✅ 1.E [CLOCK] BPM= → Task 11
- ✅ 1.F [FATAL] event → Task 3 Step 4 + Task 12 (HW validation)
- ✅ 1.G sync viewer doc → Task 13

**Estimation** : ~20-25h dev firmware. Plus ~6-8h viewer pre-coding (séparé,
plan dans `2026-05-17-viewer-juce-phase1-precode-plan.md`).

**Type consistency** : Vérifié — `viewer::emitPot(slot, target, valueStr, unit)`
signature consistante dans toutes les tasks. `viewer::Priority` enum HIGH/LOW
référencé uniquement en interne au module.

**Placeholder scan** : Tous les steps contiennent code complet ou commande
exacte. Aucun "TBD", "TODO", "implement later".

---

## Notes de migration cross-worktree

Le viewer-juce est pré-codé en avance selon le plan
[`2026-05-17-viewer-juce-phase1-precode-plan.md`](2026-05-17-viewer-juce-phase1-precode-plan.md).
Pendant l'exécution de ce plan firmware, aucun aller-retour avec le viewer
n'est requis sauf en cas de bug détecté en HW gate (debug bilatéral).

Workflow worktree :
```
~/Code/PROJECTS/ILLPAD_V2/             ← firmware (main branch) — ce plan
~/Code/PROJECTS/ILLPAD_V2-viewer/      ← viewer JUCE (viewer-juce branch) — pré-codage
```

Pour passer d'un worktree à l'autre :
```bash
cd ~/Code/PROJECTS/ILLPAD_V2          # firmware
cd ~/Code/PROJECTS/ILLPAD_V2-viewer   # viewer
```

Sync direction : firmware `main` → viewer `viewer-juce` via `git merge main`
(seulement pour les fichiers partagés `docs/superpowers/{specs,plans}`).
Aucune sync inverse.
