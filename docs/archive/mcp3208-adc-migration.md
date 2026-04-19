# Spec : Migration ADC interne → MCP3208 + Simplification PotFilter

**Statut** : spec finalisee, implementation en attente de go/no-go.
**Date** : 2026-04-16
**Scope** : remplacer les 5 lectures ADC internes ESP32-S3 par un MCP3208 externe SPI,
et simplifier la chaine de filtrage en supprimant les etages rendus inutiles par le bruit
quasi-nul de l'ADC externe.

---

## 1. Motivation

L'ADC interne de l'ESP32-S3 a un bruit de ~150 LSB raw. Pour le compenser, PotFilter
empile 6 etages de traitement (16x oversampling, EMA adaptative avec float, deadband
large). Cette chaine :

- **Limite la resolution** a ~205 positions distinctes sur 4096 (deadband=20)
- **Ajoute de la latence** (EMA adaptative ralentit le tracking)
- **Consomme du CPU** (~1600 us/cycle pour les 16 lectures ADC par pot)
- **Complexifie le code** (float, 3 etats machine, 6 parametres NVS)

Le pitch bend (14-bit MIDI, 16384 valeurs theoriques) n'exploite que 205 positions —
l'effet staircase est audible sur un instrument de scene.

Le MCP3208 a un bruit de ~2 LSB a 3.3V. Un simple deadband de 3 suffit — aucun
filtrage intermediaire necessaire.

---

## 2. MCP3208-CI/P a 3.3V — specifications

Source : datasheet Microchip DS21298E, figures 2-25 et 4-2.

| Parametre | Valeur | Notes |
|-----------|--------|-------|
| ENOB | **11.0–11.2 bits** | Interpole Fig 2-25 (SINAD 72 dB @ 5V → ENOB 11.67, degrade a 3.3V) |
| Bruit statique | **~2 LSB p-p** | Derive de ENOB : 2^12 / 2^11.1 ≈ 1.9 LSB RMS |
| INL (grade C) | ±1.0 LSB typ, ±2.0 max | No missing codes |
| DNL | ±0.5 LSB typ, ±1.0 max | No missing codes |
| SPI clock max | **~1.26 MHz** | Interpole lineaire 1.0 MHz@2.7V → 2.0 MHz@5.0V |
| Consommation | ~150–175 uA | ~0.5 mW (negligeable vs ESP32 ~150 mA) |
| Conversion | 13.5 clocks | 12 clocks conversion + 1.5 sample |
| Temps par lecture | **~15 us** | A 1 MHz SPI clock (default de la lib) |
| Impedance source max | < 1 kohm | Pour pleine precision a 1 MHz (Fig 4-2) |

### Source impedance des pots

Pot 10 kohm : impedance wiper au milieu = 2.5 kohm. Le MCP3208 perd ~0.5 LSB en
INL a cette impedance (Fig 4-2). Avec filtre RC optionnel (100 ohm serie), l'impedance
tombe a ~100 ohm — zero degradation.

---

## 3. Chaine de traitement — avant / apres

### 3.1 Chaine actuelle (ADC interne ESP32-S3)

6 etages, dont 2 filtres modifiant le signal et 2 gates le bloquant.

| # | Etage | Type | Parametres | Raison d'etre |
|---|-------|------|------------|---------------|
| 1 | **Oversampling 16x** | Filtre | OVERSAMPLE_SHIFT=4 (compile) | Reduit bruit 150→12-16 LSB |
| 2 | **Sleep/Wake** | Gate | sleepEn, sleepMs, wakeThresh (NVS) | CPU idle quand pot immobile |
| 3 | **EMA adaptative** | Filtre | snap100, actThresh10 (NVS), SETTLE_ALPHA (compile) | Lisse le bruit residuel 12-16 LSB |
| 4 | **Deadband** | Gate | deadband=20 (NVS) | Empeche les updates parasites |
| 5 | **Edge snap** | Quantize | edgeSnap=12 (NVS) | Min/max propres aux extremites |
| 6 | **Move flag** | Signal | — | Gate le downstream (PotRouter) |

**Pipeline float** : `raw` → `smoothed` (float EMA) → `stable` (uint16). Chaque cycle :
calcul d'activite (float), alpha adaptatif (float), mise a jour EMA (float), comparaison
deadband (float→int).

**3 etats machine** : ACTIVE → SETTLING → SLEEPING.
SETTLING existe uniquement pour le fade-out de l'EMA (alpha=0.001, quasi-gele).

### 3.2 Chaine cible (MCP3208)

3 etages, zero float, integer pur.

| # | Etage | Changement | Detail |
|---|-------|------------|--------|
| 1 | ~~Oversampling 16x~~ | **SUPPRIME** | 1 lecture SPI (~15 us), pas de boucle |
| 2 | Sleep/Wake | **SIMPLIFIE** | 2 etats seulement (ACTIVE / SLEEPING) |
| 3 | ~~EMA adaptative~~ | **SUPPRIME** | Plus de float, plus de smoothed/activity |
| 4 | Deadband | **REDUIT** | 20 → 3 (~1365 positions) |
| 5 | Edge snap | **REDUIT** | 12 → 3 |
| 6 | Move flag | Inchange | — |

```
Nouvelle chaine :
  SPI read (uint16_t) → sleep/wake gate → deadband (int compare) → edge snap → moved flag
```

**Supprime** :
- `oversampleRead()` (fonction entiere)
- `OVERSAMPLE_SHIFT`, `SETTLE_ALPHA` (constantes)
- `smoothed` (float par pot), `activity` (float par pot) dans PotData
- `POT_SETTLING` dans l'enum PotState
- `snap100`, `actThresh10` dans PotFilterStore
- Tout calcul float dans PotFilter

---

## 4. Resolution effective

| Parametre | Avant (ADC, db=20) | Apres (MCP3208, db=3) | Gain |
|-----------|--------------------|-----------------------|------|
| Positions distinctes /4096 | ~205 | **~1365** | x6.7 |
| Tempo (251 valeurs) | 205 | 251 (toutes) | plein |
| MIDI CC (128) | 128 | 128 | = |
| Base Velocity (127) | 127 | 127 | = |
| **Pitch Bend (16384)** | **205** | **1365** | **x6.7** |
| Shape / Gate / Depth | ~205 | ~1365 | x6.7 |

> **deadband=2** donnerait ~2048 positions (proche du plafond ENOB ~2192). Risque de
> chatter si le bruit p-p atteint 2 LSB. deadband=3 est le default conservateur.
> Ajustable live via NVS (Tool 6 futur).

---

## 5. PotFilterStore NVS — avant / apres

### 5.1 Struct

**Avant** (version 1, 12 bytes) :

```cpp
struct PotFilterStore {
    uint16_t magic;        // EEPROM_MAGIC
    uint8_t  version;      // 1
    uint8_t  snap100;      // ← supprime (EMA)
    uint8_t  actThresh10;  // ← supprime (activity detection)
    uint8_t  sleepEn;
    uint16_t sleepMs;
    uint8_t  deadband;     // default 20
    uint8_t  edgeSnap;     // default 12
    uint8_t  wakeThresh;   // default 40
};
```

**Apres** (version 2, 10 bytes) :

```cpp
struct PotFilterStore {
    uint16_t magic;        // EEPROM_MAGIC
    uint8_t  version;      // 2
    uint8_t  sleepEn;      // 0=off, 1=on (default 1)
    uint16_t sleepMs;      // 100–2000ms (default 500)
    uint8_t  deadband;     // 1–10 (default 3)
    uint8_t  edgeSnap;     // 0–10 (default 3)
    uint8_t  wakeThresh;   // 3–30 (default 8)
};
```

### 5.2 Validation

```cpp
inline void validatePotFilterStore(PotFilterStore& s) {
    if (s.sleepEn > 1)                             s.sleepEn = 1;
    if (s.sleepMs < 100 || s.sleepMs > 2000)       s.sleepMs = 500;
    if (s.deadband < 1 || s.deadband > 10)         s.deadband = 3;
    if (s.edgeSnap > 10)                           s.edgeSnap = 3;
    if (s.wakeThresh < 3 || s.wakeThresh > 30)    s.wakeThresh = 8;
}
```

### 5.3 Migration NVS

Zero-migration policy : version bump 1→2 + changement de taille (12→10 bytes).
`loadBlob()` rejette silencieusement l'ancien blob. Defaults MCP3208 appliquees au
premier boot. Warning `[NVS] illpad_pflt: version mismatch` sous `DEBUG_SERIAL`.

---

## 6. Bibliotheque MCP_ADC — API correcte

Lib : `robtillaart/MCP_ADC` (PlatformIO).

### 6.1 Erreurs dans les docs draft precedentes

| Erreur | Correction |
|--------|------------|
| Constructeur `MCP3208(MISO, MOSI, SCK)` | C'est le constructeur **software SPI** (bitbang, lent ~100 kHz). Pour hardware SPI : `MCP3208()` (default `&SPI`) |
| `mcp.analogRead(channel)` | Renomme **`mcp.read(channel)`** depuis v0.4.0 (conflit avec macro ESP32-S3) |
| SPI clock non specifie | Default lib = 1 MHz. Safe pour MCP3208 a 3.3V (max ~1.26 MHz) |
| `SPI.begin()` appele par la lib | **Non** depuis v0.5.0 — l'utilisateur doit appeler `SPI.begin()` avant `mcp.begin()` |
| Return type `uint16_t` | Return type est `int16_t` (range 0–4095 pour MCP3208) |

### 6.2 Sequence d'init correcte

```cpp
#include <SPI.h>
#include <MCP_ADC.h>

static MCP3208 s_mcp;    // hardware SPI, default &SPI

// Dans begin() :
SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);   // configure bus SPI custom pins
s_mcp.begin(CS_PIN);                                // configure CS, cree SPISettings
```

### 6.3 Lecture

```cpp
uint16_t raw = (uint16_t)s_mcp.read(channel);   // 0–4095, ~15 us @ 1 MHz
```

---

## 7. Cablage hardware

Reference complete : `docs/drafts/mcp3208-wiring.md`.

### 7.1 GPIO SPI (nouveaux)

| GPIO ESP32-S3 | Role SPI | Pin MCP3208 |
|---------------|----------|-------------|
| 18 | SCK | 13 (CLK) |
| 15 | MOSI (DIN) | 11 (DIN) |
| 16 | MISO (DOUT) | 12 (DOUT) |
| 17 | /CS | 10 (/CS) |

### 7.2 Canaux MCP3208

| Canal | Pin MCP3208 | Pot | Role firmware |
|-------|-------------|-----|---------------|
| CH0 | 1 | POT_RIGHT1 | Tempo / Division |
| CH1 | 2 | POT_RIGHT2 | Shape-Gate / Deadzone-Shuffle |
| CH2 | 3 | POT_RIGHT3 | Slew-Pattern / PB-ShufTemplate |
| CH3 | 4 | POT_RIGHT4 | Velocite base / variation |
| CH4 | 5 | POT_REAR | Luminosite LED / Sensibilite |
| CH5–7 | 6–8 | spare | Disponibles pour extensions |

### 7.3 GPIO liberes

GPIO 1, 4, 5, 6, 7 (anciennes pins ADC pot sur ADC1) deviennent libres.

### 7.4 Erreur dans le draft original

Le diagramme ASCII de `docs/drafts/mcp3208-adc-upgrade.md` place VDD sur pin 1 et
VREF sur pin 2. C'est incorrect. Le vrai pinout MCP3208 DIP-16 :
- Pin 1 = CH0, Pin 2 = CH1, ..., Pin 8 = CH7
- Pin 9 = DGND, Pin 10 = /CS, Pin 11 = DIN, Pin 12 = DOUT, Pin 13 = CLK
- Pin 14 = AGND, Pin 15 = VREF, Pin 16 = VDD

Le doc correct est `docs/drafts/mcp3208-wiring.md`.

---

## 8. Impact sur les fichiers source

### 8.1 Fichiers modifies

| Fichier | Changement |
|---------|------------|
| `platformio.ini` | +1 ligne lib_deps : `robtillaart/MCP_ADC` |
| `src/core/HardwareConfig.h` | Section pot : GPIO ADC → constantes SPI + indices canal |
| `src/core/KeyboardData.h` | PotFilterStore v2 (-2 champs), validation, version bump |
| `src/core/PotFilter.cpp` | Reecriture : suppression oversampling + EMA, ajout SPI |
| `src/core/PotFilter.h` | Suppression `getSmoothed()`, `getActivity()`, `isSleeping()` |
| `src/setup/SetupManager.cpp` | Suppression overrides snap100/actThresh10 |

### 8.2 Getters supprimes (zero consumers externes)

Verifie par grep — aucun fichier hors PotFilter.cpp/h ne les appelle :
- `getSmoothed()` — plus de champ `smoothed` dans PotData
- `getActivity()` — plus de champ `activity` dans PotData
- `isSleeping()` — debug uniquement, jamais appele

### 8.3 Fichiers NON modifies

| Fichier | Pourquoi inchange |
|---------|-------------------|
| `PotRouter.cpp/.h` | Consomme `getStable()`/`hasMoved()` — transparent |
| `SetupPotInput.cpp` | Consomme `getStable()` — transparent |
| `ToolPotMapping.cpp` | Consomme `getStable()` — transparent |
| `BatteryMonitor.cpp` | `analogRead(BAT_ADC_PIN)` GPIO10 — ADC interne, independant |
| `NvsManager.cpp` | Charge/sauve `PotFilterStore` — auto-adapte via sizeof |
| `main.cpp` | `PotFilter::begin()` gere le SPI init en interne |
| Tous les autres | API PotFilter publique inchangee |

### 8.4 Docs de reference a mettre a jour

| Doc | Section a modifier |
|-----|--------------------|
| `docs/reference/hardware-connections.md` | Section 5 (Pots), pin map ESP32, schéma ASCII, current budget |
| `docs/reference/architecture-briefing.md` | Flow "Pot → Parameter" |
| `.claude/CLAUDE.md` | Sections Hardware et "Pots — PotRouter" |

---

## 9. Setup mode (SetupManager.cpp)

Le setup mode override PotFilter pour une reponse plus rapide pendant la navigation UI.

**Avant** :
```cpp
setupCfg.snap100     = 20;   // ← champ supprime
setupCfg.actThresh10 = 80;   // ← champ supprime
setupCfg.deadband    = 10;
setupCfg.sleepEn     = 0;
```

**Apres** :
```cpp
setupCfg.deadband = 2;    // plus fin pour le setup UI (runtime default 3)
setupCfg.sleepEn  = 0;    // pas de sleep en setup
```

Les overrides snap100 et actThresh10 n'ont plus de sens sans EMA. Le deadband passe
de 10 (adapte au bruit ADC interne) a 2 (MCP3208 quasi sans bruit — resolution max
pour la navigation par pot en setup mode).

---

## 10. Timing et performance

| Metrique | Avant (ADC interne) | Apres (MCP3208) |
|----------|---------------------|-----------------|
| Lectures ADC/cycle (4 pots actifs) | 64 analogRead (4×16) | 4 SPI read |
| Temps lecture/cycle | ~1600 us | ~60 us |
| Float ops/cycle | ~20 (EMA + activity + alpha) | **0** |
| Etats machine | 3 (ACTIVE/SETTLING/SLEEPING) | 2 (ACTIVE/SLEEPING) |
| Params NVS configurables | 7 | 5 |
| Taille PotFilterStore | 12 bytes | 10 bytes |
| Positions distinctes (output) | ~205 | ~1365 |

---

## 11. Calibrage post-migration

Les defaults sont un point de depart conservateur. Ajustement possible via NVS :

| Symptome | Ajustement |
|----------|------------|
| Valeur stable oscille ±1 en continu | deadband 3 → 4 ou 5 |
| Pot se reveille trop souvent depuis sleep | wakeThresh 8 → 12 |
| Pot ne se reveille pas assez vite | wakeThresh 8 → 5 (min 3) |
| Min/max pas atteints proprement | edgeSnap 3 → 4 ou 5 |
| Resolution insuffisante sur pitch bend | deadband 3 → 2 (risque chatter) |
