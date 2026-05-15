#ifndef BANK_MANAGER_H
#define BANK_MANAGER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class MidiEngine;
class LedController;
class MidiTransport;

class BankManager {
public:
  BankManager();

  void begin(MidiEngine* engine, LedController* leds, BankSlot* banks,
             uint8_t* lastKeys, MidiTransport* transport);

  // Call every loop iteration with current key state and button state.
  // Returns true if a bank switch occurred this frame (instant or fast-forward on LEFT release).
  bool update(const uint8_t* keyIsPressed, bool btnLeftHeld);

  uint8_t  getCurrentBank() const;
  BankSlot& getCurrentSlot();
  bool     isHolding() const;

  // Set initial bank (from NVS, called before loop starts)
  void setCurrentBank(uint8_t bank);

  // Bank select pads (default 0-7, loadable from NVS later)
  void setBankPads(const uint8_t* pads);

  // Set double-tap window (copied from settings at boot; used to detect
  // LEFT + double-tap on bank pad → toggle arp Play/Stop on target bank).
  void setDoubleTapMs(uint8_t ms);

  // Set hold pad index (excluded from "any finger down" check in setCaptured).
  void setHoldPad(uint8_t padIdx);

  // Emit the bank-select Note On for the current bank on channel 16.
  // Called at boot, on BLE reconnect, and after a MIDI panic to resync the
  // DAW with the live state. Bank-switch transitions emit automatically.
  void emitBankSelectNote();

private:
  MidiEngine*    _engine;
  LedController* _leds;
  BankSlot*      _banks;
  uint8_t*       _lastKeys;    // main loop's s_lastKeys — reset on switch
  MidiTransport* _transport;

  uint8_t _currentBank;
  uint8_t _bankPads[NUM_BANKS];

  // Button state
  bool _holding;              // true while left button is held
  bool _lastBtnState;         // previous frame's button state

  // Bank pad edge detection (per bank pad, sampled prev frame)
  bool _bankPadLast[NUM_BANKS];

  // Double-tap tracking (per bank pad)
  uint32_t _lastBankPadPressTime[NUM_BANKS];  // 0 = never / consumed
  uint8_t  _doubleTapMs;

  // Hold pad index (passed to setCaptured to exclude from finger-down scan)
  uint8_t  _holdPad;

  // Pending deferred switch (ARPEG targets only)
  int8_t   _pendingSwitchBank;   // -1 = none, else 0..NUM_BANKS-1
  uint32_t _pendingSwitchTime;   // ms timestamp when armed

  // Set true when a switch occurred while LEFT held — used on LEFT release
  // to snapshot keyIsPressed into _lastKeys, avoiding phantom note events.
  bool _switchedDuringHold;

  // Commit a bank switch: PB reset, allNotesOff on old channel, channel swap,
  // PB restore on new bank, LED update, triggerEvent(EVT_BANK_SWITCH).
  void switchToBank(uint8_t newBank);

  // Send Note On/Off on canal 16 for one bank's select note.
  // on=true → vel 127, on=false → vel 0 (= noteOff convention).
  void sendBankSelectMidi(uint8_t bank, bool on);
};

#endif // BANK_MANAGER_H
