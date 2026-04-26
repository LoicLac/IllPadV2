# LOOP Phase 1 — Skeleton + Guards + LED Preparation — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **⚠️ PRE-LECTURE OBLIGATOIRE — ARPEG_GEN merged first** : ce plan a été rédigé le 2026-04-21, **avant** le plan ARPEG_GEN ([`2026-04-26-arpeg-gen-plan.md`](2026-04-26-arpeg-gen-plan.md)). Hypothèse mise à jour le 2026-04-26 : ARPEG_GEN s'implémente **avant** LOOP Phase 1. Plusieurs Steps deviennent partiellement redondants car ARPEG_GEN traite déjà les sites de cohabitation (BankType enum, validator, switches 4-way, LED dispatch, debug labels) avec des stubs `case BANK_LOOP: break;` ou commentaires `// LOOP : Phase 1`. Avant d'exécuter ce plan, **lire l'annexe « §Annexe — Patches recommandés pour le plan LOOP Phase 1 »** en fin de [`2026-04-26-arpeg-gen-plan.md`](2026-04-26-arpeg-gen-plan.md) pour la liste exhaustive des Steps à skip / amender / garder. Liste résumée :
> - **Skip** : Steps 1.2 enum + validator clamp ; Step 1.3 BankManager debug print ; Step 1.5 ToolBankConfig labels.
> - **Amender** (réduire à un trivial 1-line change) : Step 1.3 main.cpp handlePadInput (remplacer `default: break;` par `case BANK_LOOP:`) ; Step 1.6 LedController switch render (remplacer le `break;` du `case BANK_LOOP` stub par appel `renderBankLoop`).
> - **Garder intact** : Step 1.6 declare `renderBankLoop` ; Tasks 2, 3 (LoopPadStore, LoopPotStore) ; Task 4.2 BankManager double-tap LOOP consume (la branche `else if (BANK_LOOP)` reste à câbler) ; Task 4.3 ScaleManager early-return ; Task 5 (rename `fgArpPlayMax`) ; Task 6 (Tool 8 line move) ; Task 7 (EVT_WAITING).
>
> Si LOOP Phase 1 est exécuté **avant** ARPEG_GEN (cas de figure rejeté par §0 D1 du plan ARPEG_GEN — `BANK_ARPEG_GEN = 3` suppose `BANK_LOOP = 2` réservé), il faut bumper la valeur d'enum ARPEG_GEN. Mais ce scénario n'est pas planifié.

**Goal :** Poser le squelette type-system + NVS + LED prep du mode LOOP **sans aucun comportement musicien-visible**. À l'issue de cette phase, le firmware compile, les tests HW existants (NORMAL + ARPEG) passent inchangés, l'enum `BankType` admet `BANK_LOOP`, deux nouveaux Stores LOOP sont déclarés (layout NVS figé) et le `LedController` est prêt à recevoir les events `EVT_WAITING` mode-invariant câblés en Phase 2+.

**Architecture :** 7 commits courts, dépendances strictes encodées dans l'ordre. Aucune feature musicien-facing. Toutes les modifications sont défensives (guards) ou préparatoires (LED + Stores dormants). Pas de bump NVS sur les Stores existants — uniquement 2 ajouts au descriptor table.

**Tech Stack :** C++17 / PlatformIO / ESP32-S3 / FreeRTOS dual-core. Zero Migration Policy (CLAUDE.md). Pas de unit tests — vérification = `pio run` (compile) + flash manuel (upload sur autorisation explicite, pas dans ce plan) + read-back du diff.

**Sources de référence :**
- Spec LOOP (V) : [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](../specs/2026-04-19-loop-mode-design.md) §20, §23, §27, §28
- LED spec : [`docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md`](../specs/2026-04-19-led-feedback-unified-design.md) §10, §11, §17
- Audit : [`docs/superpowers/reports/rapport_audit_loop_spec.md`](../reports/rapport_audit_loop_spec.md) F2.2 / F2.3 / F2.4 résolus
- Briefing : [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) §0, §3 task index, §4 Setup↔Runtime, §8 Domain entry points
- NVS : [`docs/reference/nvs-reference.md`](../../reference/nvs-reference.md) — pattern P6 + section "How To Add a New Namespace"
- Tool conventions : [`docs/reference/setup-tools-conventions.md`](../../reference/setup-tools-conventions.md) §1 save policy
- CLAUDE.md projet — section "NVS Zero Migration Policy" + "Performance Budget — Budget Philosophy"
- CLAUDE.md user — git workflow + actions destructives + lire-avant-de-proposer

---

## §0 — Décisions pré-actées (références §28 spec LOOP)

Ces 8 décisions encadrent le plan. **Ne pas les rebattre.**

| # | Décision | Impact Phase 1 |
|---|---|---|
| Q1 | `LoopPadStore` = **23 B strict packed** (3 controls + 16 slots × uint8) | Task 3 |
| Q2 | `PendingEvent` dupliqué (LoopEngine vs ArpEngine) — pas de factorisation | Phase 2 (hors scope) |
| Q3 | `EVT_WAITING` unique mode-invariant, colorB blanc hardcodé, brightness `_fgArpStopMax × bgFactor` BG | Task 7 |
| Q4 | Rename `fgArpPlayMax` → `fgPlayMax`, ligne Tool 8 déplacée en TRANSPORT (shared ARPEG+LOOP) | Tasks 5 + 6 |
| Q5 | STOPPED-loaded + tap REC = PLAYING + OVERDUBBING simultanés | Phase 2 (hors scope) |
| Q6 | Tool 5 refactor "colonnes" deferred post-Phase 6 | Phase 3 (hors scope) |
| Q7 | Tool 4 extension bundle Phase 3 avec Tool 3 b1 | Phase 3 (hors scope) |
| Q8 | Invariant 11 §23 acté (max 1 bank REC/OD simultané) | Phase 2 (consigne pour LoopEngine) |

---

## §1 — File structure overview

Fichiers touchés, par responsabilité. Aucun nouveau fichier créé en Phase 1 — toutes les additions vivent dans des fichiers existants.

| Fichier | Tasks | Rôle |
|---|---|---|
| [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h) | 1, 3, 4, 5 | Enum `BankType`, Store structs (`LoopPadStore`, `LoopPotStore`), validators, descriptor table, rename de field |
| [`src/managers/NvsManager.cpp`](../../../src/managers/NvsManager.cpp) | 1 | Default seed loop (clamping pour BANK_LOOP en validator) |
| [`src/managers/BankManager.cpp`](../../../src/managers/BankManager.cpp) | 4 | Guards `switchToBank` + double-tap LOOP no-op (Phase 2 wires PLAY/STOP) + label debug |
| [`src/managers/ScaleManager.cpp`](../../../src/managers/ScaleManager.cpp) | 4 | Guard `processScalePads` early-return si LOOP |
| [`src/main.cpp`](../../../src/main.cpp) | 1, 4 | Switch `slot.type` (default case BANK_LOOP), `handleHoldPad` (déjà OK), debug print |
| [`src/core/LedController.h`](../../../src/core/LedController.h) | 5 | Rename membre `_fgArpPlayMax` → `_fgPlayMax` |
| [`src/core/LedController.cpp`](../../../src/core/LedController.cpp) | 4, 5, 7 | Stub `renderBankLoop` (dispatch default), rename callsites, WAITING colorB + BG-aware |
| [`src/core/LedGrammar.cpp`](../../../src/core/LedGrammar.cpp) | 7 | EVT_WAITING default colorSlot mis à jour (placeholder ARPEG → VERB_PLAY) |
| [`src/setup/ToolLedSettings.h`](../../../src/setup/ToolLedSettings.h) | 6 | Enum `LineId` : retrait `LINE_ARPEG_FG_PCT` + `LINE_LOOP_FG_PCT`, ajout `LINE_TRANSPORT_FG_PCT` |
| [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp) | 5, 6 | Rename `_lwk.fgArpPlayMax` → `_lwk.fgPlayMax`, refactor section/line tables, dispatcher `previewContextForLine` |
| [`src/setup/ToolBankConfig.cpp`](../../../src/setup/ToolBankConfig.cpp) | 1 | Aucun changement de cycle UI (Phase 3 ajoute le 3-way), seul un commentaire optionnel + le validator-en-amont protège déjà |
| [`docs/reference/nvs-reference.md`](../../reference/nvs-reference.md) | 3, 4 | Catalogue des nouveaux Stores LoopPadStore + LoopPotStore (size 23B + 8B) |
| [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) | 1, 4, 7 | §4 table 1 (BankType, LedSettings rename), §8 (note Phase 1 LOOP scaffolding) |

**Pas touché en Phase 1 :** `CapacitiveKeyboard.*`, `ArpEngine.*`, `ArpScheduler.*`, `MidiTransport.cpp`, `MidiEngine.cpp`, `PotRouter.*`, `ControlPadManager.*`, Tools 1/2/3/4/6/7. La spec mentionne "MidiTransport AT/PB guards" mais l'analyse code montre que tous les chemins AT/PB sont déjà gardés en amont (dispatch sur `slot.type` dans `processNormalMode` et `processArpMode`, donc une bank LOOP ne déclenche jamais ces appels). Pas de modif transport requise — Task 4 vérifie ce point.

---

## §2 — Dépendances inter-tasks (graph)

```
Task 1 (BankType enum + cascade)
   │
   ├──► Task 4 (guards : exigent BANK_LOOP existant)
   │
   └──► Task 2 (LoopPadStore : indépendant runtime mais cohérent après l'enum)
            │
            └──► Task 3 (LoopPotStore : indépendant)
                     │
                     └──► Task 5 (rename fgArpPlayMax → fgPlayMax)
                              │
                              └──► Task 6 (Tool 8 line move : exige rename Task 5)
                                       │
                                       └──► Task 7 (WAITING BG-aware : touche les mêmes fichiers LED)
```

**Ordre des commits = ordre des tasks** (1 → 7). Chaque task est atomique en compile + lint + read-back. Aucune task n'introduit une régression observable musicien-facing.

---

## §3 — Conventions de vérification (firmware-spécifique)

Pas de framework de tests automatisés. Pour chaque task :

1. **Compile gate** : `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1` → exit 0, no warning new.
2. **Static read-back** : grep des symboles modifiés dans tous les fichiers consommateurs ; vérifier qu'aucun callsite n'a été oublié.
3. **HW gate (regression check)** — déclenché manuellement par le user, **jamais automatiquement** (cf CLAUDE.md projet "no auto-upload" + `feedback_no_auto_upload`) :
   - Boot OK, 8 progressive LEDs.
   - Bank switch (NORMAL ↔ ARPEG) inchangé visuellement et MIDI.
   - Scale change root/mode/chrom : confirm BLINK_FAST inchangé.
   - ARPEG play (Hold pad) + tick FLASH vert : inchangé.
   - Battery gauge / setup mode entry : inchangés.
   - **Pas de comportement nouveau attendu en Phase 1.** Tout test "WAITING visible" est reporté à Phase 2 (LoopEngine émetteur).
4. **Commit gate** : commit uniquement après autorisation explicite du user ; pas de `git add -A` ni de `--no-verify` (cf CLAUDE.md user "Git workflow").

---

## Task 1 — Extension `BankType` enum + cascade défensive

**Cross-refs :** spec LOOP §27 Phase 1 item 1 ; §28 Q8 (invariant 11) ; audit §4.3 item 2.1 ; briefing §8 (BankManager entry point) + §4 Table 1 ligne BankTypeStore.

**Files :**
- Modify : [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h:315-319) — enum `BankType`, validator `validateBankTypeStore`
- Modify : [`src/managers/NvsManager.cpp`](../../../src/managers/NvsManager.cpp:609-624) — default seed loop (commentaire + clarification clamp)
- Modify : [`src/main.cpp`](../../../src/main.cpp:579-587) — `handlePadInput` switch ajoute `default:` no-op (commenté "Phase 2 wires processLoopMode")
- Modify : [`src/main.cpp`](../../../src/main.cpp:204-208) — debug print BankManager `switchToBank` ajoute label "LOOP"
- Modify : [`src/setup/ToolBankConfig.cpp`](../../../src/setup/ToolBankConfig.cpp:362) — `drawDescription` accepte LOOP (display "NORMAL" pour Phase 1, refactor 3-way en Phase 3)

**Cible enum (KeyboardData.h ligne 315) :**

```
enum BankType : uint8_t {
  BANK_NORMAL = 0,
  BANK_ARPEG  = 1,
  BANK_LOOP   = 2,
  BANK_ANY    = 0xFF
};
```

**Cible validator (KeyboardData.h ligne 613) — clamp upper bound + arpCount inchangé (BANK_LOOP ne compte pas dans la limite ARPEG, limites LOOP arrivent Phase 3 Tool 5) :**

```
inline void validateBankTypeStore(BankTypeStore& s) {
  uint8_t arpCount = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (s.types[i] > BANK_LOOP) s.types[i] = BANK_NORMAL;   // <-- bound update
    if (s.types[i] == BANK_ARPEG) arpCount++;
    if (arpCount > MAX_ARP_BANKS) s.types[i] = BANK_NORMAL;
    if (s.quantize[i] >= NUM_ARP_START_MODES) s.quantize[i] = DEFAULT_ARP_START_MODE;
    if (s.scaleGroup[i] > NUM_SCALE_GROUPS) s.scaleGroup[i] = 0;
  }
}
```

**NVS bump :** non. Layout `BankTypeStore` inchangé. La valeur `BANK_LOOP = 2` rentrait déjà dans `uint8_t types[NUM_BANKS]`. Aucun reset user.

**Steps :**

- [ ] **Step 1.1 — Lecture des 5 fichiers cibles avant édition** (cf CLAUDE.md user "Lire avant de proposer")
  Confirmer signatures : `BankType`, `validateBankTypeStore`, `NvsManager::loadAll` lignes 605-628, `handlePadInput` ligne 579, `BankManager::switchToBank` ligne 204. Aucune édition à ce step.

- [ ] **Step 1.2 — Édition `KeyboardData.h`**
  - Ajouter `BANK_LOOP = 2` entre `BANK_ARPEG` et `BANK_ANY`.
  - Mettre à jour le `if` du validator de `> BANK_ARPEG` à `> BANK_LOOP`.
  - Le commentaire "BankType enum cast" sur `types[NUM_BANKS]` reste valable.

- [ ] **Step 1.3 — Édition `main.cpp`**
  - Dans `handlePadInput()` (ligne 579-587) : ajouter `default: break;` au switch avec commentaire `// BANK_LOOP : Phase 2 wires processLoopMode`. Garde le compile -Wswitch propre.
  - Dans le debug print de `BankManager::switchToBank` (ligne 207, fichier `BankManager.cpp` en réalité) : étendre le ternaire en table 3-way `(type==BANK_ARPEG ? "ARPEG" : type==BANK_LOOP ? "LOOP" : "NORMAL")`. Cosmétique mais évite la confusion debug.
  - Dans `reloadPerBankParams` (ligne 593) : aucun changement (déjà inerte sur LOOP — le `if (type == BANK_ARPEG)` filtre, defaults arp inchangés appliqués au catch).
  - Dans `pushParamsToEngine` (ligne 701) : aucun changement (idem filtre ARPEG).

- [ ] **Step 1.4 — Édition `NvsManager.cpp`**
  - Le default seed (ligne 620) reste `(i < 4) ? BANK_NORMAL : BANK_ARPEG` — cohérent avec usage actuel. Aucun changement.
  - Vérifier que `validateBankTypeStore` est appelé après `loadBlob` ligne 608 (déjà OK).

- [ ] **Step 1.5 — Édition `ToolBankConfig.cpp`**
  - Ligne 362 et 365 : étendre le ternaire `wkTypes[cursor] == BANK_ARPEG ? "ARPEG" : "NORMAL"` pour gérer LOOP. Phase 1 affiche "LOOP" si rencontré, mais le cycle reste NORMAL ↔ ARPEG (le 3-way arrive Phase 3, décision Q6 §28). Une bank LOOP en NVS (impossible avec UI Phase 1, défensif uniquement) s'affiche correctement.

- [ ] **Step 1.6 — Édition `LedController.cpp` `renderNormalDisplay` switch**
  - Ligne 493-496 : ajouter `case BANK_LOOP:` qui appelle un nouveau stub `renderBankLoop(i, isFg, now)`.
  - Déclarer `renderBankLoop(uint8_t led, bool isFg, unsigned long now)` en privé dans `LedController.h` (à côté de `renderBankNormal` et `renderBankArpeg`).
  - Implémenter le stub : `setPixel(led, _colors[CSLOT_MODE_LOOP], isFg ? 25 : (uint8_t)((25 * _bgFactor) / 100));`. Justification : LED spec §17 EMPTY = SOLID 25%. Aucune note bloquée, aucun appel MIDI. Phase 2 remplacera le stub par la state machine LOOP complète.

- [ ] **Step 1.7 — Compile gate**
  - Lancer `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`.
  - Vérifier exit 0, pas de warning nouveau (en particulier pas de `-Wswitch` sur `BankType`).

- [ ] **Step 1.8 — Static read-back**
  - Grep `BANK_NORMAL\|BANK_ARPEG` dans `src/` : confirmer que tous les sites de comparaison `type ==` ou `type !=` sont logiquement OK pour LOOP (en pratique : aucun ne doit traiter LOOP comme NORMAL ou ARPEG par défaut sans intention).
  - Grep `s.types[i]` : confirmer que validators et défauts gèrent la nouvelle borne.

- [ ] **Step 1.9 — Mise à jour briefing (sync requirement)**
  - Modifier [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) §4 Table 1 ligne `BankTypeStore` : remplacer "Nouveau `BankType` (ex: LOOP)" par mention factuelle "LOOP supporté Phase 1, Tool 5 3-way Phase 3".
  - Modifier §8 Domain Entry Points table : ligne `Bank switching` ajouter "(BANK_LOOP : Phase 1 dormant)".
  - Cf "Keep-in-sync protocol" CLAUDE.md projet ligne "F. CRITICAL — READ FIRST, KEEP IN SYNC".

- [ ] **Step 1.10 — Commit (sur autorisation explicite)**
  - Files exacts à stager : `src/core/KeyboardData.h`, `src/main.cpp`, `src/managers/BankManager.cpp`, `src/managers/NvsManager.cpp` (si touché — doit être 0 changement, lecture seule), `src/setup/ToolBankConfig.cpp`, `src/core/LedController.h`, `src/core/LedController.cpp`, `docs/reference/architecture-briefing.md`.
  - Pas de `git add -A`. Lister les paths un par un.
  - Message proposé (HEREDOC) : `feat(loop): phase 1 step 1 — BankType enum +BANK_LOOP cascade (validators, dispatch defaults, LED stub renderBankLoop)`
  - Body : référencer §27 Phase 1 item 1 + §28 Q8 (invariant 11).

---

## Task 2 — `LoopPadStore` 23 B strict packed + descriptor

**Cross-refs :** spec LOOP §20 + §28 Q1 ; audit F2.4 résolu ; nvs-reference §"How To Add a New Namespace".

**Files :**
- Modify : [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h) — nouvelle struct + namespace constants + validator + descriptor entry + static_assert
- Modify : [`docs/reference/nvs-reference.md`](../../reference/nvs-reference.md:107) — corriger ligne `LoopPadStore size=8B` → 23B + statut "DECLARED Phase 1 (no consumer yet)"

**Cible struct (à insérer dans `KeyboardData.h` près des autres pad stores, avant les V2 stores section) :**

```
#define LOOPPAD_NVS_NAMESPACE  "illpad_lpad"
#define LOOPPAD_NVS_KEY        "pads"
#define LOOPPAD_VERSION        1

struct __attribute__((packed)) LoopPadStore {
  uint16_t magic;          // 2 B  -> EEPROM_MAGIC (0xBEEF)
  uint8_t  version;        // 1 B  -> LOOPPAD_VERSION
  uint8_t  reserved;       // 1 B  -> alignement, 0
  uint8_t  recPad;         // 1 B  -> control pad REC,         0xFF si non assigne
  uint8_t  playStopPad;    // 1 B  -> control pad PLAY/STOP,   0xFF si non assigne
  uint8_t  clearPad;       // 1 B  -> control pad CLEAR,       0xFF si non assigne
  uint8_t  slotPads[16];   // 16 B -> slot pads 0..15,         0xFF si non assigne
};
static_assert(sizeof(LoopPadStore) == 23, "LoopPadStore must be exactly 23 B (Q1)");
static_assert(sizeof(LoopPadStore) <= NVS_BLOB_MAX_SIZE, "LoopPadStore exceeds NVS blob max");
```

**Layout justification :**
- 4 B header (magic + version + reserved) = pattern P6.
- 3 B controls + 16 B slots = 19 B data.
- Total = 23 B strict (vs 24 B aligné). `__attribute__((packed))` requis car ESP32-S3 n'a pas de pénalité d'accès non aligné pour uint8_t et la spec §28 Q1 exige strict packing.
- Tous les pads commencent à `0xFF` (sentinel "non assigné") au seed default. Phase 3 Tool 3 b1 ajoutera UI d'édition + persistance.

**Cible validator (à insérer juste après `validateArpPadStore`) :**

```
inline void validateLoopPadStore(LoopPadStore& s) {
  if (s.recPad      != 0xFF && s.recPad      >= NUM_KEYS) s.recPad      = 0xFF;
  if (s.playStopPad != 0xFF && s.playStopPad >= NUM_KEYS) s.playStopPad = 0xFF;
  if (s.clearPad    != 0xFF && s.clearPad    >= NUM_KEYS) s.clearPad    = 0xFF;
  for (uint8_t i = 0; i < 16; i++) {
    if (s.slotPads[i] != 0xFF && s.slotPads[i] >= NUM_KEYS) s.slotPads[i] = 0xFF;
  }
}
```

Note : la validation collision (rec ≠ playStop ≠ clear ≠ banks ≠ controlPads) est de la responsabilité de Tool 3 (Phase 3), pas de ce validator. Le validator clampe seulement les valeurs corrompues (out-of-range pad index). Cohérent avec `validateBankPadStore` et `validateArpPadStore`.

**Cible descriptor entry (à appender au tableau `NVS_DESCRIPTORS[]` ligne 762) :**

```
{ LOOPPAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY, EEPROM_MAGIC, LOOPPAD_VERSION, (uint16_t)sizeof(LoopPadStore) },  // 12: T3 LOOP (Phase 1 declared, T3 b1 wires Phase 3)
```

**TOOL_NVS_FIRST/LAST :** ne pas réindexer T3 en Phase 1. Phase 3 (Tool 3 b1 refactor) étendra `TOOL_NVS_LAST[2]` de `4` à `12`. Pour l'instant, le descriptor 12 n'est référencé par aucun Tool — il est invisible dans le menu badges, ce qui est acceptable Phase 1 (pas de UI consommatrice).

**NVS bump :** non sur les Stores existants. Nouveau magic/version/key, nouveau namespace. Premier boot post-flash : aucune entrée présente, validator non appelé, defaults compile-time s'appliqueront le jour où Phase 3 wirera Tool 3 b1.

**Steps :**

- [ ] **Step 2.1 — Lecture KeyboardData.h** : confirmer la zone d'insertion (entre `ArpPadStore` ligne ~487 et `BankTypeStore` ligne 489).

- [ ] **Step 2.2 — Insertion struct + namespace defines** dans `KeyboardData.h`. Respecter l'ordre : defines (`LOOPPAD_NVS_NAMESPACE`, `LOOPPAD_NVS_KEY`, `LOOPPAD_VERSION`) → struct → static_asserts (sizeof == 23 ET <= 128).

- [ ] **Step 2.3 — Insertion validator** `validateLoopPadStore` après `validateArpPadStore`.

- [ ] **Step 2.4 — Append descriptor** au tableau `NVS_DESCRIPTORS[]`. Vérifier que `NVS_DESCRIPTOR_COUNT` reste calculé via `sizeof(NVS_DESCRIPTORS) / sizeof(NVS_DESCRIPTORS[0])` (déjà OK ligne 776) — l'ajout est automatiquement compté.

- [ ] **Step 2.5 — Mise à jour `nvs-reference.md`** :
  - Section "V2 Stores (replace raw formats)" ligne 107 : changer la dernière colonne de `**PLANNED** — not yet in code` à `**DECLARED Phase 1** — descriptor + validator in code, no consumer yet (T3 b1 wires Phase 3)`.
  - Confirmer size 23B (déjà correct dans le doc).

- [ ] **Step 2.6 — Compile gate** : `pio run`, exit 0. Le static_assert `== 23` est le test critique : si le compilateur n'arrive pas à packer à 23 B, le build casse au static_assert et la cause est dans la struct (alignement ESP32 par défaut).

- [ ] **Step 2.7 — Static read-back** : grep `LoopPadStore` dans tout `src/` pour confirmer 0 consommateur (attendu Phase 1).

- [ ] **Step 2.8 — Commit (sur autorisation)** : `feat(loop): phase 1 step 2 — declare LoopPadStore (23 B packed, NVS descriptor, no runtime consumer)`. Body cite §28 Q1 + audit F2.4.

---

## Task 3 — `LoopPotStore` per-bank + descriptor

**Cross-refs :** spec LOOP §20 (LoopPotStore per-bank) + §10 (5 effets) ; nvs-reference catalogue + section "Non-Blob Namespaces" pour le pattern per-bank keys.

**Files :**
- Modify : [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h) — nouvelle struct + namespace + validator + static_assert
- Modify : [`docs/reference/nvs-reference.md`](../../reference/nvs-reference.md:118) — ligne `illpad_lpot` : préciser layout final, marquer "DECLARED Phase 1, consumed Phase 5"

**Cible struct (insertion après `ArpPotStore`) :**

```
#define LOOPPOT_NVS_NAMESPACE  "illpad_lpot"
// Keys : "loop_0" through "loop_7" (per bank, identique au pattern ArpPotStore)
#define LOOPPOT_VERSION        1

struct __attribute__((packed)) LoopPotStore {
  uint16_t magic;             // 2 B
  uint8_t  version;           // 1 B
  uint8_t  reserved;          // 1 B
  uint16_t shuffleDepthRaw;   // 2 B  -> 0..4095 (raw 12-bit ADC, comme ArpPotStore)
  uint8_t  shuffleTemplate;   // 1 B  -> 0..9 (10 templates, partage avec ArpEngine, GrooveTemplates.h)
  uint16_t chaosRaw;          // 2 B  -> 0..4095 (raw)
  uint8_t  velPattern;        // 1 B  -> 0..3 (4 LUTs : accent, downbeat, backbeat, swing)
  uint16_t velPatternDepthRaw;// 2 B  -> 0..4095 (raw)
};
static_assert(sizeof(LoopPotStore) <= NVS_BLOB_MAX_SIZE, "LoopPotStore exceeds NVS blob max");
// Note : taille exacte ~12 B après packing strict ; pas de static_assert ==N car le layout
// peut accueillir un padding sans impact runtime (la nvs-reference annonce 8 B mais cela
// ne tient pas avec 5 effets ; corrige a la valeur reelle au step 3.5).
```

**Justification 5 effets (LED spec §10 + spec LOOP §10) :** shuffle depth, shuffle template, chaos, vel pattern (4 LUTs sélecteur), vel pattern depth. Base velocity et velocity variation sont **shared per-bank** avec NORMAL/ARPEG (déjà persistés dans `illpad_bvel`), pas dans LoopPotStore. Tempo est global (`illpad_tempo`).

**Cible validator :**

```
inline void validateLoopPotStore(LoopPotStore& s) {
  if (s.shuffleDepthRaw    > 4095) s.shuffleDepthRaw    = 0;
  if (s.shuffleTemplate    >= 10)  s.shuffleTemplate    = 0;  // NUM_SHUFFLE_TEMPLATES
  if (s.chaosRaw           > 4095) s.chaosRaw           = 0;
  if (s.velPattern         >= 4)   s.velPattern         = 0;
  if (s.velPatternDepthRaw > 4095) s.velPatternDepthRaw = 0;
}
```

**Pas de descriptor entry pour `NVS_DESCRIPTORS[]` :** comme `ArpPotStore` qui est aussi per-bank avec 8 keys différentes (`arp_0..arp_7`), le pattern actuel ne descripteurise pas ces stores (cf nvs-reference "Non-Blob Namespaces" — bien que `LoopPotStore` soit un blob, le multi-key per-bank rend une entrée unique trompeuse). À la place : Phase 5 ajoutera 8 entrées au descriptor table OU une fonction `checkAllLoopPots()` à la `getLoadedArpParams()`. Phase 1 = struct + validator déclarés seulement.

**NVS bump :** non sur les Stores existants. Nouveau namespace + magic/version. Aucun reset user.

**Steps :**

- [ ] **Step 3.1 — Lecture `KeyboardData.h`** + relecture spec §10 (5 effets) + nvs-reference §"Non-Blob Namespaces" pour aligner pattern multi-key.

- [ ] **Step 3.2 — Insertion struct + namespace + validator** dans `KeyboardData.h` après `ArpPotStore` (ligne ~470 environ). Respecter pattern packed. **Calculer la taille effective** post-packing avant le step 3.5 (probablement 12 B avec packing strict ; vérifier au compile via static_assert temporaire `static_assert(sizeof(LoopPotStore) == 12, ...)` puis ajuster la doc).

- [ ] **Step 3.3 — Compile gate intermédiaire** : confirmer la taille via le static_assert. Si != 12, retirer le static_assert == et noter la valeur réelle pour l'étape 3.5.

- [ ] **Step 3.4 — Retirer le static_assert ==N** une fois la taille confirmée (le `<= NVS_BLOB_MAX_SIZE` reste). Le projet n'a pas l'habitude d'imposer une taille exacte sauf cas particulier (`LoopPadStore` Q1 strict ; ici pas de contrainte spec).

- [ ] **Step 3.5 — Mise à jour `nvs-reference.md`** :
  - Section "Non-Blob Namespaces" ligne 118 : remplacer "8B each" par "~12B each (5 effets, layout final voir KeyboardData.h)" + marquer "DECLARED Phase 1, consumed Phase 5".

- [ ] **Step 3.6 — Compile gate final** : `pio run`, exit 0.

- [ ] **Step 3.7 — Static read-back** : grep `LoopPotStore` dans `src/` → 0 consommateur attendu.

- [ ] **Step 3.8 — Commit (sur autorisation)** : `feat(loop): phase 1 step 3 — declare LoopPotStore per-bank (5 effets, no runtime consumer)`. Body cite §10 + §20.

---

## Task 4 — Guards défensifs `BankManager` / `ScaleManager` / dispatch

**Cross-refs :** spec LOOP §27 Phase 1 item 4 ; invariants §23 (1, 6, 11) ; audit §4.3 item 2.

**Files :**
- Modify : [`src/managers/BankManager.cpp`](../../../src/managers/BankManager.cpp:81-98) — double-tap LOOP no-op (Phase 2 wires PLAY/STOP via LoopEngine)
- Modify : [`src/managers/BankManager.cpp`](../../../src/managers/BankManager.cpp:180-209) — `switchToBank` PB restore reste NORMAL-only (déjà OK), debug print 3-way (déjà fait Task 1 ; vérifier en read-back)
- Modify : [`src/managers/ScaleManager.cpp`](../../../src/managers/ScaleManager.cpp:114) — `processScalePads` early-return si `slot.type == BANK_LOOP`
- Read-only check : `src/midi/MidiEngine.cpp`, `src/core/MidiTransport.cpp` — confirmer que pas de guard requis (AT/PB callsite gardés en amont par `slot.type` switch dans main.cpp)

**Détail des guards :**

**A. `BankManager::tap2 LOOP bank pad** (lignes 81-98) — actuellement `if (wasRecent && _banks[b].type == BANK_ARPEG)` traite le double-tap pour toggle ArpEngine. Étendre :
- Ajouter une branche `else if (wasRecent && _banks[b].type == BANK_LOOP) { /* Phase 2 : LoopEngine.toggle() ; pour Phase 1 : consume the 2nd tap silently to prevent fall-through to bank switch */ _lastBankPadPressTime[b] = 0; _pendingSwitchBank = -1; continue; }`
- Justification : §19 spec LOOP étend le geste à LOOP. Phase 1 n'a pas de LoopEngine, donc on consomme le tap (pas de toggle, pas de switch). Phase 2 remplacera le commentaire par l'appel à `LoopEngine::setCaptured()` ou équivalent. Cela évite qu'un musicien crée par accident un comportement "double-tap = bank switch" sur LOOP, qui serait incohérent avec ARPEG.

**B. `BankManager::switchToBank`** (lignes 180-209) — déjà correct sur le PB (`if (type == BANK_NORMAL) sendPitchBend(slot.pitchBendOffset)` ne fire que pour NORMAL ; LOOP et ARPEG sautent). Le `_engine->sendPitchBend(8192)` au début est OK pour tout channel (reset universel). **Aucune modification de fond, juste vérification read-only.** Le debug print sur LOOP est cosmétique et déjà ajouté Task 1.

**C. Pendant switch refusé en RECORDING/OVERDUBBING** (invariant 2 §23) — Phase 1 n'a pas LoopEngine, donc pas de state RECORDING/OVERDUBBING à observer. Phase 2 ajoutera la logique `if (currentLoopState in {REC, OD}) silentDeny()`. **Note pour Phase 2 : cette logique doit vivre dans `BankManager::switchToBank` ou en pre-check dans `update()`. Phase 1 n'implémente pas ce guard — il sera ajouté avec LoopEngine.**

**D. `ScaleManager::processScalePads`** (ligne 114) — actuellement traite root/mode/chrom + octave (ARPEG). Le code a déjà des guards `if (slot.type == BANK_NORMAL && _engine) _engine->allNotesOff();` et `if (slot.type == BANK_ARPEG && slot.arpEngine)` pour octave. Mais la mutation `slot.scale.root = r;` (ligne 129) et `slot.scale.mode = m;` (ligne 151) et `slot.scale.chromatic = true;` (ligne 170) **fire pour tous les types**, y compris LOOP. Sur une bank LOOP, mutation gratuite et set du flag `_scaleChangeType` qui déclenche un confirm LED + un NVS write inutile.

Patch : early-return tout en haut de la fonction si `slot.type == BANK_LOOP`. Préserve l'invariant §23 invariant 6 ("Pas de scale sur une bank LOOP").

```
void ScaleManager::processScalePads(const uint8_t* keyIsPressed, BankSlot& slot) {
  if (slot.type == BANK_LOOP) return;   // <-- NEW : invariant 6 §23
  // ... rest unchanged (root/mode/chrom/octave loops) ...
}
```

**E. AT/PB transport guards** — vérification read-only :
- `processNormalMode` (main.cpp ligne 475) → seul caller de `_midiEngine.updateAftertouch()` et noteOn/Off. Dispatché depuis `handlePadInput` ligne 580 avec `case BANK_NORMAL`. **LOOP n'arrive jamais ici** grâce au switch sans default-call (Task 1 ajoute le default no-op).
- `processArpMode` (main.cpp ligne 511) → idem, gardé par `case BANK_ARPEG`.
- `BankManager::switchToBank` PB restore → gardé NORMAL-only (déjà OK).
- **Conclusion :** aucune modif transport. Le commentaire de la spec "MidiTransport guards" est satisfait par l'architecture actuelle ; documenter ce point dans le commit body.

**Steps :**

- [ ] **Step 4.1 — Lecture des 3 fichiers cibles** + grep des callsites AT/PB pour confirmer absence de chemin LOOP non gardé. Faire la liste exhaustive avant de toucher au code.

- [ ] **Step 4.2 — Édition `BankManager.cpp`** :
  - Étendre la branche double-tap pour ajouter `else if (... type == BANK_LOOP)`. Le corps consomme le 2nd tap (`_lastBankPadPressTime[b] = 0; _pendingSwitchBank = -1; continue;`) et ajoute un commentaire `// Phase 2 : route to LoopEngine.toggleCaptured() — Phase 1 silent consume to prevent unwanted bank switch on LOOP double-tap`.

- [ ] **Step 4.3 — Édition `ScaleManager.cpp`** :
  - Ajouter `if (slot.type == BANK_LOOP) return;` en première instruction de `processScalePads` (ligne 115).
  - Aucune autre modification.

- [ ] **Step 4.4 — Read-only verification AT/PB** :
  - Grep `sendPolyAftertouch\|sendPitchBend\|updateAftertouch\|noteOn\|noteOff` dans `src/main.cpp`, `src/managers/`, `src/midi/`. Tracer chaque callsite. Confirmer que tous sont sous une garde de type. Documenter le résultat (briefly) dans le commit body.

- [ ] **Step 4.5 — Mise à jour briefing** :
  - [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) §2 "Bank Switch (all side effects in order)" : ajouter une ligne note "BANK_LOOP : double-tap consommé Phase 1 ; LoopEngine.toggle Phase 2".
  - §9 invariants : ajouter invariant 11 §23 LOOP (max 1 bank REC/OD) si pas déjà présent.

- [ ] **Step 4.6 — Compile gate** : `pio run`, exit 0.

- [ ] **Step 4.7 — Static read-back** : grep `BANK_LOOP` dans `src/managers/` et `src/main.cpp` pour vérifier la couverture des guards.

- [ ] **Step 4.8 — Commit (sur autorisation)** : `feat(loop): phase 1 step 4 — defensive guards (ScaleManager early-return on LOOP, BankManager double-tap LOOP consume)`. Body :
  - Cite §27 Phase 1 item 4 + invariants 6 et 11 §23.
  - Note : "MidiTransport AT/PB : no change required, callsites already gated by slot.type switch in main.cpp::handlePadInput. Verified by grep, listed below: ..."

---

## Task 5 — Rename `LedSettingsStore::fgArpPlayMax` → `fgPlayMax`

**Cross-refs :** spec LOOP §28 Q4 ; audit F2.3 résolu.

**Files :**
- Modify : [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h:274) — field rename in `LedSettingsStore`
- Modify : [`src/core/LedController.h`](../../../src/core/LedController.h:171) — member rename `_fgArpPlayMax` → `_fgPlayMax`
- Modify : [`src/core/LedController.cpp`](../../../src/core/LedController.cpp:20,451,452,877) — constructor init + `loadLedSettings` + `renderBankArpeg` callsites
- Modify : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp:181,479,480,518,519,757,758,863,869) — working copy `_lwk.fgArpPlayMax` rename
- Modify : [`src/managers/NvsManager.cpp`](../../../src/managers/NvsManager.cpp) — verify no direct field access

**Layout NVS :** identique. `LedSettingsStore` v7 reste v7 (pas de bump version). Le binaire stocké en flash est lu sans changement ; seuls les noms changent dans le code source. **Aucun reset user.**

**Strict scope du rename :** uniquement le field `fgArpPlayMax` (et son membre `_fgArpPlayMax`). **Ne pas** toucher aux autres legacy `_bgArpStopMin`, `_bgArpPlayMin` qui restent partagés ARPEG-only — leur rename est hors scope (Phase 4+ pourra les renommer si besoin ; pour l'instant ils sont effectivement utilisés uniquement par `renderBankArpeg`).

**Steps :**

- [ ] **Step 5.1 — Inventaire grep complet** :
  - `grep -rn "fgArpPlayMax\|_fgArpPlayMax" src/` → liste tous les callsites.
  - Attendu : ~9 occurrences dans 4 fichiers (`KeyboardData.h`, `LedController.h`, `LedController.cpp`, `ToolLedSettings.cpp`).

- [ ] **Step 5.2 — Édition `KeyboardData.h` ligne 274** : `uint8_t fgArpPlayMax;` → `uint8_t fgPlayMax;`. Mettre à jour le commentaire : `// default 80 — SOLID intensity FG playing (shared ARPEG + LOOP, Q4 §28)`.

- [ ] **Step 5.3 — Édition `LedController.h` ligne 171** : member `_fgArpPlayMax` → `_fgPlayMax`. Mettre à jour le commentaire.

- [ ] **Step 5.4 — Édition `LedController.cpp`** :
  - Ligne 20 (constructor init list) : `_fgArpPlayMax(80)` → `_fgPlayMax(80)`.
  - Lignes 451-452 (`renderBankArpeg`) : `_fgArpPlayMax` → `_fgPlayMax` (2 occurrences).
  - Ligne 877 (`loadLedSettings`) : `_fgArpPlayMax = s.fgArpPlayMax;` → `_fgPlayMax = s.fgPlayMax;`.

- [ ] **Step 5.5 — Édition `ToolLedSettings.cpp`** :
  - Ligne 181 (defaults dans `loadAll`) : `_lwk.fgArpPlayMax = 80;` → `_lwk.fgPlayMax = 80;`.
  - Ligne 479 (readNumericField LINE_ARPEG_FG_PCT) : `return _lwk.fgArpPlayMax;` → `return _lwk.fgPlayMax;`. (Cette ligne disparaît au step 6 ; rename d'abord pour atomicité.)
  - Ligne 480 (readNumericField LINE_LOOP_FG_PCT) : idem. (Disparaît au step 6.)
  - Ligne 518 (writeNumericField LINE_ARPEG_FG_PCT) : `_lwk.fgArpPlayMax = (uint8_t)v;` → `_lwk.fgPlayMax = ...`. (Disparaît au step 6.)
  - Ligne 519 (writeNumericField LINE_LOOP_FG_PCT) : idem. Retirer le commentaire `// shared w/ ARPEG` (rendra la décision Q4 implicite — Step 6 le rend explicite via TRANSPORT placement).
  - Lignes 757-758 (resetForLine ARPEG/LOOP defaults) : idem rename.
  - Lignes 863, 869 (previewContextForLine `p.fgPct = _lwk.fgArpPlayMax;`) : 2 occurrences à renommer.

- [ ] **Step 5.6 — `NvsManager.cpp` read-only** : grep `fgArpPlayMax` → 0 résultat attendu (NvsManager n'accède pas directement aux fields LedSettings). Confirmer.

- [ ] **Step 5.7 — Compile gate** : `pio run`, exit 0. Le compile failure typique est un callsite oublié → l'erreur cite exactement la ligne.

- [ ] **Step 5.8 — Static read-back** : `grep -rn "fgArpPlayMax\|_fgArpPlayMax" src/` → 0 résultat.

- [ ] **Step 5.9 — Mise à jour briefing** :
  - [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) §4 Table 1 ligne LedSettings : ajouter mention du rename `fgArpPlayMax → fgPlayMax (shared ARPEG+LOOP playing, Phase 1 Q4)`.

- [ ] **Step 5.10 — Commit (sur autorisation)** : `refactor(led): phase 1 step 5 — rename LedSettingsStore::fgArpPlayMax to fgPlayMax (shared ARPEG+LOOP, no NVS bump)`. Body cite §28 Q4 + audit F2.3 + précise "binary layout identical, no user reset".

---

## Task 6 — Tool 8 ligne FG brightness déplacée vers section TRANSPORT

**Cross-refs :** spec LOOP §28 Q4 ; setup-tools-conventions §4.4 (paradigme single-view) ; Tool 8 respec doc.

**Files :**
- Modify : [`src/setup/ToolLedSettings.h`](../../../src/setup/ToolLedSettings.h:55-102) — enum `LineId` : retirer `LINE_ARPEG_FG_PCT` + `LINE_LOOP_FG_PCT`, ajouter `LINE_TRANSPORT_FG_PCT` après `LINE_TRANSPORT_BREATHING`
- Modify : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp) — refactor de toutes les tables indexées par LineId : `LINE_LABELS[]`, `LINE_DESCRIPTIONS[]`, `sectionOf()`, `firstLineOfSection()`, `lastLineOfSection()`, `shapeForLine()`, `colorSlotForLine()`, `readNumericField()`, `writeNumericField()`, `minMaxForLine()`, `resetForLine()`, `previewContextForLine()`, `drawLine()` unit dispatch, navigation hint dispatch
- Modify : [`docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md`](../specs/2026-04-20-tool8-ux-respec-design.md) — §4.1 layout : retirer "FG brightness" de ARPEG + LOOP sections, l'ajouter en TRANSPORT (1 ligne diff doc)

**Cible enum LineId** (extraits, sections diff seulement) :

```
enum LineId : uint8_t {
  LINE_NORMAL_BASE_COLOR = 0,
  LINE_NORMAL_FG_PCT,

  LINE_ARPEG_BASE_COLOR,
  // LINE_ARPEG_FG_PCT  <-- removed Q4 (Phase 1 step 6)

  LINE_LOOP_BASE_COLOR,
  // LINE_LOOP_FG_PCT  <-- removed Q4 (Phase 1 step 6)
  LINE_LOOP_SAVE_COLOR,
  LINE_LOOP_SAVE_DURATION,
  LINE_LOOP_CLEAR_COLOR,
  LINE_LOOP_CLEAR_DURATION,
  LINE_LOOP_SLOT_COLOR,
  LINE_LOOP_SLOT_DURATION,

  LINE_TRANSPORT_PLAY_COLOR,
  LINE_TRANSPORT_PLAY_TIMING,
  LINE_TRANSPORT_STOP_COLOR,
  LINE_TRANSPORT_STOP_TIMING,
  LINE_TRANSPORT_WAITING_COLOR,
  LINE_TRANSPORT_BREATHING,
  LINE_TRANSPORT_FG_PCT,           // <-- NEW Q4 : shared ARPEG+LOOP playing brightness
  LINE_TRANSPORT_TICK_COMMON,
  LINE_TRANSPORT_TICK_PLAY_COLOR,
  ... // unchanged
```

**Label cible** : `"FG brightness"` (court, cohérent avec `LINE_NORMAL_FG_PCT` de NORMAL section).
**Description cible** : `"Solid FG brightness for ARPEG playing and LOOP playing states (10-100%). BG derives via BG factor."`. Mentionne explicitement l'aspect "shared" pour respecter F1 critère LED spec et évite confusion utilisateur (cf project_vt100_manual.md mémoire user).

**Shape :** `SHAPE_SINGLE_NUM` (comme `LINE_NORMAL_FG_PCT`).
**Min/max :** 10..100 (comme `LINE_NORMAL_FG_PCT`).
**readNumericField → `_lwk.fgPlayMax`. writeNumericField → `_lwk.fgPlayMax`. resetForLine → 80.**

**Cible `sectionOf()`** : threshold update — la section ARPEG ne contient plus que `LINE_ARPEG_BASE_COLOR`, donc le test `if (line <= LINE_ARPEG_FG_PCT)` devient `if (line <= LINE_ARPEG_BASE_COLOR)`. Idem pour LOOP : threshold devient `LINE_LOOP_SLOT_DURATION` (inchangé). Pour TRANSPORT, le `lastLineOfSection` reste `LINE_TRANSPORT_TICK_WRAP_DUR`. Impact résumé :

```
Section sectionOf(LineId line) const {
  if (line <= LINE_NORMAL_FG_PCT)            return SEC_NORMAL;
  if (line <= LINE_ARPEG_BASE_COLOR)         return SEC_ARPEG;     // <-- updated
  if (line <= LINE_LOOP_SLOT_DURATION)       return SEC_LOOP;
  if (line <= LINE_TRANSPORT_TICK_WRAP_DUR)  return SEC_TRANSPORT;
  if (line <= LINE_CONFIRM_OK_SPARK)         return SEC_CONFIRMATIONS;
  return SEC_GLOBAL;
}

LineId lastLineOfSection(Section s) {
  ...
  case SEC_ARPEG: return LINE_ARPEG_BASE_COLOR;  // <-- updated (was FG_PCT)
  ...
}
```

**Cible `previewContextForLine`** : pour `LINE_ARPEG_BASE_COLOR` seul (non plus FG_PCT), le preview reste `PV_BASE_COLOR` avec `p.fgPct = _lwk.fgPlayMax`. Pour `LINE_TRANSPORT_FG_PCT` (nouvelle), preview suggéré : un mockup ARPEG playing + LOOP playing simultanés (LED 3-4 par exemple), couleurs ARPEG+LOOP base, intensité = `_lwk.fgPlayMax`. **Si le helper `ToolLedPreview` ne supporte pas ce mode, fallback simple : `PV_BASE_COLOR` avec couleur LOOP (CSLOT_MODE_LOOP) — la valeur s'applique également à ARPEG, le preview est représentatif du résultat.** À confirmer en lisant `ToolLedPreview.h` lors du step 6.2.

**Save policy :** §1 setup-tools-conventions — pas de save par keystroke. Le save existant via `saveLedSettings()` au commit (`y` confirm ou exit) reste inchangé.

**Steps :**

- [ ] **Step 6.1 — Lecture exhaustive `ToolLedSettings.cpp` + `ToolLedSettings.h`** : identifier tous les sites qui dispatchent sur `LINE_ARPEG_FG_PCT` ou `LINE_LOOP_FG_PCT`. Inventaire grep `LINE_ARPEG_FG_PCT\|LINE_LOOP_FG_PCT` dans `src/setup/`.

- [ ] **Step 6.2 — Lecture `ToolLedPreview.h`** pour décider du contexte preview de `LINE_TRANSPORT_FG_PCT` (cf bloc cible ci-dessus). Si support multi-LED simultané existe, l'utiliser ; sinon fallback `PV_BASE_COLOR` avec MODE_LOOP.

- [ ] **Step 6.3 — Édition `ToolLedSettings.h`** :
  - Retirer `LINE_ARPEG_FG_PCT` et `LINE_LOOP_FG_PCT` de l'enum.
  - Ajouter `LINE_TRANSPORT_FG_PCT` juste après `LINE_TRANSPORT_BREATHING`.
  - L'ordre des lignes après les retraits décale `LINE_COUNT` automatiquement (compile-time).

- [ ] **Step 6.4 — Édition `ToolLedSettings.cpp` tables flat-resident** :
  - `SECTION_LABELS[]` ligne 28 : inchangé (les noms de sections ne bougent pas).
  - `LINE_LABELS[]` ligne 33 : retirer les 2 lignes "FG brightness" (ARPEG, LOOP), ajouter `"FG brightness"` au bon offset TRANSPORT (entre "Breathing" et "Tick common").
  - `LINE_DESCRIPTIONS[]` ligne 83 : retirer 2 entrées correspondantes, ajouter une nouvelle description claire au bon offset.

- [ ] **Step 6.5 — Édition `sectionOf()` + `lastLineOfSection()`** : mettre à jour les thresholds (cf bloc cible).

- [ ] **Step 6.6 — Édition `shapeForLine()`** : retirer entrées `LINE_ARPEG_FG_PCT` + `LINE_LOOP_FG_PCT` ; ajouter `LINE_TRANSPORT_FG_PCT` → `SHAPE_SINGLE_NUM`.

- [ ] **Step 6.7 — Édition `colorSlotForLine()`** : aucune entrée LINE_*_FG_PCT n'avait de color slot (les FG_PCT sont brightness, pas color) → retraits silencieux. Vérifier.

- [ ] **Step 6.8 — Édition `readNumericField()`** :
  - Retirer cases `LINE_ARPEG_FG_PCT` + `LINE_LOOP_FG_PCT`.
  - Ajouter `case LINE_TRANSPORT_FG_PCT: return _lwk.fgPlayMax;`.

- [ ] **Step 6.9 — Édition `writeNumericField()`** :
  - Retirer cases `LINE_ARPEG_FG_PCT` + `LINE_LOOP_FG_PCT`.
  - Ajouter `case LINE_TRANSPORT_FG_PCT: _lwk.fgPlayMax = (uint8_t)v; break;`.

- [ ] **Step 6.10 — Édition `minMaxForLine()`** :
  - Retirer cases ARPEG/LOOP FG_PCT (mn=10, mx=100).
  - Ajouter case TRANSPORT_FG_PCT (mn=10, mx=100).

- [ ] **Step 6.11 — Édition `resetForLine()`** :
  - Retirer cases ARPEG/LOOP FG_PCT.
  - Ajouter `case LINE_TRANSPORT_FG_PCT: _lwk.fgPlayMax = 80; break;`.

- [ ] **Step 6.12 — Édition `previewContextForLine()`** :
  - Retirer cases ARPEG/LOOP FG_PCT (lignes 859-870 actuelles). `LINE_ARPEG_BASE_COLOR` et `LINE_LOOP_BASE_COLOR` gardent leur preview existant (mais `p.fgPct = _lwk.fgPlayMax` au lieu de `fgArpPlayMax` — Task 5 a déjà fait le rename).
  - Ajouter case `LINE_TRANSPORT_FG_PCT` → preview MODE_LOOP solid à `_lwk.fgPlayMax` (decided step 6.2).

- [ ] **Step 6.13 — Édition unit dispatch `drawLine()` (lignes 1144-1147)** :
  - Retirer cases ARPEG/LOOP FG_PCT du switch unit `"%"`.
  - Ajouter `case LINE_TRANSPORT_FG_PCT:` au même bloc `unit = "%"`.

- [ ] **Step 6.14 — Édition Tool 8 respec doc** :
  - [`docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md`](../specs/2026-04-20-tool8-ux-respec-design.md) §4.1 layout : retirer "FG brightness" des sections ARPEG + LOOP du tableau, ajouter une ligne "FG brightness (shared ARPEG+LOOP)" dans la section TRANSPORT entre "Breathing" et "Tick common".
  - §9 Defaults : mettre à jour le tableau des defaults pour refléter le rename + déplacement.
  - §4.4 ou 4.5 (paradigme nav) : pas d'impact, le paradigme géométrique reste.

- [ ] **Step 6.15 — Compile gate** : `pio run`. Le compile failure typique = un case oublié dans un switch → erreur explicite. Si `-Wswitch` warning sur `Section sectionOf` ou un autre switch sur LineId, les enrichir au passage.

- [ ] **Step 6.16 — Static read-back** : `grep -rn "LINE_ARPEG_FG_PCT\|LINE_LOOP_FG_PCT" src/` → 0 résultat. `grep -n "LINE_TRANSPORT_FG_PCT" src/setup/ToolLedSettings.cpp` → présent dans tous les dispatchs (~10 sites).

- [ ] **Step 6.17 — HW test (sur autorisation upload)** :
  - Boot → entrée setup → Tool 8.
  - Naviguer dans section TRANSPORT, vérifier la nouvelle ligne "FG brightness" entre Breathing et Tick common.
  - Éditer la valeur (left/right ±, up/down ±) → preview live attendu (MODE_LOOP solid à intensité variable).
  - Confirmer save (`y` à la sortie ou exit) → flashSaved unique.
  - Re-naviguer en section ARPEG : seul "Base color" reste. Idem en section LOOP : "Base color" + 3 lignes save/clear/slot.
  - Boot ARPEG playing : intensité solid suit la nouvelle valeur (plus visible si la valeur a été montée à 100).

- [ ] **Step 6.18 — Commit (sur autorisation)** : `feat(led): phase 1 step 6 — Tool 8 line "FG brightness" moved to TRANSPORT section (shared ARPEG+LOOP, Q4)`. Body cite §28 Q4 + audit F2.3 + Tool 8 respec doc updated.

---

## Task 7 — `EVT_WAITING` mode-invariant + BG-aware brightness

**Cross-refs :** spec LOOP §28 Q3 ; LED spec §10 (palette CROSSFADE_COLOR) + §17 (table mode×state) ; audit F2.2 résolu.

**Files :**
- Modify : [`src/core/LedGrammar.cpp`](../../../src/core/LedGrammar.cpp:30) — `EVT_WAITING` default colorSlot : `CSLOT_MODE_ARPEG` → `CSLOT_VERB_PLAY` (vert, éditable Tool 8 via TRANSPORT_WAITING_COLOR si exposé)
- Modify : [`src/core/LedController.cpp`](../../../src/core/LedController.cpp:649-655) — `triggerEvent` case `PTN_CROSSFADE_COLOR` : hardcode `colorB = _colors[CSLOT_CONFIRM_OK]` + override `fgPct = _fgArpStopMax`
- Modify : [`src/core/LedController.cpp`](../../../src/core/LedController.cpp:788-806) — `renderPattern` case `PTN_CROSSFADE_COLOR` : ajouter scaling `× _bgFactor` pour LEDs non-FG dans la boucle setPixel

**Décision §28 Q3 résumé (à respecter strictement) :**
- 1 event unique `EVT_WAITING` (pas de scission ARPEG/LOOP).
- `colorA = CSLOT_VERB_PLAY` (vert) — éditable via `LINE_TRANSPORT_WAITING_COLOR` Tool 8 (déjà présent).
- `colorB = CSLOT_CONFIRM_OK` (blanc) — **hardcodé dans `triggerEvent`**, pas exposé en Tool 8 (cohérence avec critère T2 LED spec : la 2e couleur du crossfade est portée par l'event consommateur).
- `fgPct = _fgArpStopMax` (breathing max — pas `_fgPlayMax`). Justification : WAITING est un état de transition court, perçu comme "respiration accélérée vers décision" — l'amplitude breathing-max est la référence partagée avec PULSE_SLOW (§17 spec LED).
- BG : `fgPct × _bgFactor / 100` (cohérent avec convention §8 spec LED "BG derives via bgFactor").

**Cible diff `LedGrammar.cpp` ligne 30** :

```
/* EVT_WAITING           */ { PTN_CROSSFADE_COLOR, CSLOT_VERB_PLAY, 100 }, // colorA = green (editable Tool 8 TRANSPORT_WAITING_COLOR) ; colorB hardcoded white in triggerEvent (Q3 §28)
```

**Cible diff `LedController.cpp` `triggerEvent` case PTN_CROSSFADE_COLOR (lignes 649-655)** :

```
case PTN_CROSSFADE_COLOR: {
  // WAITING (mode-invariant per Q3 §28) : colorA = green editable (entry), colorB = white hardcoded.
  // fgPct overridden to _fgArpStopMax (breathing max, shared with PULSE_SLOW reference).
  _eventOverlay.params.crossfadeColor.periodMs = 800;
  _eventOverlay.colorB = _colors[CSLOT_CONFIRM_OK];   // <-- white, not _eventOverlay.colorA
  _eventOverlay.fgPct  = _fgArpStopMax;                // <-- override entry.fgPct
  break;
}
```

**Cible diff `LedController.cpp` `renderPattern` case PTN_CROSSFADE_COLOR (lignes 788-806)** :

```
case PTN_CROSSFADE_COLOR: {
  const auto& p = inst.params.crossfadeColor;
  if (p.periodMs == 0) return;
  // Sine interp between colorA and colorB (existing logic, unchanged) ...
  // RGBW mixed = ...
  for (uint8_t led = 0; led < NUM_LEDS; led++) {
    if (!(mask & (1 << led))) continue;
    // BG-aware scaling : FG bank uses inst.fgPct as-is, BG banks scale by _bgFactor (Q3 §28)
    uint8_t effectivePct = (led == _currentBank)
                            ? inst.fgPct
                            : (uint8_t)((uint16_t)inst.fgPct * _bgFactor / 100);
    setPixel(led, mixed, effectivePct);
  }
  break;
}
```

**Important — pas de modification des autres patterns :** Q3 ne s'applique qu'à PTN_CROSSFADE_COLOR (utilisé par EVT_WAITING). Les autres patterns (PTN_FADE pour PLAY/STOP, PTN_BLINK_* pour confirms, PTN_FLASH pour ticks) gardent leur comportement actuel. Le "BG-aware scaling" reste local à WAITING. Si une généralisation est utile plus tard (ex : PLAY/STOP sur BG bank LOOP), elle sera portée Phase 2/4 par event au cas par cas, pas en Phase 1.

**LedGrammar.h enum :** aucun changement. `EVT_WAITING = 7` reste réservé. C'est seulement la table `EVENT_RENDER_DEFAULT` qui change.

**HW visibility en Phase 1 :** EVT_WAITING n'est jamais émis par le code Phase 1 (LoopEngine n'existe pas encore). Le test runtime est nul. Validation = compile + read-back + Phase 2 fera la première observation HW réelle.

**Steps :**

- [ ] **Step 7.1 — Lecture des 3 sites de modification** + relecture LED spec §10 (CROSSFADE) et §17 (table mode×state×FG/BG).

- [ ] **Step 7.2 — Édition `LedGrammar.cpp` ligne 30** :
  - Changer le colorSlot `CSLOT_MODE_ARPEG` → `CSLOT_VERB_PLAY`.
  - Mettre à jour le commentaire pour expliciter Q3 §28.

- [ ] **Step 7.3 — Édition `LedController.cpp` `triggerEvent` PTN_CROSSFADE_COLOR (lignes 649-655)** :
  - Remplacer `_eventOverlay.colorB = _eventOverlay.colorA;` par `_eventOverlay.colorB = _colors[CSLOT_CONFIRM_OK];`.
  - Ajouter `_eventOverlay.fgPct = _fgArpStopMax;` juste avant le `break;`.
  - Conserver `_eventOverlay.params.crossfadeColor.periodMs = 800;`.

- [ ] **Step 7.4 — Édition `LedController.cpp` `renderPattern` PTN_CROSSFADE_COLOR (lignes 788-806)** :
  - Garder la sine interp + lerp inchangés.
  - Modifier la boucle setPixel finale : remplacer `setPixel(led, mixed, inst.fgPct);` par le bloc avec `effectivePct` BG-aware.

- [ ] **Step 7.5 — Compile gate** : `pio run`, exit 0.

- [ ] **Step 7.6 — Static read-back** :
  - Grep `EVT_WAITING\|PTN_CROSSFADE_COLOR` dans `src/` : confirmer que les seuls sites d'usage sont (a) `LedGrammar.cpp` table, (b) `LedController.cpp` triggerEvent + renderPattern + isPatternExpired (ligne 712-714 — vérifier qu'aucun changement n'est requis là, le pattern reste "never expires autonomously").
  - Confirmer absence de callsite `triggerEvent(EVT_WAITING, ...)` dans le code (Phase 1 n'émet pas WAITING ; Phase 2 LoopEngine sera le 1er émetteur).

- [ ] **Step 7.7 — Mise à jour briefing** :
  - [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) §5 P5 (Event overlay LED grammar) : ajouter une note "EVT_WAITING : mode-invariant Q3 §28 — colorA verb green (editable), colorB white hardcoded, brightness fgArpStopMax × bgFactor BG".

- [ ] **Step 7.8 — Mise à jour LED spec §17 et §21 spec LOOP** (sync requirement) :
  - LED spec §17 lignes WAITING_* : reformuler pour expliciter que colorA = `CSLOT_VERB_PLAY` (pas `CSLOT_MODE_*`), colorB = `CSLOT_CONFIRM_OK`, brightness = `_fgArpStopMax`. Le tableau §17 existant peut rester si on note "Q3 §28 redefines : mode-invariant rendering".
  - Spec LOOP §21 ligne `WAITING_*` : confirmer que la formulation actuelle (colorA verb PLAY éditable, colorB white hardcodé) correspond — c'est déjà le cas dans le commit `bf26e31`. Vérifier read-back.

- [ ] **Step 7.9 — Commit (sur autorisation)** : `feat(led): phase 1 step 7 — EVT_WAITING mode-invariant (colorA green, colorB white, BG-aware brightness)`. Body cite §28 Q3 + audit F2.2 + LED spec §17.

---

## §4 — Pré-merge Phase 1 → Phase 2 — Checklist durs

À cocher avant déclaration "Phase 1 complete, ready for Phase 2 LoopEngine implementation".

### Build & static

- [ ] `pio run -e esp32-s3-devkitc-1` exit 0, no new warnings (`-Wswitch`, `-Wunused-variable`, `-Wparentheses` checked clean).
- [ ] Tous les `static_assert` passent : `sizeof(LoopPadStore) == 23`, `sizeof(LoopPadStore) <= 128`, `sizeof(LoopPotStore) <= 128`, `sizeof(LedSettingsStore) <= 128` (rename ne change pas la taille).
- [ ] Grep des symboles obsolètes :
  - `grep -rn "fgArpPlayMax\|_fgArpPlayMax" src/` → 0 résultat.
  - `grep -rn "LINE_ARPEG_FG_PCT\|LINE_LOOP_FG_PCT" src/` → 0 résultat.
- [ ] Grep des nouveaux symboles présents :
  - `grep -rn "BANK_LOOP" src/` → présent dans enum + validators + dispatch.
  - `grep -rn "LoopPadStore\|LoopPotStore" src/` → présent dans `KeyboardData.h` (et nulle part ailleurs côté `src/` Phase 1).
  - `grep -rn "fgPlayMax\|_fgPlayMax" src/` → tous les ex-callsites de `fgArpPlayMax` réécrits.
  - `grep -rn "LINE_TRANSPORT_FG_PCT" src/` → présent dans tous les dispatchs Tool 8 + dans l'enum.

### NVS

- [ ] Aucun bump de version sur Stores existants. `LedSettingsStore` reste v7. `BankTypeStore` reste v2. Tous les autres Stores inchangés.
- [ ] 2 nouvelles entrées descriptors : `LoopPadStore` (~23B, magic 0xBEEF, v1, ns "illpad_lpad", key "pads"). `LoopPotStore` per-bank n'apparaît pas dans `NVS_DESCRIPTORS[]` (pattern multi-key, comme `ArpPotStore`).
- [ ] Premier boot post-flash : `Serial.printf` warnings attendus :
  - "BankTypeStore absent/invalide — defaults usine appliqués" si NVS vierge (déjà le cas si reset hardware).
  - **Aucun warning supplémentaire** lié à `LedSettingsStore` ou `ColorSlotStore` (rename binary-compat).
  - **Aucun warning** pour `LoopPadStore`/`LoopPotStore` non plus (jamais lus en Phase 1).

### Runtime regression

(à valider sur HW lors d'un upload manuel autorisé par le user — pas dans ce plan)

- [ ] Boot LED progressive (8 steps) inchangé.
- [ ] NORMAL bank : pad press → noteOn ; pad release → noteOff ; AT functional ; pitch bend functional via pot.
- [ ] ARPEG bank : Hold pad → play/stop, tick FLASH vert, scale change root/mode/chrom → confirm BLINK_FAST + scale group propagation.
- [ ] Bank switch NORMAL ↔ ARPEG : confirm BLINK_SLOW white sur destination LED, allNotesOff sur source NORMAL, PB reset/restore correct.
- [ ] Setup mode entry : Tool 5 affiche les bank types correctement (LOOP impossible à créer en Phase 1 mais affichage défensif "LOOP" si déjà en NVS).
- [ ] Setup mode Tool 8 : section TRANSPORT contient la nouvelle ligne "FG brightness" entre Breathing et Tick common ; sections ARPEG et LOOP n'ont plus de ligne FG_PCT ; édition de la nouvelle ligne fait varier l'intensité ARPEG playing en sortie de setup.
- [ ] Battery gauge / setup comet / bargraph pots : inchangés.

### Documentation sync

- [ ] [`docs/reference/architecture-briefing.md`](../../reference/architecture-briefing.md) §4 Table 1 + §8 Domain Entry Points + §9 Invariants (ajout invariant 11) + §5 P5 (note WAITING mode-invariant) à jour.
- [ ] [`docs/reference/nvs-reference.md`](../../reference/nvs-reference.md) : `LoopPadStore` ligne 107 → "DECLARED Phase 1" ; `illpad_lpot` ligne 118 → "DECLARED Phase 1, consumed Phase 5".
- [ ] [`docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md`](../specs/2026-04-19-led-feedback-unified-design.md) §17 : note Q3 §28 reformulant WAITING en mode-invariant.
- [ ] [`docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md`](../specs/2026-04-20-tool8-ux-respec-design.md) §4.1 + §9 : "FG brightness" déplacé en TRANSPORT, ARPEG/LOOP n'exposent plus que leur color de base.

### Hors scope explicite (pour information — Phase 2+)

- [ ] LoopEngine (state machine EMPTY/REC/PLAY/OD/STOP + WAITING_*) → Phase 2.
- [ ] `processLoopMode` dispatch + `renderBankLoop` complet (tick FLASH wrap/bar/rec/od) → Phase 2.
- [ ] `consumeBarFlash`/`consumeWrapFlash` flags câblés depuis LoopEngine → Phase 2.
- [ ] Tool 3 b1 refactor (sous-pages Banks/ARPEG/LOOP, slots 16 pads) → Phase 3.
- [ ] Tool 5 3-way cycle NORMAL ↔ ARPEG ↔ LOOP + `loopQuantize` per-bank → Phase 3.
- [ ] Tool 7 3 contextes (NORMAL/ARPEG/LOOP, +8 slots) → Phase 3-4.
- [ ] Tool 4 extension (refus ControlPad sur LOOP control pads) → Phase 3.
- [ ] PotMappingStore extension 3 contextes → Phase 3-4.
- [ ] Effets shuffle/chaos/velocity patterns câblés → Phase 5.
- [ ] Slot Drive LittleFS + 16 slots persistence → Phase 6.
- [ ] Audit F2.5 (Tool 8 respec §6.6 stale `renderPreviewPattern` signature) — non-bloquant, à traiter en tâche séparée si jugé utile.

---

## §5 — Notes de risques et zones de friction

| Zone | Risque | Mitigation Phase 1 |
|---|---|---|
| `static_assert(sizeof(LoopPadStore) == 23)` | ESP32 alignement par défaut → struct = 24 B avec un padding | `__attribute__((packed))` requis. Si compile fail, c'est attendu — corriger au step 2.6. |
| Rename `fgArpPlayMax` callsite oublié | Compile error explicite, mais cherche sur les 4 fichiers connus + ToolLedSettings cpp pas évident | Step 5.8 grep stricte = filet de sécurité. |
| `sectionOf()` threshold update | Bug navigation : cursor saute dans la mauvaise section | Step 6.5 + test HW step 6.17 valide la nav par section. |
| BANK_LOOP en NVS qui surprend Tool 5 actuel | Tool 5 affiche "LOOP" mais cycle reste NORMAL ↔ ARPEG | Step 1.5 ajoute label LOOP ; Phase 3 ajoutera 3-way cycle. Comportement défensif acceptable. |
| `previewContextForLine` pour `LINE_TRANSPORT_FG_PCT` | Si helper ne supporte pas multi-LED, fallback dégradé | Step 6.2 décide à la lecture du helper ; fallback `PV_BASE_COLOR` est acceptable. |
| HW upload non testé en Phase 1 (sauf step 6.17 si user autorise) | Régression possible non détectée | Compile + read-back + grep static = filet substantiel ; user autorise upload aux moments choisis. |

---

**Plan validé pour Phase 1.** Aucune feature visible musicien. 7 commits courts. Toutes les décisions §28 respectées. Phase 2 (LoopEngine + processLoopMode) peut démarrer dès la complétion de cette checklist pré-merge.
