#ifndef SETUP_POT_INPUT_H
#define SETUP_POT_INPUT_H

#include <stdint.h>

// Lightweight pot input for setup tools.
// Two channels (pot right 1 + pot right 2) with differential catch + re-anchor.
// Call seed() when cursor moves to a new param, update() every ~30ms.

class SetupPotInput {
public:
  static const uint8_t NUM_CHANNELS = 2;

  SetupPotInput();

  // Bind a channel to a target variable. The pot directly modifies *target.
  // ch: 0 = pot right 1, 1 = pot right 2
  // target: pointer to the value (uint8_t, int8_t, uint16_t — cast to int32_t*)
  // On seed: reads ADC, records baseline. Pot must move past initial deadzone to activate.
  void seed(uint8_t ch, int32_t* target, int32_t minVal, int32_t maxVal);

  // Disable a channel
  void disable(uint8_t ch);

  // Read ADCs, apply differential + re-anchor. Returns true if any target changed.
  bool update();

  // Is the pot actively controlling the value?
  bool isActive(uint8_t ch) const;

  // Is the channel enabled?
  bool isEnabled(uint8_t ch) const;

private:
  struct Channel {
    uint8_t  pin;
    int32_t* target;        // pointer to the value being controlled
    int32_t  minVal, maxVal;
    int32_t  lastRaw;       // last raw ADC (no smoothing — direct, responsive)
    int32_t  baseline;      // raw ADC at seed time
    int32_t  accumDelta;    // accumulated fractional ADC
    bool     enabled;
    bool     active;        // past initial deadzone
    bool     anchored;      // absolute mode
  };

  Channel _ch[NUM_CHANNELS];

  int32_t adcToValue(uint8_t ch, int32_t adc) const;

  static const int32_t MOVE_THRESHOLD = 30;   // raw ADC units to activate (~0.7% of range)
  static const int32_t JITTER_DZ      = 6;    // raw ADC jitter filter
  static const int32_t ANCHOR_WINDOW  = 60;   // raw ADC snap to absolute
  static const int32_t ADC_MAX        = 4095;
};

#endif // SETUP_POT_INPUT_H
