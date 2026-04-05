# LOOP Mode — Phase 3: Setup Tools + NVS

**Goal**: LOOP bank assignable via setup UI. Remove hardcoded test config. Pad roles persist in NVS. After this phase, the full LOOP workflow is usable from setup mode.

**Prerequisite**: Phase 2 (LoopEngine + wiring) applied, LOOP playback working with hardcoded config.

---

## Overview

Three changes:
1. **ToolBankConfig rewrite** — 2-axis type/quantize (replaces flat cycle)
2. **ToolPadRoles extension** — 4th category LOOP (3 pads)
3. **NVS + main.cpp** — LoopPadStore load, remove test config, dynamic LoopEngine assignment

---

## Step 1 — ToolBankConfig: 2-axis rewrite

The current flat cycle (NORMAL→ARPEG-Imm→ARPEG-Beat→ARPEG-Bar) doesn't scale to 3 types. Replace with:

```
Left/Right : cycle TYPE   (NORMAL → ARPEG → LOOP → NORMAL)
Up/Down    : cycle QUANTIZE (Immediate → Beat → Bar) — only for ARPEG + LOOP
```

### 1a. Remove _potComboState (ToolBankConfig.h, line ~32)

Replace `int32_t _potComboState` with:

```cpp
int32_t _potTypeIdx;       // 0-2 (NORMAL/ARPEG/LOOP) — edit mode pot target
```

### 1b. Helper function (ToolBankConfig.cpp, add before run())

```cpp
static uint8_t countType(const BankType* types, BankType target) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < NUM_BANKS; i++)
        if (types[i] == target) count++;
    return count;
}
```

### 1c. Replace NAV_RIGHT/LEFT cycle (lines 216-269)

Replace the entire `if (ev.type == NAV_RIGHT)` + `else if (ev.type == NAV_LEFT)` block with:

```cpp
      // --- Type axis: Left/Right ---
      if (ev.type == NAV_RIGHT || ev.type == NAV_LEFT) {
          int8_t dir = (ev.type == NAV_RIGHT) ? 1 : -1;
          BankType cur = wkTypes[cursor];
          BankType next;

          // Cycle: NORMAL(0) → ARPEG(1) → LOOP(2) → NORMAL(0)
          uint8_t raw = ((uint8_t)cur + 3 + dir) % 3;
          next = (BankType)raw;

          // Validate constraints
          bool allowed = true;
          if (next == BANK_ARPEG && countType(wkTypes, BANK_ARPEG) >= MAX_ARP_BANKS) {
              // Try next in cycle
              raw = ((uint8_t)raw + 3 + dir) % 3;
              next = (BankType)raw;
              if (next == BANK_LOOP && countType(wkTypes, BANK_LOOP) >= MAX_LOOP_BANKS)
                  allowed = false;
              else if (next == BANK_ARPEG && countType(wkTypes, BANK_ARPEG) >= MAX_ARP_BANKS)
                  allowed = false;
          } else if (next == BANK_LOOP && countType(wkTypes, BANK_LOOP) >= MAX_LOOP_BANKS) {
              raw = ((uint8_t)raw + 3 + dir) % 3;
              next = (BankType)raw;
              if (next == BANK_ARPEG && countType(wkTypes, BANK_ARPEG) >= MAX_ARP_BANKS)
                  allowed = false;
              else if (next == BANK_LOOP && countType(wkTypes, BANK_LOOP) >= MAX_LOOP_BANKS)
                  allowed = false;
          }

          if (allowed && next != cur) {
              wkTypes[cursor] = next;
              // Reset quantize to Immediate when changing type
              if (next != BANK_NORMAL)
                  wkQuantize[cursor] = ARP_START_IMMEDIATE;
              errorShown = false;
          } else if (!allowed) {
              errorShown = true;
              errorTime = millis();
          }
          screenDirty = true;
      }

      // --- Quantize axis: Up/Down (ARPEG + LOOP only) ---
      else if (ev.type == NAV_UP || ev.type == NAV_DOWN) {
          if (wkTypes[cursor] == BANK_ARPEG || wkTypes[cursor] == BANK_LOOP) {
              int8_t dir = (ev.type == NAV_DOWN) ? 1 : -1;
              int8_t q = (int8_t)wkQuantize[cursor] + dir;
              if (q >= 0 && q < NUM_ARP_START_MODES) {
                  wkQuantize[cursor] = (uint8_t)q;
              }
              screenDirty = true;
          }
      }
```

### 1d. Replace edit mode ENTER seed (line ~210)

```cpp
      } else if (ev.type == NAV_ENTER) {
        editing = true;
        _potTypeIdx = (int32_t)wkTypes[cursor];
        _pots.seed(0, &_potTypeIdx, 0, 2, POT_RELATIVE, 8);
        screenDirty = true;
      }
```

### 1e. Replace pot combo sync (remove old _potComboState lines)

In the pot event handling (if pot changes _potTypeIdx in edit mode), map back:

```cpp
      // Pot changes type in edit mode
      if (editing) {
          BankType potType = (BankType)constrain(_potTypeIdx, 0, 2);
          if (potType != wkTypes[cursor]) {
              // Same constraint check as NAV_RIGHT
              bool allowed = true;
              if (potType == BANK_ARPEG && countType(wkTypes, BANK_ARPEG) >= MAX_ARP_BANKS) allowed = false;
              if (potType == BANK_LOOP && countType(wkTypes, BANK_LOOP) >= MAX_LOOP_BANKS) allowed = false;
              if (allowed) {
                  wkTypes[cursor] = potType;
                  if (potType != BANK_NORMAL) wkQuantize[cursor] = ARP_START_IMMEDIATE;
                  screenDirty = true;
              } else {
                  _potTypeIdx = (int32_t)wkTypes[cursor]; // snap back
              }
          }
      }
```

### 1f. Update rendering (lines 286-393)

**Header** (line ~295):
```cpp
      uint8_t arpCount = countType(wkTypes, BANK_ARPEG);
      uint8_t loopCount = countType(wkTypes, BANK_LOOP);
      char headerRight[48];
      snprintf(headerRight, sizeof(headerRight),
               "TOOL 4: BANK CONFIG  %dA/%d  %dL/%d",
               arpCount, MAX_ARP_BANKS, loopCount, MAX_LOOP_BANKS);
```

**Bank line** (lines ~305-344): Replace `bool isArpeg` with 3-way:
```cpp
      const char* typeName;
      const char* typeColor;
      if (wkTypes[i] == BANK_ARPEG)      { typeName = "ARPEG";  typeColor = VT_CYAN; }
      else if (wkTypes[i] == BANK_LOOP)   { typeName = "LOOP";   typeColor = VT_MAGENTA; }
      else                                 { typeName = "NORMAL"; typeColor = ""; }

      if (isEditing) {
          pos += snprintf(line + pos, sizeof(line) - pos,
                          "%s" VT_BOLD "[%s", typeColor, typeName);
          if (wkTypes[i] != BANK_NORMAL)
              pos += snprintf(line + pos, sizeof(line) - pos,
                              " - %s", QUANTIZE_NAMES[wkQuantize[i]]);
          pos += snprintf(line + pos, sizeof(line) - pos, "]" VT_RESET);
      } else {
          pos += snprintf(line + pos, sizeof(line) - pos,
                          selected ? "%s%s" VT_RESET "  " : "%s  ",
                          selected ? typeColor : "", typeName);
          if (wkTypes[i] != BANK_NORMAL)
              pos += snprintf(line + pos, sizeof(line) - pos,
                              "    Quantize: %s", QUANTIZE_NAMES[wkQuantize[i]]);
      }
```

**Quantize description** (line ~360): Extend guard:
```cpp
      if (editing && (wkTypes[cursor] == BANK_ARPEG || wkTypes[cursor] == BANK_LOOP)) {
```

**drawDescription** (line ~46): Add LOOP case:
```cpp
      } else if (isLoop) {
          _ui->drawFrameLine(VT_DIM "Bank %d " VT_MAGENTA "--" VT_RESET VT_DIM
                             " LOOP " VT_MAGENTA "--" VT_RESET VT_DIM
                             " MIDI channel %d" VT_RESET, bank, bank);
          _ui->drawFrameLine(VT_DIM "Percussion looper. Record/overdub/play free-timed patterns." VT_RESET);
          _ui->drawFrameLine(VT_DIM "No scale, no aftertouch. Fixed GM drum mapping." VT_RESET);
      }
```

**Error message** (line ~378): Make generic:
```cpp
      _ui->drawFrameLine(VT_BRIGHT_RED "Max reached! (%d/%d ARPEG, %d/%d LOOP)" VT_RESET,
                          arpCount, MAX_ARP_BANKS, loopCount, MAX_LOOP_BANKS);
```

**Control bar** (line ~387 editing): Update hint:
```cpp
      _ui->drawControlBar(VT_DIM "[</>] TYPE  [^v] QUANTIZE  [P1] type  [RET] SAVE  [q] CANCEL" VT_RESET);
```

---

## Step 2 — ToolPadRoles: 4th category LOOP

### 2a. Add ROLE_LOOP to enum (ToolPadRoles.h, line ~22)

```cpp
  ROLE_PLAYSTOP  = 6,
  ROLE_LOOP_REC  = 7,     // <-- ADD
  ROLE_LOOP_PS   = 8,     // <-- ADD  (LOOP play/stop)
  ROLE_LOOP_CLR  = 9,     // <-- ADD  (LOOP clear)
  ROLE_COLLISION = 0xFF
};
```

### 2b. Add _wk arrays (ToolPadRoles.h, after _wkPlayStopPad, line ~63)

```cpp
  uint8_t _wkLoopRecPad;      // <-- ADD
  uint8_t _wkLoopPlayStopPad; // <-- ADD
  uint8_t _wkLoopClearPad;    // <-- ADD
```

Also add live pointer members for main.cpp sync:
```cpp
  uint8_t* _loopRecPad;
  uint8_t* _loopPlayStopPad;
  uint8_t* _loopClearPad;
```

### 2c. Add to begin() parameters

Pass pointers to the 3 LOOP pad statics in main.cpp (same pattern as _holdPad, _playStopPad).

### 2d. Update pool constants (ToolPadRoles.cpp, lines 17-18)

```cpp
// Was:  {0, 1, 9, 16, 24, 28, 29, 30}   total=30
// Now:  {0, 1, 9, 16, 24, 28, 29, 30, 31, 32, 33}  total=33
static const uint8_t POOL_OFFSETS[] = {0, 1, 9, 16, 24, 28, 29, 30, 31, 32, 33};
static const uint8_t TOTAL_POOL_ITEMS = 33;
```

New lines:
- **Line 7 (Loop Rec)**: offset 30, size 1
- **Line 8 (Loop P/S)**: offset 31, size 1
- **Line 9 (Loop Clear)**: offset 32, size 1

### 2e. Update linearToPool loop bound (line ~24)

```cpp
  for (uint8_t i = 0; i < 10; i++) {  // was 7
```

And `poolToLinear` guard:
```cpp
  if (line >= 10) return TOTAL_POOL_ITEMS - 1;  // was 7
```

### 2f. Add to poolLineSize() (line ~134)

```cpp
    case 7: return 1;   // Loop Rec
    case 8: return 1;   // Loop Play/Stop
    case 9: return 1;   // Loop Clear
```

### 2g. Add labels (near line ~41)

```cpp
static const char* GRID_LOOP_REC_LABELS[] = { " Rec" };
static const char* GRID_LOOP_PS_LABELS[]  = { "LP/S" };
static const char* GRID_LOOP_CLR_LABELS[] = { " Clr" };

static const char* POOL_LOOP_REC_LABELS[] = { "Rec" };
static const char* POOL_LOOP_PS_LABELS[]  = { "P/S" };
static const char* POOL_LOOP_CLR_LABELS[] = { "Clr" };
static const uint8_t POOL_LOOP_REC_COUNT  = 1;
static const uint8_t POOL_LOOP_PS_COUNT   = 1;
static const uint8_t POOL_LOOP_CLR_COUNT  = 1;
```

### 2h. Add to buildRoleMap() (line ~179)

```cpp
  setRole(_wkLoopRecPad,      ROLE_LOOP_REC, GRID_LOOP_REC_LABELS[0]);
  setRole(_wkLoopPlayStopPad, ROLE_LOOP_PS,  GRID_LOOP_PS_LABELS[0]);
  setRole(_wkLoopClearPad,    ROLE_LOOP_CLR, GRID_LOOP_CLR_LABELS[0]);
```

### 2i. Add drawPoolLine calls (after Play/Stop, line ~419)

```cpp
  drawPoolLine(7, "Loop Rec:",  POOL_LOOP_REC_LABELS, POOL_LOOP_REC_COUNT, VT_BRIGHT_RED);
  drawPoolLine(8, "Loop P/S:",  POOL_LOOP_PS_LABELS,  POOL_LOOP_PS_COUNT,  VT_BRIGHT_RED);
  drawPoolLine(9, "Loop Clr:",  POOL_LOOP_CLR_LABELS, POOL_LOOP_CLR_COUNT, VT_BRIGHT_RED);
```

### 2j. Add to findPadWithRole() (line ~229)

```cpp
    case 7: if (index == 0) return _wkLoopRecPad; break;
    case 8: if (index == 0) return _wkLoopPlayStopPad; break;
    case 9: if (index == 0) return _wkLoopClearPad; break;
```

### 2k. Add to assignRole() (line ~253)

```cpp
    case 7: if (index == 0) _wkLoopRecPad = pad; break;
    case 8: if (index == 0) _wkLoopPlayStopPad = pad; break;
    case 9: if (index == 0) _wkLoopClearPad = pad; break;
```

### 2l. Add LoopPadStore save to saveAll() (after ArpPadStore, line ~348)

```cpp
  // 4. LoopPadStore
  LoopPadStore lps;
  lps.magic = EEPROM_MAGIC;
  lps.version = LOOPPAD_VERSION;
  lps.reserved = 0;
  lps.recPad      = _wkLoopRecPad;
  lps.playStopPad = _wkLoopPlayStopPad;
  lps.clearPad    = _wkLoopClearPad;
  lps._pad = 0;
  if (NvsManager::saveBlob(LOOP_PAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY, &lps, sizeof(lps))) {
    *_loopRecPad      = _wkLoopRecPad;
    *_loopPlayStopPad = _wkLoopPlayStopPad;
    *_loopClearPad    = _wkLoopClearPad;
  } else {
    allOk = false;
  }
```

### 2m. Add LoopPadStore load at entry (after ArpPadStore load, line ~630)

```cpp
  LoopPadStore lps;
  bool lpOk = NvsManager::loadBlob(LOOP_PAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY,
                                    EEPROM_MAGIC, LOOPPAD_VERSION, &lps, sizeof(lps));
  if (lpOk) {
    validateLoopPadStore(lps);
    _wkLoopRecPad      = lps.recPad;
    _wkLoopPlayStopPad = lps.playStopPad;
    _wkLoopClearPad    = lps.clearPad;
  }

  _nvsSaved = bpOk && spOk && apOk && lpOk;
```

### 2n. Add to clearRole() if it exists

Add cases for ROLE_LOOP_REC/PS/CLR that set the corresponding _wk* to 0xFF.

### 2o. Add to defaults reset

Set `_wkLoopRecPad = _wkLoopPlayStopPad = _wkLoopClearPad = 0xFF` (unassigned by default).

---

## Step 3 — main.cpp: Remove test config, load from NVS

### 3a. Remove LoopTestConfig.h include and #if blocks

Delete `#include "loop/LoopTestConfig.h"` and all `#if LOOP_TEST_ENABLED` blocks from setup().

### 3b. Delete `src/loop/LoopTestConfig.h`

### 3c. Load LoopPadStore in setup() (after ArpPadStore load, around line ~340)

```cpp
  // Load LOOP pad assignments from NVS
  {
    LoopPadStore lps;
    if (NvsManager::loadBlob(LOOP_PAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY,
                              EEPROM_MAGIC, LOOPPAD_VERSION, &lps, sizeof(lps))) {
      validateLoopPadStore(lps);
      s_recPad      = lps.recPad;
      s_loopPlayPad = lps.playStopPad;
      s_clearPad    = lps.clearPad;
    }
  }
```

### 3d. LoopEngine assignment is already dynamic (Phase 2 Step 4a)

The loop in setup() that assigns LoopEngines to BANK_LOOP banks is already in place from Phase 2. It now works with banks configured via ToolBankConfig instead of the test override.

### 3e. Pass LOOP pad pointers to ToolPadRoles::begin()

```cpp
  s_toolPadRoles.begin(&s_ui, s_banks, ...,
                        &s_recPad, &s_loopPlayPad, &s_clearPad);
```

---

## Step 4 — NvsManager: loadAll() update

### 4a. Add LoopPadStore to loadAll() (NvsManager.cpp)

Follow the ArpPadStore pattern. Load from `illpad_lpad`, validate, and populate the statics in main.cpp (via pointers passed at begin()).

**Note**: LoopPotStore load/save is Phase 4 — not needed here because effects aren't implemented yet.

---

## Build Verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

## Test Verification (hardware)

1. **Flash**, enter setup mode (rear button at boot)
2. **Tool 4**: navigate to bank 7 or 8
   - Press Right → type cycles NORMAL → ARPEG → LOOP → NORMAL
   - Press Left → reverse cycle
   - On ARPEG or LOOP: press Down → Immediate → Beat → Bar
   - Header shows `2A/4 0L/2` (or similar counts)
   - Try to exceed max 4 ARPEG or max 2 LOOP → error message
   - ENTER saves, reboot → config persists
3. **Tool 3**: navigate to 3 unused pads
   - Pool shows new lines: "Loop Rec:", "Loop P/S:", "Loop Clr:" in red
   - Assign 3 pads → grid shows "Rec", "LP/S", "Clr" labels
   - Collision check works (steal confirmation)
   - Save → reboot → assignments persist
4. **Exit setup**, switch to LOOP bank → record/play/overdub/clear works with assigned pads
5. **Reboot** → LOOP bank + pad assignments survive

---

## Files Modified

| File | Changes |
|---|---|
| `src/setup/ToolBankConfig.h` | Replace _potComboState with _potTypeIdx |
| `src/setup/ToolBankConfig.cpp` | 2-axis cycle, countType helper, 3-way rendering, LOOP description, generic error |
| `src/setup/ToolPadRoles.h` | ROLE_LOOP_REC/PS/CLR enum, _wkLoop* arrays, _loopRec/PS/Clr pointers |
| `src/setup/ToolPadRoles.cpp` | Pool constants, linearization, labels, buildRoleMap, draw, find/assign/clear, save/load |
| `src/main.cpp` | Remove test config, load LoopPadStore, pass LOOP pad pointers to ToolPadRoles |
| `src/managers/NvsManager.cpp` | loadAll(): add LoopPadStore load |

## Files Removed

| File | Why |
|---|---|
| `src/loop/LoopTestConfig.h` | Replaced by NVS-backed config |
