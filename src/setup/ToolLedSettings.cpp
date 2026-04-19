#include "ToolLedSettings.h"
#include "SetupUI.h"
#include "InputParser.h"
#include "../core/LedController.h"
#include "../core/HardwareConfig.h"
#include "../managers/NvsManager.h"
#include <Arduino.h>

// =================================================================
// Tool 8 — LED Settings (Phase 0 step 0.8b — COLORS page)
// =================================================================
// PATTERNS and EVENTS pages still stubbed (steps 0.8c / 0.8d).
// COLORS page fully functional : vertical list of 15 slots, ENTER opens
// an edit sub-state with preset pool + hue offset field. Save on ENTER
// commit (conventions §1). Live preview on LED 3-4.
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

ToolLedSettings::ToolLedSettings()
  : _leds(nullptr), _ui(nullptr), _page(PAGE_COLORS),
    _nvsSaved(false),
    _cwk{},
    _colorsCursor(0),
    _colorsSub(COLORS_NAV),
    _colorsEditField(EDIT_FIELD_PRESET),
    _colorsEditBackup{} {}

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

// --- PATTERNS / EVENTS : stubs for 0.8c / 0.8d ---

void ToolLedSettings::renderPagePatterns() {
  _ui->drawSection("PATTERNS — step 0.8c");
  _ui->drawFrameLine(VT_DIM "  Pool nav (UP/DOWN) over 9 patterns," VT_RESET);
  _ui->drawFrameLine(VT_DIM "  ENTER opens field editor for selected pattern params." VT_RESET);
  _ui->drawFrameLine(VT_DIM "  Live preview on LED 3-4." VT_RESET);
}

void ToolLedSettings::renderPageEvents() {
  _ui->drawSection("EVENTS — step 0.8d");
  _ui->drawFrameLine(VT_DIM "  Vertical field list per event." VT_RESET);
  _ui->drawFrameLine(VT_DIM "  Row edits : pattern (pool) + color slot (pool) + fgPct (field)." VT_RESET);
  _ui->drawFrameLine(VT_DIM "  RAMP_HOLD events : rampMs shown as derived (Tool 6)." VT_RESET);
  _ui->drawFrameLine(VT_DIM "  Preview via 'b' triggers the event on LED 3-4." VT_RESET);
}

// --- run() ---

void ToolLedSettings::run() {
  if (!_ui) return;

  // Reset state on entry
  _page = PAGE_PATTERNS;
  _colorsCursor    = 0;
  _colorsSub       = COLORS_NAV;
  _colorsEditField = EDIT_FIELD_PRESET;

  // Load ColorSlotStore from NVS (fallback : validator-safe defaults already
  // set by NvsManager at boot ; here we just re-load the latest persisted copy).
  _nvsSaved = NvsManager::loadBlob(LED_SETTINGS_NVS_NAMESPACE, COLOR_SLOT_NVS_KEY,
                                    COLOR_SLOT_MAGIC, COLOR_SLOT_VERSION,
                                    &_cwk, sizeof(_cwk));
  if (!_nvsSaved) {
    // Seed with defaults (same as NvsManager init body).
    _cwk.magic    = COLOR_SLOT_MAGIC;
    _cwk.version  = COLOR_SLOT_VERSION;
    _cwk.reserved = 0;
    for (uint8_t i = 0; i < COLOR_SLOT_COUNT; i++) {
      _cwk.slots[i].presetId  = DEFAULT_SLOT_PRESETS[i];
      _cwk.slots[i].hueOffset = DEFAULT_SLOT_HUES[i];
    }
  }

  if (_leds) _leds->previewBegin();

  InputParser input;
  bool screenDirty = true;

  _ui->vtClear();

  while (true) {
    NavEvent ev = input.update();

    // Inter-page nav via 't' — top level only (edit sub-states consume 't' themselves)
    if (_page == PAGE_COLORS && _colorsSub != COLORS_NAV) {
      // In a sub-state : route all events to the page handler (it handles 'q'
      // as cancel, ENTER as commit, etc.)
      handlePageColors(ev, screenDirty);
    } else {
      // Top-level page nav
      if (ev.type == NAV_QUIT) break;
      if (ev.type == NAV_TOGGLE) {
        _page = (Page)((_page + 1) % PAGE_COUNT);
        screenDirty = true;
      } else {
        // Route to active page
        switch (_page) {
          case PAGE_COLORS:   handlePageColors(ev, screenDirty); break;
          case PAGE_PATTERNS: /* step 0.8c */ break;
          case PAGE_EVENTS:   /* step 0.8d */ break;
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
