# LOOP — Invariants du buffer

Référence pour la robustesse du `LoopEngine` (Phase 2+). Identifie une classe
d'anti-patterns destructifs à éviter sur le buffer d'events LOOP, par symétrie
avec les fixes ARPEG appliqués en mai 2026 sur la pile.

**Source historique** : [`docs/archive/2026-04-26-gesture-dispatcher-design.md`](../archive/2026-04-26-gesture-dispatcher-design.md)
Parties 8-9. La refonte `GestureDispatcher` qui portait ce contenu a été
abandonnée (cf. STATUS.md, Phase 2 LOOP réécrite from scratch). Les
**invariants** restent normatifs ; seuls les passages "à refondre" sont morts.

---

## §1 — Contexte : 5 fixes ARPEG mai 2026

Pendant la préparation de la refonte abandonnée, 5 bugs réels sur la pile ARPEG
ont été corrigés directement sur main :

| Commit | Titre |
|---|---|
| `2dc80d9` | `fix(arpeg): pile préservée au LEFT release (sweep destructif supprimé)` |
| `4799918` | `feat(arpeg): auto-Play sur 1er press musical en Stop + LED feedback` |
| `5aa15fc` | `feat(arpeg): LEFT + hold pad = toggle Play/Stop global multi-bank` |
| `7432047` | `fix(arpeg): F1 — pile préservée sur Stop avec fingers down` |
| `00f88e4` | `fix(arpeg): F8 — pile préservée au release pads tenus sur Stop` |

Ces fixes ont supprimé du code qui modifiait la pile ARPEG **sur des
transitions de transport** (Play↔Stop, LEFT release) ou **des releases de
pads** au lieu de presses musicaux explicites. La même classe d'anti-patterns
pourrait se réintroduire dans `LoopEngine` si on n'y prend pas garde — d'où ce
document.

---

## §2 — Invariants ARPEG (base du raisonnement LOOP)

À préserver dans toute évolution future du code ARPEG. Ils servent de
**modèle de référence** pour les invariants LOOP §3.

1. **Pile sacrée par transition de transport** : aucun toggle Play/Stop (hold
   pad, double-tap bank pad, toggle global multi-bank, futur LOOP play/stop) ne
   modifie la pile ARPEG. Les seuls accès en écriture sont les actions de jeu
   explicites (press/release musical, double-tap remove).
2. **Pas de sweep destructif au release LEFT** : `handleLeftReleaseCleanup` ne
   fait plus de `removePadPosition` en ARPEG-OFF.
3. **Stop est momentané, pas un mode de jeu persistant** : un press musical en
   Stop re-engage Play automatiquement (auto-Play). Pour rester en Stop, ne
   pas toucher aux pads de jeu (silence volontaire).
4. **LEFT comme modifier de scope** : sans LEFT, le hold pad toggle la FG
   seule. Avec LEFT, le hold pad toggle toutes les banks. Pas de double-tap
   nécessaire.
5. **Aucune transition automatique** ne modifie la pile en dehors des 4
   actions de jeu explicites : `addPadPosition` rising edge en Play,
   `addPadPosition` rising edge musical en Stop (déclenche auto-Play),
   `removePadPosition` double-tap pad musical en Play, `clearAllNotes` 1er
   press musical en Stop avec paused pile non vide.

---

## §3 — Buffer LOOP sacré

**Symétrique** de "pile sacrée" pour ARPEG, étendu au buffer d'events LOOP :

> Aucune transition de transport (play/stop, bank switch, hold pad, toggle
> global multi-bank, sweep release LEFT, etc.) ne modifie le buffer d'events
> du `LoopEngine`. Le buffer est uniquement modifié par des actions
> utilisateur explicites listées en §5.

---

## §4 — Anti-patterns ARPEG → LOOP à NE PAS reproduire

| Anti-pattern ARPEG (corrigé) | Équivalent LOOP à éviter |
|---|---|
| Branche `anyFingerDown → clearAllNotes()` dans `ArpEngine::setCaptured(false)` (fix `7432047`) | `LoopEngine::stop()` ou `tapPlayStop()` qui wiperait le buffer si des pads sont tenus. **Le stop LOOP doit toujours préserver le buffer**, peu importe le contexte pad. |
| Branche `live remove` sur falling edge en Stop dans `processArpMode` (fix `00f88e4`) | `processLoopMode` qui retirerait des events du buffer sur falling edge de LOOP control pad ou autre. **Le release de pad ne modifie jamais le buffer**. |
| `handleLeftReleaseCleanup` ARPEG-OFF sweep destructif (fix `2dc80d9`) | sweep release LEFT qui appellerait `LoopEngine.cleanup()` ou équivalent sur la bank LOOP FG. **Le release LEFT ne déclenche aucune modification du buffer**. Le snapshot `_lastKeys` reste non-destructif. |
| Stop est un mode persistant qui nécessite un toggle manuel pour repartir (auto-Play §13.2 ancien) | `LoopEngine` en STOPPED qui resterait STOPPED jusqu'à un PLAY manuel. **Cohérence à décider** : spec LOOP §17 prévoit déjà des transitions tap PLAY/STOP explicites avec quantize. À ne pas confondre avec un auto-Play ARPEG. |

---

## §5 — Liste exhaustive des actions qui modifient le buffer LOOP

D'après spec LOOP §3, §11, §12, §13 et §17, **et uniquement ces actions** :

1. **REC press** → enregistre les events suivants dans le buffer (overdub si
   déjà PLAYING, sinon enregistrement initial).
2. **REC tap en OVERDUBBING + PLAY/STOP tap** → annule l'overdub (revert au
   buffer pré-overdub) — pas un wipe, un revert.
3. **CLEAR long-press** (durée `clearLoopTimerMs`) → wipe buffer entier, état
   → EMPTY.
4. **Load slot short-press** (300-1000 ms sous LEFT) → replace buffer par le
   slot chargé.
5. **PLAY/STOP double-tap bypass quantize** → flush MIDI notes en cours,
   **mais préserve le buffer** (spec LOOP §17).
6. **WAITING_LOAD + bank switch** → commit load avant switch, donc replace
   buffer (action explicite par le load déjà déclenché).

**Toute autre interaction** (LEFT press/release, hold pad, bank switch sans
WAITING_LOAD, scale change, pot move, pad release, etc.) doit laisser le
buffer **strictement intact**.

---

## §6 — Toggle multi-bank étendu LOOP

`toggleAllArps()` (ARPEG actuel, fix `5aa15fc`) devra être **étendu** pour
inclure les banks LOOP. Pattern d'extension à implémenter en Phase 2+ :

```cpp
static void toggleAllArpsAndLoops() {
  // 1. État collectif : au moins une bank en Play (ARPEG capturé OU LOOP PLAYING) ?
  bool anyPlaying = false;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (isArpType(s_banks[i].type) && s_banks[i].arpEngine
        && s_banks[i].arpEngine->isCaptured()) { anyPlaying = true; break; }
    if (s_banks[i].type == BANK_LOOP && s_banks[i].loopEngine
        && s_banks[i].loopEngine->isPlaying()) { anyPlaying = true; break; }
  }
  // 2. Toggle symétrique sur les 2 types
  // ... (cf. snippet original §30 spec gesture archivée pour le détail)
}
```

LED multi-bank mask `EVT_PLAY` / `EVT_STOP` couvre les deux types
indifféremment. Décision Phase pour LOOP : ce hook nécessite LED multi-bank
feedback (à coordonner avec rendu `renderBankLoop` complet).

---

## §7 — Décisions à valider lors de l'implémentation LOOP P2

À trancher au moment de la rédaction du plan Phase 2 :

| Question | Options |
|---|---|
| `LoopEngine::tapPlayStop()` (layer musical, sans LEFT) doit-il interagir avec auto-Play §13.2 ? | A. Layer musical LOOP indépendant (REC/PLAY-STOP/CLEAR ne sont jamais considérés "press musical en Stop" du dispatcher ARPEG). B. Hybride. **A recommandé** (sépare cleanly les 2 systèmes). |
| Slot save sous LEFT (LONG_PRESS) doit-il être annulable par un release prématuré ? | Spec LOOP §11 dit oui, à confirmer dans `LoopEngine`. |
| Combo CLEAR + slot delete : que se passe-t-il si l'utilisateur tient CLEAR très longtemps puis relâche sans avoir pressé de slot ? | Option A "fire wipe au release si timer dépassé" pré-recommandée. À confirmer Phase 6. |
| Bank switch pendant WAITING_LOAD : commit load **avant** switch (spec LOOP §17) — quelle signature exacte pour le hook `LoopEngine::preBankSwitch()` ? | À trancher Phase 6/7 (load slot drive). |

---

## §8 — Checklist pré-implémentation LOOP P2

Avant d'écrire la première ligne de `LoopEngine.cpp`, le reviewer doit
confirmer point par point :

- [ ] Le plan LOOP P2 ne contient aucun chemin qui appelle un équivalent de
  `clearAllNotes()` sur le buffer hors des 6 actions §5.
- [ ] Le plan LOOP P2 ne contient aucun chemin "live remove" ou "sweep des
  events non pressés" sur falling edge de quelque catégorie de pad que ce soit.
- [ ] La fonction `LoopEngine::stop()` (ou équivalent) est **idempotente sur
  le buffer** : appeler `stop()` puis `play()` sans rien d'autre redonne
  exactement le même état audible.
- [ ] Toute interaction LEFT (press/release/held) ne touche pas le buffer LOOP.
- [ ] Le toggle global `(HOLD_PAD, leftHeld=true)` est prévu pour être étendu
  aux banks LOOP (cf. §6).
- [ ] Les invariants ARPEG §2 (1-5) restent préservés par l'ajout de LOOP.

Si une checkbox ne peut pas être cochée avant l'implémentation, c'est qu'il
manque une décision de spec — trancher d'abord, coder ensuite.

---

**Fin du document.** Référence stable, ne dépend plus du doc gesture-dispatcher
archivé.
