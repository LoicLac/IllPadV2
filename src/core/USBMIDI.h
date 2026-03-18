#ifndef USBMIDI_SHIM_H
#define USBMIDI_SHIM_H

// Shim for USBMIDI class — the real implementation requires Arduino ESP32 Core 3.0+
// This stub allows the project to compile on Arduino ESP32 Core 2.x (platformio espressif32 6.x)
// TODO: Remove this file when upgrading to espressif32 platform with Core 3.0+

#include <stdint.h>

class USBMIDI {
public:
  USBMIDI(const char* name = "") { (void)name; }

  void begin() {}
  void noteOn(uint8_t note, uint8_t velocity, uint8_t channel) {
    (void)note; (void)velocity; (void)channel;
  }
  void noteOff(uint8_t note, uint8_t velocity, uint8_t channel) {
    (void)note; (void)velocity; (void)channel;
  }
  void polyPressure(uint8_t note, uint8_t pressure, uint8_t channel) {
    (void)note; (void)pressure; (void)channel;
  }
};

#endif // USBMIDI_SHIM_H
