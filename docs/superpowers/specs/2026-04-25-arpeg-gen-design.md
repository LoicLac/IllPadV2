# ILLPAD48 V2 — Mode ARPEG_GEN : design haut niveau

**Date** : 2026-04-25 — **révisé 2026-04-26** post-audit (10 décisions tranchées : B1, B2, B4, B5, I1, I3, I4, I6, I8, I9).
**Statut** : **VALIDÉ** pour plan d'implémentation. Pré-requis : aucun (pas de dépendance Phase 0/0.1 non remplie).
**Scope** : ajout d'un troisième type de bank (`ARPEG_GEN`), réduction du jeu de patterns du mode `ARPEG` classique, restauration de la sémantique pad oct = octaves littérales en classic. Haut niveau, sans code. Plan d'implémentation par phase à rédiger après validation.
**Sources** :
- État du code `main` au 2026-04-25 (commits `f3a138b` et antérieurs).
- [`docs/reference/arp-reference.md`](../../reference/arp-reference.md) — partiellement obsolète, voir §2 ci-dessous.
- [`docs/reference/vt100-design-guide.md`](../../reference/vt100-design-guide.md) — source de vérité pour Tool 5.
- [`src/setup/ToolBankConfig.cpp`](../../../src/setup/ToolBankConfig.cpp), [`src/arp/ArpEngine.cpp`](../../../src/arp/ArpEngine.cpp), [`src/midi/ScaleResolver.cpp`](../../../src/midi/ScaleResolver.cpp), [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h).
- Conversation de design 2026-04-25 (cette session).

---

## Partie 1 — Cadre

### §1 — Intention musicale

Le mode `ARPEG` actuel offre 15 patterns d'arpégiateur (UP, DOWN, RANDOM, OCTAVE_BOUNCE, PROBABILITY, etc.) sélectionnables au pot R2+hold. Plusieurs problèmes :

1. **Surface de contrôle saturée** : 15 patterns ne tiennent pas confortablement sur les 5 zones discrètes du R2+hold actuel.
2. **Octaves dénaturées** : la fonction `effectiveOctaveRange()` impose des planchers à plusieurs patterns (CHORD ≥ 4, PROBABILITY ≥ 4, OCTAVE_BOUNCE ≥ 3...) qui empêchent l'utilisateur d'obtenir le geste fondamental « je joue trois notes, j'entends trois notes ».
3. **Ergonomie binaire** : les patterns simples (UP, DOWN) et les patterns complexes (PROBABILITY, OCTAVE_BOUNCE) cohabitent sans hiérarchie. L'utilisateur passe d'un comportement très déterministe à un comportement quasi-aléatoire d'une zone à l'autre du pot, sans transition.

Le mode `ARPEG_GEN` introduit une seconde famille d'arpégiateur : un moteur génératif à pile vivante, contrôlé par un continuum unique de 15 positions allant de la boucle courte et serrée à la mélodie générative longue et exploratrice. Les paramètres techniques (longueur de cycle, étendue d'intervalles) sont encodés dans la position du pot ; le taux de mutation est exposé sur les pads oct, jouable en live.

L'utilisateur configure chaque bank au boot (Tool 5) entre `NORMAL`, `ARPEG` (classique réduit à 6 patterns essentiels) et `ARPEG_GEN` (génératif). Les deux types `ARPEG*` partagent le même pool de 4 engines (`MAX_ARP_BANKS = 4`).

### §2 — État actuel et docs obsolètes

Audit du code et des docs au 2026-04-25 :

- [`docs/reference/arp-reference.md`](../../reference/arp-reference.md) §6 liste 5 patterns. Le code définit 15 entrées (`ArpPattern` enum dans `KeyboardData.h:407-424`). **Doc obsolète sur ce point**.
- Le même doc §4 mentionne un mode `ArpStartMode::Bar`. Le code définit uniquement `ARP_START_IMMEDIATE` et `ARP_START_BEAT` (`HardwareConfig.h:266-269`). **Doc obsolète sur ce point**.
- Le code ne propose pas `BANK_ARPEG_GEN`. L'enum `BankType` (`KeyboardData.h:312-316`) contient `BANK_NORMAL=0`, `BANK_ARPEG=1`, et un sentinel `BANK_ANY=0xFF`.
- `BankTypeStore` (`KeyboardData.h:491-499`, version 2) stocke `types`, `quantize`, `scaleGroup` par bank. Pas de champ pour les paramètres génératifs.
- `Tool 5` (interne `Tool 4`, fichier [`ToolBankConfig.cpp`](../../../src/setup/ToolBankConfig.cpp)) gère le cycle plat NORMAL → ARPEG-Imm → ARPEG-Beat → NORMAL via ←→, et le cycle Group via ↑↓ en edit mode.

Ce document spécifie les modifications à apporter au code et aux refs après implémentation. La mise à jour de [`arp-reference.md`](../../reference/arp-reference.md) (correction patterns, ajout section ARPEG_GEN) fait partie du livrable d'implémentation.

### §3 — Périmètre

**Ce document couvre** :
- Le nouveau type de bank `BANK_ARPEG_GEN` et son intégration au modèle existant.
- L'algorithme génératif : pile vivante, walk, mutation, bornes, scale.
- La grille de 15 positions du pot R2+hold en mode généatif.
- La sémantique des pads oct dans chaque mode.
- La réduction du jeu de patterns en mode `ARPEG` classique (15 → 6) et la restauration des octaves littérales.
- Les modifications à `Tool 5` (nouveau type, sous-champs ARPEG_GEN, layout 2 lignes, edit mode field-focus).
- Les modifications NVS (`BankTypeStore` v3).
- Le routing des pots et le geste hold-left.
- Les triggers de reset, le comportement face aux changements de scale/root, les edge cases.

**Ce document ne contient pas** :
- De code, de signatures de fonction, de pseudocode d'implémentation.
- Le découpage par phase d'implémentation.
- Les valeurs exactes des constantes magiques NVS (déléguées au plan).
- La spécification du LED feedback (aucun changement requis : pas de signalisation visuelle DET vs GEN).

---

## Partie 2 — Architecture

### §4 — Trois types de bank, deux moteurs ARPEG

L'enum `BankType` est étendue d'une troisième valeur :

```
BANK_NORMAL    = 0
BANK_ARPEG     = 1  // ARPEG classique (renommage interne possible : BANK_ARPEG_CLASSIC)
BANK_ARPEG_GEN = 2  // ARPEG génératif (nouveau)
BANK_ANY       = 0xFF  // sentinel PotRouter, inchangé
```

Les deux types `ARPEG*` partagent le même pool d'engines `s_arpEngines[MAX_ARP_BANKS = 4]`. La somme des banks `ARPEG` + `ARPEG_GEN` est plafonnée à 4 (Tool 5 refuse au-delà, message `[!] Max 4 ARP banks!`).

L'engine `ArpEngine` interne distingue les deux modes via un champ runtime `_engineMode` (CLASSIC | GENERATIVE) pour router la logique de tick :
- En mode `CLASSIC`, `tick()` exécute la logique existante restreinte aux 6 patterns conservés (cf. §6).
- En mode `GENERATIVE`, `tick()` exécute le walk avec mutation (cf. §11).

`_engineMode` est **dérivé runtime de `slot.type`**, jamais stocké séparément en NVS. La source de vérité unique est `BankTypeStore.types[i]`. `setEngineMode(slot.type)` est appelé aux mêmes points que `setScaleConfig` : init au boot après `loadAll`, et au save Tool 5 (post-`saveConfig`). Pas de risque de désync.

**Code sites impactés.** L'affirmation « la distinction est invisible pour les autres systèmes » est **fausse** côté code. Tous les sites qui filtrent par `slot.type == BANK_ARPEG` doivent être étendus pour traiter ARPEG_GEN comme un cas ARP (helper recommandé : `inline bool isArpType(BankType t) { return t == BANK_ARPEG || t == BANK_ARPEG_GEN; }` dans `KeyboardData.h`). Sites recensés au 2026-04-25 (à mettre à jour dans le plan d'implémentation) :

| Fichier | Lignes | Comportement à étendre |
|---|---|---|
| `src/main.cpp` | 128, 352, 371, 562, 584, 599, 641, 654, 685, 703 | Engine instanciation, registration scheduler, hold pad, pushParamsToEngine |
| `src/managers/BankManager.cpp` | 85, 207 | LEFT+double-tap Play/Stop, label affiché |
| `src/core/LedController.cpp` | 351, 502 | Bargraph color, render bank LED |
| `src/managers/NvsManager.cpp` | 266, 618 | Pot debounce ARPEG, defaults boot |
| `src/managers/ScaleManager.cpp` | 181 | Octave pads (routing différent par type, cf. §33) |
| `src/managers/PotRouter.cpp` | 176 | Building bindings (ctx ARPEG s'applique aussi à ARPEG_GEN) |
| `src/setup/ToolLedSettings.cpp` | 940 | Preview color slot |
| `src/setup/ToolBankConfig.cpp` | multiple | Cycle de type, validation arpCount, layout (§22), edit mode (§23) |

Le `BankSlot` lui-même (struct dans `KeyboardData.h:327-336`) ne change pas — seuls les call-sites qui consomment `slot.type`.

### §5 — Routing pots et hold-left

Le routing pot reste **identique entre tous les modes ARPEG** sauf la sémantique du geste R2+hold et celle des pads oct.

| Pot / Geste | Cible | Classic | Generative |
|---|---|---|---|
| R1 alone | tempo BPM | identique | identique |
| R1 + hold | division (9 valeurs : 4/1 → 1/64) | identique | identique |
| R2 alone | gate length (continu 0–1+) | identique | identique |
| **R2 + hold** | structure | `TARGET_PATTERN` : sélection pattern (6 valeurs) | `TARGET_GEN_POSITION` : position grille (15 valeurs) |
| R3 alone | shuffle depth (continu 0–1) | identique | identique |
| R3 + hold | shuffle template (10 valeurs) | identique | identique |
| R4 alone | base velocity (1–127) | identique | identique |
| R4 + hold | velocity variation (0–100 %) | identique | identique |
| **Pad oct 1–4** | dimension principale | octaves littérales (1, 2, 3, 4) | mutation rate (0, 1/16, 1/8, 1/4) |

**Deux PotTargets distincts pour le slot R2+hold.** `TARGET_PATTERN` reste 0–5 (six patterns classiques). Un nouveau `TARGET_GEN_POSITION` (range 0–14, quinze positions de la grille §13) est introduit pour ARPEG_GEN. Le `PotRouter` route le slot R2+hold sur l'un ou l'autre selon `slot.type` au moment du `rebuildBindings()` (le contexte de mapping `arpegMap` est partagé, mais la cible effective est résolue à `slot.type` près). L'engine n'a pas à interpréter une valeur ambiguë : `setPattern(ArpPattern)` pour CLASSIC, `setGenPosition(uint8_t)` pour GENERATIVE.

Le bargraph snap discret (`getDiscreteSteps`) bénéficie de cette séparation : 6 zones en classic, 15 zones en generative, sans hack de remap.

**Hystérésis (nouvelle).** Aucun `PotTarget` discret existant n'a d'hystérésis aujourd'hui (le `PotRouter` quantize linéaire sur le range). L'hystérésis sur les zones est à **implémenter de zéro** pour `TARGET_GEN_POSITION` (et idéalement aussi pour `TARGET_PATTERN`, `TARGET_DIVISION`, `TARGET_SHUFFLE_TEMPLATE` par cohérence — décision plan-level). Règle : mémoriser la zone courante, ne basculer que quand l'ADC dépasse `(zone_courante ± 1.5 % × 4095)` = ±61 unités. Plan d'implémentation à détailler.

---

## Partie 3 — Mode ARPEG classique réduit

### §6 — Six patterns conservés

Le jeu actuel de 15 patterns est réduit à 6 :

| Index | Pattern | Description |
|---|---|---|
| 0 | `ARP_ORDER` | Joue les notes dans l'ordre où elles ont été ajoutées à la pile (`_positionOrder`). |
| 1 | `ARP_UP` | Joue les notes triées par hauteur, ascendant, octaves empilées. |
| 2 | `ARP_DOWN` | Comme UP, en ordre inverse. |
| 3 | `ARP_UP_DOWN` | UP puis indices descendants `[len-2 … 1]` (pas de répétition aux bornes). |
| 4 | `ARP_PEDAL_UP` | Alterne note basse de la pile et chaque autre note (basse pédale + arpège). |
| 5 | `ARP_CONVERGE` | Lecture en zigzag : note la plus basse, la plus haute, la deuxième plus basse, la deuxième plus haute, etc., jusqu'à se rejoindre au centre. |

Patterns supprimés : `ARP_RANDOM`, `ARP_CASCADE`, `ARP_DIVERGE`, `ARP_UP_OCTAVE`, `ARP_DOWN_OCTAVE`, `ARP_CHORD`, `ARP_OCTAVE_WAVE`, `ARP_OCTAVE_BOUNCE`, `ARP_PROBABILITY`. Ces patterns sont remplacés par le mode `ARPEG_GEN` qui couvre les usages génératifs avec une logique unifiée.

`NUM_ARP_PATTERNS` passe de 15 à 6.

### §7 — Octaves littérales restaurées

La fonction `effectiveOctaveRange()` est supprimée. Les pads oct 1, 2, 3, 4 contrôlent strictement le nombre d'octaves empilées dans la séquence générée : oct 1 = juste la pile, oct 2 = pile + 1 octave au-dessus, etc.

Conséquence pour les patterns conservés :
- `ARP_ORDER`, `ARP_UP`, `ARP_DOWN`, `ARP_UP_DOWN` : oct 1 produit la séquence minimale (longueur = `pile_size` pour ORDER/UP/DOWN, `2 × pile_size - 2` pour UP_DOWN). C'est le geste « j'entends ce que je joue ».
- `ARP_PEDAL_UP`, `ARP_CONVERGE` : oct 1 produit la séquence minimale du pattern, octave native uniquement.

`ARP_CHORD` (= UP + 4 octaves forcé) devient redondant et est retiré.

### §8 — Tool 5 — cycle de type étendu

Le cycle plat actuel `NORMAL → ARPEG-Imm → ARPEG-Beat → NORMAL` est étendu :

```
NORMAL → ARPEG-Imm → ARPEG-Beat → ARPEG_GEN-Imm → ARPEG_GEN-Beat → NORMAL
```

5 états sur les flèches ←→ en edit mode. La validation `arpCount >= MAX_ARP_BANKS` traite `BANK_ARPEG` et `BANK_ARPEG_GEN` ensemble.

---

## Partie 4 — Mode ARPEG_GEN : philosophie

### §9 — Pile vivante et lock implicite

Le mode `ARPEG_GEN` repose sur trois principes complémentaires :

1. **Pile vivante** : la pile (positions padOrder ajoutées par les doigts) alimente un pool de notes utilisé par le moteur génératif. Ajouter ou retirer une note de la pile **ne réinitialise pas la séquence** — la modification est intégrée graduellement par les mutations futures, ou immédiatement si la pile est vidée intégralement (cf. §17).
2. **Lock implicite via pad oct 0** : le taux de mutation est contrôlé par les pads oct (1 = 0 %, 2 = 1/16, 3 = 1/8, 4 = 1/4 du nombre de steps mutés par cycle). Pad oct 1 fige la séquence générée — elle se répète à l'identique tant que rien ne la modifie.
3. **Capture par geste** : l'utilisateur audite des mutations (pad oct 2-4), trouve un état mélodique qu'il aime, retombe sur pad oct 1 → l'état courant est figé. C'est l'équivalent du « lock » d'un Turing Machine, mais avec la pile vivante en plus comme matériel-source.

### §10 — Une séquence, deux régimes d'écriture

À tout instant, le mode `ARPEG_GEN` exécute la même boucle : il lit `_sequenceGen[step % seqLen]`, le résoud en MIDI via la scale courante, et le joue. La différence entre régimes est dans **l'écriture** de cette séquence :

- **Génération initiale** : exécutée une seule fois, à l'arrivée de la première note dans la pile (transition pile 0 → 1). Construit `seqLen` degrés en partant de la pile et en walkant strictement entre notes de pile (pas de mutation hors-pile).
- **Mutation** : exécutée à fréquence proportionnelle au taux de mutation (pad oct). Réécrit un step à la fois dans `_sequenceGen[]`, en piochant dans la pile **ou** dans la scale (selon proximité).

Ces deux régimes utilisent la même formule de pondération et les mêmes bornes ; seul le pool de candidats diffère.

---

## Partie 5 — Algorithme génératif

### §11 — Encodage de la séquence

**Deux buffers distincts dans `ArpEngine`**, sélectionnés par `_engineMode` :
- `uint8_t _sequenceClassic[MAX_ARP_SEQUENCE = 192]` — buffer existant, encode `padOrderPos + octOffset × 48`. Inchangé.
- `int8_t _sequenceGen[MAX_SEQUENCE_LEN_GEN = 96]` — **nouveau**, encode des degrés de scale signés.

Le buffer GEN ajoute 96 octets de SRAM par engine × 4 engines = 384 octets total. Conforme au budget « prefer safe over economical » de [`CLAUDE.md`](../../../.claude/CLAUDE.md). Pas de cast, pas d'union, pas de réinterprétation : chaque mode lit son propre buffer, les call-sites de tick branchent sur `_engineMode`.

**Sémantique du degré.** `_sequenceGen[i]` = degré de scale signé relatif au root courant. 0 = root. +1 = degré 1 dans la scale (par ex. 2e en major), -1 = dernier degré de la scale précédente (octave en-dessous), etc. Range pratique : ±64 degrés.

- **Mode scale 7-notes** : ±64 degrés ≈ ±9 octaves (un degré = un cran de la gamme = 1 ou 2 demi-tons selon la position).
- **Mode chromatic** : un degré = un semitone (cohérent avec `ScaleResolver::resolve` chromatic, qui fait `note = rootBase + order`). ±64 degrés = ±5.3 octaves. **L'écart §13 est donc en semitones en chromatic, alors qu'il est en degrés-de-scale en mode scale-7.** Divergence assumée : la grille pot §13 produit un grain plus fin musicalement en chromatic (écart 12 = 1 octave juste au lieu de ~1.7 octave), c'est l'effet recherché — exploration plus serrée.

Avantages de cette représentation par degrés vs MIDI absolu :
- **Transposition automatique au scale change** : tous les degrés sont remappés à de nouvelles notes MIDI uniformément. Le walk continue d'où il est, dans la nouvelle vue tonale.
- **Walk constraint en degrés naturelle** : `|Δdegree| ≤ E` est la formulation directe.
- **Pas de double encodage** : tout est dans un seul espace.

`MAX_SEQUENCE_LEN_GEN` = 96 (cap de la position 15 du pot, cf. §13). Coexiste avec `MAX_ARP_SEQUENCE = 192` (cap classic, 48 positions × 4 octaves).

### §12 — Walk : pondération de proximité

À chaque étape de génération initiale ou de mutation, le moteur :

1. Détermine le pool de candidats (cf. §14 et §15).
2. Filtre les candidats par bornes : `walk_min ≤ degree ≤ walk_max` (cf. §16).
3. Filtre par écart : `|degree(candidate) - degree(prev)| ≤ E`, où `prev` est la note précédente dans la séquence et `E` est l'écart courant (déduit de la position du pot, cf. §13).
4. Pondère chaque candidat par sa proximité :

```
w(Δ) = exp( −|Δ| / (E × 0.4) )    si |Δ| ≤ E
w(Δ) = 0                           sinon
```

- `Δ = degree(candidate) - degree(prev)`.
- À `Δ = 0` : `w = 1.0` (candidat identique fortement préféré).
- À `Δ = E` : `w ≈ 0.082` (saut maximum possible mais rare).
- Le facteur 0.4 façonne la décroissance ; à tuner empiriquement, valeur initiale codée en constante.

5. Tirage aléatoire pondéré standard (cumulative weights → random uniform → recherche dichotomique).

Cette formulation reproduit la distribution mélodique observée en analyse musicale (Huron, *Sweet Anticipation*, 2006) : intervalles courts ultra-fréquents, sauts large rares mais possibles.

Note : la pondération est **non dirigée** (sur `|Δ|`, pas sur `Δ` signé). Pas de mémoire de direction, pas de tendance ascendante/descendante héritée. La continuité émerge de la contrainte d'écart, pas d'un état directionnel.

### §13 — Grille du pot R2+hold

15 positions discrètes, encodant chacune un couple (longueur de séquence, écart maximal). Les deux dimensions croissent monotonement (ou stagnent) avec la position. Plus la position est haute, plus la séquence est longue **et** plus les sauts sont permis — donc plus le pattern est génératif au sens « éloigné d'une boucle déterministe ».

| Pos | Steps | Écart |
|---|---|---|
| 1 | 8 | 1 |
| 2 | 8 | 2 |
| 3 | 16 | 2 |
| 4 | 16 | 3 |
| 5 | 24 | 3 |
| 6 | 24 | 4 |
| 7 | 32 | 4 |
| 8 | 32 | 5 |
| 9 | 48 | 5 |
| 10 | 48 | 6 |
| 11 | 64 | 6 |
| 12 | 64 | 7 |
| 13 | 96 | 8 |
| 14 | 96 | 10 |
| 15 | 96 | 12 |

Cap volontaire à 96 steps (≈ 6 mesures à 1/16 de noire). Au-delà, l'oreille perd la mémoire du retour mélodique. Pas de position « 192 pré-généré » — la limite haute est musicalement inopérante.

Defaults compile-time :
- `_position` = pos 5 (24 steps, écart 3) à l'instanciation d'un bank ARPEG_GEN.
- L'utilisateur peut tourner R2+hold dès qu'il joue.

### §14 — Génération initiale

Déclenchée à la transition pile 0 → 1 note (cf. §17 pour la liste exhaustive des triggers de regen). Construit `seqLen` valeurs (`seqLen` est lu depuis la position courante du pot, cf. §13).

**Pool de candidats** : exclusivement les notes de la pile, exprimées en degrés de scale. Calcul : pour chaque pad de la pile, `ScaleResolver::resolve(pad, padOrder, scale) → MIDI → degree(scale)`. Le tableau `_pileDegrees[MAX_ARP_NOTES]` est précalculé et invalidé sur `addPadPosition`, `removePadPosition`, ou changement de scale/root.

**Construction** :
1. `_sequenceGen[0]` : tirage uniforme dans la pile (un seul candidat = single-note pile, sinon 1 entre tous).
2. Pour `i = 1 … seqLen-1` : pool = pile, application des filtres et pondération de §12 avec `prev = _sequenceGen[i-1]`. Si le pool filtré est vide (cas pathologique, cf. fallback ci-dessous), `_sequenceGen[i] = _sequenceGen[i-1]` (répétition).

**Écart effectif en génération initiale** : pour garantir que toute la pile soit représentée dans le seed (et éviter qu'une pile {0, 4, 7} avec écart pot=1 produise une séquence dégénérée d'une seule note répétée), l'écart appliqué pendant la génération initiale est :

```
E_init = max(E_pot, pile_hi - pile_lo)
```

Ainsi toute note de la pile est toujours atteignable depuis n'importe quelle autre, dès la génération initiale. La mutation (§15) respecte strictement `E_pot` — c'est uniquement le seed qui est élargi pour représenter le matériau joué.

Pas de pondération `bonus_pile` dans cette phase — toutes les notes du pool sont déjà dans la pile.

### §15 — Mutation

Exécutée pendant que la séquence joue. La fréquence de mutation est gouvernée par le taux courant (pad oct) :

| Pad oct | Taux | Sens concret |
|---|---|---|
| 1 | 0 (lock) | aucune mutation, séquence figée |
| 2 | 1/16 | 1 mutation tous les 16 steps |
| 3 | 1/8 | 1 mutation tous les 8 steps |
| 4 | 1/4 | 1 mutation tous les 4 steps |

Le compteur de step global (modulo `seqLen`) déclenche la mutation. À l'instant de la mutation :

1. **Choix de l'index à muter** : tirage uniforme dans `[0, seqLen-1]`. Pas de stratégie particulière (ni séquentielle, ni en cluster).
2. **Détermination de `prev`** : `_sequenceGen[(index - 1 + seqLen) % seqLen]`. Le voisin gauche dans la boucle.
3. **Pool de candidats** : pile (degrés) ∪ scale_within_window. La fenêtre scale est `[degree(prev) - E, degree(prev) + E]` filtrée par appartenance à la scale courante (les notes hors-scale sont exclues d'office, sauf en mode chromatic où toutes les notes MIDI sont des degrés de scale). **L'ancienne valeur `_sequenceGen[index]` est incluse dans le pool** (pas d'exclusion explicite) : avec la pondération exponentielle, les valeurs de pile proches de prev sont fortement préférées, ce qui crée une « inertie naturelle » musicalement souhaitable (mutation pas toujours = changement violent garanti).
4. **Filtres** : bornes (cf. §16), écart `|Δ| ≤ E` (déjà implicite dans le calcul de la fenêtre scale, mais redondant pour la pile).
5. **Pondération** : `w(Δ) = exp(−|Δ|/(E×0.4))`. Pour les candidats appartenant à la pile, `w *= bonus_pile` (multiplicateur configurable per-bank, défaut 1.5, range 1.0–2.0).
6. **Sélection** : tirage pondéré → `_sequenceGen[index]` reçoit la nouvelle valeur (potentiellement identique à l'ancienne — c'est intentionnel).

**Conséquence du `bonus_pile`** : même en mode mutation, la pile garde un poids privilégié. À `bonus_pile = 1.0`, les notes hors-pile ont la même chance que les notes pile (proximité étant le seul biais). À `bonus_pile = 2.0`, la pile est doublement préférée — la séquence reste fortement ancrée dans le matériel joué.

**Conséquence sur la pile vivante** : ajouter une note à la pile l'inscrit dans le pool. Elle n'apparaît dans `_sequenceGen[]` qu'à la première mutation qui la pioche. Avec `pad oct = 1` (lock), elle n'apparaît jamais — l'utilisateur peut alimenter le pool sans perturber la boucle figée.

### §16 — Bornes du walk

Le walk est borné par :

```
walk_min = pile_lo − margin
walk_max = pile_hi + margin
```

où `pile_lo` et `pile_hi` sont les degrés minimum et maximum de la pile courante, et `margin` est un paramètre per-bank (défaut 7 degrés, range 3–12).

Tout candidat hors `[walk_min, walk_max]` est éliminé du pool **avant** la pondération. Cela empêche le walk de dériver indéfiniment vers les extrêmes au fil de centaines de mutations.

**Comportement quand la pile change** :
- Ajout de note → bornes étendues automatiquement.
- Retrait de note → bornes rétrécies. Les valeurs existantes de `_sequenceGen[]` qui se retrouvent hors-bornes ne sont **pas** clampées rétroactivement ; elles continuent à jouer telles quelles. La séquence se ramène graduellement par mutation.

**Cas dégénéré : lock pur + pile rétrécie.** Si `pad oct = 1` (aucune mutation) et que la pile rétrécit, les valeurs hors-bornes restent **indéfiniment** — rien ne les ramène. C'est le comportement attendu : le lock préserve exactement ce qui a été verrouillé, même si la pile change sous lui (logique Turing Machine). Pour ramener la séquence dans les nouvelles bornes, l'utilisateur doit débloquer transitoirement (pad oct 2-4) ou vider intégralement la pile (régen complète).

**Pile = 1 note** : `walk_min = walk_max ± margin`, fenêtre symétrique autour de cette note. Avec pad oct 1, séquence = répétition de cette note. Avec pad oct 2-4, mutations explorent dans `±margin` degrés.

### §17 — Triggers de reset

La séquence générative est **entièrement régénérée** uniquement aux triggers suivants :

- **Pile 0 → 1 note** : premier seed après pile vidée.

Liste exhaustive des **non-triggers** (la séquence continue inchangée) :
- Bank switch (foreground ↔ background). Les engines ARPEG_GEN background continuent de tourner.
- Play/Stop (toggle via hold pad ou LEFT + double-tap).
- Ajout ou retrait de note dans une pile non-vide.
- Changement de scale ou de root (cf. §18).
- Changement de la position du pot R2+hold (cf. §19).
- Changement de pad oct (cf. §20).

La sémantique « reset = pile vide » est délibérément stricte. C'est le seul moment où l'utilisateur perd ce qu'il a construit. Si l'utilisateur veut « redémarrer » sans tout perdre, il peut figer (pad oct 1) puis manipuler la pile/pot pour reconstruire.

### §18 — Scale et root change mid-play

La séquence stockant des degrés (cf. §11) :

- **Root change** : tous les degrés résolvent à de nouvelles notes MIDI uniformément décalées. Transposition automatique de la séquence en cours.
- **Scale type change** (Ionian → Aeolian, etc.) : mêmes degrés, intervalles différents → morphing modal. La silhouette mélodique est préservée, la couleur change.
- **Scale length change** (en cas d'ajout futur de scales non-7-notes) : si nouvelle longueur < ancienne, certains degrés dépassent. Clamping doux : `degree = clamp(degree, -length+1, +length-1)` côté positif/négatif. Préserve l'enveloppe générale, perd quelques détails.
- **Pile bornes recomputées** : sur tout changement de scale ou root, `pile_lo`/`pile_hi` sont recalculés (les degrés des notes de pile dépendent de la scale).

**Continuité du walk au moment du change** : aucune. Le degré stocké en `_sequenceGen[i+1]` est déjà absolu en termes de degré scale courante, donc il joue tel quel dans la nouvelle scale. Pas de recalcul de séquence.

**Pending events** : conservent les notes MIDI résolues au moment où ils ont été schedulés (invariant existant, cf. [`runtime-flows.md`](../../reference/runtime-flows.md)). La note qui sonne au moment du scale change termine sur sa hauteur d'origine ; la note suivante sort dans la nouvelle scale.

### §19 — Pot move pendant lock (pad oct 1)

Quand l'utilisateur tourne R2+hold sans aucune mutation active (pad oct 1), la séquence figée existante est **étendue ou tronquée** plutôt que regénérée :

- **Pot tourné vers position supérieure** (longueur croît) : les `seqLen_old` premiers steps existants sont conservés. Les `seqLen_new − seqLen_old` steps additionnels sont générés selon le nouveau couple (longueur, écart) — pool = pile, walk depuis `prev = _sequenceGen[seqLen_old - 1]`. Si `prev` est hors-bornes courantes (cas où la pile a rétréci, cf. §16), `prev` est **clampé à `[walk_min, walk_max]`** pour amorcer le walk d'extension — opération locale qui ne modifie pas le buffer en place. La séquence résultante = ancienne séquence (intacte, valeurs hors-bornes incluses) + queue nouvelle dans-bornes.
- **Pot tourné vers position inférieure** (longueur décroît) : la séquence est tronquée à `seqLen_new`. Les steps au-delà sont perdus définitivement — c'est le principe d'**extension fugace** : ce qui dépasse la longueur courante est jeté, jamais conservé en mémoire fantôme.

**Edge cases** :
- **Pile vide au moment du pot move** : pas de regen, longueur reste `seqLen_old`. La séquence figée joue jusqu'à ce que l'utilisateur réinjecte une pile (qui déclenche regen complète si pile était à 0, cf. §17).
- **Moves rapides successifs** (par ex. user balaie le pot à travers plusieurs positions en 1 seconde) : pas de debounce. Chaque move déclenche son extension/troncature synchrone. Le coût est négligeable (génération ≤ 96 steps × tirage pondéré) et le user voit la longueur évoluer en live, c'est l'intention.

Si la **longueur ne change pas** mais l'écart change (cas pos 1 ↔ pos 2 par exemple, both 8 steps mais écart différent) : la séquence existante est conservée intacte. Le nouvel écart s'applique uniquement aux **mutations futures**. Cela évite de réécrire une séquence figée juste pour ajuster un caractère qui n'agit qu'en mutation.

### §20 — Pad oct change

Pad oct change ne modifie jamais `_sequenceGen[]`. Il modifie uniquement le **taux de mutation appliqué aux steps futurs** :

- Pad oct 1 ← n'importe : aucune mutation à venir. Lock effectif.
- Pad oct n (n ∈ {2, 3, 4}) ← oct 1 : reprise de mutation au taux n. La séquence courante peut commencer à muter au prochain step déclencheur.
- Pad oct n → oct 1 : capture de l'état courant. Tout mutation future inhibée.

Pas de mémoire de séquence « pre-mutation » : si l'utilisateur va de oct 1 → oct 4 (séquence se transforme), revient à oct 1 (capture), puis veut « annuler » en remontant oct 4, la nouvelle phase de mutation part de l'état capturé, pas de l'état initial.

---

## Partie 6 — Tool 5 — UI

### §21 — Types des champs et conventions

Le `BankTypeStore` v3 accumule les champs suivants (au-delà de v2) :

| Champ | Range | Encodage | Visibilité Tool 5 |
|---|---|---|---|
| `types[NUM_BANKS]` | 0–2 | `BankType` enum (était 0–1) | toujours |
| `quantize[NUM_BANKS]` | 0–1 | `ArpStartMode` enum, inchangé | si type = ARPEG ou ARPEG_GEN |
| `scaleGroup[NUM_BANKS]` | 0–4 | inchangé | toujours |
| `bonusPilex10[NUM_BANKS]` | 10–20 | `uint8_t`, divisé par 10 (1.0–2.0 par pas 0.1) | si type = ARPEG_GEN |
| `marginWalk[NUM_BANKS]` | 3–12 | `uint8_t`, lecture directe | si type = ARPEG_GEN |

Pour les banks non-ARPEG_GEN, `bonusPilex10` et `marginWalk` sont stockés mais ignorés (préservation à travers les changements de type, cf. §23).

### §22 — Layout des banks

Une bank consomme une ligne (NORMAL, ARPEG) ou deux lignes (ARPEG_GEN) dans la section `BANKS`.

**NORMAL** (1 ligne) :
```
  Bank 4    NORMAL                                    Group: A
```

**ARPEG** (1 ligne) :
```
  Bank 4    ARPEG       Quantize: Beat                Group: A
```

**ARPEG_GEN** (2 lignes) :
```
  Bank 4    ARPEG_GEN   Quantize: Beat                Group: A
            Bonus pile: 1.5    Margin: 7
```

La deuxième ligne est rendue en `VT_DIM` hors edit, en `VT_CYAN` (sous-champ focalisé) en edit. L'indicateur de cursor (`> ` cyan-bold ou `  ` dim) est rendu uniquement sur la première ligne.

Avec 4 banks ARPEG_GEN possibles (cap MAX_ARP_BANKS), la section consomme au maximum 8 + 4 = 12 lignes. Confortable dans 50 rows (cf. [`vt100-design-guide.md`](../../reference/vt100-design-guide.md) §1.1).

### §23 — Edit mode

Le comportement d'edit dépend du type courant de la bank focalisée.

**NORMAL ou ARPEG** — comportement existant préservé :
- ←→ : cycle type+quantize sur 5 états (cf. §8). Le cycle inclut les 2 nouveaux états ARPEG_GEN.
- ↑↓ : cycle scale group (-, A, B, C, D).
- Enter : save.
- q : cancel (restore depuis snapshot).

**ARPEG_GEN** — modèle field-focus, nouveau (convention §4.4 « geometric visual navigation » alignée Tool 8) :
- 4 sous-champs : `TYPE`, `GROUP`, `BONUS_PILE`, `MARGIN_WALK`.
- **`←→` : focus** entre sous-champs (loop : MARGIN → TYPE → GROUP → BONUS → MARGIN). Suit le layout visuel de la grille 2 lignes × 2 colonnes.
- **`↑↓` : ajuste la valeur** du sous-champ focalisé. Pour `TYPE`, ↑↓ cycle les 5 états (NORMAL → ARPEG-Imm → ARPEG-Beat → ARPEG_GEN-Imm → ARPEG_GEN-Beat → NORMAL). Pour `GROUP`, ↑↓ cycle les valeurs (-, A, B, C, D). Pour `BONUS_PILE` et `MARGIN_WALK`, ↑↓ ajuste la valeur numérique (`accelerated` = ×10).
- Enter : save.
- q : cancel.

Le sous-champ focalisé est rendu en `VT_CYAN VT_BOLD "[value]"` (convention existante, ToolBankConfig.cpp:306). L'indicateur `> ` cyan-bold reste sur la ligne 1 (bank entry), peu importe le sous-champ focalisé (ligne 1 ou ligne 2).

**Transition entre modèles** : si l'utilisateur est en edit ARPEG_GEN sur le sous-champ `BONUS_PILE`, et change le type vers `NORMAL` via ↑↓ sur `TYPE`, les sous-champs `BONUS_PILE` et `MARGIN_WALK` disparaissent (ligne 2 disparaît). Le cursor saute vers `TYPE` (toujours présent). Le modèle d'edit bascule en compact 1-ligne. Inversement, passer de NORMAL/ARPEG vers ARPEG_GEN affiche la ligne 2, cursor reste sur `TYPE`.

**NORMAL et ARPEG — comportement existant aligné §4.4** : ←→ focus entre `TYPE` et `GROUP` (2 sous-champs sur la même ligne). ↑↓ ajuste : sur `TYPE`, cycle les 5 états ; sur `GROUP`, cycle (-, A, B, C, D). Cela diffère légèrement du comportement actuel (où ←→ cycle TYPE et ↑↓ cycle GROUP en parallèle, sans focus), mais s'aligne sur la convention §4.4 désormais générale aux 3 types.

**Defaults au passage vers ARPEG_GEN** : si la bank n'a jamais été ARPEG_GEN (valeurs NVS non encore écrites), `bonusPilex10 = 15` et `marginWalk = 7`. Sinon, les valeurs précédemment réglées sont restaurées (préservation à travers type switches en cours d'édition).

**Reset `[d]` defaults** : banks 1-4 NORMAL, 5-8 ARPEG, immediate, group A pour 1/2/5/6 (inchangé). Pas de création automatique de bank ARPEG_GEN au reset. **`bonusPilex10` et `marginWalk` sont remis à 15 et 7 pour TOUTES les banks**, quel que soit leur type, par cohérence avec la philosophie « reset = état usine connu et reproductible » — pas de valeur résiduelle invisible.

### §24 — Header counter et messages

Le compteur en haut à droite passe de `N/4 ARPEG` à `N/4 ARP` — il agrège ARPEG + ARPEG_GEN. La dénomination « ARP » est retenue parce qu'elle ne masque pas la distinction (visible dans la liste des banks elle-même).

Message d'erreur sur tentative de dépassement : `[!] Max 4 ARP banks!` (générique, plutôt que « ARPEG » qui était spécifique). Cohérent avec le cap `MAX_ARP_BANKS = 4` qui couvre les deux familles.

### §25 — INFO panel

`drawDescription(cursor, type)` étendue pour traiter le 3e cas :

- **NORMAL** : description existante.
- **ARPEG** : description existante.
- **ARPEG_GEN** : nouveau bloc :
  ```
  Bank 4   --  ARPEG_GEN  --  MIDI channel 4
  Generative arpeggiator. Pile feeds the walk; pad oct 1-4 = mutation rate.
  R2+hold = grid position (15 zones, length+ecart). Pad oct 1 = lock.
  Pad oct change locks/unlocks; pile change feeds pool without resetting.
  ```

Lors de l'edit d'un sous-champ ARPEG_GEN, une description supplémentaire du sous-champ apparaît, cohérente avec la convention « INFO follows cursor » du design guide ([`vt100-design-guide.md`](../../reference/vt100-design-guide.md) §1.4) :

- `TYPE` : description existante par cible du cycle.
- `GROUP` : description existante.
- `BONUS_PILE` :
  ```
  [BONUS PILE]  Probability multiplier for in-pile candidates during mutation.
                1.0 = neutral. 2.0 = strong pile gravity. Default 1.5.
  ```
- `MARGIN_WALK` :
  ```
  [MARGIN WALK]  Walk bounds extension above and below pile range, in scale degrees.
                 3 = tight around pile. 12 = up to ~2 octaves drift. Default 7.
  ```

### §26 — Control bar

Trois variantes selon contexte :

| Contexte | Bar |
|---|---|
| Hors edit | `[^v] NAV  │  [RET] EDIT  [d] DFLT  │  [q] EXIT` (existant) |
| Edit, NORMAL ou ARPEG | `[</>] TYPE  [^v] GROUP  │  [RET] SAVE  │  [q] CANCEL` (existant) |
| Edit, ARPEG_GEN | `[</>] VALUE  [^v] FIELD  │  [RET] SAVE  │  [q] CANCEL` (nouveau) |

---

## Partie 7 — Persistance NVS

### §27 — BankTypeStore v3

Bump version : `BANKTYPE_VERSION = 3`. Conformément à la **NVS Zero Migration Policy** ([`CLAUDE.md`](../../../.claude/CLAUDE.md)), aucun code de migration n'est ajouté. Les données NVS v2 existantes échouent à `loadBlob()` (mismatch version) et sont rejetées silencieusement → defaults compile-time appliqués → `Serial.printf` d'avertissement au boot.

L'utilisateur re-saisit ses préférences via Tool 5. Cap < 2 minutes par bank.

Layout v3 (vs v2) :
- En tête : `magic`, `version`, `reserved` — inchangés (`uint16_t + uint8_t + uint8_t = 4 octets`).
- `types[NUM_BANKS]` : 8 octets, valeurs étendues 0–2.
- `quantize[NUM_BANKS]` : 8 octets, inchangé.
- `scaleGroup[NUM_BANKS]` : 8 octets, inchangé.
- `bonusPilex10[NUM_BANKS]` : 8 octets, **nouveau**.
- `marginWalk[NUM_BANKS]` : 8 octets, **nouveau**.

Total v3 : 4 + 8 × 5 = 44 octets. Cap NVS_BLOB_MAX_SIZE = 128. Marge confortable.

`validateBankTypeStore` (KeyboardData.h:610) est étendu :
- Si `s.types[i] > BANK_ARPEG_GEN` (= 2), reset à `BANK_NORMAL`.
- Compteur `arpCount` cumule `BANK_ARPEG` + `BANK_ARPEG_GEN`. Si dépasse `MAX_ARP_BANKS`, reset à `BANK_NORMAL`.
- `bonusPilex10[i]` clampé à `[10, 20]`. Hors range → 15.
- `marginWalk[i]` clampé à `[3, 12]`. Hors range → 7.

### §28 — Pas d'autre Store affecté

`ArpPotStore` (`KeyboardData.h:117-124`) reste inchangé. Le mode ARPEG_GEN ne stocke pas séparément les valeurs des pots — elles sont déjà capturées par le pot router existant.

`ScalePadStore`, `ArpPadStore`, `SettingsStore`, `PotMappingStore`, `LedSettingsStore`, `ColorSlotStore` : aucune modification.

---

## Partie 8 — Interactions

### §29 — Clock, division, quantize start

Le mode ARPEG_GEN obéit aux mêmes règles que le classique :
- `ArpScheduler::tick()` accumule selon la division courante (R1+hold).
- `_quantizeMode` (Immediate ou Beat) gouverne le démarrage : la séquence ne fire son premier step qu'au prochain boundary correspondant.
- Stop est toujours immédiat (pas de quantize sur stop).

Aucune modification du `ClockManager`, `ArpScheduler`, ou des invariants de timing existants.

### §30 — Shuffle, gate, velocity

Tous les paramètres « live » fonctionnent identiquement :
- Shuffle (R3 alone + R3+hold) : appliqué aux noteOn schedulés, identique au classic. La séquence générative joue avec ou sans groove sans modification du moteur.
- Gate (R2 alone) : applique au step durée fractionnelle. La logique noteOn/noteOff est identique.
- Velocity (R4 alone + R4+hold) : appliquée à chaque step, via `_baseVelocity ± _velocityVariation`.

### §31 — MIDI, refcount, pile vivante

Toutes les sorties MIDI passent par `refCountNoteOn` / `refCountNoteOff` (invariant P1 du [`patterns-catalog.md`](../../reference/patterns-catalog.md)). Pas de bypass. Pas de risque d'overlap kill malgré shuffle + boucle longue.

L'event queue (P3) reste à `MAX_PENDING_EVENTS = 64` par engine. Avec un shuffle extrême et une séquence longue (96 steps), au pire 2 events par step (noteOn + noteOff retardé), soit potentiellement 192 events théoriques en flight. En pratique, la majorité des events sont firés avant le suivant : 64 reste suffisant. À monitorer au test hardware.

### §32 — LED feedback

**Aucune modification de design LED requise.** La signalisation par couleur DET/GEN évoquée en cours de design est **abandonnée** — pas de pattern LED pour distinguer les modes en live. La distinction est faite au moment de la config en Tool 5.

Les patterns LED ARPEG existants (PULSE_SLOW pour stopped-loaded, FLASH sur tick, SOLID pour playing) s'appliquent identiquement aux deux modes ARPEG. Le `tickFlash` (`ArpEngine::consumeTickFlash`) est levé à chaque step exécuté, peu importe l'origine (classic ou generative).

**Code sites à étendre** (pas de design change, mais les conditions actuelles filtrent par `BANK_ARPEG` strict — doivent inclure `BANK_ARPEG_GEN`) :
- `src/core/LedController.cpp:351` — bargraph color (color_arpeg pour ARP* foreground).
- `src/core/LedController.cpp:502` — switch render bank, `case BANK_ARPEG_GEN:` route vers `renderBankArpeg(...)` (même fonction).
- `src/setup/ToolLedSettings.cpp:940` — preview color slot ARPEG s'applique aussi à ARPEG_GEN.

Conformément au helper `isArpType(BankType)` recommandé en §4.

### §33 — ScaleManager et octave pads

En mode `ARPEG_GEN`, la gestion des octave pads par `ScaleManager` (`ScaleManager.cpp:180-200`) doit être adaptée. Actuellement, ces pads appellent `slot.arpEngine->setOctaveRange(o + 1)` quand le bank est `BANK_ARPEG`. Pour `BANK_ARPEG_GEN`, l'appel devient `slot.arpEngine->setMutationLevel(o + 1)` (ou nom équivalent — détail d'API à finaliser au plan d'implémentation).

Les 4 pads physiques sont les mêmes ; seule la signification interne change. L'utilisateur ne reconfigure pas les pads dans Tool 3 — ils restent les « pads octave » par convention nominale.

---

## Partie 9 — Edge cases

### §34 — Pile = 1 note

- `pile_lo = pile_hi`, donc `walk_min = pile_hi - margin`, `walk_max = pile_hi + margin`.
- Génération initiale : `_sequenceGen[0]` = la note unique. Pour `i ≥ 1`, pool filtré = la même note (seule candidate dans la pile). Séquence = répétition de la note.
- En mutation (pad oct 2-4) : pool inclut la pile (1 note) + scale notes dans la fenêtre. Mutations introduisent progressivement des notes voisines hors-pile. Avec `bonus_pile = 1.5`, la note de pile reste re-piochée régulièrement.
- En lock (pad oct 1) : séquence fige sur N copies de la même note. Banal mais correct.

### §35 — Pile vide → pile 1 note rapide

Si l'utilisateur appuie sur 1 pad très brièvement (release immédiate) avant que la séquence ne fasse même un step, la séquence est générée et purgée. Sur la prochaine pression, regénération propre.

Pas de protection particulière contre le pile flicker rapide. La régénération est cheap (≤ 96 steps × tirage pondéré) — coût négligeable même en cas de regen multiple.

### §36 — Pile vidée pendant mutation active

L'utilisateur retire la dernière note de la pile alors que la séquence joue avec mutations. Comportement attendu :
- L'engine passe à `IDLE` immédiatement (`_positionCount == 0`).
- `flushPendingNoteOffs()` est invoqué : tous les noteOffs encore en queue sont firés, refcount sweep, `_playing = false`.
- `_sequenceGen[]` est marqué dirty (regen au prochain seed).

C'est le seul reset-of-séquence (cf. §17).

### §37 — Bornes restrictives au point de bloquer le walk

Cas dégénéré : `margin = 3`, pile très étroite (1 note), écart = 12 sur position pot 15. Le pool filtré peut être quasi-vide (12 degrés de portée mais seule la zone `±3` autour de la pile est valide).

Comportement : si le pool filtré est vide (cas pathologique en mutation, hors génération initiale qui a son propre élargissement §14), `_sequenceGen[i] = _sequenceGen[i-1]` (répétition fallback). Pas d'erreur, pas de silence — la séquence reste continue.

### §38 — Changement de pot pendant mutation active

L'utilisateur tourne R2+hold pendant que `pad oct = 4` (mutation maximale). Comportement :
- Si la longueur change : la séquence est tronquée (raccourcie) ou étendue (queue générée selon nouveau couple). Cf. §19.
- Si l'écart change uniquement : nouvelles mutations utilisent le nouvel écart, séquence existante préservée.

Le scénario combinant changement de longueur **et** mutation active est cohérent : l'extension générée respecte le nouvel écart ; les mutations futures utilisent le nouvel écart aussi.

---

## Partie 10 — Hors scope et questions ouvertes

### §39 — Hors scope v1

- **Mémoire des séquences capturées** : pas de stockage des séquences figées en NVS. Reboot = perte de toute séquence générée. C'est cohérent avec la pile vivante et l'absence de file system.
- **Export MIDI File des séquences générées** : non.
- **Re-injection d'une note hors-bornes après retrait de pile** : pas de logique de relocalisation forcée. La note hors-bornes joue jusqu'à ce qu'une mutation la remplace.
- **Direction inertia (momentum)** : le walk reste non-dirigé (cf. §12). Pas de mémoire de direction, pas de tendance ascendante.
- **Pondération bonus_pile dans la génération initiale** : non appliquée (cf. §14). La pondération s'applique uniquement en mutation.
- **Probabilité de saut octave dédiée** : pas de paramètre séparé. Les sauts d'octave émergent naturellement quand `E ≥ 7` (en scale 7-notes).
- **Profil personnalisable du shape de proximité** : le facteur `0.4` dans `exp(−|Δ|/(E×0.4))` est codé en dur. Pas exposé en Tool 5. À tuner empiriquement, modifiable par compile-time si besoin.

### §40 — Questions ouvertes pour test hardware

À résoudre au moment du test physique, sans réviser ce document. Valeurs initiales codées en compile-time, tuning empirique post-implémentation par commit séparé (cf. CLAUDE.md « objet unique, pas prototype » — le tuning est une passe acceptée, pas un état permanent).

1. **Valeur exacte du facteur `0.4`** dans la formule de proximité. Trop large = peu de différence entre Δ=0 et Δ=E ; trop étroit = quasi-déterministe (toujours Δ=0). Tuner par écoute.
2. **Defaults `bonus_pile = 1.5` et `margin = 7`** : à valider sur instrument live avec différentes piles.
3. **Taux de mutation maximal `1/4`** au pad oct 4 : éventuellement à pousser à `1/2` (chaos rapide) si le ressenti est trop tame. Décision lors du test, modifiable sans rupture utilisateur.
4. **Hystérésis sur 15 zones du R2+hold** : seuils stables ±1.5 % ou ±2 % du débattement. **Feature à implémenter** (pas un pattern existant — cf. §5 dernière paragraphe). Plan d'implémentation à détailler le mécanisme.
5. **Latence perçue** entre changement de pad oct et apparition de la première mutation : si trop longue (jusqu'à 16 steps × ~125 ms à 120 BPM = 2 s en pad oct 2), étudier un déclenchement immédiat de la première mutation post-change plutôt qu'attendre le prochain step déclencheur.

### §41 — Évolutions possibles post-v1

Mentionnées comme idées notées, sans engagement :

- **Probabilité de saut octave dédiée** comme paramètre per-bank (Tool 5) : `octave_jump_chance` (0–100 %) qui force occasionnellement un saut de ±7 degrés (en scale 7-notes) hors fenêtre normale. Ajouterait du relief.
- **Variation du shape de proximité par position du pot** : positions hautes pourraient avoir un shape plus plat (sauts plus fréquents). Substituerait à un seul `0.4`.
- **Direction inertia optionnelle** : un paramètre per-bank `direction_bias` ∈ [−1, +1] qui biaise la pondération vers Δ positif ou négatif. Génère des walks ascendants ou descendants par périodes.

Aucune de ces extensions n'est requise pour v1.

---

## Partie 11 — Récapitulatif des décisions

Pour relecture rapide :

| Décision | Valeur |
|---|---|
| Trois types de bank | NORMAL, ARPEG (classique), ARPEG_GEN (génératif) |
| Pool engine partagé | 4 max, ARPEG + ARPEG_GEN cumulés |
| ARPEG classic patterns | 6 : ORDER, UP, DOWN, UP_DOWN, PEDAL_UP, CONVERGE |
| ARPEG classic pad oct | octaves littérales 1–4, sans plancher imposé |
| ARPEG_GEN pad oct | mutation rate 0, 1/16, 1/8, 1/4 |
| ARPEG_GEN R2+hold | 15 positions (longueur, écart) |
| Encodage séquence | int8_t degré de scale signé |
| Walk algorithme | exponentielle, w(Δ) = exp(−|Δ|/(E×0.4)) |
| Pool initial | pile uniquement |
| Pool mutation | pile ∪ scale dans fenêtre, bonus pile pondéré |
| Bornes | walk_min/max = pile_lo/hi ± margin |
| Bonus pile | per-bank, range 1.0–2.0, default 1.5 |
| Margin | per-bank, range 3–12 degrés, default 7 |
| Reset trigger | exclusivement pile 0 → 1 note |
| Pile vivante | semantic (d) — ajout entre dans pool, séquence non interrompue |
| Lock | pad oct 1 = no mutation = lock effectif |
| Pot move at lock | extension/troncature fugace, pas regen |
| Écart change at fixed length | s'applique aux mutations futures, séquence préservée |
| Scale change | transposition automatique (degrés invariants) |
| Tool 5 cycle type | NORMAL → ARPEG-Imm → ARPEG-Beat → ARPEG_GEN-Imm → ARPEG_GEN-Beat → NORMAL |
| Tool 5 layout ARPEG_GEN | 2 lignes par bank |
| Tool 5 edit mode (tous types) | convention §4.4 alignée Tool 8 : ←→ focus, ↑↓ adjust |
| Tool 5 edit ARPEG_GEN | field-focus 4 sous-champs (TYPE, GROUP, BONUS_PILE, MARGIN_WALK) |
| Reset `[d]` defaults | banks 1-4 NORMAL, 5-8 ARPEG ; bonusPilex10/marginWalk = 15/7 pour TOUTES les banks |
| NVS | BankTypeStore v3, +16 octets, zero migration |
| Encoding séquence | 2 buffers distincts : `_sequenceClassic[192]` (uint8_t) + `_sequenceGen[96]` (int8_t) |
| `_engineMode` | runtime, dérivé de `slot.type`, jamais NVS |
| PotTarget GEN | nouveau `TARGET_GEN_POSITION` distinct de `TARGET_PATTERN` |
| Hystérésis pot | feature nouvelle, à implémenter (pas pattern existant) |
| Sémantique chromatic | écart en semitones (vs degrés-de-scale en mode scale-7), divergence assumée |
| Génération initiale | écart effectif = max(E_pot, pile_hi - pile_lo) pour représenter toute la pile |
| Mutation pool | inclut le step actuel (inertie naturelle) |
| Pot move at lock pile vide | no-op, longueur préservée |
| Walk extension prev hors-bornes | clamp local du prev seul, séquence existante intacte |
| LED | aucun design change, mais sites code à étendre (§32) |
| Code sites BANK_ARPEG | étendre via `isArpType(BankType)` helper (§4) |
| MIDI / refcount / clock / shuffle | inchangés |

---

**Validation finale par Loïc requise avant rédaction du plan d'implémentation.**
