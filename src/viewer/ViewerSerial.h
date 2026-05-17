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

// Phase 1.A : pas d'emit_xxx() encore. Ajoutés au fur et à mesure des
// sous-phases 1.C.*, 1.D, 1.E, 1.F.

}  // namespace viewer
