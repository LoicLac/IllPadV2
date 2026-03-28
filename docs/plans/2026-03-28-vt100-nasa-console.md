# VT100 NASA Console Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform the ILLPAD48 setup terminal from ASCII frame + basic grids to a cinematic Amber Phosphor CRT console with Unicode box drawing, cell grids, and iTerm2 integration.

**Architecture:** Rewrite render primitives in SetupUI (Unicode borders, cell grid, amber palette, header pulse flash). Then update each tool's render blocks to use the new primitives — logic/state machines stay untouched. Tool 1 (Calibration) excluded from this plan.

**Tech Stack:** ESP32-S3 Arduino (C++17), PlatformIO, VT100/ANSI escape sequences, iTerm2 proprietary OSC sequences, Unicode box drawing (UTF-8)

**Build command:** `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

**Spec:** `docs/specs/2026-03-28-vt100-nasa-console-design.md`

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `src/setup/SetupUI.h` | Modify | Add Unicode defines, iTerm2 defines, new drawCellGrid() signature, remove isSaveFlashActive()/checkSaveFlash(), add drawConsoleHeaderFlash() |
| `src/setup/SetupUI.cpp` | Modify | Rewrite all frame primitives to Unicode, rewrite visibleLen() for UTF-8, new drawCellGrid(), new flashSaved() with inline pulse, new printMainMenu(), add printRepeatStr() |
| `src/setup/SetupManager.cpp` | Modify | Add iTerm2 init sequences at setup entry |
| `src/setup/ToolPadOrdering.cpp` | Modify | Replace drawGrid() calls with drawCellGrid(), add progress bar, add ITERM_RESIZE |
| `src/setup/ToolPadRoles.cpp` | Modify | Replace custom drawGrid()/drawPool() with drawCellGrid()+drawFrameLine(), remove isSaveFlashActive() |
| `src/setup/ToolBankConfig.cpp` | Modify | Remove isSaveFlashActive() check, add ITERM_RESIZE |
| `src/setup/ToolSettings.cpp` | Modify | Add ITERM_RESIZE |
| `src/setup/ToolPotMapping.cpp` | Modify | Wrap drawPoolLine() in drawFrameLine(), remove isSaveFlashActive(), add ITERM_RESIZE |

---

## Task 1: SetupUI.h — Add Unicode + iTerm2 Defines and Update API

**Files:**
- Modify: `src/setup/SetupUI.h`

- [ ] **Step 1: Add Unicode box drawing defines after existing VT_ defines (after line 35)**

```cpp
// Unicode box drawing — double-line (outer frame)
#define UNI_TL  "\xe2\x95\x94"  // ╔
#define UNI_TR  "\xe2\x95\x97"  // ╗
#define UNI_BL  "\xe2\x95\x9a"  // ╚
#define UNI_BR  "\xe2\x95\x9d"  // ╝
#define UNI_H   "\xe2\x95\x90"  // ═
#define UNI_V   "\xe2\x95\x91"  // ║
#define UNI_LT  "\xe2\x95\xa0"  // ╠
#define UNI_RT  "\xe2\x95\xa3"  // ╣

// Unicode box drawing — single-line (section separators)
#define UNI_SLT "\xe2\x95\x9f"  // ╟
#define UNI_SRT "\xe2\x95\xa2"  // ╢
#define UNI_SH  "\xe2\x94\x80"  // ─

// Unicode box drawing — single-line (cell grid)
#define UNI_CTL "\xe2\x94\x8c"  // ┌
#define UNI_CTR "\xe2\x94\x90"  // ┐
#define UNI_CBL "\xe2\x94\x94"  // └
#define UNI_CBR "\xe2\x94\x98"  // ┘
#define UNI_CH  "\xe2\x94\x80"  // ─ (same as UNI_SH)
#define UNI_CV  "\xe2\x94\x82"  // │
#define UNI_CLT "\xe2\x94\x9c"  // ├
#define UNI_CRT "\xe2\x94\xa4"  // ┤
#define UNI_CT  "\xe2\x94\xac"  // ┬
#define UNI_CB  "\xe2\x94\xb4"  // ┴
#define UNI_CX  "\xe2\x94\xbc"  // ┼

// iTerm2 proprietary sequences
#define ITERM_SET_BG       "\033]1337;SetColors=bg=1a0e00\007"
#define ITERM_SET_FG       "\033]1337;SetColors=fg=ffaa33\007"
#define ITERM_TAB_TITLE    "\033]0;ILLPAD48 Setup\007"
#define ITERM_BADGE        "\033]1337;SetBadgeFormat=SUxMUEFENDg=\007"
#define ITERM_RESIZE       "\033[8;50;120t"
#define ITERM_PROGRESS_FMT "\033]9;4;1;%d\007"
#define ITERM_PROGRESS_END "\033]9;4;0;\007"
```

- [ ] **Step 2: Add GRID_ROLES to the GridMode enum (around line 50)**

Replace:
```cpp
enum GridMode : uint8_t {
  GRID_BASELINE,
  GRID_MEASUREMENT,
  GRID_ORDERING
};
```
With:
```cpp
enum GridMode : uint8_t {
  GRID_BASELINE,
  GRID_MEASUREMENT,
  GRID_ORDERING,
  GRID_ROLES
};
```

- [ ] **Step 3: Update the public API — add new methods, remove save flash timer**

Add to public section (after drawControlBar):
```cpp
  // New: 4x12 grid with Unicode cell borders
  void drawCellGrid(GridMode mode, uint16_t target, uint16_t baselines[],
                    uint16_t measuredDeltas[], bool done[], int activeKey,
                    uint16_t activeDelta, bool activeIsDone, uint8_t orderMap[],
                    const char roleLabels[][6] = nullptr,
                    const uint8_t roleMap[] = nullptr);

  // iTerm2 terminal initialization (palette, badge, tab title, resize)
  void initTerminal();

  // iTerm2 progress bar (0-100, or -1 to clear)
  void setProgress(int8_t percent);
```

Remove from public:
```cpp
  // REMOVE: bool isSaveFlashActive() const;
  // REMOVE: void checkSaveFlash();
```

- [ ] **Step 4: Update private section — add helpers, remove timer members**

Add to private:
```cpp
  static void printRepeatStr(const char* s, uint8_t n);
  void drawConsoleHeaderFlash(bool flash);
```

Remove from private:
```cpp
  // REMOVE: bool _saveFlashActive;
  // REMOVE: unsigned long _saveFlashTime;
```

- [ ] **Step 5: Compile to verify header changes**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: Compile errors in .cpp files that reference removed members/methods (expected — we fix them in Task 2)

- [ ] **Step 6: Commit**

```bash
git add src/setup/SetupUI.h
git commit -m "SetupUI.h: add Unicode + iTerm2 defines, update API for NASA console"
```

---

## Task 2: SetupUI.cpp — Rewrite All Frame Primitives

**Files:**
- Modify: `src/setup/SetupUI.cpp`

This is the core task. Every rendering primitive changes from ASCII to Unicode.

- [ ] **Step 1: Update constructor — remove _saveFlashActive/_saveFlashTime**

Replace (line 18):
```cpp
  : _leds(nullptr), _saveFlashActive(false), _saveFlashTime(0) {}
```
With:
```cpp
  : _leds(nullptr) {}
```

- [ ] **Step 2: Replace visibleLen() with UTF-8 aware version (lines 41-56)**

Replace the entire function:
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
      // Not a UTF-8 continuation byte → count as 1 visible codepoint
      len++;
    }
    s++;
  }
  return len;
}
```

- [ ] **Step 3: Add printRepeatStr() after printRepeat() (after line 60)**

```cpp
void SetupUI::printRepeatStr(const char* s, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) Serial.print(s);
}
```

- [ ] **Step 4: Rewrite drawFrameTop() (lines 66-70)**

Replace:
```cpp
void SetupUI::drawFrameTop() {
  Serial.print(VT_DIM "+");
  printRepeat('=', CONSOLE_W - 2);
  Serial.print("+" VT_RESET VT_CL "\n");
}
```
With:
```cpp
void SetupUI::drawFrameTop() {
  Serial.print(VT_DIM);
  Serial.print(UNI_TL);
  printRepeatStr(UNI_H, CONSOLE_W - 2);
  Serial.print(UNI_TR);
  Serial.print(VT_RESET VT_CL "\n");
}
```

- [ ] **Step 5: Rewrite drawFrameBottom() (lines 72-76)**

Replace with same pattern using `UNI_BL` and `UNI_BR`:
```cpp
void SetupUI::drawFrameBottom() {
  Serial.print(VT_DIM);
  Serial.print(UNI_BL);
  printRepeatStr(UNI_H, CONSOLE_W - 2);
  Serial.print(UNI_BR);
  Serial.print(VT_RESET VT_CL "\n");
}
```

- [ ] **Step 6: Rewrite drawSection() (lines 78-89)**

Replace entire function:
```cpp
void SetupUI::drawSection(const char* label) {
  Serial.print(VT_DIM);
  Serial.print(UNI_SLT);
  Serial.print(UNI_SH UNI_SH " ");
  Serial.print(VT_RESET VT_CYAN);
  Serial.print(label);
  Serial.print(VT_RESET VT_DIM " ");
  uint16_t labelVis = visibleLen(label);
  uint8_t remaining = (CONSOLE_W - 2) - 3 - labelVis - 1;
  printRepeatStr(UNI_SH, remaining);
  Serial.print(UNI_SRT);
  Serial.print(VT_RESET VT_CL "\n");
}
```

- [ ] **Step 7: Rewrite drawFrameLine() (lines 91-112)**

Replace — change `|` to `UNI_V`:
```cpp
void SetupUI::drawFrameLine(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  uint16_t vis = visibleLen(buf);
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET);
  Serial.print("  ");
  Serial.print(buf);
  int16_t pad = (int16_t)(CONSOLE_W - 5) - (int16_t)vis;
  if (pad > 0) {
    for (int16_t i = 0; i < pad; i++) Serial.print(' ');
  }
  Serial.print(" ");
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET VT_CL "\n");
}
```

- [ ] **Step 8: Rewrite drawFrameEmpty() (lines 114-118)**

Replace:
```cpp
void SetupUI::drawFrameEmpty() {
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET);
  printRepeat(' ', CONSOLE_W - 2);
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET VT_CL "\n");
}
```

- [ ] **Step 9: Rewrite drawConsoleHeader() (lines 124-157)**

Replace entire function. The header uses double-line frame, reverse-video amber title, and NVS badge:
```cpp
void SetupUI::drawConsoleHeader(const char* toolName, bool nvsSaved) {
  // Top border
  drawFrameTop();

  // Title line: ║  ▐ ILLPAD48 SETUP CONSOLE ▌     TOOL N: NAME          NVS:OK  ║
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET);
  Serial.print("  ");
  Serial.print(VT_REVERSE VT_BOLD " ILLPAD48 SETUP CONSOLE " VT_RESET);
  Serial.print("     ");
  Serial.print(toolName);

  // NVS badge — right-aligned
  const char* badge = nvsSaved
    ? VT_REVERSE VT_GREEN " NVS:OK " VT_RESET
    : VT_REVERSE VT_YELLOW " NVS:-- " VT_RESET;
  uint16_t badgeVis = 8;  // " NVS:OK " = 8 visible chars
  uint16_t titleVis = 24 + 5 + visibleLen(toolName);  // "ILLPAD48..." + gap + toolName
  int16_t gap = (int16_t)(CONSOLE_W - 5) - (int16_t)titleVis - (int16_t)badgeVis;
  if (gap > 0) {
    for (int16_t i = 0; i < gap; i++) Serial.print(' ');
  }
  Serial.print(badge);
  Serial.print(" ");
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET VT_CL "\n");

  // Separator
  Serial.print(VT_DIM);
  Serial.print(UNI_LT);
  printRepeatStr(UNI_H, CONSOLE_W - 2);
  Serial.print(UNI_RT);
  Serial.print(VT_RESET VT_CL "\n");
}
```

- [ ] **Step 10: Add drawConsoleHeaderFlash() — private helper for pulse**

Add new function:
```cpp
void SetupUI::drawConsoleHeaderFlash(bool flash) {
  // Redraws ONLY the title line (row 2, between frame top and separator)
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET);
  Serial.print("  ");
  if (flash) {
    Serial.print(VT_BOLD " ILLPAD48 SETUP CONSOLE " VT_RESET);
  } else {
    Serial.print(VT_REVERSE VT_BOLD " ILLPAD48 SETUP CONSOLE " VT_RESET);
  }
  // Pad rest of line
  printRepeat(' ', CONSOLE_W - 2 - 2 - 24);
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET VT_CL);
}
```

- [ ] **Step 11: Rewrite flashSaved() — inline 120ms pulse, remove timer pattern**

Replace flashSaved() (lines 173-177) + remove checkSaveFlash() (lines 183-187) + remove isSaveFlashActive() (lines 179-181):
```cpp
void SetupUI::flashSaved() {
  // 3 rapid pulses on the header title line (120ms total, blocking)
  for (uint8_t i = 0; i < 3; i++) {
    vtMoveTo(2, 1);  // Row 2 = title line
    drawConsoleHeaderFlash(true);   // Flash ON (normal text, no reverse)
    delay(20);
    vtMoveTo(2, 1);
    drawConsoleHeaderFlash(false);  // Flash OFF (reverse-video, normal state)
    delay(20);
  }
  if (_leds) _leds->playValidation();
}
```

Delete `checkSaveFlash()` and `isSaveFlashActive()` entirely.

- [ ] **Step 12: Rewrite drawControlBar() (lines 163-167)**

Replace:
```cpp
void SetupUI::drawControlBar(const char* controls) {
  Serial.print(VT_DIM);
  Serial.print(UNI_LT);
  printRepeatStr(UNI_H, CONSOLE_W - 2);
  Serial.print(UNI_RT);
  Serial.print(VT_RESET VT_CL "\n");

  drawFrameLine("%s", controls);

  Serial.print(VT_DIM);
  Serial.print(UNI_BL);
  printRepeatStr(UNI_H, CONSOLE_W - 2);
  Serial.print(UNI_BR);
  Serial.print(VT_RESET VT_CL "\n");
}
```

- [ ] **Step 13: Add initTerminal() and setProgress()**

```cpp
void SetupUI::initTerminal() {
  Serial.print(ITERM_SET_BG);
  Serial.print(ITERM_SET_FG);
  Serial.print(ITERM_TAB_TITLE);
  Serial.print(ITERM_BADGE);
  Serial.print(ITERM_RESIZE);
}

void SetupUI::setProgress(int8_t percent) {
  if (percent < 0) {
    Serial.print(ITERM_PROGRESS_END);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), ITERM_PROGRESS_FMT, (int)percent);
    Serial.print(buf);
  }
}
```

- [ ] **Step 14: Implement drawCellGrid()**

Add the new unified cell grid function. This replaces both `drawGrid()` and `drawRolesGrid()` for callers that want cell borders:

```cpp
void SetupUI::drawCellGrid(
  GridMode mode, uint16_t target, uint16_t baselines[],
  uint16_t measuredDeltas[], bool done[], int activeKey,
  uint16_t activeDelta, bool activeIsDone, uint8_t orderMap[],
  const char roleLabels[][6], const uint8_t roleMap[]
) {
  // Cell grid: 12 columns, 5 chars per cell, single-line Unicode borders
  // Total visible width: 1 + 12*(5+1) = 73 chars — fits in CONSOLE_INNER (116)

  // Build horizontal separators
  // Top:    ┌─────┬─────┬...┬─────┐
  // Mid:    ├─────┼─────┼...┼─────┤
  // Bottom: └─────┴─────┴...┴─────┘

  char topLine[256], midLine[256], botLine[256];
  int tp = 0, mp = 0, bp = 0;
  tp += snprintf(topLine + tp, sizeof(topLine) - tp, "    " VT_DIM);
  mp += snprintf(midLine + mp, sizeof(midLine) - mp, "    " VT_DIM);
  bp += snprintf(botLine + bp, sizeof(botLine) - bp, "    " VT_DIM);

  for (uint8_t col = 0; col < 12; col++) {
    tp += snprintf(topLine + tp, sizeof(topLine) - tp, "%s" UNI_CH UNI_CH UNI_CH UNI_CH UNI_CH,
                   col == 0 ? UNI_CTL : UNI_CT);
    mp += snprintf(midLine + mp, sizeof(midLine) - mp, "%s" UNI_CH UNI_CH UNI_CH UNI_CH UNI_CH,
                   col == 0 ? UNI_CLT : UNI_CX);
    bp += snprintf(botLine + bp, sizeof(botLine) - bp, "%s" UNI_CH UNI_CH UNI_CH UNI_CH UNI_CH,
                   col == 0 ? UNI_CBL : UNI_CB);
  }
  tp += snprintf(topLine + tp, sizeof(topLine) - tp, UNI_CTR VT_RESET);
  mp += snprintf(midLine + mp, sizeof(midLine) - mp, UNI_CRT VT_RESET);
  bp += snprintf(botLine + bp, sizeof(botLine) - bp, UNI_CBR VT_RESET);

  drawFrameLine("%s", topLine);

  for (uint8_t row = 0; row < NUM_SENSORS; row++) {
    char rowBuf[512];
    int pos = 0;
    pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos, "    ");

    for (uint8_t col = 0; col < CHANNELS_PER_SENSOR; col++) {
      int key = row * CHANNELS_PER_SENSOR + col;
      pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos, VT_DIM UNI_CV VT_RESET);

      // Cell content (5 chars visible)
      if (mode == GRID_ROLES && roleLabels && roleMap) {
        if (key == activeKey) {
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                          VT_CYAN VT_BOLD "%5s" VT_RESET, roleLabels[key]);
        } else {
          const char* color;
          switch (roleMap[key]) {
            case 1:    color = VT_BLUE;   break;  // ROLE_BANK
            case 2:    color = VT_GREEN;  break;  // ROLE_SCALE
            case 3:    color = VT_YELLOW; break;  // ROLE_ARP
            case 0xFF: color = VT_RED;    break;  // ROLE_COLLISION
            default:   color = VT_DIM;    break;
          }
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                          "%s%5s" VT_RESET, color, roleLabels[key]);
        }
      } else if (mode == GRID_ORDERING) {
        if (key == activeKey && orderMap) {
          if (activeIsDone) {
            pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                            VT_MAGENTA " *%2u*" VT_RESET, (unsigned)(orderMap[key] + 1));
          } else {
            pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                            VT_CYAN " >%2u<" VT_RESET, (unsigned)(activeDelta + 1));
          }
        } else if (done && done[key] && orderMap) {
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                          VT_GREEN "  %2u " VT_RESET, (unsigned)(orderMap[key] + 1));
        } else {
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                          VT_DIM "  -- " VT_RESET);
        }
      } else if (mode == GRID_BASELINE) {
        uint16_t val = baselines ? baselines[key] : 0;
        uint16_t tol = target / 10;
        int diff = (int)val - (int)target;
        if (diff < 0) diff = -diff;
        const char* color = ((uint16_t)diff <= tol) ? VT_GREEN : VT_YELLOW;
        pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                        "%s %4u" VT_RESET, color, val);
      } else if (mode == GRID_MEASUREMENT) {
        if (key == activeKey) {
          if (activeIsDone) {
            pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                            VT_MAGENTA " *%2u*" VT_RESET, activeDelta);
          } else {
            pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                            VT_CYAN " >%2u<" VT_RESET, activeDelta);
          }
        } else if (done && done[key]) {
          uint16_t d = measuredDeltas ? measuredDeltas[key] : 0;
          const char* color = (d >= 50) ? VT_GREEN : VT_RED;  // CAL_PRESSURE_MIN_DELTA_TO_VALIDATE
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                          "%s %4u" VT_RESET, color, d);
        } else {
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                          VT_DIM "  -- " VT_RESET);
        }
      }
    }
    // Closing border
    pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos, VT_DIM UNI_CV VT_RESET);
    drawFrameLine("%s", rowBuf);

    // Inter-row separator (not after last row)
    if (row < NUM_SENSORS - 1) {
      drawFrameLine("%s", midLine);
    }
  }

  drawFrameLine("%s", botLine);
}
```

- [ ] **Step 15: Rewrite printMainMenu() — Unicode borders + iTerm2 init**

Replace the existing printMainMenu() (lines 202-294) with Unicode version. The structure stays the same but uses the new frame primitives (which are already Unicode). The key change: call `initTerminal()` on first display. Add a static bool:

```cpp
void SetupUI::printMainMenu() {
  // Send iTerm2 init on first call
  static bool termInitDone = false;
  if (!termInitDone) {
    initTerminal();
    termInitDone = true;
  }

  // NVS status checks (unchanged logic)
  Preferences prefs;
  // ... (keep existing NVS check code exactly as-is, lines 204-234)
  // ... statusStr lambda stays the same

  vtFrameStart();
  drawConsoleHeader("MAIN MENU", true);
  drawFrameEmpty();

  drawSection("CONFIGURATION TOOLS");
  drawFrameEmpty();
  drawFrameLine("[1]  Pressure Calibration          " VT_DIM "sensitivity tuning" VT_RESET "                  %s", statusStr(calStatus));
  drawFrameLine("[2]  Pad Ordering                  " VT_DIM "pitch mapping, low to high" VT_RESET "          %s", statusStr(ordStatus));
  drawFrameLine("[3]  Pad Roles                     " VT_DIM "bank / scale / arp pads" VT_RESET "             %s", statusStr(roleStatus));
  drawFrameLine("[4]  Bank Config                   " VT_DIM "NORMAL vs ARPEG, quantize" VT_RESET "           %s", statusStr(bankStatus));
  drawFrameLine("[5]  Settings                      " VT_DIM "preferences & connectivity" VT_RESET "          %s", statusStr(setStatus));
  drawFrameLine("[6]  Pot Mapping                   " VT_DIM "parameter assignments" VT_RESET);
  drawFrameEmpty();

  drawSection("SYSTEM");
  drawFrameEmpty();
  drawFrameLine("[0]  Reboot & Exit Setup");
  drawFrameEmpty();

  drawSection("STATUS");
  drawFrameEmpty();
  drawFrameLine(VT_REVERSE VT_GREEN " ok " VT_RESET " = saved to NVS flash     " VT_DIM "--" VT_RESET " = running on factory defaults");
  drawFrameEmpty();

  drawControlBar("Type 0-6");

  vtFrameEnd();
}
```

- [ ] **Step 16: Compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: Errors in tools that still call `checkSaveFlash()` and `isSaveFlashActive()` — we fix those in subsequent tasks.

- [ ] **Step 17: Commit**

```bash
git add src/setup/SetupUI.cpp
git commit -m "SetupUI.cpp: Unicode box drawing, amber palette, cell grid, header pulse flash"
```

---

## Task 3: Fix Compile — Remove checkSaveFlash/isSaveFlashActive from All Tools

**Files:**
- Modify: `src/setup/ToolPadRoles.cpp`
- Modify: `src/setup/ToolBankConfig.cpp`
- Modify: `src/setup/ToolSettings.cpp`
- Modify: `src/setup/ToolPotMapping.cpp`
- Modify: `src/setup/ToolPadOrdering.cpp`

- [ ] **Step 1: Remove all `_ui->checkSaveFlash()` calls**

Search and delete every line that calls `checkSaveFlash()` in all 5 tool files. These are standalone lines in the main loop — just delete them.

- [ ] **Step 2: Remove all `isSaveFlashActive()` blocks**

In ToolPadRoles.cpp (line 494-498), remove:
```cpp
  if (_ui->isSaveFlashActive()) {
    _ui->drawFrameLine(VT_REVERSE VT_BRIGHT_GREEN " === SAVED === " VT_RESET);
    _ui->drawFrameEmpty();
    _ui->drawFrameEmpty();
    return;
  }
```

In ToolPotMapping.cpp (line 439-444), remove:
```cpp
  if (_ui->isSaveFlashActive()) {
    _ui->drawFrameLine(VT_REVERSE VT_BRIGHT_GREEN " === SAVED === " VT_RESET);
    _ui->drawFrameEmpty();
    _ui->drawFrameEmpty();
    _ui->drawFrameEmpty();
    return;
  }
```

In ToolBankConfig.cpp (line 294-298), remove:
```cpp
      if (_ui->isSaveFlashActive()) {
        _ui->drawFrameLine(VT_REVERSE VT_BRIGHT_GREEN " === SAVED === " VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
      } else if (confirmDefaults) {
```
And change to just:
```cpp
      if (confirmDefaults) {
```

Check ToolSettings.cpp and ToolPadOrdering.cpp for similar patterns and remove.

- [ ] **Step 3: Compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: PASS (all references to removed methods gone)

- [ ] **Step 4: Commit**

```bash
git add src/setup/ToolPadRoles.cpp src/setup/ToolBankConfig.cpp src/setup/ToolSettings.cpp src/setup/ToolPotMapping.cpp src/setup/ToolPadOrdering.cpp
git commit -m "Remove checkSaveFlash/isSaveFlashActive — header pulse replaces banner"
```

---

## Task 4: Tool 2 — Pad Ordering Cell Grid + Progress Bar

**Files:**
- Modify: `src/setup/ToolPadOrdering.cpp`

- [ ] **Step 1: Add ITERM_RESIZE at top of run()**

After `if (!_keyboard || !_leds || !_ui || !_padOrder) return;` (line 34), add:
```cpp
  Serial.print(ITERM_RESIZE);
```

- [ ] **Step 2: Replace all 3 drawGrid() calls with drawCellGrid()**

Line 115 — replace:
```cpp
_ui->drawGrid(GRID_ORDERING, 0, nullptr, nullptr, reviewDone, -1, 0, false, existingOrder);
```
With:
```cpp
_ui->drawCellGrid(GRID_ORDERING, 0, nullptr, nullptr, reviewDone, -1, 0, false, existingOrder);
```

Lines 222-223 — replace:
```cpp
_ui->drawGrid(GRID_ORDERING, 0, nullptr, nullptr, assigned,
         activeKey, (uint16_t)assignedCount, activeIsDone, orderMap);
```
With:
```cpp
_ui->drawCellGrid(GRID_ORDERING, 0, nullptr, nullptr, assigned,
             activeKey, (uint16_t)assignedCount, activeIsDone, orderMap);
```

Line 331 — replace:
```cpp
_ui->drawGrid(GRID_ORDERING, 0, nullptr, nullptr, assigned, -1, 0, false, orderMap);
```
With:
```cpp
_ui->drawCellGrid(GRID_ORDERING, 0, nullptr, nullptr, assigned, -1, 0, false, orderMap);
```

- [ ] **Step 3: Add progress bar in ORD_MEASUREMENT**

After each successful auto-assign (after line 186 `_leds->playValidation()`), add:
```cpp
              _ui->setProgress((int8_t)(assignedCount * 100 / NUM_KEYS));
```

On state exit (before transitioning to ORD_RECAP at line 197 and ORD_DONE at line 309), add:
```cpp
              _ui->setProgress(-1);
```

- [ ] **Step 4: Compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/setup/ToolPadOrdering.cpp
git commit -m "Tool 2 Ordering: cell grid, progress bar, resize"
```

---

## Task 5: Tool 3 — Pad Roles Cell Grid + Pool Rewrite

**Files:**
- Modify: `src/setup/ToolPadRoles.cpp`

- [ ] **Step 1: Add ITERM_RESIZE at top of run()**

After the null check (line ~565), add:
```cpp
  Serial.print(ITERM_RESIZE);
```

- [ ] **Step 2: Rewrite drawGrid() to use drawCellGrid()**

Replace the entire drawGrid() method (lines 291-327) with:
```cpp
void ToolPadRoles::drawGrid() {
  int selectedPad = _gridRow * 12 + _gridCol;
  _ui->drawCellGrid(GRID_ROLES, 0, nullptr, nullptr, nullptr, selectedPad,
                     0, false, nullptr, _roleLabels, _roleMap);
}
```

- [ ] **Step 3: Rewrite drawPool() to use drawFrameLine()**

Replace the entire drawPool() method (lines 336-400). The logic stays identical but output goes through drawFrameLine() instead of raw Serial.printf:

```cpp
void ToolPadRoles::drawPool() {
  int selectedPad = _gridRow * 12 + _gridCol;

  // "none" line (poolLine 0)
  {
    bool isSelectedLine = _editing && (_poolLine == 0);
    if (isSelectedLine) {
      _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET VT_REVERSE " none " VT_RESET);
    } else {
      _ui->drawFrameLine("  " VT_DIM "none" VT_RESET);
    }
  }

  auto drawPoolLine = [&](uint8_t lineNum, const char* label,
                          const char* const* labels, uint8_t count,
                          const char* lineColor) {
    bool isSelectedLine = _editing && (_poolLine == lineNum);
    char buf[256];
    int pos = 0;

    if (isSelectedLine) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, VT_CYAN VT_BOLD "> " VT_RESET VT_DIM "%-6s" VT_RESET " ", label);
    } else {
      pos += snprintf(buf + pos, sizeof(buf) - pos, "  " VT_DIM "%-6s" VT_RESET " ", label);
    }

    for (uint8_t i = 0; i < count; i++) {
      bool isCursor = isSelectedLine && (_poolIdx == i);
      uint8_t owner = findPadWithRole(lineNum, i);
      bool assignedElsewhere = (owner < NUM_KEYS && owner != (uint8_t)selectedPad);

      if (isCursor) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, VT_REVERSE VT_BOLD " %s " VT_RESET " ", labels[i]);
      } else if (_editing) {
        if (assignedElsewhere) {
          pos += snprintf(buf + pos, sizeof(buf) - pos, VT_DIM "%s" VT_RESET " ", labels[i]);
        } else {
          pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%s" VT_RESET " ", lineColor, labels[i]);
        }
      } else {
        if (owner < NUM_KEYS) {
          pos += snprintf(buf + pos, sizeof(buf) - pos, VT_DIM "%s" VT_RESET " ", labels[i]);
        } else {
          pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%s" VT_RESET " ", lineColor, labels[i]);
        }
      }
    }
    _ui->drawFrameLine("%s", buf);
  };

  drawPoolLine(1, "Bank:", POOL_BANK_LABELS, POOL_BANK_COUNT, VT_BLUE);
  drawPoolLine(2, "Scale:", POOL_SCALE_LABELS, POOL_SCALE_COUNT, VT_GREEN);
  drawPoolLine(3, "Arp:", POOL_ARP_LABELS, POOL_ARP_COUNT, VT_YELLOW);
}
```

- [ ] **Step 4: Compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/setup/ToolPadRoles.cpp
git commit -m "Tool 3 Roles: cell grid, pool via drawFrameLine, resize"
```

---

## Task 6: Tool 4 — Bank Config + Tool 5 — Settings (Minimal Unicode Swap)

**Files:**
- Modify: `src/setup/ToolBankConfig.cpp`
- Modify: `src/setup/ToolSettings.cpp`

These tools already use the frame primitives correctly. The Unicode swap happens automatically through the rewritten SetupUI. Only small touch-ups needed.

- [ ] **Step 1: Add ITERM_RESIZE to ToolBankConfig run()**

After `if (!_ui || !_banks) return;` (line 62), add:
```cpp
  Serial.print(ITERM_RESIZE);
```

- [ ] **Step 2: Add ITERM_RESIZE to ToolSettings run()**

After the null check at the start of run(), add:
```cpp
  Serial.print(ITERM_RESIZE);
```

- [ ] **Step 3: Compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add src/setup/ToolBankConfig.cpp src/setup/ToolSettings.cpp
git commit -m "Tool 4+5: add terminal resize at entry"
```

---

## Task 7: Tool 6 — Pot Mapping Pool Rewrite

**Files:**
- Modify: `src/setup/ToolPotMapping.cpp`

- [ ] **Step 1: Add ITERM_RESIZE at top of run()**

After the null checks at the start of run(), add:
```cpp
  Serial.print(ITERM_RESIZE);
```

- [ ] **Step 2: Rewrite drawPoolLine() to use drawFrameLine()**

Replace the entire drawPoolLine() method (lines 399-433). Same logic, output through drawFrameLine():

```cpp
void ToolPotMapping::drawPoolLine() {
  bool inEdit = _editing || _ccEditing;
  const PotMapping* map = currentMapConst();
  uint8_t slot = cursorToSlot();

  char buf[256];
  int pos = 0;
  pos += snprintf(buf + pos, sizeof(buf) - pos, VT_DIM "Pool:" VT_RESET " ");

  for (uint8_t i = 0; i < _poolCount; i++) {
    PotTarget t = _pool[i];
    bool isAssigned = false;
    if (t != TARGET_EMPTY && t != TARGET_MIDI_CC) {
      int8_t owner = findSlotWithTarget(t);
      isAssigned = (owner >= 0 && owner != (int8_t)slot);
    }

    bool isCurrentTarget = (map[slot].target == t);
    if (t == TARGET_MIDI_CC) isCurrentTarget = false;

    bool isCursor = inEdit && (_poolIdx == i);

    if (isCursor) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, VT_REVERSE VT_BOLD " %s " VT_RESET " ", targetName(t));
    } else if (isCurrentTarget && !inEdit) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, VT_CYAN "%s" VT_RESET " ", targetName(t));
    } else if (isAssigned) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, VT_DIM "%s" VT_RESET " ", targetName(t));
    } else if (inEdit) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, VT_BRIGHT_GREEN "%s" VT_RESET " ", targetName(t));
    } else {
      pos += snprintf(buf + pos, sizeof(buf) - pos, "%s ", targetName(t));
    }
  }

  _ui->drawFrameLine("%s", buf);
}
```

- [ ] **Step 3: Compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add src/setup/ToolPotMapping.cpp
git commit -m "Tool 6 Mapping: pool via drawFrameLine, resize"
```

---

## Task 8: SetupManager — iTerm2 Init at Setup Entry

**Files:**
- Modify: `src/setup/SetupManager.cpp`

- [ ] **Step 1: Move iTerm2 init to SetupManager entry**

The `initTerminal()` call in `printMainMenu()` works, but it's cleaner to call it once at setup entry. In `SetupManager::run()`, after line 53 (`delay(200);`), before `_ui.vtClear();`:

```cpp
  _ui.initTerminal();
```

And remove the `static bool termInitDone` block from `printMainMenu()` (since init is now done in SetupManager).

- [ ] **Step 2: Compile**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add src/setup/SetupManager.cpp src/setup/SetupUI.cpp
git commit -m "iTerm2 init at setup entry (palette, badge, title, resize)"
```

---

## Task 9: Final Compile + Cleanup

- [ ] **Step 1: Full clean build**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: PASS. Note flash/RAM usage.

- [ ] **Step 2: Check for any remaining raw Serial.printf in draw methods**

Search for `Serial.printf` and `Serial.print` in all tool render methods. Any that write directly to serial in a rendering context (inside drawScreen or screenDirty blocks) should go through `drawFrameLine()` instead. The old `drawGrid()` and `drawRolesGrid()` in SetupUI can stay for Tool 1 backward compat.

- [ ] **Step 3: Final commit**

```bash
git add -A
git commit -m "VT100 NASA Console: final cleanup and alignment check"
```
