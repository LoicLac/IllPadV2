#include "ToolPadRoles.h"
#include "SetupCommon.h"
#include "SetupUI.h"
#include "../core/KeyboardData.h"
#include "../managers/NvsManager.h"
#include <Arduino.h>
#include <string.h>

// =================================================================
// Phase 3.F — page LOOP (REC × 1, PL/S × 1, CLR × 1, Slots × 16).
//
// Spec §7.4 strict uniforme (adopté post HW Gate G3 Loïc) :
//   - ENTER sur rôle propre LOOP → dégage direct (no pool ouvert).
//   - ENTER sur pad libre → ouvre pool 4 lignes (REC/PS/CLR/Slots).
//   - ENTER sur cell cross-page (BANK/CC absorbants) → no-op silencieux.
//   - ARPEG voisin cross-AC → coexistence §7.3 (cell invisible " -- ", info
//     panel mentionne le voisin).
//   - Pool : §9.2 strict (no silent steal), retrait ligne [---] clear.
//   - Pool nav range page-scoped : [6..9].
//   - Couleurs §11.1 : REC rouge, PS vert (unifié ARPEG+LOOP §14.1),
//     CLR bleu foncé, Slots jaune.
//   - PL/S unifié §14.1 : même pad LOOP+ARPEG = geste musical commun.
// =================================================================

// =================================================================
// Statics labels (grid = pool §15.2)
// =================================================================
static const char* LOOP_GRID_REC_LABELS[1]   = { "REC" };
static const char* LOOP_GRID_PS_LABELS[1]    = { "P/S" };  // unifié ARPEG (§11.1/§14.1)
static const char* LOOP_GRID_CLR_LABELS[1]   = { "CLR" };
static const char* LOOP_GRID_SLOT_LABELS[16] = {
  "S0",  "S1",  "S2",  "S3",  "S4",  "S5",  "S6",  "S7",
  "S8",  "S9",  "S10", "S11", "S12", "S13", "S14", "S15"
};

// =================================================================
// _buildRoleMapLoop — rôles LOOP propres + §8.1 cross-page absorbants
// =================================================================
void ToolPadRoles::_buildRoleMapLoop() {
  memset(_roleMap, ROLE_NONE, NUM_KEYS);
  for (int i = 0; i < NUM_KEYS; i++) memcpy(_roleLabels[i], " -- ", 5);

  // (1) Rôles LOOP propres (priorité page LOOP)
  if (_wkLoopPad.recPad < NUM_KEYS) {
    _roleMap[_wkLoopPad.recPad] = ROLE_REC;
    strncpy(_roleLabels[_wkLoopPad.recPad], LOOP_GRID_REC_LABELS[0], 5);
    _roleLabels[_wkLoopPad.recPad][5] = '\0';
  }
  if (_wkLoopPad.playStopPad < NUM_KEYS) {
    _roleMap[_wkLoopPad.playStopPad] = ROLE_PLAY_STOP;
    strncpy(_roleLabels[_wkLoopPad.playStopPad], LOOP_GRID_PS_LABELS[0], 5);
    _roleLabels[_wkLoopPad.playStopPad][5] = '\0';
  }
  if (_wkLoopPad.clearPad < NUM_KEYS) {
    _roleMap[_wkLoopPad.clearPad] = ROLE_CLR;
    strncpy(_roleLabels[_wkLoopPad.clearPad], LOOP_GRID_CLR_LABELS[0], 5);
    _roleLabels[_wkLoopPad.clearPad][5] = '\0';
  }
  for (uint8_t i = 0; i < 16; i++) {
    uint8_t pad = _wkLoopPad.slotPads[i];
    if (pad < NUM_KEYS) {
      _roleMap[pad] = ROLE_SLOT;
      strncpy(_roleLabels[pad], LOOP_GRID_SLOT_LABELS[i], 5);
      _roleLabels[pad][5] = '\0';
    }
  }

  // (2) §8.1 cross-page page LOOP : absorbants (BANK/CC) affichent leur
  // label dim. CONTEXTUEL cross-AC ARPEG → invisible " -- " (coexistence §7.3).
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
    // ARPEG voisin → invisible §8.1 (cell reste " -- ", info panel mentionne)
  }
}

// =================================================================
// _clearRolesLoopOnly — §15.3 helper page-scoped (préserve cross-page)
// =================================================================
void ToolPadRoles::_clearRolesLoopOnly(uint8_t pad) {
  if (_wkLoopPad.recPad      == pad) _wkLoopPad.recPad      = 0xFF;
  if (_wkLoopPad.playStopPad == pad) _wkLoopPad.playStopPad = 0xFF;
  if (_wkLoopPad.clearPad    == pad) _wkLoopPad.clearPad    = 0xFF;
  for (uint8_t i = 0; i < 16; i++) {
    if (_wkLoopPad.slotPads[i] == pad) _wkLoopPad.slotPads[i] = 0xFF;
  }
}

// =================================================================
// _handleEnterLoop — ENTER grid (§7.4 strict uniforme)
// =================================================================
void ToolPadRoles::_handleEnterLoop() {
  uint8_t pad = (uint8_t)(_gridRow * 12 + _gridCol);
  PadNeighborInfo info = _padNeighborInfo(pad);

  // §8.1 refus absorbants (no-op silencieux)
  if (info.bankIdx >= 0 || info.hasCc) return;

  // §7.4 STRICT : pad porte rôle LOOP propre → dégage direct (no pool).
  bool hasLoopRole = info.isLoopRec || info.isLoopPlayStop || info.isLoopClear
                  || (info.loopSlotIdx >= 0);
  if (hasLoopRole) {
    _clearRolesLoopOnly(pad);
    if (saveAll()) _ui->flashSaved();
    return;
  }

  // Pad libre (ou ARPEG voisin coexistant) : ouvre pool LOOP.
  // _poolLine = 6 (REC) par défaut, _poolIdx = 0.
  _editing = true;
  _poolLine = 6;
  _poolIdx = 0;
}

// =================================================================
// _handleEnterPoolLoop — ENTER pool (§9.2 strict, no silent steal)
// =================================================================
void ToolPadRoles::_handleEnterPoolLoop() {
  uint8_t pad = (uint8_t)(_gridRow * 12 + _gridCol);

  // Defensif : re-check absorbants
  PadNeighborInfo info = _padNeighborInfo(pad);
  if (info.bankIdx >= 0 || info.hasCc) {
    _editing = false;
    return;
  }

  // §9.2 STRICT : refus si entry pool déjà assignée à un autre pad (no steal).
  uint8_t roleOwnerPad = 0xFF;
  switch (_poolLine) {
    case 6: roleOwnerPad = _wkLoopPad.recPad;      break;
    case 7: roleOwnerPad = _wkLoopPad.playStopPad; break;
    case 8: roleOwnerPad = _wkLoopPad.clearPad;    break;
    case 9: if (_poolIdx < 16) roleOwnerPad = _wkLoopPad.slotPads[_poolIdx]; break;
    default: _editing = false; return;
  }
  if (roleOwnerPad < NUM_KEYS && roleOwnerPad != pad) {
    return;  // no-op, entry dim
  }

  // Avant assign, libère le pad cible des autres rôles LOOP (intra-AC).
  _clearRolesLoopOnly(pad);

  // Assigne nouveau rôle LOOP. §7.3 coexistence cross-AC : ne touche pas
  // aux structures ARPEG (_wkRootPads, _wkOctavePads, etc.).
  switch (_poolLine) {
    case 6: _wkLoopPad.recPad      = pad; break;
    case 7: _wkLoopPad.playStopPad = pad; break;
    case 8: _wkLoopPad.clearPad    = pad; break;
    case 9: if (_poolIdx < 16) _wkLoopPad.slotPads[_poolIdx] = pad; break;
  }

  if (saveAll()) _ui->flashSaved();
  _editing = false;
}

// =================================================================
// _applyDefaultsLoop — §15.4 factory legacy + §15.5 skip silencieux
// =================================================================
void ToolPadRoles::_applyDefaultsLoop() {
  // Clear current LOOP roles (page-scoped)
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    _clearRolesLoopOnly(i);
  }

  // Factory defaults legacy : REC=32, PS=33, CLR=34, Slots vides.
  // §15.5 skip silencieux : pads occupés par absorbants (BANK/CC) ou
  // contextuels ARPEG sont skipped (choix conservateur — coexistence §7.3
  // possible mais user crée manuellement post-`d`).
  auto tryAssign = [&](uint8_t pad, uint8_t* slot) {
    if (pad >= NUM_KEYS) return;
    PadNeighborInfo info = _padNeighborInfo(pad);
    if (info.bankIdx >= 0 || info.hasCc) return;  // skip absorbants
    if (info.scaleRole.kind != ScaleRoleKind::NONE
        || info.arpRole.kind != ArpRoleKind::NONE) {
      return;  // skip contextuels ARPEG (choix conservateur §15.5)
    }
    *slot = pad;
  };

  tryAssign(32, &_wkLoopPad.recPad);
  tryAssign(33, &_wkLoopPad.playStopPad);
  tryAssign(34, &_wkLoopPad.clearPad);
  // Slots 0..15 restent 0xFF (clearés par _clearRolesLoopOnly ci-dessus).
}

// =================================================================
// _drawGridLoop — drawCellGrid GRID_ROLES (palette §11.1 codes 9/10/11)
// =================================================================
void ToolPadRoles::_drawGridLoop() {
  int selectedPad = _gridRow * 12 + _gridCol;
  _ui->drawCellGrid(GRID_ROLES, 0, nullptr, nullptr, nullptr, selectedPad,
                     0, false, nullptr, _roleLabels, _roleMap);
}

// =================================================================
// _drawPoolLoop — 4 lignes (REC/PS/CLR/Slots), §15.1 PAS de [---] clear
// =================================================================
void ToolPadRoles::_drawPoolLoop() {
  uint8_t cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);

  auto drawPoolLine = [&](uint8_t lineNum, const char* label,
                          const char* const* labels, uint8_t count,
                          const char* lineColor,
                          uint8_t ownerPadOrSentinel,  // for single-entry lines
                          bool useSlotsArray) {
    bool isSelectedLine = _editing && (_poolLine == lineNum);
    char buf[512];
    int pos = 0;

    if (isSelectedLine) {
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                      VT_CYAN VT_BOLD "> " VT_RESET "%-11s ", label);
    } else {
      pos += snprintf(buf + pos, sizeof(buf) - pos, "  %-11s ", label);
    }

    for (uint8_t i = 0; i < count; i++) {
      bool isCursor = isSelectedLine && (_poolIdx == i);

      // Compute owner for this entry
      uint8_t owner = 0xFF;
      if (useSlotsArray) {
        if (i < 16) owner = _wkLoopPad.slotPads[i];
      } else {
        owner = ownerPadOrSentinel;
      }
      bool isAssigned    = (owner < NUM_KEYS);
      bool isOnCursorPad = isAssigned && (owner == cursorPad);

      if (isCursor) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        VT_REVERSE VT_BOLD " %s " VT_RESET " ", labels[i]);
      } else if (isAssigned && !isOnCursorPad) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        VT_DIM "%s" VT_RESET " ", labels[i]);
      } else {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%s%s" VT_RESET " ", lineColor, labels[i]);
      }
    }
    _ui->drawFrameLine("%s", buf);
  };

  // 4 lignes catégories couleurs §11.1 (REC rouge, PS vert, CLR bleu foncé, Slots jaune)
  drawPoolLine(6, "REC :",       LOOP_GRID_REC_LABELS,  1, VT_RED,
               _wkLoopPad.recPad, false);
  drawPoolLine(7, "Play/Stop :", LOOP_GRID_PS_LABELS,   1, VT_GREEN,
               _wkLoopPad.playStopPad, false);
  drawPoolLine(8, "CLR :",       LOOP_GRID_CLR_LABELS,  1, VT_DARK_BLUE,
               _wkLoopPad.clearPad, false);
  drawPoolLine(9, "Slots :",     LOOP_GRID_SLOT_LABELS, 16, VT_YELLOW,
               0, true);

  // §15.1 + ligne [---] clear ROLE supprimée (§7.4 strict uniforme).
}

// =================================================================
// _drawInfoLoop — langue musicien §8.5 + voisins cross-page + PL/S unifié §14.1
// =================================================================
void ToolPadRoles::_drawInfoLoop() {
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

  // Cas 1 : pad porte CC absorbant cross-page — interdit pour LOOP.
  if (info.hasCc) {
    int8_t ccSlot = findControlPadEntryIdx(_wkCc, pad);
    if (ccSlot >= 0) {
      _ui->drawFrameLine(VT_YELLOW "Pad #%d : CC %u (page CC, absorbant) - cannot assign LOOP here." VT_RESET,
                         (int)pad + 1, (unsigned)_wkCc.entries[ccSlot].ccNumber);
      _ui->drawFrameLine(VT_DIM "Move CC %u in page CC first to free this pad." VT_RESET,
                         (unsigned)_wkCc.entries[ccSlot].ccNumber);
    }
    return;
  }

  // Cas 2 : pad porte BANK absorbant — interdit pour LOOP.
  if (info.bankIdx >= 0) {
    _ui->drawFrameLine(VT_YELLOW "Pad #%d : Bank %d (page BANK, absorbant) - cannot assign LOOP here." VT_RESET,
                       (int)pad + 1, info.bankIdx + 1);
    _ui->drawFrameLine(VT_DIM "Move Bank %d in page BANK first to free this pad." VT_RESET,
                       info.bankIdx + 1);
    return;
  }

  // Cas 3 : pad porte rôle LOOP propre — wording langue musicien §15.2.
  if (info.isLoopRec) {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : REC LOOP — record toggle bank courant",
                       (int)pad + 1);
    _ui->drawFrameLine(VT_DIM "[RET] to clear this REC assignment (§7.4 dégage direct)." VT_RESET);
    return;
  }
  if (info.isLoopPlayStop) {
    // PL/S unifié §14.1 — détection partage avec ARPEG PS
    bool sharedWithArpeg = (_wkLoopPad.playStopPad == _wkArpPlayStopPad)
                        && (_wkArpPlayStopPad < NUM_KEYS);
    if (sharedWithArpeg) {
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : Play/Stop LOOP + ARPEG (geste unifié §14.1)",
                         (int)pad + 1);
    } else {
      _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : Play/Stop LOOP",
                         (int)pad + 1);
    }
    _ui->drawFrameLine(VT_DIM "[RET] to clear this PL/S assignment (§7.4 dégage direct)." VT_RESET);
    return;
  }
  if (info.isLoopClear) {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : CLEAR LOOP — long-press wipe bank courant",
                       (int)pad + 1);
    _ui->drawFrameLine(VT_DIM "[RET] to clear this CLR assignment (§7.4 dégage direct)." VT_RESET);
    return;
  }
  if (info.loopSlotIdx >= 0) {
    // F11 audit Q2-F1 : prévenir user que Slots = Phase 6 runtime
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : Slot %d LOOP (UI configurée, runtime Phase 6 Slot Drive)",
                       (int)pad + 1, info.loopSlotIdx);
    _ui->drawFrameLine(VT_DIM "[RET] to clear this Slot assignment (§7.4 dégage direct)." VT_RESET);
    return;
  }

  // Cas 4 : pad porte voisin ARPEG (coexistence §7.3) — affichage informatif.
  bool hasArpegNeighbor = (info.scaleRole.kind != ScaleRoleKind::NONE)
                       || (info.arpRole.kind   != ArpRoleKind::NONE);
  if (hasArpegNeighbor) {
    char roleName[80] = {0};
    _formatRoleNameMusician(info, roleName, sizeof(roleName));
    _ui->drawFrameLine(VT_DIM "Pad #%d : libre pour LOOP (porte aussi %s, coexistence §7.3)." VT_RESET,
                       (int)pad + 1, roleName);
    _ui->drawFrameLine(VT_DIM "[RET] to open pool. ARPEG + LOOP peuvent coexister sur ce pad." VT_RESET);
    return;
  }

  // Cas 5 : pad libre — invite à assigner.
  _ui->drawFrameLine(VT_DIM "Pad #%d : libre pour LOOP. [RET] to open pool." VT_RESET,
                     (int)pad + 1);
  _ui->drawFrameEmpty();
}

// =================================================================
// _drawControlBarLoop — controls bar selon état
// =================================================================
void ToolPadRoles::_drawControlBarLoop() {
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
// _drawPageLoop — orchestrateur sections page LOOP
// =================================================================
void ToolPadRoles::_drawPageLoop() {
  _ui->drawSection("GRID");
  _drawGridLoop();
  _ui->drawFrameEmpty();

  _ui->drawSection("POOL");
  _drawPoolLoop();
  _ui->drawFrameEmpty();

  _ui->drawSection("INFO");
  _drawInfoLoop();
  _ui->drawFrameEmpty();

  _drawFlash();
  _drawControlBarLoop();
}
