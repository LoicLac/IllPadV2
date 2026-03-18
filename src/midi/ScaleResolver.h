#ifndef SCALE_RESOLVER_H
#define SCALE_RESOLVER_H

#include <stdint.h>
#include "../core/KeyboardData.h"

class ScaleResolver {
public:
  // Resolve pad index to MIDI note using pad ordering + scale config
  // Returns 0xFF if pad is unmapped or note out of range
  static uint8_t resolve(uint8_t padIndex, const uint8_t* padOrder,
                          const ScaleConfig& scale);

private:
  // Root note MIDI base values: A=57, B=59, C=60, D=62, E=64, F=65, G=67
  static const uint8_t ROOT_MIDI_BASE[7];

  // Scale intervals: [mode][degree] — 7 modes × 7 degrees
  static const uint8_t SCALE_INTERVALS[7][7];
};

#endif // SCALE_RESOLVER_H
