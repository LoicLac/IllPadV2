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
  void sendPitchBend(uint8_t channel, uint16_t value);  // 0-16383, center=8192

  // BLE connection interval (applied on next BLE connect)
  void setBleInterval(uint8_t interval);  // BleInterval enum (0-2)

  // Connection state (for LED feedback)
  bool isUsbConnected() const;
  bool isBleConnected() const;

private:
  bool _bleConnected;
};

#endif // MIDI_TRANSPORT_H
