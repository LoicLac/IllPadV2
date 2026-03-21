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
// run() — Edit settings: Profile, AT Rate, BLE, Clock, Transport, Double-Tap, Bargraph
// =================================================================

void ToolSettings::run() {
  if (!_ui) return;

  // Load current settings from NVS (or use defaults)
  SettingsStore wk = {EEPROM_MAGIC, SETTINGS_VERSION,
                      DEFAULT_BASELINE_PROFILE, AT_RATE_DEFAULT, DEFAULT_BLE_INTERVAL,
                      DEFAULT_CLOCK_MODE, DEFAULT_FOLLOW_TRANSPORT,
                      DOUBLE_TAP_MS_DEFAULT, LED_BARGRAPH_DURATION_DEFAULT};
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

  static const char* profileNames[]   = {"Adaptive", "Expressive", "Percussive"};
  static const char* bleNames[]       = {"Low Latency (7.5ms)", "Normal (15ms)", "Battery Saver (30ms)"};
  static const char* clockModeNames[] = {"Slave", "Master"};
  static const char* yesNoNames[]     = {"No", "Yes"};

  uint8_t selectedParam = 0;  // 0=none, 1-7
  bool screenDirty = true;

  _ui->vtClear();

  while (true) {
    char input = _ui->readInput();

    // Select parameter
    if (input >= '1' && input <= '7') {
      selectedParam = input - '0';
      screenDirty = true;
    }

    // +/- adjust selected param
    if (input == '+' || input == '=') {
      if (selectedParam == 1) {
        wk.baselineProfile = (wk.baselineProfile + 1) % NUM_BASELINE_PROFILES;
      } else if (selectedParam == 2) {
        if (wk.aftertouchRate < AT_RATE_MAX) wk.aftertouchRate += 5;
      } else if (selectedParam == 3) {
        wk.bleInterval = (wk.bleInterval + 1) % NUM_BLE_INTERVALS;
      } else if (selectedParam == 4) {
        wk.clockMode = (wk.clockMode + 1) % NUM_CLOCK_MODES;
      } else if (selectedParam == 5) {
        wk.followTransport = wk.followTransport ? 0 : 1;
      } else if (selectedParam == 6) {
        if (wk.doubleTapMs + 10 <= DOUBLE_TAP_MS_MAX) wk.doubleTapMs += 10;
      } else if (selectedParam == 7) {
        if (wk.potBarDurationMs + 500 <= LED_BARGRAPH_DURATION_MAX) wk.potBarDurationMs += 500;
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
      } else if (selectedParam == 4) {
        wk.clockMode = (wk.clockMode + NUM_CLOCK_MODES - 1) % NUM_CLOCK_MODES;
      } else if (selectedParam == 5) {
        wk.followTransport = wk.followTransport ? 0 : 1;
      } else if (selectedParam == 6) {
        if (wk.doubleTapMs >= DOUBLE_TAP_MS_MIN + 10) wk.doubleTapMs -= 10;
        else wk.doubleTapMs = DOUBLE_TAP_MS_MIN;
      } else if (selectedParam == 7) {
        if (wk.potBarDurationMs >= LED_BARGRAPH_DURATION_MIN + 500) wk.potBarDurationMs -= 500;
        else wk.potBarDurationMs = LED_BARGRAPH_DURATION_MIN;
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

      // Helper macro-like lambda for rendering a param line
      auto drawParam = [&](uint8_t num, const char* label, const char* value) {
        if (selectedParam == num) {
          Serial.printf("  " VT_CYAN VT_BOLD ">" VT_RESET " [%d] %-22s" VT_CYAN "%s" VT_RESET VT_CL "\n", num, label, value);
        } else {
          Serial.printf("    [%d] %-22s%s" VT_CL "\n", num, label, value);
        }
      };

      drawParam(1, "Baseline Profile:", profileNames[wk.baselineProfile]);

      char atBuf[16];
      snprintf(atBuf, sizeof(atBuf), "%d ms", wk.aftertouchRate);
      drawParam(2, "Aftertouch Rate:", atBuf);

      drawParam(3, "BLE Interval:", bleNames[wk.bleInterval]);
      drawParam(4, "Clock Mode:", clockModeNames[wk.clockMode]);
      drawParam(5, "Follow Transport:", yesNoNames[wk.followTransport ? 1 : 0]);

      char dtBuf[16];
      snprintf(dtBuf, sizeof(dtBuf), "%d ms", wk.doubleTapMs);
      drawParam(6, "Double-Tap Window:", dtBuf);

      char bgBuf[16];
      snprintf(bgBuf, sizeof(bgBuf), "%.1f s", wk.potBarDurationMs / 1000.0f);
      drawParam(7, "Bargraph Duration:", bgBuf);

      Serial.printf(VT_CL "\n");

      // Description box
      Serial.printf(VT_DIM "  ----------------------------------------" VT_RESET VT_CL "\n");
      if (selectedParam == 1) {
        Serial.printf(VT_DIM "    Controls MPR121 baseline adaptation." VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Adaptive = balanced (default)." VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Expressive = slower recovery, more dynamic." VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Percussive = fast recovery, tight response." VT_RESET VT_CL "\n");
      } else if (selectedParam == 2) {
        Serial.printf(VT_DIM "    Min interval between aftertouch messages" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    per pad (10-100ms). Lower = smoother but" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    more MIDI traffic. Default: 25ms (~40Hz)." VT_RESET VT_CL "\n");
      } else if (selectedParam == 3) {
        Serial.printf(VT_DIM "    Low Latency: 7.5ms (best response, more battery)" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Normal: 15ms (Apple compatible, default)" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Battery Saver: 30ms (saves battery, higher latency)" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Note: the host device may override this." VT_RESET VT_CL "\n");
      } else if (selectedParam == 4) {
        Serial.printf(VT_DIM "    Slave: sync to external clock (DAW)." VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Master: generate clock from pot tempo." VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Reboot required after change." VT_RESET VT_CL "\n");
      } else if (selectedParam == 5) {
        Serial.printf(VT_DIM "    Slave only. Yes: DAW Start/Stop/Continue" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    controls arps (Start=sync, Stop=silence," VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    Continue=resume). No: arps ignore DAW" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    transport. Clock ticks always received." VT_RESET VT_CL "\n");
      } else if (selectedParam == 6) {
        Serial.printf(VT_DIM "    Time window to detect double-tap on ARPEG" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    pads (HOLD mode). Lower = faster but harder" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    to trigger. Range: 100-250ms." VT_RESET VT_CL "\n");
      } else if (selectedParam == 7) {
        Serial.printf(VT_DIM "    How long the pot bargraph stays visible" VT_RESET VT_CL "\n");
        Serial.printf(VT_DIM "    after last pot movement. Range: 1-10s." VT_RESET VT_CL "\n");
      } else {
        Serial.printf(VT_DIM "    Select a parameter with [1-7]." VT_RESET VT_CL "\n");
      }
      Serial.printf(VT_DIM "  ----------------------------------------" VT_RESET VT_CL "\n");

      Serial.printf(VT_CL "\n");
      Serial.printf("    [1-7] Select   [+/-] Adjust   [ENTER] Save   [q] Back" VT_CL "\n");
      _ui->vtFrameEnd();
    }

    delay(10);
  }
}
