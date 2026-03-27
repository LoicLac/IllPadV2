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

  static constexpr uint32_t EXTERNAL_TIMEOUT_US = 2000000; // 2s in microseconds
  static constexpr float    PLL_ALPHA_USB      = 0.3f;
  static constexpr float    PLL_ALPHA_BLE      = 0.15f;

  // Raw tick reception (written by BLE callback on NimBLE task)
  std::atomic<uint32_t> _lastExternalTickUs;   // micros() of last tick (any source)
  std::atomic<uint32_t> _lastUsbTickUs;        // micros() of last USB tick only
  std::atomic<uint8_t>  _lastSource;           // source of last tick (0=USB, 1=BLE)
  std::atomic<uint8_t>  _pendingTickCount;     // ticks received since last update()

  // PLL state (only accessed from Core 1 update())
  uint8_t  _tickIntervalCount; // counts samples for seed guard (first 3 = raw)
  uint32_t _prevTickUs;        // previous tick timestamp (for interval calc)

  // PLL state
  float    _pllBPM;
  float    _pllTickInterval;   // smoothed interval between ticks (µs)
  float    _lastKnownBPM;      // continuously updated for seamless fallback

  // Source priority
  ClockSource _activeSource;

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
