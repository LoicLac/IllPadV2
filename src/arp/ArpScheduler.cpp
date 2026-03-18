#include "ArpScheduler.h"
#include "ArpEngine.h"
#include "../midi/ClockManager.h"
#include "../core/MidiTransport.h"

ArpScheduler::ArpScheduler() : _transport(nullptr), _lastTickTime(0) {
  for (uint8_t i = 0; i < MAX_ARP_BANKS; i++) {
    _slots[i].engine = nullptr;
    _slots[i].active = false;
  }
}

void ArpScheduler::begin(MidiTransport* transport) {
  _transport = transport;
}

void ArpScheduler::registerArp(uint8_t bankIndex, ArpEngine* engine) {
  if (bankIndex < MAX_ARP_BANKS) {
    _slots[bankIndex].engine = engine;
    _slots[bankIndex].active = true;
  }
}

void ArpScheduler::unregisterArp(uint8_t bankIndex) {
  if (bankIndex < MAX_ARP_BANKS) {
    _slots[bankIndex].engine = nullptr;
    _slots[bankIndex].active = false;
  }
}

void ArpScheduler::tick(ClockManager& clock) {
  (void)clock;
  // TODO: check clock tick, dispatch to all active arp engines
}
