> ⚠️ **OBSOLETE — NE PAS LIRE COMME CONTEXTE**
>
> Document **obsolète** (pré-refactor Phase 0 / 0.1). Ne doit pas être consommé par un agent ou LLM pour planning, implementation, ou audit.
>
> **Source de vérité LOOP actuelle** : [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](../superpowers/specs/2026-04-19-loop-mode-design.md) (qui consolide ce document + [`2026-04-02-loop-mode-design.md`](2026-04-02-loop-mode-design.md)).
>
> Conservé pour archive historique uniquement.

---

# LOOP Slot Drive — Design Spec

**Date**: 2026-04-06
**Status**: design approved, awaiting plan
**Author**: brainstorming session (Loïc + Claude)

---

## 1. Vue d'ensemble

Le **LOOP Slot Drive** est un système de stockage persistant qui permet à l'utilisateur de sauvegarder le contenu musical d'un loop dans 1 des 16 slots, puis de le rappeler à n'importe quel moment, y compris après reboot. Les slots sont stockés en flash via LittleFS et survivent indéfiniment jusqu'à effacement explicite.

### 1.1 Vocabulaire

| Terme | Définition |
|---|---|
| **Slot** | Un emplacement de stockage indexé 0-15. Soit vide, soit contient un *loop sauvegardé*. |
| **Loop sauvegardé** | Un fichier binaire `/loops/slot00.lpb` à `/loops/slot15.lpb` contenant les events musicaux + structure (longueur en bars, BPM de référence) + paramètres LOOP de l'instant du save. |
| **Slot pad** | Un des 16 pads physiques (configurables en Tool 3) qui sert de raccourci hardware vers les slots correspondants. La correspondance pad ↔ slot index est définie par l'ordre d'assignation en Tool 3. |
| **Drive** | La collection complète des 16 slots, vue comme un mini-filesystem. Pas d'outil de gestion dédié — toute interaction se fait via les gestes hardware. |

### 1.2 Périmètre du save

**Sauvegardé** :
- Events `_events[]` (jusqu'à 1024 × 8 octets = 8 KB)
- Structure : `_loopLengthBars`, `_recordBpm`, `_eventCount`
- Paramètres LOOP : `shuffleDepth`, `shuffleTemplate`, `chaosAmount`, `velPatternIdx`, `velPatternDepth`, `baseVelocity`, `velocityVariation`

**Pas sauvegardé** :
- Tempo runtime (live BPM)
- État playback (PLAYING/STOPPED/EMPTY)
- Refcount, pending queue, cursor position (états runtime éphémères)
- `loopQuantize` mode du bank (propriété du *bank*, pas du loop sauvegardé)

### 1.3 Périmètre du load

- Tous les events + structure + params LOOP sont restaurés
- Le **catch system** du PotRouter est ré-armé sur tous les params per-bank LOOP affectés (les pots devront être tournés jusqu'à la nouvelle valeur stockée pour reprendre la main)
- Le tempo runtime est inchangé — le loop rejoue à scaling proportionnel par rapport à son `_recordBpm`
- L'état playback est préservé : PLAYING reste PLAYING (avec hard-cut + quantize sur le beat), STOPPED reste STOPPED. EMPTY chargé devient STOPPED.

### 1.4 Architecture des rôles pad (refactor Tool 3 → b1 contextuel)

Le LOOP Slot Drive impose un refactor préalable du Tool 3 vers une architecture de **rôles contextuels** : un pad peut avoir un rôle différent selon le bank type courant.

**Globaux** (visibles partout, collisions interdites) :
- 8 bank pads — switch de bank, sous hold-left

**Contexte ARPEG** (visibles uniquement si bank foreground = ARPEG) :
- Sans hold : 1 pad play/stop ARPEG (toujours actif sur ARPEG, *différent du play/stop LOOP*)
- Avec hold-left : 7 root + 7 mode + 1 chrom + 1 hold + 4 octave = 20 pads
- **Total : 21 rôles**

**Contexte LOOP** (visibles uniquement si bank foreground = LOOP) :
- Sans hold : 3 pads (rec, play/stop LOOP, clear)
- Avec hold-left : 16 slot pads
- **Total : 19 rôles**

**Contexte NORMAL** :
- 0 rôle contextuel

**Règles de collision** :
- Bank pads ⊥ tout (interdit dans tous les contextes)
- ARPEG roles ⊥ ARPEG roles (interdit dans le contexte ARPEG)
- LOOP roles ⊥ LOOP roles (interdit dans le contexte LOOP)
- ARPEG roles ⊥ LOOP roles → **autorisé** (un même pad physique peut être Root C en ARPEG ET Slot 5 en LOOP)
- L'utilisateur peut choisir de mapper le pad "play/stop ARPEG" et le pad "play/stop LOOP" sur le même pad physique, ou pas. Son choix.

### 1.5 Choix technologique : LittleFS

**Stockage flash** : LittleFS dans une partition dédiée de 512 KB. Les 16 slots sont 16 fichiers binaires `/loops/slotNN.lpb`.

**Justification** :
1. **C'est le bon outil pour le job.** NVS est key-value pour configs courtes ; LittleFS est un filesystem pour données binaires arbitraires. 16 slots × 8 KB = 128 KB de données structurées : exactement le cas d'usage de LittleFS.
2. **Web UI futur trivial à brancher dessus.** `server.serveStatic("/loops", LittleFS, "/loops")` expose le drive en HTTP en une ligne. Avec NVS chunked il faudrait un parser custom et un endpoint dédié.
3. **Wear leveling et atomicité gratuits.** LittleFS gère les deux nativement.
4. **Évolutivité gratuite.** Si demain on veut 32 slots ou des slots plus gros, c'est juste un fichier plus gros — pas de refactor.
5. **Cohérence "Zero Migration Policy"** : le partition resize implique un reset NVS au premier flash, exactement ce que CLAUDE.md autorise pour ce projet en proto.

### 1.6 Gestes utilisateur

Tous les gestes nécessitent **bank LOOP en foreground** ET **hold-left actif** ET **pas en RECORDING/OVERDUBBING**.

| Geste | Trigger | Résultat sur slot vide | Résultat sur slot occupé |
|---|---|---|---|
| Release entre 0 et 300 ms | Annulation au release | rien | rien |
| Release entre 300 et 1000 ms | Load au release | refus (rouge) | load (vert) |
| Atteint 1000 ms pendant le press | Save fire **pendant le press** (pas au release) | save (blanc) | refus (rouge) — delete d'abord |
| Press clear pad PUIS press slot pad | Delete fire **dès le rising-edge slot** | refus (rouge) — rien à supprimer | delete (rouge double) |

**Note save** : la save se déclenche à l'instant où le press atteint 1000 ms, pas au release. Le release qui suit est ignoré (le slot est marqué `consumed`).

**Note delete combo** : la combo n'est valide QUE dans l'ordre "clear puis slot". L'inverse (slot puis clear) n'est PAS détecté comme un delete — le slot est déjà en tracking et le clear arrive trop tard. L'utilisateur apprend l'ordre naturel.

---

## 2. Architecture des composants

### 2.1 Nouveau composant — `LoopSlotStore`

**Rôle** : encapsule toute l'interaction avec LittleFS. Le seul code qui touche directement au filesystem.

**Localisation** : `src/loop/LoopSlotStore.h` + `.cpp`

**Interface publique** :

```cpp
class LoopSlotStore {
public:
  static const uint8_t SLOT_COUNT = 16;

  bool begin();                                    // mount LittleFS + cleanup .tmp orphelins
  bool isSlotOccupied(uint8_t slotIdx) const;      // O(1) — bitmask cached
  bool saveSlot(uint8_t slotIdx, const LoopEngine& eng);
  bool loadSlot(uint8_t slotIdx, LoopEngine& eng,
                MidiTransport& transport, uint32_t globalTick) const;
  bool deleteSlot(uint8_t slotIdx);
  bool isMounted() const { return _mounted; }

private:
  bool _mounted = false;
  uint16_t _occupancyBitmask = 0;  // bit i set = slot i occupied

  void rescanOccupancy();
  void cleanupOrphanTmpFiles();
  bool readHeader(uint8_t slotIdx, LoopSlotHeader& hdr) const;
};
```

### 2.2 Format de fichier `.lpb`

```
[LoopSlotHeader  : 32 bytes]
  uint16_t magic              // LOOP_SLOT_MAGIC — to be defined in impl,
                              //   suggestion: 0x1F00 (matching the codebase pattern
                              //   like EEPROM_MAGIC=0xBEEF, COLOR_SLOT_MAGIC=0xC010)
  uint8_t  version            // LOOP_SLOT_VERSION (=1)
  uint8_t  reserved
  uint16_t eventCount         // number of LoopEvent entries that follow
  uint16_t loopBars           // _loopLengthBars
  float    recordBpm          // _recordBpm
  uint16_t shuffleDepthRaw    // 0-4095
  uint8_t  shuffleTemplate    // 0-9
  uint8_t  velPatternIdx      // 0-3
  uint16_t chaosRaw           // 0-4095
  uint16_t velPatternDepthRaw // 0-4095
  uint8_t  baseVelocity       // 1-127
  uint8_t  velocityVariation  // 0-100
  uint8_t  reserved2[10]      // F-PLAN-3 fix (audit 2026-04-07): explicit
                              // padding to reach 32 bytes total. Previously
                              // reserved2[8] relied on implicit GCC trailing
                              // alignment padding which is fragile.

[LoopEvent[eventCount] : eventCount × 8 bytes]
  // existing LoopEvent format from LoopEngine.h
```

**Atomicité** : `saveSlot()` écrit dans `slotNN.tmp` puis rename en `slotNN.lpb` (rename atomique sur LittleFS). Si crash en plein write, le fichier précédent reste valide.

**Bitmask occupancy** : recalculé une fois au boot via `LittleFS.exists()` pour chaque slot. Mis à jour à chaque save (set bit) et delete (clear bit). Évite tout appel filesystem dans le hot path runtime.

**Buffer de sérialisation** : un static reusable `s_serializeBuffer[8224]` (32 + 1024 × 8) en SRAM ou PSRAM. Alloué une fois, jamais via new/delete. Cohérent avec la convention firmware.

### 2.3 Extension `LoopEngine` — serialize/deserialize

Ajout de 2 méthodes publiques :

```cpp
// Sérialise l'état actuel vers un buffer pre-alloué.
// Buffer doit être au moins sizeof(LoopSlotHeader) + _eventCount × sizeof(LoopEvent).
// Returns: nombre d'octets écrits, ou 0 sur erreur (eventCount == 0 = refus, buffer trop petit).
size_t serializeToBuffer(uint8_t* buf, size_t bufSize) const;

// Désérialise depuis un buffer.
// Hard-cut: vide refcount, vide pending queue, remplace events.
// Si _state == PLAYING, applique quantize-snap sur le dernier beat passé
// (positionne _playStartUs sur le beat). Sinon, _state inchangé (EMPTY → STOPPED).
// Returns: true si OK, false si magic/version invalides.
bool deserializeFromBuffer(const uint8_t* buf, size_t bufSize,
                            MidiTransport& transport, uint32_t globalTick);
```

**Effets de `deserializeFromBuffer` quand `_state == PLAYING`** :

1. `flushActiveNotes(transport, hard=true)` (CC123 + zero refcount + clear pending)
2. Remplace `_events`, `_eventCount`, `_loopLengthBars`, `_recordBpm`
3. Remplace tous les params LOOP
4. Calcule le `_playStartUs` virtuel pour aligner sur le **dernier beat passé** :
   ```
   tickInBeat = globalTick % 24
   usSinceLastBeat = tickInBeat * (60_000_000 / currentBPM / 24)
   _playStartUs = micros() - usSinceLastBeat
   ```
5. Reset `_cursorIdx = 0`, `_lastPositionUs = 0`, `_lastBeatIdx = 0xFFFFFFFF`
6. Reset `_beatFlash = _barFlash = _wrapFlash = false`
7. `_state` reste à `PLAYING` — le tick suivant reprend la lecture

**Effets de `deserializeFromBuffer` quand `_state == STOPPED` ou `EMPTY`** :

1. `flushActiveNotes(transport, hard=true)` (par sécurité, normalement no-op)
2. Remplace events + structure + params
3. `_state = STOPPED` (un EMPTY chargé devient STOPPED)
4. L'utilisateur appuiera play/stop pour démarrer

### 2.4 Nouveau handler — `handleLoopSlots()` dans main.cpp

Fonction statique dans `main.cpp`, analogue à `handleLoopControls()`. Appelée chaque frame **après** `handleLoopControls()` et **avant** `handlePadInput()`.

**State persistant inter-frame** (statics dans main.cpp, scope fonction) :
- `static bool s_slotLastState[16]` — état pressed/released du frame précédent par slot
- `static uint32_t s_slotPressStart[16]` — millis() du rising-edge du press en cours
- `static bool s_slotConsumed[16]` — true quand le press a déjà déclenché une action (save ou delete combo) et qu'on attend juste le release
- `static bool s_clearConsumedByCombo` — flag de coordination avec `handleLoopControls()` (cf. 2.5)

**Pseudocode** :

```
si !leftHeld OU foreground bank n'est pas LOOP OU loopEngine recording :
    reset edge states de tous les slot pads et exit

pour chaque slot pad assigné (0-15) :
    pressed = state.keyIsPressed[slotPadIdx[i]]

    si rising edge (pressed && !lastState[i]) :
        si clear pad également pressé en ce moment (= combo delete) :
            si LoopSlotStore::isSlotOccupied(i) :
                LoopSlotStore::deleteSlot(i)
                triggerConfirm(CONFIRM_LOOP_SLOT_DELETE)  // double rouge
            sinon :
                triggerConfirm(CONFIRM_LOOP_SLOT_REFUSE)  // simple rouge
            consumed[i] = true
            _clearConsumedByCombo = true  // signal à handleLoopControls()
        sinon :
            press_start[i] = now
            consumed[i] = false

    si held && !consumed[i] :
        elapsed = now - press_start[i]
        si elapsed >= LOOP_SLOT_LONG_PRESS_MS (1000) :
            // save action
            si LoopSlotStore::isSlotOccupied(i) :
                triggerConfirm(CONFIRM_LOOP_SLOT_REFUSE)
            sinon si eng.getEventCount() == 0 :
                triggerConfirm(CONFIRM_LOOP_SLOT_REFUSE)  // refus EMPTY
            sinon :
                LoopSlotStore::saveSlot(i, *currentSlot.loopEngine)
                triggerConfirm(CONFIRM_LOOP_SLOT_SAVE)
            consumed[i] = true
        sinon :
            // ramp visible pendant le hold
            showSlotSaveRamp(elapsed * 100 / LOOP_SLOT_LONG_PRESS_MS)

    si falling edge (!pressed && lastState[i]) :
        si !consumed[i] :
            elapsed = now - press_start[i]
            si elapsed < LOOP_SLOT_LOAD_MIN_MS (300) :
                // tap trop court — annulation silencieuse
                rien
            sinon :
                // tap court valide — load
                si LoopSlotStore::isSlotOccupied(i) :
                    LoopSlotStore::loadSlot(i, *currentSlot.loopEngine,
                                            transport, globalTick)
                    s_potRouter.loadStoredPerBank(...)       // re-arm catch
                    s_potRouter.loadStoredPerBankLoop(...)
                    triggerConfirm(CONFIRM_LOOP_SLOT_LOAD)
                sinon :
                    triggerConfirm(CONFIRM_LOOP_SLOT_REFUSE)
        consumed[i] = false
        lastState[i] = false
        continue

    lastState[i] = pressed
```

### 2.5 Coordination handleLoopSlots ↔ handleLoopControls

**Problème** : le clear pad seul déclenche un ramp clear loop (handleLoopControls), mais en combo avec un slot pad il déclenche un delete slot. Les deux fonctions doivent se coordonner.

**Solution** : flag global `static bool s_clearConsumedByCombo`.

- `handleLoopSlots()` est appelée **APRÈS** `handleLoopControls()` dans la pipeline
- Quand `handleLoopSlots()` détecte un slot rising-edge avec clear pressed simultanément, elle exécute la combo delete ET set `s_clearConsumedByCombo = true`
- À la **frame suivante**, `handleLoopControls()` voit le flag true au début de son traitement du clear pad et **ignore** son propre tracking (ne démarre pas de ramp, ne fire pas le clear) jusqu'à ce que le clear pad soit relâché
- Au release du clear pad, `s_clearConsumedByCombo = false` et le tracking de l'edge clear est repris normalement à la prochaine pression

**Micro-glitch accepté** : pendant la frame N (où la combo est détectée), `handleLoopControls()` a déjà tourné AVANT `handleLoopSlots()` et a démarré 1 frame de ramp clear (~1 ms). À la frame N+1, le flag coupe le ramp. Visuellement c'est invisible (1 ms < seuil de perception). Acceptable.

### 2.6 Modifications composants existants

**Note** : cette table est le **résumé total** de tous les changements à travers toutes les phases. La répartition par phase (Phase 1, Phase 3, Phase 6) est dans la **section 7**.

| Fichier | Changements |
|---|---|
| `platformio.ini` | Ajout `board_build.partitions = partitions_illpad.csv` |
| `partitions_illpad.csv` (NEW) | Table de partitions custom : NVS + app + LittleFS 512 KB |
| `src/loop/LoopEngine.h` | Déclarations `serializeToBuffer()`, `deserializeFromBuffer()` |
| `src/loop/LoopEngine.cpp` | Implémentation des 2 méthodes ; helper interne pour quantize-snap PLAYING |
| `src/core/HardwareConfig.h` | Constantes `LOOP_SLOT_LONG_PRESS_MS = 1000`, `LOOP_SLOT_LOAD_MIN_MS = 300`, `LOOP_SLOT_RAMP_DURATION_MS = 1000`, `COL_LOOP_SLOT_LOAD = vert`, `COL_LOOP_SLOT_SAVE = blanc`, `COL_LOOP_SLOT_REFUSE = rouge`, `COL_LOOP_SLOT_DELETE = double rouge` |
| `src/core/KeyboardData.h` | Enum `ROLE_LOOP_SLOT_0..15`, `ROLE_ARPEG_PLAYSTOP` ; extension `LoopPadStore.slotPads[16]` ; bump `LOOPPAD_VERSION 1→2` ; refactor logique de validation Tool 3 (architecture contextuelle b1) |
| `src/core/LedController.h` | Confirms `CONFIRM_LOOP_SLOT_LOAD/SAVE/REFUSE/DELETE` ; déclaration `showSlotSaveRamp(uint8_t pct)` + private state `_slotRampPct`, `_showingSlotRamp` |
| `src/core/LedController.cpp` | Implémentation des 4 nouveaux confirms (durée + couleur) ; rendu de `showSlotSaveRamp` (overlay vert ou blanc selon pct, sur LED bank courant) ; expiry dans `renderConfirmation` |
| `src/setup/ToolPadRoles.h` | **Refactor majeur** : 3 sous-pages (Banks / Arpeg / Loop) ; signature `getRoleForPad(pad, BankType context)` ; ajout des 16 slot roles + ARPEG playstop role |
| `src/setup/ToolPadRoles.cpp` | **Refactor majeur** : navigation entre les 3 sous-pages, validation collision contextuelle (b1), pool extension, save/load `LoopPadStore` avec `slotPads[16]` |
| `src/main.cpp` | Static `s_loopSlotStore` ; static array `s_loopSlotPads[16]` ; nouvelle fonction `handleLoopSlots()` ; appel après `handleLoopControls()` ; modification de `handleLoopControls()` pour respecter `s_clearConsumedByCombo` ; setup() ajoute `s_loopSlotStore.begin()` et propagation des slot pads vers SetupManager ; **réordonnancement de l'init boot** : `LedController::begin()` reste en premier mais ne consomme plus de step ; LittleFS mount devient step 1 (avant I2C / keyboard / NVS) |

### 2.7 Boot sequence étendue

Le step "LED hardware ready" est **supprimé** : si l'init du LED strip échoue, aucune LED ne s'allume — c'est un diagnostic visuel suffisant en soi. Le `LedController::begin()` reste le tout premier appel de `setup()` (prérequis silencieux pour pouvoir afficher quoi que ce soit), mais il ne consomme plus de step de la barre de boot. Cela libère un slot pour le mount LittleFS et garde la barre à 8 steps.

```
Step 1: ●○○○○○○○  LittleFS mounted + slot drive scanned   ← NEW (was: LED hardware ready)
Step 2: ●●○○○○○○  I2C bus ready                           (unchanged)
Step 3: ●●●○○○○○  Keyboard OK                             (unchanged)
Step 4: ●●●●○○○○  MIDI Transport (USB + BLE) started      (unchanged)
Step 5: ●●●●●○○○  NVS loaded                              (unchanged)
Step 6: ●●●●●●○○  Arp + Loop systems ready                (unchanged)
Step 7: ●●●●●●●○  Managers ready                          (unchanged)
Step 8: ●●●●●●●●  All systems go (200ms full bar)         (unchanged)
```

**Conséquence sur l'ordre d'init dans `setup()`** : LittleFS doit être monté **avant** I2C bus / keyboard / NVS. C'est techniquement OK (LittleFS ne dépend de rien d'autre que de l'API ESP-IDF), mais c'est un changement à l'**ordre d'init dans setup()**, pas juste une renumérotation cosmétique. À traiter explicitement dans le plan d'implémentation.

**Échec mount LittleFS** = step 1 LED blink rouge, halt forever. Cohérent avec la philosophie boot existante.

**Échec `LedController::begin()`** = aucune LED n'apparaît au boot. C'est exactement le comportement actuel — diagnostic via serial uniquement. Aucun changement requis.

---

## 3. Data flows

### 3.1 Save (long press 1s sur slot vide)

```
T=0       User presse hold-left (déjà en bank LOOP foreground)
          - BankManager._holding = true
          - handlePadInput() est skip (musique inactive sous hold)
          - handleLoopSlots() est appelé chaque frame

T=0+ε     User presse slot pad N (par exemple slot 5, mappé sur pad 30)
          handleLoopSlots() détecte rising edge sur slot 5 :
          - Vérifie : clear pad pas pressé simultanément → pas une combo delete
          - press_start[5] = millis()
          - consumed[5] = false

T=0..1s   Pendant le hold (chaque loop frame ~1ms) :
          - handleLoopSlots() voit slot 5 toujours pressed, !consumed
          - elapsed = now - press_start[5]
          - pct = elapsed * 100 / 1000
          - s_leds.showSlotSaveRamp(pct)
          - LedController affiche un overlay ramp blanc sur le LED du bank courant

T=1.0s    elapsed >= LOOP_SLOT_LONG_PRESS_MS (1000) :
          - Vérifie LoopSlotStore::isSlotOccupied(5) → false
          - Vérifie eng.getEventCount() > 0 → true
          - Appel LoopSlotStore::saveSlot(5, *currentSlot.loopEngine)
            ↓
            LoopSlotStore::saveSlot() :
              1. Calcule taille totale = sizeof(LoopSlotHeader) + eventCount × 8
              2. Appel eng.serializeToBuffer(s_serializeBuffer, sizeof(buffer))
                 ↓
                 LoopEngine::serializeToBuffer() :
                   - Remplit header (magic, version, eventCount, loopBars,
                     recordBpm, params)
                   - memcpy events après le header
                   - Retourne nombre d'octets écrits
              3. LittleFS.open("/loops/slot05.tmp", FILE_WRITE)
              4. file.write(buffer, size)
              5. file.close()
              6. LittleFS.rename("/loops/slot05.tmp", "/loops/slot05.lpb")  ← atomique
              7. _occupancyBitmask |= (1 << 5)
              8. return true
          - s_leds.triggerConfirm(CONFIRM_LOOP_SLOT_SAVE)  ← blink blanc 200ms
          - consumed[5] = true (reste consommé jusqu'au release)

T=1.0s+   User relâche slot 5 :
          - Falling edge détecté
          - consumed[5] est true → tap NON traité comme un load
          - consumed[5] = false (reset pour la prochaine fois)
          - lastState[5] = false
```

**Si le slot est déjà occupé** : à T=1.0s, `isSlotOccupied(5)` retourne true. Au lieu d'écrire, on déclenche `CONFIRM_LOOP_SLOT_REFUSE` (blink rouge 200ms) et `consumed[5] = true`. L'utilisateur doit delete d'abord (combo hold + clear pad + slot pad).

**Coût du write** : LittleFS sur ESP32 écrit ~50-100 KB/s. Pour un slot de 8 KB : ~80-160 ms. C'est **bloquant** sur Core 1 — une frame manquée. Acceptable parce que :
1. Action explicite et délibérée (1s de hold)
2. L'utilisateur est en hold-left → la musique est déjà gelée
3. Les notes du loop courant continuent via `tick() + processEvents()` qui sont appelés *avant* `handleLoopSlots()`
4. Le **prochain frame** reprend normalement

### 3.2 Load (tap court sur slot occupé pendant PLAYING)

```
T=0       User est en bank LOOP foreground, état PLAYING (un loop tourne)

T=0+ε     User presse hold-left :
          - BankManager._holding = true
          - handlePadInput() est skip
          - LoopEngine::tick() + processEvents() continuent dans la pipeline
          - Le loop continue de jouer normalement

T=0+2ε    User tape (court) sur slot pad 7 :
          handleLoopSlots() détecte rising edge slot 7 :
          - Pas de combo delete
          - press_start[7] = now
          - consumed[7] = false

T=0+50ms  User relâche slot 7 :
          handleLoopSlots() détecte falling edge :
          - elapsed = 50ms < 300ms → annulation silencieuse
          - rien ne se passe

(autre scénario : tap entre 300ms et 1000ms)

T=0+500ms User relâche slot 7 :
          handleLoopSlots() détecte falling edge :
          - elapsed = 500ms (entre 300 et 1000) → load
          - Vérifie LoopSlotStore::isSlotOccupied(7) → true
          - Appel LoopSlotStore::loadSlot(7, *currentSlot.loopEngine,
                                            transport, globalTick)
            ↓
            LoopSlotStore::loadSlot() :
              1. LittleFS.open("/loops/slot07.lpb", FILE_READ)
              2. file.read(s_serializeBuffer, fileSize)
              3. file.close()
              4. eng.deserializeFromBuffer(buffer, fileSize, transport,
                                            globalTick)
                 ↓
                 LoopEngine::deserializeFromBuffer() :
                   a. Validate header magic + version. Si invalide → return false.
                   b. flushActiveNotes(transport, hard=true)
                      → noteOff pour toute note refcount > 0
                      → vide la pending queue
                   c. Copie les events depuis buf vers _events[]
                   d. Restore _eventCount, _loopLengthBars, _recordBpm
                   e. Restore params LOOP
                   f. Quantize-snap (puisque _state == PLAYING) :
                      tickInBeat = globalTick % 24
                      usSinceLastBeat = tickInBeat * (60_000_000 / currentBPM / 24)
                      _playStartUs = micros() - usSinceLastBeat
                   g. _cursorIdx = 0
                      _lastPositionUs = 0
                      _lastBeatIdx = 0xFFFFFFFF
                      _beatFlash = _barFlash = _wrapFlash = false
                   h. _state reste PLAYING
                   i. return true
          - Push params vers PotRouter pour re-armer le catch :
              s_potRouter.loadStoredPerBank(
                  newSlot.baseVelocity, newSlot.velocityVariation,
                  newSlot.pitchBendOffset, ... + shufDepth, shufTmpl, ...)
              s_potRouter.loadStoredPerBankLoop(chaos, velPat, velPatDepth)
              → seedCatchValues() interne décatch tous les pots LOOP per-bank
          - s_leds.triggerConfirm(CONFIRM_LOOP_SLOT_LOAD) ← blink vert 200ms

T=0+50ms+ Frame suivante :
          - LoopEngine::tick() est appelé
          - Nouveau positionUs calculé depuis le _playStartUs corrigé
          - Le cursor scan trouve les events restaurés et commence à les fire
          - Pas de claquement (les anciennes notes sont déjà flushed)
          - Le LED renderBankLoop continue avec les nouveaux beat flashes
```

**Coût du read** : LittleFS lit ~200-400 KB/s. 8 KB → 20-40 ms. Une frame manquée maximum, dans un contexte hold-left où la musique pad est inactive.

**Si le slot est vide** : `isSlotOccupied(7)` retourne false. Au lieu de loader, on déclenche `CONFIRM_LOOP_SLOT_REFUSE`.

### 3.3 Delete (combo hold + clear pad + slot pad)

```
T=0       User est en hold-left, bank LOOP foreground

T=0+ε     User presse le clear pad :
          - handleLoopControls() détecte rising edge sur clear pad
          - clearPressStart marqué, clearFired = false
          - showClearRamp commence (mais inhibé si combo détectée)

T=0+10ms  User presse slot pad 7 (frame suivante ou même frame) :
          - handleLoopSlots() détecte rising edge sur slot 7
          - Vérifie : clear pad pressed RIGHT NOW → c'est une combo delete
          - Vérifie LoopSlotStore::isSlotOccupied(7) :
            - true → LoopSlotStore::deleteSlot(7)
              ↓
              LoopSlotStore::deleteSlot() :
                LittleFS.remove("/loops/slot07.lpb")
                _occupancyBitmask &= ~(1 << 7)
                return true
              → triggerConfirm(CONFIRM_LOOP_SLOT_DELETE)  ← double rouge
            - false → triggerConfirm(CONFIRM_LOOP_SLOT_REFUSE)
          - consumed[7] = true
          - s_clearConsumedByCombo = true

T=0+release  User relâche le clear pad puis le slot pad (ordre indifférent) :
             - handleLoopControls() voit clear released, clearFired était false,
               donc rien ne se déclenche
             - handleLoopControls() voit s_clearConsumedByCombo = true au
               release du clear pad → reset le flag à false
             - handleLoopSlots() voit slot released, consumed[7] était true,
               donc rien ne se déclenche
             - Reset consumed[7] = false
```

---

## 4. Invariants

**I1 — Le drive est toujours cohérent ou invalidé.** Toute écriture passe par `tmp + rename`. Sur power loss en plein write, l'ancien fichier reste valide ou aucun fichier n'existe. Jamais de fichier à moitié écrit.

**I2 — Pas de slot accessible en écriture quand le LoopEngine recording.** `handleLoopSlots()` skip total si `loopEngine->isRecording() || loopEngine->isOverdubbing()`. Cohérent avec le bank switch lock.

**I3 — Le catch system PotRouter est ré-armé après chaque load.** Sinon les pots conservent leur catch précédent et écrasent les params LOOP juste chargés. Utilise les fonctions existantes `loadStoredPerBank()` + `loadStoredPerBankLoop()`.

**I4 — Pas d'allocation dynamique runtime.** Le buffer de sérialisation est un static reusable de 8224 octets, alloué une fois. Cohérent avec la convention "no new/delete at runtime" du CLAUDE.md.

**I5 — LittleFS opérations bloquent Core 1, mais uniquement sous hold-left.** La musique pad est gelée pendant le hold. Le loop en cours continue à jouer parce que `tick() + processEvents()` sont appelés *avant* `handleLoopSlots()`.

**I6 — Le hard-cut + quantize d'un load PLAYING ne produit PAS d'orphan notes.** `flushActiveNotes(hard=true)` envoie noteOff pour TOUTE note refcount > 0 avant de remplacer les events. Cohérent avec l'invariant #1 du `architecture-briefing.md` ("No orphan notes").

**I7 — Le `_recordBpm` chargé est immutable jusqu'au prochain stopRecording.** Le scaling proportionnel utilise toujours `_recordBpm` comme référence. Le tempo runtime peut diverger — c'est le design.

**I8 — Le format `.lpb` est versionné via magic + version.** Lecture rejette tout fichier dont le magic ou la version ne correspond pas (return false silencieux + log DEBUG_SERIAL). Zero Migration Policy : les anciens slots sont juste ignorés après un changement de format.

**I9 — Les rôles slot sont contextuels LOOP.** Un slot pad mappé sur le pad physique 30 joue comme un pad de musique sur NORMAL/ARPEG. Sur LOOP sous hold-left il devient slot. Sur LOOP sans hold il joue comme un pad de musique.

**I10 — Le bitmask occupancy est synchronisé avec le filesystem.** Reconstruit au boot via `LittleFS.exists()`. Mis à jour à chaque save/delete. Jamais lu directement depuis le filesystem en hot path.

---

## 5. Edge cases

| # | Cas | Comportement |
|---|---|---|
| **E1** | Hold + slot pad pressé pendant RECORDING | `handleLoopSlots()` skip total. Au release du hold, lastState[i] est resynchronisé pour éviter un phantom edge. |
| **E2** | Hold + slot pad pressé sur bank NORMAL/ARPEG foreground | `handleLoopSlots()` skip total (foreground != LOOP). La résolution `getRoleForPad(pad, BANK_ARPEG)` retourne le rôle ARPEG si applicable, pas le rôle slot. |
| **E3** | Tap < 300ms (changement d'avis) | Annulation silencieuse. Aucun blink, aucune action. |
| **E4** | User maintient un slot pad enfoncé > 2s | À 1s, save (ou refus si occupé), `consumed[i] = true`. Pendant les secondes suivantes, rien. Au release, reset. |
| **E5** | Press clear puis release puis press slot | Pas une combo (clear n'est plus pressed à l'instant du slot rising). Le slot est traité normalement. |
| **E6** | Press slot puis press clear (ordre inverse) | **PAS** de delete. Le slot est déjà en tracking, le clear arrive trop tard. L'utilisateur apprend "clear d'abord". |
| **E7** | Bank switch pendant qu'un slot pad est pressé | Au début de `handleLoopSlots()`, si foreground != LOOP, reset complet de `lastState[16]` pour éviter phantom edge au prochain hold. |
| **E8** | Save d'un loop EMPTY (eventCount == 0) | **Refus**. À T=1s, blink rouge REFUSE. Aucun fichier créé. |
| **E9** | Load d'un fichier corrompu (magic invalide) | `deserializeFromBuffer()` retourne false. LoopEngine inchangé. Blink rouge REFUSE. **Bitmask occupancy mis à false** pour ce slot (considéré vide pour les futures opérations). |
| **E10** | LittleFS plein | Save échoue. Blink rouge REFUSE. Avec 512 KB / 8 KB par slot = 64 slots possibles vs 16 réservés, impossible dans le scope normal. |
| **E11** | User load → overdub → REC pour fermer overdub | L'overdub se merge dans `_events[]` qui contient maintenant chargé + nouveau. Le slot stocké en flash N'EST PAS modifié. Snapshots immuables, runtime mutable. |
| **E12** | Reboot pendant un save en cours | Le rename `.tmp` → `.lpb` n'a pas eu lieu. Au reboot, `cleanupOrphanTmpFiles()` supprime tous les `.tmp` orphelins. Le fichier `.lpb` précédent (s'il existait) est intact. |

---

## 6. Risks

**R1 — LittleFS partition resize change la layout flash.**
- **Impact** : tous les paramètres user (calibration, pad order, bank config, etc.) sont reset aux defaults au premier boot.
- **Mitigation** : explicitement autorisé par CLAUDE.md ("Zero Migration Policy"). Documenter le comportement utilisateur.
- **Acceptabilité** : ✓ design intentionnel.

**R2 — LittleFS write pendant 80-160ms bloque Core 1.**
- **Impact potentiel** : task watchdog timeout si la threshold est trop courte.
- **Mitigation** : par défaut le watchdog FreeRTOS sur ESP32-Arduino est à plusieurs secondes. Aucune action requise, mais à vérifier au test hardware.
- **Acceptabilité** : ✓ probable non-issue, à vérifier.

**R3 — La buffer de sérialisation 8224 octets en SRAM réduit le free heap de ~2.5%.**
- **Impact** : ~16% → ~18.5% SRAM utilisé. Reste très en dessous des limites.
- **Mitigation** : si problématique, allouer en PSRAM via `heap_caps_malloc(MALLOC_CAP_SPIRAM)`.
- **Acceptabilité** : ✓ marge confortable.

**R4 — Le LED ramp save (1s) + CONFIRM 200ms bloquent l'utilisateur 1.2s par save.**
- **Impact** : workflow lent si l'utilisateur veut sauver beaucoup de slots.
- **Mitigation** : délibéré (analogue au CLEAR ramp).
- **Acceptabilité** : ✓ design intentionnel.

**R5 — Le delete combo "hold + clear + slot" est une gesture composée à 3 doigts.**
- **Impact** : discoverability faible.
- **Mitigation** : documentation explicite dans le manual HTML LOOP, plus la VT100 description du clear pad et des slot pads.
- **Acceptabilité** : ✓ acté.

**R6 — Tool 3 refactor en architecture contextuelle (b1) est une grosse modification.**
- **Impact** : phase Tool 3 plus lourde, risque de régression sur les rôles existants.
- **Mitigation** : décomposer en sous-étapes (refactor d'abord, ajout des slots ensuite). Tests setup manuels après chaque sous-étape.
- **Acceptabilité** : ✓ acté en Q5.

---

## 7. Decomposition & impact sur les phases LOOP existantes

### 7.1 Recommandation

Le slot drive est suffisamment indépendant pour être traité comme **Phase 6** ajoutée après Phase 5 du plan LOOP existant. Mais le **refactor Tool 3 (b1) doit être fait dans Phase 3**, pas reporté en Phase 6, parce que :

1. Phase 3 ajoute déjà les LOOP control roles ; les ajouter en architecture b1 directement coûte le même effort que les ajouter en architecture flat puis refactor plus tard.
2. La sous-page "Loop roles" avec les 16 slots peut être ajoutée en Phase 6 sans toucher au refactor de Phase 3.

### 7.2 Impact sur les 5 plans LOOP existants

| Phase | Impact |
|---|---|
| **Phase 1 (skeleton)** | Étendre avec les nouveaux constants HW Config, l'extension `LoopPadStore.slotPads[16]`, les nouveaux ROLES dans `KeyboardData.h`, le bump `LOOPPAD_VERSION 1→2`. **Pas de breaking change**, juste des additions. |
| **Phase 2 (engine + wiring)** | Aucun impact direct. Phase 6 ajoutera `serialize/deserialize` au LoopEngine plus tard. |
| **Phase 3 (setup tools)** | **Refactor majeur de Tool 3** vers l'architecture contextuelle (b1). Le plan Phase 3 actuel suppose Tool 3 = liste plate ; il faut le réécrire pour 3 sous-pages avec validation contextuelle. Les 16 slot roles **NE sont PAS encore ajoutés en Phase 3** — juste l'architecture les rendant possibles. |
| **Phase 4 (PotRouter + LED)** | Aucun impact direct. Phase 6 ajoutera les LED confirms slot. |
| **Phase 5 (effects)** | Aucun impact. |

### 7.3 Nouvelle Phase 6

**Phase 6 — LOOP Slot Drive** (nouvelle, après Phase 5) ajoute :
- Nouveaux fichiers : `LoopSlotStore.h/.cpp`, `partitions_illpad.csv`
- `platformio.ini` : partition table custom
- `LoopEngine` : `serialize/deserialize`
- `LedController` : `CONFIRM_LOOP_SLOT_*` + `showSlotSaveRamp`
- `main.cpp` : `s_loopSlotStore`, `handleLoopSlots()`, coordination avec `handleLoopControls()` via `s_clearConsumedByCombo`, boot step 5b
- `ToolPadRoles` : extension de la sous-page Loop avec les 16 slot pads (l'architecture 3-pages existe déjà depuis Phase 3 refactor)
- `LoopPadStore` : `slotPads[16]` rempli par Tool 3
- Tests hardware Niveau 1 à 7 (cf. section 8)

---

## 8. Test strategy

Validation manuelle sur hardware, structurée en 7 niveaux. Pas de framework de tests automatisés (firmware embedded).

### Niveau 1 — Build & boot

| Test | Description |
|---|---|
| **B1** | Build clean, zéro warning lié au slot system. |
| **B2** | Premier boot après partition resize : NVS reset attendu (warnings au boot pour chaque store), tous les params utilisateur reset aux defaults. **Documenter** ce comportement comme attendu. |
| **B3** | LittleFS mount réussi : step 1 LED progresse normalement (1 LED blanche). Pas de halt. |
| **B4** | LittleFS mount échoue (test : volontairement supprimer la partition LittleFS de la table) : step 1 blink rouge, halt. Test de robustesse. |
| **B5** | Cleanup `.tmp` orphelins : créer manuellement un fichier `/loops/slot05.tmp` via script Python serial, reboot, vérifier qu'il a été supprimé au boot. |

### Niveau 2 — LoopSlotStore unit-level

| Test | Description |
|---|---|
| **S1** | Bitmask occupancy au boot : tous les slots sont vides au premier boot. Au reboot après save manuel, les slots savés sont marqués occupés. |
| **S2** | `saveSlot()` sur loop EMPTY : refus (eventCount == 0). Aucun fichier créé. |
| **S3** | `saveSlot()` sur loop avec 1 event : fichier créé, taille = 32 + 8 = 40 octets. `LittleFS.exists()` true. |
| **S4** | `saveSlot()` sur loop max events (1024) : fichier créé, taille = 32 + 8192 = 8224 octets. Temps d'écriture mesuré (attendu < 200 ms). |
| **S5** | `loadSlot()` sur slot inexistant : retourne false, LoopEngine inchangé. |
| **S6** | `loadSlot()` sur slot valide : events restaurés byte-pour-byte, structure restaurée, params restaurés. |
| **S7** | `loadSlot()` sur fichier corrompu (volontairement modifier le magic via Python) : retourne false, LoopEngine inchangé, bitmask occupancy mis à false. |
| **S8** | `deleteSlot()` sur slot occupé : fichier supprimé, `isSlotOccupied()` false après. |
| **S9** | `deleteSlot()` sur slot vide : retourne true (no-op gracieux). |
| **S10** | Power loss simulation pendant save (couper l'alim après le `tmp` write mais avant le rename) : reboot doit voir l'ancien fichier `.lpb` toujours valide, pas de `.tmp`. |

### Niveau 3 — LoopEngine serialize/deserialize round-trip

| Test | Description |
|---|---|
| **R1** | Round-trip simple : enregistrer un loop, save dans slot 0, clear le moteur, load slot 0, vérifier que le loop joue exactement pareil. |
| **R2** | Round-trip avec params modifiés : changer shuffle depth, chaos, vel pattern via pots, save, modifier les params à nouveau, load, vérifier que les params chargés écrasent les modifications. |
| **R3** | Round-trip de l'état STOPPED : enregistrer, stop, save, clear, load → moteur en STOPPED, l'user appuie play → joue correctement. |
| **R4** | Round-trip de l'état PLAYING : enregistrer, play, save, modifier le loop par overdub, load → l'overdub disparaît, le loop initial est restauré, état PLAYING avec hard-cut + quantize. |
| **R5** | Hard-cut + quantize PLAYING : visuel — le load pendant PLAYING ne produit pas de clic audible, le nouveau loop tombe sur le beat. |

### Niveau 4 — Tool 3 refactor contextuel (b1)

| Test | Description |
|---|---|
| **T1** | Tool 3 affiche 3 sous-pages navigables. Le toggle entre sous-pages est clair. |
| **T2** | Sous-page Banks : 8 bank pads mappables, collisions internes interdites (refus avec message clair). |
| **T3** | Sous-page Arpeg : 21 rôles ARPEG mappables, collisions internes interdites. |
| **T4** | Sous-page Loop : 3 LOOP ctrl pads mappables (avec ou sans les 16 slot pads selon que Phase 6 est implémentée), collisions internes interdites. |
| **T5** | Collision **inter-contexte autorisée** : assigner un pad au "Root C" en sous-page Arpeg ET au "LOOP rec" en sous-page Loop → autorisé sans erreur. Sauver, rebooter, le mapping persiste. |
| **T6** | Collision **bank ⊥ loop interdite** : essayer d'assigner un bank pad comme un slot LOOP → refus. |
| **T7** | Sur un bank ARPEG foreground, les pads marqués "rôle slot 5" jouent comme des pads de musique normaux (pas de slot recall accidentel). |
| **T8** | Sur un bank LOOP foreground, hold-left, le pad qui est mappé "Root C" en ARPEG est mappé "Slot 5" en LOOP → en hold-left c'est bien le slot 5 qui réagit. |

### Niveau 5 — Geste utilisateur complet

| Test | Description |
|---|---|
| **G1** | **Save sur slot vide.** Bank LOOP, loop enregistré stopped/playing. Hold-left + long press 1s sur slot pad mappé sur slot 0 vide. LED ramp blanc visible. À 1s, save, blink blanc. Reboot, slot 0 toujours occupé. |
| **G2** | **Save sur slot occupé.** Refaire G1 sur slot 0 occupé. À 1s, blink rouge REFUSE. Slot 0 inchangé. |
| **G3** | **Save sur loop EMPTY.** Bank LOOP, état EMPTY, hold-left + long press sur slot vide. À 1s, blink rouge REFUSE. |
| **G4** | **Load sur slot occupé en STOPPED.** Hold-left + tap (>300ms et <1000ms) sur slot 0 occupé. Au release, blink vert. STOPPED avec events chargés. PLAY/STOP → le loop joue. |
| **G5** | **Load sur slot vide.** Hold-left + tap court sur slot 1 vide. Au release, blink rouge REFUSE. |
| **G6** | **Load sur slot occupé en PLAYING.** Bank LOOP avec un loop qui joue. Hold-left + tap court sur slot 0. Au release, blink vert. Loop précédent stoppé net, nouveau démarre en hard-cut, aligné sur le beat. Pas de clic, pas de notes orphan. |
| **G7** | **Tap < 300ms = annulation silencieuse.** Hold-left + tap très court sur n'importe quel slot. Rien (pas de blink, pas d'action). |
| **G8** | **Long press > 2s = save normal puis idle.** Hold-left + tenir 3s sur slot vide. À 1s, save + blink. Pendant les 2s suivantes, rien. Au release, rien. |
| **G9** | **Delete combo (clear puis slot).** Bank LOOP, hold-left + presser clear pad + presser slot pad occupé. Slot supprimé, blink rouge double DELETE. Le clear pad seul ne déclenche PAS son ramp clear (consommé par la combo). Au release du clear pad, vérifier que le clear seul fonctionne à nouveau. |
| **G10** | **Delete combo inverse (slot puis clear) = pas de delete.** Slot pad d'abord, puis clear pad. Pas de delete. Le slot tap suit son cours normal. |
| **G11** | **Delete sur slot vide.** Hold-left + clear + slot vide. Blink rouge REFUSE. |

### Niveau 6 — Verrous & invariants

| Test | Description |
|---|---|
| **V1** | **Recording lock.** Bank LOOP en RECORDING. Hold-left + tap sur slot pad. Aucune action. Recording continue normalement. |
| **V2** | **Overdubbing lock.** Idem V1 mais en OVERDUBBING. |
| **V3** | **Foreground non-LOOP.** Bank ARPEG ou NORMAL en foreground. Hold-left + tap sur pad mappé "slot 5" en LOOP. Aucune action slot. Le pad agit selon son rôle dans le contexte courant. |
| **V4** | **Bank switch pendant slot tracking.** Hold-left, presser slot pad (rising edge tracked), presser bank pad pour switcher. Bank switch a lieu. Au release du slot pad, vérifier qu'aucune action slot ne se déclenche sur la nouvelle bank. |
| **V5** | **Catch re-arm après load.** Modifier shuffle depth via pot R2+hold à 0.7. Save. Modifier à 0.2. Load → la valeur stockée est 0.7. Le pot doit être tourné jusqu'à passer par 0.7 pour reprendre la main. Bargraph montre uncaught. |
| **V6** | **Tempo runtime préservé après load.** Tempo 100 BPM, enregistrer, save. Tempo 140 BPM. Load. Le loop joue à 140 BPM, le tempo runtime reste 140. |
| **V7** | **`_recordBpm` préservé après load.** Test idem V6 : `_recordBpm` interne reste à 100, seul le live BPM joue. Vérifier via debug serial. |

### Niveau 7 — Performance & stabilité

| Test | Description |
|---|---|
| **P1** | Save d'un slot full (1024 events) : pas de glitch audio sur le loop en cours. Pas de timeout watchdog. |
| **P2** | 16 saves consécutifs (remplir tous les slots) : pas de fragmentation visible, pas de slowdown. |
| **P3** | 100 cycles save/delete/save sur le même slot : test de wear (devrait être absorbé par le wear leveling LittleFS). |
| **P4** | Reboot stress : 20 reboots consécutifs avec le drive plein → tous les slots toujours présents et chargeables. |

---

## 9. Décisions de design en suspens (à acter dans le plan d'implémentation)

1. **Allocation du buffer de sérialisation** : SRAM (default) ou PSRAM. Si la marge SRAM devient tendue après les autres ajouts, basculer en PSRAM via `heap_caps_malloc(MALLOC_CAP_SPIRAM)`.
2. **Documentation utilisateur** : ajouter une section "LOOP Slot Drive" au manual HTML existant et aux descriptions VT100 setup mode des pads concernés.
3. **Valeur exacte de `LOOP_SLOT_MAGIC`** : à choisir au moment de l'implémentation (suggestion : `0x1F00` ou autre valeur 16-bit non encore utilisée par les autres magics du codebase).

---

## 10. Status

**Design approuvé** par Loïc le 2026-04-06. Prêt pour la phase de plan d'implémentation (à écrire séparément, en suivant le pattern des plans Phase 1-5 existants).

Le slot drive sera implémenté comme **Phase 6** du projet LOOP, avec un prerequis : **le refactor Tool 3 vers l'architecture b1 contextuelle, qui doit être incorporé à Phase 3 existante**.
