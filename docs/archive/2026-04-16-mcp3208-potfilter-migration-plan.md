# Plan : Migration ADC interne → MCP3208 (PotFilter)

**Scope** : remplacer les 5 lectures ADC interne ESP32-S3 par des lectures SPI MCP3208.  
**API publique PotFilter** : inchangée (`getStable`, `hasMoved`, `getRaw`, etc.).  
**Fichiers touchés** : 4. Fichiers inchangés : tous les autres (PotRouter, BatteryMonitor, ToolPotMapping, SetupPotInput, SetupManager, NvsManager, main.cpp, etc.).

---

## Vue d'ensemble des changements

| Fichier | Type | Détail |
|---|---|---|
| `platformio.ini` | +1 ligne | Ajouter lib `robtillaart/MCP_ADC` |
| `src/core/HardwareConfig.h` | Remplacement | Constantes pot GPIO → constantes SPI + indices MCP3208 |
| `src/core/KeyboardData.h` | 1 constante | Bump `POT_FILTER_VERSION` 1 → 2 |
| `src/core/PotFilter.cpp` | Réécriture ciblée | Remplacer les lectures ADC internes par appels MCP3208 |

---

## Étape 1 — `platformio.ini`

Ajouter dans `lib_deps` (actuellement lignes 34-36) :

```ini
lib_deps =
  max22/ESP32-BLE-MIDI
  adafruit/Adafruit NeoPixel
  robtillaart/MCP_ADC          ← ajouter
```

La lib gère le bus SPI ESP32-S3 avec pins remappables.  
API : `mcp.analogRead(channel)` retourne `uint16_t` 0-4095.

---

## Étape 2 — `HardwareConfig.h`

### Supprimer (lignes 205-215)

```cpp
// --- Analog Pots (V2: 5 potentiometers — 4 right + 1 rear) ---
// ALL pots on ADC1 (GPIO 1–10) for reliable reads with BLE active.
const uint8_t POT_RIGHT1_PIN = 4;   // GPIO4  — ADC1_CH3 ...
const uint8_t POT_RIGHT2_PIN = 5;   // GPIO5  — ADC1_CH4 ...
const uint8_t POT_RIGHT3_PIN = 6;   // GPIO6  — ADC1_CH5 ...
const uint8_t POT_RIGHT4_PIN = 7;   // GPIO7  — ADC1_CH6 ...
const uint8_t POT_REAR_PIN   = 1;   // GPIO1  — ADC1_CH0 ...
const uint8_t NUM_POTS = 5;
const uint8_t POT_PINS[NUM_POTS] = {
    POT_RIGHT1_PIN, POT_RIGHT2_PIN, POT_RIGHT3_PIN, POT_RIGHT4_PIN, POT_REAR_PIN
};
```

> `POT_PINS[]` n'est référencé que dans `PotFilter.cpp` (confirmé par grep). Il peut être supprimé sans impact sur les autres fichiers.  
> `PotRouter.cpp` ligne 7 a un commentaire mentionnant `POT_PINS` mais aucune utilisation — le commentaire devient obsolète, à nettoyer si souhaité.

### Remplacer par

```cpp
// --- MCP3208 SPI ADC — 5 pots (channels 0-4) ---
// GPIO 4/5/6/7/1 freed (formerly ADC1 pot reads).
// GPIO 14/15/16/17 are free on ESP32-S3-N8R16 (not USB, not I2C, not flash/PSRAM).
const uint8_t MCP3208_SCK_PIN  = 14;  // SPI clock
const uint8_t MCP3208_MISO_PIN = 16;  // DOUT (ADC → MCU)
const uint8_t MCP3208_MOSI_PIN = 15;  // DIN  (MCU → ADC)
const uint8_t MCP3208_CS_PIN   = 17;  // /CS  (active LOW)

// MCP3208 channel indices (match physical wiring, see docs/drafts/mcp3208-wiring.md)
const uint8_t NUM_POTS = 5;
const uint8_t POT_CH_RIGHT1 = 0;  // CH0 — Tempo / Division
const uint8_t POT_CH_RIGHT2 = 1;  // CH1 — Shape-Gate / Deadzone-Shuffle
const uint8_t POT_CH_RIGHT3 = 2;  // CH2 — Slew-Pattern / PB-ShufTemplate
const uint8_t POT_CH_RIGHT4 = 3;  // CH3 — Vélocité base / variation
const uint8_t POT_CH_REAR   = 4;  // CH4 — Luminosité LED / Sensibilité
```

> Les constantes `POT_CH_*` sont documentaires — `PotFilter.cpp` utilise directement l'index `i` (0-4) dans la boucle.

---

## Étape 3 — `KeyboardData.h`

Bumper la version NVS pour forcer un reset des defaults au premier boot avec le nouveau firmware :

```cpp
// Avant :
const uint8_t POT_FILTER_VERSION = 1;

// Après :
const uint8_t POT_FILTER_VERSION = 2;
```

**Pourquoi** : les valeurs par défaut de `applyDefaults()` changent (deadband 20 → 3, etc.). Sans bump de version, l'ancien NVS `illpad_pflt` se charge et applique des seuils calibrés pour l'ADC interne au lieu du MCP3208 — le filtre sera soit trop lent (deadband trop grand), soit instable (wakeThresh trop bas).  
Conformément à la politique zéro-migration du projet, l'ancien blob NVS est silencieusement ignoré et remplacé par les nouvelles defaults.

---

## Étape 4 — `PotFilter.cpp`

C'est le seul fichier avec de la vraie logique à modifier. Les changements sont ciblés sur 5 zones.

### 4a. Includes + objet statique MCP3208 (haut du fichier)

Ajouter après les includes existants (`Arduino.h`, `cmath`) :

```cpp
#include <SPI.h>
#include <MCP_ADC.h>

// MCP3208 static instance (hardware SPI, pins from HardwareConfig.h)
static MCP3208 s_mcp(MCP3208_MISO_PIN, MCP3208_MOSI_PIN, MCP3208_SCK_PIN);
```

### 4b. Constante `OVERSAMPLE_SHIFT` (ligne 39)

```cpp
// Avant :
static const uint8_t  OVERSAMPLE_SHIFT  = 4;    // 16× — measured NOISE 12-16 LSB ...

// Après :
static const uint8_t  OVERSAMPLE_SHIFT  = 0;    // 1× — MCP3208 noise ~1-2 LSB, oversampling inutile
```

**Calcul de timing** : à OVERSAMPLE_SHIFT=4, 16 lectures SPI × ~15µs = 240µs/pot × 5 pots = ~1.2ms de lectures seules par cycle. À OVERSAMPLE_SHIFT=0 : ~15µs × 5 = 75µs. Le MCP3208 n'ayant que 1-2 LSB de bruit, le gain en SNR de l'oversampling est nul (on moyennerait des valeurs identiques).

### 4c. Fonction `oversampleRead()` (lignes 59-66)

```cpp
// Avant :
static uint16_t oversampleRead(uint8_t pin) {
    const uint8_t count = 1 << OVERSAMPLE_SHIFT;
    uint32_t sum = 0;
    for (uint8_t j = 0; j < count; j++) {
        sum += analogRead(pin);              // ← ADC interne ESP32
    }
    return (uint16_t)(sum >> OVERSAMPLE_SHIFT);
}

// Après :
static uint16_t oversampleRead(uint8_t channel) {
    const uint8_t count = 1 << OVERSAMPLE_SHIFT;  // = 1 avec shift=0
    uint32_t sum = 0;
    for (uint8_t j = 0; j < count; j++) {
        sum += s_mcp.analogRead(channel);   // ← MCP3208 SPI
    }
    return (uint16_t)(sum >> OVERSAMPLE_SHIFT);
}
```

> Avec OVERSAMPLE_SHIFT=0 la boucle fait exactement 1 itération et `sum >> 0` est un no-op. La structure de la fonction reste intacte pour rester cohérente avec le commentaire OVERSAMPLE_SHIFT et permettre de remonter à shift=1/2 si jamais nécessaire.

### 4d. Fonction `applyDefaults()` (lignes 47-57)

Mettre à jour les seuils calibrés pour l'ADC interne → seuils MCP3208 :

```cpp
// Avant (calibrés pour ADC interne + 16× oversampling, ~12-16 LSB bruit) :
s_cfg.actThresh10 = 200;   // 20.0 activity threshold
s_cfg.deadband    = 20;    // 20 ADC units
s_cfg.edgeSnap    = 12;    // 12 ADC units from edges
s_cfg.wakeThresh  = 40;    // 40 ADC units to wake from sleep

// Après (calibrés pour MCP3208, ~1-2 LSB bruit) :
s_cfg.actThresh10 = 40;    // 4.0 activity threshold
s_cfg.deadband    = 3;     // 3 ADC units (~2048 positions distinctes sur 4096)
s_cfg.edgeSnap    = 4;     // 4 ADC units from edges
s_cfg.wakeThresh  = 8;     // 8 ADC units to wake from sleep
```

> Ces valeurs sont des points de départ. `deadband=3` donne ~1365 positions utiles sur 4096
> (vs 205 avec deadband=20). À ajuster en pratique si le pot oscille encore.

Mettre à jour aussi le commentaire au-dessus de `applyDefaults()` :

```cpp
// Avant :
// ESP32-S3 ADC noise with hardware RC (220Ω+470nF): ~150 LSB raw, ~12-16 LSB after 16×.
// BLE impact: +1-2 LSB (negligible). Defaults tuned for 16× oversampling.

// Après :
// MCP3208 noise with hardware RC (220Ω+470nF): ~1-2 LSB.
// BLE has no impact (SPI, not ADC). Defaults tuned for 1× read, shift=0.
```

### 4e. Fonction `begin()` (lignes 74-104)

```cpp
// Avant :
void PotFilter::begin() {
    static bool s_begun = false;
    if (s_begun) return;
    s_begun = true;

    applyDefaults();
    s_rearCounter = 0;

    for (uint8_t i = 0; i < NUM_POTS; i++) {
        pinMode(POT_PINS[i], INPUT);           // ← GPIO ADC interne

        uint16_t initial;
        if (i < 4) {
            initial = oversampleRead(POT_PINS[i]);  // ← GPIO pin
        } else {
            initial = analogRead(POT_PINS[i]);      // ← GPIO pin, rear sans oversampling
        }
        ...
    }
}

// Après :
void PotFilter::begin() {
    static bool s_begun = false;
    if (s_begun) return;
    s_begun = true;

    applyDefaults();
    s_rearCounter = 0;

    // Init SPI bus + MCP3208
    SPI.begin(MCP3208_SCK_PIN, MCP3208_MISO_PIN, MCP3208_MOSI_PIN, MCP3208_CS_PIN);
    s_mcp.begin(MCP3208_CS_PIN);

    for (uint8_t i = 0; i < NUM_POTS; i++) {
        // Pas de pinMode() nécessaire — SPI, pas GPIO ADC

        uint16_t initial = oversampleRead(i);  // channel index, même traitement pour tous
        ...
    }
}
```

> Le `if (i < 4)` qui différenciait le rear pot (1 lecture vs 16) disparaît :
> avec OVERSAMPLE_SHIFT=0, tous les pots font 1 lecture. Aucune raison de les différencier.

### 4f. Fonction `updateAll()` — 3 occurrences (lignes 121, 141, 143)

```cpp
// Ligne 121 (SLEEPING peek) :
// Avant :
p.raw = oversampleRead(POT_PINS[i]);
// Après :
p.raw = oversampleRead(i);

// Lignes 140-144 (ACTIVE/SETTLING read) :
// Avant :
if (i < 4) {
    p.raw = oversampleRead(POT_PINS[i]);
} else {
    p.raw = analogRead(POT_PINS[i]);
}
// Après :
p.raw = oversampleRead(i);   // tous les pots identiques, rear différencié par REAR_DIVISOR uniquement
```

> `REAR_DIVISOR=20` et le `if (i == 4)` au début de la boucle restent inchangés :
> le rear pot est toujours lu à ~50 Hz indépendamment de la source ADC.

---

## Ce qui ne change PAS

| Fichier | Pourquoi inchangé |
|---|---|
| `PotFilter.h` | API publique strictement identique |
| `main.cpp` | `PotFilter::begin()` fait le SPI init en interne — aucun appel SPI externe nécessaire |
| `BatteryMonitor.cpp` | `analogReadResolution(12)` + `analogRead(BAT_ADC_PIN)` sur GPIO10 restent valides — batterie reste sur ADC interne |
| `PotRouter.cpp/.h` | Consomme `getStable()`/`hasMoved()` — transparent |
| `SetupPotInput.cpp` | Consomme `getStable()` — transparent |
| `ToolPotMapping.cpp` | Consomme `getStable()` — transparent |
| `SetupManager.cpp` | Utilise `getConfig()`/`setConfig()` — transparent |
| `NvsManager.cpp` | Charge/sauve `PotFilterStore` — blob format inchangé, version 2 force reset des defaults |
| Tous les autres Tools | `PotFilter::updateAll()` appelé de partout — résultat identique côté sortie |

---

## Ordre d'exécution recommandé

1. `platformio.ini` — ajouter la lib
2. `KeyboardData.h` — bump version (faire en même temps que PotFilter pour cohérence)
3. `HardwareConfig.h` — remplacer les constantes pot
4. `PotFilter.cpp` — 4e-4f d'abord (lectures), puis 4a-4d (init + seuils)
5. Build — vérifier 0 erreur, 0 warning sur les anciennes références `POT_PINS`
6. Upload + monitor — vérifier `[INIT] PotFilter + PotRouter OK.` au boot

---

## Points d'attention au premier boot

- Le NVS `illpad_pflt` contient `version=1` (ancien). `loadBlob()` rejette le blob (mauvaise version) et applique les nouvelles defaults (`deadband=3`, etc.). Un `Serial.printf` de warning est émis sous `DEBUG_SERIAL`.
- `analogReadResolution(12)` dans `BatteryMonitor::begin()` configure l'ADC interne pour GPIO10. Cet appel reste valide et nécessaire — il n'interfère pas avec SPI.
- Pas de `SPI.end()` nécessaire — le bus reste ouvert pour toute la durée du firmware.

---

## Calibrage pratique post-migration (optionnel)

Les valeurs de `applyDefaults()` sont un point de départ. Si oscillation résiduelle :

| Symptôme | Ajustement |
|---|---|
| Stable oscille de 1 unité en continu | deadband 3 → 4 ou 5 |
| Pot se réveille trop souvent depuis sleep | wakeThresh 8 → 12 |
| Réponse trop lente au mouvement | actThresh10 40 → 25-30 |

Ces paramètres sont configurables en live via le Monitor de Tool 5 sans recompiler.  
Une fois satisfaisant, sauver en NVS via Tool 5 pour persistence.
