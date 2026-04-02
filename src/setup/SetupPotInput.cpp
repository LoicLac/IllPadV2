#include "SetupPotInput.h"
#include "../core/HardwareConfig.h"
#include <Arduino.h>

static const uint8_t POT_PINS[2] = { POT_RIGHT1_PIN, POT_RIGHT2_PIN };

SetupPotInput::SetupPotInput() {
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    _ch[i].pin        = POT_PINS[i];
    _ch[i].target     = nullptr;
    _ch[i].minVal     = 0;
    _ch[i].maxVal     = 100;
    _ch[i].lastRaw    = 0;
    _ch[i].baseline   = 0;
    _ch[i].accumDelta = 0;
    _ch[i].enabled    = false;
    _ch[i].active     = false;
    _ch[i].anchored   = false;
  }
}

void SetupPotInput::seed(uint8_t ch, int32_t* target, int32_t minVal, int32_t maxVal) {
  if (ch >= NUM_CHANNELS || !target) return;
  Channel& c = _ch[ch];
  c.target     = target;
  c.minVal     = minVal;
  c.maxVal     = maxVal;
  c.enabled    = true;
  c.active     = false;
  c.anchored   = false;
  c.accumDelta = 0;
  c.lastRaw    = analogRead(c.pin);
  c.baseline   = c.lastRaw;
}

void SetupPotInput::disable(uint8_t ch) {
  if (ch >= NUM_CHANNELS) return;
  _ch[ch].enabled  = false;
  _ch[ch].active   = false;
  _ch[ch].anchored = false;
  _ch[ch].target   = nullptr;
}

bool SetupPotInput::update() {
  bool changed = false;

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    Channel& c = _ch[i];
    if (!c.enabled || !c.target) continue;

    int32_t raw = analogRead(c.pin);

    // Jitter filter: ignore tiny movements
    int32_t rawDelta = raw - c.lastRaw;
    if (rawDelta > -JITTER_DZ && rawDelta < JITTER_DZ) continue;
    c.lastRaw = raw;

    if (!c.active) {
      // Wait for intentional movement from seed position
      int32_t diff = raw - c.baseline;
      if (diff < 0) diff = -diff;
      if (diff >= MOVE_THRESHOLD) {
        c.active = true;
        c.baseline = raw;
      }
      continue;
    }

    if (!c.anchored) {
      // Differential mode: accumulate ADC movement, map to value delta
      int32_t adcDelta = raw - c.baseline;
      c.baseline = raw;
      c.accumDelta += adcDelta;

      int32_t range = c.maxVal - c.minVal;
      int32_t valueDelta = (int32_t)((int64_t)c.accumDelta * range / ADC_MAX);

      if (valueDelta != 0) {
        int32_t newVal = *c.target + valueDelta;
        if (newVal < c.minVal) newVal = c.minVal;
        if (newVal > c.maxVal) newVal = c.maxVal;

        if (newVal != *c.target) {
          *c.target = newVal;
          changed = true;
        }
        // Consume used portion of accumulator
        c.accumDelta -= (int32_t)((int64_t)valueDelta * ADC_MAX / range);
      }

      // Re-anchor check: does pot position map to current value?
      int32_t mapped = adcToValue(i, raw);
      int32_t diff = mapped - *c.target;
      if (diff < 0) diff = -diff;
      int32_t threshold = range * ANCHOR_WINDOW / ADC_MAX;
      if (threshold < 1) threshold = 1;
      if (diff <= threshold) {
        c.anchored = true;
        c.accumDelta = 0;
      }
    } else {
      // Absolute mode: pot position = value
      int32_t newVal = adcToValue(i, raw);
      if (newVal != *c.target) {
        *c.target = newVal;
        changed = true;
      }
    }
  }

  return changed;
}

bool SetupPotInput::isActive(uint8_t ch) const {
  if (ch >= NUM_CHANNELS) return false;
  return _ch[ch].active;
}

bool SetupPotInput::isEnabled(uint8_t ch) const {
  if (ch >= NUM_CHANNELS) return false;
  return _ch[ch].enabled;
}

int32_t SetupPotInput::adcToValue(uint8_t ch, int32_t adc) const {
  const Channel& c = _ch[ch];
  // Bresenham-style: evenly distribute discrete values across ADC range
  int32_t range = c.maxVal - c.minVal;
  int32_t val = c.minVal + (int32_t)((int64_t)(adc + 1) * range / (ADC_MAX + 1));
  if (val < c.minVal) val = c.minVal;
  if (val > c.maxVal) val = c.maxVal;
  return val;
}
