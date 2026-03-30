#include "ToolLedSettings.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"
#include "InputParser.h"
#include <Arduino.h>
#include <Preferences.h>

// =================================================================
// Page definitions
// =================================================================
// Page 0: COLOR (16 rows) + TIMING (2 rows) = 18
// Page 1: CONFIRM — 15 params
static const uint8_t PAGE0_COUNT = 18;
static const uint8_t PAGE1_COUNT = 15;

static const char* s_pageNames[] = {"COLOR", "CONFIRM"};

// =================================================================
// COLOR row mapping: 16 rows → 13 color slots
// =================================================================
struct ColorRowDef {
  uint8_t     slotId;       // ColorSlotId
  uint8_t     partnerRow;   // 0xFF = no partner
  const char* label;
};

static const ColorRowDef COLOR_ROWS[16] = {
  { CSLOT_NORMAL_FG,   0xFF, "NORMAL fg"       },  // 0
  { CSLOT_NORMAL_BG,   0xFF, "NORMAL bg"       },  // 1
  { CSLOT_ARPEG_FG,       3, "ARPEG fg min"    },  // 2
  { CSLOT_ARPEG_FG,       2, "ARPEG fg max"    },  // 3
  { CSLOT_ARPEG_BG,       5, "ARPEG bg min"    },  // 4
  { CSLOT_ARPEG_BG,       4, "ARPEG bg max"    },  // 5
  { CSLOT_TICK_FLASH,     7, "Tick flash fg"   },  // 6
  { CSLOT_TICK_FLASH,     6, "Tick flash bg"   },  // 7
  { CSLOT_BANK_SWITCH, 0xFF, "Bank switch"     },  // 8
  { CSLOT_SCALE_ROOT,  0xFF, "Scale root"      },  // 9
  { CSLOT_SCALE_MODE,  0xFF, "Scale mode"      },  // 10
  { CSLOT_SCALE_CHROM, 0xFF, "Scale chromatic" },  // 11
  { CSLOT_HOLD,        0xFF, "Hold"            },  // 12
  { CSLOT_PLAY_ACK,    0xFF, "Play ack"        },  // 13
  { CSLOT_STOP,        0xFF, "Stop"            },  // 14
  { CSLOT_OCTAVE,      0xFF, "Octave"          },  // 15
};

// =================================================================
// Sine LUT (local copy for preview, independent of LedController)
// =================================================================
static const uint8_t SINE_LUT[256] = {
  128,131,134,137,140,143,146,149,152,155,158,162,165,167,170,173,
  176,179,182,185,188,190,193,196,198,201,203,206,208,211,213,215,
  218,220,222,224,226,228,230,232,234,235,237,238,240,241,243,244,
  245,246,248,249,250,250,251,252,253,253,254,254,254,255,255,255,
  255,255,255,255,254,254,254,253,253,252,251,250,250,249,248,246,
  245,244,243,241,240,238,237,235,234,232,230,228,226,224,222,220,
  218,215,213,211,208,206,203,201,198,196,193,190,188,185,182,179,
  176,173,170,167,165,162,158,155,152,149,146,143,140,137,134,131,
  128,124,121,118,115,112,109,106,103,100, 97, 93, 90, 88, 85, 82,
   79, 76, 73, 70, 67, 65, 62, 59, 57, 54, 52, 49, 47, 44, 42, 40,
   37, 35, 33, 31, 29, 27, 25, 23, 21, 20, 18, 17, 15, 14, 12, 11,
   10,  9,  7,  6,  5,  5,  4,  3,  2,  2,  1,  1,  1,  0,  0,  0,
    0,  0,  0,  0,  1,  1,  1,  2,  2,  3,  4,  5,  5,  6,  7,  9,
   10, 11, 12, 14, 15, 17, 18, 20, 21, 23, 25, 27, 29, 31, 33, 35,
   37, 40, 42, 44, 47, 49, 52, 54, 57, 59, 62, 65, 67, 70, 73, 76,
   79, 82, 85, 88, 90, 93, 97,100,103,106,109,112,115,118,121,124
};

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
  d.bankBlinks         = 3;
  d.bankDurationMs     = 300;
  d.bankBrightnessPct  = 80;
  d.scaleRootBlinks    = 2;
  d.scaleRootDurationMs = 200;
  d.scaleModeBlinks    = 2;
  d.scaleModeDurationMs = 200;
  d.scaleChromBlinks   = 2;
  d.scaleChromDurationMs = 200;
  d.holdOnFlashMs      = 150;
  d.holdFadeMs         = 300;
  d.stopFadeMs         = 300;
  d.playBeatCount      = 3;
  d.octaveBlinks       = 3;
  d.octaveDurationMs   = 300;
  return d;
}

static ColorSlotStore s_colorDefaults() {
  ColorSlotStore c;
  c.magic = COLOR_SLOT_MAGIC;
  c.version = 1;
  c.reserved = 0;
  static const uint8_t presets[COLOR_SLOT_COUNT] = {
    0, 1, 3, 4, 0, 0, 6, 7, 7, 4, 11, 5, 9
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
  : _leds(nullptr), _nvs(nullptr), _ui(nullptr),
    _page(0), _cursor(0), _colorField(0),
    _editing(false), _nvsSaved(true), _confirmDefaults(false),
    _prevState(PREV_IDLE), _prevStart(0), _prevEventRow(0)
{
  memset(&_wk, 0, sizeof(_wk));
  memset(&_cwk, 0, sizeof(_cwk));
}

void ToolLedSettings::begin(LedController* leds, NvsManager* nvs, SetupUI* ui) {
  _leds = leds;
  _nvs = nvs;
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
    case 6:  return _wk.tickFlashFg;
    case 7:  return _wk.tickFlashBg;
    case 8:  return _wk.bankBrightnessPct;
    default: return 100;  // rows 9-15: fixed 100%
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
    case 6: _wk.tickFlashFg = val; break;
    case 7: _wk.tickFlashBg = val; break;
    case 8: _wk.bankBrightnessPct = val; break;
    default: break;  // rows 9-15: not editable
  }
}

bool ToolLedSettings::rowHasEditableIntensity(uint8_t row) const {
  return (row <= 8);
}

// =================================================================
// adjustColorField — preset / hue / intensity
// =================================================================
void ToolLedSettings::adjustColorField(int8_t dir, bool accel) {
  if (_cursor >= 16) return;  // timing rows handled elsewhere
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
  uint8_t timingIdx = _cursor - 16;  // 0 or 1
  if (timingIdx == 0) {  // pulsePeriodMs 500-4000
    int step = accel ? 250 : 50;
    int val = (int)_wk.pulsePeriodMs + dir * step;
    if (val < 500) val = 500;
    if (val > 4000) val = 4000;
    _wk.pulsePeriodMs = (uint16_t)val;
  } else {  // tickFlashDurationMs 10-100
    int step = accel ? 25 : 5;
    int val = (int)_wk.tickFlashDurationMs + dir * step;
    if (val < 10) val = 10;
    if (val > 100) val = 100;
    _wk.tickFlashDurationMs = (uint8_t)val;
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
    case 8: { // holdOnFlashMs 50-250
      int step = accel ? 50 : 10;
      int val = (int)_wk.holdOnFlashMs + dir * step;
      if (val < 50) val = 50; if (val > 250) val = 250;
      _wk.holdOnFlashMs = (uint8_t)val;
      break;
    }
    case 9: { // holdFadeMs 100-600
      int step = accel ? 100 : 50;
      int val = (int)_wk.holdFadeMs + dir * step;
      if (val < 100) val = 100; if (val > 600) val = 600;
      _wk.holdFadeMs = (uint16_t)val;
      break;
    }
    case 10: { // playBeatCount 1-4
      int val = (int)_wk.playBeatCount + dir;
      if (val < 1) val = 4; if (val > 4) val = 1;
      _wk.playBeatCount = (uint8_t)val;
      break;
    }
    case 11: { // stopFadeMs 100-600
      int step = accel ? 100 : 50;
      int val = (int)_wk.stopFadeMs + dir * step;
      if (val < 100) val = 100; if (val > 600) val = 600;
      _wk.stopFadeMs = (uint16_t)val;
      break;
    }
    case 12: { // octaveBlinks 1-3
      int val = (int)_wk.octaveBlinks + dir;
      if (val < 1) val = 3; if (val > 3) val = 1;
      _wk.octaveBlinks = (uint8_t)val;
      break;
    }
    case 13: { // octaveDurationMs 100-500
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
  Preferences prefs;
  if (!prefs.begin(LED_SETTINGS_NVS_NAMESPACE, false)) return false;
  prefs.putBytes(LED_SETTINGS_NVS_KEY, &_wk, sizeof(LedSettingsStore));
  prefs.end();
  if (_leds) _leds->loadLedSettings(_wk);
  return true;
}

bool ToolLedSettings::saveColorSlots() {
  _cwk.magic = COLOR_SLOT_MAGIC;
  _cwk.version = 1;
  Preferences prefs;
  if (!prefs.begin(LED_SETTINGS_NVS_NAMESPACE, false)) return false;
  prefs.putBytes(COLOR_SLOT_NVS_KEY, &_cwk, sizeof(ColorSlotStore));
  prefs.end();
  if (_leds) _leds->loadColorSlots(_cwk);
  return true;
}

// =================================================================
// drawColorRow — single row with ANSI 24-bit swatch
// =================================================================
void ToolLedSettings::drawColorRow(uint8_t row, bool selected, bool editing) {
  if (row >= 16) return;
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

  // Preset field
  if (editing && _colorField == 0) {
    snprintf(presetBuf, sizeof(presetBuf), VT_CYAN VT_BOLD "[%-12s]" VT_RESET, presetName);
  } else if (selected) {
    snprintf(presetBuf, sizeof(presetBuf), VT_BRIGHT_WHITE "%-13s " VT_RESET, presetName);
  } else {
    snprintf(presetBuf, sizeof(presetBuf), "%-13s ", presetName);
  }

  // Hue field
  if (editing && _colorField == 1) {
    snprintf(hueBuf, sizeof(hueBuf), VT_CYAN VT_BOLD "[%+4d" "\xc2\xb0]" VT_RESET, slot.hueOffset);
  } else if (selected) {
    snprintf(hueBuf, sizeof(hueBuf), VT_BRIGHT_WHITE "%+4d" "\xc2\xb0" VT_RESET, slot.hueOffset);
  } else {
    snprintf(hueBuf, sizeof(hueBuf), "%+4d\xc2\xb0", slot.hueOffset);
  }

  // Intensity field
  if (!hasInt) {
    intBuf[0] = '\0';
  } else if (editing && _colorField == 2) {
    snprintf(intBuf, sizeof(intBuf), VT_CYAN VT_BOLD " [%3d%%]" VT_RESET, intensity);
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
    if (_cursor < 16) {
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
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Arpeg Foreground \xe2\x80\x94 Pulse Low" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Lowest point of the breathing pulse on the active arpeggiator." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Set low (3-8%%) for a subtle breath that barely glows," VT_RESET);
          _ui->drawFrameLine(VT_DIM "higher (20-30%%) for an always-visible pulse." VT_RESET);
          break;
        case 3:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Arpeg Foreground \xe2\x80\x94 Pulse High" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Peak of the breathing pulse. The difference between min and" VT_RESET);
          _ui->drawFrameLine(VT_DIM "max defines the depth of the breathing effect. Wider gap =" VT_RESET);
          _ui->drawFrameLine(VT_DIM "deeper breath. Narrow gap = subtle shimmer." VT_RESET);
          break;
        case 4:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Arpeg Background \xe2\x80\x94 Pulse Low" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Lowest point of the breathing pulse on background arp banks." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Set to 0 for complete darkness at the bottom of each breath." VT_RESET);
          _ui->drawFrameLine(VT_DIM "These LEDs show background arps that keep playing." VT_RESET);
          break;
        case 5:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Arpeg Background \xe2\x80\x94 Pulse High" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Peak of the background arp pulse. Keep well below foreground" VT_RESET);
          _ui->drawFrameLine(VT_DIM "values to maintain clear hierarchy between active and" VT_RESET);
          _ui->drawFrameLine(VT_DIM "background arps." VT_RESET);
          break;
        case 6:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Tick Flash \xe2\x80\x94 Foreground" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Brief spike on each arpeggiator step on the active bank." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Punctuates the rhythm visually. Try Amber or Coral for" VT_RESET);
          _ui->drawFrameLine(VT_DIM "a warmer rhythmic feel. Press [b] to preview." VT_RESET);
          break;
        case 7:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Tick Flash \xe2\x80\x94 Background" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Flash on background arp LEDs at each step. Dimmer than" VT_RESET);
          _ui->drawFrameLine(VT_DIM "foreground flash. Lets you see the rhythm of background" VT_RESET);
          _ui->drawFrameLine(VT_DIM "arps without overpowering the active bank." VT_RESET);
          break;
        case 8:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank Switch" VT_RESET);
          _ui->drawFrameLine(VT_DIM "The blink when you switch banks. This color flashes on the" VT_RESET);
          _ui->drawFrameLine(VT_DIM "destination LED to confirm the switch. Blink count and" VT_RESET);
          _ui->drawFrameLine(VT_DIM "duration are on the CONFIRM page. Press [b] to preview." VT_RESET);
          break;
        case 9:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Root" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Flash when you change the scale root note (hold left +" VT_RESET);
          _ui->drawFrameLine(VT_DIM "root pad). A vivid color helps distinguish root changes" VT_RESET);
          _ui->drawFrameLine(VT_DIM "from mode changes. Press [b] to preview." VT_RESET);
          break;
        case 10:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Mode" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Flash when you change the scale mode (Ionian, Dorian, etc.)." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Use a slightly different shade from root to tell them apart" VT_RESET);
          _ui->drawFrameLine(VT_DIM "at a glance. Press [b] to preview." VT_RESET);
          break;
        case 11:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Chromatic" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Flash when toggling chromatic mode on/off. A distinct color" VT_RESET);
          _ui->drawFrameLine(VT_DIM "confirms you've left scale mode and entered free chromatic" VT_RESET);
          _ui->drawFrameLine(VT_DIM "play. Press [b] to preview." VT_RESET);
          break;
        case 12:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Hold" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Flash when toggling HOLD mode on an arpeggiator bank." VT_RESET);
          _ui->drawFrameLine(VT_DIM "A clear visual cue that your held notes are locked in." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Choose a color that pops against your arp pulse." VT_RESET);
          break;
        case 13:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Play Ack" VT_RESET);
          _ui->drawFrameLine(VT_DIM "The green flash when an arpeggiator starts playing." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Followed by beat-synced flashes. A quick 'go' signal." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Press [b] to preview the full play sequence." VT_RESET);
          break;
        case 14:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Stop" VT_RESET);
          _ui->drawFrameLine(VT_DIM "The fade-out when an arpeggiator stops. This color" VT_RESET);
          _ui->drawFrameLine(VT_DIM "smoothly fades to zero over the stop duration." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Press [b] to preview." VT_RESET);
          break;
        case 15:
          _ui->drawFrameLine(VT_BRIGHT_WHITE "Octave" VT_RESET);
          _ui->drawFrameLine(VT_DIM "Flash when changing arp octave range (1-4). Blinks on a" VT_RESET);
          _ui->drawFrameLine(VT_DIM "pair of LEDs matching the selected octave group." VT_RESET);
          _ui->drawFrameLine(VT_DIM "Press [b] to preview." VT_RESET);
          break;
      }
    } else {
      // TIMING rows (16-17)
      if (_cursor == 16) {
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Pulse Period" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Duration of one full sine breathing cycle in milliseconds." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Affects all ARPEG banks (foreground + background)." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Lower = faster breath. Default 1472ms (~1.5s)." VT_RESET);
      } else {
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Tick Flash Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "How long each arp step flash stays visible in milliseconds." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Shorter = snappier visual beat. Longer = more visible at" VT_RESET);
        _ui->drawFrameLine(VT_DIM "fast tempos. Default 30ms." VT_RESET);
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
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Hold ON \xe2\x80\x94 Flash Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Duration of the single flash when HOLD activates." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Includes ON phase + OFF gap. Range 50-250ms." VT_RESET);
        _ui->drawFrameEmpty();
        break;
      case 9:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Hold OFF \xe2\x80\x94 Fade Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Fade-out animation when HOLD deactivates." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Smooth fade to zero. Range 100-600ms." VT_RESET);
        _ui->drawFrameEmpty();
        break;
      case 10:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Play \xe2\x80\x94 Beat Count" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Number of beat-synced flashes after the play ack." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Green ack + N flashes synced to clock. Range 1-4." VT_RESET);
        _ui->drawFrameEmpty();
        break;
      case 11:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Stop \xe2\x80\x94 Fade Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Fade-out when arp transport stops. Range 100-600ms." VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        break;
      case 12:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Octave \xe2\x80\x94 Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "ON-OFF blink cycles when changing octave range." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Blinks on a 2-LED group. Range 1-3." VT_RESET);
        _ui->drawFrameEmpty();
        break;
      case 13:
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
// =================================================================
// Pot input — seed channels for current cursor position
// =================================================================
void ToolLedSettings::seedPotsForCursor() {
  if (_page == 0 && _cursor < 16) {
    // COLOR rows: pot 1 = hue (-128..127), pot 2 = intensity (0-100)
    uint8_t slotId = COLOR_ROWS[_cursor].slotId;
    _pots.seed(0, _cwk.slots[slotId].hueOffset, -128, 127);
    if (rowHasEditableIntensity(_cursor)) {
      _pots.seed(1, getRowIntensity(_cursor), 0, 100);
    } else {
      _pots.disable(1);
    }
  } else if (_page == 0 && _cursor == 16) {
    // Pulse period
    _pots.seed(0, _wk.pulsePeriodMs, 500, 4000);
    _pots.disable(1);
  } else if (_page == 0 && _cursor == 17) {
    // Tick flash duration
    _pots.seed(0, _wk.tickFlashDurationMs, 10, 100);
    _pots.disable(1);
  } else if (_page == 1) {
    // CONFIRM page: pot 1 = value of current param
    switch (_cursor) {
      case 0: _pots.seed(0, _wk.bankBlinks, 1, 3); break;
      case 1: _pots.seed(0, _wk.bankDurationMs, 100, 500); break;
      case 2: _pots.seed(0, _wk.scaleRootBlinks, 1, 3); break;
      case 3: _pots.seed(0, _wk.scaleRootDurationMs, 100, 500); break;
      case 4: _pots.seed(0, _wk.scaleModeBlinks, 1, 3); break;
      case 5: _pots.seed(0, _wk.scaleModeDurationMs, 100, 500); break;
      case 6: _pots.seed(0, _wk.scaleChromBlinks, 1, 3); break;
      case 7: _pots.seed(0, _wk.scaleChromDurationMs, 100, 500); break;
      case 8: _pots.seed(0, _wk.holdOnFlashMs, 50, 250); break;
      case 9: _pots.seed(0, _wk.holdFadeMs, 100, 600); break;
      case 10: _pots.seed(0, _wk.playBeatCount, 1, 4); break;
      case 11: _pots.seed(0, _wk.stopFadeMs, 100, 600); break;
      case 12: _pots.seed(0, _wk.octaveBlinks, 1, 3); break;
      case 13: _pots.seed(0, _wk.octaveDurationMs, 100, 500); break;
      default: _pots.disable(0); break;
    }
    _pots.disable(1);
  }
}

bool ToolLedSettings::applyPotValues() {
  bool changed = false;
  if (_page == 0 && _cursor < 16) {
    // Pot 0 → hue
    if (_pots.isActive(0)) {
      uint8_t slotId = COLOR_ROWS[_cursor].slotId;
      int8_t newHue = (int8_t)_pots.getValue(0);
      if (_cwk.slots[slotId].hueOffset != newHue) {
        _cwk.slots[slotId].hueOffset = newHue;
        changed = true;
      }
    }
    // Pot 1 → intensity
    if (_pots.isActive(1) && rowHasEditableIntensity(_cursor)) {
      uint8_t newInt = (uint8_t)_pots.getValue(1);
      if (getRowIntensity(_cursor) != newInt) {
        setRowIntensity(_cursor, newInt);
        changed = true;
      }
    }
  } else if (_page == 0 && _cursor == 16) {
    if (_pots.isActive(0)) {
      uint16_t v = (uint16_t)_pots.getValue(0);
      if (_wk.pulsePeriodMs != v) { _wk.pulsePeriodMs = v; changed = true; }
    }
  } else if (_page == 0 && _cursor == 17) {
    if (_pots.isActive(0)) {
      uint8_t v = (uint8_t)_pots.getValue(0);
      if (_wk.tickFlashDurationMs != v) { _wk.tickFlashDurationMs = v; changed = true; }
    }
  } else if (_page == 1) {
    if (_pots.isActive(0)) {
      int32_t v = _pots.getValue(0);
      switch (_cursor) {
        case 0: if (_wk.bankBlinks != v) { _wk.bankBlinks = v; changed = true; } break;
        case 1: if (_wk.bankDurationMs != v) { _wk.bankDurationMs = v; changed = true; } break;
        case 2: if (_wk.scaleRootBlinks != v) { _wk.scaleRootBlinks = v; changed = true; } break;
        case 3: if (_wk.scaleRootDurationMs != v) { _wk.scaleRootDurationMs = v; changed = true; } break;
        case 4: if (_wk.scaleModeBlinks != v) { _wk.scaleModeBlinks = v; changed = true; } break;
        case 5: if (_wk.scaleModeDurationMs != v) { _wk.scaleModeDurationMs = v; changed = true; } break;
        case 6: if (_wk.scaleChromBlinks != v) { _wk.scaleChromBlinks = v; changed = true; } break;
        case 7: if (_wk.scaleChromDurationMs != v) { _wk.scaleChromDurationMs = v; changed = true; } break;
        case 8: if (_wk.holdOnFlashMs != v) { _wk.holdOnFlashMs = v; changed = true; } break;
        case 9: if (_wk.holdFadeMs != v) { _wk.holdFadeMs = v; changed = true; } break;
        case 10: if (_wk.playBeatCount != v) { _wk.playBeatCount = v; changed = true; } break;
        case 11: if (_wk.stopFadeMs != v) { _wk.stopFadeMs = v; changed = true; } break;
        case 12: if (_wk.octaveBlinks != v) { _wk.octaveBlinks = v; changed = true; } break;
        case 13: if (_wk.octaveDurationMs != v) { _wk.octaveDurationMs = v; changed = true; } break;
        default: break;
      }
    }
  }
  return changed;
}

// =================================================================
// Preview — map cursor to preview row
// =================================================================
uint8_t ToolLedSettings::mapCursorToPreviewRow() const {
  if (_page == 0) {
    if (_cursor < 16) return _cursor;
    if (_cursor == 16) return 2;   // pulse period → arpeg preview
    if (_cursor == 17) return 6;   // tick flash duration → tick flash preview
  } else {
    // CONFIRM page: map cursor to relevant color row
    switch (_cursor) {
      case 0: case 1: return 8;     // bank switch
      case 2: case 3: return 9;     // scale root
      case 4: case 5: return 10;    // scale mode
      case 6: case 7: return 11;    // scale chromatic
      case 8: case 9: return 12;    // hold
      case 10: return 13;           // play
      case 11: return 14;           // stop
      case 12: case 13: return 15;  // octave
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
    RGBW fgCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_FG]);
    RGBW bgCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_BG]);
    uint16_t period = _wk.pulsePeriodMs;
    uint8_t lutStep = period / 256;
    uint8_t sineIdx = (uint8_t)((now / (lutStep > 0 ? lutStep : 1)) % 256);
    uint8_t sineRaw = SINE_LUT[sineIdx];

    uint8_t fgMin = getRowIntensity(2), fgMax = getRowIntensity(3);
    uint8_t bgMin = getRowIntensity(4), bgMax = getRowIntensity(5);
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
  if (row <= 5) return;  // continuous rows, ignore b
  _prevState = PREV_EVENT;
  _prevStart = millis();
  _prevEventRow = row;
}

void ToolLedSettings::renderEventPreview(unsigned long now) {
  if (!_leds) return;
  unsigned long elapsed = now - _prevStart;
  _leds->previewClear();

  switch (_prevEventRow) {
    case 6: case 7: {
      // Tick flash: pulse background + flash spike
      RGBW fgCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_FG]);
      RGBW bgCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_BG]);
      RGBW flashCol = resolveColorSlot(_cwk.slots[CSLOT_TICK_FLASH]);
      uint16_t period = _wk.pulsePeriodMs;
      uint8_t lutStep = period / 256;
      uint8_t sineIdx = (uint8_t)((now / (lutStep > 0 ? lutStep : 1)) % 256);
      uint8_t sineRaw = SINE_LUT[sineIdx];

      uint8_t fgI = getRowIntensity(2) + (uint8_t)((uint16_t)sineRaw *
                     (getRowIntensity(3) - getRowIntensity(2)) / 255);
      uint8_t bgI = getRowIntensity(4) + (uint8_t)((uint16_t)sineRaw *
                     (getRowIntensity(5) - getRowIntensity(4)) / 255);

      // Flash every 500ms for the first 1.5s
      uint16_t flashMs = _wk.tickFlashDurationMs;
      bool flashing = ((elapsed % 500) < flashMs) && (elapsed < 1500);

      if (flashing && _prevEventRow == 6) {
        _leds->previewSetPixel(3, bgCol, bgI);
        _leds->previewSetPixel(4, flashCol, _wk.tickFlashFg);
      } else if (flashing && _prevEventRow == 7) {
        _leds->previewSetPixel(3, flashCol, _wk.tickFlashBg);
        _leds->previewSetPixel(4, fgCol, fgI);
      } else {
        _leds->previewSetPixel(3, bgCol, bgI);
        _leds->previewSetPixel(4, fgCol, fgI);
      }
      if (elapsed >= 1500) _prevState = PREV_IDLE;
      break;
    }
    case 8: {
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
    case 9: case 10: case 11: {
      // Scale: blink on solid NORMAL fg
      RGBW fgCol = resolveColorSlot(_cwk.slots[CSLOT_NORMAL_FG]);
      uint8_t slotId = COLOR_ROWS[_prevEventRow].slotId;
      RGBW blinkCol = resolveColorSlot(_cwk.slots[slotId]);
      uint8_t blinks; uint16_t dur;
      if (_prevEventRow == 9) { blinks = _wk.scaleRootBlinks; dur = _wk.scaleRootDurationMs; }
      else if (_prevEventRow == 10) { blinks = _wk.scaleModeBlinks; dur = _wk.scaleModeDurationMs; }
      else { blinks = _wk.scaleChromBlinks; dur = _wk.scaleChromDurationMs; }
      if (elapsed >= dur) { _prevState = PREV_IDLE; break; }
      _leds->previewSetPixel(4, fgCol, getRowIntensity(0));  // solid context
      uint16_t unitMs = dur / (blinks * 2);
      bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
      if (on) _leds->previewSetPixel(4, blinkCol, 100);
      break;
    }
    case 12: {
      // Hold: flash + fade on pulse background
      RGBW holdCol = resolveColorSlot(_cwk.slots[CSLOT_HOLD]);
      RGBW arpCol = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_FG]);
      uint16_t totalDur = (uint16_t)_wk.holdOnFlashMs + _wk.holdFadeMs;
      if (elapsed >= totalDur) { _prevState = PREV_IDLE; break; }
      // Background pulse on LED 3
      uint8_t lutStep = _wk.pulsePeriodMs / 256;
      uint8_t sineIdx = (uint8_t)((now / (lutStep > 0 ? lutStep : 1)) % 256);
      uint8_t sineRaw = SINE_LUT[sineIdx];
      uint8_t bgI = getRowIntensity(4) + (uint8_t)((uint16_t)sineRaw *
                     (getRowIntensity(5) - getRowIntensity(4)) / 255);
      _leds->previewSetPixel(3, arpCol, bgI);
      // LED 4: hold flash then fade
      if (elapsed < _wk.holdOnFlashMs) {
        _leds->previewSetPixel(4, holdCol, 100);
      } else {
        uint16_t fadeElapsed = elapsed - _wk.holdOnFlashMs;
        uint8_t fadePct = (uint8_t)((uint32_t)(_wk.holdFadeMs - fadeElapsed) * 100 / _wk.holdFadeMs);
        _leds->previewSetPixel(4, holdCol, fadePct);
      }
      break;
    }
    case 13: {
      // Play: ack + beat flashes (time-based, 200ms/beat)
      RGBW ackCol = resolveColorSlot(_cwk.slots[CSLOT_PLAY_ACK]);
      RGBW beatCol = resolveColorSlot(_cwk.slots[CSLOT_STOP]);
      uint16_t totalDur = 100 + (uint16_t)_wk.playBeatCount * 200;
      if (elapsed >= totalDur) { _prevState = PREV_IDLE; break; }
      // Arp pulse on LED 3
      RGBW arpBg = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_BG]);
      uint8_t lutStep = _wk.pulsePeriodMs / 256;
      uint8_t sineIdx = (uint8_t)((now / (lutStep > 0 ? lutStep : 1)) % 256);
      uint8_t bgI = getRowIntensity(4) + (uint8_t)((uint16_t)SINE_LUT[sineIdx] *
                     (getRowIntensity(5) - getRowIntensity(4)) / 255);
      _leds->previewSetPixel(3, arpBg, bgI);
      if (elapsed < 100) {
        bool on = (elapsed < 50);
        if (on) _leds->previewSetPixel(4, ackCol, 100);
      } else {
        uint8_t beat = (elapsed - 100) / 200;
        uint16_t beatOffset = (elapsed - 100) % 200;
        if (beatOffset < _wk.tickFlashDurationMs && beat < _wk.playBeatCount) {
          static const uint8_t intensities[] = {50, 75, 100, 100};
          _leds->previewSetPixel(4, beatCol, intensities[beat < 4 ? beat : 3]);
        }
      }
      break;
    }
    case 14: {
      // Stop: fade on pulse background
      RGBW stopCol = resolveColorSlot(_cwk.slots[CSLOT_STOP]);
      RGBW arpBg = resolveColorSlot(_cwk.slots[CSLOT_ARPEG_BG]);
      if (elapsed >= _wk.stopFadeMs) { _prevState = PREV_IDLE; break; }
      uint8_t lutStep = _wk.pulsePeriodMs / 256;
      uint8_t sineIdx = (uint8_t)((now / (lutStep > 0 ? lutStep : 1)) % 256);
      uint8_t bgI = getRowIntensity(4) + (uint8_t)((uint16_t)SINE_LUT[sineIdx] *
                     (getRowIntensity(5) - getRowIntensity(4)) / 255);
      _leds->previewSetPixel(3, arpBg, bgI);
      uint8_t fadePct = (uint8_t)((uint32_t)(_wk.stopFadeMs - elapsed) * 100 / _wk.stopFadeMs);
      _leds->previewSetPixel(4, stopCol, fadePct);
      break;
    }
    case 15: {
      // Octave: both LEDs blink
      RGBW octCol = resolveColorSlot(_cwk.slots[CSLOT_OCTAVE]);
      uint16_t dur = _wk.octaveDurationMs;
      if (elapsed >= dur) { _prevState = PREV_IDLE; break; }
      uint16_t unitMs = dur / (_wk.octaveBlinks * 2);
      bool on = ((elapsed / (unitMs > 0 ? unitMs : 1)) % 2 == 0);
      if (on) {
        _leds->previewSetPixel(3, octCol, 100);
        _leds->previewSetPixel(4, octCol, 100);
      }
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

  // Continuous preview: only on page 0, rows 0-5
  if (_page == 0 && _cursor <= 5) {
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
  {
    Preferences prefs;
    if (prefs.begin(LED_SETTINGS_NVS_NAMESPACE, true)) {
      size_t len = prefs.getBytesLength(LED_SETTINGS_NVS_KEY);
      if (len == sizeof(LedSettingsStore)) {
        LedSettingsStore tmp;
        prefs.getBytes(LED_SETTINGS_NVS_KEY, &tmp, sizeof(LedSettingsStore));
        if (tmp.magic == EEPROM_MAGIC && tmp.version == LED_SETTINGS_VERSION) {
          _wk = tmp;
          if (_wk.fgArpStopMin > _wk.fgArpStopMax) _wk.fgArpStopMax = _wk.fgArpStopMin;
          if (_wk.fgArpPlayMin > _wk.fgArpPlayMax) _wk.fgArpPlayMax = _wk.fgArpPlayMin;
          if (_wk.bgArpStopMin > _wk.bgArpStopMax) _wk.bgArpStopMax = _wk.bgArpStopMin;
          if (_wk.bgArpPlayMin > _wk.bgArpPlayMax) _wk.bgArpPlayMax = _wk.bgArpPlayMin;
        }
      }
      len = prefs.getBytesLength(COLOR_SLOT_NVS_KEY);
      if (len == sizeof(ColorSlotStore)) {
        ColorSlotStore tmp;
        prefs.getBytes(COLOR_SLOT_NVS_KEY, &tmp, sizeof(ColorSlotStore));
        if (tmp.magic == COLOR_SLOT_MAGIC && tmp.version == 1) {
          _cwk = tmp;
        }
      }
      prefs.end();
    }
  }

  Serial.print(ITERM_RESIZE);

  LedSettingsStore originalWk = _wk;
  ColorSlotStore   originalCwk = _cwk;
  _nvsSaved = true;
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

    // Pot input: read ADCs, apply to params (~30ms)
    if ((now - lastPotMs) >= 30) {
      lastPotMs = now;
      if (_pots.update()) {
        if (applyPotValues()) {
          screenDirty = true;
        }
      }
    }

    NavEvent ev = input.update();

    // --- Defaults confirmation ---
    if (_confirmDefaults) {
      if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
        _wk = s_ledDefaults();
        _cwk = s_colorDefaults();
        saveLedSettings();
        saveColorSlots();
        originalWk = _wk;
        originalCwk = _cwk;
        _nvsSaved = true;
        _ui->flashSaved();
        _confirmDefaults = false;
        screenDirty = true;
      } else if (ev.type != NAV_NONE) {
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
        _colorField = 0;
        seedPotsForCursor();
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        if (_cursor < count - 1) _cursor++;
        else _cursor = 0;
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
      if (_page == 0 && _cursor < 16) {
        // COLOR row editing: TAB cycles fields, ◄► adjusts
        if (ev.type == NAV_CHAR && ev.ch == '\t') {
          if (rowHasEditableIntensity(_cursor))
            _colorField = (_colorField + 1) % 3;
          else
            _colorField = (_colorField + 1) % 2;
          screenDirty = true;
        }
        if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
          adjustColorField(ev.type == NAV_RIGHT ? +1 : -1, ev.accelerated);
          screenDirty = true;
        }
        if (ev.type == NAV_ENTER) {
          saveLedSettings();
          saveColorSlots();
          _editing = false;
          _nvsSaved = true;
          _ui->flashSaved();
          originalWk = _wk;
          originalCwk = _cwk;
          screenDirty = true;
        }
      } else if (_page == 0 && _cursor >= 16) {
        // Timing rows: standard ◄► adjust
        if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
          adjustTimingParam(ev.type == NAV_RIGHT ? +1 : -1, ev.accelerated);
          screenDirty = true;
        }
        if (ev.type == NAV_ENTER) {
          saveLedSettings();
          _editing = false;
          _nvsSaved = true;
          _ui->flashSaved();
          originalWk = _wk;
          screenDirty = true;
        }
      } else {
        // CONFIRM page: standard ◄► adjust
        if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
          adjustConfirmParam(ev.type == NAV_RIGHT ? +1 : -1, ev.accelerated);
          screenDirty = true;
        }
        if (ev.type == NAV_ENTER) {
          saveLedSettings();
          _editing = false;
          _nvsSaved = true;
          _ui->flashSaved();
          originalWk = _wk;
          screenDirty = true;
        }
      }
    }

    // --- Render (throttled + backpressure: skip if serial TX buffer is too full,
    //     prevents watchdog timeout from blocking Serial.print calls) ---
    if (screenDirty && (now - lastRenderMs) >= 80
        && Serial.availableForWrite() > 256) {
      screenDirty = false;
      lastRenderMs = now;

      _ui->vtFrameStart();

      char headerBuf[64];
      snprintf(headerBuf, sizeof(headerBuf), "TOOL 7: LED SETTINGS  [%s]", s_pageNames[_page]);
      _ui->drawConsoleHeader(headerBuf, _nvsSaved);
      _ui->drawFrameEmpty();

      if (_page == 0) {
        // ============ PAGE 0: COLOR + TIMING ============

        _ui->drawSection("DISPLAY");
        for (uint8_t r = 0; r < 16; r++) {
          bool sel = (_cursor == r);
          bool edt = sel && _editing;
          drawColorRow(r, sel, edt);
        }
        _ui->drawFrameEmpty();

        _ui->drawSection("TIMING");
        char buf[32];

        // Pulse period
        {
          bool sel = (_cursor == 16);
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
          bool sel = (_cursor == 17);
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
        snprintf(buf, sizeof(buf), "%d ms", _wk.holdOnFlashMs);
        drawParam(8, "ON flash:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.holdFadeMs);
        drawParam(9, "OFF fade:", buf);
        _ui->drawFrameEmpty();

        _ui->drawSection("PLAY / STOP");
        snprintf(buf, sizeof(buf), "%d", _wk.playBeatCount);
        drawParam(10, "Beat count:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.stopFadeMs);
        drawParam(11, "Stop fade:", buf);
        _ui->drawFrameEmpty();

        _ui->drawSection("OCTAVE");
        snprintf(buf, sizeof(buf), "%d", _wk.octaveBlinks);
        drawParam(12, "Blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.octaveDurationMs);
        drawParam(13, "Duration:", buf);
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
        _ui->drawControlBar(VT_DIM "[y] confirm  [any] cancel" VT_RESET);
      } else if (_editing) {
        if (_page == 0 && _cursor < 16) {
          _ui->drawControlBar(VT_DIM "[</>] ADJUST  [TAB] FIELD  [RET] SAVE  [q] CANCEL" VT_RESET);
        } else {
          _ui->drawControlBar(VT_DIM "[</>] ADJUST  [RET] SAVE  [q] CANCEL" VT_RESET);
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
