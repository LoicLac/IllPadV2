# Illpad — OD Sync (immediate-merge overdub + 1-level Undo/Redo toggle)

**Date** : 2026-05-19
**Statut** : VALIDÉ post-brainstorm + audit feasibility 2026-05-19. À implémenter dans Paquet OD-Sync.
**Self-sufficient** : ce doc peut être lu seul. Cross-refs vers parent spec LOOP, Master Sync, et code source en pointeurs.

**Cross-refs** :
- Spec parent LOOP : [`2026-04-19-loop-mode-design.md`](2026-04-19-loop-mode-design.md) (sections impactées §8 overdub, §17 quantize, §21 LED, §27 phases)
- Spec sœur Master Sync : [`Illpad_Master_Sync.md`](Illpad_Master_Sync.md) (close-record paquet précédent, base architecturale)
- Design pivot d'origine (archivé) : [`docs/archive/2026-05-19-loop-algo-pivot-design.md`](../../archive/2026-05-19-loop-algo-pivot-design.md) (mentionnait OD comme out-of-scope Paquet A — cette spec adresse la suite)
- Code actuel : [`src/loop/LoopEngine.{h,cpp}`](../../../src/loop/LoopEngine.h), [`src/main.cpp::processLoopMode`](../../../src/main.cpp), [`src/core/LedGrammar.cpp`](../../../src/core/LedGrammar.cpp)
- LED grammar : [`2026-04-19-led-feedback-unified-design.md`](2026-04-19-led-feedback-unified-design.md)

---

## §0 Contexte

Le LOOP Phase 2 (close 2026-05-19) + Master Sync (close 2026-05-19) ont fixé deux problèmes : recording timestamps préservés (suppression rescale) et grille master commune (ARP+LOOP synchronisés). **Reste un problème musical** : pendant OVERDUBBING (OD), les events captés ne sont pas audibles en loop avant le `mergeOverdub` (geste tap REC d'exit). L'utilisateur joue dans le vide pendant l'OD — il entend ses presses live (M8 live monitor) mais pas la boucle qui se construit.

**Contre-modèle industrie** : Boss RC-505, Mobius, Sooperlooper, Loopy Pro — tous ont **immediate-merge** : chaque press pendant OD est inséré directement dans le main buffer, audible au cycle suivant. La "loop growth" est vécue live.

**Mais en immediate-merge, on perd l'abandon** (geste tap PLAY/STOP qui jetait le temp buffer pré-merge). Solution : **snapshot 1-level** du main buffer à l'entrée OD, qui permet un Undo/Redo toggle après-coup.

Cette spec capture le pivot complet vers immediate-merge avec snapshot + diff swap musical.

---

## §1 Principe directeur

> **Pendant l'OD, les events captés sont insérés DIRECTEMENT dans le main buffer (immediate-merge). Un snapshot pré-OD permet un Undo/Redo 1-level toggle sur la dernière couche.**

Conséquences pratiques observables :

- **Live loop growth** : un kick ajouté à position 0.3 pendant OD rejoue au cycle suivant à position 0.3, audible immédiatement. Pas d'attente d'exit OD pour entendre.
- **Cancel mid-OD** : tap CLEAR pendant OD = exit + rollback au snapshot. La passe est jetée, le buffer redevient pré-OD.
- **Undo/Redo après exit** : tap CLEAR court pendant PLAYING = toggle entre "loop avec dernière couche" et "loop sans dernière couche". Feature musicale live ("mute la couche HH pour un breakdown, la ramener au refrain").
- **Diff swap musical** : au moment du toggle, seules les notes de la couche affectée firent/coupent. La couche de base reste audible continue. Live press protégée.

Ce pivot est **architecturalement indépendant** de Master Sync (close-record), mais peut coexister avec lui (LOOP a maintenant deux mécanismes d'amélioration musicale : grille master pour le record, immediate-merge + Undo pour l'overdub).

---

## §2 Architecture buffers

### Pré-OD-Sync (post Master Sync)

```
_events[1024]              ← main buffer playback (loop content)
_overdubEvents[128]        ← buffer temporaire pendant OD, mergé au tap REC exit
_eventCount, _overdubCount
```

### Post-OD-Sync

```
_events[1024]              ← main buffer playback (loop content live + OD additions)
_eventsAlternate[1024]     ← snapshot pré-OD, ou "l'autre version" pour Undo/Redo toggle
_eventCount
_eventsAlternateCount
_alternateValid : bool     ← true si un toggle est possible
```

**Suppression** : `_overdubEvents[]`, `_overdubCount`, constante `MAX_LOOP_OVERDUB_EVENTS`. ~1 KB libéré par engine.

**Ajout** : `_eventsAlternate[1024]` = 8 KB par engine. ~32 KB pour 4 banks. Net SRAM +28 KB. Budget total restant 197 KB (sur 320 KB ESP32-S3).

---

## §3 Cycle de vie OD

### 3.1 Entrée OD (tap REC sur PLAYING ou STOPPED)

```
État avant : PLAYING (ou STOPPED via Q5 Phase 2 §28)
Geste     : tap REC sur le pad REC LOOP

Actions atomiques :
  1. memcpy(_eventsAlternate, _events, sizeof(_events))    // snapshot pré-OD
     _eventsAlternateCount = _eventCount
     _alternateValid = true
  2. État → OVERDUBBING (state machine inchangée vs Phase 2)
  3. (Si Q5 STOPPED → OD : startPlayback simultané comme actuellement)
  4. EVT_LOOP_OVERDUB trigger (LED : Green → Amber)
```

### 3.2 Capture pendant OD (immediate-merge)

```
État : OVERDUBPER, pad press musical (rising edge)
Geste : pad press → capturePadEvent(padIndex, velocity > 0, ...)

Actions :
  1. Live monitor MIDI (M8) :
     refCountNoteOn(transport, midiNote, velocity)
     → MIDI noteOn envoyé si refcount 0→1
  2. _padHeldLive[padIndex] = true (tracker B-N1/B-N2/R-N1 préservé)
  3. Insertion directe dans _events[] :
     insertEventSorted(_events, _eventCount, MAX_LOOP_EVENTS,
                        _playPositionUs, padIndex, midiNote, velocity)
  4. _playNextEventIdx adjustment :
     Binary search depuis _playPositionUs (logique réutilisée de l'ancien
     mergeOverdub ligne 866-876). Le nouvel event ne firera pas ce cycle
     (déjà émis live par M8), firera à partir du prochain wrap.

Falling edge (release pendant OD) :
  1. Live monitor refCountNoteOff
  2. _padHeldLive[padIndex] = false
  3. Insertion noteOff dans _events[] (velocity=0 convention)
  4. _playNextEventIdx adjustment idem
```

**Pas de buffer temporaire**. L'event est immédiatement dans la loop, audible au prochain cycle.

### 3.3 Exit commit (tap REC sur OVERDUBBING)

```
État avant : OVERDUBBING
Geste     : tap REC

Actions :
  1. Held pads injection : pour chaque _padHeldLive[pad]=true,
     inject noteOff event dans _events[] à _playPositionUs.
     (Réincarnation logique B-N2 : sans ça, noteOn capturé sans noteOff
      matching → refcount grows unbounded sur les cycles suivants si user
      release après exit OD. Voir §6.3.)
  2. NE PAS reset _padHeldLive (pad encore physiquement tenu).
  3. _eventsAlternate RESTE intacte (= pré-OD content, pour post-Undo).
  4. _alternateValid reste true.
  5. État → PLAYING (state-driven LED Amber → Green)
  6. PAS de trigger LED dédié pour OD commit (state change suffit, cf §8).
```

### 3.4 Exit cancel (tap CLEAR sur OVERDUBPER) — rising edge

```
État avant : OVERDUBBING (peut venir de PLAYING ou STOPPED-Q5)
Geste     : tap CLEAR (any duration, rising edge déclenche directement)

Actions :
  1. Diff swap musical (cf §6) :
     - Pour chaque note N (0..127) : compute before (dans _events)
       vs after (dans _eventsAlternate) à _playPositionUs.
     - Si change ET pas live-press : MIDI noteOff/noteOn ciblé.
  2. swap(_events, _eventsAlternate) via static global temp buffer (cf §6.2).
     swap(_eventCount, _eventsAlternateCount)
  3. _alternateValid reste true (Redo possible juste après).
  4. Recompute _noteRefCount depuis nouveau _events à _playPositionUs +
     live press contribution depuis _padHeldLive.
  5. Recompute _playNextEventIdx par binary search.
  6. **État → PLAYING** (toujours, décision OD-16 α post-review 2026-05-19) :
     - Si pré-OD state était PLAYING : retour cohérent.
     - Si pré-OD state était STOPPED (chemin Q5 OD-from-STOPPED) :
       **asymétrie acceptée** — Cancel ne restore PAS STOPPED, ramène PLAYING.
       Le startPlayback du Q5 reste effectif. Pour stopper, user retap PLAY/STOP.
       Justification : simplicité code (pas de tracker pre-OD state), cohérence
       mentale "Cancel = annuler les events OD, pas annuler tout le geste tap REC".
  7. EVT_LOOP_CLEAR trigger (state-driven LED Amber → Green ; PTN_NONE
     actuellement, pas de flash overlay tant que Phase 4 LOOP n'a pas
     finalisé la grammar — cf §8).
```

Conséquence musicale : la couche OD disparait proprement. Couche base + live press préservées. K+SN audibles continus, HH (couche OD) muté.

---

## §4 Cycle de vie Undo/Redo (hors OD)

### 4.1 Toggle Undo/Redo (tap CLEAR court sur PLAYING ou STOPPED)

```
État avant : PLAYING ou STOPPED, _alternateValid == true
Geste     : tap CLEAR + release avant clearLoopTimerMs (default 500ms)

Détection au falling edge :
  Si !_clearFired ET (release_time - press_time) < _clearLoopTimerMs :
    Trigger Undo/Redo

Actions identiques à §3.4 :
  1. Diff swap musical (cf §6)
  2. swap(_events, _eventsAlternate)
  3. _alternateValid reste true (toggle continu — tap suivant = Redo, etc.)
  4. Recompute refcount + _playNextEventIdx
  5. État inchangé (reste PLAYING ou STOPPED)
  6. LED trigger Option β :
     - Si on est passé "avec couche → sans couche" : EVT_STOP (coral FADE 100→0,
       sémantique "couche retirée")
     - Si on est passé "sans couche → avec couche" : EVT_PLAY (green FADE 0→100,
       sémantique "couche réintégrée")
     Détection sens : compter `_eventCount` avant et après. Plus d'events = avec couche.
```

### 4.2 Wipe (long-press CLEAR sur PLAYING ou STOPPED) — inchangé en gesture, étendu en effet

```
État avant : PLAYING ou STOPPED, _state ≠ OVERDUBBING/RECORDING (isLocked false)
Geste     : long-press CLEAR (hold >= clearLoopTimerMs)

Actions (comportement actuel ÉTENDU) :
  1. flushPendingNoteOffs (B2 self-stop → state STOPPED interne)
  2. _eventCount = 0
  3. for _events[i].active = false
  4. _overdubCount = 0  (legacy, sera retiré)
  5. _loopDurationUs = 0, _loopBars = 0
  6. État → EMPTY

NOUVEAU (OD-Sync) :
  7. _eventsAlternateCount = 0
  8. for _eventsAlternate[i].active = false
  9. _alternateValid = false   ← snapshot invalidée, plus d'Undo possible

  10. EVT_LOOP_CLEAR trigger (inchangé)
```

### 4.3 Tap CLEAR sur EMPTY ou autre

```
EMPTY : ignoré (rien à wipe ni à Undo)
RECORDING (pas PENDING_CLOSE) : refusé (isLocked)
PENDING_CLOSE (Master Sync BS-7) : refusé
WAITING_PLAY / WAITING_STOP (transport spec §17) : refusé
```

---

## §5 Gestes mapping complet par état

| État | Geste | Action |
|---|---|---|
| EMPTY | tap REC | startRecording (Master Sync) |
| EMPTY | tap CLEAR (any) | no-op |
| RECORDING | pad press musical | capturePadEvent (Master Sync anchor §3.1) |
| RECORDING (no pending) | tap REC | closeRecordingImmediate (FREE) ou armer PENDING_CLOSE (BEAT/BAR) — Master Sync §3.2 |
| RECORDING + PENDING_CLOSE | divers gestes | BS-6 à BS-9 Master Sync |
| **PLAYING** | tap REC | **Entrée OD (immediate-merge + snapshot)** §3.1 |
| **PLAYING** | tap CLEAR court | **Toggle Undo/Redo** §4.1 |
| **PLAYING** | long-press CLEAR | Wipe (étendu §4.2) |
| PLAYING | tap PLAY/STOP | transport quantize §17 spec parent |
| **STOPPED** | tap REC | Entrée OD Q5 (PLAYING + OD simultanés) |
| **STOPPED** | tap CLEAR court | Toggle Undo/Redo §4.1 (idem PLAYING) |
| STOPPED | long-press CLEAR | Wipe (étendu §4.2) |
| **OVERDUBBING** | pad press musical | **Immediate-merge insertion dans _events** §3.2 |
| **OVERDUBBING** | tap REC | **Exit commit** §3.3 |
| **OVERDUBBING** | tap PLAY/STOP | **No-op** (OD-3 décision) |
| **OVERDUBBING** | tap CLEAR (any duration, rising edge) | **Exit cancel** §3.4 |
| **OVERDUBBING** | bank switch (LEFT + bank pad) | **Silent deny** (OD-8 = invariant 11) |
| **OVERDUBBING** | long-press CLEAR | Sans effet supplémentaire : rising edge a déjà déclenché Cancel |
| WAITING_PLAY / WAITING_STOP | divers | spec §17 parent |

---

## §6 Mécanique diff swap musical

### 6.1 Problème à résoudre

Au moment du swap (Cancel ou Undo/Redo toggle), le buffer `_events[]` change de contenu. Certaines notes audibles "maintenant" peuvent disparaître, d'autres apparaître. Si on fait un MIDI sweep brutal (`allNotesOff` ou `flushPendingNoteOffs`), on coupe **toutes** les notes, y compris la couche de base — glitch musical inacceptable.

**Solution : diff par note, MIDI ciblé seulement pour les notes qui doivent changer.**

### 6.2 Algorithme

**Note implémentation post-review 2026-05-19** : le swap des buffers utilise
un **static global temp buffer** `g_swapTemp[MAX_LOOP_EVENTS]` (8 KB SRAM
permanent) au lieu d'allocation stack. Raison : default Arduino-ESP32
main loop stack = 8 KB ; allouer un `LoopEvent temp[1024]` (8 KB) sur stack
provoque overflow. Coût SRAM total OD-Sync révisé : +8 KB alternate buffer
+ +8 KB swap temp = **+16 KB par engine × 4 banks = +64 KB net** (vs +32 KB
initialement estimé). Budget large (~225 KB libres avant OD-Sync).

```cpp
// Static global, partagé entre tous les LoopEngine (un seul swap actif à la fois
// par invariant 11 ≤ 1 LOOP en REC/OD).
static LoopEvent g_swapTemp[MAX_LOOP_EVENTS];

void LoopEngine::swapForUndoRedo(MidiTransport& transport) {
  // Étape 1 : compute states "before" (dans _events) et "after" (dans _eventsAlternate).
  bool before[128], after[128];
  for (uint8_t n = 0; n < 128; n++) {
    before[n] = isNoteOnAt(_events, _eventCount, n, _playPositionUs);
    after[n]  = isNoteOnAt(_eventsAlternate, _eventsAlternateCount, n, _playPositionUs);
  }

  // Étape 2 : MIDI ciblé pour chaque note où l'état change.
  for (uint8_t n = 0; n < 128; n++) {
    if (before[n] == after[n]) continue;  // pas de change → couche base préservée

    // Check live press protection :
    bool liveOn = false;
    for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
      if (resolvePadToMidiNote(pad) == n && _padHeldLive[pad]) { liveOn = true; break; }
    }

    if (before[n] && !after[n]) {
      // Note disparait. NoteOff seulement si pas live-press.
      if (!liveOn) transport.sendNoteOn(_channel, n, 0);  // vel 0 = noteOff
    } else {  // !before && after
      // Note apparait. NoteOn seulement si pas déjà live-press.
      if (!liveOn) {
        uint8_t vel = findLatestVelAt(_eventsAlternate, _eventsAlternateCount, n, _playPositionUs);
        transport.sendNoteOn(_channel, n, vel);
      }
    }
  }

  // Étape 3 : swap (pointer swap idéal, sinon memcpy via temp).
  swap(_events, _eventsAlternate);
  swap(_eventCount, _eventsAlternateCount);

  // Étape 4 : recompute _noteRefCount from new _events at _playPositionUs + live press.
  memset(_noteRefCount, 0, sizeof(_noteRefCount));
  for (uint8_t n = 0; n < 128; n++) {
    if (after[n]) _noteRefCount[n] = 1;  // buffer attendu on
  }
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    if (_padHeldLive[pad]) {
      uint8_t note = resolvePadToMidiNote(pad);
      _noteRefCount[note]++;  // live press contribution
    }
  }

  // Étape 5 : recompute _playNextEventIdx par binary search.
  _playNextEventIdx = binary_search_upper(_events, _eventCount, _playPositionUs);
}
```

### 6.3 Helpers

```cpp
// isNoteOnAt — état "audible" d'une note à position pos dans le buffer.
// Le buffer est trié par timestamp ; on walk jusqu'à pos en suivant les transitions.
bool LoopEngine::isNoteOnAt(LoopEvent* buf, uint16_t count, uint8_t note, uint32_t pos) {
  bool on = false;
  for (uint16_t i = 0; i < count; i++) {
    if (!buf[i].active) break;
    if (buf[i].timestampUs > pos) break;
    if (buf[i].midiNote != note) continue;
    on = (buf[i].velocity > 0);  // dernier event matching note avant pos
  }
  return on;
}

// findLatestVelAt — velocity du dernier noteOn matching note avant pos.
uint8_t LoopEngine::findLatestVelAt(LoopEvent* buf, uint16_t count, uint8_t note, uint32_t pos) {
  uint8_t vel = DEFAULT_BASE_VELOCITY;  // fallback safe
  for (uint16_t i = 0; i < count; i++) {
    if (!buf[i].active) break;
    if (buf[i].timestampUs > pos) break;
    if (buf[i].midiNote != note) continue;
    if (buf[i].velocity > 0) vel = buf[i].velocity;
  }
  return vel;
}
```

Coût computation : `O(128 × count)` au swap. Pour 1024 events max, ~262K ops, soit ~0.5-1 ms à 240 MHz. **Acceptable pour un geste rare** (Cancel ou Undo/Redo). Hors hot path.

### 6.4 Scénario K+SN+HH (validation conceptuelle)

État après record loop K+SN + OD HH commit :
- `_events[]` = K + SN + HH (couche commitée)
- `_eventsAlternate[]` = K + SN (snapshot pré-OD HH)

User tap CLEAR court (Undo) à position 0.25 du cycle, K vient de firer :

| Note | before (events) | after (alternate) | liveOn | Action MIDI |
|---|---|---|---|---|
| Kick (note 36) | on (vient de firer à 0.0) | on (idem dans snapshot) | non | **rien** — K continue d'audible ✓ |
| Snare (note 38) | off (entre deux SN) | off (idem) | non | rien ✓ |
| HH (note 42) | on (HH joue à 0.0625, 0.125, ...) | off (pas dans snapshot) | non | **noteOff** — HH muté proprement ✓ |

Resultat : K+SN audibles continus, HH disparaît proprement. **Pas de glitch sur la couche de base.**

Au prochain tap CLEAR court (Redo) : logique inverse. HH revient on → noteOn impulse à velocity captée.

### 6.5 Edge case : note tenue live au moment du swap

User tient pad #4 physiquement (M8 a émis noteOn, refcount > 0). Tap CLEAR pendant OD.

- liveOn = true pour le pad #4 (`_padHeldLive[4]=true`).
- Diff swap : si before=on after=off (couche enlève la note), `if (!liveOn)` → pas de MIDI noteOff envoyé. **La note continue d'audible via la live press.**
- Étape 4 recompute : `_noteRefCount[note]` incrémenté pour la live press contribution. Cohérent.
- Quand user release pad #4 plus tard : `refCountNoteOff` → refcount décrémenté → MIDI noteOff si refcount → 0.

**Live press protégée par construction.** ✓

---

## §7 Cohérence avec invariants existants

| Invariant | Statut OD-Sync | Note |
|---|---|---|
| §23.1 — Aucune note bloquée | ✓ | Diff swap préserve live press + B-N2 logic réincarnée à OD exit |
| §23.2 — Bank switch refusé REC/OD | ✓ | `isLocked()` retourne true pour OVERDUBBING (inchangé) |
| §23.3 — Slot save/load/delete refusés REC/OD | ✓ | idem |
| §23.5 — `_recordBpm` immutable | ✓ | Master Sync rule respectée, OD ne touche pas |
| §23.9 — No new/delete runtime | ✓ | `_eventsAlternate` static array |
| §23.11 — ≤ 1 LOOP en REC/OD | ✓ | inchangé |
| B-N1 (onBackgroundTransition flush live) | ✓ | `_padHeldLive` tracker inchangé, mechanism préservé |
| **B-N2 (held pads à OD merge)** | ⚠ **DÉPLACÉE** | De `mergeOverdub` (supprimée) vers OD exit commit §3.3. Logique identique : flush `_padHeldLive` → noteOff inject dans `_events` à `_playPositionUs`. HW gate dédié à valider. |
| R-N1 (CLEAR tracker reset BG) | ✓ | `notifyClearPressEnd` inchangé. CLEAR pendant OD (rising edge) ne dépend pas du tracker. |
| B2 (flushPendingNoteOffs self-stop) | ✓ | inchangé |
| B3 (commitWaitingAction nowUs) | ✓ | inchangé (Master Sync) |
| M3 (velocity strict capture) | ✓ | inchangé |
| M6 (timestamp clamp) | ✓ | applique aussi à `_eventsAlternate` au snapshot (memcpy du `_events` déjà clampé) |
| M8 (live monitor all states) | ✓ | inchangé |
| m4 (flash flags one-shot) | ✓ | inchangé |
| **Master Sync anchor + Auto-Stop** | ✓ | OD-Sync orthogonal — anchor n'est pas affecté par OD entry/exit |

---

## §8 LED feedback (Option β — réutilisation events existants)

**Principe** : pas de nouveaux EVT_LOOP_* dans cette spec. Réutiliser les events existants avec leur grammar actuelle. Phase 4 LOOP raffinera avec patterns concrets dédiés (cf §11).

### Mapping OD-Sync → events existants

| Action OD-Sync | EventId existant réutilisé | Pattern | Sémantique cohérente |
|---|---|---|---|
| Entrée OD (tap REC PLAYING/STOPPED) | `EVT_LOOP_OVERDUB` | PTN_NONE → state-driven Amber | inchangé (déjà câblé Phase 2) |
| OD commit / exit (tap REC OVERDUB) | _(aucun trigger explicite)_ | state-driven Amber → Green | inchangé (déjà adequate) |
| **OD Cancel (tap CLEAR pendant OD)** | **`EVT_LOOP_CLEAR`** | PTN_NONE → state-driven, color slot `CSLOT_VERB_CLEAR_LOOP` (cyan) | Sémantique "rollback" — cyan est déjà la couleur "clear/abandon" |
| **Undo (CLEAR court PLAYING/STOPPED, couche retirée)** | **`EVT_STOP`** | PTN_FADE, color slot `CSLOT_VERB_STOP` (coral, FADE 100→0) | Sémantique "off / couche absente" — fade descendant musical |
| **Redo (CLEAR court PLAYING/STOPPED, couche réintégrée)** | **`EVT_PLAY`** | PTN_FADE, color slot `CSLOT_VERB_PLAY` (green, FADE 0→100) | Sémantique "on / couche présente" — fade ascendant musical |
| Wipe (long-press CLEAR) | `EVT_LOOP_CLEAR` | inchangé | déjà câblé |

### Détection Undo vs Redo (= "couche retirée" vs "couche réintégrée")

Au moment du swap (§4.1), compter `_eventCount` avant et après :

```
if (_eventCount > _eventsAlternateCount)  → on va passer "avec couche" vers "sans couche" → EVT_STOP
else                                       → on va passer "sans couche" vers "avec couche" → EVT_PLAY
```

C'est cohérent musicalement : la couche OD ajoute des events, donc le buffer "avec couche" a plus d'events que le snapshot "sans couche".

### Notes implémentation LED

- **Aucune modification de `LedGrammar.cpp`** ni de `LedController.cpp`.
- **Aucun nouveau color slot** dans `ColorSlotStore`.
- **Aucun bump NVS**.
- Les callsites à ajouter sont uniquement dans `main.cpp::processLoopMode` CLEAR handling et `LoopEngine` Cancel/Undo paths (passer le `EventId` via signal ou wrapper).

### Différé Phase 4

Phase 4 LOOP (`docs/superpowers/specs/2026-04-19-loop-mode-design.md` §27 P4) a déjà comme scope la finalisation LED grammar avec patterns concrets pour tous `EVT_LOOP_*`. Si désirée, l'introduction d'events dédiés (`EVT_LOOP_OD_CANCEL`, `EVT_LOOP_UNDO`, `EVT_LOOP_REDO`) avec patterns FLASH/SPARK ciblés sera traitée dans Phase 4. L'intervention sera minimale : remplacer les callsites Option β par les nouveaux event IDs.

---

## §9 Décisions actées (traçabilité brainstorm 2026-05-19)

| # | Sujet | Décision actée |
|---|---|---|
| OD-1 | Modèle | **Immediate-merge + snapshot 1-level toggle** (`_eventsAlternate`) |
| OD-2 | Durée OD | **Sustained** (industry standard Boss/Mobius) — user contrôle exit |
| OD-3 | tap PLAY/STOP pendant OD | **No-op** (cohérent BS-6 PENDING_CLOSE) |
| OD-4 | tap REC pendant OD | **Exit commit**, `_eventsAlternate` préservé pour post-Undo |
| OD-5 | tap CLEAR pendant OD | **Exit + rollback** (swap to alternate). Cancel mid-OD. |
| OD-6 | tap CLEAR court pendant PLAYING/STOPPED | **Toggle Undo/Redo** (swap diff par note) |
| OD-7 | long-press CLEAR pendant PLAYING/STOPPED | **Wipe** étendu (reset `_eventsAlternate` + `_alternateValid=false`) |
| OD-8 | Bank switch pendant OD | **Silent deny** (invariant §23.11 préservé) |
| OD-9 | Swap glitch handling | **Diff swap par note** — préserve couche base + live press. ~0.5-1 ms par swap. |
| OD-10 | LED pendant OD | **Amber inchangé** (state-driven `renderBankLoop`) |
| OD-11 | Gesture tap court vs long-press CLEAR | **Threshold = `_clearLoopTimerMs`** existant (default 500 ms, Tool 6) |
| OD-12 | LED trigger Cancel pendant OD | **Réutiliser `EVT_LOOP_CLEAR`** (cyan, sémantique rollback) — Option β |
| OD-13 | LED trigger Undo (couche retirée) | **Réutiliser `EVT_STOP`** (coral FADE) — Option β |
| OD-14 | LED trigger Redo (couche réintégrée) | **Réutiliser `EVT_PLAY`** (green FADE) — Option β |
| OD-15 | Events LED dédiés (EVT_LOOP_OD_*) | **Différé Phase 4** (LED wiring complet) |
| **OD-16** | **Cancel from STOPPED-Q5 (OD-from-STOPPED) ramène à STOPPED ou PLAYING ?** | **α — toujours PLAYING.** Asymétrie acceptée. Pas de tracker pre-OD state. Justification : simplicité + cohérence "Cancel = annuler events OD, pas annuler le geste tap REC". Locké post-review 2026-05-19. |

---

## §10 Delta vs code actuel

### À ajouter

| Fichier | Item | LOC est. |
|---|---|---|
| `src/loop/LoopEngine.h` | Champs `_eventsAlternate[MAX_LOOP_EVENTS]`, `_eventsAlternateCount`, `_alternateValid` | ~5 |
| `src/loop/LoopEngine.h` | Méthodes : `swapForUndoRedo`, `cancelOverdub`, `commitOverdubExit`, helpers `isNoteOnAt` / `findLatestVelAt` | ~8 declarations |
| `src/loop/LoopEngine.cpp` (constructor) | Init des nouveaux champs (false / 0) | ~5 |
| `src/loop/LoopEngine.cpp` `tapRec` PLAYING branche | Snapshot `_eventsAlternate` au lieu de `_overdubCount = 0` | ~5 |
| `src/loop/LoopEngine.cpp` `tapRec` STOPPED branche (Q5) | Snapshot idem | ~5 |
| `src/loop/LoopEngine.cpp` `tapRec` OVERDUBBING branche | Replace `mergeOverdub` par `commitOverdubExit` (held pads inject + state PLAYING). Snapshot préservé. | ~15 |
| `src/loop/LoopEngine.cpp` `capturePadEvent` OVERDUBBING branche | Replace `_overdubEvents` insert par `_events` direct insert + `_playNextEventIdx` recompute | ~15 |
| `src/loop/LoopEngine.cpp` `tapPlayStop` OVERDUBBING branche | Replace `abandonOverdub` par `no-op` (OD-3 décision) | ~3 |
| `src/loop/LoopEngine.cpp` | Nouvelle méthode `cancelOverdub` (CLEAR pendant OD) : diff swap + exit | ~30 |
| `src/loop/LoopEngine.cpp` | Nouvelle méthode `swapForUndoRedo` (CLEAR court PLAYING/STOPPED) | ~30 |
| `src/loop/LoopEngine.cpp` | Nouvelle méthode `commitOverdubExit` (tap REC OD exit) — held pads inject + state | ~15 |
| `src/loop/LoopEngine.cpp` | Helpers `isNoteOnAt` + `findLatestVelAt` | ~30 |
| `src/loop/LoopEngine.cpp` `longPressClear` | Étendre wipe : `_eventsAlternateCount = 0` + `_alternateValid = false` | ~5 |
| `src/main.cpp::processLoopMode` CLEAR handling | Dispatch state-aware : OVERDUBBING rising → cancelOverdub ; PLAYING/STOPPED short-release → swapForUndoRedo ; long-press → wipe existing | ~30 |
| `src/main.cpp::processLoopMode` CLEAR trigger LED | EVT_LOOP_CLEAR pour Cancel, EVT_STOP/EVT_PLAY pour Undo/Redo selon `_eventCount` diff | ~10 |

**Total ajouts : ~211 LOC** (estimation conservatrice avec marge ~+15%).

### À supprimer

| Fichier | Item | LOC supprimés |
|---|---|---|
| `src/loop/LoopEngine.h` | `_overdubEvents[MAX_LOOP_OVERDUB_EVENTS]`, `_overdubCount`, declarations `mergeOverdub` / `abandonOverdub` | ~5 |
| `src/loop/LoopEngine.h` | Constante `MAX_LOOP_OVERDUB_EVENTS` (peut rester si non-touch) | 0-1 |
| `src/loop/LoopEngine.cpp` | Implémentation `mergeOverdub` (lignes 805-879) | ~75 |
| `src/loop/LoopEngine.cpp` | Implémentation `abandonOverdub` (lignes 881-887) | ~10 |
| `src/loop/LoopEngine.cpp` | Init `_overdubCount`, refs `_overdubEvents` éparses | ~15 |
| `src/loop/LoopEngine.cpp` | Callsites `mergeOverdub` (tapRec line 159), `abandonOverdub` (tapPlayStop line 214) | ~3 |

**Total suppressions : ~110 LOC.**

### Solde net

**+211 - 110 = +101 LOC nets.** Comparable à Master Sync (+100 LOC nets). Pas une refonte.

### À garder strictement inchangé

- État machine 7 états (OVERDUBBING reste l'état "OD active").
- Master Sync mécanisme (recordStart anchor, PENDING_CLOSE, commitRecordingClose).
- WAITING_PLAY / WAITING_STOP transport (§17 spec parent).
- BPM-scaled playback (`_scaledElapsedUs += delta × liveBpm/recordBpm`).
- Multi-bank infrastructure.
- `onBackgroundTransition` flush live press.
- `midiPanic` flush.
- LED rendering state-driven (`renderBankLoop`).
- M3 velocity strict capture, M6 timestamp clamp, M8 live monitor tous états.
- `_padHeldLive[]` tracker.
- NVS layout (aucun bump version).
- Tools setup (Tool 3, 4, 5, 6, 7, 8).
- LedGrammar.cpp (Option β = pas de modif).

---

## §11 Impact sur spec parent et autres docs

### Sections à amender dans `2026-04-19-loop-mode-design.md`

#### §8 — Overdub
Réécriture du paragraphe central pour refléter immediate-merge + snapshot + Undo/Redo. Conservation du concept "couche overdub" mais avec mécanique nouvelle. Cross-ref vers cette spec.

#### §9 — Play / Stop / Clear
Ajout d'une sous-section §9.1 "CLEAR contextuel" qui documente les 3 sens du geste CLEAR (Cancel pendant OD / Undo/Redo court PLAYING/STOPPED / Wipe long-press). Cross-ref vers cette spec §3.4 + §4.

#### §21 — LED system
Note bandeau : "Pendant la transition OD-Sync vers Phase 4, les events Cancel/Undo/Redo réutilisent EVT_LOOP_CLEAR / EVT_STOP / EVT_PLAY (Option β). Phase 4 pourra introduire `EVT_LOOP_OD_CANCEL` / `EVT_LOOP_UNDO` / `EVT_LOOP_REDO` dédiés."

#### §27 — Phases
Ajouter ligne **OD-Sync** entre Phase 2 LOOP CLOSE et Phase 3. Cross-ref vers cette spec + plan.

#### §28 — Décisions tranchées
Ajouter ligne traçabilité OD-Sync (renvoi vers §9 de cette spec pour les 15 décisions OD-1 à OD-15).

### Sections nouvelles à mentionner ailleurs

- `STATUS.md` focus courant : OD-Sync CLOSE post-implémentation.
- `LOOP_PROGRESS.md` : nouvelle ligne tableau OD-Sync (entre Master Sync et Phase 3 LOOP).
- `docs/reference/runtime-flows.md` : §4 (ou nouvelle §) "Overdub flow — immediate-merge + Undo/Redo toggle".
- Phase 4 plan (futur) : note "OD-Sync events à promouvoir vers EVT_LOOP_OD_* dédiés".

---

## §12 HW gates (test scenarios)

### G1 — Live loop growth pendant OD
1. Bank LOOP BEAT. Record K+SN, 4 beats.
2. Tap REC → OD. LED bascule Amber. Cycle joue K+SN (sans HH).
3. Press pad HH à position 0.3 du cycle 1.
4. **Vérifier** : MIDI live noteOn HH immédiat. Cycle 1 continue.
5. Cycle 2 commence (wrap). À position 0.3, **HH joue automatiquement** depuis le buffer. K+SN aussi. ✓

### G2 — OD Cancel pendant OD (tap CLEAR)
1. État OVERDUBBING avec HH ajouté.
2. Tap CLEAR pendant OD.
3. **Vérifier** : LED bascule Amber → Green (state PLAYING) state-driven.
   EVT_LOOP_CLEAR est trigger mais grammar PTN_NONE par défaut = pas de flash
   overlay visible (différé Phase 4 LED finalisation).
4. Cycles suivants : K+SN audibles continus, **HH disparaît proprement** à la prochaine position où il jouait.
5. Vérifier au DAW : pas de glitch sur K ni SN au moment du swap.

### G3 — Undo/Redo toggle pendant PLAYING (le geste live K+SN+HH)
1. État PLAYING avec K+SN+HH (post-OD commit).
2. Tap CLEAR court (release < 500ms). LED fade coral brief (EVT_STOP).
3. **Vérifier** : HH disparaît, K+SN continuent. Cycle après cycle, plus de HH.
4. Tap CLEAR court à nouveau. LED fade green brief (EVT_PLAY).
5. **Vérifier** : HH revient au cycle suivant à sa position. K+SN continuent.
6. Test rapide : alterner Undo/Redo plusieurs fois pendant un breakdown live.

### G4 — Live press protection au swap
1. PLAYING avec K+SN+HH.
2. User tient pad K physiquement (live press).
3. Tap CLEAR court (Undo).
4. **Vérifier** : K live continue d'audible (pas de coupure). HH disparaît. K du buffer disparaît aussi mais live press maintient.
5. User release K. MIDI noteOff envoyé proprement.

### G5 — Held pad through OD exit (B-N2 déplacée)
1. PLAYING. Tap REC → OD.
2. Press pad #4 à position 0.3, **HOLD** continu.
3. Tap REC à position 0.6 → exit commit. État → PLAYING.
4. **Vérifier au DAW** : pad #4 toujours audible (live press).
5. Cycle suivant : à position 0.3, buffer fire noteOn pad #4. À position 0.6 (injected noteOff de B-N2 réincarnée), buffer fire noteOff. **Pas de stuck note.**
6. User release pad #4 plus tard. MIDI noteOff via refcount.
7. Cycle d'après : buffer fire noteOn à 0.3, noteOff à 0.6 (pas de double-trigger).

### G6 — `_playNextEventIdx` post-insert immediate-merge
1. PLAYING avec quelques events (kick à 0.2, snare à 0.4, etc.).
2. Tap REC → OD.
3. À position 0.0 exactement (juste après wrap), press pad nouveau.
4. **Vérifier** : pas de double-fire. M8 live monitor noteOn une fois.
5. Cycle 2 : à position 0.0, le nouvel event fire normalement. Aucun fire en trop ce cycle 1.

### G7 — Wipe étendu (long-press CLEAR avec snapshot valide)
1. PLAYING avec K+SN+HH (snapshot K+SN valide).
2. Long-press CLEAR (>= 500ms). EVT_LOOP_CLEAR trigger.
3. État → EMPTY. `_alternateValid = false`.
4. Tap CLEAR court juste après. **Vérifier** : no-op (rien à Undo, snapshot invalide).

### G8 — Bank switch refusé pendant OD
1. État OVERDUBBING. Tap LEFT + autre bank pad.
2. **Vérifier** : silent deny, on reste sur bank actuelle, OD continue.

### G9 — Multiple OD successifs avec snapshot écrasement
1. Loop K+SN. OD HH → exit (commit). _eventsAlternate = K+SN.
2. OD perc à nouveau → exit (commit). _eventsAlternate écrasé = K+SN+HH (= état post-1er OD).
3. Tap CLEAR court (Undo) : revient à K+SN+HH (sans perc dernière).
4. Tap CLEAR court (Redo) : revient à K+SN+HH+perc.
5. **Vérifier** : le 1er OD HH est dans le buffer comme couche "permanente" — pas d'Undo possible 2 niveaux en arrière. Cohérent avec 1-level toggle.

### G10 — Tap PLAY/STOP pendant OD = no-op (OD-3)
1. OVERDUBBING. Tap PLAY/STOP.
2. **Vérifier** : OD continue, état inchangé, aucun MIDI parasite, aucune transition LED.

### G11 — Cancel pendant OD puis re-OD immédiat (added post-review 2026-05-19)
1. Loop K+SN. Tap REC → OD HH ajouté.
2. Tap CLEAR (Cancel). State PLAYING, _events = K+SN, _eventsAlternate = K+SN+HH.
3. Tap REC immédiatement à nouveau (re-entrée OD).
4. **Vérifier** : `memcpy(_eventsAlternate, _events, ...)` écrase _eventsAlternate
   = K+SN (snapshot frais). Le K+SN+HH précédent est perdu (cohérent 1-level).
5. Press HH à position 0.4.
6. Tap REC → exit commit. State PLAYING avec K+SN+HH (nouveau HH à 0.4).
7. Tap CLEAR court (Undo) → revient à K+SN seul. ✓

### G12 — OD-from-STOPPED Q5 Cancel ramène à PLAYING (OD-16, asymétrie locké α)
1. Loop K+SN, état PLAYING.
2. Tap PLAY/STOP → STOPPED.
3. Tap REC depuis STOPPED → Q5 PLAYING + OD simultanés.
4. Press HH à position 0.3.
5. Tap CLEAR (Cancel).
6. **Vérifier** : état → **PLAYING** (pas STOPPED). Loop K+SN tourne, HH disparu.
7. Pour stopper, user retap PLAY/STOP. Asymétrie documentée OD-16.

### G13 — tapRec WAITING_STOP → OD avec snapshot (added post-review, B2 fix)
1. Loop K+SN, état PLAYING.
2. Quantize Beat ou Bar. Tap PLAY/STOP → WAITING_STOP (attend boundary).
3. Avant le boundary, tap REC.
4. **Vérifier** : transition WAITING_STOP → OVERDUBBING. _eventsAlternate snapshotté
   = K+SN (état pré-OD). _alternateValid = true.
5. Press HH à position courante.
6. Tap REC exit. State PLAYING. K+SN+HH joue.
7. Tap CLEAR court (Undo) → K+SN seul. ✓ Snapshot était correct.

### G14 — Wipe puis tap CLEAR court (no-op safe avec _alternateValid=false)
1. Loop K+SN+HH (snapshot valide K+SN).
2. Long-press CLEAR (≥500 ms) → wipe + state EMPTY + `_alternateValid = false`.
3. Tap CLEAR court juste après.
4. **Vérifier** : `swapForUndoRedo` early-return sur `!_alternateValid`. Aucun MIDI,
   aucune transition LED. Safe no-op.

### G15 — Buffer full au B-N2 inject à OD exit (telemetry edge case)
1. Loop dense (proche de 1024 events après plusieurs OD).
2. Tap REC → OD. Hold pad #4.
3. Inject events pendant OD pour saturer le buffer.
4. Tap REC exit. `commitOverdubExit` tente d'injecter noteOff pour pad #4 tenu.
5. **Vérifier** trace serial : si `insertEventSorted` retourne false, émission
   `viewer::emitLoopBufferFull(ch, "od_exit_flush")`. Pas de crash. Live press
   refcount silencera la note au release naturel.

---

## §13 Travail estimé

| Phase | Effort |
|---|---|
| Code (~100 LOC nets) répartis en 3-4 commits ciblés | 2 jours |
| HW gates G1-G10 + traces serial validation | 1.5 jours |
| Doc-sync (parent spec + STATUS + LOOP_PROGRESS + runtime-flows + cette spec si patches mineurs) | 0.5 jour |
| Marge debug/regressions imprévues (B-N2 déplacement + diff swap nouveauté) | 1 jour |

**Total : ≈ 5 jours.**

### Risques identifiés (à monitorer pendant impl)

1. **`_playNextEventIdx` adjustment** après insert immediate-merge — test G6 critique. Si mal géré : double-fire ou skip.
2. **B-N2 déplacée** vers OD exit — test G5 critique. Si mal géré : stuck notes au DAW.
3. **Diff swap musical** — tests G3 + G4 critiques. Si mal géré : couche base coupe au swap (glitch global).
4. **Refcount recompute post-swap** — cohérence live press + buffer state. Vérifier en serial trace.

### Décisions reportées explicitement hors scope

- **Events LED dédiés EVT_LOOP_OD_CANCEL / UNDO / REDO** : Phase 4 LOOP. Réutilisation Option β suffit pour validation HW OD-Sync.
- **Multi-level Undo (N layers)** : impossible avec 1-level snapshot. Si désiré future, nécessite N × 8 KB snapshot stack. Out of scope OD-Sync.
- **Substitute mode** (replace event at same timestamp lors d'un re-press) : non implémenté. Si cumul d'events dans la couche OD est gênant musicalement, à adresser future paquet.
- **OD auto-disable au wrap** (option β du brainstorm précédent, rejetée par user pour sustained α) : reste hors scope. Si réintroduit future, compatible avec snapshot.
- **Guard tap CLEAR accidentel pendant OD** (timer min hold) : non implémenté. Si feedback live montre des cancels accidentels fréquents, à ajouter post-OD-Sync.

---

## §14 Pipeline d'implémentation

1. **Ce doc validé** par Loïc. ✓ post brainstorm + audit feasibility 2026-05-19.
2. **Patch parent spec** `2026-04-19-loop-mode-design.md` §8, §9, §21, §27, §28. Cross-refs vers cette spec.
3. **Mini-plan dédié** (archivé post-exécution dans [`docs/archive/2026-05-19-od-sync-implementation-plan.md`](../../archive/2026-05-19-od-sync-implementation-plan.md)) style Master Sync — 4 commits + HW gates G1-G15.
4. **Code** : 3-4 commits ciblés.
   - **C1** : Removal `_overdubEvents` / `mergeOverdub` / `abandonOverdub` + ajout `_eventsAlternate` declarations + constructor init. Inerte (pas encore wired). ~50 LOC.
   - **C2** : Activation immediate-merge dans `capturePadEvent` OVERDUBBING + snapshot dans `tapRec` PLAYING/STOPPED entry + `commitOverdubExit` à `tapRec` OVERDUBBING. Held pads B-N2 déplacée. ~60 LOC. HW gates G1, G5, G6.
   - **C3** : Helpers diff swap + `swapForUndoRedo` + `cancelOverdub` + dispatch CLEAR dans `processLoopMode` + LED triggers Option β. ~80 LOC. HW gates G2, G3, G4, G7, G8, G9, G10.
   - **C4** : Doc-sync (parent spec + STATUS + LOOP_PROGRESS + runtime-flows).
5. **HW gates** G1-G10 — validation HW Loïc requise avant chaque commit gate.
6. **Doc-sync final** : STATUS / LOOP_PROGRESS marqués "OD-Sync implémenté commit X".

---

**Fin du doc.** Self-suffisant pour comprendre le pivot OD-Sync ILLPAD V2.
