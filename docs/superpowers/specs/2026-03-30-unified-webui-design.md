# ILLPAD48 V2 — Unified WebUI Design

> WebUI unifiee et bidirectionnelle : page Live (status + controle) et page Setup (Tools 1-7).
> Remplace le terminal VT100 comme chemin principal. VT100 reste en fallback expert.
> Priorite : P3 (apres stabilisation firmware + refactor BLE).
> Date : 30 mars 2026.

---

## 1. Vue d'Ensemble

### Objectif

Une WebUI servie par l'ESP32 en WiFi AP qui couvre deux cas d'usage :

1. **Page Live** (mode jeu) — status temps reel + controle bidirectionnel des banks (scale, arp start/stop, hold sur toutes banks ; params pot sur banks background uniquement)
2. **Page Setup** (mode setup) — remplacement complet du terminal VT100 pour les Tools 1-7 (calibration, pad ordering, pad roles, bank config, settings, pot mapping, LED settings)

### Prerequis

- **Refactor BLE** : supprimer l'aftertouch BLE en amont. Garder BLE notes/CC uniquement. Reduit le trafic radio et rend la coexistence WiFi/BLE plus simple.
- **Draft WiFi V1** (`docs/drafts/wifi-status-page.md`) : la page Live V1 (read-only) est la base. Ce design l'etend au bidirectionnel et ajoute le Setup web.

### Architecture Haut Niveau

```
Mode jeu   → ESP32 sert /   → Page Live (status + controle bidir)
Mode setup → ESP32 sert /   → Page Setup (Tools 1-7 en onglets)
```

L'ESP32 sait dans quel mode il est (jeu ou setup) et sert la page correspondante sur la meme URL `/`. Les deux pages sont des fichiers HTML autonomes, sans couplage. Le navigateur charge `/`, obtient la bonne page, connecte le WebSocket.

Un reboot (passage jeu → setup ou inverse) coupe le WS. Le navigateur reconnecte et recoit la nouvelle page.

---

## 2. WiFi Infrastructure

### Activation

- **Compile-time** : `#define WIFI_STATUS_ENABLED 1` dans `platformio.ini`. Quand 0, zero code WiFi compile.
- **Runtime** : configurable dans Tool 5 (Settings) / page Setup.

### Lifecycle

| Alimentation | Comportement WiFi |
|---|---|
| **USB branche** | WiFi AP toujours actif |
| **Batterie** | WiFi off par defaut. Activation par geste physique (a definir . ). Auto-off apres N minutes sans client WS connecte. |

### AP Configuration

- SSID : `ILLPAD48` (configurable via Setup)
- Password : `illpad48` (configurable via Setup)
- IP : `192.168.4.1`
- mDNS (optionnel) : `http://illpad.local`

### Coexistence WiFi/BLE

- `esp_coex_preference_set(ESP_COEX_PREFER_BT)` — BLE prioritaire toujours
- WiFi TX power reduite (~11dBm) pour minimiser l'interference radio
- Beacon interval augmente (300ms) pour reduire le radio time WiFi
- BLE sans aftertouch = trafic leger, coexistence triviale avec ces reglages

### Stockage Pages

Les deux pages HTML (Live + Setup) sont stockees en **PROGMEM** (gzippees dans le firmware).

- Estimation : ~30 KB gzippe total (~8 KB Live + ~22 KB Setup avec 7 Tools + SVG)
- 8 MB de flash disponibles — aucune contrainte
- Avantage : firmware = UI toujours synchronises, zero risque de version mismatch
- Inconvenient : chaque modif UI = reflash firmware (acceptable en phase prototype)

---

## 3. Page Live — Status + Controle

### Composants Visuels

La page Live reprend le design du draft WiFi V1 (Section 3 du draft) :

- **Header** : ILLPAD48, dots connexion (USB, BLE, WS), BPM statique, batterie
- **Barre de Banks** : 8 cellules, type (NORMAL/ARPEG), scale resume, pulse arp background
- **3 Colonnes** : detail bank active (info, type+params, scale)
- **Grille Pots** : 4 right pots x 2 lignes (seul / +hold left), zoom x2 sur changement
- **Ligne Pot Arriere** : brightness / sensitivity
- **SVG Pad View** : overlay sur hold bouton gauche (tous les 29 roles visibles)
- **Overlay Erreur** : bande rouge pour erreurs systeme

### Controle Bidirectionnel (V2)

La page Live ajoute des **controles interactifs** sur les elements visuels existants.

#### Regles de Souverainete

| Action | Bank active | Banks background |
|---|---|---|
| Switch bank (foreground) | ILLPAD only | — |
| Tempo | ILLPAD only | — |
| Pots (gate, shuffle, division, pattern, velocity, PB) | ILLPAD only | WebUI oui |
| Scale (root, mode, chrom) | WebUI oui | WebUI oui |
| Arp start/stop | WebUI oui | WebUI oui |
| Arp hold toggle | WebUI oui | WebUI oui |
| Arp octave range | WebUI oui | WebUI oui |

Mnemonique : **les pots physiques de la bank active restent physiques**. Tout le reste est controlable depuis le web.


#### Controles UI

Les elements suivants deviennent cliquables/interactifs sur la page Live :

| Element | Action |
|---|---|
| Cellule bank (barre de banks) | Tap = affiche detail de cette bank dans les 3 colonnes (ne change PAS le foreground sur l'ILLPAD) |
| Badge Play/Stop (colonne gauche, ARPEG) | Tap = start/stop arp de cette bank |
| Badge HOLD on/off | Tap = toggle hold de cette bank |
| Root (colonne droite) | Tap = cycle root (A-G) sur cette bank |
| Mode (colonne droite) | Tap = cycle mode (Ion-Loc) sur cette bank |
| Badge Chromatic | Tap = toggle chromatic sur cette bank |
| Chips octave (colonne centre, ARPEG) | Tap = cycle octaves (1-4) |
| Cellule pot (banks background) | Slider/tap = ajuster la valeur |

**Distinction importante** : taper une bank dans la barre NE switch PAS le foreground sur l'ILLPAD. Ca change juste quelle bank est affichee en detail sur la tablette. Le foreground reste celui choisi physiquement.
Un mode follow ( dans la webui ) permet de faire suivre la banque foreground de l'iilpad ( mais ne lock pas dessus )

#### Pot Controles (Banks Background)

Pour les banks background, les cellules pot de la grille deviennent interactives :
- Tap sur la cellule → slider horizontal ou vertical pour ajuster
- La valeur est envoyee par commande WS
- Le retour visuel vient du flux d'etat (la cellule se met a jour quand l'ESP32 confirme)

Pour la bank active, les cellules pot sont **read-only** (affichent la valeur du pot physique, non modifiables).

---

## 4. Page Setup — Tools 1-7

### Acces

L'ILLPAD entre en mode setup par le geste physique habituel (boot + rear button hold 3s). L'ESP32 sert alors la page Setup sur `/` a la place de la page Live.

### Structure

La page Setup est une **page HTML unique avec onglets internes** (un par Tool). Pas de rechargement de page entre les Tools — le WebSocket reste connecte.

```
┌─────────────────────────────────────────────────────┐
│  ILLPAD48 SETUP                           [Reboot]  │
├──┬──┬──┬──┬──┬──┬──┬───────────────────────────────┤
│ 1│ 2│ 3│ 4│ 5│ 6│ 7│                               │
├──┴──┴──┴──┴──┴──┴──┤                               │
│                      │                               │
│   Contenu du Tool    │                               │
│   selectionne        │                               │
│                      │                               │
└──────────────────────┴───────────────────────────────┘
```

### Coexistence VT100 / Web

- Un seul canal de setup actif a la fois
- Au boot setup, l'ESP32 attend sur les deux canaux (serial + WS)
- Le premier qui envoie une commande verrouille le canal
- L'autre canal recoit un message "Setup actif sur [web/serial]"
- Le verrou se relache si le canal actif se deconnecte (WS close / serial timeout)

### Tool 1 — Pressure Calibration (Web)

**Streaming pression** : l'ESP32 envoie les valeurs de pression brutes par WebSocket a ~20Hz par pad touche.

```json
{ "type": "cal_pressure", "pad": 14, "raw": 847, "max": 1023 }
```

La WebUI affiche :
- 48 cellules (grille ou SVG) avec barres de pression en temps reel
- Indicateur maxDelta par pad (valeur la plus haute atteinte)
- Bouton "Confirm pad" (ou tap sur la cellule) pour valider la calibration
- Bouton "Reset all" pour recommencer
- Bouton "Save" pour ecrire les seuils en NVS

Latence WiFi (~10-50ms) acceptable pour la calibration — l'utilisateur regarde une barre monter/descendre, pas un instrument temps reel.

### Tool 2 — Pad Ordering (Web)

L'utilisateur touche les pads du plus grave au plus aigu. L'ESP32 detecte le touch et envoie :

```json
{ "type": "pad_touch", "pad": 14, "position": 7 }
```

La WebUI affiche le SVG des 48 pads, chaque pad montre son numero d'ordre assigne. Les pads non assignes sont gris, les assignes sont colores. L'utilisateur touche physiquement les pads dans l'ordre.

### Tool 3 — Pad Roles (Web)

Formulaire interactif : 29 roles a assigner aux 48 pads.

- SVG des 48 pads affiche avec couleurs par role (meme palette que la vue SVG Live)
- Panneau lateral avec les roles disponibles (bank 1-8, root A-G, mode Ion-Loc, chrom, hold, play/stop, octave 1-4)
- Tap sur un pad + tap sur un role = assignation
- Ou : tap sur un pad physique pour le selectionner, puis tap sur un role dans le panneau
- Verification des collisions en temps reel (un pad ne peut avoir qu'un seul role)
- Sauvegarde NVS sur confirmation

### Tool 4 — Bank Config (Web)

Le plus simple : 8 banks, chacune avec :
- Toggle NORMAL / ARPEG (max 4 ARPEG)
- Select quantize mode (Immediate / Beat / Bar) pour les banks ARPEG
- Sauvegarde NVS sur confirmation

### Tool 5 — Settings (Web)

Formulaire de parametres :
- Profile (preset de reglages)
- Aftertouch rate
- BLE connection interval
- Clock mode (master / slave)
- Double-tap duration (100-250ms)
- Pot bargraph duration (1-10s)
- Panic on BLE reconnect (on/off)
- Battery ADC calibration
- WiFi settings (enable, TX power, SSID, password)

### Tool 6 — Pot Mapping (Web)

Interface pour assigner des parametres aux 8 slots pot (4 pots x 2 layers, par contexte NORMAL/ARPEG).

- 2 onglets : NORMAL / ARPEG
- 8 cellules par contexte (4 pots x 2 combos: seul / +hold left)
- Chaque cellule = select parmi le pool de params disponibles
- MIDI CC : champ CC# editable
- MIDI Pitchbend : max un par contexte
- Couleurs : vert = disponible, gris = deja assigne
- Reset to defaults par contexte
- Sauvegarde NVS sur confirmation

### Tool 7 — LED Settings (Web)

2 sections (comme les 2 pages VT100 actuelles) :

**Section 1 — Couleurs + Timing** :
- 13 slots couleur editables (preset + hue offset)
- Intensites 0-100% pour chaque etat LED
- Timings (pulse period, flash duration, etc.)
- Preview live sur les LEDs physiques (commande WS → ESP32 → LEDs)

**Section 2 — Confirmations** :
- 10 types de confirmation (bank switch, scale root, etc.)
- Couleur, pattern, duree par type
- Preview via bouton "Test" (declenche le blink sur les LEDs physiques)

---

## 5. Protocole WebSocket — Bidirectionnel

### Canal ESP32 → Client (existant, etendu)

Reprend tous les messages du draft V1 (Section 5 du draft) :
- `banks`, `state`, `tempo`, `battery`, `connections`, `pot`, `error`, `error_clear`, `button_hold`, `pad_roles`, `pad_notes`, `hb`

Ajouts pour le Setup :
- `cal_pressure` — stream pression brute (Tool 1)
- `pad_touch` — detection touch pour ordering (Tool 2)
- `setup_state` — etat complet du setup (config actuelle de chaque Tool, envoye au connect WS en mode setup)
- `setup_ack` — confirmation que la commande setup a ete appliquee (optionnel, le flux d'etat suffit generalement)

### Canal Client → ESP32 (nouveau)

#### Commandes Live (mode jeu)

```json
{ "cmd": "arp_start", "bank": 2 }
{ "cmd": "arp_stop", "bank": 2 }
{ "cmd": "arp_hold", "bank": 2, "on": true }
{ "cmd": "scale_root", "bank": 0, "root": 3 }
{ "cmd": "scale_mode", "bank": 0, "mode": 2 }
{ "cmd": "scale_chrom", "bank": 0, "on": true }
{ "cmd": "arp_octave", "bank": 2, "octave": 3 }

// Params pot (banks background uniquement)
{ "cmd": "arp_gate", "bank": 2, "value": 75 }
{ "cmd": "arp_shuffle_depth", "bank": 2, "value": 40 }
{ "cmd": "arp_shuffle_template", "bank": 2, "value": 2 }
{ "cmd": "arp_division", "bank": 2, "value": 5 }
{ "cmd": "arp_pattern", "bank": 2, "pattern": 1 }
{ "cmd": "velocity_base", "bank": 0, "value": 100 }
{ "cmd": "velocity_var", "bank": 0, "value": 25 }
{ "cmd": "pitch_bend", "bank": 0, "value": 12 }
```

#### Commandes Setup (mode setup)

```json
// Tool 1 — Calibration
{ "cmd": "cal_confirm", "pad": 14 }
{ "cmd": "cal_reset" }
{ "cmd": "cal_save" }

// Tool 2 — Pad Ordering
{ "cmd": "order_confirm", "pad": 14 }
{ "cmd": "order_reset" }
{ "cmd": "order_save" }

// Tool 3 — Pad Roles
{ "cmd": "role_assign", "pad": 14, "role": "bank", "index": 3 }
{ "cmd": "role_clear", "pad": 14 }
{ "cmd": "role_save" }

// Tool 4 — Bank Config
{ "cmd": "bank_type", "bank": 2, "type": "A" }
{ "cmd": "bank_quantize", "bank": 2, "mode": "Beat" }
{ "cmd": "bank_save" }

// Tool 5 — Settings
{ "cmd": "setting", "key": "double_tap", "value": 150 }
{ "cmd": "settings_save" }

// Tool 6 — Pot Mapping
{ "cmd": "pot_assign", "context": "ARPEG", "slot": 3, "target": "shuffle_depth" }
{ "cmd": "pot_cc", "context": "NORMAL", "slot": 2, "cc": 74 }
{ "cmd": "pot_reset_defaults", "context": "ARPEG" }
{ "cmd": "pot_save" }

// Tool 7 — LED Settings
{ "cmd": "led_color", "slot": 3, "hue": 180 }
{ "cmd": "led_intensity", "state": "arp_bg_min", "value": 8 }
{ "cmd": "led_timing", "param": "pulse_period", "value": 1500 }
{ "cmd": "led_preview", "type": "CONFIRM_BANK_SWITCH" }
{ "cmd": "led_save" }
```

### Protocole Fire-and-Forget

- Le client envoie la commande JSON
- L'ESP32 parse, valide, met en queue (dirty-flag pattern)
- La loop applique au prochain cycle (~1-2ms)
- Le StatusPoller detecte le changement et pousse le nouvel etat au client
- Le client met a jour l'UI en recevant le message d'etat
- Pas d'ack/nack explicite — le flux d'etat sert de confirmation

### Validation Cote ESP32

La `WebSocketCommandQueue` valide avant de mettre en queue :
- `bank` dans range 0-7
- `value` dans les bornes du parametre
- Commande pot sur bank active → rejetee silencieusement (regle de souverainete)
- Commande setup en mode jeu → rejetee
- Commande live en mode setup → rejetee

---

## 6. Architecture ESP32

### Fichiers

```
src/wifi/
├── WifiStatusServer.cpp/.h     # AP + HTTP + WebSocket (infra reseau)
├── StatusPoller.cpp/.h         # Lit managers, diff, serialise JSON (ESP32→client)
├── CommandHandler.cpp/.h       # Parse JSON, valide, queue dirty-flag (client→ESP32)
├── SetupWsHandler.cpp/.h      # Logique setup via WS (streaming cal, pad touch, etc.)
├── status_page.h               # Page Live HTML gzippee (PROGMEM)
└── setup_page.h                # Page Setup HTML gzippee (PROGMEM)
```

### CommandHandler — Queue Dirty-Flag

Meme pattern que NvsManager :

```cpp
class CommandHandler {
public:
    // Appele depuis le callback WS (contexte LwIP — PAS la loop)
    void queueCommand(const char* json, size_t len);

    // Appele depuis loop() (contexte Core 1 — safe)
    void processQueue(BankManager& bankMgr,
                      ScaleManager& scaleMgr,
                      ArpEngine* arpEngines,
                      PotRouter& potRouter,
                      /* ... */);

private:
    // Dirty flags (atomics)
    std::atomic<bool> _scaleRootDirty[NUM_BANKS];
    std::atomic<bool> _scaleModeDirty[NUM_BANKS];
    std::atomic<bool> _arpStartDirty[NUM_BANKS];
    std::atomic<bool> _arpStopDirty[NUM_BANKS];
    // ... etc pour chaque commande

    // Pending values (types simples)
    uint8_t _pendingRoot[NUM_BANKS];
    uint8_t _pendingMode[NUM_BANKS];
    // ... etc
};
```

### Integration Loop

```cpp
void loop() {
    // 1-10. Chemin critique MIDI (inchange)
    // ...

    // 11. PotRouter.update()
    // 12. BatteryMonitor.update()
    // 13. LedController.update()
    // 14. NvsManager.notifyIfDirty()

    #if WIFI_STATUS_ENABLED
    s_commandHandler.processQueue(/* managers */);  // ~0.1ms
    s_statusPoller.update();                         // ~0.5ms si diff
    #endif

    // 15. vTaskDelay(1)
}
```

### Persistance NVS

Les changements web suivent le meme chemin que les changements physiques :
1. CommandHandler applique via les setters des managers
2. Les managers marquent leurs dirty flags (existants)
3. NvsManager detecte les dirty flags et ecrit en flash apres le delai d'inactivite habituel

Pas de logique NVS specifique au web. Le web est juste une source d'input supplementaire.

---

## 7. Impact Systeme

### RAM

| Composant | Estimation |
|---|---|
| WiFi stack (AP) | ~40 KB |
| ESPAsyncWebServer | ~8 KB |
| WebSocket buffers (1-2 clients) | ~4 KB |
| StatusPoller (snapshots) | ~3 KB |
| CommandHandler (queue) | ~1 KB |
| SetupWsHandler | ~2 KB |
| **Total** | **~58 KB** |

Sur 320 KB SRAM disponibles (usage actuel ~16 KB). Marge confortable.

### CPU Core 1

| Composant | Impact |
|---|---|
| StatusPoller.update() | ~0.5ms si diff, ~0 si pas de client |
| CommandHandler.processQueue() | ~0.1ms (quelques comparaisons atomiques) |
| **Total** | **~1-2% CPU supplementaire** |

### Flash

| Composant | Taille |
|---|---|
| ESPAsyncWebServer + AsyncTCP | ~100 KB |
| Page Live gzippee | ~8 KB |
| Page Setup gzippee | ~22 KB |
| CommandHandler + StatusPoller + SetupWsHandler | ~15 KB |
| **Total** | **~145 KB** |

Sur 8 MB. Negligeable.

### Batterie

| Situation | Consommation WiFi |
|---|---|
| WiFi off (batterie, pas active) | +0 mA |
| WiFi AP actif, pas de client | ~80 mA |
| WiFi AP actif + client WS | ~100-120 mA |

Auto-off apres inactivite mitige l'impact batterie.

---

## 8. Roadmap d'Implementation

### Phase 0 — Prerequis
- Refactor BLE : supprimer aftertouch, garder notes/CC only
- Ajouter getters manquants sur les managers (liste dans le draft V1 Section 7)

### Phase 1 — Page Live Read-Only (V1)
- WifiStatusServer (AP + HTTP + WS)
- StatusPoller (lecture managers, diff, push etat)
- Page Live HTML (status, sans controles interactifs)
- Correspond au draft WiFi V1 existant

### Phase 2 — Page Live Bidirectionnelle (V2)
- CommandHandler (queue dirty-flag, parse JSON)
- Integration loop (processQueue)
- Controles interactifs sur la page Live (arp start/stop, scale, hold, params banks background)
- Regles de souverainete (rejet commandes pots bank active)

### Phase 3 — Page Setup (V3)
- SetupWsHandler
- Detection canal actif (web vs serial)
- Page Setup HTML (shell + onglets)
- Tool 4 (Bank Config) — le plus simple, pour valider l'archi
- Tool 5 (Settings)

### Phase 4 — Setup Complet
- Tool 3 (Pad Roles) — SVG interactif
- Tool 6 (Pot Mapping)
- Tool 7 (LED Settings) — avec preview live

### Phase 5 — Setup Touch
- Tool 2 (Pad Ordering) — streaming touch detection
- Tool 1 (Pressure Calibration) — streaming pression ~20Hz

### Phase 6 — Polish
- mDNS (`http://illpad.local`)
- WiFi auto-off batterie
- Geste physique WiFi on/off
- Configuration WiFi via Setup (SSID, password, TX power)

---

## 9. Questions Ouvertes

1. **Geste physique WiFi on/off** : quel geste exactement ? Double-press rear ? Combo left+rear ? A definir quand les boutons sont finalises.

2. **Delai auto-off WiFi sur batterie** : combien de minutes sans client WS ? 5 min ? 10 min ? Configurable dans Settings ?

3. **Nombre max de clients WS** : 1 seul (une tablette) ou 2+ (tablette + telephone) ? Recommendation : 1 seul pour simplifier. Si 2e client se connecte, le 1er est deconnecte.

4. **OTA firmware update** : mentionne dans le draft V1 comme V2 futur. Hors scope de ce design mais l'infra WiFi le rend possible.

5. **SVG des 48 pads** : le mapping SVG path → pad index doit etre calibre une fois sur le hardware. Methode : toucher chaque pad, noter la correspondance. Codage en dur ensuite.
