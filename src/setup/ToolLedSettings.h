#ifndef TOOL_LED_SETTINGS_H
#define TOOL_LED_SETTINGS_H

#include <stdint.h>
#include "../core/KeyboardData.h"  // ColorSlotStore, COLOR_SLOT_COUNT

// =================================================================
// Tool 8 — LED Settings (Phase 0 step 0.8 refactor)
// =================================================================
// Three-page editor aligned with the unified LED grammar (LED spec) and
// the setup-tools-conventions (grid / pool / field editor paradigms) :
//
//   PAGE_PATTERNS : pool of 9 palette patterns + field editor for the
//                   selected pattern's params. Live preview on LED 3-4.
//   PAGE_COLORS   : vertical list of 15 color slots (rows). ENTER opens
//                   an edit sub-state with 2 focusable fields :
//                   preset (pool of 14) + hue offset (field editor).
//                   Preview = SOLID on LED 3-4 with current slot color.
//   PAGE_EVENTS   : vertical field list. Each event row edits
//                   pattern (pool) + color slot (pool) + fgPct (field).
//
// Inter-page nav : 't' cycles PATTERNS -> COLORS -> EVENTS -> PATTERNS
//                  (not arrows — arrows stay for cursor nav per §6).
// Save policy     : save on ENTER commit of pool / field editor ; never
//                   on arrow-key navigation (conventions §1). One
//                   flashSaved() per commit (conventions §2).
//
// Step 0.8a : skeleton with page navigation and stub content per page.
// Step 0.8b : COLORS page implementation (this commit).
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

  // COLORS page sub-states
  enum ColorsSubState : uint8_t {
    COLORS_NAV   = 0,  // cursor selecting a slot row
    COLORS_EDIT  = 1,  // editing the selected slot (preset + hue)
  };

  // COLORS edit focus : which of the 2 fields is active
  enum ColorsEditField : uint8_t {
    EDIT_FIELD_PRESET = 0,
    EDIT_FIELD_HUE    = 1,
  };

  // PATTERNS page sub-states
  enum PatternsSubState : uint8_t {
    PATTERNS_NAV  = 0,  // pool cursor over 9 patterns
    PATTERNS_EDIT = 1,  // field editor for selected pattern's globals
  };

  LedController* _leds;
  SetupUI*       _ui;
  Page           _page;
  bool           _nvsSaved;  // NVS badge state in header

  // COLORS page state
  ColorSlotStore   _cwk;                  // working copy loaded from NVS
  uint8_t          _colorsCursor;         // 0..COLOR_SLOT_COUNT-1
  ColorsSubState   _colorsSub;
  ColorsEditField  _colorsEditField;
  ColorSlot        _colorsEditBackup;     // for cancel (q) during edit

  // PATTERNS page state
  LedSettingsStore _lwk;                  // working copy for pattern globals
  uint8_t          _patternsCursor;       // 0..8 (PatternId pool index)
  PatternsSubState _patternsSub;
  uint8_t          _patternsEditField;    // 0..N-1 per selected pattern
  LedSettingsStore _patternsEditBackup;   // for cancel (q)

  // Page renderers
  void renderPagePatterns();
  void renderPageColors();
  void renderPageEvents();

  // COLORS page helpers
  void handlePageColors(const struct NavEvent& ev, bool& screenDirty);
  void previewCurrentColor();
  bool saveColorSlots();
  void resetCurrentColorToDefault();

  // PATTERNS page helpers
  void handlePagePatterns(const struct NavEvent& ev, bool& screenDirty);
  uint8_t patternEditFieldCount(uint8_t patternId) const;
  void adjustPatternField(uint8_t patternId, uint8_t field, int dir, bool accel);
  bool saveLedSettings();
  void renderPatternParamsPanel(uint8_t patternId, bool editing) const;
};

#endif // TOOL_LED_SETTINGS_H
