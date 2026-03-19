#include "ScaleManager.h"
#include "../midi/MidiEngine.h"
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
{
  // Default pad assignments: root pads 8-14, mode pads 15-21
  for (uint8_t i = 0; i < 7; i++) {
    _rootPads[i] = 8 + i;   // Pads 8-14 → A,B,C,D,E,F,G
    _modePads[i] = 15 + i;  // Pads 15-21 → Ionian..Locrian
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

// =================================================================
// update() — called every loop iteration
// =================================================================
void ScaleManager::update(const uint8_t* keyIsPressed, bool btnRightHeld,
                           BankSlot& currentSlot) {
  _holding = btnRightHeld;

  if (btnRightHeld) {
    processScalePads(keyIsPressed, currentSlot);
  }

  // On release edge: snapshot state to prevent phantom notes
  if (!btnRightHeld && _lastBtnState) {
    if (_lastKeys) {
      memcpy(_lastKeys, keyIsPressed, NUM_KEYS);
    }
    // Reset scale pad edge detection
    memset(_lastScaleKeys, 0, sizeof(_lastScaleKeys));
  }

  _lastBtnState = btnRightHeld;
}

bool ScaleManager::isHolding() const {
  return _holding;
}

// =================================================================
// processScalePads — detect rising edges on root/mode/chromatic pads
// =================================================================
void ScaleManager::processScalePads(const uint8_t* keyIsPressed, BankSlot& slot) {

  // --- Root pads (0-6 → A,B,C,D,E,F,G) ---
  for (uint8_t r = 0; r < 7; r++) {
    uint8_t pad = _rootPads[r];
    if (pad >= NUM_KEYS) continue;

    bool pressed = keyIsPressed[pad];
    bool wasPressed = _lastScaleKeys[pad];

    if (pressed && !wasPressed) {
      // All notes off before changing scale (prevents orphan notes)
      if (_engine) _engine->allNotesOff();

      slot.scale.root = r;

      #if DEBUG_SERIAL
      Serial.printf("[SCALE] Root → %s, Mode → %s\n",
                    ROOT_NAMES[r], MODE_NAMES[slot.scale.mode]);
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
      if (_engine) _engine->allNotesOff();

      slot.scale.mode = m;

      #if DEBUG_SERIAL
      Serial.printf("[SCALE] Mode → %s, Root → %s\n",
                    MODE_NAMES[m], ROOT_NAMES[slot.scale.root]);
      #endif
    }
    _lastScaleKeys[pad] = pressed;
  }

  // --- Chromatic pad ---
  if (_chromaticPad < NUM_KEYS) {
    bool pressed = keyIsPressed[_chromaticPad];
    bool wasPressed = _lastScaleKeys[_chromaticPad];

    if (pressed && !wasPressed) {
      if (_engine) _engine->allNotesOff();

      slot.scale.chromatic = true;

      #if DEBUG_SERIAL
      Serial.printf("[SCALE] Chromatic ON (root %s)\n",
                    ROOT_NAMES[slot.scale.root]);
      #endif
    }
    _lastScaleKeys[_chromaticPad] = pressed;
  }
}
