#include "SetupPotInput.h"
#include "../core/PotFilter.h"
#include "../core/HardwareConfig.h"

SetupPotInput::SetupPotInput() {
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    _ch[i].target        = nullptr;
    _ch[i].minVal        = 0;
    _ch[i].maxVal        = 100;
    _ch[i].lastRaw       = 0;
    _ch[i].baseline      = 0;
    _ch[i].accumDelta    = 0;
    _ch[i].stepThreshold = 1;
    _ch[i].mode          = POT_ABSOLUTE;
    _ch[i].enabled       = false;
    _ch[i].active        = false;
    _ch[i].anchored      = false;
    _ch[i].moved         = false;
  }
}

void SetupPotInput::seed(uint8_t ch, int32_t* target, int32_t minVal, int32_t maxVal,
                         PotMode mode, uint16_t stepsHint) {
  if (ch >= NUM_CHANNELS || !target) return;
  Channel& c   = _ch[ch];
  c.target      = target;
  c.minVal      = minVal;
  c.maxVal      = maxVal;
  c.mode        = mode;
  c.enabled     = true;
  c.active      = false;
  c.anchored    = false;
  c.accumDelta  = 0;
  c.moved       = false;

  int32_t raw   = PotFilter::getStable(ch);
  c.lastRaw     = raw;
  c.baseline    = raw;

  // stepThreshold for RELATIVE mode
  if (mode == POT_RELATIVE) {
    uint16_t steps = (stepsHint > 0) ? stepsHint : (uint16_t)(maxVal - minVal + 1);
    c.stepThreshold = (steps > 0) ? ((ADC_MAX + 1) / steps) : 1;
    if (c.stepThreshold < 1) c.stepThreshold = 1;
  }
}

void SetupPotInput::disable(uint8_t ch) {
  if (ch >= NUM_CHANNELS) return;
  _ch[ch].enabled  = false;
  _ch[ch].active   = false;
  _ch[ch].anchored = false;
  _ch[ch].moved    = false;
  _ch[ch].target   = nullptr;
}

bool SetupPotInput::update() {
  bool anyMoved = false;

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    Channel& c = _ch[i];
    c.moved = false;
    if (!c.enabled || !c.target) continue;

    int32_t raw = PotFilter::getStable(i);

    // Activation: wait for intentional movement from seed position
    if (!c.active) {
      int32_t diff = raw - c.baseline;
      if (diff < 0) diff = -diff;
      if (diff >= MOVE_THRESHOLD) {
        c.active = true;
        c.baseline = raw;
      }
      continue;
    }

    if (c.mode == POT_RELATIVE) {
      // --- RELATIVE: pure delta, no re-anchor ever ---
      int32_t adcDelta = raw - c.baseline;
      c.baseline = raw;                       // baseline always advances
      c.accumDelta += adcDelta;

      // Convert accumulated delta to steps (Bresenham)
      while (c.accumDelta >= (int32_t)c.stepThreshold) {
        if (*c.target < c.maxVal) {
          *c.target += 1;
          c.moved = true;
        }
        c.accumDelta -= (int32_t)c.stepThreshold;
      }
      while (c.accumDelta <= -(int32_t)c.stepThreshold) {
        if (*c.target > c.minVal) {
          *c.target -= 1;
          c.moved = true;
        }
        c.accumDelta += (int32_t)c.stepThreshold;
      }

    } else {
      // --- ABSOLUTE: differential + re-anchor ---
      if (!c.anchored) {
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
            c.moved = true;
          }
          c.accumDelta -= (int32_t)((int64_t)valueDelta * ADC_MAX / range);
        }

        // Re-anchor check: does physical pot map to current value?
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
        // Anchored: pot position = value
        int32_t newVal = adcToValue(i, raw);
        if (newVal != *c.target) {
          *c.target = newVal;
          c.moved = true;
        }
      }
    }

    if (c.moved) anyMoved = true;
  }

  return anyMoved;
}

bool SetupPotInput::getMove(uint8_t ch) {
  if (ch >= NUM_CHANNELS) return false;
  bool m = _ch[ch].moved;
  _ch[ch].moved = false;
  return m;
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
  int32_t range = c.maxVal - c.minVal;
  int32_t val = c.minVal + (int32_t)((int64_t)(adc + 1) * range / (ADC_MAX + 1));
  if (val < c.minVal) val = c.minVal;
  if (val > c.maxVal) val = c.maxVal;
  return val;
}
