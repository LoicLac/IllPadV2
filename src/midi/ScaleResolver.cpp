#include "ScaleResolver.h"

// A1=33, B1=35, C2=36, D2=38, E2=40, F2=41, G2=43
const uint8_t ScaleResolver::ROOT_MIDI_BASE[7] = {33, 35, 36, 38, 40, 41, 43};

// Intervals in semitones from root for each mode
const uint8_t ScaleResolver::SCALE_INTERVALS[7][7] = {
  {0, 2, 4, 5, 7, 9, 11},  // Ionian (Major)
  {0, 2, 3, 5, 7, 9, 10},  // Dorian
  {0, 1, 3, 5, 7, 8, 10},  // Phrygian
  {0, 2, 4, 6, 7, 9, 11},  // Lydian
  {0, 2, 4, 5, 7, 9, 10},  // Mixolydian
  {0, 2, 3, 5, 7, 8, 10},  // Aeolian (Natural Minor)
  {0, 1, 3, 5, 6, 8, 10},  // Locrian
};

uint8_t ScaleResolver::resolve(uint8_t padIndex, const uint8_t* padOrder,
                                const ScaleConfig& scale) {
  uint8_t order = padOrder[padIndex];
  if (order == 0xFF) return 0xFF;

  uint8_t rootBase = ROOT_MIDI_BASE[scale.root];

  if (scale.chromatic) {
    uint8_t note = rootBase + order;
    return (note <= 127) ? note : 0xFF;
  }

  uint8_t degree = order % 7;
  uint8_t octave = order / 7;
  uint8_t note = rootBase + (octave * 12) + SCALE_INTERVALS[scale.mode][degree];
  return (note <= 127) ? note : 0xFF;
}
