#ifndef GROOVE_TEMPLATES_H
#define GROOVE_TEMPLATES_H

#include <stdint.h>

// Shuffle groove templates — shared between ArpEngine and LoopEngine.
// Each template is 16 steps. Values are timing offset percentages (-100..+100).
// Positive = push the step later, negative = pull earlier.
// Applied as: offsetUs = template[step%16] * shuffleDepth * stepDurationUs / 100

static const uint8_t NUM_SHUFFLE_TEMPLATES = 10;
static const uint8_t SHUFFLE_TEMPLATE_LEN = 16;

static const int8_t SHUFFLE_TEMPLATES[NUM_SHUFFLE_TEMPLATES][SHUFFLE_TEMPLATE_LEN] = {
  // --- Positive-only (classic) ---
  // 0: Classic swing — every other step pushed back
  {0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50},
  // 1: Light/heavy alternating — subtler groove
  {0, 33, 0, 66, 0, 33, 0, 66, 0, 33, 0, 66, 0, 33, 0, 66},
  // 2: Backbeat push — every 3rd step delayed
  {0, 0, 50, 0, 0, 0, 50, 0, 0, 0, 50, 0, 0, 0, 50, 0},
  // 3: Ramp — progressive delay within each group of 4
  {0, 25, 50, 75, 0, 25, 50, 75, 0, 25, 50, 75, 0, 25, 50, 75},
  // 4: Triplet feel — 2/3 + 1/3 grouping
  {0, 66, 33, 66, 0, 66, 33, 66, 0, 66, 33, 66, 0, 66, 33, 66},
  // --- Bipolar (notes rush AND drag) ---
  // 5: Push-pull — alternating early/late, jittery nervous energy
  {-25, 25,-25, 25,-25, 25,-25, 25,-25, 25,-25, 25,-25, 25,-25, 25},
  // 6: Jazz anticipation — off-beats arrive early, downbeats on grid
  {0,-20,-30,-15, 0,-20,-30,-15, 0,-20,-30,-15, 0,-20,-30,-15},
  // 7: Drunken — asymmetric, stumbling groove
  {-15, 40,-30, 60,-10, 50,-40, 20,-15, 40,-30, 60,-10, 50,-40, 20},
  // 8: Tension build — progressive anticipation within each 4-step group
  {0, 0,-10,-20, 0, 0,-20,-40, 0, 0,-30,-60, 0, 0,-10,-20},
  // 9: Human micro — subtle ±5-15%, simulates micro-timing imperfections
  {-5, 8,-3, 12,-7, 4,-2, 15,-8, 6,-4, 10,-6, 3,-1, 11},
};

#endif // GROOVE_TEMPLATES_H
