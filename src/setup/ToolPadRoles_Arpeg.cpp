#include "ToolPadRoles.h"
#include "SetupCommon.h"
#include "SetupUI.h"
#include "../core/KeyboardData.h"
#include <Arduino.h>
#include <string.h>

// =================================================================
// Phase 3.E — page ARPEG (Root × 7, Mode × 7, Chromatic, Octave × 4, PL/S).
//
// Spec §7.4 strict uniforme (adopté post HW Gate G3 Loïc) :
//   - ENTER sur rôle propre ARPEG → dégage direct (no pool ouvert).
//   - ENTER sur pad libre → ouvre pool 4 lignes (Root/Mode/Octave/PL/S).
//   - ENTER sur cell cross-page (BANK/CC absorbants) → no-op silencieux.
//   - LOOP voisin cross-AC → coexistence §7.3 (cell invisible " -- ", info
//     panel mentionne le voisin).
//   - Pool : §9.2 strict (no silent steal), retrait ligne [---] clear (mort
//     code post-§7.4 strict).
//   - Pool nav cap page-scoped : 4 lignes (Root/Mode/Octave/PL/S).
//   - Couleurs §11.1 : Root pêche, Mode cyan (+ Chromatic), Octave pourpre,
//     PL/S vert (unifié ARPEG+LOOP, §14.1 geste unifié).
// =================================================================

// =================================================================
// Statics labels (grid = pool §15.2) — migrés depuis ToolPadRoles.cpp legacy
// =================================================================
static const char* ARPEG_GRID_ROOT_LABELS[7]      = { "A",   "B",   "C",   "D",   "E",   "F",   "G"   };
static const char* ARPEG_GRID_MODE_LABELS[8]      = { "Ion", "Dor", "Phr", "Lyd", "Mix", "Aeo", "Loc", "Chr" };
static const char* ARPEG_GRID_OCTAVE_LABELS[4]    = { "Oct1", "Oct2", "Oct3", "Oct4" };
static const char* ARPEG_GRID_PLAY_STOP_LABELS[1] = { "P/S" };

// Modes long form (pour info panel langue musicien)
static const char* ARPEG_MODE_NAMES_LONG[8] = {
  "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian", "Chromatic"
};

// =================================================================
// _buildRoleMapArpeg — rôles ARPEG propres + §8.1 cross-page absorbants
// =================================================================
void ToolPadRoles::_buildRoleMapArpeg() {
  memset(_roleMap, ROLE_NONE, NUM_KEYS);
  for (int i = 0; i < NUM_KEYS; i++) memcpy(_roleLabels[i], " -- ", 5);

  // (1) Rôles ARPEG propres (priorité page ARPEG)
  for (uint8_t i = 0; i < 7; i++) {
    uint8_t pad = _wkRootPads[i];
    if (pad < NUM_KEYS) {
      _roleMap[pad] = ROLE_ROOT;
      strncpy(_roleLabels[pad], ARPEG_GRID_ROOT_LABELS[i], 5);
      _roleLabels[pad][5] = '\0';
    }
  }
  for (uint8_t i = 0; i < 7; i++) {
    uint8_t pad = _wkModePads[i];
    if (pad < NUM_KEYS) {
      _roleMap[pad] = ROLE_MODE;
      strncpy(_roleLabels[pad], ARPEG_GRID_MODE_LABELS[i], 5);
      _roleLabels[pad][5] = '\0';
    }
  }
  if (_wkChromPad < NUM_KEYS) {
    _roleMap[_wkChromPad] = ROLE_MODE;
    strncpy(_roleLabels[_wkChromPad], ARPEG_GRID_MODE_LABELS[7], 5);
    _roleLabels[_wkChromPad][5] = '\0';
  }
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t pad = _wkOctavePads[i];
    if (pad < NUM_KEYS) {
      _roleMap[pad] = ROLE_OCTAVE;
      strncpy(_roleLabels[pad], ARPEG_GRID_OCTAVE_LABELS[i], 5);
      _roleLabels[pad][5] = '\0';
    }
  }
  if (_wkArpPlayStopPad < NUM_KEYS) {
    _roleMap[_wkArpPlayStopPad] = ROLE_PLAY_STOP;
    strncpy(_roleLabels[_wkArpPlayStopPad], ARPEG_GRID_PLAY_STOP_LABELS[0], 5);
    _roleLabels[_wkArpPlayStopPad][5] = '\0';
  }

  // (2) §8.1 cross-page page ARPEG : absorbants (BANK/CC) affichent leur
  // label dim. CONTEXTUEL cross-AC LOOP → invisible " -- " (coexistence §7.3).
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    if (_roleMap[i] != ROLE_NONE) continue;
    PadNeighborInfo info = _padNeighborInfo(i);

    if (info.bankIdx >= 0) {
      snprintf(_roleLabels[i], 6, "Bk%u", (unsigned)(info.bankIdx + 1));
      _roleMap[i] = 7;
      continue;
    }
    if (info.hasCc) {
      int8_t ccSlot = findControlPadEntryIdx(_wkCc, i);
      if (ccSlot >= 0) {
        uint8_t cc = _wkCc.entries[ccSlot].ccNumber;
        if (cc < 100) snprintf(_roleLabels[i], 6, "CC%02u", (unsigned)cc);
        else          snprintf(_roleLabels[i], 6, "C%u",   (unsigned)cc);
        _roleMap[i] = 7;
      }
      continue;
    }
    // LOOP voisin → invisible §8.1 (cell reste " -- ")
  }
}

// =================================================================
// _clearRolesArpegOnly — helper page-scoped (§15.3, préserve cross-page)
// =================================================================
void ToolPadRoles::_clearRolesArpegOnly(uint8_t pad) {
  for (uint8_t i = 0; i < 7; i++) {
    if (_wkRootPads[i] == pad) _wkRootPads[i] = 0xFF;
  }
  for (uint8_t i = 0; i < 7; i++) {
    if (_wkModePads[i] == pad) _wkModePads[i] = 0xFF;
  }
  if (_wkChromPad == pad) _wkChromPad = 0xFF;
  for (uint8_t i = 0; i < 4; i++) {
    if (_wkOctavePads[i] == pad) _wkOctavePads[i] = 0xFF;
  }
  if (_wkArpPlayStopPad == pad) _wkArpPlayStopPad = 0xFF;
}

// =================================================================
// _handleEnterArpeg — ENTER grid (§7.4 strict uniforme)
// =================================================================
void ToolPadRoles::_handleEnterArpeg() {
  uint8_t pad = (uint8_t)(_gridRow * 12 + _gridCol);
  PadNeighborInfo info = _padNeighborInfo(pad);

  // §8.1 refus absorbants (no-op silencieux, modale 3.G arbitrera plus tard)
  if (info.bankIdx >= 0 || info.hasCc) return;
  // LOOP voisin → coexistence §7.3 (pas un refus, on continue)
  // (info.isLoopRec/PlayStop/Clear et info.loopSlotIdx ne bloquent PAS ARPEG)

  // §7.4 STRICT : pad porte rôle ARPEG propre → dégage direct (no pool).
  bool hasArpegRole = (info.scaleRole.kind != ScaleRoleKind::NONE)
                   || (info.arpRole.kind   != ArpRoleKind::NONE);
  if (hasArpegRole) {
    _clearRolesArpegOnly(pad);
    if (saveAll()) _ui->flashSaved();
    return;
  }

  // Pad libre (ou LOOP voisin coexistant) : ouvre pool ARPEG (4 lignes).
  // _poolLine = 2 (Root) par défaut, _poolIdx = 0 (premier élément).
  _editing = true;
  _poolLine = 2;
  _poolIdx = 0;
}

// =================================================================
// _handleEnterPoolArpeg — ENTER pool (§9.2 strict, no silent steal)
// =================================================================
void ToolPadRoles::_handleEnterPoolArpeg() {
  uint8_t pad = (uint8_t)(_gridRow * 12 + _gridCol);

  // Defensif : re-check absorbants (cas concurrent edit hypothétique)
  PadNeighborInfo info = _padNeighborInfo(pad);
  if (info.bankIdx >= 0 || info.hasCc) {
    _editing = false;
    return;
  }

  // §9.2 STRICT : refus si entry pool déjà assignée à un autre pad (no steal).
  // L'utilisateur doit d'abord dégager le rôle existant (§7.4) avant d'assigner.
  uint8_t roleOwnerPad = findPadWithRole(_poolLine, _poolIdx);
  if (roleOwnerPad < NUM_KEYS && roleOwnerPad != pad) {
    return;  // no-op, entry dim, ENTER ne fait rien
  }

  // Pad libre OU déjà le owner : assign direct.
  // Avant assign, nettoyer toute trace ARPEG existante sur ce pad (intra-AC).
  // Permet de "réassigner" un rôle existant sur le même pad sans collision.
  _clearRolesArpegOnly(pad);
  assignRole(pad, _poolLine, _poolIdx);

  if (saveAll()) _ui->flashSaved();
  _editing = false;
}

// =================================================================
// _applyDefaultsArpeg — §15.4 factory + §15.5 skip silencieux
// =================================================================
void ToolPadRoles::_applyDefaultsArpeg() {
  // Clear current ARPEG roles (only ARPEG, preserve cross-page)
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    _clearRolesArpegOnly(i);
  }

  // Apply factory defaults (legacy values) avec skip silencieux des pads
  // occupés par absorbants cross-page (placeholder modale 3.G).
  auto tryAssign = [&](uint8_t pad, uint8_t* slot) {
    if (pad >= NUM_KEYS) return;
    PadNeighborInfo info = _padNeighborInfo(pad);
    if (info.bankIdx >= 0 || info.hasCc) return;  // §15.5 skip silencieux
    *slot = pad;
  };

  // Roots A-G → pads 8-14 (legacy)
  for (uint8_t i = 0; i < 7; i++) tryAssign(8 + i,  &_wkRootPads[i]);
  // Modes Ion-Loc → pads 15-21 (legacy)
  for (uint8_t i = 0; i < 7; i++) tryAssign(15 + i, &_wkModePads[i]);
  // Chromatic → pad 22
  tryAssign(22, &_wkChromPad);
  // PL/S ARPEG → pad 23 (legacy hold pad position)
  tryAssign(23, &_wkArpPlayStopPad);
  // Octaves 1-4 → pads 25-28 (legacy)
  for (uint8_t i = 0; i < 4; i++) tryAssign(25 + i, &_wkOctavePads[i]);
}

// =================================================================
// _drawGridArpeg — call drawCellGrid GRID_ROLES (palette §11.1 + cross-page)
// =================================================================
void ToolPadRoles::_drawGridArpeg() {
  int selectedPad = _gridRow * 12 + _gridCol;
  _ui->drawCellGrid(GRID_ROLES, 0, nullptr, nullptr, nullptr, selectedPad,
                     0, false, nullptr, _roleLabels, _roleMap);
}

// =================================================================
// _drawPoolArpeg — 4 lignes (Root/Mode/Octave/PL/S), §15.1 PAS de [---] clear
// =================================================================
void ToolPadRoles::_drawPoolArpeg() {
  uint8_t cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);

  // Helper inline : dessine une ligne pool
  auto drawPoolLine = [&](uint8_t lineNum, const char* label,
                          const char* const* labels, uint8_t count,
                          const char* lineColor) {
    bool isSelectedLine = _editing && (_poolLine == lineNum);
    char buf[256];
    int pos = 0;

    if (isSelectedLine) {
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                      VT_CYAN VT_BOLD "> " VT_RESET "%-11s ", label);
    } else {
      pos += snprintf(buf + pos, sizeof(buf) - pos, "  %-11s ", label);
    }

    for (uint8_t i = 0; i < count; i++) {
      bool isCursor      = isSelectedLine && (_poolIdx == i);
      uint8_t owner      = findPadWithRole(lineNum, i);
      bool isAssigned    = (owner < NUM_KEYS);
      bool isOnCursorPad = isAssigned && (owner == cursorPad);

      if (isCursor) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        VT_REVERSE VT_BOLD " %s " VT_RESET " ", labels[i]);
      } else if (isAssigned && !isOnCursorPad) {
        // Entry assignée à autre pad : dim (§9.2 strict, ENTER no-op)
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        VT_DIM "%s" VT_RESET " ", labels[i]);
      } else {
        // Pool entry assignable (couleur de la catégorie)
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%s%s" VT_RESET " ", lineColor, labels[i]);
      }
    }
    _ui->drawFrameLine("%s", buf);
  };

  // 4 lignes catégories (Root/Mode incluant Chr/Octave/PL/S), couleurs §11.1
  drawPoolLine(2, "Root :",    ARPEG_GRID_ROOT_LABELS,      7, VT_PEACH);
  drawPoolLine(3, "Mode :",    ARPEG_GRID_MODE_LABELS,      8, VT_CYAN);
  drawPoolLine(4, "Octave :",  ARPEG_GRID_OCTAVE_LABELS,    4, VT_PURPLE);
  drawPoolLine(5, "Play/Stop:",ARPEG_GRID_PLAY_STOP_LABELS, 1, VT_GREEN);

  // §15.1 + ligne [---] clear ROLE supprimée (§7.4 strict : ENTER cell propre
  // = dégage direct, ligne pool redondante).
}

// =================================================================
// _drawInfoArpeg — langue musicien §8.5 + voisins cross-page
// =================================================================
void ToolPadRoles::_drawInfoArpeg() {
  // _confirmDefaults page-scoped (§15.1)
  if (_confirmDefaults) {
    _ui->drawFrameLine(VT_YELLOW "Restaurer les defauts de cette page ? (y/n)" VT_RESET);
    return;
  }
  if (_confirmClearAll) {
    _ui->drawFrameLine(VT_YELLOW "Clear ALL roles from all 48 pads? (y/n)" VT_RESET);
    return;
  }

  uint8_t pad = (uint8_t)(_gridRow * 12 + _gridCol);
  PadNeighborInfo info = _padNeighborInfo(pad);

  // Cas 1 : pad porte CC absorbant cross-page — interdit pour ARPEG.
  if (info.hasCc) {
    int8_t ccSlot = findControlPadEntryIdx(_wkCc, pad);
    if (ccSlot >= 0) {
      _ui->drawFrameLine(VT_YELLOW "Pad #%d : CC %u (page CC, absorbant) - cannot assign ARPEG here." VT_RESET,
                         (int)pad + 1, (unsigned)_wkCc.entries[ccSlot].ccNumber);
      _ui->drawFrameLine(VT_DIM "Move CC %u in page CC first to free this pad." VT_RESET,
                         (unsigned)_wkCc.entries[ccSlot].ccNumber);
    }
    return;
  }

  // Cas 2 : pad porte BANK absorbant — interdit pour ARPEG.
  if (info.bankIdx >= 0) {
    _ui->drawFrameLine(VT_YELLOW "Pad #%d : Bank %d (page BANK, absorbant) - cannot assign ARPEG here." VT_RESET,
                       (int)pad + 1, info.bankIdx + 1);
    _ui->drawFrameLine(VT_DIM "Move Bank %d in page BANK first to free this pad." VT_RESET,
                       info.bankIdx + 1);
    return;
  }

  // Cas 3 : pad porte rôle ARPEG propre — wording langue musicien §15.2.
  if (info.scaleRole.kind == ScaleRoleKind::ROOT) {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : Root %s",
                       (int)pad + 1, ARPEG_GRID_ROOT_LABELS[info.scaleRole.idx]);
    _ui->drawFrameLine(VT_DIM "[RET] to clear this Root assignment (§7.4 dégage direct)." VT_RESET);
    return;
  }
  if (info.scaleRole.kind == ScaleRoleKind::MODE) {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : Mode %s (%s)",
                       (int)pad + 1,
                       ARPEG_MODE_NAMES_LONG[info.scaleRole.idx],
                       ARPEG_GRID_MODE_LABELS[info.scaleRole.idx]);
    _ui->drawFrameLine(VT_DIM "[RET] to clear this Mode assignment (§7.4 dégage direct)." VT_RESET);
    return;
  }
  if (info.scaleRole.kind == ScaleRoleKind::CHROM) {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : Chromatic",
                       (int)pad + 1);
    _ui->drawFrameLine(VT_DIM "[RET] to clear this Chromatic assignment (§7.4 dégage direct)." VT_RESET);
    return;
  }
  if (info.arpRole.kind == ArpRoleKind::OCTAVE) {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : Octave %u",
                       (int)pad + 1, (unsigned)(info.arpRole.idx + 1));
    _ui->drawFrameLine(VT_DIM "[RET] to clear this Octave assignment (§7.4 dégage direct)." VT_RESET);
    return;
  }
  if (info.arpRole.kind == ArpRoleKind::PLAY_STOP) {
    // PL/S unifié §14.1 — détection partage avec LOOP PS
    bool sharedWithLoop = (_wkArpPlayStopPad == _wkLoopPad.playStopPad)
                       && (_wkLoopPad.playStopPad < NUM_KEYS);
    if (sharedWithLoop) {
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : Play/Stop ARPEG + LOOP (geste unifié §14.1)",
                         (int)pad + 1);
    } else {
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : Play/Stop ARPEG",
                         (int)pad + 1);
    }
    _ui->drawFrameLine(VT_DIM "[RET] to clear this PL/S assignment (§7.4 dégage direct)." VT_RESET);
    return;
  }

  // Cas 4 : pad porte voisin LOOP (coexistence §7.3) — affichage informatif.
  bool hasLoopNeighbor = info.isLoopRec || info.isLoopPlayStop || info.isLoopClear
                      || (info.loopSlotIdx >= 0);
  if (hasLoopNeighbor) {
    const char* loopRole = info.isLoopRec      ? "LOOP REC"
                         : info.isLoopPlayStop ? "LOOP PS"
                         : info.isLoopClear    ? "LOOP CLR"
                                               : "LOOP Slot";
    if (info.loopSlotIdx >= 0) {
      _ui->drawFrameLine(VT_DIM "Pad #%d : libre pour ARPEG (porte aussi Slot %u, coexistence §7.3)." VT_RESET,
                         (int)pad + 1, (unsigned)(info.loopSlotIdx + 1));
    } else {
      _ui->drawFrameLine(VT_DIM "Pad #%d : libre pour ARPEG (porte aussi %s, coexistence §7.3)." VT_RESET,
                         (int)pad + 1, loopRole);
    }
    _ui->drawFrameLine(VT_DIM "[RET] to open pool. ARPEG + LOOP peuvent coexister sur ce pad." VT_RESET);
    return;
  }

  // Cas 5 : pad libre — invite à assigner.
  _ui->drawFrameLine(VT_DIM "Pad #%d : libre pour ARPEG. [RET] to open pool." VT_RESET,
                     (int)pad + 1);
  _ui->drawFrameEmpty();
}

// =================================================================
// _drawControlBarArpeg — controls bar selon état
// =================================================================
void ToolPadRoles::_drawControlBarArpeg() {
  if (_confirmDefaults || _confirmClearAll) {
    _ui->drawControlBar(CBAR_CONFIRM_ANY);
    return;
  }

  if (_editing) {
    _ui->drawControlBar(VT_DIM "[^v<>] BROWSE" CBAR_SEP "[RET] ASSIGN" CBAR_SEP "[q] CANCEL" VT_RESET);
  } else {
    _ui->drawControlBar(VT_DIM "[^v<>] NAV  [TOUCH] JUMP" CBAR_SEP "[RET] ASSIGN/CLEAR  [d] DFLT" CBAR_SEP "[q] EXIT" VT_RESET);
  }
}

// =================================================================
// _drawPageArpeg — orchestrateur sections page ARPEG
// (PAS de vtFrameStart/End ni drawConsoleHeader : déjà émis par drawScreen)
// =================================================================
void ToolPadRoles::_drawPageArpeg() {
  _ui->drawSection("GRID");
  _drawGridArpeg();
  _ui->drawFrameEmpty();

  _ui->drawSection("POOL");
  _drawPoolArpeg();
  _ui->drawFrameEmpty();

  _ui->drawSection("INFO");
  _drawInfoArpeg();
  _ui->drawFrameEmpty();

  _drawFlash();
  _drawControlBarArpeg();
}
