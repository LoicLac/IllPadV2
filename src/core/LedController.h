#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "HardwareConfig.h"
#include "KeyboardData.h"
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

  // Brightness (raw pot ADC 0-255, mapped via POT_BRIGHTNESS_CURVE)
  void setBrightness(uint8_t potValue);

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

  // LED settings (from NVS)
  void loadLedSettings(const LedSettingsStore& store);
  void loadColorSlots(const ColorSlotStore& store);
  void rebuildGammaLut(uint8_t gammaTenths);

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

  // Preview API (for Tool 7 — direct LED control in setup mode)
  void previewBegin();   // Enters preview mode, suppresses normal rendering
  void previewEnd();     // Exits preview mode, clears LEDs
  void previewSetPixel(uint8_t led, const RGBW& color, uint8_t intensityPct);
  void previewClear();   // Clears all preview LEDs
  void previewShow();    // Calls _strip.show()

private:
  Adafruit_NeoPixel _strip;

  // Unified pixel setter: takes perceptual intensity (0-100%),
  // combines with master brightness, converts to linear, applies gamma
  void setPixel(uint8_t i, const RGBW& color, uint8_t intensityPct);
  void clearPixels();

  // Render helpers (priority-based, called from update())
  void renderBoot(unsigned long now);
  void renderComet(unsigned long now);
  void renderChase(unsigned long now);
  void renderError(unsigned long now);
  bool renderBattery(unsigned long now);
  bool renderBargraph(unsigned long now);
  bool renderConfirmation(unsigned long now);
  void renderCalibration(unsigned long now);
  void renderNormalDisplay(unsigned long now);

  // Master brightness (0-100 perceptual, from pot via POT_BRIGHTNESS_CURVE)
  uint8_t _brightnessPct;

  // Bank display
  uint8_t _currentBank;
  bool _batteryLow;

  // Multi-bank state
  const BankSlot* _slots;

  // Resolved colors (from ColorSlotStore, resolved at load time)
  RGBW _colNormalFg, _colNormalBg;
  RGBW _colArpFg, _colArpBg;
  RGBW _colTickFlash;
  RGBW _colBankSwitch, _colScaleRoot, _colScaleMode, _colScaleChrom;
  RGBW _colHold, _colPlayAck, _colStop, _colOctave;

  // LED settings (0-100 perceptual %)
  uint8_t  _normalFgIntensity;
  uint8_t  _normalBgIntensity;
  uint8_t  _fgArpStopMin, _fgArpStopMax;
  uint8_t  _fgArpPlayMin, _fgArpPlayMax;
  uint8_t  _bgArpStopMin, _bgArpStopMax;
  uint8_t  _bgArpPlayMin, _bgArpPlayMax;
  uint8_t  _tickFlashFg, _tickFlashBg;
  uint16_t _pulsePeriodMs;
  uint8_t  _tickFlashDurationMs;
  uint8_t  _bankBlinks;
  uint16_t _bankDurationMs;
  uint8_t  _bankBrightnessPct;
  uint8_t  _scaleRootBlinks;
  uint16_t _scaleRootDurationMs;
  uint8_t  _scaleModeBlinks;
  uint16_t _scaleModeDurationMs;
  uint8_t  _scaleChromBlinks;
  uint16_t _scaleChromDurationMs;
  uint8_t  _holdOnFlashMs;
  uint16_t _holdFadeMs;
  uint16_t _stopFadeMs;
  uint8_t  _playBeatCount;
  uint8_t  _octaveBlinks;
  uint16_t _octaveDurationMs;

  uint8_t _sineTable[256];
  uint8_t _gammaLut[256];
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

  // Preview mode (Tool 7)
  bool _previewMode;
};

#endif // LED_CONTROLLER_H
