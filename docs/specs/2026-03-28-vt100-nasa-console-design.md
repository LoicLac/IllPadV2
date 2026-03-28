# VT100 NASA Console — Design Spec

## Context

The ILLPAD48 VT100 setup terminal is a selling point. The current implementation (Sonnet pass) partially converted tools to a frame system but left grids untouched, visual style inconsistent across tools, and missed several creative opportunities. This spec defines a complete visual overhaul to achieve a cinematic **Amber Phosphor CRT** aesthetic with Unicode box drawing, iTerm2-specific features, and unified rendering across all tools.

**Scope:** Main Menu + Tools 2-6. Tool 1 (Calibration) is excluded — separate session.

## Design Decisions

| Decision | Choice |
|---|---|
| Frame borders | Unicode double-line (`╔═╗║╚╝╠╣`) |
| Section separators | Unicode single-line left/right T (`╟─╢`) |
| Grid cells | Unicode single-line table (`┌─┬┐│├─┼┤│└─┴┘`) |
| Palette | Amber Phosphor P3 (bg `#1a0e00`, fg `#ffaa33`) |
| iTerm2 features | Tab title, badge, palette change, progress bar, resize |
| Save flash | Header pulse 3x in 120ms, no blocking banner |
| Terminal size | 120x50, enforced at setup entry + each tool entry |
| visibleLen() | UTF-8 codepoint counting (not byte counting) |
| Approach | Rewrite render blocks only, keep all logic/state machines |

---

## 1. SetupUI Infrastructure

### 1.1 Unicode Box Drawing Constants

```cpp
// Double-line frame (outer borders)
#define UNI_TL  "╔"
#define UNI_TR  "╗"
#define UNI_BL  "╚"
#define UNI_BR  "╝"
#define UNI_H   "═"
#define UNI_V   "║"
#define UNI_LT  "╠"
#define UNI_RT  "╣"

// Single-line section separators
#define UNI_SLT "╟"
#define UNI_SRT "╢"
#define UNI_SH  "─"

// Cell grid (single-line)
#define UNI_CTL "┌"
#define UNI_CTR "┐"
#define UNI_CBL "└"
#define UNI_CBR "┘"
#define UNI_CH  "─"
#define UNI_CV  "│"
#define UNI_CLT "├"
#define UNI_CRT "┤"
#define UNI_CT  "┬"
#define UNI_CB  "┴"
#define UNI_CX  "┼"
```

### 1.2 iTerm2 Sequences (sent at setup entry)

```cpp
// Amber palette
#define ITERM_SET_BG    "\033]1337;SetColors=bg=1a0e00\007"
#define ITERM_SET_FG    "\033]1337;SetColors=fg=ffaa33\007"

// Tab title
#define ITERM_TAB_TITLE "\033]0;ILLPAD48 Setup\007"

// Badge (base64 of "ILLPAD48")
#define ITERM_BADGE     "\033]1337;SetBadgeFormat=SUxMUEFENDg=\007"

// Resize to 120x50
#define ITERM_RESIZE    "\033[8;50;120t"

// Progress bar
#define ITERM_PROGRESS_FMT "\033]9;4;1;%d\007"  // arg = percentage 0-100
#define ITERM_PROGRESS_END "\033]9;4;0;\007"
```

Send `ITERM_SET_BG + ITERM_SET_FG + ITERM_TAB_TITLE + ITERM_BADGE + ITERM_RESIZE` once in `SetupManager::run()` before the main menu loop. Send `ITERM_RESIZE` again at each tool `run()` entry.

### 1.3 Frame Primitives (rewritten, same signatures)

All primitives use Unicode box drawing instead of ASCII `+=-|`.

- `drawFrameTop()` → `╔══...══╗` (CONSOLE_W wide)
- `drawFrameBottom()` → `╚══...══╝`
- `drawSection(label)` → `╟── LABEL ──...──╢` (label in cyan)
- `drawFrameLine(fmt, ...)` → `║  content...pad  ║` (auto-pad to CONSOLE_INNER)
- `drawFrameEmpty()` → `║                    ║`
- `drawConsoleHeader(name, nvsSaved)` → reverse-video amber title + `NVS:OK`/`NVS:--` badge
- `drawControlBar(controls)` → between `╠══╣` and `╚══╝`

### 1.4 visibleLen() — UTF-8 Aware

```cpp
uint16_t SetupUI::visibleLen(const char* s) {
  uint16_t len = 0;
  bool inEsc = false;
  while (*s) {
    if (*s == '\033') {
      inEsc = true;
    } else if (inEsc) {
      if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) inEsc = false;
    } else if ((*s & 0xC0) != 0x80) {
      // Not a UTF-8 continuation byte = visible codepoint
      len++;
    }
    s++;
  }
  return len;
}
```

### 1.5 printRepeat() — UTF-8 Aware

Current `printRepeat(char c, uint8_t n)` only handles single-byte chars. Need a string variant:

```cpp
static void printRepeatStr(const char* s, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) Serial.print(s);
}
```

Used for repeating `═`, `─`, etc. (multi-byte UTF-8 strings).

### 1.6 drawCellGrid() — New 4x12 Grid with Cell Borders

```cpp
// New grid mode for roles (adds to existing GridMode enum)
// GRID_ROLES = 3  (new value)

void drawCellGrid(GridMode mode, ...same params as drawGrid()...,
                  const char roleLabels[][6] = nullptr,
                  const uint8_t roleMap[] = nullptr);
```

Renders:
```
┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
│ Bk1 │ Bk2 │ Bk3 │ Bk4 │ Bk5 │ Bk6 │ Bk7 │ Bk8 │ RtA │ RtB │ RtC │ RtD │
├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
│ RtE │ RtF │ RtG │MdIo │MdDo │MdPh │MdLy │MdMx │ P/S │MdLo │ Chr │ Hld │
├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
│  -- │ Oc1 │ Oc2 │ Oc3 │ Oc4 │  -- │  -- │  -- │  -- │  -- │  -- │  -- │
├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
│  -- │  -- │  -- │  -- │  -- │  -- │  -- │  -- │  -- │  -- │  -- │  -- │
└─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
```

Cell width: 5 chars (1 space + 3-4 char content + 1 space). Total grid width: 12 cells * 6 chars + 1 = 73 chars. Fits within CONSOLE_INNER (116).

Each grid row is emitted via `drawFrameLine()` so it sits inside the `║...║` frame.

Color rules per mode:
- **GRID_ORDERING**: green=assigned, dim=unassigned, cyan=active `>N<`, magenta=already-done `*N*`
- **GRID_ROLES**: blue=bank, green=scale, yellow=arp, dim=none, cyan+bold=selected, color by `roleMap[]`
- **GRID_BASELINE/MEASUREMENT**: green=in-range, yellow=out-of-range, cyan=measuring, red=failed

### 1.7 Save Flash — Header Pulse

```cpp
static const unsigned long SAVE_FLASH_DURATION_MS = 120;
static const uint8_t SAVE_FLASH_CYCLES = 3;  // 3 blinks in 120ms = 40ms each
```

`flashSaved()` executes the pulse inline (blocking, but only 120ms — acceptable):

```cpp
void SetupUI::flashSaved() {
  // 3 rapid pulses of the header line only (not full screen)
  for (uint8_t i = 0; i < 3; i++) {
    vtMoveTo(2, 1);  // header is on row 2
    // Draw header in reverse (flash ON)
    drawConsoleHeaderFlash(true);
    delay(20);
    vtMoveTo(2, 1);
    // Draw header normal (flash OFF)
    drawConsoleHeaderFlash(false);
    delay(20);
  }
  if (_leds) _leds->playValidation();
}
```

- Total blocking time: 120ms (6 * 20ms). Acceptable for a save event.
- Only redraws the header line (row 2), not the full screen. Minimal flicker.
- `drawConsoleHeaderFlash(bool flash)` is a private helper that renders just the header content for row 2.
- Badge NVS:OK is updated permanently by the caller (not tied to flash).
- `_saveFlashActive` flag and `checkSaveFlash()` are **removed entirely** — no more timer-based flash.
- `isSaveFlashActive()` is **removed** — tools no longer check it.
- The info panel is NEVER blocked by the save flash.

---

## 2. Main Menu

Unicode borders. Reverse-video amber title. Per-tool NVS status (`ok` green / `--` dim).

```
╔══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╗
║  ▐ ILLPAD48 SETUP CONSOLE ▌                                                                                      ║
╠══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╣
║                                                                                                                    ║
╟── CONFIGURATION TOOLS ─────────────────────────────────────────────────────────────────────────────────────────────╢
║                                                                                                                    ║
║    [1]  Pressure Calibration          sensitivity tuning                  ok                                       ║
║    [2]  Pad Ordering                  pitch mapping, low to high          ok                                       ║
║    [3]  Pad Roles                     bank / scale / arp pads             ok                                       ║
║    [4]  Bank Config                   NORMAL vs ARPEG, quantize           ok                                       ║
║    [5]  Settings                      preferences & connectivity          ok                                       ║
║    [6]  Pot Mapping                   parameter assignments               --                                       ║
║                                                                                                                    ║
╟── SYSTEM ──────────────────────────────────────────────────────────────────────────────────────────────────────────╢
║                                                                                                                    ║
║    [0]  Reboot & Exit Setup                                                                                        ║
║                                                                                                                    ║
╟── STATUS ──────────────────────────────────────────────────────────────────────────────────────────────────────────╢
║                                                                                                                    ║
║    ok = saved to NVS flash     -- = running on factory defaults                                                    ║
║                                                                                                                    ║
╠══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╣
║  Type 0-6                                                                                                          ║
╚══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╝
```

iTerm2 palette + tab title + badge + resize sent here (first display).

---

## 3. Tool 2 — Pad Ordering

**Logic:** unchanged (ORD_REVIEW, ORD_MEASUREMENT, ORD_RECAP, ORD_SAVE, ORD_DONE).

**Render changes:**
- Replace `drawGrid(GRID_ORDERING, ...)` with `drawCellGrid(GRID_ORDERING, ...)`
- All frame borders → Unicode
- Add iTerm2 progress bar during ORD_MEASUREMENT: `snprintf + ITERM_PROGRESS_FMT` with `assignedCount * 100 / NUM_KEYS`
- Clear progress bar on state exit: `ITERM_PROGRESS_END`
- Send `ITERM_RESIZE` at `run()` entry

---

## 4. Tool 3 — Pad Roles

**Logic:** unchanged. Bug fix (pool visual decoupled from grid) verified correct.

**Render changes:**
- Replace custom `drawGrid()` (lines 291-327) with `drawCellGrid(GRID_ROLES, roleLabels, roleMap)`
- Pool rendering: wrap in `drawFrameLine()` calls instead of raw `Serial.printf`
- Add `drawControlBar()` call (currently missing — outputs directly)
- All frame borders → Unicode
- Remove `isSaveFlashActive()` checks in drawInfoPanel() — no more SAVED banner in info
- Send `ITERM_RESIZE` at `run()` entry

---

## 5. Tool 4 — Bank Config

**Logic:** unchanged.

**Render changes:**
- All frame borders → Unicode (swap primitives)
- Remove `isSaveFlashActive()` check in info panel — header pulse handles save feedback
- Minimal changes — mostly primitive swap

---

## 6. Tool 5 — Settings

**Logic:** unchanged.

**Render changes:**
- All frame borders → Unicode
- Category sections already use `drawSection()` — will automatically get `╟── LABEL ──╢`
- Remove `isSaveFlashActive()` check if present
- Minimal changes

---

## 7. Tool 6 — Pot Mapping

**Logic:** unchanged.

**Render changes:**
- All frame borders → Unicode
- Two-column separator: `│` (already Unicode single-line, fits the scheme)
- Pool line: wrap in `drawFrameLine()` instead of raw `Serial.printf`
- Remove `isSaveFlashActive()` check if present
- Send `ITERM_RESIZE` at `run()` entry

---

## 8. Terminal Script Sync

**File:** `ItermCode/vt100_serial_terminal.py`

Already at `TARGET_COLS=120, TARGET_ROWS=50`. No changes needed unless we discover UTF-8 passthrough issues (the script passes raw bytes, should work).

---

## 9. Verification Checklist

After each tool:
1. Compile succeeds (`pio run`)
2. Content fits 120x50 frame — no wrapping, no truncation
3. Frame borders align: `║` at columns 1 and 120 on every content line
4. Cell grid aligns: 12 columns * 6 chars + 1 border = 73 chars, centered in 116-char inner
5. Unicode renders correctly in iTerm2 (no garbled chars)
6. Reverse-video amber renders in header
7. NVS badge shows green/yellow correctly
8. Save flash: 3 pulses in ~120ms, info panel not blocked
9. iTerm2 progress bar shows during ordering/calibration
10. Badge and tab title visible
11. All interactions still work (navigate, edit, save, defaults, quit)
12. No logic regressions (state machines, NVS save, input handling)
