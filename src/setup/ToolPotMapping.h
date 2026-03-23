#ifndef TOOL_POT_MAPPING_H
#define TOOL_POT_MAPPING_H

#include <stdint.h>
#include "../managers/PotRouter.h"

class LedController;
class SetupUI;

// =================================================================
// ToolPotMapping — Tool 6: user-configurable pot parameter assignments
//
// UX: two pages (NORMAL / ARPEG), toggled with 't'.
// Up/Down navigates slots (0-7). Enter toggles edit mode.
// In edit mode, Left/Right cycles the assignable pool.
// Enter confirms assignment + saves to NVS.
// CC target enters CC# sub-editor (Left/Right adjust, accel x10).
// Steal logic: picking an already-assigned param shows y/n prompt.
// Physical pot detection: turn a pot to jump cursor (when not editing).
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

  // Working copy (edited, only committed on save)
  PotMappingStore _wk;

  // Current UI state
  bool    _contextNormal;    // true=NORMAL, false=ARPEG
  uint8_t _cursor;           // 0-7: which slot
  bool    _editing;          // true = navigating pool
  uint8_t _poolIdx;          // index in current pool
  bool    _ccEditing;        // true = in CC# sub-editor
  uint8_t _ccNumber;         // CC# being edited (0-127)
  bool    _confirmSteal;     // true = waiting for y/n on steal
  int8_t  _stealSourceSlot;  // slot that would be orphaned
  PotTarget _stealTarget;    // target being stolen

  // Pool: list of assignable targets for each context
  static const uint8_t MAX_POOL = 12;
  PotTarget _pool[MAX_POOL];
  uint8_t   _poolCount;

  void buildPool();
  void drawScreen();
  void drawSlotLine(uint8_t slot);
  void drawPoolLine();
  void drawDescription();
  void drawHelpLine();

  // Find which slot in current context has a given target (returns -1 if none)
  int8_t findSlotWithTarget(PotTarget t, uint8_t ccNum = 0) const;

  // Get current context map pointer
  PotMapping* currentMap();
  const PotMapping* currentMapConst() const;

  // Slot display name (e.g. "R1 alone", "R2 + hold")
  static const char* slotName(uint8_t slot);

  // Pot ADC detection (direct reads, not through PotRouter)
  uint16_t _potBaseline[4];
  void samplePotBaselines();
  int8_t detectMovedPot(bool btnLeftHeld);

  // Target name for display
  static const char* targetName(PotTarget t);

  // Save to NVS (direct Preferences call, blocking)
  bool saveMapping();

  // Assign target to current slot, handling steal + CC + PB
  void assignCurrentTarget();
};

#endif // TOOL_POT_MAPPING_H
