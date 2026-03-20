#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "HardwareConfig.h"
#include <stdint.h>

class LedController {
public:
  LedController();
  void begin();
  void update();  // Call from loop() — handles all LED state and timing

  // Brightness (0-255, affects all LED outputs via analogWrite)
  void setBrightness(uint8_t brightness);

  // Bank display (runtime)
  void setCurrentBank(uint8_t bank);    // 0-7, lights the corresponding LED
  void setBatteryLow(bool low);         // When true, bank LED does 3 rapid blinks every BAT_LOW_BLINK_INTERVAL_MS

  // Boot sequence (normal boot only — not used in cal path)
  void showBootProgress(uint8_t step);  // Light LEDs 1 through step (progressive fill)
  void showBootFailure(uint8_t step);   // LEDs 1..step-1 solid, LED step blinks rapidly
  void endBoot();                       // Exit boot mode, switch to normal display

  // Boot: I2C error (loops forever internally — never returns)
  void haltI2CError();

  // Chase pattern (calibration entry: "release button")
  void startChase();
  void stopChase();

  // Error (runtime: sensing task stall)
  void setError(bool error);            // All 8 LEDs blink 500ms unison

  // Battery gauge: show battery level on all 8 LEDs for BAT_DISPLAY_DURATION_MS
  void showBatteryGauge(uint8_t percent);

  // Pot bargraph: solid bar at given level (0-8 LEDs). Caller decides what level means.
  void showPotBargraph(uint8_t level);

  // Calibration feedback
  void setCalibrationMode(bool active);  // All LEDs off during calibration
  void playValidation();                 // 3x rapid blink = acknowledge (calibration only)

  // All off
  void allOff();

private:
  static const uint8_t _pins[NUM_LEDS];

  // Brightness (0-255)
  uint8_t _brightness;

  // Bank display
  uint8_t _currentBank;    // 0-7
  bool _batteryLow;

  // Boot sequence
  bool _bootMode;
  uint8_t _bootStep;       // 0-8: progressive fill level
  uint8_t _bootFailStep;   // 0 = no failure; 1-8 = step that failed

  // Chase pattern
  bool _chaseActive;
  uint8_t _chasePos;              // Current lit LED (0-7)
  unsigned long _chaseLastStep;   // Timestamp of last step

  // Calibration
  bool _calibrationMode;

  // Calibration validation flash
  bool _validationFlashing;
  unsigned long _validationFlashStart;

  // Error
  bool _error;

  // Blink timer (shared by error blink)
  unsigned long _lastBlinkTime;
  bool _blinkState;

  // Battery gauge
  bool _showingBattery;
  uint8_t _batteryLeds;           // Number of LEDs to light (0-8)
  unsigned long _batteryDisplayStart;

  // Pot bargraph state
  bool _showingPotBar;
  uint8_t _potBarLevel;          // 0-8 LEDs
  unsigned long _potBarStart;    // Timestamp for timeout

  // Battery low blink burst timing
  unsigned long _batLowLastBurstTime;
};

#endif // LED_CONTROLLER_H
