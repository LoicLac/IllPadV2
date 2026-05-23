# Tool PAD ROLE — design spec

**Date** : 2026-05-23
**Statut** : VALIDÉE (issue d'une session de brainstorming 2026-05-23)
**Auteur** : Loïc (design) + Claude (rédaction)

**Spec parent affectée** : [`2026-04-19-loop-mode-design.md`](2026-04-19-loop-mode-design.md) §5 — catégorisation des pads et règles de collision **refondues par cette spec**.

**Specs et plans remplacés** (archivés) :
- `specs/2026-05-19-loop-phase-3-design.md`
- `plans/2026-05-19-loop-phase-3-plan.md` + son audit `_AUDIT.md` + son audit indépendant `_AUDIT_independent.md` + son prompt EXEC + son session-manifest.

**Code livré conservé** : commits `002400c` (3.A helpers cross-store), `cd3b3c9` (3.B Tool 4 ext refus LOOP), `97db63a` (3.C Tool 3 TAB nav + sous-page NORM + `_setFlash`). Ces livraisons restent acquises sur `main` et sont compatibles avec le nouveau concept moyennant les renommages décrits en §13.

**Étape suivante** : plan d'implémentation (session dédiée, `writing-plans`). La spec passe les détails de code et les sites au plan.

---

## §0 Index

- §1 — Contexte et motivation
- §2 — Vocabulaire
- §3 — Concept central : deux classes de rôles
- §4 — La règle unique de compatibilité
- §5 — Inventaire des rôles
- §6 — Architecture du tool : 4 pages
- §7 — Comportement d'édition par page
- §8 — Affichage de la grille
- §9 — Pool par page
- §10 — Modale d'écrasement
- §11 — Palette VT100
- §12 — Navigation et raccourcis
- §13 — Renommages
- §14 — Workflow musicien
- §15 — Conséquences sur l'existant
- §16 — Documentation à mettre à jour
- §17 — Hors scope explicite
- §18 — Sources et cross-refs

---

## §1 Contexte et motivation

Le firmware ILLPAD V2 propose un setup mode VT100 (accessible boot-only) où le musicien configure le rôle de chaque pad physique parmi 48. Historiquement, cette configuration vit dans deux outils distincts :

- **Tool 3 (Pad Roles)** édite les rôles musicaux et de modification : banks, scale roots/modes, hold pad ARPEG, octaves ARPEG ; Phase 1 LOOP a ajouté les déclarations LoopPadStore (3 controls REC/PS/CLR + 16 slots) sans en livrer l'UI.
- **Tool 4 (Control Pads)** édite les ControlPads MIDI : assignement cross-bank des pads à des CC MIDI, params DSP globaux.

Cette séparation a été créée par accumulation incrémentale, pas par design. Elle souffre de trois problèmes structurels.

### §1.1 Pas de référence canonique de « rôle de pad »

Chaque tool a son périmètre. Les règles de collision se sont multipliées de manière empirique au fur et à mesure que de nouveaux rôles apparaissaient (banks, scales, ARPEG, ControlPads, LOOP controls, LOOP slots). Aucun document, aucun module code ne centralise la sémantique de « rôle ». Les helpers cross-store livrés en Phase 3.A (`findBankIdxForPad`, `scaleRoleAtPad`, `arpRoleAtPad`, `isLoopControlPad` dans `KeyboardData.h`) sont des décorations tardives sur cette dispersion.

### §1.2 Catégorisation §5 spec parent LOOP devenue ambiguë

La session HW de Phase 3 LOOP (2026-05-20) a révélé que la catégorisation A/B/C avec règles R1-R6 (handoff `HANDOFF-2026-05-20-loop-phase-3-spec-rework.md`) mélangeait trois dimensions hétérogènes :
- Sémantique technique runtime (mode-sensibilité selon bank-type courant).
- Convention ergonomique (« bank sacré »).
- Layer physique d'activation (musical sans SHIFT vs shift avec SHIFT).

Les conséquences logiques de cette ambiguïté rendaient le code Phase 3.D/3.E ardues à écrire sans inventer des conventions que la spec ne couvrait pas.

### §1.3 La logique musicien renversée par la logique codeur

L'approche d'origine privilégiait des interdictions par défaut — « pad occupé → refus assignement ». Mais les superpositions de rôles que le code peut désambiguïser à runtime (par exclusion mutuelle sur bank-type, ou par exclusion mutuelle sur layer SHIFT) sont **musicalement utiles** :

- Avoir le même pad physique assigné à PL/S dans deux contextes (ARPEG et LOOP) est une grammaire de geste cohérente pour la mémoire musculaire. Le pad « PLAY/STOP universel » du musicien existe et a du sens — qu'il soit en bank ARPEG ou en bank LOOP, l'action est conceptuellement la même.
- Avoir un slot LOOP et un modificateur ARPEG (Root, Octave, etc.) sur le même pad fonctionne — bank ARPEG et bank LOOP sont mutuellement exclusives, donc une seule des deux fonctions est vivante à un instant donné.

Une logique « interdit toute superposition par défaut » force le musicien à étaler ses rôles sur des pads disjoints, perdant cette grammaire ergonomique.

### §1.4 Cette spec

Refonde l'ensemble en :

- **Un concept unique** : deux classes de rôles (ABSORBANT et CONTEXTUEL) + une règle de compatibilité dérivée des contextes d'activation runtime.
- **Un seul tool** : Tool PAD ROLE, 4 pages (BANK / ARPEG / LOOP / CC). Fusion architecturale de Tool 3 et Tool 4.
- **Une grammaire musicien** : seules les collisions qui produiraient une ambiguïté runtime sont refusées. Le reste est libre, et le musicien personnalise selon sa propre ergonomie.

---

## §2 Vocabulaire

| Terme | Définition |
|---|---|
| **Pad** | Un des 48 pads capacitifs physiques de l'instrument (NUM_KEYS = 48). |
| **Rôle** | Une fonction attribuée à un pad : Bank N, Root note, PL/S ARPEG, REC LOOP, ControlPad CC74, etc. |
| **Page** | Une des 4 vues du Tool PAD ROLE : BANK, ARPEG, LOOP, CC. |
| **Layer** | Couche physique d'activation d'un rôle : `musical` (sans SHIFT) ou `shift` (SHIFT button enfoncé). |
| **SHIFT button** | Bouton latéral physique qui active le layer shift. Code legacy `leftHeld` préservé (zero-migration). |
| **Bank-type** | Type d'une bank en runtime : NORMAL, ARPEG, ARPEG_GEN, LOOP. Fixé via Tool 5, persistant en NVS via BankTypeStore. |
| **Bank-type-filter** | Restriction de validité d'un rôle à un sous-ensemble de bank-types (ou `*` pour toutes). |
| **AC (Activation Context)** | Paire `(layer, bank-type-filter)` qui définit *quand* un rôle est vivant en runtime. |
| **ABSORBANT** | Classe de rôle qui occupe le pad entier, sans coexistence possible. |
| **CONTEXTUEL** | Classe de rôle qui n'est actif que dans un AC restreint, et peut coexister avec un autre CONTEXTUEL d'AC disjoint. |
| **PL/S** | Play/Stop. Label UI unifié pour les rôles de transport ARPEG et LOOP. |
| **Transport** | Catégorie de rôles musicaux de contrôle des moteurs : PL/S ARPEG, REC LOOP, PL/S LOOP, CLR LOOP. |
| **Modificateur** | Catégorie de rôles shift-layer qui modifient le contexte musical : Root, Mode, Chromatic, Octave (ARPEG) ; Slot 0..15 (LOOP). |
| **Pool** | Section UI sous la grille qui liste les rôles disponibles pour assignement, contextuel à chaque page. |
| **Foreground bank (FG)** | Bank actuellement sélectionnée, qui reçoit les entrées musicales. Une seule à un instant donné. |
| **Background bank (BG)** | Toute autre bank dont le moteur (ARPEG ou LOOP) reste actif sans recevoir d'entrées musicales. |

---

## §3 Concept central — Deux classes de rôles

Chaque rôle attribuable à un pad appartient à l'une de deux classes mutuellement exclusives.

### §3.1 ABSORBANT

Un rôle ABSORBANT **occupe le pad au sens Tool PAD ROLE** : aucun autre rôle attribuable dans cet outil ne peut cohabiter sur le pad. Il y a deux rôles ABSORBANTs, **asymétriques dans leur sémantique runtime** :

- **BANK** — un des 8 banks. ABSORBANT sur layer **shift uniquement**. Quand SHIFT est enfoncé, le pad sélectionne sa bank. Quand SHIFT est relâché, **le pad redevient un pad musical normal** et joue la note dictée par la scale active de la foreground bank. R1 « bank sacré » est donc une **convention ergonomique** qui interdit de poser un autre rôle Tool PAD ROLE sur le pad — pas une nécessité technique runtime.
- **CC MIDI** — un ControlPad. ABSORBANT **total**, insensible au SHIFT et au bank-type. Un pad CC envoie son CC **quel que soit l'état du SHIFT button** et **quel que soit le bank-type foreground**. Le pad CC ne joue donc **jamais de note musicale** : son comportement musical est intégralement remplacé par l'émission MIDI CC. C'est cette absorption complète qui justifie le refus de toute coexistence avec un autre rôle.

### §3.2 CONTEXTUEL

Un rôle CONTEXTUEL est actif uniquement dans un AC restreint `(layer, bank-type)`. Deux rôles CONTEXTUELs peuvent coexister sur le même pad **si leurs ACs sont strictement disjoints sur au moins une dimension** — c'est-à-dire si à aucun instant runtime ils ne sont vivants en même temps.

Il y a quatre rôles CONTEXTUELs, regroupés par AC :

| Rôle | Layer | Bank-type-filter | AC court |
|---|---|---|---|
| PL/S ARPEG | musical | ARPEG (et ARPEG_GEN) | M·A |
| REC / PL/S / CLR LOOP | musical | LOOP | M·L |
| Root, Mode, Chromatic, Octave | shift | ARPEG (et ARPEG_GEN) | S·A |
| Slots 0..15 | shift | LOOP | S·L |

**Pourquoi BANK est ABSORBANT et non CONTEXTUEL** : techniquement, le rôle Bank est `(shift, *)` — actif quand SHIFT enfoncé, dans toutes les banks. Il pourrait cohabiter avec un rôle musical (couches disjointes). La spec retient la convention ergonomique : un bank pad reste « pur » pour préserver sa lisibilité dans la mémoire de geste. **R1 « bank sacré » est une convention, pas une nécessité technique.**

**Pourquoi CC est ABSORBANT et non CONTEXTUEL** : un ControlPad est actif sur les deux layers (musical et shift) et dans toutes les banks. Son AC effectif est `(*, *)` — chevauche tout autre rôle en runtime. Sa présence absorbe le pad entièrement.

---

## §4 La règle unique de compatibilité

Soit un pad `p` et l'ensemble `R(p)` des rôles qui lui sont actuellement attribués. `R(p)` est **valide** ssi :

1. Si `R(p)` contient un rôle ABSORBANT, alors `|R(p)| = 1` (le rôle ABSORBANT est le seul rôle du pad).
2. Sinon, tous les rôles de `R(p)` sont CONTEXTUELs, et pour toute paire `(r1, r2)` dans `R(p)` avec `r1 ≠ r2`, les ACs de `r1` et `r2` sont disjoints sur au moins une dimension : `r1.layer ≠ r2.layer` OU `r1.bank-type ≠ r2.bank-type`.

Toute opération d'assignement qui produirait un `R(p)` invalide est **refusée** ou **forcée à arbitrage** (voir §7).

### §4.1 Conséquences automatiques

| Combinaison | Validité | Justification |
|---|---|---|
| PL/S ARPEG + PL/S LOOP sur même pad | OK | M·A et M·L disjoints sur bank-type (bank est ARPEG XOR LOOP) |
| Root ARPEG + Slot LOOP sur même pad | OK | S·A et S·L disjoints sur bank-type |
| PL/S ARPEG + Root ARPEG sur même pad | OK | M·A et S·A disjoints sur layer (musical vs shift) |
| REC LOOP + Slot LOOP sur même pad | OK | M·L et S·L disjoints sur layer |
| Root + Octave sur même pad | Refus | Mêmes layer et bank-type → mêmes AC → indistinguables runtime |
| PL/S ARPEG + ControlPad sur même pad | Refus | ControlPad ABSORBANT, occupe le pad seul |
| Bank + n'importe quoi | Refus | Bank ABSORBANT, occupe le pad seul (convention §3.1) |
| ControlPad + n'importe quoi | Refus | ControlPad ABSORBANT |

### §4.2 Cas par contraposée — quand un rôle CONTEXTUEL est seul de son AC sur un pad

Un rôle CONTEXTUEL est dit « intra-AC unique » : un pad ne peut porter qu'un seul rôle d'un AC donné. Donc :

- Au plus 1 rôle PL/S ARPEG par pad (mais évidemment 1 PL/S ARPEG par bank ARPEG max — détail runtime).
- Au plus 1 rôle de transport LOOP par pad (REC ou PL/S ou CLR, jamais deux).
- Au plus 1 rôle de modificateur ARPEG par pad (Root OU Mode OU Chromatic OU Octave, jamais deux).
- Au plus 1 rôle Slot LOOP par pad.

Donc le maximum théorique de rôles coexistant sur un même pad est **4** :
- 1 rôle M·A (PL/S ARPEG)
- 1 rôle M·L (REC ou PL/S ou CLR LOOP)
- 1 rôle S·A (un modificateur ARPEG)
- 1 rôle S·L (un Slot LOOP)

En pratique, la plupart des pads porteront 0, 1 ou 2 rôles selon les choix du musicien.

---

## §5 Inventaire des rôles

Synthèse exhaustive des rôles attribuables à un pad.

| # | Nom UI | Classe | AC | Édité dans page | Store NVS sous-jacent |
|---|---|---|---|---|---|
| 1 | BANK 1..8 (B1..B8) | ABSORBANT | (shift, *) — convention | BANK | `bankPads[]` (main.cpp, exporté à BankManager) |
| 2 | CC MIDI (CC00..C127) | ABSORBANT | (*, *) — toutes couches/banks | CC | `ControlPadStore` (illpad_ctrl) |
| 3 | PL/S ARPEG | CONTEXTUEL M·A | (musical, ARPEG/ARPEG_GEN) | ARPEG | `ArpPadStore.holdPad` (renommé arpPlayStopPad — §13) |
| 4 | REC LOOP | CONTEXTUEL M·L | (musical, LOOP) | LOOP | `LoopPadStore.recPad` |
| 5 | PL/S LOOP | CONTEXTUEL M·L | (musical, LOOP) | LOOP | `LoopPadStore.playStopPad` |
| 6 | CLR LOOP | CONTEXTUEL M·L | (musical, LOOP) | LOOP | `LoopPadStore.clearPad` |
| 7 | Root (notes 0..6, qty 7) | CONTEXTUEL S·A | (shift, ARPEG/ARPEG_GEN) | ARPEG | `ScalePadStore.rootPads[7]` |
| 8 | Mode (7 modes, qty 7) | CONTEXTUEL S·A | (shift, ARPEG/ARPEG_GEN) | ARPEG | `ScalePadStore.modePads[7]` |
| 9 | Chromatic | CONTEXTUEL S·A | (shift, ARPEG/ARPEG_GEN) | ARPEG | `ScalePadStore.chromaticPad` |
| 10 | Octave (4 niveaux, qty 4) | CONTEXTUEL S·A | (shift, ARPEG/ARPEG_GEN) | ARPEG | `ArpPadStore.octavePads[4]` |
| 11 | Slot LOOP (S00..S15, qty 16) | CONTEXTUEL S·L | (shift, LOOP) | LOOP | `LoopPadStore.slotPads[16]` |

Note : les modificateurs ARPEG (rôles #7-10) sont distribués entre `ScalePadStore` et `ArpPadStore` pour des raisons historiques. Cette répartition est **conservée** par la spec — la fusion en un store unique relève d'une refonte indépendante.

---

## §6 Architecture du tool — 4 pages

### §6.1 Identité

- **Nom** : Tool PAD ROLE.
- **Fusion** : absorbe l'actuel Tool 3 (pad assignment) et l'actuel Tool 4 (control pads). Le Tool 4 est supprimé en tant que tool autonome ; son contenu devient la page CC du Tool PAD ROLE.
- **Position dans le menu setup** : reprend la slot du Tool 3 (descriptor index 3, badge T3). Tool 4 slot devient libre (ou re-attribuée — décision plan d'impl).

### §6.2 Les 4 pages

| Ordre TAB | Nom | Rôles édités | Lentille AC |
|---|---|---|---|
| 1 | **BANK** | les 8 banks | (shift, *) — convention bank sacré |
| 2 | **ARPEG** | PL/S ARPEG, Root, Mode, Chromatic, Octave | M·A + S·A |
| 3 | **LOOP** | REC, PL/S, CLR, Slots | M·L + S·L |
| 4 | **CC** | ControlPads MIDI | (*, *) absorbante |

Chaque page est une **lentille** sur un ou plusieurs ACs. Elle gère exclusivement ses rôles propres et affiche les rôles voisins de manière sobre (cf §8).

### §6.3 Navigation entre pages

- **TAB** : avance cycliquement BANK → ARPEG → LOOP → CC → BANK.
- **SHIFT+TAB** : non spécifié dans cette spec (à trancher au plan d'impl si besoin de retour en arrière rapide).
- À l'intérieur d'une page : flèches haut/bas/gauche/droite pour nav grid + pool, ENTER pour commit, raccourcis lettres (`r` reset, `x` remove, etc.) — voir §12.

### §6.4 Comportement à l'entrée du tool

Au lancement de Tool PAD ROLE :
- Page courante = BANK (entrée par défaut).
- Le curseur grille positionné sur le 1er pad pertinent (par défaut pad 0).
- Le pool affiche les entries du contexte courant.

### §6.5 Hard-constraint à la sortie du tool

Le tool refuse la sortie si **les 8 banks ne sont pas toutes assignées à un pad valide**. Affichage d'un flash explicite (« Les 8 banks doivent etre assignees pour sortir »).

Aucun autre rôle n'est obligatoire pour sortir :
- PL/S ARPEG, transports LOOP, modificateurs ARPEG, slots LOOP, ControlPads : tous optionnels.
- Si un musicien sort sans assigner les controls LOOP alors qu'une bank LOOP existe, le runtime utilisera les defaults (cf §17 hors scope).

Cette politique est plus permissive que ce qui était prévu en Phase 3.E précédente (qui imposait REC/PL/S/CLR LOOP obligatoires si une bank LOOP existait). La logique : le musicien personnalise. Les defaults factory couvrent les cas où un rôle n'est pas explicitement assigné.

### §6.6 Propagation immédiate entre pages

Tout changement effectué dans une page (assignement, dégagement, swap-to-pool, écrasement par modale) se reflète **immédiatement** dans les autres pages du même tool. Les 4 pages partagent un état de travail unique en mémoire ; **aucun reboot, aucun commit NVS intermédiaire** n'est requis pour voir un changement propagé cross-page.

Conséquences pratiques :
- Assigner Bank 3 au pad 22 en page BANK rend le pad 22 immédiatement interdit (fond ambre+) sur les pages ARPEG, LOOP et CC dès que le musicien y TAB.
- Dégager Root C du pad 30 en page ARPEG fait disparaître ce rôle de l'info panel des autres pages immédiatement.
- L'écrasement de rôles CONTEXTUELs via modale en page absorbante propage immédiatement les pertes : les rôles écrasés réapparaissent dans le pool de leur page d'origine au TAB suivant.

Le commit NVS final intervient à la **sortie du tool** (`saveAll` existant Tool 3, étendu pour persister tous les stores impactés). Pendant la session de configuration, tout est en mémoire de travail partagée entre les pages.

---

## §7 Comportement d'édition par page

### §7.1 Matrice exhaustive

| Page courante | Pad porte | Action assignement |
|---|---|---|
| **BANK** | vide | Assigne la bank sélectionnée, commit immédiat. |
| **BANK** | rôle propre (bank existante) | UX de toggle/dégage selon convention existante : ENTER sur une bank assignée la dégage et la retourne au pool ; le pad redevient vide. |
| **BANK** | autre absorbant (ControlPad) | **Interdit**. Cell affichée en fond ambre+ (saturé), nav OK mais focus refused, info panel « Pad interdit (CC) ». |
| **BANK** | 1 ou 2 rôles CONTEXTUELs (ARPEG / LOOP) | **Modale d'écrasement** (cf §10). Si y → les rôles CONTEXTUELs écrasés retournent à leurs pools respectifs, la bank est assignée. Si n → annulation. |
| **CC** | vide | Assigne le CC sélectionné, commit immédiat. |
| **CC** | rôle propre (CC existant) | UX existante de Tool 4 (édition params CC, suppression, etc.). |
| **CC** | autre absorbant (Bank) | **Interdit**, fond ambre+, focus refused. |
| **CC** | rôles CONTEXTUELs | **Modale d'écrasement** symétrique à la page BANK. |
| **ARPEG** | vide | Assigne le rôle ARPEG sélectionné dans le pool. |
| **ARPEG** | rôle ARPEG propre intra-AC (même AC que celui qu'on tente d'assigner) | **Swap-to-pool silencieux** : l'ancien rôle retourne au pool, le nouveau est assigné. Pas de modale (déplacement intra-page, sans conséquence cross-page). |
| **ARPEG** | rôle LOOP (CONTEXTUEL d'AC disjoint) | **Coexistence** : le rôle ARPEG est assigné par-dessus, sans toucher le rôle LOOP. Pas de modale. |
| **ARPEG** | absorbant (Bank ou CC) | **Interdit**, fond ambre+, focus refused. |
| **LOOP** | symétrique à ARPEG | Idem. Swap-to-pool intra-LOOP silencieux, coexistence cross-context avec ARPEG, refus absorbants. |

### §7.2 Définition du « swap-to-pool intra-AC »

Lorsqu'un musicien assigne un rôle CONTEXTUEL `r2` à un pad qui porte déjà un rôle CONTEXTUEL `r1` du **même AC**, le système :

1. Désassigne `r1` du pad (efface l'attribution).
2. Renvoie `r1` au pool de sa page courante avec la couleur « assignable » (vert menthe, cf §11).
3. Assigne `r2` au pad.

Pas de modale, pas de confirmation. C'est un mouvement intra-page que le musicien peut annuler en re-assignant `r1` à un autre pad (ou au même).

### §7.3 Coexistence cross-AC (R4/R5 du handoff)

Lorsqu'un rôle CONTEXTUEL est assigné à un pad qui porte déjà un rôle CONTEXTUEL d'AC disjoint, **aucun arbitrage n'est nécessaire** : les deux coexistent. La présence du rôle voisin n'est **pas signalée** en page contextuelle (cf §8). Le musicien peut TAB vers l'autre page pour la voir.

### §7.4 Comportement de retrait

- En page BANK : ENTER sur une bank assignée la dégage → pool. Pad redevient vide.
- En page CC : raccourci `x` ou équivalent Tool 4 existant → CC retiré → pad redevient vide (ou conserve les rôles CONTEXTUELs qu'il portait avant l'attribution du CC si tel était le cas — la spec d'origine ne couvre pas car CC ABSORBANT efface tout à l'assignement).
- En page ARPEG / LOOP : ENTER sur un rôle assigné le dégage → pool. Si le pad portait aussi un rôle d'AC disjoint (cross-context), celui-ci reste en place.

### §7.5 Reset global

Raccourci `r` ou `R` (avec confirmation y/n) : reset des rôles de la page courante. Convention existante Tool 3.

- En page BANK : reset des 8 banks (pad → vide pour tous). Attention : viole temporairement la hard-constraint exit (§6.5). Le tool refuse la sortie tant que les 8 banks ne sont pas re-assignées.
- En page CC : reset des CCs (vide la ControlPadStore).
- En page ARPEG : reset des rôles ARPEG (Root, Mode, Chrom, Octave, PL/S ARPEG).
- En page LOOP : reset des rôles LOOP (REC, PL/S, CLR, Slots 0..15).

Le reset de la page courante **n'efface jamais** les rôles des autres pages sur les pads. C'est une opération strictement locale à la lentille AC de la page.

---

## §8 Affichage de la grille

La grille affiche les 48 pads sur 4 lignes × 12 colonnes. Chaque cell fait **4 chars** centrés/paddés.

### §8.1 Matrice de rendu par page

| Cell porte | BANK | CC | ARPEG | LOOP |
|---|---|---|---|---|
| Vide | ` -- ` | ` -- ` | ` -- ` | ` -- ` |
| Rôle propre, seul | `-B1-`..`-B8-` blanc | `CC00`..`CC99`, puis `C100`..`C127` blanc | label ARPEG en sa couleur (Rt./Mo./Ch./Oc./P/S) | label LOOP en sa couleur (REC/P/S/CLR/S00..S15) |
| Rôle CONTEXTUEL d'une autre page (1 ou 2 rôles ARPEG/LOOP coexistants sur le pad) | ` ■■ ` neutre | ` ■■ ` neutre | **rien (vide ou rôle ARPEG propre affiché)** | **rien (vide ou rôle LOOP propre affiché)** |
| Autre absorbant occupé | `CC..` dim **fond ambre+ saturé** | `-B1-`..`-B8-` dim fond ambre+ | `-B1-`..`-B8-` ou `CC..` dim fond ambre+ | `-B1-`..`-B8-` ou `CC..` dim fond ambre+ |

### §8.2 Principes

- **Chaque page affiche son rôle propre en pleine couleur.** Un pad qui porte un rôle BANK est affiché `-B3-` blanc en page BANK ; un pad qui porte un Root C est affiché `Rt.C` en couleur pêche en page ARPEG.
- **Les rôles CONTEXTUELs des autres pages sont représentés différemment selon la page courante :**
  - **En page absorbante (BANK ou CC)** : signal `■■` neutre. Indique au musicien qu'un rôle CONTEXTUEL occupe ce pad et qu'une assignement déclenchera la modale d'écrasement (§10). Pas de différenciation entre 1 ou 2 rôles voisins, pas d'identification du rôle précis (info accessible via la modale et l'info panel).
  - **En page contextuelle (ARPEG ou LOOP)** : aucune représentation des rôles de l'autre page contextuelle. La coexistence cross-context est invisible dans la grille. Pour la voir, le musicien TAB vers l'autre page.
- **Les rôles ABSORBANTs des autres pages sont toujours signalés en fond ambre+ saturé.** C'est l'« interdit » visuel : focus refused, label affiché en dim (`CC..` ou `-Bx-`) pour informer le musicien de *quel* absorbant occupe le pad. Nav OK (curseur peut traverser), edit refused. Info panel : « Pad interdit (rôle X). »
- **Vide partout pareil** : ` -- ` 4 chars.

### §8.3 Largeur et alignement

Toutes les cells font 4 chars. Les labels sont paddés ou centrés pour occuper l'espace :

- 2 chars : centré avec 1 espace de chaque côté. Ex `BANK` : `-B1-` (centré 4 chars).
- 3 chars : centré (1 espace à gauche ou droite selon convention existante). Ex `REC` : ` REC`.
- 4 chars : tient juste. Ex `S00 ` ou `CC00`.
- Cas CC > 100 : `C100` à `C127` (3 chars C+num, padded).

### §8.4 Curseur de navigation

Le curseur indique la cell courante. Rendu : **inverse fg/bg sur les 4 chars de la cell** (pattern reprend les badges NVS OK existants côté setup mode). Le curseur peut se déplacer librement sur toutes les cells, même les cells interdites — l'info panel signale alors « Pad interdit » et ENTER ne fait rien (no-op + flash discret).

### §8.5 Info panel

Sous la grille, l'info panel décrit la cell courante. Contenu selon situation :

- Cell vide : « Pad libre » (sans plus).
- Cell avec rôle propre seul : nom du rôle (« Bank 3 », « Root C », « Slot 5 », « CC74 ch1 mode CONTINUOUS »).
- Cell avec rôle propre + rôles cross-page (page contextuelle) : nom du rôle propre, mention discrète des autres rôles présents (« Root C (+ Slot 5 LOOP) »). Cette info reste dans le panel, jamais dans la grille.
- Cell avec rôles cross-page seuls (page absorbante, donc cell affichée `■■`) : liste des rôles voisins (« Root C (ARPEG) » ou « Root C (ARPEG) + Slot 5 (LOOP) »).
- Cell avec absorbant cross-page : nom de l'absorbant (« Bank 3 — interdit ici » ou « CC74 — interdit ici »).

---

## §9 Pool par page

Sous la grille et l'info panel, le pool affiche les rôles disponibles pour assignement, contextuellement à la page courante.

### §9.1 Contenu par page

| Page | Entries du pool |
|---|---|
| BANK | 8 entries B1..B8. Une bank est dans le pool ssi elle n'est pas assignée à un pad. |
| ARPEG | Root (7 entries pour les 7 notes), Mode (7 entries), Chromatic (1), Octave (4 entries 1× / 2× / 3× / 4×), PL/S ARPEG (1). |
| LOOP | REC (1), PL/S LOOP (1), CLR (1), Slots S00..S15 (16). |
| CC | reprise stricte du pool Tool 4 actuel : entries ControlPad existantes avec params (CC# / channel / mode / deadzone / release) et entry « add new ». |

### §9.2 Couleur des entries

- Entry **assignée** (présente sur un pad de la grille) : affichée en couleur dim ou avec un marqueur d'occupation (ex check, ou parenthèses, ou un autre signe — à finaliser au mockup).
- Entry **assignable** (non encore sur la grille, prête à être placée) : affichée en **vert menthe** (couleur dédiée distincte du vert PL/S de la grille). Cette couleur signale « je suis disponible ».

Le vert menthe et le vert PL/S sont visuellement distinguables. Le contexte de section (pool en bas vs grille en haut) désambiguïse également. Ce choix est explicite et documenté (cf §11.5).

### §9.3 Layout

Le pool peut s'étendre sur plusieurs lignes selon la page. Convention existante Tool 3 : 5 lignes de pool (une par catégorie de rôle).

- Page BANK : 1 ligne (8 banks).
- Page ARPEG : 4-5 lignes (Root / Mode / Chrom / Octave / PL/S).
- Page LOOP : 3-4 lignes (REC + PL/S + CLR sur 1 ligne, Slots S00..S15 sur 1-2 lignes selon largeur).
- Page CC : reprise Tool 4 (1-2 lignes selon convention existante).

L'overflow éventuel des slots LOOP (16 entries) tient sur 1 ligne de 80 chars (16 × ~4 chars = 64 chars), pas de scroll requis.

---

## §10 Modale d'écrasement

Apparaît uniquement en page absorbante (BANK ou CC) lorsque le musicien tente d'assigner un rôle ABSORBANT sur un pad portant 1 ou 2 rôles CONTEXTUELs.

### §10.1 Forme

Modale bloquante avec choix `[y/n]`. Convention existante Tool 3 (raccourci `y` pour valider, `n` ou Escape pour annuler).

### §10.2 Wording

Cas 1 rôle écrasé :
```
En placant B3 sur ce pad, "ROOT A de ARPEG" devra etre reattribue. Y/N ?
```

Cas 2 rôles écrasés :
```
En placant B3 sur ce pad, "ROOT A de ARPEG" et "PLAY/STOP de LOOP" devront etre reattribues. Y/N ?
```

### §10.3 Identification des rôles dans la modale

Langue musicien, pas numéros d'index.

**Rôles ARPEG** :
- Root : `"ROOT A"`, `"ROOT B"`, etc. (note nommée)
- Mode : `"MODE Maj"`, `"MODE Min"`, `"MODE Dor"`, etc. (mode nommé)
- Octave : `"OCTAVE 1"`, `"OCTAVE 2"`, etc. (niveau)
- Chromatic : `"CHROMATIC"` (singleton)
- PL/S ARPEG : `"PLAY/STOP de ARPEG"`

**Rôles LOOP** :
- REC : `"REC de LOOP"`
- PL/S LOOP : `"PLAY/STOP de LOOP"`
- CLR : `"CLEAR de LOOP"`
- Slot : `"SLOT de LOOP"` (sans numéro — les slots sont fongibles, leur identification numérique est inutile au musicien dans ce contexte)

### §10.4 Effet de la confirmation

- `y` (yes) : les rôles CONTEXTUELs écrasés retournent à leurs pools respectifs (pool ARPEG ou LOOP). Le rôle ABSORBANT (Bank ou CC) est assigné au pad. Commit.
- `n` ou Escape : annulation. Le pad reste tel quel, l'absorbant n'est pas assigné.

Pas de modale dans les pages contextuelles (cf §7).

---

## §11 Palette VT100

La palette est définie en termes de **noms de couleurs** et **rôles sémantiques**. Les codes VT100 exacts (ANSI 8/16/256-color) sont tranchés au mockup/implémentation, en cohérence avec la palette existante (`vt100-design-guide.md`).

### §11.1 Couleurs des rôles propres

| Rôle | Couleur |
|---|---|
| Bank (B1..B8) | blanc |
| CC MIDI (CC00..C127) | blanc |
| LOOP REC | rouge |
| LOOP CLEAR | bleu foncé |
| LOOP PL/S | vert |
| LOOP Slots S00..S15 | jaune |
| ARPEG Root | pêche |
| ARPEG Mode | cyan |
| ARPEG Chromatic | cyan (même couleur que Mode — proximité conceptuelle) |
| ARPEG Octave | pourpre |
| ARPEG PL/S | vert (identique LOOP PL/S — geste unifié visible) |

Le **vert** est partagé par les deux rôles PL/S (ARPEG et LOOP). C'est intentionnel : un musicien qui place PL/S ARPEG et PL/S LOOP sur le même pad (coexistence M·A + M·L) verra ce pad en vert dans les deux pages — visuellement cohérent avec sa décision ergonomique « PL/S universel ».

### §11.2 Couleur du « interdit » (absorbant cross-page)

Fond ambre+ saturé — c'est-à-dire la couleur du fond ambient VT100 (ambre clair) **saturée d'un cran**, donnant un effet de « gravé dans le fond ». Le label de l'absorbant est affiché par-dessus en dim.

La teinte exacte est à trancher au moment du code, en auditant la palette VT100 existante (voir §11.6).

### §11.3 Couleur du pool « assignable »

**Vert menthe** — distinct du vert PL/S de la grille. Marque les entries du pool qui ne sont pas assignées et donc disponibles pour placement.

### §11.4 Couleur du curseur de navigation

**Inverse fg/bg** sur la cell courante (4 chars). Pattern existant des badges NVS OK dans l'instrument.

### §11.5 Note sur le double sens du vert

Le vert apparaît dans deux contextes avec des sens distincts :
- Vert **grille** (cell entière colorée vert) = rôle PL/S assigné.
- Vert **menthe pool** (entry du pool colorée vert menthe) = entry assignable.

La distinction repose sur :
1. Une teinte différente (menthe vs vert pur).
2. Un contexte de section (pool en bas vs grille en haut).

Cette double sémantique est acceptée comme un compromis pragmatique. Si à l'usage la confusion s'avère problématique, une 2e couleur peut être attribuée au pool « assignable » (décision future, hors cette spec).

### §11.6 Audit de la palette ambient

Au moment du code, vérifier la teinte exacte du fond VT100 actuel pour calibrer la couleur « ambre+ saturé » des interdits. Source : `docs/reference/vt100-design-guide.md` et code VT100 inline dans les Tools existants.

---

## §12 Navigation et raccourcis

### §12.1 Navigation entre pages

- **TAB** : avance cycliquement BANK → ARPEG → LOOP → CC → BANK.

### §12.2 Navigation dans une page

- **Flèches haut / bas / gauche / droite** : déplacent le curseur dans la grille puis dans le pool, selon le pattern Tool 3 existant.
- **ENTER** : commit l'action contextuelle au focus courant (assignement, dégagement, ou ouverture modale d'écrasement selon §7).

### §12.3 Raccourcis lettres

Réutilisation des conventions existantes (cf `setup-tools-conventions.md`) :

- **`y`** : confirme `y/n` (utilisé pour modale d'écrasement §10 et reset global §7.5).
- **`n`** ou **Escape** : annule `y/n`.
- **`r`** ou **`R`** : reset des rôles de la page courante (avec confirmation y/n).
- **`x`** ou **`X`** : suppression individuelle (typiquement dans le pool CC pour retirer une entry).

D'autres raccourcis spécifiques à la page CC (édition params) sont à reprendre tel quel de l'actuel Tool 4.

### §12.4 Hardware pad tap

Pattern existant : taper un pad physique pendant que le tool est ouvert positionne le curseur sur ce pad dans la grille. Cf `vt100-design-guide.md` ligne 312.

---

## §13 Renommages

Cette spec introduit deux renommages, séparés en deux groupes selon leur portée.

### §13.1 Renommage de page : NORM → BANK

**Périmètre** : Tool 3 uniquement, ~6 sites code. Trivial.

- L'identifiant interne `SubPage::SUB_NORM` devient `SUB_BANK`.
- L'ajout de `SUB_CC` est introduit par cette spec (la 4e page).
- Les méthodes de rendu et de construction du role map sont renommées symétriquement (`_buildRoleMapNorm()` → `_buildRoleMapBank()`, et idem pool/draw).
- Le label UI VT100 `"NORM"` devient `"BANK"`.

Aucun impact NVS, aucun impact runtime musical. Détails sites/signatures au plan d'impl.

### §13.2 Renommage de rôle : holdPad → arpPlayStopPad

**Périmètre** : ~30 sites code dispersés (main, managers, NvsManager, ToolPadRoles, ArpEngine, KeyboardData). Refacto pure, attrapée intégralement par le compilateur.

**Pourquoi renommer** : le rôle « hold pad » était historiquement un bouton qui faisait « sustain l'arpège tant que pressé » (HOLD ARPEG). Suite au fix F1 du 2026-05-15 ([`src/arp/ArpEngine.cpp:514-545`](../../src/arp/ArpEngine.cpp:514)), la sémantique a été simplifiée : pile sacrée armée, le bouton fait désormais un toggle Play/Stop du moteur ARPEG (preserve `_pausedPile` à `false → true` lors de Play→Stop, relance la pile à `true → false` lors de Stop→Play). Le nom « hold » est historiquement obsolète et ne reflète plus la sémantique runtime. Le nom canonique devient **`arpPlayStopPad`** :
- Préfixe `arp` explicite le contexte (cohérent avec d'autres préfixes du projet : `arpEngine`, `ArpPadStore`).
- `PlayStop` explicite l'action (toggle).
- Pas d'abréviation cryptique — favorise la lecture LLM future.
- Cohérent avec le label UI `PL/S` (rôle exposé au musicien).

**Périmètre code** :

Le rename inclut :
- Variables globales et locales (`s_holdPad`, `_holdPad`, `holdPad`, `_wkHoldPad`).
- Setters de managers (`BankManager::setHoldPad`, `ScaleManager::setHoldPad`).
- Function handler (`handleHoldPad` dans main).
- Enum value (`ArpRoleKind::HOLD` → `ArpRoleKind::PLAY_STOP`).
- Paramètres de signatures (`arpRoleAtPad`, `ArpEngine::setCaptured`, signatures `NvsManager::loadAll`, `ToolPadRoles::begin`, `SetupManager::begin`).
- Field NVS struct `ArpPadStore.holdPad` → `ArpPadStore.arpPlayStopPad`.
- Commentaires inline contenant « HOLD » ou « hold pad ».

**Politique NVS** : le rename du field NVS struct implique un **bump de `ARPPAD_VERSION`** (de 1 à 2). Conforme à la zero-migration policy projet :
- Au boot suivant la livraison, la version NVS actuelle (1) ne match plus la version code (2).
- Le validator `validateArpPadStore` est appliqué sur une struct pré-init à `0xFF` (defaults factory).
- Le musicien doit re-saisir son pad PL/S ARPEG via Tool PAD ROLE page ARPEG. C'est une re-config single-pad, ergonomiquement trivial.

Détails de sites, ordre d'application, et vérifications cross-fichiers au plan d'impl.

### §13.3 Pas d'autre renommage dans cette spec

Le projet contient d'autres identifiants legacy potentiellement déstabilisants (`leftHeld` pour SHIFT button, `Hold` mentions dans `loop-buffer-invariants.md`, etc.). Aucun de ces autres renommages n'est mandaté par cette spec.

---

## §14 Workflow musicien — scenarios live

Ces scenarios ne sont pas normatifs mais ancrent la spec dans le réel et servent de test mental.

### §14.1 Scenario A — session live mixed ARPEG + LOOP

Le musicien démarre. Sa config (faite préalablement via Tool PAD ROLE) :
- Bank 1 = NORMAL (jeu libre).
- Bank 2 = ARPEG (arpège mélodique).
- Bank 3 = LOOP (loop percussif).
- Pad 30 = PL/S unifié (PL/S ARPEG + PL/S LOOP coexistants — cell verte sur pages ARPEG et LOOP).
- Pad 31 = REC LOOP (cell rouge page LOOP, ` ■■ ` neutre page BANK/CC, rien page ARPEG).
- Pad 32 = CLR LOOP (cell bleu foncé page LOOP).
- Pad 47 = CC74 (cell blanche page CC, ` ■■ ` impossible sur les autres pages car ABSORBANT — fond ambre+).
- Pad 5 = Root C ARPEG (cell pêche page ARPEG) + Slot 0 LOOP (cell jaune page LOOP). Coexistence S·A + S·L.

Foreground bank 3 LOOP. Le musicien tape pad 31 → REC commence. Il joue des notes sur les pads musicaux. Re-tap pad 31 (ou tap pad 30) → enregistrement clos, loop part en PLAYING.

Le musicien tape SHIFT + bank 2 pour basculer en ARPEG. Foreground bank 2 ARPEG ; loop bank 3 continue en background. Le musicien joue ARPEG. Pour stopper le moteur ARPEG, il tape pad 30 (qui est PL/S ARPEG en contexte ARPEG). Moteur ARPEG OFF, pile sacrée armée. Pad 30 affichait précédemment vert (PL/S) ; sa LED change selon ARPEG state, mais le rôle reste.

Pendant tout ce temps, pad 47 (CC74) reste actif. Toute pression — musical layer ou shift layer — envoie le CC74.

Le musicien revient sur LOOP (SHIFT + bank 3). Foreground bank 3. Tap pad 30 → PL/S LOOP toggle (rôle M·L désormais actif). Le pad « PL/S universel » a fait son office.

### §14.2 Scenario B — ré-attribution d'une bank pendant la session setup

Le musicien décide que Bank 5 doit aller sur le pad 22, mais ce pad porte déjà Root D (ARPEG) + Slot 3 (LOOP).

Il ouvre Tool PAD ROLE, navigue en page BANK, déplace le curseur sur pad 22. La cell est affichée ` ■■ ` neutre (deux rôles CONTEXTUELs voisins, page BANK les agrège en signal neutre). L'info panel affiche « Root D (ARPEG) + Slot 3 (LOOP) ».

Il sélectionne Bank 5 dans le pool (B5 vert menthe assignable), ENTER. Modale apparaît :

```
En placant B5 sur ce pad, "ROOT D de ARPEG" et "SLOT de LOOP" devront etre reattribues. Y/N ?
```

Le musicien tape `y`. Root D et Slot 3 retournent à leurs pools (vert menthe). Bank 5 est assignée au pad 22.

Il tabbe à la page ARPEG pour re-attribuer Root D ailleurs. ENTER, fait.

### §14.3 Scenario C — collision avec un CC

Le musicien veut placer Root B sur le pad 47, qui porte CC74. Il navigue en page ARPEG sur pad 47. La cell est affichée `CC74` dim fond ambre+ — focus refused. ENTER ne fait rien (no-op + flash discret). Info panel : « CC74 — interdit ici ».

Le musicien comprend qu'il doit d'abord supprimer le CC s'il veut Root B ici. TAB vers page CC, supprime CC74 (raccourci `x`). Retour page ARPEG → pad 47 est désormais vide. Il assigne Root B.

---

## §15 Conséquences sur l'existant

### §15.1 Code livré 3.A / 3.B / 3.C — compatibilité

Les trois commits livrés (2026-05-19) restent acquis sur `main` et sont **compatibles** avec le nouveau concept moyennant les renommages §13.

| Commit | Apport | Compatibilité Tool PAD ROLE |
|---|---|---|
| `002400c` (3.A) | Helpers cross-store inline dans `KeyboardData.h` (`findBankIdxForPad`, `scaleRoleAtPad`, `arpRoleAtPad`, `isLoopControlPad`) + NvsManager LoopPad setter/save | Helpers réutilisés directement par la logique de validation cross-page. Aucun changement de signature. Renommage `holdPad` → `arpPlayStopPad` impactera la signature `arpRoleAtPad`. |
| `cd3b3c9` (3.B) | Tool 4 extension : refus assignement ControlPad sur pad LOOP control. Dev seed M7 déplacé pre-setup gate. | Comportement intégré naturellement dans le nouveau concept (CC ABSORBANT vs M·L → collision selon §4). Le code Tool 4 actuel sera **absorbé** par la nouvelle page CC du Tool PAD ROLE. Refus collision préservé. |
| `97db63a` (3.C) | Tool 3 TAB nav (sous-page NORM stub) + `_setFlash` infrastructure + sous-page NORM (bank slot move + refus collision destination cross-store) | TAB nav conservé et étendu (ajout sous-page CC, cycle à 4 pages). Renommage sous-page NORM → BANK §13.1. `_setFlash` réutilisé. Code de bank slot move dans la sous-page NORM/BANK conservé. |

Le plan d'impl construit par-dessus ces 3 commits, sans rollback ni reset.

### §15.2 Tool 4 absorbé

Le module code Tool 4 (`src/setup/ToolControlPads.{cpp,h}`) ne disparaît pas nécessairement comme fichier physique. Le plan d'impl peut soit :
- (a) Le conserver comme « engine CC » consommé par la page CC du Tool PAD ROLE.
- (b) Fusionner son contenu dans le module Tool PAD ROLE.

Cette décision est laissée au plan d'impl, selon le coût de chaque option et la cohérence avec l'architecture du tool fusionné.

### §15.3 Tool 5 (BankType / params per-bank) — inchangé

Tool 5 attribue les bank-types et les params per-bank (ARPEG_GEN params, etc.) Il ne touche pas aux rôles de pad. Aucun changement.

### §15.4 Tool 7 (PotMapping) — inchangé

Tool 7 mappe les pots aux destinations runtime. Aucun lien avec Tool PAD ROLE.

### §15.5 Setup mode au boot

Aucun changement de comportement boot mode. Le tool est accessible uniquement boot-only (cf invariant projet « Setup mode — boot-only par construction »). La sortie du tool déclenche le reboot habituel.

---

## §16 Documentation à mettre à jour

Cette spec impacte 10 documents. Les patches sont appliqués dans le même commit que cette spec (« commit groupé docs(tool-pad-role) »).

### §16.1 À patcher

| Fichier | Patch principal |
|---|---|
| `specs/2026-04-19-loop-mode-design.md` | §5 catégorisation pads : note de refonte + pointer vers cette spec. |
| `reference/setup-tools-conventions.md` | **Refonte** des mentions Tool 3 / Tool 4 → Tool PAD ROLE 4 pages. Convention canonique reste valide. |
| `reference/arp-reference.md` | « Hold pad » → « PL/S ARPEG pad (Tool PAD ROLE page ARPEG) ». Signature `setCaptured(..., holdPad)` → `arpPlayStopPad`. Sémantique « fingers down → wipe pile » : actualiser (supprimée par fix F1 du 2026-05-15). |
| `reference/nvs-reference.md` | Field rename `ArpPadStore.holdPad` → `arpPlayStopPad`. Bump version 1 → 2. LoopPadStore : retirer la note dev seed Phase 2 (résolu par Tool PAD ROLE livraison). |
| `reference/runtime-flows.md` | Mentions `holdPad`/`_holdPad` → `arpPlayStopPad`. |
| `reference/architecture-briefing.md` | Tableau ownership pad stores : Tool 3/Tool 4 → Tool PAD ROLE (avec pages explicites). Templates « new mode of play » / « new pad role category » : adapter. |
| `reference/vt100-design-guide.md` | Sections « Tool 3 — Pad Roles » et « Tool 4 — Control Pads » : refonte en « Tool PAD ROLE — 4 pages ». Section « 6 role categories » : refonte en « 2 classes / 6 rôles ». |
| `reference/loop-buffer-invariants.md` | Référence `HOLD_PAD` → `ARP_PLAY_STOP_PAD`. |
| `STATUS.md` | Focus courant : reformulation spec Tool PAD ROLE (Phase 3 en cours, plan repris session suivante). |
| `docs/superpowers/LOOP_PROGRESS.md` | Tableau Phase 3 : description « Tool 3 b1 refactor (3 sous-pages...) + Tool 4 ext » → « Tool PAD ROLE (4 pages, fusion Tool 3+4) ». Pointer cette spec. |

### §16.2 À archiver

Vers `docs/archive/` avec note d'archivage en tête :

| Fichier | Raison |
|---|---|
| `specs/2026-05-19-loop-phase-3-design.md` | Caduc, remplacé par cette spec. |
| `plans/2026-05-19-loop-phase-3-plan.md` | Caduc (3.D+ jeté). |
| `plans/2026-05-19-loop-phase-3-plan_AUDIT.md` | Audit du plan caduc. |
| `plans/2026-05-19-loop-phase-3-plan_AUDIT_independent.md` | Audit indépendant du plan caduc. |
| `plans/2026-05-19-loop-phase-3-exec-prompt.md` | Prompt EXEC du plan caduc. |
| `plans/2026-05-19-loop-phase-3-session-manifest.md` | Instance Phase 3 caduque. La discipline durable reste dans `docs/superpowers/SESSION_PROTOCOL.md`. |

### §16.3 À supprimer

- `docs/superpowers/HANDOFF-2026-05-20-loop-phase-3-spec-rework.md` — consommé par cette spec. Les concepts (catégorisation B1/B2/B3/C1/C2 et règles R1-R6) sont intégrés en §3-§4 sous la forme refondue ABSORBANT/CONTEXTUEL + règle unique.

---

## §17 Hors scope explicite

Les points suivants sont **explicitement hors scope** de cette spec. Ils seront traités séparément.

### §17.1 Defaults factory NVS

Les valeurs par défaut de chaque rôle quand le NVS est vide (factory) sont **différées**. La spec retient :
- Les defaults présents dans le code actuel sont conservés tels quels (PL/S ARPEG = pad 23, REC/PL/S/CLR LOOP = 30/31/32 — défauts hérités du plan Phase 3 caduc).
- Les modificateurs ARPEG (Root, Mode, Chromatic, Octave) gardent leurs defaults existants dans `ScalePadStore`.
- Les Slots LOOP : vide par default (aucun pad assigné).
- Les ControlPads : vide par default.

Une fois la première config musicien validée par du jeu live, Loïc fournira un set de defaults stabilisés que le plan d'impl intégrera en hardcoded constants.

### §17.2 fonction_regen.md — sémantique HOLD différée

Le fichier `docs/reference/fonction_regen.md` décrit la fonction REGEN (ARPEG_GEN) avec des branches sémantiques HOLD ON / HOLD OFF qui ne reflètent plus le code (fix F1 du 2026-05-15 a supprimé la branche « fingers down → wipe pile »). Une refonte de ce doc est **différée hors de cette session**. Le rename `holdPad` → `arpPlayStopPad` y créera des inconsistances de label transitoires — le doc sera audité et refondu dans une session ultérieure dédiée.

### §17.3 Refonte ToolControlPads.cpp comme module séparé ou fusionné

La décision architecturale exacte — `ToolControlPads.cpp` conservé comme « engine CC » consommé par la page CC, ou fusionné dans le nouveau module Tool PAD ROLE — relève du plan d'impl, pas de cette spec.

### §17.4 SHIFT+TAB reverse cycle

La spec retient TAB unidirectionnel (BANK → ARPEG → LOOP → CC → BANK). L'ajout d'un SHIFT+TAB pour cycle reverse est différé — le coût UX d'un cycle long (3 TABs pour revenir d'un cran) est acceptable au vu de la fréquence d'usage du setup mode (boot-only, occasionnel).

### §17.5 Renommages identifiants legacy autres que holdPad

Le projet contient d'autres identifiants legacy (notamment `leftHeld` pour SHIFT button). Aucun renommage autre que ceux de §13 n'est mandaté par cette spec.

### §17.6 Détails de plan d'implémentation

Cette spec décrit le **quoi** (concept, classes, règles, pages, comportement, affichage, palette, modale, renommages). Le **comment-coder** (sites précis, signatures, ordre d'application, hard-asserts, etc.) relève du plan d'implémentation (`writing-plans`, session suivante).

---

## §18 Sources et cross-refs

### §18.1 Refs projet utilisées pour rédiger cette spec

- [`docs/reference/setup-tools-conventions.md`](../reference/setup-tools-conventions.md) — conventions tools setup, raccourcis, patterns nav/edit/commit.
- [`docs/reference/vt100-design-guide.md`](../reference/vt100-design-guide.md) — palette, cellules, frames, cockpit primitives.
- [`docs/reference/arp-reference.md`](../reference/arp-reference.md) — sémantique runtime PL/S ARPEG, fix F1 du 2026-05-15.
- [`docs/reference/nvs-reference.md`](../reference/nvs-reference.md) — schemas stores, conventions versioning.
- [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](2026-04-19-loop-mode-design.md) — spec parent LOOP §5 catégorisation refondue.
- [`docs/superpowers/HANDOFF-2026-05-20-loop-phase-3-spec-rework.md`](../HANDOFF-2026-05-20-loop-phase-3-spec-rework.md) — concepts intermédiaires (A/B/C, R1-R6) consommés par cette spec.
- [`src/arp/ArpEngine.cpp:514-545`](../../src/arp/ArpEngine.cpp:514) — implémentation actuelle de `setCaptured` (commentaire « pile sacrée Q3 spec gesture §13, fix F1 du 2026-05-15 »).

### §18.2 Refs projet à mettre à jour suite à cette spec

Cf §16.1.

### §18.3 Specs et plans archivés par cette spec

Cf §16.2.

---

**Spec validée 2026-05-23**. Source-of-truth Tool PAD ROLE. Prochaine étape : plan d'implémentation, session dédiée.
