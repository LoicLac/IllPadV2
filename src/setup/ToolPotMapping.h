#ifndef TOOL_POT_MAPPING_H
#define TOOL_POT_MAPPING_H

#include <stdint.h>
#include "../managers/PotRouter.h"
#include "SetupPotInput.h"

class LedController;
class SetupUI;

// =================================================================
// ToolPotMapping — Tool 6: user-configurable pot parameter assignments
//
// Two-column layout (Alone | + Hold), side by side.
// Up/Down = R1-R4, Left/Right = switch column.
// Enter = edit (pool cycling), 't' = toggle NORMAL/ARPEG context.
// =================================================================
class ToolPotMapping {
public:
  ToolPotMapping();

  void begin(LedController* leds, SetupUI* ui, PotRouter* potRouter);
  void run();  // Blocking

private:
  LedController* _leds;
  SetupUI*       _ui;
  PotRouter*     _potRouter;

  // Working copy
  PotMappingStore _wk;

  // UI state
  bool    _contextNormal;    // true=NORMAL, false=ARPEG
  uint8_t _cursorRow;        // 0-3 (R1-R4)
  uint8_t _cursorCol;        // 0=Alone, 1=+Hold
  bool    _editing;
  uint8_t _poolIdx;
  bool    _ccEditing;
  uint8_t _ccNumber;
  bool    _confirmSteal;
  int8_t  _stealSourceSlot;
  PotTarget _stealTarget;
  bool    _confirmDefaults;
  bool    _nvsSaved;

  // Pool
  static const uint8_t MAX_POOL = 12;
  PotTarget _pool[MAX_POOL];
  uint8_t   _poolCount;

  void buildPool();
  void drawScreen();
  void drawTwoColumnLayout();
  void drawPoolLine();
  void drawInfoPanel();

  // Helpers
  uint8_t cursorToSlot() const;  // Convert row+col to slot index 0-7
  int8_t findSlotWithTarget(PotTarget t, uint8_t ccNum = 0) const;
  PotMapping* currentMap();
  const PotMapping* currentMapConst() const;
  static const char* slotName(uint8_t slot);
  static const char* targetName(PotTarget t);

  // Pot detection (physical pot → slot jump, uses PotFilter)
  uint16_t _potBaseline[4];
  void samplePotBaselines();
  int8_t detectMovedPot(bool btnLeftHeld);

  // Pot input (CC# edit only — all nav is keyboard-driven)
  SetupPotInput _pots;
  int32_t _potCcNum;       // CC# 0-127 (CC edit mode)

  // NVS
  bool saveMapping();
  void assignCurrentTarget();

  // Description for each target
  void printTargetDescription(PotTarget t);
};

#endif // TOOL_POT_MAPPING_H
