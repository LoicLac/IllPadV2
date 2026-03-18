#ifndef ARP_SCHEDULER_H
#define ARP_SCHEDULER_H

#include <stdint.h>
#include "../core/KeyboardData.h"

class ArpEngine;
class ClockManager;
class MidiTransport;

class ArpScheduler {
public:
  ArpScheduler();

  void begin(MidiTransport* transport);

  // Register/unregister arp engines (max 4)
  void registerArp(uint8_t bankIndex, ArpEngine* engine);
  void unregisterArp(uint8_t bankIndex);

  // Called every loop — dispatches ticks to active arps
  void tick(ClockManager& clock);

private:
  MidiTransport* _transport;

  struct ArpSlot {
    ArpEngine* engine;
    bool       active;
  };
  ArpSlot _slots[MAX_ARP_BANKS];

  uint32_t _lastTickTime;
};

#endif // ARP_SCHEDULER_H
