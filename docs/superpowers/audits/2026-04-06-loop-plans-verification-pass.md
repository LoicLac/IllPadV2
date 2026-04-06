# LOOP Plans Verification Pass — 2026-04-06 (cycle 2)

**Scope** : audit méticuleux read-only de **vérification post-patches** des 6 plans LOOP (Phase 1-6) après l'application des 24 patches du cycle 1 (`52c3c8b` audit + `a2e46f7` patches). Mission double :

1. **Vérifier les 24 patches précédents** — sont-ils présents, corrects, cohérents, sans régression ?
2. **Recherche fresh de NOUVEAUX bugs** — sans se limiter aux zones patchées, comme un audit frais.

**Source de référence** : `docs/superpowers/audits/2026-04-06-loop-plans-deep-audit.md` (rapport pass 1).

**Mode** : read-only sur le code source, modifications autorisées sur les plans/spec/refs. Aucun code source modifié, aucun commit non autorisé.

---

## 1. Synthèse exécutive

### Résultat de la vérification des 24 patches (cycle 1)

| Phase | Patches | Statut |
|---|---|---|
| Phase 1 (skeleton) | 4 | 4 OK |
| Phase 2 (engine) | 7 | 6 OK + 1 plan↔code mismatch (B1.3) |
| Phase 3 (setup tools) | 4 | 4 OK |
| Phase 6 (slot drive) | 9 | 9 OK |
| **Total** | **24** | **23 OK + 1 issue (D1)** |

**Verdict** : les patches du cycle 1 tiennent globalement debout. Une seule issue (D1) — le snippet `handleLeftReleaseCleanup` du Phase 2 Step 10b-1 décrit une structure `switch` alors que le code utilise `if/else if`.

### Nouveaux findings (PAS dans la passe précédente)

| Catégorie | Count | Findings |
|---|---|---|
| A (bloquants) | 2 | A1 (Phase 4 missing include), A2 (Phase 3 saveConfig drop live updates — **reclassé en F après reformulation user**) |
| B (runtime) | 3 | B1 (quantize boundary missed catch-up), B2 (deserializeFromBuffer no guard), B3 (tick flash early-return order) |
| C (inter-plan) | 1 | C1 (playStopPad rename inconsistance Phase 3 Step 2-pre-1 vs Step 3e) |
| D (plan↔code) | 1 | D1 (handleLeftReleaseCleanup switch vs if/else if) |
| E (plan↔spec) | 0 | — |
| F (optim) | 3 | F1 (LED_TICK_BOOST_BEAT1 unused YAGNI), F2 (getCurrentSlot redondant), F3 (16 pool lines deferred) |

### Reclassification importante

**A2 (Phase 3 saveConfig drop live updates)** a été initialement classé comme bloquant fonctionnel (silent runtime/NVS desync). Après échange avec l'utilisateur : **toute sortie de Tool passe systématiquement par un reboot (Tool 0)**. Le live update post-save est de fait du code mort. A2 est **reclassé en F (clarification)** — pas un bug, mais une décision design à documenter.

### Niveau de confiance global

**MEDIUM-HIGH** après application des fixes de cette passe. Les plans sont prêts pour l'implémentation séquentielle Phase 1 → Phase 6.

---

## 2. Décisions de traitement (validées avec l'utilisateur)

| Finding | Décision | Action sur les docs |
|---|---|---|
| A1 (Phase 4 missing include) | Patch direct | Phase 4 Step 9b-bis nouveau + Files Modified table |
| A2 reclassifié | Documenter le drop volontaire | Phase 3 Step 1b-bis : audit note explicative |
| B1 (quantize boundary missed) | Fix dans LoopEngine seul + tracker ArpEngine dans known-bugs.md | Phase 2 Step 1a (private member) + Step 1c (tick body) ; nouveau `docs/reference/known-bugs.md` |
| B2 (deserializeFromBuffer guard) | Patch direct | Phase 6 Step 3c : guard d'état au début |
| B3 (tick flash early-return) | Patch direct | Phase 4 Step 9c : déplacer consume après early-return |
| C1 (playStopPad rename) | Renommer en `arpPlayStopPad` partout | Phase 3 Step 3e : signature update |
| D1 (handleLeftReleaseCleanup) | Réécrire snippet en if/else if + relSlot | Phase 2 Step 10b-1 : snippet rewrite |
| D2/V2 (Step 4b → 7c-1) | Patch direct | Phase 1 Step 2b : note correction |
| F1 (LED_TICK_BOOST_BEAT1) | Supprimer | Phase 1 Step 4b : remove constant + audit note ; Phase 4 Step 9c : update comment |
| F2 (getCurrentSlot redondant) | Réutiliser `slot` | Phase 6 Step 6b : code update |
| F3 (16 pool lines) | Différer | Pas d'action sur les plans |
| Q1 (LittleFS path format) | Note `VERIFY ON BUILD` dans le plan | Phase 6 Step 2b : commentaire détaillé |

---

## 3. Détail des findings

### A1 — Phase 4 Step 9c missing `#include "../loop/LoopEngine.h"` in LedController.cpp

**Sévérité** : bloquant compile

**Contexte** : Phase 1 Step 4c-4 ajoute `class LoopEngine;` (forward-declare) à `LedController.h`. C'est suffisant pour déclarer un `LoopEngine*` membre. Mais Phase 4 Step 9c implémente `renderBankLoop()` qui appelle des **méthodes** sur le pointeur (`getState()`, `getEventCount()`, `consumeBeatFlash()`, `consumeBarFlash()`, `consumeWrapFlash()`, `hasPendingAction()`, `getLoopQuantizeMode()`). Ces appels nécessitent la **définition complète** de la classe.

**Vérification source actuelle** :
```
$ grep '#include' src/core/LedController.cpp
1:#include "LedController.h"
2:#include "HardwareConfig.h"
3:#include "KeyboardData.h"
4:#include "../arp/ArpEngine.h"   ← pattern existant pour ArpEngine
```

L'analogue ARPEG (`ArpEngine.h`) est inclus depuis longtemps pour la même raison. Sans l'équivalent LOOP, Phase 4 ne compile pas. Le plan ne mentionnait pas l'ajout de cet include.

**Fix appliqué** : Phase 4 Step 9b-bis ajouté + Files Modified table mise à jour.

---

### A2 (reclassé en F) — Phase 3 Step 1b-bis saveConfig drop live updates

**Sévérité initiale** : bloquant fonctionnel
**Sévérité finale** : F (clarification design)

**Contexte initial** : le snippet du plan Phase 3 Step 1b-bis présente une réécriture complète de `saveConfig()` qui drop la boucle post-save :
```cpp
// Boucle existante dans src/setup/ToolBankConfig.cpp:36-39 :
for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _banks[i].type = types[i];
    if (_nvs) _nvs->setLoadedQuantizeMode(i, quantize[i]);
}
```

J'ai initialement classé ça en bloquant : sans la boucle, après save les `_banks[i].type` et `_nvs->_loadedQuantize[]` restent à leurs anciennes valeurs en mémoire vive. Le user qui sauvegarde, ferme le tool, et joue n'aurait pas vu ses changements appliqués.

**Reformulation utilisateur** : « après un tool, le reboot est de fait systématiquement appliqué. il n'y a jamais en pratique de changement de paramètre des tool qui ne passent pas par un reboot ». Le seul exit path d'un Tool est `Tool 0 → reboot`. Donc la boucle live update est de fait du code mort — peu importe que la mémoire vive soit synchronisée puisqu'elle sera reconstruite via `loadAll()` au prochain boot.

**Conséquence** : A2 devient une **décision design à documenter**, pas un bug. Le drop est acceptable, voire préférable (moins de code à maintenir).

**Fix appliqué** : Phase 3 Step 1b-bis : commentaire AUDIT NOTE qui explicite le drop volontaire, pour que le prochain dev qui audit ne re-trouve pas le « bug » et ne ré-introduise pas la boucle.

---

### B1 — Phase 2 Step 1c quantize boundary missed sur catch-up tick

**Sévérité** : medium

**Contexte** : la pending action dispatcher de `LoopEngine::tick()` utilise :
```cpp
if (_pendingAction != PENDING_NONE) {
    uint16_t boundary = (_quantizeMode == LOOP_QUANT_BAR) ? TICKS_PER_BAR : TICKS_PER_BEAT;
    if (globalTick % boundary == 0) {
        // execute and clear
    }
}
```

Le check `globalTick % boundary == 0` n'est satisfait que si `globalTick` tombe **exactement** sur un multiple de `boundary`.

**Vérification source actuelle** (`src/midi/ClockManager.cpp:181-203`) :
```cpp
void ClockManager::generateTicks(uint32_t nowUs) {
  ...
  // Catch up missed ticks (max 4 per call to avoid burst-fire)
  uint8_t ticksGenerated = 0;
  while ((nowUs - _lastTickTimeUs) >= interval && ticksGenerated < 4) {
    _currentTick++;
    _lastTickTimeUs += interval;
    ticksGenerated++;
    ...
  }
}
```

`generateTicks()` peut avancer `_currentTick` de **1 à 4 valeurs** dans un seul appel. Si une iteration loop() prend > 1 tick interval (~21ms à 120 BPM), le catch-up peut faire passer `globalTick` de e.g. 23 à 25, **sautant le 24**. Le check `25 % 24 != 0` échoue. L'action en attente attend la prochaine occasion (`48 % 24 == 0`), soit ~480ms (1 beat à 120 BPM) ou ~2s (1 bar à 120 BPM) de plus.

Conditions de déclenchement : NVS commit (~50ms), BLE callback burst, démarrage clock externe.

**Aggravation** : le **MÊME bug existe dans `ArpEngine.cpp:284`** (pre-existing) — la passe précédente ne l'avait pas vu.

**Fix appliqué** :
- Phase 2 Step 1a : ajout du membre `_lastDispatchedGlobalTick`
- Phase 2 Step 1b begin() : init à `0xFFFFFFFF` (sentinel)
- Phase 2 Step 1c tick() : remplacement du check par crossing detection via integer division
- Nouveau fichier `docs/reference/known-bugs.md` : entry B-001 pour tracker le bug ArpEngine pré-existant (TODO)

---

### B2 — Phase 6 Step 3c deserializeFromBuffer no guard against RECORDING/OVERDUBBING

**Sévérité** : medium (defensive)

**Contexte** : `deserializeFromBuffer()` flushes les notes actives, remplace les events, et bascule `_state = STOPPED`. Si appelée pendant `RECORDING` ou `OVERDUBBING`, elle corromprait silencieusement la session en cours. Le caller actuel (`handleLoopSlots`) bloque déjà via son check `recording`, mais un futur caller (debug, web UI, MIDI sysex) ne bénéficierait pas de ce verrou.

**Fix appliqué** : Phase 6 Step 3c : guard ajouté en début de fonction, return false si RECORDING/OVERDUBBING.

---

### B3 — Phase 4 Step 9c tick flash flags consommés avant early-return waiting-quantize

**Sévérité** : low (cosmétique)

**Contexte** : `renderBankLoop()` consomme les 3 flags (`consumeBeatFlash`, `consumeBarFlash`, `consumeWrapFlash`) au début, puis early-return si `hasPendingAction()` est vrai. Les méthodes consume sont destructives — les flags consommés pendant un waiting-quantize sont perdus. Conséquence : le tout premier beat après que le pending action complete n'a pas son flash visible.

**Fix appliqué** : Phase 4 Step 9c : déplacer la consommation des flags APRÈS le early-return.

---

### C1 — Phase 3 Step 2-pre-1 vs Step 3e disagreement on `playStopPad` rename

**Sévérité** : moyenne

**Contexte** : Phase 3 Step 2-pre-1 dit explicitement :
> Rename `_playStopPad` (live pointer) → `_arpPlayStopPad`. The `begin()` pointer parameter must be updated accordingly.

Mais Step 3e du même plan présente les signatures `begin()` avec `playStopPad` (non renommé). Phase 6 Step 7c utilise `arpPlayStopPad` (cohérent avec Step 2-pre-1). Donc Phase 3 Step 3e et Phase 6 Step 7c sont **incompatibles**.

**Décision utilisateur** : renommer en `arpPlayStopPad` partout (option Step 2-pre-1).

**Fix appliqué** : Phase 3 Step 3e : signatures SetupManager.h, SetupManager.cpp, ToolPadRoles.h, ToolPadRoles.cpp mises à jour avec `arpPlayStopPad`. La variable main.cpp `s_playStopPad` reste telle quelle (binding par référence).

---

### D1 — Phase 2 Step 10b-1 handleLeftReleaseCleanup décrit comme switch alors que c'est if/else if

**Sévérité** : haute (snippet ne s'applique pas)

**Contexte** : le plan Step 10b-1 dit "Find the existing switch in handleLeftReleaseCleanup()" et présente un snippet `case BANK_LOOP: ... break;`. Vérification du code actuel (`src/main.cpp:555-577`) :
```cpp
static void handleLeftReleaseCleanup(const SharedKeyboardState& state) {
  static bool s_wasHolding = false;
  bool holdingNow = s_bankManager.isHolding() || s_scaleManager.isHolding();
  if (s_wasHolding && !holdingNow) {
    BankSlot& relSlot = s_bankManager.getCurrentSlot();
    if (relSlot.type == BANK_NORMAL) { ... }      // ← if/else if, PAS un switch
    else if (relSlot.type == BANK_ARPEG && ...) { ... }
  }
  ...
}
```

Le snippet `case BANK_LOOP: ... break;` ne s'applique pas — il faut le réécrire en `else if (relSlot.type == BANK_LOOP && relSlot.loopEngine)`. De plus, la variable est `relSlot`, pas `slot`. Ne pas confondre avec `handlePadInput()` (`src/main.cpp:579-594`) qui LUI utilise un vrai `switch (slot.type)`.

**Fix appliqué** : Phase 2 Step 10b-1 : snippet réécrit avec la structure correcte (else if + relSlot).

---

### F1 — LED_TICK_BOOST_BEAT1 défini mais inutilisé

**Sévérité** : minor (YAGNI cleanup)

**Contexte** : Phase 1 Step 4b définit `LED_TICK_BOOST_BEAT1 = 25` avec un commentaire "Downbeat (beat 1)". Phase 4 Step 9c admet que cette constante n'est jamais utilisée au runtime (le bar boost gagne toujours sur beat 1) et la justifie par "future Tool 7 configurability" — exactement ce que CLAUDE.md interdit (YAGNI).

**Fix appliqué** : Phase 1 Step 4b : suppression de la constante + audit note explicative. Phase 4 Step 9c : commentaire dans renderBankLoop mis à jour pour ne plus référencer la constante supprimée.

---

### F2 — Phase 6 Step 6b getCurrentSlot appelé deux fois redondamment

**Sévérité** : minor

**Contexte** : `handleLoopSlots()` déclare `BankSlot& slot = s_bankManager.getCurrentSlot();` au début, puis re-déclare `BankSlot& curSlot = s_bankManager.getCurrentSlot();` dans le bloc load. Les deux pointent vers le même objet — redondant.

**Fix appliqué** : Phase 6 Step 6b : réutiliser `slot` directement, supprimer `curSlot`.

---

### F3 — 16 lignes pool slot dans Tool 3 LOOP sub-page

**Sévérité** : minor (UX deferred)

**Décision utilisateur** : différer. Pas d'action sur les plans.

---

### Q1 — LittleFS path format `/littlefs/loops/...` vs `/loops/...`

**Sévérité** : à vérifier au build

**Contexte** : Phase 6 Step 2b utilise `LittleFS.exists("/littlefs/loops/slotNN.lpb")`. Selon la version d'espressif32 installée, les paths LittleFS peuvent être interprétés avec ou sans le préfixe basePath. Sur certaines versions récentes, l'objet `LittleFS` accepte les paths SANS préfixe (`/loops/slot00.lpb`) et le basePath n'est utilisé que pour les calls POSIX/VFS hors-objet.

**Décision utilisateur** : note `VERIFY ON BUILD` dans le plan, l'implémenteur tranchera au premier test hardware.

**Fix appliqué** : Phase 6 Step 2b : commentaire détaillé dans `slotPath()` expliquant les deux variantes possibles et comment switcher si le premier test échoue.

---

## 4. Fichiers modifiés par cette passe

| Fichier | Changements |
|---|---|
| `docs/plans/loop-phase1-skeleton-guards.md` | V2/D2 (Step 2b note), F1 (Step 4b LED_TICK_BOOST_BEAT1 removed) |
| `docs/plans/loop-phase2-engine-wiring.md` | D1 (Step 10b-1 snippet rewrite), B1 (Step 1a private member, Step 1b begin init, Step 1c tick crossing detection), doc note (Step 1a `_baseVelocity`/`_velocityVariation` private members) |
| `docs/plans/loop-phase3-setup-tools.md` | A2 reclassified (Step 1b-bis audit note), C1 (Step 3e rename `playStopPad` → `arpPlayStopPad` in 4 signatures + main.cpp call comment) |
| `docs/plans/loop-phase4-potrouter-led.md` | A1 (new Step 9b-bis include + Files Modified table), B3 (Step 9c reorder), F1 cleanup (Step 9c BEAT1 comment update) |
| `docs/plans/loop-phase6-slot-drive.md` | Q1 (Step 2b VERIFY ON BUILD note), B2 (Step 3c guard), F2 (Step 6b reuse `slot`) |
| `docs/reference/known-bugs.md` | NEW — table TODO/DONE format, entry B-001 pour ArpEngine quantize boundary |
| `docs/superpowers/audits/2026-04-06-loop-plans-verification-pass.md` | NEW — ce rapport |

**Aucun code source modifié.**

---

## 5. Recommandations pour l'implémentation

Les plans LOOP sont **prêts pour l'implémentation séquentielle Phase 1 → Phase 6** après application des fixes de cette passe. Aucun cycle d'audit supplémentaire requis — les findings restants sont soit :
- Documentés en `known-bugs.md` (B-001 ArpEngine, à traiter séparément)
- Différés explicitement (F3)
- À vérifier au premier build hardware (Q1)

**Charge estimée des fixes appliqués** : ~1h de travail sur les plans (déjà fait), zéro modification source.

**Prochaine action utilisateur** : valider la liste des fichiers et le commit message proposé, puis autoriser le commit.
