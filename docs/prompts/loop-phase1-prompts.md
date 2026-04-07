# Prompts d'exécution — LOOP Phase 1

**Date** : 2026-04-07
**Cible** : démarrage de l'implémentation LOOP mode, Phase 1 (skeleton + guards)
**Modèle attendu** : Claude Opus 4.6 (1M context)

---

## Comment utiliser ce document

Ce doc contient deux prompts à copier-coller dans une **session Claude Code fraîche** :

1. **Prompt 1 — Implémentation** : à utiliser dans la session qui va écrire le code de Phase 1.
2. **Prompt 2 — Vérification** : à utiliser dans une **autre** session fraîche, après que la session 1 a fini, pour auditer le travail.

Une session par phase. Une session de vérification par phase. Pas de réutilisation de session entre phases — chaque phase démarre avec un Opus 1M vierge pour profiter de l'intégralité du contexte sans dégradation.

**Les prompts sont écrits pour combattre les biais de Claude qui surviennent quand il suit un plan sans réfléchir.** Lis-les avant de les coller pour comprendre la logique, ajuste si nécessaire pour ton contexte du moment.

---

## Contexte général (s'applique à toutes les phases)

### Branche dédiée

```bash
git checkout main
git pull origin main
git checkout -b loop          # une seule fois, à Phase 1 uniquement
git push -u origin loop
```

À partir de Phase 1, **tous les commits LOOP vont sur `loop`**. Pas de merge sur `main` avant que les 6 phases soient implémentées + testées + reviewées.

### Modèle de session

| Phase | Session impl. | Session vérif. |
|---|---|---|
| Phase 1 | session 1A | session 1B |
| Phase 2 | session 2A (fresh) | session 2B (fresh) |
| ... | ... | ... |

Entre 1B et 2A : **un fichier de handoff** transmet l'état (`docs/superpowers/handoff/phaseN-to-phaseN+1.md`). C'est la première lecture obligatoire de la session 2A.

### Politique commits

- Commits fréquents : un commit par groupe logique (Step 1 = 1 commit, Step 2 = 1 commit, etc.) — viser 5-8 commits par phase
- Jamais `git add .` ni `git add -A` — toujours nommer les fichiers
- Présenter le HEREDOC du message AVANT `git commit`, attente OK explicite
- Pas de Co-Authored-By (préférence utilisateur)
- Pas de push automatique — l'utilisateur tranche
- Pas de merge avant la fin des 6 phases

### Politique hardware test

L'implémenteur **ne peut pas** uploader le firmware ni tester sur le device. Quand il atteint un checkpoint hardware, il :

1. Stop l'exécution
2. Présente à l'utilisateur **exactement quoi tester** (scénarios précis, gestes, comportement attendu)
3. Attend la confirmation explicite de l'utilisateur que le test est OK
4. Reprend uniquement après confirmation

C'est le seul moyen de vérifier le comportement runtime. Le build clean ne suffit jamais.

---

# Prompt 1 — Implémentation Phase 1

> **Copier-coller le bloc ci-dessous dans une session Claude Code fraîche, après `cd /Users/loic/Code/PROJECTS/ILLPAD_V2`.**

````
Tu vas implémenter la Phase 1 du LOOP mode (skeleton + guards) sur une
branche dédiée `loop`. Cette session est en Opus 1M, pas de pression
temporelle, effort maximum sur chaque step. Lis l'intégralité de ce
prompt avant de commencer quoi que ce soit.

## Mission

Appliquer le plan `docs/plans/loop-phase1-skeleton-guards.md` au code
source. Après cette phase :
- Le code compile clean (zéro warning lié à Phase 1)
- Le comportement runtime est STRICTEMENT IDENTIQUE à avant Phase 1
  (aucune bank LOOP n'existe au runtime, toutes les guards sont
  traversées sans effet)
- Toutes les structures NVS étendues sont en place pour Phase 2
- La branche `loop` contient 5 à 8 commits cohérents

## Tu n'es PAS un copieur de snippets

Le plan Phase 1 te donne des snippets de code précis. Ces snippets ont
été audités plusieurs fois mais ils ne sont PAS la source de vérité.
**Le code source actuel est la source de vérité.** Ta mission n'est pas
d'appliquer aveuglément les snippets, c'est de :

1. Comprendre ce que CHAQUE step cherche à accomplir (l'intention)
2. Lire le code source actuel des fichiers ciblés intégralement
3. Vérifier que le code correspond à ce que le snippet suppose (line
   numbers, signatures, structure)
4. Adapter le snippet si nécessaire pour matcher la réalité actuelle
5. Présenter le diff exact à l'utilisateur AVANT application

Si tu trouves un mismatch entre le plan et le code (line number qui ne
matche plus, signature qui a changé, fonction renommée), **TU DOIS le
résoudre intelligemment**. Tu n'es PAS autorisé à appliquer un snippet
qui ne match plus le code. Si le mismatch est grave (le plan suppose
une architecture qui n'existe plus), tu STOP et tu poses la question à
l'utilisateur via AskUserQuestion.

## Biais à combattre activement

Tu vas être tenté par ces shortcuts. Reconnais-les chez toi et combats-les :

| Tentation | Réalité |
|---|---|
| "Le plan dit ligne 123, je patch ligne 123" | Non. Lis la fonction d'abord, vérifie qu'elle correspond, applique l'edit avec le contexte unique de la fonction |
| "L'audit fix B1 est déjà appliqué dans le plan, je trust" | Non. Lis le snippet entier, vérifie qu'il fait ce qu'il prétend faire |
| "Build clean = code correct" | Non. Build clean = compile uniquement. Cherche les bugs LOGIQUES dans le diff, lis 2 fois les sections que tu as modifiées |
| "J'ai déjà lu CLAUDE.md" | Non. Tu OUVRES le fichier au début de la session, tu le lis intégralement, pas de raccourci |
| "Cette signature je la connais" | Non. Tu la VÉRIFIES via Read avant de l'utiliser dans un edit |
| "Step trivial, pas besoin de todo" | Non. TodoWrite pour CHAQUE sous-step, marquage in_progress avant, completed après |
| "C'est juste 5 lignes, pas besoin de re-build" | Non. Build après chaque groupe logique de sous-steps |
| "L'utilisateur va voir le diff lui-même" | Non. Tu présentes le diff résumé AVANT chaque commit, attente OK |
| "Pas de hardware test à ce stade" | Non. Phase 1 a des checkpoints hardware. Tu STOP à chacun et tu donnes les scénarios |
| "Le commit message peut attendre" | Non. Présente-le maintenant en HEREDOC, attente OK |

## Contexte projet — lecture obligatoire AVANT tout edit

Lis intégralement, dans cet ordre, sans raccourci :

1. `.claude/CLAUDE.md` — spec projet complet
2. `docs/reference/architecture-briefing.md` — flows runtime + invariants
3. `docs/reference/nvs-reference.md` — patterns NVS
4. `docs/known_issues/known-bugs.md` — bugs trackés (tu vérifieras
   qu'aucun de tes edits ne réintroduit B-001 ou B-002 fixés en
   commit c23eea4)
5. `docs/known_issues/2026-04-07-setup-tools-live-runtime-apply-superflu.md`
   — dette technique connue (tu n'es PAS autorisé à toucher ces zones
   sauf si Phase 1 le demande explicitement)
6. `docs/superpowers/specs/2026-04-06-loop-slot-drive-design.md` — spec
   slot drive (Phase 1 contient des prereqs slot drive en Step 7c que
   tu ne comprendras pas sans le spec)
7. `docs/plans/loop-phase1-skeleton-guards.md` — LE PLAN. Lis-le DEUX
   FOIS : une vue d'ensemble pour la structure générale, puis une
   lecture détaillée step par step avant chaque application

À la fin de cette lecture, AVANT de toucher au code, tu écris dans le
chat :
- Un résumé en 5-10 puces de ce que Phase 1 cherche à accomplir
- La liste des fichiers que Phase 1 va modifier
- Les "audit fix" qui ont été incorporés au plan (BUG #1, BUG #2,
  BUG #3, GAP #5, A1, B6, C1, C2, plus le D-PLAN-1 qui est dans le
  Phase 2 plan mais référencé via commit 1366918)
- Toute question ou doute que tu as

Si l'utilisateur valide ton résumé, tu enchaînes avec la branche.

## Setup branche

```bash
git checkout main
git pull origin main
git status   # vérifier clean
git checkout -b loop
git push -u origin loop
git log --oneline -5   # confirmer position
```

À partir de ce moment, tous les commits Phase 1 vont sur `loop`. Tu
NE PUSH PAS automatiquement après chaque commit — l'utilisateur tranche
le push à la fin de la phase.

## Méthodologie par sous-step

Phase 1 a 7 grandes sections (Step 1 à Step 7c). Chaque section a des
sous-steps numérotés (1a, 1b, 1c, etc.). Pour CHAQUE sous-step :

1. **TodoWrite** : marquer la todo en `in_progress` (une todo par
   sous-step, pas une par section)
2. **Lire le snippet du plan** intégralement, comprendre l'intention
3. **Lire le code cible** intégralement (la fonction entière, pas juste
   les 5 lignes autour du point d'edit)
4. **Vérifier le mismatch éventuel** entre snippet et code actuel :
   - Line numbers : encore valides ?
   - Signature des fonctions appelées : encore valide ?
   - Includes nécessaires : déjà présents ?
   - Variables référencées : encore déclarées au même endroit ?
5. **Présenter le diff exact à appliquer** au format : fichier + section
   + before/after résumé (3-10 lignes max si possible)
6. Pour les diff non-triviaux ou ambigus : **attendre OK explicite** de
   l'utilisateur. Pour les diff triviaux et déjà validés en groupe :
   appliquer directement.
7. **Appliquer l'edit** via Edit
8. **TodoWrite** : marquer en `completed`

## Méthodologie par groupe logique (Step 1, Step 2, etc.)

Pour CHAQUE GROUPE de sous-steps qui forment une unité cohérente
(Step 1 entier, Step 2 entier, etc.) :

1. **Build** : `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
2. **Lire la sortie build INTÉGRALEMENT** :
   - Si erreur : diagnostiquer la cause racine, pas patcher autour
   - Si warning nouveau : analyser, pas ignorer
   - Si clean : noter les métriques RAM/Flash et passer à l'étape 3
3. **Présenter le diff résumé du groupe** : tous les fichiers touchés
   par ce groupe, avec un résumé 1-2 lignes par fichier
4. **Présenter le commit message en HEREDOC** au format préféré
   utilisateur (orienté why, pas what, pas de Co-Authored-By)
5. **Attendre OK explicite** sur le commit
6. `git add <fichiers nommés explicitement>` puis `git commit`
7. `git log --oneline -5` pour confirmer
8. **TodoWrite** : marquer le groupe entier en completed

## Hardware test checkpoints

Phase 1 ne change RIEN au comportement runtime — c'est un test de
non-régression. Tu STOP à chaque checkpoint et tu présentes à
l'utilisateur **exactement quoi tester**, avec les gestes précis et
le comportement attendu.

### Checkpoint A — après Step 5 (BankManager LOOP guards)

Stop. Présente à l'utilisateur :

> "Step 5 (BankManager guards) terminé. Avant de continuer Step 6,
> teste sur le device :
>
> 1. Boot normal, vérifier que LedController boot bar atteint LED 8
>    (pas de halt sur step 3 ou autre)
> 2. Switch bank NORMAL → ARPEG → NORMAL via hold left + bank pad :
>    aucune régression visible, confirmation blink OK
> 3. ARPEG : ajouter une note, confirmer que l'arp tourne, switch vers
>    NORMAL, vérifier que l'arp continue en background, retour vers
>    ARPEG, vérifier que l'arp est toujours là
> 4. NORMAL : jouer quelques pads, vérifier que les notes sortent
>    correctement, pitch bend pot fonctionne
> 5. Vérifier serial debug : aucun message d'erreur, aucun crash
>
> Quand c'est OK, dis-moi 'Checkpoint A OK' et je continue."

Tu attends la confirmation explicite avant de toucher au Step 6.

### Checkpoint B — après Step 6 (ScaleManager LOOP guard)

Stop. Présente à l'utilisateur :

> "Step 6 (ScaleManager early return + _lastScaleKeys sync) terminé.
> Teste :
>
> 1. Bank ARPEG : hold left + scale pads (root C, mode Dorian, etc.) :
>    le scale change comme avant, confirmation blink OK
> 2. Bank ARPEG : hold left + chromatic pad : passe en chromatique
> 3. Bank ARPEG : hold left + octave pad : octave change
> 4. Bank ARPEG : hold left + hold pad : toggle HOLD on/off
> 5. Bank NORMAL : hold left + scale pads : scale change avec
>    allNotesOff, pas de note bloquée
> 6. Vérifier qu'aucun phantom edge ne se produit après bank switch
>    avec un pad enfoncé
>
> Quand c'est OK, dis-moi 'Checkpoint B OK' et je continue."

### Checkpoint C — après Step 7c (slot drive prereqs structurels)

Stop. Présente à l'utilisateur :

> "Step 7c (slot drive prereqs : LoopPadStore 32 bytes, LOOP_SLOT_COUNT,
> color constants) terminé. C'est le checkpoint le plus risqué car les
> Stores NVS ont changé de taille. Teste :
>
> 1. Boot complet : vérifier serial debug pour les warnings NVS
>    attendus :
>    - BankTypeStore va probablement avoir un warning (size mismatch
>      car loopQuantize[] ajouté → 28 bytes vs 20 bytes)
>    - LoopPadStore va échouer (n'existe pas encore en NVS) — c'est OK
> 2. Vérifier qu'AUCUN paramètre user existant n'est perdu :
>    - Calibration pads OK
>    - Pad ordering OK
>    - Bank pads OK
>    - Settings (profile, AT rate, BLE, clock, double-tap) OK
>    - Pot mapping OK (NORMAL+ARPEG)
>    - LED settings (intensities, colors) OK
> 3. Si BankTypeStore a été reset : re-rentrer la config bank dans
>    Tool 4, vérifier que ça marche
> 4. Cycle d'usage : ARPEG, NORMAL, scale change, bank switch, pots,
>    NVS save (10 sec d'inactivité)
> 5. Reboot : tout doit être restauré
>
> Quand c'est OK, dis-moi 'Checkpoint C OK'."

### Checkpoint final — après tout Phase 1

Stop. Présente à l'utilisateur :

> "Phase 1 implémentation complète. Liste des commits :
> [liste des hashes + messages]
>
> Pour le sign-off final, teste un cycle d'usage complet 5-10 minutes :
>
> 1. ARPEG bank : pile, hold on/off, scale change, octave change, pots
>    (gate, shuffle, division, pattern, vel)
> 2. NORMAL bank : aftertouch, pitch bend, vel variation
> 3. Bank switch rapide A→B→C avec pad maintenu : pas de stuck note
> 4. Tool 4 : vérifier que NORMAL et ARPEG sont toujours sélectionnables
>    (LOOP n'apparaît PAS encore — c'est Phase 3)
> 5. Tool 3 : vérifier que les pad roles existants fonctionnent
> 6. Reboot : tout est restauré
>
> Quand tu confirmes le sign-off, j'écris le state dump pour Phase 2."

## State dump pour la session suivante

À la fin de Phase 1 (tous les commits faits, hardware test final OK),
tu écris UN fichier de handoff :

`docs/superpowers/handoff/phase1-to-phase2.md`

Contenu obligatoire :
- Liste des commits Phase 1 avec hashes courts + messages
- Liste exhaustive des fichiers modifiés par Phase 1
- Rappel : la branche est `loop`, pas main
- Les décisions prises pendant Phase 1 qui dévient du plan (s'il y en
  a) — par exemple, line numbers ajustés, snippets adaptés, fixes
  improvisés
- Tests hardware passés (checkmarks par scénario, ce qui a marché)
- Tests hardware qui ont nécessité un fix supplémentaire
- Rappels critiques pour Phase 2 :
  - `_pendingKeyIsPressed`/`_pendingPadOrder`/`_pendingBpm` doivent
    être déclarés (audit D-PLAN-1, déjà patché dans le plan en commit
    1366918, mais l'implémenteur Phase 2 doit le savoir)
  - Branche `loop`, pas `main`
  - Phase 2 démarre par lecture intégrale du plan Phase 2, pas seulement
    le diff Phase 1
- Préférences utilisateur observées pendant la session (ex:
  "l'utilisateur préfère commits par Step plutôt que groupés", "demande
  systématiquement un build après chaque edit", etc.)

Ce fichier est commit + push final de la session.

## Anti-flemme

Cette session est en Opus 1M. Pas de pression temporelle. Effort
maximal sur chaque step. Symptômes de flemme à reconnaître chez toi
en cours de session :

- "Je vais batch les edits suivants" → NON, un edit à la fois
- "Le plan dit X je trust" → NON, vérifie X
- "Pas besoin de relire la fonction entière" → SI, relis-la
- "Build sûrement OK" → NON, lance le build
- "Le commit message peut attendre" → NON, présente-le maintenant
- "On peut sauter le hardware checkpoint, c'est juste un guard" → NON,
  jamais
- "Le todo est écrit, je peux marquer completed avant d'avoir fini" →
  NON, completed = quand le sous-step est PROUVÉ fini

Si tu te surprends à penser une de ces phrases, tu RALENTIS et tu
appliques le protocole.

## Format des erreurs / blocages

Si tu rencontres une erreur de build ou un comportement inattendu :

1. Tu N'IGNORES PAS, tu DIAGNOSTIQUES
2. Tu lis l'erreur intégralement
3. Tu identifies la cause racine, pas un symptôme
4. Tu présentes ta diagnostique à l'utilisateur AVANT de patcher
5. Si tu n'es pas sûr : AskUserQuestion

Si tu rencontres un mismatch grave plan ↔ code :

1. Stop
2. AskUserQuestion : présente le mismatch + tes options de résolution
3. Attends la décision

## Démarrage

Annonce à l'utilisateur (texte exact) :

"Session Phase 1 LOOP démarrée. Branche dédiée. Je commence par lire
le contexte projet (7 fichiers obligatoires) puis le plan Phase 1
intégralement (2 lectures). Je présenterai un résumé de ce que je
vais faire AVANT de toucher au code, et chaque diff AVANT chaque
edit. Hardware checkpoints aux Step 5, Step 6, Step 7c, et final.
Aucun push automatique. C'est parti."

PUIS lance la lecture des 7 fichiers contexte sans attendre.
````

---

# Prompt 2 — Vérification Phase 1

> **Copier-coller le bloc ci-dessous dans une session Claude Code FRAÎCHE et SÉPARÉE de la session d'implémentation. Idéalement le lendemain ou au moins après /clear.**

````
Tu vas auditer l'implémentation de Phase 1 LOOP en mode READ-ONLY
strict. Aucun edit sur le code, aucun edit sur les plans. Ton seul
output est un rapport structuré.

## Mission

Trouver TOUS les problèmes dans l'implémentation Phase 1 :
- Bugs de compilation (build clean ne suffit pas — cherche les bugs
  qui sont silencieux à l'avertissement)
- Bugs runtime potentiels (race conditions, off-by-one, wrong
  signatures appelées, validators manquants)
- Mismatches plan ↔ implémentation (steps non appliqués, snippets mal
  retranscrits, side effects oubliés)
- Régressions sur le comportement existant (NORMAL, ARPEG, scale
  change, bank switch, NVS load)
- Findings F (optimisations, dead code, defensive guards manquants)

## Tu es l'auditeur, pas le copilote

Tu n'es PAS là pour valider que "tout va bien". Tu es là pour TROUVER
LES PROBLÈMES. Si après 30 minutes tu conclus "tout est bon", tu n'as
pas cherché assez. La complaisance de validation est ton pire ennemi
dans cette session.

## Biais spécifiques à combattre

| Biais | Réalité |
|---|---|
| "L'implémenteur a déjà passé du temps dessus, c'est sûrement OK" | Le but de l'audit est précisément de détecter ce qu'il a raté |
| "Le commit message dit 'Step 1 done', donc Step 1 est done" | Vérifie chaque sous-step contre le diff réel |
| "Build clean = code correct" | Build clean = compile. Cherche les bugs LOGIQUES dans le diff |
| "Tous les snippets sont là, donc la phase est faite" | Vérifie que CHAQUE snippet est appliqué ET que les SIDE-EFFECTS sont gérés |
| "Le plan était audité, donc l'implémentation est forcément OK" | Le plan est une INTENTION. Tu vérifies l'EXÉCUTION |
| "Audit fix B1 mentionné → B1 corrigé" | Vérifie que le code patché correspond au fix décrit |
| "Pas besoin de relire CLAUDE.md" | Si, relis-le |
| "30 min suffisent pour cette phase" | Non. Lis tout. Vérifie tout. |

## Contexte mandatory à charger AVANT toute analyse

Lis intégralement, dans cet ordre :

1. `.claude/CLAUDE.md` — spec projet
2. `docs/reference/architecture-briefing.md` — invariants runtime
3. `docs/reference/nvs-reference.md` — patterns NVS
4. `docs/superpowers/specs/2026-04-06-loop-slot-drive-design.md` — spec
   slot drive
5. `docs/plans/loop-phase1-skeleton-guards.md` — LE PLAN, ta source de
   vérité pour cet audit. À lire 2 fois.
6. `docs/superpowers/handoff/phase1-to-phase2.md` — le state dump de la
   session précédente. Note les "décisions prises qui dévient du plan"
   et les vérifie spécifiquement.
7. La sortie de :
   ```
   git log loop --oneline -20
   git log main..loop --oneline
   git diff main..loop --stat
   ```
   pour avoir une vue d'ensemble des commits Phase 1.
8. Le diff complet : `git diff main..loop` (gros, lis-le par section)

## Méthodologie

### Étape 1 — Cartographie commit ↔ Step
Pour chaque Step du plan (1, 2, 3, 4, 5, 6, 7, 7b, 7c), identifier :
- Quel commit a appliqué ce step (hash + position dans `git log`)
- Quels fichiers ont été modifiés par ce commit
- Si un step est manquant : finding bloquant immédiat

### Étape 2 — Vérification snippet par snippet
Pour CHAQUE sous-step du plan (1a, 1b, 1c, ..., 7c-5), vérifier dans
le code actuel via Read :
- Le code correspond-il au snippet attendu ?
- Les références croisées (line numbers, function names, includes)
  sont-elles correctes ?
- Les side-effects sont-ils gérés (variables init, includes ajoutés,
  validators à jour, NVS_DESCRIPTORS table à jour, TOOL_NVS_FIRST/LAST
  cohérents) ?

### Étape 3 — Cross-check audit fixes
Le plan Phase 1 contient plusieurs "AUDIT FIX" ou "AUDIT NOTE" :
- BUG #1 (validateBankTypeStore clamp croisé)
- BUG #2 (BankManager recording lock stub)
- BUG #3 (PotMappingStore loopMap absent)
- GAP #5 (main.cpp loopEngine = nullptr init)
- A1 (NvsManager _loadedLoopQuantize, Step 3e)
- B6 (ScaleManager hold/octave sync)
- C1 (CONFIRM_LOOP_REC expiry case)
- C2 (LoopPadStore double-definition consolidée)
- F1 (LED_TICK_BOOST_BEAT1 supprimé)

Pour CHAQUE audit fix, vérifier que le code dans la branche `loop`
correspond à ce que l'audit fix demande. Pas juste "le commentaire est
là", mais "le code fait ce que le commentaire dit".

### Étape 4 — Build verification
Run le build et vérifier :
```
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -50
```
- Build clean
- Zéro warning lié à Phase 1 (warnings préexistants OK, mais si un
  -Wreorder ou -Wunused-variable est apparu : finding)
- Métriques RAM/Flash raisonnables (vs avant Phase 1 — utilise
  `git show main:platformio.ini` n'a pas de baseline, donc compare
  juste à un seuil sain : RAM < 25%, Flash < 30%)

### Étape 5 — Static checks
Pour chaque struct/enum/constant ajouté en Phase 1, vérifier :
- `static_assert(sizeof(...) <= NVS_BLOB_MAX_SIZE)` présent
- `validate*()` function ajoutée et complète
- `NVS_DESCRIPTORS[]` table à jour
- `TOOL_NVS_FIRST/LAST` indices cohérents avec les nouveaux descripteurs

### Étape 6 — Behavior regression check (analyse statique)
Phase 1 ne doit RIEN changer au comportement runtime. Vérifier :
- `BankSlot` ctor / init : `loopEngine = nullptr` est appliqué partout
  où BankSlot est initialisé
- `BankManager::switchToBank()` : la nouvelle guard LOOP est
  conditionnelle (loopEngine != nullptr → toujours false en Phase 1)
- `ScaleManager::processScalePads()` : l'early return LOOP est
  conditionnelle (slot.type == BANK_LOOP → toujours false en Phase 1)
- `BankTypeStore` validate : la nouvelle logique loopCount est
  cohérente
- Rien dans `main.cpp loop()` n'a été modifié de façon non documentée

### Étape 7 — Hardware test scenarios (à passer à l'utilisateur)
Phase 1 a déjà eu des hardware checkpoints (A, B, C, final) pendant
l'implémentation. Pour l'audit, demande à l'utilisateur de RE-tester
les scénarios critiques :
- Liste les checkpoints qui ont été passés selon le handoff
- Pour chaque checkpoint, propose un re-test rapide à l'utilisateur
- Demande spécifiquement les scénarios qui ont été un peu rapidement
  validés

### Étape 8 — Rapport structuré
Produire un rapport markdown au format :

```
# Audit Phase 1 LOOP — [date]

## Résumé exécutif
- Findings A (bloquants) : N
- Findings B (runtime) : N
- Findings C (incohérences) : N
- Findings D (mismatch plan↔code) : N
- Findings F (optimisations) : N
- Niveau de confiance : HIGH/MEDIUM/LOW
- Recommandation : GO Phase 2 / GO Phase 2 with fixes / REWORK Phase 1

## A — Bugs bloquants
[findings ou "néant — vérifié N catégories"]

## B — Bugs runtime
[idem]

## C — Incohérences inter-step
...

## D — Mismatches plan↔code
...

## F — Optimisations
...

## Checklist par Step
- Step 1 : audité, N findings
- Step 2 : audité, N findings
- Step 3 : audité, N findings
- ...

## Tests hardware demandés à l'utilisateur
[liste]

## Conclusion
[5-8 lignes, recommandation finale]
```

## Anti-complaisance

Tu fais cet audit SUR le travail d'un autre Claude (de la session
précédente). Le bias de défense unconscious est encore plus fort que
la complaisance générale. Faire l'exercice mental :

- "Si j'avais codé ça, où aurais-je fait les erreurs ?"
- "Quels sont les snippets les plus piégeux du plan Phase 1 ?"
- "Quels sont les sous-steps que l'implémenteur a probablement bâclés
  en fin de session, quand sa concentration baisse ?"
- "Qu'est-ce que le plan dit que le code n'arrive pas à vérifier
  automatiquement (ex: 'audit fix B1 appliqué' — comment je le
  vérifie en lisant le code ?)"

## Anti-flemme

Cette session est en Opus 1M. Pas de pression temporelle. Lis tout.
Vérifie tout. Si tu te dis "ça doit être bon", relis encore une fois.

Symptômes de flemme à reconnaître :
- "J'ai vu beaucoup de findings, je peux arrêter" → NON, finis l'audit
- "Le plan est bien fait, pas besoin de tout vérifier" → NON, vérifie
- "Cette section est trop longue, je vais skim" → NON, lis
- "Build clean, pas besoin de chercher de bugs runtime" → NON, cherche

## Démarrage

Annonce à l'utilisateur :

"Audit Phase 1 démarré. Mode READ-ONLY strict. Aucune modification de
code, plan, ou commits. Lecture contexte (8 fichiers + git diff
loop..main) → cartographie commit/step → vérification snippet par
snippet → cross-check audit fixes → build verification → static checks
→ behavior regression check → rapport structuré final. Je ne vais pas
auto-valider : si tout semble OK après mon premier pass, je chercherai
plus profond avant de livrer le rapport."

PUIS lance la lecture du contexte sans attendre.
````

---

## Pour les phases suivantes (Phase 2 à Phase 6)

Ce doc couvre Phase 1. Pour les Phases 2-6, **dupliquer ce fichier** en
adaptant :

1. Le numéro de phase dans le titre et les références
2. Le plan ciblé (`docs/plans/loop-phaseN-...md`)
3. Les hardware checkpoints (chaque phase a ses propres scénarios)
4. La référence au handoff de la phase précédente (lecture obligatoire
   en plus du contexte projet)
5. Les rappels critiques spécifiques à la phase
6. Le state dump final pour la phase suivante

Le squelette anti-bias et anti-flemme reste identique — c'est
intentionnel, ces biais ne disparaissent pas entre phases.

**Naming convention** :
- `docs/prompts/loop-phase1-prompts.md` (ce fichier)
- `docs/prompts/loop-phase2-prompts.md`
- ...
- `docs/prompts/loop-phase6-prompts.md`

Et les handoffs :
- `docs/superpowers/handoff/phase1-to-phase2.md`
- `docs/superpowers/handoff/phase2-to-phase3.md`
- ...

---

## Notes méthodologiques sur ces prompts

### Pourquoi 2 prompts par phase
L'implémenteur a un biais de défense sur son propre travail. L'auditeur,
dans une session différente avec un Claude différent (même modèle, même
contexte mais sans l'historique de l'implémentation), n'a pas ce biais.
La séparation est essentielle.

### Pourquoi sessions fraîches
Opus 1M permet 1 million de tokens, mais l'effort par token n'est pas
constant : Claude a tendance à réduire la rigueur en fin de session
même quand le contexte n'est pas saturé. Une session fraîche par phase
maximise l'effort par step.

### Pourquoi pas de TDD
Firmware Arduino C++ sans framework de test. Les "tests" sont les
hardware checkpoints + le build clean + l'analyse statique. Le prompt
remplace TDD par la triade lecture-vérification-checkpoint hardware.

### Pourquoi tant d'anti-bias
Les biais de Claude sont mesurables et reproductibles. Les nommer
explicitement permet à Claude de les reconnaître chez lui-même
("symptômes à reconnaître"). Sans ça, les biais s'expriment
silencieusement et le travail est dégradé.

### Pourquoi pas de "estimation de durée"
CLAUDE.md du projet interdit les estimations de temps. Les prompts ne
disent jamais "ça devrait prendre X heures". L'effort attendu est
"maximum sur chaque step", point.
