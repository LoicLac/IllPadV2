#include "ScaleManager.h"

ScaleManager::ScaleManager()
  : _banks(nullptr), _holding(false), _lastBtnState(false),
    _chromaticPad(0xFF), _octavePad(0xFF), _holdPad(0xFF), _playStopPad(0xFF) {
  for (int i = 0; i < 7; i++) { _rootPads[i] = 0xFF; _modePads[i] = 0xFF; }
  for (int i = 0; i < 5; i++) { _patternPads[i] = 0xFF; }
}

void ScaleManager::begin(BankSlot* banks) {
  _banks = banks;
}

void ScaleManager::update(const uint8_t* keyIsPressed, bool btnRightHeld, BankSlot& currentSlot) {
  (void)keyIsPressed;
  (void)btnRightHeld;
  (void)currentSlot;
  // TODO: implement scale/arp pad controls during hold
}

bool ScaleManager::isHolding() const {
  return _holding;
}

void ScaleManager::processScalePads(const uint8_t* keyIsPressed, BankSlot& slot) {
  (void)keyIsPressed;
  (void)slot;
  // TODO: root, mode, chromatic toggle
}

void ScaleManager::processArpPads(const uint8_t* keyIsPressed, BankSlot& slot) {
  (void)keyIsPressed;
  (void)slot;
  // TODO: pattern, octave, hold, play/stop
}
