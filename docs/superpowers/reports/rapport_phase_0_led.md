# Rapport Phase 0 — Refactor LED ILLPAD V2

**Date audit** : 2026-04-20
**Auditeur** : session Claude (audit critique read-only)
**Branche** : `main`
**Commits audités** : 13 commits (5c3e57c → 8511b0d)
**Plan source** (archivé) : `docs/archive/2026-04-19-phase0-led-refactor-plan.md`
**Specs sources** :
  - `docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md`
  - `docs/superpowers/specs/2026-04-19-loop-mode-design.md` (§20 timers)

---

## 1. Objet du document

Ce rapport sert 3 usages :

1. **Validation des fixes** à venir — check-list des items résolus.
2. **Mémoire de contexte** — qu'un nouvelle session Claude puisse reprendre ici sans réauditer.
3. **Cadre de décision** — tagging des findings en `[runtime-fix]` / `[ux-defer]` / `[runtime-ok]` pour éviter un fix global qui gaspille du travail sur l'UX avant sa respec.

---

## 2. Synthèse exécutive

### 2.1 Objectif Phase 0

Refactorer le système LED autour d'une **grammaire unifiée 3-couches** (patterns × color slots × event mapping) issue de la LED spec 2026-04-19, **avant** d'implémenter le mode LOOP. Objectif : les events LOOP (REC, OVERDUB, CLEAR, save, load) arriveront dans un système déjà unifié — zéro dette.

### 2.2 État résultant (post commit 8511b0d)

- **Runtime LED** : moteur unifié `triggerEvent(EventId, ledMask)` → résolution `_eventOverrides[evt]` > `EVENT_RENDER_DEFAULT[evt]` → `renderPattern(PatternInstance, now)` qui dispatche sur 9 PatternId. Tick ARPEG share `renderFlashOverlay()`.
- **NVS** : 3 Store bumpés (SettingsStore v10→v11, LedSettingsStore v5→v6, ColorSlotStore v3→v4), Zero Migration Policy appliquée, toutes les validators à jour.
- **Tool 6** : 3 timers LOOP éditables (clearLoopTimerMs/slotSaveTimerMs/slotClearTimerMs).
- **Tool 8** : 3 pages refondues (PATTERNS / COLORS / EVENTS) — **fonctionnelles mais ergonomie à respec**.
- **Docs référence** : architecture-briefing.md + nvs-reference.md synchronisés.
- **Dual-path** : complètement retiré. Grep `triggerConfirm|ConfirmType|CONFIRM_*` = 0 occurrence métier.

### 2.3 Verdict global Phase 0

**Phase 0 merge-able structurellement** avec **2 runtime latents** (bgFactor + SPARK params non consommés) qui ne se manifestent qu'en Phase 1+ LOOP, et **une ergonomie Tool 8 à repenser** (point raised par l'utilisateur en hardware testing).

**Le moteur LED fonctionne** pour tous les events métier Phase 0 (bank switch, scale, octave, hold, tick ARPEG).

---

## 3. Architecture résultante

### 3.1 Grammaire LED 3-couches

```
COUCHE 1 — Palette de patterns (LedGrammar.h)
  9 entrées fixes : PTN_SOLID, PTN_PULSE_SLOW, PTN_CROSSFADE_COLOR,
                    PTN_BLINK_SLOW, PTN_BLINK_FAST, PTN_FADE,
                    PTN_FLASH, PTN_RAMP_HOLD, PTN_SPARK
  + sentinel PTN_NONE (0xFF)
  Params par pattern dans union PatternParams (<=16 bytes)

COUCHE 2 — Color slots (KeyboardData.h ColorSlotStore v4)
  15 slots ColorSlotId :
    MODE_NORMAL/ARPEG/LOOP (3)
    VERB_PLAY/REC/OVERDUB/CLEAR_LOOP/SLOT_CLEAR/SAVE (6)
    BANK_SWITCH, SCALE_ROOT/MODE/CHROM, OCTAVE (5)
    CONFIRM_OK (1)
  Chaque slot = {preset (0-13) + hueOffset (-128..127)}
  resolveColorSlot() → RGBW

COUCHE 3 — Event mapping (LedGrammar.cpp EVENT_RENDER_DEFAULT[])
  17 entrées EventId (10 Phase 0 + 7 LOOP reserved PTN_NONE)
  Chaque entrée = {patternId, colorSlot, fgPct}
  Override runtime via LedSettingsStore.eventOverrides[EVT_COUNT]
  Sentinel PTN_NONE → fallback sur default
```

### 3.2 Pipeline runtime

```
Callsite métier (BankManager, main.cpp, etc.)
  → s_leds.triggerEvent(EVT_XXX, ledMask)
      → _eventOverrides[evt] ou EVENT_RENDER_DEFAULT[evt]
      → resolve RGBW via _colors[colorSlot]
      → populate PatternInstance _eventOverlay
      → preempt any previous overlay
  → LedController::update() appelé à chaque frame
      → renderConfirmation() → renderPattern(_eventOverlay, now)
          → dispatch sur patternId (9 cas)
          → applies FLASH overlay via renderFlashOverlay() (shared)
      → clear overlay si isPatternExpired()
      → renderNormalDisplay() en dessous (non-cleared)
```

### 3.3 Fichiers clés

| Fichier | Rôle |
|---|---|
| `src/core/LedGrammar.h` | PatternId, EventId, PatternParams union, EventRenderEntry |
| `src/core/LedGrammar.cpp` | EVENT_RENDER_DEFAULT[17] table |
| `src/core/LedController.{h,cpp}` | Pattern engine, render helpers, 9-level priority |
| `src/core/KeyboardData.h` | LedSettingsStore v6 + ColorSlotStore v4 + SettingsStore v11 + validators |
| `src/managers/NvsManager.cpp` | Compile-time defaults + descriptor table |
| `src/setup/ToolLedSettings.{h,cpp}` | Tool 8 UI 3 pages |
| `src/setup/ToolSettings.cpp` | Tool 6 avec 3 timers LOOP |

### 3.4 Tableau NVS versions final

| Store | Namespace | Version | Size | Owner |
|---|---|---|---|---|
| `SettingsStore` | `illpad_set`/`settings` | **v11** | 20B | Tool 6 |
| `LedSettingsStore` | `illpad_lset`/`ledsettings` | **v6** | ~90B | Tool 8 |
| `ColorSlotStore` | `illpad_lset`/`ledcolors` | **v4** | 34B | Tool 8 |

Bump Phase 0 : 3 resets utilisateur au premier boot post-flash (Serial.printf warning à chaque).

---

## 4. Findings consolidés par sévérité

### Légende tagging

- `[runtime-fix]` — À fixer avant brainstorm Tool 8 (moteur doit fonctionner complètement).
- `[ux-defer]` — À ne PAS fixer avant respec Tool 8 (le fix serait jeté).
- `[runtime-ok]` — Observationnel / noté, pas un blocker pour Phase 1+.
- `[phase1+]` — À adresser en Phase 1+ LOOP.

### 4.1 Bloquants

Aucun. Phase 0 structurellement intègre.

### 4.2 Runtime (findings à fixer pour moteur complet)

#### R1 — `_bgFactor` orphan field `[runtime-fix]`
- **Origine** : commit 0.6 (ac8d18c), aggravé 0.8d (ff780e3).
- **Code** : `LedController.cpp:32` init, `LedController.cpp:883` load depuis store, **jamais lu ailleurs**.
- **Description** : Plan §0.6 exigeait "renderBankNormal/Arpeg appliquent bgFactor à la couleur FG pour dériver la couleur BG". Code réel utilise encore les intensités legacy (`normalBgIntensity`, `_bgArpStopMin`, `_bgArpPlayMin`) indépendantes.
- **Impact Phase 0** : null (BG intensités fonctionnent via les fields legacy).
- **Impact Phase 1+** : `bgFactor` reste éditable en Tool 8 (quand respec) mais sans effet → UX trompeuse.
- **Viole** : invariant §9.7 briefing (4-link chain orphan).
- **Commentaires code trompeurs** : `LedController.cpp:322-327, 410-411` disent "bgFactor is applied at setPixel intensity" — faux.
- **Criteria de résolution** :
  1. `renderBankNormal` : BG utilise `setPixel(led, _colors[CSLOT_MODE_NORMAL], _normalFgIntensity * _bgFactor / 100)` au lieu de `_normalBgIntensity`.
  2. `renderBankArpeg` : idem BG pour ARPEG.
  3. Bargraph `barDim` idem si applicable.
  4. Retirer les commentaires trompeurs ou les remettre exacts.
  5. Décision à prendre : **conserver `normalBgIntensity`/`bgArpStopMin`/`bgArpPlayMin` comme fields dépréciés** OU les retirer en bump v6→v7 (coût : NVS reset supplémentaire).

#### R2 — SPARK / RAMP_HOLD params non consommés `[runtime-fix]`
- **Origine** : commit 0.8d (ff780e3).
- **Code** : `LedController.cpp:31` init, `LedController.cpp:880-882` load `_sparkOnMs/_sparkGapMs/_sparkCycles`, **triggerEvent(PTN_SPARK) utilise 50/70/2 hardcoded** (`LedController.cpp:635-637`). Même bug pour PTN_RAMP_HOLD suffix (`LedController.cpp:651-654`).
- **Impact Phase 0** : latent. EVT_CONFIRM_OK utilise SPARK mais n'est triggeré par aucun callsite métier. EVT_LOOP_* utilisent RAMP_HOLD mais sont PTN_NONE (no render).
- **Impact Phase 1+** : sérieux. User modifie SPARK params en Tool 8, sauve, reboot → **sans effet visuel** quand Phase 1+ branchera les events SPARK/RAMP_HOLD. Trust brisé.
- **Criteria de résolution** :
  1. `triggerEvent case PTN_SPARK` : remplacer `50/70/2` par `_sparkOnMs/_sparkGapMs/_sparkCycles`.
  2. `triggerEvent case PTN_RAMP_HOLD` : idem pour `suffixOnMs/GapMs/Cycles`. `rampMs` reste hardcoded (sera dérivé en Phase 1+ via SettingsStore timers).

#### R3 — `CROSSFADE_COLOR.periodMs` hardcoded 800ms `[runtime-fix]` (optionnel)
- **Origine** : commit 0.4 (1869fd8), resté en 0.8c/d.
- **Code** : `LedController.cpp:642` `crossfadeColor.periodMs = 800`. Aucun field store.
- **Impact Phase 0** : null (EVT_WAITING pas encore triggered).
- **Impact Phase 1+** : user ne peut pas tuner la période du WAITING. Déviation silencieuse spec §10.
- **Criteria de résolution** (2 options) :
  - **Option A** : Ajouter `crossfadePeriodMs` à LedSettingsStore v7 (bump → reset supplémentaire). Plus propre.
  - **Option B** : Rester hardcoded, documenter la lacune dans le plan Phase 1+ (risque plus modéré vu la rareté des WAITING LOOP).
- **Recommandation** : Option A si on bump déjà pour R1 (NVS reset déjà pris), sinon Option B.

### 4.3 Incohérences UX Tool 8 (à différer)

#### U1 — Grid 4×4 COLORS au lieu de liste verticale `[ux-defer]`
- **Origine** : commit 0.8b (0477446).
- **Plan §0.8b** exigeait grid 4×4 ; code fait liste verticale.
- **Hardware testing** : user confirme regression de scannabilité.
- **Résolution** : à intégrer dans la respec Tool 8.

#### U2 — Absence de bloc Unicode coloré `[ux-defer]`
- **Origine** : commit 0.8b.
- **Plan §0.8b** exigeait sample visuel couleur VT100 ; code affiche preset name texte.
- **Hardware testing** : user confirme regression.
- **Résolution** : à intégrer dans la respec.

#### U3 — Navigation preset/hue contre-intuitive `[ux-defer]`
- **Origine** : commit 0.8b.
- **Code** : UP/DOWN toggle focus PRESET/HUE, LEFT/RIGHT adjust value.
- **Hardware testing** : user propose LEFT/RIGHT pour preset + UP/DOWN pour hue directement (pas de focus toggle).
- **Résolution** : à intégrer dans la respec.

#### U4 — Noms slots techniques (MODE_NORMAL, VERB_REC) `[ux-defer]`
- **Origine** : commit 0.8b, persiste 0.8d.
- **Impact** : incompréhensibles pour musicien.
- **Résolution** : redéfinir `COLOR_SLOT_LABELS[]` + `PHASE0_EVENT_LABELS[]` avec des noms orientés musicien dans la respec.

#### U5 — Page EVENTS modèle mental cassé `[ux-defer]` **(critique)**
- **Origine** : commit 0.8d (ff780e3).
- **Description** : l'utilisateur est confronté à **10 events × 3 params = 30 cellules**, chacune pouvant casser la grammaire. Modèle "pattern/color/fgPct" est correct code-side mais **mauvais UX-side**. Un musicien pense en intentions (plus voyant, plus rapide, plus discret), pas en patterns mathématiques.
- **Hardware testing** : user confirme "absolument incompréhensible".
- **Résolution** : **redéfinir la spec UX du Tool 8** avant toute réimplémentation. Probablement simplifier EVENTS à "couleur + intensité + durée" et cacher le choix du pattern (ou dans une vue avancée). Peut fusionner PATTERNS + EVENTS.

#### U6 — Live preview LED 3-4 PATTERNS page manquant `[ux-defer]`
- **Origine** : commit 0.8c (d968b10).
- **Plan §0.8c** exigeait preview pattern live sur LED 3-4. Code : rien. COLORS page a son preview, PATTERNS non.
- **Résolution** : à redéfinir dans la respec.

#### U7 — Touche `b` preview trigger events manquante `[ux-defer]`
- **Origine** : commit 0.8d.
- **Plan §0.8d** exigeait "touche `b` → trigger l'event sur LED 3-4".
- **Résolution** : respec Tool 8.

#### U8 — Conventions setup-tools §10 violées (vtClear exit, delay) `[ux-defer]`
- **Origine** : commit 0.2 (placeholder), persiste 0.8a/b/c/d/e.
- **Code** : `_ui->vtClear()` manquant avant `return` ; `delay(10)` au lieu de `delay(5)`.
- **Résolution** : à intégrer dans la réimpl Tool 8.

#### U9 — Touche `d` sans confirm (acceptable user-validated) `[runtime-ok]`
- **Origine** : commit 0.8b.
- **Plan** demandait confirm inline. Code fait reset direct.
- **User explicitly accepted** : "on accepte pas de confirm".
- **Résolution** : aucune.

#### U10 — Duplication defaults `_lwk` vs NvsManager (28 fields) `[ux-defer]`
- **Origine** : commit 0.8c.
- **Résolution** : à factoriser dans un helper partagé lors de la réimpl. Sans urgence (defaults stables).

### 4.4 Observations sans action

#### O1 — Badge NVS T8 dépend des 2 stores `[runtime-ok]`
- **Comportement** : le menu check LedSettings + ColorSlots. Si l'un est invalide (Tool 8 jamais saved), badge = `--`.
- **Résolu** en 0.8d par le fait qu'un save PATTERNS sauve LedSettings → badge OK ensuite.

#### O2 — Nommage events LOOP divergent plan `[runtime-ok]`
- **Origine** : commit 0.1.
- Plan : `EVT_LOOP_OVERDUB_START`, `EVT_LOOP_CLEAR_LOOP`.
- Code : `EVT_LOOP_OVERDUB`, `EVT_LOOP_CLEAR`.
- Code plus cohérent avec spec §12. Non-bug.

#### O3 — `EVT_WAITING` colorB implicite `[phase1+]`
- **Origine** : commit 0.1, commit 0.4.
- `CROSSFADE_COLOR` nécessite 2 couleurs mais `EventRenderEntry` n'en porte qu'une. Code hardcode `colorB = colorA` (ligne 644) — pas de vrai crossfade.
- **Résolution** : à traiter quand LOOP WAITING nécessitera 2 couleurs réelles. Options : 2 events distincts, ou `colorB` field additionnel, ou engine state (verb précédent).

#### O4 — `LED_CONFIRM_UNIT_MS` constante morte `[runtime-ok]`
- `HardwareConfig.h:291`. Pré-existe avant Phase 0, jamais utilisée.
- **Résolution** : suppression triviale en cleanup futur.

#### O5 — `PatternParams::raw[10]` oversized `[runtime-ok]`
- `LedGrammar.h:89`. Le plus gros sous-struct est 8 bytes (rampHold), raw[8] suffirait.
- **Résolution** : cosmétique, pas urgent.

#### O6 — Commentaire validator stale `[runtime-ok]`
- `KeyboardData.h:681` dit "step 0.6 = 16 v4" mais c'est 15.
- **Résolution** : patch 1 ligne.

#### O7 — BLINK events n'ont pas de brightness global éditable `[ux-defer]`
- Relevé en review 0.8c. `bankBrightnessPct` existe pour BANK_SWITCH mais non éditable en PATTERNS page (qui dit "per-event"). SCALE/OCTAVE/REFUSE hardcoded 100.
- **Résolution** : à intégrer dans la respec Tool 8 (probablement via un champ "intensity" par event).

#### O8 — Comment fallthrough trompeur dans renderPatternParamsPanel `[runtime-ok]`
- `ToolLedSettings.cpp` case PTN_RAMP_HOLD dit `// Fallthrough to SPARK` mais il y a un break. Copy-paste implicite.
- **Résolution** : cosmétique, à nettoyer.

---

## 5. Plan de fix runtime (commit groupé proposé)

### 5.1 Scope

Fixer uniquement les findings `[runtime-fix]` :
- **R1** : bgFactor consumption dans renderBankNormal/Arpeg.
- **R2** : SPARK/RAMP_HOLD params consumption dans triggerEvent.
- **R3** : `crossfadePeriodMs` **optionnel** — recommandation : ne PAS l'inclure dans ce commit pour éviter un bump NVS additionnel. Laisser hardcoded 800ms, documenter pour Phase 1+.

**Non inclus** : tout [ux-defer] et [runtime-ok] (sauf O4/O5/O6/O8 cosmétiques si temps le permet, négligeables).

### 5.2 Impact NVS

**Aucun bump nécessaire si on n'ajoute pas `crossfadePeriodMs`** :
- R1 consomme `bgFactor` (déjà dans v6). Pas de changement struct.
- R2 consomme `_sparkOnMs/GapMs/Cycles` (déjà dans v6). Pas de changement struct.

→ **Pas de reset utilisateur supplémentaire**. Commit runtime pur.

### 5.3 Changements code attendus

**Fichier `src/core/LedController.cpp`** :

```cpp
// ~ligne 400, renderBankArpeg — FG/BG via _bgFactor
// AVANT :
//   const RGBW& col = _colors[CSLOT_MODE_ARPEG];
//   setPixel(led, col, isFg ? _fgArpPlayMax : _bgArpPlayMin);
// APRÈS (proposition) :
//   const RGBW& col = _colors[CSLOT_MODE_ARPEG];
//   uint8_t intensity = isFg
//     ? _fgArpPlayMax
//     : (_fgArpPlayMax * _bgFactor / 100);
//   setPixel(led, col, intensity);
// (idem pour les autres branches stopped-loaded, idle)

// ~ligne 412, renderBankNormal
// AVANT :
//   setPixel(led, _colors[CSLOT_MODE_NORMAL],
//            isFg ? _normalFgIntensity : _normalBgIntensity);
// APRÈS :
//   setPixel(led, _colors[CSLOT_MODE_NORMAL],
//            isFg ? _normalFgIntensity
//                 : (_normalFgIntensity * _bgFactor / 100));

// ~ligne 635, triggerEvent case PTN_SPARK
// AVANT :
//   _eventOverlay.params.spark.onMs   = 50;
//   _eventOverlay.params.spark.gapMs  = 70;
//   _eventOverlay.params.spark.cycles = 2;
// APRÈS :
//   _eventOverlay.params.spark.onMs   = _sparkOnMs;
//   _eventOverlay.params.spark.gapMs  = _sparkGapMs;
//   _eventOverlay.params.spark.cycles = _sparkCycles;

// ~ligne 651, triggerEvent case PTN_RAMP_HOLD
// AVANT :
//   _eventOverlay.params.rampHold.rampMs       = 500;
//   _eventOverlay.params.rampHold.suffixOnMs   = 50;
//   _eventOverlay.params.rampHold.suffixGapMs  = 70;
//   _eventOverlay.params.rampHold.suffixCycles = 2;
// APRÈS :
//   _eventOverlay.params.rampHold.rampMs       = 500;  // Phase 1+ derives from SettingsStore timer
//   _eventOverlay.params.rampHold.suffixOnMs   = _sparkOnMs;
//   _eventOverlay.params.rampHold.suffixGapMs  = _sparkGapMs;
//   _eventOverlay.params.rampHold.suffixCycles = _sparkCycles;

// Cleanup commentaires lignes 322-327 et 410-411 : supprimer ou corriger les mentions trompeuses "bgFactor applied".
```

### 5.4 Tests hardware attendus

1. **Rendu BG** : banks BG doivent avoir la **même teinte** que FG, intensité réduite à `bgFactor%` (25% par défaut). Modifier bgFactor en Tool 8 PATTERNS → changement visuel immédiat attendu sur toutes les banks BG.

2. **SPARK params** : pas de test Phase 0 direct (EVT_CONFIRM_OK pas trigger). Peut se vérifier visuellement dans une preview LED 3-4 si le brainstorm respec Tool 8 l'ajoute. Ou valider via dump NVS (valeur persiste et se charge).

3. **RAMP_HOLD suffix** : même contrainte que SPARK (pas de callsite Phase 0).

4. **Régression** : bank switch, scale, octave, hold, tick ARPEG → rendu inchangé versus 0.9.

### 5.5 Critères done du commit de fix

- [ ] `renderBankNormal` BG utilise bgFactor (5 lignes modifiées).
- [ ] `renderBankArpeg` BG utilise bgFactor (3 branches).
- [ ] `triggerEvent(PTN_SPARK)` consomme `_sparkOnMs/GapMs/Cycles`.
- [ ] `triggerEvent(PTN_RAMP_HOLD)` consomme `_sparkOnMs/GapMs/Cycles` pour suffix.
- [ ] Commentaires trompeurs lignes 322-327, 410-411 corrigés ou supprimés.
- [ ] Build clean, pas de régression visuelle métier Phase 0.
- [ ] Badge NVS reste OK (pas de bump version).
- [ ] Commit message liste les findings résolus par ID (R1, R2).

---

## 6. Dettes différées (liste post-commit fix runtime)

Les items `[ux-defer]` (U1-U10 sauf U9) sont à traiter dans un **plan Phase 0.1 respec UX Tool 8** à rédiger séparément après brainstorm.

**Ne pas toucher avant brainstorm** :
- `COLOR_SLOT_LABELS[]` naming
- `PHASE0_EVENT_LABELS[]` naming
- Grid 4×4 ou autre layout COLORS
- Navigation preset/hue
- Page EVENTS modèle mental
- Preview live PATTERNS/EVENTS
- Touche `b` trigger
- Conventions setup-tools §10 (vtClear exit, delay)

### 6.1 Questions à préparer pour le brainstorm

1. **Qui utilise le Tool 8 et pour quoi ?** Musicien en concert ? Luthier en config initiale ? Entre les deux ?
2. **Quel niveau de contrôle est nécessaire ?** Intensité + couleur + timing suffisent-ils pour 95% des tunings ? Le choix du pattern est-il un contrôle expert ?
3. **Faut-il 3 pages ou 2 (fusion PATTERNS+EVENTS) ou 1 (vue unifiée par event) ?**
4. **Comment nommer les events pour un musicien ?** Traduction française ? Anglais simple ? Acronymes ?
5. **Faut-il cacher les patterns LOOP (REC, OVERDUB, CLEAR, SAVE) en Phase 0 ?** (Ils existent mais pas encore utilisables.)
6. **Preview : LED 3-4 comme Tool 7 ou autre paire ?**

---

## 7. Continuité — instructions pour session neuve

Si un contexte neuf reprend ce projet, lire **dans cet ordre** :

1. **Ce rapport** en entier (section 1 à 6).
2. `.claude/CLAUDE.md` racine projet (conventions ILLPAD V2).
3. `docs/reference/architecture-briefing.md` §0 Triage → §1 MM7 + §5 P5 (moteur LED).
4. `docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md` (spec grammaire).
5. Pour fix runtime R1/R2 : `src/core/LedController.cpp` lignes 322-327, 400-444, 635-655, 880-883.

### 7.1 État attendu du repo au moment où le fix runtime est à faire

- HEAD = `main` @ commit `8511b0d` (step 0.9) ou plus récent.
- Build clean esp32-s3-devkitc-1.
- Grep `triggerConfirm|ConfirmType|CONFIRM_*` → 0 occurrence métier.
- NVS versions : SettingsStore v11, LedSettingsStore v6, ColorSlotStore v4.

### 7.2 Ordre de travail recommandé

1. Valider mentalement le plan de fix runtime (§5 de ce rapport).
2. Rédiger le commit de fix runtime (section 5.3).
3. Build + demander upload utilisateur.
4. Validation hardware : bank switch / BG teintes / Tool 8 PATTERNS bgFactor modifiable → changement visible.
5. Commit.
6. **Brainstorm Tool 8** avec l'utilisateur (questions §6.1). Output = `2026-04-XX-tool8-respec-plan.md`.
7. Implémenter respec Tool 8 contre le nouveau plan.
8. Marquer Phase 0.1 close. Phase 1 LOOP peut commencer.

---

## 8. Annexes

### 8.1 Liste commits Phase 0 audités

| # | SHA | Step | Title |
|---|---|---|---|
| 1 | 5c3e57c | 0.1 | LedGrammar foundation |
| 2 | 1233818 | 0.2 | LedSettingsStore v6 + Tool 8 placeholder |
| 3 | dec9391 | 0.3 | SettingsStore +3 LOOP timers |
| 4 | 1869fd8 | 0.4 | LedController pattern engine |
| 5 | dc4727c | 0.5 | migrate callsites to triggerEvent + tick ARPEG |
| 6 | ac8d18c | 0.6 | ColorSlotStore v4 (15 slots) |
| 7 | 3ab8458 | 0.7 | Tool 6 exposes 3 LOOP RAMP_HOLD timers |
| 8 | c6d6416 | 0.8a | Tool 8 skeleton (3-page navigation) |
| 9 | 0477446 | 0.8b | Tool 8 COLORS page |
| 10 | d968b10 | 0.8c | Tool 8 PATTERNS page (pool + field editor) |
| 11 | ff780e3 | 0.8d | Tool 8 EVENTS page + NVS override wiring |
| 12 | b3ae4a6 | 0.8e | Tool 8 polish (header + cleanup) |
| 13 | 8511b0d | 0.9 | dual-path cleanup + reference doc sync |

(Commit `2798656` docs(bugs) entre 0.1 et 0.2 : hors scope Phase 0, non audité.)

### 8.2 15 Color slots (v4)

| # | ID | Default preset | Hue | Usage |
|---|---|---|---|---|
| 0 | MODE_NORMAL | Warm White (1) | 0 | Bank NORMAL identity |
| 1 | MODE_ARPEG | Ice Blue (3) | 0 | Bank ARPEG identity |
| 2 | MODE_LOOP | Gold (7) | 0 | Bank LOOP (Phase 1+) |
| 3 | VERB_PLAY | Green (11) | 0 | PLAY, tick ARPEG, HOLD capture |
| 4 | VERB_REC | Coral (8) | 0 | RECORDING + REFUSE blink |
| 5 | VERB_OVERDUB | Amber (6) | 0 | OVERDUBBING |
| 6 | VERB_CLEAR_LOOP | Cyan (5) | 0 | CLEAR long-press |
| 7 | VERB_SLOT_CLEAR | Amber (6) | **+20** | Slot delete (hue distinct d'OVERDUB) |
| 8 | VERB_SAVE | Magenta (10) | 0 | Slot save |
| 9 | BANK_SWITCH | Pure White (0) | 0 | Bank switch blink |
| 10 | SCALE_ROOT | Amber (6) | 0 | Scale root change |
| 11 | SCALE_MODE | Gold (7) | 0 | Scale mode change |
| 12 | SCALE_CHROM | Coral (8) | 0 | Scale chromatic |
| 13 | OCTAVE | Violet (9) | 0 | Octave change |
| 14 | CONFIRM_OK | Pure White (0) | 0 | SPARK suffix universel |

### 8.3 10 Events Phase 0 + defaults

| # | EventId | Default Pattern | Default Color | fgPct | Consommé Phase 0 |
|---|---|---|---|---|---|
| 0 | BANK_SWITCH | BLINK_SLOW | BANK_SWITCH | 100 (override bankBrightnessPct 80) | Oui |
| 1 | SCALE_ROOT | BLINK_FAST | SCALE_ROOT | 100 | Oui |
| 2 | SCALE_MODE | BLINK_FAST | SCALE_MODE | 100 | Oui |
| 3 | SCALE_CHROM | BLINK_FAST | SCALE_CHROM | 100 | Oui |
| 4 | OCTAVE | BLINK_FAST | OCTAVE | 100 | Oui |
| 5 | PLAY | FADE 0→100 | VERB_PLAY | 100 | Oui |
| 6 | STOP | FADE 100→0 | VERB_PLAY (option γ) | 100 | Oui |
| 7 | WAITING | CROSSFADE_COLOR | MODE_ARPEG (colorA placeholder) | 100 | Non (EVT pas trigger) |
| 8 | REFUSE | BLINK_FAST | VERB_REC | 100 | Non |
| 9 | CONFIRM_OK | SPARK | CONFIRM_OK | 100 | Non (aucun callsite) |

7 events LOOP reservés (10-16) : tous `PTN_NONE` en default, câblés en Phase 1+.

---

**Fin du rapport.** Ce document est la source de vérité pour la validation des fix runtime à venir et l'entrée en brainstorm respec Tool 8.
