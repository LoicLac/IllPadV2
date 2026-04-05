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

**UX change (intentional)**: the old `_potComboState` (0-3) combined type+quantize in one pot axis. The new `_potTypeIdx` (0-2) is type-only. Quantize is now arrow-keys-only (UP/DOWN). This simplification is needed because 3 types × 3 quantize modes = 9 combinations doesn't fit a pot range cleanly.

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
                  screenDirty = true;  // refresh to show snap-back
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

**drawDescription** — change signature from `bool isArpeg` to `BankType type` (line ~46 .cpp, line 22 .h):

Header (.h line 22):
```cpp
  void drawDescription(uint8_t cursor, BankType type);  // was: bool isArpeg
```

Implementation (.cpp line 46):
```cpp
void ToolBankConfig::drawDescription(uint8_t cursor, BankType type) {
  if (type == BANK_ARPEG) {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank %d" VT_RESET VT_DIM "  --  ARPEG  --  MIDI channel %d" VT_RESET, cursor + 1, cursor + 1);
    _ui->drawFrameLine(VT_DIM "Arpeggiator on this channel. No aftertouch. Pile-based note input:" VT_RESET);
    _ui->drawFrameLine(VT_DIM "press=add, release=remove (HOLD OFF) or double-tap=remove (HOLD ON)." VT_RESET);
    _ui->drawFrameLine(VT_DIM "Gate, shuffle, pattern, division, velocity are per-bank via pot mapping." VT_RESET);
  } else if (type == BANK_LOOP) {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank %d" VT_RESET VT_DIM "  " VT_MAGENTA "--" VT_RESET VT_DIM
                       "  LOOP  " VT_MAGENTA "--" VT_RESET VT_DIM "  MIDI channel %d" VT_RESET, cursor + 1, cursor + 1);
    _ui->drawFrameLine(VT_DIM "Percussion looper. Record/overdub/play free-timed patterns." VT_RESET);
    _ui->drawFrameLine(VT_DIM "No scale, no aftertouch. Fixed GM drum mapping." VT_RESET);
  } else {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank %d" VT_RESET VT_DIM "  --  NORMAL  --  MIDI channel %d" VT_RESET, cursor + 1, cursor + 1);
    _ui->drawFrameLine(VT_DIM "Pads play notes directly with polyphonic aftertouch." VT_RESET);
    _ui->drawFrameLine(VT_DIM "Velocity = baseVelocity +/- variation (per-bank). Pitch bend offset per-bank." VT_RESET);
    _ui->drawFrameLine(VT_DIM "Shape, slew, AT deadzone are global (affect all banks)." VT_RESET);
  }
}
```

Call site (.cpp line 357) — change from bool to BankType:
```cpp
  drawDescription(cursor, wkTypes[cursor]);  // was: wkTypes[cursor] == BANK_ARPEG
```
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

### 2e. Update pool navigation bounds

**POOL_LINE_COUNT** (ToolPadRoles.h line 107) — keyboard navigation wraps at this value:
```cpp
  static const uint8_t POOL_LINE_COUNT = 10;  // was 7 — now 0=clear, 1-9=categories
```
Without this, UP/DOWN keys wrap between 0-6 and lines 7/8/9 (Loop Rec/P-S/Clear) are unreachable.

**linearToPool** loop bound (ToolPadRoles.cpp line ~24):
```cpp
  for (uint8_t i = 0; i < 10; i++) {  // was 7
```

**poolToLinear** guard:
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

  // Note: do NOT include lpOk — LoopPadStore may not exist on first boot
  // (Zero Migration Policy: defaults are fine, badge corrects after first save)
  _nvsSaved = bpOk && spOk && apOk;
```

### 2n. Add to clearRole() if it exists

Add cases for ROLE_LOOP_REC/PS/CLR that set the corresponding _wk* to 0xFF.

### 2o. Add to defaults reset + clearAllRoles

**resetToDefaults()** (line ~284) — add after existing octave reset:
```cpp
  _wkLoopRecPad      = 0xFF;
  _wkLoopPlayStopPad = 0xFF;
  _wkLoopClearPad    = 0xFF;
```

**clearAllRoles()** (line 274-282) — add after octave memset:
```cpp
  _wkLoopRecPad      = 0xFF;
  _wkLoopPlayStopPad = 0xFF;
  _wkLoopClearPad    = 0xFF;
```
Without this, pressing `r` (reset all) leaves LOOP pad assignments intact.

### 2p. Add to getRoleForPad() (line 186-204)

Add before the `return {0, 0}` at the end:
```cpp
  if (_wkLoopRecPad == pad)      return {7, 0};
  if (_wkLoopPlayStopPad == pad) return {8, 0};
  if (_wkLoopClearPad == pad)    return {9, 0};
```
Without this, pads with LOOP roles show "no role assigned" in the info panel.

### 2q. Add to clearRole() (line 255-272)

Add after the octave loop (line 270-271):
```cpp
  if (_wkLoopRecPad == pad)      _wkLoopRecPad = 0xFF;
  if (_wkLoopPlayStopPad == pad) _wkLoopPlayStopPad = 0xFF;
  if (_wkLoopClearPad == pad)    _wkLoopClearPad = 0xFF;
```

### 2r. Add to poolItemLabel() (line 136)

Add cases after case 6:
```cpp
    case 7: return (index == 0) ? POOL_LOOP_REC_LABELS[0] : "???";
    case 8: return (index == 0) ? POOL_LOOP_PS_LABELS[0]  : "???";
    case 9: return (index == 0) ? POOL_LOOP_CLR_LABELS[0] : "???";
```

### 2s. Add to printRoleDescription() (line 436-485)

Add cases after case 6 (Play/Stop):
```cpp
    case 7:  // Loop Rec
      _ui->drawFrameLine(VT_BRIGHT_RED "Loop REC" VT_RESET "  " VT_DIM "--  LOOP banks only" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Tap to start recording, tap again to close (bar-snap)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "During playback: tap to overdub, tap again to merge." VT_RESET);
      break;
    case 8:  // Loop Play/Stop
      _ui->drawFrameLine(VT_BRIGHT_RED "Loop Play/Stop" VT_RESET "  " VT_DIM "--  LOOP banks only" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Tap to toggle playback. Supports quantized start (Immediate/Beat/Bar)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "On NORMAL/ARPEG banks, this pad plays as a regular music pad." VT_RESET);
      break;
    case 9:  // Loop Clear
      _ui->drawFrameLine(VT_BRIGHT_RED "Loop Clear" VT_RESET "  " VT_DIM "--  LOOP banks only (long press)" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Hold 500ms to clear the loop. LED ramp shows hold progress." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Loop content is ephemeral — lost on reboot. Clear is irreversible." VT_RESET);
      break;
```

### 2t. Add to run() live-to-working copy (at entry, before NVS load)

In `run()` (line ~586), where the existing code copies live pads to working copies
(e.g. `_wkHoldPad = *_holdPad`), add:
```cpp
  _wkLoopRecPad      = *_loopRecPad;
  _wkLoopPlayStopPad = *_loopPlayStopPad;
  _wkLoopClearPad    = *_loopClearPad;
```

---

## Step 3 — main.cpp: Remove test config, load from NVS

### 3a. Remove LoopTestConfig.h include and #if blocks

Delete `#include "loop/LoopTestConfig.h"` and all `#if LOOP_TEST_ENABLED` blocks from setup().

### 3b. Delete `src/loop/LoopTestConfig.h`

### ~~3c.~~ REMOVED — LoopPadStore loading is in Step 4a (via `loadAll()`) only

Loading in both `setup()` and `loadAll()` would be a double read. The consistent
pattern (ArpPadStore, ScalePadStore, BankPadStore) is to load in `loadAll()` and
populate main.cpp statics via pointers passed at `begin()`. Step 4a handles this.

### 3d. LoopEngine assignment is already dynamic (Phase 2 Step 4a)

The loop in setup() that assigns LoopEngines to BANK_LOOP banks is already in place from Phase 2. It now works with banks configured via ToolBankConfig instead of the test override.

### 3e. Thread LOOP pad references through SetupManager

The call chain is: `main.cpp` → `SetupManager::begin()` → `_toolRoles.begin()`.
All 3 must be updated.

**SetupManager.h** (line 24-29) — extend `begin()` signature:
```cpp
  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             NvsManager* nvs, BankSlot* banks,
             uint8_t* padOrder, uint8_t* bankPads,
             uint8_t* rootPads, uint8_t* modePads,
             uint8_t& chromaticPad, uint8_t& holdPad, uint8_t& playStopPad,
             uint8_t* octavePads, PotRouter* potRouter,
             uint8_t& recPad, uint8_t& loopPlayPad, uint8_t& clearPad);  // <-- ADD
```

**SetupManager.cpp** (line 30-33) — forward to `_toolRoles.begin()`:
```cpp
  _toolRoles.begin(keyboard, leds, &_ui,
                   bankPads, rootPads, modePads,
                   chromaticPad, holdPad, playStopPad,
                   octavePads,
                   recPad, loopPlayPad, clearPad);  // <-- ADD
```

**ToolPadRoles.h** (line 35-39) — extend `begin()` signature:
```cpp
  void begin(CapacitiveKeyboard* keyboard, LedController* leds, SetupUI* ui,
             uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
             uint8_t& chromaticPad, uint8_t& holdPad, uint8_t& playStopPad,
             uint8_t* octavePads,
             uint8_t& recPad, uint8_t& loopPlayPad, uint8_t& clearPad);  // <-- ADD
```

**ToolPadRoles.cpp** — store pointers in `begin()` body:
```cpp
  _loopRecPad      = &recPad;
  _loopPlayStopPad = &loopPlayPad;
  _loopClearPad    = &clearPad;
```

**main.cpp** (line 270-274) — pass LOOP pad statics:
```cpp
  s_setupManager.begin(&s_keyboard, &s_leds, &s_nvsManager,
                       s_banks, s_padOrder, bankPads,
                       rootPads, modePads, chromaticPad,
                       holdPad, s_playStopPad, octavePads,
                       &s_potRouter,
                       s_recPad, s_loopPlayPad, s_clearPad);  // <-- ADD
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
| `src/setup/ToolBankConfig.h` | Replace _potComboState with _potTypeIdx, drawDescription signature change |
| `src/setup/ToolBankConfig.cpp` | 2-axis cycle, countType helper, 3-way rendering, drawDescription 3-way, generic error |
| `src/setup/ToolPadRoles.h` | ROLE_LOOP_REC/PS/CLR enum, _wkLoop* arrays, _loopRec/PS/Clr pointers, POOL_LINE_COUNT=10 |
| `src/setup/ToolPadRoles.cpp` | Pool constants, linearization, labels, buildRoleMap, draw, find/assign/clear/clearAll, getRoleForPad, poolItemLabel, printRoleDescription, save/load, run() live-to-wk copy |
| `src/setup/SetupManager.h` | begin() signature: add recPad, loopPlayPad, clearPad params |
| `src/setup/SetupManager.cpp` | Forward LOOP pad refs to _toolRoles.begin() |
| `src/main.cpp` | Remove test config, pass LOOP pad statics to SetupManager::begin() |
| `src/managers/NvsManager.cpp` | loadAll(): add LoopPadStore load + populate LOOP pad statics |

## Files Removed

| File | Why |
|---|---|
| `src/loop/LoopTestConfig.h` | Replaced by NVS-backed config |

---

## Audit Notes (2026-04-05)

### BUG #1 — `drawDescription` signature incompatible (**ToolBankConfig**)

Le plan Step 1f ajoute `} else if (isLoop) {` mais la signature actuelle est
`drawDescription(uint8_t cursor, bool isArpeg)`. Il n'y a pas de paramètre `isLoop`.

**Fix** : changer la signature en `drawDescription(uint8_t cursor, BankType type)` et
adapter le corps :
```cpp
void ToolBankConfig::drawDescription(uint8_t cursor, BankType type) {
    uint8_t bank = cursor + 1;
    if (type == BANK_ARPEG) {
        // ... existing ARPEG description ...
    } else if (type == BANK_LOOP) {
        // ... LOOP description from plan Step 1f ...
    } else {
        // ... existing NORMAL description ...
    }
}
```
Call site : `drawDescription(cursor, wkTypes[cursor]);`

### BUG #2 — `POOL_LINE_COUNT` non mis à jour (**ToolPadRoles**)

Le header `ToolPadRoles.h` line 107 définit `static const uint8_t POOL_LINE_COUNT = 7;`.
Avec 3 lignes LOOP ajoutées, il faut passer à **10**. Sans ça, les lignes LOOP sont
affichées mais **inaccessibles au clavier** (la navigation clavier wrappe entre 0 et 6).

**Fix** : `static const uint8_t POOL_LINE_COUNT = 10;`

### BUG #3 — `_nvsSaved` faux négatif au premier boot (**ToolPadRoles**)

Step 2m fait `_nvsSaved = bpOk && spOk && apOk && lpOk`. Sur un appareil qui n'a
jamais sauvegardé de LOOP pads, `lpOk = false` → badge "NVS: --" même si les 3
autres stores sont OK.

**Fix** : garder `_nvsSaved = bpOk && spOk && apOk;` inchangé. Les defaults LOOP
(0xFF) sont corrects sans NVS. `saveAll()` écrira les 4 stores ensemble, et `lpOk`
sera true après le premier save.

### GAP #4 — `SetupManager` non listé dans les fichiers modifiés

`ToolPadRoles::begin()` n'est pas appelé directement depuis main.cpp.
L'appel passe par `SetupManager::begin()` → `_toolRoles.begin()`.
Les 3 nouveaux paramètres (recPad, loopPlayPad, clearPad) doivent être
threadés à travers `SetupManager.h/.cpp`.

**Fix** : ajouter `SetupManager.h` et `SetupManager.cpp` dans "Files Modified",
avec la signature étendue de `begin()`.

### GAP #5 — Fonctions ToolPadRoles non listées dans le plan

Plusieurs fonctions de `ToolPadRoles.cpp` ont besoin de mises à jour
non mentionnées dans le plan :

1. **`clearAllRoles()`** — doit reset `_wkLoopRecPad = _wkLoopPlayStopPad = _wkLoopClearPad = 0xFF`
2. **`getRoleForPad()`** — doit retourner les rôles LOOP (codes 7/8/9)
3. **`printRoleDescription()`** — cases 7/8/9 pour les labels LOOP
4. **`drawCellGrid()` (dans SetupUI)** — mapping couleur pour les rôles LOOP

Sans `getRoleForPad()`, l'éditeur de rôles montre "no role" pour les pads LOOP.
Sans `clearAllRoles()`, la touche `r` (reset all) ne clear pas les pads LOOP.

### GAP #6 — Double load LoopPadStore (Steps 3c + 4a)

Step 3c charge LoopPadStore dans `setup()` ET Step 4a l'ajoute à `loadAll()`.
Doublon. Choisir une seule approche : soit `loadAll()` (cohérent avec ArpPadStore),
soit le standalone dans `setup()` (mais inédit pour ce type de store).

**Recommandation** : utiliser `loadAll()` uniquement (Step 4a) et supprimer Step 3c.

### GAP #7 — Pot quantize UX change non documenté

L'ancien `_potComboState` (0-3) permettait de choisir type+quantize via le pot.
Le nouveau `_potTypeIdx` (0-2) ne cycle que le type. Le quantize passe en
arrows-only. Ce changement UX devrait être documenté comme intentionnel.

### GAP #8 — Pot snap-back sans `screenDirty`

Step 1e : quand le pot snap-back après un échec de contrainte
(`_potTypeIdx = (int32_t)wkTypes[cursor]`), il manque `screenDirty = true`.
L'affichage ne se rafraîchit pas pour montrer le snap-back.

### GAP #9 — `run()` live-to-working copy pour LOOP pads

`run()` copie les valeurs live vers les working copies au début (avant le load NVS).
Ajouter :
```cpp
_wkLoopRecPad      = *_loopRecPad;
_wkLoopPlayStopPad = *_loopPlayStopPad;
_wkLoopClearPad    = *_loopClearPad;
```
