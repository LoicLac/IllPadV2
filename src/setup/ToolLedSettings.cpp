#include "ToolLedSettings.h"
#include "../core/LedController.h"
#include "../core/PotFilter.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"
#include "InputParser.h"
#include <Arduino.h>
#include <Preferences.h>

// =================================================================
// Page definitions
// =================================================================
// Page 0: COLOR (18 rows) + TIMING (3 rows) = 21
// Page 1: CONFIRM — 11 params
static const uint8_t PAGE0_COUNT = 22;  // COLOR_ROWS(19) + TIMING(3: pulse, tickFlashDur, gamma)
static const uint8_t PAGE1_COUNT = 12;  // 12 params: bank(2) + scale(6) + hold(2: ON+OFF) + octave(2)

static const char* s_pageNames[] = {"COLOR", "CONFIRM"};

// =================================================================
// COLOR row mapping: 16 rows → 13 color slots
// =================================================================
struct ColorRowDef {
  uint8_t     slotId;       // ColorSlotId
  uint8_t     partnerRow;   // 0xFF = no partner
  const char* label;
};

// Rows 5, 6, 9 are hidden (unused in v3: no pulse on BG or playing states)
static bool isColorRowHidden(uint8_t row) {
  return (row == 5 || row == 6 || row == 9);
}

static const ColorRowDef COLOR_ROWS[19] = {
  { CSLOT_NORMAL_FG,   0xFF, "NORMAL fg"       },  // 0
  { CSLOT_NORMAL_BG,   0xFF, "NORMAL bg"       },  // 1
  { CSLOT_ARPEG_FG,       3, "ARPfg pulse lo"  },  // 2  (FG stopped-loaded pulse)
  { CSLOT_ARPEG_FG,       2, "ARPfg pulse hi"  },  // 3  (FG stopped-loaded pulse)
  { CSLOT_ARPEG_BG,    0xFF, "ARPbg stopped"   },  // 4  (solid dim)
  { CSLOT_ARPEG_BG,       4, "ARPbg stop max"  },  // 5  HIDDEN
  { CSLOT_ARPEG_FG,       7, "ARPfg play min"  },  // 6  HIDDEN
  { CSLOT_ARPEG_FG,    0xFF, "ARPfg playing"   },  // 7  (solid bright between ticks)
  { CSLOT_ARPEG_BG,    0xFF, "ARPbg playing"   },  // 8  (solid dim between ticks)
  { CSLOT_ARPEG_BG,       8, "ARPbg play max"  },  // 9  HIDDEN
  { CSLOT_TICK_FLASH,    11, "Tick flash fg"   },  // 10
  { CSLOT_TICK_FLASH,    10, "Tick flash bg"   },  // 11
  { CSLOT_BANK_SWITCH, 0xFF, "Bank switch"     },  // 12
  { CSLOT_SCALE_ROOT,  0xFF, "Scale root"      },  // 13
  { CSLOT_SCALE_MODE,  0xFF, "Scale mode"      },  // 14
  { CSLOT_SCALE_CHROM, 0xFF, "Scale chromatic" },  // 15
  { CSLOT_HOLD_ON,     0xFF, "Hold ON (fade in)"  }, // 16
  { CSLOT_HOLD_OFF,    0xFF, "Hold OFF (fade out)"}, // 17
  { CSLOT_OCTAVE,      0xFF, "Octave"          },  // 18
};

// Sine LUT: shared from HardwareConfig.h (LED_LED_SINE_LUT[256])

// =================================================================
// Defaults
// =================================================================
static LedSettingsStore s_ledDefaults() {
  LedSettingsStore d;
  d.magic   = EEPROM_MAGIC;
  d.version = LED_SETTINGS_VERSION;
  d.reserved = 0;
  d.normalFgIntensity  = 85;
  d.normalBgIntensity  = 10;
  d.fgArpStopMin       = 30;
  d.fgArpStopMax       = 100;
  d.fgArpPlayMin       = 30;
  d.fgArpPlayMax       = 80;
  d.bgArpStopMin       = 8;
  d.bgArpStopMax       = 25;
  d.bgArpPlayMin       = 8;
  d.bgArpPlayMax       = 20;
  d.tickFlashFg        = 100;
  d.tickFlashBg        = 25;
  d.pulsePeriodMs      = 1472;
  d.tickFlashDurationMs = 30;
  d.gammaTenths        = 20;
  d.bankBlinks         = 3;
  d.bankDurationMs     = 300;
  d.bankBrightnessPct  = 80;
  d.scaleRootBlinks    = 2;
  d.scaleRootDurationMs = 200;
  d.scaleModeBlinks    = 2;
  d.scaleModeDurationMs = 200;
  d.scaleChromBlinks   = 2;
  d.scaleChromDurationMs = 200;
  d.holdOnFadeMs       = 500;
  d.holdOffFadeMs      = 500;
  d.octaveBlinks       = 3;
  d.octaveDurationMs   = 300;
  return d;
}

static ColorSlotStore s_colorDefaults() {
  ColorSlotStore c;
  c.magic = COLOR_SLOT_MAGIC;
  c.version = COLOR_SLOT_VERSION;
  c.reserved = 0;
  static const uint8_t presets[COLOR_SLOT_COUNT] = {
    0, 1, 3, 4, 0, 0, 6, 7, 7, 4, 4, 9
  };
  for (uint8_t i = 0; i < COLOR_SLOT_COUNT; i++) {
    c.slots[i].presetId = presets[i];
    c.slots[i].hueOffset = 0;
  }
  return c;
}

// =================================================================
// Constructor
// =================================================================
ToolLedSettings::ToolLedSettings()
  : _leds(nullptr), _ui(nullptr),
    _page(0), _cursor(0), _colorField(0),
    _editing(false), _nvsSaved(true), _confirmDefaults(false),
    _prevState(PREV_IDLE), _prevStart(0), _prevEventRow(0)
{
  memset(&_wk, 0, sizeof(_wk));
  memset(&_cwk, 0, sizeof(_cwk));
}

void ToolLedSettings::begin(LedController* leds, SetupUI* ui) {
  _leds = leds;
  _ui = ui;
}

// =================================================================
// pageParamCount
// =================================================================
uint8_t ToolLedSettings::pageParamCount() const {
  return (_page == 0) ? PAGE0_COUNT : PAGE1_COUNT;
}

// =================================================================
// Row intensity helpers
// =================================================================
uint8_t ToolLedSettings::getRowIntensity(uint8_t row) const {
  switch (row) {
    case 0:  return _wk.normalFgIntensity;
    case 1:  return _wk.normalBgIntensity;
    case 2:  return _wk.fgArpStopMin;
    case 3:  return _wk.fgArpStopMax;
    case 4:  return _wk.bgArpStopMin;
    case 5:  return _wk.bgArpStopMax;
    case 6:  return _wk.fgArpPlayMin;
    case 7:  return _wk.fgArpPlayMax;
    case 8:  return _wk.bgArpPlayMin;
    case 9:  return _wk.bgArpPlayMax;
    case 10: return _wk.tickFlashFg;
    case 11: return _wk.tickFlashBg;
    case 12: return _wk.bankBrightnessPct;
    default: return 100;  // rows 13-17: fixed 100%
  }
}

void ToolLedSettings::setRowIntensity(uint8_t row, uint8_t val) {
  switch (row) {
    case 0: _wk.normalFgIntensity = val; break;
    case 1: _wk.normalBgIntensity = val; break;
    case 2: _wk.fgArpStopMin = val;
            if (_wk.fgArpStopMin > _wk.fgArpStopMax) _wk.fgArpStopMax = val;
            break;
    case 3: _wk.fgArpStopMax = val;
            if (_wk.fgArpStopMax < _wk.fgArpStopMin) _wk.fgArpStopMin = val;
            break;
    case 4: _wk.bgArpStopMin = val;
            if (_wk.bgArpStopMin > _wk.bgArpStopMax) _wk.bgArpStopMax = val;
            break;
    case 5: _wk.bgArpStopMax = val;
            if (_wk.bgArpStopMax < _wk.bgArpStopMin) _wk.bgArpStopMin = val;
            break;
    case 6: _wk.fgArpPlayMin = val;
            if (_wk.fgArpPlayMin > _wk.fgArpPlayMax) _wk.fgArpPlayMax = val;
            break;
    case 7: _wk.fgArpPlayMax = val;
            if (_wk.fgArpPlayMax < _wk.fgArpPlayMin) _wk.fgArpPlayMin = val;
            break;
    case 8: _wk.bgArpPlayMin = val;
            if (_wk.bgArpPlayMin > _wk.bgArpPlayMax) _wk.bgArpPlayMax = val;
            break;
    case 9: _wk.bgArpPlayMax = val;
            if (_wk.bgArpPlayMax < _wk.bgArpPlayMin) _wk.bgArpPlayMin = val;
            break;
    case 10: _wk.tickFlashFg = val; break;
    case 11: _wk.tickFlashBg = val; break;
    case 12: _wk.bankBrightnessPct = val; break;
    default: break;  // rows 13-17: not editable
  }
}

bool ToolLedSettings::rowHasEditableIntensity(uint8_t row) const {
  return (row <= 12);
}

// =================================================================
// adjustColorField — preset / hue / intensity
// =================================================================
void ToolLedSettings::adjustColorField(int8_t dir, bool accel) {
  if (_cursor >= 19) return;  // timing rows handled elsewhere
  uint8_t slotId = COLOR_ROWS[_cursor].slotId;
  ColorSlot& slot = _cwk.slots[slotId];

  switch (_colorField) {
    case 0: {  // Preset: wrap 0..COLOR_PRESET_COUNT-1
      int val = (int)slot.presetId + dir;
      if (val < 0) val = COLOR_PRESET_COUNT - 1;
      if (val >= COLOR_PRESET_COUNT) val = 0;
      slot.presetId = (uint8_t)val;
      break;
    }
    case 1: {  // Hue: clamp -128..+127
      int step = accel ? 30 : 5;
      int val = (int)slot.hueOffset + dir * step;
      if (val < -128) val = -128;
      if (val > 127) val = 127;
      slot.hueOffset = (int8_t)val;
      break;
    }
    case 2: {  // Intensity: 0-100%
      int step = accel ? 10 : 1;
      int val = (int)getRowIntensity(_cursor) + dir * step;
      if (val < 0) val = 0;
      if (val > 100) val = 100;
      setRowIntensity(_cursor, (uint8_t)val);
      break;
    }
  }
}

// =================================================================
// adjustTimingParam — pulse period / tick flash duration
// =================================================================
void ToolLedSettings::adjustTimingParam(int8_t dir, bool accel) {
  uint8_t timingIdx = _cursor - 18;  // 0, 1, or 2
  if (timingIdx == 0) {  // pulsePeriodMs 500-4000
    int step = accel ? 250 : 50;
    int val = (int)_wk.pulsePeriodMs + dir * step;
    if (val < 500) val = 500;
    if (val > 4000) val = 4000;
    _wk.pulsePeriodMs = (uint16_t)val;
  } else if (timingIdx == 1) {  // tickFlashDurationMs 10-100
    int step = accel ? 25 : 5;
    int val = (int)_wk.tickFlashDurationMs + dir * step;
    if (val < 10) val = 10;
    if (val > 100) val = 100;
    _wk.tickFlashDurationMs = (uint8_t)val;
  } else {  // gammaTenths 10-30 (gamma 1.0-3.0)
    int val = (int)_wk.gammaTenths + dir;
    if (val < 10) val = 10;
    if (val > 30) val = 30;
    _wk.gammaTenths = (uint8_t)val;
  }
}

// =================================================================
// adjustConfirmParam — page 1 (15 params)
// =================================================================
void ToolLedSettings::adjustConfirmParam(int8_t dir, bool accel) {
  switch (_cursor) {
    case 0: { // bankBlinks 1-3 wrap
      int val = (int)_wk.bankBlinks + dir;
      if (val < 1) val = 3;
      if (val > 3) val = 1;
      _wk.bankBlinks = (uint8_t)val;
      break;
    }
    case 1: { // bankDurationMs 100-500
      int step = accel ? 100 : 50;
      int val = (int)_wk.bankDurationMs + dir * step;
      if (val < 100) val = 100;
      if (val > 500) val = 500;
      _wk.bankDurationMs = (uint16_t)val;
      break;
    }
    case 2: { // scaleRootBlinks 1-3
      int val = (int)_wk.scaleRootBlinks + dir;
      if (val < 1) val = 3; if (val > 3) val = 1;
      _wk.scaleRootBlinks = (uint8_t)val;
      break;
    }
    case 3: { // scaleRootDurationMs 100-500
      int step = accel ? 100 : 50;
      int val = (int)_wk.scaleRootDurationMs + dir * step;
      if (val < 100) val = 100; if (val > 500) val = 500;
      _wk.scaleRootDurationMs = (uint16_t)val;
      break;
    }
    case 4: { // scaleModeBlinks 1-3
      int val = (int)_wk.scaleModeBlinks + dir;
      if (val < 1) val = 3; if (val > 3) val = 1;
      _wk.scaleModeBlinks = (uint8_t)val;
      break;
    }
    case 5: { // scaleModeDurationMs 100-500
      int step = accel ? 100 : 50;
      int val = (int)_wk.scaleModeDurationMs + dir * step;
      if (val < 100) val = 100; if (val > 500) val = 500;
      _wk.scaleModeDurationMs = (uint16_t)val;
      break;
    }
    case 6: { // scaleChromBlinks 1-3
      int val = (int)_wk.scaleChromBlinks + dir;
      if (val < 1) val = 3; if (val > 3) val = 1;
      _wk.scaleChromBlinks = (uint8_t)val;
      break;
    }
    case 7: { // scaleChromDurationMs 100-500
      int step = accel ? 100 : 50;
      int val = (int)_wk.scaleChromDurationMs + dir * step;
      if (val < 100) val = 100; if (val > 500) val = 500;
      _wk.scaleChromDurationMs = (uint16_t)val;
      break;
    }
    case 8: { // holdOnFadeMs 0-1000
      int step = accel ? 100 : 50;
      int val = (int)_wk.holdOnFadeMs + dir * step;
      if (val < 0) val = 0; if (val > 1000) val = 1000;
      _wk.holdOnFadeMs = (uint16_t)val;
      break;
    }
    case 9: { // holdOffFadeMs 0-1000
      int step = accel ? 100 : 50;
      int val = (int)_wk.holdOffFadeMs + dir * step;
      if (val < 0) val = 0; if (val > 1000) val = 1000;
      _wk.holdOffFadeMs = (uint16_t)val;
      break;
    }
    case 10: { // octaveBlinks 1-3
      int val = (int)_wk.octaveBlinks + dir;
      if (val < 1) val = 3; if (val > 3) val = 1;
      _wk.octaveBlinks = (uint8_t)val;
      break;
    }
    case 11: { // octaveDurationMs 100-500
      int step = accel ? 100 : 50;
      int val = (int)_wk.octaveDurationMs + dir * step;
      if (val < 100) val = 100; if (val > 500) val = 500;
      _wk.octaveDurationMs = (uint16_t)val;
      break;
    }
    default: break;
  }
}

// =================================================================
// Save
// =================================================================
bool ToolLedSettings::saveLedSettings() {
  _wk.magic = EEPROM_MAGIC;
  _wk.version = LED_SETTINGS_VERSION;
  if (!NvsManager::saveBlob(LED_SETTINGS_NVS_NAMESPACE, LED_SETTINGS_NVS_KEY, &_wk, sizeof(_wk)))
    return false;
  if (_leds) _leds->loadLedSettings(_wk);
  return true;
}

bool ToolLedSettings::saveColorSlots() {
  _cwk.magic = COLOR_SLOT_MAGIC;
  _cwk.version = COLOR_SLOT_VERSION;  // constant instead of hardcoded 1
  if (!NvsManager::saveBlob(LED_SETTINGS_NVS_NAMESPACE, COLOR_SLOT_NVS_KEY, &_cwk, sizeof(_cwk)))
    return false;
  if (_leds) _leds->loadColorSlots(_cwk);
  return true;
}

// =================================================================
// drawColorRow — single row with ANSI 24-bit swatch
// =================================================================
void ToolLedSettings::drawColorRow(uint8_t row, bool selected, bool editing) {
  if (row >= 19) return;
  const ColorRowDef& def = COLOR_ROWS[row];
  const ColorSlot& slot = _cwk.slots[def.slotId];
  RGBW resolved = resolveColorSlot(slot);

  // Approximate RGBW → RGB for terminal swatch (W adds to all channels)
  uint8_t sr = (uint8_t)min(255, (int)resolved.r + resolved.w);
  uint8_t sg = (uint8_t)min(255, (int)resolved.g + resolved.w);
  uint8_t sb = (uint8_t)min(255, (int)resolved.b + resolved.w);

  const char* presetName = COLOR_PRESET_NAMES[slot.presetId < COLOR_PRESET_COUNT ? slot.presetId : 0];
  uint8_t intensity = getRowIntensity(row);
  bool hasInt = rowHasEditableIntensity(row);

  char line[256];
  char cursor[4] = "  ";
  if (selected) { cursor[0] = '>'; cursor[1] = ' '; }

  // Build the line with field highlighting
  // Buffers sized for VT escapes: \033[36m\033[1m[content]\033[0m = ~25 bytes overhead
  char presetBuf[48], hueBuf[32], intBuf[32];

  // Preset field — arrows control this
  if (editing) {
    snprintf(presetBuf, sizeof(presetBuf), VT_CYAN VT_BOLD "\xe2\x97\x84%-12s\xe2\x96\xba" VT_RESET, presetName);
  } else if (selected) {
    snprintf(presetBuf, sizeof(presetBuf), VT_BRIGHT_WHITE "%-13s " VT_RESET, presetName);
  } else {
    snprintf(presetBuf, sizeof(presetBuf), "%-13s ", presetName);
  }

  // Hue field — pot 1 controls this
  if (editing && _pots.isActive(0)) {
    snprintf(hueBuf, sizeof(hueBuf), VT_YELLOW "%+4d" "\xc2\xb0" VT_RESET, slot.hueOffset);
  } else if (selected) {
    snprintf(hueBuf, sizeof(hueBuf), VT_BRIGHT_WHITE "%+4d" "\xc2\xb0" VT_RESET, slot.hueOffset);
  } else {
    snprintf(hueBuf, sizeof(hueBuf), "%+4d\xc2\xb0", slot.hueOffset);
  }

  // Intensity field — pot 2 controls this
  if (!hasInt) {
    intBuf[0] = '\0';
  } else if (editing && _pots.isActive(1)) {
    snprintf(intBuf, sizeof(intBuf), VT_YELLOW " %3d%%" VT_RESET, intensity);
  } else if (selected) {
    snprintf(intBuf, sizeof(intBuf), VT_BRIGHT_WHITE " %3d%%" VT_RESET, intensity);
  } else {
    snprintf(intBuf, sizeof(intBuf), " %3d%%", intensity);
  }

  // Assemble: cursor + label + swatch + preset + hue + intensity
  snprintf(line, sizeof(line),
    "%s%s%-16s \033[38;2;%d;%d;%dm\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\033[0m %s %s%s",
    selected ? VT_CYAN : "", cursor, def.label,
    sr, sg, sb,
    presetBuf, hueBuf, intBuf);

  _ui->drawFrameLine("%s" VT_RESET, line);
}

// =================================================================
// drawDescription — INFO section per page + cursor
// =================================================================
void ToolLedSettings::drawDescription() {
  if (_page == 0) {
    if (_cursor < 19) {
      // COLOR rows
      switch (_cursor) {
        case 0:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Normal Foreground" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Solid color of the active bank LED when playing NORMAL." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Lower intensities (5-15%%) give a subtle glow for dark stages." VT_RESET);
          _ui->drawFrameLine(VT_DIM "The rear pot controls the master brightness on top." VT_RESET);
          break;
        case 1:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Normal Background" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Color of inactive NORMAL bank LEDs. Keep dimmer than" VT_RESET);
          _ui->drawFrameLine(VT_DIM "foreground for clear visual hierarchy. Try a different" VT_RESET);
          _ui->drawFrameLine(VT_DIM "preset (e.g. Warm White) to distinguish from foreground." VT_RESET);
          break;
        case 2:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Arpeg FG \xe2\x80\x94 Pulse Low" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Lowest point of the breathing pulse when notes are" VT_RESET);
          _ui->drawFrameLine(VT_DIM "loaded and waiting. Also used as FG idle intensity." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Low (3-8%%) = subtle breath, higher = always visible." VT_RESET);
          break;
        case 3:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Arpeg FG \xe2\x80\x94 Pulse High" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Peak of the 'ready to play' breathing pulse." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Wider gap between min/max = deeper breath." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Narrow gap = subtle shimmer." VT_RESET);
          break;
        case 4:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Arpeg BG \xe2\x80\x94 Stopped" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Solid dim intensity for all background arp banks" VT_RESET);
          _ui->drawFrameLine(VT_DIM "when stopped. Keep well below foreground values." VT_RESET);
          _ui->drawFrameEmpty();
          break;
        case 5: // HIDDEN — legacy, cursor skips this
          _ui->drawFrameEmpty(); _ui->drawFrameEmpty();
          _ui->drawFrameEmpty(); _ui->drawFrameEmpty();
          break;
        case 6: // HIDDEN — legacy, cursor skips this
          _ui->drawFrameEmpty(); _ui->drawFrameEmpty();
          _ui->drawFrameEmpty(); _ui->drawFrameEmpty();
          break;
        case 7:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Arpeg FG \xe2\x80\x94 Playing" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Solid intensity between tick flashes when playing." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Keep below 100%% so tick flashes pop visually." VT_RESET);
          _ui->drawFrameEmpty();
          break;
        case 8:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Arpeg BG \xe2\x80\x94 Playing" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Solid dim intensity for background arp banks when" VT_RESET);
          _ui->drawFrameLine(VT_DIM "playing. Tick flashes overlay on top." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Keep dimmer than foreground playing." VT_RESET);
          break;
        case 9: // HIDDEN — legacy, cursor skips this
          _ui->drawFrameEmpty(); _ui->drawFrameEmpty();
          _ui->drawFrameEmpty(); _ui->drawFrameEmpty();
          break;
        case 10:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Tick Flash \xe2\x80\x94 Foreground" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Brief spike on each arpeggiator step on the active bank." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Punctuates the rhythm visually. Try Amber or Coral for" VT_RESET);
          _ui->drawFrameLine(VT_DIM "a warmer rhythmic feel. Press [b] to preview." VT_RESET);
          break;
        case 11:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Tick Flash \xe2\x80\x94 Background" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Flash on background arp LEDs at each step. Dimmer than" VT_RESET);
          _ui->drawFrameLine(VT_DIM "foreground flash. Lets you see the rhythm of background" VT_RESET);
          _ui->drawFrameLine(VT_DIM "arps without overpowering the active bank." VT_RESET);
          break;
        case 12:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank Switch" VT_RESET);
          _ui->drawFrameLine(VT_DIM "The blink when you switch banks. This color flashes on the" VT_RESET);
          _ui->drawFrameLine(VT_DIM "destination LED to confirm the switch. Blink count and" VT_RESET);
          _ui->drawFrameLine(VT_DIM "duration are on the CONFIRM page. Press [b] to preview." VT_RESET);
          break;
        case 13:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Root" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Flash when you change the scale root note (hold left +" VT_RESET);
          _ui->drawFrameLine(VT_DIM "root pad). A vivid color helps distinguish root changes" VT_RESET);
          _ui->drawFrameLine(VT_DIM "from mode changes. Press [b] to preview." VT_RESET);
          break;
        case 14:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Mode" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Flash when you change the scale mode (Ionian, Dorian, etc.)." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Use a slightly different shade from root to tell them apart" VT_RESET);
          _ui->drawFrameLine(VT_DIM "at a glance. Press [b] to preview." VT_RESET);
          break;
        case 15:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Chromatic" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Flash when toggling chromatic mode on/off. A distinct color" VT_RESET);
          _ui->drawFrameLine(VT_DIM "confirms you've left scale mode and entered free chromatic" VT_RESET);
          _ui->drawFrameLine(VT_DIM "play. Press [b] to preview." VT_RESET);
          break;
        case 16:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Hold ON (fade in)" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Color of the fade IN when capturing arp (OFF->ON)." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Duration set in CONFIRM page (0-1000ms)." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Press [b] to preview." VT_RESET);
          break;
        case 17:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Hold OFF (fade out)" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Color of the fade OUT when releasing arp (ON->OFF)." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Can differ from Hold ON for directional feedback." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Press [b] to preview." VT_RESET);
          break;
        case 18:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Octave" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Blink overlay on current bank LED when changing" VT_RESET);
          _ui->drawFrameLine(VT_DIM "arp octave range (1-4). Press [b] to preview." VT_RESET);
          _ui->drawFrameEmpty();
          break;
      }
    } else {
      // TIMING rows (19-21)
      if (_cursor == 19) {
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Pulse Period" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Duration of one full sine breathing cycle in milliseconds." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Affects all ARPEG banks (foreground + background)." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Lower = faster breath. Default 1472ms (~1.5s)." VT_RESET);
      } else if (_cursor == 20) {
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Tick Flash Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "How long each arp step flash stays visible in milliseconds." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Shorter = snappier visual beat. Longer = more visible at" VT_RESET);
        _ui->drawFrameLine(VT_DIM "fast tempos. Default 30ms." VT_RESET);
      } else {
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Gamma Curve" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Controls low-light LED response. Lower = more levels at" VT_RESET);
        _ui->drawFrameLine(VT_DIM "low brightness (smoother pulse on dark stages). Higher =" VT_RESET);
        _ui->drawFrameLine(VT_DIM "deeper blacks. Range 1.0-3.0, default 2.0." VT_RESET);
      }
    }
  } else {
    // PAGE 1: CONFIRM
    switch (_cursor) {
      case 0:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank Switch \xe2\x80\x94 Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Number of ON-OFF blink cycles when switching banks." VT_RESET);
        _ui->drawFrameLine(VT_DIM "The destination bank LED blinks in the color configured" VT_RESET);
        _ui->drawFrameLine(VT_DIM "on the COLOR page. Range 1-3." VT_RESET);
        break;
      case 1:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank Switch \xe2\x80\x94 Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total time for all bank switch blinks in milliseconds." VT_RESET);
        _ui->drawFrameLine(VT_DIM "More blinks at same duration = faster individual blinks." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 100-500ms." VT_RESET);
        break;
      case 2:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Root \xe2\x80\x94 Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "ON-OFF blink cycles when changing scale root note." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 1-3." VT_RESET);
        _ui->drawFrameEmpty();
        break;
      case 3:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Root \xe2\x80\x94 Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total time for scale root blinks. Range 100-500ms." VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        break;
      case 4:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Mode \xe2\x80\x94 Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "ON-OFF blink cycles when changing scale mode." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 1-3." VT_RESET);
        _ui->drawFrameEmpty();
        break;
      case 5:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Mode \xe2\x80\x94 Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total time for scale mode blinks. Range 100-500ms." VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        break;
      case 6:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Chromatic \xe2\x80\x94 Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "ON-OFF blink cycles when toggling chromatic mode." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 1-3." VT_RESET);
        _ui->drawFrameEmpty();
        break;
      case 7:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Chromatic \xe2\x80\x94 Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total time for chromatic blinks. Range 100-500ms." VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        break;
      case 8:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Hold \xe2\x80\x94 Fade Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Shared for ON (fade in) and OFF (fade out)." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 100-600ms. Shorter = snappier toggle." VT_RESET);
        _ui->drawFrameEmpty();
        break;
      case 9:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Octave \xe2\x80\x94 Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Flashes on current bank LED. Range 1-3." VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        break;
      case 10:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Octave \xe2\x80\x94 Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total time for octave blinks. Range 100-500ms." VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        break;
      default:
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        break;
    }
  }
}

// =================================================================
// Preview — map cursor to preview row
// =================================================================
// Pot input — generic: seed loads field→_potVal, pot modifies _potVal,
// applyPotValues copies _potVal→field. No per-field switch needed in pot code.
// =================================================================
void ToolLedSettings::seedPotsForCursor() {
  _pots.disable(0);
  _pots.disable(1);

  if (_page == 0 && _cursor < 19) {
    // COLOR rows: pot 1 = hue, pot 2 = intensity
    uint8_t slotId = COLOR_ROWS[_cursor].slotId;
    _potVal[0] = _cwk.slots[slotId].hueOffset;
    _pots.seed(0, &_potVal[0], -128, 127, POT_ABSOLUTE);
    if (rowHasEditableIntensity(_cursor)) {
      _potVal[1] = getRowIntensity(_cursor);
      _pots.seed(1, &_potVal[1], 0, 100, POT_ABSOLUTE);
    }
  } else if (_page == 0 && _cursor == 19) {
    _potVal[0] = _wk.pulsePeriodMs;
    _pots.seed(0, &_potVal[0], 500, 4000, POT_ABSOLUTE);
  } else if (_page == 0 && _cursor == 20) {
    _potVal[0] = _wk.tickFlashDurationMs;
    _pots.seed(0, &_potVal[0], 10, 100, POT_ABSOLUTE);
  } else if (_page == 0 && _cursor == 21) {
    _potVal[0] = _wk.gammaTenths;
    _pots.seed(0, &_potVal[0], 10, 30, POT_ABSOLUTE);
  } else if (_page == 1) {
    // CONFIRM page: pot 1 = current param value
    _potVal[0] = 0;
    int32_t mn = 0, mx = 0;
    switch (_cursor) {
      case 0:  _potVal[0] = _wk.bankBlinks;          mn=1; mx=3; break;
      case 1:  _potVal[0] = _wk.bankDurationMs;       mn=100; mx=500; break;
      case 2:  _potVal[0] = _wk.scaleRootBlinks;      mn=1; mx=3; break;
      case 3:  _potVal[0] = _wk.scaleRootDurationMs;  mn=100; mx=500; break;
      case 4:  _potVal[0] = _wk.scaleModeBlinks;      mn=1; mx=3; break;
      case 5:  _potVal[0] = _wk.scaleModeDurationMs;  mn=100; mx=500; break;
      case 6:  _potVal[0] = _wk.scaleChromBlinks;     mn=1; mx=3; break;
      case 7:  _potVal[0] = _wk.scaleChromDurationMs; mn=100; mx=500; break;
      case 8:  _potVal[0] = _wk.holdOnFadeMs;         mn=0; mx=1000; break;
      case 9:  _potVal[0] = _wk.holdOffFadeMs;        mn=0; mx=1000; break;
      case 10: _potVal[0] = _wk.octaveBlinks;         mn=1; mx=3; break;
      case 11: _potVal[0] = _wk.octaveDurationMs;     mn=100; mx=500; break;
      default: return;
    }
    _pots.seed(0, &_potVal[0], mn, mx, POT_ABSOLUTE);
  }
}

// Copy _potVal back into the actual fields. Generic — only needs to know which field.
bool ToolLedSettings::applyPotValues() {
  bool changed = false;

  if (_page == 0 && _cursor < 19) {
    uint8_t slotId = COLOR_ROWS[_cursor].slotId;
    if (_pots.isActive(0) && (int8_t)_potVal[0] != _cwk.slots[slotId].hueOffset) {
      _cwk.slots[slotId].hueOffset = (int8_t)_potVal[0];
      changed = true;
    }
    if (_pots.isActive(1) && rowHasEditableIntensity(_cursor) &&
        (uint8_t)_potVal[1] != getRowIntensity(_cursor)) {
      setRowIntensity(_cursor, (uint8_t)_potVal[1]);
      changed = true;
    }
  } else if (_page == 0 && _cursor == 19) {
    if (_pots.isActive(0) && (uint16_t)_potVal[0] != _wk.pulsePeriodMs) {
      _wk.pulsePeriodMs = (uint16_t)_potVal[0]; changed = true;
    }
  } else if (_page == 0 && _cursor == 20) {
    if (_pots.isActive(0) && (uint8_t)_potVal[0] != _wk.tickFlashDurationMs) {
      _wk.tickFlashDurationMs = (uint8_t)_potVal[0]; changed = true;
    }
  } else if (_page == 0 && _cursor == 21) {
    if (_pots.isActive(0) && (uint8_t)_potVal[0] != _wk.gammaTenths) {
      _wk.gammaTenths = (uint8_t)_potVal[0]; changed = true;
    }
  } else if (_page == 1 && _pots.isActive(0)) {
    int32_t v = _potVal[0];
    switch (_cursor) {
      case 0:  if (_wk.bankBlinks != v)          { _wk.bankBlinks = v; changed=true; } break;
      case 1:  if (_wk.bankDurationMs != v)       { _wk.bankDurationMs = v; changed=true; } break;
      case 2:  if (_wk.scaleRootBlinks != v)      { _wk.scaleRootBlinks = v; changed=true; } break;
      case 3:  if (_wk.scaleRootDurationMs != v)  { _wk.scaleRootDurationMs = v; changed=true; } break;
      case 4:  if (_wk.scaleModeBlinks != v)      { _wk.scaleModeBlinks = v; changed=true; } break;
      case 5:  if (_wk.scaleModeDurationMs != v)  { _wk.scaleModeDurationMs = v; changed=true; } break;
      case 6:  if (_wk.scaleChromBlinks != v)     { _wk.scaleChromBlinks = v; changed=true; } break;
      case 7:  if (_wk.scaleChromDurationMs != v) { _wk.scaleChromDurationMs = v; changed=true; } break;
      case 8:  if (_wk.holdOnFadeMs != v)         { _wk.holdOnFadeMs = v; changed=true; } break;
      case 9:  if (_wk.holdOffFadeMs != v)        { _wk.holdOffFadeMs = v; changed=true; } break;
      case 10: if (_wk.octaveBlinks != v)         { _wk.octaveBlinks = v; changed=true; } break;
      case 11: if (_wk.octaveDurationMs != v)     { _wk.octaveDurationMs = v; changed=true; } break;
      default: break;
    }
  }
  return changed;
}

// =================================================================
// Preview — map cursor to preview row
// =================================================================
uint8_t ToolLedSettings::mapCursorToPreviewRow() const {
  if (_page == 0) {
    if (_cursor < 19) return _cursor;
    if (_cursor == 19) return 2;   // pulse period → arpeg preview
    if (_cursor == 20) return 10;  // tick flash duration → tick flash preview
  } else {
    // CONFIRM page: map cursor to relevant color row
    switch (_cursor) {
      case 0: case 1: return 12;       // bank switch
      case 2: case 3: return 13;       // scale root
      case 4: case 5: return 14;       // scale mode
      case 6: case 7: return 15;       // scale chromatic
      case 8: return 16;                // hold ON (fade in)
      case 9: return 17;                // hold OFF (fade out)
      case 10: case 11: return 18;     // octave
    }
  }
  return 0xFF;
}

// =================================================================
// Preview — continuous (NORMAL solid, ARPEG pulse)
// =================================================================
void ToolLedSettings::renderContinuousPreview(unsigned long now) {
  if (!_leds) return;
  _leds->previewClear();

  if (_cursor <= 1) {
    // NORMAL: solid on LEDs 3-4
    RGBW fgCol = resolveColorSlot(_cwk.slots[CSLOT_NORMAL_FG]);
    RGBW bgCol = resolveColorSlot(_cwk.slots[CSLOT_NORMAL_BG]);
    _leds->previewSetPixel(3, bgCol, getRowIntensity(1));
    _leds->previewSetPixel(4, fgCol, getRowIntensity(0));
  } else {
    // ARPEG: sine pulse on LEDs 3-4
    // Rows 2-5 = stopped intensities, rows 6-9 = playing intensities
    // Show whichever group the cursor is on (play or stop)
    RGBW fgCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_FG]);
    RGBW bgCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_BG]);
    uint16_t period = _wk.pulsePeriodMs;
    uint8_t lutStep = period / 256;
    uint8_t sineIdx = (uint8_t)((now / (lutStep > 0 ? lutStep : 1)) % 256);
    uint8_t sineRaw = LED_SINE_LUT[sineIdx];

    bool playing = (_cursor >= 6);
    uint8_t fgMin = getRowIntensity(playing ? 6 : 2);
    uint8_t fgMax = getRowIntensity(playing ? 7 : 3);
    uint8_t bgMin = getRowIntensity(playing ? 8 : 4);
    uint8_t bgMax = getRowIntensity(playing ? 9 : 5);
    uint8_t fgI = fgMin + (uint8_t)((uint16_t)sineRaw * (fgMax - fgMin) / 255);
    uint8_t bgI = bgMin + (uint8_t)((uint16_t)sineRaw * (bgMax - bgMin) / 255);
    _leds->previewSetPixel(3, bgCol, bgI);
    _leds->previewSetPixel(4, fgCol, fgI);
  }
  _leds->previewShow();
}

// =================================================================
// Preview — event-based (triggered by 'b')
// =================================================================
void ToolLedSettings::startEventPreview(uint8_t row) {
  if (row == 0xFF) return;
  if (row <= 9) return;  // continuous rows, ignore b
  _prevState = PREV_EVENT;
  _prevStart = millis();
  _prevEventRow = row;
}

void ToolLedSettings::renderEventPreview(unsigned long now) {
  if (!_leds) return;
  unsigned long elapsed = now - _prevStart;
  _leds->previewClear();

  switch (_prevEventRow) {
    case 10: case 11: {
      // Tick flash: pulse background + flash spike
      RGBW fgCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_FG]);
      RGBW bgCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_BG]);
      RGBW flashCol = resolveColorSlot(_cwk.slots[CSLOT_TICK_FLASH]);
      uint16_t period = _wk.pulsePeriodMs;
      uint8_t lutStep = period / 256;
      uint8_t sineIdx = (uint8_t)((now / (lutStep > 0 ? lutStep : 1)) % 256);
      uint8_t sineRaw = LED_SINE_LUT[sineIdx];

      uint8_t fgI = getRowIntensity(6) + (uint8_t)((uint16_t)sineRaw *
                     (getRowIntensity(7) - getRowIntensity(6)) / 255);
      uint8_t bgI = getRowIntensity(8) + (uint8_t)((uint16_t)sineRaw *
                     (getRowIntensity(9) - getRowIntensity(8)) / 255);

      // Flash every 500ms for the first 1.5s
      uint16_t flashMs = _wk.tickFlashDurationMs;
      bool flashing = ((elapsed % 500) < flashMs) && (elapsed < 1500);

      if (flashing && _prevEventRow == 10) {
        _leds->previewSetPixel(3, bgCol, bgI);
        _leds->previewSetPixel(4, flashCol, _wk.tickFlashFg);
      } else if (flashing && _prevEventRow == 11) {
        _leds->previewSetPixel(3, flashCol, _wk.tickFlashBg);
        _leds->previewSetPixel(4, fgCol, fgI);
      } else {
        _leds->previewSetPixel(3, bgCol, bgI);
        _leds->previewSetPixel(4, fgCol, fgI);
      }
      if (elapsed >= 1500) _prevState = PREV_IDLE;
      break;
    }
    case 12: {
      // Bank switch: LED 3 fade, LED 4 blink
      RGBW col = resolveColorSlot(_cwk.slots[CSLOT_BANK_SWITCH]);
      RGBW bgCol = resolveColorSlot(_cwk.slots[CSLOT_NORMAL_BG]);
      uint16_t dur = _wk.bankDurationMs;
      if (elapsed >= dur) { _prevState = PREV_IDLE; break; }
      // LED 3: fade from fg to bg intensity
      uint8_t fade3 = (uint8_t)((uint32_t)(dur - elapsed) * getRowIntensity(0) / dur);
      _leds->previewSetPixel(3, col, fade3);
      // LED 4: blink
      uint16_t unitMs = dur / (_wk.bankBlinks * 2);
      bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
      if (on) _leds->previewSetPixel(4, col, _wk.bankBrightnessPct);
      break;
    }
    case 13: case 14: case 15: {
      // Scale: blink on solid NORMAL fg
      RGBW fgCol = resolveColorSlot(_cwk.slots[CSLOT_NORMAL_FG]);
      uint8_t slotId = COLOR_ROWS[_prevEventRow].slotId;
      RGBW blinkCol = resolveColorSlot(_cwk.slots[slotId]);
      uint8_t blinks; uint16_t dur;
      if (_prevEventRow == 13) { blinks = _wk.scaleRootBlinks; dur = _wk.scaleRootDurationMs; }
      else if (_prevEventRow == 14) { blinks = _wk.scaleModeBlinks; dur = _wk.scaleModeDurationMs; }
      else { blinks = _wk.scaleChromBlinks; dur = _wk.scaleChromDurationMs; }
      if (elapsed >= dur) { _prevState = PREV_IDLE; break; }
      _leds->previewSetPixel(4, fgCol, getRowIntensity(0));  // solid context
      uint16_t unitMs = dur / (blinks * 2);
      bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
      if (on) _leds->previewSetPixel(4, blinkCol, 100);
      break;
    }
    case 16: {
      // Hold ON: fade IN on LED 4 (capture OFF->ON)
      RGBW holdOnCol = resolveColorSlot(_cwk.slots[CSLOT_HOLD_ON]);
      RGBW arpCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_FG]);
      uint16_t dur = _wk.holdOnFadeMs;
      if (dur == 0) dur = 1;  // guard division; near-instant
      if (elapsed >= dur) { _prevState = PREV_IDLE; break; }
      _leds->previewSetPixel(3, arpCol, getRowIntensity(2));  // Static FG arp context
      uint8_t pct = (uint8_t)((uint32_t)elapsed * 100 / dur);
      _leds->previewSetPixel(4, holdOnCol, pct);
      break;
    }
    case 17: {
      // Hold OFF: fade OUT on LED 4 (release ON->OFF)
      RGBW holdOffCol = resolveColorSlot(_cwk.slots[CSLOT_HOLD_OFF]);
      RGBW arpCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_FG]);
      uint16_t dur = _wk.holdOffFadeMs;
      if (dur == 0) dur = 1;  // guard division; near-instant
      if (elapsed >= dur) { _prevState = PREV_IDLE; break; }
      _leds->previewSetPixel(3, arpCol, getRowIntensity(2));  // Static FG arp context
      uint8_t pct = (uint8_t)((uint32_t)(dur - elapsed) * 100 / dur);
      _leds->previewSetPixel(4, holdOffCol, pct);
      break;
    }
    case 18: {
      // Octave: blink on single LED (not 2 LEDs)
      RGBW octCol = resolveColorSlot(_cwk.slots[CSLOT_OCTAVE]);
      RGBW normCol = resolveColorSlot(_cwk.slots[CSLOT_NORMAL_FG]);
      if (elapsed >= _wk.octaveDurationMs) { _prevState = PREV_IDLE; break; }
      _leds->previewSetPixel(3, normCol, getRowIntensity(0));  // Normal FG context
      uint16_t unitMs = _wk.octaveDurationMs / (_wk.octaveBlinks * 2);
      bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
      if (on) _leds->previewSetPixel(4, octCol, 100);
      break;
    }
    default:
      _prevState = PREV_IDLE;
      break;
  }
  _leds->previewShow();
}

// =================================================================
// Preview — main dispatcher
// =================================================================
void ToolLedSettings::updatePreview(unsigned long now) {
  if (!_leds) return;

  if (_prevState == PREV_EVENT) {
    renderEventPreview(now);
    return;
  }

  // Continuous preview: only on page 0, rows 0-9 (normal + arp stopped/playing)
  if (_page == 0 && _cursor <= 9) {
    renderContinuousPreview(now);
  } else {
    // No continuous preview — clear LEDs
    _leds->previewClear();
    _leds->previewShow();
  }
}

// =================================================================
// run() — Main tool loop
// =================================================================
void ToolLedSettings::run() {
  if (!_ui) return;

  // Load working copies from NVS or defaults
  _wk = s_ledDefaults();
  _cwk = s_colorDefaults();
  bool loadedFromNvs = NvsManager::loadBlob(LED_SETTINGS_NVS_NAMESPACE, LED_SETTINGS_NVS_KEY,
                                             EEPROM_MAGIC, LED_SETTINGS_VERSION, &_wk, sizeof(_wk));
  if (loadedFromNvs) {
    validateLedSettingsStore(_wk);
  }
  NvsManager::loadBlob(LED_SETTINGS_NVS_NAMESPACE, COLOR_SLOT_NVS_KEY,
                        COLOR_SLOT_MAGIC, COLOR_SLOT_VERSION, &_cwk, sizeof(_cwk));

  Serial.print(ITERM_RESIZE);

  LedSettingsStore originalWk = _wk;
  ColorSlotStore   originalCwk = _cwk;
  _nvsSaved = loadedFromNvs;
  _page = 0;
  _cursor = 0;
  _colorField = 0;
  _editing = false;
  _confirmDefaults = false;
  _prevState = PREV_IDLE;

  // Enter preview mode
  if (_leds) _leds->previewBegin();

  InputParser input;
  bool screenDirty = true;
  unsigned long lastPreviewMs = 0;
  unsigned long lastRenderMs = 0;
  unsigned long lastPotMs = 0;

  seedPotsForCursor();

  _ui->vtClear();

  while (true) {
    unsigned long now = millis();

    // Throttle preview updates to ~30ms
    if ((now - lastPreviewMs) >= 30) {
      lastPreviewMs = now;
      updatePreview(now);
    }

    // Pot input: refresh PotFilter then read stable values (~30ms)
    PotFilter::updateAll();
    if ((now - lastPotMs) >= 30) {
      lastPotMs = now;
      if (_pots.update()) {
        if (applyPotValues()) {
          _nvsSaved = false;
          screenDirty = true;
        }
      }
    }

    NavEvent ev = input.update();

    // --- Defaults confirmation ---
    if (_confirmDefaults) {
      ConfirmResult r = SetupUI::parseConfirm(ev);
      if (r == CONFIRM_YES) {
        _wk = s_ledDefaults();
        _cwk = s_colorDefaults();
        _nvsSaved = saveLedSettings() && saveColorSlots();
        originalWk = _wk;
        originalCwk = _cwk;
        _ui->flashSaved();
        _confirmDefaults = false;
        screenDirty = true;
      } else if (r == CONFIRM_NO) {
        _confirmDefaults = false;
        screenDirty = true;
      }
      delay(5);
      continue;
    }

    // --- Quit ---
    if (ev.type == NAV_QUIT) {
      if (_editing) {
        _wk = originalWk;
        _cwk = originalCwk;
        _editing = false;
        seedPotsForCursor();
        screenDirty = true;
      } else {
        if (_leds) _leds->previewEnd();
        _ui->vtClear();
        return;
      }
    }

    // --- Defaults ---
    if (ev.type == NAV_DEFAULTS && !_editing) {
      _confirmDefaults = true;
      screenDirty = true;
    }

    // --- 'b' preview (all pages) ---
    if (ev.type == NAV_CHAR && ev.ch == 'b' && !_editing) {
      uint8_t row = mapCursorToPreviewRow();
      startEventPreview(row);
    }

    // --- Page toggle ---
    if (ev.type == NAV_TOGGLE && !_editing) {
      _page = 1 - _page;
      _cursor = 0;
      _colorField = 0;
      seedPotsForCursor();
      screenDirty = true;
    }

    uint8_t count = pageParamCount();

    // --- Navigation ---
    if (!_editing) {
      if (ev.type == NAV_UP) {
        if (_cursor > 0) _cursor--;
        else _cursor = count - 1;
        // Skip hidden color rows on page 0
        while (_page == 0 && _cursor < 19 && isColorRowHidden(_cursor)) {
          if (_cursor > 0) _cursor--; else _cursor = count - 1;
        }
        _colorField = 0;
        seedPotsForCursor();
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        if (_cursor < count - 1) _cursor++;
        else _cursor = 0;
        while (_page == 0 && _cursor < 19 && isColorRowHidden(_cursor)) {
          if (_cursor < count - 1) _cursor++; else _cursor = 0;
        }
        _colorField = 0;
        seedPotsForCursor();
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        _editing = true;
        _colorField = 0;
        screenDirty = true;
      }
    } else {
      // --- Editing ---
      if (_page == 0 && _cursor < 19) {
        // COLOR row: ◄► = preset only (pot handles hue + intensity)
        if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
          int8_t dir = ev.type == NAV_RIGHT ? +1 : -1;
          uint8_t slotId = COLOR_ROWS[_cursor].slotId;
          ColorSlot& slot = _cwk.slots[slotId];
          int val = (int)slot.presetId + dir;
          if (val < 0) val = COLOR_PRESET_COUNT - 1;
          if (val >= COLOR_PRESET_COUNT) val = 0;
          slot.presetId = (uint8_t)val;
          slot.hueOffset = 0;   // Reset hue on preset change
          seedPotsForCursor();  // Reseed pot to new hue=0 (deactivates until moved)
          _nvsSaved = false;
          screenDirty = true;
        }
        if (ev.type == NAV_ENTER) {
          bool ok = saveLedSettings() && saveColorSlots();
          _editing = false;
          _nvsSaved = ok;
          if (ok) _ui->flashSaved();
          originalWk = _wk;
          originalCwk = _cwk;
          screenDirty = true;
        }
      } else if (_page == 0 && _cursor >= 19) {
        // Timing rows: ◄► adjust + reseed pot to prevent fight
        if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
          adjustTimingParam(ev.type == NAV_RIGHT ? +1 : -1, ev.accelerated);
          seedPotsForCursor();  // Reseed pot (deactivates until moved)
          screenDirty = true;
        }
        if (ev.type == NAV_ENTER) {
          bool ok = saveLedSettings() && saveColorSlots();
          _editing = false;
          _nvsSaved = ok;
          if (ok) _ui->flashSaved();
          originalWk = _wk;
          originalCwk = _cwk;
          screenDirty = true;
        }
      } else {
        // CONFIRM page: ◄► adjust + reseed pot to prevent fight
        if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
          adjustConfirmParam(ev.type == NAV_RIGHT ? +1 : -1, ev.accelerated);
          seedPotsForCursor();  // Reseed pot (deactivates until moved)
          screenDirty = true;
        }
        if (ev.type == NAV_ENTER) {
          bool ok = saveLedSettings() && saveColorSlots();
          _editing = false;
          _nvsSaved = ok;
          if (ok) _ui->flashSaved();
          originalWk = _wk;
          originalCwk = _cwk;
          screenDirty = true;
        }
      }
    }

    // --- Render (throttled: 80ms min between frames for key repeat) ---
    // yield() in drawFrameLine protects against watchdog timeout.
    if (screenDirty && (now - lastRenderMs) >= 80) {
      screenDirty = false;
      lastRenderMs = now;

      _ui->vtFrameStart();

      char headerBuf[64];
      snprintf(headerBuf, sizeof(headerBuf), "TOOL 7: LED SETTINGS  [%s]", s_pageNames[_page]);
      _ui->drawConsoleHeader(headerBuf, _nvsSaved);
      _ui->drawFrameEmpty();

      if (_page == 0) {
        // ============ PAGE 0: COLOR + TIMING ============

        _ui->drawSection("DISPLAY \xe2\x80\x94 STOPPED");
        for (uint8_t r = 0; r < 6; r++) {
          if (isColorRowHidden(r)) continue;
          bool sel = (_cursor == r);
          bool edt = sel && _editing;
          drawColorRow(r, sel, edt);
        }
        _ui->drawSection("DISPLAY \xe2\x80\x94 PLAYING");
        for (uint8_t r = 6; r < 10; r++) {
          if (isColorRowHidden(r)) continue;
          bool sel = (_cursor == r);
          bool edt = sel && _editing;
          drawColorRow(r, sel, edt);
        }
        _ui->drawSection("EVENTS");
        for (uint8_t r = 10; r < 19; r++) {
          bool sel = (_cursor == r);
          bool edt = sel && _editing;
          drawColorRow(r, sel, edt);
        }
        _ui->drawFrameEmpty();

        _ui->drawSection("TIMING");
        char buf[32];

        // Pulse period
        {
          bool sel = (_cursor == 19);
          bool edt = sel && _editing;
          snprintf(buf, sizeof(buf), "%d ms", _wk.pulsePeriodMs);
          if (edt) {
            _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-32s" VT_CYAN "[%s]" VT_RESET, "Pulse period:", buf);
          } else if (sel) {
            _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-32s" VT_BRIGHT_WHITE "%s" VT_RESET, "Pulse period:", buf);
          } else {
            _ui->drawFrameLine("  %-32s%s", "Pulse period:", buf);
          }
        }

        // Tick flash duration
        {
          bool sel = (_cursor == 20);
          bool edt = sel && _editing;
          snprintf(buf, sizeof(buf), "%d ms", _wk.tickFlashDurationMs);
          if (edt) {
            _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-32s" VT_CYAN "[%s]" VT_RESET, "Tick flash duration:", buf);
          } else if (sel) {
            _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-32s" VT_BRIGHT_WHITE "%s" VT_RESET, "Tick flash duration:", buf);
          } else {
            _ui->drawFrameLine("  %-32s%s", "Tick flash duration:", buf);
          }
        }

        // Gamma curve
        {
          bool sel = (_cursor == 21);
          bool edt = sel && _editing;
          snprintf(buf, sizeof(buf), "%.1f", (float)_wk.gammaTenths / 10.0f);
          if (edt) {
            _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-32s" VT_CYAN "[%.1f]" VT_RESET, "Gamma curve:", (float)_wk.gammaTenths / 10.0f);
          } else if (sel) {
            _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-32s" VT_BRIGHT_WHITE "%s" VT_RESET, "Gamma curve:", buf);
          } else {
            _ui->drawFrameLine("  %-32s%s", "Gamma curve:", buf);
          }
        }

      } else {
        // ============ PAGE 1: CONFIRM ============
        char buf[32];

        auto drawParam = [&](uint8_t idx, const char* label, const char* value) {
          bool selected = (_cursor == idx);
          bool isEditing = selected && _editing;
          if (isEditing) {
            _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-32s" VT_CYAN "[%s]" VT_RESET, label, value);
          } else if (selected) {
            _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-32s" VT_BRIGHT_WHITE "%s" VT_RESET, label, value);
          } else {
            _ui->drawFrameLine("  %-32s%s", label, value);
          }
        };

        _ui->drawSection("BANK SWITCH");
        snprintf(buf, sizeof(buf), "%d", _wk.bankBlinks);
        drawParam(0, "Blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.bankDurationMs);
        drawParam(1, "Duration:", buf);
        _ui->drawFrameEmpty();

        _ui->drawSection("SCALE");
        snprintf(buf, sizeof(buf), "%d", _wk.scaleRootBlinks);
        drawParam(2, "Root blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.scaleRootDurationMs);
        drawParam(3, "Root duration:", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.scaleModeBlinks);
        drawParam(4, "Mode blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.scaleModeDurationMs);
        drawParam(5, "Mode duration:", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.scaleChromBlinks);
        drawParam(6, "Chromatic blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.scaleChromDurationMs);
        drawParam(7, "Chromatic duration:", buf);
        _ui->drawFrameEmpty();

        _ui->drawSection("HOLD");
        snprintf(buf, sizeof(buf), "%d ms", _wk.holdOnFadeMs);
        drawParam(8, "Fade IN (ON):", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.holdOffFadeMs);
        drawParam(9, "Fade OUT (OFF):", buf);
        _ui->drawFrameEmpty();

        _ui->drawSection("OCTAVE");
        snprintf(buf, sizeof(buf), "%d", _wk.octaveBlinks);
        drawParam(10, "Blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.octaveDurationMs);
        drawParam(11, "Duration:", buf);
      }

      _ui->drawFrameEmpty();

      // --- INFO ---
      _ui->drawSection("INFO");
      if (_confirmDefaults) {
        _ui->drawFrameLine(VT_YELLOW "Reset ALL LED settings to factory defaults? (y/n)" VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
      } else {
        drawDescription();
      }

      _ui->drawFrameEmpty();

      // --- Control bar ---
      if (_confirmDefaults) {
        _ui->drawControlBar(CBAR_CONFIRM_ANY);
      } else if (_editing) {
        if (_page == 0 && _cursor < 19) {
          _ui->drawControlBar(VT_DIM "[</>] PRESET  [P1] HUE  [P2] INT" CBAR_SEP "[RET] SAVE" CBAR_SEP "[q] CANCEL" VT_RESET);
        } else {
          _ui->drawControlBar(VT_DIM "[</>] ADJUST" CBAR_SEP "[RET] SAVE" CBAR_SEP "[q] CANCEL" VT_RESET);
        }
      } else {
        char ctrlBuf[256];
        snprintf(ctrlBuf, sizeof(ctrlBuf),
                 VT_DIM "[^v] NAV  [RET] SAVE  [b] PREVIEW  " VT_RESET
                 "%s" VT_DIM "%s"
                 "  [t] " VT_RESET VT_BRIGHT_WHITE "%s" VT_RESET VT_DIM "  [d] DFLT  [q] EXIT" VT_RESET,
                 _pots.isEnabled(0) ? (_pots.isActive(0) ? VT_BRIGHT_WHITE "[P1]" VT_RESET " " : VT_DIM "[P1] ") : "",
                 _pots.isEnabled(1) ? (_pots.isActive(1) ? VT_BRIGHT_WHITE "[P2]" VT_RESET " " : VT_DIM "[P2] ") : "",
                 s_pageNames[1 - _page]);
        _ui->drawControlBar(ctrlBuf);
      }

      _ui->vtFrameEnd();
    }

    delay(5);
  }
}
