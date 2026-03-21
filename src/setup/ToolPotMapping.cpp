#include "ToolPotMapping.h"
#include "SetupUI.h"
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
    case TARGET_EMPTY:              return "empty";
    default:                        return "???";
  }
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
  , _context(0), _selectedSlot(-1), _poolCursorIdx(0)
  , _ccSubMode(false), _poolCount(0)
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
  if (_context == 0) {
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
  // Then: empty, CC, PB
  if (_poolCount < MAX_POOL) _pool[_poolCount++] = TARGET_EMPTY;
  if (_poolCount < MAX_POOL) _pool[_poolCount++] = TARGET_MIDI_CC;
  if (_poolCount < MAX_POOL) _pool[_poolCount++] = TARGET_MIDI_PITCHBEND;
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
  return (_context == 0) ? _wk.normalMap : _wk.arpegMap;
}

const PotMapping* ToolPotMapping::currentMapConst() const {
  return (_context == 0) ? _wk.normalMap : _wk.arpegMap;
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
// drawScreen — full VT100 redraw
// =================================================================
void ToolPotMapping::drawScreen() {
  _ui->vtFrameStart();
  Serial.printf(VT_HOME);

  const char* ctxName = (_context == 0) ? "NORMAL" : "ARPEG";
  Serial.printf(VT_BOLD "=== [6] Pot Mapping -- %s ===" VT_RESET VT_CL "\n", ctxName);
  Serial.printf(VT_CL "\n");

  drawGrid();
  Serial.printf(VT_CL "\n");
  drawPoolLine();
  Serial.printf(VT_CL "\n");
  drawStatus();

  _ui->vtFrameEnd();
}

// =================================================================
// drawGrid — 4 rows (R1-R4), 2 columns (alone, +hold)
// =================================================================
void ToolPotMapping::drawGrid() {
  const PotMapping* map = currentMapConst();

  Serial.printf("  " VT_DIM "Pot" VT_RESET "  |  " VT_DIM "Alone" VT_RESET
                "           |  " VT_DIM "+ Hold Left" VT_RESET VT_CL "\n");
  Serial.printf("  -----+------------------+------------------" VT_CL "\n");

  for (uint8_t pot = 0; pot < 4; pot++) {
    uint8_t slotAlone = pot * 2;
    uint8_t slotHold  = pot * 2 + 1;

    // Format each slot with selection highlight
    for (uint8_t col = 0; col < 2; col++) {
      uint8_t slot = (col == 0) ? slotAlone : slotHold;
      const PotMapping& pm = map[slot];
      bool isSelected = (_selectedSlot == (int8_t)slot);

      if (col == 0) {
        Serial.printf("  R%d   |  ", pot + 1);
      } else {
        Serial.printf("  ");
      }

      // Build display string for this slot
      char buf[20];
      if (pm.target == TARGET_MIDI_CC) {
        snprintf(buf, sizeof(buf), "CC %d", pm.ccNumber);
      } else {
        snprintf(buf, sizeof(buf), "%s", targetName(pm.target));
      }

      if (isSelected) {
        Serial.printf(VT_REVERSE VT_BOLD " > %-12s < " VT_RESET, buf);
      } else if (pm.target == TARGET_EMPTY) {
        Serial.printf(VT_DIM "  %-14s  " VT_RESET, buf);
      } else {
        Serial.printf("  %-14s  ", buf);
      }

      if (col == 0) {
        Serial.printf("|");
      }
    }
    Serial.printf(VT_CL "\n");
  }
}

// =================================================================
// drawPoolLine — all assignable params, color-coded
// =================================================================
void ToolPotMapping::drawPoolLine() {
  const PotMapping* map = currentMapConst();

  Serial.printf("  " VT_DIM "-- Available " VT_RESET);
  // Separator
  for (uint8_t i = 0; i < 44; i++) Serial.print('-');
  Serial.printf(VT_CL "\n  ");

  for (uint8_t i = 0; i < _poolCount; i++) {
    PotTarget t = _pool[i];
    bool isAssigned = false;

    if (t == TARGET_EMPTY) {
      // "empty" is always available (can be on multiple slots)
      isAssigned = false;
    } else if (t == TARGET_MIDI_CC) {
      // CC is always available (multiple CCs allowed with different numbers)
      isAssigned = false;
    } else {
      isAssigned = (findSlotWithTarget(t) >= 0);
    }

    bool isCursor = (_selectedSlot >= 0 && _poolCursorIdx == (int8_t)i);

    if (isCursor) {
      // Cursor position — highlighted
      Serial.printf(VT_REVERSE VT_BOLD " %s " VT_RESET " ", targetName(t));
    } else if (isAssigned) {
      // Already on a slot — dim
      Serial.printf(VT_DIM "%s" VT_RESET " ", targetName(t));
    } else {
      // Available — bright green
      Serial.printf(VT_GREEN VT_BOLD "%s" VT_RESET " ", targetName(t));
    }
  }
  Serial.printf(VT_CL "\n");
}

// =================================================================
// drawStatus — bottom status line with commands
// =================================================================
void ToolPotMapping::drawStatus() {
  if (_ccSubMode) {
    // CC number input mode
    PotMapping* map = currentMap();
    uint8_t slot = (uint8_t)_selectedSlot;
    Serial.printf("  " VT_CYAN "CC#: [%d]" VT_RESET
                  "   < > adjust   ENTER confirm   q cancel" VT_CL "\n",
                  map[slot].ccNumber);
  } else if (_selectedSlot >= 0) {
    // Slot selected, cycling pool
    Serial.printf("  Turn pot to select  |  " VT_BOLD "< >" VT_RESET " cycle  |  "
                  VT_BOLD "ENTER" VT_RESET " context  |  "
                  VT_BOLD "s" VT_RESET "ave  " VT_BOLD "d" VT_RESET "efault  "
                  VT_BOLD "q" VT_RESET "uit" VT_CL "\n");
  } else {
    // No slot selected
    Serial.printf("  Turn a pot to select  |  "
                  VT_BOLD "ENTER" VT_RESET " context  |  "
                  VT_BOLD "s" VT_RESET "ave  " VT_BOLD "d" VT_RESET "efault  "
                  VT_BOLD "q" VT_RESET "uit" VT_CL "\n");
  }
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

  _context = 0;
  _selectedSlot = -1;
  _poolCursorIdx = 0;
  _ccSubMode = false;
  _unsaved = false;
  memset(&_ccPrevMapping, 0, sizeof(_ccPrevMapping));

  buildPool();
  samplePotBaselines();

  _ui->vtClear();
  bool screenDirty = true;
  unsigned long lastRefresh = 0;

  while (true) {
    _leds->update();

    // Detect pot movement (physical pot selection)
    bool btnLeftHeld = (digitalRead(BTN_LEFT_PIN) == LOW);
    int8_t movedSlot = detectMovedPot(btnLeftHeld);

    if (movedSlot >= 0 && !_ccSubMode) {
      _selectedSlot = movedSlot;
      // Set pool cursor to current assignment of this slot
      PotMapping* map = currentMap();
      PotTarget current = map[_selectedSlot].target;
      _poolCursorIdx = 0;
      for (uint8_t i = 0; i < _poolCount; i++) {
        if (_pool[i] == current) {
          _poolCursorIdx = (int8_t)i;
          break;
        }
      }
      screenDirty = true;
    }

    char input = _ui->readInput();
    if (input == 0) {
      // No input — refresh if needed
      if (screenDirty || millis() - lastRefresh >= 500) {
        drawScreen();
        screenDirty = false;
        lastRefresh = millis();
      }
      delay(5);
      continue;
    }

    // --- CC sub-mode: < > adjust number, ENTER confirm, q cancel ---
    if (_ccSubMode) {
      PotMapping* map = currentMap();
      uint8_t slot = (uint8_t)_selectedSlot;
      switch (input) {
        case ',': case '<': case '[':
          if (map[slot].ccNumber > 0) map[slot].ccNumber--;
          break;
        case '.': case '>': case ']':
          if (map[slot].ccNumber < 127) map[slot].ccNumber++;
          break;
        case '\r': case '\n':
          // Confirm CC assignment — check for duplicate CC#
          {
            int8_t existing = findSlotWithTarget(TARGET_MIDI_CC, map[slot].ccNumber);
            if (existing >= 0 && existing != _selectedSlot) {
              map[existing].target = TARGET_EMPTY;
              map[existing].ccNumber = 0;
            }
          }
          _ccSubMode = false;
          _unsaved = true;
          break;
        case 'q':
          // B6 fix: restore previous assignment instead of setting to empty
          map[slot] = _ccPrevMapping;
          _ccSubMode = false;
          break;
      }
      screenDirty = true;
      continue;
    }

    // --- Normal mode ---
    switch (input) {
      case ',': case '<': case '[':
      case '.': case '>': case ']':
      {
        // Cycle pool cursor left or right
        if (_selectedSlot >= 0 && _poolCount > 0) {
          if (input == ',' || input == '<' || input == '[') {
            _poolCursorIdx--;
            if (_poolCursorIdx < 0) _poolCursorIdx = _poolCount - 1;
          } else {
            _poolCursorIdx++;
            if (_poolCursorIdx >= (int8_t)_poolCount) _poolCursorIdx = 0;
          }

          PotMapping* map = currentMap();
          PotTarget newTarget = _pool[_poolCursorIdx];

          // Steal: if already assigned elsewhere, orphan the source
          // R3 fix: unified steal handles all targets including PB (no separate PB block)
          if (newTarget != TARGET_EMPTY && newTarget != TARGET_MIDI_CC) {
            int8_t existing = findSlotWithTarget(newTarget);
            if (existing >= 0 && existing != _selectedSlot) {
              map[existing].target = TARGET_EMPTY;
              map[existing].ccNumber = 0;
            }
          }

          // B6 fix: save current mapping before overwriting (for CC cancel restore)
          _ccPrevMapping = map[_selectedSlot];

          map[_selectedSlot].target = newTarget;
          if (newTarget == TARGET_MIDI_CC) {
            map[_selectedSlot].ccNumber = 1;  // Default CC#
            _ccSubMode = true;
          } else {
            _unsaved = true;
          }
        }
        screenDirty = true;
        break;
      }

      case '\r': case '\n':
        // Toggle context (NORMAL <-> ARPEG)
        _context = 1 - _context;
        _selectedSlot = -1;
        _poolCursorIdx = 0;
        _ccSubMode = false;
        buildPool();
        samplePotBaselines();
        _ui->vtClear();
        screenDirty = true;
        break;

      case 's':
        // Save
        if (saveMapping()) {
          _ui->showSaved();
          _unsaved = false;
          Serial.printf(VT_GREEN "  Saved! Applied to PotRouter." VT_RESET VT_CL "\n");
        } else {
          Serial.printf(VT_RED "  Save failed!" VT_RESET VT_CL "\n");
        }
        delay(800);
        screenDirty = true;
        break;

      case 'd':
        // B4 fix: reset only current context to defaults
        if (_context == 0) {
          memcpy(_wk.normalMap, PotRouter::DEFAULT_MAPPING.normalMap,
                 sizeof(PotMapping) * POT_MAPPING_SLOTS);
        } else {
          memcpy(_wk.arpegMap, PotRouter::DEFAULT_MAPPING.arpegMap,
                 sizeof(PotMapping) * POT_MAPPING_SLOTS);
        }
        _selectedSlot = -1;
        _poolCursorIdx = 0;
        _unsaved = true;
        buildPool();
        screenDirty = true;
        break;

      case 'q':
        // B5 fix: confirm quit if unsaved changes
        if (_unsaved) {
          Serial.printf(VT_YELLOW "  Unsaved changes! Press q again to discard, s to save."
                        VT_RESET VT_CL "\n");
          // Wait for confirmation
          while (true) {
            _leds->update();
            char confirm = _ui->readInput();
            if (confirm == 'q') return;   // Discard
            if (confirm == 's') {
              if (saveMapping()) {
                _ui->showSaved();
                delay(400);
              }
              return;
            }
            if (confirm != 0) break;  // Any other key = cancel quit
            delay(5);
          }
          screenDirty = true;
        } else {
          return;
        }
        break;
    }
  }
}
