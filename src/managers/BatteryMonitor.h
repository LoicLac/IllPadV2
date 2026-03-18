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

  uint8_t getPercent() const;
  bool    isLow() const;

private:
  LedController* _leds;
  uint8_t        _percent;
  bool           _low;
  uint32_t       _lastCheckMs;
  bool           _lastBtnState;

  void readBattery();
};

#endif // BATTERY_MONITOR_H
