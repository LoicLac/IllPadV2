#include "ArpScheduler.h"
#include "ArpEngine.h"
#include "../midi/ClockManager.h"
#include "../core/MidiTransport.h"

// =================================================================
// Ticks per step for each ArpDivision (24 ppqn MIDI clock)
// =================================================================

static const uint16_t TICKS_PER_STEP[NUM_ARP_DIVISIONS] = {
  384,  // DIV_4_1   — quadruple whole
  192,  // DIV_2_1   — double whole
  96,   // DIV_1_1   — whole note
  48,   // DIV_1_2   — half note
  24,   // DIV_1_4   — quarter note
  12,   // DIV_1_8   — eighth note
  6,    // DIV_1_16  — sixteenth note
  3,    // DIV_1_32  — thirty-second note
  2     // DIV_1_64  — sixty-fourth note (minimum 2 ticks)
};

// =================================================================
// Constructor
// =================================================================

ArpScheduler::ArpScheduler()
  : _transport(nullptr), _clock(nullptr), _slotCount(0), _lastClockTick(0) {
  for (uint8_t i = 0; i < MAX_ARP_BANKS; i++) {
    _slots[i].engine    = nullptr;
    _slots[i].bankIndex = 0xFF;
    _slots[i].tickAccum = 0;
    _slots[i].active    = false;
  }
}

// =================================================================
// begin — store transport and clock references
// =================================================================

void ArpScheduler::begin(MidiTransport* transport, ClockManager* clock) {
  _transport = transport;
  _clock     = clock;
}

// =================================================================
// Register/unregister
// =================================================================

void ArpScheduler::registerArp(uint8_t bankIndex, ArpEngine* engine) {
  if (_slotCount >= MAX_ARP_BANKS) return;
  // Avoid duplicate registration
  for (uint8_t i = 0; i < _slotCount; i++) {
    if (_slots[i].bankIndex == bankIndex) {
      _slots[i].engine = engine;
      _slots[i].active = true;
      return;
    }
  }
  _slots[_slotCount].engine    = engine;
  _slots[_slotCount].bankIndex = bankIndex;
  _slots[_slotCount].tickAccum = 0;
  _slots[_slotCount].active    = true;
  _slotCount++;
}

void ArpScheduler::unregisterArp(uint8_t bankIndex) {
  for (uint8_t i = 0; i < _slotCount; i++) {
    if (_slots[i].bankIndex == bankIndex) {
      // Shift remaining slots down
      for (uint8_t j = i; j + 1 < _slotCount; j++) {
        _slots[j] = _slots[j + 1];
      }
      _slotCount--;
      _slots[_slotCount].engine    = nullptr;
      _slots[_slotCount].bankIndex = 0xFF;
      _slots[_slotCount].tickAccum = 0;
      _slots[_slotCount].active    = false;
      return;
    }
  }
}

// =================================================================
// tick — Phase 1: dispatch steps to engines
// =================================================================
// Uses a per-engine tick accumulator instead of modulus.
// When the accumulator reaches the division's tick count,
// the engine fires a step and the accumulator wraps.
//
// This handles the case where multiple ticks arrive at once
// (e.g. loop stall, BLE burst): the while loop ensures every
// step fires, even at DIV_1_64 (2 ticks per step).
//
// stepDurationUs is computed from BPM and division:
//   60,000,000 / (bpm × 24) × ticks_per_step
//   = 2,500,000 × ticks_per_step / bpm

void ArpScheduler::tick() {
  if (!_transport || !_clock) return;

  uint32_t currentTick = _clock->getCurrentTick();
  if (currentTick == _lastClockTick) return;  // No new ticks

  uint32_t ticksElapsed = currentTick - _lastClockTick;
  // Guard against burst-firing too many steps on clock source transitions
  if (ticksElapsed > 24) ticksElapsed = 24;  // Max 1 quarter note of catch-up (fits event queue at all divisions)
  _lastClockTick = currentTick;

  for (uint8_t i = 0; i < _slotCount; i++) {
    if (!_slots[i].active || !_slots[i].engine) continue;

    ArpEngine* eng = _slots[i].engine;
    uint16_t divisor = TICKS_PER_STEP[eng->getDivision()];

    _slots[i].tickAccum += ticksElapsed;

    // Fire steps for every accumulated divisor (handles multi-tick bursts)
    while (_slots[i].tickAccum >= divisor) {
      // Calculate real-time step duration from current BPM
      uint16_t bpm = _clock->getSmoothedBPM();
      if (bpm == 0) bpm = 120;  // Safety fallback
      uint32_t stepDurationUs = (uint32_t)2500000UL * divisor / bpm;

      // Synthesize a tick value that advances with each burst step
      uint32_t synthTick = currentTick - _slots[i].tickAccum;
      eng->tick(*_transport, stepDurationUs, synthTick);

      _slots[i].tickAccum -= divisor;
    }
  }
}

// =================================================================
// processEvents — Phase 2: fire pending events for all engines
// =================================================================
// Called every loop iteration after tick(). This is where the
// actual MIDI messages for gate noteOff and shuffled noteOn
// get sent, based on their scheduled timestamps.

void ArpScheduler::processEvents() {
  if (!_transport) return;
  for (uint8_t i = 0; i < _slotCount; i++) {
    if (!_slots[i].active || !_slots[i].engine) continue;
    _slots[i].engine->processEvents(*_transport);
  }
}

