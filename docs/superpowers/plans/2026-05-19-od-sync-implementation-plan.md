# OD Sync — plan d'implémentation

**Date** : 2026-05-19
**Statut** : DRAFT — à exécuter après validation Loïc.
**Branche cible** : `main` (autocommit on).
**Estimation** : ~2j coding + 1.5j HW + 0.5j doc-sync + 1j marge ≈ **5 jours**.

**Cross-refs** :
- Spec source : [`docs/superpowers/specs/Illpad_OD_Sync.md`](../specs/Illpad_OD_Sync.md)
- Spec sœur Master Sync (architecture base) : [`Illpad_Master_Sync.md`](../specs/Illpad_Master_Sync.md)
- Plan Master Sync (modèle de référence) : [`2026-05-19-master-sync-implementation-plan.md`](2026-05-19-master-sync-implementation-plan.md)
- Parent spec LOOP : [`2026-04-19-loop-mode-design.md`](../specs/2026-04-19-loop-mode-design.md) §8, §9, §21, §27, §28
- Code touché : [`src/loop/LoopEngine.{h,cpp}`](../../../src/loop/LoopEngine.h), [`src/main.cpp::processLoopMode`](../../../src/main.cpp)

---

## §0 Pré-flight

### Build initial vérifié post Master Sync C3
```
RAM:   29.1 % (95436 / 327680 B)
Flash: 22.1 % (737993 / 3342336 B)
SUCCESS
```

### Invariants à préserver (cf parent spec §23 et CLAUDE.md projet)

Inchangés vs Master Sync (cf plan Master Sync §0). Rappel des points sensibles OD-Sync :

- **§23.11** — ≤ 1 LOOP en REC/OD à instant t : `isLocked()` retourne true sur OVERDUBBING (inchangé).
- **B-N2 audit fix** — held pads injection : **LOGIQUE DÉPLACÉE** de `mergeOverdub` (supprimée) vers nouvelle méthode `commitOverdubExit` à OD exit. Même `flushHeldPadsAsNoteOffs(_playPositionUs)` patterm. HW gate G5 critique.
- **M3, M6, M8** — velocity strict, timestamp clamp, live monitor all states : tous préservés.
- **Master Sync** — anchor + Auto-Stop + WAITING_* : aucune interaction avec OD-Sync.

### Hors scope (à NE PAS toucher)

- Tools setup (Tool 3, 4, 5, 6, 7, 8) — Phase 3 territory.
- LedGrammar.cpp / LedController.cpp — Option β = pas de modif grammar/rendering.
- BankManager — `isLocked()` consumer reste consistant.
- ArpEngine / ArpScheduler.
- NvsManager + Store struct.
- ViewerSerial.
- ClockManager — pas touché (OD-Sync orthogonal à clock).

---

## §1 Décomposition en 4 commits

| Commit | Scope | LOC | Build gate | HW gate |
|---|---|---|---|---|
| **C1** | Buffer addition + helpers (inerte) | +60 / -0 | Compile clean, 0 warning | Aucun (pas de runtime change) |
| **C2** | Immediate-merge activation + B-N2 déplacement | +60 / -110 | Compile clean, 0 warning | G1, G5, G6 (live growth, held pad, _playNextEventIdx) |
| **C3** | Diff swap + CLEAR dispatch + LED Option β | +80 / -0 | Compile clean, 0 warning | G2, G3, G4, G7, G8, G9, G10 |
| **C4** | Doc-sync | doc-only | n/a | Aucun |

**Total net** : ~+90 LOC (proche estimation spec §10 +101). Plus gros que Master Sync (+30 LOC net) mais bien décomposé.

**Principe de cohérence par commit** :
- C1 = inerte (nouvelles APIs et structures, zéro callsite).
- C2 = activation atomique de l'immediate-merge (buffer alternate + capture redirect + B-N2 déplacée). État musical observable changeable, HW gate.
- C3 = ajout des gestes Cancel/Undo/Redo + LED triggers Option β. Cohérent comme un bloc.
- C4 = doc-only finalisation.

---

## §2 Commit C1 — Buffer addition + helpers (inerte runtime)

### Objectif

Ajouter l'infrastructure (`_eventsAlternate[]`, helpers `isNoteOnAt` / `findLatestVelAt`) sans toucher au runtime. Build clean, comportement Phase 2 + Master Sync inchangé.

### Fichiers touchés

- `src/loop/LoopEngine.h`
- `src/loop/LoopEngine.cpp`

### Code à ajouter — LoopEngine.h

**Section private members, après `_recordingPendingClose` / `_recordingPendingCloseTick`** :

```cpp
  // --- OD-Sync : snapshot 1-level Undo/Redo toggle (spec Illpad_OD_Sync.md §2) ---
  // _eventsAlternate : "l'autre version" du buffer pour swap Undo/Redo.
  // - Au tap REC sur PLAYING/STOPPED → snapshot du pré-OD content.
  // - Au swap (Cancel pendant OD ou Undo/Redo court CLEAR en PLAYING/STOPPED) :
  //   échange _events ↔ _eventsAlternate, diff musical par note (§6).
  // _alternateValid : gating — false si aucun OD ne s'est produit depuis wipe/boot.
  LoopEvent        _eventsAlternate[MAX_LOOP_EVENTS];
  uint16_t         _eventsAlternateCount;
  bool             _alternateValid;
```

**Section private helpers, après `applyVelocityVariation`** :

```cpp
  // --- OD-Sync helpers (spec §6.3) ---
  // isNoteOnAt : état "audible" d'une note à position pos dans un buffer trié.
  // Walk les events jusqu'à pos, suit les transitions noteOn/noteOff matching note.
  bool isNoteOnAt(LoopEvent* buf, uint16_t count, uint8_t note, uint32_t pos) const;
  // findLatestVelAt : velocity du dernier noteOn matching note ≤ pos.
  // Fallback DEFAULT_BASE_VELOCITY si aucun event matching.
  uint8_t findLatestVelAt(LoopEvent* buf, uint16_t count, uint8_t note, uint32_t pos) const;
```

### Code à ajouter — LoopEngine.cpp

**Constructeur — init nouveaux champs** (après init `_recordingPendingCloseTick(0)`) :

```cpp
  , _eventsAlternateCount(0)
  , _alternateValid(false)
{
  // existing memset / loops ...
  // Ajouter init _eventsAlternate :
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) {
    _eventsAlternate[i].active = false;
  }
}
```

**Helpers implementations — en fin de fichier ou avec les autres helpers** :

```cpp
// =================================================================
// isNoteOnAt — état "on" d'une note à position pos dans buffer trié
// =================================================================
// Walk les events ≤ pos en suivant les transitions noteOn (vel > 0) / noteOff (vel == 0).
// Le dernier event matching détermine l'état audible à pos.
// =================================================================
bool LoopEngine::isNoteOnAt(LoopEvent* buf, uint16_t count, uint8_t note, uint32_t pos) const {
  bool on = false;
  for (uint16_t i = 0; i < count; i++) {
    if (!buf[i].active) break;
    if (buf[i].timestampUs > pos) break;       // events triés, on dépasse
    if (buf[i].midiNote != note) continue;     // pas la note cherchée
    on = (buf[i].velocity > 0);                // dernier event matching avant pos
  }
  return on;
}

// =================================================================
// findLatestVelAt — velocity du dernier noteOn matching note avant pos
// =================================================================
uint8_t LoopEngine::findLatestVelAt(LoopEvent* buf, uint16_t count, uint8_t note, uint32_t pos) const {
  uint8_t vel = DEFAULT_BASE_VELOCITY;   // fallback safe
  for (uint16_t i = 0; i < count; i++) {
    if (!buf[i].active) break;
    if (buf[i].timestampUs > pos) break;
    if (buf[i].midiNote != note) continue;
    if (buf[i].velocity > 0) vel = buf[i].velocity;
  }
  return vel;
}
```

### Build gate

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

**Exigences** :
- Exit 0
- 0 nouveau warning
- RAM delta : +8 KB par engine × 4 banks = **+32 KB**. RAM 29.1% → ~38.5% (target 127 KB / 320 KB). Confortable.
- Flash delta : ~+200 B (helpers code).

### Auto-review

```bash
# Vérifier déclarations + définitions des helpers
grep -n "isNoteOnAt\|findLatestVelAt\|_eventsAlternate\|_alternateValid" src/loop/LoopEngine.h src/loop/LoopEngine.cpp

# Vérifier aucun callsite externe (sera ajouté en C2/C3)
grep -rn "swapForUndoRedo\|cancelOverdub\|commitOverdubExit" src/
# devrait retourner 0 hit
```

### HW gate

**Aucun.** Pas de runtime change. Boot et tous les comportements Master Sync + Phase 2 restent identiques.

### Commit message

```
feat(loop): OD-Sync C1 — buffer alternate + diff helpers (inerte)

Ajout de l'infrastructure pour le pivot OD-Sync :
- _eventsAlternate[MAX_LOOP_EVENTS] = buffer "l'autre version" pour Undo/Redo toggle
- _eventsAlternateCount + _alternateValid flag de gating
- Helpers isNoteOnAt / findLatestVelAt (spec §6.3)

Aucun callsite externe dans ce commit. Pas de runtime change.
RAM +32 KB (8 KB × 4 banks), Flash +~200 B, build clean.

Préparation pour OD-Sync C2 immediate-merge activation.

Spec : docs/superpowers/specs/Illpad_OD_Sync.md §2 + §6.3
Plan : docs/superpowers/plans/2026-05-19-od-sync-implementation-plan.md §2 C1
```

---

## §3 Commit C2 — Immediate-merge activation + B-N2 déplacement

### Objectif

Activer la capture immediate-merge dans `_events[]` pendant OD. Snapshot à l'entrée OD. Remplacer `mergeOverdub` par `commitOverdubExit` (held pads inject). Suppression définitive de `_overdubEvents` + `mergeOverdub` + `abandonOverdub`.

### Fichiers touchés

- `src/loop/LoopEngine.h`
- `src/loop/LoopEngine.cpp`

### Modifications LoopEngine.h

**Supprimer** :
```cpp
LoopEvent        _overdubEvents[MAX_LOOP_OVERDUB_EVENTS];
uint16_t         _overdubCount;
bool mergeOverdub();
void abandonOverdub();
```

**Optionnel** : retirer `MAX_LOOP_OVERDUB_EVENTS` constante (déclarée plus haut). Vérifier qu'elle n'est pas référencée ailleurs avant suppression.

**Ajouter declaration** (avec les autres helpers privés) :
```cpp
// OD-Sync : exit commit (tap REC pendant OVERDUBBING).
// Held pads → noteOff inject dans _events à _playPositionUs (B-N2 déplacée).
// État → PLAYING. _eventsAlternate préservé (= pré-OD, pour post-Undo).
void commitOverdubExit(MidiTransport& transport);
```

### Modifications LoopEngine.cpp

#### Constructeur — supprimer init `_overdubCount`

```cpp
// Supprimer ligne : , _overdubCount(0)
// Supprimer boucle init _overdubEvents
```

#### `capturePadEvent` OVERDUBBING branch — immediate-merge

Lignes 332-345 actuelles (post audit) :

```cpp
if (_state == LoopState::OVERDUBBING) {
  uint32_t posInLoop = _playPositionUs;
  bool ok = insertEventSorted(_overdubEvents, _overdubCount, MAX_LOOP_OVERDUB_EVENTS,
                               posInLoop, padIndex, midiNote, velocity);
  if (!ok) {
    viewer::emitLoopBufferFull(_channel, "overdub");
  }
  return;
}
```

Remplacer par :

```cpp
if (_state == LoopState::OVERDUBBING) {
  // OD-Sync (spec §3.2) : immediate-merge insertion direct dans _events[].
  uint32_t posInLoop = _playPositionUs;
  bool ok = insertEventSorted(_events, _eventCount, MAX_LOOP_EVENTS,
                               posInLoop, padIndex, midiNote, velocity);
  if (!ok) {
    viewer::emitLoopBufferFull(_channel, "main");
    return;
  }

  // Recompute _playNextEventIdx pour éviter double-fire de l'event juste inséré
  // (le M8 live monitor l'a déjà émis au-dessus). Binary search depuis _playPositionUs.
  int32_t lo = 0;
  int32_t hi = (int32_t)_eventCount;
  while (lo < hi) {
    int32_t mid = (lo + hi) / 2;
    if (_events[mid].timestampUs <= _playPositionUs) lo = mid + 1;
    else hi = mid;
  }
  _playNextEventIdx = (uint16_t)lo;
  return;
}
```

#### `tapRec` PLAYING branche — snapshot pré-OD

Ligne 138-142 actuelle :

```cpp
case LoopState::PLAYING:
  // Enter OVERDUBBING — main buffer continues playback, overdub captures additions
  _overdubCount = 0;
  _state = LoopState::OVERDUBBING;
  break;
```

Remplacer par :

```cpp
case LoopState::PLAYING:
  // OD-Sync (spec §3.1) : snapshot _events → _eventsAlternate.
  // Sera utilisé pour Cancel (CLEAR pendant OD) et Undo/Redo (CLEAR court post-exit).
  memcpy(_eventsAlternate, _events, sizeof(_events));
  _eventsAlternateCount = _eventCount;
  _alternateValid = true;
  _state = LoopState::OVERDUBBING;
  break;
```

#### `tapRec` STOPPED branche (Q5 §28) — snapshot idem

Ligne 149-154 actuelle :

```cpp
case LoopState::STOPPED:
  // Q5 §28 : tap REC on STOPPED-loaded → PLAYING + OVERDUBBING simultaneously
  startPlayback(transport, micros());
  _overdubCount = 0;
  _state = LoopState::OVERDUBBING;
  break;
```

Remplacer par :

```cpp
case LoopState::STOPPED:
  // Q5 §28 : tap REC on STOPPED-loaded → PLAYING + OVERDUBBING simultaneously
  // OD-Sync : snapshot avant startPlayback (capture l'état figé pré-reprise).
  memcpy(_eventsAlternate, _events, sizeof(_events));
  _eventsAlternateCount = _eventCount;
  _alternateValid = true;
  startPlayback(transport, micros());
  _state = LoopState::OVERDUBBING;
  break;
```

#### `tapRec` OVERDUBBING branche — commitOverdubExit

Ligne 155-160 actuelle :

```cpp
case LoopState::OVERDUBBING:
  // M2 / M4 : mergeOverdub retourne false si capacité dépassée → abandon atomique silent.
  mergeOverdub();
  _state = LoopState::PLAYING;
  break;
```

Remplacer par :

```cpp
case LoopState::OVERDUBBING:
  // OD-Sync (spec §3.3) : exit commit. Held pads inject (B-N2 réincarnée).
  // _eventsAlternate préservé pour post-Undo (CLEAR court PLAYING).
  commitOverdubExit(transport);
  break;
```

#### `tapPlayStop` OVERDUBBING branche — no-op

Ligne 213-215 actuelle :

```cpp
case LoopState::OVERDUBBING:
  // Spec §8 : abandon overdub, stay PLAYING. A second tap then stops normally.
  abandonOverdub();
  _state = LoopState::PLAYING;
  break;
```

Remplacer par :

```cpp
case LoopState::OVERDUBBING:
  // OD-Sync (spec OD-3) : tap PLAY/STOP pendant OD est no-op.
  // Cancel se fait via tap CLEAR (cf §3.4).
  break;
```

#### Nouvelle méthode `commitOverdubExit`

Ajouter (place : après `commitRecordingClose` ou avec les autres helpers OD) :

```cpp
// =================================================================
// commitOverdubExit — OD-Sync exit commit (spec Illpad_OD_Sync.md §3.3)
// =================================================================
// Appelé par tapRec sur OVERDUBBING. Held pads injection (B-N2 réincarnée
// de l'ancienne mergeOverdub) : pour chaque pad encore tenu physiquement,
// inject noteOff dans _events à _playPositionUs. Sans ça : noteOn capturé
// pendant OD sans noteOff matching → refcount grows unbounded au wrap.
// État → PLAYING. _eventsAlternate intact (pour post-Undo via CLEAR court).
// =================================================================
void LoopEngine::commitOverdubExit(MidiTransport& transport) {
  // Held pads inject (B-N2 logic, déplacée de mergeOverdub).
  // flushHeldPadsAsNoteOffs utilise insertEventSorted → buffer reste trié.
  // IMPORTANT : ne PAS reset _padHeldLive ici. Pad encore physiquement tenu —
  // le release naturel (capturePadEvent en PLAYING) appellera refCountNoteOff.
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    if (!_padHeldLive[pad]) continue;
    insertEventSorted(_events, _eventCount, MAX_LOOP_EVENTS,
                       _playPositionUs, pad, resolvePadToMidiNote(pad), /*velocity=*/0);
    // Si insertEventSorted retourne false (buffer full) : best-effort, refcount sera
    // silencé au release naturel via M8 refCountNoteOff. Acceptable edge.
  }

  // Recompute _playNextEventIdx après les insertions (logique identique à C2 capturePadEvent).
  int32_t lo = 0;
  int32_t hi = (int32_t)_eventCount;
  while (lo < hi) {
    int32_t mid = (lo + hi) / 2;
    if (_events[mid].timestampUs <= _playPositionUs) lo = mid + 1;
    else hi = mid;
  }
  _playNextEventIdx = (uint16_t)lo;

  _state = LoopState::PLAYING;

  #if DEBUG_SERIAL
  Serial.printf("[LOOP] commitOverdubExit ch=%u eventCount=%u alternateCount=%u\n",
                _channel, _eventCount, _eventsAlternateCount);
  #endif
}
```

#### Suppression `mergeOverdub` et `abandonOverdub`

Lignes 805-887 actuelles : **supprimer entièrement** les deux méthodes.

Vérifier qu'aucun call-site externe ne reste :
```bash
grep -rn "mergeOverdub\|abandonOverdub" src/
# devrait retourner 0 hit après suppression
```

#### Nettoyage commentaires obsolètes

Mettre à jour les commentaires dans :
- `_padHeldLive` doc (LoopEngine.h:264) — mentionner `commitOverdubExit` au lieu de `mergeOverdub`.
- `flushPendingNoteOffs` doc (callers list) — pas affecté mais à vérifier.

### Build gate

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

**Exigences** :
- Exit 0, 0 warning.
- RAM delta : -4 KB (`_overdubEvents` supprimé). Total post C2 : ~+28 KB net vs pre-OD-Sync.
- Flash delta : -3 KB à -5 KB (suppression mergeOverdub + abandonOverdub > additions).

### Auto-review (static read-back)

```bash
# Vérifier que _overdubEvents / mergeOverdub / abandonOverdub sont supprimés
grep -rn "_overdubEvents\|_overdubCount\|mergeOverdub\|abandonOverdub" src/
# devrait retourner 0 hit (sauf éventuellement docs/specs en commentaires obsolètes)

# Vérifier commitOverdubExit déclaré + défini + utilisé
grep -n "commitOverdubExit" src/loop/LoopEngine.h src/loop/LoopEngine.cpp
# 1 declaration .h + 1 définition .cpp + 1 callsite tapRec OVERDUBBING

# Vérifier snapshot dans tapRec PLAYING + STOPPED
grep -n "memcpy(_eventsAlternate" src/loop/LoopEngine.cpp
# 2 callsites (PLAYING + STOPPED)

# Vérifier _alternateValid = true au snapshot
grep -n "_alternateValid" src/loop/LoopEngine.cpp
# 2 set à true (PLAYING + STOPPED snapshot) + init false (constructeur)
```

### HW gate

**G1** — Live loop growth pendant OD (spec §12 G1) :
1. Bank LOOP BEAT. Record K+SN, 4 beats. Exit.
2. Tap REC PLAYING → OD. LED Amber.
3. Press pad HH à position 0.3 du cycle 1. MIDI noteOn HH live.
4. Cycle 2 (wrap) : à position 0.3, HH joue automatiquement depuis buffer. ✓

**G5** — Held pad through OD exit (B-N2 déplacée critique) :
1. PLAYING. Tap REC → OD.
2. Hold pad #4 à position 0.3.
3. Tap REC à position 0.6 → exit commit (commitOverdubExit). État PLAYING.
4. Trace serial : "commitOverdubExit ch=N eventCount=M". Vérifier noteOff event injecté à 0.6.
5. Cycle suivant : pas de stuck note au DAW. Buffer fire noteOn 0.3, noteOff 0.6 proprement.
6. User release pad → MIDI noteOff via refcount (refCountNoteOff). Pas de double trigger.

**G6** — `_playNextEventIdx` post-insert immediate-merge :
1. PLAYING avec events à positions 0.2 et 0.4.
2. Tap REC → OD.
3. À position 0.3 (entre les 2 events), press pad nouveau.
4. Trace serial : event inséré à index entre les 2.
5. **Vérifier au DAW** : pas de double-fire. M8 live monitor noteOn ONE fois (rising edge).
6. Cycle 2 : à position 0.3, le nouvel event fire normalement (1 noteOn).

### Commit message

```
feat(loop): OD-Sync C2 — immediate-merge activation + B-N2 déplacement

Activation atomique du modèle immediate-merge OD :
- capturePadEvent OVERDUBBING : insert direct dans _events[] (pas de buffer
  temp). _playNextEventIdx recompute par binary search pour éviter double-fire.
- tapRec PLAYING/STOPPED entry : memcpy _events → _eventsAlternate (snapshot
  pré-OD). _alternateValid = true.
- tapRec OVERDUBBING : commitOverdubExit nouvelle méthode. Held pads inject
  (B-N2 réincarnée de mergeOverdub supprimée).
- tapPlayStop OVERDUBBING : no-op (OD-3 décision).

Suppression :
- _overdubEvents[128] + _overdubCount (~1 KB par engine, -4 KB total).
- mergeOverdub (M2 O(n+m) merge logic) + abandonOverdub méthodes entières.

Build clean : RAM ~+28 KB net (alt buffer +32 KB - overdub buffer -4 KB),
Flash ~-3 KB.

HW gates G1 (live loop growth), G5 (held pad through exit, B-N2 déplacée),
G6 (_playNextEventIdx post-insert no double-fire) validés.

Spec : docs/superpowers/specs/Illpad_OD_Sync.md §3.1 + §3.2 + §3.3
Plan : docs/superpowers/plans/2026-05-19-od-sync-implementation-plan.md §3 C2
```

---

## §4 Commit C3 — Diff swap + CLEAR dispatch + LED Option β

### Objectif

Ajouter les gestes Cancel (CLEAR pendant OD) + Undo/Redo (CLEAR court PLAYING/STOPPED). Diff swap musical preserve couche base + live press. Wipe étendu reset alternate. LED triggers Option β.

### Fichiers touchés

- `src/loop/LoopEngine.h`
- `src/loop/LoopEngine.cpp`
- `src/main.cpp::processLoopMode`

### Modifications LoopEngine.h

**Ajouter declarations methods** (avec les autres helpers OD) :

```cpp
// OD-Sync : Undo/Redo toggle (CLEAR court PLAYING/STOPPED) et Cancel
// (CLEAR pendant OD). Diff musical par note + swap _events ↔ _eventsAlternate.
void swapForUndoRedo(MidiTransport& transport);

// OD-Sync : Cancel pendant OD = swap + state → PLAYING.
// Appelé par processLoopMode au rising edge CLEAR si state == OVERDUBBING.
void cancelOverdub(MidiTransport& transport);
```

### Modifications LoopEngine.cpp

#### Nouvelle méthode `swapForUndoRedo`

```cpp
// =================================================================
// swapForUndoRedo — OD-Sync diff swap musical (spec §6.2)
// =================================================================
// Échange _events ↔ _eventsAlternate avec MIDI ciblé : seules les notes
// dont l'état audible change firent/coupent. Couche base et live press
// préservées par construction.
// =================================================================
void LoopEngine::swapForUndoRedo(MidiTransport& transport) {
  if (!_alternateValid) return;   // pas de snapshot, no-op safe

  // Étape 1 : compute states before (dans _events) et after (dans _eventsAlternate).
  bool before[128], after[128];
  for (uint8_t n = 0; n < 128; n++) {
    before[n] = isNoteOnAt(_events, _eventCount, n, _playPositionUs);
    after[n]  = isNoteOnAt(_eventsAlternate, _eventsAlternateCount, n, _playPositionUs);
  }

  // Étape 2 : MIDI ciblé pour notes où l'état change, sauf si live press tient la note.
  for (uint8_t n = 0; n < 128; n++) {
    if (before[n] == after[n]) continue;   // pas de change → couche base préservée

    // Live press protection : check si un pad mapping vers cette note est tenu.
    bool liveOn = false;
    for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
      if (_padHeldLive[pad] && resolvePadToMidiNote(pad) == n) {
        liveOn = true;
        break;
      }
    }

    if (before[n] && !after[n]) {
      // Note disparait. NoteOff seulement si pas live-press.
      if (!liveOn) transport.sendNoteOn(_channel, n, 0);  // vel 0 = noteOff
    } else {
      // !before && after : note apparait.
      if (!liveOn) {
        uint8_t vel = findLatestVelAt(_eventsAlternate, _eventsAlternateCount, n, _playPositionUs);
        transport.sendNoteOn(_channel, n, vel);
      }
    }
  }

  // Étape 3 : swap buffers via memcpy via stack (LoopEvent[1024] = 8 KB, OK sur stack ESP32-S3 8 KB main task).
  // Alternative pour économiser stack : swap par pointer logique (mais struct member, pas faisable simplement).
  // Solution simple : memcpy in-place via std::swap_ranges or sequential swap.
  // Ici : approche brute via temporary via memcpy en blocs (32B chunks pour limiter stack).
  // Plus simple : juste memcpy via _events temp dans flash (declared static). À voir si gain stack utile.
  // Pour la v1 : memcpy via buffer temp local. Si stack overflow, refactor.
  LoopEvent temp[MAX_LOOP_EVENTS];  // 8 KB stack — vérifier task stack ≥ 16 KB
  memcpy(temp,              _events,          sizeof(_events));
  memcpy(_events,           _eventsAlternate, sizeof(_eventsAlternate));
  memcpy(_eventsAlternate,  temp,             sizeof(temp));
  uint16_t tempCount = _eventCount;
  _eventCount = _eventsAlternateCount;
  _eventsAlternateCount = tempCount;

  // Étape 4 : recompute _noteRefCount depuis nouveau _events à _playPositionUs + live press.
  memset(_noteRefCount, 0, sizeof(_noteRefCount));
  for (uint8_t n = 0; n < 128; n++) {
    if (after[n]) _noteRefCount[n] = 1;  // buffer attendu on (after = post-swap state)
  }
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    if (_padHeldLive[pad]) {
      uint8_t note = resolvePadToMidiNote(pad);
      if (_noteRefCount[note] < 255) _noteRefCount[note]++;   // live press contribution
    }
  }

  // Étape 5 : recompute _playNextEventIdx par binary search.
  int32_t lo = 0;
  int32_t hi = (int32_t)_eventCount;
  while (lo < hi) {
    int32_t mid = (lo + hi) / 2;
    if (_events[mid].timestampUs <= _playPositionUs) lo = mid + 1;
    else hi = mid;
  }
  _playNextEventIdx = (uint16_t)lo;

  #if DEBUG_SERIAL
  Serial.printf("[LOOP] swapForUndoRedo ch=%u eventCount=%u alternateCount=%u\n",
                _channel, _eventCount, _eventsAlternateCount);
  #endif
}

// =================================================================
// cancelOverdub — OD-Sync Cancel pendant OD (spec §3.4)
// =================================================================
// Appelé par processLoopMode au rising edge CLEAR si state == OVERDUBBING.
// Swap + state → PLAYING. _alternateValid reste true (Redo possible juste après).
// =================================================================
void LoopEngine::cancelOverdub(MidiTransport& transport) {
  if (_state != LoopState::OVERDUBBING) return;   // safety
  swapForUndoRedo(transport);
  _state = LoopState::PLAYING;
  #if DEBUG_SERIAL
  Serial.printf("[LOOP] cancelOverdub ch=%u → PLAYING\n", _channel);
  #endif
}
```

**Note stack** : `LoopEvent temp[MAX_LOOP_EVENTS]` = 8 KB sur la stack. Vérifier que le task main a >= 16 KB stack. Si problème, alternative : `static LoopEvent g_swapTemp[MAX_LOOP_EVENTS]` global (mais coût SRAM +8 KB permanent). Démarrer en stack, refactor si overflow détecté en HW gate.

#### Modification `longPressClear` — wipe étendu (reset alternate)

Ligne 235-248 actuelle :

```cpp
void LoopEngine::longPressClear(MidiTransport& transport) {
  #if DEBUG_SERIAL
  Serial.printf("[LOOP] longPressClear ch=%u state=%u\n", _channel, (unsigned)_state);
  #endif
  _clearFired = true;
  if (isLocked()) return;
  flushPendingNoteOffs(transport);
  _eventCount = 0;
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) _events[i].active = false;
  _overdubCount = 0;  // ← à supprimer en C2 si pas déjà fait
  _loopDurationUs = 0;
  _loopBars = 0;
  _state = LoopState::EMPTY;
  ...
}
```

Ajouter après reset `_events[]` :

```cpp
  // OD-Sync (spec §4.2) : wipe reset aussi _eventsAlternate.
  _eventsAlternateCount = 0;
  for (uint16_t i = 0; i < MAX_LOOP_EVENTS; i++) _eventsAlternate[i].active = false;
  _alternateValid = false;
```

### Modifications main.cpp::processLoopMode CLEAR handling

Lignes 824-835 actuelles :

```cpp
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
```

Remplacer par dispatch state-aware (spec §3.4 + §4) :

```cpp
if (le->getClearPad() != 0xFF) {
  uint8_t clearPadIdx = le->getClearPad();
  bool clearHeld = state.keyIsPressed[clearPadIdx];
  bool clearWasHeld = s_lastKeys[clearPadIdx];
  LoopState curState = le->getState();

  // Rising edge sur CLEAR
  if (clearHeld && !clearWasHeld) {
    if (curState == LoopState::OVERDUBBING) {
      // OD-Sync : tap CLEAR pendant OD = exit + rollback (Cancel).
      // Trigger immédiat sur rising edge (pas de timer pour OD).
      le->cancelOverdub(s_transport);
      s_leds.triggerEvent(EVT_LOOP_CLEAR);  // Option β : cyan rollback
    } else {
      // PLAYING/STOPPED/EMPTY/WAITING_* : démarrer timer long-press.
      le->notifyClearPressStart(now);
    }
  }

  // Hold
  if (clearHeld) {
    if (curState != LoopState::OVERDUBBING) {
      // Check long-press fire (existant)
      if (le->isClearHoldFired(now)) {
        le->longPressClear(s_transport);
        s_leds.triggerEvent(EVT_LOOP_CLEAR);
      }
    }
    // Si OVERDUBBING : pas de timer (le rising edge a déjà déclenché Cancel).
  }

  // Falling edge sur CLEAR
  if (!clearHeld && clearWasHeld) {
    if (curState != LoopState::OVERDUBBING) {
      // OD-Sync : check short-tap pour Undo/Redo.
      // Short-tap = release avant clearLoopTimerMs ET pas de wipe fire pendant.
      uint32_t pressStart = le->getClearPressStartMs();  // need new getter, see below
      bool wasShortTap = (pressStart != 0) && ((now - pressStart) < le->getClearLoopTimerMs())
                                           && !le->wasClearFired();
      if (wasShortTap && (curState == LoopState::PLAYING || curState == LoopState::STOPPED)) {
        // Détection sens Undo vs Redo par diff eventCount avant/après.
        uint16_t countBefore = le->getEventCount();
        le->swapForUndoRedo(s_transport);
        uint16_t countAfter = le->getEventCount();
        if (countAfter < countBefore) {
          s_leds.triggerEvent(EVT_STOP);     // Option β : couche retirée
        } else if (countAfter > countBefore) {
          s_leds.triggerEvent(EVT_PLAY);     // Option β : couche réintégrée
        }
        // Si countAfter == countBefore : swap mais same content (rare). No LED trigger.
      }
      le->notifyClearPressEnd();
    }
    // OVERDUBPER falling edge : pas d'action (cancelOverdub déjà fait au rising).
  }
}
```

**Getters à ajouter dans LoopEngine.h** (publics) :

```cpp
// OD-Sync getters pour processLoopMode CLEAR dispatch
uint32_t getClearPressStartMs() const { return _clearPressStartMs; }
uint16_t getClearLoopTimerMs() const  { return _clearLoopTimerMs; }
bool     wasClearFired() const        { return _clearFired; }
```

### Build gate

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

**Exigences** :
- Exit 0, 0 warning.
- RAM : pas de changement vs C2 (tout sur stack pour swap).
- Flash : ~+2 KB (méthodes ajoutées + dispatch logic).
- **Warning à monitor** : task stack usage. Si compiler avertit, considérer static buffer.

### Auto-review

```bash
# Vérifier les nouvelles méthodes
grep -n "swapForUndoRedo\|cancelOverdub" src/loop/LoopEngine.h src/loop/LoopEngine.cpp src/main.cpp

# Vérifier les getters publics
grep -n "getClearPressStartMs\|getClearLoopTimerMs\|wasClearFired" src/loop/LoopEngine.h src/main.cpp

# Vérifier wipe étendu
grep -n "_alternateValid = false" src/loop/LoopEngine.cpp
# devrait apparaître au moins 2 fois : constructeur + longPressClear
```

### HW gates

**G2** — Cancel pendant OD :
1. Loop K+SN. OD HH ajouté à position 0.3.
2. Tap CLEAR pendant OD. **Vérifier** :
   - LED Amber → Green (state PLAYING).
   - Flash cyan brief (EVT_LOOP_CLEAR).
   - Cycle suivant : K+SN audibles continus, **HH disparaît proprement**.
   - Pas de glitch sur K ni SN.

**G3** — K+SN+HH Undo/Redo toggle (la feature live signature) :
1. Loop K+SN+HH (post-OD commit).
2. Tap CLEAR court (release < 500ms). LED fade coral (EVT_STOP).
   - **Vérifier** : HH disparaît, K+SN continuent au cycle suivant.
3. Tap CLEAR court à nouveau. LED fade green (EVT_PLAY).
   - **Vérifier** : HH revient, K+SN continuent.
4. Alterner rapidement plusieurs fois. Pas de drift, pas de glitch K+SN.

**G4** — Live press protection au swap :
1. PLAYING avec K+SN+HH.
2. Hold pad K physiquement (live press).
3. Tap CLEAR court (Undo).
4. **Vérifier** : K live continue d'audible (pas de coupure). HH disparait. K du buffer disparait aussi mais live press maintient.
5. Release K. MIDI noteOff via refcount.

**G7** — Wipe étendu reset alternate :
1. Loop K+SN+HH (snapshot K+SN valide).
2. Long-press CLEAR (>= 500ms). EVT_LOOP_CLEAR.
3. État EMPTY. `_alternateValid` → false.
4. Tap CLEAR court juste après. **Vérifier** : no-op (swapForUndoRedo return early sur `_alternateValid == false`).

**G8** — Bank switch refusé pendant OD :
1. OVERDUBPER. Tap LEFT + autre bank pad.
2. **Vérifier** : silent deny (current behavior, isLocked OK).

**G9** — Multi-OD snapshot overwrite :
1. Loop K+SN. OD HH → exit. _eventsAlternate = K+SN.
2. OD perc → exit. _eventsAlternate écrasé = K+SN+HH.
3. Tap CLEAR court (Undo). Revient à K+SN+HH (sans perc).
4. Tap CLEAR court (Redo). Revient à K+SN+HH+perc.
5. **Vérifier** : pas d'Undo 2 niveaux en arrière (le HH du 1er OD reste). Cohérent 1-level.

**G10** — Tap PLAY/STOP pendant OD = no-op (OD-3) :
1. OVERDUBBING. Tap PLAY/STOP pad.
2. **Vérifier** : aucun MIDI parasite, aucune transition LED, OD continue normalement.

### Commit message

```
feat(loop): OD-Sync C3 — diff swap + CLEAR dispatch + LED Option β

Ajout des gestes Cancel pendant OD et Undo/Redo en PLAYING/STOPPED via
CLEAR contextuel. Diff musical par note préserve couche base et live press.

Méthodes ajoutées :
- swapForUndoRedo : diff before/after via isNoteOnAt + MIDI ciblé + memcpy
  swap + refcount recompute + _playNextEventIdx binary search.
- cancelOverdub : wrapper swapForUndoRedo + state → PLAYING (CLEAR pendant OD).

Wipe étendu (longPressClear) : reset _eventsAlternate + _alternateValid = false.

processLoopMode CLEAR dispatch state-aware :
- rising edge OVERDUBPER → cancelOverdub + EVT_LOOP_CLEAR (cyan).
- rising edge PLAYING/STOPPED → démarrer timer long-press.
- hold + isClearHoldFired (non-OD) → longPressClear + EVT_LOOP_CLEAR.
- falling edge short-release (non-OD, PLAYING/STOPPED) → swapForUndoRedo +
  EVT_STOP (couche retirée) ou EVT_PLAY (couche réintégrée) selon diff eventCount.

LED Option β : réutilisation EVT_LOOP_CLEAR / EVT_STOP / EVT_PLAY existants.
Pas de modif LedGrammar.cpp, pas de bump NVS.

Build clean. HW gates G2 (Cancel), G3 (K+SN+HH toggle live signature),
G4 (live press protection), G7 (wipe extended), G8 (bank switch refus),
G9 (multi-OD snapshot), G10 (PLAY/STOP no-op) validés.

Spec : docs/superpowers/specs/Illpad_OD_Sync.md §3.4 + §4 + §6 + §8
Plan : docs/superpowers/plans/2026-05-19-od-sync-implementation-plan.md §4 C3
```

---

## §5 Commit C4 — Doc-sync

### Objectif

Synchroniser la doc-écosystème post-implémentation. Pas de code touché.

### Fichiers touchés

| Fichier | Modification |
|---|---|
| `docs/superpowers/specs/2026-04-19-loop-mode-design.md` | §8 réécrite (immediate-merge + snapshot), §9 ajout §9.1 CLEAR contextuel, §21 note Option β, §27 ligne OD-Sync, §28 ligne traçabilité |
| `STATUS.md` | Focus courant : OD-Sync CLOSE. Phase 3 prochaine. |
| `docs/superpowers/LOOP_PROGRESS.md` | Tableau : ligne OD-Sync (entre Master Sync et Phase 3) avec commits + cross-refs |
| `docs/reference/runtime-flows.md` | Ajout §4 (ou similaire) "Overdub flow — immediate-merge + Undo/Redo toggle" |

### Build gate

n/a (doc-only).

### HW gate

n/a.

### Commit message

```
docs(loop): doc-sync OD-Sync — parent spec + STATUS + LOOP_PROGRESS + runtime-flows

Sync écosystème docs post-implémentation OD-Sync (commits XXX).

- Parent spec 2026-04-19-loop-mode-design.md §8 réécrite (immediate-merge +
  snapshot 1-level), §9.1 CLEAR contextuel 3 sens, §21 note Option β,
  §27 ligne OD-Sync entre Master Sync et Phase 3, §28 ligne traçabilité
  référençant les 15 décisions OD-1 à OD-15.
- STATUS.md focus : OD-Sync CLOSE.
- LOOP_PROGRESS.md : nouvelle ligne tableau OD-Sync.
- runtime-flows.md : §4 nouveau "Overdub flow — immediate-merge + Undo/Redo".
```

---

## §6 Rollback plan

Si C2 ou C3 échoue HW gates ou révèle regression :

1. **Revert C3 (si C3 fail)** : `git revert <C3 hash>`. Restaure CLEAR handler v1 + supprime swap.
2. **Revert C2 (si C2 fail)** : `git revert <C2 hash>` restaure `_overdubEvents` + `mergeOverdub`. **Mais** C3 dépend de C2 → revert cascading nécessaire (`git revert C3 C2`).
3. **Garder C1** : buffer + helpers seuls sont inertes, pas besoin de revert.

Pas de re-flash custom requis. Revert + flash normal suffit. NVS layout inchangé (Zero Migration OK).

Si régression musicale sur invariant existant (ex. stuck note, perte couche base) : revert immédiat sans diagnostic profond. Re-spec si nécessaire avant retenter.

---

## §7 Definition of Done

- [ ] C1 buildé clean, committé.
- [ ] C2 buildé clean, **G1 + G5 + G6 validés HW Loïc**, committé.
- [ ] C3 buildé clean, **G2 + G3 + G4 + G7 + G8 + G9 + G10 validés HW**, committé.
- [ ] C4 doc-sync committé.
- [ ] Parent spec amendée, cohérence vérifiée vs Illpad_OD_Sync.md.
- [ ] STATUS.md à jour.
- [ ] Aucune régression Phase 2 + Master Sync (HW Loïc confirme : recording flow, multi-bank, panic, bank switch, transport quantize fonctionnent inchangés).
- [ ] Build final RAM ~+28 KB net vs pre-OD-Sync, Flash delta marginale.

---

**Fin du plan.** Prêt à exécuter sur signal Loïc.
