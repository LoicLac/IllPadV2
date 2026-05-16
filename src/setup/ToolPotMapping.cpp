#include "ToolPotMapping.h"
#include "SetupUI.h"
#include "InputParser.h"
#include "../core/LedController.h"
#include "../core/PotFilter.h"
#include "../managers/PotRouter.h"
#include "../managers/NvsManager.h"
#include <Preferences.h>
#include <Arduino.h>
#include <string.h>

// =================================================================
// Target display names
// =================================================================
const char* ToolPotMapping::targetName(PotTarget t) {
  switch (t) {
    case TARGET_TEMPO_BPM:          return "Tempo";
    case TARGET_RESPONSE_SHAPE:     return "Shape";
    case TARGET_SLEW_RATE:          return "Slew";
    case TARGET_AT_DEADZONE:        return "Deadzone";
    case TARGET_PITCH_BEND:         return "PitchBnd";
    case TARGET_GATE_LENGTH:        return "Gate";
    case TARGET_SHUFFLE_DEPTH:      return "ShufDpth";
    case TARGET_SHUFFLE_TEMPLATE:   return "ShufTmpl";
    case TARGET_DIVISION:           return "Division";
    case TARGET_PATTERN:            return "Pattern";
    case TARGET_BASE_VELOCITY:      return "BaseVel";
    case TARGET_VELOCITY_VARIATION: return "VelVar";
    case TARGET_MIDI_CC:            return "CC";
    case TARGET_MIDI_PITCHBEND:     return "PB";
    case TARGET_EMPTY:              return "(empty)";
    default:                        return "???";
  }
}

const char* ToolPotMapping::slotName(uint8_t slot) {
  static const char* names[] = {
    "R1 alone",  "R1 + hold",
    "R2 alone",  "R2 + hold",
    "R3 alone",  "R3 + hold",
    "R4 alone",  "R4 + hold"
  };
  if (slot < 8) return names[slot];
  return "???";
}

// =================================================================
// Pools
// =================================================================

// TARGET_TEMPO_BPM exclu des pools : binding fixe sur LEFT + rear pot
// (cf. PotRouter::rebuildBindings, doc pot-reference.md §6-7).
static const PotTarget NORMAL_PARAMS[] = {
  TARGET_RESPONSE_SHAPE, TARGET_SLEW_RATE,
  TARGET_AT_DEADZONE, TARGET_PITCH_BEND,
  TARGET_BASE_VELOCITY, TARGET_VELOCITY_VARIATION
};
static const uint8_t NORMAL_PARAM_COUNT = sizeof(NORMAL_PARAMS) / sizeof(NORMAL_PARAMS[0]);

static const PotTarget ARPEG_PARAMS[] = {
  TARGET_GATE_LENGTH, TARGET_SHUFFLE_DEPTH,
  TARGET_SHUFFLE_TEMPLATE, TARGET_DIVISION, TARGET_PATTERN,
  TARGET_BASE_VELOCITY, TARGET_VELOCITY_VARIATION
};
static const uint8_t ARPEG_PARAM_COUNT = sizeof(ARPEG_PARAMS) / sizeof(ARPEG_PARAMS[0]);

// =================================================================
// Constructor
// =================================================================
ToolPotMapping::ToolPotMapping()
  : _leds(nullptr), _ui(nullptr), _potRouter(nullptr)
  , _contextNormal(true), _cursorRow(0), _cursorCol(0)
  , _editing(false), _poolIdx(0), _ccEditing(false), _ccNumber(0)
  , _confirmSteal(false), _stealSourceSlot(-1), _stealTarget(TARGET_EMPTY)
  , _nvsSaved(false), _poolCount(0)
{
  memset(&_wk, 0, sizeof(_wk));
  memset(_potBaseline, 0, sizeof(_potBaseline));
}

void ToolPotMapping::begin(LedController* leds, SetupUI* ui, PotRouter* potRouter) {
  _leds = leds;
  _ui = ui;
  _potRouter = potRouter;
}

// =================================================================
// Helpers
// =================================================================

uint8_t ToolPotMapping::cursorToSlot() const {
  return _cursorRow * 2 + _cursorCol;
}

void ToolPotMapping::buildPool() {
  _poolCount = 0;
  const PotTarget* params = _contextNormal ? NORMAL_PARAMS : ARPEG_PARAMS;
  uint8_t paramCount = _contextNormal ? NORMAL_PARAM_COUNT : ARPEG_PARAM_COUNT;
  for (uint8_t i = 0; i < paramCount && _poolCount < MAX_POOL; i++) {
    _pool[_poolCount++] = params[i];
  }
  if (_poolCount < MAX_POOL) _pool[_poolCount++] = TARGET_MIDI_CC;
  if (_poolCount < MAX_POOL) _pool[_poolCount++] = TARGET_MIDI_PITCHBEND;
  if (_poolCount < MAX_POOL) _pool[_poolCount++] = TARGET_EMPTY;
}

int8_t ToolPotMapping::findSlotWithTarget(PotTarget t, uint8_t ccNum) const {
  const PotMapping* map = currentMapConst();
  for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
    if (map[i].target == t) {
      if (t == TARGET_MIDI_CC) {
        if (map[i].ccNumber == ccNum) return (int8_t)i;
      } else {
        return (int8_t)i;
      }
    }
  }
  return -1;
}

PotMapping* ToolPotMapping::currentMap() {
  return _contextNormal ? _wk.normalMap : _wk.arpegMap;
}

const PotMapping* ToolPotMapping::currentMapConst() const {
  return _contextNormal ? _wk.normalMap : _wk.arpegMap;
}

// =================================================================
// Pot detection
// =================================================================
static const uint16_t POT_DETECT_THRESHOLD = 60;  // Filtered signal, ~3x deadband

void ToolPotMapping::samplePotBaselines() {
  for (uint8_t i = 0; i < 4; i++) {
    _potBaseline[i] = PotFilter::getStable(i);
  }
}

int8_t ToolPotMapping::detectMovedPot(bool btnLeftHeld) {
  for (uint8_t i = 0; i < 4; i++) {
    uint16_t val = PotFilter::getStable(i);
    int16_t delta = (int16_t)val - (int16_t)_potBaseline[i];
    if (delta < 0) delta = -delta;
    if ((uint16_t)delta > POT_DETECT_THRESHOLD) {
      _potBaseline[i] = val;
      return (int8_t)(i * 2 + (btnLeftHeld ? 1 : 0));
    }
  }
  return -1;
}

// =================================================================
// saveMapping
// =================================================================
bool ToolPotMapping::saveMapping() {
  _wk.magic = EEPROM_MAGIC;
  _wk.version = POTMAP_VERSION;
  _wk.reserved = 0;  // FIX: was uninitialized
  if (!NvsManager::saveBlob(POTMAP_NVS_NAMESPACE, POTMAP_NVS_KEY, &_wk, sizeof(_wk)))
    return false;
  if (_potRouter) _potRouter->applyMapping(_wk);
  _nvsSaved = true;
  return true;
}

// =================================================================
// assignCurrentTarget
// =================================================================
void ToolPotMapping::assignCurrentTarget() {
  PotMapping* map = currentMap();
  uint8_t slot = cursorToSlot();
  PotTarget newTarget = _pool[_poolIdx];

  if (newTarget == TARGET_EMPTY) {
    PotMapping backup = map[slot];
    map[slot].target = TARGET_EMPTY;
    map[slot].ccNumber = 0;
    if (saveMapping()) {
      _ui->flashSaved();
      _editing = false;
    } else {
      map[slot] = backup;
    }
    return;
  }

  if (newTarget == TARGET_MIDI_CC) {
    map[slot].target = TARGET_MIDI_CC;
    _ccNumber = map[slot].ccNumber;
    if (_ccNumber == 0) _ccNumber = 1;
    _ccEditing = true;
    // Seed pot for CC# sweep (ABSOLUTE 0-127)
    _potCcNum = _ccNumber;
    _pots.seed(0, &_potCcNum, 0, 127, POT_ABSOLUTE);
    return;
  }

  if (newTarget == TARGET_MIDI_PITCHBEND) {
    int8_t existing = findSlotWithTarget(TARGET_MIDI_PITCHBEND);
    PotMapping savedSlot = map[slot];
    PotMapping savedExisting = {TARGET_EMPTY, 0};
    if (existing >= 0 && existing != (int8_t)slot) {
      savedExisting = map[existing];
      map[existing].target = TARGET_EMPTY;
      map[existing].ccNumber = 0;
    }
    map[slot].target = TARGET_MIDI_PITCHBEND;
    map[slot].ccNumber = 0;
    if (saveMapping()) {
      _ui->flashSaved();
      _editing = false;
    } else {
      // Restore working copy on NVS failure
      map[slot] = savedSlot;
      if (existing >= 0 && existing != (int8_t)slot)
        map[existing] = savedExisting;
    }
    return;
  }

  int8_t existing = findSlotWithTarget(newTarget);
  if (existing >= 0 && existing != (int8_t)slot) {
    _confirmSteal = true;
    _stealSourceSlot = existing;
    _stealTarget = newTarget;
    return;
  }

  PotMapping backup = map[slot];
  map[slot].target = newTarget;
  map[slot].ccNumber = 0;
  if (saveMapping()) {
    _ui->flashSaved();
    _editing = false;
  } else {
    map[slot] = backup;
  }
}

// =================================================================
// printTargetDescription — nerdy expanded descriptions
// =================================================================
void ToolPotMapping::printTargetDescription(PotTarget t) {
  switch (t) {
    case TARGET_TEMPO_BPM:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Tempo" VT_RESET VT_DIM "  --  Internal clock tempo in BPM" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Range: 10-260 BPM. Only active in Master clock mode." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Controls arp tick rate and outgoing MIDI clock (0xF8). Linear pot mapping." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Global parameter (shared across all banks)." VT_RESET);
      break;
    case TARGET_RESPONSE_SHAPE:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Shape" VT_RESET VT_DIM "  --  Pressure response curve" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Controls how raw capacitive delta maps to MIDI velocity/aftertouch." VT_RESET);
      _ui->drawFrameLine(VT_DIM "0.0 = linear. Higher = more exponential (more range at light touch)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Global -- affects all banks. Sent to Core 0 via atomic." VT_RESET);
      break;
    case TARGET_SLEW_RATE:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Slew" VT_RESET VT_DIM "  --  Pressure smoothing rate" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Higher = smoother aftertouch but slower response." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Lower = jittery but instant. Affects EMA filter in CapacitiveKeyboard." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Global -- sent to Core 0 via atomic<uint16_t>." VT_RESET);
      break;
    case TARGET_AT_DEADZONE:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Deadzone" VT_RESET VT_DIM "  --  Aftertouch ignore zone" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Pressure below this threshold produces no aftertouch messages." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Prevents ghost AT from ambient vibration or very light contact." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Range: 0-127 MIDI value. Default: 10. Higher = less sensitive AT." VT_RESET);
      break;
    case TARGET_PITCH_BEND:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "PitchBend" VT_RESET VT_DIM "  --  Per-bank pitch bend offset" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Applied when bank becomes foreground. Range: -8192 to +8191 (14-bit)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Useful for detuning layers. NORMAL banks only." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Per-bank parameter, stored in NVS." VT_RESET);
      break;
    case TARGET_GATE_LENGTH:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Gate" VT_RESET VT_DIM "  --  Arp note duration vs step length" VT_RESET);
      _ui->drawFrameLine(VT_DIM "0.0 = staccatissimo (off immediately). 1.0 = legato (fills step)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Default: 0.5. At high shuffle depth + long gate, notes overlap." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Per-bank ARPEG parameter." VT_RESET);
      break;
    case TARGET_SHUFFLE_DEPTH:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Shuffle Depth" VT_RESET VT_DIM "  --  Groove intensity" VT_RESET);
      _ui->drawFrameLine(VT_DIM "0.0 = straight timing. 1.0 = maximum groove." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Multiplies shuffle template values. Extreme = notes cross step boundaries." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Per-bank ARPEG parameter." VT_RESET);
      break;
    case TARGET_SHUFFLE_TEMPLATE:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Shuffle Template" VT_RESET VT_DIM "  --  Groove shape (5 templates)" VT_RESET);
      _ui->drawFrameLine(VT_DIM "16-step timing offset pattern. Template x Depth = actual swing." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Each template has a distinct feel. Cycle with pot." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Per-bank ARPEG parameter." VT_RESET);
      break;
    case TARGET_DIVISION:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Division" VT_RESET VT_DIM "  --  Arp clock division" VT_RESET);
      _ui->drawFrameLine(VT_DIM "9 values: 4/1, 2/1, 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Lower fraction = faster arp. Binary values mapped to pot range." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Per-bank ARPEG parameter." VT_RESET);
      break;
    case TARGET_PATTERN:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Pattern" VT_RESET VT_DIM "  --  Arp playback shape (15 patterns)" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Up Down UpDown Random Order | Cascade Converge Diverge PedalUp" VT_RESET);
      _ui->drawFrameLine(VT_DIM "UpOct DownOct Chord OctWave OctBounce Probability" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Per-bank ARPEG parameter." VT_RESET);
      break;
    case TARGET_BASE_VELOCITY:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Base Velocity" VT_RESET VT_DIM "  --  noteOn velocity" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Range: 1-127. Combined with VelVar for humanization." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Each noteOn: velocity = BaseVel +/- random(VelVar)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Per-bank (NORMAL + ARPEG). Stored in NVS." VT_RESET);
      break;
    case TARGET_VELOCITY_VARIATION:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Velocity Variation" VT_RESET VT_DIM "  --  random velocity range" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Range: 0-63. Higher = more human feel. 0 = robotic fixed velocity." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Each noteOn: velocity = BaseVel +/- random(VelVar)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Per-bank (NORMAL + ARPEG). Stored in NVS." VT_RESET);
      break;
    case TARGET_MIDI_CC:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "MIDI CC" VT_RESET VT_DIM "  --  Control Change output" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Send CC on foreground bank's channel. Multiple CCs allowed (diff CC#)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Only sends on value change (dirty flag -- no MIDI flood)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Enter CC# (0-127) after selection." VT_RESET);
      break;
    case TARGET_MIDI_PITCHBEND:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "MIDI Pitchbend" VT_RESET VT_DIM "  --  PB output" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Send PB on foreground bank's channel. Max one per context." VT_RESET);
      _ui->drawFrameLine(VT_DIM "14-bit resolution. Auto-steals if second assigned." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Only sends on value change." VT_RESET);
      break;
    case TARGET_EMPTY:
      _ui->drawFrameLine(VT_DIM "No parameter assigned to this slot." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Pot movement on this slot is ignored." VT_RESET);
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

// =================================================================
// drawTwoColumnLayout — side-by-side Alone | +Hold
// =================================================================
void ToolPotMapping::drawTwoColumnLayout() {
  const PotMapping* map = currentMapConst();
  uint8_t selectedSlot = cursorToSlot();

  // Column headers inside frame
  _ui->drawFrameLine(VT_DIM "          " VT_RESET
                     VT_REVERSE " Alone " VT_RESET
                     VT_DIM "                                     " VT_RESET
                     VT_REVERSE " + Hold Left " VT_RESET);

  for (uint8_t row = 0; row < 4; row++) {
    uint8_t slotAlone = row * 2;
    uint8_t slotHold  = row * 2 + 1;
    bool selAlone = (selectedSlot == slotAlone);
    bool selHold  = (selectedSlot == slotHold);

    // Build value strings
    char valAlone[20], valHold[20];
    if (map[slotAlone].target == TARGET_MIDI_CC) {
      snprintf(valAlone, sizeof(valAlone), "CC %d", map[slotAlone].ccNumber);
    } else {
      snprintf(valAlone, sizeof(valAlone), "%s", targetName(map[slotAlone].target));
    }
    if (map[slotHold].target == TARGET_MIDI_CC) {
      snprintf(valHold, sizeof(valHold), "CC %d", map[slotHold].ccNumber);
    } else {
      snprintf(valHold, sizeof(valHold), "%s", targetName(map[slotHold].target));
    }

    // Format line: "  R1:  [Value]        |  R1:  Value"
    char line[200];
    int pos = 0;

    // Left column (alone)
    if (selAlone && _editing) {
      pos += snprintf(line + pos, sizeof(line) - pos,
                      VT_CYAN VT_BOLD "> " VT_RESET "R%d:  " VT_CYAN "[%-10s]" VT_RESET, row + 1, valAlone);
    } else if (selAlone) {
      pos += snprintf(line + pos, sizeof(line) - pos,
                      VT_CYAN VT_BOLD "> " VT_RESET "R%d:  " VT_BRIGHT_WHITE "%-12s" VT_RESET, row + 1, valAlone);
    } else {
      bool empty = (map[slotAlone].target == TARGET_EMPTY);
      pos += snprintf(line + pos, sizeof(line) - pos,
                      "  R%d:  %s%-12s" VT_RESET, row + 1, empty ? VT_DIM : "", valAlone);
    }

    // Separator
    pos += snprintf(line + pos, sizeof(line) - pos, VT_DIM "     |     " VT_RESET);

    // Right column (hold)
    if (selHold && _editing) {
      pos += snprintf(line + pos, sizeof(line) - pos,
                      VT_CYAN VT_BOLD "> " VT_RESET "R%d:  " VT_CYAN "[%-10s]" VT_RESET, row + 1, valHold);
    } else if (selHold) {
      pos += snprintf(line + pos, sizeof(line) - pos,
                      VT_CYAN VT_BOLD "> " VT_RESET "R%d:  " VT_BRIGHT_WHITE "%-12s" VT_RESET, row + 1, valHold);
    } else {
      bool empty = (map[slotHold].target == TARGET_EMPTY);
      pos += snprintf(line + pos, sizeof(line) - pos,
                      "  R%d:  %s%-12s" VT_RESET, row + 1, empty ? VT_DIM : "", valHold);
    }

    _ui->drawFrameLine("%s", line);
  }
}

// =================================================================
// drawPoolLine
// =================================================================
void ToolPotMapping::drawPoolLine() {
  bool inEdit = _editing || _ccEditing;
  const PotMapping* map = currentMapConst();
  uint8_t slot = cursorToSlot();

  char buf[256];
  int pos = 0;
  pos += snprintf(buf + pos, sizeof(buf) - pos, VT_DIM "Pool:" VT_RESET " ");

  for (uint8_t i = 0; i < _poolCount; i++) {
    PotTarget t = _pool[i];
    bool isAssigned = false;
    if (t != TARGET_EMPTY && t != TARGET_MIDI_CC) {
      int8_t owner = findSlotWithTarget(t);
      isAssigned = (owner >= 0 && owner != (int8_t)slot);
    }

    bool isCurrentTarget = (map[slot].target == t);
    if (t == TARGET_MIDI_CC) isCurrentTarget = false;

    bool isCursor = inEdit && (_poolIdx == i);

    if (isCursor) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, VT_REVERSE VT_BOLD " %s " VT_RESET " ", targetName(t));
    } else if (isCurrentTarget && !inEdit) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, VT_CYAN "%s" VT_RESET " ", targetName(t));
    } else if (isAssigned) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, VT_DIM "%s" VT_RESET " ", targetName(t));
    } else if (inEdit) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, VT_BRIGHT_GREEN "%s" VT_RESET " ", targetName(t));
    } else {
      pos += snprintf(buf + pos, sizeof(buf) - pos, "%s ", targetName(t));
    }
  }

  _ui->drawFrameLine("%s", buf);
}

// =================================================================
// drawInfoPanel
// =================================================================
void ToolPotMapping::drawInfoPanel() {
  if (_confirmDefaults) {
    _ui->drawFrameLine(VT_YELLOW "Reset %s to defaults? (y/n)" VT_RESET,
                       _contextNormal ? "NORMAL" : "ARPEG");
    _ui->drawFrameEmpty();
    _ui->drawFrameEmpty();
    _ui->drawFrameEmpty();
    return;
  }

  if (_confirmSteal) {
    _ui->drawFrameLine(VT_YELLOW "Already assigned to %s. Replace? (y/n)" VT_RESET,
                       slotName((uint8_t)_stealSourceSlot));
    _ui->drawFrameEmpty();
    _ui->drawFrameEmpty();
    _ui->drawFrameEmpty();
    return;
  }

  if (_ccEditing) {
    _ui->drawFrameLine(VT_CYAN "CC#: [%d]" VT_RESET VT_DIM "  --  Use [</>] to adjust, [RET] confirm, [q] cancel" VT_RESET, _ccNumber);
    _ui->drawFrameEmpty();
    _ui->drawFrameEmpty();
    _ui->drawFrameEmpty();
    return;
  }

  // Show description of either the pool cursor (editing) or current slot's target (nav)
  if (_editing) {
    printTargetDescription(_pool[_poolIdx]);
  } else {
    uint8_t slot = cursorToSlot();
    printTargetDescription(currentMapConst()[slot].target);
  }
}

// =================================================================
// drawScreen
// =================================================================
void ToolPotMapping::drawScreen() {
  const char* ctxLabel = _contextNormal ? "NORMAL" : "ARPEG";
  char headerText[48];
  snprintf(headerText, sizeof(headerText), "TOOL 6: POT MAPPING  [%s]", ctxLabel);

  _ui->vtFrameStart();
  _ui->drawConsoleHeader(headerText, _nvsSaved);
  _ui->drawFrameEmpty();

  // Two-column layout
  char sectionLabel[40];
  snprintf(sectionLabel, sizeof(sectionLabel), "%s CONTEXT", ctxLabel);
  _ui->drawSection(sectionLabel);
  _ui->drawFrameEmpty();
  drawTwoColumnLayout();
  _ui->drawFrameEmpty();

  // Pool
  _ui->drawSection("POOL");
  drawPoolLine();
  _ui->drawFrameEmpty();

  // Info
  _ui->drawSection("INFO");
  drawInfoPanel();
  _ui->drawFrameEmpty();

  // Control bar
  if (_confirmDefaults) {
    _ui->drawControlBar(CBAR_CONFIRM_ANY);
  } else if (_confirmSteal) {
    _ui->drawControlBar(CBAR_CONFIRM_ANY);
  } else if (_ccEditing) {
    _ui->drawControlBar(VT_DIM "[</>] CC#  [P1] sweep" CBAR_SEP "[RET] CONFIRM" CBAR_SEP "[q] CANCEL" VT_RESET);
  } else if (_editing) {
    _ui->drawControlBar(VT_DIM "[</>] CYCLE POOL" CBAR_SEP "[RET] ASSIGN" CBAR_SEP "[q] CANCEL" VT_RESET);
  } else {
    {
      char ctrlBuf[128];
      snprintf(ctrlBuf, sizeof(ctrlBuf),
               VT_DIM "[^v] R1-R4  [<>] ALONE/HOLD  [RET] EDIT  [t] %s  [d] DFLT  [q] EXIT" VT_RESET,
               _contextNormal ? "ARPEG" : "NORMAL");
      _ui->drawControlBar(ctrlBuf);
    }
  }

  _ui->vtFrameEnd();
}

// =================================================================
// run — main loop
// =================================================================
void ToolPotMapping::run() {
  if (!_ui || !_leds) return;

  if (_potRouter) {
    memcpy(&_wk, &_potRouter->getMapping(), sizeof(PotMappingStore));
  } else {
    memcpy(&_wk, &PotRouter::DEFAULT_MAPPING, sizeof(PotMappingStore));
  }

  _contextNormal = true;
  _cursorRow = 0;
  _cursorCol = 0;
  _editing = false;
  _poolIdx = 0;
  _ccEditing = false;
  _ccNumber = 0;
  _confirmSteal = false;
  _stealSourceSlot = -1;
  _stealTarget = TARGET_EMPTY;
  _confirmDefaults = false;
  // Check if NVS actually has saved data (not just defaults)
  _nvsSaved = NvsManager::checkBlob(POTMAP_NVS_NAMESPACE, POTMAP_NVS_KEY,
                                     EEPROM_MAGIC, POTMAP_VERSION, sizeof(PotMappingStore));

  Serial.print(ITERM_RESIZE);

  buildPool();
  samplePotBaselines();

  InputParser input;
  bool screenDirty = true;
  // _confirmDefaults initialized in run() preamble above

  _ui->vtClear();

  _pots.disable(0);  // Pot disabled in nav mode

  while (true) {
    PotFilter::updateAll();
    _pots.update();
    _leds->update();

    // --- Pot edit handling (CC# sub-mode only — pool cycling is keyboard-only) ---
    if (_ccEditing && _pots.getMove(0)) {
      _ccNumber = (uint8_t)_potCcNum;
      screenDirty = true;
    }

    // Physical pot detection (when not editing)
    if (!_editing && !_ccEditing && !_confirmSteal && !_confirmDefaults) {
      bool btnLeftHeld = (digitalRead(BTN_LEFT_PIN) == LOW);
      int8_t movedSlot = detectMovedPot(btnLeftHeld);
      if (movedSlot >= 0) {
        _cursorRow = movedSlot / 2;
        _cursorCol = movedSlot % 2;
        screenDirty = true;
      }
    }

    NavEvent ev = input.update();

    // --- Defaults confirmation ---
    if (_confirmDefaults) {
      ConfirmResult r = SetupUI::parseConfirm(ev);
      if (r == CONFIRM_YES) {
        if (_contextNormal) {
          memcpy(_wk.normalMap, PotRouter::DEFAULT_MAPPING.normalMap,
                 sizeof(PotMapping) * POT_MAPPING_SLOTS);
        } else {
          memcpy(_wk.arpegMap, PotRouter::DEFAULT_MAPPING.arpegMap,
                 sizeof(PotMapping) * POT_MAPPING_SLOTS);
        }
        if (saveMapping()) {
          _ui->flashSaved();
        }
        _confirmDefaults = false;
        buildPool();
        screenDirty = true;
      } else if (r == CONFIRM_NO) {
        _confirmDefaults = false;
        screenDirty = true;
      }
      delay(5);
      continue;
    }

    // --- Steal confirmation ---
    if (_confirmSteal) {
      ConfirmResult r = SetupUI::parseConfirm(ev);
      if (r == CONFIRM_YES) {
        PotMapping* map = currentMap();
        uint8_t slot = cursorToSlot();
        PotMapping backupSource = map[_stealSourceSlot];
        PotMapping backupSlot = map[slot];
        map[_stealSourceSlot].target = TARGET_EMPTY;
        map[_stealSourceSlot].ccNumber = 0;
        map[slot].target = _stealTarget;
        map[slot].ccNumber = 0;
        if (saveMapping()) {
          _ui->flashSaved();
          _confirmSteal = false;
          _editing = false;
          _pots.disable(0);
          samplePotBaselines();  // Refresh baselines for NAV detect
        } else {
          map[_stealSourceSlot] = backupSource;
          map[slot] = backupSlot;
          _confirmSteal = false;
        }
        screenDirty = true;
      } else if (r == CONFIRM_NO) {
        _confirmSteal = false;
        screenDirty = true;
      }
      delay(5);
      continue;
    }

    // --- CC# sub-editor ---
    if (_ccEditing) {
      if (ev.type == NAV_LEFT) {
        int step = ev.accelerated ? 10 : 1;
        int val = (int)_ccNumber - step;
        if (val < 0) val = 0;
        _ccNumber = (uint8_t)val;
        _potCcNum = _ccNumber;  // Sync pot target
        screenDirty = true;
      } else if (ev.type == NAV_RIGHT) {
        int step = ev.accelerated ? 10 : 1;
        int val = (int)_ccNumber + step;
        if (val > 127) val = 127;
        _ccNumber = (uint8_t)val;
        _potCcNum = _ccNumber;  // Sync pot target
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        PotMapping* map = currentMap();
        uint8_t slot = cursorToSlot();
        int8_t dup = findSlotWithTarget(TARGET_MIDI_CC, _ccNumber);
        PotMapping savedSlot = map[slot];
        PotMapping savedDup = {TARGET_EMPTY, 0};
        if (dup >= 0 && dup != (int8_t)slot) {
          savedDup = map[dup];
          map[dup].target = TARGET_EMPTY;
          map[dup].ccNumber = 0;
        }
        map[slot].target = TARGET_MIDI_CC;
        map[slot].ccNumber = _ccNumber;
        if (saveMapping()) {
          _ui->flashSaved();
          _ccEditing = false;
          _editing = false;
          _pots.disable(0);
          samplePotBaselines();  // Refresh baselines for NAV detect
        } else {
          // Restore working copy on NVS failure
          map[slot] = savedSlot;
          if (dup >= 0 && dup != (int8_t)slot)
            map[dup] = savedDup;
        }
        screenDirty = true;
      } else if (ev.type == NAV_QUIT) {
        _ccEditing = false;
        uint8_t slot = cursorToSlot();
        if (_potRouter) {
          const PotMappingStore& live = _potRouter->getMapping();
          const PotMapping* liveMap = _contextNormal ? live.normalMap : live.arpegMap;
          currentMap()[slot] = liveMap[slot];
        } else {
          currentMap()[slot] = {TARGET_EMPTY, 0};
        }
        // Pot disabled — pool cycling is keyboard-only
        _pots.disable(0);
        screenDirty = true;
      }
      delay(5);
      continue;
    }

    // --- Main navigation ---
    if (ev.type == NAV_QUIT && !_editing) {
      _ui->vtClear();
      return;
    }

    if (ev.type == NAV_TOGGLE && !_editing) {
      _contextNormal = !_contextNormal;
      _cursorRow = 0;
      _cursorCol = 0;
      _editing = false;
      _poolIdx = 0;
      buildPool();
      samplePotBaselines();
      _ui->vtClear();
      screenDirty = true;
    }

    if (ev.type == NAV_DEFAULTS && !_editing) {
      _confirmDefaults = true;
      screenDirty = true;
    }

    if (!_editing) {
      // Navigation: Up/Down = R1-R4, Left/Right = Alone/Hold column
      if (ev.type == NAV_UP) {
        if (_cursorRow > 0) _cursorRow--;
        else _cursorRow = 3;
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        if (_cursorRow < 3) _cursorRow++;
        else _cursorRow = 0;
        screenDirty = true;
      } else if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
        _cursorCol = 1 - _cursorCol;  // Toggle 0/1
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        _editing = true;
        PotTarget current = currentMapConst()[cursorToSlot()].target;
        _poolIdx = 0;
        for (uint8_t i = 0; i < _poolCount; i++) {
          if (_pool[i] == current) {
            _poolIdx = i;
            break;
          }
        }
        _pots.disable(0);  // Pool cycling is keyboard-only
        screenDirty = true;
      }
    } else {
      // Editing: Left/Right = cycle pool (keyboard only)
      if (ev.type == NAV_LEFT) {
        if (_poolCount > 0) {
          if (_poolIdx == 0) _poolIdx = _poolCount - 1;
          else _poolIdx--;
        }
        screenDirty = true;
      } else if (ev.type == NAV_RIGHT) {
        if (_poolCount > 0) {
          _poolIdx++;
          if (_poolIdx >= _poolCount) _poolIdx = 0;
        }
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        assignCurrentTarget();
        // assignCurrentTarget may enter CC# sub-editor (seeds pot ABSOLUTE)
        // Only disable pot if we actually left edit mode
        if (!_ccEditing) {
          _pots.disable(0);
          samplePotBaselines();  // Refresh baselines for NAV detect
        }
        screenDirty = true;
      } else if (ev.type == NAV_QUIT) {
        if (_potRouter) {
          const PotMappingStore& live = _potRouter->getMapping();
          const PotMapping* liveMap = _contextNormal ? live.normalMap : live.arpegMap;
          uint8_t slot = cursorToSlot();
          currentMap()[slot] = liveMap[slot];
        }
        _editing = false;
        _pots.disable(0);
        samplePotBaselines();  // Refresh baselines for NAV detect
        screenDirty = true;
      }
    }

    // --- Render ---
    if (screenDirty) {
      screenDirty = false;
      drawScreen();
    }

    delay(5);
  }
}
