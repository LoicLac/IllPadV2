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
// Phase 0.1 respec : option γ abandoned. EVT_STOP now points to the new
// CSLOT_VERB_STOP slot (Coral default), decoupling STOP color from PLAY.
// The FADE direction (100 -> 0) still inverts PLAY's (0 -> 100) ; only the
// color differs. See docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md
// §4.4 label mapping and §7.2 runtime changes.
// =================================================================

const EventRenderEntry EVENT_RENDER_DEFAULT[EVT_COUNT] = {
  /* EVT_BANK_SWITCH       */ { PTN_BLINK_SLOW,      CSLOT_BANK_SWITCH,       100 },
  /* EVT_SCALE_ROOT        */ { PTN_BLINK_FAST,      CSLOT_SCALE_ROOT,        100 },
  /* EVT_SCALE_MODE        */ { PTN_BLINK_FAST,      CSLOT_SCALE_MODE,        100 },
  /* EVT_SCALE_CHROM       */ { PTN_BLINK_FAST,      CSLOT_SCALE_CHROM,       100 },
  /* EVT_OCTAVE            */ { PTN_BLINK_FAST,      CSLOT_OCTAVE,            100 },
  /* EVT_PLAY              */ { PTN_FADE,            CSLOT_VERB_PLAY,         100 },
  /* EVT_STOP              */ { PTN_FADE,            CSLOT_VERB_STOP,         100 }, // Phase 0.1 : dedicated STOP slot (Coral), FADE 100 -> 0 (direction inverts PLAY 0 -> 100)
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
