#include "ToolPadRoles.h"
#include "SetupCommon.h"
#include "SetupUI.h"
#include "../core/KeyboardData.h"
#include <Arduino.h>
#include <string.h>

// =================================================================
// Phase 3.D — page BANK (8 bank slots assignment).
//
// Spec §7.4 strict : ENTER sur bank assignée = dégage direct (no pool ouvert).
// Spec §9.2 strict : pool ENTER sur entry déjà assignée = no-op (no silent steal).
// Spec §6.5      : hard-constraint exit (8 banks toutes assignées avant exit tool).
// Spec §8.1      : cell display cross-page (CC absorbant ambre+, contextuels :: neutre).
// Spec §15.2     : labels grid = labels pool ("Bk1".."Bk8").
// Spec §15.3     : clear page-scoped via _clearRolesBankOnly (préserve cross-page).
// Spec §15.4-§15.5 : _applyDefaultsBank skip silencieux si pad occupé cross-page.
// =================================================================

// =================================================================
// _buildRoleMapBank — rôles propres + §8.1 cross-page (3.D.2)
// =================================================================
void ToolPadRoles::_buildRoleMapBank() {
  memset(_roleMap, ROLE_NONE, NUM_KEYS);
  for (int i = 0; i < NUM_KEYS; i++) memcpy(_roleLabels[i], " -- ", 5);

  // (1) Bank rôles propres
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    uint8_t pad = _wkBankPads[i];
    if (pad < NUM_KEYS) {
      _roleMap[pad] = ROLE_BANK;
      snprintf(_roleLabels[pad], 6, "Bk%u", (unsigned)(i + 1));  // §15.2
    }
  }

  // (2) §8.1 cross-page : pads sans bank propre montrent les voisins
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    if (_roleMap[i] != ROLE_NONE) continue;
    PadNeighborInfo info = _padNeighborInfo(i);

    // CC absorbant cross-page → "CC<n>" dim ambre+ (map=7)
    if (info.hasCc) {
      int8_t ccSlot = findControlPadEntryIdx(_wkCc, i);
      if (ccSlot >= 0) {
        uint8_t cc = _wkCc.entries[ccSlot].ccNumber;
        if (cc < 100) snprintf(_roleLabels[i], 6, "CC%02u", (unsigned)cc);
        else          snprintf(_roleLabels[i], 6, "C%u",    (unsigned)cc);
        _roleMap[i] = 7;
      }
      continue;
    }

    // Contextuels ARPEG (Root/Mode/Chrom/Octave/PL/S) OU LOOP slot OU LOOP
    // control (REC/PS/CLR) → " :: " neutre (map=8, placeholder ASCII 3.H.1).
    bool hasContextual = (info.scaleRole.kind != ScaleRoleKind::NONE)
                       || (info.arpRole.kind  != ArpRoleKind::NONE)
                       || (info.loopSlotIdx >= 0)
                       || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear;
    if (hasContextual) {
      strncpy(_roleLabels[i], VT_NEUTRAL_BAR, 5);
      _roleLabels[i][5] = '\0';
      _roleMap[i] = 8;
    }
  }
}

// =================================================================
// _handleEnterBank — ENTER grid nav (§7.4 strict)
// =================================================================
void ToolPadRoles::_handleEnterBank() {
  uint8_t pad = (uint8_t)(_gridRow * 12 + _gridCol);
  PadNeighborInfo info = _padNeighborInfo(pad);

  // CC absorbant → refus permanent (§4 ABSORBANT exclusif, jamais de modale).
  if (info.hasCc) return;

  // §7.4 STRICT : pad porte bank propre → dégage direct (no pool ouvert).
  if (info.bankIdx >= 0) {
    _wkBankPads[info.bankIdx] = 0xFF;
    if (saveAll()) _ui->flashSaved();
    return;
  }

  // Phase 3.G.2 — pad vide OU portant contextuels (ARPEG/LOOP) : ouvre pool.
  // L'user choisit la bank ; la modale d'écrasement §10 apparaîtra à l'ENTER
  // pool si le pad porte des contextuels (cf _handleEnterPoolBank).
  _editing = true;
  _poolLine = 1;
  _poolIdx = 0;
}

// =================================================================
// _handleEnterPoolBank — ENTER pool nav (§9.2 strict, no silent steal)
// =================================================================
void ToolPadRoles::_handleEnterPoolBank() {
  uint8_t pad = (uint8_t)(_gridRow * 12 + _gridCol);

  // [---] clear (poolLine == 0) → §15.3 page-scoped clear (préserve cross-page).
  if (_poolLine == 0) {
    _clearRolesBankOnly(pad);
    if (saveAll()) _ui->flashSaved();
    _editing = false;
    return;
  }

  // §9.2 STRICT : refus si entry pool déjà assignée à un autre pad (no silent steal).
  uint8_t targetBank = _poolIdx;
  if (targetBank >= NUM_BANKS) return;
  if (_wkBankPads[targetBank] < NUM_KEYS && _wkBankPads[targetBank] != pad) {
    return;  // no-op, entry dim, ENTER ne fait rien
  }

  // Phase 3.G.2 — défensif : CC absorbant = refus permanent (jamais de modale).
  PadNeighborInfo info = _padNeighborInfo(pad);
  if (info.hasCc) {
    _editing = false;
    return;
  }

  // Phase 3.G.2 — contextuels (ARPEG/LOOP incl. LOOP control) → modale §10.
  bool hasContextual = (info.scaleRole.kind != ScaleRoleKind::NONE)
                    || (info.arpRole.kind   != ArpRoleKind::NONE)
                    || (info.loopSlotIdx >= 0)
                    || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear;
  if (hasContextual) {
    _pendingOverwrite.action  = OVERWRITE_ACTION_BANK_ASSIGN;
    _pendingOverwrite.pad     = pad;
    _pendingOverwrite.bankIdx = targetBank;
    _confirmOverwrite = true;
    _editing = false;  // sortie pool, modale prend le relais
    return;
  }

  _wkBankPads[targetBank] = pad;
  if (saveAll()) _ui->flashSaved();
  _editing = false;
}

// =================================================================
// _applyDefaultsBank — §15.4 factory + §15.5 skip silencieux
// =================================================================
void ToolPadRoles::_applyDefaultsBank() {
  // Clear current bank assignments
  for (uint8_t i = 0; i < NUM_BANKS; i++) _wkBankPads[i] = 0xFF;

  // Apply factory defaults (bank i → pad i) avec skip silencieux des pads
  // occupés par CC ou contextuels (placeholder modale 3.G).
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    uint8_t pad = i;
    PadNeighborInfo info = _padNeighborInfo(pad);

    // Skip CC absorbant (ABSORBANT + ABSORBANT incompatible)
    if (info.hasCc) continue;

    // Skip contextuels (modale arbitrage à venir 3.G)
    if (info.scaleRole.kind != ScaleRoleKind::NONE
        || info.arpRole.kind  != ArpRoleKind::NONE
        || info.loopSlotIdx >= 0
        || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear) {
      continue;
    }

    _wkBankPads[i] = pad;
  }
  // Note : hard-constraint exit §6.5 refusera l'exit du tool tant que les
  // banks manquantes (après skip) ne sont pas re-saisies manuellement.
}

// =================================================================
// _clearRolesBankOnly — §15.3 helper page-scoped (préserve cross-page)
// =================================================================
void ToolPadRoles::_clearRolesBankOnly(uint8_t pad) {
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (_wkBankPads[i] == pad) _wkBankPads[i] = 0xFF;
  }
}

// =================================================================
// _drawGridBank — call drawCellGrid GRID_ROLES (palette étendue 3.D.2)
// =================================================================
void ToolPadRoles::_drawGridBank() {
  int selectedPad = _gridRow * 12 + _gridCol;
  _ui->drawCellGrid(GRID_ROLES, 0, nullptr, nullptr, nullptr, selectedPad,
                     0, false, nullptr, _roleLabels, _roleMap);
}

// =================================================================
// _drawPoolBank — 1 ligne "Bank: Bk1 Bk2 ... Bk8" vert menthe / dim / cursor
// =================================================================
void ToolPadRoles::_drawPoolBank() {
  bool isSelected = _editing && (_poolLine == 1);
  uint8_t cursorPad = (uint8_t)(_gridRow * 12 + _gridCol);
  char buf[256];
  int pos = 0;

  if (isSelected) {
    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    VT_CYAN VT_BOLD "> " VT_RESET "%-11s ", "Bank :");
  } else {
    pos += snprintf(buf + pos, sizeof(buf) - pos, "  %-11s ", "Bank :");
  }

  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    char label[8];
    snprintf(label, sizeof(label), "Bk%u", (unsigned)(i + 1));
    bool isCursor      = isSelected && (_poolIdx == i);
    bool isAssigned    = (_wkBankPads[i] < NUM_KEYS);
    bool isOnCursorPad = isAssigned && (_wkBankPads[i] == cursorPad);

    if (isCursor) {
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                      VT_REVERSE VT_BOLD " %s " VT_RESET " ", label);
    } else if (isAssigned && !isOnCursorPad) {
      // Entry assignée à un autre pad : dim (§9.2 strict, ENTER no-op)
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                      VT_DIM "%s" VT_RESET " ", label);
    } else {
      // Pool entry assignable (vert menthe) ou portée par cursor courant
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                      VT_MINT_GREEN "%s" VT_RESET " ", label);
    }
  }
  _ui->drawFrameLine("%s", buf);

  // Clear action at the bottom (§15.3 page-scoped, redondant avec §7.4 dégage
  // direct mais conservé pour cohérence cross-page).
  bool clearSelected = _editing && (_poolLine == 0);
  if (clearSelected) {
    _ui->drawFrameLine(VT_CYAN VT_BOLD "> " VT_RESET VT_DIM "[---] clear bank role on this pad" VT_RESET);
  } else {
    _ui->drawFrameLine("  " VT_DIM "[---] clear bank role on this pad" VT_RESET);
  }
}

// =================================================================
// _drawInfoBank — langue musicien §8.5 + prompts confirmations page-scoped
// =================================================================
void ToolPadRoles::_drawInfoBank() {
  // _confirmDefaults page-scoped : wording §15.1 "cette page"
  if (_confirmDefaults) {
    _ui->drawFrameLine(VT_YELLOW "Restaurer les defauts de cette page ? (y/n)" VT_RESET);
    return;
  }

  // _confirmClearAll : 'r' supprimé en page BANK §15.1. Safety si jamais déclenché.
  if (_confirmClearAll) {
    _ui->drawFrameLine(VT_YELLOW "Clear ALL roles from all 48 pads? (y/n)" VT_RESET);
    return;
  }

  uint8_t pad = (uint8_t)(_gridRow * 12 + _gridCol);
  PadNeighborInfo info = _padNeighborInfo(pad);

  // Cas 1 : pad porte CC absorbant cross-page — interdit pour BANK.
  if (info.hasCc) {
    int8_t ccSlot = findControlPadEntryIdx(_wkCc, pad);
    if (ccSlot >= 0) {
      _ui->drawFrameLine(VT_YELLOW "Pad #%d : CC %u (page CC, absorbant) - cannot assign Bank here." VT_RESET,
                         (int)pad + 1, (unsigned)_wkCc.entries[ccSlot].ccNumber);
      _ui->drawFrameLine(VT_DIM "Move CC %u in page CC first to free this pad." VT_RESET,
                         (unsigned)_wkCc.entries[ccSlot].ccNumber);
    }
    return;
  }

  // Cas 2 (Phase 3.G.2) : LOOP control = CONTEXTUEL — traité par le cas
  // générique contextuel ci-dessous (modale d'écrasement à l'assign §10).

  // Cas 3 : pad porte Bank propre — détails simple §7.4 (ENTER → dégage direct).
  if (info.bankIdx >= 0) {
    _ui->drawFrameLine(VT_BRIGHT_WHITE "Pad #%d" VT_RESET " : Bank %d",
                       (int)pad + 1, info.bankIdx + 1);
    _ui->drawFrameLine(VT_DIM "[RET] to clear this bank assignment (§7.4 dégage direct)." VT_RESET);
    return;
  }

  // Cas 4 : pad porte contextuels (ARPEG mod / LOOP slot/control) — la modale
  // d'écrasement §10 confirmera à l'assign.
  bool hasContextuel = (info.scaleRole.kind != ScaleRoleKind::NONE)
                     || (info.arpRole.kind  != ArpRoleKind::NONE)
                     || (info.loopSlotIdx >= 0)
                     || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear;
  if (hasContextuel) {
    char roleName[80] = {0};
    _formatRoleNameMusician(info, roleName, sizeof(roleName));
    _ui->drawFrameLine(VT_YELLOW "Pad #%d : %s (contextuel)" VT_RESET,
                       (int)pad + 1, roleName);
    _ui->drawFrameLine(VT_DIM "[RET] ouvre le pool — la modale d'ecrasement confirmera (§10)." VT_RESET);
    return;
  }

  // Cas 5 : pad libre — invite à assigner.
  _ui->drawFrameLine(VT_DIM "Pad #%d : libre pour Bank. [RET] to open pool." VT_RESET,
                     (int)pad + 1);
  _ui->drawFrameEmpty();
}

// =================================================================
// _drawControlBarBank — controls bar selon état (_editing ou non)
// =================================================================
void ToolPadRoles::_drawControlBarBank() {
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
// _drawPageBank — orchestrateur sections page BANK
// (PAS de vtFrameStart/End ni drawConsoleHeader : déjà émis par drawScreen)
// =================================================================
void ToolPadRoles::_drawPageBank() {
  _ui->drawSection("GRID");
  _drawGridBank();
  _ui->drawFrameEmpty();

  _ui->drawSection("POOL");
  _drawPoolBank();
  _ui->drawFrameEmpty();

  _ui->drawSection("INFO");
  _drawInfoBank();
  _ui->drawFrameEmpty();

  _drawFlash();
  _drawControlBarBank();
}
