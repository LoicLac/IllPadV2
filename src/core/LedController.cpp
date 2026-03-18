#include "LedController.h"
#include "HardwareConfig.h"
#include <Arduino.h>
#if INT_LED
#include "esp32-hal-rgb-led.h"
#endif

// LED pin mapping (indices 0-7 correspond to LEDs 1-8, arranged in a circle)
const uint8_t LedController::_pins[NUM_LEDS] = {
  LED_PIN_1, LED_PIN_2, LED_PIN_3, LED_PIN_4,
  LED_PIN_5, LED_PIN_6, LED_PIN_7, LED_PIN_8
};

LedController::LedController()
  : _currentBank(0),
    _batteryLow(false),
    _bootMode(false),
    _bootStep(0),
    _bootFailStep(0),
    _chaseActive(false),
    _chasePos(0),
    _chaseLastStep(0),
    _calibrationMode(false),
    _validationFlashing(false),
    _validationFlashStart(0),
    _error(false),
    _lastBlinkTime(0),
    _blinkState(false),
    _showingBattery(false),
    _batteryLeds(0),
    _batteryDisplayStart(0),
    _showingPotBar(false),
    _potBarLevel(0),
    _potBarStart(0),
    _batLowLastBurstTime(0)
{
}

void LedController::begin() {
  for (int i = 0; i < NUM_LEDS; i++) {
    pinMode(_pins[i], OUTPUT);
    digitalWrite(_pins[i], LOW);
  }
}

// ---------------------------------------------------------------
// I2C error halt — 3 rapid flashes + 1s off, loops forever
// ---------------------------------------------------------------

void LedController::haltI2CError() {
  #if INT_LED
  neopixelWrite(RGB_LED_PIN, RGB_LED_BRIGHTNESS, 0, 0);
  #endif
  while (true) {
    for (uint8_t flash = 0; flash < 3; flash++) {
      for (int i = 0; i < NUM_LEDS; i++) digitalWrite(_pins[i], HIGH);
      delay(80);
      for (int i = 0; i < NUM_LEDS; i++) digitalWrite(_pins[i], LOW);
      delay(80);
    }
    delay(1000);
  }
}

// ---------------------------------------------------------------
// Chase pattern — single LED circling (calibration entry feedback)
// ---------------------------------------------------------------

void LedController::startChase() {
  _chaseActive = true;
  _chasePos = 0;
  _chaseLastStep = millis();
  for (int i = 0; i < NUM_LEDS; i++) digitalWrite(_pins[i], LOW);
  digitalWrite(_pins[0], HIGH);
}

void LedController::stopChase() {
  _chaseActive = false;
  for (int i = 0; i < NUM_LEDS; i++) digitalWrite(_pins[i], LOW);
}

// ---------------------------------------------------------------
// Main update — call from loop() every cycle
// ---------------------------------------------------------------

void LedController::update() {
  unsigned long now = millis();

  // Shared 500ms blink timer (used by error mode)
  if (now - _lastBlinkTime >= 500) {
    _blinkState = !_blinkState;
    _lastBlinkTime = now;
  }

  // === Boot mode (progressive fill / failure blink) ===
  if (_bootMode) {
    if (_bootFailStep > 0) {
      bool fastBlink = ((now / 150) % 2) == 0;
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i < _bootFailStep - 1) {
          digitalWrite(_pins[i], HIGH);
        } else if (i == _bootFailStep - 1) {
          digitalWrite(_pins[i], fastBlink ? HIGH : LOW);
        } else {
          digitalWrite(_pins[i], LOW);
        }
      }
      #if INT_LED
      neopixelWrite(RGB_LED_PIN, RGB_LED_BRIGHTNESS, 0, 0);
      #endif
    } else {
      for (int i = 0; i < NUM_LEDS; i++) {
        digitalWrite(_pins[i], (i < _bootStep) ? HIGH : LOW);
      }
      #if INT_LED
      neopixelWrite(RGB_LED_PIN, 0, 0, RGB_LED_BRIGHTNESS);
      #endif
    }
    return;
  }

  // === Chase pattern (calibration entry) ===
  if (_chaseActive) {
    if (now - _chaseLastStep >= CHASE_STEP_MS) {
      digitalWrite(_pins[_chasePos], LOW);
      _chasePos = (_chasePos + 1) % NUM_LEDS;
      digitalWrite(_pins[_chasePos], HIGH);
      _chaseLastStep = now;
    }
    return;
  }

  // === Priority 1: Error (all 8 LEDs blink 500ms unison) ===
  if (_error) {
    for (int i = 0; i < NUM_LEDS; i++) {
      digitalWrite(_pins[i], _blinkState ? HIGH : LOW);
    }
    #if INT_LED
    neopixelWrite(RGB_LED_PIN, _blinkState ? RGB_LED_BRIGHTNESS : 0, 0, 0);
    #endif
    return;
  }

  // === Priority 2: Battery gauge (8-LED bar, heartbeat pulse, 3s) ===
  if (_showingBattery) {
    if (now - _batteryDisplayStart < BAT_DISPLAY_DURATION_MS) {
      // Asymmetric triangle: fast rise (200ms) + slow fall (800ms) = 1s period
      unsigned long phase = now % 1000;
      uint8_t brightness;
      if (phase < 200) {
        brightness = (uint8_t)((uint32_t)phase * 255 / 200);
      } else {
        brightness = (uint8_t)((uint32_t)(1000 - phase) * 255 / 800);
      }
      for (int i = 0; i < NUM_LEDS; i++) {
        analogWrite(_pins[i], (i < _batteryLeds) ? brightness : 0);
      }
      #if INT_LED
      uint8_t pct = (_batteryLeds * 100) / 8;
      if (pct > 50) {
        neopixelWrite(RGB_LED_PIN, 0, RGB_LED_BRIGHTNESS, 0);
      } else if (pct > 20) {
        neopixelWrite(RGB_LED_PIN, RGB_LED_BRIGHTNESS, RGB_LED_BRIGHTNESS, 0);
      } else {
        neopixelWrite(RGB_LED_PIN, RGB_LED_BRIGHTNESS, 0, 0);
      }
      #endif
      return;
    }
    // Battery display ended — detach LEDC, restore GPIO for digitalWrite modes
    _showingBattery = false;
    for (int i = 0; i < NUM_LEDS; i++) {
      ledcDetachPin(_pins[i]);
      pinMode(_pins[i], OUTPUT);
      digitalWrite(_pins[i], LOW);
    }
  }

  // === Priority 3: Pot bargraph (solid bar, digitalWrite only) ===
  if (_showingPotBar) {
    if (now - _potBarStart >= POT_BARGRAPH_DURATION_MS) {
      _showingPotBar = false;
    } else {
      for (int i = 0; i < NUM_LEDS; i++) {
        digitalWrite(_pins[i], (i < _potBarLevel) ? HIGH : LOW);
      }
      return;
    }
  }

  // === Priority 4: Calibration mode ===
  if (_calibrationMode) {
    if (_validationFlashing) {
      unsigned long elapsed = now - _validationFlashStart;
      if (elapsed >= 150) {
        _validationFlashing = false;
      } else {
        uint8_t phase = elapsed / 25;
        bool on = (phase < 6) && (phase % 2 == 0);
        for (int i = 0; i < NUM_LEDS; i++) {
          digitalWrite(_pins[i], on ? HIGH : LOW);
        }
        return;
      }
    }
    for (int i = 0; i < NUM_LEDS; i++) {
      digitalWrite(_pins[i], LOW);
    }
    #if INT_LED
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);
    #endif
    return;
  }

  // === Priority 5: Normal bank display ===
  for (int i = 0; i < NUM_LEDS; i++) {
    if (i == _currentBank) {
      if (_batteryLow) {
        unsigned long elapsed = now - _batLowLastBurstTime;
        if (elapsed >= BAT_LOW_BLINK_INTERVAL_MS) {
          _batLowLastBurstTime = now;
          elapsed = 0;
        }
        uint32_t burstDuration = (uint32_t)BAT_LOW_BLINK_SPEED_MS * 6;
        if (elapsed < burstDuration) {
          uint8_t phase = elapsed / BAT_LOW_BLINK_SPEED_MS;
          bool on = (phase % 2 == 0);
          digitalWrite(_pins[i], on ? HIGH : LOW);
        } else {
          digitalWrite(_pins[i], HIGH);
        }
      } else {
        digitalWrite(_pins[i], HIGH);
      }
    } else {
      digitalWrite(_pins[i], LOW);
    }
  }

  #if INT_LED
  neopixelWrite(RGB_LED_PIN, 0, RGB_LED_BRIGHTNESS, 0);
  #endif
}

// =================================================================
// Bank Display
// =================================================================

void LedController::setCurrentBank(uint8_t bank) {
  if (bank < NUM_BANKS) _currentBank = bank;
}

void LedController::setBatteryLow(bool low) {
  _batteryLow = low;
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
// Pot Bargraph
// =================================================================

void LedController::showPotBargraph(uint8_t level) {
  uint8_t newLevel = (level > NUM_LEDS) ? NUM_LEDS : level;

  // Ignore calls with identical level — prevents ADC jitter from keeping bar alive
  if (newLevel == _potBarLevel && _showingPotBar) {
    return;
  }

  _potBarStart = millis();
  _potBarLevel = newLevel;
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
  for (int i = 0; i < NUM_LEDS; i++) {
    digitalWrite(_pins[i], LOW);
  }
  #if INT_LED
  neopixelWrite(RGB_LED_PIN, 0, 0, 0);
  #endif
  _currentBank = 0;
  _batteryLow = false;
  _bootMode = false;
  _bootStep = 0;
  _bootFailStep = 0;
  _chaseActive = false;
  _calibrationMode = false;
  _validationFlashing = false;
  _error = false;
  _showingPotBar = false;
  _showingBattery = false;
}
