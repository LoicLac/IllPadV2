#ifndef SCALE_RESOLVER_H
#define SCALE_RESOLVER_H

#include <stdint.h>
#include "../core/KeyboardData.h"

class ScaleResolver {
public:
  // Resolve pad index to MIDI note using pad ordering + scale config.
  // Returns 0xFF if pad is unmapped or note > 127.
  static uint8_t resolve(uint8_t padIndex, const uint8_t* padOrder,
                          const ScaleConfig& scale);

private:
  // Root note MIDI base values (octave 1-2):
  // A1=33, B1=35, C2=36, D2=38, E2=40, F2=41, G2=43
  static const uint8_t ROOT_MIDI_BASE[7];

  // Scale intervals: [mode][degree] — 7 modes × 7 degrees
  static const uint8_t SCALE_INTERVALS[7][7];
};

#endif // SCALE_RESOLVER_H
