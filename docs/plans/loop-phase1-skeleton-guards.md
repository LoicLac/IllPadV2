# LOOP Mode — Phase 1: Skeleton + Guards

**Goal**: Prepare the codebase for LOOP mode. After this phase, the build compiles and behavior is **identical to before** — no LOOP bank exists at runtime. All guards are traversed without effect.

**Prerequisite**: Phase 0 (doc consolidation) done.

---

## Step 1 — KeyboardData.h: Enum + BankSlot

### 1a. Add BANK_LOOP to BankType enum (line 281)

```cpp
enum BankType : uint8_t {
  BANK_NORMAL = 0,
  BANK_ARPEG  = 1,
  BANK_LOOP   = 2,       // <-- ADD
  BANK_ANY    = 0xFF
};
```

### 1b. Forward-declare LoopEngine (after ArpEngine, line 294)

```cpp
class ArpEngine;
class LoopEngine;          // <-- ADD
```

### 1c. Add loopEngine pointer to BankSlot (line 296)

```cpp
struct BankSlot {
  uint8_t     channel;
  BankType    type;
  ScaleConfig scale;
  ArpEngine*  arpEngine;
  LoopEngine* loopEngine;   // <-- ADD (nullptr if not LOOP)
  bool        isForeground;
  uint8_t     baseVelocity;
  uint8_t     velocityVariation;
  uint16_t    pitchBendOffset;
};
```

### 1d. Add MAX_LOOP_BANKS (after MAX_ARP_BANKS, line 472)

```cpp
const uint8_t MAX_ARP_BANKS    = 4;
const uint8_t MAX_LOOP_BANKS   = 2;   // <-- ADD
```

---

## Step 2 — KeyboardData.h: LoopPadStore + LoopPotStore

### 2a. Add NVS namespace defines (after line 344, ARP_PAD_NVS_NAMESPACE)

```cpp
#define LOOP_PAD_NVS_NAMESPACE  "illpad_lpad"
#define LOOPPAD_NVS_KEY         "pads"
#define LOOPPAD_VERSION         1

#define LOOPPOT_NVS_NAMESPACE   "illpad_lpot"
// Keys: "loop_0" through "loop_7" (per bank, raw blob, no magic/version)
```

### 2b. Add LoopPadStore struct (after ArpPadStore, line ~380)

```cpp
struct LoopPadStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  uint8_t  recPad;         // 0xFF = unassigned
  uint8_t  playStopPad;
  uint8_t  clearPad;
  uint8_t  _pad;           // alignment to 8 bytes
};
static_assert(sizeof(LoopPadStore) <= NVS_BLOB_MAX_SIZE, "LoopPadStore exceeds NVS blob max");
```

### 2c. Add LoopPotStore struct (after ArpPotStore, line ~117)

```cpp
struct LoopPotStore {
  uint16_t shuffleDepthRaw;    // 0-4095 (maps to 0.0-1.0)
  uint8_t  shuffleTemplate;    // 0-9
  uint8_t  velPatternIdx;      // 0-3
  uint16_t chaosRaw;           // 0-4095 (maps to 0.0-1.0)
  uint16_t velPatternDepthRaw; // 0-4095 (maps to 0.0-1.0)
};
static_assert(sizeof(LoopPotStore) == 8, "LoopPotStore must be 8 bytes");
```

---

## Step 3 — KeyboardData.h: Validation + Descriptors

### 3a. Add validateLoopPadStore (after validateArpPadStore, line ~515)

```cpp
inline void validateLoopPadStore(LoopPadStore& s) {
  if (s.recPad != 0xFF && s.recPad >= NUM_KEYS)      s.recPad = 0xFF;
  if (s.playStopPad != 0xFF && s.playStopPad >= NUM_KEYS) s.playStopPad = 0xFF;
  if (s.clearPad != 0xFF && s.clearPad >= NUM_KEYS)    s.clearPad = 0xFF;
}
```

### 3b. Fix validateBankTypeStore (line 495)

Replace the current function.

> **AUDIT FIX (BUG #1)**: The original plan clamped unconditionally after
> counting — `if (arpCount > MAX) s.types[i] = NORMAL` would demote a
> legitimate LOOP bank just because the arp counter overflowed earlier.
> Each clamp must be gated on the bank's own type.

```cpp
inline void validateBankTypeStore(BankTypeStore& s) {
  uint8_t arpCount = 0;
  uint8_t loopCount = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (s.types[i] > BANK_LOOP) s.types[i] = BANK_NORMAL;  // was: > BANK_ARPEG
    if (s.types[i] == BANK_ARPEG) {
      arpCount++;
      if (arpCount > MAX_ARP_BANKS) s.types[i] = BANK_NORMAL;
    }
    if (s.types[i] == BANK_LOOP) {
      loopCount++;
      if (loopCount > MAX_LOOP_BANKS) s.types[i] = BANK_NORMAL;
    }
    if (s.quantize[i] >= NUM_ARP_START_MODES) s.quantize[i] = DEFAULT_ARP_START_MODE;
  }
}
```

### 3c. Add LoopPadStore to NVS_DESCRIPTORS (line ~644, after ArpPadStore entry)

```cpp
  { ARP_PAD_NVS_NAMESPACE,     ARPPAD_NVS_KEY,         EEPROM_MAGIC,    ARPPAD_VERSION,       (uint16_t)sizeof(ArpPadStore)       },  // 4: T3c
  { LOOP_PAD_NVS_NAMESPACE,    LOOPPAD_NVS_KEY,        EEPROM_MAGIC,    LOOPPAD_VERSION,      (uint16_t)sizeof(LoopPadStore)      },  // 5: T3d  <-- ADD
  { BANKTYPE_NVS_NAMESPACE,    BANKTYPE_NVS_KEY_V2,    EEPROM_MAGIC,    BANKTYPE_VERSION,     (uint16_t)sizeof(BankTypeStore)     },  // 6: T4 (was 5)
```

### 3d. Update TOOL_NVS_FIRST/LAST for shifted indices

```cpp
// T3 now spans 4 descriptors (indices 2-5): bankpad+scalepad+arppad+looppad
// T4-T7 indices shift +1
static constexpr uint8_t TOOL_NVS_FIRST[] = { 0, 1, 2, 6, 7, 8, 10 };
static constexpr uint8_t TOOL_NVS_LAST[]  = { 0, 1, 5, 6, 7, 8, 11 };
```

**Note**: NVS_DESCRIPTOR_COUNT is computed via sizeof — no manual update needed.

---

## Step 4 — HardwareConfig.h: Colors + Constants

### 4a. Add LOOP RGBW colors (after COL_SETUP, line ~47)

```cpp
static constexpr RGBW COL_SETUP     = {128,   0, 255,   0};  // existing

// LOOP colors (red/magenta family) — W=0 for pure chromatic
static constexpr RGBW COL_LOOP      = {255,   0,  60,   0};  // LOOP foreground — hot magenta
static constexpr RGBW COL_LOOP_DIM  = { 40,   0,  10,   0};  // LOOP background
static constexpr RGBW COL_LOOP_REC  = {255,   0,  40,   0};  // Recording — red-magenta
```

### 4b. Add LOOP LED intensity constants (after battery gradient, line ~53)

```cpp
// LOOP LED intensities — 0-100 PERCEPTUAL % (same unit as setPixel intensityPct)
// Mirrors ARPEG pattern: solid states, sine pulse only on FG stopped+events loaded.
// Compile-time for now; future Tool 7 will make runtime-configurable.
static constexpr uint8_t LED_FG_LOOP_IDLE       = 20;   // FG EMPTY or STOPPED (no events) — solid dim
static constexpr uint8_t LED_FG_LOOP_STOP_MIN   = 20;   // FG STOPPED + events loaded — sine pulse min
static constexpr uint8_t LED_FG_LOOP_STOP_MAX   = 80;   // FG STOPPED + events loaded — sine pulse max
static constexpr uint8_t LED_FG_LOOP_PLAY       = 85;   // FG PLAYING — solid bright
static constexpr uint8_t LED_FG_LOOP_FLASH      = 100;  // FG wrap flash spike
static constexpr uint8_t LED_BG_LOOP_DIM        = 5;    // BG (all non-playing states) — solid dim
static constexpr uint8_t LED_BG_LOOP_PLAY       = 8;    // BG PLAYING — solid dim
static constexpr uint8_t LED_BG_LOOP_FLASH      = 25;   // BG wrap flash spike
```

---

## Step 4c — LedController.h: CONFIRM_LOOP_REC + showClearRamp stub

Phase 2 `handleLoopControls()` calls `triggerConfirm(CONFIRM_LOOP_REC)` and
`showClearRamp()`. Both must exist for Phase 2 to compile. The real rendering
logic comes in Phase 4 — here we add the minimal skeleton.

### 4c-1. Add CONFIRM_LOOP_REC to ConfirmType enum (line 27, after CONFIRM_OCTAVE)

```cpp
  CONFIRM_OCTAVE       = 9,
  CONFIRM_LOOP_REC     = 10,  // LOOP record/overdub started — rendering in Phase 4
```

### 4c-2. Add showClearRamp stub (LedController.h, public section)

```cpp
  void showClearRamp(uint8_t pct) { (void)pct; }  // Stub — Phase 4 adds real ramp rendering
```

### 4c-3. Forward-declare LoopEngine (LedController.h, near top)

```cpp
class LoopEngine;  // Needed by Phase 4 renderBankLoop, declared early for clean includes
```

Phase 4 will replace the `showClearRamp` stub with a real implementation
(member variables `_clearRampPct`, `_showingClearRamp`, rendering in
`renderNormalDisplay`).

---

## Step 5 — BankManager: MidiTransport pointer + LOOP guards

### 5a. Add MidiTransport forward-declare and member (BankManager.h)

In BankManager.h, add forward declaration and member:

```cpp
class MidiTransport;  // forward-declare (add near top, after other forward-declares)

// In private members (line ~33):
  MidiTransport* _transport = nullptr;
```

### 5b. Extend begin() signature (BankManager.h line ~15, BankManager.cpp line 24)

```cpp
// .h
void begin(MidiEngine* engine, LedController* leds, BankSlot* banks,
           uint8_t* lastKeys, MidiTransport* transport);

// .cpp
void BankManager::begin(MidiEngine* engine, LedController* leds,
                         BankSlot* banks, uint8_t* lastKeys,
                         MidiTransport* transport) {
  _engine    = engine;
  _leds      = leds;
  _banks     = banks;
  _lastKeys  = lastKeys;
  _transport = transport;

  if (_leds) _leds->setCurrentBank(_currentBank);
}
```

### 5c. Add include in BankManager.cpp (line 1)

```cpp
#include "../core/MidiTransport.h"  // for flushLiveNotes
```

### 5d. Add LOOP guards in switchToBank() (BankManager.cpp line 117)

After the initial guard `if (newBank >= NUM_BANKS || newBank == _currentBank) return;`, add:

> **AUDIT FIX (BUG #2)**: The original stub checked only `loopEngine != nullptr`,
> which blocks ALL bank switches from a LOOP bank as soon as Phase 2 assigns the
> engine. The guard must check the engine's recording/overdubbing STATE.
> Phase 1 has no LoopEngine class yet, so use `nullptr` check (always false)
> with a comment showing the real check for Phase 2.

```cpp
  // LOOP recording lock: deny switch while recording/overdubbing
  if (_banks[_currentBank].type == BANK_LOOP && _banks[_currentBank].loopEngine) {
    // Phase 2 will replace this with:
    //   if (_banks[_currentBank].loopEngine->isRecording()) return;
    // For now loopEngine is always nullptr — guard never fires.
  }
```

Before the foreground flag swap (line ~126), add:

```cpp
  // Flush LOOP live notes on outgoing bank (CC123)
  if (_banks[_currentBank].type == BANK_LOOP && _banks[_currentBank].loopEngine && _transport) {
    // Phase 2: loopEngine->flushLiveNotes(*_transport, _currentBank);
  }
```

### 5e. Fix debug print (BankManager.cpp line 148)

Replace the ternary:

```cpp
  const char* typeNames[] = {"NORMAL", "ARPEG", "LOOP"};
  uint8_t t = _banks[_currentBank].type;
  Serial.printf("[BANK] Bank %d (ch %d, %s)\n",
                _currentBank + 1, _currentBank + 1,
                (t <= BANK_LOOP) ? typeNames[t] : "???");
```

### 5f. Update begin() call in main.cpp

Find the call to `s_bankManager.begin(...)` in main.cpp setup() and add `&s_transport` as last argument.

---

## Step 6 — ScaleManager: LOOP guard on scale pads

### 6a. Guard root/mode/chromatic processing (ScaleManager.cpp line 124)

After the opening brace of `processScalePads()`, before the root pads loop:

```cpp
void ScaleManager::processScalePads(const uint8_t* keyIsPressed, BankSlot& slot) {

  // LOOP banks bypass scale resolution — scale pads are no-op.
  // Still sync _lastScaleKeys to prevent phantom edges on bank switch.
  if (slot.type == BANK_LOOP) {
    for (uint8_t r = 0; r < 7; r++) {
      if (_rootPads[r] < NUM_KEYS) _lastScaleKeys[_rootPads[r]] = keyIsPressed[_rootPads[r]];
      if (_modePads[r] < NUM_KEYS) _lastScaleKeys[_modePads[r]] = keyIsPressed[_modePads[r]];
    }
    if (_chromaticPad < NUM_KEYS) _lastScaleKeys[_chromaticPad] = keyIsPressed[_chromaticPad];
    return;  // Skip to hold/octave which are already BANK_ARPEG-guarded
  }

  // --- Root pads (0-6) ---  (existing code continues unchanged)
```

**Why early return, not a wrapper if?** The hold pad (line 190) and octave pads (line 207) are already guarded by `slot.type == BANK_ARPEG`. An early return for LOOP skips root/mode/chromatic AND hold/octave — which is correct because LOOP has no scale, no hold, no octave.

---

## Step 7 — Initialize loopEngine to nullptr in main.cpp

> **AUDIT SNIPPET (GAP #5)**: The code at main.cpp lines 191-202 assigns each
> BankSlot field explicitly (channel, type, scale, arpEngine, isForeground,
> baseVelocity, velocityVariation, pitchBendOffset). Add `loopEngine` in the
> same loop, right after `arpEngine`.

```cpp
// main.cpp setup(), line ~197 (after arpEngine line)
  s_banks[i].loopEngine         = nullptr;       // <-- ADD
```

## Step 7b — Add `loopMap[8]` to PotMappingStore (KeyboardData.h)

> **AUDIT FIX (BUG #3)**: Phase 4 Step 5b says "PotMappingStore is already
> extended in Phase 1" but no step in Phase 1 adds it. Must be done here to
> avoid a compile error in Phase 4. Adding `loopMap` changes the struct size
> from 36 to 52 bytes — still under NVS_BLOB_MAX_SIZE (128).

```cpp
struct PotMappingStore {
  uint16_t   magic;    // Must match EEPROM_MAGIC
  uint8_t    version;  // POTMAP_VERSION
  uint8_t    reserved;
  PotMapping normalMap[POT_MAPPING_SLOTS];
  PotMapping arpegMap[POT_MAPPING_SLOTS];
  PotMapping loopMap[POT_MAPPING_SLOTS];    // <-- ADD
};
```

> **AUDIT NOTE**: Version bump is optional. Per Zero Migration Policy
> (CLAUDE.md), the size mismatch (36 vs 52 bytes) will cause `loadBlob()`
> to reject the old blob and apply defaults. No migration code needed.

---

## Build Verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected: **clean build, zero warnings** related to LOOP. Possible warning about unused `LoopPotStore` — acceptable (struct defined but not yet used).

## Test Verification

1. Flash the firmware
2. All NORMAL/ARPEG behavior identical — play notes, arp, bank switch, scale change
3. Setup mode → Tool 4: no LOOP option visible yet (ToolBankConfig not modified)
4. NVS: existing configs load correctly (no corruption from new enum value — validator now accepts BANK_LOOP but no bank has this type)

---

## Files Modified

| File | Changes |
|---|---|
| `src/core/KeyboardData.h` | BANK_LOOP enum, LoopEngine forward-declare, BankSlot.loopEngine, MAX_LOOP_BANKS, LoopPadStore, LoopPotStore, validateLoopPadStore, fix validateBankTypeStore, PotMappingStore.loopMap, NVS defines, NVS_DESCRIPTORS, TOOL_NVS indices |
| `src/core/HardwareConfig.h` | COL_LOOP/COL_LOOP_DIM/COL_LOOP_REC, LED_FG/BG_LOOP_* intensity constants |
| `src/managers/BankManager.h` | MidiTransport forward-declare, _transport member, begin() signature |
| `src/managers/BankManager.cpp` | begin() body, switchToBank() LOOP guards (stub), debug print 3-way |
| `src/managers/ScaleManager.cpp` | processScalePads() LOOP early return with _lastScaleKeys sync |
| `src/main.cpp` | begin() call updated with &s_transport |
| `src/core/LedController.h` | CONFIRM_LOOP_REC enum value, showClearRamp stub, LoopEngine forward-declare |

## Files NOT Modified

| File | Why |
|---|---|
| `LedController.cpp` | No LOOP rendering yet (Phase 4) |
| `PotRouter` | No LOOP targets yet (Phase 4) |
| `ToolBankConfig` | No LOOP cycling yet (Phase 3) |
| `ToolPadRoles` | No LOOP pads yet (Phase 3) |
| `NvsManager` | LoopPotStore load/save comes with Phase 4 |
| `GrooveTemplates.h` | Already extracted (commit 0f31838) |

---

## Audit Notes (2026-04-05)

### BUG #1 — validateBankTypeStore clamp croisé (**FIXED above**)
Le clamp `if (arpCount > MAX) s.types[i] = NORMAL` s'exécutait pour TOUT bank après overflow du compteur, y compris des LOOP banks légitimes. Fix : gater chaque clamp sur le type du bank courant.

### BUG #2 — BankManager recording lock stub bloquant (**FIXED above**)
Le guard `if (loopEngine != nullptr) return;` bloque tout switch dès Phase 2 quand les engines sont assignées. Le guard doit tester `isRecording()`, pas la présence du pointer. En Phase 1, loopEngine est toujours nullptr, pas de risque, mais le commentaire doit documenter le vrai guard pour Phase 2.

### BUG #3 — PotMappingStore loopMap absent (**FIXED above**)
Phase 4 référence `loopMap[8]` comme "already extended in Phase 1" mais aucun step ne l'ajoutait. Ajouté en Step 7b.

### ~~GAP #4~~ — POTMAP_VERSION (**SUPPRIMÉ — Zero Migration Policy**)
Le size mismatch (36→52 octets) suffit à rejeter l'ancien blob. Pas de bump nécessaire.

### GAP #5 — main.cpp init BankSlot snippet manquant (**FIXED above**)
Le code actuel initialise chaque champ explicitement (pas de memset). Le `loopEngine = nullptr` doit être ajouté dans la boucle.

### OBSERVATION #6 — LED intensités LOOP en compile-time
Les `LED_FG_LOOP_*` sont des `constexpr` dans HardwareConfig.h. Les intensités ARPEG équivalentes sont dans LedSettingsStore (runtime, configurables via Tool 7). Cette incohérence est acceptée en Phase 1 mais devra être résolue quand Tool 7 intègre les paramètres LOOP (future phase).
