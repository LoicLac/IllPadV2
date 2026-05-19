# Viewer serial protocol — addendum LOOP

**Date** : 2026-05-18
**Statut** : proposition, en attente de validation avant intégration plan / code.
**Auteur** : session Claude Opus 4.7 (review du plan loop Phase 2 + protocole existant)

## §0 Cross-refs

- Pipeline parent — protocole serial firmware → viewer :
  - [`specs/2026-05-17-viewer-serial-centralization-design.md`](./2026-05-17-viewer-serial-centralization-design.md) — Phase 1, centralisation des emit dans `ViewerSerial`.
  - [`specs/2026-05-17-viewer-bidirectional-phase2-design.md`](./2026-05-17-viewer-bidirectional-phase2-design.md) — Phase 2, write commands `!KEY=VAL` viewer → firmware.
- Pipeline parent — mode LOOP :
  - [`specs/2026-04-19-loop-mode-design.md`](./2026-04-19-loop-mode-design.md) — design d'origine du mode.
  - [`archive/2026-05-18-loop-phase-2-plan.md`](../../archive/2026-05-18-loop-phase-2-plan.md) — plan d'implémentation engine (exécuté Phase 2 LOOP CLOSE 2026-05-19, archivé).
- Code de référence :
  - [`src/viewer/ViewerSerial.h`](../../../src/viewer/ViewerSerial.h)
  - [`src/viewer/ViewerSerial.cpp`](../../../src/viewer/ViewerSerial.cpp)
  - [`ILLPADViewer/Source/serial/RuntimeParser.cpp`](../../../../ILLPAD_V2-viewer/ILLPADViewer/Source/serial/RuntimeParser.cpp) (worktree `viewer-juce`)
  - [`ILLPADViewer/Source/model/`](../../../../ILLPAD_V2-viewer/ILLPADViewer/Source/model/) (BankInfo, CurrentBankState, Model)

## §1 Contexte — ce qui manque aujourd'hui

Le plan LOOP Phase 2 ([loop-phase-2-plan.md](../../archive/2026-05-18-loop-phase-2-plan.md)) ajoute la state machine `LoopEngine` (`EMPTY / RECORDING / PLAYING / OVERDUBBING / STOPPED / WAITING_PLAY / WAITING_STOP`) et tout le pipeline capture / playback / overdub / merge. **Seule télémétrie protocole prévue** : `[LOOP_BUFFER_FULL]` ([`ViewerSerial.h:50-53`](../../../src/viewer/ViewerSerial.h)).

Conséquences observables :

1. Les transitions de state machine LOOP sont **invisibles côté viewer** — pas de feedback "REC armé / PLAY / OD start / Cleared".
2. `[STATE]` pour une bank LOOP émet `R1=--- … R4H=---` ([`ViewerSerial.cpp:659-664`](../../../src/viewer/ViewerSerial.cpp)) — aucun champ `loopState`, `eventCount`, `quantize`. Un viewer connecté à froid sur une bank LOOP active ne peut pas reconstruire l'état.
3. `[BANK_SETTINGS]` return tôt si `type != BANK_ARPEG_GEN` ([`ViewerSerial.cpp:919`](../../../src/viewer/ViewerSerial.cpp)) — `quantize` (config persistant LOOP) n'est exposé nulle part.
4. Côté viewer JUCE, `BankType::LOOP` existe dans l'enum ([`enums/BankType.h:5`](../../../../ILLPAD_V2-viewer/ILLPADViewer/Source/enums/BankType.h)) mais **zéro field binding LOOP-spécifique** dans `BankInfo` / `CurrentBankState` / `Model::apply()`. Aucun handler n'attend de transition LOOP.
5. `[LOOP_BUFFER_FULL]` lui-même n'est pas décodé par `RuntimeParser` — émission firmware vouée à tomber dans le bucket `UnknownEvent` viewer.

## §2 Patterns du protocole à étendre

| Pattern | Référence code | Application LOOP |
|---|---|---|
| `[STATE] bank=N mode=TYPE …` ; les fields ajoutés varient par type (`octave=` pour ARPEG, `mutationLevel=` pour ARPEG_GEN) | [`ViewerSerial.cpp:650-656`](../../../src/viewer/ViewerSerial.cpp) | Ajouter `loopState=`, `eventCount=`, `waitingFor=` (conditionnel) |
| `[BANK_SETTINGS] bank=N …` no-op si type ≠ ARPEG_GEN, dump dédié sinon | [`ViewerSerial.cpp:916-930`](../../../src/viewer/ViewerSerial.cpp) | Dispatch par type — variante LOOP `quantize=FREE\|BEAT\|BAR` |
| `[ARP] Bank N: <verb> (qualifier)` PRIO_HIGH pour transitions transport | [`ViewerSerial.cpp:740-760`](../../../src/viewer/ViewerSerial.cpp) | Nouvelle famille `[LOOP] Bank N: <verb> (qualifier)` |
| Boot dump + auto-resync ré-émettent header + state + settings par bank | [`ViewerSerial.cpp:108-117`](../../../src/viewer/ViewerSerial.cpp) | Aucun changement si `[STATE]` et `[BANK_SETTINGS]` sont étendus |
| PRIO_HIGH state, PRIO_LOW télémétrie droppable | [`ViewerSerial.cpp:72-91`](../../../src/viewer/ViewerSerial.cpp) | Transitions = HIGH, `[LOOP_BUFFER_FULL]` = LOW (déjà) |
| Em-dash U+2014 pour qualifier secondaire (`Play — relaunch paused`) | [`ViewerSerial.cpp:743`](../../../src/viewer/ViewerSerial.cpp) | Réutilisable si nuance à distinguer (ex. `Play — first start`) |

## §3 Événements proposés

### §3.A `[LOOP]` — transitions runtime

Nouvelle famille, calque strict de `[ARP]`. Toutes PRIO_HIGH (rare, critique LED viewer).

| Ligne émise | Transition | Callsite (à confirmer en lecture `LoopEngine.cpp`) |
|---|---|---|
| `[LOOP] Bank N: REC armed` | EMPTY → RECORDING (REC tap, attente 1er pad press) | `startRecording()` |
| `[LOOP] Bank N: REC capturing (bpm=120)` | armed → actif (1er pad press, BPM latché) | `onFirstCapturedEvent()` |
| `[LOOP] Bank N: REC closed (events=42 dur=2000ms bpm=120)` | RECORDING → PLAYING (REC tap + bar-snap) | `closeRecording()` |
| `[LOOP] Bank N: Play (events=42)` | STOPPED → PLAYING | `startPlayback()` |
| `[LOOP] Bank N: Stop (events=42)` | PLAYING → STOPPED | `stopPlayback()` |
| `[LOOP] Bank N: Waiting play` | STOPPED → WAITING_PLAY (quantize BEAT/BAR) | `armPlayQuantized()` |
| `[LOOP] Bank N: Waiting stop` | PLAYING → WAITING_STOP | `armStopQuantized()` |
| `[LOOP] Bank N: Overdub start (base=42)` | PLAYING → OVERDUBBING | `startOverdub()` |
| `[LOOP] Bank N: Overdub merged (42+7=49)` | OVERDUBBING → PLAYING (post-merge) | `mergeOverdub()` |
| `[LOOP] Bank N: Cleared` | * → EMPTY (CLEAR pad tenu, `_clearFired` armé) | `clear()` |

**Invariant à tenir** : exactement une émission par transition. Risque de double-emit sur `WAITING_PLAY → PLAYING` (event B3 du plan) si la transition passe par deux setters → à auditer au moment de poser les callsites.

### §3.B `[STATE]` étendu pour bank LOOP

Avant les 8 slots dummy `R1=--- … R4H=---`, insérer les fields type-specific :

```
[STATE] bank=N mode=LOOP ch=N scale=…
  loopState=PLAYING eventCount=42 [waitingFor=BAR]
  R1=--- R1H=--- R2=--- R2H=--- R3=--- R3H=--- R4=--- R4H=---
```

- `loopState` ∈ `EMPTY|RECORDING|PLAYING|OVERDUBBING|STOPPED|WAITING_PLAY|WAITING_STOP` — chaîne lisible, pas l'enum int.
- `eventCount` **snapshot, pas live** : refresh aux transitions REC closed / OD merged / Cleared uniquement. Pas d'émission par event capturé.
- `waitingFor=BEAT|BAR` **omis** si `loopState` ≠ `WAITING_*`.
- Slots restent `---` (LOOP n'a pas de PotRouter binding aujourd'hui — sujet séparé si des pots LOOP arrivent un jour).

### §3.C `[BANK_SETTINGS]` étendu pour bank LOOP

Aujourd'hui, return tôt si `type != BANK_ARPEG_GEN`. Proposé : dispatch par type.

| Type | Format |
|---|---|
| `BANK_ARPEG_GEN` | `[BANK_SETTINGS] bank=N bonus=X margin=Y prox=Z ecart=W` (inchangé) |
| `BANK_LOOP` | `[BANK_SETTINGS] bank=N quantize=FREE\|BEAT\|BAR` |
| Autres | no-op (inchangé) |

Choix d'exclusion délibérés :

- **`channel`** : trivialement = `bankIdx + 1` ([plan §décisions L75](../../archive/2026-05-18-loop-phase-2-plan.md)). Le viewer le dérive — pas la peine de l'émettre.
- **`recordBpm`** : pas un setting persistant mais un état latché transient. Vit dans `[LOOP] REC capturing (bpm=…)` (event §3.A) et opt. dans `[STATE]` si besoin de le voir après cold connect.

L'émission est automatiquement ramassée par les boucles existantes du boot dump, `?ALL`, `?BOTH`, `?STATE` foreground ([`ViewerSerial.cpp:112, 506, 516`](../../../src/viewer/ViewerSerial.cpp)).

### §3.D `[LOOP_BUFFER_FULL]` — fermer le trou viewer

Émission firmware déjà prête ([`ViewerSerial.h:50-53`](../../../src/viewer/ViewerSerial.h), callsites prévus Phase 2 capture overflow + merge overflow). Format actuel :

```
[LOOP_BUFFER_FULL] ch=N which=main|overdub|merge
```

À ajouter côté viewer (worktree `viewer-juce`) :

1. Branche dans `RuntimeParser::parseLine()` (split sur `ch=` / `which=`).
2. Variant `ParsedEvent::LoopBufferFullEvent { uint8_t channel; LoopBufferKind which; }`.
3. Handler `Model::apply()` — toast / badge bank, non bloquant (PRIO_LOW = drop firmware acceptable, c'est juste un diag de saturation).

### §3.E Hors-scope explicite — commandes write `!LOOP_*`

Le canal `!KEY=VAL [BANK=N]` ([`ViewerSerial.cpp:267-315`](../../../src/viewer/ViewerSerial.cpp)) supporterait sans coût structurel :

- `!LOOP_QUANTIZE=BAR BANK=N` — analogue à `!BONUS`, handler + `queueBankTypeFromCache` + emit `[BANK_SETTINGS]` post-write.
- `!LOOP_CLEAR BANK=N` / `!LOOP_REC BANK=N` / `!LOOP_PLAY BANK=N` — transport remote.

**Non couvert par cet addendum.** Justifications :

- L'addendum répond à "alimenter le viewer" (read), pas à "piloter l'instrument depuis le viewer" (write).
- `LOOP_QUANTIZE` est aujourd'hui un setting setup-mode-only (Tool dédié, contraint par le pattern setup mode boot-only du `CLAUDE.md` projet). L'ouvrir live demande une décision séparée sur la philosophie (live vs setup-only) qui dépasse le protocole.
- Le transport `REC/PLAY/CLEAR` est une action physique (pads dédiés). L'exposer via write ouvre la porte à des conflits d'état avec les pads — design distinct, hors périmètre.

À ouvrir éventuellement en Phase 3 ou design ultérieur dédié.

## §4 Récap des `emit_*` à ajouter côté firmware

```cpp
// src/viewer/ViewerSerial.h — ajouts dans namespace viewer

// Transitions LoopEngine — toutes PRIO_HIGH.
void emitLoopRecArmed(uint8_t bankIdx);
void emitLoopRecCapturing(uint8_t bankIdx, float bpm);
void emitLoopRecClosed(uint8_t bankIdx, uint16_t eventCount, uint32_t durMs, float bpm);
void emitLoopPlay(uint8_t bankIdx, uint16_t eventCount);
void emitLoopStop(uint8_t bankIdx, uint16_t eventCount);
void emitLoopWaitingPlay(uint8_t bankIdx);
void emitLoopWaitingStop(uint8_t bankIdx);
void emitLoopOverdubStart(uint8_t bankIdx, uint16_t baseCount);
void emitLoopOverdubMerged(uint8_t bankIdx, uint16_t baseCount, uint16_t addedCount);
void emitLoopCleared(uint8_t bankIdx);

// emitLoopBufferFull(uint8_t, const char*)  — existe déjà, parser viewer à ajouter.
// emitBankSettings(uint8_t)                 — étendre dispatch type (variante LOOP).
// emitState(uint8_t)                        — étendre branche BANK_LOOP (loopState, eventCount, waitingFor).
```

Toutes les transitions §3.A émettent une seule ligne PRIO_HIGH. Pas de PRIO_LOW haute fréquence introduit (cf §6 décision 3).

## §5 Travail côté viewer JUCE (worktree `viewer-juce`)

1. **`RuntimeParser`** : ~10 nouvelles branches (1 par event `[LOOP]` §3.A) + 1 branche `[LOOP_BUFFER_FULL]` §3.D + extension du parser `[STATE]` et `[BANK_SETTINGS]` pour décoder `loopState=`, `quantize=`, `eventCount=`, `waitingFor=`.
2. **Variants `ParsedEvent`** : nouveaux variants pour chaque transition `[LOOP]` (analogue à `ArpEvent::Action`) + `LoopBufferFullEvent`. Le bloc LOOP dans `[STATE]` se range dans `StateEvent` (extension de la struct, pas nouveau variant).
3. **Model** :
   - `BankInfo` : ajouter `LoopState loopState`, `uint16_t eventCount`, `LoopQuantize quantize`, opt. `float recordBpm`, opt. `LoopQuantize waitingFor`.
   - `Model::apply()` : handlers pour chaque variant LOOP (cf. handlers `[ARP]` existants L160-179 du parser comme template).
4. **Enums** : `enums/LoopState.h`, `enums/LoopQuantize.h`, `enums/LoopBufferKind.h`.
5. **UI** : LED / badge par bank LOOP — rendu basé sur `loopState`. Analogue au `playing=true/false` ARPEG mais avec 7 états à représenter (les WAITING_* méritent probablement un clignotement).

## §6 Décisions à trancher avant intégration

1. **Inclure ces events dans le plan Phase 2 actuel, ou déférer Phase 3 ?**
   - *Phase 2* : ~10 callsites à poser dans `LoopEngine.cpp`, audit "une émission par transition", parser viewer à étendre dans le même cycle. Le LOOP devient visible viewer dès la sortie Phase 2.
   - *Phase 3* : laisse Phase 2 livrer engine + LED firmware seul, viewer aveugle au LOOP. Pousse l'addendum + parser dans une phase dédiée.
   - **Trade-off** : sortir Phase 2 sans visibilité viewer = régression UX (les autres modes sont visibles). Mais ajouter ~20 lignes de patch au plan en cours = risque de re-test cascade.
2. **Dupliquer `loopState` dans `[STATE]` ET `[LOOP]` ?**
   - *Oui (recommandé)* — parité avec ARPEG_GEN (`mutationLevel` dans `[STATE]` ET `[ARP_GEN]` dédié). Permet la reconstruction d'état au cold connect / `?STATE` sans dépendre du stream.
   - *Non* — économie de bytes, mais un viewer qui se connecte à chaud sur une bank LOOP en PLAYING ne sait pas qu'elle joue tant qu'aucune transition n'arrive.
3. **`eventCount` snapshot ou live ?**
   - *Snapshot (recommandé, dans la proposition)* — refresh aux 3 transitions structurelles. Pas de pollution stream.
   - *Live* — émission à chaque pad capturé, ~100 ev/s peak en jam. PRIO_LOW droppable, mais saturation queue + bruit terminal debug. À éviter sauf besoin UX explicite (barre de progression visuelle, etc).
4. **`channel` dans `[BANK_SETTINGS]` LOOP ?**
   - Pour : explicite, robuste à un futur dé-couplage `channel` ↔ `bankIdx`.
   - Contre : redondant aujourd'hui, bytes inutiles. Proposition actuelle = omettre.
5. **`!LOOP_*` write commands (§3.E) — Phase 3 ou hors-projet ?**
   - À trancher séparément, hors scope de l'addendum.

## §7 Intégration suggérée

Pour éviter le risque (1) ci-dessus, deux options de découpage :

- **Option α — patch au plan Phase 2** : ajouter une "Phase 2.K — Viewer protocol extension" en queue de plan, après les phases déjà séquencées. Coût : ~1 commit dans le plan + ~1 phase d'implémentation. Garde le LOOP visible viewer à la sortie.
- **Option β — nouveau plan dédié post-Phase 2** : `plans/YYYY-MM-DD-viewer-serial-loop-extension-plan.md`, séparé. Coût : 1 cycle plan/code/test en plus. Découple le risque.

Décision à prendre par Loïc avant d'attaquer le code.

## §8 Notes de cross-référence à poser ailleurs

Si cet addendum est validé :

- ~~Ajouter pointeur depuis le plan Phase 2 LOOP vers ce doc~~ **N/A 2026-05-19** : Phase 2 LOOP CLOSE, plan archivé ([`archive/2026-05-18-loop-phase-2-plan.md`](../../archive/2026-05-18-loop-phase-2-plan.md)). Pas de pointeur ajouté rétroactif.
- Ajouter pointeur depuis [`specs/2026-05-17-viewer-serial-centralization-design.md`](./2026-05-17-viewer-serial-centralization-design.md) (section "extensions futures") vers ce doc.
- Une fois implémenté, **promouvoir le contenu §3 en `docs/reference/viewer-protocol.md`** (référence canonique du protocole) — pour l'instant ce doc reste une proposition.
