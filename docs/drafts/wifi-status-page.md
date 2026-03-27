# ILLPAD48 — WiFi Status Page (Spec V2)

> Spec pour le codeur. Design, protocole WebSocket, architecture polling.
> V1 = page status (header, banks, colonnes, pots). V1.5 = vue SVG des pads (overlay sur button hold).
> Priorité : P3 (après phases 1-4).
> Dernière mise à jour : 21 mars 2026.

---

## 1. Vue d'Ensemble

### Objectif

L'ILLPAD a 29 pads de contrôle (bank, scale, arp, octave, hold, play/stop) assignés par l'utilisateur dans le Setup. Les combos pot × bouton changent selon le mode NORMAL/ARPEG. **Le but principal de cette page est de servir de rappel visuel** : quels pads font quoi, quels combos sont disponibles, quel est l'état courant de chaque bank. Ce n'est **pas** un dashboard temps réel synchronisé au beat — c'est une référence qu'on consulte d'un coup d'œil sur une tablette posée à côté.

### Architecture

L'ESP32 expose un serveur WiFi AP. Une tablette/téléphone en paysage affiche l'état de l'ILLPAD. Les mises à jour arrivent par WebSocket à basse fréquence (~1-2 Hz max pour les pots, sur événement pour les changements d'état).

### Activation — Compile-Time Flag

- **Désactivé par défaut.**
- `#define WIFI_STATUS_ENABLED 1` dans `HardwareConfig.h` (ou via `build_flags` dans `platformio.ini`).
- Tout le code WiFi est encadré par `#if WIFI_STATUS_ENABLED`. Quand désactivé : zéro overhead CPU, zéro RAM, zéro impact BLE.
- La configuration fine (BLE coexistence, puissance WiFi, etc.) sera gérée par un **Tool 6 dédié** dans le Setup mode (voir Section 12).
- AP : SSID `ILLPAD48`, password `illpad48`, IP `192.168.4.1`.
- mDNS (optionnel) : `http://illpad.local`.

### Contraintes

- **Paysage obligatoire** (pas de portrait).
- **Mode sombre par défaut**.
- **Taille page** : < 15 KB gzippé (PROGMEM).
- **Basse priorité** : la page est un outil de rappel, pas un instrument. Latence d'affichage de 50-200ms parfaitement acceptable.

---

## 2. Deux Vues

### Vue Status (V1) — Affichage par défaut

```
┌──────────────────────────────────────────────────────────────────┐
│  ILLPAD48  ● USB ● BLE ● WS         [124] bpm            72%   │ Header
├──────────────────────────────────────────────────────────────────┤
│  btn rear │ LED BRIGHT 70 │ — │ — │                              │ Pot arrière
├──────────────────────────────────────────────────────────────────┤
│  [1] [2▶] [3] [4] [5] [6] [7▶] [8]                              │ Banks
├──────────────────────────────────────────────────────────────────┤
│  Bank 2       │      ARPEG           │    D                      │
│  Channel 2    │    Up-Down           │    root                   │ 3 colonnes
│  ▶ Playing    │    2 oct · 7 notes   │    Dorian                 │
├──────────────────────────────────────────────────────────────────┤
│          │ ↺ right 1  │ ↺ right 2  │ ↺ right 3  │ ↺ right 4    │
│          │ TEMPO 124  │ GATE 73    │ PATTERN Up │ BASE VEL 100  │ Grille pots
│ btn left │ — (empty)  │ SHUF D 0.4 │ SHUF T 2  │ VEL VAR ±25  │ (4 right pots)
└──────────────────────────────────────────────────────────────────┘
```

### Vue SVG Pads (V1.5) — Overlay sur button hold

Quand le **bouton gauche (left)** est held (info reçue via WebSocket), la zone principale (3 colonnes + grille pots) est **remplacée** par le SVG des 48 pads. Le header, la ligne pot arrière, et la barre de banks restent visibles. Le bouton gauche est le seul qui contrôle les pads (single-layer control : bank + scale + arp dans un seul hold).

```
┌──────────────────────────────────────────────────────────────────┐
│  ILLPAD48  ● USB ● BLE ● WS         [124] bpm            72%   │ Header (reste)
├──────────────────────────────────────────────────────────────────┤
│  btn rear │ LED BRIGHT 70 │ — │ — │                              │ Pot arr (reste)
├──────────────────────────────────────────────────────────────────┤
│  [1] [2▶] [3] [4] [5] [6] [7▶] [8]                              │ Banks (reste)
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│              ┌─── SVG 48 PADS ───┐                               │
│              │  TOUS les rôles   │                               │ Overlay SVG
│              │  visibles (bank + │                               │
│              │  scale + arp)     │                               │
│              └────────────────────┘                               │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

**Comportement** :
- Bouton gauche held → SVG apparaît, **tous les rôles visibles simultanément** : bank en **bleu**, root en **ambre**, mode en **corail**, chrom en **blanc**, arp en **teal**, play/stop en **magenta**.
- Bouton rear → pas d'overlay SVG (rear = batterie/setup/modifier pot rear uniquement).
- Pot tourné → SVG disparaît, retour à la vue status.
- Bouton relâché → SVG disparaît.

---

## 3. Vue Status (V1) — Composants Détaillés

### 3.1 Header

| Zone | Contenu |
|---|---|
| Gauche | `ILLPAD48` + `● USB` `● BLE` `● WS` (vert=connecté, gris=off) |
| Centre | BPM en gros (30px), chiffre statique. Source en petit : "USB sync", "BLE sync", "int" (ambre si fallback interne). |
| Droite | Batterie % |

**WS heartbeat** : point `● WS` passe gris si pas de message WebSocket depuis 5s.

### 3.2 Ligne Pot Arrière

Le pot arrière (rear) a 2 modes : seul = LED brightness, hold rear = pad sensitivity.

| Gauche | 3 cellules | Droite |
|---|---|---|
| `btn rear` (corail) | LED BRIGHT (neutre) · — (vide) · — (vide) | — |

Quand le bouton rear est held + pot rear tourné, la cellule active passe à SENSITIVITY (ambre). Le rear button n'a pas de rapport avec les pots droits (seul le left button modifie les pots droits).

### 3.3 Barre de Banks

8 cellules. Numéro + barre couleur (bleu=NORMAL, teal=ARPEG) + scale résumée.
- Bank active → bordure bleue 2px.
- Banks ARPEG en arrière-plan → animation pulse opacité + point vert pulsant.

### 3.4 Trois Colonnes (hauteur fixe 140px)

| Colonne | NORMAL | ARPEG |
|---|---|---|
| Gauche | Bank N, Channel N, Vel base±var, PB offset | + badge ▶ Playing / ■ Stopped, Vel base±var |
| Centre | Badge NORMAL, "Poly-AT" | Badge ARPEG, Pattern, Octaves·Notes, HOLD on/off, Quantize (Immediate/Beat/Bar) |
| Droite | Root (gros), Mode | Root (gros), Mode |

### 3.5 Grille Pots (4 colonnes × 2 lignes)

4 pots droits (right 1-4), chacun avec 2 modes : seul (neutre) et +hold left (ambre).

```
              ↺ right 1      ↺ right 2      ↺ right 3      ↺ right 4
              [cellule]      [cellule]      [cellule]      [cellule]   ← neutre
btn left      [cellule]      [cellule]      [cellule]      [cellule]   ← ambre
```

**Contenu NORMAL** :

| Pot | Seul (neutre) | + hold left (ambre) |
|---|---|---|
| Right 1 | Tempo (10-260 BPM) | — (empty) |
| Right 2 | Response shape | AT deadzone |
| Right 3 | Slew rate | Pitch bend (per-bank) |
| Right 4 | Base velocity (per-bank) | Velocity variation (per-bank) |

**Contenu ARPEG** :

| Pot | Seul (neutre) | + hold left (ambre) |
|---|---|---|
| Right 1 | Tempo (10-260 BPM) | Division (9 binary: 4/1→1/64) |
| Right 2 | Gate length | Shuffle depth (0.0-1.0) |
| Right 3 | Pattern (5 discrete) | Shuffle template (5) |
| Right 4 | Base velocity (per-bank) | Velocity variation (per-bank) |

- Pots seuls = texte neutre. Combos +btn left = **ambre**.
- Cellules vides = `—` grisé, opacité 0.25.
- Labels 12px, valeurs 17px, cellules 42px.
- **Zoom ×2** quand pot tourné : `transform: scale(2)`, z-index élevé, couleur conservée (neutre→neutre, ambre→ambre). Retour normal après 2.5s.
- Contenu change selon NORMAL/ARPEG, layout ne bouge pas.
- Tempo, shape, slew, AT deadzone = **GLOBAL**. Gate, shuffle, division, pattern = **PER BANK**. Velocity (base + variation) = **PER BANK** (NORMAL + ARPEG). Pitch bend = **PER BANK** (NORMAL only).

### 3.6 Overlay Erreur

Bande rouge translucide en haut de la page, slide-down. Plusieurs erreurs empilées. Disparaît quand l'erreur est résolue.

| Erreur | Message |
|---|---|
| Capteur I2C fail | `⚠ SENSOR [A-D] FAILED` |
| Sensing stall | `⚠ SENSING STALL` |
| BLE déconnecté | `⚠ BLE DISCONNECTED` |
| Batterie critique | `⚠ BATTERY LOW — X%` |

---

## 4. Vue SVG Pads (V1.5) — Design

### 4.1 Le SVG

48 `<path>` correspondant aux 48 pads physiques, arrangement circulaire/radial fidèle au design réel de l'ILLPAD. Chaque path a un id `p0` à `p47`.

**Mapping SVG path → pad index** : tableau de 48 entrées codé en dur. Doit être calibré une fois sur le hardware réel (toucher chaque pad, noter quel path SVG correspond). Ce mapping ne change jamais (c'est le PCB).

Taille SVG : ~10 KB non compressé, ~3 KB gzippé.

### 4.2 Couleurs des Pads

| Rôle | Couleur fill | Couleur stroke/texte | Quantité |
|---|---|---|---|
| Bank (B1-B8) | #1a3a5a | #5AA8F0 (bleu) | 8 pads |
| Root (A-G) | #4a3010 | #E5A035 (ambre) | 7 pads |
| Mode (Ion-Loc) | #4a2218 | #E8764A (corail) | 7 pads |
| Chromatique | #2a2a3a | #aaa (blanc) | 1 pad |
| Hold | #0a3a2a | #30D1A2 (teal) | 1 pad |
| Octave (1-4) | #0a3a2a | #30D1A2 (teal) | 4 pads |
| **Play/Stop** | **#3a1040** | **#D453B0 (magenta)** | **1 pad** |
| Musical (pas de rôle) | #3A3A3C | #555 (gris) | 19 pads restants |

**Total** : 29 pads avec rôle de contrôle + 19 pads musicaux = 48.

**Tous les rôles sont visibles simultanément** lors du hold bouton gauche (single-layer control). Il n'y a qu'un seul mode d'overlay — pas de distinction gauche/droit. Le bouton rear n'a pas d'overlay SVG.

### 4.3 Labels sur les Pads

Chaque pad affiche deux lignes de texte, positionnées au centre de la bounding box du path :

**Ligne 1 — Rôle** (si applicable) : label coloré (B1, Mix, ▶/■, etc.)
- Taille : 13px sur les gros pads, 11px sur les petits.
- Couleur : celle du rôle.

**Ligne 2 — Note résolue** : toujours affichée, même sur les pads à rôle.
- Taille : 9px sur les gros pads, 7px sur les petits.
- Couleur : #999 (gris clair).
- **3px d'écart** entre le rôle et la note.

Les pads sans rôle de contrôle affichent uniquement la note résolue.

Les notes **changent dynamiquement** quand la scale change (root, mode, chromatique). L'ESP32 envoie `pad_notes` via WebSocket.

### 4.4 "Vue Carte"

C'est une vue carte : les couleurs donnent l'info principale. Tous les rôles sont visibles d'un coup (single-layer). Les petits pads (rayons au centre) sont trop petits pour un texte lisible — c'est OK. Le hover/tap sur un pad affiche un tooltip avec la note en gros, le rôle, et le numéro de pad.

---

## 5. Protocole WebSocket

### Principe

Client → `ws://192.168.4.1/ws`. Unidirectionnel (ESP32 → client).

**Philosophie** : la page est un rappel visuel, pas un oscilloscope. Les messages sont envoyés **sur changement d'état** uniquement — pas de polling côté client, pas de streaming continu. Le StatusPoller côté ESP32 compare l'état courant à un snapshot "dernier envoyé" et ne pousse un message que si la valeur a changé.

### Débits & Throttling

| Catégorie | Fréquence max | Justification |
|---|---|---|
| État bank/scale/arp | Immédiat sur changement | Événements rares (interaction utilisateur) |
| Pots | **1 Hz max** par pot | Rappel, pas feedback en temps réel |
| Batterie | Toutes les 30s | Lent par nature |
| Connexions USB/BLE | Immédiat sur changement | Événement rare |
| Button hold | Immédiat | Toggle SVG overlay |
| Heartbeat | Toutes les 3s | Détection connexion perdue |
| Erreurs | Immédiat | Critique |

**Pas de message `beat`** — la page n'a pas besoin de flasher au rythme. Le BPM est affiché comme un chiffre statique.

### Messages — État

#### `banks` — Snapshot complet (~500 bytes, au connect + changement config)

```json
{
  "type": "banks",
  "active": 2,
  "slots": [
    { "ch": 1, "type": "N", "root": "C", "mode": "Ionian", "chrom": true,
      "vel": 100, "var": 0, "pb": 12 },
    { "ch": 2, "type": "A", "root": "D", "mode": "Dorian", "chrom": false,
      "vel": 100, "var": 25, "pb": 0, "play": true, "hold": true,
      "pat": "Up-Down", "oct": 2, "n": 7, "qtz": "Beat" },
    ...
  ]
}
```

- `vel` / `var` : base velocity + variation, per-bank (NORMAL + ARPEG).
- `pb` : pitch bend offset, per-bank (NORMAL only, 0 pour ARPEG).
- `qtz` : quantize mode (ARPEG only) — `"Immediate"`, `"Beat"`, `"Bar"`.
- Envoyé au connect + quand la config d'une bank change (type, scale, arp params).

#### `state` — Bank au premier plan (~150 bytes, sur changement)

```json
{
  "type": "state",
  "bank": 2,
  "bankType": "ARPEG",
  "scale": { "chromatic": false, "root": "D", "mode": "Dorian" },
  "velocity": { "base": 100, "variation": 25 },
  "pitchBend": 0,
  "arp": { "playing": true, "hold": true, "pattern": "Up-Down", "octave": 2, "notes": 7,
           "quantize": "Beat" }
}
```

- Envoyé quand la bank active change, ou quand l'état de la bank active change (scale, arp play/stop, hold toggle, etc.).
- `arp` n'est présent que pour les banks ARPEG.

#### `tempo` (~40 bytes, sur changement, sinon toutes les 10s)

```json
{ "type": "tempo", "bpm": 124, "source": "USB", "follow": true }
```

- Source : `"USB"`, `"BLE"`, `"int"` (internal = pot right 1). Priority: USB > BLE > last known > internal.
- `follow` : Follow Transport setting (slave mode). Quand false, Start/Stop/Continue externes sont ignorés.
- Envoyé quand BPM ou source change. Heartbeat tempo toutes les 10s (pas besoin de plus fréquent).

### Messages — Hardware

#### `battery` (~30 bytes, toutes les 30s)

```json
{ "type": "battery", "percent": 72, "charging": true }
```

#### `connections` (~30 bytes, sur changement)

```json
{ "type": "connections", "usb": true, "ble": true }
```

#### `pot` (~50 bytes, 1Hz max par pot)

```json
{ "type": "pot", "slot": "r2-l", "param": "shuf_depth", "value": 58 }
```

`slot` = `"{pot}-{combo}"`. Pot: `r1`-`r4` = right 1-4, `rear` = rear. Combo: `0` = seul, `l` = +hold left (right pots), `r` = +hold rear (rear pot only).

Throttlé à 1 Hz max par pot — la page montre la dernière valeur connue, pas le mouvement en temps réel.

#### `error` / `error_clear`

```json
{ "type": "error", "code": "sensor_fail", "message": "Sensor B failed" }
{ "type": "error_clear", "code": "sensor_fail" }
```

### Messages — Pad View (V1.5)

#### `button_hold` — Bouton gauche held/released

```json
{ "type": "button_hold", "held": true }
{ "type": "button_hold", "held": false }
```

Seul le bouton gauche (left) déclenche l'overlay SVG. Le bouton rear n'a pas d'overlay.

#### `pad_roles` — Assignation des 29 pads de contrôle (~200 bytes, au connect + changement config)

```json
{
  "type": "pad_roles",
  "bankPads": [14, 15, 16, 22, 6, 10, 11, 21],
  "rootPads": [2, 12, 13, 17, 18, 19, 0],
  "modePads": [1, 23, 24, 9, 8, 7, 34],
  "chromPad": 31,
  "arpPads": { "hold": 27, "playStop": 46, "octave": [29, 33, 38, 42] }
}
```

- 8 bank + 7 root + 7 mode + 1 chrom + 1 hold + 1 playStop + 4 octave = 29 control pads.
- Patterns (Up/Down/UpDown/Random/Order) sélectionnés via pot right 3, pas via pads.
- playStop : actif uniquement en ARPEG + HOLD ON ; en HOLD OFF c'est un pad musical normal.
- Change rarement (uniquement via Setup Tool 3).

#### `pad_notes` — Notes MIDI résolues (~100 bytes, sur changement scale)

```json
{ "type": "pad_notes", "notes": [60, 62, 64, 65, 67, 69, 71, 72, ...] }
```

48 valeurs. 255 = unmapped. Envoyé quand root, mode, ou chromatique change sur la bank active.

### Messages — Système

#### `hb` — Heartbeat (toutes les 3s)

```json
{ "type": "hb" }
```

Client-side : si pas de message reçu depuis 5s, afficher `● WS` en gris (connexion perdue).

### Séquence au Connect

1. `banks` — snapshot complet des 8 banks
2. `state` — détail de la bank active
3. `tempo` — BPM, source, follow
4. `battery` — pourcentage, charging
5. `connections` — USB, BLE
6. `pad_roles` — 29 control pads
7. `pad_notes` — 48 notes résolues
8. Puis : heartbeat (3s loop) + événements sur changement

---

## 6. Design Visuel — Dark Mode

### Palette

```css
:root {
  --bg: #1A1A1C;
  --sf: #242426;
  --cl: #2C2C2E;
  --t1: #E8E8E8;
  --t2: #8E8E93;
  --t3: #636366;
  --bd: rgba(255,255,255,0.08);
  --ba: #378ADD;
  --ok: #34C759;
  --err: #FF3B30;
  --amb: #E5A035;
  --cor: #E8764A;
  --tea: #30D1A2;
  --blu: #5AA8F0;
  --mag: #D453B0;
}
```

### Animations

| Animation | Spec |
|---|---|
| Bank ARPEG pulse | opacity 1→0.45, 1.2s loop |
| Play dot pulse | scale 1→1.7, 1.2s loop |
| Pot zoom in | transform scale(2), 0.2s ease-out |
| Pot zoom out | transform scale(1), 0.5s ease-out |
| Error slide in | translateY -100%→0, 0.3s |
| WS heartbeat timeout | dot vert→gris, 0.5s (si pas de message depuis 5s) |
| SVG view appear | opacity 0→1, 0.2s |
| SVG view disappear | opacity 1→0, 0.15s |

> Note : pas de "BPM beat flash" — le BPM est affiché comme un chiffre statique. La page est un rappel, pas un métronome visuel.

### Fontes

`system-ui, -apple-system, sans-serif` — fonte système, pas de custom.

---

## 7. Architecture ESP32 — Polling

### Principe : lire, comparer, pousser

**Aucun callback dans les managers existants.** Le StatusPoller lit l'état de tous les objets via leurs getters publics, compare à un snapshot "dernier envoyé", et pousse un message WebSocket uniquement si quelque chose a changé. Les managers ne savent pas que le WiFi existe.

```
┌─────────────────────────────────────────────────────────┐
│  loop() — Core 1                                        │
│                                                         │
│  1-11. Chemin critique (MIDI)          ← inchangé       │
│  12.   PotRouter.update()              ← inchangé       │
│  13.   BatteryMonitor.update()         ← inchangé       │
│  14.   LedController.update()          ← inchangé       │
│  15.   NvsManager.notifyIfDirty()      ← inchangé       │
│  16.   StatusPoller.update()           ← SEUL AJOUT     │
│  17.   vTaskDelay(1)                                    │
└─────────────────────────────────────────────────────────┘
         │
         ▼ (si WiFi activé + client connecté + état changé)
┌─────────────────────────────────────────────────────────┐
│  StatusPoller.update()                                  │
│  ├── Compare snapshot courant vs _lastSent              │
│  ├── Si diff → serialize JSON → WifiStatusServer.send() │
│  ├── Heartbeat toutes les 3s                            │
│  └── Throttle pots à 1Hz                                │
└─────────────────────────────────────────────────────────┘
```

### Fichiers

```
src/wifi/
├── WifiStatusServer.cpp/.h     # AP + HTTP + WebSocket (infra réseau)
├── StatusPoller.cpp/.h         # Lit les managers, diff, sérialise JSON
└── status_page.h               # Page HTML gzippée en PROGMEM
```

### Compile-Time Guard

Tout le code WiFi est encadré :

```cpp
// Dans HardwareConfig.h ou via platformio.ini build_flags
#define WIFI_STATUS_ENABLED 0   // 0 = désactivé (défaut), 1 = activé

// Dans main.cpp
#if WIFI_STATUS_ENABLED
  #include "wifi/WifiStatusServer.h"
  #include "wifi/StatusPoller.h"
  static WifiStatusServer s_wifiServer;
  static StatusPoller s_statusPoller;
#endif
```

Quand `WIFI_STATUS_ENABLED == 0` : aucune inclusion, aucune RAM, aucun cycle CPU, aucun impact BLE.

### StatusPoller — Interface

```cpp
class StatusPoller {
public:
  void begin(WifiStatusServer& server,
             const BankSlot* banks,         // s_banks[8]
             const BankManager& bankMgr,
             const ScaleManager& scaleMgr,
             const ClockManager& clockMgr,
             const PotRouter& potRouter,
             const MidiTransport& transport,
             const BatteryMonitor& battery,
             const ArpEngine* arpEngines,    // s_arpEngines[4]
             const uint8_t* padOrder);

  void update();  // Appelé chaque loop, fait le diff + send si nécessaire

private:
  // Snapshot "dernier envoyé" — comparé à l'état courant
  struct Snapshot {
    uint8_t  activeBank;
    uint8_t  bankTypes[8];
    ScaleConfig scales[8];
    uint8_t  baseVelocity[8];
    uint8_t  velVariation[8];
    uint16_t pitchBend[8];
    bool     arpPlaying[4];
    bool     arpHold[4];
    uint8_t  arpNoteCount[4];
    uint8_t  arpOctave[4];
    ArpPattern arpPattern[4];
    uint16_t bpm;
    bool     usbConnected;
    bool     bleConnected;
    uint8_t  batteryPct;
    bool     btnLeftHeld;
  };

  Snapshot _lastSent;
  uint32_t _lastHeartbeat;
  uint32_t _lastPotSend[5];   // Throttle 1Hz par pot

  // Références const — lecture seule, aucune mutation
  const BankSlot*       _banks;
  const BankManager*    _bankMgr;
  const ClockManager*   _clockMgr;
  const PotRouter*      _potRouter;
  const MidiTransport*  _transport;
  const BatteryMonitor* _battery;
  const ArpEngine*      _arpEngines;
  WifiStatusServer*     _server;
};
```

**Points clés** :
- Toutes les références sont `const` — le poller ne peut rien muter.
- Le snapshot `_lastSent` est comparé champ par champ. Seuls les deltas sont envoyés.
- `update()` doit prendre < 1ms dans le cas courant (pas de changement = juste des comparaisons).
- Si aucun client WebSocket n'est connecté, `update()` retourne immédiatement (pas de sérialisation).

### Getters manquants (à ajouter)

Certains getters n'existent pas encore sur les classes existantes. Ajouts nécessaires (trivial — juste `return _member;`) :

| Classe | Getter à ajouter | Retour |
|---|---|---|
| `ClockManager` | `getClockSource()` | `uint8_t` (0=USB, 1=BLE, 2=internal) |
| `ClockManager` | `getFollowTransport()` | `bool` |
| `ArpEngine` | `getGateLength()` | `float` |
| `ArpEngine` | `getShuffleDepth()` | `float` |
| `ArpEngine` | `getShuffleTemplate()` | `uint8_t` |
| `ArpEngine` | `getPattern()` | `ArpPattern` |
| `ArpEngine` | `getOctaveRange()` | `uint8_t` |
| `ArpEngine` | `getBaseVelocity()` | `uint8_t` |
| `ArpEngine` | `getVelocityVariation()` | `uint8_t` |
| `ArpEngine` | `getStartMode()` | `uint8_t` (quantize) |

Ces getters sont des one-liners `const` qui ne changent aucun comportement.

### Intégration Loop

```cpp
void loop() {
  // 1-11. Chemin critique (inchangé)
  // ...
  // 12-15. Secondaire (inchangé)
  // ...

  #if WIFI_STATUS_ENABLED
  s_statusPoller.update();    // ~0ms si pas de client, ~0.5ms si diff à envoyer
  #endif

  vTaskDelay(1);
}
```

**Une seule ligne ajoutée dans `loop()`**, gardée par `#if`. Aucun autre fichier existant n'est modifié (sauf les getters trivials ci-dessus).

### WifiStatusServer — Infrastructure

```cpp
class WifiStatusServer {
public:
  void begin();              // WiFi AP + HTTP + WebSocket setup
  bool hasClients() const;   // StatusPoller skip si false
  void send(const char* json, size_t len);  // Broadcast à tous les clients WS
  void sendToNew();          // Séquence de connect (full snapshot)

private:
  AsyncWebServer _http;
  AsyncWebSocket _ws;
};
```

Sert la page PROGMEM sur `/`, expose le WebSocket sur `/ws`. La logique métier est dans StatusPoller, pas ici.

### lib_deps

```ini
lib_deps =
  max22/ESP32-BLE-MIDI
  ; WiFi status (compile-time opt-in via WIFI_STATUS_ENABLED)
  me-no-dev/ESPAsyncWebServer
  me-no-dev/AsyncTCP
```

> Note : ESPAsyncWebServer + AsyncTCP ajoutent ~100KB flash. Si `WIFI_STATUS_ENABLED == 0`, le linker élimine le code mort, mais les libs sont quand même compilées. Pour un build sans WiFi à zéro overhead, conditionner les `lib_deps` via un env PlatformIO séparé (voir Section 12).

---

## 8. Code HTML/CSS/JS — Référence Visuelle (à réécrire)

> **⚠ CE CODE EST UNE MAQUETTE INSPIRATIONNELLE, PAS DU CODE PRODUCTION.**
> Il a été écrit pour une config hardware différente (2 pots, bouton left/right, pattern pads).
> Le look & feel (dark mode, palette, layout général, typographie) est la cible visuelle.
> Le code JS (pot bindings, button model, slot IDs, getRoleInfo) doit être **entièrement réécrit**
> pour correspondre au hardware actuel (4 right pots, bouton left/rear, 29 control pads, pas de beat flash).
> Les TODO comments dans le JS indiquent les points de divergence principaux.

La page ci-dessous combine V1 (status) et V1.5 (SVG overlay). En production, stockée en PROGMEM gzippée.

Le SVG des 48 pads est inliné dans le HTML. Chaque path a un id `p0`-`p47`. Le mapping SVG→pad est codé en dur (à calibrer une fois sur le hardware).

```html
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>ILLPAD48</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;}
:root{
  --bg:#1A1A1C;--sf:#242426;--cl:#2C2C2E;
  --t1:#E8E8E8;--t2:#8E8E93;--t3:#636366;
  --bd:rgba(255,255,255,0.08);--ba:#378ADD;
  --ok:#34C759;--err:#FF3B30;
  --amb:#E5A035;--cor:#E8764A;--tea:#30D1A2;--blu:#5AA8F0;--mag:#D453B0;
  --r:10px;
}
body{background:var(--bg);color:var(--t1);font-family:system-ui,-apple-system,sans-serif;
  font-size:14px;padding:10px;overflow:hidden;height:100vh;}
.wrap{max-width:780px;margin:0 auto;}

/* === HEADER === */
.hd{display:grid;grid-template-columns:1fr auto 1fr;align-items:center;
  padding:8px 14px;background:var(--sf);border-radius:var(--r);margin-bottom:6px;}
.hd-l{display:flex;align-items:center;gap:14px;}
.hd-logo{font-size:14px;font-weight:600;letter-spacing:1.5px;}
.hd-dots{display:flex;gap:10px;font-size:11px;color:var(--t2);}
.hd-dot{width:6px;height:6px;border-radius:50%;display:inline-block;margin-right:3px;transition:background .5s;}
.dot-on{background:var(--ok);}.dot-off{background:var(--t3);}
.hd-c{text-align:center;}
.hd-bpm{font-size:32px;font-weight:600;line-height:1;transition:opacity .15s;}
.hd-bpm.flash{opacity:0.45;}
.hd-bsrc{font-size:10px;color:var(--t3);margin-top:1px;}
.hd-bsrc.internal{color:var(--amb);}
.hd-r{text-align:right;font-size:12px;color:var(--t2);}

/* === REAR POT === */
/* TODO: LAYOUT CHANGE — rear pot: alone=LED brightness, +hold rear=Sensitivity.
   Remove tempo from rear line (tempo is on right pot 1).
   Only 'btn rear' label needed, no 'btn left'/'btn right' on this line. */
.rr{display:grid;grid-template-columns:auto 1fr auto;align-items:center;gap:6px;
  margin-bottom:6px;padding:0 2px;}
.rr-lbl{font-size:9px;text-transform:uppercase;letter-spacing:.5px;padding:0 4px;font-weight:500;}
.rr-ll{color:var(--amb);}.rr-lr{color:var(--cor);}
.rr-cells{display:grid;grid-template-columns:1fr 1fr 1fr;gap:4px;}
.rr-c{height:28px;border-radius:var(--r);background:var(--cl);display:flex;
  align-items:center;justify-content:center;gap:6px;transition:transform .2s;}
.rr-c.empty{opacity:.2;}
.rr-n{font-size:10px;color:var(--t2);text-transform:uppercase;letter-spacing:.3px;transition:font-size .25s;}
.rr-v{font-size:12px;font-weight:500;transition:font-size .25s;}
.rr-c.ca .rr-n,.rr-c.ca .rr-v{color:var(--amb);}
.rr-c.lit{transform:scale(2);z-index:10;position:relative;}
.rr-c.unlit{transition:transform .5s ease-out;}

/* === BANKS === */
@keyframes ap{0%,100%{opacity:1;}50%{opacity:.4;}}
@keyframes adt{0%,100%{transform:scale(1);}50%{transform:scale(1.7);}}
.bb{display:grid;grid-template-columns:repeat(8,1fr);gap:3px;margin-bottom:6px;}
.bk{padding:8px 2px 6px;border-radius:var(--r);border:1.5px solid var(--bd);
  text-align:center;cursor:pointer;transition:all .15s;position:relative;background:var(--sf);}
.bk:hover{border-color:rgba(255,255,255,.15);}
.bk.act{border-color:var(--ba);border-width:2px;}
.bk.pulse{animation:ap 1.2s ease-in-out infinite;}
.bk.act.pulse{animation:none;}
.bk-ch{font-size:15px;font-weight:600;}
.bk-bar{height:3px;border-radius:2px;margin:3px auto 2px;width:65%;}
.bk-bn{background:var(--blu);}.bk-ba{background:var(--tea);}
.bk-sc{font-size:9px;color:var(--t3);}
.bk-dt{width:5px;height:5px;border-radius:50%;background:var(--ok);
  position:absolute;top:3px;right:3px;animation:adt 1.2s ease-in-out infinite;}

/* === 3 COLUMNS === */
.tri{display:grid;grid-template-columns:1fr 1.4fr 1fr;gap:5px;margin-bottom:5px;}
.co{border:1px solid var(--bd);border-radius:var(--r);padding:12px 10px;
  display:flex;flex-direction:column;align-items:center;justify-content:center;
  text-align:center;height:130px;background:var(--sf);}
.co-big{font-size:22px;font-weight:600;margin-bottom:1px;}
.co-sub{font-size:11px;color:var(--t2);}
.co-st{font-size:12px;font-weight:600;padding:4px 14px;border-radius:20px;margin-top:6px;}
.st-p{background:rgba(52,199,89,.15);color:var(--ok);}
.st-s{background:var(--cl);color:var(--t3);}
.co-tp{font-size:12px;font-weight:600;padding:3px 12px;border-radius:20px;margin-bottom:8px;}
.tp-n{background:rgba(90,168,240,.12);color:var(--blu);}
.tp-a{background:rgba(48,209,162,.12);color:var(--tea);}
.ar-chips{display:flex;gap:4px;margin-top:5px;width:100%;}
.ar-c{flex:1;background:var(--cl);border-radius:8px;padding:5px 3px;text-align:center;}
.ar-cv{font-size:13px;font-weight:600;}.ar-cl{font-size:8px;color:var(--t3);}
.hl-tag{font-size:9px;padding:2px 8px;border-radius:20px;border:1px solid var(--bd);
  color:var(--t3);margin-top:5px;}
.hl-on{border-color:var(--tea);color:var(--tea);background:rgba(48,209,162,.1);}

/* === POT GRID === */
/* TODO: LAYOUT CHANGE — grid goes from 4-col (side+2pots+side) to 4 pot columns × 2 rows.
   New layout: header row (↺right1..↺right4), row 1 (alone, neutral), row 2 (+hold left, amber).
   Remove 'btn right'/corail — only btn left modifier exists for right pots.
   Rear pot is separate (1 cell alone + 1 cell +hold rear). */
.pg{margin-top:2px;}
.pg-hd{display:grid;grid-template-columns:55px 1fr 1fr 55px;gap:4px;margin-bottom:2px;}
.pg-hl{font-size:10px;text-align:center;text-transform:uppercase;letter-spacing:.5px;color:var(--t3);padding:3px 0;}
.pg-rw{display:grid;grid-template-columns:55px 1fr 1fr 55px;gap:4px;margin-bottom:3px;align-items:center;}
.pg-sd{font-size:9px;text-transform:uppercase;letter-spacing:.3px;font-weight:600;}
.pg-sl{text-align:right;padding-right:3px;color:var(--amb);}
.pg-sr{text-align:left;padding-left:3px;color:var(--cor);}
.pg-c{height:40px;border-radius:var(--r);background:var(--cl);display:flex;
  align-items:center;justify-content:center;gap:7px;transition:transform .2s ease-out;position:relative;}
.pg-c.empty{opacity:.2;}
.pg-n{font-size:12px;text-transform:uppercase;letter-spacing:.3px;transition:font-size .2s ease-out;}
.pg-v{font-size:17px;font-weight:600;transition:font-size .2s ease-out;}
.pg-c.nu .pg-n{color:var(--t2);}.pg-c.nu .pg-v{color:var(--t1);}
.pg-c.ca .pg-n,.pg-c.ca .pg-v{color:var(--amb);}
.pg-c.cr .pg-n,.pg-c.cr .pg-v{color:var(--cor);}
.pg-c.empty .pg-n,.pg-c.empty .pg-v{color:var(--t3);}
.pg-c.lit{transform:scale(2);z-index:10;}
.pg-c.unlit{transition:transform .5s ease-out;}

/* === SVG PAD VIEW (V1.5) === */
.sv-wrap{position:relative;background:var(--bg);border-radius:var(--r);padding:16px;
  display:none;transition:opacity .2s;}
.sv-wrap.show{display:block;}
.sv-svg{width:100%;height:auto;display:block;}
.sv-svg path{fill:#3A3A3C;stroke:#555;stroke-width:1;transition:fill .2s;cursor:pointer;}
.sv-svg path.role-bank{fill:#1a3a5a;stroke:var(--blu);}
.sv-svg path.role-root{fill:#4a3010;stroke:var(--amb);}
.sv-svg path.role-mode{fill:#4a2218;stroke:var(--cor);}
.sv-svg path.role-chrom{fill:#2a2a3a;stroke:#aaa;}
.sv-svg path.role-arp{fill:#0a3a2a;stroke:var(--tea);}
.sv-svg path.role-playstop{fill:#3a1040;stroke:var(--mag);}
.sv-svg path:hover{fill:#4A4A4E;stroke:#fff;stroke-width:2;}
.sv-svg path.role-bank:hover{fill:#2a5a8a;}
.sv-svg path.role-root:hover{fill:#6a4a20;}
.sv-svg path.role-mode:hover{fill:#6a3228;}
.sv-svg path.role-arp:hover{fill:#1a5a4a;}
.sv-svg path.role-playstop:hover{fill:#5a2060;}
.sv-label{position:absolute;pointer-events:none;font-family:system-ui,-apple-system,sans-serif;
  font-weight:600;text-align:center;line-height:1;white-space:nowrap;}
.sv-note{color:#999;font-size:9px;display:block;}
.sv-role{display:block;}
.sv-role-bank{color:var(--blu);}
.sv-role-root{color:var(--amb);}
.sv-role-mode{color:var(--cor);}
.sv-role-chrom{color:#aaa;}
.sv-role-arp{color:var(--tea);}
.sv-role-playstop{color:var(--mag);}
.sv-tooltip{position:absolute;background:var(--cl);border:1px solid #555;border-radius:8px;
  padding:8px 12px;pointer-events:none;opacity:0;transition:opacity .15s;z-index:10;white-space:nowrap;}
.sv-tt-note{font-size:20px;font-weight:600;color:var(--t1);}
.sv-tt-role{font-size:12px;margin-top:2px;}
.sv-tt-pad{font-size:10px;color:var(--t3);margin-top:2px;}
.sv-legend{display:flex;gap:12px;justify-content:center;margin-top:8px;flex-wrap:wrap;}
.sv-leg{display:flex;align-items:center;gap:4px;font-size:10px;color:var(--t2);}
.sv-leg-dot{width:8px;height:8px;border-radius:3px;}

/* === ERROR OVERLAY === */
.err-bar{position:fixed;top:0;left:0;right:0;background:rgba(255,59,48,.9);
  color:#fff;font-size:13px;font-weight:500;padding:10px 16px;
  transform:translateY(-100%);transition:transform .3s ease-out;z-index:100;
  display:flex;flex-direction:column;gap:4px;}
.err-bar.show{transform:translateY(0);}
</style>
</head>
<body>
<div class="wrap">
  <div class="hd">
    <div class="hd-l"><span class="hd-logo">ILLPAD48</span>
      <div class="hd-dots">
        <span><span class="hd-dot dot-off" id="d-usb"></span>USB</span>
        <span><span class="hd-dot dot-off" id="d-ble"></span>BLE</span>
        <span><span class="hd-dot dot-off" id="d-ws"></span>WS</span>
      </div>
    </div>
    <div class="hd-c"><div class="hd-bpm" id="bpm">---</div>
      <div class="hd-bsrc" id="bsrc">connecting...</div></div>
    <div class="hd-r" id="bat">--%</div>
  </div>
  <div class="rr" id="rear"></div>
  <div class="bb" id="bar"></div>

  <!-- V1: Status view -->
  <div id="v-status">
    <div class="tri" id="tri"></div>
    <div class="pg" id="pg"></div>
  </div>

  <!-- V1.5: SVG pad view (hidden by default) -->
  <div class="sv-wrap" id="v-svg">
    <svg class="sv-svg" id="svmain" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 708.66 708.66">
      <!-- 48 paths inlined here — see SVG asset file -->
      <!-- Each path has id="p0" through id="p47" -->
      <!-- PLACEHOLDER: Replace with actual SVG paths from Unknown.svg -->
    </svg>
    <div class="sv-tooltip" id="tooltip">
      <div class="sv-tt-note" id="tt-note"></div>
      <div class="sv-tt-role" id="tt-role"></div>
      <div class="sv-tt-pad" id="tt-pad"></div>
    </div>
    <div class="sv-legend">
      <div class="sv-leg"><div class="sv-leg-dot" style="background:#1a3a5a;border:1px solid var(--blu)"></div>Bank</div>
      <div class="sv-leg"><div class="sv-leg-dot" style="background:#4a3010;border:1px solid var(--amb)"></div>Root</div>
      <div class="sv-leg"><div class="sv-leg-dot" style="background:#4a2218;border:1px solid var(--cor)"></div>Mode</div>
      <div class="sv-leg"><div class="sv-leg-dot" style="background:#0a3a2a;border:1px solid var(--tea)"></div>Arp</div>
      <div class="sv-leg"><div class="sv-leg-dot" style="background:#3a1040;border:1px solid var(--mag)"></div>Play/Stop</div>
      <div class="sv-leg"><div class="sv-leg-dot" style="background:#3A3A3C;border:1px solid #555"></div>Musical</div>
    </div>
  </div>
</div>
<div class="err-bar" id="errbar"></div>

<script>
/* ===== STATE ===== */
let banks=[], act=0, litId=null, litTm=null, wsTm=null, errors={};
let holdBtn='none';  // TODO: simplify to boolean — only 'left' triggers SVG overlay now (no 'right')
let padRoles=null, padNotes=null;

/* Pot bindings (NORMAL vs ARPEG) */
/* TODO: COMPLETE REWRITE NEEDED — current grid is 2 pots × 3 combos (left/right × alone/+btn left/+btn right).
   Actual hardware has 4 right pots × 2 combos (alone / +hold left) + 1 rear pot × 2 combos (alone / +hold rear).
   New slot IDs: r1-0, r1-l, r2-0, r2-l, r3-0, r3-l, r4-0, r4-l, rear-0, rear-r.
   NORMAL: r1=Tempo, r2=Shape, r3=Slew, r4=BaseVel | +left: r1=empty, r2=AT deadzone, r3=PitchBend, r4=VelVar.
   ARPEG:  r1=Tempo, r2=Gate, r3=Pattern, r4=BaseVel | +left: r1=Division, r2=ShufDepth, r3=ShufTemplate, r4=VelVar.
   Rear: alone=LED brightness, +hold rear=Pad sensitivity. */
const potN={'0-0':{n:'shape',v:'--',c:'nu'},'0-l':{n:'—',v:'—',e:1},'0-r':{n:'slew',v:'--',c:'cr'},
  '1-0':{n:'—',v:'—',e:1},'1-l':{n:'deadzone',v:'--',c:'ca'},'1-r':{n:'—',v:'—',e:1}};
const potA={'0-0':{n:'gate',v:'--',c:'nu'},'0-l':{n:'—',v:'—',e:1},'0-r':{n:'swing',v:'--',c:'cr'},
  '1-0':{n:'division',v:'--',c:'nu'},'1-l':{n:'vel var',v:'--',c:'ca'},'1-r':{n:'base vel',v:'--',c:'cr'}};
/* TODO: REWRITE — rear pot has 2 modes: alone=LED brightness, +hold rear=Sensitivity.
   Current code wrongly puts sensitivity and tempo here. */
let rearD=[{n:'sensitivity',v:'--',c:'ca',id:'rc-l'},{n:'tempo',v:'--',c:'nu',id:'rc-0'},
  {n:'—',v:'—',e:1,c:'',id:'rc-r'}];

/* ===== WEBSOCKET ===== */
let ws=null;
function wsConnect(){
  const host=location.host||'192.168.4.1';
  ws=new WebSocket('ws://'+host+'/ws');
  ws.onopen=()=>setDot('d-ws',true);
  ws.onclose=()=>{setDot('d-ws',false);setTimeout(wsConnect,2000);};
  ws.onmessage=e=>{resetWsTimer();try{handleMsg(JSON.parse(e.data));}catch(ex){}};
}
function resetWsTimer(){setDot('d-ws',true);clearTimeout(wsTm);wsTm=setTimeout(()=>setDot('d-ws',false),3000);}
function setDot(id,on){const el=document.getElementById(id);if(el){el.classList.toggle('dot-on',on);el.classList.toggle('dot-off',!on);}}

function handleMsg(m){
  switch(m.type){
    /* TODO: 'banks' and 'state' now carry velocity{base,variation}, pitchBend, arp.quantize — store + display */
    case 'banks':banks=m.slots;act=m.active;rBar();rTri();rPots();break;
    case 'state':act=m.bank;updateBankState(m);rBar();rTri();rPots();if(holdBtn!=='none')rSvg();break;
    case 'tempo':
      document.getElementById('bpm').textContent=m.bpm;
      const src=document.getElementById('bsrc');
      src.textContent=m.source==='int'?'internal tempo':m.source+' sync';
      src.classList.toggle('internal',m.source==='int');
      rearD[1].v=m.bpm;rRear();break;
    case 'beat':flashBpm();break;
    case 'battery':document.getElementById('bat').textContent=m.percent+'%';break;
    case 'connections':setDot('d-usb',m.usb);setDot('d-ble',m.ble);break;
    case 'pot':updatePot(m);break;
    case 'error':errors[m.code]=m.message;rErrors();break;
    case 'error_clear':delete errors[m.code];rErrors();break;
    /* TODO: only 'left' button triggers SVG overlay now. Remove 'right' handling. */
    case 'button_hold':holdBtn=m.held?m.button:'none';toggleSvgView();break;
    case 'pad_roles':padRoles=m;if(holdBtn!=='none')rSvg();break;
    case 'pad_notes':padNotes=m.notes;if(holdBtn!=='none')rSvg();break;
    case 'hb':break;
  }
}

function updateBankState(m){
  if(!banks[m.bank])return;
  const b=banks[m.bank];
  b.root=m.scale.root;b.mode=m.scale.mode;b.chrom=m.scale.chromatic;
  /* TODO: store m.velocity.base, m.velocity.variation, m.pitchBend */
  if(m.arp){b.play=m.arp.playing;b.hold=m.arp.hold;b.pat=m.arp.pattern;b.oct=m.arp.octave;b.n=m.arp.notes;}
  /* TODO: store m.arp.quantize */
}
function updatePot(m){
  /* TODO: slot IDs changed — now r1-0, r1-l, r2-0, etc. Update lookup + cell IDs. */
  const pm=banks[act]&&banks[act].type==='A'?potA:potN;
  if(pm[m.slot])pm[m.slot].v=m.value;
  rPots();zoomCell('pc-'+m.slot);
  if(holdBtn!=='none'){holdBtn='none';toggleSvgView();}
}

/* ===== VIEW TOGGLE ===== */
function toggleSvgView(){
  const svEl=document.getElementById('v-svg');
  const stEl=document.getElementById('v-status');
  if(holdBtn!=='none'){
    stEl.style.display='none';
    svEl.classList.add('show');
    rSvg();
  } else {
    svEl.classList.remove('show');
    stEl.style.display='block';
  }
}

/* ===== BPM FLASH ===== */
function flashBpm(){const el=document.getElementById('bpm');el.classList.add('flash');setTimeout(()=>el.classList.remove('flash'),150);}

/* ===== ZOOM ===== */
function zoomCell(id){
  if(litId){const o=document.getElementById(litId);if(o){o.classList.remove('lit');o.classList.add('unlit');setTimeout(()=>o.classList.remove('unlit'),500);}}
  const el=document.getElementById(id);
  if(el&&!el.classList.contains('empty')){
    el.classList.remove('unlit');el.classList.add('lit');litId=id;clearTimeout(litTm);
    litTm=setTimeout(()=>{el.classList.remove('lit');el.classList.add('unlit');setTimeout(()=>el.classList.remove('unlit'),500);litId=null;},2500);
  }
}

/* ===== ERRORS ===== */
function rErrors(){
  const bar=document.getElementById('errbar');const keys=Object.keys(errors);
  if(!keys.length){bar.classList.remove('show');bar.innerHTML='';return;}
  bar.innerHTML=keys.map(k=>'<div>⚠ '+errors[k]+'</div>').join('');
  bar.classList.add('show');
}

/* ===== RENDER: REAR ===== */
function rRear(){
  let h='<div class="rr-lbl rr-ll">btn left</div><div class="rr-cells">';
  rearD.forEach(s=>{h+='<div class="rr-c '+(s.e?'empty':'')+' '+(s.c||'')+'" id="'+s.id+'"><span class="rr-n">'+s.n+'</span><span class="rr-v">'+(s.e?'—':s.v)+'</span></div>';});
  h+='</div><div class="rr-lbl rr-lr">btn right</div>';
  document.getElementById('rear').innerHTML=h;
}

/* ===== RENDER: BANKS ===== */
function sl(b){return b.chrom?'Chrom':(b.root||'?')+' '+(b.mode||'?').slice(0,3);}
function rBar(){
  document.getElementById('bar').innerHTML=banks.map((b,i)=>{
    let c='bk';if(i===act)c+=' act';if(b.type==='A'&&b.play&&i!==act)c+=' pulse';
    const d=(b.type==='A'&&b.play)?'<div class="bk-dt"></div>':'';
    return '<div class="'+c+'" onclick="sel('+i+')">'+d+'<div class="bk-ch">'+(i+1)+'</div><div class="bk-bar '+(b.type==='A'?'bk-ba':'bk-bn')+'"></div><div class="bk-sc">'+sl(b)+'</div></div>';
  }).join('');
}

/* ===== RENDER: 3 COLUMNS ===== */
/* TODO: Add velocity (base±var), pitchBend offset (NORMAL), quantize mode (ARPEG) to display. */
function rTri(){
  const b=banks[act];if(!b)return;
  let left='<div class="co-big">Bank '+(act+1)+'</div><div class="co-sub">Channel '+(act+1)+'</div>';
  if(b.type==='A')left+='<div class="co-st '+(b.play?'st-p':'st-s')+'">'+(b.play?'▶ Playing':'■ Stopped')+'</div>';
  let mid='<div class="co-tp '+(b.type==='A'?'tp-a':'tp-n')+'">'+(b.type==='A'?'ARPEG':'NORMAL')+'</div>';
  if(b.type==='A'){
    mid+='<div class="ar-chips"><div class="ar-c"><div class="ar-cv">'+(b.pat||'—')+'</div><div class="ar-cl">pattern</div></div></div>';
    mid+='<div class="ar-chips"><div class="ar-c"><div class="ar-cv">'+(b.oct||'—')+'</div><div class="ar-cl">oct</div></div><div class="ar-c"><div class="ar-cv">'+(b.n||0)+'</div><div class="ar-cl">notes</div></div></div>';
    mid+='<div class="hl-tag '+(b.hold?'hl-on':'')+'">HOLD '+(b.hold?'on':'off')+'</div>';
  } else mid+='<div class="co-sub" style="margin-top:6px">Poly-aftertouch</div><div class="co-sub">Velocity 127</div>';
  let right='<div class="co-big">'+(b.root||'—')+'</div><div class="co-sub">root</div><div style="margin-top:8px"><div class="co-big" style="font-size:16px">'+(b.chrom?'Chromatic':(b.mode||'—'))+'</div><div class="co-sub">mode</div></div>';
  document.getElementById('tri').innerHTML='<div class="co">'+left+'</div><div class="co">'+mid+'</div><div class="co">'+right+'</div>';
}

/* ===== RENDER: POTS ===== */
/* TODO: REWRITE — 4 columns (right 1-4) × 2 rows (alone / +hold left).
   Remove corail color class ('cr') — no btn right modifier on right pots.
   Add separate rear pot render (alone=brightness, +hold rear=sensitivity). */
function mkC(p,c,pm){const k=p+'-'+c;const d=pm[k];if(!d)return'<div class="pg-c empty"></div>';
  return'<div class="pg-c '+(d.e?'empty':'')+' '+(d.c||'')+'" id="pc-'+k+'"><span class="pg-n">'+d.n+'</span><span class="pg-v">'+d.v+'</span></div>';}
function rPots(){
  const b=banks[act];const pm=(b&&b.type==='A')?potA:potN;
  let h='<div class="pg-hd"><div></div><div class="pg-hl">↺ pot left</div><div class="pg-hl">↺ pot right</div><div></div></div>';
  h+='<div class="pg-rw"><div></div>'+mkC(0,'0',pm)+mkC(1,'0',pm)+'<div></div></div>';
  h+='<div class="pg-rw"><div class="pg-sd pg-sl">btn left</div>'+mkC(0,'l',pm)+mkC(1,'l',pm)+'<div></div></div>';
  h+='<div class="pg-rw"><div></div>'+mkC(0,'r',pm)+mkC(1,'r',pm)+'<div class="pg-sd pg-sr">btn right</div></div>';
  document.getElementById('pg').innerHTML=h;
}

/* ===== RENDER: SVG PAD VIEW (V1.5) ===== */
const noteNames=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
function midiName(n){return n===255?'—':noteNames[n%12]+(Math.floor(n/12)-1);}

/* TODO: REWRITE NEEDED —
   - Remove ap.patterns (patterns are via pot, not pads)
   - ap.octave is now an array of 4 (was single value), label Oct1-Oct4
   - Total control pads: 8 bank + 7 root + 7 mode + 1 chrom + 1 hold + 1 playStop + 4 octave = 29 */
function getRoleInfo(padIdx){
  if(!padRoles)return null;
  const r=padRoles;
  let i;
  i=r.bankPads.indexOf(padIdx);if(i!==-1)return{role:'bank',label:'B'+(i+1),cls:'bank'};
  i=r.rootPads.indexOf(padIdx);if(i!==-1){const names=['A','B','C','D','E','F','G'];return{role:'root',label:names[i],cls:'root'};}
  i=r.modePads.indexOf(padIdx);if(i!==-1){const names=['Ion','Dor','Phr','Lyd','Mix','Aeo','Loc'];return{role:'mode',label:names[i],cls:'mode'};}
  if(padIdx===r.chromPad)return{role:'chrom',label:'Chr',cls:'chrom'};
  const ap=r.arpPads;
  i=ap.patterns.indexOf(padIdx);if(i!==-1){const names=['Up','Dn','U-D','Rnd','Ord'];return{role:'arp',label:names[i],cls:'arp'};}
  if(padIdx===ap.octave)return{role:'arp',label:'Oct',cls:'arp'};
  if(padIdx===ap.hold)return{role:'arp',label:'Hld',cls:'arp'};
  if(padIdx===ap.playStop)return{role:'playstop',label:'▶/■',cls:'playstop'};
  return null;
}

/* TODO: REWRITE — single-layer: left hold shows ALL roles at once (bank+root+mode+chrom+arp+playstop).
   Remove holdBtn==='left'/'right' branching. All roles colored simultaneously. */
function rSvg(){
  document.querySelectorAll('.sv-label').forEach(e=>e.remove());
  for(let i=0;i<48;i++){
    const el=document.getElementById('p'+i);if(!el)continue;
    el.className='';
    const ri=getRoleInfo(i);
    if(!ri)continue;
    if(ri.role==='playstop'){el.classList.add('role-playstop');continue;}
    if(holdBtn==='left'&&ri.role==='bank')el.classList.add('role-bank');
    else if(holdBtn==='right'){
      if(ri.role==='root')el.classList.add('role-root');
      else if(ri.role==='mode')el.classList.add('role-mode');
      else if(ri.role==='chrom')el.classList.add('role-chrom');
      else if(ri.role==='arp')el.classList.add('role-arp');
    }
  }
  placeSvgLabels();
}

function placeSvgLabels(){
  const svg=document.getElementById('svmain');
  const wrap=document.getElementById('v-svg');
  if(!svg||!wrap)return;
  const svgRect=svg.getBoundingClientRect();
  const wrapRect=wrap.getBoundingClientRect();
  const sx=svgRect.width/708.66;
  const ox=svgRect.left-wrapRect.left;
  const oy=svgRect.top-wrapRect.top;

  for(let i=0;i<48;i++){
    const el=document.getElementById('p'+i);if(!el)continue;
    const bb=el.getBBox();
    const cx=ox+(bb.x+bb.width/2)*sx;
    const cy=oy+(bb.y+bb.height/2)*sx;
    const area=bb.width*bb.height*sx*sx;
    const isSmall=area<600;
    const ri=getRoleInfo(i);
    const note=padNotes?midiName(padNotes[i]):'';
    const isPlayStop=ri&&ri.role==='playstop';
    const isActiveRole=isPlayStop||(holdBtn==='left'&&ri&&ri.role==='bank')||
      (holdBtn==='right'&&ri&&['root','mode','chrom','arp'].includes(ri.role));

    const div=document.createElement('div');
    div.className='sv-label';
    div.style.left=cx+'px';div.style.top=cy+'px';div.style.transform='translate(-50%,-50%)';

    if(isActiveRole){
      const sz=isSmall?11:13;const nsz=isSmall?7:9;
      div.innerHTML='<span class="sv-role sv-role-'+ri.cls+'" style="font-size:'+sz+'px">'+ri.label+'</span>'
        +'<span class="sv-note" style="font-size:'+nsz+'px;margin-top:3px">'+note+'</span>';
    } else {
      const nsz=isSmall?7:9;
      div.innerHTML='<span class="sv-note" style="font-size:'+nsz+'px">'+note+'</span>';
    }
    wrap.appendChild(div);
  }
}

/* Tooltip on hover */
function initSvgTooltips(){
  for(let i=0;i<48;i++){
    const el=document.getElementById('p'+i);if(!el)continue;
    el.addEventListener('mouseenter',function(){
      const ri=getRoleInfo(i);
      const note=padNotes?midiName(padNotes[i]):'?';
      const tt=document.getElementById('tooltip');
      document.getElementById('tt-note').textContent=note;
      const roleEl=document.getElementById('tt-role');
      if(ri){roleEl.textContent=ri.role.toUpperCase()+' — '+ri.label;roleEl.className='sv-tt-role sv-role-'+ri.cls;}
      else{roleEl.textContent='Musical pad';roleEl.className='sv-tt-role';roleEl.style.color=getComputedStyle(document.documentElement).getPropertyValue('--t2');}
      document.getElementById('tt-pad').textContent='Pad #'+i;
      const bb=el.getBBox();const svg=document.getElementById('svmain');
      const svgR=svg.getBoundingClientRect();const wR=document.getElementById('v-svg').getBoundingClientRect();
      const sx2=svgR.width/708.66;
      tt.style.left=(svgR.left-wR.left+(bb.x+bb.width/2)*sx2)+'px';
      tt.style.top=(svgR.top-wR.top+bb.y*sx2-50)+'px';
      tt.style.transform='translateX(-50%)';tt.style.opacity='1';
    });
    el.addEventListener('mouseleave',function(){document.getElementById('tooltip').style.opacity='0';});
  }
}

function sel(i){act=i;rBar();rTri();rPots();}

/* ===== INIT ===== */
rRear();
wsConnect();
initSvgTooltips();
</script>
</body>
</html>
```

> **Note pour l'implémentation** : Le placeholder `<!-- 48 paths inlined here -->` dans le SVG doit être remplacé par les 48 `<path>` du fichier `Unknown.svg` (chaque path avec son id `p0`-`p47`). Le fichier SVG fait ~10 KB et est fourni séparément.

---

## 9. Mapping SVG Path → Pad Index

Les 48 paths dans le SVG sont dans l'ordre du fichier Illustrator, **pas** dans l'ordre électrique des capteurs. Un mapping doit être établi une fois sur le hardware :

**Méthode** : toucher chaque pad physique un par un, l'ESP32 envoie "pad N touched", on note quel path SVG correspond. 48 entrées, codé en dur dans le message `pad_roles` ou dans la page.

Ce mapping est **constant** (lié au PCB, pas à la config utilisateur) et ne change jamais.

---

## 10. Impact Système

| Ressource | `WIFI_STATUS_ENABLED 0` | `WIFI_STATUS_ENABLED 1` |
|---|---|---|
| RAM | +0 | +~55 KB (WiFi stack ~40KB, AsyncWebServer ~8KB, WS buffers ~4KB, StatusPoller ~3KB) |
| CPU Core 1 | +0 | +~1-2% (polling + diff, pas de sérialisation si pas de client) |
| Batterie | +0 | +~80-120 mA (WiFi AP actif) |
| BLE perf | baseline | À mesurer — dégradation possible (radio partagée), voir Tool 6 |
| Flash | +0 (linker dead code) | +~100 KB (ESPAsyncWebServer + AsyncTCP) + ~10 KB (page gzippée) |

> **RAM** : 55KB sur 320KB SRAM disponibles (usage actuel ~16KB). Large marge.
> **Flash** : 110KB sur 8MB. Négligeable.
> **BLE** : le vrai risque. ESP32-S3 partage le radio WiFi/BLE. Le Tool 6 devra permettre de configurer la coexistence (priorité BLE, réduction puissance WiFi, etc.).

---

## 11. Roadmap

### V1 — Status Page (rappel visuel)

Header (BPM statique, connexions USB/BLE/WS, batterie), ligne pot arrière (brightness/sensitivity), barre 8 banks (pulse arp background), 3 colonnes détail (bank info, type + params, scale), grille 4 pots × 2 combos (zoom ×2, ambre pour +hold left), overlay erreur.

### V1.5 — SVG Pad View

Vue SVG overlay sur hold bouton gauche. **Tous les 29 rôles visibles simultanément** (single-layer) : bleu bank, ambre root, corail mode, blanc chrom, teal arp/hold/octave, magenta play/stop. Notes résolues sur chaque pad. Tooltip au hover.

### V2 — Futur (hors scope)

- Web UI Setup (remplacement VT100 via même page web).
- Contrôle distant (WebSocket bidirectionnel).
- mDNS (`http://illpad.local`).
- OTA firmware update.

---

## 12. Tool 6 — WiFi Settings (Setup Mode)

> **Future** — à concevoir quand le WiFi sera implémenté. Idées initiales ci-dessous.

### Position dans le Setup

```
[1] Pressure Calibration
[2] Pad Ordering
[3] Pad Roles
[4] Bank Config
[5] Settings
[6] WiFi Status              ← NOUVEAU
[0] Reboot
```

### Paramètres envisagés

| Paramètre | Valeurs | Défaut | Stockage NVS |
|---|---|---|---|
| WiFi Enable | On / Off | Off | `illpad_set` |
| WiFi TX Power | Low / Medium / High | Low | `illpad_set` |
| BLE Priority | BLE-first / Balanced / WiFi-first | BLE-first | `illpad_set` |
| SSID | Editable string | `ILLPAD48` | `illpad_wifi` |
| Password | Editable string | `illpad48` | `illpad_wifi` |

### Questions ouvertes (à résoudre avant implémentation)

1. **WiFi on/off runtime vs compile-time** : le `#define WIFI_STATUS_ENABLED` est compile-time. Le Tool 6 serait runtime (NVS). Les deux coexistent : compile-time = "le code est présent", runtime = "le WiFi démarre ou pas au boot". Si `WIFI_STATUS_ENABLED == 0`, le Tool 6 n'apparaît même pas dans le menu.

2. **BLE coexistence** : `esp_coex_preference_set(ESP_COEX_PREFER_BT)` priorise le BLE. Faut-il exposer ce choix à l'utilisateur ou hardcoder BLE-first ?

3. **Impact batterie** : WiFi AP consomme ~100mA. Faut-il un auto-off après N minutes d'inactivité (pas de client WS connecté) ?

4. **Sécurité** : WPA2 avec mot de passe par défaut. Suffisant pour un AP local sans accès internet ? Faut-il permettre le changement de mot de passe ?

5. **Environnement PlatformIO** : pour un build sans WiFi à zéro overhead (pas même les libs compilées), créer un `[env:esp32-s3-no-wifi]` séparé sans les `lib_deps` WiFi.
