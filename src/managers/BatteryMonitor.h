#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>
#include "../core/HardwareConfig.h"

class LedController;

class BatteryMonitor {
public:
  BatteryMonitor();

  void begin(LedController* leds);
  void update(bool btnRearPressed);

  // Set calibrated ADC value at full charge (from NVS/Tool 5)
  void setAdcAtFull(uint16_t adcVal);

  // Read current raw ADC (for calibration in Tool 5)
  uint16_t readRawAdc() const;

  uint8_t getPercent() const;
  bool    isLow() const;

private:
  LedController* _leds;
  float          _smoothedPct;  // IIR-filtered percentage
  uint8_t        _percent;      // Integer percentage (derived from _smoothedPct)
  bool           _low;
  uint32_t       _lastCheckMs;
  bool           _lastBtnState;
  uint16_t       _adcAtFull;    // Calibrated raw ADC at 4.2V (0 = use theoretical)

  static constexpr float IIR_ALPHA = 0.15f;

  float   computePercent() const;  // Single ADC read → percent (float)
  void    updateSmoothed();        // IIR update from one ADC read
};

#endif // BATTERY_MONITOR_H
