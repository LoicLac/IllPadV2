#include "ToolPadRoles.h"

// =================================================================
// Phase 3.B stub — page LOOP (REC, PL/S, CLR, Slots × 16). Delegates to
// _buildRoleMapLegacy until Phase 3.F incarnates the page (3.F.1 rendu,
// 3.F.2 matrice édition + swap intra-AC silencieux, 3.F.3 coexistence
// cross-AC ARPEG ↔ LOOP testable end-to-end + mini-audit binôme 3.E.3/3.F.3).
// =================================================================

void ToolPadRoles::_buildRoleMapLoop() {
  _buildRoleMapLegacy();
}
