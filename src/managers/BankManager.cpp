#include "BankManager.h"
#include "../midi/MidiEngine.h"
#include "../core/LedController.h"
#include <Arduino.h>
#include <string.h>

BankManager::BankManager()
  : _engine(nullptr)
  , _leds(nullptr)
  , _banks(nullptr)
  , _lastKeys(nullptr)
  , _currentBank(DEFAULT_BANK)
  , _holding(false)
  , _lastBtnState(false)
  , _armed(true)
  , _switchedDuringHold(false)
{
  // Default bank pads: pad 0 → bank 0, pad 1 → bank 1, ...
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _bankPads[i] = i;
  }
}

void BankManager::begin(MidiEngine* engine, LedController* leds,
                         BankSlot* banks, uint8_t* lastKeys) {
  _engine   = engine;
  _leds     = leds;
  _banks    = banks;
  _lastKeys = lastKeys;

  // Show initial bank on LED
  if (_leds) _leds->setCurrentBank(_currentBank);
}

void BankManager::setBankPads(const uint8_t* pads) {
  memcpy(_bankPads, pads, NUM_BANKS);
}

// =================================================================
// update() — called every loop iteration
// =================================================================
bool BankManager::update(const uint8_t* keyIsPressed, bool btnLeftHeld) {
  bool switched = false;
  _holding = btnLeftHeld;

  // --- Detect button press edge (not held → held) ---
  if (btnLeftHeld && !_lastBtnState) {
    _switchedDuringHold = false;
    _armed = true;
  }

  // --- While button is held: bank select logic ---
  if (btnLeftHeld) {
    // Re-arm when all bank pads are released (prevents oscillation)
    if (!_armed) {
      bool anyBankPadHeld = false;
      for (uint8_t b = 0; b < NUM_BANKS; b++) {
        if (keyIsPressed[_bankPads[b]]) {
          anyBankPadHeld = true;
          break;
        }
      }
      if (!anyBankPadHeld) _armed = true;
    }

    // Check for bank pad press while armed
    if (_armed) {
      for (uint8_t b = 0; b < NUM_BANKS; b++) {
        if (keyIsPressed[_bankPads[b]] && b != _currentBank) {
          switchToBank(b);
          _armed = false;
          _switchedDuringHold = true;
          switched = true;
          break;
        }
      }
    }
  }

  // --- Detect button release edge (held → not held) ---
  if (!btnLeftHeld && _lastBtnState) {
    if (_switchedDuringHold && _lastKeys) {
      // Snapshot current state as "previous" — prevents phantom
      // noteOff/noteOn when resuming normal play after a switch.
      memcpy(_lastKeys, keyIsPressed, NUM_KEYS);
    }
  }

  _lastBtnState = btnLeftHeld;
  return switched;
}

uint8_t BankManager::getCurrentBank() const {
  return _currentBank;
}

void BankManager::setCurrentBank(uint8_t bank) {
  if (bank >= NUM_BANKS) return;
  _banks[_currentBank].isForeground = false;
  _currentBank = bank;
  _banks[_currentBank].isForeground = true;
  if (_engine) _engine->setChannel(_currentBank);
  if (_leds) _leds->setCurrentBank(_currentBank);
}

BankSlot& BankManager::getCurrentSlot() {
  return _banks[_currentBank];
}

bool BankManager::isHolding() const {
  return _holding;
}

// =================================================================
// switchToBank — all notes off, change channel, update LED
// =================================================================
void BankManager::switchToBank(uint8_t newBank) {
  if (newBank >= NUM_BANKS || newBank == _currentBank) return;

  // Reset pitch bend to center on old bank's channel, then all notes off
  if (_engine) {
    _engine->sendPitchBend(8192);  // PB center before switching channel
    _engine->allNotesOff();
  }

  // Update foreground flags
  _banks[_currentBank].isForeground = false;
  _currentBank = newBank;
  _banks[_currentBank].isForeground = true;

  // Switch MIDI channel to match new bank + restore pitch bend
  if (_engine) {
    _engine->setChannel(_currentBank);
    _engine->sendPitchBend(_banks[_currentBank].pitchBendOffset);
  }

  // Update LED + confirmation blink
  if (_leds) {
    _leds->setCurrentBank(_currentBank);
    _leds->triggerConfirm(CONFIRM_BANK_SWITCH);
  }

  // Reset edge detection — prevents phantom notes
  #if DEBUG_SERIAL
  Serial.printf("[BANK] Bank %d (ch %d, %s)\n",
                _currentBank + 1, _currentBank + 1,
                _banks[_currentBank].type == BANK_ARPEG ? "ARPEG" : "NORMAL");
  #endif
}