# ILLPAD48 V2 — GestureDispatcher : plan d'implémentation

**Date** : 2026-04-26
**Spec source** : [`docs/superpowers/specs/2026-04-26-gesture-dispatcher-design.md`](../specs/2026-04-26-gesture-dispatcher-design.md)
**Statut** : DRAFT pour relecture, aucun code écrit.
**Pré-requis** : aucun. Indépendant de LOOP Phase 1.

Ce document liste les phases dans l'ordre d'exécution, chacune avec :
- Périmètre exact (fichiers touchés)
- Snippet complet du code à écrire / modifier
- Checkpoints (tests manuels au flash, sans bench)
- Critère de validation avant passage à la phase suivante

**Convention** : chaque phase est isolable et compilable. Aucune phase ne casse le firmware courant — les phases 1-4 ajoutent le dispatcher en parallèle, la phase 5 bascule, les phases 6-7 finalisent.

> **Note line refs** : tous les numéros de ligne fichier:N de ce plan correspondent au commit `1c4d7cf` (ARPEG_GEN Phase 4 mergé). Si le code a bougé entre ce commit et l'exécution, vérifier par grep avant d'éditer (`grep -n "void ArpEngine::setCaptured" src/arp/ArpEngine.cpp`, `grep -n "void loop()" src/main.cpp`, etc.).

---

## Vue d'ensemble

```
Phase 0  Snapshot buffer Core 0 / Core 1                    (~30 min)
Phase 1  Squelette GestureDispatcher.h / .cpp + intégration (~4-6 h)
Phase 2  Migration BankManager → dispatcher                 (~2-3 h)
Phase 3  Migration ScaleManager → dispatcher                (~1-2 h)
Phase 4  Migration handleHoldPad + sweep release universel  (~1-2 h)
Phase 5  Pile sacrée — modif setCaptured + suppression code (~1 h)
Phase 6  Bench live + ajustement constantes timing          (~2-3 h)
Phase 7  Hooks LOOP préparatoires (stubs)                   (~1 h)
```

Total : ~15-20 h.

---

## Recap table multi-axes — état par phase

Suivant CLAUDE.md projet (workflow 5-gates, recap table dès que statut compile et HW divergent). Cocher au fur et à mesure :

| Phase | Tasks | Compile | HW gate | Commit |
|---|---|---|---|---|
| 0 | snapshot buffer + isLoopType | ☐ | **N/A** (stubs neutres) | ☐ commit autorisé avant Phase 1 |
| 1 | squelette dispatcher | ☐ | **N/A** (body vide, aucun runtime) | ☐ commit autorisé avec Phase 0 |
| 2 | migration BankManager | ☐ | ☐ **OBLIGATOIRE** — scénarios B-1 à B-6 (Annexe Bench) | ☐ **différé** (commit groupé fin Phase 5) |
| 3 | migration ScaleManager | ☐ | ☐ **OBLIGATOIRE** — scénarios S-1 à S-3 | ☐ **différé** |
| 4 | hold pad + sweep release | ☐ | ☐ **OBLIGATOIRE** — scénarios H-1, H-2 + sweep | ☐ **différé** |
| 5 | pile sacrée | ☐ | ☐ **OBLIGATOIRE** — scénarios P-1 à P-4 + non-régression complète | ☐ **commit groupé Phases 2→5 ici** |
| 6 | bench + tuning constantes | ☐ | ☐ **EST la phase** — itération bench | ☐ commit séparé (tuning constants) |
| 7 | hooks LOOP stubs | ☐ | **N/A** (`_loopEngine = nullptr`) | ☐ commit séparé |

**Principe** : aucun commit n'est gravé tant que le HW gate de la phase n'a pas été validé par toi. Phases 0+1 = commit "stubs" autorisé. Phases 2 à 5 = un seul commit groupé en fin de Phase 5 pour préserver la possibilité d'un rollback granulaire sans graver un état intermédiaire bugué. Phases 6 et 7 = commits séparés.

**Règle d'ordre** : HW gate **AVANT** commit gate, jamais après (cf. CLAUDE.md projet : "Inverser l'ordre même si le plan liste l'inverse — un commit non-testé HW grave un état potentiellement bugué").

---

## Pré-requis decision D9 avant Phase 7

La spec §15 décisions ouvertes liste D9 : "Pad CLEAR tenu très longtemps puis relâché sans combo (long-press dépassé pendant le hold) — wipe fire-t-il au release ?". Cette décision **bloque Phase 7** parce que la signature du hook `LoopEngine::cancelClear()` vs `commitClear()` au release dépend du choix.

**Action requise** : trancher D9 (option A "fire au release" recommandée, option B "annuler" possible) avant de démarrer Phase 7. Sans ça, Phase 7 stube un choix arbitraire que LOOP P2 héritera. La décision peut se prendre n'importe quand entre Phase 0 et Phase 7 — pas un blocker pour démarrer.

---

## Phase 0 — Snapshot buffer Core 0 / Core 1 + pré-requis `isLoopType()`

### Pourquoi

Deux pré-requis triviaux groupés en Phase 0 :

1. **Snapshot buffer** — hors scope strict du gesture dispatcher mais **doit précéder** : tant que `state` est une référence à un buffer mutable par Core 0 en cours de frame, le dispatcher peut voir des incohérences inter-phase. Fix triviale, gain immédiat.
2. **Helper `isLoopType()`** — symétrique de `isArpType()` ([KeyboardData.h:334](../../../src/core/KeyboardData.h:334)). Utilisé par les Phases 2/7 du dispatcher (dispatch `BANK_PAD` toggle LOOP). Doit exister avant compilation de la Phase 2, donc créé maintenant.

### Fichiers touchés

- [`src/main.cpp`](../../../src/main.cpp:951) — snapshot buffer
- [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h:334) — helper `isLoopType`

### Snippet 1 — Snapshot buffer

```cpp
// AVANT (main.cpp:951-953)
void loop() {
  const SharedKeyboardState& state = s_buffers[s_active.load(std::memory_order_acquire)];
  uint32_t now = millis();
  ...

// APRÈS
void loop() {
  // Snapshot buffer atomically, then work on a frozen local copy.
  // Prevents Core 0 from overwriting state mid-loop while managers iterate.
  SharedKeyboardState state;
  {
    uint8_t idx = s_active.load(std::memory_order_acquire);
    state = s_buffers[idx];   // ~96 bytes copy (NUM_KEYS * 2)
  }
  uint32_t now = millis();
  ...
```

`SharedKeyboardState` est défini dans [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h) — vérifié POD trivialement copiable (uint8_t arrays uniquement, pas de pointeurs).

### Snippet 2 — Helper `isLoopType`

Ajouter dans [`KeyboardData.h`](../../../src/core/KeyboardData.h) juste après `isArpType` (vers ligne 336) :

```cpp
// Helper : true si la bank est une LOOP.
// Symétrique de isArpType. Utilisé par GestureDispatcher pour router
// les toggle play/stop bank pad vers LoopEngine au lieu d'ArpEngine.
inline bool isLoopType(BankType t) {
  return t == BANK_LOOP;
}
```

### Gates Phase 0

#### 1. Compile gate

`pio run` exit 0. Grep `isLoopType` retourne la définition.

#### 2. HW gate — **N/A** (stubs neutres, aucun runtime nouveau)

Flash + jeu rapide pour confirmer qu'il n'y a pas de régression latente. Pas de scénario formel requis.

#### 3. Commit gate — autorisé (commit Phase 0)

Présenter au user :

```
chore(gesture): pre-requisites — buffer snapshot + isLoopType helper

Phase 0 du plan GestureDispatcher. Aucun comportement musical modifié.

- main.cpp loop() : SharedKeyboardState copié localement en début de
  frame plutôt que tenu en référence — élimine la race Core 0/1 où
  Core 0 peut overwriter le buffer en cours de frame.
- KeyboardData.h : isLoopType() helper symétrique de isArpType(),
  utilisé par GestureDispatcher Phase 2/7.

Spec : docs/superpowers/specs/2026-04-26-gesture-dispatcher-design.md
Plan : docs/superpowers/plans/2026-04-26-gesture-dispatcher-plan.md
```

Attendre OK explicite avant `git add src/main.cpp src/core/KeyboardData.h` + commit.

---

## Phase 1 — Squelette GestureDispatcher

### Pourquoi

Créer le fichier `GestureDispatcher.h/.cpp` avec **toute l'API publique** finale, mais **logique encore vide** (le dispatcher ne fait rien si pas activé). Permet d'intégrer dans `main.cpp` sans rien casser, puis migrer phase par phase.

### Fichiers créés

- `src/managers/GestureDispatcher.h`
- `src/managers/GestureDispatcher.cpp`

### Fichiers touchés

- `src/main.cpp` : inclusion + instance + appel placeholder

### Snippet complet — `GestureDispatcher.h`

```cpp
#ifndef GESTURE_DISPATCHER_H
#define GESTURE_DISPATCHER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class BankManager;
class ScaleManager;
class ArpEngine;
class LedController;
class MidiEngine;
class MidiTransport;

// =================================================================
// GestureDispatcher — single owner of the LEFT-held + hold-pad
// gesture surface. Replaces the distributed coordination between
// BankManager, ScaleManager, handleHoldPad and handleLeftReleaseCleanup.
//
// See docs/superpowers/specs/2026-04-26-gesture-dispatcher-design.md
// =================================================================

// Compile-time constants — see spec §6
static const uint8_t  LEFT_EDGE_GUARD_MS    = 40;
static const uint8_t  DOUBLE_TAP_DEAD_MS    = 200;
static const uint16_t SHORT_PRESS_MIN_MS    = 300;
static const uint16_t SHORT_PRESS_MAX_MS    = 1000;
// Durées LONG_PRESS lues runtime depuis SettingsStore — voir spec §6.
// slotSaveTimerMs (slot pad LOOP, save) — default 1000ms, user-tunable Tool 6.
// clearLoopTimerMs (pad CLEAR LOOP, wipe) — default 500ms, user-tunable Tool 6.
// Pas de constante compile-time LONG_PRESS_MS — éviter orphan link invariant 7.

class GestureDispatcher {
public:
  GestureDispatcher();

  // --- Wiring (called once at boot) ---
  void begin(BankManager*    bank,
             ScaleManager*   scale,
             LedController*  leds,
             MidiEngine*     engine,
             MidiTransport*  transport,
             BankSlot*       banks,
             uint8_t*        lastKeys);

  // --- Pad role configuration (mirrors BankManager / ScaleManager setters) ---
  void setBankPads(const uint8_t* pads);          // 8 bank pads
  void setRootPads(const uint8_t* pads);          // 7 root pads
  void setModePads(const uint8_t* pads);          // 7 mode pads
  void setChromaticPad(uint8_t pad);
  void setHoldPad(uint8_t pad);
  void setOctavePads(const uint8_t* pads);        // 4 octave pads
  void setDoubleTapMs(uint8_t ms);                // user-tunable [100,250]
  void setSlotSaveTimerMs(uint16_t ms);           // LOOP §11 — [500, 2000]
  void setClearLoopTimerMs(uint16_t ms);          // LOOP §17 — [200, 1500]

  // --- Per-frame entry point ---
  // Replaces in main.cpp:
  //   - s_bankManager.update(state.keyIsPressed, leftHeld)
  //   - s_scaleManager.update(state.keyIsPressed, leftHeld, currentSlot)
  //   - handleHoldPad(state)
  //   - handleLeftReleaseCleanup(state)
  // Returns true if a bank switch was committed this frame (caller may
  // want to reload per-bank pot params and queue NVS writes).
  bool update(const SharedKeyboardState& state, bool leftHeld, uint32_t now);

  // --- Queries ---
  bool isHolding() const;                         // LEFT held now
  uint8_t getCurrentBank() const;                 // forwards to BankManager

  // --- Scale change consumer (for main.cpp NVS / arp scale propagation) ---
  // Returns the type of scale change committed this frame (auto-clears).
  enum class ScaleChange : uint8_t { NONE, ROOT, MODE, CHROMATIC };
  ScaleChange consumeScaleChange();
  bool        consumeOctaveChange();
  uint8_t     getNewOctaveRange() const;

  // --- Per-pad rising-edge timer for processArpMode FG double-tap remove ---
  // Returns true if pad i had a rising edge within doubleTapMs of its
  // previous rising edge (i.e. constitutes a double-tap on a music pad).
  // Used by processArpMode to decide between addPadPosition vs removePadPosition.
  bool consumeMusicPadDoubleTap(uint8_t i, uint32_t now);

private:
  // --- Wiring ---
  BankManager*   _bank;
  ScaleManager*  _scale;
  LedController* _leds;
  MidiEngine*    _engine;
  MidiTransport* _transport;
  BankSlot*      _banks;
  uint8_t*       _lastKeys;

  // --- Pad role config ---
  uint8_t _bankPads[NUM_BANKS];
  uint8_t _rootPads[7];
  uint8_t _modePads[7];
  uint8_t _chromaticPad;
  uint8_t _holdPad;
  uint8_t _octavePads[4];
  uint8_t  _doubleTapMs;
  uint16_t _slotSaveTimerMs;
  uint16_t _clearLoopTimerMs;

  // --- LEFT state ---
  bool     _lastLeftHeld;
  uint32_t _leftEdgeGuardUntil;

  // --- Pad edge tracking (single source of truth for rising/falling) ---
  uint8_t _padLast[NUM_KEYS];

  // --- Tap classifier (global, see spec §8) ---
  uint8_t  _lastTapPad;
  uint32_t _lastTapTime;
  uint8_t  _doubleTapGuardPad;
  uint32_t _doubleTapDeadUntil;

  // --- Pending bank switch (deferred to detect 2nd tap) ---
  int8_t   _pendingSwitchBank;
  uint32_t _pendingSwitchTime;

  // --- Hold pad edge ---
  bool _lastHoldPadState;

  // --- Scale change export ---
  ScaleChange _scaleChangeType;
  bool        _octaveChanged;
  uint8_t     _newOctaveRange;

  // --- Per-pad rising-edge timer for music-pad double-tap (FG ARPEG remove) ---
  // Mirrors the old s_lastPressTime[] from main.cpp.
  uint32_t _musicPadLastRisingTime[NUM_KEYS];

  // --- Internal helpers ---

  // Phase A
  void onLeftPress(const SharedKeyboardState& state, uint32_t now);
  void onLeftRelease(const SharedKeyboardState& state, uint32_t now);

  // Phase B-C : compute rising/falling, apply guards
  // Returns true if rising edge `i` is allowed to proceed.
  bool isRisingAllowed(uint8_t i, uint32_t now) const;

  // Phase E dispatch helpers
  void handleBankPad(uint8_t b, uint32_t now);
  void handleScalePad(uint8_t i, uint8_t roleIdx, char roleType);
  void handleOctavePad(uint8_t roleIdx);
  void handleHoldPad(uint32_t now);

  // Phase F
  void commitPendingSwitch(uint32_t now);

  // §9 universal sweep
  void sweepAtRelease(const SharedKeyboardState& state);

  // Helpers
  bool isBankPad(uint8_t i, uint8_t* outBankIdx) const;
  bool isRootPad(uint8_t i, uint8_t* outRoleIdx) const;
  bool isModePad(uint8_t i, uint8_t* outRoleIdx) const;
  bool isOctavePad(uint8_t i, uint8_t* outRoleIdx) const;
};

#endif // GESTURE_DISPATCHER_H
```

### Snippet complet — `GestureDispatcher.cpp` (squelette, logique vide)

```cpp
#include "GestureDispatcher.h"
#include "BankManager.h"
#include "ScaleManager.h"
#include "../arp/ArpEngine.h"
#include "../midi/MidiEngine.h"
#include "../core/MidiTransport.h"
#include "../core/LedController.h"
#include <Arduino.h>
#include <string.h>

GestureDispatcher::GestureDispatcher()
  : _bank(nullptr), _scale(nullptr), _leds(nullptr), _engine(nullptr),
    _transport(nullptr), _banks(nullptr), _lastKeys(nullptr),
    _chromaticPad(22), _holdPad(23),
    _doubleTapMs(DOUBLE_TAP_MS_DEFAULT),
    _slotSaveTimerMs(1000), _clearLoopTimerMs(500),
    _lastLeftHeld(false), _leftEdgeGuardUntil(0),
    _lastTapPad(0xFF), _lastTapTime(0),
    _doubleTapGuardPad(0xFF), _doubleTapDeadUntil(0),
    _pendingSwitchBank(-1), _pendingSwitchTime(0),
    _lastHoldPadState(false),
    _scaleChangeType(ScaleChange::NONE),
    _octaveChanged(false), _newOctaveRange(1)
{
  for (uint8_t i = 0; i < NUM_BANKS; i++) _bankPads[i] = i;
  for (uint8_t i = 0; i < 7; i++) {
    _rootPads[i] = 8 + i;
    _modePads[i] = 15 + i;
  }
  for (uint8_t i = 0; i < 4; i++) _octavePads[i] = 25 + i;
  memset(_padLast, 0, sizeof(_padLast));
  memset(_musicPadLastRisingTime, 0, sizeof(_musicPadLastRisingTime));
}

void GestureDispatcher::begin(BankManager* bank, ScaleManager* scale,
                              LedController* leds, MidiEngine* engine,
                              MidiTransport* transport, BankSlot* banks,
                              uint8_t* lastKeys) {
  _bank = bank; _scale = scale; _leds = leds; _engine = engine;
  _transport = transport; _banks = banks; _lastKeys = lastKeys;
}

void GestureDispatcher::setBankPads(const uint8_t* pads)   { memcpy(_bankPads, pads, NUM_BANKS); }
void GestureDispatcher::setRootPads(const uint8_t* pads)   { memcpy(_rootPads, pads, 7); }
void GestureDispatcher::setModePads(const uint8_t* pads)   { memcpy(_modePads, pads, 7); }
void GestureDispatcher::setChromaticPad(uint8_t pad)       { _chromaticPad = pad; }
void GestureDispatcher::setHoldPad(uint8_t pad)            { _holdPad = pad; }
void GestureDispatcher::setOctavePads(const uint8_t* pads) { memcpy(_octavePads, pads, 4); }
void GestureDispatcher::setDoubleTapMs(uint8_t ms)         { _doubleTapMs = ms; }
void GestureDispatcher::setSlotSaveTimerMs(uint16_t ms)    { _slotSaveTimerMs = ms; }
void GestureDispatcher::setClearLoopTimerMs(uint16_t ms)   { _clearLoopTimerMs = ms; }

bool GestureDispatcher::update(const SharedKeyboardState& state,
                               bool leftHeld, uint32_t now) {
  // Phase 1 squelette: pas de logique active. Délègue aux managers existants
  // pour préserver le comportement courant pendant la migration.
  // À partir de Phase 2, ce stub disparaît.
  return false;
}

bool GestureDispatcher::isHolding() const          { return _lastLeftHeld; }
uint8_t GestureDispatcher::getCurrentBank() const  { return _bank ? _bank->getCurrentBank() : 0; }

GestureDispatcher::ScaleChange GestureDispatcher::consumeScaleChange() {
  ScaleChange t = _scaleChangeType;
  _scaleChangeType = ScaleChange::NONE;
  return t;
}

bool GestureDispatcher::consumeOctaveChange() {
  bool c = _octaveChanged;
  _octaveChanged = false;
  return c;
}

uint8_t GestureDispatcher::getNewOctaveRange() const { return _newOctaveRange; }

bool GestureDispatcher::consumeMusicPadDoubleTap(uint8_t i, uint32_t now) {
  if (i >= NUM_KEYS) return false;
  bool isDouble = (_musicPadLastRisingTime[i] > 0) &&
                  ((now - _musicPadLastRisingTime[i]) < (uint32_t)_doubleTapMs);
  if (isDouble) {
    _musicPadLastRisingTime[i] = 0;
  } else {
    _musicPadLastRisingTime[i] = now;
  }
  return isDouble;
}

// --- Phase 2+ implementations (vides en Phase 1) ---
void GestureDispatcher::onLeftPress(const SharedKeyboardState& state, uint32_t now)   {}
void GestureDispatcher::onLeftRelease(const SharedKeyboardState& state, uint32_t now) {}
bool GestureDispatcher::isRisingAllowed(uint8_t i, uint32_t now) const                { return true; }
void GestureDispatcher::handleBankPad(uint8_t b, uint32_t now)                        {}
void GestureDispatcher::handleScalePad(uint8_t i, uint8_t roleIdx, char roleType)     {}
void GestureDispatcher::handleOctavePad(uint8_t roleIdx)                              {}
void GestureDispatcher::handleHoldPad(uint32_t now)                                   {}
void GestureDispatcher::commitPendingSwitch(uint32_t now)                             {}
void GestureDispatcher::sweepAtRelease(const SharedKeyboardState& state)              {}

bool GestureDispatcher::isBankPad(uint8_t i, uint8_t* outBankIdx) const {
  for (uint8_t b = 0; b < NUM_BANKS; b++) {
    if (_bankPads[b] == i) { if (outBankIdx) *outBankIdx = b; return true; }
  }
  return false;
}

bool GestureDispatcher::isRootPad(uint8_t i, uint8_t* outRoleIdx) const {
  for (uint8_t r = 0; r < 7; r++) {
    if (_rootPads[r] == i) { if (outRoleIdx) *outRoleIdx = r; return true; }
  }
  return false;
}

bool GestureDispatcher::isModePad(uint8_t i, uint8_t* outRoleIdx) const {
  for (uint8_t m = 0; m < 7; m++) {
    if (_modePads[m] == i) { if (outRoleIdx) *outRoleIdx = m; return true; }
  }
  return false;
}

bool GestureDispatcher::isOctavePad(uint8_t i, uint8_t* outRoleIdx) const {
  for (uint8_t o = 0; o < 4; o++) {
    if (_octavePads[o] == i) { if (outRoleIdx) *outRoleIdx = o; return true; }
  }
  return false;
}
```

### Intégration `main.cpp` Phase 1

```cpp
// main.cpp — ajouter en haut, près des autres includes managers
#include "managers/GestureDispatcher.h"

// main.cpp — ajouter près des autres globals (après ControlPadManager)
static GestureDispatcher s_gestureDispatcher;

// main.cpp — dans setup(), après s_bankManager.setHoldPad(holdPad);
s_gestureDispatcher.begin(&s_bankManager, &s_scaleManager,
                          &s_leds, &s_midiEngine, &s_transport,
                          s_banks, s_lastKeys);
s_gestureDispatcher.setBankPads(bankPads);
s_gestureDispatcher.setRootPads(rootPads);
s_gestureDispatcher.setModePads(modePads);
s_gestureDispatcher.setChromaticPad(chromaticPad);
s_gestureDispatcher.setHoldPad(holdPad);
s_gestureDispatcher.setOctavePads(octavePads);
s_gestureDispatcher.setDoubleTapMs(s_doubleTapMs);
s_gestureDispatcher.setSlotSaveTimerMs(s_settings.slotSaveTimerMs);
s_gestureDispatcher.setClearLoopTimerMs(s_settings.clearLoopTimerMs);

// main.cpp — dans loop(), AVANT handleManagerUpdates, ajouter (sans rien remplacer)
// Phase 1 : appel à vide, ne fait rien. Phase 2+ : remplacera handleManagerUpdates.
s_gestureDispatcher.update(state, leftHeld, now);
```

### Gates Phase 1

#### 1. Compile gate

`pio run` exit 0. Grep `GestureDispatcher` dans `src/managers/` → définition. Grep `s_gestureDispatcher` dans `src/main.cpp` → instance + begin() + setters.

#### 2. HW gate — **N/A** (body vide, aucun runtime actif)

Flash rapide : aucun comportement musical modifié. Debug serial confirme l'instanciation du dispatcher.

#### 3. Commit gate — autorisé (commit Phase 1 séparé OU groupé avec Phase 0)

Option A : commit séparé.

```
feat(gesture): GestureDispatcher skeleton + main.cpp integration

Phase 1 du plan GestureDispatcher. Squelette de la classe + API publique
finale + setters Tool 3. Body update() vide — aucun comportement runtime
modifié. Phases 2-5 implémenteront progressivement la logique.

Spec : docs/superpowers/specs/2026-04-26-gesture-dispatcher-design.md
Plan : docs/superpowers/plans/2026-04-26-gesture-dispatcher-plan.md
```

Option B : grouper avec Phase 0 si Phase 0 pas encore commitée (économise un commit cosmétique).

Attendre OK explicite avant commit.

---

## Phase 2 — Migration BankManager → dispatcher

### Pourquoi

Implémenter Phase A (LEFT edge) + Phase D-E pour BANK_PAD + Phase F (pending commit). À la fin de cette phase, le dispatcher remplace **toute la logique LEFT-held + bank pad** de BankManager. BankManager devient une façade contenant uniquement `switchToBank()` et les getters.

### Fichiers touchés

- `src/managers/GestureDispatcher.cpp` : implémentation Phase A, D-E (BANK_PAD), F
- `src/managers/BankManager.cpp` : suppression `update()` body, garder `switchToBank()` et accesseurs
- `src/managers/BankManager.h` : suppression membres `_pendingSwitch*`, `_doubleTapMs`, `_lastBankPadPressTime[]`, `_bankPadLast[]`, `_holdPad`, `_switchedDuringHold`
- `src/main.cpp` : remplacer `s_bankManager.update()` par dispatcher, remplacer setters

### Snippet — `GestureDispatcher::update()` Phase 2

Remplace le stub précédent.

```cpp
bool GestureDispatcher::update(const SharedKeyboardState& state,
                               bool leftHeld, uint32_t now) {
  bool bankSwitched = false;

  // --- Phase A — Edge LEFT ---
  bool leftPress   = leftHeld  && !_lastLeftHeld;
  bool leftRelease = !leftHeld &&  _lastLeftHeld;
  if (leftPress)   onLeftPress(state, now);
  if (leftRelease) onLeftRelease(state, now);

  // --- Phase B — Edge pads + Phase C garde ---
  // Calcul rising/falling pour chaque pad, mise à jour _padLast.
  // Les rising sont stockés temporairement pour Phase D-E.
  bool rising[NUM_KEYS];
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    bool pressed = state.keyIsPressed[i];
    rising[i] = pressed && !_padLast[i] && isRisingAllowed(i, now);
    _padLast[i] = pressed;
  }

  // --- Phase D-E — Dispatch ---
  if (leftHeld) {
    // BANK_PAD : 1 action max par frame (spec §12 Q2).
    // Premier rising bank pad de la frame wins, les suivants sont ignorés.
    for (uint8_t b = 0; b < NUM_BANKS; b++) {
      uint8_t pad = _bankPads[b];
      if (pad >= NUM_KEYS) continue;
      if (!rising[pad]) continue;
      handleBankPad(b, now);
      break;  // 1 BANK_PAD action par frame
    }
    // SCALE_PAD, OCTAVE_PAD : Phase 3
    // HOLD_PAD : Phase 4
  } else {
    // Hors LEFT : HOLD_PAD reste actif (Q5)
    if (_holdPad < NUM_KEYS && rising[_holdPad]) {
      handleHoldPad(now);
    }
    // Music pads et control pads sont gérés en dehors du dispatcher
    // (processNormalMode / processArpMode / ControlPadManager).
  }

  // --- Phase F — Pending commit (BANK_PAD switch différé) ---
  if (_pendingSwitchBank >= 0 &&
      (now - _pendingSwitchTime) >= _doubleTapMs) {
    commitPendingSwitch(now);
    bankSwitched = true;
  }

  // Fast-forward pending au release LEFT (cohérent avec ancien comportement)
  // Note: onLeftRelease() peut aussi commit si pending armé — voir impl ci-dessous.
  // (le flag bankSwitched est set là-bas si applicable)

  _lastLeftHeld = leftHeld;
  return bankSwitched;
}
```

### Snippet — `GestureDispatcher::onLeftPress` / `onLeftRelease`

```cpp
void GestureDispatcher::onLeftPress(const SharedKeyboardState& state, uint32_t now) {
  _leftEdgeGuardUntil = now + LEFT_EDGE_GUARD_MS;
  // Snapshot des pads déjà pressés au press LEFT pour ne pas les voir comme
  // rising edge par la suite.
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    _padLast[i] = state.keyIsPressed[i];
  }
}

void GestureDispatcher::onLeftRelease(const SharedKeyboardState& state, uint32_t now) {
  // Sweep universel (§9) — applicable à toutes les FG types
  sweepAtRelease(state);

  _leftEdgeGuardUntil = now + LEFT_EDGE_GUARD_MS;

  // Reset état tap classifier + pendingSwitch (§15 Phase A)
  _lastTapPad = 0xFF;
  _lastTapTime = 0;
  _doubleTapGuardPad = 0xFF;
  _doubleTapDeadUntil = 0;

  // Fast-forward pending switch si armé
  if (_pendingSwitchBank >= 0) {
    uint8_t target = (uint8_t)_pendingSwitchBank;
    _pendingSwitchBank = -1;
    if (target != _bank->getCurrentBank()) {
      _bank->switchToBank(target);
      // Le caller (loop) verra bankSwitched=true au prochain return
      // → on doit signaler. Solution: stocker dans un flag membre.
      // Voir _commitForceFlag ci-dessous (ajout simple).
    }
  }

  // Snapshot _lastKeys pour éviter phantom notes après release
  if (_lastKeys) {
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
      _lastKeys[i] = state.keyIsPressed[i];
    }
  }
}
```

**Note** : `onLeftRelease()` peut commit un switch fast-forward. Le retour `bankSwitched` de `update()` doit refléter ça. Solution simple : ajouter un membre `_switchedThisFrame` set à `true` dans `commitPendingSwitch()` + `onLeftRelease()`, et lu/reset à la fin de `update()`. À ajouter dans le `.h`.

### Snippet — `GestureDispatcher::isRisingAllowed`

```cpp
bool GestureDispatcher::isRisingAllowed(uint8_t i, uint32_t now) const {
  // Hold pad : toujours autorisé, peu importe la garde LEFT (Q5 / spec §10)
  if (i == _holdPad) return true;

  // Garde LEFT (§7)
  if (now < _leftEdgeGuardUntil) return false;

  // Garde double-tap (§7)
  if (i == _doubleTapGuardPad && now < _doubleTapDeadUntil) return false;

  return true;
}
```

### Snippet — `GestureDispatcher::handleBankPad`

```cpp
void GestureDispatcher::handleBankPad(uint8_t b, uint32_t now) {
  uint8_t pad = _bankPads[b];

  // Tap classifier global (§8)
  bool isDouble = (_lastTapPad == pad) &&
                  (now - _lastTapTime < (uint32_t)_doubleTapMs);

  if (isDouble) {
    // TAP_DOUBLE : toggle play/stop sur target, jamais bank switch (spec §8)
    _doubleTapGuardPad = pad;
    _doubleTapDeadUntil = now + DOUBLE_TAP_DEAD_MS;
    _lastTapPad = 0xFF;
    _lastTapTime = 0;

    // Annule pending switch (§15 Phase E)
    _pendingSwitchBank = -1;

    BankSlot& target = _banks[b];
    if (isArpType(target.type) && target.arpEngine) {
      // Pile sacrée (§13) : signature simplifiée, pas de keys
      bool wasCaptured = target.arpEngine->isCaptured();
      target.arpEngine->setCaptured(!wasCaptured, *_transport);
      if (_leds) {
        EventId evt = target.arpEngine->isCaptured() ? EVT_PLAY : EVT_STOP;
        _leds->triggerEvent(evt, (uint8_t)(1 << b));
      }
    }
    // BANK_LOOP : Phase 7 hook (LoopEngine.toggle)
    return;
  }

  // TAP_SINGLE
  _lastTapPad = pad;
  _lastTapTime = now;

  uint8_t cur = _bank->getCurrentBank();
  if (b == cur) {
    // 1er tap sur FG : silencieux, attente d'un éventuel 2e tap
    return;
  }

  // 1er tap sur BG : arme pendingSwitch
  _pendingSwitchBank = (int8_t)b;
  _pendingSwitchTime = now;
}
```

### Snippet — `GestureDispatcher::commitPendingSwitch`

```cpp
void GestureDispatcher::commitPendingSwitch(uint32_t now) {
  uint8_t target = (uint8_t)_pendingSwitchBank;
  _pendingSwitchBank = -1;
  if (target == _bank->getCurrentBank()) return;
  // Hook pré-commit (Phase 7) : LoopEngine.preBankSwitch() si bank source LOOP
  _bank->switchToBank(target);
}
```

### Snippet — `GestureDispatcher::sweepAtRelease`

```cpp
void GestureDispatcher::sweepAtRelease(const SharedKeyboardState& state) {
  if (!_lastKeys) return;
  // Snapshot non-destructif. Le sweep resynchronise _lastKeys uniquement.
  // Aucun removePadPosition, aucun noteOff sweep en ARPEG (pile sacrée Q3,
  // spec §9). Le bug "pile s'efface au LEFT release" (diagnostiqué 2026-05-15
  // par log timestampé montrant les -note cascadent dans la même ms que le
  // release LEFT) venait du sweep destructif handleLeftReleaseCleanup branche
  // ARPEG-OFF — supprimé par le fix séparé avant cette Phase 4. Le dispatcher
  // doit NE PAS le réintroduire.
  //
  // Politique par FG type :
  // - BANK_NORMAL  : snapshot + noteOff sweep (préservé, code actuel correct)
  // - BANK_ARPEG / BANK_ARPEG_GEN : snapshot SEUL (pas de removePadPosition)
  // - BANK_LOOP    : snapshot SEUL (LOOP P2 ajoute sa propre logique si nécessaire)
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    _lastKeys[i] = state.keyIsPressed[i];
  }

  // NoteOff sweep pour FG NORMAL uniquement (cohérent avec [main.cpp:566-588]
  // branche `relSlot.type == BANK_NORMAL` actuelle, à migrer en Phase 4)
  if (_bank) {
    BankSlot& fg = _banks[_bank->getCurrentBank()];
    if (fg.type == BANK_NORMAL) {
      for (uint8_t i = 0; i < NUM_KEYS; i++) {
        if (/*_isControlPad*/ false) continue;  // hook ControlPadManager check
        if (!state.keyIsPressed[i]) {
          if (_engine) _engine->noteOff(i);
        }
      }
    }
  }
}
```

**Cohabitation transitoire pendant Phase 2-3** : le fix `fix(arpeg)` du 2026-05-15 supprime déjà la branche ARPEG-OFF destructive dans `handleLeftReleaseCleanup()` du code actuel. Phase 4 du plan gesture supprime entièrement `handleLeftReleaseCleanup()`, donc à ce moment-là le dispatcher reprend la branche NORMAL noteOff sweep — qui était la seule branche utile restante.

### Modifications BankManager.cpp / .h

Suppression du body de `update()` qui n'a plus de raison d'être appelé. Tous les setters listés dans Annexe A de la spec sont supprimés. **Mais** `isHolding()` est conservée comme façade redirigée vers le dispatcher — main.cpp:568 et :594 (`handleLeftReleaseCleanup`, `handlePadInput`) l'utilisent encore en Phases 2-3, suppression effective seulement en Phase 4. Le BankManager garde donc :

```cpp
// BankManager.h — version Phase 2 (transitoire jusqu'à Phase 4)
class BankManager {
public:
  BankManager();
  void begin(MidiEngine* engine, LedController* leds, BankSlot* banks,
             MidiTransport* transport);   // _lastKeys retiré (cf F-AUDITa-04)

  uint8_t   getCurrentBank() const;
  BankSlot& getCurrentSlot();
  void      setCurrentBank(uint8_t bank);
  void      switchToBank(uint8_t newBank);

  // Façade transitoire — redirige vers le dispatcher. Supprimée en Phase 4
  // quand handleLeftReleaseCleanup et handlePadInput cessent de l'appeler.
  bool      isHolding() const;

  // Wiring optionnel pour la façade isHolding (set par main.cpp Phase 2)
  void      setGestureDispatcher(class GestureDispatcher* gd);

private:
  MidiEngine*       _engine;
  LedController*    _leds;
  BankSlot*         _banks;
  MidiTransport*    _transport;
  uint8_t           _currentBank;
  class GestureDispatcher* _gd;   // façade isHolding
};
```

```cpp
// BankManager.cpp — implémentation Phase 2 transitoire
bool BankManager::isHolding() const {
  return _gd ? _gd->isHolding() : false;
}

void BankManager::setGestureDispatcher(GestureDispatcher* gd) { _gd = gd; }
```

Mêmes adjustments pour `ScaleManager::isHolding()` (façade redirigée).

`switchToBank()` et `setCurrentBank()` sont conservés tels quels (la logique pitch bend / channel / allNotesOff / triggerEvent est intacte). **Le membre `_lastKeys` est supprimé** (cf F-AUDITa-04 de l'audit cross-vérification : seul usage actuel est le snapshot phantom note BankManager.cpp:146-150 qui migre intégralement au dispatcher Phase 2).

### Conséquences sur main.cpp setup() Phase 2

L'appel `s_bankManager.begin(&s_midiEngine, &s_leds, s_banks, s_lastKeys, &s_transport)` ([main.cpp:420](../../../src/main.cpp:420)) perd son paramètre `s_lastKeys`. Ajouter dans setup() après instanciation du dispatcher :

```cpp
s_bankManager.setGestureDispatcher(&s_gestureDispatcher);
s_scaleManager.setGestureDispatcher(&s_gestureDispatcher);
```

En Phase 4, ces façades + setters sont supprimés ainsi que `s_scaleManager` complètement.

### Modifications main.cpp Phase 2

```cpp
// AVANT (main.cpp:638-700, dans handleManagerUpdates)
bool bankSwitched = s_bankManager.update(state.keyIsPressed, leftHeld);

// APRÈS
bool bankSwitched = s_gestureDispatcher.update(state, leftHeld, now);

// Suppression de handleHoldPad() de loop() (handled by dispatcher)
// Phase 4 : suppression effective. Phase 2 : on garde temporairement,
// mais le dispatcher ne fait pas encore le hold pad. Le code coexiste.
```

### Gates Phase 2 — séquence stricte

#### 1. Compile gate

`pio run -e esp32-s3-devkitc-1` → exit 0, 0 nouveau warning.

Auto-review : grep `setCaptured`, `switchToBank`, `isHolding` dans `src/` pour vérifier que tous les call sites pointent encore quelque part (façade ou dispatcher).

#### 2. HW gate — OBLIGATOIRE

Flash le firmware et exécuter les scénarios bench **dans l'ordre**. Cocher les résultats. Tout fail = STOP, ne pas passer à Phase 3, retour Phase 2.

- ☐ **B-1** Bank switch normal (LEFT + tap BG_pad → switch après 200ms). Pas de note parasite à l'arrivée.
- ☐ **B-2** Double-tap BG ARPEG (LEFT + 2 taps rapides BG_pad ARPEG) → play/stop sur cette BG, **pas de switch**.
- ☐ **B-3** Double-tap FG ARPEG (LEFT + 2 taps rapides FG_pad) → play/stop FG, **pas de switch**.
- ☐ **B-4** [F2] Double-tap à la limite 200ms (chronométré au mieux) → ne switch plus de banque. Au pire silence, jamais bank switch involontaire.
- ☐ **B-5** [F5] Faux double-tap après LEFT release/re-press rapide → ne déclenche plus de play/stop fantôme.
- ☐ **B-6** [F6] Brossage de bank pad adjacent pendant double-tap → ne redirige plus le pendingSwitch.

**Connu non-résolu en Phase 2** : F1 (pile vidée sur double-tap stop FG) reste — sera résolu Phase 5. Documenter explicitement : "B-3 toggle play/stop FG fonctionne, mais la pile est encore vidée comme avant Phase 5".

**Présentation au user** : lister B-1 à B-6 + leur résultat, attendre OK explicite "HW Phase 2 OK" avant de passer à Phase 3.

#### 3. Commit gate — **DIFFÉRÉ**

Pas de commit en fin de Phase 2. Le commit groupé Phases 2→5 se fait en fin de Phase 5. Le code reste en working tree.

Mettre à jour la **recap table** ci-dessus (Phase 2 : compile ✓, HW ✓ B-1 à B-6, commit reporté).

---

## Phase 3 — Migration ScaleManager → dispatcher

### Pourquoi

Le dispatcher prend en charge les scale pads + octave pads. ScaleManager devient façade ou est supprimé.

**Notes** :
- `ScaleManager::_holdPad` + son setter `setHoldPad()` sont des **orphan link** déjà présents dans le code actuel (le membre est écrit mais jamais lu — vérifié par grep sur `ScaleManager.cpp`). À supprimer dans cette phase.
- Rename API : `ScaleManager::hasOctaveChanged()` est utilisée à [main.cpp:690](../../../src/main.cpp:690). Le dispatcher expose `consumeOctaveChange()` (sémantique identique : flag self-clearing). C'est un rename, à appliquer aussi au call site main.cpp dans cette phase.

### Fichiers touchés

- `src/managers/GestureDispatcher.cpp` : ajout dispatch SCALE_PAD, OCTAVE_PAD
- `src/managers/ScaleManager.cpp/.h` : suppression `update()` body, suppression `processScalePads()`, suppression `_holdPad` + `setHoldPad()` (orphan link). Garder peut-être uniquement la struct `ScaleChangeType` (à terme migrée vers `GestureDispatcher::ScaleChange`)
- `src/main.cpp` : remplacer `s_scaleManager.update()` par appel dispatcher consumer ; rename `hasOctaveChanged` → `consumeOctaveChange` au call site ; supprimer `s_scaleManager.setHoldPad(holdPad)` au setup (orphan)

### Snippet — Extension Phase D-E dans `update()`

Ajouter dans la branche `if (leftHeld)` après le bloc BANK_PAD :

```cpp
    // SCALE_PAD : 1 action par frame, root XOR mode XOR chrom (§12)
    bool scaleActionDone = false;
    for (uint8_t r = 0; r < 7 && !scaleActionDone; r++) {
      uint8_t pad = _rootPads[r];
      if (pad < NUM_KEYS && rising[pad]) {
        handleScalePad(pad, r, 'R');
        scaleActionDone = true;
      }
    }
    for (uint8_t m = 0; m < 7 && !scaleActionDone; m++) {
      uint8_t pad = _modePads[m];
      if (pad < NUM_KEYS && rising[pad]) {
        handleScalePad(pad, m, 'M');
        scaleActionDone = true;
      }
    }
    if (!scaleActionDone && _chromaticPad < NUM_KEYS && rising[_chromaticPad]) {
      handleScalePad(_chromaticPad, 0, 'C');
    }

    // OCTAVE_PAD : 1 action par frame
    BankSlot& cur = _banks[_bank->getCurrentBank()];
    if (isArpType(cur.type) && cur.arpEngine) {
      for (uint8_t o = 0; o < 4; o++) {
        uint8_t pad = _octavePads[o];
        if (pad < NUM_KEYS && rising[pad]) {
          handleOctavePad(o);
          break;
        }
      }
    }
```

### Snippet — `GestureDispatcher::handleScalePad`

```cpp
void GestureDispatcher::handleScalePad(uint8_t i, uint8_t roleIdx, char roleType) {
  BankSlot& slot = _banks[_bank->getCurrentBank()];

  // NORMAL : all notes off avant changement de scale (orphans)
  if (slot.type == BANK_NORMAL && _engine) _engine->allNotesOff();

  switch (roleType) {
    case 'R':
      slot.scale.root = roleIdx;
      slot.scale.chromatic = false;
      _scaleChangeType = ScaleChange::ROOT;
      break;
    case 'M':
      slot.scale.mode = roleIdx;
      slot.scale.chromatic = false;
      _scaleChangeType = ScaleChange::MODE;
      break;
    case 'C':
      slot.scale.chromatic = true;
      _scaleChangeType = ScaleChange::CHROMATIC;
      break;
  }

  #if DEBUG_SERIAL
  Serial.printf("[SCALE] %c idx=%u\n", roleType, roleIdx);
  #endif
}
```

### Snippet — `GestureDispatcher::handleOctavePad`

```cpp
void GestureDispatcher::handleOctavePad(uint8_t roleIdx) {
  BankSlot& slot = _banks[_bank->getCurrentBank()];
  if (!isArpType(slot.type) || !slot.arpEngine) return;

  _newOctaveRange = roleIdx + 1;
  _octaveChanged = true;

  if (slot.arpEngine->getEngineMode() == EngineMode::GENERATIVE) {
    slot.arpEngine->setMutationLevel(roleIdx + 1);
  } else {
    slot.arpEngine->setOctaveRange(roleIdx + 1);
  }
}
```

### Modifications main.cpp Phase 3

```cpp
// AVANT (handleManagerUpdates):
s_scaleManager.update(state.keyIsPressed, leftHeld, s_bankManager.getCurrentSlot());
ScaleChangeType scaleChange = s_scaleManager.consumeScaleChange();
bool scaleChanged = (scaleChange != SCALE_CHANGE_NONE);
bool octaveChanged = s_scaleManager.hasOctaveChanged();

// APRÈS:
GestureDispatcher::ScaleChange scaleChange = s_gestureDispatcher.consumeScaleChange();
bool scaleChanged = (scaleChange != GestureDispatcher::ScaleChange::NONE);
bool octaveChanged = s_gestureDispatcher.consumeOctaveChange();

// Le bloc qui propage scale via scaleGroup et appelle queueScaleWrite reste
// inchangé — il consomme juste les flags.
```

`ScaleManager` peut être supprimé entièrement après cette phase si plus aucun appel ne référence ses méthodes. Vérifier qu'il n'est pas utilisé par les Tools setup mode.

### Gates Phase 3 — séquence stricte

#### 1. Compile gate

`pio run` exit 0. Grep `processScalePads`, `hasOctaveChanged`, `_holdPad` dans `src/managers/` pour vérifier la suppression effective.

#### 2. HW gate — OBLIGATOIRE

- ☐ **S-1** Root change (LEFT + tap rootPad) → root change immédiat sur la bank FG, LED confirm `EVT_SCALE_ROOT`.
- ☐ **S-2** Mode change (LEFT + tap modePad) → mode change immédiat, LED confirm `EVT_SCALE_MODE`.
- ☐ **S-3** Chromatic toggle (LEFT + tap chromaticPad) → chromatique on, LED confirm `EVT_SCALE_CHROM`.
- ☐ **S-4** Octave change FG ARPEG (LEFT + tap octavePad) → octaveRange ou mutationLevel selon EngineMode, LED `EVT_OCTAVE`.
- ☐ **S-5** Propagation scaleGroup : si bank FG appartient à un groupe, scale change propage aux autres membres du groupe. Vérifier au moins une bank groupée.
- ☐ **S-6** Régression bank pads : tester B-1 à B-6 de Phase 2 → pas de cassure introduite par Phase 3.

**Présentation au user** : lister S-1 à S-6 + résultat, attendre OK explicite "HW Phase 3 OK".

#### 3. Commit gate — **DIFFÉRÉ**

Code reste en working tree. Mettre à jour recap table.

---

## Phase 4 — Migration handleHoldPad + sweep release universel

### Pourquoi

Le hold pad et le sweep release sont les derniers morceaux du geste à migrer. Une fois fait, `handleLeftReleaseCleanup()` et `handleHoldPad()` peuvent être supprimés de main.cpp.

### Fichiers touchés

- `src/managers/GestureDispatcher.cpp` : implémentation `handleHoldPad()`, sweep release
- `src/main.cpp` : suppression des fonctions correspondantes, suppression de l'état statique `s_lastPressTime[]`

### Snippet — `GestureDispatcher::handleHoldPad`

```cpp
// Sans LEFT : toggle FG. Avec LEFT : scope étendu via toggleAllArps (§10.1).
void GestureDispatcher::handleHoldPad(uint32_t now, bool leftHeld) {
  if (_holdPad >= NUM_KEYS) return;

  if (leftHeld) {
    // Geste LEFT + hold pad simple tap (spec gesture §10.1, fix 2026-05-15) :
    // toggle global symétrique sur toutes les banks ARPEG/ARPEG_GEN (+ futur LOOP).
    toggleAllArps();
    return;
  }

  // Hors LEFT : toggle FG uniquement
  BankSlot& slot = _banks[_bank->getCurrentBank()];
  if (!isArpType(slot.type) || !slot.arpEngine) return;

  // Pile sacrée (§13) : signature simplifiée
  bool wasCaptured = slot.arpEngine->isCaptured();
  slot.arpEngine->setCaptured(!wasCaptured, *_transport);
  if (_leds) {
    _leds->triggerEvent(slot.arpEngine->isCaptured() ? EVT_PLAY : EVT_STOP);
  }

  // Reset des timers de double-tap music pad : après un toggle hold pad,
  // on repart à zéro pour le double-tap remove dans processArpMode.
  memset(_musicPadLastRisingTime, 0, sizeof(_musicPadLastRisingTime));
}

// Helper privé toggleAllArps (cf spec gesture §10.1) — itère sur toutes les
// banks isArpType (+ futur isLoopType), bascule Play↔Stop symétriquement,
// LED multi-bank mask.
void GestureDispatcher::toggleAllArps() {
  bool anyPlaying = false;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (isArpType(_banks[i].type) && _banks[i].arpEngine
        && _banks[i].arpEngine->isCaptured()) { anyPlaying = true; break; }
  }
  uint8_t mask = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (!isArpType(_banks[i].type) || !_banks[i].arpEngine) continue;
    if (anyPlaying && _banks[i].arpEngine->isCaptured()) {
      _banks[i].arpEngine->setCaptured(false, *_transport);
      mask |= (uint8_t)(1 << i);
    } else if (!anyPlaying && _banks[i].arpEngine->isPaused()
                              && _banks[i].arpEngine->hasNotes()) {
      _banks[i].arpEngine->setCaptured(true, *_transport);
      mask |= (uint8_t)(1 << i);
    }
  }
  if (mask != 0 && _leds) {
    _leds->triggerEvent(anyPlaying ? EVT_STOP : EVT_PLAY, mask);
  }
}
```

L'appel depuis `update()` Phase 4 devient : `handleHoldPad(now, leftHeld)` au lieu de `handleHoldPad(now)`.

### Modifications main.cpp Phase 4

```cpp
// SUPPRIMER (main.cpp:704-721):
static void handleHoldPad(const SharedKeyboardState& state) { ... }

// SUPPRIMER (main.cpp:566-588):
static void handleLeftReleaseCleanup(const SharedKeyboardState& state) { ... }

// SUPPRIMER (main.cpp:77):
static uint32_t s_lastPressTime[NUM_KEYS];
// (remplacé par _musicPadLastRisingTime[] interne au dispatcher)

// MODIFIER processArpMode (main.cpp:528-564) — utiliser le dispatcher pour
// le double-tap detection sur music pads:
static void processArpMode(const SharedKeyboardState& state, BankSlot& slot, uint32_t now) {
  for (int i = 0; i < NUM_KEYS; i++) {
    if (i == s_holdPad) continue;
    if (s_controlPadManager.isControlPad(i)) continue;

    bool pressed    = state.keyIsPressed[i];
    bool wasPressed = s_lastKeys[i];
    uint8_t pos = s_padOrder[i];

    if (pressed && !wasPressed) {
      if (slot.arpEngine->isCaptured()) {
        // Double-tap detection via dispatcher (timer per-pad partagé)
        if (s_gestureDispatcher.consumeMusicPadDoubleTap((uint8_t)i, now)) {
          slot.arpEngine->removePadPosition(pos);
        } else {
          slot.arpEngine->addPadPosition(pos);
        }
      } else {
        if (slot.arpEngine->isPaused() && slot.arpEngine->hasNotes()) {
          slot.arpEngine->clearAllNotes(s_transport);
        }
        slot.arpEngine->addPadPosition(pos);
      }
    } else if (!pressed && wasPressed) {
      if (!slot.arpEngine->isCaptured()) {
        slot.arpEngine->removePadPosition(pos);
      }
    }
  }
}

// SUPPRIMER de loop():
handleHoldPad(state);
handlePadInput(state, now);  // ← garder mais sans handleLeftReleaseCleanup à l'intérieur

// handlePadInput devient :
static void handlePadInput(const SharedKeyboardState& state, uint32_t now) {
  if (!s_gestureDispatcher.isHolding()) {
    BankSlot& slot = s_bankManager.getCurrentSlot();
    switch (slot.type) {
      case BANK_NORMAL: processNormalMode(state, slot); break;
      case BANK_ARPEG:
      case BANK_ARPEG_GEN:
        if (slot.arpEngine) processArpMode(state, slot, now);
        break;
      default: break;
    }
  }
  // Plus de handleLeftReleaseCleanup — le sweep est fait par le dispatcher
}
```

### Gates Phase 4 — séquence stricte

#### 1. Compile gate

`pio run` exit 0. Grep `handleHoldPad`, `handleLeftReleaseCleanup`, `s_lastPressTime` dans `src/` → 0 hit (sauf historique git).

#### 2. HW gate — OBLIGATOIRE (phase à plus haut risque musical)

- ☐ **H-1** Hold pad pendant LEFT held → toggle play/stop FG ARPEG ; pile préservée (vérifier post-Phase 5 ; Phase 4 isolée garde encore le wipe sur fingers down).
- ☐ **H-2** Hold pad hors LEFT → idem.
- ☐ **H-3** [§9 sweep] Maintenir LEFT + presser un pad musical + relâcher LEFT (pad encore pressé) → aucun noteOn / aucune addPadPosition déclenché au release, dans **tous** les types : NORMAL, ARPEG-OFF, **ARPEG-ON**, futur LOOP.
- ☐ **H-4** [§7 garde LEFT-press] Doigt sur pad musical déjà pressé + press LEFT → le pad n'est pas vu comme rising edge sous LEFT (pas d'action bank/scale/hold parasite).
- ☐ **H-5** Macro-geste réel du brief : `LEFT held + stop BG6 + start BG5 + switch B7 + scale change + switch FG8 + release LEFT`. Vérifier chaque étape musicalement.
- ☐ **H-6** [F2/F3] Re-bench triple-tap rapide → 3e tap pas pris comme nouveau 1er tap (garde double-tap §7 active).
- ☐ **H-7** Régression scale + bank pads : re-jouer B-1 à B-6, S-1 à S-6.

**Présentation au user** : lister H-1 à H-7, attendre OK explicite "HW Phase 4 OK". En cas de fail sur H-3 ou H-5, ne pas tenter Phase 5 — retour Phase 4 pour ajuster sweep ou gardes.

#### 3. Commit gate — **DIFFÉRÉ**

Code reste en working tree. Mettre à jour recap table.

---

## Phase 5 — Pile sacrée : modif setCaptured + suppression chemins morts

### Pourquoi

Suppression de la branche `anyFingerDown → clearAllNotes()` dans `ArpEngine::setCaptured()` ([ArpEngine.cpp:540-545](../../../src/arp/ArpEngine.cpp:540)). Suppression du paramètre `keyIsPressed` et `holdPadIdx`. Résolution F1.

**Périmètre strict** : seul le wipe "transport" est supprimé (gestes bank pad / hold pad). Le wipe "engagé musical" dans `processArpMode` en [main.cpp:550-552](../../../src/main.cpp:550) (1er press en Stop avec paused pile) est **conservé tel quel** — voir spec §13.2.

> **Note line refs** : les numéros de ligne ci-dessus correspondent au commit `1c4d7cf` (ARPEG_GEN Phase 4 mergé). Avant d'appliquer la Phase 5, vérifier que le code n'a pas bougé par `grep -n "void ArpEngine::setCaptured" src/arp/ArpEngine.cpp` et `grep -n "anyFingerDown" src/arp/ArpEngine.cpp`.

### Fichiers touchés

- `src/arp/ArpEngine.h` : nouvelle signature `setCaptured(bool, MidiTransport&)`
- `src/arp/ArpEngine.cpp:507-557` : suppression branche destructive
- `src/managers/GestureDispatcher.cpp` : appels `setCaptured` simplifiés (déjà en Phase 2/4 avec la signature future, ou ajustement ici)
- `src/main.cpp:528-561` (`processArpMode`) : **NE PAS toucher** au bloc `if (slot.arpEngine->isPaused() && slot.arpEngine->hasNotes()) { clearAllNotes(...) ; } addPadPosition(pos);`. Ce chemin est validé.

### Snippet — `ArpEngine::setCaptured` après refonte

```cpp
// ArpEngine.h — nouvelle signature
void setCaptured(bool captured, MidiTransport& transport);

// ArpEngine.cpp
void ArpEngine::setCaptured(bool captured, MidiTransport& transport) {
  if (captured == _captured) return;
  _captured = captured;

  if (captured) {
    // Stop → Play : relaunch si paused pile non vide
    if (_pausedPile && _positionCount > 0) {
      _playing = true;
      _stepIndex = -1;
      _shuffleStepCounter = 0;
      _waitingForQuantize = (_quantizeMode != ARP_START_IMMEDIATE);
      if (_waitingForQuantize) _lastDispatchedGlobalTick = 0xFFFFFFFF;
      #if DEBUG_SERIAL
      Serial.printf("[ARP] Bank %d: Play — relaunch paused pile (%d notes)\n",
                    _channel + 1, _positionCount);
      #endif
    } else {
      #if DEBUG_SERIAL
      Serial.printf("[ARP] Bank %d: Play (pile %d notes)\n",
                    _channel + 1, _positionCount);
      #endif
    }
    _pausedPile = false;
  } else {
    // Play → Stop : pile sacrée (§13). Plus de branche anyFingerDown.
    flushPendingNoteOffs(transport);
    _playing = false;
    _waitingForQuantize = false;
    _pausedPile = true;
    #if DEBUG_SERIAL
    Serial.printf("[ARP] Bank %d: Stop — pile kept (%d notes)\n",
                  _channel + 1, _positionCount);
    #endif
  }
}
```

### Vérifications complémentaires

- [ArpEngine.h:71-72](../../../src/arp/ArpEngine.h:71) — supprimer le commentaire qui mentionne `keyIsPressed`.
- Audit des call sites : tous les `setCaptured(...)` doivent passer à 2 args. Faire un grep sur le code :

```bash
grep -rn "setCaptured" src/
```

Attendu : appels uniquement depuis `GestureDispatcher.cpp` (handleBankPad, handleHoldPad).

### Gates Phase 5 — séquence stricte (commit groupé ici)

#### 1. Compile gate

`pio run` exit 0. Grep `anyFingerDown` dans `src/arp/` → 0 hit. Vérifier que `setCaptured(bool, MidiTransport&)` est la seule signature appelée (grep `setCaptured` partout dans `src/`).

#### 2. HW gate — OBLIGATOIRE (résolution F1 + pile sacrée)

- ☐ **P-1** [F1] Double-tap stop FG ARPEG avec pile pleine → pile **préservée**, Stop OK, LED `EVT_STOP`.
- ☐ **P-2** Hold pad stop avec doigts pressés sur autres pads → pile **préservée** (changement vs avant Phase 5).
- ☐ **P-3** Play → Stop (pile préservée) → Play → musique reprend la même pile, même séquence.
- ☐ **P-4** [§13.2 conservé] Play → Stop → press pad musical sans Play préalable → **wipe paused pile + add nouvelle note**. La pile dormante disparaît, le pad pressé devient la 1ère note live.
- ☐ **P-5** Play → Stop → repress hold pad → Play : musique reprend la pile préservée. Le wipe musical §13.2 n'a **pas** eu lieu (aucun pad musical pressé entre les deux).
- ☐ **P-6** Double-tap stop BG ARPEG (différent FG) → pile BG préservée, BG en paused, FG inchangé.
- ☐ **P-7** Régression complète : B-1 à B-6, S-1 à S-6, H-1 à H-7. Aucun bench cassé.
- ☐ **P-8** Macro-geste réel (H-5 répété) : tout passe.
- ☐ **P-9** Stress test : 5 min de jeu live, multi-bank, multi-action. Aucune note bloquée, aucun glitch, aucun pile gone unexpectedly.

**Présentation au user** : lister P-1 à P-9, attendre OK explicite "HW Phase 5 OK + autorisation commit groupé".

#### 3. Commit gate — **COMMIT GROUPÉ PHASES 2 → 5**

Première gravure de la refonte. Le commit groupe les Phases 2, 3, 4, 5 (Phases 0, 1 ont déjà été commitées séparément).

Présenter au user la liste exhaustive des fichiers modifiés (jamais `git add -A`) :

```bash
git status
```

Construire le message via HEREDOC :

```
feat(gesture): refonte GestureDispatcher — pile sacrée, double-tap déterministe

Refonte du geste LEFT-held + hold pad ARPEG dans une fonction dispatcher
unique. Élimination structurelle des fragilités F1-F7 de l'audit
2026-04-26. Pile ARPEG sacrée : aucun geste de transport ne wipe la pile.
Sweep release universel (NORMAL, ARPEG-OFF, ARPEG-ON, futur LOOP). Garde
LEFT + garde post-double-tap pour absorber les artefacts d'intention.

Spec  : docs/superpowers/specs/2026-04-26-gesture-dispatcher-design.md
Plan  : docs/superpowers/plans/2026-04-26-gesture-dispatcher-plan.md

Fichiers :
- new   : src/managers/GestureDispatcher.{h,cpp}
- mod   : src/main.cpp (snapshot buffer, intégration dispatcher,
          suppression handleHoldPad / handleLeftReleaseCleanup /
          s_lastPressTime)
- mod   : src/managers/BankManager.{h,cpp} (façade isHolding supprimée,
          update() supprimé, _lastKeys/_pendingSwitch*/etc. supprimés)
- mod   : src/managers/ScaleManager.{h,cpp} (suppression complète
          de processScalePads + _holdPad orphelin)
- mod   : src/arp/ArpEngine.{h,cpp} (setCaptured nouvelle signature,
          branche anyFingerDown → clearAllNotes supprimée)
- mod   : src/core/KeyboardData.h (isLoopType helper ajouté)

HW validé : scénarios B-1 à B-6, S-1 à S-6, H-1 à H-7, P-1 à P-9.
Stress live 5 min OK.
```

Attendre OK explicite "commit OK" avant `git add <fichiers nommés>` + `git commit -m "$(cat <<'EOF'...)"`.

Pas de `git add .` ni `git add -A` (cf CLAUDE.md projet).

---

## Phase 6 — Bench live + ajustement constantes

### Pourquoi

Les constantes `LEFT_EDGE_GUARD_MS` et `DOUBLE_TAP_DEAD_MS` sont des paris (40ms et 200ms). À tester en jeu réel.

### Procédure

1. Flash le firmware Phase 5.
2. Jouer le scénario complexe du brief :
   - LEFT held + multi-actions enchaînées sans relâcher
   - Stop BG + Start BG + Switch + Scale + Switch + Release
3. Logger via DEBUG_SERIAL si besoin (ajouter Serial.printf à `handleBankPad` et `commitPendingSwitch`).
4. Identifier si :
   - `LEFT_EDGE_GUARD_MS=40` : trop court → des taps proches du press/release glissent malgré tout dans la garde
   - Trop long → des taps légitimes intentionnels juste après le press LEFT sont perdus
5. Idem pour `DOUBLE_TAP_DEAD_MS=200`.

### Ajustements probables

Tuner par incréments de 20-30ms. Range probable :
- `LEFT_EDGE_GUARD_MS` ∈ [25, 80] ms
- `DOUBLE_TAP_DEAD_MS` ∈ [150, 300] ms

### Gates Phase 6

#### 1. Compile gate

`pio run` exit 0 (uniquement si constantes modifiées dans le source).

#### 2. HW gate — **EST la phase**

Bench live itératif. Rejouer P-1 à P-9 + H-5 (macro-geste) + scénarios "edge cases timing" (T-1 à T-3 dans l'annexe Bench) avec les nouvelles valeurs de constantes.

#### 3. Commit gate — autorisé (commit tuning séparé)

Si les constantes ont changé par rapport au commit Phase 5 :

```
tune(gesture): bench live — adjust LEFT_EDGE_GUARD_MS / DOUBLE_TAP_DEAD_MS

Phase 6 du plan GestureDispatcher. Valeurs validées sur instrument :
- LEFT_EDGE_GUARD_MS : 40 → <new value> ms
- DOUBLE_TAP_DEAD_MS : 200 → <new value> ms

Bench scénarios T-1, T-2, T-3 passés. Aucune régression sur B/S/H/P.
```

Attendre OK explicite avant commit. Si pas de changement de constantes, pas de commit.

---

## Phase 7 — Hooks LOOP préparatoires

### Pourquoi

Le dispatcher est neutre vis-à-vis de LOOP en l'état. Pour que LOOP Phase 1 puisse wirer ses handlers sans modifier le dispatcher, ajouter dès maintenant les **hooks vides** définis en spec §15.

### Fichiers touchés

- `src/managers/GestureDispatcher.h` : déclarations de hooks
- `src/managers/GestureDispatcher.cpp` : implémentations vides ou stubs

### Signatures gelées des hooks LOOP

Toutes les signatures ci-dessous sont **figées par cette phase**. Le LoopEngine (à implémenter en LOOP Phase 2) doit les respecter à l'identique. Le dispatcher publie l'interface, le LoopEngine s'y conforme.

```cpp
// GestureDispatcher.h — ajouter dans la classe

public:
  // --- LOOP Phase 2+ hooks ---
  // Le LoopEngine s'enregistre au boot : main.cpp setup() appelle
  // s_gestureDispatcher.setLoopEngine(&s_loopEngine) après instanciation.
  // Si setLoopEngine n'a pas été appelé (Phases gesture 0-7 sans LOOP P2 mergé),
  // les branches LOOP du dispatch sont no-op silencieuses.
  void setLoopEngine(class LoopEngine* engine);

private:
  class LoopEngine* _loopEngine;   // nullable jusqu'à LOOP P2
```

```cpp
// LoopEngine.h — contrat que LoopEngine doit honorer (LOOP P2)
class LoopEngine {
public:
  // Hook pré-commit bank switch (LOOP §17 : commit immédiat WAITING_LOAD
  // avant un bank switch).
  // Appelé par le dispatcher dans commitPendingSwitch() ET dans
  // onLeftRelease() fast-forward, avant BankManager::switchToBank().
  // sourceBank = bank en train de quitter le FG.
  // Retour : true si un load pending a été commité (info, pas de gate).
  bool preBankSwitch(uint8_t sourceBank);

  // Toggle PLAY/STOP — appelé par TAP_DOUBLE BANK_PAD sur bank LOOP.
  // targetBank peut être FG ou BG (le dispatcher passe l'index).
  // Pile/buffer sacré : aucune wipe automatique.
  void toggle(uint8_t targetBank);

  // Layer musical (LEFT non-held), FG=LOOP, pad PLAY/STOP.
  // tapPlayStop  : tap simple, suit loopQuantize.
  // bypassQuantize : double-tap, action immédiate + flush notes.
  void tapPlayStop();
  void bypassQuantize();

  // Layer musical (LEFT non-held), FG=LOOP, pad REC.
  void tapRec();

  // Layer musical (LEFT non-held), FG=LOOP, pad CLEAR.
  // armClear   : rising edge, démarre la rampe LED, prêt à commit OU à entrer en combo.
  // commitClear: LONG_PRESS atteint (durée _clearLoopTimerMs), wipe buffer.
  // cancelClear: release du pad avant commit OU combo détecté → annule.
  void armClear();
  void commitClear();
  void cancelClear();

  // LEFT held, FG=LOOP, slot pad.
  // saveSlot   : LONG_PRESS atteint (durée _slotSaveTimerMs), write LittleFS.
  // loadSlot   : SHORT_PRESS 300-1000ms, read LittleFS + replace buffer.
  // deleteSlot : combo CLEAR+slot (lead=CLEAR, follow=slot), rising edge slot.
  void saveSlot(uint8_t slotPadIdx);
  void loadSlot(uint8_t slotPadIdx);
  void deleteSlot(uint8_t slotPadIdx);

  // Indicateur d'état machine pour le dispatcher (utilisé par armClear pour
  // savoir si on est en WAITING_LOAD, RECORDING, etc.). Read-only.
  enum class State : uint8_t { EMPTY, RECORDING, OVERDUBBING, PLAYING, STOPPED,
                                WAITING_PLAY, WAITING_STOP, WAITING_LOAD };
  State getState(uint8_t bank) const;
};
```

### Intégration dispatcher → LoopEngine

Dans `commitPendingSwitch()` ([plan Phase 2 snippet](#snippet--gesturedispatchercommitpendingswitch)) :

```cpp
void GestureDispatcher::commitPendingSwitch(uint32_t now) {
  uint8_t target = (uint8_t)_pendingSwitchBank;
  _pendingSwitchBank = -1;
  uint8_t cur = _bank->getCurrentBank();
  if (target == cur) return;
  // Hook LOOP : laisser au LoopEngine commit un WAITING_LOAD pending.
  if (_loopEngine && isLoopType(_banks[cur].type)) {
    _loopEngine->preBankSwitch(cur);
  }
  _bank->switchToBank(target);
}
```

Idem dans `onLeftRelease()` fast-forward (mêmes 3 lignes avant `_bank->switchToBank(target)`).

Dans `handleBankPad()` ([plan Phase 2 snippet](#snippet--gesturedispatcherhandlebankpad)), brancher TAP_DOUBLE LOOP :

```cpp
  if (isArpType(target.type) && target.arpEngine) {
    // ... existing ARPEG path
  } else if (isLoopType(target.type) && _loopEngine) {
    _loopEngine->toggle(b);
    if (_leds) {
      // EVT_PLAY / EVT_STOP fired par LoopEngine elle-même (LED grammar)
    }
  }
```

### Phase 7 reste un stub si LOOP P2 pas mergé

Les hooks sont publiés mais `_loopEngine` reste `nullptr` jusqu'à LOOP P2. Toutes les branches `if (_loopEngine && ...)` sont no-op. Aucune régression possible.

### Décisions LOOP-side

LOOP Phase 2 doit :
- Implémenter `LoopEngine` avec l'interface ci-dessus.
- Appeler `s_gestureDispatcher.setLoopEngine(&s_loopEngine)` dans `main.cpp setup()`.
- Ajouter le dispatch `LOOP_SLOT_PAD` et `LOOP_CTRL_PAD` dans le dispatcher (Phase 7.2 si on prolonge le plan gesture, ou en LOOP P2 directement avec patch).

### Gates Phase 7

#### Pré-requis

**Décision D9 doit être tranchée** (cf §15 spec décisions ouvertes). Sans ça, signature `LoopEngine::cancelClear/commitClear` ambiguë.

#### 1. Compile gate

`pio run` exit 0. Hooks publiés mais `_loopEngine = nullptr`, branches `if (_loopEngine && ...)` toutes inactives.

#### 2. HW gate — **N/A** (stubs, aucun consommateur)

Flash rapide pour confirmer non-régression. Aucun scénario LOOP testable (LoopEngine pas mergé).

#### 3. Commit gate — autorisé (commit Phase 7 séparé)

```
feat(gesture): LOOP hooks — preBankSwitch + LoopEngine interface

Phase 7 du plan GestureDispatcher. Hooks préparatoires pour LOOP Phase 2 :
interface LoopEngine figée, setLoopEngine() exposé. Implémentations
stub (no-op tant que LoopEngine pas instancié).

Décision D9 tranchée : <option A "fire au release" / option B "annuler">.

Spec : docs/superpowers/specs/2026-04-26-gesture-dispatcher-design.md
Plan : docs/superpowers/plans/2026-04-26-gesture-dispatcher-plan.md
```

Attendre OK explicite avant commit.

---

## Annexe — Récapitulatif fichiers touchés

| Fichier | Phase 0 | Phase 1 | Phase 2 | Phase 3 | Phase 4 | Phase 5 | Phase 7 |
|---|---|---|---|---|---|---|---|
| `src/main.cpp` | + (snapshot buffer) | + (instance dispatcher) | + (remplace BankManager.update) | + (consumeScaleChange) | - (handleHoldPad, handleLeftReleaseCleanup, s_lastPressTime) | (vérif) | - |
| `src/managers/GestureDispatcher.h` | - | **CRÉÉ** | + impl bank pad | + impl scale/octave | + impl hold pad | + sig setCaptured | + hooks LOOP |
| `src/managers/GestureDispatcher.cpp` | - | **CRÉÉ** | + impl bank pad | + impl scale/octave | + impl hold pad / sweep | + sig setCaptured | + hooks LOOP |
| `src/managers/BankManager.h` | - | - | -- (suppr membres) | - | - | - | - |
| `src/managers/BankManager.cpp` | - | - | -- (suppr update) | - | - | - | - |
| `src/managers/ScaleManager.h` | - | - | - | -- (façade ou suppr) | - | - | - |
| `src/managers/ScaleManager.cpp` | - | - | - | -- (suppr update) | - | - | - |
| `src/arp/ArpEngine.h` | - | - | - | - | - | -- (signature setCaptured) | - |
| `src/arp/ArpEngine.cpp` | - | - | - | - | - | -- (suppr anyFingerDown) | - |

---

## Annexe — Scénarios bench (script de validation)

Liste consolidée référencée par les HW gates des Phases 2 à 6. Chaque scénario doit pouvoir être exécuté en ≤30 s, et le bench complet par phase doit tenir en ≤10 min.

### Préparation bench (à faire une fois)

- ☐ Préparer une config Tool 3 cohérente : `bankPads = 0-7`, `holdPad = 23`, `rootPads = 8-14`, `modePads = 15-21`, `chromaticPad = 22`, `octavePads = 25-28`. Pas de collision hold/bank/scale.
- ☐ Configurer 8 banks : 4 NORMAL, 2 ARPEG, 2 ARPEG_GEN. Aucune LOOP (LoopEngine pas implémenté pendant la refonte gesture).
- ☐ Activer `DEBUG_SERIAL=1` (compile-time) pour voir les `[BANK]` / `[ARP]` / `[SCALE]` traces.
- ☐ Préparer un DAW BLE MIDI ou MIDI USB pour visualiser les notes émises.

### B-* — Bank pads sous LEFT (Phase 2)

- **B-1 — Bank switch BG normal.** LEFT + tap BG_pad ≠ FG. Attendre 200ms. → switch commit, LED `EVT_BANK_SWITCH`. Aucune note parasite.
- **B-2 — Double-tap play/stop BG ARPEG.** LEFT + 2 taps rapides BG_pad ARPEG (gap < 200ms). → toggle play/stop sur cette BG. **Pas de switch.** Pile préservée si Phase 5+ acquise.
- **B-3 — Double-tap play/stop FG.** LEFT + 2 taps rapides FG_pad. → play/stop FG. **Pas de switch.** Pile préservée Phase 5+.
- **B-4 — Boundary 200ms (F2).** LEFT + 2 taps avec gap ~200ms (limite). → ne switch plus de banque. Au pire silence, jamais bank switch involontaire.
- **B-5 — Faux double-tap inter-LEFT (F5).** LEFT + tap BG_pad → release LEFT → re-press LEFT + tap BG_pad rapide. → 2 actions distinctes, pas de play/stop fantôme.
- **B-6 — Brossage adjacent (F6).** LEFT + tap BG_pad + brossage involontaire BG_pad+1 dans la même frame. → seul le 1er rising edge wins, pendingSwitch pas redirigé.

### S-* — Scale pads sous LEFT (Phase 3)

- **S-1 — Root change.** LEFT + tap rootPad C (par exemple). → root = C immédiat sur FG, LED `EVT_SCALE_ROOT`. Notes joués après reflètent C.
- **S-2 — Mode change.** LEFT + tap modePad Dorian. → mode = Dorian immédiat, LED `EVT_SCALE_MODE`.
- **S-3 — Chromatic toggle.** LEFT + tap chromaticPad. → `chromatic=true`, LED `EVT_SCALE_CHROM`.
- **S-4 — Octave / mutation.** LEFT + tap octavePad sur FG ARPEG. → `setOctaveRange(o+1)` (classic) ou `setMutationLevel` (GEN), LED `EVT_OCTAVE`.
- **S-5 — Propagation scaleGroup.** Configurer 2 banks dans le même scaleGroup. Scale change sur l'une → propagation à l'autre (queueScaleWrite + arpEngine.setScaleConfig).
- **S-6 — Non-régression bank pads.** Rejouer B-1, B-2, B-3 — aucun bug introduit.

### H-* — Hold pad + sweep release (Phase 4)

- **H-1 — Hold pad sous LEFT.** LEFT held + tap hold pad sur FG ARPEG. → toggle play/stop FG.
- **H-2 — Hold pad hors LEFT.** Tap hold pad direct sans LEFT. → idem toggle.
- **H-3 — Sweep release universel (§9).** Maintenir LEFT + presser un pad musical (e.g. pad 30) + relâcher LEFT (pad encore pressé). Test sur 4 configs FG : NORMAL, ARPEG-OFF, ARPEG-ON, ARPEG_GEN-ON. → aucun noteOn / aucune addPadPosition au release dans **aucun** des 4 cas. Le pad doit être ignoré tant qu'il n'a pas un nouveau rising edge hors LEFT.
- **H-4 — Garde LEFT-press (§7).** Doigt déjà sur pad 30 + press LEFT. Pendant les premiers ~40ms, le pad 30 n'arme aucune action bank/scale/hold même s'il est configuré comme rôle hold-left. Vérifiable surtout sur FG ARPEG : pas de toggle hold pad parasite au press LEFT.
- **H-5 — Macro-geste réel (workflow du brief).** Séquence : `LEFT down + double-tap BG6 (stop) + double-tap BG5 (play) + tap BG7 + scale root change + tap FG8 + LEFT release`. Chaque action doit produire exactement son effet attendu. Aucun switch fantôme, aucune note parasite au release.
- **H-6 — Triple-tap garde double-tap (Q7).** LEFT + 3 taps rapides BG_pad (gaps 50/50ms). → 1er + 2e = double-tap (play/stop), 3e est ignoré par garde §7. Pas de nouveau pendingSwitch armé sur 3e.
- **H-7 — Non-régression.** Rejouer B-1 à B-6 + S-1 à S-6.

### P-* — Pile sacrée (Phase 5)

- **P-1 — F1 résolu.** FG ARPEG capturée, pile pleine (3-4 notes), double-tap bank FG → Stop. Vérifier que `_positionCount` reste identique (via debug serial `[ARP] Stop — pile kept (N notes)`). Re-play → mêmes notes arpègent.
- **P-2 — Hold pad stop avec fingers down.** FG ARPEG capturée, pile pleine. Presser 2 pads musicaux + tap hold pad → Stop. Pile **préservée**. Différent du comportement avant Phase 5.
- **P-3 — Play → Stop → Play resume.** Cycle complet. Pile identique à la sortie. Visuel : LED play/stop alterne, mêmes notes au Play.
- **P-4 — Chemin §13.2 conservé.** FG ARPEG Stop avec paused pile non-vide. Presser un pad musical (pas hold, pas bank) sans LEFT. → `clearAllNotes` + `addPadPosition(pos)`. La paused pile dispar, le pad pressé devient la 1ère note d'un live. Vérifier que ce comportement n'a pas été supprimé par erreur.
- **P-5 — Pile sacrée transverse hold pad.** Play → Stop via hold pad → presser hold pad encore → Play. Mêmes notes. Aucun wipe.
- **P-6 — BG stop.** Double-tap BG ARPEG (différente FG). Pile BG préservée, BG en paused. FG inchangé. Switch vers BG plus tard → Play resume pile BG.
- **P-7 — Régression complète.** B-1 à B-6, S-1 à S-6, H-1 à H-7 tous passés. Aucun bench cassé.
- **P-8 — Macro-geste répété.** H-5 rejoué. Aucune note parasite, aucune pile perdue.
- **P-9 — Stress test live 5 min.** Jeu libre avec multi-bank, scale changes, play/stop multiples. Aucune note bloquée. Aucune pile gone unexpectedly. Aucun glitch.
- **P-10 — Pile ARPEG-OFF préservée au LEFT release (régression du fix 2026-05-15).** Bank FG ARPEG ou ARPEG_GEN en Stop avec pile non vide. Cycle simple : `LEFT press` → ne rien faire pendant 1-2s → `LEFT release`. Vérifier au log DEBUG_SERIAL qu'AUCUN `[ARP] -note` n'apparaît dans la frame du release. La pile reste à N notes. Refaire avec différentes tailles de pile (1, 3, 7 notes). C'est le scénario qui a révélé le bug racine — doit rester vert après refonte.
- **P-11 — Auto-Play sur 1er press musical en Stop (§13.2 Option 3, fix 2026-05-15).** Bank FG ARPEG ou ARPEG_GEN en Play avec pile pleine N notes (N=3 par ex.). Toggle Stop (hold pad). Vérifier `[ARP] Stop — pile kept (3 notes)`. Sans rien d'autre, presser **un pad musical** non encore dans la pile. **Attendu** : log montre wipe (`-note` × 3 + `Play (pile 0 notes)`) puis `+note (1 total)`. L'arpège démarre avec la seule nouvelle note. Pas besoin de toggle Play manuel. Le moteur est de nouveau actif (`isCaptured()==true`). Si à la place le user toggle Play sans presser de pad musical, c'est la branche §13.1 : relaunch paused pile entière, pas de wipe. Tester les deux chemins.
- **P-12 — Toggle global LEFT + hold pad (§10.1, fix 2026-05-15).** Configurer 3 banks ARPEG/ARPEG_GEN avec piles non vides, toutes en Play (capturées). Sans switch, geste : `LEFT press` → tap hold pad → `LEFT release`. **Attendu** : log montre `[ARP] Bank N: Stop — pile kept (X notes)` pour chaque bank en Play, LED `EVT_STOP` avec mask multi-bank (visuel : flash rouge sur les pads de toutes les banks concernées). Aucune ligne `-note`. Refaire le geste : `LEFT press` → tap hold pad → release. **Attendu** : `[ARP] Bank N: Play — relaunch paused pile (X notes)` sur chaque bank, LED `EVT_PLAY` multi-bank. Régression à valider : sans LEFT, tap hold pad → toggle FG seule (comportement de référence inchangé). Edge case : aucune bank avec paused pile non vide après stop → tap LEFT+hold = no-op silencieux (pas de relaunch sur pile vide).

### T-* — Tuning constants (Phase 6)

- **T-1 — `LEFT_EDGE_GUARD_MS` court (40 → 25ms).** Bench H-3 et H-4 : un tap juste après press LEFT est-il pris ou non ? Trouver la borne minimale acceptable.
- **T-2 — `LEFT_EDGE_GUARD_MS` long (40 → 80ms).** Idem. Un tap légitime juste après press LEFT est-il filtré à tort ?
- **T-3 — `DOUBLE_TAP_DEAD_MS` court/long (200 → 150 / 250ms).** Bench H-6 : la 3e frappe d'une triple-frappe involontaire est-elle bien absorbée ? Trouver le sweet spot.

### Macro-bench rapide (entre phases)

Si tu n'as que 2 min, exécuter la "smoke suite" :

- B-1 (bank switch OK)
- B-2 (double-tap play/stop BG)
- H-5 (macro-geste réel)
- P-1 (pile sacrée FG après Phase 5)

Couverture minimale qui détecte 80% des régressions.

---

**Fin du plan.** Spec source : [`2026-04-26-gesture-dispatcher-design.md`](../specs/2026-04-26-gesture-dispatcher-design.md).
