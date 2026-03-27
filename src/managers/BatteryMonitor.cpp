#include "BatteryMonitor.h"
#include "../core/LedController.h"
#include <Arduino.h>

BatteryMonitor::BatteryMonitor()
  : _leds(nullptr), _smoothedPct(100.0f), _percent(100), _low(false),
    _lastCheckMs(0), _lastBtnState(false), _adcAtFull(0) {}

void BatteryMonitor::begin(LedController* leds) {
  _leds = leds;
  analogReadResolution(12);

  // Seed filter with averaged reading (8 samples) for stable initial value
  float sum = 0.0f;
  for (uint8_t i = 0; i < 8; i++) {
    sum += computePercent();
  }
  _smoothedPct = sum / 8.0f;
  _percent = (uint8_t)constrain((int16_t)_smoothedPct, 0, 100);
  _low = (_percent < BAT_LOW_THRESHOLD_PCT);
  _lastCheckMs = millis();
}

void BatteryMonitor::setAdcAtFull(uint16_t adcVal) {
  _adcAtFull = adcVal;
}

uint16_t BatteryMonitor::readRawAdc() const {
  return (uint16_t)analogRead(BAT_ADC_PIN);
}

void BatteryMonitor::update(bool btnRearPressed) {
  uint32_t now = millis();

  // Periodic IIR update
  if ((now - _lastCheckMs) >= BAT_CHECK_INTERVAL_MS) {
    updateSmoothed();
    _lastCheckMs = now;
  }

  // Rear button press edge → show smoothed gauge (no new read)
  if (btnRearPressed && !_lastBtnState && _leds) {
    _leds->showBatteryGauge(_percent);
  }
  _lastBtnState = btnRearPressed;
}

// Single ADC read → percentage (unclamped float)
float BatteryMonitor::computePercent() const {
  uint16_t raw = (uint16_t)analogRead(BAT_ADC_PIN);

  uint16_t fullAdc = (_adcAtFull > 0) ? _adcAtFull : BAT_ADC_FULL_THEORETICAL;
  uint16_t emptyAdc = (uint16_t)((float)fullAdc * BAT_VOLTAGE_EMPTY / BAT_VOLTAGE_FULL);

  if (fullAdc <= emptyAdc) return 50.0f;  // Bad calibration fallback

  return (float)((int16_t)raw - (int16_t)emptyAdc) * 100.0f / (float)(fullAdc - emptyAdc);
}

// IIR filter update + derive integer percent and low flag
void BatteryMonitor::updateSmoothed() {
  float raw = computePercent();
  _smoothedPct = _smoothedPct * (1.0f - IIR_ALPHA) + raw * IIR_ALPHA;
  _percent = (uint8_t)constrain((int16_t)_smoothedPct, 0, 100);
  _low = (_percent < BAT_LOW_THRESHOLD_PCT);
}

uint8_t BatteryMonitor::getPercent() const {
  return _percent;
}

bool BatteryMonitor::isLow() const {
  return _low;
}
