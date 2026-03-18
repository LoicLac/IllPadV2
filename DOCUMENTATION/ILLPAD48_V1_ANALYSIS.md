# ILLPAD48 V1 — Analyse Complète du Code et des Features

> Document de référence pour la transition vers la V2.
> Dernière mise à jour : 18 mars 2026.

---

## 1. Description Physique de l'Instrument

### L'Objet

L'ILLPAD est un **bloc de bois massif** de **30 cm × 30 cm × 5 cm** dans lequel sont incrustées des **pièces d'aluminium de 4 mm d'épaisseur** formant les 48 pads capacitifs. L'arrangement des pads est **circulaire/radial** (voir `ILL-PAD-1_9_Plan_de_travail_1.png`), pas une grille rectangulaire.

> **Nature du capteur capacitif** : Les pads détectent la **surface de contact humide** (doigt) sur le métal, pas une force mécanique. La "pression" est continue (bon pour l'aftertouch) mais trop variable au moment du contact pour une velocity fiable.

### Layout Physique V2

```
              [ARRIÈRE — pot 3 + bouton battery/setup + USB-C]
              
         ┌─────────────────────────────────┐
         │                                 │
  [G]    │                                 │    [D]
  pot 1  │          48 PADS                │  pot 2
         │       (circulaire/radial)       │
  [G]    │                                 │    [D]
  btn    │                                 │  btn
         │                                 │
         └─────────────────────────────────┘
```

### Contrôles Physiques V2

| Contrôle | Position | GPIO | Fonction |
|---|---|---|---|
| **Bouton gauche** | Côté gauche, 2/3 hauteur | À définir | BANK select (hold + pad) |
| **Bouton droit** | Côté droit, 2/3 hauteur | À définir | Scale/arp controls (hold + pad) |
| **Bouton arrière** | Face arrière | À définir | Battery gauge + Setup mode entry |
| **Pot gauche** | Côté gauche | À définir (ADC) | Live — feel/son (contextuel NORMAL/ARPEG) |
| **Pot droit** | Côté droit | À définir (ADC) | Live — notes/rythme (contextuel NORMAL/ARPEG) |
| **Pot arrière** | Face arrière | À définir (ADC) | Config — tempo interne, etc. |
| **USB-C** | Face arrière | GPIO 19/20 | Charge + USB MIDI + Serial debug |

### Ergonomie des Combos (deux mains)

| Combo | Difficulté | Usage |
|---|---|---|
| Main gauche pot G + main droite joue | Facile | **Live** |
| Main droite pot D + main gauche joue | Facile | **Live** |
| Main gauche btn G + main droite pot D | Facile | **Live** (cross) |
| Main droite btn D + main gauche pot G | Facile | **Live** (cross) |
| Même côté btn + pot | Difficile | **Non-live** seulement |
| Pot arrière | Deux mains libres | **Config** (avant de jouer) |

### Différences Hardware V1 → V2

| Aspect | V1 | V2 |
|---|---|---|
| Boutons | 2 (bank GPIO2 + battery GPIO3) | 3 (gauche + droit + arrière) |
| Potentiomètres | 1 (GPIO1) | 3 (gauche + droit + arrière) |
| GPIOs | GPIO1 (pot), GPIO2 (btn), GPIO3 (btn) | 6 nouveaux GPIOs à définir |

---

## 2. Hardware Électronique (inchangé)

| Composant | Détails |
|---|---|
| MCU | ESP32-S3-N8R16 (8MB QIO flash, 16MB OPI PSRAM) |
| Sensors | 4× MPR121 capacitif I2C (0x5A–0x5D) → 48 pads |
| MIDI | USB MIDI (TinyUSB) + BLE MIDI simultanés |
| USB | Single USB-C GPIO 19/20 (native USB) |
| LEDs | 8× standard (cercle) + 1 RGB onboard (GPIO48) |
| Batterie | LiPo 3.7V, BQ25185 charger, ADC divider |

---

## 3. Architecture Software V1

- **Core 0** — `sensingTask` : I2C polling 4× MPR121, pressure pipeline, shared state (mutex).
- **Core 1** — `loop()` : edge detection, MIDI, boutons, pot, LEDs, NVS.
- Mutex timeout 10ms.

---

## 4. Features V1

48 pads polyphoniques, poly-aftertouch 0-127 (~40Hz/pad), 8 banks = 8 canaux MIDI, note mapping persisté NVS, response shaping + slew rate via pot, dual output USB+BLE, velocity fixe 127.

### Pressure Pipeline (10 étapes)

Poll → Delta → Note On/Off (seuils adaptatifs, hystérésis) → Relative Zero → Deadzone → Normalisation → Response Shaping → Slew Limiter → Moving Average → Sortie 0-127.

### Setup Mode V1 (4 tools)

1. Pressure Calibration — touch-to-measure
2. Note Mapping — base note + touch-to-assign chromatique
3. Bank Pad Assignment — touch 8 pads
4. Settings — profile, sensitivity, AT rate, deadzone

---

## 5. Limitations V1

1. main.cpp monolithique (500+ lignes, 15+ static globals)
2. Un seul mode de jeu (chromatique poly)
3. Scale mode jamais implémenté
4. Un seul bouton latéral, un seul pot
5. Velocity fixe 127
6. All notes off au bank switch (rien en arrière-plan)
7. Pas de réception MIDI clock
8. Mutex = point de couplage Core 0 / Core 1
9. NVS writes bloquantes dans le loop principal
10. Tool 2 mélange "ordre des pads" et "note MIDI"
