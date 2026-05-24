#include "ToolPadRoles.h"

// =================================================================
// Phase 3.B stub — page ARPEG (Root × 7, Mode × 7, Chromatic, Octave × 4,
// PL/S ARPEG). Delegates to _buildRoleMapLegacy until Phase 3.E incarnates
// the page (3.E.1 rendu, 3.E.2 matrice édition + swap intra-AC silencieux,
// 3.E.3 coexistence cross-AC LOOP + info panel langue musicien complet).
// =================================================================

void ToolPadRoles::_buildRoleMapArpeg() {
  _buildRoleMapLegacy();
}
