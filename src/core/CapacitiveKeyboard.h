#ifndef CAPACITIVE_KEYBOARD_H
#define CAPACITIVE_KEYBOARD_H

#include "KeyboardData.h"
#include "HardwareConfig.h"
#include <stdint.h>
#include <Wire.h>

class CapacitiveKeyboard {
public:
  CapacitiveKeyboard();

  // Main API
  bool begin();
  void update();

  // Note state getters
  bool isPressed(uint8_t note);
  bool noteOn(uint8_t note);
  bool noteOff(uint8_t note);
  uint8_t getPressure(uint8_t note);  // Returns 0-127 (MIDI range)
  const bool* getPressedKeysState() const;

  // Runtime controls
  void setResponseShape(float shape);
  void setAftertouchDeadzone(int offset);
  void setBaselineProfile(uint8_t profile);
  uint8_t getBaselineProfile() const;
  const char* getBaselineProfileName() const;
  void setPadSensitivity(uint8_t percent);
  uint8_t getPadSensitivity() const;
  void setSlewRate(uint16_t limit);
  uint16_t getSlewRate() const;
  void setAutoReconfigEnabled(bool enabled);

  // Calibration API (used by KeyboardCalibrator)
  bool runAutoconfiguration(uint16_t targetBaseline);
  void pollAllSensorData();
  void saveCalibrationData();
  void loadCalibrationData();
  void calculateAdaptiveThresholds();
  void logFullBaselineTable();
  void getBaselineData(uint16_t* destArray);
  uint16_t getFilteredData(int key);
  void setCalibrationMaxDelta(int key, uint16_t delta);
  uint16_t getTargetBaseline() const;

private:
  void writeRegister(uint8_t addr, uint8_t reg, uint8_t value);
  uint8_t readRegister(uint8_t addr, uint8_t reg);

  uint16_t filteredData[NUM_KEYS];
  uint16_t baselineData[NUM_KEYS];
  float    smoothedPressure[NUM_KEYS];
  bool     keyIsPressed[NUM_KEYS];
  bool     lastKeyIsPressed[NUM_KEYS];
  bool     isInitialized;

  uint16_t currentTargetBaseline;
  uint16_t calibrationMaxDelta[NUM_KEYS];
  uint16_t pressThresholds[NUM_KEYS];
  uint16_t releaseThresholds[NUM_KEYS];

  float responseShape;
  int aftertouchDeadzoneOffset;

  // Pressure pipeline state
  uint16_t pressDeltaStart[NUM_KEYS];
  float slewedPressure[NUM_KEYS];
  float pressureHistory[NUM_KEYS][AFTERTOUCH_SMOOTHING_WINDOW_SIZE];
  int   historyIndex[NUM_KEYS];

  // I2C error tracking (per sensor)
  static const uint8_t I2C_MAX_CONSECUTIVE_FAILURES = 3;
  uint8_t _i2cFailCount[NUM_SENSORS];
  bool    _sensorFailed[NUM_SENSORS];

  // Baseline filter profile
  uint8_t _baselineProfile;

  // Runtime-tunable settings (loaded from NVS or defaults)
  float    _pressThresholdPct;
  float    _releaseThresholdPct;
  uint16_t _slewRateLimit;
};

#endif // CAPACITIVE_KEYBOARD_H
