# LOOP Plans Deep Audit — 2026-04-06

**Scope** : audit méticuleux read-only des 6 plans LOOP (Phase 1-6) et du spec slot drive contre le code source actuel de l'ILLPAD V2, incluant design decisions et roadmap de patches.

**Mode** : aucun code source modifié, aucun commit, aucun push. Ce document capture les décisions et la roadmap pour l'implémentation à venir.

**Sources** :
- `docs/superpowers/specs/2026-04-06-loop-slot-drive-design.md` (spec slot drive)
- `docs/plans/loop-phase1-skeleton-guards.md` → `loop-phase6-slot-drive.md` (6 plans)
- `src/` complet (vérification des signatures, patterns, code existant)

---

## 1. Synthèse initiale du rapport d'audit

Avant reclassification, le rapport d'audit listait :

| Catégorie | Items | Description |
|---|---|---|
| A (bloquants) | A1, A2, A3, A4 | 4 items marqués compile-blocking |
| B (runtime) | B1, B2, B4, B5, B6, B7 | 6 items runtime bugs (B3 fusionné avec Q1) |
| C (inter-plan) | C1, C2, C3 | 3 inconsistances cross-phase |
| D, E, F | plusieurs | plan↔code gaps, plan↔spec, optimisations |

Après le review en détail finding-par-finding, **seul A1 reste un vrai bloquant compile**. A2, A3, A4 ont été reclassés en documentation/clarity gaps.

---

## 2. Décisions design — Q1 à Q5

| # | Question | Décision | Impact |
|---|---|---|---|
| Q1 | Comportement midiPanic pendant LOOP RECORDING/OVERDUBBING | `flushLiveNotes(transport, channel)` seulement — pas de changement d'état, cohérent avec panic arp qui ne stoppe pas non plus | Phase 2 Step 11a |
| Q2 | Sémantique save/load baseVelocity, velocityVariation | Preset complet — les valeurs chargées écrasent `slot.baseVelocity` / `slot.velocityVariation`. Nécessite writeback explicite au load | Phase 6 Step 6b |
| Q3 | LittleFS mount path | `/littlefs`, paths = `/littlefs/loops/slotNN.lpb`. Évite collision future avec SPIFFS | Phase 6 Step 2b |
| Q4 | Warning utilisateur reset params au flash Phase 6 | Non — Zero Migration Policy suffit, déjà documentée dans CLAUDE.md | — |
| Q5 | Sync hold/octave pads dans `_lastScaleKeys` early return LOOP | Oui, 5 lignes supplémentaires en Phase 1 Step 6a | Phase 1 Step 6a |

---

## 3. Findings après review — status final

### A — Compile-blocking (vrais bloquants)

#### A1 — `s_bankTypeStore` non déclaré dans Phase 2 Step 4a
- **Fichier** : `docs/plans/loop-phase2-engine-wiring.md` §Step 4a
- **Problème** : référence à un static `s_bankTypeStore.loopQuantize[i]` qui n'est déclaré nulle part. Compile error immédiat.
- **Décision** : **Option B** — suivre le pattern existant de `_loadedQuantize[NUM_BANKS]` + `getLoadedQuantizeMode(i)` dans NvsManager. Ajouter `_loadedLoopQuantize[NUM_BANKS]` + `getLoadedLoopQuantizeMode(i)` (et optionnellement `setLoadedLoopQuantizeMode()` pour la cohérence).
- **Impact plan** :
  - Phase 1 : ajouter la déclaration membre + getter dans `NvsManager.h` (extension du scope Phase 1 vers NvsManager.h)
  - Phase 1 : ajouter le `memset(_loadedLoopQuantize, DEFAULT_LOOP_QUANT_MODE, NUM_BANKS)` dans le constructeur
  - Phase 1 : peupler `_loadedLoopQuantize[i] = bts.loopQuantize[i]` dans `loadAll()` (juste après `_loadedQuantize[i]`)
  - Phase 2 Step 4a : remplacer `s_bankTypeStore.loopQuantize[i]` par `s_nvsManager.getLoadedLoopQuantizeMode(i)`

### D — Plan↔code gaps (ex-bloquants reclassés)

#### A2 — ToolBankConfig Phase 3 Step 1 — gaps mineurs

Après review attentif, **A2 n'est PAS compile-blocking**. Le plan gère correctement la signature change de `saveConfig` et `drawDescription`. Les vrais gaps sont :

##### A2.1 — Defaults confirmation path ne reset pas `wkLoopQuantize`
Dans la Phase 3 Step 1b-bis, la boucle de reset (`for i in NUM_BANKS`) actuelle après validation `y` ne reset que `wkTypes[i]` et `wkQuantize[i]`. Le fix : ajouter `wkLoopQuantize[i] = DEFAULT_LOOP_QUANT_MODE;` dans la même boucle.

##### A2.2 — Revert-on-cancel path ne montre pas le patch explicitement
Le plan dit "must restore `wkLoopQuantize[cursor]` from `savedLoopQuantize[cursor]`" sans montrer le code exact. Fix : expliciter la ligne :
```cpp
wkLoopQuantize[cursor] = savedLoopQuantize[cursor];
```

##### A2.3 — `Files Modified` table omet `saveConfig` signature change
Le tableau dit seulement "drawDescription signature change" pour ToolBankConfig.h. Fix : compléter avec "saveConfig 3-arg signature change".

### C — Inter-plan clarity (reclassé)

#### A3 — Phase 3/6 `POOL_OFFSETS_LOOP` collision
Après review, le plan n'est pas compile-blocking si l'implémenteur comprend "Replace" comme "modifier en place". Mais le wording est ambigu. Fix : patcher Phase 6 Step 7a et 7h pour utiliser des directives explicites **REPLACE** sur :
- `POOL_OFFSETS_LOOP[]` (file scope)
- `TOTAL_POOL_LOOP` (file scope)
- `MAP_LOOP[]` (static local dans `linearToPool`)
- `REV_LOOP[]` (static local dans `poolToLinear`)
- Le guard `if (line < 10)` → `if (line < 26)`
- `enum PadRoleCode` (étendu avec ROLE_LOOP_SLOT_0..15)

Ajouter aussi une note explicite "Do NOT duplicate these — modify the existing definitions in place."

### D — Plan↔code mismatches

#### A4 — `midiPanic` misquote + intégration Q1
- **Fichier** : `docs/plans/loop-phase2-engine-wiring.md` §Step 11a
- **Problème** : le plan montre `arpEngine->clearAllNotes(s_transport)` comme code existant, alors que le vrai code utilise `flushPendingNoteOffs(s_transport)`. De plus, l'ajout `loopEngine->stop()` est insuffisant (voir Q1).
- **Décision** :
  - Corriger le misquote pour montrer `flushPendingNoteOffs` (le vrai code)
  - Remplacer `loopEngine->stop(s_transport)` par `loopEngine->flushLiveNotes(s_transport, s_banks[i].channel)` (décision Q1)

### B — Runtime bugs

#### B1 — LOOP sweep obsolète dans `handleLeftReleaseCleanup`
- **Fichier** : `docs/plans/loop-phase2-engine-wiring.md` §Step 10b-1
- **Problème** : le sweep utilise `s_lastKeys[i] && !state.keyIsPressed[i]` (edge) qui rate les pads relâchés pendant un hold-left (`s_lastKeys` est synchronisé chaque frame, y compris pendant le hold). Refcount stuck, loop silencieux sur ces notes.
- **Décision** : **Option 1** — ajouter un tracking per-pad `_liveNote[NUM_KEYS]` dans LoopEngine (analogue à `_lastResolvedNote[]` de MidiEngine).
- **Pattern validé par la note projet** : `padOrder` est runtime-immutable (voir `memory/project_setup_boot_only.md`), donc stocker la note au press est sûr.
- **Impact plan Phase 2** :
  - LoopEngine.h : ajouter `uint8_t _liveNote[NUM_KEYS]` en private
  - LoopEngine.h : ajouter 2 méthodes publiques :
    - `void setLiveNote(uint8_t padIndex, uint8_t note)` — appelée par processLoopMode au rising edge
    - `void releaseLivePad(uint8_t padIndex, MidiTransport& transport)` — idempotent, appelée par processLoopMode au falling edge ET par handleLeftReleaseCleanup sweep
  - LoopEngine.cpp : `begin()` et `clear()` font `memset(_liveNote, 0xFF, sizeof(_liveNote))`
  - LoopEngine.cpp : `flushLiveNotes()` fait aussi le memset à 0xFF
  - main.cpp : `processLoopMode` appelle `setLiveNote` au press et `releaseLivePad` au release (remplace le pattern actuel de décrément direct)
  - main.cpp : `handleLeftReleaseCleanup` LOOP branch devient idempotent : pour chaque pad non pressé, appel `releaseLivePad(i, transport)` sans check edge

#### B2 — `recordNoteOn/Off` OVERDUBBING mauvais timebase
- **Fichier** : `docs/plans/loop-phase2-engine-wiring.md` §recordNoteOn, §recordNoteOff
- **Problème** : le code calcule `elapsedUs = (now - _playStartUs) % liveDurationUs` où `liveDurationUs` est en timebase RECORD (pas live) — mélange de timebases. Erreur audible avec changement de tempo (exemple : ~143 ms d'erreur à 120→140 BPM).
- **Décision** : **Option A** — remplacer le calcul fautif par la lecture directe de `_lastPositionUs` (déjà en timebase RECORD, mis à jour par `tick()` chaque frame). Latence 1 frame (~1 ms), musicalement imperceptible.
- **Impact plan Phase 2** : patcher Phase 2 Step 1b (sections recordNoteOn et recordNoteOff) :
  ```cpp
  } else if (_state == OVERDUBBING) {
      if (_overdubCount >= MAX_OVERDUB_EVENTS) return;
      uint32_t offsetUs = _lastPositionUs;   // from last tick(), RECORD timebase
      _overdubBuf[_overdubCount++] = { offsetUs, padIndex, velocity, {0, 0} };
      _overdubActivePads[padIndex] = true;
  }
  ```

#### B4 — `deserializeFromBuffer` quantize-snap utilise `_recordBpm` au lieu de BPM live
- **Fichier** : `docs/plans/loop-phase6-slot-drive.md` §Step 3c
- **Problème** : la formule pour `_playStartUs` utilise `_recordBpm` au lieu du BPM live, ce qui ne correspond pas à ce que le spec (§2.3) dit et produit un décalage de l'alignement beat (~8% d'un beat à 120→140 BPM). Le "hard-cut + beat align" ne tombe pas sur le beat.
- **Décision** : passer `float currentBPM` via l'API chain.
- **Impact plan Phase 6** :
  - Step 2a : signature `LoopSlotStore::loadSlot(..., float currentBPM) const`
  - Step 2b : body appelle `deserializeFromBuffer(..., currentBPM)`
  - Step 3a : signature `LoopEngine::deserializeFromBuffer(..., float currentBPM)`
  - Step 3c : formule utilise `currentBPM` dans le calcul de `usPerBeat`, avec safety floor à 10.0f
  - Step 6b (handleLoopSlots) : récupère `float currentBPM = s_clockManager.getSmoothedBPMFloat()` avant l'appel loadSlot

#### B5 — `baseVelocity` / `velocityVariation` writeback au load
- **Fichier** : `docs/plans/loop-phase6-slot-drive.md` §Step 3c, §Step 6b
- **Problème** : deserialize met à jour `LoopEngine._baseVelocity/_velocityVariation` mais pas `BankSlot.baseVelocity/velocityVariation`. `reloadPerBankParams` lit depuis BankSlot, donc les valeurs loadées sont ignorées au catch re-arm.
- **Décision** : **Option 2** — writeback dans `handleLoopSlots` après loadSlot success, avant reloadPerBankParams. Requires 2 getters inline dans LoopEngine.
- **Impact plan Phase 6** :
  - Step 3a : ajouter dans LoopEngine.h public :
    ```cpp
    uint8_t getBaseVelocity() const { return _baseVelocity; }
    uint8_t getVelocityVariation() const { return _velocityVariation; }
    ```
  - Step 6b : dans handleLoopSlots, après `loadSlot(...) == true` :
    ```cpp
    BankSlot& slot = s_bankManager.getCurrentSlot();
    slot.baseVelocity = eng->getBaseVelocity();
    slot.velocityVariation = eng->getVelocityVariation();
    reloadPerBankParams(slot);
    s_leds.triggerConfirm(CONFIRM_LOOP_SLOT_LOAD);
    ```

#### B6 — `_lastScaleKeys` sync hold + octave pads en Phase 1
- **Fichier** : `docs/plans/loop-phase1-skeleton-guards.md` §Step 6a
- **Problème** : le early return LOOP sync `_lastScaleKeys` pour les root/mode/chrom pads mais pas pour holdPad et octavePads[0..3]. Risque de phantom edge au switch LOOP → ARPEG avec hold ou octave pad tenu.
- **Décision** (Q5) : ajouter 5 lignes de sync.
- **Impact plan Phase 1 Step 6a** :
  ```cpp
  if (slot.type == BANK_LOOP) {
      // Existing: sync root, mode, chromatic
      for (uint8_t r = 0; r < 7; r++) { ... }
      if (_chromaticPad < NUM_KEYS) _lastScaleKeys[_chromaticPad] = keyIsPressed[_chromaticPad];
      // ADD: sync hold + octave pads
      if (_holdPad < NUM_KEYS) _lastScaleKeys[_holdPad] = keyIsPressed[_holdPad];
      for (uint8_t o = 0; o < 4; o++) {
        if (_octavePads[o] < NUM_KEYS) _lastScaleKeys[_octavePads[o]] = keyIsPressed[_octavePads[o]];
      }
      return;
  }
  ```

#### B7 — `doStopRecording` défense division par zéro
- **Fichier** : `docs/plans/loop-phase2-engine-wiring.md` §doStopRecording
- **Problème** : si `closeUs == _recordStartUs` (tap extrêmement rapide ou micros() qui ne progresse pas entre start et stop), `recordedDurationUs = 0`, division par zéro plus loin dans le calcul de scale.
- **Décision** : clamp défensif à 1000 us (1 ms minimum).
- **Impact plan Phase 2 doStopRecording** :
  ```cpp
  uint32_t recordedDurationUs = closeUs - _recordStartUs;
  if (recordedDurationUs < 1000) recordedDurationUs = 1000;   // 1 ms floor — prevent divZ
  ```

### C — Inter-plan consistency

#### C1 — `CONFIRM_LOOP_REC` lifecycle entre Phase 1, 2 et 4
- **Problème** : Phase 1 ajoute l'enum value mais pas le case d'expiry dans `renderConfirmation()`. Phase 2 trigger le confirm. Phase 4 ajoute enfin l'expiry et le rendering. Entre Phase 2 et Phase 4, tout appel à `triggerConfirm(CONFIRM_LOOP_REC)` est silencieusement clearé au frame suivant (default branch).
- **Décision** : **Option A** — ajouter le case d'expiry minimal dès Phase 1 Step 4c (sans rendu). Permet aux tests intermédiaires Phase 2/3 de ne pas être surpris.
- **Impact plan Phase 1 Step 4c** : ajouter dans LedController.cpp renderConfirmation :
  ```cpp
  case CONFIRM_LOOP_REC:
    if (elapsed >= 200) { _confirmType = CONFIRM_NONE; return false; }
    return true;
  ```
  Le rendering reste Phase 4.

#### C2 — `LOOPPAD_VERSION` consolidation Step 2 vs 7c
- **Problème** : Phase 1 définit `LOOPPAD_VERSION = 1` + struct 8 bytes en Step 2a/2b, puis les remplace par `LOOPPAD_VERSION = 2` + struct 32 bytes en Step 7c-2. Duplication dans la même phase, piège pour l'implémenteur.
- **Décision** : **Option A** — fusionner Step 2a, 2b et 7c-2 en un seul bloc final. Un seul `LoopPadStore` (32 bytes, version 2) défini dès le départ.
- **Impact plan Phase 1** :
  - Step 2a : conserver les #define namespace/key, mais `LOOPPAD_VERSION` devient 2 dès le départ
  - Step 2b : supprimer (struct 8 bytes obsolete)
  - Step 7c-2 : devient la définition principale de `LoopPadStore` à 32 bytes (16 slotPads + 8 padding)
  - Réorganiser le flow du Step 2 pour être linéaire et cohérent

#### C3 — `POOL_LINE_COUNT` supersession confuse
- **Problème** : Phase 3 Step 2e dit "mettre POOL_LINE_COUNT à 10" mais la Transition note en début de Step 2 dit "supprimer POOL_LINE_COUNT". Contradiction.
- **Décision** : **Option B** — marquer Step 2e comme SUPERSEDED explicitement. Garde la place numérique mais rend invalide.
- **Impact plan Phase 3 Step 2e** :
  ```
  ### ~~Step 2e~~ — SUPERSEDED BY Step 2-pre-8

  POOL_LINE_COUNT is deleted entirely. Line counts are per-sub-page now,
  computed via poolOffsetsForContext(). See Step 2-pre-8 for the replacement.
  ```

---

## 4. Roadmap des patches plan

### Phase 1 (skeleton + guards) — 4 patches

1. **A1** — NvsManager.h : ajouter `_loadedLoopQuantize[NUM_BANKS]` + getter/setter
2. **C2** — Fusionner Step 2a/2b/7c-2 : `LoopPadStore` à 32 bytes, version 2, dès le départ
3. **C1** — Step 4c : ajouter case expiry `CONFIRM_LOOP_REC` (sans rendu)
4. **B6** — Step 6a : sync hold + octave pads dans early return LOOP

### Phase 2 (LoopEngine + wiring) — 7 patches

5. **A1** — Step 4a : utiliser `s_nvsManager.getLoadedLoopQuantizeMode(i)` au lieu de `s_bankTypeStore.loopQuantize[i]`
6. **B1** — LoopEngine.h/.cpp : ajouter `_liveNote[NUM_KEYS]` + `setLiveNote()` + `releaseLivePad()`, memset dans begin/clear/flushLiveNotes
7. **B1** — processLoopMode : appeler setLiveNote au press, releaseLivePad au release
8. **B1** — handleLeftReleaseCleanup LOOP branch : devient idempotent via `releaseLivePad`
9. **B2** — recordNoteOn/Off : OVERDUBBING branch lit `_lastPositionUs`
10. **B7** — doStopRecording : clamp recordedDurationUs ≥ 1000
11. **A4 + Q1** — Step 11a midiPanic : corriger misquote (`flushPendingNoteOffs`) + utiliser `flushLiveNotes(channel)`

### Phase 3 (setup tools) — 4 patches

12. **A2.1** — Step 1b-bis defaults path : ajouter `wkLoopQuantize[i] = DEFAULT_LOOP_QUANT_MODE` dans la boucle de reset
13. **A2.2** — Step 1b-bis revert path : expliciter la ligne `wkLoopQuantize[cursor] = savedLoopQuantize[cursor]`
14. **A2.3** — Files Modified table : ajouter "saveConfig signature change" pour ToolBankConfig.h
15. **C3** — Marquer Step 2e SUPERSEDED explicitement

### Phase 4 (PotRouter + LED) — 0 patches

Phase 4 est propre après les décisions prises.

### Phase 5 (effects) — 0 patches

Phase 5 est propre.

### Phase 6 (slot drive) — 8 patches

16. **A3** — Step 7a/7h : REPLACE explicites pour POOL_OFFSETS_LOOP / TOTAL_POOL_LOOP / MAP_LOOP / REV_LOOP / enum PadRoleCode
17. **Q3** — Step 2b : LittleFS mount path `/littlefs`, slotPath prefix adapté (`/littlefs/loops/slotNN.lpb`)
18. **B4** — Step 2a : signature `LoopSlotStore::loadSlot(..., float currentBPM) const`
19. **B4** — Step 2b : body passe currentBPM à deserializeFromBuffer
20. **B4** — Step 3a : signature `deserializeFromBuffer(..., float currentBPM)`
21. **B4** — Step 3c : formule utilise currentBPM avec safety floor
22. **B4** — Step 6b (handleLoopSlots) : récupère currentBPM via `s_clockManager.getSmoothedBPMFloat()`
23. **B5** — Step 3a : ajouter getters `getBaseVelocity()` / `getVelocityVariation()` dans LoopEngine.h
24. **B5 + Q2** — Step 6b : writeback `slot.baseVelocity / velocityVariation` avant reloadPerBankParams

### Total

- **Phase 1** : 4 patches
- **Phase 2** : 7 patches
- **Phase 3** : 4 patches
- **Phase 4** : 0 patches
- **Phase 5** : 0 patches
- **Phase 6** : 9 patches (24 items listés, mais certains se regroupent en un seul edit)

Environ **24 edits** sur les plans. Zéro code source modifié à ce stade.

---

## 5. État attendu après application des patches

- **1 seul bloquant** résolu (A1) via extension NvsManager
- **6 runtime bugs** corrigés (B1, B2, B4, B5, B6, B7)
- **3 inter-plan inconsistances** résolues (C1, C2, C3)
- **3 gaps documentation** comblés (A2.1, A2.2, A2.3)
- **1 misquote** corrigé (A4)
- **1 clarity issue** résolu (A3)
- **5 décisions design** appliquées (Q1-Q5)

Les plans seraient prêts pour une implémentation séquentielle Phase 1 → 6, avec confiance élevée dans la cohérence et la compilabilité.

---

## 6. Notes méthodologiques

### Reclassifications après review

Sur les 4 findings initialement marqués bloquants (A1-A4), **seul A1 est un vrai compile-blocker**. A2, A3, A4 se sont révélés être :
- **A2** : documentation gaps (3 petits patches)
- **A3** : clarity issue sur le wording "Replace"
- **A4** : misquote factuel du code existant

Cette reclassification réduit la charge de travail mais rend aussi plus important de les corriger tous — un plan "presque correct" est plus dangereux qu'un plan "clairement bloqué" car il compile silencieusement avec des comportements subtilement faux.

### Findings runtime validés

Les 6 findings B restent tous des runtime bugs réels, mais certains (B6, B7) sont défensifs (cas limites peu probables) et d'autres (B1, B2, B4, B5) sont des bugs qui se manifesteraient dès les premiers tests hardware.

### Déjà capturé en mémoire projet

La décision B1 s'appuie sur un fait projet capturé en mémoire : `padOrder` est runtime-immutable (setup tools boot-only). Voir `memory/project_setup_boot_only.md`. Cette note évite de devoir défendre contre un changement de padOrder mid-session.

---

## 7. Questions ouvertes (aucune restante)

Toutes les questions Q1-Q5 ont été tranchées pendant la session. Aucune question restante pour l'implémentation.

---

## 8. Status

**Audit complet.** Prêt pour la phase d'application des patches sur les plans (séparée, sur demande explicite de l'utilisateur).

Aucun code source modifié à ce stade. Aucun commit créé. Le rapport est READ-ONLY par construction et cette synthèse est un artefact de session à consolider si souhaité.
