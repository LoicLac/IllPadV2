# Tool 8 — UX Respec Design (Phase 0.1)

**Date** : 2026-04-20
**Statut** : brouillon à valider
**Scope** : refonte complète de l'ergonomie du Tool 8 LED Settings, après l'audit Phase 0 qui a révélé une structure inutilisable pour un musicien. Le runtime LED est conservé (grammaire 3 couches, triggerEvent, pattern engine), seule l'exposition UX change.

**Références** :
- `docs/superpowers/reports/rapport_phase_0_led.md` — audit Phase 0 + classification des findings
- `docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md` — grammaire LED sous-jacente
- `docs/reference/setup-tools-conventions.md` — conventions interactionnelles
- `docs/reference/vt100-design-guide.md` — esthétique VT100
- `docs/reference/architecture-briefing.md` §1 MM7, §5 P5, §8 LEDs entry — modèle mental runtime

---

## 1. Problème à résoudre

Le Tool 8 actuel (post-Phase 0) expose directement la grammaire runtime au musicien :
- 15 color slots à plat (COLORS page)
- 9 patterns avec sous-params (PATTERNS page)
- 10 events × 3 fields (pattern/color/fgPct) (EVENTS page)

Pour un musicien, c'est 40+ contrôles organisés par **concepts code** (PatternId, ColorSlotId, EventRenderEntry) plutôt que par **concepts musicaux** (modes de jeu, gestes transport, confirmations). Un musicien ne sait pas ce que "VERB_OVERDUB" signifie, ni pourquoi un event a un "pattern" configurable.

**Conséquence** : l'outil est utilisable par le développeur qui a écrit la spec, pas par le musicien qui utilise l'instrument. Validé par test hardware (2026-04-20).

## 2. Objectif

Refondre l'UI du Tool 8 pour qu'un musicien puisse :
- Voir **d'un coup d'œil** les familles de params (modes, transport, confirmations).
- Comprendre les **liens implicites** entre params (ex. PLAY/STOP partagés ARPEG+LOOP, BG dérivé de FG).
- Régler **ce qu'un musicien règle** : couleurs, intensités, durées, pas de choix techniques orthogonaux.
- **Voir en live** l'effet de chaque réglage sur les LEDs réelles.

**Principe directeur** : "le Tool 8 est un mini-manuel musicien". Les labels, la structure et les previews servent la compréhension, pas la technique interne.

## 3. Scope

**Dans le scope** :
- Nouvelle structure UI (6 sections).
- Nouveau paradigme de navigation et d'édition.
- Live preview contextuel via helper dédié.
- Extensions moteur nécessaires (nouveaux slots/fields, sans refonte).
- Renommage user-facing des labels.
- Mise à jour documentaire (briefing + nvs-reference + setup-tools-conventions si nouveau paradigme).

**Hors scope** :
- Modification de la grammaire runtime 3 couches (patterns/slots/events).
- Ajout de nouveaux patterns ou events.
- Phase 1+ LOOP — les params LOOP sont exposés dans cette spec mais leur consommation runtime viendra en Phase 1+.
- Tool 1-7, CLI, setup manager.

## 4. Structure UI finale

### 4.1 Layout — 6 sections verticales

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│  ILLPAD48 SETUP CONSOLE       TOOL 8: LED SETTINGS                            NVS:OK        │
├─────────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                             │
│  ┌── NORMAL ────────────────────────────────────────────────────────────────────────────┐  │
│  │  Base color             [Warm White]     +0                            ▓▓ sample     │  │
│  │  FG brightness          85 %                                                         │  │
│  └──────────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                             │
│  ┌── ARPEG ─────────────────────────────────────────────────────────────────────────────┐  │
│  │  Base color             [Ice Blue]       +0                            ▓▓ sample     │  │
│  │  FG brightness          80 %                                                         │  │
│  └──────────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                             │
│  ┌── LOOP ──────────────────────────────────────────────────────────────────────────────┐  │
│  │  Base color             [Gold]           +0                            ▓▓ sample     │  │
│  │  FG brightness          80 %                                                         │  │
│  │                                                                                      │  │
│  │  Save slot              [Magenta]        +0                            ▓▓ sample     │  │
│  │    duration             1000 ms    (gesture hold + feedback)                         │  │
│  │  Clear loop (hold)      [Cyan]           +0                            ▓▓ sample     │  │
│  │    duration              500 ms    (gesture hold + feedback)                         │  │
│  │  Clear slot (combo)     [Amber]          +20                           ▓▓ sample     │  │
│  │    duration              800 ms    (feedback only, gesture is instant)               │  │
│  └──────────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                             │
│  ┌── TRANSPORT ─────────────────────────────────────────────────────────────────────────┐  │
│  │  Play fade-in           [Green]          +0                            ▓▓ sample     │  │
│  │    brightness  100 %    duration   500 ms                                            │  │
│  │                                                                                      │  │
│  │  Stop fade-out          [Coral]          +0                            ▓▓ sample     │  │
│  │    brightness  100 %    duration   500 ms                                            │  │
│  │                                                                                      │  │
│  │  Waiting quantise       [Green]          +0                            ▓▓ sample     │  │
│  │                                                                                      │  │
│  │  Breathing (stopped-loaded, shared ARPEG + LOOP)                                     │  │
│  │    min %  60    max %  90    period  2500 ms                                         │  │
│  │                                                                                      │  │
│  │  ── TICK ──                                                                          │  │
│  │  Tick common            FG %  100    BG %  25                                        │  │
│  │                                                                                      │  │
│  │  colors                                                                              │  │
│  │    Tick PLAY            [Green]          +0                            ▓▓ sample     │  │
│  │    Tick REC             [Coral]          +0                            ▓▓ sample     │  │
│  │    Tick OVERDUB         [Amber]          +0                            ▓▓ sample     │  │
│  │                                                                                      │  │
│  │  durations                                                                           │  │
│  │    Tick BEAT             30 ms                                                       │  │
│  │    Tick BAR              60 ms                                                       │  │
│  │    Tick WRAP            100 ms                                                       │  │
│  └──────────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                             │
│  ┌── CONFIRMATIONS ─────────────────────────────────────────────────────────────────────┐  │
│  │  Bank switch            [Pure White]     +0                            ▓▓ sample     │  │
│  │    brightness   80 %    duration   300 ms                                            │  │
│  │                                                                                      │  │
│  │  Scale root             [Amber]          +0                            ▓▓ sample     │  │
│  │    brightness  100 %    duration   200 ms                                            │  │
│  │  Scale mode             [Gold]           +0                            ▓▓ sample     │  │
│  │    brightness  100 %    duration   200 ms                                            │  │
│  │  Scale chromatic        [Coral]          +0                            ▓▓ sample     │  │
│  │    brightness  100 %    duration   200 ms                                            │  │
│  │                                                                                      │  │
│  │  Octave                 [Violet]         +0                            ▓▓ sample     │  │
│  │    brightness  100 %    duration   300 ms                                            │  │
│  │                                                                                      │  │
│  │  Confirm OK (SPARK)     [Pure White]     +0                            ▓▓ sample     │  │
│  │    on  50 ms    gap  70 ms    cycles  2                                              │  │
│  └──────────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                             │
│  ┌── GLOBAL ────────────────────────────────────────────────────────────────────────────┐  │
│  │  BG factor              25 %    (all BG banks : FG × this ratio)                     │  │
│  │  Master gamma           2.0                                                          │  │
│  └──────────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                             │
├─────────────────────────────────────────────────────────────────────────────────────────────┤
│   DESCRIPTION PANEL (fills remaining space with description of line under cursor)           │
├─────────────────────────────────────────────────────────────────────────────────────────────┤
│   [↑↓] nav  [←→] skip section  [ENTER] edit  [d] default  [q] exit                         │
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 Nombre de lignes navigables

Environ 40 lignes éditables + titres de sections. Scroll vertical automatique. Les multi-values (ex. `brightness 100 %  duration 500 ms`) restent sur une seule ligne (scannabilité prioritaire sur "1 valeur = 1 ligne").

### 4.3 Éléments NON exposés

Conservés hardcoded runtime, pas d'UI :
- **Refuse blink** (3 blinks rouge, reuse CSLOT_VERB_REC color automatiquement).
- **WAITING period** (hardcoded 800 ms, constant dans renderPattern).
- **LOOP tick durations différenciées ARPEG/LOOP** (unifiées via durées BEAT/BAR/WRAP).
- **Boot sequence, setup comet, battery gauge, error blink, pot bargraph** (cosmétique système, non tunable).

### 4.4 Labels user-facing

Remplacement des labels techniques par des termes musicaux :

| Technique (v0.8) | Musicien (respec) |
|---|---|
| `CSLOT_MODE_NORMAL` | "Base color" dans section NORMAL |
| `CSLOT_MODE_ARPEG` | "Base color" dans section ARPEG |
| `CSLOT_MODE_LOOP` | "Base color" dans section LOOP |
| `CSLOT_VERB_PLAY` | "Play fade-in" dans TRANSPORT |
| `CSLOT_VERB_STOP` (new) | "Stop fade-out" dans TRANSPORT |
| `CSLOT_VERB_REC` | "Tick REC" dans TRANSPORT (reuse pour REFUSE caché) |
| `CSLOT_VERB_OVERDUB` | "Tick OVERDUB" dans TRANSPORT |
| `CSLOT_VERB_SAVE` | "Save slot" dans LOOP |
| `CSLOT_VERB_CLEAR_LOOP` | "Clear loop (hold)" dans LOOP |
| `CSLOT_VERB_SLOT_CLEAR` | "Clear slot (combo)" dans LOOP |
| `CSLOT_BANK_SWITCH` | "Bank switch" dans CONFIRMATIONS |
| `CSLOT_SCALE_ROOT/MODE/CHROM` | "Scale root/mode/chromatic" dans CONFIRMATIONS |
| `CSLOT_OCTAVE` | "Octave" dans CONFIRMATIONS |
| `CSLOT_CONFIRM_OK` | "Confirm OK (SPARK)" dans CONFIRMATIONS |

Les enum identifiers restent techniques dans le code — le changement est **visible uniquement dans Tool 8 UI**.

### 4.5 Ticks — architecture en 2 dimensions

Les ticks sont exposés avec **décorrélation** des 2 dimensions :
- **Dimension couleur (action)** : 3 slots éditables — `Tick PLAY`, `Tick REC`, `Tick OVERDUB`.
- **Dimension durée (temporalité)** : 3 values éditables — `Tick BEAT`, `Tick BAR`, `Tick WRAP`.
- **FG % / BG %** : 1 set partagé pour tous les ticks (`Tick common`).

Le runtime dispatch combine les 2 dimensions :

| Événement | Couleur | Durée |
|---|---|---|
| ARPEG playing step | `Tick PLAY` | `Tick BEAT` |
| LOOP recording bar | `Tick REC` | `Tick BAR` |
| LOOP recording wrap | `Tick REC` | `Tick WRAP` |
| LOOP playing wrap | `Tick PLAY` | `Tick WRAP` |
| LOOP overdubbing wrap | `Tick OVERDUB` | `Tick WRAP` |

Ainsi, le musicien règle "quelle couleur" (par action) et "à quelle vitesse" (par fréquence) indépendamment. 6 contrôles pour les ticks au lieu de 3×2=6 configs couplées.

## 5. Navigation et édition

### 5.1 Paradigme de navigation

**Mode NAV (hors edit)** :
- `↑↓` : cursor ligne par ligne (saute les titres de section).
- `←→` : saute à la section suivante/précédente (cursor se pose sur la 1re ligne de la section cible).
- `ENTER` : rentre en mode EDIT sur la ligne courante.
- `d` : reset la ligne courante au default, save immédiat (no confirm).
- `q` : exit Tool 8.

### 5.2 Pattern canonique d'édition

**Mode EDIT — ligne couleur `[preset] +hue`** :
- `←→` : cycle preset (14 presets, wrap).
- `↑↓` : cycle hue offset (−128 à +127, step 1 normal, step 10 accelerated).
- Pas de focus toggle. Les 4 flèches agissent simultanément sur les 2 composantes de la même entité "couleur".
- `ENTER` : save + retour NAV.
- `q` : cancel + retour NAV (restore backup).

**Mode EDIT — ligne 1 valeur numérique** (ex. `brightness 85 %`) :
- `←→` : ±10 (step coarse).
- `↑↓` : ±1 (step fine).
- `ENTER` : save + retour NAV.
- `q` : cancel + retour NAV.

**Mode EDIT — ligne 2+ valeurs numériques côte à côte** (ex. `brightness 100 %  duration 500 ms`) :
- `←→` : **focus** entre les champs (mouvement horizontal).
- `↑↓` : **ajuste** la valeur du champ sous focus (up = +, down = −, step fixe per-field).
- Pas de re-ENTER pour changer de focus, navigation fluide.
- `ENTER` : save + retour NAV.
- `q` : cancel + retour NAV (restore backup).

**Principe géométrique** : les flèches suivent le layout visuel. Horizontal (←→) pour mouvement horizontal (focus entre champs sur une ligne). Vertical (↑↓) pour ajustement d'amplitude (valeur).

Ce pattern est à intégrer dans `docs/reference/setup-tools-conventions.md` comme nouveau canonical paradigm (§4.x).

### 5.3 Panel description

Panel permanent **entre le contenu principal et la control bar**. Affiche une description de la ligne sous cursor :
- En NAV : description du param (rôle, range, lien avec d'autres params).
- En EDIT : conseils de tuning (valeurs typiques, impact visuel).

Exemple pour `BG factor` :
> BG factor — intensity ratio applied to all background banks.
> FG color × this ratio = BG intensity. Affects NORMAL, ARPEG, LOOP BG.
> Range: 10–50 %. Low = BG barely visible, high = BG close to FG.

Coût : ~3-4 lignes visibles avant la control bar. Acceptable sur console 120×40.

### 5.4 Touches tool-specific

| Touche | Fonction |
|---|---|
| `d` | Reset ligne courante au default + save immédiat. |
| `q` | Exit tool (ou cancel edit si en EDIT). |

Pas de touche `b` pour retrigger : tous les previews one-shot bouclent automatiquement avec un black time entre itérations (voir §6.4). L'utilisateur n'a jamais à déclencher manuellement.

## 6. Live preview

### 6.1 Principe

Le Tool 8 prend le contrôle complet de la strip via `previewBegin()` (comportement déjà implémenté). Setup comet pause pendant Tool 8 (reprend en sortie). Tool 8 utilise `previewSetPixel` pour écrire les LEDs arbitrairement.

Le preview est **contextuel** : selon la ligne sous cursor, il affiche ce qui a le plus de sens pour comprendre le param.

### 6.2 Mapping ligne → preview

| Contexte | Preview |
|---|---|
| Section NORMAL / ARPEG / LOOP — Base color | Strip mockup **mono-FG** (cohérent avec runtime qui n'a qu'une FG active) : `[off][BG][BG][FG][BG][BG][off][off]` (LEDs 0,6,7 éteintes ; 1,2,4,5 en BG ; 3 en FG). FG à 100 % intensity, BG dim via bgFactor global. Change de couleur live selon la ligne sous cursor. |
| Section NORMAL / ARPEG / LOOP — FG brightness | Même strip, FG intensity mise à jour live. BG suit via bgFactor. |
| Save / Clear / Fade (TRANSPORT + LOOP gestures) | Replay en boucle du pattern complet avec black entre itérations. Formule §6.4. Rendu sur LED 3 (la FG du mockup) ou mockup complet selon la nature de l'event. |
| Breathing params | Animation continue sur mockup mono-FG. La LED FG respire en permanence, les BG suivent (même breathing atténué par bgFactor). Pas de black. |
| Waiting quantise | Crossfade continu sur mockup mono-FG entre couleur du mode courant (bank FG mémorisée à l'entrée setup) et couleur édité. Les BG suivent. |
| Tick params (TRANSPORT — colors & durations) | **Mockup LOOP ticks permanent** : 8 LEDs simulant une bank LOOP FG + 1 bank LOOP BG à tempo NVS (2 bars loop). Layout : `[off][tickBG][BG][tickFG][BG][BG][off][off]`. La LED 3 est FG full, LED 1 est BG. Les 2 "tick positions" clignotent selon le scheduler aux fréquences BEAT/BAR/WRAP. La `tickFG` (LED 3) change de couleur selon le contexte (PLAY vert, REC corail, OVERDUB amber). La `tickBG` (LED 1) reste **toujours** couleur PLAY (cohérent avec runtime : BG bank ne peut être qu'en PLAYING, pas REC/OVERDUB — spec §23 LOOP bank-switch refusé pendant capture). Durée de chaque tick (BEAT/BAR/WRAP) visible live. |
| CONFIRMATIONS (bank, scale, octave, confirm OK) | Replay en boucle sur mockup mono-FG avec black proportionnel (formule §6.4). |
| GLOBAL — BG factor | **Réutilise le mockup LOOP ticks** (§6.2 ligne tick). Le ratio FG/BG est visible en temps réel via la différence d'intensité entre LED 3 (FG) et LED 1 (tickBG) + LEDs 2,4,5 (BG dim). |
| GLOBAL — Master gamma | **Réutilise le mockup LOOP ticks** aussi. Le gamma affecte la progression tonale de toutes les LEDs allumées, donc le mockup multi-intensités (FG full + BG dim + ticks) permet de juger l'effet gamma sur plusieurs niveaux lumineux simultanément. |

### 6.3 Tempo du mockup ticks

Tempo utilisé = **tempo NVS actuel** (récupéré via `NvsManager::getTempoBPM()`), pas un hardcoded 120 BPM. Le mockup devient représentatif du contexte musical de l'utilisateur.

### 6.4 Timer black replay — formule

```
black_ms = clamp(effect_ms × 0.50, 500, 3000)
```

- Ratio 50 % : effets courts ont proportionnellement plus de pause (visible).
- Min 500 ms : voir l'effet se terminer avant replay.
- Max 3000 ms : ne jamais attendre plus de 3 s (ex. effet 10 s aurait 5 s de pause sans cap — inacceptable).

Ratio et bornes non-éditables user Phase 0.1 (hardcoded). À rendre éditable plus tard si besoin confirmé.

### 6.5 Glitch preview en édition rapide

Si l'utilisateur ajuste une valeur par burst de flèches (↑↑↑↑↑), l'animation preview peut glitcher entre 2 values. **Acceptable** — note info dans le panel description de la ligne éditée : "Preview may glitch during fast edits, last value takes effect on save".

### 6.6 Architecture — helper découplé

Le preview est implémenté dans **un fichier dédié** `src/setup/ToolLedPreview.{h,cpp}`, isolé du rendu runtime :

```
src/core/LedController.{h,cpp}   — runtime inchangé SAUF :
  + public method : renderPreviewPattern(const PatternInstance& inst,
                                          uint8_t ledMask,
                                          unsigned long now)
    // wrapper public de la méthode privée renderPattern.
    // Permet à Tool 8 d'injecter des PatternInstance arbitraires.
    // Zéro duplication du runtime, cohérence garantie.

src/setup/ToolLedPreview.{h,cpp} — nouveau fichier (~200-250 LOC)
  class ToolLedPreview {
  public:
    void begin(LedController* leds, LedSettingsStore* lwk,
               ColorSlotStore* cwk, uint16_t tempoBpm);
    void end();
    void setContext(PreviewContext ctx, const void* params);
    void update(unsigned long now);

  private:
    enum PreviewContext {
      PV_NONE,
      PV_BASE_COLOR,         // mono-FG mockup : [off][BG][BG][FG][BG][BG][off][off]
      PV_EVENT_REPLAY,       // one-shot pattern with black-then-loop (§6.4 formule)
      PV_BREATHING,          // mono-FG mockup, continuous sine
      PV_WAITING,            // mono-FG mockup, continuous crossfade
      PV_TICKS_MOCKUP,       // LOOP ticks mockup : [off][tickBG][BG][tickFG][BG][BG][off][off]
      PV_BG_FACTOR,          // réutilise PV_TICKS_MOCKUP (FG/BG visible)
      PV_GAMMA_TEST,         // réutilise PV_TICKS_MOCKUP (multi-intensités)
    };
    // internal scheduler, state, etc.
  };

src/setup/ToolLedSettings.{h,cpp} — orchestrateur SEULEMENT
  - own un _preview (ToolLedPreview)
  - _preview.begin() dans run()
  - sur chaque nav change / edit change :
      _preview.setContext(appropriate, &params);
  - boucle : _preview.update(now)
  - _preview.end() en exit
```

### 6.7 Bénéfices de l'architecture

- **Séparation stricte** : runtime LedController **inchangé** sauf +1 wrapper method. Tool 8 préserve ses responsabilités d'UI.
- **Zéro duplication** : tous les previews qui affichent un pattern passent par `renderPreviewPattern`, donc par `renderPattern`. Si renderPattern évolue, les previews suivent automatiquement.
- **Logique preview-specific encapsulée** : scheduler tempo, timer black, mockups (mono-FG + LOOP ticks) — tout dans `ToolLedPreview`. Pas dans Tool 8 orchestrateur.
- **Testabilité conceptuelle** : `ToolLedPreview` peut être raisonné indépendamment. Pas possible d'unit-test sur ESP32 mais le découplage facilite les tests visuels.

## 7. Impacts moteur

### 7.1 Store bumps (groupés en 1 commit)

**`ColorSlotStore` v4 → v5** : +1 slot
```cpp
enum ColorSlotId : uint8_t {
  // ... existing 15 slots ...
  CSLOT_VERB_STOP = 15,   // NEW : Coral par défaut, Stop fade-out
  // COLOR_SLOT_COUNT = 16
};
```
Default preset 8 (Coral), hueOffset 0.

**`LedSettingsStore` v6 → v7** : +3 durations tick
```cpp
struct LedSettingsStore {
  // ... existing v6 fields ...
  uint16_t tickBeatDurationMs;   // NEW : default 30 ms  (was : tickFlashDurationMs)
  uint16_t tickBarDurationMs;    // NEW : default 60 ms
  uint16_t tickWrapDurationMs;   // NEW : default 100 ms
};
```
Le field `tickFlashDurationMs` existant devient `tickBeatDurationMs` (rename pour clarté sémantique). +4 bytes net (2 new uint16).

**Validator updates** : clamps pour les 3 durations (ex. `[5, 500]` ms chacune).

**Defaults compile-time** dans `NvsManager.cpp` : initialisation des nouveaux fields.

### 7.2 Runtime changes

**`LedController.cpp`** :
- Ajouter cache member `_colVerbStop` (résolu depuis CSLOT_VERB_STOP).
- `EVENT_RENDER_DEFAULT[EVT_STOP]` pointer vers `CSLOT_VERB_STOP` au lieu de `CSLOT_VERB_PLAY` (option γ abandonnée).
- Ajouter caches tick : `_tickBeatDurationMs`, `_tickBarDurationMs`, `_tickWrapDurationMs`.
- Consumer tick correspondant selon le contexte (ARPEG step → BEAT, LOOP bar → BAR, LOOP wrap → WRAP, etc.).
  - Phase 0.1 : seulement ARPEG step consommé (→ BEAT). Les autres wiring attendent Phase 1+ LOOP.
- Public wrapper `renderPreviewPattern()`.

**`ArpEngine`** : inchangé. `consumeTickFlash()` continue à émettre un flag neutre ; LedController mappe vers BEAT.

**`LoopEngine` (Phase 1+)** : émettra 2-3 flags (ex. `consumeBarFlash`, `consumeWrapFlash`) — mais c'est Phase 1+, hors scope de cette spec.

### 7.3 Récap bumps

| Store | Version | Reset utilisateur |
|---|---|---|
| ColorSlotStore | v4 → v5 | Oui (1 reset groupé) |
| LedSettingsStore | v6 → v7 | Oui (groupé avec ci-dessus) |
| SettingsStore | v11 (inchangé) | Non |

**1 seul cycle de reset utilisateur** pour appliquer les 2 bumps simultanément. Serial warnings au 1er boot post-flash.

### 7.4 Note — les décisions de preview UI (§6.2) sont 100 % helper-side

Les layouts preview (mono-FG, LOOP ticks), la règle "tickBG toujours PLAY", les timings black (§6.4), la réutilisation du mockup ticks pour BG factor et gamma — **aucune de ces décisions n'a d'impact sur le runtime ni sur les bumps NVS**. Tout est encapsulé dans `ToolLedPreview` et consomme les fields Store existants (+ les nouveaux fields listés §7.1) via read-only. Zéro ajout de field Store pour supporter le preview.

## 8. Fichiers impactés

| Fichier | Type de changement |
|---|---|
| `src/core/LedGrammar.{h,cpp}` | Aucun (EventId et patterns inchangés) |
| `src/core/LedController.{h,cpp}` | +renderPreviewPattern, +CSLOT_VERB_STOP cache, +3 tick durations caches, update EVENT_RENDER_DEFAULT[EVT_STOP] |
| `src/core/KeyboardData.h` | ColorSlotStore v5 enum extension, LedSettingsStore v7 fields, validators updates |
| `src/managers/NvsManager.cpp` | Defaults tables v5 + v7, descriptor table updates |
| `src/setup/ToolLedPreview.{h,cpp}` | **Nouveau fichier** (~250 LOC) |
| `src/setup/ToolLedSettings.{h,cpp}` | **Refonte complète** (~600-800 LOC estimées ; réutilise le skeleton 0.8a pour page nav mais structure data radicalement différente) |
| `docs/reference/setup-tools-conventions.md` | +§pattern canonique d'édition (multi-valeurs ←→ focus, ↑↓ adjust) |
| `docs/reference/architecture-briefing.md` | §1 MM7 si changement structure preview, §8 LEDs entry si nouveau helper |
| `docs/reference/nvs-reference.md` | ColorSlotStore v5, LedSettingsStore v7 |

**Fichiers non-touchés** :
- MidiEngine, MidiTransport, ClockManager, ScaleResolver
- ArpEngine, ArpScheduler
- BankManager, ScaleManager, PotRouter, PotFilter, ControlPadManager, BatteryMonitor
- Autres setup tools (Tool 1-7)

## 9. Defaults finaux

Résumé des defaults compile-time :

| Param | Default |
|---|---|
| NORMAL base color | preset 1 (Warm White), hue 0 |
| NORMAL FG brightness | 85 % |
| ARPEG base color | preset 3 (Ice Blue), hue 0 |
| ARPEG FG brightness | 80 % |
| LOOP base color | preset 7 (Gold), hue 0 |
| LOOP FG brightness | 80 % |
| Save slot | preset 10 (Magenta), hue 0, duration 1000 ms |
| Clear loop | preset 5 (Cyan), hue 0, duration 500 ms |
| Clear slot | preset 6 (Amber), hue +20, duration 800 ms |
| Play fade-in | preset 11 (Green), hue 0, brightness 100 %, duration 500 ms |
| Stop fade-out | preset 8 (Coral), hue 0, brightness 100 %, duration 500 ms |
| Waiting quantise | preset 11 (Green), hue 0 |
| Breathing | min 60 %, max 90 %, period 2500 ms |
| Tick common | FG 100 %, BG 25 % |
| Tick PLAY | preset 11 (Green), hue 0 |
| Tick REC | preset 8 (Coral), hue 0 |
| Tick OVERDUB | preset 6 (Amber), hue 0 |
| Tick BEAT duration | 30 ms |
| Tick BAR duration | 60 ms |
| Tick WRAP duration | 100 ms |
| Bank switch | preset 0 (Pure White), hue 0, brightness 80 %, duration 300 ms |
| Scale root | preset 6 (Amber), hue 0, brightness 100 %, duration 200 ms |
| Scale mode | preset 7 (Gold), hue 0, brightness 100 %, duration 200 ms |
| Scale chromatic | preset 8 (Coral), hue 0, brightness 100 %, duration 200 ms |
| Octave | preset 9 (Violet), hue 0, brightness 100 %, duration 300 ms |
| Confirm OK (SPARK) | preset 0 (Pure White), hue 0, on 50 ms, gap 70 ms, cycles 2 |
| BG factor | 25 % |
| Master gamma | 2.0 (gammaTenths=20) |

Les timers LOOP (clearLoopTimerMs=500, slotSaveTimerMs=1000, slotClearTimerMs=800) **restent dans SettingsStore v11** (pas de déplacement), édités **à la fois** par Tool 6 et par Tool 8 (duplication UI intentionnelle, même field Store).

## 10. Non-goals

- **Pas de mode avancé** pour accéder au choix des patterns (ex. swap BLINK_SLOW → FADE sur un event). Décidé hors scope — les patterns sont des choix de grammaire, pas user-tunables.
- **Pas d'éditeur de color preset** custom (les 14 presets hardcoded restent). Future extension si besoin confirmé.
- **Pas d'import/export de profil LED** (user settings JSON). Future extension.
- **Pas de preview multi-strip** (simuler plusieurs bank states simultanés). Une vue à la fois.

## 11. Questions ouvertes / risques

### 11.1 Description panel — taille max

Environ 3-4 lignes utiles entre le contenu et la control bar. Si certaines descriptions sont trop longues, tronquer avec ellipsis ou wrap intelligent. À tester au prototypage UI.

### 11.2 Perf preview ticks mockup

Le scheduler tempo + rendu 8 LEDs à 100 Hz + orchestration Tool 8 + VT100 redraw = charge Core 1 non-négligeable. À mesurer au prototypage. Si saturation, baisser la fréquence du scheduler (50 Hz suffit pour visuel fluide).

### 11.3 Gamma preview

Le preview gamma réutilise le **mockup LOOP ticks** (§6.2). Les multi-intensités présentes sur la strip simultanément (FG full LED 3, BG dim LEDs 2-5, tickBG LED 1 dim, ticks en plein flash pendant leurs phases) donnent une vue complète de la progression tonale affectée par le gamma. Avantages : mutualisation avec BG factor et TICK params (même scheduler), pas de test pattern ad-hoc.

### 11.4 Clear slot combo — timing du geste

Spec LED §13 précise que `slotClearTimerMs` est **purement visuel** (le geste est instant, sur rising edge). Le mockup Tool 8 doit le mentionner (`(feedback only, gesture is instant)`). C'est ce qui est fait.

### 11.5 Waiting quantise — preview sémantique

Le WAITING crossfade entre "couleur du mode" et "couleur sélectionnée". En preview Tool 8, le "mode" est celui de la bank FG mémorisée à l'entrée setup. Si user change de bank sans exit setup (impossible), le preview reste figé sur le mode initial. Pas critique, à documenter.

### 11.6 Ajout de patterns ou events futurs

Tout ajout d'event ou de pattern post-Phase 0.1 nécessitera une **mise à jour coordonnée** de la spec Tool 8 + implémentation. Ne pas ajouter d'event sans plan UI. Principe : "le moteur est neutre, l'UX décide quoi exposer".

## 12. Transition vers implémentation

Cette spec est la source de vérité pour le plan d'implémentation à venir. Le plan devra :
- Définir l'ordre des commits (Store bumps → ToolLedPreview → ToolLedSettings refonte → docs).
- Inclure les tests hardware par étape.
- Prévoir un commit groupé pour les 2 bumps NVS (1 seul reset user).

Estimation brute : 4-6 commits, ~1000-1200 LOC (incl. preview helper + refonte Tool 8).

**Fin de la spec.** Relecture attendue avant création du plan d'implémentation.
