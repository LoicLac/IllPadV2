#include "ToolLedSettings.h"
#include "SetupUI.h"
#include "InputParser.h"
#include <Arduino.h>

// =================================================================
// Tool 8 placeholder implementation (Phase 0 step 0.2)
// =================================================================
// See ToolLedSettings.h for rationale. Full 3-page refactor in step 0.8.
// =================================================================

ToolLedSettings::ToolLedSettings()
  : _leds(nullptr), _ui(nullptr) {}

void ToolLedSettings::begin(LedController* leds, SetupUI* ui) {
  _leds = leds;
  _ui = ui;
}

void ToolLedSettings::run() {
  if (!_ui) return;

  _ui->vtClear();
  bool screenDirty = true;
  InputParser input;

  while (true) {
    if (screenDirty) {
      _ui->vtClear();
      _ui->drawConsoleHeader("LED SETTINGS", false);
      _ui->drawFrameTop();
      _ui->drawFrameLine("");
      _ui->drawFrameLine(VT_YELLOW "  Tool 8 — refactor in progress." VT_RESET);
      _ui->drawFrameLine("");
      _ui->drawFrameLine(VT_DIM "  Phase 0 step 0.2 removed the legacy editor." VT_RESET);
      _ui->drawFrameLine(VT_DIM "  Step 0.8 will deliver the new 3-page editor" VT_RESET);
      _ui->drawFrameLine(VT_DIM "  (PATTERNS / COLORS / EVENTS)." VT_RESET);
      _ui->drawFrameLine("");
      _ui->drawFrameLine(VT_DIM "  See docs/superpowers/plans/" VT_RESET);
      _ui->drawFrameLine(VT_DIM "        2026-04-19-phase0-led-refactor-plan.md" VT_RESET);
      _ui->drawFrameLine("");
      _ui->drawFrameLine(VT_DIM "  LED rendering continues normally at runtime —" VT_RESET);
      _ui->drawFrameLine(VT_DIM "  only the setup editor is temporarily disabled." VT_RESET);
      _ui->drawFrameLine("");
      _ui->drawFrameBottom();
      _ui->drawControlBar(VT_DIM "[q] BACK" VT_RESET);
      screenDirty = false;
    }

    NavEvent ev = input.update();
    if (ev.type == NAV_QUIT) break;
    if (ev.type != NAV_NONE) {
      screenDirty = true;  // any other key : redraw (no-op but responsive)
    }
    delay(10);
  }
}
