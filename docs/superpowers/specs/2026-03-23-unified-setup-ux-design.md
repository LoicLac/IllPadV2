# Unified Setup UX — Design Spec

**Date:** 2026-03-23
**Status:** Approved
**Scope:** Refactor all 6 setup tools to share a unified navigation model, input parser, and UX conventions.

---

## 1. Problem Statement

The current setup menu system has inconsistent navigation across tools:
- No arrow key parsing (each tool uses custom single-char keys)
- Saving is inconsistent (Enter in some tools, `s` in others)
- No unified edit/confirm model
- Collision handling differs between tools (blocking save vs auto-steal)
- No reset-to-defaults in most tools
- Rear button as ENTER is removed — all input via serial keyboard (iTerm2)

## 2. Terminal Environment

- **Terminal:** iTerm2 (macOS)
- **Protocol:** VT100 with synchronized updates (`ESC[?2026h/l`)
- **Arrow keys:** `ESC [ A` (up), `ESC [ B` (down), `ESC [ C` (right), `ESC [ D` (left)
- **Colors:** ANSI 8-color (already used)
- **Input:** Serial only. Rear button removed from setup mode.

## 3. Architecture — Approach B (Shared Parser, Incremental Refactor)

### 3.1 InputParser (new module: `src/setup/InputParser.h/.cpp`)

Shared input parsing layer. Stateful: stores partial escape sequences between `update()` calls and uses `millis()` for timeouts.

**NavEvent struct:**
```cpp
enum NavType : uint8_t {
  NAV_NONE = 0,
  NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT,
  NAV_ENTER,
  NAV_QUIT,        // 'q'
  NAV_DEFAULTS,    // 'd'
  NAV_TOGGLE,      // 't' (context switch in Pot Mapping)
  NAV_CHAR,        // all other keys — raw char in NavEvent.ch (includes 'y', 'n', 'u', 's', etc.)
};

struct NavEvent {
  NavType type;
  bool    accelerated;  // true if rapid repeat detected
  char    ch;           // raw character (only valid when type == NAV_CHAR)
};
```

**Responsibilities:**
- Read serial bytes, assemble VT100 escape sequences
- **Stateful escape parsing:** stores partial sequences (`ESC` received, waiting for `[` + letter). Uses `millis()` to detect timeout.
- Escape timeout: if `ESC` arrives without `[` within 50ms, discard (not a valid arrow sequence)
- Map `ESC[A/B/C/D` to UP/DOWN/LEFT/RIGHT
- Map `\r` and `\n` to ENTER
- Map `q`, `d`, `t` to their dedicated NavType equivalents
- All other characters (`y`, `n`, `u`, `s`, `0`-`9`, etc.) → `NAV_CHAR` with raw char in `event.ch`. Each tool interprets these in its own context (e.g., `n` = "no" in a confirmation prompt, `n` = "skip" in Tool 2 sequential flow, `1`-`6` = tool selection in main menu)
- Acceleration detection: if two identical LEFT/RIGHT events arrive within 120ms, set `accelerated = true`
- Return `NavEvent` (or `NAV_NONE` if no input)

**What it does NOT do:**
- No rendering
- No navigation state (cursor position, edit mode)
- No hardware input (pots, pads, buttons)

### 3.2 Conventions (the "contract" each tool follows)

#### Navigation
| Key | Action |
|---|---|
| Up/Down | Move cursor between parameters/cells |
| Left/Right | Cycle value (only in edit mode). Ignored outside edit mode (except Tool 3 grid navigation — see 4.4) |
| Enter | Toggle edit mode. Exiting edit mode = immediate NVS save + LED blink |
| `q` | Return to main menu (no confirmation needed — everything already saved) |
| `d` | Reset to defaults + confirmation "Reset to defaults? (y/n)" |

#### Edit Mode
- Selected parameter highlighted (cyan + `>` indicator, value in `[brackets]`)
- Enter toggles into edit mode: value becomes modifiable via Left/Right
- Enter again exits edit mode: NVS write fires, LED blink confirms
- While in edit mode, Up/Down are blocked (must exit edit first). **Exception:** Tool 4 Bank Config allows Down to move to Quantize sub-param within the same bank (see Section 4.2).

#### NVS Save & Failure Handling
- Enter exits edit mode → NVS write fires immediately (direct `Preferences` call, blocking)
- On success: LED blink confirmation via `SetupUI::showSaved()`
- On failure: red error message "NVS write failed!" displayed for 1.5s, value stays as-is in working copy (user can retry by pressing Enter again), edit mode stays active
- Flash wear note: setup mode is used infrequently (configuration only). The per-param save model is acceptable. Each NVS write is a single key in a single namespace — ESP32 NVS is designed for this pattern.

#### Acceleration (Left/Right)
- Two identical arrow events within 120ms: accelerated step
- **Step size:** x10 for large ranges (CC# 0-127, tempo). For small ranges where x10 would exceed 25% of total range, use x5 instead. Each tool defines the acceleration factor per parameter.
- Discrete values (enums, booleans) ignore acceleration — always step 1

#### Auto-Steal with Confirmation
Applies to: Pad Roles (Tool 3), Pot Mapping (Tool 6).

When user selects a value already assigned elsewhere:
1. Display: "Already assigned to [X], replace? (y/n)"
2. `y` = source slot becomes empty, target gets the value. The `y` acts as an implicit Enter: NVS save fires immediately, LED blink confirms, edit mode exits. This is consistent with the enter-save convention — the confirmation IS the final validation step.
3. `n` = cancel, stay in edit mode, nothing saved
4. Any other key = treated as `n`

#### Reset to Defaults
All tools support `d`:
1. Display: "Reset to defaults? (y/n)"
2. `y` = load default values into working copy, save all to NVS, LED blink
3. `n` = cancel

#### Hardware Navigation Shortcuts
Physical hardware complements arrows (never replaces):

| Tool | Hardware | Effect |
|---|---|---|
| Pot Mapping (6) | Turn a pot | Jump cursor to that pot's slot |
| Pad Roles (3) | Touch a pad | Jump cursor to that pad's grid cell |
| Pad Ordering (2) | Touch a pad | Part of sequential flow (unchanged) |
| Calibration (1) | Touch a pad | Part of sequential flow (unchanged) |
| Bank Config (4) | — | Arrows only |
| Settings (5) | — | Arrows only |

Hardware shortcut does NOT enter edit mode — just moves the cursor.

#### Display
- Status/help line at bottom: always shows available keys (context-sensitive)
- Info line: shows description/help for the currently selected parameter
- Pool lines at bottom for tools that have them (Pot Mapping: 1 line, Pad Roles: 3 lines)
- Synchronized VT100 updates (vtFrameStart/vtFrameEnd) for flicker-free rendering
- All prompt strings updated to remove references to rear button / "BTN"

## 4. Tool-by-Tool Design

### 4.0 Main Menu

The main menu keeps its current model: number keys `1`-`6` select a tool, `0` reboots. InputParser is used for input (number keys arrive as `NAV_CHAR`), but arrows are ignored on the main menu. The menu layout and NVS validation display remain unchanged.

**Migration:** Remove rear button input from `SetupManager::run()` and `SetupUI::readInput()`. Serial-only input.

### 4.1 Tool 5 — Settings (simplest, implements pattern first)

**Layout:**
```
ILLPAD48 — Settings
──────────────────────────────────────────────────────

  1. Baseline Profile      Adaptive
  2. Aftertouch Rate        25 ms
> 3. BLE Interval          [15 ms]     <- editing
  4. Clock Mode             Slave
  5. Follow Transport       Yes
  6. Double-Tap Window      150 ms
  7. Bargraph Duration      3.0 s
  8. Panic on Reconnect     No

  BLE connection interval. Lower = less latency,
  higher = less power. Requires reboot.

  [Up/Down] navigate  [Enter] edit  [d] defaults  [q] quit
```

**Navigation:** Up/Down through 8 parameters. Left/Right ignored outside edit mode.
**Edit:** Enter toggles. Left/Right cycles value. Acceleration for numeric params (x5 for small ranges like AT Rate 10-100, x10 for others).
**Save:** Enter exits edit = immediate NVS write + LED blink.
**Info:** Description box below list updates per selected param (already exists, keep). Includes reboot notice for BLE Interval and Clock Mode.
**Defaults:** `d` resets all 8 params to factory values + confirmation.

### 4.2 Tool 4 — Bank Config

**Layout:**
```
ILLPAD48 — Bank Configuration              3/4 ARPEG
──────────────────────────────────────────────────────

  Bank 1    NORMAL
  Bank 2    NORMAL
  Bank 3    NORMAL
> Bank 4   [ARPEG]    Quantize: Immediate   <- editing type
  Bank 5    ARPEG      Quantize: Beat
  Bank 6    ARPEG      Quantize: Bar
  Bank 7    ARPEG      Quantize: Immediate
  Bank 8    ARPEG      Quantize: Immediate

  Bank type. ARPEG enables arpeggiator (max 4).
  Quantize sets when arp starts after first note.

  [Up/Down] navigate  [Enter] edit  [d] defaults  [q] quit
```

**Navigation:** Up/Down through 8 banks. Left/Right ignored outside edit mode.

**Edit mode — two sub-params per bank:**
Enter on a bank enters edit on the **type** field (NORMAL/ARPEG). Left/Right cycles type.
- Max 4 ARPEG enforced: if at limit and trying to switch to ARPEG, message "Max 4 ARPEG reached", stays NORMAL.
- **Down within edit mode** moves focus to the **Quantize** sub-param of the same bank (only when type = ARPEG). Left/Right cycles Immediate/Beat/Bar. If the bank is NORMAL, Down is ignored (no Quantize field exists for NORMAL banks).
- **Up within edit mode** moves focus back to the type sub-param of the same bank. If already on type, Up is ignored.
- This is an intentional deviation from the "Up/Down blocked in edit" convention — Bank Config allows Down/Up to move between type and quantize within the same bank's edit session.
- Enter exits edit = saves both type and quantize to NVS, LED blink.

**Defaults:** `d` resets to Banks 1-3 NORMAL, Banks 4-8 ARPEG, all Quantize = Immediate + confirmation.
**Info line:** Shows ARPEG count (e.g., "3/4 ARPEG") + description of selected field (type vs quantize).

### 4.3 Tool 6 — Pot Mapping

**Layout:**
```
ILLPAD48 — Pot Mapping                    [NORMAL]  t=toggle
──────────────────────────────────────────────────────

  R1 alone     Tempo
  R1 + hold    (empty)
  R2 alone     Shape
  R2 + hold    AT Deadzone
> R3 alone    [Slew Rate]                  <- editing
  R3 + hold    Pitch Bend
  R4 alone     Base Velocity
  R4 + hold    Vel. Variation

  Pool: Shape  Slew  ATdz  PBend  Tempo  BaseVel  VelVar  CC  PB  (empty)

  Slew rate for pressure response smoothing.

  [Up/Down] navigate  [Left/Right] cycle pool  [Enter] edit
  [t] NORMAL/ARPEG  [d] defaults  [q] quit
```

**Navigation:** Up/Down through 8 slots. Turn pot = jump to its slot. Left/Right ignored outside edit mode.

**Edit:** Enter → cursor moves to pool line, Left/Right cycles available params.
- Available params: green. Already assigned: dim.
- Selecting assigned param: auto-steal with confirmation.
- Selecting MIDI CC: a two-step edit. First, Left/Right in the pool lands on "CC". Press Enter to confirm CC as the target — this opens a CC# sub-editor inline (the pool item shows "CC #0"). Left/Right adjusts CC# (0-127, acceleration x10). Press Enter again to confirm the CC# and exit edit mode (NVS save + blink). This is Enter → Enter (select CC target, then confirm CC number).
- Selecting MIDI PB: max 1 per context, auto-steal if already assigned.

**Save:** Enter exits edit = save full PotMappingStore to NVS + `applyMapping()` live, LED blink. Note: `applyMapping()` triggers `rebuildBindings()` + `seedCatchValues()`. This is acceptable in setup mode (not performance-critical).

**Context switch:** `t` toggles NORMAL/ARPEG (separate pool per context). **Migration note:** Enter is repurposed from context-switch (old behavior) to edit-toggle (new behavior). `t` replaces Enter for context switching.

**Defaults:** `d` resets current context to default mapping + confirmation.
**Info line:** Description of highlighted pool param.

### 4.4 Tool 3 — Pad Roles

**Layout:**
```
ILLPAD48 — Pad Roles
──────────────────────────────────────────────────────

   01   02   03   04   05   06   07   08   09   10   11   12
  Bk1  Bk2  Bk3  Bk4  Bk5  Bk6  Bk7  Bk8  RtC  RtC# RtD  RtD#
   13   14   15   16   17   18   19   20   21   22   23   24
  RtE  RtF  RtG [MdIo] MdDo MdPh MdLy MdMx MdLo MdAe Chr  Hld
   25   26   27   28   29   30   31   32   33   34   35   36
  P/S  Oc1  Oc2  Oc3  Oc4  ──   ──   ──   ──   ──   ──   ──
   37   38   39   40   41   42   43   44   45   46   47   48
   ──   ──   ──   ──   ──   ──   ──   ──   ──   ──   ──   ──

  Bank:  Bk1  Bk2  Bk3  Bk4  Bk5  Bk6  Bk7  Bk8
  Scale: RtC  RtC# RtD  RtD# RtE  RtF  RtG  MdIo MdDo MdPh MdLy MdMx MdLo MdAe Chr
> Arp:  [Hld] P/S  Oc1  Oc2  Oc3  Oc4

  Hold toggle pad. Press hold+pad to toggle arp hold mode.

  [Arrows] navigate grid  [Enter] edit  [Touch pad] jump
  [d] defaults  [q] quit
```

**Grid navigation (not in edit mode):** All four arrows (Up/Down/Left/Right) navigate the 4x12 grid. This is a deviation from the convention where Left/Right are edit-only — in Tool 3's grid, Left/Right are needed for spatial navigation.
- Wrapping: Right on column 12 wraps to column 1 of next row. Left on column 1 wraps to column 12 of previous row. Up on row 1 wraps to row 4. Down on row 4 wraps to row 1.
- Touch pad = jump to cell (does not enter edit mode).

**Edit mode (pool navigation):**
Enter on a grid cell → cursor descends to the pool area (3 lines + none):
- **4 vertical positions:** None (implicit, above the pool lines), Bank, Scale, Arp
- Up/Down navigates between these 4 positions
- Left/Right navigates within the current pool line's roles
- Current role of the selected pad = highlighted in the pool. Roles already assigned to other pads = dim with pad number shown.
- Selecting a taken role: "Already assigned to pad X, replace? (y/n)" → y = steal (other pad becomes none), n = cancel.

**Removing a role:** Navigate to the "None" position (Up above Bank line) and press Enter. The pad's role is cleared.

**Confirming a role:** Enter on any pool item = validate choice, save immediate to NVS, LED blink, cursor returns to grid.

**Touch detection in edit mode:** If user touches a different pad while in edit mode, exit edit mode (discard uncommitted selection), jump to new pad's cell in grid.

**Baseline drift:** The existing `detectActiveKey()` helper in SetupCommon.h handles baseline compensation and ambiguous-touch detection. This remains unchanged — it is called to detect pad touches regardless of edit mode state.

**Defaults:** `d` resets all pad roles to default assignment + confirmation. Default assignment is defined in code (existing `DEFAULT_BANK_PADS`, `DEFAULT_SCALE_PADS`, `DEFAULT_ARP_PADS` arrays).

**Info line:** Description of the highlighted role in the pool, or of the selected pad's current role when not in edit mode.

**No undo:** The old sequential-assignment flow had `u` for undo. The new model does not need it — each pad is edited individually with immediate save. To revert a mistake, re-edit the pad and pick the correct role.

### 4.5 Tool 2 — Pad Ordering (minimal changes)

Sequential touch flow remains (touch pads low to high, positions 1-48).

**Changes:**
- Uses InputParser for key handling
- `u` = undo last (existing behavior, received as `NAV_CHAR` with `ch='u'`)
- `d` = reset to default linear order (0-47) + confirmation
- `q` = abort + return to menu
- Info line shows progress and instructions

**Preserved keys:** `u` (undo), `n` (skip) — received via `NAV_CHAR` passthrough from InputParser.

### 4.6 Tool 1 — Calibration (minimal changes)

Guided flow remains unchanged.

**Changes:**
- Uses InputParser for key handling
- `q` = abort + return to menu

**Preserved keys:** `r` (redo), `s` (save) — received via `NAV_CHAR` passthrough from InputParser.

## 5. Implementation Order

1. **InputParser** — standalone module, no dependencies
2. **Tool 5 (Settings)** — simplest list navigation, validates the pattern
3. **Tool 4 (Bank Config)** — list with sub-param (quantize), tests edit-mode deviation
4. **Tool 6 (Pot Mapping)** — list + pool + hardware pot detection + context toggle + steal
5. **Tool 3 (Pad Roles)** — grid + 3-line pool + hardware pad detection + steal (most complex)
6. **Tool 2 (Pad Ordering)** — minimal changes (InputParser + defaults)
7. **Tool 1 (Calibration)** — minimal changes (InputParser)
8. **SetupManager + SetupUI cleanup** — remove rear button logic, update prompt strings

Each tool is independently deliverable and testable.

## 6. NVS Save Strategy

All tools save immediately on Enter (exit edit mode):
- Each tool writes only the parameter(s) that changed (blocking `Preferences` calls)
- On success: LED blink confirmation via `SetupUI::showSaved()`
- On failure: red error message "NVS write failed!" for 1.5s, stays in edit mode, user can retry
- Flash wear: acceptable — setup mode is used infrequently (configuration only)

Reset-to-defaults saves the full default config for that tool in one NVS write.

## 7. Files Changed

**New:**
- `src/setup/InputParser.h` — NavEvent enum + struct, InputParser class
- `src/setup/InputParser.cpp` — VT100 escape sequence parsing (stateful), acceleration detection, char passthrough

**Modified (major refactor):**
- `src/setup/ToolSettings.cpp/.h` — new navigation model
- `src/setup/ToolBankConfig.cpp/.h` — new navigation model + sub-param deviation
- `src/setup/ToolPotMapping.cpp/.h` — new navigation model + pool + steal. Enter repurposed from context-switch to edit-toggle.
- `src/setup/ToolPadRoles.cpp/.h` — complete rewrite: sub-menu model replaced by grid + 3-pool model

**Modified (minor):**
- `src/setup/SetupManager.cpp` — remove rear button input from main loop, use InputParser for menu char reading
- `src/setup/SetupUI.cpp/.h` — remove rear button logic from `readInput()`, remove "BTN" references from prompt strings
- `src/setup/ToolPadOrdering.cpp/.h` — InputParser + defaults
- `src/setup/ToolCalibration.cpp/.h` — InputParser + quit

**Not changed:**
- `src/setup/SetupCommon.h` — touch detection helpers remain as-is (used by Tool 2, 3)
