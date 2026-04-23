#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "HardwareConfig.h"
#include "KeyboardData.h"
#include "LedGrammar.h"
#include <Adafruit_NeoPixel.h>
#include <stdint.h>

// Forward declarations
struct BankSlot;
class ArpEngine;

class LedController {
public:
  LedController();
  void begin();
  void update();

  // Brightness (raw pot ADC 0-255, mapped via POT_BRIGHTNESS_CURVE)
  void setBrightness(uint8_t potValue);

  // Bank display
  void setCurrentBank(uint8_t bank);
  void setBatteryLow(bool low);

  // Multi-bank state
  void setBankSlots(const BankSlot* slots);

  // Event trigger (unified grammar — Phase 0 step 0.4).
  // Preempts any active event overlay. Resolves pattern + color + fgPct from
  // LedSettingsStore.eventOverrides[evt] (NVS override via Tool 8 Page EVENTS)
  // or EVENT_RENDER_DEFAULT (compile-time fallback). Durations for BLINK/FADE
  // come from the legacy per-event settings fields (bankDurationMs, holdOnFadeMs,
  // etc.) — editable via Tool 8 PATTERNS for globals.
  //
  // ledMask : 0 = target _currentBank only. Non-zero = bitmask of LEDs to
  //           animate simultaneously (used by SCALE_* scale-group propagation
  //           and PLAY/STOP via LEFT+double-tap on BG bank pad).
  void triggerEvent(EventId evt, uint8_t ledMask = 0);

  // Bargraph persistence
  void setPotBarDuration(uint16_t ms);

  // LED settings (from NVS)
  void loadLedSettings(const LedSettingsStore& store);
  void loadColorSlots(const ColorSlotStore& store);
  void rebuildGammaLut(uint8_t gammaTenths);

  // Boot
  void showBootProgress(uint8_t step);
  void showBootFailure(uint8_t step);
  void endBoot();

  // I2C error halt
  void haltI2CError();

  // Chase (calibration entry)
  void startChase();
  void stopChase();

  // Setup comet (active during Tools 1-6)
  void startSetupComet();
  void stopSetupComet();

  // Error
  void setError(bool error);

  // Battery gauge
  void showBatteryGauge(uint8_t percent);

  // Pot bargraph with catch visualization
  void showPotBargraph(float realLevel, uint8_t potLevel, bool caught);

  // Calibration
  void setCalibrationMode(bool active);
  void playValidation();

  // All off
  void allOff();

  // Preview API (for Tool 7 — direct LED control in setup mode)
  void previewBegin();   // Enters preview mode, suppresses normal rendering
  void previewEnd();     // Exits preview mode, clears LEDs
  void previewSetPixel(uint8_t led, const RGBW& color, uint8_t intensityPct);
  void previewClear();   // Clears all preview LEDs
  void previewShow();    // Calls _strip.show()

private:
  Adafruit_NeoPixel _strip;

  // Unified pixel setter: takes perceptual intensity (0-100%),
  // combines with master brightness, converts to linear, applies gamma
  void setPixel(uint8_t i, const RGBW& color, uint8_t intensityPct);
  void clearPixels();

  // Render helpers (priority-based, called from update())
  void renderBoot(unsigned long now);
  void renderComet(unsigned long now);
  void renderChase(unsigned long now);
  void renderError(unsigned long now);
  bool renderBattery(unsigned long now);
  bool renderBargraph(unsigned long now);
  bool renderConfirmation(unsigned long now);
  void renderCalibration(unsigned long now);
  void renderNormalDisplay(unsigned long now);
  void renderBankNormal(uint8_t led, bool isFg);
  void renderBankArpeg(uint8_t led, bool isFg, unsigned long now);

public:
  // Pattern engine (Phase 0 step 0.4).
  // Active event overlay is a single PatternInstance : new triggerEvent()
  // preempts any active one (same semantics as the legacy single-slot confirm).
  //
  // Phase 0.1 : struct declaration promoted to public: so ToolLedPreview can
  // construct arbitrary PatternInstance values and pass them to
  // renderPreviewPattern() below. The _eventOverlay member stays private.
  struct PatternInstance {
    uint8_t       patternId;  // PTN_* enum (0..8) or PTN_NONE when inactive
    uint8_t       fgPct;      // foreground intensity 0-100 (scaled by pattern math)
    uint8_t       ledMask;    // 0 = current bank ; non-zero = bitmask of LEDs
    uint8_t       reserved;
    unsigned long startTime;  // millis() at trigger
    PatternParams params;     // pattern-specific params (from LedGrammar.h)
    RGBW          colorA;     // primary color (resolved from ColorSlotId at trigger)
    RGBW          colorB;     // secondary color (used by CROSSFADE_COLOR only)
    bool          active;
  };

  // Phase 0.1 — Public wrapper for Tool 8 live preview (ToolLedPreview helper).
  // Thin pass-through to the private renderPattern() so the preview runs through
  // the same engine as runtime events (zero duplication). Does NOT touch
  // _eventOverlay. Caller owns inst.startTime and inst.ledMask.
  void renderPreviewPattern(const PatternInstance& inst, unsigned long now);

private:
  PatternInstance _eventOverlay;

  // Engine methods (step 0.4).
  void renderPattern(const PatternInstance& inst, unsigned long now);
  bool isPatternExpired(const PatternInstance& inst, unsigned long now) const;
  RGBW colorForSlot(uint8_t slotId) const;
  // Shared FLASH rendering logic (used inline by tick ARPEG in step 0.5, and
  // by the pattern engine for FLASH events).
  void renderFlashOverlay(uint8_t led, const RGBW& color, uint8_t fgPct,
                          uint8_t bgPct, unsigned long startTime,
                          uint16_t durationMs, bool isFg, unsigned long now);

  // Master brightness (0-100 perceptual, from pot via POT_BRIGHTNESS_CURVE)
  uint8_t _brightnessPct;

  // Bank display
  uint8_t _currentBank;
  bool _batteryLow;

  // Multi-bank state
  const BankSlot* _slots;

  // Resolved colors (from ColorSlotStore v4, 15 slots).
  // Populated at boot by loadColorSlots() ; accessed via colorForSlot(id)
  // or direct indexing _colors[CSLOT_*].
  RGBW _colors[COLOR_SLOT_COUNT];

  // LED settings (0-100 perceptual %)
  uint8_t  _normalFgIntensity;
  uint8_t  _fgArpStopMin, _fgArpStopMax;
  uint8_t  _fgArpPlayMax;         // FG ARPEG playing solid. BG intensities derived from FG via _bgFactor.
  uint8_t  _tickFlashFg, _tickFlashBg;
  uint16_t _pulsePeriodMs;
  uint16_t _tickBeatDurationMs;   // Phase 0.1 : ARPEG step FLASH duration (renamed + widened from uint8 _tickFlashDurationMs).
  uint16_t _tickBarDurationMs;    // Phase 0.1 : LOOP bar FLASH duration. Cached now, consumed by LoopEngine in Phase 1+.
  uint16_t _tickWrapDurationMs;   // Phase 0.1 : LOOP wrap FLASH duration. Cached now, consumed by LoopEngine in Phase 1+.
  uint8_t  _bankBlinks;
  uint16_t _bankDurationMs;
  uint8_t  _bankBrightnessPct;
  uint8_t  _scaleRootBlinks;
  uint16_t _scaleRootDurationMs;
  uint8_t  _scaleModeBlinks;
  uint16_t _scaleModeDurationMs;
  uint8_t  _scaleChromBlinks;
  uint16_t _scaleChromDurationMs;
  uint16_t _holdOnFadeMs;
  uint16_t _holdOffFadeMs;
  uint8_t  _octaveBlinks;
  uint16_t _octaveDurationMs;
  // SPARK params (LedSettingsStore v6).
  uint16_t _sparkOnMs;
  uint16_t _sparkGapMs;
  uint8_t  _sparkCycles;
  // Global bg factor (v6).
  uint8_t  _bgFactor;
  // Per-event overrides (LedSettingsStore v6 eventOverrides[EVT_COUNT]).
  // Copied at loadLedSettings() ; consulted by triggerEvent() — NVS override
  // takes precedence over EVENT_RENDER_DEFAULT when patternId != PTN_NONE.
  EventRenderEntry _eventOverrides[EVT_COUNT];

  uint8_t _gammaLut[256];
  unsigned long _flashStartTime[NUM_LEDS];

  // _eventOverlay replaces the legacy _confirmType/_confirmStart/... fields
  // as of step 0.4 (pattern engine). Single-slot overlay, preempted on
  // each triggerEvent call.

  // Bargraph
  uint16_t _potBarDurationMs;
  bool _showingPotBar;
  float   _potBarRealLevel;
  uint8_t _potBarPotLevel;
  bool    _potBarCaught;
  unsigned long _potBarStart;

  // Boot
  bool _bootMode;
  uint8_t _bootStep;
  uint8_t _bootFailStep;

  // Chase (calibration entry)
  bool _chaseActive;
  uint8_t _chasePos;
  unsigned long _chaseLastStep;

  // Setup comet
  bool _setupComet;
  uint8_t _cometPos;          // 0-13 (ping-pong: 0-7 forward, 8-13 = 6 down to 1)
  unsigned long _cometLastStep;

  // Calibration
  bool _calibrationMode;
  bool _validationFlashing;
  unsigned long _validationFlashStart;

  // Error
  bool _error;

  // Blink timer
  unsigned long _lastBlinkTime;
  bool _blinkState;

  // Battery gauge
  bool _showingBattery;
  uint8_t _batteryLeds;
  unsigned long _batteryDisplayStart;

  // Battery low blink
  unsigned long _batLowLastBurstTime;

  // Preview mode (Tool 7)
  bool _previewMode;
};

#endif // LED_CONTROLLER_H
