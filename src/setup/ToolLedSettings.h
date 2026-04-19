#ifndef TOOL_LED_SETTINGS_H
#define TOOL_LED_SETTINGS_H

// =================================================================
// Tool 8 — LED Settings
// =================================================================
// Phase 0 step 0.2 : placeholder implementation. The previous v5 editor
// referenced fields removed from LedSettingsStore v6 (fgArpPlayMin,
// bgArpStopMax, bgArpPlayMax). The full 3-page refactor (PATTERNS /
// COLORS / EVENTS) is scheduled in step 0.8 (see plan §0.8a-e).
//
// Until step 0.8 lands, this tool only displays a static "refactor in
// progress" screen. User can 'q' back to the menu. All LED settings
// continue to work at runtime via compile-time defaults (post-flash)
// or whatever NVS v5 data survives the v6 bump — nothing is lost, just
// not user-editable.
// =================================================================

class LedController;
class SetupUI;

class ToolLedSettings {
public:
  ToolLedSettings();
  void begin(LedController* leds, SetupUI* ui);
  void run();

private:
  LedController* _leds;
  SetupUI*       _ui;
};

#endif // TOOL_LED_SETTINGS_H
