#ifndef ARP_SCHEDULER_H
#define ARP_SCHEDULER_H

#include <stdint.h>
#include "../core/KeyboardData.h"

class ArpEngine;
class ClockManager;
class MidiTransport;

// =================================================================
// ArpScheduler — dispatches clock ticks to up to 4 ArpEngines
// =================================================================
// Two-phase per loop iteration:
//   1. tick()          — check clock, fire engine steps at division boundaries
//   2. processEvents() — fire any pending noteOn/noteOff events (gate + shuffle)
//
// Uses a per-engine tick accumulator instead of modulus-based boundary
// detection. This guarantees no missed steps even at fast divisions
// (DIV_1_64 = 2 ticks per step) when the loop stalls briefly.

class ArpScheduler {
public:
  ArpScheduler();

  void begin(MidiTransport* transport, ClockManager* clock);

  // Register/unregister arp engines (max 4)
  void registerArp(uint8_t bankIndex, ArpEngine* engine);
  void unregisterArp(uint8_t bankIndex);

  // Phase 1: check clock, dispatch steps to engines
  void tick();

  // Phase 2: fire pending events for all engines (gate noteOff + shuffle noteOn)
  void processEvents();


private:
  MidiTransport* _transport;
  ClockManager*  _clock;

  struct ArpSlot {
    ArpEngine* engine;
    uint8_t    bankIndex;
    uint32_t   tickAccum;   // Accumulated ticks since last step fired
    bool       active;
  };
  ArpSlot  _slots[MAX_ARP_BANKS];
  uint8_t  _slotCount;

  uint32_t _lastClockTick;  // Last processed ClockManager tick
};

#endif // ARP_SCHEDULER_H
