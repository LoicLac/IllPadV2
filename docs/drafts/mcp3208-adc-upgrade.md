# Plan : MCP3208 — ADC externe pour les 5 pots

## Contexte

L'ADC interne de l'ESP32-S3 a ~8-9 ENOB (bruit ~150 LSB raw, ~12-16 LSB apres 16x oversampling).
Le MCP3208 a ~11.5 ENOB (bruit ~1-2 LSB). Remplacer l'ADC interne par un MCP3208 permettrait
de reduire le deadband de 20 a 2-3 et d'obtenir ~2000 positions distinctes au lieu de 205.

Impact principal : le pitch bend (14-bit, 16384 valeurs) passerait de ~205 positions utiles
a ~2000+ — eliminerait l'effet staircase audible.

## Chip

**MCP3208-CI/P** (DIP-16) — ~2.39 EUR chez Mouser/DigiKey/TME.
- 12-bit, 8 canaux single-ended (5 utilises, 3 spare)
- SPI, 100 kS/s max
- Alim 2.7-5.5V (utiliser 3.3V)
- Grade CI = commercial/industrial, suffisant pour des pots

Acheter chez un distributeur officiel Microchip (Mouser, DigiKey, TME) — pas AliExpress
(risque de contrefacon, surtout sur un ADC ou la precision est le but).

## Schema

### Version legere (3 composants)

```
                    MCP3208-CI/P
                 ┌────────────────┐
3.3V ──┬── VDD  │1             16│ DGND ── GND
       │        │                │
       ├── VREF │2             15│ CLK  ── ESP32 SCK
       │        │                │
 POT0 ─── CH0  │3             14│ DOUT ── ESP32 MISO
 POT1 ─── CH1  │4             13│ DIN  ── ESP32 MOSI
 POT2 ─── CH2  │5             12│ CS   ── ESP32 GPIO xx
 POT3 ─── CH3  │6             11│ CH7
 POT4 ─── CH4  │7             10│ CH6
       │        │                │
GND ──┴── AGND │8              9│ CH5
                 └────────────────┘

C1 = 100nF ceramique   VDD (pin 1) → GND (pin 8)
C2 = 100nF ceramique   VREF (pin 2) → GND (pin 8)
```

### Version blindee (+10 composants)

Ajouter par canal pot :
- R = 100 ohm serie (entre pot wiper et CHx)
- C = 100nF ceramique (entre CHx et GND, apres R)

Ajouter sur VREF :
- C3 = 10uF tantale en parallele de C2 (stabilise la reference BF)

Ajouter sur SPI (optionnel, si traces longues) :
- R = 33 ohm serie sur CLK, DIN, CS (amortit les reflexions)

## Integration firmware

### PotFilter.cpp — changement minimal

Remplacer `analogRead()` et `oversampleRead()` par des lectures SPI MCP3208.
L'API publique (`getStable`, `hasMoved`, etc.) ne change pas.

```cpp
// Avant (ADC interne) :
p.raw = oversampleRead(POT_PINS[i]);   // 16x analogRead

// Apres (MCP3208) :
p.raw = mcp3208Read(i);                // 1 lecture SPI, ~15us, bruit ~1-2 LSB
```

Avec un bruit de 1-2 LSB, l'oversampling n'est plus necessaire. Une seule lecture
SPI par pot par cycle suffit. Le filtre adaptatif reste utile pour le sleep/wake
mais le deadband peut descendre a 2-3.

### Pins SPI

L'ESP32-S3 a du SPI remappable. Choisir des GPIO qui ne sont pas :
- GPIO 19/20 (USB natif)
- GPIO 8/9 (I2C MPR121)
- GPIO 4/5/6/7/1 (anciennes pins ADC pot — liberes par le MCP3208)

Le bus SPI du MCP3208 est independant du bus I2C des MPR121.

### Bibliotheque

`MCP_ADC` par RobTillaart (PlatformIO: `robtillaart/MCP_ADC`).
API simple : `mcp.analogRead(channel)` retourne 0-4095.
https://github.com/RobTillaart/MCP_ADC

### Defaults PotFilter avec MCP3208

| Param | Valeur actuelle (ADC interne) | Valeur avec MCP3208 |
|-------|-------------------------------|---------------------|
| OVERSAMPLE_SHIFT | 4 (16 reads) | 0 (1 read suffit) |
| DEADBAND | 20 | 2-3 |
| SENSE (actThresh) | 20.0 | 3.0-5.0 |
| WAKE | 40 | 5-10 |

### Resolution effective

| Param | Positions (ADC interne, db=20) | Positions (MCP3208, db=3) |
|-------|-------------------------------|--------------------------|
| Tempo (251 valeurs) | 205 | 251 (toutes) |
| MIDI CC (128) | 128 | 128 (toutes) |
| Base Velocity (127) | 127 | 127 (toutes) |
| Pitch Bend (16384) | 205 | 1365 |
| Shape/Gate/Depth (4096) | 205 | 1365 |
| Division (9) | 9 | 9 |

## Statut

Non implemente. Necessite la fabrication d'une nouvelle board pot avec le MCP3208
integre. Le firmware actuel (PotFilter avec ADC interne + 16x oversampling) est
fonctionnel en attendant.
