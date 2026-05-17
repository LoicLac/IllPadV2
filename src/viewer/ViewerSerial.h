// src/viewer/ViewerSerial.h
#pragma once

#include <Arduino.h>
#include <atomic>

namespace viewer {

// NOTE: PRIO_LOW / PRIO_HIGH instead of LOW / HIGH to avoid collision with
// Arduino's `#define LOW 0x0` / `#define HIGH 0x1` (esp32-hal-gpio.h). The
// macros are substituted by the preprocessor before C++ scoping kicks in,
// so even `Priority::LOW` would be broken. Renaming the enumerators is the
// standard workaround in Arduino-land.
enum Priority : uint8_t {
  PRIO_LOW  = 0,
  PRIO_HIGH = 1,
};

// Lifecycle — to be called from main.cpp setup() / loop()
void begin();          // create queue + task, call after Serial.begin in setup()
void pollCommands();   // non-blocking, call in tete de loop()

// Connection state — cheap atomic load
bool isConnected();

// --- Phase 1.C.1 : [POT] events ---
// slot : "R1", "R1H", ..., "R4H", or "--" for rear pot / global no-slot.
// target : printable target name (e.g., "Tempo", "LED_Bright", "CC74").
// valueStr : pre-formatted value ("120", "0.50", "Dorian", ...).
// unit : optional unit suffix ("BPM"), pass nullptr for none.
void emitPot(const char* slot, const char* target, const char* valueStr, const char* unit);

// --- Phase 1.C.2 : [BANK]/[STATE]/[READY] events ---
// Boot dump uses these directly. Runtime bank switch uses emitBankSwitch.
void emitBanksHeader(uint8_t count);          // [BANKS] count=N
void emitBank(uint8_t idx);                    // [BANK] idx=N type=... (reads s_banks)
void emitState(uint8_t bankIdx);               // [STATE] bank=N ... (reads s_banks + s_potRouter)
void emitReady(uint8_t currentBank1Based);     // [READY] current=N
void emitBankSwitch(uint8_t newBankIdx);       // [BANK] Bank N + [STATE] bank=N

// --- Phase 1.C.3 : [ARP]/[GEN] events ---
// kind : "+note", "-note" — for pile changes. count = pile size.
void emitArpNoteAdd(uint8_t bankIdx, uint8_t pileCount);
void emitArpNoteRemove(uint8_t bankIdx, uint8_t pileCount);
// Play/Stop variants
void emitArpPlay(uint8_t bankIdx, uint8_t pileCount, bool relaunchPaused);
void emitArpStop(uint8_t bankIdx, uint8_t pileCount);
void emitArpQueueFull();
// GEN seed (ARPEG_GEN). pileCount=1 triggers the degenerate form.
void emitGenSeed(uint16_t seqLen, uint8_t eInit, uint8_t pileCount,
                 int8_t lo, int8_t hi);
void emitGenSeedDegenerate(uint16_t seqLen, int8_t singleDegree);

// --- Phase 1.C.4 : [SCALE]/[ARP_GEN] events ---
enum ScaleEventKind : uint8_t {
  SCALE_ROOT, SCALE_MODE, SCALE_CHROMATIC,
};
void emitScale(ScaleEventKind kind, uint8_t rootIdx, uint8_t modeIdx);
void emitArpOctave(uint8_t octave);                   // [ARP] Octave N
void emitArpGenMutation(uint8_t mutationLevel);       // [ARP_GEN] MutationLevel N

// Phase 1.A : pas d'emit_xxx() encore. Ajoutés au fur et à mesure des
// sous-phases 1.C.*, 1.D, 1.E, 1.F.

}  // namespace viewer
