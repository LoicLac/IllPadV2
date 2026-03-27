#include "ClockManager.h"
#include "../core/HardwareConfig.h"
#include "../core/MidiTransport.h"
#include <Arduino.h>

ClockManager::ClockManager()
  : _lastUsbTickUs(0), _lastBleTickUs(0), _pendingUsbTicks(0), _pendingBleTicks(0),
    _tickIntervalCount(0), _prevTickUs(0),
    _pllBPM(120.0f), _pllTickInterval(0.0f), _lastKnownBPM(0.0f),
    _activeSource(SRC_INTERNAL), _lastKnownEntryUs(0),
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
  if (source == 0) {
    _lastUsbTickUs.store(now, std::memory_order_release);
    _pendingUsbTicks.fetch_add(1, std::memory_order_release);
  } else {
    _lastBleTickUs.store(now, std::memory_order_release);
    _pendingBleTicks.fetch_add(1, std::memory_order_release);
  }
}

void ClockManager::setInternalBPM(uint16_t bpm) {
  _internalBPM = bpm;
}

void ClockManager::update() {
  uint32_t nowUs = micros();

  // --- Process pending external ticks (per-source counters — no cross-contamination) ---
  uint8_t usbTicks = _pendingUsbTicks.exchange(0, std::memory_order_acquire);
  uint8_t bleTicks = _pendingBleTicks.exchange(0, std::memory_order_acquire);

  if (usbTicks > 0 || bleTicks > 0) {
    // Update source priority: USB > BLE
    ClockSource prevSource = _activeSource;
    if (usbTicks > 0) {
      _activeSource = SRC_USB;
    } else if (_activeSource != SRC_USB) {
      _activeSource = SRC_BLE;
    }

    // Select the active source's ticks only — ignore the other source entirely
    uint8_t  activeTicks = 0;
    uint32_t activeTickUs = 0;
    uint8_t  activeSourceId = 0;

    if (_activeSource == SRC_USB && usbTicks > 0) {
      activeTicks = usbTicks;
      activeTickUs = _lastUsbTickUs.load(std::memory_order_acquire);
      activeSourceId = 0;
    } else if (_activeSource == SRC_BLE && bleTicks > 0) {
      activeTicks = bleTicks;
      activeTickUs = _lastBleTickUs.load(std::memory_order_acquire);
      activeSourceId = 1;
    }

    if (activeTicks > 0) {
      // Reset _prevTickUs on source change to avoid one garbage interval
      if (_activeSource != prevSource) {
        _prevTickUs = 0;
        _tickIntervalCount = 0;
        #if DEBUG_SERIAL
        Serial.printf("[CLOCK] Source: %s\n", _activeSource == SRC_USB ? "USB" : "BLE");
        #endif
      }

      // Calculate interval from previous tick.
      // Divide by activeTicks to recover per-tick interval when a burst arrived.
      if (_prevTickUs > 0) {
        uint32_t totalInterval = activeTickUs - _prevTickUs;
        uint32_t interval = totalInterval / activeTicks;
        // Sanity check: ignore intervals < 2ms (>1250 BPM) or > 2s (< 1.25 BPM)
        if (interval > 2000 && interval < 2000000) {
          updatePLL(interval, activeSourceId);
        }
      }
      _prevTickUs = activeTickUs;
    }

    _rawTickCount += activeTicks;
  }

  // --- Timeout: external clock lost ---
  // Each source checks its own timestamp to enforce USB > BLE priority on dropout.
  if (_activeSource == SRC_USB || _activeSource == SRC_BLE) {
    uint32_t lastActiveTick = (_activeSource == SRC_USB)
        ? _lastUsbTickUs.load(std::memory_order_relaxed)
        : _lastBleTickUs.load(std::memory_order_relaxed);

    if (lastActiveTick > 0 && (nowUs - lastActiveTick) > EXTERNAL_TIMEOUT_US) {
      bool switchedToBle = false;
      if (_activeSource == SRC_USB) {
        // USB timed out — check if BLE is still alive
        uint32_t lastBle = _lastBleTickUs.load(std::memory_order_relaxed);
        if (lastBle > 0 && (nowUs - lastBle) < EXTERNAL_TIMEOUT_US) {
          _activeSource = SRC_BLE;
          _prevTickUs = 0;
          _tickIntervalCount = 0;
          switchedToBle = true;
          #if DEBUG_SERIAL
          Serial.println("[CLOCK] Source: BLE (USB timed out)");
          #endif
        }
      }
      // Clear stale timestamp of the timed-out source to prevent
      // false "alive" detection after micros() wraps (~71.6 min)
      if (!switchedToBle) {
        _lastUsbTickUs.store(0, std::memory_order_relaxed);
        _lastBleTickUs.store(0, std::memory_order_relaxed);
        if (_lastKnownBPM > 0.0f) {
          _activeSource = SRC_LAST_KNOWN;
          _lastKnownEntryUs = nowUs;
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

  // --- SRC_LAST_KNOWN timeout: fall to internal after 5s ---
  if (_activeSource == SRC_LAST_KNOWN &&
      _lastKnownEntryUs > 0 && (nowUs - _lastKnownEntryUs) > LAST_KNOWN_TIMEOUT_US) {
    _activeSource = SRC_INTERNAL;
    #if DEBUG_SERIAL
    Serial.println("[CLOCK] Source: internal (last known timed out)");
    #endif
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
    if (interval > 0) {
      // Guard against large gaps (e.g. after timeout) — reset accumulator
      if ((nowUs - _lastTickTimeUs) > interval * 4) {
        _lastTickTimeUs = nowUs;
      }
      // Catch up missed ticks (max 4 per call to avoid burst-fire)
      uint8_t ticksGenerated = 0;
      while ((nowUs - _lastTickTimeUs) >= interval && ticksGenerated < 4) {
        _currentTick++;
        _lastTickTimeUs += interval;  // Accumulate to avoid drift
        ticksGenerated++;
        // Master: send clock tick to USB+BLE
        if (_masterMode && _transport) {
          _transport->sendClockTick();
        }
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

  if (_tickIntervalCount < 4) _tickIntervalCount++;
  if (_tickIntervalCount < 4) {
    _pllBPM = newBPM;  // First 3 samples: seed with raw value
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
bool     ClockManager::isExternalSync() const   { return _activeSource == SRC_USB || _activeSource == SRC_BLE; }
