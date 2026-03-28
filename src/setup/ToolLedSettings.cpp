#include "ToolLedSettings.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"
#include "InputParser.h"
#include <Arduino.h>
#include <Preferences.h>

// Page 0: DISPLAY — 15 params (0-14)
// Page 1: CONFIRM — 15 params (0-14)
static const uint8_t PAGE0_COUNT = 15;
static const uint8_t PAGE1_COUNT = 15;

static const char* s_pageNames[] = {"DISPLAY", "CONFIRM"};

// =================================================================
// Constructor
// =================================================================
ToolLedSettings::ToolLedSettings()
  : _leds(nullptr), _nvs(nullptr), _ui(nullptr),
    _page(0), _cursor(0), _editing(false),
    _nvsSaved(true), _confirmDefaults(false)
{
  memset(&_wk, 0, sizeof(_wk));
}

void ToolLedSettings::begin(LedController* leds, NvsManager* nvs, SetupUI* ui) {
  _leds = leds;
  _nvs = nvs;
  _ui = ui;
}

// =================================================================
// pageParamCount
// =================================================================
uint8_t ToolLedSettings::pageParamCount() const {
  return (_page == 0) ? PAGE0_COUNT : PAGE1_COUNT;
}

// =================================================================
// adjustParam
// =================================================================
void ToolLedSettings::adjustParam(int8_t dir, bool accel) {
  if (_page == 0) {
    // ---- PAGE 0: DISPLAY ----
    switch (_cursor) {
      case 0: { // normalFgIntensity 10-255
        int step = accel ? 25 : 5;
        int val = (int)_wk.normalFgIntensity + dir * step;
        if (val < 10) val = 10;
        if (val > 255) val = 255;
        _wk.normalFgIntensity = (uint8_t)val;
        break;
      }
      case 1: { // normalBgIntensity 5-100
        int step = accel ? 25 : 5;
        int val = (int)_wk.normalBgIntensity + dir * step;
        if (val < 5) val = 5;
        if (val > 100) val = 100;
        _wk.normalBgIntensity = (uint8_t)val;
        break;
      }
      case 2: { // fgArpStopMin 10-255
        int step = accel ? 25 : 5;
        int val = (int)_wk.fgArpStopMin + dir * step;
        if (val < 10) val = 10;
        if (val > 255) val = 255;
        _wk.fgArpStopMin = (uint8_t)val;
        break;
      }
      case 3: { // fgArpStopMax 10-255
        int step = accel ? 25 : 5;
        int val = (int)_wk.fgArpStopMax + dir * step;
        if (val < 10) val = 10;
        if (val > 255) val = 255;
        _wk.fgArpStopMax = (uint8_t)val;
        break;
      }
      case 4: { // fgArpPlayMin 10-255
        int step = accel ? 25 : 5;
        int val = (int)_wk.fgArpPlayMin + dir * step;
        if (val < 10) val = 10;
        if (val > 255) val = 255;
        _wk.fgArpPlayMin = (uint8_t)val;
        break;
      }
      case 5: { // fgArpPlayMax 10-255
        int step = accel ? 25 : 5;
        int val = (int)_wk.fgArpPlayMax + dir * step;
        if (val < 10) val = 10;
        if (val > 255) val = 255;
        _wk.fgArpPlayMax = (uint8_t)val;
        break;
      }
      case 6: { // fgTickFlash 50-255
        int step = accel ? 25 : 5;
        int val = (int)_wk.fgTickFlash + dir * step;
        if (val < 50) val = 50;
        if (val > 255) val = 255;
        _wk.fgTickFlash = (uint8_t)val;
        break;
      }
      case 7: { // bgArpStopMin 0-100
        int step = accel ? 25 : 5;
        int val = (int)_wk.bgArpStopMin + dir * step;
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        _wk.bgArpStopMin = (uint8_t)val;
        break;
      }
      case 8: { // bgArpStopMax 0-100
        int step = accel ? 25 : 5;
        int val = (int)_wk.bgArpStopMax + dir * step;
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        _wk.bgArpStopMax = (uint8_t)val;
        break;
      }
      case 9: { // bgArpPlayMin 0-100
        int step = accel ? 25 : 5;
        int val = (int)_wk.bgArpPlayMin + dir * step;
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        _wk.bgArpPlayMin = (uint8_t)val;
        break;
      }
      case 10: { // bgArpPlayMax 0-100
        int step = accel ? 25 : 5;
        int val = (int)_wk.bgArpPlayMax + dir * step;
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        _wk.bgArpPlayMax = (uint8_t)val;
        break;
      }
      case 11: { // bgTickFlash 10-100
        int step = accel ? 25 : 5;
        int val = (int)_wk.bgTickFlash + dir * step;
        if (val < 10) val = 10;
        if (val > 100) val = 100;
        _wk.bgTickFlash = (uint8_t)val;
        break;
      }
      case 12: { // absoluteMax 50-255
        int step = accel ? 25 : 5;
        int val = (int)_wk.absoluteMax + dir * step;
        if (val < 50) val = 50;
        if (val > 255) val = 255;
        _wk.absoluteMax = (uint8_t)val;
        break;
      }
      case 13: { // pulsePeriodMs 500-4000
        int step = accel ? 250 : 50;
        int val = (int)_wk.pulsePeriodMs + dir * step;
        if (val < 500) val = 500;
        if (val > 4000) val = 4000;
        _wk.pulsePeriodMs = (uint16_t)val;
        break;
      }
      case 14: { // tickFlashDurationMs 10-100
        int step = accel ? 25 : 5;
        int val = (int)_wk.tickFlashDurationMs + dir * step;
        if (val < 10) val = 10;
        if (val > 100) val = 100;
        _wk.tickFlashDurationMs = (uint8_t)val;
        break;
      }
    }
  } else {
    // ---- PAGE 1: CONFIRM ----
    switch (_cursor) {
      case 0: { // bankBlinks 1-3 wrap
        _wk.bankBlinks = (_wk.bankBlinks - 1 + 3 + dir) % 3 + 1;
        break;
      }
      case 1: { // bankDurationMs 100-500
        int step = accel ? 100 : 50;
        int val = (int)_wk.bankDurationMs + dir * step;
        if (val < 100) val = 100;
        if (val > 500) val = 500;
        _wk.bankDurationMs = (uint16_t)val;
        break;
      }
      case 2: { // bankBrightnessPct 20-100
        int step = accel ? 25 : 5;
        int val = (int)_wk.bankBrightnessPct + dir * step;
        if (val < 20) val = 20;
        if (val > 100) val = 100;
        _wk.bankBrightnessPct = (uint8_t)val;
        break;
      }
      case 3: { // scaleRootBlinks 1-3 wrap
        _wk.scaleRootBlinks = (_wk.scaleRootBlinks - 1 + 3 + dir) % 3 + 1;
        break;
      }
      case 4: { // scaleRootDurationMs 100-500
        int step = accel ? 100 : 50;
        int val = (int)_wk.scaleRootDurationMs + dir * step;
        if (val < 100) val = 100;
        if (val > 500) val = 500;
        _wk.scaleRootDurationMs = (uint16_t)val;
        break;
      }
      case 5: { // scaleModeBlinks 1-3 wrap
        _wk.scaleModeBlinks = (_wk.scaleModeBlinks - 1 + 3 + dir) % 3 + 1;
        break;
      }
      case 6: { // scaleModeDurationMs 100-500
        int step = accel ? 100 : 50;
        int val = (int)_wk.scaleModeDurationMs + dir * step;
        if (val < 100) val = 100;
        if (val > 500) val = 500;
        _wk.scaleModeDurationMs = (uint16_t)val;
        break;
      }
      case 7: { // scaleChromBlinks 1-3 wrap
        _wk.scaleChromBlinks = (_wk.scaleChromBlinks - 1 + 3 + dir) % 3 + 1;
        break;
      }
      case 8: { // scaleChromDurationMs 100-500
        int step = accel ? 100 : 50;
        int val = (int)_wk.scaleChromDurationMs + dir * step;
        if (val < 100) val = 100;
        if (val > 500) val = 500;
        _wk.scaleChromDurationMs = (uint16_t)val;
        break;
      }
      case 9: { // holdOnFlashMs 50-250
        int step = accel ? 50 : 10;
        int val = (int)_wk.holdOnFlashMs + dir * step;
        if (val < 50) val = 50;
        if (val > 250) val = 250;
        _wk.holdOnFlashMs = (uint8_t)val;
        break;
      }
      case 10: { // holdFadeMs 100-600
        int step = accel ? 100 : 50;
        int val = (int)_wk.holdFadeMs + dir * step;
        if (val < 100) val = 100;
        if (val > 600) val = 600;
        _wk.holdFadeMs = (uint16_t)val;
        break;
      }
      case 11: { // playBeatCount 1-4 wrap
        _wk.playBeatCount = (_wk.playBeatCount - 1 + 4 + dir) % 4 + 1;
        break;
      }
      case 12: { // stop fade — shares holdFadeMs (same mechanism in LedController)
        int step = accel ? 100 : 50;
        int val = (int)_wk.holdFadeMs + dir * step;
        if (val < 100) val = 100;
        if (val > 600) val = 600;
        _wk.holdFadeMs = (uint16_t)val;
        break;
      }
      case 13: { // octaveBlinks 1-3 wrap
        _wk.octaveBlinks = (_wk.octaveBlinks - 1 + 3 + dir) % 3 + 1;
        break;
      }
      case 14: { // octaveDurationMs 100-500
        int step = accel ? 100 : 50;
        int val = (int)_wk.octaveDurationMs + dir * step;
        if (val < 100) val = 100;
        if (val > 500) val = 500;
        _wk.octaveDurationMs = (uint16_t)val;
        break;
      }
    }
  }
}

// =================================================================
// saveSettings
// =================================================================
bool ToolLedSettings::saveSettings() {
  _wk.magic = EEPROM_MAGIC;
  _wk.version = LED_SETTINGS_VERSION;
  Preferences prefs;
  if (!prefs.begin(LED_SETTINGS_NVS_NAMESPACE, false)) return false;
  prefs.putBytes(LED_SETTINGS_NVS_KEY, &_wk, sizeof(LedSettingsStore));
  prefs.end();
  if (_leds) _leds->loadLedSettings(_wk);
  return true;
}

// =================================================================
// drawDescription — per-parameter info panel
// =================================================================
void ToolLedSettings::drawDescription() {
  if (_page == 0) {
    switch (_cursor) {
      case 0:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Foreground Intensity (NORMAL)" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Intensity of the LED showing the active NORMAL bank." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Solid, no pulse. This is the reference point for all" VT_RESET);
        _ui->drawFrameLine(VT_DIM "other brightness values. Range 10-255." VT_RESET);
        break;
      case 1:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Background Intensity (NORMAL)" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Intensity of LEDs for inactive NORMAL banks." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Solid dim glow. Higher values make background banks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "more visible but reduce contrast with foreground. Range 5-100." VT_RESET);
        break;
      case 2:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "FG Pulse Min (Stopped)" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Lowest point of the sine breathing cycle on the active" VT_RESET);
        _ui->drawFrameLine(VT_DIM "ARPEG bank when stopped. The difference between min and" VT_RESET);
        _ui->drawFrameLine(VT_DIM "max determines the breath depth. Range 10-255." VT_RESET);
        break;
      case 3:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "FG Pulse Max (Stopped)" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Highest point of the sine breathing cycle on the active" VT_RESET);
        _ui->drawFrameLine(VT_DIM "ARPEG bank when stopped. Wider min-max gap = deeper breath." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Narrow gap = subtle shimmer. Range 10-255." VT_RESET);
        break;
      case 4:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "FG Pulse Min (Playing)" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Lowest point of the sine cycle when the active ARPEG bank" VT_RESET);
        _ui->drawFrameLine(VT_DIM "is playing. Tick flashes overlay this pulse. Lower values" VT_RESET);
        _ui->drawFrameLine(VT_DIM "make tick flashes stand out more. Range 10-255." VT_RESET);
        break;
      case 5:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "FG Pulse Max (Playing)" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Highest point of the sine cycle when the active ARPEG bank" VT_RESET);
        _ui->drawFrameLine(VT_DIM "is playing. Kept below stopped-max so tick flashes are visible" VT_RESET);
        _ui->drawFrameLine(VT_DIM "against the pulse. Range 10-255." VT_RESET);
        break;
      case 6:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "FG Tick Flash" VT_RESET);
        _ui->drawFrameLine(VT_DIM "White flash intensity on the active ARPEG LED at each arp step." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Uses absolute brightness (ignores global brightness pot)." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Capped by Absolute Max. Range 50-255." VT_RESET);
        break;
      case 7:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "BG Pulse Min (Stopped)" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Lowest point of the sine cycle on background ARPEG banks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "when stopped. Lower = deeper fade into darkness. Set to 0" VT_RESET);
        _ui->drawFrameLine(VT_DIM "for complete off at the bottom of each breath. Range 0-100." VT_RESET);
        break;
      case 8:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "BG Pulse Max (Stopped)" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Highest point of the sine cycle on background ARPEG banks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "when stopped. Keep well below foreground values to maintain" VT_RESET);
        _ui->drawFrameLine(VT_DIM "clear visual hierarchy between active and background. Range 0-100." VT_RESET);
        break;
      case 9:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "BG Pulse Min (Playing)" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Lowest point of the sine cycle on background ARPEG banks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "when playing. Background playing banks also receive tick" VT_RESET);
        _ui->drawFrameLine(VT_DIM "flashes at reduced intensity. Range 0-100." VT_RESET);
        break;
      case 10:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "BG Pulse Max (Playing)" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Highest point of the sine cycle on background ARPEG banks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "when playing. Kept below stopped-max so tick flashes are" VT_RESET);
        _ui->drawFrameLine(VT_DIM "distinguishable from the breathing pulse. Range 0-100." VT_RESET);
        break;
      case 11:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "BG Tick Flash" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Flash intensity on background ARPEG LEDs at each arp step." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Uses absolute brightness (ignores global brightness pot)." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Capped by Absolute Max. Range 10-100." VT_RESET);
        break;
      case 12:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Absolute Max Cap" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Maximum brightness for events that bypass the global brightness" VT_RESET);
        _ui->drawFrameLine(VT_DIM "pot (tick flashes, error blinks). Lower this to tame absolute" VT_RESET);
        _ui->drawFrameLine(VT_DIM "events on bright LED strips. Range 50-255." VT_RESET);
        break;
      case 13:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Pulse Period" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Duration of one full sine breathing cycle in milliseconds." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Affects all ARPEG bank pulsing (foreground + background)." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Lower = faster breath. Default 1472ms (~1.5s). Range 500-4000." VT_RESET);
        break;
      case 14:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Tick Flash Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "How long each arp step flash stays visible in milliseconds." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Shorter = snappier visual beat. Longer = more visible at" VT_RESET);
        _ui->drawFrameLine(VT_DIM "fast tempos. Default 30ms. Range 10-100." VT_RESET);
        break;
    }
  } else {
    switch (_cursor) {
      case 0:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank Switch Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Number of ON-OFF blink cycles when switching banks. The" VT_RESET);
        _ui->drawFrameLine(VT_DIM "destination bank LED blinks in white (NORMAL) or blue (ARPEG)." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Other 7 bank LEDs stay visible underneath. Range 1-3." VT_RESET);
        break;
      case 1:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank Switch Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total time for all bank switch blink cycles in milliseconds." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Divided equally among blinks. More blinks at same duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "= faster individual blinks. Range 100-500." VT_RESET);
        break;
      case 2:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Bank Switch Intensity" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Brightness of bank switch blinks as a percentage of the" VT_RESET);
        _ui->drawFrameLine(VT_DIM "global brightness setting. Lower values produce subtler" VT_RESET);
        _ui->drawFrameLine(VT_DIM "blinks. Range 20-100%%." VT_RESET);
        break;
      case 3:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Root Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Number of ON-OFF blink cycles when changing scale root note." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Blinks vivid yellow on the current bank LED." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 1-3." VT_RESET);
        break;
      case 4:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Root Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total time for all scale root blink cycles in milliseconds." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Divided equally among blinks." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 100-500." VT_RESET);
        break;
      case 5:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Mode Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Number of ON-OFF blink cycles when changing scale mode." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Blinks pale yellow on the current bank LED." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 1-3." VT_RESET);
        break;
      case 6:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Mode Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total time for all scale mode blink cycles in milliseconds." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Divided equally among blinks." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 100-500." VT_RESET);
        break;
      case 7:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Chromatic Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Number of ON-OFF blink cycles when toggling chromatic mode." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Blinks golden yellow on the current bank LED." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 1-3." VT_RESET);
        break;
      case 8:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Scale Chromatic Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total time for all scale chromatic blink cycles in ms." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Divided equally among blinks." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 100-500." VT_RESET);
        break;
      case 9:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Hold ON Flash Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Duration of the single flash when HOLD mode activates." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Deep blue flash on the current bank LED. Includes both" VT_RESET);
        _ui->drawFrameLine(VT_DIM "the ON phase and the OFF gap. Range 50-250." VT_RESET);
        break;
      case 10:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Hold OFF Fade Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Duration of the fade-out animation when HOLD mode deactivates." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Deep blue fades smoothly to zero on the current bank LED." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 100-600." VT_RESET);
        break;
      case 11:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Play Beat Count" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Number of beat-synced flashes after the initial play ack." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Green ack flash then blue-cyan flashes synced to clock." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total flashes = 1 ack + this count. Range 1-4." VT_RESET);
        break;
      case 12:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Stop Fade Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Duration of the fade-out animation when arp transport stops." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Blue-cyan fades smoothly to zero. Shares the hold fade" VT_RESET);
        _ui->drawFrameLine(VT_DIM "mechanism internally. Range 100-600." VT_RESET);
        break;
      case 13:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Octave Blinks" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Number of ON-OFF blink cycles when changing arp octave range." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Blue-violet blinks on a 2-LED group (octave 1=LEDs 0-1," VT_RESET);
        _ui->drawFrameLine(VT_DIM "2=LEDs 2-3, 3=LEDs 4-5, 4=LEDs 6-7). Range 1-3." VT_RESET);
        break;
      case 14:
        _ui->drawFrameLine(VT_BRIGHT_WHITE "Octave Duration" VT_RESET);
        _ui->drawFrameLine(VT_DIM "Total time for all octave blink cycles in milliseconds." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Divided equally among blinks." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Range 100-500." VT_RESET);
        break;
    }
  }
}

// =================================================================
// Hardcoded defaults (mirrors HardwareConfig.h constants)
// =================================================================
static LedSettingsStore s_defaults() {
  LedSettingsStore d;
  d.magic   = EEPROM_MAGIC;
  d.version = LED_SETTINGS_VERSION;
  d.reserved = 0;
  // NORMAL banks
  d.normalFgIntensity = 255;
  d.normalBgIntensity = 40;
  // ARPEG banks
  d.fgArpStopMin      = LED_FG_ARP_STOP_MIN;   // 77
  d.fgArpStopMax      = LED_FG_ARP_STOP_MAX;   // 255
  d.fgArpPlayMin      = LED_FG_ARP_PLAY_MIN;   // 77
  d.fgArpPlayMax      = LED_FG_ARP_PLAY_MAX;   // 204
  d.fgTickFlash        = 255;
  d.bgArpStopMin      = LED_BG_ARP_STOP_MIN;   // 20
  d.bgArpStopMax      = LED_BG_ARP_STOP_MAX;   // 64
  d.bgArpPlayMin      = LED_BG_ARP_PLAY_MIN;   // 20
  d.bgArpPlayMax      = LED_BG_ARP_PLAY_MAX;   // 51
  d.bgTickFlash        = LED_BG_ARP_PLAY_FLASH; // 64
  d.absoluteMax        = LED_ABSOLUTE_MAX;       // 255
  // Timing
  d.pulsePeriodMs      = LED_PULSE_PERIOD_MS;   // 1472
  d.tickFlashDurationMs = LED_TICK_FLASH_DURATION_MS; // 30
  // Confirmations
  d.bankBlinks         = 3;
  d.bankDurationMs     = 300;
  d.bankBrightnessPct  = LED_CONFIRM_BRIGHTNESS_PCT; // 50
  d.scaleRootBlinks    = 2;
  d.scaleRootDurationMs = 200;
  d.scaleModeBlinks    = 2;
  d.scaleModeDurationMs = 200;
  d.scaleChromBlinks   = 2;
  d.scaleChromDurationMs = 200;
  d.holdOnFlashMs      = LED_CONFIRM_HOLD_ON_MS;  // 150
  d.holdFadeMs         = LED_CONFIRM_FADE_MS;     // 300
  d.playBeatCount      = 3;
  d.octaveBlinks       = 3;
  d.octaveDurationMs   = 300;
  return d;
}

// =================================================================
// run() — LED Settings with 2-page layout
// =================================================================
void ToolLedSettings::run() {
  if (!_ui) return;

  // Load working copy from NVS (or defaults)
  _wk = s_defaults();
  {
    Preferences prefs;
    if (prefs.begin(LED_SETTINGS_NVS_NAMESPACE, true)) {
      size_t len = prefs.getBytesLength(LED_SETTINGS_NVS_KEY);
      if (len == sizeof(LedSettingsStore)) {
        LedSettingsStore tmp;
        prefs.getBytes(LED_SETTINGS_NVS_KEY, &tmp, sizeof(LedSettingsStore));
        if (tmp.magic == EEPROM_MAGIC && tmp.version == LED_SETTINGS_VERSION) {
          _wk = tmp;
        }
      }
      prefs.end();
    }
  }

  Serial.print(ITERM_RESIZE);

  LedSettingsStore original = _wk;
  _nvsSaved = true;
  _page = 0;
  _cursor = 0;
  _editing = false;
  _confirmDefaults = false;

  InputParser input;
  bool screenDirty = true;

  _ui->vtClear();

  while (true) {
    if (_leds) _leds->update();

    NavEvent ev = input.update();

    // --- Defaults confirmation ---
    if (_confirmDefaults) {
      if (ev.type == NAV_CHAR && (ev.ch == 'y' || ev.ch == 'Y')) {
        _wk = s_defaults();
        if (saveSettings()) {
          original = _wk;
          _nvsSaved = true;
          _ui->flashSaved();
        }
        _confirmDefaults = false;
        screenDirty = true;
      } else if (ev.type != NAV_NONE) {
        _confirmDefaults = false;
        screenDirty = true;
      }
      delay(5);
      continue;
    }

    // --- Main navigation ---
    if (ev.type == NAV_QUIT) {
      if (_editing) {
        _wk = original;
        _editing = false;
        screenDirty = true;
      } else {
        _ui->vtClear();
        return;
      }
    }

    if (ev.type == NAV_DEFAULTS && !_editing) {
      _confirmDefaults = true;
      screenDirty = true;
    }

    // Page toggle
    if (ev.type == NAV_TOGGLE && !_editing) {
      _page = 1 - _page;
      _cursor = 0;
      screenDirty = true;
    }

    uint8_t count = pageParamCount();

    if (!_editing) {
      if (ev.type == NAV_UP) {
        if (_cursor > 0) _cursor--;
        else _cursor = count - 1;
        screenDirty = true;
      } else if (ev.type == NAV_DOWN) {
        if (_cursor < count - 1) _cursor++;
        else _cursor = 0;
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        _editing = true;
        screenDirty = true;
      }
    } else {
      if (ev.type == NAV_LEFT) {
        adjustParam(-1, ev.accelerated);
        screenDirty = true;
      } else if (ev.type == NAV_RIGHT) {
        adjustParam(+1, ev.accelerated);
        screenDirty = true;
      } else if (ev.type == NAV_ENTER) {
        if (saveSettings()) {
          _editing = false;
          _nvsSaved = true;
          _ui->flashSaved();
          original = _wk;
          screenDirty = true;
        }
      }
    }

    // --- Render ---
    if (screenDirty) {
      screenDirty = false;

      _ui->vtFrameStart();

      // Header with page indicator
      char headerBuf[64];
      snprintf(headerBuf, sizeof(headerBuf), "TOOL 7: LED SETTINGS  [%s]", s_pageNames[_page]);
      _ui->drawConsoleHeader(headerBuf, _nvsSaved);
      _ui->drawFrameEmpty();

      // Helper for param line
      auto drawParam = [&](uint8_t idx, const char* label, const char* value) {
        bool selected = (_cursor == idx);
        bool isEditing = selected && _editing;
        if (isEditing) {
          _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-32s" VT_CYAN "[%s]" VT_RESET, label, value);
        } else if (selected) {
          _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET "%-32s" VT_BRIGHT_WHITE "%s" VT_RESET, label, value);
        } else {
          _ui->drawFrameLine("  %-32s%s", label, value);
        }
      };

      char buf[32];

      if (_page == 0) {
        // ============ PAGE 0: DISPLAY ============

        // --- NORMAL BANKS ---
        _ui->drawSection("NORMAL BANKS");
        snprintf(buf, sizeof(buf), "%d", _wk.normalFgIntensity);
        drawParam(0, "Foreground intensity:", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.normalBgIntensity);
        drawParam(1, "Background intensity:", buf);
        _ui->drawFrameEmpty();

        // --- ARPEG BANKS ---
        _ui->drawSection("ARPEG BANKS");
        snprintf(buf, sizeof(buf), "%d", _wk.fgArpStopMin);
        drawParam(2, "FG pulse min (stopped):", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.fgArpStopMax);
        drawParam(3, "FG pulse max (stopped):", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.fgArpPlayMin);
        drawParam(4, "FG pulse min (playing):", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.fgArpPlayMax);
        drawParam(5, "FG pulse max (playing):", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.fgTickFlash);
        drawParam(6, "FG tick flash:", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.bgArpStopMin);
        drawParam(7, "BG pulse min (stopped):", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.bgArpStopMax);
        drawParam(8, "BG pulse max (stopped):", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.bgArpPlayMin);
        drawParam(9, "BG pulse min (playing):", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.bgArpPlayMax);
        drawParam(10, "BG pulse max (playing):", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.bgTickFlash);
        drawParam(11, "BG tick flash:", buf);
        snprintf(buf, sizeof(buf), "%d", _wk.absoluteMax);
        drawParam(12, "Absolute max cap:", buf);
        _ui->drawFrameEmpty();

        // --- TIMING ---
        _ui->drawSection("TIMING");
        snprintf(buf, sizeof(buf), "%d ms", _wk.pulsePeriodMs);
        drawParam(13, "Pulse period:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.tickFlashDurationMs);
        drawParam(14, "Tick flash duration:", buf);

      } else {
        // ============ PAGE 1: CONFIRM ============

        // --- BANK SWITCH ---
        _ui->drawSection("BANK SWITCH");
        snprintf(buf, sizeof(buf), "%d", _wk.bankBlinks);
        drawParam(0, "Blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.bankDurationMs);
        drawParam(1, "Duration:", buf);
        snprintf(buf, sizeof(buf), "%d%%", _wk.bankBrightnessPct);
        drawParam(2, "Intensity:", buf);
        _ui->drawFrameEmpty();

        // --- SCALE ROOT ---
        _ui->drawSection("SCALE ROOT");
        snprintf(buf, sizeof(buf), "%d", _wk.scaleRootBlinks);
        drawParam(3, "Blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.scaleRootDurationMs);
        drawParam(4, "Duration:", buf);
        _ui->drawFrameEmpty();

        // --- SCALE MODE ---
        _ui->drawSection("SCALE MODE");
        snprintf(buf, sizeof(buf), "%d", _wk.scaleModeBlinks);
        drawParam(5, "Blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.scaleModeDurationMs);
        drawParam(6, "Duration:", buf);
        _ui->drawFrameEmpty();

        // --- SCALE CHROMATIC ---
        _ui->drawSection("SCALE CHROMATIC");
        snprintf(buf, sizeof(buf), "%d", _wk.scaleChromBlinks);
        drawParam(7, "Blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.scaleChromDurationMs);
        drawParam(8, "Duration:", buf);
        _ui->drawFrameEmpty();

        // --- HOLD ---
        _ui->drawSection("HOLD");
        snprintf(buf, sizeof(buf), "%d ms", _wk.holdOnFlashMs);
        drawParam(9, "ON flash duration:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.holdFadeMs);
        drawParam(10, "OFF fade duration:", buf);
        _ui->drawFrameEmpty();

        // --- PLAY / STOP ---
        _ui->drawSection("PLAY / STOP");
        snprintf(buf, sizeof(buf), "%d", _wk.playBeatCount);
        drawParam(11, "Beat count:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.holdFadeMs);
        drawParam(12, "Stop fade duration:", buf);
        _ui->drawFrameEmpty();

        // --- OCTAVE ---
        _ui->drawSection("OCTAVE");
        snprintf(buf, sizeof(buf), "%d", _wk.octaveBlinks);
        drawParam(13, "Blinks:", buf);
        snprintf(buf, sizeof(buf), "%d ms", _wk.octaveDurationMs);
        drawParam(14, "Duration:", buf);
      }

      _ui->drawFrameEmpty();

      // --- INFO ---
      _ui->drawSection("INFO");

      if (_confirmDefaults) {
        _ui->drawFrameLine(VT_YELLOW "Reset ALL LED settings to factory defaults? (y/n)" VT_RESET);
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
        _ui->drawFrameEmpty();
      } else {
        drawDescription();
      }

      _ui->drawFrameEmpty();

      // Control bar
      if (_confirmDefaults) {
        _ui->drawControlBar(VT_DIM "[y] confirm  [any] cancel" VT_RESET);
      } else if (_editing) {
        _ui->drawControlBar(VT_DIM "[</>] CHANGE VALUE  [RET] CONFIRM & SAVE  [q] CANCEL" VT_RESET);
      } else {
        char ctrlBuf[128];
        snprintf(ctrlBuf, sizeof(ctrlBuf),
                 VT_DIM "[^v] NAV  [RET] EDIT  [t] " VT_RESET VT_BRIGHT_WHITE "%s" VT_RESET VT_DIM "  [d] DFLT  [q] EXIT" VT_RESET,
                 s_pageNames[1 - _page]);
        _ui->drawControlBar(ctrlBuf);
      }

      _ui->vtFrameEnd();
    }

    delay(5);
  }
}
