#include "MidiTransport.h"
#include "HardwareConfig.h"
#include <Arduino.h>

// --- USB MIDI (ESP32-S3 built-in TinyUSB) ---
#include "USB.h"
#include "USBMIDI.h"
#include "tusb.h"  // tud_mounted() for USB connection detection

// --- BLE MIDI (max22/ESP32-BLE-MIDI) ---
#include <BLEMidi.h>

// USB MIDI global instance (built-in framework API)
// Uses compile-time USB device name from HardwareConfig.h
static USBMIDI usbMidi(DEVICE_NAME_USB);

// Track BLE connection state via callbacks
static volatile bool s_bleConnected = false;

static void onBleConnected() {
  s_bleConnected = true;
  #if DEBUG_SERIAL
  Serial.println("[MIDI] BLE client connected.");
  #endif
}

static void onBleDisconnected() {
  s_bleConnected = false;
  #if DEBUG_SERIAL
  Serial.println("[MIDI] BLE client disconnected.");
  #endif
}

// =================================================================
// Constructor
// =================================================================

MidiTransport::MidiTransport()
  : _bleConnected(false)
{
}

// =================================================================
// Initialization
// =================================================================

void MidiTransport::begin() {
  // --- USB MIDI Init (ESP32-S3 built-in) ---
  usbMidi.begin();
  // Set USB composite device descriptor strings (what the host shows)
  USB.manufacturerName("ILLPAD");
  USB.productName(DEVICE_NAME_USB);
  USB.begin();

  #if DEBUG_SERIAL
  Serial.println("[MIDI] USB MIDI initialized.");
  #endif

  // --- BLE MIDI Init ---
  // Uses compile-time BLE device name from HardwareConfig.h
  BLEMidiServer.begin(DEVICE_NAME_BLE);
  BLEMidiServer.setOnConnectCallback(onBleConnected);
  BLEMidiServer.setOnDisconnectCallback(onBleDisconnected);

  #if DEBUG_SERIAL
  Serial.println("[MIDI] BLE MIDI initialized.");
  #endif
}

// =================================================================
// Loop Housekeeping
// =================================================================

void MidiTransport::update() {
  _bleConnected = s_bleConnected;
}

// =================================================================
// Dual-Output MIDI Sends
// =================================================================

void MidiTransport::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  // USB MIDI — only send if host is connected
  if (tud_mounted()) {
    if (velocity > 0) {
      usbMidi.noteOn(note, velocity, channel + 1);
    } else {
      usbMidi.noteOff(note, 0, channel + 1);
    }
  }

  // BLE MIDI (0-indexed channel)
  if (BLEMidiServer.isConnected()) {
    if (velocity > 0) {
      BLEMidiServer.noteOn(channel, note, velocity);
    } else {
      BLEMidiServer.noteOff(channel, note, 0);
    }
  }
}

void MidiTransport::sendPolyAftertouch(uint8_t channel, uint8_t note, uint8_t pressure) {
  // USB MIDI — only send if host is connected
  if (tud_mounted()) {
    usbMidi.polyPressure(note, pressure, channel + 1);
  }

  // BLE MIDI
  if (BLEMidiServer.isConnected()) {
    BLEMidiServer.afterTouchPoly(channel, note, pressure);
  }
}

// =================================================================
// Connection State
// =================================================================

bool MidiTransport::isUsbConnected() const {
  return tud_mounted();
}

bool MidiTransport::isBleConnected() const {
  return _bleConnected;
}
