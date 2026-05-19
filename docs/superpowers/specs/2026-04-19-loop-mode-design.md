# ILLPAD48 V2 — Mode LOOP : design haut niveau

> **MAJ 2026-05-17 (post nettoyage LOOP)** — Spec corrigée pour refléter :
> - LED v9 brightness unification (commit `3b85011`) : 4 fields FG fusionnés
>   en un seul `_fgIntensity`. §28 Q3 et Q4 actualisés inline. Q4 caduque.
> - Phase 1 LOOP close (commit `2624b12` Task 4 défensif). Plan archivé.
> - Plan Phase 2 à rédiger from scratch depuis cette spec + code main
>   (l'ancien plan archive-based jeté, cf [STATUS.md](../../../STATUS.md)).
> - Invariants buffer LOOP : [docs/reference/loop-buffer-invariants.md](../../reference/loop-buffer-invariants.md).
>
> **MAJ 2026-05-19 (post Master Sync + OD-Sync pivots)** — Spec amendée pour refléter :
> - **Master Sync** : pivot algorithmique close-record vers Auto-Stop boundary-aware
>   + master grid anchor (§7 réécrit, §17 étendu, §23.5 invariant amendé, §24
>   non-goal "pas de rescale silencieux" ajouté, §28 ligne Master Sync). Cf
>   [`Illpad_Master_Sync.md`](Illpad_Master_Sync.md) self-suffisante.
> - **OD-Sync** : pivot algorithmique overdub vers immediate-merge + snapshot
>   1-level Undo/Redo toggle (§8 réécrit, §9 CLEAR contextuel ajouté, §24
>   "Pas d'undo/redo" qualifié 1-level, §28 ligne OD-Sync). Cf
>   [`Illpad_OD_Sync.md`](Illpad_OD_Sync.md) self-suffisante.
> - Commits livrés 2026-05-19 : `89f6c11` → `33149b8` + doc-sync. HW gates
>   G1-G9 (Master Sync) + G1-G15 (OD-Sync) validés.

**Date** : 2026-04-19 (créé) — **révisé 2026-04-20** post Phase 0.1
**Statut** : **VALIDÉ** pour plan d'implémentation LOOP Phase 1→6. Pré-requis Phase 0 LED + Phase 0.1 Tool 8 respec **DONE** (commits `cad7530`, `39d2deb`, `290839d`, `cc379f5`, `6ac9ff3` sur `main`).
**Scope** : LOOP core + Slot Drive + refactor Tool 3 b1. Haut niveau, sans code. Les plans d'implémentation par phase viennent dans des documents séparés.
**Sources** : consolidation de `docs/archive/2026-04-02-loop-mode-design.md` + `docs/archive/2026-04-06-loop-slot-drive-design.md` (**tous deux marqués obsolètes** — ne pas lire, superseded par ce document), `docs/reference/architecture-briefing.md`, état du code `main` au 2026-04-20.
**Spec compagnon** : `docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md` (grammaire LED unifiée, **implémentée** Phase 0 + respec Tool 8 Phase 0.1 — source de vérité pour §21).
**Tool 8 respec compagnon** : `docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md` (single-view 6 sections, implémenté commit `cc379f5`).

---

## Partie 1 — Cadre

### §1 — Intention musicale et place dans ILLPAD48

ILLPAD48 V2 propose huit slots de performance, chacun relié à un canal MIDI fixe. Trois types de banks existent aujourd'hui sur `main` : **NORMAL** (jeu mélodique classique sur gamme/mode), **ARPEG** (arpégiateur séquentiel quantisé à l'horloge), et **ARPEG_GEN** (arpégiateur génératif, walk pondéré sur pile). Le mode **LOOP** est le quatrième type, déclaré en Phase 1 (enum `BANK_LOOP = 2`, stores NVS prêts) mais sans runtime engine — dédié à la construction en temps réel de boucles rythmiques.

L'idée directrice : offrir un boucleur intégré à l'instrument, jouable d'une seule main, sans sortir du flux de performance. Le musicien enregistre un motif avec les doigts, l'ILLPAD le cale seul sur la mesure, puis il tourne en fond pendant que le musicien change de bank, déclenche un arpège, joue une ligne mélodique par-dessus, rappelle une autre boucle, ou compose un empilement.

Le mode LOOP est volontairement **percussif et non-mélodique** : chaque pad déclenche une note MIDI fixe (convention General MIDI à partir du kick C2). Pas de gamme, pas de pitch, pas d'aftertouch, pas de pitch bend. Cette simplicité est assumée — un pattern de batterie capté en 2 secondes vaut mieux qu'un système mélodique sophistiqué mais hors de portée d'une main.

Le **Slot Drive** prolonge cette philosophie : 16 boucles persistantes, rappelables à tout moment y compris après reboot, accessibles uniquement via gestes hardware (pas d'écran de gestion de fichiers, pas d'interface de banque). Le drive est une **extension naturelle du geste musical**, pas un sous-système de production.

### §2 — Périmètre de ce document

**Ce document couvre** :
- Les intentions musicales et les workflows utilisateur du mode LOOP et du Slot Drive
- Les interactions avec les systèmes existants (NORMAL, ARPEG, ControlPads, clock, pots, LEDs, NVS)
- Le refactor du Tool 3 vers une architecture contextuelle "b1" (rôles dépendant du bank type)
- Les invariants, limites et non-goals
- Les questions de design encore ouvertes

**Ce document ne contient pas** :
- De code, de snippets, de signatures de fonction, de formats binaires
- De découpage phase par phase de l'implémentation
- De mapping détaillé champ-à-champ des structures NVS
- De pseudo-code d'algorithme

Ces éléments viendront dans des **plans d'implémentation par phase**, rédigés séparément après validation de ce document.

---

## Partie 2 — Les trois briques fonctionnelles

### §3 — LOOP core

Le **LOOP core** est le moteur d'enregistrement et de playback. Il vit dans chaque bank configurée en mode LOOP et expose une petite machine d'état :

| État | Description |
|---|---|
| **EMPTY** | Aucun contenu. Prêt à enregistrer. |
| **RECORDING** | Capture en cours. Les frappes de pad sont stockées avec leur timing au microseconde près. |
| **PLAYING** | La boucle tourne, calée sur le tempo courant. Le musicien peut jouer par-dessus. |
| **OVERDUBBING** | Une couche supplémentaire est en cours d'enregistrement, la boucle continue à jouer. |
| **STOPPED** | La boucle existe mais ne joue pas. Contenu préservé. |

Le moteur est **éphémère par défaut** : le contenu d'une boucle est perdu au reboot si elle n'a pas été sauvegardée dans un slot du drive. Les **paramètres d'effets** (shuffle, chaos, velocity pattern) sont en revanche persistants per-bank, stockés en NVS — ils survivent au reboot même sans slot save. Les slots embarquent aussi ces params dans leur fichier, ce qui crée une redondance assumée : sans slot chargé, la bank mémorise ses réglages NVS ; au load d'un slot, les params du slot écrasent le state courant.

Le LOOP core est conçu pour **tourner en fond** : une boucle lancée sur une bank LOOP continue à jouer quand l'utilisateur change de bank. Comme les arpèges, il n'existe **jamais** de "mise en sommeil" d'un engine — toute bank LOOP existe en RAM et sa boucle joue ou ne joue pas selon son état. **Nombre max de banks LOOP simultanées** : **MAX_LOOP_BANKS = 4** (acté Phase 2, commit `6c0b4d8`). Mesure réelle SRAM : ~9.7 KB par engine × 4 = ~38.8 KB sur 320 KB disponibles (12 %). Constante définie dans `src/core/KeyboardData.h:676`.

Trois pads de contrôle dédiés pilotent l'engine : **REC**, **PLAY/STOP**, **CLEAR**. Ces trois pads ne sont actifs que sur une bank LOOP en foreground. Sur une bank NORMAL ou ARPEG, les mêmes pads physiques jouent comme des pads musicaux ordinaires.

### §4 — Slot Drive

Le **Slot Drive** est un mini-filesystem de **16 emplacements persistants** en flash, baptisés slots 0 à 15. Chaque slot contient soit rien, soit une boucle sauvegardée (events + structure + paramètres d'effets à l'instant de la sauvegarde).

Les slots sont **persistants indéfiniment** : ils survivent au reboot, au flash d'un nouveau firmware (tant que la partition reste), et ne sont jamais écrasés sans action explicite de l'utilisateur. Le stockage est assuré par **LittleFS**, qui gère nativement l'atomicité des écritures et le wear leveling du flash.

L'accès aux slots passe par **16 pads slot** configurables en Tool 3 (sous-page LOOP). Ces 16 pads sont actifs **uniquement** quand toutes ces conditions sont réunies :
1. La bank foreground est de type LOOP
2. Le bouton gauche (`LEFT`) est maintenu (hold-left)
3. L'engine n'est pas en RECORDING ou OVERDUBBING

En dehors de ces conditions, les mêmes pads physiques jouent comme pads musicaux, ou comme rôles ARPEG selon la configuration — voir §5.

Trois gestes, un mapping simple :
- **Tap court** (relâché entre 300 et 1000 ms) → **charger** le slot
- **Press long** (maintenu 1 s sans relâcher) → **sauvegarder** dans le slot
- **Press du pad CLEAR puis press d'un slot pad** (pendant que CLEAR est tenu) → **supprimer** le slot

Aucun écran, aucun menu, aucune confirmation par texte. Le feedback est entièrement LED (ramp pendant le hold, blink vert/blanc/rouge selon l'issue).

### §5 — Refactor Tool 3 → rôles contextuels b1

**Clarification layers avant les rôles.** ILLPAD48 a deux layers d'interaction sur les pads :

- **Layer musical** (press direct, sans hold) : c'est ici que les pads jouent des notes. Certains rôles squattent aussi ce layer et volent donc des pads musicaux — les **ControlPads (Tool 4, MIDI CC/latch/momentary)** et les **3 controls LOOP (REC, PLAY/STOP, CLEAR)**. ARPEG n'a **aucun** rôle sur le layer musical — son play/stop passe par le geste `LEFT + double-tap bank pad` (layer hold-left).
- **Layer hold-left** (LEFT maintenu) : aucun pad ne joue de note. Les rôles ici n'entrent jamais en compétition avec la musique. C'est où vivent les bank pads, les scale/root/mode/chrom, les octave/hold ARPEG, et les 16 slot pads LOOP.

Cette distinction est importante pour comprendre l'impact du Slot Drive : ajouter 16 slot pads ne réduit **pas** la surface musicale disponible, parce que les slots sont sur le layer hold-left. Le coût réel du mode LOOP sur la musique est les **3 controls REC/PLAY/STOP/CLEAR** qui sont sur le layer musical.

Le Tool 3 actuel (`ToolPadRoles`) gère une **liste plate de rôles** : BANK (8) + SCALE (7 root + 7 mode + 1 chrom) + ARP (1 hold + 4 octave) = 28 pads contrôle, visibles tout le temps, toutes collisions interdites. Note : le play/stop ARPEG n'est pas un pad dédié — il passe par le geste `LEFT + double-tap sur un bank pad` (donc sur le layer hold-left, sans consommer de position pad).

Le Slot Drive ajoute **19 rôles nouveaux** (3 controls LOOP + 16 slots) dont la majorité (les 16 slots) n'ont de sens que sur une bank LOOP. Deux approches possibles :

1. **Conserver l'architecture plate** et ajouter 19 rôles globaux → Tool 3 surchargé, validation plate insensée (pourquoi le slot 5 et le Root C seraient-ils exclusifs alors qu'ils n'agissent jamais au même moment ?).
2. **Passer à une architecture contextuelle "b1"** : les rôles dépendent du bank type courant. Un même pad physique peut porter plusieurs rôles contextuels — par exemple "Root C" en contexte ARPEG et "Slot 5" en contexte LOOP. Validation collision par contexte.

C'est la seconde approche qui est retenue, pour la cohérence de validation et la lisibilité du Tool 3. Elle évite des interdictions arbitraires inter-contexte.

Le refactor b1 organise les rôles en trois catégories :

| Catégorie | Rôles | Layer | Visibles |
|---|---|---|---|
| **Globaux** (toujours actifs) | 8 bank pads | hold-left | Partout |
| **Contexte ARPEG** | 7 root, 7 mode, 1 chrom, 1 hold, 4 octave = 20 rôles | hold-left | FG ARPEG |
| **Contexte LOOP** | 3 controls (REC, PLAY/STOP, CLEAR) + 16 slots = 19 rôles | mixte (3 controls = musical, 16 slots = hold-left) | FG LOOP |

Les gestes `LEFT + double-tap sur bank pad` (play/stop ARPEG et LOOP, §19) n'apparaissent pas dans cette table : ils sont rattachés au bank pad comme fonction secondaire du geste, pas à un pad contextuel dédié. Pour LOOP, c'est un deuxième chemin vers le même toggle que le pad PLAY/STOP dédié ; pour ARPEG, c'est le seul chemin.

**Règles de collision** (mises à jour pour intégrer Tool 4 ControlPads) :

1. **Bank pads sacrés** — interdits dans tout autre rôle, peu importe le contexte ou le layer. Jamais de collision possible avec un bank pad.
2. **Layer musical — exclusion mutuelle** : sur le même pad physique, pas plus d'un rôle parmi : `ControlPad (Tool 4 MIDI CC)`, `LOOP REC`, `LOOP PLAY/STOP`, `LOOP CLEAR`. Tous ces rôles se déclenchent par press direct (sans LEFT), ils ne peuvent pas cohabiter sur le même pad.
3. **Layer hold-left — par contexte** : dans un contexte donné, un pad ne peut porter qu'un seul rôle hold-left (pas deux "Root C" en ARPEG, pas deux "Slot 3" en LOOP).
4. **Hold-left inter-contexte — autorisé** : pad 30 = "Root C" en ARPEG **ET** "Slot 5" en LOOP → OK. Encouragé pour optimiser l'espace.
5. **Cross-layer (hold-left ↔ musical) — orthogonal** : pad 30 = "Slot 5" en LOOP (hold-left) **ET** "ControlPad CC 74" (musical) → OK. Le pad a deux comportements selon LEFT pressé ou pas.

Le Tool 3 est réorganisé en **trois sous-pages** navigables : Banks / ARPEG / LOOP. La validation de collision applique ces 5 règles. Le Tool 4 doit être étendu pour appliquer la règle 2 : refus d'assigner un ControlPad sur un pad déjà utilisé comme LOOP REC / PLAY-STOP / CLEAR.

---

## Partie 3 — Workflows utilisateur

### §6 — Configurer une bank LOOP

Tout commence en mode setup (boot + appui long sur le bouton arrière). Trois tools sont impliqués :

1. **Tool 5 — Bank Config** : le musicien cycle le type de la bank voulue. L'ordre est NORMAL → ARPEG → LOOP → NORMAL. Maximum `MAX_LOOP_BANKS = 4` banks en LOOP (le tool refuse au-delà — acté Phase 2, cf §3 + `KeyboardData.h:676`). Pour chaque bank LOOP, un **mode de quantization** (No quantize / Beat / Bar, **défaut Bar**) est paramétrable pour piloter le comportement play/stop, load, **et close-record** (cf §17 et Master Sync §3).

   > **Refactor Tool 5 — validé et tranché 2026-05-17** : audit confirme que la structure actuelle (cycle linéaire 5 états + multi-lignes ARPEG_GEN) ne supporte pas élégamment l'ajout LOOP. Refacto traité dans spec dédiée [`2026-05-17-tool5-bank-config-refactor-design.md`](2026-05-17-tool5-bank-config-refactor-design.md) — vue tableau matriciel banks×params, nav 2D, INFO auto-update, validator `quantize` discriminé par type. **Refacto exécuté pré-Phase 2 LOOP** (Option A — bank LOOP créable via UI avant LoopEngine runtime). Q6 §28 originale (refactor "deferred") **inversée**.

2. **Tool 3 — Pad Roles, sous-page LOOP** : le musicien assigne les 3 pads de contrôle (REC, PLAY/STOP, CLEAR) et optionnellement les 16 slot pads. Les slots inutilisés restent `0xFF` (non assignés). La sous-page LOOP ne présente les slot roles que si le refactor b1 est complet — sinon, seulement les 3 controls. Rien n'oblige à assigner les 16 slots d'un coup : un musicien peut très bien commencer avec 2 ou 4 slots et étendre plus tard.

3. **Tool 7 — Pot Mapping, contexte LOOP** : le musicien configure quels pots pilotent quels paramètres LOOP. Les 8 slots de mapping (4 pots × 2 couches hold/not-hold) reçoivent par défaut : base velocity, vel pattern, vel pattern depth, chaos, shuffle depth, shuffle template, velocity variation, MIDI CC/PB libre. Tous réassignables — y compris en MIDI CC ou pitch bend pour un canal MIDI arbitraire. **Note** : depuis `POTMAP_VERSION = 2`, le tempo n'est plus assignable via Tool 7 — il a un binding fixe sur LEFT + rear pot. Le mapping LOOP Phase 4 ne doit pas réintroduire tempo dans le pool.

Le **Tool 8 — LED Settings** n'a pas besoin de configuration spécifique pour LOOP : les couleurs jaunes utilisées par LOOP viennent de slots de couleur configurables si le musicien veut les nuancer (intensité, hue offset), mais les valeurs par défaut sont déjà chargées.

Au retour en mode jeu, la bank LOOP est prête. Son LED affiche le fond jaune solide dim de l'état EMPTY (voir LED spec §17).

### §7 — Enregistrer un premier loop

> **⚠ MAJ 2026-05-19 — Master Sync pivot** : cette section a été réécrite suite au pivot algorithmique Auto-Stop + master grid anchor (commits `89f6c11` + `edbdd2b`). **Le bar-snap+rescale décrit dans la version antérieure est supprimé du code.** Source de vérité : [`Illpad_Master_Sync.md`](Illpad_Master_Sync.md).

Le musicien est sur la bank LOOP. Le LED jaune est solide dim (EMPTY). Il tape sur le pad **REC** — le LED garde le fond jaune solide mais ajoute un flash rouge à chaque bar (pattern RECORDING, voir LED spec §17), l'engine entre en état **RECORDING**. Mais rien n'est encore capturé : le moteur attend le premier coup.

Le musicien frappe son premier pad. L'horloge d'enregistrement démarre à cet instant précis (pas au tap REC, pour ne pas enregistrer une anacrouse parasite). Selon le `loopQuantize` per-bank, le `_recordStartUs` est **ancré sur la grille master** :
- **FREE** : ancré sur `micros()` (tap-to-tap, hors grille master).
- **BEAT** : ancré sur `lastBeatWallTime` (= dernier master beat boundary). Event[0] reçoit timestamp = phase Δ du hit dans son beat.
- **BAR** : ancré sur `lastBarWallTime` (= dernier master bar boundary). Event[0] reçoit timestamp = phase Δ dans son bar.

Les coups suivants sont enregistrés avec leur décalage temporel en microsecondes par rapport à ce point zéro. **Pas de quantize à l'enregistrement, le micro-timing du groove humain est préservé** — relativement à la grille master pour BEAT/BAR, ou absolu pour FREE.

Quand le motif est complet, le musicien tape à nouveau **REC**. Le comportement dépend du `loopQuantize` per-bank :

#### Cas FREE (hors grille)
`closeRecordingImmediate` synchrone :
1. `_loopDurationUs = rawDur` exact (pas de snap, pas de rescale).
2. **Flush des pads tenus** : noteOff implicite injecté à `_loopDurationUs - 1` dans le buffer pour pads encore tenus.
3. **Transition immédiate vers PLAYING** : la boucle redémarre.

#### Cas BEAT / BAR (Auto-Stop, sur grille master)
Le tap REC final ne ferme PAS la loop immédiatement. Il **arme** un commit au prochain master tick boundary :
1. `_recordingPendingClose = true`, `_recordingPendingCloseTick = computeNextBoundaryTick(quantize)` (next multiple de 24 ticks pour BEAT, 96 pour BAR).
2. **L'état reste RECORDING**, capture continue (le musicien peut continuer à jouer jusqu'au boundary — décision BS-2 "α capture continue").
3. Au boundary tick atteint (`update()` phase 0), `commitRecordingClose` :
   - `_loopDurationUs = boundaryWallTime - _recordStartUs` (multiple entier de quantize unit par construction).
   - Flush des pads tenus à `_loopDurationUs - 1`.
   - `startPlayback` ancré au boundary tick wall time exact. Loop position 0 coïncide avec le master tick.

**Conséquence musicale** : ARP Beat + LOOP BEAT/BAR + LOOP autres banks partagent la grille master clock. Les wraps de loop tombent sur des master tick boundaries. Les wraps multi-loop coïncident.

#### Gestes pendant la fenêtre PENDING_CLOSE (BEAT/BAR uniquement)
- Tap PLAY/STOP : ignoré (no-op).
- 2e tap REC : ignoré (commit ferme, pas d'undo — décision BS-9).
- Long-press CLEAR : refusé (`isLocked()` retourne true).
- Bank switch : silent deny (invariant §23.2 préservé).
- Pad presses musicaux : capturés normalement (α).

> **Pourquoi cette refonte (Master Sync 2026-05-19)** : la version antérieure (bar-snap 25 % deadzone + rescale proportionnel des event timestamps) introduisait des shifts de micro-timing jusqu'à ×1.6 sur loops courts à la frontière de deadzone. Aucun looper de référence (Boss/Mobius/Sooperlooper/EHX/Loopy Pro) ne rescale ses event timestamps stockés au close-record — tous font soit Auto-Stop (attend boundary) soit Round-off (trim/extend silent). ILLPAD V2 a adopté Auto-Stop pour la cohérence avec le principe "all beat in sync" (cf [`Illpad_Master_Sync.md`](Illpad_Master_Sync.md) §1 et recherche industrie 2026-05-19).

Le loop tourne. Le musicien peut maintenant soit le laisser tourner, soit jouer par-dessus (§13), soit ajouter une couche (§8), soit le couper (§9), soit le sauver dans un slot (§11).

### §8 — Overdub

> **⚠ MAJ 2026-05-19 — OD-Sync pivot** : cette section a été réécrite suite au
> pivot algorithmique immediate-merge + snapshot 1-level Undo/Redo toggle
> (commits `eaf5674` → `33149b8`). **Le modèle deferred-merge avec buffer
> temporaire décrit dans la version antérieure est supprimé du code.** Source
> de vérité : [`Illpad_OD_Sync.md`](Illpad_OD_Sync.md).

Pendant qu'un loop joue, taper **REC** une seconde fois fait entrer l'engine en
**OVERDUBBING**. Depuis un **STOPPED-loaded** (boucle chargée, en pause), tap
REC produit la transition équivalente Q5 — l'engine repart en **PLAYING +
OVERDUBBING simultanés** (reprise position 0 + armement overdub). Décision Q5
pré-plan Phase 1 — voir §28.

À l'entrée OD, un **snapshot** du buffer pré-OD est pris dans `_eventsAlternate`
(coût 8 KB SRAM par bank ; cf [`Illpad_OD_Sync.md`](Illpad_OD_Sync.md) §2). Le
LED garde le fond jaune solide et bascule à Amber `CSLOT_VERB_OVERDUB`.

**Immediate-merge** : chaque pad press pendant OD est inséré **directement**
dans le buffer principal `_events[]` à la position courante. La loop "grows
live" — le hit ajouté au cycle N rejoue audible au cycle N+1 sans attente
d'exit. Plus de buffer temporaire `_overdubEvents`, plus de `mergeOverdub`.

**Trois sorties possibles** :

- **Tap REC** → `commitOverdubExit` : exit + commit. Held pads injectent un
  noteOff implicite à `_playPositionUs` (réincarnation B-N2 fix de l'ancienne
  `mergeOverdub`). État → PLAYING. Le snapshot `_eventsAlternate` reste valide
  pour permettre un Undo post-exit via CLEAR court.
- **Tap CLEAR (any duration, rising edge)** → Cancel mid-OD : swap musical
  par note (cf [`Illpad_OD_Sync.md`](Illpad_OD_Sync.md) §6) qui ne touche
  que les notes affectées par la couche OD — couche base et live press
  préservées audibles continues. État → PLAYING (toujours, OD-16 asymétrie
  acceptée depuis STOPPED-Q5).
- **Tap PLAY/STOP** → **no-op** (OD-3 décision). Pour stopper la loop, exit
  OD via REC ou Cancel via CLEAR d'abord.

**Undo/Redo post-exit** : tap CLEAR court (release < `clearLoopTimerMs`) sur
PLAYING ou STOPPED déclenche un toggle Undo/Redo. Même mécanique swap musical.
Trigger LED Option β : EVT_STOP (couche retirée) ou EVT_PLAY (couche
réintégrée) selon diff `_eventCount`. Permet le geste live signature "mute la
couche HH pour un breakdown puis la ramener" (cf G3 HW gate validation).

**Contraintes** :
- **Bank switch refusé** pendant RECORDING et OVERDUBBING. Le musicien doit
  clore avant de changer de bank (invariant §23.2 + §11).
- **Buffer principal plein** (`MAX_LOOP_EVENTS = 1024`) → les events en
  surplus sont droppés silencieusement avec télémétrie viewer
  (`emitLoopBufferFull`). Pas de signal d'erreur visible musicien.

**Suppression du modèle deferred-merge** : le buffer temporaire
`_overdubEvents`, les méthodes `mergeOverdub` et `abandonOverdub` ont été
supprimés. Le geste abandon (jadis tap PLAY/STOP) est remplacé par le Cancel
via CLEAR (plus ergonomique car asymétrique avec tap REC commit).

### §9 — Play / Stop / Clear

Ces trois actions pilotent la vie de la boucle une fois qu'elle existe.

**PLAY/STOP** alterne entre PLAYING et STOPPED. La détection du tap est instantanée, mais l'**action musicale** (démarrage ou arrêt) suit **strictement** le mode de quantization per-bank (`loopQuantize`) — voir §17. Tap PLAY/STOP déclenche l'action quantisée si `loopQuantize` est Beat ou Bar, ou immédiate si No quantize. **Pas de bypass quantize** : pour un stop instantané en live, configurer la bank en No quantize. En cas de stop immédiat (No quantize ou à boundary Beat/Bar), un **flush de toutes les notes en cours** est émis (refcount → 0, CC123 All Notes Off en sécurité) pour éviter les notes bloquées.

Sur un OVERDUBBING, PLAY/STOP est **no-op** (OD-3 décision OD-Sync ; cf §8). L'abandon mid-OD se fait via tap CLEAR (cf §9.1).

#### §9.1 — CLEAR contextuel (3 sens selon état, OD-Sync 2026-05-19)

Le geste CLEAR a maintenant trois sémantiques distinctes selon l'état du LoopEngine :

| État du LoopEngine | Geste CLEAR | Action |
|---|---|---|
| **OVERDUBBING** | tap CLEAR (any duration, rising edge) | **Cancel mid-OD** : `cancelOverdub` ; swap musical par note `_events ↔ _eventsAlternate` (préserve couche base + live press) ; état → PLAYING ; EVT_LOOP_CLEAR trigger (Option β LED). |
| **PLAYING / STOPPED** | tap CLEAR **court** (release < `clearLoopTimerMs`, default 500 ms) | **Toggle Undo/Redo** : `swapForUndoRedo` ; swap la dernière couche OD en mémoire ; LED Option β trigger EVT_STOP (couche retirée) ou EVT_PLAY (couche réintégrée) selon diff `_eventCount`. No-op safe si `_alternateValid == false` (aucun OD n'a eu lieu depuis wipe/boot). |
| **PLAYING / STOPPED** | long-press CLEAR (≥ `clearLoopTimerMs`) | **Wipe destructif** : `longPressClear` ; buffer effacé + `_eventsAlternate` reset + `_alternateValid = false` ; état → EMPTY ; EVT_LOOP_CLEAR trigger. |
| RECORDING / WAITING_* | tap CLEAR | **Refusé** (`isLocked()` true ou contexte transitoire). |

Détails complets : cf [`Illpad_OD_Sync.md`](Illpad_OD_Sync.md) §3.4 + §4.

Le wipe long-press reste destructif **et garde-fou** : une boucle perdue est perdue (sauf si stockée dans un slot), et les pads capacitifs sont trop faciles à activer par erreur pour qu'un tap court puisse effacer. La rampe visuelle laisse le temps au musicien de comprendre ce qui va se passer et d'interrompre.

**Autre geste équivalent à PLAY/STOP** : `LEFT + double-tap sur le bank pad` de la bank LOOP (détail en §19). Ce geste cible aussi bien la bank FG que les banks BG, ce qui en fait l'accès le plus rapide pour contrôler plusieurs loops en parallèle sans changer de bank.

### §10 — Effets

Cinq effets modulent la boucle à la lecture, tous contrôlables en temps réel via pots :

| Effet | Comportement |
|---|---|
| **Shuffle depth** | Intensité globale du décalage de timing |
| **Shuffle template** | Choix parmi 10 grooves (5 positifs + 5 bipolaires, partagés avec les arps) |
| **Chaos / jitter** | Décalage aléatoire déterministe par event (toujours le même pour un event donné tant que le seed ne change pas), amplitude ±½ step |
| **Velocity pattern** | Choix parmi 4 LUTs (accent 1-3, strong downbeat, backbeat, swing) |
| **Vel pattern depth** | Mix entre la vélocité originale et celle imposée par le pattern |

Tous ces paramètres sont **per-bank** : chaque bank LOOP a ses propres valeurs, persistées en NVS, rechargées au boot, restaurées au load d'un slot (qui les embarque dans son fichier).

Le musicien ajuste les effets en live via les pots. Le **catch system** s'applique normalement : si un pot est tourné sans correspondre à la valeur stockée pour la bank courante, il n'a pas d'effet tant qu'il n'a pas physiquement atteint la valeur mémorisée. Ce comportement évite les sauts brutaux lors des changements de bank ou des loads de slot.

Le **base velocity** et la **velocity variation** ne sont pas des effets LOOP à proprement parler : ce sont les paramètres globaux déjà utilisés par NORMAL et ARPEG. Ils s'appliquent aussi aux loops (vélocité de base des notes à l'enregistrement, randomisation à la lecture).

**Chaos — re-seed sur retour à zéro** : le chaos est déterministe par event tant que le seed ne change pas. Même chaos=0.5 appliqué deux fois de suite produit les mêmes décalages. Mais si le pot chaos **descend à 0 puis remonte**, un nouveau seed est tiré au moment du premier frame où chaos > 0. Le musicien dispose ainsi d'un geste naturel pour "re-rouler les dés" sans modifier la valeur finale du chaos. Entre deux re-seeds, le chaos reste stable et prévisible. **Pas de feedback visuel** du re-seed : c'est un comportement silencieux, le musicien découvre l'effet à l'oreille.

### §11 — Sauvegarder un slot

Le musicien est sur une bank LOOP **en état STOPPED** avec un contenu enregistré. **Les saves ne sont pas autorisés pendant PLAYING** (ni pendant RECORDING/OVERDUBBING, qui était déjà la règle). Le musicien doit stopper la boucle avant de sauver.

Cette contrainte simplifie considérablement le write bloquant : le write LittleFS prend 80 à 160 ms pour une boucle pleine. En exigeant STOPPED, on accepte que ce write puisse interrompre la musique sur la bank courante — mais comme elle est déjà silencieuse (STOPPED), il n'y a rien à interrompre. Les autres banks (ARPEG, NORMAL, autre LOOP en BG) continuent à jouer normalement parce que leur tick tourne avant le handler slot dans la pipeline et que le write bloque uniquement brièvement Core 1.

Le geste lui-même : le musicien maintient **LEFT**, puis appuie **longuement** sur le pad physique assigné à un slot. Pendant le press, le LED de la bank courante affiche une rampe de progression (pattern `RAMP_HOLD` couleur magenta `CSLOT_VERB_SAVE` — voir LED spec §12) dont la durée `slotSaveTimerMs` (default 1000 ms, paramétrable en Tool 6, voir §20) matche le hold attendu. Quand la rampe atteint 100 % :
- Si le slot ciblé est **vide** : la boucle est sérialisée et écrite en flash. La boucle embarque les events, la structure (nombre de mesures, BPM de référence, eventCount), et tous les paramètres d'effets. Un SPARK blanc confirme. Le press qui suit est ignoré (le release ne déclenche aucun load).
- Si le slot ciblé est **occupé** : refus. `BLINK_FAST` rouge. Le musicien doit supprimer d'abord (§13). Cette règle est explicite : **pas d'écrasement implicite**. Le musicien voit le refus et prend une action délibérée.

Si le loop est vide (eventCount = 0), le save est refusé aussi. `BLINK_FAST` rouge. Pas d'intérêt à sauver une boucle vide.

**Si l'engine est en PLAYING au moment où le musicien commence le long press : refus immédiat au press** (BLINK_FAST rouge, pas de ramp). Le musicien comprend tout de suite que l'action n'est pas autorisée. Load et delete, eux, restent autorisés pendant PLAYING (voir §12 et §13) — seul le **save** exige STOPPED. Cette asymétrie vient de la durée : save est une opération plus lourde (80-160 ms) et mérite un state figé ; load est rapide (20-40 ms) et vise la compatibilité live.

Le release avant `slotSaveTimerMs` interrompt la sauvegarde : rien n'est écrit. C'est l'inverse de PLAY/STOP (action au press vs action au release) — la logique est "le long press EST le geste d'engagement ; arriver à la complétion signifie 'oui'".

### §12 — Rappeler un slot

Même posture : bank LOOP en FG, LEFT maintenu, pas en RECORDING/OVERDUBBING. Le musicien **tape court** le pad slot voulu. Si la durée entre press et release est :
- **Inférieure à 300 ms** : **annulation silencieuse**. Rien ne se passe. Ce garde-fou filtre les touches accidentelles.
- **Entre 300 ms et 1 s** : **load**. Si le slot est occupé, la boucle sauvée est chargée dans l'engine.
- **Au-delà de 1 s** sans release : c'était un save (§11), pas un load.

Le chargement en RAM est **toujours rapide** (read LittleFS ~20-40 ms + copie en RAM). C'est la transition musicale qui suit les règles de quantize.

**Objectif design central** : le load doit être **compatible avec le live**. Pas d'interruption audible sur les autres banks (ARPEG, NORMAL, autre LOOP BG) pendant le read flash. Idéalement, même la bank FG PLAYING doit transitionner sans glitch — passer d'un loop à un autre en plein jeu doit être une feature musicalement exploitable. Cela implique que le playback de la nouvelle boucle démarre en alignement avec le groove courant.

Comportement par état courant :

- **Engine en STOPPED ou EMPTY** : la boucle est chargée, l'engine passe en STOPPED (même un EMPTY devient STOPPED après load). Le musicien appuie PLAY/STOP quand il veut démarrer. Simple, aucun enjeu de timing.

- **Engine en PLAYING** : la transition suit la même règle de quantize que PLAY/STOP (§17). La valeur `loopQuantize` de la bank détermine :
  - **No quantize** : hard-cut instantané — noteOff flush de toutes les notes en cours, la nouvelle boucle démarre au `positionUs` correspondant au dernier beat passé (pour rester dans le pattern rythmique global)
  - **Beat** : la nouvelle boucle est chargée en RAM, le playback actuel continue jusqu'au prochain beat, puis bascule
  - **Bar** : idem, bascule au prochain bar

**Pas de double-tap slot pour forcer un load immédiat** : le load suit strictement `loopQuantize`. Pour un hard-cut, la bank doit être configurée en "No quantize". Cette simplification évite d'empiler une logique de double-tap par-dessus le timing tap court vs long press (qui sert déjà à distinguer load 300-1000 ms vs save >1 s).

Le **tempo runtime est inchangé** par le load. La boucle chargée rejoue à scaling proportionnel vs son BPM de référence d'origine : si elle a été enregistrée à 120 BPM et que le tempo courant est 140, elle tournera à 140 sans rescaling du contenu. Le groove reste fidèle, seule la vitesse s'adapte.

Le **catch system est ré-armé** : les paramètres d'effets chargés depuis le slot prennent le dessus sur les valeurs courantes, et les pots doivent être ramenés aux nouvelles valeurs pour reprendre la main. Le musicien voit le bargraph "uncaught" la prochaine fois qu'il touche un pot.

Si le slot ciblé est **vide** : refus. Flash rouge. Aucun changement.

### §13 — Supprimer un slot

Geste combiné, à 2 doigts sous LEFT :
1. Le musicien maintient **LEFT**
2. Il appuie sur le pad **CLEAR** (le pad de contrôle LOOP, pas un slot pad)
3. Pendant que CLEAR est tenu, il tape un slot pad

Si le slot est occupé : suppression **immédiate** sur rising edge du slot pad. Le fichier est retiré du drive, le slot redevient libre. L'animation visuelle qui confirme l'action est une `RAMP_HOLD` couleur orange `CSLOT_VERB_SLOT_CLEAR` suivie d'un SPARK blanc ; sa durée est `slotClearTimerMs` (default 800 ms proposé, paramétrable en Tool 6, voir §20). Cette durée n'est pas un hold user — le delete fire dès le rising edge — c'est purement le temps de la confirmation visuelle.

Si le slot est vide : refus. `BLINK_FAST` rouge simple.

La coordination avec le comportement normal du pad CLEAR (long press → CLEAR la boucle en cours) est gérée en interne : dès qu'une combo delete est détectée (slot pressé pendant que CLEAR est encore tenu), l'action CLEAR loop est inhibée jusqu'au relâché du CLEAR pad. La rampe cyan CLEAR loop est annulée avant complétion.

L'ordre est important : **CLEAR puis slot**, pas l'inverse. Taper un slot puis appuyer CLEAR ne déclenche pas de delete — le slot est déjà en tracking (save ou load selon la durée), et CLEAR arrive trop tard. Cette asymétrie est intentionnelle : le musicien apprend le geste "d'abord j'arme la destruction avec CLEAR, ensuite je désigne la cible".

### §14 — Jouer plusieurs loops en parallèle

Jusqu'à **`MAX_LOOP_BANKS` banks LOOP** peuvent exister simultanément (limite SRAM — valeur à figer Phase 2, cf §3). Chacune a son propre contenu, son propre état (PLAYING / STOPPED / etc.), ses propres effets, son propre canal MIDI.

Quand une bank LOOP est en foreground, le musicien voit ses LEDs (fond jaune + flash colorés selon le state, voir LED spec §17) et peut la piloter avec les 3 pads de contrôle. Les autres banks LOOP (en background) continuent à jouer normalement — leur tick et leur playback tournent dans la pipeline chaque frame, avec un fond dimmé (bgFactor) et des flashes de plus faible intensité.

Pour contrôler une bank LOOP background sans la passer en FG, le musicien utilise **LEFT + double-tap sur son bank pad** (§19). Ce geste déclenche PLAY/STOP sur la bank ciblée, qu'elle soit FG ou BG. Un loop BG peut être stoppé/relancé à la volée sans jamais quitter la bank courante.

Les saves et loads de slot, eux, **nécessitent la bank LOOP cible en foreground**. Pas de save/load cross-bank — le contexte visuel (LED de la bank courante pour les rampes et blinks) ne permet pas d'exécuter la combo sur une bank lointaine sans ambiguïté.

Combinaisons typiques :
- 2 banks LOOP (drums + perc) + 4 banks ARPEG (basses/pads) + 2 banks NORMAL (leads)
- 1 bank LOOP (rythmique complète) + 1 bank ARPEG (arp harmonique) + 6 banks NORMAL (instruments joués)
- 1 bank LOOP en fond permanent + 7 banks NORMAL comme palette multi-instruments

---

## Partie 4 — Interactions système

### §15 — Cohabitation avec NORMAL / ARPEG / ControlPads

Le mode LOOP coexiste avec les trois autres systèmes sans collision fonctionnelle, à condition de respecter les règles de rôle définies en §5.

**Avec NORMAL** : un bank NORMAL en FG ignore complètement les roles LOOP. Les pads REC/PLAY/STOP/CLEAR et les slot pads jouent comme des pads musicaux normaux. Le ScaleResolver résout les notes via la gamme de la bank NORMAL. Aucune interaction inter-bank au niveau des roles.

**Avec ARPEG** : un bank ARPEG en FG voit ses 20 rôles contextuels hold-left (scale + octave + hold) + le geste LEFT+double-tap bank pad pour le play/stop. Les rôles LOOP spécifiques (REC, PLAY/STOP, CLEAR, slots) sont masqués — ces pads jouent musicalement sur une bank ARPEG. Aucun conflit entre le play/stop ARPEG (rattaché au bank pad) et le PLAY/STOP LOOP (pad dédié sur layer musical).

**Avec ControlPads (Tool 4)** : les ControlPads sont des pads sparse CC/latch/momentary qui émettent des messages MIDI cross-bank sur press direct — donc sur le **layer musical**. Les règles de coexistence sont détaillées en §5 ; rappel ici des conséquences pratiques :

- **Interdit** — ControlPad ⊥ LOOP REC / PLAY-STOP / CLEAR : tous sur layer musical, compétition directe pour la même position pad. Pas d'assignation possible sur un pad qui porte déjà un de ces rôles (ou inversement).
- **Autorisé** — ControlPad ⊥ slot pad : les slots sont sur layer hold-left, orthogonaux. Le pad 30 peut être "ControlPad CC 74" en press direct ET "Slot 5" sous hold-left en contexte LOOP. Les deux comportements sont accessibles sans ambiguïté.
- **Autorisé** — ControlPad ⊥ rôles ARPEG hold-left (root, mode, chrom, hold, octave) : les rôles ARPEG sont tous sous LEFT hold, donc orthogonaux aux ControlPads sur layer musical.

Le music block skippe déjà les control pads via `isControlPad(i)` avant résolution — ce filtre s'étend naturellement aux 3 controls LOOP (REC, PLAY/STOP, CLEAR) avec le même mécanisme : sur une bank LOOP FG, ces 3 pads ne doivent jamais générer de note.

### §16 — Clock / tempo / sync DAW

Le mode LOOP suit la même hiérarchie d'horloge que le reste de l'instrument : USB clock > BLE clock > dernière source connue (5 s de mémoire) > interne (pot tempo). Les messages MIDI Start/Stop/Continue sont **ignorés en entrée et non émis en sortie** — l'ILLPAD est un instrument, pas un transport follower.

Le LOOP a une particularité par rapport à l'ARPEG : il stocke des events avec un **timestamp en microsecondes** (pas en ticks PPQN). Le BPM à l'instant de l'enregistrement est latché et utilisé comme **référence d'échelle** (`_recordBpm`). À la lecture, la position courante dans la boucle est **scalée proportionnellement** entre le tempo live et le tempo de référence. Conséquence :
- Si le tempo live monte, la boucle accélère. Les espacements relatifs sont préservés.
- Si le tempo live baisse, la boucle ralentit. Idem.
- Les events ne sont **jamais réécrits** par un changement de tempo. Seule la vitesse de balayage change.

Cette approche évite les problèmes de PPQN (24 ticks par noire) qui limiteraient la résolution ou causeraient des overflows. Le prix est un multiply + divide flottant par tick — négligeable à 240 MHz.

**Master mode** : quand l'ILLPAD est la source de tempo (rien ne pousse d'horloge externe), il génère ses propres ticks à partir du pot tempo et les émet sur USB + BLE. Les banks LOOP tournent sur cette horloge interne. Le musicien peut tourner le pot tempo en live, tout suit.

**Slave mode** : l'ILLPAD reçoit l'horloge d'un DAW ou d'un séquenceur externe. Le pot tempo est neutralisé musicalement mais affiche quand même un bargraph quand on le touche (un futur polish pourra le masquer, voir §20).

### §17 — Quantization (Play, Stop, Load, Record close)

> **MAJ 2026-05-19** : le scope de `loopQuantize` a été **étendu au close-record** suite au pivot Master Sync (cf [`Illpad_Master_Sync.md`](Illpad_Master_Sync.md)). Le paramètre per-bank gouverne maintenant deux comportements parallèles : transport (PLAY/STOP/LOAD existant) ET fermeture de recording (Auto-Stop nouveau).

Le mode LOOP applique un quantize **uniforme** à tous les démarrages et arrêts musicaux : Play, Stop, Load pendant PLAYING, ET fermeture de RECORDING. La valeur `loopQuantize` est paramétrée par bank en Tool 5 et prend une des trois valeurs :

| Valeur | Transport (PLAY/STOP/LOAD) | Record close (Auto-Stop) |
|---|---|---|
| **FREE / No quantize** | Aucun quantize. Action fire à la microseconde du tap. | Tap REC final = close immédiat, loop length = rawDur exact. Hors grille master. |
| **Beat** | Action attend le prochain 1/4 de note (24 ticks). | Tap REC final = entrée PENDING_CLOSE, capture continue jusqu'au prochain master beat boundary, puis close + startPlayback ancré sur ce tick. Loop length = multiple entier de 24 ticks au record BPM. Sur grille master. |
| **Bar** | Action attend la prochaine mesure (96 ticks). | Idem Beat mais unité = bar (96 ticks). Loop length = multiple entier de 96 ticks. Sur grille master. |

**Défaut : Bar** à la création d'une bank LOOP.

**Principe directeur** ([`Illpad_Master_Sync.md`](Illpad_Master_Sync.md) §1) : le master clock fournit LA grille temporelle commune à tout ce qui est quantizé sur l'ILLPAD. ARP Beat + LOOP BEAT/BAR + LOOP cross-bank partagent la grille. FREE = explicitement hors grille (par design, dérive intentionnelle).

**Modèle PLAY/STOP** :

- **Tap PLAY/STOP** → l'action **suit strictement** `loopQuantize`. Si Beat ou Bar, l'engine entre dans un état transitoire (WAITING_PLAY ou WAITING_STOP). Quand le boundary arrive, l'action s'exécute. Si `loopQuantize` = No quantize, le tap est déjà immédiat (pas d'état transitoire).
- **Pas de bypass** : la valeur `loopQuantize` est respectée dans tous les cas. Pour un déclenchement instantané en live, configurer la bank en No quantize.

**Load slot** : suit strictement `loopQuantize` (cf §12). Pour un hard-cut, configurer la bank en No quantize.

**Machine d'état étendue** :

```
STOPPED ──tap──→ WAITING_PLAY (si Beat/Bar) ──boundary──→ PLAYING
STOPPED ──tap si No quantize──→ PLAYING (immédiat)

PLAYING ──tap──→ WAITING_STOP (si Beat/Bar) ──boundary──→ STOPPED
PLAYING ──tap si No quantize──→ STOPPED (immédiat, flush notes)

PLAYING ──tap slot (court)──→ WAITING_LOAD (si Beat/Bar) ──boundary──→ PLAYING (new)
PLAYING ──tap slot (court) si No quantize──→ PLAYING (new, hard-cut)
```

**Règle générale des gestes concurrents pendant WAITING_*** :

Pendant WAITING_PLAY, WAITING_STOP ou WAITING_LOAD, **seules les actions PLAY/STOP/REC et le bank switch** peuvent modifier l'état. Toutes les autres actions (CLEAR, save, load, delete) sont **interdites** pendant l'attente.

Principe : le musicien peut **changer d'avis** sur son intention de lecture (annuler un stop, annuler un load, passer à overdub) ou **quitter la bank**, mais il ne peut rien faire d'autre jusqu'à ce que l'action en attente soit résolue.

Table détaillée :

| Geste pendant WAITING_* | WAITING_PLAY | WAITING_STOP | WAITING_LOAD |
|---|---|---|---|
| Tap PLAY/STOP | Annule, retour STOPPED | Annule, retour PLAYING | Annule load, puis tap applique play/stop sur PLAYING → WAITING_STOP |
| Tap REC | Ignoré (REC n'a aucun sens sur STOPPED) | **Annule STOP + entre OVERDUBBING** | Annule load + entre OVERDUBBING |
| Tap slot (court ou long) | **Interdit** | **Interdit** | **Interdit** (pas de changement de slot en plein load pending) |
| Long-press CLEAR | **Interdit** | **Interdit** | **Interdit** |
| Bank switch | Commit immédiat PLAYING + bank switch | Commit immédiat STOP + bank switch | Commit immédiat load + bank switch |

**Feedback visuel des états transitoires** : pattern `CROSSFADE_COLOR` entre la couleur du mode (`CSLOT_MODE_LOOP`, jaune) et la couleur verb play (`CSLOT_VERB_PLAY`, vert), period ~800 ms (voir LED spec §10 / §17).

### §18 — Hiérarchie et résolution de rôles pad

Ordre de résolution d'un pad pressé, du plus prioritaire au moins prioritaire :

1. **Bank pads sous LEFT** : bank switch (tap simple) ou toggle play/stop (double-tap sur ARPEG ou LOOP). Jamais masqués.
2. **ControlPads** (filter `isControlPad(i)`) : émettent leur CC selon leur mode. Le music block skippe ces pads. Les rôles LOOP du layer musical ne peuvent pas collisionner (voir §15).
3. **Rôles contextuels layer hold-left (LEFT pressé)** :
   - Si bank FG = ARPEG : rôles ARPEG (scale, octave, hold)
   - Si bank FG = LOOP : rôles slot (16 slot pads)
4. **Rôles contextuels layer musical (LEFT relâché)** :
   - Si bank FG = LOOP : rôles REC, PLAY/STOP, CLEAR
   - Si bank FG = ARPEG ou NORMAL : rien (le pad joue comme pad musical)
5. **Pad musical** : note via ScaleResolver (NORMAL/ARPEG) ou percussion fixe (LOOP, offset C2).

La validation Tool 3 doit assurer qu'aucune collision réelle n'est possible, en particulier au croisement des catégories (un bank pad ne peut pas être un slot, un ControlPad ne peut pas être un rôle LOOP du layer musical, etc.).

### §19 — LEFT + double-tap bank pad pour LOOP

**Décision** : le geste LEFT + double-tap sur un bank pad est **étendu aux banks LOOP** par symétrie avec ARPEG.

Sur une bank ARPEG, le geste déclenche `ArpEngine::setCaptured()` — toggle play/stop du pile arp. Sur une bank LOOP, il déclenche de manière analogue le toggle PLAY/STOP du LoopEngine. La cible peut être la bank FG ou une bank BG (keys pointer = nullptr pour BG).

> **État Phase 2 LOOP (commits `6c0b4d8` → `284bec4`)** : la branche
> `else if (... type == BANK_LOOP)` dans `BankManager::update` est désormais
> **câblée** ([BankManager.cpp:110-133](../../../src/managers/BankManager.cpp))
> et appelle `loopEngine->tapPlayStop(transport, keys)` avec dispatch LED
> EVT_WAITING / EVT_PLAY / EVT_STOP aligné sur les transitions effectives
> (audit fix R2). Tap simple sur LOOP bank pad reste un bank switch
> standard ; double-tap = play/stop toggle FG ou BG. Cf
> [docs/reference/loop-buffer-invariants.md §3](../../reference/loop-buffer-invariants.md)
> pour les invariants du buffer.

Conséquences :
- Sur la bank LOOP FG, LEFT + double-tap est **équivalent** au tap PLAY/STOP (mais plus rapide à atteindre si on est déjà en hold-left pour autre chose)
- Sur une bank LOOP BG, LEFT + double-tap est le **seul moyen** de piloter cette bank sans la passer en FG
- Le feedback LED (events PLAY / STOP depuis la grammaire unifiée LED spec §12) s'applique sur le LED de la bank cible, pas sur le LED FG

Spécificité LOOP vs ARPEG :
- ARPEG : double-tap sur bank pile vide → rien (aucun pile à capturer)
- LOOP : double-tap sur bank EMPTY → rien (aucune boucle à démarrer). Sur RECORDING/OVERDUBBING → ignoré (lock). Sur STOPPED → PLAY (avec quantize). Sur PLAYING → STOP (avec quantize selon §17).

Le geste ne change jamais la bank courante (comportement identique à ARPEG). Le bank pad tap simple (1er tap) reste le geste de switch bank — mais il est **différé** de `doubleTapMs` (~150 ms) pour permettre la détection du 2e tap.

### §20 — NVS et persistence

**État post Phase 1 close (commit `2624b12`)** : les extensions NVS requises pour LOOP sont **toutes déclarées** ; il manque uniquement le filesystem slots (Phase 6).

| Store | Rôle | Statut | Persisté |
|---|---|---|---|
| **LoopPadStore** | 3 control pads (REC, PLAY/STOP, CLEAR) + 16 slot pads | ✅ **DECLARED Phase 1** (commit `1b0ac8c`) — struct ([KeyboardData.h:528](../../../src/core/KeyboardData.h)) + validator + descriptor index 12 dans `NVS_DESCRIPTORS[]`. Pas de consommateur runtime ; Tool 3 b1 wires Phase 3 | Namespace `illpad_lpad` / `pads`, **23 B strict packed** (décision Q1) |
| **LoopPotStore** | 5 effets per-bank (shuffle depth/template, chaos, vel pattern/depth) | ✅ **DECLARED Phase 1** (commit `68855e3`) — struct ([KeyboardData.h:149](../../../src/core/KeyboardData.h)) 12 B packed + validator. Per-bank multi-key (pattern `ArpPotStore`), pas de descriptor entry. Câblage effets Phase 5 | Per-bank dans `illpad_lpot` / `loop_0..7` |
| **Slot files** | 1 fichier LittleFS par slot occupé | ❌ TODO Phase 6 | `/loops/slotNN.lpb`, partition LittleFS dédiée |

Extensions des Store existants :
- **BankTypeStore v4** : le champ `scaleGroup[8]` existe. Pour les banks LOOP, il est **ignoré** — les loops n'ont pas de gamme. Le Tool 5 ne doit pas exposer le scaleGroup pour les banks LOOP. Le NvsManager ne propage pas de scale change vers une bank LOOP, même si le scaleGroup est accidentellement non-zéro. V3 + V4 ont ajouté `bonusPilex10[]`, `marginWalk[]`, `proximityFactorx10[]`, `ecart[]` pour ARPEG_GEN — non utilisés par LOOP. `BankType` enum actuel = `{BANK_NORMAL=0, BANK_ARPEG=1, BANK_LOOP=2, BANK_ARPEG_GEN=3, BANK_ANY=0xFF}` — BANK_LOOP ajouté Phase 1 (commit `a84c955`). Cascade Phase 1 : validator clamp `> BANK_ARPEG_GEN` ✅, Tool 5 cycle proposera LOOP après refacto Tool 5 (pré-Phase 2, cf §6 footnote + spec dédiée), LedController dispatch ✅ (`renderBankLoop` stub), `main.cpp::handlePadInput` default no-op (commentaire "Phase 2 wires processLoopMode"), `reloadPerBankParams` filtre via `isArpType` (LOOP non concerné). **Storage `quantize[NUM_BANKS]` réutilisé pour LOOP** via discrimination contextuelle au validator (ARPEG/ARPEG_GEN = 0..1 Imm/Beat ; LOOP = 0..2 Free/Beat/Bar). Pas de bump v4 → v5 nécessaire. Détails dans la spec Tool 5 refacto §16.
- **PotMappingStore** : passe de 2 contextes (NORMAL / ARPEG) à 3 contextes (+ LOOP). 8 slots de mapping supplémentaires, total 24. ❌ TODO Phase 3-4 (Tool 7 + PotRouter 3 contexts). Rewrite complet vu la Zero Migration Policy — les anciennes données NVS rejetées au boot, defaults appliqués. **Note `POTMAP_VERSION = 2`** : tempo retiré du pool Tool 7 (binding fixe LEFT + rear pot). L'extension LOOP doit respecter cette politique (tempo absent du pool LOOP par défaut).
- **SettingsStore v11** ✅ **DONE** (commit `dec9391` Phase 0 step 0.3 + `cad7530` Phase 0.1) : les **3 timers LOOP globaux** sont déjà persistés et éditables en Tool 6 + Tool 8 (shared-field) :
  - `clearLoopTimerMs` — default 500 ms, range [200, 1500] ✅
  - `slotSaveTimerMs` — default 1000 ms, range [500, 2000] ✅
  - `slotClearTimerMs` — default 800 ms, range [400, 1500] ✅
- **LedSettingsStore v9** ✅ **DONE** (chaîne `cad7530` v7 → `3b85011` v9) : `tickBarDurationMs` + `tickWrapDurationMs` persistés et cachés dans `LedController` (`_tickBarDurationMs`, `_tickWrapDurationMs`), non consommés runtime (attendent `consumeBarFlash` / `consumeWrapFlash` du LoopEngine Phase 2+). V9 a fusionné les 4 fields FG (`fgArpPlayMax`/`fgArpStopMin`/`fgArpStopMax`/`normalFgIntensity`) en un seul `_fgIntensity` — cf §28 Q3/Q4.
- **ColorSlotStore v5** ✅ **DONE** (commit `cad7530`) : 16 slots, incluant `CSLOT_MODE_LOOP` (Gold preset), `CSLOT_VERB_PLAY/REC/OVERDUB/CLEAR_LOOP/SLOT_CLEAR/SAVE` (tous verbs transport LOOP), `CSLOT_VERB_STOP`, `CSLOT_CONFIRM_OK`. Tous éditables via Tool 8 Phase 0.1.

**Partition flash** : ❌ TODO Phase 6. L'ajout de LittleFS implique un repartitionnement du flash (512 KB dédiés à LittleFS). Au premier boot après flash de ce firmware, **tous les paramètres utilisateurs seront reset aux defaults** (calibration, pad order, pad roles, etc.). Comportement assumé par la Zero Migration Policy du projet — un Serial.printf au boot signale chaque reset, le musicien reconfigure via setup mode.

**Zero Migration Policy** : comme ailleurs dans le projet, les changements de struct se font en bumpant la version ou en changeant le size. Les anciennes données NVS sont rejetées silencieusement, les defaults compile-time s'appliquent. Aucune migration à écrire.

### §21 — LED system (renvoi)

Le feedback LED du mode LOOP est défini par la **LED feedback unified design** : [`docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md`](2026-04-19-led-feedback-unified-design.md) — **implémentée Phase 0** (commits `5c3e57c` → `8511b0d`). Ce document est la source de vérité pour tout ce qui concerne le rendu visuel — grammaire, palette de 9 patterns, 16 color slots, table de mapping events.

**État post Phase 0.1** : la grammaire LED 3 couches est en place sur `main`. Le Tool 8 UX a été **refondu en single-view 6 sections** (respec `docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md`, commit `cc379f5`) — remplace la mention "Tool 8 refondu en 3 pages" de la LED spec historique.

Éléments structurants à connaître pour comprendre la spec LOOP sans relire toute la LED spec :

- **Slogan directeur** : "le nom au fond, le verbe en surface". La couleur du mode (`CSLOT_MODE_LOOP`, **jaune Gold**) est le fond persistant ; les actions transport (REC, OVERDUB, PLAY, STOP, CLEAR, SAVE, LOAD, DELETE, REFUSE) sont des overlays colorés transitoires.
- **Palette de patterns** (LED spec §10) : `SOLID`, `PULSE_SLOW`, `CROSSFADE_COLOR`, `BLINK_SLOW`, `BLINK_FAST`, `FADE`, `FLASH`, `RAMP_HOLD`, `SPARK`. Chaque rendu LOOP pointe vers un de ces 9 patterns. ✅ Implémentés Phase 0.
- **Couplage timer métier** (LED spec §13) : le pattern `RAMP_HOLD` consomme sa durée depuis un timer stocké en `SettingsStore` v11 (§20) ✅, jamais un literal. CLEAR long-press = `clearLoopTimerMs`, slot save = `slotSaveTimerMs`, slot delete animation = `slotClearTimerMs`.
- **Phase 0 LED** ✅ **DONE** (commits Phase 0 + Phase 0.1). Les Phases 1-6 LOOP peuvent démarrer — les events LOOP arriveront dans un système unifié prêt.
- **Couleurs principales LOOP** — toutes **présentes dans ColorSlotStore v5** (commit `cad7530`) : `CSLOT_MODE_LOOP` (jaune Gold preset 7), `CSLOT_VERB_PLAY` (Green preset 11), `CSLOT_VERB_REC` (Coral preset 8), `CSLOT_VERB_OVERDUB` (Amber preset 6), `CSLOT_VERB_CLEAR_LOOP` (Cyan preset 5), `CSLOT_VERB_SLOT_CLEAR` (Amber preset 6 + hue+20), `CSLOT_VERB_SAVE` (Magenta preset 10), `CSLOT_CONFIRM_OK` (Pure White preset 0, universel SPARK).
- **Refus** : réutilise `CSLOT_VERB_REC` (Coral) via pattern `BLINK_FAST` cycles=3. Pas de color slot dédié. ✅ Pattern implémenté, callsites LOOP à câbler Phase 2+.
- **WAITING_*** : pattern `CROSSFADE_COLOR` unifié mode-invariant — **colorA = `CSLOT_VERB_PLAY` (vert, éditable Tool 8), colorB = `CSLOT_CONFIRM_OK` (blanc, hardcodé dans `triggerEvent`)**, period hardcoded 800 ms (Tool 8 respec §4.3 "Éléments non exposés"), brightness = `_fgIntensity` (LedSettingsStore v9 single FG slider) × `bgFactor` en BG. ✅ Implémenté commit `48b96fb` Phase 1 Task 7 amendée v9. Décision Q3 — voir §28.
- **EVT_LOOP_* enum** ✅ **réservés** dans `LedGrammar.h` (EVT_LOOP_REC, OVERDUB, SLOT_LOADED, SLOT_WRITTEN, SLOT_CLEARED, SLOT_REFUSED, CLEAR) — mapping par défaut `PTN_NONE` actuellement. À wirer Phase 4 (PotRouter + LED wiring) avec le bon `{patternId, colorSlot, fgPct}` selon LED spec §12.
- **Tick durations BAR/WRAP** ✅ **persistées** dans LedSettingsStore v9 + **cachées** dans LedController (`_tickBarDurationMs`, `_tickWrapDurationMs`). Non consommées runtime — attendent `consumeBarFlash()` / `consumeWrapFlash()` flags dans LoopEngine Phase 2+.

Tous les tunings (intensités, durées, couleurs) sont configurables en **Tool 8 LED Settings single-view** (sections NORMAL / ARPEG / LOOP / TRANSPORT / CONFIRMATIONS / GLOBAL). La section LOOP + les lignes TRANSPORT tick (BEAT/BAR/WRAP durations + verb colors PLAY/REC/OVERDUB) sont déjà exposées musicien-facing. Pas de hardcode.

Le reste des détails (intensités exactes, tables de rendu par mode×state×FG/BG, refactor Tool 8 respec, machine d'état `LedController`, Zero Migration NVS) est dans la LED spec + la Tool 8 respec.

### §22 — Pot routing et catch

Le PotRouter accueille un **3e contexte LOOP** (s'ajoutant à NORMAL et ARPEG). Cela implique :
- Extension de `PotMappingStore` (voir §20)
- **Refactor du Tool 7 en 3 pages dédiées** (NORMAL / ARPEG / LOOP), chacune avec la même mécanique de navigation par pot (identique à l'existant). **Pas de touche `t` pour toggler entre contextes** — la navigation inter-pages se fait par la même logique multi-pages que les autres tools. Chaque page reste cohérente : les pots font défiler, les pages se remplacent proprement.
- Nouveau pool de paramètres LOOP : tempo, base vel, vel pattern, vel pattern depth, chaos, shuffle depth, shuffle template, velocity variation (+ MIDI CC et pitch bend comme les autres)

Mapping par défaut LOOP **proposé** (8 slots = 4 pots × 2 couches) — à finaliser Phase 4 :

| Pot | Seul | + hold-left |
|---|---|---|
| R1 | Chaos | Shuffle template (10 discrets) |
| R2 | Base velocity | Shuffle depth |
| R3 | Vel pattern (4 discrets) | Velocity variation |
| R4 | Vel pattern depth | (MIDI CC libre — assignable Tool 7) |

**Note** : tempo n'apparaît plus dans le pool Tool 7 (binding fixe LEFT + rear pot depuis `POTMAP_VERSION = 2`). Tempo reste néanmoins un paramètre live affectant les banks LOOP — son binding hardware est juste découplé de Tool 7.

**Catch** : comme ARPEG, les paramètres per-bank LOOP ont une politique catch per-bank (reset au bank switch). Les paramètres globaux (le binding fixe tempo notamment) gardent leur catch à travers les switches. Les paramètres qui sont partagés avec ARPEG (shuffle depth, shuffle template) utilisent le même slot interne de PotRouter — le main loop route la valeur vers le bon engine selon le type de la bank FG.

Au **load d'un slot**, tous les paramètres per-bank LOOP sont réécrits par le contenu du fichier. Le catch est ré-armé pour tous ces paramètres (seedCatchValues interne). Le musicien voit les pots comme "uncaught" la prochaine fois qu'il les touche.

---

## Partie 5 — Invariants et contraintes

### §23 — Invariants

Ces règles doivent être vraies tout au long de l'exécution. Toute modification du design qui les viole est à rediscuter.

1. **Aucune note bloquée**. Chaque noteOn finit par un noteOff. Refcount cohérent. Les bank switches, les loads de slot, les CLEAR, les flush overdub injectent des noteOff pour tout pad tenu ou toute note en refcount > 0.
2. **Bank switch refusé pendant RECORDING / OVERDUBBING**. Silent deny.
3. **Slot save / load / delete refusés pendant RECORDING / OVERDUBBING**. Le hold-left sous ces états skip entièrement le handler slot.
4. **Pas d'écrasement implicite d'un slot**. Save sur slot occupé = refus. L'utilisateur doit delete d'abord.
5. **Pas de mélange d'horloges par event**. Le recordBpm d'une boucle est immutable jusqu'au prochain close-record (commitRecordingClose ou closeRecordingImmediate). Les events ne sont jamais réécrits par un changement de tempo, ni par un changement de loop length (Master Sync pivot 2026-05-19 supprime le rescale silencieux).
6. **Pas de scale sur une bank LOOP**. Les pads scale/root/mode/chrom sont no-op en contexte LOOP. Le scaleGroup est ignoré.
7. **Le catch system est ré-armé à chaque changement de contexte**. Bank switch, load slot, reconfig Tool 7 — tous triggent une re-seed.
8. **Setup/Runtime 4-link chain**. Tout paramètre persisté a son Store, son Tool UI, son chemin de load au boot, son chemin de save au runtime. Un Store sans Tool UI ou un Tool case sans consommateur runtime est un bug.
9. **No new/delete runtime**. Toutes les allocations sont statiques. Le buffer de sérialisation LittleFS (~8 KB) est un static reusable.
10. **No blocking on Core 0**. Le Slot Drive utilise la flash, donc bloque Core 1 pendant 20-160 ms selon l'opération. C'est accepté tant que les opérations se passent sous hold-left (musique pad gelée de toute façon). Aucune opération slot ne doit jamais se passer sur Core 0 ni pendant un jeu actif sans hold.
11. **Au plus une bank LOOP en RECORDING ou OVERDUBBING à un instant t**. Les autres banks LOOP sont forcément PLAYING, STOPPED, ou EMPTY. Conséquence combinée des invariants 2 (bank switch refusé pendant REC/OD) et §18 (pads REC/PS/CLEAR sur FG layer musical uniquement). Décision Q8 — voir §28.

### §24 — Non-goals

Volontairement hors du mode LOOP, y compris pour des versions futures :

- **Pas de pitch** — les pads déclenchent des notes fixes
- **Pas de gamme / mode / root** — pas applicable à la percussion
- **Pas d'aftertouch**
- **Pas de pitch bend**
- **Pas de count-in** — le recording démarre au premier coup
- **Pas d'armement** ("arm to record") — tap REC = entrée directe en RECORDING
- **Pas de mute/unmute** — la vélocité de base (pot) fait office de contrôle de volume
- **Pas de quantize per-event à l'enregistrement** — le groove humain (micro-timing relatif au master beat/bar pour BEAT/BAR, ou absolu pour FREE) est préservé. Le close-record est quantizé sur la grille master via Auto-Stop (cf §17 + [`Illpad_Master_Sync.md`](Illpad_Master_Sync.md)), mais les events stockés ne sont JAMAIS shiftés dans le temps.
- **Pas de rescale silencieux des timestamps stockés** — interdit de modifier le temps d'un event après capture (anti-pattern bar-snap rescale Phase 2 supprimé 2026-05-19, cf [`Illpad_Master_Sync.md`](Illpad_Master_Sync.md) §0 contexte).
- **Pas de scroll / shift / reverse** — la boucle est jouée telle qu'enregistrée
- ~~**Pas d'undo / redo**~~ **MAJ 2026-05-19 — qualifiée par OD-Sync** : un **toggle Undo/Redo 1-level** sur la dernière couche OD est introduit via tap CLEAR court en PLAYING/STOPPED (cf [`Illpad_OD_Sync.md`](Illpad_OD_Sync.md) §4). Permet le geste musical live "mute la couche HH pour un breakdown puis la ramener". **Multi-level undo (N couches en arrière) reste hors-goals**. Le CLEAR long-press reste la seule sortie **destructive** d'une boucle non désirée ; les slots sont la seule persistance.
- **Pas de gestion de fichiers du Slot Drive** — pas de rename, pas de copie, pas d'export. Tout passe par les gestes hardware.
- **Pas de preview d'un slot avant load** — le load EST la preview

### §25 — Budget ressources

| Ressource | Utilisation LOOP | Note |
|---|---|---|
| **SRAM** | ~74.8 KB (Phase 2 base 38.8 KB + OD-Sync alt buffers 32 KB + g_swapTemp 8 KB - overdub buffers supprimés 4 KB) | sur 320 KB disponibles. MAX_LOOP_BANKS=4 acté Phase 2 (commit `6c0b4d8`). Build actuel post-OD-Sync C3 : RAM 40.4 % total (~132 KB / 327 KB). |
| **Flash (LittleFS)** | 512 KB partition dédiée | 16 slots × 8 KB = 128 KB occupés max (Phase 6) |
| **Core 1 CPU** | ~1 float mult + div par tick par bank | négligeable |
| **LittleFS write** | 80-160 ms bloquant Core 1 | accepté sous hold-left (musique gelée), Phase 6 |
| **LittleFS read** | 20-40 ms | accepté idem, Phase 6 |
| **Buffer sérialisation** | 8224 octets static (SRAM ou PSRAM) | alloué une fois, Phase 6 |
| **BLE bandwidth** | impact similaire à ARPEG | aftertouch n'existe pas, bandwidth moindre que NORMAL |

Budget confortable partout. La contrainte tangible est la SRAM pour 4 banks (~38.8 KB), pas la flash.

---

## Partie 6 — Points ouverts

### §26 — Questions à trancher ensemble

Structure en trois groupes : **questions tranchées** (pour traçabilité), **questions encore ouvertes**, **questions déférées à d'autres sessions**.

#### Questions tranchées (pass 1 critique + pass 2)

| # | Sujet | Décision |
|---|---|---|
| — | LEFT + double-tap bank pad LOOP | Étendu à LOOP par symétrie ARPEG (§19) |
| — | Coexistence Tool 4 ControlPads / slot pads | Slots ⊥ ControlPads orthogonal (cross-layer), LOOP controls ⊥ ControlPads interdit (même layer musical) — voir §5, §15 |
| — | scaleGroup sur bank LOOP | Ignoré — les loops n'ont pas de scale (§20, invariant 6) |
| — | Couleur LOOP | Jaune (§21, détails LED déférés) |
| — | Default loopQuantize | **Bar** (§6) |
| — | Overdub abandonné | Engine reste en PLAYING, pas STOPPED (§8) |
| — | Bar-snap | Arrondi vers le haut avec deadzone **25 %** (§7) |
| — | Tool 5 refactor | Refacto **dédié pré-Phase 2** acté 2026-05-17 — spec [`2026-05-17-tool5-bank-config-refactor-design.md`](2026-05-17-tool5-bank-config-refactor-design.md). Tableau matriciel banks×params, nav 2D, validator quantize discriminé par type. |
| — | Tool 7 | **3 pages dédiées** (N/A/L), même nav pot que l'existant, pas de touche `t` (§22) |
| — | Recording quantisé future | Abandonné (pas de WAITING_REC future) |
| — | Save de slot | Autorisé uniquement en état STOPPED (§11) |
| — | Stop / Play quantisé | Suit **strictement** `loopQuantize` per-bank — **pas de bypass** (No quantize / Beat / Bar décide). Pour un toggle instantané, configurer la bank en No quantize (§17). |
| — | Load slot pendant PLAYING | Suit loopQuantize, **pas de double-tap bypass** (§12, §17) |
| — | Persistance params per-bank + slot | **Garder les deux** : NVS per-bank pour la mémoire "bank", slot embarque les params pour le rappel cohérent (§3) |
| — | Mode Immediate → renommé | **"No quantize"** pour clarifier l'intention (§17) |
| — | Chaos re-seed | Feature actée, **pas de feedback visuel** (§10) |
| — | Save refusé pendant PLAYING | **Refus immédiat au press** (`BLINK_FAST` rouge, pas de ramp) (§11) |
| — | NVS timers LOOP | Les 3 timers RAMP_HOLD (`clearLoopTimerMs`, `slotSaveTimerMs`, `slotClearTimerMs`) vivent en `SettingsStore`, éditables en Tool 6 (§20). Requis par LED spec §13 (couplage pattern-timer). |
| — | Phase 0 LED | Refactor LED isolé **avant** LOOP (Partie 7). Les phases 1-6 LOOP consomment ensuite la grammaire LED unifiée. |
| — | Gestes concurrents pendant WAITING_* | Seules actions PLAY/STOP/REC et bank switch autorisées. CLEAR/slot/delete interdits. (§17) |
| — | Load safety | **Option a** : valider header avant toute modification RAM (§12) |

#### Questions encore ouvertes

**État post Phase 0.1** : aucune question bloquante restante. Les Qs résiduelles LED mentionnées historiquement ont été closes :
- `bgFactor` → défaut acté 25 %, range [10, 50], éditable en Tool 8 section GLOBAL.
- `slotClearTimerMs` → défaut acté 800 ms, range [400, 1500], persisté SettingsStore v11 ✅.
- Color slot bank switch → `CSLOT_BANK_SWITCH` = Pure White preset 0, implémenté v4.
- Nav keys Tool 8 → redéfinies Phase 0.1 respec (single-view, paradigme géométrique §4.4 conventions, `d` sans confirm).

**Items encore à trancher** : **aucun**. Les 8 questions résiduelles (audit 2026-04-20) ont été tranchées — voir **§28 Décisions pré-plan Phase 1**. SRAM 2 banks reste à vérifier par mesure lors de Phase 2 (non-bloquant, suivi).

#### Items LED déférés — **résolus par la LED spec 2026-04-19**

Les 5 items qui étaient déférés au brainstorm LED sont adressés par [`2026-04-19-led-feedback-unified-design.md`](2026-04-19-led-feedback-unified-design.md) :

| Item déféré | Résolution |
|---|---|
| LED feedback WAITING_PLAY / STOP / LOAD | Pattern `CROSSFADE_COLOR` entre couleur mode et `CSLOT_VERB_PLAY` (LED spec §10, §17) |
| LED feedback pendant write LittleFS (80-160 ms) | Le SPARK white de complétion `RAMP_HOLD` fire à 100 % ; le write bloque Core 1 mais le SPARK reste affichable après write puisque PLAYING est stoppé pendant le save (voir §11). Pas de traitement spécial nécessaire. |
| LED feedback combo delete | Pattern `RAMP_HOLD` orange `CSLOT_VERB_SLOT_CLEAR` de durée `slotClearTimerMs`, suivi du SPARK white universel (LED spec §12) |
| Color slot dédié LOOP | Nouveau `CSLOT_MODE_LOOP` = Gold preset (16 slots au total, LED spec §11) |
| Convention générale couleur/pattern | Grammaire "nom au fond, verbe en surface" + palette 9 patterns fixes (LED spec §8, §10, §11) |

---

## Partie 7 — Suite

Ce document sert de **référence de haut niveau** pour les plans d'implémentation LOOP Phase 1→6.

**État Phase 0 + Phase 0.1** ✅ **DONE** (prérequis LED) :
- **Phase 0 LED** (LED spec §23) — Refactor LED isolé : grammaire unifiée, palette 9 patterns, 15 color slots (v4). Tool 8 en 3 pages PATTERNS/COLORS/EVENTS. `SettingsStore` v11 avec 3 timers LOOP. Tool 6 expose les 3 timers. Commits : `5c3e57c` → `8511b0d`. Audit post `docs/superpowers/reports/rapport_phase_0_led.md`.
- **Phase 0.1 Tool 8 respec** — Tool 8 refondu en single-view 6 sections (NORMAL/ARPEG/LOOP/TRANSPORT/CONFIRMATIONS/GLOBAL). `ColorSlotStore` v5 (16 slots, +`CSLOT_VERB_STOP`). `LedSettingsStore` v7 (+`tickBeatDurationMs`/`tickBarDurationMs`/`tickWrapDurationMs`). `LedController::renderPreviewPattern` public wrapper + helper `ToolLedPreview`. Gamma hot-reload. Commits : `cad7530` → `6ac9ff3`. Plan (archivé) : `docs/archive/2026-04-20-tool8-ux-respec-plan.md`. Spec : `docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md`.

**Phases LOOP à venir** :

1. **Phase 1 — CLOSE** (commits `a84c955` → `2624b12`) — Skeleton + guards + LED preparation :
   - ✅ Extension `BankType` enum (+`BANK_LOOP = 2`, commit `a84c955`) ; cascade : validator `BankTypeStore` clamp `> BANK_ARPEG_GEN`, ToolBankConfig `drawDescription` accepte LOOP, LedController dispatch + `renderBankLoop` stub, `main.cpp::handlePadInput` default no-op (commentaire "Phase 2 wires processLoopMode").
   - ✅ Struct `LoopPadStore` **23 B strict packed** (décision Q1, §28) + validator + descriptor index 12 `NVS_DESCRIPTORS[]` (commit `1b0ac8c`). Pas de consommateur runtime — layout figé.
   - ✅ Struct `LoopPotStore` per-bank + validator (commit `68855e3`). Câblage effets Phase 5.
   - ✅ Guards défensifs Task 4 (commit `2624b12`) : `ScaleManager::processScalePads` early-return si `slot.type == BANK_LOOP` (invariant 6 §23) ; `BankManager::update` consume silencieusement le 2ème tap LOOP (anticipe §19 toggle). **Note** : pas de guard explicite dans `BankManager::switchToBank` ni `MidiTransport` — la garde est **par construction** (le dispatch `handlePadInput` ne route jamais `BANK_LOOP` vers `processNormalMode`/`processArpMode`, donc AT et pitch bend ne sont jamais émis sur LOOP ; `sendPitchBend(8192)` reset universel safe ; `allNotesOff` safe sur LOOP sans note).
   - ~~**Step LED prep** : rename `LedSettingsStore::fgArpPlayMax` → `fgPlayMax`...~~ **CADUQUE post-v9** : LedSettingsStore v9 (commit `3b85011`) a fusionné les 4 fields FG en `_fgIntensity` unique, Tool 8 expose 1 slider en section GLOBAL. Décision Q4 §28 sans objet.
   - ✅ **Step LED WAITING BG-aware** : implémenté commit `48b96fb` post-v9 — hardcode `colorB = _colors[CSLOT_CONFIRM_OK]` pour `PTN_CROSSFADE_COLOR`, `fgPct = _fgIntensity` (au lieu de `_fgArpStopMax` proposé Q3 originale), scaling `× bgFactor` pour LEDs non-FG dans `renderPattern` CROSSFADE_COLOR. Décision Q3 §28 respectée modulo terminologie v9.
   - Aucune nouvelle feature visible pour le musicien. **Phase 1 close 2026-05-17**.
2. **Phase 2 — CLOSE** (commits `6c0b4d8` → `284bec4`, HW gates G1-G9 validés 2026-05-19) — LoopEngine core + main wiring + LED + multi-bank protections :
   - ✅ Classe `LoopEngine` (state machine 7 états EMPTY / RECORDING / PLAYING / OVERDUBBING / STOPPED + WAITING_PLAY / WAITING_STOP transitoires), buffer 1024 events 8 B (`LoopEvent` static_assert m1), recording timestamps µs, `_recordBpm` latché au 1er pad press (invariant §23.5), bar-snap 25 % deadzone à `stopRecording`, M6 timestamp clamp validation.
   - ✅ Playback **BPM-scaled intégration incrémentale** (audit fix B1 — `_scaledElapsedUs += delta × liveBpm / recordBpm` cumulatif, pas re-calc depuis `_playStartUs`, élimine sauts sur tempo live).
   - ✅ Refcount noteOn/noteOff **dupliqué** entre ArpEngine et LoopEngine (décision Q2 §28), MAX_LOOP_PENDING_NOTEOFFS=64. `flushPendingNoteOffs` self-stop (audit fix B2 — state → STOPPED).
   - ✅ `processLoopMode` dans `main.cpp` : dispatch BANK_LOOP, REC/PLAY/CLEAR control pads, musical pads vers `capturePadEvent` (M8 live drumming spec §18 — MIDI émis dans **tous** états).
   - ✅ `renderBankLoop` complet : state-driven FG color (EMPTY/STOPPED/WAITING_* → Gold, RECORDING → Coral, OVERDUBBING → Amber, PLAYING → Green), FLASH overlay bar/wrap via `consumeBarFlash`/`consumeWrapFlash` (m4 one-shot semantics), BG = FG × bgFactor (v9 unified).
   - ✅ EVT_LOOP_* triggers depuis processLoopMode + BankManager + WaitingExit signal (root-cause fix : PTN_CROSSFADE_COLOR continuous, `commitWaitingAction` set `_waitingExit` flag, main consume → `triggerEvent(EVT_PLAY/STOP)` clear overlay).
   - ✅ Overdub merge **atomique** (audit fix M4 — pre-check capacité, abandon silent si dépassement, jamais de paires noteOn/Off cassées). Merge O(n+m) de 2 arrays pré-triés (M2 live-sort dans `capturePadEvent`).
   - ✅ Q5 STOPPED-loaded REC = PLAYING + OVERDUBBING simultanés (décision Q5 §28) — câblé dans `tapRec` state machine.
   - ✅ Quantize WAITING_PLAY/STOP : `computeNextBoundaryTick(24 BEAT / 96 BAR)` via `ClockManager::getCurrentTick()`, `commitWaitingAction(transport, nowUs)` propage `nowUs` (audit fix B3 — évite underflow uint32 sur enchaînement). FREE quantize = action immédiate.
   - ✅ NvsManager charge `LoopPadStore` (descriptor index 12) + `LoopPotStore` per-bank (multi-key `loop_0..loop_7`) au boot. Helper `applyDevSeedLoopPadsIfSafe` (audit fix M7) seed pads 32/33/34 si NVS vide ET pas de collision Tool 4 — à retirer Phase 3 quand Tool 3 b1 livre l'UI propre.
   - ✅ BankManager double-tap LOOP → `loopEngine->tapPlayStop()` (FG ou BG, audit fix R2 LED event aligné processLoopMode). `toggleAllArps` → `toggleAllArpsAndLoops` étend LEFT+hold_pad aux LOOP banks (loop-buffer-invariants §6).
   - ✅ `midiPanic` flush all LoopEngines (Phase 1b avant MidiEngine.allNotesOff).
   - ✅ Bank switch guard pendant LOOP REC/OD (audit fix M9, **2 paths patchés** : pending-timeout + LEFT-release fast-forward — silent deny per spec §23.2 + invariant 11).
   - ✅ `LoopEngine::onBackgroundTransition` (audit fix B-N1/R-N1) — flush live press refcount via `_padHeldLive[]` + reset CLEAR press tracker au bank switch out. Tracker `_padHeldLive[NUM_KEYS]` unifié set par `capturePadEvent` dans tous états, consommé par `stopRecording` + `mergeOverdub` (B-N2) + `onBackgroundTransition` (B-N1).
   - **Décision actée Phase 2** : `PendingEvent` struct dupliqué (Q2 §28), `MAX_LOOP_BANKS = 4` (D1 acté, 38.8 KB SRAM), live-sort buffer M2 (Q4), velocity strict capture M3 (Q8), MIDI live monitor tous états M8 (Q1).
   - ★ **Premier son MIDI LOOP audible HW** commit `d345f01` (gate G5 milestone Phase 2). HW gate G9 procédure 11 steps incluant audit-fix B-N1/B-N2/R-N1.
3. **Phase 3** — Setup tools :
   - Refactor **Tool 3 vers b1 contextuel** (3 sous-pages Banks / ARPEG / LOOP). Miroir de la règle collision §5 rule 2 : refus d'assigner LOOP REC/PS/CLEAR sur un pad déjà ControlPad.
   - ~~Extension **Tool 5**~~ → **absorbé dans refacto Tool 5 dédié pré-Phase 2** (Option A actée 2026-05-17, cf §6 footnote + spec [`2026-05-17-tool5-bank-config-refactor-design.md`](2026-05-17-tool5-bank-config-refactor-design.md)). Phase 3 LOOP ne touche plus au Tool 5.
   - Refactor **Tool 7** en 3 pages (NORMAL / ARPEG / LOOP, sans touche `t` — spec §22).
   - Extension **Tool 4** pour refuser ControlPad sur pad LOOP control (règle collision §5 rule 2, **bundle Phase 3** avec Tool 3 b1 — décision Q7, §28). Validation bi-directionnelle via helper `LoopPadStore::isLoopControlPad(padIndex)`.
4. **Phase 4** — PotRouter + LED wiring : `PotMappingStore` passe à 3 contextes (+8 slots LOOP), rendu `renderBankLoop` complet, mapping `EVT_LOOP_*` → patterns concrets dans `EVENT_RENDER_DEFAULT` (LED spec §12), câblage `consumeBarFlash`/`consumeWrapFlash` flags depuis LoopEngine vers LedController.
5. **Phase 5** — Effets : shuffle (shared templates avec ARPEG), chaos avec re-seed sur retour à zéro, velocity patterns (4 LUTs), bloc pots LOOP complet.
6. **Phase 6** — Slot Drive : partition LittleFS 512 KB, `LoopSlotStore` (format fichier binaire), serialize/deserialize, `handleLoopSlots` (load/save/delete gestes), Tool 3 slot role section (16 slot pads hold-left context LOOP).

Chaque phase aura son **plan détaillé** séparé (via skill `superpowers:writing-plans`), listant les fichiers touchés, les signatures de fonction, les tests manuels HW, et les cross-references vers les invariants de ce document et de la LED spec.

**Note Phase 0 fallback obsolète** : l'option de repli "implémenter LOOP avec patterns ad-hoc puis grand refactor LED après" (LED spec §25 option c) **n'est plus pertinente** — Phase 0 + 0.1 sont exécutées, la grammaire LED est en place. Le plan nominal (Phase 1-6 sur base LED unifiée) est le seul chemin prévu.

---

## Partie 8 — Décisions pré-plan Phase 1

### §28 — Tranchage des 8 questions résiduelles (2026-04-20)

Suite à l'audit de cohérence `docs/archive/rapport_audit_loop_spec.md` (archivé), 8 questions résiduelles ont été identifiées et tranchées en session brainstorming. Section de traçabilité :

| # | Question | Décision actée | Référence |
|---|---|---|---|
| Q1 | `LoopPadStore` size | **23 B strict packed** (3 controls + 16 slots). nvs-reference.md corrigé (8 B → 23 B). | §20 |
| Q2 | `PendingEvent` factorisé ou dupliqué entre ArpEngine et LoopEngine | **Dupliqué**. LoopEngine définit sa propre struct + buffer + logique refcount. Raison : besoins divergents (timestamps µs vs ticks PPQN), éviter refactor ArpEngine qui marche (principe DO NOT MODIFY étendu aux pièces musicales calibrées), budget SRAM non contraint (+64-128 B négligeable). | §27 Phase 2 |
| Q3 | `EVT_WAITING` mode-aware (ARPEG bleu vs LOOP jaune) | **1 event unique**. colorA = `CSLOT_VERB_PLAY` (vert, éditable Tool 8), colorB = `CSLOT_CONFIRM_OK` (blanc, hardcodé dans `triggerEvent`), brightness = `_fgIntensity` (LedSettingsStore v9 single FG slider, post fusion `fgArpPlayMax`/`fgArpStopMin`/`fgArpStopMax`/`normalFgIntensity`) × `_bgFactor` en BG. Mode-invariant assumé : WAITING est une transition courte (800 ms), la couleur de fond du mode réapparaît au résultat. **Implémenté commit `48b96fb`** Phase 1 Task 7 amendée v9. | §21, §27 Phase 1 step |
| Q4 | `LOOP FG brightness` : field séparé ou partagé avec ARPEG | **CADUQUE post-v9**. LedSettingsStore v9 (commit `3b85011`) a fusionné les 4 fields FG (`fgArpPlayMax`/`fgArpStopMin`/`fgArpStopMax`/`normalFgIntensity`) en un seul `_fgIntensity`. Tool 8 expose désormais 1 slider unique "FG brightness" en section GLOBAL (et non TRANSPORT comme proposait Q4 originale). Tasks 5-6 du plan Phase 1 LOOP devenues sans objet. | §27 Phase 1 step (caduque) |
| Q5 | Comportement STOPPED-loaded + tap REC | **PLAYING + OVERDUBBING simultanés** (option a). Reprise de la lecture position 0 + armement overdub en un geste. Cohérent avec §8 (PLAYING → REC = OVERDUBBING). Le musicien n'a pas à faire PLAY explicite avant d'overdub. | §8, §27 Phase 2 |
| Q6 | Tool 5 refactor "présentation en colonnes" | ~~Phase 3 minimal, refactor deferred~~ **INVERSÉE 2026-05-17** — audit Tool 5 a montré que la structure actuelle (cycle linéaire 5 états + multi-lignes ARPEG_GEN) ne supporte pas élégamment l'ajout LOOP. Refacto **dédié pré-Phase 2** validé via spec [`2026-05-17-tool5-bank-config-refactor-design.md`](2026-05-17-tool5-bank-config-refactor-design.md). Tableau matriciel banks×params, nav 2D, INFO auto-update, validator `quantize` discriminé par type. Pas de bump NVS. | §6, §27 |
| Q7 | Tool 4 extension (refus ControlPad sur pad LOOP control) | **Phase 3 bundle** avec Tool 3 b1. Validation bi-directionnelle (Tool 3 et Tool 4 se connaissent mutuellement via helper `LoopPadStore::isLoopControlPad`). Pas de pré-wiring Phase 1/2 (pas de chemin de création du conflit avant Phase 3). | §27 Phase 3 |
| Q8 | Max 1 bank LOOP en REC/OD à un instant t ? | **Oui, expliciter comme invariant 11 §23**. Conséquence combinée des invariants 2 (bank switch refusé pendant REC/OD) et §18 (pads REC/PS/CLEAR sur FG layer musical uniquement). Coût : 1 ligne spec, permet LoopEngine + `renderBankLoop` de faire des hypothèses explicites sans code défensif. Aligne spec LOOP avec LED spec §17 table ("BG RECORDING/OVERDUBBING : impossible"). | §23 |
| **Master Sync** | Pivot algorithmique close-record (post-audit musical 2026-05-19) | **Auto-Stop + master grid anchor**. Remplace bar-snap+rescale destructeur par Auto-Stop boundary-aware. ARP+LOOP+LOOP cross-bank désormais sur la grille master clock. FREE = explicitement hors grille. 9 sous-décisions BS-1 à BS-9 actées (cf [`Illpad_Master_Sync.md`](Illpad_Master_Sync.md) §6). Commits `89f6c11` (ClockManager getters) + `edbdd2b` (LoopEngine atomic) + `a7a461a` (doc-sync). HW gates G1-G9 validés. | §7, §17, §24, [`Illpad_Master_Sync.md`](Illpad_Master_Sync.md) |
| **OD-Sync** | Pivot algorithmique overdub (post-audit musical 2026-05-19, complément Master Sync) | **Immediate-merge + snapshot 1-level Undo/Redo toggle**. Remplace deferred-merge avec buffer temporaire par insertion directe dans `_events[]` pendant OD (live loop growth audible immédiatement). Snapshot `_eventsAlternate` pris à l'entrée OD pour permettre Cancel mid-OD (CLEAR pendant OD) et Undo/Redo post-exit (CLEAR court PLAYING/STOPPED). Diff swap musical par note préserve couche base + live press. LED Option β = réutilisation events existants (EVT_LOOP_CLEAR / EVT_STOP / EVT_PLAY). 16 sous-décisions OD-1 à OD-16 actées (cf [`Illpad_OD_Sync.md`](Illpad_OD_Sync.md) §9). Commits `eaf5674` (C1 buffers/helpers) + `fc2ff9b` (C2 immediate-merge + B-N2 déplacée) + `33149b8` (C3 diff swap + CLEAR dispatch + LED). HW gates G1-G15 validés. | §8, §9, §24, [`Illpad_OD_Sync.md`](Illpad_OD_Sync.md) |

### §29 — Drifts spec↔code résolus via ces décisions

L'audit a flagué 4 drifts spec↔code (cf rapport §3) :

- **F2.2** (`EVT_WAITING` colorA hardcodé ARPEG) → résolu par Q3 (pas de scission, hardcode colorB blanc via `triggerEvent`, brightness BG-aware). Step Phase 1 spécifié §27.
- **F2.3** (`LINE_LOOP_FG_PCT` partage silencieusement `fgArpPlayMax`) → CADUQUE post-v9. LedSettingsStore v9 a supprimé tous ces fields séparés ; le slider unique `_fgIntensity` rend le problème sans objet. Q4 caduque (cf §28).
- **F2.4** (`LoopPadStore` size=8 B dans nvs-reference) → corrigé nvs-reference.md à 23 B (Q1).
- **F2.5** (`renderPreviewPattern` signature drift) : Tool 8 respec §6.6 à mettre à jour rétroactivement (signature sans `ledMask`). Non-bloquant — à traiter en tâche séparée ou ignorer.

---

**Spec VALIDÉE post Phase 0.1 + tranchage pré-plan** (2026-04-20). Prête à servir d'entrée pour la rédaction du plan d'implémentation Phase 1.
