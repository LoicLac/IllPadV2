# Tool 8 — UX Respec Implementation Plan (Phase 0.1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refondre l'UX du Tool 8 LED Settings en 6 sections single-view (NORMAL/ARPEG/LOOP/TRANSPORT/CONFIRMATIONS/GLOBAL), avec live preview découplé et 2 extensions NVS mineures — sans toucher la grammaire runtime 3 couches.

**Architecture:** 5 commits séquentiels : (1) Store bumps groupés `ColorSlotStore v4→v5` (+slot `CSLOT_VERB_STOP`) + `LedSettingsStore v6→v7` (rename+2 new tick durations) ; (2) `LedController` gagne un wrapper public `renderPreviewPattern` + caches tick/verb_stop + default mapping `EVT_STOP` pointant vers le nouveau slot ; (3) nouveau helper `src/setup/ToolLedPreview.{h,cpp}` encapsulant toute la logique preview (mockups, scheduler, replay) ; (4) refonte complète de `ToolLedSettings.{h,cpp}` orchestrant les 6 sections + panel description + preview ; (5) sync docs (setup-tools-conventions §4 nouveau paradigme + architecture-briefing + nvs-reference v5/v7).

**Tech Stack:** C++17 Arduino / PlatformIO, ESP32-S3, FreeRTOS, SK6812 NeoPixel, NVS (Preferences), VT100 terminal. Pas d'unit tests — la vérification par étape est (a) `pio run -e esp32-s3-devkitc-1` build clean, (b) upload hardware manuel utilisateur aux checkpoints explicites, (c) observation visuelle LEDs + interaction VT100.

**Préconditions :**
- HEAD = `main` @ `0441f5a` (ou plus récent).
- Working tree : fichiers `D`/`??` dans `docs/drafts/`, `docs/plans/`, `docs/archive/`, `docs/superpowers/plans/`, `docs/superpowers/specs/`, `docs/Futur_exploration/` → housekeeping utilisateur pré-existant, **ne pas toucher**, ne pas inclure dans les commits de ce plan.
- Spec source : [docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md](../specs/2026-04-20-tool8-ux-respec-design.md) — référence obligatoire à relire par l'exécutant avant chaque tâche.
- Git workflow strict ([.claude/CLAUDE.md](../../../.claude/CLAUDE.md) + `~/.claude/CLAUDE.md`) : toujours sur `main`, jamais de branch, commit/push uniquement sur autorisation explicite utilisateur par opération, jamais `git add .` ni `-A`, jamais `--no-verify`, jamais upload sans demande explicite.

**Règles d'exécution de ce plan :**
- Chaque commit est **présenté** à l'utilisateur avec la liste exacte des fichiers à stager + le message HEREDOC proposé. **Attendre un OK explicite** avant `git add <files>` + `git commit`.
- À chaque checkpoint hardware (`HW:` lignes ci-dessous), **demander à l'utilisateur** d'uploader le firmware et d'effectuer la vérification listée. Ne jamais lancer `pio ... -t upload` soi-même.
- Audit de complaisance interdit : si une tâche semble "juste marcher", relire les sections du spec concernées et vérifier champ par champ avant de déclarer `done`.

---

## File Structure

Fichiers créés, modifiés, ou laissés tels quels au terme du plan.

| Fichier | Action | Rôle |
|---|---|---|
| `src/core/KeyboardData.h` | Modifier | +`CSLOT_VERB_STOP=15`, `COLOR_SLOT_COUNT=16`, `COLOR_SLOT_VERSION=5` ; LedSettingsStore field `tickFlashDurationMs` (uint8) → `tickBeatDurationMs` (uint16) + new uint16 `tickBarDurationMs`, `tickWrapDurationMs` ; `LED_SETTINGS_VERSION=7` ; validator clamps sur les 3 nouvelles durées [5..500] |
| `src/core/LedGrammar.cpp` | Modifier | `EVENT_RENDER_DEFAULT[EVT_STOP]` : `colorSlot` passe de `CSLOT_VERB_PLAY` à `CSLOT_VERB_STOP` |
| `src/core/LedController.h` | Modifier | Rendre `PatternInstance` struct `public:` ; déclarer `void renderPreviewPattern(const PatternInstance&, unsigned long)` public ; remplacer member `_tickFlashDurationMs` par `_tickBeatDurationMs`/`_tickBarDurationMs`/`_tickWrapDurationMs` (uint16_t) ; ajouter cache `RGBW _colVerbStop` (unused direct, mais `_colors[15]` existe via COLOR_SLOT_COUNT=16) |
| `src/core/LedController.cpp` | Modifier | `loadLedSettings` charge les 3 durées tick ; `loadColorSlots` couvre 16 slots (la boucle itère déjà sur `COLOR_SLOT_COUNT`) ; définir `renderPreviewPattern` wrapper qui appelle `renderPattern(inst, now)` ; pas d'autre changement de dispatch en Phase 0.1 (ARPEG tick consomme BEAT via renommage) |
| `src/managers/NvsManager.cpp` | Modifier | Defaults v7 : init `tickBeatDurationMs=30`, `tickBarDurationMs=60`, `tickWrapDurationMs=100` ; defaults v5 : ajouter `CSLOT_VERB_STOP` preset=8 (Coral) hue=0 |
| `src/setup/ToolLedPreview.h` | **Créer** | Classe helper preview : enum `PreviewContext`, `begin/end`, `setContext`, `update` |
| `src/setup/ToolLedPreview.cpp` | **Créer** | Implémentation : mockup mono-FG, mockup LOOP ticks, one-shot replay avec black timer (§6.4 spec), breathing continu, waiting crossfade, tempo-synced scheduler |
| `src/setup/ToolLedSettings.h` | **Réécrire** | Nouveau header : enum `Section` (NORMAL/ARPEG/LOOP/TRANSPORT/CONFIRMATIONS/GLOBAL), enum `EditMode`, state + membres pour single-view scrollable, own un `ToolLedPreview` |
| `src/setup/ToolLedSettings.cpp` | **Réécrire** | Render 6 sections, NAV (↑↓ lignes, ←→ skip section, ENTER edit, d default, q exit), paradigmes d'édition (color/single/multi-focus), description panel, preview dispatch |
| `src/setup/SetupManager.h` | Modifier | Propager `PotRouter*` vers `_toolLedSettings.begin(...)` (injection tempo pour ToolLedPreview — confirmé via grep : `getTempoBPM()` vit sur PotRouter, pas NvsManager) |
| `src/setup/SetupManager.cpp` | Modifier | `case '8'` dispatch : `_toolLedSettings.begin(_leds, &_ui, potRouter)` — mirror du pattern Tool 7 existant |
| `docs/reference/setup-tools-conventions.md` | Modifier | +§4.4 : paradigme "geometric visual navigation" (multi-value row : ←→ focus, ↑↓ adjust ; exception color : ←→ preset, ↑↓ hue) + note fin §4.3 flaggant la divergence ↔ §4.4 + exception `d`-sans-confirm en §6.1 pour Tool 8 |
| `docs/reference/architecture-briefing.md` | Modifier | §4 Table 1 : ligne `LedSettingsStore`/`ColorSlotStore` → note v7/v5 + "Tool 8 = single view 6 sections" ; §8 Domain LEDs → Tool 8 pattern decoupled via `ToolLedPreview` ; §5 P14 mise à jour si besoin (preview plus étoffé) |
| `docs/reference/nvs-reference.md` | Modifier | Table Store Catalog : LedSettingsStore v6→v7, ColorSlotStore v4→v5 ; validator LedSettingsStore : mentionner les 3 nouveaux tick clamps |

Fichiers **explicitement non touchés** :
- `src/core/LedGrammar.h` (PatternId, EventId, PatternParams, EventRenderEntry inchangés).
- `src/core/CapacitiveKeyboard.*` (DO NOT MODIFY).
- `src/arp/*`, `src/midi/*`, `src/managers/BankManager/ScaleManager/BatteryMonitor/ControlPadManager.*`.
- `src/managers/PotRouter.{h,cpp}` : lu (source de `getTempoBPM()`) mais pas modifié.
- Autres setup Tools (`ToolCalibration/PadOrdering/PadRoles/ControlPads/BankConfig/Settings/PotMapping.*`).
- `src/main.cpp` (SetupManager est consommé sans changement de signature externe).
- `platformio.ini`.
- `docs/drafts/*`, `docs/archive/*`, `docs/Futur_exploration/*`, fichiers git `??`/`D` pré-existants.

---

## Task 1 — Store bumps groupés (ColorSlotStore v5 + LedSettingsStore v7)

**Why:** Un seul cycle de reset utilisateur pour les 2 bumps, minimise les warnings NVS au boot. Conformément à la Zero-Migration Policy (CLAUDE.md projet) — on bump, on laisse les anciens blobs être silencieusement rejetés, les compile-time defaults s'appliquent.

**Files:**
- Modify: `src/core/KeyboardData.h` (COLOR_SLOT_COUNT, COLOR_SLOT_VERSION, CSLOT_VERB_STOP, LED_SETTINGS_VERSION, tick durations fields, validateLedSettingsStore clamps)
- Modify: `src/managers/NvsManager.cpp` (defaults v7 + v5)
- Modify: `src/core/LedController.h` (rename member `_tickFlashDurationMs` → `_tickBeatDurationMs`, type uint8_t → uint16_t — guard build clean between Task 1 and Task 2)
- Modify: `src/core/LedController.cpp` (adapt `loadLedSettings` copy + any dispatch reading the renamed member)
- Modify: `src/setup/ToolLedSettings.cpp` (TLS v0.8e currently references `_lwk.tickFlashDurationMs` at 5 sites — rename inline so the old Tool 8 keeps compiling until Task 4 replaces it wholesale)
- Modify: `docs/reference/nvs-reference.md` (Store Catalog v7/v5 entries + validator line)

### Steps

- [ ] **Step 1.1 — Lire la spec §7.1 (Store bumps) + §9 (defaults).** Ne pas procéder sans avoir la spec ouverte sur ces deux sections, car les champs concrets (tickBeat/Bar/Wrap + CSLOT_VERB_STOP) doivent être encodés verbatim.

- [ ] **Step 1.2 — Étendre `ColorSlotId` et bumper ColorSlotStore.** Dans [src/core/KeyboardData.h](../../../src/core/KeyboardData.h:167), remplacer :

```cpp
#define COLOR_SLOT_COUNT       15
```

par :

```cpp
#define COLOR_SLOT_COUNT       16
```

puis dans l'enum `ColorSlotId` (ligne ~191), ajouter l'entrée après `CSLOT_CONFIRM_OK` :

```cpp
  CSLOT_CONFIRM_OK       = 14,  // Pure White  — SPARK suffix universal (LOOP Phase 1+)
  CSLOT_VERB_STOP        = 15,  // Coral       — Stop fade-out (Phase 0.1 respec)
};
```

Et bumper (ligne 500) :

```cpp
#define COLOR_SLOT_VERSION  5
```

- [ ] **Step 1.3 — Bumper LedSettingsStore et remplacer le field tick.** Dans [src/core/KeyboardData.h](../../../src/core/KeyboardData.h:261), bumper la version :

```cpp
#define LED_SETTINGS_VERSION       7   // v6 -> v7 : rename tickFlashDurationMs -> tickBeatDurationMs (uint8->uint16), +tickBarDurationMs, +tickWrapDurationMs
```

Dans le struct `LedSettingsStore`, repérer ligne ~281 :

```cpp
  uint8_t  tickFlashDurationMs;   // default 30  — FLASH pattern durationMs (ARPEG tick)
```

Remplacer par :

```cpp
  uint16_t tickBeatDurationMs;    // default 30  — FLASH pattern durationMs for ARPEG step / LOOP beat ticks (v7 rename + widen)
  uint16_t tickBarDurationMs;     // default 60  — FLASH pattern durationMs for LOOP bar ticks (v7 new, consumed Phase 1+)
  uint16_t tickWrapDurationMs;    // default 100 — FLASH pattern durationMs for LOOP wrap ticks (v7 new, consumed Phase 1+)
```

- [ ] **Step 1.4 — Mettre à jour le validator.** Dans [src/core/KeyboardData.h](../../../src/core/KeyboardData.h:647) `validateLedSettingsStore`, remplacer les deux lignes :

```cpp
  if (s.tickFlashDurationMs < 10)  s.tickFlashDurationMs = 10;
  if (s.tickFlashDurationMs > 100) s.tickFlashDurationMs = 100;
```

par :

```cpp
  if (s.tickBeatDurationMs < 5)   s.tickBeatDurationMs = 5;
  if (s.tickBeatDurationMs > 500) s.tickBeatDurationMs = 500;
  if (s.tickBarDurationMs  < 5)   s.tickBarDurationMs  = 5;
  if (s.tickBarDurationMs  > 500) s.tickBarDurationMs  = 500;
  if (s.tickWrapDurationMs < 5)   s.tickWrapDurationMs = 5;
  if (s.tickWrapDurationMs > 500) s.tickWrapDurationMs = 500;
```

- [ ] **Step 1.5 — Mettre à jour les defaults compile-time LedSettings.** Dans [src/managers/NvsManager.cpp](../../../src/managers/NvsManager.cpp:51), remplacer :

```cpp
  _ledSettings.tickFlashDurationMs = 30;
```

par :

```cpp
  _ledSettings.tickBeatDurationMs = 30;
  _ledSettings.tickBarDurationMs  = 60;
  _ledSettings.tickWrapDurationMs = 100;
```

- [ ] **Step 1.6 — Mettre à jour les defaults compile-time ColorSlots.** Dans [src/managers/NvsManager.cpp](../../../src/managers/NvsManager.cpp:87), la table `defaultPresets[COLOR_SLOT_COUNT]` a actuellement 15 entrées. Ajouter la 16ᵉ ligne à la fin, avant la fermeture `};` :

```cpp
    /* CSLOT_CONFIRM_OK       */ 0,   // Pure White (SPARK universal)
    /* CSLOT_VERB_STOP        */ 8,   // Coral (Phase 0.1 — Stop fade-out)
  };
```

Même opération ligne ~104 dans `defaultHueOffsets[COLOR_SLOT_COUNT]` :

```cpp
    /* CONFIRM_OK       */ 0,
    /* VERB_STOP        */ 0,
  };
```

La boucle `for` qui remplit `_colorSlots.slots[i]` (vérifier qu'elle itère bien sur `COLOR_SLOT_COUNT`, ce qui est déjà le cas) couvrira automatiquement le nouveau slot.

- [ ] **Step 1.7 — Propager le rename dans tous les consommateurs.** Le symbole `tickFlashDurationMs` n'existe plus après Step 1.3 ; tout consommateur doit être renommé **dans la même Task 1** pour garder le build clean. Principe de compilabilité entre commits inconditionnel.

Commande d'inventaire :

Run: Grep pattern `tickFlashDurationMs` dans `src/`
Expected après patch : 0 occurrence (ni dans un commentaire, ni dans un identifier).

Sites à corriger, connus à l'écriture du plan :

1. **`src/core/LedController.h`** : member `uint8_t _tickFlashDurationMs` → `uint16_t _tickBeatDurationMs`. (Les 2 nouveaux caches `_tickBarDurationMs` / `_tickWrapDurationMs` sont ajoutés en Task 2, pas ici — Task 1 ne fait que le rename + widen pour préserver la compilabilité.)

2. **`src/core/LedController.cpp`** :
   - `loadLedSettings` : ligne qui copie `s.tickFlashDurationMs` → remplacer par `_tickBeatDurationMs = s.tickBeatDurationMs;`.
   - Tout autre usage `_tickFlashDurationMs` dans le .cpp → renommer en `_tickBeatDurationMs`.

3. **`src/setup/ToolLedSettings.cpp` (TLS v0.8e)** — 5 sites confirmés, inconditionnels :
   - **Ligne 276** : commentaire `// PTN_FLASH : 3 fields (tickFlashDurationMs, tickFlashFg, tickFlashBg)` → remplacer `tickFlashDurationMs` par `tickBeatDurationMs`.
   - **Ligne 317** : `int v = (int)_lwk.tickFlashDurationMs + dir * step;` → `int v = (int)_lwk.tickBeatDurationMs + dir * step;`. Le cast `(int)` reste correct (uint16_t convert silencieusement). Step reste le même.
   - **Ligne 320** : `_lwk.tickFlashDurationMs = (uint8_t)v;` → `_lwk.tickBeatDurationMs = (uint16_t)v;`. **Changer le cast** : uint8_t → uint16_t (sinon perte de precision sur v > 255, or le validator accepte maintenant jusqu'à 500).
   - **Ligne 379** : `snprintf(buf, sizeof(buf), "%d ms  (10-100)", _lwk.tickFlashDurationMs);` → `snprintf(buf, sizeof(buf), "%d ms  (5-500)", _lwk.tickBeatDurationMs);`. **Élargir la mention de range** dans le texte affiché (5-500 conforme au nouveau validator) + le format `%d` reste compatible uint16_t.
   - **Ligne 713** : `_lwk.tickFlashDurationMs = 30;` → `_lwk.tickBeatDurationMs = 30;` (reset-to-defaults). Pas de changement de valeur, juste le rename.

4. **Audit après patch** : re-run `Grep tickFlashDurationMs` dans `src/`. Si une occurrence subsiste, la corriger avant de passer au Step 1.8.

**Pourquoi patcher TLS v0.8e en Task 1 plutôt qu'en Task 4 ?** Parce que Task 4 est une réécriture complète qui supprime TLS v0.8e. Si Task 1 ne patche pas TLS v0.8e, le build casse à Step 1.9 (entre Task 1 et Task 4 inclusivement) — violation du principe de compilabilité entre commits. Le rename TLS v0.8e est éphémère : Task 4 l'écrase entièrement, les 5 sites disparaissent. Coût plan 5 lignes, bénéfice : 3 commits intermédiaires buildables.

- [ ] **Step 1.8 — Mettre à jour `docs/reference/nvs-reference.md`.** Dans la table "Store Struct Catalog", modifier la ligne `LedSettingsStore` :

```
| `LedSettingsStore` | `illpad_lset` | `ledsettings` | 0xBEEF | 7 | ~96B | T8 LedSettings (v7 Phase 0.1 respec : rename tickFlashDurationMs -> tickBeatDurationMs (uint16), add tickBarDurationMs + tickWrapDurationMs for LOOP bar/wrap ticks Phase 1+) |
```

Et `ColorSlotStore` :

```
| `ColorSlotStore` | `illpad_lset` | `ledcolors` | 0xC010 | 5 | 36B | T8 LedSettings (v5 Phase 0.1 respec : add CSLOT_VERB_STOP slot 15 for Stop fade-out — decouples STOP from PLAY color) |
```

Dans la section "Validation Functions", ajouter à la ligne `validateLedSettingsStore` la mention `, tickBeat/Bar/WrapDurationMs [5..500]`.

- [ ] **Step 1.9 — Build clean.**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`, pas d'erreur, pas de warning sur `LedSettingsStore` ou `ColorSlotStore`.

Si le build casse sur un symbole `tickFlashDurationMs` encore référencé dans LedController, revenir au Step 1.7 et compléter le renommage.

- [ ] **Step 1.10 — Checkpoint commit (attendre OK utilisateur).**

Présenter à l'utilisateur :

Files à stager (nommer individuellement, pas de `-A`) :
- `src/core/KeyboardData.h`
- `src/core/LedController.h`
- `src/core/LedController.cpp`
- `src/managers/NvsManager.cpp`
- `src/setup/ToolLedSettings.cpp`
- `docs/reference/nvs-reference.md`

Message proposé :

```
feat(nvs): phase 0.1 step 1 — bump ColorSlotStore v5 + LedSettingsStore v7

- ColorSlotStore v4->v5: +CSLOT_VERB_STOP=15 (Coral default), COLOR_SLOT_COUNT=16.
  Decouples STOP fade-out from PLAY color (Phase 0.1 respec requirement).
- LedSettingsStore v6->v7: rename tickFlashDurationMs -> tickBeatDurationMs (widened
  to uint16), +tickBarDurationMs (default 60), +tickWrapDurationMs (default 100).
  BAR/WRAP durations land now to stabilize field layout; LOOP engine consumes
  them in Phase 1+.
- Validator: clamp the 3 tick durations to [5, 500] ms each.
- Defaults tables in NvsManager.cpp updated for both bumps.
- nvs-reference.md sync.

Zero Migration Policy: existing user settings silently reset to defaults on
first boot post-flash. One Serial.printf warning expected per store.

Phase 0.1 plan: docs/superpowers/plans/2026-04-20-tool8-ux-respec-plan.md
Spec: docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md §7.1
```

**Ne PAS committer** tant que l'utilisateur n'a pas explicitement autorisé (`OK`, `commit`, `go`).

- [ ] **Step 1.11 — HW: checkpoint hardware optionnel (utilisateur décide d'uploader ou pas).**

Si l'utilisateur upload après le commit :
- Attendu au boot serial : **deux** warnings de type `NVS: ledsettings reset to defaults` et `NVS: ledcolors reset to defaults`.
- Le LED behaviour doit rester identique à `0441f5a` (la seule différence fonctionnelle est que `EVT_STOP` est toujours câblé avec la couleur PLAY, change en Task 2).
- **Sous réserve de la propagation du rename dans TLS v0.8e (Step 1.7 sites 3)** : Tool 8 ancienne version reste fonctionnel ; la ligne PATTERNS/PTN_FLASH affiche désormais la range "5-500" au lieu de "10-100" et édite un uint16_t mais l'UX globale est inchangée. Le nouveau slot CSLOT_VERB_STOP apparaît en COLORS page avec sa default Coral (preset 8) — cosmétique, acceptable jusqu'à Task 4.

Si l'utilisateur ne veut pas uploader maintenant, passer à Task 2 ; le hardware checkpoint se fera au terme de Task 2 (même risque cumulé, plus sûr une fois `_colVerbStop` câblé).

---

## Task 2 — LedController : wrapper preview + caches tick + default EVT_STOP

**Why:** Exposer la machinery runtime pour que le helper Tool 8 preview puisse injecter des `PatternInstance` arbitraires sans dupliquer le rendu (`renderPattern` reste la seule source de vérité). Charger les 3 nouvelles durées tick + résoudre le nouveau slot `CSLOT_VERB_STOP`. Câbler `EVT_STOP` default sur le nouveau slot.

**Files:**
- Modify: `src/core/LedController.h`
- Modify: `src/core/LedController.cpp`
- Modify: `src/core/LedGrammar.cpp`

### Steps

- [ ] **Step 2.1 — Rendre `PatternInstance` public dans LedController.** Dans [src/core/LedController.h](../../../src/core/LedController.h:92), la struct `PatternInstance` est actuellement dans le bloc `private:`. Déplacer sa déclaration **avant** `private:` (ou ajouter un `public:` local devant elle). Le membre `_eventOverlay` reste dans `private:`, seule la définition du type passe en public.

Résultat attendu : `ToolLedPreview.cpp` (Task 3) pourra écrire `LedController::PatternInstance inst; inst.patternId = ...;`.

- [ ] **Step 2.2 — Déclarer `renderPreviewPattern` public.** Dans [src/core/LedController.h](../../../src/core/LedController.h), à la fin de la section publique (après `previewShow()` ligne ~91), ajouter :

```cpp
  // Phase 0.1 — Preview wrapper (Tool 8 live preview via ToolLedPreview helper).
  // Exposes renderPattern() publicly with an arbitrary PatternInstance.
  // Does NOT touch _eventOverlay (Tool 8 drives its own state). Zero runtime
  // duplication : any change to renderPattern automatically reflects in preview.
  void renderPreviewPattern(const PatternInstance& inst, unsigned long now);
```

- [ ] **Step 2.3 — Ajouter les 2 caches tick manquants dans le header.** Dans le bloc private de LedController.h (là où `_tickBeatDurationMs` a été renommé au Step 1.7), ajouter à côté les 2 nouveaux caches :

```cpp
  uint16_t _tickBeatDurationMs;   // default 30 (v7 Phase 0.1 rename)
  uint16_t _tickBarDurationMs;    // default 60 (v7 Phase 0.1, consumed Phase 1+)
  uint16_t _tickWrapDurationMs;   // default 100 (v7 Phase 0.1, consumed Phase 1+)
```

(Retirer `_tickFlashDurationMs` s'il existe encore — il a été renommé en Step 1.7.)

- [ ] **Step 2.4 — Implémenter `renderPreviewPattern` dans le .cpp.** Dans [src/core/LedController.cpp](../../../src/core/LedController.cpp), à la fin du fichier (après la dernière méthode), ajouter :

```cpp
// ---------------------------------------------------------------------------
// Phase 0.1 — Tool 8 preview wrapper.
// Thin pass-through to renderPattern() so ToolLedPreview can inject arbitrary
// PatternInstance values without duplicating runtime code. Does NOT consult or
// mutate _eventOverlay. Caller is responsible for owning `inst.startTime`.
// ---------------------------------------------------------------------------
void LedController::renderPreviewPattern(const PatternInstance& inst, unsigned long now) {
  renderPattern(inst, now);
}
```

- [ ] **Step 2.5 — Charger les 3 durées depuis le store dans `loadLedSettings`.** Dans [src/core/LedController.cpp](../../../src/core/LedController.cpp:869) `loadLedSettings`, à la section qui copie les timings, remplacer la ligne existante `_tickFlashDurationMs = s.tickFlashDurationMs;` (si encore présente après Step 1.7) par :

```cpp
  _tickBeatDurationMs = s.tickBeatDurationMs;
  _tickBarDurationMs  = s.tickBarDurationMs;
  _tickWrapDurationMs = s.tickWrapDurationMs;
```

Vérifier dans le même fichier que tout ancien usage de `_tickFlashDurationMs` (notamment dans le case `PTN_FLASH` / ARPEG tick rendering) utilise désormais `_tickBeatDurationMs`. Pour Phase 0.1, **ARPEG step → BEAT** est le seul dispatch actif — BAR et WRAP restent chargés en cache mais non consommés tant que la Phase 1+ LoopEngine n'arrive pas. Ne pas câbler de consommation BAR/WRAP maintenant (hors scope).

- [ ] **Step 2.6 — Câbler le default `EVT_STOP` sur `CSLOT_VERB_STOP`.** Dans [src/core/LedGrammar.cpp](../../../src/core/LedGrammar.cpp), localiser la ligne de `EVENT_RENDER_DEFAULT[EVT_STOP]`. Elle pointe actuellement vers `CSLOT_VERB_PLAY` (option γ legacy, cf. rapport Phase 0 §8.3). La remplacer par `CSLOT_VERB_STOP`. Le pattern reste `PTN_FADE`, `fgPct=100`.

Commande préalable pour repérer la ligne exacte :

Run: Grep `EVT_STOP` dans `src/core/LedGrammar.cpp`

- [ ] **Step 2.7 — Vérifier la couverture slots dans `loadColorSlots`.** Dans [src/core/LedController.cpp](../../../src/core/LedController.cpp:907) `loadColorSlots`, la boucle itère normalement sur `COLOR_SLOT_COUNT` (maintenant 16). Confirmer visuellement que le loop est bien `for (uint8_t i = 0; i < COLOR_SLOT_COUNT; ...)` ou équivalent : pas de numéro en dur à 15. Si une constante `15` dure existe, la remplacer par `COLOR_SLOT_COUNT`.

- [ ] **Step 2.8 — Retirer les commentaires stale hérités de la Phase 0 audit.** Le rapport Phase 0 §4.2 R1 mentionne des commentaires trompeurs (lignes 322-327, 410-411 de LedController.cpp à l'époque ; les numéros peuvent avoir dérivé après `16dbe8a`). Grep pour repérer :

Run: Grep `bgFactor.*is applied at setPixel intensity` dans `src/core/LedController.cpp`

Si un commentaire résiduel subsiste (suite au fix R1/R2), le laisser tel quel s'il est exact ; s'il est trompeur, le corriger en une ligne ("// bgFactor applied to FG intensity to derive BG — see renderBankNormal/Arpeg"). Hors scope : ne PAS refactorer la logique bgFactor ici.

- [ ] **Step 2.9 — Build clean.**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`. Pas d'erreur "undefined reference" sur `renderPreviewPattern`, pas d'erreur "PatternInstance is private".

- [ ] **Step 2.10 — Checkpoint commit (attendre OK utilisateur).**

Files à stager :
- `src/core/LedController.h`
- `src/core/LedController.cpp`
- `src/core/LedGrammar.cpp`

Message proposé :

```
feat(led): phase 0.1 step 2 — renderPreviewPattern wrapper + tick caches + EVT_STOP default

- LedController::PatternInstance becomes a public struct (no layout change) so
  ToolLedPreview can inject arbitrary pattern instances through the new wrapper.
- Public renderPreviewPattern(inst, now) delegates to private renderPattern().
  Zero runtime duplication — all Tool 8 previews will go through the same engine.
- Load 3 tick duration caches (_tickBeatDurationMs, _tickBarDurationMs,
  _tickWrapDurationMs) from LedSettingsStore v7. BEAT consumed now by ARPEG
  step flash; BAR/WRAP reserved for Phase 1+ LoopEngine dispatch.
- EVENT_RENDER_DEFAULT[EVT_STOP].colorSlot flips from CSLOT_VERB_PLAY to
  CSLOT_VERB_STOP, decoupling Stop fade-out color from Play fade-in color.

Runtime behaviour unchanged for existing events (bank switch, scale, octave,
HOLD capture, ARPEG tick, bgFactor BG rendering). **EVT_STOP default color
changes**: was Green (via option γ — EVT_STOP reused CSLOT_VERB_PLAY), now
Coral (CSLOT_VERB_STOP default preset 8). Visible diff: Stop fade-out renders
Coral instead of Green — this is the intended decoupling.

Phase 0.1 plan: docs/superpowers/plans/2026-04-20-tool8-ux-respec-plan.md
Spec: docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md §7.2
```

- [ ] **Step 2.11 — HW: checkpoint hardware (recommandé).**

Demander à l'utilisateur d'uploader et de valider :
1. Boot serial : 2 warnings NVS reset (ledsettings + ledcolors) si pas encore uploadé après Task 1.
2. Bank switch, scale change, octave, hold capture, tick ARPEG : **rendu inchangé** versus `0441f5a`.
3. FG/BG banks : teintes correctes (invariant bgFactor préservé).
4. **EVT_STOP : déclencher un Stop** (LEFT+double-tap sur pad bank ARPEG en lecture, ou Hold pad en Play mode). Observer le fade-out : **doit être Coral** (nouveau CSLOT_VERB_STOP) et non plus Green (ancien option γ). C'est le seul changement visuel attendu de Task 2.
5. Tool 8 ancienne version : accessible, navigation sans crash. Les valeurs affichées sont maintenant partiellement invalides (la struct a bougé), acceptable jusqu'à Task 4.

Si régression visuelle (points 2/3) → revenir sur Step 2.5 / 2.6 / 2.7 en priorité. Le rapport Phase 0 §4.2 est le référentiel. Si point 4 affiche encore Green, vérifier Step 2.6 (default mapping EVT_STOP dans LedGrammar.cpp).

---

## Task 3 — Helper `ToolLedPreview` (nouveaux fichiers)

**Why:** Isoler toute la logique preview (mockups, scheduler tempo, timer black replay, dispatch par contexte) dans un helper dédié — pour que `ToolLedSettings` reste un orchestrateur lisible et que le preview soit raisonnable indépendamment.

**Files:**
- Create: `src/setup/ToolLedPreview.h`
- Create: `src/setup/ToolLedPreview.cpp`

### Steps

- [ ] **Step 3.1 — Lire la spec §6 (Live preview) intégralement.** Les sections clés :
  - §6.2 mapping ligne → preview (obligatoire avant d'implémenter `setContext`)
  - §6.3 tempo mockup (`NvsManager::getTempoBPM()`)
  - §6.4 formule black `clamp(effect_ms × 0.50, 500, 3000)`
  - §6.6 architecture helper (signature et enum `PreviewContext`)

- [ ] **Step 3.2 — Créer `src/setup/ToolLedPreview.h`.** Contenu complet à écrire (pas de TODO) :

```cpp
#ifndef TOOL_LED_PREVIEW_H
#define TOOL_LED_PREVIEW_H

#include <stdint.h>
#include "../core/LedController.h"    // for LedController::PatternInstance
#include "../core/KeyboardData.h"     // LedSettingsStore, ColorSlotStore, RGBW

class NvsManager;  // forward

// =================================================================
// Tool 8 Live Preview Helper (Phase 0.1)
// =================================================================
// Encapsulates all preview logic for the Tool 8 LED Settings refonte.
// ToolLedSettings owns a ToolLedPreview instance, calls begin() at run()
// entry (after _leds->previewBegin()), sets the current context whenever
// the cursor moves or a value changes, calls update(now) each loop
// iteration, and calls end() before exiting the tool.
//
// No UI responsibility: the helper only writes LEDs via LedController's
// preview API + the public renderPreviewPattern wrapper. All VT100 work
// stays in ToolLedSettings.
//
// Spec: docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md §6
// =================================================================
class ToolLedPreview {
public:
  // Context types (one per kind of preview, mapped from the cursor line).
  enum PreviewContext : uint8_t {
    PV_NONE = 0,
    PV_BASE_COLOR,      // mono-FG mockup [off][BG][BG][FG][BG][BG][off][off]
    PV_EVENT_REPLAY,    // one-shot pattern with black-then-loop (§6.4 formule)
    PV_BREATHING,       // mono-FG mockup, continuous sine (FG ARPEG stopped-loaded)
    PV_WAITING,         // mono-FG mockup, continuous crossfade (WAITING quantise)
    PV_TICKS_MOCKUP,    // LOOP ticks mockup [off][tickBG][BG][tickFG][BG][BG][off][off]
    PV_BG_FACTOR,       // reuses PV_TICKS_MOCKUP (FG vs BG intensity visible)
    PV_GAMMA_TEST,      // reuses PV_TICKS_MOCKUP (multi-intensity ladder)
  };

  // Parameter packet. Caller sets only fields relevant to the context.
  // Unused fields ignored. Extensible: add members as future contexts require.
  struct Params {
    // PV_BASE_COLOR / PV_BREATHING / PV_WAITING : color for the FG LED.
    RGBW   fgColor;
    // PV_BASE_COLOR : FG intensity 0..100. (BG derives from bgFactor.)
    uint8_t fgPct;
    // PV_WAITING : destination color (crossfade source = current mode bank color).
    RGBW   crossfadeTargetColor;
    // PV_BREATHING : min%, max%, period ms.
    uint8_t breathMinPct;
    uint8_t breathMaxPct;
    uint16_t breathPeriodMs;
    // PV_EVENT_REPLAY : fully-formed PatternInstance. startTime rewritten by helper.
    //                   effectDurationMs governs the §6.4 black timer.
    LedController::PatternInstance replayInst;
    uint16_t effectDurationMs;
    // PV_TICKS_MOCKUP / PV_BG_FACTOR / PV_GAMMA_TEST :
    //   base colors for the underlying bank mockup (FG bank + BG bank),
    //   tick color (PLAY/REC/OVERDUB resolved by caller),
    //   tick durations (BEAT/BAR/WRAP),
    //   tick common FG% / BG%.
    RGBW   modeColorFg;      // foreground bank color (NORMAL/ARPEG/LOOP)
    RGBW   modeColorBg;      // background bank color (usually = modeColorFg with bgFactor)
    RGBW   tickColorActive;  // LED 3 overlay : verb of current line (PLAY / REC / OVERDUB).
                             // Driven by the cursor : if the user is editing "Tick REC color",
                             // pass the REC resolved color here so the preview flashes REC.
    RGBW   tickColorPlayBg;  // LED 1 overlay : ALWAYS resolved CSLOT_VERB_PLAY color, regardless
                             // of the cursor line. Matches runtime invariant §6.2 spec : a LOOP
                             // bank cannot be in REC/OVERDUB when in background, so tickBG is
                             // always the PLAY tint.
    uint16_t tickBeatMs;
    uint16_t tickBarMs;
    uint16_t tickWrapMs;
    uint8_t tickCommonFgPct; // tick flash FG%
    uint8_t tickCommonBgPct; // tick flash BG%
    // Which tick subdivision is currently highlighted (BEAT / BAR / WRAP).
    // 0 = BEAT, 1 = BAR, 2 = WRAP. Used by ticks mockup to schedule the right
    // period; also used by BG_FACTOR / GAMMA_TEST (pick BAR for decent pacing).
    uint8_t  activeTickKind;
    // Global shaping for BG_FACTOR / GAMMA_TEST previews.
    uint8_t  bgFactorPct;
  };

  ToolLedPreview();

  // tempoBpm snapshotted at begin() — used by PV_TICKS_MOCKUP scheduler.
  // The preview does NOT re-query tempo mid-session; a bank switch during
  // Tool 8 is impossible, so tempoBpm is stable for the tool's lifetime.
  void begin(LedController* leds, uint16_t tempoBpm);
  void end();

  void setContext(PreviewContext ctx, const Params& p);

  // Must be called every frame from ToolLedSettings::run().
  void update(unsigned long now);

private:
  // Render helpers (one per PreviewContext). Each writes LEDs via _leds->
  // previewSetPixel + previewShow.
  void renderBaseColor(unsigned long now);
  void renderBreathing(unsigned long now);
  void renderWaiting(unsigned long now);
  void renderEventReplay(unsigned long now);
  void renderTicksMockup(unsigned long now);

  // §6.4 : black_ms = clamp(effect_ms * 0.50, 500, 3000)
  static uint16_t computeBlackMs(uint16_t effectMs);

  // Mono-FG mockup writer : LEDs 0,6,7 off, 3 = FG, 1,2,4,5 = BG derived via bgFactor.
  void drawMonoFgMockup(const RGBW& fg, uint8_t fgPct, uint8_t bgFactorPct);

  LedController* _leds;
  uint16_t       _tempoBpm;
  PreviewContext _ctx;
  Params         _p;
  // One-shot replay timer: state machine {PLAYING, BLACK}.
  unsigned long  _replayPhaseStart;
  bool           _replayInBlack;
  // Ticks mockup scheduler — tracks flash start time per kind.
  unsigned long  _tickLastFire[3];   // BEAT, BAR, WRAP
  unsigned long  _tickFlashStart[3]; // 0 = not flashing
  // Frame rate cap (spec §11.2 mitigation — Tool 8 loop runs ~200 Hz via delay(5),
  // preview rendering at 50 Hz is visually sufficient and spares Core 1 from VT100
  // redraw collisions during burst arrow-key edits).
  unsigned long  _lastUpdateMs;
};

#endif // TOOL_LED_PREVIEW_H
```

- [ ] **Step 3.3 — Créer `src/setup/ToolLedPreview.cpp`.** Squelette à compléter. L'exécutant écrit chaque section complète d'un coup — pas de placeholder.

Scaffold minimum à poser en premier :

```cpp
#include "ToolLedPreview.h"
#include "../core/LedGrammar.h"
#include <Arduino.h>

ToolLedPreview::ToolLedPreview()
  : _leds(nullptr), _tempoBpm(120), _ctx(PV_NONE),
    _replayPhaseStart(0), _replayInBlack(false), _lastUpdateMs(0) {
  for (uint8_t i = 0; i < 3; i++) { _tickLastFire[i] = 0; _tickFlashStart[i] = 0; }
  // _p zeroed by default init
}

void ToolLedPreview::begin(LedController* leds, uint16_t tempoBpm) {
  _leds = leds;
  _tempoBpm = tempoBpm == 0 ? 120 : tempoBpm;
  _ctx = PV_NONE;
  _replayPhaseStart = 0;
  _replayInBlack = false;
  _lastUpdateMs = 0;
  for (uint8_t i = 0; i < 3; i++) { _tickLastFire[i] = 0; _tickFlashStart[i] = 0; }
}

void ToolLedPreview::end() {
  if (_leds) _leds->previewClear();
  _leds = nullptr;
  _ctx = PV_NONE;
}

void ToolLedPreview::setContext(PreviewContext ctx, const Params& p) {
  _ctx = ctx;
  _p = p;
  // One-shot replay: reset phase when entering / re-entering this context.
  if (ctx == PV_EVENT_REPLAY) {
    _replayPhaseStart = millis();
    _replayInBlack = false;
  }
  // Ticks mockup: reset schedulers so the flash starts cleanly.
  if (ctx == PV_TICKS_MOCKUP || ctx == PV_BG_FACTOR || ctx == PV_GAMMA_TEST) {
    unsigned long now = millis();
    for (uint8_t i = 0; i < 3; i++) { _tickLastFire[i] = now; _tickFlashStart[i] = 0; }
  }
}

void ToolLedPreview::update(unsigned long now) {
  if (!_leds) return;
  // 50 Hz cap (spec §11.2) : preview only re-renders every 20 ms.
  // Unsigned subtraction is wrap-safe for millis() rollover.
  if (_lastUpdateMs != 0 && (now - _lastUpdateMs) < 20) return;
  _lastUpdateMs = now;
  switch (_ctx) {
    case PV_BASE_COLOR:                          renderBaseColor(now);    break;
    case PV_BREATHING:                           renderBreathing(now);    break;
    case PV_WAITING:                             renderWaiting(now);      break;
    case PV_EVENT_REPLAY:                        renderEventReplay(now);  break;
    case PV_TICKS_MOCKUP:
    case PV_BG_FACTOR:
    case PV_GAMMA_TEST:                          renderTicksMockup(now);  break;
    case PV_NONE:
    default:                                     _leds->previewClear();   break;
  }
  _leds->previewShow();
}

uint16_t ToolLedPreview::computeBlackMs(uint16_t effectMs) {
  uint32_t raw = (uint32_t)effectMs * 50 / 100;  // 50% ratio
  if (raw < 500)  raw = 500;
  if (raw > 3000) raw = 3000;
  return (uint16_t)raw;
}
```

- [ ] **Step 3.4 — Implémenter `drawMonoFgMockup`.** Layout : `[off][BG][BG][FG][BG][BG][off][off]`. Code (aligné spec §6.2) :

```cpp
void ToolLedPreview::drawMonoFgMockup(const RGBW& fg, uint8_t fgPct, uint8_t bgFactorPct) {
  if (!_leds) return;
  uint8_t bgPct = (uint16_t)fgPct * bgFactorPct / 100;
  const RGBW off = {0, 0, 0, 0};
  _leds->previewSetPixel(0, off, 0);
  _leds->previewSetPixel(1, fg, bgPct);
  _leds->previewSetPixel(2, fg, bgPct);
  _leds->previewSetPixel(3, fg, fgPct);
  _leds->previewSetPixel(4, fg, bgPct);
  _leds->previewSetPixel(5, fg, bgPct);
  _leds->previewSetPixel(6, off, 0);
  _leds->previewSetPixel(7, off, 0);
}

void ToolLedPreview::renderBaseColor(unsigned long /*now*/) {
  drawMonoFgMockup(_p.fgColor, _p.fgPct, _p.bgFactorPct == 0 ? 25 : _p.bgFactorPct);
}
```

- [ ] **Step 3.5 — Implémenter `renderBreathing`.** Réutilise `LED_SINE_LUT` depuis `HardwareConfig.h` (voir §4.1 du CLAUDE.md projet : "256-entry precomputed LUT"). Période = `_p.breathPeriodMs`. Formule : phase16 = `((now - start) * 65536 / period) & 0xFFFF` → index = `phase16 >> 8` → lerp LUT[i] / LUT[i+1] selon phase16 & 0xFF. Intensité = `min + (max-min) * sineNorm / 255`. Appliquer `drawMonoFgMockup` avec cette intensité dynamique.

Code :

```cpp
void ToolLedPreview::renderBreathing(unsigned long now) {
  if (_p.breathPeriodMs == 0) {
    drawMonoFgMockup(_p.fgColor, _p.breathMinPct, _p.bgFactorPct == 0 ? 25 : _p.bgFactorPct);
    return;
  }
  uint32_t elapsed = now % _p.breathPeriodMs;
  uint32_t phase16 = (elapsed * 65536UL) / _p.breathPeriodMs;
  uint8_t idx  = (phase16 >> 8) & 0xFF;
  uint8_t frac = phase16 & 0xFF;
  uint8_t a = LED_SINE_LUT[idx];
  uint8_t b = LED_SINE_LUT[(uint8_t)(idx + 1)];
  uint8_t sineNorm = (uint8_t)(((uint16_t)a * (255 - frac) + (uint16_t)b * frac) >> 8);
  uint8_t minP = _p.breathMinPct, maxP = _p.breathMaxPct;
  if (maxP < minP) maxP = minP;
  uint8_t pct = minP + (uint16_t)(maxP - minP) * sineNorm / 255;
  drawMonoFgMockup(_p.fgColor, pct, _p.bgFactorPct == 0 ? 25 : _p.bgFactorPct);
}
```

- [ ] **Step 3.6 — Implémenter `renderWaiting`.** Crossfade continu sur mono-FG mockup entre `_p.fgColor` (couleur du mode bank actuel) et `_p.crossfadeTargetColor`. Période fixe 1500 ms (cohérent UX, non exposé Phase 0.1).

```cpp
void ToolLedPreview::renderWaiting(unsigned long now) {
  const uint16_t periodMs = 1500;
  uint32_t elapsed = now % periodMs;
  uint32_t phase16 = (elapsed * 65536UL) / periodMs;
  uint8_t idx  = (phase16 >> 8) & 0xFF;
  uint8_t frac = phase16 & 0xFF;
  uint8_t a = LED_SINE_LUT[idx];
  uint8_t b = LED_SINE_LUT[(uint8_t)(idx + 1)];
  uint8_t s = (uint8_t)(((uint16_t)a * (255 - frac) + (uint16_t)b * frac) >> 8);
  // Lerp RGBW by s/255 between fgColor (s=0) and target (s=255).
  RGBW blended;
  blended.r = (uint8_t)(((uint16_t)_p.fgColor.r * (255 - s) + (uint16_t)_p.crossfadeTargetColor.r * s) >> 8);
  blended.g = (uint8_t)(((uint16_t)_p.fgColor.g * (255 - s) + (uint16_t)_p.crossfadeTargetColor.g * s) >> 8);
  blended.b = (uint8_t)(((uint16_t)_p.fgColor.b * (255 - s) + (uint16_t)_p.crossfadeTargetColor.b * s) >> 8);
  blended.w = (uint8_t)(((uint16_t)_p.fgColor.w * (255 - s) + (uint16_t)_p.crossfadeTargetColor.w * s) >> 8);
  drawMonoFgMockup(blended, _p.fgPct == 0 ? 100 : _p.fgPct, _p.bgFactorPct == 0 ? 25 : _p.bgFactorPct);
}
```

- [ ] **Step 3.7 — Implémenter `renderEventReplay`.** State machine 2-phase (PLAYING → BLACK → PLAYING). Durée PLAYING = `_p.effectDurationMs`. Durée BLACK = `computeBlackMs(_p.effectDurationMs)`.

```cpp
void ToolLedPreview::renderEventReplay(unsigned long now) {
  uint16_t effectMs = _p.effectDurationMs == 0 ? 500 : _p.effectDurationMs;
  uint16_t blackMs  = computeBlackMs(effectMs);

  unsigned long phaseElapsed = now - _replayPhaseStart;
  if (!_replayInBlack) {
    // PLAYING : render pattern centered on mono-FG mockup. Use inst as-is.
    LedController::PatternInstance inst = _p.replayInst;
    inst.startTime = _replayPhaseStart;
    // Draw base BG/off frame first so the event renders on top.
    const RGBW off = {0, 0, 0, 0};
    for (uint8_t i = 0; i < 8; i++) _leds->previewSetPixel(i, off, 0);
    // Delegate the pattern to the runtime engine (zero duplication).
    _leds->renderPreviewPattern(inst, now);
    if (phaseElapsed >= effectMs) {
      _replayInBlack = true;
      _replayPhaseStart = now;
      // Clear to black for the pause phase.
      for (uint8_t i = 0; i < 8; i++) _leds->previewSetPixel(i, off, 0);
    }
  } else {
    // BLACK : all off.
    const RGBW off = {0, 0, 0, 0};
    for (uint8_t i = 0; i < 8; i++) _leds->previewSetPixel(i, off, 0);
    if (phaseElapsed >= blackMs) {
      _replayInBlack = false;
      _replayPhaseStart = now;
    }
  }
}
```

**Gotcha** : `renderPreviewPattern` itself writes to specific LEDs via `previewSetPixel`. The mockup layout constraint (LEDs 0,6,7 forced off) is embedded in the pattern's `ledMask` — the caller prepares `_p.replayInst.ledMask = 0b00111110` (LEDs 1-5 active) OR uses mask=0 + `_currentBank`. Pour Phase 0.1 preview, le plus simple : définir `inst.ledMask = 0b00001000` (LED 3 uniquement) pour que l'event FADE/BLINK ne s'affiche que sur la LED FG centrale. Documenter ce choix dans le commentaire de `renderEventReplay`.

- [ ] **Step 3.8 — Implémenter `renderTicksMockup`.** Layout : `[off][tickBG][BG][tickFG][BG][BG][off][off]`. LED 3 = FG bank full intensity, LEDs 2/4/5 = BG dim, LED 1 = tickBG (coul PLAY en BG), LEDs 0/6/7 = off. Scheduler tempo-driven.

BPM → tick intervals :
- BEAT period = `60000 / tempoBpm` ms.
- BAR period = 4 × BEAT.
- WRAP period = 2 bars loop = 8 × BEAT.

Pour chaque kind (BEAT/BAR/WRAP), déclencher un flash à intervalles réguliers. Utiliser `_p.activeTickKind` pour décider quelle fréquence est "active" (celle du param sous cursor) — seule active déclenche le flash sur LED 3/1.

```cpp
void ToolLedPreview::renderTicksMockup(unsigned long now) {
  // 1) Base layer : mono-mockup with FG + BG dim.
  uint8_t bgFactor = _p.bgFactorPct == 0 ? 25 : _p.bgFactorPct;
  uint8_t fgBase = 100;
  uint8_t bgBase = (uint16_t)fgBase * bgFactor / 100;
  const RGBW off = {0, 0, 0, 0};
  _leds->previewSetPixel(0, off, 0);
  _leds->previewSetPixel(1, _p.modeColorBg, bgBase);  // tickBG base
  _leds->previewSetPixel(2, _p.modeColorBg, bgBase);
  _leds->previewSetPixel(3, _p.modeColorFg, fgBase);  // tickFG base
  _leds->previewSetPixel(4, _p.modeColorBg, bgBase);
  _leds->previewSetPixel(5, _p.modeColorBg, bgBase);
  _leds->previewSetPixel(6, off, 0);
  _leds->previewSetPixel(7, off, 0);

  // 2) Tick scheduler (only the activeTickKind period is rendered).
  const uint16_t beatMs = _tempoBpm == 0 ? 500 : (uint16_t)(60000UL / _tempoBpm);
  uint32_t periodMs = 0;
  uint16_t durMs = 0;
  uint8_t  kind = _p.activeTickKind > 2 ? 0 : _p.activeTickKind;
  switch (kind) {
    case 0: periodMs = beatMs;       durMs = _p.tickBeatMs; break;
    case 1: periodMs = beatMs * 4;   durMs = _p.tickBarMs;  break;
    case 2: periodMs = beatMs * 8;   durMs = _p.tickWrapMs; break;
  }
  if (periodMs == 0) return;

  // Fire a flash when (now - _tickLastFire[kind]) >= periodMs.
  if (now - _tickLastFire[kind] >= periodMs) {
    _tickLastFire[kind]   = now;
    _tickFlashStart[kind] = now;
  }
  // If currently flashing and within duration, overlay the tick verb color on LED 3 (FG).
  // tickBG (LED 1) is always in PLAY color (spec §6.2: "tickBG reste toujours couleur PLAY").
  // Because ToolLedSettings passes _p.tickColor = current verb (PLAY/REC/OVERDUB) for LED 3
  // and the caller separately ensures modeColorBg is the PLAY tint for LED 1 context, we
  // apply tickColor to LED 3 only.
  if (_tickFlashStart[kind] != 0 && (now - _tickFlashStart[kind]) < durMs) {
    // LED 3 : verb color matching the cursor line (PLAY/REC/OVERDUB).
    _leds->previewSetPixel(3, _p.tickColorActive,
                           _p.tickCommonFgPct == 0 ? 100 : _p.tickCommonFgPct);
    // LED 1 : ALWAYS resolved CSLOT_VERB_PLAY color (runtime invariant §6.2).
    _leds->previewSetPixel(1, _p.tickColorPlayBg,
                           _p.tickCommonBgPct == 0 ? 25 : _p.tickCommonBgPct);
  } else if (_tickFlashStart[kind] != 0) {
    // Flash done, reset so it stops overlaying until next period.
    _tickFlashStart[kind] = 0;
  }
}
```

**Contrat Params → rendu** (aligné avec Step 3.2 header et spec §6.2) :
- `tickColorActive` : couleur de la verb correspondant à la ligne sous cursor. Si cursor sur `LINE_TRANSPORT_TICK_REC_COLOR`, Tool 8 passe la REC resolved color ; si cursor sur une durée (BEAT/BAR/WRAP), Tool 8 passe la couleur de la verb par défaut associée (PLAY pour BEAT, REC pour BAR, OVERDUB pour WRAP — mapping spec §4.5). Appliqué **LED 3**.
- `tickColorPlayBg` : **TOUJOURS** la couleur résolue de `CSLOT_VERB_PLAY`, indépendamment de la ligne. Appliqué **LED 1**. Matches runtime invariant §6.2 : BG bank ne peut être en REC/OVERDUB — BG bank toujours en PLAY state.

L'exécutant Task 4 sub-step 21 remplit ces deux champs pour chaque ligne tick ; l'exécutant Task 3 implémente `renderTicksMockup` exactement selon le code du Step 3.8 ci-dessus.

- [ ] **Step 3.9 — Build clean.**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`. Nouveau .o pour ToolLedPreview, aucun warning sur les conversions uint16/uint32.

**Gotcha build** : `ToolLedPreview.cpp` n'est référencé nulle part pour l'instant. PlatformIO compile tous les .cpp dans `src/`, donc il sera lié en bibliothèque mais son code sera mort (linker garbage-collection via `-ffunction-sections`/`-fdata-sections` si activé). Pas de souci. La consommation réelle arrive en Task 4.

- [ ] **Step 3.10 — Checkpoint commit (attendre OK utilisateur).**

Files à stager :
- `src/setup/ToolLedPreview.h`
- `src/setup/ToolLedPreview.cpp`

Message proposé :

```
feat(setup): phase 0.1 step 3 — ToolLedPreview helper (live preview decoupled)

New helper (~250 LOC) encapsulating all Tool 8 live preview logic:
  - mono-FG mockup (base color / breathing / waiting / event replay)
  - LOOP ticks mockup (BEAT/BAR/WRAP scheduler, tempo-driven)
  - one-shot event replay with black-timer loop (§6.4 formula)
  - 8 preview contexts dispatching to the right renderer

Uses LedController::renderPreviewPattern (public wrapper, step 2) to run
runtime patterns through zero-duplication dispatch. ToolLedSettings will own
an instance of this helper in step 4 and set its context per cursor line.

Unused until Task 4 wires it in (dead code linker-collected). Introduced as
a standalone commit so the refonte stays reviewable.

Phase 0.1 plan: docs/superpowers/plans/2026-04-20-tool8-ux-respec-plan.md
Spec: docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md §6
```

---

## Task 4 — Refonte `ToolLedSettings` (single-view 6 sections)

**Why:** Le gros morceau. Remplace les 3 pages techniques PATTERNS/COLORS/EVENTS par la structure musicien NORMAL/ARPEG/LOOP/TRANSPORT/CONFIRMATIONS/GLOBAL, introduit le nouveau paradigme d'édition (color : ←→ preset ↑↓ hue ; single numeric : ←→±10 ↑↓±1 ; multi-numeric : ←→ focus ↑↓ adjust), câble le preview via `ToolLedPreview`, et partage les timers LOOP avec Tool 6.

**Files:**
- Rewrite: `src/setup/ToolLedSettings.h`
- Rewrite: `src/setup/ToolLedSettings.cpp`
- Modify: `src/setup/SetupManager.h` (propager `PotRouter*` dans la signature `_toolLedSettings.begin(...)`)
- Modify: `src/setup/SetupManager.cpp` (idem case '8' dispatch)

**Avertissement scope** : Tool 4 est une réécriture complète (~800-1000 LOC). Elle ne se décompose pas en sub-commits compilables sans effort massif (doubler le code pour faire coexister ancien+nouveau). Le plan propose **un seul commit atomique** pour Task 4, avec la Task décomposée en sous-étapes d'authoring.

### Steps

- [ ] **Step 4.1 — Lire la spec intégralement.** Cette tâche touche §4 (Structure UI), §5 (Navigation et édition), §6 (Live preview), §7 (Impacts moteur — ce qui est déjà fait en Task 1+2), §9 (Defaults finaux), §11 (Risques). Rouvrir la spec pour chaque sous-étape — les détails (couleurs par défaut, bornes min/max par field, exceptions) sont non-négociables.

- [ ] **Step 4.2 — Réécrire `src/setup/ToolLedSettings.h`.** Remplacement complet (via Write tool, après Read du fichier existant). Structure attendue :

```cpp
#ifndef TOOL_LED_SETTINGS_H
#define TOOL_LED_SETTINGS_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "ToolLedPreview.h"

class LedController;
class SetupUI;

// =================================================================
// Tool 8 — LED Settings (Phase 0.1 respec : single-view 6 sections)
// =================================================================
// Spec: docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md
//
// Single scrollable view with 6 semantic sections, NAV+EDIT paradigm:
//   ↑↓ navigate lines (skip section titles)
//   ←→ skip section (cursor jumps to next/prev section title)
//   ENTER enter edit mode on current line
//   d reset current line to default (no confirm, immediate save)
//   q exit (or cancel edit)
//
// Edit modes (context-sensitive):
//   COLOR line     : ←→ cycle preset (14), ↑↓ cycle hue offset (-128..127)
//   Single numeric : ←→ ±10 (coarse), ↑↓ ±1 (fine)
//   Multi numeric  : ←→ focus between fields on the line, ↑↓ adjust focused value
//
// Live preview via ToolLedPreview — context set on cursor move or value change.
// =================================================================

class ToolLedSettings {
public:
  ToolLedSettings();
  void begin(LedController* leds, SetupUI* ui);
  void run();

private:
  // -------- Section model --------
  enum Section : uint8_t {
    SEC_NORMAL         = 0,
    SEC_ARPEG          = 1,
    SEC_LOOP           = 2,
    SEC_TRANSPORT      = 3,
    SEC_CONFIRMATIONS  = 4,
    SEC_GLOBAL         = 5,
    SEC_COUNT          = 6,
  };

  // Enumeration of every navigable/editable line. Order matches the visible
  // screen layout (spec §4.1). Each LineId maps to a Section + a handler that
  // paints the row, knows its edit paradigm, and dispatches to ToolLedPreview.
  // This is the single source of truth for line ordering — changing it only
  // requires updating the handler table in the .cpp.
  enum LineId : uint8_t {
    // NORMAL
    LINE_NORMAL_BASE_COLOR,
    LINE_NORMAL_FG_PCT,
    // ARPEG
    LINE_ARPEG_BASE_COLOR,
    LINE_ARPEG_FG_PCT,
    // LOOP
    LINE_LOOP_BASE_COLOR,
    LINE_LOOP_FG_PCT,
    LINE_LOOP_SAVE_COLOR,
    LINE_LOOP_SAVE_DURATION,
    LINE_LOOP_CLEAR_COLOR,
    LINE_LOOP_CLEAR_DURATION,
    LINE_LOOP_SLOT_COLOR,
    LINE_LOOP_SLOT_DURATION,
    // TRANSPORT
    LINE_TRANSPORT_PLAY_COLOR,
    LINE_TRANSPORT_PLAY_TIMING,       // multi: brightness + duration
    LINE_TRANSPORT_STOP_COLOR,
    LINE_TRANSPORT_STOP_TIMING,       // multi: brightness + duration
    LINE_TRANSPORT_WAITING_COLOR,
    LINE_TRANSPORT_BREATHING,         // multi: min% + max% + period
    LINE_TRANSPORT_TICK_COMMON,       // multi: FG% + BG%
    LINE_TRANSPORT_TICK_PLAY_COLOR,
    LINE_TRANSPORT_TICK_REC_COLOR,
    LINE_TRANSPORT_TICK_OVERDUB_COLOR,
    LINE_TRANSPORT_TICK_BEAT_DUR,
    LINE_TRANSPORT_TICK_BAR_DUR,
    LINE_TRANSPORT_TICK_WRAP_DUR,
    // CONFIRMATIONS
    LINE_CONFIRM_BANK_COLOR,
    LINE_CONFIRM_BANK_TIMING,         // multi: brightness + duration
    LINE_CONFIRM_SCALE_ROOT_COLOR,
    LINE_CONFIRM_SCALE_ROOT_TIMING,
    LINE_CONFIRM_SCALE_MODE_COLOR,
    LINE_CONFIRM_SCALE_MODE_TIMING,
    LINE_CONFIRM_SCALE_CHROM_COLOR,
    LINE_CONFIRM_SCALE_CHROM_TIMING,
    LINE_CONFIRM_OCTAVE_COLOR,
    LINE_CONFIRM_OCTAVE_TIMING,
    LINE_CONFIRM_OK_COLOR,
    LINE_CONFIRM_OK_SPARK,            // multi: on + gap + cycles
    // GLOBAL
    LINE_GLOBAL_BG_FACTOR,
    LINE_GLOBAL_GAMMA,
    // Sentinel
    LINE_COUNT,
  };

  enum UiMode : uint8_t {
    UI_NAV  = 0,  // cursor on a line, no edit
    UI_EDIT = 1,  // editing the selected line (paradigm depends on LineId)
  };

  // Edit state for multi-value lines: which field is under ←→ focus.
  uint8_t _editFocus;  // 0..N-1 per line

  // Edit backups for cancel (q) : per-Section struct copies.
  LedSettingsStore _lwkBackup;
  ColorSlotStore   _cwkBackup;
  SettingsStore    _sesBackup;  // for LOOP timers shared with Tool 6

  // Working copies loaded from NVS at run() entry.
  LedSettingsStore _lwk;
  ColorSlotStore   _cwk;
  SettingsStore    _ses;

  // Snapshot of the foreground bank's type at setup entry. Used by the WAITING
  // preview (spec §11.5) to pick the "mode color source" for the crossfade
  // (NORMAL → CSLOT_MODE_NORMAL, ARPEG → CSLOT_MODE_ARPEG, LOOP → CSLOT_MODE_LOOP).
  // Bank switch in setup mode is impossible, so this snapshot is stable for
  // the tool's lifetime — no live refresh needed.
  BankType         _setupEntryBankType;

  // Injected deps.
  LedController* _leds;
  SetupUI*       _ui;
  PotRouter*     _potRouter;  // tempo source for ToolLedPreview (Step 4.8)

  // Cursor + mode.
  LineId         _cursor;
  UiMode         _uiMode;
  bool           _nvsSaved;

  // Preview helper.
  ToolLedPreview _preview;

  // -------- Run loop helpers --------
  void loadAll();
  bool saveLedSettings();
  bool saveColorSlots();
  bool saveSettings();
  void refreshBadge();

  // Navigation.
  Section sectionOf(LineId line) const;
  LineId  firstLineOfSection(Section s) const;
  LineId  lastLineOfSection(Section s) const;
  void    cursorUp();
  void    cursorDown();
  void    cursorNextSection();
  void    cursorPrevSection();

  // Edit dispatch.
  void    enterEdit();
  void    commitEdit();   // ENTER
  void    cancelEdit();   // q in edit mode — restore backup
  void    resetLineDefault();  // d key — no confirm

  // Per-line edit paradigms.
  void    editColor(LineId line, int dx, int dy);           // ←→ preset, ↑↓ hue
  void    editSingleNumeric(LineId line, int dx, int dy);   // ←→ ±10, ↑↓ ±1
  void    editMultiNumeric(LineId line, int dx, int dy);    // ←→ focus, ↑↓ adjust

  // Field access — returns ColorSlot* / uint8_t* / uint16_t* etc for line.
  // Centralizes "which field does this line point at" — adding a new line only
  // needs an entry here + in the renderers + in enterEdit.
  ColorSlot*  colorSlotForLine(LineId line);
  // For numeric lines: return number of fields on the line + pointer to field N.
  uint8_t     numericFieldCountForLine(LineId line) const;
  // Read + write helpers (returns current, write new value) ; tuple encoded
  // with separate getter/setter indexed by fieldIdx.
  int32_t     readNumericField(LineId line, uint8_t fieldIdx) const;
  void        writeNumericField(LineId line, uint8_t fieldIdx, int32_t newVal);
  void        getNumericFieldBounds(LineId line, uint8_t fieldIdx,
                                    int32_t& minOut, int32_t& maxOut,
                                    int32_t& stepCoarseOut, int32_t& stepFineOut) const;

  // Rendering.
  void drawScreen();
  void drawHeader();
  void drawSection(Section s);
  void drawLine(LineId line, bool isCursor, bool inEdit);
  void drawDescriptionPanel();   // 3-4 lines above control bar (§5.3)
  void drawControlBar();

  // Description lookup (line → text). Spec §5.3 example provided per line.
  const char* descriptionForLine(LineId line, bool inEdit) const;

  // Preview context dispatch — called whenever cursor moves or a value changes.
  void updatePreviewContext();

  // Default-reset per line (spec §9 defaults table).
  void resetDefaultForLine(LineId line);
};

#endif // TOOL_LED_SETTINGS_H
```

Commentaire : cette structure pose **un enum LineId à plat** (40 entrées) + un enum Section orthogonal + 3 handlers d'édition polymorphes. C'est le pattern le plus compact pour représenter une vue scrollable hétérogène. Chaque line sait ce qu'elle édite via le trio `colorSlotForLine` / `numericFieldCountForLine` / `readNumericField` / `writeNumericField`. Ajouter une ligne = ajouter une entrée enum + un case dans chaque helper.

- [ ] **Step 4.3 — Écrire `src/setup/ToolLedSettings.cpp` (orchestrateur).** Sub-steps d'authoring :

  1. **Constructeur + `begin` + `refreshBadge`** : trivial, mirror Tool 6/7.
  2. **`loadAll`** : `NvsManager::loadBlob` pour `_lwk` (`LED_SETTINGS_NVS_NAMESPACE/KEY`, magic `EEPROM_MAGIC`, version `LED_SETTINGS_VERSION`) ; idem pour `_cwk` (`COLOR_SLOT_MAGIC/VERSION`) ; idem pour `_ses` (`SETTINGS_NVS_NAMESPACE`, `SETTINGS_VERSION`). Appeler les 3 validators en sortie. **Aussi charger le bank type courant** pour le WAITING preview : `BankTypeStore bts; NvsManager::loadBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2, EEPROM_MAGIC, BANKTYPE_VERSION, &bts, sizeof(bts));` puis `_setupEntryBankType = (BankType)bts.types[currentBank];` où `currentBank` est récupéré via `_potRouter->getCurrentBank()` ou équivalent (vérifier l'API existante, Tool 6/7 le font déjà). Si le load échoue, fallback `_setupEntryBankType = BANK_NORMAL`.
  3. **Section↔Line helpers** : tables statiques `SECTION_FIRST[SEC_COUNT]`, `SECTION_LAST[SEC_COUNT]` déduites à partir de l'enum LineId.
  4. **Curseur** : `cursorUp`/`cursorDown` avance/recule dans l'enum LineId, clamp aux bornes. `cursorNextSection` : `_cursor = firstLineOfSection(sectionOf(_cursor) + 1)` clamp à SEC_COUNT-1. Idem `cursorPrevSection`.
  5. **`run` loop** : squelette conforme [`setup-tools-conventions.md`](../../reference/setup-tools-conventions.md:277) §10. Important : `_leds->previewBegin()` en entrée, `_preview.begin(_leds, nvs->getTempoBPM())` après. `_preview.end()` + `_leds->previewEnd()` en sortie. `vtClear()` à l'entrée ET à la sortie. `delay(5)` en fin de boucle. Snapshot `_uiMode` pour l'exit 2-step.
  6. **Dispatch NavEvent** :
     - `NAV_UP`/`NAV_DOWN` en `UI_NAV` : cursorUp/Down + `updatePreviewContext` + `screenDirty`.
     - `NAV_LEFT`/`NAV_RIGHT` en `UI_NAV` : `cursorPrevSection`/`cursorNextSection` + preview + redraw.
     - `NAV_ENTER` en `UI_NAV` : `enterEdit()`.
     - `NAV_DEFAULTS` en `UI_NAV` : `resetLineDefault()` + save + `flashSaved`.
     - `NAV_QUIT` en `UI_NAV` : break loop.
     - `NAV_UP/DOWN/LEFT/RIGHT` en `UI_EDIT` : dispatch selon line type (color / single / multi) vers `editColor` / `editSingleNumeric` / `editMultiNumeric`. Update preview.
     - `NAV_ENTER` en `UI_EDIT` : `commitEdit()` → save + `flashSaved` + retour NAV.
     - `NAV_QUIT` en `UI_EDIT` : `cancelEdit()` → restore backup + retour NAV.
  7. **`enterEdit`** : snapshot 3 working copies dans les 3 backups, `_uiMode = UI_EDIT`, `_editFocus = 0`.
  8. **`commitEdit`** : save appropriate store(s) selon la ligne. Pour les lignes color : `saveColorSlots()`. Pour les lignes LOOP save/clear/slot duration : `saveSettings()`. Pour les autres numériques LED : `saveLedSettings()`. `flashSaved()` une seule fois par commit (§2 conventions). Retour NAV.
  9. **`cancelEdit`** : `_lwk = _lwkBackup; _cwk = _cwkBackup; _ses = _sesBackup; _uiMode = UI_NAV;` → refresh preview + redraw.
  10. **`resetLineDefault`** : table de défauts (spec §9) indexée par LineId. Écrire via `writeNumericField` ou replace `ColorSlot`. Puis save + `flashSaved`. Pas de y/n (user-accepted, cf. U9 rapport Phase 0).
  11. **`editColor`** : `ColorSlot* s = colorSlotForLine(line);` → `dx` cycle `s->presetId` (wrap sur `COLOR_PRESET_COUNT`), `dy` cycle `s->hueOffset` (step 1 normal, step 10 si `ev.accelerated`). `updatePreviewContext()`.
  12. **`editSingleNumeric`** : 1 field. `int32_t cur = readNumericField(line, 0);` → step coarse si `dx != 0`, step fine si `dy != 0`. Clamp via `getNumericFieldBounds`. `writeNumericField(line, 0, newVal)`.
  13. **`editMultiNumeric`** : `dx` avance `_editFocus` modulo `numericFieldCountForLine(line)`. `dy` ajuste la valeur du field sous focus, step fine (`dy`) ou coarse (si `ev.accelerated`).
  14. **`colorSlotForLine`** : switch géant sur LineId, retourne `&_cwk.slots[CSLOT_*]` selon la ligne. Centralise le mapping ligne→slot.
  15. **`numericFieldCountForLine` / `readNumericField` / `writeNumericField` / `getNumericFieldBounds`** : switch géant. Pour chaque ligne numérique, on décrit ses N fields, leurs pointers, leurs bornes, leurs steps. Exemple LINE_LOOP_SAVE_DURATION = 1 field, `_ses.slotSaveTimerMs`, bornes [500, 2000], step coarse 100, step fine 10. LINE_TRANSPORT_BREATHING = 3 fields (`_lwk.fgArpStopMin`, `_lwk.fgArpStopMax`, `_lwk.pulsePeriodMs`).
  16. **`drawScreen`** : boucle sur Sections (SEC_NORMAL..SEC_GLOBAL), pour chaque section : `_ui->drawSection("NORMAL")` ; pour chaque line in section : `drawLine(line, line==_cursor, _uiMode==UI_EDIT && line==_cursor)`. Puis `drawDescriptionPanel()` puis `drawControlBar()`.
  17. **`drawLine`** : switch sur LineId. Utilise `_ui->drawFrameLine(fmt, ...)`. Pour une ligne color, afficher `[preset name] +hueOffset    ▓▓` (où ▓▓ est un bloc coloré VT100 — voir `vt100-design-guide.md` si besoin). Pour une ligne multi-numeric, afficher les N fields séparés par 4 spaces et entourer d'un `>` le field sous focus en UI_EDIT.
  18. **`drawDescriptionPanel`** : 3-4 lignes bordées, texte issu de `descriptionForLine(_cursor, _uiMode==UI_EDIT)`. Panel cohérent avec §5.3 de la spec.
  19. **`drawControlBar`** : `[↑↓] nav  [←→] skip section  [ENTER] edit  [d] default  [q] exit` en NAV ; `[↑↓] adjust  [←→] ...  [ENTER] save  [q] cancel` en EDIT (texte contextuel adapté selon color / single / multi).
  20. **`descriptionForLine`** : table const `DESCRIPTION_TEXTS[LINE_COUNT]` (stocker en `static const char* const DESCRIPTION_TEXTS[LINE_COUNT] = { ... };` placé en file-scope dans le .cpp — lecture seule, flash-résident, zéro impact RAM).

     Table exhaustive à implémenter verbatim (39 entrées, respecter l'ordre de l'enum LineId) :

     ```
     LINE_NORMAL_BASE_COLOR        : "NORMAL base color — identifies NORMAL banks. FG shown at 100%, BG via bg factor."
     LINE_NORMAL_FG_PCT            : "Foreground brightness for NORMAL banks (10-100%). BG derives as FG x bg factor."
     LINE_ARPEG_BASE_COLOR         : "ARPEG base color — identifies ARPEG banks. Breathing when stopped-loaded."
     LINE_ARPEG_FG_PCT             : "Foreground brightness for ARPEG banks (10-100%). Applied when playing (solid)."
     LINE_LOOP_BASE_COLOR          : "LOOP base color — identifies LOOP banks. Consumed Phase 1+ (runtime dormant)."
     LINE_LOOP_FG_PCT              : "Foreground brightness for LOOP banks (10-100%). Consumed Phase 1+."
     LINE_LOOP_SAVE_COLOR          : "Save slot color — shown during the long-press ramp on slot pad (LOOP Phase 1+)."
     LINE_LOOP_SAVE_DURATION       : "Hold duration to trigger slot save (500-2000 ms). Shared with Tool 6."
     LINE_LOOP_CLEAR_COLOR         : "Clear loop color — shown during the long-press ramp on clear pad (LOOP Phase 1+)."
     LINE_LOOP_CLEAR_DURATION      : "Hold duration to clear a LOOP bank (200-1500 ms). Shared with Tool 6."
     LINE_LOOP_SLOT_COLOR          : "Slot delete color — visual feedback for the instant delete combo (not a hold)."
     LINE_LOOP_SLOT_DURATION       : "Visual duration of the slot delete feedback (400-1500 ms). Gesture is instant."
     LINE_TRANSPORT_PLAY_COLOR     : "Play fade-in color — flashes on Hold on or double-tap Play."
     LINE_TRANSPORT_PLAY_TIMING    : "Left/right focus: brightness (0-100) or duration (0-1000 ms). Up/down adjusts."
     LINE_TRANSPORT_STOP_COLOR     : "Stop fade-out color — flashes on Hold off or double-tap Stop."
     LINE_TRANSPORT_STOP_TIMING    : "Left/right focus: brightness (0-100) or duration (0-1000 ms). Up/down adjusts."
     LINE_TRANSPORT_WAITING_COLOR  : "Waiting quantise color — crossfades with mode color while waiting for beat/bar."
     LINE_TRANSPORT_BREATHING      : "Breathing min%, max%, period. Applies to FG ARPEG / LOOP stopped-loaded."
     LINE_TRANSPORT_TICK_COMMON    : "Tick FG% and BG% — shared flash intensity across PLAY/REC/OVERDUB ticks."
     LINE_TRANSPORT_TICK_PLAY_COLOR: "Tick PLAY color — ARPEG step flash and LOOP playing wrap tick."
     LINE_TRANSPORT_TICK_REC_COLOR : "Tick REC color — LOOP recording bar and wrap ticks (Phase 1+)."
     LINE_TRANSPORT_TICK_OVERDUB_COLOR : "Tick OVERDUB color — LOOP overdubbing wrap tick (Phase 1+)."
     LINE_TRANSPORT_TICK_BEAT_DUR  : "Tick BEAT duration (5-500 ms). Consumed now for ARPEG step flash."
     LINE_TRANSPORT_TICK_BAR_DUR   : "Tick BAR duration (5-500 ms). Consumed Phase 1+ for LOOP bar flash."
     LINE_TRANSPORT_TICK_WRAP_DUR  : "Tick WRAP duration (5-500 ms). Consumed Phase 1+ for LOOP wrap flash."
     LINE_CONFIRM_BANK_COLOR       : "Bank switch confirmation color — blinks on destination bank pad."
     LINE_CONFIRM_BANK_TIMING      : "Left/right focus: brightness (0-100) or duration (100-500 ms). Up/down adjusts."
     LINE_CONFIRM_SCALE_ROOT_COLOR : "Scale root change color — blinks on changed pads (fires group when linked)."
     LINE_CONFIRM_SCALE_ROOT_TIMING: "Left/right focus: brightness or duration (100-500 ms). Up/down adjusts."
     LINE_CONFIRM_SCALE_MODE_COLOR : "Scale mode change color — blinks on changed pads."
     LINE_CONFIRM_SCALE_MODE_TIMING: "Left/right focus: brightness or duration (100-500 ms). Up/down adjusts."
     LINE_CONFIRM_SCALE_CHROM_COLOR: "Scale chromatic toggle color — blinks on changed pads."
     LINE_CONFIRM_SCALE_CHROM_TIMING: "Left/right focus: brightness or duration (100-500 ms). Up/down adjusts."
     LINE_CONFIRM_OCTAVE_COLOR     : "Octave change color — blinks on the octave pad (ARPEG only)."
     LINE_CONFIRM_OCTAVE_TIMING    : "Left/right focus: brightness or duration (100-500 ms). Up/down adjusts."
     LINE_CONFIRM_OK_COLOR         : "Confirm OK color — universal SPARK suffix (e.g. after a Tool save)."
     LINE_CONFIRM_OK_SPARK         : "Left/right focus: on (20-200 ms), gap (20-300 ms), cycles (1-4). Up/down adjusts."
     LINE_GLOBAL_BG_FACTOR         : "BG factor (10-50%). All BG banks render as FG color x this ratio."
     LINE_GLOBAL_GAMMA             : "Master gamma (1.0-3.0). Affects perceptual LED intensity curve. Hot-reloaded."
     ```

     `descriptionForLine(line, inEdit)` : retourner `DESCRIPTION_TEXTS[line]` en NAV. En EDIT, optionnellement suffixer par un hint contextuel (ex. `"  Preview may glitch during fast edits."` pour les burst warnings §6.5) — laissé à la discrétion de l'exécutant, non obligatoire.
  21. **`updatePreviewContext`** : switch géant LineId → `ToolLedPreview::Params` + `PreviewContext`. Exemples :
      - `LINE_NORMAL_BASE_COLOR` → `PV_BASE_COLOR`, `fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_NORMAL])`, `fgPct = _lwk.normalFgIntensity`, `bgFactorPct = _lwk.bgFactor`.
      - `LINE_TRANSPORT_PLAY_COLOR` → `PV_EVENT_REPLAY` avec PatternInstance FADE 0→brightness durée holdOnFadeMs.
      - `LINE_TRANSPORT_WAITING_COLOR` → `PV_WAITING`. **Source color** : résolue depuis `_setupEntryBankType` snapshot au loadAll : `fgColor = resolveColorSlot(_cwk.slots[_setupEntryBankType == BANK_ARPEG ? CSLOT_MODE_ARPEG : (/*BANK_LOOP*/ false ? CSLOT_MODE_LOOP : CSLOT_MODE_NORMAL)])` (la branche LOOP est dormante Phase 0.1 — gardée pour future Phase 1+). **Target color** : `crossfadeTargetColor = resolveColorSlot(_cwk.slots[CSLOT_VERB_PLAY])` (WAITING précède PLAY dans le flow de transport). `bgFactorPct = _lwk.bgFactor`. Le preview rend un mockup mono-FG qui crossfade continu entre la couleur du mode et la couleur PLAY — c'est la sémantique runtime §11.5.
      - `LINE_TRANSPORT_BREATHING` → `PV_BREATHING` avec min/max/period lus depuis `_lwk.fgArpStopMin/Max` + `_lwk.pulsePeriodMs`.
      - `LINE_TRANSPORT_TICK_BEAT_DUR` → `PV_TICKS_MOCKUP` avec `activeTickKind=0` (BEAT).
      - `LINE_GLOBAL_BG_FACTOR` / `LINE_GLOBAL_GAMMA` → `PV_BG_FACTOR` / `PV_GAMMA_TEST`, le bgFactorPct courant (ou gamma) est reflété via les intensités du mockup.
  22. **`resetDefaultForLine`** : table du spec §9 exhaustive. Une entrée par line. Pour les lines color : `ColorSlot{preset, hue}`. Pour les numériques : valeur typée.

  Cette liste d'authoring est volontairement détaillée. Chaque sub-step = 30-80 LOC. Total estimé ~900 LOC pour le .cpp + ~150 pour le .h.

- [ ] **Step 4.4 — Gamma hot-reload.** Spec §3 décision 9 : "Master gamma hot-reloadable confirmé (plus de reboot only)". Dans `commitEdit` pour `LINE_GLOBAL_GAMMA`, après `saveLedSettings()`, appeler `_leds->rebuildGammaLut(_lwk.gammaTenths)` pour appliquer immédiatement sans reboot. Noter en commentaire "// Phase 0.1 : gamma hot-reload — rebuildGammaLut is safe runtime (just recomputes a 256-entry LUT)".

- [ ] **Step 4.5 — `_lwk` → runtime reload.** Après save LED settings, appeler `_leds->loadLedSettings(_lwk)` pour que les valeurs éditées (brightness FG, durées flash, etc.) prennent effet sans reboot. Idem après save color slots : `_leds->loadColorSlots(_cwk)`. Tools 6/7 existants le font déjà, mirror le pattern.

- [ ] **Step 4.6 — Tool 6 ↔ Tool 8 partage LOOP timers (spec §9 & §10.7).** Les 3 fields `clearLoopTimerMs / slotSaveTimerMs / slotClearTimerMs` vivent dans `SettingsStore` (édités par Tool 6 et Tool 8). Vérifier que Tool 6 **charge** `SettingsStore` à chaque entrée, donc pas de conflit si Tool 8 sauve puis Tool 6 re-édite. Aucun changement de Tool 6 nécessaire ; par contre l'exécutant doit s'assurer que `_ses` dans Tool 8 est bien **rechargé** à chaque `run()` entrée (sinon stale si Tool 6 a écrit entre-temps). Pattern standard `loadAll()` au début de `run()`.

- [ ] **Step 4.7 — Nettoyage : Tool 8 ancienne API.** L'ancien header avait 3 enum Page + 4 enum sub-state + 15 méthodes internes spécifiques aux 3 pages. Le nouveau header les supprime tous (puisque c'est une réécriture complète via Write). Vérifier après le Write qu'aucun consommateur externe de ces symboles n'existe (Tool 8 est en black-box, consommé uniquement par `SetupManager`). Grep :

Run: Grep pattern `ToolLedSettings::` dans `src/`
Expected: seules les méthodes du nouveau header apparaissent.

- [ ] **Step 4.8 — `SetupManager` : injecter `PotRouter*` pour le tempo.** Le tempo courant est exposé par **`PotRouter::getTempoBPM()`** ([src/managers/PotRouter.cpp:638](../../../src/managers/PotRouter.cpp:638)), pas par `NvsManager` (qui ne fait que persister via `queueTempoWrite()` en écoutant PotRouter). La spec §6.3 évoque erronément `NvsManager::getTempoBPM()` — prendre `PotRouter::getTempoBPM()` comme source canonique ; signaler la correction éventuelle de la spec hors scope de ce plan.

Signature de `ToolLedSettings::begin` à adopter :

```cpp
void ToolLedSettings::begin(LedController* leds, SetupUI* ui, PotRouter* potRouter);
```

Membre privé à ajouter dans `ToolLedSettings.h` (Step 4.2) : `PotRouter* _potRouter;`.

Modifications dans `SetupManager` :

1. **`src/setup/SetupManager.h`** — la déclaration `void begin(...)` qui chaîne l'injection vers `_toolLedSettings.begin(...)` doit désormais propager `PotRouter*`. Mirror Tool 7 ([src/setup/SetupManager.cpp](../../../src/setup/SetupManager.cpp) `case '7'` où `_toolPotMapping.begin(..., potRouter)` est appelé — reproduire le même pattern pour Tool 8).
2. **`src/setup/SetupManager.cpp`** — `case '8'` dispatch : remplacer `_toolLedSettings.begin(_leds, &_ui);` par `_toolLedSettings.begin(_leds, &_ui, potRouter);` (nom de la variable à vérifier : si le handle est `_potRouter` comme membre, utiliser `_potRouter` ; si injecté via `begin()` local, utiliser le paramètre).

3. **`main.cpp`** — si `SetupManager::begin(...)` ne prenait pas déjà `PotRouter*`, **ne pas le modifier** ici : Tool 7 fournit déjà l'injection. L'exécutant doit ouvrir SetupManager.{h,cpp}, localiser comment `potRouter` arrive jusqu'à `case '7'`, et le réutiliser dans `case '8'` sans bouger l'API publique. Si `potRouter` est stocké comme membre `_potRouter` dans SetupManager, aucun changement de signature externe.

Dans `ToolLedSettings::run()`, l'appel `_preview.begin(_leds, _potRouter->getTempoBPM())` remplace la formulation du Step 4.3 sub-step 5.

La convention §3.2 (setup-tools-conventions) — comet restart après Tool 8 — reste respectée, rien à changer de ce côté.

- [ ] **Step 4.9 — Build clean.**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`. Aucun "undefined reference", aucun "unused variable" sur le nouveau header.

- [ ] **Step 4.10 — Self-audit avant commit.** Relire la spec §4 + §5 + §6 et cocher mentalement :
  - [ ] Toutes les lignes du mockup §4.1 sont présentes dans l'enum LineId (compter 40 ± 2).
  - [ ] Le panel description a du texte pour chaque ligne (pas de "TODO").
  - [ ] Les 3 paradigmes d'édition (color / single / multi) sont implémentés et testables.
  - [ ] Les defaults §9 sont tous encodés dans `resetDefaultForLine`.
  - [ ] `_preview` est `begin`/`end` correctement + setContext appelé sur tout mouvement cursor ou value change.
  - [ ] Gamma hot-reload est câblé (Step 4.4).
  - [ ] Le partage Tool 6 ↔ Tool 8 sur les LOOP timers ne casse pas Tool 6 (load au run entry).
  - [ ] Conventions §1 : save uniquement sur ENTER commit + `d` default. Arrows ne sauvent pas.
  - [ ] Conventions §2 : `flashSaved()` une fois par commit.
  - [ ] Conventions §10 : `_ui->vtClear()` entrée et sortie, `delay(5)` en fin de boucle.
  - [ ] Labels user-facing traduits (spec §4.4) — `CSLOT_VERB_PLAY` → "Play fade-in" etc.

- [ ] **Step 4.11 — HW: checkpoint hardware obligatoire.** Demander à l'utilisateur d'uploader et de tester **exhaustivement** :

  1. **Navigation** : ↑↓ parcourt les 40 lignes, saute les titres de section. ←→ jump d'une section à l'autre. Curseur visible (inverse video).
  2. **Sections visibles** : les 6 sections s'affichent correctement avec bordures Unicode.
  3. **Panel description** : chaque ligne affiche une description utile quand le curseur se pose dessus.
  4. **Paradigme COLOR** (LINE_NORMAL_BASE_COLOR p.ex.) :
     - ENTER → edit mode. ←→ cycle preset (observer le nom changer). ↑↓ cycle hue (observer la couleur glisser sur le preview LEDs).
     - ENTER → save + badge "SAVED" brièvement. Retour NAV.
     - q en edit → restore backup (la valeur visible repasse à ce qu'elle était avant enterEdit).
  5. **Paradigme SINGLE numeric** (LINE_NORMAL_FG_PCT p.ex.) :
     - ←→ ±10 (observer la valeur bondir). ↑↓ ±1 (observer l'ajustement fin).
     - Preview reflète la brightness live (LED FG plus/moins intense).
  6. **Paradigme MULTI numeric** (LINE_TRANSPORT_PLAY_TIMING ou LINE_CONFIRM_OK_SPARK p.ex.) :
     - ←→ change le field sous focus (observer le ">" se déplacer).
     - ↑↓ ajuste le field focusé.
     - ENTER save puis retour NAV.
  7. **Reset default (d)** sur n'importe quelle ligne : la valeur repasse au default spec §9, save immédiat, pas de prompt.
  8. **Live preview** :
     - LINE_NORMAL_BASE_COLOR → strip mockup mono-FG s'affiche en temps réel avec la couleur NORMAL.
     - LINE_TRANSPORT_PLAY_COLOR → animation FADE 0→100 + replay après black.
     - LINE_TRANSPORT_STOP_COLOR → animation FADE 100→0 Coral (distincte de PLAY Green).
     - LINE_TRANSPORT_WAITING_COLOR → crossfade continu entre la couleur du bank FG au moment de l'entrée setup (NORMAL = Warm White, ARPEG = Ice Blue) et la couleur PLAY (Green). Vérifier que la couleur source change si on entre le setup depuis une bank NORMAL vs ARPEG.
     - LINE_TRANSPORT_BREATHING → LED 3 respire.
     - LINE_TRANSPORT_TICK_BEAT_DUR → strip LOOP ticks + LED 3 flashe à la fréquence BPM.
     - LINE_GLOBAL_BG_FACTOR → modification visible de l'écart FG/BG sur le mockup ticks.
     - LINE_GLOBAL_GAMMA → progression tonale visible quand on passe de 1.0 à 3.0 (échelonnage des intensités).
  9. **Gamma hot-reload** : ajuster gamma, ENTER save → les autres Tools (test : revenir au menu, puis entrer Tool 6) ont déjà le nouveau gamma appliqué, pas besoin de reboot.
  10. **Sortie `q`** : `q` en NAV → retour au menu principal, comet reprend.
  11. **LOOP timers Tool 6 ↔ Tool 8** : dans Tool 8, modifier `slotSaveTimerMs` à 1200. Sortir. Entrer Tool 6. Le slotSaveTimerMs affiché est bien 1200.
  12. **Burst arrow-key stress test** (mitigation perf §11.2) : sur une ligne multi-numeric (LINE_TRANSPORT_PLAY_TIMING p.ex.), en EDIT mode, maintenir ↑ pendant 2-3 secondes puis relâcher. Attendu : le preview continue à rendre de façon fluide (pas de freeze VT100 > 100 ms, pas de perte d'input serial), la valeur affichée finale correspond à la dernière touche relâchée, le rate-cap 50 Hz tient. Si freeze > 500 ms → hausser le cap à 30 Hz (~33 ms) dans `ToolLedPreview::update` et retester.

Si n'importe lequel de ces 12 points échoue, **ne pas committer**. Revenir sur le step concerné et corriger.

- [ ] **Step 4.12 — Checkpoint commit (attendre OK utilisateur).**

Files à stager :
- `src/setup/ToolLedSettings.h`
- `src/setup/ToolLedSettings.cpp`
- `src/setup/SetupManager.h`
- `src/setup/SetupManager.cpp`

Message proposé :

```
feat(setup): phase 0.1 step 4 — Tool 8 UX respec (single-view 6 sections)

Complete rewrite of Tool 8 LED Settings, from 3 technical pages (PATTERNS /
COLORS / EVENTS) to a single scrollable view organized around musician-facing
semantics:
  [NORMAL] [ARPEG] [LOOP] [TRANSPORT] [CONFIRMATIONS] [GLOBAL]

New navigation paradigm (to be codified in setup-tools-conventions §4):
  NAV:  up/down walks lines, left/right skips section, ENTER edits, d resets
  EDIT: context-sensitive — color = left/right preset + up/down hue ; single
        numeric = left/right ±10 + up/down ±1 ; multi numeric = left/right
        focus between fields + up/down adjusts the focused value

Live preview handled by ToolLedPreview helper (step 3). Zero duplication of
runtime rendering: all patterns go through LedController::renderPreviewPattern.

LOOP duration timers (clearLoop/slotSave/slotClear) are now editable both from
Tool 6 and Tool 8 — same field in SettingsStore, two UIs write it, no new store.

Gamma is now hot-reloadable (rebuildGammaLut called on commit) — no reboot
required. All other settings already were.

Labels translated from technical (CSLOT_VERB_PLAY) to musician-facing ("Play
fade-in"). C++ enum identifiers unchanged.

Phase 0.1 plan: docs/superpowers/plans/2026-04-20-tool8-ux-respec-plan.md
Spec: docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md §4 §5 §6 §9
```

---

## Task 5 — Sync docs reference

**Why:** Une briefing stale est un bug (architecture-briefing.md §0 keep-in-sync protocol F). Les 3 docs référencent des patterns / store versions qui viennent de changer.

**Files:**
- Modify: `docs/reference/setup-tools-conventions.md`
- Modify: `docs/reference/architecture-briefing.md`
- Modify: `docs/reference/nvs-reference.md` (Task 1 l'a déjà partiellement mis à jour — finaliser ici)

### Steps

- [ ] **Step 5.1 — setup-tools-conventions : ajouter §4.4 paradigme "geometric visual navigation" + flagger la divergence avec §4.3.** Dans [docs/reference/setup-tools-conventions.md](../../reference/setup-tools-conventions.md), cette étape fait **deux** modifications liées :

**A. Ajouter §4.4 après §4.3 Field editor (ligne ~118)** :

```markdown
### 4.4 Multi-value row — geometric visual navigation (Phase 0.1 — Tool 8 canonical)

When a single visual row carries **2+ distinct numeric fields** side by side
(e.g. `brightness 100 %  duration 500 ms`), use the following paradigm:

- `←→` moves **focus** horizontally between the fields on the row (no value change).
- `↑↓` **adjusts** the value of the field currently under focus.
  - Default step: ±1 fine. `ev.accelerated` → ±10 coarse.
  - Each field has its own range + step, looked up via a per-line metadata table.
- `ENTER` commits all fields on the row (one `flashSaved`) and returns to NAV.
- `q` cancels, restoring the full row's pre-edit values from a snapshot backup.

**Geometric principle** (why ←→ = focus, ↑↓ = adjust, contrary to §4.3):
arrows follow the **visual layout of the row**. The row is laid out
horizontally, so horizontal arrows traverse it horizontally (between fields).
Vertical arrows are reserved for "amplitude of value" — the universal
vertical-as-magnitude intuition (up = more, down = less). This gives a
**single mental model across every edit mode in Tool 8**: vertical arrows
always adjust, regardless of paradigm (color, single, multi).

**Exception — color row `[preset] +hue`**: the 2 components belong to the same
logical entity (color = preset + hue shift), so there is no "focus" to cycle.
All 4 arrows act simultaneously on the same entity :
- `←→` cycles the preset (traverse the preset pool).
- `↑↓` cycles the hue offset (adjust magnitude of the hue shift).

Vertical-as-magnitude still holds. Horizontal as "alternate dimension of the
same concept" (preset family) is the degenerate case.

Reference implementation: [`ToolLedSettings`](../../src/setup/ToolLedSettings.cpp)
(Tool 8, Phase 0.1 respec 2026-04-20).
```

**B. Flagger la divergence avec §4.3 Field editor.** Le pattern §4.3 (`←→` adjust, `↑↓` field) est l'inverse de §4.4. Cette divergence est **intentionnelle** mais doit être documentée pour éviter la confusion. Ajouter en fin de §4.3 (juste avant "Reference : [`ToolSettings`]...") :

```markdown
**Note — relation with §4.4**: §4.3 uses `↑↓` for field navigation (vertical
list of fields) and `←→` for value adjust. §4.4 uses the opposite mapping
because fields are laid out **horizontally on one row** rather than stacked
vertically. Both are "arrows follow the visual layout" — the layout differs.
Tool 6 currently uses §4.3 for its continuous fields. If a Tool 6 refactor
revisits field layout (unlikely Phase 0.1), it should adopt §4.4 only if the
fields become multi-column. Otherwise §4.3 remains canonical for vertical lists.
```

**C. Ajouter une note sous §6.1 (Reserved keys) pour l'exception `d` sans confirm.** Le tableau §6.1 définit `d, D` = "Reset to defaults (with y/n confirm)". Tool 8 Phase 0.1 ne demande pas de confirm (décision spec §5.4 + rapport Phase 0 §4.3 U9 user-validated). Ajouter après le tableau §6.1 :

```markdown
**Exception — Tool 8 Phase 0.1** : `d` resets the current line to its default
**without y/n confirm** and commits immediately to NVS. The reset is
line-scoped (one numeric field or one color slot), not tool-wide, so the blast
radius is minimal. This deviation is user-validated (rapport Phase 0 §4.3 U9).
Any future tool needing tool-wide `d` reset must restore the y/n confirm.
```

- [ ] **Step 5.2 — architecture-briefing : MM7 + §4 Table 1 + §8 LEDs.** Dans [docs/reference/architecture-briefing.md](../../reference/architecture-briefing.md) :

  - §1 **MM7** (ligne ~86) : remplacer la dernière phrase sur la grammaire 3 couches par :

    > The 3-layer LED grammar (patterns × color slots × event mapping, see P5) is the single source of truth for all visual events since Phase 0 refactor 2026-04-19 ; Phase 0.1 respec (2026-04-20) adds `CSLOT_VERB_STOP` (16th slot) and 3 tick durations (BEAT/BAR/WRAP) without changing the grammar structure. **Don't**: design a new visual by writing pixels — declare a pattern + color slot + event entry first.

  - §4 Table 1 (ligne ~322) : mettre à jour la ligne LedSettingsStore :

    ```
    | LED pattern globals + event overrides + intensities + gamma | `LedSettingsStore` v7 | Tool 8 ToolLedSettings (single-view 6 sections: NORMAL / ARPEG / LOOP / TRANSPORT / CONFIRMATIONS / GLOBAL) | `illpad_lset` / `settings` | Nouveau event → `EventId` + `EVENT_RENDER_DEFAULT[]` + optional line in Tool 8 LineId enum ; nouveau global pattern param → field in v7 Store + line in Tool 8 + apply dans `renderPattern()` |
    ```

    Et la ligne ColorSlotStore :

    ```
    | Color slots (14 preset + hue) | `ColorSlotStore` v5 | Tool 8 ToolLedSettings | `illpad_lset` / `colors` | Ajouter un slot → `COLOR_SLOT_COUNT` + Tool 8 LineId (nouvelle ligne COLOR) + `resolveColorSlot()` + default dans NvsManager.cpp |
    ```

  - §5 **P5 Event overlay** (ligne ~380) : mentionner que les tunables par line passent par le nouveau paradigme d'édition. Ajouter en fin de section :

    > Phase 0.1 note : Tool 8 now uses a single scrollable view with 6 sections
    > and a new horizontal-focus edit paradigm (see [setup-tools-conventions §4.4](setup-tools-conventions.md#44-multi-value-row-with-horizontal-focus-phase-01--tool-8-canonical)).
    > Live preview is decoupled into [`ToolLedPreview`](../../src/setup/ToolLedPreview.h)
    > which routes pattern previews through `LedController::renderPreviewPattern` —
    > zero runtime duplication.

  - §5 **P14 LED preview during tool editing** (ligne ~487) : compléter avec :

    > Phase 0.1 : Tool 8 ships a richer preview via the helper
    > [`ToolLedPreview`](../../src/setup/ToolLedPreview.h). Tool 8 owns it, calls
    > `begin(leds, tempoBpm)` at `run()` entry and `setContext(ctx, params)` on
    > cursor / value change. The helper encapsulates mono-FG mockup, LOOP-ticks
    > mockup (tempo-synced), one-shot replay, breathing, and crossfade. Pattern
    > rendering dispatches to `LedController::renderPreviewPattern`.

  - §8 **LEDs entry** (ligne ~548) : actualiser la description :

    ```
    | **LEDs** | `LedController::update` | 9-level priority ladder + event overlay. Event grammar : `triggerEvent(EventId, ledMask)` -> `renderPattern(_eventOverlay, now)`. Tick ARPEG uses shared `renderFlashOverlay()`. See `LedGrammar.h` for palette. Preview wrapper `renderPreviewPattern(inst, now)` is the public entry for Tool 8 via `ToolLedPreview`. |
    ```

- [ ] **Step 5.3 — nvs-reference : finalize table + validator note.** Task 1 a déjà mis à jour `ColorSlotStore` et `LedSettingsStore` dans la "Store Struct Catalog". Vérifier :

  - Le paragraphe "How To Add a New Namespace" n'a pas besoin d'ajustement.
  - La ligne "validateLedSettingsStore" dans la table "Validation Functions" mentionne bien `tickBeat/Bar/Wrap durations [5..500]` (ajouté en Step 1.8).

  Ajouter un paragraphe "Phase 0.1 notes" à la fin du fichier :

  ```markdown
  ---

  ## Phase 0.1 Notes (2026-04-20)

  - `ColorSlotStore` v4 → v5 : +`CSLOT_VERB_STOP=15`, `COLOR_SLOT_COUNT=16`.
  - `LedSettingsStore` v6 → v7 : rename `tickFlashDurationMs` (uint8) →
    `tickBeatDurationMs` (uint16), +`tickBarDurationMs`, +`tickWrapDurationMs`.
  - Both bumps deliberately grouped into a single commit so users see **one**
    reset cycle (two warnings), not two. Per Zero-Migration Policy, users re-enter
    their LED preferences via Tool 8 first run.
  - LOOP bar/wrap tick durations are stored now (layout stability) but consumed
    by runtime only in Phase 1+ (LoopEngine).
  ```

- [ ] **Step 5.4 — Build clean + grep exhaustif.** Aucune compilation requise (docs only), mais vérifier :

Run: Grep pattern `COLOR_SLOT_COUNT\s*=?\s*15|LED_SETTINGS_VERSION\s*=?\s*6` dans `docs/`
Expected: 0 occurrence (sauf dans un historique de commit ou README archivé).

Run: Grep pattern `tickFlashDurationMs` dans `docs/`
Expected: 0 occurrence (sauf dans le rapport Phase 0 ou specs datées).

Si les references docs listent l'ancien état dans des contextes historiques (ex. plan Phase 0 archivé), c'est acceptable — laisser. Seules les refs prescriptives (briefing + conventions + nvs-reference) doivent être à jour.

- [ ] **Step 5.5 — Checkpoint commit final (attendre OK utilisateur).**

Files à stager :
- `docs/reference/setup-tools-conventions.md`
- `docs/reference/architecture-briefing.md`
- `docs/reference/nvs-reference.md`

Message proposé :

```
docs(reference): phase 0.1 step 5 — sync briefing + conventions + nvs-reference

- setup-tools-conventions.md §4.4: new canonical paradigm "multi-value
  horizontal focus" (left/right focus between fields on a row, up/down
  adjust the focused value), plus the color-row exception (left/right
  preset, up/down hue, no focus). Reference implementation: Tool 8.
- architecture-briefing.md §1 MM7, §4 Table 1, §5 P5 + P14, §8 LEDs:
  updated for Phase 0.1 respec (CSLOT_VERB_STOP, BEAT/BAR/WRAP durations,
  ToolLedPreview helper, single-view Tool 8).
- nvs-reference.md: finalize LedSettingsStore v7 + ColorSlotStore v5 entries
  and add a Phase 0.1 notes paragraph.

Phase 0.1 is now closed: runtime grammar preserved, Tool 8 rebuilt for
musicians, zero dead code, zero drift between runtime and preview.

Phase 0.1 plan: docs/superpowers/plans/2026-04-20-tool8-ux-respec-plan.md
```

---

## Self-Review Checklist (meta, second pass)

Première passe (rédaction initiale) a **manqué** les 2 bloquants F1 et F2 de l'audit externe. Cette seconde passe est exécutée avec les 12 findings de l'audit en main pour vérifier l'intégration.

**Bloquants F1 + F2** :
- [x] F1 — rename `tickFlashDurationMs` propagé en Task 1 Step 1.7 avec les 5 sites TLS v0.8e listés par ligne (276 / 317 / 320 / 379 / 713). Staging Step 1.10 inclut inconditionnellement `src/setup/ToolLedSettings.cpp`. Files: Task 1 listent désormais LedController.h/cpp + TLS.cpp sans conditionnel.
- [x] F2 — `PotRouter::getTempoBPM()` adopté comme source canonique (confirmé via grep : l'API est bien sur PotRouter). Signature `ToolLedSettings::begin(LedController*, SetupUI*, PotRouter*)` adoptée, membre `_potRouter`, SetupManager.{h,cpp} dans Files de Task 4 et staging Step 4.12 inconditionnel. Note explicite que la spec §6.3 évoque erronément `NvsManager::getTempoBPM()` — on prend `PotRouter::getTempoBPM()`.

**Secondaires F3-F11** :
- [x] F3 — Task 1 Files list alignée avec Step 1.10 staging (5 fichiers + doc).
- [x] F4 — Step 2.10 commit msg reformulé : "EVT_STOP default changes: was Green (option γ), now Coral (CSLOT_VERB_STOP)". Step 2.11 HW ajoute point 4 "déclencher un Stop → observer fade-out Coral".
- [x] F5 — Task 5.1 section A + B + C. Justification géométrique explicite ("les flèches suivent le layout visuel") avec l'argument utilisateur "vertical-as-magnitude tient dans tous les paradigmes Tool 8". Divergence §4.3 ↔ §4.4 flaggée en fin de §4.3.
- [x] F6 — `_setupEntryBankType` ajouté en Step 4.2 (header), loadAll amendé en sub-step 2 pour lire `BankTypeStore.types[currentBank]`. Exemple WAITING détaillé en sub-step 21 avec résolution explicite du mode color source + target PLAY. HW test 4.11 point 8 ajoute sous-point WAITING mentionnant l'observation depuis NORMAL vs ARPEG.
- [x] F7 — Step 1.11 reformulé "Sous réserve de la propagation du rename dans TLS v0.8e (Step 1.7 sites 3)".
- [x] F8 — Sub-step 4.3.20 embed la table exhaustive (39 descriptions one-liners). L'exécutant n'a plus de jugement à faire sur le texte. Sub-step 21 reste guidance (6 exemples exhaustifs du point de vue sémantique : BASE / PLAY / WAITING / BREATHING / TICK / GLOBAL couvrent les 7 PreviewContext — le reste est variation d'index).
- [x] F9 — Task 5.1 section C ajoute la note d'exception sous §6.1 avec scope (line-scoped, pas tool-wide) et condition (tool-wide future tool doit restaurer y/n confirm).
- [x] F10 — Time-gate 50 Hz intégré dans ToolLedPreview Step 3.3 (update() early return si < 20 ms). Step 4.11 HW ajoute point 12 "Burst arrow-key stress test" avec critère d'acceptation + fallback 30 Hz si freeze.
- [x] F11 — SetupManager.{h,cpp} dans Task 4 Files list + Step 4.12 staging, inconditionnels.

**Spec coverage** (re-vérifié §1-§12, aucun gap résiduel) — §4.3 Ticks ARPEG/LOOP dispatch : confirmé que Phase 0.1 câble BEAT uniquement (ARPEG step), BAR/WRAP restent chargés mais dormants — explicité Step 2.5.

**Placeholder scan** — aucun "TBD/TODO/fill in later" dans les steps. Les 2 tables exhaustives (39 descriptions + 22 sub-steps d'authoring Task 4.3) sont déterministes.

**Type consistency** (re-vérifié) :
- `CSLOT_VERB_STOP=15`, `COLOR_SLOT_COUNT=16` cohérents Task 1 → 2 → 4.
- `tickBeatDurationMs` (uint16_t) cohérent Task 1.3 (decl) → 1.7 (TLS rename cast uint16_t) → 2.3 (member type uint16_t) → 2.5 (copy) → Task 4 (LINE_TRANSPORT_TICK_BEAT_DUR metadata).
- `LedController::PatternInstance` public Task 2.1 → Task 3 struct Params member → Task 3 Step 3.7 renderPreviewPattern delegation.
- `ToolLedPreview::Params::tickColorActive` + `tickColorPlayBg` : ces 2 fields sont présents dans le header Step 3.2 (à confirmer à l'exécution — l'édition précédente a laissé la correction inline dans la Self-Review historique. **Je confirme ci-dessous par une correction directe dans Step 3.2**).
- `_potRouter*` (PotRouter) cohérent Task 4 Step 4.2 (header member) → Step 4.3 sub-step 5 (begin signature) → Step 4.8 (SetupManager injection).
- `_setupEntryBankType` cohérent Task 4 Step 4.2 (header member) → sub-step 2 (loadAll) → sub-step 21 (WAITING dispatch) → HW 4.11 point 8.

**Correction inline confirmatoire pour Step 3.2 header Params**: le champ initial `RGBW tickColor;` doit être remplacé par les **2 champs** :
```cpp
    RGBW   tickColorActive;  // LED 3 overlay : verb of current line (PLAY/REC/OVERDUB)
    RGBW   tickColorPlayBg;  // LED 1 overlay : ALWAYS resolved CSLOT_VERB_PLAY color
```
Et `renderTicksMockup` (Step 3.8) utilise `_p.tickColorActive` pour LED 3 et `_p.tickColorPlayBg` pour LED 1. L'exécutant Task 3 applique cette correction en écrivant le header Step 3.2 — si la version précédente du plan listait encore `tickColor` seul, **ignorer** au profit de cette version.

**Conclusion** : les 2 bloquants sont fixés de façon à garantir le build clean entre chaque commit (Task 1 → 2 → 3 → 4 → 5 tous compilables individuellement). Les 10 findings secondaires sont intégrés. La première passe avait manqué F1/F2 faute de grep préalable sur les consommateurs runtime ; le correctif est structurel (Files list + staging list inconditionnels, API dependencies vérifiées par grep).

---

## Execution Handoff

**Plan complet et sauvegardé dans `docs/superpowers/plans/2026-04-20-tool8-ux-respec-plan.md`. Deux options d'exécution :**

**1. Subagent-Driven (recommandé)** — Fresh subagent par tâche, review entre chaque, itération rapide. Adapté ici car 5 tasks indépendantes dans leur portée fichier (Task 1 = NVS/Stores, Task 2 = LedController, Task 3 = nouveau helper, Task 4 = refonte gros, Task 5 = docs).

**2. Inline Execution** — Exécution dans cette session via `superpowers:executing-plans`, checkpoints batchés. Adapté si tu veux garder le contrôle fin entre chaque step et surtout pendant Task 4 (rewrite critique avec 11 points de validation hardware).

**Quelle approche ?**
