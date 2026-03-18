#include "ClockManager.h"

ClockManager::ClockManager()
  : _rawTickCount(0), _lastRawTickTime(0),
    _pllBPM(120.0f), _pllPhase(0.0f), _pllTickInterval(0.0f),
    _lastPllUpdate(0),
    _activeSource(SRC_INTERNAL), _externalTimeoutMs(2000),
    _internalBPM(120), _currentTick(0) {}

void ClockManager::begin() {
  // TODO: init PLL state
}

void ClockManager::update() {
  // TODO: check for external timeout, generate ticks, update PLL
}

void ClockManager::onMidiClockTick() {
  // TODO: record raw tick, update source priority
}

void ClockManager::onMidiStart() {
  // TODO: reset tick counter
}

void ClockManager::onMidiStop() {
  // TODO: handle transport stop
}

void ClockManager::setInternalBPM(uint16_t bpm) {
  _internalBPM = bpm;
}

uint32_t ClockManager::getCurrentTick() const  { return _currentTick; }
uint16_t ClockManager::getSmoothedBPM() const  { return (uint16_t)_pllBPM; }
bool     ClockManager::isExternalSync() const   { return _activeSource != SRC_INTERNAL; }

void ClockManager::updatePLL() {
  // TODO: recalibrate PLL from external ticks
}

void ClockManager::generateInternalTicks() {
  // TODO: generate ticks at internal BPM when no external source
}
