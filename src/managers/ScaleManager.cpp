#include "ScaleManager.h"
#include "../midi/MidiEngine.h"
#include "../arp/ArpEngine.h"
#include "../viewer/ViewerSerial.h"
#include <Arduino.h>
#include <string.h>

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

  // Invariant 6 §23 spec LOOP : pas de scale sur une bank LOOP. Les pads
  // scale/root/mode/chrom porteront d'autres rôles en contexte LOOP (slots
  // sous LEFT, Phase 3 Tool 3 b1). Early-return évite mutation gratuite
  // de slot.scale + déclenchement confirm LED + NVS write inutile.
  if (slot.type == BANK_LOOP) return;

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

      viewer::emitScale(viewer::SCALE_ROOT, r, slot.scale.mode);
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

      viewer::emitScale(viewer::SCALE_MODE, slot.scale.root, m);
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

      viewer::emitScale(viewer::SCALE_CHROMATIC, slot.scale.root, slot.scale.mode);
    }
    _lastScaleKeys[_chromaticPad] = pressed;
  }

  // --- Octave pads (ARPEG/ARPEG_GEN, 1-4) ---
  // ARPEG     : pad o+1 = octaveRange (1..4 octaves litterales, spec §7).
  // ARPEG_GEN : pad o+1 = mutationLevel (1=lock, 2=1/16, 3=1/8, 4=1/4 — spec §15).
  // Branchement par _engineMode (et non par BankType) : robuste face a une desync hypothetique.
  // Le champ NVS (ArpPotStore.octaveRange) est dual-semantic et stocke o+1 ∈ [1..4] dans les
  // deux cas — queueArpOctaveWrite (main.cpp:693) marche sans modif.
  if (isArpType(slot.type) && slot.arpEngine) {
    for (uint8_t o = 0; o < 4; o++) {
      uint8_t pad = _octavePads[o];
      if (pad >= NUM_KEYS) continue;

      bool pressed = keyIsPressed[pad];
      bool wasPressed = _lastScaleKeys[pad];

      if (pressed && !wasPressed) {
        _newOctaveRange = o + 1;  // 1-4
        _octaveChanged = true;
        if (slot.arpEngine->getEngineMode() == EngineMode::GENERATIVE) {
          slot.arpEngine->setMutationLevel(o + 1);
          viewer::emitArpGenMutation(o + 1);
        } else {
          slot.arpEngine->setOctaveRange(o + 1);
          viewer::emitArpOctave(o + 1);
        }
      }
      _lastScaleKeys[pad] = pressed;
    }
  }
}
