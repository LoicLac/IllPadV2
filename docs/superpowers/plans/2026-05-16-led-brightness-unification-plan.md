# LED brightness unification — plan d'implémentation

**Date** : 2026-05-16
**Audit source** : session 2026-05-16 (read-only) — 5 incohérences + 1 stale + 2 manquants
**Décisions de design** : session 2026-05-16 (réponses Q1–Q7 du brainstorming)
**NVS bump** : `LED_SETTINGS_VERSION` 8 → 9 (zero-migration policy, reset user-facing attendu)
**Statut** : à valider avant Phase 1

---

## 0. Contexte

L'audit a remonté deux symptômes signalés par l'utilisateur :

1. **« ARPEG FG/BG ne respecte pas les valeurs du Tool 8 »** — cause racine : le slider `LINE_ARPEG_FG_PCT` ne contrôle que `fgArpPlayMax` (état playing). L'idle/breathing utilise `fgArpStopMin` planqué sous le label "BREATHING min".
2. **« BG/FG perçus moins brillants que la batterie »** — cumul de 3 facteurs : preset Ice Blue à W=40 (faible), bgFactor 25 % par défaut, gamma LUT floor=1 qui collapse les canaux <16 à la valeur 1.

Les décisions prises (réponses utilisateur) :

| Q | Décision |
|---|---|
| Q1 | Un seul `fgIntensity` global (NORMAL = ARPEG = LOOP, tous états) |
| Q2 | `breathDepth` default 50 % (dip à 50 % de FG, pic à FG) |
| Q3 | `W_WEIGHT` hardcoded à 70 (= 0.7) appliqué dans `setPixel` sur le canal W uniquement |
| Q4 | Résolu par Q1 (un seul champ partagé par design, plus d'aliasing) |
| Q5 | `bankBrightnessPct` reste séparé (intensité de l'event BANK_SWITCH, sans rapport avec fond bank) |
| Q6 | Consolider les 3 sources de defaults (LedController ctor / NvsManager / Tool fallback) — fait dans ce refactor |
| Q7 | Aligner les defaults reset (`d`) Tool 8 avec les defaults boot — fait dans ce refactor |

**Hors scope** :
- LOOP Phase 1+ runtime (la spec [`2026-04-19-loop-mode-design.md`](../specs/2026-04-19-loop-mode-design.md) référence `_fgArpStopMax` pour WAITING → impact spec uniquement, voir §6.4).
- Per-bank-type FG sliders (rejetés par Q1).
- Modification des color presets (rejet de B1 du brainstorming initial — la pondération W globale est préférée).

---

## 1. Périmètre — fichiers touchés

| Fichier | Lignes ~impact | Type |
|---|---|---|
| [`src/core/HardwareConfig.h`](../../../src/core/HardwareConfig.h) | +3 | ajout `W_WEIGHT` |
| [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h) | ~25 | struct fields + validator + version bump |
| [`src/core/LedController.h`](../../../src/core/LedController.h) | ~5 | membres |
| [`src/core/LedController.cpp`](../../../src/core/LedController.cpp) | ~50 | constructor + renderBankNormal/Arpeg + setPixel + loadLedSettings |
| [`src/managers/NvsManager.cpp`](../../../src/managers/NvsManager.cpp) | ~6 | defaults block |
| [`src/setup/ToolLedSettings.h`](../../../src/setup/ToolLedSettings.h) | ~5 | enum LineId |
| [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp) | ~80 | LINE_LABELS, LINE_DESCRIPTIONS, sectionOf, lastLineOfSection, shapeForLine, numericFieldCountForLine, readNumericField, writeNumericField, getNumericFieldBounds, resetDefaultForLine, updatePreviewContext, drawLine units, loadAll fallback |
| [`src/setup/ToolLedPreview.cpp`](../../../src/setup/ToolLedPreview.cpp) | 0 | aucun changement (Params reçoit déjà breathMin/Max calculés par Tool 8) |
| [`docs/reference/led-reference.md`](../../reference/led-reference.md) | ~20 | §4 tableau états + §7 Tool 8 + §10 bug patterns |
| [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](../specs/2026-04-19-loop-mode-design.md) | ~5 | note de mise à jour WAITING brightness ref (Phase 1+) |

**Aucun changement** à : `main.cpp`, `PotRouter.*`, `BankManager.*`, `ArpEngine.*`, `MidiEngine.*`, `Adafruit_NeoPixel` (toujours pas de `setBrightness()` appelé).

---

## 2. Modèle cible (récap)

### 2.1 Champs `LedSettingsStore` v9

| Champ supprimé | Champ ajouté/conservé | Sémantique |
|---|---|---|
| `normalFgIntensity` (u8) | — fusionné → `fgIntensity` | — |
| `fgArpStopMin` (u8) | — supprimé | — |
| `fgArpStopMax` (u8) | — supprimé | — |
| `fgArpPlayMax` (u8) | — fusionné → `fgIntensity` | — |
| — | `fgIntensity` (u8) **NEW** | Intensité unique FG bank, tous types/états. Default 80. Range [10,100] |
| — | `breathDepth` (u8) **NEW** | Profondeur du dip breathing en %. Default 50. Range [0,80] (clampé runtime par bgFactor) |
| `pulsePeriodMs` (u16) | inchangé | Période breathing. Default 1472 |
| `bgFactor` (u8) | inchangé | BG = FG × bgFactor / 100. Default 25, range [10,50] |
| `tickFlashFg/Bg` (u8/u8) | inchangé | Spike absolu pendant tick, indépendant de FG (décision user : "le slider exprime un absolu") |
| `bankBrightnessPct` (u8) | inchangé | Q5 — intensité BANK_SWITCH event, séparé |
| Tous les autres champs (timings, sparkOnMs/GapMs/Cycles, blinks, eventOverrides[], gammaTenths) | inchangés | — |

**Économie nette** : -2 octets (4 u8 supprimés, 2 u8 ajoutés). La struct reste sous les 128 octets NVS_BLOB_MAX_SIZE.

### 2.2 Rendu runtime

```
ANY BANK (NORMAL / ARPEG / LOOP)
  FG idle / no-notes        : SOLID fgIntensity
  FG stopped-loaded (ARPEG) : SINE oscille [fgIntensity×(1-breathDepth/100)] → [fgIntensity]
                              avec floor = fgIntensity×bgFactor/100 + 1 (garantit FG_min > BG)
  FG playing (ARPEG)        : SOLID fgIntensity + tick FLASH (tickFlashFg)
  BG (tous états)           : SOLID fgIntensity × bgFactor / 100
                              + tick FLASH (tickFlashBg) si playing
```

**Invariant strict** : FG ≥ BG en tout instant (même au creux du breathing).

### 2.3 W_WEIGHT

```cpp
// Dans setPixel, après gamma, avant _strip.setPixelColor :
uint8_t w_out = (uint8_t)((uint16_t)_gammaLut[w_byte] * W_WEIGHT / 100);
```

R, G, B inchangés. Battery (W=0 dans tous les presets `COL_BATTERY[]`) non concernée par construction.

**Effet sur les autres affichages W-portants (review 2026-05-16)** :
- `COL_BOOT = {0, 0, 0, 255}` — boot progress + calibration validation flash. W=255 → après W_WEIGHT=70, sortie W=178 au lieu de 255. Boot LEDs perçus ~30 % moins éclatants. **Acceptable** car boot reste largement visible.
- `COL_ERROR = {255, 0, 0, 0}` — W=0, non affecté.
- `COL_BOOT_FAIL = {255, 0, 0, 0}` — W=0, non affecté.
- `COL_SETUP = {128, 0, 255, 0}` — W=0, non affecté (comet violet).
- `COL_BATTERY[]` — W=0 sur les 8 entrées, non affecté.
- Color slots avec W=0 (Deep Blue, Cyan, Pure White Coral, Green) — non affectés. Color slots avec W élevé (Warm White W=200, Cool White W=220, Ice Blue W=40 + tous les autres) — atténuation proportionnelle.

**Effet sur le floor gamma=1** : pour `w_gamma = 1` (cas extrême après gamma), `w_final = 1 * 70 / 100 = 0`. Le floor=1 garantissait qu'un input non-nul produit toujours de la lumière sur W. **Avec W_WEIGHT < 100, cette garantie disparaît sur le canal W** (préservée sur R/G/B). Décision design assumée : le but de la pondération est précisément d'atténuer W aux bas niveaux. Si à l'usage HW on observe une perte de feedback "la bank existe" sur des couleurs purement-W à très basse intensité, baisser le seuil ou compenser via `floor(W_WEIGHT_LUT[w_gamma])`. Pas implémenté en v9.

### 2.4 Tool 8 — nouvelle ligne layout

| Section | Lignes (après refactor) | Δ |
|---|---|---|
| NORMAL | `Base color` | -1 (`FG brightness` retiré) |
| ARPEG | `Base color` | -1 (`FG brightness` retiré) |
| LOOP | `Base color`, `Save slot`/`duration`, `Clear loop`/`duration`, `Clear slot`/`duration` | -1 (`FG brightness` retiré) |
| TRANSPORT | inchangé (13 lignes) | 0 (`Breathing` devient 2 fields au lieu de 3) |
| CONFIRMATIONS | inchangé | 0 |
| GLOBAL | **`FG intensity` (NEW)**, `BG factor`, `Master gamma` | +1 |

**LINE_COUNT** : 40 − 3 + 1 = **38**.

---

## 3. Phase 1 — Struct + defaults

### Task 1.1 — `KeyboardData.h` : LedSettingsStore v9

**Fichier** : [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h)
**Lignes actuelles** : 275 (version constant), 277-318 (struct), 693-738 (validator)

#### 1.1.a — Bump version

```cpp
// AVANT (ligne 275)
#define LED_SETTINGS_VERSION       8   // v7 -> v8 : remove zombie fields ...

// APRÈS
#define LED_SETTINGS_VERSION       9   // v8 -> v9 : unify FG intensity (single fgIntensity for all bank types/states), add breathDepth, remove fgArpStopMin/Max/PlayMax + normalFgIntensity. NVS reset on update.
```

#### 1.1.b — Modifier la struct

```cpp
// AVANT (lignes 277-289)
struct LedSettingsStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  // --- Intensities (0-100, perceptual %) ---
  uint8_t  normalFgIntensity;     // default 85  — SOLID intensity FG NORMAL
  uint8_t  fgArpStopMin;          // default 30  — PULSE_SLOW min intensity FG ARPEG stopped-loaded
  uint8_t  fgArpStopMax;          // default 100 — PULSE_SLOW max intensity idem
  uint8_t  fgArpPlayMax;          // default 80  — SOLID intensity FG ARPEG playing (between FLASH ticks)
  uint8_t  tickFlashFg;           // default 100 — FLASH pattern fgPct
  uint8_t  tickFlashBg;           // default 25  — FLASH pattern bgPct
  // --- Global background factor ---
  uint8_t  bgFactor;              // default 25  — BG = FG color x bgFactor%. Range [10, 50].
  // --- Timing ---
  uint16_t pulsePeriodMs;         // default 1472 — PULSE_SLOW period FG ARPEG stopped-loaded

// APRÈS
struct LedSettingsStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  // --- Intensity (v9 : unified, 0-100 perceptual %) ---
  uint8_t  fgIntensity;           // default 80  — SOLID intensity FG for any bank type/state (NORMAL/ARPEG/LOOP, idle/playing). Range [10,100].
  uint8_t  breathDepth;           // default 50  — breathing dip depth % (FG oscillates fgIntensity*(1-depth/100) → fgIntensity). Range [0,80] (clamped runtime by bgFactor to keep FG > BG).
  uint8_t  tickFlashFg;           // default 100 — FLASH pattern fgPct (absolute, independent of fgIntensity)
  uint8_t  tickFlashBg;           // default 25  — FLASH pattern bgPct (absolute, independent of fgIntensity)
  // --- Global background factor ---
  uint8_t  bgFactor;              // default 25  — BG = FG × bgFactor%. Range [10, 50].
  // --- Timing ---
  uint16_t pulsePeriodMs;         // default 1472 — breathing sine period
```

Le reste de la struct (`tickBeatDurationMs`, `gammaTenths`, `sparkOnMs/GapMs/Cycles`, `bankBlinks` + `bankBrightnessPct`, scale*, octave, hold fades, eventOverrides[]) reste **inchangé**.

#### 1.1.c — Mettre à jour `validateLedSettingsStore`

```cpp
// AVANT (ligne 695)
if (s.fgArpStopMin > s.fgArpStopMax) s.fgArpStopMax = s.fgArpStopMin;

// APRÈS (remplacer cette ligne par)
if (s.fgIntensity < 10)   s.fgIntensity = 10;
if (s.fgIntensity > 100)  s.fgIntensity = 100;
if (s.breathDepth > 80)   s.breathDepth = 80;
// Note : breathDepth clamp final est runtime-dependent (bgFactor) — voir renderBankArpeg.
```

Garder le reste du validateur (bgFactor [10,50], pulsePeriodMs [500,4000], etc.) inchangé.

#### 1.1.d — Aucun impact sur `static_assert(sizeof(LedSettingsStore) <= 128)`

À vérifier au build : la nouvelle struct doit toujours fitter dans `NVS_BLOB_MAX_SIZE = 128`. Calcul rapide : retrait 4 u8, ajout 2 u8 → -2 octets, donc OK.

---

### Task 1.2 — `NvsManager.cpp` : defaults v9

**Fichier** : [`src/managers/NvsManager.cpp`](../../../src/managers/NvsManager.cpp)
**Lignes actuelles** : 41-78 (bloc defaults)

```cpp
// AVANT (lignes 46-54)
// Intensities (BG derived from FG via bgFactor)
_ledSettings.normalFgIntensity = 85;
_ledSettings.fgArpStopMin = 30;
_ledSettings.fgArpStopMax = 100;
_ledSettings.fgArpPlayMax = 80;
_ledSettings.tickFlashFg = 100;
_ledSettings.tickFlashBg = 25;
// Global background factor (v6 new — step 0.6 activates in BG rendering)
_ledSettings.bgFactor = 25;  // provisional, tune on hardware in 0.9

// APRÈS
// Intensities v9 : unified FG (any bank type/state) + breathing depth
_ledSettings.fgIntensity = 80;
_ledSettings.breathDepth = 50;
_ledSettings.tickFlashFg = 100;
_ledSettings.tickFlashBg = 25;
// Global background factor — BG = FG × bgFactor / 100
_ledSettings.bgFactor = 25;
```

Le reste du bloc (lignes 55-84, timings + sparks + confirmations + event overrides) reste **inchangé**.

---

### Task 1.3 — `LedController.h` : membres

**Fichier** : [`src/core/LedController.h`](../../../src/core/LedController.h)
**Lignes actuelles** : 165-167

```cpp
// AVANT
uint8_t  _normalFgIntensity;
uint8_t  _fgArpStopMin, _fgArpStopMax;
uint8_t  _fgArpPlayMax;         // FG ARPEG playing solid. BG intensities derived from FG via _bgFactor.

// APRÈS
uint8_t  _fgIntensity;          // v9 : SOLID intensity FG for any bank/state (NORMAL/ARPEG/LOOP)
uint8_t  _breathDepth;          // v9 : breathing dip depth % (renderBankArpeg stopped-loaded branch)
```

---

### Task 1.4 — `LedController.cpp` : constructor + loadLedSettings

**Fichier** : [`src/core/LedController.cpp`](../../../src/core/LedController.cpp)

#### 1.4.a — Constructor init list (lignes 18-20)

```cpp
// AVANT
_normalFgIntensity(85),
_fgArpStopMin(30), _fgArpStopMax(100),
_fgArpPlayMax(80),

// APRÈS
_fgIntensity(80),
_breathDepth(50),
```

#### 1.4.b — `loadLedSettings` (lignes 888-892)

```cpp
// AVANT
_normalFgIntensity = s.normalFgIntensity;
// Guard against inverted min/max from corrupted NVS
_fgArpStopMin = s.fgArpStopMin;
_fgArpStopMax = (s.fgArpStopMax >= s.fgArpStopMin) ? s.fgArpStopMax : s.fgArpStopMin;
_fgArpPlayMax = s.fgArpPlayMax;

// APRÈS
_fgIntensity = s.fgIntensity;
_breathDepth = s.breathDepth;
```

**Gate compile** (sortie attendue après cette task) : encore des erreurs car `renderBankNormal`/`renderBankArpeg`/fallback ligne 532 référencent les anciens membres. Phase 2 corrige.

---

### Gates Phase 1

- [ ] **Compile gate** : `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1` — DOIT échouer sur les 5 sites de `renderBankNormal/Arpeg` + fallback ligne 532. C'est attendu, Phase 2 les corrige. (Vérifier qu'aucun autre fichier ne casse, sinon = surprise à investiguer.)
- [ ] **Static read-back** : `grep -rn "normalFgIntensity\|fgArpStopMin\|fgArpStopMax\|fgArpPlayMax\|_normalFgIntensity\|_fgArpStopMin\|_fgArpStopMax\|_fgArpPlayMax" src/` → ne doit rester que les 5-6 occurrences dans `LedController.cpp:440,441,465,466,487,488,494,495,532` + 0 référence header/struct/defaults.

---

## 4. Phase 2 — Runtime rendering + W_WEIGHT

### Task 2.1 — `HardwareConfig.h` : ajout `W_WEIGHT`

**Fichier** : [`src/core/HardwareConfig.h`](../../../src/core/HardwareConfig.h)
**Lignes** : insérer après ligne 56 (après le commentaire Gamma LUT)

```cpp
// --- W channel perceptual weight (v9) ---
// SK6812 RGBW : the W channel is perceptually ~1.5-2x brighter than R/G/B
// individuals at the same drive value. Hardcoded weight applied in setPixel
// after gamma, only on the W output byte. 0-100 (100 = no weight, raw output).
// Default 70 (0.7) : tune on hardware to balance W-heavy bank colors (warm
// white) against RGB-only colors (battery LEDs, ice blue arpeg). Lower = W
// more attenuated relative to RGB.
static constexpr uint8_t W_WEIGHT = 70;
```

### Task 2.2 — `LedController::setPixel` : appliquer W_WEIGHT

**Fichier** : [`src/core/LedController.cpp`](../../../src/core/LedController.cpp)
**Lignes actuelles** : 114-126

```cpp
// AVANT
void LedController::setPixel(uint8_t i, const RGBW& c, uint8_t intensityPct) {
  uint8_t scaled = (uint8_t)((uint32_t)intensityPct * _brightnessPct * 255 / 10000);
  _strip.setPixelColor(i,
    _gammaLut[(uint16_t)c.r * scaled / 255],
    _gammaLut[(uint16_t)c.g * scaled / 255],
    _gammaLut[(uint16_t)c.b * scaled / 255],
    _gammaLut[(uint16_t)c.w * scaled / 255]
  );
}

// APRÈS
void LedController::setPixel(uint8_t i, const RGBW& c, uint8_t intensityPct) {
  uint8_t scaled = (uint8_t)((uint32_t)intensityPct * _brightnessPct * 255 / 10000);
  // W_WEIGHT (HardwareConfig.h) : balance W channel perceptual brightness
  // against RGB. Applied AFTER gamma, only on W byte. RGB pass through.
  uint8_t w_gamma = _gammaLut[(uint16_t)c.w * scaled / 255];
  uint8_t w_final = (uint8_t)((uint16_t)w_gamma * W_WEIGHT / 100);
  _strip.setPixelColor(i,
    _gammaLut[(uint16_t)c.r * scaled / 255],
    _gammaLut[(uint16_t)c.g * scaled / 255],
    _gammaLut[(uint16_t)c.b * scaled / 255],
    w_final
  );
}
```

### Task 2.3 — `renderBankNormal` : unifier sur `_fgIntensity`

**Fichier** : [`src/core/LedController.cpp`](../../../src/core/LedController.cpp)
**Lignes actuelles** : 436-443

```cpp
// AVANT
void LedController::renderBankNormal(uint8_t led, bool isFg) {
  // FG uses MODE_NORMAL. BG uses same color at reduced intensity
  // derived from FG via global _bgFactor.
  uint8_t intensity = isFg
                      ? _normalFgIntensity
                      : (uint8_t)((uint16_t)_normalFgIntensity * _bgFactor / 100);
  setPixel(led, _colors[CSLOT_MODE_NORMAL], intensity);
}

// APRÈS
void LedController::renderBankNormal(uint8_t led, bool isFg) {
  // v9 : unified _fgIntensity (NORMAL/ARPEG/LOOP all share). BG = FG × bgFactor.
  uint8_t intensity = isFg
                      ? _fgIntensity
                      : (uint8_t)((uint16_t)_fgIntensity * _bgFactor / 100);
  setPixel(led, _colors[CSLOT_MODE_NORMAL], intensity);
}
```

### Task 2.4 — `renderBankArpeg` : 3 états sur `_fgIntensity` + `_breathDepth`

**Fichier** : [`src/core/LedController.cpp`](../../../src/core/LedController.cpp)
**Lignes actuelles** : 445-498

```cpp
// AVANT (extraits clés)
if (playing) {
  uint8_t intensity = isFg
                      ? _fgArpPlayMax
                      : (uint8_t)((uint16_t)_fgArpPlayMax * _bgFactor / 100);
  setPixel(led, col, intensity);
  // ... tick flash ...
} else if (isFg && hasNotes) {
  // sine breathing
  uint16_t period = _pulsePeriodMs > 0 ? _pulsePeriodMs : 1;
  uint16_t phase = (uint16_t)((now * 65536UL / period) % 65536);
  uint8_t  idx   = phase >> 8;
  uint8_t  frac  = phase & 0xFF;
  uint16_t sine16 = (uint16_t)LED_SINE_LUT[idx] * (256 - frac)
                  + (uint16_t)LED_SINE_LUT[(idx + 1) & 0xFF] * frac;
  uint8_t intensity = _fgArpStopMin
                    + (uint8_t)((uint32_t)sine16 * (_fgArpStopMax - _fgArpStopMin) / 65280);
  setPixel(led, col, intensity);
} else {
  uint8_t intensity = isFg
                      ? _fgArpStopMin
                      : (uint8_t)((uint16_t)_fgArpStopMin * _bgFactor / 100);
  setPixel(led, col, intensity);
}

// APRÈS
if (playing) {
  uint8_t intensity = isFg
                      ? _fgIntensity
                      : (uint8_t)((uint16_t)_fgIntensity * _bgFactor / 100);
  setPixel(led, col, intensity);
  // tick flash overlay — unchanged (uses _tickFlashFg/_tickFlashBg, absolute spike)
  if (_flashStartTime[led] != 0) {
    if ((now - _flashStartTime[led]) < _tickBeatDurationMs) {
      renderFlashOverlay(led, _colors[CSLOT_VERB_PLAY], _tickFlashFg, _tickFlashBg,
                         _flashStartTime[led], _tickBeatDurationMs, isFg, now);
    } else {
      _flashStartTime[led] = 0;
    }
  }
} else if (isFg && hasNotes) {
  // FG stopped-loaded : breathing sine oscillates from breathMin to _fgIntensity.
  // breathMin = _fgIntensity × (100 - _breathDepth) / 100,
  // floor-clamped to (BG + 1) so FG never dips at or below BG.
  uint16_t period = _pulsePeriodMs > 0 ? _pulsePeriodMs : 1;
  uint16_t phase = (uint16_t)((now * 65536UL / period) % 65536);
  uint8_t  idx   = phase >> 8;
  uint8_t  frac  = phase & 0xFF;
  uint16_t sine16 = (uint16_t)LED_SINE_LUT[idx] * (256 - frac)
                  + (uint16_t)LED_SINE_LUT[(idx + 1) & 0xFF] * frac;
  uint8_t breathMin = (uint8_t)((uint16_t)_fgIntensity * (100 - _breathDepth) / 100);
  uint8_t bgIntensity = (uint8_t)((uint16_t)_fgIntensity * _bgFactor / 100);
  if (breathMin <= bgIntensity) breathMin = (uint8_t)(bgIntensity + 1);  // invariant FG > BG
  uint8_t intensity = breathMin
                    + (uint8_t)((uint32_t)sine16 * (_fgIntensity - breathMin) / 65280);
  setPixel(led, col, intensity);
} else {
  // BG (all states) OR FG idle no-notes : solid at _fgIntensity (FG) or scaled BG.
  uint8_t intensity = isFg
                      ? _fgIntensity
                      : (uint8_t)((uint16_t)_fgIntensity * _bgFactor / 100);
  setPixel(led, col, intensity);
}
```

**Note runtime safety** : si `_fgIntensity == 10` et `_bgFactor == 50`, alors `bgIntensity = 5` et `breathMin = 10 × (100-50)/100 = 5`. La clause `breathMin <= bgIntensity` force `breathMin = 6`. L'amplitude breathing devient `[6, 10]` — très faible mais cohérente avec l'invariant. Acceptable au minimum du range.

### Task 2.5 — Fallback ligne 532 (no slots)

**Fichier** : [`src/core/LedController.cpp`](../../../src/core/LedController.cpp)
**Ligne actuelle** : 532

```cpp
// AVANT
setPixel(_currentBank, _colors[CSLOT_MODE_NORMAL], _normalFgIntensity);

// APRÈS
setPixel(_currentBank, _colors[CSLOT_MODE_NORMAL], _fgIntensity);
```

### Gates Phase 2

- [ ] **Compile gate** : `pio run -e esp32-s3-devkitc-1` → exit 0, 0 nouveau warning.
- [ ] **Static read-back** : `grep -rn "normalFgIntensity\|fgArpStopMin\|fgArpStopMax\|fgArpPlayMax" src/` → 0 résultat (tous renommés/supprimés).
- [ ] **Static read-back** : `grep -rn "_fgIntensity\|_breathDepth\|W_WEIGHT" src/` → liste cohérente avec les sites modifiés.
- [ ] **HW gate Phase 2** : flash + tester :
  - Une bank NORMAL en foreground (la W warm white doit être moins éclatante qu'avant — c'est attendu).
  - Une bank ARPEG en foreground sans notes (idle).
  - Une bank ARPEG en foreground avec notes mais non playing (breathing visible, doit jamais s'éteindre).
  - Une bank ARPEG en playing.
  - BG visible (autres banks) pour chaque type.
  - Battery gauge (bouton rear) : doit rester identique (W=0 dans tous COL_BATTERY).
  - Comparer la perception ARPEG idle vs NORMAL idle vs battery : ARPEG doit être proportionnel maintenant.
- [ ] **HW gate — ajustement W_WEIGHT** : si battery encore visiblement plus brillante que NORMAL/ARPEG, baisser `W_WEIGHT` à 60 ou 50. Si NORMAL devient trop terne, remonter à 80. Itérer.

---

## 5. Phase 3 — Tool 8 UX refactor

### Task 3.1 — `ToolLedSettings.h` : enum LineId

**Fichier** : [`src/setup/ToolLedSettings.h`](../../../src/setup/ToolLedSettings.h)
**Lignes actuelles** : 55-102

```cpp
// AVANT (extraits)
enum LineId : uint8_t {
  LINE_NORMAL_BASE_COLOR = 0,
  LINE_NORMAL_FG_PCT,           // ← À SUPPRIMER

  LINE_ARPEG_BASE_COLOR,
  LINE_ARPEG_FG_PCT,            // ← À SUPPRIMER

  LINE_LOOP_BASE_COLOR,
  LINE_LOOP_FG_PCT,             // ← À SUPPRIMER
  LINE_LOOP_SAVE_COLOR,
  // ... etc

  // GLOBAL
  LINE_GLOBAL_BG_FACTOR,
  LINE_GLOBAL_GAMMA,

  LINE_COUNT,
};

// APRÈS
enum LineId : uint8_t {
  LINE_NORMAL_BASE_COLOR = 0,
  // (LINE_NORMAL_FG_PCT retiré v9 — FG unifié déplacé en GLOBAL)

  LINE_ARPEG_BASE_COLOR,
  // (LINE_ARPEG_FG_PCT retiré v9 — FG unifié déplacé en GLOBAL)

  LINE_LOOP_BASE_COLOR,
  // (LINE_LOOP_FG_PCT retiré v9 — FG unifié déplacé en GLOBAL)
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
  LINE_TRANSPORT_BREATHING,          // multi v9 : depth + period (2 fields au lieu de 3)
  LINE_TRANSPORT_TICK_COMMON,
  LINE_TRANSPORT_TICK_PLAY_COLOR,
  LINE_TRANSPORT_TICK_REC_COLOR,
  LINE_TRANSPORT_TICK_OVERDUB_COLOR,
  LINE_TRANSPORT_TICK_BEAT_DUR,
  LINE_TRANSPORT_TICK_BAR_DUR,
  LINE_TRANSPORT_TICK_WRAP_DUR,

  // CONFIRMATIONS — inchangé
  LINE_CONFIRM_BANK_COLOR,
  LINE_CONFIRM_BANK_TIMING,
  LINE_CONFIRM_SCALE_ROOT_COLOR,
  LINE_CONFIRM_SCALE_ROOT_TIMING,
  LINE_CONFIRM_SCALE_MODE_COLOR,
  LINE_CONFIRM_SCALE_MODE_TIMING,
  LINE_CONFIRM_SCALE_CHROM_COLOR,
  LINE_CONFIRM_SCALE_CHROM_TIMING,
  LINE_CONFIRM_OCTAVE_COLOR,
  LINE_CONFIRM_OCTAVE_TIMING,
  LINE_CONFIRM_OK_COLOR,
  LINE_CONFIRM_OK_SPARK,

  // GLOBAL v9 : FG intensity en premier (le plus impactant)
  LINE_GLOBAL_FG_INTENSITY,           // NEW
  LINE_GLOBAL_BG_FACTOR,
  LINE_GLOBAL_GAMMA,

  LINE_COUNT,                          // = 38 (était 40 : -3 FG_PCT, +1 FG_INTENSITY)
};
```

### Task 3.2 — `ToolLedSettings.cpp` : LINE_LABELS

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 33-79

Retirer les entrées `"FG brightness"` des sections NORMAL/ARPEG/LOOP (3 retraits), ajouter `"FG intensity"` en tête de GLOBAL.

```cpp
// APRÈS (extrait)
static const char* const LINE_LABELS[ToolLedSettings::LINE_COUNT] = {
  // NORMAL
  "Base color",
  // ARPEG
  "Base color",
  // LOOP
  "Base color",
  "Save slot",
  "  duration",
  "Clear loop (hold)",
  "  duration",
  "Clear slot (combo)",
  "  duration",
  // TRANSPORT (inchangé)
  "Play fade-in",
  "  timing",
  "Stop fade-out",
  "  timing",
  "Waiting quantise",
  "Breathing",
  "Tick common",
  "Tick PLAY",
  "Tick REC",
  "Tick OVERDUB",
  "Tick BEAT duration",
  "Tick BAR duration",
  "Tick WRAP duration",
  // CONFIRMATIONS (inchangé)
  "Bank switch",
  "  timing",
  "Scale root",
  "  timing",
  "Scale mode",
  "  timing",
  "Scale chromatic",
  "  timing",
  "Octave",
  "  timing",
  "Confirm OK (SPARK)",
  "  timing",
  // GLOBAL v9
  "FG intensity",         // NEW
  "BG factor",
  "Master gamma",
};
```

### Task 3.3 — `ToolLedSettings.cpp` : LINE_DESCRIPTIONS

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 83-123

```cpp
// APRÈS (extrait avec descriptions reformulées pour v9)
static const char* const LINE_DESCRIPTIONS[ToolLedSettings::LINE_COUNT] = {
  "NORMAL base color - identifies NORMAL banks. FG/BG intensity set in GLOBAL section.",
  "ARPEG base color - identifies ARPEG banks. Breathing when stopped-loaded.",
  "LOOP base color - identifies LOOP banks. Consumed Phase 1+ (runtime dormant).",
  "Save slot color - shown during the long-press ramp on slot pad (LOOP Phase 1+).",
  "Hold duration to trigger slot save (500-2000 ms). Shared with Tool 6.",
  "Clear loop color - shown during the long-press ramp on clear pad (LOOP Phase 1+).",
  "Hold duration to clear a LOOP bank (200-1500 ms). Shared with Tool 6.",
  "Slot delete color - visual feedback for the instant delete combo (not a hold).",
  "Visual duration of the slot delete feedback (400-1500 ms). Gesture is instant.",
  "Play fade-in color - flashes on Hold on or double-tap Play.",
  "Left/right focus: brightness (0-100) or duration (0-1000 ms). Up/down adjusts.",
  "Stop fade-out color - flashes on Hold off or double-tap Stop.",
  "Left/right focus: brightness (0-100) or duration (0-1000 ms). Up/down adjusts.",
  "Waiting quantise color - crossfades with mode color while waiting for beat/bar.",
  "Breathing depth (0-80%) and period. FG dips from FG intensity down by depth%.",
  "Tick FG% and BG% - shared flash intensity across PLAY/REC/OVERDUB ticks.",
  "Tick PLAY color - ARPEG step flash and LOOP playing wrap tick.",
  "Tick REC color - LOOP recording bar and wrap ticks (Phase 1+).",
  "Tick OVERDUB color - LOOP overdubbing wrap tick (Phase 1+).",
  "Tick BEAT duration (5-500 ms). Consumed now for ARPEG step flash.",
  "Tick BAR duration (5-500 ms). Consumed Phase 1+ for LOOP bar flash.",
  "Tick WRAP duration (5-500 ms). Consumed Phase 1+ for LOOP wrap flash.",
  "Bank switch confirmation color - blinks on destination bank pad.",
  "Left/right focus: brightness (0-100) or duration (100-500 ms). Up/down adjusts.",
  "Scale root change color - blinks on changed pads (fires group when linked).",
  "Left/right focus: brightness or duration (100-500 ms). Up/down adjusts.",
  "Scale mode change color - blinks on changed pads.",
  "Left/right focus: brightness or duration (100-500 ms). Up/down adjusts.",
  "Scale chromatic toggle color - blinks on changed pads.",
  "Left/right focus: brightness or duration (100-500 ms). Up/down adjusts.",
  "Octave change color - blinks on the octave pad (ARPEG only).",
  "Left/right focus: brightness or duration (100-500 ms). Up/down adjusts.",
  "Confirm OK color - universal SPARK suffix (e.g. after a Tool save).",
  "Left/right focus: on (20-200 ms), gap (20-300 ms), cycles (1-4). Up/down adjusts.",
  "FG intensity (10-100%) for all bank FG (NORMAL/ARPEG/LOOP, any state). BG = FG x BG factor.",
  "BG factor (10-50%). All BG banks render as FG color x this ratio.",
  "Master gamma (1.0-3.0). Affects perceptual LED intensity curve. Hot-reloaded.",
};
```

**Note** : la description de `LINE_NORMAL_BASE_COLOR` (ligne 84 actuelle) mentionnait "FG shown at FG brightness, BG via BG factor" — déplacé vers la description globale GLOBAL_FG_INTENSITY.

### Task 3.4 — `sectionOf` + `lastLineOfSection`

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 302-308, 323-332

```cpp
// AVANT (sectionOf)
ToolLedSettings::Section ToolLedSettings::sectionOf(LineId line) const {
  if (line <= LINE_NORMAL_FG_PCT)          return SEC_NORMAL;
  if (line <= LINE_ARPEG_FG_PCT)           return SEC_ARPEG;
  if (line <= LINE_LOOP_SLOT_DURATION)     return SEC_LOOP;
  if (line <= LINE_TRANSPORT_TICK_WRAP_DUR) return SEC_TRANSPORT;
  if (line <= LINE_CONFIRM_OK_SPARK)       return SEC_CONFIRMATIONS;
  return SEC_GLOBAL;
}

// APRÈS
ToolLedSettings::Section ToolLedSettings::sectionOf(LineId line) const {
  if (line <= LINE_NORMAL_BASE_COLOR)       return SEC_NORMAL;
  if (line <= LINE_ARPEG_BASE_COLOR)        return SEC_ARPEG;
  if (line <= LINE_LOOP_SLOT_DURATION)      return SEC_LOOP;
  if (line <= LINE_TRANSPORT_TICK_WRAP_DUR) return SEC_TRANSPORT;
  if (line <= LINE_CONFIRM_OK_SPARK)        return SEC_CONFIRMATIONS;
  return SEC_GLOBAL;
}
```

```cpp
// AVANT (lastLineOfSection — lignes 323-332)
case SEC_NORMAL:        return LINE_NORMAL_FG_PCT;
case SEC_ARPEG:         return LINE_ARPEG_FG_PCT;
...
default:                return LINE_NORMAL_FG_PCT;

// APRÈS
case SEC_NORMAL:        return LINE_NORMAL_BASE_COLOR;
case SEC_ARPEG:         return LINE_ARPEG_BASE_COLOR;
case SEC_LOOP:          return LINE_LOOP_SLOT_DURATION;  // inchangé
case SEC_TRANSPORT:     return LINE_TRANSPORT_TICK_WRAP_DUR;  // inchangé
case SEC_CONFIRMATIONS: return LINE_CONFIRM_OK_SPARK;  // inchangé
case SEC_GLOBAL:        return LINE_GLOBAL_GAMMA;  // inchangé
default:                return LINE_NORMAL_BASE_COLOR;
```

`firstLineOfSection` reste **inchangé** (premier élément de chaque section conservé).

### Task 3.5 — `shapeForLine`

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 372-410

Retirer les cases `LINE_NORMAL_FG_PCT`, `LINE_ARPEG_FG_PCT`, `LINE_LOOP_FG_PCT` (qui tombaient sur `default → SHAPE_SINGLE_NUM`). `LINE_GLOBAL_FG_INTENSITY` tombe naturellement sur `default → SHAPE_SINGLE_NUM` aussi, aucun ajout requis.

**Aucun changement nécessaire** — le `default: return SHAPE_SINGLE_NUM` couvre la nouvelle ligne. À vérifier post-edit que les cases supprimées sont bien gone.

### Task 3.6 — `numericFieldCountForLine`

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 436-456

```cpp
// AVANT (extrait)
case LINE_TRANSPORT_BREATHING:
case LINE_CONFIRM_OK_SPARK:
  return 3;

// APRÈS
case LINE_CONFIRM_OK_SPARK:
  return 3;
case LINE_TRANSPORT_BREATHING:
  return 2;  // v9 : depth + period (était 3 : min/max/period)
```

### Task 3.7 — `readNumericField`

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 476-513

```cpp
// AVANT (cases concernés)
case LINE_NORMAL_FG_PCT:              return _lwk.normalFgIntensity;
case LINE_ARPEG_FG_PCT:               return _lwk.fgArpPlayMax;
case LINE_LOOP_FG_PCT:                return _lwk.fgArpPlayMax;
...
case LINE_TRANSPORT_BREATHING:        return (f == 0) ? (int32_t)_lwk.fgArpStopMin
                                                      : (f == 1) ? (int32_t)_lwk.fgArpStopMax
                                                                 : (int32_t)_lwk.pulsePeriodMs;

// APRÈS (cases concernés ; les 3 LINE_*_FG_PCT supprimés)
case LINE_TRANSPORT_BREATHING:        return (f == 0) ? (int32_t)_lwk.breathDepth
                                                      : (int32_t)_lwk.pulsePeriodMs;
...
case LINE_GLOBAL_FG_INTENSITY:        return _lwk.fgIntensity;   // NEW
case LINE_GLOBAL_BG_FACTOR:           return _lwk.bgFactor;
case LINE_GLOBAL_GAMMA:               return _lwk.gammaTenths;
```

### Task 3.8 — `writeNumericField`

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 515-572

```cpp
// AVANT (cases concernés)
case LINE_NORMAL_FG_PCT:              _lwk.normalFgIntensity = (uint8_t)v; break;
case LINE_ARPEG_FG_PCT:               _lwk.fgArpPlayMax = (uint8_t)v; break;
case LINE_LOOP_FG_PCT:                _lwk.fgArpPlayMax = (uint8_t)v; break;
...
case LINE_TRANSPORT_BREATHING:
  if (f == 0) _lwk.fgArpStopMin = (uint8_t)v;
  else if (f == 1) _lwk.fgArpStopMax = (uint8_t)v;
  else        _lwk.pulsePeriodMs = (uint16_t)v;
  break;

// APRÈS (cases concernés ; les 3 LINE_*_FG_PCT supprimés)
case LINE_TRANSPORT_BREATHING:
  if (f == 0) _lwk.breathDepth = (uint8_t)v;
  else        _lwk.pulsePeriodMs = (uint16_t)v;
  break;
...
case LINE_GLOBAL_FG_INTENSITY:        _lwk.fgIntensity = (uint8_t)v; break;   // NEW
case LINE_GLOBAL_BG_FACTOR:           _lwk.bgFactor = (uint8_t)v; break;
case LINE_GLOBAL_GAMMA:               _lwk.gammaTenths = (uint8_t)v; break;
```

### Task 3.9 — `getNumericFieldBounds`

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 574-616

```cpp
// AVANT (cases concernés)
case LINE_NORMAL_FG_PCT:
case LINE_ARPEG_FG_PCT:
case LINE_LOOP_FG_PCT:                mn = 10; mx = 100; break;
...
case LINE_TRANSPORT_BREATHING:
  if (f == 2) { mn = 500; mx = 4000; coarse = 100; fine = 50; }
  else        { mn = 0; mx = 100; coarse = 10; fine = 1; }
  break;

// APRÈS (cases concernés ; les 3 LINE_*_FG_PCT supprimés)
case LINE_TRANSPORT_BREATHING:
  if (f == 0) { mn = 0; mx = 80; coarse = 10; fine = 1; }     // depth %
  else        { mn = 500; mx = 4000; coarse = 100; fine = 50; }  // period ms
  break;
...
case LINE_GLOBAL_FG_INTENSITY:        mn = 10; mx = 100; coarse = 10; fine = 1; break;   // NEW
case LINE_GLOBAL_BG_FACTOR:           mn = 10; mx = 50; coarse = 5; fine = 1; break;
case LINE_GLOBAL_GAMMA:               mn = 10; mx = 30; coarse = 2; fine = 1; break;
```

### Task 3.10 — `resetDefaultForLine` (Q7 — defaults alignés au boot)

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 733-811

Aligner sur les defaults NvsManager (Phase 1 Task 1.2). Tableau d'alignement à vérifier :

| Champ | Reset Tool 8 actuel | Boot default NvsManager | Décision Q7 |
|---|---|---|---|
| FG intensity (v9) | — (nouveau) | 80 | reset = 80 |
| breathDepth | — (nouveau) | 50 | reset = 50 |
| pulsePeriodMs | 2500 (l.773) | 1472 | reset = **1472** (alignement Q7) |
| bankDurationMs | 150 (l.784) | 150 | OK |
| scaleRoot/Mode/ChromDurationMs | 130 (l.788,792,796) | 130 | OK |
| octaveDurationMs | 130 (l.800) | 130 | OK |
| sparkOnMs/GapMs/Cycles | 20/40/4 (l.803-805) | 20/40/4 | OK |
| bankBrightnessPct | 80 (l.783) | 80 | OK |
| holdOnFadeMs/holdOffFadeMs | 500 (l.764,768) | 500 | OK |
| tickFlashFg/Bg | 100/25 (l.776-777) | 100/25 | OK |
| tickBeat/Bar/WrapDurationMs | 30/60/100 (l.779-781) | 30/60/100 | OK |
| bgFactor | 25 (l.807) | 25 | OK |
| gammaTenths | 20 (l.808) | 20 | OK |

**Seul alignement requis** : `pulsePeriodMs` 2500 → 1472. Le reste matche déjà.

```cpp
// AVANT (lignes 756-758, 770-773)
case LINE_NORMAL_FG_PCT:              _lwk.normalFgIntensity = 85; break;
case LINE_ARPEG_FG_PCT:               _lwk.fgArpPlayMax = 80; break;
case LINE_LOOP_FG_PCT:                _lwk.fgArpPlayMax = 80; break;
...
case LINE_TRANSPORT_BREATHING:
  _lwk.fgArpStopMin = 60;
  _lwk.fgArpStopMax = 90;
  _lwk.pulsePeriodMs = 2500;
  break;

// APRÈS — REMINDER post-review : supprimer du switch les 3 cases :
//   case LINE_NORMAL_FG_PCT: _lwk.normalFgIntensity = 85; break;
//   case LINE_ARPEG_FG_PCT:  _lwk.fgArpPlayMax = 80; break;
//   case LINE_LOOP_FG_PCT:   _lwk.fgArpPlayMax = 80; break;
// Les enum values n'existent plus après Task 3.1, donc le compilateur
// les flaggerait, mais supprimer explicitement évite le bruit.
//
// breathing reset aligné Q7 (was 60/90/2500)
case LINE_TRANSPORT_BREATHING:
  _lwk.breathDepth   = 50;
  _lwk.pulsePeriodMs = 1472;
  break;
...
case LINE_GLOBAL_FG_INTENSITY:        _lwk.fgIntensity = 80; break;   // NEW
case LINE_GLOBAL_BG_FACTOR:           _lwk.bgFactor = 25; break;
case LINE_GLOBAL_GAMMA:               _lwk.gammaTenths = 20; break;
```

### Task 3.11 — `updatePreviewContext` (BREATHING preview + base color preview)

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 841-1103 (fonction entière commence à 841, switch principal de 851 à 1100)

#### 3.11.a — Cases LINE_NORMAL/ARPEG/LOOP_BASE_COLOR (lignes 853-870)

```cpp
// AVANT
case LINE_NORMAL_BASE_COLOR:
case LINE_NORMAL_FG_PCT:
  ctx = ToolLedPreview::PV_BASE_COLOR;
  p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_NORMAL]);
  p.fgPct   = _lwk.normalFgIntensity;
  break;
case LINE_ARPEG_BASE_COLOR:
case LINE_ARPEG_FG_PCT:
  ctx = ToolLedPreview::PV_BASE_COLOR;
  p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_ARPEG]);
  p.fgPct   = _lwk.fgArpPlayMax;
  break;
case LINE_LOOP_BASE_COLOR:
case LINE_LOOP_FG_PCT:
  ctx = ToolLedPreview::PV_BASE_COLOR;
  p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_LOOP]);
  p.fgPct   = _lwk.fgArpPlayMax;
  break;

// APRÈS (les LINE_*_FG_PCT n'existent plus, lignes plus courtes)
case LINE_NORMAL_BASE_COLOR:
  ctx = ToolLedPreview::PV_BASE_COLOR;
  p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_NORMAL]);
  p.fgPct   = _lwk.fgIntensity;
  break;
case LINE_ARPEG_BASE_COLOR:
  ctx = ToolLedPreview::PV_BASE_COLOR;
  p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_ARPEG]);
  p.fgPct   = _lwk.fgIntensity;
  break;
case LINE_LOOP_BASE_COLOR:
  ctx = ToolLedPreview::PV_BASE_COLOR;
  p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_LOOP]);
  p.fgPct   = _lwk.fgIntensity;
  break;
```

#### 3.11.b — Case LINE_TRANSPORT_BREATHING (lignes 951-957)

```cpp
// AVANT
case LINE_TRANSPORT_BREATHING:
  ctx = ToolLedPreview::PV_BREATHING;
  p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_ARPEG]);
  p.breathMinPct   = _lwk.fgArpStopMin;
  p.breathMaxPct   = _lwk.fgArpStopMax;
  p.breathPeriodMs = _lwk.pulsePeriodMs;
  break;

// APRÈS — calcule min/max depuis fgIntensity + breathDepth, conserve API ToolLedPreview
case LINE_TRANSPORT_BREATHING:
  ctx = ToolLedPreview::PV_BREATHING;
  p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_ARPEG]);
  // v9 : derive min/max from unified fgIntensity + breathDepth
  // Min clamped against BG so preview matches runtime invariant FG > BG.
  {
    uint8_t bgIntensity = (uint8_t)((uint16_t)_lwk.fgIntensity * _lwk.bgFactor / 100);
    uint8_t breathMin   = (uint8_t)((uint16_t)_lwk.fgIntensity * (100 - _lwk.breathDepth) / 100);
    if (breathMin <= bgIntensity) breathMin = (uint8_t)(bgIntensity + 1);
    p.breathMinPct   = breathMin;
    p.breathMaxPct   = _lwk.fgIntensity;
  }
  p.breathPeriodMs = _lwk.pulsePeriodMs;
  break;
```

#### 3.11.c — Cases LINE_GLOBAL_FG_INTENSITY / LINE_GLOBAL_BG_FACTOR / LINE_GLOBAL_GAMMA (lignes 1080-1096)

Le case existant `case LINE_GLOBAL_BG_FACTOR: case LINE_GLOBAL_GAMMA:` reste actif. **Ajouter** :

```cpp
// APRÈS — nouveau case en tête du bloc GLOBAL preview
case LINE_GLOBAL_FG_INTENSITY:
  // Preview NEW : mono-FG mockup (mêmes LEDs que PV_BASE_COLOR) avec couleur
  // mode-NORMAL pour donner un repère "ce que ça donne sur une bank standard".
  ctx = ToolLedPreview::PV_BASE_COLOR;
  p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_NORMAL]);
  p.fgPct   = _lwk.fgIntensity;
  break;
```

Et le bloc `LINE_GLOBAL_BG_FACTOR / LINE_GLOBAL_GAMMA` reste tel quel.

### Task 3.12 — `loadAll` fallback defaults (Q6)

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 175-201

Aligner sur NvsManager Phase 1 Task 1.2.

```cpp
// AVANT (lignes 179-189 — extrait)
_lwk.normalFgIntensity = 85;
_lwk.fgArpStopMin = 30; _lwk.fgArpStopMax = 100;
_lwk.fgArpPlayMax = 80;
_lwk.tickFlashFg = 100; _lwk.tickFlashBg = 25;
_lwk.bgFactor = 25;
_lwk.pulsePeriodMs = 1472;

// APRÈS — match NvsManager v9
_lwk.fgIntensity = 80;
_lwk.breathDepth = 50;
_lwk.tickFlashFg = 100; _lwk.tickFlashBg = 25;
_lwk.bgFactor = 25;
_lwk.pulsePeriodMs = 1472;
```

Le reste du fallback (lignes 190-200) reste **inchangé**.

**Note Q6** : pour éviter une 4e copie des defaults, on pourrait extraire dans un header `LedSettingsDefaults.h` consommé par les 3 sites (LedController ctor, NvsManager, ToolLedSettings fallback). Décision pragmatique : **ne pas le faire dans ce refactor** — 3 sites courts, alignement manuel vérifié au cas par cas. Si un quatrième site apparaît plus tard, ce sera le moment d'extraire. Documenté ici pour mémoire.

### Task 3.13 — `drawLine` units (LINE_GLOBAL_FG_INTENSITY)

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 1143-1147

```cpp
// AVANT
case LINE_NORMAL_FG_PCT:
case LINE_ARPEG_FG_PCT:
case LINE_LOOP_FG_PCT:
case LINE_GLOBAL_BG_FACTOR:          unit = "%"; break;

// APRÈS
case LINE_GLOBAL_FG_INTENSITY:
case LINE_GLOBAL_BG_FACTOR:          unit = "%"; break;
```

### Task 3.14 — Multi-num labels pour BREATHING

**Fichier** : [`src/setup/ToolLedSettings.cpp`](../../../src/setup/ToolLedSettings.cpp)
**Lignes actuelles** : 1186-1187

```cpp
// AVANT
case LINE_TRANSPORT_BREATHING:
  n0 = "min"; u0 = "%"; n1 = "max"; u1 = "%"; n2 = "period"; u2 = "ms"; break;

// APRÈS
case LINE_TRANSPORT_BREATHING:
  n0 = "depth"; u0 = "%"; n1 = "period"; u1 = "ms"; break;
```

`n2`/`u2` ne sont plus utilisés pour BREATHING (count=2). Le `fmtField` au-dessus n'appelle `f2` que si `count >= 3` (ligne 1204).

### Gates Phase 3

- [ ] **Compile gate** : `pio run -e esp32-s3-devkitc-1` → exit 0, 0 nouveau warning.
- [ ] **Static read-back** :
  - `grep -rn "LINE_NORMAL_FG_PCT\|LINE_ARPEG_FG_PCT\|LINE_LOOP_FG_PCT" src/` → 0 résultat.
  - `grep -rn "LINE_GLOBAL_FG_INTENSITY" src/` → présent uniquement dans `ToolLedSettings.h/cpp`.
  - `grep -rn "fgIntensity\|breathDepth" src/` → cohérent (KeyboardData.h, LedController.h/cpp, NvsManager.cpp, ToolLedSettings.cpp).
- [ ] **HW gate Phase 3** : entrer en setup mode, tester :
  - Navigation sections : NORMAL et ARPEG ne contiennent plus que "Base color".
  - Section GLOBAL : ligne "FG intensity" en tête, ajuste l'intensité de toutes les banks.
  - Édition BREATHING : 2 fields (depth, period). Preview montre le dip cohérent.
  - Reset (`d`) sur chaque ligne : aucune valeur surprenante.
  - Sauvegarde + re-entrée setup : valeurs persistées.

---

## 6. Phase 4 — Doc keep-in-sync

### Task 4.1 — `docs/reference/led-reference.md` §4 tableau états

**Fichier** : [`docs/reference/led-reference.md`](../../reference/led-reference.md)
**Lignes actuelles** : 142-157

```markdown
<!-- AVANT (lignes 146-153) -->
| State | Color | Pattern | Intensity | Rate |
|---|---|---|---|---|
| Current NORMAL | White (W channel) | Solid | `normalFgIntensity` (default 85 %) | — |
| Current ARPEG idle (pile empty) | Blue | Solid dim | `fgArpStopMin` | — |
| Current ARPEG stopped (notes loaded) | Blue | Sine pulse | `fgArpStopMin ↔ fgArpStopMax` | ~1.5 s period |
| Current ARPEG playing | Blue | Solid + white tick flash | `fgArpPlayMax`, spike `tickFlashFg` on step | 30 ms flash |
| BG NORMAL | White dim | Solid | `normalFgIntensity × bgFactor%` (derived) | — |
| BG ARPEG (all states) | Blue dim | Solid (+ tick flash if playing) | `fgArpStopMin × bgFactor%` or `fgArpPlayMax × bgFactor%` | 30 ms flash if playing |

<!-- APRÈS -->
| State | Color | Pattern | Intensity | Rate |
|---|---|---|---|---|
| Current NORMAL | White (W channel) | Solid | `fgIntensity` (default 80 %) | — |
| Current ARPEG idle (pile empty) | Blue | Solid | `fgIntensity` | — |
| Current ARPEG stopped (notes loaded) | Blue | Sine pulse | `fgIntensity × (1 - breathDepth%)` ↔ `fgIntensity` | `pulsePeriodMs` |
| Current ARPEG playing | Blue | Solid + white tick flash | `fgIntensity`, spike `tickFlashFg` on step | 30 ms flash |
| BG (all bank types, all states) | mode color dim | Solid (+ tick flash if playing) | `fgIntensity × bgFactor%` (derived) | 30 ms flash if playing |

v9 (2026-05-16) : single `fgIntensity` field for all bank FG (NORMAL/ARPEG/LOOP) in all states. Differentiation between states comes from animation (breathing for ARPEG stopped-loaded, tick flash for ARPEG playing), not from base intensity. Breathing min is clamped at runtime to stay > BG (invariant FG > BG strict). W channel is perceptually weighted in setPixel via `W_WEIGHT` constant (HardwareConfig.h, default 70).
```

### Task 4.2 — `docs/reference/led-reference.md` §2 setPixel description

**Fichier** : [`docs/reference/led-reference.md`](../../reference/led-reference.md)
**Lignes actuelles** : 37-49

Ajouter mention de W_WEIGHT dans la description du pipeline `setPixel` :

```markdown
<!-- À AJOUTER après le bullet 2 (gamma) -->
3. `W_WEIGHT` (HardwareConfig.h, default 70 = 0.7) applied to the W output
   byte only — perceptual balance against R/G/B. Hardcoded, not user-tunable.
   Battery LEDs (W=0 in all `COL_BATTERY[]` entries) are unaffected.
```

### Task 4.3 — `docs/reference/led-reference.md` §7 Tool 8

**Fichier** : [`docs/reference/led-reference.md`](../../reference/led-reference.md)
**Lignes actuelles** : 222-261

Mettre à jour les listes de sections :

```markdown
<!-- AVANT -->
1. **NORMAL** — base color + foreground/background intensity.
2. **ARPEG** — base color + FG/BG intensities + breathing (stopped-loaded pulse).
...
6. **GLOBAL** — bgFactor + gamma.

<!-- APRÈS -->
1. **NORMAL** — base color (FG/BG intensity in GLOBAL).
2. **ARPEG** — base color (FG/BG intensity in GLOBAL ; breathing in TRANSPORT).
3. **LOOP** — base color + gesture timers (FG/BG intensity in GLOBAL).
4. **TRANSPORT** — play/stop/waiting/breathing (depth + period) + tick common FG/BG + tick verb colors + tick durations.
5. **CONFIRMATIONS** — bank / scale / octave durations + blink counts + SPARK params.
6. **GLOBAL** — **FG intensity** (v9) + bgFactor + gamma.
```

Et la section §7.2 "Configuration mechanics" : mention version v9 + retrait des 4 fields.

### Task 4.4 — `docs/reference/led-reference.md` §10 bug patterns

Ajouter une entrée :

```markdown
| LEDs BG too dim or color collapses to white-floor | `bgFactor` × `fgIntensity` produces sub-16 values that the gamma LUT clamps to floor=1, killing channel ratio. | Raise `fgIntensity` (GLOBAL) or `bgFactor` (GLOBAL). Last resort : reduce gamma LUT floor in `rebuildGammaLut`. |
```

### Task 4.5 — `docs/superpowers/specs/2026-04-19-loop-mode-design.md` — note WAITING

**Fichier** : [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](../specs/2026-04-19-loop-mode-design.md)
**Lignes concernées** : 435, 587-588, 618-619, 630

Ajouter en tête de fichier (après le frontmatter / §0) :

```markdown
> **MAJ 2026-05-16 (LED v9 brightness unification)** : la décision Q4 de cette
> spec (rename `fgArpPlayMax` → `fgPlayMax`) est **superseded**. Le field a été
> fusionné dans `fgIntensity` (cf [`2026-05-16-led-brightness-unification-plan.md`](../plans/2026-05-16-led-brightness-unification-plan.md)).
> Implication pour Phase 1 step LED WAITING : la référence de brightness
> `_fgArpStopMax` n'existe plus — utiliser `_fgIntensity` à la place (la
> brightness max du breathing est désormais égale à `_fgIntensity`).
> Tableau §28 Q3 / Q4 obsolète pour les noms de fields ; sémantique préservée.
```

### Task 4.6 — `.claude/CLAUDE.md` projet (invariants)

**Fichier** : [`.claude/CLAUDE.md`](../../../.claude/CLAUDE.md)

Aucun invariant projet ne change. Pas de modification requise.

### Gates Phase 4

- [ ] **Lecture croisée** : `grep -rn "fgArpStopMin\|fgArpStopMax\|fgArpPlayMax\|normalFgIntensity" docs/` → ne reste que les références historiques dans `docs/superpowers/plans/2026-04-19-phase0-...`, `2026-04-20-tool8-...`, `2026-04-21-loop-...`, `2026-04-26-arpeg-gen-...`, `2026-05-15-illpad-...`, et `docs/superpowers/specs/2026-04-19-...` + `docs/superpowers/reports/...` — **ne pas toucher ces archives historiques**, elles documentent l'état au moment où elles ont été écrites.

---

## 7. Validation HW finale (gate 4 commit)

Checklist avant commit de chaque phase :

| Test | Critère pass |
|---|---|
| Bank NORMAL FG visible | W moins éclatant qu'avant (effet W_WEIGHT), mais lisible à distance |
| Bank NORMAL BG (autres banks) | Visible et proportionnel, jamais noir |
| Bank ARPEG idle FG | Ice blue solide à `fgIntensity`, plus dim que NORMAL si même couleur, distinct |
| Bank ARPEG idle BG | Visible, dim mais discernable |
| Bank ARPEG avec notes, stopped | Breathing visible, jamais s'éteint, dip < FG mais > BG |
| Bank ARPEG playing | Solid + tick flash blanc à chaque step, FG ≠ BG |
| Bank ARPEG playing BG | Solid + tick flash discret (tickFlashBg) |
| Battery gauge (rear button) | Identique à avant (W=0 dans les presets, W_WEIGHT no-op) |
| Tool 8 entrée setup | Toutes les nouvelles lignes éditables, previews cohérentes |
| Tool 8 reset `d` | Valeurs cohérentes avec boot defaults |
| Master pot brightness (rear) | Effet linéaire sur toutes les LEDs (sauf battery qui les bypass déjà à 100 % intensity dans `renderBattery`) |
| Bank switch event | BANK_SWITCH blinks toujours à `bankBrightnessPct = 80 %` (Q5 préservé) |
| Reboot après update | NVS v8 rejeté, defaults v9 chargés, message `Serial.printf` d'avertissement attendu |

Si un critère échoue : NE PAS commit. Identifier la cause, fixer, re-tester.

---

## 8. Stratégie de commit

5-gate per phase, commit groupé en fin de phase (pas par task individuelle).

### Commit 1 — Phase 1+2+3 ensemble (correction review 2026-05-16)

**Correction post-review** : la stratégie initiale "Phase 1+2 groupés / Phase 3 séparée" était fausse. Phase 1 supprime les fields struct, Phase 2 fixe les call-sites dans `LedController.cpp`, **mais Tool 8 (`ToolLedSettings.cpp`) référence aussi les fields supprimés** (~14 sites dans readNumericField, writeNumericField, resetDefaultForLine, updatePreviewContext, loadAll fallback). Sans Phase 3, le build casse aussi côté Tool 8.

Conclusion : Phase 1+2+3 sont indissociables pour le compile gate → **1 seul commit atomique** couvrant struct + runtime + Tool 8 + W_WEIGHT. Phase 4 (docs) reste un commit séparé.

Pourquoi grouper : compile-green strict, repo jamais en état rouge entre commits.

**Message** :
```
feat(led): unify fg intensity into single fgIntensity, add W_WEIGHT (NVS v8→v9)

Audit 2026-05-16 found two perception bugs:
- ARPEG FG/BG ignored Tool 8 values when not actively playing (slider only
  controlled fgArpPlayMax, idle/breathing used fgArpStopMin under "BREATHING").
- W-dominant colors (warm white) perceptually overshadowed RGB-only displays
  (battery), and low-intensity multi-channel colors collapsed to gamma floor.

Solution per Q1-Q7 design session:
- LedSettingsStore v9: merge normalFgIntensity + fgArpStopMin/Max/PlayMax
  into single fgIntensity (default 80, range [10,100]). Add breathDepth
  (default 50, range [0,80] clamped runtime by bgFactor invariant). NVS bump,
  user reset expected (zero-migration policy).
- renderBankNormal/Arpeg: all bank types (NORMAL/ARPEG/LOOP) and all states
  share fgIntensity for FG base. ARPEG stopped-loaded breathing dips from
  fgIntensity × (1 - breathDepth/100) to fgIntensity, floor-clamped > BG.
  Tick flash unchanged (absolute spike).
- W_WEIGHT constant (HardwareConfig.h, default 70): perceptual balance for
  W channel against RGB, applied in setPixel after gamma, only on W byte.
  Battery LEDs (W=0) unaffected.

Refs: docs/superpowers/plans/2026-05-16-led-brightness-unification-plan.md
```

### Commit 2 — Phase 4 docs

```
docs(led): sync LED reference + LOOP spec note with v9 brightness model
```

---

## 9. Risques + mitigations

| Risque | Impact | Mitigation |
|---|---|---|
| W_WEIGHT=70 trop fort (NORMAL devient terne sur HW) | UX dégradée sur banks NORMAL | HW gate Phase 2 : ajuster à 50-80 selon perception. Constante, easy to re-flash. |
| W_WEIGHT=70 pas assez fort (perception inchangée) | Symptôme 2 non résolu | Idem, baisser à 50 ou 40. |
| `breathDepth=50` produit un dip trop visible | UX breathing désagréable | Slider user-tunable [0,80], default ajustable post-HW. |
| Invariant breathMin > BG difficile à respecter aux extrêmes | Animation breathing visuellement "écrasée" si fgIntensity=10 et bgFactor=50 | Acceptable au minimum (cas extrême), comportement documenté ligne `breathMin = bgIntensity + 1`. |
| Reset NVS perçu comme régression par user | Frustration ré-saisie Tool 8 | Politique projet `Zero Migration` documentée dans `.claude/CLAUDE.md`. User notifié au boot via `Serial.printf` existant (NvsManager rejection). |
| Cross-référence WAITING dans LOOP spec Phase 1+ pointe vers `_fgArpStopMax` supprimé | Confusion future si Phase 1 est repris en l'état | Task 4.5 : note de superseding en tête de fichier spec LOOP. |
| Defaults consolidation (Q6) skip "extract into header" | Dette technique persiste | Task 3.12 note : pragmatique pour ce refactor, à réévaluer si 4e site apparaît. |
| Build casse au milieu de Phase 1 (struct sans renderBank refacto) | Repo en état rouge entre tasks | Pas commit en fin de Phase 1 seule. Phase 1+2 = commit unique. |

---

## 10. Estimation

| Phase | Effort estimé | Notes |
|---|---|---|
| Phase 1 (struct + defaults) | 30 min | Multi-fichiers mais mécanique |
| Phase 2 (runtime) | 30 min | Math breathing, 1 invariant à vérifier |
| Phase 3 (Tool 8) | 90 min | Le plus gros chantier, ~14 sites à modifier dans un fichier |
| Phase 4 (docs) | 30 min | Tableaux markdown |
| HW gates + tuning W_WEIGHT | 30-60 min | Dépend de la précision visuelle souhaitée |
| **Total** | **~4 h** | Inclut tuning HW |

---

## 11. Cross-check du plan vs code réel (pré-flight)

Avant de démarrer Phase 1, exécuter ces 4 greps pour confirmer que le plan colle au repo HEAD :

```bash
# 1. Sites de membres LedController à renommer
grep -rn "_normalFgIntensity\|_fgArpStopMin\|_fgArpStopMax\|_fgArpPlayMax" src/

# 2. Sites struct LedSettingsStore field access
grep -rn "\.normalFgIntensity\|\.fgArpStopMin\|\.fgArpStopMax\|\.fgArpPlayMax" src/

# 3. Sites Tool 8 LineId à supprimer
grep -rn "LINE_NORMAL_FG_PCT\|LINE_ARPEG_FG_PCT\|LINE_LOOP_FG_PCT" src/

# 4. Bypass éventuels de setPixel (doit retourner uniquement LedController.cpp)
grep -rn "_strip\.\|setPixelColor" src/
```

Si l'un de ces greps retourne des sites HORS de la liste prévue dans ce plan, **arrêter et re-investiguer** avant de coder. Le plan a été écrit contre le HEAD du 2026-05-16 ; un drift code/plan serait suspect.

---

## 11bis. Extension grammaire visuelle (appliquée en cours d'exécution)

Décision prise après HW gate Phase 2 (cf. discussion session 2026-05-16) :
étendre le breathing ARPEG stopped-loaded **aussi au BG** (était FG-only
dans le design initial §2.2).

**Implémentation** : `renderBankArpeg` ligne ~478 — retirer le guard
`isFg &&` sur la branche breathing, scaler la valeur sine par `_bgFactor`
en sortie si non-FG.

**Effet** : tous les banks ARPEG en état stopped-loaded breathent en
phase, FG plein et BG à FG×bgFactor. Le tick flash playing reste le seul
différenciateur FG vs BG en état "playing".

**Invariant FG > BG préservé** : à chaque instant `t`, BG[t] = FG[t] ×
bgFactor / 100 avec bgFactor < 100. Pas de risque de croisement.

**Doc impactée** :
- `docs/reference/led-reference.md` §4 tableau (lignes BG ARPEG stopped-
  loaded ajoutée) + §4.x bug pattern "All ARPEG banks pulse simultaneously".

---

## 12. Suite (post-implémentation)

- Surveiller l'usage : si le user redemande "ARPEG plus brillant" malgré W_WEIGHT, envisager option B1 du brainstorming (remonter W=40 d'Ice Blue à W=100-120).
- LOOP Phase 1+ : appliquer les notes de Task 4.5 au moment de reprendre le plan LOOP. Le rename `fgArpPlayMax` est obsolète, utiliser `fgIntensity` directement.
- Defaults extraction (Q6, déférée) : si un 4e site apparaît (web JSON handler, MIDI sysex config…), extraire `LedSettingsDefaults.h` à ce moment.
