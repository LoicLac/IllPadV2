# Manifeste de session — exécution Phase 2 LOOP

À coller en tête de la session d'exécution. Ces règles surclassent les défauts par défaut de Claude (complaisance, sign-off sans evidence, batch sans gate).

**Plan référence** : [`2026-05-18-loop-phase-2-plan.md`](2026-05-18-loop-phase-2-plan.md)
**Audit source** : audit final adversarial 2026-05-18 (bloquants B-N1/B-N2, runtime R-N1 intégrés dans le plan)

---

## Règles strictes pour cette session

### 1. HW gates : confirmation explicite obligatoire

INTERDIT de marquer un HW Gate (G1 à G9) comme « OK » sans message texte explicite de Loïc contenant le mot « GO » ou « validé ».

Tu ne peux pas voir le HW. Tu ne peux pas observer le DAW. Tu ne peux pas vérifier l'absence de stuck notes. La phase ne progresse pas tant que tu n'as pas reçu la confirmation explicite. En cas de doute, demande.

### 2. Commit gates : présentation pre-commit obligatoire

AVANT chaque `git commit`, présente à Loïc :
- `git status -uno` (ou équivalent) pour voir les fichiers modifiés.
- Liste exacte des fichiers à `git add` (jamais `git add .` ni `git add -A`).
- Le message HEREDOC complet.

Attendre « ok commit » explicite. JAMAIS de commit auto, même en mode autocommit.

### 3. Auto-review greps : assertion bloquante

Quand un grep de vérification est dans une step (par exemple `grep -c "static_assert" doit retourner 1`, ou les hard-asserts bash dans Task 19/20/36) :

- Si le résultat ne correspond pas au résultat attendu → **STOP immédiat**.
- Signale à Loïc avec le résultat observé vs attendu.
- Ne continue PAS à la step suivante en pensant « ça passera plus tard ».
- Ne tente PAS de « corriger en aveugle » la divergence.

### 4. Tâches « Read full target region » : lecture intégrale obligatoire

Avant chaque édition, exécute le `Read` tool sur la région cible **complète** (ou le fichier complet s'il est court). Pas de patch à l'aveugle basé sur la mémoire du plan.

Multi-fichiers : lire **tous** les fichiers impactés AVANT le premier edit.

Si le plan dit « Edit in-place, do NOT replace », respecter strictement — ne pas réécrire le bloc.

### 5. Instructions trompeuses ou contradictoires : STOP et demande

Si une instruction du plan paraît :
- Trompeuse (cf Task 20 step 2 « supprimer X » mais X n'a jamais été déclaré).
- Contradictoire avec une autre task.
- Référencer un élément qui n'existe pas dans le code.

→ **STOP immédiat**, demande clarification à Loïc. Ne devine pas. Ne « fixe » pas en silence.

### 6. TodoWrite : obligatoire

Crée la liste des tasks au début de session (36 tasks + Task 16.5 + Task 34.5 + Task 36 = ~38 entrées, plus 9 HW gates G1-G9 marqués explicitement).

- Marque `in_progress` AU MOMENT de commencer la task, pas avant.
- Marque `completed` IMMÉDIATEMENT après le commit gate validé. Pas de batch.
- Exactement UNE task en `in_progress` à tout moment.

Loïc lit cette liste pour suivre l'avancement et peut t'interrompre si tu dérives.

### 7. Checkpoints inter-phases : annonce + GO obligatoire

Au début de chaque phase (2.A, 2.B, ..., 2.J, puis Task 36) :

Présente à Loïc :
```
## PHASE 2.X — CHECKPOINT

Phase précédente : [commit hash + HW gate validé / DEFER / SKIP]
Objectif phase 2.X : [résumé 1 ligne]
Coût attendu : [N tasks, ~M builds, 1 HW gate ou pas]
OK pour démarrer ?
```

Attendre « GO » explicite avant le premier `Edit`/`Write`. Si « SKIP » ou « DEFER », marquer dans le recap-table multi-axes du plan et ne pas démarrer.

### 8. Audit-fix B-N1/B-N2/R-N1 : nouveaux invariants

Le plan a été patché post-audit pour intégrer 3 fix critiques (Task 34.5 + modifications Task 17/18/24). Pendant l'exécution :

- Ne **pas** sauter Task 34.5 (elle est entre Task 34 et Task 35).
- Ne **pas** retomber sur l'ancien tracker `_padHeldInRec` — le rename vers `_padHeldLive` est appliqué partout dans le plan.
- HW Gate G9 procedure inclut maintenant 11 steps (steps 9, 10, 11 = tests audit-fix). Tous doivent être validés par Loïc.

### 9. Standard de qualité

Cf project CLAUDE.md : ILLPAD est un instrument vendu, joué live. « Prototype » n'est pas une excuse. Si une feature semble « ça marche au premier test », vérifier edge cases. Pas de TODO/FIXME dans le code livré.

---

## Récap : les 4 zones de survol identifiées par l'audit

Le plan a été spécifiquement rigidifié à ces endroits. Si tu y arrives, applique la vigilance maximale :

| Zone | Risque | Protection ajoutée |
|---|---|---|
| Task 19 step 4 | Trace temporaire `[LOOP REC CLOSED]` non retirée avant commit Phase 2.E | Hard-assert bash : `grep -c "LOOP REC CLOSED"` doit retourner 0, sinon exit 1 |
| Task 20 step 2 | Instruction trompeuse « supprimer prototype qui n'existe pas » | Reformulée en hard-assert vérifiant ABSENCE du prototype |
| Task 34.5 (nouvelle) | Fix audit B-N1/R-N1 (méthode `onBackgroundTransition` + call main.cpp) | Step explicite avec auto-review grep ≥ 3 occurrences |
| Task 36 step 7 | Doc-sync 6 fichiers — bâclage approximatif | Hard-assert bash : grep par fichier pour keyword attendu, sinon exit 1 |

---

## Une chose importante

**Loïc reste le garant final** sur tout ce qui est HW. Tu ne peux pas confirmer qu'il n'y a pas de stuck note au DAW, qu'une LED s'allume bien en coral pendant RECORDING, qu'une boucle wrap correctement à 120 BPM. Lui valide. Toi tu protèges contre le bâclage software (skip steps, traces oubliées, doc-sync approximative).

En cas de doute, demande. Le coût d'une question (30 secondes) est largement inférieur au coût d'un commit foireux à reverter (heures + perte de confiance).
