#include "NvsManager.h"
#include "PotRouter.h"
#include "../core/PotFilter.h"
#include "../midi/GrooveTemplates.h"
#include <Preferences.h>
#include <Arduino.h>
#include <string.h>

// =================================================================
// Constructor
// =================================================================
NvsManager::NvsManager()
  : _taskHandle(nullptr)
  , _bankDirty(false)
  , _potDirty(false)
  , _tempoDirty(false)
  , _ledBrightDirty(false)
  , _padSensDirty(false)
  , _padOrderDirty(false)
  , _anyDirty(false)
  , _pendingBank(DEFAULT_BANK)
  , _pendingLedBright(128)
  , _pendingPadSens(PAD_SENSITIVITY_DEFAULT)
  , _pendingTempo(TEMPO_BPM_DEFAULT)
  , _pendingResponseShape(RESPONSE_SHAPE_DEFAULT)
  , _pendingSlewRate(SLEW_RATE_DEFAULT)
  , _pendingAtDeadzone(AT_DEADZONE_DEFAULT)
  , _potRightLastChangeMs(0)
  , _potRearLastChangeMs(0)
  , _potRightPendingSave(false)
  , _potRearPendingSave(false)
  , _anyPadPressed(false)
  // Phase 2 init (cf spec §10.1)
  , _settingsDirty(false)
  , _bankTypeDirty(false)
  , _settingsLastChangeMs(0)
  , _bankTypeLastChangeMs(0)
  , _settingsPendingSave(false)
  , _bankTypePendingSave(false)
{
  // BankTypeStore v3 + v4 : ARPEG_GEN per-bank params.
  // v3 defaults : bonus_pile=1.5 (=15), margin=7.
  // v4 defaults : proximity_factor=0.4 (=4), ecart=5.
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _loadedBonusPile[i]  = 15;
    _loadedMarginWalk[i] = 7;
    _loadedProximity[i]  = 4;
    _loadedEcart[i]      = 5;
  }
  // Phase 2 : default BankType cache (overridden by loadAll()).
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _loadedBankType[i] = BANK_NORMAL;
  }
  // Default LED settings v6 (0-100 perceptual %)
  // See docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md
  _ledSettings.magic = EEPROM_MAGIC;
  _ledSettings.version = LED_SETTINGS_VERSION;
  _ledSettings.reserved = 0;
  // Intensities v9 : unified FG (any bank type/state) + breathing depth
  // Defaults tuned on hardware 2026-05-16 (post-flash dump session).
  _ledSettings.fgIntensity = 100;
  _ledSettings.breathDepth = 50;
  _ledSettings.tickFlashFg = 100;
  _ledSettings.tickFlashBg = 40;
  // Global background factor — BG = FG × bgFactor / 100
  _ledSettings.bgFactor = 18;
  // Timing
  _ledSettings.pulsePeriodMs = 1472;
  _ledSettings.tickBeatDurationMs = 50;
  _ledSettings.tickBarDurationMs  = 80;
  _ledSettings.tickWrapDurationMs = 120;
  _ledSettings.gammaTenths = 17;  // gamma 1.7 (tuned HW)
  // SPARK params (Phase 0.1 tuning : shorter + more cycles)
  _ledSettings.sparkOnMs = 20;
  _ledSettings.sparkGapMs = 40;
  _ledSettings.sparkCycles = 4;
  // Confirmations (Phase 0.1 tuning : snappier bank/scale/octave)
  _ledSettings.bankBlinks = 3;
  _ledSettings.bankDurationMs = 150;
  _ledSettings.bankBrightnessPct = 80;
  _ledSettings.scaleRootBlinks = 2;
  _ledSettings.scaleRootDurationMs = 130;
  _ledSettings.scaleModeBlinks = 2;
  _ledSettings.scaleModeDurationMs = 130;
  _ledSettings.scaleChromBlinks = 2;
  _ledSettings.scaleChromDurationMs = 130;
  _ledSettings.holdOnFadeMs = 500;
  _ledSettings.holdOffFadeMs = 500;
  _ledSettings.octaveBlinks = 3;
  _ledSettings.octaveDurationMs = 130;
  // Event overrides : all PTN_NONE by default -> fallback on EVENT_RENDER_DEFAULT
  for (uint8_t i = 0; i < EVT_COUNT; i++) {
    _ledSettings.eventOverrides[i].patternId = PTN_NONE;
    _ledSettings.eventOverrides[i].colorSlot = 0;
    _ledSettings.eventOverrides[i].fgPct    = 0;
  }

  // Default color slots
  _colorSlots.magic = COLOR_SLOT_MAGIC;
  _colorSlots.version = COLOR_SLOT_VERSION;
  _colorSlots.reserved = 0;
  // v4 defaults — 15 slots, aligned with LedController init body and LED spec §11.
  // Preset indices refer to COLOR_PRESET_NAMES[] :
  //   0 Pure White, 1 Warm White, 2 Cool White, 3 Ice Blue, 4 Deep Blue,
  //   5 Cyan, 6 Amber, 7 Gold, 8 Coral, 9 Violet, 10 Magenta, 11 Green,
  //   12 Soft Peach, 13 Mint.
  // Tuned defaults 2026-05-16 (HW dump): MODE_ARPEG=Deep Blue (no W),
  // VERB_OVERDUB=Magenta (distinct from REC), BANK_SWITCH hue -6, OCTAVE=Magenta.
  static const uint8_t defaultPresets[COLOR_SLOT_COUNT] = {
    /* CSLOT_MODE_NORMAL      */ 1,   // Warm White
    /* CSLOT_MODE_ARPEG       */ 4,   // Deep Blue (was Ice Blue — tuned HW)
    /* CSLOT_MODE_LOOP        */ 7,   // Gold
    /* CSLOT_VERB_PLAY        */ 11,  // Green
    /* CSLOT_VERB_REC         */ 8,   // Coral
    /* CSLOT_VERB_OVERDUB     */ 10,  // Magenta (was Amber — tuned HW)
    /* CSLOT_VERB_CLEAR_LOOP  */ 5,   // Cyan
    /* CSLOT_VERB_SLOT_CLEAR  */ 6,   // Amber (with hue offset, see below)
    /* CSLOT_VERB_SAVE        */ 10,  // Magenta
    /* CSLOT_BANK_SWITCH      */ 0,   // Pure White
    /* CSLOT_SCALE_ROOT       */ 6,   // Amber
    /* CSLOT_SCALE_MODE       */ 7,   // Gold
    /* CSLOT_SCALE_CHROM      */ 8,   // Coral
    /* CSLOT_OCTAVE           */ 10,  // Magenta (was Violet — tuned HW)
    /* CSLOT_CONFIRM_OK       */ 0,   // Pure White (SPARK universal)
    /* CSLOT_VERB_STOP        */ 8,   // Coral (Phase 0.1 — Stop fade-out)
  };
  static const int8_t defaultHueOffsets[COLOR_SLOT_COUNT] = {
    /* MODE_NORMAL      */ 0,
    /* MODE_ARPEG       */ 0,
    /* MODE_LOOP        */ 0,
    /* VERB_PLAY        */ 0,
    /* VERB_REC         */ 0,
    /* VERB_OVERDUB     */ 0,
    /* VERB_CLEAR_LOOP  */ 0,
    /* VERB_SLOT_CLEAR  */ 20,  // hue-shifted orange, distinct from VERB_OVERDUB (LED spec §11)
    /* VERB_SAVE        */ 0,
    /* BANK_SWITCH      */ -6,  // subtle warming tweak (tuned HW)
    /* SCALE_ROOT       */ 0,
    /* SCALE_MODE       */ 0,
    /* SCALE_CHROM      */ 0,
    /* OCTAVE           */ 0,
    /* CONFIRM_OK       */ 0,
    /* VERB_STOP        */ 0,
  };
  for (uint8_t i = 0; i < COLOR_SLOT_COUNT; i++) {
    _colorSlots.slots[i].presetId  = defaultPresets[i];
    _colorSlots.slots[i].hueOffset = defaultHueOffsets[i];
  }

  // Default control pads (empty) + V2 DSP defaults.
  // I1 : with all DSP params at 0, V2 degenerates to V1 behavior until the
  // user saves a blob. Seed meaningful defaults so fresh NVS already gets
  // the pipeline (mild low-pass, short S&H window, ~50ms fade-out).
  memset(&_ctrlStore, 0, sizeof(_ctrlStore));
  _ctrlStore.magic        = CONTROLPAD_MAGIC;
  _ctrlStore.version      = CONTROLPAD_VERSION;
  _ctrlStore.smoothMs     = 10;
  _ctrlStore.sampleHoldMs = 15;
  _ctrlStore.releaseMs    = 50;

  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _scaleDirty[i] = false;
    _velocityDirty[i] = false;
    _pitchBendDirty[i] = false;
    _arpPotDirty[i] = false;
    _pendingScale[i] = {true, 2, 0};  // chromatic, root C, Ionian
    _pendingBaseVel[i] = DEFAULT_BASE_VELOCITY;
    _pendingVelVar[i] = DEFAULT_VELOCITY_VARIATION;
    _pendingPitchBend[i] = DEFAULT_PITCH_BEND_OFFSET;
    _pendingArpPot[i] = {ARPPOT_MAGIC, ARPPOT_VERSION, 0, 2048, 0, DIV_1_8, ARP_UP, 1, 0};  // hdr + gate, shuffle, div, pat, oct, tmpl
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
  Serial.println("[BOOT NVS] Task created (Core 1, priority 1).");
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
// tickDebounce — 3-group debounce (pot rear 2s / pot right 10s / Phase 2 500ms).
// Phase 2 checks placed in head — see spec §10.5 for the early-return rationale.
// =================================================================
void NvsManager::tickDebounce(uint32_t now, bool rearDirty, bool rightDirty,
                               const PotRouter& potRouter,
                               uint8_t currentBank, BankType currentType) {
  // -------------------------------------------------------------------
  // Phase 2 : Settings + BankType debounce (500ms).
  // PLACEMENT CRITIQUE — en tête, AVANT le right-pot early return ligne 258.
  // Sinon ces checks ne seraient exécutés que 10s après un mouvement de
  // pot droit → NVS save settings/banktype silencieusement cassé.
  // Cf spec §10.5.
  // -------------------------------------------------------------------
  if (_settingsPendingSave && (now - _settingsLastChangeMs) >= 500) {
    _settingsDirty = true;
    _anyDirty = true;
    _settingsPendingSave = false;
  }
  if (_bankTypePendingSave && (now - _bankTypeLastChangeMs) >= 500) {
    _bankTypeDirty = true;
    _anyDirty = true;
    _bankTypePendingSave = false;
  }

  // -------------------------------------------------------------------
  // Rear pot (tempo, LED brightness, pad sensitivity) — 2 s debounce.
  // These are global params adjusted by intentional gesture (LEFT held,
  // REAR held, or rear-pot tweak), not twiddled continuously. Short
  // debounce so a quick reboot after the gesture preserves the change.
  // -------------------------------------------------------------------
  if (rearDirty) {
    _potRearLastChangeMs = now;
    _potRearPendingSave = true;
  }
  if (_potRearPendingSave && (now - _potRearLastChangeMs >= POT_REAR_NVS_SAVE_DEBOUNCE_MS)) {
    _potRearPendingSave = false;

    // Tempo (global)
    uint16_t newTempo = potRouter.getTempoBPM();
    if (newTempo != _pendingTempo) {
      _pendingTempo = newTempo;
      _tempoDirty = true;
      _anyDirty = true;
    }

    // LED brightness (global)
    uint8_t newBright = potRouter.getLedBrightness();
    if (newBright != _pendingLedBright) {
      _pendingLedBright = newBright;
      _ledBrightDirty = true;
      _anyDirty = true;
    }

    // Pad sensitivity (global)
    uint8_t newSens = potRouter.getPadSensitivity();
    if (newSens != _pendingPadSens) {
      _pendingPadSens = newSens;
      _padSensDirty = true;
      _anyDirty = true;
    }
  }

  // -------------------------------------------------------------------
  // Right pots (R1-R4) — 10 s debounce.
  // Shape/slew/deadzone (global pot params), per-bank velocity,
  // pitch bend, and arp pot params (gate/shuffle/division/pattern/tpl).
  // -------------------------------------------------------------------
  if (rightDirty) {
    _potRightLastChangeMs = now;
    _potRightPendingSave = true;
  }
  if (!_potRightPendingSave || (now - _potRightLastChangeMs < POT_NVS_SAVE_DEBOUNCE_MS)) {
    return;
  }
  _potRightPendingSave = false;

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

  // === Per-bank: arp pot params (ARPEG / ARPEG_GEN sauvegardent les memes champs) ===
  if (isArpType(currentType)) {
    ArpPotStore newArp;
    newArp.magic       = ARPPOT_MAGIC;
    newArp.version     = ARPPOT_VERSION;
    newArp.reserved    = 0;
    newArp.gateRaw     = (uint16_t)(potRouter.getGateLength() * 4095.0f);
    newArp.shuffleDepthRaw = (uint16_t)(potRouter.getShuffleDepth() * 4095.0f);
    newArp.division    = (uint8_t)potRouter.getDivision();
    newArp.pattern     = (uint8_t)potRouter.getPattern();
    newArp.octaveRange = _pendingArpPot[currentBank].octaveRange;  // Preserve pad-set value
    newArp.shuffleTemplate = potRouter.getShuffleTemplate();

    const ArpPotStore& cur = _pendingArpPot[currentBank];
    if (newArp.gateRaw != cur.gateRaw || newArp.shuffleDepthRaw != cur.shuffleDepthRaw
        || newArp.division != cur.division || newArp.pattern != cur.pattern
        || newArp.shuffleTemplate != cur.shuffleTemplate) {
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

void NvsManager::queueArpPotWrite(uint8_t bankIdx, float gate, float shuffleDepth,
                                    ArpDivision div, ArpPattern pat, uint8_t octave,
                                    uint8_t shuffleTmpl) {
  if (bankIdx >= NUM_BANKS) return;
  _pendingArpPot[bankIdx].gateRaw          = (uint16_t)(gate * 4095.0f);
  _pendingArpPot[bankIdx].shuffleDepthRaw  = (uint16_t)(shuffleDepth * 4095.0f);
  _pendingArpPot[bankIdx].division         = (uint8_t)div;
  _pendingArpPot[bankIdx].pattern          = (uint8_t)pat;
  _pendingArpPot[bankIdx].octaveRange      = octave;
  _pendingArpPot[bankIdx].shuffleTemplate  = shuffleTmpl;
  _arpPotDirty[bankIdx] = true;
  _anyDirty = true;
}

void NvsManager::queueArpOctaveWrite(uint8_t bankIdx, uint8_t octave) {
  if (bankIdx >= NUM_BANKS) return;
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

void NvsManager::queueSettingsWrite(const SettingsStore& settings) {
  // Phase 2 : copy snapshot, arm debounce timer.
  // _settingsDirty NOT set here — tickDebounce will set it after 500ms of
  // inactivity so commitAll() doesn't fire prematurely (stream slider viewer).
  _pendingSettings = settings;
  _settingsLastChangeMs = millis();
  _settingsPendingSave = true;
}

void NvsManager::queueBankTypeFromCache() {
  // Phase 2 : arm debounce timer only. The actual blob is reconstructed
  // from _loadedX[] arrays at save time (cf saveBankType). Caller must
  // have updated setLoadedBonusPile / setLoadedMarginWalk / etc. before
  // calling this.
  _bankTypeLastChangeMs = millis();
  _bankTypePendingSave = true;
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

  if (_potDirty)         { savePotParams();     _potDirty = false; }
  if (_tempoDirty)       { saveTempo();         _tempoDirty = false; }
  if (_ledBrightDirty)   { saveLedBrightness(); _ledBrightDirty = false; }
  if (_padSensDirty)     { savePadSensitivity(); _padSensDirty = false; }
  if (_padOrderDirty)    { savePadOrder();       _padOrderDirty = false; }
  // Phase 2 : viewer-driven NVS writes (debounced 500ms via tickDebounce).
  if (_settingsDirty)    { saveSettings();      _settingsDirty = false; }
  if (_bankTypeDirty)    { saveBankType();      _bankTypeDirty = false; }
}

// =================================================================
// Static NVS helpers — usable without NvsManager instance
// =================================================================

namespace {
// Shared implementation for loadBlob and checkBlob — no code duplication.
// If out == nullptr, validates only (checkBlob). If out != nullptr, copies data (loadBlob).
bool readAndValidateBlob(const char* ns, const char* key,
                          uint16_t expectedMagic, uint8_t expectedVersion,
                          void* out, size_t expectedSize) {
  if (expectedSize > NVS_BLOB_MAX_SIZE) {
    #if DEBUG_SERIAL
    Serial.printf("[NVS] %s/%s: size %d exceeds max %d\n", ns, key, (int)expectedSize, (int)NVS_BLOB_MAX_SIZE);
    #endif
    return false;
  }
  Preferences prefs;
  if (!prefs.begin(ns, true)) {
    #if DEBUG_SERIAL
    Serial.printf("[NVS] %s/%s: namespace open failed\n", ns, key);
    #endif
    return false;
  }
  bool ok = false;
  size_t len = prefs.getBytesLength(key);
  if (len == expectedSize) {
    uint8_t tmp[NVS_BLOB_MAX_SIZE];
    prefs.getBytes(key, tmp, expectedSize);
    uint16_t magic;
    memcpy(&magic, tmp, sizeof(uint16_t));
    uint8_t version = tmp[2];
    if (magic == expectedMagic && version == expectedVersion) {
      if (out) memcpy(out, tmp, expectedSize);
      ok = true;
    }
    #if DEBUG_SERIAL
    else {
      if (magic != expectedMagic)
        Serial.printf("[NVS] %s/%s: magic 0x%04X != expected 0x%04X\n", ns, key, magic, expectedMagic);
      else
        Serial.printf("[NVS] %s/%s: version %d != expected %d\n", ns, key, version, expectedVersion);
    }
    #endif
  }
  #if DEBUG_SERIAL
  else if (len > 0) {
    Serial.printf("[NVS] %s/%s: size %d != expected %d\n", ns, key, (int)len, (int)expectedSize);
  }
  // len == 0 = key not found, silent (normal on first boot)
  #endif
  prefs.end();
  return ok;
}
} // anonymous namespace

bool NvsManager::loadBlob(const char* ns, const char* key,
                           uint16_t expectedMagic, uint8_t expectedVersion,
                           void* out, size_t expectedSize) {
  return readAndValidateBlob(ns, key, expectedMagic, expectedVersion, out, expectedSize);
}

bool NvsManager::checkBlob(const char* ns, const char* key,
                            uint16_t expectedMagic, uint8_t expectedVersion,
                            size_t expectedSize) {
  return readAndValidateBlob(ns, key, expectedMagic, expectedVersion, nullptr, expectedSize);
}

bool NvsManager::saveBlob(const char* ns, const char* key,
                           const void* data, size_t size) {
  #if DEBUG_SERIAL
  if (size >= 2) {
    uint16_t magic;
    memcpy(&magic, data, sizeof(uint16_t));
    if (magic == 0) Serial.printf("[NVS] WARNING: %s/%s saving blob with magic=0!\n", ns, key);
  }
  #endif
  Preferences prefs;
  if (!prefs.begin(ns, false)) {
    #if DEBUG_SERIAL
    Serial.printf("[NVS] %s/%s: namespace open for write failed\n", ns, key);
    #endif
    return false;
  }
  size_t written = prefs.putBytes(key, data, size);
  prefs.end();
  #if DEBUG_SERIAL
  if (written != size)
    Serial.printf("[NVS] %s/%s: write failed (%d/%d bytes)\n", ns, key, (int)written, (int)size);
  #endif
  return (written == size);
}

// =================================================================
// loadAll — blocking reads at boot
// =================================================================
void NvsManager::loadAll(BankSlot* banks, uint8_t& currentBank,
                          uint8_t* padOrder, uint8_t* bankPads,
                          uint8_t* rootPads, uint8_t* modePads,
                          uint8_t& chromaticPad, uint8_t& holdPad,
                          uint8_t* octavePads,
                          PotRouter& potRouter, SettingsStore& settings) {
  Preferences prefs;

  // --- Current bank ---
  if (prefs.begin(BANK_NVS_NAMESPACE, true)) {
    currentBank = prefs.getUChar(BANK_NVS_KEY, DEFAULT_BANK);
    if (currentBank >= NUM_BANKS) currentBank = DEFAULT_BANK;
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[BOOT NVS] Bank loaded: %d\n", currentBank);
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
        // Clamp to valid range — corrupt NVS would cause OOB in ScaleResolver
        if (banks[i].scale.root > 6) banks[i].scale.root = 2;  // default C
        if (banks[i].scale.mode > 6) banks[i].scale.mode = 0;  // default Ionian
        #if DEBUG_SERIAL
        Serial.printf("[BOOT NVS] Scale bank %d: chrom=%d root=%d mode=%d\n",
                      i, banks[i].scale.chromatic, banks[i].scale.root, banks[i].scale.mode);
        #endif
      }
    }
    prefs.end();
  }

  // --- Bank types + quantize modes + scale groups + ARPEG_GEN params (v3 + v4) ---
  memset(_loadedQuantize, DEFAULT_ARP_START_MODE, NUM_BANKS);
  memset(_loadedScaleGroup, 0, NUM_BANKS);
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _loadedBonusPile[i]  = 15;
    _loadedMarginWalk[i] = 7;
    _loadedProximity[i]  = 4;
    _loadedEcart[i]      = 5;
  }
  {
    BankTypeStore bts;
    if (NvsManager::loadBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2,
                              EEPROM_MAGIC, BANKTYPE_VERSION, &bts, sizeof(bts))) {
      validateBankTypeStore(bts);
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        banks[i].type        = (BankType)bts.types[i];
        _loadedBankType[i]   = bts.types[i];
        _loadedQuantize[i]   = bts.quantize[i];
        _loadedScaleGroup[i] = bts.scaleGroup[i];
        _loadedBonusPile[i]  = bts.bonusPilex10[i];
        _loadedMarginWalk[i] = bts.marginWalk[i];
        _loadedProximity[i]  = bts.proximityFactorx10[i];
        _loadedEcart[i]      = bts.ecart[i];
      }
      #if DEBUG_SERIAL
      Serial.println("[BOOT NVS] Bank types + quantize + scale groups + ARPEG_GEN params loaded (v4 store).");
      #endif
    } else {
      // Premier boot / NVS vierge / v3->v4 : defaults usine = 4 NORMAL + 4 ARPEG,
      // group A sur banques 1,2 (NORMAL) et 5,6 (ARPEG). Identique au reset 'd' de Tool 4.
      // ARPEG_GEN params : bonusPilex10=15, marginWalk=7, proximity=4 (=0.4), ecart=5.
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        banks[i].type        = (i < 4) ? BANK_NORMAL : BANK_ARPEG;
        _loadedBankType[i]   = (i < 4) ? BANK_NORMAL : BANK_ARPEG;
        _loadedQuantize[i]   = DEFAULT_ARP_START_MODE;
        _loadedScaleGroup[i] = (i == 0 || i == 1 || i == 4 || i == 5) ? 1 : 0;
        _loadedBonusPile[i]  = 15;
        _loadedMarginWalk[i] = 7;
        _loadedProximity[i]  = 4;
        _loadedEcart[i]      = 5;
      }
      #if DEBUG_SERIAL
      Serial.println("[BOOT NVS] BankTypeStore absent/invalide (v3->v4 reset attendu) - defaults usine appliques.");
      #endif
    }
  }

  // --- Scale group propagation (leader wins) ---
  // Le premier membre de chaque groupe fait foi. Evite l'incoherence si des blobs
  // scale ont derive avant l'assignation d'un groupe. Pas de queueScaleWrite au boot :
  // la propagation live realignera NVS au premier changement.
  for (uint8_t g = 1; g <= NUM_SCALE_GROUPS; g++) {
    int8_t leader = -1;
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (_loadedScaleGroup[i] != g) continue;
      if (leader < 0) {
        leader = i;
      } else {
        banks[i].scale = banks[leader].scale;
        #if DEBUG_SERIAL
        Serial.printf("[BOOT NVS] Group %c: bank %d adopte scale de bank %d\n",
                      'A' + g - 1, i, leader);
        #endif
      }
    }
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
    Serial.println("[BOOT NVS] Velocity params loaded.");
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
    Serial.println("[BOOT NVS] Pitch bend offsets loaded.");
    #endif
  }

  // --- Arp pot params per bank (v1 = header + 8B payload, post pattern reduction) ---
  if (prefs.begin(ARP_POT_NVS_NAMESPACE, true)) {
    bool anyReset = false;
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      char key[8];
      snprintf(key, sizeof(key), "arp_%d", i);
      size_t len = prefs.getBytesLength(key);
      ArpPotStore tmp;
      if (len == sizeof(ArpPotStore)) {
        prefs.getBytes(key, &tmp, sizeof(ArpPotStore));
        if (tmp.magic == ARPPOT_MAGIC && tmp.version == ARPPOT_VERSION) {
          _pendingArpPot[i] = tmp;
          validateArpPotStore(_pendingArpPot[i]);
        } else {
          // Magic/version mismatch -> defaults compile-time deja en place via constructor.
          anyReset = true;
        }
      } else if (len > 0) {
        // Size mismatch (typically v0 raw 8B post-reduction) -> defaults compile-time.
        anyReset = true;
      }
    }
    prefs.end();
    if (anyReset) {
      #if DEBUG_SERIAL
      Serial.println("[BOOT NVS] ArpPotStore raw/v0 detecte - reset v1 applique (defaults compile-time). User doit re-regler gate/shuffle/division/oct/template.");
      #endif
    }
    #if DEBUG_SERIAL
    Serial.println("[BOOT NVS] Arp pot params loaded (v1).");
    #endif
  }

  // --- Tempo ---
  if (prefs.begin(TEMPO_NVS_NAMESPACE, true)) {
    _pendingTempo = prefs.getUShort(TEMPO_NVS_KEY, TEMPO_BPM_DEFAULT);
    // Defensive clamp (B-CODE-4 fix): NVS-stored tempo could be 0 or out of
    // range. Without this, ClockManager::setInternalBPM(0) → _pllTickInterval
    // = inf → cast to uint32_t is UB. The downstream interval==0 check at
    // generateTicks() L185 mitigates the consequence but the cast is undefined.
    if (_pendingTempo < TEMPO_BPM_MIN || _pendingTempo > TEMPO_BPM_MAX)
      _pendingTempo = TEMPO_BPM_DEFAULT;
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[BOOT NVS] Tempo: %d BPM\n", _pendingTempo);
    #endif
  }

  // --- Global pot params (shape, slew, deadzone) ---
  {
    PotParamsStore pps;
    if (loadBlob(POT_PARAMS_NVS_NAMESPACE, POT_PARAMS_NVS_KEY,
                           EEPROM_MAGIC, POT_PARAMS_VERSION, &pps, sizeof(pps))) {
      _pendingResponseShape = (float)pps.responseShapeRaw / 4095.0f;
      _pendingSlewRate = pps.slewRate;
      _pendingAtDeadzone = pps.atDeadzone;
      #if DEBUG_SERIAL
      Serial.printf("[BOOT NVS] Pot params: shape=%.2f slew=%d dz=%d\n",
                    _pendingResponseShape, _pendingSlewRate, _pendingAtDeadzone);
      #endif
    }
  }

  // --- LED brightness ---
  if (prefs.begin(LED_NVS_NAMESPACE, true)) {
    _pendingLedBright = prefs.getUChar(LED_NVS_KEY, 128);
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[BOOT NVS] LED brightness: %d\n", _pendingLedBright);
    #endif
  }

  // --- Pad sensitivity ---
  if (prefs.begin(SENSITIVITY_NVS_NAMESPACE, true)) {
    _pendingPadSens = prefs.getUChar(SENSITIVITY_NVS_KEY, PAD_SENSITIVITY_DEFAULT);
    // Defensive clamp (B-CODE-5 fix): CapacitiveKeyboard::setPadSensitivity()
    // clamps in aval, but PotRouter::seedCatchValues() reads it BEFORE that
    // clamp can apply (computes (val - MIN) / (MAX - MIN) which wraps if val
    // is out of range), producing a wrong catch storedValue at boot.
    if (_pendingPadSens < PAD_SENSITIVITY_MIN || _pendingPadSens > PAD_SENSITIVITY_MAX)
      _pendingPadSens = PAD_SENSITIVITY_DEFAULT;
    prefs.end();
    #if DEBUG_SERIAL
    Serial.printf("[BOOT NVS] Pad sensitivity: %d\n", _pendingPadSens);
    #endif
  }

  // --- Pad order ---
  {
    NoteMapStore nms;
    if (loadBlob(NOTEMAP_NVS_NAMESPACE, NOTEMAP_NVS_KEY,
                           EEPROM_MAGIC, NOTEMAP_VERSION, &nms, sizeof(nms))) {
      validateNoteMapStore(nms);
      memcpy(padOrder, nms.noteMap, NUM_KEYS);
      #if DEBUG_SERIAL
      Serial.println("[BOOT NVS] Pad order loaded.");
      #endif
    }
  }

  // --- Bank pads ---
  {
    BankPadStore bps;
    if (loadBlob(BANKPAD_NVS_NAMESPACE, BANKPAD_NVS_KEY,
                           EEPROM_MAGIC, BANKPAD_VERSION, &bps, sizeof(bps))) {
      memcpy(bankPads, bps.bankPads, NUM_BANKS);
      for (uint8_t j = 0; j < NUM_BANKS; j++) {
        if (bankPads[j] >= NUM_KEYS) bankPads[j] = j;
      }
      #if DEBUG_SERIAL
      Serial.println("[BOOT NVS] Bank pads loaded.");
      #endif
    }
  }

  // --- Scale control pads ---
  {
    ScalePadStore sps;
    if (NvsManager::loadBlob(SCALE_PAD_NVS_NAMESPACE, SCALEPAD_NVS_KEY,
                              EEPROM_MAGIC, SCALEPAD_VERSION, &sps, sizeof(sps))) {
      validateScalePadStore(sps);
      memcpy(rootPads, sps.rootPads, 7);
      memcpy(modePads, sps.modePads, 7);
      chromaticPad = sps.chromaticPad;
      #if DEBUG_SERIAL
      Serial.println("[BOOT NVS] Scale pads loaded (v2 store).");
      #endif
    }
  }

  // --- Arp control pads ---
  {
    ArpPadStore aps;
    if (NvsManager::loadBlob(ARP_PAD_NVS_NAMESPACE, ARPPAD_NVS_KEY,
                              EEPROM_MAGIC, ARPPAD_VERSION, &aps, sizeof(aps))) {
      validateArpPadStore(aps);
      holdPad     = aps.holdPad;
      memcpy(octavePads, aps.octavePads, 4);
      #if DEBUG_SERIAL
      Serial.printf("[BOOT NVS] Arp pads loaded (v2 store): hold=%d oct=%d,%d,%d,%d\n",
                    holdPad, octavePads[0], octavePads[1],
                    octavePads[2], octavePads[3]);
      #endif
    }
  }

  // --- Settings (profile, AT rate, BLE interval, clock, double-tap, bargraph, panic, bat cal, LOOP timers) ---
  settings = {EEPROM_MAGIC, SETTINGS_VERSION, DEFAULT_BASELINE_PROFILE, AT_RATE_DEFAULT,
              DEFAULT_BLE_INTERVAL, DEFAULT_CLOCK_MODE,
              DOUBLE_TAP_MS_DEFAULT, LED_BARGRAPH_DURATION_DEFAULT, DEFAULT_PANIC_ON_RECONNECT,
              0, DEFAULT_BAT_ADC_AT_FULL,
              500, 1000, 800};  // clearLoopTimerMs, slotSaveTimerMs, slotClearTimerMs
  if (loadBlob(SETTINGS_NVS_NAMESPACE, SETTINGS_NVS_KEY,
                     EEPROM_MAGIC, SETTINGS_VERSION, &settings, sizeof(settings))) {
    validateSettingsStore(settings);
  }
  #if DEBUG_SERIAL
  Serial.printf("[BOOT NVS] Settings: profile=%d, atRate=%d, bleInt=%d, clock=%d, dblTap=%d\n",
                settings.baselineProfile, settings.aftertouchRate, settings.bleInterval,
                settings.clockMode, settings.doubleTapMs);
  #endif

  // --- LED settings (Tool 7) ---
  // B-CODE-3 fix: validate after load. Without this, a NVS blob with valid
  // magic+version but corrupt blink fields (e.g. 0) would cause runtime
  // division by zero in LedController::renderConfirmation/renderNormalDisplay
  // for scaleRoot/scaleMode/scaleChrom/octave blink unitMs. The bankBlinks/
  // playBlinks/stopBlinks have ad-hoc fallbacks in LedController::loadLedSettings
  // but the others do not.
  if (loadBlob(LED_SETTINGS_NVS_NAMESPACE, LED_SETTINGS_NVS_KEY,
               EEPROM_MAGIC, LED_SETTINGS_VERSION, &_ledSettings, sizeof(_ledSettings))) {
    validateLedSettingsStore(_ledSettings);
  }
  // ColorSlotStore has no validate function — defensive validation deferred.
  loadBlob(LED_SETTINGS_NVS_NAMESPACE, COLOR_SLOT_NVS_KEY,
                     COLOR_SLOT_MAGIC, COLOR_SLOT_VERSION, &_colorSlots, sizeof(_colorSlots));
  #if DEBUG_SERIAL
  Serial.println("[BOOT NVS] LED settings + color slots loaded.");
  #endif

  // --- Control pads (Tool 4) — single blob in illpad_ctrl / pads ---
  // I1 : seed sensible V2 DSP defaults pre-load. If loadBlob succeeds, it
  // overwrites them with persisted values ; if it fails (fresh NVS / wrong
  // version), these survive and V2 behaves as intended instead of V1.
  memset(&_ctrlStore, 0, sizeof(_ctrlStore));
  _ctrlStore.magic        = CONTROLPAD_MAGIC;
  _ctrlStore.version      = CONTROLPAD_VERSION;
  _ctrlStore.smoothMs     = 10;
  _ctrlStore.sampleHoldMs = 15;
  _ctrlStore.releaseMs    = 50;
  if (NvsManager::loadBlob(CONTROLPAD_NVS_NAMESPACE, CONTROLPAD_NVS_KEY,
                           CONTROLPAD_MAGIC, CONTROLPAD_VERSION,
                           &_ctrlStore, sizeof(_ctrlStore))) {
    validateControlPadStore(_ctrlStore);
#if DEBUG_SERIAL
    Serial.printf("[BOOT NVS] loaded %u control pad(s)\n", (unsigned)_ctrlStore.count);
#endif
  } else {
#if DEBUG_SERIAL
    Serial.println("[BOOT NVS] control pads: defaults (empty)");
#endif
    _ctrlStore.count = 0;
  }

  // --- Pot mapping (user-configurable pot assignments) ---
  {
    PotMappingStore pms;
    if (loadBlob(POTMAP_NVS_NAMESPACE, POTMAP_NVS_KEY,
                           EEPROM_MAGIC, POTMAP_VERSION, &pms, sizeof(pms))) {
      potRouter.loadMapping(pms);
      #if DEBUG_SERIAL
      Serial.println("[BOOT NVS] Pot mapping loaded.");
      #endif
    }
  }

  // --- Pot filter config ---
  {
    PotFilterStore pfs;
    if (loadBlob(POTFILTER_NVS_NAMESPACE, POTFILTER_NVS_KEY,
                 EEPROM_MAGIC, POT_FILTER_VERSION, &pfs, sizeof(pfs))) {
      validatePotFilterStore(pfs);
      PotFilter::setConfig(pfs);
      #if DEBUG_SERIAL
      Serial.println("[BOOT NVS] Pot filter config loaded.");
      #endif
    }
    // else: PotFilter uses built-in defaults (set in begin())
  }

  // --- Apply loaded values to PotRouter (BEFORE PotRouter::begin()) ---
  // This ensures catch seeds reflect NVS-saved values, not constructor defaults.
  // Without this, every reboot would lose tempo, shape, gate, etc.
  potRouter.loadStoredGlobals(_pendingResponseShape, _pendingSlewRate,
                               _pendingAtDeadzone, _pendingTempo,
                               _pendingLedBright, _pendingPadSens);

  // Per-bank params: load for the current bank (the one that will be foreground).
  // arp.pattern is dual-semantic — for ARPEG_GEN it carries _genPosition, for ARPEG it carries
  // ArpPattern enum. PotRouter holds both fields so we route correctly.
  const ArpPotStore& arp = _pendingArpPot[currentBank];
  bool isCurGen = (banks[currentBank].type == BANK_ARPEG_GEN);
  ArpPattern  patForRouter    = isCurGen ? ARP_UP : (ArpPattern)arp.pattern;
  uint8_t     genPosForRouter = isCurGen ? arp.pattern : 5;
  if (genPosForRouter >= NUM_GEN_POSITIONS) genPosForRouter = NUM_GEN_POSITIONS - 1;
  potRouter.loadStoredPerBank(
    _pendingBaseVel[currentBank], _pendingVelVar[currentBank],
    _pendingPitchBend[currentBank],
    max(0.005f, (float)arp.gateRaw / 4095.0f),
    (float)arp.shuffleDepthRaw / 4095.0f,
    (ArpDivision)arp.division, patForRouter,
    arp.shuffleTemplate, genPosForRouter
  );

  #if DEBUG_SERIAL
  Serial.printf("[BOOT NVS] PotRouter loaded: tempo=%d shape=%.2f slew=%d dz=%d bright=%d sens=%d\n",
                _pendingTempo, _pendingResponseShape, _pendingSlewRate,
                _pendingAtDeadzone, _pendingLedBright, _pendingPadSens);
  #endif
}

uint8_t NvsManager::getLoadedQuantizeMode(uint8_t bank) const {
  if (bank >= NUM_BANKS) return DEFAULT_ARP_START_MODE;
  return _loadedQuantize[bank];
}

void NvsManager::setLoadedQuantizeMode(uint8_t bank, uint8_t mode) {
  if (bank < NUM_BANKS) _loadedQuantize[bank] = mode;
}

uint8_t NvsManager::getLoadedScaleGroup(uint8_t bank) const {
  if (bank >= NUM_BANKS) return 0;
  return _loadedScaleGroup[bank];
}

void NvsManager::setLoadedScaleGroup(uint8_t bank, uint8_t group) {
  if (bank < NUM_BANKS && group <= NUM_SCALE_GROUPS) _loadedScaleGroup[bank] = group;
}

uint8_t NvsManager::getLoadedBankType(uint8_t bank) const {
  if (bank >= NUM_BANKS) return BANK_NORMAL;
  return _loadedBankType[bank];
}

void NvsManager::setLoadedBankType(uint8_t bank, uint8_t type) {
  if (bank < NUM_BANKS && type <= BANK_ARPEG_GEN) _loadedBankType[bank] = type;
}

uint8_t NvsManager::getLoadedBonusPile(uint8_t bank) const {
  if (bank >= NUM_BANKS) return 15;
  return _loadedBonusPile[bank];
}

void NvsManager::setLoadedBonusPile(uint8_t bank, uint8_t x10) {
  if (bank < NUM_BANKS && x10 >= 10 && x10 <= 20) _loadedBonusPile[bank] = x10;
}

uint8_t NvsManager::getLoadedMarginWalk(uint8_t bank) const {
  if (bank >= NUM_BANKS) return 7;
  return _loadedMarginWalk[bank];
}

void NvsManager::setLoadedMarginWalk(uint8_t bank, uint8_t margin) {
  if (bank < NUM_BANKS && margin >= 3 && margin <= 12) _loadedMarginWalk[bank] = margin;
}

uint8_t NvsManager::getLoadedProximityFactor(uint8_t bank) const {
  if (bank >= NUM_BANKS) return 4;
  return _loadedProximity[bank];
}

void NvsManager::setLoadedProximityFactor(uint8_t bank, uint8_t x10) {
  if (bank < NUM_BANKS && x10 >= 4 && x10 <= 20) _loadedProximity[bank] = x10;
}

uint8_t NvsManager::getLoadedEcart(uint8_t bank) const {
  if (bank >= NUM_BANKS) return 5;
  return _loadedEcart[bank];
}

void NvsManager::setLoadedEcart(uint8_t bank, uint8_t ecart) {
  if (bank < NUM_BANKS && ecart >= 1 && ecart <= 12) _loadedEcart[bank] = ecart;
}

const ArpPotStore& NvsManager::getLoadedArpParams(uint8_t bankIdx) const {
  static const ArpPotStore defaultArp = {ARPPOT_MAGIC, ARPPOT_VERSION, 0, 2048, 0, DIV_1_8, ARP_UP, 1, 0};
  if (bankIdx >= NUM_BANKS) return defaultArp;
  return _pendingArpPot[bankIdx];
}

const LedSettingsStore& NvsManager::getLoadedLedSettings() const {
  return _ledSettings;
}

const ColorSlotStore& NvsManager::getLoadedColorSlots() const {
  return _colorSlots;
}

const ControlPadStore& NvsManager::getLoadedControlPadStore() const {
  return _ctrlStore;
}

// =================================================================
// Save methods — each opens/closes its own Preferences namespace
// =================================================================

void NvsManager::saveBank() {
  Preferences prefs;
  if (prefs.begin(BANK_NVS_NAMESPACE, false)) {
    prefs.putUChar(BANK_NVS_KEY, _pendingBank);
    prefs.end();
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
  }
}

void NvsManager::saveTempo() {
  Preferences prefs;
  if (prefs.begin(TEMPO_NVS_NAMESPACE, false)) {
    prefs.putUShort(TEMPO_NVS_KEY, _pendingTempo);
    prefs.end();
  }
}

void NvsManager::saveLedBrightness() {
  Preferences prefs;
  if (prefs.begin(LED_NVS_NAMESPACE, false)) {
    prefs.putUChar(LED_NVS_KEY, _pendingLedBright);
    prefs.end();
  }
}

void NvsManager::savePadSensitivity() {
  Preferences prefs;
  if (prefs.begin(SENSITIVITY_NVS_NAMESPACE, false)) {
    prefs.putUChar(SENSITIVITY_NVS_KEY, _pendingPadSens);
    prefs.end();
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

// =================================================================
// Phase 2 — Settings + BankType saves (viewer-driven, NVS task)
// =================================================================
// Both called from commitAll() on the NVS background task (Core 1).
// _anyPadPressed guard (commitAll head) defers writes during live play.

void NvsManager::saveSettings() {
  // Direct saveBlob — NVS task is background, so the blocking flash write
  // does not stall Core 1 main loop. Pattern matches existing savePotParams.
  NvsManager::saveBlob(SETTINGS_NVS_NAMESPACE, SETTINGS_NVS_KEY,
                       &_pendingSettings, sizeof(_pendingSettings));
  #if DEBUG_SERIAL
  Serial.println("[NVS] Saved settings.");
  #endif
}

void NvsManager::saveBankType() {
  // Rebuild BankTypeStore from internal _loadedX[] arrays at save time
  // (single source of truth, no _pendingBankType duplicating state).
  // _loadedBankType[i] populated by loadAll() + maintained by Phase 2 path
  // (currently no runtime BankType change — palier rouge cf spec §17).
  BankTypeStore bts;
  bts.magic    = EEPROM_MAGIC;
  bts.version  = BANKTYPE_VERSION;
  bts.reserved = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    bts.types[i]              = _loadedBankType[i];
    bts.quantize[i]           = _loadedQuantize[i];
    bts.scaleGroup[i]         = _loadedScaleGroup[i];
    bts.bonusPilex10[i]       = _loadedBonusPile[i];
    bts.marginWalk[i]         = _loadedMarginWalk[i];
    bts.proximityFactorx10[i] = _loadedProximity[i];
    bts.ecart[i]              = _loadedEcart[i];
  }
  validateBankTypeStore(bts);
  NvsManager::saveBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2,
                       &bts, sizeof(bts));
  #if DEBUG_SERIAL
  Serial.println("[NVS] Saved bank type.");
  #endif
}
