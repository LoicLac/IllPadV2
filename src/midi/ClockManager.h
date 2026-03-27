#ifndef CLOCK_MANAGER_H
#define CLOCK_MANAGER_H

#include <stdint.h>
#include <atomic>

class MidiTransport;  // forward declaration

class ClockManager {
public:
  ClockManager();

  void begin(MidiTransport* transport = nullptr);
  void update();  // Called every loop iteration

  // Callback from MidiTransport (BLE calls from NimBLE task!)
  void onMidiClockTick(uint8_t source);  // 0xF8, source: 0=USB, 1=BLE

  // Internal tempo source (pot right 1)
  void setInternalBPM(uint16_t bpm);

  // Config (applied at boot from Tool 5 settings)
  void setMasterMode(bool master);

  // Output — smoothed clock
  uint32_t getCurrentTick() const;
  uint16_t getSmoothedBPM() const;
  bool     isExternalSync() const;

private:
  enum ClockSource : uint8_t {
    SRC_INTERNAL,
    SRC_BLE,
    SRC_USB,
    SRC_LAST_KNOWN
  };

  static constexpr uint32_t EXTERNAL_TIMEOUT_US      = 2000000; // 2s in microseconds
  static constexpr uint32_t LAST_KNOWN_TIMEOUT_US    = 5000000; // 5s before falling to internal
  static constexpr float    PLL_ALPHA_USB            = 0.3f;
  static constexpr float    PLL_ALPHA_BLE            = 0.15f;

  // Raw tick reception (written by BLE callback on NimBLE task)
  // Per-source counters prevent USB/BLE tick contamination in the PLL.
  std::atomic<uint32_t> _lastUsbTickUs;        // micros() of last USB tick
  std::atomic<uint32_t> _lastBleTickUs;        // micros() of last BLE tick
  std::atomic<uint8_t>  _pendingUsbTicks;      // USB ticks since last update()
  std::atomic<uint8_t>  _pendingBleTicks;      // BLE ticks since last update()

  // PLL state (only accessed from Core 1 update())
  uint8_t  _tickIntervalCount; // counts samples for seed guard (first 3 = raw)
  uint32_t _prevTickUs;        // previous tick timestamp (for interval calc)

  // PLL state
  float    _pllBPM;
  float    _pllTickInterval;   // smoothed interval between ticks (µs)
  float    _lastKnownBPM;      // continuously updated for seamless fallback

  // Source priority
  ClockSource _activeSource;
  uint32_t    _lastKnownEntryUs;  // timestamp when SRC_LAST_KNOWN was entered

  // Config
  bool           _masterMode;
  MidiTransport* _transport;

  // Internal tempo
  uint16_t _internalBPM;

  // Tick generation
  uint32_t _currentTick;
  uint32_t _lastTickTimeUs;    // timestamp of last generated tick

  // External tick count (processed on Core 1)
  uint32_t _rawTickCount;

  void updatePLL(uint32_t intervalUs, uint8_t source);
};

#endif // CLOCK_MANAGER_H
