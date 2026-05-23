# LOOP Phase 3 — Tool 3 b1 contextuel + Tool 4 ext + retrait dev seed M7

> **ARCHIVÉ 2026-05-23** — Document caduc. Phase 3 LOOP refondue en Tool PAD ROLE (fusion Tool 3 + Tool 4 en 4 pages). Voir [`tool-pad-role-design.md`](../../superpowers/specs/2026-05-23-tool-pad-role-design.md). Code 3.A/3.B/3.C livré sur main reste acquis.

**Date** : 2026-05-19
**Statut** : VALIDÉ pour rédaction plan d'implémentation
**Scope** : refacto Tool 3 (`src/setup/ToolPadRoles.{h,cpp}`) en 3 sous-pages contextuelles
NORM / ARPEG / LOOP, extension Tool 4 (`src/setup/ToolControlPads.{h,cpp}`) pour
refuser ControlPad sur pad LOOP control, retrait du dev seed M7 (Phase 2 transitoire)
remplacé par defaults LOOP hardcodés.
**Sources** :
- Spec LOOP [`2026-04-19-loop-mode-design.md`](2026-04-19-loop-mode-design.md) §5 (refactor Tool 3 b1) + §27 Phase 3.
- Phase 2 close (commits `6c0b4d8` → `284bec4`, HW gates G1-G9 validés 2026-05-19).
- Brainstorm 2026-05-19 (2 tours : ouvertures Phase 3 + collisions centralisé vs minimaliste).
- Décision architecturale **2026-05-19** : refus de l'arbiter centralisé `PadRoleArbiter`
  comme over-engineering Phase 3 — adoption d'helpers cross-store inline.

---

## Partie 1 — Cadre

### §1 — Objectifs Phase 3

Trois livrables, à exécuter dans un même bundle Phase 3 :

1. **Tool 3 b1 contextuel** — refacto du Tool 3 actuel (`ToolPadRoles`, 788 lignes
   `.cpp`) en **3 sous-pages mutuellement exclusives** : NORM, ARPEG, LOOP. La
   sous-page NORM gère l'assignement des 8 bank pads ; ARPEG les 20 rôles scale
   (root/mode/chrom) + arp (hold/octave) ; LOOP les 19 rôles loop (3 controls
   REC/PS/CLEAR + 16 slots).

2. **Tool 4 extension** — `ToolControlPads` refuse l'assignement d'un ControlPad sur
   un pad déjà occupé par un LOOP control (REC, PS, CLEAR). Refus avec flash msg
   (pattern existant `_setFlash`).

3. **Retrait dev seed M7** — la fonction `applyDevSeedLoopPadsIfSafe()` (Phase 2
   transitoire) est supprimée. Remplacée par des **defaults hardcodés** dans le
   validator de `LoopPadStore` au boot : `recPad=30, playStopPad=31, clearPad=32`
   si NVS LoopPadStore initialisé vide.

### §2 — Hors scope Phase 3

- **Tool 7 refacto** (PotMapping en 3 pages NORMAL / ARPEG / LOOP) → Phase 4
  (bundle avec PotRouter 3 contexts + LED wiring complet). Drift spec §27 P3 ligne
  621 à corriger en doc-sync (§19).
- **16 slot pads runtime wiring** → Phase 6 (Slot Drive LittleFS). Phase 3 livre
  l'UI d'assignement des 16 slots dans Tool 3 sous-page LOOP, mais aucun
  consommateur runtime — le `LoopEngine` n'utilise pas `slotPads[]`.
- **`PadRoleArbiter` centralisé** — refusé après évaluation coût/bénéfice
  (brainstorm 2026-05-19). Approche minimaliste : helpers cross-store + inline
  checks dans Tool 3 / Tool 4. Si une 3e consommateur émerge en Phase 4+ ou si
  les règles passent de 5 à 10+, refactor possible plus tard. Pattern YAGNI.
- **Cache NvsManager pour Scale/Arp/Bank pads** — refusé après audit indépendant
  2026-05-19 (refondation (c)). L'architecture existing maintient ces arrays
  comme variables locales `main.cpp` (`bankPads[NUM_BANKS]`, `rootPads[7]`,
  `modePads[7]`, `chromaticPad`, `holdPad`, `octavePads[4]`) passées par référence
  aux managers. NvsManager fait juste `loadBlob` / `saveBlob`, ne cache pas.
  Phase 3 respecte ce design : les helpers §13 prennent les arrays par référence
  pour ces catégories. Seuls `_loadedLoopPad` (Phase 2) et `_ctrlStore` (existing)
  sont cachés NvsManager — ils gardent l'API getter.

### §3 — Convention de nommage : code vs UI

**Règle stricte** : les noms de **fonctions, variables, types code** ne changent
pas (`ToolPadRoles`, `_wkBankPads`, `_poolLine`, `ROLE_BANK`, etc.). Le refacto
Phase 3 est **interne** (extension de l'architecture sous-page) sans rename.

**En UI / labels VT100 / INFO panels / docs** : utiliser systématiquement
**NORM** / **ARPEG** / **LOOP** pour désigner les 3 sous-pages. Le label header
du tool est `Pad Roles [NORM|ARPEG|LOOP]` (sous-page active highlighted).

**Justification** : cohérence visuelle avec Tool 5 refacto (qui utilise déjà
NORM / ARP_N / LOOP / ARP_G en labels). Pas de coût de migration code. Le mot
"Bank" ne désigne plus la sous-page parce qu'il y a 8 banks attribuables aux 3
types — "NORM" reflète mieux la fonction de cette sous-page (assignement des
banks dont le type NORMAL est le défaut).

---

## Partie 2 — Architecture UI Tool 3 b1

### §4 — Navigation

**3 sous-pages navigables via touche TAB** :

```
NORM ←TAB→ ARPEG ←TAB→ LOOP ←TAB→ NORM (cycle)
```

**Arrow keys (↑↓←→)** : navigation **intra-sous-page uniquement**. Pas de switch
de sous-page via arrows (évite confusion bord du grid).

**Convention applicable à tous les futurs tools multi-sous-pages** (Tool 7 Phase 4,
Tool 8 déjà 6-sections via shortcuts). À noter dans setup-tools-conventions.md
comme pattern UI normalisé.

### §5 — Sous-page NORM

**Fonction** : assignement des 8 bank pads (où chaque bank `0..7` est physiquement
mappée).

**Grid 4×12 affiche** :
- Pad qui est bank `N` → label `B<N+1>`, vert active, **éditable**.
- Pad qui a un rôle non-bank (root/mode/chrom/hold/octave/loop-anything/ControlPad)
  → label de ce rôle, **dim/grey, locked** (R1 sacré bank — impossible d'assigner
  un bank ici sans clear l'autre rôle d'abord).
- Pad libre → label `··`, neutre, éditable.

**Pool** : 1 ligne avec les 8 bank slots (`B1..B8`).

**Edition** : user navigate cursor sur cell, choose bank slot dans pool, ENTER →
move bank vers ce pad. Ancien pad bank N redevient libre.

### §6 — Sous-page ARPEG

**Fonction** : assignement des 20 rôles ARPEG (7 root + 7 mode + 1 chrom + 1 hold
+ 4 octave).

**Grid 4×12 affiche** :
- Pad qui est rôle ARPEG (assigné dans working) → label rôle (`R0..R6`, `M0..M6`,
  `Ch`, `Hd`, `O1..O4`), vert active, **éditable**.
- Pad qui est bank → label `B<N>`, **dim red, locked** (R1).
- Pad qui est ControlPad → label `cp`, **dim grey, éditable** (R5 cross-layer
  autorise un rôle ARPEG hold-left sur même pad).
- Pad qui est rôle LOOP (slot ou control) → label dim (ex : `·sN`, `·R/P/C`),
  **éditable** (R4 inter-ctx pour Slot ; R5 cross-layer pour REC/PS/CLEAR).
- Pad libre → label `··`, neutre, éditable.

**Pool** : 5 lignes (root × 7 + mode × 7 + chrom × 1 + hold × 1 + octave × 4).

### §7 — Sous-page LOOP

**Fonction** : assignement des 19 rôles LOOP (3 controls + 16 slots).

**Grid 4×12 affiche** :
- Pad qui est rôle LOOP (assigné dans working) → label (`REC`, `PS_`, `CLR`,
  `S00..S15`), vert active, **éditable** (avec contraintes §13-§14 sur les 3
  controls).
- Pad qui est bank → label `B<N>`, **dim red, locked** (R1).
- Pad qui est ControlPad → label `cp`, **dim grey, conditionnel** :
  - Éditable pour Slot LOOP (R5 cross-layer Slot/CP).
  - **Non-éditable pour REC/PS/CLEAR** (R2 layer musical exclusion + ControlPad
    carries config — refus avec flash).
- Pad qui est rôle ARPEG (root/mode/chrom/hold/octave) → label dim
  (ex : `·R0`, `·Hd`), **éditable** (R4 inter-ctx pour Slot ; R5 cross-layer
  pour REC/PS/CLEAR).
- Pad libre → label `··`, neutre, éditable.

**Pool** : 2 sections :
- Section "Controls" (1 ligne, 3 items : REC, PS, CLR).
- Section "Slots" (2 lignes de 8 items : S00..S07, S08..S15).

### §8 — Layout VT100

**Header** :
```
┌──────────────────────────────────────────────────────────────┐
│ Tool 3 — Pad Roles                          [NORM|ARPEG|LOOP]│
└──────────────────────────────────────────────────────────────┘
```

Sous-page active highlighted (couleur ou inversé). Cycle TAB.

**Grid 4×12** : identique au Tool 3 actuel (12 cols × 4 rows). Labels et couleurs
adaptés selon sous-page (voir §5-§7).

**Pool** : sous l'INFO panel, contenu adapté selon sous-page.

**INFO panel** : décrit le rôle courant sous cursor + hints contextuels (par ex.
"Pad 30 has Root C (ARPEG, cross-context). Slot 5 (LOOP) can be added here.").

**Control bar** : 1 ligne en bas avec keys actifs (TAB, ↑↓←→, ENTER, ESC,
CLEAR-ALL).

**Bank pads visible partout** : dans toutes les 3 sous-pages, les 8 bank pads
sont **toujours affichés** dans le grid (en NORM ils sont éditables, dans ARPEG
et LOOP ils sont dim red locked). Cette redondance visuelle est intentionnelle —
elle aide l'user à éviter de naviguer/cliquer sur un bank pad par erreur.

---

## Partie 3 — Système de collision

### §9 — Hiérarchie : 3 cases + Bank sacré

Le modèle conceptuel central : un pad peut contenir simultanément **0 ou 1 rôle
dans chacune de 3 cases mutuellement exclusives** :

| Case | Layer | Context | Rôles |
|---|---|---|---|
| **Musical** | press direct | NONE | ControlPad ⊕ LOOP_REC ⊕ LOOP_PS ⊕ LOOP_CLEAR |
| **HL ARPEG** | LEFT maintenu | ARPEG | Root × 7 ⊕ Mode × 7 ⊕ Chrom ⊕ Hold ⊕ Octave × 4 |
| **HL LOOP** | LEFT maintenu | LOOP | Slot × 16 |

Plus **Bank sacré** (R1) : si pad occupe Bank, les 3 cases sont vides ; et
réciproquement.

**Conséquence** : un pad peut porter **au plus 3 rôles coexistants** (1 musical
+ 1 HL ARPEG + 1 HL LOOP), OU 1 Bank (exclusif).

### §10 — Les 5 règles formalisées

Reprise spec LOOP §5, formalisées comme clauses logiques :

```
canAssign(pad, newRole, ctx_newRole) :
  existing = getRolesAt(pad)

  # R1. Bank sacré
  if any X in existing where layer(X) == BANK_SACRED :
    return REFUSE("Pad is Bank — sacred")
  if layer(newRole) == BANK_SACRED and existing not empty :
    return REFUSE("Pad has roles — clear first")

  # R2. Layer musical exclusion (1 max par pad)
  if layer(newRole) == MUSICAL :
    for X in existing :
      if layer(X) == MUSICAL :
        # conflit intra-musical : swap-to-pool si X tolère ; refus sinon
        if X.toleratesUnassigned and not X.carriesConfig :
          return SWAP_TO_POOL(X, newRole)
        return REFUSE("Pad has <X> — move first")

  # R3. Layer hold-left intra-contexte (1 max par ctx)
  if layer(newRole) == HOLD_LEFT :
    for X in existing :
      if layer(X) == HOLD_LEFT and context(X) == context(newRole) :
        if X.toleratesUnassigned :
          return SWAP_TO_POOL(X, newRole)
        return REFUSE("Pad has <X> — move first")

  # R4. Hold-left inter-contexte → OK (fall-through)
  # R5. Cross-layer (HL ↔ MUSICAL) → OK (fall-through)
  return COEXIST(newRole)
```

### §11 — Table des rôles : attributs

| Rôle | Layer | Context | Tolère 0xFF | Config riche |
|---|---|---|---|---|
| Bank 0..7 | BANK_SACRED | — | non | non |
| Root 0..6 | HOLD_LEFT | ARPEG | oui | non |
| Mode 0..6 | HOLD_LEFT | ARPEG | oui | non |
| Chrom | HOLD_LEFT | ARPEG | oui | non |
| Hold | HOLD_LEFT | ARPEG | oui | non |
| Octave 0..3 | HOLD_LEFT | ARPEG | oui | non |
| LOOP REC | MUSICAL | LOOP | **non** (hard-constraint Q14) | non |
| LOOP PS | MUSICAL | LOOP | **non** (hard-constraint Q14) | non |
| LOOP CLEAR | MUSICAL | LOOP | **non** (hard-constraint Q14) | non |
| LOOP Slot 0..15 | HOLD_LEFT | LOOP | oui | non |
| ControlPad | MUSICAL | — | oui | **oui** (CC#, mode, channel, deadzone, releaseMode) |

### §12 — Matrice de compatibilité rôle × rôle

Quand pad a déjà rôle **X** et user veut placer rôle **Y** :

| X (existant) ↓ / Y (entrant) → | Bank | CP (musical) | REC/PS/CLR (musical) | Root/Mode/.../Oct (HL ARPEG) | Slot (HL LOOP) |
|---|---|---|---|---|---|
| **Bank** | refuse R1 | refuse R1 | refuse R1 | refuse R1 | refuse R1 |
| **CP** | refuse R1 | refuse (config loss) | refuse (hard-c) | coexist R5 | coexist R5 |
| **REC/PS/CLR** | refuse R1 | refuse (hard-c) | refuse (hard-c) | coexist R5 | coexist R5 |
| **Root/Mode/.../Oct** | refuse R1 | coexist R5 | coexist R5 | swap-to-pool R3 (Y évince X) | coexist R4 |
| **Slot** | refuse R1 | coexist R5 | coexist R5 | coexist R4 | swap-to-pool R3 (Y évince X) |

### §13 — Helpers cross-store

5 helpers exposés dans `KeyboardData.h` à côté des structs concernées. Pas de
module dédié. ~50 lignes total.

> **Note architecturale** (post audit indépendant 2026-05-19, refondation (c))
> : seuls `LoopPadStore` et `ControlPadStore` sont **cachés** dans NvsManager
> (`_loadedLoopPad` Phase 2 + `_ctrlStore` existing). Les Scale/Arp/Bank pads
> sont **des arrays propriétaires de `main.cpp`** (variables locales `bankPads[NUM_BANKS]`
> `rootPads[7]`, `modePads[7]`, `chromaticPad`, `holdPad`, `octavePads[4]`)
> passés par référence à `NvsManager.loadAll(...)` et aux managers
> (`BankManager.setBankPads`, `ScaleManager.setRootPads`, etc.). NvsManager
> fait juste le `loadBlob` / `saveBlob` ; **pas de cache redondant**. Les
> helpers ci-dessous reflètent cette architecture : signatures **prennent les
> arrays par référence** quand applicable.

```cpp
// LoopPadStore (cached in NvsManager._loadedLoopPad — Phase 2)
inline bool isLoopControlPad(const LoopPadStore& s, uint8_t pad) {
  return s.recPad == pad || s.playStopPad == pad || s.clearPad == pad;
}
inline int8_t findLoopSlotIdx(const LoopPadStore& s, uint8_t pad) {
  for (uint8_t i = 0; i < 16; i++)
    if (s.slotPads[i] == pad) return (int8_t)i;
  return -1;
}

// ControlPadStore (cached in NvsManager._ctrlStore — existing)
inline int8_t findControlPadEntryIdx(const ControlPadStore& s, uint8_t pad) {
  for (uint8_t i = 0; i < s.count; i++)
    if (s.entries[i].padIndex == pad) return (int8_t)i;
  return -1;
}

// Scale roles — owned by main.cpp (rootPads[7], modePads[7], chromaticPad).
// Signature prend les arrays + pad chromatique en paramètres.
enum class ScaleRoleKind : uint8_t { NONE, ROOT, MODE, CHROM };
struct ScaleRoleResult { ScaleRoleKind kind; uint8_t idx; };

inline ScaleRoleResult scaleRoleAtPad(const uint8_t* rootPads,
                                       const uint8_t* modePads,
                                       uint8_t chromaticPad,
                                       uint8_t pad) {
  for (uint8_t i = 0; i < 7; i++) {
    if (rootPads[i] == pad) return ScaleRoleResult{ScaleRoleKind::ROOT, i};
    if (modePads[i] == pad) return ScaleRoleResult{ScaleRoleKind::MODE, i};
  }
  if (chromaticPad == pad) return ScaleRoleResult{ScaleRoleKind::CHROM, 0};
  return ScaleRoleResult{ScaleRoleKind::NONE, 0};
}

// Arp roles — owned by main.cpp (holdPad scalar, octavePads[4]).
enum class ArpRoleKind : uint8_t { NONE, HOLD, OCTAVE };
struct ArpRoleResult { ArpRoleKind kind; uint8_t idx; };

inline ArpRoleResult arpRoleAtPad(uint8_t holdPad,
                                   const uint8_t* octavePads,
                                   uint8_t pad) {
  if (holdPad == pad) return ArpRoleResult{ArpRoleKind::HOLD, 0};
  for (uint8_t i = 0; i < 4; i++) {
    if (octavePads[i] == pad) return ArpRoleResult{ArpRoleKind::OCTAVE, i};
  }
  return ArpRoleResult{ArpRoleKind::NONE, 0};
}

// Bank assignment — owned by main.cpp (bankPads[NUM_BANKS]).
// Note : struct BankSlot (KeyboardData.h:369) ne contient PAS de champ `pad`
// (le mapping bank→pad vit dans `bankPads[]`, pas dans `BankSlot::*`).
inline int8_t findBankIdxForPad(const uint8_t* bankPads, uint8_t pad) {
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (bankPads[i] == pad) return (int8_t)i;
  }
  return -1;
}
```

**Usage** :
- Tool 3 (`ToolPadRoles`) a déjà les pointeurs `_bankPads`, `_rootPads`, `_modePads`,
  `_chromaticPad`, `_holdPad`, `_octavePads` comme members (cf `ToolPadRoles.h:46-51`,
  passés via `begin()`). Aucun changement de signature `begin()` requis pour les
  arrays Scale/Arp/Bank. Tool 3 appelle les helpers inline avec ses members.
- Tool 3 a aussi besoin d'un nouveau member `NvsManager* _nvs` Phase 3 pour
  accéder à `getLoadedLoopPadStore()` et `getLoadedControlPadStore()` — c'est le
  seul changement de signature `begin()` (ajouter `NvsManager* nvs` en param).
- Tool 4 (`ToolControlPads`) a déjà `_nvs` (cf `ToolControlPads.h:27`) et peut
  appeler `isLoopControlPad(_nvs->getLoadedLoopPadStore(), pad)` sans extension
  de signature.

---

## Partie 4 — Lifecycle des rôles

### §14 — Assign / move / swap-to-pool semantics

**Trois opérations user-facing dans Tool 3** :

1. **Assign** : placer rôle Y sur pad libre N. Le store correspondant écrit Y → N.
   Cas trivial, OK_COEXIST per §10.

2. **Move** : placer rôle Y sur pad N alors que Y était sur pad M (M ≠ N).
   Le store écrit Y → N et M devient libre (au pool si Y tolère 0xFF). Sémantique :
   le rôle Y est unique, son pad d'assignement change.

3. **Swap-to-pool** : placer rôle Y sur pad N alors que pad N contient déjà rôle
   X (où X et Y sont dans la même case §9). X est évincé : son store est mis à
   0xFF (X retourne au pool des rôles non-assignés). Y prend N.
   Condition : `X.toleratesUnassigned && !X.carriesConfig`.
   Si condition non remplie : refus + flash msg.

**Aucune opération "Clear"** sur les rôles soumis au hard-constraint (REC/PS/CLEAR).
Pour les autres rôles, "clear" = move vers 0xFF (retour au pool) explicite.

### §15 — Hard constraints

**3 controls LOOP (REC, PS, CLEAR)** :
- Toujours assignés (jamais 0xFF dans LoopPadStore).
- Initial : defaults hardcodés 30/31/32 (§16).
- Move autorisé (vers autre pad libre, ou évincement d'un rôle swap-able).
- Clear / unassign INTERDIT.
- Pool item `ROLE_NONE` masqué / désactivé visuellement quand cursor sur REC/PS/CLEAR.
- Exit Tool 3 bloqué si un des 3 == 0xFF (cas pathologique post-suppression dev seed M7
  + NVS corrompu — défense en profondeur).

**8 bank pads** :
- Toujours assignés (Phase 0+ déjà cas).
- Bank pads sacrés (R1) — pas de coexistence.
- Move autorisé dans Tool 3 sous-page NORM ; clear interdit (validator garantit
  toujours 8 banks distincts).

### §16 — Defaults LOOP hardcodés 30/31/32

Dans `validateLoopPadStore()` (extension Phase 3) :

```cpp
constexpr uint8_t LOOP_REC_DEFAULT_PAD       = 30;
constexpr uint8_t LOOP_PLAYSTOP_DEFAULT_PAD  = 31;
constexpr uint8_t LOOP_CLEAR_DEFAULT_PAD     = 32;

inline void validateLoopPadStore(LoopPadStore& s) {
  // Magic / version / etc. handled by descriptor logic.
  // Apply defaults if controls unassigned (NVS empty case).
  if (s.recPad == 0xFF)      s.recPad      = LOOP_REC_DEFAULT_PAD;
  if (s.playStopPad == 0xFF) s.playStopPad = LOOP_PLAYSTOP_DEFAULT_PAD;
  if (s.clearPad == 0xFF)    s.clearPad    = LOOP_CLEAR_DEFAULT_PAD;
  // Slots restent 0xFF si non-assignés (tolerated).
  // Pas de check collision avec ControlPadStore au validator level — c'est
  // l'arbiter Tool 3 ext qui empêchera futurs cas. Boot collision = warning Serial
  // sur trace post-validator (cf §17 EC9).
}
```

**Comportement** :
- Boot premier flash / NVS vide → recPad=30, PS=31, CLEAR=32 directement.
- Boot avec NVS existant valide → defaults non-appliqués (NVS gagne).
- User peut bouger 30/31/32 vers d'autres pads via Tool 3 sous-page LOOP (move).
- Évolution future : changer les constantes 30/31/32 si retour user inconfortable.

### §17 — Retrait dev seed M7

Phase 2 a livré `applyDevSeedLoopPadsIfSafe()` ([NvsManager.cpp:1203-1230](../../src/managers/NvsManager.cpp))
avec call site [main.cpp:538](../../src/main.cpp). Cette fonction est **rendue obsolète**
par les defaults §16.

**Actions Phase 3** :
- Supprimer la fonction (header + implémentation).
- Supprimer le call site dans `main.cpp`.
- Supprimer le commentaire dans `loadAll()` ([NvsManager.cpp:162](../../src/managers/NvsManager.cpp))
  qui référence le seed conditionnel.
- Mettre à jour spec LOOP §27 P2 ligne 611 pour refléter la suppression du seed.

**Trace boot warning** : si `validateLoopPadStore()` détecte que les defaults
30/31/32 collisionnent avec un ControlPadStore existant (cas pathologique edge),
émettre un Serial warning au boot :
```
[BOOT] WARN: LOOP defaults 30/31/32 collide with ControlPad assignments.
[BOOT]   → LoopPadStore controls left at 0xFF.
[BOOT]   → User must resolve in Tool 3 sub-page LOOP (setup mode).
```
Avec un check explicite à la fin de `loadAll()` post-validator (lecture
`_loadedControlPads` + `_loadedLoopPad` croisée).

---

## Partie 5 — Tool 4 extension

### §18 — Refus ControlPad sur pad LOOP control

**Modification `ToolControlPads`** :

Avant d'enregistrer un ControlPad sur pad N (via slot add ou edit), Tool 4 appelle
`isLoopControlPad(_nvs->getLoadedLoopPadStore(), N)`. Si `true` :
- Refus du `_addSlot(N)` ou de l'édition `padIndex` vers N.
- Flash msg : `"Pad N is LOOP REC/PS/CLR — move in Tool 3 first."`
- Pas de side effect sur l'état working.

**Grid render Tool 4** : pads correspondant à LOOP REC/PS/CLEAR affichés `R`,
`P`, `C` (label 1-char), couleur dim red, marquage locked.

**Cas de la grid sélection** : si user navigue cursor sur un pad LOOP control,
le cursor peut visiter la cell mais l'enter / assignement est refusé. Cohérent
avec le pattern existant Tool 4 (cursor visite tous les pads, action conditionnelle).

---

## Partie 6 — Test scenarios

### §19 — Edge cases EC1-EC9

Référence pour HW gate Phase 3.F (validation collision cross-tool).

**EC1** — Tool 4 : tente assigner ControlPad sur pad LOOP REC (30 par défaut).
- Attendu : refus + flash `"Pad 30 is LOOP REC — move in Tool 3 first."`
- Vérif : ControlPadStore.entries[] inchangé après tentative.

**EC2** — Tool 3 sous-page LOOP : tente assigner LOOP REC sur pad qui est
ControlPad (ex : pad 5 avec ControlPad CC74).
- Attendu : refus + flash `"Pad 5 is ControlPad — delete entry in Tool 4 first."`
- Vérif : LoopPadStore.recPad inchangé.

**EC3** — Tool 3 sous-page LOOP : assigne Slot 5 sur pad 16 qui est Root C
ARPEG.
- Attendu : OK coexist R4. LoopPadStore.slotPads[5] = 16. ScalePadStore.rootPads[0] = 16
  (inchangé).
- Vérif grid sous-page LOOP : pad 16 affiche `S05` active. Sous-page ARPEG : pad 16
  affiche `R0` active.

**EC4** — Tool 3 sous-page LOOP : assigne Slot 5 sur pad 17 qui est déjà Slot 7
LOOP (intra-ctx).
- Attendu : swap-to-pool. LoopPadStore.slotPads[7] = 0xFF (Slot 7 retourne au pool).
  LoopPadStore.slotPads[5] = 17.
- Flash : `"Slot 7 returned to pool — re-assign manually."`
- Vérif grid sous-page LOOP : pad 17 affiche `S05`. Pool montre Slot 7 unassigned.

**EC5** — Tool 3 sous-page LOOP : assigne Slot 5 sur pad 8 qui est ControlPad
CC74 (cross-layer).
- Attendu : OK coexist R5. LoopPadStore.slotPads[5] = 8. ControlPadStore inchangé.
- Vérif grid sous-page LOOP : pad 8 affiche `S05` active. Tool 4 grid : pad 8
  affiche `cp` active.

**EC6** — Tool 3 sous-page ARPEG : assigne Root D sur pad 16 qui est déjà Root C
(intra-ARPEG hold-left, R3).
- Attendu : swap-to-pool. ScalePadStore.rootPads[0] = 0xFF (Root C unassigned).
  ScalePadStore.rootPads[1] = 16 (Root D assigné).
- Flash : `"Root C returned to pool — re-assign manually."`

**EC7** — Tool 3 sous-page LOOP : assigne LOOP REC sur pad 31 qui est déjà
LOOP PS (intra-musical, layer M, hard-constraint).
- Attendu : **refus** + flash `"Pad 31 is LOOP PS — move PS first to free this pad."`
- Vérif : LoopPadStore.recPad et playStopPad inchangés.

**EC8** — Tool 3 sous-page NORM : tente déplacer Bank 3 vers pad 16 qui est
Root C ARPEG (Bank sacré, R1).
- Attendu : refus + flash `"Pad 16 has Root C — clear in Tool 3 ARPEG first."`
- Vérif : BankSlots[3].pad inchangé.

**EC9** — Boot : NVS empty LoopPadStore + ControlPadStore avec entry pad 30
(ControlPad CC74 déjà assigné par user via Tool 4 avant ce firmware).
- Attendu : validator applique defaults 30/31/32 directement (validator est
  unilatéral — pas de check cross-store). Post-loadAll, collision check secondaire
  détecte conflit pad 30 = REC + ControlPad.
- Comportement : Serial warning trace, mais state laissé tel quel (LoopPadStore.recPad=30
  ET ControlPadStore.entries[*].padIndex=30 coexistent en NVS).
- Première action user en setup mode : entrer Tool 3 sous-page LOOP, le grid
  montre la collision (pad 30 = `REC` ET en Tool 4 sous-page = `cp`), user move
  l'un ou l'autre.
- Pas de patch automatique. Cas vraiment edge.

**EC10 (additionnel)** — Tool 3 sous-page LOOP : exit avec un des 3 controls
== 0xFF.
- Attendu : exit bloqué + flash `"LOOP needs REC/PS/CLR assigned — move from defaults."`
- Vérif : ce cas ne devrait pas être atteignable avec defaults §16. Test de
  défense en profondeur si user manuellement met `_wkRecPad = 0xFF` (impossible
  par l'UI nominale).

---

## Partie 7 — Cross-references et doc-sync

### §20 — Docs à mettre à jour (Phase 3.H)

| Doc | Mise à jour |
|---|---|
| [`docs/reference/setup-tools-conventions.md`](../reference/setup-tools-conventions.md) | Nouvelle section "Pad role collisions" : 5 règles + table compatibilité + 5 helpers documentés + pattern TAB navigation sous-pages |
| [`docs/reference/nvs-reference.md`](../reference/nvs-reference.md) | LoopPadStore : "DECLARED Phase 1 — WIRED Phase 3 (Tool 3 b1 sous-page LOOP)". Defaults 30/31/32 documentés |
| [`docs/reference/architecture-briefing.md`](../reference/architecture-briefing.md) | Section setup-mode : référence Tool 3 b1 multi-sous-pages. Pattern TAB navigation à ajouter |
| [`docs/reference/patterns-catalog.md`](../reference/patterns-catalog.md) | Pattern P15 (ou suivant disponible) "Multi sub-page tool with TAB navigation" si jugé réutilisable |
| `STATUS.md` | Focus courant + table commits Phase 3 |
| [`docs/superpowers/LOOP_PROGRESS.md`](../LOOP_PROGRESS.md) | Phase 3 → CLOSE |
| [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](2026-04-19-loop-mode-design.md) | §27 P3 : corriger ligne 621 (Tool 7 → Phase 4) ; §27 P2 ligne 611 : retrait référence dev seed M7 ; §28 Q7 : marquer "IMPLEMENTED Phase 3" |

### §21 — Drift spec à corriger Phase 3.H

**§27 P3 ligne 621** : `Refactor Tool 7 en 3 pages` → déplacer texte vers
§27 P4 (déjà mentionné Phase 4). Phase 3 n'inclut plus Tool 7.

**§27 P2 ligne 611** : retirer mention `applyDevSeedLoopPadsIfSafe` (fonction
supprimée Phase 3.G).

---

## Partie 8 — Invariants et budget

### §22 — Invariants ajoutés Phase 3

**Invariant 12 (nouveau)** : LoopPadStore.recPad / playStopPad / clearPad ne sont
**jamais** 0xFF en runtime. Garanti par validator boot §16 + hard-constraint exit
Tool 3 §15.

**Invariant 13 (nouveau)** : Sur un pad donné, au plus 3 rôles coexistent : 1
musical + 1 HL ARPEG + 1 HL LOOP, OU 1 Bank seul. Garanti par les 5 règles §10.

**Invariant 14 (nouveau)** : Les helpers cross-store §13 sont la **source unique**
de vérité de la présence d'un rôle sur un pad. Tout consommateur (Tool 3, Tool 4,
runtime LoopEngine, futurs tools) appelle ces helpers — pas de scan ad-hoc des
stores. **Les helpers prennent en paramètre soit le store NvsManager (LoopPadStore,
ControlPadStore — cachés `_loadedLoopPad`, `_ctrlStore`) soit les arrays managers
(`bankPads`, `rootPads`, `modePads`, `chromaticPad`, `holdPad`, `octavePads` —
propriétés de `main.cpp`, partagées via références non-owning).** Pas de cache
redondant pour les arrays managers — l'architecture existing les considère comme
source de vérité unique.

### §23 — Budget ressources

| Ressource | Phase 3 delta | Justification |
|---|---|---|
| SRAM | ~+0 (helpers inline, pas de cache) | Helpers sont des fonctions inline ; pas d'allocation runtime |
| Flash | ~+5 KB | Code Tool 3 b1 refacto + Tool 4 ext + helpers + retrait dev seed |
| Core 0 | 0 | Setup mode ; pas d'impact runtime |
| Core 1 | 0 | Setup mode ; pas d'impact runtime |
| Wear NVS | 0 | Pas de nouveau write path ; pas de bump struct |

### §24 — Non-goals Phase 3 (rappels)

- Pas de runtime wiring des 16 slot pads LOOP (Phase 6).
- Pas de refacto Tool 7 (Phase 4).
- Pas de module arbiter centralisé (refusé YAGNI).
- Pas de migration NVS (Zero Migration Policy ; pas de bump struct).
- Pas de nouvelle feature musicale (livraison purement UI / config).

---

## Partie 9 — Suite

**Plan d'implémentation** : à rédiger dans
`docs/superpowers/plans/2026-05-19-loop-phase-3-plan.md` après validation
de cette spec.

**Audit adversarial du plan** : posture STOP-and-find avant exécution. Output
B-N* / M* / m* à intégrer dans le manifeste session EXEC.

**Manifeste session EXEC** : adaptation du template SESSION_PROTOCOL.md avec les
9 règles + zones de survol identifiées.

**Prompt d'ouverture session EXEC** : structure standard SESSION_PROTOCOL.md.

---

**Spec VALIDÉE 2026-05-19 post brainstorm 2 tours (questions ouvertes + collisions).**
Prête à servir d'entrée pour la rédaction du plan d'implémentation Phase 3.
