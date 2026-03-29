# Refactoring Plan — Reduction de complexite

Phase stabilisation V2. Chaque refactor est **pur restructuring** : meme logique, meme ordre d'execution, zero changement de comportement. Compiler apres chaque etape.

**Deja fait** : `loop()` dans main.cpp — 487 lignes decomposees en 6 fonctions nommees (69 lignes restantes). Voir commit a venir.

---

## Chantier 1 — LedController::update() → Table de priorite

### Pourquoi
`update()` fait ~480 lignes, nesting max 6, 9 niveaux de priorite en cascade if/return. Le niveau 7 (confirmation blinks) fait 180 lignes a lui seul avec un switch a 10 cases. Le niveau 9 (normal display) fait 120 lignes avec nesting 5-6. Les bugs BUG-06 et BUG-07 du bugreport 2026-03-29 viennent directement de cette complexite.

### Comment verifier avant de commencer
1. Lire `src/core/LedController.cpp`, methode `update()` — reperer la cascade if/return et les 9 niveaux
2. Lire `src/core/LedController.h` — reperer les enums d'etat, les membres `_bootMode`, `_setupComet`, `_chaseActive`, `_error`, `_showingBattery`, `_showingPotBar`, `_confirmType`, `_calibrationMode`
3. Verifier que chaque niveau fait `return` (sauf `CONFIRM_BANK_SWITCH` qui overlay sur le normal display)
4. Lire `docs/bugreport-2026-03-29.md` BUG-06 et BUG-07 pour comprendre les bugs lies

### Pattern : Extract Method (meme approche que loop())
Extraire chaque niveau de priorite en une fonction `static` dans le meme fichier :

| Priorite | Fonction a extraire | Lignes estimees | Complexite |
|---|---|---|---|
| 1 | `renderBoot()` | ~25 | Simple |
| 2 | `renderComet()` | ~45 | Simple |
| 3 | `renderChase()` | ~10 | Trivial |
| 4 | `renderError()` | ~10 | Trivial |
| 5 | `renderBattery()` | ~15 | Simple |
| 6 | `renderBargraph()` | ~35 | Simple |
| 7 | `renderConfirmation()` | ~180 | **Complexe** — switch 10 cases, PLAY beat-synced |
| 8 | `renderCalibration()` | ~25 | Simple |
| 9 | `renderNormalDisplay()` | ~120 | **Complexe** — nesting 5-6, sine pulse + tick flash |

`update()` devient :
```cpp
void LedController::update() {
    uint32_t now = millis();
    _strip.clear();
    if (_bootMode)       { renderBoot(now); return; }
    if (_setupComet && !_calibrationMode) { renderComet(now); return; }
    if (_chaseActive)    { renderChase(now); return; }
    if (_error)          { renderError(now); return; }
    if (renderBattery(now))   return;   // auto-expire
    if (renderBargraph(now))  return;   // auto-expire
    if (renderConfirmation(now)) {      // BANK_SWITCH falls through
        if (_confirmType != CONFIRM_BANK_SWITCH) return;
    }
    if (_calibrationMode){ renderCalibration(now); return; }
    renderNormalDisplay(now);
    _strip.show();
}
```

**Cas special CONFIRM_BANK_SWITCH** : c'est le seul niveau qui ne fait pas `return` — il laisse le normal display se rendre puis overlay son blink par-dessus. La fonction extraite doit retourner un bool pour indiquer si elle a pris le controle complet ou si le display normal doit aussi tourner.

### Ordre d'implementation
1. Extraire les niveaux triviaux (3, 4, 5) — zero risque, valider le pattern
2. Extraire boot (1) et comet (2)
3. Extraire bargraph (6) et calibration (8)
4. Extraire normalDisplay (9) — nesting profond, tester soigneusement
5. Extraire confirmation (7) — le plus gros, tester chaque ConfirmType

### Fichiers
- `src/core/LedController.cpp` — seul fichier modifie
- `src/core/LedController.h` — les nouvelles methodes sont `private`, declarer dans le .h

### Verification
- `pio run` apres chaque extraction
- Test visuel : boot sequence (8 LEDs progressives), bank switch blink, arp sine pulse, bargraph, error blink

---

## Chantier 2 — NvsManager::loadAll() → Table data-driven

### Pourquoi
`loadAll()` fait 317 lignes, 18 namespaces NVS charges en sequence. Chaque bloc repete le meme pattern open/read/validate/close avec des types et cles differents. Ajouter un 19e namespace = copier-coller 15 lignes et risquer une erreur. Le bug BUG-09 (validation manquante sur ArpPotStore) vient de ce copier-coller.

### Comment verifier avant de commencer
1. Lire `src/managers/NvsManager.cpp`, methode `loadAll()` — reperer le pattern repete
2. Lire `src/core/KeyboardData.h` — reperer les structs *Store (CalDataStore, PadOrderStore, etc.), leurs champs `magic` et `version`
3. Compter les namespaces : chaque `prefs.begin("illpad_xxx")` est un bloc
4. Verifier que chaque bloc fait : open → getBytesLength → getBytes → check magic/version → copy → close
5. Noter les exceptions : certains blocs chargent des tableaux (scale config x8, arp params x8) avec des boucles internes

### Pattern : Data-Driven Table
Creer une table statique qui decrit chaque namespace, et une fonction generique qui les parcourt.

**Attention** : tous les blocs ne sont pas identiques. Il y a 3 familles :
- **Simple struct** : un blob avec magic/version (CalDataStore, SettingsStore, PotMappingStore, LedSettingsStore)
- **Tableau indexe** : 8 valeurs par bank (scale config, velocity, pitch bend, arp params)
- **Tableau fixe** : padOrder[48], bankPads[8], scalePads, arpPads

La table doit supporter ces 3 familles. Approche possible : une fonction `loadSimpleStruct()` et une `loadArray()`, avec la table qui indique quelle fonction appeler.

**Alternative plus simple** : garder `loadAll()` mais extraire les 3 familles en helpers internes, reduisant la repetition sans table formelle. 317 lignes → ~100 lignes. Moins elegant mais plus lisible pour du code embedded.

### Ordre d'implementation
1. Extraire `loadSimpleStruct()` — helper generique pour les 4-5 blocs simples
2. Extraire `loadBankArray()` — helper pour les blocs x8
3. Remplacer les blocs dans loadAll() par des appels aux helpers
4. Verifier que les exceptions (premiers-boot defaults, validation custom) sont preservees

### Fichiers
- `src/managers/NvsManager.cpp` — principal
- `src/managers/NvsManager.h` — declarer les helpers private

### Verification
- `pio run`
- Test : effacer NVS (premier boot), verifier que les defaults sont corrects
- Test : sauvegarder des valeurs dans setup, rebooter, verifier qu'elles sont restaurees

---

## Chantier 3 — ClockManager::update() → Priority Resolution

### Pourquoi
`update()` fait 147 lignes, nesting max 5, avec 6 variables d'etat source qui interagissent (usbTicks, bleTicks, activeTicks, activeSourceId, prevSource, switchedToBle). La cascade de fallback USB → BLE → LastKnown → Internal est cachee dans des if imbriques. Un bug ici est silencieux (le tempo derive, pas de crash).

### Comment verifier avant de commencer
1. Lire `src/midi/ClockManager.cpp`, methode `update()` — reperer les 4 sources et les transitions
2. Lire `src/midi/ClockManager.h` — reperer l'enum `ClockSource` et les membres `_activeSource`, `_lastUsbTickUs`, `_lastBleTickUs`, timeout constants
3. Tracer le flow de fallback : USB timeout → essayer BLE → sinon LastKnown → sinon Internal
4. Reperer le bloc PLL (smoothing des ticks) et le bloc de generation de ticks internes

### Pattern : Separation resolution/action
Decouperupdate() en 2 phases :

```cpp
// Phase 1 : pure logique, pas de side effects
ClockSource resolveSource(uint32_t now);

// Phase 2 : agir sur le changement + PLL + generation
void update() {
    uint32_t now = micros();
    ClockSource newSrc = resolveSource(now);
    if (newSrc != _activeSource) handleSourceTransition(newSrc);
    consumeTicks(newSrc);  // PLL + tick generation
}
```

**Attention** : le PLL utilise des timestamps par source (USB vs BLE). La separation doit preserver l'acces aux bons timestamps selon la source active.

### Ordre d'implementation
1. Extraire `resolveSource()` — la partie decision pure
2. Extraire `handleSourceTransition()` — logging + reset PLL si changement
3. Verifier que le path "no external ticks → internal generation" fonctionne toujours

### Fichiers
- `src/midi/ClockManager.cpp` — principal
- `src/midi/ClockManager.h` — declarer les helpers private

### Verification
- `pio run`
- Test : brancher un DAW USB avec clock → verifier sync. Debrancher → verifier fallback internal. Rebrancher → verifier re-sync.

---

## Chantier 4 — ArpEngine::tick() → Etats nommes

### Pourquoi
`tick()` fait 137 lignes avec 4 flags booleens en entree (`_positionCount`, `_holdOn`, `_playing`, `_waitingForQuantize`) qui representent implicitement 4-5 etats. Le code enchaine des `if` sur ces flags sans nommer les etats, ce qui rend les transitions difficiles a verifier.

### Comment verifier avant de commencer
1. Lire `src/arp/ArpEngine.cpp`, methode `tick()` — reperer les 4 guards en entree
2. Lire `src/arp/ArpEngine.h` — reperer les membres `_positionCount`, `_holdOn`, `_playing`, `_waitingForQuantize`, `_sequence`, `_stepIndex`
3. Tracer les etats implicites :
   - IDLE : `_positionCount == 0`
   - WAITING_QUANTIZE : `_waitingForQuantize == true`
   - PLAYING : `_playing == true` (ou `!_holdOn && _positionCount > 0`)
   - HELD_STOPPED : `_holdOn && !_playing`
4. Verifier dans quel etat on entre quand : addPadPosition (0→1 note), playStop(), hold toggle

### Pattern : Enum explicite + switch
Ajouter un helper `ArpState currentState()` qui nomme les combinaisons de flags. Puis remplacer la cascade de if par un switch sur l'etat.

**Attention** : ne PAS remplacer les 4 flags par un seul enum. Les flags sont ecrits par des fonctions differentes (addPadPosition modifie _positionCount, playStop modifie _playing, etc.). Un enum unique forcerait chaque setter a connaitre toutes les transitions. Le helper `currentState()` est en lecture seule — il observe les flags et nomme l'etat courant.

```cpp
enum class ArpState { IDLE, WAITING_QUANTIZE, PLAYING, HELD_STOPPED };

ArpState ArpEngine::currentState() const {
    if (_positionCount == 0) return ArpState::IDLE;
    if (_waitingForQuantize)  return ArpState::WAITING_QUANTIZE;
    if (_playing)             return ArpState::PLAYING;
    if (_holdOn)              return ArpState::HELD_STOPPED;
    return ArpState::PLAYING;  // HOLD OFF + notes = auto-play
}
```

### Ordre d'implementation
1. Ajouter l'enum et `currentState()` dans le .h
2. Remplacer les guards en entree de `tick()` par un switch
3. Extraire le coeur du step (resolve + schedule) en helper si > 50 lignes

### Fichiers
- `src/arp/ArpEngine.cpp` — principal
- `src/arp/ArpEngine.h` — enum + helper

### Verification
- `pio run`
- Test musical : HOLD OFF (live arp, fingers up = stop), HOLD ON (persistent, play/stop pad), bank switch pendant arp, scale change pendant arp, 4 arps en background

---

## Ordre recommande des chantiers

| # | Chantier | Effort | Risque | Justification |
|---|---|---|---|---|
| 1 | LedController | MOYEN | Faible | Meme pattern que loop(), deja maitrise. Corrige la surface des bugs LED. |
| 2 | NvsManager | FACILE | Faible | Repetition pure, pas de logique musicale. |
| 3 | ClockManager | FACILE | Moyen | Logique delicate (PLL), mais petit fichier. |
| 4 | ArpEngine | MOYEN | Moyen | Touche au timing realtime. Faire en dernier. |

Chaque chantier = 1 session. Lire ce doc + les fichiers cibles, verifier le plan, puis executer.
