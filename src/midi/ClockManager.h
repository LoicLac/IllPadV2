#ifndef CLOCK_MANAGER_H
#define CLOCK_MANAGER_H

#include <stdint.h>

class ClockManager {
public:
  ClockManager();

  void begin();
  void update();  // Called every loop iteration

  // Callbacks from MidiTransport
  void onMidiClockTick();   // 0xF8
  void onMidiStart();       // 0xFA
  void onMidiStop();        // 0xFC

  // Internal tempo source (pot rear)
  void setInternalBPM(uint16_t bpm);

  // Output — smoothed clock
  uint32_t getCurrentTick() const;
  uint16_t getSmoothedBPM() const;
  bool     isExternalSync() const;

private:
  enum ClockSource : uint8_t {
    SRC_USB,
    SRC_BLE,
    SRC_LAST_KNOWN,
    SRC_INTERNAL
  };

  // Raw tick reception
  volatile uint32_t _rawTickCount;
  volatile uint32_t _lastRawTickTime;

  // PLL
  float    _pllBPM;
  float    _pllPhase;
  float    _pllTickInterval;
  uint32_t _lastPllUpdate;

  // Source priority
  ClockSource _activeSource;
  uint32_t    _externalTimeoutMs;

  // Internal
  uint16_t _internalBPM;

  // Smoothed output
  uint32_t _currentTick;

  void updatePLL();
  void generateInternalTicks();
};

#endif // CLOCK_MANAGER_H
