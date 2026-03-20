#ifndef SCALE_MANAGER_H
#define SCALE_MANAGER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class MidiEngine;

class ScaleManager {
public:
  ScaleManager();

  void begin(BankSlot* banks, MidiEngine* engine, uint8_t* lastKeys);

  // Call every loop. Processes scale pad presses while left button held (single-layer).
  void update(const uint8_t* keyIsPressed, bool btnHeld, BankSlot& currentSlot);

  bool isHolding() const;
  bool hasScaleChanged();  // True if scale was modified this frame (auto-clears)

  // Override pad assignments (for future NVS loading / ToolPadRoles)
  void setRootPads(const uint8_t* pads);
  void setModePads(const uint8_t* pads);
  void setChromaticPad(uint8_t pad);
  void setHoldPad(uint8_t pad);

private:
  BankSlot*   _banks;
  MidiEngine* _engine;
  uint8_t*    _lastKeys;
  bool        _holding;
  bool        _lastBtnState;

  // Edge detection for scale pads (prevent repeat while holding)
  uint8_t _lastScaleKeys[NUM_KEYS];

  // Pad assignments (defaults: root=8-14, mode=15-21, chromatic=22)
  uint8_t _rootPads[7];
  uint8_t _modePads[7];
  uint8_t _chromaticPad;
  uint8_t _holdPad;       // HOLD toggle pad (for ARPEG banks)
  bool    _scaleChanged;  // Set by processScalePads, cleared by hasScaleChanged()

  void processScalePads(const uint8_t* keyIsPressed, BankSlot& slot);
};

#endif // SCALE_MANAGER_H
