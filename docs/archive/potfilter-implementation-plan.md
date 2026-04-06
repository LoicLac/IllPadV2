# Plan : PotFilter adaptatif — Pipeline pot unifie

## Phase 1 — DONE (implemente 2026-04-04)

PotFilter cree et integre dans PotRouter, NvsManager, main.cpp.
Mesures hardware, bugs trouves et fixes pendant l'implementation :
- Sleep peek sans oversampling → faux reveils en boucle (fix: oversampleRead dans peek)
- Oversampling shift/reads mismatch (fix: boucle dynamique `count = 1 << shift`)
- Defaults ajustes aux mesures reelles (16x oversampling, bruit ~12-16 LSB)

**Valeurs finales en production** (PotFilter.cpp defaults) :

| Param | Valeur | Justification |
|-------|--------|---------------|
| OVERSAMPLE_SHIFT | 4 (16 reads) | Bruit mesure : 150 LSB raw → 12-16 LSB apres 16x |
| SNAP | 0.05 | Alpha max en mouvement |
| SENSE | 20.0 | Seuil activite, au-dessus du bruit 16x |
| DEADBAND | 20 | ~205 positions, marge 25% au-dessus du bruit |
| EDGE | 12 | Snap aux extremes |
| SLEEP | ON, 500ms | Arret ADC quand pot immobile |
| WAKE | 40 | Seuil de reveil (oversampled peek) |

**Contraintes a respecter pour les phases suivantes** :
- Core 1 only (pas thread-safe)
- Un seul appelant de `updateAll()` par cycle (runtime: PotRouter, setup: SetupPotInput)
- Boot order: `loadAll() → PotFilter::begin() → PotRouter::begin()` (commentaire dans main.cpp)
- Catch window (100) doit rester > 5x deadband

---

## Phases restantes

| Phase | Scope | Risque | Resultat |
|-------|-------|--------|----------|
| **Phase 2 — Pots universels** | SetupPotInput dual-mode + T3/T4/T5/T6/T7 | MOYEN (setup mode) | Pot = input par defaut dans tous les tools |
| **Phase 3 — Monitor** | Page Monitor dans Tool 6 | FAIBLE (additif pur) | Tuning temps reel des params filtre |

## Fichiers par phase

### Phase 2 — SetupPotInput dual-mode + pot universel dans tous les tools

| Fichier | Action |
|---------|--------|
| `src/setup/SetupPotInput.h` | Ajouter `PotMode` enum, `getMove(ch)`, `bool moved` par channel, supprimer JITTER_DZ + pin |
| `src/setup/SetupPotInput.cpp` | Rewire sur PotFilter (tool appelle updateAll, pas SetupPotInput), mode RELATIVE + ABSOLUTE |
| `src/setup/ToolPadRoles.cpp/.h` | Ajouter SetupPotInput : NAV grille (RELATIVE) + pool linearise toutes lignes (RELATIVE) |
| `src/setup/ToolBankConfig.cpp/.h` | Ajouter SetupPotInput : NAV banks (RELATIVE) + cycle combine 4 etats (RELATIVE) |
| `src/setup/ToolSettings.cpp/.h` | Ajouter SetupPotInput : NAV params (RELATIVE) + valeurs numeriques (ABSOLUTE) |
| `src/setup/ToolPotMapping.cpp/.h` | Rewire detectMovedPot → PotFilter, pot pool linearise + CC# en mode EDIT seulement |
| `src/setup/ToolLedSettings.cpp` | Adapter aux nouveaux seed() signatures (ajouter mode ABSOLUTE explicite) |

### Phase 3 — Monitor

| Fichier | Action |
|---------|--------|
| `src/setup/ToolPotMapping.h` | Ajouter membres monitor (_monitorMode, _monitorCfg, _monitorCursor) |
| `src/setup/ToolPotMapping.cpp` | Page Monitor VT100, boucle temps reel, pot RELATIVE pour cursor + ABSOLUTE pour valeurs |

---

Phase 1 implementation details removed — see git history for step-by-step changes.

---


---

# ═══════════════════════════════════════════════════
# PHASE 2 — POTS UNIVERSELS EN SETUP
# SetupPotInput dual-mode + integration dans tous les tools
# ═══════════════════════════════════════════════════

## Philosophie

Le pot est un input **par defaut** dans tous les tools, pas un bonus. Chaque tool
assume que le pot right 1 est disponible pour naviguer ET editer. Les fleches et
le pot sont **toujours compatibles** — aucun ne peut casser l'etat de l'autre.

## Deux modes de pot

Le conflit fleches/pot vient du re-ancrage absolu : le pot force sa position physique
comme valeur, ecrasant ce que les fleches ont modifie. La solution : deux modes.

### POT_RELATIVE — pour curseurs et selections discretes

- Differentiel pur, **jamais de re-ancrage**
- Le pot fonctionne comme une molette : seul le delta compte
- La baseline avance a chaque lecture → toujours de la course dans les deux sens
- Les fleches modifient la meme valeur → zero conflit (les deux sont des deltas)
- Pas besoin de re-seed apres une fleche
- Sensibilite configurable : `stepsPerFullTurn` = combien de steps pour 0→4095
  - `stepThreshold = 4096 / stepsPerFullTurn` ADC units par step
  - L'accumulateur franchit ce seuil → un step, le reste est conserve (Bresenham)
  - Ex: 48 pads → stepThreshold=85, 8 banks → stepThreshold=512

### POT_ABSOLUTE — pour valeurs continues

- Differentiel puis re-ancrage quand la position physique converge vers la valeur
- C'est le mode actuel de Tool 7 (hue, intensity, durees)
- Les fleches declenchent un re-seed (comme aujourd'hui)
- Adapte quand la position physique du pot doit refleter la valeur

### Convention universelle

| Contexte | Pot mode | Pourquoi |
|----------|----------|----------|
| Curseur / index (nav) | RELATIVE | Conflit fleches si absolu |
| Selection dans un pool discret | RELATIVE | Meme raison — c'est un index |
| Valeur numerique continue | ABSOLUTE | Position = valeur, intuitif |
| Toggle on/off, 2-3 choix | RELATIVE | Discret = index |

---

## Etape 3 — SetupPotInput dual-mode + PotFilter

### 3a. SetupPotInput.h — nouveau design

```cpp
enum PotMode : uint8_t { POT_RELATIVE, POT_ABSOLUTE };

struct Channel {
    int32_t* target;
    int32_t  minVal, maxVal;
    int32_t  lastRaw;        // Derniere lecture PotFilter (baseline differentielle)
    int32_t  baseline;       // Point de reference pour le delta
    int32_t  accumDelta;     // Accumulateur fractionnel (Bresenham)
    uint16_t stepThreshold;  // ADC units par step (RELATIVE only)
    PotMode  mode;
    bool     enabled;
    bool     active;         // A franchi MOVE_THRESHOLD depuis le seed
    bool     anchored;       // Re-anchor atteint (ABSOLUTE only, jamais vrai en RELATIVE)
    bool     moved;          // Target modifie ce cycle (cleared par getMove)
};
```

Changements vs actuel :
- Ajout `PotMode mode`, `uint16_t stepThreshold`, `bool moved`
- Suppression `pin` (PotFilter connait les pins)
- Suppression `JITTER_DZ = 6` (PotFilter gere le deadband)
- `NUM_CHANNELS` reste a **2** (right 1 + right 2)

**getMove(ch)** — retourne `_ch[ch].moved` puis le clear. Pattern consommable :
```cpp
bool getMove(uint8_t ch) {
    if (ch >= NUM_CHANNELS) return false;
    bool m = _ch[ch].moved;
    _ch[ch].moved = false;
    return m;
}
```
Chaque tool poll apres `update()` pour savoir si un channel specifique a change.
`seed()` et `disable()` reset `moved = false`.

### 3b. SetupPotInput.cpp — rewire sur PotFilter + mode RELATIVE

**seed() — nouvelle signature :**
```cpp
void seed(uint8_t ch, int32_t* target, int32_t minVal, int32_t maxVal,
          PotMode mode = POT_ABSOLUTE, uint16_t stepsHint = 0);
```
- `mode` : RELATIVE ou ABSOLUTE
- `stepsHint` : nombre de steps pour un tour complet (RELATIVE only). Si 0, calcul auto = `maxVal - minVal + 1`
- Calcul : `stepThreshold = 4096 / stepsHint` (minimum 1)
- Init : `lastRaw = baseline = PotFilter::getStable(ch)`, `active = false`, `anchored = false`

**update() — lit PotFilter, NE fait PAS updateAll() :**

**Regle d'ownership :** C'est le `run()` du tool qui appelle `PotFilter::updateAll()`
une seule fois en debut de boucle, AVANT `_pots.update()`. SetupPotInput ne fait que
lire `PotFilter::getStable(ch)`. Cela permet a Tool 6 d'utiliser getStable() pour
sa propre detection de pot en plus de SetupPotInput.

```
// PotFilter::updateAll() deja appele par le tool

for ch = 0..1:
  _ch[ch].moved = false       // reset chaque cycle
  if !enabled: continue
  raw = PotFilter::getStable(ch)

  // Activation (identique aux deux modes)
  if !active:
    if |raw - baseline| >= MOVE_THRESHOLD: active = true, baseline = raw
    else: continue

  if mode == POT_RELATIVE:
    // Delta brut
    adcDelta = raw - baseline
    baseline = raw              // baseline avance toujours
    accumDelta += adcDelta

    // Conversion delta → steps (Bresenham)
    while |accumDelta| >= stepThreshold:
      if accumDelta > 0:
        *target = min(*target + 1, maxVal)
        accumDelta -= stepThreshold
        _ch[ch].moved = true
      else:
        *target = max(*target - 1, minVal)
        accumDelta += stepThreshold
        _ch[ch].moved = true

  else:  // POT_ABSOLUTE
    // ... code actuel inchange (differentiel + re-anchor) ...
    // set _ch[ch].moved = true si *target change
```

**Pas de re-anchor en RELATIVE** — `anchored` reste toujours `false`. Le pot ne saute
jamais a une position absolue. Les fleches peuvent modifier `*target` librement entre
deux updates sans conflit.

### 3c. Adapter Tool 7 (ToolLedSettings)

Ajouter `POT_ABSOLUTE` explicite dans les appels `seed()` existants :
```cpp
// Avant (implicite):
_pots.seed(0, &_potVal[0], minVal, maxVal);
// Apres (explicite):
_pots.seed(0, &_potVal[0], minVal, maxVal, POT_ABSOLUTE);
```
Aucun changement de comportement — c'est le mode par defaut, on le rend explicite.

**Verification etape 3 :** build + upload. Tool 7 fonctionne exactement comme avant.

---

## Etape 4 — Pot dans Tool 3 (Pad Roles)

Le plus gros gain : la selection dans le pool (30 items sur 7 lignes) devient fluide au pot.

### 4a. ToolPadRoles.h — ajouter membre

```cpp
#include "SetupPotInput.h"
SetupPotInput _pots;
int32_t _potNavIdx;       // Index lineaire 0-47 pour nav grille (pot target)
int32_t _potPoolLinear;   // Index lineaire 0-29 pour pool (pot target en edit mode)
```

### 4b. Pool linearise — table de mapping

Le pool Tool 3 a 7 lignes de tailles differentes. Le pot les traverse en continu :

```
Ligne  0: Clear      (1 item)   offsets 0
Ligne  1: Banks      (8 items)  offsets 1-8
Ligne  2: Roots      (7 items)  offsets 9-15
Ligne  3: Modes      (8 items)  offsets 16-23
Ligne  4: Octaves    (4 items)  offsets 24-27
Ligne  5: Hold       (1 item)   offset  28
Ligne  6: Play/Stop  (1 item)   offset  29
                                 TOTAL = 30
```

Table cumulative (constante, compile-time) :
```cpp
static const uint8_t POOL_OFFSETS[] = {0, 1, 9, 16, 24, 28, 29, 30};
static const uint8_t TOTAL_POOL_ITEMS = 30;
```

**Conversion linear → 2D :**
```cpp
void linearToPool(int32_t linear, uint8_t& poolLine, uint8_t& poolIdx) {
    for (uint8_t i = 0; i < POOL_LINE_COUNT; i++) {
        if (linear < POOL_OFFSETS[i + 1]) {
            poolLine = i;
            poolIdx = linear - POOL_OFFSETS[i];
            return;
        }
    }
    poolLine = POOL_LINE_COUNT - 1;
    poolIdx = 0;
}
```

**Conversion 2D → linear :**
```cpp
int32_t poolToLinear(uint8_t poolLine, uint8_t poolIdx) {
    return POOL_OFFSETS[poolLine] + poolIdx;
}
```

### 4c. ToolPadRoles.cpp — integration

**Boucle principale — PotFilter en premier :**
```cpp
// En debut de boucle run()
PotFilter::updateAll();   // UNE SEULE FOIS — alimente getStable()
_pots.update();           // Lit getStable(0), getStable(1)
```

**Mode GRID (navigation) :**
```cpp
// Au debut de run(), seed pot 1 sur l'index grille linearise
_potNavIdx = _gridRow * 12 + _gridCol;
_pots.seed(0, &_potNavIdx, 0, 47, POT_RELATIVE);  // 48 pads, ~1 step par 85 ADC

// Dans la boucle :
if (_pots.getMove(0)) {
    _gridRow = _potNavIdx / 12;
    _gridCol = _potNavIdx % 12;
    screenDirty = true;
}
// Les fleches modifient _gridRow/_gridCol → recalculer _potNavIdx
if (arrowMoved) {
    _potNavIdx = _gridRow * 12 + _gridCol;
    // PAS de re-seed — en RELATIVE, le pot applique des deltas sur la nouvelle valeur
}
// Touch pad → idem, mettre a jour _potNavIdx
if (touchJumped) {
    _potNavIdx = _gridRow * 12 + _gridCol;
}
```

**Mode POOL (selection role) — pot linearise :**
```cpp
// Entree en edit (ENTER depuis grid) : seed pot 1 sur l'index pool linearise
_potPoolLinear = poolToLinear(_poolLine, _poolIdx);
_pots.seed(0, &_potPoolLinear, 0, TOTAL_POOL_ITEMS - 1, POT_RELATIVE, TOTAL_POOL_ITEMS * 2);
// stepsHint = 60 → un demi-tour de pot = tout le pool

// Dans la boucle edit :
if (_pots.getMove(0)) {
    linearToPool(_potPoolLinear, _poolLine, _poolIdx);
    screenDirty = true;
}
// Les fleches UP/DOWN/LEFT/RIGHT modifient _poolLine/_poolIdx en 2D → recalculer
if (arrowMoved) {
    _potPoolLinear = poolToLinear(_poolLine, _poolIdx);
}
```

**Re-seeds aux transitions :**
```
ENTER (grid → pool) : seed sur _potPoolLinear (ci-dessus)
q/assign (pool → grid) : re-seed sur _potNavIdx :
    _potNavIdx = _gridRow * 12 + _gridCol;
    _pots.seed(0, &_potNavIdx, 0, 47, POT_RELATIVE);
```

**Synchronisation fleches → pot :** En mode RELATIVE, quand les fleches modifient
la position (2D), on recalcule l'index lineaire dans `*target`. Le pot continuera
ses deltas relatifs a partir de la nouvelle valeur. Zero conflit, zero re-seed.

**Verification etape 4 :** build + upload. Tool 3 :
- Pot right 1 navigue fluide dans la grille 48 pads (clampe aux bords)
- ENTER → pot defile les 30 items du pool en continu (toutes lignes)
- UP/DOWN (fleches) saute de ligne, pot se resync
- q → retour grille, pot re-seede sur la position courante
- Touch pad saute le curseur, pot resync (_potNavIdx mis a jour)
- Fleches et pot alternent librement sans conflit

---

## Etape 5 — Pot dans Tool 4 (Bank Config)

Petit gain mais coherence : meme pattern que les autres tools.

### 5a. ToolBankConfig.h — ajouter membre

```cpp
#include "SetupPotInput.h"
SetupPotInput _pots;
int32_t _potBankIdx;     // 0-7 (nav mode)
int32_t _potComboState;  // 0-3 cycle combine (edit mode)
```

### 5b. ToolBankConfig.cpp — integration

**Boucle principale :**
```cpp
PotFilter::updateAll();
_pots.update();
```

**Mode NAV :** `_pots.seed(0, &_potBankIdx, 0, 7, POT_RELATIVE, 16)` — 8 banks, un quart de tour.

**Mode EDIT — cycle combine 4 etats :**

Le code actuel cycle LEFT/RIGHT a travers : NORMAL(0) → ARPEG-Immediate(1) → ARPEG-Beat(2) → ARPEG-Bar(3).
Le pot fait pareil avec un index combine 0-3 :

```cpp
// Entree en edit :
_potComboState = (wkTypes[cursor] == BANK_NORMAL) ? 0 : 1 + wkQuantize[cursor];
_pots.seed(0, &_potComboState, 0, 3, POT_RELATIVE, 8);  // 4 etats, demi-tour
```

**Conversion pot → type/quantize :**
```cpp
if (_pots.getMove(0)) {
    // Constraint max 4 ARPEG : si _potComboState > 0 et deja 4 arps, clamper a 0
    if (_potComboState > 0) {
        uint8_t arpCount = 0;
        for (uint8_t i = 0; i < NUM_BANKS; i++)
            if (i != cursor && wkTypes[i] == BANK_ARPEG) arpCount++;
        if (arpCount >= 4) {
            _potComboState = 0;  // force retour NORMAL
            errorShown = true; errorTime = millis();
        }
    }
    // Appliquer
    if (_potComboState == 0) {
        wkTypes[cursor] = BANK_NORMAL;
        wkQuantize[cursor] = DEFAULT_ARP_START_MODE;
    } else {
        wkTypes[cursor] = BANK_ARPEG;
        wkQuantize[cursor] = _potComboState - 1;  // 0=Imm, 1=Beat, 2=Bar
    }
    screenDirty = true;
}
```

**Synchro fleches → pot :** Les fleches modifient wkTypes/wkQuantize directement,
puis recalculent `_potComboState` :
```cpp
if (arrowMoved) {
    _potComboState = (wkTypes[cursor] == BANK_NORMAL) ? 0 : 1 + wkQuantize[cursor];
}
```

**Re-seeds :**
```
ENTER (nav → edit) : seed _potComboState (ci-dessus)
ENTER (edit → save) : re-seed _potBankIdx sur cursor
q (edit → cancel)   : re-seed _potBankIdx sur cursor
```

**Verification etape 5 :** build. Tool 4 :
- Pot navigue les 8 banks (RELATIVE)
- ENTER → pot cycle les 4 etats (NORMAL/Imm/Beat/Bar)
- Max 4 ARPEG respecte : le pot clampe a NORMAL si limite atteinte + feedback erreur
- Fleches et pot alternent sans conflit

---

## Etape 6 — Pot dans Tool 5 (Settings)

Le gain principal : 3 parametres numeriques continus (AT rate, double-tap, bargraph duration).

### 6a. ToolSettings.h — ajouter membre

```cpp
#include "SetupPotInput.h"
SetupPotInput _pots;
int32_t _potCursorIdx;  // 0-7 param index
int32_t _potEditVal;    // Valeur en cours d'edition
```

### 6b. ToolSettings.cpp — integration

**Boucle principale :**
```cpp
PotFilter::updateAll();
_pots.update();
```

**Mode NAV :** `_pots.seed(0, &_potCursorIdx, 0, 7, POT_RELATIVE, 16)`

**Mode EDIT :** seed selon le param actif :
```
param 0 (profile)     : seed(0, &val, 0, 2, POT_RELATIVE, 6)        // 3 choix discrets
param 1 (AT rate)     : seed(0, &val, 10, 100, POT_ABSOLUTE)         // continu (AT_RATE_MIN/MAX)
param 2 (BLE interval): seed(0, &val, 0, 3, POT_RELATIVE, 8)        // 4 choix
param 3 (clock mode)  : seed(0, &val, 0, 1, POT_RELATIVE, 4)        // 2 choix
param 4 (double-tap)  : seed(0, &val, 100, 250, POT_ABSOLUTE)        // continu
param 5 (bargraph dur): seed(0, &val, 1000, 10000, POT_ABSOLUTE)     // continu
param 6 (panic)       : seed(0, &val, 0, 1, POT_RELATIVE, 4)        // toggle
param 7 (battery cal) : disable(0)                                    // read-only
```

**Note :** Les fleches ont un mode `accelerated` (step ×5/×10 sur certains params).
Le pot ABSOLUTE n'a pas ce concept — le sweep est physique et continu. C'est un
compromis accepte : le pot offre un controle precis, les fleches offrent des sauts rapides.

**Re-seeds :**
```
ENTER (nav → edit) : seed selon param (table ci-dessus)
ENTER (edit → save) : re-seed _potCursorIdx
q (edit → cancel)   : re-seed _potCursorIdx
UP/DOWN (nav)       : pas de re-seed (RELATIVE, meme target)
```

**Verification etape 6 :** build + upload. Tool 5 :
- Pot navigue les 8 params (RELATIVE)
- Pot ajuste AT rate (10-100) / double-tap / bargraph en continu (ABSOLUTE)
- Pot cycle les choix discrets (RELATIVE)
- Fleches toujours fonctionnelles en parallele

---

## Etape 7 — Pot dans Tool 6 (Pot Mapping)

Tool 6 garde `detectMovedPot()` pour la nav slots (inchange). Le pot right 1 via
SetupPotInput s'active **uniquement en mode EDIT** (pool + CC#).

### 7a. ToolPotMapping.h — ajouter membre

```cpp
#include "SetupPotInput.h"
SetupPotInput _pots;
int32_t _potPoolIdx;    // Pool index linearise (edit mode)
int32_t _potCcNum;      // CC# 0-127 (CC edit mode)
```
Pas de `_potSlotIdx` — la nav slots reste par detection physique + fleches.

### 7b. ToolPotMapping.cpp — integration

**Rewire detectMovedPot() sur PotFilter :**
```cpp
void ToolPotMapping::samplePotBaselines() {
    for (uint8_t i = 0; i < 4; i++)
        _potBaseline[i] = PotFilter::getStable(i);  // remplace analogRead
}

int8_t ToolPotMapping::detectMovedPot(bool btnLeftHeld) {
    for (uint8_t i = 0; i < 4; i++) {
        uint16_t val = PotFilter::getStable(i);     // remplace analogRead
        int16_t delta = (int16_t)val - (int16_t)_potBaseline[i];
        if (delta < 0) delta = -delta;
        if ((uint16_t)delta > POT_DETECT_THRESHOLD) {
            _potBaseline[i] = val;
            return (int8_t)(i * 2 + (btnLeftHeld ? 1 : 0));
        }
    }
    return -1;
}
```

**Seuil de detection :** Reduire `POT_DETECT_THRESHOLD` de 200 (raw ADC) a **60**
(signal filtre). Avec PotFilter (deadband=20), un delta de 60 = ~3x le deadband,
suffisant pour distinguer un mouvement intentionnel du bruit residuel.

**Boucle principale :**
```cpp
PotFilter::updateAll();   // UNE SEULE FOIS — alimente getStable() pour tout
_pots.update();           // Lit getStable(0) pour le channel pot right 1
```

**Mode NAV :** `detectMovedPot()` inchange (4 pots, jump au slot). SetupPotInput
desactive (`_pots.disable(0)`). Fleches UP/DOWN/LEFT/RIGHT pour nav fine.

**Mode POOL (apres ENTER) :** Le pool Tool 6 est deja un tableau plat (`_pool[]`,
`_poolCount` items). Pas besoin de linearisation — l'index est deja 0 a _poolCount-1.
```cpp
_potPoolIdx = _poolIdx;
_pots.seed(0, &_potPoolIdx, 0, _poolCount - 1, POT_RELATIVE, _poolCount * 2);
```
```cpp
if (_pots.getMove(0)) {
    _poolIdx = (uint8_t)_potPoolIdx;
    screenDirty = true;
}
// Fleches LEFT/RIGHT modifient _poolIdx → resync :
if (arrowMoved) _potPoolIdx = _poolIdx;
```

**Mode CC# (sous-editeur) :** Sweep continu 0-127.
```cpp
_potCcNum = _ccNumber;
_pots.seed(0, &_potCcNum, 0, 127, POT_ABSOLUTE);
```
```cpp
if (_pots.getMove(0)) {
    _ccNumber = (uint8_t)_potCcNum;
    screenDirty = true;
}
```

**Re-seeds :**
```
ENTER (nav → pool edit) : seed _potPoolIdx
Pool → CC# sub-mode     : re-seed _potCcNum (ABSOLUTE)
CC# ENTER/q             : re-seed _potPoolIdx ou disable selon retour
q (pool → nav)          : _pots.disable(0)
```

**Verification etape 7 :** build + upload. Tool 6 :
- Mode NAV : detection physique des 4 pots fonctionne (seuil 60 sur filtered)
- ENTER → pot right 1 defile le pool (10+ items, RELATIVE)
- Pool → CC# : pot sweep 0-127 (ABSOLUTE), fleches aussi
- q → retour nav, pot desactive
- Fleches et pot alternent sans conflit dans chaque mode

---

### Verification Phase 2 complete

| Tool | Test |
|------|------|
| T3 | Pot nav grille 48 pads (RELATIVE, clamp) + pool linearise 30 items, re-seed grid↔pool, touch OK, fleches 2D OK |
| T4 | Pot nav 8 banks (RELATIVE) + cycle combine 4 etats (NORMAL/Imm/Beat/Bar), max 4 ARPEG respecte, fleches OK |
| T5 | Pot nav 8 params (RELATIVE) + edit continus ABSOLUTE (AT 10-100, double-tap, bargraph) + discrets RELATIVE, fleches OK |
| T6 | Mode NAV = detection physique 4 pots (seuil 60) inchange. Mode EDIT = pot pool RELATIVE + CC# ABSOLUTE. Disable en NAV |
| T7 | Inchange (POT_ABSOLUTE explicite), hue/intensity/timings OK — tester le feeling post-PotFilter |
| T1-T2 | Inchanges, pas de regression |

**Regle universelle verifiee** : dans chaque tool, alterner pot et fleches ne cree
jamais de saut de valeur ni de conflit de position.

**Regle PotFilter::updateAll()** : chaque tool appelle `PotFilter::updateAll()` UNE FOIS
en debut de boucle, avant `_pots.update()`. SetupPotInput ne fait que lire `getStable()`.

**Note Tool 7** : le switch analogRead → PotFilter::getStable() change les caracteristiques
du signal (EMA + deadband). Le feeling subjectif du pot peut varier — tester en conditions
reelles et ajuster si necessaire.

**La Phase 2 peut etre mergee separement.** Prerequis : Phase 1 mergee.

---

# ═══════════════════════════════════════════════════
# PHASE 3 — MONITOR
# Page Monitor temps reel dans Tool 6
# Feature additive, zero risque de regression
# ═══════════════════════════════════════════════════

## Design du Pot Monitor

### Objectif

Outil de diagnostic ET de tuning. L'utilisateur doit pouvoir :
1. **VOIR** le bruit ADC brut et ce que le filtre en fait — cote a cote, temps reel
2. **COMPRENDRE** chaque parametre (description inline, pas juste un nom)
3. **TUNER** les parametres et voir l'effet immediatement sur les barres
4. **SAUVER** quand le resultat est satisfaisant

### Pourquoi raw + stable

Le raw ADC jitter visiblement a l'ecran (~10-20 LSB de bruit ESP32). Le stable
reste immobile. Ce contraste visuel est LA demonstration que le filtre fonctionne.
Le delta Δ = |raw − stable| montre l'ecart instantane : si Δ depasse souvent le
deadband, c'est que le deadband est trop bas.

### Pas de navigation par pot dans ce tool

Les 4 game pots sont **observes** par les barres — l'utilisateur les tourne pour
voir l'effet du filtre en temps reel. Le pot right 1 ne peut pas servir a la fois
de cible d'observation ET de controle de navigation. Navigation = fleches uniquement.

### Interaction

| Input | Action |
|-------|--------|
| **↑/↓** | Navigue le curseur params (7 lignes) |
| **←/→** | Ajuste la valeur du param selectionne, applique en live via `setConfig()` |
| **Tourner un game pot** | La barre correspondante s'anime : raw jitter, stable suit, etat change. C'est le feedback visuel du tuning. |
| **s** | Save to NVS + feedback "Saved" |
| **d** | Reset defaults + apply live |
| **m** ou **q** | Retour a la page Pot Mapping |

---

## Etape 8 — Page Monitor dans Tool 6

### 8a. ToolPotMapping.h

Ajouter :
```cpp
bool _monitorMode;            // Toggle avec 'm'
PotFilterStore _monitorCfg;   // Copie de travail des params
uint8_t _monitorCursor;       // 0-6 (7 params ajustables)
```

### 8b. ToolPotMapping.cpp — touche 'm' et boucle monitor

Dans la boucle principale de `run()`, ajouter handler pour `'m'` :
```cpp
case 'm':
    _monitorMode = !_monitorMode;
    if (_monitorMode) {
        _monitorCfg = PotFilter::getConfig();
        _monitorCursor = 0;
        drawMonitorFull();
    } else {
        redrawFull();  // retour a la page mapping
    }
    break;
```

Quand `_monitorMode == true`, la boucle appelle `updateMonitor()` au lieu du flow
mapping normal. Les pots SetupPotInput sont desactives (disable) — on ne veut pas
qu'ils interferent avec l'observation.

### 8c. drawMonitorFull() — rendu VT100

Layout 80 colonnes (suivre vt100-design-guide.md) :
```
┌──────────────────────────────────────────────────────────────────────────────┐
│  [6] POT MAPPING — FILTER MONITOR                                          │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  RIGHT 1   raw 2047   stable 2044   Δ3    ████████████░░░░  ACTIVE          │
│  RIGHT 2   raw 1520   stable 1523   Δ3    ██████░░░░░░░░░░  SETTLING        │
│  RIGHT 3   raw 4009   stable 4012   Δ3    ████████████████  ACTIVE          │
│  RIGHT 4   raw   28   stable   23   Δ5    ░░░░░░░░░░░░░░░░  SLEEP           │
│  REAR      raw 2504   stable 2501   Δ3    ██████████░░░░░░  SLEEP           │
│                                                                              │
│  ── FILTER PARAMETERS ───────────────────────────────────────────────────    │
│                                                                              │
│▸ SNAP       0.05    Tracking speed (higher = faster, more noise)             │
│  SENSE      15.0    Noise floor (raw Δ below this = frozen)                  │
│  DEADBAND     16    Output gate (stable moves only when smoothed Δ ≥ this)   │
│  EDGE         12    Snap to 0/4095 near endpoints                            │
│  SLEEP        ON    Stop ADC reads when pot idle                             │
│  DELAY      500ms   Idle time before sleep                                   │
│  WAKE         40    ADC Δ needed to exit sleep                               │
│                                                                              │
│  [▲▼] select   [◄►] adjust   [s] save   [d] defaults   [m] back             │
└──────────────────────────────────────────────────────────────────────────────┘
```

**Section barres (5 lignes, refresh continu) :**

| Element | Source | Comportement |
|---------|--------|-------------|
| `raw XXXX` | `PotFilter::getRaw(i)` | Jitter visible — le nombre saute. L'utilisateur VOIT le bruit ESP32. |
| `stable XXXX` | `PotFilter::getStable(i)` | Immobile quand le pot ne bouge pas. Contraste avec raw = efficacite du filtre. |
| `ΔXXX` | `abs(raw − stable)` | Ecart instantane. Si Δ > deadband souvent → deadband trop bas. |
| Barre 16 chars | `stable / 4095 × 16` | Position stable. Ne redraw que quand stable change (pas de flicker). |
| Etat | `.state` | ACTIVE = vert, SETTLING = jaune, SLEEP = dim |

Les valeurs numeriques raw/Δ se rafraichissent a chaque cycle (~30fps). La barre ne
redraw que sur changement de stable (cursor-positioned update, pas full redraw).

Quand un pot dort : raw affiche la valeur du dernier peek (rafraichi toutes les 50ms).
Le Δ par rapport a la baseline sleep est visible — quand Δ monte vers WAKE, l'utilisateur
comprend pourquoi le pot va se reveiller.

**Section parametres (7 lignes) :**

Groupes par logique : filtre actif (SNAP, SENSE, DEADBAND, EDGE) puis sleep (SLEEP, DELAY, WAKE).

| # | Param | Range | Step | Effet visible en changeant |
|---|-------|-------|------|---------------------------|
| 0 | SNAP | 0.01–0.30 | ±0.01 | raw→stable convergent plus/moins vite quand un pot bouge |
| 1 | SENSE | 2.0–20.0 | ±0.5 | Seuil sous lequel le bruit est ignore. Trop bas = jitter, trop haut = lent |
| 2 | DEADBAND | 1–30 | ±1 | Plus haut = stable bouge moins souvent, plus propre mais perd en resolution |
| 3 | EDGE | 0–30 | ±1 | Visible aux extremes du pot — stable verrouille a 0 ou 4095 |
| 4 | SLEEP | ON/OFF | toggle | OFF → les pots restent ACTIVE (raw jitter permanent). ON → transition vers SLEEP |
| 5 | DELAY | 100–2000ms | ±100 | Plus court → SETTLING→SLEEP plus rapide apres avoir lache un pot |
| 6 | WAKE | 10–100 | ±5 | Plus haut → faut tourner plus fort pour reveiller. Le Δ monte avant le reveil |

### 8d. updateMonitor() — boucle temps reel

```
PotFilter::updateAll()          // rafraichir le filtre (les barres en dependent)
drawMonitorBars()               // raw + stable + Δ + barre + etat pour les 5 pots

if input UP/DOWN: move _monitorCursor (0-6, wrapping)
if input LEFT/RIGHT:
    switch _monitorCursor:
      0: _monitorCfg.snap100 ± 1 (clamp 1-30)
      1: _monitorCfg.actThresh10 ± 5 (clamp 20-200)
      2: _monitorCfg.deadband ± 1 (clamp 1-30)
      3: _monitorCfg.edgeSnap ± 1 (clamp 0-30)
      4: _monitorCfg.sleepEn ^= 1
      5: _monitorCfg.sleepMs ± 100 (clamp 100-2000)
      6: _monitorCfg.wakeThresh ± 5 (clamp 10-100)
    PotFilter::setConfig(_monitorCfg)  // applique en live
    drawMonitorParams()                // redessiner la section params

if input 's':
    _monitorCfg.magic = EEPROM_MAGIC
    _monitorCfg.version = POT_FILTER_VERSION
    NvsManager::saveBlob(POTFILTER_NVS_NAMESPACE, POTFILTER_NVS_KEY,
                         &_monitorCfg, sizeof(PotFilterStore))
    afficher "Saved" feedback

if input 'd':
    applyDefaults into _monitorCfg
    PotFilter::setConfig(_monitorCfg)
    drawMonitorParams()

if input 'm' or 'q': sortir du monitor
```

**Apply en temps reel** : chaque changement ←/→ appelle `setConfig()`. L'utilisateur
voit immediatement l'effet sur les barres. Le workflow typique :
1. Tourner un game pot → regarder raw/stable/Δ
2. Ajuster DEADBAND → le Δ necessaire pour un changement de stable augmente/diminue
3. Lacher le pot → observer SETTLING → SLEEP
4. Ajuster WAKE → tourner legerement le pot → voir si le reveil est trop sensible
5. Quand satisfait → 's' pour sauver

**Verification etape 8 :** build + upload. Entrer Tool 6 → 'm' :
- 5 barres affichent raw + stable + Δ + etat en temps reel
- Tourner un game pot → raw jitter visible, stable suit proprement, Δ fluctue
- Lacher → SETTLING (jaune) → SLEEP (dim) apres DELAY
- Ajuster DEADBAND ←/→ → stable devient plus/moins sensible (visible immediatement)
- Ajuster WAKE ←/→ → tourner legerement un pot endormi → voir si le seuil est bon
- 'd' reset les defaults, 's' sauve, 'm' retourne au mapping
- Pot right 1 ne fait RIEN dans ce tool (pas de nav pot)

### Verification Phase 3 complete

1. **Raw vs stable** : le contraste visuel montre clairement l'efficacite du filtre
2. **Δ utile** : permet de calibrer le deadband vs le bruit reel de chaque pot
3. **Etats visibles** : ACTIVE (vert) → SETTLING (jaune) → SLEEP (dim)
4. **Tuning live** : chaque changement de param est visible en temps reel
5. **Save/defaults/reload** : 's' sauve, 'd' reset, reboot restaure
6. **Pas de conflit pot** : navigation fleches uniquement, game pots = observation

---

# ═══════════════════════════════════════════════════
# VERIFICATION END-TO-END (apres les 3 phases)
# ═══════════════════════════════════════════════════

### Phase 1 — Runtime
1. **Jeu live** : jouer des notes, tourner pots — pas de jitter, pas de latence perceptible, CC propres
2. **Sleep** : lacher tous les pots 1s → transition SETTLING → SLEEP
3. **Catch** : bank switch → per-bank uncatch, global preserved, bargraph affiche la cible
4. **NVS** : PotFilterStore charge au boot si present, defaults sinon

### Phase 2 — Pots universels en setup
5. **Pot RELATIVE nav** : T3 grille+pool linearise, T4 banks+cycle combine, T5 params+discrets
6. **Pot ABSOLUTE edit** : T5 continus (AT 10-100, double-tap, bargraph) + T6 CC# + T7 hue/intensity
7. **Zero conflit** : dans chaque tool, alterner pot et fleches ne cree jamais de saut
8. **Tool 6 mode EDIT** : pot right 1 pool + CC# uniquement, detection physique 4 pots inchangee en NAV
9. **Tool 7 feeling** : verifier que le switch analogRead → PotFilter::getStable() ne degrade pas l'UX

### Phase 3 — Monitor
9. **Raw vs stable** : 5 barres temps reel avec raw jitter + stable propre + Δ
10. **7 params** : SNAP/SENSE/DEADBAND/EDGE/SLEEP/DELAY/WAKE, descriptions inline
11. **Tuning live** : ajuster un param → voir l'effet immediatement sur les barres
12. **Save/reload** : config persist apres reboot, defaults restaurables avec 'd'
