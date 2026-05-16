# ILLPAD48 V2 — GestureDispatcher : refonte du geste LEFT-held

**Date** : 2026-04-26
**Statut** : DRAFT pour validation. Aucun code modifié à ce jour.
**Scope** : unification du traitement des pads sous LEFT-held + hold pad ARPEG dans une fonction dispatcher déterministe. Élimination structurelle des fragilités identifiées dans l'audit du 2026-04-26 (F1–F7).
**Pré-requis** : aucun (le refonte ne dépend pas de LOOP Phase 1, mais la prépare).
**Sources** :
- Audit `2026-04-26` (cette session) — symptômes terrain et findings F1–F7.
- État du code `main` au 2026-04-26 (commits `738b640` et antérieurs).
- [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](2026-04-19-loop-mode-design.md) — spec LOOP, §17/§19 alignement requis.
- [`docs/reference/runtime-flows.md`](../../reference/runtime-flows.md) — flux pad → MIDI / arp / bank switch.
- [`src/managers/BankManager.cpp`](../../../src/managers/BankManager.cpp), [`src/managers/ScaleManager.cpp`](../../../src/managers/ScaleManager.cpp), [`src/managers/ControlPadManager.cpp`](../../../src/managers/ControlPadManager.cpp), [`src/arp/ArpEngine.cpp`](../../../src/arp/ArpEngine.cpp), [`src/main.cpp`](../../../src/main.cpp).

---

## Partie 1 — Cadre

### §1 — Intention musicale

Le mécanisme actuel "LEFT held + tap bank pad" est éclaté en **trois managers indépendants** qui se coordonnent par état partagé (`s_lastKeys`, booléen `leftHeld`) :

- `BankManager` : edge bank pads, double-tap, pendingSwitch.
- `ScaleManager` : edge root/mode/chrom/octave pads.
- `ControlPadManager` : edge control pads, gating LEFT, handoff bank-switch.
- `handleHoldPad()` (main.cpp) : edge hold pad, indépendant de LEFT.

Cette dispersion produit en pratique des comportements **non-déterministes** sous geste long enchaîné (par exemple : `LEFT held` → `stop BG6` → `start BG5` → `switch B7` → `change root` → `switch FG8` → `release LEFT`). Symptômes observés et tracés dans l'audit du 2026-04-26 :

1. Le double-tap `play/stop` sur la bank FG **vide silencieusement la pile** ARPEG.
2. Un 2e tap arrivant à `_doubleTapMs` exact (boundary) **ré-arme un nouveau pendingSwitch** au lieu de toggle play/stop.
3. Un 2e tap "doux" non perçu comme rising edge **commit un bank switch** au lieu d'un play/stop.
4. Un release de LEFT laisse les pads pressés **actifs** sur la nouvelle FG (ARPEG-ON non sweepé).

Le présent document spécifie la refonte sous forme d'une **fonction dispatcher unique** (`GestureDispatcher`) qui assume la responsabilité de toutes les actions sous LEFT-held + hold pad, élimine la coordination implicite entre managers, et rend chaque action déterministe.

### §2 — Posture de design

- **Pas de journal**, pas de transactions au release. Chaque action **commit immédiatement** dans la frame où elle est détectée (Q1).
- **Une seule fonction** `GestureDispatcher::update()`, séquence linéaire de phases A→F sans appels croisés.
- **Pile sacrée** : aucune action utilisateur ne vide la pile ARPEG par effet de bord. Seule une action explicite (futur : reset banque, ou geste dédié) peut vider la pile (Q3).
- **Timer global** pour la détection double-tap : un seul `_lastTapPad` + `_lastTapTime`, partagé pour toutes les catégories (Q4).
- **Zones mortes** au press LEFT, au release LEFT, et après un double-tap (Q3, Q7) — absorbent les artefacts d'intention.
- **Hooks d'extension explicites** pour LOOP Phase 1+ : pré-commit bank switch (LoopEngine peut intercepter), classification slot pad (long-press vs short-press), combo CLEAR+slot (multi-pad ordonné).

### §3 — Périmètre

**Inclut** :
- Détection edge LEFT (press / release).
- Détection edge pads sous LEFT.
- Classification pads par rôle dans le contexte courant (bank type FG, customisation Tool 3).
- Tap classifier global (single / double).
- Dispatch d'action vers les sous-systèmes (BankManager.switchToBank, ArpEngine.setCaptured, MidiEngine.setScale, futur LoopEngine.toggle).
- Sweep universel des pads pressés au release LEFT.
- Détection long-press (préparation slot save LOOP §11).
- Hooks combo (préparation slot delete LOOP §13).
- Gating control pads pendant LEFT-held.
- Gating hold pad pendant zones mortes LEFT.

**Exclut** (inchangé après refonte) :
- Pipeline capacitif (`CapacitiveKeyboard.cpp`, marqué DO NOT MODIFY).
- Exécution musicale (`ArpEngine`, `ArpScheduler`, `MidiEngine`, futur `LoopEngine`).
- LedController (le dispatcher *appelle* `triggerEvent()` aux mêmes endroits que les managers actuels — la grammaire LED reste intacte).
- Pipeline pots (`PotRouter`, `PotFilter`).
- NVS (`NvsManager`).
- Double buffer Core 0 / Core 1 ([F4 dans l'audit](../../../src/main.cpp:39) — traité indépendamment, voir §16).

---

## Partie 2 — Règles de geste consolidées

### §4 — Catégories de pads

Une catégorie est attribuée à chaque pad pour chaque frame, selon l'état (`leftHeld`, `bankFG.type`, customisation Tool 3 / Tool 4). Les catégories sont **mutuellement exclusives** dans une frame.

| Catégorie | Quand | Source |
|---|---|---|
| `BANK_PAD` | LEFT held, pad ∈ bankPads (Tool 3 sub-page Banks) | hold-left layer |
| `SCALE_PAD` | LEFT held, pad ∈ rootPads ∪ modePads ∪ {chromaticPad} | hold-left layer |
| `OCTAVE_PAD` | LEFT held, pad ∈ octavePads, FG = ARPEG/ARPEG_GEN | hold-left layer |
| `HOLD_PAD` | pad == holdPad (FG ARPEG/ARPEG_GEN) | indépendant LEFT (Q5) |
| `LOOP_SLOT_PAD` | LEFT held, FG = LOOP, pad ∈ slotPads (Tool 3 sub-page LOOP) | hold-left layer (LOOP §5) |
| `LOOP_CTRL_PAD` | LEFT non-held, FG = LOOP, pad ∈ {recPad, playStopPad, clearPad} | musical layer (LOOP §5) |
| `CONTROL_PAD` | LEFT non-held, pad ∈ Tool 4 control pads | musical layer |
| `MUSIC_PAD` | LEFT non-held, pad ∉ ci-dessus | musical layer (note via ScaleResolver) |
| `IGNORED` | LEFT held, pad ∉ ci-dessus | hold-left layer (pads "neutres") |

Le sous-système `LOOP_SLOT_PAD` n'existe en runtime qu'à partir de LOOP Phase 1 (struct LoopPadStore + Tool 3 sub-page LOOP). Avant : la catégorie est simplement absente.

**Règle de collision pad** : aucun pad physique ne peut porter deux rôles **hold-left** simultanément. En particulier, `holdPad ∉ bankPads ∪ scalePads ∪ octavePads`. La validation est appliquée **au Tool 3** (boot-only), pas au runtime — cohérent avec l'invariant "Setup mode = boot-only par construction" (CLAUDE.md projet). Conséquence : le dispatcher n'a **pas** à arbitrer une collision runtime entre catégories de la même couche hold-left. Si une config NVS invalide remontait (ex. import depuis une version antérieure), `validateBankPadStore` / `validateScalePadStore` / `validateArpPadStore` doivent rejeter et appliquer les défauts (Zero Migration Policy).

**Cross-layer autorisé** (cf. LOOP spec §5) : un pad peut être à la fois rôle hold-left (slot pad LOOP) et rôle musical (ControlPad CC). Les deux comportements coexistent sans collision parce qu'ils sont séparés par `leftHeld`.

### §5 — Primitives de geste

5 primitives reconnues par le dispatcher :

| Primitive | Définition | Catégories qui la consomment |
|---|---|---|
| `TAP_SINGLE` | rising edge isolé | BANK_PAD, SCALE_PAD, OCTAVE_PAD, HOLD_PAD, LOOP_SLOT_PAD (court), LOOP_CTRL_PAD |
| `TAP_DOUBLE` | 2e rising edge sur le même pad dans `doubleTapMs`, hors zone morte post-double-tap | BANK_PAD (ARPEG/LOOP), LOOP_CTRL_PAD (PLAY/STOP) |
| `LONG_PRESS` | rising edge maintenu > seuil sans release | LOOP_SLOT_PAD (save), LOOP_CTRL_PAD (CLEAR loop) |
| `SHORT_PRESS` | rising + falling edges < seuil short_press | LOOP_SLOT_PAD (load) |
| `COMBO` | rising edge B alors que pad A est encore tenu, ordre imposé | LOOP `CLEAR + slot` (LOOP §13) |

**Règles de priorité** quand plusieurs primitives sont candidates pour un même pad :
1. `COMBO` > tout (si pad de tête du combo est actif).
2. `TAP_DOUBLE` > `TAP_SINGLE` (sur la 2e rising edge).
3. `LONG_PRESS` > `SHORT_PRESS` (le long press s'engage par durée, le short press s'engage au falling edge).
4. **`COMBO_LEAD` suspend `LONG_PRESS` sur le même pad** : tant qu'un pad est lead d'un combo armé, sa propre `LONG_PRESS` ne fire pas. Cas concret : pad CLEAR LOOP a une `LONG_PRESS` (wipe buffer à `clearLoopTimerMs`) ET peut être lead du combo CLEAR+slot (delete slot, LOOP §13). Tant que CLEAR est tenu en attente d'un slot pad, le wipe ne fire pas. Si le user relâche CLEAR avant de presser un slot, le wipe peut alors fire (à condition d'avoir atteint la durée). Si le user presse un slot pendant le hold, le combo se déclenche et la `LONG_PRESS` du lead est définitivement annulée jusqu'au release.

### §6 — Constantes de timing

**Constantes compile-time** (internes au dispatcher, non exposées Tool 6) :

```
LEFT_EDGE_GUARD_MS         = 40 ms       (Q3) zone morte au press / release LEFT
DOUBLE_TAP_DEAD_MS         = 200 ms      (Q7) zone morte post-double-tap consommé
SHORT_PRESS_MIN_MS         = 300 ms      (LOOP §12 — < 300ms = silent ignore)
SHORT_PRESS_MAX_MS         = 1000 ms     (LOOP §12 borne haute load slot)
```

**Constantes runtime** (lues depuis `SettingsStore` v11, déjà persistées, éditables Tool 6) :

```
doubleTapMs        ∈ [100, 250]  default 200ms — fenêtre double-tap globale
slotSaveTimerMs    ∈ [500, 2000] default 1000ms — durée LONG_PRESS sur slot pad LOOP (save)
clearLoopTimerMs   ∈ [200, 1500] default 500ms  — durée LONG_PRESS sur pad CLEAR LOOP (wipe)
slotClearTimerMs   ∈ [400, 1500] default 800ms  — durée animation delete slot (LED only, pas user-engagement)
```

**Conséquence pour l'implémentation** : le dispatcher ne peut **pas** stocker une seule constante `LONG_PRESS_MS`. La primitive `LONG_PRESS` est paramétrée par sa **cible** :
- `LOOP_SLOT_PAD` → seuil = `slotSaveTimerMs`
- `LOOP_CTRL_PAD` (CLEAR) → seuil = `clearLoopTimerMs`
- Aucune autre catégorie n'expose `LONG_PRESS` aujourd'hui.

Le dispatcher lit ces deux valeurs depuis `SettingsStore` à chaque boot (Phase 7 du plan), et offre un setter pour mise à jour live si le user édite Tool 6 hors setup-mode (en théorie pas possible vu que Tool 6 est boot-only — cohérent avec invariant 7 "Setup/Runtime coherence").

**Note source de vérité** : `slotSaveTimerMs` / `clearLoopTimerMs` ont leur chaîne 4-link complète (Runtime ↔ Store ↔ Tool ↔ NVS). Ne pas dupliquer en compile-time.

### §7 — Zones mortes (les "gardes")

**Garde LEFT** (Q3) :
- `t_leftEdge` = `now` à chaque rising/falling edge LEFT.
- Pendant `[t_leftEdge, t_leftEdge + LEFT_EDGE_GUARD_MS]`, **toute rising edge pad est ignorée**, dans toutes les catégories.
- Au release LEFT, en plus de la garde : sweep universel (§9).
- Au press LEFT : pas de sweep, mais le `_padLast[]` interne est snapshoté pour que les pads déjà pressés soient connus comme "présents avant le LEFT" et exclus de leur prochain rising edge effectif.

**Garde double-tap** (Q7) :
- Quand un double-tap est consommé, `_doubleTapDeadUntil = now + DOUBLE_TAP_DEAD_MS`.
- Pendant cette garde, **toute rising edge sur le pad qui vient d'être double-tappé est ignorée** (pas seulement tap classifier — l'edge entier est swallow). Sur les autres pads, comportement normal.
- Évite que la 3e frappe d'une triple-frappe involontaire devienne un nouveau "1er tap" qui arme un switch.

**Conséquence pratique** : un double-tap rapide est lu comme exactement deux taps. La 3e frappe d'une frappe nerveuse est éteinte. Le user n'a plus à "se retenir" après un double-tap.

### §8 — Tap classifier (timer global)

État interne minimal :
- `uint32_t _lastTapTime` (init 0)
- `uint8_t  _lastTapPad`  (init 0xFF)

Logique sur rising edge filtrée (passée par les gardes §7) d'un pad `i` candidat double-tap (catégories BANK_PAD ARPEG/LOOP, LOOP_CTRL_PAD PLAY/STOP) :

```
if (_lastTapPad == i && now - _lastTapTime < DOUBLE_TAP_MS) {
    primitive = TAP_DOUBLE
    _doubleTapDeadUntil = now + DOUBLE_TAP_DEAD_MS
    _lastTapPad = 0xFF                  // reset, évite triple-tap
    _lastTapTime = 0
} else {
    primitive = TAP_SINGLE
    _lastTapPad = i
    _lastTapTime = now
}
```

Pour les catégories qui ne consomment pas le double-tap (SCALE_PAD, OCTAVE_PAD, HOLD_PAD, CONTROL_PAD), la rising edge est traitée immédiatement comme `TAP_SINGLE` sans toucher au tap classifier.

**Détection d'intent "switch vers une autre bank"** : si une rising edge BANK_PAD est `TAP_SINGLE` et que `b != currentBank`, on arme un pendingSwitch. La fenêtre d'attente du 2e tap est `DOUBLE_TAP_MS`. Si le 2e tap n'arrive pas, le pendingSwitch commit. Si un 2e tap sur le même pad arrive dans la fenêtre, il devient `TAP_DOUBLE`, le pendingSwitch est annulé, et le toggle play/stop est exécuté à la place.

**Détection d'intent "play/stop sur la FG"** : si une rising edge BANK_PAD est `TAP_SINGLE` et que `b == currentBank`, **aucun pendingSwitch n'est armé** (pas de switch vers soi-même), mais le timer tap est bien armé pour potentiellement détecter un `TAP_DOUBLE` derrière. Le 1er tap sur la FG bank pad est silencieux.

### §9 — Sweep universel au release LEFT (Q3)

À l'edge falling LEFT, avant l'application de la garde, on sweep tous les pads selon le type de la FG bank.

**Principe directeur** : aucun sweep ne **retire** de notes/pile en automatique. Le rôle du sweep est strictement de **resynchroniser `_lastKeys`** pour que la frame suivante ne voie pas de rising/falling edges artificiels en mode pad input. Aucun appel `removePadPosition`, aucun appel `noteOff`, aucun appel destructif.

| FG type | Action sur pad pressé au release LEFT | Action sur pad NON pressé au release LEFT |
|---|---|---|
| `BANK_NORMAL` | `_lastKeys[i] = true` (snapshot — évite faux rising edge si pad reste pressé après LEFT release) | `_lastKeys[i] = false` (rien à faire, pas de noteOff sweep) |
| `BANK_ARPEG` / `BANK_ARPEG_GEN`, captured=false | `_lastKeys[i] = true` snapshot — pad ignoré, pas d'addPadPosition au prochain frame | `_lastKeys[i] = false` — **AUCUN `removePadPosition`** (pile sacrée Q3) |
| `BANK_ARPEG` / `BANK_ARPEG_GEN`, captured=true | `_lastKeys[i] = true` snapshot | `_lastKeys[i] = false`, pile inchangée |
| `BANK_LOOP` (futur) | `_lastKeys[i] = true` snapshot | `_lastKeys[i] = false` ; les LOOP_CTRL_PAD non pressés ne déclenchent rien |

**Précédent destructif supprimé (commit fix `fix(arpeg)` du 2026-05-15)** : la branche `else if (isArpType && !captured)` de `handleLeftReleaseCleanup()` itérait sur les 48 pads et appelait `removePadPosition(s_padOrder[i])` pour chaque pad non pressé physiquement. Conséquence : à chaque cycle LEFT press/release **sans** que l'utilisateur tienne les pads de la pile, la pile entière était vidée. Bug diagnostiqué via log timestampé montrant les `-note` cascade dans la même milliseconde que `[BTN] LEFT release`. Fix : suppression du `else if`. Le dispatcher reproduira ce comportement corrigé.

Le sweep + snapshot remplace donc trois mécanismes existants après refonte gesture :
- [`BankManager._switchedDuringHold` + memcpy `_lastKeys`](../../../src/managers/BankManager.cpp:146) (BankManager.cpp:146-150) — préservé comme snapshot non-destructif
- [`handleLeftReleaseCleanup()` branche NORMAL](../../../src/main.cpp:566) (main.cpp:566-588 actuel) — préservé comme noteOff sweep pour NORMAL uniquement
- ~~`handleLeftReleaseCleanup()` branche ARPEG-OFF~~ — **supprimé** dans le fix `2026-05-15`, ne pas réintroduire
- [`ScaleManager` memcpy `_lastKeys` au release](../../../src/managers/ScaleManager.cpp:91) (ScaleManager.cpp:91-93) — préservé comme snapshot non-destructif

Tous fusionnés dans `sweepAtRelease()` du dispatcher.

**Garantie** : peu importe le geste précédent, après un release LEFT propre, la pile ARPEG est exactement dans l'état où elle était au press LEFT. Cohérent invariant 8 "pile sacrée".

### §10 — Hold pad ARPEG (Q5) + scope modifier LEFT

Le hold pad reste actif **indépendamment** du LEFT, conformément à ta décision Q5 ("le hold pad est toujours un hold pad, quelque soit l'état du bouton left"). Le `leftHeld` agit comme **modifier de scope** sur le geste :

| Geste | Scope | Effet |
|---|---|---|
| Hold pad rising edge, `!leftHeld` | FG seule | Toggle Play/Stop de la bank FG (comportement de référence) |
| Hold pad rising edge, `leftHeld` | Toutes banks ARPEG (futur LOOP) | Toggle global symétrique — cf §10.1 ci-dessous |

Conséquences architecturales :
- Le hold pad rising edge **n'est pas filtré** par la garde LEFT (§7). Il déclenche toujours, sans délai, peu importe le scope.
- Pendant la garde LEFT, les autres rising edges sont swallow, mais pas le hold pad.

**Note** : si le user presse hold pad pile dans la fenêtre `LEFT_EDGE_GUARD_MS` du press LEFT, l'action s'applique avec le scope correspondant au `leftHeld` du moment. Aucune ambiguïté.

**Pile sacrée** (Q3) : le toggle setCaptured() depuis le hold pad ne wipe **jamais** la pile, **même si des fingers sont down**. C'est un changement par rapport au code actuel ([ArpEngine.cpp:540-545](../../../src/arp/ArpEngine.cpp:540)) qui appelle `clearAllNotes()` dans la branche `anyFingerDown`. Cette branche **disparaît** de `setCaptured()`. Voir §13.

#### §10.1 — Toggle global (LEFT + hold pad simple tap, amendement 2026-05-15)

**Sémantique** : toggle symétrique sur **toutes les banks ARPEG / ARPEG_GEN** (et futur LOOP) selon leur état collectif.

| État collectif | Effet |
|---|---|
| Au moins une bank en Play (`isCaptured()==true`) | Stop sur **toutes** les banks en Play. Pile préservée (paused) sur chacune. LED `EVT_STOP` une fois avec mask multi-bank. |
| Toutes en Stop, au moins une avec `_pausedPile && hasNotes()` | Play sur **toutes** les banks éligibles (relaunch). LED `EVT_PLAY` une fois avec mask multi-bank. |
| Toutes en Stop, aucune paused pile non vide | No-op silencieux (rien à reprendre). |

**Implémentation actuelle** (signature `setCaptured` 4 args, pré-refonte) :

```cpp
static void toggleAllArps() {
  bool anyPlaying = false;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (isArpType(s_banks[i].type) && s_banks[i].arpEngine
        && s_banks[i].arpEngine->isCaptured()) { anyPlaying = true; break; }
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

**Après refonte gesture Phase 4** (signature `setCaptured` 2 args + dispatcher catégorisé) :
- Le geste devient une primitive dispatcher : `(HOLD_PAD, TAP_SINGLE, leftHeld=true)` → handler `toggleAllArps()`.
- `setCaptured(false, transport)` (sans keyIsPressed) puisque pile sacrée stricte.
- Reste du code identique.

**Workflow musicien** :
- Setup live : 3 banks ARPEG remplies, en Play. Transition vers un break-down.
- Geste : LEFT + tap hold pad → silence net sur les 3 banks, piles préservées.
- Plus tard pour reprendre tout en simultané : LEFT + tap hold pad → relaunch des 3 piles simultané.
- Si user veut reprendre seulement la bank FG : tap hold pad sans LEFT → toggle FG uniquement, les BG restent en Stop.

**Cohérence avec Q3** : aucune pile n'est wipée par ce geste. La règle "pile sacrée" est respectée intégralement. Pour vider une bank, le user repasse en FG et utilise le geste musical §13.2 (press musical en Stop) ou les double-taps remove individuels.

**Extension LOOP futur** : le helper itère sur les banks `isLoopType` aussi, appelle `LoopEngine.toggleAll()` ou équivalent. Spec figée Phase 7 du plan gesture (signature `LoopEngine`).

### §11 — Scale change pendant pendingSwitch (Q8)

Ta règle Q8 : "le changement de scale ou de root se fait uniquement sur la banque en cours".

**Définition opérationnelle** : la "bank en cours" est la bank actuellement **FG**, c'est-à-dire celle dont `isForeground == true`. Un pendingSwitch armé n'a pas encore commit : la bank cible n'est pas FG. Donc un scale change émis pendant la fenêtre pending s'applique à la **bank avant le switch**.

Ce comportement est déjà celui du code actuel (cf. audit). À documenter explicitement dans la spec pour qu'il ne soit pas perçu comme un bug. Si tu veux le changer plus tard ("scale s'applique à la bank cible du pending"), la modification se fait dans la Phase E de §15, sans casser l'archi.

### §12 — Hiérarchie des actions par frame (Q2)

Au plus **1 action par catégorie** émise par le dispatcher dans une frame :

| Catégorie | Action max par frame |
|---|---|
| BANK_PAD | 1 (le 1er rising edge de la frame ; les autres sont ignorés) |
| SCALE_PAD | 1 (root XOR mode XOR chrom — le 1er trouvé) |
| OCTAVE_PAD | 1 |
| HOLD_PAD | 1 |
| LOOP_SLOT_PAD | 1 |
| LOOP_CTRL_PAD | 1 |
| CONTROL_PAD | n (cohérent avec le code actuel — chaque control pad est indépendant) |
| MUSIC_PAD | n (chaque note est indépendante) |

**Multi-catégorie** : un scale change (SCALE_PAD) et un bank tap (BANK_PAD) dans la même frame **s'exécutent tous les deux**. Pas de mutex inter-catégorie. C'est cohérent avec la rare réalité physique d'un tel double event simultané, et préserve la liberté du user d'exécuter une combo (e.g., un doigt sur scale, un autre sur bank).

### §13 — Pile sacrée — modifications dans setCaptured() (Q3)

**Définition opérationnelle de "pile sacrée"** : la pile ARPEG ne peut **jamais** être vidée par un effet de bord d'un **geste de transport** (bank pad double-tap, hold pad, edge LEFT, pads pressés aux gardes). Elle peut être vidée uniquement par une **intention engagée du user** sur le layer musical (voir §13.2).

#### §13.1 — Modification de `ArpEngine::setCaptured()`

Le contrat de `ArpEngine::setCaptured()` est révisé. **Fix appliqué hors refonte le 2026-05-15** (commit dédié `fix(arpeg): F1`), la spec ici décrit l'état actuel du code et le reste à faire en Phase 5 de la refonte.

**AVANT** (ancien code, supprimé) :
```
captured=false: if (anyFingerDown) clearAllNotes()  ← chemin destructif (F1)
                else { flush noteOffs ; _playing = false ; _pausedPile = true }
```

**APRÈS (état actuel du code, fix 2026-05-15)** ([ArpEngine.cpp:544-558](../../../src/arp/ArpEngine.cpp:544)) :
```
captured=true:  if (_pausedPile && positionCount > 0) relaunch ; _pausedPile = false
captured=false: flush noteOffs ; _playing = false ; _pausedPile = true
                  ← peu importe les fingers down, la pile est préservée
```

Conséquences :
- F1 résolu : le bank pad pressé pour le 2e tap ne peut plus déclencher un wipe.
- Hold pad sur FG ARPEG en mode capturé, avec d'autres pads pressés → pile préservée. Conforme à la règle Q3.
- F7 (asymétrie BG/FG dans setCaptured) résolue : les paramètres `keyIsPressed` et `holdPadIdx` deviennent inutiles, marqués `(void)` pour silencer warnings. La refonte Phase 5 simplifiera la signature à :
  ```cpp
  void setCaptured(bool captured, MidiTransport& transport);
  ```
- Le sweep release universel (§9) prend le relais pour assurer qu'aucun pad pressé pendant un release LEFT n'ajoute de note à la pile ni ne déclenche de wipe (déjà appliqué hors refonte le 2026-05-15).

#### §13.2 — Auto-Play sur 1er press musical en Stop (Option 3, amendée 2026-05-15)

**Sémantique** : `_captured` n'est pas un mode de jeu persistant, c'est un toggle d'engine. Le Stop est une **commande momentanée** : couper le son maintenant. Toute interaction musicale post-Stop (press d'un pad de jeu) est interprétée comme un **re-engagement** : le moteur repart en Play automatiquement, et la pile précédente est wipée pour permettre une construction propre.

**Logique** :
1. 1er press musical en `!_captured` :
   - Si `_pausedPile && hasNotes()` → `clearAllNotes()` (wipe table rase).
   - `setCaptured(true)` (re-engage Play).
   - `triggerEvent(EVT_PLAY)` (feedback LED, cohérent avec hold pad et double-tap bank pad).
   - `addPadPosition(pos)` (ajoute la nouvelle note).
2. Press suivant en `_captured=true` : comportement Play standard (add ou double-tap remove).

**Code après refonte gesture Phase 5** (signature simplifiée `setCaptured(bool, MidiTransport&)`) :
```cpp
} else {
  if (slot.arpEngine->isPaused() && slot.arpEngine->hasNotes()) {
    slot.arpEngine->clearAllNotes(s_transport);
  }
  slot.arpEngine->setCaptured(true, s_transport);
  s_leds.triggerEvent(EVT_PLAY);   // feedback LED auto-Play
  slot.arpEngine->addPadPosition(pos);
}
```

**Code transitoire pré-refonte** (signature actuelle `setCaptured(bool, MidiTransport&, const uint8_t*, uint8_t)`, déjà appliqué dans le fix 2026-05-15) :
```cpp
slot.arpEngine->setCaptured(true, s_transport, nullptr, s_holdPad);
```

**Workflow utilisateur résultant** :

| Action | Effet |
|---|---|
| Play actif, pile pleine | arpège tourne |
| Toggle Stop (hold pad ou double-tap bank pad) | engine coupe net, pile préservée |
| Press pad musical après Stop | wipe pile + add nouvelle note + Play auto → arpège reprend avec la nouvelle pile |
| Toggle Play (hold pad / double-tap) après Stop sans presser de pad | relaunch la paused pile (cf §13.1) |

**Choix conscient** : le Stop n'a pas à être "défait" manuellement par le musicien. Le seul moyen de conserver la paused pile et reprendre la lecture est de toggle Play **avant** de toucher à un pad musical. Pour repartir from scratch, simplement presser un pad.

**Justification du wipe (Option 3 vs Option 1 "pile sacrée stricte")** : sans wipe, la pile devient immortelle — aucun moyen automatique de la vider, le musicien doit double-tap chaque note individuellement. Avec wipe sur press musical, on a un geste naturel "je presse → je repars propre" qui colle au sens musical du re-engagement.

#### §13.3 — Frontière conceptuelle "pile sacrée" vs "wipe engagé"

| Geste | Catégorie | Effet sur la pile |
|---|---|---|
| LEFT + double-tap bank pad (FG ou BG ARPEG) | transport | **préservée** (Q3, §13.1) |
| Hold pad press (avec ou sans fingers down) | transport | **préservée** (Q3, §13.1) |
| Pads pressés au press/release LEFT (zone de garde §7, §9) | transport | **préservée** (sweep snapshot, pas d'addPadPosition) |
| Bank switch (commit pendingSwitch) | transport | **préservée** (`switchToBank` ne touche pas la pile) |
| Panic global (BLE reconnect, triple-click rear) | système | **préservée** (`flushPendingNoteOffs` ne touche pas `_positionCount`) |
| 1er press pad musical en Stop avec paused pile | musical (re-engagement Play) | **wipée** + nouvelle note ajoutée + auto-Play (§13.2 Option 3) |
| 1er press pad musical en Stop sans paused pile | musical (re-engagement Play) | pile vide + nouvelle note ajoutée + auto-Play (§13.2 Option 3) |
| Double-tap remove sur pad musical en Play (FG ARPEG capturé) | musical (édition pile) | une position retirée (pas de wipe global) |

**Aucun autre chemin** ne wipe la pile dans le code post-refonte.

### §14 — Contrat avec LOOP spec (alignement)

Vérifications croisées avec [`2026-04-19-loop-mode-design.md`](2026-04-19-loop-mode-design.md) :

| LOOP §xx | Décision | Couverture par dispatcher |
|---|---|---|
| §17 (quantize, double-tap = bypass) | Tap simple PLAY/STOP suit `loopQuantize`, double-tap = immédiat. | Tap classifier §8 réutilisé sur LOOP_CTRL_PAD PLAY/STOP : émet `TAP_SINGLE` ou `TAP_DOUBLE`, le LoopEngine consomme la primitive. |
| §19 (LEFT + double-tap bank pad LOOP) | Symétrique ARPEG. Toggle play/stop LOOP. Pas de bank switch. | Branche dispatcher : `TAP_DOUBLE` BANK_PAD + `isLoopType(target)` → `LoopEngine.toggle()`. Pile/buffer sacré (cohérent avec Q3). |
| §11 (slot save long-press > 1s) | LONG_PRESS sur slot pad sous LEFT held, FG=LOOP. | Détection LONG_PRESS §5, primitive routée vers handler `onLoopSlotLongPress(padIdx)`. Annulation au release prématuré couvert par la machine d'états interne. |
| §12 (slot load short-press 300-1000ms) | SHORT_PRESS sur slot pad sous LEFT held, FG=LOOP. < 300ms ignoré. | Détection SHORT_PRESS §5, handler `onLoopSlotShortPress(padIdx)`. Filtre <300ms appliqué dans le dispatcher. |
| §13 (combo CLEAR + slot delete) | Multi-pad ordonné : CLEAR puis slot. | Primitive COMBO §5 : tracking d'un "lead pad" actif (CLEAR), puis rising edge sur slot pad pendant lead → handler `onLoopSlotDelete(padIdx)`. |
| §17 (WAITING_*, gestes concurrents) | Bank switch pendant WAITING_LOAD commit le load avant switch. | Hook **pré-commit bank switch** : avant `BankManager.switchToBank()`, si la bank source est LOOP, appel `LoopEngine.preBankSwitch()` qui peut commit immédiatement un load pending. |
| §18 (résolution rôles) | Bank pads sous LEFT > control pads > rôles contextuels > music pad. | Catégories §4 honorent cet ordre par construction. |
| §15 (LOOP_CTRL_PAD = layer musical, pas hold-left) | REC/PLAY-STOP/CLEAR jouent direct, sans LEFT. Pendant LEFT-held, ils sont gated comme music pads. | Catégorisation §4 : `LOOP_CTRL_PAD` n'existe que si `!leftHeld`. Sous LEFT-held, le pad redevient `IGNORED` (équivalent music pad gated). |

**Conflits potentiels** :
- Aucun bloquant. Les hooks pré-commit + handler par primitive permettent à LoopEngine d'ajouter sa logique Phase 1+ sans modifier le dispatcher.
- LOOP §13 ordre `CLEAR puis slot` est strict. Si le user inverse l'ordre, le slot est vu comme `LONG_PRESS` ou `SHORT_PRESS` standard (save/load). Comportement attendu par la spec LOOP. Aucune ambiguïté côté dispatcher.

### §15 — Phases A→F du dispatcher

Une frame du dispatcher est une séquence linéaire :

```
GestureDispatcher::update(state, leftHeld, now)
│
├─ Phase A — Edge LEFT
│   leftPress  = leftHeld && !_lastLeftHeld
│   leftRelease= !leftHeld &&  _lastLeftHeld
│   if leftPress   : _leftEdgeGuardUntil = now + LEFT_EDGE_GUARD_MS
│                    snapshot _padLast[i] = state.keyIsPressed[i]  pour exclure les pads "déjà pressés"
│   if leftRelease : sweep universel §9
│                    _leftEdgeGuardUntil = now + LEFT_EDGE_GUARD_MS
│                    reset _lastTapPad = 0xFF, _lastTapTime = 0,
│                          _lastBankPadPressTime[] = 0,
│                          _pendingSwitchBank = -1
│   _lastLeftHeld = leftHeld
│
├─ Phase B — Edge pads
│   for i in 0..NUM_KEYS-1 :
│     pressed_i = state.keyIsPressed[i]
│     rising_i  = pressed_i && !_padLast[i]
│     falling_i = !pressed_i && _padLast[i]
│     _padLast[i] = pressed_i
│
├─ Phase C — Filtrage gardes
│   for each rising_i :
│     if now < _leftEdgeGuardUntil  AND  i != _holdPad : drop
│     if i == _doubleTapGuardPad AND now < _doubleTapDeadUntil : drop
│   for each falling_i :
│     pas de garde — les fallings sont toujours traités
│     (utilisés pour LONG_PRESS commit, SHORT_PRESS commit, fin de combo)
│
├─ Phase D — Classification
│   for each rising_i passé les gardes :
│     category = classify(i, leftHeld, FG_type, customisations)
│     primitive = analyze(category, i, now, _comboLeadPad, _longPressTracking[i])
│
├─ Phase E — Dispatch action (1 action / catégorie / frame)
│   switch (category, primitive) :
│     (BANK_PAD, TAP_SINGLE)        → arm pendingSwitch (si b != current)
│                                      ou silent (si b == current)
│     (BANK_PAD, TAP_DOUBLE)        → cancel pendingSwitch
│                                      if isArpType(target)  : ArpEngine.setCaptured(toggle)
│                                      if isLoopType(target) : LoopEngine.toggle()
│                                      LedController.triggerEvent(EVT_PLAY/STOP, mask=1<<b)
│     (SCALE_PAD, TAP_SINGLE)       → apply root/mode/chrom sur current bank
│     (OCTAVE_PAD, TAP_SINGLE)      → setOctaveRange ou setMutationLevel selon EngineMode
│     (HOLD_PAD, TAP_SINGLE)        → ArpEngine.setCaptured(toggle) sur FG (pas de keys, pile sacrée)
│     (LOOP_SLOT_PAD, LONG_PRESS)   → LoopEngine.saveSlot(padIdx)         [LOOP Phase 1+]
│     (LOOP_SLOT_PAD, SHORT_PRESS)  → LoopEngine.loadSlot(padIdx)         [LOOP Phase 1+]
│     (LOOP_SLOT_PAD, COMBO_CLEAR)  → LoopEngine.deleteSlot(padIdx)       [LOOP Phase 1+]
│     (LOOP_CTRL_PAD, TAP_SINGLE)   → LoopEngine.tapPlayStop / REC / armCLEAR
│     (LOOP_CTRL_PAD, TAP_DOUBLE)   → LoopEngine.bypassQuantize           [LOOP §17]
│     (LOOP_CTRL_PAD, LONG_PRESS)   → LoopEngine.commitClear              [LOOP §11 CLEAR]
│     (CONTROL_PAD, *)              → délégation à ControlPadManager (logique inchangée hors gating)
│     (MUSIC_PAD, *)                → délégation au music block (processNormal/processArp)
│
└─ Phase F — Pending commit + LONG_PRESS commit + COMBO timeout
   if _pendingSwitchBank >= 0 AND now - _pendingSwitchTime >= DOUBLE_TAP_MS :
     pre-commit hook  (LoopEngine.preBankSwitch si type source LOOP)
     BankManager.switchToBank(target)
     _pendingSwitchBank = -1
   for each pad en LONG_PRESS_TRACKING :
     duration = (i == clearPad) ? _clearLoopTimerMs
              : (i in slotPads)  ? _slotSaveTimerMs
              : skip
     if combo_lead == i : skip  // règle §5 priorité 4
     if now - pressTime[i] >= duration :
       fire LONG_PRESS handler
       _longPressFired[i] = true
   COMBO :
     fin du combo = release du lead pad (pas de timeout temporel).
     Le user peut tenir CLEAR indéfiniment en attendant de viser le bon slot.
     Si CLEAR est tenu trop longtemps sans presser de slot, c'est de l'attention
     du user, pas une erreur — quand il relâche, le combo se désarme sans
     side effect (sauf si la `LONG_PRESS` du lead aurait eu le temps de fire,
     auquel cas elle a été suspendue par règle §5 priorité 4 et fire sur le
     release — à trancher dans D9 ci-dessous).
```

Le code de référence est dans le plan d'implémentation jumeau ([2026-04-26-gesture-dispatcher-plan.md](../plans/2026-04-26-gesture-dispatcher-plan.md)).

### §16 — Race Core 0 / Core 1 (F4)

Hors scope de cette refonte, mais **doit être adressé indépendamment** sous peine de réintroduire des fragilités de timing. La fix recommandée : remplacer la référence `state` par une copie locale en début de loop().

```cpp
// main.cpp loop() — début
SharedKeyboardState state;
{
  uint8_t idx = s_active.load(std::memory_order_acquire);
  state = s_buffers[idx];           // copy 96 bytes
}
```

Coût négligeable (~96 octets memcpy). Garantit qu'aucun manager (ni le dispatcher) ne voit le buffer muter en cours de frame. Ajout proposé dans la **Phase 0 du plan d'implémentation**.

---

## Partie 3 — Mapping symptômes → résolution

| Symptôme terrain | Finding audit | Cause profonde | Mécanisme dispatcher qui résout |
|---|---|---|---|
| Pile vidée silencieusement après double-tap stop FG | F1 | `setCaptured(false)` avec keys[bankPad]=true → wipe | §13 — branche `anyFingerDown` supprimée |
| Bank switch au lieu de stop (boundary 200ms) | F2 | wasRecent strict + re-arm sur 1st-tap path | §7 garde double-tap + §8 timer global avec dead-zone |
| Bank switch au lieu de stop (2e tap doux) | F3 | rising edge manqué sur 2e tap | §7 garde + §16 (race C0/C1) résolue ; pas de fix au pipeline capacitif |
| Rien ne se passe (FG, 2e tap >200ms) | F2 | wasRecent=false → continue silently | §8 — la 2e frappe `TAP_SINGLE` est bien enregistrée comme nouveau "1er tap" ; si la 3e arrive < DOUBLE_TAP_MS, elle devient `TAP_DOUBLE`. Comportement reste asymétrique pour FG (pas de switch vers soi-même) mais déterministe. Le user voit que rien ne se passe et peut taper à nouveau. |
| Timer leak inter-LEFT (faux double-tap après press LEFT) | F5 | `_lastBankPadPressTime` non reset au release LEFT | §15 Phase A — reset complet `_lastTapPad`, `_lastTapTime`, `_pendingSwitchBank` au release |
| Brossage de bank pad adjacent perturbe pendingSwitch | F6 | dernier rising edge gagne dans la frame | §12 — 1 action par catégorie par frame, le 1er rising BANK_PAD wins |
| Asymétrie API setCaptured BG/FG | F7 | exclusion `holdPadIdx` insuffisante | §13 — paramètre `keyIsPressed` supprimé de la signature |
| Pad pressé au release LEFT laisse une note sur ARPEG-ON | (observé, non numéroté dans audit) | pas de sweep ARPEG-ON dans handleLeftReleaseCleanup | §9 — sweep universel inclut ARPEG-ON |
| LOOP slot save long-press impossible | (futur, LOOP §11) | aucune détection long-press dans les managers actuels | §5 primitive `LONG_PRESS` + Phase F commit |
| LOOP combo CLEAR+slot impossible | (futur, LOOP §13) | aucun tracking multi-pad ordonné | §5 primitive `COMBO` + état `_comboLeadPad` |

---

## Partie 4 — Invariants

À conserver après refonte (vérification finale du plan d'implémentation) :

1. **No orphan notes** — toute note jouée a un noteOff. Conservé via flush dans `setCaptured(false)` (§13) et via [BankManager.switchToBank](../../../src/managers/BankManager.cpp:181) `allNotesOff()` inchangé.
2. **Arp refcount atomicity** — inchangé, ArpEngine non touché côté pile/refcount/scheduler.
3. **No blocking Core 1** — dispatcher est pure logique CPU, latence O(NUM_KEYS) = 48 itérations × few ops = négligeable.
4. **Core 0 never writes MIDI** — inchangé.
5. **Catch system** — inchangé, PotRouter pas touché.
6. **Bank slots always alive** — inchangé.
7. **Setup/Runtime coherence** — aucun nouveau Store NVS, aucune Tool modifiée. Constantes nouvelles compile-time.
8. **Pile sacrée** — **nouvelle invariante issue de Q3**. Aucun **geste de transport** (bank pad, hold pad, edge LEFT) ne wipe la pile par effet de bord. Le seul chemin de wipe automatique conservé est le 1er press d'un pad musical en Stop avec paused pile (§13.2) — c'est une intention engagée, pas un effet de bord.

---

## Partie 5 — Décisions ouvertes

À trancher avant le plan d'implémentation détaillé :

| ID | Question | Proposition par défaut | Décision |
|---|---|---|---|
| D1 | `LEFT_EDGE_GUARD_MS` valeur exacte | 40 ms | À valider en bench live |
| D2 | `DOUBLE_TAP_DEAD_MS` valeur exacte | 200 ms | À valider en bench live |
| D3 | Exposer ces deux constantes en Tool 6 ? | Non (compile-time, on fige) | À confirmer |
| D4 | Hold pad pressé pendant garde LEFT : ignoré ou actif ? | **Actif** (Q5 stricte) | Confirmé Q5 |
| D5 | Multi-bank-pad rising edge dans la même frame : 1er ou dernier wins ? | **1er** (par index croissant) | Confirmé Q2/§12 |
| D6 | Position du dispatcher dans loop() | Remplace `BankManager.update + ScaleManager.update + handleHoldPad + handleLeftReleaseCleanup` (4 appels → 1) | Confirmé §15 |
| D7 | Faut-il garder `BankManager`, `ScaleManager` comme classes minces (façade) ? | Oui — `BankManager` garde `switchToBank()`, `getCurrentBank()`. Le dispatcher consomme. | Confirmé §3 |
| D8 | Hooks LoopEngine — interface minimale | `preBankSwitch()`, `toggle()`, `saveSlot/loadSlot/deleteSlot()`, `tapPlayStop()`, `bypassQuantize()`, `armClear() / commitClear()` | Hors scope refonte, à figer en LOOP Phase 1 |
| D9 | Pad CLEAR tenu très longtemps puis relâché sans combo (long-press dépassé pendant le hold) — wipe fire-t-il au release ? | **Option A (recommandée)** : OUI, fire le wipe au release du lead si la durée a été atteinte (cohérent avec "le user a maintenu suffisamment, son intention est claire"). **Option B** : NON, wipe annulé si le user a tenu trop longtemps sans relâcher (forcer un re-press net). | À trancher avant Phase 7 LOOP P2 |

---

## Partie 6 — Hors scope explicite

- Pas de modification du pipeline capacitif.
- Pas de modification de `ArpScheduler`, `MidiEngine`, `MidiTransport`.
- Pas de modification de `PotRouter`, `PotFilter`.
- Pas de nouvelle Store NVS (les timers existants suffisent).
- Pas de modification des Tool setup (Tool 3, 6, etc.).
- Pas d'ajout de feature musicale (LOOP arrive plus tard, le dispatcher est neutre).
- Pas de refonte de `LedController` ni de la grammaire LED.
- Pas de réorganisation des fichiers (le dispatcher est un nouveau fichier ; les anciens sont allégés mais conservés).

---

## Partie 7 — Estimation effort

| Phase | Effort | Dépendances |
|---|---|---|
| Phase 0 — Snapshot buffer Core 0/1 (F4) | 30 min | aucune |
| Phase 1 — Squelette `GestureDispatcher.h/.cpp`, intégration `loop()` | 4-6 h | Phase 0 |
| Phase 2 — Migration BankManager logique → dispatcher | 2-3 h | Phase 1 |
| Phase 3 — Migration ScaleManager logique → dispatcher | 1-2 h | Phase 2 |
| Phase 4 — Migration handleHoldPad + sweep release | 1-2 h | Phase 3 |
| Phase 5 — Pile sacrée (modif `setCaptured`) + suppression chemins morts | 1 h | Phase 4 |
| Phase 6 — Bench live + ajustement constantes | 2-3 h | Phase 5 |
| Phase 7 — Hooks LOOP préparatoires (stubs) | 1 h | Phase 5 |
| **Total** | **~15-20 h** | linéaire |

LOOP Phase 1 (slot pads, combo CLEAR+slot) bénéficie du dispatcher mais n'est pas inclus dans le total ci-dessus. Le dispatcher livré en Phase 7 est **prêt à recevoir** les handlers LOOP via les hooks définis en §15 Phase E.

---

## Annexe A — Code à supprimer après refonte

Liste des chemins de code rendus obsolètes par le dispatcher (à confirmer dans le plan d'implémentation) :

- `BankManager::update()` body (lignes 58-155) → remplacé par dispatcher
- `BankManager::_pendingSwitchBank`, `_pendingSwitchTime`, `_switchedDuringHold`, `_doubleTapMs`, `_lastBankPadPressTime[]`, `_bankPadLast[]`, `_holdPad` → état migré dans dispatcher
- `BankManager::setDoubleTapMs()`, `setHoldPad()` → API du dispatcher
- `ScaleManager::update()` body (le scan reste, mais sans gestion edge LEFT) → simplifié, appelé par dispatcher
- `ScaleManager::_lastBtnState`, `_lastScaleKeys[]`, memcpy `_lastKeys` au release → état migré
- `main.cpp::handleHoldPad()` → migré dans dispatcher
- `main.cpp::handleLeftReleaseCleanup()` → migré dans dispatcher (sweep universel §9)
- `ArpEngine::setCaptured()` paramètres `keyIsPressed`, `holdPadIdx` → supprimés
- `ArpEngine::setCaptured()` branche `anyFingerDown → clearAllNotes()` → supprimée
- `s_lastPressTime[NUM_KEYS]` dans main.cpp (utilisé pour FG double-tap remove pad) → migré dans `processArpMode` qui appelle un helper du dispatcher pour cette détection (timer dédié ou réutilisation du timer global selon design détaillé)

---

## Annexe B — Diagramme de dépendances

```
                  ┌──────────────────────────────┐
                  │        GestureDispatcher     │
                  │  (nouveau, central)          │
                  │                              │
                  │  - tap classifier global     │
                  │  - garde LEFT, garde 2-tap   │
                  │  - sweep release universel   │
                  │  - LONG_PRESS, COMBO         │
                  └─────┬───────┬────────┬───────┘
                        │       │        │
            switchToBank│       │setCapt │triggerEvent
                        ▼       ▼        ▼
                ┌──────────┐ ┌──────────┐ ┌──────────┐
                │  Bank    │ │ ArpEngine│ │   Led    │
                │ Manager  │ │          │ │Controller│
                │ (façade) │ │          │ │          │
                └──────────┘ └──────────┘ └──────────┘
                        ▲
                        │ scale write
                ┌──────────┐
                │  Scale   │
                │ Manager  │
                │ (façade) │
                └──────────┘
                        ▲
                        │ futur: toggle / save / load / delete / tap / bypass
                ┌──────────┐
                │   Loop   │
                │  Engine  │ (LOOP Phase 1+)
                └──────────┘
```

ControlPadManager reste appelé indépendamment dans loop() — son gating LEFT est conservé tel quel ; le dispatcher ne le pilote pas.

---

**Fin de la spec.** Le plan d'implémentation détaillé avec snippets complets se trouve dans [2026-04-26-gesture-dispatcher-plan.md](../plans/2026-04-26-gesture-dispatcher-plan.md).
