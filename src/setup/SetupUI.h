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
#define VT_CL         "\033[K"
#define VT_SYNC_START "\033[?2026h"
#define VT_SYNC_END   "\033[?2026l"

// =================================================================
// Grid display modes
// =================================================================
enum GridMode : uint8_t {
  GRID_BASELINE,      // Stabilization: colored baselines vs target
  GRID_MEASUREMENT,   // Calibration: measured deltas, active key highlight
  GRID_ORDERING       // Pad ordering: position numbers 1-48
};

// =================================================================
// SetupUI — VT100 terminal + LED feedback + dual input
// =================================================================
class SetupUI {
public:
  SetupUI();

  void begin(LedController* leds);

  // VT100 primitives
  void vtClear();
  void vtHome();
  void vtFrameStart();
  void vtFrameEnd();

  // Header / menus
  void drawHeader(const char* title, const char* rightText);
  void printMainMenu();
  void printSubMenu(const char* title, const char* const* items, uint8_t count);
  void printPrompt(const char* msg);
  void printConfirm(const char* msg);
  void printError(const char* msg);

  // 4x12 pad grid
  // Parameters:
  //   mode          — display mode
  //   target        — GRID_BASELINE: target baseline for coloring
  //   baselines[]   — GRID_BASELINE: live baseline values
  //   measuredDeltas[] — GRID_MEASUREMENT: stored deltas per key
  //   done[]        — which keys are done (calibrated/assigned)
  //   activeKey     — currently touched key (-1=none)
  //   activeDelta   — GRID_MEASUREMENT: live delta for active key
  //                    GRID_ORDERING: next position number (0-based)
  //   activeIsDone  — is active key already done?
  //   orderMap[]    — GRID_ORDERING: padIndex → position (0-based, 0xFF=unassigned)
  void drawGrid(GridMode mode, uint16_t target, uint16_t baselines[],
                uint16_t measuredDeltas[], bool done[], int activeKey,
                uint16_t activeDelta, bool activeIsDone, uint8_t orderMap[]);

  // Pad roles grid — 5-char colored labels per cell
  // roleMap[48]: PadRoleCode per pad (0=none, 1=bank, 2=scale, 3=arp, 0xFF=collision)
  // roleLabels[48][6]: 5-char null-terminated label per pad
  void drawRolesGrid(const uint8_t roleMap[], const char roleLabels[][6], int activeKey);

  // Dual input: serial keyboard + rear button (returns '\r' on button press)
  char readInput();

  // LED feedback during setup
  void showToolActive(uint8_t toolIndex);
  void showPadFeedback(uint8_t padIndex);
  void showCollision(uint8_t padIndex);
  void showSaved();

private:
  LedController* _leds;
};

#endif // SETUP_UI_H
