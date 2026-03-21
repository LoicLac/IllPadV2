#ifndef MIDI_TRANSPORT_H
#define MIDI_TRANSPORT_H

#include <stdint.h>

// Clock callback types
typedef void (*MidiClockCallback)(uint8_t source);       // source: 0=USB, 1=BLE
typedef void (*MidiTransportCallback)(uint8_t status, uint8_t source);  // status: 0xFA/0xFB/0xFC

class MidiTransport {
public:
  MidiTransport();
  void begin();
  void update();  // Call from loop() — BLE housekeeping + USB clock polling

  // Dual-output MIDI send (USB + BLE simultaneously)
  void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
  void sendPolyAftertouch(uint8_t channel, uint8_t note, uint8_t pressure);
  void sendPitchBend(uint8_t channel, uint16_t value);  // 0-16383, center=8192

  // BLE connection interval (applied on next BLE connect)
  void setBleInterval(uint8_t interval);  // BleInterval enum (0-2)

  // MIDI clock reception callbacks
  void setClockCallback(MidiClockCallback cb);
  void setTransportCallback(MidiTransportCallback cb);

  // CC 123 All Notes Off (panic) — brute force silence on a single channel
  void sendAllNotesOff(uint8_t channel);

  // Clock output (Master mode — ticks only, never sends Start/Stop)
  void sendClockTick();   // 0xF8

  // Connection state (for LED feedback)
  bool isUsbConnected() const;
  bool isBleConnected() const;

private:
  // No members — BLE state read directly from static atomic
};

#endif // MIDI_TRANSPORT_H
