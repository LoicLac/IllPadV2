#ifndef TOOL_LED_SETTINGS_H
#define TOOL_LED_SETTINGS_H

#include <stdint.h>

// =================================================================
// Tool 8 — LED Settings (Phase 0 step 0.8 refactor)
// =================================================================
// Three-page editor aligned with the unified LED grammar (LED spec) and
// the setup-tools-conventions (grid / pool / field editor paradigms) :
//
//   PAGE_PATTERNS : pool of 9 palette patterns + field editor for the
//                   selected pattern's params. Live preview on LED 3-4.
//   PAGE_COLORS   : grid 4x4 of 15 color slots + pool (14 presets) +
//                   field editor (hueOffset). Preview = SOLID on LED 3-4.
//   PAGE_EVENTS   : vertical field list. Each event row edits
//                   pattern (pool) + color slot (pool) + fgPct (field).
//                   Preview = trigger the event on LED 3-4 via 'b'.
//
// Inter-page nav : 't' cycles PATTERNS -> COLORS -> EVENTS -> PATTERNS
//                  (not arrows — arrows stay for cursor nav per §6).
// Save policy     : save on ENTER commit of pool / field editor ; never
//                   on arrow-key navigation (conventions §1). One
//                   flashSaved() per commit (conventions §2).
//
// Step 0.8a : skeleton with page navigation and stub content per page.
// Step 0.8b : COLORS page implementation.
// Step 0.8c : PATTERNS page implementation.
// Step 0.8d : EVENTS page implementation (wires NVS eventOverrides[]).
// Step 0.8e : polish, preview integration, flashSaved audit.
// =================================================================

class LedController;
class SetupUI;

class ToolLedSettings {
public:
  ToolLedSettings();
  void begin(LedController* leds, SetupUI* ui);
  void run();

private:
  enum Page : uint8_t {
    PAGE_PATTERNS = 0,
    PAGE_COLORS   = 1,
    PAGE_EVENTS   = 2,
    PAGE_COUNT    = 3,
  };

  LedController* _leds;
  SetupUI*       _ui;
  Page           _page;

  // Page renderers (stubs in 0.8a ; implementations land in 0.8b..0.8d).
  void renderPagePatterns();
  void renderPageColors();
  void renderPageEvents();
};

#endif // TOOL_LED_SETTINGS_H
