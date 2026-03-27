#include "ToolSettings.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"
#include "InputParser.h"
#include <Arduino.h>
#include <Preferences.h>

static const uint8_t NUM_PARAMS = 8;

static const char* s_profileNames[]   = {"Adaptive", "Expressive", "Percussive"};
static const char* s_bleNames[]       = {"Low Latency (7.5ms)", "Normal (15ms)", "Battery Saver (30ms)", "Off (USB only)"};
static const char* s_clockModeNames[] = {"Slave", "Master"};
static const char* s_yesNoNames[]     = {"No", "Yes"};

ToolSettings::ToolSettings()
  : _keyboard(nullptr), _leds(nullptr), _nvs(nullptr), _ui(nullptr) {}

void ToolSettings::begin(CapacitiveKeyboard* keyboard, LedController* leds, NvsManager* nvs, SetupUI* ui) {
  _keyboard = keyboard;
  _leds = leds;
  _nvs = nvs;
  _ui = ui;
}

// =================================================================
// adjustParam — apply directional step with clamping/wrapping
// =================================================================
void ToolSettings::adjustParam(SettingsStore& wk, uint8_t param, int dir, bool accelerated) {
  switch (param) {
    case 0: // Baseline Profile: cycle 0-2
      wk.baselineProfile = (wk.baselineProfile + NUM_BASELINE_PROFILES + dir) % NUM_BASELINE_PROFILES;
      break;
    case 1: { // Aftertouch Rate: step 5ms, range 10-100, accel x5
      int step = accelerated ? 25 : 5;
      int val = (int)wk.aftertouchRate + dir * step;
      if (val < AT_RATE_MIN) val = AT_RATE_MIN;
      if (val > AT_RATE_MAX) val = AT_RATE_MAX;
      wk.aftertouchRate = (uint8_t)val;
      break;
    }
    case 2: // BLE Interval: cycle 0-3
      wk.bleInterval = (wk.bleInterval + NUM_BLE_INTERVALS + dir) % NUM_BLE_INTERVALS;
      break;
    case 3: // Clock Mode: cycle 0-1
      wk.clockMode = (wk.clockMode + NUM_CLOCK_MODES + dir) % NUM_CLOCK_MODES;
      break;
    case 4: { // Double-Tap: step 10ms, range 100-250, accel x5
      int step = accelerated ? 50 : 10;
      int val = (int)wk.doubleTapMs + dir * step;
      if (val < DOUBLE_TAP_MS_MIN) val = DOUBLE_TAP_MS_MIN;
      if (val > DOUBLE_TAP_MS_MAX) val = DOUBLE_TAP_MS_MAX;
      wk.doubleTapMs = (uint16_t)val;
      break;
    }
    case 5: { // Bargraph Duration: step 500ms, range 1000-10000, accel x10
      int step = accelerated ? 5000 : 500;
      int val = (int)wk.potBarDurationMs + dir * step;
      if (val < (int)LED_BARGRAPH_DURATION_MIN) val = LED_BARGRAPH_DURATION_MIN;
      if (val > (int)LED_BARGRAPH_DURATION_MAX) val = LED_BARGRAPH_DURATION_MAX;
      wk.potBarDurationMs = (uint16_t)val;
      break;
    }
    case 6: // Panic on Reconnect: cycle 0-1
      wk.panicOnReconnect = wk.panicOnReconnect ? 0 : 1;
      break;
    case 7: // Battery Calibration: no left/right adjustment
      break;
  }
}

// =================================================================
// saveSettings — write to NVS, apply live where possible
// =================================================================
bool ToolSettings::saveSettings(const SettingsStore& wk) {
  SettingsStore toSave = wk;
  toSave.magic = EEPROM_MAGIC;
  toSave.version = SETTINGS_VERSION;
  Preferences prefs;
  if (!prefs.begin(SETTINGS_NVS_NAMESPACE, false)) return false;
  prefs.putBytes(SETTINGS_NVS_KEY, &toSave, sizeof(SettingsStore));
  prefs.end();

  // Apply baseline profile immediately if keyboard available
  if (_keyboard) {
    _keyboard->setBaselineProfile(toSave.baselineProfile);
  }
  return true;
}

// =================================================================
// drawDescription — show help text for the selected parameter
// =================================================================
void ToolSettings::drawDescription(uint8_t param) {
  Serial.printf(VT_DIM "  ----------------------------------------" VT_RESET VT_CL "\n");
  switch (param) {
    case 0:
      Serial.printf(VT_DIM "    Controls MPR121 baseline adaptation." VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    Adaptive = balanced (default)." VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    Expressive = slower recovery, more dynamic." VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    Percussive = fast recovery, tight response." VT_RESET VT_CL "\n");
      break;
    case 1:
      Serial.printf(VT_DIM "    Min interval between aftertouch messages" VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    per pad (10-100ms). Lower = smoother but" VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    more MIDI traffic. Default: 25ms (~40Hz)." VT_RESET VT_CL "\n");
      break;
    case 2:
      Serial.printf(VT_DIM "    Low Latency: 7.5ms (best response, more battery)" VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    Normal: 15ms (Apple compatible, default)" VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    Battery Saver: 30ms (saves battery, higher latency)" VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    Off: BLE disabled, USB only (saves RAM, faster boot)" VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    Reboot required after change." VT_RESET VT_CL "\n");
      break;
    case 3:
      Serial.printf(VT_DIM "    Slave: sync to external clock (DAW)." VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    Master: generate clock from pot tempo." VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    Reboot required after change." VT_RESET VT_CL "\n");
      break;
    case 4:
      Serial.printf(VT_DIM "    Time window to detect double-tap on ARPEG" VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    pads (HOLD mode). Lower = faster but harder" VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    to trigger. Range: 100-250ms." VT_RESET VT_CL "\n");
      break;
    case 5:
      Serial.printf(VT_DIM "    How long the pot bargraph stays visible" VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    after last pot movement. Range: 1-10s." VT_RESET VT_CL "\n");
      break;
    case 6:
      Serial.printf(VT_DIM "    Yes: send CC123 (All Notes Off) on all" VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    channels when BLE reconnects. Prevents" VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    stuck notes after connection drop." VT_RESET VT_CL "\n");
      break;
    case 7:
      Serial.printf(VT_DIM "    Calibrate battery ADC at full charge." VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    Plug in charger, wait for full, press" VT_RESET VT_CL "\n");
      Serial.printf(VT_DIM "    [Enter] to read and save the ADC value." VT_RESET VT_CL "\n");
      break;
  }
  Serial.printf(VT_DIM "  ----------------------------------------" VT_RESET VT_CL "\n");
}

// =================================================================
// run() — Unified arrow navigation with immediate save per param
// =================================================================

void ToolSettings::run() {
  if (!_ui) return;

  // Load current settings from NVS (or use defaults)
  SettingsStore wk = {EEPROM_MAGIC, SETTINGS_VERSION,
                      DEFAULT_BASELINE_PROFILE, AT_RATE_DEFAULT, DEFAULT_BLE_INTERVAL,
                      DEFAULT_CLOCK_MODE,
                      DOUBLE_TAP_MS_DEFAULT, LED_BARGRAPH_DURATION_DEFAULT,
                      DEFAULT_PANIC_ON_RECONNECT, DEFAULT_BAT_ADC_AT_FULL};
  {
    Preferences prefs;
    if (prefs.begin(SETTINGS_NVS_NAMESPACE, true)) {
      size_t len = prefs.getBytesLength(SETTINGS_NVS_KEY);
      if (len == sizeof(SettingsStore)) {
        SettingsStore tmp;
        prefs.getBytes(SETTINGS_NVS_KEY, &tmp, sizeof(SettingsStore));
        if (tmp.magic == EEPROM_MAGIC && tmp.version == SETTINGS_VERSION) {
          wk = tmp;
        }
      }
      prefs.end();
    }
  }

  // Save original for reboot-required change detection
  SettingsStore original = wk;
  bool needsReboot = false;

  InputParser input;
  uint8_t cursor = 0;
  bool editing = false;
  bool screenDirty = true;
  bool confirmDefaults = false;

  _ui->vtClear();

  while (true) {
    if (_leds) _leds->update();

    NavEvent ev = input.update();

    // --- Defaults confirmation sub-mode ---
    if (confirmDefaults) {
      if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
        wk = {EEPROM_MAGIC, SETTINGS_VERSION,
              DEFAULT_BASELINE_PROFILE, AT_RATE_DEFAULT, DEFAULT_BLE_INTERVAL,
              DEFAULT_CLOCK_MODE,
              DOUBLE_TAP_MS_DEFAULT, LED_BARGRAPH_DURATION_DEFAULT,
              DEFAULT_PANIC_ON_RECONNECT, DEFAULT_BAT_ADC_AT_FULL};
        if (saveSettings(wk)) {
          original = wk;
          _ui->showSaved();
        }
        confirmDefaults = false;
        screenDirty = true;
      } else if (ev.type != NAV_NONE) {
        confirmDefaults = false;
        screenDirty = true;
      }
      delay(5);
      continue;
    }

    // --- Main navigation ---
    if (ev.type == NAV_QUIT) {
      if (editing) {
        // Cancel edit — revert working copy to last saved state
        wk = original;
        editing = false;
        screenDirty = true;
      } else {
        _ui->vtClear();
        return;
      }
    }

    if (ev.type == NAV_DEFAULTS && !editing) {
      confirmDefaults = true;
      screenDirty = true;
    }

    if (!editing) {
      if (ev.type == NAV_UP) {
        if (cursor > 0) cursor--;
        else cursor = NUM_PARAMS - 1;
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        if (cursor < NUM_PARAMS - 1) cursor++;
        else cursor = 0;
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        editing = true;
        screenDirty = true;
      }
    } else {
      // Editing mode
      if (ev.type == NAV_LEFT) {
        adjustParam(wk, cursor, -1, ev.accelerated);
        screenDirty = true;
      } else if (ev.type == NAV_RIGHT) {
        adjustParam(wk, cursor, +1, ev.accelerated);
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        // Battery cal: read ADC now instead of adjusting
        if (cursor == 7) {
          wk.batAdcAtFull = (uint16_t)analogRead(BAT_ADC_PIN);
        }
        // Save on confirm
        if (saveSettings(wk)) {
          editing = false;
          _ui->showSaved();

          if (wk.bleInterval != original.bleInterval ||
              wk.clockMode != original.clockMode) {
            needsReboot = true;
          }
          original = wk;
          screenDirty = true;
        } else {
          // NVS write failed — stay in edit mode, show inline error
          Serial.printf("\r\n" VT_RED "  NVS write failed!" VT_RESET);
          delay(1500);
          screenDirty = true;
        }
      }
    }

    // --- Render ---
    if (screenDirty) {
      screenDirty = false;

      _ui->vtFrameStart();
      _ui->drawHeader("SETTINGS", "");
      Serial.printf(VT_CL "\n");

      // Helper lambda for rendering a param line
      auto drawParam = [&](uint8_t idx, const char* label, const char* value) {
        bool selected = (cursor == idx);
        bool isEditing = selected && editing;
        if (selected) {
          if (isEditing) {
            Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " %-24s" VT_CYAN "[%s]" VT_RESET VT_CL "\n", label, value);
          } else {
            Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " %-24s" VT_CYAN "%s" VT_RESET VT_CL "\n", label, value);
          }
        } else {
          Serial.printf("    %-24s%s" VT_CL "\n", label, value);
        }
      };

      drawParam(0, "Baseline Profile:", s_profileNames[wk.baselineProfile]);

      char atBuf[24];
      snprintf(atBuf, sizeof(atBuf), "%d ms  (%d-%d)", wk.aftertouchRate, AT_RATE_MIN, AT_RATE_MAX);
      drawParam(1, "Aftertouch Rate:", atBuf);

      drawParam(2, "BLE Interval:", s_bleNames[wk.bleInterval]);
      drawParam(3, "Clock Mode:", s_clockModeNames[wk.clockMode]);

      char dtBuf[24];
      snprintf(dtBuf, sizeof(dtBuf), "%d ms  (%d-%d)", wk.doubleTapMs, DOUBLE_TAP_MS_MIN, DOUBLE_TAP_MS_MAX);
      drawParam(4, "Double-Tap Window:", dtBuf);

      char bgBuf[24];
      snprintf(bgBuf, sizeof(bgBuf), "%.1f s  (%.1f-%.1f)",
               wk.potBarDurationMs / 1000.0f,
               LED_BARGRAPH_DURATION_MIN / 1000.0f,
               LED_BARGRAPH_DURATION_MAX / 1000.0f);
      drawParam(5, "Bargraph Duration:", bgBuf);
      drawParam(6, "Panic on Reconnect:", s_yesNoNames[wk.panicOnReconnect ? 1 : 0]);

      char batBuf[32];
      if (wk.batAdcAtFull > 0) {
        snprintf(batBuf, sizeof(batBuf), "Calibrated (%d)", wk.batAdcAtFull);
      } else {
        snprintf(batBuf, sizeof(batBuf), "Not calibrated");
      }
      drawParam(7, "Battery Cal:", batBuf);

      Serial.printf(VT_CL "\n");

      // Description box
      drawDescription(cursor);

      Serial.printf(VT_CL "\n");

      if (confirmDefaults) {
        Serial.printf(VT_YELLOW "  Reset to defaults? (y/n)" VT_RESET VT_CL "\n");
      } else if (editing) {
        Serial.printf(VT_DIM "  [Left/Right] change value  [Enter] confirm & save" VT_RESET VT_CL "\n");
      } else {
        Serial.printf(VT_DIM "  [Up/Down] navigate  [Enter] edit  [d] defaults  [q] quit" VT_RESET VT_CL "\n");
      }

      if (needsReboot) {
        Serial.printf(VT_YELLOW "  * Reboot required for BLE/Clock changes." VT_RESET VT_CL "\n");
      }

      _ui->vtFrameEnd();
    }

    delay(5);
  }
}
