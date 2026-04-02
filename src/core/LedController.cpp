#include "LedController.h"
#include "HardwareConfig.h"
#include "KeyboardData.h"
#include "../arp/ArpEngine.h"
#include "../midi/ClockManager.h"
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
    _colNormalFg(COLOR_PRESETS[0]), _colNormalBg(COLOR_PRESETS[1]),
    _colArpFg(COLOR_PRESETS[3]), _colArpBg(COLOR_PRESETS[4]),
    _colTickFlash(COLOR_PRESETS[0]),
    _colBankSwitch(COLOR_PRESETS[0]), _colScaleRoot(COLOR_PRESETS[6]),
    _colScaleMode(COLOR_PRESETS[7]), _colScaleChrom(COLOR_PRESETS[7]),
    _colHold(COLOR_PRESETS[4]), _colPlayAck(COLOR_PRESETS[11]),
    _colStop(COLOR_PRESETS[5]), _colOctave(COLOR_PRESETS[9]),
    _normalFgIntensity(85), _normalBgIntensity(10),
    _fgArpStopMin(30), _fgArpStopMax(100),
    _fgArpPlayMin(30), _fgArpPlayMax(80),
    _bgArpStopMin(8), _bgArpStopMax(25),
    _bgArpPlayMin(8), _bgArpPlayMax(20),
    _tickFlashFg(100), _tickFlashBg(25),
    _pulsePeriodMs(1472), _tickFlashDurationMs(30),
    _bankBlinks(3), _bankDurationMs(300), _bankBrightnessPct(80),
    _scaleRootBlinks(2), _scaleRootDurationMs(200),
    _scaleModeBlinks(2), _scaleModeDurationMs(200),
    _scaleChromBlinks(2), _scaleChromDurationMs(200),
    _holdOnFlashMs(150), _holdFadeMs(300), _stopFadeMs(300),
    _playBeatCount(3),
    _octaveBlinks(3), _octaveDurationMs(300),
    _clock(nullptr),
    _confirmType(CONFIRM_NONE),
    _confirmStart(0),
    _confirmParam(0),
    _fadeStartTime(0),
    _playFlashPhase(0),
    _playLastBeatTick(0),
    _potBarDurationMs(LED_BARGRAPH_DURATION_DEFAULT),
    _showingPotBar(false),
    _potBarRealLevel(0),
    _potBarPotLevel(0),
    _potBarCaught(true),
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
  // Precompute sine LUT -- 256 entries covering one full period (0-255 output range)
  // Only runs once at boot, no float in update()
  for (uint16_t i = 0; i < 256; i++) {
    _sineTable[i] = (uint8_t)(127.5f + 127.5f * sinf((float)i * 6.2831853f / 256.0f));
  }
  // Init tick flash timers
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    _flashStartTime[i] = 0;
  }
}

// =================================================================
// begin
// =================================================================

void LedController::begin() {
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
    GAMMA_LUT[(uint16_t)c.r * scaled / 255],
    GAMMA_LUT[(uint16_t)c.g * scaled / 255],
    GAMMA_LUT[(uint16_t)c.b * scaled / 255],
    GAMMA_LUT[(uint16_t)c.w * scaled / 255]
  );
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
  if (renderConfirmation(now)) {
    if (_confirmType != CONFIRM_BANK_SWITCH) return;
  }
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
  // Determine bar color based on foreground bank type
  const RGBW& barColor = (_slots && _slots[_currentBank].type == BANK_ARPEG) ? _colArpFg : _colNormalFg;
  const RGBW& barDim   = (_slots && _slots[_currentBank].type == BANK_ARPEG) ? _colArpBg : _colNormalBg;

  clearPixels();

  if (_potBarCaught) {
    for (uint8_t i = 0; i < _potBarRealLevel && i < NUM_LEDS; i++) {
      setPixel(i, barColor, 100);
    }
  } else {
    for (uint8_t i = 0; i < _potBarRealLevel && i < NUM_LEDS; i++) {
      setPixel(i, barDim, 30);
    }
    if (_potBarPotLevel < NUM_LEDS) {
      setPixel(_potBarPotLevel, barColor, 100);
    }
  }

  _strip.show();
  return true;
}

bool LedController::renderConfirmation(unsigned long now) {
  if (_confirmType == CONFIRM_NONE) return false;
  unsigned long elapsed = now - _confirmStart;

  switch (_confirmType) {
    case CONFIRM_BANK_SWITCH: {
      if (elapsed >= _bankDurationMs) {
        _confirmType = CONFIRM_NONE;
        return false;
      }
      return true;  // Active, but overlay handled by renderNormalDisplay
    }

    case CONFIRM_SCALE_ROOT: {
      if (elapsed < _scaleRootDurationMs) {
        uint16_t unitMs = _scaleRootDurationMs / (_scaleRootBlinks * 2);
        uint8_t phase = elapsed / (unitMs > 0 ? unitMs : 1);
        bool on = (phase % 2 == 0);
        clearPixels();
        if (on) setPixel(_currentBank, _colScaleRoot, 100);
        _strip.show();
        return true;
      }
      _confirmType = CONFIRM_NONE;
      return false;
    }

    case CONFIRM_SCALE_MODE: {
      if (elapsed < _scaleModeDurationMs) {
        uint16_t unitMs = _scaleModeDurationMs / (_scaleModeBlinks * 2);
        uint8_t phase = elapsed / (unitMs > 0 ? unitMs : 1);
        bool on = (phase % 2 == 0);
        clearPixels();
        if (on) setPixel(_currentBank, _colScaleMode, 100);
        _strip.show();
        return true;
      }
      _confirmType = CONFIRM_NONE;
      return false;
    }

    case CONFIRM_SCALE_CHROM: {
      if (elapsed < _scaleChromDurationMs) {
        uint16_t unitMs = _scaleChromDurationMs / (_scaleChromBlinks * 2);
        uint8_t phase = elapsed / (unitMs > 0 ? unitMs : 1);
        bool on = (phase % 2 == 0);
        clearPixels();
        if (on) setPixel(_currentBank, _colScaleChrom, 100);
        _strip.show();
        return true;
      }
      _confirmType = CONFIRM_NONE;
      return false;
    }

    case CONFIRM_HOLD_ON: {
      if (elapsed < (uint16_t)_holdOnFlashMs + 100) {
        clearPixels();
        if (elapsed < _holdOnFlashMs) {
          setPixel(_currentBank, _colHold, 100);
        }
        _strip.show();
        return true;
      }
      _confirmType = CONFIRM_NONE;
      return false;
    }

    case CONFIRM_HOLD_OFF: {
      if (elapsed < _holdFadeMs) {
        // Fade from 100% to 0% over _holdFadeMs
        uint8_t fadePct = (uint8_t)((uint32_t)(_holdFadeMs - elapsed) * 100 / _holdFadeMs);
        clearPixels();
        setPixel(_currentBank, _colHold, fadePct);
        _strip.show();
        return true;
      }
      _confirmType = CONFIRM_NONE;
      return false;
    }

    case CONFIRM_PLAY: {
      uint8_t playSteps = _playBeatCount + 1;
      if (_playFlashPhase == 255) {
        uint8_t ackUnitMs = LED_CONFIRM_UNIT_MS;
        if (elapsed < (uint16_t)ackUnitMs * 2) {
          bool on = (elapsed < ackUnitMs);
          clearPixels();
          if (on) setPixel(_currentBank, _colPlayAck, 100);
          _strip.show();
          return true;
        }
        _playFlashPhase = 0;
        if (_clock) {
          _playLastBeatTick = _clock->getCurrentTick();
        }
      }

      if (_playFlashPhase < playSteps) {
        // Intensity ramp: 50%, 75%, 100% for beat-synced flashes
        static const uint8_t playIntensity[] = {0, 50, 75, 100};

        if (_clock) {
          uint32_t currentTick = _clock->getCurrentTick();
          if (currentTick >= _playLastBeatTick + 24) {
            _playLastBeatTick = currentTick - (currentTick % 24);
            _fadeStartTime = now;
            _playFlashPhase++;
            if (_playFlashPhase >= _playBeatCount) {
              _confirmType = CONFIRM_NONE;
            }
          }
        } else {
          uint8_t beatPhase = (uint8_t)(elapsed / 200);
          if (beatPhase >= playSteps) {
            _confirmType = CONFIRM_NONE;
            return false;
          } else if (beatPhase > _playFlashPhase) {
            _fadeStartTime = now;
            _playFlashPhase = beatPhase;
          }
        }

        if (_fadeStartTime != 0 && (now - _fadeStartTime) < _tickFlashDurationMs) {
          uint8_t phase = (_playFlashPhase < 4) ? _playFlashPhase : 3;
          clearPixels();
          setPixel(_currentBank, _colStop, playIntensity[phase]);
          _strip.show();
          return true;
        }
      }
      return false;
    }

    case CONFIRM_STOP: {
      if (elapsed < _stopFadeMs) {
        uint8_t fadePct = (uint8_t)((uint32_t)(_stopFadeMs - elapsed) * 100 / _stopFadeMs);
        clearPixels();
        setPixel(_currentBank, _colStop, fadePct);
        _strip.show();
        return true;
      }
      _confirmType = CONFIRM_NONE;
      return false;
    }

    case CONFIRM_OCTAVE: {
      if (elapsed < _octaveDurationMs) {
        uint16_t unitMs = _octaveDurationMs / (_octaveBlinks * 2);
        uint8_t phase = elapsed / (unitMs > 0 ? unitMs : 1);
        bool on = (phase % 2 == 0);
        clearPixels();
        if (on && _confirmParam >= 1 && _confirmParam <= 4) {
          uint8_t baseLed = (_confirmParam - 1) * 2;
          setPixel(baseLed, _colOctave, 100);
          if (baseLed + 1 < NUM_LEDS) {
            setPixel(baseLed + 1, _colOctave, 100);
          }
        }
        _strip.show();
        return true;
      }
      _confirmType = CONFIRM_NONE;
      return false;
    }

    default:
      _confirmType = CONFIRM_NONE;
      return false;
  }
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

void LedController::renderNormalDisplay(unsigned long now) {
  clearPixels();

  if (_slots) {
    // 16-bit phase + linear interpolation for smooth sine pulse.
    // phase: 0..65535 maps to one full sine period.
    // idx: top 8 bits select LUT entry, frac: bottom 8 bits interpolate.
    uint16_t period = _pulsePeriodMs > 0 ? _pulsePeriodMs : 1;
    uint16_t phase = (uint16_t)((now * 65536UL / period) % 65536);
    uint8_t  idx   = phase >> 8;
    uint8_t  frac  = phase & 0xFF;
    uint16_t sine16 = (uint16_t)_sineTable[idx] * (256 - frac)
                    + (uint16_t)_sineTable[(idx + 1) & 0xFF] * frac;  // 0..65280

    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      const BankSlot& slot = _slots[i];
      bool isFg = (i == _currentBank);

      if (slot.type == BANK_NORMAL) {
        // NORMAL: solid color at configured intensity
        setPixel(i, isFg ? _colNormalFg : _colNormalBg,
                 isFg ? _normalFgIntensity : _normalBgIntensity);
      } else {
        // ARPEG: sine pulse + optional tick flash
        bool playing = slot.arpEngine && slot.arpEngine->isPlaying() && slot.arpEngine->hasNotes();

        if (slot.arpEngine && slot.arpEngine->consumeTickFlash()) {
          _flashStartTime[i] = now;
        }
        bool flashing = (_flashStartTime[i] != 0) &&
                         ((now - _flashStartTime[i]) < _tickFlashDurationMs);

        // Pick min/max and color based on fg/bg and play state
        uint8_t pMin, pMax;
        const RGBW& col = isFg ? _colArpFg : _colArpBg;
        if (isFg) {
          pMin = playing ? _fgArpPlayMin : _fgArpStopMin;
          pMax = playing ? _fgArpPlayMax : _fgArpStopMax;
        } else {
          pMin = playing ? _bgArpPlayMin : _bgArpStopMin;
          pMax = playing ? _bgArpPlayMax : _bgArpStopMax;
        }

        if (flashing) {
          // Tick flash overrides pulse — use tick flash color + intensity
          setPixel(i, _colTickFlash, isFg ? _tickFlashFg : _tickFlashBg);
        } else {
          // Interpolated sine → intensity (full precision before setPixel gamma)
          uint8_t intensity = pMin + (uint8_t)((uint32_t)sine16 * (pMax - pMin) / 65280);
          setPixel(i, col, intensity);
        }
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
    setPixel(_currentBank, _colNormalFg, _normalFgIntensity);
  }

  // --- CONFIRM_BANK_SWITCH overlay ---
  if (_confirmType == CONFIRM_BANK_SWITCH) {
    unsigned long elapsed = now - _confirmStart;
    if (elapsed < _bankDurationMs) {
      uint16_t unitMs = _bankDurationMs / (_bankBlinks * 2);
      uint8_t phase = elapsed / (unitMs > 0 ? unitMs : 1);
      bool on = (phase % 2 == 0);
      if (on) {
        setPixel(_currentBank, _colBankSwitch, _bankBrightnessPct);
      } else {
        _strip.setPixelColor(_currentBank, 0);
      }
    }
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

void LedController::setClockManager(const ClockManager* clock) {
  _clock = clock;
}

// =================================================================
// Confirmation Blinks
// =================================================================

void LedController::triggerConfirm(ConfirmType type, uint8_t param) {
  _confirmType = type;
  _confirmStart = millis();
  _confirmParam = param;

  // Special init for play confirmation
  if (type == CONFIRM_PLAY) {
    _playFlashPhase = 255;  // 255 = ack sentinel; 0 = waiting for beat 1
    _fadeStartTime = 0;  // Used as flash hold timer during beat-synced phases
    if (_clock) {
      _playLastBeatTick = _clock->getCurrentTick();
    }
  }
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
  _fgArpPlayMin = s.fgArpPlayMin;
  _fgArpPlayMax = (s.fgArpPlayMax >= s.fgArpPlayMin) ? s.fgArpPlayMax : s.fgArpPlayMin;
  _bgArpStopMin = s.bgArpStopMin;
  _bgArpStopMax = (s.bgArpStopMax >= s.bgArpStopMin) ? s.bgArpStopMax : s.bgArpStopMin;
  _bgArpPlayMin = s.bgArpPlayMin;
  _bgArpPlayMax = (s.bgArpPlayMax >= s.bgArpPlayMin) ? s.bgArpPlayMax : s.bgArpPlayMin;
  _tickFlashFg = s.tickFlashFg;
  _tickFlashBg = s.tickFlashBg;
  _pulsePeriodMs = s.pulsePeriodMs;
  _tickFlashDurationMs = s.tickFlashDurationMs;
  _bankBlinks = (s.bankBlinks > 0) ? s.bankBlinks : 3;
  _bankDurationMs = s.bankDurationMs;
  _bankBrightnessPct = s.bankBrightnessPct;
  _scaleRootBlinks = s.scaleRootBlinks;
  _scaleRootDurationMs = s.scaleRootDurationMs;
  _scaleModeBlinks = s.scaleModeBlinks;
  _scaleModeDurationMs = s.scaleModeDurationMs;
  _scaleChromBlinks = s.scaleChromBlinks;
  _scaleChromDurationMs = s.scaleChromDurationMs;
  _holdOnFlashMs = s.holdOnFlashMs;
  _holdFadeMs = s.holdFadeMs;
  _stopFadeMs = s.stopFadeMs;
  _playBeatCount = s.playBeatCount;
  _octaveBlinks = s.octaveBlinks;
  _octaveDurationMs = s.octaveDurationMs;
}

void LedController::loadColorSlots(const ColorSlotStore& store) {
  _colNormalFg   = resolveColorSlot(store.slots[CSLOT_NORMAL_FG]);
  _colNormalBg   = resolveColorSlot(store.slots[CSLOT_NORMAL_BG]);
  _colArpFg      = resolveColorSlot(store.slots[CSLOT_ARPEG_FG]);
  _colArpBg      = resolveColorSlot(store.slots[CSLOT_ARPEG_BG]);
  _colTickFlash  = resolveColorSlot(store.slots[CSLOT_TICK_FLASH]);
  _colBankSwitch = resolveColorSlot(store.slots[CSLOT_BANK_SWITCH]);
  _colScaleRoot  = resolveColorSlot(store.slots[CSLOT_SCALE_ROOT]);
  _colScaleMode  = resolveColorSlot(store.slots[CSLOT_SCALE_MODE]);
  _colScaleChrom = resolveColorSlot(store.slots[CSLOT_SCALE_CHROM]);
  _colHold       = resolveColorSlot(store.slots[CSLOT_HOLD]);
  _colPlayAck    = resolveColorSlot(store.slots[CSLOT_PLAY_ACK]);
  _colStop       = resolveColorSlot(store.slots[CSLOT_STOP]);
  _colOctave     = resolveColorSlot(store.slots[CSLOT_OCTAVE]);
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

void LedController::showPotBargraph(uint8_t realLevel, uint8_t potLevel, bool caught) {
  _potBarRealLevel = (realLevel > NUM_LEDS) ? NUM_LEDS : realLevel;     // 0-8 (LED count)
  _potBarPotLevel = (potLevel >= NUM_LEDS) ? (NUM_LEDS - 1) : potLevel; // 0-7 (LED index)
  _potBarCaught = caught;
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
  _showingBattery = false;
  _confirmType = CONFIRM_NONE;
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
