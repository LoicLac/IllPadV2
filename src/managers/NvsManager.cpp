#include "NvsManager.h"

NvsManager::NvsManager()
  : _bankDirty(false), _scaleDirty(false), _typesDirty(false),
    _settingsDirty(false), _potDirty(false), _padOrderDirty(false),
    _controlPadsDirty(false), _pendingBank(0), _pendingScaleBank(0) {
  _pendingScale = {true, 2, 0};  // C Ionian chromatic
  for (uint8_t i = 0; i < NUM_BANKS; i++) _pendingTypes[i] = BANK_NORMAL;
  for (uint8_t i = 0; i < NUM_KEYS; i++) _pendingPadOrder[i] = 0xFF;
}

void NvsManager::begin() {
  // TODO: create FreeRTOS task pinned to Core 1
}

void NvsManager::queueBankWrite(uint8_t bank) {
  _pendingBank = bank;
  _bankDirty = true;
}

void NvsManager::queueScaleWrite(uint8_t bank, const ScaleConfig& cfg) {
  _pendingScaleBank = bank;
  _pendingScale = cfg;
  _scaleDirty = true;
}

void NvsManager::queueBankTypesWrite(const BankType* types) {
  for (uint8_t i = 0; i < NUM_BANKS; i++) _pendingTypes[i] = types[i];
  _typesDirty = true;
}

void NvsManager::queueSettingsWrite() {
  _settingsDirty = true;
}

void NvsManager::queuePotParamsWrite() {
  _potDirty = true;
}

void NvsManager::queuePadOrderWrite(const uint8_t* order) {
  for (uint8_t i = 0; i < NUM_KEYS; i++) _pendingPadOrder[i] = order[i];
  _padOrderDirty = true;
}

void NvsManager::queueControlPadsWrite() {
  _controlPadsDirty = true;
}

void NvsManager::notifyIfDirty() {
  // TODO: signal NVS task via xTaskNotifyGive
}

void NvsManager::loadAll(BankSlot* banks, uint8_t& currentBank,
                          uint8_t* padOrder, uint8_t* bankPads,
                          uint8_t* rootPads, uint8_t* modePads, uint8_t& chromaticPad,
                          uint8_t* patternPads, uint8_t& octavePad,
                          uint8_t& holdPad, uint8_t& playStopPad) {
  (void)banks; (void)currentBank; (void)padOrder; (void)bankPads;
  (void)rootPads; (void)modePads; (void)chromaticPad;
  (void)patternPads; (void)octavePad; (void)holdPad; (void)playStopPad;
  // TODO: read all NVS namespaces, populate output params
}

void NvsManager::nvsTask(void* arg) {
  (void)arg;
  // TODO: FreeRTOS task loop — wait for notification, commitAll
}

void NvsManager::commitAll() {
  // TODO: write dirty data to NVS
}
