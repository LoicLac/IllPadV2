#include "USBMIDI.h"
#if SOC_USB_OTG_SUPPORTED

#if CONFIG_TINYUSB_MIDI_ENABLED

#include "Arduino.h"
#include "esp32-hal-tinyusb.h"

#ifndef ESP32_USB_MIDI_DEFAULT_NAME
#define ESP32_USB_MIDI_DEFAULT_NAME "TinyUSB MIDI"
#endif

#define DEFAULT_CN 0

// Endpoint size for MIDI (framework uses CFG_TUD_ENDOINT0_SIZE for ep0; MIDI EPs use 64)
#ifndef CFG_TUD_ENDOINT_SIZE
#define CFG_TUD_ENDOINT_SIZE 64
#endif

char *USBMIDI::midiUserDeviceName = nullptr;

__attribute__((weak)) const char *getUSBMIDIDefaultDeviceName() {
  return ESP32_USB_MIDI_DEFAULT_NAME;
}

static bool tinyusb_midi_descriptor_loaded = false;
static bool tinyusb_midi_interface_enabled = false;

extern "C" uint16_t tusb_midi_load_descriptor(uint8_t *dst, uint8_t *itf) {
  if (tinyusb_midi_descriptor_loaded) {
    return 0;
  }
  tinyusb_midi_descriptor_loaded = true;

  uint8_t str_index = tinyusb_add_string_descriptor(USBMIDI::getCurrentDeviceName());
  uint8_t ep_in = tinyusb_get_free_in_endpoint();
  TU_VERIFY(ep_in != 0);
  uint8_t ep_out = tinyusb_get_free_out_endpoint();
  TU_VERIFY(ep_out != 0);
  uint8_t descriptor[TUD_MIDI_DESC_LEN] = {
    TUD_MIDI_DESCRIPTOR(*itf, str_index, ep_out, (uint8_t)(0x80 | ep_in), CFG_TUD_ENDOINT_SIZE),
  };
  *itf += 2;
  memcpy(dst, descriptor, TUD_MIDI_DESC_LEN);

  return TUD_MIDI_DESC_LEN;
}

USBMIDI::USBMIDI() {
  if (!tinyusb_midi_interface_enabled) {
    tinyusb_midi_interface_enabled = true;
    tinyusb_enable_interface(USB_INTERFACE_MIDI, TUD_MIDI_DESC_LEN, tusb_midi_load_descriptor);
  } else {
    log_e("USBMIDI: Multiple instances of USBMIDI not supported!");
  }
}

void USBMIDI::setDeviceName(const char *name) {
  const uint8_t maxNameLength = 32;
  if (name != nullptr && strlen(name) > 0) {
    if (strlen(name) > maxNameLength) {
      log_w("USBMIDI: Device name too long, truncating to %d characters.", maxNameLength);
    }
    if (!midiUserDeviceName) {
      midiUserDeviceName = new char[maxNameLength + 1];
    }
    if (midiUserDeviceName) {
      strncpy(midiUserDeviceName, name, maxNameLength);
      midiUserDeviceName[maxNameLength] = '\0';
    } else {
      log_e("USBMIDI: Failed to allocate memory for device name, using default name.");
    }
  } else {
    log_w("USBMIDI: No device name provided, using default name [%s].", getUSBMIDIDefaultDeviceName());
  }
}

USBMIDI::USBMIDI(const char *name) {
  if (!tinyusb_midi_interface_enabled) {
    setDeviceName(name);
    tinyusb_midi_interface_enabled = true;
    tinyusb_enable_interface(USB_INTERFACE_MIDI, TUD_MIDI_DESC_LEN, tusb_midi_load_descriptor);
  } else {
    log_e("USBMIDI: Multiple instances of USBMIDI not supported!");
  }
}

USBMIDI::~USBMIDI() {
  if (midiUserDeviceName) {
    delete[] midiUserDeviceName;
    midiUserDeviceName = nullptr;
  }
}

void USBMIDI::begin() {}
void USBMIDI::end() {}

const char *USBMIDI::getCurrentDeviceName(void) {
  if (midiUserDeviceName) {
    return midiUserDeviceName;
  }
  setDeviceName(getUSBMIDIDefaultDeviceName());
  if (midiUserDeviceName && strlen(midiUserDeviceName)) {
    return midiUserDeviceName;
  }
  return "TinyUSB MIDI";
}

#define uconstrain(amt, low, high) ((amt) <= (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define STATUS(CIN, CHANNEL) static_cast<uint8_t>(((CIN & 0x7F) << 4) | (uconstrain(CHANNEL - 1, 0, 15) & 0x7F))
#define _(x) static_cast<uint8_t>(uconstrain(x, 0, 127))

void USBMIDI::noteOn(uint8_t note, uint8_t velocity, uint8_t channel) {
  midiEventPacket_t event = {(uint8_t)MIDI_CIN_NOTE_ON, STATUS(MIDI_CIN_NOTE_ON, channel), _(note), _(velocity)};
  writePacket(&event);
}

void USBMIDI::noteOff(uint8_t note, uint8_t velocity, uint8_t channel) {
  midiEventPacket_t event = {(uint8_t)MIDI_CIN_NOTE_OFF, STATUS(MIDI_CIN_NOTE_OFF, channel), _(note), _(velocity)};
  writePacket(&event);
}

void USBMIDI::programChange(uint8_t program, uint8_t channel) {
  midiEventPacket_t event = {(uint8_t)MIDI_CIN_PROGRAM_CHANGE, STATUS(MIDI_CIN_PROGRAM_CHANGE, channel), _(program), 0x0};
  writePacket(&event);
}

void USBMIDI::controlChange(uint8_t control, uint8_t value, uint8_t channel) {
  midiEventPacket_t event = {(uint8_t)MIDI_CIN_CONTROL_CHANGE, STATUS(MIDI_CIN_CONTROL_CHANGE, channel), _(control), _(value)};
  writePacket(&event);
}

void USBMIDI::polyPressure(uint8_t note, uint8_t pressure, uint8_t channel) {
  midiEventPacket_t event = {(uint8_t)MIDI_CIN_POLY_KEYPRESS, STATUS(MIDI_CIN_POLY_KEYPRESS, channel), _(note), _(pressure)};
  writePacket(&event);
}

void USBMIDI::channelPressure(uint8_t pressure, uint8_t channel) {
  midiEventPacket_t event = {(uint8_t)MIDI_CIN_CHANNEL_PRESSURE, STATUS(MIDI_CIN_CHANNEL_PRESSURE, channel), _(pressure), 0x0};
  writePacket(&event);
}

void USBMIDI::pitchBend(int16_t value, uint8_t channel) {
  uint16_t pitchBendValue = constrain(value, -8192, 8191) + 8192;
  pitchBend(pitchBendValue, channel);
}

void USBMIDI::pitchBend(uint16_t value, uint8_t channel) {
  uint16_t pitchBendValue = static_cast<uint16_t>(uconstrain(value, 0, 16383));
  uint8_t lsb = pitchBendValue & 0x7F;
  uint8_t msb = (pitchBendValue >> 7) & 0x7F;
  midiEventPacket_t event = {(uint8_t)MIDI_CIN_PITCH_BEND_CHANGE, STATUS(MIDI_CIN_PITCH_BEND_CHANGE, channel), lsb, msb};
  writePacket(&event);
}

void USBMIDI::pitchBend(double value, uint8_t channel) {
  int16_t pitchBendValue = static_cast<int16_t>(round(constrain(value, -1.0, 1.0) * 8191.0));
  pitchBend(pitchBendValue, channel);
}

bool USBMIDI::readPacket(midiEventPacket_t *packet) {
  return tud_midi_packet_read((uint8_t *)packet);
}

bool USBMIDI::writePacket(midiEventPacket_t *packet) {
  return tud_midi_packet_write((uint8_t *)packet);
}

size_t USBMIDI::write(uint8_t c) {
  midiEventPacket_t packet = {DEFAULT_CN | MIDI_CIN_1BYTE_DATA, c, 0, 0};
  return tud_midi_packet_write((uint8_t *)&packet);
}

#endif /* CONFIG_TINYUSB_MIDI_ENABLED */
#endif /* SOC_USB_OTG_SUPPORTED */
