#include "BankManager.h"
#include "../core/MidiTransport.h"

BankManager::BankManager()
  : _transport(nullptr), _banks(nullptr), _currentBank(DEFAULT_BANK),
    _holding(false), _lastBtnState(false) {}

void BankManager::begin(MidiTransport* transport, BankSlot* banks) {
  _transport = transport;
  _banks = banks;
}

void BankManager::update(const uint8_t* keyIsPressed, bool btnLeftHeld) {
  (void)keyIsPressed;
  (void)btnLeftHeld;
  // TODO: implement bank select logic (hold btn + pad)
}

uint8_t BankManager::getCurrentBank() const {
  return _currentBank;
}

BankSlot& BankManager::getCurrentSlot() {
  return _banks[_currentBank];
}

bool BankManager::isHolding() const {
  return _holding;
}

void BankManager::switchToBank(uint8_t newBank) {
  (void)newBank;
  // TODO: implement all-notes-off for NORMAL, swap foreground
}
