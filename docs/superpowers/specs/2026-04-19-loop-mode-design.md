# ILLPAD48 V2 — Mode LOOP : design haut niveau

**Date** : 2026-04-19
**Statut** : brouillon, à corriger
**Scope** : LOOP core + Slot Drive + refactor Tool 3 b1. Haut niveau, sans code. Les plans d'implémentation par phase viendront dans des documents séparés.
**Sources** : `docs/superpowers/specs/2026-04-02-loop-mode-design.md` (extrait de la branche `loop` au commit `39ef3cc^`), `docs/superpowers/specs/2026-04-06-loop-slot-drive-design.md`, `docs/reference/architecture-briefing.md`, état du code `main` au 2026-04-19.
**Spec compagnon** : `docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md` (grammaire LED unifiée, source de vérité pour §21).

---

## Partie 1 — Cadre

### §1 — Intention musicale et place dans ILLPAD48

ILLPAD48 V2 propose huit slots de performance, chacun relié à un canal MIDI fixe. Deux types de banks existent aujourd'hui sur `main` : **NORMAL** (jeu mélodique classique sur gamme/mode) et **ARPEG** (arpégiateur séquentiel quantisé à l'horloge). Le mode **LOOP** ajoute un troisième type, dédié à la construction en temps réel de boucles rythmiques.

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

Le LOOP core est conçu pour **tourner en fond** : une boucle lancée sur une bank LOOP continue à jouer quand l'utilisateur change de bank. Comme les arpèges, il n'existe **jamais** de "mise en sommeil" d'un engine — toute bank LOOP existe en RAM et sa boucle joue ou ne joue pas selon son état. Maximum **2 banks LOOP simultanément** (contrainte SRAM).

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

1. **Tool 5 — Bank Config** : le musicien cycle le type de la bank voulue. L'ordre est NORMAL → ARPEG → LOOP → NORMAL. Maximum 2 banks en LOOP (le tool refuse au-delà). Pour chaque bank LOOP, un **mode de quantization** (No quantize / Beat / Bar, **défaut Bar**) est paramétrable pour piloter le comportement play/stop et load — détail en §17.

   > **Refactor Tool 5 à prévoir** : avec l'ajout du quantize LOOP, l'extension Bar pour ARPEG, et les modes de stop quantize à venir, le Tool 5 actuel (présentation inline par bank) va devenir difficile à lire. Refactor prévu vers une présentation en **colonnes** (un tableau : une ligne par bank, une colonne par paramètre). Hors scope de ce document mais à prévoir dans le plan d'implémentation.

2. **Tool 3 — Pad Roles, sous-page LOOP** : le musicien assigne les 3 pads de contrôle (REC, PLAY/STOP, CLEAR) et optionnellement les 16 slot pads. Les slots inutilisés restent `0xFF` (non assignés). La sous-page LOOP ne présente les slot roles que si le refactor b1 est complet — sinon, seulement les 3 controls. Rien n'oblige à assigner les 16 slots d'un coup : un musicien peut très bien commencer avec 2 ou 4 slots et étendre plus tard.

3. **Tool 7 — Pot Mapping, contexte LOOP** : le musicien configure quels pots pilotent quels paramètres LOOP. Les 8 slots de mapping (4 pots × 2 couches hold/not-hold) reçoivent par défaut : tempo, base velocity, vel pattern, vel pattern depth, chaos, shuffle depth, shuffle template, velocity variation. Tous réassignables — y compris en MIDI CC ou pitch bend pour un canal MIDI arbitraire.

Le **Tool 8 — LED Settings** n'a pas besoin de configuration spécifique pour LOOP : les couleurs jaunes utilisées par LOOP viennent de slots de couleur configurables si le musicien veut les nuancer (intensité, hue offset), mais les valeurs par défaut sont déjà chargées.

Au retour en mode jeu, la bank LOOP est prête. Son LED affiche le fond jaune solide dim de l'état EMPTY (voir LED spec §17).

### §7 — Enregistrer un premier loop

Le musicien est sur la bank LOOP. Le LED jaune est solide dim (EMPTY). Il tape sur le pad **REC** — le LED garde le fond jaune solide mais ajoute un flash rouge à chaque bar (pattern RECORDING, voir LED spec §17), l'engine entre en état **RECORDING**. Mais rien n'est encore capturé : le moteur attend le premier coup.

Le musicien frappe son premier pad. L'horloge d'enregistrement démarre à cet instant précis (pas au tap REC, pour ne pas enregistrer une anacrouse parasite). Ce premier coup devient l'**offset 0** du loop. Les coups suivants sont enregistrés avec leur décalage temporel en microsecondes par rapport à ce point zéro.

Le musicien joue son motif. Tous les frappes et relâchés sont capturés, avec leur timing exact — **pas de quantization à l'enregistrement**, le groove humain est préservé. Le tempo en cours est utilisé comme référence (BPM "recording timebase") mais n'est pas verrouillé : il peut changer pendant que la boucle joue, voir §16.

Quand le motif est complet, le musicien tape à nouveau **REC**. Trois choses se passent :
1. **Bar-snap avec deadzone** : la durée enregistrée est arrondie à la mesure entière **suivante**, mais avec une deadzone de tolérance pour absorber le retard naturel de réaction (tap typiquement 20-80 ms après la bar line visée). Règle : si `elapsed` est à moins de `0.25 × barDuration` après une bar line, snap à cette bar (arrondi bas) ; sinon, round up. Threshold **25 %** acté (= si elapsed = 3.2 bars, snap à 3 ; si 3.3 bars, snap à 4). Min 1 bar, max 64. Les offsets des events sont rescalés proportionnellement pour remplir exactement la nouvelle durée.
2. **Flush des pads tenus** : si un doigt est encore appuyé sur un pad au moment du tap REC, un noteOff implicite est injecté à la position courante pour éviter les notes bloquées.
3. **Transition vers PLAYING** : la boucle redémarre immédiatement (quantize No quantize forcé à la clôture), le LED garde le fond jaune solide et ajoute un flash vert à chaque wrap de la boucle (pattern PLAYING, voir LED spec §17).

> **Pourquoi pas un ceil strict** : sans deadzone, un tap REC 30 ms après la bar line (réaction humaine normale) produit 4 bars alors que le musicien voulait 3. La deadzone absorbe cet overshoot naturel.

Le loop tourne. Le musicien peut maintenant soit le laisser tourner, soit jouer par-dessus (§13), soit ajouter une couche (§8), soit le couper (§9), soit le sauver dans un slot (§11).

### §8 — Overdub

Pendant qu'un loop joue, taper **REC** une seconde fois fait entrer l'engine en **OVERDUBBING**. Le LED garde le fond jaune solide et passe du flash vert (PLAYING) au flash orange (OVERDUBBING) à chaque wrap (voir LED spec §17). Le loop continue de jouer. Tout ce que le musicien frappe est capturé dans un buffer temporaire d'overdub, avec la même résolution microseconde.

Deux sorties possibles :
- **Tap REC** → l'overdub est **mergé** dans la boucle principale. Les events sont fusionnés par ordre temporel, les pads tenus sont flushés (comme à la clôture d'un RECORDING initial). La boucle repart en PLAYING avec son nouveau contenu.
- **Tap PLAY/STOP** → l'overdub est **abandonné**. Le buffer temporaire est jeté, la boucle d'origine reste intacte, **l'engine reste en PLAYING**. Pour stopper ensuite la boucle, un second tap PLAY/STOP est nécessaire (transition PLAYING → STOPPED normale, quantisée ou non selon §17).

Deux contraintes à connaître :
- **Bank switch refusé** pendant RECORDING et OVERDUBBING. Le musicien doit clore avant de changer de bank. Cette contrainte protège de l'ambiguïté "est-ce que l'enregistrement continue en fond quand je passe ailleurs ?". Réponse : non, parce qu'on ne peut pas partir.
- **Buffer d'overdub plein** (~128 events max) ou **buffer principal plein** après merge (1024 events max) → les events en surplus sont droppés silencieusement. Pas de signal d'erreur visible. Le musicien entend que ses dernières frappes ne sont pas capturées, mais la boucle reste cohérente.

### §9 — Play / Stop / Clear

Ces trois actions pilotent la vie de la boucle une fois qu'elle existe.

**PLAY/STOP** alterne entre PLAYING et STOPPED. La détection du tap est instantanée, mais l'**action musicale** (démarrage ou arrêt) suit le mode de quantization per-bank (`loopQuantize`) — voir §17. Un tap simple déclenche l'action quantisée si `loopQuantize` est Beat ou Bar, ou immédiate si No quantize. Un **double-tap** force toujours l'action immédiate, quelle que soit la valeur de `loopQuantize` (bypass du quantize). En cas de stop immédiat, un **flush de toutes les notes en cours** est émis (refcount → 0, CC123 All Notes Off en sécurité) pour éviter les notes bloquées.

Sur un OVERDUBBING, PLAY/STOP abandonne l'overdub et laisse l'engine en PLAYING (voir §8).

**CLEAR** est destructif : il vide le buffer d'events et repasse l'engine en EMPTY. L'action n'est pas immédiate — il faut **maintenir le pad CLEAR** pendant la durée `clearLoopTimerMs` (default 500 ms, paramétrable en Tool 6, voir §20). Pendant ce hold, le LED de la bank courante affiche une rampe de progression (pattern `RAMP_HOLD` de la grammaire LED, couleur cyan `CSLOT_VERB_CLEAR_LOOP` — voir LED spec §12). Un relâché anticipé annule. À la complétion, la boucle est effacée et un SPARK blanc confirme.

Ce garde-fou long-press est délibéré. Une boucle perdue est perdue (sauf si elle était stockée dans un slot), et les pads capacitifs sont trop faciles à activer par erreur pour qu'un tap court puisse effacer. La rampe visuelle laisse le temps au musicien de comprendre ce qui va se passer et d'interrompre.

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

Jusqu'à **2 banks LOOP** peuvent exister simultanément (limite SRAM). Chacune a son propre contenu, son propre état (PLAYING / STOPPED / etc.), ses propres effets, son propre canal MIDI.

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

### §17 — Quantization (Play, Stop, Load)

Le mode LOOP applique un quantize **uniforme** à tous les démarrages et arrêts musicaux : Play, Stop, et Load pendant PLAYING. La valeur `loopQuantize` est paramétrée par bank en Tool 5 et prend une des trois valeurs :

| Valeur | Signification |
|---|---|
| **No quantize** | Aucun quantize. L'action fire à la microseconde du tap. |
| **Beat** | L'action attend le prochain 1/4 de note (24 ticks à 24 PPQN). |
| **Bar** | L'action attend la prochaine mesure (96 ticks). |

**Défaut : Bar** à la création d'une bank LOOP.

**Modèle tap / double-tap pour PLAY/STOP** :

- **Tap simple** sur PLAY/STOP → l'action **suit** `loopQuantize`. Si Beat ou Bar, l'engine entre dans un état transitoire (WAITING_PLAY ou WAITING_STOP). Quand le boundary arrive, l'action s'exécute. Si `loopQuantize` = No quantize, le tap simple est déjà immédiat (pas d'état transitoire).
- **Double-tap** dans une fenêtre `doubleTapMs` (~150 ms, réutilise le paramètre existant) → l'action **bypasse** le quantize et fire immédiatement.

**Load slot** : suit strictement `loopQuantize`, **pas de double-tap bypass** (voir §12). Pour un hard-cut, le musicien configure la bank en No quantize.

**Machine d'état étendue** :

```
STOPPED ──tap──→ WAITING_PLAY (si Beat/Bar) ──boundary──→ PLAYING
STOPPED ──double-tap / tap si No quantize──→ PLAYING (immédiat)

PLAYING ──tap──→ WAITING_STOP (si Beat/Bar) ──boundary──→ STOPPED
PLAYING ──double-tap / tap si No quantize──→ STOPPED (immédiat, flush notes)

PLAYING ──tap slot (court)──→ WAITING_LOAD (si Beat/Bar) ──boundary──→ PLAYING (new)
PLAYING ──tap slot (court) si No quantize──→ PLAYING (new, hard-cut)
```

**Règle générale des gestes concurrents pendant WAITING_*** :

Pendant WAITING_PLAY, WAITING_STOP ou WAITING_LOAD, **seules les actions PLAY/STOP/REC et le bank switch** peuvent modifier l'état. Toutes les autres actions (CLEAR, save, load, delete) sont **interdites** pendant l'attente.

Principe : le musicien peut **changer d'avis** sur son intention de lecture (annuler un stop, annuler un load, passer à overdub) ou **quitter la bank**, mais il ne peut rien faire d'autre jusqu'à ce que l'action en attente soit résolue.

Table détaillée :

| Geste pendant WAITING_* | WAITING_PLAY | WAITING_STOP | WAITING_LOAD |
|---|---|---|---|
| Tap PLAY/STOP **dans** doubleTapMs (double-tap bypass) | → PLAYING immédiat | → STOPPED immédiat | Annule load + STOPPED immédiat (1er tap annule load, 2e tap = bypass) |
| Tap PLAY/STOP **hors** doubleTapMs | Annule, retour STOPPED | Annule, retour PLAYING | Annule load, puis tap applique play/stop sur PLAYING → WAITING_STOP |
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

Conséquences :
- Sur la bank LOOP FG, LEFT + double-tap est **équivalent** au tap PLAY/STOP (mais plus rapide à atteindre si on est déjà en hold-left pour autre chose)
- Sur une bank LOOP BG, LEFT + double-tap est le **seul moyen** de piloter cette bank sans la passer en FG
- Le feedback LED (confirm HOLD_ON / HOLD_OFF) s'applique sur le LED de la bank cible, pas sur le LED FG

Spécificité LOOP vs ARPEG :
- ARPEG : double-tap sur bank pile vide → rien (aucun pile à capturer)
- LOOP : double-tap sur bank EMPTY → rien (aucune boucle à démarrer). Sur RECORDING/OVERDUBBING → ignoré (lock). Sur STOPPED → PLAY (avec quantize). Sur PLAYING → STOP (avec quantize selon §17).

Le geste ne change jamais la bank courante (comportement identique à ARPEG). Le bank pad tap simple (1er tap) reste le geste de switch bank — mais il est **différé** de `doubleTapMs` (~150 ms) pour permettre la détection du 2e tap.

### §20 — NVS et persistence

Trois nouveaux Store sont nécessaires (les détails field-par-field viendront dans les plans d'implémentation) :

| Store | Rôle | Persisté |
|---|---|---|
| **LoopPadStore** | 3 control pads (REC, PLAY/STOP, CLEAR) + 16 slot pads | Namespace unique via `illpad_lpad` / `pads` |
| **LoopPotStore** | 5 effets per-bank (shuffle depth/template, chaos, vel pattern/depth) | Per-bank dans `illpad_lpot` / `loop_0..7` |
| **Slot files** | 1 fichier LittleFS par slot occupé | `/loops/slotNN.lpb`, partition LittleFS dédiée |

Extensions des Store existants :
- **BankTypeStore v2+** : le champ `scaleGroup[8]` existe déjà. Pour les banks LOOP, il est **ignoré** — les loops n'ont pas de gamme. Le Tool 5 ne doit pas exposer le scaleGroup pour les banks LOOP. Le NvsManager ne propage pas de scale change vers une bank LOOP, même si le scaleGroup est accidentellement non-zéro.
- **PotMappingStore** : passe de 2 contextes (NORMAL / ARPEG) à 3 contextes (+ LOOP). 8 slots de mapping supplémentaires, total 24. Rewrite complet vu la Zero Migration Policy — les anciennes données NVS sont rejetées au boot, defaults appliqués.
- **SettingsStore** : ajout de **3 timers LOOP globaux** éditables en Tool 6, requis par la grammaire LED (pattern `RAMP_HOLD` couplé à un timer métier, voir LED spec §13) :
  - `clearLoopTimerMs` — default 500 ms, range [200, 1500]. Durée du long press CLEAR pour vider une boucle (§9).
  - `slotSaveTimerMs` — default 1000 ms, range [500, 2000]. Durée du long press slot pour sauvegarder (§11).
  - `slotClearTimerMs` — default 800 ms (proposé, à valider), range [400, 1500]. Durée de l'**animation visuelle** confirmant la suppression d'un slot via combo delete (§13). Pas un hold user.
  Ces 3 valeurs sont **globales** (pas per-bank). `SettingsStore` contient déjà des timings globaux analogues (bargraph duration, doubleTap window, aftertouch rate). Version bump de `SettingsStore` requis (Zero Migration Policy : reset aux defaults au premier boot post-flash). Le 4-link chain s'applique : champ dans Store + validator + case switch Tool 6 + apply dans `setup()`.

**Partition flash** : l'ajout de LittleFS implique un repartitionnement du flash (512 KB dédiés à LittleFS). Au premier boot après flash de ce firmware, **tous les paramètres utilisateurs seront reset aux defaults** (calibration, pad order, pad roles, etc.). Comportement assumé par la Zero Migration Policy du projet — un Serial.printf au boot signale chaque reset, le musicien reconfigure via setup mode.

**Zero Migration Policy** : comme ailleurs dans le projet, les changements de struct se font en bumpant la version ou en changeant le size. Les anciennes données NVS sont rejetées silencieusement, les defaults compile-time s'appliquent. Aucune migration à écrire.

### §21 — LED system (renvoi)

Le feedback LED du mode LOOP est défini par la **LED feedback unified design** : [`docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md`](2026-04-19-led-feedback-unified-design.md). Ce document est la source de vérité pour tout ce qui concerne le rendu visuel — grammaire, palette de 9 patterns, 16 color slots, table de mapping events.

Éléments structurants à connaître pour comprendre la spec LOOP sans relire toute la LED spec :

- **Slogan directeur** : "le nom au fond, le verbe en surface". La couleur du mode (`CSLOT_MODE_LOOP`, **jaune**) est le fond persistant ; les actions transport (REC, OVERDUB, PLAY, STOP, CLEAR, SAVE, LOAD, DELETE, REFUSE) sont des overlays colorés transitoires.
- **Palette de patterns** (LED spec §10) : `SOLID`, `PULSE_SLOW`, `CROSSFADE_COLOR`, `BLINK_SLOW`, `BLINK_FAST`, `FADE`, `FLASH`, `RAMP_HOLD`, `SPARK`. Chaque rendu LOOP pointe vers un de ces 9 patterns.
- **Couplage timer métier** (LED spec §13) : le pattern `RAMP_HOLD` consomme sa durée depuis un timer stocké en `SettingsStore` (§20), jamais un literal. CLEAR long-press = `clearLoopTimerMs`, slot save = `slotSaveTimerMs`, slot delete animation = `slotClearTimerMs`.
- **Phase 0 LED** (LED spec §23) : le refactor LED est à faire **avant** le LOOP core, pour que les events LOOP arrivent dans un système unifié. Les Phases 1-6 LOOP arrivent après.
- **Couleurs principales LOOP** : `CSLOT_MODE_LOOP` (jaune, Gold preset), `CSLOT_VERB_PLAY` (vert), `CSLOT_VERB_REC` (rouge), `CSLOT_VERB_OVERDUB` (orange), `CSLOT_VERB_CLEAR_LOOP` (cyan), `CSLOT_VERB_SLOT_CLEAR` (orange distinct), `CSLOT_VERB_SAVE` (magenta), `CSLOT_CONFIRM_OK` (blanc, universel pour SPARK).
- **Refus** : réutilise `CSLOT_VERB_REC` (rouge) via pattern `BLINK_FAST` cycles=3. Pas de color slot dédié.
- **WAITING_*** : pattern `CROSSFADE_COLOR` entre `CSLOT_MODE_LOOP` (jaune) et `CSLOT_VERB_PLAY` (vert), period ~800 ms.

Tous les tunings (intensités, durées, couleurs) sont configurables en Tool 8 LED Settings (pas de hardcode).

Le reste des détails (intensités exactes, tables de rendu par mode×state×FG/BG, refactor Tool 8 en 3 pages, machine d'état `LedController` refactorée, Zero Migration NVS) est dans la LED spec.

### §22 — Pot routing et catch

Le PotRouter accueille un **3e contexte LOOP** (s'ajoutant à NORMAL et ARPEG). Cela implique :
- Extension de `PotMappingStore` (voir §20)
- **Refactor du Tool 7 en 3 pages dédiées** (NORMAL / ARPEG / LOOP), chacune avec la même mécanique de navigation par pot (identique à l'existant). **Pas de touche `t` pour toggler entre contextes** — la navigation inter-pages se fait par la même logique multi-pages que les autres tools. Chaque page reste cohérente : les pots font défiler, les pages se remplacent proprement.
- Nouveau pool de paramètres LOOP : tempo, base vel, vel pattern, vel pattern depth, chaos, shuffle depth, shuffle template, velocity variation (+ MIDI CC et pitch bend comme les autres)

Mapping par défaut LOOP (8 slots = 4 pots × 2 couches) :

| Pot | Seul | + hold-left |
|---|---|---|
| R1 | Tempo | Chaos |
| R2 | Base velocity | Shuffle depth |
| R3 | Vel pattern (4 discrets) | Shuffle template (10 discrets) |
| R4 | Vel pattern depth | Velocity variation |

**Catch** : comme ARPEG, les paramètres per-bank LOOP ont une politique catch per-bank (reset au bank switch). Les paramètres globaux (tempo) gardent leur catch à travers les switches. Les paramètres qui sont partagés avec ARPEG (shuffle depth, shuffle template) utilisent le même slot interne de PotRouter — le main loop route la valeur vers le bon engine selon le type de la bank FG.

Au **load d'un slot**, tous les paramètres per-bank LOOP sont réécrits par le contenu du fichier. Le catch est ré-armé pour tous ces paramètres (seedCatchValues interne). Le musicien voit les pots comme "uncaught" la prochaine fois qu'il les touche.

---

## Partie 5 — Invariants et contraintes

### §23 — Invariants

Ces règles doivent être vraies tout au long de l'exécution. Toute modification du design qui les viole est à rediscuter.

1. **Aucune note bloquée**. Chaque noteOn finit par un noteOff. Refcount cohérent. Les bank switches, les loads de slot, les CLEAR, les flush overdub injectent des noteOff pour tout pad tenu ou toute note en refcount > 0.
2. **Bank switch refusé pendant RECORDING / OVERDUBBING**. Silent deny.
3. **Slot save / load / delete refusés pendant RECORDING / OVERDUBBING**. Le hold-left sous ces états skip entièrement le handler slot.
4. **Pas d'écrasement implicite d'un slot**. Save sur slot occupé = refus. L'utilisateur doit delete d'abord.
5. **Pas de mélange d'horloges par event**. Le recordBpm d'une boucle est immutable jusqu'au prochain stopRecording. Les events ne sont jamais réécrits par un changement de tempo.
6. **Pas de scale sur une bank LOOP**. Les pads scale/root/mode/chrom sont no-op en contexte LOOP. Le scaleGroup est ignoré.
7. **Le catch system est ré-armé à chaque changement de contexte**. Bank switch, load slot, reconfig Tool 7 — tous triggent une re-seed.
8. **Setup/Runtime 4-link chain**. Tout paramètre persisté a son Store, son Tool UI, son chemin de load au boot, son chemin de save au runtime. Un Store sans Tool UI ou un Tool case sans consommateur runtime est un bug.
9. **No new/delete runtime**. Toutes les allocations sont statiques. Le buffer de sérialisation LittleFS (~8 KB) est un static reusable.
10. **No blocking on Core 0**. Le Slot Drive utilise la flash, donc bloque Core 1 pendant 20-160 ms selon l'opération. C'est accepté tant que les opérations se passent sous hold-left (musique pad gelée de toute façon). Aucune opération slot ne doit jamais se passer sur Core 0 ni pendant un jeu actif sans hold.

### §24 — Non-goals

Volontairement hors du mode LOOP, y compris pour des versions futures :

- **Pas de pitch** — les pads déclenchent des notes fixes
- **Pas de gamme / mode / root** — pas applicable à la percussion
- **Pas d'aftertouch**
- **Pas de pitch bend**
- **Pas de count-in** — le recording démarre au premier coup
- **Pas d'armement** ("arm to record") — tap REC = entrée directe en RECORDING
- **Pas de mute/unmute** — la vélocité de base (pot) fait office de contrôle de volume
- **Pas de quantize à l'enregistrement** — le groove humain est préservé, la quantization existe uniquement au playback
- **Pas de scroll / shift / reverse** — la boucle est jouée telle qu'enregistrée
- **Pas d'undo / redo** — le CLEAR long-press est la seule sortie d'une boucle non désirée, les slots sont la seule persistance
- **Pas de gestion de fichiers du Slot Drive** — pas de rename, pas de copie, pas d'export. Tout passe par les gestes hardware.
- **Pas de preview d'un slot avant load** — le load EST la preview

### §25 — Budget ressources

| Ressource | Utilisation LOOP | Note |
|---|---|---|
| **SRAM** | ~18.8 KB (2 banks × 9.4 KB) | sur 320 KB disponibles |
| **Flash (LittleFS)** | 512 KB partition dédiée | 16 slots × 8 KB = 128 KB occupés max |
| **Core 1 CPU** | ~1 float mult + div par tick par bank | négligeable |
| **LittleFS write** | 80-160 ms bloquant Core 1 | accepté sous hold-left (musique gelée) |
| **LittleFS read** | 20-40 ms | accepté idem |
| **Buffer sérialisation** | 8224 octets static (SRAM ou PSRAM) | alloué une fois |
| **BLE bandwidth** | impact similaire à ARPEG | aftertouch n'existe pas, bandwidth moindre que NORMAL |

Budget confortable partout. La contrainte tangible est la SRAM pour 2 banks (18.8 KB), pas la flash.

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
| — | Tool 5 refactor | Refactor en présentation **colonnes** acté, hors scope de ce doc |
| — | Tool 7 | **3 pages dédiées** (N/A/L), même nav pot que l'existant, pas de touche `t` (§22) |
| — | Recording quantisé future | Abandonné (pas de WAITING_REC future) |
| — | Save de slot | Autorisé uniquement en état STOPPED (§11) |
| — | Stop / Play quantisé | Double-tap = bypass immédiat, tap simple = suit loopQuantize (§17) |
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

Aucune à ce stade pour le périmètre LOOP. La LED spec ([`2026-04-19-led-feedback-unified-design.md`](2026-04-19-led-feedback-unified-design.md)) porte quelques Qs résiduelles (bgFactor, valeur exacte `slotClearTimerMs`, color slot bank switch, nav keys Tool 8) — mais aucune ne bloque le design LOOP.

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

Ce document, une fois corrigé et validé, sert de **référence de haut niveau** pour les plans d'implémentation par phase. L'ordre d'implémentation est désormais préfixé par une **Phase 0 LED** (voir [LED spec §23](2026-04-19-led-feedback-unified-design.md)) qui refactorise le système LED dans sa cible finale **avant** l'arrivée du mode LOOP. Les events LOOP arrivent ainsi dans un système unifié, zéro dette visuelle.

1. **Phase 0** (LED spec §23) — Refactor LED isolé : grammaire unifiée, palette 9 patterns, 16 color slots, Tool 8 refondu en 3 pages. Aucun changement moteur. Prérequis de toutes les phases LOOP qui consomment la grammaire LED. Inclut aussi l'extension `SettingsStore` pour les 3 timers LOOP (`clearLoopTimerMs`, `slotSaveTimerMs`, `slotClearTimerMs`) — voir §20 — avec leur édition en Tool 6.
2. **Phase 1** — Skeleton + guards : structs, enums, validate fonctions, NVS descriptors, guards dans BankManager / ScaleManager / MidiTransport. Aucune nouvelle feature visible.
3. **Phase 2** — LoopEngine core + main wiring : state machine, recording, playback, refcount, processLoopMode. Test mode activable via `ENABLE_LOOP_MODE`.
4. **Phase 3** — Setup tools : refactor Tool 3 vers b1 contextuel, extension Tool 5 (3-way type cycle + quantize), extension Tool 7 (3e contexte).
5. **Phase 4** — PotRouter + LED wiring : 3e contexte PotMappingStore, rendu LED LOOP complet (table `event_rendering` étendue), confirms via grammaire existante.
6. **Phase 5** — Effets : shuffle, chaos, velocity patterns, tout le bloc pots LOOP.
7. **Phase 6** — Slot Drive : LittleFS mount, LoopSlotStore, serialize/deserialize, handleLoopSlots, Tool 3 slot role section.

Chaque phase aura son **plan détaillé** séparé, listant les fichiers touchés, les signatures de fonction, les tests manuels, et les cross-references vers les invariants de ce document et de la LED spec.

**Option de repli** : si Phase 0 s'avère trop coûteuse à implémenter avant LOOP (machine d'état LED difficile à refactorer proprement), on bascule sur l'option (c) de la LED spec §25 — implémenter LOOP avec patterns ad-hoc puis grand refactor LED après stabilisation. Dette temporaire acceptée comme chemin de repli, pas comme plan nominal.

---

**Fin du brouillon.** Relecture et corrections attendues.
