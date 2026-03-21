#ifndef TOOL_POT_MAPPING_H
#define TOOL_POT_MAPPING_H

#include <stdint.h>
#include "../managers/PotRouter.h"

class LedController;
class SetupUI;

// =================================================================
// ToolPotMapping — Tool 6: user-configurable pot parameter assignments
//
// UX: two pages (NORMAL / ARPEG). Each shows a 4×2 grid of pot slots
// and a pool line showing all assignable parameters, color-coded:
//   BRIGHT = available (not on any slot)
//   DIM    = already assigned to a slot
//
// Turn a pot (or hold-left + turn) to select a slot.
// < > to cycle through the pool. ENTER to confirm.
// Picking an already-assigned param = source slot becomes "empty".
// CC requires immediate CC# sub-input. PB is limited to one per context.
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
  uint8_t _context;        // 0=NORMAL, 1=ARPEG
  int8_t  _selectedSlot;   // -1=none, 0-7=selected
  int8_t  _poolCursorIdx;  // Index into pool list for current context
  bool    _ccSubMode;      // True when entering CC# for a CC assignment
  bool    _unsaved;        // True if working copy differs from saved
  PotMapping _ccPrevMapping; // B6: saved before entering CC sub-mode for cancel restore

  // Pool: list of assignable targets for each context
  // Built from the context's own param list + EMPTY + CC + PB
  static const uint8_t MAX_POOL = 12;
  PotTarget _pool[MAX_POOL];
  uint8_t   _poolCount;

  void buildPool();
  void drawScreen();
  void drawGrid();
  void drawPoolLine();
  void drawStatus();

  // Find which slot in current context has a given target (returns -1 if none)
  int8_t findSlotWithTarget(PotTarget t, uint8_t ccNum = 0) const;

  // Get current context map pointer
  PotMapping* currentMap();
  const PotMapping* currentMapConst() const;

  // Pot ADC detection (direct reads, not through PotRouter)
  uint16_t _potBaseline[4];
  void samplePotBaselines();
  int8_t detectMovedPot(bool btnLeftHeld);

  // Target name for display
  static const char* targetName(PotTarget t);

  // Save to NVS (direct Preferences call, blocking)
  bool saveMapping();
};

#endif // TOOL_POT_MAPPING_H
