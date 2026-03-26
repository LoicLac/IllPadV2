#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "HardwareConfig.h"
#include <Adafruit_NeoPixel.h>
#include <stdint.h>

// Forward declarations
struct BankSlot;
class ArpEngine;
class ClockManager;

// =================================================================
// Confirmation blink types
// =================================================================
enum ConfirmType : uint8_t {
  CONFIRM_NONE         = 0,
  CONFIRM_BANK_SWITCH  = 1,
  CONFIRM_SCALE_ROOT   = 2,
  CONFIRM_SCALE_MODE   = 3,
  CONFIRM_SCALE_CHROM  = 4,
  CONFIRM_HOLD_ON      = 5,
  CONFIRM_HOLD_OFF     = 6,
  CONFIRM_PLAY         = 7,
  CONFIRM_STOP         = 8,
  CONFIRM_OCTAVE       = 9,
};

class LedController {
public:
  LedController();
  void begin();
  void update();

  // Brightness (0-255)
  void setBrightness(uint8_t brightness);

  // Bank display
  void setCurrentBank(uint8_t bank);
  void setBatteryLow(bool low);

  // Multi-bank state
  void setBankSlots(const BankSlot* slots);

  // Clock manager (for play confirmation beat sync)
  void setClockManager(const ClockManager* clock);

  // Confirmations
  void triggerConfirm(ConfirmType type, uint8_t param = 0);

  // Bargraph persistence
  void setPotBarDuration(uint16_t ms);

  // Boot
  void showBootProgress(uint8_t step);
  void showBootFailure(uint8_t step);
  void endBoot();

  // I2C error halt
  void haltI2CError();

  // Chase (calibration entry)
  void startChase();
  void stopChase();

  // Setup comet (active during Tools 1-6)
  void startSetupComet();
  void stopSetupComet();

  // Error
  void setError(bool error);

  // Battery gauge
  void showBatteryGauge(uint8_t percent);

  // Pot bargraph with catch visualization
  void showPotBargraph(uint8_t realLevel, uint8_t potLevel, bool caught);

  // Calibration
  void setCalibrationMode(bool active);
  void playValidation();

  // All off
  void allOff();

private:
  Adafruit_NeoPixel _strip;

  // Helper: set pixel from RGB struct (applies _brightness scaling)
  void setPixel(uint8_t i, const RGB& color);
  void setPixelScaled(uint8_t i, const RGB& color, uint8_t scale);
  void clearPixels();

  // Brightness
  uint8_t _brightness;

  // Bank display
  uint8_t _currentBank;
  bool _batteryLow;

  // Multi-bank state
  const BankSlot* _slots;
  uint8_t _sineTable[64];
  unsigned long _flashStartTime[NUM_LEDS];

  // Clock manager (for play beat detection)
  const ClockManager* _clock;

  // Confirmation state
  ConfirmType   _confirmType;
  unsigned long _confirmStart;
  uint8_t       _confirmParam;

  // Play confirmation state
  unsigned long _fadeStartTime;    // Flash hold timer for beat-synced play flashes
  uint8_t       _playFlashPhase;    // 0=ack done, 1-3=beat flashes
  uint32_t      _playLastBeatTick;  // Clock tick at last beat flash

  // Bargraph
  uint16_t _potBarDurationMs;
  bool _showingPotBar;
  uint8_t _potBarRealLevel;
  uint8_t _potBarPotLevel;
  bool    _potBarCaught;
  unsigned long _potBarStart;

  // Boot
  bool _bootMode;
  uint8_t _bootStep;
  uint8_t _bootFailStep;

  // Chase (calibration entry)
  bool _chaseActive;
  uint8_t _chasePos;
  unsigned long _chaseLastStep;

  // Setup comet
  bool _setupComet;
  uint8_t _cometPos;          // 0-13 (ping-pong: 0-7 forward, 8-13 = 6 down to 1)
  unsigned long _cometLastStep;

  // Calibration
  bool _calibrationMode;
  bool _validationFlashing;
  unsigned long _validationFlashStart;

  // Error
  bool _error;

  // Blink timer
  unsigned long _lastBlinkTime;
  bool _blinkState;

  // Battery gauge
  bool _showingBattery;
  uint8_t _batteryLeds;
  unsigned long _batteryDisplayStart;

  // Battery low blink
  unsigned long _batLowLastBurstTime;
};

#endif // LED_CONTROLLER_H
