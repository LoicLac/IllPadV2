#ifndef SCALE_MANAGER_H
#define SCALE_MANAGER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class MidiEngine;

enum ScaleChangeType : uint8_t {
  SCALE_CHANGE_NONE      = 0,
  SCALE_CHANGE_ROOT      = 1,
  SCALE_CHANGE_MODE      = 2,
  SCALE_CHANGE_CHROMATIC = 3,
};

class ScaleManager {
public:
  ScaleManager();

  void begin(BankSlot* banks, MidiEngine* engine, uint8_t* lastKeys);

  // Call every loop. Processes scale pad presses while left button held (single-layer).
  void update(const uint8_t* keyIsPressed, bool btnHeld, BankSlot& currentSlot);

  bool isHolding() const;
  ScaleChangeType consumeScaleChange();  // Returns change type, auto-clears to NONE

  // Override pad assignments (for future NVS loading / ToolPadRoles)
  void setRootPads(const uint8_t* pads);
  void setModePads(const uint8_t* pads);
  void setChromaticPad(uint8_t pad);
  void setHoldPad(uint8_t pad);         // Hold pad index (skipped from scale processing)
  void setOctavePads(const uint8_t* pads);  // 4 pads for octave 1-4 (ARPEG only)

  bool hasOctaveChanged();       // True if octave was changed this frame (auto-clears)
  uint8_t getNewOctaveRange() const;  // 1-4, last value set by octave pad

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
  uint8_t _holdPad;           // Hold pad index (skipped from scale processing)
  uint8_t _octavePads[4];    // Octave range 1-4 pads (for ARPEG banks)
  ScaleChangeType _scaleChangeType;  // Set by processScalePads, cleared by consumeScaleChange()
  bool    _octaveChanged;    // Set by processScalePads, cleared by hasOctaveChanged()
  uint8_t _newOctaveRange;   // 1-4, last octave set by pad press

  void processScalePads(const uint8_t* keyIsPressed, BankSlot& slot);
};

#endif // SCALE_MANAGER_H
