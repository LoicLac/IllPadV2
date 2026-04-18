#ifndef SETUP_UI_H
#define SETUP_UI_H

#include <stdint.h>
#include "../core/HardwareConfig.h"
#include "InputParser.h"

class LedController;

// =================================================================
// Confirmation result — used by parseConfirm() to unify y/n handlers
// =================================================================
enum ConfirmResult : uint8_t {
  CONFIRM_PENDING = 0,   // no decisive input yet — keep prompt visible
  CONFIRM_YES     = 1,   // user pressed y/Y
  CONFIRM_NO      = 2,   // any other key → cancel (preserves legacy behavior)
};

// =================================================================
// Control bar templates — unified across tools
// Pair each with the matching parseConfirm*() semantics in the handler.
// =================================================================
// Loose confirm: y/Y = yes, any other key cancels (most tools)
#define CBAR_CONFIRM_ANY     "\033[2m[y] confirm  [any] cancel\033[0m"
// Strict confirm: y/Y = yes, n/N = no, everything else ignored (Calibration, reboot)
#define CBAR_CONFIRM_STRICT  "\033[1m[y] YES  [n] NO\033[0m"
// Group separator for control bars — use between logical groups of keys
// Usage:  VT_DIM "[^v] NAV" CBAR_SEP "[RET] EDIT" CBAR_SEP "[q] EXIT" VT_RESET
#define CBAR_SEP             "  \xe2\x94\x82  "  // 2sp + │ + 2sp

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
#define VT_ORANGE     "\033[38;5;208m"
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

// Cockpit accents (Apollo panel)
#define UNI_RIVET  "\xe2\x97\x89"  // ◉ (fisheye) — rivet/bolt head
#define UNI_LED_ON   "\xe2\x97\x8f"  // ● BLACK CIRCLE — lit indicator
#define UNI_LED_OFF  "\xe2\x97\x8b"  // ○ WHITE CIRCLE — dim indicator
#define UNI_BAR_FULL "\xe2\x96\x88"  // █ FULL BLOCK — gauge filled
#define UNI_BAR_EMPTY "\xe2\x96\x91" // ░ LIGHT SHADE — gauge empty

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

  // --- Confirmation input parsing ---
  // Factors the inline y/n handler duplicated across all tools.
  // Preserves legacy "any key cancels" semantics:
  //   - NAV_NONE           → CONFIRM_PENDING
  //   - NAV_CHAR y/Y       → CONFIRM_YES
  //   - anything else      → CONFIRM_NO
  // Callers still own the prompt rendering.
  static ConfirmResult parseConfirm(const NavEvent& ev);

  // =================================================================
  // Cockpit primitives — Apollo-inspired panel accents
  // =================================================================

  // Step indicator breadcrumb rendered as a full frame line.
  // Layout:  (1) LABEL ─▸ (2) LABEL ─▸ [[3 LABEL]] ─▸ (4) LABEL ─▸ ...
  // - current step is highlighted reverse-cyan-bold
  // - past steps dim green (completed)
  // - future steps dim white
  void drawStepIndicator(const char* const* labels, uint8_t count, uint8_t current);

  // Labelled gauge bar: "  LABEL   [████░░░░░░░░░]  NN%"
  // percent 0..100, label left-aligned, bar right of label, % value at far right.
  // If selected=true, label + bar are rendered in cyan (cursor highlight).
  void drawGaugeLine(const char* label, uint8_t percent, bool selected = false);

  // Segmented readout ("Nixie-style") for a single headline value.
  // Draws 3 successive frame lines : top border, digits row with unit, bottom border.
  // digits=3 renders ┌───┬───┬───┐, etc. value auto-padded with leading zeros.
  void drawSegmentedValue(const char* label, uint32_t value, uint8_t digits, const char* unit);

  // Status cluster — a row of ●/○ indicators with inline labels.
  // Rendered as a single frame line. Lit items use VT_GREEN, unlit VT_DIM.
  struct StatusItem {
    const char* label;
    bool        lit;
  };
  void drawStatusCluster(const StatusItem* items, uint8_t count);

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

  // --- LED feedback ---
  void showToolActive(uint8_t toolIndex);
  void showPadFeedback(uint8_t padIndex);
  void showCollision(uint8_t padIndex);

private:
  LedController* _leds;
  char _lastToolName[64];
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
