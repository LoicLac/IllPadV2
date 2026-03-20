#include "NvsManager.h"
#include "PotRouter.h"
#include <Preferences.h>
#include <Arduino.h>
#include <string.h>

// =================================================================
// Constructor
// =================================================================
NvsManager::NvsManager()
  : _taskHandle(nullptr)
  , _bankDirty(false)
  , _typesDirty(false)
  , _potDirty(false)
  , _tempoDirty(false)
  , _ledBrightDirty(false)
  , _padSensDirty(false)
  , _padOrderDirty(false)
  , _controlPadsDirty(false)
  , _anyDirty(false)
  , _pendingBank(DEFAULT_BANK)
  , _pendingLedBright(128)
  , _pendingPadSens(PAD_SENSITIVITY_DEFAULT)
  , _pendingTempo(TEMPO_BPM_DEFAULT)
  , _pendingResponseShape(RESPONSE_SHAPE_DEFAULT)
  , _pendingSlewRate(SLEW_RATE_DEFAULT)
  , _pendingAtDeadzone(AT_DEADZONE_DEFAULT)
  , _potLastChangeMs(0)
  , _potPendingSave(false)
  , _anyPadPressed(false)
{
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _scaleDirty[i] = false;
    _velocityDirty[i] = false;
    _pitchBendDirty[i] = false;
    _arpPotDirty[i] = false;
    _pendingScale[i] = {true, 2, 0};  // chromatic, root C, Ionian
    _pendingTypes[i] = BANK_NORMAL;
    _pendingBaseVel[i] = DEFAULT_BASE_VELOCITY;
    _pendingVelVar[i] = DEFAULT_VELOCITY_VARIATION;
    _pendingPitchBend[i] = DEFAULT_PITCH_BEND_OFFSET;
    _pendingArpPot[i] = {2048, 2048, DIV_1_8, ARP_UP, 1, 0};
  }
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    _pendingPadOrder[i] = i;
  }
}

// =================================================================
// begin — create the NVS task
// =================================================================
void NvsManager::begin() {
  xTaskCreatePinnedToCore(nvsTask, "nvs", 4096, this, 1, (TaskHandle_t*)&_taskHandle, 1);
  #if DEBUG_SERIAL
  Serial.println("[NVS] Task created (Core 1, priority 1).");
  #endif
}

// =================================================================
// NVS Task — waits for notification, then commits
// =================================================================
void NvsManager::nvsTask(void* arg) {
  NvsManager* self = (NvsManager*)arg;
  for (;;) {
    // Wait for notification (indefinite block)
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Small delay to batch multiple dirty flags
    vTaskDelay(pdMS_TO_TICKS(50));
    self->commitAll();
  }
}

// =================================================================
// notifyIfDirty — called from loop(), signals the NVS task
// =================================================================
void NvsManager::setAnyPadPressed(bool pressed) {
  _anyPadPressed = pressed;
}

void NvsManager::notifyIfDirty() {
  if (_anyDirty && _taskHandle) {
    xTaskNotifyGive((TaskHandle_t)_taskHandle);
    _anyDirty = false;
  }
}

// =================================================================
// tickPotDebounce — 10s debounce for pot param saves
// =================================================================
void NvsManager::tickPotDebounce(uint32_t now, bool potRouterDirty, const PotRouter& potRouter,
                                  uint8_t currentBank, BankType currentType) {
  if (potRouterDirty) {
    _potLastChangeMs = now;
    _potPendingSave = true;
  }
  if (!_potPendingSave || (now - _potLastChangeMs < POT_NVS_SAVE_DEBOUNCE_MS)) {
    return;
  }
  _potPendingSave = false;

  // === Global pot params (shape, slew, deadzone) ===
  float newShape = potRouter.getResponseShape();
  uint16_t newSlew = potRouter.getSlewRate();
  uint16_t newDeadzone = potRouter.getAtDeadzone();
  if (newShape != _pendingResponseShape || newSlew != _pendingSlewRate
      || newDeadzone != _pendingAtDeadzone) {
    _pendingResponseShape = newShape;
    _pendingSlewRate = newSlew;
    _pendingAtDeadzone = newDeadzone;
    _potDirty = true;
    _anyDirty = true;
  }

  // === Tempo (global) ===
  uint16_t newTempo = potRouter.getTempoBPM();
  if (newTempo != _pendingTempo) {
    _pendingTempo = newTempo;
    _tempoDirty = true;
    _anyDirty = true;
  }

  // === LED brightness (global) ===
  uint8_t newBright = potRouter.getLedBrightness();
  if (newBright != _pendingLedBright) {
    _pendingLedBright = newBright;
    _ledBrightDirty = true;
    _anyDirty = true;
  }

  // === Pad sensitivity (global) ===
  uint8_t newSens = potRouter.getPadSensitivity();
  if (newSens != _pendingPadSens) {
    _pendingPadSens = newSens;
    _padSensDirty = true;
    _anyDirty = true;
  }

  // === Per-bank: velocity + variation (NORMAL + ARPEG) ===
  {
    uint8_t newVel = potRouter.getBaseVelocity();
    uint8_t newVar = potRouter.getVelocityVariation();
    if (newVel != _pendingBaseVel[currentBank] || newVar != _pendingVelVar[currentBank]) {
      _pendingBaseVel[currentBank] = newVel;
      _pendingVelVar[currentBank] = newVar;
      _velocityDirty[currentBank] = true;
      _anyDirty = true;
    }
  }

  // === Per-bank: pitch bend (NORMAL only) ===
  if (currentType == BANK_NORMAL) {
    uint16_t newPB = potRouter.getPitchBend();
    if (newPB != _pendingPitchBend[currentBank]) {
      _pendingPitchBend[currentBank] = newPB;
      _pitchBendDirty[currentBank] = true;
      _anyDirty = true;
    }
  }

  // === Per-bank: arp pot params (ARPEG only) ===
  if (currentType == BANK_ARPEG) {
    ArpPotStore newArp;
    newArp.gateRaw     = (uint16_t)(potRouter.getGateLength() * 4095.0f);
    newArp.swingRaw    = (uint16_t)((potRouter.getSwing() - 0.5f) / 0.25f * 4095.0f);
    newArp.division    = (uint8_t)potRouter.getDivision();
    newArp.pattern     = (uint8_t)potRouter.getPattern();
    newArp.octaveRange = potRouter.getOctaveRange();
    newArp.reserved    = 0;

    const ArpPotStore& cur = _pendingArpPot[currentBank];
    if (newArp.gateRaw != cur.gateRaw || newArp.swingRaw != cur.swingRaw
        || newArp.division != cur.division || newArp.pattern != cur.pattern
        || newArp.octaveRange != cur.octaveRange) {
      _pendingArpPot[currentBank] = newArp;
      _arpPotDirty[currentBank] = true;
      _anyDirty = true;
    }
  }
}

// =================================================================
// Queue methods — set dirty flag + copy pending data
// =================================================================

void NvsManager::queueBankWrite(uint8_t bank) {
  _pendingBank = bank;
  _bankDirty = true;
  _anyDirty = true;
}

void NvsManager::queueScaleWrite(uint8_t bankIdx, const ScaleConfig& cfg) {
  if (bankIdx >= NUM_BANKS) return;
  _pendingScale[bankIdx] = cfg;
  _scaleDirty[bankIdx] = true;
  _anyDirty = true;
}

void NvsManager::queueBankTypesWrite(const BankType* types) {
  for (uint8_t i = 0; i < NUM_BANKS; i++) _pendingTypes[i] = types[i];
  _typesDirty = true;
  _anyDirty = true;
}

void NvsManager::queueVelocityWrite(uint8_t bankIdx, uint8_t baseVel, uint8_t variation) {
  if (bankIdx >= NUM_BANKS) return;
  _pendingBaseVel[bankIdx] = baseVel;
  _pendingVelVar[bankIdx] = variation;
  _velocityDirty[bankIdx] = true;
  _anyDirty = true;
}

void NvsManager::queuePitchBendWrite(uint8_t bankIdx, uint16_t offset) {
  if (bankIdx >= NUM_BANKS) return;
  _pendingPitchBend[bankIdx] = offset;
  _pitchBendDirty[bankIdx] = true;
  _anyDirty = true;
}

void NvsManager::queueLedBrightnessWrite(uint8_t brightness) {
  _pendingLedBright = brightness;
  _ledBrightDirty = true;
  _anyDirty = true;
}

void NvsManager::queuePadSensitivityWrite(uint8_t sensitivity) {
  _pendingPadSens = sensitivity;
  _padSensDirty = true;
  _anyDirty = true;
}

void NvsManager::queueArpPotWrite(uint8_t bankIdx, float gate, float swing,
                                    ArpDivision div, ArpPattern pat, uint8_t octave) {
  if (bankIdx >= NUM_BANKS) return;
  _pendingArpPot[bankIdx].gateRaw     = (uint16_t)(gate * 4095.0f);
  _pendingArpPot[bankIdx].swingRaw    = (uint16_t)((swing - 0.5f) / 0.25f * 4095.0f);
  _pendingArpPot[bankIdx].division    = (uint8_t)div;
  _pendingArpPot[bankIdx].pattern     = (uint8_t)pat;
  _pendingArpPot[bankIdx].octaveRange = octave;
  _arpPotDirty[bankIdx] = true;
  _anyDirty = true;
}

void NvsManager::queueTempoWrite(uint16_t bpm) {
  _pendingTempo = bpm;
  _tempoDirty = true;
  _anyDirty = true;
}

void NvsManager::queuePadOrderWrite(const uint8_t* order) {
  memcpy(_pendingPadOrder, order, NUM_KEYS);
  _padOrderDirty = true;
  _anyDirty = true;
}

void NvsManager::queueControlPadsWrite() {
  _controlPadsDirty = true;
  _anyDirty = true;
}

// =================================================================
// commitAll — called by NVS task, writes all dirty data
// =================================================================
void NvsManager::commitAll() {
  // Safety: don't write flash while pads are pressed (flash stall = latency)
  if (_anyPadPressed) {
    _anyDirty = true;  // Re-arm so we retry next notification
    return;
  }

  if (_bankDirty) { saveBank(); _bankDirty = false; }

  // Batch scale saves (one namespace open)
  {
    bool anyScale = false;
    for (uint8_t i = 0; i < NUM_BANKS; i++) { if (_scaleDirty[i]) { anyScale = true; break; } }
    if (anyScale) {
      Preferences prefs;
      if (prefs.begin(SCALE_NVS_NAMESPACE, false)) {
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
          if (_scaleDirty[i]) {
            char key[8];
            snprintf(key, sizeof(key), "cfg_%d", i);
            prefs.putBytes(key, &_pendingScale[i], sizeof(ScaleConfig));
            _scaleDirty[i] = false;
          }
        }
        prefs.end();
      }
    }
  }

  // Batch velocity saves (one namespace open)
  {
    bool anyVel = false;
    for (uint8_t i = 0; i < NUM_BANKS; i++) { if (_velocityDirty[i]) { anyVel = true; break; } }
    if (anyVel) {
      Preferences prefs;
      if (prefs.begin(VELOCITY_NVS_NAMESPACE, false)) {
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
          if (_velocityDirty[i]) {
            char keyVel[8], keyVar[8];
            snprintf(keyVel, sizeof(keyVel), "vel_%d", i);
            snprintf(keyVar, sizeof(keyVar), "var_%d", i);
            prefs.putUChar(keyVel, _pendingBaseVel[i]);
            prefs.putUChar(keyVar, _pendingVelVar[i]);
            _velocityDirty[i] = false;
          }
        }
        prefs.end();
      }
    }
  }

  // Batch pitch bend saves (one namespace open)
  {
    bool anyPB = false;
    for (uint8_t i = 0; i < NUM_BANKS; i++) { if (_pitchBendDirty[i]) { anyPB = true; break; } }
    if (anyPB) {
      Preferences prefs;
      if (prefs.begin(PITCHBEND_NVS_NAMESPACE, false)) {
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
          if (_pitchBendDirty[i]) {
            char key[8];
            snprintf(key, sizeof(key), "pb_%d", i);
            prefs.putUShort(key, _pendingPitchBend[i]);
            _pitchBendDirty[i] = false;
          }
        }
        prefs.end();
      }
    }
  }

  // Batch arp pot saves (one namespace open)
  {
    bool anyArp = false;
    for (uint8_t i = 0; i < NUM_BANKS; i++) { if (_arpPotDirty[i]) { anyArp = true; break; } }
    if (anyArp) {
      Preferences prefs;
      if (prefs.begin(ARP_POT_NVS_NAMESPACE, false)) {
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
          if (_arpPotDirty[i]) {
            char key[8];
            snprintf(key, sizeof(key), "arp_%d", i);
            prefs.putBytes(key, &_pendingArpPot[i], sizeof(ArpPotStore));
            _arpPotDirty[i] = false;
          }
        }
        prefs.end();
      }
    }
  }

  if (_typesDirty)       { saveBankTypes();     _typesDirty = false; }
  if (_potDirty)         { savePotParams();     _potDirty = false; }
  if (_tempoDirty)       { saveTempo();         _tempoDirty = false; }
  if (_ledBrightDirty)   { saveLedBrightness(); _ledBrightDirty = false; }
  if (_padSensDirty)     { savePadSensitivity(); _padSensDirty = false; }
  if (_padOrderDirty)    { savePadOrder();       _padOrderDirty = false; }
  if (_controlPadsDirty) { saveControlPads();    _controlPadsDirty = false; }
}

// =================================================================
// loadAll — blocking reads at boot
// =================================================================
void NvsManager::loadAll(BankSlot* banks, uint8_t& currentBank,
                          uint8_t* padOrder, uint8_t* bankPads,
                          uint8_t* rootPads, uint8_t* modePads,
                          uint8_t& chromaticPad, uint8_t& holdPad,
                          uint8_t& playStopPad, PotRouter& potRouter,
                          SettingsStore& settings) {
  Preferences prefs;

  // --- Current bank ---
  if (prefs.begin(BANK_NVS_NAMESPACE, true)) {
    currentBank = prefs.getUChar(BANK_NVS_KEY, DEFAULT_BANK);
    if (currentBank >= NUM_BANKS) currentBank = DEFAULT_BANK;
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[NVS] Bank loaded: %d\n", currentBank);
    #endif
  }

  // --- Scale config per bank ---
  if (prefs.begin(SCALE_NVS_NAMESPACE, true)) {
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      char key[8];
      snprintf(key, sizeof(key), "cfg_%d", i);
      size_t len = prefs.getBytesLength(key);
      if (len == sizeof(ScaleConfig)) {
        prefs.getBytes(key, &banks[i].scale, sizeof(ScaleConfig));
        #if DEBUG_SERIAL
        Serial.printf("[NVS] Scale bank %d: chrom=%d root=%d mode=%d\n",
                      i, banks[i].scale.chromatic, banks[i].scale.root, banks[i].scale.mode);
        #endif
      }
    }
    prefs.end();
  }

  // --- Bank types ---
  if (prefs.begin(BANKTYPE_NVS_NAMESPACE, true)) {
    size_t len = prefs.getBytesLength(BANKTYPE_NVS_KEY);
    if (len == NUM_BANKS) {
      uint8_t types[NUM_BANKS];
      prefs.getBytes(BANKTYPE_NVS_KEY, types, NUM_BANKS);
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        banks[i].type = (BankType)types[i];
      }
      #if DEBUG_SERIAL
      Serial.println("[NVS] Bank types loaded.");
      #endif
    }
    prefs.end();
  }

  // --- Velocity per bank ---
  if (prefs.begin(VELOCITY_NVS_NAMESPACE, true)) {
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      char keyVel[8], keyVar[8];
      snprintf(keyVel, sizeof(keyVel), "vel_%d", i);
      snprintf(keyVar, sizeof(keyVar), "var_%d", i);
      banks[i].baseVelocity = prefs.getUChar(keyVel, DEFAULT_BASE_VELOCITY);
      banks[i].velocityVariation = prefs.getUChar(keyVar, DEFAULT_VELOCITY_VARIATION);
    }
    prefs.end();
    #if DEBUG_SERIAL
    Serial.println("[NVS] Velocity params loaded.");
    #endif
  }

  // --- Pitch bend per bank ---
  if (prefs.begin(PITCHBEND_NVS_NAMESPACE, true)) {
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      char key[8];
      snprintf(key, sizeof(key), "pb_%d", i);
      banks[i].pitchBendOffset = prefs.getUShort(key, DEFAULT_PITCH_BEND_OFFSET);
    }
    prefs.end();
    #if DEBUG_SERIAL
    Serial.println("[NVS] Pitch bend offsets loaded.");
    #endif
  }

  // --- Arp pot params per bank ---
  if (prefs.begin(ARP_POT_NVS_NAMESPACE, true)) {
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      char key[8];
      snprintf(key, sizeof(key), "arp_%d", i);
      size_t len = prefs.getBytesLength(key);
      if (len == sizeof(ArpPotStore)) {
        prefs.getBytes(key, &_pendingArpPot[i], sizeof(ArpPotStore));
      }
    }
    prefs.end();
    #if DEBUG_SERIAL
    Serial.println("[NVS] Arp pot params loaded.");
    #endif
  }

  // --- Tempo ---
  if (prefs.begin(TEMPO_NVS_NAMESPACE, true)) {
    _pendingTempo = prefs.getUShort(TEMPO_NVS_KEY, TEMPO_BPM_DEFAULT);
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[NVS] Tempo: %d BPM\n", _pendingTempo);
    #endif
  }

  // --- Global pot params (shape, slew, deadzone) ---
  if (prefs.begin(POT_PARAMS_NVS_NAMESPACE, true)) {
    size_t len = prefs.getBytesLength(POT_PARAMS_NVS_KEY);
    if (len == sizeof(PotParamsStore)) {
      PotParamsStore pps;
      prefs.getBytes(POT_PARAMS_NVS_KEY, &pps, sizeof(PotParamsStore));
      if (pps.magic == EEPROM_MAGIC && pps.version == POT_PARAMS_VERSION) {
        _pendingResponseShape = (float)pps.responseShapeRaw / 4095.0f;
        _pendingSlewRate = pps.slewRate;
        _pendingAtDeadzone = pps.atDeadzone;
        #if DEBUG_SERIAL
        Serial.printf("[NVS] Pot params: shape=%.2f slew=%d dz=%d\n",
                      _pendingResponseShape, _pendingSlewRate, _pendingAtDeadzone);
        #endif
      }
    }
    prefs.end();
  }

  // --- LED brightness ---
  if (prefs.begin(LED_NVS_NAMESPACE, true)) {
    _pendingLedBright = prefs.getUChar(LED_NVS_KEY, 128);
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[NVS] LED brightness: %d\n", _pendingLedBright);
    #endif
  }

  // --- Pad sensitivity ---
  if (prefs.begin(SENSITIVITY_NVS_NAMESPACE, true)) {
    _pendingPadSens = prefs.getUChar(SENSITIVITY_NVS_KEY, PAD_SENSITIVITY_DEFAULT);
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[NVS] Pad sensitivity: %d\n", _pendingPadSens);
    #endif
  }

  // --- Pad order ---
  if (prefs.begin(NOTEMAP_NVS_NAMESPACE, true)) {
    size_t len = prefs.getBytesLength(NOTEMAP_NVS_KEY);
    if (len == sizeof(NoteMapStore)) {
      NoteMapStore nms;
      prefs.getBytes(NOTEMAP_NVS_KEY, &nms, sizeof(NoteMapStore));
      if (nms.magic == EEPROM_MAGIC && nms.version == NOTEMAP_VERSION) {
        memcpy(padOrder, nms.noteMap, NUM_KEYS);
        #if DEBUG_SERIAL
        Serial.println("[NVS] Pad order loaded.");
        #endif
      }
    }
    prefs.end();
  }

  // --- Bank pads ---
  if (prefs.begin(BANKPAD_NVS_NAMESPACE, true)) {
    size_t len = prefs.getBytesLength(BANKPAD_NVS_KEY);
    if (len == sizeof(BankPadStore)) {
      BankPadStore bps;
      prefs.getBytes(BANKPAD_NVS_KEY, &bps, sizeof(BankPadStore));
      if (bps.magic == EEPROM_MAGIC && bps.version == BANKPAD_VERSION) {
        memcpy(bankPads, bps.bankPads, NUM_BANKS);
        #if DEBUG_SERIAL
        Serial.println("[NVS] Bank pads loaded.");
        #endif
      }
    }
    prefs.end();
  }

  // --- Scale control pads ---
  if (prefs.begin(SCALE_PAD_NVS_NAMESPACE, true)) {
    size_t len;
    len = prefs.getBytesLength(SCALE_PAD_ROOT_KEY);
    if (len == 7) prefs.getBytes(SCALE_PAD_ROOT_KEY, rootPads, 7);
    len = prefs.getBytesLength(SCALE_PAD_MODE_KEY);
    if (len == 7) prefs.getBytes(SCALE_PAD_MODE_KEY, modePads, 7);
    chromaticPad = prefs.getUChar(SCALE_PAD_CHROM_KEY, 22);
    prefs.end();
    #if DEBUG_SERIAL
    Serial.println("[NVS] Scale pads loaded.");
    #endif
  }

  // --- Arp control pads ---
  if (prefs.begin(ARP_PAD_NVS_NAMESPACE, true)) {
    holdPad = prefs.getUChar(ARP_PAD_HOLD_KEY, 23);
    playStopPad = prefs.getUChar(ARP_PAD_PS_KEY, 24);
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[NVS] Arp pads: hold=%d ps=%d\n", holdPad, playStopPad);
    #endif
  }

  // --- Settings (profile, AT rate, BLE interval) ---
  settings = {EEPROM_MAGIC, SETTINGS_VERSION, DEFAULT_BASELINE_PROFILE, AT_RATE_DEFAULT, DEFAULT_BLE_INTERVAL};
  if (prefs.begin(SETTINGS_NVS_NAMESPACE, true)) {
    size_t len = prefs.getBytesLength(SETTINGS_NVS_KEY);
    if (len == sizeof(SettingsStore)) {
      SettingsStore tmp;
      prefs.getBytes(SETTINGS_NVS_KEY, &tmp, sizeof(SettingsStore));
      if (tmp.magic == EEPROM_MAGIC && tmp.version == SETTINGS_VERSION) {
        settings = tmp;
      }
    }
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[NVS] Settings: profile=%d, atRate=%d, bleInt=%d\n",
                  settings.baselineProfile, settings.aftertouchRate, settings.bleInterval);
    #endif
  }

  (void)potRouter;  // PotRouter stored values will be set by caller
}

// =================================================================
// Save methods — each opens/closes its own Preferences namespace
// =================================================================

void NvsManager::saveBank() {
  Preferences prefs;
  if (prefs.begin(BANK_NVS_NAMESPACE, false)) {
    prefs.putUChar(BANK_NVS_KEY, _pendingBank);
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[NVS] Saved bank: %d\n", _pendingBank);
    #endif
  }
}

void NvsManager::saveBankTypes() {
  Preferences prefs;
  if (prefs.begin(BANKTYPE_NVS_NAMESPACE, false)) {
    uint8_t types[NUM_BANKS];
    for (uint8_t i = 0; i < NUM_BANKS; i++) types[i] = (uint8_t)_pendingTypes[i];
    prefs.putBytes(BANKTYPE_NVS_KEY, types, NUM_BANKS);
    prefs.end();
    #if DEBUG_SERIAL
    Serial.println("[NVS] Saved bank types.");
    #endif
  }
}

void NvsManager::savePotParams() {
  Preferences prefs;
  if (prefs.begin(POT_PARAMS_NVS_NAMESPACE, false)) {
    PotParamsStore pps;
    pps.magic = EEPROM_MAGIC;
    pps.version = POT_PARAMS_VERSION;
    pps.reserved = 0;
    pps.responseShapeRaw = (uint16_t)(_pendingResponseShape * 4095.0f);
    pps.slewRate = _pendingSlewRate;
    pps.atDeadzone = _pendingAtDeadzone;
    prefs.putBytes(POT_PARAMS_NVS_KEY, &pps, sizeof(PotParamsStore));
    prefs.end();
    #if DEBUG_SERIAL
    Serial.println("[NVS] Saved pot params.");
    #endif
  }
}

void NvsManager::saveArpPotParams() {
  // Batched in commitAll — this is a no-op placeholder (batch logic is in commitAll)
}

void NvsManager::saveTempo() {
  Preferences prefs;
  if (prefs.begin(TEMPO_NVS_NAMESPACE, false)) {
    prefs.putUShort(TEMPO_NVS_KEY, _pendingTempo);
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[NVS] Saved tempo: %d BPM\n", _pendingTempo);
    #endif
  }
}

void NvsManager::saveLedBrightness() {
  Preferences prefs;
  if (prefs.begin(LED_NVS_NAMESPACE, false)) {
    prefs.putUChar(LED_NVS_KEY, _pendingLedBright);
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[NVS] Saved LED brightness: %d\n", _pendingLedBright);
    #endif
  }
}

void NvsManager::savePadSensitivity() {
  Preferences prefs;
  if (prefs.begin(SENSITIVITY_NVS_NAMESPACE, false)) {
    prefs.putUChar(SENSITIVITY_NVS_KEY, _pendingPadSens);
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[NVS] Saved pad sensitivity: %d\n", _pendingPadSens);
    #endif
  }
}

void NvsManager::savePadOrder() {
  Preferences prefs;
  if (prefs.begin(NOTEMAP_NVS_NAMESPACE, false)) {
    NoteMapStore nms;
    nms.magic = EEPROM_MAGIC;
    nms.version = NOTEMAP_VERSION;
    nms.reserved = 0;
    memcpy(nms.noteMap, _pendingPadOrder, NUM_KEYS);
    prefs.putBytes(NOTEMAP_NVS_KEY, &nms, sizeof(NoteMapStore));
    prefs.end();
    #if DEBUG_SERIAL
    Serial.println("[NVS] Saved pad order.");
    #endif
  }
}

void NvsManager::saveControlPads() {
  // Scale pads + arp pads — written by ToolPadRoles, not by runtime code
  // Placeholder for future tool integration
  #if DEBUG_SERIAL
  Serial.println("[NVS] Control pads save (placeholder).");
  #endif
}
