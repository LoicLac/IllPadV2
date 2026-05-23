# Manifeste de session — Exécution Phase 3 LOOP

> **ARCHIVÉ 2026-05-23** — Instance Phase 3 caduque. Discipline durable codifiée dans [`docs/superpowers/SESSION_PROTOCOL.md`](../../superpowers/SESSION_PROTOCOL.md) — réutilisable pour toutes phases futures. Référence historique des audit-fix v1+v2 Phase 3. Voir aussi [`tool-pad-role-design.md`](../../superpowers/specs/2026-05-23-tool-pad-role-design.md).

**Plan référence** : [`2026-05-19-loop-phase-3-plan.md`](2026-05-19-loop-phase-3-plan.md) (2900+ lignes + addendums v1 audit-fix + v2 refondation indépendante)

**Spec source** : [`../specs/2026-05-19-loop-phase-3-design.md`](../specs/2026-05-19-loop-phase-3-design.md) (refondue §13 + §22 + §2 post audit indépendant 2026-05-19)

**Audit auto (v1)** : [`2026-05-19-loop-phase-3-plan_AUDIT.md`](2026-05-19-loop-phase-3-plan_AUDIT.md) (1 B-N + 7 M + 11 m). Fix critiques B-N1 + M1-M5 intégrés dans le plan via addendum v1.

**Audit indépendant (v2)** : [`2026-05-19-loop-phase-3-plan_AUDIT_independent.md`](2026-05-19-loop-phase-3-plan_AUDIT_independent.md) (2 B-N + 7 M + 6 m supplémentaires, dont B-N2, B-N3, M8, M14 critiques). Fix intégrés dans le plan via addendum v2 (refondation (c)).

**Session protocole** : [`../SESSION_PROTOCOL.md`](../SESSION_PROTOCOL.md) (9 règles + 5 templates + 7 anti-patterns).

---

## Règles strictes pour cette session

### Règle 1 — HW gates : confirmation explicite obligatoire

INTERDIT de marquer un HW Gate comme « OK » sans message texte explicite de Loïc contenant le mot **« GO »** ou **« validé »**.

Claude ne peut pas voir le HW (terminal VT100 du setup, comportement runtime LOOP, persistance NVS post-reboot). La phase ne progresse pas tant qu'il n'a pas reçu la confirmation textuelle. En cas de doute, demander.

**Pourquoi le mot exact** : « yes », « ok », « ça marche » sont des validations implicites trop faciles à interpréter par défaut. Le mot strict force Loïc à prendre 1 seconde supplémentaire pour s'engager.

**Phase 3 spécifique** : les HW gates G2-G5 testent l'**UX** du Tool 3 (TAB nav, sous-pages, refus visuel). Phase 3 a moins de risk musical que Phase 2 mais plus de risk UX subtil. Les validations « substantielles » exigent que Loïc décrive textuellement ce qu'il voit (« le grid affiche `R` sur pad 30 dim red », pas juste « GO »).

### Règle 2 — Commit gates : présentation pre-commit obligatoire

AVANT chaque `git commit`, présenter à Loïc :
- `git status --porcelain` ou équivalent (fichiers modifiés + untracked).
- Liste exacte des fichiers à `git add` (jamais `git add .` ni `git add -A`).
- Le message complet en HEREDOC, **sans `Co-Authored-By:`** (override per global CLAUDE.md).

Attendre **« ok commit »** explicite. JAMAIS de commit auto, même en mode autocommit déclaré.

### Règle 3 — Auto-review greps / hard-asserts : assertion bloquante

Quand un grep ou un script bash de vérification est dans une step (ex : `grep -c "applyDevSeedLoopPadsIfSafe" src/ == 0`) :

- Si le résultat ne correspond pas au résultat attendu → **STOP immédiat**.
- Signaler à Loïc avec résultat observé vs attendu.
- Ne **pas** continuer en pensant « ça passera plus tard ».
- Ne **pas** tenter de « corriger en aveugle » la divergence.

**Phase 3 hard-asserts localisés** (zones de survol identifiées par l'audit) :
- Task 23 audit : `grep -c "findControlPadEntryIdx" src/setup/ToolPadRoles.cpp >= 2`.
- Task 26 retrait dev seed : `grep -rn "applyDevSeedLoopPadsIfSafe\|DevSeedLoopPads" src/ == 0`.
- Task 17.5 extension pool : `grep -c "case 6:\|case 7:" src/setup/ToolPadRoles.cpp >= 4`.
- Task 27 doc-sync : 4 grep targets (setup-tools-conventions, nvs-reference, LOOP_PROGRESS, spec LOOP).

### Règle 4 — Lecture intégrale avant édition

Avant chaque édition, exécuter le `Read` tool sur la région cible **complète** (ou le fichier complet s'il est court). Pas de patch à l'aveugle basé sur la mémoire du plan.

Multi-fichiers : lire **tous** les fichiers impactés AVANT le premier edit.

**Phase 3 zones critiques de lecture** :
- Task 11 : Read intégral `ToolPadRoles.cpp` (788 lignes) avant le refacto en sous-pages.
- Task 11.5 : Read `ToolControlPads.cpp:104-117` (pattern `_setFlash`) pour aligner Tool 3.
- Task 13/18 : Read `_drawGridLegacy` extracté avant écrire les versions contextuelles.
- Task 17 : Read NvsManager::loadAll() section LoopPadStore avant insérer validator wire.
- Task 26 : Read NvsManager.cpp:162 + 1196-1230 + main.cpp:538 pour retrait propre.

### Règle 5 — Instructions trompeuses ou contradictoires : STOP et demande

Si une instruction du plan paraît :
- Trompeuse (ex : « supprimer X » mais X n'a jamais été déclaré).
- Contradictoire avec une autre task.
- Référencer un élément qui n'existe pas dans le code.
- Causer une erreur de compilation par type-mismatch documenté.

→ **STOP immédiat**, demander clarification à Loïc. Ne **pas** deviner. Ne **pas** « fixer en silence » la divergence.

**Phase 3 contradiction potentielle déjà identifiée par audit** : la spec LOOP §27 P3 ligne 621 dit « Tool 7 refactor » mais STATUS / LOOP_PROGRESS / scope Phase 3 du plan disent que Tool 7 est Phase 4. Le plan acte « Tool 7 OUT Phase 3 ». Si une référence parasite à Tool 7 apparaît dans une task, STOP + clarifier.

### Règle 6 — TodoWrite : obligatoire

Créer la liste des tasks au début de session :
- 1 entrée par task plan + 1 entrée par HW gate (visible pour Loïc).
- 29 tasks Phase 3 (Tasks 1-27 + 11.5 + 17.5) + 6 HW gates G1-G6.
- Marquer `in_progress` AU MOMENT de commencer la task, pas avant.
- Marquer `completed` IMMÉDIATEMENT après le commit gate validé. Pas de batch.
- Exactement UNE task en `in_progress` à tout moment.

Si TodoWrite est perdu par compression de contexte longue session (constaté Phase 2 × 2) : recréer la liste depuis le plan, ne pas paniquer.

### Règle 7 — Checkpoints inter-phases : annonce + GO obligatoire

Au début de chaque phase ou sous-phase :

```
## PHASE 3.X — CHECKPOINT

Phase précédente : [commit hash + HW gate validé]
Objectif phase 3.X : [résumé 1 ligne]
Coût attendu : [N tasks, ~M builds, 1 HW gate Gn ou pas]
OK pour démarrer ?
```

Attendre **« GO »** explicite avant le premier `Edit`/`Write`. Si « SKIP » ou « DEFER », marquer dans le recap-table multi-axes du plan et ne pas démarrer.

**Phase 3 checkpoints prévus** :
- 3.A → 3.B (post HW G1)
- 3.B → 3.C (post commit Phase 3.B)
- 3.C → 3.D (post HW G2 + commit)
- 3.D → 3.E (post HW G3 + commit)
- 3.E → 3.F (post HW G4 + commit)
- 3.F → 3.G (post HW G5 + commit)
- 3.G → 3.H (post HW G6 + commit)

### Règle 8 — Audit-fix : nouveaux invariants

Le plan a été patché post-audit pour intégrer B-N1 + M1-M5 (cf addendum « Audit-fix integration » dans le plan). Pendant l'exécution :

- Ne **pas** sauter les tasks audit-fix (Tasks 11.5 et 17.5 sont nouvelles, **PAS** dans la numérotation originale).
- Ne **pas** retomber sur l'ancien comportement « silent steal » — Phase 3 introduit flash msg sur swap-to-pool (M1 / M7).
- HW gates G2/G3/G4 incluant des audit-fix tests : tous les steps doivent être validés par Loïc, pas seulement les steps « originaux » du plan.

**Mapping audit-fix → tasks** :
- B-N1 → Task 8 Step 2 (call site `_toolRoles` SetupManager.cpp:30).
- M1 → Task 15 reformulée (flash sur steal existing, pas nouveau mécanisme).
- M2 → **Task 11.5 nouvelle** (`_setFlash` infrastructure Tool 3).
- M3 → Task 11 Step 2 Option A (`_drawGridLegacy()` extracté).
- M4 → **Task 17.5 nouvelle** (extension pool dispatcher LOOP lines 6+7).
- M5 → Task 17 Step 3 (validator wire dans loadAll).

### Règle 9 — Standard de qualité

Cf project CLAUDE.md : ILLPAD est un instrument vendu, joué live. « Prototype » n'est pas une excuse. Si une feature semble « ça marche au premier test », vérifier edge cases.

Interdits :
- TODO / FIXME / XXX dans le code livré (acceptés temporairement dans une session, retirés avant commit final de la phase).
- Traces `Serial.printf` temporaires non retirées (cf hard-assert dédié si applicable).
- Workaround HW non documenté.
- Validation HW sur 1 cas test seul (vérifier edge cases : wrap, overflow, multi-bank, stress).

**Phase 3 spécifique** :
- Phase 3 = UI/setup, pas de path musical critique. Mais : non-régression Tool 3 ARPEG existing est critique (utilisateur a configuré ses scale roles).
- Vérifier persistance NVS post-reboot **systématiquement** quand un assignement est modifié (HW gates G2, G3, G4, G6 incluent ce check).
- Edge case prioritaire EC8 (Tool 3 NORM bank move vers pad occupé) — souvent oublié en test live.

---

## Récap : zones de survol identifiées par l'audit

Le plan a été spécifiquement rigidifié à ces endroits. Si tu y arrives, applique la vigilance maximale :

### Zones identifiées par audit v1 (auto-révision)

| Zone | Risque identifié | Protection ajoutée |
|---|---|---|
| Task 8 Step 2 | Mauvais nom variable (`_padRolesTool` vs `_toolRoles`) → call site `begin()` non trouvé | B-N1 fix v1 : nom + ligne exact SetupManager.cpp:30 (étendu par audit indépendant — cf zones v2) |
| Task 11 | Refacto monolithique 788 lignes : régression UX possible sous-page ARPEG / LOOP pendant Phase 3.C | M3 fix v1 : Option A `_drawGridLegacy()` extracté en stub (downgrade par audit indépendant — voir m14 v2) |
| Tasks 12, 15, 20, 24, 25 | Dépendent toutes de `_setFlash` Tool 3 (infrastructure manquante) | M2 fix v1 : Task 11.5 explicite ajoute infrastructure |
| Task 15 | Risque de dupliquer le swap-to-pool ARPEG (existe déjà silencieusement) | M1 fix v1 : reformulation « flash sur steal existing » |
| Tasks 18-20 | Pool LOOP lines 6+7 non gérées par dispatcher actuel (POOL_LINE_COUNT=6) | M4 fix v1 : Task 17.5 explicite étend dispatcher |
| Task 17 Step 3 | Validator wire dans loadAll() non précisé | M5 fix v1 : emplacement exact spécifié (upgrade B-N3 par audit indépendant — voir zones v2) |
| Task 26 | Collision check post-loadAll emplacement vague | M6 fix v1 : tranché NvsManager::loadAll() à la fin |
| Task 25 (HW G5) | EC9 boot collision pas testé en G5 (placé en G6) | Documenté + accepté |
| Task 27 doc-sync | Drift Tool 7 spec §27 P3 ligne 621 | Action explicit Step 7 |
| Tasks 13/18 dim markers | UTF-8 `·` peut casser le rendu terminal | m9 v1 : fallback ASCII `.` documenté |

### Zones additionnelles identifiées par audit indépendant (v2 refondation)

| Zone | Risque identifié | Protection ajoutée |
|---|---|---|
| Tasks 1, 2 | Signature `findBankIdxForPad(const BankSlot*, ...)` **fausse** — `BankSlot::pad` n'existe pas | **B-N2 fix v2** : signature `(const uint8_t* bankPads, uint8_t pad)`, inline dans KeyboardData.h, Task 2 supprimée |
| Task 17, Task 26 | Validator non appelé sur branche else NVS vide → invariant 12 cassé après retrait dev seed | **B-N3 fix v2** : pré-init 0xFF + validator hors du if (verbatim dans addendum v2 plan) |
| Tasks 11, 13, 18, 20, 23 | Helpers `scaleRoleAtPad(const ScalePadStore&)` et `arpRoleAtPad(const ArpPadStore&)` réfèrent à caches NvsManager **inexistants** | **M8 fix v2** : signatures refondues (helpers prennent arrays main.cpp par référence) — voir spec design §13 refondé |
| Task 8 | B-N1 v1 incomplet — manque 4 dérivés (member `_nvs`, init list, begin assign, SetupManager vérif) | **M13 fix v2** : verbatim étendu dans addendum v2 plan |
| Task 10 | API SetupUI `setInverse` / `moveCursor` **inexistante** → build break | **M14 fix v2** : refonte avec `drawFrameLine` + VT100 escapes inline (verbatim dans addendum v2 plan) |
| Tasks 17 / 26 | Coexistence transitoire dev seed Phase 2 (32/33/34) ↔ defaults Phase 3 (30/31/32) | **M9 fix v2** : commentaire transitoire dans Task 17 |
| Task 17.5 (addendum v1) | Dispatch `assignRole → assignLoopRole` brise pattern existing ARPEG swap | **M10 fix v2** : `assignLoopRole` SOLE responsible swap intra-LOOP, caller `run()` skip clearRole pour line 6/7 |
| Task 21 | `saveAll` partial-fail non spécifié (bank OK + loop fail = état NVS incohérent) | **M11 fix v2** : log warning, accepte best-effort |
| Task 20 / `clearAllRoles` | `clearAllRoles()` reset les 3 LOOP controls → invariant 12 cassé transitoirement | **M12 fix v2** : préserver REC/PS/CLEAR, seul slotPads reset |
| Task 11 (addendum v1 M3) | Factorisation `drawGrid` mal ciblée — `drawGrid()` Tool 3 est 3 lignes triviales | **m14 fix v2** : factorisation cible `buildRoleMap()` (pas `drawGrid`), nouveaux `_buildRoleMapNorm/Arpeg/Loop` |
| Labels grid Tasks 13, 18 | Labels 3-char (`"REC"`, `"S00"`, `"Hd"`) au lieu de 4-char (` REC`, ` S00`, ` Hld`) | **m12 fix v2** : leading space pour alignement 5-char cells |

---

## Une chose importante

Loïc reste le garant final sur tout ce qui est HW (DAW, LED, audio, latence) et sur l'**UX du setup mode** Phase 3. Tu protèges contre le bâclage software (skip steps, traces oubliées, doc-sync approximative, refacto non-régression).

**Spécifique Phase 3** : c'est une phase de **refacto setup mode** sans path musical complexe. La discipline doit se concentrer sur :
1. **Non-régression Tool 3 ARPEG** — l'instrument peut être déjà configuré (scale roles importants). Casser ça = inconvénient majeur pour Loïc.
2. **Persistance NVS** — chaque assignement modifié via UI doit survivre reboot. À tester systématiquement.
3. **Coexistence visuelle** — la grille VT100 doit refléter cleanly les 3 cases (musical / HL ARPEG / HL LOOP) sans rendre la grid illisible. Si après G2/G3/G4 la grid devient confuse, **retravailler le rendu** avant de continuer.
4. **Hard-constraint LOOP controls** — les 3 controls REC/PS/CLEAR doivent **toujours** être assignés en runtime (invariant 12 spec). Test : impossible d'exit Tool 3 si un des 3 == 0xFF.

**Différence avec Phase 2 LOOP** :
- Phase 2 = runtime musical critique → tests « entendre le MIDI », « stuck notes ?», « overdub propre ?».
- Phase 3 = setup config → tests « le grid affiche correctement », « persistance NVS », « collision visible ».

En cas de doute, demande. Le coût d'une question (30 secondes) est largement inférieur au coût d'un commit foireux à reverter (heures + perte de confiance + risque de casser une config user existante).

---

**Manifeste validé 2026-05-19**. À lire en tête de la session EXEC Phase 3.
