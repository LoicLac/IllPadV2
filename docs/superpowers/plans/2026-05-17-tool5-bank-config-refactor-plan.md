# Tool 5 Bank Config — Refacto — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL — Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** Refacto complet du Tool 5 setup mode (`src/setup/ToolBankConfig.{h,cpp}`) — vue tableau matriciel banks × params, nav 2D, INFO auto-update, validator `quantize[]` contextuel pour LOOP. Pas de bump NVS. À exécuter **pré-Phase 2 LOOP**.

**Architecture :** 5 tasks séquentielles. Task 1 = constantes `HardwareConfig` + validator contextuel `KeyboardData`. Task 2 = header refacto (struct unifiée + table déclarative `PARAM_TABLE`). Task 3 = body refacto complet (rewrite `ToolBankConfig.cpp` avec rendering + nav 2D + édition). Task 4 = retrait helpers NvsManager caducs. Task 5 = HW non-régression complète + doc sync.

**Tech Stack :** C++17 / PlatformIO / ESP32-S3 / FreeRTOS dual-core. Zero Migration Policy (CLAUDE.md). Pas de unit tests — vérification = `pio run` (compile) + static read-back grep + HW gate bloquant sur Task 3.

**Sources de référence :**
- Spec Tool 5 refacto : [`docs/superpowers/specs/2026-05-17-tool5-bank-config-refactor-design.md`](../specs/2026-05-17-tool5-bank-config-refactor-design.md) — 12 décisions D1-D12, mockup, code couleurs, comportements détaillés.
- État du code `main` au commit `fb67ca1`.
- Spec LOOP `docs/superpowers/specs/2026-04-19-loop-mode-design.md` §6 footnote + §28 Q6 (refacto Tool 5 acté pré-Phase 2).
- VT100 reference : [`docs/reference/vt100-design-guide.md`](../../reference/vt100-design-guide.md).
- Setup tools conventions : [`docs/reference/setup-tools-conventions.md`](../../reference/setup-tools-conventions.md).
- Architecture briefing : [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) §4 Table 1, §8 Domain entry points.
- CLAUDE.md user — git workflow autocommit, actions destructives, lire-avant-de-proposer.
- CLAUDE.md projet — 5 gates par task (Code / Build / Auto-review / HW gate / Commit), HW gate AVANT commit gate.

---

## §0 — Décisions pré-actées (références spec)

Reprises de la spec Tool 5 refacto §19. **Ne pas rebattre.**

| # | Décision | Tasks impactées |
|---|---|---|
| D1 | Paradigme : tableau matriciel banks (colonnes) × params (lignes) unique | T3 (rendering) |
| D2 | Nav 2D `^v` param vertical + `<>` bank horizontal, wrap aux bords, stricte cell-par-cell | T3 (event loop) |
| D3 | Différenciation éditable / non-éditable : `·` VT_DIM ; éditable couleur catégorie ; focus éditable `[X]` cyan bold, focus non-éditable `[·]` dim | T3 (drawTable) |
| D4 | INFO auto-update sur chaque mouvement (3 états : éditable / non-éditable / en édition) | T3 (drawInfo) |
| D5 | Type cycle 4 valeurs (NORMAL/ARPEG/ARPEG_GEN/LOOP), Type + Quantize = 2 fields distincts | T1 + T3 |
| D6 | Quantize options dynamiques selon type ; storage même field `quantize[]` discriminé par type | T1 (validator) + T3 (rendering) |
| D7 | Sections inline `─ TYPE ─` `─ SCALE ─` `─ WALK ─` portées par bordures horizontales | T3 (drawTable) |
| D8 | Couleur ARPEG_GEN = Magenta VT-only (pas de slot runtime dédié) | T3 (drawTable) |
| D9 | Working-copy struct unifiée + table déclarative `PARAM_TABLE` + fonctions courtes | T2 (header) + T3 (body) |
| D10 | `d` Defaults = reset cell focus uniquement (pas tableau entier) | T3 (event loop) |
| D11 | Cap MAX_ARP_BANKS / MAX_LOOP_BANKS : skip type au cap, INFO indique `(cap atteint)`. `MAX_LOOP_BANKS = 2` placeholder | T1 (constante) + T3 (cycle Type) |
| D12 | NvsManager refacto : retrait paires `Loaded*` Tool-5-only (si non utilisées ailleurs) | T4 |

---

## §1 — File structure overview

Fichiers touchés par Task.

| Fichier | Tasks | Rôle Refacto |
|---|---|---|
| [`src/core/HardwareConfig.h`](../../../src/core/HardwareConfig.h) | T1 | Ajout `MAX_LOOP_BANKS = 2`. |
| [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h) | T1 | `validateBankTypeStore` étendu : discrimination contextuelle `quantize[]` (ARPEG/ARPEG_GEN clamp 0..1 ; LOOP clamp 0..2 default 2 Bar). |
| [`src/setup/ToolBankConfig.h`](../../../src/setup/ToolBankConfig.h) | T2 | Refonte complète. Struct `Tool5Working`, struct `ParamDescriptor`, déclaration `PARAM_TABLE`. |
| [`src/setup/ToolBankConfig.cpp`](../../../src/setup/ToolBankConfig.cpp) | T3 | Rewrite complet. `loadWorkingCopy`, `drawTable`, `drawInfo`, `handleNavigation`, `handleEdition`, `applyDefault`, `saveAll`, `run`. |
| [`src/managers/NvsManager.h`](../../../src/managers/NvsManager.h) | T4 | Retrait conditionnel des paires `getLoadedBonusPile/setLoadedBonusPile`, `getLoadedMarginWalk/setLoadedMarginWalk`, `getLoadedProximityFactor/setLoadedProximityFactor`, `getLoadedEcart/setLoadedEcart` si grep montre 0 consommateur hors Tool 5. |
| [`src/managers/NvsManager.cpp`](../../../src/managers/NvsManager.cpp) | T4 | Idem retrait bodies. |
| [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) | T5 | §4 Table 1 ligne `BankTypeStore` : note "Tool 5 refacto livré 2026-05-17, validator contextuel quantize". §8 ligne Tool 5 mise à jour. |
| [`docs/reference/setup-tools-conventions.md`](../../reference/setup-tools-conventions.md) | T5 (opt) | Ajouter référence au nouveau paradigme tableau matriciel pour les futurs Tools multi-banks (Tool 7 LOOP page Phase 4 pourra s'en inspirer). |

**Pas touché :** Tools 1/2/3/4/6/7/8, runtime LedController/BankManager/ScaleManager/PotRouter/ArpEngine/MidiEngine, stores LOOP existants (LoopPadStore, LoopPotStore), validators LOOP existants. Le runtime ARPEG_GEN, NORMAL, ARPEG reste strictement inchangé musicalement (non-régression complète attendue Task 5).

---

## §2 — Graphe dépendances inter-tasks

```
Task 1 (HardwareConfig MAX_LOOP_BANKS + KeyboardData validator contextuel)
   │    [prereq : MAX_LOOP_BANKS visible avant compilation ToolBankConfig]
   │
Task 2 (Header ToolBankConfig.h : Tool5Working + ParamDescriptor + PARAM_TABLE)
   │    [prereq Task 1 : constantes + validator alignés sur les nouveaux types]
   │
Task 3 (Body ToolBankConfig.cpp : rewrite complet)
   │    [prereq Task 2 : header utilisable]
   │    [HW Checkpoint A — bloquant : rendering + nav + édition + persistance OK]
   │
Task 4 (NvsManager retrait helpers Tool-5-only)
   │    [prereq Task 3 : Tool 5 nouveau ne les utilise plus]
   │
Task 5 (HW non-régression complète + doc sync)
        [prereq Task 4 : codebase clean]
        [HW Checkpoint B — bloquant : non-régression ARPEG_GEN complète]
```

**Ordre commits = ordre tasks** (1 → 5). Task 3 est le plus gros (rewrite ~500-700 LOC). Tasks 1, 2, 4 sont petites. Task 5 est HW + doc seule (pas de code).

**Fusion possible** : si Task 4 grep montre 0 consommateur des helpers ailleurs et qu'on veut un commit compact, fusionner T3 + T4 en un seul commit "refacto Tool 5 + retrait helpers caducs". Acceptable car bisect reste utile (commit complet, pas de mid-state cassé). À trancher en exécution selon ce que le grep révèle.

---

## §3 — Conventions de vérification firmware

**5 gates par Task** (cf CLAUDE.md projet "Workflow d'implémentation — 5 gates par task") :

1. **Code** — Read fichier cible intégral avant édition. Edit (jamais Write pour fichier existant sauf rewrite complet annoncé). Multi-fichiers : lire tous avant le premier edit.
2. **Build (compile gate)** : `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`. Exit 0, 0 nouveau warning (`-Wswitch`, `-Wunused-variable`, `-Wparentheses` clean). Bloque tant que rouge.
3. **Auto-review (static read-back)** : grep des symboles modifiés. Vérifier couverture call-sites. Cf chaque Task — section "Static read-back" liste les greps attendus.
4. **HW gate (si applicable)** : présenter les points HW à valider, attendre OK explicite. Compile passé ≠ ça marche ; le HW est seul juge final. HW Checkpoint A bloquant Task 3, HW Checkpoint B bloquant Task 5.
5. **Commit gate** : Mode autocommit actif (cf CLAUDE.md user). Commit auto au point de bascule, message HEREDOC inclus dans chaque Task. **`git push` reste explicite** — jamais sans demande utilisateur. **`git add` par fichiers individuels**, jamais `-A` ou `.`.

**Règle absolue** : HW gate AVANT commit gate, jamais après.

**Recap-table multi-axes** à maintenir pendant l'exécution :

| Task | Compile | HW | Commit |
|---|---|---|---|
| 1 | | n/a | |
| 2 | | n/a | |
| 3 | | **HW-A** | |
| 4 | | n/a | |
| 5 | | **HW-B** | (doc sync only commit, déjà committed code à T3) |

---

## Task 1 — Constantes `HardwareConfig` + validator contextuel `KeyboardData`

**Cross-refs :** Spec Tool 5 refacto §15 (cap `MAX_LOOP_BANKS = 2` placeholder), §16 (validator contextuel `quantize[]`).

**Files :**
- Modify : [`src/core/HardwareConfig.h`](../../../src/core/HardwareConfig.h) — ajout constante.
- Modify : [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h:702-718) — extension `validateBankTypeStore`.

**Steps :**

- [ ] **Step 1.1 — Lecture `HardwareConfig.h`**
  - Identifier la section où sont définis `MAX_ARP_BANKS`, `MAX_ARP_NOTES`, etc. (probablement dans la zone "V2 — Arp Constants" ou similaire).
  - Repérer le bon emplacement d'insertion logique (à côté de `MAX_ARP_BANKS = 4`).

- [ ] **Step 1.2 — Lecture `KeyboardData.h:702-718`**
  - Localiser `inline void validateBankTypeStore(BankTypeStore& s)`.
  - Confirmer la structure actuelle du clamp `quantize` :
    ```cpp
    if (s.quantize[i] >= NUM_ARP_START_MODES) s.quantize[i] = DEFAULT_ARP_START_MODE;
    ```
  - Repérer la position relative aux autres clamps (types, scaleGroup, bonusPilex10, etc.).

- [ ] **Step 1.3 — Édition `HardwareConfig.h`**
  - Ajouter, à côté de `MAX_ARP_BANKS = 4` :
    ```cpp
    const uint8_t MAX_LOOP_BANKS = 2;  // Placeholder pré-Phase 2 LOOP. Reconfirmé Phase 2 selon mesure SRAM réelle de LoopEngine. Cf spec Tool 5 refacto §15.
    ```

- [ ] **Step 1.4 — Édition `KeyboardData.h` validateBankTypeStore**
  - Remplacer le clamp unique de `quantize` par un clamp contextuel selon le type :
    ```cpp
    inline void validateBankTypeStore(BankTypeStore& s) {
      uint8_t arpCount = 0;
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        // Type clamp : tout > BANK_ARPEG_GEN (sauf BANK_ANY=0xFF) -> NORMAL
        if (s.types[i] > BANK_ARPEG_GEN && s.types[i] != BANK_ANY) s.types[i] = BANK_NORMAL;
        if (isArpType((BankType)s.types[i])) arpCount++;
        if (arpCount > MAX_ARP_BANKS) s.types[i] = BANK_NORMAL;
        // Quantize : interpretation discriminee par type (Tool 5 refacto §16).
        //   - ARPEG / ARPEG_GEN : 0..1 (Imm/Beat), default DEFAULT_ARP_START_MODE.
        //   - LOOP              : 0..2 (Free/Beat/Bar), default 2 (Bar).
        //   - NORMAL            : ignore (Tool 5 affiche `·`).
        if (isArpType((BankType)s.types[i])) {
          if (s.quantize[i] >= NUM_ARP_START_MODES) s.quantize[i] = DEFAULT_ARP_START_MODE;
        } else if (s.types[i] == BANK_LOOP) {
          if (s.quantize[i] >= 3) s.quantize[i] = 2;  // Bar default
        }
        // NORMAL : pas de clamp, le field est ignore.
        if (s.scaleGroup[i] > NUM_SCALE_GROUPS) s.scaleGroup[i] = 0;
        // V3 : nouveaux champs (clamp aux ranges declares en spec §21)
        if (s.bonusPilex10[i] < 10 || s.bonusPilex10[i] > 20) s.bonusPilex10[i] = 15;
        if (s.marginWalk[i]  < 3  || s.marginWalk[i]  > 12) s.marginWalk[i]  = 7;
        // V4 : walk tuning (Tool 5 override de la constante compile-time et de TABLE_GEN_POSITION ecart)
        if (s.proximityFactorx10[i] < 4 || s.proximityFactorx10[i] > 20) s.proximityFactorx10[i] = 4;
        if (s.ecart[i] < 1 || s.ecart[i] > 12) s.ecart[i] = 5;
      }
    }
    ```
  - **Note** : le cap `MAX_LOOP_BANKS` n'est pas dans le validator (le validator clamp les valeurs, pas les compositions — le cap LOOP est appliqué au cycle Type dans Tool 5, cf Task 3). Cohérent avec MAX_ARP_BANKS qui n'est pas non plus dans le validator (forcing à NORMAL si arpCount > cap est déjà là).

- [ ] **Step 1.5 — Compile gate**
  - Lancer `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`.
  - Vérifier exit 0, pas de warning nouveau.
  - Vérifier RAM / Flash usage inchangés (le validator est plus long de quelques lignes mais inlined).

- [ ] **Step 1.6 — Static read-back**
  - Grep `MAX_LOOP_BANKS` dans `src/` : confirmer présence dans `HardwareConfig.h` uniquement à ce stade.
  - Grep `s.quantize[i]` dans `KeyboardData.h` : confirmer les 2 branches `isArpType` / `BANK_LOOP` présentes.
  - Vérifier que le runtime existant (ArpEngine, BankManager) ne casse pas — ils lisent `quantize[]` après `validateBankTypeStore` au boot ; le validator reste safe pour ARPEG/ARPEG_GEN existant (clamp 0..1).

- [ ] **Step 1.7 — Commit (autocommit)**
  - Files : `src/core/HardwareConfig.h`, `src/core/KeyboardData.h`.
  - Message proposé :
    ```
    feat(tool5-refacto): step 1 — MAX_LOOP_BANKS + validator quantize contextuel

    HardwareConfig.h : MAX_LOOP_BANKS = 2 (placeholder pré-Phase 2 LOOP,
    reconfirmé Phase 2 selon mesure SRAM réelle LoopEngine).

    KeyboardData.h validateBankTypeStore : clamp quantize[] discriminé
    par type. ARPEG/ARPEG_GEN reste à 0..1 (NUM_ARP_START_MODES Imm/Beat).
    LOOP clamp à 0..2 default 2 (Free/Beat/Bar). NORMAL : pas de clamp,
    field ignoré côté Tool 5 (sera affiché `·`).

    Aucun reset user attendu (BankTypeStore v4 reste v4, layout identique).
    Pas d'observable runtime tant que Tool 5 refacto (T3) n'utilise pas
    le cycle Type cyclant vers LOOP. ARPEG_GEN inchangé musicalement.

    Refs : spec Tool 5 refacto §15 + §16. CLAUDE.md projet NVS Zero
    Migration (pas de bump v4 → v5 pour LOOP quantize 3-way).
    ```

---

## Task 2 — Header `ToolBankConfig.h` refacto

**Cross-refs :** Spec Tool 5 refacto §11 (architecture code : working-copy struct unifiée + table déclarative).

**Files :**
- Modify : [`src/setup/ToolBankConfig.h`](../../../src/setup/ToolBankConfig.h) — refonte complète du header.

**Cible structure :**

```cpp
#ifndef TOOL_BANK_CONFIG_H
#define TOOL_BANK_CONFIG_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class LedController;
class NvsManager;
class SetupUI;

// =================================================================
// Tool5Working — working copy struct unifiée (cf spec §11)
// =================================================================
// Remplace les 7 arrays globaux du current. Cancel-edit = 1 assignation
// `_wk = _saved;` au lieu de 7 memcpy. Phase 3 LOOP : pas de nouveau
// field — quantize[] est réinterprété selon types[] (cf KeyboardData.h
// validator contextuel, Task 1).
struct Tool5Working {
  BankType type[NUM_BANKS];
  uint8_t  quantize[NUM_BANKS];      // 0..1 si ARPEG/ARPEG_GEN ; 0..2 si LOOP ; ignoré si NORMAL
  uint8_t  scaleGroup[NUM_BANKS];
  uint8_t  bonusPilex10[NUM_BANKS];
  uint8_t  marginWalk[NUM_BANKS];
  uint8_t  proximityFactorx10[NUM_BANKS];
  uint8_t  ecart[NUM_BANKS];
};

// =================================================================
// ParamDescriptor — table déclarative des params (cf spec §11)
// =================================================================
// Une entrée par ligne du tableau. Permet d'ajouter un param/section
// en éditant uniquement la table (pas de code rendering ni handleEdit).
struct ParamOptions {
  // Discriminée selon mode : discrete (cycle de labels) OU range (continue).
  bool isDiscrete;
  union {
    struct { const char* const* labels; uint8_t count; } discrete;
    struct { int16_t minVal; int16_t maxVal; int16_t stepNormal; int16_t stepAccelerated; const char* unit; } range;
  };
};

struct ParamDescriptor {
  const char* label;             // Affiché en colonne label gauche : "Type", "Quantize", "Group", "Bonus", ...
  const char* sectionLabel;      // "TYPE" / "SCALE" / "WALK" — non-null si nouvelle section (ligne séparateur)
  uint8_t     fieldOffset;       // offsetof(Tool5Working, field) — accès via reinterpret_cast<uint8_t*>(&_wk) + fieldOffset + bank
  bool        (*isApplicable)(BankType type);            // function pointer : cell éditable pour ce type ?
  const ParamOptions& (*getOptions)(BankType type);      // function pointer : options dynamiques selon type
  const char* infoDescription;                           // texte INFO state 1 (éditable)
  const char* infoNonApplicable;                         // texte INFO state 2 (non-éditable), suffixé par hint type
};

extern const ParamDescriptor PARAM_TABLE[];
extern const uint8_t         PARAM_TABLE_COUNT;

// =================================================================
// ToolBankConfig — refacto 2026-05-17 (cf spec Tool 5 refacto)
// =================================================================
class ToolBankConfig {
public:
  ToolBankConfig();
  void begin(LedController* leds, NvsManager* nvs, SetupUI* ui, BankSlot* banks);
  void run();  // Blocking — nav 2D tableau matriciel banks × params

private:
  // --- Data loading / saving ---
  void loadWorkingCopy();                          // NVS load + seed defaults
  bool saveAll();                                  // commit NVS + sync _banks. Returns true on success.

  // --- Rendering (pure) ---
  void drawTable();                                // tableau matriciel, focus highlighting
  void drawInfo();                                 // INFO panel selon _cursor + _editing

  // --- Event handling ---
  void handleNavigation(const NavEvent& ev);       // ^v<> hors édition
  void handleEdition(const NavEvent& ev);          // ^v / RET / q en édition
  void applyDefaults();                            // `d` reset cell focus

  // --- Helpers ---
  bool isCellApplicable(uint8_t paramIdx, uint8_t bankIdx) const;
  uint8_t  getCellValue(uint8_t paramIdx, uint8_t bankIdx) const;
  void     setCellValue(uint8_t paramIdx, uint8_t bankIdx, uint8_t value);
  uint8_t  countBanksOfType(BankType type) const;  // pour cap MAX_ARP_BANKS / MAX_LOOP_BANKS

  // --- State ---
  Tool5Working _wk;        // working copy (current edit state)
  Tool5Working _saved;     // snapshot pour cancel-edit
  uint8_t      _cursorParam;   // 0..PARAM_TABLE_COUNT-1
  uint8_t      _cursorBank;    // 0..NUM_BANKS-1
  bool         _editing;
  bool         _screenDirty;
  bool         _errorShown;
  unsigned long _errorTime;
  bool         _nvsSaved;

  // --- Backref ---
  LedController* _leds;
  NvsManager*    _nvs;
  SetupUI*       _ui;
  BankSlot*      _banks;
};

#endif // TOOL_BANK_CONFIG_H
```

**Steps :**

- [ ] **Step 2.1 — Lecture intégrale `ToolBankConfig.h` current** (31 lignes)
  - Confirmer signatures publiques actuelles (`ToolBankConfig()`, `begin`, `run`).
  - Confirmer présence des `private:` `saveConfig` + `drawDescription` à supprimer.

- [ ] **Step 2.2 — Lecture `NavEvent` definition** (probablement dans `InputParser.h`)
  - Grep `struct NavEvent` ou `typedef.*NavEvent` dans `src/setup/`.
  - Confirmer la signature avant de la référencer dans `handleNavigation(const NavEvent& ev)`.

- [ ] **Step 2.3 — Rewrite `ToolBankConfig.h`**
  - Remplacer le contenu intégral du fichier par le bloc cible ci-dessus.
  - Vérifier les includes : `KeyboardData.h` (pour `BankType`, `BankSlot`, `NUM_BANKS`), `HardwareConfig.h` (pour `MAX_LOOP_BANKS`).
  - Si `NavEvent` doit être inclus, ajouter `#include "InputParser.h"` (ou équivalent selon Step 2.2). Sinon forward-declare `struct NavEvent;`.

- [ ] **Step 2.4 — Compile gate**
  - `pio run` → exit 0. Le header seul ne casse rien tant que `ToolBankConfig.cpp` n'est pas réécrit, MAIS il peut casser le current .cpp qui référence `saveConfig` 7-params et `drawDescription`. Donc compile sera **rouge** à ce stade.
  - **Tolérance temporaire** : le compile peut rester rouge entre Task 2 et Task 3 (le .cpp reste sur l'ancienne signature). C'est explicitement accepté pour ce refacto — l'agent doit savoir que le compile redevient vert à la fin de Task 3.
  - **Alternative pour rester compile-vert** : différer le commit de Task 2 et le fusionner avec Task 3 (cas particulier de refacto où header et body évoluent ensemble). **Décision pragmatique** : faire un commit séparé Task 2 = "WIP : header refacto, body suit Task 3" avec note explicite. Bisect garde l'intention claire.

- [ ] **Step 2.5 — Static read-back**
  - Grep `Tool5Working`, `ParamDescriptor`, `PARAM_TABLE` dans `src/` : présents uniquement dans le header (le .cpp les utilisera en Task 3).
  - Confirmer que les anciennes signatures `saveConfig`/`drawDescription` ne sont plus déclarées dans le header.

- [ ] **Step 2.6 — Commit (autocommit)**
  - Files : `src/setup/ToolBankConfig.h`.
  - Message proposé :
    ```
    refactor(tool5-refacto): step 2 — header refacto (Tool5Working + PARAM_TABLE)

    Refonte complète de ToolBankConfig.h. Ajout :
    - struct Tool5Working : working copy unifiée (7 arrays NUM_BANKS).
    - struct ParamOptions + ParamDescriptor : table déclarative.
    - extern PARAM_TABLE + PARAM_TABLE_COUNT (définis dans .cpp Task 3).
    - Class ToolBankConfig refondue : loadWorkingCopy, saveAll, drawTable,
      drawInfo, handleNavigation, handleEdition, applyDefaults, helpers.

    Compile temporairement ROUGE : le current .cpp utilise encore les
    anciennes signatures (saveConfig 7-params, drawDescription). Resolved
    par Task 3 (body rewrite).

    Refs : spec Tool 5 refacto §11 (architecture code).
    ```

---

## Task 3 — Body `ToolBankConfig.cpp` refacto complet

**Cross-refs :** Spec Tool 5 refacto §4 (mockup), §5 (cells éditables/non), §6 (couleurs), §7 (nav + INFO), §8 (édition par cell), §9 (sections inline), §11 (architecture code), §12 (conventions VT100).

**Files :**
- Rewrite : [`src/setup/ToolBankConfig.cpp`](../../../src/setup/ToolBankConfig.cpp) — rewrite intégral.

**Steps :**

- [ ] **Step 3.1 — Lecture du current `ToolBankConfig.cpp` intégrale** (712 lignes)
  - Identifier toutes les conventions utilisées : `_ui->vtFrameStart`, `_ui->drawFrameLine`, `_ui->drawSection`, `_ui->drawConsoleHeader`, `_ui->drawControlBar`, `_ui->flashSaved`, `VT_CYAN`, `VT_BOLD`, `VT_RESET`, `VT_DIM`, `VT_BRIGHT_WHITE`, `VT_YELLOW`, `VT_BRIGHT_RED`, `CBAR_SEP`, `CBAR_CONFIRM_ANY`, `NAV_*`.
  - Identifier helpers existants : `SetupUI::parseConfirm`, `InputParser::update`, etc.
  - Identifier le pattern de save : `saveConfig` → `NvsManager::saveBlob(BANKTYPE_NVS_NAMESPACE, ...)` puis sync `_banks[i].type` et `_nvs->setLoadedXxx`.

- [ ] **Step 3.2 — Lecture `SetupUI` + `InputParser`** signatures publiques utilisées
  - Confirmer les signatures qu'on va appeler dans le rewrite.

- [ ] **Step 3.3 — Définir `PARAM_TABLE` en `.cpp`**
  - Définir les `ParamOptions` statiques pour chaque param :
    ```cpp
    static const char* const TYPE_LABELS[] = { "NORM", "ARPG", "AGEN", "LOOP" };
    static const ParamOptions TYPE_OPTIONS = {
      .isDiscrete = true,
      .discrete = { TYPE_LABELS, 4 }
    };

    static const char* const QUANTIZE_LABELS_ARP[] = { "Imm", "Beat" };
    static const char* const QUANTIZE_LABELS_LOOP[] = { "Free", "Beat", "Bar" };
    static const ParamOptions QUANTIZE_OPTIONS_ARP  = { true, { QUANTIZE_LABELS_ARP, 2 } };
    static const ParamOptions QUANTIZE_OPTIONS_LOOP = { true, { QUANTIZE_LABELS_LOOP, 3 } };

    static const char* const GROUP_LABELS[] = { "-", "A", "B", "C", "D" };
    static const ParamOptions GROUP_OPTIONS = { true, { GROUP_LABELS, 5 } };

    static const ParamOptions BONUS_OPTIONS   = { false, .range = { 10, 20, 1, 5, "" } };  // affichage / 10.0
    static const ParamOptions MARGIN_OPTIONS  = { false, .range = { 3, 12, 1, 3, "" } };
    static const ParamOptions PROX_OPTIONS    = { false, .range = { 4, 20, 1, 5, "" } };
    static const ParamOptions ECART_OPTIONS   = { false, .range = { 1, 12, 1, 3, "" } };
    ```
  - Implémenter `isApplicable*` et `getOptions*` :
    ```cpp
    static bool isAlways(BankType t) { (void)t; return true; }
    static bool isArpOrLoop(BankType t) { return isArpType(t) || t == BANK_LOOP; }
    static bool isNotLoop(BankType t)   { return t != BANK_LOOP; }
    static bool isArpegGenOnly(BankType t) { return t == BANK_ARPEG_GEN; }

    static const ParamOptions& typeOpts(BankType t)     { (void)t; return TYPE_OPTIONS; }
    static const ParamOptions& quantizeOpts(BankType t) {
      if (t == BANK_LOOP) return QUANTIZE_OPTIONS_LOOP;
      return QUANTIZE_OPTIONS_ARP;
    }
    static const ParamOptions& groupOpts(BankType t)    { (void)t; return GROUP_OPTIONS; }
    static const ParamOptions& bonusOpts(BankType t)    { (void)t; return BONUS_OPTIONS; }
    static const ParamOptions& marginOpts(BankType t)   { (void)t; return MARGIN_OPTIONS; }
    static const ParamOptions& proxOpts(BankType t)     { (void)t; return PROX_OPTIONS; }
    static const ParamOptions& ecartOpts(BankType t)    { (void)t; return ECART_OPTIONS; }
    ```
  - Définir la `PARAM_TABLE` :
    ```cpp
    const ParamDescriptor PARAM_TABLE[] = {
      // section TYPE
      { "Type",     "TYPE",  offsetof(Tool5Working, type[0]),                isAlways,        typeOpts,
        "Bank type. Cycle 4 valeurs : NORMAL (jeu mélodique), ARPEG (arpégiateur classique), ARPEG_GEN (arpégiateur génératif), LOOP (loop percussif).",
        nullptr /* Type est toujours applicable */ },
      { "Quantize", nullptr, offsetof(Tool5Working, quantize[0]),            isArpOrLoop,     quantizeOpts,
        "Quantize start/stop : ARPEG/ARPEG_GEN = Imm (clock division)/Beat (1/4 note). LOOP = Free/Beat/Bar.",
        "Quantize n'a de sens que sur ARPEG / ARPEG_GEN / LOOP." },
      // section SCALE
      { "Group",    "SCALE", offsetof(Tool5Working, scaleGroup[0]),          isNotLoop,       groupOpts,
        "Scale group : root/mode changes propagate to all banks of same group.",
        "Scale group ignoré sur LOOP (invariant 6 : pas de scale sur bank LOOP)." },
      // section WALK (ARPEG_GEN only)
      { "Bonus",    "WALK",  offsetof(Tool5Working, bonusPilex10[0]),        isArpegGenOnly,  bonusOpts,
        "Walk weight on pile degrees during mutation. Higher = mutations stay anchored to pile.",
        "Bonus pile ARPEG_GEN-only. Change Type to ARPEG_GEN to enable." },
      { "Margin",   nullptr, offsetof(Tool5Working, marginWalk[0]),          isArpegGenOnly,  marginOpts,
        "Margin walk : how far the walk can drift above/below pile range.",
        "Margin walk ARPEG_GEN-only. Change Type to ARPEG_GEN to enable." },
      { "Prox",     nullptr, offsetof(Tool5Working, proximityFactorx10[0]),  isArpegGenOnly,  proxOpts,
        "Proximity factor : exponential falloff steepness. Smaller = step-wise. Larger = erratic.",
        "Prox ARPEG_GEN-only. Change Type to ARPEG_GEN to enable." },
      { "Ecart",    nullptr, offsetof(Tool5Working, ecart[0]),               isArpegGenOnly,  ecartOpts,
        "Max degree jump between consecutive steps. Overrides R2+hold ecart.",
        "Ecart ARPEG_GEN-only. Change Type to ARPEG_GEN to enable." },
    };
    const uint8_t PARAM_TABLE_COUNT = sizeof(PARAM_TABLE) / sizeof(PARAM_TABLE[0]);
    ```
  - **Note `offsetof`** : permet d'accéder à l'array via pointer arithmetic — `*(reinterpret_cast<uint8_t*>(&_wk) + descr.fieldOffset + bankIdx)`. Pas idiomatique C++ moderne mais simple et explicite. Alternative : pointer-to-member-array, plus complexe à manipuler dans une table statique.

- [ ] **Step 3.4 — Implémenter `loadWorkingCopy`**
  ```cpp
  void ToolBankConfig::loadWorkingCopy() {
    // 1. Seed avec valeurs courantes _banks et _nvs (pour le cas où NVS n'a pas encore de blob).
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      _wk.type[i]              = _banks[i].type;
      _wk.quantize[i]          = _nvs ? _nvs->getLoadedQuantizeMode(i) : DEFAULT_ARP_START_MODE;
      _wk.scaleGroup[i]        = _nvs ? _nvs->getLoadedScaleGroup(i) : 0;
      _wk.bonusPilex10[i]      = _nvs ? _nvs->getLoadedBonusPile(i) : 15;
      _wk.marginWalk[i]        = _nvs ? _nvs->getLoadedMarginWalk(i) : 7;
      _wk.proximityFactorx10[i] = _nvs ? _nvs->getLoadedProximityFactor(i) : 4;
      _wk.ecart[i]             = _nvs ? _nvs->getLoadedEcart(i) : 5;
    }
    // 2. Override depuis NVS si store valide.
    BankTypeStore bts;
    if (NvsManager::loadBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2,
                             EEPROM_MAGIC, BANKTYPE_VERSION, &bts, sizeof(bts))) {
      validateBankTypeStore(bts);
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        _wk.type[i]              = (BankType)bts.types[i];
        _wk.quantize[i]          = bts.quantize[i];
        _wk.scaleGroup[i]        = bts.scaleGroup[i];
        _wk.bonusPilex10[i]      = bts.bonusPilex10[i];
        _wk.marginWalk[i]        = bts.marginWalk[i];
        _wk.proximityFactorx10[i] = bts.proximityFactorx10[i];
        _wk.ecart[i]             = bts.ecart[i];
      }
      _nvsSaved = true;
    } else {
      _nvsSaved = false;
    }
    // 3. Snapshot pour cancel-edit.
    _saved = _wk;
  }
  ```

- [ ] **Step 3.5 — Implémenter `saveAll`**
  ```cpp
  bool ToolBankConfig::saveAll() {
    BankTypeStore bts;
    bts.magic = EEPROM_MAGIC;
    bts.version = BANKTYPE_VERSION;
    bts.reserved = 0;
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      bts.types[i] = (uint8_t)_wk.type[i];
      bts.quantize[i] = _wk.quantize[i];
      bts.scaleGroup[i] = _wk.scaleGroup[i];
      bts.bonusPilex10[i] = _wk.bonusPilex10[i];
      bts.marginWalk[i] = _wk.marginWalk[i];
      bts.proximityFactorx10[i] = _wk.proximityFactorx10[i];
      bts.ecart[i] = _wk.ecart[i];
    }
    validateBankTypeStore(bts);

    if (!NvsManager::saveBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2, &bts, sizeof(bts)))
      return false;

    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      _banks[i].type = (BankType)bts.types[i];
      if (_nvs) {
        _nvs->setLoadedQuantizeMode(i, bts.quantize[i]);
        _nvs->setLoadedScaleGroup(i, bts.scaleGroup[i]);
        _nvs->setLoadedBonusPile(i, bts.bonusPilex10[i]);
        _nvs->setLoadedMarginWalk(i, bts.marginWalk[i]);
        _nvs->setLoadedProximityFactor(i, bts.proximityFactorx10[i]);
        _nvs->setLoadedEcart(i, bts.ecart[i]);
      }
    }
    _saved = _wk;
    _nvsSaved = true;
    return true;
  }
  ```

- [ ] **Step 3.6 — Implémenter helpers `isCellApplicable`, `getCellValue`, `setCellValue`, `countBanksOfType`**
  ```cpp
  bool ToolBankConfig::isCellApplicable(uint8_t paramIdx, uint8_t bankIdx) const {
    if (paramIdx >= PARAM_TABLE_COUNT || bankIdx >= NUM_BANKS) return false;
    return PARAM_TABLE[paramIdx].isApplicable(_wk.type[bankIdx]);
  }

  uint8_t ToolBankConfig::getCellValue(uint8_t paramIdx, uint8_t bankIdx) const {
    const uint8_t* base = reinterpret_cast<const uint8_t*>(&_wk);
    return *(base + PARAM_TABLE[paramIdx].fieldOffset + bankIdx);
  }

  void ToolBankConfig::setCellValue(uint8_t paramIdx, uint8_t bankIdx, uint8_t value) {
    uint8_t* base = reinterpret_cast<uint8_t*>(&_wk);
    *(base + PARAM_TABLE[paramIdx].fieldOffset + bankIdx) = value;
  }

  uint8_t ToolBankConfig::countBanksOfType(BankType type) const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (_wk.type[i] == type) count++;
    }
    return count;
  }
  ```

- [ ] **Step 3.7 — Implémenter `drawTable`** (rendering pur, suit le mockup spec §4)
  - Frame start + header `[BANKS]`.
  - Ligne header banks `Bk1..Bk8` en bleu.
  - Pour chaque entrée de `PARAM_TABLE` :
    - Si `sectionLabel != null` : draw line separator `─ SECTION ─` (style mockup).
    - Sinon : draw param line avec label gauche + 8 cells.
    - Pour chaque cell :
      - Si non-applicable : `·` VT_DIM.
      - Si applicable : valeur formatée (label discret ou `%.1f`/`%d` range).
      - Couleur selon catégorie (cf table ci-dessous).
      - Si focus (`_cursorParam == paramIdx && _cursorBank == bankIdx`) :
        - Éditable : `[X]` brackets cyan bold autour de la valeur (couleur catégorie préservée à l'intérieur).
        - Non-éditable : `[·]` brackets VT_DIM + dim point.
  - Frame end.

  **Mapping couleurs → macros VT** (à utiliser dans `drawTable` ; les macros sont définies dans `SetupUI.h` ou `vt100-design-guide.md`) :

  | Élément | Macro VT | ANSI fallback |
  |---|---|---|
  | Tags `[BANKS]`, `[INFO]` | `VT_CYAN VT_BOLD` | `\e[1;36m` |
  | Bank headers `Bk1..Bk8` | `VT_BRIGHT_BLUE` ou ANSI 94 (à confirmer Step 3.2 si macro existe ; sinon `\e[94m`) | `\e[94m` |
  | Param labels (`Type`, `Quantize`, ...) | `VT_CYAN` | `\e[36m` |
  | Section inline `─ TYPE ─` etc. | `VT_CYAN` + VT_DIM | `\e[2;36m` |
  | Cell value `NORM` | `VT_DIM` ou warm white (à confirmer Step 3.2) | `\e[33m` (yellow dim) |
  | Cell value `ARPG` | `VT_CYAN` (Ice Blue proche) | `\e[36m` |
  | Cell value `AGEN` | `VT_MAGENTA` (Magenta acté D8 §6 spec) | `\e[35m` |
  | Cell value `LOOP` | `VT_YELLOW` ou ANSI Gold | `\e[33m` |
  | Cell value `Imm`/`Beat`/`Bar`/`Free` | Suit la couleur du type de la même colonne | — |
  | Cell value `A`/`B`/`C`/`D` | `VT_YELLOW` (Amber-proche) | `\e[33m` |
  | Cell value walk params (`1.5`, `7`, ...) | `VT_MAGENTA` (cohérence AGEN) | `\e[35m` |
  | **Cell non-éditable `·`** | `VT_DIM` | `\e[2m` |
  | Cell focus éditable brackets | `VT_CYAN VT_BOLD` | `\e[1;36m` |
  | Cell focus non-éditable brackets | `VT_DIM` | `\e[2m` |
  | INFO body | `VT_YELLOW` ou orange/jaune (style PNG) | `\e[33m` |
  | Control bar | `VT_DIM` | `\e[2m` |

  **Note implementation** : ne pas tout déballer en un long bloc — découper en helpers locaux statiques (`drawSectionSeparator(...)`, `drawCellValue(...)`) pour lisibilité. **Step 3.2 doit confirmer quelles macros VT sont disponibles dans `SetupUI.h`** (et créer celles manquantes via `#define VT_BRIGHT_BLUE "\e[94m"` si nécessaire — placer dans `SetupUI.h` au début de la zone macros).

- [ ] **Step 3.8 — Implémenter `drawInfo`** (INFO panel, 3 états)
  ```cpp
  void ToolBankConfig::drawInfo() {
    const ParamDescriptor& descr = PARAM_TABLE[_cursorParam];
    BankType bankType = _wk.type[_cursorBank];
    bool applicable = descr.isApplicable(bankType);

    _ui->drawSection("INFO");
    // Header line : "Bank N / ParamName [· non applicable]"
    char header[64];
    if (applicable) {
      snprintf(header, sizeof(header), "Bank %d / %s%s",
               _cursorBank + 1, descr.label,
               _editing ? " (editing)" : "");
    } else {
      snprintf(header, sizeof(header), "Bank %d / %s · non applicable",
               _cursorBank + 1, descr.label);
    }
    _ui->drawFrameLine(VT_BRIGHT_WHITE "%s" VT_RESET, header);

    // Description : state 1 / 2 / 3 selon applicable + editing.
    if (applicable) {
      _ui->drawFrameLine(VT_DIM "%s" VT_RESET, descr.infoDescription);
      // Cycle / range hint
      const ParamOptions& opts = descr.getOptions(bankType);
      if (opts.isDiscrete) {
        char cycle[80] = "Cycle : ";
        for (uint8_t k = 0; k < opts.discrete.count; k++) {
          strncat(cycle, opts.discrete.labels[k], sizeof(cycle) - strlen(cycle) - 1);
          if (k + 1 < opts.discrete.count) strncat(cycle, ", ", sizeof(cycle) - strlen(cycle) - 1);
        }
        strncat(cycle, ".", sizeof(cycle) - strlen(cycle) - 1);
        _ui->drawFrameLine(VT_DIM "%s" VT_RESET, cycle);
      } else {
        _ui->drawFrameLine(VT_DIM "Range : %d..%d, step %d." VT_RESET,
                           opts.range.minVal, opts.range.maxVal, opts.range.stepNormal);
      }
      if (_editing) {
        _ui->drawFrameLine(VT_DIM "^v adjust, RET save, q cancel." VT_RESET);
      }
    } else {
      // State 2 : non-applicable. Affiche la hint "comment activer".
      _ui->drawFrameLine(VT_DIM "%s" VT_RESET, descr.infoNonApplicable);
      _ui->drawFrameLine(VT_DIM "Bank %d type is %s." VT_RESET, _cursorBank + 1, TYPE_LABELS[bankType]);
    }
  }
  ```

- [ ] **Step 3.9 — Implémenter `handleNavigation`** (hors édition, ^v <>)
  ```cpp
  void ToolBankConfig::handleNavigation(const NavEvent& ev) {
    if (ev.type == NAV_UP) {
      _cursorParam = (_cursorParam == 0) ? (PARAM_TABLE_COUNT - 1) : (_cursorParam - 1);
      _screenDirty = true;
    } else if (ev.type == NAV_DOWN) {
      _cursorParam = (_cursorParam + 1) % PARAM_TABLE_COUNT;
      _screenDirty = true;
    } else if (ev.type == NAV_LEFT) {
      _cursorBank = (_cursorBank == 0) ? (NUM_BANKS - 1) : (_cursorBank - 1);
      _screenDirty = true;
    } else if (ev.type == NAV_RIGHT) {
      _cursorBank = (_cursorBank + 1) % NUM_BANKS;
      _screenDirty = true;
    } else if (ev.type == NAV_ENTER) {
      if (isCellApplicable(_cursorParam, _cursorBank)) {
        _editing = true;
        _screenDirty = true;
      }
      // Sinon : no-op silencieux (INFO state 2 déjà visible).
    } else if (ev.type == NAV_DEFAULTS) {
      applyDefaults();
      _screenDirty = true;
    }
  }
  ```

- [ ] **Step 3.10 — Implémenter `handleEdition`** (en édition, ^v / RET / q)
  - ^v ajuste la valeur du cell focus (cycle discret ou ±step range).
  - **Cas spécial Type cycle** : si `_cursorParam == 0` (Type) et que la nouvelle valeur ferait dépasser un cap (MAX_ARP_BANKS pour ARPEG/ARPEG_GEN, MAX_LOOP_BANKS pour LOOP), **skip** au type suivant. INFO en état 1 affiche `(cap atteint)`.
  - RET : save inline + sort de l'édition (`_editing = false`).
  - q : restore from `_saved` pour le cell focus uniquement (cancel local).

  ```cpp
  void ToolBankConfig::handleEdition(const NavEvent& ev) {
    const ParamDescriptor& descr = PARAM_TABLE[_cursorParam];
    BankType bankType = _wk.type[_cursorBank];
    const ParamOptions& opts = descr.getOptions(bankType);

    if (ev.type == NAV_UP || ev.type == NAV_DOWN) {
      bool up = (ev.type == NAV_UP);
      if (opts.isDiscrete) {
        uint8_t cur = getCellValue(_cursorParam, _cursorBank);
        uint8_t next = up ? (cur + 1) % opts.discrete.count
                          : (cur == 0 ? opts.discrete.count - 1 : cur - 1);
        // Cap check pour Type cycle.
        if (_cursorParam == 0) {  // Type field
          // Compter ARPEG+ARPEG_GEN combinés et LOOP, en EXCLUANT la bank courante (elle peut déjà compter dans son type actuel).
          BankType curType = _wk.type[_cursorBank];
          uint8_t arpCountExcl = 0;
          uint8_t loopCountExcl = 0;
          for (uint8_t i = 0; i < NUM_BANKS; i++) {
            if (i == _cursorBank) continue;
            if (isArpType(_wk.type[i])) arpCountExcl++;
            if (_wk.type[i] == BANK_LOOP) loopCountExcl++;
          }
          (void)curType;  // info inutile, on raisonne sur "après changement de cette bank"
          // Skip au prochain type valide (boucler tant que le cap n'autorise pas).
          uint8_t guard = 0;
          bool found = false;
          while (guard++ < (uint8_t)opts.discrete.count) {  // au plus une rotation complète
            BankType candidate = (BankType)next;
            bool capOk = true;
            // ARPEG/ARPEG_GEN partagent MAX_ARP_BANKS=4 (cohérent current cycleTypeForward + isArpType count).
            if (isArpType(candidate) && arpCountExcl >= MAX_ARP_BANKS) capOk = false;
            if (candidate == BANK_LOOP && loopCountExcl >= MAX_LOOP_BANKS) capOk = false;
            // NORMAL et BANK_ANY : pas de cap.
            if (capOk) { found = true; break; }
            next = up ? (next + 1) % opts.discrete.count
                      : (next == 0 ? opts.discrete.count - 1 : next - 1);
          }
          if (!found) {
            // Cas extrême "tous types saturés" : reste sur cur, signale via INFO state error.
            // En pratique impossible avec NUM_BANKS=8 et MAX_ARP_BANKS+MAX_LOOP_BANKS = 6 (toujours ≥ 2 NORMAL possibles).
            // Mais on garde la défense.
            next = cur;
            _errorShown = true;
            _errorTime = millis();
          }
        }
        setCellValue(_cursorParam, _cursorBank, next);
      } else {
        // Range param
        int16_t step = ev.accelerated ? opts.range.stepAccelerated : opts.range.stepNormal;
        int16_t cur = (int16_t)getCellValue(_cursorParam, _cursorBank);
        int16_t newVal = up ? cur + step : cur - step;
        if (newVal < opts.range.minVal) newVal = opts.range.minVal;
        if (newVal > opts.range.maxVal) newVal = opts.range.maxVal;
        setCellValue(_cursorParam, _cursorBank, (uint8_t)newVal);
      }
      _screenDirty = true;
    } else if (ev.type == NAV_ENTER) {
      if (saveAll()) {
        _ui->flashSaved();
        _editing = false;
        _screenDirty = true;
      }
    } else if (ev.type == NAV_QUIT) {
      // Cancel : restore cell from _saved.
      uint8_t savedVal = *(reinterpret_cast<const uint8_t*>(&_saved) + descr.fieldOffset + _cursorBank);
      setCellValue(_cursorParam, _cursorBank, savedVal);
      _editing = false;
      _screenDirty = true;
    }
  }
  ```

- [ ] **Step 3.11 — Implémenter `applyDefaults`** (reset cell focus)
  ```cpp
  void ToolBankConfig::applyDefaults() {
    // Default values per param.
    static const uint8_t DEFAULTS[] = {
      BANK_NORMAL,             // Type
      DEFAULT_ARP_START_MODE,  // Quantize (Imm)
      0,                       // Group : -
      15,                      // Bonus : 1.5
      7,                       // Margin
      4,                       // Prox : 0.4
      5,                       // Ecart
    };
    // Garde : sentinel si l'arr a la mauvaise taille.
    static_assert(sizeof(DEFAULTS) / sizeof(DEFAULTS[0]) == 7,
                  "DEFAULTS size must match PARAM_TABLE count (excluding LOOP-specific)");
    if (_cursorParam < sizeof(DEFAULTS) / sizeof(DEFAULTS[0])) {
      setCellValue(_cursorParam, _cursorBank, DEFAULTS[_cursorParam]);
      _screenDirty = true;
    }
  }
  ```

- [ ] **Step 3.12 — Implémenter `run()` event loop**
  ```cpp
  void ToolBankConfig::run() {
    if (!_ui || !_banks) return;
    loadWorkingCopy();
    _cursorParam = 0;
    _cursorBank = 0;
    _editing = false;
    _screenDirty = true;
    _errorShown = false;

    Serial.print(ITERM_RESIZE);
    _ui->vtClear();

    InputParser input;
    while (true) {
      if (_leds) _leds->update();
      NavEvent ev = input.update();

      if (_errorShown && (millis() - _errorTime) > 2000) {
        _errorShown = false;
        _screenDirty = true;
      }

      if (ev.type == NAV_QUIT && !_editing) {
        _ui->vtClear();
        return;
      }

      if (!_editing) {
        handleNavigation(ev);
      } else {
        handleEdition(ev);
      }

      if (_screenDirty) {
        _screenDirty = false;
        _ui->vtFrameStart();
        // Header
        char headerRight[32];
        uint8_t arpCount = countBanksOfType(BANK_ARPEG) + countBanksOfType(BANK_ARPEG_GEN);
        uint8_t loopCount = countBanksOfType(BANK_LOOP);
        uint8_t normalCount = NUM_BANKS - arpCount - loopCount;
        snprintf(headerRight, sizeof(headerRight),
                 "TOOL 5: BANK CONFIG  %dA/%dL/%dN", arpCount, loopCount, normalCount);
        _ui->drawConsoleHeader(headerRight, _nvsSaved);
        _ui->drawFrameEmpty();

        drawTable();
        _ui->drawFrameEmpty();
        drawInfo();
        _ui->drawFrameEmpty();

        // Control bar
        if (_editing) {
          _ui->drawControlBar(VT_DIM "[^v] VALUE" CBAR_SEP "[RET] SAVE" CBAR_SEP "[q] CANCEL" VT_RESET);
        } else {
          _ui->drawControlBar(VT_DIM "[^v<>] NAV" CBAR_SEP "[RET] EDIT  [d] DFLT" CBAR_SEP "[q] EXIT" VT_RESET);
        }
        _ui->vtFrameEnd();
      }

      delay(5);
    }
  }
  ```

- [ ] **Step 3.13 — Compile gate**
  - `pio run` → exit 0, no warning new (`-Wswitch`, `-Wunused-variable`, etc.).
  - Vérifier RAM / Flash : la table déclarative + helpers ajoutent ~200 octets flash (function pointers), pas de SRAM perm. Acceptable.

- [ ] **Step 3.14 — Static read-back**
  - Grep `saveConfig` dans `src/setup/` → 0 résultat (ancienne signature retirée).
  - Grep `drawDescription` → 0 résultat.
  - Grep `cursorSubField` → 0 résultat (ancien state retiré).
  - Grep `PARAM_TABLE` dans `src/setup/ToolBankConfig.cpp` → présent.
  - Grep `loadWorkingCopy`, `drawTable`, `drawInfo`, `handleNavigation`, `handleEdition`, `applyDefaults`, `saveAll` → présents dans `.cpp`.

- [ ] **Step 3.15 — HW Checkpoint A (bloquant)**
  - Présenter à l'utilisateur les points à valider HW (upload firmware autorisé par user explicite, pas auto) :
    - **Rendering** :
      - Entrée setup mode → Tool 5 → tableau matriciel s'affiche.
      - 8 bank headers `Bk1..Bk8` colorés bleu.
      - Sections inline `─ TYPE ─` `─ SCALE ─` `─ WALK ─` visibles.
      - Cells non-applicables affichées `·` VT_DIM.
      - Cell focus initial (Bk1, Type) en `[NORM]` cyan bold.
    - **Nav 2D** :
      - `<>` change bank focus horizontalement avec wrap.
      - `^v` change param focus verticalement avec wrap.
      - INFO panel se met à jour à chaque mouvement.
      - Cells non-éditables focus = `[·]` dim (pas cyan bold).
    - **INFO panel** :
      - State 1 (éditable) : description + cycle/range.
      - State 2 (non-éditable) : `· non applicable` + hint type.
      - State 3 (en édition) : description + cycle + `^v adjust, RET save, q cancel`.
    - **Édition Type** :
      - RET sur cell Type → édition active.
      - `^v` cycle 4 valeurs : NORMAL → ARPEG → ARPEG_GEN → LOOP → NORMAL.
      - Cap ARPEG : si 4 banks déjà ARPEG/ARPEG_GEN → skip au type suivant non-cap.
      - Cap LOOP : si 2 banks déjà LOOP → skip au type suivant non-cap.
      - RET save → flashSaved + retour mode nav.
      - q cancel → restore type pre-édit.
    - **Édition Quantize** :
      - Cell Quantize sur bank ARPEG : cycle 2 valeurs Imm/Beat.
      - Cell Quantize sur bank ARPEG_GEN : cycle 2 valeurs Imm/Beat.
      - Cell Quantize sur bank LOOP : cycle 3 valeurs Free/Beat/Bar.
      - Cell Quantize sur bank NORMAL : non-applicable (`·` focus dim, RET no-op).
    - **Édition Group** : cycle 5 valeurs -/A/B/C/D.
    - **Édition Bonus/Margin/Prox/Ecart** sur bank ARPEG_GEN : ±1 normal, accéléré ±5/3 selon range. Clamp aux bornes.
    - **`d` Defaults** : reset cell focus à default sans confirmation modal.
    - **Persistance NVS** :
      - Sauver (RET) → flashSaved.
      - Quitter Tool 5 (q) → re-entrer Tool 5 → valeurs persistées.
      - Reboot → valeurs persistées (compile + flash + boot).
    - **Non-régression ARPEG_GEN runtime** :
      - Bank ARPEG_GEN joue (pile add → walk + bonus_pile audible).
      - Tool 5 modifie Bonus/Margin/Prox/Ecart → effet audible immédiat post-save.
    - **Non-régression NORMAL / ARPEG** :
      - Bank NORMAL joue notes pad correctement.
      - Bank ARPEG (Imm + Beat) joue arp correctement.
      - Scale group propagation OK.
  - **Attendre OK explicite utilisateur** avant commit gate.

- [ ] **Step 3.16 — Commit (autocommit, après HW OK)**
  - Files : `src/setup/ToolBankConfig.cpp`.
  - Message proposé :
    ```
    refactor(tool5-refacto): step 3 — body rewrite (tableau matriciel, nav 2D, INFO contextuel)

    Rewrite intégral ToolBankConfig.cpp. Remplace le current 712-line
    monolithique par une architecture modulaire :
    - PARAM_TABLE déclarative : 7 entries (Type/Quantize/Group/Bonus/
      Margin/Prox/Ecart) avec isApplicable + getOptions dynamiques.
    - loadWorkingCopy + saveAll : working-copy struct unifiée Tool5Working.
    - drawTable + drawInfo : rendering pur, sections inline `─ TYPE ─`
      `─ SCALE ─` `─ WALK ─`, couleurs catégorie (NORM warm white, ARPG
      Ice Blue, AGEN magenta, LOOP gold, Group amber).
    - handleNavigation + handleEdition : nav 2D (^v param vertical, <>
      bank horizontal, wrap), édition par cell avec cap MAX_ARP_BANKS /
      MAX_LOOP_BANKS skip cycle.
    - applyDefaults : reset cell focus uniquement (pas tableau entier).
    - run : event loop court (event dispatch + render si dirty).

    Cells non-applicables : `·` VT_DIM, focus = `[·]` dim. Cells
    éditables : valeur couleur, focus = `[X]` cyan bold.

    INFO panel auto-update sur chaque mouvement curseur : 3 états
    (éditable / non-éditable + hint type / en édition + cycle hint).

    Compile redevient vert (Task 2 header + Task 3 body alignés).

    Refs : spec Tool 5 refacto §4-§14 (UX), §11 (architecture).
    HW Checkpoint A validé (rendering + nav + édition + persistance NVS
    + non-régression ARPEG_GEN/NORMAL/ARPEG).
    ```

---

## Task 4 — Retrait NvsManager helpers caducs (conditionnel)

**Cross-refs :** Spec Tool 5 refacto §16 (NvsManager refacto).

**Files :**
- Modify (conditionnel) : [`src/managers/NvsManager.h`](../../../src/managers/NvsManager.h) — retrait paires `Loaded*` Tool-5-only.
- Modify (conditionnel) : [`src/managers/NvsManager.cpp`](../../../src/managers/NvsManager.cpp) — retrait bodies.

**Steps :**

- [ ] **Step 4.1 — Grep callsites helpers Tool-5-only**
  - Pour chaque paire concernée :
    - `grep -rn "getLoadedBonusPile\|setLoadedBonusPile" src/` → liste callsites.
    - `grep -rn "getLoadedMarginWalk\|setLoadedMarginWalk" src/` → idem.
    - `grep -rn "getLoadedProximityFactor\|setLoadedProximityFactor" src/` → idem.
    - `grep -rn "getLoadedEcart\|setLoadedEcart" src/` → idem.
  - **Décision conditionnelle** :
    - Si **0 callsite hors ToolBankConfig.cpp** : procéder au retrait.
    - Si **≥1 callsite hors ToolBankConfig.cpp** (ex : PotRouter consomme `getLoadedBonusPile` pour ARPEG_GEN walk) : **garder les helpers**. Spec §16 anticipe ce cas. Documenter en commit body.

- [ ] **Step 4.2 — Lecture `NvsManager.h:42-58`** + `NvsManager.cpp` bodies des paires concernées
  - Identifier les déclarations exactes et les implémentations.

- [ ] **Step 4.3 — Édition `NvsManager.h` (si Step 4.1 = procéder au retrait)**
  - Retirer les déclarations des paires `getLoaded*`/`setLoaded*` confirmées sans consommateur externe.
  - **Garder** `getLoadedQuantizeMode`/`setLoadedQuantizeMode` et `getLoadedScaleGroup`/`setLoadedScaleGroup` — ils sont consommés runtime ailleurs (ArpEngine, etc.).

- [ ] **Step 4.4 — Édition `NvsManager.cpp` (si retrait)**
  - Retirer les bodies correspondants.
  - Retirer les arrays mirror privés (`_loadedBonusPilex10`, `_loadedMarginWalk`, `_loadedProximityFactorx10`, `_loadedEcart`) si plus consommés runtime.
  - Adapter `NvsManager::loadAll` qui les initialise au boot — retirer les memset associés.

- [ ] **Step 4.5 — Édition `ToolBankConfig.cpp` (si retrait)**
  - Adapter `loadWorkingCopy` (Step 3.4) : lire les params ARPEG_GEN directement depuis `BankTypeStore` chargé via `loadBlob` (déjà fait dans Step 3.4 — la branche `if (loadBlob)` ne dépend pas des helpers). Le seed initial avec `_nvs->getLoadedBonusPile(i)` etc. doit être remplacé par des defaults compile-time (15, 7, 4, 5).
  - Adapter `saveAll` : retirer les appels `_nvs->setLoadedBonusPile`, etc.

- [ ] **Step 4.6 — Compile gate**
  - `pio run` → exit 0, no new warning.

- [ ] **Step 4.7 — Static read-back**
  - Grep des symboles retirés → 0 résultat.

- [ ] **Step 4.8 — Commit (autocommit)**
  - Files : `src/managers/NvsManager.h`, `src/managers/NvsManager.cpp`, `src/setup/ToolBankConfig.cpp` (si adapté).
  - Message proposé (cas retrait) :
    ```
    refactor(nvs-manager): step 4 — retrait helpers Tool 5 caducs

    Retrait des paires getLoaded*/setLoaded* Loaded* Tool-5-only confirmées
    sans consommateur runtime hors ToolBankConfig :
    - getLoadedBonusPile / setLoadedBonusPile
    - getLoadedMarginWalk / setLoadedMarginWalk
    - getLoadedProximityFactor / setLoadedProximityFactor
    - getLoadedEcart / setLoadedEcart

    Tool 5 (refacto T3) lit/écrit BankTypeStore directement via loadBlob/
    saveBlob. Defaults compile-time appliqués au seed initial (15, 7, 4,
    5 pour bonusPilex10, marginWalk, proximityFactorx10, ecart).

    Conservé : getLoadedQuantizeMode/setLoadedQuantizeMode (consommé runtime
    ailleurs), getLoadedScaleGroup/setLoadedScaleGroup (idem).

    Refs : spec Tool 5 refacto §16.
    ```
  - Message proposé (cas no-op si callsite trouvé) :
    ```
    chore(tool5-refacto): step 4 — NvsManager helpers Loaded* conservés (callsites runtime trouvés)

    Grep des paires getLoadedBonusPile, getLoadedMarginWalk, etc. a révélé
    des callsites runtime hors ToolBankConfig : [liste]. Les helpers
    restent en place. Refacto Tool 5 (T3) utilise BankTypeStore directement
    pour ses propres besoins mais ne casse pas les consommateurs runtime.

    Refs : spec Tool 5 refacto §16 (cas conditionnel anticipé).
    ```
  - Si **aucun changement** (callsites trouvés partout) : **skipper le commit**. Documenter en exécution.

---

## Task 5 — HW non-régression complète + doc sync

**Cross-refs :** Spec Tool 5 refacto §17 (hors scope : pas de modif runtime).

**Files :**
- Modify : [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) — §4 Table 1 ligne `BankTypeStore`, §8 ligne Tool 5.
- Modify (optionnel) : [`docs/reference/setup-tools-conventions.md`](../../reference/setup-tools-conventions.md) — note paradigme tableau matriciel.

**Steps :**

- [ ] **Step 5.1 — HW Checkpoint B (bloquant) — non-régression complète**
  - Présenter à l'utilisateur les points à valider HW (le firmware est déjà flashé post Step 3.15, mais on revalide après Task 4) :
    - **NORMAL** : 4 banks NORMAL en jeu (notes pad), AT polyphonique, pitch bend per-bank via pot.
    - **ARPEG classique** : Hold pad → arp tourne, tick FLASH vert, scale change root/mode/chrom → confirm BLINK_FAST.
    - **ARPEG_GEN** : pile add → walk + bonus_pile audible, mutation pad oct, Tool 5 modifie Bonus/Margin/Prox/Ecart → effet audible immédiat post-save.
    - **Bank switch** : NORMAL ↔ ARPEG ↔ ARPEG_GEN propre, pots reseed catch.
    - **Setup mode entry** : Tool 5 nouveau s'ouvre, tableau matriciel rendu correctement.
    - **Persistance reboot** : flash → boot → Tool 5 affiche les valeurs sauvegardées (pas de reset user attendu).
    - **NVS badges** : Tool 5 affiche `NVS:OK` après save. Premier boot après flash refacto : pas de "réinitialisé" warning sur `BankTypeStore` (v4 inchangé).
  - **Attendre OK explicite utilisateur**.

- [ ] **Step 5.2 — Mise à jour `architecture-briefing.md`**
  - §4 Table 1 ligne `BankTypeStore` : ajouter note finale `Tool 5 refacto livré 2026-05-17 (tableau matriciel banks×params, nav 2D, validator quantize contextuel pour LOOP). Pas de bump NVS.`
  - §8 Domain entry points ligne `Setup tools` ou similaire : ajouter note `Tool 5 : refacto tableau matriciel, intègre LOOP côté UI pré-Phase 2 runtime`.

- [ ] **Step 5.3 — Mise à jour `setup-tools-conventions.md` (optionnel)**
  - Ajouter une section ou note : `Paradigme tableau matriciel banks×params (introduit Tool 5 2026-05-17). Référence pour les futurs Tools multi-banks. Cf spec docs/superpowers/specs/2026-05-17-tool5-bank-config-refactor-design.md.`

- [ ] **Step 5.4 — Mise à jour STATUS.md**
  - Section "Focus courant" : retirer la mention "Refacto Tool 5 à rédiger", remplacer par "Phase 2 LOOP à rédiger from scratch (Refacto Tool 5 livré 2026-05-17, prêt pour LOOP)".
  - Ajouter section historique commits "Refacto Tool 5 — historique commits" avec les commits de Tasks 1-5.

- [ ] **Step 5.5 — Commit doc sync (autocommit)**
  - Files : `docs/reference/architecture-briefing.md`, `docs/reference/setup-tools-conventions.md` (si modifié), `STATUS.md`.
  - Message proposé :
    ```
    docs: tool 5 refacto livré — doc sync + status update

    Tool 5 refacto complet livré sur main (commits Tasks 1-4) :
    - HardwareConfig MAX_LOOP_BANKS = 2 (placeholder pré-Phase 2 LOOP)
    - KeyboardData validateBankTypeStore quantize contextuel
    - ToolBankConfig refacto complet (tableau matriciel banks×params,
      nav 2D, INFO auto-update, PARAM_TABLE déclarative)
    - NvsManager helpers caducs retirés (conditionnel)

    HW Checkpoint B validé : non-régression complète NORMAL/ARPEG/
    ARPEG_GEN runtime + persistance NVS reboot OK.

    Briefing §4 Table 1 + §8 Tool 5 : note refacto livré 2026-05-17.
    setup-tools-conventions : référence au paradigme tableau matriciel
    pour futurs Tools multi-banks (Tool 7 LOOP page Phase 4 etc.).

    STATUS.md : focus courant maintenant "Phase 2 LOOP à rédiger from
    scratch", refacto Tool 5 prérequis livré.

    Refs : spec Tool 5 refacto §17 (pas de modif runtime), §20 (suite).
    ```

---

## §4 — Pré-merge checklist (post Task 5)

À cocher avant déclaration "Refacto Tool 5 complete, Phase 2 LOOP peut démarrer".

### Build & static

- [ ] `pio run -e esp32-s3-devkitc-1` exit 0, no new warnings.
- [ ] Grep des symboles obsolètes :
  - `grep -rn "saveConfig" src/setup/` → 0 résultat dans ToolBankConfig (ancien retiré).
  - `grep -rn "drawDescription" src/setup/ToolBankConfig.cpp` → 0 résultat.
  - `grep -rn "cursorSubField\|SubField" src/setup/ToolBankConfig.cpp` → 0 résultat.
- [ ] Grep des nouveaux symboles :
  - `grep -rn "Tool5Working\|ParamDescriptor\|PARAM_TABLE" src/setup/` → présents.
  - `grep -n "MAX_LOOP_BANKS" src/core/HardwareConfig.h src/setup/ToolBankConfig.cpp` → présents.

### NVS

- [ ] `BankTypeStore` reste v4 (`BANKTYPE_VERSION = 4`). Pas de bump.
- [ ] Premier boot post-flash : pas de warning "BankTypeStore réinitialisé" inattendu (valider HW Step 5.1).
- [ ] Validator `validateBankTypeStore` accepte les anciennes données NVS (clamp ARPEG/ARPEG_GEN quantize à 0..1 — comportement original préservé).

### Runtime regression (à valider HW Step 5.1)

- [ ] NORMAL bank : notes pad + AT + pitch bend OK.
- [ ] ARPEG bank : Hold pad → arp, tick FLASH vert OK.
- [ ] ARPEG_GEN bank : pile add → walk audible, mutation pad oct, paramètres Bonus/Margin/Prox/Ecart effectifs.
- [ ] Bank switch propre.
- [ ] Tool 5 entry + tableau matriciel rendu OK.
- [ ] Persistance reboot OK.

### Documentation sync

- [ ] [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) §4 Table 1 + §8 à jour.
- [ ] STATUS.md focus courant + historique commits à jour.
- [ ] Spec Tool 5 refacto cohérente avec ce qui a été livré (pas de TBD résiduel).

### Hors scope explicite (pour information — Phase 2+)

- [ ] LoopEngine (Phase 2 LOOP).
- [ ] Tool 3 b1 refactor (Phase 3 LOOP).
- [ ] Tool 4 ext (Phase 3 LOOP).
- [ ] PotMappingStore extension 3 contextes (Phase 4 LOOP).
- [ ] LOOP-specific defaults dans Tool 5 (Phase 5 LOOP — section "LOOP defaults" à ajouter à PARAM_TABLE le moment venu).
- [ ] Slot Drive LittleFS (Phase 6 LOOP).

---

## §5 — Notes de risques et zones de friction

| Zone | Risque | Mitigation |
|---|---|---|
| Task 2 compile rouge temporaire | Bisect cassé entre T2 et T3 commits | Documenté en Step 2.4 + 2.6 (commit message explicite "WIP, body suit T3"). Acceptable car refacto cohérent T2+T3. |
| `offsetof` sur `Tool5Working` membres array | Comportement UB sur certaines plateformes si non-POD | `Tool5Working` est POD (uint8_t arrays + BankType enum), `offsetof` OK. Compile gate confirme. |
| Couleurs ANSI exactes pour AGEN Magenta | Pas de slot runtime dédié, à acter VT-only | Spec §6 D8 acté. Utiliser `VT_MAGENTA` ou ANSI 35. Confirmer rendu HW Step 3.15. |
| Skip cap Type cycle infini | Si TOUS les types sont au cap (4 ARPEG + 2 LOOP + ?), boucle infinie | `while (guard++ < 8)` anti-loop dans `handleEdition` Step 3.10. En pratique impossible avec NUM_BANKS=8 et caps 4+2+2 NORMAL. Si guard expire : laisser le cur (no-op visible). |
| Helpers NvsManager retirés cassent un consommateur externe | Compile rouge après Task 4 si grep Step 4.1 a manqué un callsite | Strict grep Step 4.1 + compile gate Step 4.6 = filet. Si rouge : revert Step 4.3-4.5, repasser en mode "conservation helpers". |
| ARPEG_GEN walk runtime cassé par retrait helpers | PotRouter ou ArpEngine consomme un helper retiré | Step 4.1 grep doit lister explicitement les callsites avant décision retrait. HW Checkpoint B Step 5.1 confirme non-régression audio. |
| HW gate non bloquant | Si l'agent skip le HW gate par erreur, commit sans validation | CLAUDE.md projet règle "HW gate AVANT commit gate" — l'agent doit présenter et attendre OK. Si pas d'autorisation upload : compile + read-back suffit comme proxy, mais HW reste à valider avant Phase 2 LOOP. |

---

**Plan validé pour exécution.** 5 tasks, 2 HW Checkpoints bloquants (A après T3, B après T5). Compile-rouge temporaire accepté entre T2 et T3 (refacto cohérent). Pas de bump NVS. À l'issue de Task 5, refacto Tool 5 complet livré sur main, Phase 2 LOOP peut démarrer.
