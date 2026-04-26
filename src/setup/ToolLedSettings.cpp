// =================================================================
// Tool 8 — LED Settings (Phase 0.1 respec : single-view 6 sections)
// =================================================================
// Complete rewrite replacing the 3-page PATTERNS/COLORS/EVENTS UI with a
// single scrollable view organized around musician-facing semantics :
//   [NORMAL] [ARPEG] [LOOP] [TRANSPORT] [CONFIRMATIONS] [GLOBAL]
//
// Spec : docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md
// Plan : docs/superpowers/plans/2026-04-20-tool8-ux-respec-plan.md §4
// =================================================================

#include "ToolLedSettings.h"
#include "../managers/NvsManager.h"
#include "../managers/PotRouter.h"
#include "../core/LedController.h"
#include "../core/LedGrammar.h"
#include "SetupUI.h"
#include "InputParser.h"
#include <Arduino.h>
#include <string.h>

// =================================================================
// Static tables (file scope) — labels, section metadata, descriptions.
// All const, flash-resident (zero impact on SRAM).
// =================================================================

// Section display labels (upper-case, no ANSI prefix — SetupUI::drawSection applies).
static const char* const SECTION_LABELS[ToolLedSettings::SEC_COUNT] = {
  "NORMAL", "ARPEG", "LOOP", "TRANSPORT", "CONFIRMATIONS", "GLOBAL",
};

// Visible line labels. Ordered by LineId. Used by drawLine().
static const char* const LINE_LABELS[ToolLedSettings::LINE_COUNT] = {
  // NORMAL
  "Base color",
  "FG brightness",
  // ARPEG
  "Base color",
  "FG brightness",
  // LOOP
  "Base color",
  "FG brightness",
  "Save slot",
  "  duration",
  "Clear loop (hold)",
  "  duration",
  "Clear slot (combo)",
  "  duration",
  // TRANSPORT
  "Play fade-in",
  "  timing",
  "Stop fade-out",
  "  timing",
  "Waiting quantise",
  "Breathing",
  "Tick common",
  "Tick PLAY",
  "Tick REC",
  "Tick OVERDUB",
  "Tick BEAT duration",
  "Tick BAR duration",
  "Tick WRAP duration",
  // CONFIRMATIONS
  "Bank switch",
  "  timing",
  "Scale root",
  "  timing",
  "Scale mode",
  "  timing",
  "Scale chromatic",
  "  timing",
  "Octave",
  "  timing",
  "Confirm OK (SPARK)",
  "  timing",
  // GLOBAL
  "BG factor",
  "Master gamma",
};

// Description panel (spec §5.3) — one line per LineId, optionally multi-line
// separated by '\n' (caller wraps). Flash-resident.
static const char* const LINE_DESCRIPTIONS[ToolLedSettings::LINE_COUNT] = {
  "NORMAL base color - identifies NORMAL banks. FG shown at FG brightness, BG via BG factor.",
  "Foreground brightness for NORMAL banks (10-100%). BG derives as FG x BG factor.",
  "ARPEG base color - identifies ARPEG banks. Breathing when stopped-loaded.",
  "Foreground brightness for ARPEG banks (10-100%). Applied when playing (solid).",
  "LOOP base color - identifies LOOP banks. Consumed Phase 1+ (runtime dormant).",
  "Foreground brightness for LOOP banks (10-100%). Consumed Phase 1+.",
  "Save slot color - shown during the long-press ramp on slot pad (LOOP Phase 1+).",
  "Hold duration to trigger slot save (500-2000 ms). Shared with Tool 6.",
  "Clear loop color - shown during the long-press ramp on clear pad (LOOP Phase 1+).",
  "Hold duration to clear a LOOP bank (200-1500 ms). Shared with Tool 6.",
  "Slot delete color - visual feedback for the instant delete combo (not a hold).",
  "Visual duration of the slot delete feedback (400-1500 ms). Gesture is instant.",
  "Play fade-in color - flashes on Hold on or double-tap Play.",
  "Left/right focus: brightness (0-100) or duration (0-1000 ms). Up/down adjusts.",
  "Stop fade-out color - flashes on Hold off or double-tap Stop.",
  "Left/right focus: brightness (0-100) or duration (0-1000 ms). Up/down adjusts.",
  "Waiting quantise color - crossfades with mode color while waiting for beat/bar.",
  "Breathing min%, max%, period. Applies to FG ARPEG / LOOP stopped-loaded.",
  "Tick FG% and BG% - shared flash intensity across PLAY/REC/OVERDUB ticks.",
  "Tick PLAY color - ARPEG step flash and LOOP playing wrap tick.",
  "Tick REC color - LOOP recording bar and wrap ticks (Phase 1+).",
  "Tick OVERDUB color - LOOP overdubbing wrap tick (Phase 1+).",
  "Tick BEAT duration (5-500 ms). Consumed now for ARPEG step flash.",
  "Tick BAR duration (5-500 ms). Consumed Phase 1+ for LOOP bar flash.",
  "Tick WRAP duration (5-500 ms). Consumed Phase 1+ for LOOP wrap flash.",
  "Bank switch confirmation color - blinks on destination bank pad.",
  "Left/right focus: brightness (0-100) or duration (100-500 ms). Up/down adjusts.",
  "Scale root change color - blinks on changed pads (fires group when linked).",
  "Left/right focus: brightness or duration (100-500 ms). Up/down adjusts.",
  "Scale mode change color - blinks on changed pads.",
  "Left/right focus: brightness or duration (100-500 ms). Up/down adjusts.",
  "Scale chromatic toggle color - blinks on changed pads.",
  "Left/right focus: brightness or duration (100-500 ms). Up/down adjusts.",
  "Octave change color - blinks on the octave pad (ARPEG only).",
  "Left/right focus: brightness or duration (100-500 ms). Up/down adjusts.",
  "Confirm OK color - universal SPARK suffix (e.g. after a Tool save).",
  "Left/right focus: on (20-200 ms), gap (20-300 ms), cycles (1-4). Up/down adjusts.",
  "BG factor (10-50%). All BG banks render as FG color x this ratio.",
  "Master gamma (1.0-3.0). Affects perceptual LED intensity curve. Hot-reloaded.",
};

// Viewport size (content lines visible simultaneously). Sized for a 40-line
// terminal : ~1 header + frame top + 25 content + description (3) + frame
// bottom + control bar. Section titles render inline and do not count here ;
// a small slack absorbs the overhead (1 title per section = up to 6 extra rows
// if viewport spans all sections — in practice 1-2 visible at a time).
static const uint8_t VIEWPORT_SIZE = 22;

// =================================================================
// Constructor + begin
// =================================================================

ToolLedSettings::ToolLedSettings()
    : _setupEntryBankType(BANK_NORMAL),
      _leds(nullptr),
      _ui(nullptr),
      _potRouter(nullptr),
      _banks(nullptr),
      _cursor((LineId)0),
      _uiMode(UI_NAV),
      _editFocus(0),
      _nvsSaved(false),
      _viewportStart(0) {
  memset(&_lwk, 0, sizeof(_lwk));
  memset(&_cwk, 0, sizeof(_cwk));
  memset(&_ses, 0, sizeof(_ses));
  memset(&_lwkBackup, 0, sizeof(_lwkBackup));
  memset(&_cwkBackup, 0, sizeof(_cwkBackup));
  memset(&_sesBackup, 0, sizeof(_sesBackup));
}

void ToolLedSettings::begin(LedController* leds, SetupUI* ui,
                             PotRouter* potRouter, BankSlot* banks) {
  _leds = leds;
  _ui = ui;
  _potRouter = potRouter;
  _banks = banks;
}

// =================================================================
// NVS load / save
// =================================================================

void ToolLedSettings::loadAll() {
  // LedSettingsStore v7
  bool ledOk = NvsManager::loadBlob(LED_SETTINGS_NVS_NAMESPACE,
                                     LED_SETTINGS_NVS_KEY,
                                     EEPROM_MAGIC, LED_SETTINGS_VERSION,
                                     &_lwk, sizeof(_lwk));
  if (!ledOk) {
    // Fallback to defaults. NvsManager already logged the rejection.
    // Mirror NvsManager::NvsManager() defaults (kept in sync manually).
    memset(&_lwk, 0, sizeof(_lwk));
    _lwk.magic   = EEPROM_MAGIC;
    _lwk.version = LED_SETTINGS_VERSION;
    _lwk.normalFgIntensity = 85;
    _lwk.fgArpStopMin = 30; _lwk.fgArpStopMax = 100;
    _lwk.fgArpPlayMax = 80;
    _lwk.tickFlashFg = 100; _lwk.tickFlashBg = 25;
    _lwk.bgFactor = 25;
    _lwk.pulsePeriodMs = 1472;
    _lwk.tickBeatDurationMs = 30;
    _lwk.tickBarDurationMs  = 60;
    _lwk.tickWrapDurationMs = 100;
    _lwk.gammaTenths = 20;
    _lwk.sparkOnMs = 20; _lwk.sparkGapMs = 40; _lwk.sparkCycles = 4;
    _lwk.bankBlinks = 3; _lwk.bankDurationMs = 150; _lwk.bankBrightnessPct = 80;
    _lwk.scaleRootBlinks = 2; _lwk.scaleRootDurationMs = 130;
    _lwk.scaleModeBlinks = 2; _lwk.scaleModeDurationMs = 130;
    _lwk.scaleChromBlinks = 2; _lwk.scaleChromDurationMs = 130;
    _lwk.holdOnFadeMs = 500; _lwk.holdOffFadeMs = 500;
    _lwk.octaveBlinks = 3; _lwk.octaveDurationMs = 130;
    for (uint8_t i = 0; i < EVT_COUNT; i++) {
      _lwk.eventOverrides[i].patternId = PTN_NONE;
      _lwk.eventOverrides[i].colorSlot = 0;
      _lwk.eventOverrides[i].fgPct = 0;
    }
  }
  validateLedSettingsStore(_lwk);

  // ColorSlotStore v5
  bool colOk = NvsManager::loadBlob(LED_SETTINGS_NVS_NAMESPACE,
                                     COLOR_SLOT_NVS_KEY,
                                     COLOR_SLOT_MAGIC, COLOR_SLOT_VERSION,
                                     &_cwk, sizeof(_cwk));
  if (!colOk) {
    memset(&_cwk, 0, sizeof(_cwk));
    _cwk.magic   = COLOR_SLOT_MAGIC;
    _cwk.version = COLOR_SLOT_VERSION;
    // Compile-time defaults mirror NvsManager.cpp (Phase 0.1 respec).
    static const uint8_t dp[COLOR_SLOT_COUNT] = {
      1, 3, 7, 11, 8, 6, 5, 6, 10, 0, 6, 7, 8, 9, 0, 8,
    };
    static const int8_t  dh[COLOR_SLOT_COUNT] = {
      0, 0, 0, 0, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    for (uint8_t i = 0; i < COLOR_SLOT_COUNT; i++) {
      _cwk.slots[i].presetId  = dp[i];
      _cwk.slots[i].hueOffset = dh[i];
    }
  }

  // SettingsStore v11 (LOOP timers, shared with Tool 6)
  bool setOk = NvsManager::loadBlob(SETTINGS_NVS_NAMESPACE,
                                     SETTINGS_NVS_KEY,
                                     EEPROM_MAGIC, SETTINGS_VERSION,
                                     &_ses, sizeof(_ses));
  if (!setOk) {
    memset(&_ses, 0, sizeof(_ses));
    _ses.magic   = EEPROM_MAGIC;
    _ses.version = SETTINGS_VERSION;
    _ses.clearLoopTimerMs = 500;
    _ses.slotSaveTimerMs  = 1000;
    _ses.slotClearTimerMs = 800;
  }
  validateSettingsStore(_ses);

  // Snapshot foreground bank type for WAITING preview (spec §11.5).
  _setupEntryBankType = BANK_NORMAL;
  if (_banks) {
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (_banks[i].isForeground) {
        _setupEntryBankType = _banks[i].type;
        break;
      }
    }
  }
}

bool ToolLedSettings::saveLedSettings() {
  _lwk.magic   = EEPROM_MAGIC;
  _lwk.version = LED_SETTINGS_VERSION;
  _lwk.reserved = 0;
  validateLedSettingsStore(_lwk);
  bool ok = NvsManager::saveBlob(LED_SETTINGS_NVS_NAMESPACE,
                                  LED_SETTINGS_NVS_KEY,
                                  &_lwk, sizeof(_lwk));
  if (ok && _leds) _leds->loadLedSettings(_lwk);
  return ok;
}

bool ToolLedSettings::saveColorSlots() {
  _cwk.magic   = COLOR_SLOT_MAGIC;
  _cwk.version = COLOR_SLOT_VERSION;
  _cwk.reserved = 0;
  bool ok = NvsManager::saveBlob(LED_SETTINGS_NVS_NAMESPACE,
                                  COLOR_SLOT_NVS_KEY,
                                  &_cwk, sizeof(_cwk));
  if (ok && _leds) _leds->loadColorSlots(_cwk);
  return ok;
}

bool ToolLedSettings::saveSettings() {
  _ses.magic   = EEPROM_MAGIC;
  _ses.version = SETTINGS_VERSION;
  // NOTE: SettingsStore byte 3 = baselineProfile, NOT reserved — do NOT zero.
  validateSettingsStore(_ses);
  return NvsManager::saveBlob(SETTINGS_NVS_NAMESPACE,
                               SETTINGS_NVS_KEY,
                               &_ses, sizeof(_ses));
}

void ToolLedSettings::refreshBadge() {
  bool a = NvsManager::checkBlob(LED_SETTINGS_NVS_NAMESPACE,
                                  LED_SETTINGS_NVS_KEY,
                                  EEPROM_MAGIC, LED_SETTINGS_VERSION,
                                  sizeof(LedSettingsStore));
  bool b = NvsManager::checkBlob(LED_SETTINGS_NVS_NAMESPACE,
                                  COLOR_SLOT_NVS_KEY,
                                  COLOR_SLOT_MAGIC, COLOR_SLOT_VERSION,
                                  sizeof(ColorSlotStore));
  _nvsSaved = a && b;
}

// =================================================================
// Section / Line navigation helpers
// =================================================================

ToolLedSettings::Section ToolLedSettings::sectionOf(LineId line) const {
  if (line <= LINE_NORMAL_FG_PCT)          return SEC_NORMAL;
  if (line <= LINE_ARPEG_FG_PCT)           return SEC_ARPEG;
  if (line <= LINE_LOOP_SLOT_DURATION)     return SEC_LOOP;
  if (line <= LINE_TRANSPORT_TICK_WRAP_DUR) return SEC_TRANSPORT;
  if (line <= LINE_CONFIRM_OK_SPARK)       return SEC_CONFIRMATIONS;
  return SEC_GLOBAL;
}

ToolLedSettings::LineId ToolLedSettings::firstLineOfSection(Section s) const {
  switch (s) {
    case SEC_NORMAL:        return LINE_NORMAL_BASE_COLOR;
    case SEC_ARPEG:         return LINE_ARPEG_BASE_COLOR;
    case SEC_LOOP:          return LINE_LOOP_BASE_COLOR;
    case SEC_TRANSPORT:     return LINE_TRANSPORT_PLAY_COLOR;
    case SEC_CONFIRMATIONS: return LINE_CONFIRM_BANK_COLOR;
    case SEC_GLOBAL:        return LINE_GLOBAL_BG_FACTOR;
    default:                return LINE_NORMAL_BASE_COLOR;
  }
}

ToolLedSettings::LineId ToolLedSettings::lastLineOfSection(Section s) const {
  switch (s) {
    case SEC_NORMAL:        return LINE_NORMAL_FG_PCT;
    case SEC_ARPEG:         return LINE_ARPEG_FG_PCT;
    case SEC_LOOP:          return LINE_LOOP_SLOT_DURATION;
    case SEC_TRANSPORT:     return LINE_TRANSPORT_TICK_WRAP_DUR;
    case SEC_CONFIRMATIONS: return LINE_CONFIRM_OK_SPARK;
    case SEC_GLOBAL:        return LINE_GLOBAL_GAMMA;
    default:                return LINE_NORMAL_FG_PCT;
  }
}

void ToolLedSettings::cursorUp() {
  if (_cursor > 0) _cursor = (LineId)(_cursor - 1);
  ensureCursorVisible();
}
void ToolLedSettings::cursorDown() {
  if (_cursor + 1 < LINE_COUNT) _cursor = (LineId)(_cursor + 1);
  ensureCursorVisible();
}
void ToolLedSettings::cursorNextSection() {
  Section s = sectionOf(_cursor);
  if (s + 1 < SEC_COUNT) {
    _cursor = firstLineOfSection((Section)(s + 1));
    ensureCursorVisible();
  }
}
void ToolLedSettings::cursorPrevSection() {
  Section s = sectionOf(_cursor);
  if (s > 0) {
    _cursor = firstLineOfSection((Section)(s - 1));
    ensureCursorVisible();
  }
}
void ToolLedSettings::ensureCursorVisible() {
  if (_cursor < _viewportStart) _viewportStart = _cursor;
  if (_cursor >= _viewportStart + VIEWPORT_SIZE) {
    _viewportStart = _cursor - VIEWPORT_SIZE + 1;
  }
  if (_viewportStart + VIEWPORT_SIZE > LINE_COUNT) {
    _viewportStart = (LINE_COUNT > VIEWPORT_SIZE)
                      ? (uint8_t)(LINE_COUNT - VIEWPORT_SIZE) : 0;
  }
}

// =================================================================
// Line shape + field introspection
// =================================================================

ToolLedSettings::LineShape ToolLedSettings::shapeForLine(LineId line) const {
  switch (line) {
    // COLOR lines (15 total)
    case LINE_NORMAL_BASE_COLOR:
    case LINE_ARPEG_BASE_COLOR:
    case LINE_LOOP_BASE_COLOR:
    case LINE_LOOP_SAVE_COLOR:
    case LINE_LOOP_CLEAR_COLOR:
    case LINE_LOOP_SLOT_COLOR:
    case LINE_TRANSPORT_PLAY_COLOR:
    case LINE_TRANSPORT_STOP_COLOR:
    case LINE_TRANSPORT_WAITING_COLOR:
    case LINE_TRANSPORT_TICK_PLAY_COLOR:
    case LINE_TRANSPORT_TICK_REC_COLOR:
    case LINE_TRANSPORT_TICK_OVERDUB_COLOR:
    case LINE_CONFIRM_BANK_COLOR:
    case LINE_CONFIRM_SCALE_ROOT_COLOR:
    case LINE_CONFIRM_SCALE_MODE_COLOR:
    case LINE_CONFIRM_SCALE_CHROM_COLOR:
    case LINE_CONFIRM_OCTAVE_COLOR:
    case LINE_CONFIRM_OK_COLOR:
      return SHAPE_COLOR;
    // MULTI lines
    case LINE_TRANSPORT_PLAY_TIMING:
    case LINE_TRANSPORT_STOP_TIMING:
    case LINE_TRANSPORT_BREATHING:
    case LINE_TRANSPORT_TICK_COMMON:
    case LINE_CONFIRM_BANK_TIMING:
    case LINE_CONFIRM_SCALE_ROOT_TIMING:
    case LINE_CONFIRM_SCALE_MODE_TIMING:
    case LINE_CONFIRM_SCALE_CHROM_TIMING:
    case LINE_CONFIRM_OCTAVE_TIMING:
    case LINE_CONFIRM_OK_SPARK:
      return SHAPE_MULTI_NUM;
    // Everything else = SINGLE numeric
    default:
      return SHAPE_SINGLE_NUM;
  }
}

ColorSlot* ToolLedSettings::colorSlotForLine(LineId line) {
  switch (line) {
    case LINE_NORMAL_BASE_COLOR:        return &_cwk.slots[CSLOT_MODE_NORMAL];
    case LINE_ARPEG_BASE_COLOR:         return &_cwk.slots[CSLOT_MODE_ARPEG];
    case LINE_LOOP_BASE_COLOR:          return &_cwk.slots[CSLOT_MODE_LOOP];
    case LINE_LOOP_SAVE_COLOR:          return &_cwk.slots[CSLOT_VERB_SAVE];
    case LINE_LOOP_CLEAR_COLOR:         return &_cwk.slots[CSLOT_VERB_CLEAR_LOOP];
    case LINE_LOOP_SLOT_COLOR:          return &_cwk.slots[CSLOT_VERB_SLOT_CLEAR];
    case LINE_TRANSPORT_PLAY_COLOR:     return &_cwk.slots[CSLOT_VERB_PLAY];
    case LINE_TRANSPORT_STOP_COLOR:     return &_cwk.slots[CSLOT_VERB_STOP];
    case LINE_TRANSPORT_WAITING_COLOR:  return &_cwk.slots[CSLOT_VERB_PLAY];  // WAITING target = PLAY per spec §11.5
    case LINE_TRANSPORT_TICK_PLAY_COLOR:    return &_cwk.slots[CSLOT_VERB_PLAY];
    case LINE_TRANSPORT_TICK_REC_COLOR:     return &_cwk.slots[CSLOT_VERB_REC];
    case LINE_TRANSPORT_TICK_OVERDUB_COLOR: return &_cwk.slots[CSLOT_VERB_OVERDUB];
    case LINE_CONFIRM_BANK_COLOR:       return &_cwk.slots[CSLOT_BANK_SWITCH];
    case LINE_CONFIRM_SCALE_ROOT_COLOR: return &_cwk.slots[CSLOT_SCALE_ROOT];
    case LINE_CONFIRM_SCALE_MODE_COLOR: return &_cwk.slots[CSLOT_SCALE_MODE];
    case LINE_CONFIRM_SCALE_CHROM_COLOR:return &_cwk.slots[CSLOT_SCALE_CHROM];
    case LINE_CONFIRM_OCTAVE_COLOR:     return &_cwk.slots[CSLOT_OCTAVE];
    case LINE_CONFIRM_OK_COLOR:         return &_cwk.slots[CSLOT_CONFIRM_OK];
    default: return nullptr;
  }
}

uint8_t ToolLedSettings::numericFieldCountForLine(LineId line) const {
  switch (line) {
    // Multi : 2 fields
    case LINE_TRANSPORT_PLAY_TIMING:
    case LINE_TRANSPORT_STOP_TIMING:
    case LINE_TRANSPORT_TICK_COMMON:
    case LINE_CONFIRM_BANK_TIMING:
    case LINE_CONFIRM_SCALE_ROOT_TIMING:
    case LINE_CONFIRM_SCALE_MODE_TIMING:
    case LINE_CONFIRM_SCALE_CHROM_TIMING:
    case LINE_CONFIRM_OCTAVE_TIMING:
      return 2;
    // Multi : 3 fields
    case LINE_TRANSPORT_BREATHING:
    case LINE_CONFIRM_OK_SPARK:
      return 3;
    // Single : 1 field
    default:
      return 1;
  }
}

uint8_t ToolLedSettings::effectiveEventFgPct(uint8_t evt) const {
  if (evt >= EVT_COUNT) return 100;
  const EventRenderEntry& e = _lwk.eventOverrides[evt];
  if (e.patternId == PTN_NONE) return EVENT_RENDER_DEFAULT[evt].fgPct;
  return e.fgPct;
}

void ToolLedSettings::setEventOverrideFgPct(uint8_t evt, uint8_t newFgPct) {
  if (evt >= EVT_COUNT) return;
  EventRenderEntry& e = _lwk.eventOverrides[evt];
  // Activate override : patternId must be non-NONE. If currently NONE, copy default.
  if (e.patternId == PTN_NONE) {
    e.patternId = EVENT_RENDER_DEFAULT[evt].patternId;
    e.colorSlot = EVENT_RENDER_DEFAULT[evt].colorSlot;
  }
  e.fgPct = newFgPct;
}

int32_t ToolLedSettings::readNumericField(LineId line, uint8_t f) const {
  switch (line) {
    case LINE_NORMAL_FG_PCT:              return _lwk.normalFgIntensity;
    case LINE_ARPEG_FG_PCT:               return _lwk.fgArpPlayMax;
    case LINE_LOOP_FG_PCT:                return _lwk.fgArpPlayMax; // LOOP reuses ARPEG FG pct (no separate field yet)
    case LINE_LOOP_SAVE_DURATION:         return _ses.slotSaveTimerMs;
    case LINE_LOOP_CLEAR_DURATION:        return _ses.clearLoopTimerMs;
    case LINE_LOOP_SLOT_DURATION:         return _ses.slotClearTimerMs;
    case LINE_TRANSPORT_PLAY_TIMING:      return (f == 0) ? (int32_t)effectiveEventFgPct(EVT_PLAY)
                                                          : (int32_t)_lwk.holdOnFadeMs;
    case LINE_TRANSPORT_STOP_TIMING:      return (f == 0) ? (int32_t)effectiveEventFgPct(EVT_STOP)
                                                          : (int32_t)_lwk.holdOffFadeMs;
    case LINE_TRANSPORT_BREATHING:        return (f == 0) ? (int32_t)_lwk.fgArpStopMin
                                                          : (f == 1) ? (int32_t)_lwk.fgArpStopMax
                                                                     : (int32_t)_lwk.pulsePeriodMs;
    case LINE_TRANSPORT_TICK_COMMON:      return (f == 0) ? (int32_t)_lwk.tickFlashFg
                                                          : (int32_t)_lwk.tickFlashBg;
    case LINE_TRANSPORT_TICK_BEAT_DUR:    return _lwk.tickBeatDurationMs;
    case LINE_TRANSPORT_TICK_BAR_DUR:     return _lwk.tickBarDurationMs;
    case LINE_TRANSPORT_TICK_WRAP_DUR:    return _lwk.tickWrapDurationMs;
    case LINE_CONFIRM_BANK_TIMING:        return (f == 0) ? (int32_t)_lwk.bankBrightnessPct
                                                          : (int32_t)_lwk.bankDurationMs;
    case LINE_CONFIRM_SCALE_ROOT_TIMING:  return (f == 0) ? (int32_t)effectiveEventFgPct(EVT_SCALE_ROOT)
                                                          : (int32_t)_lwk.scaleRootDurationMs;
    case LINE_CONFIRM_SCALE_MODE_TIMING:  return (f == 0) ? (int32_t)effectiveEventFgPct(EVT_SCALE_MODE)
                                                          : (int32_t)_lwk.scaleModeDurationMs;
    case LINE_CONFIRM_SCALE_CHROM_TIMING: return (f == 0) ? (int32_t)effectiveEventFgPct(EVT_SCALE_CHROM)
                                                          : (int32_t)_lwk.scaleChromDurationMs;
    case LINE_CONFIRM_OCTAVE_TIMING:      return (f == 0) ? (int32_t)effectiveEventFgPct(EVT_OCTAVE)
                                                          : (int32_t)_lwk.octaveDurationMs;
    case LINE_CONFIRM_OK_SPARK:           return (f == 0) ? (int32_t)_lwk.sparkOnMs
                                                          : (f == 1) ? (int32_t)_lwk.sparkGapMs
                                                                     : (int32_t)_lwk.sparkCycles;
    case LINE_GLOBAL_BG_FACTOR:           return _lwk.bgFactor;
    case LINE_GLOBAL_GAMMA:               return _lwk.gammaTenths;
    default: return 0;
  }
}

void ToolLedSettings::writeNumericField(LineId line, uint8_t f, int32_t v) {
  switch (line) {
    case LINE_NORMAL_FG_PCT:              _lwk.normalFgIntensity = (uint8_t)v; break;
    case LINE_ARPEG_FG_PCT:               _lwk.fgArpPlayMax = (uint8_t)v; break;
    case LINE_LOOP_FG_PCT:                _lwk.fgArpPlayMax = (uint8_t)v; break; // shared w/ ARPEG
    case LINE_LOOP_SAVE_DURATION:         _ses.slotSaveTimerMs = (uint16_t)v; break;
    case LINE_LOOP_CLEAR_DURATION:        _ses.clearLoopTimerMs = (uint16_t)v; break;
    case LINE_LOOP_SLOT_DURATION:         _ses.slotClearTimerMs = (uint16_t)v; break;
    case LINE_TRANSPORT_PLAY_TIMING:
      if (f == 0) setEventOverrideFgPct(EVT_PLAY, (uint8_t)v);
      else        _lwk.holdOnFadeMs = (uint16_t)v;
      break;
    case LINE_TRANSPORT_STOP_TIMING:
      if (f == 0) setEventOverrideFgPct(EVT_STOP, (uint8_t)v);
      else        _lwk.holdOffFadeMs = (uint16_t)v;
      break;
    case LINE_TRANSPORT_BREATHING:
      if (f == 0) _lwk.fgArpStopMin = (uint8_t)v;
      else if (f == 1) _lwk.fgArpStopMax = (uint8_t)v;
      else        _lwk.pulsePeriodMs = (uint16_t)v;
      break;
    case LINE_TRANSPORT_TICK_COMMON:
      if (f == 0) _lwk.tickFlashFg = (uint8_t)v;
      else        _lwk.tickFlashBg = (uint8_t)v;
      break;
    case LINE_TRANSPORT_TICK_BEAT_DUR:    _lwk.tickBeatDurationMs = (uint16_t)v; break;
    case LINE_TRANSPORT_TICK_BAR_DUR:     _lwk.tickBarDurationMs  = (uint16_t)v; break;
    case LINE_TRANSPORT_TICK_WRAP_DUR:    _lwk.tickWrapDurationMs = (uint16_t)v; break;
    case LINE_CONFIRM_BANK_TIMING:
      if (f == 0) _lwk.bankBrightnessPct = (uint8_t)v;
      else        _lwk.bankDurationMs = (uint16_t)v;
      break;
    case LINE_CONFIRM_SCALE_ROOT_TIMING:
      if (f == 0) setEventOverrideFgPct(EVT_SCALE_ROOT, (uint8_t)v);
      else        _lwk.scaleRootDurationMs = (uint16_t)v;
      break;
    case LINE_CONFIRM_SCALE_MODE_TIMING:
      if (f == 0) setEventOverrideFgPct(EVT_SCALE_MODE, (uint8_t)v);
      else        _lwk.scaleModeDurationMs = (uint16_t)v;
      break;
    case LINE_CONFIRM_SCALE_CHROM_TIMING:
      if (f == 0) setEventOverrideFgPct(EVT_SCALE_CHROM, (uint8_t)v);
      else        _lwk.scaleChromDurationMs = (uint16_t)v;
      break;
    case LINE_CONFIRM_OCTAVE_TIMING:
      if (f == 0) setEventOverrideFgPct(EVT_OCTAVE, (uint8_t)v);
      else        _lwk.octaveDurationMs = (uint16_t)v;
      break;
    case LINE_CONFIRM_OK_SPARK:
      if (f == 0) _lwk.sparkOnMs = (uint16_t)v;
      else if (f == 1) _lwk.sparkGapMs = (uint16_t)v;
      else        _lwk.sparkCycles = (uint8_t)v;
      break;
    case LINE_GLOBAL_BG_FACTOR:           _lwk.bgFactor = (uint8_t)v; break;
    case LINE_GLOBAL_GAMMA:               _lwk.gammaTenths = (uint8_t)v; break;
    default: break;
  }
}

void ToolLedSettings::getNumericFieldBounds(LineId line, uint8_t f,
                                             int32_t& mn, int32_t& mx,
                                             int32_t& coarse, int32_t& fine) const {
  // Defaults (most common).
  mn = 0; mx = 100; coarse = 10; fine = 1;
  switch (line) {
    case LINE_NORMAL_FG_PCT:
    case LINE_ARPEG_FG_PCT:
    case LINE_LOOP_FG_PCT:                mn = 10; mx = 100; break;
    case LINE_LOOP_SAVE_DURATION:         mn = 500; mx = 2000; coarse = 100; fine = 10; break;
    case LINE_LOOP_CLEAR_DURATION:        mn = 200; mx = 1500; coarse = 100; fine = 10; break;
    case LINE_LOOP_SLOT_DURATION:         mn = 400; mx = 1500; coarse = 100; fine = 10; break;
    case LINE_TRANSPORT_PLAY_TIMING:
    case LINE_TRANSPORT_STOP_TIMING:
      if (f == 0) { mn = 0; mx = 100; coarse = 10; fine = 1; }
      else        { mn = 0; mx = 1000; coarse = 100; fine = 10; }
      break;
    case LINE_TRANSPORT_BREATHING:
      if (f == 2) { mn = 500; mx = 4000; coarse = 100; fine = 50; }
      else        { mn = 0; mx = 100; coarse = 10; fine = 1; }
      break;
    case LINE_TRANSPORT_TICK_COMMON:      mn = 0; mx = 100; coarse = 10; fine = 1; break;
    case LINE_TRANSPORT_TICK_BEAT_DUR:
    case LINE_TRANSPORT_TICK_BAR_DUR:
    case LINE_TRANSPORT_TICK_WRAP_DUR:    mn = 5; mx = 500; coarse = 10; fine = 1; break;
    case LINE_CONFIRM_BANK_TIMING:
    case LINE_CONFIRM_SCALE_ROOT_TIMING:
    case LINE_CONFIRM_SCALE_MODE_TIMING:
    case LINE_CONFIRM_SCALE_CHROM_TIMING:
    case LINE_CONFIRM_OCTAVE_TIMING:
      if (f == 0) { mn = 0; mx = 100; coarse = 10; fine = 1; }
      else        { mn = 100; mx = 500; coarse = 50; fine = 10; }
      break;
    case LINE_CONFIRM_OK_SPARK:
      if (f == 0)      { mn = 20; mx = 200; coarse = 10; fine = 1; }   // on
      else if (f == 1) { mn = 20; mx = 300; coarse = 10; fine = 1; }   // gap
      else             { mn = 1; mx = 4; coarse = 1; fine = 1; }        // cycles
      break;
    case LINE_GLOBAL_BG_FACTOR:           mn = 10; mx = 50; coarse = 5; fine = 1; break;
    case LINE_GLOBAL_GAMMA:               mn = 10; mx = 30; coarse = 2; fine = 1; break;
    default: break;
  }
}

// =================================================================
// Edit paradigms
// =================================================================

void ToolLedSettings::editColor(LineId line, int dx, int dy, bool accel) {
  ColorSlot* s = colorSlotForLine(line);
  if (!s) return;
  if (dx != 0) {
    // Cycle preset, wrap.
    int p = (int)s->presetId + dx;
    if (p < 0) p = COLOR_PRESET_COUNT - 1;
    if (p >= (int)COLOR_PRESET_COUNT) p = 0;
    s->presetId = (uint8_t)p;
  }
  if (dy != 0) {
    int step = accel ? 10 : 1;
    int h = (int)s->hueOffset + dy * step;
    if (h < -128) h = -128;
    if (h > 127)  h = 127;
    s->hueOffset = (int8_t)h;
  }
  updatePreviewContext();
}

void ToolLedSettings::editSingleNumeric(LineId line, int dx, int dy, bool /*accel*/) {
  int32_t mn, mx, coarse, fine;
  getNumericFieldBounds(line, 0, mn, mx, coarse, fine);
  int32_t cur = readNumericField(line, 0);
  int32_t dir = dx + dy;  // horizontal coarse, vertical fine — same net sign
  int32_t step = (dx != 0) ? coarse : fine;
  int32_t nv = cur + dir * step;
  if (nv < mn) nv = mn;
  if (nv > mx) nv = mx;
  writeNumericField(line, 0, nv);
  updatePreviewContext();
}

void ToolLedSettings::editMultiNumeric(LineId line, int dx, int dy, bool accel) {
  uint8_t count = numericFieldCountForLine(line);
  if (count == 0) return;
  if (dx != 0) {
    // Focus navigation.
    int f = (int)_editFocus + dx;
    if (f < 0) f = count - 1;
    if (f >= (int)count) f = 0;
    _editFocus = (uint8_t)f;
    return;
  }
  if (dy != 0) {
    int32_t mn, mx, coarse, fine;
    getNumericFieldBounds(line, _editFocus, mn, mx, coarse, fine);
    int32_t cur = readNumericField(line, _editFocus);
    int32_t step = accel ? coarse : fine;
    int32_t nv = cur + dy * step;
    if (nv < mn) nv = mn;
    if (nv > mx) nv = mx;
    writeNumericField(line, _editFocus, nv);
    updatePreviewContext();
  }
}

// =================================================================
// Edit dispatch
// =================================================================

void ToolLedSettings::enterEdit() {
  _lwkBackup = _lwk;
  _cwkBackup = _cwk;
  _sesBackup = _ses;
  _uiMode = UI_EDIT;
  _editFocus = 0;
  updatePreviewContext();
}

void ToolLedSettings::commitEdit() {
  LineShape shape = shapeForLine(_cursor);
  // Save target depends on which store the line touches.
  bool wroteAny = false;
  if (shape == SHAPE_COLOR) {
    wroteAny = saveColorSlots();
  } else {
    // Numeric : determine target store.
    switch (_cursor) {
      case LINE_LOOP_SAVE_DURATION:
      case LINE_LOOP_CLEAR_DURATION:
      case LINE_LOOP_SLOT_DURATION:
        wroteAny = saveSettings();
        break;
      default:
        wroteAny = saveLedSettings();
        // Gamma hot-reload (spec §3 decision 9) — LedController::rebuildGammaLut
        // is already invoked via loadLedSettings() inside saveLedSettings().
        break;
    }
  }
  if (wroteAny && _ui) _ui->flashSaved();
  refreshBadge();
  _uiMode = UI_NAV;
  _editFocus = 0;
  updatePreviewContext();
}

void ToolLedSettings::cancelEdit() {
  _lwk = _lwkBackup;
  _cwk = _cwkBackup;
  _ses = _sesBackup;
  _uiMode = UI_NAV;
  _editFocus = 0;
  updatePreviewContext();
}

// =================================================================
// Default reset per line (spec §9)
// =================================================================

void ToolLedSettings::resetDefaultForLine(LineId line) {
  // Colors : restore preset + hue per Phase 0.1 defaults.
  static const uint8_t dp[COLOR_SLOT_COUNT] = {
    1, 3, 7, 11, 8, 6, 5, 6, 10, 0, 6, 7, 8, 9, 0, 8,
  };
  static const int8_t  dh[COLOR_SLOT_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  ColorSlot* s = colorSlotForLine(line);
  if (s) {
    // Reverse-map line → CSLOT id to pick the default.
    // Simpler : walk _cwk.slots pointer-equal to s, read index.
    for (uint8_t i = 0; i < COLOR_SLOT_COUNT; i++) {
      if (&_cwk.slots[i] == s) {
        s->presetId  = dp[i];
        s->hueOffset = dh[i];
        break;
      }
    }
    return;
  }
  // Numerics
  switch (line) {
    case LINE_NORMAL_FG_PCT:              _lwk.normalFgIntensity = 85; break;
    case LINE_ARPEG_FG_PCT:               _lwk.fgArpPlayMax = 80; break;
    case LINE_LOOP_FG_PCT:                _lwk.fgArpPlayMax = 80; break;
    case LINE_LOOP_SAVE_DURATION:         _ses.slotSaveTimerMs = 1000; break;
    case LINE_LOOP_CLEAR_DURATION:        _ses.clearLoopTimerMs = 500; break;
    case LINE_LOOP_SLOT_DURATION:         _ses.slotClearTimerMs = 800; break;
    case LINE_TRANSPORT_PLAY_TIMING:
      setEventOverrideFgPct(EVT_PLAY, 100);
      _lwk.holdOnFadeMs = 500;
      break;
    case LINE_TRANSPORT_STOP_TIMING:
      setEventOverrideFgPct(EVT_STOP, 100);
      _lwk.holdOffFadeMs = 500;
      break;
    case LINE_TRANSPORT_BREATHING:
      _lwk.fgArpStopMin = 60;
      _lwk.fgArpStopMax = 90;
      _lwk.pulsePeriodMs = 2500;
      break;
    case LINE_TRANSPORT_TICK_COMMON:
      _lwk.tickFlashFg = 100;
      _lwk.tickFlashBg = 25;
      break;
    case LINE_TRANSPORT_TICK_BEAT_DUR:    _lwk.tickBeatDurationMs = 30; break;
    case LINE_TRANSPORT_TICK_BAR_DUR:     _lwk.tickBarDurationMs  = 60; break;
    case LINE_TRANSPORT_TICK_WRAP_DUR:    _lwk.tickWrapDurationMs = 100; break;
    case LINE_CONFIRM_BANK_TIMING:
      _lwk.bankBrightnessPct = 80;
      _lwk.bankDurationMs = 150;
      break;
    case LINE_CONFIRM_SCALE_ROOT_TIMING:
      setEventOverrideFgPct(EVT_SCALE_ROOT, 100);
      _lwk.scaleRootDurationMs = 130;
      break;
    case LINE_CONFIRM_SCALE_MODE_TIMING:
      setEventOverrideFgPct(EVT_SCALE_MODE, 100);
      _lwk.scaleModeDurationMs = 130;
      break;
    case LINE_CONFIRM_SCALE_CHROM_TIMING:
      setEventOverrideFgPct(EVT_SCALE_CHROM, 100);
      _lwk.scaleChromDurationMs = 130;
      break;
    case LINE_CONFIRM_OCTAVE_TIMING:
      setEventOverrideFgPct(EVT_OCTAVE, 100);
      _lwk.octaveDurationMs = 130;
      break;
    case LINE_CONFIRM_OK_SPARK:
      _lwk.sparkOnMs = 20;
      _lwk.sparkGapMs = 40;
      _lwk.sparkCycles = 4;
      break;
    case LINE_GLOBAL_BG_FACTOR:           _lwk.bgFactor = 25; break;
    case LINE_GLOBAL_GAMMA:               _lwk.gammaTenths = 20; break;
    default: break;
  }
}

void ToolLedSettings::resetLineDefault() {
  resetDefaultForLine(_cursor);
  // Save the relevant store (mirrors commitEdit logic).
  LineShape shape = shapeForLine(_cursor);
  bool wrote = false;
  if (shape == SHAPE_COLOR) {
    wrote = saveColorSlots();
  } else {
    switch (_cursor) {
      case LINE_LOOP_SAVE_DURATION:
      case LINE_LOOP_CLEAR_DURATION:
      case LINE_LOOP_SLOT_DURATION:
        wrote = saveSettings();
        break;
      default:
        wrote = saveLedSettings();
        break;
    }
  }
  if (wrote && _ui) _ui->flashSaved();
  refreshBadge();
  updatePreviewContext();
}

// =================================================================
// Preview context dispatch (spec §6.2)
// =================================================================

void ToolLedSettings::updatePreviewContext() {
  ToolLedPreview::Params p;
  memset(&p, 0, sizeof(p));
  p.bgFactorPct = _lwk.bgFactor;

  // Resolve PLAY color once (used by multiple contexts for tickColorPlayBg etc).
  RGBW playColor = resolveColorSlot(_cwk.slots[CSLOT_VERB_PLAY]);

  ToolLedPreview::PreviewContext ctx = ToolLedPreview::PV_NONE;

  switch (_cursor) {
    // --- Base color / FG brightness : mono-FG mockup ---
    case LINE_NORMAL_BASE_COLOR:
    case LINE_NORMAL_FG_PCT:
      ctx = ToolLedPreview::PV_BASE_COLOR;
      p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_NORMAL]);
      p.fgPct   = _lwk.normalFgIntensity;
      break;
    case LINE_ARPEG_BASE_COLOR:
    case LINE_ARPEG_FG_PCT:
      ctx = ToolLedPreview::PV_BASE_COLOR;
      p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_ARPEG]);
      p.fgPct   = _lwk.fgArpPlayMax;
      break;
    case LINE_LOOP_BASE_COLOR:
    case LINE_LOOP_FG_PCT:
      ctx = ToolLedPreview::PV_BASE_COLOR;
      p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_LOOP]);
      p.fgPct   = _lwk.fgArpPlayMax;
      break;

    // --- LOOP gestures : event replay (RAMP_HOLD-ish, preview as FADE) ---
    case LINE_LOOP_SAVE_COLOR:
    case LINE_LOOP_SAVE_DURATION:
      ctx = ToolLedPreview::PV_EVENT_REPLAY;
      p.replayInst.patternId = PTN_FADE;
      p.replayInst.fgPct     = 100;
      p.replayInst.ledMask   = 0x08;  // LED 3 only
      p.replayInst.colorA    = resolveColorSlot(_cwk.slots[CSLOT_VERB_SAVE]);
      p.replayInst.params.fade.durationMs = _ses.slotSaveTimerMs;
      p.replayInst.params.fade.startPct   = 0;
      p.replayInst.params.fade.endPct     = 100;
      p.effectDurationMs = _ses.slotSaveTimerMs;
      break;
    case LINE_LOOP_CLEAR_COLOR:
    case LINE_LOOP_CLEAR_DURATION:
      ctx = ToolLedPreview::PV_EVENT_REPLAY;
      p.replayInst.patternId = PTN_FADE;
      p.replayInst.fgPct     = 100;
      p.replayInst.ledMask   = 0x08;
      p.replayInst.colorA    = resolveColorSlot(_cwk.slots[CSLOT_VERB_CLEAR_LOOP]);
      p.replayInst.params.fade.durationMs = _ses.clearLoopTimerMs;
      p.replayInst.params.fade.startPct   = 0;
      p.replayInst.params.fade.endPct     = 100;
      p.effectDurationMs = _ses.clearLoopTimerMs;
      break;
    case LINE_LOOP_SLOT_COLOR:
    case LINE_LOOP_SLOT_DURATION:
      ctx = ToolLedPreview::PV_EVENT_REPLAY;
      p.replayInst.patternId = PTN_FADE;
      p.replayInst.fgPct     = 100;
      p.replayInst.ledMask   = 0x08;
      p.replayInst.colorA    = resolveColorSlot(_cwk.slots[CSLOT_VERB_SLOT_CLEAR]);
      p.replayInst.params.fade.durationMs = _ses.slotClearTimerMs;
      p.replayInst.params.fade.startPct   = 100;
      p.replayInst.params.fade.endPct     = 0;
      p.effectDurationMs = _ses.slotClearTimerMs;
      break;

    // --- Transport fades ---
    case LINE_TRANSPORT_PLAY_COLOR:
    case LINE_TRANSPORT_PLAY_TIMING:
      ctx = ToolLedPreview::PV_EVENT_REPLAY;
      p.replayInst.patternId = PTN_FADE;
      p.replayInst.fgPct     = effectiveEventFgPct(EVT_PLAY);
      p.replayInst.ledMask   = 0x08;
      p.replayInst.colorA    = resolveColorSlot(_cwk.slots[CSLOT_VERB_PLAY]);
      p.replayInst.params.fade.durationMs = _lwk.holdOnFadeMs;
      p.replayInst.params.fade.startPct   = 0;
      p.replayInst.params.fade.endPct     = effectiveEventFgPct(EVT_PLAY);
      p.effectDurationMs = _lwk.holdOnFadeMs;
      break;
    case LINE_TRANSPORT_STOP_COLOR:
    case LINE_TRANSPORT_STOP_TIMING:
      ctx = ToolLedPreview::PV_EVENT_REPLAY;
      p.replayInst.patternId = PTN_FADE;
      p.replayInst.fgPct     = effectiveEventFgPct(EVT_STOP);
      p.replayInst.ledMask   = 0x08;
      p.replayInst.colorA    = resolveColorSlot(_cwk.slots[CSLOT_VERB_STOP]);
      p.replayInst.params.fade.durationMs = _lwk.holdOffFadeMs;
      p.replayInst.params.fade.startPct   = effectiveEventFgPct(EVT_STOP);
      p.replayInst.params.fade.endPct     = 0;
      p.effectDurationMs = _lwk.holdOffFadeMs;
      break;

    // --- Waiting quantise : crossfade mode color <-> PLAY ---
    case LINE_TRANSPORT_WAITING_COLOR:
      ctx = ToolLedPreview::PV_WAITING;
      // Source color = current mode color snapshot.
      if (isArpType(_setupEntryBankType)) {
        p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_ARPEG]);
      } else {
        p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_NORMAL]);
      }
      // Target = edited color (WAITING uses PLAY slot per spec).
      p.crossfadeTargetColor = resolveColorSlot(_cwk.slots[CSLOT_VERB_PLAY]);
      p.fgPct = 100;
      break;

    // --- Breathing ---
    case LINE_TRANSPORT_BREATHING:
      ctx = ToolLedPreview::PV_BREATHING;
      p.fgColor = resolveColorSlot(_cwk.slots[CSLOT_MODE_ARPEG]);
      p.breathMinPct   = _lwk.fgArpStopMin;
      p.breathMaxPct   = _lwk.fgArpStopMax;
      p.breathPeriodMs = _lwk.pulsePeriodMs;
      break;

    // --- Tick common / tick colors / tick durations : LOOP ticks mockup ---
    case LINE_TRANSPORT_TICK_COMMON:
    case LINE_TRANSPORT_TICK_PLAY_COLOR:
    case LINE_TRANSPORT_TICK_REC_COLOR:
    case LINE_TRANSPORT_TICK_OVERDUB_COLOR:
    case LINE_TRANSPORT_TICK_BEAT_DUR:
    case LINE_TRANSPORT_TICK_BAR_DUR:
    case LINE_TRANSPORT_TICK_WRAP_DUR:
      ctx = ToolLedPreview::PV_TICKS_MOCKUP;
      p.modeColorFg = resolveColorSlot(_cwk.slots[CSLOT_MODE_LOOP]);
      p.modeColorBg = p.modeColorFg;
      // Active tick color depends on the cursor line.
      switch (_cursor) {
        case LINE_TRANSPORT_TICK_REC_COLOR:
          p.tickColorActive = resolveColorSlot(_cwk.slots[CSLOT_VERB_REC]);
          p.activeTickKind  = 1;  // BAR
          break;
        case LINE_TRANSPORT_TICK_OVERDUB_COLOR:
          p.tickColorActive = resolveColorSlot(_cwk.slots[CSLOT_VERB_OVERDUB]);
          p.activeTickKind  = 2;  // WRAP
          break;
        case LINE_TRANSPORT_TICK_BAR_DUR:
          p.tickColorActive = resolveColorSlot(_cwk.slots[CSLOT_VERB_REC]);
          p.activeTickKind  = 1;
          break;
        case LINE_TRANSPORT_TICK_WRAP_DUR:
          p.tickColorActive = resolveColorSlot(_cwk.slots[CSLOT_VERB_OVERDUB]);
          p.activeTickKind  = 2;
          break;
        default:  // TICK_COMMON, TICK_PLAY_COLOR, TICK_BEAT_DUR
          p.tickColorActive = playColor;
          p.activeTickKind  = 0;  // BEAT
          break;
      }
      p.tickColorPlayBg   = playColor;
      p.tickBeatMs        = _lwk.tickBeatDurationMs;
      p.tickBarMs         = _lwk.tickBarDurationMs;
      p.tickWrapMs        = _lwk.tickWrapDurationMs;
      p.tickCommonFgPct   = _lwk.tickFlashFg;
      p.tickCommonBgPct   = _lwk.tickFlashBg;
      break;

    // --- Confirmations : event replay on LED 3 ---
    case LINE_CONFIRM_BANK_COLOR:
    case LINE_CONFIRM_BANK_TIMING:
      ctx = ToolLedPreview::PV_EVENT_REPLAY;
      p.replayInst.patternId = PTN_BLINK_SLOW;
      p.replayInst.fgPct     = _lwk.bankBrightnessPct;
      p.replayInst.ledMask   = 0x08;
      p.replayInst.colorA    = resolveColorSlot(_cwk.slots[CSLOT_BANK_SWITCH]);
      p.replayInst.params.blinkSlow.onMs   = _lwk.bankDurationMs / 2;
      p.replayInst.params.blinkSlow.offMs  = _lwk.bankDurationMs / 2;
      p.replayInst.params.blinkSlow.cycles = _lwk.bankBlinks;
      p.replayInst.params.blinkSlow.blackoutOff = 1;
      p.effectDurationMs = _lwk.bankDurationMs * _lwk.bankBlinks;
      break;
    case LINE_CONFIRM_SCALE_ROOT_COLOR:
    case LINE_CONFIRM_SCALE_ROOT_TIMING:
      ctx = ToolLedPreview::PV_EVENT_REPLAY;
      p.replayInst.patternId = PTN_BLINK_FAST;
      p.replayInst.fgPct     = effectiveEventFgPct(EVT_SCALE_ROOT);
      p.replayInst.ledMask   = 0x08;
      p.replayInst.colorA    = resolveColorSlot(_cwk.slots[CSLOT_SCALE_ROOT]);
      p.replayInst.params.blinkFast.onMs   = _lwk.scaleRootDurationMs / 2;
      p.replayInst.params.blinkFast.offMs  = _lwk.scaleRootDurationMs / 2;
      p.replayInst.params.blinkFast.cycles = _lwk.scaleRootBlinks;
      p.replayInst.params.blinkFast.blackoutOff = 1;
      p.effectDurationMs = _lwk.scaleRootDurationMs * _lwk.scaleRootBlinks;
      break;
    case LINE_CONFIRM_SCALE_MODE_COLOR:
    case LINE_CONFIRM_SCALE_MODE_TIMING:
      ctx = ToolLedPreview::PV_EVENT_REPLAY;
      p.replayInst.patternId = PTN_BLINK_FAST;
      p.replayInst.fgPct     = effectiveEventFgPct(EVT_SCALE_MODE);
      p.replayInst.ledMask   = 0x08;
      p.replayInst.colorA    = resolveColorSlot(_cwk.slots[CSLOT_SCALE_MODE]);
      p.replayInst.params.blinkFast.onMs   = _lwk.scaleModeDurationMs / 2;
      p.replayInst.params.blinkFast.offMs  = _lwk.scaleModeDurationMs / 2;
      p.replayInst.params.blinkFast.cycles = _lwk.scaleModeBlinks;
      p.replayInst.params.blinkFast.blackoutOff = 1;
      p.effectDurationMs = _lwk.scaleModeDurationMs * _lwk.scaleModeBlinks;
      break;
    case LINE_CONFIRM_SCALE_CHROM_COLOR:
    case LINE_CONFIRM_SCALE_CHROM_TIMING:
      ctx = ToolLedPreview::PV_EVENT_REPLAY;
      p.replayInst.patternId = PTN_BLINK_FAST;
      p.replayInst.fgPct     = effectiveEventFgPct(EVT_SCALE_CHROM);
      p.replayInst.ledMask   = 0x08;
      p.replayInst.colorA    = resolveColorSlot(_cwk.slots[CSLOT_SCALE_CHROM]);
      p.replayInst.params.blinkFast.onMs   = _lwk.scaleChromDurationMs / 2;
      p.replayInst.params.blinkFast.offMs  = _lwk.scaleChromDurationMs / 2;
      p.replayInst.params.blinkFast.cycles = _lwk.scaleChromBlinks;
      p.replayInst.params.blinkFast.blackoutOff = 1;
      p.effectDurationMs = _lwk.scaleChromDurationMs * _lwk.scaleChromBlinks;
      break;
    case LINE_CONFIRM_OCTAVE_COLOR:
    case LINE_CONFIRM_OCTAVE_TIMING:
      ctx = ToolLedPreview::PV_EVENT_REPLAY;
      p.replayInst.patternId = PTN_BLINK_FAST;
      p.replayInst.fgPct     = effectiveEventFgPct(EVT_OCTAVE);
      p.replayInst.ledMask   = 0x08;
      p.replayInst.colorA    = resolveColorSlot(_cwk.slots[CSLOT_OCTAVE]);
      p.replayInst.params.blinkFast.onMs   = _lwk.octaveDurationMs / 2;
      p.replayInst.params.blinkFast.offMs  = _lwk.octaveDurationMs / 2;
      p.replayInst.params.blinkFast.cycles = _lwk.octaveBlinks;
      p.replayInst.params.blinkFast.blackoutOff = 1;
      p.effectDurationMs = _lwk.octaveDurationMs * _lwk.octaveBlinks;
      break;
    case LINE_CONFIRM_OK_COLOR:
    case LINE_CONFIRM_OK_SPARK:
      ctx = ToolLedPreview::PV_EVENT_REPLAY;
      p.replayInst.patternId = PTN_SPARK;
      p.replayInst.fgPct     = 100;
      p.replayInst.ledMask   = 0x08;
      p.replayInst.colorA    = resolveColorSlot(_cwk.slots[CSLOT_CONFIRM_OK]);
      p.replayInst.params.spark.onMs   = _lwk.sparkOnMs;
      p.replayInst.params.spark.gapMs  = _lwk.sparkGapMs;
      p.replayInst.params.spark.cycles = _lwk.sparkCycles;
      p.effectDurationMs = (_lwk.sparkOnMs + _lwk.sparkGapMs) * _lwk.sparkCycles;
      break;

    // --- GLOBAL : BG factor / gamma — reuse TICKS_MOCKUP ---
    case LINE_GLOBAL_BG_FACTOR:
    case LINE_GLOBAL_GAMMA:
      ctx = (_cursor == LINE_GLOBAL_BG_FACTOR)
              ? ToolLedPreview::PV_BG_FACTOR
              : ToolLedPreview::PV_GAMMA_TEST;
      p.modeColorFg = resolveColorSlot(_cwk.slots[CSLOT_MODE_LOOP]);
      p.modeColorBg = p.modeColorFg;
      p.tickColorActive = playColor;
      p.tickColorPlayBg = playColor;
      p.activeTickKind  = 1;  // BAR cadence, decent pacing
      p.tickBeatMs = _lwk.tickBeatDurationMs;
      p.tickBarMs  = _lwk.tickBarDurationMs;
      p.tickWrapMs = _lwk.tickWrapDurationMs;
      p.tickCommonFgPct = _lwk.tickFlashFg;
      p.tickCommonBgPct = _lwk.tickFlashBg;
      break;

    default:
      break;
  }

  _preview.setContext(ctx, p);
}

// =================================================================
// Rendering
// =================================================================

void ToolLedSettings::drawLine(LineId line, bool isCursor, bool inEdit) {
  if (!_ui) return;
  const char* prefix = isCursor
                         ? (inEdit ? VT_YELLOW VT_BOLD "> " VT_RESET
                                   : VT_CYAN VT_BOLD "> " VT_RESET)
                         : "  ";
  // Label color : bright-red+bold in EDIT (visual "live value, handle with
  // care"), cyan+bold under cursor in NAV, plain otherwise.
  const char* lblPre = inEdit                 ? VT_BRIGHT_RED VT_BOLD
                       : isCursor             ? VT_CYAN VT_BOLD
                                              : "";
  const char* lblPost = (inEdit || isCursor)  ? VT_RESET : "";
  const char* lbl = LINE_LABELS[line];

  LineShape shape = shapeForLine(line);

  if (shape == SHAPE_COLOR) {
    const ColorSlot* s = ((ToolLedSettings*)this)->colorSlotForLine(line);
    const char* presetName = (s && s->presetId < COLOR_PRESET_COUNT)
                               ? COLOR_PRESET_NAMES[s->presetId] : "?";
    int hue = s ? s->hueOffset : 0;
    _ui->drawFrameLine("%s%s%-22s%s [%-12s] %+4d",
                       prefix, lblPre, lbl, lblPost, presetName, hue);
    return;
  }

  // Numeric lines : emit label + N values.
  uint8_t count = numericFieldCountForLine(line);
  char val[96];
  val[0] = 0;
  if (shape == SHAPE_SINGLE_NUM) {
    int32_t v = readNumericField(line, 0);
    // Unit hint by line (simple switch).
    const char* unit = "";
    switch (line) {
      case LINE_NORMAL_FG_PCT:
      case LINE_ARPEG_FG_PCT:
      case LINE_LOOP_FG_PCT:
      case LINE_GLOBAL_BG_FACTOR:          unit = "%"; break;
      case LINE_GLOBAL_GAMMA:
        snprintf(val, sizeof(val), "%d.%d", (int)(v/10), (int)(v%10));
        unit = "";
        _ui->drawFrameLine("%s%s%-22s%s %s", prefix, lblPre, lbl, lblPost, val);
        return;
      case LINE_LOOP_SAVE_DURATION:
      case LINE_LOOP_CLEAR_DURATION:
      case LINE_LOOP_SLOT_DURATION:
      case LINE_TRANSPORT_TICK_BEAT_DUR:
      case LINE_TRANSPORT_TICK_BAR_DUR:
      case LINE_TRANSPORT_TICK_WRAP_DUR:   unit = "ms"; break;
      default:                             unit = ""; break;
    }
    snprintf(val, sizeof(val), "%ld %s", (long)v, unit);
    _ui->drawFrameLine("%s%s%-22s%s %s", prefix, lblPre, lbl, lblPost, val);
    return;
  }

  // SHAPE_MULTI_NUM : render fields with focus marker on the active one.
  char f0[48]; char f1[48]; char f2[48];
  f0[0] = 0; f1[0] = 0; f2[0] = 0;
  auto fmtField = [](char* buf, size_t n, int32_t v, const char* name, const char* unit,
                      bool focus, bool editing) {
    if (focus && editing) {
      snprintf(buf, n, VT_YELLOW VT_BOLD "%s %ld%s" VT_RESET,
               name, (long)v, unit);
    } else {
      snprintf(buf, n, "%s %ld%s", name, (long)v, unit);
    }
  };
  // Field labels and units per line (consistent with descriptions).
  const char* n0 = ""; const char* u0 = "";
  const char* n1 = ""; const char* u1 = "";
  const char* n2 = ""; const char* u2 = "";
  switch (line) {
    case LINE_TRANSPORT_PLAY_TIMING:
    case LINE_TRANSPORT_STOP_TIMING:
      n0 = "brightness"; u0 = "%"; n1 = "duration"; u1 = "ms"; break;
    case LINE_TRANSPORT_BREATHING:
      n0 = "min"; u0 = "%"; n1 = "max"; u1 = "%"; n2 = "period"; u2 = "ms"; break;
    case LINE_TRANSPORT_TICK_COMMON:
      n0 = "FG"; u0 = "%"; n1 = "BG"; u1 = "%"; break;
    case LINE_CONFIRM_BANK_TIMING:
    case LINE_CONFIRM_SCALE_ROOT_TIMING:
    case LINE_CONFIRM_SCALE_MODE_TIMING:
    case LINE_CONFIRM_SCALE_CHROM_TIMING:
    case LINE_CONFIRM_OCTAVE_TIMING:
      n0 = "brightness"; u0 = "%"; n1 = "duration"; u1 = "ms"; break;
    case LINE_CONFIRM_OK_SPARK:
      n0 = "on"; u0 = "ms"; n1 = "gap"; u1 = "ms"; n2 = "cycles"; u2 = ""; break;
    default: break;
  }
  fmtField(f0, sizeof(f0), readNumericField(line, 0), n0, u0,
           inEdit && _editFocus == 0, inEdit);
  fmtField(f1, sizeof(f1), readNumericField(line, 1), n1, u1,
           inEdit && _editFocus == 1, inEdit);
  if (count >= 3) {
    fmtField(f2, sizeof(f2), readNumericField(line, 2), n2, u2,
             inEdit && _editFocus == 2, inEdit);
    _ui->drawFrameLine("%s%s%-22s%s %s   %s   %s",
                       prefix, lblPre, lbl, lblPost, f0, f1, f2);
  } else {
    _ui->drawFrameLine("%s%s%-22s%s %s   %s",
                       prefix, lblPre, lbl, lblPost, f0, f1);
  }
}

const char* ToolLedSettings::descriptionForLine(LineId line, bool /*inEdit*/) const {
  if (line >= LINE_COUNT) return "";
  return LINE_DESCRIPTIONS[line];
}

void ToolLedSettings::drawDescriptionPanel() {
  if (!_ui) return;
  _ui->drawFrameEmpty();
  const char* txt = descriptionForLine(_cursor, _uiMode == UI_EDIT);
  _ui->drawFrameLine(VT_DIM "%s" VT_RESET, txt);
}

void ToolLedSettings::drawScreen() {
  if (!_ui) return;
  _ui->vtFrameStart();
  _ui->vtHome();
  _ui->drawConsoleHeader("TOOL 8: LED SETTINGS", _nvsSaved);
  _ui->drawFrameTop();

  // Scroll indicator (top) : yellow+bold, centered (~CONSOLE_INNER/2 leading
  // spaces). Shown only when content exists above viewport.
  if (_viewportStart > 0) {
    _ui->drawFrameLine("                                             "
                       VT_BRIGHT_YELLOW VT_BOLD
                       "\xe2\x96\xb2 %u more lines above"
                       VT_RESET,
                       (unsigned)_viewportStart);
  }

  // Render the viewport : VIEWPORT_SIZE lines starting at _viewportStart.
  // Emit a section title whenever the viewport crosses a section boundary.
  Section lastSection = (Section)0xFF;
  uint8_t end = _viewportStart + VIEWPORT_SIZE;
  if (end > LINE_COUNT) end = LINE_COUNT;
  for (uint8_t i = _viewportStart; i < end; i++) {
    LineId line = (LineId)i;
    Section s = sectionOf(line);
    if (s != lastSection) {
      _ui->drawSection(SECTION_LABELS[s]);
      lastSection = s;
    }
    drawLine(line, line == _cursor, _uiMode == UI_EDIT && line == _cursor);
  }

  // Scroll indicator (bottom) : yellow+bold, centered.
  if (end < LINE_COUNT) {
    _ui->drawFrameLine("                                             "
                       VT_BRIGHT_YELLOW VT_BOLD
                       "\xe2\x96\xbc %u more lines below"
                       VT_RESET,
                       (unsigned)(LINE_COUNT - end));
  }

  drawDescriptionPanel();
  _ui->drawFrameBottom();

  if (_uiMode == UI_EDIT) {
    LineShape sh = shapeForLine(_cursor);
    switch (sh) {
      case SHAPE_COLOR:
        _ui->drawControlBar(VT_DIM "[<>] preset  [^v] hue  [ENTER] save  [q] cancel" VT_RESET);
        break;
      case SHAPE_SINGLE_NUM:
        _ui->drawControlBar(VT_DIM "[<>] +/-10  [^v] +/-1  [ENTER] save  [q] cancel" VT_RESET);
        break;
      case SHAPE_MULTI_NUM:
        _ui->drawControlBar(VT_DIM "[<>] focus  [^v] adjust  [ENTER] save  [q] cancel" VT_RESET);
        break;
    }
  } else {
    _ui->drawControlBar(VT_DIM "[^v] nav  [<>] section  [ENTER] edit  [d] default  [q] exit" VT_RESET);
  }
  _ui->vtFrameEnd();
}

// =================================================================
// Main run loop — follows setup-tools-conventions §10 skeleton.
// =================================================================

void ToolLedSettings::run() {
  if (!_leds || !_ui) return;

  loadAll();
  refreshBadge();
  _cursor = (LineId)0;
  _uiMode = UI_NAV;
  _editFocus = 0;
  _viewportStart = 0;

  _ui->vtClear();

  // Preview : take LED strip ownership, init helper.
  _leds->previewBegin();
  uint16_t tempoBpm = _potRouter ? _potRouter->getTempoBPM() : 120;
  _preview.begin(_leds, tempoBpm);
  updatePreviewContext();

  static InputParser sInput;

  bool screenDirty = true;

  while (true) {
    _leds->update();
    unsigned long now = millis();
    _preview.update(now);

    NavEvent ev = sInput.update();
    UiMode modeAtStart = _uiMode;

    switch (ev.type) {
      case NAV_UP:
        if (_uiMode == UI_NAV) { cursorUp(); updatePreviewContext(); screenDirty = true; }
        else {
          LineShape sh = shapeForLine(_cursor);
          if (sh == SHAPE_COLOR)            editColor(_cursor, 0, +1, ev.accelerated);
          else if (sh == SHAPE_SINGLE_NUM)  editSingleNumeric(_cursor, 0, +1, ev.accelerated);
          else                              editMultiNumeric(_cursor, 0, +1, ev.accelerated);
          screenDirty = true;
        }
        break;
      case NAV_DOWN:
        if (_uiMode == UI_NAV) { cursorDown(); updatePreviewContext(); screenDirty = true; }
        else {
          LineShape sh = shapeForLine(_cursor);
          if (sh == SHAPE_COLOR)            editColor(_cursor, 0, -1, ev.accelerated);
          else if (sh == SHAPE_SINGLE_NUM)  editSingleNumeric(_cursor, 0, -1, ev.accelerated);
          else                              editMultiNumeric(_cursor, 0, -1, ev.accelerated);
          screenDirty = true;
        }
        break;
      case NAV_LEFT:
        if (_uiMode == UI_NAV) { cursorPrevSection(); updatePreviewContext(); screenDirty = true; }
        else {
          LineShape sh = shapeForLine(_cursor);
          if (sh == SHAPE_COLOR)            editColor(_cursor, -1, 0, ev.accelerated);
          else if (sh == SHAPE_SINGLE_NUM)  editSingleNumeric(_cursor, -1, 0, ev.accelerated);
          else                              editMultiNumeric(_cursor, -1, 0, ev.accelerated);
          screenDirty = true;
        }
        break;
      case NAV_RIGHT:
        if (_uiMode == UI_NAV) { cursorNextSection(); updatePreviewContext(); screenDirty = true; }
        else {
          LineShape sh = shapeForLine(_cursor);
          if (sh == SHAPE_COLOR)            editColor(_cursor, +1, 0, ev.accelerated);
          else if (sh == SHAPE_SINGLE_NUM)  editSingleNumeric(_cursor, +1, 0, ev.accelerated);
          else                              editMultiNumeric(_cursor, +1, 0, ev.accelerated);
          screenDirty = true;
        }
        break;
      case NAV_ENTER:
        if (_uiMode == UI_NAV) { enterEdit(); screenDirty = true; }
        else                   { commitEdit(); screenDirty = true; }
        break;
      case NAV_DEFAULTS:
        if (_uiMode == UI_NAV) { resetLineDefault(); screenDirty = true; }
        break;
      case NAV_QUIT:
        if (_uiMode == UI_EDIT) { cancelEdit(); screenDirty = true; }
        break;
      default: break;
    }

    // Exit on q at top level (two-step pattern).
    if (ev.type == NAV_QUIT && modeAtStart == UI_NAV) {
      _preview.end();
      _leds->previewEnd();
      _ui->vtClear();
      return;
    }

    if (screenDirty) {
      drawScreen();
      screenDirty = false;
    }

    delay(5);
  }
}
