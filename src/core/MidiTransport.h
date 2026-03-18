#ifndef MIDI_TRANSPORT_H
#define MIDI_TRANSPORT_H

#include <stdint.h>

class MidiTransport {
public:
  MidiTransport();
  void begin();
  void update();  // Call from loop() for BLE housekeeping

  // Dual-output MIDI send (USB + BLE simultaneously)
  void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
  void sendPolyAftertouch(uint8_t channel, uint8_t note, uint8_t pressure);

  // Connection state (for LED feedback)
  bool isUsbConnected() const;
  bool isBleConnected() const;

private:
  bool _bleConnected;
};

#endif // MIDI_TRANSPORT_H
