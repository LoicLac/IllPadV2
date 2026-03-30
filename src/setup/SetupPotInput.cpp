#include "SetupPotInput.h"
#include "../core/HardwareConfig.h"
#include <Arduino.h>

// GPIO pins for the two channels
static const uint8_t POT_PINS[2] = { POT_RIGHT1_PIN, POT_RIGHT2_PIN };

SetupPotInput::SetupPotInput() {
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    _ch[i].pin      = POT_PINS[i];
    _ch[i].value    = 0;
    _ch[i].minVal   = 0;
    _ch[i].maxVal   = 100;
    _ch[i].smoothed = 0;
    _ch[i].baseline = 0;
    _ch[i].enabled  = false;
    _ch[i].active   = false;
    _ch[i].anchored = false;
  }
}

void SetupPotInput::seed(uint8_t ch, int32_t currentValue, int32_t minVal, int32_t maxVal) {
  if (ch >= NUM_CHANNELS) return;
  Channel& c = _ch[ch];
  c.value    = currentValue;
  c.minVal   = minVal;
  c.maxVal   = maxVal;
  c.enabled    = true;
  c.active     = false;
  c.anchored   = false;
  c.accumDelta = 0;
  // Read current ADC and seed smoothed + baseline
  int32_t raw = analogRead(c.pin);
  c.smoothed = raw << EMA_SHIFT;
  c.baseline = raw;
}

void SetupPotInput::disable(uint8_t ch) {
  if (ch >= NUM_CHANNELS) return;
  _ch[ch].enabled  = false;
  _ch[ch].active   = false;
  _ch[ch].anchored = false;
}

bool SetupPotInput::update() {
  bool changed = false;

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    Channel& c = _ch[i];
    if (!c.enabled) continue;

    // 1. Read ADC and apply EMA smoothing (fixed-point)
    int32_t raw = analogRead(c.pin);
    c.smoothed += (int32_t)EMA_ALPHA * ((raw << EMA_SHIFT) - c.smoothed) >> EMA_SHIFT;
    int32_t adc = c.smoothed >> EMA_SHIFT;  // back to integer ADC units

    if (!c.active) {
      // Phase 1-2: waiting for initial movement past INITIAL_DZ
      int32_t diff = adc - c.baseline;
      if (diff < 0) diff = -diff;
      if (diff >= INITIAL_DZ) {
        c.active = true;
        c.baseline = adc;  // re-anchor baseline to current position
        // Don't change value yet — first movement just activates
      }
    } else if (!c.anchored) {
      // Phase 2: Differential mode — accumulate ADC delta, convert when enough
      int32_t adcDelta = adc - c.baseline;
      // Apply deadzone: ignore jitter
      if (adcDelta > -DEADZONE && adcDelta < DEADZONE) continue;

      // Accumulate delta (avoids integer truncation for small ranges)
      c.accumDelta += adcDelta;
      c.baseline = adc;  // consume this movement

      // Convert accumulated ADC delta to value delta
      int32_t range = c.maxVal - c.minVal;
      int32_t valueDelta = (int32_t)((int64_t)c.accumDelta * range / ADC_MAX);

      if (valueDelta != 0) {
        int32_t newVal = c.value + valueDelta;
        if (newVal < c.minVal) newVal = c.minVal;
        if (newVal > c.maxVal) newVal = c.maxVal;

        if (newVal != c.value) {
          c.value = newVal;
          changed = true;
        }
        // Consume the portion of accumDelta that was used
        c.accumDelta -= (int32_t)((int64_t)valueDelta * ADC_MAX / range);
      }

      // Check for re-anchor: does pot position match current value?
      int32_t mappedAdc = adcToValue(i, adc);
      int32_t mappedDiff = mappedAdc - c.value;
      if (mappedDiff < 0) mappedDiff = -mappedDiff;
      int32_t anchorThreshold = range * ANCHOR_WINDOW / ADC_MAX;
      if (anchorThreshold < 1) anchorThreshold = 1;
      if (mappedDiff <= anchorThreshold) {
        c.anchored = true;
        c.accumDelta = 0;
      }
    } else {
      // Phase 3: Absolute mode — pot position = value directly
      int32_t newVal = adcToValue(i, adc);

      if (newVal != c.value) {
        c.value = newVal;
        changed = true;
      }
    }
  }

  return changed;
}

int32_t SetupPotInput::getValue(uint8_t ch) const {
  if (ch >= NUM_CHANNELS) return 0;
  return _ch[ch].value;
}

bool SetupPotInput::isActive(uint8_t ch) const {
  if (ch >= NUM_CHANNELS) return false;
  return _ch[ch].active;
}

bool SetupPotInput::isAnchored(uint8_t ch) const {
  if (ch >= NUM_CHANNELS) return false;
  return _ch[ch].anchored;
}

bool SetupPotInput::isEnabled(uint8_t ch) const {
  if (ch >= NUM_CHANNELS) return false;
  return _ch[ch].enabled;
}

int32_t SetupPotInput::adcToValue(uint8_t ch, int32_t adc) const {
  const Channel& c = _ch[ch];
  // Map 0-4095 → minVal..maxVal
  int32_t val = c.minVal + (int32_t)((int64_t)(adc) * (c.maxVal - c.minVal) / ADC_MAX);
  if (val < c.minVal) val = c.minVal;
  if (val > c.maxVal) val = c.maxVal;
  return val;
}

int32_t SetupPotInput::valueToAdc(uint8_t ch, int32_t val) const {
  const Channel& c = _ch[ch];
  if (c.maxVal == c.minVal) return 0;
  return (int32_t)((int64_t)(val - c.minVal) * ADC_MAX / (c.maxVal - c.minVal));
}
