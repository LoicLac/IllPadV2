#ifndef GROOVE_TEMPLATES_H
#define GROOVE_TEMPLATES_H

#include <stdint.h>

// Shuffle groove templates — shared between ArpEngine and LoopEngine.
// Each template is 16 steps. Values are timing offset percentages 0..+100.
// Positive = push the step later. Negative values are not used (the engine
// can only fire-on-tick, so negative offsets would only shorten gate, not
// anticipate noteOn — see ArpEngine::executeStepNote).
// Applied as: offsetUs = template[step%16] * shuffleDepth * stepDurationUs / 100

static const uint8_t NUM_SHUFFLE_TEMPLATES = 8;
static const uint8_t SHUFFLE_TEMPLATE_LEN = 16;

static const int8_t SHUFFLE_TEMPLATES[NUM_SHUFFLE_TEMPLATES][SHUFFLE_TEMPLATE_LEN] = {
  // 0: Humanizer — pseudo-random micro-variations 1-15%
  {2, 8, 4, 12, 3, 7, 1, 15, 5, 10, 2, 6, 9, 3, 11, 4},
  // 1: Boom Bap — asymmetric 16th swing, rap 90s / Dilla style
  {0, 50, 0, 30, 0, 50, 0, 30, 0, 50, 0, 30, 0, 50, 0, 30},
  // 2: Trap Roll — ascending ramp, build / fills
  {0, 25, 50, 75, 0, 25, 50, 75, 0, 25, 50, 75, 0, 25, 50, 75},
  // 3: Halftime — isolated push on beat 3 (step 8), trap kick drag
  {0, 0, 0, 0, 0, 0, 0, 0, 50, 0, 0, 0, 0, 0, 0, 0},
  // 4: Reggaeton — Tresillo 3-3-2, latin trap / dembow
  {0, 0, 0, 40, 0, 0, 40, 0, 40, 0, 0, 40, 0, 0, 40, 0},
  // 5: Dub Drag — push beats 2 & 4 (steps 4, 12), reggae / dub feel
  {0, 0, 0, 0, 30, 0, 0, 0, 0, 0, 0, 0, 30, 0, 0, 0},
  // 6: Swing 50 — MPC classic 1:2 swing
  {0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50},
  // 7: Swing 75 — hard marked swing, near-ternary
  {0, 75, 0, 75, 0, 75, 0, 75, 0, 75, 0, 75, 0, 75, 0, 75},
};

#endif // GROOVE_TEMPLATES_H
