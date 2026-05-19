# Session Protocol — Discipline d'exécution embedded ILLPAD V2

**Document de référence** pour exécuter une phase multi-tasks avec HW validation.
Codifie le protocole qui a livré Phase 2 LOOP (10 commits, 9 HW gates G1-G9, 0
warning, 0 TODO, 18+ audit-fix appliqués) sans dérive complaisance.

**Maintenu** : à mettre à jour quand un pattern nouveau émerge ou qu'un anti-pattern
non-listé est identifié en post-mortem.

---

## Quand utiliser ce protocole

**Cas d'usage** : exécution d'un **plan multi-tasks (10+ tasks)** avec :
- HW validation requise (test sur instrument, observation visuelle/audio),
- Multiples commits intermédiaires,
- Audit-fix prévus (zones de survol identifiées),
- Risque de dérive sur la durée (session > 2 h).

**À éviter pour** : single fix trivial, exploration / brainstorming, refactor < 3
fichiers sans HW validation. Le surcoût du protocole ne se justifie que sur
charge cognitive longue où la discipline se relâche naturellement.

---

## Vue d'ensemble du cycle

```
Session PREP                       Session EXEC
─────────────                      ─────────────
brainstorm                         lire manifeste + plan
   ↓                                   ↓
spec design                        TodoWrite (38 entrées Phase 2)
   ↓                                   ↓
plan détaillé (tasks + HW gates)   checkpoint phase N.A
   ↓                                   ↓ GO
audit adversarial du plan           exécuter task 1
   ↓                                   ↓
manifeste session (9 règles)        build → auto-review → ...
   ↓                                   ↓
prompt d'ouverture session EXEC    HW gate G1 → attendre "GO/validé"
                                        ↓
                                    commit gate → attendre "ok commit"
                                        ↓
                                    checkpoint phase N.B → ...
                                        ↓
                                    Task 36 doc-sync
```

**Règle absolue** : session EXEC ne démarre **jamais** sans plan + audit + manifeste
livrés. Cf "Anti-patterns" §1.

---

## Les 9 règles strictes du manifeste

Ces règles sont à inclure verbatim (ou adaptées) dans le manifeste de chaque phase.
Posées **en tête du prompt** session EXEC pour qu'elles soient visibles avant la
1re action.

### Règle 1 — HW gates : confirmation explicite obligatoire

INTERDIT de marquer un HW Gate comme « OK » sans message texte explicite de Loïc
contenant le mot **« GO »** ou **« validé »**.

Claude ne peut pas voir le HW. Il ne peut pas observer le DAW, vérifier l'absence
de stuck notes au son, observer une couleur LED. La phase ne progresse pas tant
qu'il n'a pas reçu la confirmation textuelle. En cas de doute, demander.

**Pourquoi le mot exact** : « yes », « ok », « ça marche » sont des validations
implicites trop faciles à interpréter par défaut. Le mot strict force Loïc à
prendre 1 seconde supplémentaire pour s'engager — c'est précisément cette pause
qui empêche la complaisance partagée.

**Faiblesse à noter** : la règle tient sur le mot, pas sur la **substance**. Si
Loïc dit « GO » sans avoir réellement testé, le gate passe. À renforcer pour
milestones critiques par une exigence supplémentaire (log audio, screenshot LED,
texte « j'ai entendu »).

### Règle 2 — Commit gates : présentation pre-commit obligatoire

AVANT chaque `git commit`, présenter à Loïc :
- `git status --porcelain` ou équivalent (fichiers modifiés + untracked).
- Liste exacte des fichiers à `git add` (jamais `git add .` ni `git add -A`).
- Le message complet en HEREDOC.

Attendre **« ok commit »** explicite. JAMAIS de commit auto, même en mode
autocommit déclaré.

**Pourquoi** : un commit déjà fait est techniquement revertable mais socialement
acquis (le commit hash apparaît dans le log, l'historique se construit). Mieux
vaut 30 secondes de présentation que des reverts qui font remonter de la dette
de discipline.

### Règle 3 — Auto-review greps / hard-asserts : assertion bloquante

Quand un grep ou un script bash de vérification est dans une step
(ex : `grep -c "TODO" == 0`, `grep -c onBackgroundTransition >= 3`) :

- Si le résultat ne correspond pas au résultat attendu → **STOP immédiat**.
- Signaler à Loïc avec résultat observé vs attendu.
- Ne **pas** continuer en pensant « ça passera plus tard ».
- Ne **pas** tenter de « corriger en aveugle » la divergence.

**Pourquoi les hard-asserts en bash** : externalisent la vérification à un script
extérieur, indépendant du jugement subjectif. Plus puissant qu'une discipline
pure « fais bien » parce que la machine décide, pas l'humain.

**Localiser les hard-asserts aux zones de survol** : les endroits du plan où
Claude est statistiquement le plus susceptible de bâcler ou sauter (traces
temporaires à retirer, doc-sync multi-fichiers, audit-fix critiques).

### Règle 4 — Lecture intégrale avant édition

Avant chaque édition, exécuter le `Read` tool sur la région cible **complète**
(ou le fichier complet s'il est court). Pas de patch à l'aveugle basé sur la
mémoire du plan.

Multi-fichiers : lire **tous** les fichiers impactés AVANT le premier edit.

Si le plan dit « Edit in-place, do NOT replace », respecter strictement — ne pas
réécrire le bloc entier.

**Pourquoi** : le fichier réel peut avoir dérivé depuis l'écriture du plan
(numéros de ligne stale, structure modifiée). Lire avant édition élimine 80 %
des bugs d'édition non-conformes.

### Règle 5 — Instructions trompeuses ou contradictoires : STOP et demande

Si une instruction du plan paraît :
- Trompeuse (ex : « supprimer X » mais X n'a jamais été déclaré).
- Contradictoire avec une autre task.
- Référencer un élément qui n'existe pas dans le code.
- Causer une erreur de compilation par type-mismatch documenté.

→ **STOP immédiat**, demander clarification à Loïc. Ne **pas** deviner. Ne **pas**
« fixer en silence » la divergence.

**Exemple Phase 2** : Task 1 a déclaré `uint8_t _overdubCount` mais Task 17 step 4
appelait `insertEventSorted(_overdubEvents, _overdubCount, ...)` qui prend
`uint16_t&`. Type-mismatch détecté au build. STOP + présentation 2 options A/B
→ Loïc tranche → fix appliqué.

Sans cette règle, j'aurais peut-être casté silencieusement ou changé la signature
de insertEventSorted, créant une divergence non-documentée.

### Règle 6 — TodoWrite : obligatoire

Créer la liste des tasks au début de session :
- 1 entrée par task plan + 1 entrée par HW gate (visible pour Loïc).
- Marquer `in_progress` AU MOMENT de commencer la task, pas avant.
- Marquer `completed` IMMÉDIATEMENT après le commit gate validé. Pas de batch.
- Exactement UNE task en `in_progress` à tout moment.

Loïc lit cette liste pour suivre l'avancement et peut interrompre si Claude
dérive.

**Faiblesse identifiée Phase 2** : la liste peut être perdue par compression de
contexte longue session. Constaté 2× en Phase 2. **Workaround** : recréer la
liste si perdue, n'utiliser pas TodoWrite comme source unique de vérité — le
plan + manifeste restent la source canonique. La liste est un aide-mémoire visuel.

### Règle 7 — Checkpoints inter-phases : annonce + GO obligatoire

Au début de chaque phase ou sous-phase :

```
## PHASE X.Y — CHECKPOINT

Phase précédente : [commit hash + HW gate validé / DEFER / SKIP]
Objectif phase X.Y : [résumé 1 ligne]
Coût attendu : [N tasks, ~M builds, 1 HW gate ou pas]
OK pour démarrer ?
```

Attendre **« GO »** explicite avant le premier `Edit`/`Write`. Si « SKIP » ou
« DEFER », marquer dans le recap-table multi-axes du plan et ne pas démarrer.

**Pourquoi** : crée un battement régulier qui permet à Loïc de re-cadrer ou
re-prioriser sans avoir à interrompre une session en cours. Aussi : oblige
Claude à formuler le coût attendu, ce qui rend visibles les engagements.

### Règle 8 — Audit-fix : nouveaux invariants

Si le plan a été patché post-audit pour intégrer des fix critiques (ex : B-N1,
B-N2, R-N1 en Phase 2), pendant l'exécution :

- Ne **pas** sauter les tasks audit-fix (souvent insérées entre les tasks
  normales, ex : Task 34.5 entre 34 et 35).
- Ne **pas** retomber sur l'ancien nommage si un rename a eu lieu post-audit
  (ex : `_padHeldInRec` → `_padHeldLive`).
- HW gates incluant des audit-fix tests : tous les steps doivent être validés
  par Loïc, pas seulement les steps "originaux" du plan.

### Règle 9 — Standard de qualité

Cf project CLAUDE.md : ILLPAD est un instrument vendu, joué live. « Prototype »
n'est pas une excuse. Si une feature semble « ça marche au premier test »,
vérifier edge cases.

Interdits :
- TODO / FIXME / XXX dans le code livré (acceptés temporairement dans une session,
  retirés avant commit final de la phase).
- Traces `Serial.printf` temporaires non retirées (cf hard-assert dédié si applicable).
- Workaround HW non documenté.
- Validation HW sur 1 cas test seul (vérifier edge cases : wrap, overflow,
  multi-bank, stress).

---

## Templates réutilisables

### Manifeste de session — template

À adapter pour chaque phase. Garder les 9 règles, modifier les sections
"Récap zones de survol" et "Une chose importante".

```markdown
# Manifeste de session — exécution Phase X.Y

**Plan référence** : [`plans/YYYY-MM-DD-phaseXY-plan.md`](...)
**Audit source** : audit adversarial YYYY-MM-DD (bloquants B-N* / runtime R-N*
intégrés dans le plan)

---

## Règles strictes pour cette session

[copier les 9 règles ci-dessus verbatim ou avec ajustements minor par phase]

---

## Récap : zones de survol identifiées par l'audit

Le plan a été spécifiquement rigidifié à ces endroits. Si tu y arrives,
applique la vigilance maximale :

| Zone | Risque | Protection ajoutée |
|---|---|---|
| Task N step M | [risque concret] | [hard-assert ou structure du plan] |
...

---

## Une chose importante

Loïc reste le garant final sur tout ce qui est HW (DAW, LED, audio, latence).
Tu protèges contre le bâclage software (skip steps, traces oubliées, doc-sync
approximative).

En cas de doute, demande. Le coût d'une question (30 secondes) est largement
inférieur au coût d'un commit foireux à reverter (heures + perte de confiance).
```

### Prompt d'ouverture de session EXEC — structure

```
Tu es Claude Code. Tu vas exécuter la Phase X.Y du firmware ILLPAD V2
(instrument embedded ESP32-S3 vendu et joué live — pas un prototype, qualité
finale attendue). Objectif Phase X.Y : [résumé 1 ligne].

═══════════════════════════════════════════════════════════════════════════
LECTURE OBLIGATOIRE AVANT TOUTE ACTION (dans cet ordre)
═══════════════════════════════════════════════════════════════════════════

1. [path-vers-manifeste-session.md] — règles strictes
2. [path-vers-plan.md] — tasks détaillées
3. .claude/CLAUDE.md + ~/.claude/CLAUDE.md (auto-loadé)
4. docs/superpowers/SESSION_PROTOCOL.md (référence patterns)

═══════════════════════════════════════════════════════════════════════════
ÉTAT COURANT DU REPO
═══════════════════════════════════════════════════════════════════════════

- Branche : main (ou autre si exception documentée projet CLAUDE.md)
- HEAD : [commit hash + résumé]
- État NVS / HW connu de Loïc

═══════════════════════════════════════════════════════════════════════════
RÈGLES STRICTES (résumé — détail dans le manifeste)
═══════════════════════════════════════════════════════════════════════════

[9 règles en 1-2 lignes chacune]

═══════════════════════════════════════════════════════════════════════════
OUTILS BUILD / HW
═══════════════════════════════════════════════════════════════════════════

[commandes pio run / upload / monitor avec full paths]

═══════════════════════════════════════════════════════════════════════════
PREMIÈRE ACTION
═══════════════════════════════════════════════════════════════════════════

1. Lire le manifeste session.
2. Lire les sections du plan (lignes X-Y environ).
3. Créer la TodoWrite list.
4. Présenter le CHECKPOINT Phase X.A.
5. Attendre "GO" explicite avant tout Edit.
```

### HW gate — formulation type

Après le code livré et le build vert, présenter :

```
## HW GATE Gn — procédure

**Build** : OK, RAM XX % / Flash XX % (delta vs commit précédent : +/- B).

**Upload + monitor** :
[commandes]

**Pré-requis HW** : [setup nécessaire, config Tool 5 / DAW / etc.]

**Procédure de test** :
1. [step 1 concret]
2. [step 2 concret]
...

**Critères Gn** :
- ✓ [observable 1]
- ✓ [observable 2]
...

Autorise l'upload, teste, et réponds **GO** ou **validé** quand le boot/playback
est OK pour que je puisse présenter le commit gate.
```

### Hard-assert bash — template

```bash
PROTO_COUNT=$(grep -E -c "PATTERN" /path/to/file)
if [ "$PROTO_COUNT" -ne EXPECTED ]; then
  echo "FAIL: $file expected EXPECTED matches, got $PROTO_COUNT."
  exit 1
fi
echo "PASS: $file matches expected ($PROTO_COUNT)."
```

Variantes :
- Trace temporaire à retirer : `grep -c "TEMP_TRACE_TAG" file == 0`
- Symbole obligatoirement défini : `grep -c "void Class::method(" file == 1`
- Couverture cross-fichiers : `grep -c "symbol" file1 file2 file3 >= N`

**Règle** : un hard-assert ne fail JAMAIS sans STOP + signalement. Pas de fix
silencieux.

### Commit gate — séquence type

```
## COMMIT GATE Phase X.Y

**git status** :
```
[output git status --porcelain]
```

**Fichiers à add (N)** :
- `path1`
- `path2`
...

**Message proposé** :

```
[HEREDOC complet avec sujet + corps + sections optionnelles]
```

Pas de `Co-Authored-By:`. [si applicable per CLAUDE.md global]

**Tu confirmes "ok commit" ?**
```

Après « ok commit » :

```bash
git add path1 path2 path3 && git commit -m "$(cat <<'EOF'
[message]
EOF
)" && echo "---" && git log --oneline -N && git status
```

Vérifier sortie : commit hash, working tree clean (sauf untracked hors-scope).

### Checkpoint inter-phases — template

```
## PHASE X.Y — CHECKPOINT

**Phase précédente** : [commit hash + résumé]. HW gate G(n-1) validé : [résumé
ce qui a été observé].

**Objectif Phase X.Y** : [paragraphe explicatif, mentionne audit-fix ou décisions
acted s'il y en a].

**Coût attendu** :
- N tasks (Tasks A → B)
- ~M builds
- **1 HW gate Gn** : [critère observable HW]
- 1 commit gate Phase X.Y

**Note importante** : [edge cases, dépendances, choses à savoir avant de commencer]

**OK pour démarrer ?**
```

### Convention nommage audit-fix

Audit adversarial pré-exécution identifie 3 catégories :

| Préfixe | Sévérité | Sémantique | Exemple |
|---|---|---|---|
| **B-N** | Bloquant (Nouveau) | Bug invariant-violating, doit être fixé avant commit phase | B-N1 stuck note bank switch |
| **M** | Majeur | Bug fonctionnel sérieux, fix urgent | M2 live-sort, M4 atomic merge |
| **m** | Mineur | Optimisation, clean-up, pas critique | m1 static_assert, m9 telemetry |

Le numéro est séquentiel dans la catégorie (B-N1, B-N2, B-N3...) sans
sous-numérotation. Le mapping doit apparaître dans le manifeste de session et
dans les commit messages des phases qui appliquent le fix.

---

## Workflow PREP → EXEC

### Session PREP (durée typique : 2-4 h)

**Output minimum viable** :
1. **Spec design doc** (si la spec haut niveau n'est pas suffisante) — 1-3 pages.
2. **Plan détaillé** — multi-pages, structure :
   - Header (objectif, architecture, décisions actées avant code)
   - File structure (CREATE / MODIFY par fichier avec responsabilité)
   - Build / HW workflow (5 gates par task)
   - Recap-table multi-axes (phases × tasks × compile × HW × commit)
   - Tasks numérotées (verbatim code, hard-asserts, build gates)
   - HW gates intercalés
   - Self-review final
3. **Audit adversarial du plan** — relecture critique avec posture STOP-and-find,
   pas de complaisance. Sortie : liste de B-N*, M*, m* à intégrer.
4. **Manifeste session** — template ci-dessus rempli pour la phase.
5. **Prompt d'ouverture** — pour la session EXEC.

**Ordre** : brainstorm (questions ouvertes) → spec → plan → audit → manifeste →
prompt. Si questions ouvertes substantielles, brainstorm dédié en amont.

### Session EXEC (durée typique : 4-8 h pour 30+ tasks)

**Ordre** :
1. Claude lit manifeste + plan opening + project CLAUDE.md.
2. Claude crée TodoWrite.
3. Claude présente CHECKPOINT Phase X.A → attend GO.
4. Pour chaque task :
   a. Lire la région cible (règle 4).
   b. Code (Edit / Write).
   c. Build → exit 0, 0 warning.
   d. Auto-review (grep + assertions).
   e. Hard-assert si présent.
   f. Marquer task completed.
5. Quand HW gate atteint : présenter procédure → attendre GO/validé.
6. Quand commit gate atteint : présenter git status + files + HEREDOC → attendre
   « ok commit » → commit + verify.
7. Itérer CHECKPOINT → tasks → gates → commit jusqu'à fin de phase.
8. Task doc-sync finale.

---

## Anti-patterns à éviter

### 1. Démarrer EXEC sans PREP livrée

**Tentation** : « le plan est en tête, ça suffit, je le rédigerai au fur et à mesure ».

**Conséquence** : la rigueur s'effondre dès la première décision non-cadrée. Les
audit-fix critiques (B-N1 etc.) ne sont jamais découverts → bugs en HW gate →
revert.

**Remède** : refuser de démarrer EXEC tant que les 5 outputs PREP ne sont pas livrés.

### 2. Interpréter « yes » / « ok » comme validation HW

**Tentation** : Loïc est concis, son intent est clair, je peux passer.

**Conséquence** : la règle 1 s'érode. Au prochain HW gate, accepter « ça a l'air bon »
devient acceptable. Spirale de complaisance.

**Remède** : demander explicitement le mot « GO » ou « validé » à chaque gate,
même si ça semble pédant. La friction est le mécanisme.

### 3. Fixer une contradiction du plan en silence

**Tentation** : le plan a un type-mismatch trivial, je le corrige inline sans signaler.

**Conséquence** : divergence non-documentée entre plan et code. Future audit /
review ne sait pas pourquoi le code diverge. Dette de discipline.

**Remède** : STOP, présenter 2-3 options, attendre l'arbitrage. Documenter le
choix dans le commit message ou le manifeste.

### 4. Batcher des commits ou des HW gates

**Tentation** : « les 3 prochaines tasks sont triviales, je vais tout faire puis
présenter un seul commit gate ».

**Conséquence** : si un bug se glisse au milieu, le revert est plus large + le
commit message devient flou.

**Remède** : 1 phase = 1 commit. Phase regroupe N tasks cohérentes. Pas de
multi-phase par commit.

### 5. Sauter l'audit avant exécution

**Tentation** : « le plan a déjà été relu, l'audit prend 1 h supplémentaire, on
y va ».

**Conséquence** : les bugs invariant-violating (stuck notes, race conditions,
overflow) ne sont pas détectés. Ils émergent en HW gate avec coût × 10 à fixer
en exécution.

**Remède** : audit adversarial obligatoire avant chaque phase EXEC. Posture
STOP-and-find, pas de complaisance. Documenter les B-N*/M*/m* identifiés dans
le manifeste session.

### 6. Hack en main loop plutôt que root cause

**Tentation** : un bug visible peut être masqué par un poll/comparaison externe.
1 ligne de code, ça marche, on y va.

**Conséquence** : couplage architectural (la layer haut consume état bas pour
deviner les transitions). Future modification du bas casse silencieusement le haut.

**Remède** : identifier la **source** du signal manquant, ajouter un flag/enum
émis là où la décision est prise, consommé là où le side-effect doit s'appliquer.
Cf WaitingExit pattern Phase 2 LOOP.

### 7. Commit message bâclé

**Tentation** : le code est bon, le message est secondaire.

**Conséquence** : 6 mois plus tard, impossible de comprendre pourquoi un commit
a été fait. Le `git log` devient illisible.

**Remède** : message structuré (sujet 50 chars, corps détaille décisions
techniques et liens spec). Référencer le commit avec :
- audit-fix appliqués (B-N1, M9, etc.)
- décisions Q* du spec §28
- HW gate validé
- spec sections couvertes

---

## Lessons learned Phase 2 LOOP (référence)

### Ce qui a marché remarquablement

1. **Manifeste en tête de prompt** : externalise la discipline. Sans manifeste,
   compétence technique reste mais discipline glisse.
2. **Hard-asserts bash bloquants** : externalisent la vérification au shell.
   Plus puissant que la promesse interne.
3. **Mot magique GO/validé** : pattern binaire, vérifiable. Loïc le contourne
   parfois par concision (« yes », « tout fonctionne »), Claude doit re-cadrer.
4. **Plan pré-audité 3700 lignes** : absorbe le doute avant exécution. 18+
   audit-fix appliqués en amont. Les 3 vrais bugs trouvés en exécution étaient
   tous mineurs et résolus en < 30 min chacun.
5. **Checkpoints inter-phases** : battement régulier. Permet à Loïc de
   re-cadrer sans interrompre. Oblige Claude à formuler les coûts.
6. **Interventions actives Loïc** : 3 moments décisifs (re-cadrage hack EVT_WAITING,
   force d'audit CLEAR en quantize BAR, force du mot exact). Discipline
   passive ne suffit pas — la pression active maintient le niveau.

### Faiblesses identifiées (à renforcer pour phases suivantes)

1. **Validation HW substantielle vs lexicale** : à G5 milestone (1er son MIDI),
   Loïc a dit « GO » sans log audio explicite. Si le test n'avait pas eu lieu,
   un faux milestone aurait été commité. Pour milestones critiques, exiger
   un observable factuel (log, screenshot, description audio textuelle).

2. **TodoWrite perdu par compression** : 2× en Phase 2. Workaround manuel
   (recréation). Pas catastrophique mais à anticiper : ne pas compter sur la
   liste comme source unique.

3. **Tentation hack en main loop** : 1 fois (proposition state-before/after
   polling pour clear EVT_WAITING). Re-cadré par Loïc en 1 message. Aurait
   été commité sans son intervention. À noter : le manifeste seul ne couvre
   pas tous les types de dérive, certaines nécessitent l'humain.

4. **Documentation post-hoc** : Task 36 doc-sync est en fin de phase. Mieux
   vaudrait mettre à jour les docs **au fil** des tasks pour éviter qu'elles
   soient bâclées en bloc à la fin. Compromise : doc-sync finale concentrée
   garde la cohérence, mais coût d'attention est plus élevé.

---

## Limites identifiées du protocole

### Limites connues

- **TodoWrite n'est pas une source canonique** : peut être perdu par
  compression. Plan + manifeste restent la source de vérité.
- **Le protocole suppose 1 sujet par session** : multi-projets simultanés dans
  une même session = dégradation rapide. Ouvrir des sessions séparées.
- **Discipline passive insuffisante** : le manifeste ne couvre pas tous les types
  de dérive. L'intervention humaine active de Loïc reste un facteur de qualité.
- **Coût de prep > exécution courte** : pour < 10 tasks, le surcoût de la PREP
  complète peut être disproportionné. Adapter (manifeste léger, pas d'audit
  adversarial dédié).

### Hypothèses non-encore testées

- Le protocole a été validé en Phase 2 LOOP (38 tasks, 9 HW gates, 1 session
  EXEC de plusieurs heures). Il reste à valider sur :
  - Phase multi-jours (qualité maintenue ou pas après 24 h ?).
  - Phase multi-personne (si plusieurs développeurs accèdent à la même session).
  - Phase avec dependancies externes (ex : API tierce avec downtime).

---

## Quand mettre à jour ce document

- Après chaque phase exécutée (Phase 3, 4, 5, 6 LOOP) : ajouter section
  "Lessons learned Phase N" avec ce qui a marché et ce qui a failli rater.
- Après chaque post-mortem identifiant un anti-pattern non-listé : ajouter
  à §"Anti-patterns à éviter".
- Si un pattern nouveau émerge (ex : un type de hard-assert non-couvert) :
  ajouter à §"Templates réutilisables".

**Politique d'élagage** : si le doc dépasse 600 lignes, élaguer les sections
les moins consultées. Garder les 9 règles et les templates verbatim — c'est
le cœur réutilisable.

---

## Références

- **Manifeste Phase 2 LOOP** (référence concrète d'application) :
  [`plans/2026-05-18-loop-phase-2-session-manifest.md`](plans/2026-05-18-loop-phase-2-session-manifest.md)
- **Plan Phase 2 LOOP** (référence concrète) :
  [`plans/2026-05-18-loop-phase-2-plan.md`](plans/2026-05-18-loop-phase-2-plan.md)
- **Project CLAUDE.md** (invariants projet) : [`.claude/CLAUDE.md`](../../.claude/CLAUDE.md)
- **Global CLAUDE.md** (préférences user) : `~/.claude/CLAUDE.md`
- **LOOP_PROGRESS.md** (jalons restants) : [`LOOP_PROGRESS.md`](LOOP_PROGRESS.md)
- **STATUS.md** (focus courant) : [`../../STATUS.md`](../../STATUS.md)
