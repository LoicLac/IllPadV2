#ifndef ARP_ENGINE_H
#define ARP_ENGINE_H

#include <stdint.h>
#include "../core/KeyboardData.h"

class MidiTransport;

class ArpEngine {
public:
  ArpEngine();

  void setChannel(uint8_t ch);
  void setPattern(ArpPattern pattern);
  void setOctaveRange(uint8_t range);      // 1-4
  void setDivision(ArpDivision div);
  void setGateLength(float gate);          // 0.0-1.0
  void setSwing(float swing);             // 0.5-0.75
  void setBaseVelocity(uint8_t vel);       // 1-127
  void setVelocityVariation(uint8_t pct);  // 0-100

  // Pile management — stores padOrder positions, NOT MIDI notes.
  // Resolution to MIDI notes happens at tick time via current ScaleConfig.
  void addPadPosition(uint8_t padOrderPos);
  void removePadPosition(uint8_t padOrderPos);
  void clearAllNotes();

  // Scale/pad context — set by main loop, used at tick for note resolution
  void setScaleConfig(const ScaleConfig& scale);
  void setPadOrder(const uint8_t* padOrder);

  void setHold(bool on);
  bool isHoldOn() const;

  void playStop();           // Toggle, restart from beginning
  bool isPlaying() const;

  void tick(MidiTransport& transport);  // Called by ArpScheduler

  uint8_t getNoteCount() const;
  bool    hasNotes() const;

private:
  uint8_t     _channel;
  ArpPattern  _pattern;
  uint8_t     _octaveRange;
  ArpDivision _division;
  float       _gateLength;
  float       _swing;
  uint8_t     _baseVelocity;
  uint8_t     _velocityVariation;

  // Note pile — stores padOrder positions (0-47), NOT MIDI notes
  uint8_t _positions[MAX_ARP_NOTES];      // Sorted pile
  uint8_t _positionCount;
  uint8_t _positionOrder[MAX_ARP_NOTES];  // Chronological order
  uint8_t _orderCount;

  // Built sequence (positions × octaves, resolved at tick time)
  uint8_t _sequence[MAX_ARP_SEQUENCE];    // padOrder positions
  uint8_t _sequenceLen;
  bool    _sequenceDirty;

  // Scale context for tick-time resolution
  ScaleConfig    _scale;
  const uint8_t* _padOrder;  // Pointer to global padOrder[48]

  // Playback state
  int16_t _stepIndex;
  int8_t  _direction;
  uint8_t _lastPlayedNote;   // MIDI note (for noteOff)
  bool    _playing;
  bool    _holdOn;

  void rebuildSequence();
};

#endif // ARP_ENGINE_H
