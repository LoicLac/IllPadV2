#include "ToolSettings.h"
#include "../core/CapacitiveKeyboard.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"
#include <Arduino.h>
#include <Preferences.h>

ToolSettings::ToolSettings()
  : _keyboard(nullptr), _nvs(nullptr), _ui(nullptr) {}

void ToolSettings::begin(CapacitiveKeyboard* keyboard, NvsManager* nvs, SetupUI* ui) {
  _keyboard = keyboard;
  _nvs = nvs;
  _ui = ui;
}

// =================================================================
// run() — Edit settings: Profile, AT Rate, BLE Interval
// =================================================================

void ToolSettings::run() {
  if (!_ui) return;

  // Load current settings from NVS (or use defaults)
  SettingsStore wk = {EEPROM_MAGIC, SETTINGS_VERSION,
                      DEFAULT_BASELINE_PROFILE, AT_RATE_DEFAULT, DEFAULT_BLE_INTERVAL};
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

  static const char* profileNames[] = {"Adaptive", "Expressive", "Percussive"};
  static const char* bleNames[]     = {"Low Latency (7.5ms)", "Normal (15ms)", "Battery Saver (30ms)"};

  uint8_t selectedParam = 0;  // 0=none, 1=profile, 2=atRate, 3=bleInterval
  bool screenDirty = true;

  _ui->vtClear();

  while (true) {
    char input = _ui->readInput();

    // Select parameter
    if (input == '1') { selectedParam = 1; screenDirty = true; }
    if (input == '2') { selectedParam = 2; screenDirty = true; }
    if (input == '3') { selectedParam = 3; screenDirty = true; }

    // +/- adjust selected param
    if (input == '+' || input == '=') {
      if (selectedParam == 1) {
        wk.baselineProfile = (wk.baselineProfile + 1) % NUM_BASELINE_PROFILES;
      } else if (selectedParam == 2) {
        if (wk.aftertouchRate < AT_RATE_MAX) wk.aftertouchRate += 5;
      } else if (selectedParam == 3) {
        wk.bleInterval = (wk.bleInterval + 1) % NUM_BLE_INTERVALS;
      }
      screenDirty = true;
    }
    if (input == '-' || input == '_') {
      if (selectedParam == 1) {
        wk.baselineProfile = (wk.baselineProfile + NUM_BASELINE_PROFILES - 1) % NUM_BASELINE_PROFILES;
      } else if (selectedParam == 2) {
        if (wk.aftertouchRate > AT_RATE_MIN) wk.aftertouchRate -= 5;
        if (wk.aftertouchRate < AT_RATE_MIN) wk.aftertouchRate = AT_RATE_MIN;
      } else if (selectedParam == 3) {
        wk.bleInterval = (wk.bleInterval + NUM_BLE_INTERVALS - 1) % NUM_BLE_INTERVALS;
      }
      screenDirty = true;
    }

    // Save [ENTER]
    if (input == '\r' || input == '\n') {
      wk.magic = EEPROM_MAGIC;
      wk.version = SETTINGS_VERSION;
      Preferences prefs;
      if (prefs.begin(SETTINGS_NVS_NAMESPACE, false)) {
        prefs.putBytes(SETTINGS_NVS_KEY, &wk, sizeof(SettingsStore));
        prefs.end();

        // Apply baseline profile immediately if keyboard available
        if (_keyboard) {
          _keyboard->setBaselineProfile(wk.baselineProfile);
        }

        _ui->showSaved();
        _ui->vtClear();
        Serial.printf(VT_GREEN "  Settings saved. Reboot to apply all changes." VT_RESET "\n");
        delay(800);
      } else {
        _ui->vtClear();
        Serial.printf(VT_RED "  NVS write failed!" VT_RESET "\n");
        delay(1500);
      }
      _ui->vtClear();
      return;
    }

    // Quit [q]
    if (input == 'q' || input == 'Q') {
      _ui->vtClear();
      return;
    }

    // Refresh display
    if (screenDirty) {
      screenDirty = false;

      _ui->vtFrameStart();
      _ui->drawHeader("SETTINGS", "");
      Serial.printf(VT_CL "\n");

      // Parameter 1: Baseline Profile
      if (selectedParam == 1) {
        Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " [1] Baseline Profile:  "
                      VT_CYAN "%s" VT_RESET VT_CL "\n", profileNames[wk.baselineProfile]);
      } else {
        Serial.printf("    [1] Baseline Profile:  %s" VT_CL "\n", profileNames[wk.baselineProfile]);
      }

      // Parameter 2: Aftertouch Rate
      if (selectedParam == 2) {
        Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " [2] Aftertouch Rate:   "
                      VT_CYAN "%d ms" VT_RESET VT_CL "\n", wk.aftertouchRate);
      } else {
        Serial.printf("    [2] Aftertouch Rate:   %d ms" VT_CL "\n", wk.aftertouchRate);
      }

      // Parameter 3: BLE Interval
      if (selectedParam == 3) {
        Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " [3] BLE Interval:      "
                      VT_CYAN "%s" VT_RESET VT_CL "\n", bleNames[wk.bleInterval]);
      } else {
        Serial.printf("    [3] BLE Interval:      %s" VT_CL "\n", bleNames[wk.bleInterval]);
      }

      Serial.printf(VT_CL "\n");

      // Description box
      if (selectedParam == 1) {
        Serial.printf(VT_DIM "  ----------------------------------------" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Controls MPR121 baseline adaptation." VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Adaptive = balanced (default)." VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Expressive = slower recovery, more dynamic." VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Percussive = fast recovery, tight response." VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "  ----------------------------------------" VT_RESET VT_CL "\n");
      } else if (selectedParam == 2) {
        Serial.printf(VT_DIM "  ----------------------------------------" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Min interval between aftertouch messages" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    per pad (10-100ms). Lower = smoother but" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    more MIDI traffic. Default: 25ms (~40Hz)." VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "  ----------------------------------------" VT_RESET VT_CL "\n");
      } else if (selectedParam == 3) {
        Serial.printf(VT_DIM "  ----------------------------------------" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Low Latency: 7.5ms (best response, more battery)" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Normal: 15ms (Apple compatible, default)" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Battery Saver: 30ms (saves battery, higher latency)" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Note: the host device may override this setting." VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "  ----------------------------------------" VT_RESET VT_CL "\n");
      }

      Serial.printf(VT_CL "\n");
      Serial.printf("    [1-3] Select   [+/-] Adjust   [ENTER] Save   [q] Back" VT_CL "\n");
      _ui->vtFrameEnd();
    }

    delay(10);
  }
}
