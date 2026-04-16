# MCP3208 — Cablage ILLPAD V2

> **Chip** : MCP3208-CI/P (DIP-16, 12-bit, 8 canaux, SPI)
> **Alim** : 3.3V (rail 3V3 de l'ESP32-S3)
> **5 pots** connectes sur CH0-CH4, 3 canaux spare

---

## 1. Pinout MCP3208 (vue de dessus, encoche a gauche)

```
                    encoche
                  ┌────╨────┐
  POT_RIGHT1  CH0│ 1    16 │VDD  ─── 3.3V
  POT_RIGHT2  CH1│ 2    15 │VREF ─── 3.3V
  POT_RIGHT3  CH2│ 3    14 │AGND ─── GND
  POT_RIGHT4  CH3│ 4    13 │CLK  ─── GPIO 18
    POT_REAR  CH4│ 5    12 │DOUT ─── GPIO 16
        spare CH5│ 6    11 │DIN  ─── GPIO 15
        spare CH6│ 7    10 │/CS  ─── GPIO 17
        spare CH7│ 8     9 │DGND ─── GND
                  └─────────┘
```

Gauche (1-8) : entrees analogiques, de haut en bas.
Droite (16-9) : alim + SPI, de haut en bas.

---

## 2. Connexions

### SPI

| Pin | Nom  | GPIO ESP32 |
|-----|------|------------|
| 13  | CLK  | 18         |
| 12  | DOUT | 16         |
| 11  | DIN  | 15         |
| 10  | /CS  | 17         |

### Alimentation

| Pin | Nom  | Vers  |
|-----|------|-------|
| 16  | VDD  | 3.3V  |
| 15  | VREF | 3.3V  |
| 14  | AGND | GND   |
| 9   | DGND | GND   |

AGND et DGND au meme point GND.

### Canaux

| Pin | Canal | Pot        |
|-----|-------|------------|
| 1   | CH0   | POT_RIGHT1 |
| 2   | CH1   | POT_RIGHT2 |
| 3   | CH2   | POT_RIGHT3 |
| 4   | CH3   | POT_RIGHT4 |
| 5   | CH4   | POT_REAR   |
| 6-8 | CH5-7 | spare      |

---

## 3. Potentiometres

10 kohm lineaire, 3 broches :

```
3.3V ─── broche gauche (CCW)
 GND ─── broche droite (CW)
 CHx ─── broche centrale (Wiper)
```

Si le sens de rotation est inverse : echanger CCW↔CW au cablage.

---

## 4. Condensateurs

### Obligatoires

```
VDD  (pin 16) ──[100nF ceramique]── GND     au plus pres du chip
VREF (pin 15) ──[100nF ceramique]── GND     au plus pres du chip
```

### Filtre RC par canal (recommande)

Entre le wiper du pot et l'entree CHx :

```
Wiper ──[100 ohm]──┬── CHx
                    │
               [100nF]
                    │
                   GND
```

Passe-bas a 16 kHz. Bloque les parasites RF. Ramene l'impedance source a ~100 ohm
(le MCP3208 demande < 1 kohm a 1 MHz pour pleine precision).

### VREF (optionnel)

```
VREF (pin 15) ──[10uF tantale]── GND     en parallele du 100nF
```

Stabilise la reference si bruit basse frequence.

---

## 5. GPIO ESP32-S3

### Utilises par le MCP3208

| GPIO | Role  |
|------|-------|
| 18   | SCK   |
| 15   | MOSI  |
| 16   | MISO  |
| 17   | /CS   |

### Liberes (anciens ADC pots)

| GPIO | Etait      |
|------|------------|
| 1    | POT_REAR   |
| 4    | POT_RIGHT1 |
| 5    | POT_RIGHT2 |
| 6    | POT_RIGHT3 |
| 7    | POT_RIGHT4 |

---

## 6. Schema complet

```
                       ESP32-S3
                  ┌────────────────┐
        3.3V ────┤ 3V3            │
         GND ────┤ GND            │
 GPIO 18 (SCK) ──┤                │
GPIO 15 (MOSI) ──┤                │
GPIO 16 (MISO) ──┤                │
  GPIO 17 (CS) ──┤                │
                  └──┬──┬──┬──┬───┘
                     │  │  │  │
                     │  │  │  │         MCP3208 (vue de dessus)
                     │  │  │  │           encoche
                     │  │  │  │         ┌────╨────┐
         POT_R1 ─[RC]─ CH0│ 1    16 │VDD ── 3.3V
         POT_R2 ─[RC]─ CH1│ 2    15 │VREF ─ 3.3V
         POT_R3 ─[RC]─ CH2│ 3    14 │AGND ─ GND
         POT_R4 ─[RC]─ CH3│ 4    13 │CLK ──┘
        POT_REAR─[RC]─ CH4│ 5    12 │DOUT ─┘
              spare CH5│ 6    11 │DIN ──┘
              spare CH6│ 7    10 │/CS ──┘
              spare CH7│ 8     9 │DGND ─ GND
                        └─────────┘

  [RC] = 100 ohm + 100nF vers GND (recommande)

  Chaque pot : 3.3V ── CCW | Wiper → CHx | CW ── GND
```

---

## 7. Consommation

| Composant | Courant typ |
|-----------|-------------|
| MCP3208   | ~175 uA     |

Negligeable vs ESP32 (~150 mA). Pas d'impact sur le budget courant.
