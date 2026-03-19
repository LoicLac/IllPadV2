# ILLPAD48 V2 — Nouvelles Idées & Features

> Scale system + arpégiateur multi-bank + 3 pots contextuels.
> Dernière mise à jour : 18 mars 2026.

---

## A. Banks Toujours Actives

### Paradigme

Les 8 banks sont **toujours vivantes**. Le bank select change quelle bank reçoit les pads. Les ArpEngines en HOLD continuent en arrière-plan.

### Types de Banks

| Type | Comportement |
|---|---|
| **NORMAL** | Pads → notes + poly-aftertouch. Ne joue qu'au premier plan. |
| **ARPEG** | Arpégiateur. Deux sous-modes : live (non-HOLD) et persistant (HOLD). Pas d'aftertouch. Velocity fixe avec variation optionnelle. |

Max 4 banks ARPEG. Configurable dans Tool 4 (Bank Config).

### Bank Switch

Le BankManager ne gère **aucune logique d'arrêt d'arpège**. Les arpèges vivent/meurent par leur propre logique.

| Situation | Au switch |
|---|---|
| Quitter NORMAL | All notes off |
| Quitter ARPEG (HOLD off) | Rien — déjà mort (doigts levés) |
| Quitter ARPEG (HOLD on) | Rien — continue tout seul |
| Revenir sur ARPEG HOLD qui tourne | Pads ajoutent des notes, double-tap retire |

---

## B. Les 3 Couches de Mapping

### Couche 1 : Pad Ordering (physique → logique)

`padOrder[48]` — rang 0 à 47, grave à aigu. Fait une fois, toutes les banks. Tool 2.

### Couche 2 : Scale Config (par bank, runtime)

- **Chromatique** : `rootBase + padOrder[i]`
- **Gamme** : `rootBase + octave*12 + scaleIntervals[mode][degree]`
- **Root = base note partout.** Changer la root change la tonalité en gamme ET le point de départ en chromatique.
- Changeable à chaud via bouton droit. Persisté NVS par bank.

### Couche 3 : Control Pad Assignment

31 pads assignés à des fonctions. Un pad = un seul rôle. Tool 3.

---

## C. Scale System (Bouton Droit Hold)

### Pads Communs (toutes banks) — 15

| Zone | Pads | Fonction |
|---|---|---|
| Root | 7 | A, B, C, D, E, F, G |
| Mode | 7 | Ionian → Locrian |
| Chromatique | 1 | Toggle chromatique |

### Pads Arp (bank ARPEG seulement) — 7 pendant le hold

| Pad | Fonction |
|---|---|
| Pattern Up | Arpège montant |
| Pattern Down | Descendant |
| Pattern Up-Down | Ping-pong |
| Pattern Random | Aléatoire |
| Pattern Order | Ordre de press |
| Octave cycle | 1→2→3→4→1 |
| HOLD toggle | On/off |

### Pad Play/Stop — 1 pad spécial

| Pad | Fonction |
|---|---|
| Play/Stop | Toggle transport de l'arpège |

**Comportement contextuel** :
- Sur bank NORMAL → ce pad joue une note normalement.
- Sur bank ARPEG, HOLD OFF → ce pad joue une note (entre dans l'arpège comme les autres).
- Sur bank ARPEG, HOLD ON → ce pad = **play/stop toggle** (pas de note). Stop puis Play repart du début.

C'est le **seul pad de contrôle actif en mode jeu** (les 30 autres ne sont actifs que pendant les holds).

**Total pads de contrôle** : 8 bank + 15 scale + 8 arp (7 hold + 1 play/stop) = **31 pads**. 17 libres pendant les holds.

---

## D. Arpégiateur

### Deux Sous-Modes (déterminés par HOLD)

#### HOLD OFF — Mode Live

| Action | Effet |
|---|---|
| Presser un pad | Note ajoutée |
| Lever un doigt | Note retirée |
| Tous les doigts levés | Liste vide → arpège s'arrête |
| Pad play/stop | Joue une note (pas de rôle spécial) |
| Double-tap | Pas de sens (lever le doigt suffit) |

#### HOLD ON — Mode Persistant

| Action | Effet |
|---|---|
| Presser un pad | Note ajoutée (persiste) |
| Double-tap (< 300ms) | Note retirée |
| Pad play/stop | Toggle play/stop. Play repart du début. |
| Lever les doigts | Rien — notes restent |
| Bank switch | Arpège continue en background |

### 5 Patterns

| Pattern | Comportement |
|---|---|
| Up | Grave → aigu, boucle |
| Down | Aigu → grave, boucle |
| Up-Down | Ping-pong (sans répéter extrêmes) |
| Random | Aléatoire parmi la pile |
| Order | Ordre chronologique d'ajout |

> Note : garder des stubs dans le code pour ajout possible de patterns supplémentaires.

### Pile de Notes

Jusqu'à **48 notes** (tous les pads). Avec 4 octaves = 192 steps max. À 120BPM en croches = **48 secondes** sans répétition en Up. Up-Down double ça.

### Octave Range : 1-4 (cycle via pad)

### Division Rythmique : Pot Droit (voir section E)

### Velocity : fixe avec variation optionnelle (voir section E)

---

## E. Les 3 Potentiomètres

### Principe

Chaque pot a une fonction principale (pot seul) et des fonctions secondaires (combo avec un bouton). Les fonctions changent selon le type de bank au premier plan (NORMAL vs ARPEG). Système **catch** sur tous les pots (le pot doit passer par la valeur stockée avant de prendre le contrôle).

**Stockage des valeurs pot** :
- Paramètres **NORMAL** (response shape, slew rate, AT deadzone) = **GLOBAUX** — un seul set partagé par toutes les banks NORMAL.
- Paramètres **ARPEG** (gate length, swing, division, base velocity, velocity variation) = **PAR BANK** — chaque bank ARPEG a ses propres valeurs.
- Au bank switch ARPEG → le catch se réinitialise (le pot physique doit re-catcher la valeur de la nouvelle bank).

### Pot Gauche — Feel / Son

| Contexte | Pot seul (LIVE) | Btn droit + pot G (LIVE cross) | Btn gauche + pot G (non-live) |
|---|---|---|---|
| **NORMAL** | Response shape (courbe AT) | Slew rate (smoothing AT) | 🔲 LIBRE |
| **ARPEG** | Gate length (staccato↔legato) | Swing amount (droit↔shuffle) | 🔲 LIBRE |

### Pot Droit — Notes / Rythme

| Contexte | Pot seul (LIVE) | Btn gauche + pot D (LIVE cross) | Btn droit + pot D (non-live) |
|---|---|---|---|
| **NORMAL** | 🔲 LIBRE | AT deadzone | 🔲 LIBRE |
| **ARPEG** | Division rythmique | Velocity variation (±% random) | Base velocity (centre) |

### Pot Arrière — Config

| Combo | Fonction |
|---|---|
| Pot seul | Tempo interne (BPM fallback, 40-240) |
| Btn gauche + pot arr | Pad sensitivity |
| Btn droit + pot arr | 🔲 LIBRE |

### Divisions Rythmiques (Pot Droit, bank ARPEG)

```
Position    Division
0%          1/1 (ronde)
~15%        1/2 (blanche)
~30%        1/4 (noire)
~45%        1/8 (croche)
~60%        1/16 (double croche)
~75%        1/8T (croche triolée)
~90%        1/16T (double croche triolée)
100%        1/32 (triple croche)
```

### Velocity Variation (Btn gauche + Pot Droit, bank ARPEG)

Le pot contrôle le **pourcentage de variation aléatoire** autour de la base velocity :
- 0% → velocity fixe (ex: toujours 80)
- 50% → variation ±40 autour de 80 (range 40-120)
- 100% → variation ±63 (range 17-143, clampé 1-127)

La **base velocity** est réglée via btn droit + pot droit (non-live, on la règle une fois).

### Slots Libres (7)

| Slot | Statut |
|---|---|
| NORMAL : pot D seul | 🔲 LIBRE |
| NORMAL : btn gauche + pot G | 🔲 LIBRE |
| NORMAL : btn droit + pot D | 🔲 LIBRE |
| ARPEG : btn gauche + pot G | 🔲 LIBRE |
| Global : btn droit + pot arr | 🔲 LIBRE |

Réservés pour de futures features. Le système est extensible.

---

## F. Tempo / MIDI Clock

### Sources (par priorité)

1. **USB MIDI Clock** — prioritaire si disponible (jitter ±0.5ms)
2. **BLE MIDI Clock** — si pas d'USB (jitter ±7-15ms)
3. **Dernier tempo reçu** — si le clock externe s'arrête
4. **Tempo interne** — pot arrière (40-240 BPM) si jamais de clock

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
[3] Arp Pads (8)     — 5 patterns + 1 octave + 1 HOLD + 1 play/stop
[4] View All / Collisions
[s] Save  [q] Back
```

Grille couleur (bleu=bank, vert=scale, jaune=arp, rouge=collision). Refuse de sauvegarder avec collisions.

### Tool 4 : Bank Config

Toggle NORMAL/ARPEG par bank. Max 4 ARPEG.

### Tool 5 : Settings

- Baseline Profile (Adaptive/Expressive/Percussive)
- Pad Sensitivity (5-30%)
- AT Rate (10-100ms)
- AT Deadzone (0-250)
- BLE Connection Interval (Low Latency / Normal / Battery Saver)

---

## H. Ce Qu'on NE Fait PAS

- ~~Looper~~ — retiré
- ~~Split / Drum / CC~~ — hors scope
- ~~Velocity liée à la pression~~ — à discuter plus tard

---

## I. Priorités

| Feature | Priorité |
|---|---|
| Restructuration + BankSlots | **P0** |
| Pad Ordering + Scale system | **P0** |
| Bank select (bouton gauche) | **P0** |
| PotRouter (3 pots) | **P0** |
| ArpEngine (live + persistant) | **P1** |
| MIDI Clock réception + PLL | **P1** |
| Arp controls (patterns, hold, play/stop, double-tap) | **P1** |
| Pad Roles + Bank Config tools | **P2** |
| Settings (BLE interval, etc.) | **P2** |
