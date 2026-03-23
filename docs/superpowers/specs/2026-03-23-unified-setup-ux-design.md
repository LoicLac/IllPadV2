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

Shared input parsing layer. No business logic, no state beyond escape sequence buffering.

**NavEvent struct:**
```cpp
enum NavType : uint8_t {
  NAV_NONE = 0,
  NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT,
  NAV_ENTER,
  NAV_QUIT,        // 'q'
  NAV_DEFAULTS,    // 'd'
  NAV_TOGGLE,      // 't' (context switch in Pot Mapping)
};

struct NavEvent {
  NavType type;
  bool    accelerated;  // true if rapid repeat detected (step x10)
};
```

**Responsibilities:**
- Read serial bytes, assemble VT100 escape sequences
- Escape timeout: if `ESC` arrives without `[` within 50ms, discard
- Map `ESC[A/B/C/D` to UP/DOWN/LEFT/RIGHT
- Map `\r` and `\n` to ENTER
- Map `q`, `d`, `t` to QUIT, DEFAULTS, TOGGLE
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
| Left/Right | Cycle value (only in edit mode) |
| Enter | Toggle edit mode. Exiting edit mode = immediate NVS save + LED blink |
| `q` | Return to main menu (no confirmation needed — everything already saved) |
| `d` | Reset to defaults + confirmation "Reset to defaults? (y/n)" |

#### Edit Mode
- Selected parameter highlighted (cyan + `>` indicator)
- Enter toggles into edit mode: value becomes modifiable via Left/Right
- Enter again exits edit mode: NVS write fires, LED blink confirms
- While in edit mode, Up/Down are blocked (must exit edit first)

#### Acceleration (Left/Right)
- Two identical arrow events within 120ms: step x10
- Applies to numeric values (CC# 0-127, tempo ranges, deadzone, etc.)
- Discrete values (enums, booleans) ignore acceleration — always step 1

#### Auto-Steal with Confirmation
Applies to: Pad Roles (Tool 3), Pot Mapping (Tool 6).

When user selects a value already assigned elsewhere:
1. Display: "Already assigned to [X], replace? (y/n)"
2. `y` = source slot becomes empty, target gets the value, save both
3. `n` = cancel, nothing changes
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

## 4. Tool-by-Tool Design

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

**Navigation:** Up/Down through 8 parameters.
**Edit:** Enter toggles. Left/Right cycles value. Acceleration for numeric params.
**Save:** Enter exits edit = immediate NVS write + LED blink.
**Info:** Description box below list updates per selected param (already exists, keep).
**Defaults:** `d` resets all 8 params to factory values.

### 4.2 Tool 4 — Bank Config

**Layout:**
```
ILLPAD48 — Bank Configuration              3/4 ARPEG
──────────────────────────────────────────────────────

  Bank 1    NORMAL
  Bank 2    NORMAL
  Bank 3    NORMAL
> Bank 4   [ARPEG]    Quantize: Immediate   <- editing
  Bank 5    ARPEG      Quantize: Beat
  Bank 6    ARPEG      Quantize: Bar
  Bank 7    ARPEG      Quantize: Immediate
  Bank 8    ARPEG      Quantize: Immediate

  Bank type. ARPEG enables arpeggiator (max 4).
  Quantize sets when arp starts after first note.

  [Up/Down] navigate  [Enter] edit  [d] defaults  [q] quit
```

**Navigation:** Up/Down through 8 banks.
**Edit:** Enter on a bank → Left/Right cycles NORMAL/ARPEG.
- Max 4 ARPEG enforced: if at limit, ARPEG option skipped with message "Max 4 ARPEG reached".
- If bank is ARPEG: Down moves to Quantize sub-param (same bank), Left/Right cycles Immediate/Beat/Bar.
**Save:** Enter exits edit = save bank types + quantize to NVS, LED blink.
**Defaults:** `d` resets to Banks 1-3 NORMAL, Banks 4-8 ARPEG, all Quantize = Immediate.
**Info line:** Shows ARPEG count + description of selected field.

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

**Navigation:** Up/Down through 8 slots. Turn pot = jump to its slot.
**Edit:** Enter → cursor moves to pool line, Left/Right cycles available params.
- Available params: green. Already assigned: dim.
- Selecting assigned param: auto-steal with confirmation.
- Selecting MIDI CC: Left/Right continues to CC# (0-127, acceleration active).
- Selecting MIDI PB: max 1 per context, auto-steal if already assigned.
**Save:** Enter exits edit = save full PotMappingStore to NVS + applyMapping() live, LED blink.
**Context switch:** `t` toggles NORMAL/ARPEG (separate pool per context).
**Defaults:** `d` resets current context to default mapping.
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

**Grid navigation:** Up/Down/Left/Right moves cursor in 4x12 grid. Touch pad = jump to cell.
**Edit:** Enter on a cell → cursor descends to pool (3 lines: Bank / Scale / Arp).
- Up/Down navigates between 3 pool lines + implicit "none" (pressing Enter on a cell with a role while cursor is outside all pools = remove role, set to none).
- Left/Right navigates within the pool line.
- Current role of the pad = highlighted. Roles taken by other pads = dim.
- Selecting a taken role: "Already assigned to pad X, replace? (y/n)".
- Enter = validate choice, save immediate, LED blink, return to grid.
**Defaults:** `d` resets all pad roles to default assignment.
**Info line:** Description of highlighted role.

### 4.5 Tool 2 — Pad Ordering (minimal changes)

Sequential touch flow remains (touch pads low to high, positions 1-48).
**Changes:**
- Uses InputParser for key handling
- `d` = reset to default linear order (0-47) + confirmation
- `q` = abort + return to menu
- Info line shows progress and instructions

### 4.6 Tool 1 — Calibration (minimal changes)

Guided flow remains unchanged.
**Changes:**
- Uses InputParser for key handling
- `q` = abort + return to menu

## 5. Implementation Order

1. **InputParser** — standalone module, no dependencies
2. **Tool 5 (Settings)** — simplest list navigation, validates the pattern
3. **Tool 4 (Bank Config)** — list with sub-param (quantize)
4. **Tool 6 (Pot Mapping)** — list + pool + hardware pot detection + context toggle
5. **Tool 3 (Pad Roles)** — grid + 3-line pool + hardware pad detection + steal
6. **Tool 2 (Pad Ordering)** — minimal changes (InputParser + defaults)
7. **Tool 1 (Calibration)** — minimal changes (InputParser)

Each tool is independently deliverable and testable.

## 6. NVS Save Strategy

All tools save immediately on Enter (exit edit mode):
- Each tool writes only the parameter that changed (not the full config)
- Direct `Preferences` calls (setup mode is blocking, no background task)
- LED blink confirmation via `SetupUI::showSaved()`

Reset-to-defaults saves the full default config for that tool in one NVS write.

## 7. Files Changed

**New:**
- `src/setup/InputParser.h` — NavEvent enum, InputParser class
- `src/setup/InputParser.cpp` — VT100 parsing, acceleration detection

**Modified (major refactor):**
- `src/setup/ToolSettings.cpp/.h` — new navigation model
- `src/setup/ToolBankConfig.cpp/.h` — new navigation model
- `src/setup/ToolPotMapping.cpp/.h` — new navigation model + pool + steal
- `src/setup/ToolPadRoles.cpp/.h` — new navigation model + grid + 3-pool + steal

**Modified (minor):**
- `src/setup/SetupManager.cpp` — remove rear button input, use InputParser for menu
- `src/setup/SetupUI.cpp/.h` — remove `readInput()` rear button logic, add helpers if needed
- `src/setup/ToolPadOrdering.cpp/.h` — InputParser + defaults
- `src/setup/ToolCalibration.cpp/.h` — InputParser

**Not changed:**
- `src/setup/SetupCommon.h` — touch detection helpers remain as-is
