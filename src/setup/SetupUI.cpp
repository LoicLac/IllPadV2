#include "SetupUI.h"
#include "../core/LedController.h"
#include "../core/KeyboardData.h"
#include "../managers/NvsManager.h"
#include "../managers/PotRouter.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdarg.h>

// =================================================================
// Constructor
// =================================================================

SetupUI::SetupUI()
  : _leds(nullptr), _lastNvsSaved(true) { _lastToolName[0] = '\0'; }

void SetupUI::begin(LedController* leds) {
  _leds = leds;
}

// =================================================================
// VT100 Primitives
// =================================================================

void SetupUI::vtClear()      { Serial.print(VT_CLEAR VT_HOME); }
void SetupUI::vtHome()       { Serial.print(VT_HOME); }
void SetupUI::vtFrameStart() { Serial.print(VT_HOME VT_SYNC_START); }
void SetupUI::vtFrameEnd()   { Serial.print("\033[J" VT_SYNC_END); }

void SetupUI::vtMoveTo(uint8_t row, uint8_t col) {
  Serial.printf("\033[%d;%dH", row, col);
}

// =================================================================
// Helpers
// =================================================================

uint16_t SetupUI::visibleLen(const char* s) {
  uint16_t len = 0;
  bool inEsc = false;
  while (*s) {
    if (*s == '\033') {
      inEsc = true;
    } else if (inEsc) {
      if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) inEsc = false;
    } else if ((*s & 0xC0) != 0x80) {
      len++;
    }
    s++;
  }
  return len;
}

void SetupUI::printRepeat(char c, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) Serial.print(c);
}

void SetupUI::printRepeatStr(const char* s, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) Serial.print(s);
}

// =================================================================
// Console Frame Primitives
// =================================================================

void SetupUI::drawFrameTop() {
  Serial.print(VT_DIM);
  Serial.print(UNI_TL);
  printRepeatStr(UNI_H, CONSOLE_W - 2);
  Serial.print(UNI_TR);
  Serial.print(VT_RESET VT_CL "\n");
}

void SetupUI::drawFrameBottom() {
  Serial.print(VT_DIM);
  Serial.print(UNI_BL);
  printRepeatStr(UNI_H, CONSOLE_W - 2);
  Serial.print(UNI_BR);
  Serial.print(VT_RESET VT_CL "\n");
}

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

void SetupUI::drawFrameLine(const char* fmt, ...) {
  char buf[512];
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
  yield();  // Feed RTOS watchdog — prevents timeout during large VT100 frames
}

void SetupUI::drawFrameEmpty() {
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET);
  printRepeat(' ', CONSOLE_W - 2);
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET VT_CL "\n");
  yield();
}

// =================================================================
// Console Header — reverse-video title bar with NVS badge
// =================================================================

void SetupUI::drawConsoleHeader(const char* toolName, bool nvsSaved) {
  strncpy(_lastToolName, toolName, sizeof(_lastToolName) - 1);
  _lastToolName[sizeof(_lastToolName) - 1] = '\0';
  _lastNvsSaved = nvsSaved;
  drawFrameTop();

  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET);
  Serial.print("  ");
  Serial.print(VT_REVERSE VT_BOLD " ILLPAD48 SETUP CONSOLE " VT_RESET);
  Serial.print("     ");
  Serial.print(toolName);

  const char* badge = nvsSaved
    ? VT_REVERSE VT_GREEN " NVS:OK " VT_RESET
    : VT_REVERSE VT_YELLOW " NVS:-- " VT_RESET;
  uint16_t badgeVis = 8;
  uint16_t titleVis = 24 + 5 + visibleLen(toolName);
  int16_t gap = (int16_t)(CONSOLE_W - 5) - (int16_t)titleVis - (int16_t)badgeVis;
  if (gap > 0) {
    for (int16_t i = 0; i < gap; i++) Serial.print(' ');
  }
  Serial.print(badge);
  Serial.print(" ");
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET VT_CL "\n");

  Serial.print(VT_DIM);
  Serial.print(UNI_LT);
  printRepeatStr(UNI_H, CONSOLE_W - 2);
  Serial.print(UNI_RT);
  Serial.print(VT_RESET VT_CL "\n");
}

// =================================================================
// Control Bar — fixed at bottom
// =================================================================

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

// =================================================================
// Save Flash — 120ms inline pulse
// =================================================================

void SetupUI::drawConsoleHeaderFlash(bool flash) {
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET);
  Serial.print("  ");
  if (flash) {
    Serial.print(VT_BOLD " ILLPAD48 SETUP CONSOLE " VT_RESET);
  } else {
    Serial.print(VT_REVERSE VT_BOLD " ILLPAD48 SETUP CONSOLE " VT_RESET);
  }
  Serial.print("     ");
  Serial.print(_lastToolName);

  const char* badge = _lastNvsSaved
    ? VT_REVERSE VT_GREEN " NVS:OK " VT_RESET
    : VT_REVERSE VT_YELLOW " NVS:-- " VT_RESET;
  uint16_t badgeVis = 8;
  uint16_t titleVis = 24 + 5 + visibleLen(_lastToolName);
  int16_t gap = (int16_t)(CONSOLE_W - 5) - (int16_t)titleVis - (int16_t)badgeVis;
  if (gap > 0) {
    for (int16_t i = 0; i < gap; i++) Serial.print(' ');
  }
  Serial.print(badge);
  Serial.print(" ");
  Serial.print(VT_DIM);
  Serial.print(UNI_V);
  Serial.print(VT_RESET VT_CL);
}

void SetupUI::flashSaved() {
  for (uint8_t i = 0; i < 3; i++) {
    vtMoveTo(2, 1);
    drawConsoleHeaderFlash(true);
    Serial.flush();
    delay(50);
    vtMoveTo(2, 1);
    drawConsoleHeaderFlash(false);
    Serial.flush();
    delay(50);
  }
  if (_leds) _leds->playValidation();
}

// =================================================================
// iTerm2 Terminal Integration
// =================================================================

void SetupUI::initTerminal() {
  // Only resize — OSC sequences (palette, badge, title) don't survive
  // serial passthrough reliably. Palette is handled by the Python terminal script.
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

// =================================================================
// Legacy Header (backward compat during migration)
// =================================================================

void SetupUI::drawHeader(const char* title, const char* rightText) {
  drawConsoleHeader(title, true);
  (void)rightText;  // Absorbed into toolName in new API
}

// =================================================================
// Main Menu
// =================================================================

void SetupUI::printMainMenu() {
  // Unified NVS status checks via descriptor table
  char toolStatus[7];
  for (uint8_t t = 0; t < 7; t++) {
    bool allOk = true;
    for (uint8_t d = TOOL_NVS_FIRST[t]; d <= TOOL_NVS_LAST[t]; d++) {
      if (!NvsManager::checkBlob(NVS_DESCRIPTORS[d].ns, NVS_DESCRIPTORS[d].key,
                                  NVS_DESCRIPTORS[d].magic, NVS_DESCRIPTORS[d].version,
                                  NVS_DESCRIPTORS[d].size)) {
        allOk = false;
        break;
      }
    }
    toolStatus[t] = allOk ? 'v' : '!';
  }
  char calStatus  = toolStatus[0];
  char ordStatus  = toolStatus[1];
  char roleStatus = toolStatus[2];
  char bankStatus = toolStatus[3];
  char setStatus  = toolStatus[4];
  char potStatus  = toolStatus[5];
  char ledStatus  = toolStatus[6];

  auto statusStr = [](char s) -> const char* {
    if (s == 'v') return VT_REVERSE VT_GREEN " ok " VT_RESET;
    if (s == '!') return VT_DIM " -- " VT_RESET;
    return "    ";
  };

  vtFrameStart();

  // Header
  drawConsoleHeader("MAIN MENU", true);

  drawFrameEmpty();

  // Tools section
  drawSection("CONFIGURATION TOOLS");
  drawFrameEmpty();
  drawFrameLine("[1]  Pressure Calibration          " VT_DIM "sensitivity tuning" VT_RESET "                  %s", statusStr(calStatus));
  drawFrameLine("[2]  Pad Ordering                  " VT_DIM "pitch mapping, low to high" VT_RESET "          %s", statusStr(ordStatus));
  drawFrameLine("[3]  Pad Roles                     " VT_DIM "bank / scale / arp pads" VT_RESET "             %s", statusStr(roleStatus));
  drawFrameLine("[4]  Bank Config                   " VT_DIM "NORMAL vs ARPEG, quantize" VT_RESET "           %s", statusStr(bankStatus));
  drawFrameLine("[5]  Settings                      " VT_DIM "preferences & connectivity" VT_RESET "          %s", statusStr(setStatus));
  drawFrameLine("[6]  Pot Mapping                   " VT_DIM "parameter assignments" VT_RESET "               %s", statusStr(potStatus));
  drawFrameLine("[7]  LED Settings                  " VT_DIM "colors, intensity & timing" VT_RESET "          %s", statusStr(ledStatus));
  drawFrameEmpty();

  // System section
  drawSection("SYSTEM");
  drawFrameEmpty();
  drawFrameLine("[0]  Reboot & Exit Setup");
  drawFrameEmpty();

  // Status section
  drawSection("STATUS");
  drawFrameEmpty();
  drawFrameLine(VT_REVERSE VT_GREEN " ok " VT_RESET " = saved to NVS flash     " VT_DIM "--" VT_RESET " = running on factory defaults");
  drawFrameEmpty();

  // Control bar
  drawControlBar("Type 0-7");

  vtFrameEnd();
}

// =================================================================
// Sub menu helpers
// =================================================================

void SetupUI::printSubMenu(const char* title, const char* const* items, uint8_t count) {
  Serial.printf(VT_BOLD "  %s" VT_RESET VT_CL "\n", title);
  Serial.printf(VT_CL "\n");
  for (uint8_t i = 0; i < count; i++) {
    Serial.printf("   [%d] %s" VT_CL "\n", i + 1, items[i]);
  }
}

void SetupUI::printPrompt(const char* msg) {
  Serial.printf("  %s" VT_CL "\n", msg);
}

void SetupUI::printConfirm(const char* msg) {
  Serial.printf(VT_GREEN "  %s" VT_RESET VT_CL "\n", msg);
}

void SetupUI::printError(const char* msg) {
  Serial.printf(VT_RED "  %s" VT_RESET VT_CL "\n", msg);
}

// =================================================================
// 4x12 Cell Grid — Unicode borders, 4 modes
// =================================================================

void SetupUI::drawCellGrid(
  GridMode mode, uint16_t target, uint16_t baselines[],
  uint16_t measuredDeltas[], bool done[], int activeKey,
  uint16_t activeDelta, bool activeIsDone, uint8_t orderMap[],
  const char roleLabels[][6], const uint8_t roleMap[]
) {
  // Build horizontal separator lines
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

  for (uint8_t row = 0; row < 4; row++) {
    char rowBuf[512];
    int pos = 0;
    pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos, "    ");

    for (uint8_t col = 0; col < 12; col++) {
      int key = row * 12 + col;
      pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos, VT_DIM UNI_CV VT_RESET);

      if (mode == GRID_ROLES && roleLabels && roleMap) {
        if (key == activeKey) {
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos,
                          VT_CYAN VT_BOLD "%5s" VT_RESET, roleLabels[key]);
        } else {
          const char* color;
          switch (roleMap[key]) {
            case 1:    color = VT_BLUE;       break;  // Bank
            case 2:    color = VT_GREEN;      break;  // Root
            case 3:    color = VT_CYAN;       break;  // Mode
            case 4:    color = VT_YELLOW;     break;  // Octave
            case 5:    color = VT_MAGENTA;    break;  // Hold
            case 6:    color = VT_BRIGHT_RED; break;  // Play/Stop
            case 0xFF: color = VT_RED;        break;  // Collision
            default:   color = VT_DIM;        break;
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
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos, VT_DIM "  -- " VT_RESET);
        }
      } else if (mode == GRID_BASELINE) {
        uint16_t val = baselines ? baselines[key] : 0;
        uint16_t tol = target / 10;
        int diff = (int)val - (int)target;
        if (diff < 0) diff = -diff;
        const char* color = ((uint16_t)diff <= tol) ? VT_GREEN : VT_YELLOW;
        pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos, "%s %4u" VT_RESET, color, val);
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
          const char* color = (d >= 50) ? VT_GREEN : VT_RED;
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos, "%s %4u" VT_RESET, color, d);
        } else {
          pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos, VT_DIM "  -- " VT_RESET);
        }
      }
    }
    pos += snprintf(rowBuf + pos, sizeof(rowBuf) - pos, VT_DIM UNI_CV VT_RESET);
    drawFrameLine("%s", rowBuf);

    if (row < 3) {
      drawFrameLine("%s", midLine);
    }
  }

  drawFrameLine("%s", botLine);
}

// =================================================================
// Legacy Grid Wrappers (delegate to drawCellGrid)
// =================================================================

void SetupUI::drawGrid(
  GridMode mode, uint16_t target, uint16_t baselines[],
  uint16_t measuredDeltas[], bool done[], int activeKey,
  uint16_t activeDelta, bool activeIsDone, uint8_t orderMap[]
) {
  drawCellGrid(mode, target, baselines, measuredDeltas, done,
               activeKey, activeDelta, activeIsDone, orderMap);
}

void SetupUI::drawRolesGrid(const uint8_t roleMap[], const char roleLabels[][6], int activeKey) {
  drawCellGrid(GRID_ROLES, 0, nullptr, nullptr, nullptr,
               activeKey, 0, false, nullptr, roleLabels, roleMap);
}

// =================================================================
// LED Feedback
// =================================================================

void SetupUI::showToolActive(uint8_t toolIndex) {
  if (_leds) _leds->setCurrentBank(toolIndex);
}

void SetupUI::showPadFeedback(uint8_t padIndex) {
  (void)padIndex;
  if (_leds) _leds->playValidation();
}

void SetupUI::showCollision(uint8_t padIndex) {
  (void)padIndex;
  if (_leds) _leds->playValidation();
}

void SetupUI::showSaved() {
  if (_leds) _leds->playValidation();
}
