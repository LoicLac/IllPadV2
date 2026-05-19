# Prompt d'ouverture session EXEC — Phase 3 LOOP

À copier-coller en tête d'une nouvelle session Claude Code pour exécuter Phase 3.

---

```
Tu es Claude Code. Tu vas exécuter la Phase 3 LOOP du firmware ILLPAD V2
(instrument embedded ESP32-S3 vendu et joué live — pas un prototype, qualité
finale attendue).

Objectif Phase 3 : Tool 3 b1 contextuel (3 sous-pages NORM/ARPEG/LOOP via TAB)
+ Tool 4 ext (refus ControlPad sur pad LOOP control) + retrait dev seed M7
(remplacement par defaults LOOP 30/31/32 hardcodés dans validator).

═══════════════════════════════════════════════════════════════════════════
LECTURE OBLIGATOIRE AVANT TOUTE ACTION (dans cet ordre)
═══════════════════════════════════════════════════════════════════════════

1. docs/superpowers/plans/2026-05-19-loop-phase-3-session-manifest.md
   → règles strictes 9 + zones de survol + une chose importante
2. docs/superpowers/plans/2026-05-19-loop-phase-3-plan.md
   → 29 tasks (Tasks 1-27 + 11.5 + 17.5), 6 HW gates G1-G6, 8 commits
   → ADDENDUM en fin de plan : audit-fix integration B-N1 + M1-M5 (CRITIQUE)
3. docs/superpowers/plans/2026-05-19-loop-phase-3-plan_AUDIT.md
   → 19 findings (1 B-N + 7 M + 11 m), section "Findings à intégrer"
4. docs/superpowers/specs/2026-05-19-loop-phase-3-design.md
   → spec design (référence si doute sur les 5 règles collision + EC1-EC9)
5. .claude/CLAUDE.md (auto-loadé) + ~/.claude/CLAUDE.md (auto-loadé)
6. docs/superpowers/SESSION_PROTOCOL.md (référence patterns + templates)

═══════════════════════════════════════════════════════════════════════════
ÉTAT COURANT DU REPO
═══════════════════════════════════════════════════════════════════════════

- Branche : main
- HEAD : 7aec5b6 (audit-fix integration, après commits spec design 6768189 +
  plan 2ea9da9 + audit 7aec5b6)
- État NVS connu de Loïc : LoopPadStore avec dev seed M7 actif (recPad=32,
  playStopPad=33, clearPad=34 si dev seed appliqué Phase 2). Sera remplacé
  par defaults 30/31/32 en Task 17 (validator) + Task 26 (retrait dev seed).
- Phase 2 close : 10 commits 6c0b4d8 → 284bec4, HW gates G1-G9 Phase 2 validés
  2026-05-19. 1er son MIDI LOOP audible commit d345f01.
- Build clean : RAM ~29 %, Flash ~22 % (référence Phase 2 close).

═══════════════════════════════════════════════════════════════════════════
RÈGLES STRICTES (résumé — détail dans le manifeste)
═══════════════════════════════════════════════════════════════════════════

R1. HW gates : confirmation "GO" ou "validé" explicite de Loïc obligatoire.
R2. Commit gates : present git status + files + HEREDOC, attendre "ok commit"
    explicite. Pas de Co-Authored-By: dans les messages.
R3. Hard-asserts bash bloquants : STOP si grep ne matche pas attendu.
R4. Read intégral fichier ciblé AVANT chaque édition. Lire tous les fichiers
    impactés AVANT le premier edit en multi-fichier.
R5. Instruction trompeuse / contradictoire → STOP, demander à Loïc.
R6. TodoWrite : 29 tasks + 6 HW gates, 1 in_progress max, completed
    immédiatement après commit gate validé.
R7. Checkpoint inter-phases : annonce + "GO" explicite avant 1er Edit.
R8. Audit-fix Phase 3 : Tasks 11.5 et 17.5 SONT dans le plan (cf addendum).
    Comportement "silent steal → flash steal" (M1/M7).
R9. Standard de qualité : pas de TODO/FIXME, pas de traces temporaires
    oubliées, pas de workaround HW non documenté.

═══════════════════════════════════════════════════════════════════════════
OUTILS BUILD / HW
═══════════════════════════════════════════════════════════════════════════

Build :
  ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1

Upload :
  ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload

Monitor :
  ~/.platformio/penv/bin/pio device monitor -b 115200

Terminal VT100 setup mode :
  python ItermCode/vt100_serial_terminal.py
  (Permet d'interagir avec le mode setup via VT100 — utilisé pour HW gates
   G2/G3/G4/G5/G6 qui testent Tool 3 et Tool 4 UI.)

`pio` n'est PAS dans le PATH — toujours utiliser le full path.

═══════════════════════════════════════════════════════════════════════════
PHASES PRÉVUES (recap)
═══════════════════════════════════════════════════════════════════════════

| Sous-phase | Tasks | HW Gate | Commit |
|---|---|---|---|
| 3.A — Helpers cross-store + getters NvsManager | 1-3 | — | 1 |
| 3.B — Tool 4 ext (refus ControlPad sur LOOP control) | 4-6 | G1 | 1 |
| 3.C — Tool 3 refacto nav TAB + _setFlash infra + NORM | 7-12 (+ 11.5) | G2 | 1 |
| 3.D — Tool 3 sous-page ARPEG | 13-16 | G3 | 1 |
| 3.E — Tool 3 LOOP + defaults validator + hard-constraint + pool ext | 17-22 (+ 17.5) | G4 | 1 |
| 3.F — Validation collision cross-tool | 23-25 | G5 | 1 |
| 3.G — Retrait dev seed M7 + collision check post-loadAll | 26 | G6 | 1 |
| 3.H — Doc-sync 7 fichiers | 27 | — | 1 |

Total : 29 tasks, 6 HW gates, 8 commits.

═══════════════════════════════════════════════════════════════════════════
PREMIÈRE ACTION
═══════════════════════════════════════════════════════════════════════════

1. Lire le manifeste session (le doc complet, pas juste survoler).
2. Lire le plan Phase 3 — sections Header + Scope + Décisions actées + File
   structure + Recap-table multi-axes + Addendum audit-fix (en fin de plan).
3. Lire le projet CLAUDE.md (auto-loadé, mais à parcourir intentionnellement
   pour les invariants 1-14 + la politique NVS Zero Migration).
4. Créer la TodoWrite list avec 29 tasks + 6 HW gates.
5. Présenter le CHECKPOINT Phase 3.A :

   ## PHASE 3.A — CHECKPOINT

   Phase précédente : Phase 2 LOOP CLOSE (commit 284bec4, HW gates G1-G9
   validés 2026-05-19). Spec + plan + audit Phase 3 commités (HEAD 7aec5b6).

   Objectif Phase 3.A : helpers cross-store inline dans KeyboardData.h
   (5 helpers : isLoopControlPad, findLoopSlotIdx, findControlPadEntryIdx,
   scaleRoleAtPad, arpRoleAtPad) + getter manquants NvsManager. Pas de runtime
   impact (compile-only).

   Coût attendu :
   - 3 tasks (Tasks 1-3)
   - ~3-4 builds
   - **Pas de HW gate** (compile only)
   - 1 commit Phase 3.A

   Note : Phase 3 est un refacto setup mode, pas de path musical critique. La
   discipline se concentre sur non-régression Tool 3 ARPEG existing,
   persistance NVS, coexistence visuelle grid VT100, et hard-constraint LOOP
   controls.

   **OK pour démarrer ?**

6. Attendre **"GO"** explicite de Loïc avant le premier Edit.

═══════════════════════════════════════════════════════════════════════════
COMMENT EXÉCUTER UNE TASK (workflow 5-gates par task)
═══════════════════════════════════════════════════════════════════════════

Pour chaque task :
a. Marquer task `in_progress` dans TodoWrite.
b. Read intégral du fichier ciblé (Règle 4).
c. Code (Edit / Write).
d. Build → exit 0, 0 warning (gate compile).
e. Auto-review (grep + assertions selon le plan).
f. Hard-assert si présent dans la task (Règle 3 : STOP si échec).
g. Marquer task `completed` SEULEMENT après le commit gate validé (pas avant).

Quand HW gate atteint :
a. Présenter procédure HW (build OK + upload + monitor + procédure de test
   + critères Gn).
b. Attendre "GO" / "validé" explicite (Règle 1).
c. Marquer le HW gate completed dans TodoWrite.

Quand commit gate atteint :
a. Présenter git status --porcelain + liste fichiers à add + HEREDOC.
b. Attendre "ok commit" (Règle 2).
c. Commit + git log --oneline -3 + git status (verify).
d. Marquer tasks de la sous-phase completed dans TodoWrite.

═══════════════════════════════════════════════════════════════════════════

En cas de doute : demander. Le coût d'une question (30 secondes) est largement
inférieur au coût d'un commit foireux à reverter.

Bonne session.
```

---

**Notes pour la session PREP (à supprimer de ce doc avant copy-paste)** :

- Le prompt ci-dessus est autonome — pas de référence à la conversation PREP.
- Le HEAD au moment de la session EXEC peut différer si d'autres commits sont faits
  entre temps. Mettre à jour avant copy-paste si nécessaire.
- Si Phase 4 ou autre travail intervient avant Phase 3 EXEC, valider que le base
  Phase 2 LOOP CLOSE est toujours d'actualité (rien n'a régressé).
- Le prompt suppose une nouvelle session Claude Code clean — pas de contexte
  partagé avec la session PREP.

**Validation finale PREP Phase 3** :
- [x] Spec design Phase 3 commité (6768189).
- [x] Plan détaillé Phase 3 commité (2ea9da9).
- [x] Audit adversarial + fix integration commités (7aec5b6).
- [x] Manifeste session écrit.
- [x] Prompt d'ouverture EXEC écrit.

**Prêt pour Phase 3 EXEC**.
