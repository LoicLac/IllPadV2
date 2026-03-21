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

  // Callbacks from MidiTransport (BLE calls from NimBLE task!)
  void onMidiClockTick(uint8_t source);  // 0xF8, source: 0=USB, 1=BLE
  void onMidiStart();                     // 0xFA
  void onMidiContinue();                  // 0xFB
  void onMidiStop();                      // 0xFC

  // Internal tempo source (pot right 1)
  void setInternalBPM(uint16_t bpm);

  // Config (applied at boot from Tool 5 settings)
  void setMasterMode(bool master);
  void setFollowTransport(bool follow);

  // Transport flags — consumed once per loop by main.cpp
  bool consumeStartReceived();    // true once after 0xFA (reset tick counter)
  bool consumeStopReceived();     // true once after 0xFC (flush all arps)

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

  static constexpr uint8_t  PLL_BUFFER_SIZE    = 24;   // 24 ticks = 1 quarter note
  static constexpr uint32_t EXTERNAL_TIMEOUT_US = 2000000; // 2s in microseconds
  static constexpr float    PLL_ALPHA_USB      = 0.3f;
  static constexpr float    PLL_ALPHA_BLE      = 0.1f;

  // Raw tick reception (written by BLE callback on NimBLE task)
  std::atomic<uint32_t> _lastExternalTickUs;   // micros() of last external tick
  std::atomic<uint8_t>  _lastSource;           // source of last tick (0=USB, 1=BLE)
  std::atomic<bool>     _newTickAvailable;     // flag: new tick arrived
  std::atomic<bool>     _startReceived;        // flag: MIDI Start received
  std::atomic<bool>     _stopReceived;         // flag: MIDI Stop received

  // PLL circular buffer (only accessed from Core 1 update())
  uint32_t _tickIntervals[PLL_BUFFER_SIZE];
  uint8_t  _tickIntervalIdx;
  uint8_t  _tickIntervalCount;
  uint32_t _prevTickUs;        // previous tick timestamp (for interval calc)

  // PLL state
  float    _pllBPM;
  float    _pllTickInterval;   // smoothed interval between ticks (µs)
  float    _lastKnownBPM;      // continuously updated for seamless fallback

  // Source priority
  ClockSource _activeSource;

  // Config
  bool           _masterMode;
  bool           _followTransport;
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
