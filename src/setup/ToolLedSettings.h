#ifndef TOOL_LED_SETTINGS_H
#define TOOL_LED_SETTINGS_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "ToolLedPreview.h"

class LedController;
class SetupUI;
class PotRouter;

// =================================================================
// Tool 8 — LED Settings (Phase 0.1 respec : single-view 6 sections)
// =================================================================
// Spec: docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md
//
// Single scrollable view with 6 semantic sections (NORMAL / ARPEG / LOOP /
// TRANSPORT / CONFIRMATIONS / GLOBAL). NAV+EDIT paradigm:
//   NAV   : up/down = line, left/right = skip section, ENTER = edit,
//           d = reset line to default, q = exit tool
//   EDIT  : context-sensitive — color line: left/right cycle preset,
//                                           up/down cycle hue
//                              single num: left/right ±10, up/down ±1
//                              multi num : left/right focus field,
//                                          up/down adjust focused value
//
// Live preview via ToolLedPreview — context set on cursor move or value
// change. Gamma hot-reloads on commit (no reboot required).
// =================================================================

class ToolLedSettings {
public:
  ToolLedSettings();
  void begin(LedController* leds, SetupUI* ui, PotRouter* potRouter,
             BankSlot* banks);
  void run();

  // Enums promoted to public scope for file-scope static tables that index
  // by these values (SECTION_LABELS[], LINE_LABELS[], LINE_DESCRIPTIONS[]).
  // Purely internal semantics — no external consumer.

  // -------- Section model --------
  enum Section : uint8_t {
    SEC_NORMAL         = 0,
    SEC_ARPEG          = 1,
    SEC_LOOP           = 2,
    SEC_TRANSPORT      = 3,
    SEC_CONFIRMATIONS  = 4,
    SEC_GLOBAL         = 5,
    SEC_COUNT          = 6,
  };

  // Flat enum of every navigable/editable line. Order = visible layout
  // order (spec §4.1). Section resolution via sectionOf() helper.
  enum LineId : uint8_t {
    LINE_NORMAL_BASE_COLOR = 0,

    LINE_ARPEG_BASE_COLOR,

    LINE_LOOP_BASE_COLOR,
    LINE_LOOP_SAVE_COLOR,
    LINE_LOOP_SAVE_DURATION,
    LINE_LOOP_CLEAR_COLOR,
    LINE_LOOP_CLEAR_DURATION,
    LINE_LOOP_SLOT_COLOR,
    LINE_LOOP_SLOT_DURATION,

    LINE_TRANSPORT_PLAY_COLOR,
    LINE_TRANSPORT_PLAY_TIMING,        // multi : brightness + duration
    LINE_TRANSPORT_STOP_COLOR,
    LINE_TRANSPORT_STOP_TIMING,        // multi : brightness + duration
    LINE_TRANSPORT_WAITING_COLOR,
    LINE_TRANSPORT_BREATHING,          // v9 multi : depth + period
    LINE_TRANSPORT_TICK_COMMON,        // multi : FG + BG %
    LINE_TRANSPORT_TICK_PLAY_COLOR,
    LINE_TRANSPORT_TICK_REC_COLOR,
    LINE_TRANSPORT_TICK_OVERDUB_COLOR,
    LINE_TRANSPORT_TICK_BEAT_DUR,
    LINE_TRANSPORT_TICK_BAR_DUR,
    LINE_TRANSPORT_TICK_WRAP_DUR,

    LINE_CONFIRM_BANK_COLOR,
    LINE_CONFIRM_BANK_TIMING,          // multi : brightness + duration
    LINE_CONFIRM_SCALE_ROOT_COLOR,
    LINE_CONFIRM_SCALE_ROOT_TIMING,    // multi : brightness (override) + duration
    LINE_CONFIRM_SCALE_MODE_COLOR,
    LINE_CONFIRM_SCALE_MODE_TIMING,
    LINE_CONFIRM_SCALE_CHROM_COLOR,
    LINE_CONFIRM_SCALE_CHROM_TIMING,
    LINE_CONFIRM_OCTAVE_COLOR,
    LINE_CONFIRM_OCTAVE_TIMING,        // multi : brightness (override) + duration
    LINE_CONFIRM_OK_COLOR,
    LINE_CONFIRM_OK_SPARK,             // multi : on + gap + cycles

    LINE_GLOBAL_FG_INTENSITY,          // v9 NEW : single FG slider for all bank types/states
    LINE_GLOBAL_BG_FACTOR,
    LINE_GLOBAL_GAMMA,

    LINE_COUNT,
  };

private:
  enum UiMode : uint8_t {
    UI_NAV  = 0,  // cursor on a line, no edit
    UI_EDIT = 1,  // editing the selected line (paradigm depends on LineId)
  };

  // Line shape (drives the edit paradigm + renderer branch).
  enum LineShape : uint8_t {
    SHAPE_COLOR        = 0,  // [preset] +hue  ; left/right preset, up/down hue
    SHAPE_SINGLE_NUM   = 1,  // one numeric   ; left/right ±coarse, up/down ±fine
    SHAPE_MULTI_NUM    = 2,  // N>=2 numerics ; left/right focus, up/down adjust
  };

  // -------- Working copies + backups --------
  LedSettingsStore _lwk;        // working copy for LED settings (v7)
  ColorSlotStore   _cwk;        // working copy for color slots (v5)
  SettingsStore    _ses;        // working copy for LOOP timers (v11, shared w/ Tool 6)

  LedSettingsStore _lwkBackup;  // snapshot at enterEdit, restored on cancelEdit
  ColorSlotStore   _cwkBackup;
  SettingsStore    _sesBackup;

  // Snapshot of foreground bank type at setup entry (for WAITING preview
  // §11.5 — source color of the crossfade = current mode color).
  BankType         _setupEntryBankType;

  // -------- Injected deps --------
  LedController*   _leds;
  SetupUI*         _ui;
  PotRouter*       _potRouter;   // tempo source for ToolLedPreview
  BankSlot*        _banks;       // read isForeground + type at run() entry

  // -------- Cursor + mode --------
  LineId   _cursor;
  UiMode   _uiMode;
  uint8_t  _editFocus;     // for SHAPE_MULTI_NUM : 0..N-1 field under focus
  bool     _nvsSaved;
  uint8_t  _viewportStart; // first visible LineId (scroll)

  // -------- Preview --------
  ToolLedPreview _preview;

  // -------- Run loop helpers --------
  void loadAll();
  bool saveLedSettings();
  bool saveColorSlots();
  bool saveSettings();
  void refreshBadge();

  // -------- Navigation --------
  Section sectionOf(LineId line) const;
  LineId  firstLineOfSection(Section s) const;
  LineId  lastLineOfSection(Section s) const;
  void    cursorUp();
  void    cursorDown();
  void    cursorNextSection();
  void    cursorPrevSection();
  void    ensureCursorVisible();

  // -------- Edit dispatch --------
  void    enterEdit();
  void    commitEdit();              // ENTER
  void    cancelEdit();               // q in edit : restore backup
  void    resetLineDefault();         // d : no confirm, immediate save

  // Edit paradigms — dir values : dx ∈ {-1, 0, +1}, dy ∈ {-1, 0, +1}.
  // accel = InputParser rapid-repeat flag.
  void    editColor(LineId line, int dx, int dy, bool accel);
  void    editSingleNumeric(LineId line, int dx, int dy, bool accel);
  void    editMultiNumeric(LineId line, int dx, int dy, bool accel);

  // -------- Line introspection (centralizes line -> data mapping) --------
  LineShape   shapeForLine(LineId line) const;
  ColorSlot*  colorSlotForLine(LineId line);                         // SHAPE_COLOR
  uint8_t     numericFieldCountForLine(LineId line) const;            // SHAPE_SINGLE/MULTI
  int32_t     readNumericField(LineId line, uint8_t fieldIdx) const;
  void        writeNumericField(LineId line, uint8_t fieldIdx, int32_t newVal);
  void        getNumericFieldBounds(LineId line, uint8_t fieldIdx,
                                    int32_t& minOut, int32_t& maxOut,
                                    int32_t& stepCoarseOut,
                                    int32_t& stepFineOut) const;

  // -------- Rendering --------
  void drawScreen();
  void drawLine(LineId line, bool isCursor, bool inEdit);
  void drawDescriptionPanel();
  const char* descriptionForLine(LineId line, bool inEdit) const;

  // Preview context dispatch — called on cursor move or value change.
  void updatePreviewContext();

  // Default reset per line (spec §9 defaults).
  void resetDefaultForLine(LineId line);

  // Helper : resolve the "effective" fgPct for event-override-based
  // brightness fields (scale root/mode/chrom, octave, play, stop).
  // If eventOverrides[evt].patternId == PTN_NONE, return EVENT_RENDER_DEFAULT
  // fgPct ; else return override fgPct.
  uint8_t  effectiveEventFgPct(uint8_t evt) const;
  // Writing back respects the override activation pattern : sets patternId
  // to EVENT_RENDER_DEFAULT[evt].patternId if currently PTN_NONE, preserves
  // the default colorSlot, and stores the new fgPct.
  void     setEventOverrideFgPct(uint8_t evt, uint8_t newFgPct);
};

#endif // TOOL_LED_SETTINGS_H
