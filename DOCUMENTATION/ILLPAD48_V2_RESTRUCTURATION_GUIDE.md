# ILLPAD48 V2 — Guide de Restructuration du Code

> **Audience** : Codeur (Cursor / AI coding assistant).
> **Pré-requis** : Lire `ILLPAD48_V1_ANALYSIS.md` et `ILLPAD48_V2_NEW_IDEAS.md`.
> **Approche** : Fresh start. Pas de backward compatibility.
> Dernière mise à jour : 18 mars 2026.

---

## 1. Résumé des Changements V1 → V2

| Aspect | V1 | V2 |
|---|---|---|
| Boutons | 2 | 3 (gauche=bank, droit=scale+arp, arrière=battery) |
| Pots | 1 | 3 (gauche=feel, droit=rythme, arrière=config) |
| Banks | 1 active | 8 vivantes, ARPEG HOLD en background |
| Types bank | Tous identiques | NORMAL / ARPEG (max 4) |
| Scale | Non implémenté | Runtime root+mode, root=base note |
| Mapping | noteMap = notes MIDI | 3 couches (ordering, scale, control pads) |
| Arpégiateur | N'existe pas | ArpEngine live + persistant |
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
├── HardwareConfig.h                # Pins (3 btn, 3 pots), constantes
│
├── core/
│   ├── CapacitiveKeyboard.cpp/.h       # INCHANGÉ
│   ├── MidiTransport.cpp/.h            # ÉTENDU (réception clock, BLE interval)
│   ├── LedController.cpp/.h            # ÉTENDU
│   └── KeyboardData.h                  # ÉTENDU (BankSlot, ScaleConfig, etc.)
│
├── managers/
│   ├── BankManager.cpp/.h              # Bouton gauche — bank switch
│   ├── ScaleManager.cpp/.h             # Bouton droit — scale + arp controls
│   ├── PotRouter.cpp/.h               # 3 pots, bindings contextuels, catch
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
};
```

---

## 3. Optimisations Architecture

### 3.1 Double Buffer (remplace le Mutex)

Le mutex V1 couple Core 0 et Core 1 — si l'un tient le lock, l'autre attend. Le double buffer élimine ce couplage.

```cpp
struct SharedKeyboardState {
  uint8_t keyIsPressed[NUM_KEYS];
  uint8_t pressure[NUM_KEYS];
};

static SharedKeyboardState s_buffers[2];
static volatile uint8_t s_writeIndex = 0;
static volatile uint8_t s_readIndex = 1;

// Core 0 : écrire dans le buffer d'écriture
void sensingTask(void*) {
  for (;;) {
    SharedKeyboardState& buf = s_buffers[s_writeIndex];
    keyboard.update();
    for (int i = 0; i < NUM_KEYS; i++) {
      buf.keyIsPressed[i] = keyboard.isPressed(i) ? 1 : 0;
      buf.pressure[i] = keyboard.getPressure(i);
    }
    // Swap atomique — Core 1 verra les nouvelles données
    uint8_t tmp = s_writeIndex;
    s_writeIndex = s_readIndex;
    s_readIndex = tmp;

    vTaskDelay(1);
  }
}

// Core 1 : lire sans attente
void loop() {
  const SharedKeyboardState& state = s_buffers[s_readIndex];
  // ... utiliser state — jamais bloqué
}
```

**Paramètres lents (pots → Core 0)** : responseShape et slewRate passent par des `volatile` simples — pas besoin de buffer, ce sont des valeurs scalaires mises à jour rarement.

```cpp
static volatile float s_responseShape = 0.5f;
static volatile uint16_t s_slewRate = 150;

// Core 1 (PotRouter) écrit :
s_responseShape = potRouter.getResponseShape();

// Core 0 (sensingTask) lit :
keyboard.setResponseShape(s_responseShape);
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
  readButtons();
  
  bankManager.update(state.keyIsPressed, btnLeftHeld);
  scaleManager.update(state.keyIsPressed, btnRightHeld, currentSlot);
  
  if (!bankManager.isHolding() && !scaleManager.isHolding()) {
    if (currentSlot.type == BANK_NORMAL)
      processNormalMode(currentSlot, state);
    else
      processArpMode(currentSlot, state);
  }
  
  arpScheduler.tick(clockManager);  // background arps
  midiEngine.flush();
  
  // === SECONDAIRE (peut être retardé de 1-2ms sans impact) ===
  potRouter.update(btnLeftHeld, btnRightHeld, btnRearHeld, currentSlot.type);
  batteryMonitor.update(btnRear);
  ledController.update();
  nvsManager.notifyIfDirty();  // signal à la task NVS, non-bloquant
  
  vTaskDelay(1);
}
```

---

## 4. PotRouter

### Concept

Un seul module lit les 3 ADC, résout les combos boutons, applique le catch, et expose des getters. Les consumers ne savent pas quel pot physique fournit leur paramètre.

```cpp
enum PotTarget : uint8_t {
  // NORMAL
  TARGET_RESPONSE_SHAPE,
  TARGET_SLEW_RATE,
  TARGET_AT_DEADZONE,
  // ARPEG
  TARGET_GATE_LENGTH,
  TARGET_SWING,
  TARGET_DIVISION,
  TARGET_VELOCITY_VARIATION,
  TARGET_BASE_VELOCITY,
  // Global
  TARGET_TEMPO_BPM,
  TARGET_PAD_SENSITIVITY,
  // Libre
  TARGET_NONE
};

struct PotBinding {
  uint8_t potIndex;       // 0=gauche, 1=droit, 2=arrière
  uint8_t buttonMask;     // 0=aucun, bit0=gauche, bit1=droit, bit2=arrière
  BankType bankType;      // NORMAL, ARPEG, ou ANY
  PotTarget target;
  uint16_t rangeMin;
  uint16_t rangeMax;
};

class PotRouter {
public:
  void begin();
  void update(bool btnL, bool btnR, bool btnRear, BankType currentType);

  // Getters — les consumers appellent ça
  float getResponseShape() const;       // 0.0-1.0
  uint16_t getSlewRate() const;
  uint16_t getAtDeadzone() const;
  float getGateLength() const;          // 0.0-1.0
  float getSwing() const;               // 0.5-0.75
  ArpDivision getDivision() const;
  uint8_t getVelocityVariation() const; // 0-100
  uint8_t getBaseVelocity() const;      // 1-127
  uint16_t getTempoBPM() const;         // 40-240
  uint8_t getPadSensitivity() const;

  // LED bargraph (quel pot bouge, quel level)
  bool hasBargraphUpdate() const;
  uint8_t getBargraphLevel() const;

private:
  static const PotBinding BINDINGS[];

  // Hardware
  uint16_t _rawAdc[3];
  float _smoothedAdc[3];

  // Catch per-binding
  struct CatchState {
    uint16_t storedValue;
    bool caught;
  };
  CatchState _catch[12];  // max bindings

  // Active binding per pot
  const PotBinding* _active[3];

  void resolveBindings(bool btnL, bool btnR, bool btnRear, BankType type);
  void updateCatch(uint8_t potIndex);
};
```

### Table de Bindings (déclarative)

```cpp
const PotBinding PotRouter::BINDINGS[] = {
  // Pot Gauche
  {0, 0b000, BANK_NORMAL, TARGET_RESPONSE_SHAPE,     0, 4095},
  {0, 0b010, BANK_NORMAL, TARGET_SLEW_RATE,          SLEW_MIN, SLEW_MAX},
  {0, 0b000, BANK_ARPEG,  TARGET_GATE_LENGTH,        0, 4095},
  {0, 0b010, BANK_ARPEG,  TARGET_SWING,              0, 4095},

  // Pot Droit
  {1, 0b000, BANK_ARPEG,  TARGET_DIVISION,           0, 4095},
  {1, 0b001, BANK_ARPEG,  TARGET_VELOCITY_VARIATION,  0, 100},
  {1, 0b010, BANK_ARPEG,  TARGET_BASE_VELOCITY,      1, 127},
  {1, 0b001, BANK_NORMAL, TARGET_AT_DEADZONE,        0, 250},

  // Pot Arrière
  {2, 0b000, BANK_ANY,    TARGET_TEMPO_BPM,          40, 240},
  {2, 0b001, BANK_ANY,    TARGET_PAD_SENSITIVITY,    5, 30},
};
// buttonMask: bit0=gauche, bit1=droit, bit2=arrière
```

Pour ajouter un paramètre futur → ajouter une ligne. Pas de changement dans le loop.

### Coût

~30µs pour 3× analogRead + resolve + catch. Négligeable.

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
  void setInternalBPM(uint16_t bpm);  // pot arrière

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
  uint16_t _internalBPM;            // pot arrière
  
  void updatePLL();                 // recale la PLL sur le clock externe
  void generateInternalTicks();     // génère des ticks si pas de clock externe
};
```

### Priorité des Sources

1. **USB Clock** détecté (ticks arrivent via USB) → source = SRC_USB, PLL se lock dessus.
2. **BLE Clock** détecté (ticks via BLE, pas d'USB) → source = SRC_BLE, PLL lisse le jitter.
3. **Timeout 2s** sans tick → source = SRC_LAST_KNOWN, PLL continue au dernier BPM.
4. **Jamais de clock reçu** → source = SRC_INTERNAL, tempo = pot arrière.

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

  void addNote(uint8_t midiNote);
  void removeNote(uint8_t midiNote);
  void clearAllNotes();

  void setHold(bool on);
  bool isHoldOn() const;

  void playStop();             // toggle, repart du début
  bool isPlaying() const;

  void tick(MidiTransport& transport);  // appelé par ArpScheduler

  uint8_t getNoteCount() const;
  bool hasNotes() const;

private:
  uint8_t _channel;
  ArpPattern _pattern;
  uint8_t _octaveRange;
  float _gateLength;
  float _swing;
  uint8_t _baseVelocity;
  uint8_t _velocityVariation;

  uint8_t _notes[48];
  uint8_t _noteCount;
  uint8_t _noteOrder[48];
  uint8_t _orderCount;

  uint8_t _sequence[192];       // max 48 × 4 octaves
  uint8_t _sequenceLen;
  bool _sequenceDirty;

  int16_t _stepIndex;
  int8_t _direction;
  uint8_t _lastPlayedNote;
  bool _playing;
  bool _holdOn;

  void rebuildSequence();
};
```

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

    uint8_t note = ScaleResolver::resolve(i, s_padOrder, slot.scale);
    if (note == 0xFF) continue;

    if (arp->isHoldOn()) {
      // HOLD ON : press = ajoute, double-tap = retire
      if (pressed && !wasPressed) {
        if (isDoubleTap(i))
          arp->removeNote(note);
        else
          arp->addNote(note);
      }
    } else {
      // HOLD OFF : press = ajoute, release = retire
      if (pressed && !wasPressed) arp->addNote(note);
      else if (!pressed && wasPressed) arp->removeNote(note);
    }
  }
}
```

---

## 7. BankManager

Ne connaît pas l'arpégiateur. Gère uniquement all notes off pour NORMAL :

```cpp
void BankManager::switchToBank(uint8_t newBank) {
  BankSlot& old = _banks[_currentBank];

  if (old.type == BANK_NORMAL) {
    for (int i = 0; i < NUM_KEYS; i++) {
      if (old.lastResolvedNote[i] != 0xFF) {
        _transport->sendNoteOn(old.channel, old.lastResolvedNote[i], 0);
        old.lastResolvedNote[i] = 0xFF;
      }
    }
  }
  // ARPEG : rien. L'arpège vit/meurt par sa propre logique.

  old.isForeground = false;
  _banks[newBank].isForeground = true;
  _currentBank = newBank;
}
```

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
| `illpad_set` | `settings` | Settings (profile, sens, AT rate, deadzone, BLE interval) |
| `illpad_pot` | `params` | PotParams (tous les paramètres pots, par bank) |
| `illpad_btype` | `types` | uint8_t[8] NORMAL/ARPEG |
| `illpad_scale` | `cfg_N` | ScaleConfig par bank |
| `illpad_spad` | `root_pads` | uint8_t[7] |
| `illpad_spad` | `mode_pads` | uint8_t[7] |
| `illpad_spad` | `chrom_pad` | uint8_t |
| `illpad_apad` | `pat_pads` | uint8_t[5] |
| `illpad_apad` | `oct_pad` | uint8_t |
| `illpad_apad` | `hold_pad` | uint8_t |
| `illpad_apad` | `ps_pad` | uint8_t (play/stop) |

---

## 10. Setup Mode V2

```
[1] Pressure Calibration    — inchangé V1
[2] Pad Ordering            — positions 1-48, pas de base note
[3] Pad Roles               — unifié bank+scale+arp, grille couleur, collisions
[4] Bank Config             — NORMAL/ARPEG (max 4 ARPEG)
[5] Settings                — profile, sens, AT, BLE interval
[0] Reboot & Exit
```

---

## 11. Plan d'Implémentation

### Phase 1 : Infrastructure

1. Arborescence fichiers.
2. Double buffer (remplace mutex).
3. NVS task dédiée.
4. BankSlot struct (NORMAL only).
5. BankManager, BatteryMonitor.
6. Nouveau main.cpp.
7. **Test** : comportement V1 avec double buffer.

### Phase 2 : Pots + Scale

1. PotRouter (3 pots, bindings, catch, bargraph).
2. ToolPadOrdering (remplace Note Mapping).
3. ScaleResolver + ScaleManager.
4. Root = base note.
5. **Test** : chromatique + switch gamme + 3 pots.

### Phase 3 : Pad Roles + Bank Config

1. ToolPadRoles (grille unifiée, collisions).
2. ToolBankConfig (NORMAL/ARPEG).
3. ToolSettings (+ BLE interval).
4. **Test** : 31 pads assignés, pas de collisions.

### Phase 4 : Arpégiateur

1. ClockManager (réception + PLL + fallback).
2. ArpEngine (live + persistant, play/stop, double-tap).
3. ArpScheduler.
4. Gate length, swing, velocity variation.
5. **Test** : 1 arp → 2 → 4 simultanés.

### Phase 5 : Polish

LED feedback, edge cases, factory reset.

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
| CPU Core 0 | ~92% (sensing, inchangé) | ✅ |
| CPU Core 1 | ~16% (avec 4 arps + PotRouter) | ✅ |
| USB MIDI | <1% | ✅ |
| BLE MIDI | 30-50% (4 arps + 16 pads AT) | ⚠️ Goulot potentiel |
| SRAM | ~5% (~16 KB / 320 KB) | ✅ |
| Flash | ~6% | ✅ |

Mitigation BLE : noteOn/Off bypass queue (prioritaires). Aftertouch en queue avec overflow toléré. PLL réduit le jitter clock.
