#include "ToolPotMapping.h"
#include "SetupUI.h"
#include "InputParser.h"
#include "../core/LedController.h"
#include "../managers/PotRouter.h"
#include <Preferences.h>
#include <Arduino.h>
#include <string.h>

// =================================================================
// Target display names (short, fits in VT100 columns)
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

// =================================================================
// Slot display names
// =================================================================
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
// Pools: which targets are assignable in each context
// =================================================================

// NORMAL context: these params + empty + CC + PB
static const PotTarget NORMAL_PARAMS[] = {
  TARGET_TEMPO_BPM, TARGET_RESPONSE_SHAPE, TARGET_SLEW_RATE,
  TARGET_AT_DEADZONE, TARGET_PITCH_BEND,
  TARGET_BASE_VELOCITY, TARGET_VELOCITY_VARIATION
};
static const uint8_t NORMAL_PARAM_COUNT = sizeof(NORMAL_PARAMS) / sizeof(NORMAL_PARAMS[0]);

// ARPEG context: these params + empty + CC + PB
static const PotTarget ARPEG_PARAMS[] = {
  TARGET_TEMPO_BPM, TARGET_GATE_LENGTH, TARGET_SHUFFLE_DEPTH,
  TARGET_SHUFFLE_TEMPLATE, TARGET_DIVISION, TARGET_PATTERN,
  TARGET_BASE_VELOCITY, TARGET_VELOCITY_VARIATION
};
static const uint8_t ARPEG_PARAM_COUNT = sizeof(ARPEG_PARAMS) / sizeof(ARPEG_PARAMS[0]);

// =================================================================
// Constructor
// =================================================================
ToolPotMapping::ToolPotMapping()
  : _leds(nullptr), _ui(nullptr), _potRouter(nullptr)
  , _contextNormal(true), _cursor(0), _editing(false)
  , _poolIdx(0), _ccEditing(false), _ccNumber(0)
  , _confirmSteal(false), _stealSourceSlot(-1), _stealTarget(TARGET_EMPTY)
  , _poolCount(0)
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
// buildPool — fill _pool[] for current context
// =================================================================
void ToolPotMapping::buildPool() {
  _poolCount = 0;

  const PotTarget* params;
  uint8_t paramCount;
  if (_contextNormal) {
    params = NORMAL_PARAMS;
    paramCount = NORMAL_PARAM_COUNT;
  } else {
    params = ARPEG_PARAMS;
    paramCount = ARPEG_PARAM_COUNT;
  }

  // Internal params first
  for (uint8_t i = 0; i < paramCount && _poolCount < MAX_POOL; i++) {
    _pool[_poolCount++] = params[i];
  }
  // Then: CC, PB, empty
  if (_poolCount < MAX_POOL) _pool[_poolCount++] = TARGET_MIDI_CC;
  if (_poolCount < MAX_POOL) _pool[_poolCount++] = TARGET_MIDI_PITCHBEND;
  if (_poolCount < MAX_POOL) _pool[_poolCount++] = TARGET_EMPTY;
}

// =================================================================
// findSlotWithTarget — which slot has this target? (for steal logic)
// =================================================================
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
// Pot detection (direct ADC reads, independent of PotRouter)
// =================================================================
static const uint16_t POT_DETECT_THRESHOLD = 200;

void ToolPotMapping::samplePotBaselines() {
  for (uint8_t i = 0; i < 4; i++) {
    _potBaseline[i] = analogRead(PotRouter::POT_PINS[i]);
  }
}

int8_t ToolPotMapping::detectMovedPot(bool btnLeftHeld) {
  for (uint8_t i = 0; i < 4; i++) {
    uint16_t raw = analogRead(PotRouter::POT_PINS[i]);
    int16_t delta = (int16_t)raw - (int16_t)_potBaseline[i];
    if (delta < 0) delta = -delta;
    if ((uint16_t)delta > POT_DETECT_THRESHOLD) {
      _potBaseline[i] = raw;
      // Slot index: pot i alone = slot i*2, pot i + hold = slot i*2+1
      return (int8_t)(i * 2 + (btnLeftHeld ? 1 : 0));
    }
  }
  return -1;
}

// =================================================================
// saveMapping — direct NVS write (blocking, setup mode)
// =================================================================
bool ToolPotMapping::saveMapping() {
  // Stamp magic/version before save
  _wk.magic = EEPROM_MAGIC;
  _wk.version = POTMAP_VERSION;

  Preferences prefs;
  if (!prefs.begin(POTMAP_NVS_NAMESPACE, false)) return false;
  prefs.putBytes(POTMAP_NVS_KEY, &_wk, sizeof(PotMappingStore));
  prefs.end();

  // Apply to live PotRouter
  if (_potRouter) {
    _potRouter->applyMapping(_wk);
  }

  return true;
}

// =================================================================
// assignCurrentTarget — handle the assignment of pool[_poolIdx] to
// current slot, including steal confirmation, CC sub-editor, PB max-one
// =================================================================
void ToolPotMapping::assignCurrentTarget() {
  PotMapping* map = currentMap();
  PotTarget newTarget = _pool[_poolIdx];

  // --- Empty: direct assign, save, exit edit ---
  if (newTarget == TARGET_EMPTY) {
    map[_cursor].target = TARGET_EMPTY;
    map[_cursor].ccNumber = 0;
    if (saveMapping()) {
      _ui->showSaved();
      _editing = false;
    } else {
      Serial.printf("\r\n" VT_RED "  NVS write failed!" VT_RESET);
      delay(1500);
    }
    return;
  }

  // --- CC: enter CC# sub-editor ---
  if (newTarget == TARGET_MIDI_CC) {
    map[_cursor].target = TARGET_MIDI_CC;
    _ccNumber = map[_cursor].ccNumber;
    if (_ccNumber == 0) _ccNumber = 1;  // Default CC#
    _ccEditing = true;
    return;
  }

  // --- PB: max one per context, auto-steal ---
  if (newTarget == TARGET_MIDI_PITCHBEND) {
    int8_t existing = findSlotWithTarget(TARGET_MIDI_PITCHBEND);
    if (existing >= 0 && existing != (int8_t)_cursor) {
      map[existing].target = TARGET_EMPTY;
      map[existing].ccNumber = 0;
    }
    map[_cursor].target = TARGET_MIDI_PITCHBEND;
    map[_cursor].ccNumber = 0;
    if (saveMapping()) {
      _ui->showSaved();
      _editing = false;
    } else {
      Serial.printf("\r\n" VT_RED "  NVS write failed!" VT_RESET);
      delay(1500);
    }
    return;
  }

  // --- Regular param: check steal ---
  int8_t existing = findSlotWithTarget(newTarget);
  if (existing >= 0 && existing != (int8_t)_cursor) {
    // Already assigned elsewhere — enter steal confirmation
    _confirmSteal = true;
    _stealSourceSlot = existing;
    _stealTarget = newTarget;
    return;
  }

  // Not assigned elsewhere (or already on this slot) — direct assign + save
  map[_cursor].target = newTarget;
  map[_cursor].ccNumber = 0;
  if (saveMapping()) {
    _ui->showSaved();
    _editing = false;
  } else {
    Serial.printf("\r\n" VT_RED "  NVS write failed!" VT_RESET);
    delay(1500);
  }
}

// =================================================================
// drawScreen — full VT100 redraw
// =================================================================
void ToolPotMapping::drawScreen() {
  _ui->vtFrameStart();

  const char* ctxLabel = _contextNormal ? "NORMAL" : "ARPEG";
  char rightText[32];
  snprintf(rightText, sizeof(rightText), "[%s]  t=toggle", ctxLabel);
  _ui->drawHeader("POT MAPPING", rightText);
  Serial.printf(VT_CL "\n");

  // Draw 8 slot lines
  for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
    drawSlotLine(i);
  }

  Serial.printf(VT_CL "\n");
  drawPoolLine();
  Serial.printf(VT_CL "\n");
  drawDescription();
  Serial.printf(VT_CL "\n");
  drawHelpLine();

  _ui->vtFrameEnd();
}

// =================================================================
// drawSlotLine — one line for a slot
// =================================================================
void ToolPotMapping::drawSlotLine(uint8_t slot) {
  const PotMapping* map = currentMapConst();
  const PotMapping& pm = map[slot];
  bool selected = (_cursor == slot);
  bool isEditing = selected && _editing;

  // Build value string
  char val[20];
  if (pm.target == TARGET_MIDI_CC) {
    snprintf(val, sizeof(val), "CC %d", pm.ccNumber);
  } else {
    snprintf(val, sizeof(val), "%s", targetName(pm.target));
  }

  if (selected) {
    if (isEditing) {
      Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " %-13s" VT_CYAN "[%s]" VT_RESET VT_CL "\n",
                     slotName(slot), val);
    } else {
      Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " %-13s" VT_CYAN "%s" VT_RESET VT_CL "\n",
                     slotName(slot), val);
    }
  } else {
    if (pm.target == TARGET_EMPTY) {
      Serial.printf("    %-13s" VT_DIM "%s" VT_RESET VT_CL "\n", slotName(slot), val);
    } else {
      Serial.printf("    %-13s%s" VT_CL "\n", slotName(slot), val);
    }
  }
}

// =================================================================
// drawPoolLine — all assignable params, color-coded
// =================================================================
void ToolPotMapping::drawPoolLine() {
  bool inEdit = _editing || _ccEditing;

  Serial.printf("  " VT_DIM "Pool:" VT_RESET " ");

  const PotMapping* map = currentMapConst();

  for (uint8_t i = 0; i < _poolCount; i++) {
    PotTarget t = _pool[i];
    bool isAssigned = false;

    if (t == TARGET_EMPTY) {
      isAssigned = false;
    } else if (t == TARGET_MIDI_CC) {
      isAssigned = false;  // Multiple CCs allowed
    } else {
      int8_t owner = findSlotWithTarget(t);
      isAssigned = (owner >= 0 && owner != (int8_t)_cursor);
    }

    bool isCursor = inEdit && (_poolIdx == i);

    if (isCursor) {
      Serial.printf(VT_REVERSE VT_BOLD " %s " VT_RESET " ", targetName(t));
    } else if (isAssigned) {
      Serial.printf(VT_DIM "%s" VT_RESET " ", targetName(t));
    } else if (inEdit) {
      Serial.printf(VT_GREEN VT_BOLD "%s" VT_RESET " ", targetName(t));
    } else {
      // Navigation mode: show pool dimmed as reference
      Serial.printf(VT_DIM "%s" VT_RESET " ", targetName(t));
    }
  }
  Serial.printf(VT_CL "\n");
}

// =================================================================
// drawDescription — context-sensitive help for current pool selection
// =================================================================
void ToolPotMapping::drawDescription() {
  if (!_editing && !_ccEditing && !_confirmSteal) {
    Serial.printf(VT_CL "\n");
    return;
  }

  if (_confirmSteal) {
    Serial.printf(VT_YELLOW "  Already assigned to %s. Replace? (y/n)" VT_RESET VT_CL "\n",
                  slotName((uint8_t)_stealSourceSlot));
    return;
  }

  if (_ccEditing) {
    Serial.printf("  " VT_CYAN "CC#: [%d]" VT_RESET VT_CL "\n", _ccNumber);
    return;
  }

  // Show brief description of the currently highlighted pool target
  PotTarget t = _pool[_poolIdx];
  const char* desc = "";
  switch (t) {
    case TARGET_TEMPO_BPM:          desc = "Internal clock tempo (10-260 BPM)."; break;
    case TARGET_RESPONSE_SHAPE:     desc = "Pressure response curve shape."; break;
    case TARGET_SLEW_RATE:          desc = "Slew rate for pressure response smoothing."; break;
    case TARGET_AT_DEADZONE:        desc = "Aftertouch deadzone threshold."; break;
    case TARGET_PITCH_BEND:         desc = "Pitch bend offset (per-bank, NORMAL only)."; break;
    case TARGET_GATE_LENGTH:        desc = "Arp gate length (note duration vs step)."; break;
    case TARGET_SHUFFLE_DEPTH:      desc = "Shuffle intensity (0.0-1.0)."; break;
    case TARGET_SHUFFLE_TEMPLATE:   desc = "Groove template selection (5 templates)."; break;
    case TARGET_DIVISION:           desc = "Arp clock division (4/1 to 1/64)."; break;
    case TARGET_PATTERN:            desc = "Arp pattern (Up/Down/UpDown/Random/Order)."; break;
    case TARGET_BASE_VELOCITY:      desc = "Base velocity for note output (per-bank)."; break;
    case TARGET_VELOCITY_VARIATION: desc = "Random velocity variation range (per-bank)."; break;
    case TARGET_MIDI_CC:            desc = "Send MIDI CC on foreground channel."; break;
    case TARGET_MIDI_PITCHBEND:     desc = "Send MIDI Pitchbend on foreground channel. Max one per context."; break;
    case TARGET_EMPTY:              desc = "Remove assignment from this slot."; break;
    default: break;
  }
  Serial.printf("  " VT_DIM "%s" VT_RESET VT_CL "\n", desc);
}

// =================================================================
// drawHelpLine — bottom help text
// =================================================================
void ToolPotMapping::drawHelpLine() {
  if (_confirmSteal) {
    // No extra help needed — description line has the prompt
    Serial.printf(VT_CL "\n");
    return;
  }

  if (_ccEditing) {
    Serial.printf(VT_DIM "  [Left/Right] adjust CC#  [Enter] confirm  [q] cancel" VT_RESET VT_CL "\n");
    return;
  }

  if (_editing) {
    Serial.printf(VT_DIM "  [Left/Right] cycle pool  [Enter] confirm" VT_RESET VT_CL "\n");
  } else {
    Serial.printf(VT_DIM "  [Up/Down] navigate  [Enter] edit  [t] %s  [d] defaults  [q] quit" VT_RESET VT_CL "\n",
                  _contextNormal ? "NORMAL/ARPEG" : "ARPEG/NORMAL");
  }
}

// =================================================================
// run — main tool loop (blocking)
// =================================================================
void ToolPotMapping::run() {
  if (!_ui || !_leds) return;

  // Copy live mapping into working copy
  if (_potRouter) {
    memcpy(&_wk, &_potRouter->getMapping(), sizeof(PotMappingStore));
  } else {
    memcpy(&_wk, &PotRouter::DEFAULT_MAPPING, sizeof(PotMappingStore));
  }

  _contextNormal = true;
  _cursor = 0;
  _editing = false;
  _poolIdx = 0;
  _ccEditing = false;
  _ccNumber = 0;
  _confirmSteal = false;
  _stealSourceSlot = -1;
  _stealTarget = TARGET_EMPTY;

  buildPool();
  samplePotBaselines();

  InputParser input;
  bool screenDirty = true;
  bool confirmDefaults = false;

  _ui->vtClear();

  while (true) {
    _leds->update();

    // --- Physical pot detection (only when not editing) ---
    if (!_editing && !_ccEditing && !_confirmSteal && !confirmDefaults) {
      bool btnLeftHeld = (digitalRead(BTN_LEFT_PIN) == LOW);
      int8_t movedSlot = detectMovedPot(btnLeftHeld);
      if (movedSlot >= 0) {
        _cursor = (uint8_t)movedSlot;
        screenDirty = true;
      }
    }

    NavEvent ev = input.update();

    // --- Defaults confirmation sub-mode ---
    if (confirmDefaults) {
      if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
        if (_contextNormal) {
          memcpy(_wk.normalMap, PotRouter::DEFAULT_MAPPING.normalMap,
                 sizeof(PotMapping) * POT_MAPPING_SLOTS);
        } else {
          memcpy(_wk.arpegMap, PotRouter::DEFAULT_MAPPING.arpegMap,
                 sizeof(PotMapping) * POT_MAPPING_SLOTS);
        }
        if (saveMapping()) {
          _ui->showSaved();
        }
        confirmDefaults = false;
        buildPool();
        screenDirty = true;
      } else if (ev.type != NAV_NONE) {
        confirmDefaults = false;
        screenDirty = true;
      }
      delay(5);
      continue;
    }

    // --- Steal confirmation sub-mode ---
    if (_confirmSteal) {
      if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
        // Steal: orphan source, assign to current slot, save
        PotMapping* map = currentMap();
        map[_stealSourceSlot].target = TARGET_EMPTY;
        map[_stealSourceSlot].ccNumber = 0;
        map[_cursor].target = _stealTarget;
        map[_cursor].ccNumber = 0;
        if (saveMapping()) {
          _ui->showSaved();
          _confirmSteal = false;
          _editing = false;
        } else {
          Serial.printf("\r\n" VT_RED "  NVS write failed!" VT_RESET);
          delay(1500);
          _confirmSteal = false;  // exit steal prompt, stay in edit
        }
        screenDirty = true;
      } else if (ev.type != NAV_NONE) {
        // Cancel steal — stay in edit mode
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
        screenDirty = true;
      } else if (ev.type == NAV_RIGHT) {
        int step = ev.accelerated ? 10 : 1;
        int val = (int)_ccNumber + step;
        if (val > 127) val = 127;
        _ccNumber = (uint8_t)val;
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        // Confirm CC assignment
        PotMapping* map = currentMap();
        // Check for duplicate CC# BEFORE assigning cursor (otherwise
        // findSlotWithTarget may find the cursor itself if cursor < existing)
        int8_t dup = findSlotWithTarget(TARGET_MIDI_CC, _ccNumber);
        if (dup >= 0 && dup != (int8_t)_cursor) {
          map[dup].target = TARGET_EMPTY;
          map[dup].ccNumber = 0;
        }
        map[_cursor].target = TARGET_MIDI_CC;
        map[_cursor].ccNumber = _ccNumber;
        if (saveMapping()) {
          _ui->showSaved();
          _ccEditing = false;
          _editing = false;
        } else {
          Serial.printf("\r\n" VT_RED "  NVS write failed!" VT_RESET);
          delay(1500);
          // Stay in CC editor — user can retry Enter
        }
        screenDirty = true;
      } else if (ev.type == NAV_QUIT) {
        // Cancel CC editing — revert slot to previous state
        // (we haven't saved yet, so just exit CC sub-editor)
        _ccEditing = false;
        // Restore slot to what it was before entering CC mode
        // The slot was set to TARGET_MIDI_CC when entering, but not saved.
        // Reload from last saved state by re-reading working copy.
        // Actually, _wk still has the unsaved change. We need to revert.
        // Simplest: reload from the live router mapping.
        if (_potRouter) {
          const PotMappingStore& live = _potRouter->getMapping();
          const PotMapping* liveMap = _contextNormal ? live.normalMap : live.arpegMap;
          currentMap()[_cursor] = liveMap[_cursor];
        }
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
      // Toggle context NORMAL <-> ARPEG
      _contextNormal = !_contextNormal;
      _cursor = 0;
      _editing = false;
      _poolIdx = 0;
      buildPool();
      samplePotBaselines();
      _ui->vtClear();
      screenDirty = true;
    }

    if (ev.type == NAV_DEFAULTS && !_editing) {
      confirmDefaults = true;
      screenDirty = true;
    }

    if (!_editing) {
      // Navigation mode
      if (ev.type == NAV_UP) {
        if (_cursor > 0) _cursor--;
        else _cursor = POT_MAPPING_SLOTS - 1;
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        if (_cursor < POT_MAPPING_SLOTS - 1) _cursor++;
        else _cursor = 0;
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        _editing = true;
        // Set pool cursor to current assignment of this slot
        PotTarget current = currentMapConst()[_cursor].target;
        _poolIdx = 0;
        for (uint8_t i = 0; i < _poolCount; i++) {
          if (_pool[i] == current) {
            _poolIdx = i;
            break;
          }
        }
        screenDirty = true;
      }
    } else {
      // Editing mode (cycling pool)
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
        // Confirm assignment
        assignCurrentTarget();
        screenDirty = true;
      } else if (ev.type == NAV_QUIT) {
        // Cancel editing, revert slot to saved state
        if (_potRouter) {
          const PotMappingStore& live = _potRouter->getMapping();
          const PotMapping* liveMap = _contextNormal ? live.normalMap : live.arpegMap;
          currentMap()[_cursor] = liveMap[_cursor];
        }
        _editing = false;
        screenDirty = true;
      }
    }

    // --- Render ---
    if (screenDirty) {
      screenDirty = false;

      drawScreen();

      // Extra line for defaults confirmation
      if (confirmDefaults) {
        Serial.printf(VT_YELLOW "  Reset %s context to defaults? (y/n)" VT_RESET VT_CL "\n",
                      _contextNormal ? "NORMAL" : "ARPEG");
      }
    }

    delay(5);
  }
}
