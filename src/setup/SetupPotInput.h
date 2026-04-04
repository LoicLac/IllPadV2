#ifndef SETUP_POT_INPUT_H
#define SETUP_POT_INPUT_H

#include <stdint.h>

// Lightweight pot input for setup tools.
// Two channels (pot right 1 + pot right 2) with dual-mode: RELATIVE or ABSOLUTE.
// Reads PotFilter::getStable() — the tool must call PotFilter::updateAll() BEFORE update().
// Call seed() when cursor moves to a new param, update() every loop iteration.

enum PotMode : uint8_t { POT_RELATIVE, POT_ABSOLUTE };

class SetupPotInput {
public:
  static const uint8_t NUM_CHANNELS = 2;

  SetupPotInput();

  // Bind channel to target. Channel 0 = pot right 1, 1 = pot right 2.
  // mode: RELATIVE (delta/Bresenham) or ABSOLUTE (differential + re-anchor).
  // stepsHint: steps for a full pot turn (RELATIVE only, 0 = auto = maxVal-minVal+1).
  void seed(uint8_t ch, int32_t* target, int32_t minVal, int32_t maxVal,
            PotMode mode = POT_ABSOLUTE, uint16_t stepsHint = 0);

  // Disable a channel
  void disable(uint8_t ch);

  // Read PotFilter::getStable(), apply logic. Returns true if any target changed.
  bool update();

  // Per-channel "moved this cycle" — read-and-clear.
  bool getMove(uint8_t ch);

  // Has the pot been physically moved since last seed?
  bool isActive(uint8_t ch) const;

  // Is the channel enabled?
  bool isEnabled(uint8_t ch) const;

private:
  struct Channel {
    int32_t* target;
    int32_t  minVal, maxVal;
    int32_t  lastRaw;        // Last PotFilter reading (differential baseline)
    int32_t  baseline;       // Reference point (seed or running)
    int32_t  accumDelta;     // Fractional accumulator (Bresenham, RELATIVE)
    uint16_t stepThreshold;  // ADC units per step (RELATIVE only)
    PotMode  mode;
    bool     enabled;
    bool     active;         // Past MOVE_THRESHOLD since seed
    bool     anchored;       // Re-anchor reached (ABSOLUTE only, never true in RELATIVE)
    bool     moved;          // Target modified this cycle (cleared by getMove or next update)
  };

  Channel _ch[NUM_CHANNELS];

  int32_t adcToValue(uint8_t ch, int32_t adc) const;

  static const int32_t MOVE_THRESHOLD = 22;   // ~1 deadband step + margin (PotFilter deadband=20)
  static const int32_t ANCHOR_WINDOW  = 60;   // ADC units, re-anchor check (ABSOLUTE only)
  static const int32_t ADC_MAX        = 4095;
};

#endif // SETUP_POT_INPUT_H
