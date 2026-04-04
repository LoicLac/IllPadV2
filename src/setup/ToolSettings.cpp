#include "ToolSettings.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../core/PotFilter.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"
#include "InputParser.h"
#include <Arduino.h>
#include <Preferences.h>

static const uint8_t NUM_PARAMS = 8;

static const char* s_profileNames[]   = {"Adaptive", "Expressive", "Percussive"};
static const char* s_bleNames[]       = {"ON " "\xe2\x94\x80" " Low Latency (7.5ms)", "ON " "\xe2\x94\x80" " Normal (15ms)", "ON " "\xe2\x94\x80" " Battery Saver (30ms)", "OFF (USB only)"};
static const char* s_clockModeNames[] = {"Slave", "Master"};
static const char* s_yesNoNames[]     = {"No", "Yes"};

// Category boundaries (param index ranges)
static const uint8_t CAT_PERF_START = 0;
static const uint8_t CAT_PERF_END   = 2;   // 0,1
static const uint8_t CAT_CONN_START = 2;
static const uint8_t CAT_CONN_END   = 4;   // 2,3
static const uint8_t CAT_TIME_START = 4;
static const uint8_t CAT_TIME_END   = 6;   // 4,5
static const uint8_t CAT_SAFE_START = 6;
static const uint8_t CAT_SAFE_END   = 8;   // 6,7

ToolSettings::ToolSettings()
  : _keyboard(nullptr), _leds(nullptr), _ui(nullptr) {}

void ToolSettings::begin(CapacitiveKeyboard* keyboard, LedController* leds, SetupUI* ui) {
  _keyboard = keyboard;
  _leds = leds;
  _ui = ui;
}

// =================================================================
// adjustParam
// =================================================================
void ToolSettings::adjustParam(SettingsStore& wk, uint8_t param, int dir, bool accelerated) {
  switch (param) {
    case 0:
      wk.baselineProfile = (wk.baselineProfile + NUM_BASELINE_PROFILES + dir) % NUM_BASELINE_PROFILES;
      break;
    case 1: {
      int step = accelerated ? 25 : 5;
      int val = (int)wk.aftertouchRate + dir * step;
      if (val < AT_RATE_MIN) val = AT_RATE_MIN;
      if (val > AT_RATE_MAX) val = AT_RATE_MAX;
      wk.aftertouchRate = (uint8_t)val;
      break;
    }
    case 2:
      wk.bleInterval = (wk.bleInterval + NUM_BLE_INTERVALS + dir) % NUM_BLE_INTERVALS;
      break;
    case 3:
      wk.clockMode = (wk.clockMode + NUM_CLOCK_MODES + dir) % NUM_CLOCK_MODES;
      break;
    case 4: {
      int step = accelerated ? 50 : 10;
      int val = (int)wk.doubleTapMs + dir * step;
      if (val < DOUBLE_TAP_MS_MIN) val = DOUBLE_TAP_MS_MIN;
      if (val > DOUBLE_TAP_MS_MAX) val = DOUBLE_TAP_MS_MAX;
      wk.doubleTapMs = (uint8_t)val;
      break;
    }
    case 5: {
      int step = accelerated ? 5000 : 500;
      int val = (int)wk.potBarDurationMs + dir * step;
      if (val < (int)LED_BARGRAPH_DURATION_MIN) val = LED_BARGRAPH_DURATION_MIN;
      if (val > (int)LED_BARGRAPH_DURATION_MAX) val = LED_BARGRAPH_DURATION_MAX;
      wk.potBarDurationMs = (uint16_t)val;
      break;
    }
    case 6:
      wk.panicOnReconnect = wk.panicOnReconnect ? 0 : 1;
      break;
    case 7:
      break;
  }
}

// =================================================================
// seedPotForEdit — seed pot with mode/range matching each param
// =================================================================
void ToolSettings::seedPotForEdit(const SettingsStore& wk, uint8_t param) {
  switch (param) {
    case 0:  // Profile (3 choices)
      _potEditVal = wk.baselineProfile;
      _pots.seed(0, &_potEditVal, 0, NUM_BASELINE_PROFILES - 1, POT_RELATIVE, 6);
      break;
    case 1:  // AT rate (continuous 10-100)
      _potEditVal = wk.aftertouchRate;
      _pots.seed(0, &_potEditVal, AT_RATE_MIN, AT_RATE_MAX, POT_ABSOLUTE);
      break;
    case 2:  // BLE interval (4 choices)
      _potEditVal = wk.bleInterval;
      _pots.seed(0, &_potEditVal, 0, NUM_BLE_INTERVALS - 1, POT_RELATIVE, 8);
      break;
    case 3:  // Clock mode (2 choices)
      _potEditVal = wk.clockMode;
      _pots.seed(0, &_potEditVal, 0, NUM_CLOCK_MODES - 1, POT_RELATIVE, 4);
      break;
    case 4:  // Double-tap (continuous 100-250)
      _potEditVal = wk.doubleTapMs;
      _pots.seed(0, &_potEditVal, DOUBLE_TAP_MS_MIN, DOUBLE_TAP_MS_MAX, POT_ABSOLUTE);
      break;
    case 5:  // Bargraph duration (continuous)
      _potEditVal = wk.potBarDurationMs;
      _pots.seed(0, &_potEditVal, LED_BARGRAPH_DURATION_MIN, LED_BARGRAPH_DURATION_MAX, POT_ABSOLUTE);
      break;
    case 6:  // Panic on reconnect (toggle)
      _potEditVal = wk.panicOnReconnect;
      _pots.seed(0, &_potEditVal, 0, 1, POT_RELATIVE, 4);
      break;
    case 7:  // Battery cal (read-only)
      _pots.disable(0);
      break;
  }
}

// =================================================================
// applyPotEdit — copy pot value back into wk for current param
// =================================================================
void ToolSettings::applyPotEdit(SettingsStore& wk, uint8_t param) {
  switch (param) {
    case 0: wk.baselineProfile  = (uint8_t)_potEditVal; break;
    case 1: wk.aftertouchRate   = (uint8_t)_potEditVal; break;
    case 2: wk.bleInterval      = (uint8_t)_potEditVal; break;
    case 3: wk.clockMode        = (uint8_t)_potEditVal; break;
    case 4: wk.doubleTapMs      = (uint8_t)_potEditVal; break;
    case 5: wk.potBarDurationMs = (uint16_t)_potEditVal; break;
    case 6: wk.panicOnReconnect = (uint8_t)_potEditVal; break;
    default: break;
  }
}

// =================================================================
// saveSettings
// =================================================================
bool ToolSettings::saveSettings(const SettingsStore& wk) {
  SettingsStore toSave = wk;
  toSave.magic = EEPROM_MAGIC;
  toSave.version = SETTINGS_VERSION;
  // PIEGE: SettingsStore byte 3 is baselineProfile, NOT reserved. Do NOT zero it.
  // The static_assert in KeyboardData.h defends this layout.
  if (!NvsManager::saveBlob(SETTINGS_NVS_NAMESPACE, SETTINGS_NVS_KEY, &toSave, sizeof(toSave)))
    return false;

  if (_keyboard) _keyboard->setBaselineProfile(toSave.baselineProfile);
  return true;
}

// =================================================================
// drawDescription — expanded per-parameter info
// =================================================================
void ToolSettings::drawDescription(uint8_t param) {
  switch (param) {
    case 0:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Baseline Profile" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Controls MPR121 capacitive baseline tracking algorithm." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Adaptive: balanced drift compensation. Good for most uses (default)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Expressive: slower recovery after release. Wider dynamic range for" VT_RESET);
      _ui->drawFrameLine(VT_DIM "  sustained pressure gestures. More susceptible to drift." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Percussive: fast recovery. Tight staccato response, minimal aftertouch." VT_RESET);
      break;
    case 1:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Aftertouch Rate" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Min interval between poly-aftertouch MIDI messages per pad." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Lower = smoother expression but higher MIDI bandwidth." VT_RESET);
      _ui->drawFrameLine(VT_DIM "At 25ms (default): ~40 AT msg/sec/pad. 10 fingers = ~400 msg/sec total." VT_RESET);
      _ui->drawFrameLine(VT_DIM "USB handles this easily. BLE may drop some at very low rates." VT_RESET);
      break;
    case 2:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "BLE Interval" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Connection interval negotiated with host. Lower = less latency." VT_RESET);
      _ui->drawFrameLine(VT_DIM "7.5ms: best for live perf, ~2x battery. 15ms: Apple-compatible default." VT_RESET);
      _ui->drawFrameLine(VT_DIM "30ms: touring/battery mode. Off: BLE disabled, saves ~40KB RAM." VT_RESET);
      break;
    case 3:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Clock Mode" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Slave: sync arp tempo to incoming MIDI clock (0xF8) from DAW." VT_RESET);
      _ui->drawFrameLine(VT_DIM "USB clock has priority over BLE. PLL smooths BLE jitter." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Master: generate clock from pot tempo (10-260 BPM), broadcast." VT_RESET);
      break;
    case 4:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Double-Tap Window" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Time window for detecting double-tap on ARPEG pads in HOLD ON mode." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Double-tap removes a note from the arp pile." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Lower = faster double-tap but harder to trigger. Range: 100-250ms." VT_RESET);
      _ui->drawFrameLine(VT_DIM "If you miss double-taps, increase this value." VT_RESET);
      break;
    case 5:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Bargraph Duration" VT_RESET);
      _ui->drawFrameLine(VT_DIM "How long the LED pot bargraph stays visible after last pot movement." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Shows target value + physical pot position + catch state." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Longer = more time to read. Shorter = less visual interruption." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Range: 1.0-10.0 seconds. Default: 3.0s." VT_RESET);
      break;
    case 6:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Panic on Reconnect" VT_RESET);
      _ui->drawFrameLine(VT_DIM "When BLE reconnects after a connection drop, send CC#123" VT_RESET);
      _ui->drawFrameLine(VT_DIM "(All Notes Off) on all 8 channels. Prevents stuck notes" VT_RESET);
      _ui->drawFrameLine(VT_DIM "that survive a BLE dropout. Recommended: Yes for live use." VT_RESET);
      break;
    case 7:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Battery Calibration" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Stores the ADC reading at 100%% charge (full LiPo, charger connected)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Used to calculate accurate battery percentage display." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Calibrate once after hardware assembly. Plug charger, wait for" VT_RESET);
      _ui->drawFrameLine(VT_DIM "full charge LED, then press [RET] to sample current ADC value." VT_RESET);
      break;
  }
}

// =================================================================
// run() — Settings with named category sections
// =================================================================

void ToolSettings::run() {
  if (!_ui) return;

  SettingsStore wk = {EEPROM_MAGIC, SETTINGS_VERSION,
                      DEFAULT_BASELINE_PROFILE, AT_RATE_DEFAULT, DEFAULT_BLE_INTERVAL,
                      DEFAULT_CLOCK_MODE,
                      DOUBLE_TAP_MS_DEFAULT, LED_BARGRAPH_DURATION_DEFAULT,
                      DEFAULT_PANIC_ON_RECONNECT, 0, DEFAULT_BAT_ADC_AT_FULL};
  bool loadedFromNvs = NvsManager::loadBlob(SETTINGS_NVS_NAMESPACE, SETTINGS_NVS_KEY,
                                             EEPROM_MAGIC, SETTINGS_VERSION, &wk, sizeof(wk));
  if (loadedFromNvs) {
    validateSettingsStore(wk);
  }

  Serial.print(ITERM_RESIZE);

  SettingsStore original = wk;
  bool nvsSaved = loadedFromNvs;

  InputParser input;
  uint8_t cursor = 0;
  bool editing = false;
  bool screenDirty = true;
  bool confirmDefaults = false;

  // Seed pot for param navigation
  _potCursorIdx = cursor;
  _pots.seed(0, &_potCursorIdx, 0, NUM_PARAMS - 1, POT_RELATIVE, 16);

  _ui->vtClear();

  while (true) {
    PotFilter::updateAll();
    _pots.update();
    if (_leds) _leds->update();

    // --- Pot navigation ---
    if (!confirmDefaults) {
      if (!editing && _pots.getMove(0)) {
        cursor = (uint8_t)_potCursorIdx;
        screenDirty = true;
      } else if (editing && _pots.getMove(0)) {
        applyPotEdit(wk, cursor);
        screenDirty = true;
      }
    }

    NavEvent ev = input.update();

    // --- Defaults confirmation ---
    if (confirmDefaults) {
      if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
        wk = {EEPROM_MAGIC, SETTINGS_VERSION,
              DEFAULT_BASELINE_PROFILE, AT_RATE_DEFAULT, DEFAULT_BLE_INTERVAL,
              DEFAULT_CLOCK_MODE,
              DOUBLE_TAP_MS_DEFAULT, LED_BARGRAPH_DURATION_DEFAULT,
              DEFAULT_PANIC_ON_RECONNECT, 0, DEFAULT_BAT_ADC_AT_FULL};
        if (saveSettings(wk)) {
          original = wk;
          nvsSaved = true;
          _ui->flashSaved();
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
        wk = original;
        editing = false;
        // Re-seed pot for nav mode
        _potCursorIdx = cursor;
        _pots.seed(0, &_potCursorIdx, 0, NUM_PARAMS - 1, POT_RELATIVE, 16);
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
        _potCursorIdx = cursor;  // Sync pot target
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        if (cursor < NUM_PARAMS - 1) cursor++;
        else cursor = 0;
        _potCursorIdx = cursor;  // Sync pot target
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        editing = true;
        seedPotForEdit(wk, cursor);
        screenDirty = true;
      }
    } else {
      if (ev.type == NAV_LEFT) {
        adjustParam(wk, cursor, -1, ev.accelerated);
        // Sync pot target from arrow change
        seedPotForEdit(wk, cursor);
        screenDirty = true;
      } else if (ev.type == NAV_RIGHT) {
        adjustParam(wk, cursor, +1, ev.accelerated);
        // Sync pot target from arrow change
        seedPotForEdit(wk, cursor);
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        if (cursor == 7) {
          wk.batAdcAtFull = (uint16_t)analogRead(BAT_ADC_PIN);
        }
        if (saveSettings(wk)) {
          editing = false;
          nvsSaved = true;
          _ui->flashSaved();
          original = wk;
          // Re-seed pot for nav mode
          _potCursorIdx = cursor;
          _pots.seed(0, &_potCursorIdx, 0, NUM_PARAMS - 1, POT_RELATIVE, 16);
          screenDirty = true;
        }
      }
    }

    // --- Render ---
    if (screenDirty) {
      screenDirty = false;

      _ui->vtFrameStart();
      _ui->drawConsoleHeader("TOOL 5: SETTINGS", nvsSaved);
      _ui->drawFrameEmpty();

      // Helper for param line
      auto drawParam = [&](uint8_t idx, const char* label, const char* value) {
        bool selected = (cursor == idx);
        bool isEditing = selected && editing;
        if (isEditing) {
          _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-26s" VT_CYAN "[%s]" VT_RESET, label, value);
        } else if (selected) {
          _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-26s" VT_BRIGHT_WHITE "%s" VT_RESET, label, value);
        } else {
          _ui->drawFrameLine("  %-26s%s", label, value);
        }
      };

      // --- PERFORMANCE ---
      _ui->drawSection("PERFORMANCE");
      drawParam(0, "Baseline Profile:", s_profileNames[wk.baselineProfile]);
      {
        char atBuf[24];
        snprintf(atBuf, sizeof(atBuf), "%d ms  (%d-%d)", wk.aftertouchRate, AT_RATE_MIN, AT_RATE_MAX);
        drawParam(1, "Aftertouch Rate:", atBuf);
      }
      _ui->drawFrameEmpty();

      // --- CONNECTIVITY ---
      _ui->drawSection("CONNECTIVITY");
      drawParam(2, "BLE:", s_bleNames[wk.bleInterval]);
      drawParam(3, "Clock Mode:", s_clockModeNames[wk.clockMode]);
      _ui->drawFrameEmpty();

      // --- TIMING ---
      _ui->drawSection("TIMING");
      {
        char dtBuf[24];
        snprintf(dtBuf, sizeof(dtBuf), "%d ms  (%d-%d)", wk.doubleTapMs, DOUBLE_TAP_MS_MIN, DOUBLE_TAP_MS_MAX);
        drawParam(4, "Double-Tap Window:", dtBuf);
      }
      {
        char bgBuf[24];
        snprintf(bgBuf, sizeof(bgBuf), "%.1f s  (%.1f-%.1f)",
                 wk.potBarDurationMs / 1000.0f,
                 LED_BARGRAPH_DURATION_MIN / 1000.0f,
                 LED_BARGRAPH_DURATION_MAX / 1000.0f);
        drawParam(5, "Bargraph Duration:", bgBuf);
      }
      _ui->drawFrameEmpty();

      // --- SAFETY ---
      _ui->drawSection("SAFETY");
      drawParam(6, "Panic on Reconnect:", s_yesNoNames[wk.panicOnReconnect ? 1 : 0]);
      {
        char batBuf[32];
        if (wk.batAdcAtFull > 0) {
          snprintf(batBuf, sizeof(batBuf), "Calibrated (%d)", wk.batAdcAtFull);
        } else {
          snprintf(batBuf, sizeof(batBuf), "Not calibrated");
        }
        drawParam(7, "Battery Cal:", batBuf);
      }
      _ui->drawFrameEmpty();

      // --- INFO ---
      _ui->drawSection("INFO");

      if (confirmDefaults) {
        _ui->drawFrameLine(VT_YELLOW "Reset ALL settings to factory defaults? (y/n)" VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
      } else {
        drawDescription(cursor);
      }

      _ui->drawFrameEmpty();

      // Control bar
      if (confirmDefaults) {
        _ui->drawControlBar(VT_DIM "[y] confirm  [any] cancel" VT_RESET);
      } else if (editing) {
        _ui->drawControlBar(VT_DIM "[</>] CHANGE VALUE  [RET] CONFIRM & SAVE  [q] CANCEL" VT_RESET);
      } else {
        _ui->drawControlBar(VT_DIM "[^v] NAV  [RET] EDIT  [d] DFLT  [q] EXIT" VT_RESET);
      }

      _ui->vtFrameEnd();
    }

    delay(5);
  }
}
