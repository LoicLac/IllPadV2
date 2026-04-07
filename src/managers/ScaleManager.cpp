#include "ScaleManager.h"
#include "../midi/MidiEngine.h"
#include "../arp/ArpEngine.h"
#include <Arduino.h>
#include <string.h>

// Root names for debug
#if DEBUG_SERIAL
static const char* ROOT_NAMES[7] = {"A", "B", "C", "D", "E", "F", "G"};
static const char* MODE_NAMES[7] = {
  "Ionian", "Dorian", "Phrygian", "Lydian",
  "Mixolydian", "Aeolian", "Locrian"
};
#endif

ScaleManager::ScaleManager()
  : _banks(nullptr)
  , _engine(nullptr)
  , _lastKeys(nullptr)
  , _holding(false)
  , _lastBtnState(false)
  , _chromaticPad(22)
  , _holdPad(23)
  , _scaleChangeType(SCALE_CHANGE_NONE)
  , _octaveChanged(false)
  , _holdToggled(false)
  , _newOctaveRange(1)
{
  // Default pad assignments: root pads 8-14, mode pads 15-21
  for (uint8_t i = 0; i < 7; i++) {
    _rootPads[i] = 8 + i;   // Pads 8-14 → A,B,C,D,E,F,G
    _modePads[i] = 15 + i;  // Pads 15-21 → Ionian..Locrian
  }
  // Default octave pads 25-28
  for (uint8_t i = 0; i < 4; i++) {
    _octavePads[i] = 25 + i;
  }
  memset(_lastScaleKeys, 0, sizeof(_lastScaleKeys));
}

void ScaleManager::begin(BankSlot* banks, MidiEngine* engine, uint8_t* lastKeys) {
  _banks    = banks;
  _engine   = engine;
  _lastKeys = lastKeys;
}

void ScaleManager::setRootPads(const uint8_t* pads) {
  memcpy(_rootPads, pads, 7);
}

void ScaleManager::setModePads(const uint8_t* pads) {
  memcpy(_modePads, pads, 7);
}

void ScaleManager::setChromaticPad(uint8_t pad) {
  _chromaticPad = pad;
}

void ScaleManager::setHoldPad(uint8_t pad) {
  _holdPad = pad;
}

void ScaleManager::setOctavePads(const uint8_t* pads) {
  memcpy(_octavePads, pads, 4);
}

bool ScaleManager::hasHoldToggled() {
  if (_holdToggled) {
    _holdToggled = false;
    return true;
  }
  return false;
}

bool ScaleManager::hasOctaveChanged() {
  if (_octaveChanged) {
    _octaveChanged = false;
    return true;
  }
  return false;
}

uint8_t ScaleManager::getNewOctaveRange() const {
  return _newOctaveRange;
}

// =================================================================
// update() — called every loop iteration
// =================================================================
void ScaleManager::update(const uint8_t* keyIsPressed, bool btnHeld,
                           BankSlot& currentSlot) {
  _holding = btnHeld;

  if (btnHeld) {
    processScalePads(keyIsPressed, currentSlot);
  }

  // On release edge: snapshot state to prevent phantom notes
  if (!btnHeld && _lastBtnState) {
    if (_lastKeys) {
      memcpy(_lastKeys, keyIsPressed, NUM_KEYS);
    }
    // Reset scale pad edge detection
    memset(_lastScaleKeys, 0, sizeof(_lastScaleKeys));
  }

  _lastBtnState = btnHeld;
}

bool ScaleManager::isHolding() const {
  return _holding;
}

ScaleChangeType ScaleManager::consumeScaleChange() {
  ScaleChangeType t = _scaleChangeType;
  _scaleChangeType = SCALE_CHANGE_NONE;
  return t;
}

// =================================================================
// processScalePads — detect rising edges on root/mode/chromatic pads
// =================================================================
void ScaleManager::processScalePads(const uint8_t* keyIsPressed, BankSlot& slot) {

  // LOOP banks bypass scale resolution — scale pads are no-op.
  // Still sync _lastScaleKeys to prevent phantom edges on subsequent
  // LOOP → ARPEG bank switch (scale, hold, and octave pads all matter).
  // Audit fix B6: also sync hold + octave entries (originally only the
  // 15 scale pads were synced, leaving hold/octave stale).
  if (slot.type == BANK_LOOP) {
    // Root / mode / chromatic
    for (uint8_t r = 0; r < 7; r++) {
      if (_rootPads[r] < NUM_KEYS) _lastScaleKeys[_rootPads[r]] = keyIsPressed[_rootPads[r]];
      if (_modePads[r] < NUM_KEYS) _lastScaleKeys[_modePads[r]] = keyIsPressed[_modePads[r]];
    }
    if (_chromaticPad < NUM_KEYS) _lastScaleKeys[_chromaticPad] = keyIsPressed[_chromaticPad];
    // Hold + octave (ARPEG-only roles, same phantom-edge risk on switch)
    if (_holdPad < NUM_KEYS) _lastScaleKeys[_holdPad] = keyIsPressed[_holdPad];
    for (uint8_t o = 0; o < 4; o++) {
      if (_octavePads[o] < NUM_KEYS) _lastScaleKeys[_octavePads[o]] = keyIsPressed[_octavePads[o]];
    }
    return;  // Skip root/mode/chrom processing AND hold/octave (already ARPEG-guarded below)
  }

  // --- Root pads (0-6 → A,B,C,D,E,F,G) ---
  for (uint8_t r = 0; r < 7; r++) {
    uint8_t pad = _rootPads[r];
    if (pad >= NUM_KEYS) continue;

    bool pressed = keyIsPressed[pad];
    bool wasPressed = _lastScaleKeys[pad];

    if (pressed && !wasPressed) {
      // NORMAL: all notes off before scale change (prevents orphan notes)
      // ARPEG: no allNotesOff — arp re-resolves at next tick
      if (slot.type == BANK_NORMAL && _engine) _engine->allNotesOff();

      slot.scale.root = r;
      slot.scale.chromatic = false;  // Selecting root exits chromatic
      _scaleChangeType = SCALE_CHANGE_ROOT;

      #if DEBUG_SERIAL
      Serial.printf("[SCALE] Root %s (mode %s)\n", ROOT_NAMES[r], MODE_NAMES[slot.scale.mode]);
      #endif
    }
    _lastScaleKeys[pad] = pressed;
  }

  // --- Mode pads (0-6 → Ionian..Locrian) ---
  for (uint8_t m = 0; m < 7; m++) {
    uint8_t pad = _modePads[m];
    if (pad >= NUM_KEYS) continue;

    bool pressed = keyIsPressed[pad];
    bool wasPressed = _lastScaleKeys[pad];

    if (pressed && !wasPressed) {
      if (slot.type == BANK_NORMAL && _engine) _engine->allNotesOff();

      slot.scale.mode = m;
      slot.scale.chromatic = false;  // Selecting mode exits chromatic
      _scaleChangeType = SCALE_CHANGE_MODE;

      #if DEBUG_SERIAL
      Serial.printf("[SCALE] Mode %s (root %s)\n", MODE_NAMES[m], ROOT_NAMES[slot.scale.root]);
      #endif
    }
    _lastScaleKeys[pad] = pressed;
  }

  // --- Chromatic pad ---
  if (_chromaticPad < NUM_KEYS) {
    bool pressed = keyIsPressed[_chromaticPad];
    bool wasPressed = _lastScaleKeys[_chromaticPad];

    if (pressed && !wasPressed) {
      if (slot.type == BANK_NORMAL && _engine) _engine->allNotesOff();

      slot.scale.chromatic = true;
      _scaleChangeType = SCALE_CHANGE_CHROMATIC;

      #if DEBUG_SERIAL
      Serial.printf("[SCALE] Chromatic (root %s)\n", ROOT_NAMES[slot.scale.root]);
      #endif
    }
    _lastScaleKeys[_chromaticPad] = pressed;
  }

  // --- HOLD pad (ARPEG banks only) ---
  if (_holdPad < NUM_KEYS && slot.type == BANK_ARPEG && slot.arpEngine) {
    bool pressed = keyIsPressed[_holdPad];
    bool wasPressed = _lastScaleKeys[_holdPad];

    if (pressed && !wasPressed) {
      bool newHold = !slot.arpEngine->isHoldOn();
      slot.arpEngine->setHold(newHold);
      _holdToggled = true;

      #if DEBUG_SERIAL
      Serial.printf("[ARP] Hold %s\n", newHold ? "ON" : "OFF");
      #endif
    }
    _lastScaleKeys[_holdPad] = pressed;
  }

  // --- Octave pads (ARPEG only, 1-4 octaves) ---
  if (slot.type == BANK_ARPEG && slot.arpEngine) {
    for (uint8_t o = 0; o < 4; o++) {
      uint8_t pad = _octavePads[o];
      if (pad >= NUM_KEYS) continue;

      bool pressed = keyIsPressed[pad];
      bool wasPressed = _lastScaleKeys[pad];

      if (pressed && !wasPressed) {
        _newOctaveRange = o + 1;  // 1-4
        _octaveChanged = true;
        slot.arpEngine->setOctaveRange(o + 1);

        #if DEBUG_SERIAL
        Serial.printf("[ARP] Octave %d\n", o + 1);
        #endif
      }
      _lastScaleKeys[pad] = pressed;
    }
  }
}
