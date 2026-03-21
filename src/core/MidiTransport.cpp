#include "MidiTransport.h"
#include "HardwareConfig.h"
#include <Arduino.h>
#include <atomic>

// --- USB MIDI (ESP32-S3 built-in TinyUSB) ---
#include "USB.h"
#include "USBMIDI.h"
#include "tusb.h"  // tud_mounted() for USB connection detection

// --- BLE MIDI (ILLPAD fork with system RT callback) ---
#include <BLEMidi.h>
#include <NimBLEDevice.h>

// USB MIDI global instance (built-in framework API)
// Uses compile-time USB device name from HardwareConfig.h
static USBMIDI usbMidi(DEVICE_NAME_USB);

// Track BLE connection state via callbacks
static std::atomic<bool> s_bleConnected{false};
static std::atomic<uint8_t> s_bleIntervalSetting{BLE_NORMAL};

// Clock reception callbacks (set by main.cpp, called from USB poll + BLE callback)
static MidiClockCallback     s_clockCb     = nullptr;
static MidiTransportCallback s_transportCb = nullptr;

// BLE system real-time callback (called from NimBLE task)
static void onBleSystemRT(uint8_t status) {
  if (status == 0xF8 && s_clockCb)     s_clockCb(1);        // BLE clock tick
  if ((status == 0xFA || status == 0xFB || status == 0xFC) && s_transportCb)
    s_transportCb(status, 1);                                 // BLE start/continue/stop
}

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
    }
  }

  #if DEBUG_SERIAL
  Serial.println("[MIDI] BLE connected");
  #endif
}

static void onBleDisconnected() {
  s_bleConnected = false;
  #if DEBUG_SERIAL
  Serial.println("[MIDI] BLE disconnected");
  #endif
}

// =================================================================
// Constructor
// =================================================================

MidiTransport::MidiTransport() {}

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

  // --- BLE MIDI Init (skip if BLE_OFF) ---
  if (s_bleIntervalSetting.load(std::memory_order_relaxed) != BLE_OFF) {
    BLEMidiServer.begin(DEVICE_NAME_BLE);
    BLEMidiServer.setOnConnectCallback(onBleConnected);
    BLEMidiServer.setOnDisconnectCallback(onBleDisconnected);
    BLEMidiServer.setSystemRTCallback(onBleSystemRT);

    #if DEBUG_SERIAL
    Serial.println("[MIDI] BLE MIDI initialized.");
    #endif
  } else {
    #if DEBUG_SERIAL
    Serial.println("[MIDI] BLE disabled (USB only).");
    #endif
  }
}

// =================================================================
// Loop Housekeeping + USB Clock Polling
// =================================================================

void MidiTransport::update() {
  // Track USB connection state changes
  #if DEBUG_SERIAL
  {
    static bool s_lastUsbMounted = false;
    bool mounted = tud_mounted();
    if (mounted != s_lastUsbMounted) {
      Serial.printf("[MIDI] USB %s\n", mounted ? "connected" : "disconnected");
      s_lastUsbMounted = mounted;
    }
  }
  #endif

  // Poll USB MIDI for incoming clock messages (system real-time)
  if (tud_mounted()) {
    midiEventPacket_t pkt;
    while (usbMidi.readPacket(&pkt)) {
      uint8_t cin = MIDI_EP_HEADER_CIN_GET(pkt.header);
      if (cin == 0x0F) {  // CIN 0x0F = Single Byte (system real-time)
        uint8_t status = pkt.byte1;
        if (status == 0xF8 && s_clockCb)     s_clockCb(0);        // USB clock tick
        if ((status == 0xFA || status == 0xFB || status == 0xFC) && s_transportCb)
          s_transportCb(status, 0);                                 // USB start/continue/stop
      }
    }
  }
}

// =================================================================
// BLE Connection Interval
// =================================================================

void MidiTransport::setBleInterval(uint8_t interval) {
  s_bleIntervalSetting.store(interval, std::memory_order_relaxed);
}

// =================================================================
// Clock Callback Setters
// =================================================================

void MidiTransport::setClockCallback(MidiClockCallback cb) {
  s_clockCb = cb;
}

void MidiTransport::setTransportCallback(MidiTransportCallback cb) {
  s_transportCb = cb;
}

// =================================================================
// Clock Output (Master mode)
// =================================================================

void MidiTransport::sendClockTick() {
  if (tud_mounted()) {
    midiEventPacket_t pkt = {0x0F, 0xF8, 0, 0};
    usbMidi.writePacket(&pkt);
  }
  if (BLEMidiServer.isConnected()) {
    BLEMidiServer.sendSystemRT(0xF8);
  }
}

// =================================================================
// CC 123 All Notes Off (Panic)
// =================================================================

void MidiTransport::sendAllNotesOff(uint8_t channel) {
  // CC 123, value 0 = All Notes Off (MIDI standard)
  uint8_t status = 0xB0 | (channel & 0x0F);
  if (tud_mounted()) {
    midiEventPacket_t pkt = {0x0B, status, 123, 0};
    usbMidi.writePacket(&pkt);
  }
  if (BLEMidiServer.isConnected()) {
    BLEMidiServer.controlChange(channel, 123, 0);
  }
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

void MidiTransport::sendCC(uint8_t channel, uint8_t cc, uint8_t value) {
  if (tud_mounted()) {
    uint8_t status = 0xB0 | (channel & 0x0F);
    midiEventPacket_t pkt = {0x0B, status, cc, value};
    usbMidi.writePacket(&pkt);
  }
  if (BLEMidiServer.isConnected()) {
    BLEMidiServer.controlChange(channel, cc, value);
  }
}

// =================================================================
// Connection State
// =================================================================

bool MidiTransport::isUsbConnected() const {
  return tud_mounted();
}

bool MidiTransport::isBleConnected() const {
  return s_bleConnected.load(std::memory_order_relaxed);
}
