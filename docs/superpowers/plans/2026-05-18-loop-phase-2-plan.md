# LOOP Phase 2 Implementation Plan — LoopEngine + first audible MIDI

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** Livrer le 1er son MIDI LOOP audible HW. Une bank LOOP créée via Tool 5 (livré commit `e2857c5`) doit pouvoir enregistrer un motif percussif, le rejouer scalé au tempo live, supporter overdub, et respecter le quantize per-bank (Free/Beat/Bar). Aucune note bloquée. Engine fond + multi-bank prêts (1 bank LOOP en REC/OD au max, autres LOOP banks PLAYING/STOPPED en BG).

**Architecture :** Classe `LoopEngine` dans nouveau dossier `src/loop/` (parallèle à `src/arp/`). State machine 6 états (EMPTY / RECORDING / PLAYING / OVERDUBBING / STOPPED + transients WAITING_PLAY / WAITING_STOP — WAITING_LOAD déféré Phase 6). Buffer events µs-timestamped, playback BPM-scalé via `ClockManager::getSmoothedBPM()` et latch `_recordBpm`. Refcount noteOn/Off **dupliqué** depuis ArpEngine (Q2 §28 spec, structs `PendingEvent` + `_noteRefCount[128]` indépendants — pas de mutualisation). Intégration main loop via `processLoopMode()` symétrique à `processArpMode()`. NvsManager étendu pour loader `LoopPadStore` (control pads REC/PLAY/CLEAR + 16 slot pads) et `LoopPotStore` per-bank (effets — câblés Phase 5, ici juste loadés en mémoire). `BankSlot` étendu d'un `loopEngine*`. LED `renderBankLoop` complété pour consommer l'état engine + flags `consumeBarFlash` / `consumeWrapFlash` + déclencher `EVT_LOOP_REC/OVERDUB/CLEAR` + `EVT_WAITING`.

**Tech stack :** C++17, Arduino framework, PlatformIO, ESP32-S3-N8R16, FreeRTOS, `std::atomic` (inter-core), `micros()` timestamps, ClockManager 24 PPQN.

---

## Scope Phase 2 — inclus / exclus

| Inclus Phase 2 | Exclus (autre phase) |
|---|---|
| Classe `LoopEngine` (state machine, buffer events, refcount, scheduler interne) | Effets shuffle/chaos/velPattern (Phase 5) |
| `processLoopMode()` main wiring (REC/PLAY/CLEAR + musical pads → enregistrement/jeu) | `PotRouter` 3 contexts + Tool 7 LOOP page (Phase 4) |
| `BankSlot::loopEngine*` + assignment boot | Tool 3 b1 refactor (Phase 3) |
| `NvsManager` extension : load `LoopPadStore` (3 controls) + `LoopPotStore[8]` (defaults seulement, runtime apply Phase 5) | Tool 4 ext refus ControlPad/LOOP control (Phase 3) |
| Recording µs-timestamps + bar-snap (deadzone 25 %) + flush held pads à clôture | Slot Drive (LittleFS, save/load/delete — Phase 6) |
| Playback BPM-scaled + refcount + wrap/bar flash flags | WAITING_LOAD (slot load quantizé — Phase 6) |
| Overdub buffer (~128 events) + merge chronologique + Q5 STOPPED-loaded REC = PLAYING+OVERDUB | Effects per-bank pot routing (Phase 4) |
| WAITING_PLAY / WAITING_STOP transients (quantize Beat/Bar via ClockManager ticks) | LED tuning de `EVENT_RENDER_DEFAULT[EVT_LOOP_*]` (Phase 4) |
| `LedController::renderBankLoop` body complet (FG/BG, FLASH overlay bar/wrap, EVT triggers) | Tempo via LEFT+rear pot binding (déjà livré Phase 2 viewer, indépendant) |
| `EVT_LOOP_REC / OVERDUB / CLEAR` + `EVT_WAITING` emissions | CLEAR + slot delete combo (Phase 6) |
| `BankManager` LOOP double-tap → `loopEngine->tapPlayStop()` (remplace Phase 1 silent consume) | `LoopEngine::preBankSwitch()` (Phase 6 — slot load context) |
| `toggleAllArps` → `toggleAllArpsAndLoops` (loop-buffer-invariants §6) | LED preview de patterns LOOP-specific dans Tool 8 (Phase 4 polish) |
| `midiPanic()` extension pour flush `LoopEngine`s | — |
| Premier son MIDI LOOP audible HW (gate **G5**, milestone Phase 2) | — |

---

## Décisions actées avant code

### Q1 §7 loop-buffer-invariants — `tapPlayStop()` interaction avec auto-Play §13.2 ARPEG

**Décision : A — layer musical LOOP indépendant.**

Rationale : le dispatch dans `handlePadInput` (main.cpp:778) est strict par bank type :
- Bank LOOP → `processLoopMode()` exclusivement, `processArpMode()` jamais appelé.
- Bank ARPEG/ARPEG_GEN → `processArpMode()` exclusivement, REC/PLAY/CLEAR jouent comme pads musicaux normaux (pas filtrés en LOOP controls).
- Bank NORMAL → `processNormalMode()` exclusivement.

Conséquence : `LoopEngine::tapPlayStop()` n'a aucun chemin vers `ArpEngine::setCaptured()` ou inversement. Le concept "press musical en Stop = auto-Play" du dispatcher ARPEG (main.cpp:723-738) reste confiné à `processArpMode()`. Aucun risque de cross-talk. La filtration `isControlPad(i)` (Tool 4) et la filtration LOOP controls (REC/PLAY/CLEAR pads) se font **dans** `processLoopMode()`, pas via un dispatcher central.

**Q2-Q4 §7 loop-buffer-invariants** : différées Phase 6 (Slot Drive). Hors scope Phase 2.

### Checklist §8 pre-impl (loop-buffer-invariants)

- [x] **Aucun chemin n'appelle l'équivalent de `clearAllNotes()` sur le buffer hors des 6 actions §5.**
  Plan Phase 2 ne wipe le buffer qu'à `longPressClear()` (action §5.3) et à `mergeOverdub()` (action §5.1 réinterprétée comme "extension RECORDING"). Pas de wipe au bank switch, au release LEFT, au release de pad.
- [x] **Aucun "live remove" / "sweep" sur falling edge.**
  `processLoopMode()` falling edge sur pad musical : recording d'un noteOff event (si REC/OD), playback noteOff via refcount (si PLAYING). Pas de modification structurelle du buffer.
- [x] **`stop()` idempotent sur le buffer.**
  `tapPlayStop()` PLAYING→STOPPED : flush MIDI notes (refcount → 0), mais buffer events intact. `play()` STOPPED→PLAYING reset `_scaledElapsedUs = 0` + `_lastUpdateUs = nowUs` (intégration incrémentale, cf B1), ne touche pas le buffer.
- [x] **Interaction LEFT n'altère jamais le buffer LOOP.**
  `BankManager::update` LOOP double-tap : appelle `loopEngine->tapPlayStop()` (action de transport pur, pas de buffer touch). Pas de cleanup au LEFT release.
- [x] **Toggle global LEFT+hold_pad prévu pour inclure LOOP banks.**
  Task 32 — rename `toggleAllArps` → `toggleAllArpsAndLoops`.
- [x] **Invariants ARPEG §2 (1-5) restent préservés.**
  `processArpMode()` inchangé. Aucune modification de `ArpEngine` ni `ArpScheduler`.

### Décisions techniques Phase 2

| Élément | Valeur | Source |
|---|---|---|
| `MAX_LOOP_BANKS` | **4** (KeyboardData.h:659 bump 2→4 dans Task D1 ci-dessous) | spec §3 acté Phase 2 post-audit ; budget SRAM 4 × ~9.7 KB ≈ 38.8 KB (sur 320 KB total) |
| `MAX_LOOP_EVENTS` | 1024 (main buffer per bank, sorted live cf M2 ci-dessous) | spec §8 "buffer principal ~1024 events" |
| `MAX_LOOP_OVERDUB_EVENTS` | 128 (overdub buffer per bank, sorted live cf M2) | spec §8 "buffer d'overdub ~128 events" |
| `MAX_LOOP_PENDING_NOTEOFFS` | 64 (scheduled noteOff queue, gates ou notes off différés) | symétrique `MAX_PENDING_EVENTS = 64` ArpEngine |
| Timestamps recording | `micros()` (sub-ms precision) | spec §16 "timestamp en microsecondes" |
| BPM source playback scaling | `ClockManager::getSmoothedBPM()`, **intégration incrémentale** (cf B1 ci-dessous) | symétrique ArpScheduler.cpp:120 — formule par-tick `_scaledElapsedUs += delta × liveBpm / recordBpm` |
| BPM source recording timebase | latché `_recordBpm = getSmoothedBPM()` au 1er pad press en RECORDING | spec §16 "recordBpm latché" + invariant §23.5 immutable jusqu'au stopRecording |
| MIDI channel | `_channel = bankIndex` (0..7), set au boot via `setChannel(bank)` | Q2 acté — symétrique ArpEngine pattern, multi-bank natif |
| MIDI note par pad | `MIDI_BASE_NOTE + s_padOrder[padIndex]` (= 36 + padOrder) | spec §1 "convention GM à partir du kick C2" — applique padOrder, ignore scale (spec §24 non-goals "pas de scale") |
| Quantize values | 0=Free / 1=Beat / 2=Bar — discriminé via `BankTypeStore::quantize[bank]` + `_loadedBankType[bank] == BANK_LOOP` | validateBankTypeStore KeyboardData.h:725-726 (déjà actée Tool 5 refacto) |
| Beat boundary | 24 ticks (= 1/4 note à 24 PPQN) | spec §17 |
| Bar boundary | 96 ticks (= 4/4) | spec §17 |
| Bar-snap deadzone | 25 % du barDuration courant | spec §7 "Threshold 25 %" |
| Min/max loop length | 1 bar / 64 bars | spec §7 |
| LED FG/BG intensity LOOP | `_fgIntensity` (unified v9, LedController:920) × `_bgFactor` en BG | spec §21 + LedSettingsStore v9 |
| Velocity LOOP recording | `slot.baseVelocity` **strict, sans variation** (capture + live monitor) | Q8 acté — variation appliquée uniquement au playback (spec §10 "randomisation à la lecture") |
| Velocity LOOP playback | `applyVelocityVariation(event.velocity)` au moment du fire dans `update()` | Q8 acté — 1 seule randomisation par playback |
| Pads musicaux sur bank LOOP | **Émettent MIDI live dans tous les états** (EMPTY/STOPPED/PLAYING/REC/OD/WAITING) | Q1 acté — spec §18 "percussion fixe (LOOP, offset C2)" |
| `mergeOverdub` strategy | **Live-sort** dans `capturePadEvent` (insertion triée O(n) par event), `mergeOverdub` = merge O(n+m) de 2 arrays pré-triés | Q4 acté — élimine 5-25 ms freeze potentiel Core 1 |
| Dev seed pads REC/PLAY/CLEAR | **Conditionnel** : pads 32/33/34 seedés au boot UNIQUEMENT si NVS LoopPadStore vide ET aucune entry ControlPadStore sur 32/33/34 | Q5 acté — zéro collision silencieuse avec Tool 4 |
| Boot warning > MAX_LOOP_BANKS | `Serial.printf("[WARN] Bank N BANK_LOOP exceeds MAX_LOOP_BANKS=4, runtime disabled")` | Q6 acté — diag user via serial/viewer, pas de LED pollution |
| `_clearFired` semantics | Set `true` à la fire de `longPressClear`, reset à `false` au release CLEAR ou nouveau press | Fix audit B2 — prévient wipe répété tant que CLEAR tenu |

### Hors scope explicite (signalement)

- **PendingEvent NON factorisé** avec ArpEngine (Q2 §28). LoopEngine définit son propre `PendingEvent` (struct identique en pratique mais classe distincte), son propre `MAX_LOOP_PENDING_NOTEOFFS`, ses propres `scheduleEvent / refCountNoteOn / refCountNoteOff / processEvents / flushPendingNoteOffs`. Pas de header partagé.
- **Pas de scheduler centralisé style ArpScheduler** : LoopEngine est µs-driven (pas tick-driven), `update()` est appelé directement depuis main loop pour chaque bank LOOP (boucle `for (i: 0..NUM_BANKS) if (s_banks[i].loopEngine) s_banks[i].loopEngine->update()`).
- **Pas de scale propagation sur bank LOOP** : ScaleManager early-return livré Phase 1 (commit `2624b12`), inchangé.
- **`reloadPerBankParams`** (main.cpp:797) reste no-op pour LOOP — PotRouter LOOP context vient Phase 4. Vérifié : la fonction skip via `if (isArpType(newSlot.type) && newSlot.arpEngine)` (L803), pas de crash sur LOOP bank.

### Post-audit additions (B1-M9, m1-m11) — invariants supplémentaires consacrés

Ces décisions actées post-audit doivent être respectées partout dans le plan :

- **B1 — Intégration incrémentale BPM** : la position de playback est calculée par delta cumulatif (`_scaledElapsedUs += delta × liveBpm / recordBpm` chaque tick), pas par re-interprétation de `realElapsed × liveBpm / recordBpm` (formulation buggy car réinterprète tout l'historique au nouveau ratio). Rebase explicite `_scaledElapsedUs = 0; _lastUpdateUs = nowUs;` à chaque wrap.
- **B2 — `_clearFired` armé** : `longPressClear()` set `_clearFired = true;` en fin pour bloquer re-fires tant que CLEAR pad reste tenu.
- **B3 — `startPlayback(transport, nowUs)`** : signature étendue avec `uint32_t nowUs` capturé en début d'`update()`. Évite l'underflow uint32 quand `commitWaitingAction` re-appelle `micros()` après le `nowUs` initial.
- **M1 — Pas de dead-code dans state machine** : `tapRec` case `WAITING_STOP` aplati, pas de `_state = PLAYING` écrasé immédiatement.
- **M2 — Live-sort** : `capturePadEvent` insère chaque event à sa position triée O(n) shift dans `_events[]` (RECORDING) ou `_overdubEvents[]` (OVERDUBBING). Le buffer est toujours sorted. `stopRecording` n'a plus besoin de sort à la fin. `mergeOverdub` devient O(n+m) merge de 2 arrays pré-triés.
- **M3 — Velocity strict à capture** : `processLoopMode` passe `slot.baseVelocity` brut (pas de variation) à `capturePadEvent`. Variation appliquée uniquement au playback dans `update()` (1 randomisation).
- **M4 — Overdub atomique** : `mergeOverdub` pré-check `if (_eventCount + _overdubCount > MAX_LOOP_EVENTS) abandon atomique silent`. Jamais de drop partiel qui casse les paires noteOn/Off.
- **M6 — Validation timestamp** : `stopRecording` après rescale, assertion `forall i : _events[i].timestampUs < _loopDurationUs` + clamp `_loopDurationUs - 1` si dépassement détecté.
- **M7 — Dev seed conditionnel** : helper `NvsManager::applyDevSeedLoopPadsIfSafe()` seed pads 32/33/34 uniquement si NVS LoopPadStore vide ET aucun ControlPad assigné sur {32, 33, 34}.
- **M8 — Live drumming spec §18** : `capturePadEvent` émet MIDI noteOn/Off via `refCountNoteOn/Off` dans **TOUS** les états (EMPTY/STOPPED/PLAYING/RECORDING/OVERDUBBING/WAITING_*). Le buffer write est conditionnel (REC/OD seulement), le MIDI émission est inconditionnel.
- **M9 — Bank switch guard 2 paths** : Task 35 patche le pending-timeout AND le LEFT-release fast-forward (2 snippets explicites).
- **m1 — LoopEvent 8 B** : `static_assert(sizeof(LoopEvent) == 8, "LoopEvent must be 8 B");` au-dessous de la struct dans LoopEngine.h.
- **m2 — Boot warning > cap** : main.cpp post-assignment scanne et `Serial.printf [WARN]` pour chaque bank LOOP sans engine.
- **m5 — Task 35 placement** : intégrée dans Phase 2.J avant Self-Review, pas après.
- **m9 — `viewer::emitLoopBufferFull(bankIdx)`** : nouveau callsite ViewerSerial pour telemetry buffer-full (équivalent `emitArpQueueFull`).

---

## File Structure

| Statut | Fichier | Responsabilité Phase 2 |
|---|---|---|
| **CREATE** | [`src/loop/LoopEngine.h`](../../../src/loop/LoopEngine.h) | Class declaration, enum `LoopState`, struct `LoopEvent`, struct `LoopPendingNoteOff`, API publique |
| **CREATE** | [`src/loop/LoopEngine.cpp`](../../../src/loop/LoopEngine.cpp) | Implémentation state machine + recording + playback + overdub + refcount + flushes |
| **MODIFY** | [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h) | `BankSlot` : + `LoopEngine* loopEngine` field (ligne ~372) ; forward declaration class `LoopEngine` (ligne ~366) |
| **MODIFY** | [`src/managers/NvsManager.h`](../../../src/managers/NvsManager.h) | + accessors `getLoadedLoopPadStore()` / `getLoadedLoopPotParams(uint8_t bank)` + members `_loadedLoopPad` (LoopPadStore) + `_loadedLoopPot[NUM_BANKS]` (LoopPotStore) |
| **MODIFY** | [`src/managers/NvsManager.cpp`](../../../src/managers/NvsManager.cpp) | Constructor defaults LoopPad/Pot + `loadAll()` étendue (charge LoopPadStore single + LoopPotStore per-bank via boucle `loop_0..loop_7`) |
| **MODIFY** | [`src/main.cpp`](../../../src/main.cpp) | + `s_loopEngines[MAX_LOOP_BANKS]` static (ligne après s_arpEngines, ~L72) + `static void processLoopMode(...)` (à insérer ~L752 entre processArpMode et handleLeftReleaseCleanup) + boot wiring (assignment LoopEngine to LOOP banks, apply LoopPad pads, init channel) (ligne après ARPEG boot block ~L562) + `handlePadInput` switch case BANK_LOOP wiring (L787) + `midiPanic()` flush extension (L150) + `toggleAllArps` → `toggleAllArpsAndLoops` + LOOP banks (L894) + loop() body call to update each LoopEngine |
| **MODIFY** | [`src/managers/BankManager.cpp`](../../../src/managers/BankManager.cpp) | Remplacer le silent-consume LOOP double-tap (L106-110) par appel `loopEngine->tapPlayStop()` (avec keys pointer = nullptr si BG, comme ARPEG L89) |
| **MODIFY** | [`src/core/LedController.h`](../../../src/core/LedController.h) | + déclaration interne d'helper pour render LOOP states (privé) — pas d'API publique nouvelle |
| **MODIFY** | [`src/core/LedController.cpp`](../../../src/core/LedController.cpp) | `renderBankLoop()` (L508) body complet : SOLID FG/BG (état EMPTY/STOPPED), FLASH bar/wrap consumes via `loopEngine->consumeBarFlash() / consumeWrapFlash()`, état machine drives FG color overlay (REC/OD/PLAYING) |

**Note** : pas de modification de `src/arp/ArpEngine.{h,cpp}` ni `src/arp/ArpScheduler.{cpp,h}` — DO NOT MODIFY étendu aux pièces ARPEG calibrées (Q2 §28).

---

## Build / HW workflow

Chaque task suit les 5 gates projet (cf `~/.claude/CLAUDE.md` embedded section) :

1. **Code** — Read fichier cible intégral avant édition. Edit (jamais Write si fichier existe).
2. **Build** — `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`. Exit 0, 0 nouveau warning. Bloque tant que rouge.
3. **Auto-review** — `grep -n` des symboles modifiés. Vérifier couverture call-sites.
4. **HW gate** — regroupé en fin de sous-phase (G1..G9 ci-dessous). Compile passé ≠ ça marche, HW seul juge.
5. **Commit gate** — proposer fichiers + message HEREDOC, attendre OK. Commit groupé en fin de sous-phase.

**Recap table multi-axes** (à maintenir par l'implémenteur au fil de l'exécution) :

| Phase | Tasks | Compile | HW gate | Commit |
|---|---|---|---|---|
| 2.A LoopEngine class skeleton | 1-4 | — | — | — |
| 2.B BankSlot + boot wiring (D1 bump MAX_LOOP_BANKS=4 + m2 warning) | 5-8 | — | G1 (boot OK avec LOOP bank Tool 5) | — |
| 2.C NvsManager LoopPad/Pot load (M7 dev seed conditionnel + m8 hoist) | 9-12 | — | G2 (LoopPad/Pot loaded reflect setup state) | — |
| 2.D processLoopMode + REC/PLAY/CLEAR (M3 velocity strict + M8 live drumming) | 13-16 | — | G3 (state transitions via serial trace) | — |
| 2.E Recording + bar-snap (Task 16.5 viewer m9 + Task 17 M2 live-sort + Task 18 M5 M6 + m10 trace) | 16.5-19 | — | G4 (record motif, eventCount > 0) | — |
| 2.F Playback **(premier son MIDI LOOP)** (B1 incrémental + B3 nowUs) | 20-23 | — | **G5 ★ audible son MIDI loop** | — |
| 2.G Overdub + Q5 STOPPED-loaded REC (M2 merge O(n+m) + M4 atomic) | 24-25 | — | G6 (overdub merge audible) | — |
| 2.H Quantize WAITING_PLAY/STOP (B3 nowUs commitWaitingAction) | 26-27 | — | G7 (Bar/Beat-aligned start/stop) | — |
| 2.I LedController renderBankLoop body (m4 consume one-shot) | 28-30 | — | G8 (visual feedback per LED spec §17) | — |
| 2.J BankManager double-tap + toggleAllArpsAndLoops + midiPanic + M9 bank switch guard | 31-35 | — | G9 (multi-bank toggle + panic + guard) | — |
| Doc-sync (m11) | 36 | — | — | — |

**Règle absolue** : HW gate AVANT commit gate, jamais après. Un commit non-testé HW grave un état potentiellement bugué qu'il faudra reverter.

---

# Phase 2.A — LoopEngine class skeleton

Objectif : produire un fichier `.h` + `.cpp` qui compile, n'est pas encore instancié dans main, expose l'API publique complète sans implémenter encore recording/playback. Bases pour Phase 2.B-2.J.

### Task 1 — Create `LoopEngine.h` (full API declaration)

**Files:**
- Create: `src/loop/LoopEngine.h`

- [ ] **Step 1: Verify directory doesn't exist yet**

```bash
ls /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/ 2>&1
```
Expected: `No such file or directory` (parent `src/` exists).

- [ ] **Step 2: Create directory**

```bash
mkdir -p /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop
```

- [ ] **Step 3: Write `src/loop/LoopEngine.h`**

```cpp
#ifndef LOOP_ENGINE_H
#define LOOP_ENGINE_H

#include <stdint.h>
#include "../core/KeyboardData.h"

class MidiTransport;
class ClockManager;

// =================================================================
// LoopState — full state machine (spec §3 + §17)
// =================================================================
// Phase 2 implements: EMPTY/RECORDING/PLAYING/OVERDUBBING/STOPPED
//                     + transients WAITING_PLAY/WAITING_STOP
// Phase 6 adds:       WAITING_LOAD (slot load with quantize)
enum class LoopState : uint8_t {
  EMPTY        = 0,
  RECORDING    = 1,
  PLAYING      = 2,
  OVERDUBBING  = 3,
  STOPPED      = 4,
  WAITING_PLAY = 5,
  WAITING_STOP = 6,
};

// =================================================================
// LoopQuantize — values stored in BankTypeStore::quantize[bank]
//                when type == BANK_LOOP (validator KeyboardData.h:725-726)
// =================================================================
enum LoopQuantize : uint8_t {
  LOOP_QUANT_FREE = 0,   // No quantize, immediate action
  LOOP_QUANT_BEAT = 1,   // Next beat boundary (24 ticks)
  LOOP_QUANT_BAR  = 2,   // Next bar boundary (96 ticks) — default
};

// =================================================================
// Buffer caps (spec §8, §25 + symetric ArpEngine MAX_PENDING_EVENTS = 64)
// =================================================================
static const uint16_t MAX_LOOP_EVENTS              = 1024;  // main buffer per bank
static const uint8_t  MAX_LOOP_OVERDUB_EVENTS      = 128;   // overdub temp buffer
static const uint8_t  MAX_LOOP_PENDING_NOTEOFFS    = 64;    // scheduled noteOff queue (gates)

// =================================================================
// Tick boundaries (24 PPQN)
// =================================================================
static const uint32_t TICKS_PER_BEAT = 24;
static const uint32_t TICKS_PER_BAR  = 96;

// =================================================================
// LoopEvent — single captured pad action (noteOn or noteOff)
// =================================================================
// Stored in _events[] (main buffer) and _overdubEvents[] (overdub temp).
// Layout naturel ARM 32-bit : 8 B (uint32 timestampUs + 4× uint8). Live-sorted
// dans capturePadEvent (M2 décision Q4) — pas de compaction nécessaire.
// `velocity` strict baseVelocity au capture (M3 décision Q8), randomisation
// appliquée seulement au playback dans update().
struct LoopEvent {
  uint32_t timestampUs;  // µs offset from loop position 0 (event timeline within loop)
  uint8_t  padIndex;     // 0..NUM_KEYS-1 (raw key index, padOrder applied at play time)
  uint8_t  midiNote;     // pre-resolved MIDI note (MIDI_BASE_NOTE + padOrder[padIndex])
  uint8_t  velocity;     // 0 = noteOff event, 1..127 = noteOn event (baseVelocity strict)
  bool     active;       // false = slot vacant (live-sort sentinel, end-of-buffer marker)
};
static_assert(sizeof(LoopEvent) == 8, "LoopEvent must be exactly 8 B (ARM natural alignment)");

// =================================================================
// LoopPendingNoteOff — scheduled future noteOff (gate length or stuck-note flush)
// =================================================================
// Symmetric to ArpEngine::PendingEvent. Used by refCountNoteOn to schedule
// safety noteOff at end of loop, and by overdub clear-on-tap-REC.
struct LoopPendingNoteOff {
  uint32_t fireTimeUs;   // micros() absolute timestamp
  uint8_t  note;
  bool     active;
};

// =================================================================
// LoopEngine — one loop instance per BANK_LOOP slot (max MAX_LOOP_BANKS)
// =================================================================
class LoopEngine {
public:
  LoopEngine();

  // --- Configuration (set at boot from BankManager / NvsManager) ---
  void setChannel(uint8_t ch);
  void setQuantize(LoopQuantize q);              // from BankTypeStore::quantize[bank]
  void setPadOrder(const uint8_t* padOrder);     // for MIDI note resolution
  void setClockManager(ClockManager* clock);     // for getSmoothedBPM + getCurrentTick
  void setControlPads(uint8_t recPad, uint8_t playStopPad, uint8_t clearPad);
  void setBaseVelocity(uint8_t vel);
  void setVelocityVariation(uint8_t pct);
  void setClearLoopTimerMs(uint16_t ms);         // from SettingsStore::clearLoopTimerMs (Tool 6)

  // --- Transport actions (called by processLoopMode) ---
  // Tap REC : state machine per spec §7 §8 + Q5 §28
  //   EMPTY    → RECORDING (arms capture, _recordStartUs set on first pad press)
  //   RECORDING→ stopRecording() (bar-snap close, → PLAYING)
  //   PLAYING  → OVERDUBBING (start overdub buffer)
  //   STOPPED  → PLAYING + OVERDUBBING simultaneously (Q5 — reprise + arm)
  //   OVERDUB  → mergeOverdub() (commit overdub into main buffer)
  void tapRec(MidiTransport& transport);

  // Tap PLAY/STOP : state machine per spec §9 §17
  //   EMPTY      → no-op
  //   STOPPED    → quantized PLAY (or immediate if FREE) → PLAYING / WAITING_PLAY
  //   PLAYING    → quantized STOP (or immediate if FREE) → STOPPED / WAITING_STOP
  //   OVERDUBBING→ abandon overdub, stay PLAYING (spec §8)
  //   WAITING_*  → cancel waiting (per §17 concurrent gestures table)
  // currentKeys = keyIsPressed (FG context) or nullptr (BG context, multi-bank).
  // Param actuellement non utilisé (réservé pour usages futurs ; ne pas dépendre du contenu).
  void tapPlayStop(MidiTransport& transport, const uint8_t* currentKeys = nullptr);

  // Long-press CLEAR : state machine per spec §9
  //   Any except RECORDING/OVERDUBBING with held-for-clearLoopTimerMs → EMPTY (wipe buffer)
  // Called by processLoopMode tracker (rising edge starts timer, sustained press fires this).
  void longPressClear(MidiTransport& transport);

  // --- Recording (called by processLoopMode on musical pad edges) ---
  // Capture a noteOn (rising edge) or noteOff (falling edge) into main buffer
  // (RECORDING) or overdub buffer (OVERDUBBING). No-op if state != REC/OD.
  // velocity = 0 for noteOff capture, > 0 for noteOn capture.
  void capturePadEvent(uint8_t padIndex, uint8_t velocity, MidiTransport& transport);

  // --- Update (called from main loop EVERY iteration, µs-driven) ---
  // Walks main buffer firing events whose scaled timestamp <= current loop position.
  // Handles wrap (consumeWrapFlash) and bar crossing (consumeBarFlash).
  // Resolves WAITING_PLAY/WAITING_STOP boundaries via ClockManager ticks.
  // Drains pending noteOff queue (gates + stuck-note safety).
  void update(MidiTransport& transport);

  // --- Emergency flush ---
  // Cancel all pending events + sweep refcount → noteOff + state → STOPPED.
  // Called by midiPanic() and by bank switch on REC/OD lock (spec §23.2).
  void flushPendingNoteOffs(MidiTransport& transport);

  // --- Background transition hook (Audit-fix B-N1 / R-N1) ---
  // Appelé depuis main.cpp quand cette bank LOOP passe de FG à BG (bank switch out).
  // 1. Pour chaque pad encore physiquement enfoncé (_padHeldLive[pad] == true) :
  //    refCountNoteOff + reset flag. Évite stuck note au DAW (invariant §23.1).
  // 2. Reset CLEAR press tracker (_clearPressStartMs = 0, _clearFired = false)
  //    pour éviter wipe instantané au retour FG si CLEAR encore tenu (spec §9).
  // NE CHANGE PAS `_state` — la bank LOOP en BG doit pouvoir continuer son playback.
  void onBackgroundTransition(MidiTransport& transport);

  // --- Queries ---
  LoopState getState() const          { return _state; }
  bool      isPlaying() const         { return _state == LoopState::PLAYING || _state == LoopState::OVERDUBBING; }
  bool      isRecording() const       { return _state == LoopState::RECORDING; }
  bool      isOverdubbing() const     { return _state == LoopState::OVERDUBBING; }
  bool      isWaiting() const         { return _state == LoopState::WAITING_PLAY || _state == LoopState::WAITING_STOP; }
  bool      isLocked() const          { return _state == LoopState::RECORDING || _state == LoopState::OVERDUBBING; }
  bool      hasContent() const        { return _eventCount > 0; }
  uint16_t  getEventCount() const     { return _eventCount; }
  uint32_t  getLoopDurationUs() const { return _loopDurationUs; }
  uint16_t  getRecordBpm() const      { return _recordBpm; }

  // --- LED flash hooks (consumed once by LedController::renderBankLoop) ---
  // consumeBarFlash : set true by update() on bar crossing within loop (every 96 ticks scaled)
  // consumeWrapFlash : set true by update() on loop wrap (position → 0)
  bool consumeBarFlash();
  bool consumeWrapFlash();

  // --- Control pad lookup (used by processLoopMode) ---
  bool isLoopControlPad(uint8_t padIndex) const;
  uint8_t getRecPad() const       { return _recPad; }
  uint8_t getPlayStopPad() const  { return _playStopPad; }
  uint8_t getClearPad() const     { return _clearPad; }

  // --- CLEAR press tracking (called by processLoopMode on CLEAR pad edges) ---
  void notifyClearPressStart(uint32_t nowMs);   // rising edge
  void notifyClearPressEnd();                   // falling edge (cancels timer)
  bool isClearHoldFired(uint32_t nowMs) const;  // true once threshold passed

private:
  // --- Configuration ---
  uint8_t          _channel;
  LoopQuantize     _quantize;
  const uint8_t*   _padOrder;
  ClockManager*    _clock;
  uint8_t          _recPad;       // 0xFF = unassigned (LoopPadStore)
  uint8_t          _playStopPad;  // 0xFF = unassigned
  uint8_t          _clearPad;     // 0xFF = unassigned
  uint8_t          _baseVelocity;
  uint8_t          _velocityVariation;
  uint16_t         _clearLoopTimerMs;  // from SettingsStore (default 500, range 200-1500)

  // --- State machine ---
  LoopState        _state;

  // --- Recording timebase ---
  uint32_t         _recordStartUs;     // micros() at first pad press in RECORDING (offset 0)
  uint32_t         _recordEndUs;       // micros() at REC tap (un-snapped end)
  uint16_t         _recordBpm;         // latched at first pad press (invariant §23.5)
  bool             _recordFirstPressDone;  // false until first capturePadEvent in RECORDING

  // --- Loop structure (set at stopRecording) ---
  uint32_t         _loopDurationUs;    // bar-snapped duration (after deadzone snap + rescale)
  uint16_t         _loopBars;          // 1..64 (post snap)

  // --- Main event buffer (committed loop content) ---
  LoopEvent        _events[MAX_LOOP_EVENTS];
  uint16_t         _eventCount;        // number of active events (compacted, contiguous 0.._eventCount-1)

  // --- Overdub temp buffer (committed on tapRec during OVERDUBBING) ---
  LoopEvent        _overdubEvents[MAX_LOOP_OVERDUB_EVENTS];
  uint8_t          _overdubCount;

  // --- Playback timeline (intégration incrémentale BPM, M-fix B1) ---
  // _playStartUs : conservé pour compat / debug (timestamp d'entrée en PLAYING), pas utilisé pour position.
  // _scaledElapsedUs : position cumulative en µs scalée par BPM ratio. Pas modulée.
  //   À chaque update() : delta = nowUs - _lastUpdateUs ; _scaledElapsedUs += delta × liveBpm / recordBpm.
  //   Wrap détecté quand _scaledElapsedUs >= _loopDurationUs → soustraire loopDuration + _wrapFlash + reset playNextEventIdx.
  // _playPositionUs : projection actuelle dans le loop (0.._loopDurationUs-1), dérivée de _scaledElapsedUs.
  uint32_t         _playStartUs;       // debug / timestamp PLAYING entry
  uint64_t         _scaledElapsedUs;   // cumulative scaled µs since PLAYING entry (uint64 anti-overflow long sessions)
  uint32_t         _lastUpdateUs;      // micros() at previous update() call, for delta computation
  uint32_t         _playPositionUs;    // current scaled position within loop (0.._loopDurationUs)
  uint16_t         _playNextEventIdx;  // next event to fire (sorted by timestampUs — live-sort invariant)
  uint16_t         _lastBarIndex;      // for bar crossing detection (which bar of loop we last reported)

  // --- Pending noteOff queue (gate length + stuck-note safety) ---
  LoopPendingNoteOff _pendingNoteOffs[MAX_LOOP_PENDING_NOTEOFFS];

  // --- Refcount per MIDI note (symmetric ArpEngine pattern Q2 §28) ---
  uint8_t          _noteRefCount[128];

  // --- LED flash flags ---
  bool             _barFlash;
  bool             _wrapFlash;

  // --- Quantize transient ---
  uint32_t         _waitingTargetTick;   // ClockManager tick at which WAITING_PLAY/STOP commits

  // --- CLEAR press tracker (set by notifyClearPressStart) ---
  uint32_t         _clearPressStartMs;   // 0 = not pressing
  bool             _clearFired;          // true once threshold fired (avoid re-fire)

  // --- Per-pad live-press tracker (Audit-fix B-N1 / B-N2 / R-N1) ---
  // Set true à chaque live monitor noteOn (peu importe le state : EMPTY / STOPPED /
  // PLAYING / RECORDING / OVERDUBBING / WAITING_*). Reset au falling edge.
  // Trois consumers :
  //   - stopRecording → flushHeldPadsAsNoteOffs(snappedDurUs) : inject noteOff fin de loop.
  //   - mergeOverdub → inject noteOff dans _events à _playPositionUs (B-N2 fix).
  //   - onBackgroundTransition → refCountNoteOff direct + reset flag (B-N1 fix).
  // Remplace l'ancien _padHeldLive[] qui ne couvrait que RECORDING (insuffisant).
  bool             _padHeldLive[NUM_KEYS];

  // --- Helpers ---
  // Recording
  void startRecording(MidiTransport& transport);
  void stopRecording(MidiTransport& transport);  // bar-snap + rescale + → PLAYING
  void flushHeldPadsAsNoteOffs(uint32_t timestampUs);
  // Live-sort insertion (M2 décision Q4) — buffer maintenu trié à l'insertion.
  // Retourne true si inséré, false si buffer plein (drop silent per spec §8).
  bool insertEventSorted(LoopEvent* buffer, uint16_t& count, uint16_t cap,
                          uint32_t timestampUs, uint8_t padIndex, uint8_t midiNote, uint8_t velocity);

  // Playback / scheduler (B1 intégration incrémentale)
  // startPlayback(transport, nowUs) : signature étendue avec nowUs capturé en début d'update
  // pour éviter underflow uint32 sur enchaînement commitWaitingAction → startPlayback (B3 fix).
  // computeLoopPositionUs SUPPRIMÉ : position maintenue par accumulation incrementale dans update() (B1).
  void startPlayback(MidiTransport& transport, uint32_t nowUs);
  void stopPlayback(MidiTransport& transport, bool flushNotes);
  bool scheduleNoteOff(uint32_t fireTimeUs, uint8_t note);
  void drainPendingNoteOffs(MidiTransport& transport, uint32_t nowUs);
  void refCountNoteOn(MidiTransport& transport, uint8_t note, uint8_t velocity);
  void refCountNoteOff(MidiTransport& transport, uint8_t note);

  // Overdub (M2 décision Q4 : merge O(n+m) de 2 arrays pré-triés ; M4 fix : pré-check capacité)
  // Retourne true si merge OK, false si capacité dépassée → abandon atomique silent (spec §8).
  bool mergeOverdub();   // M2: O(n+m) merge ; M4: pre-check + atomic abandon
  void abandonOverdub(); // wipe overdub buffer, state stays PLAYING (spec §8)

  // Quantize boundary detection
  uint32_t computeNextBoundaryTick(LoopQuantize q) const;  // ClockManager tick of next boundary
  // commitWaitingAction(transport, nowUs) : nowUs propagé pour B3 fix (passé à startPlayback).
  void commitWaitingAction(MidiTransport& transport, uint32_t nowUs);

  // MIDI note resolution
  uint8_t resolvePadToMidiNote(uint8_t padIndex) const;

  // Velocity randomization (symmetric processNormalMode main.cpp:676-680).
  // Appliquée uniquement au playback dans update() (M3 décision Q8 : pas au capture).
  uint8_t applyVelocityVariation(uint8_t baseVel) const;
};

#endif // LOOP_ENGINE_H
```

- [ ] **Step 4: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: build error "LoopEngine.cpp not found" or "no source files in src/loop/" — Step 4 verifies header alone doesn't break unused includes (it isn't included yet anywhere).

Actually expected: build **passes** (file not included anywhere, ignored by linker). If unexpected error appears (e.g. ESP32 build picks up stray .h orphans), proceed to Task 2 immediately to silence.

### Task 2 — Implement `LoopEngine.cpp` constructor + state queries + setters

**Files:**
- Create: `src/loop/LoopEngine.cpp`

- [ ] **Step 1: Read `src/arp/ArpEngine.cpp` lines 23-58 (constructor template)**

Reference for constructor pattern (init queue, refcount, ranges).

- [ ] **Step 2: Write `src/loop/LoopEngine.cpp` constructor + simple setters**

```cpp
#include "LoopEngine.h"
#include "../core/MidiTransport.h"
#include "../midi/ClockManager.h"
#include "../core/HardwareConfig.h"
#include "../viewer/ViewerSerial.h"   // viewer::emitLoopBufferFull (m9 audit)
#include <Arduino.h>
#include <string.h>

// =================================================================
// Constructor
// =================================================================
LoopEngine::LoopEngine()
  : _channel(0)
  , _quantize(LOOP_QUANT_BAR)
  , _padOrder(nullptr)
  , _clock(nullptr)
  , _recPad(0xFF)
  , _playStopPad(0xFF)
  , _clearPad(0xFF)
  , _baseVelocity(DEFAULT_BASE_VELOCITY)
  , _velocityVariation(DEFAULT_VELOCITY_VARIATION)
  , _clearLoopTimerMs(500)
  , _state(LoopState::EMPTY)
  , _recordStartUs(0)
  , _recordEndUs(0)
  , _recordBpm(120)
  , _recordFirstPressDone(false)
  , _loopDurationUs(0)
  , _loopBars(0)
  , _eventCount(0)
  , _overdubCount(0)
  , _playStartUs(0)
  , _scaledElapsedUs(0)        // B1: cumulative scaled position (uint64)
  , _lastUpdateUs(0)            // B1: previous update() tick timestamp
  , _playPositionUs(0)
  , _playNextEventIdx(0)
  , _lastBarIndex(0)
  , _barFlash(false)
  , _wrapFlash(false)
  , _waitingTargetTick(0)
  , _clearPressStartMs(0)
  , _clearFired(false)
{
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) {
    _events[i].active = false;
  }
  for (uint8_t i = 0; i < MAX_LOOP_OVERDUB_EVENTS; i++) {
    _overdubEvents[i].active = false;
  }
  for (uint8_t i = 0; i < MAX_LOOP_PENDING_NOTEOFFS; i++) {
    _pendingNoteOffs[i].active = false;
  }
  memset(_noteRefCount, 0, sizeof(_noteRefCount));
  memset(_padHeldLive, 0, sizeof(_padHeldLive));
}

// =================================================================
// Configuration setters
// =================================================================
void LoopEngine::setChannel(uint8_t ch)              { _channel = ch; }
void LoopEngine::setQuantize(LoopQuantize q)         { _quantize = (q <= LOOP_QUANT_BAR) ? q : LOOP_QUANT_BAR; }
void LoopEngine::setPadOrder(const uint8_t* padOrder){ _padOrder = padOrder; }
void LoopEngine::setClockManager(ClockManager* clock){ _clock = clock; }
void LoopEngine::setBaseVelocity(uint8_t vel)        { _baseVelocity = vel; }
void LoopEngine::setVelocityVariation(uint8_t pct)   { _velocityVariation = pct; }
void LoopEngine::setClearLoopTimerMs(uint16_t ms)    { _clearLoopTimerMs = ms; }

void LoopEngine::setControlPads(uint8_t recPad, uint8_t playStopPad, uint8_t clearPad) {
  _recPad      = recPad;
  _playStopPad = playStopPad;
  _clearPad    = clearPad;
}

bool LoopEngine::isLoopControlPad(uint8_t padIndex) const {
  return padIndex == _recPad
      || padIndex == _playStopPad
      || padIndex == _clearPad;
}

// =================================================================
// LED flash hooks
// =================================================================
bool LoopEngine::consumeBarFlash() {
  bool v = _barFlash;
  _barFlash = false;
  return v;
}

bool LoopEngine::consumeWrapFlash() {
  bool v = _wrapFlash;
  _wrapFlash = false;
  return v;
}

// =================================================================
// CLEAR press tracking
// =================================================================
void LoopEngine::notifyClearPressStart(uint32_t nowMs) {
  if (_clearPressStartMs == 0) {
    _clearPressStartMs = nowMs;
    _clearFired = false;
  }
}

void LoopEngine::notifyClearPressEnd() {
  _clearPressStartMs = 0;
  _clearFired = false;
}

bool LoopEngine::isClearHoldFired(uint32_t nowMs) const {
  if (_clearPressStartMs == 0 || _clearFired) return false;
  return (nowMs - _clearPressStartMs) >= _clearLoopTimerMs;
}

// --- Stubs (real impl Tasks 3-46) ---
void LoopEngine::tapRec(MidiTransport&) {}
void LoopEngine::tapPlayStop(MidiTransport&, const uint8_t*) {}
void LoopEngine::longPressClear(MidiTransport&) {}
void LoopEngine::capturePadEvent(uint8_t, uint8_t, MidiTransport&) {}
void LoopEngine::update(MidiTransport&) {}
void LoopEngine::flushPendingNoteOffs(MidiTransport&) {}
uint8_t LoopEngine::resolvePadToMidiNote(uint8_t padIndex) const { return MIDI_BASE_NOTE + (_padOrder ? _padOrder[padIndex] : padIndex); }
uint8_t LoopEngine::applyVelocityVariation(uint8_t v) const { return v; }
bool LoopEngine::insertEventSorted(LoopEvent*, uint16_t&, uint16_t, uint32_t, uint8_t, uint8_t, uint8_t) { return false; }
void LoopEngine::startRecording(MidiTransport&) {}
void LoopEngine::stopRecording(MidiTransport&) {}
void LoopEngine::flushHeldPadsAsNoteOffs(uint32_t) {}
void LoopEngine::startPlayback(MidiTransport&, uint32_t) {}   // B3 : signature étendue avec nowUs
void LoopEngine::stopPlayback(MidiTransport&, bool) {}
// computeLoopPositionUs stub supprimé (B1 fix : méthode supprimée du plan)
bool LoopEngine::scheduleNoteOff(uint32_t, uint8_t) { return false; }
void LoopEngine::drainPendingNoteOffs(MidiTransport&, uint32_t) {}
void LoopEngine::refCountNoteOn(MidiTransport&, uint8_t, uint8_t) {}
void LoopEngine::refCountNoteOff(MidiTransport&, uint8_t) {}
bool LoopEngine::mergeOverdub() { return true; }
void LoopEngine::abandonOverdub() {}
uint32_t LoopEngine::computeNextBoundaryTick(LoopQuantize) const { return 0; }
void LoopEngine::commitWaitingAction(MidiTransport&, uint32_t) {}   // B3 : signature étendue
```

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS, 0 warning. (Stubs returning default values, no logic.)

- [ ] **Step 4: Auto-review**

```bash
grep -n "void LoopEngine::" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/LoopEngine.cpp | wc -l
```
Expected: count matches header private method count (~20). Cross-check no method declared in `.h` is missing from `.cpp` (each declared private/public method has a definition stub, even no-op).

### Task 3 — Implement refcount + scheduleNoteOff + flushPendingNoteOffs + drainPendingNoteOffs

**Files:**
- Modify: `src/loop/LoopEngine.cpp`

- [ ] **Step 1: Read `src/arp/ArpEngine.cpp` lines 829-865 + 871-904 (template)**

Reference :
- `processEvents()` ArpEngine.cpp:829-842 — drain by `nowUs - fireTimeUs >= 0`
- `flushPendingNoteOffs()` ArpEngine.cpp:852-865 — 3-phase flush
- `scheduleEvent()` ArpEngine.cpp:871-883 — array scan, return false on full
- `refCountNoteOn/Off` ArpEngine.cpp:889-904 — refcount lifecycle

- [ ] **Step 2: Replace stubs in `src/loop/LoopEngine.cpp` for refcount + scheduleNoteOff + flush + drain**

Locate the stubs `void LoopEngine::flushPendingNoteOffs(MidiTransport&) {}` etc. and replace:

```cpp
// =================================================================
// scheduleNoteOff — queue a future noteOff (gate length or stuck flush)
// Returns false if queue full (silent drop, no crash).
// =================================================================
bool LoopEngine::scheduleNoteOff(uint32_t fireTimeUs, uint8_t note) {
  for (uint8_t i = 0; i < MAX_LOOP_PENDING_NOTEOFFS; i++) {
    if (!_pendingNoteOffs[i].active) {
      _pendingNoteOffs[i].fireTimeUs = fireTimeUs;
      _pendingNoteOffs[i].note       = note;
      _pendingNoteOffs[i].active     = true;
      return true;
    }
  }
  return false;  // queue full — note will rely on next flushPendingNoteOffs for safety
}

// =================================================================
// drainPendingNoteOffs — fire pending noteOffs whose time has arrived
// =================================================================
void LoopEngine::drainPendingNoteOffs(MidiTransport& transport, uint32_t nowUs) {
  for (uint8_t i = 0; i < MAX_LOOP_PENDING_NOTEOFFS; i++) {
    if (!_pendingNoteOffs[i].active) continue;
    if ((int32_t)(nowUs - _pendingNoteOffs[i].fireTimeUs) >= 0) {
      refCountNoteOff(transport, _pendingNoteOffs[i].note);
      _pendingNoteOffs[i].active = false;
    }
  }
}

// =================================================================
// Refcounted MIDI noteOn / noteOff (symetric ArpEngine pattern Q2 §28)
// =================================================================
void LoopEngine::refCountNoteOn(MidiTransport& transport, uint8_t note, uint8_t velocity) {
  if (_noteRefCount[note] == 0) {
    transport.sendNoteOn(_channel, note, velocity);
  }
  if (_noteRefCount[note] < 255) {
    _noteRefCount[note]++;
  }
}

void LoopEngine::refCountNoteOff(MidiTransport& transport, uint8_t note) {
  if (_noteRefCount[note] == 0) return;
  _noteRefCount[note]--;
  if (_noteRefCount[note] == 0) {
    transport.sendNoteOn(_channel, note, 0);  // velocity 0 = noteOff convention
  }
}

// =================================================================
// flushPendingNoteOffs — emergency silence + state → STOPPED.
// Symetric ArpEngine::flushPendingNoteOffs (ArpEngine.cpp:864 fait `_playing = false`).
// Audit-fix B2 : sans la transition d'état, midiPanic() laisse les LOOP banks en
// PLAYING/OVERDUBBING ; au prochain update() phase 3, refCountNoteOn repart de 0→1
// et MIDI noteOn est ré-émis → panic inefficace. Engine self-stop ferme l'invariant
// "flush = stop" par construction.
// Callers cross-check (tous neutres ou voulus) :
//   - stopRecording   : flush → STOPPED, puis startPlayback → PLAYING. Net = PLAYING.
//   - longPressClear  : flush → STOPPED, puis _state = EMPTY en fin. Net = EMPTY.
//   - stopPlayback(_,true) : flush → STOPPED, puis set STOPPED. Net = STOPPED (idempotent).
//   - midiPanic       : flush → STOPPED. Net = STOPPED (cible voulue, plus de resume audio).
// =================================================================
void LoopEngine::flushPendingNoteOffs(MidiTransport& transport) {
  // Phase 1 : cancel pending noteOff queue
  for (uint8_t i = 0; i < MAX_LOOP_PENDING_NOTEOFFS; i++) {
    _pendingNoteOffs[i].active = false;
  }
  // Phase 2 : sweep refcount — any note > 0 gets a hard noteOff
  for (uint8_t n = 0; n < 128; n++) {
    if (_noteRefCount[n] > 0) {
      transport.sendNoteOn(_channel, n, 0);
      _noteRefCount[n] = 0;
    }
  }
  // Phase 3 (audit-fix B2) : engine self-stop. Voir block comment ci-dessus.
  _state = LoopState::STOPPED;
}
```

Delete the obsolete stubs (`void LoopEngine::flushPendingNoteOffs(MidiTransport&) {}` etc.).

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

- [ ] **Step 4: Auto-review**

```bash
grep -n "refCountNoteOn\|refCountNoteOff\|scheduleNoteOff\|drainPendingNoteOffs\|flushPendingNoteOffs" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/LoopEngine.cpp
```
Expected: each method has exactly one definition (no leftover stub).

### Task 4 — Implement `resolvePadToMidiNote` + `applyVelocityVariation` + tapRec/tapPlayStop/longPressClear state-only stubs

**Files:**
- Modify: `src/loop/LoopEngine.cpp`

- [ ] **Step 1: Replace `resolvePadToMidiNote` + `applyVelocityVariation` stubs**

Locate `uint8_t LoopEngine::resolvePadToMidiNote(...)` and `uint8_t LoopEngine::applyVelocityVariation(...)` :

```cpp
// =================================================================
// resolvePadToMidiNote — pad → MIDI note (spec §1, no scale, no padOrder if null)
// =================================================================
// Convention GM : pad 0 = kick C2 = MIDI 36. With padOrder applied, the user's
// "ordered position" of the pad determines the note offset.
uint8_t LoopEngine::resolvePadToMidiNote(uint8_t padIndex) const {
  if (padIndex >= NUM_KEYS) return MIDI_BASE_NOTE;
  uint8_t pos = _padOrder ? _padOrder[padIndex] : padIndex;
  uint16_t note = (uint16_t)MIDI_BASE_NOTE + pos;
  return (note > 127) ? 127 : (uint8_t)note;
}

// =================================================================
// applyVelocityVariation — symmetric processNormalMode main.cpp:676-680
// =================================================================
uint8_t LoopEngine::applyVelocityVariation(uint8_t baseVel) const {
  if (_velocityVariation == 0) return baseVel;
  int16_t range = (int16_t)_velocityVariation * 127 / 200;
  int16_t offset = (int16_t)(random(-range, range + 1));
  int16_t result = (int16_t)baseVel + offset;
  if (result < 1) result = 1;
  if (result > 127) result = 127;
  return (uint8_t)result;
}
```

- [ ] **Step 2: Replace `tapRec` stub with state machine dispatch (no recording capture yet — Tasks 19-22 implement startRecording / stopRecording / overdub merge)**

> **Notes post-audit** :
> - `_state = LoopState::PLAYING` redondant dans WAITING_STOP case supprimé (M1 fix).
> - `startPlayback(transport, nowUs)` reçoit `micros()` capturé localement (B3 fix : tapRec n'est pas dans `update()`, donc safe d'appeler `micros()` directement ici).

```cpp
// =================================================================
// tapRec — transport action, state machine dispatch (spec §7 §8 + Q5 §28)
// =================================================================
void LoopEngine::tapRec(MidiTransport& transport) {
  switch (_state) {
    case LoopState::EMPTY:
      startRecording(transport);
      break;
    case LoopState::RECORDING:
      stopRecording(transport);  // bar-snap + → PLAYING via startPlayback interne
      break;
    case LoopState::PLAYING:
      // Enter OVERDUBBING — main buffer continues playback, overdub captures additions
      _overdubCount = 0;
      _state = LoopState::OVERDUBBING;
      break;
    case LoopState::OVERDUBBING:
      // M2 / M4 : mergeOverdub retourne false si capacité dépassée → abandon atomique silent.
      // Si OK : main buffer merged, → PLAYING. Si échec : overdub déjà reset, stay PLAYING.
      mergeOverdub();
      _state = LoopState::PLAYING;
      break;
    case LoopState::STOPPED:
      // Q5 §28 : tap REC on STOPPED-loaded → PLAYING + OVERDUBBING simultaneously
      startPlayback(transport, micros());   // B3 : nowUs capturé localement
      _overdubCount = 0;
      _state = LoopState::OVERDUBBING;
      break;
    case LoopState::WAITING_PLAY:
      // Spec §17 : REC during WAITING_PLAY ignored (REC senseless on STOPPED-pending-play)
      break;
    case LoopState::WAITING_STOP:
      // Spec §17 : REC during WAITING_STOP cancels stop and enters OVERDUBBING.
      // M1 fix : pas de double-assignment _state ici, directement OVERDUBBING.
      _overdubCount = 0;
      _state = LoopState::OVERDUBBING;
      break;
  }
}

// =================================================================
// tapPlayStop — transport action, state machine dispatch (spec §9 §17)
// currentKeys = keyIsPressed (FG context) or nullptr (BG context).
// Param non utilisé actuellement (réservé), peut être ignoré par l'impl.
// =================================================================
void LoopEngine::tapPlayStop(MidiTransport& transport, const uint8_t* /*currentKeys*/) {
  switch (_state) {
    case LoopState::EMPTY:
      // No-op : nothing to play
      break;
    case LoopState::STOPPED:
      if (_quantize == LOOP_QUANT_FREE) {
        startPlayback(transport, micros());     // B3 : nowUs capturé localement (hors update())
      } else {
        _waitingTargetTick = computeNextBoundaryTick(_quantize);
        _state = LoopState::WAITING_PLAY;
      }
      break;
    case LoopState::PLAYING:
      if (_quantize == LOOP_QUANT_FREE) {
        stopPlayback(transport, /*flushNotes=*/true);
      } else {
        _waitingTargetTick = computeNextBoundaryTick(_quantize);
        _state = LoopState::WAITING_STOP;
      }
      break;
    case LoopState::OVERDUBBING:
      // Spec §8 : abandon overdub, stay PLAYING. A second tap then stops normally.
      abandonOverdub();
      _state = LoopState::PLAYING;
      break;
    case LoopState::RECORDING:
      // No-op : PLAY/STOP during RECORDING ignored (REC is the only way out)
      break;
    case LoopState::WAITING_PLAY:
      // Spec §17 : cancel waiting, return STOPPED
      _state = LoopState::STOPPED;
      break;
    case LoopState::WAITING_STOP:
      // Spec §17 : cancel waiting, return PLAYING
      _state = LoopState::PLAYING;
      break;
  }
}

// =================================================================
// longPressClear — destructive wipe (spec §9). Called once threshold fired.
// Refused during RECORDING/OVERDUBBING (caller filters via isLocked()).
// B2 fix : set _clearFired=true à la fin pour bloquer re-fires tant que CLEAR tenu.
// =================================================================
void LoopEngine::longPressClear(MidiTransport& transport) {
  if (isLocked()) return;  // safety net (caller should not invoke when locked)
  // Wipe buffer + flush MIDI notes + state → EMPTY
  flushPendingNoteOffs(transport);
  _eventCount = 0;
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) _events[i].active = false;
  _overdubCount = 0;
  _loopDurationUs = 0;
  _loopBars = 0;
  _state = LoopState::EMPTY;
  // B2 fix : armé true. isClearHoldFired retourne false jusqu'au release CLEAR
  // ou nouveau press (notifyClearPressStart / End reset _clearFired).
  _clearFired = true;
}
```

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS — `startRecording`, `stopRecording`, `startPlayback`, `stopPlayback`, `mergeOverdub`, `abandonOverdub`, `computeNextBoundaryTick` are still stubs returning default. Compile must pass with these dependencies (they exist).

- [ ] **Step 4: Auto-review**

```bash
grep -n "case LoopState::" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/LoopEngine.cpp | wc -l
```
Expected: at least 14 (each switch has 7 cases × 2 switches in tapRec + tapPlayStop minimum).

---

# Phase 2.B — BankSlot extension + main.cpp boot wiring

Objectif : étendre `BankSlot` du field `loopEngine*`, allouer `s_loopEngines[MAX_LOOP_BANKS]`, assigner au boot, init channel + padOrder. Pas encore de dispatch (Phase 2.D). Une bank LOOP créée via Tool 5 doit pouvoir booter sans crash.

> **Audit-fix I5 — ordre des tasks (compile-time) vs ordre runtime** :
> L'ordre du plan est Task 5 (ajout field) → Task 6 (allocation `s_loopEngines` + assignment block post-`loadAll`) → Task 7 (init `loopEngine = nullptr` au bank init, main.cpp:343-355). Compile-time, cet ordre garantit que le field existe avant que Task 6 le référence.
>
> À l'exécution, l'ordre est inverse : main.cpp ligne ~344 initialise `loopEngine = nullptr` (code Task 7), puis `loadAll()` (~L393), puis le block d'assignment (code Task 6 step 5, post-`loadAll`, ~L527+). Le `nullptr` initial est ainsi écrasé par l'assignation pour les banks LOOP, et les autres types restent à `nullptr` (consommé par les checks `if (slot.loopEngine)` partout ailleurs).

### Task 5 — Extend `BankSlot` struct + forward declaration `LoopEngine`

**Files:**
- Modify: `src/core/KeyboardData.h:365-377`

- [ ] **Step 1: Read full target region**

```bash
sed -n '360,380p' /Users/loic/Code/PROJECTS/ILLPAD_V2/src/core/KeyboardData.h
```

- [ ] **Step 2: Add forward declaration after `class ArpEngine;` (KeyboardData.h:366)**

Insert below `class ArpEngine;` :

```cpp
// Forward declaration
class ArpEngine;
class LoopEngine;
```

- [ ] **Step 3: Add `loopEngine` field to `BankSlot` struct (KeyboardData.h:368-377)**

Original:
```cpp
struct BankSlot {
  uint8_t     channel;                    // 0-7 (fixed, = bank index)
  BankType    type;                       // NORMAL or ARPEG
  ScaleConfig scale;
  ArpEngine*  arpEngine;                  // non-null if ARPEG
  bool        isForeground;
  uint8_t     baseVelocity;              // 1-127, per-bank (NORMAL + ARPEG)
  uint8_t     velocityVariation;         // 0-100%, per-bank (NORMAL + ARPEG)
  uint16_t    pitchBendOffset;           // 0-16383, center=8192 (NORMAL only)
};
```

Modified (add `loopEngine` after `arpEngine`):
```cpp
struct BankSlot {
  uint8_t     channel;                    // 0-7 (fixed, = bank index)
  BankType    type;                       // NORMAL / ARPEG / LOOP / ARPEG_GEN
  ScaleConfig scale;
  ArpEngine*  arpEngine;                  // non-null if ARPEG / ARPEG_GEN
  LoopEngine* loopEngine;                 // non-null if LOOP
  bool        isForeground;
  uint8_t     baseVelocity;              // 1-127, per-bank (NORMAL + ARPEG + LOOP)
  uint8_t     velocityVariation;         // 0-100%, per-bank (NORMAL + ARPEG + LOOP)
  uint16_t    pitchBendOffset;           // 0-16383, center=8192 (NORMAL only)
};
```

- [ ] **Step 4: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS. `loopEngine` field exists but uninitialized — main.cpp boot init Task 7 sets it to nullptr.

### Task 6 — Allocate `s_loopEngines[MAX_LOOP_BANKS]` in main.cpp + assign at boot

**Files:**
- Modify: `src/main.cpp:60-85` (statics)
- Modify: `src/main.cpp:507-526` (engine assignment block)

- [ ] **Step 1: Read static globals region**

```bash
sed -n '55,90p' /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
```

- [ ] **Step 2: Add `#include "loop/LoopEngine.h"` to main.cpp includes**

Find the existing includes block (search `#include "arp/ArpEngine.h"`) and add below:

```cpp
#include "arp/ArpEngine.h"
#include "arp/ArpScheduler.h"
#include "loop/LoopEngine.h"     // Phase 2 LOOP : s_loopEngines instances
```

- [ ] **Step 3: Bump `MAX_LOOP_BANKS = 2` → `4` dans `src/core/KeyboardData.h:659`** (D1 décision actée post-audit)

```bash
sed -n '655,662p' /Users/loic/Code/PROJECTS/ILLPAD_V2/src/core/KeyboardData.h
```

Edit ligne 659 :

Original :
```cpp
const uint8_t MAX_LOOP_BANKS   = 2;
```

Modified :
```cpp
const uint8_t MAX_LOOP_BANKS   = 4;
```

(SRAM impact : 4 × ~9.7 KB ≈ 38.8 KB, sur 320 KB total. Confortable. Cf décisions techniques header.)

- [ ] **Step 4: Add `s_loopEngines[MAX_LOOP_BANKS]` static declaration**

Locate `static ArpEngine s_arpEngines[4];` (main.cpp:72) and insert below:

```cpp
static ArpEngine s_arpEngines[4];
static LoopEngine s_loopEngines[MAX_LOOP_BANKS];  // Phase 2 LOOP : MAX_LOOP_BANKS = 4 (KeyboardData.h:659)
```

- [ ] **Step 5: Add LoopEngine assignment block + warning >MAX_LOOP_BANKS (m2 fix)**

Read existing block first :

```bash
sed -n '505,570p' /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
```

Locate the end of the `for (uint8_t i = 0; i < NUM_BANKS && arpIdx < 4; i++)` loop closing on line 526. **After** that closing `}` (after the `#if DEBUG_SERIAL` "No ARPEG banks" trace), and **before** `// ArpScheduler — register engines + set scale/padOrder context`, insert:

```cpp
  // Assign LoopEngines to BANK_LOOP banks (Phase 2 LOOP)
  {
    uint8_t loopIdx = 0;
    for (uint8_t i = 0; i < NUM_BANKS && loopIdx < MAX_LOOP_BANKS; i++) {
      if (s_banks[i].type == BANK_LOOP) {
        s_loopEngines[loopIdx].setChannel(i);
        s_loopEngines[loopIdx].setPadOrder(s_padOrder);
        s_loopEngines[loopIdx].setClockManager(&s_clockManager);
        s_loopEngines[loopIdx].setBaseVelocity(s_banks[i].baseVelocity);
        s_loopEngines[loopIdx].setVelocityVariation(s_banks[i].velocityVariation);
        s_loopEngines[loopIdx].setClearLoopTimerMs(s_settings.clearLoopTimerMs);
        // Quantize per-bank : LOOP interprets BankTypeStore::quantize[i] as 0..2 (Free/Beat/Bar).
        // validateBankTypeStore (KeyboardData.h:725-726) already clamps via type discrimination.
        s_loopEngines[loopIdx].setQuantize((LoopQuantize)s_nvsManager.getLoadedQuantizeMode(i));
        s_banks[i].loopEngine = &s_loopEngines[loopIdx];
        loopIdx++;
        #if DEBUG_SERIAL
        Serial.printf("[BOOT] Bank %d: LOOP, LoopEngine assigned\n", i + 1);
        #endif
      }
    }
    #if DEBUG_SERIAL
    if (loopIdx == 0) {
      Serial.println("[BOOT] No LOOP banks configured.");
    }
    // m2 fix (audit Q6) : warning explicite si NVS contient > MAX_LOOP_BANKS LOOP banks.
    // Cas pathologique : NVS corruption ou downgrade depuis future firmware MAX>4.
    // Tool 5 refacto enforce le cap au cycle UI, donc impossible en usage normal.
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (s_banks[i].type == BANK_LOOP && s_banks[i].loopEngine == nullptr) {
        Serial.printf("[WARN] Bank %u BANK_LOOP exceeds MAX_LOOP_BANKS=%u, runtime disabled\n",
                      i + 1, MAX_LOOP_BANKS);
      }
    }
    #endif
  }
```

- [ ] **Step 6: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS. Note : `setControlPads` not yet called here (Task 11 reads LoopPadStore from NvsManager and applies it). For now, REC/PLAY/CLEAR remain 0xFF (unassigned).

### Task 7 — Init `loopEngine = nullptr` on bank defaults (main.cpp:343-355)

**Files:**
- Modify: `src/main.cpp:343-355`

- [ ] **Step 1: Read bank init region**

```bash
sed -n '340,360p' /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
```

- [ ] **Step 2: Add `loopEngine = nullptr` to bank init loop**

Original (main.cpp:343-355):
```cpp
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
```

Modified (add `loopEngine = nullptr` after `arpEngine = nullptr`):
```cpp
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
```

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 8 — HW Gate G1 + commit Phase 2.A + 2.B

**Files:** none (HW + commit gate)

- [ ] **Step 1: Boot test setup**

Pré-requis HW : avoir au moins 1 bank LOOP créée via Tool 5 setup mode (livré commit `e2857c5`). Si non, entrer en setup mode au boot (rear button hold), naviguer Tool 5, cycler une bank vers LOOP, sauver, reboot.

- [ ] **Step 2: Upload firmware**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload 2>&1 | tail -20
```

- [ ] **Step 3: Boot monitor**

```bash
~/.platformio/penv/bin/pio device monitor -b 115200
```

**HW Gate G1 expected output** :
- `[BOOT] === ILLPAD48 V2 ===`
- ... séquence boot habituelle ...
- `[BOOT] Bank N: LOOP, LoopEngine assigned` (au moins une si une bank LOOP existe)
- Pas de `[FATAL]`, pas de crash, pas de stack overflow
- `[BOOT] Ready.`

**Si plus de MAX_LOOP_BANKS=2 banks sont LOOP** : la boucle d'assignment arrête à 2, les banks suivantes restent avec `loopEngine = nullptr` — c'est OK pour le boot mais signale un déficit (Tool 5 cap doit empêcher mais cas pathologique post-NVS corruption à surveiller).

- [ ] **Step 4: Confirmer HW gate G1 OK avec utilisateur (texte plain, pas AskUserQuestion)**

Présenter texte : "Boot stable avec bank LOOP assignée, LoopEngine en mémoire, pas de crash. OK pour commit ?"

- [ ] **Step 5: Commit gate Phase 2.A + 2.B**

```bash
git add src/loop/LoopEngine.h src/loop/LoopEngine.cpp src/core/KeyboardData.h src/main.cpp
git commit -m "$(cat <<'EOF'
feat(loop): LoopEngine class skeleton + BankSlot extension + boot wiring (Phase 2.A+B)

- New src/loop/LoopEngine.{h,cpp} : state machine enum (7 states), buffer caps,
  full public API (tapRec/tapPlayStop/longPressClear/capturePadEvent/update),
  refcount noteOn/Off + scheduleNoteOff + drainPendingNoteOffs + flush
  (dupliqué from ArpEngine pattern, Q2 §28 spec — no factorization)
- BankSlot extended with loopEngine* field (KeyboardData.h)
- s_loopEngines[MAX_LOOP_BANKS] static allocation in main.cpp
- Boot assignment loop : LoopEngine attached to BANK_LOOP banks with
  channel/padOrder/ClockManager/baseVelocity/quantize from NVS (loaded
  by Phase 1 BankTypeStore v4 + SettingsStore v11)
- Recording / playback / overdub logic = stubs (Tasks 19-46)
- HW gate G1 OK : boot with LOOP bank doesn't crash, trace shows assignment

Spec : docs/superpowers/specs/2026-04-19-loop-mode-design.md §3, §23
Plan : docs/superpowers/plans/2026-05-18-loop-phase-2-plan.md
EOF
)"
```

---

# Phase 2.C — NvsManager LoopPadStore + LoopPotStore load

Objectif : charger `LoopPadStore` (single descriptor index 12, déjà déclaré Phase 1) et `LoopPotStore` per-bank (multi-key `loop_0..loop_7`, non descripteurisé) au boot. Appliquer aux LoopEngines (control pad indices). Validation HW G2 : control pads REC/PLAY/CLEAR fonctionnels selon Tool 3 LOOP sous-page **future** — mais pour Phase 2, la config NVS peut être seedée manuellement par défaut (Tool 3 b1 est Phase 3, donc en Phase 2 la `LoopPadStore` est presque toujours vide = 0xFF partout). Phase 2.D devra accepter `0xFF` = unassigned et continuer (les controls sont alors inopérants, le user crée une bank LOOP mais ne peut pas REC/PLAY/CLEAR sans Tool 3 b1 — c'est attendu).

**Workaround Phase 2** : ajouter un override compile-time dans `LoopPadStore` defaults pour seeder REC=pad 32, PLAY/STOP=pad 33, CLEAR=pad 34 par défaut (debug/dev), permettant les HW gates G3-G9 sans dépendre de Phase 3. À retirer Phase 3 quand Tool 3 b1 livre l'UI propre.

### Task 9 — Add `_loadedLoopPad` + `_loadedLoopPot[NUM_BANKS]` members + accessors to NvsManager

**Files:**
- Modify: `src/managers/NvsManager.h:160-194` (private members area)

- [ ] **Step 1: Read NvsManager.h private area**

```bash
sed -n '110,200p' /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/NvsManager.h
```

- [ ] **Step 2: Add accessors + helper M7 (dev seed conditionnel) to public API (after `getLoadedControlPadStore`, NvsManager.h:83)**

```cpp
  // Access loaded control pads (for ControlPadManager init at boot, after loadAll)
  const ControlPadStore& getLoadedControlPadStore() const;

  // Access loaded LOOP pad assignment (3 control pads + 16 slot pads, Phase 1 declared)
  const LoopPadStore& getLoadedLoopPadStore() const;
  // Access loaded LOOP pot params per-bank (5 effects)
  const LoopPotStore& getLoadedLoopPotParams(uint8_t bankIdx) const;

  // M7 fix (audit Q5=c) : dev seed conditionnel pads 32/33/34 pour HW gates Phase 2.
  // À appeler APRÈS loadAll() (qui peuple _loadedLoopPad et _ctrlStore).
  // Seede pads {32, 33, 34} dans _loadedLoopPad UNIQUEMENT si :
  //   - _loadedLoopPad.recPad == 0xFF (NVS LoopPadStore vide ou absent)
  //   - aucune entry _ctrlStore.entries[*].padIndex ∈ {32, 33, 34}
  // Sinon laisse en l'état (et trace serial le cas). À retirer Phase 3 quand Tool 3 b1 livre.
  void applyDevSeedLoopPadsIfSafe();
```

- [ ] **Step 3: Add members to private area (after `_ctrlStore`, NvsManager.h:168)**

```cpp
  // Control pads (loaded at boot from NVS, consumed by ControlPadManager::applyStore)
  ControlPadStore _ctrlStore;

  // LOOP pad assignment (loaded at boot from NVS, applied to LoopEngines)
  LoopPadStore _loadedLoopPad;
  LoopPotStore _loadedLoopPot[NUM_BANKS];  // per-bank, multi-key loop_0..loop_7
```

- [ ] **Step 4: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: build error "accessor declared but not defined". Task 10 implements.

### Task 10 — Implement accessors + extend `loadAll` to load LoopPadStore + LoopPotStore

**Files:**
- Modify: `src/managers/NvsManager.cpp` (constructor defaults + accessors + loadAll body)

- [ ] **Step 1: Find existing `loadAll` definition**

```bash
grep -n "void NvsManager::loadAll\|::getLoadedControlPadStore" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/NvsManager.cpp
```

- [ ] **Step 2: Add accessor implementations next to `getLoadedControlPadStore` (file location TBD by Step 1 grep)**

```cpp
const ControlPadStore& NvsManager::getLoadedControlPadStore() const {
  return _ctrlStore;
}

const LoopPadStore& NvsManager::getLoadedLoopPadStore() const {
  return _loadedLoopPad;
}

const LoopPotStore& NvsManager::getLoadedLoopPotParams(uint8_t bankIdx) const {
  static LoopPotStore safe{
    .magic = LOOPPOT_MAGIC,
    .version = LOOPPOT_VERSION,
    .reserved = 0,
    .shuffleDepthRaw = 0,
    .shuffleTemplate = 0,
    .chaosRaw = 0,
    .velPattern = 0,
    .velPatternDepthRaw = 0,
  };
  if (bankIdx >= NUM_BANKS) return safe;
  return _loadedLoopPot[bankIdx];
}
```

- [ ] **Step 3: Add defaults init in constructor — TOUS UNASSIGNED (M7 fix : pas de dev seed inconditionnel)**

Locate NvsManager.cpp constructor body (lines 12-175). After the `_ctrlStore` init block (around L154-159, `memset(&_ctrlStore, 0, ...)` then `_ctrlStore.magic = CONTROLPAD_MAGIC`), insert :

```cpp
  // LOOP pad defaults : tous 0xFF (non assignés). Phase 3 (Tool 3 b1) ou Phase 2 dev seed
  // conditionnel (applyDevSeedLoopPadsIfSafe, appelé post-loadAll par main.cpp) les écrasera
  // si collision Tool 4 absente.
  _loadedLoopPad.magic       = EEPROM_MAGIC;
  _loadedLoopPad.version     = LOOPPAD_VERSION;
  _loadedLoopPad.reserved    = 0;
  _loadedLoopPad.recPad      = 0xFF;
  _loadedLoopPad.playStopPad = 0xFF;
  _loadedLoopPad.clearPad    = 0xFF;
  for (uint8_t i = 0; i < 16; i++) _loadedLoopPad.slotPads[i] = 0xFF;

  // LOOP pot defaults per-bank (5 effects, Phase 5 will route runtime)
  for (uint8_t b = 0; b < NUM_BANKS; b++) {
    _loadedLoopPot[b].magic              = LOOPPOT_MAGIC;
    _loadedLoopPot[b].version            = LOOPPOT_VERSION;
    _loadedLoopPot[b].reserved           = 0;
    _loadedLoopPot[b].shuffleDepthRaw    = 0;
    _loadedLoopPot[b].shuffleTemplate    = 0;
    _loadedLoopPot[b].chaosRaw           = 0;
    _loadedLoopPot[b].velPattern         = 0;
    _loadedLoopPot[b].velPatternDepthRaw = 0;
  }
```

- [ ] **Step 4: Extend `loadAll` to load `LoopPadStore` (single descriptor index 12)**

In `loadAll`, find the spot where `ControlPadStore` is loaded (search `CONTROLPAD_NVS_NAMESPACE` in NvsManager.cpp). After that block, add:

```cpp
  // === LoopPadStore (Phase 1 declared, Phase 2 loaded) ===
  {
    LoopPadStore tmp;
    if (loadBlob(LOOPPAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY,
                 EEPROM_MAGIC, LOOPPAD_VERSION, &tmp, sizeof(tmp))) {
      validateLoopPadStore(tmp);
      _loadedLoopPad = tmp;
      #if DEBUG_SERIAL
      Serial.printf("[BOOT NVS] LoopPadStore loaded : rec=%u playStop=%u clear=%u\n",
                    _loadedLoopPad.recPad, _loadedLoopPad.playStopPad, _loadedLoopPad.clearPad);
      #endif
    } else {
      #if DEBUG_SERIAL
      Serial.println("[BOOT NVS] LoopPadStore not found, using Phase 2 dev defaults (pads 32/33/34)");
      #endif
    }
  }

  // === LoopPotStore per-bank (multi-key loop_0..loop_7, Phase 2 loaded for future Phase 5 runtime) ===
  {
    char key[16];
    for (uint8_t b = 0; b < NUM_BANKS; b++) {
      snprintf(key, sizeof(key), "loop_%u", b);
      LoopPotStore tmp;
      if (loadBlob(LOOP_POT_NVS_NAMESPACE, key,
                   EEPROM_MAGIC, LOOPPOT_VERSION, &tmp, sizeof(tmp))) {
        validateLoopPotStore(tmp);
        _loadedLoopPot[b] = tmp;
      }
      // else : keep constructor default (above)
    }
  }
```

- [ ] **Step 5: Implement `applyDevSeedLoopPadsIfSafe` (M7 fix Q5=c)**

Add at end of NvsManager.cpp (or wherever helper functions live) :

```cpp
// =================================================================
// applyDevSeedLoopPadsIfSafe — Phase 2 dev seed conditionnel (audit M7 / Q5=c)
// =================================================================
// Seede pads {32, 33, 34} dans _loadedLoopPad UNIQUEMENT si :
//   - LoopPadStore vide en NVS (post-loadAll : _loadedLoopPad.recPad == 0xFF)
//   - aucune entry ControlPadStore.entries[*].padIndex ∈ {32, 33, 34}
// Sinon laisse en l'état + trace serial. À retirer Phase 3.
void NvsManager::applyDevSeedLoopPadsIfSafe() {
  // Skip si NVS a déjà des LOOP pads valides.
  if (_loadedLoopPad.recPad != 0xFF) {
    #if DEBUG_SERIAL
    Serial.printf("[BOOT] LOOP dev seed skipped: NVS LoopPadStore has data (rec=%u)\n",
                  _loadedLoopPad.recPad);
    #endif
    return;
  }
  // Check collision avec ControlPad assignments Tool 4.
  for (uint8_t i = 0; i < _ctrlStore.count; i++) {
    uint8_t p = _ctrlStore.entries[i].padIndex;
    if (p == 32 || p == 33 || p == 34) {
      #if DEBUG_SERIAL
      Serial.printf("[BOOT] LOOP dev seed skipped: pad %u already assigned as ControlPad (Tool 4)\n", p);
      Serial.println("[BOOT]   → REC/PLAY/CLEAR LOOP control pads will stay unassigned until Tool 3 b1 (Phase 3).");
      #endif
      return;
    }
  }
  // Safe : seed.
  _loadedLoopPad.recPad      = 32;
  _loadedLoopPad.playStopPad = 33;
  _loadedLoopPad.clearPad    = 34;
  #if DEBUG_SERIAL
  Serial.println("[BOOT] LOOP dev seed applied: rec=32 playStop=33 clear=34 (Phase 2 testing, remove Phase 3)");
  #endif
}
```

Et **appeler** depuis main.cpp après `loadAll` (Phase 2.B Task 6 step 5 area, AVANT le block d'assignment LoopEngine) :

```cpp
// M7 fix : dev seed conditionnel pour HW gates Phase 2. Retire Phase 3 (Tool 3 b1).
s_nvsManager.applyDevSeedLoopPadsIfSafe();
```

- [ ] **Step 6: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 11 — Apply LoopPadStore to s_loopEngines at boot

**Files:**
- Modify: `src/main.cpp` (LoopEngine assignment block from Task 6)

- [ ] **Step 1: Locate the LoopEngine assignment block added in Task 6**

```bash
grep -n "s_loopEngines\[loopIdx\].setChannel" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
```

- [ ] **Step 2: Hoist `lps` reference + add `setControlPads` call inside the loop (m8 audit fix)**

`LoopPadStore` est shared cross-bank (spec §5 : 3 controls + 16 slots partagés). Hoist la référence hors de la for-loop banks pour clarté.

**Audit-fix I1 — EDIT in-place, pas REPLACE** : cette step modifie le block d'assignment ajouté Task 6 step 5 par 2 ajouts surgicaux. NE PAS effacer le block entier puis recopier le snippet ci-dessous (qui est tronqué). Procédure :

1. AVANT la for-loop `for (uint8_t i = 0; i < NUM_BANKS && loopIdx < MAX_LOOP_BANKS; i++)`, hoist la ligne :
   ```cpp
   const LoopPadStore& lps = s_nvsManager.getLoadedLoopPadStore();
   ```
2. DANS la for-loop, juste après `s_loopEngines[loopIdx].setQuantize((LoopQuantize)s_nvsManager.getLoadedQuantizeMode(i));`, insérer :
   ```cpp
   // Control pads (REC/PLAY/CLEAR) — shared across all LOOP banks per spec §5
   s_loopEngines[loopIdx].setControlPads(lps.recPad, lps.playStopPad, lps.clearPad);
   ```
3. Conserver INCHANGÉ le reste du block Task 6 step 5 : le `s_banks[i].loopEngine = &s_loopEngines[loopIdx]; loopIdx++;`, le `Serial.printf("[BOOT] Bank %d: LOOP, LoopEngine assigned\n", i + 1);`, le `if (loopIdx == 0)` trace, et le `[WARN] Bank %u BANK_LOOP exceeds MAX_LOOP_BANKS=%u` post-loop scan.

Forme finale (référence visuelle, NE PAS recopier intégralement — vérifier que les lignes Task 6 step 5 sont préservées) :
```cpp
  // m8 audit fix : LoopPadStore shared cross-bank, hoist out of loop.
  const LoopPadStore& lps = s_nvsManager.getLoadedLoopPadStore();
  {
    uint8_t loopIdx = 0;
    for (uint8_t i = 0; i < NUM_BANKS && loopIdx < MAX_LOOP_BANKS; i++) {
      if (s_banks[i].type == BANK_LOOP) {
        s_loopEngines[loopIdx].setChannel(i);
        s_loopEngines[loopIdx].setPadOrder(s_padOrder);
        s_loopEngines[loopIdx].setClockManager(&s_clockManager);
        s_loopEngines[loopIdx].setBaseVelocity(s_banks[i].baseVelocity);
        s_loopEngines[loopIdx].setVelocityVariation(s_banks[i].velocityVariation);
        s_loopEngines[loopIdx].setClearLoopTimerMs(s_settings.clearLoopTimerMs);
        s_loopEngines[loopIdx].setQuantize((LoopQuantize)s_nvsManager.getLoadedQuantizeMode(i));
        // Control pads (REC/PLAY/CLEAR) — shared across all LOOP banks per spec §5
        s_loopEngines[loopIdx].setControlPads(lps.recPad, lps.playStopPad, lps.clearPad);
        s_banks[i].loopEngine = &s_loopEngines[loopIdx];
        loopIdx++;
        #if DEBUG_SERIAL
        Serial.printf("[BOOT] Bank %d: LOOP, LoopEngine assigned\n", i + 1);
        #endif
      }
    }
    #if DEBUG_SERIAL
    if (loopIdx == 0) {
      Serial.println("[BOOT] No LOOP banks configured.");
    }
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (s_banks[i].type == BANK_LOOP && s_banks[i].loopEngine == nullptr) {
        Serial.printf("[WARN] Bank %u BANK_LOOP exceeds MAX_LOOP_BANKS=%u, runtime disabled\n",
                      i + 1, MAX_LOOP_BANKS);
      }
    }
    #endif
  }
```

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 12 — HW Gate G2 + commit Phase 2.C

- [ ] **Step 1: Upload + monitor**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload && \
  ~/.platformio/penv/bin/pio device monitor -b 115200
```

**HW Gate G2 expected output** (post M7 dev seed conditionnel) :
- `[BOOT NVS] LoopPadStore not found, using Phase 2 dev defaults (pads 32/33/34)` (premier boot post-flash, NVS vide pour LoopPad)
- Puis l'un des 3 cas selon état Tool 4 :
  - `[BOOT] LOOP dev seed applied: rec=32 playStop=33 clear=34 ...` (cas nominal, pas de Tool 4 ControlPad sur 32/33/34)
  - `[BOOT] LOOP dev seed skipped: pad N already assigned as ControlPad (Tool 4)` (collision détectée)
  - `[BOOT] LOOP dev seed skipped: NVS LoopPadStore has data` (Tool 3 b1 a écrit du NVS — improbable Phase 2)
- `[BOOT] Bank N: LOOP, LoopEngine assigned` pour chaque bank LOOP (jusqu'à MAX_LOOP_BANKS=4)
- (Si plus de 4 banks LOOP en NVS) `[WARN] Bank N BANK_LOOP exceeds MAX_LOOP_BANKS=4, runtime disabled` pour chaque excès
- Trace boot complète sans crash

- [ ] **Step 2: Commit gate Phase 2.C**

```bash
git add src/managers/NvsManager.h src/managers/NvsManager.cpp src/main.cpp
git commit -m "$(cat <<'EOF'
feat(loop): NvsManager loads LoopPadStore + LoopPotStore at boot (Phase 2.C)

- NvsManager extended with _loadedLoopPad (LoopPadStore single, descriptor
  index 12 from Phase 1) + _loadedLoopPot[NUM_BANKS] (LoopPotStore per-bank,
  multi-key loop_0..loop_7 sous illpad_lpot namespace)
- Accessors getLoadedLoopPadStore() / getLoadedLoopPotParams(bankIdx) added
- Constructor seeds LOOP defaults : recPad=32, playStopPad=33, clearPad=34
  (Phase 2 dev seed — to be REMOVED Phase 3 quand Tool 3 b1 livre l'UI propre)
- loadAll() étendue : charge LoopPadStore + 8 LoopPotStore au boot
- main.cpp LoopEngine assignment block applique setControlPads avec
  les valeurs de LoopPadStore
- HW gate G2 OK : boot trace confirme load (ou defaults fallback)

Spec : §5, §20 + Phase 1 commits 1b0ac8c (LoopPadStore) + 68855e3 (LoopPotStore)
EOF
)"
```

---

# Phase 2.D — processLoopMode + REC/PLAY/CLEAR detection

Objectif : main loop dispatch `BANK_LOOP → processLoopMode()`. REC tap → `tapRec()`. PLAY/STOP tap → `tapPlayStop()`. CLEAR long-press → `longPressClear()` après `clearLoopTimerMs`. Pads musicaux LOOP : edge detection mais pas encore d'enregistrement (Phase 2.E) ni de jeu (Phase 2.F).

### Task 13 — Add `processLoopMode()` function in main.cpp

**Files:**
- Modify: `src/main.cpp:702-794` (insert between `processArpMode` and `handleLeftReleaseCleanup`)

- [ ] **Step 1: Locate insertion point**

```bash
grep -n "^static void handleLeftReleaseCleanup\|^static void processArpMode" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
```
Expected: `processArpMode` at line 703, `handleLeftReleaseCleanup` at line 753.

- [ ] **Step 2: Insert `processLoopMode` immediately after `processArpMode` (after line 751)**

```cpp
// =================================================================
// processLoopMode — pad input handler for BANK_LOOP (Phase 2 LOOP)
// =================================================================
// Pad categories on a LOOP bank in foreground (musical layer, LEFT not held) :
//   - LoopEngine.recPad      → tapRec()
//   - LoopEngine.playStopPad → tapPlayStop()
//   - LoopEngine.clearPad    → notifyClearPressStart/End + longPressClear when threshold
//   - Tool 4 control pads    → handled by ControlPadManager (skip via isControlPad filter)
//   - All other pads         → musical drums : capturePadEvent + immediate noteOn/Off
//
// Q1 §7 loop-buffer-invariants : LOOP layer musical indépendant. Dispatch par
// bank type via handlePadInput — pas d'interaction avec processArpMode.
// =================================================================
static void processLoopMode(const SharedKeyboardState& state, BankSlot& slot, uint32_t now) {
  LoopEngine* le = slot.loopEngine;
  if (!le) return;

  // CLEAR long-press check (state-machine driven, not edge-driven)
  if (le->getClearPad() != 0xFF) {
    bool clearHeld = state.keyIsPressed[le->getClearPad()];
    if (clearHeld) {
      le->notifyClearPressStart(now);
      if (le->isClearHoldFired(now)) {
        le->longPressClear(s_transport);
        s_leds.triggerEvent(EVT_LOOP_CLEAR);
      }
    } else {
      le->notifyClearPressEnd();
    }
  }

  // Iterate musical pads (rising/falling edges)
  for (int i = 0; i < NUM_KEYS; i++) {
    if (i == s_holdPad) continue;
    if (s_controlPadManager.isControlPad(i)) continue;
    if (le->isLoopControlPad((uint8_t)i)) {
      // REC / PLAY/STOP tap on rising edge (CLEAR already handled above)
      if (i == le->getRecPad()) {
        if (state.keyIsPressed[i] && !s_lastKeys[i]) {
          le->tapRec(s_transport);
        }
      } else if (i == le->getPlayStopPad()) {
        if (state.keyIsPressed[i] && !s_lastKeys[i]) {
          le->tapPlayStop(s_transport, state.keyIsPressed);
        }
      }
      // CLEAR pad : already handled by sustained-press logic above
      continue;
    }

    // Musical pad — comportement Q1/Q8/M3/M8 actés post-audit :
    //   - Velocity passée à capturePadEvent : slot.baseVelocity strict, SANS variation
    //     (M3 fix Q8 : la variation s'applique seulement au playback dans update()).
    //   - capturePadEvent émet MIDI live dans TOUS les états (Q1/M8 spec §18 "percussion fixe") :
    //     EMPTY/STOPPED → live monitor strict, pas de capture buffer ;
    //     RECORDING/OVERDUBBING → live monitor + capture buffer (live-sort live, M2 fix) ;
    //     WAITING_* → live monitor strict (musique non-altérée pendant l'attente quantize).
    bool pressed    = state.keyIsPressed[i];
    bool wasPressed = s_lastKeys[i];
    if (pressed && !wasPressed) {
      // Rising edge : noteOn avec baseVelocity strict (M3 fix Q8 audit).
      le->capturePadEvent((uint8_t)i, slot.baseVelocity, s_transport);
    } else if (!pressed && wasPressed) {
      // Falling edge : noteOff (velocity == 0 convention).
      le->capturePadEvent((uint8_t)i, 0, s_transport);
    }
  }
}
```

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS (capturePadEvent is still a stub returning no-op, so musical pads do nothing yet — Phase 2.E implements).

### Task 14 — Wire `handlePadInput` switch case BANK_LOOP

**Files:**
- Modify: `src/main.cpp:775-794`

- [ ] **Step 1: Read switch region**

```bash
sed -n '775,795p' /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
```

- [ ] **Step 2: Replace `default:` branch with explicit `case BANK_LOOP:`**

Original:
```cpp
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
```

Modified:
```cpp
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
      case BANK_LOOP:
        if (slot.loopEngine) processLoopMode(state, slot, now);
        break;
      default:
        break;
    }
  }

  handleLeftReleaseCleanup(state);
}
```

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 15 — Add `[LOOP STATE]` viewer trace (debug aid for HW gate G3)

**Files:**
- Modify: `src/loop/LoopEngine.cpp` (add debug trace to tapRec / tapPlayStop / longPressClear)

- [ ] **Step 1: Add Serial trace to `tapRec`, `tapPlayStop`, `longPressClear` at top of method body**

In `tapRec`, after the `switch` opening brace, add (or replace the `switch` to wrap with trace):

```cpp
void LoopEngine::tapRec(MidiTransport& transport) {
  #if DEBUG_SERIAL
  Serial.printf("[LOOP] tapRec ch=%u state=%u\n", _channel, (unsigned)_state);
  #endif
  switch (_state) {
    // ... existing cases ...
  }
  #if DEBUG_SERIAL
  Serial.printf("[LOOP] tapRec -> state=%u\n", (unsigned)_state);
  #endif
}
```

Same pattern for `tapPlayStop`, `longPressClear`. Replace the existing trace stubs in those methods (or add if not present).

- [ ] **Step 2: Add `#include "../core/HardwareConfig.h"` to LoopEngine.cpp if not already (need DEBUG_SERIAL macro)**

```bash
grep -n "HardwareConfig\|DEBUG_SERIAL" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/LoopEngine.cpp | head -5
```
If `HardwareConfig.h` not included, add it.

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 16 — HW Gate G3 + commit Phase 2.D

- [ ] **Step 1: Upload + monitor**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload && \
  ~/.platformio/penv/bin/pio device monitor -b 115200
```

**HW Gate G3 procedure** :
1. Switch to a LOOP bank in normal play (LEFT + bank pad of LOOP bank → bank switch).
2. Tap pad 32 (REC) once. Expected trace : `[LOOP] tapRec ch=N state=0` → `[LOOP] tapRec -> state=1` (EMPTY → RECORDING).
3. Tap pad 32 again. Expected : `state=1` → `state=2` (RECORDING → PLAYING, although stopRecording is still a stub so the transition will be incomplete — for G3 we only validate trace appears).
4. Tap pad 33 (PLAY/STOP). Expected : `[LOOP] tapPlayStop ch=N state=2` → state changes per dispatch.
5. Hold pad 34 (CLEAR) for ~600 ms. Expected : `[LOOP] longPressClear ch=N` (CLEAR threshold = 500 ms default).

**Critère G3** : Les 3 traces apparaissent au bon moment. Pas d'envoi MIDI noteOn parasite sur les 3 control pads (pads 32-34 ne doivent pas jouer le drum kit). Les autres pads (par exemple pad 0-31, 35-47) ne doivent rien faire en LOOP bank (Phase 2.E activera la capture).

- [ ] **Step 2: Commit gate Phase 2.D**

```bash
git add src/main.cpp src/loop/LoopEngine.cpp
git commit -m "$(cat <<'EOF'
feat(loop): processLoopMode dispatch + REC/PLAY/CLEAR detection (Phase 2.D)

- Add processLoopMode(state, slot, now) in main.cpp, dispatched from
  handlePadInput switch case BANK_LOOP
- REC / PLAY-STOP pads : rising-edge tap → LoopEngine.tapRec / tapPlayStop
- CLEAR pad : sustained-press tracking via notifyClearPressStart/End
  + isClearHoldFired check → longPressClear when SettingsStore::clearLoopTimerMs
  threshold passed (default 500ms)
- LOOP control pads filtered from musical pad iteration
  (isLoopControlPad lookup)
- Musical pads call capturePadEvent (stub — Phase 2.E implements recording)
- DEBUG_SERIAL traces in tapRec/tapPlayStop/longPressClear for HW debugging
- HW gate G3 OK : tap traces visible serial, control pads don't trigger drums

Decision Q1 §7 loop-buffer-invariants : layer musical LOOP indépendant —
dispatch par bank type, aucune interaction avec processArpMode auto-Play.

Spec §5, §7, §9, §15, §17 + invariants §23
EOF
)"
```

---

# Phase 2.E — Recording + bar-snap

Objectif : `startRecording` arme RECORDING (mais ne capture rien jusqu'au 1er pad press). `capturePadEvent` durant RECORDING écrit dans `_events[]` avec timestamp µs et latche `_recordBpm` + `_recordStartUs` au tout 1er event. `stopRecording` ferme : bar-snap (deadzone 25 %), rescale events, flush pads tenus, → PLAYING (via Phase 2.F).

### Task 16.5 — Wire `viewer::emitLoopBufferFull` helper in ViewerSerial (m9 audit prerequisite)

**Files:**
- Modify: `src/viewer/ViewerSerial.h`
- Modify: `src/viewer/ViewerSerial.cpp`

> **Rationale m9 audit fix** : `LoopEngine::capturePadEvent` (Task 17 ci-dessous) et `mergeOverdub` (Task 24) appellent `viewer::emitLoopBufferFull(channel, which)` quand un buffer plein force un drop. Le handler doit exister avant le 1er callsite pour que le build passe.

- [ ] **Step 1: Read ViewerSerial header API existante**

```bash
grep -n "emitArpQueueFull\|emit[A-Z]" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/viewer/ViewerSerial.h | head -20
```

Identifier le pattern `viewer::emitArpQueueFull()` (référence pour signature + PRIO_LOW droppable).

- [ ] **Step 2: Add declaration `emitLoopBufferFull` dans `ViewerSerial.h`**

Locate la section `namespace viewer { ... }` avec les autres `emit*` functions, et ajouter :

```cpp
namespace viewer {
  // ... (existing functions) ...

  // Phase 2 LOOP : telemetry buffer full (équivalent emitArpQueueFull pour LoopEngine).
  // which : "main" (recording buffer plein) | "overdub" (overdub buffer plein) | "merge" (capacité dépassée à merge).
  // PRIO_LOW droppable — non-critique, juste diag.
  void emitLoopBufferFull(uint8_t channel, const char* which);
}
```

- [ ] **Step 3: Implement `emitLoopBufferFull` dans `ViewerSerial.cpp`**

Locate l'implémentation de `emitArpQueueFull` (ViewerSerial.cpp:762) et copier le pattern *exact* :

```cpp
void emitLoopBufferFull(uint8_t channel, const char* which) {
  #if DEBUG_SERIAL
  emit(PRIO_LOW, "[LOOP_BUFFER_FULL] ch=%u which=%s\n",
       channel + 1, which ? which : "?");
  #endif
}
```

**Audit-fix R1** : pas de `s_viewerConnected.load(...)` manuel (déjà fait à l'intérieur de `emit()` ViewerSerial.cpp:73). Pas de `enqueueLine(...)` (fonction inexistante dans le namespace `viewer` — seul `emit(Priority, fmt, ...)` est public). Garder le bloc derrière `#if DEBUG_SERIAL` comme tous les autres `emit*` (cf `emitArpQueueFull` L762-766).

- [ ] **Step 4: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS — la fonction est déclarée + définie, prêt à être appelée par LoopEngine Task 17.

### Task 17 — Implement `startRecording` + capture armed state

**Files:**
- Modify: `src/loop/LoopEngine.cpp`
- Required prerequisite : Task 16.5 (viewer::emitLoopBufferFull wiré).

- [ ] **Step 1: Read clockManager getSmoothedBPM availability**

```bash
grep -n "getSmoothedBPM\|getCurrentTick" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/midi/ClockManager.h
```

- [ ] **Step 2: Replace `startRecording` stub**

```cpp
// =================================================================
// startRecording — EMPTY → RECORDING. Arms capture. recordStartUs set on 1st pad press.
// =================================================================
void LoopEngine::startRecording(MidiTransport& transport) {
  (void)transport;  // no MIDI flush needed here (EMPTY = nothing playing)
  _eventCount = 0;
  _overdubCount = 0;
  _recordStartUs = 0;
  _recordEndUs = 0;
  _recordBpm = _clock ? _clock->getSmoothedBPM() : 120;
  if (_recordBpm == 0) _recordBpm = 120;
  _recordFirstPressDone = false;
  memset(_padHeldLive, 0, sizeof(_padHeldLive));
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) _events[i].active = false;
  _state = LoopState::RECORDING;
}
```

- [ ] **Step 3: Implement `insertEventSorted` helper (M2 live-sort)**

```cpp
// =================================================================
// insertEventSorted — M2 Q4 décision (live-sort) :
// Insère un event à sa position triée (par timestampUs croissant) dans buffer[].
// Retourne true si inséré, false si buffer plein (drop silent per spec §8).
// Complexité : O(n) par insertion (binary search + shift).
// =================================================================
bool LoopEngine::insertEventSorted(LoopEvent* buffer, uint16_t& count, uint16_t cap,
                                     uint32_t timestampUs, uint8_t padIndex,
                                     uint8_t midiNote, uint8_t velocity) {
  if (count >= cap) return false;  // buffer full
  // Binary search position d'insertion (upper_bound : 1er index avec timestamp > new).
  // Permet de garder l'ordre stable pour events au même timestamp (push back ↦ ordre d'arrivée).
  int32_t lo = 0;
  int32_t hi = (int32_t)count;
  while (lo < hi) {
    int32_t mid = (lo + hi) / 2;
    if (buffer[mid].timestampUs <= timestampUs) lo = mid + 1;
    else hi = mid;
  }
  // lo = position d'insertion (peut être == count si append en fin).
  // Shift right.
  for (int32_t j = (int32_t)count; j > lo; j--) {
    buffer[j] = buffer[j - 1];
  }
  buffer[lo].timestampUs = timestampUs;
  buffer[lo].padIndex    = padIndex;
  buffer[lo].midiNote    = midiNote;
  buffer[lo].velocity    = velocity;
  buffer[lo].active      = true;
  count++;
  return true;
}
```

- [ ] **Step 4: Replace `capturePadEvent` stub** (M2 live-sort + M3 strict velocity + M8 live drumming tous états)

```cpp
// =================================================================
// capturePadEvent — décisions actées post-audit :
//   M3 (Q8) : velocity stocké = baseVelocity STRICT (passé par processLoopMode).
//             Variation appliquée uniquement au playback dans update().
//   M2 (Q4) : insertion live-sorted dans _events[] (RECORDING) ou _overdubEvents[]
//             (OVERDUBBING). Buffer toujours trié, mergeOverdub devient O(n+m).
//   M8 (Q1) : MIDI live monitor émis dans TOUS les états (spec §18 "percussion fixe").
//             EMPTY/STOPPED/WAITING_* → live monitor seul, pas de capture buffer.
//             RECORDING/OVERDUBBING → live monitor + capture buffer.
// velocity == 0 → noteOff event ; > 0 → noteOn event.
// =================================================================
void LoopEngine::capturePadEvent(uint8_t padIndex, uint8_t velocity, MidiTransport& transport) {
  if (padIndex >= NUM_KEYS) return;

  bool isNoteOn = (velocity > 0);
  uint8_t midiNote = resolvePadToMidiNote(padIndex);

  // M8 Q1 : live monitor MIDI émis dans TOUS les états (spec §18 "percussion fixe").
  // Velocity strict baseVelocity (sans variation, M3 Q8). Pas d'aftertouch (spec §24).
  if (isNoteOn) {
    refCountNoteOn(transport, midiNote, velocity);
  } else {
    refCountNoteOff(transport, midiNote);
  }

  // Audit-fix B-N1 / B-N2 / R-N1 : tracker live press (TOUS états).
  // Set true à chaque rising edge, false à chaque falling edge. Consommé par :
  //   - stopRecording flushHeldPadsAsNoteOffs (fin de loop).
  //   - mergeOverdub flush held (inject noteOff à _playPositionUs).
  //   - onBackgroundTransition (refCountNoteOff direct au bank switch out).
  // Invariant : _padHeldLive[pad] == true ssi pad physiquement enfoncé ET live
  // monitor a émis noteOn (refCountNoteOn ci-dessus). Set/reset symétrique.
  _padHeldLive[padIndex] = isNoteOn ? 1 : 0;

  // Capture buffer écriture conditionnelle (RECORDING ou OVERDUBBING seuls).
  if (_state == LoopState::RECORDING) {
    uint32_t nowUs = micros();

    // m6 Q7 : noteOff avant 1st press musical ignoré (pad tenu avant tapRec).
    if (!_recordFirstPressDone && !isNoteOn) {
      return;
    }
    // First-press latches recordStart + recordBpm (invariant §23.5)
    if (!_recordFirstPressDone && isNoteOn) {
      _recordStartUs = nowUs;
      _recordBpm = _clock ? _clock->getSmoothedBPM() : 120;
      if (_recordBpm == 0) _recordBpm = 120;
      _recordFirstPressDone = true;
    }

    uint32_t timestampUs = nowUs - _recordStartUs;
    // M2 live-sort : insertion triée dans _events[].
    bool ok = insertEventSorted(_events, _eventCount, MAX_LOOP_EVENTS,
                                 timestampUs, padIndex, midiNote, velocity);
    if (!ok) {
      // m9 telemetry : buffer plein, drop silent per spec §8 + emit viewer.
      viewer::emitLoopBufferFull(_channel, "main");
    }

    // Audit-fix : _padHeldLive[padIndex] déjà set en haut de capturePadEvent (tracker unifié).
    return;
  }

  if (_state == LoopState::OVERDUBBING) {
    // B1 fix audit : utilise _playPositionUs maintenu par update() (precision ~1 ms),
    // pas d'appel à computeLoopPositionUs (méthode supprimée).
    uint32_t posInLoop = _playPositionUs;

    // M2 live-sort : insertion triée dans _overdubEvents[].
    bool ok = insertEventSorted(_overdubEvents, _overdubCount, MAX_LOOP_OVERDUB_EVENTS,
                                 posInLoop, padIndex, midiNote, velocity);
    if (!ok) {
      // m9 telemetry overdub buffer full.
      viewer::emitLoopBufferFull(_channel, "overdub");
    }
    return;
  }

  // EMPTY / STOPPED / PLAYING / WAITING_* : live monitor seul (déjà émis ci-dessus).
  // Pas de buffer write — la musique live n'altère pas le loop sans tap REC explicite.
  // Conformité spec §5 "Le buffer LOOP est sacré".
}
```

> **Note dépendance `viewer::emitLoopBufferFull`** : nouveau callsite m9. Le handler doit être ajouté dans `src/viewer/ViewerSerial.cpp` (équivalent `emitArpQueueFull`). Spec : émettre `[LOOP_BUFFER_FULL] ch=N which=main|overdub` PRIO_LOW droppable. À wirer dans la même phase (cf Task ViewerSerial extension dans Phase 2.J ou earlier).

- [ ] **Step 4: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 18 — Implement `stopRecording` (bar-snap + flush held pads + → PLAYING)

**Files:**
- Modify: `src/loop/LoopEngine.cpp`

- [ ] **Step 1: Replace `stopRecording` + `flushHeldPadsAsNoteOffs` stubs**

```cpp
// =================================================================
// flushHeldPadsAsNoteOffs — inject noteOff into main buffer for pads still held at stopRecording
// =================================================================
// M2 fix : utilise insertEventSorted pour maintenir buffer trié (live-sort).
void LoopEngine::flushHeldPadsAsNoteOffs(uint32_t timestampUs) {
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    if (!_padHeldLive[pad]) continue;
    // insertion triée (M2). insertEventSorted retourne false si buffer plein → silent drop.
    insertEventSorted(_events, _eventCount, MAX_LOOP_EVENTS,
                       timestampUs, pad, resolvePadToMidiNote(pad), /*velocity=*/0);
    _padHeldLive[pad] = false;
  }
}

// =================================================================
// stopRecording — close RECORDING with bar-snap (spec §7)
// 1. Compute raw recorded duration (now - recordStartUs)
// 2. Compute barDuration_us at recordBpm (4 beats × 60s/bpm × 1e6 = 240e6/bpm)
// 3. Snap with deadzone 25% : if elapsed within 0.25×barDuration after a bar line, snap down
//    else snap up. Min 1 bar, max 64 bars.
// 4. Rescale event timestamps proportionally to fill snapped duration
// 5. Flush pads held → noteOff events at snapped duration
// 6. State → PLAYING (via startPlayback)
// =================================================================
void LoopEngine::stopRecording(MidiTransport& transport) {
  uint32_t nowUs = micros();
  _recordEndUs = nowUs;

  if (!_recordFirstPressDone || _eventCount == 0) {
    // No content — back to EMPTY
    _state = LoopState::EMPTY;
    return;
  }

  uint32_t rawDurUs = nowUs - _recordStartUs;
  uint32_t barDurUs = (uint32_t)(240000000UL / _recordBpm);  // 240e6 / bpm = bar duration µs (4 beats)
  if (barDurUs == 0) barDurUs = 2000000;  // safety : 120 BPM = 2s bar

  // Bar-snap with 25% deadzone
  uint32_t barsFloor = rawDurUs / barDurUs;
  uint32_t remainder = rawDurUs - (barsFloor * barDurUs);
  uint32_t deadzone  = barDurUs / 4;  // 25%

  uint32_t snappedBars;
  if (remainder <= deadzone) {
    snappedBars = barsFloor;  // snap down (deadzone absorbs overshoot)
  } else {
    snappedBars = barsFloor + 1;  // round up
  }
  if (snappedBars < 1)  snappedBars = 1;
  if (snappedBars > 64) snappedBars = 64;
  _loopBars = (uint16_t)snappedBars;
  uint32_t snappedDurUs = snappedBars * barDurUs;

  // Rescale event timestamps proportionally
  if (rawDurUs > 0 && snappedDurUs != rawDurUs) {
    uint64_t scaleNum = snappedDurUs;
    uint64_t scaleDen = rawDurUs;
    for (uint16_t i = 0; i < _eventCount; i++) {
      _events[i].timestampUs = (uint32_t)((uint64_t)_events[i].timestampUs * scaleNum / scaleDen);
    }
  }
  _loopDurationUs = snappedDurUs;

  // Flush held pads as noteOff events at snapped duration.
  // flushHeldPadsAsNoteOffs utilise insertEventSorted (M2 live-sort) → buffer reste trié.
  flushHeldPadsAsNoteOffs(snappedDurUs);

  // M5 fix : pas de re-sort ici (live-sort dans capturePadEvent maintient l'ordre,
  // et flushHeldPadsAsNoteOffs insert également via insertEventSorted). Le buffer
  // est invariant sorted dès la fin du recording.

  // M6 fix : validation timestamp < loopDuration (defense in depth contre rescale buggy
  // ou edge case capture juste à rawDurUs). Clamp _loopDurationUs - 1 si dépassement.
  uint16_t clampedCount = 0;
  for (uint16_t i = 0; i < _eventCount; i++) {
    if (_events[i].timestampUs >= _loopDurationUs) {
      _events[i].timestampUs = _loopDurationUs - 1;
      clampedCount++;
    }
  }
  #if DEBUG_SERIAL
  if (clampedCount > 0) {
    Serial.printf("[LOOP WARN] stopRecording clamped %u events to loopDur=%lu us\n",
                  clampedCount, (unsigned long)_loopDurationUs);
  }
  #endif

  // Flush refcount + transition to PLAYING (B3 fix : passer nowUs capturé).
  flushPendingNoteOffs(transport);
  startPlayback(transport, micros());
}
```

> **Note M5** : `stopRecording` n'a plus de tri final — la live-sort dans `capturePadEvent` (M2 Q4) maintient `_events[]` trié pendant tout le recording. `flushHeldPadsAsNoteOffs` injecte les noteOff via le même `insertEventSorted` (cf step suivant). Le `for insertion sort` original a été supprimé.

- [ ] **Step 2: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS — `startPlayback` is still a stub. Compilation OK.

### Task 19 — HW Gate G4 + commit Phase 2.E

- [ ] **Step 1: Add temporary debug trace in `stopRecording` (m10 fix : task step explicite)**

Insérer **avant** le `flushPendingNoteOffs(transport)` final dans `stopRecording` :

```cpp
#if DEBUG_SERIAL
  Serial.printf("[LOOP REC CLOSED] rawDurMs=%lu snappedBars=%u snappedDurMs=%lu eventCount=%u recBpm=%u\n",
                (unsigned long)(rawDurUs / 1000UL), (unsigned)snappedBars,
                (unsigned long)(snappedDurUs / 1000UL), _eventCount, _recordBpm);
#endif
```

À **retirer** après G4 validé (cf Step 4 ci-dessous).

- [ ] **Step 2: Upload + monitor**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload && \
  ~/.platformio/penv/bin/pio device monitor -b 115200
```

- [ ] **Step 3: Exécuter procédure HW Gate G4**

1. Switch to LOOP bank.
2. Tap REC pad (32 par défaut Phase 2 dev seed). Expected : `[LOOP] tapRec state=0 -> state=1` (RECORDING).
3. Tap drum pads (kick = pad 0 → MIDI 36, snare = pad 1 → MIDI 37, etc.). **Each tap should play live MIDI immediately** (live monitor M8 fix Q1, baseVelocity strict M3 fix Q8).
4. Wait quelques secondes (motif joué dans le tempo perçu).
5. Tap REC again. Expected : `[LOOP] tapRec state=1 -> state=2` (PLAYING) + `[LOOP REC CLOSED] rawDurMs=... snappedBars=... eventCount=... recBpm=...`.

**Critère G4** :
- Live monitor noteOn/Off **audible HW dans tous les états** (M8 fix : pas que en RECORDING).
- `eventCount > 0` cohérent avec le nombre de press+release effectués.
- `snappedBars >= 1` (clamp min).
- `snappedDurMs` proche de `snappedBars × 60000 / recBpm × 4` (= snappedBars × bar duration à recBpm).
- Aucune noteOn bloquée — vérifier au DAW BLE/USB (toutes les notes drums déclenchées par le live monitor doivent avoir leur noteOff après release).

- [ ] **Step 4: Retirer la trace `[LOOP REC CLOSED]` (m10 fix : trace explicitement éphémère, pas committée)**

Manuellement Edit le fichier pour supprimer le bloc `#if DEBUG_SERIAL ... Serial.printf("[LOOP REC CLOSED]...) ... #endif` ajouté en Step 1. Le commit Phase 2.E ne doit PAS inclure cette trace temporaire.

**HARD-ASSERT bloquant avant Step 5** (Audit-renforcement vigilance Claude) :

```bash
TRACE_COUNT=$(grep -c "LOOP REC CLOSED" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/LoopEngine.cpp)
if [ "$TRACE_COUNT" -ne 0 ]; then
  echo "FAIL: trace temporaire encore présente ($TRACE_COUNT occurrences). STOP — ne pas commit."
  exit 1
fi
echo "PASS: trace temporaire retirée, OK pour commit."
```

Si exit 1 : **STOP immédiat**. Signaler à Loïc que la trace n'a pas été retirée. Ne pas continuer vers Step 5 (commit). Re-éditer LoopEngine.cpp pour retirer le bloc, ré-exécuter l'assert.

- [ ] **Step 2: Commit gate Phase 2.E**

```bash
git add src/loop/LoopEngine.cpp
git commit -m "$(cat <<'EOF'
feat(loop): recording + bar-snap deadzone 25% (Phase 2.E)

- startRecording : EMPTY -> RECORDING, arms capture (no buffer write yet)
- capturePadEvent : append events to main buffer (REC) with µs timestamps,
  latches recordBpm + recordStartUs on first noteOn press (invariant §23.5).
  Live monitor : noteOn/noteOff also fired immediately so musician hears
  their press while recording. Tracks _padHeldLive for stopRecording flush.
- stopRecording : compute raw duration, bar-snap with 25% deadzone (snap
  down if remainder ≤ 0.25×barDur, else round up), clamp 1..64 bars,
  rescale event timestamps proportionally to fill snapped duration,
  flush held pads as noteOff events at snapped end, sort buffer by
  timestamp (insertion sort), flush MIDI refcount, → startPlayback (stub)
- HW gate G4 OK : record motif, live monitor audible, eventCount > 0,
  bar-snap matches BPM

Spec §7 + invariants §23.1 (no orphan notes) + §23.5 (immutable recordBpm)
EOF
)"
```

---

# Phase 2.F — Playback (BPM-scaled, refcount, scheduler) — **CRITICAL : premier son MIDI LOOP audible**

Objectif : `startPlayback` arme PLAYING. `update()` parcourt `_events[]`, dispatche `refCountNoteOn` quand le timestamp scalé matche la position de loop courante. `update()` détecte wrap (position → 0) et fire `consumeWrapFlash`. Wire l'appel `update()` depuis main loop pour chaque LOOP bank.

### Task 20 — Implement `startPlayback(transport, nowUs)` + `stopPlayback` (B1+B3 fixes audit)

**Files:**
- Modify: `src/loop/LoopEngine.cpp`

> **B1 fix audit (intégration incrémentale)** : la formule originale `realElapsed × liveBpm / recordBpm` réinterprète tout l'historique au nouveau ratio quand BPM change live → position saute. Remplacée par **intégration incrémentale par delta** : à chaque `update()`, accumuler `delta × liveBpm / recordBpm` dans `_scaledElapsedUs` (uint64). Pas de position calculée à partir de `_playStartUs`.
>
> **B3 fix audit** : `startPlayback(transport, nowUs)` accepte `nowUs` du caller pour synchroniser `_lastUpdateUs` quand commitWaitingAction enchaîne sur startPlayback dans le même update tick.

- [ ] **Step 1: Replace `startPlayback`, `stopPlayback` stubs (B1+B3 fixes)**

```cpp
// =================================================================
// startPlayback — STOPPED/EMPTY → PLAYING (B1+B3 fixes audit)
// nowUs param : timestamp capturé par le caller (update() phase 1 commitWaitingAction,
// OR tapPlayStop/tapRec où micros() est safe). Évite l'underflow uint32 sur enchaînement
// commitWaitingAction → startPlayback (B3 audit).
// =================================================================
void LoopEngine::startPlayback(MidiTransport& transport, uint32_t nowUs) {
  (void)transport;
  _playStartUs = nowUs;                  // debug / timestamp PLAYING entry
  _scaledElapsedUs = 0;                  // B1 : reset intégration cumulative
  _lastUpdateUs = nowUs;                 // B1 : ancrage premier delta = 0 au prochain update
  _playPositionUs = 0;
  _playNextEventIdx = 0;
  _lastBarIndex = 0;
  _state = LoopState::PLAYING;
}

// =================================================================
// stopPlayback — PLAYING/OVERDUBBING → STOPPED. flushNotes=true emits noteOff for refcount > 0
// =================================================================
void LoopEngine::stopPlayback(MidiTransport& transport, bool flushNotes) {
  if (flushNotes) {
    flushPendingNoteOffs(transport);
  }
  _state = LoopState::STOPPED;
}
```

> **Note `computeLoopPositionUs` supprimée** : la position est désormais maintenue par accumulation dans `update()` (cf Task 21 refactor), plus besoin de la calculer "à la demande" depuis `_playStartUs`. La méthode existait dans le squelette Task 1 mais devient morte — supprimer le prototype dans `LoopEngine.h` Helpers privés. Si certains callers (par ex. `capturePadEvent` OVERDUBBING) ont besoin de la position actuelle, ils lisent `_playPositionUs` directement (mis à jour par `update()` du frame précédent — précision ~1 ms acceptable pour overdub capture).

- [ ] **Step 2: Vérifier que le prototype `computeLoopPositionUs` est ABSENT de `LoopEngine.h`** (cas attendu post-Task 1 step 3 — la méthode a été supprimée du squelette dès Task 1, ce step est donc un no-op de sanity).

**HARD-ASSERT bloquant** (Audit-renforcement vigilance Claude — l'instruction originale "supprimer le prototype" était trompeuse car le prototype n'est jamais déclaré dans Task 1 step 3) :

```bash
PROTO_COUNT=$(grep -c "computeLoopPositionUs" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/LoopEngine.h)
if [ "$PROTO_COUNT" -ne 0 ]; then
  echo "FAIL: prototype 'computeLoopPositionUs' inattendu présent dans LoopEngine.h ($PROTO_COUNT occurrences)."
  echo "Task 1 step 3 n'a pas été suivi correctement — header diverge du plan. STOP."
  exit 1
fi
echo "PASS: prototype absent comme attendu (no-op de cette step)."
```

Si exit 1 : **STOP immédiat**. Header diverge du plan Task 1 step 3 — signaler à Loïc, ne pas tenter de "fixer" en aveugle.

Si PASS : passer à Step 3 (le membre privé `_playPositionUs` reste, déjà déclaré Task 1 step 3).

> **Audit-fix I2 — step 3 retirée** : Task 17 step 4 livre déjà la forme finale de `capturePadEvent` OVERDUBBING (avec `uint32_t posInLoop = _playPositionUs;` direct, sans appel à `computeLoopPositionUs`). Aucune mise à jour à faire ici, le step 3 antérieur était un no-op redondant qui risquait de semer la confusion à l'implémenteur.

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 21 — Implement `update()` — intégration incrémentale BPM (B1 fix audit) + event firing + wrap/bar flash

**Files:**
- Modify: `src/loop/LoopEngine.cpp`

> **B1 audit fix critical** : la position est intégrée par delta cumulatif (uint64 anti-overflow), pas re-calculée à partir de `_playStartUs`. À chaque tick d'update : `delta = nowUs - _lastUpdateUs ; _scaledElapsedUs += delta × liveBpm / recordBpm`. Wrap détecté quand `_scaledElapsedUs >= _loopDurationUs` (et soustrait pour rester borné).
>
> **B3 audit fix** : `nowUs` capturé en début d'update() est propagé à `commitWaitingAction` puis à `startPlayback` pour éviter underflow uint32 quand le tick de update commit un WAITING_PLAY.

- [ ] **Step 1: Replace `update` stub**

```cpp
// =================================================================
// update — called every main loop iteration (Phase 2 LOOP, B1+B3 fixes audit)
// =================================================================
// Phases :
//   1. WAITING_PLAY / WAITING_STOP boundary check (ClockManager ticks)
//      → commitWaitingAction(transport, nowUs) — B3 propage nowUs.
//   2. Drain pending noteOff queue (gates, stuck-note safety).
//   3. PLAYING / OVERDUBBING : intégration incrémentale BPM (B1) + walk events.
//   4. Wrap detection sur _scaledElapsedUs ≥ _loopDurationUs.
//   5. Bar crossing detection (sur _playPositionUs).
// =================================================================
void LoopEngine::update(MidiTransport& transport) {
  uint32_t nowUs = micros();

  // (1) WAITING_* → commit when boundary tick reached (B3 : propager nowUs)
  if (_state == LoopState::WAITING_PLAY || _state == LoopState::WAITING_STOP) {
    if (_clock && _clock->getCurrentTick() >= _waitingTargetTick) {
      commitWaitingAction(transport, nowUs);
      // Si on vient de transitioner vers PLAYING, _lastUpdateUs == nowUs (startPlayback l'a set),
      // donc le delta de la phase (3) ci-dessous sera 0. Pas de saut.
    }
  }

  // (2) Drain pending noteOffs (gates, etc.)
  drainPendingNoteOffs(transport, nowUs);

  // (3) Playback walk (B1 intégration incrémentale + audit-fix B1 WAITING_STOP)
  // WAITING_STOP inclus dans la garde : spec §17 "la LOOP continue de jouer
  // jusqu'au boundary tick commit". Sans WAITING_STOP ici, le tap PLAY/STOP en
  // quantize Beat/Bar silencerait la LOOP instantanément (silence immédiat puis
  // commit silencieux au boundary = quantize inaudible).
  if (_state == LoopState::PLAYING || _state == LoopState::OVERDUBBING
      || _state == LoopState::WAITING_STOP) {
    if (_eventCount == 0 || _loopDurationUs == 0) {
      _lastUpdateUs = nowUs;  // garder ancre cohérente même si rien à jouer
      return;
    }

    // B1 : accumulate scaled delta cumulatif. uint64 anti-overflow longues sessions.
    uint16_t liveBpm = _clock ? _clock->getSmoothedBPM() : _recordBpm;
    if (liveBpm == 0) liveBpm = _recordBpm;
    uint32_t deltaUs = nowUs - _lastUpdateUs;     // unsigned wraparound-safe sur micros() overflow
    _lastUpdateUs = nowUs;
    _scaledElapsedUs += (uint64_t)deltaUs * liveBpm / _recordBpm;

    // (4) Wrap detection : while-loop pour absorber catch-up sur freeze potentiel (insertion sort).
    while (_scaledElapsedUs >= (uint64_t)_loopDurationUs) {
      // Fire tail events (du playNextEventIdx jusqu'à eventCount) avant de wrap.
      while (_playNextEventIdx < _eventCount && _events[_playNextEventIdx].active) {
        const LoopEvent& e = _events[_playNextEventIdx];
        if (e.velocity > 0) {
          refCountNoteOn(transport, e.midiNote, applyVelocityVariation(e.velocity));
        } else {
          refCountNoteOff(transport, e.midiNote);
        }
        _playNextEventIdx++;
      }
      // Wrap : soustrait loopDuration de l'accumulateur (reste borné).
      _scaledElapsedUs -= _loopDurationUs;
      _playNextEventIdx = 0;
      _lastBarIndex = 0;
      _wrapFlash = true;
    }
    _playPositionUs = (uint32_t)_scaledElapsedUs;

    // Fire events whose timestamp <= current position
    while (_playNextEventIdx < _eventCount
           && _events[_playNextEventIdx].active
           && _events[_playNextEventIdx].timestampUs <= _playPositionUs) {
      const LoopEvent& e = _events[_playNextEventIdx];
      if (e.velocity > 0) {
        // M3 fix Q8 : applyVelocityVariation appliquée seulement au playback (ici).
        refCountNoteOn(transport, e.midiNote, applyVelocityVariation(e.velocity));
      } else {
        refCountNoteOff(transport, e.midiNote);
      }
      _playNextEventIdx++;
    }

    // (5) Bar crossing detection (sur position courante)
    uint32_t barDurUs = (_loopBars > 0) ? (_loopDurationUs / _loopBars) : _loopDurationUs;
    uint16_t currentBarIdx = (barDurUs > 0) ? (uint16_t)(_playPositionUs / barDurUs) : 0;
    if (currentBarIdx != _lastBarIndex) {
      _barFlash = true;
      _lastBarIndex = currentBarIdx;
    }
    return;
  }

  // États non-playback : garder _lastUpdateUs synchronisé pour éviter delta géant au prochain PLAYING.
  _lastUpdateUs = nowUs;
}
```

- [ ] **Step 2: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS — `commitWaitingAction(transport, nowUs)` signature étendue (B3), stub mis à jour dans Task 2.

### Task 22 — Wire `update()` call from main loop (m3 audit : location explicite)

**Files:**
- Modify: `src/main.cpp` `loop()` body (entre L1348 et L1351 selon état courant)

> **m3 audit fix** : la location d'insertion **précise** est entre `s_arpScheduler.processEvents();` (L1348) et `s_midiEngine.flush();` (L1351). Cela place LoopEngine.update() :
> - **APRÈS** `handlePadInput` (L1339, processLoopMode dispatch) — les captures du frame courant sont déjà émises en live monitor.
> - **APRÈS** `s_arpScheduler.processEvents()` (L1348) — les events ARP du frame sont firés (pas de cross-talk avec LOOP).
> - **AVANT** `s_midiEngine.flush()` (L1351) — les noteOn LOOP émis par update sont dans la même fenêtre flush que les autres MIDI (latency cohérente).
> - **AVANT** `// --- CRITICAL PATH END ---` marker (L1350) — LOOP fait partie du critical path audio.

- [ ] **Step 1: Locate main `loop()` function + le marker exact**

```bash
grep -n "s_arpScheduler.tick\|s_arpScheduler.processEvents\|CRITICAL PATH END\|s_midiEngine.flush" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
```
Expected : `s_arpScheduler.processEvents();` ligne 1348, `// --- CRITICAL PATH END ---` ligne 1350, `s_midiEngine.flush();` ligne 1351.

- [ ] **Step 2: Insert LoopEngine.update() loop entre L1348 et L1350**

Original (L1347-1351) :
```cpp
  // --- ArpScheduler: dispatch clock ticks to all active arps ---
  s_arpScheduler.tick();
  s_arpScheduler.processEvents();  // Fire pending gate noteOff + shuffled noteOn

  // --- CRITICAL PATH END ---
  s_midiEngine.flush();
```

Modified (insertion juste après processEvents, dans le critical path) :
```cpp
  // --- ArpScheduler: dispatch clock ticks to all active arps ---
  s_arpScheduler.tick();
  s_arpScheduler.processEvents();  // Fire pending gate noteOff + shuffled noteOn

  // Phase 2 LOOP : drive every LoopEngine each frame (µs-driven, no scheduler).
  // Placement m3 audit : APRÈS arpScheduler (pas de cross-talk), AVANT
  // midiEngine.flush (latency cohérente flush window).
  for (uint8_t b = 0; b < NUM_BANKS; b++) {
    if (s_banks[b].loopEngine) {
      s_banks[b].loopEngine->update(s_transport);
    }
  }

  // --- CRITICAL PATH END ---
  s_midiEngine.flush();
```

- [ ] **Step 4: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 23 — HW Gate G5 ★ **PREMIER SON MIDI LOOP AUDIBLE** + commit Phase 2.F

- [ ] **Step 1: Upload + monitor + DAW connected (BLE or USB MIDI)**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload && \
  ~/.platformio/penv/bin/pio device monitor -b 115200
```

Pré-requis HW : DAW (Ableton / Logic / etc.) ouvert, MIDI input ILLPAD activé, instrument percussif assigné au channel de la LOOP bank.

**HW Gate G5 procedure** ★ MILESTONE :
1. Switch to LOOP bank.
2. Tap REC.
3. Tap drum pads (kick, snare, hh) au tempo ressenti. Vérifier live monitor audible.
4. Tap REC à nouveau. La bank entre en PLAYING.
5. **Vérifier que la boucle JOUE en MIDI** : les events s'enchaînent à l'identique (timing préservé), wrap silencieux à chaque cycle, pas de notes bloquées.
6. Vérifier que les notes drum sortent au bon channel MIDI (= bank index).
7. Changer le tempo (LEFT + rear pot ou DAW si slave) → la boucle doit accélérer/ralentir sans glitch.

**Critère G5** : 1er son MIDI LOOP audible. Wrap propre. BPM-scaling fonctionnel. Pas de notes bloquées après plusieurs wraps. **Phase 2 milestone atteint.**

Si G5 échoue :
- Notes silencieuses → vérifier `_channel = bankIdx` (debug Serial), DAW MIDI input config.
- Notes bloquées → refcount bug ou wrap fait sauter des noteOff. Vérifier que dans capture, chaque noteOn a son noteOff (count events velocity > 0 vs velocity == 0).
- Boucle ne wrap pas → `_loopDurationUs` à 0 ou bug bar-snap. Debug stopRecording values.
- BPM scaling cassé → l'intégration incrémentale (B1) accumule via `_scaledElapsedUs`. Loguer `deltaUs`, `liveBpm`, `recordBpm`, `_scaledElapsedUs`. Wrap manqué = `_scaledElapsedUs` ne décroît jamais sous `_loopDurationUs`. Saut = `delta × liveBpm / recordBpm` overflow uint64 (impossible math).

- [ ] **Step 2: Commit gate Phase 2.F ★**

```bash
git add src/loop/LoopEngine.cpp src/main.cpp
git commit -m "$(cat <<'EOF'
feat(loop): playback BPM-scaled + first audible MIDI loop (Phase 2.F, gate G5)

- startPlayback(transport, nowUs) : B3 audit fix signature étendue ; STOPPED/EMPTY ->
  PLAYING, reset _scaledElapsedUs + _lastUpdateUs = nowUs + playNextEventIdx + lastBarIndex
- stopPlayback : PLAYING/OD -> STOPPED, optional MIDI flush
- update() implements µs-driven engine cycle (B1 audit fix intégration incrémentale) :
   1. WAITING_* boundary check (passe nowUs à commitWaitingAction, B3)
   2. Drain pending noteOffs (gates)
   3. B1 fix : _scaledElapsedUs += delta × liveBpm / recordBpm cumulatif (pas
      re-calc depuis _playStartUs — élimine saut sur tempo change live)
   4. Wrap detection : while _scaledElapsedUs >= loopDuration, fire tail + soustraire
      loopDuration + _wrapFlash = true (absorbe catch-up multi-wrap si insertion-sort freeze)
   5. Bar crossing detection : _barFlash = true on bar index change
   6. M3 fix Q8 : applyVelocityVariation seulement au playback (pas au capture)
- main loop iterates s_banks[].loopEngine->update() each frame (m3 audit : insertion
  entre s_arpScheduler.processEvents et s_midiEngine.flush)
- HW gate G5 OK : ★ FIRST AUDIBLE MIDI LOOP — record → playback,
  wrap clean, BPM-scaling responds to tempo changes (no jump on PLL ripple), no stuck notes

Invariants §23.1 (no orphan notes via refcount), §23.5 (immutable recordBpm)
Spec §7, §9, §16, §17 (PLAYING entry, no WAITING_PLAY yet — Phase 2.H)
EOF
)"
```

---

# Phase 2.G — Overdub + Q5 STOPPED-loaded REC = PLAYING+OVERDUB

Objectif : overdub buffer captures notes pendant que main buffer continue de jouer. tap REC en OVERDUBBING merge l'overdub. tap PLAY/STOP en OVERDUBBING abandonne. Q5 §28 : STOPPED + tap REC = PLAYING + OVERDUBBING simultanés.

### Task 24 — Implement `mergeOverdub` + `abandonOverdub`

**Files:**
- Modify: `src/loop/LoopEngine.cpp`

- [ ] **Step 1: Replace `mergeOverdub` + `abandonOverdub` stubs** (M2 audit live-sort + M4 audit pré-check atomique + m9 telemetry)

```cpp
// =================================================================
// mergeOverdub — commit overdub buffer into main buffer chronologiquement (M2+M4 audit fixes)
// =================================================================
// M2 (Q4 décision) : buffers déjà triés à l'insertion via insertEventSorted (live-sort
//   dans capturePadEvent). Merge = O(n+m) walk de 2 arrays pré-triés (pas d'insertion sort
//   O(n²) sur 1024 events).
// M4 (audit fix) : pré-check capacité. Si _eventCount + _overdubCount > MAX_LOOP_EVENTS,
//   abandon ATOMIQUE silent (per spec §8) — pas de drop partiel qui casserait
//   les paires noteOn/noteOff orphelines.
// m9 (audit fix) : telemetry viewer si abandon (buffer full).
// Retourne true si merge OK, false si abandon atomique.
bool LoopEngine::mergeOverdub() {
  // (1) M4 pré-check atomique
  if ((uint32_t)_eventCount + (uint32_t)_overdubCount > (uint32_t)MAX_LOOP_EVENTS) {
    // Buffer full. Abandon atomique : pas de partial merge, pas de paires cassées.
    abandonOverdub();
    viewer::emitLoopBufferFull(_channel, "merge");
    return false;
  }
  if (_overdubCount == 0) return true;   // rien à merger, no-op

  // (2) M2 merge O(n+m) de deux arrays pré-triés.
  // Insère chaque event overdub à sa position triée dans main buffer.
  // _overdubEvents[] et _events[] sont triés invariant grâce à insertEventSorted.
  for (uint8_t i = 0; i < _overdubCount; i++) {
    const LoopEvent& e = _overdubEvents[i];
    insertEventSorted(_events, _eventCount, MAX_LOOP_EVENTS,
                       e.timestampUs, e.padIndex, e.midiNote, e.velocity);
    // insertEventSorted ne devrait jamais retourner false ici (pré-check Step 1 garantit la capacité).
  }

  // (2.5) Audit-fix B-N2 : flush held pads (spec §8 "les pads tenus sont flushés
  //       comme à la clôture d'un RECORDING initial").
  // Pour chaque pad encore physiquement enfoncé pendant le merge, inject noteOff
  // dans _events[] à _playPositionUs courant (= position au moment du tap REC).
  // Sans ça, le noteOn de l'overdub mergé n'a pas de noteOff matching → refcount
  // accumule cycle après cycle → stuck note au DAW (cf audit Bloquant B-N2).
  //
  // Position choisie : _playPositionUs (= "position courante" au moment du merge).
  // Sémantique : la note jouée dure du press jusqu'au merge, ce qui correspond
  // à la durée pendant laquelle le user a physiquement tenu le pad en OD.
  //
  // IMPORTANT : ne PAS reset _padHeldLive[pad] ici. Le pad est encore physiquement
  // enfoncé — le tracker doit refléter l'état réel. Reset au falling edge naturel
  // dans capturePadEvent (rising → set, falling → reset), ou via onBackgroundTransition
  // si bank switch out avant release.
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    if (!_padHeldLive[pad]) continue;
    bool flushOk = insertEventSorted(_events, _eventCount, MAX_LOOP_EVENTS,
                                      _playPositionUs, pad,
                                      resolvePadToMidiNote(pad), /*velocity=*/0);
    if (!flushOk) {
      // Buffer full au flush (pré-check (1) passait sans compter ces N noteOffs).
      // Best-effort + telemetry. Live press refcount sera silencé au release naturel.
      viewer::emitLoopBufferFull(_channel, "merge_flush");
    }
  }

  // (3) Reset overdub buffer
  _overdubCount = 0;
  for (uint8_t i = 0; i < MAX_LOOP_OVERDUB_EVENTS; i++) _overdubEvents[i].active = false;

  // (4) Update _playNextEventIdx : recompute against current _playPositionUs (B1 cumul).
  // Binary search le 1er event avec timestamp > _playPositionUs (events à fire au reste du cycle).
  _playNextEventIdx = 0;
  int32_t lo = 0;
  int32_t hi = (int32_t)_eventCount;
  while (lo < hi) {
    int32_t mid = (lo + hi) / 2;
    if (_events[mid].timestampUs <= _playPositionUs) lo = mid + 1;
    else hi = mid;
  }
  _playNextEventIdx = (uint16_t)lo;
  return true;
}

// =================================================================
// abandonOverdub — wipe overdub buffer, state stays PLAYING (caller transitions)
// Idempotent : safe à appeler même si _overdubCount == 0.
// =================================================================
void LoopEngine::abandonOverdub() {
  _overdubCount = 0;
  for (uint8_t i = 0; i < MAX_LOOP_OVERDUB_EVENTS; i++) _overdubEvents[i].active = false;
}
```

- [ ] **Step 2: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 25 — HW Gate G6 + commit Phase 2.G

- [ ] **Step 1: Upload + monitor + DAW**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload && \
  ~/.platformio/penv/bin/pio device monitor -b 115200
```

**HW Gate G6 procedure** :
1. Record un kick simple (pad 0, 4 fois alignées sur bars).
2. Tap REC pendant que la boucle joue → entre OVERDUBBING. LED jaune avec flash orange par wrap (Phase 2.I rendra ça visible).
3. Tap snare (pad 1) sur les contretemps.
4. Tap REC → merge. La boucle joue maintenant kick + snare.
5. Vérifier que le merge est chronologique (events à l'instant T joués au bon T au wrap suivant).
6. Test Q5 : tap PLAY/STOP → STOPPED. Tap REC → doit entrer PLAYING + OVERDUBBING. Tap hh (pad 2) → enregistre. Tap REC → merge.
7. Test abandon : entrer en OVERDUBBING, tap PLAY/STOP → overdub abandonné, boucle continue PLAYING. Tap PLAY/STOP à nouveau → STOPPED (transition normale).

**Critère G6** : Overdub merge audible et timing préservé. Q5 STOPPED-loaded REC fonctionne (entre PLAYING + OD en un geste). Abandon overdub propre (overdub buffer reset, main buffer intact).

- [ ] **Step 2: Commit gate Phase 2.G**

```bash
git add src/loop/LoopEngine.cpp
git commit -m "$(cat <<'EOF'
feat(loop): overdub merge + Q5 STOPPED-loaded REC = PLAYING+OVERDUB (Phase 2.G)

- mergeOverdub : sort overdub buffer chronologically, append to main buffer,
  re-sort whole main, reset overdub, update playNextEventIdx
- abandonOverdub : wipe overdub buffer only, main buffer untouched
- tapRec from PLAYING -> OVERDUBBING (overdub buffer armed)
- tapRec from STOPPED -> startPlayback + state = OVERDUBBING (Q5 §28 :
  reprise position 0 + arm overdub en un seul geste)
- tapRec from OVERDUBBING -> mergeOverdub + state = PLAYING
- tapPlayStop from OVERDUBBING -> abandonOverdub + state = PLAYING (spec §8)
- HW gate G6 OK : kick + snare overdub audible, Q5 STOPPED+REC entre en
  PLAYING+OD, abandon propre

Spec §8 + Q5 §28 décisions tranchées
EOF
)"
```

---

# Phase 2.H — Quantize WAITING_PLAY / WAITING_STOP

Objectif : `tapPlayStop` avec quantize Beat ou Bar entre dans `WAITING_PLAY` ou `WAITING_STOP`. `update()` détecte le boundary tick (via `ClockManager::getCurrentTick()`) et commit. Spec §17 table — gestes pendant WAITING_*.

### Task 26 — Implement `computeNextBoundaryTick` + `commitWaitingAction`

**Files:**
- Modify: `src/loop/LoopEngine.cpp`

- [ ] **Step 1: Replace `computeNextBoundaryTick` + `commitWaitingAction` stubs**

```cpp
// =================================================================
// computeNextBoundaryTick — return next tick that matches the quantize boundary
// FREE : 0 (caller checks for FREE before this call)
// BEAT : next multiple of 24 ticks
// BAR  : next multiple of 96 ticks
// =================================================================
uint32_t LoopEngine::computeNextBoundaryTick(LoopQuantize q) const {
  if (!_clock) return 0;
  uint32_t now = _clock->getCurrentTick();
  uint32_t mod = (q == LOOP_QUANT_BAR) ? TICKS_PER_BAR : TICKS_PER_BEAT;
  uint32_t boundary = ((now / mod) + 1) * mod;
  return boundary;
}

// =================================================================
// commitWaitingAction — called by update() when waitingTargetTick reached
// B3 audit fix : signature étendue (transport, nowUs) pour propager le timestamp
// de update() à startPlayback (évite underflow uint32 sur enchaînement même tick).
// =================================================================
void LoopEngine::commitWaitingAction(MidiTransport& transport, uint32_t nowUs) {
  if (_state == LoopState::WAITING_PLAY) {
    startPlayback(transport, nowUs);  // → PLAYING (B3 : nowUs propagé)
  } else if (_state == LoopState::WAITING_STOP) {
    stopPlayback(transport, /*flushNotes=*/true);  // → STOPPED + flush
  }
}
```

- [ ] **Step 2: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 27 — HW Gate G7 + commit Phase 2.H

- [ ] **Step 1: Upload + monitor + DAW with stable BPM (slave from DAW MIDI clock recommended)**

**HW Gate G7 procedure** :
1. Configure bank LOOP quantize via Tool 5 = Bar (default).
2. Record une boucle 1 bar.
3. Tap PLAY/STOP en milieu de bar. Expected : `WAITING_STOP`. La boucle continue de jouer jusqu'à la fin de la bar courante, puis stop net (flush notes).
4. Tap PLAY/STOP en milieu de bar suivante. Expected : `WAITING_PLAY`. La boucle repart au début du prochain bar.
5. Reconfigure quantize = Beat (Tool 5). Tap PLAY/STOP → délai max 1 beat avant action.
6. Reconfigure quantize = Free (Tool 5). Tap PLAY/STOP → action immédiate (pas de WAITING).
7. Test concurrency : entrer en WAITING_STOP, tap REC pendant l'attente → spec §17 doit annuler WAITING_STOP et entrer en OVERDUBBING.

**Critère G7** : Les actions PLAY/STOP s'alignent visiblement sur la grille Beat/Bar du DAW. Pas de stuck notes au stop. Concurrency tap REC pendant WAITING_STOP fonctionne (annule + OD).

- [ ] **Step 2: Commit gate Phase 2.H**

```bash
git add src/loop/LoopEngine.cpp
git commit -m "$(cat <<'EOF'
feat(loop): WAITING_PLAY / WAITING_STOP quantize Beat/Bar (Phase 2.H)

- computeNextBoundaryTick : next multiple of 24 (BEAT) or 96 (BAR) ticks
  from ClockManager.getCurrentTick()
- commitWaitingAction : called by update() when waitingTargetTick reached
   - WAITING_PLAY  -> startPlayback
   - WAITING_STOP  -> stopPlayback(flushNotes=true)
- tapPlayStop dispatches to WAITING_* when quantize != FREE (already wired
  in Task 4 state machine)
- update() Phase 2.F already checks WAITING_* in step (1) — now functional
- HW gate G7 OK : Bar/Beat-aligned start/stop via ClockManager,
  Free quantize immediate, concurrency tap REC during WAITING_STOP -> OD

Spec §17 quantize per-bank + concurrent gestures table
EOF
)"
```

---

# Phase 2.I — LedController renderBankLoop body + EVT_LOOP_* triggers

Objectif : compléter `renderBankLoop` body : SOLID FG/BG en EMPTY/STOPPED, FLASH overlay sur consumeBarFlash / consumeWrapFlash, couleur overlay selon état (REC = coral, OVERDUB = amber, PLAYING = green). Trigger `EVT_LOOP_REC` / `EVT_LOOP_OVERDUB` / `EVT_LOOP_CLEAR` / `EVT_WAITING` aux bons moments depuis `processLoopMode` ou directement depuis LoopEngine (via callback ? non — simpler : depuis processLoopMode après chaque action).

### Task 28 — Extend `renderBankLoop` body for state-driven rendering

**Files:**
- Modify: `src/core/LedController.cpp:508-515`

- [ ] **Step 1: Read current renderBankLoop body + renderBankArpeg template**

```bash
sed -n '447,520p' /Users/loic/Code/PROJECTS/ILLPAD_V2/src/core/LedController.cpp
```

- [ ] **Step 2: Replace `renderBankLoop` stub with state-aware body (m4 audit : consume flash flags ONCE, one-shot)**

Insert `#include "../loop/LoopEngine.h"` in LedController.cpp includes if not present.

```cpp
void LedController::renderBankLoop(uint8_t led, bool isFg, unsigned long now) {
  const BankSlot& slot = _slots[led];
  LoopEngine* le = slot.loopEngine;

  // Fallback : no engine assigned (boot defaults, ou bank > MAX_LOOP_BANKS) → solid mode color
  if (!le) {
    uint8_t intensity = isFg
                        ? _fgIntensity
                        : (uint8_t)((uint16_t)_fgIntensity * _bgFactor / 100);
    setPixel(led, _colors[CSLOT_MODE_LOOP], intensity);
    return;
  }

  uint8_t baseIntensity = isFg
                          ? _fgIntensity
                          : (uint8_t)((uint16_t)_fgIntensity * _bgFactor / 100);

  // m4 audit fix : consume flash flags UNE SEULE FOIS (one-shot semantics).
  // Phase 2 : on n'utilise qu'une seule durée tickBarDurationMs pour les deux
  // (bar + wrap). Phase 4 polish pourra ajouter _lastFlashDurationMs pour différencier.
  bool barFlash  = le->consumeBarFlash();
  bool wrapFlash = le->consumeWrapFlash();
  if (barFlash || wrapFlash) {
    _flashStartTime[led] = now;
  }

  // State-driven foreground color
  ColorSlotId fgColorSlot;
  bool recording   = le->isRecording();
  bool overdubbing = le->isOverdubbing();
  bool playing     = le->isPlaying();

  if (recording)       fgColorSlot = CSLOT_VERB_REC;
  else if (overdubbing) fgColorSlot = CSLOT_VERB_OVERDUB;
  else if (playing)     fgColorSlot = CSLOT_VERB_PLAY;
  else                  fgColorSlot = CSLOT_MODE_LOOP;  // EMPTY / STOPPED / WAITING_* : just mode color

  setPixel(led, _colors[fgColorSlot], baseIntensity);

  // FLASH overlay on bar/wrap (duration tickBarDurationMs Phase 2 — voir m4 fix)
  if (_flashStartTime[led] != 0) {
    uint16_t durationMs = _tickBarDurationMs;
    if ((now - _flashStartTime[led]) < durationMs) {
      renderFlashOverlay(led, _colors[CSLOT_VERB_PLAY], _tickFlashFg, _tickFlashBg,
                         _flashStartTime[led], durationMs, isFg, now);
    } else {
      _flashStartTime[led] = 0;
    }
  }
}
```

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 29 — Trigger `EVT_LOOP_*` events from processLoopMode

**Files:**
- Modify: `src/main.cpp` (processLoopMode added Task 13)

- [ ] **Step 1: Read processLoopMode current body**

```bash
grep -n "static void processLoopMode" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
```

- [ ] **Step 2: Add `s_leds.triggerEvent(...)` after each transport action**

Locate `le->tapRec(s_transport);` and replace by :

```cpp
        if (state.keyIsPressed[i] && !s_lastKeys[i]) {
          LoopState before = le->getState();
          le->tapRec(s_transport);
          LoopState after = le->getState();
          // Emit LED event based on the transition
          if (before == LoopState::EMPTY && after == LoopState::RECORDING) {
            s_leds.triggerEvent(EVT_LOOP_REC);
          } else if (after == LoopState::OVERDUBBING) {
            s_leds.triggerEvent(EVT_LOOP_OVERDUB);
          }
          // EMPTY -> RECORDING -> PLAYING transitions chained on REC tap close
          // emit EVT_PLAY (via WAITING handling will also fire it Phase 2.H)
          if (before == LoopState::RECORDING && after == LoopState::PLAYING) {
            s_leds.triggerEvent(EVT_PLAY);
          }
        }
```

Similarly for `tapPlayStop` :

```cpp
      } else if (i == le->getPlayStopPad()) {
        if (state.keyIsPressed[i] && !s_lastKeys[i]) {
          LoopState before = le->getState();
          le->tapPlayStop(s_transport, state.keyIsPressed);
          LoopState after = le->getState();
          if (after == LoopState::WAITING_PLAY || after == LoopState::WAITING_STOP) {
            s_leds.triggerEvent(EVT_WAITING);
          } else if (before == LoopState::STOPPED && after == LoopState::PLAYING) {
            s_leds.triggerEvent(EVT_PLAY);
          } else if (before == LoopState::PLAYING && after == LoopState::STOPPED) {
            s_leds.triggerEvent(EVT_STOP);
          }
        }
      }
```

CLEAR : déjà câblé Task 13 (`s_leds.triggerEvent(EVT_LOOP_CLEAR)`).

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 30 — HW Gate G8 + commit Phase 2.I

- [ ] **Step 1: Upload + monitor + observe LED bar**

**HW Gate G8 procedure** :

**Audit-fix R3 — feedback visuel Phase 2 vs Phase 4** : en Phase 2, `EVENT_RENDER_DEFAULT[EVT_LOOP_REC/OVERDUB/CLEAR]` sont `PTN_NONE` (LedGrammar.cpp:34-40) et `triggerEvent()` retourne early ligne 600 quand patternId = PTN_NONE. Donc les `triggerEvent(EVT_LOOP_*)` câblés Phase 2 (Tasks 13, 29) sont latents — pas d'overlay flash/ramp animé. Le changement de couleur observé vient de `renderBankLoop` state-driven (Task 28), pas d'un event overlay. Le câblage Phase 2 est correct (architectural emit fire-and-forget), Phase 4 activera l'overlay via Tool 8 NVS override ou retune compile-time de EVENT_RENDER_DEFAULT. EVT_PLAY / EVT_STOP / EVT_WAITING en revanche ont des `PTN_*` valides (LedGrammar.cpp:28-30) et animent dès Phase 2.

1. Switch to LOOP bank. LED bar : la LED de la bank LOOP = Gold (CSLOT_MODE_LOOP).
2. Tap REC → LED change vers coral (CSLOT_VERB_REC) via `renderBankLoop` state-driven. (`triggerEvent(EVT_LOOP_REC)` câblé mais inactif Phase 2 — pas de flash blanc bref attendu ici.)
3. Pendant RECORDING, FG color reste coral.
4. Tap REC → LED change vers green (CSLOT_VERB_PLAY) via state-driven + EVT_PLAY animé (PTN_FADE). Bar/Wrap flash visibles selon `tickBarDurationMs` (consumeBarFlash / consumeWrapFlash, en color CSLOT_VERB_PLAY).
5. Tap REC → LED change vers amber (CSLOT_VERB_OVERDUB) via state-driven. (`triggerEvent(EVT_LOOP_OVERDUB)` câblé mais inactif Phase 2.)
6. Tap REC → retour green (PLAYING) + EVT_PLAY animé.
7. Tap PLAY/STOP (quantize Bar) → LED crossfade green ↔ white (EVT_WAITING via PTN_CROSSFADE_COLOR sur CSLOT_VERB_PLAY × CSLOT_CONFIRM_OK).
8. Quand stop commit, LED → Gold (STOPPED, state-driven). Pas de LED event au boundary commit — la transition silencieuse est attendue.
9. Hold CLEAR 500ms → completion silencieuse Phase 2 (pas de ramp cyan : `EVENT_RENDER_DEFAULT[EVT_LOOP_CLEAR]` = `PTN_NONE`). Loop wipe + LED retour Gold via state-driven. Le ramp cyan + SPARK blanc seront activés Phase 4 (Tool 8 override RAMP_HOLD sur CSLOT_VERB_CLEAR_LOOP).

**Critère G8** : Visual feedback cohérent avec ce que les pattern actifs Phase 2 livrent (EVT_PLAY/STOP/WAITING + state-driven renderBankLoop). Pas de stuck color. Pas d'attente de ramp cyan / flash REC tant que Phase 4 n'a pas tuné EVENT_RENDER_DEFAULT.

- [ ] **Step 2: Commit gate Phase 2.I**

```bash
git add src/core/LedController.cpp src/main.cpp
git commit -m "$(cat <<'EOF'
feat(loop): LedController renderBankLoop body + EVT_LOOP_* triggers (Phase 2.I)

- renderBankLoop drives FG color by LoopEngine state :
   EMPTY/STOPPED  -> CSLOT_MODE_LOOP (Gold)
   RECORDING      -> CSLOT_VERB_REC (Coral)
   OVERDUBBING    -> CSLOT_VERB_OVERDUB (Amber)
   PLAYING        -> CSLOT_VERB_PLAY (Green)
  BG = FG × bgFactor (v9 unified).
- consumeBarFlash / consumeWrapFlash drive FLASH overlay with
  tickBarDurationMs duration (Phase 4 may differentiate Bar vs Wrap)
- processLoopMode trigger LED events :
   tap REC EMPTY->RECORDING        : EVT_LOOP_REC
   tap REC PLAYING/STOPPED->OD     : EVT_LOOP_OVERDUB
   tap REC RECORDING->PLAYING      : EVT_PLAY
   tap PLAY/STOP entering WAITING_*: EVT_WAITING
   tap PLAY/STOP STOPPED->PLAYING  : EVT_PLAY
   tap PLAY/STOP PLAYING->STOPPED  : EVT_STOP
   long-press CLEAR fires          : EVT_LOOP_CLEAR (déjà Task 13)
- HW gate G8 OK : visual feedback matches LED spec §17 LOOP state table

Spec §21 + LED spec §10 §12 §17 + EVT_LOOP_* réservés Phase 1 (LedGrammar.h)
EOF
)"
```

---

# Phase 2.J — BankManager double-tap LOOP + toggleAllArpsAndLoops + midiPanic extension

Objectif : remplacer le silent-consume Phase 1 du LOOP double-tap par `loopEngine->tapPlayStop()`. Étendre `toggleAllArps` → `toggleAllArpsAndLoops`. Étendre `midiPanic` pour flush LoopEngines.

### Task 31 — Replace BankManager LOOP double-tap consume with `loopEngine->tapPlayStop()`

**Files:**
- Modify: `src/managers/BankManager.cpp:101-110`

- [ ] **Step 1: Read current LOOP double-tap branch**

```bash
sed -n '95,115p' /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/BankManager.cpp
```

- [ ] **Step 2: Add include `#include "../loop/LoopEngine.h"` at top of BankManager.cpp**

```bash
grep -n "include.*ArpEngine" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/BankManager.cpp
```
Add `#include "../loop/LoopEngine.h"` below the `#include "../arp/ArpEngine.h"` line.

- [ ] **Step 3: Replace LOOP double-tap branch**

Original (BankManager.cpp:101-110) :
```cpp
      // --- Double-tap on LOOP bank pad : consume silently for now ---
      // Spec LOOP §19 : LEFT+double-tap → LoopEngine.toggle() (Phase 2+).
      // Without a LoopEngine the 2nd tap is consumed to prevent a bank-switch
      // parasite. PLAY/STOP LOOP will also be reachable via the dedicated
      // control pad on the musical layer (Phase 2 handleLoopControls).
      else if (wasRecent && _banks[b].type == BANK_LOOP) {
        _lastBankPadPressTime[b] = 0;
        _pendingSwitchBank = -1;
        continue;
      }
```

Modified :
```cpp
      // --- Double-tap on LOOP bank pad : toggle PLAY/STOP via LoopEngine ---
      // Spec LOOP §19 : LEFT+double-tap = PLAY/STOP toggle, FG ou BG.
      // BG context : keys = nullptr (no fingers possible off-foreground).
      // Audit-fix R2 : dispatch LED event aligné sur processLoopMode Task 29 —
      // isPlaying() retourne false en WAITING_PLAY/STOP, donc le test naïf
      // `isPlaying() ? EVT_PLAY : EVT_STOP` émettrait EVT_STOP sur entrée en
      // WAITING_PLAY (alors que la LOOP va démarrer au prochain boundary !).
      // Check WAITING_* d'abord, puis transitions STOPPED↔PLAYING.
      else if (wasRecent && _banks[b].type == BANK_LOOP) {
        if (_banks[b].loopEngine && _transport) {
          const uint8_t* keys = (b == _currentBank) ? keyIsPressed : nullptr;
          LoopState before = _banks[b].loopEngine->getState();
          _banks[b].loopEngine->tapPlayStop(*_transport, keys);
          LoopState after = _banks[b].loopEngine->getState();
          if (_leds) {
            EventId evt;
            if (after == LoopState::WAITING_PLAY || after == LoopState::WAITING_STOP) {
              evt = EVT_WAITING;
            } else if (before == LoopState::STOPPED && after == LoopState::PLAYING) {
              evt = EVT_PLAY;
            } else if (before == LoopState::PLAYING && after == LoopState::STOPPED) {
              evt = EVT_STOP;
            } else {
              evt = _banks[b].loopEngine->isPlaying() ? EVT_PLAY : EVT_STOP;  // fallback
            }
            _leds->triggerEvent(evt, (uint8_t)(1 << b));
          }
        }
        _lastBankPadPressTime[b] = 0;
        _pendingSwitchBank = -1;
        continue;
      }
```

- [ ] **Step 4: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 32 — Rename `toggleAllArps` → `toggleAllArpsAndLoops` + extend to LOOP

**Files:**
- Modify: `src/main.cpp:895-919` (function)
- Modify: any call site (search `toggleAllArps`)

- [ ] **Step 1: Find all call sites of `toggleAllArps`**

```bash
grep -rn "toggleAllArps" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/
```

- [ ] **Step 2: Rename function + extend body**

Original (main.cpp:895-919) :
```cpp
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
      s_banks[i].arpEngine->setCaptured(false, s_transport, nullptr, s_holdPad);
      mask |= (uint8_t)(1 << i);
    } else if (!anyPlaying && s_banks[i].arpEngine->isPaused()
                              && s_banks[i].arpEngine->hasNotes()) {
      s_banks[i].arpEngine->setCaptured(true, s_transport, nullptr, s_holdPad);
      mask |= (uint8_t)(1 << i);
    }
  }
  if (mask != 0) s_leds.triggerEvent(anyPlaying ? EVT_STOP : EVT_PLAY, mask);
}
```

Modified (rename + LOOP iteration, per loop-buffer-invariants §6) :
```cpp
// --- Toggle play/stop globalement sur ARPEG + LOOP banks ---
// loop-buffer-invariants §6 : extension de toggleAllArps inclut désormais
// les LoopEngines. Géométrie symétrique :
//   - Au moins une bank en Play (ARPEG capturé OU LOOP playing) → Stop sur tous
//   - Sinon, Play sur ce qui peut repartir (ARPEG paused-with-notes, LOOP STOPPED-with-content)
static void toggleAllArpsAndLoops() {
  bool anyPlaying = false;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (isArpType(s_banks[i].type) && s_banks[i].arpEngine
        && s_banks[i].arpEngine->isCaptured()) {
      anyPlaying = true;
      break;
    }
    if (s_banks[i].type == BANK_LOOP && s_banks[i].loopEngine
        && s_banks[i].loopEngine->isPlaying()) {
      anyPlaying = true;
      break;
    }
  }
  uint8_t mask = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    // ARPEG / ARPEG_GEN banks
    if (isArpType(s_banks[i].type) && s_banks[i].arpEngine) {
      if (anyPlaying && s_banks[i].arpEngine->isCaptured()) {
        s_banks[i].arpEngine->setCaptured(false, s_transport, nullptr, s_holdPad);
        mask |= (uint8_t)(1 << i);
      } else if (!anyPlaying && s_banks[i].arpEngine->isPaused()
                                && s_banks[i].arpEngine->hasNotes()) {
        s_banks[i].arpEngine->setCaptured(true, s_transport, nullptr, s_holdPad);
        mask |= (uint8_t)(1 << i);
      }
    }
    // LOOP banks
    else if (s_banks[i].type == BANK_LOOP && s_banks[i].loopEngine) {
      LoopEngine* le = s_banks[i].loopEngine;
      if (anyPlaying && le->isPlaying()) {
        le->tapPlayStop(s_transport, nullptr);  // BG context
        mask |= (uint8_t)(1 << i);
      } else if (!anyPlaying && le->getState() == LoopState::STOPPED && le->hasContent()) {
        le->tapPlayStop(s_transport, nullptr);
        mask |= (uint8_t)(1 << i);
      }
    }
  }
  if (mask != 0) s_leds.triggerEvent(anyPlaying ? EVT_STOP : EVT_PLAY, mask);
}
```

- [ ] **Step 3: Rename call sites**

For each occurrence found in Step 1, replace `toggleAllArps()` with `toggleAllArpsAndLoops()`.

- [ ] **Step 4: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 33 — Extend `midiPanic` to flush LoopEngines

**Files:**
- Modify: `src/main.cpp:150-166`

- [ ] **Step 1: Read midiPanic body**

```bash
sed -n '145,170p' /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
```

- [ ] **Step 2: Add LOOP flush phase**

Original :
```cpp
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
  viewer::emitPanic();
}
```

Modified :
```cpp
static void midiPanic() {
  // Phase 1a: flush all arp engines (pending events + refcounts)
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (isArpType(s_banks[i].type) && s_banks[i].arpEngine) {
      s_banks[i].arpEngine->flushPendingNoteOffs(s_transport);
    }
  }
  // Phase 1b: flush all loop engines (pending noteOffs + refcounts)
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (s_banks[i].type == BANK_LOOP && s_banks[i].loopEngine) {
      s_banks[i].loopEngine->flushPendingNoteOffs(s_transport);
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
  viewer::emitPanic();
}
```

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 34 — BankManager guard bank switch pendant LOOP REC/OD (M9 audit fix, spec §23.2)

**Files:**
- Modify: `src/managers/BankManager.cpp::update` — **deux paths à patcher** (L133-143 timeout + L146-158 LEFT-release fast-forward).

> **m5 audit fix** : task intégrée dans Phase 2.J (était post-Self-Review dans la version pré-audit). Hardcode le guard contre l'invariant §23.2 (bank switch refusé silencieusement si LOOP courante en REC/OD). Si l'implémenteur skip cette task, Phase 2 ne garantit pas invariant 11 (max 1 LOOP en REC/OD).
> **M9 audit fix** : 2 paths du switch — pending-timeout ET LEFT-release fast-forward — DOIVENT être patchés tous les deux. Snippet explicite ci-dessous pour les 2.

- [ ] **Step 1: Read BankManager.cpp::update full body**

```bash
sed -n '59,170p' /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/BankManager.cpp
```

- [ ] **Step 2: Patch path #1 — pending-timeout (BankManager.cpp:133-143)**

Original :
```cpp
  // --- Pending switch expiration (natural timeout) ---
  if (_pendingSwitchBank >= 0 &&
      (now - _pendingSwitchTime) >= _doubleTapMs) {
    uint8_t target = (uint8_t)_pendingSwitchBank;
    _pendingSwitchBank = -1;
    if (target != _currentBank) {
      switchToBank(target);
      if (btnLeftHeld) _switchedDuringHold = true;
      switched = true;
    }
  }
```

Modified (silent deny si LOOP courante locked) :
```cpp
  // --- Pending switch expiration (natural timeout) ---
  if (_pendingSwitchBank >= 0 &&
      (now - _pendingSwitchTime) >= _doubleTapMs) {
    uint8_t target = (uint8_t)_pendingSwitchBank;
    _pendingSwitchBank = -1;
    if (target != _currentBank) {
      // Spec §23.2 + invariant 11 : bank switch refusé pendant LOOP RECORDING/OVERDUBBING
      // de la bank courante. Silent deny (pas de LED feedback per §23.2).
      BankSlot& current = _banks[_currentBank];
      bool currentLocked = (current.type == BANK_LOOP && current.loopEngine
                            && current.loopEngine->isLocked());
      if (!currentLocked) {
        switchToBank(target);
        if (btnLeftHeld) _switchedDuringHold = true;
        switched = true;
      }
      // else : silent deny (current LOOP in REC/OD)
    }
  }
```

- [ ] **Step 3: Patch path #2 — LEFT-release fast-forward (BankManager.cpp:146-158)** (M9 audit : snippet explicite)

Original :
```cpp
  // --- Detect LEFT button release edge (held → not held) ---
  if (!btnLeftHeld && _lastBtnState) {
    // Fast-forward pending switch on LEFT release
    if (_pendingSwitchBank >= 0) {
      uint8_t target = (uint8_t)_pendingSwitchBank;
      _pendingSwitchBank = -1;
      if (target != _currentBank) {
        switchToBank(target);
        _switchedDuringHold = true;
        switched = true;
      }
    }
    if (_switchedDuringHold && _lastKeys) {
      // Snapshot current state as "previous" — prevents phantom
      // noteOff/noteOn when resuming normal play after a switch.
      memcpy(_lastKeys, keyIsPressed, NUM_KEYS);
    }
  }
```

Modified (même guard appliqué au fast-forward) :
```cpp
  // --- Detect LEFT button release edge (held → not held) ---
  if (!btnLeftHeld && _lastBtnState) {
    // Fast-forward pending switch on LEFT release
    if (_pendingSwitchBank >= 0) {
      uint8_t target = (uint8_t)_pendingSwitchBank;
      _pendingSwitchBank = -1;
      if (target != _currentBank) {
        // M9 audit fix : même guard que pending-timeout path.
        // Spec §23.2 silent deny si LOOP courante en REC/OD.
        BankSlot& current = _banks[_currentBank];
        bool currentLocked = (current.type == BANK_LOOP && current.loopEngine
                              && current.loopEngine->isLocked());
        if (!currentLocked) {
          switchToBank(target);
          _switchedDuringHold = true;
          switched = true;
        }
        // else : silent deny
      }
    }
    if (_switchedDuringHold && _lastKeys) {
      // Snapshot current state as "previous" — prevents phantom
      // noteOff/noteOn when resuming normal play after a switch.
      memcpy(_lastKeys, keyIsPressed, NUM_KEYS);
    }
  }
```

- [ ] **Step 4: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

### Task 34.5 — Audit-fix B-N1 / R-N1 : `onBackgroundTransition` + main.cpp call

**Files:**
- Modify: `src/loop/LoopEngine.cpp` (implémentation `onBackgroundTransition`)
- Modify: `src/main.cpp` (`handleManagerUpdates` — snapshot prevBank + appel post-bank-switch)

> **Rationale** : audit final adversarial 2026-05-18 a identifié 2 bugs invariant-violating laissés par M9 guard :
> - **B-N1** : bank switch depuis LOOP non-locked + live press → `refCountNoteOn` (live monitor) sur l'ancien engine jamais décrémenté → MIDI noteOff jamais envoyé sur l'ancien channel → stuck note au DAW. Spec §23.1 invariant 1 violé.
> - **R-N1** : `_clearPressStartMs` stale quand la bank LOOP passe en BG → retour FG avec CLEAR encore tenu = `isClearHoldFired` retourne true au 1er frame (parce que `now - _clearPressStartMs >> 500ms`) → `longPressClear` fire instantanément, buffer wipé sans le seuil 500 ms. Spec §9 garde-fou court-circuité.
>
> Fix unifié : méthode `LoopEngine::onBackgroundTransition(transport)` qui flushe le live press refcount (via tracker `_padHeldLive[]`) ET reset CLEAR tracker (via `notifyClearPressEnd()`). Appelée depuis main.cpp dès qu'un bank switch détecté, sur l'**ancienne** bank si LOOP.
>
> Coût : ~25 lignes total (méthode + call site). 48 bytes RAM (tracker `_padHeldLive[NUM_KEYS]` qui remplace `_padHeldInRec[]`, voir Task 1 step 3 patch). CPU O(48+128) au moment du switch (événement rare, ~1/10s en jeu live).

- [ ] **Step 1: Implémenter `LoopEngine::onBackgroundTransition` dans `src/loop/LoopEngine.cpp`**

Ajouter à la fin de LoopEngine.cpp (ou à côté de `flushPendingNoteOffs`) :

```cpp
// =================================================================
// onBackgroundTransition — appelé au bank switch quand cette bank passe FG → BG
// =================================================================
// Audit-fix B-N1 / R-N1 (audit adversarial 2026-05-18) :
//   B-N1 : pads physiquement tenus (live press refCount > 0) ne reçoivent
//          jamais de noteOff naturel parce que processLoopMode n'est plus appelé
//          pour cette bank en BG. Stuck note au DAW. Solution : refCountNoteOff
//          direct pour chaque pad du tracker _padHeldLive[].
//   R-N1 : _clearPressStartMs stale (figé à la valeur du moment où la bank était
//          FG). Au retour FG, isClearHoldFired retourne true instantanément si
//          assez de temps a passé en BG → wipe sans seuil 500 ms. Solution :
//          notifyClearPressEnd() reset le tracker (et _clearFired).
// IMPORTANT : ne PAS changer `_state`. La bank LOOP en BG doit pouvoir continuer
// à jouer son buffer normalement (spec §14 multi-bank LOOP).
// =================================================================
void LoopEngine::onBackgroundTransition(MidiTransport& transport) {
  // Phase 1 (B-N1) : flush live press refCount pour chaque pad encore physiquement tenu.
  // refCountNoteOff décrémente uniquement la contribution live press ; si le buffer
  // playback avait aussi incrémenté la même note (refCount = 2), le noteOff MIDI
  // ne fire pas ici (1→1, return early sur 0→0), mais le buffer fire naturellement
  // son noteOff matching plus tard → MIDI noteOff cohérent au DAW.
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    if (_padHeldLive[pad]) {
      refCountNoteOff(transport, resolvePadToMidiNote(pad));
      _padHeldLive[pad] = 0;
    }
  }
  // Phase 2 (R-N1) : reset CLEAR press tracker. Au retour FG, notifyClearPressStart
  // détectera la nouvelle frame avec _clearPressStartMs == 0 et armera le timer
  // fresh (500ms à attendre depuis le retour, pas depuis le press d'origine).
  notifyClearPressEnd();
}
```

- [ ] **Step 2: Snapshot `prevBank` + appel `onBackgroundTransition` dans `main.cpp::handleManagerUpdates`**

Locate `handleManagerUpdates` (autour de main.cpp:821-884) :

```bash
grep -n "static bool handleManagerUpdates\|bool bankSwitched = s_bankManager.update" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
```

Modifier la séquence `update + bankSwitched` :

Original :
```cpp
static bool handleManagerUpdates(const SharedKeyboardState& state, bool leftHeld) {
  bool bankSwitched = s_bankManager.update(state.keyIsPressed, leftHeld);
  s_scaleManager.update(state.keyIsPressed, leftHeld, s_bankManager.getCurrentSlot());

  if (bankSwitched) {
    s_nvsManager.queueBankWrite(s_bankManager.getCurrentBank());
    reloadPerBankParams(s_bankManager.getCurrentSlot());
  }
  ...
}
```

Modified (snapshot prevBank avant update + appel `onBackgroundTransition` post-switch) :
```cpp
static bool handleManagerUpdates(const SharedKeyboardState& state, bool leftHeld) {
  // Audit-fix B-N1/R-N1 : snapshot la bank courante AVANT le switch pour pouvoir
  // appeler onBackgroundTransition sur l'ancien LoopEngine si nécessaire.
  uint8_t prevBank = s_bankManager.getCurrentBank();
  bool bankSwitched = s_bankManager.update(state.keyIsPressed, leftHeld);
  s_scaleManager.update(state.keyIsPressed, leftHeld, s_bankManager.getCurrentSlot());

  if (bankSwitched) {
    // Audit-fix B-N1/R-N1 : si l'ancienne bank était LOOP, flushe son live press
    // refCount + reset CLEAR tracker. Évite stuck note au DAW (invariant §23.1)
    // et wipe instantané au retour FG (spec §9).
    if (s_banks[prevBank].type == BANK_LOOP && s_banks[prevBank].loopEngine) {
      s_banks[prevBank].loopEngine->onBackgroundTransition(s_transport);
    }
    s_nvsManager.queueBankWrite(s_bankManager.getCurrentBank());
    reloadPerBankParams(s_bankManager.getCurrentSlot());
  }
  ...
}
```

- [ ] **Step 3: Build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -30
```
Expected: PASS.

- [ ] **Step 4: Auto-review**

```bash
grep -n "onBackgroundTransition" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/LoopEngine.cpp /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/LoopEngine.h /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
```
Expected: 3 occurrences au minimum :
- 1 dans `LoopEngine.h` (declaration).
- 1 dans `LoopEngine.cpp` (implémentation).
- 1 dans `main.cpp` (call site dans handleManagerUpdates).

### Task 35 — HW Gate G9 + commit Phase 2.J

- [ ] **Step 1: Upload + monitor + DAW**

**HW Gate G9 procedure** (post-M9 bank switch guard + audit-fix B-N1/B-N2/R-N1) :
1. Créer 4 banks LOOP (MAX_LOOP_BANKS=4 acté). Enregistrer une boucle simple sur 2 d'entre elles.
2. Mettre les 2 banks en PLAYING.
3. LEFT + hold pad simple tap → `toggleAllArpsAndLoops` → toutes les LOOP doivent stopper en même temps. ARPEG aussi si présent.
4. LEFT + hold pad → relancer tout.
5. Test BG : sur bank 0 (FG), faire jouer une LOOP. Switcher vers bank 4 (autre LOOP). Verifier que bank 0 LOOP continue de jouer en BG. LEFT + double-tap bank 0 pad → tapPlayStop sur bank 0 BG → stop audible.
6. Test panic : tap rear button longuement (raccourci panic, si configuré) ou trigger panic via viewer. Tous les LoopEngines flushent leurs notes. Pas de stuck note.
7. **Test M9 bank switch guard** : sur bank A LOOP, tap REC → RECORDING. Pendant RECORDING, LEFT + tap bank B pad → switch DOIT être refusé silencieusement. Tap REC à nouveau pour finir RECORDING → PLAYING. Re-test bank switch → maintenant autorisé.
8. Test invariant 11 §23 : tenter REC sur bank A pendant que bank B est en RECORDING (impossible par construction puisque bank switch refusé pendant REC). Vérifier que le scenario "REC sur deux banks simultanément" n'est jamais atteignable.
9. **Test Audit-fix B-N1 (bank switch + live press)** : sur LOOP A en PLAYING, **maintenir physiquement** un drum pad (live monitor → MIDI noteOn au DAW). LEFT + tap bank pad B → switch autorisé (A non-locked). **Relâcher** le drum pad. Vérifier au DAW que le **MIDI noteOff arrive sur le channel A** dans la même frame que le bank switch (pas après le release physique). Sans le fix : note bloquée indéfiniment.
10. **Test Audit-fix B-N2 (overdub merge + pad tenu)** : sur LOOP avec un loop simple en PLAYING, tap REC → OVERDUBBING. Presser et **maintenir** un nouveau drum pad. **Sans relâcher**, tap REC pour merger → état PLAYING. Relâcher le pad. Vérifier au DAW que la note **s'arrête bien au release** (pas de drone bloqué). Au cycle suivant, le hat merged doit jouer normalement dans le motif. Sans le fix : note bloquée indéfiniment.
11. **Test Audit-fix R-N1 (CLEAR stale au retour FG)** : sur LOOP A, presser CLEAR pad et **maintenir**. Avant 500 ms, LEFT + tap bank B → switch autorisé. **Garder CLEAR enfoncé** pendant 2+ secondes. LEFT + tap bank A pour revenir. Vérifier que le buffer **n'est PAS wipé instantanément** — soit attendre 500 ms post-retour pour qu'il wipe (rampe LED cyan visible), soit relâcher CLEAR sans wipe. Sans le fix : wipe instantané au 1er frame de retour sans seuil ni rampe.

**Critère G9** : Toggle multi-bank fonctionnel ARPEG+LOOP. BG LOOP toggle via double-tap fonctionne. midiPanic clean toutes les LOOP banks. **M9 guard : bank switch silently refusé pendant LOOP REC/OD**. **Audit-fix B-N1/B-N2/R-N1 validés (steps 9/10/11)**. Pas de notes bloquées au panic ni dans les 3 scenarios audit.

- [ ] **Step 2: Commit gate Phase 2.J**

```bash
git add src/managers/BankManager.cpp src/main.cpp src/loop/LoopEngine.cpp src/loop/LoopEngine.h
git commit -m "$(cat <<'EOF'
feat(loop): BankManager LOOP + toggleAllArpsAndLoops + midiPanic + audit-fix B-N1/B-N2/R-N1 (Phase 2.J)

- BankManager double-tap LOOP : remplace le silent-consume Phase 1 par
  loopEngine->tapPlayStop() (FG ou BG). Spec §19.
- Rename toggleAllArps -> toggleAllArpsAndLoops, étend l'itération pour
  inclure LoopEngines (loop-buffer-invariants §6) :
   - anyPlaying = arpEngine.isCaptured() OU loopEngine.isPlaying()
   - Stop : ARPEG setCaptured(false) + LOOP tapPlayStop pour les banks playing
   - Play : ARPEG setCaptured(true) si paused-with-notes + LOOP tapPlayStop
            si STOPPED-with-content
   - Single LED triggerEvent EVT_PLAY/STOP avec bitmask multi-bank
- midiPanic ext : flush all LoopEngines avant MidiEngine.allNotesOff
- M9 + spec §23.2 : BankManager guard bank switch silent deny pendant LOOP REC/OD
   - pending-timeout path : check current.loopEngine->isLocked()
   - LEFT-release fast-forward path : même check (M9 audit fix les 2 paths explicit)
- Audit adversarial 2026-05-18 — fix B-N1/B-N2/R-N1 :
   - LoopEngine::onBackgroundTransition() : flush live press refcount via tracker
     _padHeldLive[] + reset CLEAR press tracker. NE CHANGE PAS _state (LOOP en BG
     continue son playback per spec §14).
   - main.cpp handleManagerUpdates : snapshot prevBank, appel post-bank-switch
     sur l'ancienne bank si type LOOP.
   - Tracker _padHeldLive[NUM_KEYS] (remplace _padHeldInRec[]) set en top de
     capturePadEvent dans TOUS les états (live press tracking unifié).
   - mergeOverdub : flush held pads (inject noteOff à _playPositionUs) — fix B-N2
     spec §8 "pads tenus flushés comme à la clôture d'un RECORDING initial".
- HW gate G9 OK : multi-bank toggle ARPEG+LOOP, BG LOOP double-tap, panic clean,
  bank switch refused pendant LOOP REC/OD, audit-fix B-N1/B-N2/R-N1 validés
  (steps 9/10/11 procédure G9).

Spec §8, §9, §14, §19, §23.1 §23.2 + loop-buffer-invariants §6 + invariant 11
Audit : audit final adversarial 2026-05-18 (B-N1, B-N2, R-N1).
EOF
)"
```

---

### Task 36 — Doc-sync (m11 audit fix) — refs `docs/reference/*` + STATUS.md + spec MAJ

**Files:**
- Modify: `docs/reference/runtime-flows.md`
- Modify: `docs/reference/nvs-reference.md`
- Modify: `docs/reference/architecture-briefing.md`
- Modify: `STATUS.md`
- Modify: `docs/superpowers/specs/2026-04-19-loop-mode-design.md` (acter Phase 2 close)
- Modify: `docs/superpowers/LOOP_PROGRESS.md`

> **Rationale m11 audit fix** : project CLAUDE.md `~/.claude/CLAUDE.md` + `.claude/CLAUDE.md` impose **keep-in-sync protocol** sur les refs touchés dans la même commit. Phase 2 introduit un sous-système runtime entier (LoopEngine) — doc sync obligatoire pour ne pas créer de drift.

- [ ] **Step 1: Update `docs/reference/runtime-flows.md`**

Ajouter une section "LOOP runtime flow (Phase 2)" décrivant :
- Pad press → `handlePadInput` → `processLoopMode` (dispatch par bank type).
- Capture buffer flow : `processLoopMode` → `LoopEngine::capturePadEvent` → `insertEventSorted` (live-sort M2) → buffer sorted invariant.
- Playback flow : main loop tick → `LoopEngine::update` → B1 intégration incrémentale + walk events triés + refcount noteOn/Off → MidiTransport.
- Transport actions : tap REC/PLAY/CLEAR via control pads + LEFT+double-tap bank pad → state machine.
- Wrap detection : `_scaledElapsedUs >= _loopDurationUs` → fire tail + reset + `_wrapFlash=true` → `renderBankLoop` consume.
- Quantize WAITING_* : ClockManager tick boundary → `commitWaitingAction(nowUs)` → `startPlayback(nowUs)`.

- [ ] **Step 2: Update `docs/reference/nvs-reference.md`**

Faire passer `LoopPadStore` + `LoopPotStore` de **DECLARED Phase 1** à **DECLARED + LOADED Phase 2** :
- Pour `LoopPadStore` : ajouter accessor `NvsManager::getLoadedLoopPadStore()` + helper `applyDevSeedLoopPadsIfSafe()` (M7 fix). Pas de writer NVS Phase 2 (Phase 3 = Tool 3 b1).
- Pour `LoopPotStore` : ajouter accessor `getLoadedLoopPotParams(bankIdx)`. Pas de writer NVS Phase 2 (Phase 5 = effets + pot runtime).
- Confirmer `descriptor index 12 LoopPadStore` toujours valide.

- [ ] **Step 3: Update `docs/reference/architecture-briefing.md`**

§4 Table 1 "Tools × banks × params" : ajouter colonne LOOP, lignes velocity/quantize remplies pour LOOP, lignes scale/octave/pitch marquées `—` pour LOOP. §0 Scope Triage : ajouter route "tight modification LOOP runtime → loop-buffer-invariants.md + arp-reference.md (template)".

- [ ] **Step 4: Update `STATUS.md`**

Ajouter section "LOOP Phase 2 — historique commits" alignée avec Phase 1, ARPEG_GEN, Refacto Tool 5. Lister les commits Phase 2.A → 2.J + Task 36 doc-sync. Mettre à jour "Focus courant" pour acter Phase 2 close + prochaine étape Phase 3 (Tool 3 b1).

- [ ] **Step 5: Update `docs/superpowers/specs/2026-04-19-loop-mode-design.md`**

- §3 LoopEngine core : acter `MAX_LOOP_BANKS = 4` (au lieu de "à figer Phase 2 selon mesure réelle, la spec d'origine proposait 2").
- §25 Budget ressources : actualiser SRAM "4 banks × ~9.7 KB ≈ 38.8 KB" (au lieu de "2 banks × 9.4 KB = 18.8 KB"). Mention budget total 320 KB → ~28 % utilisé.
- §27 Partie 7 : marquer **Phase 2 — CLOSE** avec lien commits.

- [ ] **Step 6: Update `docs/superpowers/LOOP_PROGRESS.md`**

- Tableau d'étapes : Phase 2 LOOP statut `✅ **CLOSE** (commits Phase 2.A → 2.J)` au lieu de `⏳ À rédiger`.
- Sources actives : confirmer references à jour.

- [ ] **Step 7: HARD-ASSERT doc-sync** (Audit-renforcement vigilance Claude — Task 36 est la zone de survol la plus à risque du plan)

Avant le commit gate, exécuter le hard-assert qui vérifie que chaque fichier contient au moins un keyword attendu. Si une seule assertion échoue, **STOP** — la doc-sync est incomplète.

```bash
FAIL_COUNT=0

check() {
  local file="$1"
  local pattern="$2"
  local count=$(grep -E -c "$pattern" "/Users/loic/Code/PROJECTS/ILLPAD_V2/$file" 2>/dev/null || echo 0)
  if [ "$count" -eq 0 ]; then
    echo "FAIL: $file missing expected pattern: $pattern"
    FAIL_COUNT=$((FAIL_COUNT + 1))
  else
    echo "PASS: $file has '$pattern' ($count match(es))."
  fi
}

check "docs/reference/runtime-flows.md"                    "LOOP runtime flow|LoopEngine::update"
check "docs/reference/nvs-reference.md"                    "getLoadedLoopPadStore|applyDevSeedLoopPadsIfSafe"
check "docs/reference/architecture-briefing.md"            "BANK_LOOP|LoopEngine"
check "STATUS.md"                                          "LOOP Phase 2"
check "docs/superpowers/specs/2026-04-19-loop-mode-design.md"  "MAX_LOOP_BANKS = 4|MAX_LOOP_BANKS=4"
check "docs/superpowers/LOOP_PROGRESS.md"                  "CLOSE"

if [ "$FAIL_COUNT" -gt 0 ]; then
  echo ""
  echo "FAIL: $FAIL_COUNT doc files incomplete. STOP — ne pas commit doc-sync."
  echo "Re-éditer les fichiers manquants (steps 1-6), puis relancer ce hard-assert."
  exit 1
fi
echo ""
echo "PASS: tous les 6 fichiers doc-sync contiennent les keywords attendus. OK pour commit."
```

Si exit 1 : **STOP immédiat**. Signaler à Loïc quels fichiers manquent. Ne PAS faire le `git add` ni le commit. Re-éditer les fichiers concernés (steps 1-6 du plan ci-dessus) puis relancer ce hard-assert.

- [ ] **Step 8: Commit gate Task 36 doc-sync** (uniquement si Step 7 PASS)

```bash
git add docs/reference/runtime-flows.md docs/reference/nvs-reference.md docs/reference/architecture-briefing.md STATUS.md docs/superpowers/specs/2026-04-19-loop-mode-design.md docs/superpowers/LOOP_PROGRESS.md
git commit -m "$(cat <<'EOF'
docs(loop): Phase 2 doc-sync — runtime-flows + nvs-ref + architecture-briefing + STATUS + spec MAJ + LOOP_PROGRESS

- runtime-flows.md : section "LOOP runtime flow (Phase 2)" — capture sorted live-sort,
  playback intégration incrémentale, transport actions, wrap detection, quantize WAITING_*.
- nvs-reference.md : LoopPadStore + LoopPotStore passent DECLARED→LOADED Phase 2.
- architecture-briefing.md : §4 Table 1 LOOP column + §0 routing.
- STATUS.md : section "LOOP Phase 2 — historique commits" + focus courant MAJ.
- spec LOOP : §3 acte MAX_LOOP_BANKS=4, §25 actualise SRAM, §27 marque CLOSE.
- LOOP_PROGRESS.md : Phase 2 CLOSE.

Audit m11 (post-audit doc-sync) — keep-in-sync protocol projet CLAUDE.md.
EOF
)"
```

---

# Self-Review (à exécuter avant fin Phase 2)

### 1. Spec coverage

Pour chaque section spec, vérifier qu'au moins une task implémente le comportement :

| Spec section | Task(s) |
|---|---|
| §3 LOOP core state machine | Tasks 1, 4, 17, 18, 20, 22, 26 |
| §6 Tool 5 config (default Bar) | Hors scope (Tool 5 refacto livré) |
| §7 Enregistrement + bar-snap | Tasks 17, 18 |
| §8 Overdub | Tasks 24 |
| §9 Play/Stop/Clear | Tasks 4, 20, 22, 26 (CLEAR Task 4 + 13 + 28) |
| §10 Effets | Hors scope Phase 2 (Phase 5) — params loadés Tasks 9-10 |
| §11-§13 Slot save/load/delete | Hors scope Phase 2 (Phase 6) |
| §14 Multi-loops parallèle | Tasks 6, 22, 32, 33 |
| §15 Cohabitation NORMAL/ARPEG/ControlPads | Task 13 (filter isControlPad + isLoopControlPad) |
| §16 Clock/tempo/sync (BPM scaling) | Tasks 17, 20, 21 (B1 intégration incrémentale `_scaledElapsedUs`) |
| §17 Quantization | Tasks 4, 21, 26 (B3 nowUs propagé commitWaitingAction) |
| §18 Hiérarchie résolution rôles + percussion fixe LOOP | Tasks 13, 17 (M8 live drumming tous états), 31 |
| §19 LEFT + double-tap bank pad | Task 31 |
| §20 NVS + persistence | Tasks 9, 10, 11 (M7 dev seed conditionnel) |
| §21 LED system | Tasks 28 (m4 consume one-shot), 29 |
| §22 Pot routing | Hors scope Phase 2 (Phase 4) — LoopPotStore loadé seulement |
| §23 Invariants 1-11 | Tasks 4 (Q1 layer indep), 17-23 (refcount + flush + no orphan notes), 31-34 (panic + toggle + M9 bank switch guard 2 paths) |
| §27 Phase 2 outline | TOUT (Phase 2 plan, 36 tasks incluant Task 16.5 viewer + Task 36 doc-sync) |
| §28 Q1 (LoopPadStore 23B) | Phase 1 livré (commit `1b0ac8c`) |
| §28 Q2 (PendingEvent dupliqué) | Tasks 1, 3 |
| §28 Q3 (EVT_WAITING mode-invariant) | Phase 1 livré (commit `48b96fb`) |
| §28 Q4 (LOOP FG brightness) | Caduque post-v9 |
| §28 Q5 (STOPPED-loaded REC = PLAYING+OVERDUB) | Task 4, 25 |
| §28 Q6 (Tool 5 refacto) | Livré commit `e2857c5` |
| §28 Q7 (Tool 4 ext refus ControlPad sur LOOP control) | Hors scope (Phase 3) |
| §28 Q8 (invariant 11 - max 1 LOOP en REC/OD) | Garanti par construction (Task 13 filter + Task 34 bank switch guard) |

**Gap §23.2 fermé** : Task 34 (Phase 2.J) — `BankManager` guard bank switch silently refusé quand current LOOP est en RECORDING/OVERDUBBING. Patch sur **les 2 paths** (pending-timeout + LEFT-release fast-forward) via `isLocked()` check sur `current.loopEngine`. HW gate G9 procédure inclut le test (Step 7). Invariant 11 §23 garanti par construction (bank switch refusé pendant REC/OD ⇒ pas de 2e REC simultané).

### 2. Placeholder scan

```bash
grep -rn "TODO\|FIXME\|XXX\|stub" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/LoopEngine.cpp
```
Expected: no `TODO`, no `FIXME`. Si présent → résoudre avant fin Phase 2.

```bash
grep -n "Phase 2 dev seed\|Phase 3" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/NvsManager.cpp
```
Expected: 1 occurrence (le seed dev pads 32/33/34 LoopPadStore, à retirer Phase 3).

### 3. Type consistency

Vérifier que les types utilisés sont cohérents :

```bash
grep -n "LoopState::" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/LoopEngine.cpp
grep -n "LoopState::" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
grep -n "LOOP_QUANT_" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/LoopEngine.cpp
```

(LoopQuantize est un enum non-scoped — les références utilisent `LOOP_QUANT_FREE/BEAT/BAR` directement.)

Vérifier signatures :
- `LoopEngine::tapRec(MidiTransport&)` — pas `(MidiTransport*)`.
- `LoopEngine::tapPlayStop(MidiTransport&, const uint8_t*)` — second param optionnel = nullptr.
- `LoopEngine::capturePadEvent(uint8_t padIndex, uint8_t velocity, MidiTransport&)` — order args.
- `LoopEngine::update(MidiTransport&)` — bare param.
- `LoopEngine::longPressClear(MidiTransport&)` — bare param.

Vérifier que `BankSlot::loopEngine` est lu uniquement après nullptr check.

```bash
grep -n "slot.loopEngine\|s_banks\[.\]\.loopEngine" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp
```
Chaque ligne doit être précédée d'un check `if (slot.loopEngine)` ou équivalent. Sinon → fix.

### 4. Invariants finaux

- [ ] Aucune note bloquée après séquence record → play → 100 wraps → stop → record again
- [ ] Aucune note bloquée après panic
- [ ] Aucune note bloquée après bank switch pendant PLAYING
- [ ] Bank switch refusé pendant RECORDING (Task 34 Phase 2.J)
- [ ] Bank switch refusé pendant OVERDUBBING (Task 34 Phase 2.J)
- [ ] Scale change sur bank ARPEG voisine ne touche pas LOOP (ScaleManager early-return Phase 1)
- [ ] CLEAR long-press wipe buffer + `_clearFired = true` empêche re-fires tant que CLEAR tenu (B2 audit fix)
- [ ] Refus implicite : pas d'écrasement de slot — hors scope Phase 2 (Phase 6)
- [ ] BPM scaling stable : intégration incrémentale `_scaledElapsedUs` cumulative (B1 audit fix) — pas de saut sur tempo change live
- [ ] WAITING_PLAY commit safe : `nowUs` propagé `commitWaitingAction → startPlayback` (B3 audit fix) — pas d'underflow uint32
- [ ] Velocity randomisée 1× au playback (M3 audit fix) — pas de double randomization au capture
- [ ] mergeOverdub atomique (M4 audit fix) — drop atomique si capacité dépassée, pas de paires noteOn/Off cassées
- [ ] LoopEvent sizeof == 8 B (m1 audit fix) — static_assert au .h
- [ ] **Audit-fix B-N1** : Aucune note bloquée après bank switch depuis LOOP non-locked avec live press tenue. Test HW : sur LOOP A en PLAYING, maintenir un drum pad → LEFT + tap bank B → release pad → vérifier que MIDI noteOff arrive sur channel A au DAW. Validé par HW Gate G9 step 9 (Task 35).
- [ ] **Audit-fix B-N2** : Aucune note bloquée après overdub merge avec pad tenu. Test HW : pendant OVERDUBBING, presser et maintenir un drum pad → tap REC pour merger (sans relâcher) → release physique du pad → vérifier que la note s'arrête bien au DAW. Validé par HW Gate G9 step 10 (Task 35).
- [ ] **Audit-fix R-N1** : Pas de wipe instantané au retour FG si CLEAR a été tenu durant tout le bank switch. Test HW : press CLEAR sur LOOP A, switch bank B sans relâcher CLEAR, attendre 2 s, retour bank A → soit wipe seulement après 500 ms post-retour, soit relâcher CLEAR sans wipe. Validé par HW Gate G9 step 11 (Task 35).
- [ ] **Audit-fix tracker `_padHeldLive`** : invariant set ssi pad physiquement enfoncé ET live monitor MIDI noteOn fired. Auto-review code (post-Task 17 step 4) :
  ```bash
  # Le set _padHeldLive doit être en TOP de capturePadEvent (avant le if RECORDING),
  # pas seulement dans la branche RECORDING.
  grep -B5 -A2 "_padHeldLive\[padIndex\]" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/loop/LoopEngine.cpp
  ```
  La/les occurrence(s) doivent montrer le set juste après le live monitor `refCountNoteOn/Off`, AVANT le test `if (_state == LoopState::RECORDING)`.

---

## Phase 2 — fin

**Sortie complète Phase 2 (36 tasks dont Task 16.5 viewer telemetry + Task 36 doc-sync, 9 HW gates G1-G9)** :
- `src/loop/LoopEngine.{h,cpp}` — engine complet : state machine 7 états, recording µs (M2 live-sort buffer toujours trié), bar-snap deadzone 25 % + validation timestamp (M6), playback BPM-scalé (B1 intégration incrémentale, anti-saut tempo change), overdub merge atomique (M2 O(n+m) + M4 pré-check), quantize Free/Beat/Bar (B3 nowUs propagé), refcount noteOn/Off symétrique ArpEngine (Q2 §28 dupliqué), scheduler interne, LED flash hooks (`consumeBarFlash`/`consumeWrapFlash`), `_clearFired` armé après wipe (B2), live drumming MIDI dans tous les états (M8 spec §18 percussion fixe).
- `BankSlot::loopEngine*` field + boot wiring (`s_loopEngines[MAX_LOOP_BANKS=4]` D1), warning `[WARN]` si > cap (m2).
- `processLoopMode()` main wiring + `case BANK_LOOP` dans `handlePadInput`. Velocity strict baseVelocity au capture (M3), variation 1× au playback.
- `NvsManager` étendu : load + accessors `LoopPadStore` + `LoopPotStore[NUM_BANKS]` (Phase 5 runtime apply), helper M7 dev seed conditionnel pads 32/33/34 (skipped si collision ControlPad Tool 4).
- `LedController::renderBankLoop` body complet (m4 consume flags one-shot) + EVT_LOOP_* triggers depuis processLoopMode.
- `BankManager` LOOP double-tap → `tapPlayStop()` (FG/BG) ; **bank switch guard 2 paths (M9 pending-timeout + LEFT-release fast-forward) silent deny pendant LOOP REC/OD**.
- `toggleAllArpsAndLoops()` extension multi-bank (loop-buffer-invariants §6).
- `midiPanic()` extension flush LOOP banks.
- `viewer::emitLoopBufferFull(channel, which)` telemetry (m9) — handler ViewerSerial Task 16.5.
- `static_assert(sizeof(LoopEvent) == 8)` (m1).
- Doc-sync Task 36 : `runtime-flows.md`, `nvs-reference.md`, `architecture-briefing.md`, `STATUS.md`, spec LOOP §3 §25 §27, `LOOP_PROGRESS.md`.

**HW gates validés G1-G9** + critère **G5 ★ premier son MIDI LOOP audible**.

**Reste à faire post-Phase 2** :
- **Phase 3** : Tool 3 b1 refactor (3 sous-pages Banks / ARPEG / LOOP) + Tool 4 ext refus ControlPad sur LOOP control. **Retirer le Phase 2 dev seed conditionnel `applyDevSeedLoopPadsIfSafe()` du NvsManager** (Tool 3 b1 livre l'UI propre).
- **Phase 4** : PotRouter 3 contexts + Tool 7 page LOOP + LED wiring complet (EVENT_RENDER_DEFAULT pour EVT_LOOP_*) + différenciation `tickBar` vs `tickWrap` durations dans renderBankLoop (Phase 2 utilise tickBarDurationMs pour les deux).
- **Phase 5** : Effets shuffle/chaos/velPattern + câblage `LoopPotStore` runtime → `LoopEngine`.
- **Phase 6** : Slot Drive LittleFS — partition flash 512 KB, `LoopSlotStore`, save/load/delete + WAITING_LOAD state.

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-05-18-loop-phase-2-plan.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — Dispatch un fresh subagent par task, review entre tasks, itération rapide. REQUIRED SUB-SKILL : `superpowers:subagent-driven-development`.

**2. Inline Execution** — Exécuter les tasks dans cette session via `superpowers:executing-plans`, batch avec checkpoints HW. À privilégier si tu veux garder le contexte vivant entre les sous-phases et trancher les surprises au fil de l'eau.

Vu la nature embedded (HW gates G1-G9 à exécuter physiquement entre les phases) et le besoin d'arbitrage live sur les choix musicaux (bar-snap deadzone, FG/BG color overlays, etc.), **option 2 (Inline Execution)** est probablement plus adaptée — les subagents ne peuvent pas observer le HW. À toi de trancher.
