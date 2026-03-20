#include "MidiTransport.h"
#include "HardwareConfig.h"
#include <Arduino.h>
#include <atomic>

// --- USB MIDI (ESP32-S3 built-in TinyUSB) ---
#include "USB.h"
#include "USBMIDI.h"
#include "tusb.h"  // tud_mounted() for USB connection detection

// --- BLE MIDI (max22/ESP32-BLE-MIDI) ---
#include <BLEMidi.h>
#include <NimBLEDevice.h>

// USB MIDI global instance (built-in framework API)
// Uses compile-time USB device name from HardwareConfig.h
static USBMIDI usbMidi(DEVICE_NAME_USB);

// Track BLE connection state via callbacks
static volatile bool s_bleConnected = false;
static std::atomic<uint8_t> s_bleIntervalSetting{BLE_NORMAL};

static void onBleConnected() {
  s_bleConnected = true;

  // Apply BLE connection interval via NimBLE.
  // getPeerDevices() returns std::vector because the ESP32-BLE-MIDI callback
  // signature (void(*)()) strips the NimBLEServer* parameter, so we can't
  // access the connection handle directly from the callback arguments.
  NimBLEServer* pServer = NimBLEDevice::getServer();
  if (pServer) {
    std::vector<uint16_t> peers = pServer->getPeerDevices();
    if (!peers.empty()) {
      uint16_t handle = peers.back();
      uint16_t minI, maxI;
      switch (s_bleIntervalSetting.load(std::memory_order_relaxed)) {
        case BLE_LOW_LATENCY:   minI = 6;  maxI = 6;  break;  // 7.5ms
        case BLE_BATTERY_SAVER: minI = 24; maxI = 24; break;  // 30ms
        default:                minI = 12; maxI = 12; break;  // 15ms
      }
      pServer->updateConnParams(handle, minI, maxI, 0, 200);
      #if DEBUG_SERIAL
      Serial.printf("[MIDI] BLE conn params: interval=%d (%.1fms)\n", minI, minI * 1.25f);
      #endif
    }
  }

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
// BLE Connection Interval
// =================================================================

void MidiTransport::setBleInterval(uint8_t interval) {
  s_bleIntervalSetting.store(interval, std::memory_order_relaxed);
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

void MidiTransport::sendPitchBend(uint8_t channel, uint16_t value) {
  // USB MIDI — pitchBend takes signed value -8192..+8191 (USB MIDI lib convention)
  if (tud_mounted()) {
    int16_t signedVal = (int16_t)value - 8192;
    usbMidi.pitchBend(signedVal, channel + 1);
  }

  // BLE MIDI — pitchBend takes 0-16383
  if (BLEMidiServer.isConnected()) {
    BLEMidiServer.pitchBend(channel, value);
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
