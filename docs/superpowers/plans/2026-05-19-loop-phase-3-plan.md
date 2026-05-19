# LOOP Phase 3 Implementation Plan — Tool 3 b1 contextuel + Tool 4 ext + retrait dev seed M7

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **Workflow embedded** : 5 gates par task (Code → Build → Auto-review → HW gate quand applicable → Commit gate) per [SESSION_PROTOCOL.md](../SESSION_PROTOCOL.md). HW gates **bloquent** — pas de commit avant validation HW Loïc. Mot magique `GO` / `validé` obligatoire (cf manifeste session).

**Goal** : Livrer Tool 3 b1 contextuel (3 sous-pages NORM / ARPEG / LOOP via TAB) + Tool 4 extension (refus ControlPad sur pad LOOP control) + retrait dev seed M7 (remplacement par defaults LOOP 30/31/32 hardcodés dans validator). État final : user peut configurer 3 controls LOOP + 16 slots via UI propre, Tool 4 protège l'invariant R2 (layer musical exclusion), validator garantit `recPad/playStopPad/clearPad` jamais 0xFF.

**Architecture** : extension du Tool 3 actuel (`ToolPadRoles`, 788 lignes `.cpp`) avec état `_activeSubPage`, working copy `_wkLoopPad: LoopPadStore`, refacto interne de `buildRoleMap` / `drawGrid` / `drawPool` / `run` pour gérer les 3 sous-pages. Pas de rename code (cf §3 spec). 5 helpers cross-store ajoutés dans `KeyboardData.h` (~50 lignes), consommés inline par Tool 3 et Tool 4. Validator `LoopPadStore` étendu avec defaults hardcodés. Pas de module arbiter centralisé (refusé YAGNI — cf §2 spec).

**Tech stack** : C++17, Arduino framework, PlatformIO, ESP32-S3-N8R16, VT100 setup mode terminal, Preferences NVS.

**Sources** :
- [Spec design Phase 3](../specs/2026-05-19-loop-phase-3-design.md) — VALIDÉE 2026-05-19
- [Spec LOOP design](../specs/2026-04-19-loop-mode-design.md) §5 (Tool 3 b1) + §27 P3
- Phase 2 LOOP commits `6c0b4d8` → `284bec4` (close 2026-05-19, dev seed M7 = `applyDevSeedLoopPadsIfSafe`)
- Code actuel : `src/setup/ToolPadRoles.{h,cpp}` (788 lignes cpp), `src/setup/ToolControlPads.{h,cpp}` (863 lignes cpp), `src/core/KeyboardData.h` (LoopPadStore §545, ControlPadStore §410, ScalePadStore §511, ArpPadStore §525)

**Audit adversarial** : [`2026-05-19-loop-phase-3-plan_AUDIT.md`](2026-05-19-loop-phase-3-plan_AUDIT.md) — 19 findings (1 B-N + 7 M + 11 m). **Fix critiques B-N1 + M1-M5 intégrés** dans les tasks ci-dessous (cf addendum "Audit-fix integration" en fin de plan). M6-M7 et mineurs documentés comme actions doc-sync.

---

## Scope Phase 3 — inclus / exclus

| Inclus Phase 3 | Exclus (autre phase / refusé) |
|---|---|
| 5 helpers cross-store inline dans `KeyboardData.h` (~50 lignes) | Module `PadRoleArbiter` centralisé (refusé YAGNI 2026-05-19) |
| Tool 3 b1 : refacto `ToolPadRoles` avec `_activeSubPage` (NORM/ARPEG/LOOP) + nav TAB | Tool 7 PotMapping refacto 3 pages (Phase 4) |
| Tool 3 sous-page NORM : assignement 8 bank pads (extraction de l'existant) | Tool 5 refacto (déjà livré pré-Phase 2) |
| Tool 3 sous-page ARPEG : 20 rôles (root×7, mode×7, chrom, hold, octave×4) (extraction de l'existant) | PotRouter 3 contexts (Phase 4) |
| Tool 3 sous-page LOOP : 3 controls + 16 slots editable | Runtime wiring 16 slots LOOP (Phase 6 — Slot Drive LittleFS) |
| Tool 3 swap-to-pool intra-ctx hold-left (R3) | Migration NVS (Zero Migration Policy ; pas de bump struct LoopPadStore) |
| Tool 3 hard-constraint exit : refus si recPad/PS/CLEAR == 0xFF | LED `EVT_LOOP_*` pattern mapping (Phase 4) |
| Tool 3 cross-store visualisation grid : dim cross-ctx + locked Bank/CP/musical | Tool 4 internal refacto (Phase 4 ou skip) |
| Tool 4 ext : refus ControlPad sur pad LOOP REC/PS/CLEAR (helper `isLoopControlPad`) | Module `BankAssignmentManager` (out-of-scope) |
| Tool 4 grid : LOOP REC/PS/CLEAR montrés locked rouge dim | LED preview LOOP-specific Tool 8 (Phase 4 polish) |
| Validator `LoopPadStore` étendu : defaults hardcodés 30/31/32 si recPad/PS/CLEAR == 0xFF | Tool 1 / Tool 2 / Tool 6 / Tool 8 (pas touchés) |
| Retrait `applyDevSeedLoopPadsIfSafe` (Phase 2 transitoire) + call site main.cpp:538 + commentaire NvsManager.cpp:162 | — |
| Doc-sync : setup-tools-conventions / nvs-reference / architecture-briefing / patterns-catalog / STATUS / LOOP_PROGRESS / spec LOOP §27 corrections | — |

---

## Décisions actées avant code

### D1 — Convention nommage : code vs UI

Code (fonctions, variables, types, enum values) : noms existants conservés (`ToolPadRoles`, `_wkBankPads`, `_poolLine`, `ROLE_BANK`, `POOL_BANK_LABELS`, etc.). **Aucun rename**.

UI / labels VT100 / INFO panel / docs : **NORM / ARPEG / LOOP** pour les 3 sous-pages. Label header `Pad Roles [NORM|ARPEG|LOOP]`.

Justification : cohérence UX avec Tool 5 (qui utilise déjà NORM / ARP_N / LOOP / ARP_G), pas de coût migration code, scope strict Phase 3.

### D2 — Pas d'arbiter centralisé (YAGNI)

Module `PadRoleArbiter` proposé en brainstorm puis refusé après évaluation coût/bénéfice. Approche minimaliste retenue : 5 helpers inline dans `KeyboardData.h` + checks inline dans Tool 3 et Tool 4. Si une 3e consommateur émerge en Phase 4+ ou si les 5 règles passent à 10+, refactor possible plus tard.

### D3 — Defaults LOOP : hardcodés dans validator

`recPad=30, playStopPad=31, clearPad=32` hardcodés via constexpr dans `KeyboardData.h`. Appliqués dans `validateLoopPadStore()` quand champs == 0xFF (cas NVS vide). Plus tard (post-Phase 3, si feedback live), changer les constantes et recompiler.

**Conséquence boot** :
- NVS vide → defaults 30/31/32 directement.
- NVS valide → defaults non-appliqués (NVS gagne).
- Si NVS Tool 4 a un ControlPad sur 30/31/32 (cas pathologique edge) → validator unilatéral assigne 30/31/32 quand même ; collision check secondaire post-loadAll émet warning Serial.

### D4 — Hard-constraint LOOP controls

`recPad / playStopPad / clearPad` ne sont **jamais** 0xFF en runtime. Garanti par :
- Validator §16 spec (defaults D3).
- Tool 3 hard-constraint exit : refus si un des 3 == 0xFF.
- Tool 3 UI design : pool item `ROLE_NONE` désactivé quand cursor sur REC/PS/CLEAR (peut faire move only).

### D5 — Swap-to-pool

Quand user assigne rôle Y sur pad N occupé par rôle X dans la **même case** (layer × context per §9 spec), X est évincé vers le pool (store correspondant mis à 0xFF). Condition : `X.toleratesUnassigned && !X.carriesConfig` (cf table §11 spec).

Si condition non remplie → refus + flash msg.

### D6 — Tool 4 ext : helper-based check

Pas de refacto interne Tool 4. Juste ajout d'un check inline avant chaque `_addSlot` / édition `padIndex` : `if (isLoopControlPad(_nvs->getLoadedLoopPadStore(), pad)) { _setFlash(...); return; }`.

### D7 — Drift Tool 7 dans spec §27 P3

La spec LOOP §27 P3 ligne 621 dit "Refactor Tool 7 en 3 pages" — c'est obsolète (Tool 7 = Phase 4 per LOOP_PROGRESS + STATUS). À corriger en doc-sync Phase 3.H.

### D8 — Retrait dev seed M7 = dernière action (Phase 3.G)

Permet de tester le path runtime LOOP avant retrait (dev seed actif jusqu'à 3.F). En 3.G, le dev seed est supprimé après validation HW G6 (full workflow boot factory NVS vide → Tool 3 assigne → LOOP joue).

---

## File structure — Create / Modify

### Files to CREATE

Aucun nouveau fichier source. Helpers cross-store ajoutés inline dans `KeyboardData.h` (header existant).

### Files to MODIFY

| Fichier | Phase | Nature de la modification |
|---|---|---|
| `src/core/KeyboardData.h` | 3.A, 3.E | Ajout 5 helpers inline (`isLoopControlPad`, `findLoopSlotIdx`, `findControlPadEntryIdx`, `scaleRoleAtPad`, `arpRoleAtPad`, `findBankIdxForPad`) ; ajout constexpr defaults LOOP (30/31/32) ; ajout enums `ScaleRoleKind` + `ArpRoleKind` ; extension `validateLoopPadStore()` |
| `src/setup/ToolPadRoles.h` | 3.C, 3.D, 3.E | Ajout enum `SubPage` ; ajout membres `_activeSubPage` + `_wkLoopPad: LoopPadStore` + dérivés ; ajout méthodes `_handleTab`, `_drawSubPageHeader`, `_buildLoopRoleMap`, ext `assignRole`/`clearRole`/etc. |
| `src/setup/ToolPadRoles.cpp` | 3.C, 3.D, 3.E | Refacto `buildRoleMap` selon sous-page ; refacto `drawGrid` / `drawPool` / `drawInfoPanel` / `drawControlBar` selon sous-page ; ajout handlers TAB ; extension `assignRole`/`clearRole` LOOP context ; extension `saveAll` pour persister LoopPadStore ; hard-constraint exit |
| `src/setup/ToolControlPads.h` | 3.B | Forward declaration `LoopPadStore` (déjà via KeyboardData.h) ; pas de modif struct |
| `src/setup/ToolControlPads.cpp` | 3.B | Ajout check `isLoopControlPad` avant `_addSlot` (1 endroit) + avant édition `padIndex` (handler `_handlePropEdit` / `_handleValueEdit`) ; ajout grid render dim red lock pour LOOP controls |
| `src/managers/NvsManager.h` | 3.E, 3.G | Suppression déclaration `applyDevSeedLoopPadsIfSafe()` ; ajout signature `saveLoopPad()` si manquante (déjà présent ? à vérifier task) |
| `src/managers/NvsManager.cpp` | 3.E, 3.G | Suppression implémentation `applyDevSeedLoopPadsIfSafe` (lignes 1196-1230) ; suppression commentaire dev seed dans `loadAll` (ligne 162) ; ajout collision check secondaire post-loadAll (EC9 §17 spec) |
| `src/main.cpp` | 3.G | Suppression call `s_nvsManager.applyDevSeedLoopPadsIfSafe()` (ligne 538) |
| `docs/reference/setup-tools-conventions.md` | 3.H | Nouvelle section "Pad role collisions" : 5 règles + table compatibilité + 5 helpers + pattern TAB navigation |
| `docs/reference/nvs-reference.md` | 3.H | LoopPadStore : DECLARED → WIRED Phase 3 ; defaults documentés |
| `docs/reference/architecture-briefing.md` | 3.H | Section setup-mode : Tool 3 b1 multi-sous-pages |
| `docs/reference/patterns-catalog.md` | 3.H | Pattern P15 "Multi sub-page tool with TAB navigation" si jugé réutilisable |
| `STATUS.md` | 3.H | Focus courant + table commits Phase 3 |
| `docs/superpowers/LOOP_PROGRESS.md` | 3.H | Phase 3 → CLOSE |
| `docs/superpowers/specs/2026-04-19-loop-mode-design.md` | 3.H | §27 P3 corriger Tool 7 ; §27 P2 ligne 611 retirer dev seed M7 ; §28 Q7 marquer IMPLEMENTED |

---

## Recap-table multi-axes

| Sous-phase | Tasks | Build | HW gate | Commit |
|---|---|---|---|---|
| 3.A — Helpers cross-store + getters NvsManager | 1-3 | ✓ | — (compile only) | 1 |
| 3.B — Tool 4 ext (refus ControlPad sur LOOP control) | 4-6 | ✓ | **G1** | 1 |
| 3.C — Tool 3 refacto nav TAB 3 sous-pages + NORM | 7-12 | ✓ | **G2** | 1 |
| 3.D — Tool 3 sous-page ARPEG (refacto sans changement métier) | 13-16 | ✓ | **G3** | 1 |
| 3.E — Tool 3 sous-page LOOP + defaults validator + hard-constraint | 17-22 | ✓ | **G4** | 1 |
| 3.F — Validation collision cross-tool (inline checks + dim cross-ctx grid) | 23-25 | ✓ | **G5** | 1 |
| 3.G — Retrait dev seed M7 + collision check secondaire post-loadAll | 26 | ✓ | **G6** | 1 |
| 3.H — Doc-sync | 27 | — (docs) | — | 1 |

**Total** : 27 tasks, 6 HW gates (G1-G6), 8 commits.

---

## Phase 3.A — Helpers cross-store + getters NvsManager

### Task 1 — Add 5 cross-store helpers to `KeyboardData.h`

**Files** :
- Modify : `src/core/KeyboardData.h` (ajout après `validateControlPadStore` ~ligne 430, avant `static_assert ScalePadStore` ~ligne 520)

- [ ] **Step 1: Read intégral des structs concernées**

Read `src/core/KeyboardData.h` lignes 380-580 pour avoir tous les structs (ControlPadStore, ScalePadStore, ArpPadStore, LoopPadStore) en contexte.

- [ ] **Step 2: Ajouter enums + structs résultat + helpers (50 lignes)**

Localiser un point d'insertion approprié dans `KeyboardData.h` après la définition de `LoopPadStore` (~ligne 555) et avant `BankTypeStore` (~ligne 557).

Code à ajouter :

```cpp
// =================================================================
// Cross-store role lookup helpers (Phase 3 — spec §13)
// =================================================================
// Source unique de vérité pour la présence d'un rôle sur un pad.
// Consommés inline par Tool 3 (ToolPadRoles), Tool 4 (ToolControlPads),
// et runtime si nécessaire. Pas de module centralisé (refusé YAGNI Phase 3).
//
// Invariant 14 : tout consommateur appelle ces helpers, pas de scan ad-hoc.

// --- LoopPadStore : controls + slots ---

inline bool isLoopControlPad(const LoopPadStore& s, uint8_t pad) {
  return s.recPad == pad || s.playStopPad == pad || s.clearPad == pad;
}

inline int8_t findLoopSlotIdx(const LoopPadStore& s, uint8_t pad) {
  for (uint8_t i = 0; i < 16; i++) {
    if (s.slotPads[i] == pad) return (int8_t)i;
  }
  return -1;
}

// --- ControlPadStore : entries indexed ---

inline int8_t findControlPadEntryIdx(const ControlPadStore& s, uint8_t pad) {
  for (uint8_t i = 0; i < s.count; i++) {
    if (s.entries[i].padIndex == pad) return (int8_t)i;
  }
  return -1;
}

// --- ScalePadStore : root / mode / chrom ---

enum class ScaleRoleKind : uint8_t { NONE, ROOT, MODE, CHROM };
struct ScaleRoleResult { ScaleRoleKind kind; uint8_t idx; };

inline ScaleRoleResult scaleRoleAtPad(const ScalePadStore& s, uint8_t pad) {
  for (uint8_t i = 0; i < 7; i++) {
    if (s.rootPads[i] == pad) return ScaleRoleResult{ScaleRoleKind::ROOT, i};
    if (s.modePads[i] == pad) return ScaleRoleResult{ScaleRoleKind::MODE, i};
  }
  if (s.chromaticPad == pad) return ScaleRoleResult{ScaleRoleKind::CHROM, 0};
  return ScaleRoleResult{ScaleRoleKind::NONE, 0};
}

// --- ArpPadStore : hold / octave ---

enum class ArpRoleKind : uint8_t { NONE, HOLD, OCTAVE };
struct ArpRoleResult { ArpRoleKind kind; uint8_t idx; };

inline ArpRoleResult arpRoleAtPad(const ArpPadStore& s, uint8_t pad) {
  if (s.holdPad == pad) return ArpRoleResult{ArpRoleKind::HOLD, 0};
  for (uint8_t i = 0; i < 4; i++) {
    if (s.octavePads[i] == pad) return ArpRoleResult{ArpRoleKind::OCTAVE, i};
  }
  return ArpRoleResult{ArpRoleKind::NONE, 0};
}

// --- BankSlot[] : bank assignment ---
// (helper externe vu que BankSlot vit dans BankManager / NvsManager pas dans
// un store dédié — signature accepte le pointeur de tableau)

struct BankSlot;  // forward (defined elsewhere)

int8_t findBankIdxForPad(const BankSlot* slots, uint8_t pad);
// Définition dans NvsManager.cpp ou helpers.cpp (impl Task 2 ci-dessous).
```

- [ ] **Step 3: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected : compile OK, 0 warning, RAM/Flash usage inchangé sensiblement (helpers inline).

- [ ] **Step 4: Auto-review grep**

```bash
grep -c "isLoopControlPad\|findLoopSlotIdx\|findControlPadEntryIdx\|scaleRoleAtPad\|arpRoleAtPad" src/core/KeyboardData.h
# Expected : >= 5 (déclarations)
```

- [ ] **Step 5: No commit yet** — Task 2 + 3 dans le même commit Phase 3.A.

---

### Task 2 — Implement `findBankIdxForPad` in `NvsManager.cpp` (or appropriate location)

**Files** :
- Modify : `src/managers/NvsManager.cpp` (ajout helper avant `applyDevSeedLoopPadsIfSafe` ou dans une zone "helpers" du fichier)

OU si pas approprié : créer un mini-fichier `src/core/PadHelpers.cpp` avec juste cette fonction.

- [ ] **Step 1: Read NvsManager.cpp structure** pour identifier la zone helpers

Read `src/managers/NvsManager.cpp` lignes 1-50 + lignes 1180-1200 (zone proche du dev seed à retirer).

- [ ] **Step 2: Choix du fichier d'implémentation**

Décision : implémenter dans `NvsManager.cpp` (cohérent avec `applyDevSeedLoopPadsIfSafe` qui sera retiré 3.G ; même NvsManager possède `getLoadedBankSlots()` typique).

**Vérifier** : la struct `BankSlot` est-elle déjà visible dans NvsManager.cpp (via include) ? Si non, ajouter `#include "../managers/BankManager.h"` ou équivalent.

- [ ] **Step 3: Implémenter**

```cpp
// Add near other helpers in NvsManager.cpp (e.g., before applyDevSeedLoopPadsIfSafe)
// Or in a dedicated section "Cross-store helpers"

int8_t findBankIdxForPad(const BankSlot* slots, uint8_t pad) {
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (slots[i].pad == pad) return (int8_t)i;
  }
  return -1;
}
```

- [ ] **Step 4: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected : compile OK.

- [ ] **Step 5: Auto-review grep**

```bash
grep -c "findBankIdxForPad" src/managers/NvsManager.cpp
# Expected : 1 (définition)
grep -c "findBankIdxForPad" src/core/KeyboardData.h
# Expected : 1 (déclaration)
```

- [ ] **Step 6: No commit yet** — wait for Task 3.

---

### Task 3 — Verify / add NvsManager getters for cross-store reads

**Files** :
- Modify (if needed) : `src/managers/NvsManager.h` / `src/managers/NvsManager.cpp`

Objectif : s'assurer que Tool 3 et Tool 4 peuvent lire les 5 stores via NvsManager. Audit présence des getters.

- [ ] **Step 1: Audit getters existants**

```bash
grep -n "getLoadedScalePads\|getLoadedArpPads\|getLoadedControlPadStore\|getLoadedLoopPadStore\|getLoadedBankSlots\|getBankSlots" src/managers/NvsManager.h
```

Expected : devraient exister :
- `const LoopPadStore& getLoadedLoopPadStore() const` (Phase 2)
- `const ControlPadStore& getLoadedControlPadStore() const` ou similar
- `const ScalePadStore& ...` 
- `const ArpPadStore& ...`
- Accès aux `BankSlot[NUM_BANKS]` (via main.cpp ou BankManager)

Si manquants : les ajouter (signature `const T& getLoadedX() const`, return `_loadedX`).

- [ ] **Step 2: Add missing getters if any**

Pour chaque getter manquant, ajouter dans NvsManager.h + body trivial dans NvsManager.cpp.

Exemple (si manquant) :
```cpp
// NvsManager.h
const ScalePadStore& getLoadedScalePads() const { return _loadedScalePad; }
const ArpPadStore&   getLoadedArpPads()   const { return _loadedArpPad; }
```

- [ ] **Step 3: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 4: Auto-review**

```bash
grep -c "getLoadedScalePads\|getLoadedArpPads\|getLoadedControlPadStore\|getLoadedLoopPadStore" src/managers/NvsManager.h
# Expected : >= 4
```

- [ ] **Step 5: Commit gate Phase 3.A**

Présenter à Loïc :

```bash
git status --porcelain
```

Fichiers à add :
- `src/core/KeyboardData.h`
- `src/managers/NvsManager.h` (si modifié)
- `src/managers/NvsManager.cpp`

Message HEREDOC :

```
feat(loop-phase-3): cross-store helpers + getters NvsManager (Phase 3.A)

5 helpers inline dans KeyboardData.h :
- isLoopControlPad / findLoopSlotIdx (LoopPadStore)
- findControlPadEntryIdx (ControlPadStore)
- scaleRoleAtPad / arpRoleAtPad (ScalePadStore / ArpPadStore)

1 helper externe :
- findBankIdxForPad (BankSlot[]) — implémenté NvsManager.cpp

Getters NvsManager confirmés / ajoutés selon audit (Step 1).

Source unique de vérité pour la présence d'un rôle sur un pad (invariant 14).
Pas de module centralisé (PadRoleArbiter refusé YAGNI). Consommation inline
par Tool 3 + Tool 4 dans phases suivantes.

Build clean. Pas de HW gate (compile only).
```

Attendre **« ok commit »** explicite.

---

## Phase 3.B — Tool 4 extension (refus ControlPad sur LOOP control)

### Task 4 — Add `isLoopControlPad` check in `_addSlot()` (Tool 4)

**Files** :
- Modify : `src/setup/ToolControlPads.cpp` (function `_addSlot`)

- [ ] **Step 1: Read `_addSlot` implementation**

Grep first :
```bash
grep -n "_addSlot\|::_addSlot" src/setup/ToolControlPads.cpp
```

Read la fonction complète (~20-40 lignes).

- [ ] **Step 2: Add check at entry of `_addSlot`**

Insérer **au début** de la fonction (après les guards triviaux mais avant tout side-effect) :

```cpp
bool ToolControlPads::_addSlot(uint8_t padIdx) {
  // ... existing guards (e.g., _wk.count >= MAX_CONTROL_PADS) ...

  // Phase 3 — refus collision avec LOOP control pads (spec §5 R2, §18 spec Phase 3)
  if (isLoopControlPad(_nvs->getLoadedLoopPadStore(), padIdx)) {
    _setFlash("Pad is LOOP REC/PS/CLR — move in Tool 3 first.");
    return false;
  }

  // ... existing body ...
}
```

**Note importante** : le format du message flash doit tenir dans `_flashMsg[80]` (cf ToolControlPads.h:58). Compter : "Pad is LOOP REC/PS/CLR — move in Tool 3 first." = 49 chars. OK.

- [ ] **Step 3: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 4: Auto-review grep**

```bash
grep -c "isLoopControlPad" src/setup/ToolControlPads.cpp
# Expected : 1 (this task) — Task 5 ajoutera une 2e occurence
```

- [ ] **Step 5: No commit yet** — Tasks 4-6 mêmes commit Phase 3.B.

---

### Task 5 — Add check in `_handleValueEdit` / `_handlePropEdit` for padIndex changes

**Files** :
- Modify : `src/setup/ToolControlPads.cpp` (handler de l'édition `padIndex`)

- [ ] **Step 1: Locate padIndex edit logic**

```bash
grep -n "padIndex\|_handleValueEdit\|_handlePropEdit\|_adjustField" src/setup/ToolControlPads.cpp | head -20
```

Identifier où `_wk.entries[i].padIndex` peut être modifié interactivement. Si l'UI Tool 4 ne permet pas d'éditer `padIndex` après ajout (uniquement via remove + re-add), cette task peut être un no-op — vérifier.

- [ ] **Step 2: Add check si applicable**

Si `padIndex` peut être édité :
```cpp
// Before assigning new padIndex value
if (isLoopControlPad(_nvs->getLoadedLoopPadStore(), newPadIdx)) {
  _setFlash("Pad is LOOP REC/PS/CLR — move in Tool 3 first.");
  return;  // ou équivalent rejet
}
```

Si pas applicable (padIndex non éditable post-add) : documenter par un commentaire dans le code Tool 4 que la protection se fait uniquement à l'ajout (`_addSlot`).

- [ ] **Step 3: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 4: No commit yet**.

---

### Task 6 — Tool 4 grid render : LOOP REC/PS/CLEAR locked dim red

**Files** :
- Modify : `src/setup/ToolControlPads.cpp` (function `_drawGrid`)

- [ ] **Step 1: Read `_drawGrid`**

```bash
grep -n "_drawGrid\|::_drawGrid" src/setup/ToolControlPads.cpp
```

Read la fonction complète.

- [ ] **Step 2: Add LOOP control check in grid loop**

Dans la boucle cell par cell de `_drawGrid`, ajouter une branche pour les pads qui sont LOOP controls :

```cpp
// In _drawGrid, inside the cell-by-cell loop :
for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
  // ... existing logic (bank check, controlpad check, etc.) ...

  // Phase 3 — Display LOOP controls as locked dim red
  if (isLoopControlPad(_nvs->getLoadedLoopPadStore(), pad)) {
    const LoopPadStore& lp = _nvs->getLoadedLoopPadStore();
    const char* label = (pad == lp.recPad)      ? " R"
                      : (pad == lp.playStopPad) ? " P"
                                                : " C";
    // Render cell with dim red color + label, mark as non-editable
    // (suivre le pattern existing pour bank pads locked)
    ...
    continue;  // skip default rendering
  }

  // ... existing rendering ...
}
```

- [ ] **Step 3: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 4: HW Gate G1 — Tool 4 ext refus + grid render**

Upload + monitor :

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

**Pré-requis HW** :
- LoopPadStore avec recPad=30, playStopPad=31, clearPad=32 (par dev seed M7 actif Phase 2 jusqu'à 3.G).
- ControlPadStore avec au moins 0 ou 1 ControlPad existant.

**Procédure de test** :
1. Boot, vérifier badges Tool 1-8 normaux.
2. Setup mode → Tool 4 ControlPads.
3. Naviguer dans le grid, observer cells aux pads 30/31/32 → afficher `R`/`P`/`C` dim red.
4. Tenter d'ajouter un ControlPad sur pad 30 (cursor sur pad 30, action add) → refus + flash msg "Pad is LOOP REC/PS/CLR — move in Tool 3 first.".
5. Tenter idem sur pad 31, pad 32 → mêmes refus.
6. Vérifier qu'un ControlPad assigné sur pad 5 (non-LOOP) fonctionne normalement.

**Critères G1** :
- ✓ Grid Tool 4 affiche LOOP controls comme cells locked dim red avec labels `R`/`P`/`C`.
- ✓ Refus tentative add ControlPad sur pad 30 → flash msg visible.
- ✓ ControlPadStore inchangé après tentative.
- ✓ Add ControlPad sur pad non-LOOP fonctionne (non-régression).

Autoriser l'upload, tester. **Répondre `GO` ou `validé` quand G1 OK.**

- [ ] **Step 5: Commit gate Phase 3.B**

Présenter à Loïc :

```bash
git status --porcelain
```

Fichiers à add :
- `src/setup/ToolControlPads.cpp`

Message HEREDOC :

```
feat(loop-phase-3): Tool 4 ext refus ControlPad sur LOOP control (Phase 3.B)

3 modifs ToolControlPads.cpp :
1. _addSlot : check isLoopControlPad au début, refus + flash msg si match.
2. _handleValueEdit/_handlePropEdit : check symétrique sur padIndex edit
   (si applicable per audit).
3. _drawGrid : render LOOP controls (pad recPad/playStopPad/clearPad) comme
   cells locked dim red avec labels 1-char R/P/C.

Cohérent règle R2 spec LOOP §5 (layer musical exclusion). Helper consommé :
isLoopControlPad(LoopPadStore, pad). Pas de modif autre.

HW gate G1 validé : refus visible, flash msg, grid render dim red, non-régression
add ControlPad sur pad non-LOOP.
```

Attendre **« ok commit »** explicite.

---

## Phase 3.C — Tool 3 refacto nav TAB 3 sous-pages + sous-page NORM

### Task 7 — Add `SubPage` enum + `_activeSubPage` state + `_wkLoopPad` working copy in `ToolPadRoles.h`

**Files** :
- Modify : `src/setup/ToolPadRoles.h`

- [ ] **Step 1: Read intégral ToolPadRoles.h**

Read `src/setup/ToolPadRoles.h` (112 lignes, complet).

- [ ] **Step 2: Add SubPage enum + members + new helpers**

Dans `ToolPadRoles.h`, ajouter (proche des enum existants ou en début de classe) :

```cpp
// Phase 3 — sous-page contexte
enum SubPage : uint8_t {
  SUB_NORM  = 0,   // Bank pads assignment
  SUB_ARPEG = 1,   // 20 roles (root/mode/chrom/hold/octave)
  SUB_LOOP  = 2,   // 19 roles (3 controls + 16 slots)
  SUB_COUNT = 3
};
```

Dans la classe `ToolPadRoles`, après les membres existants (`_octavePads`, etc.) :

```cpp
private:
  // ... existing members ...

  // Phase 3 — sous-page active + working copy LoopPadStore
  SubPage      _activeSubPage;
  LoopPadStore _wkLoopPad;       // working copy for Tool 3 sous-page LOOP

  // Phase 3 — helpers
  void _handleTab();             // cycle sous-page NORM → ARPEG → LOOP → NORM
  void _drawSubPageHeader();     // affiche [NORM|ARPEG|LOOP] highlighted

  // Extended pool / role logic per sous-page
  uint8_t poolLineSize_loop(uint8_t line) const;   // pool LOOP-specific
  const char* poolItemLabel_loop(uint8_t line, uint8_t index) const;

  // LOOP-specific role helpers
  void assignLoopRole(uint8_t pad, uint8_t line, uint8_t index);
  void clearLoopRole(uint8_t pad);
  bool hardConstraintLoopControlsAssigned() const;   // returns true if rec/PS/CLR != 0xFF
```

Constructor init list : ajouter `_activeSubPage(SUB_NORM)` et initialiser `_wkLoopPad` (memset 0xFF or default).

- [ ] **Step 3: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Attendu : compile OK (déclarations non utilisées encore — Task 8+ implémente).

- [ ] **Step 4: No commit yet** — Phase 3.C bundle Tasks 7-12.

---

### Task 8 — Initialize `_wkLoopPad` in constructor + load from NvsManager in `begin()`

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp` (constructor + begin)

- [ ] **Step 1: Update constructor init list**

Localiser le constructeur (`ToolPadRoles::ToolPadRoles()` ligne ~58).

Ajouter dans init list `_activeSubPage(SUB_NORM)`.

Dans le body, ajouter :
```cpp
// Init _wkLoopPad with sentinel values (will be loaded in begin())
memset(&_wkLoopPad, 0, sizeof(_wkLoopPad));
_wkLoopPad.recPad      = 0xFF;
_wkLoopPad.playStopPad = 0xFF;
_wkLoopPad.clearPad    = 0xFF;
for (uint8_t i = 0; i < 16; i++) _wkLoopPad.slotPads[i] = 0xFF;
```

- [ ] **Step 2: Update begin() to accept NvsManager and load _wkLoopPad**

Modifier la signature `begin()` pour accepter un `NvsManager*` (pour load le LoopPadStore loaded). Voir signature actuelle (ToolPadRoles.h:32).

Option A — étendre signature :
```cpp
void begin(CapacitiveKeyboard* keyboard, LedController* leds,
           SetupUI* ui, NvsManager* nvs,    // <-- NEW PARAM
           uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
           uint8_t& chromaticPad, uint8_t& holdPad,
           uint8_t* octavePads);
```

Option B — ajouter méthode séparée `setNvsManager(NvsManager* nvs)`.

Reco : Option A pour cohérence Tool 4 (qui prend `NvsManager*` dans `begin()`).

**Vérifier call site** : grep `_padRolesTool.begin` ou `padRolesTool.begin` dans `SetupManager.cpp` pour adapter.

```bash
grep -rn "padRolesTool\.begin\|_padRolesTool\.begin\|padroles.*begin" src/setup/ src/main.cpp
```

Adapter le call site pour passer le `NvsManager*`.

Dans `begin()`, après assignement des autres pointeurs :
```cpp
_nvs = nvs;
// Load LoopPadStore working copy from NvsManager loaded state
_wkLoopPad = nvs->getLoadedLoopPadStore();
```

- [ ] **Step 3: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 4: Auto-review grep**

```bash
grep -c "_wkLoopPad\|_activeSubPage" src/setup/ToolPadRoles.cpp
# Expected : >= 5 (init, begin, plus tard handlers Tasks suivantes)
```

- [ ] **Step 5: No commit yet**.

---

### Task 9 — Implement TAB key handler `_handleTab` + integrate in `run()`

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp` (`run()` event loop)

- [ ] **Step 1: Read `run()` event loop**

Read `ToolPadRoles::run()` (ligne ~530 jusqu'à fin).

Identifier comment les keys sont parsées (`_input` parser, `NavEvent` ou équivalent).

- [ ] **Step 2: Check InputParser supports TAB**

```bash
grep -n "TAB\|VK_TAB\|tab\|'\\\\t'" src/setup/InputParser.{h,cpp}
```

Si InputParser ne parse pas TAB (ANSI `\t` ou escape `\e[Z`) → étendre. Sinon utiliser le code existant.

Si extension nécessaire :
```cpp
// InputParser.h — add to NavEventType enum
enum NavEventType { ... NAV_TAB, ... };

// InputParser.cpp — detect TAB in input stream
// ASCII TAB = 0x09 ('\t')
if (c == '\t') {
  ev.type = NAV_TAB;
  return ev;
}
```

- [ ] **Step 3: Implement `_handleTab`**

Dans `ToolPadRoles.cpp`, ajouter (après `_handleTab` declaration en .h) :

```cpp
void ToolPadRoles::_handleTab() {
  _activeSubPage = (SubPage)((_activeSubPage + 1) % SUB_COUNT);
  _gridRow = 0; _gridCol = 0;   // reset cursor
  _editing = false;
  _poolLine = 0; _poolIdx = 0;
  buildRoleMap();                // rebuild for new sub-page context
  drawScreen();                  // full redraw
}
```

- [ ] **Step 4: Integrate TAB dispatch in `run()`**

Localiser le switch / if-chain pour les `NavEvent` types. Ajouter avant le default case :

```cpp
case NAV_TAB:
  _handleTab();
  break;
```

- [ ] **Step 5: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 6: No commit yet**.

---

### Task 10 — Add `_drawSubPageHeader` + integrate in `drawScreen`

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp` (drawScreen + new method)

- [ ] **Step 1: Read `drawScreen` (ligne ~499)**

- [ ] **Step 2: Implement `_drawSubPageHeader`**

```cpp
void ToolPadRoles::_drawSubPageHeader() {
  // Affiche en haut du tool : "Tool 3 — Pad Roles [NORM|ARPEG|LOOP]"
  // avec la sous-page active highlighted (inverse ou couleur).
  //
  // Pattern UI cohérent avec Tool 5 6-fields header.
  // Référence VT100 design : docs/reference/vt100-design-guide.md.

  _ui->moveCursor(1, 1);  // adapt to actual SetupUI API
  _ui->print("Tool 3 - Pad Roles  [");

  const char* labels[SUB_COUNT] = { "NORM", "ARPEG", "LOOP" };
  for (uint8_t i = 0; i < SUB_COUNT; i++) {
    if (i == _activeSubPage) {
      _ui->setInverse(true);    // or setColor(active)
    }
    _ui->print(labels[i]);
    if (i == _activeSubPage) {
      _ui->setInverse(false);
    }
    if (i < SUB_COUNT - 1) _ui->print("|");
  }
  _ui->print("]");
}
```

**Note** : adapter à l'API SetupUI réelle (lire SetupUI.h pour les méthodes disponibles).

- [ ] **Step 3: Integrate in `drawScreen`**

Au début de `drawScreen` (avant les autres draws), ajouter :
```cpp
_drawSubPageHeader();
```

- [ ] **Step 4: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 5: No commit yet**.

---

### Task 11 — Refacto `drawGrid` / `drawPool` / `drawControlBar` to respect `_activeSubPage`

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp` (`drawGrid`, `drawPool`, `drawControlBar`)

**Note importante** : c'est la plus grosse task Phase 3.C. Phase 3.D (Task 13-16) traitera spécifiquement la sous-page ARPEG. Phase 3.E (Tasks 17-22) traitera la sous-page LOOP. Pour Phase 3.C, l'objectif est juste de **rendre la sous-page NORM isolée** (n'affiche que les bank roles, le grid montre les autres rôles existants mais en dim non-éditable).

- [ ] **Step 1: Read `drawGrid` (~ligne 314)**

- [ ] **Step 2: Conditionner `drawGrid` selon `_activeSubPage`**

Pattern : factoriser le scan grid en 3 fonctions privées :
- `_drawGridNorm()` — bank pads éditables + autres dim
- `_drawGridArpeg()` — Tasks 13-16
- `_drawGridLoop()` — Tasks 17-22

Pour Phase 3.C, implémenter `_drawGridNorm()` :

```cpp
void ToolPadRoles::_drawGridNorm() {
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    // Determine role at this pad
    // - Check _wkBankPads[] : is this pad a bank ? (editable)
    // - Check working copies ARPEG (scale/arp roles : dim non-editable in NORM)
    // - Check _wkLoopPad : is this pad LOOP role ? (dim non-editable in NORM)
    // - Check ControlPadStore (loaded) : is this pad ControlPad ? (dim non-editable)
    // - Else : libre

    int8_t bankIdx = -1;
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (_wkBankPads[i] == pad) { bankIdx = (int8_t)i; break; }
    }

    if (bankIdx >= 0) {
      // Bank pad — editable, vert active
      char label[8];
      snprintf(label, sizeof(label), " B%d", bankIdx + 1);
      _renderCell(pad, label, COLOR_BANK_ACTIVE, /*editable=*/true);
      continue;
    }

    // Check other roles (read-only in NORM sub-page)
    if (isLoopControlPad(_wkLoopPad, pad)) {
      const char* lbl = (pad == _wkLoopPad.recPad) ? " R"
                      : (pad == _wkLoopPad.playStopPad) ? " P"
                                                        : " C";
      _renderCell(pad, lbl, COLOR_OTHER_ROLE_DIM, /*editable=*/false);
      continue;
    }

    int8_t loopSlot = findLoopSlotIdx(_wkLoopPad, pad);
    if (loopSlot >= 0) {
      char label[8];
      snprintf(label, sizeof(label), "S%02d", loopSlot);
      _renderCell(pad, label, COLOR_OTHER_ROLE_DIM, /*editable=*/false);
      continue;
    }

    // Check ARPEG roles via helpers
    ScaleRoleResult sr = scaleRoleAtPad(_nvs->getLoadedScalePads(), pad);
    if (sr.kind != ScaleRoleKind::NONE) {
      const char* lbl = (sr.kind == ScaleRoleKind::ROOT) ? "R" :
                        (sr.kind == ScaleRoleKind::MODE) ? "M" : "Ch";
      _renderCell(pad, lbl, COLOR_OTHER_ROLE_DIM, false);
      continue;
    }

    ArpRoleResult ar = arpRoleAtPad(_nvs->getLoadedArpPads(), pad);
    if (ar.kind != ArpRoleKind::NONE) {
      const char* lbl = (ar.kind == ArpRoleKind::HOLD) ? "Hd" : "Oc";
      _renderCell(pad, lbl, COLOR_OTHER_ROLE_DIM, false);
      continue;
    }

    // Check ControlPad
    int8_t cpIdx = findControlPadEntryIdx(_nvs->getLoadedControlPadStore(), pad);
    if (cpIdx >= 0) {
      _renderCell(pad, "cp", COLOR_OTHER_ROLE_DIM, false);
      continue;
    }

    // Empty cell
    _renderCell(pad, " .", COLOR_EMPTY, /*editable=*/true);
  }
}
```

**Note** : `_renderCell` est une abstraction — voir le pattern existing dans `drawGrid` pour le moteur de rendu cell-by-cell. Adapter à l'API réelle.

Dispatch dans `drawGrid()` :
```cpp
void ToolPadRoles::drawGrid() {
  switch (_activeSubPage) {
    case SUB_NORM:  _drawGridNorm(); break;
    case SUB_ARPEG: _drawGridArpeg(); break;
    case SUB_LOOP:  _drawGridLoop(); break;
  }
}
```

**Phase 3.C scope** : `_drawGridArpeg` et `_drawGridLoop` peuvent être stubs (appelent l'ancien `drawGrid` logic monolithique en attendant Task 13 et Task 17).

- [ ] **Step 3: Conditioner `drawPool` selon sous-page**

Pattern similaire — pool différent pour NORM (1 ligne 8 banks) vs ARPEG (5 lignes) vs LOOP (2 sections).

Pour Phase 3.C, implémenter `_drawPoolNorm()` :
```cpp
void ToolPadRoles::_drawPoolNorm() {
  // 1 ligne : "Banks: Bk1 Bk2 Bk3 ... Bk8"
  _ui->setRowColPool();  // adapt
  _ui->print("Banks: ");
  for (uint8_t i = 0; i < 8; i++) {
    if (_poolLine == 1 && _poolIdx == i) _ui->setInverse(true);
    _ui->printf(" %s", POOL_BANK_LABELS[i]);
    if (_poolLine == 1 && _poolIdx == i) _ui->setInverse(false);
  }
}
```

- [ ] **Step 4: Conditioner `drawControlBar`**

Ajouter mention TAB key dans le control bar :
```cpp
void ToolPadRoles::drawControlBar() {
  _ui->moveCursor(ROW_CONTROLBAR, 1);
  _ui->print("TAB=sub-page  ARROWS=nav  ENTER=assign  CLEAR=reset  ESC=exit");
}
```

- [ ] **Step 5: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 6: No commit yet**.

---

### Task 12 — Sous-page NORM bank slot move : refus si destination occupée + commit Phase 3.C

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp` (`assignRole` for bank)

- [ ] **Step 1: Read `assignRole` (ligne ~195)**

- [ ] **Step 2: Add collision check for bank moves**

Dans `assignRole`, quand `line == 1` (bank) :

```cpp
void ToolPadRoles::assignRole(uint8_t pad, uint8_t line, uint8_t index) {
  if (line == 1) {  // bank
    // Check destination is free of other roles (R1 sacré — pas de coexistence Bank)
    if (isLoopControlPad(_wkLoopPad, pad) ||
        findLoopSlotIdx(_wkLoopPad, pad) >= 0 ||
        scaleRoleAtPad(_nvs->getLoadedScalePads(), pad).kind != ScaleRoleKind::NONE ||
        arpRoleAtPad(_nvs->getLoadedArpPads(), pad).kind != ArpRoleKind::NONE ||
        findControlPadEntryIdx(_nvs->getLoadedControlPadStore(), pad) >= 0) {
      // Flash message — pad has another role, refuse
      // (suivre pattern flash existing or _setFlash equivalent in Tool 3)
      // Si _setFlash n'existe pas dans Tool 3 actuel, créer un équivalent.
      ...
      return;
    }
  }
  // ... existing assignment logic ...
}
```

**Note** : Tool 3 actuel n'a pas `_setFlash` équivalent — pattern à introduire si manquant. Mécanisme : `_flashMsg[80]` + `_flashExpireMs` + check à chaque `drawScreen`. Cf Tool 4 pattern.

- [ ] **Step 3: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 4: HW Gate G2 — Tool 3 nav TAB + sous-page NORM**

Upload + monitor.

**Pré-requis HW** :
- NVS valide avec assignements existants (bank pads + scale/arp roles + LOOP defaults 30/31/32).

**Procédure de test** :
1. Boot → setup mode → Tool 3 Pad Roles.
2. Header affiche `Tool 3 — Pad Roles [NORM|ARPEG|LOOP]`, sous-page NORM highlighted.
3. Grid affiche bank pads `B1..B8` active vert, autres rôles dim (ARPEG roles, LOOP roles, ControlPads visibles dim non-editable).
4. Press TAB → switch vers ARPEG (header highlight bouge, grid refresh — peut être stub Phase 3.C).
5. Press TAB → switch vers LOOP.
6. Press TAB → retour NORM.
7. En NORM, naviguer cursor sur bank pad, déplacer vers pad libre via pool → bank move OK.
8. Tenter de bouger bank vers pad qui a Root C ARPEG → refus + flash msg.
9. Vérifier non-régression : sortie Tool 3 propage bien les bank pad changes en NVS (post-reboot, bank pad nouveau actif).

**Critères G2** :
- ✓ Header `[NORM|ARPEG|LOOP]` affiché correctement, sous-page active highlighted.
- ✓ TAB cycle nav fonctionne, pas de freeze.
- ✓ Sous-page NORM grid : bank pads actifs verts, autres rôles dim non-editable.
- ✓ Bank move via pool OK.
- ✓ Refus avec flash msg si destination occupée par autre rôle.
- ✓ Save bank assignement persiste (reboot test).

Autoriser l'upload, tester. **Répondre `GO` ou `validé` quand G2 OK.**

- [ ] **Step 5: Commit gate Phase 3.C**

Fichiers à add :
- `src/setup/ToolPadRoles.h`
- `src/setup/ToolPadRoles.cpp`
- `src/setup/InputParser.{h,cpp}` (si TAB ajouté)
- Call site `begin()` ajusté (SetupManager.cpp ou main.cpp)

Message HEREDOC :

```
feat(loop-phase-3): Tool 3 b1 nav TAB + sous-page NORM (Phase 3.C)

Refacto Tool 3 (ToolPadRoles) pour nav 3 sous-pages contextuelles :
- Enum SubPage {SUB_NORM, SUB_ARPEG, SUB_LOOP}, état _activeSubPage init NORM.
- Working copy _wkLoopPad: LoopPadStore (loaded depuis NvsManager.begin).
- TAB key handler cycle NORM → ARPEG → LOOP → NORM (InputParser étendu si besoin).
- _drawSubPageHeader : "[NORM|ARPEG|LOOP]" active highlighted.
- drawGrid dispatch selon _activeSubPage : _drawGridNorm livré, _drawGridArpeg
  et _drawGridLoop stubs (Phase 3.D / 3.E).
- _drawGridNorm : bank pads actifs verts, autres rôles dim non-editable
  (via 5 helpers cross-store).
- drawPool : pool 1-ligne Banks pour sous-page NORM.
- drawControlBar : ajout "TAB=sub-page" hint.
- assignRole bank : check collision destination (refus si autre rôle existant)
  + flash msg (pattern _setFlash introduit dans Tool 3).

HW gate G2 validé : nav TAB OK, sous-page NORM bank move + refus collision OK,
save persisté.
```

Attendre **« ok commit »** explicite.

---

## Phase 3.D — Tool 3 sous-page ARPEG (refacto sans changement métier)

### Task 13 — Implement `_drawGridArpeg`

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp`

- [ ] **Step 1: Implement `_drawGridArpeg`**

Mirror de `_drawGridNorm` mais avec :
- Rôles ARPEG (root/mode/chrom/hold/octave) actifs verts éditables.
- Bank pads dim red **locked** (R1).
- ControlPad / LOOP roles dim **éditables** (R5 cross-layer pour CP, R4 inter-ctx pour Slot ; R5 cross-layer pour REC/PS/CLEAR).

```cpp
void ToolPadRoles::_drawGridArpeg() {
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    // Bank pads : locked dim red (R1)
    int8_t bankIdx = -1;
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (_wkBankPads[i] == pad) { bankIdx = (int8_t)i; break; }
    }
    if (bankIdx >= 0) {
      char label[8]; snprintf(label, sizeof(label), " B%d", bankIdx + 1);
      _renderCell(pad, label, COLOR_BANK_LOCKED, /*editable=*/false);
      continue;
    }

    // ARPEG roles active : check working copies _wkRootPads, _wkModePads, _wkChromPad, _wkHoldPad, _wkOctavePads
    // ... scan + render vert active editable ...

    // LOOP roles : dim non-editable in ARPEG sub-page (info only, R4/R5 visible)
    if (isLoopControlPad(_wkLoopPad, pad)) {
      // ... render dim with marker like "·R" ...
      continue;
    }
    // ... etc for slots, ControlPads ...
  }
}
```

- [ ] **Step 2: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 3: No commit yet**.

---

### Task 14 — Implement `_drawPoolArpeg`

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp`

- [ ] **Step 1: Implement**

Reprendre la logique pool existante (5 lignes : root×7 / mode×7+chrom / octave×4 / hold×1) dans `_drawPoolArpeg`. C'est une simple extraction du code monolithique existant.

- [ ] **Step 2: Build**

- [ ] **Step 3: No commit yet**.

---

### Task 15 — Verify ARPEG assignement (no-op non-régression)

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp` (`assignRole` pour line 2-5 — root/mode/octave/hold)

- [ ] **Step 1: Vérifier que les chemins existing fonctionnent**

`assignRole(pad, 2, idx)` pour root — vérifier que le path est intact malgré refacto.

- [ ] **Step 2: Ajouter check collision intra-ctx avec swap-to-pool R3**

Quand user assigne Root C sur pad déjà Root D (intra-ctx ARPEG hold-left, R3) :
- Swap-to-pool : pad précédent Root C → 0xFF, pad nouveau → Root C.
- Flash msg "Root C returned to pool — re-assign manually." si évincement.

```cpp
void ToolPadRoles::assignRole(uint8_t pad, uint8_t line, uint8_t index) {
  // ... existing code ...

  if (line == 2 /* root */ || line == 3 /* mode/chrom */ || ...) {
    // Check intra-ctx ARPEG hold-left collision
    ScaleRoleResult srExisting = scaleRoleAtPad(/* working ARPEG */, pad);
    ArpRoleResult arExisting = arpRoleAtPad(/* working ARPEG */, pad);

    // Si déjà occupé par autre rôle ARPEG → swap-to-pool R3
    if (srExisting.kind != ScaleRoleKind::NONE || arExisting.kind != ArpRoleKind::NONE) {
      // Évincement vers pool : reset l'ancien pad du rôle existant
      // (l'ancien pad correspondant devient 0xFF dans _wkXxxPads)
      _evictArpegRoleFromPad(pad);   // helper to implement
      _setFlash("Previous role returned to pool.");
    }
    // ... place new role ...
  }
}
```

- [ ] **Step 3: Build**

- [ ] **Step 4: No commit yet**.

---

### Task 16 — HW Gate G3 (non-régression ARPEG) + commit Phase 3.D

- [ ] **Step 1: HW Gate G3 — Sous-page ARPEG non-régression**

Upload + monitor.

**Pré-requis HW** :
- Bank ARPEG existante (créée via Tool 5).
- Assignements scale roles existing (root/mode/chrom/hold/octave).

**Procédure de test** :
1. Boot → setup mode → Tool 3.
2. TAB pour aller en sous-page ARPEG.
3. Header highlight sur ARPEG.
4. Grid affiche les rôles ARPEG actifs verts (root/mode/chrom/hold/octave).
5. Bank pads dim red locked.
6. LOOP roles dim visibles éditables.
7. ControlPad dim visible éditable.
8. Pool affiche 5 lignes (root/mode/octave/hold).
9. Bouger Root D vers un pad libre → OK.
10. Bouger Root C vers pad déjà Root D → swap-to-pool R3, flash msg.
11. Save + reboot → assignements persistent.
12. Vérifier non-régression : ARPEG bank joue normalement post-reboot (test runtime brief — bank ARPEG vivant, scale change).

**Critères G3** :
- ✓ Sous-page ARPEG affiche correctement rôles + autres rôles cross-context dim.
- ✓ Non-régression : assignements ARPEG persistent post-reboot.
- ✓ Bank ARPEG fonctionne en runtime (scale change OK).
- ✓ Swap-to-pool R3 visible si conflit intra-ctx.

Autoriser l'upload, tester. **Répondre `GO` ou `validé` quand G3 OK.**

- [ ] **Step 2: Commit gate Phase 3.D**

Message HEREDOC :

```
feat(loop-phase-3): Tool 3 sous-page ARPEG (Phase 3.D)

Refacto extraction du chemin ARPEG existant en _drawGridArpeg / _drawPoolArpeg.
Pas de changement métier — assignements scale roles inchangés.

Ajouts :
- Bank pads : dim red locked en sous-page ARPEG (R1 sacré).
- LOOP roles + ControlPads visibles dim éditables (R4/R5 visualisation).
- Swap-to-pool R3 inter-ctx ARPEG (si user place Root C sur pad déjà Root D).
- Flash msg sur évincement.

HW gate G3 validé : non-régression ARPEG OK, swap-to-pool R3 visible.
```

Attendre **« ok commit »** explicite.

---

## Phase 3.E — Tool 3 sous-page LOOP + defaults validator + hard-constraint

### Task 17 — Add defaults LOOP 30/31/32 in `KeyboardData.h` + extend `validateLoopPadStore`

**Files** :
- Modify : `src/core/KeyboardData.h`

- [ ] **Step 1: Read current LoopPadStore validator (si existe)**

```bash
grep -n "validateLoopPadStore\|LoopPadStore.*valid" src/core/KeyboardData.h src/managers/NvsManager.cpp
```

- [ ] **Step 2: Add defaults + extend validator**

Dans `KeyboardData.h`, près de LoopPadStore declaration :

```cpp
// Phase 3 — defaults LOOP controls (hardcodés, modifiable par recompile)
constexpr uint8_t LOOP_REC_DEFAULT_PAD       = 30;
constexpr uint8_t LOOP_PLAYSTOP_DEFAULT_PAD  = 31;
constexpr uint8_t LOOP_CLEAR_DEFAULT_PAD     = 32;

inline void validateLoopPadStore(LoopPadStore& s) {
  // Magic / version handled by descriptor logic (no-op here if not).

  // Apply Phase 3 defaults if controls unassigned (NVS empty case).
  if (s.recPad == 0xFF)      s.recPad      = LOOP_REC_DEFAULT_PAD;
  if (s.playStopPad == 0xFF) s.playStopPad = LOOP_PLAYSTOP_DEFAULT_PAD;
  if (s.clearPad == 0xFF)    s.clearPad    = LOOP_CLEAR_DEFAULT_PAD;

  // Slots restent 0xFF si non-assignés (tolerated).
  // Sanity : pad indices must be < NUM_KEYS (else clamp à 0xFF + warning).
  if (s.recPad >= NUM_KEYS)      s.recPad = LOOP_REC_DEFAULT_PAD;
  if (s.playStopPad >= NUM_KEYS) s.playStopPad = LOOP_PLAYSTOP_DEFAULT_PAD;
  if (s.clearPad >= NUM_KEYS)    s.clearPad = LOOP_CLEAR_DEFAULT_PAD;
  for (uint8_t i = 0; i < 16; i++) {
    if (s.slotPads[i] >= NUM_KEYS) s.slotPads[i] = 0xFF;
  }
}
```

- [ ] **Step 3: Wire validator in NVS load path**

Vérifier que `validateLoopPadStore()` est appelé après chaque `loadBlob` LoopPadStore. Si pas, ajouter l'appel.

```bash
grep -n "validateLoopPadStore\|loadBlob.*loop\|LOOPPAD" src/managers/NvsManager.cpp
```

Identifier le path de load et ajouter `validateLoopPadStore(_loadedLoopPad);` post-load.

- [ ] **Step 4: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 5: No commit yet**.

---

### Task 18 — Implement `_drawGridLoop`

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp`

- [ ] **Step 1: Implement `_drawGridLoop`**

Pattern :
- Bank pads dim red locked (R1).
- LOOP REC/PS/CLEAR actifs verts éditables (move only).
- LOOP slots actifs verts éditables (move + clear OK).
- ControlPads dim grey :
  - éditable pour Slot LOOP (R5 cross-layer Slot/CP).
  - **non-éditable** pour REC/PS/CLEAR (R2 + carriesConfig → refus).
- ARPEG roles dim éditables (R4 inter-ctx pour Slot, R5 cross-layer pour REC/PS/CLEAR).

```cpp
void ToolPadRoles::_drawGridLoop() {
  for (uint8_t pad = 0; pad < NUM_KEYS; pad++) {
    // Bank → locked R1
    int8_t bankIdx = -1;
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (_wkBankPads[i] == pad) { bankIdx = (int8_t)i; break; }
    }
    if (bankIdx >= 0) {
      char label[8]; snprintf(label, sizeof(label), " B%d", bankIdx + 1);
      _renderCell(pad, label, COLOR_BANK_LOCKED, /*editable=*/false);
      continue;
    }

    // LOOP controls : active editable (with hard-constraint move-only logic in handler)
    if (pad == _wkLoopPad.recPad)      { _renderCell(pad, "REC", COLOR_LOOP_ACTIVE, /*editable=*/true); continue; }
    if (pad == _wkLoopPad.playStopPad) { _renderCell(pad, "PS_", COLOR_LOOP_ACTIVE, /*editable=*/true); continue; }
    if (pad == _wkLoopPad.clearPad)    { _renderCell(pad, "CLR", COLOR_LOOP_ACTIVE, /*editable=*/true); continue; }

    // LOOP slot active
    int8_t slotIdx = findLoopSlotIdx(_wkLoopPad, pad);
    if (slotIdx >= 0) {
      char label[8]; snprintf(label, sizeof(label), "S%02d", slotIdx);
      _renderCell(pad, label, COLOR_LOOP_SLOT_ACTIVE, true);
      continue;
    }

    // ControlPad : visible, editable only for slot (not for REC/PS/CLEAR)
    int8_t cpIdx = findControlPadEntryIdx(_nvs->getLoadedControlPadStore(), pad);
    if (cpIdx >= 0) {
      _renderCell(pad, "cp", COLOR_OTHER_LAYER_DIM, /*editable=*/true /* conditional in handler */);
      continue;
    }

    // ARPEG role : visible dim, editable (R4/R5)
    ScaleRoleResult sr = scaleRoleAtPad(_nvs->getLoadedScalePads(), pad);
    if (sr.kind != ScaleRoleKind::NONE) {
      // ... render dim with marker like "·R0" ...
      continue;
    }
    // ... etc for arp roles ...

    // Empty
    _renderCell(pad, " .", COLOR_EMPTY, true);
  }
}
```

- [ ] **Step 2: Build**

- [ ] **Step 3: No commit yet**.

---

### Task 19 — Implement `_drawPoolLoop`

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp`

- [ ] **Step 1: Implement pool LOOP 2-sections layout**

```cpp
void ToolPadRoles::_drawPoolLoop() {
  // Section 1 : Controls (3 items)
  _ui->moveCursor(ROW_POOL, 1);
  _ui->print("Controls: ");
  const char* controlLabels[3] = { "REC", "PS_", "CLR" };
  for (uint8_t i = 0; i < 3; i++) {
    bool active = (_poolLine == /*POOL_LINE_LOOP_CONTROLS*/ 6) && (_poolIdx == i);
    if (active) _ui->setInverse(true);
    _ui->printf(" %s", controlLabels[i]);
    if (active) _ui->setInverse(false);
  }

  // Section 2 : Slots (16 items en 2 rows de 8)
  _ui->moveCursor(ROW_POOL + 1, 1);
  _ui->print("Slots:    ");
  for (uint8_t i = 0; i < 8; i++) {
    bool active = (_poolLine == /*POOL_LINE_LOOP_SLOTS*/ 7) && (_poolIdx == i);
    if (active) _ui->setInverse(true);
    _ui->printf(" S%02d", i);
    if (active) _ui->setInverse(false);
  }
  _ui->moveCursor(ROW_POOL + 2, 1);
  _ui->print("          ");
  for (uint8_t i = 8; i < 16; i++) {
    bool active = (_poolLine == /*POOL_LINE_LOOP_SLOTS*/ 7) && (_poolIdx == i);
    if (active) _ui->setInverse(true);
    _ui->printf(" S%02d", i);
    if (active) _ui->setInverse(false);
  }
}
```

**Note** : adapter `POOL_LINE_LOOP_CONTROLS` / `POOL_LINE_LOOP_SLOTS` constants — étendre `poolLineSize` et `poolItemLabel` pour gérer ces nouvelles lignes selon sous-page (`_activeSubPage == SUB_LOOP`).

- [ ] **Step 2: Build**

- [ ] **Step 3: No commit yet**.

---

### Task 20 — Implement `assignLoopRole` + swap-to-pool + hard-constraint

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp`

- [ ] **Step 1: Implement `assignLoopRole(pad, line, index)`**

```cpp
void ToolPadRoles::assignLoopRole(uint8_t pad, uint8_t line, uint8_t index) {
  // line == 6 → controls (index 0=REC, 1=PS, 2=CLR)
  // line == 7 → slots (index 0..15)

  // R1 check : refuser si pad est bank
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (_wkBankPads[i] == pad) {
      _setFlash("Pad is Bank — sacred.");
      return;
    }
  }

  if (line == 6) {  // LOOP control
    // R2 layer musical exclusion + handling carriesConfig (ControlPad)
    int8_t cpIdx = findControlPadEntryIdx(_nvs->getLoadedControlPadStore(), pad);
    if (cpIdx >= 0) {
      _setFlash("Pad is ControlPad — delete entry in Tool 4 first.");
      return;
    }

    // R2 intra-LOOP musical (REC ↔ PS ↔ CLR hard-constraint each other)
    if (isLoopControlPad(_wkLoopPad, pad)) {
      // Same role? no-op
      uint8_t targetPadCur = (index == 0) ? _wkLoopPad.recPad
                            : (index == 1) ? _wkLoopPad.playStopPad
                                           : _wkLoopPad.clearPad;
      if (targetPadCur == pad) return;  // already

      _setFlash("Pad has other LOOP control — move first.");
      return;
    }

    // Move : the role currently has a different pad, free that pad
    uint8_t* targetPad = (index == 0) ? &_wkLoopPad.recPad
                       : (index == 1) ? &_wkLoopPad.playStopPad
                                      : &_wkLoopPad.clearPad;
    // (no swap-to-pool to 0xFF possible for these — they're hard-constraint.
    // The role just changes pad.)
    *targetPad = pad;

  } else if (line == 7) {  // LOOP slot
    // R5 cross-layer with CP OK, R4 inter-ctx with ARPEG OK
    // R3 intra-LOOP hold-left : check if pad already another slot
    int8_t existingSlot = findLoopSlotIdx(_wkLoopPad, pad);
    if (existingSlot >= 0 && existingSlot != index) {
      // Swap-to-pool : free the existing slot
      _wkLoopPad.slotPads[existingSlot] = 0xFF;
      _setFlash("Slot returned to pool.");
    }

    // Move the slot role from its current pad to the new pad
    uint8_t oldPad = _wkLoopPad.slotPads[index];
    _wkLoopPad.slotPads[index] = pad;
    if (oldPad != 0xFF && oldPad != pad) {
      // Old pad freed (implicit — no need to write 0xFF since slotPads[index] now points to new pad)
    }
  }
}
```

- [ ] **Step 2: Implement `clearLoopRole(pad)`**

```cpp
void ToolPadRoles::clearLoopRole(uint8_t pad) {
  // Hard-constraint : REC / PS / CLEAR cannot be cleared
  if (pad == _wkLoopPad.recPad || pad == _wkLoopPad.playStopPad || pad == _wkLoopPad.clearPad) {
    _setFlash("LOOP control cannot be cleared — move it instead.");
    return;
  }
  // Slot OK
  int8_t slotIdx = findLoopSlotIdx(_wkLoopPad, pad);
  if (slotIdx >= 0) {
    _wkLoopPad.slotPads[slotIdx] = 0xFF;
  }
}
```

- [ ] **Step 3: Hard-constraint exit handler**

Dans `run()`, avant l'exit (ESC ou save+exit), ajouter check :

```cpp
// Phase 3 — hard-constraint exit : refuse if LOOP controls unassigned
if (_wkLoopPad.recPad == 0xFF || _wkLoopPad.playStopPad == 0xFF || _wkLoopPad.clearPad == 0xFF) {
  _setFlash("LOOP needs REC/PS/CLR assigned — move from defaults.");
  // Force user to switch to sous-page LOOP if not already
  if (_activeSubPage != SUB_LOOP) {
    _activeSubPage = SUB_LOOP;
    buildRoleMap();
    drawScreen();
  }
  return;  // refuse exit
}
```

- [ ] **Step 4: Build**

- [ ] **Step 5: No commit yet**.

---

### Task 21 — Extend `saveAll` to persist `_wkLoopPad` to NVS

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp` (`saveAll` ~ligne 259)

- [ ] **Step 1: Read `saveAll`**

- [ ] **Step 2: Add LoopPadStore save**

```cpp
bool ToolPadRoles::saveAll() {
  // ... existing saves (bank pads, scale, arp) ...

  // Phase 3 — save LoopPadStore working copy
  if (_nvs) {
    _nvs->setLoadedLoopPad(_wkLoopPad);   // updates cache
    // Persist to NVS (via Preferences or NvsManager API)
    _nvs->saveLoopPad();   // sync write or queue depending on existing pattern
  }

  // ... return success ...
}
```

**Note** : vérifier l'API NvsManager pour saveLoopPad. Si pas existant, l'ajouter (saveBlob pattern). Pattern existing : `NvsManager::saveBank()`, `savePotParams()`, etc.

Si signature manquante :
```cpp
// NvsManager.h
void saveLoopPad();
void setLoadedLoopPad(const LoopPadStore& s) { _loadedLoopPad = s; }

// NvsManager.cpp
void NvsManager::saveLoopPad() {
  Preferences prefs;
  if (prefs.begin(LOOPPAD_NVS_NAMESPACE, false)) {
    prefs.putBytes(LOOPPAD_NVS_KEY, &_loadedLoopPad, sizeof(LoopPadStore));
    prefs.end();
  }
}
```

- [ ] **Step 3: Build**

- [ ] **Step 4: No commit yet**.

---

### Task 22 — HW Gate G4 + commit Phase 3.E

- [ ] **Step 1: HW Gate G4 — Sous-page LOOP + defaults + hard-constraint**

Upload + monitor.

**Pré-requis HW** :
- LoopPadStore actuel (probablement avec dev seed 32/33/34 actif Phase 2).
- Bank LOOP existante (créée via Tool 5) pour test runtime ultérieur.

**Procédure de test** :
1. Boot → setup mode → Tool 3.
2. TAB pour aller en sous-page LOOP.
3. Header highlight sur LOOP.
4. Grid affiche :
   - 3 LOOP controls actifs verts (avec defaults 32/33/34 si dev seed ou 30/31/32 si pas).
   - Bank pads dim red locked.
   - ControlPads dim grey.
   - ARPEG roles dim visibles.
5. Pool affiche 2 sections : Controls (REC/PS/CLR) + Slots (S00..S15).
6. Bouger REC : naviguer cursor sur pad libre, choose pool REC, ENTER → REC se déplace, ancien pad libre.
7. Tenter clear REC (naviguer pool ROLE_NONE) → refus + flash msg "LOOP control cannot be cleared".
8. Assigner Slot 5 sur pad libre → OK.
9. Tenter Slot 5 sur pad déjà Slot 7 → swap-to-pool, flash msg.
10. Tenter REC sur pad ControlPad → refus.
11. Tenter REC sur pad PS → refus.
12. Bouger Slot 5 sur pad qui est Root C ARPEG → coexist OK (R4).
13. Save + reboot → assignements persistent.
14. Test runtime : bank LOOP joue avec les nouveaux REC/PS/CLR pads ?

**Critères G4** :
- ✓ Sous-page LOOP affiche correctement controls actifs + slots.
- ✓ Pool 2 sections (Controls / Slots) visible.
- ✓ Move REC fonctionne.
- ✓ Refus clear LOOP control.
- ✓ Swap-to-pool slot intra-ctx visible.
- ✓ Refus REC sur pad ControlPad / PS.
- ✓ Coexist Slot + ARPEG hold-left.
- ✓ Persistance NVS post-reboot.
- ✓ Bank LOOP runtime fonctionne avec nouveaux pads.

Autoriser l'upload, tester. **Répondre `GO` ou `validé` quand G4 OK.**

- [ ] **Step 2: Commit gate Phase 3.E**

Message HEREDOC :

```
feat(loop-phase-3): Tool 3 sous-page LOOP + defaults validator (Phase 3.E)

3 livrables Phase 3.E :
1. Defaults LOOP 30/31/32 hardcodés dans KeyboardData.h (constexpr) + extension
   validateLoopPadStore : applique defaults si recPad/PS/CLEAR == 0xFF, clamp
   sanity NUM_KEYS.
2. Tool 3 sous-page LOOP :
   - _drawGridLoop : controls actifs verts + slots actifs + bank locked +
     CP/ARPEG dim visibles selon R4/R5.
   - _drawPoolLoop : 2-sections Controls / Slots (16 slots en 2 rows).
   - assignLoopRole : R2 musical exclusion (refus + flash), swap-to-pool slot
     R3 intra-ctx, move-only LOOP controls.
   - clearLoopRole : refus clear LOOP controls (hard-constraint Q14), clear OK
     pour slots.
   - Hard-constraint exit : refuse exit Tool 3 si LOOP controls partiellement
     unassigned.
3. saveAll étendu pour persister _wkLoopPad via NvsManager.saveLoopPad
   (helper ajouté si manquant).

Invariants Phase 3 activés :
- Inv 12 : LoopPadStore.recPad/PS/CLEAR jamais 0xFF en runtime.
- Inv 13 : max 3 rôles coexistent par pad.

HW gate G4 validé : sous-page LOOP fonctionnelle, swap-to-pool slot R3 visible,
move REC/PS/CLR OK, refus clear LOOP control, hard-constraint exit, persistance
NVS, bank LOOP runtime OK avec nouveaux pads.
```

Attendre **« ok commit »** explicite.

---

## Phase 3.F — Validation collision cross-tool (inline checks + grid dim cross-ctx)

### Task 23 — Verify Tool 3 → Tool 4 collision symmetric checks (inline)

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp` (`assignLoopRole` review)

- [ ] **Step 1: Audit `assignLoopRole` cross-tool checks**

Pour chaque cas R2 (LOOP control assignement), vérifier que ControlPad cross-check est présent (`findControlPadEntryIdx`).

Vérifier que :
- LOOP REC sur ControlPad → refus.
- LOOP PS sur ControlPad → refus.
- LOOP CLEAR sur ControlPad → refus.
- LOOP Slot sur ControlPad → OK (R5 coexist).

Si manquant, ajouter checks comme dans Task 20 (déjà inclus en théorie — Task 23 = audit fix si nécessaire).

- [ ] **Step 2: Build + grep audit**

```bash
grep -c "findControlPadEntryIdx" src/setup/ToolPadRoles.cpp
# Expected : >= 2-3 (in assignLoopRole + _drawGridLoop + _drawGridArpeg)
```

- [ ] **Step 3: No commit yet**.

---

### Task 24 — Add visual cross-context dim markers in grid render

**Files** :
- Modify : `src/setup/ToolPadRoles.cpp` (`_drawGridLoop` + `_drawGridArpeg`)

- [ ] **Step 1: Enhance dim markers**

Quand un pad a un rôle cross-context (Slot LOOP visible en sous-page ARPEG, ou Root C ARPEG visible en sous-page LOOP), afficher un marker dim distinctif.

Pattern : préfixer le label avec `·` (middle dot UTF-8 ou `.` ASCII) ou utiliser une couleur dim spécifique.

```cpp
// In _drawGridLoop, when rendering ARPEG role visible cross-ctx :
if (sr.kind == ScaleRoleKind::ROOT) {
  char label[8]; snprintf(label, sizeof(label), "·R%d", sr.idx);
  _renderCell(pad, label, COLOR_CROSS_CONTEXT_DIM, /*editable=*/true);  // can add Slot here (R4)
  continue;
}
```

INFO panel hint quand cursor sur ce pad :
```
"Pad N has Root C (ARPEG, cross-context). Slot 5 (LOOP) can be added here."
```

- [ ] **Step 2: Build**

- [ ] **Step 3: No commit yet**.

---

### Task 25 — HW Gate G5 (collision cross-tool) + commit Phase 3.F

- [ ] **Step 1: HW Gate G5 — Collisions cross-tool (EC1-EC9 spec)**

Upload + monitor.

**Procédure de test EC1-EC9** :

**EC1** — Tool 4 : tente ControlPad sur pad LOOP REC → refus + flash.
**EC2** — Tool 3 LOOP : tente REC sur pad ControlPad → refus + flash.
**EC3** — Tool 3 LOOP : Slot 5 sur pad Root C → coexist OK, INFO panel hint.
**EC4** — Tool 3 LOOP : Slot 5 sur pad Slot 7 → swap-to-pool, flash msg.
**EC5** — Tool 3 LOOP : Slot 5 sur pad ControlPad → coexist OK.
**EC6** — Tool 3 ARPEG : Root D sur pad Root C → swap-to-pool R3.
**EC7** — Tool 3 LOOP : REC sur pad PS → refus + flash.
**EC8** — Tool 3 NORM : bouger Bank 3 vers pad Root C → refus + flash.

**Critères G5** :
- ✓ 8 scénarios EC1-EC8 produisent le comportement attendu.
- ✓ Flash messages clairs et explicatifs.
- ✓ Grid render reflète l'état post-action.
- ✓ Persistance NVS post-reboot pour chaque action OK.

Autoriser l'upload, tester. **Répondre `GO` ou `validé` quand G5 OK.**

- [ ] **Step 2: Commit gate Phase 3.F**

Message HEREDOC :

```
feat(loop-phase-3): validation collision cross-tool (Phase 3.F)

Audit inline checks + visual cross-context dim markers :
- Tool 3 assignLoopRole : check exhaustif R2 (ControlPad), R3 (intra-LOOP),
  R1 (Bank). Refus + flash msg explicite.
- _drawGridLoop / _drawGridArpeg : marker dim cross-context (·R0, ·M3, etc.)
  + INFO panel hint contextuel.
- Tool 4 _addSlot déjà couvert Phase 3.B.

HW gate G5 validé : 8 scénarios EC1-EC8 testés, comportement conforme spec §10
matrice compatibilité.
```

Attendre **« ok commit »** explicite.

---

## Phase 3.G — Retrait dev seed M7 + collision check secondaire post-loadAll

### Task 26 — Remove `applyDevSeedLoopPadsIfSafe` + call site + add collision check post-loadAll

**Files** :
- Modify : `src/managers/NvsManager.h` (suppression déclaration)
- Modify : `src/managers/NvsManager.cpp` (suppression body + commentaire ligne 162 + ajout collision check secondaire)
- Modify : `src/main.cpp` (suppression call ligne 538)

- [ ] **Step 1: Audit usages dev seed**

```bash
grep -rn "applyDevSeedLoopPadsIfSafe\|DevSeedLoopPads" src/
```

Expected sites :
- `src/main.cpp:538` (call)
- `src/managers/NvsManager.cpp:162` (commentaire)
- `src/managers/NvsManager.cpp:1197-1230` (impl)
- `src/managers/NvsManager.h:96` (déclaration)

- [ ] **Step 2: Delete declaration**

`NvsManager.h` ligne 96 :
```cpp
// REMOVE :
void applyDevSeedLoopPadsIfSafe();
```

- [ ] **Step 3: Delete implementation**

`NvsManager.cpp` lignes 1196-1230 — supprimer toute la fonction.

- [ ] **Step 4: Delete commentaire `loadAll`**

`NvsManager.cpp` ligne 162 — supprimer ou réécrire la ligne référençant le dev seed.

- [ ] **Step 5: Delete call site**

`main.cpp:538` — supprimer la ligne `s_nvsManager.applyDevSeedLoopPadsIfSafe();`.

- [ ] **Step 6: Add collision check secondaire post-loadAll**

Dans `NvsManager::loadAll()` (à la fin, post-validators) ou dans `main.cpp` post-boot, ajouter :

```cpp
// Phase 3 — collision check secondaire post-loadAll (spec EC9)
// Cas pathologique : ControlPad existant sur pad qui matche LOOP defaults
// (30/31/32 ou autre assignement existing). Validator LoopPadStore est
// unilatéral, donc collision n'est pas auto-résolue. Warning Serial.
const LoopPadStore& lp = _loadedLoopPad;
const ControlPadStore& cp = _loadedCtrlStore;
uint8_t controlPads[3] = { lp.recPad, lp.playStopPad, lp.clearPad };
const char* controlNames[3] = { "REC", "PS", "CLEAR" };
for (uint8_t i = 0; i < 3; i++) {
  for (uint8_t j = 0; j < cp.count; j++) {
    if (cp.entries[j].padIndex == controlPads[i]) {
      #if DEBUG_SERIAL
      Serial.printf("[BOOT] WARN: LOOP %s pad %u collides with ControlPad entry %u (CC=%u).\n",
                    controlNames[i], controlPads[i], j, cp.entries[j].ccNumber);
      Serial.println("[BOOT]   → User must resolve in Tool 3 sub-page LOOP or Tool 4 (setup mode).");
      #endif
    }
  }
}
```

- [ ] **Step 7: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected : compile OK. Vérifier que la suppression du dev seed ne casse pas le boot (le validator LoopPadStore avec defaults remplace fonctionnellement le dev seed).

- [ ] **Step 8: Auto-review grep — vérifier suppression complète**

```bash
grep -rn "applyDevSeedLoopPadsIfSafe\|DevSeedLoopPads" src/
# Expected : 0 résultats — tout supprimé
```

**Hard-assert** : si grep retourne >0 lignes → STOP, signaler résidu.

```bash
LEFT=$(grep -rn "applyDevSeedLoopPadsIfSafe\|DevSeedLoopPads" src/ | wc -l)
if [ "$LEFT" -ne 0 ]; then
  echo "FAIL: $LEFT references to dev seed M7 left in src/"
  exit 1
fi
echo "PASS: dev seed M7 fully removed."
```

- [ ] **Step 9: HW Gate G6 — Full workflow boot factory NVS vide → Tool 3 assigne → LOOP joue**

Procédure de test (le plus important HW gate Phase 3) :

**Pré-requis HW** : effacer NVS LoopPadStore (Tool 6 reset ou flash erase complet).

1. Erase NVS (commande ou méthode existing).
2. Boot → trace serial montre defaults appliqués par validator (30/31/32).
3. Setup mode → Tool 5 : créer bank LOOP (bank slot 3 en LOOP par exemple).
4. Tool 3 → sous-page LOOP : voir REC=30, PS=31, CLR=32 actifs.
5. Bouger REC vers pad 35 (move).
6. Exit setup → reboot.
7. Mode jeu, bank LOOP active : taper pad 35 (nouveau REC) → enregistrement démarre.
8. Pad PS=31 → play.
9. Pad CLR=32 → clear (long-press).
10. Vérifier full workflow LOOP OK avec nouveaux pads.

**Critères G6** :
- ✓ Boot avec NVS vide : defaults 30/31/32 appliqués, trace serial visible.
- ✓ Setup → Tool 3 LOOP : controls actifs visibles.
- ✓ Move REC vers autre pad fonctionne.
- ✓ Save + reboot persiste les nouveaux pads.
- ✓ Bank LOOP runtime fonctionne avec nouveaux pads.
- ✓ Aucune référence dev seed M7 dans le code (hard-assert §8).

Autoriser l'upload, tester. **Répondre `GO` ou `validé` quand G6 OK.**

- [ ] **Step 10: Commit gate Phase 3.G**

Message HEREDOC :

```
feat(loop-phase-3): retrait dev seed M7 + collision check post-loadAll (Phase 3.G)

Retrait complet du dev seed Phase 2 transitoire :
- NvsManager::applyDevSeedLoopPadsIfSafe : suppression impl + déclaration .h.
- main.cpp:538 : suppression call site.
- NvsManager.cpp:162 : suppression commentaire dev seed.

Remplacement fonctionnel par validator étendu (déjà livré Phase 3.E) :
- validateLoopPadStore applique defaults 30/31/32 si recPad/PS/CLEAR == 0xFF.
- Pas de conditionnalité collision Tool 4 (validator unilatéral).

Collision check secondaire ajouté post-loadAll : warning Serial si
LOOP defaults entrent en conflit avec ControlPadStore existing (cas
pathologique edge). User résout via Tool 3 / Tool 4 setup mode.

HW gate G6 validé : boot factory NVS vide → defaults appliqués → setup Tool 3
LOOP → assignement custom → reboot → bank LOOP runtime OK.

Hard-assert vert : 0 référence applyDevSeedLoopPadsIfSafe / DevSeedLoopPads
dans src/.
```

Attendre **« ok commit »** explicite.

---

## Phase 3.H — Doc-sync

### Task 27 — Update all reference docs + LOOP spec corrections

**Files** :
- Modify : `docs/reference/setup-tools-conventions.md`
- Modify : `docs/reference/nvs-reference.md`
- Modify : `docs/reference/architecture-briefing.md`
- Modify : `docs/reference/patterns-catalog.md`
- Modify : `STATUS.md`
- Modify : `docs/superpowers/LOOP_PROGRESS.md`
- Modify : `docs/superpowers/specs/2026-04-19-loop-mode-design.md`

- [ ] **Step 1: Update `setup-tools-conventions.md`**

Ajouter une nouvelle section "Pad role collisions" comprenant :
- Les 5 règles spec §5 LOOP design verbatim.
- Table des 11 rôles avec attrs (layer, context, toleratesUnassigned, carriesConfig) verbatim de §11 spec Phase 3 design.
- Matrice compatibilité rôle × rôle verbatim de §12.
- Les 5 helpers cross-store + signatures (post refondation (c) — arrays par
  référence pour Scale/Arp/Bank).
- Pattern TAB navigation sous-pages (`SubPage` enum, cycle, arrows intra-page only).

**Note (b) intégrée — 2 gestes runtime sur pad CLEAR** : ajouter une sous-section
"Pad CLEAR — dual-gesture runtime (OD-Sync 2026-05-19)" mentionnant :
- **Long-press** → `LoopEngine::longPressClear` (clear destructive, comportement
  Phase 2 préservé).
- **Tap court en PLAYING / STOPPED** → Undo/Redo 1-level toggle sur la dernière
  couche OD (OD-Sync nouveau, cf [`Illpad_OD_Sync.md`](../superpowers/specs/Illpad_OD_Sync.md) §4).
- **Conséquence Tool 3** : un seul `clearPad` field dans `LoopPadStore` ; le
  runtime LoopEngine dispatch selon la durée du press. Tool 3 sous-page LOOP
  montre un seul pad CLEAR. INFO panel peut mentionner les 2 gestes pour
  exhaustivité (optionnel, hors-scope strict Phase 3).

- [ ] **Step 2: Update `nvs-reference.md`**

LoopPadStore : statut "DECLARED Phase 1 (commit `1b0ac8c`) — WIRED Phase 3 (Tool 3 b1 sous-page LOOP, commits Phase 3.E + 3.G)". Defaults documentés (30/31/32 via constexpr KeyboardData.h).

- [ ] **Step 3: Update `architecture-briefing.md`**

Section setup-mode :
- Tool 3 b1 multi-sous-pages : Banks (8 bank pads), ARPEG (20 roles), LOOP (3 controls + 16 slots).
- Pattern TAB navigation à normaliser.
- Helpers cross-store comme source unique de vérité (invariant 14).

- [ ] **Step 4: Update `patterns-catalog.md`**

Évaluer si "Multi sub-page tool with TAB navigation" mérite un pattern P15. Si oui, écrire :
- Quand utiliser : tool avec 3+ contextes orthogonaux, nav TAB normalisée.
- Comment : enum SubPage, état `_activeSubPage`, dispatch dans `drawGrid`/`drawPool`/handlers.
- Exemples : Tool 3 b1 (Phase 3), Tool 7 (Phase 4 prévu).
- Anti-pattern : arrows pour switch sous-page (collision avec nav intra-page).

- [ ] **Step 5: Update `STATUS.md`**

Focus courant : Phase 3 LOOP CLOSE. Table commits Phase 3.A → 3.H. HW gates G1-G6 validés.

- [ ] **Step 6: Update `LOOP_PROGRESS.md`**

Phase 3 row : statut ✅ CLOSE + range commits.

- [ ] **Step 7: Update `2026-04-19-loop-mode-design.md`**

3 corrections :
- §27 P3 ligne 621 : supprimer "Refactor Tool 7 en 3 pages" (déplacé Phase 4, déjà mentionné §27 P4).
- §27 P2 ligne 611 : retirer mention `applyDevSeedLoopPadsIfSafe` (fonction supprimée Phase 3.G).
- §28 Q7 : marquer "IMPLEMENTED Phase 3".

- [ ] **Step 8: Hard-assert greps doc-sync**

```bash
# Vérifier que setup-tools-conventions a une section "Pad role collisions"
grep -c "Pad role collisions\|Pad Role Collisions" docs/reference/setup-tools-conventions.md
# Expected : >= 1

# Vérifier que nvs-reference a la mention "WIRED Phase 3"
grep -c "WIRED Phase 3" docs/reference/nvs-reference.md
# Expected : >= 1

# Vérifier que LOOP_PROGRESS Phase 3 = CLOSE
grep -c "Phase 3 LOOP.*CLOSE" docs/superpowers/LOOP_PROGRESS.md
# Expected : >= 1

# Vérifier que applyDevSeedLoopPadsIfSafe n'est plus dans la spec
grep -c "applyDevSeedLoopPadsIfSafe" docs/superpowers/specs/2026-04-19-loop-mode-design.md
# Expected : 0
```

**Hard-assert** : si un grep échoue → STOP, signaler doc-sync incomplet.

- [ ] **Step 9: Commit gate Phase 3.H**

Présenter à Loïc :

```bash
git status --porcelain
```

Fichiers à add :
- `docs/reference/setup-tools-conventions.md`
- `docs/reference/nvs-reference.md`
- `docs/reference/architecture-briefing.md`
- `docs/reference/patterns-catalog.md` (si pattern P15 ajouté)
- `STATUS.md`
- `docs/superpowers/LOOP_PROGRESS.md`
- `docs/superpowers/specs/2026-04-19-loop-mode-design.md`

Message HEREDOC :

```
docs(loop-phase-3): sync 7 references post Phase 3 close (Phase 3.H)

Doc-sync exhaustive Phase 3 :
- setup-tools-conventions : nouvelle section "Pad role collisions" (5 règles,
  table 11 rôles avec attrs, matrice compatibilité, 5 helpers, pattern TAB nav).
- nvs-reference : LoopPadStore DECLARED → WIRED Phase 3, defaults 30/31/32 documentés.
- architecture-briefing : section setup-mode Tool 3 b1 multi-sous-pages.
- patterns-catalog : pattern P15 (ou suivant) "Multi sub-page tool with TAB
  navigation" si jugé réutilisable.
- STATUS.md : focus courant Phase 3 CLOSE + table commits 3.A → 3.H.
- LOOP_PROGRESS : Phase 3 → CLOSE.
- Spec LOOP : §27 P3 supp Tool 7 (drift D7), §27 P2 supp dev seed M7, §28 Q7
  marqué IMPLEMENTED.

Hard-asserts greps vert. 0 référence dev seed M7 dans src/ ni docs.

Phase 3 LOOP CLOSE.
```

Attendre **« ok commit »** explicite.

---

## Phase 3 — fin

À la complétion de Task 27 (commit Phase 3.H), Phase 3 LOOP est **CLOSE**.

**État final attendu** :
- 8 commits sur main (Phase 3.A → 3.H).
- 6 HW gates G1-G6 validés HW Loïc.
- Tool 3 b1 livré (3 sous-pages NORM/ARPEG/LOOP nav TAB).
- Tool 4 ext livré (refus ControlPad sur LOOP control).
- Dev seed M7 retiré complètement.
- LoopPadStore defaults hardcodés (30/31/32) via validator.
- 5 helpers cross-store comme source unique de vérité.
- 7 docs reference + spec syncés.

**Phase suivante** : Phase 4 LOOP (PotRouter 3 contexts + Tool 7 refacto + LED wiring complet + EVT_LOOP_* patterns).

---

## Addendum — Audit-fix integration (2026-05-19)

Issu de l'[audit adversarial](2026-05-19-loop-phase-3-plan_AUDIT.md). 6 fix critiques intégrés comme **overrides** des tasks originales.

### B-N1 override — Task 8 Step 2 : précisions call site `begin()`

Le call site **unique** est `src/setup/SetupManager.cpp:30` :
```cpp
_toolRoles.begin(keyboard, leds, &_ui, ...);
```

Variable **nommée `_toolRoles`** (pas `_padRolesTool` comme le plan le suppose).

Action Task 8 Step 2 :
1. Read `src/setup/SetupManager.cpp` lignes 1-50 + ligne 30.
2. Étendre signature `begin()` : ajouter `NvsManager* nvs` en 4ème position (après `&_ui`).
3. Modifier le call site SetupManager.cpp:30 : ajouter `&_nvsManager` (ou pointer équivalent à l'instance NvsManager visible dans SetupManager — vérifier le nom dans `SetupManager.h:9-50`).

Pas de scan multi-fichier nécessaire — 1 seul call site confirmé.

### M1 override — Task 15 reformulation : flash sur steal existing

Le code Tool 3 actuel **fait déjà le swap-to-pool** silencieusement (`ToolPadRoles.cpp:759-766`, commentaire « Steal silencieux »). Task 15 n'introduit pas le mécanisme — elle ajoute juste un **flash msg cosmétique**.

Action Task 15 reformulée :

- [ ] **Step 1: Lire `ToolPadRoles.cpp` lignes 748-775**

Identifier le bloc `NAV_ENTER` qui fait le steal silencieux.

- [ ] **Step 2: Ajouter flash sur le steal pour ARPEG roles**

Modifier le bloc lignes 759-766 :

```cpp
} else {
  // Phase 3 : flash on steal (was: silent steal).
  uint8_t owner = findPadWithRole(_poolLine, _poolIdx);
  if (owner < NUM_KEYS && owner != (uint8_t)pad) {
    clearRole(owner);
    _setFlash("Previous role returned to pool.");   // <-- NEW Phase 3
  }
  clearRole((uint8_t)pad);
  assignRole((uint8_t)pad, _poolLine, _poolIdx);
  if (saveAll()) {
    _ui->flashSaved();
    _editing = false;
  }
  screenDirty = true;
}
```

Pas d'autre changement métier. Le pattern de swap reste identique, juste flash visible.

**Note** : ce changement « silent → flash » s'applique à **tous** les rôles via ce dispatcher (bank, root, mode, octave, hold, et plus tard LOOP via M4). Affecte aussi NORM bank moves (Task 12) — cohérent avec D5 spec.

### M2 override — Add Task 11.5 : `_setFlash` infrastructure Tool 3

**Nouvelle task à insérer entre Task 11 et Task 12** :

#### Task 11.5 — Add `_setFlash` infrastructure to ToolPadRoles

**Files** : `src/setup/ToolPadRoles.h`, `src/setup/ToolPadRoles.cpp`

- [ ] **Step 1: Add members + helper declarations to .h**

Dans `ToolPadRoles.h`, section private :
```cpp
// Phase 3 — flash msg infrastructure (pattern aligned with ToolControlPads.h:58-59)
char     _flashMsg[80];
uint32_t _flashExpireMs;

void _setFlash(const char* msg);
bool _flashActive() const;
```

- [ ] **Step 2: Init in constructor**

Dans `ToolPadRoles::ToolPadRoles()` body :
```cpp
_flashMsg[0] = '\0';
_flashExpireMs = 0;
```

- [ ] **Step 3: Implement helpers in .cpp**

```cpp
void ToolPadRoles::_setFlash(const char* msg) {
  strncpy(_flashMsg, msg, sizeof(_flashMsg) - 1);
  _flashMsg[sizeof(_flashMsg) - 1] = '\0';
  _flashExpireMs = millis() + 2500;  // 2.5s timeout, aligned with Tool 4
}

bool ToolPadRoles::_flashActive() const {
  return _flashExpireMs > millis() && _flashMsg[0] != '\0';
}
```

- [ ] **Step 4: Render flash in drawScreen or drawControlBar**

Pattern à choisir selon layout disponible. Reco : intégrer dans `drawControlBar` :
```cpp
void ToolPadRoles::drawControlBar() {
  if (_flashActive()) {
    _ui->moveCursor(ROW_FLASH, 1);
    _ui->setColor(COLOR_FLASH);
    _ui->print(_flashMsg);
    _ui->resetColor();
  } else {
    // existing control bar render
    _ui->print("TAB=sub-page  ARROWS=nav  ENTER=assign  CLEAR=reset  ESC=exit");
  }
}
```

- [ ] **Step 5: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 6: No commit yet** — wait for Phase 3.C bundle.

### M3 override — Task 11 Step 2 : Option A `_drawGridLegacy()` extracté

Task 11 Step 2 doit explicitement choisir **Option A** (stub non-destructive).

Action :

1. Avant de réécrire `drawGrid`, **extraire le body monolithique existant** dans une nouvelle méthode privée `_drawGridLegacy()` :

```cpp
// In ToolPadRoles.cpp
void ToolPadRoles::_drawGridLegacy() {
  // Body original du drawGrid (lignes 314-326 actuelles, ou plus selon l'extraction).
  // Inchangé fonctionnellement.
  // À utiliser comme fallback pendant Phase 3.C tant que _drawGridArpeg/_drawGridLoop
  // ne sont pas encore implémentés (Tasks 13 et 18).
  ...
}
```

2. Réécrire `drawGrid()` en dispatcher :

```cpp
void ToolPadRoles::drawGrid() {
  switch (_activeSubPage) {
    case SUB_NORM:  _drawGridNorm(); break;
    case SUB_ARPEG: _drawGridArpeg(); break;
    case SUB_LOOP:  _drawGridLoop(); break;
  }
}
```

3. Stubs initiaux :

```cpp
void ToolPadRoles::_drawGridArpeg() {
  _drawGridLegacy();   // stub Phase 3.C ; Task 13 remplacera par vrai impl ARPEG
}

void ToolPadRoles::_drawGridLoop() {
  _drawGridLegacy();   // stub Phase 3.C ; Task 18 remplacera par vrai impl LOOP
}
```

Idem pour `drawPool` → `_drawPoolLegacy()` extracté + stubs `_drawPoolArpeg/_drawPoolLoop` → `_drawPoolLegacy()`.

**Conséquence** : pendant Phase 3.C HW gate G2, sous-page NORM affiche le nouveau rendu, sous-pages ARPEG et LOOP affichent l'ancien rendu monolithique (qui montre tous les rôles tous-en-un comme aujourd'hui). Pas de regression visible. Tasks 13 et 18 remplaceront proprement.

### M4 override — Add Task 17.5 : Extend pool dispatcher for LOOP lines

**Nouvelle task à insérer entre Task 17 et Task 18** :

#### Task 17.5 — Extend pool dispatcher (POOL_LINE_COUNT, poolLineSize, poolItemLabel, findPadWithRole, assignRole, clearRole) for LOOP lines 6 (controls) + 7 (slots)

**Files** : `src/setup/ToolPadRoles.h`, `src/setup/ToolPadRoles.cpp`

- [ ] **Step 1: Extend constants in ToolPadRoles.h**

```cpp
// ToolPadRoles.h — bump POOL_LINE_COUNT + add new sizes
static const uint8_t POOL_LOOP_CTRL_COUNT  = 3;    // REC, PS, CLR
static const uint8_t POOL_LOOP_SLOT_COUNT  = 16;   // slots 0..15
static const uint8_t POOL_LINE_COUNT       = 8;    // 0=clear, 1=bank, 2=root, 3=mode, 4=octave, 5=hold, 6=loop-ctrl, 7=loop-slot

// Pool labels LOOP
static const char* POOL_LOOP_CTRL_LABELS[3];
static const char* POOL_LOOP_SLOT_LABELS[16];
```

- [ ] **Step 2: Define labels in .cpp**

```cpp
// ToolPadRoles.cpp
const char* ToolPadRoles::POOL_LOOP_CTRL_LABELS[3] = { "REC", "PS_", "CLR" };
const char* ToolPadRoles::POOL_LOOP_SLOT_LABELS[16] = {
  "S00","S01","S02","S03","S04","S05","S06","S07",
  "S08","S09","S10","S11","S12","S13","S14","S15"
};
```

- [ ] **Step 3: Extend poolLineSize switch**

```cpp
uint8_t ToolPadRoles::poolLineSize(uint8_t line) const {
  switch (line) {
    case 1: return POOL_BANK_COUNT;
    case 2: return POOL_ROOT_COUNT;
    case 3: return POOL_MODE_COUNT;
    case 4: return POOL_OCTAVE_COUNT;
    case 5: return POOL_HOLD_COUNT;
    case 6: return POOL_LOOP_CTRL_COUNT;    // NEW Phase 3
    case 7: return POOL_LOOP_SLOT_COUNT;    // NEW Phase 3
    default: return 0;
  }
}
```

- [ ] **Step 4: Extend poolItemLabel switch**

```cpp
const char* ToolPadRoles::poolItemLabel(uint8_t line, uint8_t index) const {
  switch (line) {
    case 1: return (index < POOL_BANK_COUNT)   ? POOL_BANK_LABELS[index]   : "";
    case 2: return (index < POOL_ROOT_COUNT)   ? POOL_ROOT_LABELS[index]   : "";
    case 3: return (index < POOL_MODE_COUNT)   ? POOL_MODE_LABELS[index]   : "";
    case 4: return (index < POOL_OCTAVE_COUNT) ? POOL_OCTAVE_LABELS[index] : "";
    case 5: return (index < POOL_HOLD_COUNT)   ? POOL_HOLD_LABELS[index]   : "";
    case 6: return (index < POOL_LOOP_CTRL_COUNT) ? POOL_LOOP_CTRL_LABELS[index] : "";  // NEW
    case 7: return (index < POOL_LOOP_SLOT_COUNT) ? POOL_LOOP_SLOT_LABELS[index] : "";  // NEW
    default: return "";
  }
}
```

- [ ] **Step 5: Extend findPadWithRole for LOOP lines**

```cpp
uint8_t ToolPadRoles::findPadWithRole(uint8_t line, uint8_t index) const {
  switch (line) {
    case 1: return (index < NUM_BANKS) ? _wkBankPads[index] : 0xFF;
    case 2: return (index < 7) ? _wkRootPads[index] : 0xFF;
    case 3:
      if (index < 7) return _wkModePads[index];
      if (index == 7) return _wkChromPad;
      return 0xFF;
    case 4: return (index < 4) ? _wkOctavePads[index] : 0xFF;
    case 5: return (index == 0) ? _wkHoldPad : 0xFF;
    // NEW Phase 3 : LOOP controls
    case 6:
      if (index == 0) return _wkLoopPad.recPad;
      if (index == 1) return _wkLoopPad.playStopPad;
      if (index == 2) return _wkLoopPad.clearPad;
      return 0xFF;
    // NEW Phase 3 : LOOP slots
    case 7: return (index < 16) ? _wkLoopPad.slotPads[index] : 0xFF;
    default: return 0xFF;
  }
}
```

- [ ] **Step 6: Extend assignRole for LOOP lines**

```cpp
void ToolPadRoles::assignRole(uint8_t pad, uint8_t line, uint8_t index) {
  if (line == 1) {
    if (index < NUM_BANKS) _wkBankPads[index] = pad;
  } else if (line == 2) {
    if (index < 7) _wkRootPads[index] = pad;
  } else if (line == 3) {
    if (index < 7) _wkModePads[index] = pad;
    else if (index == 7) _wkChromPad = pad;
  } else if (line == 4) {
    if (index < 4) _wkOctavePads[index] = pad;
  } else if (line == 5) {
    if (index == 0) _wkHoldPad = pad;
  // NEW Phase 3
  } else if (line == 6) {
    // LOOP control : route to assignLoopRole (handles R2 + carriesConfig refus + hard-constraint)
    assignLoopRole(pad, line, index);
  } else if (line == 7) {
    // LOOP slot : route to assignLoopRole (handles R3 swap-to-pool)
    assignLoopRole(pad, line, index);
  }
}
```

- [ ] **Step 7: Extend clearRole for LOOP lines**

```cpp
void ToolPadRoles::clearRole(uint8_t pad) {
  // ... existing clears for bank/root/mode/chrom/hold/octave ...

  // NEW Phase 3 : LOOP slots (slot pads tolerate 0xFF)
  for (uint8_t i = 0; i < 16; i++) {
    if (_wkLoopPad.slotPads[i] == pad) _wkLoopPad.slotPads[i] = 0xFF;
  }

  // NEW Phase 3 : LOOP controls — hard-constraint, do not clear
  // (REC/PS/CLR cannot become 0xFF — invariant 12 spec design Phase 3)
  // clearLoopRole flash msg is handled there
  if (pad == _wkLoopPad.recPad || pad == _wkLoopPad.playStopPad || pad == _wkLoopPad.clearPad) {
    clearLoopRole(pad);  // delegates to LOOP-specific handler with flash
  }
}
```

- [ ] **Step 8: Build**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

- [ ] **Step 9: Auto-review grep**

```bash
grep -c "case 6:\|case 7:" src/setup/ToolPadRoles.cpp
# Expected : >= 4 (poolLineSize + poolItemLabel + findPadWithRole + assignRole)
```

- [ ] **Step 10: No commit yet** — Phase 3.E bundle.

### M5 override — Task 17 Step 3 : emplacement validator wire

Spécifier exactement où ajouter `validateLoopPadStore()` dans `NvsManager.cpp::loadAll()` :

Action Task 17 Step 3 reformulée :

1. Lire `NvsManager.cpp::loadAll()` zone autour du load LoopPadStore. Identifier le pattern existing :
```cpp
// Probable pattern (à confirmer en lecture) :
if (prefs.begin(LOOPPAD_NVS_NAMESPACE, true)) {
  size_t sz = prefs.getBytes(LOOPPAD_NVS_KEY, &_loadedLoopPad, sizeof(LoopPadStore));
  if (sz != sizeof(LoopPadStore) || _loadedLoopPad.magic != EEPROM_MAGIC ||
      _loadedLoopPad.version != LOOPPAD_VERSION) {
    memset(&_loadedLoopPad, 0xFF, sizeof(LoopPadStore));
    _loadedLoopPad.magic = EEPROM_MAGIC;
    _loadedLoopPad.version = LOOPPAD_VERSION;
  }
  prefs.end();
}
```

2. Ajouter **immédiatement après** ce bloc :
```cpp
validateLoopPadStore(_loadedLoopPad);   // Phase 3 : applies defaults 30/31/32 if controls == 0xFF
```

3. Auto-review grep :
```bash
grep -c "validateLoopPadStore" src/managers/NvsManager.cpp
# Expected : 1 (call site dans loadAll)
grep -c "validateLoopPadStore" src/core/KeyboardData.h
# Expected : 1 (définition)
```

### M6 — Note : Task 26 collision check emplacement

Décision tranchée par audit : **dans `NvsManager::loadAll()` à la fin**, juste avant la fin de la méthode. Cohérent avec les autres validates + warning Serial format `[BOOT]`.

Task 26 Step 6 mise à jour avec cette précision.

### M7 — Note : changement « silent steal → flash steal »

Documenter dans spec design Phase 3 (§14 lifecycle) + commit messages Phase 3.D et 3.E :
```
NOTE comportement : Phase 3 introduit un flash msg sur swap-to-pool. Le pattern
silencieux d'origine (commentaire "Steal silencieux" ToolPadRoles.cpp:759) est
remplacé pour améliorer la lisibilité UX. Affecte bank, scale, arp, et LOOP roles.
```

### Mineurs m1-m11

Non-bloquants. Liste actions doc-sync Phase 3.H :
- m1-m3 : précisions Tasks 1-3 (cf audit doc).
- m4 : Task 5 marquée explicitement no-op après audit (laisser le commentaire defensive).
- m5-m7 : précisions Tasks 6, 9, 10 (lire le code existant + adapter).
- m8-m9 : ASCII fallback `.` (decision : utiliser ASCII pour cohérence terminal Python).
- m10 : dispatch `assignLoopRole` documenté dans M4 override (Task 17.5 Step 6).
- m11 : Pattern P15 décision pré-exécution = **OUI ajouter** (Tool 7 Phase 4 utilisera le même pattern TAB).

---

## Recap revised — Phase 3 tasks count

Après intégration audit-fix :

| Sous-phase | Tasks | Δ vs initial |
|---|---|---|
| 3.A | 1, 2, 3 | inchangé |
| 3.B | 4, 5, 6 | inchangé |
| 3.C | 7, 8, 9, 10, 11, **11.5**, 12 | +1 (Task 11.5 = `_setFlash` infrastructure) |
| 3.D | 13, 14, 15, 16 | inchangé (Task 15 reformulée) |
| 3.E | 17, **17.5**, 18, 19, 20, 21, 22 | +1 (Task 17.5 = pool dispatcher extension) |
| 3.F | 23, 24, 25 | inchangé |
| 3.G | 26 | inchangé |
| 3.H | 27 | inchangé |

**Total** : 29 tasks (vs 27 initial). 6 HW gates G1-G6, 8 commits, unchanged.

---

## Self-review final (pré-exécution)

### Spec coverage

Vérifier chaque section/exigence du spec Phase 3 design est couverte :

| Spec § | Couvert par Task |
|---|---|
| §1 objectifs | Tasks 1-27 (3 livrables) |
| §2 hors scope | Documenté Scope table |
| §3 convention nommage code vs UI | D1 + Task 7/10 |
| §4 nav TAB | Task 9 |
| §5 sous-page NORM | Tasks 11-12 |
| §6 sous-page ARPEG | Tasks 13-16 |
| §7 sous-page LOOP | Tasks 18-22 |
| §8 layout VT100 | Task 10 + Tasks 11-19 |
| §9 hiérarchie 3 cases | Couvert via assignement logic Tasks 12/15/20 |
| §10 5 règles formalisées | Tasks 4-6 (Tool 4), 12 (NORM), 15 (ARPEG), 20 (LOOP) |
| §11 table rôles attributs | D5 + Task 20 (used implicitly) |
| §12 matrice compatibilité | Tasks 4-6, 12, 15, 20, 25 |
| §13 5 helpers cross-store | Tasks 1-3 |
| §14 swap-to-pool semantics | Tasks 15 (ARPEG), 20 (LOOP) |
| §15 hard constraints | Task 20 + Task 12 (bank) |
| §16 defaults 30/31/32 | Task 17 |
| §17 retrait dev seed M7 | Task 26 |
| §18 Tool 4 ext | Tasks 4-6 |
| §19 EC1-EC10 | Task 25 (HW gate G5) |
| §20 doc-sync | Task 27 |
| §21 drift Tool 7 spec | Task 27 step 7 |
| §22-§24 invariants budget non-goals | Task 27 doc-sync |

✓ Toutes les sections spec sont couvertes.

### Placeholder scan

Aucun "TBD", "TODO", "implement later" dans le plan. Verbatim code fourni pour helpers, validator, assignLoopRole/clearLoopRole, collision check post-loadAll. Sections refacto Tool 3 (Tasks 11-22) fournissent verbatim code partiel + indication zones d'extension — l'exécutant lira le fichier existant et étendra selon pattern décrit. Acceptable vu la taille du refacto (788 lignes Tool 3).

### Type consistency

- `LoopPadStore` : utilisé cohérenment dans helpers + Tool 3 + validator + saveLoopPad.
- `SubPage` enum : init `SUB_NORM = 0`, `SUB_ARPEG = 1`, `SUB_LOOP = 2`, `SUB_COUNT = 3` — utilisé cohérenment dans dispatch.
- `ScaleRoleKind` / `ArpRoleKind` : enum class avec NONE / ROOT / MODE / CHROM (scale) ou NONE / HOLD / OCTAVE (arp) — utilisé cohérenment.
- Signatures `isLoopControlPad(const LoopPadStore&, uint8_t)` cohérente entre déclaration et usages.
- `findLoopSlotIdx` returns `int8_t` (-1 si absent) — cohérent.

### Risk profile Phase 3

- **Refacto Tool 3** (Tasks 11-22) : zone de survol principale. La fonction `drawGrid` actuelle est monolithique — découper proprement sans casser le rendu existing. Hard-assert sur build success + HW gate G2/G3 non-régression critique.
- **Save path LoopPadStore** (Task 21) : si l'API NvsManager n'a pas `saveLoopPad`, l'ajouter proprement (pattern Preferences existing).
- **Dev seed retrait** (Task 26) : hard-assert grep verifies 0 résidu — bloque commit si fail.
- **Collision check post-loadAll** (Task 26) : edge case rare, mais EC9 spec mentionne explicitement. Trace serial visible si applicable.

---

**Plan complet — prêt pour audit adversarial.**

---

## Addendum v2 — Audit indépendant fix (2026-05-19, refondation (c))

Suite à l'[audit indépendant](2026-05-19-loop-phase-3-plan_AUDIT_independent.md) (sub-agent contradictoire, 17 findings additionnels), **3 bloquants** confirmés empiriquement + **6 majeurs** intégrés via cette refondation (c).

**Cet addendum v2 override l'addendum v1** sur les points concernés. Pour les autres aspects (Tasks 11.5 + 17.5 ajoutées, pattern flash, etc.), l'addendum v1 reste applicable.

### B-N2 override — Task 1 step 2 + Task 2 : signature `findBankIdxForPad` corrigée

**Constat empirique** : `struct BankSlot` (KeyboardData.h:369-379) ne contient PAS de champ `pad` (9 champs : channel, type, scale, arpEngine, loopEngine, isForeground, baseVelocity, velocityVariation, pitchBendOffset). Le mapping bank→pad vit dans `bankPads[NUM_BANKS]` variable locale `main.cpp:352`.

**Action Task 1 step 2 — refondre les helpers cross-store** :

```cpp
// =================================================================
// Cross-store role lookup helpers (Phase 3 — spec §13 refondée 2026-05-19)
// =================================================================
// Source unique de vérité pour la présence d'un rôle sur un pad.
// Consommés inline par Tool 3 (ToolPadRoles), Tool 4 (ToolControlPads).
// Pas de module centralisé (refusé YAGNI). Pas de cache redondant pour
// Scale/Arp/Bank arrays (refusé refondation (c) — l'architecture existing
// maintient ces arrays comme variables locales main.cpp).

// --- LoopPadStore : cached in NvsManager._loadedLoopPad ---

inline bool isLoopControlPad(const LoopPadStore& s, uint8_t pad) {
  return s.recPad == pad || s.playStopPad == pad || s.clearPad == pad;
}

inline int8_t findLoopSlotIdx(const LoopPadStore& s, uint8_t pad) {
  for (uint8_t i = 0; i < 16; i++) {
    if (s.slotPads[i] == pad) return (int8_t)i;
  }
  return -1;
}

// --- ControlPadStore : cached in NvsManager._ctrlStore ---

inline int8_t findControlPadEntryIdx(const ControlPadStore& s, uint8_t pad) {
  for (uint8_t i = 0; i < s.count; i++) {
    if (s.entries[i].padIndex == pad) return (int8_t)i;
  }
  return -1;
}

// --- Scale roles : owned by main.cpp (rootPads[7], modePads[7], chromaticPad) ---

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

// --- Arp roles : owned by main.cpp (holdPad, octavePads[4]) ---

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

// --- Bank assignment : owned by main.cpp (bankPads[NUM_BANKS]) ---
// Note : struct BankSlot ne contient PAS de champ `pad` — le mapping vit dans
// bankPads[]. Helper inline OK (pas besoin d'externalisation NvsManager.cpp).

inline int8_t findBankIdxForPad(const uint8_t* bankPads, uint8_t pad) {
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (bankPads[i] == pad) return (int8_t)i;
  }
  return -1;
}
```

**Action Task 2** : **annulée**. Plus besoin d'externalisation dans NvsManager.cpp — `findBankIdxForPad` est inline dans KeyboardData.h. Renumérotation : Task 2 supprimée, Task 3 (getters audit) renommée Task 2.

### B-N3 override — Task 17 step 3 : validator dans les deux branches (pré-init 0xFF)

**Constat empirique** : `NvsManager.cpp:1006-1022` montre validator dans la branche IF uniquement. Branche ELSE (NVS empty) couverte par dev seed M7 (à retirer Task 26). Sans fix, retrait dev seed = recPad/PS/CLEAR jamais initialisés = invariant 12 cassé.

**Action Task 17 step 3 — wire validator dans les deux branches** :

Remplacer le bloc actuel `NvsManager.cpp:1006-1022` par :

```cpp
// === LoopPadStore (Phase 1 declared, Phase 2 loaded, Phase 3 defaults) ===
{
  LoopPadStore tmp;
  // Phase 3 : pré-init aux sentinels 0xFF pour que validator puisse appliquer
  // defaults sur la branche else (NVS empty). Magic/version cohérents.
  memset(&tmp, 0xFF, sizeof(tmp));
  tmp.magic    = EEPROM_MAGIC;
  tmp.version  = LOOPPAD_VERSION;
  tmp.reserved = 0;

  bool loaded = loadBlob(LOOPPAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY,
                          EEPROM_MAGIC, LOOPPAD_VERSION, &tmp, sizeof(tmp));
  validateLoopPadStore(tmp);   // Phase 3 : appliqué TOUJOURS (hors du if)
  _loadedLoopPad = tmp;

  #if DEBUG_SERIAL
  if (loaded) {
    Serial.printf("[BOOT NVS] LoopPadStore loaded : rec=%u playStop=%u clear=%u\n",
                  _loadedLoopPad.recPad, _loadedLoopPad.playStopPad, _loadedLoopPad.clearPad);
  } else {
    Serial.printf("[BOOT NVS] LoopPadStore empty, Phase 3 defaults applied : rec=%u playStop=%u clear=%u\n",
                  _loadedLoopPad.recPad, _loadedLoopPad.playStopPad, _loadedLoopPad.clearPad);
  }
  #endif
}
```

**Hard-assert** Task 17 step 3 : `grep -A 5 "validateLoopPadStore" src/managers/NvsManager.cpp | grep -c "outside if"` ou check manuel — confirmer validator hors du if.

### M8 override — Task 3 (anciennement Task 3 = audit getters NvsManager)

**Constat empirique** : `_loadedScalePad` et `_loadedArpPad` n'existent pas dans NvsManager. Pas de cache. Les arrays vivent dans main.cpp.

**Action Task 3 reformulée** :
- **Aucun getter Scale/Arp à ajouter** dans NvsManager. Refacto annulé.
- **Vérifier** existence de `getLoadedLoopPadStore()` (Phase 2) et `getLoadedControlPadStore()` / `getCtrlStore()` ou équivalent (existing).
- Si `setLoadedLoopPad(const LoopPadStore&)` + `saveLoopPad()` n'existent pas, les **ajouter** (Task 21 dépend). Pattern Preferences existing comme `saveBank()`.

**Renumérotation après suppression de l'ancienne Task 2** :
- Ancien Task 1 = nouveau Task 1 (helpers cross-store inline §B-N2).
- Ancien Task 2 (`findBankIdxForPad` impl NvsManager.cpp) = **supprimé** (inline maintenant).
- Ancien Task 3 = nouveau Task 2 (getters NvsManager — désormais juste vérification + ajout `setLoadedLoopPad` + `saveLoopPad` si manquants).
- Tasks suivantes 4-27 inchangées en numéro.

**Conséquence Tasks 11, 13, 18, 20, 23** : les appels aux helpers doivent utiliser les members `_rootPads`, `_modePads`, `_chromaticPad`, `_holdPad`, `_octavePads`, `_bankPads` (Tool 3) ou les pointers via NvsManager.getLoadedLoopPadStore() (uniquement LOOP / CP). Verbatim corrigé :

```cpp
// Dans _drawGridNorm/Arpeg/Loop, scan cross-context :
ScaleRoleResult sr = scaleRoleAtPad(_rootPads, _modePads, *_chromaticPad, pad);
// (note : _chromaticPad est uint8_t* member, donc déréf)

ArpRoleResult ar = arpRoleAtPad(*_holdPad, _octavePads, pad);
// (note : _holdPad est uint8_t* member, donc déréf)

int8_t bankIdx = findBankIdxForPad(_bankPads, pad);

int8_t cpIdx = findControlPadEntryIdx(_nvs->getLoadedControlPadStore(), pad);
// (member `_nvs` ajouté Phase 3 via B-N1)

bool loopCtrl = isLoopControlPad(_wkLoopPad, pad);  // ou _nvs->getLoadedLoopPadStore() selon contexte
```

### B-N1 override étendu — Task 8 step 2 : 4 dérivés explicites

**Action Task 8 step 2 étendue** (au-delà du nom variable `_toolRoles`) :

1. **`ToolPadRoles.h`** : ajouter member privé :
```cpp
private:
  // ... existing members ...
  NvsManager* _nvs;   // Phase 3 — pour LoopPadStore + ControlPadStore lookup
```

2. **`ToolPadRoles::ToolPadRoles()`** : ajouter dans init list :
```cpp
ToolPadRoles::ToolPadRoles()
  : _keyboard(nullptr), _leds(nullptr), _ui(nullptr),
    _nvs(nullptr),   // <-- Phase 3
    // ... existing inits ...
```

3. **`ToolPadRoles::begin()`** : étendre signature + assignement :
```cpp
void ToolPadRoles::begin(CapacitiveKeyboard* keyboard, LedController* leds,
                          SetupUI* ui, NvsManager* nvs,   // <-- Phase 3 nvs added
                          uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
                          uint8_t& chromaticPad, uint8_t& holdPad,
                          uint8_t* octavePads) {
  _keyboard     = keyboard;
  _leds         = leds;
  _ui           = ui;
  _nvs          = nvs;   // <-- Phase 3
  // ... existing assignments ...
  // Phase 3 — load LoopPadStore working copy from cached state
  _wkLoopPad = nvs->getLoadedLoopPadStore();
}
```

4. **`SetupManager.cpp:30`** : étendre call site :
```cpp
// AVANT :
_toolRoles.begin(keyboard, leds, &_ui, bankPads, rootPads, modePads,
                  chromaticPad, holdPad, octavePads);

// APRÈS (Phase 3) :
_toolRoles.begin(keyboard, leds, &_ui, &_nvsManager,   // <-- &_nvsManager added
                  bankPads, rootPads, modePads,
                  chromaticPad, holdPad, octavePads);
```

**Vérifier** que `SetupManager` a un member `_nvsManager` accessible — `grep "_nvsManager\|NvsManager" src/setup/SetupManager.h` pour confirmer. Si nom différent, adapter.

### M14 override — Task 10 step 2 : SetupUI API `drawFrameLine` + VT100 escapes

**Constat empirique** : `SetupUI.h:147,165` montre `drawFrameLine` et `drawCellGrid`. Pas de `moveCursor` ni `setInverse`. Le pattern Tool 3 existing utilise inline VT100 escapes (cf `ToolPadRoles.cpp:339` : `VT_CYAN VT_BOLD "> " VT_RESET`).

**Action Task 10 step 2 reformulée** — verbatim corrigé :

```cpp
void ToolPadRoles::_drawSubPageHeader() {
  const char* labels[SUB_COUNT] = { "NORM", "ARPEG", "LOOP" };
  char buf[128];
  int pos = 0;
  pos += snprintf(buf + pos, sizeof(buf) - pos, "Pad Roles  [");
  for (uint8_t i = 0; i < SUB_COUNT; i++) {
    if (i == _activeSubPage) {
      // Active subpage : reverse + bold via VT100 escapes
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                       VT_REVERSE VT_BOLD "%s" VT_RESET, labels[i]);
    } else {
      // Inactive : dim
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                       VT_DIM "%s" VT_RESET, labels[i]);
    }
    if (i < SUB_COUNT - 1) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, "|");
    }
  }
  pos += snprintf(buf + pos, sizeof(buf) - pos, "]");
  _ui->drawFrameLine("%s", buf);
}
```

**Vérifier** que `VT_REVERSE`, `VT_BOLD`, `VT_DIM`, `VT_RESET` sont définis dans `SetupCommon.h` ou un header VT100 commun. Si manquants, soit les ajouter, soit utiliser les escapes ANSI bruts (`"\e[7m"` pour reverse, `"\e[1m"` bold, `"\e[2m"` dim, `"\e[0m"` reset).

### M9 — Task 17 : note transitoire dev seed

**Action** : ajouter commentaire dans Task 17 :
```cpp
// Phase 3 defaults 30/31/32 + Phase 2 dev seed (32/33/34) coexistent transitoirement
// entre Task 17 (3.E) et Task 26 (3.G). Cosmetic only — validator ne ré-applique
// pas defaults si recPad != 0xFF. Task 26 retire le dev seed après G6 validation.
```

### M10 — addendum v1 M4 override : unifier swap-to-pool intra-LOOP

**Action** : Task 17.5 step 6 (addendum v1) → modifier le dispatch :

```cpp
void ToolPadRoles::assignRole(uint8_t pad, uint8_t line, uint8_t index) {
  // ... existing ARPEG/NORM assignements (line 1-5, le `run()` ligne 759-766
  // fait déjà le swap-to-pool silencieux via clearRole(owner) avant cet appel) ...

  // Phase 3 — LOOP : assignLoopRole responsible du swap-to-pool intra-LOOP.
  // Le caller `run()` NE doit PAS clearRole(owner) pour line 6/7 — laisser
  // assignLoopRole gérer (sinon double-clear).
  } else if (line == 6 || line == 7) {
    assignLoopRole(pad, line, index);
  }
}
```

**Adaptation `run()` lignes 759-766** : ajouter check pour skip le clearRole(owner) pre-call si line == 6 || line == 7 :

```cpp
} else {
  // Steal silencieux : pour ARPEG (line 2-5), le caller libère owner.
  // Phase 3 : pour LOOP (line 6-7), assignLoopRole gère le swap internement.
  if (_poolLine < 6) {
    uint8_t owner = findPadWithRole(_poolLine, _poolIdx);
    if (owner < NUM_KEYS && owner != (uint8_t)pad) {
      clearRole(owner);
      _setFlash("Previous role returned to pool.");   // M1 audit v1 — flash
    }
    clearRole((uint8_t)pad);
  }
  assignRole((uint8_t)pad, _poolLine, _poolIdx);
  if (saveAll()) {
    _ui->flashSaved();
    _editing = false;
  }
  screenDirty = true;
}
```

### M11 — Task 21 : comportement partial-fail `saveAll`

**Action** : ajouter dans Task 21 step 2 un commentaire de comportement :

```cpp
bool ToolPadRoles::saveAll() {
  bool ok = true;
  // ... existing saves (bank, scale, arp) ...
  // Each save returns bool ; on partial fail, log warning but continue.

  // Phase 3 — save LoopPadStore working copy
  if (_nvs) {
    _nvs->setLoadedLoopPad(_wkLoopPad);
    if (!_nvs->saveLoopPad()) {
      #if DEBUG_SERIAL
      Serial.println("[Tool 3] WARN: saveLoopPad failed, NVS may be inconsistent.");
      #endif
      ok = false;
    }
  }

  return ok;
}
```

Setup mode = rare-action ; NVS sain en pratique. Warning suffit, pas de rollback. Documenter dans commit message Phase 3.E.

### M12 — Task 20 : préserver LOOP controls dans `clearAllRoles`

**Action** : ajouter dans Task 20 un step explicite :

```cpp
void ToolPadRoles::clearAllRoles() {
  memset(_wkBankPads, 0xFF, sizeof(_wkBankPads));
  memset(_wkRootPads, 0xFF, sizeof(_wkRootPads));
  memset(_wkModePads, 0xFF, sizeof(_wkModePads));
  _wkChromPad = 0xFF;
  _wkHoldPad = 0xFF;
  memset(_wkOctavePads, 0xFF, sizeof(_wkOctavePads));

  // Phase 3 — invariant 12 : LOOP controls NEVER cleared.
  // Slots OK to clear (tolerate 0xFF).
  memset(_wkLoopPad.slotPads, 0xFF, sizeof(_wkLoopPad.slotPads));
  // _wkLoopPad.recPad / playStopPad / clearPad PRESERVED.
}
```

### Mineurs m12-m17 — actions ponctuelles

- **m12** : labels alignement 4-char width. Utiliser ` REC`, ` PS_`, ` CLR`, ` S00..S15` (leading space). Idem ` Hd`, ` Oc` deviennent réutilisations de `GRID_HOLD_LABELS[0]` et `GRID_OCTAVE_LABELS[i]`.
- **m13** : réutiliser constants `GRID_*_LABELS` existants (Tasks 13, 18).
- **m14** : factorisation `buildRoleMap` (pas `drawGrid`). Override M3 addendum v1 :
  - Au lieu de `_drawGridLegacy()` extraction, factoriser dans `_buildRoleMapLegacy()`.
  - `drawGrid()` Tool 3 reste 3 lignes (dispatch `_roleMap` via `_ui->drawCellGrid`).
  - Nouveaux `_buildRoleMapNorm/_buildRoleMapArpeg/_buildRoleMapLoop` construisent `_roleMap[NUM_KEYS]` + `_roleLabels[NUM_KEYS][6]` selon `_activeSubPage`.
- **m15** : trancher **ASCII `.`** pour cross-context dim marker (pas UTF-8 `·`).
- **m16** : flash render entre `drawInfoPanel()` et `drawControlBar()` (pas overwrite control bar).
- **m17** : Task 22 HW gate G4 — documenter test hard-constraint exit comme défense en profondeur (avec defaults validator, unreachable en pratique).

---

## Recap revised v2 — Phase 3 tasks count

Après refondation (c) :

| Sous-phase | Tasks | Δ vs v1 | Δ vs initial |
|---|---|---|---|
| 3.A | **1, 2** (ex-3) | -1 (Task 2 ancien `findBankIdxForPad` impl supprimée, inline) | -1 |
| 3.B | 4, 5, 6 | inchangé | inchangé |
| 3.C | 7, 8, 9, 10, 11, 11.5, 12 | inchangé | +1 |
| 3.D | 13, 14, 15, 16 | inchangé | inchangé |
| 3.E | 17, 17.5, 18, 19, 20, 21, 22 | inchangé | +1 |
| 3.F | 23, 24, 25 | inchangé | inchangé |
| 3.G | 26 | inchangé | inchangé |
| 3.H | 27 | inchangé | inchangé |

**Total v2** : 28 tasks (vs 29 v1 vs 27 initial). 6 HW gates G1-G6 inchangé, 8 commits.

---

**Plan v2 — refondation (c) intégrée. Prêt pour session EXEC.**

Lecture obligatoire EXEC : ce plan + audit v1 + audit indépendant + manifeste + spec design (refondue §13 + §22 + §2).
