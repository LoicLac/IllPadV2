# La fonction génératif (ARPEG_GEN) — manuel musicien

> Tu veux comprendre comment ça sonne, pas comment c'est codé. Ce doc
> décrit les paramètres et le comportement live, vu depuis le clavier.
> Pour les détails internes : [arp-reference.md §13](arp-reference.md).

---

## Esprit

Une bank `ARPEG_GEN` est un arpégiateur **génératif** — au lieu de répéter
un motif fixe à partir des notes que tu joues, l'instrument compose une
séquence qui **évolue**.

Tu joues des notes (la « pile »), et l'instrument utilise ces notes comme
*matière première* pour fabriquer une mélodie qui dérive, mute, varie. Tu
ne joues pas les notes une par une — tu nourris une **boucle vivante**.

La séquence n'est pas pré-écrite : elle est tirée au hasard pondéré, dans
les limites que tu fixes (range vertical, taille des sauts, attachement
aux notes de la pile). Le hasard est encadré — c'est de la marche, pas du
chaos.

---

## Tagger une bank en ARPEG_GEN

Dans Setup (Tool 5) :

1. Naviguer sur la bank voulue (↑↓).
2. `ENTER` pour éditer.
3. `←/→` sur le champ TYPE — focus brackets `[ARPEG_GEN ─ Imm]` ou
   `[ARPEG_GEN ─ Beat]`.
4. `↑/↓` cycle le type : NORMAL → ARPEG-Imm → ARPEG-Beat → **ARPEG_GEN-Imm** →
   ARPEG_GEN-Beat → NORMAL.
5. `ENTER` pour sauvegarder. Reboot pour appliquer.

Max 4 banks ARP cumulé (ARPEG + ARPEG_GEN), cap matériel.

---

## Les 6 paramètres Tool 5 (réglés une fois, persistés)

Une fois la bank tagged ARPEG_GEN, l'édition (`ENTER`) cycle sur 6 champs
via `←/→` :

```
TYPE → GROUP → BONUS pile → MARGIN → PROX → ECART → TYPE
```

`↑/↓` ajuste la valeur du champ focalisé. Tenir `←/→` ou `↑/↓` accélère
le mouvement (gros pas).

### 1. TYPE
Le type de bank (cf section précédente).

### 2. GROUP (scale group)
`-`, `A`, `B`, `C`, `D`. Les banks dans le même group partagent les
changements de scale (root / mode / chromatique) — quand tu changes la
scale sur une bank du group, toutes les autres suivent. `-` = bank
indépendante.

### 3. BONUS pile (range 1.0..2.0, default 1.5)

> « Quand la mélodie s'écarte, est-ce qu'elle revient vers tes notes
> ou est-ce qu'elle erre librement dans la gamme ? »

Multiplicateur de poids favorisant les **degrés de ta pile** quand
l'engine choisit le prochain step.

- `1.0` : pile et gamme alentour traités à égalité. Mélodie qui dérive.
- `1.5` (default) : équilibre. Pile attractive mais pas dominante.
- `2.0` : pile dominante. La mélodie revient toujours vers tes notes.

À pousser haut si tu veux entendre clairement tes pads. À baisser si tu
veux que l'instrument prenne le large.

### 4. MARGIN (range 3..12, default 7)

> « Combien la mélodie peut s'éloigner verticalement de ta pile ? »

Détermine la fenêtre verticale du walk autour de la pile :

```
walk_min = note la plus basse de la pile  −  MARGIN
walk_max = note la plus haute de la pile  +  MARGIN
```

L'unité est le **degré de la gamme** (1 = une note de la gamme courante,
ou 1 demi-ton en chromatique).

- `3` : walk serré. La mélodie reste collée au range de ta pile.
- `7` (default) : ~une octave de dérive haut et bas.
- `12` : ~deux octaves de dérive. La mélodie peut traverser des registres.

Petite MARGIN + grand BONUS = mélodie sur place qui mâche tes notes.
Grande MARGIN + petit BONUS = mélodie qui s'évade.

### 5. PROX (proximity factor, range 0.4..2.0, default 0.4)

> « La mélodie marche à petits pas ou saute par grands intervalles ? »

Pente du falloff exponentiel qui pondère le choix du prochain degré
selon sa distance au précédent. Le walk préfère les degrés proches —
PROX règle à quel point cette préférence est marquée.

- `0.4` (default) : falloff brutal. Le walk préfère fortement les steps
  adjacents. Lignes mélodiques *step-wise*, conjointes.
- `1.0` : intermédiaire. Sauts modérés possibles.
- `2.0` : falloff doux. Le walk peut prendre de grands intervalles aussi
  facilement que de petits. Mélodies erratiques, bondissantes.

PROX bas pour des phrases chantantes, PROX haut pour de l'angulaire.

### 6. ECART (range 1..12, default 5)

> « Quelle est la taille MAX d'un saut ? »

Limite dure : aucun step ne peut être à plus de ECART degrés du
précédent. C'est un plafond, indépendamment de PROX (qui pondère la
préférence à l'intérieur de cette limite).

- `1` : mélodie strictement chromatique (chaque step est adjacent au
  précédent).
- `5` (default) : sauts jusqu'à 5 degrés. Quintes possibles.
- `12` : sauts jusqu'à 12 degrés. Octaves complètes.

ECART petit + PROX bas = ligne mélodique douce et conjointe.
ECART grand + PROX haut = chaos contrôlé.

> **Important** : ECART remplace complètement l'ecart qui était piloté
> par R2+hold dans les versions précédentes. R2+hold ne pilote plus que
> la **longueur** de la séquence (cf section suivante).

---

## Les contrôles live (en jeu, hors setup)

Une fois la bank ARPEG_GEN sélectionnée en runtime :

### Pads — la pile vivante

Tes pads alimentent la **pile** (pool de degrés que l'engine peut tirer).

- **Press** : ajoute le pad à la pile.
- **Release** : retire le pad (HOLD OFF) ou aucun effet (HOLD ON).
- **Double-tap** (HOLD ON) : retire le pad de la pile.

Le **hold pad** (configuré dans Tool 4) bascule HOLD OFF ↔ HOLD ON.

**Important** : un seul pad pressé = mélodie qui répète cette note.
Plusieurs pads = walk qui pioche entre eux.

### R2 + hold-left — longueur de la séquence

Maintiens le bouton hold-left + tourne le pot R2. Le pot balaye 8 zones
discrètes (avec hystérésis pour éviter le flicker aux frontières) :

| Zone | seqLen | Caractère |
|---|---|---|
| 0 | 2 | Micro-pattern, 2 steps |
| 1 | 3 | Triolet |
| 2 | 4 | Demi-mesure |
| 3 | 8 | Une mesure de 1/8 |
| 4 | 12 | 1,5 mesure |
| 5 | 16 | Deux mesures |
| 6 | 32 | Quatre mesures |
| 7 | 64 | Phrase longue |

Quand tu **augmentes** la longueur, les nouveaux steps sont générés via
walk continu depuis le dernier step existant (pas de rupture audible —
la mélodie se prolonge naturellement).

Quand tu **diminues**, la séquence est tronquée — les steps au-delà de
la nouvelle longueur ne sont plus joués, mais ils restent en mémoire et
ressortent si tu remontes.

### Pad oct 1-4 — vitesse de mutation

Les 4 pads d'octave (configurés dans Tool 3) **n'ont pas la même
sémantique** sur ARPEG_GEN que sur ARPEG classique :

| Pad oct | Mode | Effet sur la séquence |
|---|---|---|
| 1 | **LOCK** | Fige la séquence. Aucune mutation. La mélodie courante boucle telle quelle. |
| 2 | 1/16 | Mutation lente. ~1 step ré-écrit toutes les 16 steps joués. Évolution lente. |
| 3 | 1/8 | Mutation moyenne. Évolution audible mais progressive. |
| 4 | 1/4 | Mutation rapide. La mélodie se transforme nettement. |

À chaque mutation, un index aléatoire de la séquence est ré-écrit via le
walk pondéré (PROX + BONUS pile + MARGIN + ECART). Si tu as ajouté des
notes à la pile depuis la génération initiale, ces nouvelles notes ont
de fortes chances d'apparaître via mutation.

### Hold pad (HOLD ON ↔ HOLD OFF)

Configuré dans Tool 3. Bascule entre :

- **HOLD OFF** : pile = doigts en contact. Tu lâches, la note sort de la
  pile. Mode « joue tant que tu touches ».
- **HOLD ON** : pile = mémoire. Tu lâches, la note reste. Double-tap
  pour retirer. Mode « accumule un pool puis lâche les mains ».

### Hold-left + double-tap bank pad — Play/Stop

Maintiens hold-left + tape le pad de bank deux fois rapidement. Toggle
Play / Stop sur cette bank.

- **Stop** : la séquence se vide, la pile reste préservée (état « paused
  pile »).
- **Play** : reprise. Si la pile était préservée, la séquence
  redémarre.

### Scale / root pads

Changer la scale ou le root transpose **automatiquement** la séquence
en cours. La grille des degrés est conservée — c'est juste la conversion
degré → MIDI qui s'adapte à la nouvelle scale. La mélodie se déplace,
mais sa forme reste reconnaissable.

---

## Scénarios live — comment ça se comporte

### Scénario 1 — démarrer une boucle

Tu sélectionnes une bank ARPEG_GEN-Imm. Pad oct 1 (LOCK). Tu presses
un seul pad.

- L'instrument détecte pile 0 → 1, génère une séquence (longueur par
  défaut zone 3 = 8 steps).
- La séquence n'a qu'un degré à dispo → elle répète cette note.
- Tu entends la note pulser. Lock figé.

Tu presses 2 autres pads pendant que ça tourne.

- Mécanisme `Option B` : 3 mutations forcées immédiates intègrent ces
  notes dans la séquence. Tu entends ces nouvelles notes apparaître
  audiblement dans les ~3 prochains tours de boucle.
- La pile contient maintenant 3 degrés. Le walk pioche entre eux. La
  ligne mélodique s'enrichit.

### Scénario 2 — passer en mutation continue

Toujours sur la même bank. Tu passes pad oct 1 → pad oct 3 (1/8).

- À partir de maintenant, ~1 step sur 8 est ré-écrit à chaque tour de
  boucle.
- La mélodie commence à dériver. Elle reste reconnaissable mais évolue.
- Si tu ajoutes une note à la pile, elle s'infiltre progressivement
  (Option B + mutation continue).

Tu pousses pad oct 4 (1/4). Mutation rapide. La mélodie change
clairement à chaque boucle.

Tu reviens pad oct 1 (LOCK). La séquence se fige sur son état courant
— *capture* du moment musical.

### Scénario 3 — allonger la séquence

Boucle qui tourne, seqLen = 8. Tu maintiens hold-left + R2 vers la
droite jusqu'à zone 5 (seqLen 16).

- Les 8 nouveaux steps sont générés via walk continu depuis le step 7.
- Tu entends la mélodie se prolonger sans rupture.

R2 jusqu'à zone 7 (seqLen 64). Phrase très longue. La mélodie a le
temps de respirer, de varier sans se répéter mécaniquement.

R2 retour vers zone 3 (seqLen 8). Tronquature. La mélodie redevient
courte, mais quand tu remontes R2 plus tard, les anciens steps 8-63
ressortent (en mémoire).

### Scénario 4 — changer la scale en pleine boucle

Bank ARPEG_GEN qui joue en C Ionien. Tu changes le root vers D, ou la
mode vers Aeolien.

- La séquence ne change pas de forme — les *degrés* stockés sont
  conservés.
- La conversion degré → MIDI s'adapte à la nouvelle scale.
- Tu entends la même phrase transposée. La forme reconnaissable, la
  hauteur déplacée.

Si tu changes la scale **pendant LOCK** : la séquence figée se
transpose, reste figée. Lock garantit que la mélodie courante survit
au changement de scale.

### Scénario 5 — combiner ARPEG_GEN background + ARPEG classique foreground

Tu as bank 5 = ARPEG classique (pattern Up, division 1/8) et bank 6 =
ARPEG_GEN-Beat.

- Bank 5 en foreground : tu joues 3 pads, ils s'arpègent en Up.
- Bank 6 en background : son engine continue de tourner — la séquence
  ARPEG_GEN joue silencieusement (pas envoyée tant que la bank n'est
  pas foreground), mais ses mutations continuent.
- Tu switches en bank 6 : tu retrouves la séquence GEN exactement où
  elle en était, pas un re-démarrage.

### Scénario 6 — créer un drone évolutif

Bank ARPEG_GEN. BONUS = 2.0 (max). MARGIN = 3 (min). PROX = 0.4 (min).
ECART = 1 (min). Pad oct 4 (mutation rapide). seqLen = 64. Tu presses
3 pads très proches.

- Mélodie strictement chromatique conjointe (ECART=1).
- Range serré autour de la pile (MARGIN=3, BONUS=2.0).
- Falloff brutal (PROX=0.4) → walk *step-wise*.
- Mais mutations 1/4 → ça mute en permanence.
- Résultat : un drone qui ondule sur place, jamais identique, toujours
  cohérent.

Maintenant tu pousses MARGIN à 12, ECART à 12, PROX à 2.0 sur la même
bank (via Tool 5, donc reboot puis re-sélection) : la même pile
produit une mélodie qui erre sur deux octaves avec de grands sauts.
Même matière, comportement opposé.

---

## Combinaisons utiles à explorer

| Effet recherché | BONUS | MARGIN | PROX | ECART | Pad oct |
|---|---|---|---|---|---|
| Drone évolutif sur place | 2.0 | 3 | 0.4 | 1 | 4 (1/4) |
| Ligne chantante stable | 1.5 | 5 | 0.4 | 3 | 2 (1/16) |
| Mélodie qui prend le large | 1.0 | 12 | 1.0 | 8 | 3 (1/8) |
| Pulse hypnotique | 2.0 | 3 | 0.4 | 1 | 1 (LOCK) |
| Chaos contrôlé | 1.0 | 12 | 2.0 | 12 | 4 (1/4) |
| Phrase longue méditative | 1.5 | 7 | 0.4 | 4 | 2 (1/16), seqLen 64 |

---

## Limites et garde-fous

- **Max 4 banks ARP** (cumul ARPEG + ARPEG_GEN). Tool 5 affiche le compteur
  `N/4 ARP` et bloque le 5e tagging avec un message rouge.
- **Pile max 48 notes** (= nombre de pads). Au-delà, les nouvelles
  presses sont ignorées.
- **Aucune note orpheline** garantie : changement de scale, switch de
  bank, panic — la séquence ne laisse jamais de note coincée.
- **NVS Zero Migration Policy** : si tu changes la version du firmware
  et que la struct `BankTypeStore` a évolué (cf [nvs-reference.md](nvs-reference.md)),
  le boot affiche un warning `BankTypeStore absent/invalide` et applique
  les defaults usine. Tu re-règles tes 6 params via Tool 5. C'est rapide.

---

## Defaults compile-time (post-reset usine)

| Param | Default |
|---|---|
| TYPE | NORMAL (banks 1-4) / ARPEG-Imm (banks 5-8) |
| GROUP | A pour banks 1,2,5,6 ; - pour 3,4,7,8 |
| BONUS pile | 1.5 |
| MARGIN | 7 |
| PROX | 0.4 |
| ECART | 5 |
| Pad oct | 1 (LOCK) au démarrage de chaque bank |
| Longueur initiale (R2+hold) | zone 3 = seqLen 8 |

Reset via `[d]` dans Tool 5 : remet tout aux defaults usine.

---

## Pour les détails techniques

- Architecture interne (pickNextDegree, seedSequenceGen, maybeMutate) :
  [arp-reference.md §13](arp-reference.md)
- Structure NVS BankTypeStore v4 + ArpPotStore v1 :
  [nvs-reference.md](nvs-reference.md)
- Spec de design complète : [`docs/superpowers/specs/2026-04-25-arpeg-gen-design.md`](../superpowers/specs/2026-04-25-arpeg-gen-design.md)
- Plan d'implémentation (archivé) : [`docs/archive/2026-04-26-arpeg-gen-plan.md`](../archive/2026-04-26-arpeg-gen-plan.md)
