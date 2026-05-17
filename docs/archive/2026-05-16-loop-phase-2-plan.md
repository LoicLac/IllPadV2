# LOOP Phase 2 — LoopEngine + main wiring — Plan d'implémentation

> **🛑 PRE-LECTURE OBLIGATOIRE — Garde-fous post-fixes ARPEG mai 2026**
>
> Avant la première ligne de `LoopEngine.cpp`, lire **impérativement** :
> - [`docs/superpowers/specs/2026-04-26-gesture-dispatcher-design.md`](../specs/2026-04-26-gesture-dispatcher-design.md) **Parties 8 et 9** — invariant "buffer LOOP sacré" (§27), mapping anti-patterns ARPEG→LOOP à NE PAS reproduire (§28), 6 actions explicites qui modifient le buffer (§29), checklist §32 6 points à valider point par point avant de commencer.
> - [`docs/superpowers/LOOP_ROADMAP.md`](../LOOP_ROADMAP.md) §4 (Phase 1 close + arc gesture-dispatcher abandonné + Q1-Q8 actées) et §5 (P1-P6 — résolues en session 1 conception, voir §0 ci-dessous).
> - [`docs/superpowers/plans/2026-04-21-loop-phase-1-plan.md`](2026-04-21-loop-phase-1-plan.md) — pour contexte Phase 1 (5 commits mergés sur main : `a84c955`, `1b0ac8c`, `68855e3`, `48b96fb`, `8c0d68b`, `c3d04ac`).
>
> **Règle d'or** : le **code main au commit courant est la source de vérité**, PAS les snippets de l'archive `loop-archive-2026-05-16` → `b79d03b`. Les snippets de cette archive sont une **référence d'intention** ; pour chaque cible, vérifier line number et signature dans le code main avant édition.

> **For agentic workers:** REQUIRED SUB-SKILL — Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** Premier son MIDI depuis une bank LOOP. Record un motif via 3 pads de contrôle (REC / PLAY-STOP / CLEAR), bar-snap au stopRecording, playback proportionnel BPM, overdub merge, clear long-press 500 ms. Aucune UI setup (Phase 3), aucun rendu LED LOOP runtime (Phase 4), aucun effet (Phase 5), aucun slot drive (Phase 6). Test config hardcodée (pads 47/46/45, bank 7 forcé en BANK_LOOP).

**Architecture livrée** : nouveau dossier `src/loop/` avec `LoopEngine.h`/`.cpp`/`LoopTestConfig.h`. Wiring `main.cpp` : 3 nouvelles fonctions (`processLoopMode`, `handleLoopControls`, `pushParamsToLoop`), tick + processEvents par bank en background dans `loop()`, branche `case BANK_LOOP` dans `handlePadInput`, branche `else if (relSlot.type == BANK_LOOP)` dans `handleLeftReleaseCleanup`. `BankManager` activate guards (recording lock silent deny + flushLiveNotes outgoing). Task 4 LOOP P1 (caduc gesture-dispatcher) câblée Step 0 : BankManager double-tap LOOP consume + ScaleManager early-return BANK_LOOP.

**Tech Stack :** C++17 / PlatformIO / ESP32-S3 / FreeRTOS dual-core. Zero Migration Policy (CLAUDE.md projet). Pas de unit tests — vérification = `pio run` (compile gate) + static read-back grep + HW Checkpoints B / C / D bloquants gated par autorisation user (no auto-upload).

**Sources de référence :**
- Spec LOOP (V) : [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](../specs/2026-04-19-loop-mode-design.md) §3 (LOOP core), §7 (record), §8 (overdub), §9 (play/stop/clear), §14 (multi-banks), §17 (quantization), §22 (pot routing), §23 invariants 1-11, §27 Phase 2, §28 Q1-Q8.
- Spec gesture-dispatcher : [`docs/superpowers/specs/2026-04-26-gesture-dispatcher-design.md`](../specs/2026-04-26-gesture-dispatcher-design.md) §27 (buffer LOOP sacré), §28 (anti-patterns), §29 (6 actions), §30 (toggle global multi-bank), §31 (4 décisions), §32 (checklist 6 points).
- Roadmap LOOP : [`docs/superpowers/LOOP_ROADMAP.md`](../LOOP_ROADMAP.md) §1 (stratégie), §2 (sources), §4 (insights Phase 1 close), §5 (P1-P6 résolues session 1).
- Plan Phase 1 LOOP (clos) : [`docs/superpowers/plans/2026-04-21-loop-phase-1-plan.md`](2026-04-21-loop-phase-1-plan.md).
- Archive plan Phase 2 (référence intention) : `git show loop-archive-2026-05-16:docs/plans/loop-phase2-engine-wiring.md`.
- Archive LoopEngine implémentation référentielle : `git show loop-archive-2026-05-16:src/loop/LoopEngine.{h,cpp}`.
- Archive handoff Phase 1→2 (AUDIT FIXES catalog) : `git show loop-archive-2026-05-16:docs/superpowers/handoff/phase1-to-phase2.md`.
- Briefing architecture : [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) §0 scope triage, §3 task index, §8 domain entry points.
- NVS reference : [`docs/reference/nvs-reference.md`](../../reference/nvs-reference.md).
- Patterns catalog : [`docs/reference/patterns-catalog.md`](../../reference/patterns-catalog.md) — refcount (P3), per-pad live tracking (mirror MidiEngine `_lastResolvedNote`).
- CLAUDE.md projet — § "Invariants" (1 no orphan notes, 4 Core 0 never writes MIDI, 5 catch system, 6 banks always alive, 7 Setup/Runtime coherence), § "Performance Budget — Budget Philosophy" (prefer safe over economical on SRAM/PSRAM).
- CLAUDE.md user — git workflow autocommit, actions destructives, lire-avant-de-proposer.

---

## §0 — Décisions pré-actées

### §0.1 — Décisions Q1-Q8 spec LOOP §28 (rappel)

Ces décisions encadrent toutes les Phases 1-6. Pour Phase 2, les pertinentes :

| # | Décision | Impact Phase 2 |
|---|---|---|
| Q2 | `PendingEvent` dupliqué (LoopEngine vs ArpEngine) | LoopEngine définit sa propre `PendingNote` + `_pending[MAX_PENDING=48]`. Pas de factorisation préparatoire. |
| Q5 | STOPPED-loaded + tap REC = PLAYING + OVERDUBBING simultanés | Documenter dans state machine LoopEngine (Task 2 commentaire + Task 4 transition note). Mais Phase 2 n'expose **pas** ce chemin (pas de slot load avant Phase 6) — le code ne déclenche jamais `startOverdub` depuis STOPPED en Phase 2 ; la spec est notée pour traçabilité. |
| Q8 | Max 1 bank LOOP en REC/OD à un instant t (invariant 11 §23) | Conséquence combinée des invariants 2 + §18. LoopEngine `isRecording()` retourne `true` pour RECORDING+OVERDUBBING ; BankManager `switchToBank` silent deny utilise ce flag. Aucun code défensif "et si 2 banks en REC" — par construction impossible. |

### §0.2 — Décisions P1-P6 roadmap LOOP §5 (résolues session 1)

| # | Sujet | Décision actée session 1 |
|---|---|---|
| **P1** | Découpage commits Phase 2 | **10-11 commits** groupés par responsabilité. Task 0 (Task 4 LOOP P1) en commit propre. LoopEngine.cpp en 4 chunks (Tasks 3-4-5-6) — fusionner Tasks 5+6 en 1 commit si on veut rester à 10. |
| **P2** | Task 4 LOOP P1 placement | **Commit (0) séparé** — `feat(loop): wire Task 4 LOOP P1 — BankManager double-tap LOOP consume + ScaleManager early-return BANK_LOOP`. Isolable, bisectable, message explicite "porter Task 4 caduc gesture-dispatcher". |
| **P3** | HW Checkpoints A/B/C/D | **3 checkpoints B / C / D** (skip A — Task 0 purement défensif sans observable LOOP runtime). HW-B après Task 7 (boot + LoopEngine allocué bank 7, stub renderBankLoop visible). HW-C après Task 9 (premier son MIDI complet, 13 sub-tests). HW-D après Task 10 (recording lock + flushLiveNotes outgoing). |
| **P4** | Interface publique LoopEngine | **Interface complète archive** avec stubs Phase 5 (setShuffle/setChaos/setVelPattern/setBaseVelocity/setVelocityVariation + getters miroirs). Per-pad live tracking (`_liveNote[NUM_KEYS]` + `setLiveNote`/`releaseLivePad`) inclus (AUDIT B1). API stabilisée dès Phase 2 — Phases 3-6 ne bougent plus le header. preBankSwitch hook **différé** Phase 6/7. |
| **P5** | `loopQuantize` storage layout | **Différé session 2 (Phase 3 conception)** — choix entre BankTypeStore v4→v5 (`loopQuantize[NUM_BANKS]`) vs Store dédié `LoopBankConfigStore`. Phase 2 : `NvsManager::getLoadedLoopQuantizeMode(bank)` retourne **toujours `LOOP_QUANT_FREE`** (stub). Test config Phase 2 hardcode `LOOP_QUANT_FREE` pour bank 7. Phase 3 câblera la vraie lecture après tranchage P5. |
| **P6** | 4 décisions §31 spec gesture | **#1 acté** : layer musical LOOP indépendant (option A spec). Justification : `handleLoopControls` early-return si `slot.type != BANK_LOOP`, et auto-Play §13.2 ARPEG vit dans `processArpMode` qui n'est jamais appelé sur bank LOOP → orthogonalité par construction, aucun code Phase 2. **#2 (slot save annulable), #3 (CLEAR tenu sans slot), #4 (preBankSwitch hook signature) : différés Phase 6/7** — pas de WAITING_LOAD ni de slot drive en Phase 2. |

### §0.3 — Décisions architecture cumulatives main 2026-05-16

- **`MAX_LOOP_BANKS = 4`** (roadmap Q1) — 4 LoopEngine × ~10.3 KB ≈ **~41 KB SRAM** (recalcul agent audit : PendingNote 16 B padded). Marge confortable sur 320 KB. Spec §25 mentionnait 2 banks (~18.8 KB) ; on monte à 4 par cohérence avec `s_arpEngines[4]` et pour permettre tous les scenarios "combinaisons typiques" §14. Figé par `static_assert(sizeof(LoopEngine) <= 11000, "..."` dans LoopEngine.h Task 2.
- **Pads LOOP test 47/46/45** (roadmap Q3) — validés HW non-conflictuels avec drumming musical (vs 30/31/32 problématiques sur le layout actuel).
- **Pas de refonte gesture-dispatcher** (roadmap §4) — `BankManager::update()` + `ScaleManager::processScalePads()` + `handleLoopControls()` restent autonomes en main.cpp / managers. Task 0 câble Task 4 LOOP P1 directement dans ces fichiers (caduc "porté par dispatcher").
- **§30 spec gesture `toggleAllArpsAndLoops()`** — extension multi-bank du toggle global LEFT+hold pad pour inclure LOOP : **différée Phase 4** (besoin LED feedback multi-bank pour signaler quelles banks ont basculé). Phase 2 garde `toggleAllArps()` inchangée (ARPEG seulement).
- **Step 11 midiPanic LOOP** — **différé Phase 4+** (roadmap Q5). Trivial (~10 lignes), pas bloquant Phase 2. Documenté en §5 risques.
- **WAITING_* state machine spec §17 — modèle hybride `WAITING_PENDING`** (audit B2 acté post-rédaction) : ajouter au `State` enum un seul état générique `WAITING_PENDING = 5` plutôt qu'un état par PendingAction (économise 5+ valeurs enum + complexité state machine). `getState()` retourne `WAITING_PENDING` quand `_pendingAction != PENDING_NONE`, sinon `_state` brut. `handleLoopControls` gère 3 cas concurrents §17 : (a) tap PLAY/STOP pendant `WAITING_PENDING` annule la pending action sans set une nouvelle, (b) tap REC pendant `WAITING_PENDING` avec `_pendingAction == PENDING_STOP` convertit en OVERDUBBING (commit immédiat du STOP latent + entry OVERDUBBING), (c) bank switch pendant `WAITING_PENDING` commit immédiat la pending action (Task 10 — Phase 2 implémente simple silent-deny si _state==REC/OD, le commit-then-switch sera ajouté Phase 3 quand BEAT/BAR seront câblés). Phase 2 hardcode `LOOP_QUANT_FREE` → aucun `WAITING_PENDING` traversé en runtime, donc test code-correctness only (review code). **Bénéfice : interface vraiment stable Phases 3-6 — le `State` enum ne bumpera plus.**
- **CLEAR sémantique pendant OVERDUBBING — divergence vs spec §9 strict, acceptée** (audit M1 acté post-rédaction) : `CLEAR` 500ms hold pendant `OVERDUBBING` = `cancelOverdub()` (undo overdub pass, loop préservée). `CLEAR` 500ms hold sinon = `clear()` vers `EMPTY` (spec §9). Justification musicale : "j'ai foiré mon overdub mais je veux garder ma boucle" est un cas musicien important — l'archive l'implémente, c'est utile. Divergence vs spec §9 documentée. **TODO** : amender spec LOOP §9 dans une future revision pour expliciter le comportement hybride.
- **`abortOverdub` (PLAY/STOP pendant OVERDUBBING) → PLAYING, pas STOPPED** (audit N5 acté post-rédaction) : alignement avec spec §8 ("l'engine reste en PLAYING"). Pas de `flushActiveNotes` au abort (loop continue audible). Pour stopper, user re-tap PLAY/STOP (transition PLAYING → STOPPED normale). Sémantique "abort overdub" est distincte de "stop loop" — 2 gestes séparés.

### §0.4 — Invariants à préserver Phase 2 (CLAUDE.md projet + spec gesture)

1. **No orphan notes** : `noteOff` ALWAYS via refcount transition 1→0. `releaseLivePad` clear `_liveNote[pad]` **avant** noteRefDecrement (re-entry safety). `flushActiveNotes(hard=false)` au `doStop()` (soft flush, trailing pending OK). `flushActiveNotes(hard=true)` au `clear()` (vide tout).
2. **No blocking on Core 1 musical path** : LoopEngine tick + processEvents s'exécutent dans `loop()` Core 1 mais sans NVS write. Pas de blocking.
3. **Core 0 never writes MIDI** : LoopEngine est instancié Core 1 only. Toutes les `transport.sendNoteOn` viennent de tick / processEvents / handleLoopControls qui tournent Core 1.
4. **No new/delete runtime** : `s_loopEngines[MAX_LOOP_BANKS=4]` static array. Tous les buffers internes (`_events[1024]`, `_overdubBuf[128]`, `_pending[48]`, `_noteRefCount[128]`, `_liveNote[NUM_KEYS=48]`, `_overdubActivePads[48]`) static members.
5. **Bank slots always alive** : tous les 4 LoopEngine sont allocués au boot et tick + processEvents même en background — un loop BG continue à émettre MIDI quand sa bank n'est pas FG.
6. **Buffer LOOP sacré (spec gesture §27)** : aucune transition de transport (play/stop, bank switch, hold pad, toggle global, sweep release LEFT) ne modifie `_events[]`. Modifications uniquement via les 6 actions §29 : REC press, REC tap pendant OVERDUBBING (revert), CLEAR long-press (wipe), load slot (replace, Phase 6), PLAY/STOP double-tap bypass (préserve buffer), WAITING_LOAD + bank switch commit (Phase 6).
7. **Pas de scale sur bank LOOP (invariant 6 §23 spec LOOP)** : `ScaleManager::processScalePads` early-return si `slot.type == BANK_LOOP` (Task 0 Step 0.2).
8. **Pas de live remove sur falling edge (anti-pattern F8 spec gesture)** : `processLoopMode` au release ne fait QUE `releaseLivePad` + `recordNoteOff` si REC/OD. **Jamais** de `removeEvent` ou équivalent destructif sur `_events[]`.

### §0.5 — Checklist §32 spec gesture — à valider avant Task 2 (LoopEngine.h)

Cette checklist est **destinée au reviewer du plan**. Toutes les boîtes doivent être vertes avant l'écriture de la première ligne de `LoopEngine.cpp`.

- [x] Le plan ne contient aucun chemin appelant un équivalent de `clearAllNotes()` sur `_events[]` hors des 6 actions §29 — vérifié sur snippets archive Task 4, 5, 6 ci-dessous.
- [x] Aucun "live remove" / "sweep des events non pressés" sur falling edge n'apparaît dans `processLoopMode` ni `handleLeftReleaseCleanup` — vérifié Task 8 snippet.
- [x] `LoopEngine::stop()` est **idempotente sur le contenu du buffer** (audit N2 fix : pas idempotente sur la phase de lecture — `doPlay` reset `_cursorIdx = 0` + `_playStartUs = micros()`, donc play après stop restart au top du loop, pas reprise à la position courante). Le buffer `_events[]` reste intact, seule la phase change. Vérifié Task 4 + 5.
- [x] Toute interaction LEFT (press/release/held) ne touche pas le buffer — `handleLeftReleaseCleanup` LOOP branch utilise `releaseLivePad` idempotent. Vérifié Task 8.
- [ ] Le toggle global `(HOLD_PAD, leftHeld=true)` étendu pour LOOP via `toggleAllArpsAndLoops()` — **DIFFÉRÉ Phase 4** (acté §0.3). Documenter dans §5 risques que `toggleAllArps()` actuel n'inclut pas les LOOP banks Phase 2.
- [x] Invariants §25 spec gesture (1-5) préservés ou explicitement remplacés : §0.4 invariants 1-8 ci-dessus couvrent les équivalents LOOP.

---

## §1 — File structure overview

Fichiers touchés par Task. **Aucun fichier de Phase 1 LOOP existante n'est modifié** au-delà de ce que cette table liste. Pas de bump NVS sur stores existants.

| Fichier | Tasks | Rôle Phase 2 |
|---|---|---|
| `src/managers/BankManager.cpp` | 0, 10 | T0: ajouter branche `else if (_banks[b].type == BANK_LOOP)` dans le double-tap detection (ligne ~92, consume silently). T10: activate recording lock (early `return` si `loopEngine->isRecording()`) + flushLiveNotes outgoing dans `switchToBank` (ligne ~188-200). |
| `src/managers/ScaleManager.cpp` | 0 | T0: ajouter `if (slot.type == BANK_LOOP) return;` en première instruction de `processScalePads` (ligne ~114). |
| `src/core/HardwareConfig.h` | 1 | T1: ajouter `MAX_LOOP_BANKS = 4`, `LOOP_NOTE_OFFSET = 36`, `LoopQuantMode` enum + `NUM_LOOP_QUANT_MODES`, `DEFAULT_LOOP_QUANT_MODE = LOOP_QUANT_FREE`. (Constantes engine `MAX_LOOP_EVENTS / MAX_OVERDUB_EVENTS / MAX_PENDING / TICKS_PER_BEAT / TICKS_PER_BAR` restent file-scope dans `LoopEngine.h`.) |
| `src/managers/NvsManager.h` | 1 | T1: ajouter `uint8_t _loadedLoopQuantize[NUM_BANKS]` private member + `uint8_t getLoadedLoopQuantizeMode(uint8_t bank) const` public getter (stub Phase 2). |
| `src/managers/NvsManager.cpp` | 1 | T1: dans le ctor (ou en static init), `memset(_loadedLoopQuantize, DEFAULT_LOOP_QUANT_MODE, sizeof(_loadedLoopQuantize))`. Body du getter. Pas de modification de `loadAll()` Phase 2 (P5 différé). |
| `src/core/KeyboardData.h` | 2 | T2: forward decl `class LoopEngine;` + ajouter `LoopEngine* loopEngine = nullptr;` au struct `BankSlot` (à côté de `ArpEngine* arpEngine`). |
| `src/loop/LoopEngine.h` | 2 | T2: nouveau fichier. State enum, PendingAction enum, LoopEvent + PendingNote structs, classe LoopEngine — interface complète (P4 reco). |
| `src/loop/LoopEngine.cpp` | 3, 4, 5, 6 | T3: ctor + begin + clear + setPadOrder + setLoopQuantizeMode + setChannel + padToNote + refcount helpers + calcLoopDurationUs + effect stubs Phase 5. T4: public transitions (startRecording…cancelOverdub) + private `do*` (doStartRecording…doStop). T5: `tick()` + `processEvents()` + `schedulePending` + `flushActiveNotes` + `flushLiveNotes` + `setLiveNote` + `releaseLivePad`. T6: `recordNoteOn` + `recordNoteOff` + `sortEvents` + `mergeOverdub` + tous les getters (state + flash flags + effect getters). |
| `src/loop/LoopTestConfig.h` | 7 | T7: nouveau fichier. `LOOP_TEST_ENABLED 1`, `LOOP_TEST_BANK 6` (bank 7 visible musicien = index 6 zero-based, channel 7), `LOOP_TEST_REC_PAD 47`, `LOOP_TEST_PLAYSTOP_PAD 46`, `LOOP_TEST_CLEAR_PAD 45`. À retirer Phase 3. |
| `src/main.cpp` | 7, 8, 9 | T7: includes + `static LoopEngine s_loopEngines[MAX_LOOP_BANKS]` + statics LOOP control pads + init `s_banks[i].loopEngine = nullptr` (ligne ~552) + `setup()` assignment loop (post NVS loadAll) + test config override + memset s_loopSlotPads 0xFF. T8: `processLoopMode()` + `case BANK_LOOP:` dans `handlePadInput` (ligne ~973) + branche `else if (relSlot.type == BANK_LOOP)` dans `handleLeftReleaseCleanup` (ligne ~946). T9: `handleLoopControls()` + appel dans `loop()` **entre `s_controlPadManager.update` (ligne 1510-1511) et `handlePadInput` (ligne 1513)** (audit B1 fix : il n'y a pas de `handlePlayStopPad` dans le code main, ordre réel `loop()` est `handleManagerUpdates → handleHoldPad → s_controlPadManager.update → handlePadInput`) + tick/processEvents par bank LOOP dans `loop()` (entre `s_arpScheduler.processEvents()` ligne 1522 et `s_midiEngine.flush()` ligne 1525) + branche LOOP dans `reloadPerBankParams` (ligne ~990) + `pushParamsToLoop()` + appel après `pushParamsToEngine` (ligne ~1184). |
| `docs/reference/architecture-briefing.md` | 0, 7, 9 | Sync §8 domain entry points (LoopEngine ajouté), §4 table 1 (BankTypeStore loopQuantize statut), §9 invariants (refs aux invariants §0.4 ci-dessus). |
| `docs/reference/nvs-reference.md` | 1 | Sync `LoopPotStore` ligne 118 : statut reste "DECLARED Phase 1, consumed Phase 5" (Phase 2 ne câble pas non plus). Pas de modification structurelle. |

**Pas touché en Phase 2** : `src/core/CapacitiveKeyboard.{h,cpp}`, `src/core/LedController.{h,cpp}` (renderBankLoop reste stub Phase 1), `src/setup/Tool*.{h,cpp}` (Phase 3), `src/managers/PotRouter.{h,cpp}` (Phase 4), `src/managers/ControlPadManager.{h,cpp}`, `src/midi/MidiEngine.cpp`, `src/core/MidiTransport.cpp`, `platformio.ini` (pas de lib_deps nouvelle).

---

## §2 — Graphe dépendances inter-tasks

```
Task 0  Task 4 LOOP P1 portée (BankManager + ScaleManager guards)
   │    [defensive, indépendant runtime LOOP, ne touche pas LoopEngine]
   │
Task 1  HardwareConfig LOOP constants + NvsManager quantize stub
   │    [prereq : MAX_LOOP_BANKS / LOOP_NOTE_OFFSET / LoopQuantMode visibles avant LoopEngine.h]
   │
Task 2  LoopEngine.h skeleton + BankSlot.loopEngine member
   │    [prereq Task 1 : constants visibles ; forward decl LoopEngine pour BankSlot]
   │
Task 3  LoopEngine.cpp ctor + helpers
   │    [prereq Task 2 : header complet]
   │
Task 4  LoopEngine.cpp transitions + doXxx privates
   │    [prereq Task 3 : ctor + helpers callables]
   │
Task 5  LoopEngine.cpp tick + processEvents + sched + flush + live tracking
   │    [prereq Task 4 : doXxx privates callables depuis tick dispatcher]
   │
Task 6  LoopEngine.cpp record + sort + merge + getters
   │    [prereq Task 5 : tick écrit _lastPositionUs lu par recordNoteOn/Off (B2 fix)]
   │
Task 7  LoopTestConfig.h + main statics + setup() assignment
   │    [prereq Task 6 : LoopEngine complet et linkable]
   │    [HW Checkpoint B]
   │
Task 8  main processLoopMode + handlePadInput case + handleLeftReleaseCleanup LOOP
   │    [prereq Task 7 : s_loopEngines allocué, s_recPad/s_loopPlayPad/s_clearPad seedés]
   │
Task 9  main handleLoopControls + loop() wiring + reloadPerBankParams + pushParamsToLoop
   │    [prereq Task 8 : processLoopMode produit les recordNoteOn/Off]
   │    [HW Checkpoint C — premier son MIDI LOOP]
   │
Task 10 BankManager activate guards (recording lock + flushLiveNotes outgoing)
        [prereq Task 6 : LoopEngine::isRecording() + flushLiveNotes signatures stables]
        [HW Checkpoint D]
```

**Ordre commits = ordre tasks** (0 → 10). Chaque task atomique en compile + lint + read-back. Aucune task n'introduit de régression observable musicien-facing **avant Task 7** (LoopTestConfig override actif).

**Fusion possible 10 commits** : si on veut rester strictement à 10 commits (P1 reco), fusionner **Task 5 + Task 6 en un seul commit** (volume LoopEngine.cpp ~500 LOC en un commit). Acceptable car les deux tasks touchent le même fichier sans dépendance externe. À trancher à l'exécution selon la lisibilité du diff.

---

## §3 — Conventions de vérification firmware

Pas de framework de tests automatisés. **5 gates par Task** (cf CLAUDE.md user "Workflow d'implémentation — 5 gates par task") :

1. **Code** — Read fichier cible intégral avant édition. Edit (jamais Write si fichier existe sauf création nouveau). Multi-fichiers : lire tous avant le premier edit.
2. **Build (compile gate)** : `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`. Exit 0, 0 nouveau warning (`-Wswitch`, `-Wunused-variable`, `-Wparentheses` clean). Bloque tant que rouge.
3. **Auto-review (static read-back)** : grep des symboles modifiés dans tous les fichiers consommateurs. Vérifier couverture call-sites. Cf chaque Task — section "Static read-back" liste les greps attendus.
4. **HW gate (si applicable)** : présenter au user les points HW à valider, attendre OK explicite. **Compile passé ≠ ça marche** ; le HW est seul juge final. HW Checkpoints B / C / D bloquants — sections dédiées dans Tasks 7, 9, 10. Pour les Tasks sans HW gate (0, 1, 2, 3, 4, 5, 6, 8), le compile gate + read-back suffisent.
5. **Commit gate** : proposer fichiers + message HEREDOC, attendre OK user. Commits groupés en fin de Task, jamais par sub-Step individuel. Mode `autocommit` actif par défaut (cf `~/.claude/CLAUDE.md`) — commit auto au point de bascule mais **`git push` reste explicite**.

**Règle absolue** : HW gate AVANT commit gate, jamais après. Pour les Tasks 7, 9, 10 — HW Checkpoint **bloque le commit** tant que le user n'a pas validé en HW.

**Recap-table multi-axes** à maintenir pendant l'exécution :

| Task | Compile | HW | Commit |
|---|---|---|---|
| 0 | | n/a | |
| 1 | | n/a | |
| 2 | | n/a | |
| 3 | | n/a | |
| 4 | | n/a | |
| 5 | | n/a | |
| 6 | | n/a | |
| 7 | | HW-B | |
| 8 | | n/a (préparatoire HW-C) | |
| 9 | | **HW-C** | |
| 10 | | **HW-D** | |

---

## Task 0 — Wire Task 4 LOOP P1 (BankManager + ScaleManager defensive guards)

**Cross-refs** : spec LOOP §19 (LEFT+double-tap LOOP étendu) + §23 invariant 6 (no scale on LOOP). Spec gesture §28 (anti-patterns) + §32 checklist box 4 (LEFT interactions ne touchent pas le buffer). Roadmap §4 "Task 4 LOOP P1 reste à câbler — confirmé prérequis défensif Phase 2". Plan Phase 1 LOOP commit `BankManager.cpp:92` commentaire `// LOOP : double-tap handler à câbler par plan LOOP Phase 1 (else if BANK_LOOP).`.

**Files** :
- Modify : [`src/managers/BankManager.cpp`](../../../src/managers/BankManager.cpp:91-106) — ajouter branche `else if (_banks[b].type == BANK_LOOP)` dans la double-tap detection.
- Modify : [`src/managers/ScaleManager.cpp`](../../../src/managers/ScaleManager.cpp:114) — ajouter early-return `if (slot.type == BANK_LOOP) return;` en première instruction de `processScalePads`.

**Cible diff `BankManager.cpp` ligne 91-106** :

```cpp
      // --- Double-tap on ARPEG/ARPEG_GEN bank pad = Play/Stop toggle ---
      // Same event chain as hold pad on FG; BG banks pass keys=nullptr
      // (no fingers possible off-foreground → pile kept, paused).
      // Rule: double-tap NEVER changes bank. Always consume the 2nd tap.
      if (wasRecent && isArpType(_banks[b].type)) {
        if (_banks[b].arpEngine && _transport) {
          bool wasCaptured = _banks[b].arpEngine->isCaptured();
          const uint8_t* keys = (b == _currentBank) ? keyIsPressed : nullptr;
          _banks[b].arpEngine->setCaptured(!wasCaptured, *_transport, keys, _holdPad);
          if (_leds) {
            EventId evt = _banks[b].arpEngine->isCaptured() ? EVT_PLAY : EVT_STOP;
            _leds->triggerEvent(evt, (uint8_t)(1 << b));
          }
        }
        _lastBankPadPressTime[b] = 0;
        _pendingSwitchBank = -1;
        continue;  // never fall through — 2nd tap on ARPEG/ARPEG_GEN is always consumed
      }

      // --- Double-tap on LOOP bank pad = Play/Stop toggle (spec LOOP §19) ---
      // Phase 2 : consume the 2nd tap silently — LoopEngine.toggle() à câbler
      // dans une session ultérieure (le toggle PLAY/STOP LOOP est aussi disponible
      // via le pad de contrôle PLAY/STOP du layer musical, cf handleLoopControls).
      // Le silent consume empêche un bank switch parasite sur double-tap LOOP.
      else if (wasRecent && _banks[b].type == BANK_LOOP) {
        _lastBankPadPressTime[b] = 0;
        _pendingSwitchBank = -1;
        continue;  // never fall through — 2nd tap on LOOP is always consumed
      }
```

**Cible diff `ScaleManager.cpp` ligne 114** :

```cpp
void ScaleManager::processScalePads(const uint8_t* keyIsPressed, BankSlot& slot) {

  // Spec LOOP §23 invariant 6 : pas de scale sur une bank LOOP.
  // Mutation gratuite de slot.scale.* + déclenchement confirm LED + NVS write
  // inutile sans cette garde. Aligne aussi avec spec gesture §27 (buffer LOOP
  // sacré : LEFT interactions ne modifient pas le state musical de la bank LOOP).
  if (slot.type == BANK_LOOP) return;

  // --- Root pads (0-6 → A,B,C,D,E,F,G) --- (code existant, inchangé)
  for (uint8_t r = 0; r < 7; r++) {
    // ... existing body ...
```

**Pourquoi un commit séparé (P2 acté)** : Task 4 LOOP P1 est purement défensif — aucune dépendance sur LoopEngine runtime. Isolable, bisectable, message clair. Le silent consume du double-tap LOOP **n'a aucun effet visible** tant qu'aucune bank n'est de type LOOP (impossible avant Task 7 LoopTestConfig override). C'est uniquement à partir de HW Checkpoint B que le silent consume devient observable (double-tap sur bank 7 ne déclenche ni bank switch ni autre comportement parasite).

**Steps** :

- [ ] **Step 0.1 — Lecture `BankManager.cpp` lignes 75-127** : confirmer le double-tap detection block + `_lastBankPadPressTime[b] = 0; _pendingSwitchBank = -1; continue;` pattern utilisé par la branche ARPEG.

- [ ] **Step 0.2 — Lecture `ScaleManager.cpp` lignes 110-180** : confirmer signature `processScalePads(const uint8_t* keyIsPressed, BankSlot& slot)` + early-return possible avant la première itération root pads.

- [ ] **Step 0.3 — Édition `BankManager.cpp` après la branche `if (wasRecent && isArpType(...))`** : insérer la branche `else if (wasRecent && _banks[b].type == BANK_LOOP)` (cf snippet ci-dessus). 5 lignes ajoutées + commentaires explicatifs (~12 lignes commentaires).

- [ ] **Step 0.4 — Édition `ScaleManager.cpp` ligne 114** : insérer `if (slot.type == BANK_LOOP) return;` en première instruction de `processScalePads`. 1 ligne + 3 lignes commentaires.

- [ ] **Step 0.5 — Compile gate** : `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`. Exit 0, no warning new.

- [ ] **Step 0.6 — Static read-back** :
  - `grep -n "BANK_LOOP" src/managers/BankManager.cpp src/managers/ScaleManager.cpp` → confirmer les 2 nouvelles occurrences (1 dans chaque fichier).
  - `grep -n "isArpType\|BANK_LOOP\|BANK_ARPEG_GEN" src/managers/BankManager.cpp` → confirmer que la branche LOOP est bien APRÈS la branche `isArpType()` (else if, pas avant). Sinon on intercepte les ARPEG.

- [ ] **Step 0.7 — Mise à jour briefing (sync requirement)** :
  - [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) §2 "Bank Switch (all side effects in order)" : ajouter note "LOOP banks : double-tap consume silently Phase 2, LoopEngine.toggle() à câbler Phase 4+ (LED multi-bank feedback)".
  - §9 invariants : confirmer invariant 6 spec LOOP listé (no scale on LOOP).

- [ ] **Step 0.8 — Commit (mode autocommit on)** :
  - Files : `src/managers/BankManager.cpp`, `src/managers/ScaleManager.cpp`, `docs/reference/architecture-briefing.md`.
  - Message proposé :
    ```
    feat(loop): phase 2 task 0 — wire Task 4 LOOP P1 (caduc gesture-dispatcher)

    BankManager::update double-tap branch on BANK_LOOP : consume the 2nd tap
    silently (zeros _lastBankPadPressTime[b] + clears _pendingSwitchBank, then
    continue) — prevents bank-switch parasite. LoopEngine.toggle() à câbler
    plus tard (PLAY/STOP LOOP est aussi accessible via le control pad du layer
    musical, cf handleLoopControls Phase 2 Task 9).

    ScaleManager::processScalePads early-return if slot.type == BANK_LOOP —
    invariant 6 §23 spec LOOP (no scale on LOOP). Évite mutation gratuite de
    slot.scale + déclenchement confirm LED + NVS write inutile.

    Refs : spec LOOP §19, §23 inv 6. Spec gesture §27, §32 box 4. Roadmap §4
    "Task 4 LOOP P1 reste à câbler — confirmé prérequis défensif Phase 2".
    Commit BankManager.cpp:92 commentaire stale supprimé.
    ```

---

## Task 1 — HardwareConfig LOOP constants + NvsManager loop quantize helper stub

**Cross-refs** : spec LOOP §17 (quantize FREE/BEAT/BAR), §28 P5 différé (loopQuantize storage Phase 3). Archive `HardwareConfig.h` Phase 1 loop branch (référence intention).

**Files** :
- Modify : [`src/core/HardwareConfig.h`](../../../src/core/HardwareConfig.h) — ajouter constantes LOOP runtime (engine constants restent file-scope dans LoopEngine.h Task 2).
- Modify : [`src/managers/NvsManager.h`](../../../src/managers/NvsManager.h) — ajouter `_loadedLoopQuantize[NUM_BANKS]` private + `getLoadedLoopQuantizeMode(bank)` public.
- Modify : [`src/managers/NvsManager.cpp`](../../../src/managers/NvsManager.cpp) — ctor init `memset(_loadedLoopQuantize, DEFAULT_LOOP_QUANT_MODE, sizeof(_loadedLoopQuantize))` + getter body.

**Cible diff `HardwareConfig.h`** (zone : section LOOP à créer en fin de fichier ou près des autres MAX_*_BANKS) :

```cpp
// =================================================================
// LOOP Mode (Phase 2+) — runtime constants
// =================================================================

// Nombre maximum de banks LOOP simultanées (chacune ≈ 9.4 KB SRAM).
// 4 × 9.4 KB ≈ 37.6 KB sur 320 KB total — marge confortable (cf Budget
// Philosophy CLAUDE.md projet).
#define MAX_LOOP_BANKS  4

// GM kick drum offset : padOrder 0 → MIDI note 36 (C2). Permet de jouer
// les samples drum kit standard sans configuration.
#define LOOP_NOTE_OFFSET  36

// Quantize mode per-bank LOOP — applique à start/stop record, play, stop
// (spec LOOP §17). NVS storage différé Phase 3 (P5 roadmap §5).
enum LoopQuantMode : uint8_t {
  LOOP_QUANT_FREE = 0,   // pas de snap — action fire à la microseconde du tap
  LOOP_QUANT_BEAT = 1,   // attend la prochaine 1/4 note (24 ticks PPQN)
  LOOP_QUANT_BAR  = 2    // attend la prochaine mesure (96 ticks, 4/4)
};
#define NUM_LOOP_QUANT_MODES   3
#define DEFAULT_LOOP_QUANT_MODE  LOOP_QUANT_FREE   // Phase 2 test config
```

**Cible diff `NvsManager.h`** (ajout dans la section private + public access getter, près de `_loadedQuantize[NUM_BANKS]` existant pour ARPEG) :

```cpp
// Dans section public, près de getLoadedQuantizeMode :
uint8_t getLoadedLoopQuantizeMode(uint8_t bank) const;

// Dans section private, près de _loadedQuantize :
uint8_t _loadedLoopQuantize[NUM_BANKS];   // stub Phase 2 : toujours DEFAULT_LOOP_QUANT_MODE
                                          // Phase 3 (P5 tranché) : lu depuis BankTypeStore v5 OU LoopBankConfigStore
```

**Cible diff `NvsManager.cpp`** — audit B3 fix : l'init `_loadedQuantize` / `_loadedScaleGroup` se fait dans `loadAll()` lignes 612-613 (PAS dans le ctor — pattern projet pour les arrays NUM_BANKS). Placer le `_loadedLoopQuantize` au même endroit pour cohérence :

```cpp
// Dans NvsManager::loadAll(), juste après les memsets existants ligne 612-613 :
memset(_loadedQuantize, DEFAULT_ARP_START_MODE, NUM_BANKS);
memset(_loadedScaleGroup, 0, NUM_BANKS);
memset(_loadedLoopQuantize, DEFAULT_LOOP_QUANT_MODE, NUM_BANKS);   // <-- NEW Phase 2 Task 1

// Getter body (file scope, près de getLoadedQuantizeMode) :
uint8_t NvsManager::getLoadedLoopQuantizeMode(uint8_t bank) const {
  if (bank >= NUM_BANKS) return DEFAULT_LOOP_QUANT_MODE;
  return _loadedLoopQuantize[bank];
}
```

Note : pas d'init dans le ctor (cohérent avec `_loadedQuantize` / `_loadedScaleGroup`). Les params ARPEG_GEN ctor-init (ligne 35-40 ctor) ont un précédent contraire mais ce sont des params à valeur "non triviale" — pour des arrays NUM_BANKS init au défaut, le pattern projet est `loadAll`.

**Justification stub Phase 2 (P5 différé)** : Le storage NVS de `loopQuantize` (BankTypeStore v5 vs Store dédié) est tranché session 2 (Phase 3 conception). Phase 2 fournit l'interface mais retourne toujours `LOOP_QUANT_FREE`. Avantages :
- L'API `getLoadedLoopQuantizeMode(bank)` est stable dès Phase 2 → setup() peut l'appeler sans `#if PHASE_3_DONE` conditional.
- Test config Phase 2 (LoopTestConfig) hardcode `LOOP_QUANT_FREE` cohérent avec ce que retourne le getter → comportement test = comportement runtime non-Phase-3.
- Phase 3 changera **uniquement** `loadAll()` pour seeder `_loadedLoopQuantize[]` depuis NVS — aucun call site à modifier.

**NVS bump** : non. Aucun store touché. `_loadedLoopQuantize` est un member RAM-only initialisé à `DEFAULT_LOOP_QUANT_MODE` au boot.

**Steps** :

- [ ] **Step 1.1 — Lecture `HardwareConfig.h`** : identifier zone d'insertion (fin de fichier ou section MAX_*_BANKS). Vérifier que `MAX_ARP_BANKS` et autres MAX_* sont au même endroit.

- [ ] **Step 1.2 — Lecture `NvsManager.h` lignes proches de `_loadedQuantize`** : copier le pattern public getter + private member.

- [ ] **Step 1.3 — Lecture `NvsManager.cpp` `loadAll()` lignes 612-613** : identifier emplacement exact du `memset(_loadedQuantize, ...)` (PAS dans le ctor — audit B3 fix). Insérer le `memset(_loadedLoopQuantize, ...)` à la suite, juste après `memset(_loadedScaleGroup, ...)`.

- [ ] **Step 1.4 — Édition `HardwareConfig.h`** : ajouter le bloc LOOP runtime constants (cf snippet). Veiller à `enum LoopQuantMode : uint8_t` (cast explicite) et `#define NUM_LOOP_QUANT_MODES 3` pour les validators.

- [ ] **Step 1.5 — Édition `NvsManager.h`** : ajouter la déclaration publique du getter + le member private. Garder le commentaire "stub Phase 2" pour clarté du reviewer Phase 3.

- [ ] **Step 1.6 — Édition `NvsManager.cpp`** : ajouter le `memset(_loadedLoopQuantize, DEFAULT_LOOP_QUANT_MODE, NUM_BANKS);` dans `loadAll()` juste après les memsets `_loadedQuantize` / `_loadedScaleGroup` (lignes 612-613) — audit B3 fix. Body du getter en fin de fichier (ou près de `getLoadedQuantizeMode` existant pour cohérence visuelle).

- [ ] **Step 1.7 — Compile gate** : `pio run`, exit 0, no warning. Le `enum LoopQuantMode : uint8_t` doit compiler sans `-Wenum-compare` ; vérifier qu'aucun switch existant n'en consomme (ne devrait pas — Phase 2 introduit cet enum).

- [ ] **Step 1.8 — Static read-back** :
  - `grep -rn "LoopQuantMode\|LOOP_QUANT_\|MAX_LOOP_BANKS\|LOOP_NOTE_OFFSET" src/` → présent dans `HardwareConfig.h` (1 enum + 3 valeurs + 2 #define) et `NvsManager.cpp` (1 occurrence pour DEFAULT). Aucun autre site Phase 1 LOOP existant ne réfère ces noms.
  - `grep -n "getLoadedLoopQuantizeMode\|_loadedLoopQuantize" src/` → présent dans `NvsManager.{h,cpp}` uniquement. 0 consommateur attendu Phase 2 jusqu'à Task 7 setup().

- [ ] **Step 1.9 — Mise à jour `nvs-reference.md`** :
  - Section ligne 118 (`illpad_lpot`) : statut reste "DECLARED Phase 1, consumed Phase 5". Pas de modification structurelle.
  - Section générale : ajouter note quelque part visible "LOOP quantize : stub Phase 2 (NvsManager::_loadedLoopQuantize ram-only DEFAULT_LOOP_QUANT_MODE). Storage NVS Phase 3 selon P5."

- [ ] **Step 1.10 — Commit** :
  - Files : `src/core/HardwareConfig.h`, `src/managers/NvsManager.h`, `src/managers/NvsManager.cpp`, `docs/reference/nvs-reference.md`.
  - Message proposé :
    ```
    feat(loop): phase 2 task 1 — HardwareConfig LOOP constants + NvsManager quantize stub

    HardwareConfig.h : MAX_LOOP_BANKS=4, LOOP_NOTE_OFFSET=36 (GM kick),
    LoopQuantMode enum {FREE, BEAT, BAR} + NUM_LOOP_QUANT_MODES + DEFAULT.

    NvsManager : _loadedLoopQuantize[NUM_BANKS] RAM-only stub +
    getLoadedLoopQuantizeMode(bank) helper. Phase 2 retourne toujours
    LOOP_QUANT_FREE (DEFAULT) ; Phase 3 câblera la lecture NVS depuis
    BankTypeStore v5 ou LoopBankConfigStore selon P5 roadmap (différé
    session 2 conception).

    Refs : spec LOOP §17 (quantize FREE/BEAT/BAR), §28 P5 différé.
    Roadmap §5 P5 "à trancher session 2 (Phase 3 conception)".
    ```

---

## Task 2 — LoopEngine.h skeleton + BankSlot.loopEngine member

**Cross-refs** : spec LOOP §3 (state machine), §8 (overdub), §17 (WAITING_*, pending action dispatcher). Spec gesture §27-32 (invariant buffer sacré). Archive `git show loop-archive-2026-05-16:src/loop/LoopEngine.h` (249 lignes, référence d'intention).

**Files** :
- Create : [`src/loop/LoopEngine.h`](../../../src/loop/LoopEngine.h) — nouveau fichier, ~250 lignes (interface complète P4 reco).
- Modify : [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h) — forward decl `class LoopEngine;` (près de `class ArpEngine;`) + ajout `LoopEngine* loopEngine = nullptr;` au struct `BankSlot` (à côté de `ArpEngine* arpEngine`).

**Cible `src/loop/LoopEngine.h` complète** (à porter de l'archive avec adaptations minimes — header guard, includes corrects, constants reference `HardwareConfig.h` Task 1) :

```cpp
#ifndef LOOP_ENGINE_H
#define LOOP_ENGINE_H

#include <stdint.h>
#include <string.h>
#include "../core/HardwareConfig.h"  // NUM_KEYS, MAX_LOOP_BANKS, LOOP_NOTE_OFFSET,
                                     // LoopQuantMode, NUM_LOOP_QUANT_MODES,
                                     // DEFAULT_LOOP_QUANT_MODE

class MidiTransport;

// =================================================================
// Loop constants — file scope (engine-internal, not part of HW config)
// =================================================================
static const uint16_t MAX_LOOP_EVENTS    = 1024;  // Main event buffer (8 KB / engine)
static const uint8_t  MAX_OVERDUB_EVENTS = 128;   // Overdub buffer (1 KB / engine)
static const uint8_t  MAX_PENDING        = 48;    // Pending note queue — generous per Budget Philosophy
static const uint16_t TICKS_PER_BEAT     = 24;    // MIDI PPQN standard
static const uint16_t TICKS_PER_BAR      = 96;    // 4/4 assumption (4 × 24)

// =================================================================
// LoopEvent — recorded pad event (noteOn or noteOff)
// =================================================================
// Stored in _events[] and _overdubBuf[]. Offset in RECORD timebase
// (microseconds from _recordStartUs at first press). noteOn vs noteOff
// encoded via velocity : 0 = noteOff, >0 = noteOn (MIDI convention,
// pas de field séparé isNoteOn — économise 1 byte/event).
//
// AUDIT M4 (2026-05-16) : la struct n'a PAS d'attribut packed — sous
// ESP32 GCC (Xtensa LX7) avec ordre [uint32_t, uint8_t, uint8_t, uint8_t[2]],
// l'alignement naturel produit sizeof == 8 sans padding ajouté. Si on
// réorganise les champs (ex. padIndex en premier), le sizeof deviendrait
// 12 (alignement 4 B pour offsetUs au milieu). Le static_assert ci-dessous
// fige le contrat ; un futur refactor qui le casse verra le compile fail.
// `__attribute__((packed))` n'est PAS utilisé car cela introduirait des
// unaligned loads sur uint32_t (penalty CPU sur Xtensa).
struct LoopEvent {
    uint32_t offsetUs;    // µs from loop start (record timebase)
    uint8_t  padIndex;    // 0..47
    uint8_t  velocity;    // 0 = noteOff, >0 = noteOn
    uint8_t  _pad[2];     // padding alignement 8 B
};
static_assert(sizeof(LoopEvent) == 8, "LoopEvent must be exactly 8 bytes (no packed, GCC natural alignment)");

// =================================================================
// PendingNote — scheduled note emission with time offset (shuffle/chaos)
// =================================================================
// Used by _pending[] for shuffle/chaos scheduling. Both noteOn AND noteOff
// flow through this queue, so gate length is preserved under shuffle —
// un noteOn shuffled avec un noteOff non-shuffled raccourcirait le gate
// (audible sur sustained sounds : bass, pads). Décision design archive.
struct PendingNote {
    uint32_t fireTimeUs;  // micros() timestamp à atteindre
    uint8_t  note;        // MIDI note number (0..127)
    uint8_t  velocity;    // 0 = noteOff, >0 = noteOn
    bool     active;      // true = slot in use, false = free for reuse
};

// =================================================================
// LoopEngine — one loop instance (max MAX_LOOP_BANKS in system)
// =================================================================
//
// State machine (spec LOOP §3) :
//   EMPTY       : fresh engine, no events recorded
//   RECORDING   : capturing first pass into _events[]
//   PLAYING     : looping _events[] proportionally to live BPM
//   OVERDUBBING : playing AND capturing new events into _overdubBuf[]
//   STOPPED     : _events[] intact but not playing
//
// Spec gesture §27 "buffer LOOP sacré" : aucune transition de transport
// (play/stop, bank switch, hold pad, sweep release LEFT) ne modifie
// _events[]. Modifications uniquement via les 6 actions §29 (REC press,
// REC tap pendant OVERDUBBING = revert, CLEAR long-press = wipe, load
// slot = replace, double-tap PLAY/STOP bypass = préserve, WAITING_LOAD +
// bank switch commit). Phase 2 implémente REC press / revert overdub /
// wipe ; load slot et WAITING_LOAD viennent Phase 6.
//
class LoopEngine {
public:
    // --- State enum ---
    // AUDIT B2 (2026-05-16) : `WAITING_PENDING` ajouté comme état générique
    // pour exposer les transitions transitoires §17 spec LOOP (audit hybride
    // acté §0.3). `getState()` retourne WAITING_PENDING quand
    // `_pendingAction != PENDING_NONE`, sinon `_state` brut. Phase 2 hardcode
    // LOOP_QUANT_FREE → WAITING_PENDING jamais traversé en runtime ; le code
    // est correct par review, pas testable HW Phase 2.
    enum State : uint8_t {
        EMPTY           = 0,
        RECORDING       = 1,
        PLAYING         = 2,
        OVERDUBBING     = 3,
        STOPPED         = 4,
        WAITING_PENDING = 5    // returned by getState() if _pendingAction != PENDING_NONE
    };

    // --- Pending action for quantized transitions (spec LOOP §17) ---
    // Set by public transition methods when _quantizeMode != LOOP_QUANT_FREE,
    // consumed by tick() at boundary crossing (B1 pass 2 sentinel fix).
    enum PendingAction : uint8_t {
        PENDING_NONE             = 0,
        PENDING_START_RECORDING  = 1,   // EMPTY       → RECORDING
        PENDING_STOP_RECORDING   = 2,   // RECORDING   → PLAYING (close + bar-snap)
        PENDING_START_OVERDUB    = 3,   // PLAYING     → OVERDUBBING
        PENDING_STOP_OVERDUB     = 4,   // OVERDUBBING → PLAYING (merge)
        PENDING_PLAY             = 5,   // STOPPED     → PLAYING
        PENDING_STOP             = 6    // PLAYING     → STOPPED (soft flush au boundary)
    };

    LoopEngine();

    // --- Config ---
    void begin(uint8_t channel);
    void clear(MidiTransport& transport);           // Hard flush — refcount + pending + events + overdub
    void setPadOrder(const uint8_t* padOrder);
    void setLoopQuantizeMode(uint8_t mode);         // LoopQuantMode FREE/BEAT/BAR
    void setChannel(uint8_t ch);

    // --- State transitions (quantizable — snap to beat/bar if mode != FREE) ---
    // STOPPED-loaded + tap REC = PLAYING + OVERDUBBING simultanés (Q5 §28) :
    // not exposed Phase 2 (no slot load path before Phase 6). The state-machine
    // signature allows it (startOverdub from STOPPED would need a pre-play step).
    void startRecording();
    void stopRecording(const uint8_t* keyIsPressed, const uint8_t* padOrder, float currentBPM);
    void startOverdub();
    void stopOverdub(const uint8_t* keyIsPressed, const uint8_t* padOrder, float currentBPM);
    void play(float currentBPM);
    void stop(MidiTransport& transport);            // PLAYING → STOPPED (soft flush, trailing pending OK)

    // --- State transitions (ALWAYS immediate, never quantized) ---
    void abortOverdub(MidiTransport& transport);    // OVERDUBBING → STOPPED (hard flush, discard overdub)
    void cancelOverdub();                           // OVERDUBBING → PLAYING (keep loop, discard overdub pass)
    void flushLiveNotes(MidiTransport& transport, uint8_t channel);

    // --- Core playback (called every loop iteration, from main.cpp) ---
    void tick(MidiTransport& transport, float currentBPM, uint32_t globalTick);
    void processEvents(MidiTransport& transport);

    // --- Recording input (called by processLoopMode when RECORDING or OVERDUBBING) ---
    void recordNoteOn(uint8_t padIndex, uint8_t velocity);
    void recordNoteOff(uint8_t padIndex);

    // --- Refcount helpers (called by processLoopMode for live-play deduplication) ---
    // Returns true on 0→1 (noteOn) / 1→0 (noteOff) — caller sends the actual MIDI.
    bool noteRefIncrement(uint8_t note);
    bool noteRefDecrement(uint8_t note);

    // --- Per-pad live-press tracking (AUDIT FIX B1 2026-04-06) ---
    // Mirror of MidiEngine::_lastResolvedNote[] pattern. Enables idempotent
    // cleanup sweeps (hold-release, bank-switch, panic) without depending on
    // s_lastKeys edge detection (which is absorbed during hold and would miss
    // the falling edge for pads released during the hold).
    void setLiveNote(uint8_t padIndex, uint8_t note);
    void releaseLivePad(uint8_t padIndex, MidiTransport& transport);

    // --- Note mapping ---
    // Returns 0xFF for unmapped pads (padOrder[i] == 0xFF). WITHOUT the guard:
    // 0xFF + 36 = 35 (uint8_t overflow), wrong MIDI note silently emitted.
    uint8_t padToNote(uint8_t padIndex) const;

    // --- Param setters (stubs Phase 2 — filled in Phase 5) ---
    void setShuffleDepth(float depth);
    void setShuffleTemplate(uint8_t tmpl);
    void setChaosAmount(float amount);
    void setVelPatternIdx(uint8_t idx);
    void setVelPatternDepth(float depth);
    void setBaseVelocity(uint8_t vel);
    void setVelocityVariation(uint8_t pct);

    // --- Getters ---
    // getState() : retourne WAITING_PENDING si _pendingAction != PENDING_NONE,
    // sinon le _state brut (EMPTY/RECORDING/PLAYING/OVERDUBBING/STOPPED).
    // Phase 2 hardcode LOOP_QUANT_FREE → _pendingAction toujours PENDING_NONE
    // en runtime → WAITING_PENDING jamais retourné. Phase 3+ : LedController
    // peut render le WAITING_PENDING via CROSSFADE_COLOR (EVT_WAITING).
    State    getState() const;
    bool     isPlaying() const;              // _state == PLAYING (ignore WAITING_PENDING)
    bool     isRecording() const;            // _state == RECORDING || OVERDUBBING (lock for bank switch §23 inv 2)
    bool     hasPendingAction() const;       // LED feedback Phase 4 : waiting quantize visual (_pendingAction != PENDING_NONE)
    uint8_t  getLoopQuantizeMode() const;    // LED feedback Phase 4 : FREE vs QUANTIZED color
    uint16_t getEventCount() const;

    // --- Tick flash flags (consume-on-read, Phase 4 LedController) ---
    // Three independent flags let the LED hierarchy render beat/bar/wrap distinctly.
    // Detection source :
    //   PLAYING / OVERDUBBING : derived from positionUs + _loopLengthBars
    //                          (only set when _quantizeMode != LOOP_QUANT_FREE
    //                          — FREE mode stays visually solid during playback)
    //   RECORDING            : derived from globalTick (no loop structure yet,
    //                          both FREE and QUANTIZED get tick flashes during REC)
    //   EMPTY / STOPPED      : never set
    bool     consumeBeatFlash();
    bool     consumeBarFlash();
    bool     consumeWrapFlash();

    // --- Param getters (stubs Phase 2 — read by Phase 4 reloadPerBankParams + Phase 6 slot serialize) ---
    float    getShuffleDepth() const;
    uint8_t  getShuffleTemplate() const;
    float    getChaosAmount() const;
    uint8_t  getVelPatternIdx() const;
    float    getVelPatternDepth() const;

private:
    // --- Config ---
    uint8_t        _channel      = 0;
    uint8_t        _quantizeMode = LOOP_QUANT_FREE;
    const uint8_t* _padOrder     = nullptr;

    // --- State machine ---
    State         _state         = EMPTY;
    PendingAction _pendingAction = PENDING_NONE;

    // --- Event buffers (static SRAM, no new/delete runtime) ---
    LoopEvent _events[MAX_LOOP_EVENTS];        // 8 KB
    LoopEvent _overdubBuf[MAX_OVERDUB_EVENTS]; // 1 KB
    uint16_t  _eventCount   = 0;
    uint16_t  _overdubCount = 0;
    uint16_t  _cursorIdx    = 0;

    // --- Playback timing anchors ---
    uint16_t _loopLengthBars = 0;
    uint32_t _recordStartUs  = 0;
    uint32_t _playStartUs    = 0;
    uint32_t _lastPositionUs = 0;
    float    _recordBpm      = 120.0f;   // latched at stopRecording for proportional playback

    // --- Refcount + per-pad live-press tracking (B1 fix) ---
    uint8_t _noteRefCount[128];
    uint8_t _liveNote[NUM_KEYS];   // per-pad live note (0xFF = none). 48 B / engine.

    // --- Pending note queue (shuffle/chaos scheduling — noteOn AND noteOff) ---
    PendingNote _pending[MAX_PENDING];   // 48 × 8 B = 384 B / engine

    // --- Overdub active-pad tracking ---
    // True for pads that had a recordNoteOn during the current overdub session.
    // Used by doStopOverdub() to only inject noteOff for pads actually recorded
    // during this overdub (not for pads held from before — would be orphan).
    bool _overdubActivePads[48];

    // --- Pending action dispatcher stash (AUDIT FIX D-PLAN-1 2026-04-07) ---
    // Captured at the transition call site (stopRecording / stopOverdub / play),
    // consumed by the dispatcher in tick() when the quantize boundary crosses.
    // keyIsPressed is uint8_t[] (0/1) to match SharedKeyboardState.keyIsPressed
    // — bool[] would require an unsafe type-punned cast at every call site.
    const uint8_t* _pendingKeyIsPressed = nullptr;
    const uint8_t* _pendingPadOrder     = nullptr;
    float          _pendingBpm          = 120.0f;

    // --- Tick flash state (hierarchy: beat < bar < wrap) ---
    bool     _beatFlash          = false;
    bool     _barFlash           = false;
    bool     _wrapFlash          = false;
    uint32_t _lastBeatIdx        = 0;
    uint32_t _lastRecordBeatTick = 0xFFFFFFFF;   // force first beat flash at first RECORDING tick

    // --- B1 pass 2 : sentinel-based boundary crossing detection ---
    // Replaces (globalTick % boundary == 0) exact-equality check that could
    // miss a boundary when ClockManager::generateTicks() catches up multiple
    // ticks in one call (up to 4 — see src/midi/ClockManager.cpp). Parallel
    // to ArpEngine's known pending bug (B-001 / B-CODE-1) which is NOT fixed
    // here — LoopEngine starts clean.
    uint32_t _lastDispatchedGlobalTick = 0xFFFFFFFF;

    // --- Velocity params (per-bank, set via setters) ---
    uint8_t _baseVelocity      = 100;
    uint8_t _velocityVariation = 0;

    // --- Phase 5 effect stubs (members alive Phase 2, formulas Phase 5) ---
    float   _shuffleDepth    = 0.0f;
    uint8_t _shuffleTemplate = 0;
    float   _chaosAmount     = 0.0f;
    uint8_t _velPatternIdx   = 0;
    float   _velPatternDepth = 0.0f;

    // --- Private transition implementations (the real work) ---
    void doStartRecording();
    void doStopRecording(const uint8_t* keyIsPressed, const uint8_t* padOrder, float currentBPM);
    void doStartOverdub();
    void doStopOverdub(const uint8_t* keyIsPressed, const uint8_t* padOrder, float currentBPM);
    void doPlay(float currentBPM);
    void doStop(MidiTransport& transport);

    // --- Private helpers ---
    uint32_t calcLoopDurationUs(float bpm) const;
    int32_t  calcShuffleOffsetUs(uint32_t eventOffsetUs, uint32_t recordDurationUs);   // stub Phase 5
    int32_t  calcChaosOffsetUs(uint32_t eventOffsetUs);                                // stub Phase 5
    uint8_t  applyVelocityPattern(uint8_t origVel, uint32_t eventOffsetUs,
                                  uint32_t recordDurationUs);                          // stub Phase 5
    void     schedulePending(uint32_t fireTimeUs, uint8_t note, uint8_t velocity);
    void     flushActiveNotes(MidiTransport& transport, bool hard);
    void     sortEvents(LoopEvent* buf, uint16_t count);
    void     mergeOverdub();
};

// AUDIT M3 (2026-05-16) : fige le contrat SRAM. 4 × ~10.3 KB ≈ 41 KB pour
// MAX_LOOP_BANKS=4. Si un futur refactor fait dépasser, le build casse ici
// — c'est intentionnel (force re-évaluation explicite vs Budget Philosophy).
static_assert(sizeof(LoopEngine) <= 11000, "LoopEngine SRAM footprint exceeded 11 KB — re-evaluate MAX_LOOP_BANKS or trim buffers");

#endif // LOOP_ENGINE_H
```

**Cible diff `KeyboardData.h`** (zone : section forward decls + struct `BankSlot`) :

```cpp
// Près des autres forward decls (probablement haut de fichier ou avant struct BankSlot) :
class LoopEngine;   // Phase 2 LOOP runtime engine (src/loop/LoopEngine.h)

// Dans struct BankSlot, près de ArpEngine* arpEngine. AUDIT M2 fix (2026-05-16) :
// PAS de field initializer = nullptr — cohérent avec ArpEngine* arpEngine actuel
// (sans initializer dans le struct, init explicite main.cpp:551). L'init
// s_banks[i].loopEngine = nullptr est dans Task 7 Zone 4 explicite.
struct BankSlot {
  uint8_t      channel;
  BankType     type;
  ScaleConfig  scale;
  ArpEngine*   arpEngine;
  LoopEngine*  loopEngine;            // <-- NEW Phase 2 : init explicite Task 7 (pas de field initializer)
  // ... rest of fields unchanged ...
};
```

**Steps** :

- [ ] **Step 2.1 — Lecture `KeyboardData.h`** : identifier zone forward decl (proche `class ArpEngine;`) + struct `BankSlot` complet (champs + ordre). Confirmer `ArpEngine* arpEngine` initialisé à `nullptr` (sinon pattern différent).

- [ ] **Step 2.2 — Lecture spec gesture §32 checklist** : valider point par point (cf §0.5 ci-dessus — toutes les boxes vertes sauf "toggle global multi-bank" différé Phase 4).

- [ ] **Step 2.3 — Création `src/loop/`** :
  - `mkdir -p src/loop` (la première fois — vérifier l'existence : `ls src/loop/` doit échouer avant cette étape).

- [ ] **Step 2.4 — Création `src/loop/LoopEngine.h`** : Write avec le contenu complet ci-dessus (~250 lignes). Aucun include de fichiers externes au projet (juste `<stdint.h>` `<string.h>` + `HardwareConfig.h`).

- [ ] **Step 2.5 — Édition `KeyboardData.h`** : ajouter forward decl `class LoopEngine;` + member `LoopEngine* loopEngine = nullptr;` dans `BankSlot`. Vérifier que le include `<stdint.h>` couvre tous les types utilisés (déjà OK normalement).

- [ ] **Step 2.6 — Compile gate** : `pio run`, exit 0, no warning. Le compile fail typique = un membre non initialisé ou un static_assert qui échoue. Le `static_assert(sizeof(LoopEvent) == 8, ...)` est le test critique.

- [ ] **Step 2.7 — Static read-back** :
  - `grep -rn "class LoopEngine\|LoopEngine\*" src/` → 1 forward decl dans `KeyboardData.h`, 1 forward decl dans `LoopEngine.h`, 1 member dans `BankSlot`, header file dans `LoopEngine.h` lui-même.
  - `grep -n "loopEngine" src/` → 1 dans `BankSlot` member, futur usage Tasks 7+.
  - `grep -n "MAX_LOOP_BANKS\|LOOP_NOTE_OFFSET\|LoopQuantMode" src/` → présents dans `HardwareConfig.h` + `LoopEngine.h` (références).

- [ ] **Step 2.8 — Commit** :
  - Files : `src/loop/LoopEngine.h` (nouveau), `src/core/KeyboardData.h`.
  - Message proposé :
    ```
    feat(loop): phase 2 task 2 — LoopEngine.h skeleton + BankSlot.loopEngine

    LoopEngine.h : interface complète Phase 2-6 (P4 reco roadmap) — State enum
    (EMPTY/RECORDING/PLAYING/OVERDUBBING/STOPPED), PendingAction enum (6 valeurs
    pour quantize boundary dispatch), LoopEvent (8 B packed), PendingNote.
    Public API : transitions (start/stop record + overdub + play + stop +
    abort/cancel) + tick/processEvents + record input + refcount + per-pad
    live tracking (B1 fix) + padToNote + setters/getters effets Phase 5 stubs +
    consumeBeatFlash/BarFlash/WrapFlash (Phase 4 LED consumers).

    Private members : _events[1024] + _overdubBuf[128] + _pending[48] +
    _noteRefCount[128] + _liveNote[NUM_KEYS=48] (B1) + _overdubActivePads[48] +
    _pendingKeyIsPressed/_pendingPadOrder/_pendingBpm stash (D-PLAN-1) +
    _lastDispatchedGlobalTick sentinel (B1 pass 2).

    KeyboardData.h : forward decl LoopEngine + member BankSlot.loopEngine =
    nullptr (Task 7 setup l'allouera pour bank.type == BANK_LOOP).

    SRAM impact : 0 jusqu'à Task 7 (member nullptr). MAX_LOOP_BANKS=4 × ~9.4 KB
    = 37.6 KB total quand alloué — marge confortable (Budget Philosophy).

    Refs : spec LOOP §3, §8, §17. Spec gesture §27-§32 (checklist box 4 vérifiée).
    Archive intention : git show loop-archive-2026-05-16:src/loop/LoopEngine.h.
    ```

---

## Task 3 — LoopEngine.cpp ctor + begin + clear + helpers

**Cross-refs** : spec LOOP §3 (state machine init). Archive `git show loop-archive-2026-05-16:src/loop/LoopEngine.cpp` lignes 1-160 (ctor + begin + clear + setPadOrder + setLoopQuantizeMode + setChannel + padToNote + refcount + calcLoopDurationUs + effect stubs).

**Files** :
- Create : `src/loop/LoopEngine.cpp` — nouveau fichier. Cette Task pose les fondations (ctor + helpers) ; Tasks 4-5-6 ajouteront les transitions, tick/processEvents, et record/sort/merge.

**Cible diff `LoopEngine.cpp`** (en-tête + ctor + begin + clear + setPadOrder + setLoopQuantizeMode + setChannel + padToNote + refcount + calcLoopDurationUs + effect stubs Phase 5) :

```cpp
#include "LoopEngine.h"
#include "../core/MidiTransport.h"
#include "../core/HardwareConfig.h"   // DEBUG_SERIAL guard (cf CLAUDE.md projet)
#include <Arduino.h>                  // micros(), random(), constrain(), Serial

// =================================================================
// Ctor + begin + clear
// =================================================================

LoopEngine::LoopEngine() {
    // Init via field initializers in header (= 0 / nullptr / 120.0f / EMPTY).
    // begin() seeds the runtime fields once channel is known.
    memset(_noteRefCount, 0, sizeof(_noteRefCount));
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    memset(_liveNote, 0xFF, sizeof(_liveNote));   // B1 : no live notes at construction
    for (uint8_t i = 0; i < MAX_PENDING; i++) _pending[i].active = false;
}

void LoopEngine::begin(uint8_t channel) {
    _channel        = channel;
    _state          = EMPTY;
    _pendingAction  = PENDING_NONE;
    _quantizeMode   = LOOP_QUANT_FREE;
    _eventCount     = 0;
    _overdubCount   = 0;
    _cursorIdx      = 0;
    _loopLengthBars = 0;
    _recordStartUs  = 0;
    _playStartUs    = 0;
    _lastPositionUs = 0;
    _recordBpm      = 120.0f;
    _beatFlash      = false;
    _barFlash       = false;
    _wrapFlash      = false;
    _lastBeatIdx    = 0;
    _lastRecordBeatTick       = 0xFFFFFFFF;
    _lastDispatchedGlobalTick = 0xFFFFFFFF;   // B1 pass 2 : sentinel "never dispatched"
    _pendingKeyIsPressed = nullptr;           // D-PLAN-1 : stash init
    _pendingPadOrder     = nullptr;
    _pendingBpm          = 120.0f;
    _padOrder       = nullptr;
    memset(_noteRefCount, 0, sizeof(_noteRefCount));
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    memset(_liveNote, 0xFF, sizeof(_liveNote));
    for (uint8_t i = 0; i < MAX_PENDING; i++) _pending[i].active = false;
}

void LoopEngine::clear(MidiTransport& transport) {
    // Spec gesture §29 action 3 (CLEAR long-press → wipe buffer entier).
    // Hard flush : noteOff sur toutes les notes actives + vide pending queue
    // + reset _events[] / _overdubBuf[] / _liveNote[].
    flushActiveNotes(transport, /*hard=*/true);
    _state          = EMPTY;
    _pendingAction  = PENDING_NONE;
    _eventCount     = 0;
    _overdubCount   = 0;
    _cursorIdx      = 0;
    _loopLengthBars = 0;
    _lastPositionUs = 0;
    _beatFlash      = false;
    _barFlash       = false;
    _wrapFlash      = false;
    _lastBeatIdx    = 0;
    _lastRecordBeatTick = 0xFFFFFFFF;
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    memset(_liveNote, 0xFF, sizeof(_liveNote));   // B1 : outstanding live notes gone
}

// =================================================================
// Setters
// =================================================================

void LoopEngine::setPadOrder(const uint8_t* padOrder) {
    _padOrder = padOrder;   // runtime-immutable (setup tools boot-only)
}

void LoopEngine::setLoopQuantizeMode(uint8_t mode) {
    if (mode >= NUM_LOOP_QUANT_MODES) mode = DEFAULT_LOOP_QUANT_MODE;
    _quantizeMode = mode;
    // Si mode descend à FREE pendant qu'un pending action attend, on laisse le
    // pending intact — la boundary check dans tick() se fait sur (globalTick /
    // boundary > _lastDispatchedGlobalTick / boundary) qui sera vrai au
    // prochain tick (sentinel 0xFFFFFFFF > any value) → fire immediat. OK.
}

void LoopEngine::setChannel(uint8_t ch) {
    _channel = ch;
}

// =================================================================
// padToNote — unmapped pad guard (essentiel)
// =================================================================
// Returns 0xFF for unmapped pads (padOrder[i] == 0xFF).
// SANS guard : 0xFF + 36 = 35 (uint8_t overflow), wrong MIDI note silent.

uint8_t LoopEngine::padToNote(uint8_t padIndex) const {
    if (!_padOrder) return 0xFF;
    if (padIndex >= NUM_KEYS) return 0xFF;
    uint8_t order = _padOrder[padIndex];
    if (order == 0xFF) return 0xFF;
    return order + LOOP_NOTE_OFFSET;
}

// =================================================================
// Refcount helpers
// =================================================================
// noteRefIncrement returns true on 0→1 (caller sends MIDI noteOn).
// noteRefDecrement returns true on 1→0 (caller sends MIDI noteOff).
// MUST guard refcount==0 before decrement : flushActiveNotes(hard) zeros all
// refcounts, then a subsequent pad release would underflow to 255 without
// the guard.

bool LoopEngine::noteRefIncrement(uint8_t note) {
    if (note >= 128) return false;
    return (_noteRefCount[note]++ == 0);
}

bool LoopEngine::noteRefDecrement(uint8_t note) {
    if (note >= 128) return false;
    if (_noteRefCount[note] > 0) {
        return (--_noteRefCount[note] == 0);
    }
    return false;
}

// =================================================================
// calcLoopDurationUs — duration µs for current loop length at given BPM
// =================================================================
// Guard clamps bpm to 10.0f min (matches pot range) — prevents both div/0
// AND uint32_t overflow on long loops at very low BPM.
// At bpm=10, 64 bars = 1,536,000,000 µs (~25 min) — fits uint32_t.

uint32_t LoopEngine::calcLoopDurationUs(float bpm) const {
    if (bpm < 10.0f) bpm = 10.0f;
    return (uint32_t)((float)_loopLengthBars * 4.0f * 60000000.0f / bpm);
}

// =================================================================
// Phase 5 effect stubs — implementations Phase 5 plan
// =================================================================
// Setters store params, getters read them. Math is applied via these helpers
// in tick() — for Phase 2, the helpers return neutral values (0 offset,
// passthrough velocity) so the playback engine is fully audible without
// effects active.

int32_t LoopEngine::calcShuffleOffsetUs(uint32_t, uint32_t) { return 0; }
int32_t LoopEngine::calcChaosOffsetUs(uint32_t)             { return 0; }
uint8_t LoopEngine::applyVelocityPattern(uint8_t origVel, uint32_t, uint32_t) {
    return origVel;
}

// --- Param setters / getters (stubs Phase 5 — formulas in Phase 5 plan) ---
void  LoopEngine::setShuffleDepth(float depth)         { _shuffleDepth = depth; }
void  LoopEngine::setShuffleTemplate(uint8_t tmpl)     { _shuffleTemplate = tmpl; }
void  LoopEngine::setChaosAmount(float amount)         { _chaosAmount = amount; }
void  LoopEngine::setVelPatternIdx(uint8_t idx)        { _velPatternIdx = idx; }
void  LoopEngine::setVelPatternDepth(float depth)      { _velPatternDepth = depth; }
void  LoopEngine::setBaseVelocity(uint8_t vel)         { _baseVelocity = vel; }
void  LoopEngine::setVelocityVariation(uint8_t pct)    { _velocityVariation = pct; }
```

**Steps** :

- [ ] **Step 3.1 — Création `src/loop/LoopEngine.cpp`** : Write avec le contenu ci-dessus (~170 lignes). Include order : `LoopEngine.h` → `MidiTransport.h` → `HardwareConfig.h` → `<Arduino.h>` (pour `micros()`).

- [ ] **Step 3.2 — Compile gate** : `pio run`, exit 0. Comme Tasks 4-5-6 ne sont pas encore écrites, le linker peut warn sur `flushActiveNotes` / `sortEvents` / `mergeOverdub` non-définis si elles sont référencées depuis `clear()`. **`clear()` appelle `flushActiveNotes(...)`** → undefined reference attendu jusqu'à Task 5. **Décision : ce commit ne compile probablement pas seul** — accepter un linker error temporaire, le commit gate de Task 3 vérifie uniquement le compile gate (preprocessor + syntax), pas le link. OU : déplacer `clear()` en Task 5 avec `flushActiveNotes`. **Reco : déplacer `clear()` en Task 5** (cohérent avec `flushActiveNotes` qu'il appelle).

> **Note plan exécution** : à l'écriture du code, faire la version réorganisée — `clear()` en Task 5 avec `flushActiveNotes`. Le snippet ci-dessus est l'intention finale, l'exécutant peut découper différemment pour avoir compile vert à chaque Task.

- [ ] **Step 3.3 — Static read-back** : `grep -n "LoopEngine::" src/loop/LoopEngine.cpp` → liste tous les fonctions définies dans ce commit. Comparer avec la liste header : les non-définies seront ajoutées Tasks 4-5-6.

- [ ] **Step 3.4 — Commit** :
  - File : `src/loop/LoopEngine.cpp` (nouveau, partiel).
  - Message proposé :
    ```
    feat(loop): phase 2 task 3 — LoopEngine.cpp ctor + helpers (no transitions yet)

    Implémente ctor (memset buffers refcount/overdubActive/_liveNote 0xFF,
    pending.active=false), begin(channel) (seed runtime fields), setters
    config (setPadOrder/setLoopQuantizeMode/setChannel), padToNote guard
    (0xFF unmapped + offset 36 GM kick), refcount helpers (Increment/Decrement
    avec guard underflow), calcLoopDurationUs (clamp bpm≥10 anti-div0 + uint32
    overflow), et stubs Phase 5 (calcShuffle/calcChaos/applyVelocityPattern
    retournent neutre, setters/getters paramètres).

    clear() différé Task 5 (dépend de flushActiveNotes).

    Refs : spec LOOP §3 init state machine. Archive intention :
    git show loop-archive-2026-05-16:src/loop/LoopEngine.cpp lignes 1-160.
    ```

---

## Task 4 — LoopEngine.cpp public transitions + private doXxx

**Cross-refs** : spec LOOP §7 (RECORDING → PLAYING bar-snap), §8 (overdub merge/abort/cancel), §9 (PLAY/STOP), §17 (pending action dispatcher, quantize boundary). Spec gesture §29 (6 actions buffer-modifying). Archive lignes 160-420 (transitions publiques + doXxx).

**Files** :
- Modify : `src/loop/LoopEngine.cpp` — ajouter les transitions publiques (startRecording, stopRecording, startOverdub, stopOverdub, play, stop, abortOverdub, cancelOverdub) + les private doXxx (doStartRecording, doStopRecording, doStartOverdub, doStopOverdub, doPlay, doStop).

**Cible diff à ajouter à `LoopEngine.cpp`** (~250 lignes — public puis private) :

```cpp
// =================================================================
// State transitions — public API with quantize gating
// =================================================================
// Each public transition checks _quantizeMode. If FREE, execute now
// (private doXxx). If BEAT/BAR, set _pendingAction and return — tick()
// will dispatch when the boundary crosses (B1 pass 2 sentinel check).

void LoopEngine::startRecording() {
    if (_state != EMPTY) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doStartRecording();
    } else {
        _pendingAction = PENDING_START_RECORDING;
    }
}

void LoopEngine::stopRecording(const uint8_t* keyIsPressed, const uint8_t* padOrder,
                               float currentBPM) {
    if (_state != RECORDING) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doStopRecording(keyIsPressed, padOrder, currentBPM);
    } else {
        // Stash args — pending dispatcher has no access to caller's stack
        _pendingKeyIsPressed = keyIsPressed;   // D-PLAN-1 stash
        _pendingPadOrder     = padOrder;
        _pendingBpm          = currentBPM;
        _pendingAction       = PENDING_STOP_RECORDING;
    }
}

void LoopEngine::startOverdub() {
    if (_state != PLAYING) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doStartOverdub();
    } else {
        _pendingAction = PENDING_START_OVERDUB;
    }
}

void LoopEngine::stopOverdub(const uint8_t* keyIsPressed, const uint8_t* padOrder,
                             float currentBPM) {
    if (_state != OVERDUBBING) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doStopOverdub(keyIsPressed, padOrder, currentBPM);
    } else {
        _pendingKeyIsPressed = keyIsPressed;
        _pendingPadOrder     = padOrder;
        _pendingBpm          = currentBPM;
        _pendingAction       = PENDING_STOP_OVERDUB;
    }
}

void LoopEngine::play(float currentBPM) {
    if (_state != STOPPED) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doPlay(currentBPM);
    } else {
        _pendingBpm    = currentBPM;
        _pendingAction = PENDING_PLAY;
    }
}

void LoopEngine::stop(MidiTransport& transport) {
    if (_state != PLAYING) return;
    if (_quantizeMode == LOOP_QUANT_FREE) {
        doStop(transport);
    } else {
        _pendingAction = PENDING_STOP;
        // Playback continues until boundary — visible to LedController via
        // hasPendingAction() pour WAITING_STOP rendering Phase 4.
    }
}

// Abort overdub (PLAY/STOP pad during OVERDUBBING) : ALWAYS immediate.
// Quantize mode is ignored — abort means "discard overdub, keep playing".
// Spec LOOP §8 (audit N5 fix 2026-05-16) : "Tap PLAY/STOP → l'overdub est
// abandonné. Le buffer temporaire est jeté, la boucle d'origine reste
// intacte, l'engine reste en PLAYING. Pour stopper ensuite la boucle, un
// second tap PLAY/STOP est nécessaire."
// → NE PAS flushActiveNotes (loop continue audible) ; NE PAS transitionner
//   à STOPPED (rester PLAYING). Symétrique avec cancelOverdub (M1) qui aussi
//   reste en PLAYING.
void LoopEngine::abortOverdub(MidiTransport& transport) {
    (void)transport;   // unused — pas de flush, loop continue
    if (_state != OVERDUBBING) return;
    _overdubCount = 0;
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    _state         = PLAYING;
    _pendingAction = PENDING_NONE;
    // _playStartUs, _cursorIdx, _lastPositionUs untouched → loop continues
    // exactly where it was. Any live pad the user is still holding will
    // release normally via processLoopMode on the next frame.
}

// Cancel overdub (CLEAR long-press during OVERDUBBING) : discard only the
// events captured during the current overdub pass, keep the loop playing.
// "Undo overdub" path — user made a mistake, wants to try again without
// losing the underlying loop. Spec gesture §29 action 2 (revert).
// ALWAYS immediate (the 500ms long-press IS the "human quantize").
// No flush of active notes : main loop keeps running via _events[] + pending
// queue continues firing naturally. Any live pad still held will release
// normally via processLoopMode on the next frame.
void LoopEngine::cancelOverdub() {
    if (_state != OVERDUBBING) return;
    _overdubCount = 0;
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    _state         = PLAYING;
    _pendingAction = PENDING_NONE;
    // _playStartUs, _cursorIdx, _lastPositionUs untouched → loop continues
    // exactly where it was. No audible gap, no retrigger.
}

// =================================================================
// Private transition implementations (the real work)
// =================================================================

void LoopEngine::doStartRecording() {
    _eventCount    = 0;
    _recordStartUs = micros();
    _lastRecordBeatTick = 0xFFFFFFFF;  // force first beat flash at first tick
    _state         = RECORDING;
}

void LoopEngine::doStopRecording(const uint8_t* keyIsPressed, const uint8_t* padOrder,
                                 float currentBPM) {
    (void)padOrder;  // unused — events stored by padIndex, padToNote() resolves at playback

    // 1. Flush held pads : inject noteOff for pads still pressed. Pas de
    //    "_recordedActivePads" en RECORDING (vs OVERDUBBING) — pendant le
    //    premier recording, on capture tout press → tout pad encore tenu à
    //    la clôture doit recevoir un noteOff pour ne pas être orphan.
    uint32_t closeUs   = micros();
    uint32_t positionUs = closeUs - _recordStartUs;
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        if (keyIsPressed[i] && _eventCount < MAX_LOOP_EVENTS) {
            _events[_eventCount++] = { positionUs, i, 0, {0, 0} };
        }
    }

    // 2. Latch recording BPM for proportional playback
    _recordBpm = (currentBPM < 10.0f) ? 10.0f : currentBPM;

    // 3. Bar-snap : round recorded duration to nearest integer bar count
    //    AUDIT FIX B7 2026-04-06 : defensive floor at 1 ms to prevent
    //    division by zero in the scale computation. En usage normal, la
    //    séquence rising-edge → falling-edge du REC pad garantit quelques ms,
    //    mais un wraparound micros() ou un tap mono-frame pourrait théoriquement
    //    produire une durée zéro.
    uint32_t barDurationUs = (uint32_t)(4.0f * 60000000.0f / _recordBpm);
    uint32_t recordedDurationUs = closeUs - _recordStartUs;
    if (recordedDurationUs < 1000) recordedDurationUs = 1000;
    // Bar-snap : nearest with deadzone (spec LOOP §7 — 25% threshold).
    // Note : la deadzone 25% spec §7 vise à éviter qu'un tap 30 ms après
    // une bar line ne produise 4 bars au lieu de 3. Formule "nearest" :
    // bars = (recordedDurationUs + barDurationUs/2) / barDurationUs = round.
    // Cela correspond à une deadzone effective 50% — pas exactement 25%.
    // Pour 25%, il faudrait : bars = floor(rec/bar) si (rec % bar) < bar/4
    //                          else  ceil(rec/bar).
    // Décision Phase 2 : utiliser "nearest" (round simple) — la deadzone 25%
    // est un raffinement musical à introduire en Phase 4+ si besoin (impact
    // ressenti à mesurer HW). Cohérent avec scope-strict CLAUDE.md.
    uint16_t bars = (recordedDurationUs + barDurationUs / 2) / barDurationUs;
    if (bars == 0) bars = 1;
    if (bars > 64) bars = 64;
    _loopLengthBars = bars;

    // 4. Normalize event offsets to [0, bars * barDurationUs) via proportional scale
    float scale = (float)((uint32_t)bars * barDurationUs) / (float)recordedDurationUs;
    for (uint16_t i = 0; i < _eventCount; i++) {
        _events[i].offsetUs = (uint32_t)((float)_events[i].offsetUs * scale);
    }

    // 5. Sort events by offsetUs (insertion sort, N typically <128 — cheap)
    sortEvents(_events, _eventCount);

    // 6. Transition directly to PLAYING — pas d'appel à play() (on est déjà
    //    sur la boundary garantie par le pending dispatcher, ou en FREE mode
    //    sans snap nécessaire).
    _playStartUs    = micros();
    _cursorIdx      = 0;
    _lastPositionUs = 0;
    _lastBeatIdx    = 0xFFFFFFFF;  // force beat flash at first beat of playback
    _state          = PLAYING;
}

void LoopEngine::doStartOverdub() {
    _overdubCount = 0;
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));
    _state        = OVERDUBBING;
}

void LoopEngine::doStopOverdub(const uint8_t* keyIsPressed, const uint8_t* padOrder,
                               float currentBPM) {
    (void)padOrder;  // unused — kept for signature symmetry with stopRecording

    // 1. Flush held pads : ONLY pads that had a recordNoteOn during this
    //    overdub session (tracked via _overdubActivePads). Pads held from
    //    BEFORE the overdub must NOT receive an injected noteOff (would
    //    be orphan, no matching noteOn in the buffer).
    uint32_t closeUs = micros();
    uint32_t liveDurationUs    = calcLoopDurationUs(currentBPM);
    uint32_t recordDurationUs  = calcLoopDurationUs(_recordBpm);
    uint32_t elapsedUs         = (liveDurationUs > 0)
                                  ? ((closeUs - _playStartUs) % liveDurationUs)
                                  : 0;
    uint32_t positionUs        = (liveDurationUs > 0 && recordDurationUs > 0)
                                  ? (uint32_t)((float)elapsedUs * (float)recordDurationUs
                                              / (float)liveDurationUs)
                                  : 0;

    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        if (_overdubActivePads[i] && keyIsPressed[i]
            && _overdubCount < MAX_OVERDUB_EVENTS) {
            _overdubBuf[_overdubCount++] = { positionUs, i, 0, {0, 0} };
        }
    }
    memset(_overdubActivePads, 0, sizeof(_overdubActivePads));

    // 2. Sort overdub buffer by offsetUs
    sortEvents(_overdubBuf, _overdubCount);

    // 3. Reverse merge O(n+m) into _events[] (B-PLAN-1 fix : refuse l'entire
    //    overdub si overflow, vs truncate tail arbitraire).
    mergeOverdub();

    // 4. Back to PLAYING — no change to _playStartUs (loop already running)
    _state = PLAYING;
}

void LoopEngine::doPlay(float currentBPM) {
    (void)currentBPM;  // BPM read live in tick() via parameter
    // Direct start. If we got here via pending dispatcher, we're already on
    // a boundary — cursor restarts from the top of the loop.
    _playStartUs    = micros();
    _cursorIdx      = 0;
    _lastPositionUs = 0;
    _lastBeatIdx    = 0xFFFFFFFF;  // force beat flash at first beat
    _state          = PLAYING;
}

void LoopEngine::doStop(MidiTransport& transport) {
    // Soft flush — noteOff currently active notes, let pending queue finish
    // its scheduled events (trailing notes allowed). Spec LOOP §17 stop
    // quantize tap simple.
    flushActiveNotes(transport, /*hard=*/false);
    _state = STOPPED;
}
```

**Note Phase 2 décision deadzone bar-snap** : la spec LOOP §7 demande une deadzone 25% pour absorber le retard humain au tap REC. La formule "nearest" implémentée (round simple) équivaut à une deadzone 50% (symétrique). Différence ressentie HW à mesurer Phase 4+ ; pour l'instant scope-strict (round simple). **À documenter dans `STATUS.md` Phase 2 close** comme "raffinement musical en suspens".

**Steps** :

- [ ] **Step 4.1 — Lecture `LoopEngine.cpp` actuel (post Task 3)** : confirmer présence de `clear()` (différé Task 5 dans la décision §3.2 ci-dessus — donc absent au moment de Task 4 si on a appliqué la reco). Confirmer présence des helpers (padToNote, refcount, calcLoopDuration).

- [ ] **Step 4.2 — Insertion section "State transitions — public API"** : 8 fonctions (startRecording, stopRecording, startOverdub, stopOverdub, play, stop, abortOverdub, cancelOverdub) avec quantize gating. ~90 lignes.

- [ ] **Step 4.3 — Insertion section "Private transition implementations"** : 6 fonctions (doStartRecording, doStopRecording, doStartOverdub, doStopOverdub, doPlay, doStop). ~140 lignes.

- [ ] **Step 4.4 — Compile gate** : `pio run`. Linker error attendu sur `sortEvents`, `mergeOverdub`, `flushActiveNotes`, `calcLoopDurationUs` (déjà OK Task 3). `sortEvents` + `mergeOverdub` arrivent Task 6, `flushActiveNotes` arrive Task 5. **Le commit Task 4 ne link pas seul** ; le pio run compile mais le link échoue. Accepter ou différer le compile-then-link gate jusqu'à Task 5 (intermédiaire) ou Task 6 (link complet).

> **Décision exécution** : valider Task 4 sur "compile passe" (preprocessor + syntax + sémantique types) ; relier link gate à Task 6 où tous les symboles seront définis. Documenter explicitement dans le commit message.

- [ ] **Step 4.5 — Static read-back** :
  - `grep -n "void LoopEngine::do" src/loop/LoopEngine.cpp` → 6 occurrences (doStart/StopRecording, doStart/StopOverdub, doPlay, doStop).
  - `grep -n "LoopEngine::start\|LoopEngine::stop\|LoopEngine::play\|LoopEngine::abort\|LoopEngine::cancel" src/loop/LoopEngine.cpp` → 8 public transitions.

- [ ] **Step 4.6 — Commit** :
  - File : `src/loop/LoopEngine.cpp` (modifié).
  - Message proposé :
    ```
    feat(loop): phase 2 task 4 — LoopEngine.cpp transitions + doXxx privates

    Public API (8 transitions avec quantize gating) : startRecording,
    stopRecording, startOverdub, stopOverdub, play, stop (quantizable selon
    _quantizeMode), abortOverdub + cancelOverdub (ALWAYS immediate, ignore
    quantize). Pending action dispatcher utilise _pendingKeyIsPressed/
    _pendingPadOrder/_pendingBpm stash (D-PLAN-1 fix) pour les transitions
    qui ont besoin de keyIsPressed[] hors stack.

    Private doXxx (6) : doStartRecording (seed _recordStartUs micros),
    doStopRecording (flush held pads + bar-snap round + scale offsets +
    sortEvents + transition à PLAYING — B7 fix floor 1ms anti-div0),
    doStartOverdub (reset _overdubActivePads), doStopOverdub (flush
    _overdubActivePads-only avec position en record timebase + sortEvents +
    mergeOverdub), doPlay (seed _playStartUs micros, cursor 0), doStop
    (soft flush + STOPPED).

    Note Phase 2 : bar-snap utilise round simple (deadzone 50% effective).
    Spec LOOP §7 demande 25% — raffinement musical différé Phase 4+ après
    mesure HW. Documenter dans STATUS.md.

    Compile gate : pass. Link gate : différé Task 6 (sortEvents,
    mergeOverdub, flushActiveNotes pas encore définis).

    Refs : spec LOOP §7 (record + bar-snap), §8 (overdub merge/abort/cancel),
    §17 (pending action). Spec gesture §29 (6 actions buffer-modifying).
    ```

---

## Task 5 — LoopEngine.cpp tick() + processEvents + sched + flush + live tracking

**Cross-refs** : spec LOOP §16 (proportional playback scaled record/live BPM), §17 (pending action dispatcher + quantize boundary crossing). Spec gesture §27 (buffer sacré — tick ne modifie pas _events[]), §28 anti-pattern B1 + F8 (sweep release LEFT et live remove évités). Archive lignes 420-900 (tick + processEvents + schedulePending + flushActiveNotes + flushLiveNotes + setLiveNote + releaseLivePad + `clear()` déplacé ici).

**Files** :
- Modify : `src/loop/LoopEngine.cpp` — ajouter tick + processEvents + schedulePending + flushActiveNotes + flushLiveNotes + setLiveNote + releaseLivePad + (déplacer `clear()` ici si on a appliqué la reco Task 3.2 — sinon il est déjà là).

**Cible diff à ajouter à `LoopEngine.cpp`** (~330 lignes — tick le plus gros) :

```cpp
// =================================================================
// tick — pending action dispatcher + proportional playback
// =================================================================
// AUDIT FIX (B1 pass 2, 2026-04-06) : original draft used (globalTick %
// boundary == 0) for the boundary check. That FAILS when ClockManager::
// generateTicks() catches up multiple ticks in one update() call (up to 4
// — voir src/midi/ClockManager.cpp). Exemple : à 120 BPM, globalTick peut
// avancer de 23 à 25 en un frame, skip 24. La check exacte raterait la
// boundary entière et l'action pending attendrait un beat de plus (480ms
// at 120 BPM) la prochaine opportunité. Fix : track _lastDispatchedGlobalTick
// + détection CROSSING via division entière. Sentinel 0xFFFFFFFF / boundary
// > any valid value → premier tick après set pending fire immédiatement.

void LoopEngine::tick(MidiTransport& transport, float currentBPM, uint32_t globalTick) {
    // ---- 1. Pending action dispatcher (quantize boundary CROSSING check) ----
    if (_pendingAction != PENDING_NONE) {
        uint16_t boundary = (_quantizeMode == LOOP_QUANT_BAR) ? TICKS_PER_BAR
                                                              : TICKS_PER_BEAT;
        bool crossed = (_lastDispatchedGlobalTick == 0xFFFFFFFF)
                     || ((globalTick / boundary)
                         > (_lastDispatchedGlobalTick / boundary));
        if (crossed) {
            switch (_pendingAction) {
                case PENDING_START_RECORDING:
                    doStartRecording();
                    break;
                case PENDING_STOP_RECORDING:
                    doStopRecording(_pendingKeyIsPressed, _pendingPadOrder, _pendingBpm);
                    break;
                case PENDING_START_OVERDUB:
                    doStartOverdub();
                    break;
                case PENDING_STOP_OVERDUB:
                    doStopOverdub(_pendingKeyIsPressed, _pendingPadOrder, _pendingBpm);
                    break;
                case PENDING_PLAY:
                    doPlay(_pendingBpm);
                    break;
                case PENDING_STOP:
                    doStop(transport);
                    break;
                default: break;
            }
            _pendingAction = PENDING_NONE;
            _lastDispatchedGlobalTick = globalTick;
        }
        // Fall through to playback logic — loop continues running while
        // PENDING_STOP waits for boundary, and PENDING_START_OVERDUB plays
        // back normally while waiting.
    }

    // ---- 2. RECORDING tick flash (globalTick-based, no loop structure yet) ----
    // Runs BEFORE the playback gate so it fires while state == RECORDING.
    // Both FREE and QUANTIZED get tick flashes during recording — the
    // recording blink needs rhythmic feedback from the master clock.
    if (_state == RECORDING) {
        uint32_t currentBeatTick = globalTick / TICKS_PER_BEAT;
        if (currentBeatTick != _lastRecordBeatTick) {
            _beatFlash = true;
            if ((globalTick % TICKS_PER_BAR) < TICKS_PER_BEAT) _barFlash = true;
            _lastRecordBeatTick = currentBeatTick;
        }
        return;  // RECORDING does not run playback logic
    }

    // ---- 3. Playback logic (only PLAYING and OVERDUBBING produce scheduled events) ----
    if (_state != PLAYING && _state != OVERDUBBING) return;

    uint32_t now = micros();

    uint32_t liveDurationUs   = calcLoopDurationUs(currentBPM);
    uint32_t recordDurationUs = calcLoopDurationUs(_recordBpm);
    if (liveDurationUs == 0 || recordDurationUs == 0) return;   // defense in depth

    uint32_t elapsedUs  = (now - _playStartUs) % liveDurationUs;
    uint32_t positionUs = (uint32_t)((float)elapsedUs * (float)recordDurationUs
                                    / (float)liveDurationUs);

    // Wrap detection : position jumped backward vs last tick.
    // DO NOT flush active notes here — refcount + pending queue handle long
    // notes that cross the wrap naturally (overlaps allowed per Budget
    // Philosophy). Spec gesture §27 : tick ne modifie pas _events[].
    bool wrapped = (positionUs < _lastPositionUs);
    if (wrapped) {
        _cursorIdx    = 0;
        _wrapFlash    = true;
        _lastBeatIdx  = 0xFFFFFFFF;   // reset so first beat of new cycle flashes
    }

    // ---- 4. PLAYING/OVERDUBBING tick flash (positionUs-based, QUANTIZED only) ----
    // FREE mode : no tick flashes during playback (solid render, no internal
    // beat grid exposed). QUANTIZED modes : derive beat/bar from the loop's
    // own structure.
    // AUDIT N3 (2026-05-16) : `barDurationUs / 4` hardcode 4/4 time signature
    // (cohérent avec TICKS_PER_BAR=96 = 4 × TICKS_PER_BEAT=24). Si TICKS_PER_BAR
    // change un jour pour supporter 3/4 ou 7/8, ce calcul cassera — le facteur
    // 4 deviendra TICKS_PER_BAR / TICKS_PER_BEAT. Phase 2 : 4/4 assumed (spec §16).
    if (_quantizeMode != LOOP_QUANT_FREE && _loopLengthBars > 0) {
        uint32_t barDurationUs  = recordDurationUs / _loopLengthBars;
        uint32_t beatDurationUs = barDurationUs / 4;   // 4/4 assumption
        if (beatDurationUs > 0) {
            uint32_t beatIdxNow = positionUs / beatDurationUs;
            if (beatIdxNow != _lastBeatIdx) {
                _beatFlash = true;
                if ((beatIdxNow % 4) == 0 && !wrapped) _barFlash = true;
                _lastBeatIdx = beatIdxNow;
            }
        }
    }

    _lastPositionUs = positionUs;

    // ---- 5. Cursor scan — both noteOn AND noteOff through schedulePending ----
    // Spec gesture §27 : cette boucle LIT _events[] sans le modifier — buffer
    // sacré préservé.
    while (_cursorIdx < _eventCount &&
           _events[_cursorIdx].offsetUs <= positionUs) {
        const LoopEvent& ev = _events[_cursorIdx];
        int32_t shuffleUs = calcShuffleOffsetUs(ev.offsetUs, recordDurationUs);
        int32_t chaosUs   = calcChaosOffsetUs(ev.offsetUs);

        uint8_t note = padToNote(ev.padIndex);
        if (note != 0xFF) {
            if (ev.velocity > 0) {
                uint8_t vel = applyVelocityPattern(ev.velocity, ev.offsetUs,
                                                   recordDurationUs);
                schedulePending((uint32_t)((int32_t)now + shuffleUs + chaosUs),
                                note, vel);
            } else {
                // noteOff : ALSO through pending with same shuffle/chaos
                // offset — preserves gate length.
                schedulePending((uint32_t)((int32_t)now + shuffleUs + chaosUs),
                                note, 0);
            }
        }
        _cursorIdx++;
    }
}

// =================================================================
// processEvents — fire pending notes via refcount
// =================================================================
// noteOn : refcount increment, MIDI only on 0→1 transition.
// noteOff : refcount decrement, MIDI only on 1→0 transition.
// Mirror du pattern ArpEngine.processEvents.

void LoopEngine::processEvents(MidiTransport& transport) {
    uint32_t now = micros();
    for (uint8_t i = 0; i < MAX_PENDING; i++) {
        if (!_pending[i].active) continue;
        if ((int32_t)(now - _pending[i].fireTimeUs) < 0) continue;   // not yet

        uint8_t note = _pending[i].note;
        if (note < 128) {
            if (_pending[i].velocity > 0) {
                // noteOn : only send MIDI on 0→1
                if (_noteRefCount[note] == 0) {
                    transport.sendNoteOn(_channel, note, _pending[i].velocity);
                }
                _noteRefCount[note]++;
            } else {
                // noteOff : only send MIDI on 1→0
                if (_noteRefCount[note] > 0) {
                    _noteRefCount[note]--;
                    if (_noteRefCount[note] == 0) {
                        transport.sendNoteOn(_channel, note, 0);
                    }
                }
            }
        }
        _pending[i].active = false;
    }
}

// =================================================================
// schedulePending — queue a note for later firing
// =================================================================
// Store a pending note in the first free slot. Silent drop on overflow —
// MAX_PENDING = 48 is sized generously per Budget Philosophy "prefer safe
// over economical". Under DEBUG_SERIAL, log a warning so overflow shows up
// in dev but costs nothing in release.

void LoopEngine::schedulePending(uint32_t fireTimeUs, uint8_t note, uint8_t velocity) {
    for (uint8_t i = 0; i < MAX_PENDING; i++) {
        if (!_pending[i].active) {
            _pending[i].fireTimeUs = fireTimeUs;
            _pending[i].note       = note;
            _pending[i].velocity   = velocity;
            _pending[i].active     = true;
            return;
        }
    }
    #if DEBUG_SERIAL
    Serial.printf("[LOOP] pending queue overflow on bank %u — note %u dropped\n",
                  _channel, note);
    #endif
}

// =================================================================
// flushActiveNotes — soft or hard flush
// =================================================================
// hard = true  → noteOff every active note AND empty pending queue.
//                Used by clear(), abortOverdub(), flushLiveNotes() paths.
// hard = false → noteOff every active note but LEAVE pending queue running.
//                Used by doStop() — trailing shuffle/chaos events finish.

void LoopEngine::flushActiveNotes(MidiTransport& transport, bool hard) {
    for (uint8_t n = 0; n < 128; n++) {
        if (_noteRefCount[n] > 0) {
            transport.sendNoteOn(_channel, n, 0);   // velocity 0 = noteOff
            _noteRefCount[n] = 0;
        }
    }
    if (hard) {
        for (uint8_t i = 0; i < MAX_PENDING; i++) _pending[i].active = false;
    }
}

// =================================================================
// flushLiveNotes — bank switch cleanup (CC123 + zero refcount + reset _liveNote)
// =================================================================
// Called by BankManager on bank switch (outgoing bank, Task 10) and by
// midiPanic() (Phase 4+ deferred). Sends CC123 (All Notes Off) on this
// engine's channel and zeroes refcounts + live-note tracking. Does NOT
// touch pending queue — the engine keeps running in background and its
// pending events will fire on the correct channel (still _channel).
//
// Spec gesture §27 : flushLiveNotes ne modifie PAS _events[] — le buffer
// reste sacré. Seul l'état refcount + live tracking est wipé.

void LoopEngine::flushLiveNotes(MidiTransport& transport, uint8_t channel) {
    (void)channel;   // signature kept for symmetry ; we use _channel
    transport.sendAllNotesOff(_channel);
    memset(_noteRefCount, 0, sizeof(_noteRefCount));
    memset(_liveNote, 0xFF, sizeof(_liveNote));   // B1 : clear live-press tracking
    // Pending queue NOT cleared — trailing events fire normally on _channel.
}

// =================================================================
// setLiveNote / releaseLivePad — per-pad live tracking (AUDIT FIX B1)
// =================================================================
// setLiveNote : called by processLoopMode (main.cpp Task 8) at rising edge,
// INCONDITIONALLY (regardless of whether MIDI noteOn was actually sent by
// noteRefIncrement — when refcount was already > 0 from loop playback, no
// noteOn is sent but we still register the pad as "live" so its falling
// edge can decrement refcount cleanly). Audit N4 fix (2026-05-16) :
// commentaire original disait "AFTER noteRefIncrement has sent the MIDI
// noteOn" — trompeur, le call est inconditionnel.
// Since padOrder is runtime-immutable (setup tools boot-only), the stored
// note remains valid for the entire live-press lifetime.

void LoopEngine::setLiveNote(uint8_t padIndex, uint8_t note) {
    if (padIndex >= NUM_KEYS) return;
    _liveNote[padIndex] = note;
}

// Idempotent per-pad noteOff : releases whatever live note was attached to
// this pad. No-op if the pad has no live note (0xFF). Callers :
//   1. processLoopMode at falling edge (replaces direct noteRefDecrement).
//   2. handleLeftReleaseCleanup sweep on LOOP banks (idempotent sweep
//      pattern, mirror MidiEngine noteOff() pour NORMAL banks).
// Clears _liveNote[padIndex] AVANT noteRefDecrement (audit N1 fix
// 2026-05-16 : justification "idempotence sweep simultané" — si un même
// pad est swept deux fois dans le même frame depuis 2 sources distinctes,
// le 2e appel verra _liveNote == 0xFF et early-return. "re-entry safety"
// original incorrect — noteRefDecrement est synchronous integer math sans
// callback).

void LoopEngine::releaseLivePad(uint8_t padIndex, MidiTransport& transport) {
    if (padIndex >= NUM_KEYS) return;
    uint8_t note = _liveNote[padIndex];
    if (note == 0xFF) return;                         // already released — idempotent no-op
    _liveNote[padIndex] = 0xFF;                       // clear first (sweep idempotence)
    if (noteRefDecrement(note)) {
        transport.sendNoteOn(_channel, note, 0);      // velocity 0 = noteOff
    }
}
```

**Note (`clear()` déplacé)** : si la reco Task 3.2 a été suivie, ajouter aussi `clear()` ici (cf snippet Task 3) puisque il dépend de `flushActiveNotes` qui devient disponible.

**Steps** :

- [ ] **Step 5.1 — Lecture spec gesture §27 + §29 + §32** : re-confirmer que tick() ne modifie pas `_events[]` (boucle ligne 5 lit, ne write pas) et que `flushActiveNotes`/`flushLiveNotes`/`releaseLivePad` ne touchent pas non plus le buffer. Cocher checklist §32 box 1 et 4.

- [ ] **Step 5.2 — Insertion section "tick"** : ~110 lignes. Vérifier `boundary` calcul (BAR si LOOP_QUANT_BAR, sinon BEAT — donc FREE devient implicitement BEAT pour boundary, mais le check précédent `_quantizeMode == LOOP_QUANT_FREE` dans les transitions publiques garantit qu'on n'arrive jamais ici en FREE — défense en profondeur OK).

- [ ] **Step 5.3 — Insertion "processEvents"** : ~30 lignes. Vérifier guard `note < 128`.

- [ ] **Step 5.4 — Insertion "schedulePending"** : ~20 lignes. `#if DEBUG_SERIAL` autour du Serial.printf (CLAUDE.md projet).

- [ ] **Step 5.5 — Insertion "flushActiveNotes"** : ~15 lignes. Cohérence soft/hard.

- [ ] **Step 5.6 — Insertion "flushLiveNotes"** : ~10 lignes. Vérifier que `sendAllNotesOff` existe sur `MidiTransport` (signature `(uint8_t channel)`).

- [ ] **Step 5.7 — Insertion "setLiveNote / releaseLivePad"** : ~30 lignes. Vérifier ordre : clear `_liveNote[pad]` AVANT `noteRefDecrement` (re-entry safety documenté).

- [ ] **Step 5.8 — Insertion `clear()` (si différé Task 3)** : ~20 lignes, appelle `flushActiveNotes(transport, true)` qui est maintenant disponible.

- [ ] **Step 5.9 — Compile gate** : `pio run`. Link error encore possible sur `sortEvents` + `mergeOverdub` (Task 6). Pareil que Task 4 : accepter, link gate à Task 6.

- [ ] **Step 5.10 — Static read-back** :
  - `grep -n "LoopEngine::tick\|LoopEngine::process\|LoopEngine::schedule\|LoopEngine::flush\|LoopEngine::setLive\|LoopEngine::releaseLive" src/loop/LoopEngine.cpp` → 7 fonctions définies.
  - `grep -n "_events\[\]" src/loop/LoopEngine.cpp tick` → 0 résultat WRITE dans tick (uniquement lecture). Buffer sacré préservé.

- [ ] **Step 5.11 — Commit** :
  - File : `src/loop/LoopEngine.cpp` (modifié).
  - Message proposé :
    ```
    feat(loop): phase 2 task 5 — LoopEngine.cpp tick + processEvents + flush + live tracking

    tick() : pending action dispatcher (B1 pass 2 boundary crossing sentinel
    fix — évite skip de boundary quand ClockManager catches up multi-ticks),
    RECORDING tick flash globalTick-based, PLAYING/OVERDUBBING playback avec
    proportional scale recordBpm↔currentBPM, wrap detection sans flush
    (refcount handle overlaps naturellement — Budget Philosophy), tick flashes
    positionUs-based en QUANTIZED, cursor scan _events[] lecture-seule
    (spec gesture §27 buffer sacré préservé).

    processEvents() : fire pending notes au fireTimeUs, refcount transitions
    0→1 (sendNoteOn) et 1→0 (sendNoteOff via velocity=0).

    schedulePending() : first-free-slot insert, silent drop on overflow +
    Serial.printf sous DEBUG_SERIAL.

    flushActiveNotes(hard) : soft (refcount only) ou hard (refcount + pending
    queue wipe). flushLiveNotes(transport, ch) : CC123 + zero refcount +
    reset _liveNote — pour BankManager switch outgoing (Task 10) et midiPanic
    Phase 4+. Ne touche PAS _events[] (buffer sacré).

    setLiveNote / releaseLivePad : per-pad live tracking B1 fix, idempotent
    sweep pattern miroir MidiEngine._lastResolvedNote[]. Pour processLoopMode
    falling edge + handleLeftReleaseCleanup LOOP branch (Task 8).

    clear() déplacé ici (dépendance flushActiveNotes maintenant disponible).

    Compile gate : pass. Link gate : différé Task 6 (sortEvents +
    mergeOverdub pas encore).

    Refs : spec LOOP §16 (proportional scale), §17 (pending dispatcher).
    Spec gesture §27 (buffer sacré — tick lit, n'écrit pas) + §32 box 1+4.
    AUDIT FIXES : B1 (live tracking), B1 pass 2 (boundary crossing).
    ```

---

## Task 6 — LoopEngine.cpp record + sort + merge + getters

**Cross-refs** : spec LOOP §7 (recordNoteOn pendant RECORDING), §8 (recordNoteOn pendant OVERDUBBING avec _overdubActivePads + B2 fix), §17 (getter `hasPendingAction` pour LED Phase 4). Spec gesture §28 anti-pattern F8 (live remove évité). Archive lignes 640-844 (record + sort + merge + getters complet).

**Files** :
- Modify : `src/loop/LoopEngine.cpp` — ajouter recordNoteOn + recordNoteOff + sortEvents + mergeOverdub + tous les getters (state + flash flags + effect getters).

**Cible diff à ajouter à `LoopEngine.cpp`** (~200 lignes) :

```cpp
// =================================================================
// recordNoteOn / recordNoteOff — capture input pendant RECORDING/OVERDUBBING
// =================================================================
// AUDIT FIX (B2, 2026-04-06) : original OVERDUBBING branch computed
// elapsedUs = (now - _playStartUs) % liveDurationUs où liveDurationUs était
// calculé from _recordBpm (variable name misleading) — mixing LIVE et RECORD
// timebases. Avec tempo change entre recording et playback, les events
// finissaient désalignés de dizaines de ms. Fix : lire _lastPositionUs
// (déjà en RECORD timebase, écrit par tick() en fin de chaque frame). Latence
// ≤ 1 frame (~1 ms), musicalement imperceptible.
//
// Pipeline note : dans main loop (Phase 2 Task 8 + 9), handlePadInput →
// processLoopMode → recordNoteOn/Off s'exécute AVANT la boucle per-bank
// loopEngine->tick(). Donc dans frame N, recordNoteOn lit _lastPositionUs
// écrit par tick() à la fin de frame N-1. La latence 1 frame est in fine
// imperceptible (< 1ms).
//
// Spec gesture §29 action 1 (REC press → enregistre) — seules modifications
// autorisées de _events[] / _overdubBuf[] en runtime.

void LoopEngine::recordNoteOn(uint8_t padIndex, uint8_t velocity) {
    if (padIndex >= NUM_KEYS) return;
    if (_state == RECORDING) {
        if (_eventCount >= MAX_LOOP_EVENTS) return;
        uint32_t offsetUs = micros() - _recordStartUs;   // real-time during RECORDING
        _events[_eventCount++] = { offsetUs, padIndex, velocity, {0, 0} };
    } else if (_state == OVERDUBBING) {
        if (_overdubCount >= MAX_OVERDUB_EVENTS) return;
        // B2 fix : utiliser _lastPositionUs (écrit par tick() à chaque frame
        // en fin), déjà en RECORD timebase. Cohérent avec le cursor scan
        // de tick() — overdub events parfaitement alignés avec _events[]
        // timebase regardless of live BPM divergence.
        uint32_t offsetUs = _lastPositionUs;
        _overdubBuf[_overdubCount++] = { offsetUs, padIndex, velocity, {0, 0} };
        _overdubActivePads[padIndex] = true;
    }
    // EMPTY / PLAYING / STOPPED : no-op — pas de capture hors REC/OD.
    // Spec gesture §27 : LEFT interactions / press musical hors REC/OD ne
    // modifient pas le buffer.
}

void LoopEngine::recordNoteOff(uint8_t padIndex) {
    if (padIndex >= NUM_KEYS) return;
    if (_state == RECORDING) {
        if (_eventCount >= MAX_LOOP_EVENTS) return;
        uint32_t offsetUs = micros() - _recordStartUs;
        _events[_eventCount++] = { offsetUs, padIndex, 0, {0, 0} };
    } else if (_state == OVERDUBBING) {
        if (_overdubCount >= MAX_OVERDUB_EVENTS) return;
        uint32_t offsetUs = _lastPositionUs;   // B2 fix (cf recordNoteOn)
        _overdubBuf[_overdubCount++] = { offsetUs, padIndex, 0, {0, 0} };
        // NOTE : NE PAS clear _overdubActivePads[padIndex] ici. Le bitmask
        // tracke "ce pad a-t-il eu un recordNoteOn pendant cet overdub" —
        // utilisé uniquement par doStopOverdub() au close pour décider quels
        // held pads méritent un injected noteOff. Une fois le user release
        // naturellement, le bit ne sert plus.
    }
    // Spec gesture §28 F8 anti-pattern : recordNoteOff NE modifie PAS _events[]
    // en EMPTY/PLAYING/STOPPED. Pas de "live remove" sur falling edge.
}

// =================================================================
// sortEvents — insertion sort in place
// =================================================================
// N small (< MAX_OVERDUB_EVENTS = 128 pour overdub buf, typically < 200
// events pour main buf). Stable, no allocation. O(n²) worst case — sur 128
// events ≈ 16K comparaisons, négligeable à 240 MHz.

void LoopEngine::sortEvents(LoopEvent* buf, uint16_t count) {
    for (uint16_t i = 1; i < count; i++) {
        LoopEvent key = buf[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && buf[j].offsetUs > key.offsetUs) {
            buf[j + 1] = buf[j];
            j--;
        }
        buf[j + 1] = key;
    }
}

// =================================================================
// mergeOverdub — reverse merge O(n+m), in place in _events[]
// =================================================================
// Algorithm : reverse merge from end. Suppose _events[] a capacité pour
// _eventCount + _overdubCount. Les deux buffers doivent être triés par
// offsetUs.
//
// AUDIT FIX (B-PLAN-1, 2026-04-07) : refuse l'entire overdub merge plutôt
// que truncate arbitraire. Original truncation gardait _overdubCount =
// (MAX - eventCount), drop des LATEST overdub events depuis la tail (musi-
// calement contre-intuitif — overdub accumule en général à la fin). Refuser
// le merge préserve la loop existante + signale via DEBUG log que le user
// a hit le ceiling.

void LoopEngine::mergeOverdub() {
    if (_overdubCount == 0) return;
    uint32_t totalCount = (uint32_t)_eventCount + (uint32_t)_overdubCount;
    if (totalCount > MAX_LOOP_EVENTS) {
        #if DEBUG_SERIAL
        Serial.printf("[LOOP] mergeOverdub refused: total %u > max %u, %u overdub events lost\n",
                      (unsigned)totalCount, (unsigned)MAX_LOOP_EVENTS,
                      (unsigned)_overdubCount);
        #endif
        _overdubCount = 0;
        return;
    }

    int32_t i = (int32_t)_eventCount - 1;     // tail of main buffer
    int32_t j = (int32_t)_overdubCount - 1;   // tail of overdub buffer
    int32_t k = (int32_t)totalCount - 1;      // write position

    while (j >= 0) {
        if (i >= 0 && _events[i].offsetUs > _overdubBuf[j].offsetUs) {
            _events[k--] = _events[i--];
        } else {
            _events[k--] = _overdubBuf[j--];
        }
    }
    // Remaining _events[i..] already in place.

    _eventCount   = (uint16_t)totalCount;
    _overdubCount = 0;
}

// =================================================================
// Getters
// =================================================================

// AUDIT B2 (2026-05-16) : getState() retourne WAITING_PENDING si une pending
// action est en attente (quantize boundary pas encore croisée), sinon _state
// brut. Phase 2 hardcode LOOP_QUANT_FREE → _pendingAction toujours PENDING_NONE
// en runtime → WAITING_PENDING jamais retourné. Phase 3+ : LedController
// renderBankLoop utilisera ce return pour render le CROSSFADE_COLOR (EVT_WAITING).
LoopEngine::State LoopEngine::getState() const {
    return (_pendingAction != PENDING_NONE) ? WAITING_PENDING : _state;
}

// isPlaying / isRecording ignorent WAITING_PENDING — ils reflètent le _state
// brut (le buffer est-il en train de jouer / d'enregistrer ?). Utilisés par
// BankManager guards Task 10 : isRecording() pour silent deny.
bool     LoopEngine::isPlaying() const           { return _state == PLAYING; }
bool     LoopEngine::isRecording() const         { return _state == RECORDING
                                                       || _state == OVERDUBBING; }
bool     LoopEngine::hasPendingAction() const    { return _pendingAction != PENDING_NONE; }
uint8_t  LoopEngine::getLoopQuantizeMode() const { return _quantizeMode; }
uint16_t LoopEngine::getEventCount() const       { return _eventCount; }

// Consume-on-read pattern : flag cleared as it's returned. Chaque flag est
// consommé indépendamment par LedController (Phase 4) chaque frame, donc si
// plusieurs flags sont set (beat + bar + wrap au même sample), tous sont
// delivered. LedController choisit le hiérarchique winner (wrap > bar > beat).

bool LoopEngine::consumeBeatFlash() {
    bool tmp = _beatFlash;
    _beatFlash = false;
    return tmp;
}
bool LoopEngine::consumeBarFlash() {
    bool tmp = _barFlash;
    _barFlash = false;
    return tmp;
}
bool LoopEngine::consumeWrapFlash() {
    bool tmp = _wrapFlash;
    _wrapFlash = false;
    return tmp;
}

// --- Param getters (stubs Phase 5) ---
float    LoopEngine::getShuffleDepth() const     { return _shuffleDepth; }
uint8_t  LoopEngine::getShuffleTemplate() const  { return _shuffleTemplate; }
float    LoopEngine::getChaosAmount() const      { return _chaosAmount; }
uint8_t  LoopEngine::getVelPatternIdx() const    { return _velPatternIdx; }
float    LoopEngine::getVelPatternDepth() const  { return _velPatternDepth; }
```

**Steps** :

- [ ] **Step 6.1 — Insertion section "recordNoteOn / recordNoteOff"** : ~40 lignes. Vérifier B2 fix appliqué (utiliser `_lastPositionUs` en OVERDUBBING). Cohérence guard `padIndex >= NUM_KEYS`.

- [ ] **Step 6.2 — Insertion "sortEvents"** : ~12 lignes insertion sort canonique.

- [ ] **Step 6.3 — Insertion "mergeOverdub"** : ~30 lignes avec B-PLAN-1 fix (refuse merge si overflow). Logger DEBUG_SERIAL `(unsigned)` cast pour Serial.printf %u portable.

- [ ] **Step 6.4 — Insertion section "Getters"** : ~25 lignes — state getters + consume flash + effect getters.

- [ ] **Step 6.5 — Compile gate complète + link gate** : `pio run`, **link doit passer maintenant** car toutes les fonctions sont définies. Vérifier RAM/Flash dans output (cf §4 checklist).

- [ ] **Step 6.6 — Static read-back complet** :
  - `grep -c "LoopEngine::" src/loop/LoopEngine.cpp` → ~30 définitions (toutes les méthodes du header).
  - `nm` (optionnel) : objdump pour vérifier qu'aucun symbole `LoopEngine::*` reste undefined.

- [ ] **Step 6.7 — Commit** :
  - File : `src/loop/LoopEngine.cpp` (modifié — devient complet).
  - Message proposé :
    ```
    feat(loop): phase 2 task 6 — LoopEngine.cpp record + sort + merge + getters

    recordNoteOn / recordNoteOff : capture input pendant RECORDING (offset =
    micros() - _recordStartUs) ou OVERDUBBING (offset = _lastPositionUs en
    RECORD timebase — B2 fix anti mixing live/record). _overdubActivePads
    marqué au noteOn pour permettre doStopOverdub flush sélectif (pas
    d'orphan noteOff pour pads tenus avant overdub). EMPTY/PLAYING/STOPPED :
    no-op (spec gesture §28 F8 — pas de live remove).

    sortEvents : insertion sort in place, stable, O(n²) sur N petit (<200
    events typique) — négligeable à 240 MHz.

    mergeOverdub : reverse merge O(n+m) in place dans _events[]. B-PLAN-1
    fix : refuse l'entire overdub merge si overflow, vs truncate tail
    arbitraire — préserve loop existante + DEBUG_SERIAL log.

    Getters : getState/isPlaying/isRecording/hasPendingAction/getLoopQuantizeMode/
    getEventCount (Phase 4 LedController consumers). Consume-on-read pattern
    pour consumeBeatFlash/BarFlash/WrapFlash (LedController choisit hiérarchie
    wrap>bar>beat). Effect getters Phase 5 stubs (Shuffle/Chaos/VelPattern).

    Link gate : pass. RAM/Flash budget vérifiés dans output build.

    Refs : spec LOOP §7 (REC capture), §8 (overdub merge B2 + B-PLAN-1), §17
    (getter hasPendingAction LED). Spec gesture §28 F8 (no live remove), §29
    action 1 (REC press = unique modification _events[]).

    Phase 2 LoopEngine COMPLET — prêt pour main wiring Tasks 7-10.
    ```

---

## Task 7 — LoopTestConfig.h + main.cpp statics + setup() LoopEngine assignment

**Cross-refs** : spec LOOP §6 (config bank LOOP), §22 (pot routing — Phase 4). Roadmap Q3 (pads 47/46/45 validés HW). Plan archive Steps 2 + 3 + 4.

**Files** :
- Create : `src/loop/LoopTestConfig.h` — nouveau, ~15 lignes, hardcode bank 7 + pads test.
- Modify : `src/main.cpp` — includes + statics + init BankSlot.loopEngine=nullptr + setup() loop assignment + test config override.

**Cible `src/loop/LoopTestConfig.h`** :

```cpp
#pragma once

// =================================================================
// LOOP test config — hardcoded values for Phase 2 testing
// =================================================================
// À retirer Phase 3 (ToolBankConfig + ToolPadRoles support LOOP via NVS).
// Pads test validés HW non-conflictuels avec drumming (vs 30/31/32
// problématiques sur le layout actuel — cf roadmap §4 Q3).

#define LOOP_TEST_ENABLED        1
#define LOOP_TEST_BANK           6   // Bank 7 affichée musicien = index 6 zero-based (channel 7)
#define LOOP_TEST_REC_PAD        47
#define LOOP_TEST_PLAYSTOP_PAD   46
#define LOOP_TEST_CLEAR_PAD      45
```

**Cible diff `src/main.cpp`** (4 zones de patch) :

**Zone 1 — includes (haut de fichier, près des autres include core/managers)** :

```cpp
#include "loop/LoopEngine.h"
#if defined(LOOP_TEST_ENABLED) && LOOP_TEST_ENABLED
  #include "loop/LoopTestConfig.h"
#endif
```

Si `LoopTestConfig.h` est inclus inconditionnellement et `LOOP_TEST_ENABLED` y est défini, l'`#if` au-dessus est redondant. Simplifier en `#include "loop/LoopTestConfig.h"` direct + `#if LOOP_TEST_ENABLED` autour des usages.

**Zone 2 — static instance arrays (après `s_arpEngines[4]`, ~ligne 68)** :

```cpp
static LoopEngine s_loopEngines[MAX_LOOP_BANKS];   // 4 × ~9.4 KB = ~37.6 KB SRAM
```

**Zone 3 — LOOP control pad statics + state edge (après les autres statics pad, près de s_holdPad ligne 81 ou plus naturel)** :

```cpp
// LOOP control pads (Phase 2 : seedé par LoopTestConfig override ; Phase 3 : seedé par LoopPadStore NVS load)
static uint8_t  s_recPad             = 0xFF;
static uint8_t  s_loopPlayPad        = 0xFF;
static uint8_t  s_clearPad           = 0xFF;
static bool     s_lastRecState       = false;
static bool     s_lastLoopPlayState  = false;
static bool     s_lastClearState     = false;
static uint32_t s_clearPressStart    = 0;
static bool     s_clearFired         = false;
static const uint32_t CLEAR_LONG_PRESS_MS = 500;   // spec LOOP §9 CLEAR long-press default
```

**Zone 4 — BankSlot init loop addition (ligne ~552, après `s_banks[i].arpEngine = nullptr;`)** :

```cpp
    s_banks[i].arpEngine          = nullptr;
    s_banks[i].loopEngine         = nullptr;   // <-- NEW Phase 2 : init explicite (audit GAP #5 archive)
    s_banks[i].isForeground       = false;
    // ... rest unchanged ...
```

**Zone 5 — setup() LoopEngine assignment loop (après le NVS loadAll + ArpEngine assignment, avant la fin de setup())** :

```cpp
  // =================================================================
  // Assign LoopEngines to LOOP banks (Phase 2)
  // =================================================================
  // Reads loop quantize mode from NvsManager (stub Phase 2 : retourne toujours
  // LOOP_QUANT_FREE ; Phase 3 câblera la vraie lecture après tranchage P5).
  // Loop over all banks ; assign up to MAX_LOOP_BANKS engines.
  {
    uint8_t loopIdx = 0;
    for (uint8_t i = 0; i < NUM_BANKS && loopIdx < MAX_LOOP_BANKS; i++) {
      if (s_banks[i].type == BANK_LOOP) {
        s_loopEngines[loopIdx].begin(s_banks[i].channel);
        s_loopEngines[loopIdx].setPadOrder(s_padOrder);
        s_loopEngines[loopIdx].setLoopQuantizeMode(
          s_nvsManager.getLoadedLoopQuantizeMode(i)
        );
        s_banks[i].loopEngine = &s_loopEngines[loopIdx];
        loopIdx++;
      }
    }
    #if DEBUG_SERIAL
    Serial.printf("[LOOP] %u LoopEngine(s) allocated to LOOP banks\n", loopIdx);
    #endif
  }

  // -----------------------------------------------------------------
  // Test config override (Phase 2 only — retiré Phase 3)
  // -----------------------------------------------------------------
  #if LOOP_TEST_ENABLED
  {
    static_assert(LOOP_TEST_BANK < NUM_BANKS, "LOOP_TEST_BANK out of range");
    static_assert(MAX_LOOP_BANKS >= 1, "Need at least 1 LoopEngine for test");
    // Force bank LOOP_TEST_BANK as BANK_LOOP for testing
    if (s_banks[LOOP_TEST_BANK].type != BANK_LOOP) {
      s_banks[LOOP_TEST_BANK].type = BANK_LOOP;
      // Re-assign the first LoopEngine to this bank (assumes < MAX_LOOP_BANKS banks were already LOOP type from NVS)
      if (!s_banks[LOOP_TEST_BANK].loopEngine) {
        s_loopEngines[0].begin(s_banks[LOOP_TEST_BANK].channel);
        s_loopEngines[0].setPadOrder(s_padOrder);
        s_loopEngines[0].setLoopQuantizeMode(LOOP_QUANT_FREE);   // test = no quantize
        s_banks[LOOP_TEST_BANK].loopEngine = &s_loopEngines[0];
      }
    }
    s_recPad      = LOOP_TEST_REC_PAD;
    s_loopPlayPad = LOOP_TEST_PLAYSTOP_PAD;
    s_clearPad    = LOOP_TEST_CLEAR_PAD;
    #if DEBUG_SERIAL
    Serial.printf("[LOOP TEST] bank %u → LoopEngine, pads REC=%u PLAYSTOP=%u CLEAR=%u\n",
                  LOOP_TEST_BANK, s_recPad, s_loopPlayPad, s_clearPad);
    #endif
  }
  #endif
```

**Steps** :

- [ ] **Step 7.1 — Création `src/loop/LoopTestConfig.h`** : Write avec contenu ci-dessus (15 lignes).

- [ ] **Step 7.2 — Lecture `src/main.cpp` lignes 1-90 + 540-560** : identifier zones exactes pour includes, statics, BankSlot init loop. Confirmer ordre `s_arpEngines` puis `s_loopEngines` (cohérent avec le reste).

- [ ] **Step 7.3 — Lecture `src/main.cpp` setup() lignes ~700-810** : identifier emplacement précis pour LoopEngine assignment (après NVS loadAll + ArpEngine assignment, avant `dumpBanksGlobal()` ou autre côté boot).

- [ ] **Step 7.4 — Édition `src/main.cpp` Zone 1 (includes)** : ajouter les 2 lignes include LoopEngine + LoopTestConfig.

- [ ] **Step 7.5 — Édition `src/main.cpp` Zone 2 (s_loopEngines array)** : ajouter `static LoopEngine s_loopEngines[MAX_LOOP_BANKS];` après s_arpEngines.

- [ ] **Step 7.6 — Édition `src/main.cpp` Zone 3 (control pad statics)** : ajouter les 9 lignes statics LOOP.

- [ ] **Step 7.7 — Édition `src/main.cpp` Zone 4 (BankSlot init)** : ajouter `s_banks[i].loopEngine = nullptr;` dans la loop init.

- [ ] **Step 7.8 — Édition `src/main.cpp` Zone 5 (setup() assignment + test override)** : insérer le bloc assignment + le bloc `#if LOOP_TEST_ENABLED` à l'endroit identifié Step 7.3.

- [ ] **Step 7.9 — Compile gate** : `pio run`, exit 0. Vérifier output build : RAM augmente d'environ **~41 KB** (4 × LoopEngine ~10.3 KB chacun, audit M3 recalcul — PendingNote 16 B padded) + ~2 KB code. Le total RAM passe de ~16% (baseline Phase 1 close) à **~30%**. Le `static_assert(sizeof(LoopEngine) <= 11000)` dans LoopEngine.h Task 2 garantit qu'on ne dépasse pas le contrat ; si compile échoue ici, audit le footprint avant de continuer (Budget Philosophy CLAUDE.md projet).

- [ ] **Step 7.10 — Static read-back** :
  - `grep -n "s_loopEngines\|s_recPad\|s_loopPlayPad\|s_clearPad" src/main.cpp` → présents.
  - `grep -n "loopEngine = nullptr\|loopEngine = &s_loop" src/main.cpp` → init nullptr + assignment loop trouvés.
  - `grep -n "LOOP_TEST_BANK\|LOOP_TEST_REC_PAD" src/main.cpp` → présents sous `#if LOOP_TEST_ENABLED`.

- [ ] **Step 7.11 — HW Checkpoint B (BLOQUANT — user autorise upload)** :
  - Upload sur autorisation explicite user : `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload`.
  - Boot : 8 progressive LEDs OK.
  - Serial monitor (`pio device monitor -b 115200`) : voir `[LOOP] 1 LoopEngine(s) allocated to LOOP banks` (ou 0 si NVS BankTypeStore n'a aucune bank LOOP — normal Phase 2 vu que Tool 5 ne propose pas LOOP en cycle) puis `[LOOP TEST] bank 6 → LoopEngine, pads REC=47 PLAYSTOP=46 CLEAR=45`.
  - Navigation banks 1-8 : bank 7 (visible musicien, index 6) doit afficher le stub `renderBankLoop` Phase 1 — jaune solide dim (CSLOT_MODE_LOOP à 25% baseline). Banks 1-6 + bank 8 inchangées (NORMAL/ARPEG/ARPEG_GEN selon config NVS).
  - Bank switch NORMAL ↔ ARPEG ↔ ARPEG_GEN inchangé (Task 0 Task 4 LOOP P1 ne casse rien).
  - Scale change root/mode/chrom + octave ARPEG : inchangé (Task 0 ScaleManager early-return concerne seulement BANK_LOOP).
  - Aucune note bloquée, aucun MIDI parasite ch7 (le LoopEngine est allocué mais aucun pad input ne va encore vers processLoopMode — Task 8).
  - **Si HW-B fail** : ne pas commit. Diagnostiquer (RAM out, SRAM corruption, LoopEngine.begin segfault…) puis retest.
  - **Si HW-B OK** : autoriser commit Step 7.12.

- [ ] **Step 7.12 — Commit (après HW-B OK explicite user)** :
  - Files : `src/loop/LoopTestConfig.h` (nouveau), `src/main.cpp`.
  - Message proposé :
    ```
    feat(loop): phase 2 task 7 — LoopTestConfig + main statics + setup() assignment

    LoopTestConfig.h : LOOP_TEST_ENABLED=1, LOOP_TEST_BANK=6 (bank 7 musicien,
    channel 7), pads REC=47 / PLAYSTOP=46 / CLEAR=45 (validés HW non-conflict
    drumming vs 30/31/32 problématiques — roadmap Q3). À retirer Phase 3.

    main.cpp :
    - include LoopEngine.h + LoopTestConfig.h
    - static LoopEngine s_loopEngines[MAX_LOOP_BANKS=4] (~37.6 KB SRAM)
    - statics control pads (s_recPad/s_loopPlayPad/s_clearPad + edge state +
      s_clearPressStart/Fired + CLEAR_LONG_PRESS_MS=500)
    - BankSlot init loop : s_banks[i].loopEngine = nullptr (audit GAP #5
      archive — init explicite pour cohérence avec arpEngine)
    - setup() : boucle assign LoopEngines aux banks LOOP (begin/setPadOrder/
      setLoopQuantizeMode via NvsManager stub) + Serial debug count
    - setup() #if LOOP_TEST_ENABLED : override bank 6 → BANK_LOOP + assign
      s_loopEngines[0] + seed pads test + Serial debug

    HW Checkpoint B validé : boot OK, LoopEngine allocué, stub renderBankLoop
    visible bank 7 (jaune dim), aucune régression NORMAL/ARPEG/ARPEG_GEN,
    aucun MIDI parasite ch7 (processLoopMode pas encore câblé — Task 8).

    RAM impact : ~16% → ~30% (+~14% pour 4 × ~10.3 KB LoopEngine — audit M3 recalcul).
    Flash impact : ~21.7% → ~23% (+~1.3% pour code LoopEngine).
    Contrat figé par static_assert(sizeof(LoopEngine) <= 11000) dans LoopEngine.h.

    Refs : spec LOOP §6, §22 (Phase 4). Roadmap Q3 (pads test 47/46/45).
    ```

---

## Task 8 — main.cpp processLoopMode + handlePadInput case + handleLeftReleaseCleanup LOOP branch

**Cross-refs** : spec LOOP §3 (LOOP core), §14 (multi-bank — engines tick en background), §15 (cohabitation NORMAL/ARPEG/Control). Spec gesture §27 (buffer sacré — processLoopMode falling edge n'écrit pas dans _events[] hors REC/OD), §28 anti-pattern B1 (sweep release LEFT idempotent), §29 (recordNoteOn = action 1 autorisée). Archive Steps 5a + 7a + 10b.

**Files** :
- Modify : `src/main.cpp` — ajouter `processLoopMode()`, ajouter `case BANK_LOOP:` dans `handlePadInput` switch (ligne ~973-983), ajouter branche `else if (relSlot.type == BANK_LOOP && relSlot.loopEngine)` dans `handleLeftReleaseCleanup` (ligne ~946-965).

**Cible diff `main.cpp` — Zone A : nouveau processLoopMode (à insérer après processArpMode, ~ligne 945)** :

```cpp
// =================================================================
// processLoopMode — pad input dispatch for LOOP banks
// =================================================================
// Handles live play via refcount (live press does not retrigger a loop note
// already sounding) and routes presses into the engine's record buffer when
// state is RECORDING or OVERDUBBING.
//
// AUDIT FIX (B1, 2026-04-06) : utilise setLiveNote() (après refcount
// increment) et releaseLivePad() (qui handle refcount decrement + MIDI
// noteOff internally). Le release path devient symétrique avec
// handleLeftReleaseCleanup sweep qui utilise aussi releaseLivePad().
//
// Spec gesture §27 + §29 : recordNoteOn/Off (uniquement RECORDING/OVERDUBBING)
// sont la SEULE modification autorisée de _events[] depuis processLoopMode.
// LEFT release / falling edge musical / autres états : ne touchent jamais
// le buffer (anti-pattern F8 spec gesture).

static void processLoopMode(const SharedKeyboardState& state, BankSlot& slot,
                            uint32_t now) {
    (void)now;   // unused — kept for signature symmetry with processArpMode
    LoopEngine* eng = slot.loopEngine;
    if (!eng) return;

    LoopEngine::State ls = eng->getState();

    for (uint8_t p = 0; p < NUM_KEYS; p++) {
        // Skip LOOP control pads — handled by handleLoopControls (Task 9).
        // Spec LOOP §5 layer musical : REC/PLAYSTOP/CLEAR ne jouent pas
        // musicalement sur bank LOOP FG.
        if (p == s_recPad || p == s_loopPlayPad || p == s_clearPad) continue;

        bool pressed    = state.keyIsPressed[p];
        bool wasPressed = s_lastKeys[p];

        if (pressed && !wasPressed) {
            // --- Rising edge : play live note + record si REC/OD ---
            uint8_t note = eng->padToNote(p);
            if (note == 0xFF) continue;   // unmapped pad — skip silently

            uint8_t vel = slot.baseVelocity;
            if (slot.velocityVariation > 0) {
                int16_t range  = (int16_t)slot.velocityVariation * 127 / 200;
                int16_t offset = (int16_t)random(-range, range + 1);
                vel = (uint8_t)constrain((int16_t)vel + offset, 1, 127);
            }
            // Refcount : only send MIDI noteOn on 0→1
            if (eng->noteRefIncrement(note)) {
                s_transport.sendNoteOn(slot.channel, note, vel);
            }
            // B1 fix : track per-pad live note (idempotent sweep cleanup
            // pour handleLeftReleaseCleanup LOOP branche ci-dessous).
            eng->setLiveNote(p, note);

            // Record during RECORDING or OVERDUBBING — spec gesture §29 action 1
            if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
                eng->recordNoteOn(p, vel);
            }
        } else if (!pressed && wasPressed) {
            // --- Falling edge : release live (refcount decrement via
            //     releaseLivePad which handles MIDI noteOff internally
            //     and clears _liveNote[p]) ---
            // B1 fix : releaseLivePad est idempotent — no-op si _liveNote[p]
            // == 0xFF (e.g. pad relâché pendant sweep antérieur). Pas de
            // padToNote re-resolve, pas de noteRefDecrement manuel.
            eng->releaseLivePad(p, s_transport);

            // Record noteOff during RECORDING or OVERDUBBING — spec gesture §29
            if (ls == LoopEngine::RECORDING || ls == LoopEngine::OVERDUBBING) {
                eng->recordNoteOff(p);
            }
            // Spec gesture §28 anti-pattern F8 : pas d'action sur _events[]
            // pour EMPTY/PLAYING/STOPPED en falling edge.
        }
    }
}
```

**Différences clés avec processNormalMode** (à documenter dans le commit body) :
- LOOP bypasse `MidiEngine` — utilise `s_transport.sendNoteOn()` direct (pas de ScaleResolver, pas de pitch musical, percussion fixe offset 36).
- Press path : refcount manuel + sendNoteOn + `setLiveNote` pour idempotent tracking.
- Release path : un seul call à `releaseLivePad()` — miroir `MidiEngine::noteOff()`.
- Refcount empêche duplicate noteOn / premature noteOff quand live play coïncide avec loop playback.
- `padToNote()` retourne 0xFF pour unmapped pads — skip à press time (release path handle ça automatiquement via `_liveNote[p] == 0xFF`).

**Cible diff `main.cpp` — Zone B : handlePadInput switch (ligne ~973-983)** :

Lire le switch actuel d'abord pour confirmer la structure exacte. D'après l'inventaire :
- Ligne 974: `processNormalMode(state, slot);` (case BANK_NORMAL)
- Ligne 978: `if (slot.arpEngine) processArpMode(state, slot, now);` (case BANK_ARPEG / BANK_ARPEG_GEN probablement avec `case BANK_ARPEG: case BANK_ARPEG_GEN:`)
- Ligne 981: commentaire `// BANK_LOOP : Phase 1 LOOP wires processLoopMode here`

Adapter le commentaire en code actif :

```cpp
  switch (slot.type) {
    case BANK_NORMAL:
      processNormalMode(state, slot);
      break;
    case BANK_ARPEG:
    case BANK_ARPEG_GEN:
      if (slot.arpEngine) processArpMode(state, slot, now);
      break;
    case BANK_LOOP:                                               // <-- WAS: comment placeholder
      if (slot.loopEngine) processLoopMode(state, slot, now);
      break;
    default:
      break;
  }
```

**Cible diff `main.cpp` — Zone C : handleLeftReleaseCleanup LOOP branch (ligne ~946-965)** :

Lire la fonction actuelle avant édition pour confirmer signature `if/else if` chain. D'après audit V1/D1 archive : c'est bien un `if/else if (relSlot.type == BANK_NORMAL/BANK_ARPEG)`, pas un switch. Ajouter la 3ème branche après ARPEG :

```cpp
  } else if (relSlot.type == BANK_LOOP && relSlot.loopEngine) {
    // AUDIT FIX (B1, 2026-04-06) + (D1/V1, 2026-04-06 pass 2) :
    // Spec gesture §27 + §28 : sweep release LEFT idempotent qui ne touche
    // pas _events[]. releaseLivePad est no-op pour pads avec _liveNote[]
    // déjà 0xFF (e.g. release pendant la frame de switch). Mirror du sweep
    // NORMAL via MidiEngine::noteOff() qui utilise _lastResolvedNote[].
    //
    // Pourquoi cette branche ? Pendant un hold (isHolding()), processLoopMode
    // n'est PAS appelé — donc les pads relâchés pendant le hold n'ont pas
    // leur falling edge traitée. Sans ce sweep, leur refcount reste > 0
    // et la loop devient silencieuse sur ces notes. Le sweep finds les
    // pads avec _liveNote[i] != 0xFF (set au rising pré-hold) et les
    // release proprement.
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
      if (!state.keyIsPressed[i]) {
        relSlot.loopEngine->releaseLivePad(i, s_transport);
      }
    }
  }
```

**Steps** :

- [ ] **Step 8.1 — Lecture `main.cpp` lignes 940-985** : confirmer signature exacte de `handleLeftReleaseCleanup` (if/else if chain avec `relSlot` variable) et `handlePadInput` switch + commentaire BANK_LOOP placeholder.

- [ ] **Step 8.2 — Insertion `processLoopMode` après `processArpMode` (~ligne 945)** : ~65 lignes avec commentaires B1 + spec gesture refs.

- [ ] **Step 8.3 — Édition `handlePadInput` switch (~ligne 981)** : remplacer le commentaire placeholder par `case BANK_LOOP: if (slot.loopEngine) processLoopMode(state, slot, now); break;`. Vérifier que `default: break;` reste présent.

- [ ] **Step 8.4 — Édition `handleLeftReleaseCleanup` (~ligne 946-965)** : insérer la branche `else if (relSlot.type == BANK_LOOP && relSlot.loopEngine)` après la branche ARPEG, avec commentaires B1 + V1/D1 + spec gesture §27.

- [ ] **Step 8.5 — Compile gate** : `pio run`, exit 0. Si `random()` / `constrain()` ne sont pas reconnus dans `processLoopMode` (Arduino macros), vérifier include `<Arduino.h>` ou utiliser std::rand + std::clamp si compile pure-C++17.

- [ ] **Step 8.6 — Static read-back** :
  - `grep -n "processLoopMode" src/main.cpp` → 2 occurrences (définition + appel dans handlePadInput switch).
  - `grep -n "releaseLivePad" src/main.cpp src/loop/LoopEngine.cpp` → 3+ occurrences (définition LoopEngine + 2 sites main : processLoopMode falling + handleLeftReleaseCleanup LOOP branch).
  - `grep -n "_events\[\]\|_events\[" src/main.cpp` → 0 résultat (main.cpp ne touche jamais `_events[]` direct — invariant buffer sacré).

- [ ] **Step 8.7 — HW gate préliminaire (optionnel — pas un checkpoint bloquant)** :
  - Si user autorise upload intermédiaire avant Task 9 : presser un pad musical (pas REC/PLAYSTOP/CLEAR) sur bank 7 → MIDI noteOn ch7 attendu (sans loop encore : pas de PLAY/STOP UI, pas de REC). Release → MIDI noteOff.
  - Hold LEFT pendant que pad pressé puis release pad puis release LEFT → noteOff attendu via handleLeftReleaseCleanup LOOP branch.
  - Spec gesture §27 vérification : `_events[]` ne doit jamais être touché (vérifier via Serial logs en l'absence de REC tap).
  - **Décision** : ce gate est INFORMATIF, pas bloquant. Task 9 fournira le vrai HW Checkpoint C.

- [ ] **Step 8.8 — Commit** :
  - File : `src/main.cpp` (modifié).
  - Message proposé :
    ```
    feat(loop): phase 2 task 8 — main processLoopMode + handlePadInput LOOP + sweep release

    processLoopMode : pad input dispatch pour BANK_LOOP. Skip control pads
    (s_recPad/s_loopPlayPad/s_clearPad — Task 9 handleLoopControls). Rising
    edge → padToNote + refcount increment + sendNoteOn direct (bypass
    MidiEngine, no ScaleResolver) + setLiveNote (B1 idempotent tracking) +
    recordNoteOn si RECORDING/OVERDUBBING. Falling edge → releaseLivePad
    (handle refcount decrement + sendNoteOff internally + clear _liveNote) +
    recordNoteOff si REC/OD. EMPTY/PLAYING/STOPPED falling edge : no-op
    (spec gesture §28 anti-pattern F8 évité).

    handlePadInput switch : remplace commentaire placeholder par case
    BANK_LOOP appelant processLoopMode. BANK_ARPEG et BANK_ARPEG_GEN
    inchangés (cohabitation §15 spec LOOP).

    handleLeftReleaseCleanup : branche else if (relSlot.type == BANK_LOOP &&
    relSlot.loopEngine) ajoutée après ARPEG. Sweep idempotent via releaseLivePad
    sur tous les pads non-pressés — B1 fix essentiel : sans ce sweep, les
    pads relâchés pendant un hold ont leur refcount stuck > 0 et la loop
    devient silencieuse sur ces notes. V1/D1 fix : adapter au if/else if
    pattern existant (pas un switch).

    Aucune action sur _events[] dans main.cpp — invariant "buffer LOOP
    sacré" (spec gesture §27) préservé : recordNoteOn/Off sont les seules
    modifications via LoopEngine API, et uniquement en REC/OD.

    Refs : spec LOOP §3, §14, §15. Spec gesture §27, §28 F8, §29 action 1.
    AUDIT FIXES : B1 (live tracking + sweep idempotent), V1/D1 (if/else if).
    ```

---

## Task 9 — main.cpp handleLoopControls + loop() tick wiring + reloadPerBankParams + pushParamsToLoop

**Cross-refs** : spec LOOP §7-§9 (REC/PLAY-STOP/CLEAR semantics), §10 (effets pots), §14 (multi-bank background tick), §17 (quantize tap simple vs double-tap bypass), §22 (pot routing). Spec gesture §31 décision #1 (layer musical LOOP indépendant — acté §0.2 P6). Archive Steps 6 + 8 + 9.

**Files** :
- Modify : `src/main.cpp` — ajouter `handleLoopControls()`, ajouter appel `handleLoopControls(state, now)` dans `loop()` après `handlePlayStopPad`, ajouter boucle tick/processEvents par bank LOOP dans `loop()` après `s_arpScheduler.processEvents()`, ajouter branche LOOP dans `reloadPerBankParams`, ajouter `pushParamsToLoop()` et l'appeler après `pushParamsToEngine`.

**Cible diff `main.cpp` — Zone A : nouveau handleLoopControls (à insérer après `handlePlayStopPad` ou autre handler similaire)** :

```cpp
// =================================================================
// handleLoopControls — REC / PLAY-STOP / CLEAR pad edge detection
// =================================================================
// Spec LOOP §7-§9 : 3 control pads sur layer musical, actifs uniquement
// quand slot.type == BANK_LOOP en foreground. Spec gesture §31 décision #1
// (acté roadmap §5 P6) : layer musical LOOP indépendant — early-return
// si !BANK_LOOP préserve l'orthogonalité avec auto-Play §13.2 ARPEG.
//
// State transition table (toutes transitions respectent loopQuantize per-
// bank — sauf abort et clear qui sont always-immediate) :
//
// | Pad | Current State    | Action                          | Quantizable |
// |-----|------------------|---------------------------------|-------------|
// | REC | EMPTY            | startRecording()                | YES         |
// | REC | RECORDING        | stopRecording() + bar-snap      | YES         |
// | REC | PLAYING          | startOverdub()                  | YES         |
// | REC | OVERDUBBING      | stopOverdub() + merge           | YES         |
// | REC | STOPPED          | ignored                         | -           |
// | P/S | PLAYING          | stop() + soft flush             | YES         |
// | P/S | OVERDUBBING      | abortOverdub() + hard flush     | NO (abort)  |
// | P/S | STOPPED          | play()                          | YES         |
// | P/S | EMPTY/RECORDING  | ignored                         | -           |
// | CLR | OVERDUBBING (500ms hold) | cancelOverdub() (undo overdub) | NO (human q) |
// | CLR | * other except EMPTY (500ms hold) | clear() + hard flush | NO        |

static void handleLoopControls(const SharedKeyboardState& state, uint32_t now) {
    BankSlot& slot = s_bankManager.getCurrentSlot();
    if (slot.type != BANK_LOOP || !slot.loopEngine) {
        // Pas une bank LOOP FG — reset edge states (les pads jouent comme
        // music sur les autres types). Spec §15 cohabitation NORMAL/ARPEG.
        s_lastRecState      = (s_recPad < NUM_KEYS)
                                ? state.keyIsPressed[s_recPad] : false;
        s_lastLoopPlayState = (s_loopPlayPad < NUM_KEYS)
                                ? state.keyIsPressed[s_loopPlayPad] : false;
        s_lastClearState    = (s_clearPad < NUM_KEYS)
                                ? state.keyIsPressed[s_clearPad] : false;
        return;
    }

    LoopEngine* eng = slot.loopEngine;
    LoopEngine::State ls = eng->getState();

    // --- REC pad : simple tap edge ---
    // AUDIT B2 (2026-05-16) : si ls == WAITING_PENDING (FREE only en Phase 2,
    // donc unreachable Phase 2 mais code correct par review), gérer le cas
    // §17 spec "Tap REC pendant WAITING_STOP = annule STOP + entre
    // OVERDUBBING". Pour le tap REC pendant WAITING_PLAY ou WAITING_START_*,
    // spec §17 indique "ignoré" (le tap est absorbé). Comme on a un seul
    // WAITING_PENDING générique, on doit consulter hasPendingAction() et
    // type d'action via un futur getter (Phase 3 le rendra public). Phase 2 :
    // unreachable in FREE mode, branche pour la complétude code-review only.
    if (s_recPad < NUM_KEYS) {
        bool pressed = state.keyIsPressed[s_recPad];
        if (pressed && !s_lastRecState) {
            switch (ls) {
                case LoopEngine::EMPTY:
                    eng->startRecording();
                    break;
                case LoopEngine::RECORDING:
                    eng->stopRecording(state.keyIsPressed, s_padOrder,
                                       s_clockManager.getSmoothedBPMFloat());
                    break;
                case LoopEngine::PLAYING:
                    eng->startOverdub();
                    break;
                case LoopEngine::OVERDUBBING:
                    eng->stopOverdub(state.keyIsPressed, s_padOrder,
                                     s_clockManager.getSmoothedBPMFloat());
                    break;
                case LoopEngine::WAITING_PENDING:
                    // Spec §17 : si pending = PENDING_STOP, tap REC annule STOP
                    // + entre OVERDUBBING. Sinon ignored. Phase 2 unreachable
                    // (FREE only). Phase 3 implémente quand BEAT/BAR câblés —
                    // nécessitera un nouveau LoopEngine API getter pour exposer
                    // _pendingAction type, ou une méthode dédiée
                    // eng->cancelPendingStopAndStartOverdub().
                    break;
                default: break;   // STOPPED : REC ignored Phase 2
                                  // (STOPPED-loaded + tap REC = PLAYING + OD
                                  //  simultanés Q5 spec §28 — Phase 6 quand
                                  //  slot load existe)
            }
        }
        s_lastRecState = pressed;
    }

    // --- PLAY/STOP pad : simple tap edge ---
    // Pas de triggerConfirm Phase 2 — LED Phase 4 décidera des confirms.
    // AUDIT B2 + N5 (2026-05-16) :
    //   - PLAYING + tap PLAY/STOP : stop() quantizable (FREE = immédiat).
    //   - OVERDUBBING + tap PLAY/STOP : abortOverdub() → retour PLAYING
    //     (audit N5 fix, spec §8). PAS STOPPED. 2e tap PLAY/STOP requis pour
    //     stopper.
    //   - STOPPED + tap PLAY/STOP : play() quantizable.
    //   - WAITING_PENDING + tap PLAY/STOP : annule la pending action sans
    //     en set une nouvelle (audit B2 spec §17 "Tap PLAY/STOP hors
    //     doubleTapMs pendant WAITING_PLAY → Annule, retour STOPPED").
    //     Phase 2 unreachable (FREE only).
    if (s_loopPlayPad < NUM_KEYS) {
        bool pressed = state.keyIsPressed[s_loopPlayPad];
        if (pressed && !s_lastLoopPlayState) {
            switch (ls) {
                case LoopEngine::PLAYING:
                    eng->stop(s_transport);          // quantizable soft stop
                    break;
                case LoopEngine::OVERDUBBING:
                    eng->abortOverdub(s_transport);  // → PLAYING (N5 fix)
                    break;
                case LoopEngine::STOPPED:
                    eng->play(s_clockManager.getSmoothedBPMFloat());   // quantizable
                    break;
                case LoopEngine::WAITING_PENDING:
                    // Spec §17 : annule pending sans en set une nouvelle.
                    // Phase 2 unreachable (FREE only). Phase 3 nécessitera
                    // un eng->cancelPendingAction() API pour clear
                    // _pendingAction sans transition. Pour Phase 2 review :
                    // hypothèse d'absence de WAITING_PENDING runtime OK.
                    break;
                default: break;   // EMPTY, RECORDING : ignored
            }
        }
        s_lastLoopPlayState = pressed;
    }

    // --- CLEAR pad : long press (500ms) ---
    // ALWAYS immediate (no quantize snap — le 500ms hold IS la "human quantize").
    // Behavior at ramp completion :
    //   OVERDUBBING → cancelOverdub() (undo overdub pass only, loop preserved)
    //   * other except EMPTY → clear() (hard flush, state → EMPTY)
    //
    // Edge state s_lastClearState updated INDEPENDANTLY of ls != EMPTY guard
    // (cf audit B-CODE) — sans ça, un CLEAR tenu pendant EMPTY → RECORDING
    // transition produirait un faux rising edge et démarrerait le clear
    // timer mid-recording.
    bool clearPressed = (s_clearPad < NUM_KEYS)
                          ? state.keyIsPressed[s_clearPad] : false;
    if (s_clearPad < NUM_KEYS && ls != LoopEngine::EMPTY) {
        if (clearPressed && !s_lastClearState) {
            s_clearPressStart = now;
            s_clearFired = false;
        }
        if (clearPressed && !s_clearFired) {
            uint32_t held = now - s_clearPressStart;
            if (held >= CLEAR_LONG_PRESS_MS) {
                if (ls == LoopEngine::OVERDUBBING) {
                    eng->cancelOverdub();
                } else {
                    eng->clear(s_transport);
                }
                s_clearFired = true;
            }
            // Phase 4 LED : showClearRamp(held / CLEAR_LONG_PRESS_MS * 100)
            // sera ajouté ici quand LedController consume Phase 4.
        }
    }
    // Update edge state TOUJOURS (même si ls == EMPTY) pour éviter faux
    // rising edge sur state transition.
    s_lastClearState = clearPressed;
}
```

**Cible diff `main.cpp` — Zone B : reloadPerBankParams branche LOOP (~ligne 990, après ARPEG block)** :

```cpp
  // Existing ARPEG block...
  if (newSlot.type == BANK_ARPEG && newSlot.arpEngine) {
      // ... gateLength + shuffle + ... ...
  }

  // NEW Phase 2 : LOOP branch — read shuffle/template depuis LoopEngine
  if (newSlot.type == BANK_LOOP && newSlot.loopEngine) {
      shufDepth = newSlot.loopEngine->getShuffleDepth();
      shufTmpl  = newSlot.loopEngine->getShuffleTemplate();
      // chaos / velPattern / velPatternDepth : pas encore exposés au PotRouter
      // Phase 2 (Phase 4 ajoutera les targets pot LOOP). Stubs Phase 5 dans
      // LoopEngine — pas de read ici sans target.
  }
```

**Cible diff `main.cpp` — Zone C : nouveau pushParamsToLoop (à insérer après `pushParamsToEngine`, ~ligne 1163)** :

```cpp
// Push PotRouter live values into LoopEngine setters. Spec LOOP §22 (pot
// routing) + §10 (effets). Phase 2 : seulement les params partagés avec
// ARPEG (shuffleDepth/Template) + baseVelocity/velocityVariation (déjà
// dans BankSlot). chaos/velPattern viennent Phase 4 (PotRouter targets LOOP).
static void pushParamsToLoop(BankSlot& slot) {
    if (slot.type != BANK_LOOP || !slot.loopEngine) return;
    slot.loopEngine->setShuffleDepth(s_potRouter.getShuffleDepth());
    slot.loopEngine->setShuffleTemplate(s_potRouter.getShuffleTemplate());
    slot.loopEngine->setBaseVelocity(s_potRouter.getBaseVelocity());
    slot.loopEngine->setVelocityVariation(s_potRouter.getVelocityVariation());
}
```

**Cible diff `main.cpp` — Zone D : handlePotPipeline (~ligne 1184), ajout appel pushParamsToLoop** :

```cpp
  pushParamsToEngine(potSlot);     // existing — ARPEG
  pushParamsToLoop(potSlot);       // <-- NEW Phase 2 : LOOP
```

**Cible diff `main.cpp` — Zone E : loop() execution order (~ligne 1460-1530)** :

Deux insertions. **AUDIT FIX B1 (2026-05-16) — il n'y a PAS de `handlePlayStopPad` dans le code main**. L'ordre réel `loop()` est `handleManagerUpdates → handleHoldPad → s_controlPadManager.update → handlePadInput → sync s_lastKeys → s_arpScheduler.tick/processEvents → s_midiEngine.flush → handlePotPipeline`. Le placement naturel pour `handleLoopControls` est **entre `s_controlPadManager.update` et `handlePadInput`** : le contrôle des pads LOOP doit consommer les edges AVANT que `processLoopMode` (appelé depuis `handlePadInput`) ne traite les pads musicaux du même frame.

**E1. Appel `handleLoopControls` dans `loop()` entre `s_controlPadManager.update` (ligne 1510-1511) et `handlePadInput` (ligne 1513)** :

```cpp
  // --- Control pads (step 7b) : after bank switch + hold pad, before music block ---
  s_controlPadManager.update(state, leftHeld,
                             s_bankManager.getCurrentSlot().channel);

  handleLoopControls(state, now);                              // <-- NEW Phase 2

  handlePadInput(state, now);

  // Always sync edge state — prevents ghost notes when button releases
  for (int i = 0; i < NUM_KEYS; i++) {
    s_lastKeys[i] = state.keyIsPressed[i];
  }
```

**E2. Boucle tick/processEvents LOOP banks entre `s_arpScheduler.processEvents()` (ligne 1522) et `s_midiEngine.flush()` (ligne 1525)** :

```cpp
  // --- ArpScheduler: dispatch clock ticks to all active arps ---
  s_arpScheduler.tick();
  s_arpScheduler.processEvents();  // Fire pending gate noteOff + shuffled noteOn

  // LOOP engines: tick + processEvents (all banks, not just foreground).
  // Spec LOOP §14 : banks LOOP en background continuent à jouer. tick()
  // reçoit globalTick (de ClockManager) pour le pending action dispatcher
  // (quantize boundary crossing). Pas de scheduler global — chaque
  // LoopEngine maintient son cursor microsecond-based.
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
```

**Steps** :

- [ ] **Step 9.1 — Lecture `main.cpp` lignes 985-1015 (`reloadPerBankParams`)** : confirmer structure if-cascade existing.

- [ ] **Step 9.2 — Lecture `main.cpp` lignes 1145-1200 (`pushParamsToEngine` + `handlePotPipeline`)** : confirmer signatures et emplacement appel.

- [ ] **Step 9.3 — Lecture `main.cpp` lignes 1460-1520 (`loop()` execution order)** : identifier emplacement exact des appels `handlePlayStopPad`, `handlePadInput`, `s_arpScheduler.tick()`, `s_arpScheduler.processEvents()`, `s_midiEngine.flush()`.

- [ ] **Step 9.4 — Lecture `s_clockManager` API** : confirmer méthodes `getCurrentTick()` (`uint32_t`) et `getSmoothedBPMFloat()` (`float`). Si signatures différentes, adapter.

- [ ] **Step 9.5 — Lecture `s_potRouter` API** : confirmer méthodes `getShuffleDepth()` / `getShuffleTemplate()` / `getBaseVelocity()` / `getVelocityVariation()`. Si différent (ex: paramétré par slot), adapter `pushParamsToLoop`.

- [ ] **Step 9.6 — Insertion `handleLoopControls`** : ~110 lignes après l'autre handler similaire.

- [ ] **Step 9.7 — Édition `reloadPerBankParams`** : ajouter branche `if (newSlot.type == BANK_LOOP && newSlot.loopEngine)` après bloc ARPEG.

- [ ] **Step 9.8 — Insertion `pushParamsToLoop`** : ~10 lignes après `pushParamsToEngine`.

- [ ] **Step 9.9 — Édition `handlePotPipeline`** : ajouter `pushParamsToLoop(potSlot);` après `pushParamsToEngine(potSlot);`.

- [ ] **Step 9.10 — Édition `loop()` Zone E1** : ajouter `handleLoopControls(state, now);` **entre `s_controlPadManager.update` (ligne 1510-1511) et `handlePadInput` (ligne 1513)** — audit B1 fix. Ne PAS chercher `handlePlayStopPad`, cette fonction n'existe pas dans main.cpp.

- [ ] **Step 9.11 — Édition `loop()` Zone E2** : ajouter boucle tick/processEvents LOOP banks **entre `s_arpScheduler.processEvents()` (ligne 1522) et `s_midiEngine.flush()` (ligne 1525)**.

- [ ] **Step 9.12 — Compile gate** : `pio run`, exit 0. Vérifier RAM/Flash output (devrait être stable vs Task 7 — pas de nouveau buffer).

- [ ] **Step 9.13 — Static read-back** :
  - `grep -n "handleLoopControls\|pushParamsToLoop" src/main.cpp` → 2 occurrences chacune (def + call).
  - `grep -n "loopEngine->tick\|loopEngine->processEvents" src/main.cpp` → 1 occurrence chacun dans loop() Zone E2.
  - `grep -n "BANK_LOOP" src/main.cpp` → présent dans handlePadInput case, handleLeftReleaseCleanup branche, reloadPerBankParams branche, pushParamsToLoop guard, loop() filter, dumpBankState (déjà existant).

- [ ] **Step 9.14 — HW Checkpoint C (BLOQUANT — user autorise upload)** :
  - Upload sur autorisation explicite : `pio run -t upload`.
  - Serial monitor.
  - **Tests Phase 2 complets** (cf Test Verification archive plan Phase 2) :
    1. Boot OK, switch to bank 7 (BANK_LOOP).
    2. Tap REC pad (47) → état = RECORDING (pas de feedback LED encore — Phase 4 ; vérifier via Serial `[LOOP] state=RECORDING` si DEBUG_SERIAL ajouté en option).
    3. Tap des pads musicaux (pas 45/46/47) → noteOn/noteOff sur channel 7 visibles dans MIDI monitor (DAW, MidiMonitor.app, etc.).
    4. Tap REC pad (47) à nouveau → bar-snap, loop démarre playback. Le pattern doit se répéter audiblement.
    5. Changer le tempo (pot R1 par exemple) → playback suit proportionnellement.
    6. Tap REC pendant playback → OVERDUBBING. Taper plus de pads → overdubbed.
    7. Tap REC à nouveau → merge, retour PLAYING avec nouveau contenu.
    8. Tap PLAY/STOP (46) → silence (STOPPED).
    9. Tap PLAY/STOP → resume playback.
    10. Hold CLEAR (45) pendant 500 ms → loop cleared, retour EMPTY.
    11. Switch vers une autre bank et retour → loop continue en background (notes audibles ch7 même hors bank LOOP FG).
    12. Test cancelOverdub : REC pendant PLAYING → OVERDUBBING, tap quelques pads, hold CLEAR 500ms → undo overdub, loop original reprend.
    13. Test abortOverdub (audit N5 fix 2026-05-16) : REC pendant PLAYING → OVERDUBBING, tap pads, tap PLAY/STOP → **état = PLAYING + loop original continue audible** (overdub abandonné, pas de flush, spec §8). Pour stopper, re-tap PLAY/STOP (état PLAYING → STOPPED transition normale).
  - **Tests anti-régression** :
    - Bank NORMAL : pad press → noteOn, pitch bend pot fonctionne.
    - Bank ARPEG : Hold pad → play/stop, tick FLASH vert, scale change root/mode/chrom → confirm BLINK_FAST.
    - Bank ARPEG_GEN : pad press → walk/bonus_pile audible, R2+hold étend séquence.
    - Bank switch entre tous les types : aucune note bloquée, allNotesOff côté NORMAL, PB reset.
  - **Tests spec gesture §32 checklist en HW** :
    - Bank LOOP en PLAYING, switch vers ARPEG, retour LOOP → buffer `_events[]` intact (les notes rejouent identiquement). Spec gesture §27 buffer sacré validé.
    - Hold LEFT pendant PLAYING + release → loop continue identique, pas de stuck note. handleLeftReleaseCleanup LOOP branch fonctionnel.
    - Stop → Play : loop identique au pre-stop. `stop()` idempotente sur buffer.
  - **Si HW-C fail** : ne pas commit. Diagnostiquer (DEBUG_SERIAL logs LoopEngine state transitions, pending dispatcher fires, recordNoteOn captures).
  - **Si HW-C OK** : autoriser commit Step 9.15.

- [ ] **Step 9.15 — Commit (après HW-C OK explicite user)** :
  - File : `src/main.cpp` (modifié).
  - Message proposé :
    ```
    feat(loop): phase 2 task 9 — handleLoopControls + loop() wiring + pot params LOOP

    handleLoopControls : edge detection REC/PLAYSTOP/CLEAR pads. Early-return
    si !BANK_LOOP FG (reset edge states) — préserve orthogonalité avec
    auto-Play §13.2 ARPEG (spec gesture §31 décision #1, acté roadmap P6).
    State machine table 11 transitions (REC: EMPTY→REC→PLAYING→OD→PLAYING,
    PLAYSTOP: PLAYING↔STOPPED + abort OD, CLEAR: 500ms hold → cancel OD ou
    clear()). s_lastClearState updated outside guard pour éviter faux rising
    edge sur state transition.

    reloadPerBankParams : branche LOOP read shuffleDepth + shuffleTemplate
    depuis LoopEngine (params partagés avec ARPEG via PotRouter slots).

    pushParamsToLoop + appel dans handlePotPipeline : push live values
    Shuffle/BaseVel/VelVariation vers LoopEngine. Chaos/VelPattern viennent
    Phase 4 (PotRouter targets LOOP).

    loop() : appel handleLoopControls entre handlePlayStopPad et
    handlePadInput. Boucle tick/processEvents per-bank LOOP après ArpScheduler
    et avant midiEngine.flush — toutes banks (FG + BG) ticent indépendamment
    via micros() cursor (§14 spec, pas de scheduler global).

    HW Checkpoint C validé — premier son MIDI LOOP complet :
    - REC → bar-snap → playback proportionnel BPM (R1 pot live tempo)
    - Overdub merge + abort + cancel
    - Clear 500ms hard flush
    - Bank switch background continuation (loop tick continue en BG)
    - Pas de régression NORMAL/ARPEG/ARPEG_GEN
    - Buffer LOOP sacré (spec gesture §27) : switch + hold release identiques

    Refs : spec LOOP §7-§10, §14, §17, §22. Spec gesture §31 #1.
    Archive intention : Steps 6, 8, 9 du plan Phase 2 loop branch.
    ```

---

## Task 10 — BankManager activate guards (recording lock + flushLiveNotes outgoing)

**Cross-refs** : spec LOOP §23 invariants 2 (bank switch refusé pendant REC/OD) + 1 (no orphan notes — flushLiveNotes outgoing). Spec gesture §27 (flushLiveNotes ne modifie pas _events[]). Archive Step 10a + 10b + 10c.

**Files** :
- Modify : `src/managers/BankManager.cpp` — ajouter `#include "../loop/LoopEngine.h"` (pour appeler `isRecording()` + `flushLiveNotes()`), ajouter recording lock guard + flushLiveNotes outgoing dans `switchToBank()`.

**Cible diff `BankManager.cpp` — Zone A : include en haut de fichier** :

```cpp
#include "../core/MidiTransport.h"
#include "../loop/LoopEngine.h"           // <-- NEW Phase 2 Task 10 : isRecording() + flushLiveNotes()
```

**Cible diff `BankManager.cpp` — Zone B : switchToBank début (~ligne 188-200)** :

Lire `switchToBank` actuel d'abord pour confirmer structure. D'après ce qu'on a vu : début par `if (newBank >= NUM_BANKS || newBank == _currentBank) return;` puis `_engine->sendPitchBend(8192); _engine->allNotesOff();` etc.

Insertion AVANT les side effects (pour deny avant tout) :

```cpp
void BankManager::switchToBank(uint8_t newBank) {
  if (newBank >= NUM_BANKS || newBank == _currentBank) return;

  // --- LOOP recording lock : deny switch while RECORDING or OVERDUBBING ---
  // Spec LOOP §23 invariant 2 : bank switch refusé pendant REC/OD pour éviter
  // l'ambiguïté "est-ce que l'enregistrement continue en fond ?" (réponse :
  // non, parce qu'on ne peut pas partir). Silent deny — pas de feedback LED
  // Phase 2 (Phase 4 ajoutera EVT_REFUSE BLINK_FAST rouge).
  //
  // AUDIT B2 (2026-05-16) : spec §17 dit aussi "Bank switch pendant WAITING_*
  // = commit immédiat + bank switch" (pas de deny). isRecording() ne couvre
  // PAS WAITING_PENDING (par design — un PLAYING + _pendingAction=PENDING_STOP
  // n'est pas RECORDING). Phase 2 hardcode LOOP_QUANT_FREE → WAITING_PENDING
  // jamais atteint en runtime → on garde le guard simple isRecording() seul.
  // Phase 3 (quand BEAT/BAR câblés) devra remplacer ce guard par :
  //   if (isRecording()) deny ; else if (hasPendingAction()) commit-immediate-then-switch
  // À acter Phase 3 conception P5 + nouveau LoopEngine API commitPending().
  if (_banks[_currentBank].type == BANK_LOOP && _banks[_currentBank].loopEngine) {
    if (_banks[_currentBank].loopEngine->isRecording()) {
      return;   // silently deny — user must close recording first
    }
  }

  // --- LOOP outgoing : flushLiveNotes (CC123 + zero refcount + reset _liveNote) ---
  // Spec LOOP §23 invariant 1 (no orphan notes). Spec gesture §27 :
  // flushLiveNotes ne touche PAS _events[] — buffer sacré préservé,
  // seul l'état live (refcount + tracking) est wipé pour la outgoing bank.
  // L'engine reste alive en background — son pending queue continue de
  // fire normalement sur _channel.
  if (_banks[_currentBank].type == BANK_LOOP && _banks[_currentBank].loopEngine && _transport) {
    _banks[_currentBank].loopEngine->flushLiveNotes(*_transport, _banks[_currentBank].channel);
  }

  uint8_t oldBank = _currentBank;
  // ... rest of switchToBank body unchanged (sendPitchBend, allNotesOff, isForeground swap, LED update, MIDI notification, debug print) ...
```

**Note** : si le code main current a déjà `_engine->sendPitchBend(8192); _engine->allNotesOff();` au tout début du body, le insert se fait avant ces lignes. À adapter à la lecture précise.

**Steps** :

- [ ] **Step 10.1 — Lecture `src/managers/BankManager.cpp` lignes 185-235** : confirmer début exact de `switchToBank` (early returns + side effects ordering).

- [ ] **Step 10.2 — Édition `BankManager.cpp` Zone A** : ajouter `#include "../loop/LoopEngine.h"` après `#include "../core/MidiTransport.h"`.

- [ ] **Step 10.3 — Édition `BankManager.cpp` Zone B** : insérer les 2 guards (recording lock + flushLiveNotes outgoing) au tout début de `switchToBank` body, après le early return `if (newBank >= NUM_BANKS || newBank == _currentBank) return;`.

- [ ] **Step 10.4 — Compile gate** : `pio run`, exit 0.

- [ ] **Step 10.5 — Static read-back** :
  - `grep -n "loopEngine->isRecording\|loopEngine->flushLiveNotes" src/managers/BankManager.cpp` → 2 occurrences (1 chacun).
  - `grep -n "#include.*LoopEngine.h" src/managers/BankManager.cpp` → 1 occurrence.

- [ ] **Step 10.6 — HW Checkpoint D (BLOQUANT — user autorise upload)** :
  - Upload sur autorisation explicite.
  - **Test recording lock** : bank 7 (LOOP), tap REC → RECORDING. Tenter bank switch via hold LEFT + bank pad. **Attendu** : silent deny, on reste bank 7, RECORDING continue. Idem pendant OVERDUBBING (tap REC pendant PLAYING puis tenter switch).
  - **Test flushLiveNotes outgoing** : bank 7 LOOP en PLAYING avec notes audibles ch7. Tap quelques pads musicaux pour avoir des live notes ch7 en plus. Switch vers bank 1 (NORMAL). **Attendu** : les live notes pad sont coupées (CC123 ch7), mais le pattern loop continue audible ch7 (engine BG tick).
  - **Test panel idempotence** : double bank switch rapide LOOP→NORMAL→LOOP, refaire bank switch pendant que le pattern joue. Pas de notes bloquées.
  - **Si HW-D fail** : diagnostiquer (lock loupé : Serial logs `[LOOP] silent deny` à ajouter si besoin ; flushLiveNotes incomplet : refcount résiduel via Serial dump).
  - **Si HW-D OK** : autoriser commit Step 10.7.

- [ ] **Step 10.7 — Mise à jour briefing + STATUS** :
  - [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) §8 Domain Entry Points : ajouter ligne `LoopEngine — src/loop/LoopEngine.h ; tick/processEvents called per-bank in loop() ; processLoopMode dispatched from handlePadInput case BANK_LOOP ; control pads via handleLoopControls`.
  - §9 invariants : ajouter invariants spec LOOP §0.4 (1-8) avec refs.
  - `STATUS.md` : ajouter section "LOOP Phase 2 — close" avec commits Tasks 0-10 (hashes à remplir au moment du commit) + tests HW-B/C/D validés.

- [ ] **Step 10.8 — Commit final Phase 2 (après HW-D OK explicite user)** :
  - Files : `src/managers/BankManager.cpp`, `docs/reference/architecture-briefing.md`, `STATUS.md`.
  - Message proposé :
    ```
    feat(loop): phase 2 task 10 — BankManager guards (recording lock + flushLiveNotes outgoing)

    switchToBank : 2 guards LOOP au début du body :
    1. Recording lock — silent deny si _banks[_currentBank].loopEngine->
       isRecording() (RECORDING ou OVERDUBBING). Spec LOOP §23 invariant 2
       (bank switch refusé pendant REC/OD). LED EVT_REFUSE Phase 4.
    2. flushLiveNotes outgoing — CC123 + zero refcount + reset _liveNote
       sur la outgoing LOOP bank. Spec LOOP §23 invariant 1 (no orphan
       notes). Spec gesture §27 : ne touche PAS _events[] — buffer sacré
       préservé, engine continue en BG (pending queue ↦ MIDI ch).

    #include "../loop/LoopEngine.h" ajouté en tête de BankManager.cpp pour
    accéder à isRecording() et flushLiveNotes().

    HW Checkpoint D validé :
    - Bank switch tentative pendant RECORDING/OVERDUBBING : silent deny OK
    - LOOP→NORMAL switch : live notes pad coupées (CC123 ch7), pattern loop
      continue audible BG
    - Double bank switch rapide LOOP↔NORMAL : pas de notes bloquées

    Briefing §8 + §9 mis à jour. STATUS.md : Phase 2 close + HW-B/C/D
    validés + commits référencés.

    Phase 2 COMPLET — prêt pour Phase 3 conception (session 2 roadmap).

    Refs : spec LOOP §23 inv 1+2. Spec gesture §27 (flushLiveNotes buffer-safe).
    Archive intention : Step 10a/b/c plan Phase 2 loop branch.
    ```

---

## §4 — Pré-merge Phase 2 → Phase 3 checklist

À cocher avant déclaration "Phase 2 complete, ready for Phase 3 conception".

### Build & static

- [ ] `pio run -e esp32-s3-devkitc-1` exit 0, no new warnings (`-Wswitch`, `-Wunused-variable`, `-Wparentheses` clean).
- [ ] Tous les `static_assert` passent : `sizeof(LoopEvent) == 8`.
- [ ] Grep des symboles présents :
  - `grep -rn "class LoopEngine" src/` → 2 occurrences (forward decl KeyboardData.h + header).
  - `grep -rn "LoopEngine::" src/loop/LoopEngine.cpp | wc -l` → ~30 méthodes définies (toutes celles déclarées dans .h).
  - `grep -rn "BANK_LOOP" src/` → présent dans BankManager.cpp (Task 0 + Task 10), ScaleManager.cpp (Task 0), main.cpp (Tasks 7/8/9), KeyboardData.h (déjà), dumpBankState.
- [ ] Grep "buffer sacré" — vérifier qu'aucune écriture `_events[*] =` n'arrive hors `recordNoteOn` / `recordNoteOff` / `doStopRecording` / `mergeOverdub` :
  - `grep -n "_events\[.*\] *=" src/loop/LoopEngine.cpp` → uniquement recordNoteOn (1 write), recordNoteOff (1 write), doStopRecording (1 write — flush held), mergeOverdub (write dans la reverse merge).

### NVS

- [ ] Aucun bump version. `BankTypeStore` reste v4, `LedSettingsStore` v7, `ColorSlotStore` v5, `SettingsStore` v11, etc.
- [ ] Aucune nouvelle entrée `NVS_DESCRIPTORS[]` (LoopPadStore + LoopPotStore déjà DECLARED Phase 1 — Phase 2 ne change rien).
- [ ] `_loadedLoopQuantize[]` RAM-only ; pas de read NVS Phase 2 (stub retourne DEFAULT). Premier boot post-flash : pas de warning supplémentaire.

### Runtime regression (validé en HW-B / C / D)

- [ ] Boot LED progressive (8 steps) inchangé.
- [ ] Bank NORMAL : pad press → noteOn ; pad release → noteOff ; AT functional ; pitch bend functional via pot.
- [ ] Bank ARPEG : Hold pad → play/stop, tick FLASH vert, scale change root/mode/chrom → confirm BLINK_FAST + scale group propagation.
- [ ] Bank ARPEG_GEN : pad press → walk + bonus_pile audible, R2+hold étend séquence, mutation level via pad oct 1-4.
- [ ] Bank LOOP (bank 7 test) :
  - Tap REC → RECORDING, tap pads → MIDI ch7 events.
  - Tap REC → bar-snap → PLAYING avec pattern audible répétant.
  - Tempo pot R1 → playback suit proportionnellement.
  - Tap REC pendant PLAYING → OVERDUBBING, overdub merge OK.
  - Tap PLAY/STOP pendant OVERDUBBING → abort, retour STOPPED (hard flush).
  - Hold CLEAR 500ms : pendant OVERDUBBING → cancelOverdub (undo) ; sinon → clear() (EMPTY).
  - Bank switch LOOP→autre→LOOP : loop continue audible en BG ; recording lock denies switch pendant REC/OD.
- [ ] Battery gauge / setup mode entry : inchangés.
- [ ] LED renderBankLoop : reste stub jaune dim (Phase 4 ajoutera state-machine rendering, tick flashes, WAITING crossfade).

### Documentation sync

- [ ] `docs/reference/architecture-briefing.md` §8 (LoopEngine entry point) + §9 (invariants §0.4) mis à jour.
- [ ] `STATUS.md` racine : section "LOOP Phase 2 — historique commits" avec Tasks 0-10 + HW-B/C/D validés.
- [ ] `docs/superpowers/LOOP_ROADMAP.md` §3 (statut session 1 = DONE + statut Phase 2 exécution close), §4 (insights Phase 2), §5 (P1-P6 résolues + nouvelles décisions pour session 2 Phase 3 conception : P5 loopQuantize storage tranchage prioritaire).

### Hors scope explicite (pour information — Phases 3-6)

- [ ] Tool 3 b1 refactor (sous-pages Banks/ARPEG/LOOP + LoopPadStore writer + descriptor index 12 dans TOOL_NVS_LAST[2]) → Phase 3.
- [ ] Tool 5 cycle 7-way (NORMAL / ARPEG-Imm / ARPEG-Beat / ARPEG_GEN-Imm / ARPEG_GEN-Beat / LOOP-Free / LOOP-Beat / LOOP-Bar) + `loopQuantize` per-bank persistance → Phase 3 (selon P5 tranchage).
- [ ] Tool 7 3 contextes (NORMAL/ARPEG/LOOP) + PotRouter 3e contexte → Phase 3-4.
- [ ] Tool 4 refus ControlPad sur LOOP control pads (règle collision §5 rule 2) → Phase 3.
- [ ] LedController `renderBankLoop` runtime state machine + WAITING crossfade colorB blanc + tick FLASH wrap/bar/rec/od + EVT_LOOP_* mapping → Phase 4.
- [ ] PotMappingStore extension 3 contextes (+8 slots LOOP) → Phase 3-4.
- [ ] Effets shuffle/chaos/velocity patterns formulas (calcShuffleOffsetUs, calcChaosOffsetUs, applyVelocityPattern non-trivial) → Phase 5.
- [ ] Slot Drive LittleFS + 16 slots persistence + serialize/deserialize + handleLoopSlots gesture handler → Phase 6.
- [ ] `toggleAllArpsAndLoops()` extension §30 spec gesture (LEFT+hold pad multi-bank inclus LOOP) → Phase 4 (LED feedback multi-bank requis).
- [ ] Step 11 midiPanic LOOP flush (`flushLiveNotes` au midiPanic) → Phase 4+ (trivial, ~10 lignes ; pas bloquant Phase 2).
- [ ] §31 spec gesture décisions #2/#3/#4 (slot save annulable, CLEAR tenu sans slot, preBankSwitch hook signature) → Phase 6/7 selon scope.
- [ ] Bar-snap deadzone 25% (vs 50% effective Phase 2 round simple) → Phase 4+ après mesure HW.

---

## §5 — Notes de risques et zones de friction

| Zone | Risque | Mitigation Phase 2 |
|---|---|---|
| `_events[]` 8 KB × 4 banks = 32 KB SRAM + autres LoopEngine members (~2.3 KB chacun) ≈ ~41 KB total | RAM dépasse budget si MAX_LOOP_BANKS mal dimensionné | Compile gate Task 2 figé par `static_assert(sizeof(LoopEngine) <= 11000)` — si compile échoue, audit footprint avant de continuer. RAM target ~30%, marge confortable sur 320 KB. Si Phase 2 + autres futurs ajouts dépassent ~40% : réduire MAX_LOOP_BANKS à 2 (spec original) et documenter trade-off roadmap. |
| `_lastDispatchedGlobalTick` sentinel boundary crossing | Bug subtil : si globalTick reset à 0 (reboot), le sentinel 0xFFFFFFFF compare faux et la première pending action ne fire jamais | begin() reset sentinel à 0xFFFFFFFF. tick() check `_lastDispatchedGlobalTick == 0xFFFFFFFF` en premier. Test HW : tap REC tout de suite après boot — première action doit fire. |
| `recordNoteOn` en OVERDUBBING utilise `_lastPositionUs` écrit par tick() | Premier overdub note dans le frame suivant le startOverdub : `_lastPositionUs` = valeur du frame précédent (≤ 1ms latence). Si frame très long (improbable < 5ms), latence augmente | Documenter dans le code (déjà fait dans le commentaire B2). HW-C test 6-7 valide cohérence overdub-vs-loop timeline. |
| Refcount `_noteRefCount[note]` underflow si flushActiveNotes(hard=true) zeros puis pad release | Guard `_noteRefCount[note] > 0` dans noteRefDecrement | Test HW : flushLiveNotes (bank switch outgoing) puis return + pad release sur même note → pas de noteOff parasite. |
| Static `s_loopEngines[4]` allocated 37.6 KB même si 0 bank LOOP en NVS | "Wasted" SRAM avant Phase 3 NVS support | Spec choice : tous engines always-alive (invariant 5 §0.4). Acceptable vu marge SRAM 80%+. Phase 3+ peut reconsidérer si pression. |
| Pas de toggle global multi-bank LOOP en Phase 2 (`toggleAllArps` ARPEG-only) | LEFT+hold pad ne toggle pas les LOOP en background | Différé Phase 4 explicite (§0.3 + checklist §0.5 box "DIFFÉRÉ"). User documenté via STATUS. |
| Pas de midiPanic LOOP en Phase 2 | Stuck notes LOOP si midiPanic triggered | Différé Phase 4+ (§0.3 acté). Trivial à ajouter ; risque limité Phase 2 vu absence de scenario natural panic en jeu (panic = test ou crash recovery). |
| `setLoopQuantizeMode(LOOP_QUANT_FREE)` hardcodé Phase 2 (P5 différé) | Pas de test HW Quantize BEAT/BAR Phase 2 | Acceptable scope : Phase 2 vise "premier son" pas "quantize tested". Phase 3 testera BEAT/BAR après P5 storage tranché. Le code dispatcher pending action est implémenté et testé via Phase 2 review code, juste pas en HW. |
| LoopEngine API exposée complètement Phase 2 (stubs Phase 5) | YAGNI ? | Acté P4 reco : stabiliser interface évite bumps header Phases 3-6. Coût marginal (~30 LOC stubs triviaux). |
| Bar-snap deadzone 50% effective (vs 25% spec §7) | Tap REC 30ms après bar line produit 4 bars au lieu de 3 | Documenté hors scope Phase 2 (§4 hors scope + §5 ci-dessus). Raffinement musical Phase 4+ après mesure HW. |
| HW Checkpoints B/C/D dépendent d'upload manuel user | Phase 2 ne peut pas progresser si user indisponible | Pas un risque technique. Process habituel CLAUDE.md user "no auto-upload" — l'agentic worker doit attendre OK explicite. |

---

## §6 — Findings audit traités (post-rédaction, agent background 2026-05-16)

Plan audité par agent dédié juste après rédaction (axes : drift plan↔code main, snippets non-compilables, drift plan↔spec, anti-patterns §28, checklist §32, AUDIT FIXES archive, décisions roadmap, cohérence interne, scope, cohérence Tool 5). Résumé des corrections appliquées :

| # | Sévérité | Finding | Résolution |
|---|---|---|---|
| **B1** | 🛑 | `handlePlayStopPad` n'existe pas dans `main.cpp` (nom inventé) | Task 9 Zone E1 + Step 9.10 + §1 corrigés : `handleLoopControls` placé **entre `s_controlPadManager.update` (ligne 1510-1511) et `handlePadInput` (ligne 1513)**. |
| **B2** | 🛑 | WAITING_* state machine spec §17 jamais implémentée | **Hybride acté** : ajout `WAITING_PENDING = 5` au State enum. `getState()` retourne WAITING_PENDING si `_pendingAction != PENDING_NONE`. `handleLoopControls` étendu pour gérer les 3 cas concurrents §17 (Phase 2 unreachable en FREE mode mais code correct par review). Task 10 recording lock note Phase 3 commit-then-switch. **Bénéfice : interface stable Phases 3-6, P4 reco préservé**. |
| **B3** | 🛑 | Init `_loadedLoopQuantize` placé dans ctor mais le pattern projet l'init dans `loadAll()` ligne 612-613 | Task 1 cible diff + Step 1.3 + Step 1.6 corrigés : memset déplacé vers `loadAll()` à côté de `_loadedQuantize` / `_loadedScaleGroup` (lignes 612-613). |
| **M1** | ⚠️ | CLEAR=cancelOverdub pendant OVERDUBBING absent de spec §9 | **Acté** : décision design documentée en §0.3 ("divergence vs spec §9 strict, acceptée pour la musicalité"). TODO amender spec §9 dans future revision. |
| **M2** | ⚠️ | `BankSlot.loopEngine = nullptr` field initializer incohérent avec `arpEngine` sans initializer | Task 2 cible diff KeyboardData.h corrigée : retrait `= nullptr` du field. Init reste explicite Task 7 Zone 4 (cohérent pattern existant). |
| **M3** | ⚠️ | Footprint SRAM sous-estimé (plan disait 37.6 KB, recalcul ~41 KB) | §0.3 + Task 2 LoopEngine.h + Task 7 Step 7.9 + commit message corrigés : ~41 KB total, RAM target ~30%. `static_assert(sizeof(LoopEngine) <= 11000)` ajouté dans LoopEngine.h pour figer le contrat. |
| **M4** | ⚠️ | `static_assert(sizeof(LoopEvent) == 8)` sans `packed` non garanti | Task 2 LoopEngine.h commentaire ajouté : "GCC ESP32 alignement naturel produit 8 B ; packed pas utilisé car penalty unaligned uint32_t". static_assert fige le contrat pour future refactor. |
| **N1** | 🔶 | Commentaire "re-entry safety" trompeur (pas de re-entry possible) | Task 5 releaseLivePad commentaire reformulé : "idempotence sweep simultané" (justification correcte). |
| **N2** | 🔶 | Box 3 §0.5 "stop() idempotente" — vrai sur buffer, faux sur phase | §0.5 box 3 reformulé : "idempotente sur le **contenu** du buffer (pas sur la phase de lecture — doPlay reset cursor)". |
| **N3** | 🔶 | tick formula `barDurationUs / 4` hardcode 4/4 | Task 5 commentaire tick ajouté : "4/4 assumption, valid pour TICKS_PER_BAR=96. Si time signature change, refactor en `TICKS_PER_BAR / TICKS_PER_BEAT`". |
| **N4** | 🔶 | Commentaire setLiveNote "AFTER noteRefIncrement has sent MIDI" trompeur (call inconditionnel) | Task 5 setLiveNote commentaire reformulé : "INCONDITIONALLY, regardless of noteRefIncrement return". |
| **N5** | 🔶 | `abortOverdub` → STOPPED dans plan vs spec §8 → PLAYING | **Aligné spec §8** : Task 4 `abortOverdub` modifié pour transitionner vers PLAYING (pas STOPPED), pas de flushActiveNotes. Task 9 commentaire + Step 9.14 sub-test 13 mis à jour. |

**NITs (NIT1-NIT4) non bloquants** : terminologie, ordre defensif if/else, edge state update — pas de correction nécessaire (déjà cohérents avec l'intention).

**Couverture audit** : 10/10 axes auditeur, 26 zones vérifiées, 9 AUDIT FIXES archive validés portés correctement.

---

**Plan Phase 2 PRÊT pour exécution post-audit.** Total Tasks : 11 (0-10). Total commits estimés : 10-11 (option fusion Tasks 5+6). HW Checkpoints : 3 bloquants (B après Task 7, C après Task 9, D après Task 10). Pré-requis vérifiés : spec LOOP §28 Q1-Q8 actées, spec gesture §32 checklist 6 points (1-4 + 6 cochées, 5 différé Phase 4 documenté), code main `BANK_LOOP=2` enum + `_transport` BankManager + 4-way switch handlePadInput déjà en place (Phase 1 LOOP P1 close + ARPEG_GEN cohabitation). 12 findings audit traités (3 bloquants + 4 majeurs + 5 mineurs).

**Prochaine étape session 2 conception** : rédiger `docs/superpowers/plans/YYYY-MM-DD-loop-phase-3-plan.md` après tranchage P5 (BankTypeStore v5 vs LoopBankConfigStore). Phase 3 implémentera aussi `commitPendingAction()` (audit B2 follow-up) pour permettre le commit-then-switch lors d'un bank switch pendant WAITING_PENDING.


