# ILLPAD48 V2 — Nouvelles Idées & Features

> Scale system + arpégiateur multi-bank + 5 pots contextuels + bouton unique.
> Dernière mise à jour : 20 mars 2026.

---

## A. Banks Toujours Actives

### Paradigme

Les 8 banks sont **toujours vivantes**. Le bank select change quelle bank reçoit les pads. Les ArpEngines en HOLD continuent en arrière-plan.

### Types de Banks

| Type | Comportement |
|---|---|
| **NORMAL** | Pads → notes + poly-aftertouch. Ne joue qu'au premier plan. Velocity = base ± variation (per-bank). Pitch bend offset per-bank. |
| **ARPEG** | Arpégiateur. Deux sous-modes : live (non-HOLD) et persistant (HOLD). Pas d'aftertouch. Velocity = base ± variation (per-bank). |

Max 4 banks ARPEG. Configurable dans Tool 4 (Bank Config).

### Bank Switch

Le BankManager ne gère **aucune logique d'arrêt d'arpège**. Les arpèges vivent/meurent par leur propre logique.

| Situation | Au switch |
|---|---|
| Quitter NORMAL | All notes off + arrêt pitch bend |
| Quitter ARPEG (HOLD off) | Rien — déjà mort (doigts levés) |
| Quitter ARPEG (HOLD on) | Rien — continue tout seul |
| Revenir sur ARPEG HOLD qui tourne | Pads ajoutent des notes, double-tap retire |
| Arriver sur NORMAL | Envoi du pitch bend offset stocké de la bank |

---

## B. Les 3 Couches de Mapping

### Couche 1 : Pad Ordering (physique → logique)

`padOrder[48]` — rang 0 à 47, grave à aigu. Fait une fois, toutes les banks. Tool 2.

### Couche 2 : Scale Config (par bank, runtime)

- **Chromatique** : `rootBase + padOrder[i]`
- **Gamme** : `rootBase + octave*12 + scaleIntervals[mode][degree]`
- **Root = base note partout.** Changer la root change la tonalité en gamme ET le point de départ en chromatique.
- Changeable à chaud via bouton gauche (hold + scale pads). Persisté NVS par bank.

### Couche 3 : Control Pad Assignment

25 pads assignés à des fonctions. Un pad = un seul rôle. Tool 3.

---

## C. Bouton Gauche — Single Layer Control

### Principe

Un seul bouton pour toutes les fonctions de contrôle (bank + scale + arp). Toutes les fonctions sont accessibles sur **un seul layer** pendant le hold. Remplace les deux boutons (gauche bank + droit scale) de l'ancienne conception.

### Pads Communs (toutes banks) — 23

| Zone | Pads | Fonction |
|---|---|---|
| Bank | 8 | Bank select (1-8) |
| Root | 7 | A, B, C, D, E, F, G |
| Mode | 7 | Ionian → Locrian |
| Chromatique | 1 | Toggle chromatique |

### Pads Arp (bank ARPEG seulement) — 1 pendant le hold

| Pad | Fonction |
|---|---|
| HOLD toggle | On/off |

Les anciens pads arp (5 patterns, 1 octave) sont supprimés — ces fonctions passent sur les pots droits.

### Pad Play/Stop — 1 pad spécial

| Pad | Fonction |
|---|---|
| Play/Stop | Toggle transport de l'arpège |

**Comportement contextuel** :
- Sur bank NORMAL → ce pad joue une note normalement.
- Sur bank ARPEG, HOLD OFF → ce pad joue une note (entre dans l'arpège comme les autres).
- Sur bank ARPEG, HOLD ON → ce pad = **play/stop toggle** (pas de note). Stop puis Play repart du début.

C'est le **seul pad de contrôle actif en mode jeu** (les 24 autres ne sont actifs que pendant le hold).

**Total pads de contrôle** : 8 bank + 15 scale + 2 arp (1 hold + 1 play/stop) = **25 pads**. 23 libres pendant les holds.

### Séquençage pendant un hold

Pendant un seul hold, on peut enchaîner :
1. Switch de bank 4 → configurer sa scale → switch de bank 5 → configurer sa scale → revenir sur bank 1 → release.
2. Les arps en background reflètent les changements de scale **au tick suivant** (résolution live, pas d'interruption).

### Protection notes fantômes

- Release bouton : snapshot `_lastKeys`
- Changement scale sur NORMAL : `allNotesOff()` avant
- Changement scale sur ARPEG : **pas de allNotesOff** — résolution live au tick

---

## D. Arpégiateur

### Deux Sous-Modes (déterminés par HOLD)

#### HOLD OFF — Mode Live

| Action | Effet |
|---|---|
| Presser un pad | Position ajoutée à la pile |
| Lever un doigt | Position retirée |
| Tous les doigts levés | Liste vide → arpège s'arrête |
| Pad play/stop | Joue une note (pas de rôle spécial) |
| Double-tap | Pas de sens (lever le doigt suffit) |

#### HOLD ON — Mode Persistant

| Action | Effet |
|---|---|
| Presser un pad | Position ajoutée (persiste) |
| Double-tap (< 300ms) | Position retirée |
| Pad play/stop | Toggle play/stop. Play repart du début. |
| Lever les doigts | Rien — positions restent |
| Bank switch | Arpège continue en background |

### Pile de Positions (nouveau V2)

La pile stocke des **padOrder index** (positions 0-47), pas des notes MIDI. La résolution en note MIDI se fait **au moment du tick** via la ScaleConfig courante de la bank.

**Conséquence** : changer la scale/root d'une bank ARPEG en background change immédiatement les notes jouées. Les arpèges ne s'interrompent jamais lors d'un changement de scale.

Jusqu'à **48 positions** (tous les pads). Avec 4 octaves = 192 steps max.

### 5 Patterns (via pot droit 3)

| Pattern | Comportement |
|---|---|
| Up | Grave → aigu, boucle |
| Down | Aigu → grave, boucle |
| Up-Down | Ping-pong (sans répéter extrêmes) |
| Random | Aléatoire parmi la pile |
| Order | Ordre chronologique d'ajout |

Sélection par pot droit 3 (5 positions discrètes). Pas de pads pattern.

> Note : garder des stubs dans le code pour ajout possible de patterns supplémentaires.

### Octave Range : 1-4 (via hold + pot droit 3)

### Division Rythmique : hold + pot droit 1 (9 valeurs binaires, 4/1 → 1/64)

### Velocity : base ± variation per-bank (pot droit 4 / hold + pot droit 4)

---

## E. Les 5 Potentiomètres

### Principe

4 pots à droite + 1 pot à l'arrière. Chaque pot droit a 2 slots : seul et hold bouton gauche. Les fonctions changent automatiquement selon le type de bank (NORMAL vs ARPEG). Système **catch** sur tous les pots.

### Boutons Modifier

| Bouton | Modifie |
|--------|---------|
| Gauche | 4 pots droits (2ème slot) |
| Arrière | Pot arrière uniquement (2ème slot) |

Le bouton gauche n'affecte jamais le pot arrière. Le bouton arrière n'affecte jamais les pots droits.

### Pot Droit 1 — Tempo / Division

| Contexte | Pot seul | Hold gauche + pot |
|---|---|---|
| **NORMAL** | Tempo (10-260 BPM) | — vide — |
| **ARPEG** | Tempo (10-260 BPM) | Division (9 valeurs binaires) |

Tempo = global, partagé par tous les modes.

### Pot Droit 2 — Shape/Gate / Deadzone/Swing

| Contexte | Pot seul | Hold gauche + pot |
|---|---|---|
| **NORMAL** | Response shape (courbe AT) | AT deadzone |
| **ARPEG** | Gate length (staccato↔legato) | Swing amount |

### Pot Droit 3 — Slew/Pattern / PitchBend/Octave

| Contexte | Pot seul | Hold gauche + pot |
|---|---|---|
| **NORMAL** | Slew rate (smoothing AT) | Pitch bend (offset per-bank) |
| **ARPEG** | Pattern (5 positions) | Octave range (1-4) |

### Pot Droit 4 — Velocity (identique NORMAL et ARPEG)

| Contexte | Pot seul | Hold gauche + pot |
|---|---|---|
| **ANY** | Base velocity (1-127) | Velocity variation (0-100%) |

### Pot Arrière — Config

| Combo | Fonction |
|---|---|
| Pot seul | LED brightness (0-255) |
| Hold arrière + pot | Pad sensitivity (5-30%) |

### Divisions Rythmiques (hold + pot droit 1, bank ARPEG)

```
Position    Division
0%          4/1 (quadruple ronde)
~12%        2/1 (double ronde)
~25%        1/1 (ronde)
~37%        1/2 (blanche)
~50%        1/4 (noire)
~62%        1/8 (croche)
~75%        1/16 (double croche)
~87%        1/32 (triple croche)
100%        1/64
```

Pas de triolets, pas de pointées. 9 valeurs binaires uniquement.

### Velocity Variation (hold + pot droit 4, toutes banks)

Le pot contrôle le **pourcentage de variation aléatoire** autour de la base velocity :
- 0% → velocity fixe (ex: toujours 80)
- 50% → variation ±40 autour de 80 (range 40-120)
- 100% → variation ±63 (range 17-143, clampé 1-127)

La **base velocity** est réglée via pot droit 4 seul.

### Stockage des Valeurs Pot

- **Tempo** : global.
- **Response shape, slew rate, AT deadzone** : globaux (partagés par toutes les banks NORMAL).
- **Gate length, swing, division, pattern, octave** : per-bank ARPEG.
- **Base velocity, velocity variation** : per-bank (NORMAL + ARPEG).
- **Pitch bend offset** : per-bank NORMAL.
- **LED brightness, pad sensitivity** : globaux.
- Au bank switch → catch reset pour les params per-bank.

### Catch System

Le pot physique doit passer par la valeur stockée avant de prendre le contrôle. Bargraph LED (0-8 LEDs) affiche la valeur cible pendant le catch.

---

## F. Tempo / MIDI Clock

### Sources (par priorité)

1. **USB MIDI Clock** — prioritaire si disponible (jitter ±0.5ms)
2. **BLE MIDI Clock** — si pas d'USB (jitter ±7-15ms)
3. **Dernier tempo reçu** — si le clock externe s'arrête
4. **Tempo interne** — pot droit 1 (10-260 BPM) si jamais de clock

### PLL Logicielle (Clock Smoothing)

Le clock BLE arrive par rafales (groupé par connection interval). Une **PLL logicielle** lisse les ticks :
- Calcule le tempo moyen sur 24 ticks (1 noire).
- Génère un clock interne régulier à ce tempo.
- Les ArpEngines se synchronisent au clock lissé, pas aux ticks bruts.
- Se recale doucement sur le clock externe (pas de sauts).
- Jitter résultant : **±1-2ms** même en BLE (contre ±15ms sans PLL).

### BLE Connection Interval

Configurable dans les Settings :
- **Low Latency** : 7.5ms (meilleur latence, plus de batterie)
- **Normal** : 15ms (défaut, compatible Apple)
- **Battery Saver** : 30ms (économie batterie, plus de latence)

> Note : le device demande l'interval au central. Apple impose 15ms minimum.

---

## G. Setup Mode V2

```
[1] Pressure Calibration
[2] Pad Ordering
[3] Pad Roles
[4] Bank Config
[5] Settings
[0] Reboot & Exit
```

### Tool 1 : Pressure Calibration — inchangé V1

### Tool 2 : Pad Ordering

"Touche les pads du grave à l'aigu." Positions 1-48, pas de base note. Partial save OK.

### Tool 3 : Pad Roles (unifié, sous-menu)

```
[1] Bank Pads (8)
[2] Scale Pads (15)
[3] Arp Pads (2)     — 1 HOLD + 1 play/stop
[4] View All / Collisions
[s] Save  [q] Back
```

Grille couleur (bleu=bank, vert=scale, jaune=arp, rouge=collision). Refuse de sauvegarder avec collisions.

### Tool 4 : Bank Config

Toggle NORMAL/ARPEG par bank. Max 4 ARPEG.

### Tool 5 : Settings

- Baseline Profile (Adaptive/Expressive/Percussive)
- AT Rate (10-100ms)
- BLE Connection Interval (Low Latency / Normal / Battery Saver)

~~Pad Sensitivity~~ → pot arrière (hold arrière). ~~AT Deadzone~~ → pot droit 2 (hold gauche, NORMAL). ~~LED Brightness~~ → pot arrière (seul).

---

## H. Ce Qu'on NE Fait PAS

- ~~Looper~~ — retiré
- ~~Split / Drum / CC~~ — hors scope
- ~~Velocity liée à la pression~~ — à discuter plus tard

---

## I. Priorités

| Feature | Priorité |
|---|---|
| Restructuration + BankSlots (velocity, pitch bend) | **P0** |
| Pad Ordering + Scale system | **P0** |
| Bank select + Scale control (bouton gauche unique) | **P0** |
| PotRouter (5 pots, bindings contextuels) | **P0** |
| ArpEngine (live + persistant, pile positions, résolution au tick) | **P1** |
| MIDI Clock réception + PLL | **P1** |
| Arp controls (HOLD, play/stop, pattern/octave/division via pots) | **P1** |
| Pad Roles (25 pads) + Bank Config tools | **P2** |
| Settings (BLE interval, etc.) | **P2** |
