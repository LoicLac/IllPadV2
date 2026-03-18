#include "soc/soc_caps.h"
#if SOC_USB_OTG_SUPPORTED
#include "esp32-hal-tinyusb.h"
#include "sdkconfig.h"

#if CONFIG_TINYUSB_MIDI_ENABLED

#pragma once

#define MIDI_EP_HEADER_CN_GET(x)  (x >> 4)
#define MIDI_EP_HEADER_CIN_GET(x) ((midi_code_index_number_t)((x) & 0xF))

typedef struct {
  uint8_t header;
  uint8_t byte1;
  uint8_t byte2;
  uint8_t byte3;
} midiEventPacket_t;

class USBMIDI {
private:
  static char *midiUserDeviceName;              // user device name
  static void setDeviceName(const char *name);  // set user device name limited to 32 characters

public:
  /**
   * @brief Default constructor
   * Will use the compile-time name if set via SET_USB_MIDI_DEVICE_NAME(),
   * otherwise uses "TinyUSB MIDI"
  */
  USBMIDI(void);

  /**
   * @brief Set the current device name
   * 1. Name set via constructor (if any)
   * 2. Name set via SET_USB_MIDI_DEVICE_NAME() macro (if defined)
   * 3. Default name "TinyUSB MIDI"
   * It has no effect if name is set as NULL or ""
  */
  USBMIDI(const char *name);

  ~USBMIDI();

  void begin(void);
  void end(void);

  /**
   * @brief Get the current device name
  */
  static const char *getCurrentDeviceName(void);

  /* User-level API */

  void noteOn(uint8_t note, uint8_t velocity = 0, uint8_t channel = 1);
  void noteOff(uint8_t note, uint8_t velocity = 0, uint8_t channel = 1);
  void programChange(uint8_t inProgramNumber, uint8_t channel = 1);
  void controlChange(uint8_t inControlNumber, uint8_t inControlValue = 0, uint8_t channel = 1);
  void polyPressure(uint8_t note, uint8_t pressure, uint8_t channel = 1);
  void channelPressure(uint8_t pressure, uint8_t channel = 1);
  void pitchBend(int16_t pitchBendValue, uint8_t channel = 1);
  void pitchBend(uint16_t pitchBendValue, uint8_t channel = 1);
  void pitchBend(double pitchBendValue, uint8_t channel = 1);

  bool readPacket(midiEventPacket_t *packet);
  bool writePacket(midiEventPacket_t *packet);
  size_t write(uint8_t c);
};

#endif /* CONFIG_TINYUSB_MIDI_ENABLED */
#endif /* SOC_USB_OTG_SUPPORTED */
