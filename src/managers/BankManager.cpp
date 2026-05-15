#include "BankManager.h"
#include "../midi/MidiEngine.h"
#include "../core/MidiTransport.h"
#include "../core/LedController.h"
#include "../arp/ArpEngine.h"
#include <Arduino.h>
#include <string.h>

BankManager::BankManager()
  : _engine(nullptr)
  , _leds(nullptr)
  , _banks(nullptr)
  , _lastKeys(nullptr)
  , _transport(nullptr)
  , _currentBank(DEFAULT_BANK)
  , _holding(false)
  , _lastBtnState(false)
  , _doubleTapMs(DOUBLE_TAP_MS_DEFAULT)
  , _holdPad(0xFF)
  , _pendingSwitchBank(-1)
  , _pendingSwitchTime(0)
  , _switchedDuringHold(false)
{
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _bankPads[i] = i;
    _bankPadLast[i] = false;
    _lastBankPadPressTime[i] = 0;
  }
}

void BankManager::begin(MidiEngine* engine, LedController* leds,
                         BankSlot* banks, uint8_t* lastKeys,
                         MidiTransport* transport) {
  _engine    = engine;
  _leds      = leds;
  _banks     = banks;
  _lastKeys  = lastKeys;
  _transport = transport;

  if (_leds) _leds->setCurrentBank(_currentBank);
}

void BankManager::setBankPads(const uint8_t* pads) {
  memcpy(_bankPads, pads, NUM_BANKS);
}

void BankManager::setDoubleTapMs(uint8_t ms) {
  _doubleTapMs = ms;
}

void BankManager::setHoldPad(uint8_t padIdx) {
  _holdPad = padIdx;
}

// =================================================================
// update() — called every loop iteration
// =================================================================
bool BankManager::update(const uint8_t* keyIsPressed, bool btnLeftHeld) {
  bool switched = false;
  _holding = btnLeftHeld;
  uint32_t now = millis();

  // --- Detect LEFT button rising edge (start of a new hold) ---
  if (btnLeftHeld && !_lastBtnState) {
    _switchedDuringHold = false;
  }

  // --- Bank pad edge detection + double-tap/switch logic (while LEFT held) ---
  if (btnLeftHeld) {
    for (uint8_t b = 0; b < NUM_BANKS; b++) {
      bool isPressed = keyIsPressed[_bankPads[b]];
      bool wasPressed = _bankPadLast[b];
      _bankPadLast[b] = isPressed;

      if (!isPressed || wasPressed) continue;  // only rising edges

      // Rising edge on bank pad b
      bool wasRecent = (_lastBankPadPressTime[b] != 0) &&
                       ((now - _lastBankPadPressTime[b]) < _doubleTapMs);

      // --- Double-tap on ARPEG/ARPEG_GEN bank pad = Play/Stop toggle ---
      // Same event chain as hold pad on FG; BG banks pass keys=nullptr
      // (no fingers possible off-foreground → pile kept, paused).
      // Rule: double-tap NEVER changes bank. Always consume the 2nd tap.
      // LOOP : double-tap handler à câbler par plan LOOP Phase 1 (else if BANK_LOOP).
      if (wasRecent && isArpType(_banks[b].type)) {
        if (_banks[b].arpEngine && _transport) {
          bool wasCaptured = _banks[b].arpEngine->isCaptured();
          const uint8_t* keys = (b == _currentBank) ? keyIsPressed : nullptr;
          _banks[b].arpEngine->setCaptured(!wasCaptured, *_transport, keys, _holdPad);
          if (_leds) {
            EventId evt = _banks[b].arpEngine->isCaptured() ? EVT_PLAY : EVT_STOP;
            _leds->triggerEvent(evt, (uint8_t)(1 << b));
          }
        }
        _lastBankPadPressTime[b] = 0;
        _pendingSwitchBank = -1;
        continue;  // never fall through — 2nd tap on ARPEG/ARPEG_GEN is always consumed
      }

      // --- 2nd tap on NORMAL (wasRecent, but no play/stop semantics) ---
      // Ignore: preserve pending switch and 1st-tap timestamp so the
      // natural timeout still commits at T0 + _doubleTapMs. Re-arming
      // would postpone the switch on every repeat tap.
      if (wasRecent) continue;

      // --- 1st-tap logic ---
      _lastBankPadPressTime[b] = now;
      if (b == _currentBank) continue;

      // Defer switch ~doubleTapMs for ALL bank types so the tactile
      // feel is uniform (NORMAL and ARPEG switches use the same delay).
      // ARPEG also uses this window to detect double-tap Play/Stop.
      _pendingSwitchBank = (int8_t)b;
      _pendingSwitchTime = now;
    }
  } else {
    // LEFT not held — reset edge tracking so next hold detects rising edges cleanly
    for (uint8_t b = 0; b < NUM_BANKS; b++) _bankPadLast[b] = false;
  }

  // --- Pending switch expiration (natural timeout) ---
  if (_pendingSwitchBank >= 0 &&
      (now - _pendingSwitchTime) >= _doubleTapMs) {
    uint8_t target = (uint8_t)_pendingSwitchBank;
    _pendingSwitchBank = -1;
    if (target != _currentBank) {
      switchToBank(target);
      if (btnLeftHeld) _switchedDuringHold = true;
      switched = true;
    }
  }

  // --- Detect LEFT button release edge (held → not held) ---
  if (!btnLeftHeld && _lastBtnState) {
    // Fast-forward pending switch on LEFT release
    if (_pendingSwitchBank >= 0) {
      uint8_t target = (uint8_t)_pendingSwitchBank;
      _pendingSwitchBank = -1;
      if (target != _currentBank) {
        switchToBank(target);
        _switchedDuringHold = true;
        switched = true;
      }
    }
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

  uint8_t oldBank = _currentBank;

  // Reset pitch bend to center on old bank's channel, then all notes off
  if (_engine) {
    _engine->sendPitchBend(8192);
    _engine->allNotesOff();
  }

  _banks[_currentBank].isForeground = false;
  _currentBank = newBank;
  _banks[_currentBank].isForeground = true;

  if (_engine) {
    _engine->setChannel(_currentBank);
    if (_banks[_currentBank].type == BANK_NORMAL)
      _engine->sendPitchBend(_banks[_currentBank].pitchBendOffset);
  }

  if (_leds) {
    _leds->setCurrentBank(_currentBank);
    _leds->triggerEvent(EVT_BANK_SWITCH);
  }

  // Bank-select MIDI notification on canal 16 (DAW resync).
  sendBankSelectMidi(oldBank, false);
  sendBankSelectMidi(_currentBank, true);

  #if DEBUG_SERIAL
  const char* typeLabel = "?";
  switch (_banks[_currentBank].type) {
    case BANK_NORMAL:    typeLabel = "NORMAL";    break;
    case BANK_ARPEG:     typeLabel = "ARPEG";     break;
    case BANK_ARPEG_GEN: typeLabel = "ARPEG_GEN"; break;
    case BANK_LOOP:      typeLabel = "LOOP";      break;  // placeholder LOOP Phase 1
    default:             typeLabel = "?";         break;
  }
  Serial.printf("[BANK] Bank %d (ch %d, %s)\n",
                _currentBank + 1, _currentBank + 1, typeLabel);
  #endif
}

// =================================================================
// Bank-select MIDI helpers (canal 16, notes BASE..BASE+7)
// =================================================================
void BankManager::sendBankSelectMidi(uint8_t bank, bool on) {
  if (!_transport || bank >= NUM_BANKS) return;
  _transport->sendNoteOn(BANK_SELECT_MIDI_CHANNEL,
                          BANK_SELECT_MIDI_BASE_NOTE + bank,
                          on ? BANK_SELECT_MIDI_VELOCITY : 0);
}

void BankManager::emitBankSelectNote() {
  sendBankSelectMidi(_currentBank, true);
}
