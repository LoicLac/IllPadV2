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
  : _strip(NUM_LEDS, LED_DATA_PIN, NEO_GRB + NEO_KHZ800),
    _brightness(255),
    _currentBank(0),
    _batteryLow(false),
    _slots(nullptr),
    _normalFgIntensity(255), _normalBgIntensity(40),
    _fgArpStopMin(77), _fgArpStopMax(255),
    _fgArpPlayMin(77), _fgArpPlayMax(204),
    _fgTickFlash(255),
    _bgArpStopMin(20), _bgArpStopMax(64),
    _bgArpPlayMin(20), _bgArpPlayMax(51),
    _bgTickFlash(64), _absoluteMax(255),
    _pulsePeriodMs(1472), _tickFlashDurationMs(30),
    _bankBlinks(3), _bankDurationMs(300), _bankBrightnessPct(50),
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
    _batLowLastBurstTime(0)
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
// Pixel Helpers -- per-pixel brightness scaling (no strip.setBrightness)
// =================================================================

void LedController::setPixel(uint8_t i, const RGB& c) {
  _strip.setPixelColor(i, _strip.Color(
    (uint8_t)((uint16_t)c.r * _brightness / 255),
    (uint8_t)((uint16_t)c.g * _brightness / 255),
    (uint8_t)((uint16_t)c.b * _brightness / 255)
  ));
}

void LedController::setPixelScaled(uint8_t i, const RGB& c, uint8_t scale) {
  uint16_t combinedScale = (uint16_t)scale * _brightness / 255;
  _strip.setPixelColor(i, _strip.Color(
    (uint8_t)((uint16_t)c.r * combinedScale / 255),
    (uint8_t)((uint16_t)c.g * combinedScale / 255),
    (uint8_t)((uint16_t)c.b * combinedScale / 255)
  ));
}

void LedController::setPixelAbsolute(uint8_t i, const RGB& c) {
  // Capped by _absoluteMax (runtime ceiling for absolute events)
  _strip.setPixelColor(i, _strip.Color(
    (uint8_t)((uint16_t)c.r * _absoluteMax / 255),
    (uint8_t)((uint16_t)c.g * _absoluteMax / 255),
    (uint8_t)((uint16_t)c.b * _absoluteMax / 255)
  ));
}

void LedController::setPixelAbsoluteScaled(uint8_t i, const RGB& c, uint8_t scale) {
  // scale first, then cap by _absoluteMax
  uint16_t combined = (uint16_t)scale * _absoluteMax / 255;
  _strip.setPixelColor(i, _strip.Color(
    (uint8_t)((uint16_t)c.r * combined / 255),
    (uint8_t)((uint16_t)c.g * combined / 255),
    (uint8_t)((uint16_t)c.b * combined / 255)
  ));
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
      for (uint8_t i = 0; i < NUM_LEDS; i++) setPixelAbsolute(i, COL_ERROR);
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
  setPixel(0, COL_SETUP);
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
  unsigned long now = millis();

  // Shared 500ms blink timer (used by error mode)
  if (now - _lastBlinkTime >= 500) {
    _blinkState = !_blinkState;
    _lastBlinkTime = now;
  }

  // === Priority 1: Boot mode (progressive fill / failure blink) ===
  if (_bootMode) {
    clearPixels();
    if (_bootFailStep > 0) {
      bool fastBlink = ((now / 150) % 2) == 0;
      for (uint8_t i = 0; i < NUM_LEDS; i++) {
        if (i < _bootFailStep - 1) {
          setPixel(i, COL_BOOT);
        } else if (i == _bootFailStep - 1) {
          if (fastBlink) setPixelAbsolute(i, COL_BOOT_FAIL);  // Absolute: boot failure
          // else stays cleared (off)
        }
        // remaining LEDs stay cleared
      }
    } else {
      for (uint8_t i = 0; i < NUM_LEDS; i++) {
        if (i < _bootStep) {
          setPixel(i, COL_BOOT);
        }
      }
    }
    _strip.show();
    return;
  }

  // === Priority 2: Setup comet (ping-pong chase, skipped if in calibration) ===
  if (_setupComet && !_calibrationMode) {
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
    setPixel(headLed, COL_SETUP);
    // Trail -1 at 40% (if valid)
    uint8_t prevPos = (_cometPos == 0) ? 13 : (_cometPos - 1);
    uint8_t trail1Led;
    if (prevPos <= 7) {
      trail1Led = prevPos;
    } else {
      trail1Led = 14 - prevPos;
    }
    if (trail1Led != headLed) {
      setPixelScaled(trail1Led, COL_SETUP, 102);  // 40% of 255
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
      setPixelScaled(trail2Led, COL_SETUP, 26);  // 10% of 255
    }

    _strip.show();
    return;
  }

  // === Priority 3: Chase pattern (calibration entry) ===
  if (_chaseActive) {
    if (now - _chaseLastStep >= CHASE_STEP_MS) {
      _chasePos = (_chasePos + 1) % NUM_LEDS;
      _chaseLastStep = now;
    }
    clearPixels();
    setPixel(_chasePos, COL_SETUP);
    _strip.show();
    return;
  }

  // === Priority 4: Error (LEDs 3-4 blink red, 500ms) ===
  if (_error) {
    clearPixels();
    if (_blinkState) {
      setPixelAbsolute(3, COL_ERROR);  // Absolute: errors always visible
      setPixelAbsolute(4, COL_ERROR);
    }
    _strip.show();
    return;
  }

  // === Priority 5: Battery gauge (solid gradient bar, 3s) ===
  if (_showingBattery) {
    if (now - _batteryDisplayStart < BAT_DISPLAY_DURATION_MS) {
      clearPixels();
      for (uint8_t i = 0; i < _batteryLeds && i < NUM_LEDS; i++) {
        setPixel(i, COL_BATTERY[i]);
      }
      _strip.show();
      return;
    }
    _showingBattery = false;
  }

  // === Priority 6: Pot bargraph (solid/catch bar, configurable duration) ===
  if (_showingPotBar) {
    if (now - _potBarStart >= _potBarDurationMs) {
      _showingPotBar = false;
    } else {
      // Determine bar color based on foreground bank type
      RGB barColor = COL_WHITE;
      RGB barDim   = COL_WHITE_DIM;
      if (_slots && _slots[_currentBank].type == BANK_ARPEG) {
        barColor = COL_BLUE;
        barDim   = COL_BLUE_DIM;
      }

      clearPixels();

      if (_potBarCaught) {
        // Caught: full-color solid bar (realLevel = 0-8, number of LEDs)
        for (uint8_t i = 0; i < _potBarRealLevel && i < NUM_LEDS; i++) {
          setPixel(i, barColor);
        }
      } else {
        // Not caught: dim bar for real level + bright cursor for pot position
        for (uint8_t i = 0; i < _potBarRealLevel && i < NUM_LEDS; i++) {
          setPixel(i, barDim);
        }
        // Bright cursor at pot position (potLevel = 0-7, LED index)
        if (_potBarPotLevel < NUM_LEDS) {
          setPixel(_potBarPotLevel, barColor);
        }
      }

      _strip.show();
      return;
    }
  }

  // === Priority 7: Confirmation blinks (auto-expiring feedback) ===
  if (_confirmType != CONFIRM_NONE) {
    unsigned long elapsed = now - _confirmStart;

    switch (_confirmType) {
      case CONFIRM_BANK_SWITCH: {
        // Render normal display first, then overlay destination LED with triple blink
        // Fall through to normal display below, with overlay applied after
        if (elapsed >= _bankDurationMs) {
          _confirmType = CONFIRM_NONE;
        }
        // Don't return here -- fall through to normal display (handled there)
        break;
      }

      case CONFIRM_SCALE_ROOT: {
        if (elapsed < _scaleRootDurationMs) {
          uint16_t unitMs = _scaleRootDurationMs / (_scaleRootBlinks * 2);
          uint8_t phase = elapsed / (unitMs > 0 ? unitMs : 1);
          bool on = (phase % 2 == 0);
          clearPixels();
          if (on) setPixel(_currentBank, COL_SCALE_ROOT);
          _strip.show();
          return;
        }
        _confirmType = CONFIRM_NONE;
        break;
      }

      case CONFIRM_SCALE_MODE: {
        if (elapsed < _scaleModeDurationMs) {
          uint16_t unitMs = _scaleModeDurationMs / (_scaleModeBlinks * 2);
          uint8_t phase = elapsed / (unitMs > 0 ? unitMs : 1);
          bool on = (phase % 2 == 0);
          clearPixels();
          if (on) setPixel(_currentBank, COL_SCALE_MODE);
          _strip.show();
          return;
        }
        _confirmType = CONFIRM_NONE;
        break;
      }

      case CONFIRM_SCALE_CHROM: {
        if (elapsed < _scaleChromDurationMs) {
          uint16_t unitMs = _scaleChromDurationMs / (_scaleChromBlinks * 2);
          uint8_t phase = elapsed / (unitMs > 0 ? unitMs : 1);
          bool on = (phase % 2 == 0);
          clearPixels();
          if (on) setPixel(_currentBank, COL_SCALE_CHROM);
          _strip.show();
          return;
        }
        _confirmType = CONFIRM_NONE;
        break;
      }

      case CONFIRM_HOLD_ON: {
        // Sharp blink current LED in deep blue
        if (elapsed < (uint16_t)_holdOnFlashMs + 100) {
          clearPixels();
          if (elapsed < _holdOnFlashMs) {
            setPixel(_currentBank, COL_ARP_HOLD);
          }
          _strip.show();
          return;
        }
        _confirmType = CONFIRM_NONE;
        break;
      }

      case CONFIRM_HOLD_OFF: {
        // Fade-out from deep blue over _holdFadeMs
        if (elapsed < _holdFadeMs) {
          uint8_t fadeScale = (uint8_t)((uint32_t)(_holdFadeMs - elapsed) * 255 / _holdFadeMs);
          clearPixels();
          setPixelScaled(_currentBank, COL_ARP_HOLD, fadeScale);
          _strip.show();
          return;
        }
        _confirmType = CONFIRM_NONE;
        break;
      }

      case CONFIRM_PLAY: {
        // Phase 255 = immediate green ack flash (sentinel)
        // Phase 0    = ack done, waiting for beat 1
        // Phases 1-N = N beats fired (_playBeatCount beats total)
        uint8_t playSteps = _playBeatCount + 1;  // 1 ack + N beats
        if (_playFlashPhase == 255) {
          // Ack flash: show green for one unit (fixed, not coupled to bank switch params)
          uint8_t ackUnitMs = LED_CONFIRM_UNIT_MS;
          if (elapsed < (uint16_t)ackUnitMs * 2) {
            bool on = (elapsed < ackUnitMs);
            clearPixels();
            if (on) setPixelAbsolute(_currentBank, COL_PLAY_ACK);  // Absolute: play ack
            _strip.show();
            return;
          }
          // Ack done, advance to beat-synced phases (0 = no beats fired yet)
          _playFlashPhase = 0;
          if (_clock) {
            _playLastBeatTick = _clock->getCurrentTick();
          }
          // Fall through to normal display between beats
        }

        if (_playFlashPhase < playSteps) {
          static const uint8_t playIntensity[] = {0, 128, 191, 255};

          // Check for beat boundary (24 ticks = 1 quarter note)
          if (_clock) {
            uint32_t currentTick = _clock->getCurrentTick();
            if (currentTick >= _playLastBeatTick + 24) {
              _playLastBeatTick = currentTick - (currentTick % 24);
              _fadeStartTime = now;  // Start flash hold timer
              _playFlashPhase++;
              if (_playFlashPhase >= _playBeatCount) {
                _confirmType = CONFIRM_NONE;
              }
            }
          } else {
            // No clock: use time-based fallback (200ms per beat)
            // beatPhase > _playFlashPhase avoids collision at beat 1
            uint8_t beatPhase = (uint8_t)(elapsed / 200);
            if (beatPhase >= playSteps) {
              _confirmType = CONFIRM_NONE;
              break;
            } else if (beatPhase > _playFlashPhase) {
              _fadeStartTime = now;  // Start flash hold timer
              _playFlashPhase = beatPhase;
            }
          }

          // Hold flash for _tickFlashDurationMs (same as tick flash)
          if (_fadeStartTime != 0 && (now - _fadeStartTime) < _tickFlashDurationMs) {
            uint8_t phase = (_playFlashPhase < 4) ? _playFlashPhase : 3;
            clearPixels();
            setPixelAbsoluteScaled(_currentBank, COL_ARP_PLAY, playIntensity[phase]);  // Absolute: beat flash
            _strip.show();
            return;
          }
          // Between beats: fall through to normal display
        }
        break;
      }

      case CONFIRM_STOP: {
        // Fade-out from blue-cyan over _stopFadeMs (independent from hold-off)
        if (elapsed < _stopFadeMs) {
          uint8_t fadeScale = (uint8_t)((uint32_t)(_stopFadeMs - elapsed) * 255 / _stopFadeMs);
          clearPixels();
          setPixelScaled(_currentBank, COL_ARP_PLAY, fadeScale);
          _strip.show();
          return;
        }
        _confirmType = CONFIRM_NONE;
        break;
      }

      case CONFIRM_OCTAVE: {
        // Triple blink 2 LEDs of selected group
        // param 1 -> LEDs 0-1, param 2 -> LEDs 2-3, param 3 -> LEDs 4-5, param 4 -> LEDs 6-7
        if (elapsed < _octaveDurationMs) {
          uint16_t unitMs = _octaveDurationMs / (_octaveBlinks * 2);
          uint8_t phase = elapsed / (unitMs > 0 ? unitMs : 1);
          bool on = (phase % 2 == 0);
          clearPixels();
          if (on && _confirmParam >= 1 && _confirmParam <= 4) {
            uint8_t baseLed = (_confirmParam - 1) * 2;
            setPixel(baseLed, COL_ARP_OCTAVE);
            if (baseLed + 1 < NUM_LEDS) {
              setPixel(baseLed + 1, COL_ARP_OCTAVE);
            }
          }
          _strip.show();
          return;
        }
        _confirmType = CONFIRM_NONE;
        break;
      }

      default:
        _confirmType = CONFIRM_NONE;
        break;
    }
    // If confirmation was cleared, fall through to lower priorities
  }

  // === Priority 8: Calibration mode ===
  if (_calibrationMode) {
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
            setPixelAbsolute(i, COL_BOOT);  // Absolute: validation flash always visible
          }
        }
        _strip.show();
        return;
      }
    }
    clearPixels();
    _strip.show();
    return;
  }

  // === Priority 9: Normal bank display (multi-bank state) ===
  // Label used by CONFIRM_BANK_SWITCH to render normal display then overlay
  clearPixels();

  if (_slots) {
    // Sine LUT index: divide period into 256 steps
    const uint8_t lutStep = _pulsePeriodMs / 256;  // ~5.7ms
    uint8_t sineIdx = (uint8_t)((now / (lutStep > 0 ? lutStep : 1)) % 256);
    uint8_t sineRaw = _sineTable[sineIdx];

    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      const BankSlot& slot = _slots[i];
      bool isFg = (i == _currentBank);

      if (slot.type == BANK_NORMAL) {
        if (isFg) {
          // Foreground NORMAL: solid white (intensity from settings)
          setPixelScaled(i, COL_WHITE, _normalFgIntensity);
        } else {
          // Background NORMAL: dim white (intensity from settings)
          setPixelScaled(i, COL_WHITE, _normalBgIntensity);
        }
      } else {
        // ARPEG bank -- check play state and tick flash
        bool playing = slot.arpEngine && slot.arpEngine->isPlaying() && slot.arpEngine->hasNotes();

        // Consume tick flash: record start time on fresh tick
        if (slot.arpEngine && slot.arpEngine->consumeTickFlash()) {
          _flashStartTime[i] = now;
        }
        bool flashing = (_flashStartTime[i] != 0) &&
                         ((now - _flashStartTime[i]) < _tickFlashDurationMs);

        if (isFg) {
          if (playing) {
            // Foreground ARPEG playing: blue sine pulse + white tick flash override
            if (flashing) {
              RGB tickCol = {_fgTickFlash, _fgTickFlash, _fgTickFlash};
              setPixelAbsolute(i, tickCol);  // Absolute: tick flash always visible
            } else {
              uint8_t bVal = _fgArpPlayMin + (uint8_t)((uint16_t)sineRaw *
                       (_fgArpPlayMax - _fgArpPlayMin) / 255);
              RGB col = {0, 0, bVal};
              setPixel(i, col);
            }
          } else {
            // Foreground ARPEG stopped: blue sine pulse
            uint8_t bVal = _fgArpStopMin + (uint8_t)((uint16_t)sineRaw *
                     (_fgArpStopMax - _fgArpStopMin) / 255);
            RGB col = {0, 0, bVal};
            setPixel(i, col);
          }
        } else {
          if (playing) {
            // Background ARPEG playing: dimmed blue sine pulse + dimmed tick flash
            if (flashing) {
              RGB col = {0, 0, _bgTickFlash};
              setPixelAbsolute(i, col);  // Absolute: bg tick flash always visible
            } else {
              uint8_t bVal = _bgArpPlayMin + (uint8_t)((uint16_t)sineRaw *
                       (_bgArpPlayMax - _bgArpPlayMin) / 255);
              RGB col = {0, 0, bVal};
              setPixel(i, col);
            }
          } else {
            // Background ARPEG stopped: dimmed blue sine pulse
            uint8_t bVal = _bgArpStopMin + (uint8_t)((uint16_t)sineRaw *
                     (_bgArpStopMax - _bgArpStopMin) / 255);
            RGB col = {0, 0, bVal};
            setPixel(i, col);
          }
        }
      }

      // Battery low override: 3-blink burst on foreground bank (NORMAL or ARPEG)
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
            _strip.setPixelColor(i, 0);  // Off during blink
          }
        }
      }
    }
  } else {
    // Fallback: no slots pointer, simple single-bank display (pre-init)
    setPixel(_currentBank, COL_WHITE);
  }

  // --- CONFIRM_BANK_SWITCH overlay ---
  // Normal display is already rendered above. Override the destination bank LED
  // with a blink in context color (on top of the normal display).
  if (_confirmType == CONFIRM_BANK_SWITCH) {
    unsigned long elapsed = now - _confirmStart;
    if (elapsed < _bankDurationMs) {
      uint16_t unitMs = _bankDurationMs / (_bankBlinks * 2);
      uint8_t phase = elapsed / (unitMs > 0 ? unitMs : 1);
      bool on = (phase % 2 == 0);
      // Determine context color: white for NORMAL, blue for ARPEG
      RGB blinkColor = COL_WHITE;
      if (_slots && _slots[_currentBank].type == BANK_ARPEG) {
        blinkColor = COL_BLUE;
      }
      if (on) {
        setPixelScaled(_currentBank, blinkColor,
                       (uint8_t)((uint16_t)255 * _bankBrightnessPct / 100));
      } else {
        _strip.setPixelColor(_currentBank, 0);
      }
    }
    // Note: CONFIRM_BANK_SWITCH expiry was already handled in the confirmation section above
  }

  _strip.show();
}

// =================================================================
// Bank Display
// =================================================================

void LedController::setBrightness(uint8_t brightness) {
  _brightness = brightness;
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
  // Guard against inverted min/max from corrupted NVS (unsigned subtraction UB)
  _fgArpStopMin = s.fgArpStopMin;
  _fgArpStopMax = (s.fgArpStopMax >= s.fgArpStopMin) ? s.fgArpStopMax : s.fgArpStopMin;
  _fgArpPlayMin = s.fgArpPlayMin;
  _fgArpPlayMax = (s.fgArpPlayMax >= s.fgArpPlayMin) ? s.fgArpPlayMax : s.fgArpPlayMin;
  _fgTickFlash = s.fgTickFlash;
  _bgArpStopMin = s.bgArpStopMin;
  _bgArpStopMax = (s.bgArpStopMax >= s.bgArpStopMin) ? s.bgArpStopMax : s.bgArpStopMin;
  _bgArpPlayMin = s.bgArpPlayMin;
  _bgArpPlayMax = (s.bgArpPlayMax >= s.bgArpPlayMin) ? s.bgArpPlayMax : s.bgArpPlayMin;
  _bgTickFlash = s.bgTickFlash;
  _absoluteMax = s.absoluteMax;
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
