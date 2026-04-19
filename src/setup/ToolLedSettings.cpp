#include "ToolLedSettings.h"
#include "SetupUI.h"
#include "InputParser.h"
#include "../core/LedController.h"
#include <Arduino.h>

// =================================================================
// Tool 8 — LED Settings (Phase 0 step 0.8a skeleton)
// =================================================================
// See ToolLedSettings.h for architecture. This file is the skeleton :
// 3 pages, inter-page nav via 't', two-step exit via 'q'. Page content
// is stubbed — implementations land in 0.8b / 0.8c / 0.8d.
// =================================================================

ToolLedSettings::ToolLedSettings()
  : _leds(nullptr), _ui(nullptr), _page(PAGE_PATTERNS) {}

void ToolLedSettings::begin(LedController* leds, SetupUI* ui) {
  _leds = leds;
  _ui = ui;
}

// --- Page renderers (stubs in 0.8a) ---

void ToolLedSettings::renderPagePatterns() {
  _ui->drawFrameLine(VT_DIM "  9 palette patterns editable here in step 0.8c :" VT_RESET);
  _ui->drawFrameLine(VT_DIM "  pool nav (UP/DOWN) selects the pattern," VT_RESET);
  _ui->drawFrameLine(VT_DIM "  ENTER enters a field editor for its params." VT_RESET);
  _ui->drawFrameLine("");
  _ui->drawFrameLine(VT_DIM "  Live preview on LED 3-4 (press 'b' to retrigger)." VT_RESET);
}

void ToolLedSettings::renderPageColors() {
  _ui->drawFrameLine(VT_DIM "  15 color slots editable here in step 0.8b :" VT_RESET);
  _ui->drawFrameLine(VT_DIM "  4x4 grid cursor, ENTER opens preset pool + hue editor." VT_RESET);
  _ui->drawFrameLine("");
  _ui->drawFrameLine(VT_DIM "  Preview = SOLID on LED 3-4 with current slot color." VT_RESET);
}

void ToolLedSettings::renderPageEvents() {
  _ui->drawFrameLine(VT_DIM "  ~10 events editable here in step 0.8d :" VT_RESET);
  _ui->drawFrameLine(VT_DIM "  field list ; per row edit pattern + color slot + fgPct." VT_RESET);
  _ui->drawFrameLine(VT_DIM "  RAMP_HOLD events show their rampMs as derived (Tool 6)." VT_RESET);
  _ui->drawFrameLine("");
  _ui->drawFrameLine(VT_DIM "  Preview = trigger the event on LED 3-4 via 'b'." VT_RESET);
}

// --- run() — skeleton page navigation ---

void ToolLedSettings::run() {
  if (!_ui) return;

  _page = PAGE_PATTERNS;

  // Preview mode on : LED 3-4 become free for Tool 8 control
  // (conventions §3.2 — tool drives the LEDs for preview).
  if (_leds) _leds->previewBegin();

  InputParser input;
  bool screenDirty = true;

  _ui->vtClear();

  while (true) {
    NavEvent ev = input.update();

    if (ev.type == NAV_QUIT) break;
    if (ev.type == NAV_TOGGLE) {
      _page = (Page)((_page + 1) % PAGE_COUNT);
      screenDirty = true;
    } else if (ev.type != NAV_NONE) {
      // Any other key redraws (stub : no other handlers yet).
      screenDirty = true;
    }

    if (screenDirty) {
      screenDirty = false;
      _ui->vtFrameStart();

      // Header with page indicator.
      _ui->drawConsoleHeader("TOOL 8: LED SETTINGS", false);
      _ui->drawFrameEmpty();

      // Page tabs line : active page highlighted, others dim.
      {
        const char* pn = (_page == PAGE_PATTERNS) ? VT_CYAN VT_BOLD "[PATTERNS]" VT_RESET : VT_DIM " PATTERNS " VT_RESET;
        const char* cn = (_page == PAGE_COLORS)   ? VT_CYAN VT_BOLD "[COLORS]"   VT_RESET : VT_DIM " COLORS "   VT_RESET;
        const char* en = (_page == PAGE_EVENTS)   ? VT_CYAN VT_BOLD "[EVENTS]"   VT_RESET : VT_DIM " EVENTS "   VT_RESET;
        _ui->drawFrameLine("  %s   %s   %s", pn, cn, en);
      }
      _ui->drawFrameEmpty();

      // Page content.
      switch (_page) {
        case PAGE_PATTERNS: renderPagePatterns(); break;
        case PAGE_COLORS:   renderPageColors();   break;
        case PAGE_EVENTS:   renderPageEvents();   break;
        default: break;
      }
      _ui->drawFrameEmpty();

      // Control bar.
      _ui->drawControlBar(VT_DIM "[t] NEXT PAGE" CBAR_SEP "[q] EXIT" VT_RESET);

      _ui->vtFrameEnd();
    }

    delay(10);
  }

  if (_leds) _leds->previewEnd();
}
