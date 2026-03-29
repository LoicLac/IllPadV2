#ifndef SETUP_UI_H
#define SETUP_UI_H

#include <stdint.h>
#include "../core/HardwareConfig.h"

class LedController;

// =================================================================
// VT100 ANSI Escape Codes
// =================================================================
#define VT_CLEAR      "\033[2J"
#define VT_HOME       "\033[H"
#define VT_BOLD       "\033[1m"
#define VT_DIM        "\033[2m"
#define VT_REVERSE    "\033[7m"
#define VT_RESET      "\033[0m"
#define VT_GREEN      "\033[32m"
#define VT_YELLOW     "\033[33m"
#define VT_RED        "\033[31m"
#define VT_CYAN       "\033[36m"
#define VT_MAGENTA    "\033[35m"
#define VT_BLUE       "\033[34m"
#define VT_WHITE      "\033[37m"
#define VT_CL         "\033[K"
#define VT_SYNC_START "\033[?2026h"
#define VT_SYNC_END   "\033[?2026l"

// Bright (high-intensity) variants
#define VT_BRIGHT_WHITE   "\033[97m"
#define VT_BRIGHT_CYAN    "\033[96m"
#define VT_BRIGHT_GREEN   "\033[92m"
#define VT_BRIGHT_YELLOW  "\033[93m"
#define VT_BRIGHT_RED     "\033[91m"
#define VT_BRIGHT_BLUE    "\033[94m"

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

// =================================================================
// Console Layout Constants
// =================================================================
static const uint8_t CONSOLE_W     = 120;  // Total line width
static const uint8_t CONSOLE_INNER = 116;  // Usable content between "| " and " |"

// =================================================================
// Grid display modes
// =================================================================
enum GridMode : uint8_t {
  GRID_BASELINE,      // Stabilization: colored baselines vs target
  GRID_MEASUREMENT,   // Calibration: measured deltas, active key highlight
  GRID_ORDERING,      // Pad ordering: position numbers 1-48
  GRID_ROLES          // Pad roles: colored 5-char labels
};

// =================================================================
// SetupUI — NASA Console VT100 Terminal + LED Feedback
// =================================================================
class SetupUI {
public:
  SetupUI();

  void begin(LedController* leds);

  // --- VT100 primitives ---
  void vtClear();
  void vtHome();
  void vtFrameStart();
  void vtFrameEnd();
  void vtMoveTo(uint8_t row, uint8_t col);

  // --- Console frame primitives ---
  // Double-line frame:  +====...====+
  void drawFrameTop();
  void drawFrameBottom();

  // Section separator:  +-- LABEL ----...----+
  void drawSection(const char* label);

  // Content line:  |  content...padded...  |
  // fmt is printf-style. Auto-pads with spaces to CONSOLE_INNER.
  void drawFrameLine(const char* fmt, ...);

  // Empty line inside frame:  |                                  |
  void drawFrameEmpty();

  // --- Console header (reverse-video title bar) ---
  // +====================================================================+
  // |  #### ILLPAD48 SETUP CONSOLE ####     TOOL N: NAME    [NVS:OK]    |
  // +====================================================================+
  void drawConsoleHeader(const char* toolName, bool nvsSaved);

  // --- Control bar (fixed bottom) ---
  // +====================================================================+
  // | [^v<>] NAV  [RET] EDIT  [d] DFLT  [q] EXIT                       |
  // +====================================================================+
  void drawControlBar(const char* controls);

  // --- 4x12 cell grid (Unicode borders, 4 modes) ---
  void drawCellGrid(GridMode mode, uint16_t target, uint16_t baselines[],
                    uint16_t measuredDeltas[], bool done[], int activeKey,
                    uint16_t activeDelta, bool activeIsDone, uint8_t orderMap[],
                    const char roleLabels[][6] = nullptr,
                    const uint8_t roleMap[] = nullptr);

  // --- iTerm2 terminal integration ---
  void initTerminal();
  void setProgress(int8_t percent);

  // --- Save feedback ---
  // Flash reverse-video header pulse + LED flash (120ms inline)
  void flashSaved();

  // --- Legacy header (kept for backward compat during migration) ---
  void drawHeader(const char* title, const char* rightText);

  // --- Main menu ---
  void printMainMenu();

  // --- Sub menu helpers ---
  void printSubMenu(const char* title, const char* const* items, uint8_t count);
  void printPrompt(const char* msg);
  void printConfirm(const char* msg);
  void printError(const char* msg);

  // --- 4x12 pad grid (3 modes) ---
  void drawGrid(GridMode mode, uint16_t target, uint16_t baselines[],
                uint16_t measuredDeltas[], bool done[], int activeKey,
                uint16_t activeDelta, bool activeIsDone, uint8_t orderMap[]);

  // --- Pad roles grid (colored 5-char labels) ---
  void drawRolesGrid(const uint8_t roleMap[], const char roleLabels[][6], int activeKey);

  // --- LED feedback ---
  void showToolActive(uint8_t toolIndex);
  void showPadFeedback(uint8_t padIndex);
  void showCollision(uint8_t padIndex);
  void showSaved();  // LED-only (legacy). Prefer flashSaved() for VT100+LED.

private:
  LedController* _leds;
  const char* _lastToolName;
  bool _lastNvsSaved;

  // Helper: count visible chars in a string (skips ANSI escape sequences + UTF-8)
  static uint16_t visibleLen(const char* s);

  // Helper: print N copies of a character
  static void printRepeat(char c, uint8_t n);

  // Helper: print N copies of a multi-byte string (for Unicode box chars)
  static void printRepeatStr(const char* s, uint8_t n);

  // Helper: draw header line with flash state (for save pulse animation)
  void drawConsoleHeaderFlash(bool flash);
};

#endif // SETUP_UI_H
