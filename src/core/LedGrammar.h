#ifndef LED_GRAMMAR_H
#define LED_GRAMMAR_H

#include <stdint.h>

// =================================================================
// LED Grammar — Unified pattern + event model
// =================================================================
// See docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md
// for the design rationale ("nom au fond, verbe en surface").
//
// 3-layer architecture :
//   COUCHE 1 — Palette of 9 fixed patterns (this file)
//   COUCHE 2 — Color slots (KeyboardData.h : ColorSlotStore)
//   COUCHE 3 — Event mapping (this file : EVENT_RENDER_DEFAULT)
//
// Include direction : KeyboardData.h -> LedGrammar.h (one-way).
// LedGrammar.h itself has no dependency on KeyboardData.h ; the
// EVENT_RENDER_DEFAULT array references color slot IDs as raw uint8_t
// values via LedGrammar.cpp (which may freely include KeyboardData.h).
// =================================================================

// -----------------------------------------------------------------
// Couche 1 — Pattern IDs
// -----------------------------------------------------------------
enum PatternId : uint8_t {
  PTN_SOLID            = 0,  // Steady brightness
  PTN_PULSE_SLOW       = 1,  // Moderate sine amplitude, slow (>= 2000 ms)
  PTN_CROSSFADE_COLOR  = 2,  // Continuous fade between two color slots
  PTN_BLINK_SLOW       = 3,  // On/off, ~800 ms total
  PTN_BLINK_FAST       = 4,  // On/off, ~300-450 ms total
  PTN_FADE             = 5,  // Linear ramp startPct -> endPct over durationMs
  PTN_FLASH            = 6,  // Short tick (30-80 ms), fg preserves bg intensity
  PTN_RAMP_HOLD        = 7,  // Long-press progression + SPARK suffix
  PTN_SPARK            = 8,  // Double-flash (confirm universel)
  PTN_COUNT            = 9,
  PTN_NONE             = 0xFF,  // Sentinel : "no override, use compile-time default"
};

// -----------------------------------------------------------------
// Couche 3 — Event IDs
// -----------------------------------------------------------------
// Phase 0 wires EVT_BANK_SWITCH .. EVT_CONFIRM_OK.
// LOOP events (EVT_LOOP_*) are reserved in the enum to stabilize
// indices before their consumers arrive in Phase 1+. Their default
// mapping uses PTN_NONE, and the renderer ignores such entries.
enum EventId : uint8_t {
  EVT_BANK_SWITCH        = 0,
  EVT_SCALE_ROOT         = 1,
  EVT_SCALE_MODE         = 2,
  EVT_SCALE_CHROM        = 3,
  EVT_OCTAVE             = 4,
  EVT_PLAY               = 5,
  EVT_STOP               = 6,
  EVT_WAITING            = 7,
  EVT_REFUSE             = 8,
  EVT_CONFIRM_OK         = 9,
  // LOOP reserved — not wired until Phase 1+
  EVT_LOOP_REC           = 10,
  EVT_LOOP_OVERDUB       = 11,
  EVT_LOOP_SLOT_LOADED   = 12,
  EVT_LOOP_SLOT_WRITTEN  = 13,
  EVT_LOOP_SLOT_CLEARED  = 14,
  EVT_LOOP_SLOT_REFUSED  = 15,
  EVT_LOOP_CLEAR         = 16,
  EVT_COUNT              = 17,
};

// -----------------------------------------------------------------
// Couche 1 — Pattern parameters
// -----------------------------------------------------------------
// Union discriminated by PatternId. Caller reads the field matching the
// pattern type. `raw[]` is a sizeof-pinning placeholder.
//
// Layout note : the union is packed to <= 16 bytes. Any new field must
// fit or the static_assert triggers.
struct PatternParams {
  union {
    struct { uint8_t  pct; }                                                  solid;
    struct { uint8_t  minPct; uint8_t maxPct; uint16_t periodMs; }            pulseSlow;
    struct { uint16_t periodMs; }                                             crossfadeColor;
    struct { uint16_t onMs; uint16_t offMs; uint8_t cycles; uint8_t blackoutOff; } blinkSlow;
    struct { uint16_t onMs; uint16_t offMs; uint8_t cycles; uint8_t blackoutOff; } blinkFast;
    struct { uint16_t durationMs; uint8_t startPct; uint8_t endPct; }         fade;
    struct { uint16_t durationMs; uint8_t fgPct; uint8_t bgPct; }             flash;
    struct { uint16_t rampMs; uint16_t suffixOnMs; uint16_t suffixGapMs;
             uint8_t  suffixCycles; }                                         rampHold;
    struct { uint16_t onMs; uint16_t gapMs; uint8_t cycles; }                 spark;
    uint8_t raw[10];  // sizeof-pinning placeholder
  };
};
static_assert(sizeof(PatternParams) <= 16, "PatternParams exceeds 16 bytes");

// -----------------------------------------------------------------
// Couche 3 — Event rendering entry
// -----------------------------------------------------------------
// Ties an event to its visual rendering. Editable via Tool 8 Page EVENTS
// (Phase 0 step 0.8d) and persisted in LedSettingsStore.eventOverrides[]
// (Phase 0 step 0.2). Sentinel patternId = PTN_NONE -> fallback on
// EVENT_RENDER_DEFAULT below.
//
// The colorSlot field is uint8_t rather than ColorSlotId to decouple
// this header from KeyboardData.h. Callers cast to ColorSlotId as needed.
struct EventRenderEntry {
  uint8_t patternId;   // PatternId value, or PTN_NONE sentinel
  uint8_t colorSlot;   // ColorSlotId value (defined in KeyboardData.h)
  uint8_t fgPct;       // 0-100 perceptual brightness
};
static_assert(sizeof(EventRenderEntry) == 3, "EventRenderEntry must be 3 bytes");

// -----------------------------------------------------------------
// Couche 3 — Compile-time default event mapping
// -----------------------------------------------------------------
// Authoritative fallback when NVS override entry has patternId == PTN_NONE.
// Definition lives in LedGrammar.cpp (where KeyboardData.h is included
// and color slot enum values are in scope). Each entry follows LED spec
// §12 (event mapping table).
//
// Phase 0 entries use color slots from the current ColorSlotStore (v3).
// Step 0.6 updates defaults when ColorSlotStore v4 lands with renamed
// slots (CSLOT_MODE_*, CSLOT_VERB_*, etc.).
extern const EventRenderEntry EVENT_RENDER_DEFAULT[EVT_COUNT];

#endif // LED_GRAMMAR_H
