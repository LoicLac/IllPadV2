# LOOP — pivot d'algorithme : vers un instrument standard

**Date** : 2026-05-19
**Statut** : design draft post-audit Phase 2 close — **non encore validé par Loïc**, pré-requis avant tout code.
**Auteur** : session audit + recherche loopers de référence du 2026-05-19.

**Cross-refs** :
- Spec parent (à amender post-validation) : [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](../specs/2026-04-19-loop-mode-design.md) §7, §17, §24
- Plan déjà exécuté (Phase 2 close) : [`docs/superpowers/plans/2026-05-18-loop-phase-2-plan.md`](../plans/2026-05-18-loop-phase-2-plan.md)
- Audit déclencheur : conversation session 2026-05-19 (rapport read-only en mémoire conversation, pas de doc dédié)
- Recherche loopers de référence : conversation session 2026-05-19 (Boss RC-505, Mobius, Sooperlooper, EHX 95000, Headrush Looperboard, Loopy Pro, Pigtronix Infinity, Chase Bliss Blooper, Ableton Live Looper, Fractal Audio)
- État courant : [`STATUS.md`](../../../STATUS.md) — focus LOOP Phase 2 close, prochaine étape Phase 3 (Tool 3 b1 + Tool 4 ext)

---

## §0 — Contexte et déclencheur

Phase 2 LOOP a été livrée 2026-05-19 sur `main` (10 commits 2.A → 2.J + doc-sync, HW gates G1-G9 validés). Build clean, premier son MIDI LOOP audible au DAW, multi-bank fonctionnel.

**Test musical post-livraison** : Loïc rapporte que le looper "ne sonne pas exactement comme ce qu'il a joué". Sensation de timing décalé, groove altéré, comportement non-standard pour un musicien.

**Audit cross-référence code vs spec (read-only)** : 4 runtime findings + 3 incohérences identifiés. **Le finding dominant musicalement** est le **bar-snap avec rescale** au `stopRecording` — les timestamps des events sont multipliés par `snappedDur / rawDur` pour faire rentrer la performance dans une longueur arrondie. Conséquence : un musicien qui joue 1.3 bars et tape REC voit ses events **stretchés ×1.54** (snap-up à 2 bars) — le micro-timing du groove est détruit.

**Recherche industrie 2026-05-19** : aucun looper de référence ne rescale les timestamps au close-record. Tous (sans exception identifiée sur 11 produits) font du **trim-late** (drop des events tail) ou **extend-silent** (ajout de silence en fin), jamais du stretch sur événements stockés. Le rescale n'est ni un détail d'implémentation ni une variante mineure — c'est une **anomalie structurelle** vis-à-vis du standard de l'industrie.

**Décision** : pivot d'algorithme nécessaire. Le looper doit devenir un instrument standard musicalement avant toute extension fonctionnelle (Phase 3+).

---

## §1 — Diagnostic technique synthétique

### Ce qui pose problème (à supprimer)

`stopRecording` ligne 534-541 actuellement :

```cpp
// Rescale event timestamps proportionally
if (rawDurUs > 0 && snappedDurUs != rawDurUs) {
  uint64_t scaleNum = snappedDurUs;
  uint64_t scaleDen = rawDurUs;
  for (uint16_t i = 0; i < _eventCount; i++) {
    _events[i].timestampUs = (uint32_t)((uint64_t)_events[i].timestampUs * scaleNum / scaleDen);
  }
}
```

Effet réel :
- Snap-down (remainder ≤ deadzone 25 %) : ratio dans [0.94, 1.0] → compression légère (≤ 6 %).
- **Snap-up (remainder > deadzone)** : ratio = `(bars+1) / (bars+0.25)` → discontinuité au seuil :
  - 1 bar : **×1.60 stretch** (60 %)
  - 2 bars : ×1.33
  - 4 bars : ×1.17
  - 8 bars : ×1.09

Les loops courts sont **violemment déformés** dès qu'on est à 26 % au-delà d'une bar line — soit ~130 ms de latence du tap REC à 120 BPM. Aucun musicien ne tape avec une précision de 30 ms.

### Ce qui ne pose pas problème (à conserver)

Le BPM-scaled playback dans `update()` lignes 369-373 :

```cpp
uint16_t liveBpm = _clock ? _clock->getSmoothedBPM() : _recordBpm;
uint32_t deltaUs = nowUs - _lastUpdateUs;
_scaledElapsedUs += (uint64_t)deltaUs * liveBpm / _recordBpm;
```

**Ce mécanisme est standard et utile**. Loopy Pro, Mobius, Ableton, Headrush — tous adaptent la vitesse de lecture au tempo live. Ce stretch-là est :
- Non-destructif : les timestamps stockés ne sont pas modifiés.
- Réversible : si le tempo live revient à `_recordBpm`, la lecture revient à l'identique.
- Voulu par l'utilisateur : c'est l'invariant "loop suit le tempo".

**Distinction critique à graver dans toute discussion future** :
| Mécanisme | Effet sur timestamps stockés | À garder ? |
|---|---|---|
| **Stretch au record** (rescale au stopRecording) | Permanent, destructif | **NON — à supprimer** |
| **Stretch au playback** (`liveBpm/recordBpm` chaque update) | Lecture seule, non-destructif | **OUI — à conserver** |

---

## §2 — Le pivot : trim-late + extend-silent

### Nouveau comportement de `stopRecording`

```
1. Calculer rawDurUs = micros() - _recordStartUs (inchangé)
2. Calculer barDurUs = 240e6 / _recordBpm (inchangé)
3. Discriminer par _quantize :

   FREE  → _loopDurationUs = rawDurUs
           events conservés tels quels, aucun snap
           held pads → noteOff inject au rawDurUs - 1

   BEAT  → snap unit = beatDurUs (= barDurUs / 4)
   BAR   → snap unit = barDurUs

   Pour BEAT/BAR :
   - barsFloor = rawDurUs / snapUnit
   - remainder = rawDurUs - barsFloor × snapUnit
   - deadzone = snapUnit / 4  (= 25 %)
   - if remainder ≤ deadzone : snap DOWN (snappedDur = barsFloor × snapUnit)
   - else                     : snap UP   (snappedDur = (barsFloor+1) × snapUnit)
   - clamp 1..64 (en unités snapUnit)
   - _loopDurationUs = snappedDur

4. Selon snap result :

   SNAP-DOWN (snappedDur < rawDur) :
   - TRIM events avec timestampUs ≥ snappedDur (drop silencieux)
   - Pour toute note encore active (refcount évent virtuel à snappedDur),
     inject noteOff au timestamp snappedDur - 1
   - held pads → inject noteOff au timestamp snappedDur - 1

   SNAP-UP (snappedDur > rawDur) :
   - events INCHANGÉS (gardent leurs timestamps originaux)
   - Silence implicite entre rawDur et snappedDur
   - held pads → inject noteOff au timestamp rawDur - 1  (pas snappedDur !)

   FREE / snap-exact (snappedDur == rawDur) :
   - events inchangés
   - held pads → inject noteOff au timestamp rawDur - 1

5. Aucun rescale. Aucune multiplication de timestamps.
```

### Pourquoi ça résout le problème musical

- **Snap-down** : les events au-delà de la barre sont coupés net. Le musicien perd les 6 % de fin (max), mais le groove dans la barre est intact. Aucune note ne bouge.
- **Snap-up** : la barre suivante est complétée par du silence. Le musicien voit sa boucle entière à sa place, plus du silence en fin. Le groove est intact à la microseconde.
- **Free** : la boucle = exactement ce que le musicien a joué.

Dans les trois cas, le micro-timing est préservé. La boucle joue littéralement ce qui a été enregistré, sans transformation temporelle.

### Conformité industrie

| Looper de référence | Comportement REC stop | Équivalent dans le pivot |
|---|---|---|
| Boss RC-505 Loop Quantize | "extend or trim end of loop" | BEAT/BAR snap (extend = snap-up silence, trim = snap-down drop) |
| Mobius QuantizeMode | "round up to next bar" | snap-up uniquement (variante) |
| Sooperlooper sync | "round off to multiple of sync interval", "does NOT timestretch" | BEAT/BAR snap |
| Loopy Pro Count-Out Master | "waits to next master cycle" | snap-up auto-stop (variante) |
| EHX 95000 Quantize ON | "automatically stops on bar" | snap-up auto-stop (variante) |
| Boss RC-3 sans rhythm | tap-to-tap exact | FREE |
| Sooperlooper sync = none | tap-to-tap exact | FREE |
| Headrush Free Mode | "tracks can be different lengths" | FREE |

Le nouveau comportement ILLPAD couvre les deux paradigmes dominants (round-off Boss/Sooperlooper et auto-stop Mobius/Loopy/EHX) via le discriminant `_quantize` per-bank.

---

## §3 — Paquet A — Fix musical fondamental

**Objectif** : remplacer le rescale par trim-late + extend-silent, exposer un vrai mode FREE, garder le reste.

### Items inclus

| # | Item | LOC est | Risque | Fichier principal |
|---|---|---|---|---|
| A.1 | Remplacer rescale par trim-late + extend-silent dans `stopRecording` | ~40 | Faible | `src/loop/LoopEngine.cpp:504-571` |
| A.2 | FREE quantize = no bar-snap au record (early return ou branche dédiée) | ~10 | Faible | idem |
| A.3 | BEAT quantize = snap à multiple de beat (snapUnit = barDur/4) | ~20 | Faible | idem |
| A.4 | Doc-sync : spec §7, §17, §24 + plan + STATUS + nvs-ref + runtime-flows | doc-only | Faible | docs/ |

**Total estimé** : 1.5j coding + 1j HW + 0.5j doc ≈ **3 jours**.

### Ce qui ne change PAS dans Paquet A

- State machine 7 états (EMPTY/RECORDING/PLAYING/OVERDUBBING/STOPPED/WAITING_PLAY/WAITING_STOP).
- WAITING_PLAY/STOP transport quantize via master clock ticks.
- BPM-scaled playback (`_scaledElapsedUs += delta × liveBpm/recordBpm`).
- Refcount noteOn/Off symétrique ArpEngine.
- Multi-bank infrastructure (4 LoopEngines statiques).
- Overdub merge atomique O(n+m).
- `onBackgroundTransition` flush live press.
- `midiPanic` flush all LoopEngines.
- `toggleAllArpsAndLoops`.
- LED rendering state-driven.
- M8 live drumming MIDI émis tous états.
- M3 velocity strict capture + variation au playback.
- `_padHeldLive[]` tracker unifié.
- NVS layout (pas de bump version).

### Décisions à trancher AVANT code Paquet A

Voir §6.

---

## §4 — Paquet B — Master Sync multi-bank

**Objectif** : ajouter le concept de master loop. La première bank LOOP enregistrée définit la longueur de référence ; les loops suivants sont forcés à un multiple entier (×1, ×2, ×3, ×4).

**Phase dédiée**, à acter post-Paquet A et post-brainstorming des décisions ouvertes.

### Items inclus

| # | Item | LOC est | Risque | Notes |
|---|---|---|---|---|
| B.1 | Struct `LoopMasterRef` (durée référence + bank index source) + hooks `stopRecording` / `longPressClear` | ~80 | Moyen | Nouveau fichier `src/loop/LoopMasterRef.h/.cpp` ou intégré dans existing |
| B.2 | Snap subsequent loops à ×1/×2/×3/×4 master (override bar-snap quand master existe) | ~30 | Faible | Réutilise infra trim-late/extend-silent du Paquet A |
| B.3 | Politique de reset master sur CLEAR (cascade / réélection / refus) | ~40 | Moyen | Décision design avant code |
| B.4 | `SettingsStore` v12 flag global "Master Sync ON/OFF" + Tool 6 ligne édition + cycle Tool 5 | ~80 | Faible | Bump NVS v11→v12 |
| B.5 | LED feedback "I am master" (color slot dédié ou pattern overlay) | ~30 | Faible | Tool 8 ajoute le color slot |
| B.6 | HW gates complets multi-bank scenarios | tests | Variable | Master record, slave record, master clear, mismatch refus |

**Total estimé** : 3-4j coding + 2j HW + 1j brainstorm design + 0.5j doc ≈ **7-8 jours**.

### Pré-requis avant Paquet B

1. Paquet A livré (le master sync repose sur trim-late/extend-silent du Paquet A).
2. Brainstorm dédié des 4 décisions design :
   - Master = 1ère LOOP enregistrée auto, ou désignée explicitement par geste ?
   - Politique de reset master quand le master est CLEARed ?
   - Master sync = global ON/OFF, ou par-bank opt-in ?
   - Comportement si rawDur < 0.75× master (snap to 0 = refus, ou snap to 1× = stretch silencieux à master) ?
3. Spec dédiée Master Sync (nouveau doc ou section ajoutée à la spec LOOP).

### Décisions ouvertes Paquet B (brainstorm à venir)

Aucune décision à prendre maintenant — ces points seront tranchés lors de la prep Paquet B.

---

## §5 — Paquet C — Future (hors scope immédiat)

Pour mémoire, capturé ici parce qu'évoqué en discussion :

**Subdivision change runtime (1/8 ↔ 1/64, etc.)** : permettre à l'utilisateur de doubler/diviser la vitesse de playback d'une LOOP en live, indépendamment du tempo master. Mécanisme proposé : ajouter un champ `_playbackRateMul` (float, défaut 1.0) qui multiplie `liveBpm/recordBpm` dans le calcul `_scaledElapsedUs`. Pot mapping Tool 7 ou geste dédié.

**Statut** : feature future, hors Paquet A et B. Non bloquante. Sera spec'ée à part quand priorisée.

---

## §6 — Décisions à trancher avant code Paquet A

À brainstormer en conversation entre Loïc et Claude **avant** toute modification de spec ou de code.

### D1 — Comportement strict de FREE au record

- **Option A1** (recommandé) : FREE = strict tap-to-tap. Aucun snap. `_loopDurationUs = rawDurUs` exactement. Aucun event modifié. Held pads → noteOff inject au `rawDurUs - 1`.
- **Option A2** : FREE = tap-to-tap mais avec snap optionnel paramétrable (4e quantize value ?). Complexité ajoutée pour gain marginal.

### D2 — Trim-late : injection noteOff au boundary

Au snap-down, des events noteOn peuvent exister sans noteOff matching dans le buffer (cas : pad pressé près de la fin, REC tapé avant release naturelle, snap-down coupe les events qui étaient au-delà mais le noteOn reste).

- **Option B1** (recommandé) : walker le buffer en ordre, maintenir un set "notes encore actives à snappedDur", inject noteOff au timestamp `snappedDur - 1` pour chacune. Coût : un parcours O(N) + un set 128 bool.
- **Option B2** : seulement utiliser `_padHeldLive[]` au moment du tap REC. Simple mais incomplet (manque les notes capturées dans le buffer dont le release est passé au-delà du snap-down).

### D3 — Extend-silent : held pads où ?

Quand snap-up étend la loop avec silence, et qu'un pad est encore physiquement tenu au moment du tap REC :

- **Option C1** (recommandé) : noteOff inject au `rawDurUs - 1`. La note dure du press à la fin de ce que le musicien a joué. Le silence suit naturellement.
- **Option C2** : noteOff inject au `snappedDurUs - 1`. La note tient jusqu'à la fin musicale de la loop. Comportement potentiellement surprenant (note tenue pendant un silence apparent au playback).

### D4 — BEAT quantize au record : précision

Au quantize BEAT, snap unit = `barDurUs / 4` (= 1/4 note). Deadzone 25 % du snap unit (= 1/16 note).

- **Option D1** (recommandé) : deadzone proportionnel = 25 % du snap unit dans tous les cas. Cohérent avec BAR.
- **Option D2** : deadzone fixe = 25 % du barDur dans les deux cas. Plus permissif au BEAT (deadzone = bar entière).

### D5 — Edge case : rawDur < snapUnit

Si l'utilisateur tape REC sur EMPTY → joue 0.4 beat → tap REC :

- **Option E1** (recommandé) : snap-up forcé. Minimum = 1× snapUnit. Si BEAT, min 1 beat ; si BAR, min 1 bar. Cohérent avec clamp `snappedBars ≥ 1` existant.
- **Option E2** : refus (revert à EMPTY, ne pas créer de loop). Comportement Boss-like ("trop court, on ignore").

### D6 — Renommage enum `LoopQuantize`

Actuellement : `LOOP_QUANT_FREE = 0`, `LOOP_QUANT_BEAT = 1`, `LOOP_QUANT_BAR = 2`.

- **Option F1** (recommandé) : conserver les valeurs (zéro-migration NVS), garder les noms. Le sens couvre maintenant transport ET record, ce qui est cohérent.
- **Option F2** : renommer en `LOOP_SYNC_OFF / LOOP_SYNC_BEAT / LOOP_SYNC_BAR` pour mieux refléter le scope étendu. Code-cosmétique, NVS values identiques.

### D7 — Tool 5 affichage label FREE

Tool 5 affiche actuellement "Free" / "Beat" / "Bar" dans la cellule quantize d'une bank LOOP.

- **Option G1** (recommandé) : conserver. Le label "Free" devient cohérent (= aucun sync au record ni au transport).
- **Option G2** : renommer "Free" → "No sync" pour clarifier. Cosmétique.

---

## §7 — Plan de sortie après Paquet A livré

1. **Maintenant** : ce doc validé par Loïc (lecture + ajustements).
2. **Brainstorm décisions D1-D7** (§6) en conversation. Une décision à la fois, multi-choice quand approprié.
3. **Update spec `2026-04-19-loop-mode-design.md`** : §7 (récriture), §17 (note sur scope record vs transport), §24 (non-goals — "pas de stretch implicite des timestamps stockés" ajouté).
4. **Décision plan ou pas plan** : si LOC final < 100 et changement bien circonscrit, commit direct avec HW gate. Sinon mini-plan dédié dans `plans/`.
5. **Code Paquet A** : commit unique idéalement, ou 2-3 commits ciblés (FREE / BEAT / BAR / doc-sync).
6. **HW gate** : enregistrer 1 loop sur chaque mode FREE/BEAT/BAR, vérifier au DAW que les hits sortent au timing joué, que snap-down trim proprement, que snap-up ajoute du silence sans toucher aux events.
7. **Doc-sync** : `STATUS.md` + `LOOP_PROGRESS.md` + cette spec (statut "implémenté").

**Pas de Paquet B avant Paquet A close et HW validé.**

---

## §8 — Risques et contre-mesures

| Risque | Probabilité | Impact | Contre-mesure |
|---|---|---|---|
| Régression playback (touch involontaire du chemin BPM-scaled) | Faible | Élevé (perte feature à garder) | Tests HW comparatifs avant/après. Ne pas toucher `update()`. |
| Snap-up + held pad : ambiguïté noteOff position | Moyen | Moyen | Décision D3 explicite. Test HW dédié pad tenu au REC stop. |
| FREE mode avec rawDur très court (< 100 ms) crée loop "tap-to-tap" inutilisable | Faible | Faible | Décision D5 : floor minimum à 1 beat ou 1 bar. |
| Refacto stopRecording casse un audit-fix antérieur (M6 clamp, B-N2 flush) | Moyen | Moyen | Garder M6 clamp en defense-in-depth. Garder B-N2 inject noteOff via `_padHeldLive[]`. |
| Doc spec divergente après commit code | Faible | Moyen | Doc-sync dans le commit code (4-link Setup/Runtime invariant 7). |

---

## §9 — Validation requise avant code

- [ ] Loïc lit et valide ce doc.
- [ ] Décisions D1-D7 (§6) tranchées en brainstorm.
- [ ] Spec `2026-04-19-loop-mode-design.md` patchée et validée.
- [ ] Décision : commit direct (LOC < 100) ou mini-plan ?
- [ ] HW gate scenarios listés.

Une fois ces 5 checkboxes cochées, le coding peut commencer.

---

**Fin du doc.** Référence stable pour le pivot LOOP standard-compliance.
