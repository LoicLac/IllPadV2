# Plan Phase 0 — Refactor LED isolé

**Date** : 2026-04-19
**Statut** : brouillon d'implémentation, à valider avant exécution
**Scope** : refactor complet du système LED pour intégrer la grammaire unifiée de la LED spec. Aucune feature LOOP ajoutée à ce stade.
**Prérequis lecture** :
- [`2026-04-19-led-feedback-unified-design.md`](../specs/2026-04-19-led-feedback-unified-design.md) — grammaire LED (palette 9 patterns, 16 color slots, event mapping)
- [`2026-04-19-loop-mode-design.md`](../specs/2026-04-19-loop-mode-design.md) — spec LOOP (source des 3 timers)
- [`../reference/setup-tools-conventions.md`](../reference/setup-tools-conventions.md) — **conventions comportementales** des setup tools (save policy, 3 paradigmes nav, keybindings, LED ownership). Référence impérative pour le refactor Tool 8.
- [`../reference/architecture-briefing.md`](../reference/architecture-briefing.md) §1 MM7, §2 Flow 3, §5 P5/P14 — modèle mental LED existant
- [`../reference/vt100-design-guide.md`](../reference/vt100-design-guide.md) — esthétique VT100

**Cible** : `main` branche unique. Chaque step = commit direct sur main (pas de branche, pas de PR — workflow projet). Upload firmware déclenché **manuellement par l'utilisateur** à chaque étape ; le plan ne suppose jamais d'upload automatique.

---

## 1 · Contexte et état actuel

### 1.1 État du code au 2026-04-19 (HEAD = `94a36ca`)

Vérification des commits récents et de l'état sur `main` :

- `LedController.cpp` : 795 lignes, 9 render helpers extraits par priorité, `renderBankNormal()` + `renderBankArpeg()` (pas de `renderBankLoop`).
- `LedController.h` : 221 lignes, enum `ConfirmType` à 7 valeurs (`BANK_SWITCH`, `SCALE_ROOT/MODE/CHROM`, `HOLD_ON/OFF`, `OCTAVE`).
- `LedSettingsStore` actuellement à **v5** (pas v3 comme mentionné dans LED spec §22 — à actualiser quand on rédige le commit). Contient 20+ champs dont 3 legacy unused (`fgArpPlayMin`, `bgArpStopMax`, `bgArpPlayMax`).
- `ColorSlotStore` à **v3**, 12 slots (enum `CSLOT_NORMAL_FG` → `CSLOT_OCTAVE`).
- `ToolLedSettings` : 1337 lignes, 2 pages (COLOR+TIMING / CONFIRM), navigation par flèches + toggle `t`.
- Setup menu : Tools 1–8 avec Tool 4 = Control Pads (v3.b, pool + MODE_PICK + save-on-exit), Tool 5 = BankConfig, Tool 6 = Settings, Tool 7 = PotMapping, Tool 8 = LedSettings.
- Preview API déjà en place : `previewBegin/End/SetPixel/Clear/Show`. Réutilisable sans toucher la signature.
- ControlPadStore à v2 (avec DSP pipeline globals `smoothMs`/`sampleHoldMs`/`releaseMs`). Pas impacté par Phase 0.

### 1.2 Conventions setup tools à respecter (nouveau 2026-04-19)

Le commit `13581a4` a introduit [`setup-tools-conventions.md`](../reference/setup-tools-conventions.md) comme source de vérité des règles interactionnelles. Tool 8 refondu doit s'y conformer **strictement**. Points directement applicables à Phase 0 :

| Règle | Application Tool 8 refondu |
|---|---|
| **§1 Save policy** — save sur commit, jamais sur navigation | `saveAll()` uniquement à : ENTER commit d'un pool / sortie field-editor / confirm YES defaults. Aucun save sur arrow key. |
| **§2 flashSaved cost** — un flash par commit, pas par ajustement | Un seul `flashSaved()` par opération de save. Pas de flash sur chaque valeur modifiée pendant un field editor. |
| **§3 LED ownership** | SetupComet = SetupManager (inchangé). Tool 8 prend la main LED via `previewBegin/End` pour preview live. |
| **§4 3 paradigmes de nav** | Chaque page Tool 8 utilise un ou combinaison des 3 paradigmes canoniques (grid cursor / pool / field editor). Pas de "cards" ad-hoc. |
| **§5 Pool legend** | Chaque grille avec color encoding sémantique doit exposer son mapping via pool ou legend. |
| **§6 Keybindings** | `↑↓←→` = cursor dans le context courant uniquement. `ENTER` = commit. `q` = retour (two-step exit). Toggle de page = touche custom (`t`). |
| **§8 Confirmation sub-states** | Inline prompt, jamais clear screen. |

La référence vivante pour ces conventions est **Tool 4 v3** (`ToolControlPads.cpp`). Le Tool 3 (`ToolPadRoles`) sert de référence pour les pools.

### 1.3 Cible

Après Phase 0 :
- `LedController` pilote 9 patterns de la palette (LED spec §10) via table d'events et logique de fond, pas de rendu hardcoded.
- `LedSettingsStore` **v6** : restructuré par groupes de params par pattern + bgFactor global + array NVS des overrides `EventRenderEntry[]`, 3 champs legacy supprimés.
- `ColorSlotStore` **v4** : 16 slots avec la nouvelle nomenclature (MODE_*, VERB_*, BANK_SWITCH, SCALE_*, OCTAVE, CONFIRM_OK).
- `SettingsStore` étendu : 3 nouveaux timers LOOP (`clearLoopTimerMs`, `slotSaveTimerMs`, `slotClearTimerMs`), éditables en Tool 6.
- `ToolLedSettings` refondu en **3 pages** (PATTERNS / COLORS / EVENTS) appliquant les 3 paradigmes canoniques (pool + field editor), preview live réutilisant l'API existante.
- Events historiques (BANK_SWITCH, SCALE_*, OCTAVE, HOLD_ON/OFF, tick step ARPEG, fond ARPEG) migrés vers la grammaire. Rendu visuel équivalent ou amélioré.
- Aucune régression sur boot, setup comet, error blink, battery gauge, bargraphs, calibration.

### 1.4 Hors scope Phase 0

- Tout ce qui touche au mode LOOP (engine, LoopPadStore, LoopPotStore, LittleFS, handleLoopControls, handleLoopSlots) — réservé aux phases 1–6 LOOP.
- Refactor Tool 3 vers architecture b1 contextuelle — Phase 3 LOOP.
- Extension `PotMappingStore` au 3e contexte LOOP — Phase 4 LOOP.
- Modifications de `CapacitiveKeyboard`, `MidiEngine`, `ArpEngine` (logique interne), `PotRouter`, `BatteryMonitor`.

---

## 2 · Philosophie d'exécution

**Petits commits directs sur main.** Chaque step = un commit sur `main` (pas de branche, pas de PR, conformément au workflow projet). Chaque commit compile, l'utilisateur uploade **manuellement** s'il souhaite tester, puis revient valider.

**Save policy stricte** (conventions §1) : toute opération NVS de Tool 8 arrive sur un commit explicite de l'utilisateur (ENTER dans pool, sortie de field editor, confirm YES). **Aucun save sur nav arrow.** `flashSaved()` **un seul appel par commit**, pas par ajustement.

**Dual-path toléré temporairement** : pendant steps 0.4–0.5, le code peut emprunter le vieux chemin *ou* le nouveau pour un même event, le temps de migrer les callsites. Convergence obligatoire en 0.5, vérifiée en 0.9 (grep exhaustif `triggerConfirm` / `ConfirmType` dans la logique métier doit renvoyer 0).

**Zero Migration Policy** (CLAUDE.md) : chaque bump de struct invalide l'ancien NVS, les defaults compile-time s'appliquent. Un `Serial.printf` par reset au boot. Tous les tunings utilisateurs seront reset à la première exécution post-flash — accepté.

**Tests visuels manuels** à chaque step, pas de framework automatique (firmware embedded). Le `vt100_serial_terminal.py` sert pour les tools.

**Upload** : jamais par l'assistant sans demande explicite de l'utilisateur (cf. `feedback_no_auto_upload`). Le plan décrit les tests attendus ; l'exécution des tests incombe à l'utilisateur.

---

## 3 · Découpage en steps

9 steps, chacun un commit (sauf 0.8 en sous-steps). Estimation : 8–12 commits total.

### Step 0.1 — Data model foundation (compilable, non-utilisé)

**Objectif** : poser les types fondamentaux sans changer le comportement.

**Fichier nouveau** : `src/core/LedGrammar.h` — abrite les enums et table compile-time, séparé de `HardwareConfig.h` (qui reste pour constantes hardware pures) et de `KeyboardData.h` (qui reste pour NVS Stores).

**Changements** :
- Dans `LedGrammar.h` :
  - Enum `PatternId` : SOLID / PULSE_SLOW / CROSSFADE_COLOR / BLINK_SLOW / BLINK_FAST / FADE / FLASH / RAMP_HOLD / SPARK (9 valeurs, 0-8).
  - Enum `EventId` : `EVT_BANK_SWITCH`, `EVT_SCALE_ROOT`, `EVT_SCALE_MODE`, `EVT_SCALE_CHROM`, `EVT_OCTAVE`, `EVT_PLAY`, `EVT_STOP`, `EVT_WAITING`, `EVT_REFUSE`, `EVT_CONFIRM_OK` (Phase 0 scope). Slots LOOP-spécifiques (REC, OVERDUB_START, SLOT_LOADED, SLOT_WRITTEN, SLOT_CLEARED, SLOT_REFUSED, CLEAR_LOOP) **réservés mais non utilisés** — ajoutés à l'enum pour ne pas casser le format NVS quand Phase 1+ les consomme.
  - Struct `PatternParams` union discriminée par pattern (`{SOLID: {pct}, BLINK_FAST: {onMs, offMs, cycles}, FADE: {durationMs, startPct, endPct}, RAMP_HOLD: {rampMs, suffixOnMs, suffixGapMs, suffixCycles}, FLASH: {durationMs, fgPct, bgPct}, SPARK: {onMs, gapMs}, CROSSFADE_COLOR: {periodMs}, PULSE_SLOW: {minPct, maxPct, periodMs}, BLINK_SLOW: {onMs, offMs, cycles}}`).
  - Struct `EventRenderEntry` : `{PatternId patternId; ColorSlotId colorSlot; uint8_t fgPct;}`. Taille ~3 octets.
  - Table `EVENT_RENDER_DEFAULT[NUM_EVENTS]` — defaults compile-time utilisés quand le NVS est vide/invalide.
  - `static_assert(sizeof(PatternParams) <= 16)` pour borner l'union.
- Dans `KeyboardData.h` : rien (pour l'instant ; l'array `EventRenderEntry[]` sera ajouté à `LedSettingsStore` en step 0.2).

**Fichiers touchés** : `core/LedGrammar.h` (nouveau).

**Tests** :
- Build clean. Aucune régression visuelle (rien n'utilise les nouveaux types).

**Critère done** : build clean, firmware identique.

**Risques** : union size blow-up. Mitigation via `static_assert`.

---

### Step 0.2 — `LedSettingsStore` v6

**Objectif** : restructurer le store autour de la palette de patterns. Ajouter l'array d'overrides events.

**Changements** :
- Retirer les 3 champs legacy (`fgArpPlayMin`, `bgArpStopMax`, `bgArpPlayMax`).
- Réorganiser en groupes de params par pattern (un groupe `PULSE_SLOW`, un `BLINK_FAST`, un `FADE`, etc.). Un sous-struct ou simple flat fields selon préférence d'impl — décision au commit.
- Ajouter `bgFactor` global (0–100 %, **default 25 % provisoire** à tuner en 0.9 sur hardware, range [10, 50]).
- Ajouter params SPARK (`sparkOnMs`, `sparkGapMs`, `sparkCycles` — default 2).
- Ajouter array `EventRenderEntry eventOverrides[NUM_EVENTS]` — user-configurables depuis Tool 8 Page EVENTS. Les entrées vides (pattern = 0xFF ou sentinel) retombent sur `EVENT_RENDER_DEFAULT` de `LedGrammar.h` à la résolution.
- Conserver les intensités existantes (fond FG/BG, tick flash FG/BG) → deviennent les params `SOLID` et `FLASH`.
- Bump `LED_SETTINGS_VERSION` 5 → 6.
- Mettre à jour validator + compile-time defaults.
- `LedController::loadLedSettings()` refactoré pour peupler les groupes de params. Les render helpers référencent ces groupes.

**Fichiers touchés** : `core/KeyboardData.h` (struct + validator + version bump), `core/LedController.h/.cpp` (loadLedSettings + champs internes refactorés), `managers/NvsManager.cpp` (descriptor table si size change).

**Tests** :
- Premier boot post-flash : log `NVS: LED settings reset to defaults`.
- Bank switch, scale confirm, octave, hold, arp playing : rendu visuel inchangé (defaults équivalent v5).
- Tool 8 actuel : cassé à partir de ce commit (signatures de champs changent). Voir stratégie §2.1 ci-dessous.

**Critère done** : store persiste/charge correctement, rendu historique préservé, Tool 8 désactivé proprement.

**Stratégie Tool 8 entre 0.2 et 0.8** : **désactivation propre**. À l'entrée du Tool 8 entre ces commits, afficher un écran placeholder :
```
Tool 8 — LED Settings
═══════════════════════
Refactor in progress.
See plan Phase 0 §0.8.
Press 'q' to return.
```
Navigation VT100 basique uniquement. Aucun accès aux valeurs. L'utilisateur n'a pas de besoin live en Phase 0 (les defaults sont bons) — il retrouvera toute la config en 0.8e. **Cette stratégie est acceptable seulement si 0.2→0.8 s'enchaîne en quelques sessions contiguës.** Si ça prend plus d'une semaine, revoir (option : garder un Tool 8 intermédiaire v5-compatible partiel, coûteux, non recommandé).

**Risques** :
- Breaking change Tool 8 : accepté avec la mitigation ci-dessus.
- `EventRenderEntry[]` array : taille ~10 × 3 = 30 bytes, rentre largement dans `NVS_BLOB_MAX_SIZE = 128` mais vérifier le total `sizeof(LedSettingsStore) ≤ 128`.

---

### Step 0.3 — `SettingsStore` extension pour timers LOOP

**Objectif** : préparer le coupling RAMP_HOLD ↔ timer métier exigé par LED spec §13.

**Changements** :
- Ajouter 3 champs à `SettingsStore` :
  - `clearLoopTimerMs` : default 500, range [200, 1500]
  - `slotSaveTimerMs` : default 1000, range [500, 2000]
  - `slotClearTimerMs` : default 800, range [400, 1500]
- Bump `SETTINGS_VERSION`.
- Étendre `validateSettingsStore()` avec clamps.
- Mettre à jour les defaults compile-time.

**Fichiers touchés** : `core/KeyboardData.h` (struct + validator), `managers/NvsManager.cpp` (descriptor si size change).

**Tests** :
- Boot : warning `NVS: settings reset to defaults` au premier boot post-flash.
- Tool 6 continue à fonctionner sur les champs existants (pas encore d'UI pour les 3 nouveaux — step 0.7).

**Critère done** : compile + boot + Tool 6 existant inchangé.

**Risques** : faible. Extension simple, patterns existants.

---

### Step 0.4 — `LedController` : moteur de patterns

**Objectif** : introduire l'engine de rendu par pattern. Dual-path pour permettre la migration progressive.

**Changements** :
- Nouvelle méthode privée `renderPattern(const PatternInstance& inst, unsigned long now)` qui switch sur `inst.patternId` et dessine sur les pixels correspondants (`inst.ledMask`, `inst.color`, `inst.fgPct/bgPct`, `inst.startTime`).
- Nouvelle struct interne `PatternInstance` : état d'un rendu en cours (pattern, params, colors, start time, expiry, mask).
- `LedController` expose `triggerEvent(EventId evt, uint8_t ledMask = 0)` qui :
  1. Résout la table `EVENT_RENDER_DEFAULT[evt]` ou overrides NVS si extension future.
  2. Résout la `PatternParams` depuis le store (ex. BLINK_SLOW → `{onMs, offMs, cycles}`).
  3. Résout la `RGBW color` via `resolveColorSlot(entry.colorSlot)`.
  4. Instancie un `PatternInstance` et le place dans un slot d'animation active.
- Support **1 animation event active à la fois** pour commencer (même comportement que l'existant : nouveau trigger préempte l'ancien).
- Séparation claire entre :
  - **Background** (fond permanent) : rendu par `renderBankNormal/Arpeg` existants, sans toucher à la partie event.
  - **Event overlay** : rendu par `renderPattern` sur top du background. `renderPattern` ne clear jamais, il peint par-dessus le fond existant.
- **Conservation de la compatibilité** : `triggerConfirm(ConfirmType, uint8_t param, uint8_t ledMask)` reste publique. Son impl interne appelle `triggerEvent(mapConfirmToEvent(confirmType), ledMask)`. Les callsites existants sont inchangés en 0.4.
- `renderConfirmation(now)` reste mais devient plus fin : il ne fait plus la logique de pattern, il délègue au `renderPattern` interne.

**Fichiers touchés** : `core/LedController.h/.cpp`.

**Tests** :
- Compile + upload.
- Bank switch : doit rendre comme avant (BLINK_SLOW blanc 3 cycles). Si le rendu diffère, c'est la valeur params default qui diffère entre ancien et nouveau — ajuster.
- Scale root/mode/chrom : BLINK_FAST avec color masks. Rendu identique.
- Octave : BLINK_FAST violet. Rendu identique.
- HOLD_ON / HOLD_OFF : FADE IN/OUT. Rendu identique.
- Arp playing : tick step + fond SOLID. Rendu identique (la migration du tick vers FLASH vient en 0.5).
- Boot, setup comet, battery, bargraphs, error : inchangés.

**Critère done** : tous les confirms + overlays passent par le nouvel engine mais rendent identique.

**Risques** :
- Engine edge cases : animation interrompue par une nouvelle, pattern qui dépasse (RAMP_HOLD sans complétion), overlay + tick flash simultanés. Documenter les cas et tester.
- Perf : fréquence d'appel à `renderPattern` — vérifier que `millis()` math reste négligeable (pattern d'overflow-safe `(now - startTime) < duration`).

---

### Step 0.5 — Migration des events historiques

**Objectif** : tous les callsites bascullent sur `triggerEvent(EventId)`. On retire progressivement `triggerConfirm(ConfirmType)` ou on le garde comme wrapper.

**Changements** :
- `BankManager::switchToBank()` : remplace `triggerConfirm(CONFIRM_BANK_SWITCH)` par `triggerEvent(EVT_BANK_SWITCH)`.
- `ScaleManager::processScalePads()` : `triggerConfirm(CONFIRM_SCALE_*)` → `triggerEvent(EVT_SCALE_*)` avec mask group préservé.
- `main.cpp::handleOctaveChange` (ou le consommateur de `hasOctaveChanged()`) : `triggerEvent(EVT_OCTAVE)`.
- `ArpEngine::setCaptured()` → consommateur externe `main.cpp::handleHoldPad` / BankManager double-tap : `triggerEvent(EVT_PLAY)` ou `triggerEvent(EVT_STOP)` selon l'edge `false→true` / `true→false`.
- **Tick ARPEG — approche hybride** (minimal cost + propre + future-proof) :
  - **Triggering inline** : dans `renderBankArpeg()`, consommer `arpEngine->consumeTickFlash()` et gérer l'état `_flashStartTime[i]` par LED (pattern existant, non modifié). Pas de création de PatternInstance par tick, pas d'event transport. Économique.
  - **Rendering partagé** : extraire la logique FLASH visuelle dans une fonction helper `renderFlashOverlay(led, color, fgPct, bgPct, startTime, durationMs, isFg)` définie dans `LedController.cpp`. Cette fonction encapsule la sémantique FLASH (tick perce, intensité fond préservée, pixel temporairement remplacé pendant la durée).
  - **Même fonction utilisée** par le pattern engine pour les events transport qui renderent un FLASH (ex. si un event LOOP `EVT_TICK_PLAYING` arrive en Phase 1+). Une seule vérité visuelle FLASH.
  - Résultat : ticks ARPEG ne polluent pas la queue d'events, la sémantique FLASH vit à un seul endroit, les futurs events FLASH (LOOP wrap/bar/rec) héritent automatiquement du comportement.
  - Couleurs/intensités : migrées vers `CSLOT_VERB_PLAY` (vert) + params `FLASH.fgPct`/`FLASH.bgPct` depuis `LedSettingsStore` v6.
- **Fond ARPEG** : `renderBankArpeg()` utilise `SOLID` pour idle/playing et `PULSE_SLOW` pour stopped-loaded. Intensités et couleurs depuis les nouveaux groupes de params de `LedSettingsStore` v6.
- **Fond NORMAL** : `renderBankNormal()` reste simple : `SOLID` à `normalFgIntensity` / `normalBgIntensity`. Couleur via `CSLOT_MODE_NORMAL` (warm white).

Optionnel en 0.5 : retirer `ConfirmType` + `triggerConfirm()` de l'API publique, ne garder que `triggerEvent` + `EventId`. Si on fait ça, il faut migrer `LedController::triggerConfirm` callsites dans ce commit. Si on préfère laisser comme wrapper temporairement, on reporte à 0.9.

**Fichiers touchés** : `core/LedController.h/.cpp`, `managers/BankManager.cpp`, `managers/ScaleManager.cpp`, `main.cpp`, `arp/ArpEngine.cpp`.

**Tests** :
- Bank switch : **visuellement identique** (BLINK_SLOW blanc).
- Scale change sur bank ayant un scaleGroup : tous les LEDs du groupe blinkent simultanément (mask préservé).
- Octave sur ARPEG : BLINK_FAST violet FG.
- Hold ARPEG capture ON : FADE IN vert sur LED cible (FG ou BG via double-tap).
- Hold capture OFF : FADE OUT vert.
- Arp playing : tick vert 30 ms sur fond bleu, breathing sine pendant stopped-loaded.
- Boot, setup comet, battery, bargraphs, error, calibration : inchangés.

**Critère done** : plus aucun callsite ne référence les anciennes valeurs `ConfirmType` dans la logique métier. `LedController` ne render plus que via le nouveau pipeline.

**Risques** :
- Régression visuelle sur un event rare (ex. double-tap bank ARPEG BG → HOLD_ON sur BG LED). Test dédié obligatoire.
- Mask scale group : vérifier que la propagation fonctionne comme avant après la migration.

---

### Step 0.6 — `ColorSlotStore` v4 (16 slots + logique BG via bgFactor)

**Objectif** : extension à 16 slots avec la nomenclature finale. **Attention : renames + logic changes.**

#### Renommages directs (12 existants)
| Ancien | Nouveau | Logique |
|---|---|---|
| `CSLOT_NORMAL_FG` | `CSLOT_MODE_NORMAL` | Rename pur |
| `CSLOT_ARPEG_FG` | `CSLOT_MODE_ARPEG` | Rename pur |
| `CSLOT_TICK_FLASH` | `CSLOT_VERB_PLAY` | Rename + **la couleur par défaut change** : blanc → vert (alignement grammaire "PLAY = vert"). Affecte tick ARPEG step. |
| `CSLOT_BANK_SWITCH` | `CSLOT_BANK_SWITCH` | Inchangé |
| `CSLOT_SCALE_ROOT` | `CSLOT_SCALE_ROOT` | Inchangé |
| `CSLOT_SCALE_MODE` | `CSLOT_SCALE_MODE` | Inchangé |
| `CSLOT_SCALE_CHROM` | `CSLOT_SCALE_CHROM` | Inchangé |
| `CSLOT_HOLD_ON` | `CSLOT_VERB_PLAY` (fusionné) | **Logic change** : HOLD_ON et PLAY utilisent maintenant le même slot (même vert, via `triggerEvent(EVT_PLAY)` en step 0.5). |
| `CSLOT_HOLD_OFF` | **retiré** | **Logic change** : le STOP utilise la color du state précédent (option γ LED spec §11). Pour Phase 0, STOP depuis HOLD_OFF utilise aussi `CSLOT_VERB_PLAY` (vert qui fade out). |
| `CSLOT_OCTAVE` | `CSLOT_OCTAVE` | Inchangé |
| `CSLOT_NORMAL_BG` | **retiré** | **Logic change** : le BG utilise maintenant la couleur du slot FG × `bgFactor`. Fini les 2 slots indépendants — une seule couleur identité par mode, dim via bgFactor pour le BG. Perte de souplesse acceptée. |
| `CSLOT_ARPEG_BG` | **retiré** | Idem. |

#### Nouveaux slots (6 nouveaux = 16 total)
| Slot | Preset default | Hue | Rôle |
|---|---|---|---|
| `CSLOT_MODE_LOOP` | Gold | 0 | Fond jaune bank LOOP |
| `CSLOT_VERB_REC` | Coral (ou Red si ajouté) | 0 | Tick RECORDING |
| `CSLOT_VERB_OVERDUB` | Amber | 0 | Tick OVERDUBBING |
| `CSLOT_VERB_CLEAR_LOOP` | Cyan | 0 | RAMP_HOLD CLEAR long-press |
| `CSLOT_VERB_SLOT_CLEAR` | Amber | +20 | RAMP_HOLD delete slot (orange distinct de OVERDUB via hue offset — **principe du système, cf. discussion conception**) |
| `CSLOT_VERB_SAVE` | Magenta | 0 | RAMP_HOLD slot save |
| `CSLOT_CONFIRM_OK` | Pure White | 0 | SPARK suffix (editable mais convention "keep white") |

Sur l'utilisation du hue offset : la différenciation entre `CSLOT_VERB_OVERDUB` (orange) et `CSLOT_VERB_SLOT_CLEAR` (orange distinct) passe par un même preset Amber + hue offset différent. Ce pattern est le mécanisme canonique de différenciation fine du système ColorSlot (preset+hueOffset, tous deux NVS). L'utilisateur peut re-tuner les deux slots indépendamment en Tool 8 Page COLORS.

#### Changements code
- `COLOR_SLOT_COUNT` : 12 → 16.
- Enum `ColorSlotId` réordonné pour grouper MODE_* / VERB_* / SETUP (BANK_SWITCH, SCALE_*, OCTAVE) / CONFIRM_OK.
- `COLOR_SLOT_VERSION` : 3 → 4.
- Validator mis à jour.
- Defaults compile-time : mapping preset/hue par slot comme ci-dessus.
- `LedController::loadColorSlots()` : résout les 16 slots, les stocke dans des champs groupés (`_colMode[3]`, `_colVerb[6]`, `_colSetup[5]`, `_colConfirmOk`) — structure exacte décidée au commit.
- Tous les callsites qui référencent `CSLOT_NORMAL_BG`, `CSLOT_ARPEG_BG`, `CSLOT_HOLD_ON/OFF`, `CSLOT_TICK_FLASH` doivent être mis à jour. Grep exhaustif obligatoire avant commit.
- `renderBankNormal/Arpeg` appliquent `bgFactor` à la couleur FG pour dériver la couleur BG (plus de second slot dédié).

**Fichiers touchés** : `core/KeyboardData.h` (enum + struct + version bump + validator), `core/LedController.h/.cpp` (loadColorSlots + render helpers), `managers/NvsManager.cpp` (descriptor).

**Tests** :
- Boot post-flash : reset ColorSlotStore aux defaults.
- Bank switch, scale, octave, hold capture, arp playing : rendu visuel avec les nouvelles couleurs. Un changement notable : tick step ARPEG passe de blanc (ancien `CSLOT_TICK_FLASH`) à vert (nouveau `CSLOT_VERB_PLAY`). C'est intentionnel — cohérence grammaire "verb PLAY = vert".
- Fond NORMAL : warm white solide FG + warm white dim (via bgFactor) BG. L'identité visuelle est préservée (la dim suit la couleur FG au lieu d'avoir potentiellement une teinte différente — simplification assumée).

**Critère done** : 16 slots résolus, rendu cohérent, bgFactor correctement appliqué au BG.

**Risques** :
- Rename massif → grep exhaustif obligatoire pour ne rater aucun callsite.
- Perte de souplesse BG (ne plus pouvoir customiser BG indépendamment du FG) : discutée + assumée. Si un utilisateur regrette, on réintroduit des slots BG en Phase post-LOOP.
- Tick step ARPEG change de couleur (blanc → vert) : changement visuel délibéré, à documenter dans le commit.

---

### Step 0.7 — Tool 6 extension pour timers LOOP

**Objectif** : exposer les 3 timers ajoutés en 0.3.

**Changements** :
- Ajouter 3 cases dans le Tool 6 pour éditer `clearLoopTimerMs`, `slotSaveTimerMs`, `slotClearTimerMs`.
- Respecter la présentation VT100 existante (mêmes patterns que `bargraphDurationMs`, `doubleTapMs`, etc.).
- Navigation : intégrer dans l'ordre logique du Tool 6. Emplacement proposé : après les timings existants (doubleTap, bargraph), avant les paramètres AT.
- Save via `NvsManager::saveBlob` (API unifiée), `flashSaved()` au succès.
- `NvsManager::checkBlob` consommé pour le badge "saved" au header.

**Fichiers touchés** : `setup/ToolSettings.h/.cpp`, éventuellement `setup/SetupUI.h` si nouvelles constantes d'affichage.

**Tests** :
- Entrer en setup mode, naviguer jusqu'à Tool 6.
- Les 3 nouveaux params sont visibles, éditables par flèches ±, affichage en ms.
- Valeurs respectent les ranges [200, 1500], [500, 2000], [400, 1500].
- `ENTER` save. Badge "saved" apparait. Reboot : valeurs persistées.
- Range clamps testés (essayer une valeur hors range → clamp automatique).

**Critère done** : Tool 6 éditable pour les 3 timers, persistance OK.

**Risques** : faible. Pattern connu.

---

### Step 0.8 — Tool 8 refactor en 3 pages

**Objectif** : refonte de `ToolLedSettings` alignée sur les **3 paradigmes canoniques** (`setup-tools-conventions.md` §4) : grid cursor, pool, field editor. **Pas de "cards" ad-hoc.** Tool 4 v3 (`ToolControlPads`) et Tool 3 (`ToolPadRoles`) servent de références implémentées.

**Mapping pages × paradigmes** :

| Page | Paradigme principal | Paradigmes secondaires |
|---|---|---|
| **PATTERNS** (9 entrées) | **Pool vertical** des 9 patterns (sélection) | **Field editor** pour les params du pattern sélectionné |
| **COLORS** (16 slots) | **Grid 4×4** des slots (cursor nav) | **Pool** des 14 presets (ENTER sur un slot → pool select preset) + **field editor** pour hueOffset |
| **EVENTS** (~10 entries) | **Field list vertical** (une ligne par event) | **Pool** pour le pattern de l'event + **pool** pour le color_slot + **field editor** pour fgPct |

**Inter-page nav** : touche `t` (toggle cyclique PATTERNS → COLORS → EVENTS → PATTERNS). Cohérent avec l'usage actuel de `t` dans le Tool 8 v5 (toggle entre 2 pages). Pas d'overload des flèches qui restent dédiées à la nav cursor intra-page (convention §6). Header affiche `[PATTERNS | COLORS | EVENTS]` avec highlight sur la page active.

**Save policy** (conventions §1) : `saveAll()` déclenché uniquement à :
- ENTER d'un pool (sélection d'un preset ou d'un pattern ou d'un color_slot) — commit de l'assignation.
- Sortie d'un field editor par ENTER (save-on-commit du field editor).
- Confirm YES sur "Reset to defaults" (touche `d` → confirm).

**Aucun save sur arrow key.** Un seul `flashSaved()` par commit (conventions §2).

**Preview live** : `previewBegin()` au tool entry, `previewEnd()` au tool exit (également sur `q` multi-level via two-step exit §6.4). LED 3-4 suivent la sélection courante :
- Page PATTERNS : pattern sélectionné rendu en boucle sur LED 3-4 (avec les params courants). Touche `b` re-triggere le pattern si non périodique (SPARK/FADE).
- Page COLORS : slot sélectionné rendu en SOLID sur LED 3-4.
- Page EVENTS : event sélectionné rendu sur LED 3-4 à `b` (trigger unique, comme un vrai event).

#### Sous-steps

**Ordre d'implémentation** : skeleton → COLORS (le plus simple, update existant) → PATTERNS (nouveau paradigme) → EVENTS (dépend des 2 précédents) → polish.

##### 0.8a — Skeleton + inter-page nav + exit clean

**Changements** :
- Nouvelle machine à pages dans `ToolLedSettings::run()` avec enum local `{PAGE_PATTERNS, PAGE_COLORS, PAGE_EVENTS}`, default = `PAGE_PATTERNS`.
- Touche `t` cycle la page. `↑↓←→` font de la nav intra-page (stub). `q` two-step exit.
- Header `drawConsoleHeader` avec pagination `[PATTERNS | COLORS | EVENTS]`, badge NVS (via `NvsManager::checkBlob`).
- `previewBegin()` au entry, `previewEnd()` au exit (sur `q` depuis page level).
- Stubs de contenu dans chaque page : "Page X — not yet implemented".

**Tests** : navigation entre 3 pages OK, preview 3-4 off, exit propre.

##### 0.8b — Page COLORS (16 slots)

**Changements** :
- Grid 4×4 affichant les 16 slots. Chaque cellule : nom court du slot (abréviation 6-8 chars) + sample visuel (bloc Unicode coloré via VT100 color codes, aligné `vt100-design-guide.md`).
- Cursor nav `↑↓←→` dans la grille (convention §4.1).
- ENTER sur un slot → sous-état **edit** avec 2 sous-paradigmes :
  - **Pool des 14 presets** (navigable ↑↓, ENTER commit le preset).
  - **Field editor** pour `hueOffset` (range -128..+127, convention §4.3 : flèches ±, pas de save à chaque ajust, save à la sortie par ENTER).
  - Deux sous-sections dans l'écran edit : "Preset:" (pool) et "Hue:" (field editor). Tab ou flèche gauche-droite pour basculer entre les deux sous-paradigmes.
- Touche `q` : retour à la grid sans commit si dans un sous-état, retour à grid level si dans grid (two-step exit).
- Preview : slot sélectionné (grid cursor ou edit en cours) → SOLID sur LED 3-4.
- Save : sur ENTER final du pool OU sortie field editor, `saveAll()` + `flashSaved()`.
- Touche `d` : reset slot courant au default. Confirm YES → reset + save.

**Tests** : 16 slots parcourus, preset et hue éditables, preview live, save persiste reboot.

##### 0.8c — Page PATTERNS (9 patterns)

**Changements** :
- Pool vertical des 9 patterns. Cursor `↑↓` (convention §4.2). `←→` inutilisés ou dédiés à incrément rapide dans field editor ultérieur.
- Sélection d'un pattern (cursor) affiche un panel "Params" à droite listant les params du pattern sélectionné (ex. pour `BLINK_FAST` : `onMs`, `offMs`, `cycles`).
- ENTER sur un pattern → sous-état **field editor** avec les params du pattern comme lignes. Nav `↑↓` entre params. `←→` ajust valeur (step défini par type : ms = 10/100, pct = 1/10, cycles = 1). Accélération sur repeat long via `NavEvent::accelerated`.
- ENTER valide → retour pool. `q` annule → retour pool sans commit.
- **Cas RAMP_HOLD** : le field `rampMs` est **read-only** (dérivé des timers LOOP dans `SettingsStore` — voir §13 LED spec). Affichage "dérivé Tool 6" au lieu du chiffre modifiable. Seuls les params SPARK suffix (`onMs`, `gapMs`, `cycles`) sont éditables ici.
- Preview : pattern courant (en pool ou en edit) joue en boucle sur LED 3-4 avec les params live. Pour SPARK/FADE (non périodiques), touche `b` re-triggere.
- Save : sortie field editor par ENTER → `saveAll()` + `flashSaved()`.

**Tests** : 9 patterns listés, params éditables, preview change en live, save persiste, RAMP_HOLD affiche bien la dérivation sans autoriser l'édition de `rampMs`.

##### 0.8d — Page EVENTS (~10 events Phase 0)

**Changements** :
- Field list vertical. Une ligne par event. Chaque ligne affiche : `event_name | pattern_name | color_slot_name | fgPct`.
- Cursor `↑↓` pour sélectionner un event. `←→` dans la ligne pour sélectionner le champ (pattern / color / fgPct).
- ENTER sur le champ `pattern` → pool des 9 patterns (convention §4.2). ENTER commit + retour field list.
- ENTER sur le champ `color_slot` → pool des 16 color slots. ENTER commit + retour field list.
- ENTER sur le champ `fgPct` → field editor numeric (range 0-100, convention §4.3). Sortie ENTER → commit + retour.
- **Pool Legend** obligatoire (convention §5) : une zone dédiée affiche le mapping color du pattern courant (ex. si pattern = BLINK_FAST color = CSLOT_SCALE_ROOT, on voit la sample couleur de SCALE_ROOT).
- Resolution de l'event à la lecture : si `eventOverrides[eventId].patternId != 0xFF`, utiliser l'override ; sinon fallback sur `EVENT_RENDER_DEFAULT[eventId]` (de `LedGrammar.h`). Ce fallback permet à l'utilisateur de "ne rien overrider" et laisser les defaults.
- Touche `d` sur un event : reset aux defaults (sentinel 0xFF). Confirm YES.
- Preview : touche `b` → trigger l'event sur LED 3-4 (pattern + color + fgPct du mapping courant).
- Save sur ENTER commit de chaque pool ou field editor.

**Tests** :
- Changer `BANK_SWITCH.pattern` BLINK_SLOW → BLINK_FAST, save, faire un bank switch → blink rapide.
- Changer `BANK_SWITCH.color_slot` BANK_SWITCH → SCALE_ROOT → bank switch rend dans la couleur amber.
- Reset defaults → BANK_SWITCH revient à BLINK_SLOW blanc.
- Reboot → persistance OK.

##### 0.8e — Polish + save UX finalization

**Changements** :
- Vérifier : `previewBegin()` appelé à l'entrée du tool, `previewEnd()` au exit propre depuis tous les niveaux (grid, pool, field editor, confirm).
- Vérifier : **un seul** `flashSaved()` par commit — grep le code pour s'assurer qu'il n'est pas appelé à chaque `→` dans un field editor.
- Vérifier `screenDirty` discipline (convention §7) : re-render uniquement sur event OU tick périodique (≥50 ms). Pas de render par frame sans changement.
- Cas limites testés : exit direct depuis un sous-état via two-step `q` (compte exit sub-state puis exit tool ; convention §6.4).
- Tests visuels complets : navigation entre les 3 pages, édition sur chaque page, persistance reboot, rendu normal final cohérent avec les nouvelles configs.
- Retrait de tout stub / "not yet implemented" résiduel.

**Critère done** : Tool 8 complet, conforme aux conventions, 0 régression visible, save respectueux (pas de flood NVS).

---

### Step 0.9 — Tests de régression complets + polish

**Objectif** : valider l'ensemble.

**Checks visuels** (hardware requis, `vt100_serial_terminal.py` pour setup mode) :
- Boot : step progression intacte (1 LED / step), échec → blink rouge halt.
- Setup comet : ping-pong violet.
- Chase (calibration entry) : chase blanc.
- Error : LEDs 3-4 blink rouge 500 ms.
- Battery gauge : gradient rouge→vert 3s.
- Bargraphs (pot + tempo) : bar + tip LED BPM pulse.
- Bank switch : BLINK_SLOW blanc 3 cycles sur FG.
- Scale root/mode/chrom : BLINK_FAST couleur correcte sur mask group complet.
- Octave : BLINK_FAST violet.
- Hold capture ON (ARPEG pad ou double-tap bank) : FADE IN vert.
- Hold capture OFF : FADE OUT vert.
- Arp playing : tick vert 30 ms sur fond bleu. Stopped-loaded : PULSE_SLOW. Idle : SOLID dim.
- Tool 6 : 3 nouveaux timers LOOP éditables.
- Tool 8 : 3 pages (PATTERNS grid-less pool, COLORS grid 4×4 + pool preset + field hue, EVENTS field-list + pools imbriqués), preview live sur LED 3-4, save sur ENTER commit uniquement, persistance reboot.
- Cas multi-bank : 2 ARPEG banks en BG avec un playing et un stopped-loaded → les deux rendus distincts en BG (dim).
- Cas events simultanés : bank switch + scale change en <300 ms → le plus récent préempte l'ancien.
- Battery low burst pendant un confirm : coexistence acceptable.

**NVS check** :
- Premier boot post-flash : warnings `NVS: xyz reset to defaults` pour LedSettingsStore, ColorSlotStore, SettingsStore.
- Reboot immédiat après re-config : valeurs persistées, pas de warning.

**Polish** :
- **Dual-path cleanup** : grep exhaustif `triggerConfirm\|ConfirmType` dans `src/` hors `LedController.{h,cpp}`. Zéro occurrence attendue dans la logique métier (BankManager, ScaleManager, main, ArpEngine). Si des wrappers `triggerConfirm()` legacy sont encore en place dans `LedController.h`, décider : supprimer (breaking) ou garder comme compat (commenter pour clarifier "legacy, see triggerEvent").
- Supprimer enum `ConfirmType` si plus utilisé nulle part.
- **bgFactor tuning sur hardware** : tester les valeurs 20 / 25 / 30 % sur la strip réelle avec différentes combinaisons multi-bank, acter la valeur finale dans `LedSettingsStore` defaults.
- **SPARK default check** : vérifier que `CSLOT_CONFIRM_OK` rend bien blanc par défaut (Pure White preset, hue 0). Confirmer visuellement.
- Nettoyage des logs DEBUG_SERIAL (retirer les debug prints temporaires).
- Sync `docs/reference/architecture-briefing.md` §5 P5 (Confirmation overlay → Event overlay via grammaire unifiée) + MM7 (LED priority inchangée mais préciser le modèle 3-couches) + §8 entrée LEDs.
- Sync `docs/reference/nvs-reference.md` avec les nouvelles versions (`LedSettingsStore v6`, `ColorSlotStore v4`, `SettingsStore` étendu) et le nouveau fichier `LedGrammar.h` comme source des defaults compile-time events.

**Critère done** : comportement visuel équivalent ou amélioré, aucune régression, docs synchronisées, NVS propre, 0 dual-path legacy.

---

## 4 · Fichiers impactés (récap)

| Fichier | Steps | Changement |
|---|---|---|
| `core/LedGrammar.h` **(nouveau)** | 0.1 | Enums PatternId/EventId, struct PatternParams, EventRenderEntry, table EVENT_RENDER_DEFAULT |
| `core/KeyboardData.h` | 0.2, 0.3, 0.6 | ColorSlotStore v4 (16 slots, enum renommé), LedSettingsStore v6 (groupes params + bgFactor + eventOverrides[]), SettingsStore +3 timers |
| `core/LedController.h` | 0.4, 0.5, 0.6 | API triggerEvent, PatternInstance, renderFlashOverlay helper, champs internes refactorés |
| `core/LedController.cpp` | 0.4, 0.5, 0.6 | renderPattern engine, renderFlashOverlay, migration des helpers render, bgFactor applied en BG |
| `managers/BankManager.cpp` | 0.5 | triggerEvent(EVT_BANK_SWITCH) |
| `managers/ScaleManager.cpp` | 0.5 | triggerEvent(EVT_SCALE_*) |
| `managers/NvsManager.cpp` | 0.2, 0.3, 0.6 | Descriptors + validators + version bumps |
| `arp/ArpEngine.cpp` | 0.5 | Callsites hold/capture (généralement via main.cpp/BankManager) |
| `main.cpp` | 0.5 | Callsites octave + hold, tick consume inline |
| `setup/ToolSettings.{h,cpp}` | 0.7 | 3 nouveaux timers LOOP éditables |
| `setup/ToolLedSettings.{h,cpp}` | 0.8a-e | Refactor complet 3 pages (pool + grid + field editor) |
| `docs/reference/architecture-briefing.md` | 0.9 | Sync MM7 + P5 + §8 entry |
| `docs/reference/nvs-reference.md` | 0.9 | Sync versions + store shapes |

Fichiers **non touchés** :
- `core/CapacitiveKeyboard.{cpp,h}` (intouchable)
- `midi/*` (MidiEngine, MidiTransport, ClockManager, ScaleResolver, GrooveTemplates)
- `arp/ArpScheduler.{cpp,h}` (logique arp interne non touchée)
- `managers/PotRouter.{cpp,h}`, `PotFilter.{cpp,h}`
- `managers/ControlPadManager.{cpp,h}` (Tool 4 deja v2 stable)
- `managers/BatteryMonitor.{cpp,h}`
- `setup/ToolCalibration`, `ToolPadOrdering`, `ToolPadRoles`, `ToolControlPads`, `ToolBankConfig`, `ToolPotMapping`
- `platformio.ini`

---

## 5 · Risques et options de repli

### 5.1 Risques principaux

| Risque | Mitigation |
|---|---|
| Engine pattern — edge case non anticipé (cancellation, simultaneity) | Tests dédiés par pattern, documenter les invariants, fallback preempt-and-replace documenté |
| Tool 8 cassé entre 0.2 et 0.8e | Placeholder propre "refactor in progress" à partir de 0.2 (cf. step 0.2). Tenable si 0.2→0.8e enchaîné en quelques sessions ; sinon revoir. |
| Régression visuelle sur un event rare | Liste exhaustive de tests en 0.9, exécutée après chaque step majeur (0.4, 0.5, 0.6) |
| NVS Zero Migration → user perd ses réglages | Acceptation projet. Document clair au commit. |
| Tick ARPEG : approche hybride (triggering inline + rendering partagé) | Choix acté en step 0.5. Partage de la logique FLASH via `renderFlashOverlay()` garantit future-proof ; pas de triggering via engine pour ticks. |
| Perf Core 1 : engine ajoute table lookup par event | Cost négligeable : ~100 µs/s worst-case. Core 1 actuel à ~16 %. Marge confortable. |
| Flash size | Refactor ajoute `LedGrammar.h` + logique pattern engine. Ordre de grandeur : quelques Ko. Budget flash 8 MB, no concern. Vérifier `pio run` size output au step 0.4 pour cadrer. |
| Dual-path legacy résiduel (step 0.9) | Grep exhaustif obligatoire en 0.9 : `triggerConfirm\|ConfirmType` dans `src/` hors `LedController`. Doit renvoyer 0 callsites métier. |

### 5.2 Option de repli (LED spec §25)

Si la refonte `LedController` s'avère trop complexe en cours de route (machine d'état impossible à stabiliser, incompatibilité avec un comportement existant), on bascule sur :
- Implémenter les events LOOP avec patterns ad-hoc hardcoded dans `LedController`, comme l'existant.
- Dette technique acceptée : le grand refactor se fait après LOOP stabilisé (Phase post-6).
- Cette option **n'est pas le plan nominal**. À ne déclencher qu'après discussion explicite si un blocage sérieux apparait en 0.4 ou 0.5.

---

## 6 · Ordre d'exécution et check-in

Ordre strict : 0.1 → 0.2 → 0.3 → 0.4 → 0.5 → 0.6 → 0.7 → 0.8a → 0.8b → 0.8c → 0.8d → 0.8e → 0.9.

**Check-in utilisateur recommandés** :
- Après 0.3 : foundation data + Settings timers OK. Engine pas encore actif. Valider qu'on a bien la bonne structure avant de taper dans LedController.
- Après 0.5 : migration events complète. **Checkpoint critique** : comportement visuel équivalent ou amélioré, aucune régression. Si KO, retour arrière ou option de repli.
- Après 0.7 : LedController + ColorSlotStore + Tool 6 complets. Tool 8 toujours placeholder.
- Après 0.8e : Tool 8 refactor complet. Configuration utilisateur complète.
- Après 0.9 : tests manuels exhaustifs validés, docs de référence synchronisées. Phase 0 **mergée**.

Entre chaque step : commit direct sur main (pas de squash, granularité pour `git bisect`), upload **déclenché manuellement par l'utilisateur** (jamais par l'assistant), test visuel du périphérique touché.

---

## 7 · Sortie de Phase 0

Quand Phase 0 est mergée :
- `main` en état stable avec grammaire LED unifiée active, tous les events historiques migrés.
- Aucune feature LOOP ajoutée, mais le terrain est préparé (SettingsStore a ses 3 timers, ColorSlotStore a ses slots VERB_REC / VERB_OVERDUB / VERB_CLEAR_LOOP / VERB_SAVE / VERB_SLOT_CLEAR / MODE_LOOP, PatternId inclut RAMP_HOLD + SPARK, EventId peut s'étendre avec les events LOOP).
- **Phase 1 LOOP** peut démarrer avec son propre plan détaillé, consommant la grammaire LED en place (ajout d'events LOOP à EVENT_RENDER_DEFAULT + EventId, extension table → zéro dette).

---

**Fin du plan Phase 0.** Relecture et validation attendues avant exécution.
