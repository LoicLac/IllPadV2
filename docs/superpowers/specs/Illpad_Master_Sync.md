# Illpad — Master Sync (clock-aligned recording and playback)

**Date** : 2026-05-19
**Statut** : VALIDÉ post-brainstorm 2026-05-19. À implémenter dans Paquet A unifié.
**Self-sufficient** : ce doc peut être lu seul. Les cross-refs vers la spec parent LOOP sont fournies en pointeur uniquement, pas en pré-requis de lecture.

**Cross-refs** :
- Design doc déclencheur : [`docs/superpowers/designs/2026-05-19-loop-algo-pivot-design.md`](../designs/2026-05-19-loop-algo-pivot-design.md)
- Spec parent LOOP (à amender post-implémentation) : [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](2026-04-19-loop-mode-design.md) §7, §17, §24
- Code actuel : [`src/loop/LoopEngine.{h,cpp}`](../../../src/loop/LoopEngine.h), [`src/midi/ClockManager.{h,cpp}`](../../../src/midi/ClockManager.h), [`src/main.cpp`](../../../src/main.cpp)

---

## §0 Contexte

Le looper Phase 2 close 2026-05-19 a révélé deux problèmes musicaux interdépendants :

1. **Bar-snap au stopRecording rescale les events stockés** → groove humain détruit. Identifié comme anomalie vs l'industrie (Boss/Mobius/Sooperlooper/EHX/Loopy Pro). Aucun looper de référence ne stretche les timestamps stockés au close-record.
2. **Les LOOPs ne sont pas alignés à la grille master clock** → ARP et LOOP, ou deux LOOPs, démarrent à des phases arbitraires. Pas de coordination musicale entre les éléments quantizés de l'instrument.

Cette spec couvre la solution unifiée : **toute bank ILLPAD en mode quantize (ARPEG Beat, ARPEG_GEN Beat, LOOP Beat, LOOP Bar) partage la grille temporelle du master clock**. Les banks en mode "free" (ARPEG Imm, LOOP Free, NORMAL) sont délibérément hors grille.

---

## §1 Principe directeur

> **Le master clock fournit LA grille temporelle commune à tout ce qui est quantizé sur l'ILLPAD.**

Conséquences pratiques observables :

- ARP 1/4 sur bank A + LOOP Beat sur bank B → la note arp et le wrap du loop tombent sur le **même master tick**.
- LOOP1 Beat + LOOP2 Bar → leurs wraps respectifs sont alignés sur la grille master commune (loop1 sur tous les beats, loop2 sur les beats divisibles par 4 — sous-ensemble).
- ARP Imm + LOOP Free → pas d'alignement, par choix utilisateur.
- LOOP Bar + LOOP Free → loop bar aligné, loop free dérive. La différence est musicalement audible et c'est intentionnel.

La règle s'énonce en une ligne. Tout le reste découle.

---

## §2 ClockManager — grille de référence

### 2.1 État pré-existant

`ClockManager` ([src/midi/ClockManager.{h,cpp}](../../../src/midi/ClockManager.h)) maintient déjà :
- `_currentTick` (uint32_t) : compteur incrémental ticks master à 24 PPQN.
- `_lastTickTimeUs` (uint32_t) : wall time `micros()` du dernier tick fired.
- `_pllTickInterval` (float) : intervalle µs entre 2 ticks au BPM courant.

`_currentTick` > 0 dès la 1ère frame post-boot (tempo interne default 120 BPM si pas de source externe). La grille master existe **indépendamment** de toute bank ARP ou LOOP active.

### 2.2 Modes master vs slave (rappel)

| Mode | Source des ticks | Sortie externe |
|---|---|---|
| **Master** | Pot tempo interne (PLL no-op, intervalle figé) | Envoie F8 sur USB + BLE |
| **Slave** | USB > BLE > last known > interne (cascade fallback automatique) | Silencieux côté sortie clock |

Dans les deux modes : `_currentTick` tourne, `_lastTickTimeUs` à jour, grille valide. Pas de pré-requis utilisateur. Pas de "warmup" ni d'arp/loop à lancer pour amorcer la grille.

### 2.3 Primitives à exposer (nouveaux getters publics)

| Getter | Définition | Dérivation |
|---|---|---|
| `getLastTickWallTimeUs()` | Wall time du dernier master tick fired | Retourne `_lastTickTimeUs` direct |
| `getLastBeatWallTimeUs()` | Wall time du dernier beat boundary (multiple de 24 ticks) | `_lastTickTimeUs - (currentTick % 24) × _pllTickInterval` |
| `getLastBarWallTimeUs()` | Wall time du dernier bar boundary (multiple de 96 ticks) | `_lastTickTimeUs - (currentTick % 96) × _pllTickInterval` |

Estimation : **~30 LOC dans ClockManager.h/cpp**. Aucun overhead runtime hors-call (3 getters const, dérivés de l'état existant).

---

## §3 LoopEngine — intégration grille master

### 3.1 Ancrage du recordStart (Mécanisme β)

Au 1er pad press en RECORDING ([LoopEngine.cpp:291-296](../../../src/loop/LoopEngine.cpp:291)), au lieu de `_recordStartUs = micros()` (wall time arbitraire), l'anchor est aligné à un tick boundary selon le quantize per-bank :

| Quantize | Source de `_recordStartUs` |
|---|---|
| **FREE** | `micros()` (tap-to-tap, hors grille) |
| **BEAT** | `clock.getLastBeatWallTimeUs()` |
| **BAR** | `clock.getLastBarWallTimeUs()` |

**Effet** : le 1er event capté reçoit timestamp `(nowUs - anchor) = phase Δ du hit dans son beat/bar courant`. Le micro-timing du groove humain est préservé **relativement à la grille master**, pas relativement au tap utilisateur.

Exemple : utilisateur tape 1er pad à 80 ms après le master beat boundary 12. `_recordStartUs = wallTime(beat 12)`. Event[0] timestamp = 80 ms. À la lecture, l'event firera 80 ms après chaque "position 0 modulo loopDur" (= chaque master beat où la loop wrappe).

### 3.2 Auto-Stop : RECORDING_PENDING_CLOSE

#### Sémantique utilisateur

Le 2e tap REC ne ferme PAS la loop immédiatement. Il signale l'intention de fermer. La loop ferme effectivement au prochain master boundary (next beat ou next bar selon quantize). Pendant l'attente, la capture continue normalement (l'utilisateur peut continuer à jouer jusqu'au boundary).

#### Implémentation : flag sur RECORDING, pas nouvel état

Ajouter 2 champs privés à `LoopEngine` :

```cpp
bool      _recordingPendingClose;      // false par défaut, true après 2e tap REC
uint32_t  _recordingPendingCloseTick;  // tick master cible (boundary)
```

Pas de nouvel état `LoopState`. La state machine reste à 7 états (cf parent spec §3). `PENDING_CLOSE` est un **sous-état de RECORDING via flag**. Cela simplifie :
- `isLocked()` reste `state == RECORDING || state == OVERDUBBING` → PENDING_CLOSE refuse bank switch / CLEAR par construction.
- LED rendering reste sur la couleur RECORDING (Coral) sans modification (décision BS-5).
- Aucun nouveau cas dans `processLoopMode` pour le dispatch state-based.

#### Algorithme tapRec en RECORDING

```
if (_state == RECORDING && _recordFirstPressDone) {
  if (_quantize == FREE) {
    closeRecordingImmediate();   // path FREE : close direct (§3.3)
  } else {
    if (_recordingPendingClose) {
      // BS-9 : 2e tap REC pendant PENDING_CLOSE déjà actif → ignoré
      return;
    }
    _recordingPendingCloseTick = computeNextBoundaryTick(_quantize);
    _recordingPendingClose = true;
    // état reste RECORDING, capture continue (α)
  }
}
```

#### Algorithme update() phase 1 (commit boundary)

```
if (_state == RECORDING && _recordingPendingClose) {
  if (_clock->getCurrentTick() >= _recordingPendingCloseTick) {
    commitRecordingClose();
  }
}
```

#### `commitRecordingClose()` — nouvelle méthode

```
1. _loopDurationUs = (boundary tick wall time) - _recordStartUs
   où "boundary tick wall time" = exact wall time of _recordingPendingCloseTick.
   Précision : _clock->getLastTickWallTimeUs() avec correction sub-tick si catch-up
   (cf §3.4 edge case).

2. Flush held pads : pour chaque pad avec _padHeldLive[pad] == true,
   inject noteOff event au timestamp (_loopDurationUs - 1) dans le buffer.
   Reset _padHeldLive[pad] = false.

3. M6 clamp : pour chaque event avec timestamp >= _loopDurationUs,
   clamp à (_loopDurationUs - 1). Defense in depth (devrait être rare avec
   Auto-Stop, mais protège edge race tick boundary).

4. _recordingPendingClose = false;

5. startPlayback(transport, boundary tick wall time)
   → _lastUpdateUs = boundary tick wall time, _scaledElapsedUs = 0.
   → state = PLAYING.
```

#### Caractéristique géométrique garantie

Par construction, `_loopDurationUs` est un **multiple entier exact** de `quantize unit × tickInterval au record BPM` :
- BEAT : multiple de 24 ticks (= 1 beat).
- BAR : multiple de 96 ticks (= 1 bar).

L'absence de bar-snap math, de deadzone, de trim, et d'extend est conséquence directe de cette construction. La longueur émerge naturellement de la fenêtre `[recordStartAnchor, boundary]`.

### 3.3 FREE mode (hors grille master)

| Étape | Comportement |
|---|---|
| 1er pad press | `_recordStartUs = micros()` — pas d'anchor master |
| Tap REC final | `closeRecordingImmediate()` direct (pas de PENDING_CLOSE) |
| `closeRecordingImmediate()` | `_loopDurationUs = micros() - _recordStartUs = rawDurUs`. Flush held pads à `_loopDurationUs - 1`. `startPlayback(transport, micros())` immédiat. |

Le FREE loop joue tap-to-tap. Sa position 0 ne coïncide avec aucun master tick par chance. Les wraps dérivent par rapport à la grille master au cours du temps (proportionnel au tempo et au rawDurUs).

**Cohérent avec le principe "FREE = explicitement hors grille".**

### 3.4 Edge cases

#### 3.4.1 Boundary tick rattrapé par catch-up

`generateTicks` ([ClockManager.cpp:204](../../../src/midi/ClockManager.cpp:204)) peut générer jusqu'à 4 ticks en une frame si le main loop a un hiccup. Si `currentTick > _recordingPendingCloseTick + 1` au commit, `_lastTickTimeUs` correspond à un tick **après** le boundary visé.

Mitigation : calculer le wall time exact du boundary tick par :
```
diff = _clock->getCurrentTick() - _recordingPendingCloseTick;
boundaryWallTime = _clock->getLastTickWallTimeUs() - diff × _pllTickInterval;
```
Cela nécessite d'exposer aussi `getTickIntervalUs()` côté ClockManager (1 LOC additionnel).

Précision dans le cas normal : 0 µs (boundary tick = currentTick).
Précision dans le cas catch-up : ≤ 80 ms (4 ticks × 20 ms à 120 BPM).

#### 3.4.2 1er pad press au boot avant que ClockManager soit en sync externe

Si l'utilisateur start ILLPAD en slave mode et record AVANT qu'une source externe arrive, ClockManager utilise le tempo interne default 120 BPM. La grille est valide. L'anchor `lastBeatWallTimeUs` retourne une valeur correcte. Loop enregistré aligné sur grille @ 120 BPM.

Si plus tard une source externe arrive, le PLL converge vers le nouveau tempo. `_recordBpm` du loop existant reste latché (immutable). La BPM scaling adapte la vitesse de lecture sans dé-aligner les wraps (cf §4.4).

**Pas de fallback `_currentTick == 0` à coder** — ce cas n'arrive pas en pratique (clock interne tourne dès le boot).

#### 3.4.3 Tap REC final avant tout pad press (buffer vide)

Si `_recordFirstPressDone == false` au tap REC final :
- Pas d'anchor recordStart à honorer.
- Pas de PENDING_CLOSE.
- État retourne `EMPTY` directement.

Comportement actuel préservé (cf [LoopEngine.cpp:508-512](../../../src/loop/LoopEngine.cpp:508)).

#### 3.4.4 Tempo change pendant PENDING_CLOSE

Si l'utilisateur change le tempo (pot ou source externe) pendant la fenêtre d'attente :
- `_pllTickInterval` recompute dans ClockManager.
- Le wall time du boundary tick change (peut être plus tôt ou plus tard).
- Au commit, `_loopDurationUs` reflète la nouvelle durée wall time.
- `_recordBpm` du loop est latché au 1er press, immutable. La lecture utilise donc le BPM d'origine du loop, scalé par liveBpm courant. Cohérent.

Edge case rare, comportement déterministe.

---

## §4 Cohabitation timer ARP / LOOP

### 4.1 Deux représentations, une seule grille

| Système | Représentation interne | Source de truth | Granularité |
|---|---|---|---|
| **ARP / ARPEG_GEN** | Tick-driven via `ArpScheduler.tick()` qui lit `ClockManager::getCurrentTick()` | Master clock direct | 1 tick = 1/24 noire (~20.8 ms à 120 BPM) |
| **LOOP** | µs-driven via `_scaledElapsedUs` accumulé chaque `update()` | `micros()` ancré à un tick boundary à 3 moments fixes | µs (effectif ~3-5 ms limité par cadence main loop) |

L'ARP consomme directement les ticks master. Le LOOP utilise µs pour préserver le micro-timing (un hit utilisateur tombe entre 2 ticks, résolution 20 ms est trop grossière pour le groove).

### 4.2 Points de contact unique : `_lastTickTimeUs`

Le **point de contact** entre les deux mondes est `ClockManager::_lastTickTimeUs`, lu par 3 primitives publiques (§2.3). ARP le lit implicitement (chaque tick = event). LOOP le lit explicitement à 3 moments d'ancrage :

1. **recordStart** (1er pad press) : anchor `_recordStartUs` (§3.1).
2. **Commit boundary** (close PENDING_CLOSE) : `_lastUpdateUs` au startPlayback (§3.2).
3. **Wrap** (loop position retour à 0) : automatique par cohérence math (§4.4).

Entre ces ancrages, l'intégration µs continue sans re-sync. La grille est self-maintaining.

### 4.3 Cohérence BPM-scaled playback × alignement grille

La BPM scaling existante reste **inchangée** :

```cpp
_scaledElapsedUs += (uint64_t)deltaUs × liveBpm / _recordBpm;
```

Démonstration que les wraps restent sur master beats au liveBpm courant :

Soit `loopDur = N × beatDurAt_recordBpm = N × 60e6 / recordBpm`.

Le wall time entre 2 wraps successifs au playback :
```
wallTime(wrap) = loopDur × recordBpm / liveBpm
              = N × (60e6 / recordBpm) × recordBpm / liveBpm
              = N × 60e6 / liveBpm
              = N × beatDurAt_liveBpm  ✓
```

Donc les wraps tombent sur des master beats au tempo live courant, tant que `loopDur` est multiple entier de beat. C'est garanti par Auto-Stop (§3.2 caractéristique géométrique).

### 4.4 Limites de précision

- **PLL ripple ±0.5 BPM** (convergence USB) : drift wall time du wrap ±33 ms max sur loop de 4 bars. Sub-perceptible. Réversible quand PLL re-converge.
- **Switch source externe USB ↔ BLE** mid-loop : phase jump possible (~5 ms). Réversible quand PLL re-converge sur la nouvelle source.
- **Catch-up tick** : ≤ 80 ms (4 ticks) dans le pire cas (main loop hiccup), 0 µs en frame normale.
- **Truncation uint16 BPM** ([ClockManager.cpp:265](../../../src/midi/ClockManager.cpp:265)) : déjà identifiée dans l'audit. Affecte aussi cette logique en BPMs non-entiers (PLL converge à 119.7 → recordBpm = 119). Hors scope de cette spec ; à traiter séparément si nécessaire.

Ces limites sont **sub-perceptibles musicalement** dans les conditions normales d'usage (tempo stable, instrument joué live, durée de loop typique 1-16 bars).

---

## §5 State machine étendue (sous-état flag)

Diagramme synthétique :

```
EMPTY ──tapRec──→ RECORDING
                  │
                  │ (1er pad press latch _recordStartUs selon quantize)
                  │
                  ├── tapRec si FREE ──→ closeRecordingImmediate ──→ PLAYING
                  │
                  └── tapRec si BEAT/BAR ──→ RECORDING + flag PENDING_CLOSE
                                              │
                                              │ (capture continue, gestes ignorés/refusés)
                                              │
                                              └── boundary tick reached
                                                  ──→ commitRecordingClose
                                                      ──→ PLAYING (anchor = boundary wall time)

PLAYING ──tapRec──→ OVERDUBBING ──tapRec──→ PLAYING (merge immédiat, inchangé)
PLAYING ──tapPlayStop──→ WAITING_STOP/STOPPED (transport quantize, inchangé)
STOPPED ──tapPlayStop──→ WAITING_PLAY/PLAYING (inchangé)
```

### Diff vs parent spec §3 / §17

| Élément | Avant | Après |
|---|---|---|
| Nombre d'états | 7 | 7 (inchangé) |
| Flag privé `_recordingPendingClose` | absent | ajouté |
| Transition RECORDING → PLAYING (BEAT/BAR) | directe via stopRecording | via PENDING_CLOSE flag + boundary tick |
| Transition RECORDING → PLAYING (FREE) | directe via stopRecording | directe via closeRecordingImmediate (équivalent) |
| WAITING_PLAY / WAITING_STOP (transport) | existants | inchangés |
| OVERDUBBING merge sur tap REC | immédiat | inchangé |

---

## §6 Décisions actées (traçabilité brainstorm 2026-05-19)

| # | Sujet | Décision actée |
|---|---|---|
| BS-1 | Pivot vs rescale actuel | **Quantize REC stop variante Auto-Stop** (Mobius/EHX/Loopy/Boss RC-505). Remplace entièrement le rescale et le bar-snap actuels. |
| BS-2 | Capture pendant PENDING_CLOSE | **α capture continue** — events captés jusqu'au boundary. |
| BS-3 | Granularité anchor recordStart | **β matched quantize** : `lastBeatWallTime` pour BEAT, `lastBarWallTime` pour BAR. |
| BS-4 | FREE mode au record | **Strict tap-to-tap**, hors grille master, pas de PENDING_CLOSE, anchor = `micros()`. |
| BS-5 | LED feedback PENDING_CLOSE | **Solide RECORDING (Coral)** inchangé pendant la fenêtre. |
| BS-6 | Tap PLAY/STOP pendant PENDING_CLOSE | **Ignoré** (no-op). |
| BS-7 | Long-press CLEAR pendant PENDING_CLOSE | **Refusé** (`isLocked()` retourne true). |
| BS-8 | Bank switch pendant PENDING_CLOSE | **Silent deny** (invariant §23.2 parent spec préservé). |
| BS-9 | Tap REC pendant PENDING_CLOSE | **Ignoré** (le commit est ferme, pas d'undo). |

---

## §7 Delta vs code actuel

### À ajouter

| Fichier | Item | LOC est. |
|---|---|---|
| `src/midi/ClockManager.h/cpp` | Getters `getLastTickWallTimeUs()`, `getLastBeatWallTimeUs()`, `getLastBarWallTimeUs()`, et `getTickIntervalUs()` (pour edge case §3.4.1) | ~30 |
| `src/loop/LoopEngine.h` | Champs `_recordingPendingClose`, `_recordingPendingCloseTick` | ~3 |
| `src/loop/LoopEngine.cpp` (constructor) | Init des nouveaux champs à false/0 | ~3 |
| `src/loop/LoopEngine.cpp` `capturePadEvent` first-press | Anchor `_recordStartUs` selon quantize (§3.1) | ~10 |
| `src/loop/LoopEngine.cpp` `tapRec` branche RECORDING | Si FREE → closeRecordingImmediate ; si BEAT/BAR → set pending (sauf si déjà set) | ~15 |
| `src/loop/LoopEngine.cpp` `update` phase 1 | Check pending boundary, call commitRecordingClose si atteint | ~10 |
| `src/loop/LoopEngine.cpp` | Nouvelle méthode `commitRecordingClose()` (§3.2) | ~30 |
| `src/loop/LoopEngine.cpp` | Nouvelle méthode `closeRecordingImmediate()` (path FREE, §3.3) | ~20 |

**Total à ajouter : ~120 LOC.**

### À supprimer (rescale + bar-snap deadzone math)

| Fichier | Item | LOC supprimés |
|---|---|---|
| `src/loop/LoopEngine.cpp` `stopRecording` | Rescale events (lignes 534-541) | ~8 |
| `src/loop/LoopEngine.cpp` `stopRecording` | Bar-snap deadzone math (lignes 518-532) | ~15 |
| `src/loop/LoopEngine.cpp` `stopRecording` | Méthode entière → remplacée par `commitRecordingClose` (BEAT/BAR) + `closeRecordingImmediate` (FREE) | total `stopRecording` ~70 LOC supprimés |

**Total à supprimer : ~90 LOC** (méthode `stopRecording` entière retirée, son rôle redistribué dans 2 nouvelles méthodes plus claires).

### Solde net

**+120 - 90 = +30 LOC nets.** Code marginal plus simple parce que la complexité bar-snap (deadzone, rescale, snap-up vs snap-down) disparait au profit d'une logique boundary linéaire.

### À garder strictement inchangé (rappel explicite)

- BPM-scaled playback dans `update()` ([LoopEngine.cpp:369-373](../../../src/loop/LoopEngine.cpp:369)).
- WAITING_PLAY / WAITING_STOP transport (STOPPED↔PLAYING) ([LoopEngine.cpp:184-219](../../../src/loop/LoopEngine.cpp:184)).
- OVERDUB merge immédiat à tapRec ([LoopEngine.cpp:143-148](../../../src/loop/LoopEngine.cpp:143)).
- Refcount noteOn/Off ([LoopEngine.cpp:644-659](../../../src/loop/LoopEngine.cpp:644)).
- Multi-bank infrastructure (4 LoopEngines statiques).
- `onBackgroundTransition` flush live press ([LoopEngine.cpp:676-692](../../../src/loop/LoopEngine.cpp:676)).
- `midiPanic` flush all engines.
- LED rendering state-driven ([LedController.cpp:509-558](../../../src/core/LedController.cpp:509)).
- M3 velocity strict capture + variation au playback.
- M8 live monitor MIDI émis tous états.
- `_padHeldLive[NUM_KEYS]` tracker unifié.
- NVS layout (aucun bump version requis).
- Tools setup (Tool 3, 4, 5, 6, 7, 8) inchangés.

---

## §8 Impact sur spec parent

### Sections à amender dans `2026-04-19-loop-mode-design.md`

#### §7 — "Enregistrer un premier loop"
Réécriture du paragraphe "trois choses se passent" au tap REC final :
- **Bar-snap avec deadzone** → **SUPPRIMÉ**. Plus de rescale, plus de deadzone, plus de snap rétroactif.
- **Flush des pads tenus** → conservé. Au timestamp `_loopDurationUs - 1` (boundary tick wall time - anchor - 1 pour BEAT/BAR, rawDurUs - 1 pour FREE).
- **Transition vers PLAYING** → réécrite : immédiate en FREE, au prochain master tick boundary en BEAT/BAR via PENDING_CLOSE.

#### §17 — "Quantization (Play, Stop, Load)"
Ajouter §17.1 "Quantize au record (Auto-Stop)" — le `loopQuantize` per-bank gouverne maintenant **DEUX** comportements :
1. **Transport** : tap PLAY/STOP/LOAD en STOPPED/PLAYING (existant, §17 actuel).
2. **Recording close** : tap REC final en RECORDING (nouveau, Auto-Stop, §3.2 de cette spec).

Cross-ref pointer vers cette spec `Illpad_Master_Sync.md`.

#### §24 — Non-goals
Ajouter une ligne : **"Pas de rescale silencieux des timestamps stockés"** — interdit de modifier le temps d'un event après capture. Le micro-timing est sacré.

### Sections nouvelles à cross-référencer

- §17.1 (nouvelle) → renvoie à `Illpad_Master_Sync.md` (cette spec).
- §28 (décisions tranchées) → ajouter ligne "Master Sync — pivot Auto-Stop validé 2026-05-19, cf `Illpad_Master_Sync.md`".

---

## §9 Impact LED / gesture / autres systèmes

| Système | Impact |
|---|---|
| **LED grammar** | Aucun. EVT_LOOP_REC reste solide Coral pendant PENDING_CLOSE (BS-5). EVT_PLAY au commit boundary (déjà câblé). Aucun nouveau pattern, aucun nouveau color slot. |
| **Gesture (processLoopMode)** | Aucun nouveau geste utilisateur. Les gestes existants sont étendus en sémantique par flag PENDING_CLOSE (table §3.2). Le tap REC final acquiert un comportement "armed close" en BEAT/BAR. |
| **Tool 3 Pad Roles** | Aucun. |
| **Tool 4 Control Pads** | Aucun. |
| **Tool 5 Bank Config** | Aucun. Labels "Free/Beat/Bar" gardent leur sens (valeurs 0/1/2 préservées dans BankTypeStore). |
| **Tool 6 Settings** | Aucun. |
| **Tool 7 Pot Mapping** | Aucun. |
| **Tool 8 LED Settings** | Aucun. |
| **Viewer protocol** | Optionnel : ajouter trace `[LOOP] Bank N: REC pending close (target=tick)` PRIO_HIGH si Phase 3 Viewer LOOP est implémenté. Pas requis pour Paquet A. |
| **NvsManager** | Aucun. NVS layout inchangé. Pas de bump version. |
| **ArpEngine** | Aucun. ARP continue de consommer la grille master directement, sans modification. |

---

## §10 HW gates / scénarios de test

### G1 — Recording BEAT alignement basique
1. Master mode, tempo interne 120 BPM. Tool 5 : bank 2 = LOOP BEAT.
2. Bank 2 sélectionnée, EMPTY. Tap REC. État RECORDING (LED Coral).
3. Au master beat #2 environ, tap 1er pad. Vérifier serial trace : `_recordStartUs` correspond au wall time du master beat #2 (= `lastBeatWallTime`).
4. Tap 4 pads sur 2 beats.
5. Tap REC final entre beat #4 et #5. État reste RECORDING (LED Coral inchangé, capture continue).
6. Tap pad #5 entre tap REC et boundary. Doit être capturé (α).
7. Au master beat #5 wall time, close + startPlayback automatique. LED bascule Green PLAYING.
8. Vérifier au DAW : loop length = exactement 3 beats wall time. Premier event après wrap fire pile sur master beat.

**Validation** : LOOP wraps tombent sur master beats à la milliseconde.

### G2 — Recording BAR alignement basique
Idem G1 mais avec quantize BAR. Loop length = N bars (entiers). Anchor = `lastBarWallTime` (= beat 1 d'une mesure).

### G3 — Recording + ARP sync simultané
1. Bank 1 = ARPEG NORMAL division 1/4. Capture une pile de notes pour la laisser tourner.
2. Bank 2 = LOOP BEAT. Bascule sur bank 2.
3. Master clock 120 BPM (interne).
4. Sur bank 2, record une loop de 4 beats avec Auto-Stop.
5. **Vérifier au DAW** : les notes arp (canal 1) et les wraps loop (canal 2) tombent sur les mêmes ms — delta de phase < 1 ms (limite PLL convergence + frame jitter main loop).

### G4 — Recording LOOP1 + LOOP2 sync mutuel
1. Banks 1 et 2 = LOOP BEAT.
2. Record LOOP1, 4 beats. Bascule sur bank 2.
3. Record LOOP2, 2 beats.
4. **Vérifier au DAW** : LOOP1 wraps toutes les 4 beats, LOOP2 wraps toutes les 2 beats. Les wraps LOOP2 sur beats pairs coïncident exactement avec les wraps LOOP1 (à la milliseconde près).

### G5 — FREE mode dérive intentionnelle
1. Bank LOOP en quantize FREE.
2. Tap REC, jouer ~1.7 s, tap REC.
3. **Vérifier au DAW** : loop length = ~1.7 s exact (pas de snap). Wraps dérivent par rapport au master clock visible sur le métronome DAW. Comportement conforme à la dichotomie "FREE = hors grille".

### G6 — Gestes refusés/ignorés pendant PENDING_CLOSE
1. Bank LOOP BEAT, RECORDING avec ≥ 1 pad press effectué.
2. Tap REC final → entrée PENDING_CLOSE (≤ 1 beat d'attente).
3. **Pendant la fenêtre** :
   - Tap PLAY/STOP → ignoré (BS-6, no-op silencieux).
   - 2e tap REC → ignoré (BS-9, le commit reste ferme).
   - LEFT + bank pad autre → silent deny (BS-8, pas de bank switch).
   - Long press CLEAR (≥ 500 ms si possible dans la fenêtre) → ignoré (BS-7, isLocked).
4. Boundary atteint → close + PLAYING (LED Green).

### G7 — Continue capture pendant PENDING_CLOSE (α)
1. Tap REC final ~200 ms avant un boundary master beat.
2. Pendant ces 200 ms, tap 2 pads supplémentaires.
3. **Vérifier au DAW** : les 2 derniers hits sont présents dans la loop, firent à leur position relative dans le cycle suivant.

### G8 — Held pad through boundary commit
1. Hold pad #4 dès le 1er pad press du recording. Maintenir tenu pendant tout l'enregistrement et au-delà.
2. Tap REC final encore tenu.
3. Au boundary commit, vérifier :
   - NoteOff event inject à `_loopDurationUs - 1` dans le buffer (trace serial).
   - Pad encore physiquement enfoncé (état hardware inchangé).
4. Loop joue. Au passage du noteOff dans le buffer (fin de cycle), MIDI noteOff envoyé au DAW. Au wrap, le noteOn ré-fire → MIDI noteOn.
5. Release du pad final. MIDI noteOff (refcount).

### G9 — Tempo change pendant PENDING_CLOSE
1. Master mode, tempo 120 BPM.
2. RECORDING + 1 pad press. Tap REC final (entrée PENDING_CLOSE).
3. Pendant l'attente, tourner pot tempo vers 100 BPM.
4. **Vérifier** : boundary tick reste = même nombre de ticks visé, mais wall time du tick glisse vers le futur. Close au nouveau wall time. Loop length reflète le timing au nouveau tempo. Pas de crash, état stable.

---

## §11 Travail estimé

| Phase | Effort |
|---|---|
| Code (~30 LOC nets) répartis en 3-4 commits ciblés | 1.5 jours |
| HW gates G1-G9 + traces serial validation | 1.5 jours |
| Doc-sync (parent spec amendement + STATUS + LOOP_PROGRESS + runtime-flows + nvs-ref) | 0.5 jour |
| Marge debug/regressions imprévues | 0.5 jour |

**Total : ≈ 4 jours.**

### Décisions reportées explicitement hors scope

- **D6 / D7** : rename `LoopQuantize` enum / Tool 5 label cosmétique. Defer. Aucune valeur ajoutée musicale immédiate.
- **OVERDUB quantization** : tap REC sur OVERDUBBING = merge immédiat (inchangé). À reconsidérer dans un futur Paquet si désirable.
- **Count-in / Auto-Rec armed start** : variante de "Auto-X" qui retarde le DÉBUT du recording (tap REC sur EMPTY → wait next bar, puis RECORDING). Hors scope. Pourrait revenir comme paramètre `recordArm` futur.
- **LED feedback rich pendant PENDING_CLOSE** : options B (modulation) et C (état dédié) du brainstorm. Pourraient revenir en polish UX futur (Paquet D LED).
- **Master Loop multi-bank** : concept différent (1ère LOOP = référence, autres = ×N multiple). **Explicitement mis de côté** par décision utilisateur 2026-05-19 — pas la direction souhaitée.
- **Subdivision change runtime** (1/8 ↔ 1/64, _playbackRateMul) : feature future, hors Paquet A.

---

## §12 Pipeline d'implémentation

1. **Ce doc validé** (lecture + ajustements). ✓ post brainstorm 2026-05-19.
2. **Patch parent spec** [`2026-04-19-loop-mode-design.md`](2026-04-19-loop-mode-design.md) : §7, §17, §24 amendés. Cross-ref vers ce doc.
3. **Décider commit direct ou mini-plan** :
   - LOC estimé : ~30 nets. Sous le seuil "commit direct" habituel.
   - Mais le refacto touche plusieurs fichiers (ClockManager + LoopEngine), donc un mini-plan `plans/2026-05-19-master-sync-implementation-plan.md` est probablement souhaitable pour découper en gates de validation HW intermédiaires.
   - À trancher avec Loïc post-validation de cette spec.
4. **Code** : 3-4 commits ciblés.
   - Commit 1 : ClockManager getters (`getLastTickWallTimeUs`, `getLastBeatWallTimeUs`, `getLastBarWallTimeUs`, `getTickIntervalUs`).
   - Commit 2 : LoopEngine champs + `capturePadEvent` first-press anchor selon quantize.
   - Commit 3 : `tapRec` PENDING_CLOSE + `update()` phase 1 commit + nouvelles méthodes `commitRecordingClose` / `closeRecordingImmediate`. Suppression de `stopRecording` (bar-snap + rescale).
   - Commit 4 : doc-sync (parent spec + STATUS + LOOP_PROGRESS + runtime-flows + nvs-ref si touché).
5. **HW gates G1-G9** (§10) — validation HW Loïc requise avant doc-sync final.
6. **Doc-sync final** : STATUS / LOOP_PROGRESS marqués comme "Master Sync implémenté commit X".

---

**Fin du doc.** Self-suffisant pour comprendre le pivot Master Sync ILLPAD V2.
