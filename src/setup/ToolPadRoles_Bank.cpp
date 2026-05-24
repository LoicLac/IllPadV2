#include "ToolPadRoles.h"

// =================================================================
// Phase 3.B stub — page BANK (8 bank slots assignment).
// Delegates to _buildRoleMapLegacy until Phase 3.D incarnates the page.
// Future scope (3.D) : ENTER strict (no silent steal), ENTER sur bank
// assignée = dégage direct, pool 1 ligne, info panel langue musicien §8.5.
// =================================================================

void ToolPadRoles::_buildRoleMapBank() {
  _buildRoleMapLegacy();
}
