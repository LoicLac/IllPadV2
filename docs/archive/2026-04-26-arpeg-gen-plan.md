# ILLPAD48 V2 — ARPEG_GEN — Plan d'implémentation

> **For agentic workers :** REQUIRED SUB-SKILL : Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Date** : 2026-04-26.
**Spec source** : [`docs/superpowers/specs/2026-04-25-arpeg-gen-design.md`](../specs/2026-04-25-arpeg-gen-design.md) (révisée 2026-04-26, 689 lignes).
**Audit Phase 1** : conversation 2026-04-26, 5 bloquants/importants tranchés (cf §0).

**Goal** : implémenter le 3e type de bank `BANK_ARPEG_GEN` (arpégiateur génératif à pile vivante, 15 positions de grille, pile vivante, walk pondéré, mutation gouvernée par pad oct, lock implicite). Réduire `ARPEG` classique de 15 → 6 patterns, restaurer les octaves littérales. Aucune régression musicien-facing sur les modes existants. Qualité finale, fiabilité non négociable (cf [`.claude/CLAUDE.md`](../../../.claude/CLAUDE.md) : objet unique vendu, pas prototype).

**Architecture** : 8 phases séquentielles, ~21 tasks. Chaque phase produit un état firmware compilable + testable hardware. Les phases 2-3 sont préparatoires (compile only, aucun changement musical) ; les phases 4+ produisent du comportement nouveau. **HW checkpoint mandatory AVANT chaque commit majeur** (cf mémoire utilisateur `feedback_exec_plan_order.md`). Auto-commit autorisé en cours de phase pour les commits intermédiaires de plumbing, mais HW checkpoint obligatoire à la fin de chaque phase.

**Tech Stack** : C++17 / PlatformIO / ESP32-S3-N8R16 / FreeRTOS dual-core / Arduino framework. Pas de unit tests automatisés — vérification = `pio run` (compile gate) + flash manuel sur autorisation explicite + read-back du diff + run live (HW gate). NVS Zero Migration Policy ([`docs/reference/nvs-reference.md`](../../reference/nvs-reference.md), [`CLAUDE.md`](../../../.claude/CLAUDE.md)).

**Sources de référence :**
- Spec ARPEG_GEN : [`docs/superpowers/specs/2026-04-25-arpeg-gen-design.md`](../specs/2026-04-25-arpeg-gen-design.md) — toutes sections §1-§41.
- Audit Phase 1 (cette session) : 5 décisions tranchées (cf §0).
- Refs runtime : [`docs/reference/arp-reference.md`](../../reference/arp-reference.md) (à mettre à jour Phase 8), [`docs/reference/runtime-flows.md`](../../reference/runtime-flows.md), [`docs/reference/patterns-catalog.md`](../../reference/patterns-catalog.md) (P1 refcount, P3 event queue, P4 catch).
- NVS : [`docs/reference/nvs-reference.md`](../../reference/nvs-reference.md), pattern P6.
- Pot : [`docs/reference/pot-reference.md`](../../reference/pot-reference.md).
- Tools : [`docs/reference/setup-tools-conventions.md`](../../reference/setup-tools-conventions.md) §4.3, §4.4.
- VT100 : [`docs/reference/vt100-design-guide.md`](../../reference/vt100-design-guide.md).
- Plan modèle : [`docs/superpowers/plans/2026-04-21-loop-phase-1-plan.md`](2026-04-21-loop-phase-1-plan.md) (format, structure, niveau de détail attendu).
- CLAUDE.md projet : NVS Zero Migration Policy, Performance Budget Philosophy, Invariants 1-7.
- CLAUDE.md user : git workflow (autocommit ON par défaut, push explicite uniquement), actions destructives, lire-avant-de-proposer.

**Plan LOOP concurrent (réservation enum)** : [`docs/superpowers/plans/2026-04-21-loop-phase-1-plan.md`](2026-04-21-loop-phase-1-plan.md) — la valeur `BANK_LOOP = 2` est réservée par ce plan, donc `BANK_ARPEG_GEN = 3` (cf §0 D1).

**Hypothèse d'ordre d'implémentation** : ce plan ARPEG_GEN s'exécute **avant** LOOP Phase 1. Conséquences :
- L'enum `BankType` introduit `BANK_LOOP = 2` réservé (placeholder, pas d'impl) en même temps que `BANK_ARPEG_GEN = 3`. LOOP Phase 1 trouvera la valeur déjà déclarée et n'aura qu'à wirer ses guards.
- Tous les sites code modifiés par ce plan prévoient explicitement la cohabitation avec LOOP : switches 4-way (NORMAL/ARPEG/ARPEG_GEN/LOOP) avec branches LOOP en `default: break;` ou stub commenté `// LOOP : Phase 1+ ailleurs`. Le plan LOOP Phase 1 pourra alors juste compléter les branches LOOP sans toucher aux structures.
- Voir §1.5 « Cohabitation LOOP » ci-dessous pour la liste exhaustive des 5 sites concernés.

---

## §0 — Décisions pré-actées

Ces 5 décisions ont été tranchées pendant l'audit Phase 1 (session 2026-04-26). **Ne pas les rebattre.**

| # | Sujet | Décision | Impact |
|---|---|---|---|
| D1 | Valeur enum `BANK_ARPEG_GEN` | `= 3`. `BANK_LOOP = 2` reste réservé pour le plan LOOP Phase 1 (cohérent avec `CSLOT_MODE_LOOP = 2` déjà en place). | Task 1 (KeyboardData.h enum). |
| D2 | `ArpPotStore` post-réduction patterns 15 → 6 | Bump : ajout `magic + version + reserved` (struct passe de 8 → 12 octets), nouvelle constante `ARPPOT_VERSION = 1`. NVS reset au boot post-update (warning `Serial.printf`). User re-règle gate/shuffle/division/oct/template par bank ARPEG. | Task 3. |
| D3 | Pot routing `TARGET_GEN_POSITION` | Two-binding strategy : `rebuildBindings` génère 2 entrées par slot pot pour le contexte ARPEG, l'une avec `bankType=BANK_ARPEG → TARGET_PATTERN`, l'autre avec `bankType=BANK_ARPEG_GEN → TARGET_GEN_POSITION`. `TARGET_GEN_POSITION` n'est pas exposé dans le pool Tool 7 (replacement runtime de `TARGET_PATTERN`, pas une cible mappable séparée). « Un seul caractère d'arp » côté musicien. | Tasks 13, 15. |
| D4 | Tool 5 type switch | Pas de prompt « Restart now? » : la sortie de setup mode est intrinsèquement liée au reboot. Toute modification Tool 5 prend effet au reboot suivant. **Pas de runtime pool reassignment, pas de doc spécifique à ajouter.** | Hors-scope hard ; documenté implicitement par §0 ici. |
| D5 | Tool 5 edit ARPEG_GEN navigation | Cycle linéaire 4-champs : `MARGIN → TYPE → GROUP → BONUS → MARGIN` via ←→. ↑↓ ajuste valeur du champ focalisé. Exception §4.4 documentée dans le code (commentaire en tête du handler). | Task 16. |

---

## §1 — File structure overview

Fichiers touchés, par responsabilité. **Aucun nouveau fichier créé** — toutes les additions vivent dans des fichiers existants.

| Fichier | Tasks | Rôle |
|---|---|---|
| [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h) | 1, 2, 3, 4 | Enum `BankType`, `ArpPattern`, `BankTypeStore` v3, `ArpPotStore` v1 bump, `validateBankTypeStore`, `validateArpPotStore` (nouveau), constantes ARPEG_GEN |
| [`src/managers/NvsManager.h`](../../../src/managers/NvsManager.h) + `.cpp` | 2, 3, 6 | Boot defaults + load path `BankTypeStore` v3, `ArpPotStore` v1 (load+save), getter `getLoadedBonusPile`, `getLoadedMarginWalk`, queueArpPotWrite ARPEG+ARPEG_GEN |
| [`src/arp/ArpEngine.h`](../../../src/arp/ArpEngine.h) + `.cpp` | 4, 6, 8, 9, 10, 11, 12, 18 | Réduction patterns 15→6, suppression `effectiveOctaveRange`, `_engineMode`, `setEngineMode`, `_sequenceGen`, `_pileDegrees`, `setGenPosition`, `setMutationLevel`, helpers degree↔MIDI, walk + bornes, generation initiale, mutation, branching tick |
| [`src/midi/ScaleResolver.h`](../../../src/midi/ScaleResolver.h) + `.cpp` | 8 | Helper `degreeToMidi(degree, scale)` + `padIndexToDegree(padIdx, padOrder, scale)` |
| [`src/managers/PotRouter.h`](../../../src/managers/PotRouter.h) + `.cpp` | 13, 14, 15 | `TARGET_GEN_POSITION`, two-binding strategy, hystérésis (TARGET_GEN_POSITION uniquement), `_genPosition`, getter, `loadStoredPerBank` extension, `getRangeForTarget`, `seedCatchValues`, `applyBinding`, `isPerBankTarget`, `getDiscreteSteps` |
| [`src/main.cpp`](../../../src/main.cpp) | 5, 6, 15 | `isArpType()` cascade (boot engine assignment, hold pad, panic, pushParamsToEngine, reloadPerBankParams, handlePadInput, handleLeftReleaseCleanup, scale change propagation), branching pushParamsToEngine ARPEG vs ARPEG_GEN |
| [`src/managers/BankManager.cpp`](../../../src/managers/BankManager.cpp) | 5 | Double-tap Play/Stop : `isArpType` au lieu de `BANK_ARPEG` strict (line 85, 207) |
| [`src/managers/ScaleManager.cpp`](../../../src/managers/ScaleManager.cpp) | 19 | Octave pads : `setMutationLevel` pour ARPEG_GEN, `setOctaveRange` pour ARPEG (branching via `isArpType`+ check `_engineMode`) |
| [`src/core/LedController.cpp`](../../../src/core/LedController.cpp) | 5, 20 | `case BANK_ARPEG_GEN:` route vers `renderBankArpeg` (line 502), bargraph color `isArpType` (line 351) |
| [`src/setup/ToolBankConfig.cpp`](../../../src/setup/ToolBankConfig.cpp) | 7, 16, 17 | Cycle 5 états Tool 5, layout 1-ligne minimal puis 2-lignes complet, edit field-focus 4-champs ARPEG_GEN, INFO panel ARPEG_GEN, control bar variants, reset `[d]` defaults bonusPilex10/marginWalk = 15/7 |
| [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp) | 5 | Preview color slot (line 940) : `isArpType` au lieu de `BANK_ARPEG` strict |
| [`docs/reference/arp-reference.md`](../../reference/arp-reference.md) | 21 | §6 patterns 15→6, ajout §13 « Generative mode » (architecture, walk, mutation, bornes, triggers reset), correction §4 (`ArpStartMode::Bar` n'existe pas) |
| [`docs/reference/patterns-catalog.md`](../../reference/patterns-catalog.md) | 21 | Note P1/P3 que ARPEG_GEN partage refcount + event queue avec classique |
| [`docs/reference/nvs-reference.md`](../../reference/nvs-reference.md) | 21 | `BankTypeStore` v3 (44B), `ArpPotStore` v1 (12B), validateArpPotStore |

**Pas touché :**
- `CapacitiveKeyboard.{h,cpp}` (DO NOT MODIFY).
- `HardwareConfig.h` pressure tuning constants (DO NOT MODIFY).
- `platformio.ini`.
- `MidiTransport.cpp`, `MidiEngine.cpp` : ARPEG_GEN sort par les mêmes paths que ARPEG (refcountNoteOn/Off via P1).
- `ArpScheduler.{h,cpp}` : type-agnostic, dispatche tout engine enregistré (cf [`ArpScheduler.cpp:98-131`](../../../src/arp/ArpScheduler.cpp)).
- `ClockManager.{h,cpp}` : aucun changement timing requis (spec §29).
- `ItermCode/vt100_serial_terminal.py` : la spec §22 ne change pas le rendu base, mais Phase 7 vérifiera si l'ajout d'une 2e ligne par bank passe le filtre.

### §1.5 — Cohabitation LOOP (5 sites de collision pré-traités)

L'audit Phase 1 a identifié 5 sites où l'implémentation ARPEG_GEN et le plan LOOP Phase 1 (untracked) modifient le même code. **Ce plan ARPEG_GEN intègre dès maintenant la branche LOOP correcte** (en stub `default: break;` ou commentaire `// LOOP : Phase 1`) pour que le plan LOOP Phase 1 n'ait plus qu'à compléter le contenu sans casser le travail ARPEG_GEN.

| # | Site | Forme intégrée par ARPEG_GEN | Action du plan LOOP Phase 1 |
|---|---|---|---|
| C1 | [`main.cpp:580-587`](../../../src/main.cpp:580-587) `handlePadInput` switch | `case BANK_NORMAL` + `case BANK_ARPEG` + `case BANK_ARPEG_GEN` + `default: break;` (couvre LOOP silencieux) | Aucune. Le `default` est déjà en place. Phase 2 LOOP remplacera le `default` par un `case BANK_LOOP: processLoopMode(...)`. |
| C2 | [`BankManager.cpp:85`](../../../src/managers/BankManager.cpp:85) double-tap | `if (wasRecent && isArpType(...))  { /* arp Play/Stop */ }` (LOOP NON traité ici) | Phase 1 LOOP ajoute `else if (wasRecent && type == BANK_LOOP) { silent consume }` après le `if` ARPEG_GEN. |
| C3 | [`BankManager.cpp:207`](../../../src/managers/BankManager.cpp:207) debug print | Switch 4-way `(NORMAL/ARPEG/ARPEG_GEN/LOOP)` avec LOOP retournant `"LOOP"` (placeholder, jamais sélectionnable Phase 4 ARPEG_GEN mais cas couvert) | Aucune. Branche LOOP déjà présente. |
| C4 | [`LedController.cpp:500-503`](../../../src/core/LedController.cpp:500-503) switch render | `case BANK_NORMAL` + `case BANK_ARPEG` + `case BANK_ARPEG_GEN: renderBankArpeg(...)` + `case BANK_LOOP: /* Phase 1 LOOP wires renderBankLoop stub */ break;` | Phase 1 LOOP remplace le `break;` du `case BANK_LOOP` par l'appel `renderBankLoop(i, isFg, now)`. |
| C5 | [`LedController.cpp:351`](../../../src/core/LedController.cpp:351) bargraph color | Switch 3-way **explicite** : `isArpType → CSLOT_MODE_ARPEG`, `BANK_LOOP → CSLOT_MODE_LOOP`, `else → CSLOT_MODE_NORMAL`. **Site oublié par le plan LOOP**, ARPEG_GEN le traite proactivement. | Aucune. La couleur LOOP du bargraph est déjà câblée. |

**Conséquence** : après merge ARPEG_GEN complet, le plan LOOP Phase 1 doit être amendé pour retirer ses Steps 1.6 (LedController switch render dispatch — déjà fait), 4.2 partiel (BankManager double-tap — squelette en place, juste compléter `else if`), et 1.5 (ToolBankConfig 4-way label — déjà 3-way, ajouter LOOP). Patch séparé sur le plan LOOP en bas de ce document (§annexe).

---

## §2 — Dépendances inter-tasks (graph)

```
Task 1 (BankType + isArpType helper)
   │
   ├──► Task 2 (BankTypeStore v3) ───► Task 5 (isArpType cascade)
   │                                       │
   │                                       └──► Task 6 (_engineMode + boot assignment)
   │                                              │
   ├──► Task 3 (ArpPotStore v1 bump)              │
   │                                              │
   ├──► Task 4 (ArpPattern 15→6 + suppr effectiveOctaveRange)
   │       │
   │       └──► Task 5 (ARPEG classique restoration finale)
   │
   └──► Task 6 ──► Task 7 (Tool 5 cycle 5 états + 1-ligne minimal)
                       │
                       └──► HW checkpoint Phase 4 = ARPEG_GEN sélectionnable, joue silencieusement (engine pas encore en GENERATIVE)

Task 8 (ScaleResolver helpers + buffers _sequenceGen/_pileDegrees + setEngineMode payload)
   │
   ├──► Task 9 (Walk + bornes §12 §16)
   │       │
   │       └──► Task 10 (Generation initiale §14 + reset triggers §17)
   │
   └──► Task 11 (Mutation §15 + step counter mutation)
              │
              └──► Task 12 (tick branching CLASSIC vs GENERATIVE)
                       │
                       └──► HW checkpoint Phase 5 = ARPEG_GEN joue (premier vrai test musical)

Task 13 (PotRouter TARGET_GEN_POSITION + 6 fonctions étendues)
   │
   ├──► Task 14 (Hystérésis TARGET_GEN_POSITION)
   │
   └──► Task 15 (pushParamsToEngine + reloadPerBankParams branching ARPEG vs ARPEG_GEN)
              │
              └──► HW checkpoint Phase 6 = R2+hold balaye 15 zones, hystérésis OK, bank switch reseed catch

Task 16 (Tool 5 edit field-focus 4-champs ARPEG_GEN, layout 2-lignes §22 §23)
   │
   └──► Task 17 (INFO panel §25 + control bar §26)
              │
              └──► Task 18 (Pot move at lock §19 + pad oct change §20 — tests integration)
                       │
                       └──► HW checkpoint Phase 7 = Tool 5 ARPEG_GEN flow complet, persistance reboot

Task 19 (ScaleManager octave pads → setMutationLevel pour ARPEG_GEN)
   │
   └──► Task 20 (LED finitions §32 — bargraph + render dispatch)
              │
              └──► Task 21 (docs update arp-reference + nvs-reference + patterns-catalog)
                       │
                       └──► HW checkpoint final Phase 8 = audit invariants live (orphan notes, refcount, paused pile, bank switch silence, FG/BG continuity)
```

Ordre des commits = ordre des tasks (1 → 21). Chaque task est atomique sur compile + lint + read-back. Aucune task n'introduit de régression musicien-facing sur les modes existants (NORMAL + ARPEG classique).

---

## §3 — Conventions de vérification (firmware-spécifique)

Pas de framework de tests automatisés. Pour chaque task :

1. **Compile gate** : `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1` → exit 0, no new warning.
2. **Static read-back** : grep des symboles modifiés dans tous les fichiers consommateurs ; vérifier qu'aucun call-site n'a été oublié.
3. **HW gate (regression check)** — déclenché manuellement par le user, **jamais automatiquement** (cf [`CLAUDE.md`](../../../.claude/CLAUDE.md) projet + mémoire `feedback_no_auto_upload`) :
   - Boot OK, 8 progressive LEDs, scenario nominal selon la phase.
   - Bank switch ↔ inchangé visuellement et MIDI pour les modes existants.
   - Aucun stuck note (vérifier qu'un pad release laisse le MIDI propre).
   - Run live ≥ 30 s sur la feature ajoutée.
4. **Commit gate** : autocommit ON par défaut (cf [`CLAUDE.md`](../../../.claude/CLAUDE.md) user). Commit auto-déclenché à chaque task complet **après HW checkpoint** par phase. Pas de `git add -A`. Pas de `--no-verify`. `git push` reste explicite par le user.

**HW checkpoint AVANT commit, toujours.** Cf mémoire utilisateur `feedback_exec_plan_order.md`.

---

# Phase 2 — Plumbing enum + NVS sans engine logic

**Objectif** : firmware compile, NVS structures en place, enum étendu. **Aucun comportement musicien-facing nouveau.** Toutes les banks ARPEG_GEN seront créables dans Tool 5 mais traités comme des shells silencieux jusqu'à Phase 4+.

**HW checkpoint Phase 2** : boot OK, NVS reset attendu (warnings Serial pour `BankTypeStore` v2→v3 et `ArpPotStore` raw→v1), Tools 1-4/6/7/8 OK, MIDI normal/arpeg classic identique. Tool 5 affiche encore le cycle 3-états ancien (Phase 4 le passe à 5).

---

## Task 1 — Extension `BankType` enum + helper `isArpType`

**Cross-refs** : spec §4 « Trois types de bank, deux moteurs ARPEG » + §0 D1 + audit Phase 1 §1 B-1.

**Files :**
- Modify : [`src/core/KeyboardData.h:312-316`](../../../src/core/KeyboardData.h:312-316) — extension enum `BankType`.

**Cible enum `BankType`** :

```cpp
enum BankType : uint8_t {
  BANK_NORMAL    = 0,
  BANK_ARPEG     = 1,
  BANK_LOOP      = 2,   // RESERVED : LOOP Phase 1 (not yet implemented)
  BANK_ARPEG_GEN = 3,   // ARPEG génératif (this plan)
  BANK_ANY       = 0xFF // Used in PotRouter bindings (matches any type)
};
```

**Helper inline** (sous l'enum, dans le même header) :

```cpp
// Helper : true si la bank utilise un ArpEngine (ARPEG classique OU ARPEG_GEN).
// Tous les call-sites qui filtraient sur BANK_ARPEG strict doivent passer par ce helper.
inline bool isArpType(BankType t) {
  return t == BANK_ARPEG || t == BANK_ARPEG_GEN;
}
```

**Steps :**
- [ ] Étendre l'enum à `BANK_LOOP = 2` (réservé, sans implémentation) et `BANK_ARPEG_GEN = 3`.
- [ ] Ajouter `inline bool isArpType(BankType t)` immédiatement sous l'enum, accompagné du commentaire.
- [ ] Compile gate : `pio run`.

**Verification :**
- `grep -n "BANK_LOOP" src/` → uniquement la définition (aucun call-site encore).
- `grep -n "BANK_ARPEG_GEN" src/` → uniquement la définition.
- Compile : 0 erreur, 0 nouveau warning.

---

## Task 2 — `BankTypeStore` v3 + `validateBankTypeStore` extension

**Cross-refs** : spec §27 « BankTypeStore v3 », §21 (champs et conventions), §0 D1, NVS Zero Migration Policy.

**Files :**
- Modify : [`src/core/KeyboardData.h:486-499`](../../../src/core/KeyboardData.h:486-499) — bump `BANKTYPE_VERSION` de 2 à 3, ajout 2 champs.
- Modify : [`src/core/KeyboardData.h:610-619`](../../../src/core/KeyboardData.h:610-619) — `validateBankTypeStore` : étendre clamp types, accepter `BANK_ARPEG_GEN`, comptage arpCount cumulé `BANK_ARPEG + BANK_ARPEG_GEN`, clamp `bonusPilex10[]` et `marginWalk[]`.
- Modify : [`src/managers/NvsManager.cpp:618-621`](../../../src/managers/NvsManager.cpp:618-621) — boot defaults : ne pas changer la répartition (banks 1-4 NORMAL, 5-8 ARPEG, identique aujourd'hui), mais initialiser `_loadedBonusPile[i] = 15`, `_loadedMarginWalk[i] = 7` pour TOUTES les banks.
- Modify : [`src/managers/NvsManager.h`](../../../src/managers/NvsManager.h) — déclarer `_loadedBonusPile[NUM_BANKS]`, `_loadedMarginWalk[NUM_BANKS]`, getters `getLoadedBonusPile(uint8_t bank) const`, `getLoadedMarginWalk(uint8_t bank) const`, setters `setLoadedBonusPile`, `setLoadedMarginWalk`.

**Cible struct (KeyboardData.h:486-499)** :

```cpp
#define BANKTYPE_NVS_KEY_V2  "config"
#define BANKTYPE_VERSION     3   // 2->3 : ajout bonusPilex10[] + marginWalk[] (ARPEG_GEN per-bank)

struct BankTypeStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  uint8_t  types[NUM_BANKS];          // BankType enum cast (0..3)
  uint8_t  quantize[NUM_BANKS];       // ArpStartMode enum
  uint8_t  scaleGroup[NUM_BANKS];     // 0 = none, 1..NUM_SCALE_GROUPS = A..D
  uint8_t  bonusPilex10[NUM_BANKS];   // V3 : 10..20 (bonus_pile × 10), defaults 15. Used only when type == BANK_ARPEG_GEN.
  uint8_t  marginWalk[NUM_BANKS];     // V3 : 3..12 (degrés). Defaults 7. Used only when type == BANK_ARPEG_GEN.
};
static_assert(sizeof(BankTypeStore) <= NVS_BLOB_MAX_SIZE, "BankTypeStore exceeds NVS blob max");
// Total v3 : 4 (header) + 8 × 5 = 44 octets. Confortable sous le cap NVS_BLOB_MAX_SIZE = 128.
```

**Cible validate** :

```cpp
inline void validateBankTypeStore(BankTypeStore& s) {
  uint8_t arpCount = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    // Type clamp : tout > BANK_ARPEG_GEN (sauf BANK_ANY=0xFF) -> NORMAL
    if (s.types[i] > BANK_ARPEG_GEN && s.types[i] != BANK_ANY) s.types[i] = BANK_NORMAL;
    if (isArpType((BankType)s.types[i])) arpCount++;
    if (arpCount > MAX_ARP_BANKS) s.types[i] = BANK_NORMAL;
    if (s.quantize[i] >= NUM_ARP_START_MODES) s.quantize[i] = DEFAULT_ARP_START_MODE;
    if (s.scaleGroup[i] > NUM_SCALE_GROUPS) s.scaleGroup[i] = 0;
    // V3 : nouveaux champs (clamp aux ranges déclarés en spec §21)
    if (s.bonusPilex10[i] < 10 || s.bonusPilex10[i] > 20) s.bonusPilex10[i] = 15;
    if (s.marginWalk[i]  < 3  || s.marginWalk[i]  > 12) s.marginWalk[i]  = 7;
  }
}
```

**Steps :**
- [ ] Bump `BANKTYPE_VERSION` 2 → 3.
- [ ] Ajouter `bonusPilex10[NUM_BANKS]` et `marginWalk[NUM_BANKS]` à la fin de `BankTypeStore` (après `scaleGroup`).
- [ ] Vérifier `static_assert(sizeof(BankTypeStore) <= NVS_BLOB_MAX_SIZE)` toujours valide (44 ≤ 128).
- [ ] Étendre `validateBankTypeStore` selon le code cible.
- [ ] Déclarer `_loadedBonusPile[NUM_BANKS]` + `_loadedMarginWalk[NUM_BANKS]` dans `NvsManager.h` (private), avec getters/setters publics.
- [ ] Initialiser ces arrays dans le constructor `NvsManager` (à 15 et 7).
- [ ] Étendre la load path `loadAll()` ([`NvsManager.cpp:600-625`](../../../src/managers/NvsManager.cpp)) pour copier `bts.bonusPilex10[i]` et `bts.marginWalk[i]` dans `_loadedBonusPile[i]` / `_loadedMarginWalk[i]` après validate.
- [ ] Étendre la save path (Tool 5 → `saveConfig`) pour passer ces valeurs (Phase 7 finalisera la signature, en Phase 2 utiliser les defaults).
- [ ] Compile gate.

**Verification :**
- `static_assert` passe au compile.
- Boot avec NVS v2 existante : `loadBlob` échoue (size mismatch v2=28 vs v3=44), warning Serial, defaults compile-time appliqués (banks 1-4 NORMAL, 5-8 ARPEG, bonusPilex10=15, marginWalk=7). User re-règle.
- HW checkpoint partiel : boot OK, premier boot post-update affiche le warning, navigation Tool 5 inchangée (cycle 3-états toujours en place jusqu'en Phase 4).

---

## Task 3 — `ArpPotStore` v1 bump (header magic+version+reserved)

**Cross-refs** : spec §28 « Pas d'autre Store affecté » + §0 D2 + audit Phase 1 §1 B-2.

**La spec §28 dit explicitement « ArpPotStore reste inchangé », mais l'audit Phase 1 a démontré que la réduction patterns 15→6 invalide les valeurs `pattern ∈ [6,14]` stockées en NVS. Bump nécessaire.**

**Files :**
- Modify : [`src/core/KeyboardData.h:117-124`](../../../src/core/KeyboardData.h:117-124) — ajout header magic+version+reserved, struct passe de 8 à 12 octets.
- Modify : [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h) — ajouter `validateArpPotStore` inline.
- Modify : [`src/managers/NvsManager.cpp`](../../../src/managers/NvsManager.cpp) — load path `loadArpPotForBank` (ou équivalent) doit checker magic+version, fallback defaults compile-time si invalid + warning Serial.
- Modify : [`src/managers/NvsManager.cpp`](../../../src/managers/NvsManager.cpp) — save path doit écrire magic+version+reserved.

**Cible struct** :

```cpp
const uint16_t ARPPOT_MAGIC   = EEPROM_MAGIC;  // 0xBEEF
const uint8_t  ARPPOT_VERSION = 1;             // 1 = post-pattern-reduction (15->6)

struct ArpPotStore {
  uint16_t magic;             // ARPPOT_MAGIC
  uint8_t  version;           // ARPPOT_VERSION
  uint8_t  reserved;
  uint16_t gateRaw;           // gate × 4095 (range 0-32760, i.e. 0.0-8.0; floor 0.005 on load)
  uint16_t shuffleDepthRaw;   // 0-4095 (maps to 0.0-1.0)
  uint8_t  division;          // ArpDivision enum (0-8)
  uint8_t  pattern;           // ArpPattern enum (0-5 post-Task-4) OR position grille (0-14) via interpretation runtime
  uint8_t  octaveRange;       // 1-4 (semantically = mutationLevel for ARPEG_GEN)
  uint8_t  shuffleTemplate;   // 0-9 (index into groove templates)
};
static_assert(sizeof(ArpPotStore) <= NVS_BLOB_MAX_SIZE, "ArpPotStore exceeds NVS blob max");
// Total : 4 (header) + 8 = 12 octets.
```

**Note d'interprétation `pattern`** : pour bank ARPEG, c'est l'index `ArpPattern` (0-5 post-Task-4). Pour bank ARPEG_GEN, c'est `_genPosition` (0-14, range distinct). L'engine décide selon `_engineMode`. Pas besoin d'un champ séparé en NVS — le sens est porté par `BankTypeStore.types[i]`. Documenter dans le commentaire de struct.

**Cible validate** :

```cpp
inline void validateArpPotStore(ArpPotStore& s) {
  // pattern range étendu pour couvrir les 2 sémantiques :
  // - ARPEG classique : 0..NUM_ARP_PATTERNS-1 (= 0..5 après Task 4)
  // - ARPEG_GEN     : 0..14 (15 positions de grille)
  // Le validate clampe au max des deux pour ne pas casser un pattern stocké pour une bank ARPEG_GEN.
  if (s.pattern > 14) s.pattern = 0;
  if (s.division >= NUM_ARP_DIVISIONS) s.division = DIV_1_8;
  if (s.octaveRange < 1 || s.octaveRange > 4) s.octaveRange = 1;
  if (s.shuffleTemplate >= NUM_SHUFFLE_TEMPLATES) s.shuffleTemplate = 0;
  // gateRaw / shuffleDepthRaw : pas de clamp strict (uint16 native), sera clampé à load par les getters float.
}
```

**Steps :**
- [ ] Refactorer `ArpPotStore` selon la cible.
- [ ] Ajouter `ARPPOT_MAGIC` et `ARPPOT_VERSION` constants.
- [ ] Ajouter `validateArpPotStore`.
- [ ] Trouver les call-sites NVS qui lisent/écrivent `ArpPotStore` ([`NvsManager.cpp`](../../../src/managers/NvsManager.cpp), grep `ARP_POT_NVS_NAMESPACE`) :
  - Load : si `prefs.getBytes(...)` retourne size != sizeof(ArpPotStore) ou magic/version invalide → fallback defaults + warning Serial.
  - Save : remplir magic+version+reserved avant `putBytes`.
- [ ] Vérifier que les defaults compile-time injectés à boot (cf [`NvsManager.cpp` constructor](../../../src/managers/NvsManager.cpp:31)) initialisent `_pendingArpPot[i].magic = ARPPOT_MAGIC`, `version = ARPPOT_VERSION`, `pattern = 0` (ARP_ORDER post-Task-4), `octaveRange = 1`, etc.
- [ ] Compile gate.

**Verification :**
- `grep -rn "ArpPotStore\b" src/` → tous les sites identifiés et patchés.
- Boot avec NVS v0 (raw) existante : load échoue silencieusement (size 8 ≠ 12 OR magic absent), warning Serial, defaults appliqués. ARPEG banks démarrent en `ARP_ORDER` (pattern=0), gate=0.5, division=DIV_1_8, etc.
- HW checkpoint partiel : Tools utilisent les valeurs default (visible dans Tool 7 / pots).

---

## Task 4 — Réduction `ArpPattern` enum 15 → 6 + suppression `effectiveOctaveRange`

**Cross-refs** : spec §6 « Six patterns conservés », §7 « Octaves littérales restaurées ».

**Files :**
- Modify : [`src/core/KeyboardData.h:407-424`](../../../src/core/KeyboardData.h:407-424) — réduire enum à 6 entrées.
- Modify : [`src/arp/ArpEngine.h`](../../../src/arp/ArpEngine.h) — supprimer déclaration `effectiveOctaveRange`.
- Modify : [`src/arp/ArpEngine.cpp:252-262`](../../../src/arp/ArpEngine.cpp:252-262) — supprimer définition `effectiveOctaveRange`.
- Modify : [`src/arp/ArpEngine.cpp:269-518`](../../../src/arp/ArpEngine.cpp:269-518) — `rebuildSequence` : supprimer cas obsolètes (`ARP_RANDOM`, `ARP_CASCADE`, `ARP_DIVERGE`, `ARP_UP_OCTAVE`, `ARP_DOWN_OCTAVE`, `ARP_CHORD`, `ARP_OCTAVE_WAVE`, `ARP_OCTAVE_BOUNCE`, `ARP_PROBABILITY`), remplacer `effectiveOctaveRange()` par `_octaveRange` direct.
- Modify : [`src/arp/ArpEngine.cpp:9-12`](../../../src/arp/ArpEngine.cpp:9-12) — supprimer arrays `FIB_BOUNCE` et `FIB_SPIRAL` (orphelins).
- Modify : [`src/arp/ArpEngine.cpp:579-587`](../../../src/arp/ArpEngine.cpp:579-587) — `executeStep` : supprimer le shuffle case `ARP_RANDOM` (ligne 579-587).

**Cible enum** :

```cpp
enum ArpPattern : uint8_t {
  ARP_UP             = 0,   // (kept) low → high
  ARP_DOWN           = 1,   // (kept) high → low
  ARP_UP_DOWN        = 2,   // (kept) UP puis indices descendants
  ARP_ORDER          = 3,   // (was 4) chronologique
  ARP_PEDAL_UP       = 4,   // (was 8) basse pédale + arpège
  ARP_CONVERGE       = 5,   // (was 6) zigzag low/high vers centre
  NUM_ARP_PATTERNS   = 6
};
```

**Note D2 (NVS) :** la renumérotation déplace ARP_ORDER (4 → 3), ARP_PEDAL_UP (8 → 4), ARP_CONVERGE (6 → 5). Une valeur stockée en NVS sous l'ancienne enum sera réinterprétée comme un autre pattern. Le bump `ArpPotStore` v1 (Task 3) provoque le reset NVS qui empêche cette réinterprétation silencieuse — les valeurs raw existantes échouent au load (size mismatch 8 ≠ 12).

**Steps :**
- [ ] Réduire enum `ArpPattern` selon la cible.
- [ ] Vérifier que `NUM_ARP_PATTERNS` est consommé partout cohérent (grep) : `PotRouter::getRangeForTarget` line 146 utilise `NUM_ARP_PATTERNS - 1` → automatic.
- [ ] Supprimer `effectiveOctaveRange` (header + impl).
- [ ] `rebuildSequence` : supprimer tous les cas non listés dans la nouvelle enum, remplacer `effectiveOctaveRange()` par `_octaveRange` direct.
- [ ] Supprimer arrays `FIB_BOUNCE` / `FIB_SPIRAL` (orphelins après suppression OCTAVE_BOUNCE et PROBABILITY).
- [ ] `executeStep` ligne 579-587 : supprimer le shuffle case `ARP_RANDOM`.
- [ ] `s_patNames[]` dans main.cpp (cf [`arp-reference.md` §11](../../reference/arp-reference.md)) : grep `s_patNames`, mettre à jour.
- [ ] Compile gate.

**Verification :**
- `grep -rn "ARP_RANDOM\|ARP_CASCADE\|ARP_DIVERGE\|ARP_UP_OCTAVE\|ARP_DOWN_OCTAVE\|ARP_CHORD\|ARP_OCTAVE_WAVE\|ARP_OCTAVE_BOUNCE\|ARP_PROBABILITY\|effectiveOctaveRange\|FIB_BOUNCE\|FIB_SPIRAL" src/` → 0 résultats.
- Compile : 0 erreur, 0 warning.
- HW checkpoint Phase 2 (final) : boot OK, NVS double reset (BankTypeStore + ArpPotStore) avec 2 warnings Serial. Tool 5 cycle ARPEG (toujours 3-états en Phase 2). Bank ARPEG joue. Pad oct 1-4 = octaves 1-4 littérales (pas de plancher). Pattern par défaut = ARP_ORDER. Patterns UP, DOWN, UP_DOWN, PEDAL_UP, CONVERGE accessibles via R2+hold (range 0-5).

**Commit gate** : commit Phase 2 après HW checkpoint validé. Message suggéré : `feat(arpeg-gen): plumbing — BankType=ARPEG_GEN, BankTypeStore v3, ArpPotStore v1 bump, ArpPattern 15→6`.

---

# Phase 3 — `isArpType` cascade

**Objectif** : étendre tous les call-sites qui filtraient sur `BANK_ARPEG` strict pour traiter `BANK_ARPEG_GEN` comme une variante d'ARPEG. **Aucun comportement musicien-facing nouveau** — les banks ARPEG_GEN se comportent comme ARPEG mais sans engine assigné (assignation engine = Phase 4 Task 6).

**HW checkpoint Phase 3** : compile OK, ARPEG classique inchangé musicalement. Pas encore possible de jouer une bank ARPEG_GEN (pas d'engine assigné).

---

## Task 5 — `isArpType` cascade dans tous les call-sites

**Cross-refs** : spec §4 (table « Code sites impactés ») + §32 (LED) + audit Phase 1 §1 I-1 + §1.5 cohabitation LOOP supra.

**Files** (recensement exhaustif) :
- Modify : [`src/main.cpp:128`](../../../src/main.cpp:128) — `midiPanic` : `isArpType` au lieu de `BANK_ARPEG`. (LOOP : pas d'engine, naturellement skip.)
- Modify : [`src/main.cpp:352`](../../../src/main.cpp:352) — boot engine assignment (`if (s_banks[i].type == BANK_ARPEG)`) → `if (isArpType(s_banks[i].type))`. **Phase 3 ne change PAS encore l'engine semantic** — l'engine reste en mode CLASSIC (defensive default). Phase 4 ajoute le `setEngineMode`.
- Modify : [`src/main.cpp:371`](../../../src/main.cpp:371) — boucle d'application params NVS aux engines : `isArpType`.
- Modify : [`src/main.cpp:562`](../../../src/main.cpp:562) — `handleLeftReleaseCleanup` : `isArpType` (ARPEG_GEN se comporte comme ARPEG pour le sweep).
- Modify : [`src/main.cpp:584`](../../../src/main.cpp:584) — `handlePadInput` switch : **structure 4-way avec default LOOP** (cf §1.5 C1) :
  ```cpp
  switch (slot.type) {
    case BANK_NORMAL:    processNormalMode(state, slot); break;
    case BANK_ARPEG:
    case BANK_ARPEG_GEN: if (slot.arpEngine) processArpMode(state, slot, now); break;
    default: break;  // BANK_LOOP : Phase 1 LOOP wires processLoopMode here
  }
  ```
- Modify : [`src/main.cpp:599`](../../../src/main.cpp:599) — `reloadPerBankParams` : `isArpType`.
- Modify : [`src/main.cpp:641`](../../../src/main.cpp:641) — scale change propagation foreground : `isArpType`.
- Modify : [`src/main.cpp:654`](../../../src/main.cpp:654) — scale change propagation group : `isArpType`.
- Modify : [`src/main.cpp:685`](../../../src/main.cpp:685) — `handleHoldPad` : `isArpType` (hold pad disponible pour ARPEG_GEN aussi).
- Modify : [`src/main.cpp:703`](../../../src/main.cpp:703) — `pushParamsToEngine` : `isArpType`. **Phase 3 conserve `setPattern(potRouter.getPattern())`** — Phase 6 fera le branching ARPEG vs ARPEG_GEN sur Pattern vs GenPosition.
- Modify : [`src/managers/BankManager.cpp:85`](../../../src/managers/BankManager.cpp:85) — double-tap Play/Stop : `isArpType` au lieu de `BANK_ARPEG` strict (cf §1.5 C2). **Pas de branche LOOP ici** : LOOP Phase 1 ajoutera son propre `else if (wasRecent && type == BANK_LOOP) { silent consume }`. Laisser un commentaire `// LOOP : double-tap handler à câbler par plan LOOP Phase 1` après le `if` arp.
- Modify : [`src/managers/BankManager.cpp:207`](../../../src/managers/BankManager.cpp:207) — debug print label : remplacer le ternaire par un **switch 4-way** :
  ```cpp
  const char* typeLabel = "?";
  switch (_banks[_currentBank].type) {
    case BANK_NORMAL:    typeLabel = "NORMAL";    break;
    case BANK_ARPEG:     typeLabel = "ARPEG";     break;
    case BANK_ARPEG_GEN: typeLabel = "ARPEG_GEN"; break;
    case BANK_LOOP:      typeLabel = "LOOP";      break;  // placeholder Phase 1 LOOP
    default:             typeLabel = "?";         break;
  }
  ```
- Modify : [`src/managers/ScaleManager.cpp:181`](../../../src/managers/ScaleManager.cpp:181) — octave pads : `isArpType` (Phase 8 Task 19 fera le branching `setOctaveRange` vs `setMutationLevel` selon `_engineMode`). En Phase 3, garder appel `setOctaveRange` pour ne pas casser ARPEG existant — mutation level sera no-op pour ARPEG_GEN tant que l'engine n'a pas `_engineMode = GENERATIVE`. (LOOP : naturellement skip via `isArpType`.)
- Modify : [`src/core/LedController.cpp:351`](../../../src/core/LedController.cpp:351) — bargraph color : **switch 3-way explicite** (cf §1.5 C5, site oublié par LOOP) :
  ```cpp
  const RGBW* barColor;
  if (_slots) {
    BankType ft = _slots[_currentBank].type;
    if      (isArpType(ft))    barColor = &_colors[CSLOT_MODE_ARPEG];
    else if (ft == BANK_LOOP)  barColor = &_colors[CSLOT_MODE_LOOP];
    else                       barColor = &_colors[CSLOT_MODE_NORMAL];
  } else {
    barColor = &_colors[CSLOT_MODE_NORMAL];
  }
  // Adapter l'usage `barColor` (déréférencement) en aval ; `barDim = *barColor` (même hue).
  ```
- Modify : [`src/core/LedController.cpp:500-503`](../../../src/core/LedController.cpp:500-503) — switch render dispatch : **structure 4-way avec stub LOOP** (cf §1.5 C4) :
  ```cpp
  switch (_slots[i].type) {
    case BANK_NORMAL:    renderBankNormal(i, isFg);      break;
    case BANK_ARPEG:
    case BANK_ARPEG_GEN: renderBankArpeg(i, isFg, now);  break;
    case BANK_LOOP:      /* Phase 1 LOOP wires renderBankLoop(i, isFg, now) */ break;
    default: break;
  }
  ```
- Modify : [`src/managers/NvsManager.cpp:266`](../../../src/managers/NvsManager.cpp:266) — `currentType == BANK_ARPEG` (queueArpPotWrite) : `isArpType` (ARPEG_GEN sauvegarde aussi son arp pot params).
- Modify : [`src/managers/NvsManager.cpp:618`](../../../src/managers/NvsManager.cpp:618) — boot defaults : conserver `(i < 4) ? BANK_NORMAL : BANK_ARPEG` (spec §23 reset defaults). Pas de création automatique de bank ARPEG_GEN au reset.
- Modify : [`src/setup/ToolLedSettings.cpp:940`](../../../src/setup/ToolLedSettings.cpp:940) — preview color slot : `isArpType`.
- Modify : [`src/setup/ToolBankConfig.cpp`](../../../src/setup/ToolBankConfig.cpp) (multiple call-sites lignes 131, 193, 199, 219, 225, 267, 284, 362, 365) — Phase 3 garde le cycle 3-états. **Ne pas changer le cycle ici** : la transition vers le cycle 5-états est en Phase 4 (Task 7) pour éviter les rollbacks intermédiaires si HW checkpoint échoue. **Pour les call-sites de label** (lignes 284, 362) : refactorer en switch 4-way label `(NORMAL / ARPEG / ARPEG_GEN / LOOP)` même si seul ARPEG_GEN est introduit ici — ça évite à Phase 1 LOOP de re-toucher ces sites.

**Steps :**
- [ ] Patcher chaque ligne de `BANK_ARPEG` strict identifiée → `isArpType` (sauf ToolBankConfig, gardé en Phase 4).
- [ ] Vérifier les switches `BankType` (LedController:500, main.cpp:580) ajoutent un `case BANK_ARPEG_GEN`.
- [ ] BankManager.cpp:207 : refactorer le ternaire en switch 3-way pour le label debug.
- [ ] Compile gate.
- [ ] Static read-back : `grep -rn "BANK_ARPEG\b" src/` doit retourner uniquement les définitions enum + les call-sites légitimes restants (ex. NvsManager defaults, switch cases). Aucun filtre de comportement runtime ne doit comparer `== BANK_ARPEG` strict.

**Verification HW checkpoint Phase 3 :**
- Boot OK, ARPEG classique inchangé musicalement (NORMAL + ARPEG identiques).
- Si on force manuellement `_banks[i].type = BANK_ARPEG_GEN` à la première éxecution post-Task-5 (édition Tool 5 sera Phase 4) — pas applicable encore sans Tool 5 cycle 5-états. Le HW checkpoint cible : aucune régression sur les modes existants.

**Commit gate** : commit Phase 3 après HW validation. Message : `refactor(arpeg-gen): isArpType cascade in all BANK_ARPEG callsites`.

---

# Phase 4 — `_engineMode` field + Tool 5 cycle 5 états + UI minimal

**Objectif** : Tool 5 permet de tagger une bank ARPEG_GEN. Engine reçoit `_engineMode` mais reste en `CLASSIC` semantic (Phase 5 implémente GENERATIVE). Bank ARPEG_GEN apparaît dans Tool 5, accepte des notes, mais joue silencieusement (sequenceGen vide, tick early-return en Phase 4).

**HW checkpoint Phase 4** : Tool 5 cycle 5-états OK. Bank tagged ARPEG_GEN apparaît, sélectionnable. Aucun crash, aucun stuck note. Bank ARPEG_GEN joue rien (volontaire).

---

## Task 6 — ArpEngine `_engineMode` + boot engine assignment

**Cross-refs** : spec §4 (engine pool partagé), §10 (deux régimes d'écriture).

**Files :**
- Modify : [`src/arp/ArpEngine.h`](../../../src/arp/ArpEngine.h) — ajouter `enum class EngineMode { CLASSIC, GENERATIVE }`, membre `EngineMode _engineMode`, méthode `setEngineMode(BankType type)`, méthode `EngineMode getEngineMode() const`.
- Modify : [`src/arp/ArpEngine.cpp:17-43`](../../../src/arp/ArpEngine.cpp:17-43) — constructor : init `_engineMode = EngineMode::CLASSIC`.
- Modify : [`src/arp/ArpEngine.cpp`](../../../src/arp/ArpEngine.cpp) — implémentation `setEngineMode` : si transition CLASSIC → GENERATIVE, marquer `_sequenceGenDirty = true` (sera utilisé en Task 10 pour forcer regen au prochain seed).
- Modify : [`src/main.cpp:352-388`](../../../src/main.cpp:352-388) — boot engine assignment : appeler `s_arpEngines[arpIdx].setEngineMode(s_banks[i].type)` après `setChannel`.
- Modify : [`src/main.cpp`](../../../src/main.cpp) — boot params loading : ajouter `s_banks[i].arpEngine->setBonusPile(s_nvsManager.getLoadedBonusPile(i))` et `setMarginWalk(s_nvsManager.getLoadedMarginWalk(i))` (méthodes ajoutées en Task 8, mais déclarées dans `ArpEngine.h` ici comme stubs `void setBonusPile(uint8_t x10)` qui stocke `_bonusPilex10`, idem `setMarginWalk(uint8_t)`).

**Steps :**
- [ ] Ajouter enum `EngineMode` dans `ArpEngine.h` (header public, accessible depuis main.cpp).
- [ ] Membre `EngineMode _engineMode` (private).
- [ ] Methods `setEngineMode(BankType)` (public, à appeler depuis boot path et hypothétiquement Tool 5 save — D4 dit reboot mandatory donc en pratique boot path uniquement) + `getEngineMode()`.
- [ ] Stubs `setBonusPile(uint8_t)` / `setMarginWalk(uint8_t)` + membres `uint8_t _bonusPilex10 = 15`, `uint8_t _marginWalk = 7`.
- [ ] Constructor init des 3 nouveaux membres.
- [ ] Boot path main.cpp : `setEngineMode(slot.type)`, `setBonusPile(getLoadedBonusPile(i))`, `setMarginWalk(getLoadedMarginWalk(i))`.
- [ ] Compile gate.

**Verification :**
- `grep -n "_engineMode\|setEngineMode" src/arp/` : présent.
- Boot : pas de crash, comportement ARPEG classique inchangé (engine en CLASSIC default, GENERATIVE not yet wired in tick).

---

## Task 7 — Tool 5 cycle 5 états + 1-ligne minimal

**Cross-refs** : spec §8 « Tool 5 — cycle de type étendu », §22 (layout — Phase 7 finalise les 2 lignes), §0 D5.

**Files :**
- Modify : [`src/setup/ToolBankConfig.cpp`](../../../src/setup/ToolBankConfig.cpp) :
  - Ligne 131 (`confirmDefaults` reset) : ajouter init `wkBonusPilex10[i] = 15`, `wkMarginWalk[i] = 7` pour TOUTES les banks (cf spec §23 « Reset `[d]` defaults »).
  - Lignes 187-238 (cycle ←→ 3-états) : étendre au cycle 5-états.
  - Lignes 191-194, 218-221 (arpCount) : utiliser `isArpType((BankType)wkTypes[i])` au lieu de `== BANK_ARPEG`.
  - Lignes 270-272 (header counter) : passer de "N/4 ARPEG" à "N/4 ARP" + utiliser `isArpType` pour le compte.
  - Lignes 281-349 (render banks) : Phase 4 garde le rendu 1-ligne pour ARPEG_GEN ("ARPEG_GEN  Quantize: X" sur une seule ligne, pas encore Bonus/Margin). Phase 7 (Task 16) ajoute la 2e ligne.
  - Lignes 362, 365 (drawDescription) : étendre `drawDescription(cursor, type)` à 3 cas (NORMAL, ARPEG, ARPEG_GEN). Description ARPEG_GEN selon spec §25 (placeholder en Phase 4, finalisé Phase 7 Task 17).
- Modify : [`src/setup/ToolBankConfig.h`](../../../src/setup/ToolBankConfig.h) — signature `saveConfig` étendue pour accepter `bonusPilex10[]` et `marginWalk[]` (déjà nécessaire ici car le cycle peut faire NORMAL → ARPEG_GEN, et la save doit persister les defaults).

**Cycle cible (right arrow)** :

```
NORMAL → ARPEG-Imm → ARPEG-Beat → ARPEG_GEN-Imm → ARPEG_GEN-Beat → NORMAL
```

**Cycle cible (left arrow)** : sens inverse.

**Validation arpCount** : `isArpType((BankType)wkTypes[i])` cumule ARPEG + ARPEG_GEN. Cap = `MAX_ARP_BANKS = 4`. Message d'erreur : `[!] Max 4 ARP banks!` (spec §24).

**Steps :**
- [ ] Ajouter `uint8_t wkBonusPilex10[NUM_BANKS]` et `wkMarginWalk[NUM_BANKS]` dans `run()`.
- [ ] Initialiser ces working copies depuis NVS (`getLoadedBonusPile`, `getLoadedMarginWalk`) au démarrage de `run()`.
- [ ] `saveConfig` signature étendue, écrit ces deux tableaux dans `bts.bonusPilex10[]` / `bts.marginWalk[]`.
- [ ] Sur reset `[d]` : remettre `wkBonusPilex10[i] = 15`, `wkMarginWalk[i] = 7` pour toutes les banks.
- [ ] Étendre le cycle ←→ pour gérer 5 états cohérents avec `arpCount` cap.
- [ ] Header counter : "N/4 ARP" + `isArpType` cumul.
- [ ] Render bank ARPEG_GEN en 1-ligne minimal (Phase 4) : `"  Bank N    ARPEG_GEN   Quantize: X        Group: A"`.
- [ ] `drawDescription` étendue pour 3 cas.
- [ ] Compile gate.

**Verification HW checkpoint Phase 4 :**
- Boot OK.
- Tool 5 : navigation ↑↓ entre banks OK. Edit mode (ENTER) cycle 5-états OK. Reset `[d]` OK (banks 1-4 NORMAL, 5-8 ARPEG, bonusPilex10=15, marginWalk=7). Save OK (NVS écrit, flashSaved).
- Reboot, vérifier persistance.
- Tagger une bank en ARPEG_GEN-Imm. Reboot. Sélectionner cette bank. Hold pad disponible. Pad press : pad ajouté à la pile (engine reçoit `addPadPosition`), tick fires, mais `_engineMode = GENERATIVE` et `_sequenceGen[]` vide → executeStep retourne sans note (sequenceLen = 0). **Aucune note MIDI émise mais pas de crash.**
- Switch entre banks ARPEG_GEN, ARPEG, NORMAL : aucun stuck note, aucun glitch.

**Commit gate** : commit Phase 4 après HW validation. Message : `feat(arpeg-gen): _engineMode field + Tool 5 cycle 5 états + 1-line minimal layout`.

---

# Phase 5 — ArpEngine GENERATIVE mode core (algo §11–§17)

**Objectif** : implémenter le mode GENERATIVE complet. Bank ARPEG_GEN joue effectivement, premier vrai test musical. Plus grosse phase du plan, 5 tasks (8 → 12) pour décomposer le travail.

**HW checkpoint Phase 5** : ARPEG_GEN joue. Pad oct 1 = lock (séquence figée), pad oct 2-4 = mutations (1/16, 1/8, 1/4). R2+hold balaye 15 zones de la grille (longueur, écart). Pile vivante (ajout/retrait notes intégrés via mutations futures, pas de reset sauf pile 0→1). Premier check de ressenti musical.

---

## Task 8 — Buffers `_sequenceGen`, `_pileDegrees` + ScaleResolver helpers

**Cross-refs** : spec §11 « Encodage de la séquence », §14 (`_pileDegrees` précalculé).

**Files :**
- Modify : [`src/midi/ScaleResolver.h`](../../../src/midi/ScaleResolver.h) + `.cpp` — ajouter 2 statiques :
  - `static int8_t padOrderToDegree(uint8_t order, const ScaleConfig& scale)` : retourne le degré scale signé pour un padOrder donné. En chromatic, retourne `order` direct (1 semitone = 1 degré). En scale 7-notes, retourne `order` (un degré scale = `order % 7`, octave = `order / 7`, mais on peut représenter ça en degré linéaire `order` puisque chaque pad correspond à un degré de la gamme).
  - `static uint8_t degreeToMidi(int8_t degree, const ScaleConfig& scale)` : conversion inverse. Retourne `0xFF` si MIDI hors range [0,127]. En chromatic : `note = ROOT_MIDI_BASE[scale.root] + degree`. En scale 7-notes : `note = ROOT_MIDI_BASE[scale.root] + (degree/7)*12 + INTERVALS[scale.mode][((degree%7)+7)%7]` (gestion modulo négatif).
- Modify : [`src/arp/ArpEngine.h`](../../../src/arp/ArpEngine.h) :
  - Ajouter constante `MAX_ARP_GEN_SEQUENCE = 96` (spec §11).
  - Membres :
    - `int8_t _sequenceGen[MAX_ARP_GEN_SEQUENCE]` (96 octets).
    - `int8_t _pileDegrees[MAX_ARP_NOTES]` (48 octets).
    - `uint8_t _pileDegreeCount` (current count, separate from `_positionCount` in case of resolution failures).
    - `int8_t _pileLo, _pileHi` (cached pile_lo, pile_hi).
    - `uint16_t _seqLenGen` (current `seqLen` from §13 grid position).
    - `uint8_t _genPosition` (0-14, current grid position).
    - `uint8_t _mutationLevel` (1-4, where 1 = lock, 2 = 1/16, 3 = 1/8, 4 = 1/4 — alias on `_octaveRange` semantic for ARPEG_GEN).
    - `bool _sequenceGenDirty` (set on pile 0→1, scale change, etc.).
    - `int16_t _stepIndexGen` (read pointer into `_sequenceGen[]`).
    - `uint16_t _mutationStepCounter` (counter for mutation step targets).
  - Méthodes :
    - `void setGenPosition(uint8_t pos)` (clamp 0-14, lookup TABLE_GEN_POSITION[pos] = {seqLen, ecart}).
    - `void setMutationLevel(uint8_t level)` (clamp 1-4, alias for setOctaveRange semantic when ARPEG_GEN).
- Modify : [`src/arp/ArpEngine.cpp`](../../../src/arp/ArpEngine.cpp) :
  - Constructor : init de tous les nouveaux membres.
  - Define static `TABLE_GEN_POSITION[15]` selon spec §13 (15 entries `{seqLen, ecart}`).
  - Implémentations `setGenPosition` (lookup + handle pot move at lock = §19 partiellement, finalisé Task 18), `setMutationLevel` (mémorise le level, prend effet à mutation step counter check).
  - Helper `recomputePileDegrees()` : pour chaque pad de la pile, `padOrderToDegree(_padOrder[padIdx], _scale)`. Recalc `_pileLo`, `_pileHi`. Appelé sur `addPadPosition`, `removePadPosition`, `setScaleConfig`.

**Cible static table (`ArpEngine.cpp`)** :

```cpp
// Spec §13 : 15 positions discrètes (longueur, écart)
struct GenPositionEntry { uint16_t seqLen; uint8_t ecart; };
static const GenPositionEntry TABLE_GEN_POSITION[15] = {
  { 8,  1}, { 8,  2}, {16,  2}, {16,  3}, {24,  3},
  {24,  4}, {32,  4}, {32,  5}, {48,  5}, {48,  6},
  {64,  6}, {64,  7}, {96,  8}, {96, 10}, {96, 12}
};
```

**Steps :**
- [ ] Ajouter helpers ScaleResolver (`padOrderToDegree`, `degreeToMidi`) avec gestion modulo négatif et clamp MIDI.
- [ ] Ajouter constante `MAX_ARP_GEN_SEQUENCE = 96` dans `ArpEngine.h`.
- [ ] Déclarer membres et méthodes selon le plan.
- [ ] Constructor init.
- [ ] Static `TABLE_GEN_POSITION` defined dans `ArpEngine.cpp`.
- [ ] Implémenter `setGenPosition`, `setMutationLevel`, `recomputePileDegrees`.
- [ ] Hook `recomputePileDegrees()` dans `addPadPosition`, `removePadPosition`, `setScaleConfig`.
- [ ] Compile gate.

**Verification :**
- `grep -n "_sequenceGen\|_pileDegrees\|TABLE_GEN_POSITION" src/arp/` : présent.
- Compile : 0 warning.

---

## Task 9 — Walk : pondération exponentielle + bornes (spec §12 §16)

**Cross-refs** : spec §12 « Walk : pondération de proximité », §16 « Bornes du walk ».

**Files :**
- Modify : [`src/arp/ArpEngine.h`](../../../src/arp/ArpEngine.h) — ajouter méthodes private :
  - `int8_t pickNextDegree(int8_t prev, uint8_t E, bool useScalePool)` : retourne le degré tiré pondéré, ou `prev` si pool vide (fallback répétition spec §37).
- Modify : [`src/arp/ArpEngine.cpp`](../../../src/arp/ArpEngine.cpp) — implémentation `pickNextDegree` :
  1. Computer `walk_min = _pileLo - _marginWalk`, `walk_max = _pileHi + _marginWalk`.
  2. Construire pool de candidats :
     - **Si `useScalePool == false` (initial generation)** : pool = `_pileDegrees[0..pileDegreeCount-1]`.
     - **Si `useScalePool == true` (mutation)** : pool = `_pileDegrees[]` ∪ scale_within_window. Pour la fenêtre scale `[prev - E, prev + E]`, énumérer chaque degré ; en chromatic, tous sont valides. En scale 7-notes, tous sont valides aussi (un degré = une note de la gamme).
  3. Filtrer par bornes : `walk_min ≤ d ≤ walk_max`.
  4. Filtrer par écart : `|d - prev| ≤ E`.
  5. Si pool filtré vide → retour `prev` (fallback).
  6. Calculer poids : `w(Δ) = expf(-fabsf((float)(d - prev)) / ((float)E * 0.4f))` pour chaque candidat. Si candidat appartient à `_pileDegrees[]`, multiplier par `_bonusPilex10 / 10.0f` (uniquement si `useScalePool == true`).
  7. Cumulative weights, tirage uniforme via `random()`, recherche dichotomique (linéaire OK pour ≤ 30 candidats).
  8. Retourner degré sélectionné.

**Note `_bonusPilex10`** : stocké en uint8_t (10..20), divisé par 10.0 à l'usage. Spec §15 : « bonus_pile multiplicateur configurable per-bank, défaut 1.5, range 1.0–2.0 ».

**Const facteur 0.4 (spec §40 point 1)** : exposer comme constante compile-time : `static constexpr float ARPEG_GEN_PROXIMITY_FACTOR = 0.4f;` au niveau fichier `ArpEngine.cpp`. Tunable post-implémentation.

**Steps :**
- [ ] Constante `ARPEG_GEN_PROXIMITY_FACTOR = 0.4f`.
- [ ] Implémentation `pickNextDegree` selon le plan.
- [ ] Méthode privée non testable seule (testée via Task 10 + 11 indirectement).
- [ ] Compile gate.

**Verification :**
- Compile.

---

## Task 10 — Generation initiale + reset triggers (spec §14 §17)

**Cross-refs** : spec §14 « Génération initiale », §17 « Triggers de reset », §16 (cas dégénéré pile=1 note).

**Files :**
- Modify : [`src/arp/ArpEngine.h`](../../../src/arp/ArpEngine.h) — ajouter méthode private :
  - `void seedSequenceGen()` : (re)génère `_sequenceGen[0..seqLen-1]`.
- Modify : [`src/arp/ArpEngine.cpp`](../../../src/arp/ArpEngine.cpp) :
  - Implémentation `seedSequenceGen` :
    1. Pré-condition : `_pileDegreeCount > 0`. Sinon early-return avec `_seqLenGen = 0`.
    2. `seqLen = TABLE_GEN_POSITION[_genPosition].seqLen`.
    3. `E_pot = TABLE_GEN_POSITION[_genPosition].ecart`.
    4. `E_init = max(E_pot, _pileHi - _pileLo)` (spec §14 « écart effectif »).
    5. Si `_pileDegreeCount == 1` : `_sequenceGen[0..seqLen-1] = _pileDegrees[0]` (répétition, spec §34).
    6. Sinon : `_sequenceGen[0] = _pileDegrees[random(0, _pileDegreeCount)]` (tirage uniforme).
    7. Pour `i = 1..seqLen-1` : `_sequenceGen[i] = pickNextDegree(_sequenceGen[i-1], E_init, false)` (pool = pile uniquement, pas de bonus_pile, écart élargi).
    8. `_seqLenGen = seqLen`. `_sequenceGenDirty = false`. `_stepIndexGen = -1`.
  - Hook trigger sur `addPadPosition` : si `wasEmpty && _engineMode == GENERATIVE` après l'incrément `_positionCount`, marquer `_sequenceGenDirty = true`.
  - Hook trigger sur `clearAllNotes` : `_sequenceGenDirty = true` (sera regen au prochain seed).
  - Hook trigger sur `setScaleConfig` : recomputePileDegrees() (déjà appelé), mais **pas de regen** (spec §17 : scale change n'est pas un trigger). Les degrés stockés restent valides.

**Steps :**
- [ ] Implémenter `seedSequenceGen`.
- [ ] Hooks dans `addPadPosition` / `clearAllNotes`.
- [ ] Compile gate.

**Verification :**
- Trace `Serial.printf("[GEN] seed seqLen=%d E_init=%d\n", _seqLenGen, E_init)` debug-gated `#if DEBUG_SERIAL`.

---

## Task 11 — Mutation + step counter (spec §15 §20)

**Cross-refs** : spec §15 « Mutation », §20 « Pad oct change ».

**Files :**
- Modify : [`src/arp/ArpEngine.cpp`](../../../src/arp/ArpEngine.cpp) — ajouter méthode private :
  - `void maybeMutate(uint16_t globalStepCount)` : appelée à chaque step exécuté en mode GENERATIVE.
    - Lookup mutation rate selon `_mutationLevel` :
      - 1 → no mutation, return immédiat (lock).
      - 2 → 1/16 : si `globalStepCount % 16 == 0`, mute.
      - 3 → 1/8 : si `% 8 == 0`, mute.
      - 4 → 1/4 : si `% 4 == 0`, mute.
    - Si mutation déclenchée :
      1. Tirage uniforme `index = random(0, _seqLenGen)`.
      2. `prev = _sequenceGen[(index - 1 + _seqLenGen) % _seqLenGen]`.
      3. `E_pot = TABLE_GEN_POSITION[_genPosition].ecart`.
      4. `_sequenceGen[index] = pickNextDegree(prev, E_pot, true)` (pool = pile ∪ scale_window, bonus_pile actif, écart strict E_pot).

**Steps :**
- [ ] Implémenter `maybeMutate`.
- [ ] Initialiser `_mutationStepCounter = 0` dans constructor + reset à chaque `seedSequenceGen` (spec §15 implicite : compteur global step).
- [ ] Compile gate.

---

## Task 12 — `tick()` branching CLASSIC vs GENERATIVE

**Cross-refs** : spec §10 (deux régimes), §11 (deux buffers).

**Files :**
- Modify : [`src/arp/ArpEngine.cpp:572-654`](../../../src/arp/ArpEngine.cpp:572-654) — `executeStep` : dispatcher selon `_engineMode`.

**Cible** :

```cpp
void ArpEngine::executeStep(MidiTransport& transport, uint32_t stepDurationUs) {
  if (_engineMode == EngineMode::GENERATIVE) {
    // -- GENERATIVE path --
    if (_sequenceGenDirty) seedSequenceGen();
    if (_seqLenGen == 0) return;

    _stepIndexGen++;
    if (_stepIndexGen >= (int16_t)_seqLenGen) _stepIndexGen = 0;

    int8_t degree = _sequenceGen[_stepIndexGen];
    uint8_t midiNote = ScaleResolver::degreeToMidi(degree, _scale);
    if (midiNote == 0xFF) {
      // Out-of-range MIDI : silence ce step, mais maybeMutate (séquence évoluera)
      maybeMutate(_mutationStepCounter);
      _mutationStepCounter++;
      _tickFlash = true;
      _shuffleStepCounter++;
      return;
    }

    // Velocity, gate, shuffle calc (identique au CLASSIC path)
    // ... (extraction commune dans helper privé recommandée)

    // Schedule noteOff + noteOn (pattern identique au CLASSIC path)

    maybeMutate(_mutationStepCounter);
    _mutationStepCounter++;
    _tickFlash = true;
    _shuffleStepCounter++;
    return;
  }

  // -- CLASSIC path (existing logic ArpEngine.cpp:572-654 unchanged) --
  if (_sequenceDirty) rebuildSequence();
  // ... etc
}
```

**Steps :**
- [ ] Refactorer `executeStep` pour dispatcher en début de fonction.
- [ ] Extraire la logique velocity+gate+shuffle+schedule dans un helper privé `executeStepNote(transport, stepDurationUs, midiNote)` (DRY entre CLASSIC et GENERATIVE).
- [ ] CLASSIC path : appelle `executeStepNote` avec note résolue via padIndex+_padOrder+_scale (logique existante).
- [ ] GENERATIVE path : appelle `executeStepNote` avec note résolue via `degreeToMidi`.
- [ ] Compile gate.

**Verification HW checkpoint Phase 5 :**
- Tagger bank 8 en ARPEG_GEN-Imm. Reboot.
- Sélectionner bank 8. Pad oct 1 (= lock). Press un pad → première note ajoutée → `_sequenceGenDirty = true` → `seedSequenceGen` au premier tick → séquence figée.
- Press 2 autres pads → notes intégrées au pool, séquence reste figée (lock).
- Pad oct 2 (1/16) → mutations apparaissent ~1 fois par 16 steps. Audible.
- Pad oct 3 (1/8), pad oct 4 (1/4) → mutations plus fréquentes.
- Pad oct 1 (retour lock) → séquence figée à nouveau (capture).
- R2+hold balaye position 1 → 15 : longueur change (8 → 96 steps), écart change. Audible.
- Vider la pile → séquence stop, _sequenceGenDirty armé. Re-press un pad → regen complète.
- Bank switch ARPEG_GEN ↔ NORMAL : aucun stuck note.
- Bank switch ARPEG_GEN background : continue de tourner.
- Stop / Play (hold pad) : Stop avec doigts levés → paused pile, séquence reste. Play → reprend (séquence préservée).
- Scale change : transposition automatique (spec §18). Vérifier audiblement.
- Scale change pendant lock : la séquence figée transpose, reste figée.

**Commit gate** : commit Phase 5 après HW validation. Message : `feat(arpeg-gen): GENERATIVE engine — seed, walk, mutation, branching tick`.

---

# Phase 6 — Pot routing TARGET_GEN_POSITION

**Objectif** : R2+hold contrôle `_genPosition` via PotRouter avec hystérésis, two-binding strategy active. Pad oct contrôle `_mutationLevel` via routing existant (octaveRange→mutation level alias).

**HW checkpoint Phase 6** : R2+hold balaye 15 zones avec hystérésis stable (pas de zone flicker). Bank switch ARPEG↔ARPEG_GEN reseed catch correctement (pot doit être recapturé). Pad oct fonctionne pour les deux modes.

---

## Task 13 — `TARGET_GEN_POSITION` + 6 fonctions PotRouter étendues + two-binding

**Cross-refs** : spec §5 « Routing pots et hold-left », §0 D3 (two-binding strategy), audit Phase 1 §1 I-1.

**Files :**
- Modify : [`src/core/KeyboardData.h:506-535`](../../../src/core/KeyboardData.h:506-535) — enum `PotTarget` : ajouter `TARGET_GEN_POSITION` juste après `TARGET_PATTERN`. **Insérer avant `TARGET_NONE`** pour ne pas changer la valeur de TARGET_NONE. Préférable : insérer juste après `TARGET_PATTERN` pour grouper sémantiquement, mais alors les valeurs des targets suivants se décalent → impact sur PotMappingStore stocké.
- Modify : [`src/managers/PotRouter.h`](../../../src/managers/PotRouter.h) — ajouter membre `uint8_t _genPosition`, getter `uint8_t getGenPosition() const`, extension `loadStoredPerBank` signature pour accepter gen_position.
- Modify : [`src/managers/PotRouter.cpp`](../../../src/managers/PotRouter.cpp) — 6 fonctions étendues (cf audit Phase 1 §1 I-1 tableau).

**Décision insertion enum `TARGET_GEN_POSITION`** :
- Insérer **juste avant `TARGET_EMPTY`** (=fin de la liste utile, avant les sentinels) pour minimiser le risque de décalage. Les valeurs de TARGET_PATTERN, etc. restent inchangées. Le seul nouveau target a une valeur fraîche.
- TARGET_GEN_POSITION n'apparaît PAS dans le pool Tool 7 (D3) → pas d'impact sur `PotMappingStore` stocké.

**Cible enum (KeyboardData.h)** :

```cpp
enum PotTarget : uint8_t {
  // ... (existing entries unchanged) ...
  TARGET_PAD_SENSITIVITY,
  TARGET_MIDI_CC,
  TARGET_MIDI_PITCHBEND,
  // ARPEG_GEN per-bank (NEW — runtime-only, not user-mappable in Tool 7)
  TARGET_GEN_POSITION,
  // Empty slot
  TARGET_EMPTY,
  // Sentinel
  TARGET_NONE,
  TARGET_COUNT = TARGET_NONE
};
```

**6 fonctions PotRouter étendues** :

| Fonction | Lignes existantes | Modification |
|---|---|---|
| `getRangeForTarget` | 136-156 | `case TARGET_GEN_POSITION: lo = 0; hi = 14; break;` |
| `rebuildBindings` | 161-227 | Boucle `ctx < 2` inchangée. **Après** la boucle, ajouter une 2e passe pour ARPEG_GEN : pour chaque slot ARPEG context (`_mapping.arpegMap`), si target == TARGET_PATTERN, créer un binding miroir avec `bankType = BANK_ARPEG_GEN` et `target = TARGET_GEN_POSITION`. Tous les autres slots ARPEG context sont aussi mirrorés avec `bankType = BANK_ARPEG_GEN` (sinon les pots ARPEG_GEN n'auront pas leurs autres bindings) — sauf si le target est TARGET_PATTERN (substitué). |
| `seedCatchValues` | 233-300 | `case TARGET_GEN_POSITION: norm = (float)_genPosition / 14.0f; break;` |
| `applyBinding` | 385-543 | `case TARGET_GEN_POSITION:` write `_genPosition = (uint8_t)adcToRange(adc, 0, 14);` (avec hystérésis Task 14). |
| `isPerBankTarget` | 587-603 | Ajouter `case TARGET_GEN_POSITION:` dans la liste retournant `true`. |
| `getDiscreteSteps` | 607-619 | `case TARGET_GEN_POSITION: return 14;` (15 zones, 14 steps entre boundaries). |

**Two-binding strategy détaillée** (`rebuildBindings` 2e passe) :

```cpp
// Phase 6 : after the existing ctx<2 loop, mirror ARPEG bindings to ARPEG_GEN context
for (uint8_t slot = 0; slot < POT_MAPPING_SLOTS; slot++) {
  PotTarget target = _mapping.arpegMap[slot].target;
  if (target == TARGET_EMPTY || target == TARGET_NONE) continue;

  uint8_t potIdx  = slot / 2;
  uint8_t btnMask = (slot & 1) ? 0x01 : 0x00;

  // Substitute TARGET_PATTERN -> TARGET_GEN_POSITION for ARPEG_GEN
  PotTarget effective = (target == TARGET_PATTERN) ? TARGET_GEN_POSITION : target;

  uint16_t lo, hi;
  getRangeForTarget(effective, lo, hi);

  PotBinding& b = _bindings[_numBindings];
  b.potIndex   = potIdx;
  b.buttonMask = btnMask;
  b.bankType   = BANK_ARPEG_GEN;
  b.target     = effective;
  b.rangeMin   = lo;
  b.rangeMax   = hi;
  b.ccNumber   = _mapping.arpegMap[slot].ccNumber;

  // CC slot tracking si target == TARGET_MIDI_CC (le user a mappé un CC en arpeg context, applique aussi à arpeg_gen)
  if (effective == TARGET_MIDI_CC && _ccSlotCount < MAX_CC_SLOTS) {
    _ccNumber[_ccSlotCount]     = _mapping.arpegMap[slot].ccNumber;
    _ccValue[_ccSlotCount]      = 0;
    _ccDirty[_ccSlotCount]      = false;
    _ccBindingIdx[_ccSlotCount] = _numBindings;
    _ccSlotCount++;
  }

  _numBindings++;
  if (_numBindings >= MAX_BINDINGS) break;
}
```

**Capacity check** : `MAX_BINDINGS = 24` dans PotRouter.h. Aujourd'hui, default mapping = 8 NORMAL + 8 ARPEG + 2 rear = 18 bindings. Avec 8 ARPEG_GEN ajoutés → 26 → **dépasse `MAX_BINDINGS = 24`**. Plan : augmenter `MAX_BINDINGS = 32` dans PotRouter.h. Cohérent avec le budget « prefer safe » (32 × sizeof(PotBinding) ≈ 32 × 12 ≈ 400 octets supplémentaires).

**Steps :**
- [ ] Insérer `TARGET_GEN_POSITION` dans enum PotTarget (juste avant TARGET_EMPTY).
- [ ] PotRouter.h : `MAX_BINDINGS = 32`, ajout `_genPosition`, getter, signature `loadStoredPerBank` étendue (8e param).
- [ ] PotRouter.cpp : 6 fonctions étendues + 2e passe rebuildBindings.
- [ ] Constructor : init `_genPosition = 5` (= position 5, defaults compile-time spec §13).
- [ ] Compile gate.

**Verification :**
- `grep -rn "TARGET_GEN_POSITION" src/` : 6 sites + définition + getter consumer.
- Compile.

---

## Task 14 — Hystérésis pour `TARGET_GEN_POSITION`

**Cross-refs** : spec §5 (dernière paragraphe), §40 point 4, audit Phase 1 §1 I-5 (scope restreint à TARGET_GEN_POSITION en v1).

**Files :**
- Modify : [`src/managers/PotRouter.h`](../../../src/managers/PotRouter.h) — ajouter membres `uint8_t _genPosLastZone[NUM_BANKS]` (mémoire de zone par bank, initialisé à 0xFF = not yet captured). Ou plus simple : `uint8_t _genPosLastZone` (global, partagé entre banks ARPEG_GEN — acceptable car pot R2+hold est global, par-bank catch déjà géré ailleurs).
- Modify : [`src/managers/PotRouter.cpp`](../../../src/managers/PotRouter.cpp) — `applyBinding` case TARGET_GEN_POSITION : implémenter hystérésis ±1.5 % × 4095 ≈ ±61 ADC units. Ne pas mettre à jour `_genPosition` tant que l'ADC n'a pas franchi la frontière de zone élargie de cette marge.

**Cible logique** :

```cpp
case TARGET_GEN_POSITION: {
  // Convertir ADC -> zone candidate (0..14)
  uint8_t candidate = (uint8_t)adcToRange(adc, 0, 14);
  if (candidate > 14) candidate = 14;

  // Hystérésis : si on n'est pas encore initialisé, accepter direct.
  if (_genPosLastZone == 0xFF) {
    _genPosition = candidate;
    _genPosLastZone = candidate;
    break;
  }

  // Sinon, ne basculer que si on dépasse la frontière de zone courante de ±HYSTERESIS_LSB
  static constexpr uint16_t HYSTERESIS_LSB = 61; // ±1.5 % × 4095
  // Calc ADC bornes de la zone courante
  float zoneCenterAdc = ((float)_genPosLastZone + 0.5f) * 4095.0f / 15.0f;
  float distFromCenter = fabsf(adc - zoneCenterAdc);
  float zoneHalfWidth  = 4095.0f / 30.0f; // demi-largeur d'une zone
  if (distFromCenter > (zoneHalfWidth + HYSTERESIS_LSB)) {
    _genPosition    = candidate;
    _genPosLastZone = candidate;
  }
  break;
}
```

**Note** : la formulation ci-dessus est indicative. Le plan d'exécution doit valider le calcul exact (centre de zone, demi-largeur, hystérésis). À tuner si nécessaire au HW checkpoint.

**Steps :**
- [ ] Membre `_genPosLastZone = 0xFF` dans PotRouter.h.
- [ ] Implémenter hystérésis dans applyBinding case TARGET_GEN_POSITION.
- [ ] Compile gate.

**Verification :**
- HW : tourner R2+hold lentement à travers les 15 zones, vérifier qu'il n'y a pas de zone flicker à la frontière de zone (oscillation rapide entre deux zones).

---

## Task 15 — `pushParamsToEngine` + `reloadPerBankParams` branching ARPEG vs ARPEG_GEN

**Cross-refs** : spec §5, §11 (deux buffers).

**Files :**
- Modify : [`src/main.cpp:702-714`](../../../src/main.cpp:702-714) — `pushParamsToEngine` : si `_engineMode == GENERATIVE`, appeler `setGenPosition(s_potRouter.getGenPosition())` au lieu de `setPattern(s_potRouter.getPattern())`. Tous les autres setters (gate, shuffle, division, base velocity, velocity variation) restent communs.
- Modify : [`src/main.cpp:594-613`](../../../src/main.cpp:594-613) — `reloadPerBankParams` : étendre pour récupérer le `_genPosition` depuis l'engine ARPEG_GEN si applicable, et le pousser vers `s_potRouter.loadStoredPerBank` (signature étendue Task 13). Pour les banks ARPEG classiques, `_genPosition` non significatif → pousser default 5 ou la valeur PotRouter actuelle.

**Cible `pushParamsToEngine`** :

```cpp
static void pushParamsToEngine(BankSlot& slot) {
  if (!isArpType(slot.type) || !slot.arpEngine) return;

  slot.arpEngine->setGateLength(s_potRouter.getGateLength());
  slot.arpEngine->setShuffleDepth(s_potRouter.getShuffleDepth());
  slot.arpEngine->setDivision(s_potRouter.getDivision());
  slot.arpEngine->setShuffleTemplate(s_potRouter.getShuffleTemplate());
  slot.arpEngine->setBaseVelocity(s_potRouter.getBaseVelocity());
  slot.arpEngine->setVelocityVariation(s_potRouter.getVelocityVariation());

  if (slot.arpEngine->getEngineMode() == ArpEngine::EngineMode::GENERATIVE) {
    slot.arpEngine->setGenPosition(s_potRouter.getGenPosition());
  } else {
    slot.arpEngine->setPattern(s_potRouter.getPattern());
  }
}
```

**Steps :**
- [ ] Modifier `pushParamsToEngine` selon le plan.
- [ ] Modifier `reloadPerBankParams` pour récupérer `_genPosition` (ajouter getter `ArpEngine::getGenPosition()` si pas déjà fait Task 8).
- [ ] Modifier signature `loadStoredPerBank` PotRouter (Task 13 a peut-être déjà couvert ce point — vérifier cohérence).
- [ ] Compile gate.

**Verification HW checkpoint Phase 6 :**
- R2+hold balaye 15 zones avec hystérésis OK.
- Bank switch ARPEG → ARPEG_GEN : pot R2+hold doit être recapturé (catch system kicks in, bargraph dim affiché jusqu'au crossing).
- Inverse ARPEG_GEN → ARPEG : pareil.
- Si user mappe Tool 7 le slot R2+hold ARPEG context vers MIDI CC : plus de TARGET_PATTERN ni TARGET_GEN_POSITION. ARPEG joue avec le pattern courant figé. ARPEG_GEN joue avec _genPosition figé. Cohérent (D3).

**Commit gate** : commit Phase 6 après HW validation. Message : `feat(arpeg-gen): TARGET_GEN_POSITION pot routing + hysteresis + two-binding`.

---

# Phase 7 — Tool 5 sous-champs ARPEG_GEN UI complète

**Objectif** : Tool 5 affiche le layout 2-lignes pour ARPEG_GEN, edit field-focus 4-champs (D5), INFO panel ARPEG_GEN, control bar variants. Bonus_pile et margin éditables, persistés en NVS.

**HW checkpoint Phase 7** : Tool 5 ARPEG_GEN flow complet. Edit cycle 4-champs (TYPE → GROUP → BONUS → MARGIN → TYPE). Bonus_pile = 1.0..2.0 (pas 0.1) éditable. Margin = 3..12 éditable. Save → reboot → vérifier persistance + comportement engine ARPEG_GEN avec ces valeurs.

---

## Task 16 — Tool 5 layout 2-lignes + edit field-focus 4-champs ARPEG_GEN

**Cross-refs** : spec §22 « Layout des banks », §23 « Edit mode », §0 D5.

**Files :**
- Modify : [`src/setup/ToolBankConfig.cpp`](../../../src/setup/ToolBankConfig.cpp) :
  - Ajouter enum local `enum SubField { SF_TYPE, SF_GROUP, SF_BONUS, SF_MARGIN }` + variable `SubField cursorSubField` initialisé à `SF_TYPE`.
  - Le cycle linéaire D5 : ←→ avance/recule cursorSubField selon ordre `TYPE → GROUP → BONUS → MARGIN → TYPE` (ou inverse).
  - Si bank type courant != ARPEG_GEN, restreindre cursorSubField à {SF_TYPE, SF_GROUP} (BONUS/MARGIN n'existent pas pour NORMAL/ARPEG).
  - ↑↓ ajuste valeur du cursorSubField :
    - SF_TYPE : cycle 5 états (NORMAL → ARPEG-Imm → ARPEG-Beat → ARPEG_GEN-Imm → ARPEG_GEN-Beat → NORMAL).
    - SF_GROUP : cycle (-, A, B, C, D).
    - SF_BONUS : ±1 (ev.accelerated → ±10) sur range 10-20 (= 1.0-2.0).
    - SF_MARGIN : ±1 (ev.accelerated → ±10) sur range 3-12.
  - Render :
    - NORMAL/ARPEG : 1 ligne (existant), cursorSubField visible via `[brackets]` cyan-bold.
    - ARPEG_GEN : 2 lignes selon §22.
      - Ligne 1 : `Bank N    ARPEG_GEN   Quantize: X        Group: A`.
      - Ligne 2 : `              Bonus pile: 1.5    Margin: 7` (indentation à la colonne 14, sous le label `ARPEG_GEN`).
  - Le `> ` cyan-bold cursor reste sur la ligne 1 toujours (spec §22 dernière paragraphe).
  - Sous-champ focalisé rendu en `VT_CYAN VT_BOLD "[value]" VT_RESET`.
  - Sous-champ non focalisé rendu en `VT_DIM` (hors edit) ou normal (en edit).
  - Transition de type pendant edit : si user cycle vers NORMAL/ARPEG depuis ARPEG_GEN, et cursorSubField ∈ {SF_BONUS, SF_MARGIN}, jump cursorSubField à SF_TYPE.

**Note exception §4.4 (D5)** : ajouter commentaire en tête du handler ←→/↑↓ :

```cpp
// Tool 5 ARPEG_GEN edit mode : cycle linéaire 4-champs MARGIN→TYPE→GROUP→BONUS→MARGIN.
// Exception §4.4 strict (qui demande ←→ horizontal sur la ligne, ↑↓ change ligne) :
// le cycle linéaire est plus court (4 keypress max) et préserve la règle universelle ↑↓ = adjust.
// Plan ARPEG_GEN §0 D5.
```

**Steps :**
- [ ] Refactorer la state machine d'edit pour gérer cursorSubField.
- [ ] Render layout 2-lignes ARPEG_GEN (calcul colonnes pour aligner Bonus/Margin sous label).
- [ ] Steps de type cycle 5 états (Task 7 a posé la base, raffiner ici).
- [ ] Save étendu : `saveConfig` reçoit aussi `bonusPilex10[]` + `marginWalk[]`.
- [ ] Compile gate.

**Verification :**
- Naviguer Tool 5, ENTER sur bank ARPEG_GEN, cycle ←→ entre TYPE → GROUP → BONUS → MARGIN. Édition ↑↓ pour chaque champ. Save (ENTER), flashSaved.

---

## Task 17 — INFO panel §25 + control bar §26

**Cross-refs** : spec §25 « INFO panel », §26 « Control bar ».

**Files :**
- Modify : [`src/setup/ToolBankConfig.cpp::drawDescription`](../../../src/setup/ToolBankConfig.cpp:51-63) : étendre pour 3 cas (NORMAL, ARPEG, ARPEG_GEN).
  - ARPEG_GEN base description selon spec §25 (extrait à recopier dans le code).
- Modify : `ToolBankConfig.cpp` (fonction de rendu INFO) : si en edit ARPEG_GEN, afficher description du sous-champ focalisé (TYPE / GROUP / BONUS_PILE / MARGIN_WALK) selon spec §25 (extraits à recopier).
- Modify : `ToolBankConfig.cpp` (control bar — lignes 386-392) : 3 variantes selon contexte.
  - Hors edit : `[^v] NAV  │  [RET] EDIT  [d] DFLT  │  [q] EXIT` (existant).
  - Edit NORMAL ou ARPEG : `[</>] FIELD  [^v] VALUE  │  [RET] SAVE  │  [q] CANCEL` (modifié — alignement §4.4).
  - Edit ARPEG_GEN : `[</>] FIELD  [^v] VALUE  │  [RET] SAVE  │  [q] CANCEL` (identique au précédent — la nav est la même au sens utilisateur).

**Note unification** : §26 spec donne 2 variantes différentes pour edit ARPEG vs ARPEG_GEN (`[</>] TYPE [^v] GROUP` vs `[</>] VALUE [^v] FIELD`). En réalité, après Task 16, la nav est uniformisée (←→ focus, ↑↓ adjust) selon la nouvelle convention §4.4. Donc une seule variante `[</>] FIELD  [^v] VALUE` pour edit, applicable aux deux.

**Steps :**
- [ ] Étendre `drawDescription` à 3 cas + sous-descriptions par champ.
- [ ] Mettre à jour control bar pour edit (uniformisée).
- [ ] Compile gate.

---

## Task 18 — Pot move at lock §19 + pad oct change §20

**Cross-refs** : spec §19 « Pot move pendant lock », §20 « Pad oct change ».

**Files :**
- Modify : [`src/arp/ArpEngine.cpp`](../../../src/arp/ArpEngine.cpp) — `setGenPosition` (Task 8 stub) : implémentation complète.
  - Nouveau `genPosNew = pos`, ancien `genPosOld = _genPosition`.
  - Lookup new `seqLenNew = TABLE_GEN_POSITION[genPosNew].seqLen`, `ecartNew = TABLE_GEN_POSITION[genPosNew].ecart`.
  - Si `_engineMode == CLASSIC` : juste store + return (no-op pour ARPEG classique).
  - Si `genPosNew == genPosOld` : no-op.
  - Si `_pileDegreeCount == 0` : juste store seqLenGen = seqLenNew (la séquence est vide, pas de regen tant que pile pas réinjectée). Spec §19 « Pile vide au moment du pot move : pas de regen, longueur reste seqLen_old » — formulation un peu ambiguë, on choisit : longueur prend la nouvelle valeur (cohérent avec extension fugace), mais pas de regen tant que pile vide.
  - Si `_mutationLevel == 1` (lock) :
    - Si seqLenNew > _seqLenGen : extension fugace — générer steps additionnels via `pickNextDegree` avec `prev` clampé à `[walk_min, walk_max]` localement.
    - Si seqLenNew < _seqLenGen : troncature.
    - Si seqLenNew == _seqLenGen : seul l'écart change (cas pos 1↔2, etc.) → no regen, nouvel écart pris en compte aux mutations futures (qui n'existent pas en lock — donc aucun effet visible jusqu'à mutation level change).
  - Si `_mutationLevel >= 2` (mutation active) : même logique extension/troncature, mutation continue avec nouvel écart.
  - Update `_genPosition = genPosNew`, `_seqLenGen = seqLenNew`.
- Modify : [`src/arp/ArpEngine.cpp`](../../../src/arp/ArpEngine.cpp) — `setMutationLevel` (Task 8 stub) : juste store level, prend effet à `maybeMutate`. Pas de mémoire séquence pre-mutation (spec §20 « Pas de mémoire de séquence pre-mutation »).

**Steps :**
- [ ] Implémenter logique complète `setGenPosition`.
- [ ] Implémenter `setMutationLevel` (déjà simple).
- [ ] Compile gate.

**Verification HW checkpoint Phase 7 :**
- Tool 5 flow ARPEG_GEN complet : édition TYPE/GROUP/BONUS/MARGIN, save, reboot, persistance vérifiée.
- Bonus_pile à 1.0 : mutation très libre (pile peu privilégiée). Bonus_pile à 2.0 : mutation reste très ancrée pile.
- Margin à 3 : walk très serré autour de pile. Margin à 12 : walk dérive beaucoup.
- En lock + pot move R2+hold (changement de longueur) : extension/troncature fugace OK.
- En lock + pot move (longueur identique, écart change) : séquence préservée.
- Pad oct 4 → pad oct 1 : capture de l'état courant.
- Pad oct 1 → pad oct 4 : reprise de mutation depuis l'état capturé.

**Commit gate** : commit Phase 7 après HW validation. Message : `feat(arpeg-gen): Tool 5 sub-fields UI + pot move at lock + pad oct semantics`.

---

# Phase 8 — ScaleManager octave pads + LED finitions + docs

**Objectif** : finalisation. ScaleManager octave pads route correctement vers `setMutationLevel` pour ARPEG_GEN. LED finitions §32. Mise à jour des refs de doc.

**HW checkpoint Phase 8 (final)** : audit des invariants live :
- No orphan notes (sweep gauche-button release).
- Refcount atomicity (pas de stuck note malgré shuffle + boucle longue).
- Paused pile préservé sur bank switch.
- Bank switch silence OK.
- FG ↔ BG ARPEG_GEN continuité (BG continue de tourner).
- Scale change transposition automatique sur FG ARPEG_GEN.

---

## Task 19 — ScaleManager octave pads → setMutationLevel pour ARPEG_GEN

**Cross-refs** : spec §33 « ScaleManager et octave pads ».

**Files :**
- Modify : [`src/managers/ScaleManager.cpp:181-200`](../../../src/managers/ScaleManager.cpp:181-200) :

```cpp
// --- Octave pads (ARPEG and ARPEG_GEN, semantic differs by engine mode) ---
if (isArpType(slot.type) && slot.arpEngine) {
  for (uint8_t o = 0; o < 4; o++) {
    uint8_t pad = _octavePads[o];
    if (pad >= NUM_KEYS) continue;

    bool pressed = keyIsPressed[pad];
    bool wasPressed = _lastScaleKeys[pad];

    if (pressed && !wasPressed) {
      _newOctaveRange = o + 1;  // 1-4
      _octaveChanged = true;

      // Sémantique selon engine mode :
      // - CLASSIC : octave range littéral (1-4 octaves de la pile)
      // - GENERATIVE : mutation level (1=lock, 2=1/16, 3=1/8, 4=1/4)
      if (slot.arpEngine->getEngineMode() == ArpEngine::EngineMode::GENERATIVE) {
        slot.arpEngine->setMutationLevel(o + 1);
      } else {
        slot.arpEngine->setOctaveRange(o + 1);
      }

      #if DEBUG_SERIAL
      Serial.printf("[ARP] Pad oct %d (%s)\n", o + 1,
                    slot.arpEngine->getEngineMode() == ArpEngine::EngineMode::GENERATIVE
                    ? "mutation level" : "octave range");
      #endif
    }
    _lastScaleKeys[pad] = pressed;
  }
}
```

**Steps :**
- [ ] Refactorer le bloc octave pads selon le plan.
- [ ] Compile gate.

**Verification :**
- HW : pad oct 1-4 sur bank ARPEG → octaves 1-4 (litterales). Sur bank ARPEG_GEN → 1=lock, 2=1/16, 3=1/8, 4=1/4 mutation rate.

---

## Task 20 — LED finitions §32

**Cross-refs** : spec §32 « LED feedback ».

**Files :**
- Modify : [`src/core/LedController.cpp`](../../../src/core/LedController.cpp) — vérifier que tous les sites `BANK_ARPEG` strict (LedController.cpp:351 bargraph color, :502 render dispatch) sont bien `isArpType` ou `case BANK_ARPEG_GEN`. Patché en Task 5, mais re-vérifier ici par read-back final.
- Modify : [`src/setup/ToolLedSettings.cpp:940`](../../../src/setup/ToolLedSettings.cpp:940) — déjà patché en Task 5, re-vérifier.

**Steps :**
- [ ] Static read-back sur LedController + ToolLedSettings.
- [ ] HW : bank ARPEG_GEN affiche le même rendu LED qu'ARPEG (pulse_slow stopped-loaded, flash on tick, solid playing). Bargraph color identique à ARPEG.

---

## Task 21 — Docs update

**Cross-refs** : spec §2 « État actuel et docs obsolètes » (la spec dit que la mise à jour de `arp-reference.md` fait partie du livrable d'implémentation).

**Files :**
- Modify : [`docs/reference/arp-reference.md`](../../reference/arp-reference.md) :
  - §4 (« Scheduler state machine ») : corriger « Bar | Next bar | up to 96 ticks » → supprimer la ligne (le mode `Bar` n'existe pas en code, seulement Immediate et Beat).
  - §6 (« Patterns ») : remplacer la table 5 patterns par la table 6 patterns post-réduction.
  - Ajouter §13 « Generative mode (ARPEG_GEN) » : architecture (pile vivante, walk, mutation), §11–§17 spec en condensé.
  - Mettre à jour §11 « Adding a new arp pattern » : ajout note que les patterns sont dans le mode CLASSIC uniquement, ARPEG_GEN ne se configure pas via patterns.
- Modify : [`docs/reference/nvs-reference.md`](../../reference/nvs-reference.md) :
  - Mettre à jour table « V2 Stores » : `BankTypeStore` v3 (44B), ajouter mention `ArpPotStore` v1 (12B) dans table « Existing Stores ».
  - Étendre `validateBankTypeStore` description.
  - Ajouter `validateArpPotStore` à la table.
- Modify : [`docs/reference/patterns-catalog.md`](../../reference/patterns-catalog.md) :
  - P1 (refcount) : noter que ARPEG_GEN partage refcount avec ARPEG (pas de duplication, même `_noteRefCount[128]`).
  - P3 (event queue) : noter que ARPEG_GEN partage event queue avec ARPEG (`MAX_PENDING_EVENTS = 64` per engine, alimenté par les deux modes).

**Steps :**
- [ ] Patcher les 3 fichiers ref.
- [ ] Vérifier que tous les liens cross-ref sont valides.

**Verification HW checkpoint Phase 8 (FINAL) :**
Run live ≥ 2 minutes audit complet :
- Bank ARPEG_GEN : seed initial OK sur première note. Pile vivante (notes ajoutées intégrées via mutations). Lock figé exact. Mutation rate 1/16, 1/8, 1/4 audibles distinctement.
- Pot R2+hold balaye 15 zones avec hystérésis stable.
- Pad oct 1 (lock) : séquence reproductible à l'identique sur plusieurs cycles.
- Bank switch ARPEG ↔ ARPEG_GEN : aucun stuck note, foreground change OK.
- BG ARPEG_GEN continue de tourner pendant FG ARPEG.
- Scale change : transposition automatique audible (foreground).
- Stop/Play (hold pad ou LEFT+double-tap) : paused pile préservé, relaunch from step 0.
- Pile vidée pendant mutation active : engine passe à IDLE sans glitch.
- Reboot : config ARPEG_GEN persistée (Tool 5 valeurs bonus_pile / margin OK).
- Vérifier qu'aucun warning Serial inattendu n'apparaît au boot (les warnings BankTypeStore v2→v3 et ArpPotStore raw→v1 ne doivent plus apparaître après le premier reboot post-update).

**Commit gate** : commit Phase 8 après HW validation. Message : `docs(arpeg-gen): update arp-reference, nvs-reference, patterns-catalog + finalize ScaleManager + LED`.

---

## §4 — Risques résiduels et tuning post-implémentation

Identifiés en audit Phase 1, à valider/tuner au HW :

1. **Facteur `0.4` dans formule de proximité** (spec §40 point 1). Constante compile-time dans `ArpEngine.cpp`. À tuner par écoute live.
2. **Defaults `bonus_pile = 1.5`, `margin = 7`** (spec §40 point 2). Defaults dans `validateBankTypeStore` + `NvsManager` constructor. Modifiables par le user via Tool 5.
3. **Taux mutation max 1/4** (spec §40 point 3). Si trop tame, augmenter à 1/2 dans la table de mapping `_mutationLevel`.
4. **Hystérésis ±1.5 % sur TARGET_GEN_POSITION** (spec §40 point 4). À tuner si zone flicker ou si trop ferme.
5. **Latence pad oct change → première mutation** (spec §40 point 5). Peut nécessiter un déclenchement immédiat de la première mutation post-change plutôt qu'attendre le prochain step.
6. **Out-of-range MIDI silence** (audit Phase 1 §3 S-4). Trace Serial debug-gated ajoutée Task 12. Si fréquent en run live, étudier extension de `walk_min/max` formula.
7. **Float math `expf` per mutation** (audit Phase 1 §3 S-3). Si overhead Core 1 perceptible, remplacer par LUT 16 entrées avec interpolation linéaire. Mesurer avant d'optimiser.

Ces tuning sont **acceptés** comme passes post-implémentation, modifiables par commits séparés (cf [`CLAUDE.md`](../../../.claude/CLAUDE.md) projet : « le tuning est une passe acceptée, pas un état permanent »).

---

## §5 — Checklist phase d'achèvement

Avant de marquer le plan complet :

- [ ] Phase 2 — plumbing OK (compile + boot warnings attendus + ARPEG inchangé musicalement).
- [ ] Phase 3 — isArpType cascade OK (compile + ARPEG inchangé).
- [ ] Phase 4 — Tool 5 cycle 5 états OK (engine assigné mais joue silencieusement).
- [ ] Phase 5 — ARPEG_GEN joue (premier vrai test musical).
- [ ] Phase 6 — Pot routing TARGET_GEN_POSITION + hystérésis (R2+hold balaye 15 zones).
- [ ] Phase 7 — Tool 5 UI complète (édition bonus/margin, persistance).
- [ ] Phase 8 — finitions + docs (audit invariants live).
- [ ] Toutes les tasks (1 → 21) marquées `[x]`.
- [ ] Git log propre (1 commit par phase, ou commits intermédiaires de plumbing + 1 commit terminal par phase).
- [ ] [`docs/reference/arp-reference.md`](../../reference/arp-reference.md) à jour.
- [ ] [`docs/reference/nvs-reference.md`](../../reference/nvs-reference.md) à jour.
- [ ] [`docs/reference/patterns-catalog.md`](../../reference/patterns-catalog.md) à jour.

---

**Validation finale par Loïc requise après HW checkpoint Phase 8.**

---

## §Annexe — Patches recommandés pour le plan LOOP Phase 1 (post-merge ARPEG_GEN)

Une fois ARPEG_GEN mergé, les Steps suivants du plan LOOP Phase 1 (`docs/superpowers/plans/2026-04-21-loop-phase-1-plan.md`) deviennent partiellement redondants ou nécessitent une amende. Le plan LOOP est à patcher en conséquence avant exécution :

| Step LOOP | État après ARPEG_GEN | Action |
|---|---|---|
| Step 1.2 (KeyboardData.h enum) | `BANK_LOOP = 2` est déjà déclaré (par ARPEG_GEN Task 1) | Skip — vérifier que l'enum est conforme. |
| Step 1.2 (validator clamp) | `validateBankTypeStore` clampe déjà `> BANK_ARPEG_GEN` | Skip — couvre déjà LOOP. |
| Step 1.3 (main.cpp handlePadInput switch) | Switch est déjà 3-cases + `default: break;` (couvre LOOP silencieux) | Modifier : remplacer le `default: break;` par `case BANK_LOOP: /* Phase 2 wires processLoopMode */ break;` (cosmétique, plus explicite). |
| Step 1.3 (main.cpp BankManager debug print) | Switch 4-way déjà en place (couvre LOOP) | Skip. |
| Step 1.5 (ToolBankConfig labels) | Labels déjà 4-way (couvre LOOP) | Skip. |
| Step 1.6 (LedController.h declare renderBankLoop) | À faire : nouveau stub à ajouter | Garder. |
| Step 1.6 (LedController.cpp switch render) | `case BANK_LOOP: break;` est déjà en place avec commentaire « Phase 1 LOOP wires renderBankLoop » | Modifier : remplacer le `break;` par `renderBankLoop(i, isFg, now); break;`. Trivial. |
| Step 4.2 (BankManager double-tap LOOP consume) | La branche `if (isArpType(...))` est en place avec commentaire « LOOP : double-tap handler à câbler » | Garder : ajouter la branche `else if (wasRecent && _banks[b].type == BANK_LOOP) { silent consume }` après la branche arp. |
| Step 4.3 (ScaleManager early-return LOOP) | À faire (ARPEG_GEN ne touche pas ce site) | Garder intact. |
| Step 5 (rename `fgArpPlayMax` → `fgPlayMax`) | À faire (ARPEG_GEN ne touche pas ce field) | Garder intact. |
| Step 6 (Tool 8 line move TRANSPORT) | À faire (ARPEG_GEN ne touche pas Tool 8 layout) | Garder intact. |
| Step 7 (EVT_WAITING) | À faire (ARPEG_GEN ne touche pas LedGrammar/event mapping) | Garder intact. |

**Modifications à faire dans le plan LOOP Phase 1 lui-même** (avant son exécution future) :
- En tête du document : ajouter une note « Plan rédigé avant ARPEG_GEN, patché post-merge ARPEG_GEN. Cf annexe `2026-04-26-arpeg-gen-plan.md` §Annexe pour la liste des Steps désormais skippables. »
- Step 1.10 commit message : ajuster pour refléter le scope réduit (le BankType enum + validator sont déjà fait, ne pas reclaim).
- Step 1.6 : retirer la mention `renderBankArpeg` mirror (pas concerné par LOOP).

Cette annexe sert de **change-list** au moment de reprendre LOOP Phase 1. Aucun patch immédiat n'est appliqué au fichier `2026-04-21-loop-phase-1-plan.md` par ce plan ARPEG_GEN — l'opérateur LOOP devra lire cette annexe en pré-lecture.
