#include "LedGrammar.h"
#include "KeyboardData.h"

// =================================================================
// EVENT_RENDER_DEFAULT — compile-time fallback table
// =================================================================
// Referenced from LedController when a NVS override entry has
// patternId == PTN_NONE. Values align with ColorSlotStore v4
// (15 slots, MODE_*/VERB_*/setup/CONFIRM_OK families) per LED spec §12.
//
// Phase 0 entries are wired (EVT_BANK_SWITCH .. EVT_CONFIRM_OK).
// LOOP entries are PTN_NONE : the renderer treats them as "no render"
// until Phase 1+ wires them.
//
// LED spec option γ : EVT_STOP reuses CSLOT_VERB_PLAY — the FADE
// direction (100 -> 0) inverts PLAY's (0 -> 100), giving the visual
// "what was on is fading out" without a dedicated STOP color slot.
// =================================================================

const EventRenderEntry EVENT_RENDER_DEFAULT[EVT_COUNT] = {
  /* EVT_BANK_SWITCH       */ { PTN_BLINK_SLOW,      CSLOT_BANK_SWITCH,       100 },
  /* EVT_SCALE_ROOT        */ { PTN_BLINK_FAST,      CSLOT_SCALE_ROOT,        100 },
  /* EVT_SCALE_MODE        */ { PTN_BLINK_FAST,      CSLOT_SCALE_MODE,        100 },
  /* EVT_SCALE_CHROM       */ { PTN_BLINK_FAST,      CSLOT_SCALE_CHROM,       100 },
  /* EVT_OCTAVE            */ { PTN_BLINK_FAST,      CSLOT_OCTAVE,            100 },
  /* EVT_PLAY              */ { PTN_FADE,            CSLOT_VERB_PLAY,         100 },
  /* EVT_STOP              */ { PTN_FADE,            CSLOT_VERB_PLAY,         100 }, // option γ : same color as PLAY, FADE direction inverted
  /* EVT_WAITING           */ { PTN_CROSSFADE_COLOR, CSLOT_MODE_ARPEG,        100 }, // placeholder colorA ; colorB supplied by LOOP callsite
  /* EVT_REFUSE            */ { PTN_BLINK_FAST,      CSLOT_VERB_REC,          100 },
  /* EVT_CONFIRM_OK        */ { PTN_SPARK,           CSLOT_CONFIRM_OK,        100 },
  // LOOP reserved — not wired in Phase 0
  /* EVT_LOOP_REC          */ { PTN_NONE,            CSLOT_VERB_REC,            0 },
  /* EVT_LOOP_OVERDUB      */ { PTN_NONE,            CSLOT_VERB_OVERDUB,        0 },
  /* EVT_LOOP_SLOT_LOADED  */ { PTN_NONE,            CSLOT_CONFIRM_OK,          0 },
  /* EVT_LOOP_SLOT_WRITTEN */ { PTN_NONE,            CSLOT_VERB_SAVE,           0 },
  /* EVT_LOOP_SLOT_CLEARED */ { PTN_NONE,            CSLOT_VERB_SLOT_CLEAR,     0 },
  /* EVT_LOOP_SLOT_REFUSED */ { PTN_NONE,            CSLOT_VERB_REC,            0 },
  /* EVT_LOOP_CLEAR        */ { PTN_NONE,            CSLOT_VERB_CLEAR_LOOP,     0 },
};
