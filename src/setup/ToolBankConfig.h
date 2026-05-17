#ifndef TOOL_BANK_CONFIG_H
#define TOOL_BANK_CONFIG_H

#include <stdint.h>
#include "../core/KeyboardData.h"
#include "../core/HardwareConfig.h"
#include "InputParser.h"

class LedController;
class NvsManager;
class SetupUI;

// =================================================================
// Tool5Working — working copy struct unifiee (cf spec §11)
// =================================================================
// Remplace les 7 arrays globaux du current. Cancel-edit = 1 assignation
// `_wk = _saved;` au lieu de 7 memcpy. quantize[] est reinterprete selon
// types[] (cf KeyboardData.h validator contextuel, Task 1) :
//   - ARPEG / ARPEG_GEN : 0..1 (Imm/Beat)
//   - LOOP              : 0..2 (Free/Beat/Bar)
//   - NORMAL            : ignore (Tool 5 affiche `·`)
struct Tool5Working {
  BankType type[NUM_BANKS];
  uint8_t  quantize[NUM_BANKS];
  uint8_t  scaleGroup[NUM_BANKS];
  uint8_t  bonusPilex10[NUM_BANKS];
  uint8_t  marginWalk[NUM_BANKS];
  uint8_t  proximityFactorx10[NUM_BANKS];
  uint8_t  ecart[NUM_BANKS];
};

// =================================================================
// ParamOptions / ParamDescriptor — table declarative (cf spec §11)
// =================================================================
// Discrete = cycle de labels (Type, Quantize, Group).
// Range    = bornes continues (Bonus, Margin, Prox, Ecart).
struct ParamOptions {
  bool isDiscrete;
  // Note : pas d'anonymous union (initialisation designee {.range = {...}}
  // mal supportee + l'union deguise mal le contenu). Les deux structs
  // coexistent ; on lit selon isDiscrete.
  struct { const char* const* labels; uint8_t count; } discrete;
  struct { int16_t minVal; int16_t maxVal; int16_t stepNormal; int16_t stepAccelerated; const char* unit; } range;
};

// Une entree par ligne du tableau. Ajout d'un param/section = ajout d'une
// entree dans PARAM_TABLE, pas de modification rendering ni handleEdition.
struct ParamDescriptor {
  const char* label;             // Affiche colonne label gauche : "Type", "Quantize", ...
  const char* sectionLabel;      // "TYPE" / "SCALE" / "WALK" — non-null si nouvelle section
  uint8_t     fieldOffset;       // offsetof(Tool5Working, field[0]) — index dans le struct
  bool        (*isApplicable)(BankType type);            // cell editable pour ce type ?
  const ParamOptions& (*getOptions)(BankType type);      // options dynamiques selon type
  const char* infoDescription;                           // texte INFO state 1 (editable)
  const char* infoNonApplicable;                         // texte INFO state 2 (non-editable)
};

extern const ParamDescriptor PARAM_TABLE[];
extern const uint8_t         PARAM_TABLE_COUNT;

// =================================================================
// ToolBankConfig — refacto 2026-05-17 (cf spec Tool 5 refacto)
// =================================================================
class ToolBankConfig {
public:
  ToolBankConfig();
  void begin(LedController* leds, NvsManager* nvs, SetupUI* ui, BankSlot* banks);
  void run();  // Blocking — nav 2D tableau matriciel banks × params

private:
  // --- Data loading / saving ---
  void loadWorkingCopy();              // NVS load + seed defaults
  bool saveAll();                      // commit NVS + sync _banks. Returns true on success.

  // --- Rendering (pure) ---
  void drawTable();                    // tableau matriciel, focus highlighting
  void drawInfo();                     // INFO panel selon _cursor + _editing

  // --- Event handling ---
  void handleNavigation(const NavEvent& ev);   // ^v<> hors edition
  void handleEdition(const NavEvent& ev);      // ^v / RET / q en edition
  void applyDefaults();                        // `d` reset cell focus

  // --- Helpers ---
  bool isCellApplicable(uint8_t paramIdx, uint8_t bankIdx) const;
  uint8_t getCellValue(uint8_t paramIdx, uint8_t bankIdx) const;
  void    setCellValue(uint8_t paramIdx, uint8_t bankIdx, uint8_t value);
  uint8_t countBanksOfType(BankType type) const;

  // --- State ---
  Tool5Working _wk;        // working copy (current edit state)
  Tool5Working _saved;     // snapshot for cancel-edit
  uint8_t      _cursorParam;   // 0..PARAM_TABLE_COUNT-1
  uint8_t      _cursorBank;    // 0..NUM_BANKS-1
  bool         _editing;
  bool         _screenDirty;
  bool         _errorShown;
  unsigned long _errorTime;
  bool         _nvsSaved;

  // --- Backref ---
  LedController* _leds;
  NvsManager*    _nvs;
  SetupUI*       _ui;
  BankSlot*      _banks;
};

#endif // TOOL_BANK_CONFIG_H
