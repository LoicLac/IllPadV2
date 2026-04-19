#include "ToolLedSettings.h"
#include "SetupUI.h"
#include "InputParser.h"
#include "../core/LedController.h"
#include "../core/LedGrammar.h"
#include "../core/HardwareConfig.h"
#include "../managers/NvsManager.h"
#include <Arduino.h>

// =================================================================
// Tool 8 — LED Settings (Phase 0 step 0.8e — polish complete)
// =================================================================
// Three-page editor for the unified LED grammar :
//   COLORS   : 15 color slots (preset + hue offset)   [§0.8b]
//   PATTERNS : 9 palette patterns, 4 with editable globals  [§0.8c]
//   EVENTS   : 10 Phase 0 events, per-event {pattern, color, fgPct}
//              override ; PTN_NONE sentinel = fallback on
//              EVENT_RENDER_DEFAULT                      [§0.8d]
//
// Conventions respected :
//   §1 Save on commit only. ENTER commits, 'q' cancels (no save).
//      'd' on an item resets it to default, which IS a commit (saves).
//   §2 One flashSaved() per commit — audited via grep.
//   §3 LED 3-4 driven by the tool in preview mode (COLORS page).
//   §4 3 canonical paradigms : grid-like cursor list (NAV), pool
//      (pattern / color / preset cycling via LEFT/RIGHT), field editor
//      (numeric adjustments with accel).
//   §6 't' toggles pages ; arrows strictly intra-page.
//      Two-step exit : 'q' in sub-state cancels edit, 'q' in NAV exits tool.
// =================================================================

// Short labels (max 16 chars) for each color slot — indexed by ColorSlotId.
static const char* const COLOR_SLOT_LABELS[COLOR_SLOT_COUNT] = {
  "MODE_NORMAL",      // 0
  "MODE_ARPEG",       // 1
  "MODE_LOOP",        // 2
  "VERB_PLAY",        // 3
  "VERB_REC",         // 4
  "VERB_OVERDUB",     // 5
  "VERB_CLEAR_LOOP",  // 6
  "VERB_SLOT_CLEAR",  // 7
  "VERB_SAVE",        // 8
  "BANK_SWITCH",      // 9
  "SCALE_ROOT",       // 10
  "SCALE_MODE",       // 11
  "SCALE_CHROM",      // 12
  "OCTAVE",           // 13
  "CONFIRM_OK",       // 14
};

// Default preset per slot (mirrors NvsManager::_colorSlots init).
static const uint8_t DEFAULT_SLOT_PRESETS[COLOR_SLOT_COUNT] = {
  1, 3, 7, 11, 8, 6, 5, 6, 10, 0, 6, 7, 8, 9, 0
};
// Default hue offset per slot (VERB_SLOT_CLEAR = +20 to distinguish from VERB_OVERDUB).
static const int8_t DEFAULT_SLOT_HUES[COLOR_SLOT_COUNT] = {
  0, 0, 0, 0, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0
};

// Phase 0 events (Tool 8 PAGE_EVENTS shows these, hides LOOP reserved).
// Index is the EventId ; the EVENTS page cursor traverses this array in
// order.
static const uint8_t PHASE0_EVENT_IDS[] = {
  EVT_BANK_SWITCH,
  EVT_SCALE_ROOT,
  EVT_SCALE_MODE,
  EVT_SCALE_CHROM,
  EVT_OCTAVE,
  EVT_PLAY,
  EVT_STOP,
  EVT_WAITING,
  EVT_REFUSE,
  EVT_CONFIRM_OK,
};
static const uint8_t NUM_PHASE0_EVENTS = sizeof(PHASE0_EVENT_IDS);

static const char* const PHASE0_EVENT_LABELS[NUM_PHASE0_EVENTS] = {
  "BANK_SWITCH",
  "SCALE_ROOT",
  "SCALE_MODE",
  "SCALE_CHROM",
  "OCTAVE",
  "PLAY",
  "STOP",
  "WAITING",
  "REFUSE",
  "CONFIRM_OK",
};

// Pattern pool labels (short, aligned with LED spec §10 palette).
static const char* const PATTERN_POOL_LABELS[PTN_COUNT] = {
  "SOLID",            // 0
  "PULSE_SLOW",       // 1
  "CROSSFADE_COLOR",  // 2
  "BLINK_SLOW",       // 3
  "BLINK_FAST",       // 4
  "FADE",             // 5
  "FLASH",            // 6
  "RAMP_HOLD",        // 7
  "SPARK",            // 8
};

ToolLedSettings::ToolLedSettings()
  : _leds(nullptr), _ui(nullptr), _page(PAGE_COLORS),
    _nvsSaved(false),
    _cwk{},
    _colorsCursor(0),
    _colorsSub(COLORS_NAV),
    _colorsEditField(EDIT_FIELD_PRESET),
    _colorsEditBackup{},
    _lwk{},
    _patternsCursor(0),
    _patternsSub(PATTERNS_NAV),
    _patternsEditField(0),
    _patternsEditBackup{},
    _eventsCursor(0),
    _eventsSub(EVENTS_NAV),
    _eventsEditField(EVT_FIELD_PATTERN),
    _eventsEditBackup{} {}

void ToolLedSettings::begin(LedController* leds, SetupUI* ui) {
  _leds = leds;
  _ui = ui;
}

// --- COLORS page ---

void ToolLedSettings::previewCurrentColor() {
  // Show the currently-selected slot's color on LED 3-4 (convention §3.2
  // for tool-driven preview). Uses resolveColorSlot() so hue rotation is
  // reflected live as the user adjusts hueOffset.
  if (!_leds) return;
  if (_colorsCursor >= COLOR_SLOT_COUNT) return;
  RGBW color = resolveColorSlot(_cwk.slots[_colorsCursor]);
  _leds->previewClear();
  _leds->previewSetPixel(3, color, 100);
  _leds->previewSetPixel(4, color, 100);
  _leds->previewShow();
}

bool ToolLedSettings::saveColorSlots() {
  _cwk.magic    = COLOR_SLOT_MAGIC;
  _cwk.version  = COLOR_SLOT_VERSION;
  _cwk.reserved = 0;
  if (!NvsManager::saveBlob(LED_SETTINGS_NVS_NAMESPACE, COLOR_SLOT_NVS_KEY,
                             &_cwk, sizeof(_cwk))) {
    return false;
  }
  if (_leds) _leds->loadColorSlots(_cwk);  // refresh cached _colors[]
  return true;
}

void ToolLedSettings::resetCurrentColorToDefault() {
  if (_colorsCursor >= COLOR_SLOT_COUNT) return;
  _cwk.slots[_colorsCursor].presetId  = DEFAULT_SLOT_PRESETS[_colorsCursor];
  _cwk.slots[_colorsCursor].hueOffset = DEFAULT_SLOT_HUES[_colorsCursor];
}

void ToolLedSettings::handlePageColors(const NavEvent& ev, bool& screenDirty) {
  if (_colorsSub == COLORS_NAV) {
    // ----- Grid navigation -----
    if (ev.type == NAV_UP) {
      _colorsCursor = (_colorsCursor == 0) ? (COLOR_SLOT_COUNT - 1)
                                            : (_colorsCursor - 1);
      screenDirty = true;
    } else if (ev.type == NAV_DOWN) {
      _colorsCursor = (_colorsCursor + 1) % COLOR_SLOT_COUNT;
      screenDirty = true;
    } else if (ev.type == NAV_ENTER) {
      _colorsSub        = COLORS_EDIT;
      _colorsEditField  = EDIT_FIELD_PRESET;
      _colorsEditBackup = _cwk.slots[_colorsCursor];  // for 'q' cancel
      screenDirty       = true;
    } else if (ev.type == NAV_DEFAULTS) {
      resetCurrentColorToDefault();
      if (saveColorSlots()) {
        _nvsSaved = true;
        _ui->flashSaved();
      }
      screenDirty = true;
    }
  } else {
    // ----- Edit sub-state : preset pool + hue field -----
    if (ev.type == NAV_UP || ev.type == NAV_DOWN) {
      // Toggle focused field
      _colorsEditField = (_colorsEditField == EDIT_FIELD_PRESET)
                          ? EDIT_FIELD_HUE : EDIT_FIELD_PRESET;
      screenDirty = true;
    } else if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
      int dir = (ev.type == NAV_RIGHT) ? 1 : -1;
      ColorSlot& s = _cwk.slots[_colorsCursor];
      if (_colorsEditField == EDIT_FIELD_PRESET) {
        // Pool : cycle through 14 presets
        int p = (int)s.presetId + dir;
        if (p < 0) p = COLOR_PRESET_COUNT - 1;
        if (p >= (int)COLOR_PRESET_COUNT) p = 0;
        s.presetId = (uint8_t)p;
      } else {
        // Field editor : adjust hue offset, accelerated step of 10
        int step = ev.accelerated ? 10 : 1;
        int h = (int)s.hueOffset + dir * step;
        if (h < -128) h = -128;
        if (h >  127) h =  127;
        s.hueOffset = (int8_t)h;
      }
      screenDirty = true;
    } else if (ev.type == NAV_ENTER) {
      // Commit + save on ENTER (conventions §1 : save on commit only)
      if (saveColorSlots()) {
        _nvsSaved = true;
        _ui->flashSaved();
      }
      _colorsSub = COLORS_NAV;  // back to grid
      screenDirty = true;
    } else if (ev.type == NAV_QUIT) {
      // Cancel : restore backup, back to grid (consumed by outer loop too,
      // but we handle it first via two-step exit convention §6.4).
      _cwk.slots[_colorsCursor] = _colorsEditBackup;
      _colorsSub = COLORS_NAV;
      screenDirty = true;
    }
  }
}

void ToolLedSettings::renderPageColors() {
  _ui->drawSection("COLOR SLOTS");
  for (uint8_t i = 0; i < COLOR_SLOT_COUNT; i++) {
    bool selected = (i == _colorsCursor);
    bool editing  = selected && (_colorsSub == COLORS_EDIT);

    const ColorSlot& s = _cwk.slots[i];
    const char* presetName = (s.presetId < COLOR_PRESET_COUNT)
                              ? COLOR_PRESET_NAMES[s.presetId] : "?";

    if (editing) {
      const char* presetFocus = (_colorsEditField == EDIT_FIELD_PRESET) ? VT_CYAN VT_BOLD : "";
      const char* hueFocus    = (_colorsEditField == EDIT_FIELD_HUE)    ? VT_CYAN VT_BOLD : "";
      _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-16s  %s[%s]" VT_RESET "  hue %s%+d" VT_RESET,
                         COLOR_SLOT_LABELS[i], presetFocus, presetName, hueFocus, s.hueOffset);
    } else if (selected) {
      _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-16s  " VT_BRIGHT_WHITE "[%s]" VT_RESET "  hue %+d",
                         COLOR_SLOT_LABELS[i], presetName, s.hueOffset);
    } else {
      _ui->drawFrameLine("  %-16s  [%s]  hue %+d",
                         COLOR_SLOT_LABELS[i], presetName, s.hueOffset);
    }
  }
  _ui->drawFrameEmpty();
  _ui->drawSection("INFO");
  if (_colorsSub == COLORS_EDIT) {
    _ui->drawFrameLine(VT_DIM "Editing %s — %s focused." VT_RESET,
                       COLOR_SLOT_LABELS[_colorsCursor],
                       (_colorsEditField == EDIT_FIELD_PRESET) ? "PRESET" : "HUE");
    _ui->drawFrameLine(VT_DIM "UP/DOWN : switch field. LEFT/RIGHT : adjust." VT_RESET);
    _ui->drawFrameLine(VT_DIM "ENTER : save & exit edit. 'q' : cancel." VT_RESET);
  } else {
    _ui->drawFrameLine(VT_DIM "%s" VT_RESET, COLOR_SLOT_LABELS[_colorsCursor]);
    _ui->drawFrameLine(VT_DIM "Preview shown on LED 3-4. Choose preset + hue offset" VT_RESET);
    _ui->drawFrameLine(VT_DIM "via ENTER. 'd' resets this slot to its default." VT_RESET);
  }
}

// --- PATTERNS page ---
//
// Pool of 9 patterns (selected via UP/DOWN). Some patterns have editable
// global params stored in LedSettingsStore ; others are entirely
// event-specific (edit via EVENTS page in 0.8d) or not-yet-configurable.
//
// Global params map :
//   PTN_SOLID            : none (per-event)
//   PTN_PULSE_SLOW       : 1 field  (pulsePeriodMs)
//   PTN_CROSSFADE_COLOR  : none (not yet a store field)
//   PTN_BLINK_SLOW       : none (per-event)
//   PTN_BLINK_FAST       : none (per-event)
//   PTN_FADE             : none (per-event)
//   PTN_FLASH            : 3 fields (tickFlashDurationMs, tickFlashFg, tickFlashBg)
//   PTN_RAMP_HOLD        : 3 fields (sparkOnMs, sparkGapMs, sparkCycles — shared with SPARK)
//   PTN_SPARK            : 3 fields (sparkOnMs, sparkGapMs, sparkCycles — shared with RAMP_HOLD)

bool ToolLedSettings::saveLedSettings() {
  _lwk.magic    = EEPROM_MAGIC;
  _lwk.version  = LED_SETTINGS_VERSION;
  _lwk.reserved = 0;
  validateLedSettingsStore(_lwk);
  if (!NvsManager::saveBlob(LED_SETTINGS_NVS_NAMESPACE, LED_SETTINGS_NVS_KEY,
                             &_lwk, sizeof(_lwk))) {
    return false;
  }
  if (_leds) _leds->loadLedSettings(_lwk);  // push to runtime cache
  return true;
}

uint8_t ToolLedSettings::patternEditFieldCount(uint8_t patternId) const {
  switch (patternId) {
    case PTN_PULSE_SLOW:  return 1;
    case PTN_FLASH:       return 3;
    case PTN_RAMP_HOLD:   return 3;  // suffix = spark
    case PTN_SPARK:       return 3;
    default:              return 0;
  }
}

void ToolLedSettings::adjustPatternField(uint8_t patternId, uint8_t field, int dir, bool accel) {
  switch (patternId) {
    case PTN_PULSE_SLOW: {
      // field 0 : pulsePeriodMs (500-4000 ms, step 100 / accel 500)
      int step = accel ? 500 : 100;
      int v = (int)_lwk.pulsePeriodMs + dir * step;
      if (v < 500) v = 500;
      if (v > 4000) v = 4000;
      _lwk.pulsePeriodMs = (uint16_t)v;
      break;
    }
    case PTN_FLASH: {
      if (field == 0) {
        int step = accel ? 10 : 5;
        int v = (int)_lwk.tickFlashDurationMs + dir * step;
        if (v < 10) v = 10;
        if (v > 100) v = 100;
        _lwk.tickFlashDurationMs = (uint8_t)v;
      } else if (field == 1) {
        int v = (int)_lwk.tickFlashFg + dir * (accel ? 10 : 1);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        _lwk.tickFlashFg = (uint8_t)v;
      } else if (field == 2) {
        int v = (int)_lwk.tickFlashBg + dir * (accel ? 10 : 1);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        _lwk.tickFlashBg = (uint8_t)v;
      }
      break;
    }
    case PTN_RAMP_HOLD:
    case PTN_SPARK: {
      // Both edit the same sparkOnMs / sparkGapMs / sparkCycles fields
      // (RAMP_HOLD uses them for its suffix).
      if (field == 0) {
        int step = accel ? 10 : 5;
        int v = (int)_lwk.sparkOnMs + dir * step;
        if (v < 20) v = 20;
        if (v > 200) v = 200;
        _lwk.sparkOnMs = (uint16_t)v;
      } else if (field == 1) {
        int step = accel ? 10 : 5;
        int v = (int)_lwk.sparkGapMs + dir * step;
        if (v < 20) v = 20;
        if (v > 300) v = 300;
        _lwk.sparkGapMs = (uint16_t)v;
      } else if (field == 2) {
        int v = (int)_lwk.sparkCycles + dir;
        if (v < 1) v = 1;
        if (v > 4) v = 4;
        _lwk.sparkCycles = (uint8_t)v;
      }
      break;
    }
    default: break;
  }
}

void ToolLedSettings::renderPatternParamsPanel(uint8_t patternId, bool editing) const {
  auto fieldLine = [&](uint8_t fieldIdx, const char* label, const char* value) {
    bool focus = editing && (_patternsEditField == fieldIdx);
    if (focus) {
      _ui->drawFrameLine(VT_CYAN VT_BOLD "  > %-20s" VT_RESET VT_CYAN "[%s]" VT_RESET, label, value);
    } else {
      _ui->drawFrameLine("    %-20s%s", label, value);
    }
  };
  char buf[24];

  switch (patternId) {
    case PTN_PULSE_SLOW:
      snprintf(buf, sizeof(buf), "%d ms  (500-4000)", _lwk.pulsePeriodMs);
      fieldLine(0, "periodMs:", buf);
      break;
    case PTN_FLASH:
      snprintf(buf, sizeof(buf), "%d ms  (10-100)", _lwk.tickFlashDurationMs);
      fieldLine(0, "durationMs:", buf);
      snprintf(buf, sizeof(buf), "%d %%  (0-100)", _lwk.tickFlashFg);
      fieldLine(1, "fgPct:", buf);
      snprintf(buf, sizeof(buf), "%d %%  (0-100)", _lwk.tickFlashBg);
      fieldLine(2, "bgPct:", buf);
      break;
    case PTN_RAMP_HOLD:
      _ui->drawFrameLine(VT_DIM "    rampMs:             derived from Tool 6 timers" VT_RESET);
      // Fallthrough to SPARK : suffix params
      snprintf(buf, sizeof(buf), "%d ms  (20-200)", _lwk.sparkOnMs);
      fieldLine(0, "suffix onMs:", buf);
      snprintf(buf, sizeof(buf), "%d ms  (20-300)", _lwk.sparkGapMs);
      fieldLine(1, "suffix gapMs:", buf);
      snprintf(buf, sizeof(buf), "%d  (1-4)", _lwk.sparkCycles);
      fieldLine(2, "suffix cycles:", buf);
      break;
    case PTN_SPARK:
      snprintf(buf, sizeof(buf), "%d ms  (20-200)", _lwk.sparkOnMs);
      fieldLine(0, "onMs:", buf);
      snprintf(buf, sizeof(buf), "%d ms  (20-300)", _lwk.sparkGapMs);
      fieldLine(1, "gapMs:", buf);
      snprintf(buf, sizeof(buf), "%d  (1-4)", _lwk.sparkCycles);
      fieldLine(2, "cycles:", buf);
      break;
    default:
      _ui->drawFrameLine(VT_DIM "    No global params. Edit via EVENTS page (0.8d)." VT_RESET);
      break;
  }
}

void ToolLedSettings::handlePagePatterns(const NavEvent& ev, bool& screenDirty) {
  if (_patternsSub == PATTERNS_NAV) {
    // Pool navigation over 9 patterns
    if (ev.type == NAV_UP) {
      _patternsCursor = (_patternsCursor == 0) ? (PTN_COUNT - 1) : (_patternsCursor - 1);
      screenDirty = true;
    } else if (ev.type == NAV_DOWN) {
      _patternsCursor = (_patternsCursor + 1) % PTN_COUNT;
      screenDirty = true;
    } else if (ev.type == NAV_ENTER) {
      if (patternEditFieldCount(_patternsCursor) > 0) {
        _patternsSub         = PATTERNS_EDIT;
        _patternsEditField   = 0;
        _patternsEditBackup  = _lwk;  // backup full store for cancel
        screenDirty          = true;
      }
      // else : pattern has no editable globals, ENTER is a no-op (user sees
      // the "No global params" message in the panel).
    }
  } else {
    // PATTERNS_EDIT : field editor over N params of selected pattern
    uint8_t count = patternEditFieldCount(_patternsCursor);
    if (count == 0) {
      // Should not happen (we only enter EDIT if count > 0) but handle safely
      _patternsSub = PATTERNS_NAV;
      screenDirty = true;
      return;
    }
    if (ev.type == NAV_UP) {
      _patternsEditField = (_patternsEditField == 0) ? (count - 1) : (_patternsEditField - 1);
      screenDirty = true;
    } else if (ev.type == NAV_DOWN) {
      _patternsEditField = (_patternsEditField + 1) % count;
      screenDirty = true;
    } else if (ev.type == NAV_LEFT) {
      adjustPatternField(_patternsCursor, _patternsEditField, -1, ev.accelerated);
      screenDirty = true;
    } else if (ev.type == NAV_RIGHT) {
      adjustPatternField(_patternsCursor, _patternsEditField, +1, ev.accelerated);
      screenDirty = true;
    } else if (ev.type == NAV_ENTER) {
      if (saveLedSettings()) {
        _nvsSaved = true;
        _ui->flashSaved();
      }
      _patternsSub = PATTERNS_NAV;
      screenDirty = true;
    } else if (ev.type == NAV_QUIT) {
      // Cancel : restore backup, back to NAV
      _lwk = _patternsEditBackup;
      _patternsSub = PATTERNS_NAV;
      screenDirty = true;
    }
  }
}

void ToolLedSettings::renderPagePatterns() {
  _ui->drawSection("PATTERNS");
  for (uint8_t i = 0; i < PTN_COUNT; i++) {
    bool selected = (i == _patternsCursor);
    bool editing  = selected && (_patternsSub == PATTERNS_EDIT);
    uint8_t fieldCount = patternEditFieldCount(i);

    char hint[32];
    if (fieldCount > 0) snprintf(hint, sizeof(hint), "%d global", fieldCount);
    else                snprintf(hint, sizeof(hint), "per-event (see EVENTS)");

    if (editing) {
      _ui->drawFrameLine(VT_CYAN VT_BOLD "> %-18s" VT_RESET VT_DIM " [editing]" VT_RESET, PATTERN_POOL_LABELS[i]);
    } else if (selected) {
      _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-18s" VT_DIM " %s" VT_RESET, PATTERN_POOL_LABELS[i], hint);
    } else {
      _ui->drawFrameLine("  %-18s" VT_DIM " %s" VT_RESET, PATTERN_POOL_LABELS[i], hint);
    }
  }
  _ui->drawFrameEmpty();

  _ui->drawSection(_patternsSub == PATTERNS_EDIT ? "EDITING" : "PARAMS");
  renderPatternParamsPanel(_patternsCursor, _patternsSub == PATTERNS_EDIT);
}

// --- EVENTS page ---
//
// Vertical field list of Phase 0 events (LOOP reserved events hidden
// until Phase 1+). Each row shows the event name + 3 fields :
//   - pattern   (pool : 9 patterns + PTN_NONE "default")
//   - color     (pool : 15 color slots)
//   - fgPct     (numeric, 0-100)
//
// Sentinel PTN_NONE means "use EVENT_RENDER_DEFAULT[evt]" — the
// compile-time fallback takes over. Any other PatternId value is a
// user override stored in LedSettingsStore.eventOverrides[].

void ToolLedSettings::adjustEventField(uint8_t eventIdx, uint8_t field, int dir, bool accel) {
  if (eventIdx >= NUM_PHASE0_EVENTS) return;
  uint8_t evt = PHASE0_EVENT_IDS[eventIdx];
  EventRenderEntry& entry = _lwk.eventOverrides[evt];

  if (field == EVT_FIELD_PATTERN) {
    // Cycle through 9 patterns + PTN_NONE (default) = 10 options.
    // Storage : 0..8 are PatternId, 0xFF is PTN_NONE.
    // We map them to a linear cursor : 0..8 = patterns, 9 = default.
    uint8_t cur = (entry.patternId == PTN_NONE) ? 9 : entry.patternId;
    int n = (int)cur + dir;
    if (n < 0)  n = 9;
    if (n > 9)  n = 0;
    entry.patternId = (n == 9) ? PTN_NONE : (uint8_t)n;
  } else if (field == EVT_FIELD_COLOR) {
    int c = (int)entry.colorSlot + dir;
    if (c < 0) c = COLOR_SLOT_COUNT - 1;
    if (c >= (int)COLOR_SLOT_COUNT) c = 0;
    entry.colorSlot = (uint8_t)c;
  } else if (field == EVT_FIELD_FG_PCT) {
    int step = accel ? 10 : 1;
    int v = (int)entry.fgPct + dir * step;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    entry.fgPct = (uint8_t)v;
  }
}

void ToolLedSettings::resetCurrentEventToDefault() {
  if (_eventsCursor >= NUM_PHASE0_EVENTS) return;
  uint8_t evt = PHASE0_EVENT_IDS[_eventsCursor];
  _lwk.eventOverrides[evt].patternId = PTN_NONE;  // fallback active
  _lwk.eventOverrides[evt].colorSlot = 0;
  _lwk.eventOverrides[evt].fgPct     = 0;
}

void ToolLedSettings::handlePageEvents(const NavEvent& ev, bool& screenDirty) {
  if (_eventsSub == EVENTS_NAV) {
    if (ev.type == NAV_UP) {
      _eventsCursor = (_eventsCursor == 0) ? (NUM_PHASE0_EVENTS - 1) : (_eventsCursor - 1);
      screenDirty = true;
    } else if (ev.type == NAV_DOWN) {
      _eventsCursor = (_eventsCursor + 1) % NUM_PHASE0_EVENTS;
      screenDirty = true;
    } else if (ev.type == NAV_ENTER) {
      _eventsSub        = EVENTS_EDIT;
      _eventsEditField  = EVT_FIELD_PATTERN;
      uint8_t evt       = PHASE0_EVENT_IDS[_eventsCursor];
      _eventsEditBackup = _lwk.eventOverrides[evt];
      screenDirty       = true;
    } else if (ev.type == NAV_DEFAULTS) {
      resetCurrentEventToDefault();
      if (saveLedSettings()) {
        _nvsSaved = true;
        _ui->flashSaved();
      }
      screenDirty = true;
    }
  } else {
    // EVENTS_EDIT
    if (ev.type == NAV_UP || ev.type == NAV_DOWN) {
      // Cycle focused field among 3
      int n = (int)_eventsEditField + (ev.type == NAV_DOWN ? 1 : -1);
      if (n < 0) n = 2;
      if (n > 2) n = 0;
      _eventsEditField = (EventsEditField)n;
      screenDirty = true;
    } else if (ev.type == NAV_LEFT) {
      adjustEventField(_eventsCursor, _eventsEditField, -1, ev.accelerated);
      screenDirty = true;
    } else if (ev.type == NAV_RIGHT) {
      adjustEventField(_eventsCursor, _eventsEditField, +1, ev.accelerated);
      screenDirty = true;
    } else if (ev.type == NAV_ENTER) {
      if (saveLedSettings()) {
        _nvsSaved = true;
        _ui->flashSaved();
      }
      _eventsSub = EVENTS_NAV;
      screenDirty = true;
    } else if (ev.type == NAV_QUIT) {
      // Cancel : restore backup for this event only
      uint8_t evt = PHASE0_EVENT_IDS[_eventsCursor];
      _lwk.eventOverrides[evt] = _eventsEditBackup;
      _eventsSub = EVENTS_NAV;
      screenDirty = true;
    }
  }
}

void ToolLedSettings::renderPageEvents() {
  _ui->drawSection("EVENTS");
  for (uint8_t i = 0; i < NUM_PHASE0_EVENTS; i++) {
    bool selected = (i == _eventsCursor);
    bool editing  = selected && (_eventsSub == EVENTS_EDIT);

    uint8_t evt = PHASE0_EVENT_IDS[i];
    const EventRenderEntry& entry = _lwk.eventOverrides[evt];

    // Derive visible pattern/color/fgPct : if override is PTN_NONE,
    // show the default values (informational) but tag as "(default)".
    bool isOverride = (entry.patternId != PTN_NONE);
    const EventRenderEntry& effective = isOverride ? entry : EVENT_RENDER_DEFAULT[evt];

    const char* patLabel = (entry.patternId == PTN_NONE)
                             ? "default"
                             : (effective.patternId < PTN_COUNT
                                ? PATTERN_POOL_LABELS[effective.patternId]
                                : "?");
    const char* colLabel = (effective.colorSlot < COLOR_SLOT_COUNT)
                             ? COLOR_SLOT_LABELS[effective.colorSlot]
                             : "?";

    // Build each of the 3 fields with its focus tag (cyan bold when edited,
    // bright white when just selected, plain otherwise). snprintf to a
    // per-field buffer to avoid a single-static-buffer aliasing issue.
    char fgBuf[8];
    snprintf(fgBuf, sizeof(fgBuf), "%d%%", effective.fgPct);

    char patBuf[48], colBuf[48], fgOutBuf[48], rowBuf[192];
    bool focusPat = editing && _eventsEditField == EVT_FIELD_PATTERN;
    bool focusCol = editing && _eventsEditField == EVT_FIELD_COLOR;
    bool focusFg  = editing && _eventsEditField == EVT_FIELD_FG_PCT;
    snprintf(patBuf, sizeof(patBuf), "%s[%s]%s",
             focusPat ? VT_CYAN VT_BOLD : (selected ? VT_BRIGHT_WHITE : ""), patLabel, VT_RESET);
    snprintf(colBuf, sizeof(colBuf), "%s[%-16s]%s",
             focusCol ? VT_CYAN VT_BOLD : (selected ? VT_BRIGHT_WHITE : ""), colLabel, VT_RESET);
    snprintf(fgOutBuf, sizeof(fgOutBuf), "%s[%s]%s",
             focusFg ? VT_CYAN VT_BOLD : (selected ? VT_BRIGHT_WHITE : ""), fgBuf, VT_RESET);

    const char* leader = selected ? (VT_CYAN VT_BOLD "> " VT_RESET) : "  ";
    snprintf(rowBuf, sizeof(rowBuf), "%s%-12s %-28s %-28s %-12s",
             leader, PHASE0_EVENT_LABELS[i], patBuf, colBuf, fgOutBuf);
    _ui->drawFrameLine("%s", rowBuf);
  }

  _ui->drawFrameEmpty();
  _ui->drawSection("INFO");
  if (_eventsSub == EVENTS_EDIT) {
    const char* fieldName = (_eventsEditField == EVT_FIELD_PATTERN) ? "PATTERN"
                          : (_eventsEditField == EVT_FIELD_COLOR)   ? "COLOR" : "FG%";
    _ui->drawFrameLine(VT_DIM "Editing %s — %s focused." VT_RESET,
                       PHASE0_EVENT_LABELS[_eventsCursor], fieldName);
    _ui->drawFrameLine(VT_DIM "UP/DOWN : switch field. LEFT/RIGHT : adjust." VT_RESET);
    _ui->drawFrameLine(VT_DIM "ENTER : save & exit edit. 'q' : cancel." VT_RESET);
  } else {
    uint8_t evt = PHASE0_EVENT_IDS[_eventsCursor];
    bool isDefault = (_lwk.eventOverrides[evt].patternId == PTN_NONE);
    _ui->drawFrameLine(VT_DIM "%s %s" VT_RESET,
                       PHASE0_EVENT_LABELS[_eventsCursor],
                       isDefault ? "(using compile-time default)" : "(overridden)");
    _ui->drawFrameLine(VT_DIM "ENTER edits pattern / color / fgPct. 'd' resets to default." VT_RESET);
  }
}

// --- run() ---

void ToolLedSettings::run() {
  if (!_ui) return;

  // Reset state on entry
  _page = PAGE_PATTERNS;
  _colorsCursor    = 0;
  _colorsSub       = COLORS_NAV;
  _colorsEditField = EDIT_FIELD_PRESET;
  _patternsCursor  = 0;
  _patternsSub     = PATTERNS_NAV;
  _patternsEditField = 0;
  _eventsCursor    = 0;
  _eventsSub       = EVENTS_NAV;
  _eventsEditField = EVT_FIELD_PATTERN;

  // Load ColorSlotStore from NVS
  bool colorLoaded = NvsManager::loadBlob(LED_SETTINGS_NVS_NAMESPACE, COLOR_SLOT_NVS_KEY,
                                           COLOR_SLOT_MAGIC, COLOR_SLOT_VERSION,
                                           &_cwk, sizeof(_cwk));
  if (!colorLoaded) {
    _cwk.magic    = COLOR_SLOT_MAGIC;
    _cwk.version  = COLOR_SLOT_VERSION;
    _cwk.reserved = 0;
    for (uint8_t i = 0; i < COLOR_SLOT_COUNT; i++) {
      _cwk.slots[i].presetId  = DEFAULT_SLOT_PRESETS[i];
      _cwk.slots[i].hueOffset = DEFAULT_SLOT_HUES[i];
    }
  }

  // Load LedSettingsStore from NVS — needed for PATTERNS page.
  bool ledLoaded = NvsManager::loadBlob(LED_SETTINGS_NVS_NAMESPACE, LED_SETTINGS_NVS_KEY,
                                         EEPROM_MAGIC, LED_SETTINGS_VERSION,
                                         &_lwk, sizeof(_lwk));
  if (ledLoaded) {
    validateLedSettingsStore(_lwk);
  } else {
    // Not saved yet : NvsManager has compile-time defaults loaded in its
    // internal cache, but we need our own working copy. Seed minimal
    // defaults matching NvsManager init body for the fields we edit here.
    _lwk.magic                = EEPROM_MAGIC;
    _lwk.version              = LED_SETTINGS_VERSION;
    _lwk.reserved             = 0;
    _lwk.normalFgIntensity    = 85;
    _lwk.normalBgIntensity    = 10;
    _lwk.fgArpStopMin         = 30;
    _lwk.fgArpStopMax         = 100;
    _lwk.fgArpPlayMax         = 80;
    _lwk.bgArpStopMin         = 8;
    _lwk.bgArpPlayMin         = 8;
    _lwk.tickFlashFg          = 100;
    _lwk.tickFlashBg          = 25;
    _lwk.bgFactor             = 25;
    _lwk.pulsePeriodMs        = 1472;
    _lwk.tickFlashDurationMs  = 30;
    _lwk.gammaTenths          = 20;
    _lwk.sparkOnMs            = 50;
    _lwk.sparkGapMs           = 70;
    _lwk.sparkCycles          = 2;
    _lwk.bankBlinks           = 3;
    _lwk.bankDurationMs       = 300;
    _lwk.bankBrightnessPct    = 80;
    _lwk.scaleRootBlinks      = 2;
    _lwk.scaleRootDurationMs  = 200;
    _lwk.scaleModeBlinks      = 2;
    _lwk.scaleModeDurationMs  = 200;
    _lwk.scaleChromBlinks     = 2;
    _lwk.scaleChromDurationMs = 200;
    _lwk.holdOnFadeMs         = 500;
    _lwk.holdOffFadeMs        = 500;
    _lwk.octaveBlinks         = 3;
    _lwk.octaveDurationMs     = 300;
    for (uint8_t i = 0; i < EVT_COUNT; i++) {
      _lwk.eventOverrides[i].patternId = PTN_NONE;
      _lwk.eventOverrides[i].colorSlot = 0;
      _lwk.eventOverrides[i].fgPct     = 0;
    }
  }

  // NVS badge : "saved" if both blobs loaded cleanly.
  _nvsSaved = colorLoaded && ledLoaded;

  if (_leds) _leds->previewBegin();

  InputParser input;
  bool screenDirty = true;

  _ui->vtClear();

  while (true) {
    NavEvent ev = input.update();

    // Determine if we're in a page sub-state (sub-state consumes 'q' and 't'
    // for cancel / field switch rather than top-level exit / page toggle).
    bool inSubState = (_page == PAGE_COLORS   && _colorsSub   != COLORS_NAV)
                   || (_page == PAGE_PATTERNS && _patternsSub != PATTERNS_NAV)
                   || (_page == PAGE_EVENTS   && _eventsSub   != EVENTS_NAV);

    if (inSubState) {
      switch (_page) {
        case PAGE_COLORS:   handlePageColors(ev, screenDirty);   break;
        case PAGE_PATTERNS: handlePagePatterns(ev, screenDirty); break;
        case PAGE_EVENTS:   handlePageEvents(ev, screenDirty);   break;
        default: break;
      }
    } else {
      // Top-level page nav
      if (ev.type == NAV_QUIT) break;
      if (ev.type == NAV_TOGGLE) {
        _page = (Page)((_page + 1) % PAGE_COUNT);
        screenDirty = true;
      } else {
        // Route to active page
        switch (_page) {
          case PAGE_COLORS:   handlePageColors(ev, screenDirty);   break;
          case PAGE_PATTERNS: handlePagePatterns(ev, screenDirty); break;
          case PAGE_EVENTS:   handlePageEvents(ev, screenDirty);   break;
          default: break;
        }
      }
    }

    // Live preview for COLORS page : refreshes each frame when cursor moves
    // or hue/preset changes (screenDirty triggers it here).
    if (_page == PAGE_COLORS && screenDirty) {
      previewCurrentColor();
    }

    if (screenDirty) {
      screenDirty = false;
      _ui->vtFrameStart();
      _ui->drawConsoleHeader("TOOL 8: LED SETTINGS", _nvsSaved);
      _ui->drawFrameEmpty();

      // Page tabs line
      {
        const char* pn = (_page == PAGE_PATTERNS) ? VT_CYAN VT_BOLD "[PATTERNS]" VT_RESET : VT_DIM " PATTERNS " VT_RESET;
        const char* cn = (_page == PAGE_COLORS)   ? VT_CYAN VT_BOLD "[COLORS]"   VT_RESET : VT_DIM " COLORS "   VT_RESET;
        const char* en = (_page == PAGE_EVENTS)   ? VT_CYAN VT_BOLD "[EVENTS]"   VT_RESET : VT_DIM " EVENTS "   VT_RESET;
        _ui->drawFrameLine("  %s   %s   %s", pn, cn, en);
      }
      _ui->drawFrameEmpty();

      switch (_page) {
        case PAGE_PATTERNS: renderPagePatterns(); break;
        case PAGE_COLORS:   renderPageColors();   break;
        case PAGE_EVENTS:   renderPageEvents();   break;
        default: break;
      }
      _ui->drawFrameEmpty();

      // Control bar — context-aware
      if (_page == PAGE_COLORS && _colorsSub == COLORS_EDIT) {
        _ui->drawControlBar(VT_DIM "[^v] FIELD" CBAR_SEP "[</>] ADJUST" CBAR_SEP "[RET] SAVE" CBAR_SEP "[q] CANCEL" VT_RESET);
      } else if (_page == PAGE_COLORS) {
        _ui->drawControlBar(VT_DIM "[^v] NAV" CBAR_SEP "[RET] EDIT" CBAR_SEP "[d] DFLT" CBAR_SEP "[t] PAGE" CBAR_SEP "[q] EXIT" VT_RESET);
      } else if (_page == PAGE_PATTERNS && _patternsSub == PATTERNS_EDIT) {
        _ui->drawControlBar(VT_DIM "[^v] FIELD" CBAR_SEP "[</>] ADJUST" CBAR_SEP "[RET] SAVE" CBAR_SEP "[q] CANCEL" VT_RESET);
      } else if (_page == PAGE_PATTERNS) {
        _ui->drawControlBar(VT_DIM "[^v] NAV" CBAR_SEP "[RET] EDIT" CBAR_SEP "[t] PAGE" CBAR_SEP "[q] EXIT" VT_RESET);
      } else if (_page == PAGE_EVENTS && _eventsSub == EVENTS_EDIT) {
        _ui->drawControlBar(VT_DIM "[^v] FIELD" CBAR_SEP "[</>] ADJUST" CBAR_SEP "[RET] SAVE" CBAR_SEP "[q] CANCEL" VT_RESET);
      } else if (_page == PAGE_EVENTS) {
        _ui->drawControlBar(VT_DIM "[^v] NAV" CBAR_SEP "[RET] EDIT" CBAR_SEP "[d] DFLT" CBAR_SEP "[t] PAGE" CBAR_SEP "[q] EXIT" VT_RESET);
      } else {
        _ui->drawControlBar(VT_DIM "[t] NEXT PAGE" CBAR_SEP "[q] EXIT" VT_RESET);
      }

      _ui->vtFrameEnd();
    }

    delay(10);
  }

  if (_leds) {
    _leds->previewClear();
    _leds->previewEnd();
  }
}
