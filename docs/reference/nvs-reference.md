# NVS Reference — ILLPAD V2

Source of truth for all Non-Volatile Storage usage. Read before touching any NVS code.

---

## API — 3 Static Helpers on NvsManager

All setup Tools and `NvsManager::loadAll()` use these. No direct `Preferences` in setup code.

```cpp
// Read blob, validate magic + version. Returns false on missing/corrupt/mismatch.
static bool NvsManager::loadBlob(const char* ns, const char* key,
                                  uint16_t expectedMagic, uint8_t expectedVersion,
                                  void* out, size_t expectedSize);

// Write blob. Warns (DEBUG_SERIAL) if magic==0. Returns false on write failure.
static bool NvsManager::saveBlob(const char* ns, const char* key,
                                  const void* data, size_t size);

// Validate only (no data copy). Used by menu status badges.
static bool NvsManager::checkBlob(const char* ns, const char* key,
                                   uint16_t expectedMagic, uint8_t expectedVersion,
                                   size_t expectedSize);
```

Internally, `loadBlob` and `checkBlob` share `readAndValidateBlob()` (anonymous namespace in NvsManager.cpp). All error paths log under `#if DEBUG_SERIAL` (magic mismatch, version mismatch, size mismatch, namespace open failed, write failed).

**Important**: `Preferences` is still used directly in `NvsManager.cpp` (runtime async saves via dirty flags + FreeRTOS task) and `CapacitiveKeyboard.cpp` (calibration save — DO NOT MODIFY). Only setup Tools are fully migrated to the static helpers.

---

## Descriptor Table

`NVS_DESCRIPTORS[11]` in `KeyboardData.h` — one entry per Store blob:

```cpp
struct NvsDescriptor {
  const char* ns;
  const char* key;
  uint16_t    magic;
  uint8_t     version;
  uint16_t    size;
};
```

`TOOL_NVS_FIRST[7]` / `TOOL_NVS_LAST[7]` map Tools 1-7 to descriptor index ranges:

| Tool | Descriptors | Stores |
|------|-------------|--------|
| T1 Calibration | [0] | CalDataStore |
| T2 Pad Ordering | [1] | NoteMapStore |
| T3 Pad Roles | [2..4] | BankPadStore + ScalePadStore + ArpPadStore |
| T4 Bank Config | [5] | BankTypeStore |
| T5 Settings | [6] | SettingsStore |
| T6 Pot Mapping | [7..8] | PotMappingStore + PotFilterStore |
| T7 LED Settings | [9..10] | LedSettingsStore + ColorSlotStore |

Menu (`SetupUI::printMainMenu`) loops over these to check all stores in one pass.

---

## Validation Functions

8 `inline validate*()` functions in `KeyboardData.h`. Called after every `loadBlob` and before every `saveBlob` from external input (future WiFi). Single source of truth for field bounds.

| Function | Clamps |
|----------|--------|
| `validateSettingsStore` | profile, AT rate, BLE interval, clock mode, double-tap, bargraph, panic, batADC |
| `validateBankTypeStore` | types (max BANK_ARPEG), arpCount (max 4), quantize modes, scaleGroup (max NUM_SCALE_GROUPS) |
| `validateScalePadStore` | rootPads, modePads, chromaticPad (all < NUM_KEYS) |
| `validateArpPadStore` | holdPad, octavePads (all < NUM_KEYS) |
| `validateBankPadStore` | bankPads (all < NUM_KEYS) |
| `validateNoteMapStore` | noteMap entries (all < NUM_KEYS) |
| `validateLedSettingsStore` | intensity cross-validation, timing ranges, confirmation blink counts/durations |
| `validatePotFilterStore` | snap, actThresh, sleepEn, sleepMs, deadband, edgeSnap, wakeThresh |

---

## Store Struct Catalog

All structs have magic (uint16_t) + version (uint8_t) at bytes 0-2. `NVS_BLOB_MAX_SIZE` (128) enforced by `static_assert` on every struct.

### Existing Stores (unchanged formats)

| Struct | Namespace | Key | Magic | Version | Size | Owner |
|--------|-----------|-----|-------|---------|------|-------|
| `CalDataStore` | `illpad_cal` | `caldata` | 0xBEEF | 5 | 102B | CapacitiveKeyboard (DO NOT MODIFY) |
| `NoteMapStore` | `illpad_nmap` | `map` | 0xBEEF | 1 | 52B | T2 PadOrdering |
| `BankPadStore` | `illpad_bpad` | `map` | 0xBEEF | 1 | 12B | T3 PadRoles |
| `SettingsStore` | `illpad_set` | `settings` | 0xBEEF | 10 | 14B | T5 Settings |
| `PotParamsStore` | `illpad_pot` | `params` | 0xBEEF | 2 | 10B | NvsManager (runtime) |
| `PotMappingStore` | `illpad_pmap` | `mapping` | 0xBEEF | 1 | 36B | T6 PotMapping |
| `PotFilterStore` | `illpad_pflt` | `cfg` | 0xBEEF | 1 | 12B | PotFilter (runtime, tuned via T6 Monitor). Fields: snap, actThresh, sleepEn, sleepMs, deadband, edgeSnap, wakeThresh |
| `LedSettingsStore` | `illpad_lset` | `ledsettings` | 0xBEEF | 3 | 38B | T7 LedSettings |
| `ColorSlotStore` | `illpad_lset` | `ledcolors` | 0xC010 | 1 | 30B | T7 LedSettings |

### V2 Stores (replace raw formats)

| Struct | Namespace | Key | Magic | Version | Size | Replaces |
|--------|-----------|-----|-------|---------|------|----------|
| `ScalePadStore` | `illpad_spad` | `pads` | 0xBEEF | 1 | 20B | 3 separate keys (root_pads, mode_pads, chrom_pad) |
| `ArpPadStore` | `illpad_apad` | `pads` | 0xBEEF | 2 | 12B | 2 separate keys (hold_pad, oct_pads) — v2 drops legacy play/stop pad |
| `BankTypeStore` | `illpad_btype` | `config` | 0xBEEF | 2 | 28B | raw types[8] + qmode[8] (2 blobs, desync risk). v2 adds `scaleGroup[8]` (0=none, 1..4=A..D) for inter-bank scale linking |
| `LoopPadStore` | `illpad_lpad` | `pads` | 0xBEEF | 1 | 8B | **PLANNED** — not yet in code |

### Non-Blob Namespaces (scalar values, not Store structs)

| Namespace | Key pattern | Content | Owner |
|-----------|-------------|---------|-------|
| `illpad_bank` | `bank` | uint8_t current bank (0-7) | NvsManager (runtime) |
| `illpad_scale` | `cfg_0`..`cfg_7` | ScaleConfig per bank (3B each) | NvsManager (runtime) |
| `illpad_bvel` | `vel_0`..`vel_7`, `var_0`..`var_7` | velocity params per bank | NvsManager (runtime) |
| `illpad_pbnd` | `pb_0`..`pb_7` | pitch bend offset per bank | NvsManager (runtime) |
| `illpad_apot` | `arp_0`..`arp_7` | ArpPotStore per bank (8B each) | NvsManager (runtime) |
| `illpad_lpot` | `loop_0`..`loop_7` | LoopPotStore per bank (8B each): shuffle, chaos, vel pattern | **PLANNED** — not yet in code |
| `illpad_tempo` | `bpm` | uint16_t tempo BPM | NvsManager (runtime) |
| `illpad_led` | `brightness` | uint8_t LED brightness | NvsManager (runtime) |
| `illpad_sens` | `sensitivity` | uint8_t pad sensitivity | NvsManager (runtime) |

---

## Compile-Time Guards

```cpp
static_assert(sizeof(CalDataStore) <= NVS_BLOB_MAX_SIZE, "...");
// ... one per Store struct ...
static_assert(offsetof(SettingsStore, baselineProfile) == 3,
              "byte 3 must be baselineProfile (not reserved)");
```

The `offsetof` guard prevents accidentally zeroing `baselineProfile` by treating byte 3 as `reserved`.

---

## How To Add a New Namespace

1. **Define the Store struct** in `KeyboardData.h` (magic + version + reserved + fields). Add `static_assert(sizeof(...) <= NVS_BLOB_MAX_SIZE)`.

2. **Define constants**: `#define MYNS_NVS_NAMESPACE "illpad_xxx"`, `#define MYNS_NVS_KEY "data"`, `#define MYNS_VERSION 1`.

3. **Add a validate function**: `inline void validateMyStore(MyStore& s) { ... }` in `KeyboardData.h`.

4. **Add to descriptor table**: append entry to `NVS_DESCRIPTORS[]`, update `NVS_DESCRIPTOR_COUNT` (automatic via sizeof), update `TOOL_NVS_FIRST/LAST` if it maps to a Tool.

5. **Use in setup Tool**: `NvsManager::loadBlob(...)` at entry, `NvsManager::saveBlob(...)` on save. Call `validateMyStore()` after load.

6. **Use in loadAll()** (if needed at boot): add `NvsManager::loadBlob(...)` + validate call.

7. **Update this doc** and the NVS section in `CLAUDE.md`.

---

## Save Patterns

### Setup Tools — synchronous, blocking
```cpp
// Load at entry
MyStore wk;
bool loaded = NvsManager::loadBlob(NS, KEY, MAGIC, VERSION, &wk, sizeof(wk));
if (loaded) validateMyStore(wk);

// Save on user action
wk.magic = EEPROM_MAGIC;
wk.version = MY_VERSION;
wk.reserved = 0;  // always zero padding fields
if (NvsManager::saveBlob(NS, KEY, &wk, sizeof(wk))) {
    // update live pointers
    ui->flashSaved();
}
```

### Per-namespace live update (T3 pattern — 3 stores)
Each `saveBlob` that succeeds updates its live pointers immediately. If one fails, the others are still attempted. NVS and live stay coherent per-namespace.

### Runtime — async via dirty flags
`NvsManager::queueXxxWrite()` sets dirty flag + pending data. Background FreeRTOS task commits to flash via `Preferences`. Loop never blocks.

---

## SettingsStore Byte-3 Trap

`SettingsStore` has `baselineProfile` at byte 3, NOT `reserved`. Many Store structs use byte 3 as padding, so a generic `s.reserved = 0` pattern would zero the baseline profile. The `static_assert(offsetof(...) == 3)` in KeyboardData.h defends this layout. In `saveSettings()`, do NOT add `toSave.reserved = 0` — there is no reserved field.
