#ifndef TOOL_PAD_ROLES_H
#define TOOL_PAD_ROLES_H

#include <stdint.h>
#include "../core/HardwareConfig.h"

class CapacitiveKeyboard;
class LedController;
class NvsManager;
class SetupUI;

// Role codes for grid coloring
enum PadRoleCode : uint8_t {
  ROLE_NONE      = 0,
  ROLE_BANK      = 1,
  ROLE_SCALE     = 2,
  ROLE_ARP       = 3,
  ROLE_COLLISION = 0xFF
};

class ToolPadRoles {
public:
  ToolPadRoles();

  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             NvsManager* nvs, SetupUI* ui,
             uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
             uint8_t& chromaticPad, uint8_t& holdPad, uint8_t& playStopPad);
  void run();  // Blocking — sub-menu driven

private:
  CapacitiveKeyboard* _keyboard;
  LedController*      _leds;
  NvsManager*         _nvs;
  SetupUI*            _ui;

  // Pointers to live pad assignment arrays (owned by caller)
  uint8_t* _bankPads;      // [NUM_BANKS]
  uint8_t* _rootPads;      // [7]
  uint8_t* _modePads;      // [7]
  uint8_t* _chromaticPad;  // single
  uint8_t* _holdPad;       // single
  uint8_t* _playStopPad;   // single

  // Working copies (edited during tool, committed on save)
  uint8_t _wkBankPads[NUM_BANKS];
  uint8_t _wkRootPads[7];
  uint8_t _wkModePads[7];
  uint8_t _wkChromPad;
  uint8_t _wkHoldPad;
  uint8_t _wkPlayStopPad;

  // Grid state (rebuilt before each draw)
  uint8_t _roleMap[NUM_KEYS];        // PadRoleCode per pad
  char    _roleLabels[NUM_KEYS][6];  // 5-char + null

  // Sub-tools
  void runBankPads();
  void runScalePads();
  void runArpPads();
  void viewAll();

  // Helpers
  void buildRoleMap();
  int  countCollisions() const;
  bool saveAll();

  // Reusable touch-to-assign loop
  void assignSection(const char* sectionTitle, const char* const* labels,
                     uint8_t* targets, uint8_t count);
};

#endif // TOOL_PAD_ROLES_H
