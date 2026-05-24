# Audit 3 — Tool PAD ROLE : vue large LOOP runtime + oublis + doc-sync

**Date** : 2026-05-24  
**Plan audité** : `docs/superpowers/plans/2026-05-23-tool-pad-role-plan.md`  
**Spec de référence** : `docs/superpowers/specs/2026-05-23-tool-pad-role-design.md`  
**Posture** : read-only strict, critique non-complaisante  
**Scope** : Q1 (compatibilité LOOP runtime), Q2 (oublis musicaux), Q3 (docs orphelines)

---

## 0. Carto du runtime LOOP existant

### API LoopEngine (Phase 2 livrée, src/loop/LoopEngine.h)

| Méthode / champ | Rôle runtime | Source data |
|---|---|---|
| `setControlPads(rec, playStop, clear)` | Unique bridge store → runtime (appelé boot main.cpp:548-565) | `LoopPadStore.{recPad,playStopPad,clearPad}` |
| `getRecPad()`, `getPlayStopPad()`, `getClearPad()` | Getters utilisés par KeyboardTask (main.cpp:896) | copy interne LoopEngine |
| `isLoopControlPad(pad)` | Test pad = contrôle LOOP | copy interne |
| `tapRec()`, `tapPlayStop()`, `longPressClear()` | Dispatch événements LOOP | — |
| `capturePadEvent(pad, note, vel, type)` | Buffer overdub | — |
| `update(nowUs)` | Tick engine état | — |

**Champs LoopPadStore utilisés à runtime** : `recPad`, `playStopPad`, `clearPad` uniquement.  
`slotPads[16]` : déclarés, écrits dans le store, **jamais lus à runtime (aucun appel findLoopSlotIdx dans main.cpp)**. Slot Drive = Phase 6.

**LoopPotStore** : déclaré Phase 1, chargé NVS. Les 5 effets (shuffle, chaos, vel pattern, etc.) **ne sont pas injectés dans LoopEngine**. Wiring = Phase 5. Après livraison PAD ROLE, les pots LOOP n'ont aucun effet musical.

### Consumers NVS LoopPadStore (main.cpp, boot path)

```cpp
// main.cpp:548-565
const LoopPadStore& lps = s_nvsManager.getLoadedLoopPadStore();
for (uint8_t b = 0; b < numLoopBanks; b++) {
    s_loopEngines[b].setControlPads(lps.recPad, lps.playStopPad, lps.clearPad);
}
```

Observation : **tous les banks LOOP partagent les mêmes 3 pads de contrôle**. `LoopPadStore` est une struct globale (un seul jeu REC/PS/CLR pour MAX_LOOP_BANKS=4 banks). C'est cohérent avec la spec — pas un bug, mais à noter pour Q2.

---

## Q1 — Compatibilité plan PAD ROLE ↔ LOOP runtime

### Bridge control pads (3.F → main.cpp boot)

| Élément | Plan | Runtime actuel | Gap ? |
|---|---|---|---|
| UI Page LOOP (3.F) | Livre `_buildRoleMapLoop`, `_handleEnterLoop`, `_handleEnterPoolLoop` | main.cpp lit `lps.recPad/playStopPad/clearPad` au boot | Aucun gap — si store écrit correctement, bridge complet |
| `saveAll()` LoopPadStore | §12.12 (M6) requiert extension explicite de `saveAll()` dans 3.F.3 | `NvsManager::saveLoopPad()` existe, mais `saveAll()` ne l'appelle pas (finding M6 préexistant) | Gap comblé **si et seulement si** M6 est exécuté |
| Defaults factory (3.F.3) | REC=32, PS=33, CLR=34, slots=0xFF | Dev seed M7 (main.cpp:406-410) plante les mêmes valeurs temporairement | M7 supprimé en 3.H.2 — séquence correcte |
| PL/S unifié §14.1 | Même pad PL/S ARPEG (M·A) + PL/S LOOP (M·L) | Runtime dispatch bank-type-aware | OK — ACs disjoints garantis |
| slotPads[] (3.F) | UI configurée, stockée NVS | Aucun consumer runtime jusqu'à Phase 6 | **Par construction — hors scope PAD ROLE** |

**Verdict Q1** : le bridge des 3 pads de contrôle est structurellement complet. Le seul point critique est M6 (saveAll extension) qui est dans le plan §12.12 mais qui, si omis en exécution, fait que les pads LOOP ne sont jamais persistés. C'est le seul bloquant fonctionnel — il est documenté dans le plan.

### Finding Q1-F1 — Incomplet : M6 est critique mais dans une section §12 (annexe audit)

**Sévérité** : Incomplet (risque execution)  
**Fichier** : `plan §12.12`, `src/managers/NvsManager.h:89`, `src/main.cpp`  
**Fait** : M6 exige d'étendre `saveAll()` pour appeler `saveLoopPad()`. Cette extension n'est pas dans les tasks numérotées de 3.F — elle est dans une annexe audit (§12). Un exécutant qui suit le plan task-by-task sans lire §12 pourrait passer à côté.  
**Impact concret** : sans M6, la page LOOP sauvegarde localement (`_loopPad` dans le Tool) mais `saveAll()` exit ne persiste jamais LoopPadStore → au reboot, recPad/playStopPad/clearPad = 0xFF, engine sans contrôle.  
**Recommandation** : M6 devrait être une task explicite dans 3.F.3, pas seulement une note §12.

### Finding Q1-F2 — Observation : un seul jeu REC/PS/CLR pour tous les LOOP banks

**Sévérité** : Observation (pas un bug, mais implication musicale)  
**Fichier** : `src/main.cpp:548-565`, `src/core/KeyboardData.h:545-554`  
**Fait** : `LoopPadStore` est une struct globale — un seul `recPad`, `playStopPad`, `clearPad`. Tous les LOOP banks (jusqu'à 4) partagent les mêmes pads de contrôle. La page LOOP du plan configure ces 3 pads globalement, pas par bank.  
**Impact concret** : pas de problème fonctionnel — la spec ne prévoit pas de contrôles per-bank. Mais un utilisateur avec 3 banks LOOP simultanés devra mémoriser que le même pad REC tape sur le bank LOOP actif. Aucune action requise sur le plan.

### Éléments vérifiés OK — Q1

- `LoopEngine.h` : aucune référence Tool 3 / Tool 4 legacy dans le code runtime.
- `LoopEngine.cpp` (grepped) : aucun commentaire legacy Tool 3/4.
- Bridge `setControlPads()` est le seul point d'entrée — identique à ce que 3.F écrit dans `LoopPadStore`.
- `ArpRoleKind` rename (3.A : HOLD → PLAY_STOP) n'affecte pas LoopEngine — les deux systèmes sont indépendants.
- Version bump `ARPPAD_VERSION 2→3` (3.A) déclenche reset NVS ArpPadStore. `LOOPPAD_VERSION` non touché — cohérent.

---

## Q2 — Oublis : ce qui manque pour que LOOP soit musicalement utilisable après PAD ROLE

### Finding Q2-F1 — Incomplet : slotPads[] configurables mais sans effet jusqu'à Phase 6

**Sévérité** : Incomplet (attente utilisateur non gérée)  
**Fichier** : `plan §8 (3.F)`, `src/main.cpp` (aucun appel `findLoopSlotIdx`)  
**Fait** : la page LOOP du plan livre une UI complète pour assigner 16 pads de slots. Ces assignments sont stockés dans NVS. Mais aucun code runtime ne lit `slotPads[]` — le Slot Drive est Phase 6. Un utilisateur qui assigne des slots après livraison PAD ROLE ne verra aucun effet.  
**Impact concret** : pas un bug, mais une expérience dégradée non annoncée. Si la page LOOP affiche 16 pads configurables sans mention que la feature est inactive, l'utilisateur peut penser que ça marche.  
**Ce qui manque dans le plan** : une note dans l'info panel de la page LOOP (ou dans les defaults) indiquant que les slots sont "réservés Phase 6". Actuellement rien dans 3.F ne prévient l'utilisateur.

### Finding Q2-F2 — Incomplet : LoopPotStore non injecté dans LoopEngine après PAD ROLE

**Sévérité** : Incomplet (scope boundary non explicité)  
**Fichier** : `docs/superpowers/specs/2026-04-19-loop-mode-design.md §20`, `src/loop/LoopEngine.h`  
**Fait** : `LoopPotStore` est déclaré Phase 1 et contient 5 paramètres d'effets par bank (shuffle templates, chaos re-seed, velocity patterns, vel pattern depth). Ces valeurs ne sont **jamais injectées dans LoopEngine** — le wiring est explicitement Phase 5. Après livraison PAD ROLE, les pots LOOP affectés à ces paramètres (Tool 7, Phase 4) tourneront dans le vide.  
**Impact concret** : un musicien qui ouvre le LOOP en live post-Phase 3 n'a aucun effet pot fonctionnel. Ce n'est pas un oubli du plan PAD ROLE (c'est Phase 5), mais la spec parent §27 ne signale pas clairement que Phase 3 + Phase 4 ensemble ne suffisent pas à rendre LOOP musicalement complet.  
**Ce qui manque** : mention explicite dans LOOP_PROGRESS que "LOOP utilisable = Phase 4 minimum (pots) + Phase 5 (effets)" — le statut "CLOSE Phase 3" peut induire en erreur.

### Finding Q2-F3 — Observation : exit sans REC/PS/CLR assignés = LOOP bank inopérant silencieusement

**Sévérité** : Observation (comportement correct mais non documenté)  
**Fichier** : `plan §6.5` (spec §6.5 — exit hard-constraint : 8 banks NORM/ARP doivent être assignés), `src/loop/LoopEngine.h` (`_recPad(0xFF)` par défaut)  
**Fait** : la spec impose une hard-constraint exit sur les 8 banks NORM/ARP, pas sur les pads LOOP. Si un utilisateur configure une bank LOOP sans assigner REC/PS/CLR (laissant les defaults 0xFF), le runtime est safe (engine sans contrôle = loop jamais enregistré), mais la bank LOOP est silencieusement inutilisable.  
**Ce qui manque** : le plan ne prévoit pas d'avertissement à l'exit si un bank LOOP actif n'a pas de REC pad assigné. Une validation légère (warning non bloquant) serait musicalement utile. Ce n'est pas un bloquant — c'est une faiblesse UX intentionnellement hors scope.

### Finding Q2-F4 — Hors scope documenté : Phase 4 PotRouter + Phase 5 effets + Phase 6 Slot Drive

**Sévérité** : Hors scope (mention de cadrage)  
**Fait** : après livraison Plan PAD ROLE, LOOP reste à 3 étapes musicalement critiques :
- Phase 4 : PotRouter 3 contextes + Tool 7 extension (pots LOOP mappés mais non fonctionnels sans PotRouter) + LED wiring EVT_LOOP_* complet
- Phase 5 : effets (shuffle, chaos, vel) câblés LoopPotStore → LoopEngine
- Phase 6 : Slot Drive (16 slots load/save/delete)  

Le plan PAD ROLE ne couvre aucune de ces phases. C'est documenté dans LOOP_PROGRESS. **Aucune action requise sur le plan** — c'est un rappel de contexte pour ne pas lire "PAD ROLE = LOOP feature complete".

### Éléments vérifiés OK — Q2

- Le bridge 3 pads de contrôle est complet et suffisant pour qu'un LOOP bank soit opérationnel (record → play → overdub → clear).
- Les defaults factory (REC=32, PS=33, CLR=34) donnent un état fonctionnel immédiat sans configuration manuelle.
- `applyDevSeedLoopPadsIfSafe` (M7, main.cpp:406-410) supprimé en 3.H.2 = pas de double-initialisation.
- La spec §17 documente explicitement "hors scope : que se passe-t-il si REC/PS/CLR non assignés" → runtime uses 0xFF, no control pad.

---

## Q3 — Doc-sync : références orphelines Tool 3 / Tool 4 / holdPad

### Table d'audit des docs de référence

| Doc | Cartouche MAJ 2026-05-23 ? | References `holdPad` legacy ? | References Tool 3 / Tool 4 ? | Couvert dans plan §11 ? |
|---|---|---|---|---|
| `docs/reference/arp-reference.md` | Oui | Oui (lignes 59, 64 : `holdPad`, `_holdPad`) | Non | 3.I.1 ✓ |
| `docs/reference/nvs-reference.md` | Oui | Oui (ligne 76 : table validateArpPadStore) | Non (note LoopPadStore "Writer/UI à livrer") | 3.I.1 ✓ |
| `docs/reference/runtime-flows.md` | Oui | Oui (lignes 89, 185, 229) | Non | 3.I.2 ✓ |
| `docs/reference/setup-tools-conventions.md` | Oui | Inconnu (cartouche présent) | Oui (Tool 3 / Tool 4 mentionnés) | 3.I.1 ✓ |
| `docs/reference/arp-reference.md` | Oui | Oui | Non | 3.I.1 ✓ |
| `docs/reference/architecture-briefing.md` | Non trouvé | Non trouvé dans grep | Non trouvé | 3.I.1 (citée) |
| `docs/reference/loop-buffer-invariants.md` | — | Oui (ligne 189 : "Code legacy `HOLD_PAD`") | Non | 3.I.2 ✓ |
| `docs/superpowers/specs/2026-04-19-loop-mode-design.md §5` | Oui (cartouche §5) | — | §27 Phase 3 = FRAMING ANCIEN (voir F3) | 3.I.2 partiel |
| `docs/superpowers/LOOP_PROGRESS.md` | Non (last update 2026-05-19) | Non | Phase 3 = "Tool 3 b1 setup + Tool 4 ext" | 3.I.3 ✓ |

### Finding Q3-F1 — Doc-orphelin : spec parent §27 Phase 3 = framing ancien non mis à jour

**Sévérité** : Doc-orphelin (incohérence doc, impact lecture/navigation)  
**Fichier** : `docs/superpowers/specs/2026-04-19-loop-mode-design.md`, §27 Phase 3  
**Fait** : le §5 de la spec parent a un cartouche "REFONDU 2026-05-23 — remplacée par tool-pad-role-design.md". Mais le §27 Phase 3 contient encore l'ancienne description : _"Refactor Tool 3 vers b1 contextuel + Extension Tool 4"_. Un lecteur qui commence par §27 (tableau d'étapes) reçoit l'ancien framing et doit aller en §5 pour trouver la redirection.  
**Plan 3.I.2** : couvre la spec parent avec "vérifier cross-pointer en §5" — mais ne mentionne pas la mise à jour du §27 Phase 3.  
**Impact concret** : la lecture de §27 Phase 3 induit en erreur sur ce que délivre réellement la phase (Tool PAD ROLE vs Tool 3 b1 + Tool 4). Faible mais réel — la spec est un document vivant consulté par sessions futures.  
**Recommandation** : dans 3.I.2, ajouter explicitement "mettre à jour §27 Phase 3 avec le nouveau framing Tool PAD ROLE".

### Finding Q3-F2 — Doc-orphelin : LOOP_PROGRESS Phase 3 = ancien framing

**Sévérité** : Doc-orphelin (mineur, même problème que Q3-F1)  
**Fichier** : `docs/superpowers/LOOP_PROGRESS.md`, ligne 28  
**Fait** : le tableau d'étapes de LOOP_PROGRESS décrit Phase 3 LOOP comme _"Tool 3 b1 setup + Tool 4 ext refus collision LOOP control"_ — framing pré-refonte. Le statut est "🔄 Spec refondue 2026-05-23, plan d'impl à venir" — donc déjà signalé comme en cours de refonte. Mais la description de la colonne "Sortie attendue" reste l'ancienne.  
**Plan 3.I.3** : couvre LOOP_PROGRESS — inclus dans la liste de docs à mettre à jour.  
**Impact concret** : cohérence lecture. Mineur.

### Finding Q3-F3 — Observation : architecture-briefing.md non confirmé propre

**Sévérité** : Observation (incertitude résiduelle)  
**Fichier** : `docs/reference/architecture-briefing.md`  
**Fait** : le grep sur Tool 3 / Tool 4 / holdPad dans architecture-briefing.md n'a pas retourné de mentions claires (résultats de recherche tronqués). Le plan 3.I.1 cite ce fichier dans la liste des docs à mettre à jour. Cela indique que le plan couvre le risque. Mais l'audit n'a pas pu confirmer avec certitude l'absence de references legacy.  
**Impact** : faible — le plan 3.I.1 couvre ce fichier explicitement.

### Éléments vérifiés OK — Q3

- Tous les docs référencés dans 3.I.1 ont un cartouche "MAJ 2026-05-23" (les cartouches ont été posés anticipativement).
- `src/loop/LoopEngine.cpp` : aucun commentaire Tool 3 / Tool 4 dans le code runtime.
- `loop-buffer-invariants.md` ligne 189 : "Code legacy `HOLD_PAD`, rename vers `arpPlayStopPad` à la livraison Tool PAD ROLE" — couvert par 3.I.2 et par 3.A qui fait le rename.
- Les fichiers `arp-reference.md`, `nvs-reference.md`, `runtime-flows.md` ont tous des cartouches anticipatifs et sont dans 3.I.1.

---

## Synthèse globale

### Table par sévérité

| ID | Sévérité | Sujet | Action requise |
|---|---|---|---|
| Q1-F1 | **Incomplet** | M6 (saveAll LoopPadStore) est dans §12 annexe, pas dans les tasks 3.F.3 | Ajouter M6 comme task explicite dans 3.F.3 |
| Q2-F1 | **Incomplet** | slotPads[] UI livrée, aucun effet runtime — utilisateur non prévenu | Ajouter note info panel "slots réservés Phase 6" dans la page LOOP |
| Q2-F2 | **Incomplet** | LoopPotStore effets non câblés — LOOP musicalement partiel post-PAD ROLE | Clarifier dans LOOP_PROGRESS que LOOP utilisable ≠ Phase 3 seule |
| Q3-F1 | **Doc-orphelin** | spec parent §27 Phase 3 = framing ancien (Tool 3 b1 + Tool 4) | Ajouter mise à jour §27 Phase 3 dans 3.I.2 |
| Q3-F2 | **Doc-orphelin** | LOOP_PROGRESS Phase 3 description = ancien framing | Inclus dans 3.I.3 — vérifier à l'exécution |
| Q1-F2 | Observation | Un seul jeu REC/PS/CLR partagé entre tous LOOP banks | Pas d'action — cohérent avec spec |
| Q2-F3 | Observation | Exit sans REC/PS/CLR = LOOP bank silencieusement inopérant | Pas d'action — hors scope délibéré |
| Q3-F3 | Observation | architecture-briefing.md non confirmé propre par audit | Couvert dans 3.I.1 — pas d'action supplémentaire |
| Q2-F4 | Hors scope | Phases 4-5-6 manquantes pour LOOP complet | Rappel de cadrage — aucune action sur PAD ROLE |

### Verdict

**Plan exécutable tel quel** avec 3 ajustements recommandés avant de démarrer :

1. **M6 → task 3.F.3 explicite** : déplacer ou dupliquer M6 (saveAll LoopPadStore) dans les tasks numérotées de la phase 3.F.3, pas seulement en §12. C'est le seul point qui peut rendre la livraison silencieusement non-fonctionnelle si un exécutant suit les tasks sans lire l'annexe.

2. **Info panel slots Phase 6** : dans la page LOOP (3.F), ajouter une ligne dans le panel INFO des slot pads indiquant que les slots sont configurables mais actifs uniquement en Phase 6. Effort marginal, évite confusion utilisateur.

3. **§27 Phase 3 dans 3.I.2** : compléter la task 3.I.2 avec la mise à jour du §27 Phase 3 de la spec parent (`2026-04-19-loop-mode-design.md`), pas seulement le §5 cartouche.

**Aucun bloquant qui empêche l'exécution.** Les 3 incomplets ci-dessus sont des faiblesses d'exécution (M6) et de clarté utilisateur/doc (slots, §27), pas des erreurs d'architecture. La compatibilité Q1 est structurellement solide. Les gaps Q2 (effets, slots) sont des frontières de phase correctement définies — le plan PAD ROLE n'est pas responsable de les combler.

---

_Audit 3 — read-only. Aucune modification apportée aux sources. Rapport seul produit de cette session._
