# ILLPAD48 V2 — Guide de Restructuration du Code

> **Audience** : Codeur (Cursor / AI coding assistant).
> **Pré-requis** : Lire `ILLPAD48_V1_ANALYSIS.md` et `ILLPAD48_V2_NEW_IDEAS.md`.
> **Approche** : Fresh start. Pas de backward compatibility.
> Dernière mise à jour : 20 mars 2026.

---

## 1. Résumé des Changements V1 → V2

| Aspect | V1 | V2 |
|---|---|---|
| Boutons | 2 | 2 (gauche=bank+scale+arp, arrière=battery+modifier pot arrière) |
| Pots | 1 | 5 (4 droite=musical, 1 arrière=config) |
| Banks | 1 active | 8 vivantes, ARPEG HOLD en background |
| Types bank | Tous identiques | NORMAL / ARPEG (max 4) |
| Velocity | Fixe 127 | Per-bank base velocity + variation (NORMAL + ARPEG) |
| Pitch bend | Non implémenté | Per-bank offset (NORMAL) |
| Scale | Non implémenté | Runtime root+mode, root=base note |
| Mapping | noteMap = notes MIDI | 3 couches (ordering, scale, control pads) |
| Arpégiateur | N'existe pas | ArpEngine live + persistant |
| Pile arp | N/A | Stocke padOrder index, résolution au tick |
| MIDI Clock | Envoi seul | Réception + PLL + fallback |
| Mutex | Mutex classique | Double buffer lock-free |
| NVS | Writes dans le loop | Task NVS dédiée |
| Setup | 4 tools | 5 tools |

### Inchangé

CapacitiveKeyboard (pressure pipeline), dual-core FreeRTOS, aftertouch sur NORMAL.

---

## 2. Architecture

### Arborescence

```
src/
├── main.cpp                        # ~150 lignes
│
├── core/
│   ├── HardwareConfig.h                # Pins (2 btn, 5 pots), constantes
│   ├── CapacitiveKeyboard.cpp/.h       # INCHANGÉ
│   ├── MidiTransport.cpp/.h            # ÉTENDU (réception clock, BLE interval)
│   ├── LedController.cpp/.h            # ÉTENDU
│   └── KeyboardData.h                  # ÉTENDU (BankSlot, ScaleConfig, etc.)
│
├── managers/
│   ├── BankManager.cpp/.h              # Bouton gauche — bank switch
│   ├── ScaleManager.cpp/.h             # Bouton gauche — scale + arp controls (même bouton que bank)
│   ├── PotRouter.cpp/.h               # 5 pots, bindings contextuels, catch
│   ├── BatteryMonitor.cpp/.h           # Bouton arrière
│   └── NvsManager.cpp/.h              # Task NVS dédiée (non-bloquant)
│
├── midi/
│   ├── MidiEngine.cpp/.h
│   ├── ScaleResolver.cpp/.h
│   └── ClockManager.cpp/.h             # Réception + PLL + fallback
│
├── arp/
│   ├── ArpEngine.cpp/.h                # Moteur autonome (live + persistant)
│   └── ArpScheduler.cpp/.h             # Tick dispatcher (max 4)
│
└── setup/
    ├── SetupManager.cpp/.h
    ├── ToolCalibration.cpp/.h
    ├── ToolPadOrdering.cpp/.h
    ├── ToolPadRoles.cpp/.h
    ├── ToolBankConfig.cpp/.h
    ├── ToolSettings.cpp/.h
    └── SetupUI.cpp/.h
```

### BankSlot

```cpp
enum BankType : uint8_t { BANK_NORMAL = 0, BANK_ARPEG = 1 };

struct ScaleConfig {
  bool chromatic;
  uint8_t root;       // 0-6 (A-G)
  uint8_t mode;       // 0-6 (Ionian-Locrian)
};

struct BankSlot {
  uint8_t channel;              // 0-7 fixe
  BankType type;
  ScaleConfig scale;
  ArpEngine* arpEngine;         // non-null si ARPEG
  bool isForeground;
  uint8_t lastResolvedNote[NUM_KEYS];
  // V2 nouveaux champs
  uint8_t baseVelocity;        // 1-127, per-bank (NORMAL + ARPEG)
  uint8_t velocityVariation;   // 0-100%, per-bank (NORMAL + ARPEG)
  uint16_t pitchBendOffset;    // 0-16383, centre=8192 (NORMAL uniquement)
};
```

---

## 3. Optimisations Architecture

### 3.1 Double Buffer (remplace le Mutex)

Le mutex V1 couple Core 0 et Core 1 — si l'un tient le lock, l'autre attend. Le double buffer élimine ce couplage.

Un seul `std::atomic<uint8_t>` sert de point de synchronisation. Core 0 écrit dans le buffer inactif, puis publie l'index atomiquement. Core 1 lit l'index pour savoir quel buffer contient les données les plus récentes.

```cpp
#include <atomic>

struct SharedKeyboardState {
  uint8_t keyIsPressed[NUM_KEYS];
  uint8_t pressure[NUM_KEYS];
};

static SharedKeyboardState s_buffers[2];
static std::atomic<uint8_t> s_active{0};  // Index du buffer que Core 1 doit LIRE

// Core 0 : écrire dans le buffer que Core 1 ne lit PAS
void sensingTask(void*) {
  for (;;) {
    keyboard.update();
    uint8_t writeIdx = 1 - s_active.load(std::memory_order_acquire);
    SharedKeyboardState& buf = s_buffers[writeIdx];
    for (int i = 0; i < NUM_KEYS; i++) {
      buf.keyIsPressed[i] = keyboard.isPressed(i) ? 1 : 0;
      buf.pressure[i] = keyboard.getPressure(i);
    }
    // Publication atomique — Core 1 verra les nouvelles données
    s_active.store(writeIdx, std::memory_order_release);

    vTaskDelay(1);
  }
}

// Core 1 : lire sans attente
void loop() {
  const SharedKeyboardState& state = s_buffers[s_active.load(std::memory_order_acquire)];
  // ... utiliser state — jamais bloqué
}
```

**Paramètres lents (pots → Core 0)** : responseShape et slewRate passent par des `std::atomic` — pas besoin de buffer, ce sont des valeurs scalaires mises à jour rarement.

```cpp
static std::atomic<float> s_responseShape{0.5f};
static std::atomic<uint16_t> s_slewRate{150};

// Core 1 (PotRouter) écrit :
s_responseShape.store(potRouter.getResponseShape(), std::memory_order_relaxed);

// Core 0 (sensingTask) lit :
keyboard.setResponseShape(s_responseShape.load(std::memory_order_relaxed));
```

### 3.2 NVS Task Dédiée

Les NVS writes (5-10ms) ne bloquent plus le loop principal.

```cpp
static TaskHandle_t s_nvsTaskHandle = nullptr;

class NvsManager {
public:
  void begin() {
    xTaskCreatePinnedToCore(nvsTask, "nvs", 4096, this, 1, &s_nvsTaskHandle, 1);
  }

  // Le loop principal enqueue un write (non-bloquant)
  void queueBankWrite(uint8_t bank) {
    _pendingBank = bank;
    _bankDirty = true;
    xTaskNotifyGive(s_nvsTaskHandle);
  }

  // ... idem pour pot, settings, scale, etc.

private:
  static void nvsTask(void* arg) {
    NvsManager* self = (NvsManager*)arg;
    for (;;) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // attend un signal
      // Conditions de sécurité (pas de pads pressés) vérifiées ici
      self->commitAll();
    }
  }

  void commitAll() {
    if (_bankDirty)    { writeBankToNvs();    _bankDirty = false; }
    if (_potDirty)     { writePotToNvs();     _potDirty = false; }
    if (_scaleDirty)   { writeScaleToNvs();   _scaleDirty = false; }
    // ... etc.
  }
};
```

### 3.3 BLE Connection Interval

Configurable dans les Settings. Une ligne au setup :

```cpp
// Dans MidiTransport::begin(), après BLE init :
switch (bleIntervalSetting) {
  case BLE_LOW_LATENCY:   BLEMidiServer.setConnectionParams(6, 6, 0, 400); break;   // 7.5ms
  case BLE_NORMAL:        BLEMidiServer.setConnectionParams(12, 12, 0, 400); break;  // 15ms
  case BLE_BATTERY_SAVER: BLEMidiServer.setConnectionParams(24, 24, 0, 400); break;  // 30ms
}
```

> Note : le central (Mac, iPhone, etc.) peut ignorer la demande. Apple impose 15ms minimum.

### 3.4 Séparation Chemin Critique dans le Loop

```cpp
void loop() {
  // === CHEMIN CRITIQUE (latence minimale) ===
  const SharedKeyboardState& state = s_buffers[s_readIndex];  // instantané
  bool leftHeld = (digitalRead(BTN_LEFT_PIN) == LOW);
  bool rearHeld = (digitalRead(BTN_REAR_PIN) == LOW);

  bankManager.update(state.keyIsPressed, leftHeld);
  scaleManager.update(state.keyIsPressed, leftHeld, currentSlot);

  if (!bankManager.isHolding() && !scaleManager.isHolding()) {
    if (currentSlot.type == BANK_NORMAL)
      processNormalMode(currentSlot, state);
    else
      processArpMode(currentSlot, state);
  }

  arpScheduler.tick(clockManager);  // background arps
  midiEngine.flush();

  // === SECONDAIRE (peut être retardé de 1-2ms sans impact) ===
  potRouter.update(leftHeld, rearHeld, currentSlot.type);
  batteryMonitor.update(rearHeld);
  ledController.update();
  nvsManager.notifyIfDirty();  // signal à la task NVS, non-bloquant

  vTaskDelay(1);
}
```

**Note** : `bankManager` et `scaleManager` reçoivent le **même bouton** (`leftHeld`). Pas de conflit car les pads de contrôle sont distincts (bank pads ≠ scale pads, vérifié par Tool 3).

---

## 4. PotRouter

### Concept

Un seul module lit les 5 ADC (4 droite + 1 arrière), résout les combos boutons, applique le catch, et expose des getters. Les consumers ne savent pas quel pot physique fournit leur paramètre.

### Boutons Modifier

| Bouton | Modifie | Masque |
|--------|---------|--------|
| Gauche | 4 pots droits | `bit 0` |
| Arrière | Pot arrière uniquement | `bit 1` |

Le bouton gauche n'affecte **jamais** le pot arrière. Le bouton arrière n'affecte **jamais** les pots droits.

```cpp
enum PotTarget : uint8_t {
  // NORMAL
  TARGET_RESPONSE_SHAPE,
  TARGET_SLEW_RATE,
  TARGET_AT_DEADZONE,
  TARGET_PITCH_BEND,          // NOUVEAU — per-bank offset
  // ARPEG
  TARGET_GATE_LENGTH,
  TARGET_SWING,
  TARGET_DIVISION,
  TARGET_PATTERN,              // NOUVEAU — 5 positions discrètes
  TARGET_OCTAVE_RANGE,         // NOUVEAU — 4 positions discrètes
  // NORMAL + ARPEG (per-bank)
  TARGET_BASE_VELOCITY,
  TARGET_VELOCITY_VARIATION,
  // Global
  TARGET_TEMPO_BPM,
  TARGET_LED_BRIGHTNESS,       // NOUVEAU — global
  TARGET_PAD_SENSITIVITY,
  // Vide
  TARGET_NONE
};

struct PotBinding {
  uint8_t potIndex;       // 0-3=droite, 4=arrière
  uint8_t buttonMask;     // 0=aucun, bit0=gauche, bit1=arrière
  BankType bankType;      // NORMAL, ARPEG, ou ANY
  PotTarget target;
  uint16_t rangeMin;
  uint16_t rangeMax;
};

class PotRouter {
public:
  void begin();
  void update(bool btnLeft, bool btnRear, BankType currentType);

  // Getters — les consumers appellent ça
  float getResponseShape() const;         // 0.0-1.0
  uint16_t getSlewRate() const;
  uint16_t getAtDeadzone() const;
  uint16_t getPitchBendOffset() const;    // 0-16383
  float getGateLength() const;            // 0.0-1.0
  float getSwing() const;                 // 0.5-0.75
  ArpDivision getDivision() const;
  ArpPattern getPattern() const;          // NOUVEAU
  uint8_t getOctaveRange() const;         // NOUVEAU — 1-4
  uint8_t getBaseVelocity() const;        // 1-127
  uint8_t getVelocityVariation() const;   // 0-100
  uint16_t getTempoBPM() const;           // 10-260
  uint8_t getLedBrightness() const;       // NOUVEAU — 0-255
  uint8_t getPadSensitivity() const;

  // LED bargraph (quel pot bouge, quel level)
  bool hasBargraphUpdate() const;
  uint8_t getBargraphLevel() const;

private:
  static const PotBinding BINDINGS[];

  // Hardware
  uint16_t _rawAdc[5];          // 4 droite + 1 arrière
  float _smoothedAdc[5];

  // Catch per-binding
  struct CatchState {
    uint16_t storedValue;
    bool caught;
  };
  CatchState _catch[16];  // max bindings

  // Active binding per pot
  const PotBinding* _active[5];

  void resolveBindings(bool btnLeft, bool btnRear, BankType type);
  void updateCatch(uint8_t potIndex);
};
```

### Table de Bindings (déclarative)

```cpp
const PotBinding PotRouter::BINDINGS[] = {
  // Pot Droit 1 (index 0) — Tempo / Division
  {0, 0b00, BANK_ANY,    TARGET_TEMPO_BPM,           10, 260},      // Tempo (global, toujours)
  {0, 0b01, BANK_ARPEG,  TARGET_DIVISION,             0, 4095},     // Hold gauche + ARPEG = division
  // Hold gauche + NORMAL = TARGET_NONE (vide, volontairement)

  // Pot Droit 2 (index 1) — Shape/Gate / Deadzone/Swing
  {1, 0b00, BANK_NORMAL, TARGET_RESPONSE_SHAPE,       0, 4095},
  {1, 0b01, BANK_NORMAL, TARGET_AT_DEADZONE,          0, 250},
  {1, 0b00, BANK_ARPEG,  TARGET_GATE_LENGTH,          0, 4095},
  {1, 0b01, BANK_ARPEG,  TARGET_SWING,                0, 4095},

  // Pot Droit 3 (index 2) — Slew/Pattern / PitchBend/Octave
  {2, 0b00, BANK_NORMAL, TARGET_SLEW_RATE,            SLEW_MIN, SLEW_MAX},
  {2, 0b01, BANK_NORMAL, TARGET_PITCH_BEND,           0, 16383},
  {2, 0b00, BANK_ARPEG,  TARGET_PATTERN,              0, 4095},
  {2, 0b01, BANK_ARPEG,  TARGET_OCTAVE_RANGE,         1, 4},

  // Pot Droit 4 (index 3) — Velocity (identique NORMAL et ARPEG)
  {3, 0b00, BANK_ANY,    TARGET_BASE_VELOCITY,        1, 127},
  {3, 0b01, BANK_ANY,    TARGET_VELOCITY_VARIATION,   0, 100},

  // Pot Arrière (index 4) — LED / Sensitivity
  {4, 0b00, BANK_ANY,    TARGET_LED_BRIGHTNESS,       0, 255},
  {4, 0b10, BANK_ANY,    TARGET_PAD_SENSITIVITY,      5, 30},
};
// buttonMask: bit0=gauche, bit1=arrière
```

Pour ajouter un paramètre futur → ajouter une ligne. Pas de changement dans le loop.

### Divisions Rythmiques (9 positions binaires)

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

Pas de triolets, pas de pointées. Binaires uniquement.

### Stockage des Valeurs Pot

- **Tempo** : global. Un seul tempo partagé par tous les modes et banks.
- **Response shape, slew rate, AT deadzone** : globaux. Partagés par toutes les banks NORMAL.
- **Gate length, swing, division, pattern, octave** : per-bank ARPEG.
- **Base velocity, velocity variation** : per-bank (NORMAL + ARPEG).
- **Pitch bend offset** : per-bank NORMAL.
- **LED brightness, pad sensitivity** : globaux.
- Au bank switch → catch reset pour les params per-bank.

### Coût

~50µs pour 5× analogRead + resolve + catch. Négligeable.

---

## 5. ClockManager — PLL Logicielle

```cpp
class ClockManager {
public:
  void begin();
  void update();  // appelé chaque loop

  // Callbacks branchés sur MidiTransport
  void onMidiClockTick();   // 0xF8
  void onMidiStart();       // 0xFA
  void onMidiStop();        // 0xFC

  // Source
  void setInternalBPM(uint16_t bpm);  // pot droit 1 (10-260)

  // Output — clock lissé
  uint32_t getCurrentTick() const;    // tick monotone
  uint16_t getSmoothedBPM() const;
  bool isExternalSync() const;

private:
  // Réception ticks bruts
  volatile uint32_t _rawTickCount;
  volatile uint32_t _lastRawTickTime;

  // PLL
  float _pllBPM;                    // tempo lissé
  float _pllPhase;                  // phase accumulée
  float _pllTickInterval;           // µs entre ticks internes
  uint32_t _lastPllUpdate;

  // Source priority
  enum ClockSource { SRC_USB, SRC_BLE, SRC_LAST_KNOWN, SRC_INTERNAL };
  ClockSource _activeSource;
  uint32_t _externalTimeoutMs;      // 2000ms sans tick → fallback

  // Interne
  uint16_t _internalBPM;            // pot droit 1 (10-260)

  void updatePLL();                 // recale la PLL sur le clock externe
  void generateInternalTicks();     // génère des ticks si pas de clock externe
};
```

### Priorité des Sources

1. **USB Clock** détecté (ticks arrivent via USB) → source = SRC_USB, PLL se lock dessus.
2. **BLE Clock** détecté (ticks via BLE, pas d'USB) → source = SRC_BLE, PLL lisse le jitter.
3. **Timeout 2s** sans tick → source = SRC_LAST_KNOWN, PLL continue au dernier BPM.
4. **Jamais de clock reçu** → source = SRC_INTERNAL, tempo = pot droit 1.

### PLL — Principe

```
Clock externe (jittery)  →  PLL calcule tempo moyen  →  Clock interne lissé
                                                              ↓
                                                        ArpScheduler
```

La PLL mesure l'intervalle moyen entre 24 ticks (1 noire) et ajuste progressivement son tempo interne. Constante de temps ~500ms — assez rapide pour suivre un accelerando, assez lent pour filtrer le jitter BLE.

Résultat : jitter **±1-2ms** même sur BLE (contre ±15ms sans PLL).

---

## 6. ArpEngine

### Interface

```cpp
class ArpEngine {
public:
  void setChannel(uint8_t ch);
  void setPattern(ArpPattern pattern);
  void setOctaveRange(uint8_t range);     // 1-4
  void setDivision(ArpDivision div);
  void setGateLength(float gate);         // 0.0-1.0
  void setSwing(float swing);             // 0.5-0.75
  void setBaseVelocity(uint8_t vel);      // 1-127
  void setVelocityVariation(uint8_t pct); // 0-100

  // V2 : pile stocke des padOrder POSITIONS, pas des notes MIDI
  void addPadPosition(uint8_t padOrderIndex);
  void removePadPosition(uint8_t padOrderIndex);
  void clearAllPositions();

  // Scale pour résolution au tick
  void setScaleConfig(const ScaleConfig& scale);
  void setPadOrder(const uint8_t* padOrder);

  void setHold(bool on);
  bool isHoldOn() const;

  void playStop();             // toggle, repart du début
  bool isPlaying() const;

  void tick(MidiTransport& transport);  // appelé par ArpScheduler

  uint8_t getPositionCount() const;
  bool hasPositions() const;

private:
  uint8_t _channel;
  ArpPattern _pattern;
  uint8_t _octaveRange;
  float _gateLength;
  float _swing;
  uint8_t _baseVelocity;
  uint8_t _velocityVariation;

  // Pile stocke des padOrder index, PAS des notes MIDI
  uint8_t _positions[48];       // padOrder indices
  uint8_t _positionCount;
  uint8_t _positionOrder[48];   // ordre chronologique d'ajout
  uint8_t _orderCount;

  // Séquence reconstruite à chaque changement de pile ou d'octave
  uint8_t _sequence[192];       // max 48 × 4 octaves (padOrder indices + octave offset)
  uint8_t _sequenceLen;
  bool _sequenceDirty;

  // Scale config pour résolution au tick
  ScaleConfig _scale;
  const uint8_t* _padOrder;

  int16_t _stepIndex;
  int8_t _direction;
  uint8_t _lastPlayedNote;      // MIDI note (pour noteOff correct)
  bool _playing;
  bool _holdOn;

  void rebuildSequence();
  uint8_t resolvePositionToNote(uint8_t padOrderIndex, uint8_t octaveOffset) const;
};
```

### Résolution au Tick (nouveau V2)

```cpp
void ArpEngine::tick(MidiTransport& transport) {
  if (!_playing || _sequenceLen == 0) return;

  // noteOff de la note précédente (utilise _lastPlayedNote, pas re-résolution)
  if (_lastPlayedNote != 0xFF) {
    transport.sendNoteOff(_channel, _lastPlayedNote, 0);
  }

  // Résolution LIVE : padOrder position → MIDI note via ScaleConfig courante
  uint8_t posIndex = _sequence[_stepIndex] & 0x3F;    // padOrder index
  uint8_t octOffset = _sequence[_stepIndex] >> 6;       // octave offset 0-3
  uint8_t note = resolvePositionToNote(posIndex, octOffset);

  if (note != 0xFF && note <= 127) {
    uint8_t vel = computeVelocity();
    transport.sendNoteOn(_channel, note, vel);
    _lastPlayedNote = note;  // stocké pour noteOff correct
  } else {
    _lastPlayedNote = 0xFF;
  }

  advanceStep();
}
```

**Conséquence** : changer la ScaleConfig d'une bank ARPEG en background change immédiatement les notes jouées au tick suivant, sans interruption de l'arpège.

### processArpMode — Logique Complète

```cpp
void processArpMode(BankSlot& slot, const SharedKeyboardState& state,
                    const uint8_t* lastKeys) {
  ArpEngine* arp = slot.arpEngine;
  const uint8_t* keys = state.keyIsPressed;

  for (int i = 0; i < NUM_KEYS; i++) {
    bool pressed = keys[i];
    bool wasPressed = lastKeys[i];

    // Play/Stop pad — actif seulement en HOLD ON
    if (arp->isHoldOn() && i == s_playStopPad) {
      if (pressed && !wasPressed) arp->playStop();
      continue;  // ce pad ne produit pas de note
    }

    // V2 : on stocke la position padOrder, pas la note MIDI
    uint8_t orderIndex = s_padOrder[i];
    if (orderIndex == 0xFF) continue;

    if (arp->isHoldOn()) {
      // HOLD ON : press = ajoute, double-tap = retire
      if (pressed && !wasPressed) {
        if (isDoubleTap(i))
          arp->removePadPosition(orderIndex);
        else
          arp->addPadPosition(orderIndex);
      }
    } else {
      // HOLD OFF : press = ajoute, release = retire
      if (pressed && !wasPressed) arp->addPadPosition(orderIndex);
      else if (!pressed && wasPressed) arp->removePadPosition(orderIndex);
    }
  }
}
```

---

## 7. BankManager

Ne connaît pas l'arpégiateur. Gère uniquement all notes off pour NORMAL via `MidiEngine::allNotesOff()`. Gère aussi le pitch bend offset au switch :

```cpp
void BankManager::switchToBank(uint8_t newBank) {
  BankSlot& old = _banks[_currentBank];

  if (old.type == BANK_NORMAL) {
    // MidiEngine envoie noteOff pour chaque note active via lastResolvedNote[]
    _midiEngine->allNotesOff();
  }
  // ARPEG : rien. L'arpège vit/meurt par sa propre logique.

  old.isForeground = false;
  _banks[newBank].isForeground = true;
  _currentBank = newBank;
  _midiEngine->setChannel(newBank);  // canal MIDI suit la bank

  // V2 : envoyer le pitch bend offset stocké de la nouvelle bank
  if (_banks[newBank].type == BANK_NORMAL) {
    _midiEngine->sendPitchBend(newBank, _banks[newBank].pitchBendOffset);
  }
}
```

**Bouton** : BankManager et ScaleManager reçoivent le **même bouton gauche** (`leftHeld`). Pas de conflit car les pads sont distincts (bank pads ≠ scale pads ≠ arp pads). Vérifié par Tool 3 (collision check).

---

## 8. ScaleResolver

```cpp
uint8_t ScaleResolver::resolve(uint8_t padIndex, const uint8_t* padOrder,
                                const ScaleConfig& scale) {
  uint8_t order = padOrder[padIndex];
  if (order == 0xFF) return 0xFF;
  uint8_t rootBase = ROOT_MIDI_BASE[scale.root];

  if (scale.chromatic) {
    uint8_t note = rootBase + order;
    return (note <= 127) ? note : 0xFF;
  }

  uint8_t degree = order % 7;
  uint8_t octave = order / 7;
  uint8_t note = rootBase + (octave * 12) + SCALE_INTERVALS[scale.mode][degree];
  return (note <= 127) ? note : 0xFF;
}
```

**lastResolvedNote** : stocke la note au noteOn. Le noteOff utilise cette valeur, pas de re-résolution. Empêche les notes orphelines.

---

## 9. NVS

### Namespaces

| Namespace | Key | Contenu |
|---|---|---|
| `illpad_cal` | `caldata` | CalDataStore (maxDelta[48]) |
| `illpad_nmap` | `map` | padOrder[48] (positions 0-47, pas des notes MIDI) |
| `illpad_bpad` | `map` | uint8_t[8] bank pads |
| `illpad_bank` | `bank` | uint8_t bank courante |
| `illpad_set` | `settings` | Settings (profile, AT rate, BLE interval) |
| `illpad_pot` | `params` | Global pot params (response shape, slew rate, AT deadzone) |
| `illpad_btype` | `types` | uint8_t[8] NORMAL/ARPEG |
| `illpad_scale` | `cfg_N` | ScaleConfig par bank |
| `illpad_spad` | `root_pads` | uint8_t[7] |
| `illpad_spad` | `mode_pads` | uint8_t[7] |
| `illpad_spad` | `chrom_pad` | uint8_t |
| `illpad_apad` | `hold_pad` | uint8_t |
| `illpad_apad` | `ps_pad` | uint8_t (play/stop) |
| `illpad_apot` | `params_N` | Arp pot params per bank (gate, swing, div, pattern, oct) |
| `illpad_bvel` | `vel_N` | Base velocity + velocity variation per bank (NORMAL + ARPEG) |
| `illpad_pbnd` | `bend_N` | Pitch bend offset per bank NORMAL |
| `illpad_led` | `brightness` | LED brightness (global) |
| `illpad_sens` | `sensitivity` | Pad sensitivity (global) |

**Changements vs ancien** :
- `illpad_set` : retiré sensitivity, deadzone (exposés via pots)
- `illpad_apad` : réduit à hold_pad + ps_pad (pattern/octave/division → pots)
- `illpad_apot` : ajout pattern, octave
- `illpad_bvel` : **nouveau** — velocity per bank (NORMAL + ARPEG)
- `illpad_pbnd` : **nouveau** — pitch bend per bank
- `illpad_led` : **nouveau** — LED brightness
- `illpad_sens` : **nouveau** — pad sensitivity (sorti de illpad_set)

---

## 10. Setup Mode V2

```
[1] Pressure Calibration    — inchangé V1
[2] Pad Ordering            — positions 1-48, pas de base note
[3] Pad Roles               — bank(8) + scale(15) + arp(2), grille couleur, collisions
[4] Bank Config             — NORMAL/ARPEG (max 4 ARPEG)
[5] Settings                — profile, AT rate, BLE interval
[0] Reboot & Exit
```

### Tool 3 : Pad Roles (simplifié)

```
[1] Bank Pads (8)
[2] Scale Pads (15)      — 7 root + 7 mode + 1 chromatic
[3] Arp Pads (2)         — 1 HOLD + 1 play/stop
[4] View All / Collisions
[s] Save  [q] Back
```

25 pads de contrôle total (avant : 31). 23 pads libres pour le jeu (avant : 17).

### Tool 5 : Settings (réduit)

| Paramètre | Plage | Note |
|-----------|-------|------|
| Baseline Profile | Adaptive/Expressive/Percussive | 3 valeurs discrètes |
| AT Rate | 10-100ms | Aftertouch update interval |
| BLE Connection Interval | Low Latency / Normal / Battery Saver | |

Paramètres retirés (maintenant sur pots hardware) :
- ~~Pad sensitivity~~ → hold arrière + pot arrière
- ~~AT deadzone~~ → hold gauche + pot droit 2 (NORMAL)
- ~~LED brightness~~ → pot arrière seul

---

## 11. Plan d'Implémentation

### Phase 1 : Infrastructure

1. Arborescence fichiers.
2. Double buffer (remplace mutex).
3. NVS task dédiée.
4. BankSlot struct (NORMAL only, avec velocity + pitch bend).
5. BankManager, BatteryMonitor.
6. Nouveau main.cpp (2 boutons, 5 pots).
7. **Test** : comportement V1 avec double buffer.

### Phase 2 : Pots + Scale

1. PotRouter (5 pots, bindings, catch, bargraph).
2. ToolPadOrdering (remplace Note Mapping).
3. ScaleResolver + ScaleManager (même bouton que BankManager).
4. Root = base note.
5. **Test** : chromatique + switch gamme + 5 pots.

### Phase 3 : Pad Roles + Bank Config

1. ToolPadRoles (grille unifiée, 25 pads, collisions).
2. ToolBankConfig (NORMAL/ARPEG).
3. ToolSettings (profile, AT rate, BLE interval).
4. **Test** : 25 pads assignés, pas de collisions.

### Phase 4 : Arpégiateur

1. ClockManager (réception + PLL + fallback).
2. ArpEngine (pile = padOrder positions, résolution au tick).
3. ArpScheduler.
4. Gate length, swing, velocity variation, pattern (pot), octave (pot), division (pot).
5. **Test** : 1 arp → 2 → 4 simultanés. Scale change en background vérifié.

### Phase 5 : Polish

LED feedback, pitch bend, edge cases, factory reset.

---

## 12. Conventions

Fresh start. `s_` static, `_` members, `SCREAMING_SNAKE` constantes. `#if !PRODUCTION_MODE`. Pas de `new`/`delete` runtime. C++17, Arduino, PlatformIO.

---

## 13. Ne PAS Modifier

- `platformio.ini`
- Constantes de tuning pression (HardwareConfig.h)
- Pressure pipeline (CapacitiveKeyboard.cpp)

---

## 14. Performance Worst Case

| Ressource | Utilisation | Verdict |
|---|---|---|
| CPU Core 0 | ~92% (sensing, inchangé) | OK |
| CPU Core 1 | ~16% (avec 4 arps + PotRouter 5 pots) | OK |
| USB MIDI | <1% | OK |
| BLE MIDI | 30-50% (4 arps + 16 pads AT) | Goulot potentiel |
| SRAM | ~5% (~16 KB / 320 KB) | OK |
| Flash | ~6% | OK |

Mitigation BLE : noteOn/Off bypass queue (prioritaires). Aftertouch en queue avec overflow toléré. PLL réduit le jitter clock.
