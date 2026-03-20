#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class PotRouter;

class NvsManager {
public:
  NvsManager();

  // Creates the NVS task on Core 1 (low priority)
  void begin();

  // --- Non-blocking queue calls (from loop, signals NVS task) ---
  void queueBankWrite(uint8_t bank);
  void queueScaleWrite(uint8_t bankIdx, const ScaleConfig& cfg);
  void queueBankTypesWrite(const BankType* types);
  void queueVelocityWrite(uint8_t bankIdx, uint8_t baseVel, uint8_t variation);
  void queuePitchBendWrite(uint8_t bankIdx, uint16_t offset);
  void queueArpPotWrite(uint8_t bankIdx, float gate, float swing,
                         ArpDivision div, ArpPattern pat, uint8_t octave);
  void queueTempoWrite(uint16_t bpm);
  void queueLedBrightnessWrite(uint8_t brightness);
  void queuePadSensitivityWrite(uint8_t sensitivity);
  void queuePadOrderWrite(const uint8_t* order);
  void queueControlPadsWrite();

  // --- Blocking reads (called once at boot before loop starts) ---
  // Loads all NVS data into the provided outputs. Missing data → defaults.
  void loadAll(BankSlot* banks, uint8_t& currentBank,
               uint8_t* padOrder, uint8_t* bankPads,
               uint8_t* rootPads, uint8_t* modePads,
               uint8_t& chromaticPad, uint8_t& holdPad,
               uint8_t& playStopPad, PotRouter& potRouter,
               SettingsStore& settings);

  // Update pad-pressed state (call from loop before notifyIfDirty)
  void setAnyPadPressed(bool pressed);

  // Call at end of loop — signals NVS task if anything is dirty
  void notifyIfDirty();

  // Debounce: call with current millis, pot dirty state, pot router for value snapshot,
  // and current bank context for per-bank params.
  void tickPotDebounce(uint32_t now, bool potRouterDirty, const PotRouter& potRouter,
                        uint8_t currentBank, BankType currentType);

private:
  static void nvsTask(void* arg);
  void commitAll();

  // FreeRTOS task handle
  void* _taskHandle;

  // Dirty flags (set by queue calls, cleared by commitAll)
  volatile bool _bankDirty;
  volatile bool _scaleDirty[NUM_BANKS];
  volatile bool _typesDirty;
  volatile bool _potDirty;
  volatile bool _velocityDirty[NUM_BANKS];
  volatile bool _pitchBendDirty[NUM_BANKS];
  volatile bool _arpPotDirty[NUM_BANKS];
  volatile bool _tempoDirty;
  volatile bool _ledBrightDirty;
  volatile bool _padSensDirty;
  volatile bool _padOrderDirty;
  volatile bool _controlPadsDirty;
  volatile bool _anyDirty;

  // Pending data (copied by queue calls, read by NVS task)
  uint8_t     _pendingBank;
  ScaleConfig _pendingScale[NUM_BANKS];
  BankType    _pendingTypes[NUM_BANKS];
  uint8_t     _pendingBaseVel[NUM_BANKS];
  uint8_t     _pendingVelVar[NUM_BANKS];
  uint16_t    _pendingPitchBend[NUM_BANKS];
  ArpPotStore _pendingArpPot[NUM_BANKS];
  uint16_t    _pendingTempo;
  uint8_t     _pendingLedBright;
  uint8_t     _pendingPadSens;
  uint8_t     _pendingPadOrder[NUM_KEYS];

  // Global pot params (shape, slew, deadzone) — stored as raw values
  float       _pendingResponseShape;
  uint16_t    _pendingSlewRate;
  uint16_t    _pendingAtDeadzone;

  // Pot debounce timer
  uint32_t    _potLastChangeMs;
  bool        _potPendingSave;

  // Safety: no write while pads pressed
  volatile bool _anyPadPressed;

  // Internal save methods
  void saveBank();
  void saveBankTypes();
  void savePotParams();
  void saveArpPotParams();
  void saveTempo();
  void saveLedBrightness();
  void savePadSensitivity();
  void savePadOrder();
  void saveControlPads();
};

#endif // NVS_MANAGER_H
