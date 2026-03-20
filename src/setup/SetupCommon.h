#ifndef SETUP_COMMON_H
#define SETUP_COMMON_H

#include "../core/CapacitiveKeyboard.h"
#include "../core/HardwareConfig.h"
#include <Arduino.h>

// =================================================================
// Touch Detection Constants
// =================================================================
static const uint16_t TOUCH_DETECT_THRESHOLD = 50;

// =================================================================
// Calibration Stats
// =================================================================
struct CalStats {
  int count;
  uint16_t minVal, maxVal, avgVal;
  int warnings;
};

static inline CalStats computeStats(uint16_t deltas[], bool done[]) {
  CalStats s = {0, 0xFFFF, 0, 0, 0};
  uint32_t sum = 0;
  for (int i = 0; i < NUM_KEYS; i++) {
    if (!done[i]) continue;
    s.count++;
    uint16_t d = deltas[i];
    if (d < s.minVal) s.minVal = d;
    if (d > s.maxVal) s.maxVal = d;
    sum += d;
    if (d < CAL_PRESSURE_MIN_DELTA_TO_VALIDATE) s.warnings++;
  }
  s.avgVal = s.count > 0 ? (uint16_t)(sum / s.count) : 0;
  if (s.count == 0) s.minVal = 0;
  return s;
}

// =================================================================
// Detect which pad is being touched (highest delta wins)
// Returns: key index (0-47), -1 = no touch, -2 = ambiguous
// =================================================================
static inline int detectActiveKey(CapacitiveKeyboard& kb, uint16_t refBaselines[]) {
  uint16_t maxDelta = 0, secondDelta = 0;
  int bestKey = -1;

  for (int i = 0; i < NUM_KEYS; i++) {
    uint16_t f = kb.getFilteredData(i);
    uint16_t delta = (refBaselines[i] > f) ? (refBaselines[i] - f) : 0;
    if (delta > maxDelta) {
      secondDelta = maxDelta;
      maxDelta = delta;
      bestKey = i;
    } else if (delta > secondDelta) {
      secondDelta = delta;
    }
  }

  if (maxDelta < TOUCH_DETECT_THRESHOLD) return -1;

  // Ambiguity: two strong touches within 80% of each other
  if (secondDelta > TOUCH_DETECT_THRESHOLD &&
      secondDelta * 5 > maxDelta * 4) {
    return -2;
  }
  return bestKey;
}

// =================================================================
// Quick baseline snapshot for touch-only tools
// No autoconfig or visual stabilization — just grab current baselines
// =================================================================
static inline void captureBaselines(CapacitiveKeyboard& kb, uint16_t referenceBaselines[]) {
  kb.pollAllSensorData();
  delay(50);
  kb.pollAllSensorData();
  kb.getBaselineData(referenceBaselines);
}

#endif // SETUP_COMMON_H
