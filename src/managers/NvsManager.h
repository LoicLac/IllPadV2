#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <stdint.h>
#include <atomic>
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
  void queueVelocityWrite(uint8_t bankIdx, uint8_t baseVel, uint8_t variation);
  void queuePitchBendWrite(uint8_t bankIdx, uint16_t offset);
  void queueArpPotWrite(uint8_t bankIdx, float gate, float shuffleDepth,
                         ArpDivision div, ArpPattern pat, uint8_t octave,
                         uint8_t shuffleTmpl);
  void queueArpOctaveWrite(uint8_t bankIdx, uint8_t octave);
  void queueTempoWrite(uint16_t bpm);
  void queueLedBrightnessWrite(uint8_t brightness);
  void queuePadSensitivityWrite(uint8_t sensitivity);
  void queuePadOrderWrite(const uint8_t* order);

  // --- Blocking reads (called once at boot before loop starts) ---
  // Loads all NVS data into the provided outputs. Missing data → defaults.
  void loadAll(BankSlot* banks, uint8_t& currentBank,
               uint8_t* padOrder, uint8_t* bankPads,
               uint8_t* rootPads, uint8_t* modePads,
               uint8_t& chromaticPad, uint8_t& holdPad,
               uint8_t* octavePads,
               PotRouter& potRouter, SettingsStore& settings);

  // Access loaded quantize modes (per-bank, for ArpEngine init at boot)
  uint8_t getLoadedQuantizeMode(uint8_t bank) const;
  void    setLoadedQuantizeMode(uint8_t bank, uint8_t mode);

  // Access loaded scale group (per-bank, 0=none, 1..NUM_SCALE_GROUPS=A..D)
  uint8_t getLoadedScaleGroup(uint8_t bank) const;
  void    setLoadedScaleGroup(uint8_t bank, uint8_t group);

  // Access loaded ARPEG_GEN params (per-bank). Stored in BankTypeStore v3 (bonus/margin)
  // et v4 (proximityFactor/ecart).
  uint8_t getLoadedBonusPile(uint8_t bank) const;     // x10 (10..20), used only when type == BANK_ARPEG_GEN
  void    setLoadedBonusPile(uint8_t bank, uint8_t x10);
  uint8_t getLoadedMarginWalk(uint8_t bank) const;    // 3..12 degres
  void    setLoadedMarginWalk(uint8_t bank, uint8_t margin);
  uint8_t getLoadedProximityFactor(uint8_t bank) const; // V4 : x10 (4..20)
  void    setLoadedProximityFactor(uint8_t bank, uint8_t x10);
  uint8_t getLoadedEcart(uint8_t bank) const;           // V4 : 1..12
  void    setLoadedEcart(uint8_t bank, uint8_t ecart);

  // Access loaded arp params (for ArpEngine init at boot, after loadAll)
  const ArpPotStore& getLoadedArpParams(uint8_t bankIdx) const;

  // Access loaded LED settings (for LedController init at boot, after loadAll)
  const LedSettingsStore& getLoadedLedSettings() const;

  // Access loaded color slots (for LedController init at boot, after loadAll)
  const ColorSlotStore& getLoadedColorSlots() const;

  // Access loaded control pads (for ControlPadManager init at boot, after loadAll)
  const ControlPadStore& getLoadedControlPadStore() const;

  // --- Static NVS helpers (usable without instance, for setup Tools + menu) ---
  static bool loadBlob(const char* ns, const char* key,
                       uint16_t expectedMagic, uint8_t expectedVersion,
                       void* out, size_t expectedSize);
  static bool saveBlob(const char* ns, const char* key,
                       const void* data, size_t size);
  static bool checkBlob(const char* ns, const char* key,
                        uint16_t expectedMagic, uint8_t expectedVersion,
                        size_t expectedSize);

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

  // Dirty flags (set by queue calls on loop task, cleared by NVS task)
  std::atomic<bool> _bankDirty;
  std::atomic<bool> _scaleDirty[NUM_BANKS];
  std::atomic<bool> _potDirty;
  std::atomic<bool> _velocityDirty[NUM_BANKS];
  std::atomic<bool> _pitchBendDirty[NUM_BANKS];
  std::atomic<bool> _arpPotDirty[NUM_BANKS];
  std::atomic<bool> _tempoDirty;
  std::atomic<bool> _ledBrightDirty;
  std::atomic<bool> _padSensDirty;
  std::atomic<bool> _padOrderDirty;
  std::atomic<bool> _anyDirty;

  // Pending data (copied by queue calls, read by NVS task)
  uint8_t     _pendingBank;
  ScaleConfig _pendingScale[NUM_BANKS];
  uint8_t     _pendingBaseVel[NUM_BANKS];
  uint8_t     _pendingVelVar[NUM_BANKS];
  uint16_t    _pendingPitchBend[NUM_BANKS];
  ArpPotStore _pendingArpPot[NUM_BANKS];
  uint8_t     _loadedQuantize[NUM_BANKS];    // ArpStartMode per bank (loaded at boot)
  uint8_t     _loadedScaleGroup[NUM_BANKS];  // 0=none, 1..NUM_SCALE_GROUPS=A..D (loaded at boot)
  uint8_t     _loadedBonusPile[NUM_BANKS];   // BankTypeStore v3 : x10 (10..20), defaults 15
  uint8_t     _loadedMarginWalk[NUM_BANKS];  // BankTypeStore v3 : 3..12, defaults 7
  uint8_t     _loadedProximity[NUM_BANKS];   // BankTypeStore v4 : x10 (4..20), defaults 4
  uint8_t     _loadedEcart[NUM_BANKS];       // BankTypeStore v4 : 1..12, defaults 5
  uint16_t    _pendingTempo;
  uint8_t     _pendingLedBright;
  uint8_t     _pendingPadSens;
  uint8_t     _pendingPadOrder[NUM_KEYS];

  // LED settings (loaded at boot from NVS)
  LedSettingsStore _ledSettings;
  ColorSlotStore _colorSlots;

  // Control pads (loaded at boot from NVS, consumed by ControlPadManager::applyStore)
  ControlPadStore _ctrlStore;

  // Global pot params (shape, slew, deadzone) — stored as raw values
  float       _pendingResponseShape;
  uint16_t    _pendingSlewRate;
  uint16_t    _pendingAtDeadzone;

  // Pot debounce timer
  uint32_t    _potLastChangeMs;
  bool        _potPendingSave;

  // Safety: no write while pads pressed
  std::atomic<bool> _anyPadPressed;

  // Internal save methods
  void saveBank();
  void savePotParams();
  void saveTempo();
  void saveLedBrightness();
  void savePadSensitivity();
  void savePadOrder();
};

#endif // NVS_MANAGER_H
