# Unified Setup UX — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor all 6 setup tools to share a unified arrow-key navigation model with immediate save-on-edit and consistent UX.

**Architecture:** New InputParser module handles VT100 escape sequence parsing and key mapping. Each tool is refactored individually to use InputParser and follow the unified navigation conventions (Up/Down navigate, Enter toggles edit, Left/Right cycle values, immediate NVS save on edit exit). Rear button removed from setup mode.

**Tech Stack:** C++17, Arduino framework, ESP32-S3, PlatformIO, VT100/iTerm2 serial terminal

**Spec:** `docs/superpowers/specs/2026-03-23-unified-setup-ux-design.md`

**Build command:** `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
**Upload command:** `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload`

---

## File Structure

**New files:**
| File | Responsibility |
|---|---|
| `src/setup/InputParser.h` | NavType enum, NavEvent struct, InputParser class declaration |
| `src/setup/InputParser.cpp` | VT100 escape parsing, acceleration detection, char passthrough |

**Modified files (major):**
| File | Changes |
|---|---|
| `src/setup/ToolSettings.cpp` | Replace char-based input with InputParser, Up/Down nav, Enter edit toggle, immediate save per param |
| `src/setup/ToolBankConfig.cpp` | Replace char-based input with InputParser, Up/Down nav, sub-param (quantize) edit |
| `src/setup/ToolPotMapping.cpp` | Replace Enter context-switch with `t`, pool navigation with arrows, auto-steal confirmation, CC# two-step |
| `src/setup/ToolPadRoles.cpp` | Complete rewrite: grid navigation + 3-line pool + auto-steal |

**Modified files (minor):**
| File | Changes |
|---|---|
| `src/setup/SetupUI.h` | Remove `_btnLast`, make `readInput()` serial-only (remove button logic) — final removal in Task 7 |
| `src/setup/SetupUI.cpp` | Strip button logic from `readInput()` and `begin()` — keep method until all tools migrated |
| `src/setup/SetupManager.cpp` | Use InputParser for menu input, remove button wait/debounce |
| `src/setup/ToolSettings.h` | Add `LedController*` member for LED updates in blocking loops |
| `src/setup/ToolPadOrdering.cpp` | Use InputParser, add `d` defaults |
| `src/setup/ToolCalibration.cpp` | Use InputParser for quit |

**Unchanged:**
- `src/setup/SetupCommon.h` — touch detection helpers
- `src/setup/ToolBankConfig.h` — class interface stays the same

---

## Task 1: InputParser Module

**Files:**
- Create: `src/setup/InputParser.h`
- Create: `src/setup/InputParser.cpp`

- [ ] **Step 1: Create InputParser.h**

```cpp
#ifndef INPUT_PARSER_H
#define INPUT_PARSER_H

#include <stdint.h>

enum NavType : uint8_t {
  NAV_NONE = 0,
  NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT,
  NAV_ENTER,
  NAV_QUIT,        // 'q'
  NAV_DEFAULTS,    // 'd'
  NAV_TOGGLE,      // 't'
  NAV_CHAR,        // all other chars — raw char in NavEvent.ch
};

struct NavEvent {
  NavType type;
  bool    accelerated;  // true if rapid LEFT/RIGHT repeat < 120ms
  char    ch;           // raw character (valid when type == NAV_CHAR)
};

class InputParser {
public:
  InputParser();

  // Call every loop iteration. Returns parsed event or NAV_NONE.
  NavEvent update();

private:
  // Escape sequence state machine
  enum EscState : uint8_t { ESC_IDLE, ESC_GOT_ESC, ESC_GOT_BRACKET };
  EscState _escState;
  unsigned long _escTime;       // millis() when ESC received

  // Acceleration detection
  NavType  _lastArrowType;
  unsigned long _lastArrowTime;

  // Enter debounce (\r\n sends two bytes — ignore second within 50ms)
  unsigned long _lastEnterTime;

  static const unsigned long ESC_TIMEOUT_MS = 50;
  static const unsigned long ACCEL_WINDOW_MS = 120;
  static const unsigned long ENTER_DEBOUNCE_MS = 50;
};

#endif
```

- [ ] **Step 2: Create InputParser.cpp**

```cpp
#include "InputParser.h"
#include <Arduino.h>

InputParser::InputParser()
  : _escState(ESC_IDLE)
  , _escTime(0)
  , _lastArrowType(NAV_NONE)
  , _lastArrowTime(0)
  , _lastEnterTime(0)
{}

NavEvent InputParser::update() {
  NavEvent ev = { NAV_NONE, false, 0 };
  unsigned long now = millis();

  // Check escape timeout (ESC received but no '[' within 50ms)
  if (_escState == ESC_GOT_ESC && (now - _escTime) >= ESC_TIMEOUT_MS) {
    _escState = ESC_IDLE;
    // Discard stale ESC — don't return anything
  }
  if (_escState == ESC_GOT_BRACKET && (now - _escTime) >= ESC_TIMEOUT_MS) {
    _escState = ESC_IDLE;
  }

  if (!Serial.available()) return ev;

  char c = (char)Serial.read();

  // Escape sequence state machine
  switch (_escState) {
    case ESC_IDLE:
      if (c == '\033') {
        _escState = ESC_GOT_ESC;
        _escTime = now;
        return ev;  // Wait for next byte
      }
      break;

    case ESC_GOT_ESC:
      if (c == '[') {
        _escState = ESC_GOT_BRACKET;
        return ev;  // Wait for direction byte
      }
      // Not a valid sequence — discard ESC, process char normally
      _escState = ESC_IDLE;
      break;

    case ESC_GOT_BRACKET:
      _escState = ESC_IDLE;
      switch (c) {
        case 'A': ev.type = NAV_UP;    break;
        case 'B': ev.type = NAV_DOWN;  break;
        case 'C': ev.type = NAV_RIGHT; break;
        case 'D': ev.type = NAV_LEFT;  break;
        default: return ev;  // Unknown sequence, ignore
      }
      // Acceleration detection for LEFT/RIGHT
      if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
        if (ev.type == _lastArrowType && (now - _lastArrowTime) < ACCEL_WINDOW_MS) {
          ev.accelerated = true;
        }
        _lastArrowType = ev.type;
        _lastArrowTime = now;
      }
      return ev;
  }

  // Normal character mapping (only reached from ESC_IDLE)
  switch (c) {
    case '\r':
    case '\n':
      // Debounce: terminals may send \r\n — ignore second within 50ms
      if ((now - _lastEnterTime) < ENTER_DEBOUNCE_MS) return ev;
      _lastEnterTime = now;
      ev.type = NAV_ENTER;
      break;
    case 'q':   ev.type = NAV_QUIT;     break;
    case 'd':   ev.type = NAV_DEFAULTS; break;
    case 't':   ev.type = NAV_TOGGLE;   break;
    default:
      ev.type = NAV_CHAR;
      ev.ch = c;
      break;
  }
  return ev;
}
```

- [ ] **Step 3: Build to verify compilation**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS (InputParser is self-contained, no external dependencies beyond Arduino.h)

Note: InputParser.cpp won't be linked yet since nothing includes it. Create a temporary `#include` in SetupManager.cpp to force compilation:
```cpp
#include "InputParser.h"  // temporary — remove after Task 2
```

- [ ] **Step 4: Commit**

```bash
git add src/setup/InputParser.h src/setup/InputParser.cpp
git commit -m "Add InputParser: VT100 arrow key parsing with acceleration detection"
```

---

## Task 2: Strip Rear Button from Setup + Wire InputParser into SetupManager

**Files:**
- Modify: `src/setup/SetupUI.h` (line 92: remove `_btnLast`)
- Modify: `src/setup/SetupUI.cpp` (lines 13-16: remove button init; lines 257-265: simplify `readInput()` to serial-only)
- Modify: `src/setup/SetupManager.cpp` (lines 40-121)

**Important:** Do NOT remove `readInput()` yet — unrefactored tools (1-4, 6) still call it. Only strip the button logic. Full removal happens in Task 7 after all tools are migrated.

- [ ] **Step 1: Strip button logic from SetupUI (keep readInput serial-only)**

In `SetupUI.h`: remove `_btnLast` member (line 92). Keep `readInput()` declaration.

In `SetupUI.cpp`:
- Remove button init from `begin()` (line 15: `_btnLast = digitalRead(BTN_REAR_PIN);`)
- Simplify `readInput()` body to serial-only:
```cpp
char SetupUI::readInput() {
  if (Serial.available()) return (char)Serial.read();
  return 0;
}
```

- [ ] **Step 2: Update SetupManager to use InputParser**

In `SetupManager.cpp`:
- Add `#include "InputParser.h"` at top
- Add `static InputParser s_input;` before `run()`
- In `run()` main loop: replace `char input = _ui.readInput();` with:
```cpp
NavEvent ev = s_input.update();
char input = 0;
if (ev.type == NAV_CHAR) input = ev.ch;
else if (ev.type == NAV_QUIT) input = '0';  // q = reboot from main menu
```
- Remove the button-release wait loop at the top of `run()` (lines 48-52: `while (digitalRead(BTN_REAR_PIN) == LOW)` loop). Replace with a simple `delay(200);` debounce.

- [ ] **Step 3: Build and verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. All tools still compile (they call `readInput()` which still exists, just serial-only now).

- [ ] **Step 4: Flash and verify on hardware**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload`
- Hold rear button 3s at boot → setup mode
- Verify: number keys 1-6 still launch tools (old tools work with serial-only readInput)
- Verify: `q` triggers reboot
- Verify: rear button press does nothing in setup mode

- [ ] **Step 5: Commit**

```bash
git add src/setup/SetupUI.h src/setup/SetupUI.cpp src/setup/SetupManager.cpp
git commit -m "Strip rear button from setup mode, wire InputParser into SetupManager"
```

---

## Task 3: Refactor Tool 5 — Settings

**Files:**
- Modify: `src/setup/ToolSettings.h` (add `LedController*` member)
- Modify: `src/setup/ToolSettings.cpp` (full rewrite of run() method, ~250 lines)

This is the simplest tool — validates the unified pattern before touching others.

- [ ] **Step 1: Add LedController pointer to ToolSettings**

In `ToolSettings.h`: add `LedController* _leds;` to private members. Update `begin()` signature to accept it.

In `ToolSettings.cpp` `begin()`: store the pointer. Update the call in `SetupManager.cpp` to pass `_leds`.

- [ ] **Step 2: Rewrite ToolSettings::run() input loop**

Replace the entire input handling section. The new structure:

```cpp
// State
InputParser input;
uint8_t cursor = 0;        // 0-7: which param is selected
bool editing = false;       // true = Left/Right modify value
bool screenDirty = true;
SettingsStore wk = /* load from NVS as before */;

// Main loop
while (true) {
  _leds->update();
  NavEvent ev = input.update();

  if (ev.type == NAV_QUIT) break;

  if (ev.type == NAV_DEFAULTS && !editing) {
    // Show "Reset to defaults? (y/n)" — see Step 3
    screenDirty = true;
    continue;
  }

  if (!editing) {
    // Navigation mode
    if (ev.type == NAV_UP && cursor > 0) { cursor--; screenDirty = true; }
    if (ev.type == NAV_DOWN && cursor < 7) { cursor++; screenDirty = true; }
    if (ev.type == NAV_ENTER) { editing = true; screenDirty = true; }
  } else {
    // Edit mode
    if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
      int step = ev.accelerated ? accelStep(cursor) : 1;
      if (ev.type == NAV_LEFT) step = -step;
      adjustParam(wk, cursor, step);
      screenDirty = true;
    }
    if (ev.type == NAV_ENTER) {
      // Exit edit → save to NVS
      if (saveSettings(wk)) {
        _ui->showSaved();
        editing = false;
      } else {
        // NVS failure: show error, STAY in edit mode (user can retry Enter)
        Serial.printf(VT_RED "  NVS write failed!" VT_RESET);
        delay(1500);
      }
      screenDirty = true;
    }
  }

  // Display (unchanged layout, add [brackets] around edited value)
  if (screenDirty || (millis() - lastRefresh >= 500)) {
    drawSettingsScreen(wk, cursor, editing);
    lastRefresh = millis();
    screenDirty = false;
  }
  delay(5);
}
```

Key helper functions to extract:
- `adjustParam(SettingsStore& wk, uint8_t param, int step)` — applies step to the selected param with clamping
- `accelStep(uint8_t param)` — returns acceleration factor per param:
  - AT Rate (10-100, range 90): x5
  - Double-Tap (100-250, range 150): x5
  - Bargraph Duration (1000-10000, range 9000): x10
  - All discrete params (profile, BLE, clock, follow, panic): ignored (step always 1)
- `saveSettings(const SettingsStore& wk)` — NVS write, returns `bool` success (extract from current save block, lines 113-151)
- `drawSettingsScreen(...)` — extract current display code (lines 160-245), add bracket notation for editing state

- [ ] **Step 3: Implement defaults confirmation flow**

When `d` is pressed (only outside edit mode):
```cpp
if (ev.type == NAV_DEFAULTS && !editing) {
  Serial.print("\r\n  Reset to defaults? (y/n) ");
  while (true) {
    _leds->update();
    NavEvent confirm = input.update();
    if (confirm.type == NAV_CHAR && confirm.ch == 'y') {
      wk = getDefaultSettings();  // factory defaults
      if (saveSettings(wk)) _ui->showSaved();
      break;
    }
    if (confirm.type != NAV_NONE) break;  // any other key = cancel
    delay(5);
  }
  screenDirty = true;
}
```

- [ ] **Step 3: Update display to show edit state**

The selected param line changes based on state:
- **Not editing:** `> 3. BLE Interval          15 ms` (cyan `>` prefix)
- **Editing:** `> 3. BLE Interval         [15 ms]` (value in brackets, brighter)

Status line at bottom:
- **Not editing:** `[Up/Down] navigate  [Enter] edit  [d] defaults  [q] quit`
- **Editing:** `[Left/Right] change value  [Enter] confirm & save`

- [ ] **Step 4: Build and verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS

- [ ] **Step 5: Flash and test on hardware**

Test checklist:
- Up/Down moves cursor through 8 params
- Enter enters edit mode (brackets appear)
- Left/Right cycles value
- Enter again exits edit + saves (LED blink)
- Holding Left/Right accelerates (x5 or x10 depending on param)
- `d` shows confirmation, `y` resets to defaults
- `q` returns to main menu
- Info/help text updates per selected param
- Left/Right ignored when not editing

- [ ] **Step 6: Commit**

```bash
git add src/setup/ToolSettings.cpp
git commit -m "Refactor Tool 5 Settings: unified arrow navigation + immediate save"
```

---

## Task 4: Refactor Tool 4 — Bank Config

**Files:**
- Modify: `src/setup/ToolBankConfig.cpp` (full rewrite of run() method, ~174 lines)

- [ ] **Step 1: Rewrite ToolBankConfig::run() input loop**

New state model:
```cpp
InputParser input;
uint8_t cursor = 0;         // 0-7: which bank
bool editing = false;
uint8_t editField = 0;      // 0 = type, 1 = quantize (only when ARPEG)
BankType wkTypes[8];         // working copy
uint8_t wkQuantize[8];       // working copy
```

Navigation:
- Up/Down: move cursor between 8 banks (not editing) or between type/quantize sub-fields (editing, ARPEG only)
- Enter: toggle edit mode
- Left/Right: cycle value of current field
- In edit mode on type field: Left/Right cycles NORMAL/ARPEG (max 4 ARPEG enforced)
- In edit mode on quantize field: Left/Right cycles Immediate/Beat/Bar
- Down on type field when ARPEG → moves to quantize sub-field
- Down on NORMAL bank type field → ignored
- Enter exits edit → saves both type + quantize arrays to NVS

- [ ] **Step 2: Implement display**

Layout per bank line (single column, not 2-column):
```
  Bank 1    NORMAL
  Bank 2    NORMAL
> Bank 4   [ARPEG]    Quantize: Immediate   <- editing type
  Bank 5    ARPEG      Quantize: Beat
```

When editing quantize sub-field:
```
> Bank 4    ARPEG     Quantize: [Immediate]  <- editing quantize
```

Header shows ARPEG count: `"3/4 ARPEG"`

Info line: description of current field (type vs quantize).

Status line changes based on edit state.

- [ ] **Step 3: NVS failure handling**

Same pattern as Tool 5: `saveSettings()` returns bool. On failure, show red error 1.5s, stay in edit mode. User can retry with Enter.

- [ ] **Step 4: Implement defaults**

`d` → "Reset to defaults? (y/n)" → Banks 1-4 NORMAL, 5-8 ARPEG (4 ARPEG = max), all Quantize = Immediate.

- [ ] **Step 5: Build and verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS

- [ ] **Step 6: Flash and test on hardware**

Test checklist:
- Up/Down navigates 8 banks
- Enter enters edit on type field
- Left/Right toggles NORMAL/ARPEG (max 4 enforced, shows message)
- Down moves to quantize sub-field (ARPEG only, ignored for NORMAL)
- Left/Right cycles Immediate/Beat/Bar on quantize
- Enter saves and exits edit (LED blink)
- NVS failure → red error, stays in edit mode
- `d` resets to defaults with confirmation
- `q` returns to menu

- [ ] **Step 7: Commit**

```bash
git add src/setup/ToolBankConfig.cpp
git commit -m "Refactor Tool 4 Bank Config: unified navigation + sub-param editing"
```

---

## Task 5: Refactor Tool 6 — Pot Mapping

**Files:**
- Modify: `src/setup/ToolPotMapping.cpp` (major rewrite, ~511 lines)
- Modify: `src/setup/ToolPotMapping.h` (if state variables need changes)

This is complex: pool navigation, auto-steal with confirmation, CC# two-step, context toggle, physical pot detection.

- [ ] **Step 1: Replace input handling with InputParser**

New state model:
```cpp
InputParser input;
uint8_t cursor = 0;          // 0-7: which slot (4 pots × 2 layers)
bool editing = false;         // true = navigating pool
uint8_t poolIdx = 0;          // index in pool array
bool ccEditing = false;       // true = editing CC# sub-mode
uint8_t ccNumber = 0;         // CC# being edited
bool contextNormal = true;    // true = NORMAL, false = ARPEG
```

Key changes:
- `t` key (`NAV_TOGGLE`) switches context (replaces old Enter for context switch)
- Enter toggles edit mode (pool navigation)
- In pool edit: Left/Right cycles through available targets
- Selecting CC → first Enter confirms CC as target, enters CC# sub-editor, **Left/Right arrow keys** adjust CC# 0-127 (replaces old `<>`/`,` char keys, acceleration x10), second Enter confirms CC# and exits edit
- Auto-steal: selecting a taken param shows "Already assigned to [slot], replace? (y/n)" — `y` steals + saves + exits edit, anything else cancels

- [ ] **Step 2: Implement NVS failure handling**

Same pattern as Tool 5: save functions return bool. On failure, red error 1.5s, stay in edit mode.

- [ ] **Step 3: Implement physical pot detection alongside arrows**

Keep existing pot ADC detection logic (reads POT_PINS[], detects movement > 200 threshold). When pot movement detected and not editing, jump cursor to that pot's slot. Does not enter edit mode.

- [ ] **Step 4: Implement pool display**

Pool line below the slot grid:
```
Pool: Shape  Slew  ATdz  PBend  Tempo  BaseVel  VelVar  CC  PB  (empty)
```
- Green = available for assignment
- Dim = already assigned to another slot
- Cyan/brackets = currently highlighted in edit mode
- Info line below pool = description of highlighted param

- [ ] **Step 5: Implement NVS save on edit exit**

Each Enter exit: write full `PotMappingStore` to NVS + call `_potRouter->applyMapping(_wk)`. Accept the `rebuildBindings()` overhead in setup mode. Return bool for failure handling.

- [ ] **Step 6: Implement defaults**

`d` → "Reset to defaults? (y/n)" → resets current context (NORMAL or ARPEG) to `PotRouter::DEFAULT_MAPPING`.

- [ ] **Step 7: Build and verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS

- [ ] **Step 8: Flash and test on hardware**

Test checklist:
- Up/Down navigates 8 slots
- Turn pot → jumps to slot
- Enter enters pool edit, Left/Right arrow keys cycle pool
- Available = green, assigned = dim
- Selecting assigned param → steal confirmation y/n
- Selecting CC → Enter → CC# sub-editor → Left/Right adjusts (accel x10) → Enter confirms
- PB: max 1, auto-steal
- `t` toggles NORMAL/ARPEG context
- Enter exits edit → NVS save + LED blink + applyMapping live
- NVS failure → red error, stays in edit mode
- `d` resets current context to defaults
- `q` returns to menu

- [ ] **Step 9: Commit**

```bash
git add src/setup/ToolPotMapping.cpp src/setup/ToolPotMapping.h
git commit -m "Refactor Tool 6 Pot Mapping: unified navigation + pool + steal confirmation"
```

---

## Task 6: Refactor Tool 3 — Pad Roles

**Files:**
- Modify: `src/setup/ToolPadRoles.cpp` (complete rewrite, ~577 lines)
- Modify: `src/setup/ToolPadRoles.h` (state variable changes)

Most complex tool — grid navigation + 3-line pool + touch detection + auto-steal.

- [ ] **Step 1: Define new state model**

```cpp
InputParser input;
uint8_t gridRow = 0;      // 0-3 (4 rows of 12)
uint8_t gridCol = 0;      // 0-11

bool editing = false;      // true = pool navigation
uint8_t poolLine = 0;      // 0=none, 1=bank, 2=scale, 3=arp
uint8_t poolIdx = 0;       // index within current pool line

// Role assignments (working copy)
uint8_t roleMap[NUM_KEYS]; // ROLE_NONE, ROLE_BANK_1..8, ROLE_ROOT_C..B, etc.
```

- [ ] **Step 2: Implement grid navigation**

- All 4 arrows navigate the 4×12 grid when not editing
- Wrapping: Right on col 11 → col 0 next row. Left on col 0 → col 11 prev row. Up on row 0 → row 3. Down on row 3 → row 0.
- Touch pad detection (via `detectActiveKey()` from SetupCommon.h): jump to cell, don't enter edit mode
- If touch detected while editing → exit edit (discard), jump to new cell

- [ ] **Step 3: Implement pool navigation (edit mode)**

Enter on grid cell → enter edit mode:
- Pool area shows 3 lines: Bank (8 items), Scale (15 items), Arp (6 items)
- poolLine 0 = "none" position (above bank line) — selecting this removes the role
- Up/Down navigates between none/bank/scale/arp
- Left/Right navigates within the current pool line
- Current pad's existing role = highlighted in pool
- Roles assigned to other pads = dim with pad number

- [ ] **Step 4: Implement auto-steal confirmation**

When selecting a role already assigned to another pad:
```
"Already assigned to pad 14, replace? (y/n)"
```
- `y` → steal (pad 14 becomes none, current pad gets role), save NVS, LED blink, exit edit
- anything else → cancel, stay in edit

- [ ] **Step 5: Implement save and defaults**

- Enter on a pool item → assign role to current pad, save all role arrays to NVS, LED blink, return to grid
- Enter on "none" → remove role from pad, save, blink, return to grid
- `d` → "Reset to defaults? (y/n)" → reset all roles to defaults

- [ ] **Step 6: Implement display**

Grid: 4×12 with colored 5-char labels (same colors as current `drawRolesGrid()`):
- BLUE = bank, GREEN = scale, YELLOW = arp, DIM = none
- Selected cell = cyan highlight / border

Pool area (3 lines):
```
  Bank:  Bk1  Bk2  Bk3  Bk4  Bk5  Bk6  Bk7  Bk8
  Scale: RtC  RtC# RtD  RtD# RtE  RtF  RtG  MdIo MdDo MdPh MdLy MdMx MdLo MdAe Chr
  Arp:   Hld  P/S  Oc1  Oc2  Oc3  Oc4
```

Info line: description of highlighted role.
Status line: context-sensitive keys.

- [ ] **Step 7: Build and verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS

- [ ] **Step 8: Flash and test on hardware**

Test checklist:
- Arrows navigate 4×12 grid with wrapping
- Touch pad → jump to cell
- Enter → descend to pool, Up/Down between none/bank/scale/arp
- Left/Right cycle within pool line
- Enter on pool item → assign + save + blink + return to grid
- Enter on "none" → remove role + save
- Selecting taken role → steal confirmation y/n
- Touch new pad while in pool edit → exit edit, jump to new cell
- `d` resets all roles to defaults
- `q` returns to menu
- Existing roles display with correct colors

- [ ] **Step 9: Commit**

```bash
git add src/setup/ToolPadRoles.cpp src/setup/ToolPadRoles.h
git commit -m "Refactor Tool 3 Pad Roles: grid navigation + 3-pool assignment + auto-steal"
```

---

## Task 7: Minor Updates — Tool 2, Tool 1, Cleanup

**Files:**
- Modify: `src/setup/ToolPadOrdering.cpp`
- Modify: `src/setup/ToolCalibration.cpp`

- [ ] **Step 1: Update Tool 2 (Pad Ordering) to use InputParser**

- Add `InputParser input;` and use `input.update()` instead of `_ui->readInput()`
- Map `NAV_QUIT` → abort and return
- Map `NAV_DEFAULTS` → "Reset to default order? (y/n)" → reset padOrder to linear 0-47, save NVS
- Existing `u` (undo), `n` (skip) keys arrive as `NAV_CHAR` — handle via `ev.ch`
- Keep sequential touch flow unchanged

- [ ] **Step 2: Update Tool 1 (Calibration) to use InputParser**

- Add `InputParser input;` and use `input.update()` instead of `_ui->readInput()`
- Map `NAV_QUIT` → abort and return
- Existing `r` (redo), `s` (save) keys arrive as `NAV_CHAR` — handle via `ev.ch`
- Keep guided flow unchanged

- [ ] **Step 3: Remove readInput() from SetupUI**

All tools now use InputParser. Remove `readInput()` declaration from `SetupUI.h` and its body from `SetupUI.cpp`. Compile to verify no remaining callers.

- [ ] **Step 4: Clean up prompt strings**

Search all setup files for references to "BTN", "button", "rear" in user-facing strings. Remove or replace with serial-only instructions. Key locations:
- `ToolCalibration.cpp` lines 96, 210, 282, 315, 393
- `ToolPadOrdering.cpp` lines 133, 148, 230
- `ToolPadRoles.cpp` lines 331, 339

- [ ] **Step 5: Build and verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. No compilation errors from removed `readInput()`.

- [ ] **Step 6: Flash and test on hardware**

Test checklist:
- Tool 1: calibration flow works, `q` quits
- Tool 2: ordering flow works, `q` quits, `d` resets to default order, `u` undoes
- No "button" references in any tool's display
- All 6 tools accessible from main menu
- Full cycle: enter setup → visit each tool → quit → reboot

- [ ] **Step 7: Commit**

```bash
git add src/setup/SetupUI.h src/setup/SetupUI.cpp src/setup/ToolPadOrdering.cpp src/setup/ToolCalibration.cpp
git commit -m "Update Tool 1 & 2: InputParser + defaults, remove readInput + button references"
```

---

## Task 8: Final Integration Test + Push

- [ ] **Step 1: Full integration test on hardware**

Flash final build. Enter setup mode. Test complete flow:
1. Main menu: all 6 tools accessible, `q` reboots
2. Tool 5 (Settings): full nav + edit + save + defaults cycle
3. Tool 4 (Bank Config): nav + type toggle + quantize sub-param + defaults
4. Tool 6 (Pot Mapping): nav + pot detection + pool + CC# + steal + context toggle + defaults
5. Tool 3 (Pad Roles): grid nav + touch + pool + steal + defaults
6. Tool 2 (Pad Ordering): sequential + undo + defaults
7. Tool 1 (Calibration): guided flow + quit
8. Reboot → verify all saved settings persist

- [ ] **Step 2: Push**

```bash
git push origin main
```
