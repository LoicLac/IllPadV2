#include "ToolBankConfig.h"
#include "../core/LedController.h"
#include "../managers/NvsManager.h"
#include "SetupUI.h"
#include "InputParser.h"
#include <Arduino.h>
#include <stddef.h>
#include <string.h>

// =================================================================
// Static labels & ParamOptions (cf spec §11 + §6 + Task 3 Step 3.3)
// =================================================================
// Cell labels (table). Indice = BankType : NORMAL=0, ARPEG=1, LOOP=2, ARPEG_GEN=3.
// 5-char labels ARP_N / ARP_G accommodes par CELL_W = 7 (cf drawTable).
static const char* const TYPE_LABELS[]      = { "NORM",   "ARP_N",           "LOOP", "ARP_G" };
// Long form pour INFO (cf user-facing). Indice identique a TYPE_LABELS.
static const char* const TYPE_LABELS_LONG[] = { "Normal", "Arpeggio normal", "Loop", "Arpeggio generatif" };

static const char* const QUANTIZE_LABELS_ARP[]  = { "Imm",  "Beat" };
static const char* const QUANTIZE_LABELS_LOOP[] = { "Free", "Beat", "Bar" };
static const char* const GROUP_LABELS[]         = { "-", "A", "B", "C", "D" };

// Largeur visible d'une cell du tableau. Etendue de 6 -> 7 pour accommoder
// labels 5-char + focus brackets (ex. [ARP_N] = 7 chars visible).
static const int CELL_W = 7;

// Discrete : labels + count. Range : min/max/stepNormal/stepAccel/unit.
static const ParamOptions TYPE_OPTIONS = {
  /*isDiscrete*/ true,
  /*discrete*/   { TYPE_LABELS, 4 },
  /*range*/      { 0, 0, 0, 0, nullptr }
};
static const ParamOptions QUANTIZE_OPTIONS_ARP = {
  true,  { QUANTIZE_LABELS_ARP,  2 },  { 0, 0, 0, 0, nullptr }
};
static const ParamOptions QUANTIZE_OPTIONS_LOOP = {
  true,  { QUANTIZE_LABELS_LOOP, 3 },  { 0, 0, 0, 0, nullptr }
};
static const ParamOptions GROUP_OPTIONS = {
  true,  { GROUP_LABELS, 5 },  { 0, 0, 0, 0, nullptr }
};
static const ParamOptions BONUS_OPTIONS  = { false, { nullptr, 0 }, { 10, 20, 1, 5, "" } };
static const ParamOptions MARGIN_OPTIONS = { false, { nullptr, 0 }, {  3, 12, 1, 3, "" } };
static const ParamOptions PROX_OPTIONS   = { false, { nullptr, 0 }, {  4, 20, 1, 5, "" } };
static const ParamOptions ECART_OPTIONS  = { false, { nullptr, 0 }, {  1, 12, 1, 3, "" } };

// =================================================================
// isApplicable + getOptions helpers (cf spec §11)
// =================================================================
static bool isAlways(BankType t)        { (void)t; return true; }
static bool isArpOrLoop(BankType t)     { return isArpType(t) || t == BANK_LOOP; }
static bool isNotLoop(BankType t)       { return t != BANK_LOOP; }
static bool isArpegGenOnly(BankType t)  { return t == BANK_ARPEG_GEN; }

static const ParamOptions& typeOpts(BankType t)     { (void)t; return TYPE_OPTIONS; }
static const ParamOptions& quantizeOpts(BankType t) {
  if (t == BANK_LOOP) return QUANTIZE_OPTIONS_LOOP;
  return QUANTIZE_OPTIONS_ARP;
}
static const ParamOptions& groupOpts(BankType t)    { (void)t; return GROUP_OPTIONS; }
static const ParamOptions& bonusOpts(BankType t)    { (void)t; return BONUS_OPTIONS; }
static const ParamOptions& marginOpts(BankType t)   { (void)t; return MARGIN_OPTIONS; }
static const ParamOptions& proxOpts(BankType t)     { (void)t; return PROX_OPTIONS; }
static const ParamOptions& ecartOpts(BankType t)    { (void)t; return ECART_OPTIONS; }

// =================================================================
// PARAM_TABLE (cf spec §11, plan §3.3)
// =================================================================
// fieldOffset : offset (en octets) du debut de l'array dans Tool5Working.
// Acces : *(reinterpret_cast<uint8_t*>(&_wk) + fieldOffset + bankIdx).
// Marche car BankType est `enum : uint8_t` -> 1 octet par element.
const ParamDescriptor PARAM_TABLE[] = {
  // --- section TYPE ---
  { "Type",     "TYPE",  (uint8_t)offsetof(Tool5Working, type),                isAlways,       typeOpts,
    "Bank type. Cycle 4 valeurs : NORMAL (notes pad + AT poly), ARPEG (arp pile-based), ARPEG_GEN (arp generatif + walk), LOOP (loop percussif).",
    nullptr },
  { "Quantize", nullptr, (uint8_t)offsetof(Tool5Working, quantize),            isArpOrLoop,    quantizeOpts,
    "Start/stop quantize. ARPEG/ARPEG_GEN : Imm (next clock division), Beat (next 1/4 note). LOOP : Free (no quant), Beat (next 1/4), Bar (next measure).",
    "Quantize gere le timing du start/stop. NORMAL bank n'a pas de start/stop." },
  // --- section SCALE ---
  { "Group",    "SCALE", (uint8_t)offsetof(Tool5Working, scaleGroup),          isNotLoop,      groupOpts,
    "Scale group : root/mode changes propagate to all banks of same group. Cycle -, A, B, C, D.",
    "Scale group ignore sur LOOP (invariant 6 : pas de scale sur bank LOOP)." },
  // --- section WALK (ARPEG_GEN only) ---
  { "Bonus",    "WALK",  (uint8_t)offsetof(Tool5Working, bonusPilex10),        isArpegGenOnly, bonusOpts,
    "Walk weight on pile degrees during mutation. Higher = mutations stay anchored to pile. Lower = wider exploration. Range 1.0..2.0 (step 0.1, accel 0.5).",
    "Bonus pile ARPEG_GEN-only. Change Type to ARPEG_GEN to enable." },
  { "Margin",   nullptr, (uint8_t)offsetof(Tool5Working, marginWalk),          isArpegGenOnly, marginOpts,
    "Margin walk : how far the walk can drift above/below pile range. Smaller = melody hugs pile. Larger = melody drifts. Range 3..12 (step 1, accel 3).",
    "Margin walk ARPEG_GEN-only. Change Type to ARPEG_GEN to enable." },
  { "Prox",     nullptr, (uint8_t)offsetof(Tool5Working, proximityFactorx10),  isArpegGenOnly, proxOpts,
    "Proximity factor : exponential falloff steepness. Smaller = step-wise. Larger = erratic leaps. Range 0.4..2.0 (step 0.1, accel 0.5).",
    "Prox ARPEG_GEN-only. Change Type to ARPEG_GEN to enable." },
  { "Ecart",    nullptr, (uint8_t)offsetof(Tool5Working, ecart),               isArpegGenOnly, ecartOpts,
    "Max degree jump between consecutive steps. Overrides R2+hold ecart. Range 1..12 (step 1, accel 3).",
    "Ecart ARPEG_GEN-only. Change Type to ARPEG_GEN to enable." },
};
const uint8_t PARAM_TABLE_COUNT = sizeof(PARAM_TABLE) / sizeof(PARAM_TABLE[0]);

// =================================================================
// Color picking helpers — spec §6 code couleurs
// =================================================================
// Type column : color = couleur du type lui-meme.
// Quantize    : suit la couleur du type de la bank (coherence verticale).
// Group       : amber (VT_YELLOW).
// Walk params : magenta (coherence AGEN).
static const char* colorForType(BankType t) {
  switch (t) {
    case BANK_NORMAL:    return VT_BRIGHT_WHITE;
    case BANK_ARPEG:     return VT_CYAN;
    case BANK_LOOP:      return VT_YELLOW;
    case BANK_ARPEG_GEN: return VT_MAGENTA;
    default:             return VT_DIM;
  }
}
static const char* pickCellColor(uint8_t paramIdx, BankType bankType) {
  // paramIdx 0 = Type, 1 = Quantize, 2 = Group, 3..6 = Walk params.
  if (paramIdx == 0 || paramIdx == 1) return colorForType(bankType);
  if (paramIdx == 2) return VT_YELLOW;       // Group amber
  return VT_MAGENTA;                          // Walk params
}

// =================================================================
// Constructor / begin
// =================================================================
ToolBankConfig::ToolBankConfig()
  : _cursorParam(0), _cursorBank(0), _editing(false), _screenDirty(true),
    _errorShown(false), _errorTime(0), _nvsSaved(false),
    _leds(nullptr), _nvs(nullptr), _ui(nullptr), _banks(nullptr) {
  memset(&_wk, 0, sizeof(_wk));
  memset(&_saved, 0, sizeof(_saved));
}

void ToolBankConfig::begin(LedController* leds, NvsManager* nvs, SetupUI* ui, BankSlot* banks) {
  _leds  = leds;
  _nvs   = nvs;
  _ui    = ui;
  _banks = banks;
}

// =================================================================
// loadWorkingCopy — NVS load + seed defaults (plan §3.4)
// =================================================================
void ToolBankConfig::loadWorkingCopy() {
  // 1. Seed avec valeurs courantes _banks et _nvs (cas premier boot, pas de blob NVS).
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _wk.type[i]               = _banks ? _banks[i].type : BANK_NORMAL;
    _wk.quantize[i]           = _nvs ? _nvs->getLoadedQuantizeMode(i)    : DEFAULT_ARP_START_MODE;
    _wk.scaleGroup[i]         = _nvs ? _nvs->getLoadedScaleGroup(i)      : 0;
    _wk.bonusPilex10[i]       = _nvs ? _nvs->getLoadedBonusPile(i)       : 15;
    _wk.marginWalk[i]         = _nvs ? _nvs->getLoadedMarginWalk(i)      : 7;
    _wk.proximityFactorx10[i] = _nvs ? _nvs->getLoadedProximityFactor(i) : 4;
    _wk.ecart[i]              = _nvs ? _nvs->getLoadedEcart(i)           : 5;
  }
  // 2. Override depuis NVS si blob valide.
  BankTypeStore bts;
  if (NvsManager::loadBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2,
                           EEPROM_MAGIC, BANKTYPE_VERSION, &bts, sizeof(bts))) {
    validateBankTypeStore(bts);
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      _wk.type[i]               = (BankType)bts.types[i];
      _wk.quantize[i]           = bts.quantize[i];
      _wk.scaleGroup[i]         = bts.scaleGroup[i];
      _wk.bonusPilex10[i]       = bts.bonusPilex10[i];
      _wk.marginWalk[i]         = bts.marginWalk[i];
      _wk.proximityFactorx10[i] = bts.proximityFactorx10[i];
      _wk.ecart[i]              = bts.ecart[i];
    }
    _nvsSaved = true;
  } else {
    _nvsSaved = false;
  }
  // 3. Snapshot pour cancel-edit.
  _saved = _wk;
}

// =================================================================
// saveAll — commit NVS + sync _banks + NvsManager loaded mirrors (plan §3.5)
// =================================================================
bool ToolBankConfig::saveAll() {
  BankTypeStore bts;
  bts.magic    = EEPROM_MAGIC;
  bts.version  = BANKTYPE_VERSION;
  bts.reserved = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    bts.types[i]              = (uint8_t)_wk.type[i];
    bts.quantize[i]           = _wk.quantize[i];
    bts.scaleGroup[i]         = _wk.scaleGroup[i];
    bts.bonusPilex10[i]       = _wk.bonusPilex10[i];
    bts.marginWalk[i]         = _wk.marginWalk[i];
    bts.proximityFactorx10[i] = _wk.proximityFactorx10[i];
    bts.ecart[i]              = _wk.ecart[i];
  }
  validateBankTypeStore(bts);

  if (!NvsManager::saveBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2, &bts, sizeof(bts)))
    return false;

  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (_banks) _banks[i].type = (BankType)bts.types[i];
    if (_nvs) {
      _nvs->setLoadedQuantizeMode(i,    bts.quantize[i]);
      _nvs->setLoadedScaleGroup(i,      bts.scaleGroup[i]);
      _nvs->setLoadedBonusPile(i,       bts.bonusPilex10[i]);
      _nvs->setLoadedMarginWalk(i,      bts.marginWalk[i]);
      _nvs->setLoadedProximityFactor(i, bts.proximityFactorx10[i]);
      _nvs->setLoadedEcart(i,           bts.ecart[i]);
    }
  }
  _saved    = _wk;
  _nvsSaved = true;
  return true;
}

// =================================================================
// Helpers : isCellApplicable / getCellValue / setCellValue / countBanksOfType
// =================================================================
bool ToolBankConfig::isCellApplicable(uint8_t paramIdx, uint8_t bankIdx) const {
  if (paramIdx >= PARAM_TABLE_COUNT || bankIdx >= NUM_BANKS) return false;
  return PARAM_TABLE[paramIdx].isApplicable(_wk.type[bankIdx]);
}

uint8_t ToolBankConfig::getCellValue(uint8_t paramIdx, uint8_t bankIdx) const {
  const uint8_t* base = reinterpret_cast<const uint8_t*>(&_wk);
  return *(base + PARAM_TABLE[paramIdx].fieldOffset + bankIdx);
}

void ToolBankConfig::setCellValue(uint8_t paramIdx, uint8_t bankIdx, uint8_t value) {
  uint8_t* base = reinterpret_cast<uint8_t*>(&_wk);
  *(base + PARAM_TABLE[paramIdx].fieldOffset + bankIdx) = value;
}

uint8_t ToolBankConfig::countBanksOfType(BankType type) const {
  uint8_t c = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) if (_wk.type[i] == type) c++;
  return c;
}

// =================================================================
// Cell rendering helpers (private statics)
// =================================================================
// Format inner string (visible) centered into a 6-char cell, with optional
// focus brackets. Color applies only to the inner string.
// visLen = visible char count of `inner` (UTF-8 safe — caller knows).
static void appendCell(char* dst, size_t dstSize, int& pos,
                       const char* inner, int visLen,
                       const char* color, bool focus, bool focusEditable) {
  int total = visLen + (focus ? 2 : 0);
  if (total > CELL_W) total = CELL_W;
  int leftPad  = (CELL_W - total) / 2;
  int rightPad = CELL_W - total - leftPad;
  for (int i = 0; i < leftPad; i++)  pos += snprintf(dst + pos, dstSize - pos, " ");
  if (focus) {
    const char* bracketStyle = focusEditable ? (VT_CYAN VT_BOLD) : VT_DIM;
    pos += snprintf(dst + pos, dstSize - pos, "%s[" VT_RESET, bracketStyle);
  }
  pos += snprintf(dst + pos, dstSize - pos, "%s%s" VT_RESET, color, inner);
  if (focus) {
    const char* bracketStyle = focusEditable ? (VT_CYAN VT_BOLD) : VT_DIM;
    pos += snprintf(dst + pos, dstSize - pos, "%s]" VT_RESET, bracketStyle);
  }
  for (int i = 0; i < rightPad; i++) pos += snprintf(dst + pos, dstSize - pos, " ");
}

// =================================================================
// drawTable — render the matrix banks × params (plan §3.7)
// =================================================================
void ToolBankConfig::drawTable() {
  const int LABEL_W = 14;

  // --- Top border : 14-space indent + ┌──────┬──────...┬──────┐ ---
  {
    char buf[256];
    int p = 0;
    for (int k = 0; k < LABEL_W; k++) p += snprintf(buf + p, sizeof(buf) - p, " ");
    p += snprintf(buf + p, sizeof(buf) - p, VT_DIM "%s", UNI_CTL);
    for (int b = 0; b < NUM_BANKS; b++) {
      for (int k = 0; k < CELL_W; k++) p += snprintf(buf + p, sizeof(buf) - p, "%s", UNI_CH);
      p += snprintf(buf + p, sizeof(buf) - p, "%s",
                    (b < NUM_BANKS - 1) ? UNI_CT : UNI_CTR);
    }
    p += snprintf(buf + p, sizeof(buf) - p, VT_RESET);
    _ui->drawFrameLine("%s", buf);
  }

  // --- Bank header row : indent + │ Bk1  │ Bk2  │ ... ---
  {
    char buf[256];
    int p = 0;
    for (int k = 0; k < LABEL_W; k++) p += snprintf(buf + p, sizeof(buf) - p, " ");
    p += snprintf(buf + p, sizeof(buf) - p, VT_DIM "%s" VT_RESET, UNI_CV);
    for (int b = 0; b < NUM_BANKS; b++) {
      char label[8];
      snprintf(label, sizeof(label), "Bk%d", b + 1);
      int vis = strlen(label);   // ASCII only
      appendCell(buf, sizeof(buf), p, label, vis, VT_BRIGHT_BLUE, false, false);
      p += snprintf(buf + p, sizeof(buf) - p, VT_DIM "%s" VT_RESET, UNI_CV);
    }
    _ui->drawFrameLine("%s", buf);
  }

  // --- Param rows + section separators ---
  for (uint8_t pi = 0; pi < PARAM_TABLE_COUNT; pi++) {
    const ParamDescriptor& descr = PARAM_TABLE[pi];

    // Section separator (if descr.sectionLabel != null)
    if (descr.sectionLabel) {
      char buf[256];
      int p = 0;
      // Label area : "   ─ <SECTION> ────" filling LABEL_W visible chars.
      p += snprintf(buf + p, sizeof(buf) - p, VT_DIM "   ");
      int labelVis = 3;
      p += snprintf(buf + p, sizeof(buf) - p, VT_CYAN "%s %s " VT_DIM,
                    UNI_CH, descr.sectionLabel);
      labelVis += 1 + 1 + (int)strlen(descr.sectionLabel) + 1;
      while (labelVis < LABEL_W) {
        p += snprintf(buf + p, sizeof(buf) - p, "%s", UNI_CH);
        labelVis++;
      }
      // Table area : ┼──────┼──────...┼──────┤
      p += snprintf(buf + p, sizeof(buf) - p, "%s", UNI_CX);
      for (int b = 0; b < NUM_BANKS; b++) {
        for (int k = 0; k < CELL_W; k++) p += snprintf(buf + p, sizeof(buf) - p, "%s", UNI_CH);
        p += snprintf(buf + p, sizeof(buf) - p, "%s",
                      (b < NUM_BANKS - 1) ? UNI_CX : UNI_CRT);
      }
      p += snprintf(buf + p, sizeof(buf) - p, VT_RESET);
      _ui->drawFrameLine("%s", buf);
    }

    // Param row
    char buf[512];
    int p = 0;
    // Label area : "   <label>      " filling LABEL_W visible chars.
    p += snprintf(buf + p, sizeof(buf) - p, "   " VT_CYAN "%s" VT_RESET, descr.label);
    int labelVis = 3 + (int)strlen(descr.label);
    while (labelVis < LABEL_W) {
      p += snprintf(buf + p, sizeof(buf) - p, " ");
      labelVis++;
    }
    p += snprintf(buf + p, sizeof(buf) - p, VT_DIM "%s" VT_RESET, UNI_CV);

    for (uint8_t b = 0; b < NUM_BANKS; b++) {
      BankType bt = _wk.type[b];
      bool applicable = descr.isApplicable(bt);
      bool focus      = (pi == _cursorParam && b == _cursorBank);
      uint8_t cellVal = getCellValue(pi, b);

      if (!applicable) {
        // Non-applicable cell : · VT_DIM. Focus = [·] dim brackets.
        appendCell(buf, sizeof(buf), p, "\xc2\xb7", 1, VT_DIM, focus, false);
      } else {
        // Applicable : value formatted per options + color.
        const ParamOptions& opts = descr.getOptions(bt);
        const char* color = pickCellColor(pi, bt);
        char valBuf[16];
        int  visLen;
        if (opts.isDiscrete) {
          uint8_t idx = (cellVal < opts.discrete.count) ? cellVal : 0;
          snprintf(valBuf, sizeof(valBuf), "%s", opts.discrete.labels[idx]);
          visLen = strlen(valBuf);  // ASCII labels
        } else {
          // Range : Bonus, Prox display /10.0 float ; Margin, Ecart integer.
          if (pi == 3 /*Bonus*/ || pi == 5 /*Prox*/) {
            snprintf(valBuf, sizeof(valBuf), "%.1f", (float)cellVal / 10.0f);
          } else {
            snprintf(valBuf, sizeof(valBuf), "%d", cellVal);
          }
          visLen = strlen(valBuf);
        }
        appendCell(buf, sizeof(buf), p, valBuf, visLen, color, focus, true);
      }
      p += snprintf(buf + p, sizeof(buf) - p, VT_DIM "%s" VT_RESET, UNI_CV);
    }
    _ui->drawFrameLine("%s", buf);
  }

  // --- Bottom border : indent + └──────┴──────...┴──────┘ ---
  {
    char buf[256];
    int p = 0;
    for (int k = 0; k < LABEL_W; k++) p += snprintf(buf + p, sizeof(buf) - p, " ");
    p += snprintf(buf + p, sizeof(buf) - p, VT_DIM "%s", UNI_CBL);
    for (int b = 0; b < NUM_BANKS; b++) {
      for (int k = 0; k < CELL_W; k++) p += snprintf(buf + p, sizeof(buf) - p, "%s", UNI_CH);
      p += snprintf(buf + p, sizeof(buf) - p, "%s",
                    (b < NUM_BANKS - 1) ? UNI_CB : UNI_CBR);
    }
    p += snprintf(buf + p, sizeof(buf) - p, VT_RESET);
    _ui->drawFrameLine("%s", buf);
  }
}

// =================================================================
// drawInfo — INFO panel 3 etats (plan §3.8)
// =================================================================
void ToolBankConfig::drawInfo() {
  const ParamDescriptor& descr = PARAM_TABLE[_cursorParam];
  BankType bankType = _wk.type[_cursorBank];
  bool applicable = descr.isApplicable(bankType);

  _ui->drawSection("INFO");

  // Header line : "Bank N / ParamName [hint]"
  char header[96];
  if (applicable) {
    snprintf(header, sizeof(header), "Bank %d / %s%s",
             _cursorBank + 1, descr.label,
             _editing ? " (editing)" : "");
  } else {
    snprintf(header, sizeof(header), "Bank %d / %s  \xc2\xb7  non applicable",
             _cursorBank + 1, descr.label);
  }
  _ui->drawFrameLine(VT_BRIGHT_WHITE "%s" VT_RESET, header);

  if (applicable) {
    _ui->drawFrameLine(VT_YELLOW "%s" VT_RESET, descr.infoDescription);
    // Cycle / range hint
    const ParamOptions& opts = descr.getOptions(bankType);
    if (opts.isDiscrete) {
      char cycle[128];
      int p = snprintf(cycle, sizeof(cycle), "Cycle : ");
      for (uint8_t k = 0; k < opts.discrete.count; k++) {
        p += snprintf(cycle + p, sizeof(cycle) - p, "%s%s",
                      opts.discrete.labels[k],
                      (k + 1 < opts.discrete.count) ? ", " : ".");
      }
      _ui->drawFrameLine(VT_DIM "%s" VT_RESET, cycle);
    } else {
      // Range : Bonus / Prox display /10.0 ; Margin / Ecart integer.
      if (_cursorParam == 3 /*Bonus*/ || _cursorParam == 5 /*Prox*/) {
        _ui->drawFrameLine(VT_DIM "Range : %.1f..%.1f, step %.1f (accel %.1f)." VT_RESET,
                           (float)opts.range.minVal / 10.0f,
                           (float)opts.range.maxVal / 10.0f,
                           (float)opts.range.stepNormal / 10.0f,
                           (float)opts.range.stepAccelerated / 10.0f);
      } else {
        _ui->drawFrameLine(VT_DIM "Range : %d..%d, step %d (accel %d)." VT_RESET,
                           opts.range.minVal, opts.range.maxVal,
                           opts.range.stepNormal, opts.range.stepAccelerated);
      }
    }
    if (_editing) {
      _ui->drawFrameLine(VT_DIM "^v adjust, RET save, q cancel." VT_RESET);
    }
  } else {
    // Non-applicable : hint pourquoi + hint type.
    _ui->drawFrameLine(VT_DIM "%s" VT_RESET, descr.infoNonApplicable);
    _ui->drawFrameLine(VT_DIM "Bank %d type is %s." VT_RESET,
                       _cursorBank + 1, TYPE_LABELS_LONG[bankType]);
  }
}

// =================================================================
// handleNavigation — hors edition (plan §3.9)
// =================================================================
void ToolBankConfig::handleNavigation(const NavEvent& ev) {
  if (ev.type == NAV_UP) {
    _cursorParam = (_cursorParam == 0) ? (PARAM_TABLE_COUNT - 1) : (_cursorParam - 1);
    _screenDirty = true;
  } else if (ev.type == NAV_DOWN) {
    _cursorParam = (_cursorParam + 1) % PARAM_TABLE_COUNT;
    _screenDirty = true;
  } else if (ev.type == NAV_LEFT) {
    _cursorBank = (_cursorBank == 0) ? (NUM_BANKS - 1) : (_cursorBank - 1);
    _screenDirty = true;
  } else if (ev.type == NAV_RIGHT) {
    _cursorBank = (_cursorBank + 1) % NUM_BANKS;
    _screenDirty = true;
  } else if (ev.type == NAV_ENTER) {
    if (isCellApplicable(_cursorParam, _cursorBank)) {
      _editing = true;
      _screenDirty = true;
    }
    // Sinon : no-op silencieux (INFO state 2 deja visible).
  } else if (ev.type == NAV_DEFAULTS) {
    applyDefaults();
    _screenDirty = true;
  }
}

// =================================================================
// handleEdition — en edition (plan §3.10)
// =================================================================
void ToolBankConfig::handleEdition(const NavEvent& ev) {
  const ParamDescriptor& descr = PARAM_TABLE[_cursorParam];
  BankType bankType = _wk.type[_cursorBank];
  const ParamOptions& opts = descr.getOptions(bankType);

  if (ev.type == NAV_UP || ev.type == NAV_DOWN) {
    bool up = (ev.type == NAV_UP);

    if (opts.isDiscrete) {
      uint8_t cur = getCellValue(_cursorParam, _cursorBank);
      uint8_t next = up
        ? (uint8_t)((cur + 1) % opts.discrete.count)
        : (uint8_t)((cur == 0) ? (opts.discrete.count - 1) : (cur - 1));

      // Cas special Type cycle : cap MAX_ARP_BANKS / MAX_LOOP_BANKS -> skip.
      if (_cursorParam == 0 /*Type*/) {
        uint8_t arpExcl  = 0;
        uint8_t loopExcl = 0;
        for (uint8_t i = 0; i < NUM_BANKS; i++) {
          if (i == _cursorBank) continue;
          if (isArpType(_wk.type[i])) arpExcl++;
          if (_wk.type[i] == BANK_LOOP) loopExcl++;
        }
        uint8_t guard = 0;
        bool found = false;
        while (guard++ < opts.discrete.count) {
          BankType candidate = (BankType)next;
          bool capOk = true;
          if (isArpType(candidate) && arpExcl  >= MAX_ARP_BANKS)  capOk = false;
          if (candidate == BANK_LOOP && loopExcl >= MAX_LOOP_BANKS) capOk = false;
          if (capOk) { found = true; break; }
          next = up
            ? (uint8_t)((next + 1) % opts.discrete.count)
            : (uint8_t)((next == 0) ? (opts.discrete.count - 1) : (next - 1));
        }
        if (!found) {
          next = cur;
          _errorShown = true;
          _errorTime = millis();
        }
        // Si le nouveau type rend invalide le quantize courant, le re-clamper.
        BankType newType = (BankType)next;
        if (isArpType(newType) && _wk.quantize[_cursorBank] >= NUM_ARP_START_MODES) {
          _wk.quantize[_cursorBank] = DEFAULT_ARP_START_MODE;
        } else if (newType == BANK_LOOP && _wk.quantize[_cursorBank] >= 3) {
          _wk.quantize[_cursorBank] = 2;  // Bar default
        }
      }
      setCellValue(_cursorParam, _cursorBank, next);
    } else {
      // Range param : ±step (accel selon ev.accelerated). Clamp aux bornes.
      int16_t step = ev.accelerated ? opts.range.stepAccelerated : opts.range.stepNormal;
      int16_t cur  = (int16_t)getCellValue(_cursorParam, _cursorBank);
      int16_t v    = up ? (cur + step) : (cur - step);
      if (v < opts.range.minVal) v = opts.range.minVal;
      if (v > opts.range.maxVal) v = opts.range.maxVal;
      setCellValue(_cursorParam, _cursorBank, (uint8_t)v);
    }
    _screenDirty = true;
  } else if (ev.type == NAV_ENTER) {
    if (saveAll()) {
      _ui->flashSaved();
      _editing = false;
      _screenDirty = true;
    }
  } else if (ev.type == NAV_QUIT) {
    // Cancel : restore cell focus from _saved.
    const uint8_t* base = reinterpret_cast<const uint8_t*>(&_saved);
    uint8_t savedVal = *(base + descr.fieldOffset + _cursorBank);
    setCellValue(_cursorParam, _cursorBank, savedVal);
    _editing = false;
    _screenDirty = true;
  }
}

// =================================================================
// applyDefaults — `d` reset cell focus uniquement (plan §3.11)
// =================================================================
void ToolBankConfig::applyDefaults() {
  // Default per param index, aligne sur PARAM_TABLE order.
  static const uint8_t DEFAULTS[] = {
    BANK_NORMAL,             // 0 Type
    DEFAULT_ARP_START_MODE,  // 1 Quantize (Imm)
    0,                       // 2 Group : -
    15,                      // 3 Bonus : 1.5
    7,                       // 4 Margin
    4,                       // 5 Prox : 0.4
    5,                       // 6 Ecart
  };
  static_assert(sizeof(DEFAULTS) / sizeof(DEFAULTS[0]) == 7,
                "DEFAULTS size must match PARAM_TABLE row count");
  if (_cursorParam < (sizeof(DEFAULTS) / sizeof(DEFAULTS[0]))) {
    setCellValue(_cursorParam, _cursorBank, DEFAULTS[_cursorParam]);
    _screenDirty = true;
  }
}

// =================================================================
// run — event loop (plan §3.12)
// =================================================================
void ToolBankConfig::run() {
  if (!_ui || !_banks) return;

  loadWorkingCopy();
  _cursorParam = 0;
  _cursorBank  = 0;
  _editing     = false;
  _screenDirty = true;
  _errorShown  = false;

  Serial.print(ITERM_RESIZE);
  _ui->vtClear();

  InputParser input;
  while (true) {
    if (_leds) _leds->update();
    NavEvent ev = input.update();

    if (_errorShown && (millis() - _errorTime) > 2000) {
      _errorShown = false;
      _screenDirty = true;
    }

    // Exit Tool : `q` hors edition.
    if (ev.type == NAV_QUIT && !_editing) {
      _ui->vtClear();
      return;
    }

    if (!_editing) {
      handleNavigation(ev);
    } else {
      handleEdition(ev);
    }

    if (_screenDirty) {
      _screenDirty = false;

      // Header : "TOOL 5: BANK CONFIG  XA/YL/ZN"
      char headerRight[40];
      uint8_t arpCount  = countBanksOfType(BANK_ARPEG) + countBanksOfType(BANK_ARPEG_GEN);
      uint8_t loopCount = countBanksOfType(BANK_LOOP);
      uint8_t normalCount = NUM_BANKS - arpCount - loopCount;
      snprintf(headerRight, sizeof(headerRight),
               "TOOL 5: BANK CONFIG  %dA/%dL/%dN", arpCount, loopCount, normalCount);

      _ui->vtFrameStart();
      _ui->drawConsoleHeader(headerRight, _nvsSaved);
      _ui->drawFrameEmpty();
      _ui->drawSection("BANKS");
      _ui->drawFrameEmpty();
      drawTable();
      _ui->drawFrameEmpty();
      drawInfo();

      if (_errorShown) {
        _ui->drawFrameEmpty();
        _ui->drawFrameLine(VT_BRIGHT_RED "Cap atteint : tous les types sont satures (rare)." VT_RESET);
      }

      _ui->drawFrameEmpty();

      // Control bar
      if (_editing) {
        _ui->drawControlBar(VT_DIM "[^v] VALUE" CBAR_SEP "[RET] SAVE" CBAR_SEP "[q] CANCEL" VT_RESET);
      } else {
        _ui->drawControlBar(VT_DIM "[^v<>] NAV" CBAR_SEP "[RET] EDIT  [d] DFLT" CBAR_SEP "[q] EXIT" VT_RESET);
      }
      _ui->vtFrameEnd();
    }

    delay(5);
  }
}
