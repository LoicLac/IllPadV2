# LOOP Mode — Phase 3: Setup Tools + NVS

**Goal**: LOOP bank assignable via setup UI. Remove hardcoded test config. Pad roles persist in NVS. After this phase, the full LOOP workflow is usable from setup mode.

**Prerequisite**: Phase 2 (LoopEngine + wiring) applied, LOOP playback working with hardcoded config.

---

## Overview

Three changes:
1. **ToolBankConfig rewrite** — 2-axis type/quantize (replaces flat cycle)
2. **ToolPadRoles extension** — 4th category LOOP (3 pads)
3. **NVS + main.cpp** — LoopPadStore load, remove test config, dynamic LoopEngine assignment

**LOOP quantize is a separate NVS field.** Phase 1 added `loopQuantize[NUM_BANKS]` to `BankTypeStore` alongside the existing `quantize[NUM_BANKS]`. ARPEG banks read/write `quantize[]` (`ArpStartMode`), LOOP banks read/write `loopQuantize[]` (`LoopQuantMode`). Tool 4's Up/Down axis dispatches to the correct field based on the current bank type.

---

## Step 1 — ToolBankConfig: 2-axis rewrite

The current flat cycle (NORMAL→ARPEG-Imm→ARPEG-Beat→ARPEG-Bar) doesn't scale to 3 types. Replace with:

```
Left/Right : cycle TYPE      (NORMAL → ARPEG → LOOP → NORMAL)
Up/Down    : cycle QUANTIZE  (ARPEG: Immediate → Beat → Bar)
                               (LOOP:  Free      → Beat → Bar)
             — ARPEG banks edit quantize[] (ArpStartMode)
             — LOOP  banks edit loopQuantize[] (LoopQuantMode)
             — NORMAL: Up/Down ignored
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

### 1b-bis. Extend run() working copies with wkLoopQuantize

Alongside the existing `wkTypes[]` and `wkQuantize[]` stack arrays in `run()`, add a third working copy for the LOOP quantize field. Initialize from `BankTypeStore.loopQuantize[]` (loaded via `NvsManager::loadBlob()`). If load fails, default every entry to `LOOP_QUANT_FREE`.

```cpp
  BankType wkTypes[NUM_BANKS];
  uint8_t  wkQuantize[NUM_BANKS];
  uint8_t  wkLoopQuantize[NUM_BANKS];      // <-- ADD
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
      wkTypes[i]         = _banks[i].type;
      wkQuantize[i]      = _nvs ? _nvs->getLoadedQuantizeMode(i) : DEFAULT_ARP_START_MODE;
      wkLoopQuantize[i]  = DEFAULT_LOOP_QUANT_MODE;  // populated from NVS below
  }

  // When reloading from NVS after validateBankTypeStore():
  {
    BankTypeStore bts;
    if (NvsManager::loadBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2,
                             EEPROM_MAGIC, BANKTYPE_VERSION, &bts, sizeof(bts))) {
        validateBankTypeStore(bts);
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
            wkTypes[i]         = (BankType)bts.types[i];
            wkQuantize[i]      = bts.quantize[i];
            wkLoopQuantize[i]  = bts.loopQuantize[i];   // <-- ADD
        }
    }
  }

  // Also add a saved snapshot for revert-on-cancel:
  uint8_t savedLoopQuantize[NUM_BANKS];
  memcpy(savedLoopQuantize, wkLoopQuantize, sizeof(savedLoopQuantize));
```

And in `saveConfig()`, both fields are written to `BankTypeStore`:

```cpp
bool ToolBankConfig::saveConfig(const BankType* types,
                                const uint8_t*  quantize,
                                const uint8_t*  loopQuantize) {   // <-- ADD param
    BankTypeStore bts;
    memset(&bts, 0, sizeof(bts));
    bts.magic   = EEPROM_MAGIC;
    bts.version = BANKTYPE_VERSION;  // = 2 per Phase 1
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
        bts.types[i]         = (uint8_t)types[i];
        bts.quantize[i]      = quantize[i];
        bts.loopQuantize[i]  = loopQuantize[i];
    }
    return NvsManager::saveBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2,
                                 &bts, sizeof(bts));
}
```

Update the header declaration accordingly: `bool saveConfig(const BankType*, const uint8_t*, const uint8_t*);`

All existing call sites of `saveConfig()` inside `run()` must pass the new argument. There are currently two of them (around lines 158 and 271 of the existing `ToolBankConfig.cpp`) — both called after a savepoint swap, and both followed by a `memcpy(savedQuantize, wkQuantize, ...)`. Both must become:

```cpp
        if (saveConfig(wkTypes, wkQuantize, wkLoopQuantize)) {
          memcpy(savedTypes,        wkTypes,        sizeof(savedTypes));
          memcpy(savedQuantize,     wkQuantize,     sizeof(savedQuantize));
          memcpy(savedLoopQuantize, wkLoopQuantize, sizeof(savedLoopQuantize));
```

Similarly the revert-on-cancel path must restore `wkLoopQuantize[cursor]` from `savedLoopQuantize[cursor]`.

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
              // Reset the appropriate quantize field when changing type
              if (next == BANK_ARPEG) {
                  wkQuantize[cursor] = ARP_START_IMMEDIATE;
              } else if (next == BANK_LOOP) {
                  wkLoopQuantize[cursor] = LOOP_QUANT_FREE;
              }
              errorShown = false;
          } else if (!allowed) {
              errorShown = true;
              errorTime = millis();
          }
          screenDirty = true;
      }

      // --- Quantize axis: Up/Down (dispatches to quantize[] or loopQuantize[]) ---
      else if (ev.type == NAV_UP || ev.type == NAV_DOWN) {
          int8_t dir = (ev.type == NAV_DOWN) ? 1 : -1;
          if (wkTypes[cursor] == BANK_ARPEG) {
              int8_t q = (int8_t)wkQuantize[cursor] + dir;
              if (q >= 0 && q < NUM_ARP_START_MODES) {
                  wkQuantize[cursor] = (uint8_t)q;
              }
              screenDirty = true;
          } else if (wkTypes[cursor] == BANK_LOOP) {
              int8_t q = (int8_t)wkLoopQuantize[cursor] + dir;
              if (q >= 0 && q < NUM_LOOP_QUANT_MODES) {
                  wkLoopQuantize[cursor] = (uint8_t)q;
              }
              screenDirty = true;
          }
          // NORMAL: Up/Down ignored
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
                  // Reset the appropriate quantize field on type change
                  if (potType == BANK_ARPEG) {
                      wkQuantize[cursor] = ARP_START_IMMEDIATE;
                  } else if (potType == BANK_LOOP) {
                      wkLoopQuantize[cursor] = LOOP_QUANT_FREE;
                  }
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

**Bank line** (lines ~305-344): Replace `bool isArpeg` with 3-way. The quantize mode is read from `wkQuantize[]` (ARPEG) or `wkLoopQuantize[]` (LOOP) and displayed via **two distinct name tables**: ARPEG uses `QUANTIZE_NAMES[]` ("Immediate"/"Beat"/"Bar"), LOOP uses a new `LOOP_QUANTIZE_NAMES[]` ("Free"/"Beat"/"Bar"). The "Free" terminology is player-facing, matching the visual manual and the engine's `LOOP_QUANT_FREE` enum.

Add the LOOP name table alongside the existing ARPEG one:

```cpp
// ToolBankConfig.cpp, near the existing QUANTIZE_NAMES definition:
static const char* LOOP_QUANTIZE_NAMES[NUM_LOOP_QUANT_MODES] = {
    "Free",   // LOOP_QUANT_FREE  — no snap, instant transitions
    "Beat",   // LOOP_QUANT_BEAT  — snap to next beat (24 ticks)
    "Bar"     // LOOP_QUANT_BAR   — snap to next bar  (96 ticks)
};
```

Then in the bank line rendering:

```cpp
      const char* typeName;
      const char* typeColor;
      const char* qname = nullptr;
      if (wkTypes[i] == BANK_ARPEG) {
          typeName = "ARPEG";  typeColor = VT_CYAN;
          qname = QUANTIZE_NAMES[wkQuantize[i]];
      } else if (wkTypes[i] == BANK_LOOP) {
          typeName = "LOOP";   typeColor = VT_MAGENTA;
          qname = LOOP_QUANTIZE_NAMES[wkLoopQuantize[i]];
      } else {
          typeName = "NORMAL"; typeColor = "";
      }

      if (isEditing) {
          pos += snprintf(line + pos, sizeof(line) - pos,
                          "%s" VT_BOLD "[%s", typeColor, typeName);
          if (qname)
              pos += snprintf(line + pos, sizeof(line) - pos, " - %s", qname);
          pos += snprintf(line + pos, sizeof(line) - pos, "]" VT_RESET);
      } else {
          pos += snprintf(line + pos, sizeof(line) - pos,
                          selected ? "%s%s" VT_RESET "  " : "%s  ",
                          selected ? typeColor : "", typeName);
          if (qname)
              pos += snprintf(line + pos, sizeof(line) - pos,
                              "    Quantize: %s", qname);
      }
```

**Why two distinct tables**: ARPEG keeps "Immediate" because that terminology is established in the arp UI (and matches `ARP_START_IMMEDIATE`). LOOP uses "Free" because the musical feel is different — a FREE loop is not "immediate fire, then nothing special", it's "the whole session is free-feel, no snap anywhere". The word "Free" better communicates this to the player.

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

## Step 2 — ToolPadRoles: refactor to b1 contextual architecture + LOOP roles

> **DESIGN REF**: see `docs/superpowers/specs/2026-04-06-loop-slot-drive-design.md` §1.4 (architecture des rôles), §2.6 (modifications composants existants). The Tool 3 refactor toward a contextual b1 architecture is a **prerequisite** for the LOOP slot drive (Phase 6) but is implemented here in Phase 3 because it costs no more effort than adding the LOOP control roles to a flat-list Tool 3 — and avoids a second refactor pass later.

### Step 2-pre — Architecture refactor (b1 contextual roles)

The current Tool 3 is a flat list of roles ; one pad → one role across all bank types. The new architecture splits roles into **3 contextual sub-pages** (Banks / Arpeg / Loop), with collision rules:

- **Bank pads** ⊥ all (forbidden in any context)
- **Arpeg roles** ⊥ Arpeg roles (forbidden inside Arpeg sub-page)
- **Loop roles** ⊥ Loop roles (forbidden inside Loop sub-page)
- **Arpeg roles** ⊥ **Loop roles** = **ALLOWED** (a single physical pad may be Root C in Arpeg AND Loop Rec in Loop, since both contexts are mutually exclusive at runtime)

The 3 sub-pages share the same 4×12 grid view of the 48 physical pads. Switching sub-page is a `[t]` keyboard toggle (or `[Tab]`). The header shows which context is active.

Phase 3 adds: ARPEG playstop role (separate from LOOP playstop), 3 LOOP control roles (rec, playstop, clear), and the 3-context infrastructure. The 16 LOOP slot pads are deferred to Phase 6.

#### Step 2-pre-1 — Add `BankType context` parameter to `getRoleForPad`

The current `getRoleForPad(pad)` returns the unique role of a pad. The new signature is `getRoleForPad(pad, BankType context)` and only inspects roles relevant to the given context (plus the global bank pads which are visible in all contexts).

In `src/setup/ToolPadRoles.h`, replace line 92:

```cpp
PadRole getRoleForPad(uint8_t pad, BankType context) const;
```

In `src/setup/ToolPadRoles.cpp`, rewrite `getRoleForPad`:

```cpp
PadRole ToolPadRoles::getRoleForPad(uint8_t pad, BankType context) const {
  if (pad >= NUM_KEYS) return {0, 0};

  // === Globals (visible in all contexts) ===
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (_wkBankPads[i] == pad) return {1, i};
  }

  // === Arpeg context ===
  if (context == BANK_ARPEG) {
    for (uint8_t i = 0; i < 7; i++) {
      if (_wkRootPads[i] == pad) return {2, i};
    }
    for (uint8_t i = 0; i < 7; i++) {
      if (_wkModePads[i] == pad) return {3, i};
    }
    if (_wkChromPad == pad) return {3, 7};
    for (uint8_t i = 0; i < 4; i++) {
      if (_wkOctavePads[i] == pad) return {4, i};
    }
    if (_wkHoldPad == pad)         return {5, 0};
    if (_wkArpPlayStopPad == pad)  return {6, 0};   // ARPEG playstop (renamed from generic playstop)
  }

  // === Loop context ===
  if (context == BANK_LOOP) {
    if (_wkLoopRecPad == pad)      return {7, 0};
    if (_wkLoopPlayStopPad == pad) return {8, 0};
    if (_wkLoopClearPad == pad)    return {9, 0};
    // Phase 6: ROLE_LOOP_SLOT_0..15 added here
  }

  return {0, 0};
}
```

**Note**: the existing `_wkPlayStopPad` is renamed to `_wkArpPlayStopPad` to distinguish it from the new `_wkLoopPlayStopPad`. All references in the file must be updated. The renamed member is added to the `_wk*` arrays section in 2b below.

#### Step 2-pre-2 — Update all callers of `getRoleForPad`

The current callers in `ToolPadRoles.cpp` pass no context. They must now pass the **active sub-page context**. There are 3 main call sites:

1. **`drawInfoPanel()` line ~529** — uses the grid cursor position to fetch a role. Must pass the current sub-page context:
   ```cpp
   uint8_t pad = _gridRow * 12 + _gridCol;
   BankType ctx = currentContext();   // returns BANK_NORMAL/ARPEG/LOOP based on _activeSubPage
   PadRole role = getRoleForPad(pad, ctx);
   ```

2. **`printPadDescription()` line ~491** — same fix:
   ```cpp
   void ToolPadRoles::printPadDescription(uint8_t pad) {
     PadRole role = getRoleForPad(pad, currentContext());
     // ... rest unchanged
   }
   ```

3. **Any other reference** — search the file for `getRoleForPad` and add the `currentContext()` argument.

The new helper `BankType currentContext() const` returns:
- `BANK_NORMAL` if `_activeSubPage == 0` (Banks sub-page — globals only, no contextual roles)
- `BANK_ARPEG` if `_activeSubPage == 1`
- `BANK_LOOP` if `_activeSubPage == 2`

Add to the private section of `ToolPadRoles.h`:

```cpp
  BankType currentContext() const;
  uint8_t  _activeSubPage = 0;   // 0=Banks, 1=Arpeg, 2=Loop
```

And the implementation in `ToolPadRoles.cpp` (near the top, after constructor):

```cpp
BankType ToolPadRoles::currentContext() const {
  switch (_activeSubPage) {
    case 1: return BANK_ARPEG;
    case 2: return BANK_LOOP;
    default: return BANK_NORMAL;
  }
}
```

#### Step 2-pre-3 — Restructure `buildRoleMap` for context-aware rendering

The current `buildRoleMap` paints every assigned pad regardless of context. The new version paints only the roles **relevant to the active sub-page** plus the always-visible bank pads.

Replace the body:

```cpp
void ToolPadRoles::buildRoleMap() {
  memset(_roleMap, ROLE_NONE, NUM_KEYS);
  for (int i = 0; i < NUM_KEYS; i++) {
    memcpy(_roleLabels[i], " -- ", 5);
  }

  auto setRole = [&](uint8_t pad, uint8_t role, const char* label) {
    if (pad >= NUM_KEYS) return;
    if (_roleMap[pad] != ROLE_NONE) {
      _roleMap[pad] = ROLE_COLLISION;
      snprintf(_roleLabels[pad], 6, " !! ");
    } else {
      _roleMap[pad] = role;
      snprintf(_roleLabels[pad], 6, "%s", label);
    }
  };

  // === Globals (always shown) ===
  for (int i = 0; i < NUM_BANKS; i++)
    setRole(_wkBankPads[i], ROLE_BANK, GRID_BANK_LABELS[i]);

  // === Arpeg context ===
  if (_activeSubPage == 1) {
    for (int i = 0; i < 7; i++)
      setRole(_wkRootPads[i], ROLE_ROOT, GRID_ROOT_LABELS[i]);
    for (int i = 0; i < 7; i++)
      setRole(_wkModePads[i], ROLE_MODE, GRID_MODE_LABELS[i]);
    setRole(_wkChromPad, ROLE_MODE, GRID_MODE_LABELS[7]);
    setRole(_wkHoldPad, ROLE_HOLD, GRID_HOLD_LABELS[0]);
    setRole(_wkArpPlayStopPad, ROLE_ARPEG_PLAYSTOP, GRID_ARPEG_PS_LABELS[0]);
    for (int i = 0; i < 4; i++)
      setRole(_wkOctavePads[i], ROLE_OCTAVE, GRID_OCTAVE_LABELS[i]);
  }

  // === Loop context ===
  if (_activeSubPage == 2) {
    setRole(_wkLoopRecPad,      ROLE_LOOP_REC, GRID_LOOP_REC_LABELS[0]);
    setRole(_wkLoopPlayStopPad, ROLE_LOOP_PS,  GRID_LOOP_PS_LABELS[0]);
    setRole(_wkLoopClearPad,    ROLE_LOOP_CLR, GRID_LOOP_CLR_LABELS[0]);
    // Phase 6: setRole for ROLE_LOOP_SLOT_0..15 added here
  }

  // === Banks sub-page (active sub-page 0): only the bank pads above are shown ===
}
```

**Why bank pads are always painted**: in the Banks sub-page they are the only thing shown (and the user edits them). In the Arpeg/Loop sub-pages they are still shown (in their bank color) so the user knows which pads are reserved. Collisions of bank pads with arpeg/loop roles are still detected by the `setRole` lambda — they would set `ROLE_COLLISION`, but since bank pads are written first, they always "win" and any subsequent attempt to assign a bank pad as something else triggers the collision marker.

#### Step 2-pre-4 — Restructure `assignRole` and `clearRole` for contextual writes

The new `assignRole(pad, line, index)` is called from the pool selector. It already knows which line (1-9) it is writing to, so the context is implicit in the line number. **No signature change needed** — the function just writes to the right `_wk*` field.

But `clearRole(pad)` currently scans **all** `_wk*` arrays. The new version only scans the arrays relevant to the **active sub-page** (so clearing a pad in the Loop sub-page does NOT also wipe its Arpeg role on the same pad).

```cpp
void ToolPadRoles::clearRole(uint8_t pad) {
  if (pad >= NUM_KEYS) return;

  // Globals: a bank pad can be cleared from any sub-page (it is global)
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (_wkBankPads[i] == pad) _wkBankPads[i] = 0xFF;
  }

  // Contextual: only clear in the active sub-page
  if (_activeSubPage == 1) {
    for (uint8_t i = 0; i < 7; i++) {
      if (_wkRootPads[i] == pad) _wkRootPads[i] = 0xFF;
    }
    for (uint8_t i = 0; i < 7; i++) {
      if (_wkModePads[i] == pad) _wkModePads[i] = 0xFF;
    }
    if (_wkChromPad == pad)        _wkChromPad        = 0xFF;
    if (_wkHoldPad == pad)         _wkHoldPad         = 0xFF;
    if (_wkArpPlayStopPad == pad)  _wkArpPlayStopPad  = 0xFF;
    for (uint8_t i = 0; i < 4; i++) {
      if (_wkOctavePads[i] == pad) _wkOctavePads[i] = 0xFF;
    }
  }

  if (_activeSubPage == 2) {
    if (_wkLoopRecPad == pad)      _wkLoopRecPad      = 0xFF;
    if (_wkLoopPlayStopPad == pad) _wkLoopPlayStopPad = 0xFF;
    if (_wkLoopClearPad == pad)    _wkLoopClearPad    = 0xFF;
  }
}
```

#### Step 2-pre-5 — Add sub-page navigation key (`[t]` toggle)

In `ToolPadRoles::run()`, the keyboard input loop must handle a new key `t` that cycles `_activeSubPage` 0 → 1 → 2 → 0. Find the `if (ev.type == NAV_CHAR && ev.ch == 'q')` block and add:

```cpp
      else if (ev.type == NAV_CHAR && ev.ch == 't') {
        _activeSubPage = (_activeSubPage + 1) % 3;
        // Reset pool cursor to "clear" line — the new sub-page has different lines
        _poolLine = 0;
        _poolIdx  = 0;
        _editing  = false;
        screenDirty = true;
      }
```

The control bar must also advertise `[t] CTX` for visibility. Update `drawControlBar()`:

```cpp
  if (_editing) {
    _ui->drawControlBar(VT_DIM "[^v<>] browse  [t] CTX  [P1] scroll  [RET] assign  [q] cancel" VT_RESET);
  } else {
    _ui->drawControlBar(VT_DIM "[^v<>] NAV  [t] CTX  [P1] scroll  [RET] EDIT  [TOUCH] JUMP  [d] DFLT  [r] CLEAR  [q] EXIT" VT_RESET);
  }
```

#### Step 2-pre-6 — Adapt `drawPool()` to show only the active sub-page lines

The current `drawPool()` always draws 6 category lines + clear. The new version draws different lines based on `_activeSubPage`:

- **Sub-page 0 (Banks)**: only the Bank line (and clear)
- **Sub-page 1 (Arpeg)**: Root / Mode / Octave / Hold / Arpeg P/S lines (5 lines + clear)
- **Sub-page 2 (Loop)**: Loop Rec / Loop P/S / Loop Clr lines (3 lines + clear) — Phase 6 will append the 16 slot lines

Replace the body of `drawPool()`:

```cpp
void ToolPadRoles::drawPool() {
  int selectedPad = _gridRow * 12 + _gridCol;

  auto drawPoolLine = [&](uint8_t lineNum, const char* label,
                          const char* const* labels, uint8_t count,
                          const char* lineColor) {
    // ... existing lambda body unchanged ...
  };

  // === Active sub-page lines ===
  if (_activeSubPage == 0) {
    // Banks sub-page
    drawPoolLine(1, "Bank:", POOL_BANK_LABELS, POOL_BANK_COUNT, VT_BLUE);
  }
  else if (_activeSubPage == 1) {
    // Arpeg sub-page
    drawPoolLine(2, "Root:",      POOL_ROOT_LABELS,     POOL_ROOT_COUNT,     VT_GREEN);
    drawPoolLine(3, "Mode:",      POOL_MODE_LABELS,     POOL_MODE_COUNT,     VT_CYAN);
    drawPoolLine(4, "Octave:",    POOL_OCTAVE_LABELS,   POOL_OCTAVE_COUNT,   VT_YELLOW);
    drawPoolLine(5, "Hold:",      POOL_HOLD_LABELS,     POOL_HOLD_COUNT,     VT_MAGENTA);
    drawPoolLine(6, "Arp P/S:",   POOL_ARPEG_PS_LABELS, POOL_ARPEG_PS_COUNT, VT_BRIGHT_RED);
  }
  else if (_activeSubPage == 2) {
    // Loop sub-page
    drawPoolLine(7, "Loop Rec:",  POOL_LOOP_REC_LABELS, POOL_LOOP_REC_COUNT, VT_BRIGHT_RED);
    drawPoolLine(8, "Loop P/S:",  POOL_LOOP_PS_LABELS,  POOL_LOOP_PS_COUNT,  VT_BRIGHT_RED);
    drawPoolLine(9, "Loop Clr:",  POOL_LOOP_CLR_LABELS, POOL_LOOP_CLR_COUNT, VT_BRIGHT_RED);
    // Phase 6: 16 slot pad lines added here
  }

  // Clear action at the bottom (always visible)
  {
    bool isSelectedLine = _editing && (_poolLine == 0);
    if (isSelectedLine) {
      _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET VT_DIM "[---] clear role" VT_RESET);
    } else {
      _ui->drawFrameLine("  " VT_DIM "[---] clear role" VT_RESET);
    }
  }
}
```

#### Step 2-pre-7 — Adapt header to show active context

In `drawScreen()`, the console header must show the active sub-page:

```cpp
  const char* ctxName;
  switch (_activeSubPage) {
    case 1:  ctxName = "ARPEG ROLES"; break;
    case 2:  ctxName = "LOOP ROLES"; break;
    default: ctxName = "BANKS"; break;
  }
  char headerTitle[48];
  snprintf(headerTitle, sizeof(headerTitle), "TOOL 3: %s", ctxName);
  _ui->drawConsoleHeader(headerTitle, _nvsSaved);
```

#### Step 2-pre-8 — Pool linearization for pot navigation (per sub-page)

The current `POOL_OFFSETS[]` linearizes a single flat pool. With 3 sub-pages, each sub-page has its own pool layout. The pot navigation linearization must use a different table per sub-page.

Replace the static `POOL_OFFSETS[]` and `TOTAL_POOL_ITEMS` declarations with:

```cpp
// Per-sub-page pool linearization
// Sub-page 0 (Banks):  Clear(1) + Bank(8) = 9
// Sub-page 1 (Arpeg):  Clear(1) + Root(7) + Mode(8) + Octave(4) + Hold(1) + ArpPS(1) = 22
// Sub-page 2 (Loop):   Clear(1) + LoopRec(1) + LoopPS(1) + LoopClr(1) = 4
//                      Phase 6 will append 16 slot lines = 20 total

static const uint8_t POOL_OFFSETS_BANKS[]  = {0, 1, 9};                       // total=9
static const uint8_t POOL_OFFSETS_ARPEG[]  = {0, 1, 8, 16, 20, 21, 22};       // total=22
static const uint8_t POOL_OFFSETS_LOOP[]   = {0, 1, 2, 3, 4};                 // total=4 (Phase 3)

static const uint8_t TOTAL_POOL_BANKS = 9;
static const uint8_t TOTAL_POOL_ARPEG = 22;
static const uint8_t TOTAL_POOL_LOOP  = 4;   // Phase 6 → 20

// Returns the offsets table + line count for the active sub-page.
// linesOut: how many lines (sentinel-terminated implies +1 in array size)
static const uint8_t* poolOffsetsForContext(uint8_t subPage,
                                             uint8_t& linesOut,
                                             uint8_t& totalOut) {
  switch (subPage) {
    case 1:
      linesOut = 6;   // 0=clear, 1-5=arpeg category lines (mapped to internal lines 2-6)
      totalOut = TOTAL_POOL_ARPEG;
      return POOL_OFFSETS_ARPEG;
    case 2:
      linesOut = 4;   // 0=clear, 1-3=loop category lines (mapped to internal lines 7-9)
      totalOut = TOTAL_POOL_LOOP;
      return POOL_OFFSETS_LOOP;
    default:
      linesOut = 2;   // 0=clear, 1=bank
      totalOut = TOTAL_POOL_BANKS;
      return POOL_OFFSETS_BANKS;
  }
}
```

The linearization helpers `linearToPool` and `poolToLinear` must take the active sub-page into account. Update the file-scope helpers (and pass `_activeSubPage` from the call sites):

```cpp
static void linearToPool(int32_t linear, uint8_t subPage, uint8_t& line, uint8_t& idx) {
  uint8_t lineCount, total;
  const uint8_t* offsets = poolOffsetsForContext(subPage, lineCount, total);
  for (uint8_t i = 0; i < lineCount; i++) {
    if (linear < offsets[i + 1]) {
      // Map sub-page-local line index back to global pool line number
      // Sub-page 0: 0=clear, 1=bank
      // Sub-page 1: 0=clear, 1=root(2), 2=mode(3), 3=octave(4), 4=hold(5), 5=arpPS(6)
      // Sub-page 2: 0=clear, 1=loopRec(7), 2=loopPS(8), 3=loopClr(9)
      static const uint8_t MAP_BANKS[]  = {0, 1};
      static const uint8_t MAP_ARPEG[]  = {0, 2, 3, 4, 5, 6};
      static const uint8_t MAP_LOOP[]   = {0, 7, 8, 9};
      const uint8_t* map = (subPage == 1) ? MAP_ARPEG :
                           (subPage == 2) ? MAP_LOOP : MAP_BANKS;
      line = map[i];
      idx  = (uint8_t)(linear - offsets[i]);
      return;
    }
  }
  line = 0; idx = 0;
}

static int32_t poolToLinear(uint8_t line, uint8_t idx, uint8_t subPage) {
  uint8_t lineCount, total;
  const uint8_t* offsets = poolOffsetsForContext(subPage, lineCount, total);
  // Reverse-map global line → local index in this sub-page
  uint8_t localIdx = 0xFF;
  if (subPage == 0) {
    if (line == 0) localIdx = 0;
    else if (line == 1) localIdx = 1;
  } else if (subPage == 1) {
    static const uint8_t REV_ARPEG[] = {0, 0xFF, 1, 2, 3, 4, 5};  // global line -> local
    if (line < 7) localIdx = REV_ARPEG[line];
  } else if (subPage == 2) {
    static const uint8_t REV_LOOP[] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3};
    if (line < 10) localIdx = REV_LOOP[line];
  }
  if (localIdx == 0xFF) return 0;
  return offsets[localIdx] + idx;
}
```

All call sites of `linearToPool` and `poolToLinear` (3 in `ToolPadRoles.cpp`) must pass `_activeSubPage` as a new argument.

> **AUDIT NOTE**: this is the most invasive part of the refactor. Search the file for `linearToPool(` and `poolToLinear(` and update each call site. The pot navigation `_potPoolLinear` is now scoped per sub-page — switching sub-page resets it to 0 (already handled in Step 2-pre-5 with `_poolLine = 0; _poolIdx = 0`).

#### Step 2-pre-9 — Collision validation must be context-scoped

In the run() pool selector, when the user presses ENTER on a pool item, the existing code checks if another pad already owns that role and offers a steal. The steal check only fires if the **same role** is already assigned.

With contextual roles, we ALSO need to detect: "the target pad already has a global (bank) role, which is forbidden in any context". The check must scan only the **active context's** roles + the **globals**.

Add a new helper `bool isPadOccupiedInContext(uint8_t pad)`:

```cpp
bool ToolPadRoles::isPadOccupiedInContext(uint8_t pad) const {
  if (pad >= NUM_KEYS) return false;
  // Globals are always blocking
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (_wkBankPads[i] == pad) return true;
  }
  // Contextual: only the active sub-page
  if (_activeSubPage == 1) {
    for (uint8_t i = 0; i < 7; i++) if (_wkRootPads[i] == pad) return true;
    for (uint8_t i = 0; i < 7; i++) if (_wkModePads[i] == pad) return true;
    if (_wkChromPad == pad) return true;
    if (_wkHoldPad == pad) return true;
    if (_wkArpPlayStopPad == pad) return true;
    for (uint8_t i = 0; i < 4; i++) if (_wkOctavePads[i] == pad) return true;
  } else if (_activeSubPage == 2) {
    if (_wkLoopRecPad == pad) return true;
    if (_wkLoopPlayStopPad == pad) return true;
    if (_wkLoopClearPad == pad) return true;
  }
  return false;
}
```

Add the declaration to `ToolPadRoles.h` private section:

```cpp
  bool isPadOccupiedInContext(uint8_t pad) const;
```

This helper is what the steal-confirmation logic should use instead of the current any-collision check. Find the call site in `run()` (around the ENTER handler) and replace any collision check with `isPadOccupiedInContext(targetPad)`. The exact line varies — search for `_confirmSteal = true`.

#### Step 2-pre-10 — `clearAllRoles()` must be context-scoped

The current `clearAllRoles()` wipes everything. The new version wipes only the **active sub-page's** roles. Replace:

```cpp
void ToolPadRoles::clearAllRoles() {
  // Globals: only wipe in Banks sub-page
  if (_activeSubPage == 0) {
    memset(_wkBankPads, 0xFF, sizeof(_wkBankPads));
    return;
  }

  // Arpeg sub-page
  if (_activeSubPage == 1) {
    memset(_wkRootPads, 0xFF, sizeof(_wkRootPads));
    memset(_wkModePads, 0xFF, sizeof(_wkModePads));
    _wkChromPad        = 0xFF;
    _wkHoldPad         = 0xFF;
    _wkArpPlayStopPad  = 0xFF;
    memset(_wkOctavePads, 0xFF, sizeof(_wkOctavePads));
  }

  // Loop sub-page
  if (_activeSubPage == 2) {
    _wkLoopRecPad      = 0xFF;
    _wkLoopPlayStopPad = 0xFF;
    _wkLoopClearPad    = 0xFF;
    // Phase 6: also wipe slot pads
  }
}
```

Update the confirmation prompt in `drawInfoPanel()` to mention the context:
```cpp
  if (_confirmClearAll) {
    const char* ctx = (_activeSubPage == 0) ? "BANKS" :
                       (_activeSubPage == 1) ? "ARPEG" : "LOOP";
    _ui->drawFrameLine(VT_YELLOW "Clear ALL %s roles? (y/n)" VT_RESET, ctx);
    return;
  }
```

#### Step 2-pre-11 — `resetToDefaults()` must populate all 3 contexts

`resetToDefaults()` is unchanged in scope (it always resets everything to factory defaults regardless of sub-page), but it must now also set the new fields:

```cpp
void ToolPadRoles::resetToDefaults() {
  // Globals
  for (uint8_t i = 0; i < NUM_BANKS; i++) _wkBankPads[i] = i;

  // Arpeg context defaults
  for (uint8_t i = 0; i < 7; i++) _wkRootPads[i] = 8 + i;
  for (uint8_t i = 0; i < 7; i++) _wkModePads[i] = 15 + i;
  _wkChromPad        = 22;
  _wkHoldPad         = 23;
  _wkArpPlayStopPad  = 24;
  _wkOctavePads[0] = 25;
  _wkOctavePads[1] = 26;
  _wkOctavePads[2] = 27;
  _wkOctavePads[3] = 28;

  // Loop context defaults — leave unassigned; user must opt in via Tool 3
  _wkLoopRecPad      = 0xFF;
  _wkLoopPlayStopPad = 0xFF;
  _wkLoopClearPad    = 0xFF;
  // Phase 6: slot pads also default to 0xFF
}
```

#### Step 2-pre-12 — `saveAll()` must save the new fields

`saveAll()` already saves `BankPadStore`, `ScalePadStore`, `ArpPadStore`. Add a 4th save block for `LoopPadStore` (which now contains the 3 LOOP control pads + the slot pads array, even though slot pads are still all 0xFF in Phase 3):

```cpp
  // 4. LoopPadStore (Phase 3: only 3 LOOP control pads, slot pads all 0xFF)
  LoopPadStore lps;
  memset(&lps, 0, sizeof(lps));
  lps.magic       = EEPROM_MAGIC;
  lps.version     = LOOPPAD_VERSION;   // = 2 per Phase 1
  lps.recPad      = _wkLoopRecPad;
  lps.playStopPad = _wkLoopPlayStopPad;
  lps.clearPad    = _wkLoopClearPad;
  // slotPads[16] all default 0xFF — Phase 6 will populate them
  memset(lps.slotPads, 0xFF, sizeof(lps.slotPads));
  if (NvsManager::saveBlob(LOOP_PAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY, &lps, sizeof(lps))) {
    *_loopRecPad      = _wkLoopRecPad;
    *_loopPlayStopPad = _wkLoopPlayStopPad;
    *_loopClearPad    = _wkLoopClearPad;
  } else {
    allOk = false;
  }
```

> **AUDIT NOTE — `_nvsSaved` flag**: do NOT include `lpOk` in the boolean conjunction for `_nvsSaved` (to avoid the BUG #3 of the Phase 3 audit). The `_nvsSaved` flag remains `bpOk && spOk && apOk`. Once the LOOP store is successfully written for the first time, future loads will succeed and the badge will reflect reality.

> **AUDIT NOTE — load path**: the `LoopPadStore` load logic in `run()` (entry section, around line ~599) must be added. See sub-step 2m below for the exact code (still applicable, just updated for the new fields).

---

### 2a. Add new role enum values (ToolPadRoles.h, line ~22)

> **CHANGED FROM ORIGINAL PLAN**: the original plan added 3 LOOP roles to a flat enum. The new b1 architecture also requires `ROLE_ARPEG_PLAYSTOP` (separate from a generic playstop), and the `ROLE_PLAYSTOP` value is renamed.

Replace the enum with:

```cpp
enum PadRoleCode : uint8_t {
  ROLE_NONE            = 0,
  ROLE_BANK            = 1,
  ROLE_ROOT            = 2,
  ROLE_MODE            = 3,
  ROLE_OCTAVE          = 4,
  ROLE_HOLD            = 5,
  ROLE_ARPEG_PLAYSTOP  = 6,    // <-- RENAMED from ROLE_PLAYSTOP (was generic)
  ROLE_LOOP_REC        = 7,    // <-- ADD
  ROLE_LOOP_PS         = 8,    // <-- ADD  (LOOP play/stop)
  ROLE_LOOP_CLR        = 9,    // <-- ADD  (LOOP clear)
  // Phase 6: ROLE_LOOP_SLOT_0..15 = 10..25
  ROLE_COLLISION       = 0xFF
};
```

**Audit note**: search the file for any use of the old `ROLE_PLAYSTOP` constant and replace with `ROLE_ARPEG_PLAYSTOP`. There should be ~3 references.

### Original Step 2a (legacy reference, superseded)

The original Step 2a addition remains documented below for traceability but is **superseded by the new enum above**:

```cpp
  ROLE_PLAYSTOP  = 6,
  ROLE_LOOP_REC  = 7,     // <-- ADD
  ROLE_LOOP_PS   = 8,     // <-- ADD  (LOOP play/stop)
  ROLE_LOOP_CLR  = 9,     // <-- ADD  (LOOP clear)
  ROLE_COLLISION = 0xFF
};
```

---

### Transition note for sub-sections 2b through 2t

The remaining sub-sections (2b–2t) describe the addition of fields, members, labels, save/load logic, and pool handlers for the 3 new LOOP control roles. These sub-sections **remain valid in the b1 architecture** but require the following adaptations applied while implementing them:

1. **Rename `_wkPlayStopPad` → `_wkArpPlayStopPad`** everywhere it appears (Step 2-pre-1 already documented this). Apply globally with a search/replace before working through 2b–2t.
2. **Rename `_playStopPad` (live pointer) → `_arpPlayStopPad`** in the same files. The corresponding pointer in `main.cpp` (`s_playStopPad`) is **NOT renamed** at the main.cpp level — only the ToolPadRoles internals change. The `begin()` pointer parameter must be updated accordingly.
3. **Pool linearization changes (Step 2-pre-8)** override the simple POOL_OFFSETS update in original Step 2d. Apply the per-sub-page tables instead of the flat one.
4. **`POOL_LINE_COUNT = 10`** in Step 2e is **superseded** — line counts are per-sub-page now. Delete the `POOL_LINE_COUNT` constant; instead, the active count is fetched via `poolOffsetsForContext(_activeSubPage, lineCount, total)`.
5. **`drawPool()` modifications in Step 2i** are **superseded** — the new `drawPool()` body is in Step 2-pre-6. The original Step 2i `drawPoolLine` calls are subsumed into the sub-page 2 branch of the new function.
6. **`buildRoleMap()` modifications in Step 2h** are **superseded** — the new body is in Step 2-pre-3. The original Step 2h calls are subsumed into the `_activeSubPage == 2` branch.
7. **`saveAll()` LoopPadStore section in Step 2l** is **superseded** — the new save block is in Step 2-pre-12. The original Step 2l snippet is correct in spirit but does not include the `slotPads[16]` field that Phase 1 added to the struct.
8. **`getRoleForPad` modifications in Step 2p** are **superseded** — the new function (taking a `BankType context` parameter) is in Step 2-pre-1.
9. **`clearRole` modifications in Step 2q** are **superseded** — the new function (context-scoped) is in Step 2-pre-4.
10. **`clearAllRoles` modifications in Step 2o** are **superseded** — the new function (context-scoped) is in Step 2-pre-10.

**Sub-sections 2b, 2c, 2f, 2g, 2j, 2k, 2m, 2n, 2r, 2s, 2t remain unchanged** (they add `_wk*` members, labels, helper switches, save logic, NVS load logic, default reset, getRoleForPad case branches, clearRole case branches, poolItemLabel case branches, printRoleDescription case branches, and run() live-to-wk copy — all of which are still needed in the b1 architecture, just integrated into the new context-aware structure).

---

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
      _ui->drawFrameLine(VT_DIM "Tap to toggle playback. Supports quantized start (Free/Beat/Bar)." VT_RESET);
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

**Signature change** — extend `NvsManager::loadAll()` with 3 new reference parameters for the LOOP control pads:

```cpp
// NvsManager.h
void loadAll(BankSlot* banks, uint8_t& currentBank, uint8_t* padOrder,
             uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
             uint8_t& chromaticPad, uint8_t& holdPad,
             uint8_t& playStopPad, uint8_t* octavePads,
             PotRouter& potRouter, SettingsStore& outSettings,
             uint8_t& recPad, uint8_t& loopPlayPad, uint8_t& clearPad);  // <-- ADD
```

**Implementation** (in `NvsManager.cpp`, after the `ArpPadStore` load block):

```cpp
  // LoopPadStore — pad assignments for LOOP control (rec, play/stop, clear)
  // Phase 6 will extend this to also load slotPads[16]
  {
    LoopPadStore lps;
    if (loadBlob(LOOP_PAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY,
                 EEPROM_MAGIC, LOOPPAD_VERSION, &lps, sizeof(lps))) {
      validateLoopPadStore(lps);
      recPad      = lps.recPad;
      loopPlayPad = lps.playStopPad;
      clearPad    = lps.clearPad;
      // slotPads[16] ignored in Phase 3 (Phase 6 will populate the s_loopSlotPads array)
    }
    // No else: defaults (0xFF) already in place from main.cpp init
  }
```

**Caller update** in `main.cpp setup()` — find the existing `s_nvsManager.loadAll(...)` call and add the 3 new arguments:

```cpp
  s_nvsManager.loadAll(s_banks, currentBank, s_padOrder, bankPads,
                        rootPads, modePads, chromaticPad, holdPad,
                        s_playStopPad, octavePads, s_potRouter, s_settings,
                        s_recPad, s_loopPlayPad, s_clearPad);   // <-- ADD
```

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
   - On ARPEG: press Down → Immediate → Beat → Bar
   - On LOOP:  press Down → Free → Beat → Bar
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
| `src/setup/ToolPadRoles.h` | **b1 contextual refactor**: PadRoleCode enum (ROLE_ARPEG_PLAYSTOP rename + ROLE_LOOP_REC/PS/CLR add), `_activeSubPage` member, `currentContext()` helper, `getRoleForPad(pad, BankType context)` signature, `isPadOccupiedInContext()` helper, `_wkArpPlayStopPad` rename, `_wkLoopRec/PS/Clr` arrays, `_loopRec/PS/Clr` pointers, **POOL_LINE_COUNT removed** (now per-sub-page) |
| `src/setup/ToolPadRoles.cpp` | **b1 contextual refactor**: `currentContext()` impl, per-sub-page POOL_OFFSETS_BANKS/ARPEG/LOOP tables, `linearToPool(linear, subPage, line, idx)` and `poolToLinear(line, idx, subPage)` helpers, `[t]` toggle key, context-aware `buildRoleMap` / `drawPool` / `clearRole` / `clearAllRoles` / `resetToDefaults` / `saveAll` (with LoopPadStore), `printRoleDescription` cases, `printPadDescription` context arg, header sub-page label, run() live-to-wk copy for LOOP fields |
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
