# Prompts d'exécution — LOOP Phase 2

**Date** : 2026-04-08
**Cible** : implémentation LOOP mode, Phase 2 (LoopEngine + wiring)
**Modèle attendu** : Claude Opus 4.6 (1M context)
**Prérequis** : Phase 1 terminée, mergée sur la branche `loop`, handoff `phase1-to-phase2.md` écrit

---

## Comment utiliser ce document

Ce doc contient deux prompts à copier-coller dans une **session Claude Code fraîche** :

1. **Prompt 1 — Implémentation** : à utiliser dans la session qui va écrire le code de Phase 2.
2. **Prompt 2 — Vérification** : à utiliser dans une **autre** session fraîche, après que la session 1 a fini, pour auditer le travail.

Une session par phase. Une session de vérification par phase. Pas de réutilisation de session entre phases — chaque phase démarre avec un Opus 1M vierge pour profiter de l'intégralité du contexte sans dégradation.

**Les prompts sont écrits pour combattre les biais de Claude qui surviennent quand il suit un plan sans réfléchir.** Lis-les avant de les coller pour comprendre la logique, ajuste si nécessaire pour ton contexte du moment.

---

## Contexte général (rappels inter-phases)

### Branche dédiée

La branche `loop` existe déjà, créée en Phase 1. **NE PAS** créer une nouvelle branche, **NE PAS** merger sur main, **NE PAS** checkout main.

```bash
git checkout loop
git pull origin loop          # si l'utilisateur a pushé depuis la dernière fois
git status                    # vérifier clean
git log --oneline -15         # confirmer position, le top doit être un commit Phase 1
```

Tous les commits Phase 2 vont sur `loop`. Pas de merge sur `main` avant que les 6 phases soient implémentées + testées + reviewées.

### Modèle de session

| Phase | Session impl. | Session vérif. |
|---|---|---|
| Phase 1 | ✅ session 1A (faite) | ✅ session 1B (si faite) |
| Phase 2 | session 2A (CETTE session) | session 2B (fresh, après) |
| Phase 3 | session 3A (fresh) | session 3B (fresh) |
| ... | ... | ... |

Entre 1B et 2A : **un fichier de handoff** transmet l'état, `docs/superpowers/handoff/phase1-to-phase2.md`. **C'est la première lecture obligatoire de cette session 2A**, en plus du contexte projet.

### Politique commits

- Commits fréquents : un commit par groupe logique (Step 1 = 1 commit, Step 2 = 1 commit, etc.) — viser 8-12 commits pour Phase 2 (plus grosse que Phase 1)
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

**Phase 2 est FONDAMENTALEMENT différente de Phase 1 sur ce point** : Phase 1 était un test de non-régression (aucune bank LOOP au runtime). Phase 2 introduit le **premier comportement LOOP runtime** — recording, overdub, playback, quantize. Les checkpoints hardware sont la seule façon de valider que ça marche, et un build clean ne prouve strictement rien à ce stade.

---

# Prompt 1 — Implémentation Phase 2

> **Copier-coller le bloc ci-dessous dans une session Claude Code fraîche, après `cd /Users/loic/Code/PROJECTS/ILLPAD_V2`.**

````
Tu vas implémenter la Phase 2 du LOOP mode (LoopEngine + wiring) sur la
branche dédiée `loop` qui existe déjà depuis Phase 1. Cette session est
en Opus 1M, pas de pression temporelle, effort maximum sur chaque step.
Lis l'intégralité de ce prompt avant de commencer quoi que ce soit.

## Mission

Appliquer le plan `docs/plans/loop-phase2-engine-wiring.md` au code
source. Après cette phase :
- `src/loop/LoopEngine.h` et `src/loop/LoopEngine.cpp` existent et
  compilent clean
- `main.cpp` héberge `s_loopEngines[MAX_LOOP_BANKS]`, un
  `processLoopMode()`, un `handleLoopControls()`, un dispatch BANK_LOOP
  dans `handlePadInput()`, un `LoopEngine::tick()` + `processEvents()`
  dans le loop order
- `BankManager::switchToBank` guards LOOP activés (remplace les stubs
  Phase 1 par les vrais appels `isRecording()` / `flushLiveNotes()`)
- Une bank LOOP forcée en dur (via `LoopTestConfig.h`) permet de tester
  sur le device : record, overdub, playback, quantized start/stop
- Le build compile clean (zéro warning lié à Phase 2)
- Le comportement NORMAL et ARPEG reste identique (régression zéro sur
  ces modes)
- La branche `loop` contient 8 à 12 nouveaux commits cohérents

## Tu n'es PAS un copieur de snippets

Le plan Phase 2 te donne des snippets de code précis. Ces snippets ont
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

**Spécifique à Phase 2** : Phase 1 a modifié BankManager, ScaleManager,
NvsManager, LedController, main.cpp, KeyboardData.h, HardwareConfig.h.
Certains line numbers référencés dans le plan Phase 2 peuvent avoir
drifté de quelques lignes à cause des insertions Phase 1. Vérifie
systématiquement via Read avant chaque edit.

## Biais à combattre activement

Tu vas être tenté par ces shortcuts. Reconnais-les chez toi et combats-les :

| Tentation | Réalité |
|---|---|
| "Le plan dit ligne 123, je patch ligne 123" | Non. Lis la fonction d'abord, vérifie qu'elle correspond, applique l'edit avec le contexte unique de la fonction |
| "L'audit fix B1 est déjà appliqué dans le plan, je trust" | Non. Lis le snippet entier, vérifie qu'il fait ce qu'il prétend faire |
| "Build clean = code correct" | Non. Build clean = compile uniquement. Phase 2 a du vrai runtime, les bugs logiques sont la menace principale |
| "J'ai déjà lu CLAUDE.md" | Non. Tu OUVRES le fichier au début de la session, tu le lis intégralement, pas de raccourci |
| "Cette signature je la connais" | Non. Tu la VÉRIFIES via Read avant de l'utiliser dans un edit |
| "Step trivial, pas besoin de todo" | Non. TodoWrite pour CHAQUE sous-step, marquage in_progress avant, completed après |
| "C'est juste 5 lignes, pas besoin de re-build" | Non. Build après chaque groupe logique de sous-steps |
| "L'utilisateur va voir le diff lui-même" | Non. Tu présentes le diff résumé AVANT chaque commit, attente OK |
| "Pas de hardware test à ce stade" | Non. Phase 2 a BEAUCOUP plus de checkpoints hardware que Phase 1. Tu STOP à chacun |
| "Le commit message peut attendre" | Non. Présente-le maintenant en HEREDOC, attente OK |
| "Le LoopEngine est 800 lignes, j'écris tout d'un coup" | Non. Step 1 du plan te donne le fichier par morceaux logiques — respecte le découpage |
| "Phase 1 a touché ces mêmes fichiers, c'est bon" | Non. Re-Read tout, les offsets ont bougé |

## Contexte projet — lecture obligatoire AVANT tout edit

Lis intégralement, dans cet ordre, sans raccourci :

1. `.claude/CLAUDE.md` — spec projet complet
2. `docs/reference/architecture-briefing.md` — flows runtime + invariants.
   **Attention** : certaines sections (pad→MIDI, bank switch) vont devoir
   être mises à jour EN Phase 2 parce que tu modifies les flows. Note
   les endroits où une update doc sera nécessaire.
3. `docs/reference/nvs-reference.md` — patterns NVS (mis à jour en fin
   de Phase 1 au commit 5842ae7)
4. `docs/known_issues/known-bugs.md` — bugs trackés. Phase 1 a ajouté
   B-004/B-005/B-006 (tous pré-existants Tool 6, hors scope Phase 2).
   Tu vérifieras qu'aucun de tes edits ne réintroduit B-001/B-002 fixés
   en commit c23eea4, et qu'aucun nouvel edit ne crée un bug similaire.
5. `docs/known_issues/2026-04-07-setup-tools-live-runtime-apply-superflu.md`
   — dette technique connue (tu n'es PAS autorisé à toucher ces zones
   sauf si Phase 2 le demande explicitement — elle ne le demande pas)
6. `docs/superpowers/specs/2026-04-06-loop-slot-drive-design.md` — spec
   slot drive (Phase 6, mais contient des infos utiles pour comprendre
   l'architecture LoopEngine)
7. `docs/superpowers/handoff/phase1-to-phase2.md` — **CE DOC EST CRITIQUE**.
   Il contient la liste des commits Phase 1, les déviations par rapport
   au plan Phase 1, les audit fixes déjà appliqués (BUG #1/#2/#3, GAP #5,
   A1, B6, C1, C2), les rappels spécifiques pour Phase 2, et les
   préférences utilisateur observées. **Lis-le en entier** avant de
   toucher au plan Phase 2.
8. `docs/plans/loop-phase1-skeleton-guards.md` — parcours rapide pour
   contextualiser les guards Phase 1 que tu vas activer en Phase 2
9. `docs/plans/loop-phase2-engine-wiring.md` — LE PLAN. Lis-le DEUX
   FOIS : une vue d'ensemble pour la structure générale, puis une
   lecture détaillée step par step avant chaque application

À la fin de cette lecture, AVANT de toucher au code, tu écris dans le
chat :
- Un résumé en 5-10 puces de ce que Phase 2 cherche à accomplir
- La liste des fichiers que Phase 2 va créer et modifier
- Les "audit fix" qui ont été incorporés au plan (A1, A4+Q1, B1 multi-pass,
  B2, B7, D1/V1, D-PLAN-1 via commit 1366918) et ce qu'ils corrigent
- Les rappels du handoff phase1-to-phase2 que tu as notés (deviations
  Phase 1, audit fixes déjà en place que tu ne dois PAS casser)
- Toute question ou doute que tu as sur le plan Phase 2

Si l'utilisateur valide ton résumé, tu enchaînes directement avec la
vérification de la branche (pas besoin de re-créer la branche — elle
existe depuis Phase 1).

## Setup branche

```bash
git branch --show-current         # doit imprimer "loop"
git status                        # doit être clean
git log --oneline -15             # confirmer position, top = commit Phase 1
git log main..HEAD --oneline      # liste des commits Phase 1 visible
```

Si `git branch --show-current` ne retourne pas `loop`, **STOP** et
signale à l'utilisateur. Ne pas créer de nouvelle branche, ne pas
switcher. Attends les instructions.

À partir de ce moment, tous les commits Phase 2 vont sur `loop`. Tu
NE PUSH PAS automatiquement après chaque commit — l'utilisateur tranche
le push à la fin de la phase.

## Méthodologie par sous-step

Phase 2 a 11 grandes sections (Step 1 à Step 11). Chaque section a des
sous-steps numérotés. Pour CHAQUE sous-step :

1. **TodoWrite** : marquer la todo en `in_progress` (une todo par
   sous-step, pas une par section)
2. **Lire le snippet du plan** intégralement, comprendre l'intention
3. **Lire le code cible** intégralement (la fonction entière, pas juste
   les 5 lignes autour du point d'edit). Pour LoopEngine.cpp qui est
   un nouveau fichier, lire les équivalents ArpEngine.cpp pour
   comparer les patterns (pile, refcount, pending queue)
4. **Vérifier le mismatch éventuel** entre snippet et code actuel :
   - Line numbers : encore valides ? (Phase 1 a shifté certains)
   - Signature des fonctions appelées : encore valide ?
   - Includes nécessaires : déjà présents ?
   - Variables référencées : encore déclarées au même endroit ?
5. **Présenter le diff exact à appliquer** au format : fichier + section
   + before/after résumé (3-10 lignes max si possible)
6. Pour les diff non-triviaux ou ambigus : **attendre OK explicite** de
   l'utilisateur. Pour les diff triviaux et déjà validés en groupe :
   appliquer directement.
7. **Appliquer l'edit** via Edit ou Write (Write uniquement pour le
   premier jet de LoopEngine.h/.cpp — ensuite toujours Edit)
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

## Rappels critiques issus du handoff Phase 1

Ces points viennent du handoff `phase1-to-phase2.md` et sont
particulièrement piégeux. Relis-les avant de démarrer :

1. **Step 3e (Phase 1) a placé `_loadedLoopQuantize` memset dans
   `loadAll()`, pas dans le constructeur.** Phase 2 Step 4a doit utiliser
   `s_nvsManager.getLoadedLoopQuantizeMode(i)`, **JAMAIS** un accès
   direct à un hypothétique `s_bankTypeStore.loopQuantize[i]` qui
   n'existe pas (audit A1).

2. **BankManager.cpp a des guards LOOP stub** depuis Phase 1. Phase 2
   Step 10 les remplace par les vrais appels `isRecording()` et
   `flushLiveNotes()`. Ne crée PAS de nouveaux blocs — modifie les
   existants, en gardant les commentaires qui documentent l'audit
   BUG #2.

3. **ScaleManager.cpp a une early return LOOP** qui sync 20 entrées
   de `_lastScaleKeys`. Phase 2 NE DOIT PAS toucher ce fichier. Si
   tu dois le toucher pour une raison inattendue, préserve le sync
   complet des 20 entrées (audit B6).

4. **`_pendingKeyIsPressed` / `_pendingPadOrder` / `_pendingBpm`** —
   audit D-PLAN-1 identifié pendant la review du plan Phase 2. Ces
   membres sont référencés dans les snippets du plan (Step 1
   lignes ~317-321 et ~544-553) et doivent être déclarés dans
   LoopEngine.h private section. **Vérifie** que le plan que tu lis
   a bien la déclaration dans la liste des membres (patch commit
   1366918 sur le plan). Si manquante, ajoute-la.

5. **Commit 02f257f a délibérément NOT ajouté LoopPadStore au
   `NVS_DESCRIPTORS[]`** — Phase 2 ne touche toujours pas à ce
   descriptor. Le struct existe, le validator existe, les namespace
   defines existent, mais le descriptor entry est reservé pour Phase 3.

6. **Commit 9d2763e (PotFilterStore bootstrap)** a ajouté un
   `saveBlob` dans `NvsManager::loadAll()`. C'est un fix hors-scope
   Phase 1 pour un bug pré-existant. Phase 2 ne touche pas à
   `NvsManager` sauf si le plan le demande — et il ne le demande
   pas (les changements NvsManager Phase 2 sont exclusivement pour
   Phase 4, voir "Files NOT Modified" du plan Phase 2).

7. **`BankSlot.loopEngine` est nullptr pour toutes les banks** jusqu'à
   ce que Step 4 (Phase 2) fasse l'assignation dans le setup() loop.
   Steps 1-3 peuvent supposer que certains banks auront `loopEngine
   != nullptr` au runtime, mais tant que Step 4 n'est pas appliqué,
   le code ne s'exécute pas de toute façon (pas de bank LOOP active).

## Hardware test checkpoints

Phase 2 introduit le PREMIER comportement LOOP runtime. Les checkpoints
hardware sont ESSENTIELS — un build clean ne prouve rien à ce stade.
Tu STOP à chaque checkpoint et tu présentes à l'utilisateur **exactement
quoi tester**, avec les gestes précis et le comportement attendu.

### Checkpoint A — après Step 4 (test config + LoopEngine assignment)

À ce stade, `LoopEngine.h/.cpp` existe, Step 4 a forcé le bank 8 en
LOOP via `LoopTestConfig.h`, mais aucun pad control n'est wired encore
(Step 5/6/7/8 arrivent juste après). Le but est de vérifier que :
- Le build compile
- Le boot montre Bank 8 initialisé comme LOOP
- Aucun crash, aucune régression NORMAL/ARPEG sur les autres banks
- Le LoopEngine est assigné et vivant (pas nullptr) pour le bank 8

Stop. Présente à l'utilisateur :

> "Step 4 (test config + LoopEngine assignment) terminé. Avant de
> continuer Step 5, teste sur le device :
>
> 1. Boot normal, vérifier que LedController boot bar atteint LED 8
> 2. Serial debug : chercher un message du type '[INIT] Bank 8: LOOP,
>    LoopEngine assigned' ou équivalent — le bank 8 DOIT être marqué
>    LOOP (pas ARPEG, pas NORMAL)
> 3. Switch bank NORMAL → ARPEG → LOOP (bank 8) → NORMAL via hold
>    left + bank pad : aucune régression sur NORMAL/ARPEG, le switch
>    vers LOOP ne doit PAS crasher
> 4. Sur le bank 8 LOOP foreground : ne rien faire de particulier
>    (pas de controls wired yet), juste vérifier que jouer un pad
>    envoie du MIDI ch8 comme si c'était un NORMAL (les pads sont
>    encore live jusqu'à Step 5)
> 5. Reboot : vérifier que la config NVS est préservée (modulo reset
>    attendus si quelqu'un a changé Tool 4 entre Phase 1 et Phase 2)
>
> Quand c'est OK, dis-moi 'Checkpoint A OK' et je continue Step 5."

Tu attends la confirmation explicite avant de toucher au Step 5.

### Checkpoint B — après Step 8 (loop execution order wired)

À ce stade, `processLoopMode()`, `handleLoopControls()`, le dispatch
BANK_LOOP dans `handlePadInput()`, et `LoopEngine::tick()` +
`processEvents()` sont dans le loop order. La bank 8 LOOP est
complètement fonctionnelle pour record / playback / overdub.

Stop. Présente à l'utilisateur :

> "Step 8 (loop execution order) terminé. C'est le premier test
> RÉEL de Phase 2 — le comportement LOOP runtime tourne pour la
> première fois. Teste :
>
> 1. Boot, switch vers bank 8 (LOOP)
> 2. Presser le rec pad : state devient RECORDING (pas de LED visible
>    encore — c'est Phase 4 — mais la logique tourne)
> 3. Jouer quelques pads : noteOn/noteOff sur ch8 visibles dans un
>    MIDI monitor
> 4. Presser le rec pad à nouveau : fin du recording, le loop commence
>    à rejouer les events enregistrés. Tu dois entendre le motif se
>    répéter
> 5. Changer le tempo (pot R1) : la playback doit suivre
>    proportionnellement
> 6. Presser le rec pad pendant la playback : state devient
>    OVERDUBBING. Jouer d'autres pads : overdubbés
> 7. Presser le rec pad à nouveau : merge, retour PLAYING avec les
>    nouveaux events
> 8. Presser le play/stop pad : silence (STOPPED)
> 9. Presser le play/stop pad à nouveau : reprise playback
> 10. Tenir le clear pad 500ms : loop cleared, retour EMPTY
> 11. Switch vers une autre bank et retour : le loop continue en
>     background (tu l'entends sur ch8)
> 12. MIDI panic (rear hold si applicable) : toutes les notes LOOP
>     silenced
> 13. Vérifier que NORMAL et ARPEG sur les autres banks n'ont AUCUNE
>     régression (play notes, switch bank, scale change)
>
> Quand c'est OK, dis-moi 'Checkpoint B OK' et je continue Steps 9-11."

Tu attends la confirmation explicite avant de continuer.

### Checkpoint C — après Step 10/10b (BankManager guards activés + handleLeftReleaseCleanup LOOP branch)

À ce stade, les guards LOOP Phase 1 stubs sont remplacés par les vrais
appels `isRecording()` / `flushLiveNotes()`, et `handleLeftReleaseCleanup`
a sa branche LOOP.

Stop. Présente à l'utilisateur :

> "Step 10/10b (BankManager guards activés + handleLeftReleaseCleanup
> LOOP) terminé. Teste les cas limites du bank switch LOOP :
>
> 1. Bank LOOP en RECORDING : essayer de switcher vers une autre bank
>    → le switch doit être REFUSÉ (la guard isRecording() bloque)
>    Vérifier qu'il n'y a pas de crash, qu'on reste sur le bank 8
> 2. Bank LOOP en OVERDUBBING : idem, switch refusé
> 3. Bank LOOP en PLAYING : switch doit fonctionner, le loop continue
>    en background sur ch8
> 4. Bank LOOP foreground : jouer des pads (live notes routées vers
>    ch8), maintenir hold-left, relâcher hold-left : les live notes
>    doivent être flushed proprement (pas de note bloquée)
> 5. Bank LOOP foreground : jouer des pads pendant le RECORDING,
>    maintenir hold-left, relâcher : le cleanup doit fonctionner
>    sans corrompre les events enregistrés
> 6. Bank switch LOOP → NORMAL avec un loop qui joue : le loop
>    continue (background), flushLiveNotes n'est PAS appelée parce
>    que le loop n'est pas en recording
>
> Quand c'est OK, dis-moi 'Checkpoint C OK'."

### Checkpoint D — après Step 11 (LOOP panic dans midiPanic)

Petit checkpoint, vérifier que le panic sweep fonctionne pour LOOP.

Stop. Présente à l'utilisateur :

> "Step 11 (LOOP panic dans midiPanic) terminé. Test rapide :
>
> 1. Bank LOOP en PLAYING avec des events qui jouent
> 2. Triple-click rear button (ou autre trigger midiPanic configuré)
> 3. Vérifier : silence total, aucune note bloquée, refcounts zeroed
> 4. Reprise normale possible après (pas de state corrompu)
>
> Quand c'est OK, dis-moi 'Checkpoint D OK' et on passe au sign-off."

### Checkpoint final — sign-off Phase 2

Stop. Présente à l'utilisateur :

> "Phase 2 implémentation complète. Liste des commits :
> [liste des hashes + messages]
>
> Pour le sign-off final, teste un cycle d'usage complet 10-15
> minutes avec un focus LOOP :
>
> 1. Bank LOOP : record un motif de 4 mesures, jouer, overdub un
>    deuxième motif par-dessus, écouter, clear, recommencer
> 2. Tester les 3 LoopQuantMode : FREE (record/play sans snap),
>    BEAT (snap à la beat), BAR (snap à la bar). LoopTestConfig.h
>    doit te permettre de changer ça manuellement en recompilant,
>    sinon tester au moins un mode
> 3. Bank LOOP + bank switch rapide vers NORMAL + retour : loop
>    continue proprement, pas de glitch, pas de note bloquée
> 4. NORMAL bank : aftertouch, pitch bend, vel variation — aucune
>    régression
> 5. ARPEG bank : hold on/off, scale change, octave change, pots,
>    background continuation — aucune régression
> 6. Tool 4 : vérifier que NORMAL et ARPEG sont toujours sélectionnables
>    (LOOP n'est pas encore dans Tool 4 — c'est Phase 3)
> 7. Reboot : tout est restauré côté NVS (BankType préservé, etc.),
>    bank 8 redevient LOOP au démarrage grâce au LoopTestConfig.h
>
> Quand tu confirmes le sign-off, j'écris le state dump pour Phase 3."

## State dump pour la session suivante

À la fin de Phase 2 (tous les commits faits, hardware test final OK),
tu écris UN fichier de handoff :

`docs/superpowers/handoff/phase2-to-phase3.md`

Contenu obligatoire :
- Liste des commits Phase 2 avec hashes courts + messages
- Liste exhaustive des fichiers créés ET modifiés par Phase 2
- Rappel : la branche est `loop`, pas main
- Les décisions prises pendant Phase 2 qui dévient du plan (s'il y en
  a) — par exemple, line numbers ajustés, snippets adaptés, fixes
  improvisés, audit findings non documentés qui ont nécessité un patch
- Tests hardware passés (checkmarks par scénario, ce qui a marché)
- Tests hardware qui ont nécessité un fix supplémentaire
- Rappels critiques pour Phase 3 :
  - `LoopTestConfig.h` doit être SUPPRIMÉ en Phase 3 (Tool 4 devient
    la source de vérité pour les bank types, et Tool 3 gagne les
    pad roles LOOP)
  - Phase 3 refactore Tool 3 en architecture contextuelle b1 —
    gros changement, lire `loop-spec-toolpotmapping-refactor.md`
    en plus du plan
  - Phase 3 réintroduit le descriptor `LoopPadStore` dans
    `NVS_DESCRIPTORS[]` (déféré en Phase 1 par commit 02f257f)
  - Architecture briefing va devoir être mis à jour en Phase 3
    pour documenter les nouveaux pad roles et le scale ⊥ LOOP
    collision check
- Préférences utilisateur observées pendant la session Phase 2 (en
  plus de celles déjà notées dans phase1-to-phase2.md)
- Build metrics baseline (RAM / Flash) pour tracking de la dérive
- Si des `docs/reference/` docs ont été mis à jour, les lister

Ce fichier est commit + push final de la session (si push autorisé).

## Update doc architecture-briefing.md

**Obligatoire en Phase 2** : `docs/reference/architecture-briefing.md`
doit être mis à jour avec les nouveaux flows runtime Phase 2 :

1. Section "1. Five Critical Data Flows" : ajouter un 6e flow "Loop
   Record → MIDI NoteOn" + "Loop Tick → MIDI Playback" (ou étendre
   le flow existant pad→MIDI avec la branche LOOP)
2. Section "3. Invariants" : ajouter I7 "LoopEngine refcount atomicity"
   similaire à l'invariant arp
3. Section "5. File Map by Domain" : ajouter la ligne "Loop" avec
   `LoopEngine.cpp/.h`, `LoopTestConfig.h`, et les fichiers touches
4. Section "6. Dirty Flags & Event Queues" : ajouter la ligne
   "Loop events" pour le MAX_PENDING=48 queue du LoopEngine

Cette update est un commit séparé, typiquement avant le sign-off
final. Elle n'attend pas la fin des hardware tests — le doc peut être
mis à jour dès que le code est stabilisé.

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
  jamais, surtout Phase 2 où les checkpoints sont la seule vraie
  vérification de correctness
- "Le todo est écrit, je peux marquer completed avant d'avoir fini" →
  NON, completed = quand le sous-step est PROUVÉ fini
- "LoopEngine est long, je peux skimmer certaines sections" → NON,
  lis tout, c'est le fichier principal de Phase 2
- "Phase 1 a bien marché, Phase 2 va bien marcher aussi" → NON, Phase 2
  est beaucoup plus risquée parce qu'elle introduit du vrai runtime

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

Si tu rencontres un bug runtime hardware sur un checkpoint :

1. Ne marque PAS le checkpoint comme OK
2. Demande à l'utilisateur le comportement observé exact
3. Propose un diagnostic basé sur la lecture du code que tu viens
   d'écrire + le serial debug si fourni
4. Propose un fix ciblé APRÈS diagnostic, jamais avant
5. Re-test hardware après le fix avant de marquer OK

## Démarrage

Annonce à l'utilisateur (texte exact) :

"Session Phase 2 LOOP démarrée. Branche loop (existante depuis Phase 1).
Je commence par lire le contexte projet (9 fichiers obligatoires dont
le handoff phase1-to-phase2) puis le plan Phase 2 intégralement
(2 lectures). Je présenterai un résumé de ce que je vais faire AVANT
de toucher au code, et chaque diff AVANT chaque edit. Hardware
checkpoints aux Step 4, Step 8, Step 10/10b, Step 11, et final.
Aucun push automatique. C'est parti."

PUIS lance la lecture des 9 fichiers contexte sans attendre.
````

---

# Prompt 2 — Vérification Phase 2

> **Copier-coller le bloc ci-dessous dans une session Claude Code FRAÎCHE et SÉPARÉE de la session d'implémentation. Idéalement le lendemain ou au moins après /clear.**

````
Tu vas auditer l'implémentation de Phase 2 LOOP en mode READ-ONLY
strict. Aucun edit sur le code, aucun edit sur les plans. Ton seul
output est un rapport structuré.

## Mission

Trouver TOUS les problèmes dans l'implémentation Phase 2 :
- Bugs de compilation (build clean ne suffit pas — cherche les bugs
  qui sont silencieux à l'avertissement)
- Bugs runtime potentiels (race conditions, off-by-one, wrong
  signatures appelées, refcount underflow, orphan notes, stuck pile
  positions)
- Mismatches plan ↔ implémentation (steps non appliqués, snippets mal
  retranscrits, side effects oubliés)
- Régressions sur le comportement existant (NORMAL, ARPEG, scale
  change, bank switch, NVS load, Tool 3/4/5/6/7)
- Régressions sur les guards Phase 1 que Phase 2 était censée
  "activer" — vérifier que les stubs LOOP de Phase 1 sont bien
  remplacés, pas ajoutés à côté
- Findings F (optimisations, dead code, defensive guards manquants)

## Focus spécifique Phase 2

Phase 2 introduit le PREMIER comportement LOOP runtime. Les points
sensibles à auditer prioritairement :

1. **Refcount atomicity** : noteOn sur refcount 0→1, noteOff sur 1→0
   uniquement. Pas de send si transition incomplète. Cohérent avec
   l'invariant existant ArpEngine.
2. **Quantize boundary crossing** (pas exact-equality) : le plan
   référence le pattern `_lastDispatchedGlobalTick` de l'audit B1 pass 2.
   Vérifier que le code utilise le CROSSING detection, pas un
   `% boundary == 0` naïf qui raterait un boundary sur tick catch-up.
3. **`flushActiveNotes(hard)`** : mode soft ne clear pas la pending
   queue, mode hard si. Vérifier les callers de chaque mode.
4. **`_overdubActivePads[48]` bitmask** : ensure `stopOverdub()` ne
   fire de noteOff QUE pour les pads qui ont reçu un `recordNoteOn`
   pendant le current overdub session.
5. **Pile 0→1 transition auto-play** (HOLD OFF) : vérifier que la
   logique LoopEngine HOLD OFF matche le pattern ArpEngine, avec le
   respect du quantize start.
6. **`setLiveNote` / `releaseLivePad`** (per-pad live-press tracking,
   audit B1 2026-04-06) : vérifier que le cleanup est idempotent,
   pas de double-free, pas d'orphan.
7. **`BankManager::switchToBank` LOOP guards ACTIVÉS** : Phase 1 avait
   des stubs inertes, Phase 2 Step 10 les remplace. Vérifier que les
   stubs ont disparu et que les vrais appels `isRecording()` et
   `flushLiveNotes()` sont là.
8. **`handleLeftReleaseCleanup` LOOP branch** : nouvelle branche Phase 2
   Step 10b. Vérifier qu'elle ne casse pas la logique NORMAL/ARPEG
   existante qui vivait déjà dans cette fonction.
9. **`midiPanic` LOOP flush** : Phase 2 Step 11. Doit flush TOUTES les
   LoopEngine, hard mode.
10. **`processLoopMode`, `handleLoopControls`, dispatch BANK_LOOP dans
    `handlePadInput`** : vérifier qu'ils sont appelés au bon endroit
    du loop order Step 8 (critical path vs secondary path).

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
| "Build clean = code correct" | Build clean = compile. Cherche les bugs LOGIQUES dans le diff — encore plus critique en Phase 2 qu'en Phase 1 |
| "Tous les snippets sont là, donc la phase est faite" | Vérifie que CHAQUE snippet est appliqué ET que les SIDE-EFFECTS sont gérés |
| "Le plan était audité, donc l'implémentation est forcément OK" | Le plan est une INTENTION. Tu vérifies l'EXÉCUTION |
| "Audit fix B1 mentionné → B1 corrigé" | Vérifie que le code patché correspond au fix décrit |
| "Pas besoin de relire CLAUDE.md" | Si, relis-le |
| "30 min suffisent pour cette phase" | Non. Phase 2 est grosse. Lis tout. Vérifie tout. |
| "Les checkpoints hardware ont été passés, donc le runtime est OK" | Les checkpoints user sont faillibles. Tu dois trouver ce qu'ils n'ont pas attrapé |
| "LoopEngine ressemble à ArpEngine, c'est forcément correct" | Non. La différence est exactement ce qui compte — vérifie les deltas |

## Contexte mandatory à charger AVANT toute analyse

Lis intégralement, dans cet ordre :

1. `.claude/CLAUDE.md` — spec projet
2. `docs/reference/architecture-briefing.md` — invariants runtime (mis
   à jour en Phase 2 normalement, vérifier que l'update est cohérente)
3. `docs/reference/nvs-reference.md` — patterns NVS
4. `docs/superpowers/specs/2026-04-06-loop-slot-drive-design.md` — spec
   slot drive (pour comprendre l'architecture LoopEngine)
5. `docs/known_issues/known-bugs.md` — bugs trackés (vérifier que
   Phase 2 n'a pas réintroduit B-001/B-002 et n'a pas accidentellement
   affecté B-004/B-005/B-006)
6. `docs/plans/loop-phase2-engine-wiring.md` — LE PLAN, ta source de
   vérité pour cet audit. À lire 2 fois.
7. `docs/superpowers/handoff/phase2-to-phase3.md` — le state dump de la
   session précédente. Note les "décisions prises qui dévient du plan"
   et les vérifie spécifiquement.
8. `docs/superpowers/handoff/phase1-to-phase2.md` — pour contexte
   Phase 1 (audit fixes déjà en place que Phase 2 ne devait PAS
   casser)
9. La sortie de :
   ```
   git log loop --oneline -30
   git log main..loop --oneline
   git diff main..loop --stat
   ```
   pour avoir une vue d'ensemble des commits Phase 1 + Phase 2.
10. Le diff spécifique Phase 2 :
    ```
    git log --oneline --reverse main..loop | head -20  # identifier le
    # premier commit Phase 2 (après ceux de Phase 1), puis
    git diff <dernier-phase1-commit>..loop --stat
    git diff <dernier-phase1-commit>..loop              # gros, lis-le par section
    ```

## Méthodologie

### Étape 1 — Cartographie commit ↔ Step
Pour chaque Step du plan Phase 2 (1 à 11, 10b inclus), identifier :
- Quel commit a appliqué ce step (hash + position dans `git log`)
- Quels fichiers ont été modifiés par ce commit
- Si un step est manquant : finding bloquant immédiat

### Étape 2 — Vérification snippet par snippet
Pour CHAQUE sous-step du plan (toutes les sections du Step 1, Step 2,
etc.), vérifier dans le code actuel via Read :
- Le code correspond-il au snippet attendu ?
- Les références croisées (line numbers, function names, includes)
  sont-elles correctes ?
- Les side-effects sont-ils gérés (variables init, includes ajoutés,
  validators à jour, dispatching correct dans main loop) ?

### Étape 3 — Cross-check audit fixes Phase 2
Le plan Phase 2 contient plusieurs "AUDIT FIX" ou "AUDIT NOTE" :
- A1 (NvsManager getLoadedLoopQuantizeMode utilisation — doit matcher
  ce que Phase 1 Step 3e a setup)
- A4 + Q1 (midiPanic LOOP flush, Step 11)
- B1 multi-pass (per-pad live tracking `_liveNote[48]`, `setLiveNote`/
  `releaseLivePad`, crossing detection `_lastDispatchedGlobalTick`)
- B2 (OVERDUBBING branch pending events)
- B7 (floor 1ms défensif contre division par zéro)
- D1/V1 (handleLeftReleaseCleanup LOOP branch, Step 10b)
- D-PLAN-1 (membres `_pendingKeyIsPressed`/`_pendingPadOrder`/
  `_pendingBpm` déclarés dans LoopEngine.h)
- Design #1 redesign (noteOn AND noteOff routed through schedulePending,
  gate length preserved on sustained notes)

Pour CHAQUE audit fix, vérifier que le code dans la branche `loop`
correspond à ce que l'audit fix demande. Pas juste "le commentaire est
là", mais "le code fait ce que le commentaire dit".

### Étape 4 — Cross-check audit fixes Phase 1 (non-régression)
Phase 2 ne doit PAS avoir cassé les audit fixes Phase 1. Vérifier :
- BUG #1 (validateBankTypeStore clamp gated) : le validator dans
  KeyboardData.h est inchangé
- BUG #2 (BankManager guards BUG #2) : Phase 2 doit ACTIVER les stubs,
  pas les supprimer. Le commentaire BUG #2 qui documente le pattern
  doit soit rester, soit être remplacé par le vrai code qui suit le
  pattern
- BUG #3 (PotMappingStore loopMap) : inchangé en Phase 2
- GAP #5 (loopEngine = nullptr init) : remplacé par l'assignation
  Step 4 pour les banks LOOP, nullptr pour les autres
- A1 (NvsManager _loadedLoopQuantize) : inchangé en Phase 2, utilisé
  par Phase 2 Step 4a
- B6 (ScaleManager LOOP early return full sync) : inchangé, Phase 2
  ne touche pas ScaleManager
- C1 (CONFIRM_LOOP_REC expiry case) : inchangé, Phase 2 va trigger
  le confirm mais le rendering reste Phase 4
- C2 (LoopPadStore single definition) : inchangé
- PotFilterStore bootstrap (commit 9d2763e) : inchangé

### Étape 5 — Build verification
Run le build et vérifier :
```
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -50
```
- Build clean
- Zéro warning lié à Phase 2
- Métriques RAM/Flash vs baseline Phase 1 (handoff dit RAM 16.1%,
  Flash 20.6%). Expect RAM growth modérée (LoopEngine instances),
  Flash growth modérée (code LoopEngine). Si RAM > 22% ou Flash > 30%,
  investiguer la cause.

### Étape 6 — Static checks
Pour chaque struct/enum/constant ajouté en Phase 2, vérifier :
- Includes cohérents (LoopEngine.h inclu là où il faut)
- Membres privés de LoopEngine tous déclarés (en particulier les
  `_pending*` stash referencés par l'audit D-PLAN-1)
- `static_assert` sur les tailles (MAX_PENDING, LoopEvent size)
- Forward declarations correctes (LoopEngine déjà forward-declared
  dans BankManager.h depuis Phase 1)

### Étape 7 — Behavior regression check (analyse statique)
Phase 2 doit préserver intégralement le comportement NORMAL et ARPEG.
Vérifier via lecture du diff :
- Aucune modification de ArpEngine.cpp/.h
- Aucune modification de ScaleResolver.cpp/.h
- Aucune modification du flow NORMAL dans processNormalMode
- Aucune modification du flow ARPEG dans processArpMode
- `handlePadInput` : le dispatch BANK_LOOP est un AJOUT, pas un remplacement
- `loop()` : les appels `LoopEngine::tick()` + `processEvents()` sont
  insérés, pas en remplacement d'un autre appel

### Étape 8 — Invariants runtime
Pour chaque invariant LoopEngine, vérifier par lecture de code :
- **Refcount atomicity** : grep pour `noteRefIncrement` / `noteRefDecrement`,
  vérifier qu'ils gate les sends MIDI sur les transitions 0→1 et 1→0
- **No orphan notes** : vérifier que `_lastResolvedNote[padIndex]`
  pattern est respecté (probably via `_liveNote[48]` en Phase 2)
- **Quantize crossing** : grep `_lastDispatchedGlobalTick`, vérifier
  le pattern `while (tick > _lastDispatchedGlobalTick && (tick %
  boundary) crosses)`, pas un exact-equality
- **Recording lock** : grep `isRecording()`, vérifier que BankManager
  utilise la méthode (pas un accès direct à un membre private)
- **Static allocation** : grep `new ` et `malloc` dans LoopEngine —
  résultat attendu : vide (convention CLAUDE.md "no new/delete at
  runtime")

### Étape 9 — Hardware test scenarios (à passer à l'utilisateur)
Phase 2 a déjà eu des hardware checkpoints (A, B, C, D, final) pendant
l'implémentation. Pour l'audit, demande à l'utilisateur de RE-tester
les scénarios critiques :
- Liste les checkpoints qui ont été passés selon le handoff
- Pour chaque checkpoint, propose un re-test rapide à l'utilisateur
- Demande spécifiquement les scénarios qui ont été un peu rapidement
  validés
- Ajoute des scénarios edge case que l'implémenteur a pu oublier :
  - REC tap pendant hold-left : comportement ?
  - Bank switch pendant l'attente du quantize boundary : state
    préservé ou corrompu ?
  - Loop très long (plusieurs bars) + clear à mi-loop : pas de
    stuck notes ?
  - Overdub sur un loop qui contient une note qui va wrapper au
    moment du stopOverdub : l'event merge correctement ?

### Étape 10 — Rapport structuré
Produire un rapport markdown au format :

```
# Audit Phase 2 LOOP — [date]

## Résumé exécutif
- Findings A (bloquants) : N
- Findings B (runtime) : N
- Findings C (incohérences) : N
- Findings D (mismatch plan↔code) : N
- Findings F (optimisations) : N
- Niveau de confiance : HIGH/MEDIUM/LOW
- Recommandation : GO Phase 3 / GO Phase 3 with fixes / REWORK Phase 2

## A — Bugs bloquants
[findings ou "néant — vérifié N catégories"]

## B — Bugs runtime
[idem, attention particulière aux refcount, orphan notes, quantize]

## C — Incohérences inter-step
...

## D — Mismatches plan↔code
...

## F — Optimisations
...

## Checklist par Step
- Step 1 (LoopEngine.h/.cpp): audité, N findings
- Step 2 (test config): audité, N findings
- Step 3 (main.cpp static instances): audité, N findings
- Step 4 (setup() LoopEngine assignment): audité, N findings
- Step 5 (processLoopMode): audité, N findings
- Step 6 (handleLoopControls): audité, N findings
- Step 7 (handlePadInput dispatch): audité, N findings
- Step 8 (loop order): audité, N findings
- Step 9 (reloadPerBankParams LOOP): audité, N findings
- Step 10 (BankManager guards activés): audité, N findings
- Step 10b (handleLeftReleaseCleanup LOOP): audité, N findings
- Step 11 (midiPanic LOOP): audité, N findings

## Non-régression Phase 1
- BUG #1 validateBankTypeStore : [inchangé / modifié]
- BUG #2 BankManager guards : [activé correctement / cassé]
- ... (liste complète des audit fixes Phase 1)

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
- "Quels sont les snippets les plus piégeux du plan Phase 2 ?"
  (probablement : le refcount noteOn/noteOff, la pending queue
  avec shuffle/chaos, le quantize crossing, le handleLeftReleaseCleanup
  LOOP branch qui coexiste avec les branches NORMAL/ARPEG existantes)
- "Quels sont les sous-steps que l'implémenteur a probablement bâclés
  en fin de session, quand sa concentration baisse ?"
- "Qu'est-ce que le plan dit que le code n'arrive pas à vérifier
  automatiquement (ex: 'refcount atomique' — comment je le vérifie
  en lisant le code ?)"
- "Phase 2 a plus de hardware checkpoints que Phase 1. L'implémenteur
  a peut-être sauté un checkpoint en le marquant OK pour gagner du
  temps. Vérifie que chaque checkpoint a un OK explicite dans le
  chat de la session précédente"

## Anti-flemme

Cette session est en Opus 1M. Pas de pression temporelle. Lis tout.
Vérifie tout. Si tu te dis "ça doit être bon", relis encore une fois.
Phase 2 est la phase qui introduit le premier vrai runtime LOOP —
les bugs cachés ici vont être visibles pendant toutes les phases
suivantes.

Symptômes de flemme à reconnaître :
- "J'ai vu beaucoup de findings, je peux arrêter" → NON, finis l'audit
- "Le plan est bien fait, pas besoin de tout vérifier" → NON, vérifie
- "Cette section est trop longue, je vais skim" → NON, lis
- "Build clean, pas besoin de chercher de bugs runtime" → NON, cherche
- "Les hardware checkpoints sont passés, c'est validé" → NON, les
  checkpoints ne couvrent pas tout. Cherche les edge cases qu'ils
  n'ont pas attrapés.
- "LoopEngine est trop long à auditer ligne par ligne" → Si, pas
  de shortcut, c'est le fichier principal

## Démarrage

Annonce à l'utilisateur :

"Audit Phase 2 démarré. Mode READ-ONLY strict. Aucune modification de
code, plan, ou commits. Lecture contexte (10 sources incluant 2
handoffs + diff Phase 2 seul) → cartographie commit/step → vérification
snippet par snippet → cross-check audit fixes Phase 2 ET Phase 1
(non-régression) → build verification → static checks → behavior
regression check → invariants runtime → rapport structuré final. Je
ne vais pas auto-valider : si tout semble OK après mon premier pass,
je chercherai plus profond avant de livrer le rapport."

PUIS lance la lecture du contexte sans attendre.
````

---

## Pour les phases suivantes (Phase 3 à Phase 6)

Ce doc couvre Phase 2. Pour les Phases 3-6, **dupliquer ce fichier** en
adaptant :

1. Le numéro de phase dans le titre et les références
2. Le plan ciblé (`docs/plans/loop-phaseN-...md`)
3. Les hardware checkpoints (chaque phase a ses propres scénarios)
4. La référence au handoff de la phase précédente (lecture obligatoire
   en plus du contexte projet)
5. Les rappels critiques spécifiques à la phase (audit fixes déjà en
   place, déviations Phase N-1 à ne pas casser)
6. Le state dump final pour la phase suivante

Le squelette anti-bias et anti-flemme reste identique — c'est
intentionnel, ces biais ne disparaissent pas entre phases.

**Naming convention** :
- `docs/prompts/loop-phase1-prompts.md`
- `docs/prompts/loop-phase2-prompts.md` (ce fichier)
- ...
- `docs/prompts/loop-phase6-prompts.md`

Et les handoffs :
- `docs/superpowers/handoff/phase1-to-phase2.md`
- `docs/superpowers/handoff/phase2-to-phase3.md` (à écrire en fin Phase 2)
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

### Pourquoi les checkpoints Phase 2 sont plus nombreux que Phase 1
Phase 1 était de la non-régression pure (aucun comportement runtime
changé). Phase 2 introduit le premier vrai runtime LOOP, donc chaque
sous-section (LoopEngine assignment, processLoopMode, BankManager
guards, midiPanic flush) mérite son propre checkpoint parce qu'elles
peuvent casser indépendamment.

### Pourquoi pas de "estimation de durée"
CLAUDE.md du projet interdit les estimations de temps. Les prompts ne
disent jamais "ça devrait prendre X heures". L'effort attendu est
"maximum sur chaque step", point.
