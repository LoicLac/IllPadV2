# Master Sync — plan d'implémentation

**Date** : 2026-05-19
**Statut** : DRAFT — à exécuter immédiatement après validation Loïc.
**Branche cible** : `main` (autocommit on, pas de feature branch).
**Estimation** : ~2j coding + 1.5j HW + 0.5j doc-sync ≈ **4 jours**.

**Cross-refs** :
- Spec source : [`docs/superpowers/specs/Illpad_Master_Sync.md`](../specs/Illpad_Master_Sync.md)
- Design doc déclencheur : [`docs/superpowers/designs/2026-05-19-loop-algo-pivot-design.md`](../designs/2026-05-19-loop-algo-pivot-design.md)
- Parent spec à patcher post-impl : [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](../specs/2026-04-19-loop-mode-design.md) §7, §17, §24, §28
- Code touché : [`src/midi/ClockManager.{h,cpp}`](../../../src/midi/ClockManager.h), [`src/loop/LoopEngine.{h,cpp}`](../../../src/loop/LoopEngine.h)

---

## §0 Pré-flight

### Build initial vérifié
```
RAM:   29.1 % (95436 / 327680 B)
Flash: 22.1 % (737401 / 3342336 B)
SUCCESS in 3.59 s
```
Phase 2 LOOP CLOSE. Phase 3 prep en cours mais orthogonale (Tools setup). Aucun fichier en conflit.

### Invariants à préserver (cf parent spec §23 et CLAUDE.md projet)

1. **Aucune note bloquée** — refcount cohérent, held pads flushed.
2. **Bank switch refusé pendant RECORDING/OVERDUBBING** (= PENDING_CLOSE included, `isLocked()` true).
3. **Au plus 1 bank LOOP en REC/OD à instant t** (invariant 11).
4. **Pas de mélange d'horloges per event** (`_recordBpm` immutable).
5. **No new/delete runtime** (allocations statiques).
6. **No blocking on Core 0** (NVS débounce on FreeRTOS task).
7. **Catch system intact** (pas touché par Master Sync).
8. **Setup/Runtime 4-link chain** (NVS layout inchangé, pas de Tool touché).

### Invariants Phase 2 audit-fix à préserver

- B-N1/R-N1 : `onBackgroundTransition` flush live press + reset CLEAR tracker.
- B-N2 : `mergeOverdub` flush held pads → noteOff inject.
- B2 : `flushPendingNoteOffs` self-stop (state → STOPPED).
- B3 : `commitWaitingAction` propage nowUs (anti underflow uint32).
- M3 : velocity strict capture, variation au playback seulement.
- M6 : timestamp clamp `< _loopDurationUs`.
- M8 : MIDI live monitor tous états.
- m4 : flash flags one-shot (`consumeBarFlash`/`consumeWrapFlash`).

### Hors scope (à NE PAS toucher)

- Tools setup (Tool 3, 4, 5, 6, 7, 8) — Phase 3 territory.
- LedController + LedGrammar.
- BankManager (sauf si silent deny path nécessite vérif).
- ArpEngine / ArpScheduler.
- NvsManager + tous Store struct.
- ViewerSerial.

---

## §1 Décomposition en 3 commits

| Commit | Scope | LOC | Build gate | HW gate |
|---|---|---|---|---|
| **C1** | ClockManager getters (API publique, aucun runtime change) | +30 | Compile clean, 0 warning | Aucun (pas d'effet observable) |
| **C2** | LoopEngine Master Sync activation atomique | +90 / -90 | Compile clean, 0 warning | G1-G9 (cf §4) |
| **C3** | Doc-sync (parent spec + STATUS + LOOP_PROGRESS) | doc-only | n/a | Aucun |

**Principe de cohérence par commit** : chaque commit laisse le firmware dans un état consistant et démontrable. C1 ajoute des getters non utilisés (no-op runtime). C2 active la nouvelle logique atomique (toute la chaîne anchor + PENDING_CLOSE + commitRecordingClose). C3 finalise la doc.

**Pas de split intermédiaire C2** : la transition anchor sans suppression du rescale = état musical incohérent (events à phase Δ + rescale qui les déforme à nouveau). Il faut tout activer en même temps.

---

## §2 Commit 1 — ClockManager getters

### Objectif

Exposer 4 primitives publiques permettant à LoopEngine d'ancrer ses µs internes sur la grille master tick.

### Fichiers touchés

- `src/midi/ClockManager.h` (ajout déclarations)
- `src/midi/ClockManager.cpp` (ajout implémentations)

### Code à ajouter

**`ClockManager.h`** — section public, après les getters existants ([h:25-30](../../../src/midi/ClockManager.h)) :

```cpp
// --- Master grid wall times (pour LoopEngine sync) ---
uint32_t getLastTickWallTimeUs() const;     // wall time du dernier tick fired
uint32_t getLastBeatWallTimeUs() const;     // wall time du dernier beat boundary (mod 24)
uint32_t getLastBarWallTimeUs() const;      // wall time du dernier bar boundary (mod 96)
float    getTickIntervalUs() const;         // intervalle µs courant entre 2 ticks (PLL)
```

**`ClockManager.cpp`** — après les getters existants ligne 264-277 :

```cpp
uint32_t ClockManager::getLastTickWallTimeUs() const {
  return _lastTickTimeUs;
}

uint32_t ClockManager::getLastBeatWallTimeUs() const {
  uint32_t phase = _currentTick % 24;
  return _lastTickTimeUs - (uint32_t)(phase * _pllTickInterval);
}

uint32_t ClockManager::getLastBarWallTimeUs() const {
  uint32_t phase = _currentTick % 96;
  return _lastTickTimeUs - (uint32_t)(phase * _pllTickInterval);
}

float ClockManager::getTickIntervalUs() const {
  return _pllTickInterval;
}
```

### Build gate

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

**Exigences** :
- Exit 0
- 0 nouveau warning
- RAM delta < +50 B (juste 4 getters, pas de nouveau membre)
- Flash delta < +200 B

### Auto-review

```bash
grep -n "getLastTickWallTimeUs\|getLastBeatWallTimeUs\|getLastBarWallTimeUs\|getTickIntervalUs" src/midi/ClockManager.h src/midi/ClockManager.cpp
```

Confirmer :
- 4 déclarations dans .h
- 4 définitions dans .cpp
- Aucun call-site externe (sera ajouté en C2)

### HW gate

**Aucun.** Les getters sont nouveaux, non appelés. Pas d'effet observable au boot ni au runtime. Le boot et tous les comportements Phase 2 restent identiques.

### Commit message

```
feat(clock): expose master grid wall time getters

Ajout de getLastTickWallTimeUs / getLastBeatWallTimeUs / getLastBarWallTimeUs /
getTickIntervalUs au public API de ClockManager. Dérivés de _lastTickTimeUs +
_pllTickInterval déjà existants. Aucun call-site, aucun overhead runtime.

Préparation pour LoopEngine Master Sync (cf docs/superpowers/specs/Illpad_Master_Sync.md §2.3).
```

---

## §3 Commit 2 — LoopEngine Master Sync activation atomique

### Objectif

Activer la nouvelle logique Master Sync en une transition atomique : recordStart anchored, Auto-Stop PENDING_CLOSE, suppression de stopRecording rescale+deadzone.

### Fichiers touchés

- `src/loop/LoopEngine.h` (ajout champs + déclarations méthodes)
- `src/loop/LoopEngine.cpp` (ajout/modification/suppression)

### Code à ajouter — LoopEngine.h

**Section private members, après `_padHeldLive[NUM_KEYS]` ligne ~263** :

```cpp
// --- Auto-Stop PENDING_CLOSE (Master Sync spec §3.2) ---
// Set true par tapRec sur RECORDING quand quantize != FREE.
// Consommé par update() phase 1 au boundary tick → commitRecordingClose.
bool             _recordingPendingClose;
uint32_t         _recordingPendingCloseTick;   // tick master cible
```

**Section private helpers, après `commitWaitingAction` ligne ~294** :

```cpp
// Master Sync : close + démarrage playback au boundary tick (BEAT/BAR).
void commitRecordingClose(MidiTransport& transport);
// Master Sync FREE : close immédiat tap-to-tap, sans PENDING_CLOSE.
void closeRecordingImmediate(MidiTransport& transport);
```

**`stopRecording` reste déclarée mais sa logique change radicalement** (devient dispatcher FREE vs BEAT/BAR).

### Code à modifier — LoopEngine.cpp

#### Constructeur — init nouveaux champs

Ligne ~43 après `_clearFired(false)`, ajouter à la liste d'init :

```cpp
  , _recordingPendingClose(false)
  , _recordingPendingCloseTick(0)
```

#### `capturePadEvent` — anchor recordStart selon quantize (spec §3.1)

Lignes 291-296 actuelles :

```cpp
// First-press latches recordStart + recordBpm (invariant §23.5)
if (!_recordFirstPressDone && isNoteOn) {
  _recordStartUs = nowUs;
  _recordBpm = _clock ? _clock->getSmoothedBPM() : 120;
  if (_recordBpm == 0) _recordBpm = 120;
  _recordFirstPressDone = true;
}
```

Remplacer par :

```cpp
// First-press latches recordStart + recordBpm (invariant §23.5)
// Master Sync (spec §3.1) : recordStart anchored selon quantize.
//   FREE → micros() (tap-to-tap, hors grille)
//   BEAT → lastBeatWallTime (anchor sur master beat)
//   BAR  → lastBarWallTime (anchor sur master bar)
if (!_recordFirstPressDone && isNoteOn) {
  if (_quantize == LOOP_QUANT_FREE || !_clock) {
    _recordStartUs = nowUs;
  } else if (_quantize == LOOP_QUANT_BEAT) {
    _recordStartUs = _clock->getLastBeatWallTimeUs();
  } else {  // LOOP_QUANT_BAR
    _recordStartUs = _clock->getLastBarWallTimeUs();
  }
  _recordBpm = _clock ? _clock->getSmoothedBPM() : 120;
  if (_recordBpm == 0) _recordBpm = 120;
  _recordFirstPressDone = true;
}
```

#### `tapRec` — branche RECORDING (spec §3.2 Auto-Stop)

Lignes 135-137 actuelles :

```cpp
case LoopState::RECORDING:
  stopRecording(transport);  // bar-snap + → PLAYING via startPlayback interne
  break;
```

Remplacer par :

```cpp
case LoopState::RECORDING:
  // Master Sync (spec §3.2) : Auto-Stop dispatch selon quantize.
  if (_quantize == LOOP_QUANT_FREE) {
    // FREE : close immédiat tap-to-tap
    closeRecordingImmediate(transport);
  } else {
    // BEAT/BAR : armer pending close, capture continue jusqu'au boundary
    if (_recordingPendingClose) {
      // BS-9 : 2e tap REC pendant PENDING_CLOSE déjà actif → ignoré
      break;
    }
    _recordingPendingCloseTick = computeNextBoundaryTick(_quantize);
    _recordingPendingClose = true;
  }
  break;
```

#### `update` phase 1 — check pending boundary (spec §3.2)

Avant le bloc actuel "(1) WAITING_*" ligne 344-351, ajouter :

```cpp
// (0) Master Sync : commit PENDING_CLOSE au boundary tick (spec §3.2).
if (_state == LoopState::RECORDING && _recordingPendingClose) {
  if (_clock && _clock->getCurrentTick() >= _recordingPendingCloseTick) {
    commitRecordingClose(transport);
    // Si transition → PLAYING : _lastUpdateUs set au boundary wall time
    // par startPlayback dans commitRecordingClose. Phase 3 ci-dessous
    // tournera avec deltaUs = nowUs - boundaryWallTime, normal.
  }
}
```

#### Nouvelles méthodes (à ajouter en fin de fichier ou après stopRecording)

```cpp
// =================================================================
// commitRecordingClose — Master Sync Auto-Stop boundary commit (spec §3.2)
// =================================================================
// Appelé par update() phase 1 quand _recordingPendingCloseTick atteint.
// Termine RECORDING : calcule loopDur depuis le boundary, flush held pads,
// startPlayback ancré au boundary tick wall time.
// =================================================================
void LoopEngine::commitRecordingClose(MidiTransport& transport) {
  if (!_clock) {
    // Safety fallback : pas de clock → close immédiat tap-to-tap.
    closeRecordingImmediate(transport);
    return;
  }

  // 1. Calculer le wall time exact du boundary tick (spec §3.4.1).
  uint32_t currentTick = _clock->getCurrentTick();
  uint32_t diff = currentTick - _recordingPendingCloseTick;  // 0 si pile, >0 si catch-up
  float    tickInterval = _clock->getTickIntervalUs();
  uint32_t boundaryWallTime = _clock->getLastTickWallTimeUs()
                              - (uint32_t)(diff * tickInterval);

  // 2. Loop length = boundary - anchor (par construction multiple entier de quantize unit).
  _loopDurationUs = boundaryWallTime - _recordStartUs;
  uint32_t snapUnitTicks = (_quantize == LOOP_QUANT_BAR) ? TICKS_PER_BAR : TICKS_PER_BEAT;
  _loopBars = (uint16_t)(_loopDurationUs / ((uint32_t)(snapUnitTicks * tickInterval)));
  if (_loopBars < 1) _loopBars = 1;  // safety floor

  // 3. Flush held pads → noteOff inject au timestamp _loopDurationUs - 1.
  flushHeldPadsAsNoteOffs(_loopDurationUs > 0 ? _loopDurationUs - 1 : 0);

  // 4. M6 clamp : tout event >= _loopDurationUs ramené à _loopDurationUs - 1.
  uint16_t clampedCount = 0;
  for (uint16_t i = 0; i < _eventCount; i++) {
    if (_events[i].timestampUs >= _loopDurationUs) {
      _events[i].timestampUs = _loopDurationUs > 0 ? _loopDurationUs - 1 : 0;
      clampedCount++;
    }
  }
  #if DEBUG_SERIAL
  if (clampedCount > 0) {
    Serial.printf("[LOOP WARN] commitRecordingClose clamped %u events to loopDur=%lu us\n",
                  clampedCount, (unsigned long)_loopDurationUs);
  }
  #endif

  // 5. Reset pending close flag.
  _recordingPendingClose = false;
  _recordingPendingCloseTick = 0;

  // 6. Flush refcount + startPlayback ancré au boundary wall time.
  flushPendingNoteOffs(transport);
  startPlayback(transport, boundaryWallTime);

  #if DEBUG_SERIAL
  Serial.printf("[LOOP] commitRecordingClose ch=%u loopDur=%lu us bars=%u events=%u\n",
                _channel, (unsigned long)_loopDurationUs, _loopBars, _eventCount);
  #endif
}

// =================================================================
// closeRecordingImmediate — Master Sync FREE path (spec §3.3)
// =================================================================
// Path FREE strict tap-to-tap : pas de boundary, pas de PENDING_CLOSE.
// loopDur = rawDur exact. Flush held pads. startPlayback immédiat.
// Appelé directement par tapRec(RECORDING) si quantize == FREE,
// et par commitRecordingClose comme fallback si !_clock.
// =================================================================
void LoopEngine::closeRecordingImmediate(MidiTransport& transport) {
  uint32_t nowUs = micros();
  if (!_recordFirstPressDone || _eventCount == 0) {
    // Buffer vide → revert EMPTY (comportement actuel préservé).
    _state = LoopState::EMPTY;
    return;
  }

  // 1. Loop length = rawDur exact (pas de snap, pas de rescale).
  _loopDurationUs = nowUs - _recordStartUs;
  _loopBars = 1;  // sémantique FREE : pas de bar logique, on garde 1 pour cohérence struct.

  // 2. Flush held pads → noteOff inject à _loopDurationUs - 1.
  flushHeldPadsAsNoteOffs(_loopDurationUs > 0 ? _loopDurationUs - 1 : 0);

  // 3. M6 clamp defense in depth.
  for (uint16_t i = 0; i < _eventCount; i++) {
    if (_events[i].timestampUs >= _loopDurationUs) {
      _events[i].timestampUs = _loopDurationUs > 0 ? _loopDurationUs - 1 : 0;
    }
  }

  // 4. Flush refcount + startPlayback ancré sur nowUs (pas de grille master).
  flushPendingNoteOffs(transport);
  startPlayback(transport, micros());  // re-capture après flushPendingNoteOffs pour minimiser drift.

  #if DEBUG_SERIAL
  Serial.printf("[LOOP] closeRecordingImmediate (FREE) ch=%u loopDur=%lu us events=%u\n",
                _channel, (unsigned long)_loopDurationUs, _eventCount);
  #endif
}
```

#### `stopRecording` — supprimer la méthode entière

Lignes 504-571 actuelles : **supprimer**. La logique est répartie dans `commitRecordingClose` (BEAT/BAR) et `closeRecordingImmediate` (FREE).

**Note** : `stopRecording` n'a aucun call-site externe au LoopEngine (toujours appelé via `tapRec`). Vérifier par grep :
```bash
grep -rn "stopRecording" src/ docs/  # devrait montrer 0 call-site externe après suppression
```

### Build gate

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

**Exigences** :
- Exit 0
- 0 nouveau warning
- RAM delta < +50 B (2 champs nouveaux ~6 B par engine × 4 = 24 B max)
- Flash delta peut être négatif (suppression stopRecording > ajouts)

### Auto-review (static read-back)

```bash
# Vérifier que stopRecording est supprimé partout
grep -rn "stopRecording" src/

# Vérifier que les nouvelles méthodes sont déclarées + définies
grep -n "commitRecordingClose\|closeRecordingImmediate" src/loop/LoopEngine.h src/loop/LoopEngine.cpp

# Vérifier les call-sites cohérents
grep -n "_recordingPendingClose" src/loop/LoopEngine.cpp  # devrait apparaître ~5 fois (init, set, check, reset, doc comment)
```

Attendu :
- `stopRecording` 0 hit
- `commitRecordingClose` 2 hits (h + cpp)
- `closeRecordingImmediate` 2 hits (h + cpp)
- `_recordingPendingClose` ~5 hits

### HW gate

**G1 à G9 selon spec §10.** Validation HW Loïc requise avant commit gate.

Résumé pratique :
- **G1** : record BEAT, vérifier alignement master beat au DAW.
- **G2** : record BAR, vérifier alignement bar.
- **G3** : ARP NORMAL 1/4 + LOOP BEAT, sync visuelle au DAW.
- **G4** : LOOP1 BEAT 4 beats + LOOP2 BEAT 2 beats, wraps coïncident.
- **G5** : LOOP FREE, dérive intentionnelle visible.
- **G6** : gestes refusés/ignorés pendant PENDING_CLOSE.
- **G7** : capture continue pendant PENDING_CLOSE (α).
- **G8** : held pad through boundary commit.
- **G9** : tempo change pendant PENDING_CLOSE.

### Commit message

```
feat(loop): Master Sync — Auto-Stop + master grid anchor (spec Illpad_Master_Sync.md)

Pivot algorithmique vers standard industrie (Boss/Mobius/EHX/Loopy). Remplace
le rescale silencieux des timestamps stockés par Auto-Stop boundary-aware.

Changements LoopEngine :
- _recordStartUs anchored à lastBeatWallTime (BEAT) ou lastBarWallTime (BAR)
  au 1er pad press. FREE garde micros() (tap-to-tap hors grille).
- tapRec(RECORDING) entre en PENDING_CLOSE pour BEAT/BAR (flag sur RECORDING,
  pas nouvel état). FREE → close immédiat.
- update() phase 1 commit PENDING_CLOSE quand boundary tick atteint.
- commitRecordingClose : loopDur = boundary - anchor (multiple entier par
  construction), flush held pads, startPlayback ancré au boundary wall time.
- closeRecordingImmediate : path FREE strict tap-to-tap, loopDur = rawDur.
- stopRecording entière supprimée (logique répartie dans les 2 nouvelles).
- Tous les autres invariants Phase 2 préservés (M3, M6, M8, B-N1, B-N2, B2, etc.).

Effet musical : ARP Beat et LOOP BEAT/BAR partagent la grille master tick.
Wraps LOOP tombent sur master beats. Loops mutuellement alignés.
FREE = explicitement hors grille (par design).

HW gates G1-G9 validés (cf spec §10).
```

---

## §4 Commit 3 — Doc-sync

### Objectif

Synchroniser la doc-écosystème post-implémentation. Pas de code touché.

### Fichiers touchés

| Fichier | Modification |
|---|---|
| `docs/superpowers/specs/2026-04-19-loop-mode-design.md` | §7 réécriture paragraphe stopRecording. §17 ajout §17.1 quantize au record. §24 ajout non-goal "pas de rescale silencieux". §28 ajout ligne traçabilité Master Sync. |
| `STATUS.md` | Section "Focus courant" : ajouter Master Sync close. Ajouter colonne dans tableau Phase 2 (ou nouvelle section dédiée). |
| `docs/superpowers/LOOP_PROGRESS.md` | Jalon "Master Sync close 2026-05-19" |
| `docs/reference/runtime-flows.md` | Vérifier si REC stop flow décrit — patcher si oui. |
| `docs/reference/nvs-reference.md` | Aucun changement (NVS layout inchangé). Confirmer. |

### Build gate

n/a (doc-only).

### HW gate

n/a.

### Commit message

```
docs(loop): doc-sync Master Sync — parent spec amendée + STATUS + LOOP_PROGRESS

Sync écosystème docs post-implémentation Master Sync (commit XXX).

- Parent spec 2026-04-19-loop-mode-design.md §7+§17.1+§24+§28 amendée. Cross-refs
  ajoutées vers Illpad_Master_Sync.md.
- STATUS.md "Focus courant" mis à jour : Master Sync close, prochaine étape
  reste Phase 3 (Tool 3 b1 + Tool 4 ext + retrait dev seed M7).
- LOOP_PROGRESS.md : jalon Master Sync 2026-05-19 ajouté.
- runtime-flows.md : REC stop flow patché (Auto-Stop + Anchor au lieu de
  bar-snap rescale).
```

---

## §5 Rollback plan

Si C2 échoue HW gates G1-G9 ou révèle une regression Phase 2 :

1. **Revert C2** : `git revert <C2 hash>` — restaure stopRecording, rescale, etc.
2. **Garder C1** : les getters ClockManager ne dérangent rien. Pas besoin de revert.
3. **Diagnostic** : conserver les traces HW failure pour analyse offline.
4. **Re-spec** : si bug structurel, amender Illpad_Master_Sync.md, re-brainstorm, refaire C2.

Pas de re-flash custom requis — revert + flash normal suffit (Zero Migration Policy NVS = pas d'impact sur données user).

---

## §6 Definition of Done

- [ ] C1 buildé clean, committé.
- [ ] C2 buildé clean, **G1-G9 validés HW Loïc**, committé.
- [ ] C3 doc-sync committé.
- [ ] Parent spec amendée, cohérence vérifiée vs Illpad_Master_Sync.md.
- [ ] STATUS.md à jour.
- [ ] Aucune régression Phase 2 (HW Loïc confirme : multi-bank toggle, panic, bank switch guard, overdub, etc. fonctionnent identiques).
- [ ] Build final RAM delta négligeable, Flash delta proche de 0 (suppression ~ ajouts).

---

**Fin du plan.** Prêt à exécuter sur signal Loïc.
