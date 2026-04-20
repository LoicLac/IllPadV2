#include "LedController.h"
#include "HardwareConfig.h"
#include "KeyboardData.h"
#include "../arp/ArpEngine.h"
#include <Arduino.h>
#include <math.h>

// =================================================================
// Constructor
// =================================================================

LedController::LedController()
  : _strip(NUM_LEDS, LED_DATA_PIN, NEO_GRBW + NEO_KHZ800),
    _brightnessPct(100),
    _currentBank(0),
    _batteryLow(false),
    _slots(nullptr),
    _normalFgIntensity(85), _normalBgIntensity(10),
    _fgArpStopMin(30), _fgArpStopMax(100),
    _fgArpPlayMax(80),
    _bgArpStopMin(8),
    _bgArpPlayMin(8),
    _tickFlashFg(100), _tickFlashBg(25),
    _pulsePeriodMs(1472), _tickBeatDurationMs(30),
    _tickBarDurationMs(60), _tickWrapDurationMs(100),
    _bankBlinks(3), _bankDurationMs(300), _bankBrightnessPct(80),
    _scaleRootBlinks(2), _scaleRootDurationMs(200),
    _scaleModeBlinks(2), _scaleModeDurationMs(200),
    _scaleChromBlinks(2), _scaleChromDurationMs(200),
    _holdOnFadeMs(500), _holdOffFadeMs(500),
    _octaveBlinks(3), _octaveDurationMs(300),
    _sparkOnMs(50), _sparkGapMs(70), _sparkCycles(2),
    _bgFactor(25),
    _potBarDurationMs(LED_BARGRAPH_DURATION_DEFAULT),
    _showingPotBar(false),
    _potBarRealLevel(0),
    _potBarPotLevel(0),
    _potBarCaught(true),
    _potBarIsTempo(false),
    _potBarBpm(120),
    _potBarStart(0),
    _bootMode(false),
    _bootStep(0),
    _bootFailStep(0),
    _chaseActive(false),
    _chasePos(0),
    _chaseLastStep(0),
    _setupComet(false),
    _cometPos(0),
    _cometLastStep(0),
    _calibrationMode(false),
    _validationFlashing(false),
    _validationFlashStart(0),
    _error(false),
    _lastBlinkTime(0),
    _blinkState(false),
    _showingBattery(false),
    _batteryLeds(0),
    _batteryDisplayStart(0),
    _batLowLastBurstTime(0),
    _previewMode(false)
{
  // Init tick flash timers
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    _flashStartTime[i] = 0;
  }
  // Init event overlay to inactive
  _eventOverlay = {};
  _eventOverlay.active = false;
  _eventOverlay.patternId = PTN_NONE;
  // Init per-event override array : all PTN_NONE -> fallback on
  // EVENT_RENDER_DEFAULT until loadLedSettings() populates from NVS.
  for (uint8_t i = 0; i < EVT_COUNT; i++) {
    _eventOverrides[i].patternId = PTN_NONE;
    _eventOverrides[i].colorSlot = 0;
    _eventOverrides[i].fgPct     = 0;
  }
  // Init color slot cache with v4 compile-time defaults matching
  // NvsManager::_colorSlots. loadColorSlots() overwrites when NVS loads.
  _colors[CSLOT_MODE_NORMAL]      = COLOR_PRESETS[1];   // Warm White
  _colors[CSLOT_MODE_ARPEG]       = COLOR_PRESETS[3];   // Ice Blue
  _colors[CSLOT_MODE_LOOP]        = COLOR_PRESETS[7];   // Gold
  _colors[CSLOT_VERB_PLAY]        = COLOR_PRESETS[11];  // Green
  _colors[CSLOT_VERB_REC]         = COLOR_PRESETS[8];   // Coral
  _colors[CSLOT_VERB_OVERDUB]     = COLOR_PRESETS[6];   // Amber
  _colors[CSLOT_VERB_CLEAR_LOOP]  = COLOR_PRESETS[5];   // Cyan
  _colors[CSLOT_VERB_SLOT_CLEAR]  = COLOR_PRESETS[6];   // Amber (hue-shift at store load)
  _colors[CSLOT_VERB_SAVE]        = COLOR_PRESETS[10];  // Magenta
  _colors[CSLOT_BANK_SWITCH]      = COLOR_PRESETS[0];   // Pure White
  _colors[CSLOT_SCALE_ROOT]       = COLOR_PRESETS[6];   // Amber
  _colors[CSLOT_SCALE_MODE]       = COLOR_PRESETS[7];   // Gold
  _colors[CSLOT_SCALE_CHROM]      = COLOR_PRESETS[8];   // Coral
  _colors[CSLOT_OCTAVE]           = COLOR_PRESETS[9];   // Violet
  _colors[CSLOT_CONFIRM_OK]       = COLOR_PRESETS[0];   // Pure White (SPARK)
  _colors[CSLOT_VERB_STOP]        = COLOR_PRESETS[8];   // Coral (Phase 0.1 — Stop fade-out)
}

// =================================================================
// begin
// =================================================================

void LedController::begin() {
  rebuildGammaLut(20);  // default gamma 2.0 until NVS loads
  _strip.begin();
  _strip.show();  // All pixels off
}

// =================================================================
// Unified Pixel Setter — perceptual intensity + master brightness + gamma
// =================================================================

void LedController::setPixel(uint8_t i, const RGBW& c, uint8_t intensityPct) {
  // Combine intensity (0-100%) × brightness (0-100%) → scale to 0-255.
  // Multiply before dividing to preserve resolution at low values:
  // e.g. 8% × 10% × 255 / 10000 = 2 instead of (8×10/100)×255/100 = 0
  uint8_t scaled = (uint8_t)((uint32_t)intensityPct * _brightnessPct * 255 / 10000);
  // Scale each channel by intensity, then apply gamma for LED
  _strip.setPixelColor(i,
    _gammaLut[(uint16_t)c.r * scaled / 255],
    _gammaLut[(uint16_t)c.g * scaled / 255],
    _gammaLut[(uint16_t)c.b * scaled / 255],
    _gammaLut[(uint16_t)c.w * scaled / 255]
  );
}

void LedController::rebuildGammaLut(uint8_t gammaTenths) {
  float gamma = (float)gammaTenths / 10.0f;
  if (gamma < 1.0f) gamma = 1.0f;
  if (gamma > 3.0f) gamma = 3.0f;
  _gammaLut[0] = 0;
  for (uint16_t i = 1; i < 256; i++) {
    float v = powf((float)i / 255.0f, gamma) * 255.0f + 0.5f;
    uint8_t out = (uint8_t)v;
    if (out < 1) out = 1;  // floor=1: any non-zero input produces light
    _gammaLut[i] = out;
  }
}

void LedController::clearPixels() {
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    _strip.setPixelColor(i, 0);
  }
}

// =================================================================
// I2C error halt -- 3 rapid red flashes + 1s off, loops forever
// =================================================================

void LedController::haltI2CError() {
  while (true) {
    for (uint8_t flash = 0; flash < 3; flash++) {
      for (uint8_t i = 0; i < NUM_LEDS; i++) setPixel(i, COL_ERROR, 100);
      _strip.show();
      delay(80);
      clearPixels();
      _strip.show();
      delay(80);
    }
    delay(1000);
  }
}

// =================================================================
// Chase pattern -- single LED circling (calibration entry feedback)
// =================================================================

void LedController::startChase() {
  _chaseActive = true;
  _chasePos = 0;
  _chaseLastStep = millis();
  clearPixels();
  setPixel(0, COL_SETUP, 100);
  _strip.show();
}

void LedController::stopChase() {
  _chaseActive = false;
  clearPixels();
  _strip.show();
}

// =================================================================
// Setup comet -- ping-pong chase with trail (active during Tools 1-6)
// =================================================================

void LedController::startSetupComet() {
  _setupComet = true;
  _cometPos = 0;
  _cometLastStep = millis();
}

void LedController::stopSetupComet() {
  _setupComet = false;
  clearPixels();
  _strip.show();
}

// =================================================================
// Main update -- priority-based state machine
// =================================================================

void LedController::update() {
  if (_previewMode) return;  // Tool 7 owns the LEDs

  unsigned long now = millis();

  // Shared 500ms blink timer (used by error mode)
  if (now - _lastBlinkTime >= 500) {
    _blinkState = !_blinkState;
    _lastBlinkTime = now;
  }

  if (_bootMode)                         { renderBoot(now); return; }
  if (_setupComet && !_calibrationMode)  { renderComet(now); return; }
  if (_chaseActive)                      { renderChase(now); return; }
  if (_error)                            { renderError(now); return; }
  if (renderBattery(now))                return;
  if (renderBargraph(now))               return;
  renderConfirmation(now);  // State tracking only — all overlays in renderNormalDisplay
  if (_calibrationMode)                  { renderCalibration(now); return; }
  renderNormalDisplay(now);
}

// =================================================================
// Render helpers (priority order)
// =================================================================

void LedController::renderBoot(unsigned long now) {
  clearPixels();
  if (_bootFailStep > 0) {
    bool fastBlink = ((now / 150) % 2) == 0;
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      if (i < _bootFailStep - 1) {
        setPixel(i, COL_BOOT, 100);
      } else if (i == _bootFailStep - 1) {
        if (fastBlink) setPixel(i, COL_BOOT_FAIL, 100);
      }
    }
  } else {
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      if (i < _bootStep) {
        setPixel(i, COL_BOOT, 100);
      }
    }
  }
  _strip.show();
}

void LedController::renderComet(unsigned long now) {
  if (now - _cometLastStep >= LED_SETUP_CHASE_SPEED_MS) {
    _cometPos = (_cometPos + 1) % 14;  // 0-13 for ping-pong
    _cometLastStep = now;
  }
  // Map ping-pong position to LED index:
  // 0-7 = LEDs 0-7 forward, 8-13 = LEDs 6 down to 1 backward
  uint8_t headLed;
  if (_cometPos <= 7) {
    headLed = _cometPos;
  } else {
    headLed = 14 - _cometPos;  // 8->6, 9->5, 10->4, 11->3, 12->2, 13->1
  }

  clearPixels();
  // Head at 100%
  setPixel(headLed, COL_SETUP, 100);
  // Trail -1 at 40%
  uint8_t prevPos = (_cometPos == 0) ? 13 : (_cometPos - 1);
  uint8_t trail1Led;
  if (prevPos <= 7) {
    trail1Led = prevPos;
  } else {
    trail1Led = 14 - prevPos;
  }
  if (trail1Led != headLed) {
    setPixel(trail1Led, COL_SETUP, 40);
  }
  // Trail -2 at 10%
  uint8_t prevPos2 = (prevPos == 0) ? 13 : (prevPos - 1);
  uint8_t trail2Led;
  if (prevPos2 <= 7) {
    trail2Led = prevPos2;
  } else {
    trail2Led = 14 - prevPos2;
  }
  if (trail2Led != headLed && trail2Led != trail1Led) {
    setPixel(trail2Led, COL_SETUP, 10);
  }

  _strip.show();
}

void LedController::renderChase(unsigned long now) {
  if (now - _chaseLastStep >= CHASE_STEP_MS) {
    _chasePos = (_chasePos + 1) % NUM_LEDS;
    _chaseLastStep = now;
  }
  clearPixels();
  setPixel(_chasePos, COL_SETUP, 100);
  _strip.show();
}

void LedController::renderError(unsigned long now) {
  (void)now;
  clearPixels();
  if (_blinkState) {
    setPixel(3, COL_ERROR, 100);
    setPixel(4, COL_ERROR, 100);
  }
  _strip.show();
}

bool LedController::renderBattery(unsigned long now) {
  if (!_showingBattery) return false;
  if (now - _batteryDisplayStart >= BAT_DISPLAY_DURATION_MS) {
    _showingBattery = false;
    return false;
  }
  clearPixels();
  for (uint8_t i = 0; i < _batteryLeds && i < NUM_LEDS; i++) {
    setPixel(i, COL_BATTERY[i], 100);
  }
  _strip.show();
  return true;
}

bool LedController::renderBargraph(unsigned long now) {
  if (!_showingPotBar) return false;
  if (now - _potBarStart >= _potBarDurationMs) {
    _showingPotBar = false;
    return false;
  }
  // Determine bar color based on foreground bank type.
  // Bar color : mode base. Dim : same hue ; intensity hardcoded at 30 %
  // for uncaught pot visualization (not derived from bgFactor — the bargraph
  // has its own aesthetic and is not a BG-bank rendering).
  const RGBW& barColor = (_slots && _slots[_currentBank].type == BANK_ARPEG)
                         ? _colors[CSLOT_MODE_ARPEG]
                         : _colors[CSLOT_MODE_NORMAL];
  const RGBW& barDim   = barColor;  // same hue, intensity scaled below

  clearPixels();

  uint8_t full = (uint8_t)_potBarRealLevel;           // fully-lit LED count
  float   frac = _potBarRealLevel - (float)full;       // fractional tip brightness

  if (_potBarCaught) {
    for (uint8_t i = 0; i < full && i < NUM_LEDS; i++) {
      setPixel(i, barColor, 100);
    }
    if (frac > 0.01f && full < NUM_LEDS) {
      setPixel(full, barColor, (uint8_t)(frac * 100.0f));
    }
  } else {
    // Dim bar = stored level (with fractional tip scaled to 30%)
    for (uint8_t i = 0; i < full && i < NUM_LEDS; i++) {
      setPixel(i, barDim, 30);
    }
    if (frac > 0.01f && full < NUM_LEDS) {
      setPixel(full, barDim, (uint8_t)(frac * 30.0f));
    }
    // Bright dot = physical pot position
    if (_potBarPotLevel < NUM_LEDS) {
      setPixel(_potBarPotLevel, barColor, 100);
    }
  }

  // Tempo pulse: tip LED blinks at BPM rate
  if (_potBarIsTempo && _potBarCaught && _potBarBpm > 0) {
    uint32_t periodMs = 60000UL / _potBarBpm;
    bool beatOn = ((now % periodMs) < (periodMs / 2));
    uint8_t tipLed = (uint8_t)_potBarRealLevel;
    if (tipLed >= NUM_LEDS) tipLed = NUM_LEDS - 1;
    if (!beatOn) {
      _strip.setPixelColor(tipLed, 0);  // Off phase of pulse
    }
  }

  _strip.show();
  return true;
}

bool LedController::renderConfirmation(unsigned long now) {
  // All event overlays are rendered in renderNormalDisplay() overlay section
  // via renderPattern(_eventOverlay, now). This method only tracks expiry :
  // it returns false when there is no active overlay (so renderNormalDisplay
  // can skip the overlay step).
  if (!_eventOverlay.active) return false;
  if (isPatternExpired(_eventOverlay, now)) {
    _eventOverlay.active = false;
    _eventOverlay.patternId = PTN_NONE;
    return false;
  }
  return true;
}

void LedController::renderCalibration(unsigned long now) {
  if (_validationFlashing) {
    unsigned long elapsed = now - _validationFlashStart;
    if (elapsed >= 150) {
      _validationFlashing = false;
    } else {
      uint8_t phase = elapsed / 25;
      bool on = (phase < 6) && (phase % 2 == 0);
      clearPixels();
      if (on) {
        for (uint8_t i = 0; i < NUM_LEDS; i++) {
          setPixel(i, COL_BOOT, 100);
        }
      }
      _strip.show();
      return;
    }
  }
  clearPixels();
  _strip.show();
}

// --- Per-type bank render (extracted for LOOP extensibility) ---

void LedController::renderBankNormal(uint8_t led, bool isFg) {
  // v4 : FG uses MODE_NORMAL. BG uses same color at reduced intensity
  // derived from FG via global _bgFactor (v6 unified grammar, post-audit fix).
  // Legacy field _normalBgIntensity kept in Store for NVS compat but unused.
  uint8_t intensity = isFg
                      ? _normalFgIntensity
                      : (uint8_t)((uint16_t)_normalFgIntensity * _bgFactor / 100);
  setPixel(led, _colors[CSLOT_MODE_NORMAL], intensity);
}

void LedController::renderBankArpeg(uint8_t led, bool isFg, unsigned long now) {
  const BankSlot& slot = _slots[led];
  bool playing = slot.arpEngine && slot.arpEngine->isPlaying() && slot.arpEngine->hasNotes();
  bool hasNotes = slot.arpEngine && slot.arpEngine->hasNotes();

  // Consume tick flag -> stash flash start time for this LED.
  // The tick "triggering" stays inline (negligible cost, no event pollution,
  // belongs to fond semantics). The tick "rendering" is delegated to
  // renderFlashOverlay() below — single source of truth for FLASH visual.
  if (slot.arpEngine && slot.arpEngine->consumeTickFlash()) {
    _flashStartTime[led] = now;
  }

  // v4 : same color for FG and BG, differentiated by intensity.
  const RGBW& col = _colors[CSLOT_MODE_ARPEG];

  if (playing) {
    // Base state : solid bright (FG) or solid dim (BG) — no pulse.
    // v4 : BG intensity derived from FG via _bgFactor (post-audit fix).
    // Legacy _bgArpPlayMin kept in Store for NVS compat but unused.
    uint8_t intensity = isFg
                        ? _fgArpPlayMax
                        : (uint8_t)((uint16_t)_fgArpPlayMax * _bgFactor / 100);
    setPixel(led, col, intensity);
    // Tick flash overlay : uses VERB_PLAY color (green, v4 grammar).
    // Replaces the base during the tickBeatDurationMs window (Phase 0.1 : BEAT kind only, ARPEG step).
    if (_flashStartTime[led] != 0) {
      if ((now - _flashStartTime[led]) < _tickBeatDurationMs) {
        renderFlashOverlay(led, _colors[CSLOT_VERB_PLAY], _tickFlashFg, _tickFlashBg,
                           _flashStartTime[led], _tickBeatDurationMs, isFg, now);
      } else {
        _flashStartTime[led] = 0;
      }
    }
  } else if (isFg && hasNotes) {
    // FG stopped with notes loaded : slow sine pulse (PULSE_SLOW semantic,
    // not yet driven by the pattern engine — step 0.6/0.8 will wire).
    uint16_t period = _pulsePeriodMs > 0 ? _pulsePeriodMs : 1;
    uint16_t phase = (uint16_t)((now * 65536UL / period) % 65536);
    uint8_t  idx   = phase >> 8;
    uint8_t  frac  = phase & 0xFF;
    uint16_t sine16 = (uint16_t)LED_SINE_LUT[idx] * (256 - frac)
                    + (uint16_t)LED_SINE_LUT[(idx + 1) & 0xFF] * frac;
    uint8_t intensity = _fgArpStopMin
                      + (uint8_t)((uint32_t)sine16 * (_fgArpStopMax - _fgArpStopMin) / 65280);
    setPixel(led, col, intensity);
  } else {
    // BG (all states) or FG idle (no notes) : solid dim.
    // v4 : BG intensity derived from FG via _bgFactor (post-audit fix).
    // Legacy _bgArpStopMin kept in Store for NVS compat but unused.
    uint8_t intensity = isFg
                        ? _fgArpStopMin
                        : (uint8_t)((uint16_t)_fgArpStopMin * _bgFactor / 100);
    setPixel(led, col, intensity);
  }
}

void LedController::renderNormalDisplay(unsigned long now) {
  clearPixels();

  if (_slots) {
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      bool isFg = (i == _currentBank);
      switch (_slots[i].type) {
        case BANK_NORMAL: renderBankNormal(i, isFg); break;
        case BANK_ARPEG:  renderBankArpeg(i, isFg, now); break;
      }

      // Battery low override: 3-blink burst on foreground bank
      if (isFg && _batteryLow) {
        unsigned long elapsed = now - _batLowLastBurstTime;
        if (elapsed >= BAT_LOW_BLINK_INTERVAL_MS) {
          _batLowLastBurstTime = now;
          elapsed = 0;
        }
        uint32_t burstDuration = (uint32_t)BAT_LOW_BLINK_SPEED_MS * 6;
        if (elapsed < burstDuration) {
          uint8_t phase = elapsed / BAT_LOW_BLINK_SPEED_MS;
          if (phase % 2 != 0) {
            _strip.setPixelColor(i, 0);
          }
        }
      }
    }
  } else {
    // Fallback: no slots pointer (pre-init)
    setPixel(_currentBank, _colors[CSLOT_MODE_NORMAL], _normalFgIntensity);
  }

  // --- Event overlay (unified pattern engine — step 0.4) ---
  // All events (bank switch, scale, octave, hold capture, etc.) flow through
  // renderPattern() which picks up _eventOverlay populated by triggerEvent()
  // by triggerEvent(). renderConfirmation() has already
  // cleared expired overlays before we get here.
  if (_eventOverlay.active) {
    renderPattern(_eventOverlay, now);
  }

  _strip.show();
}

// =================================================================
// Bank Display
// =================================================================

void LedController::setBrightness(uint8_t potValue) {
  _brightnessPct = POT_BRIGHTNESS_CURVE[potValue];
}

void LedController::setCurrentBank(uint8_t bank) {
  if (bank < NUM_BANKS) _currentBank = bank;
}

void LedController::setBatteryLow(bool low) {
  _batteryLow = low;
}

void LedController::setBankSlots(const BankSlot* slots) {
  _slots = slots;
}


// =================================================================
// Confirmation Blinks
// =================================================================

void LedController::triggerEvent(EventId evt, uint8_t ledMask) {
  if (evt >= EVT_COUNT) return;

  // --- Resolve render entry : NVS override > compile-time default ---
  // Phase 0 step 0.8d : _eventOverrides[] is populated from LedSettingsStore
  // v6 eventOverrides[EVT_COUNT] at loadLedSettings(). PTN_NONE sentinel
  // falls back to EVENT_RENDER_DEFAULT. If the default is also PTN_NONE
  // (LOOP reserved events in Phase 0), no render.
  EventRenderEntry entry = _eventOverrides[evt];
  if (entry.patternId == PTN_NONE) {
    entry = EVENT_RENDER_DEFAULT[evt];
    if (entry.patternId == PTN_NONE) return;
  }

  // --- Populate the PatternInstance ---
  _eventOverlay.patternId = entry.patternId;
  _eventOverlay.fgPct     = entry.fgPct;
  _eventOverlay.ledMask   = ledMask;
  _eventOverlay.startTime = millis();
  _eventOverlay.colorA    = colorForSlot(entry.colorSlot);
  _eventOverlay.colorB    = _eventOverlay.colorA;  // used only by CROSSFADE_COLOR

  // --- Resolve pattern-specific params from the legacy v5 settings fields ---
  // (step 0.8d will let the user override these via Tool 8, but for Phase 0
  // they are driven by bankDurationMs / holdOnFadeMs / etc. as before.)
  switch (entry.patternId) {
    case PTN_BLINK_SLOW: {
      uint16_t durationMs = _bankDurationMs;
      uint8_t  cycles     = _bankBlinks > 0 ? _bankBlinks : 1;
      if (evt == EVT_BANK_SWITCH) {
        // BANK_SWITCH uses bankBrightnessPct, not the default 100 fgPct,
        // to preserve v5 visual behavior. Override.
        _eventOverlay.fgPct = _bankBrightnessPct;
      }
      uint16_t cycleMs = durationMs / cycles;
      uint16_t halfMs  = cycleMs / 2;
      _eventOverlay.params.blinkSlow.onMs        = halfMs;
      _eventOverlay.params.blinkSlow.offMs       = cycleMs - halfMs;
      _eventOverlay.params.blinkSlow.cycles      = cycles;
      _eventOverlay.params.blinkSlow.blackoutOff = 1;  // BANK_SWITCH blackouts during off (v5 behavior)
      break;
    }
    case PTN_BLINK_FAST: {
      uint16_t durationMs = 300;
      uint8_t  cycles     = 2;
      uint8_t  blackout   = 1;
      switch (evt) {
        case EVT_SCALE_ROOT:
          durationMs = _scaleRootDurationMs;  cycles = _scaleRootBlinks;  break;
        case EVT_SCALE_MODE:
          durationMs = _scaleModeDurationMs;  cycles = _scaleModeBlinks;  break;
        case EVT_SCALE_CHROM:
          durationMs = _scaleChromDurationMs; cycles = _scaleChromBlinks; break;
        case EVT_OCTAVE:
          durationMs = _octaveDurationMs;     cycles = _octaveBlinks;
          blackout = 0;  // OCTAVE preserves bank base during off (v5 behavior)
          break;
        default: break;
      }
      if (cycles == 0) cycles = 1;
      uint16_t cycleMs = durationMs / cycles;
      uint16_t halfMs  = cycleMs / 2;
      _eventOverlay.params.blinkFast.onMs        = halfMs;
      _eventOverlay.params.blinkFast.offMs       = cycleMs - halfMs;
      _eventOverlay.params.blinkFast.cycles      = cycles;
      _eventOverlay.params.blinkFast.blackoutOff = blackout;
      break;
    }
    case PTN_FADE: {
      uint16_t durationMs = 500;
      uint8_t  startPct = 0, endPct = 100;
      if (evt == EVT_PLAY)       { durationMs = _holdOnFadeMs;  startPct = 0;   endPct = 100; }
      else if (evt == EVT_STOP)  { durationMs = _holdOffFadeMs; startPct = 100; endPct = 0;   }
      _eventOverlay.params.fade.durationMs = durationMs;
      _eventOverlay.params.fade.startPct   = startPct;
      _eventOverlay.params.fade.endPct     = endPct;
      break;
    }
    case PTN_FLASH: {
      _eventOverlay.params.flash.durationMs = _tickBeatDurationMs;
      _eventOverlay.params.flash.fgPct      = _tickFlashFg;
      _eventOverlay.params.flash.bgPct      = _tickFlashBg;
      break;
    }
    case PTN_SPARK: {
      // SPARK params come from LedSettingsStore v6 fields, loaded into
      // _sparkOnMs/_sparkGapMs/_sparkCycles by loadLedSettings().
      // Editable via Tool 8 PATTERNS page (SPARK row).
      _eventOverlay.params.spark.onMs   = _sparkOnMs;
      _eventOverlay.params.spark.gapMs  = _sparkGapMs;
      _eventOverlay.params.spark.cycles = _sparkCycles;
      break;
    }
    case PTN_CROSSFADE_COLOR: {
      // Used by WAITING_* events (LOOP phase). Period default 800 ms.
      _eventOverlay.params.crossfadeColor.periodMs = 800;
      // colorB = VERB_PLAY — not yet a dedicated slot in Phase 0, fallback to colorA
      _eventOverlay.colorB = _eventOverlay.colorA;
      break;
    }
    case PTN_RAMP_HOLD:
      // rampMs stays a Phase 1+ derivation from SettingsStore timers
      // (clearLoopTimerMs / slotSaveTimerMs / slotClearTimerMs per event).
      // Suffix params (onMs/gapMs/cycles) are shared with SPARK, coming
      // from LedSettingsStore v6 and editable via Tool 8 PATTERNS page.
      _eventOverlay.params.rampHold.rampMs       = 500;  // Phase 1+ will derive per-event
      _eventOverlay.params.rampHold.suffixOnMs   = _sparkOnMs;
      _eventOverlay.params.rampHold.suffixGapMs  = _sparkGapMs;
      _eventOverlay.params.rampHold.suffixCycles = _sparkCycles;
      break;
    case PTN_PULSE_SLOW:
    case PTN_SOLID:
      // Not typically used as event overlays (they belong to bank backgrounds).
      // Populate anyway for robustness.
      _eventOverlay.params.pulseSlow.minPct    = 30;
      _eventOverlay.params.pulseSlow.maxPct    = 100;
      _eventOverlay.params.pulseSlow.periodMs  = _pulsePeriodMs;
      break;
    default: break;
  }

  _eventOverlay.active = true;
}

RGBW LedController::colorForSlot(uint8_t slotId) const {
  // v4 : trivial array access. 15 slots defined by the ColorSlotId enum.
  if (slotId >= COLOR_SLOT_COUNT) return _colors[CSLOT_MODE_NORMAL];
  return _colors[slotId];
}

bool LedController::isPatternExpired(const PatternInstance& inst, unsigned long now) const {
  if (!inst.active) return true;
  unsigned long elapsed = now - inst.startTime;

  switch (inst.patternId) {
    case PTN_BLINK_SLOW:
    case PTN_BLINK_FAST: {
      const auto& p = inst.params.blinkFast;  // same layout as blinkSlow
      unsigned long totalMs = (unsigned long)(p.onMs + p.offMs) * p.cycles;
      return elapsed >= totalMs;
    }
    case PTN_FADE:
      return elapsed >= inst.params.fade.durationMs;
    case PTN_FLASH:
      return elapsed >= inst.params.flash.durationMs;
    case PTN_SPARK: {
      const auto& p = inst.params.spark;
      unsigned long totalMs = (unsigned long)(p.onMs + p.gapMs) * p.cycles;
      return elapsed >= totalMs;
    }
    case PTN_RAMP_HOLD: {
      // RAMP_HOLD only expires after the SPARK suffix completes.
      const auto& p = inst.params.rampHold;
      unsigned long suffixMs = (unsigned long)(p.suffixOnMs + p.suffixGapMs) * p.suffixCycles;
      return elapsed >= ((unsigned long)p.rampMs + suffixMs);
    }
    case PTN_CROSSFADE_COLOR:
    case PTN_PULSE_SLOW:
    case PTN_SOLID:
      // Continuous patterns : never expire autonomously. Caller must clear
      // _eventOverlay.active manually (e.g., when WAITING_* state exits).
      return false;
    default:
      return true;  // unknown pattern : treat as expired
  }
}

void LedController::renderPattern(const PatternInstance& inst, unsigned long now) {
  if (!inst.active || inst.patternId == PTN_NONE) return;
  unsigned long elapsed = now - inst.startTime;
  uint8_t mask = (inst.ledMask != 0) ? inst.ledMask : (uint8_t)(1 << _currentBank);

  switch (inst.patternId) {
    case PTN_BLINK_SLOW:
    case PTN_BLINK_FAST: {
      const auto& p = inst.params.blinkFast;  // same layout
      uint16_t cycleMs = p.onMs + p.offMs;
      if (cycleMs == 0) return;
      unsigned long posInCycle = elapsed % cycleMs;
      bool on = (posInCycle < p.onMs);
      for (uint8_t led = 0; led < NUM_LEDS; led++) {
        if (!(mask & (1 << led))) continue;
        if (on) {
          setPixel(led, inst.colorA, inst.fgPct);
        } else if (p.blackoutOff) {
          _strip.setPixelColor(led, 0);
        }
        // else : transparent off (OCTAVE semantic) — bank base shows through
      }
      break;
    }
    case PTN_FADE: {
      const auto& p = inst.params.fade;
      if (p.durationMs == 0) {
        // Instant : paint endPct and stop (renderConfirmation will expire next frame)
        for (uint8_t led = 0; led < NUM_LEDS; led++) {
          if (mask & (1 << led)) setPixel(led, inst.colorA, p.endPct);
        }
        return;
      }
      unsigned long e = (elapsed > p.durationMs) ? p.durationMs : elapsed;
      int16_t delta = (int16_t)p.endPct - (int16_t)p.startPct;
      int32_t val   = (int32_t)p.startPct + ((int32_t)delta * (int32_t)e) / (int32_t)p.durationMs;
      if (val < 0) val = 0;
      if (val > 100) val = 100;
      for (uint8_t led = 0; led < NUM_LEDS; led++) {
        if (mask & (1 << led)) setPixel(led, inst.colorA, (uint8_t)val);
      }
      break;
    }
    case PTN_FLASH: {
      const auto& p = inst.params.flash;
      if (elapsed >= p.durationMs) return;
      // Event-level FLASH : paint fgPct over masked LEDs.
      for (uint8_t led = 0; led < NUM_LEDS; led++) {
        if (mask & (1 << led)) setPixel(led, inst.colorA, p.fgPct);
      }
      break;
    }
    case PTN_SPARK: {
      const auto& p = inst.params.spark;
      uint16_t cycleMs = p.onMs + p.gapMs;
      if (cycleMs == 0) return;
      unsigned long posInCycle = elapsed % cycleMs;
      bool on = (posInCycle < p.onMs);
      for (uint8_t led = 0; led < NUM_LEDS; led++) {
        if (!(mask & (1 << led))) continue;
        if (on) setPixel(led, inst.colorA, inst.fgPct);
        // else : transparent gap between flashes (bank base shows through)
      }
      break;
    }
    case PTN_CROSSFADE_COLOR: {
      const auto& p = inst.params.crossfadeColor;
      if (p.periodMs == 0) return;
      // Sine interp between colorA and colorB over periodMs (ease-in-out)
      uint32_t phase = elapsed % p.periodMs;
      float t = (float)phase / (float)p.periodMs;  // 0..1
      float s = 0.5f - 0.5f * cosf(t * 2.0f * 3.14159265f);  // 0..1..0 ease
      auto lerpChan = [&](uint8_t a, uint8_t b) -> uint8_t {
        return (uint8_t)((1.0f - s) * a + s * b);
      };
      RGBW mixed;
      mixed.r = lerpChan(inst.colorA.r, inst.colorB.r);
      mixed.g = lerpChan(inst.colorA.g, inst.colorB.g);
      mixed.b = lerpChan(inst.colorA.b, inst.colorB.b);
      mixed.w = lerpChan(inst.colorA.w, inst.colorB.w);
      for (uint8_t led = 0; led < NUM_LEDS; led++) {
        if (mask & (1 << led)) setPixel(led, mixed, inst.fgPct);
      }
      break;
    }
    case PTN_RAMP_HOLD: {
      const auto& p = inst.params.rampHold;
      if (elapsed < p.rampMs) {
        // Ramp phase : linear 0 -> fgPct
        uint32_t val = (uint32_t)inst.fgPct * elapsed / p.rampMs;
        for (uint8_t led = 0; led < NUM_LEDS; led++) {
          if (mask & (1 << led)) setPixel(led, inst.colorA, (uint8_t)val);
        }
      } else {
        // Suffix SPARK phase
        unsigned long suffixElapsed = elapsed - p.rampMs;
        uint16_t cycleMs = p.suffixOnMs + p.suffixGapMs;
        if (cycleMs == 0) return;
        unsigned long posInCycle = suffixElapsed % cycleMs;
        bool on = (posInCycle < p.suffixOnMs);
        for (uint8_t led = 0; led < NUM_LEDS; led++) {
          if (!(mask & (1 << led))) continue;
          if (on) setPixel(led, inst.colorA, 100);
          // else transparent gap
        }
      }
      break;
    }
    case PTN_SOLID: {
      const auto& p = inst.params.solid;
      for (uint8_t led = 0; led < NUM_LEDS; led++) {
        if (mask & (1 << led)) setPixel(led, inst.colorA, p.pct);
      }
      break;
    }
    case PTN_PULSE_SLOW: {
      const auto& p = inst.params.pulseSlow;
      if (p.periodMs == 0) return;
      uint32_t phase = elapsed % p.periodMs;
      float t = (float)phase / (float)p.periodMs;
      float s = 0.5f - 0.5f * cosf(t * 2.0f * 3.14159265f);
      uint8_t val = (uint8_t)((1.0f - s) * p.minPct + s * p.maxPct);
      for (uint8_t led = 0; led < NUM_LEDS; led++) {
        if (mask & (1 << led)) setPixel(led, inst.colorA, val);
      }
      break;
    }
    default: break;
  }
}

void LedController::renderFlashOverlay(uint8_t led, const RGBW& color, uint8_t fgPct,
                                        uint8_t bgPct, unsigned long startTime,
                                        uint16_t durationMs, bool isFg, unsigned long now) {
  // Shared FLASH rendering logic, used inline by tick ARPEG (step 0.5) AND
  // by the pattern engine's PTN_FLASH case. Keeps the visual semantics of
  // FLASH in one place : during the duration, pixel is replaced by color
  // at fgPct (if isFg) or bgPct (if BG). Caller controls the timing.
  unsigned long elapsed = now - startTime;
  if (elapsed >= durationMs) return;
  uint8_t pct = isFg ? fgPct : bgPct;
  setPixel(led, color, pct);
}

void LedController::setPotBarDuration(uint16_t ms) {
  _potBarDurationMs = ms;
}

void LedController::loadLedSettings(const LedSettingsStore& s) {
  _normalFgIntensity = s.normalFgIntensity;
  _normalBgIntensity = s.normalBgIntensity;
  // Guard against inverted min/max from corrupted NVS
  _fgArpStopMin = s.fgArpStopMin;
  _fgArpStopMax = (s.fgArpStopMax >= s.fgArpStopMin) ? s.fgArpStopMax : s.fgArpStopMin;
  _fgArpPlayMax = s.fgArpPlayMax;
  _bgArpStopMin = s.bgArpStopMin;
  _bgArpPlayMin = s.bgArpPlayMin;
  _tickFlashFg = s.tickFlashFg;
  _tickFlashBg = s.tickFlashBg;
  _pulsePeriodMs = s.pulsePeriodMs;
  _tickBeatDurationMs = s.tickBeatDurationMs;
  _tickBarDurationMs  = s.tickBarDurationMs;   // Phase 0.1 : cached, consumed Phase 1+ (LoopEngine bar flash).
  _tickWrapDurationMs = s.tickWrapDurationMs;  // Phase 0.1 : cached, consumed Phase 1+ (LoopEngine wrap flash).
  _bankBlinks = (s.bankBlinks > 0) ? s.bankBlinks : 3;
  _bankDurationMs = s.bankDurationMs;
  _bankBrightnessPct = s.bankBrightnessPct;
  _scaleRootBlinks = s.scaleRootBlinks;
  _scaleRootDurationMs = s.scaleRootDurationMs;
  _scaleModeBlinks = s.scaleModeBlinks;
  _scaleModeDurationMs = s.scaleModeDurationMs;
  _scaleChromBlinks = s.scaleChromBlinks;
  _scaleChromDurationMs = s.scaleChromDurationMs;
  _holdOnFadeMs = s.holdOnFadeMs;
  _holdOffFadeMs = s.holdOffFadeMs;
  _octaveBlinks = s.octaveBlinks;
  _octaveDurationMs = s.octaveDurationMs;
  _sparkOnMs   = s.sparkOnMs;
  _sparkGapMs  = s.sparkGapMs;
  _sparkCycles = s.sparkCycles;
  _bgFactor    = s.bgFactor;
  // Copy per-event overrides array — consumed by triggerEvent() for NVS-driven
  // per-event customization (Tool 8 EVENTS page, step 0.8d).
  for (uint8_t i = 0; i < EVT_COUNT; i++) {
    _eventOverrides[i] = s.eventOverrides[i];
  }
  rebuildGammaLut(s.gammaTenths > 0 ? s.gammaTenths : 20);
}

void LedController::loadColorSlots(const ColorSlotStore& store) {
  // v5 (Phase 0.1) : iterate over all 16 slots (was 15 in v4). Loop driven by
  // COLOR_SLOT_COUNT so future bumps pick up automatically. Validator in
  // ColorSlotStore already clamped preset IDs ; resolveColorSlot is defensive.
  for (uint8_t i = 0; i < COLOR_SLOT_COUNT; i++) {
    _colors[i] = resolveColorSlot(store.slots[i]);
  }
}

// =================================================================
// Boot Sequence
// =================================================================

void LedController::showBootProgress(uint8_t step) {
  _bootStep = step;
  _bootMode = true;
}

void LedController::showBootFailure(uint8_t step) {
  _bootFailStep = step;
  _bootMode = true;
}

void LedController::endBoot() {
  _bootMode = false;
  _bootStep = 0;
  _bootFailStep = 0;
}

// =================================================================
// Error
// =================================================================

void LedController::setError(bool error) {
  _error = error;
}

// =================================================================
// Battery Gauge
// =================================================================

void LedController::showBatteryGauge(uint8_t percent) {
  if (percent > 100) percent = 100;
  _batteryLeds = (percent * 8 + 50) / 100;
  _showingBattery = true;
  _batteryDisplayStart = millis();
}

// =================================================================
// Pot Bargraph (with catch visualization)
// =================================================================

void LedController::showPotBargraph(float realLevel, uint8_t potLevel, bool caught) {
  _potBarRealLevel = (realLevel > (float)NUM_LEDS) ? (float)NUM_LEDS : (realLevel < 0.0f ? 0.0f : realLevel);
  _potBarPotLevel = (potLevel >= NUM_LEDS) ? (NUM_LEDS - 1) : potLevel;
  _potBarCaught = caught;
  _potBarIsTempo = false;
  _potBarStart = millis();
  _showingPotBar = true;
}

void LedController::showTempoBargraph(float realLevel, uint8_t potLevel, bool caught, uint16_t bpm) {
  _potBarRealLevel = (realLevel > (float)NUM_LEDS) ? (float)NUM_LEDS : (realLevel < 0.0f ? 0.0f : realLevel);
  _potBarPotLevel = (potLevel >= NUM_LEDS) ? (NUM_LEDS - 1) : potLevel;
  _potBarCaught = caught;
  _potBarIsTempo = true;
  _potBarBpm = bpm;
  _potBarStart = millis();
  _showingPotBar = true;
}

// =================================================================
// Calibration Feedback
// =================================================================

void LedController::setCalibrationMode(bool active) {
  _calibrationMode = active;
}

void LedController::playValidation() {
  _validationFlashStart = millis();
  _validationFlashing = true;
}

// =================================================================
// All Off
// =================================================================

void LedController::allOff() {
  clearPixels();
  _strip.show();

  _currentBank = 0;
  _batteryLow = false;
  _bootMode = false;
  _bootStep = 0;
  _bootFailStep = 0;
  _chaseActive = false;
  _setupComet = false;
  _calibrationMode = false;
  _validationFlashing = false;
  _error = false;
  _showingPotBar = false;
  _potBarIsTempo = false;
  _showingBattery = false;
  _eventOverlay.active = false;
  _eventOverlay.patternId = PTN_NONE;
  for (uint8_t i = 0; i < NUM_LEDS; i++) _flashStartTime[i] = 0;
}

// =================================================================
// Preview API (Tool 7 — direct LED control in setup mode)
// =================================================================

void LedController::previewBegin() {
  _previewMode = true;
  if (_setupComet) stopSetupComet();
  clearPixels();
  _strip.show();
}

void LedController::previewEnd() {
  _previewMode = false;
  clearPixels();
  _strip.show();
}

void LedController::previewSetPixel(uint8_t led, const RGBW& color, uint8_t intensityPct) {
  if (led >= NUM_LEDS) return;
  setPixel(led, color, intensityPct);
}

void LedController::previewClear() {
  clearPixels();
}

void LedController::previewShow() {
  _strip.show();
}

// ---------------------------------------------------------------------------
// Phase 0.1 — Tool 8 preview wrapper.
// Thin pass-through to renderPattern() so ToolLedPreview can inject arbitrary
// PatternInstance values without duplicating runtime code. Does NOT consult or
// mutate _eventOverlay. Caller owns inst.startTime and inst.ledMask.
// ---------------------------------------------------------------------------
void LedController::renderPreviewPattern(const PatternInstance& inst, unsigned long now) {
  renderPattern(inst, now);
}
