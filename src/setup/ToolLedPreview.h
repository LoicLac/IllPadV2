#ifndef TOOL_LED_PREVIEW_H
#define TOOL_LED_PREVIEW_H

#include <stdint.h>
#include "../core/LedController.h"    // for LedController::PatternInstance
#include "../core/KeyboardData.h"     // LedSettingsStore, ColorSlotStore, RGBW

// =================================================================
// Tool 8 Live Preview Helper (Phase 0.1)
// =================================================================
// Encapsulates all preview logic for the Tool 8 LED Settings refonte.
// ToolLedSettings owns a ToolLedPreview instance, calls begin() at run()
// entry (after _leds->previewBegin()), sets the current context whenever
// the cursor moves or a value changes, calls update(now) each loop
// iteration, and calls end() before exiting the tool.
//
// No UI responsibility: the helper only writes LEDs via LedController's
// preview API + the public renderPreviewPattern wrapper. All VT100 work
// stays in ToolLedSettings.
//
// Spec: docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md §6
// =================================================================
class ToolLedPreview {
public:
  // Context types (one per kind of preview, mapped from the cursor line).
  enum PreviewContext : uint8_t {
    PV_NONE = 0,
    PV_BASE_COLOR,      // mono-FG mockup [off][BG][BG][FG][BG][BG][off][off]
    PV_EVENT_REPLAY,    // one-shot pattern with black-then-loop (§6.4 formule)
    PV_BREATHING,       // mono-FG mockup, continuous sine (FG ARPEG stopped-loaded)
    PV_WAITING,         // mono-FG mockup, continuous crossfade (WAITING quantise)
    PV_TICKS_MOCKUP,    // LOOP ticks mockup [off][tickBG][BG][tickFG][BG][BG][off][off]
    PV_BG_FACTOR,       // reuses PV_TICKS_MOCKUP (FG vs BG intensity visible)
    PV_GAMMA_TEST,      // reuses PV_TICKS_MOCKUP (multi-intensity ladder)
  };

  // Parameter packet. Caller sets only fields relevant to the context.
  // Unused fields ignored. Extensible: add members as future contexts require.
  struct Params {
    // PV_BASE_COLOR / PV_BREATHING / PV_WAITING : color for the FG LED.
    RGBW     fgColor;
    // PV_BASE_COLOR : FG intensity 0..100. (BG derives from bgFactor.)
    uint8_t  fgPct;
    // PV_WAITING : destination color (crossfade source = current mode bank color).
    RGBW     crossfadeTargetColor;
    // PV_BREATHING : min%, max%, period ms.
    uint8_t  breathMinPct;
    uint8_t  breathMaxPct;
    uint16_t breathPeriodMs;
    // PV_EVENT_REPLAY : fully-formed PatternInstance. startTime rewritten by helper.
    //                   effectDurationMs governs the §6.4 black timer.
    LedController::PatternInstance replayInst;
    uint16_t effectDurationMs;
    // PV_TICKS_MOCKUP / PV_BG_FACTOR / PV_GAMMA_TEST :
    //   base colors for the underlying bank mockup (FG bank + BG bank),
    //   tick colors (active verb for LED 3, always PLAY for LED 1),
    //   tick durations (BEAT/BAR/WRAP),
    //   tick common FG% / BG%.
    RGBW     modeColorFg;      // foreground bank color (NORMAL/ARPEG/LOOP)
    RGBW     modeColorBg;      // background bank color (usually = modeColorFg with bgFactor)
    RGBW     tickColorActive;  // LED 3 overlay : verb of current line (PLAY / REC / OVERDUB).
                               // Driven by the cursor : if the user is editing "Tick REC color",
                               // pass the REC resolved color here so the preview flashes REC.
    RGBW     tickColorPlayBg;  // LED 1 overlay : ALWAYS resolved CSLOT_VERB_PLAY color, regardless
                               // of the cursor line. Matches runtime invariant §6.2 spec : a LOOP
                               // bank cannot be in REC/OVERDUB when in background, so tickBG is
                               // always the PLAY tint.
    uint16_t tickBeatMs;
    uint16_t tickBarMs;
    uint16_t tickWrapMs;
    uint8_t  tickCommonFgPct;  // tick flash FG%
    uint8_t  tickCommonBgPct;  // tick flash BG%
    // Which tick subdivision is currently highlighted (BEAT / BAR / WRAP).
    // 0 = BEAT, 1 = BAR, 2 = WRAP. Used by ticks mockup to schedule the right
    // period; also used by BG_FACTOR / GAMMA_TEST (pick BAR for decent pacing).
    uint8_t  activeTickKind;
    // Global shaping for BG_FACTOR / GAMMA_TEST previews.
    uint8_t  bgFactorPct;
  };

  ToolLedPreview();

  // tempoBpm snapshotted at begin() — used by PV_TICKS_MOCKUP scheduler.
  // The preview does NOT re-query tempo mid-session; a bank switch during
  // Tool 8 is impossible, so tempoBpm is stable for the tool's lifetime.
  void begin(LedController* leds, uint16_t tempoBpm);
  void end();

  void setContext(PreviewContext ctx, const Params& p);

  // Must be called every frame from ToolLedSettings::run().
  // Rate-capped at 50 Hz internally (spec §11.2 mitigation).
  void update(unsigned long now);

private:
  // Render helpers (one per PreviewContext). Each writes LEDs via _leds->
  // previewSetPixel + previewShow.
  void renderBaseColor(unsigned long now);
  void renderBreathing(unsigned long now);
  void renderWaiting(unsigned long now);
  void renderEventReplay(unsigned long now);
  void renderTicksMockup(unsigned long now);

  // §6.4 : black_ms = clamp(effect_ms * 0.50, 500, 3000)
  static uint16_t computeBlackMs(uint16_t effectMs);

  // Mono-FG mockup writer : LEDs 0,6,7 off, 3 = FG, 1,2,4,5 = BG derived via bgFactor.
  void drawMonoFgMockup(const RGBW& fg, uint8_t fgPct, uint8_t bgFactorPct);

  LedController* _leds;
  uint16_t       _tempoBpm;
  PreviewContext _ctx;
  Params         _p;
  // One-shot replay timer: state machine {PLAYING, BLACK}.
  unsigned long  _replayPhaseStart;
  bool           _replayInBlack;
  // Ticks mockup scheduler — tracks flash start time per kind.
  unsigned long  _tickLastFire[3];   // BEAT, BAR, WRAP
  unsigned long  _tickFlashStart[3]; // 0 = not flashing
  // Frame rate cap (spec §11.2 mitigation — Tool 8 loop runs ~200 Hz via delay(5),
  // preview rendering at 50 Hz is visually sufficient and spares Core 1 from VT100
  // redraw collisions during burst arrow-key edits).
  unsigned long  _lastUpdateMs;
};

#endif // TOOL_LED_PREVIEW_H
