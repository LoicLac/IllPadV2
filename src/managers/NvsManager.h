#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"

class NvsManager {
public:
  NvsManager();

  void begin();

  // Non-blocking queue (signals dedicated NVS task)
  void queueBankWrite(uint8_t bank);
  void queueScaleWrite(uint8_t bank, const ScaleConfig& cfg);
  void queueBankTypesWrite(const BankType* types);
  void queueSettingsWrite();
  void queuePotParamsWrite();
  void queuePadOrderWrite(const uint8_t* order);
  void queueControlPadsWrite();

  // Blocking reads (called at boot)
  void loadAll(BankSlot* banks, uint8_t& currentBank,
               uint8_t* padOrder, uint8_t* bankPads,
               uint8_t* rootPads, uint8_t* modePads, uint8_t& chromaticPad,
               uint8_t* patternPads, uint8_t& octavePad,
               uint8_t& holdPad, uint8_t& playStopPad);

  // Signal task if any dirty flags set
  void notifyIfDirty();

private:
  static void nvsTask(void* arg);
  void commitAll();

  volatile bool _bankDirty;
  volatile bool _scaleDirty;
  volatile bool _typesDirty;
  volatile bool _settingsDirty;
  volatile bool _potDirty;
  volatile bool _padOrderDirty;
  volatile bool _controlPadsDirty;

  // Pending data
  uint8_t     _pendingBank;
  uint8_t     _pendingScaleBank;
  ScaleConfig _pendingScale;
  BankType    _pendingTypes[NUM_BANKS];
  uint8_t     _pendingPadOrder[NUM_KEYS];
};

#endif // NVS_MANAGER_H
