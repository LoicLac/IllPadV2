#include "ClockManager.h"
#include "../core/HardwareConfig.h"
#include "../core/MidiTransport.h"
#include <Arduino.h>

ClockManager::ClockManager()
  : _lastExternalTickUs(0), _lastSource(0), _newTickAvailable(false),
    _tickIntervalIdx(0), _tickIntervalCount(0), _prevTickUs(0),
    _pllBPM(120.0f), _pllTickInterval(0.0f), _lastKnownBPM(0.0f),
    _activeSource(SRC_INTERNAL),
    _masterMode(false), _transport(nullptr),
    _internalBPM(120), _currentTick(0), _lastTickTimeUs(0),
    _rawTickCount(0)
{
  memset(_tickIntervals, 0, sizeof(_tickIntervals));
}

void ClockManager::begin(MidiTransport* transport) {
  _transport = transport;
  _lastTickTimeUs = micros();
  // Start with internal clock — PLL takes over when external ticks arrive
  _pllTickInterval = 60000000.0f / (_internalBPM * 24.0f);

  #if DEBUG_SERIAL
  Serial.println("[CLOCK] ClockManager initialized (internal clock).");
  #endif
}

// Called from MidiTransport — USB: from Core 1 loop, BLE: from NimBLE task
void ClockManager::onMidiClockTick(uint8_t source) {
  if (_masterMode) return;  // Ignore external ticks in master mode
  _lastExternalTickUs.store(micros(), std::memory_order_release);
  _lastSource.store(source, std::memory_order_relaxed);
  _newTickAvailable.store(true, std::memory_order_release);
}

void ClockManager::setInternalBPM(uint16_t bpm) {
  _internalBPM = bpm;
}

void ClockManager::update() {
  uint32_t nowUs = micros();

  // --- Process new external tick (if any) ---
  if (_newTickAvailable.load(std::memory_order_acquire)) {
    _newTickAvailable.store(false, std::memory_order_relaxed);

    uint32_t tickUs = _lastExternalTickUs.load(std::memory_order_acquire);
    uint8_t  source = _lastSource.load(std::memory_order_relaxed);

    // Update source priority: USB > BLE
    ClockSource prevSource = _activeSource;
    if (source == 0) {
      _activeSource = SRC_USB;
    } else if (_activeSource != SRC_USB) {
      _activeSource = SRC_BLE;
    }
    #if DEBUG_SERIAL
    if (_activeSource != prevSource) {
      Serial.printf("[CLOCK] Source: %s\n", _activeSource == SRC_USB ? "USB" : "BLE");
    }
    #endif

    // Calculate interval from previous tick
    if (_prevTickUs > 0) {
      uint32_t interval = tickUs - _prevTickUs;
      // Sanity check: ignore intervals < 2ms (>1250 BPM) or > 2s (< 1.25 BPM)
      if (interval > 2000 && interval < 2000000) {
        updatePLL(interval, source);
      }
    }
    _prevTickUs = tickUs;

    _rawTickCount++;
  }

  // --- Timeout: external clock lost ---
  if (_activeSource == SRC_USB || _activeSource == SRC_BLE) {
    uint32_t lastTick = _lastExternalTickUs.load(std::memory_order_relaxed);
    if (lastTick > 0 && (nowUs - lastTick) > EXTERNAL_TIMEOUT_US) {
      if (_lastKnownBPM > 0.0f) {
        _activeSource = SRC_LAST_KNOWN;
        _pllBPM = _lastKnownBPM;
        _pllTickInterval = 60000000.0f / (_pllBPM * 24.0f);
        #if DEBUG_SERIAL
        Serial.printf("[CLOCK] Source: last known (%.0f BPM)\n", _pllBPM);
        #endif
      } else {
        _activeSource = SRC_INTERNAL;
        #if DEBUG_SERIAL
        Serial.println("[CLOCK] Source: internal (no external BPM)");
        #endif
      }
      // Reset PLL buffer for next external sync
      _tickIntervalCount = 0;
      _tickIntervalIdx = 0;
      _prevTickUs = 0;
    }
  }

  // --- Update tick interval for internal/last_known sources ---
  if (_activeSource == SRC_INTERNAL) {
    _pllBPM = (float)_internalBPM;
    _pllTickInterval = 60000000.0f / (_pllBPM * 24.0f);
  }
  // SRC_LAST_KNOWN: _pllTickInterval already set at timeout

  // --- Generate ticks at _pllTickInterval ---
  if (_pllTickInterval > 0.0f) {
    uint32_t interval = (uint32_t)_pllTickInterval;
    if (interval > 0 && (nowUs - _lastTickTimeUs) >= interval) {
      _currentTick++;
      _lastTickTimeUs += interval;  // Accumulate to avoid drift
      // Guard against large gaps (e.g. after timeout) — don't burst-fire ticks
      if ((nowUs - _lastTickTimeUs) > interval * 2) {
        _lastTickTimeUs = nowUs;
      }
      // Master: send clock tick to USB+BLE
      if (_masterMode && _transport) {
        _transport->sendClockTick();
      }
    }
  }
}

void ClockManager::updatePLL(uint32_t intervalUs, uint8_t source) {
  // Store in circular buffer
  _tickIntervals[_tickIntervalIdx] = intervalUs;
  _tickIntervalIdx = (_tickIntervalIdx + 1) % PLL_BUFFER_SIZE;
  if (_tickIntervalCount < PLL_BUFFER_SIZE) _tickIntervalCount++;

  // Calculate average interval over available samples
  uint32_t sum = 0;
  for (uint8_t i = 0; i < _tickIntervalCount; i++) {
    sum += _tickIntervals[i];
  }
  float avgInterval = (float)sum / _tickIntervalCount;

  // Convert to BPM: 24 ticks per quarter note
  float newBPM = 60000000.0f / (avgInterval * 24.0f);

  // IIR filter — different alpha for USB vs BLE
  float alpha = (source == 0) ? PLL_ALPHA_USB : PLL_ALPHA_BLE;

  if (_tickIntervalCount < 3) {
    // Not enough samples yet — use raw value
    _pllBPM = newBPM;
  } else {
    _pllBPM = _pllBPM * (1.0f - alpha) + newBPM * alpha;
  }

  _pllTickInterval = 60000000.0f / (_pllBPM * 24.0f);

  // Continuously update fallback value
  _lastKnownBPM = _pllBPM;
}

void ClockManager::setMasterMode(bool master) {
  _masterMode = master;
}

uint32_t ClockManager::getCurrentTick() const  { return _currentTick; }
uint16_t ClockManager::getSmoothedBPM() const  { return (uint16_t)_pllBPM; }
bool     ClockManager::isExternalSync() const   { return _activeSource != SRC_INTERNAL; }
