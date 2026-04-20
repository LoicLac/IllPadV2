> ⚠️ **OBSOLETE — NE PAS LIRE COMME CONTEXTE**
>
> Plan d'implémentation **obsolète** (pré-refactor Phase 0 / 0.1). Ne doit pas être consommé par un agent ou LLM pour planning, implementation, ou audit. Références codebase pré-Phase-0 (Tool 8 3-pages, ColorSlotStore v3, etc.).
>
> **Source de vérité spec LOOP actuelle** : [`docs/superpowers/specs/2026-04-19-loop-mode-design.md`](../superpowers/specs/2026-04-19-loop-mode-design.md). Le plan d'implémentation v2 sera rédigé à partir de la spec révisée Phase 0.1.
>
> Conservé pour archive historique uniquement.

---

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

### 1e. Add LoopQuantMode enum in HardwareConfig.h (after ArpStartMode, line ~268)

LOOP quantize is a **separate field** from ARPEG quantize (intentionally — allows future extension with 1/2 bar, 2 bars, etc., without touching ArpStartMode). Numeric values parallel ArpStartMode for familiarity.

```cpp
// --- Loop Quantize (per-bank LOOP, set in Tool 4) ---
// Affects 6 transitions: start rec, close rec, start overdub, close overdub,
// play, stop. FREE = loop libre (no snap). Abort (PLAY/STOP during
// OVERDUBBING) and CLEAR (long-press) are ALWAYS immediate regardless of mode.
enum LoopQuantMode : uint8_t {
  LOOP_QUANT_FREE  = 0,  // No snap — transitions fire on tap
  LOOP_QUANT_BEAT  = 1,  // Snap to next beat (24 ticks)
  LOOP_QUANT_BAR   = 2,  // Snap to next bar (96 ticks, 4/4)
  NUM_LOOP_QUANT_MODES = 3
};
const uint8_t DEFAULT_LOOP_QUANT_MODE = LOOP_QUANT_FREE;
```

### 1f. Extend BankTypeStore with loopQuantize[] field (KeyboardData.h, line 384)

The current struct is 20 bytes. Adding `loopQuantize[NUM_BANKS]` brings it to 28 bytes — still well under `NVS_BLOB_MAX_SIZE` (128). The `static_assert` already guards the max.

```cpp
struct BankTypeStore {
  uint16_t magic;
  uint8_t  version;
  uint8_t  reserved;
  uint8_t  types[NUM_BANKS];         // BankType enum cast
  uint8_t  quantize[NUM_BANKS];      // ArpStartMode enum (ARPEG banks)
  uint8_t  loopQuantize[NUM_BANKS];  // LoopQuantMode enum (LOOP banks)  <-- ADD
};
static_assert(sizeof(BankTypeStore) <= NVS_BLOB_MAX_SIZE, "BankTypeStore exceeds NVS blob max");
```

**Bump `BANKTYPE_VERSION`** from 1 → 2 to force NVS reset of the new field (Zero Migration Policy — old 20-byte blobs get rejected and defaults apply).

```cpp
#define BANKTYPE_VERSION     2   // was 1
```

---

## Step 2 — KeyboardData.h: LoopPadStore + LoopPotStore

> **AUDIT FIX (C2, 2026-04-06)**: the original plan defined LoopPadStore twice
> (8 bytes in Step 2b with `LOOPPAD_VERSION 1`, then 32 bytes in Step 7c-2 with
> `LOOPPAD_VERSION 2`). This was a consolidation trap for implementers. The
> sub-steps below now define the **final** version (32 bytes, version 2) in
> one pass. Step 7c-2 below has been emptied accordingly.

### 2a. Add NVS namespace defines (after line 344, ARP_PAD_NVS_NAMESPACE)

```cpp
#define LOOP_PAD_NVS_NAMESPACE  "illpad_lpad"
#define LOOPPAD_NVS_KEY         "pads"
#define LOOPPAD_VERSION         2   // slotPads[] included from the start (see Step 2b)

#define LOOPPOT_NVS_NAMESPACE   "illpad_lpot"
// Keys: "loop_0" through "loop_7" (per bank, raw blob, no magic/version)
```

### 2b. Add LoopPadStore struct (after ArpPadStore, line ~380)

This is the **final** struct layout — 32 bytes, version 2. It includes the
`slotPads[16]` array required by the Phase 6 slot drive. Defining it at 32
bytes from Phase 1 avoids a second NVS bump and a struct rewrite in Phase 6.

```cpp
struct LoopPadStore {
  uint16_t magic;                       // EEPROM_MAGIC
  uint8_t  version;                     // LOOPPAD_VERSION (=2)
  uint8_t  reserved;
  uint8_t  recPad;                      // 0xFF = unassigned
  uint8_t  playStopPad;
  uint8_t  clearPad;
  uint8_t  _pad;                        // alignment to 8 bytes
  uint8_t  slotPads[LOOP_SLOT_COUNT];   // 16 bytes — 0xFF = slot unassigned
  uint8_t  _pad2[8];                    // padding to 32 bytes (room for future fields)
};
static_assert(sizeof(LoopPadStore) == 32, "LoopPadStore must be 32 bytes");
static_assert(sizeof(LoopPadStore) <= NVS_BLOB_MAX_SIZE, "LoopPadStore exceeds NVS blob max");
```

**Why 32 bytes total**: 8 bytes (control pads + alignment) + 16 bytes
(slotPads) + 8 bytes (future room) = 32. Aligned, well under the 128 byte
NVS limit. Per the Zero Migration Policy, any NVS blob that existed before
this struct definition is silently rejected at first boot (wrong size) and
defaults apply.

> **Note**: `LOOP_SLOT_COUNT` is defined in `HardwareConfig.h` in Step 7c-1 below
> (the slot drive prerequisites section). Step 2b depends on that constant being
> visible; in the actual codebase both files are included transitively via
> `KeyboardData.h → HardwareConfig.h`, so the order at implementation time can
> be either "Step 7c-1 first, then Step 2b" or "both in the same edit pass".

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

> **AUDIT NOTE (2026-04-06)**: consolidated with the `slotPads[]` validation
> that was previously in Step 7c-3. Final form below.

```cpp
inline void validateLoopPadStore(LoopPadStore& s) {
  if (s.recPad != 0xFF && s.recPad >= NUM_KEYS)             s.recPad      = 0xFF;
  if (s.playStopPad != 0xFF && s.playStopPad >= NUM_KEYS)   s.playStopPad = 0xFF;
  if (s.clearPad != 0xFF && s.clearPad >= NUM_KEYS)         s.clearPad    = 0xFF;
  for (uint8_t i = 0; i < LOOP_SLOT_COUNT; i++) {
    if (s.slotPads[i] != 0xFF && s.slotPads[i] >= NUM_KEYS) s.slotPads[i] = 0xFF;
  }
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
    if (s.loopQuantize[i] >= NUM_LOOP_QUANT_MODES) s.loopQuantize[i] = DEFAULT_LOOP_QUANT_MODE;
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

### 3e. Extend `NvsManager` with `_loadedLoopQuantize[NUM_BANKS]` + getter/setter

> **AUDIT FIX (A1, 2026-04-06)**: Phase 2 Step 4a references an undeclared
> `s_bankTypeStore.loopQuantize[i]` static, which would be a compile error.
> Instead, follow the existing `_loadedQuantize[NUM_BANKS]` + `getLoadedQuantizeMode(i)`
> pattern used for `ArpStartMode`, and add a parallel `_loadedLoopQuantize`
> for `LoopQuantMode`. This step extends the Phase 1 scope to touch NvsManager.h/.cpp.

**In `src/managers/NvsManager.h`** — public section (around line 42, right after
`getLoadedQuantizeMode`) :

```cpp
  // Access loaded quantize modes (per-bank, for ArpEngine init at boot)
  uint8_t getLoadedQuantizeMode(uint8_t bank) const;
  void    setLoadedQuantizeMode(uint8_t bank, uint8_t mode);

  // Access loaded LOOP quantize modes (per-bank, for LoopEngine init at boot)
  uint8_t getLoadedLoopQuantizeMode(uint8_t bank) const;            // <-- ADD
  void    setLoadedLoopQuantizeMode(uint8_t bank, uint8_t mode);    // <-- ADD
```

**In `src/managers/NvsManager.h`** — private section (around line 102, right after
`_loadedQuantize[NUM_BANKS]`) :

```cpp
  uint8_t     _loadedQuantize[NUM_BANKS];      // ArpStartMode per bank (loaded at boot)
  uint8_t     _loadedLoopQuantize[NUM_BANKS];  // LoopQuantMode per bank (loaded at boot)  <-- ADD
```

**In `src/managers/NvsManager.cpp`** — constructor (search for the `memset(_loadedQuantize...)`
line in the constructor body, around the field-init block) :

```cpp
  memset(_loadedQuantize, DEFAULT_ARP_START_MODE, NUM_BANKS);
  memset(_loadedLoopQuantize, DEFAULT_LOOP_QUANT_MODE, NUM_BANKS);   // <-- ADD
```

**In `src/managers/NvsManager.cpp`** — inside `loadAll()`, in the BankTypeStore load
section (look for `_loadedQuantize[i] = bts.quantize[i];`) :

```cpp
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        banks[i].type = (BankType)bts.types[i];
        _loadedQuantize[i]     = bts.quantize[i];
        _loadedLoopQuantize[i] = bts.loopQuantize[i];   // <-- ADD
      }
```

> **Note**: the exact body of the BankTypeStore load block in `loadAll()` uses
> `validateBankTypeStore(bts)` before the copy. Place the new line inside the
> same `for` loop, immediately after `_loadedQuantize[i] = bts.quantize[i]`.

**In `src/managers/NvsManager.cpp`** — getter/setter (near the existing
`getLoadedQuantizeMode` / `setLoadedQuantizeMode` definitions, around line 783) :

```cpp
uint8_t NvsManager::getLoadedLoopQuantizeMode(uint8_t bank) const {
  if (bank >= NUM_BANKS) return DEFAULT_LOOP_QUANT_MODE;
  return _loadedLoopQuantize[bank];
}

void NvsManager::setLoadedLoopQuantizeMode(uint8_t bank, uint8_t mode) {
  if (bank < NUM_BANKS) _loadedLoopQuantize[bank] = mode;
}
```

**Phase 2 Step 4a** will then use `s_nvsManager.getLoadedLoopQuantizeMode(i)`
instead of the bogus `s_bankTypeStore.loopQuantize[i]`. See Phase 2 plan for
the corresponding fix.

---

## Step 4 — HardwareConfig.h: Colors + Constants

### 4a. Add LOOP RGBW colors (after COL_SETUP, line ~47)

```cpp
static constexpr RGBW COL_SETUP     = {128,   0, 255,   0};  // existing

// LOOP colors (red/magenta family) — W=0 for pure chromatic
// Five distinct colors:
//   - COL_LOOP_FREE       : solid during PLAYING/STOPPED in FREE mode (no tick flashes)
//   - COL_LOOP_QUANTIZED  : solid during PLAYING/STOPPED in BEAT/BAR mode (tick flashes active)
//   - COL_LOOP_REC        : blink base during RECORDING (both modes)
//   - COL_LOOP_OVD        : blink base during OVERDUBBING (both modes)
//   - COL_LOOP_DIM        : EMPTY state and background dim base
// Compile-time for Phase 2-4; future Tool 7 LOOP page will migrate to runtime
// color slots (like ARPEG colors already do).
static constexpr RGBW COL_LOOP_FREE      = {255,   0, 100,   0};  // FREE playback — hot magenta-red
static constexpr RGBW COL_LOOP_QUANTIZED = {180,   0, 180,   0};  // QUANTIZED playback — cooler magenta-violet
static constexpr RGBW COL_LOOP_REC       = {255,   0,   0,   0};  // RECORDING — pure red
static constexpr RGBW COL_LOOP_OVD       = {255,   0, 150,   0};  // OVERDUBBING — bright magenta
static constexpr RGBW COL_LOOP_DIM       = { 30,   0,  15,   0};  // EMPTY/background dim
```

### 4b. Add LOOP LED intensity constants (after battery gradient, line ~53)

```cpp
// LOOP LED intensities — 0-100 PERCEPTUAL % (same unit as setPixel intensityPct)
// Mirrors ARPEG pattern: solid states, sine pulse only on FG stopped+events loaded.
// Compile-time for now; future Tool 7 LOOP page will make runtime-configurable.
static constexpr uint8_t LED_FG_LOOP_IDLE       = 20;   // FG EMPTY or STOPPED (no events) — solid dim
static constexpr uint8_t LED_FG_LOOP_STOP_MIN   = 20;   // FG STOPPED + events loaded — sine pulse min
static constexpr uint8_t LED_FG_LOOP_STOP_MAX   = 80;   // FG STOPPED + events loaded — sine pulse max
static constexpr uint8_t LED_FG_LOOP_PLAY       = 85;   // FG PLAYING — solid bright (both FREE and QUANTIZED)
static constexpr uint8_t LED_FG_LOOP_REC        = 75;   // FG RECORDING base intensity (red blink)
static constexpr uint8_t LED_FG_LOOP_OVD        = 75;   // FG OVERDUBBING base intensity (magenta blink)
static constexpr uint8_t LED_BG_LOOP_DIM        = 5;    // BG (all non-playing states) — solid dim
static constexpr uint8_t LED_BG_LOOP_PLAY       = 8;    // BG PLAYING — solid dim base

// Tick flash hierarchy — boosts added to base intensity on beat/bar/wrap crossings.
// Applied ONLY in QUANTIZED modes (BEAT/BAR) during PLAYING/OVERDUBBING, using
// the loop's internal positionUs-derived beat grid. ALSO applied during RECORDING
// (both FREE and QUANTIZED) using globalTick from ClockManager, since the loop
// structure is not yet established. In FREE mode during PLAYING, tick flashes
// are DISABLED — the color stays solid (no internal beat grid exists yet).
//
// Boost = additive percentage points on top of base intensity, clamped to 100.
// Set a boost to 0 to disable the corresponding flash entirely.
// Hierarchy: max(active flash boosts) wins when multiple flashes overlap.
//
// AUDIT FIX (F1, 2026-04-06 pass 2): the original draft included a separate
// LED_TICK_BOOST_BEAT1 = 25 constant for "downbeat (beat 1)". In practice it
// was never used at runtime — when beat 1 of a bar fires, both _beatFlashStart
// and _barFlashStart are set, and the bar boost (40) wins via the hierarchy.
// The constant was kept "for future Tool 7 configurability" which violates the
// YAGNI rule in CLAUDE.md. Removed. Will be reintroduced if/when Tool 7 adds
// runtime configuration of LOOP LED constants.
static constexpr uint8_t LED_TICK_BOOST_BEAT    = 10;   // Normal beat (beats 2/3/4 of a bar)
static constexpr uint8_t LED_TICK_BOOST_BAR     = 40;   // Bar boundary (beat 1 of a new bar)
static constexpr uint8_t LED_TICK_BOOST_WRAP    = 60;   // Loop wrap (end-of-cycle, hardest flash)
static constexpr uint8_t LED_TICK_BOOST_BEAT_BG = 5;    // BG simple beat tick (same color as BG base)

static constexpr uint16_t LED_TICK_DUR_BEAT_MS  = 20;   // Beat flash hold time
static constexpr uint16_t LED_TICK_DUR_BAR_MS   = 40;   // Bar flash hold time
static constexpr uint16_t LED_TICK_DUR_WRAP_MS  = 60;   // Wrap flash hold time

// Waiting quantize blink (during hasPendingAction() wait for boundary)
static constexpr uint16_t LED_WAIT_QUANT_PERIOD_MS = 100;  // blink half-period (on/off)
static constexpr uint8_t  LED_WAIT_QUANT_INTENSITY = 90;   // blink "on" intensity
```

---

## Step 4c — LedController.h/.cpp: CONFIRM_LOOP_REC + showClearRamp stub

Phase 2 `handleLoopControls()` calls `triggerConfirm(CONFIRM_LOOP_REC)` and
`showClearRamp()`. Both must exist for Phase 2 to compile. The real rendering
logic comes in Phase 4 — here we add the minimal skeleton.

> **AUDIT FIX (C1, 2026-04-06)**: the original plan only added the enum
> value in Phase 1 but left the `renderConfirmation()` expiry case for
> Phase 4. Between Phase 2 (which triggers the confirm) and Phase 4, the
> `default:` branch in `renderConfirmation` silently clears the confirm
> at the very first frame, making the feedback "disappear" during
> intermediate testing. Fix : add the expiry case in Phase 1 (without
> rendering). Phase 4 will add the actual overlay rendering.

### 4c-1. Add CONFIRM_LOOP_REC to ConfirmType enum (LedController.h line 27, after CONFIRM_OCTAVE)

```cpp
  CONFIRM_OCTAVE       = 9,
  CONFIRM_LOOP_REC     = 10,  // LOOP record/overdub started — rendering in Phase 4
```

### 4c-2. Add expiry case in `renderConfirmation()` (LedController.cpp, in the switch)

In `src/core/LedController.cpp`, locate `renderConfirmation()` (around line 357)
and find the switch statement that handles per-type expiry. Add the new case
**before** the final `default:` branch :

```cpp
    case CONFIRM_OCTAVE:
      if (elapsed >= _octaveDurationMs) { _confirmType = CONFIRM_NONE; return false; }
      return true;

    case CONFIRM_LOOP_REC:                                          // <-- ADD
      if (elapsed >= 200) { _confirmType = CONFIRM_NONE; return false; }
      return true;

    default:
      _confirmType = CONFIRM_NONE;
      return false;
```

**Why 200 ms**: matches the duration that Phase 4 Step 12a will use for
rendering. Keeping the value in sync at Phase 1 prevents a mismatch when
Phase 4 adds the overlay. 200 ms is also consistent with other instant
confirmations (`CONFIRM_PLAY`, `CONFIRM_STOP`).

**No rendering yet**: `renderNormalDisplay()` is NOT patched in Phase 1.
Between Phase 2 and Phase 4, `triggerConfirm(CONFIRM_LOOP_REC)` will
set `_confirmType = 10` and live for 200 ms without any visible overlay.
Phase 4 Step 12b will add the visual rendering (double red blink on the
current bank LED).

### 4c-3. Add showClearRamp stub (LedController.h, public section)

```cpp
  void showClearRamp(uint8_t pct) { (void)pct; }  // Stub — Phase 4 adds real ramp rendering
```

### 4c-4. Forward-declare LoopEngine (LedController.h, near top)

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

> **AUDIT FIX (B6, 2026-04-06)**: the original plan synced only 15
> `_lastScaleKeys[]` entries (7 root + 7 mode + 1 chrom). Since the early
> return skips the ARPEG-guarded hold/octave sections entirely, their
> entries were left stale. On a subsequent LOOP → ARPEG bank switch with
> the hold pad or an octave pad held, the first ARPEG frame would see a
> phantom rising edge and toggle the hold or trigger an octave change.
> Fix : also sync hold + octave entries in the early return.

```cpp
void ScaleManager::processScalePads(const uint8_t* keyIsPressed, BankSlot& slot) {

  // LOOP banks bypass scale resolution — scale pads are no-op.
  // Still sync _lastScaleKeys to prevent phantom edges on subsequent
  // LOOP → ARPEG bank switch (scale, hold, and octave pads all matter).
  if (slot.type == BANK_LOOP) {
    // Root / mode / chromatic
    for (uint8_t r = 0; r < 7; r++) {
      if (_rootPads[r] < NUM_KEYS) _lastScaleKeys[_rootPads[r]] = keyIsPressed[_rootPads[r]];
      if (_modePads[r] < NUM_KEYS) _lastScaleKeys[_modePads[r]] = keyIsPressed[_modePads[r]];
    }
    if (_chromaticPad < NUM_KEYS) _lastScaleKeys[_chromaticPad] = keyIsPressed[_chromaticPad];
    // Hold + octave (ARPEG-only roles, same phantom-edge risk on switch)
    if (_holdPad < NUM_KEYS) _lastScaleKeys[_holdPad] = keyIsPressed[_holdPad];
    for (uint8_t o = 0; o < 4; o++) {
      if (_octavePads[o] < NUM_KEYS) _lastScaleKeys[_octavePads[o]] = keyIsPressed[_octavePads[o]];
    }
    return;  // Skip root/mode/chrom processing AND hold/octave (already ARPEG-guarded below)
  }

  // --- Root pads (0-6) ---  (existing code continues unchanged)
```

**Why early return, not a wrapper if?** The hold pad (ScaleManager.cpp line 190) and octave
pads (line 207) are already guarded by `slot.type == BANK_ARPEG`. An early return for LOOP
skips root/mode/chromatic AND hold/octave processing — which is correct because LOOP has no
scale, no hold, no octave. The manual sync of `_lastScaleKeys[]` for hold and octave pads
in the early return replaces what the skipped ARPEG-guarded sections would have done
(sync at the end of each section).

**Related pre-existing issue (not in scope)**: the same phantom-edge bug technically exists
for NORMAL → ARPEG transitions with hold or octave pads held. The `BANK_ARPEG` guard on
those sections means `_lastScaleKeys[holdPad/octavePads[]]` is never synced on NORMAL
banks either. Phase 1 only addresses the LOOP → ARPEG case; the NORMAL → ARPEG case
remains pre-existing and out of scope.

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

## Step 7c — Slot Drive prerequisites (forward-compatible additions)

> **DESIGN REF**: see `docs/superpowers/specs/2026-04-06-loop-slot-drive-design.md` §1.4
> (architecture des rôles), §2.6 (modifications composants existants), §1.6 (gestes
> utilisateur). The slot drive itself is implemented in Phase 6 ; this step adds the
> structural extensions that must exist from Phase 1 to avoid double NVS version bumps
> later. **No runtime behavior changes from these additions** — the new fields default
> to 0xFF (unassigned) and the new constants are unused at this stage.

### 7c-1. Add LOOP slot constants in HardwareConfig.h

Add after the LOOP color constants (around line 220, after `COL_LOOP_DIM`):

```cpp
// --- LOOP Slot Drive (Phase 6) ---
// Long-press threshold for save action (release before this = load attempt)
static constexpr uint16_t LOOP_SLOT_LONG_PRESS_MS = 1000;
// Minimum press duration to count as a load (release below this = silent cancel)
static constexpr uint16_t LOOP_SLOT_LOAD_MIN_MS   = 300;
// LED ramp duration during save hold (matches LONG_PRESS_MS by definition)
static constexpr uint16_t LOOP_SLOT_RAMP_DURATION_MS = 1000;

// Slot drive LED colors (red/white/green family) — W=0 for pure chromatic
static constexpr RGBW COL_LOOP_SLOT_LOAD   = {  0, 255,   0,   0};  // Load OK — green
static constexpr RGBW COL_LOOP_SLOT_SAVE   = {  0,   0,   0, 255};  // Save OK — white
static constexpr RGBW COL_LOOP_SLOT_REFUSE = {255,   0,   0,   0};  // Refus — red
static constexpr RGBW COL_LOOP_SLOT_DELETE = {255,   0,   0,   0};  // Delete OK — red (rendered as double blink)

// Number of slot pads (= number of slots in the drive)
static constexpr uint8_t LOOP_SLOT_COUNT = 16;
```

### ~~7c-2~~ — CONSOLIDATED INTO Step 2b (AUDIT 2026-04-06)

`LoopPadStore` is now defined in its final form (32 bytes, version 2) directly
in Step 2b above. No rewrite needed here — Step 2b is the single source of
truth for this struct. This sub-step is intentionally left empty for
traceability.

### ~~7c-3~~ — CONSOLIDATED INTO Step 3a (AUDIT 2026-04-06)

The full `validateLoopPadStore` (with `slotPads[]` validation) should be
written directly in Step 3a below. See Step 3a for the final function.

### 7c-4. PadRoleCode enum extensions (deferred to Phase 3)

The new enum values `ROLE_ARPEG_PLAYSTOP` and `ROLE_LOOP_SLOT_0..15` are NOT added in Phase 1. They belong to the Phase 3 refactor of Tool 3 (which restructures the entire role system to the contextual b1 architecture). Phase 1 is documentation only.

> **AUDIT NOTE**: in the current codebase `PadRoleCode` is defined in `src/setup/ToolPadRoles.h` (lines 14-23). Phase 3 will refactor it; Phase 1 leaves the file untouched.

### 7c-5. Forward declaration of `s_loopSlotPads[16]` in Phase 2 step 3c

This sub-step is a forward-edit to Phase 2's snippet. When Phase 2 lands, the LOOP control pads section in main.cpp must already declare a `s_loopSlotPads[16]` array, even though Phase 2 doesn't use it. The Phase 2 plan (Step 3c) is updated to include this declaration. The Phase 1 task here is to **acknowledge the cross-phase dependency** — no Phase 1 code change.

Reference: see `docs/plans/loop-phase2-engine-wiring.md` Step 3c, which now includes:

```cpp
static uint8_t  s_loopSlotPads[LOOP_SLOT_COUNT];   // initialized to 0xFF in setup()
```

And the corresponding `memset` in setup() (Step 4 of Phase 2).

### Build verification for Step 7c

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected: clean build. The new constants are defined but unused at this stage (no warnings expected from `-Wunused-variable` since they are `constexpr` at namespace scope, which the compiler tolerates).

### Test verification for Step 7c

1. Flash firmware. NVS layout has changed (`LoopPadStore` 12 → 32 bytes) but Phase 1 doesn't load this store yet, so no observable change.
2. All NORMAL/ARPEG behavior identical to baseline.

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
| `src/core/KeyboardData.h` | BANK_LOOP enum, LoopEngine forward-declare, BankSlot.loopEngine, MAX_LOOP_BANKS, LoopPadStore (with slotPads[16]), LOOPPAD_VERSION=2, LoopPotStore, validateLoopPadStore (with slot validation), fix validateBankTypeStore, PotMappingStore.loopMap, NVS defines, NVS_DESCRIPTORS, TOOL_NVS indices |
| `src/core/HardwareConfig.h` | LoopQuantMode enum, COL_LOOP_FREE/QUANTIZED/REC/OVD/DIM, LED_FG/BG_LOOP_* intensities, LED_TICK_BOOST_*, LED_TICK_DUR_*, LED_WAIT_QUANT_*, **LOOP_SLOT_LONG_PRESS_MS / LOAD_MIN_MS / RAMP_DURATION_MS, COL_LOOP_SLOT_LOAD/SAVE/REFUSE/DELETE, LOOP_SLOT_COUNT** |
| `src/managers/BankManager.h` | MidiTransport forward-declare, _transport member, begin() signature |
| `src/managers/BankManager.cpp` | begin() body, switchToBank() LOOP guards (stub), debug print 3-way |
| `src/managers/ScaleManager.cpp` | processScalePads() LOOP early return with _lastScaleKeys sync (root/mode/chrom + hold + octave) |
| `src/managers/NvsManager.h` | `_loadedLoopQuantize[NUM_BANKS]` member + `getLoadedLoopQuantizeMode()` / `setLoadedLoopQuantizeMode()` (Step 3e, A1 fix) |
| `src/managers/NvsManager.cpp` | Constructor memset, loadAll() copies `bts.loopQuantize[i]`, getter/setter bodies (Step 3e, A1 fix) |
| `src/main.cpp` | begin() call updated with &s_transport |
| `src/core/LedController.h` | CONFIRM_LOOP_REC enum value, showClearRamp stub, LoopEngine forward-declare |
| `src/core/LedController.cpp` | CONFIRM_LOOP_REC expiry case in renderConfirmation (C1 fix, Step 4c-1 below) |

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

---

## Audit Notes (2026-04-06) — deep review cycle

### C2 — LoopPadStore double-definition (**FIXED above**)
Phase 1 définissait `LoopPadStore` deux fois : 8 bytes en Step 2b (version 1),
puis 32 bytes en Step 7c-2 (version 2). Piège pour l'implémenteur qui lit
linéairement. **Fix** : Step 2b contient directement la version finale à
32 bytes, version 2. Step 7c-2 et 7c-3 sont émptiés (consolidated markers
conservés pour traçabilité). Step 3a contient la validation complète avec
`slotPads[]`. Plus de re-définition, un seul bloc de vérité.

### A1 — `s_bankTypeStore` undeclared in Phase 2 (**FIXED in Step 3e above**)
Phase 2 Step 4a référençait `s_bankTypeStore.loopQuantize[i]` qui n'existe
nulle part comme static global — compile error garanti. **Fix** : nouveau
Step 3e qui étend `NvsManager` avec `_loadedLoopQuantize[NUM_BANKS]` +
`getLoadedLoopQuantizeMode(bank)`, suivant exactement le pattern existant
de `_loadedQuantize` + `getLoadedQuantizeMode` pour `ArpStartMode`. Phase 2
Step 4a utilisera `s_nvsManager.getLoadedLoopQuantizeMode(i)` à la place.
Cette extension élargit le scope de Phase 1 vers `NvsManager.h/.cpp`, noté
dans la table Files Modified.

### C1 — CONFIRM_LOOP_REC silent clear between Phase 2 and Phase 4 (**FIXED in Step 4c-2 above**)
Phase 1 ajoutait la valeur enum `CONFIRM_LOOP_REC = 10` mais ne touchait
pas `renderConfirmation()`. Le `default:` branch existant clear
silencieusement tout confirm non-connu au premier frame. Entre Phase 2
(qui trigger) et Phase 4 (qui ajoute le rendering + expiry), le confirm
était inaudible/invisible. **Fix** : ajouter le case d'expiry dans
Phase 1 Step 4c-2 (200 ms, sans rendu). Le rendu overlay reste en Phase 4.
Permet aux tests intermédiaires Phase 2/3 de ne pas être surpris.

### B6 — `_lastScaleKeys` sync incomplete pour hold/octave pads (**FIXED in Step 6a above**)
L'early return LOOP dans `processScalePads` ne synchronisait que les 15
pads scale (root + mode + chrom), laissant les pads hold et octave non
synchronisés. Au switch LOOP → ARPEG avec hold ou octave tenu, phantom
edge possible. **Fix** : sync explicit de `_lastScaleKeys[holdPad]` et
`_lastScaleKeys[octavePads[0..3]]` dans l'early return. 5 lignes
supplémentaires. Note : le même problème latent existe pour NORMAL →
ARPEG (pré-existant, hors scope).

### OBSERVATION #6 — LED intensités LOOP en compile-time
Les `LED_FG_LOOP_*` sont des `constexpr` dans HardwareConfig.h. Les intensités ARPEG équivalentes sont dans LedSettingsStore (runtime, configurables via Tool 7). Cette incohérence est acceptée en Phase 1 mais devra être résolue quand Tool 7 intègre les paramètres LOOP (future phase).
