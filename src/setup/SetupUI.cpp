#include "SetupUI.h"
#include "../core/LedController.h"
#include <Arduino.h>

// =================================================================
// Constructor
// =================================================================

SetupUI::SetupUI() : _leds(nullptr), _btnLast(false) {}

void SetupUI::begin(LedController* leds) {
  _leds = leds;
  _btnLast = (digitalRead(BTN_REAR_PIN) == LOW);
}

// =================================================================
// VT100 Primitives
// =================================================================

void SetupUI::vtClear()      { Serial.print(VT_CLEAR VT_HOME); }
void SetupUI::vtHome()       { Serial.print(VT_HOME); }
void SetupUI::vtFrameStart() { Serial.print(VT_HOME VT_SYNC_START); }
void SetupUI::vtFrameEnd()   { Serial.print("\033[J" VT_SYNC_END); }

// =================================================================
// Header banner
// =================================================================

void SetupUI::drawHeader(const char* title, const char* rightText) {
  Serial.printf(VT_BOLD "========================================================" VT_RESET VT_CL "\n");
  Serial.printf(VT_BOLD "  ILLPAD48 -- %-16s  %s" VT_RESET VT_CL "\n", title, rightText);
  Serial.printf(VT_BOLD "========================================================" VT_RESET VT_CL "\n");
}

// =================================================================
// Main Menu
// =================================================================

void SetupUI::printMainMenu() {
  vtFrameStart();
  Serial.printf(VT_BOLD "========================================================" VT_RESET VT_CL "\n");
  Serial.printf(VT_BOLD "             ILLPAD48 -- SETUP MODE" VT_RESET VT_CL "\n");
  Serial.printf(VT_BOLD "========================================================" VT_RESET VT_CL "\n");
  Serial.printf(VT_CL "\n");
  Serial.printf("  Select tool:" VT_CL "\n");
  Serial.printf(VT_CL "\n");
  Serial.printf("   [1] Pressure Calibration" VT_CL "\n");
  Serial.printf("   [2] Pad Ordering" VT_CL "\n");
  Serial.printf("   [3] Pad Roles" VT_CL "\n");
  Serial.printf("   [4] Bank Config" VT_CL "\n");
  Serial.printf("   [5] Settings" VT_CL "\n");
  Serial.printf(VT_CL "\n");
  Serial.printf("   [0] Reboot & Exit Setup" VT_CL "\n");
  Serial.printf(VT_CL "\n");
  Serial.printf("  Type 0-5" VT_CL "\n");
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
// 4x12 Pad Grid — 3 modes
// =================================================================

void SetupUI::drawGrid(
  GridMode mode,
  uint16_t target,
  uint16_t baselines[],
  uint16_t measuredDeltas[],
  bool     done[],
  int      activeKey,
  uint16_t activeDelta,
  bool     activeIsDone,
  uint8_t  orderMap[]
) {
  static const char* labels[] = { "A", "B", "C", "D" };

  Serial.printf("       Ch0  Ch1  Ch2  Ch3  Ch4  Ch5  Ch6  Ch7  Ch8  Ch9  Ch10 Ch11" VT_CL "\n");

  for (int row = 0; row < NUM_SENSORS; row++) {
    Serial.printf("  %s:  ", labels[row]);
    for (int col = 0; col < CHANNELS_PER_SENSOR; col++) {
      int key = row * CHANNELS_PER_SENSOR + col;

      if (mode == GRID_BASELINE) {
        uint16_t val = baselines[key];
        uint16_t tol = target / 10;
        int diff = (int)val - (int)target;
        if (diff < 0) diff = -diff;
        const char* color = ((uint16_t)diff <= tol) ? VT_GREEN : VT_YELLOW;
        Serial.printf("%s%4u " VT_RESET, color, val);
      }
      else if (mode == GRID_MEASUREMENT) {
        if (key == activeKey) {
          if (activeIsDone) {
            Serial.printf(VT_MAGENTA "*%3u*" VT_RESET, activeDelta);
          } else {
            Serial.printf(VT_CYAN ">%3u<" VT_RESET, activeDelta);
          }
        }
        else if (done[key]) {
          uint16_t d = measuredDeltas[key];
          const char* color = (d >= CAL_PRESSURE_MIN_DELTA_TO_VALIDATE) ? VT_GREEN : VT_RED;
          Serial.printf("%s%4u " VT_RESET, color, d);
        }
        else {
          Serial.printf(VT_DIM " --- " VT_RESET);
        }
      }
      else if (mode == GRID_ORDERING) {
        if (key == activeKey && orderMap != nullptr) {
          if (activeIsDone) {
            // Touching already-assigned pad: show existing position in magenta
            Serial.printf(VT_MAGENTA "*%3u*" VT_RESET, (uint16_t)(orderMap[key] + 1));
          } else {
            // Active pad: show next position being assigned
            Serial.printf(VT_CYAN ">%3u<" VT_RESET, activeDelta + 1);  // activeDelta = nextPosition (0-based), display 1-based
          }
        }
        else if (done[key] && orderMap != nullptr) {
          Serial.printf(VT_GREEN "%4u " VT_RESET, (uint16_t)(orderMap[key] + 1));
        }
        else {
          Serial.printf(VT_DIM " --- " VT_RESET);
        }
      }
    }
    Serial.print(VT_CL "\n");
  }
}

// =================================================================
// 4x12 Pad Roles Grid — colored 5-char labels
// =================================================================

void SetupUI::drawRolesGrid(const uint8_t roleMap[], const char roleLabels[][6], int activeKey) {
  static const char* rowLabels[] = { "A", "B", "C", "D" };

  // Column headers (5-char wide)
  Serial.printf("       Ch0   Ch1   Ch2   Ch3   Ch4   Ch5   Ch6   Ch7   Ch8   Ch9   Ch10  Ch11" VT_CL "\n");

  for (int row = 0; row < NUM_SENSORS; row++) {
    Serial.printf("  %s: ", rowLabels[row]);
    for (int col = 0; col < CHANNELS_PER_SENSOR; col++) {
      int key = row * CHANNELS_PER_SENSOR + col;

      if (key == activeKey) {
        // Highlighted: cyan with brackets
        Serial.printf(VT_CYAN VT_BOLD ">%.5s<" VT_RESET, roleLabels[key]);
      } else {
        const char* color;
        switch (roleMap[key]) {
          case 1:    color = VT_BLUE;   break;  // ROLE_BANK
          case 2:    color = VT_GREEN;  break;  // ROLE_SCALE
          case 3:    color = VT_YELLOW; break;  // ROLE_ARP
          case 0xFF: color = VT_RED;    break;  // ROLE_COLLISION
          default:   color = VT_DIM;    break;  // ROLE_NONE
        }
        Serial.printf("%s %-.5s" VT_RESET, color, roleLabels[key]);
      }
    }
    Serial.print(VT_CL "\n");
  }

  // Legend
  Serial.printf(VT_CL "\n");
  Serial.printf("  " VT_BLUE "Bank" VT_RESET "  "
                VT_GREEN "Scale" VT_RESET "  "
                VT_YELLOW "Arp" VT_RESET "  "
                VT_RED "Collision" VT_RESET "  "
                VT_DIM "---" VT_RESET VT_CL "\n");
  Serial.printf("  Bank: BNK 1-8   Scale: RootA-G, ModIo/Do/Ph/Ly/Mx/Ae/Lo, CHROM   Arp: HOLD, PL/ST" VT_CL "\n");
}

// =================================================================
// Dual Input: serial + rear button = ENTER
// =================================================================

char SetupUI::readInput() {
  if (Serial.available()) {
    return Serial.read();
  }
  bool cur = (digitalRead(BTN_REAR_PIN) == LOW);
  bool edge = cur && !_btnLast;
  _btnLast = cur;
  return edge ? '\r' : 0;
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
  // Rapid blink to signal collision — reuse validation pattern
  if (_leds) _leds->playValidation();
}

void SetupUI::showSaved() {
  if (_leds) _leds->playValidation();
}
