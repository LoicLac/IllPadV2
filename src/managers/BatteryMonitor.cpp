#include "BatteryMonitor.h"
#include "../core/LedController.h"

BatteryMonitor::BatteryMonitor()
  : _leds(nullptr), _percent(100), _low(false),
    _lastCheckMs(0), _lastBtnState(false) {}

void BatteryMonitor::begin(LedController* leds) {
  _leds = leds;
}

void BatteryMonitor::update(bool btnRearPressed) {
  (void)btnRearPressed;
  // TODO: periodic battery check, button press shows gauge on LEDs
}

uint8_t BatteryMonitor::getPercent() const {
  return _percent;
}

bool BatteryMonitor::isLow() const {
  return _low;
}

void BatteryMonitor::readBattery() {
  // TODO: read BAT_ADC_PIN, compute percent
}
