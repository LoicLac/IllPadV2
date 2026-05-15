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
  if (scale.root >= 7 || scale.mode >= 7) return 0xFF;

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

// =================================================================
// ARPEG_GEN helpers — degree <-> MIDI conversion in scale-relative space
// =================================================================
// In both chromatic and 7-notes scale, padOrder is the linear "degree" index :
//   chromatic   : 1 unit = 1 semitone (root = degree 0).
//   7-notes     : 1 unit = 1 scale degree (root = degree 0, +7 = next octave).
// padOrderToDegree is therefore an identity cast — kept as a named helper for spec
// readability and to mirror degreeToMidi.

int8_t ScaleResolver::padOrderToDegree(uint8_t order, const ScaleConfig& scale) {
  (void)scale;  // semantics encoded in degreeToMidi
  return (int8_t)order;
}

uint8_t ScaleResolver::degreeToMidi(int8_t degree, const ScaleConfig& scale) {
  if (scale.root >= 7 || scale.mode >= 7) return 0xFF;
  int16_t rootBase = (int16_t)ROOT_MIDI_BASE[scale.root];

  if (scale.chromatic) {
    int16_t note = rootBase + (int16_t)degree;
    if (note < 0 || note > 127) return 0xFF;
    return (uint8_t)note;
  }

  // 7-notes scale : floor division (handles negative degrees from walk dipping below pile_lo).
  // Cannot rely on C truncation toward zero — explicit floor.
  int16_t deg = (int16_t)degree;
  int16_t octave = deg / 7;
  int16_t deg7 = deg % 7;
  if (deg7 < 0) { deg7 += 7; octave -= 1; }

  int16_t note = rootBase + (octave * 12) + (int16_t)SCALE_INTERVALS[scale.mode][deg7];
  if (note < 0 || note > 127) return 0xFF;
  return (uint8_t)note;
}
