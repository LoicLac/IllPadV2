#ifndef GROOVE_TEMPLATES_H
#define GROOVE_TEMPLATES_H

#include <stdint.h>

// Shuffle groove templates — shared between ArpEngine and LoopEngine.
// Each template is 16 steps. Values are timing offset percentages (-100..+100).
// Positive = push the step later, negative = pull earlier.
// Applied as: offsetUs = template[step%16] * shuffleDepth * stepDurationUs / 100

static const uint8_t NUM_SHUFFLE_TEMPLATES = 5;
static const uint8_t SHUFFLE_TEMPLATE_LEN = 16;

static const int8_t SHUFFLE_TEMPLATES[NUM_SHUFFLE_TEMPLATES][SHUFFLE_TEMPLATE_LEN] = {
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
};

#endif // GROOVE_TEMPLATES_H
