#include "ToolPadRoles.h"

// =================================================================
// Phase 3.B stub — page CC (MIDI CC ControlPads, absorbs Tool 4).
// Delegates to _buildRoleMapLegacy until Phase 3.C incarnates the page
// (3.C.1a migration mécanique Tool 4 → ToolPadRoles_Cc, 3.C.1b câblage
// orchestrateur, 3.C.2 cell display §8.1 + helpers cross-store).
// =================================================================

void ToolPadRoles::_buildRoleMapCc() {
  _buildRoleMapLegacy();
}
