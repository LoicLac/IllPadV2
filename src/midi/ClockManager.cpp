#include "ClockManager.h"
#include "../core/HardwareConfig.h"
#include "../core/MidiTransport.h"
#include <Arduino.h>

ClockManager::ClockManager()
  : _lastExternalTickUs(0), _lastUsbTickUs(0), _lastSource(0), _pendingTickCount(0),
    _tickIntervalCount(0), _prevTickUs(0),
    _pllBPM(120.0f), _pllTickInterval(0.0f), _lastKnownBPM(0.0f),
    _activeSource(SRC_INTERNAL),
    _masterMode(false), _transport(nullptr),
    _internalBPM(120), _currentTick(0), _lastTickTimeUs(0),
    _rawTickCount(0)
{
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
  uint32_t now = micros();
  _lastExternalTickUs.store(now, std::memory_order_release);
  _lastSource.store(source, std::memory_order_relaxed);
  if (source == 0) {
    _lastUsbTickUs.store(now, std::memory_order_relaxed);  // USB-only timestamp
  }
  _pendingTickCount.fetch_add(1, std::memory_order_release);
}

void ClockManager::setInternalBPM(uint16_t bpm) {
  _internalBPM = bpm;
}

void ClockManager::update() {
  uint32_t nowUs = micros();

  // --- Process pending external ticks (atomic count — handles BLE bursts) ---
  uint8_t pendingTicks = _pendingTickCount.exchange(0, std::memory_order_acquire);
  if (pendingTicks > 0) {
    uint32_t tickUs = _lastExternalTickUs.load(std::memory_order_acquire);
    uint8_t  source = _lastSource.load(std::memory_order_relaxed);

    // Update source priority: USB > BLE
    ClockSource prevSource = _activeSource;
    if (source == 0) {
      _activeSource = SRC_USB;
    } else if (_activeSource != SRC_USB) {
      _activeSource = SRC_BLE;
    }

    // Filter: reject BLE ticks when USB is the active source
    // Prevents interleaved intervals from corrupting the PLL
    if (source == 1 && _activeSource == SRC_USB) {
      // BLE tick ignored — USB has priority
    } else {
      // Reset _prevTickUs on source change to avoid one garbage interval
      if (_activeSource != prevSource) {
        _prevTickUs = 0;
        _tickIntervalCount = 0;
        #if DEBUG_SERIAL
        Serial.printf("[CLOCK] Source: %s\n", _activeSource == SRC_USB ? "USB" : "BLE");
        #endif
      }

      // Calculate interval from previous tick.
      // Divide by pendingTicks to recover per-tick interval when a burst arrived.
      if (_prevTickUs > 0) {
        uint32_t totalInterval = tickUs - _prevTickUs;
        uint32_t interval = totalInterval / pendingTicks;
        // Sanity check: ignore intervals < 2ms (>1250 BPM) or > 2s (< 1.25 BPM)
        if (interval > 2000 && interval < 2000000) {
          updatePLL(interval, source);
        }
      }
      _prevTickUs = tickUs;
    }

    _rawTickCount += pendingTicks;
  }

  // --- Timeout: external clock lost ---
  // Each source checks its own timestamp to enforce USB > BLE priority on dropout.
  if (_activeSource == SRC_USB || _activeSource == SRC_BLE) {
    uint32_t lastActiveTick = (_activeSource == SRC_USB)
        ? _lastUsbTickUs.load(std::memory_order_relaxed)     // USB: isolated timestamp
        : _lastExternalTickUs.load(std::memory_order_relaxed); // BLE: only source here

    if (lastActiveTick > 0 && (nowUs - lastActiveTick) > EXTERNAL_TIMEOUT_US) {
      bool switchedToBle = false;
      if (_activeSource == SRC_USB) {
        // USB timed out — check if BLE is still alive
        uint32_t lastAny = _lastExternalTickUs.load(std::memory_order_relaxed);
        if (_lastSource.load(std::memory_order_relaxed) == 1
            && lastAny > 0 && (nowUs - lastAny) < EXTERNAL_TIMEOUT_US) {
          _activeSource = SRC_BLE;
          _prevTickUs = 0;
          _tickIntervalCount = 0;
          switchedToBle = true;
          #if DEBUG_SERIAL
          Serial.println("[CLOCK] Source: BLE (USB timed out)");
          #endif
        }
      }
      if (!switchedToBle) {
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
        _tickIntervalCount = 0;
        _prevTickUs = 0;
      }
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
  // Single-stage IIR low-pass — no moving average buffer.
  // USB (α=0.3): converges to ±1 BPM in ~200ms. Low jitter, fast response.
  // BLE (α=0.15): converges to ±1 BPM in ~500ms. Smooths ±15ms jitter to ±0.5 BPM.
  float newBPM = 60000000.0f / ((float)intervalUs * 24.0f);
  float alpha = (source == 0) ? PLL_ALPHA_USB : PLL_ALPHA_BLE;

  if (_tickIntervalCount < 3) _tickIntervalCount++;
  if (_tickIntervalCount < 3) {
    _pllBPM = newBPM;  // First samples: seed with raw value
  } else {
    _pllBPM = _pllBPM * (1.0f - alpha) + newBPM * alpha;
  }

  _pllTickInterval = 60000000.0f / (_pllBPM * 24.0f);
  _lastKnownBPM = _pllBPM;
}

void ClockManager::setMasterMode(bool master) {
  _masterMode = master;
}

uint32_t ClockManager::getCurrentTick() const  { return _currentTick; }
uint16_t ClockManager::getSmoothedBPM() const  { return (uint16_t)_pllBPM; }
bool     ClockManager::isExternalSync() const   { return _activeSource != SRC_INTERNAL; }
