# VT100 Setup Console — Design Guide

This document defines the visual language, navigation patterns, and technical conventions for VT100 terminal-based setup interfaces. It serves as the spec for anyone touching the setup UI code.

---

# Part 1 — General (reusable across instruments)

These rules apply to any embedded instrument that uses a VT100 serial terminal for configuration.

## 1.1 Terminal Environment

- **Terminal size**: 120 columns x 50 rows. Enforced at setup entry via XTWINOPS (`CSI 8;50;120 t`). Re-sent at each tool entry.
- **Transport**: USB CDC serial. No baud rate dependency (CDC ignores it). The host runs a Python terminal script that bridges serial to the local terminal emulator.
- **Rendering protocol**: DEC 2026 synchronized output. Every frame is wrapped in `CSI ? 2026 h` (start) and `CSI ? 2026 l` (end). This prevents tearing — the terminal buffers all output between the two markers and renders it as one atomic frame.
- **Frame origin**: every frame starts with `CSI H` (cursor home) before `CSI ? 2026 h`. The Python script uses this as the boot sync marker.
- **Clear-to-EOL**: every line ends with `CSI K` (erase to end of line) to avoid ghosting from previous longer frames.

## 1.2 Frame System — Unicode Box Drawing

All visual structure uses Unicode box drawing characters (UTF-8 encoded, 3 bytes each). The ESP32 Serial.print() sends raw UTF-8 bytes; the terminal decodes them.

### Border hierarchy

| Level | Characters | Usage |
|---|---|---|
| **Outer frame** | `╔ ═ ╗ ║ ╚ ╝ ╠ ╣` (double-line) | Page border, header, control bar |
| **Section separator** | `╟ ─ ╢` (single-line, connects to outer) | Named sections within the page |
| **Cell grid** | `┌ ─ ┬ ┐ │ ├ ─ ┼ ┤ │ └ ─ ┴ ┘` (single-line, standalone) | Data grids (pad matrices, tables) |
| **Two-column divider** | `│` (single vertical) | Side-by-side layouts |

### Frame anatomy

```
╔══════════════════════════════════════════════════════╗   ← drawFrameTop()
║  [TITLE REVERSE]     TOOL NAME           [NVS:OK]   ║   ← drawConsoleHeader()
╠══════════════════════════════════════════════════════╣   ← separator (part of header)
║                                                      ║   ← drawFrameEmpty()
╟── SECTION LABEL ─────────────────────────────────────╢   ← drawSection()
║  Content line with auto-padding                      ║   ← drawFrameLine()
║                                                      ║   ← drawFrameEmpty()
╠══════════════════════════════════════════════════════╣   ← drawControlBar() top
║  [KEY] ACTION   [KEY] ACTION   [q] EXIT              ║   ← drawControlBar() content
╚══════════════════════════════════════════════════════╝   ← drawControlBar() bottom
```

### Width math

- `CONSOLE_W = 120` — total frame width including borders
- `CONSOLE_INNER = 116` — usable content area
- Frame line structure: `║` + `  ` + content + padding + ` ` + `║` = 120 chars
- Visible content budget: `CONSOLE_W - 5 = 115` chars (2 border + 2 indent + 1 trailing space)

### visibleLen() — UTF-8 and ANSI aware

All padding calculations use `visibleLen()` which counts **visible terminal columns**, not bytes:
- Skips ANSI escape sequences (everything between `ESC` and the terminating letter)
- Skips UTF-8 continuation bytes (`0x80-0xBF`) — a 3-byte UTF-8 codepoint counts as 1 column
- All box drawing characters are single-width (1 terminal column)

### printRepeatStr() for multi-byte chars

`printRepeat(char, n)` works for ASCII. For Unicode chars (multi-byte), use `printRepeatStr(const char* s, n)`.

## 1.3 Color Palette Philosophy

Colors are semantic, not decorative. Each color has a fixed meaning across all tools.

| Color | ANSI code | Meaning |
|---|---|---|
| **Dim** | `\033[2m` | Structure (borders, separators, labels, inactive items) |
| **Cyan** | `\033[36m` | Cursor / selection / active item |
| **Cyan Bold** | `\033[36m\033[1m` | Cursor with content (editing) |
| **Green** | `\033[32m` | Confirmed / valid / saved / available |
| **Bright Green** | `\033[92m` | Strong confirmation (NVS:OK badge) |
| **Yellow** | `\033[33m` | Warning / modified / attention |
| **Red** | `\033[31m` | Error / invalid / failed |
| **Bright Red** | `\033[91m` | Critical / special function |
| **Magenta** | `\033[35m` | Already-done / duplicate / special modifier |
| **Blue** | `\033[34m` | Category identity (e.g., bank) |
| **Bright White** | `\033[97m` | Important data values |
| **Reverse** | `\033[7m` | Emphasis (titles, badges, pool cursor) |

### Rule: borders are ALWAYS dim

Frame borders (`║`, `═`, `─`, `╟`, cell grid lines) are always rendered in `VT_DIM`. They are structural, not content. Content colors stand out against dim structure.

### Rule: section labels are ALWAYS cyan

The label text inside `╟── LABEL ──╢` separators is always `VT_CYAN`. This provides consistent visual anchoring.

## 1.4 Navigation Patterns

### Standard input model

- **Arrow keys**: navigate (up/down/left/right)
- **Enter**: confirm / enter edit mode / save
- **q**: back / cancel / exit
- **d**: reset to defaults (with y/n confirmation)
- **r**: clear all (with y/n confirmation, where applicable)
- **t**: toggle context (where applicable)

### Confirmation pattern

Confirmations are **inline** — never clear the screen. Show the question inside the frame (in the INFO section or control bar). Wait for `y` or any-other-key-to-cancel.

### screenDirty pattern

All tools use a `screenDirty` flag:
1. Logic modifies state and sets `screenDirty = true`
2. Render block checks `if (screenDirty)`, clears flag, redraws
3. Some states use time-based refresh (e.g., 200ms for live sensor data) instead of or in addition to the dirty flag
4. Never render outside the screenDirty gate (or time gate)

### Info panel rule: always follows cursor

The INFO section always shows context for the currently selected item:
- In grid navigation: info describes the pad under the grid cursor
- In pool/edit mode: info describes the pool item under the pool cursor
- In parameter lists: info describes the parameter under the cursor

The info panel content changes with every cursor movement. It never shows stale or generic content.

## 1.5 Save Feedback

### Header pulse (120ms → 300ms)

On save, `flashSaved()` does an inline blocking pulse on the header title:
- 3 cycles of (reverse ON → delay → reverse OFF → delay)
- Total duration ~300ms
- Only redraws the header line (row 2), not the full screen
- LED validation animation triggers simultaneously
- The NVS badge updates permanently (not tied to the flash)

### No blocking banner

The info panel is NEVER blocked by save feedback. No `=== SAVED ===` banner that replaces useful content. The header pulse + LED is sufficient.

## 1.6 NVS Status Badge

Every tool header shows a badge:
- `NVS:OK` in reverse green — data saved to flash for this tool
- `NVS:--` in reverse yellow — running on factory defaults

The main menu shows per-tool status (`ok` / `--`). Each check reads the NVS namespace and validates size + magic/version where applicable.

## 1.7 Cell Grid (4xN pad matrix)

For instruments with pad matrices, `drawCellGrid()` renders a bordered grid:

```
┌─────┬─────┬─────┬─────┐
│ val │ val │ val │ val │
├─────┼─────┼─────┼─────┤
│ val │ val │ val │ val │
└─────┴─────┴─────┴─────┘
```

- Cell width: 5 visible chars (content centered)
- Grid is emitted via `drawFrameLine()` so it sits inside the outer `║...║` frame
- Grid indented 4 spaces from left content edge
- Color per cell is mode-dependent (each tool defines its color rules)

## 1.8 Python Terminal Script Conventions

The Python serial terminal (`ItermCode/vt100_serial_terminal.py`) is the bridge between the instrument and the user's terminal emulator.

### Responsibilities

| Concern | Owner |
|---|---|
| Terminal palette (colors, bg) | **Python script** (OSC sequences to terminal emulator) |
| Terminal resize | **Firmware** (CSI sequence, passes through serial) |
| Frame rendering | **Firmware** (VT100/Unicode via Serial.print) |
| Boot sync | **Python script** (discard garbage, wait for `ESC[H`) |
| Line ending normalization | **Python script** (`\n` → `\r\n`) |
| Arrow key atomic send | **Python script** (read full escape sequence, write atomically) |

### Palette management

- Script sets custom palette on connect (before boot sync)
- Script resets to default palette on disconnect (in finally block)
- OSC 1337 sequences for iTerm2; standard OSC 10/11 for other terminals
- Firmware does NOT send palette sequences (they don't survive serial passthrough reliably)

### Boot sync protocol

1. Phase 1: hard discard for 0.5s (ESP32 boot log garbage)
2. Phase 2: buffer until `ESC[H` found (first real frame marker)
3. Output everything from `ESC[H` onward, enter main passthrough loop
4. On reconnect: repeat boot sync

---

# Part 2 — ILLPAD48 Specific

These rules are specific to the ILLPAD48 V2 and its 7 setup tools.

## 2.1 Amber Phosphor CRT Palette

The ILLPAD48 uses an Amber Phosphor P3 aesthetic:
- Background: `#1a0e00` (dark brown-black)
- Foreground: `#ffaa33` (warm amber)
- Set by the Python terminal script via `OSC 1337;SetColors=bg=1a0e00` and `fg=ffaa33`
- Restored to defaults on script exit

This gives a vintage Wyse/Hazeltine terminal look. All ANSI colors remain active and contrast strongly against the amber background.

## 2.2 Tool Structure

All 7 tools + main menu follow the same frame layout:

```
╔══...══╗
║  [ILLPAD48 SETUP CONSOLE]   TOOL N: NAME        [NVS:OK]  ║
╠══...══╣
║                                                             ║
╟── SECTION ──────────────────────────────────────────────────╢
║  content                                                    ║
╟── INFO ─────────────────────────────────────────────────────╢
║  context-sensitive description                              ║
╠══...══╣
║  [KEY] ACTION  [KEY] ACTION  [q] EXIT                       ║
╚══...══╝
```

### Main Menu

- Sections: CONFIGURATION TOOLS, SYSTEM, STATUS
- Per-tool NVS status (`ok` green, `--` dim)
- iTerm2 init happens here (resize, palette sent by script)

### Tool 1 — Pressure Calibration

- States: SENSITIVITY → STABILIZATION → MEASUREMENT → RECAP → SAVE
- Grid mode: GRID_BASELINE (stabilization), GRID_MEASUREMENT (calibration)
- No pool. Detail box shows sensor/channel/delta for active pad.

### Tool 2 — Pad Ordering

- States: REVIEW → MEASUREMENT → RECAP → SAVE
- Grid mode: GRID_ORDERING (position numbers 1-48)
- Touch = auto-assign (no Enter needed)
- REVIEW state shows current NVS ordering in green

### Tool 3 — Pad Roles

- Grid mode: GRID_ROLES (colored labels by category)
- Pool: 7 lines with distinct colors (see 2.3)
- Grid nav: arrows move cursor, touch = jump
- Edit mode: Enter on grid → pool navigation, assign role to pad
- `[r]` clear all roles, `[d]` factory defaults

### Tool 4 — Bank Config

- List of 8 banks with type (NORMAL/ARPEG) and quantize mode
- Arrow nav, Enter to edit, Left/Right to toggle type
- Down arrow from type field → quantize sub-field (ARPEG only)
- Max 4 ARPEG banks enforced

### Tool 5 — Settings

- Named category sections: PERFORMANCE, CONNECTIVITY, TIMING, SAFETY
- 8 parameters, arrow nav, Enter to edit, Left/Right to change value
- Parameter types: enum (wrap), numeric (clamp + accelerate), boolean (toggle), calibration (ADC sample)
- Reboot warning for BLE Interval and Clock Mode changes

### Tool 6 — Pot Mapping

- Two-column layout: Alone | + Hold Left
- Up/Down = R1-R4, Left/Right = switch column
- Context toggle: `t` switches NORMAL/ARPEG
- Pool line shows available targets with color coding
- CC sub-editor and steal confirmation sub-states

### Tool 7 — LED Settings

- 2 pages toggled with `t`: DISPLAY (15 params) and CONFIRM (15 params)
- DISPLAY page: Normal bank fg/bg intensity, Arpeg pulse min/max (fg/bg, stopped/playing), tick flash intensities, absolute max cap, pulse period, tick flash duration
- CONFIRM page: per-event blink count (1-3) + duration (ms) for bank switch, scale root/mode/chrom, hold on/off, play beats, stop fade, octave
- Same drawParam/adjustParam pattern as Tool 5
- NVS namespace `illpad_lset`, struct `LedSettingsStore` with magic/version
- Save applies immediately via `LedController::loadLedSettings()`
- Sine LUT: 256 entries (upgraded from 64) for smooth breathing
- `setPixelAbsolute()` for tick flash and errors — ignores global brightness, capped by `absoluteMax`

## 2.3 Pad Role Categories and Colors

Tool 3 uses 7 role categories, each with a distinct VT100 color:

| Category | Pool label | Items | Color | ANSI | RoleCode |
|---|---|---|---|---|---|
| **Bank** | `Bank:` | Bk1-Bk8 | Blue | `\033[34m` | 1 |
| **Root** | `Root:` | A-G | Green | `\033[32m` | 2 |
| **Mode** | `Mode:` | Ion Dor Phr Lyd Mix Aeo Loc Chr | Cyan | `\033[36m` | 3 |
| **Octave** | `Octave:` | 1-4 | Yellow | `\033[33m` | 4 |
| **Hold** | `Hold:` | Hld | Magenta | `\033[35m` | 5 |
| **Play/Stop** | `Play/Stop:` | P/S | Bright Red | `\033[91m` | 6 |
| **Clear** | `[---] clear role` | (action) | Dim | `\033[2m` | 0 |

These colors are used in:
- The pool inventory lines
- The 4x12 grid cells (via `roleMap[]` → `drawCellGrid()`)
- The info panel title when showing a role description

Hold and Play/Stop have their own lines and colors because they are unique functions:
- Hold = arp-only toggle (hold on/off)
- Play/Stop = the ONLY pad that works WITHOUT the left button held — it's a standalone transport control

## 2.4 Grid Color Rules by Mode

| Mode | Green | Yellow | Cyan | Magenta | Red | Dim |
|---|---|---|---|---|---|---|
| **GRID_BASELINE** | Within tolerance | Out of tolerance | — | — | — | — |
| **GRID_MEASUREMENT** | Delta valid (>=50) | — | Active (measuring) | Active (already done) | Delta too low | Uncalibrated |
| **GRID_ORDERING** | Assigned position | — | Active (touching) | Active (already assigned) | — | Unassigned |
| **GRID_ROLES** | Root | — | Mode (selected=bold) | — | Collision | None/unassigned |

## 2.5 Files

| File | Role |
|---|---|
| `src/setup/SetupUI.h` | All VT100/Unicode defines, frame primitive signatures, iTerm2 defines |
| `src/setup/SetupUI.cpp` | Frame primitive implementations, drawCellGrid(), flashSaved(), initTerminal() |
| `src/setup/SetupManager.cpp` | Setup mode entry, tool dispatch, iTerm2 init call |
| `src/setup/InputParser.h` | Arrow/Enter/char input parsing, NAV_* event types |
| `src/setup/Tool*.cpp` | Individual tool rendering + logic |
| `ItermCode/vt100_serial_terminal.py` | Python serial bridge, palette management, boot sync |
