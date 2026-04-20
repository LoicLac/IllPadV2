#include "ToolLedPreview.h"
#include "../core/LedGrammar.h"
#include "../core/HardwareConfig.h"   // LED_SINE_LUT[256]
#include <Arduino.h>

// =================================================================
// Tool 8 Live Preview Helper — implementation.
// Spec : docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md §6
// =================================================================

ToolLedPreview::ToolLedPreview()
    : _leds(nullptr),
      _tempoBpm(120),
      _ctx(PV_NONE),
      _replayPhaseStart(0),
      _replayInBlack(false),
      _lastUpdateMs(0) {
  for (uint8_t i = 0; i < 3; i++) {
    _tickLastFire[i] = 0;
    _tickFlashStart[i] = 0;
  }
  // _p is value-initialized (all zero) by struct-default.
}

void ToolLedPreview::begin(LedController* leds, uint16_t tempoBpm) {
  _leds = leds;
  _tempoBpm = (tempoBpm == 0) ? 120 : tempoBpm;
  _ctx = PV_NONE;
  _replayPhaseStart = 0;
  _replayInBlack = false;
  _lastUpdateMs = 0;
  for (uint8_t i = 0; i < 3; i++) {
    _tickLastFire[i] = 0;
    _tickFlashStart[i] = 0;
  }
}

void ToolLedPreview::end() {
  if (_leds) _leds->previewClear();
  _leds = nullptr;
  _ctx = PV_NONE;
}

void ToolLedPreview::setContext(PreviewContext ctx, const Params& p) {
  _ctx = ctx;
  _p = p;
  // One-shot replay : reset phase when entering / re-entering this context.
  if (ctx == PV_EVENT_REPLAY) {
    _replayPhaseStart = millis();
    _replayInBlack = false;
  }
  // Ticks mockup : reset schedulers so the flash starts cleanly on context change.
  if (ctx == PV_TICKS_MOCKUP || ctx == PV_BG_FACTOR || ctx == PV_GAMMA_TEST) {
    unsigned long now = millis();
    for (uint8_t i = 0; i < 3; i++) {
      _tickLastFire[i] = now;
      _tickFlashStart[i] = 0;
    }
  }
}

void ToolLedPreview::update(unsigned long now) {
  if (!_leds) return;
  // 50 Hz cap (spec §11.2) : preview only re-renders every 20 ms.
  // Unsigned subtraction is wrap-safe for millis() rollover.
  if (_lastUpdateMs != 0 && (now - _lastUpdateMs) < 20) return;
  _lastUpdateMs = now;
  switch (_ctx) {
    case PV_BASE_COLOR:     renderBaseColor(now);    break;
    case PV_BREATHING:      renderBreathing(now);    break;
    case PV_WAITING:        renderWaiting(now);      break;
    case PV_EVENT_REPLAY:   renderEventReplay(now);  break;
    case PV_TICKS_MOCKUP:
    case PV_BG_FACTOR:
    case PV_GAMMA_TEST:     renderTicksMockup(now);  break;
    case PV_NONE:
    default:                _leds->previewClear();   break;
  }
  _leds->previewShow();
}

uint16_t ToolLedPreview::computeBlackMs(uint16_t effectMs) {
  uint32_t raw = (uint32_t)effectMs * 50 / 100;  // 50 % ratio
  if (raw < 500)  raw = 500;
  if (raw > 3000) raw = 3000;
  return (uint16_t)raw;
}

// -----------------------------------------------------------------
// Mono-FG mockup : [off][BG][BG][FG][BG][BG][off][off]
// LEDs 0,6,7 always off. LED 3 = FG at fgPct. LEDs 1,2,4,5 = FG color dimmed
// by bgFactorPct (i.e. BG bank rendering = FG hue × bgFactor intensity).
// -----------------------------------------------------------------
void ToolLedPreview::drawMonoFgMockup(const RGBW& fg, uint8_t fgPct, uint8_t bgFactorPct) {
  if (!_leds) return;
  uint8_t bgPct = (uint16_t)fgPct * bgFactorPct / 100;
  const RGBW off = {0, 0, 0, 0};
  _leds->previewSetPixel(0, off, 0);
  _leds->previewSetPixel(1, fg, bgPct);
  _leds->previewSetPixel(2, fg, bgPct);
  _leds->previewSetPixel(3, fg, fgPct);
  _leds->previewSetPixel(4, fg, bgPct);
  _leds->previewSetPixel(5, fg, bgPct);
  _leds->previewSetPixel(6, off, 0);
  _leds->previewSetPixel(7, off, 0);
}

void ToolLedPreview::renderBaseColor(unsigned long /*now*/) {
  drawMonoFgMockup(_p.fgColor,
                   _p.fgPct,
                   _p.bgFactorPct == 0 ? 25 : _p.bgFactorPct);
}

// -----------------------------------------------------------------
// Breathing : continuous sine on the FG LED, BG follows.
// Uses LED_SINE_LUT (HardwareConfig.h:296) for smooth interpolation.
// -----------------------------------------------------------------
void ToolLedPreview::renderBreathing(unsigned long now) {
  if (_p.breathPeriodMs == 0) {
    // Degenerate : solid at min.
    drawMonoFgMockup(_p.fgColor,
                     _p.breathMinPct,
                     _p.bgFactorPct == 0 ? 25 : _p.bgFactorPct);
    return;
  }
  uint32_t elapsed = now % _p.breathPeriodMs;
  uint32_t phase16 = (elapsed * 65536UL) / _p.breathPeriodMs;
  uint8_t idx  = (phase16 >> 8) & 0xFF;
  uint8_t frac = phase16 & 0xFF;
  uint8_t a = LED_SINE_LUT[idx];
  uint8_t b = LED_SINE_LUT[(uint8_t)(idx + 1)];
  uint8_t sineNorm = (uint8_t)(((uint16_t)a * (255 - frac) + (uint16_t)b * frac) >> 8);
  uint8_t minP = _p.breathMinPct;
  uint8_t maxP = _p.breathMaxPct;
  if (maxP < minP) maxP = minP;
  uint8_t pct = minP + (uint16_t)(maxP - minP) * sineNorm / 255;
  drawMonoFgMockup(_p.fgColor,
                   pct,
                   _p.bgFactorPct == 0 ? 25 : _p.bgFactorPct);
}

// -----------------------------------------------------------------
// Waiting : crossfade continuously between mode color (current bank at setup
// entry) and target color (the edited color). Period hardcoded 1500 ms
// (cohérence UX Phase 0.1, non exposed — spec §4.3 non-éditable).
// -----------------------------------------------------------------
void ToolLedPreview::renderWaiting(unsigned long now) {
  const uint16_t periodMs = 1500;
  uint32_t elapsed = now % periodMs;
  uint32_t phase16 = (elapsed * 65536UL) / periodMs;
  uint8_t idx  = (phase16 >> 8) & 0xFF;
  uint8_t frac = phase16 & 0xFF;
  uint8_t a = LED_SINE_LUT[idx];
  uint8_t b = LED_SINE_LUT[(uint8_t)(idx + 1)];
  uint8_t s = (uint8_t)(((uint16_t)a * (255 - frac) + (uint16_t)b * frac) >> 8);
  // Lerp RGBW by s/255 between fgColor (s=0) and target (s=255).
  RGBW blended;
  blended.r = (uint8_t)(((uint16_t)_p.fgColor.r              * (255 - s)
                         + (uint16_t)_p.crossfadeTargetColor.r * s) >> 8);
  blended.g = (uint8_t)(((uint16_t)_p.fgColor.g              * (255 - s)
                         + (uint16_t)_p.crossfadeTargetColor.g * s) >> 8);
  blended.b = (uint8_t)(((uint16_t)_p.fgColor.b              * (255 - s)
                         + (uint16_t)_p.crossfadeTargetColor.b * s) >> 8);
  blended.w = (uint8_t)(((uint16_t)_p.fgColor.w              * (255 - s)
                         + (uint16_t)_p.crossfadeTargetColor.w * s) >> 8);
  drawMonoFgMockup(blended,
                   _p.fgPct == 0 ? 100 : _p.fgPct,
                   _p.bgFactorPct == 0 ? 25 : _p.bgFactorPct);
}

// -----------------------------------------------------------------
// Event replay : one-shot pattern with black pause loop (§6.4 formula).
// Delegates rendering to LedController::renderPreviewPattern — zero runtime
// duplication. ledMask is set on inst by the caller (typically 0b00001000
// so only LED 3 animates, keeping the mono-FG mockup layout).
// -----------------------------------------------------------------
void ToolLedPreview::renderEventReplay(unsigned long now) {
  uint16_t effectMs = (_p.effectDurationMs == 0) ? 500 : _p.effectDurationMs;
  uint16_t blackMs  = computeBlackMs(effectMs);

  unsigned long phaseElapsed = now - _replayPhaseStart;
  const RGBW off = {0, 0, 0, 0};

  if (!_replayInBlack) {
    // PLAYING : render pattern. Frame base = all off (ledMask-driven overlay).
    for (uint8_t i = 0; i < 8; i++) _leds->previewSetPixel(i, off, 0);
    LedController::PatternInstance inst = _p.replayInst;
    inst.startTime = _replayPhaseStart;
    _leds->renderPreviewPattern(inst, now);
    if (phaseElapsed >= effectMs) {
      _replayInBlack = true;
      _replayPhaseStart = now;
      for (uint8_t i = 0; i < 8; i++) _leds->previewSetPixel(i, off, 0);
    }
  } else {
    // BLACK : all off.
    for (uint8_t i = 0; i < 8; i++) _leds->previewSetPixel(i, off, 0);
    if (phaseElapsed >= blackMs) {
      _replayInBlack = false;
      _replayPhaseStart = now;
    }
  }
}

// -----------------------------------------------------------------
// Ticks mockup : LOOP bank FG (LED 3) + BG (LED 1) with tempo-synced flash
// overlay on the active tick kind (BEAT/BAR/WRAP).
// Spec §6.2 : LED 3 overlays tickColorActive (PLAY/REC/OVERDUB per cursor),
//             LED 1 overlays tickColorPlayBg (ALWAYS PLAY, runtime invariant).
// Periods derived from _tempoBpm : BEAT = 60000/bpm, BAR = 4·BEAT, WRAP = 8·BEAT.
// -----------------------------------------------------------------
void ToolLedPreview::renderTicksMockup(unsigned long now) {
  // 1) Base layer : mono-mockup with FG + BG dim (LEDs 0/6/7 off).
  uint8_t bgFactor = (_p.bgFactorPct == 0) ? 25 : _p.bgFactorPct;
  uint8_t fgBase = 100;
  uint8_t bgBase = (uint16_t)fgBase * bgFactor / 100;
  const RGBW off = {0, 0, 0, 0};
  _leds->previewSetPixel(0, off, 0);
  _leds->previewSetPixel(1, _p.modeColorBg, bgBase);   // tickBG base (LOOP BG bank)
  _leds->previewSetPixel(2, _p.modeColorBg, bgBase);
  _leds->previewSetPixel(3, _p.modeColorFg, fgBase);   // tickFG base (LOOP FG bank)
  _leds->previewSetPixel(4, _p.modeColorBg, bgBase);
  _leds->previewSetPixel(5, _p.modeColorBg, bgBase);
  _leds->previewSetPixel(6, off, 0);
  _leds->previewSetPixel(7, off, 0);

  // 2) Tick scheduler : only the activeTickKind period is rendered.
  const uint16_t beatMs = (_tempoBpm == 0) ? 500 : (uint16_t)(60000UL / _tempoBpm);
  uint32_t periodMs = 0;
  uint16_t durMs = 0;
  uint8_t  kind = (_p.activeTickKind > 2) ? 0 : _p.activeTickKind;
  switch (kind) {
    case 0: periodMs = beatMs;       durMs = _p.tickBeatMs; break;
    case 1: periodMs = (uint32_t)beatMs * 4; durMs = _p.tickBarMs;  break;
    case 2: periodMs = (uint32_t)beatMs * 8; durMs = _p.tickWrapMs; break;
  }
  if (periodMs == 0) return;

  // Fire a flash when (now - _tickLastFire[kind]) >= periodMs.
  if (now - _tickLastFire[kind] >= periodMs) {
    _tickLastFire[kind]   = now;
    _tickFlashStart[kind] = now;
  }
  // During flash window : LED 3 overlays tickColorActive, LED 1 overlays
  // tickColorPlayBg (always PLAY — runtime invariant §6.2).
  if (_tickFlashStart[kind] != 0 && (now - _tickFlashStart[kind]) < durMs) {
    _leds->previewSetPixel(3, _p.tickColorActive,
                           _p.tickCommonFgPct == 0 ? 100 : _p.tickCommonFgPct);
    _leds->previewSetPixel(1, _p.tickColorPlayBg,
                           _p.tickCommonBgPct == 0 ? 25 : _p.tickCommonBgPct);
  } else if (_tickFlashStart[kind] != 0) {
    // Flash done, reset so it stops overlaying until next period boundary.
    _tickFlashStart[kind] = 0;
  }
}
