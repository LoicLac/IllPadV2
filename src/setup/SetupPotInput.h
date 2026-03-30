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

  // Configure a channel for a parameter range. Resets catch state.
  // ch: 0 = pot right 1, 1 = pot right 2
  void seed(uint8_t ch, int32_t currentValue, int32_t minVal, int32_t maxVal);

  // Disable a channel (param not pot-controllable)
  void disable(uint8_t ch);

  // Read ADCs, apply smoothing, compute deltas. Returns true if any value changed.
  bool update();

  // Current value for channel (clamped to seeded range)
  int32_t getValue(uint8_t ch) const;

  // Is the pot actively controlling the value? (past initial deadzone)
  bool isActive(uint8_t ch) const;

  // Is the pot in absolute (anchored) mode?
  bool isAnchored(uint8_t ch) const;

  // Is the channel enabled?
  bool isEnabled(uint8_t ch) const;

private:
  struct Channel {
    uint8_t  pin;
    int32_t  value;
    int32_t  minVal, maxVal;
    int32_t  smoothed;      // EMA-filtered ADC × 256 (fixed-point, 8 fractional bits)
    int32_t  baseline;      // smoothed ADC at seed time (for differential)
    bool     enabled;
    bool     active;        // past initial deadzone
    bool     anchored;      // absolute mode (pot position = value)
  };

  Channel _ch[NUM_CHANNELS];

  // Map ADC value (0-4095) to param range
  int32_t adcToValue(uint8_t ch, int32_t adc) const;
  // Map param value to ADC range (for anchor detection)
  int32_t valueToAdc(uint8_t ch, int32_t val) const;

  static const int32_t EMA_SHIFT     = 8;     // fixed-point fractional bits
  static const int32_t EMA_ALPHA     = 20;    // ~0.08 in fixed-point (20/256)
  static const int32_t DEADZONE      = 8;     // ADC units, jitter filter
  static const int32_t INITIAL_DZ    = 100;   // ADC units, first activation threshold
  static const int32_t ANCHOR_WINDOW = 60;    // ADC units, snap to absolute mode
  static const int32_t ADC_MAX       = 4095;
};

#endif // SETUP_POT_INPUT_H
