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

  // ARPEG_GEN helpers (spec §11) — work in scale-relative degrees.
  // padOrderToDegree : returns padOrder cast to signed degree (chromatic = semitone unit,
  //   7-notes scale = scale-degree unit). Trivially the order value, but the helper exists
  //   for spec clarity and to match degreeToMidi semantics.
  // degreeToMidi : inverse mapping. Returns 0xFF if resulting MIDI out of [0,127].
  //   Handles negative degrees with proper floor division (walk can dip below pile_lo).
  static int8_t  padOrderToDegree(uint8_t order, const ScaleConfig& scale);
  static uint8_t degreeToMidi(int8_t degree, const ScaleConfig& scale);

private:
  // Root note MIDI base values (octave 1-2):
  // A1=33, B1=35, C2=36, D2=38, E2=40, F2=41, G2=43
  static const uint8_t ROOT_MIDI_BASE[7];

  // Scale intervals: [mode][degree] — 7 modes × 7 degrees
  static const uint8_t SCALE_INTERVALS[7][7];
};

#endif // SCALE_RESOLVER_H
