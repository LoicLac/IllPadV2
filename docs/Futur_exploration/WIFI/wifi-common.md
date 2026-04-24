# ILLPAD48 — WiFi Common Infrastructure (Spec)

> Infrastructure WiFi partagee entre la page Live et la page Setup.
> AP, WebSocket, CommandHandler, persistance NVS, impact systeme.
> Prerequis pour `wifi-status-page.md` (Live) et `wifi-setup-page.md` (Setup).
> Derniere mise a jour : 30 mars 2026.

---

## 1. Architecture Haut Niveau

L'ESP32 expose un serveur WiFi AP. Selon le mode (jeu ou setup), il sert une page differente sur la meme URL `/`.

```
Mode jeu   → ESP32 sert /   → Page Live (status + controle bidir)
Mode setup → ESP32 sert /   → Page Setup (Tools 1-7 en onglets)
```

Les deux pages sont des fichiers HTML autonomes, sans couplage. Un reboot (passage jeu ↔ setup) coupe le WS. Le navigateur reconnecte et recoit la nouvelle page.

### Prerequis

- **Refactor BLE** : supprimer l'aftertouch BLE en amont. Garder BLE notes/CC uniquement. Reduit le trafic radio et rend la coexistence WiFi/BLE plus simple.

---

## 2. WiFi AP

### Activation

- **Compile-time** : `#define WIFI_STATUS_ENABLED 1` dans `platformio.ini`. Quand 0, zero code WiFi compile.
- **Runtime** : configurable dans Tool 5 (Settings) / page Setup.

### Lifecycle

| Alimentation | Comportement WiFi |
|---|---|
| **USB branche** | WiFi AP toujours actif |
| **Batterie** | WiFi off par defaut. Activation par geste physique (a definir). Auto-off apres N minutes sans client WS connecte. |

### Configuration AP

- SSID : `ILLPAD48` (configurable via Setup)
- Password : `illpad48` (configurable via Setup)
- IP : `192.168.4.1`
- mDNS (optionnel, phase polish) : `http://illpad.local`

### Coexistence WiFi/BLE

- `esp_coex_preference_set(ESP_COEX_PREFER_BT)` — BLE prioritaire toujours
- WiFi TX power reduite (~11dBm) pour minimiser l'interference radio
- Beacon interval augmente (300ms) pour reduire le radio time WiFi
- BLE sans aftertouch = trafic leger, coexistence triviale avec ces reglages

---

## 3. Stockage Pages — PROGMEM

Les deux pages HTML (Live + Setup) sont stockees en PROGMEM (gzippees dans le firmware).

- Estimation : ~30 KB gzippe total (~8 KB Live + ~22 KB Setup avec 7 Tools + SVG)
- 8 MB de flash disponibles — aucune contrainte
- Avantage : firmware = UI toujours synchronises, zero risque de version mismatch
- Inconvenient : chaque modif UI = reflash firmware (acceptable en phase prototype)

---

## 4. WebSocket — Transport

### Principe

Un seul endpoint WebSocket : `ws://192.168.4.1/ws`.

Bidirectionnel :
- **ESP32 → Client** : messages d'etat (JSON), pousse sur changement
- **Client → ESP32** : commandes (JSON), fire-and-forget

### Protocole Fire-and-Forget

- Le client envoie la commande JSON
- L'ESP32 parse, valide, met en queue (dirty-flag pattern)
- La loop applique au prochain cycle (~1-2ms)
- Le StatusPoller detecte le changement et pousse le nouvel etat au client
- Le client met a jour l'UI en recevant le message d'etat
- Pas d'ack/nack explicite — le flux d'etat sert de confirmation

### Heartbeat

ESP32 envoie `{ "type": "hb" }` toutes les 3s. Client-side : si pas de message recu depuis 5s, afficher `● WS` en gris (connexion perdue).

### Sequence au Connect

1. `banks` — snapshot complet des 8 banks
2. `state` — detail de la bank active
3. `tempo` — BPM, source, follow
4. `battery` — pourcentage, charging
5. `connections` — USB, BLE
6. `pad_roles` — 29 control pads
7. `pad_notes` — 48 notes resolues
8. Puis : heartbeat (3s loop) + evenements sur changement

En mode setup, la sequence inclut aussi `setup_state` (config actuelle de chaque Tool).

### Debits & Throttling

| Categorie | Frequence max | Justification |
|---|---|---|
| Etat bank/scale/arp | Immediat sur changement | Evenements rares (interaction utilisateur) |
| Pots | 1 Hz max par pot | Rappel, pas feedback temps reel |
| Batterie | Toutes les 30s | Lent par nature |
| Connexions USB/BLE | Immediat sur changement | Evenement rare |
| Button hold | Immediat | Toggle SVG overlay |
| Heartbeat | Toutes les 3s | Detection connexion perdue |
| Erreurs | Immediat | Critique |
| Calibration pressure (setup) | ~20Hz par pad touche | Streaming temps reel |
| Pad touch (setup) | Immediat | Evenement rare |

---

## 5. CommandHandler — Queue Dirty-Flag

### Probleme

Le callback AsyncWebSocket tourne dans le contexte LwIP/WiFi — un task FreeRTOS different de la loop() Core 1. Les setters des managers ne sont pas thread-safe. Appeler directement les setters depuis le callback WS = race conditions, donnees corrompues, notes stuck.

### Solution

Meme pattern que NvsManager : le callback WS pose des flags atomiques + pending values, la loop() applique au prochain cycle.

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

### Validation

La CommandHandler valide avant de mettre en queue :
- `bank` dans range 0-7
- `value` dans les bornes du parametre
- Commande pot sur bank active → rejetee silencieusement (regle de souverainete, voir `wifi-status-page.md`)
- Commande setup en mode jeu → rejetee
- Commande live en mode setup → rejetee

### Latence

Le callback WS pose le flag (~0 CPU). La loop() lit le flag au prochain cycle (~1-2ms). La latence totale d'une commande web est ~10-40ms (WiFi dominant, queue negligeable).

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

---

## 6. Persistance NVS

Les changements web suivent le meme chemin que les changements physiques :
1. CommandHandler applique via les setters des managers
2. Les managers marquent leurs dirty flags (existants)
3. NvsManager detecte les dirty flags et ecrit en flash apres le delai d'inactivite habituel

**Pas de logique NVS specifique au web.** Le web est juste une source d'input supplementaire. Auto-persist avec delai, meme comportement que les pots physiques.

---

## 7. Coexistence VT100 / Web (Mode Setup)

- Un seul canal de setup actif a la fois
- Au boot setup, l'ESP32 attend sur les deux canaux (serial + WS)
- Le premier qui envoie une commande verrouille le canal
- L'autre canal recoit un message "Setup actif sur [web/serial]"
- Le verrou se relache si le canal actif se deconnecte (WS close / serial timeout)

---

## 8. Fichiers ESP32

```
src/wifi/
├── WifiStatusServer.cpp/.h     # AP + HTTP + WebSocket (infra reseau)
├── StatusPoller.cpp/.h         # Lit managers, diff, serialise JSON (ESP32→client)
├── CommandHandler.cpp/.h       # Parse JSON, valide, queue dirty-flag (client→ESP32)
├── SetupWsHandler.cpp/.h      # Logique setup via WS (streaming cal, pad touch, etc.)
├── status_page.h               # Page Live HTML gzippee (PROGMEM)
└── setup_page.h                # Page Setup HTML gzippee (PROGMEM)
```

---

## 9. Impact Systeme

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

## 10. Questions Ouvertes

1. **Geste physique WiFi on/off** : quel geste exactement ? A definir quand les boutons sont finalises.
2. **Delai auto-off WiFi sur batterie** : combien de minutes sans client WS ? Configurable dans Settings ?
3. **Nombre max de clients WS** : recommandation 1 seul. Si 2e client se connecte, le 1er est deconnecte.
4. **OTA firmware update** : hors scope, mais l'infra WiFi le rend possible.
5. **SVG des 48 pads** : mapping SVG path → pad index a calibrer une fois sur le hardware (constant, lie au PCB).
