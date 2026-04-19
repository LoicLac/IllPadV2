#include "LedGrammar.h"
#include "KeyboardData.h"

// =================================================================
// EVENT_RENDER_DEFAULT — compile-time fallback table
// =================================================================
// Referenced from LedController when a NVS override entry has
// patternId == PTN_NONE. Values reflect the current ColorSlotStore v3
// enum ; step 0.6 will renumber these when v4 introduces
// CSLOT_MODE_*, CSLOT_VERB_*, CSLOT_CONFIRM_OK.
//
// Phase 0 status :
//   - Phase 0 entries (EVT_BANK_SWITCH .. EVT_CONFIRM_OK) use whatever
//     maps closest to the final grammar. EVT_PLAY and EVT_STOP reuse
//     CSLOT_HOLD_ON / CSLOT_HOLD_OFF respectively (closest equivalents
//     until CSLOT_VERB_PLAY exists in 0.6).
//   - LOOP entries are PTN_NONE : renderer treats as "no render" until
//     Phase 1+ wires them.
// =================================================================

const EventRenderEntry EVENT_RENDER_DEFAULT[EVT_COUNT] = {
  /* EVT_BANK_SWITCH       */ { PTN_BLINK_SLOW,      CSLOT_BANK_SWITCH,  100 },
  /* EVT_SCALE_ROOT        */ { PTN_BLINK_FAST,      CSLOT_SCALE_ROOT,   100 },
  /* EVT_SCALE_MODE        */ { PTN_BLINK_FAST,      CSLOT_SCALE_MODE,   100 },
  /* EVT_SCALE_CHROM       */ { PTN_BLINK_FAST,      CSLOT_SCALE_CHROM,  100 },
  /* EVT_OCTAVE            */ { PTN_BLINK_FAST,      CSLOT_OCTAVE,       100 },
  /* EVT_PLAY              */ { PTN_FADE,            CSLOT_HOLD_ON,      100 },
  /* EVT_STOP              */ { PTN_FADE,            CSLOT_HOLD_OFF,     100 },
  /* EVT_WAITING           */ { PTN_CROSSFADE_COLOR, CSLOT_ARPEG_FG,     100 },
  /* EVT_REFUSE            */ { PTN_BLINK_FAST,      CSLOT_SCALE_ROOT,   100 }, // placeholder (no red slot pre-0.6)
  /* EVT_CONFIRM_OK        */ { PTN_SPARK,           CSLOT_BANK_SWITCH,  100 }, // placeholder (no CONFIRM_OK slot pre-0.6)
  // LOOP reserved — not wired in Phase 0
  /* EVT_LOOP_REC          */ { PTN_NONE,            CSLOT_NORMAL_FG,      0 },
  /* EVT_LOOP_OVERDUB      */ { PTN_NONE,            CSLOT_NORMAL_FG,      0 },
  /* EVT_LOOP_SLOT_LOADED  */ { PTN_NONE,            CSLOT_NORMAL_FG,      0 },
  /* EVT_LOOP_SLOT_WRITTEN */ { PTN_NONE,            CSLOT_NORMAL_FG,      0 },
  /* EVT_LOOP_SLOT_CLEARED */ { PTN_NONE,            CSLOT_NORMAL_FG,      0 },
  /* EVT_LOOP_SLOT_REFUSED */ { PTN_NONE,            CSLOT_NORMAL_FG,      0 },
  /* EVT_LOOP_CLEAR        */ { PTN_NONE,            CSLOT_NORMAL_FG,      0 },
};
