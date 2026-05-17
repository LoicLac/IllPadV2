# Tool 5 Bank Config — Refacto design (pré-LOOP Phase 3)

**Date** : 2026-05-17
**Statut** : **VALIDÉ** pour rédaction plan d'implémentation
**Scope** : refacto complet du Tool 5 setup mode (`src/setup/ToolBankConfig.{h,cpp}`). Vue tableau matriciel banks × params, nav 2D, INFO auto-update, code modulaire. À exécuter **avant Phase 3 LOOP** pour intégrer LOOP élégamment.
**Sources** :
- État du code `main` au commit `dfe8747`.
- Audit Tool 5 sur main (10 findings A-J — voir §2).
- Spec LOOP [`2026-04-19-loop-mode-design.md`](2026-04-19-loop-mode-design.md) §6 + §28 Q6 (refactor évoqué, alors deferred — on défait ce defer).
- Convention VT100 ILLPAD (cf Tool 3 PAD ROLES screenshot — tags cyan `[GRID]/[POOL]/[INFO]`, bank chips bleus, INFO body orange/jaune, control bar VT_DIM).

---

## Partie 1 — Cadre

### §1 — Pourquoi ce refacto maintenant

Le Tool 5 actuel est techniquement fonctionnel (ARPEG_GEN feature complete) mais **structurellement non-extensible**. L'audit (§2) montre que le coût d'ajout d'un type ou d'un param est linéaire dans 5 dimensions : working arrays, switch cases, NvsManager pairs, drawDescription cases, layout lines.

Greffer LOOP (type +1, quantize 3-way Free/Beat/Bar, futurs effets defaults) sur cette structure produit une usine à gaz. La spec LOOP §6 footnote l'avait déjà signalé ; Q6 §28 disait "Phase 3 minimal, refactor deferred". **On rembourse cette dette maintenant**.

### §2 — Audit Tool 5 actuel (synthèse)

10 findings sur `src/setup/ToolBankConfig.cpp` (712 lignes) :

| # | Finding | Impact |
|---|---|---|
| A | `run()` monolithique 608 lignes, 14 variables locales | extension touche 5-7 endroits éparpillés |
| B | Cycle type 5-state mélange 2 axes orthogonaux (type + quantize couplés en cycle linéaire) | non-scalable ; LOOP = cycle 8 états ; pas de NORMAL → ARPEG_GEN direct |
| C | Sub-field cycle hétérogène (6 fields ARPEG_GEN vs 2 fields NORMAL/ARPEG) | branchements conditionnels, workaround saut vers TYPE quand on quitte ARPEG_GEN |
| D | Layout multi-lignes par bank inconsistant (1 ligne NORMAL/ARPEG, 3 lignes ARPEG_GEN) | hauteur variable selon config, dépasse VT100 utile |
| E | `drawDescription` 4 cas hard-codés (NORMAL/ARPEG/ARPEG_GEN/LOOP placeholder) | +1 cas par type ajouté |
| F | Snapshot/restore prolifère (7 saved arrays + 7 memcpy snapshot + 7 memcpy cancel) | tax linéaire à chaque ajout |
| G | `saveConfig` signature 7 params pointeurs | insoutenable, pas de struct working-copy unifié |
| H | Pas de séparation type ↔ params (giant switch sur `cursorSubField`) | pas de modularité |
| I | Validations dispersées (Tool 5 runtime + KeyboardData.h validators) | risque divergence règles métier |
| J | 7 paires get/set `Loaded*` dans NvsManager | bag de getters/setters indexés par bank |

### §3 — Principes directeurs du redesign

1. **Un seul tableau matriciel** banks × params : vue d'ensemble + zone d'édition unifiée. Banks en **colonnes**, params en **lignes**.
2. **Type et Quantize comme 2 lignes distinctes** : les 2 axes orthogonaux sont décorrélés visuellement et fonctionnellement.
3. **Nav 2D spatiale** : `^v` navigue les params (vertical), `<>` navigue les banks (horizontal). Mouvement = direction visuelle.
4. **INFO auto-update sur chaque mouvement** (pas seulement à l'édit) : le musicien voit immédiatement la description du cell focus et son statut (éditable ou non).
5. **Différenciation visuelle éditable / non-éditable** : cells non-applicables au type de la bank sont DIM avec sentinel `·` ; cells éditables ont la couleur sémantique de leur catégorie.
6. **Sections inline** par catégorie de params (`─ TYPE ─` `─ SCALE ─` `─ WALK ─`) pour structurer sans bouffer de lignes — la ligne de séparation du tableau porte le label de section.
7. **Code modulaire** : working-copy struct unifiée, table déclarative des params (catégorie, applicabilité par type, range, label, default), rendering pure-function. Ajouter un param = 1 entrée dans la table déclarative.

---

## Partie 2 — Design UX

### §4 — Layout général

```
┌─[ ILLPAD48 SETUP CONSOLE ]────[ TOOL 5: BANK CONFIG ]──────────[ NVS:OK ]─┐
│                                                                           │
│ [BANKS]                                                                   │
│              ┌──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┐    │
│              │ Bk1  │ Bk2  │ Bk3  │ Bk4  │ Bk5  │ Bk6  │ Bk7  │ Bk8  │    │
│   ─ TYPE ────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┤    │
│   Type       │ NORM │ NORM │ LOOP │ ARPG │ AGEN │ ARPG │ NORM │ NORM │    │
│   Quantize   │  ·   │  ·   │ Bar  │ Beat │ Beat │ Imm  │  ·   │  ·   │    │
│   ─ SCALE ───┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┤    │
│   Group      │  ·   │  A   │  ·   │  A   │ [A]  │  B   │  ·   │  ·   │    │
│   ─ WALK ────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┤    │
│   Bonus      │  ·   │  ·   │  ·   │  ·   │ 1.5  │  ·   │  ·   │  ·   │    │
│   Margin     │  ·   │  ·   │  ·   │  ·   │  7   │  ·   │  ·   │  ·   │    │
│   Prox       │  ·   │  ·   │  ·   │  ·   │ 0.4  │  ·   │  ·   │  ·   │    │
│   Ecart      │  ·   │  ·   │  ·   │  ·   │  5   │  ·   │  ·   │  ·   │    │
│              └──────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┘    │
│                                                                           │
│ [INFO]                                                                    │
│ Bank 5 / Group                                                            │
│ Scale group A : root/mode changes propagate to all banks of group A.      │
│ Cycle : -, A, B, C, D.                                                    │
│                                                                           │
│ [^v<>] NAV   [RET] EDIT   [d] DFLT   [q] EXIT                             │
└───────────────────────────────────────────────────────────────────────────┘
```

**Décompte vertical** : ~26 lignes utilisées sur 42 disponibles → ~16 lignes de marge pour extensions futures (sections LOOP defaults Phase 5, etc.).

**Largeur tableau** : 14 chars label + 8 × 6 chars cells + 9 séparateurs verticaux = 71 chars. Plus bordures externes ≈ 73-74. Largement dans les 76 utiles (frame ~4 chars overhead).

### §5 — Cells éditables vs non-éditables

Différenciation **visuelle immédiate**, sans appui RET :

| État cell | Affichage | Couleur |
|---|---|---|
| Éditable, valeur | Texte valeur (ex. `NORM`, `Beat`, `1.5`) | Couleur sémantique de la catégorie (voir §6) |
| Non-éditable | `·` (point central) | VT_DIM gris |
| Focus éditable | `[NORM]` brackets | Cyan bold autour de la valeur (couleur catégorie préservée à l'intérieur) |
| Focus non-éditable | `[·]` brackets dim | VT_DIM brackets + dim point — pas de "tease" visuel |

→ Le musicien sait au focus s'il peut éditer. RET sur cell non-éditable = no-op silencieux (le message est déjà dans INFO).

### §6 — Code couleurs par élément

| Élément | Couleur ANSI | Référence runtime (slot) |
|---|---|---|
| Tags `[BANKS]`, `[INFO]` | Cyan bold | — (style PNG) |
| Bank headers `Bk1..Bk8` | Bleu | — (style PNG bank chips) |
| Param labels (`Type`, `Quantize`, `Group`, `Bonus`...) | Cyan | — (style PNG `Bank:`, `Root:`) |
| Section inline `─ TYPE ─` `─ SCALE ─` `─ WALK ─` | Cyan dim | — |
| Cell value `NORM` | Warm white | CSLOT_MODE_NORMAL |
| Cell value `ARPG` | Ice Blue | CSLOT_MODE_ARPEG |
| Cell value `AGEN` | Magenta | (pas de slot runtime dédié — couleur acté VT-only pour Tool 5) |
| Cell value `LOOP` | Gold | CSLOT_MODE_LOOP |
| Cell value `Imm`/`Beat`/`Bar`/`Free` | Même couleur que le type de la bank (cohérence verticale) | — |
| Cell value `A`/`B`/`C`/`D` | Amber | CSLOT_SCALE_ROOT |
| Cell value walk params (`1.5`, `7`, `0.4`, `5`) | Magenta | (cohérence avec type `AGEN`) |
| **Cell non-éditable `·`** | **VT_DIM** | — |
| Cell focus éditable `[X]` | Brackets Cyan bold | — |
| Cell focus non-éditable `[·]` | Brackets VT_DIM | — |
| INFO body | Orange/jaune | (style PNG INFO panel) |
| Control bar | VT_DIM | (style PNG control bar) |

### §7 — Comportement nav + INFO auto-update

**Nav stricte cell-par-cell** (pas de skip-non-applicable) :

| Touche (hors édition) | Action |
|---|---|
| `^` (up) | Param précédent (vertical) — wrap top → bottom |
| `v` (down) | Param suivant (vertical) — wrap bottom → top |
| `<` (left) | Bank précédente (horizontal) — wrap Bk1 → Bk8 |
| `>` (right) | Bank suivante (horizontal) — wrap Bk8 → Bk1 |
| `RET` | Entre en édition si cell éditable. No-op silencieux sinon. |
| `d` | Reset cell focus à sa valeur default (ou tout, voir §11 décisions). |
| `q` | Exit Tool. |

**INFO auto-update** : à **chaque** mouvement de curseur, l'INFO panel se rafraîchit pour décrire :
- Ligne 1 : `Bank N / ParamName` (+ ` · non applicable` si cell non-éditable)
- Lignes 2-3 : description du param + cycle / range
- Si non-éditable : ligne 3-4 explique pourquoi (type bank incompatible) et comment activer.

3 états INFO :

**État 1 — Cell focus éditable** :
```
[INFO]
Bank 5 / Group
Scale group A : root/mode changes propagate to all banks of group A.
Cycle : -, A, B, C, D.
```

**État 2 — Cell focus non-éditable** :
```
[INFO]
Bank 3 / Bonus pile · non applicable
Walk weight on pile degrees during mutation — ARPEG_GEN only.
Bank 3 type is LOOP. Change Type column to ARPEG_GEN to enable.
```

**État 3 — Cell focus éditable en édition (RET pressé)** :
```
[INFO]
Bank 5 / Group (editing)
Cycle : -, A, B, C, D.
^v adjust, RET save, q cancel.
```

### §8 — Comportement édition par cell

| Cell type | Édition `^v` | Validation |
|---|---|---|
| Type | Cycle 4 valeurs : NORMAL → ARPEG → ARPEG_GEN → LOOP → NORMAL | Refus si cap atteint (`MAX_ARP_BANKS` pour ARPEG+ARPEG_GEN combinés ; `MAX_LOOP_BANKS` pour LOOP). Sur changement de type, les params spécifiques deviennent automatiquement éditables ou non. |
| Quantize | Options dynamiques selon Type bank : NORMAL = bloqué (`·`) ; ARPEG/ARPEG_GEN = `Imm`/`Beat` ; LOOP = `Free`/`Beat`/`Bar`. | Clamp dans la range. |
| Group | Cycle `-/A/B/C/D` | Aucun blocage. |
| Bonus | `^v` ajuste `bonusPilex10` ±1 (=`±0.1`), accéléré ±5 | Clamp [10, 20] (= 1.0 à 2.0). |
| Margin | ±1, accéléré ±3 | Clamp [3, 12]. |
| Prox | `±1` (=`±0.1`), accéléré ±5 | Clamp [4, 20] (= 0.4 à 2.0). |
| Ecart | ±1, accéléré ±3 | Clamp [1, 12]. |

**RET sur cell éditable** : entre en mode édition (focus brackets passent en cyan bold + INFO change en état 3). En édition, `^v` ajuste, `RET` save, `q` cancel (restore valeur pré-édit).

**RET sur cell non-éditable** : no-op silencieux. L'INFO panel reste en état 2 (déjà affichant "non applicable").

### §9 — Sections inline du tableau

Les bordures horizontales du tableau **portent** les labels de section (`─ TYPE ─`, `─ SCALE ─`, `─ WALK ─`) dans la colonne de labels (col 0). Ça structure visuellement sans ligne dédiée.

**Sections actuelles** (post-refacto, pré-LOOP) :

| Section | Params | Applicabilité |
|---|---|---|
| TYPE | Type, Quantize | Type : tous. Quantize : ARPEG / ARPEG_GEN / LOOP. |
| SCALE | Group | NORMAL / ARPEG / ARPEG_GEN. LOOP ignore (invariant 6 spec LOOP). |
| WALK | Bonus, Margin, Prox, Ecart | ARPEG_GEN uniquement. |

**Sections futures** (Phase 5 LOOP defaults possibles) :

| Section | Params | Applicabilité |
|---|---|---|
| LOOP defaults | Chaos default, Shuffle defaults, etc. | LOOP uniquement. |

Ajouter une section = 1 entrée dans la table déclarative + N entrées params + 1 séparateur visuel. Pas de refonte UI.

---

## Partie 3 — Spec technique

### §10 — Params par type (matrice référence)

| Param | NORMAL | ARPEG | ARPEG_GEN | LOOP |
|---|---|---|---|---|
| Type | ✓ | ✓ | ✓ | ✓ |
| Quantize | · | ✓ (Imm/Beat) | ✓ (Imm/Beat) | ✓ (Free/Beat/Bar) |
| Group | ✓ | ✓ | ✓ | · (invariant 6 LOOP) |
| Bonus | · | · | ✓ | · |
| Margin | · | · | ✓ | · |
| Prox | · | · | ✓ | · |
| Ecart | · | · | ✓ | · |

`✓` = éditable, `·` = non-éditable (DIM).

### §11 — Architecture code (décomposition)

Découpage proposé du `run()` monolithique en fonctions courtes :

```
run()                        — main event loop, ~50 lignes
  loadWorkingCopy()          — NVS load + defaults seed
  drawTable()                — pure rendering du tableau
  drawInfo(cell, editing)    — pure rendering INFO panel
  handleNavigation(event)    — ^v<> hors edit
  handleEdition(event, cell) — ^v / RET / q en edit
  applyDefaults(cell)        — d
  saveAll()                  — commit NVS + update _banks
```

**Working-copy struct unifiée** (remplace 7 arrays globaux) :

```cpp
struct Tool5Working {
  BankType type[NUM_BANKS];
  uint8_t  quantize[NUM_BANKS];      // 0..1 (Imm/Beat) si ARPEG/ARPEG_GEN ; 0..2 (Free/Beat/Bar) si LOOP ; ignoré si NORMAL
  uint8_t  scaleGroup[NUM_BANKS];
  uint8_t  bonusPilex10[NUM_BANKS];
  uint8_t  marginWalk[NUM_BANKS];
  uint8_t  proximityFactorx10[NUM_BANKS];
  uint8_t  ecart[NUM_BANKS];
};

Tool5Working _wk;        // working copy (current edit state)
Tool5Working _saved;     // snapshot for cancel-edit
```

→ 1 ligne par param au lieu de 7 arrays distincts. Cancel = `_wk = _saved;` (1 assignation au lieu de 7 memcpy). **Pas de nouveau field LOOP** — `quantize[NUM_BANKS]` est réinterprété selon `type[NUM_BANKS]` (cf §16 validator contextuel).

**Table déclarative des params** :

```cpp
struct ParamDescriptor {
  const char* label;             // "Type", "Quantize", "Group", "Bonus", ...
  const char* sectionLabel;      // "TYPE", "SCALE", "WALK" — null si pas de nouvelle section
  uint8_t     (Tool5Working::*field)[NUM_BANKS];  // pointer-to-member-array
  bool        (*isApplicable)(BankType type);     // function pointer
  // Options éditables — selon le type de la bank focus (cycle dynamique).
  // getOptions(type) renvoie un descripteur (liste discrète OU plage continue).
  const ParamOptions& (*getOptions)(BankType type);
  const char* infoDescription;   // texte INFO state 1
  const char* infoNonApplicable; // texte INFO state 2 (suffixé par hint type)
};

static const ParamDescriptor PARAM_TABLE[] = {
  // section TYPE
  { "Type",      "TYPE",  &Tool5Working::type,                 alwaysApplicable, typeOptions,    ... },
  { "Quantize",  nullptr, &Tool5Working::quantize,             isArpOrLoop,      quantizeOptions, ... },
  // section SCALE
  { "Group",     "SCALE", &Tool5Working::scaleGroup,           notLoop,          groupOptions,    ... },
  // section WALK
  { "Bonus",     "WALK",  &Tool5Working::bonusPilex10,         isArpegGen,       bonusRange,      ... },
  { "Margin",    nullptr, &Tool5Working::marginWalk,           isArpegGen,       marginRange,     ... },
  { "Prox",      nullptr, &Tool5Working::proximityFactorx10,   isArpegGen,       proxRange,       ... },
  { "Ecart",     nullptr, &Tool5Working::ecart,                isArpegGen,       ecartRange,      ... },
};
```

`quantizeOptions(BankType type)` retourne `["Imm", "Beat"]` pour ARPEG/ARPEG_GEN, `["Free", "Beat", "Bar"]` pour LOOP. C'est cette indirection qui permet à la cell Quantize d'exposer 2 ou 3 valeurs selon le type de la bank focus, sans dupliquer la cell ni le field de stockage.

→ Ajouter un param = 1 ligne dans `PARAM_TABLE`. Ajouter un type avec quantize 5-way demain = étendre `quantizeOptions` (1 fonction). Pas de modification de `drawTable()` ni `handleEdition()`.

### §12 — Conventions VT100 et accessibilité

- Frame ASCII style identique aux autres Tools (corners ronds, double bordures externes du PNG).
- Headers section : tags `[BANKS]`, `[INFO]` cyan bold (style PNG).
- Largeur cell : **6 chars** (suffisant pour `NORMAL` tronqué en `NORM`, `ARPEG` → `ARPG`, `ARPEG_GEN` → `AGEN`, `1.5`, `[A]`, etc.).
- Largeur label gauche : **14 chars** (`Quantize`, `Bonus pile`...).
- Symboles : `·` pour cell non-éditable (point central, U+00B7 ou ASCII `.` centré).
- Cell focus brackets : `[` et `]` autour de la valeur, espace de padding préservé pour aligner avec cells non-focus.

### §13 — Comportement `d` (Defaults)

Deux options possibles. **Décision** : **Reset cell focus uniquement** (pas le tableau entier).

- `d` hors édition : reset le cell focus à son default (par ex. Bonus → 1.5, Group → A pour la bank, etc.). INFO confirme : `Bank 5 / Bonus : reset to default (1.5)`.
- `d` en édition : reset la valeur en cours d'édit (équivalent au cancel mais avec la default au lieu de la pré-édit).
- **Reset global** déféré : si on veut un reset full-tool, l'utilisateur passe par `q` puis re-entre dans le Tool. Évite confusion "j'ai perdu toute ma config".

### §14 — Comportement `RET` sur cells de type "Quantize" (dynamique)

La cell `Quantize` est éditable ssi `isArpType(type) || type == BANK_LOOP`. Mais ses options dépendent du type :
- ARPEG / ARPEG_GEN : `Imm` (0) / `Beat` (1). Cycle 2 valeurs.
- LOOP : `Free` (0) / `Beat` (1) / `Bar` (2). Cycle 3 valeurs.

Le storage utilise le **même field `quantize[NUM_BANKS]`** (cf BankTypeStore actuel) — le type de la bank discrimine l'interprétation. Validators clampent selon type.

INFO state 1 sur cell Quantize :
```
Bank 5 / Quantize (ARPEG_GEN)
Imm : arp starts on next clock division. Beat : arp starts on next quarter note.
Cycle : Imm, Beat.
```

```
Bank 3 / Quantize (LOOP)
Free : no quantize. Beat : action waits next 1/4. Bar : action waits next measure.
Cycle : Free, Beat, Bar.
```

INFO state 2 sur cell Quantize d'une bank NORMAL :
```
Bank 1 / Quantize · non applicable
Quantize controls arp/loop start timing — NORMAL banks have no such timing.
Bank 1 type is NORMAL. Change Type to ARPEG/ARPEG_GEN/LOOP to enable.
```

### §15 — Comportement cap (MAX_ARP_BANKS, MAX_LOOP_BANKS)

Cycle Type :
- `^v` (cycle direction) : si target = ARPEG/ARPEG_GEN et `arpCount >= MAX_ARP_BANKS` → **skip** au type suivant valide dans le cycle.
- Idem pour LOOP avec `MAX_LOOP_BANKS`.

**Décision actée** : skip (pas de refus bloquant). INFO state 1 affiche `(cap atteint, MAX_ARP_BANKS=4)` ou `(cap atteint, MAX_LOOP_BANKS=N)` quand on est au cap pour le type courant. Évite des erreurs UI persistantes ; le musicien voit pourquoi ça saute un type.

**Constantes** :
- `MAX_ARP_BANKS = 4` : déjà défini ([HardwareConfig.h](../../../src/core/HardwareConfig.h)).
- `MAX_LOOP_BANKS` : placeholder dans le refacto = **2** (cohérent spec LOOP §3). Constante à introduire dans `HardwareConfig.h` au moment du refacto Tool 5 ; valeur définitive sera reconfirmée Phase 2 LOOP selon mesure SRAM réelle de `LoopEngine`. Si Phase 2 change la valeur, c'est un bump de constante sans impact UX ni NVS.

### §16 — NvsManager : suppression des paires Loaded\* + storage `quantize` discriminé par type

**Paires Loaded\*** : le NvsManager actuel expose 7 paires `getLoadedXxx`/`setLoadedXxx` ([NvsManager.h:42-58](../../../src/managers/NvsManager.h:42)) qui sont des proxies vers des arrays internes mirrorant BankTypeStore.

**Refacto proposé** :
- Garder les arrays mirror (`_loadedQuantize`, `_loadedScaleGroup`, etc.) — utilisés par d'autres sites runtime (PotRouter, ArpEngine, etc.).
- Le Tool 5 lit directement depuis `BankTypeStore` chargé via `NvsManager::loadBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2, ...)` au démarrage du tool, et écrit via `saveBlob` au commit. La synchronisation avec les arrays mirror se fait via une seule méthode `NvsManager::syncFromBankTypeStore(const BankTypeStore&)` appelée après `saveBlob`.
- Suppression des paires `getLoadedBonusPile/setLoadedBonusPile`, etc. (laissées en place si d'autres sites les utilisent — sinon retirées en commit séparé du refacto Tool 5).

**Storage `quantize` réutilisé pour LOOP** : le field `BankTypeStore.quantize[NUM_BANKS]` est `uint8_t` et accepte 0..255 au niveau binaire. Le validator actuel clamp à `NUM_ARP_START_MODES` (=2) sans discriminer par type. Refacto du validator pour discrimination contextuelle :

```cpp
inline void validateBankTypeStore(BankTypeStore& s) {
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (s.types[i] > BANK_ARPEG_GEN && s.types[i] != BANK_ANY) s.types[i] = BANK_NORMAL;
    if (isArpType((BankType)s.types[i])) {
      if (s.quantize[i] >= NUM_ARP_START_MODES) s.quantize[i] = DEFAULT_ARP_START_MODE;  // 0..1 (Imm/Beat)
    } else if (s.types[i] == BANK_LOOP) {
      if (s.quantize[i] >= 3) s.quantize[i] = 2;  // 0..2 (Free/Beat/Bar) — default Bar
    }
    // NORMAL : quantize ignoré, pas de clamp (sera affiché `·` dans Tool 5).
    // ... autres champs ...
  }
}
```

→ **Pas de bump BankTypeStore** (reste v4). Pas de reset user. LOOP Quantize 3-way s'intègre via réinterprétation du champ existant.

---

## Partie 4 — Périmètre

### §17 — Hors scope explicite

- **Pas de changement NVS layout** : `BankTypeStore` reste v4 (mêmes fields, même size). LOOP Quantize 3-way s'intègre via discrimination contextuelle du field `quantize` existant (cf §16). Pas de reset user.
- **Pas de modification des autres Tools** : Tool 3, 4, 6, 7, 8 inchangés.
- **Pas de modification du runtime** : ArpEngine, PotRouter, BankManager inchangés (consomment toujours BankTypeStore via NvsManager).
- **LOOP côté Tool 5 inclus dans le refacto** : Type cycle 4 valeurs (NORMAL/ARPEG/ARPEG_GEN/LOOP), Quantize 3-way pour LOOP. Cohérent avec `BANK_LOOP = 2` déjà valide dans l'enum (Phase 1 close).
- **LOOP côté runtime hors scope** : `LoopEngine` n'existe pas encore (Phase 2 LOOP). Créer une bank LOOP via Tool 5 refacto produit une bank dont le runtime est no-op (`handlePadInput` default case). C'est cohérent avec l'état Phase 1 ; le runtime arrivera Phase 2 sans toucher au Tool 5.
- **`MAX_LOOP_BANKS`** : constante introduite **dans ce refacto** avec valeur placeholder 2 (§15). Phase 2 LOOP la confirme/ajuste selon mesure SRAM réelle.

### §18 — Migration NVS

Aucune. Le refacto est purement UX/code, lit/écrit le même `BankTypeStore v4` que le current. Premier boot après flash refacto Tool 5 : même defaults, mêmes valeurs persistées que pré-refacto.

Si pour une raison technique le refacto exige un bump (ex. ajout de field `loopQuantize` au store pour Phase 3 LOOP) : ce sera dans le plan Phase 3 LOOP, pas dans le plan refacto Tool 5.

### §19 — Décisions actées

| # | Question | Décision |
|---|---|---|
| D1 | Paradigme général | Tableau matriciel unique banks (colonnes) × params (lignes) |
| D2 | Nav | 2D : `^v` param, `<>` bank. Wrap au bord. Stricte cell-par-cell (pas de skip). |
| D3 | Différenciation éditable / non-éditable | Cell non-éditable = `·` VT_DIM ; éditable = valeur couleur catégorie ; focus = `[X]` brackets cyan bold (éditable) ou VT_DIM (non-éditable). |
| D4 | INFO panel | Auto-update à chaque mouvement curseur. 3 états (éditable / non-éditable / en édition). |
| D5 | Type cycle | 4 valeurs : NORMAL → ARPEG → ARPEG_GEN → LOOP. Type et Quantize sont 2 fields distincts. |
| D6 | Quantize | Options dynamiques selon type : NORMAL = bloqué ; ARPEG/ARPEG_GEN = Imm/Beat ; LOOP = Free/Beat/Bar. Storage = même field `quantize[NUM_BANKS]`, interprétation discriminée par type. |
| D7 | Sections inline | `─ TYPE ─` `─ SCALE ─` `─ WALK ─` portées par les bordures horizontales du tableau dans la colonne label gauche. |
| D8 | Couleur ARPEG_GEN | Magenta VT-only (pas de slot runtime dédié). |
| D9 | Code architecture | Working-copy struct unifiée, table déclarative `PARAM_TABLE`, fonctions courtes (drawTable, drawInfo, handleNavigation, handleEdition). |
| D10 | `d` Defaults | Reset cell focus uniquement (pas le tableau entier). |
| D11 | Cap MAX_ARP_BANKS / MAX_LOOP_BANKS | Cycle Type **skip** le type au-delà du cap (pas de refus bloquant). INFO state 1 affiche `(cap atteint)`. `MAX_LOOP_BANKS` introduit dans le refacto avec valeur placeholder 2. |
| D12 | NvsManager refacto | Tool 5 lit/écrit `BankTypeStore` directement, ne dépend plus des 7 paires `Loaded*` (laissées si utilisées ailleurs). |

### §20 — Suite

Plan d'implémentation à rédiger via `superpowers:writing-plans` après validation de cette spec. Le plan devra :
- Découper en tasks courtes (rendering, working-copy, table déclarative, integration, retrait NvsManager paires).
- HW gate dédié : valider visuellement chaque section (TYPE / SCALE / WALK) + nav 2D + INFO auto-update + non-régression ARPEG_GEN runtime.
- Pas de bump NVS prévu.
- Cible : commit `dfe8747` → refacto livré → terrain prêt pour Phase 3 LOOP.

---

**Spec VALIDÉE pour rédaction plan d'implémentation** (2026-05-17).
