#include "ToolSettings.h"
#include "../core/CapacitiveKeyboard.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"
#include "InputParser.h"
#include <Arduino.h>
#include <Preferences.h>

static const uint8_t NUM_PARAMS = 11;  // 8 legacy + 3 LOOP timers (v11)

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
    case 8: {  // clearLoopTimerMs — CLEAR long-press duration (LOOP Phase 1+)
      int step = accelerated ? 100 : 50;
      int val = (int)wk.clearLoopTimerMs + dir * step;
      if (val < 200) val = 200;
      if (val > 1500) val = 1500;
      wk.clearLoopTimerMs = (uint16_t)val;
      break;
    }
    case 9: {  // slotSaveTimerMs — slot save long-press duration (LOOP Phase 1+)
      int step = accelerated ? 100 : 50;
      int val = (int)wk.slotSaveTimerMs + dir * step;
      if (val < 500) val = 500;
      if (val > 2000) val = 2000;
      wk.slotSaveTimerMs = (uint16_t)val;
      break;
    }
    case 10: {  // slotClearTimerMs — slot delete visual animation (LOOP Phase 1+)
      int step = accelerated ? 100 : 50;
      int val = (int)wk.slotClearTimerMs + dir * step;
      if (val < 400) val = 400;
      if (val > 1500) val = 1500;
      wk.slotClearTimerMs = (uint16_t)val;
      break;
    }
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
    case 8:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "CLEAR Loop Timer" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Long-press duration on the LOOP CLEAR pad to empty a loop bank." VT_RESET);
      _ui->drawFrameLine(VT_DIM "The LED ramp (RAMP_HOLD cyan) matches this duration exactly —" VT_RESET);
      _ui->drawFrameLine(VT_DIM "single source of truth (LED spec §13). Release before expiry cancels." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Range: 200-1500 ms. Default: 500 ms. (Wired in LOOP Phase 1+.)" VT_RESET);
      break;
    case 9:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Slot Save Timer" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Long-press duration on a LOOP slot pad to save the current loop." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Matches the RAMP_HOLD magenta animation duration (LED spec §13)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Release before expiry cancels the save. Range: 500-2000 ms." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Default: 1000 ms. (Wired in LOOP Phase 1+.)" VT_RESET);
      break;
    case 10:
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Slot Clear Timer" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Visual animation duration for slot delete combo (CLEAR + slot pad)." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Unlike the other two timers, this is NOT a user hold — the delete" VT_RESET);
      _ui->drawFrameLine(VT_DIM "fires on rising edge. This timer purely sizes the RAMP_HOLD orange" VT_RESET);
      _ui->drawFrameLine(VT_DIM "confirmation animation. Range: 400-1500 ms. Default: 800 ms." VT_RESET);
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
                      DEFAULT_PANIC_ON_RECONNECT, 0, DEFAULT_BAT_ADC_AT_FULL,
                      500, 1000, 800};  // clearLoopTimerMs, slotSaveTimerMs, slotClearTimerMs
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

  _ui->vtClear();

  while (true) {
    if (_leds) _leds->update();

    NavEvent ev = input.update();

    // --- Defaults confirmation ---
    if (confirmDefaults) {
      ConfirmResult r = SetupUI::parseConfirm(ev);
      if (r == CONFIRM_YES) {
        wk = {EEPROM_MAGIC, SETTINGS_VERSION,
              DEFAULT_BASELINE_PROFILE, AT_RATE_DEFAULT, DEFAULT_BLE_INTERVAL,
              DEFAULT_CLOCK_MODE,
              DOUBLE_TAP_MS_DEFAULT, LED_BARGRAPH_DURATION_DEFAULT,
              DEFAULT_PANIC_ON_RECONNECT, 0, DEFAULT_BAT_ADC_AT_FULL,
              500, 1000, 800};  // LOOP timers defaults (v11)
        if (saveSettings(wk)) {
          original = wk;
          nvsSaved = true;
          _ui->flashSaved();
        }
        confirmDefaults = false;
        screenDirty = true;
      } else if (r == CONFIRM_NO) {
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
      if (ev.type == NAV_LEFT) {
        adjustParam(wk, cursor, -1, ev.accelerated);
        screenDirty = true;
      } else if (ev.type == NAV_RIGHT) {
        adjustParam(wk, cursor, +1, ev.accelerated);
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

      // --- LOOP TIMERS (v11) ---
      // These drive the RAMP_HOLD LED animation AND the user hold expectation
      // for LOOP CLEAR / slot save / slot delete confirmations. Single source
      // of truth per event (LED spec §13). Consumed by LOOP Phase 1+.
      _ui->drawSection("LOOP TIMERS");
      {
        char clrBuf[24];
        snprintf(clrBuf, sizeof(clrBuf), "%d ms  (200-1500)", wk.clearLoopTimerMs);
        drawParam(8, "CLEAR long-press:", clrBuf);
      }
      {
        char svBuf[24];
        snprintf(svBuf, sizeof(svBuf), "%d ms  (500-2000)", wk.slotSaveTimerMs);
        drawParam(9, "Slot save press:", svBuf);
      }
      {
        char scBuf[24];
        snprintf(scBuf, sizeof(scBuf), "%d ms  (400-1500)", wk.slotClearTimerMs);
        drawParam(10, "Slot clear anim:", scBuf);
      }
      _ui->drawFrameEmpty();

      // --- VEDETTE readout : segmented display when editing a numeric ---
      if (editing) {
        switch (cursor) {
          case 1:
            _ui->drawSection("AFTERTOUCH RATE");
            _ui->drawFrameEmpty();
            _ui->drawSegmentedValue("", wk.aftertouchRate, 3, "ms");
            _ui->drawFrameEmpty();
            break;
          case 4:
            _ui->drawSection("DOUBLE-TAP WINDOW");
            _ui->drawFrameEmpty();
            _ui->drawSegmentedValue("", wk.doubleTapMs, 3, "ms");
            _ui->drawFrameEmpty();
            break;
          case 5:
            _ui->drawSection("BARGRAPH DURATION");
            _ui->drawFrameEmpty();
            _ui->drawSegmentedValue("", wk.potBarDurationMs, 5, "ms");
            _ui->drawFrameEmpty();
            break;
          case 8:
            _ui->drawSection("CLEAR LOOP TIMER");
            _ui->drawFrameEmpty();
            _ui->drawSegmentedValue("", wk.clearLoopTimerMs, 4, "ms");
            _ui->drawFrameEmpty();
            break;
          case 9:
            _ui->drawSection("SLOT SAVE TIMER");
            _ui->drawFrameEmpty();
            _ui->drawSegmentedValue("", wk.slotSaveTimerMs, 4, "ms");
            _ui->drawFrameEmpty();
            break;
          case 10:
            _ui->drawSection("SLOT CLEAR TIMER");
            _ui->drawFrameEmpty();
            _ui->drawSegmentedValue("", wk.slotClearTimerMs, 4, "ms");
            _ui->drawFrameEmpty();
            break;
          default: break;
        }
      }

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
        _ui->drawControlBar(CBAR_CONFIRM_ANY);
      } else if (editing) {
        _ui->drawControlBar(VT_DIM "[</>] CHANGE VALUE" CBAR_SEP "[RET] CONFIRM & SAVE" CBAR_SEP "[q] CANCEL" VT_RESET);
      } else {
        _ui->drawControlBar(VT_DIM "[^v] NAV" CBAR_SEP "[RET] EDIT  [d] DFLT" CBAR_SEP "[q] EXIT" VT_RESET);
      }

      _ui->vtFrameEnd();
    }

    delay(5);
  }
}
