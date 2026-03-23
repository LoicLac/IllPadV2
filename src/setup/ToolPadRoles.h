#ifndef TOOL_PAD_ROLES_H
#define TOOL_PAD_ROLES_H

#include <stdint.h>
#include "../core/HardwareConfig.h"
#include "InputParser.h"

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

// Unified role identifier (pool item)
struct PadRole {
  uint8_t line;   // 0=none, 1=bank, 2=scale, 3=arp
  uint8_t index;  // index within that line (bank 0-7, scale 0-14, arp 0-5)
};

class ToolPadRoles {
public:
  ToolPadRoles();

  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             NvsManager* nvs, SetupUI* ui,
             uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
             uint8_t& chromaticPad, uint8_t& holdPad, uint8_t& playStopPad,
             uint8_t* octavePads);
  void run();  // Blocking — grid + pool driven

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
  uint8_t* _octavePads;    // [4]

  // Working copies (edited during tool, committed on save)
  uint8_t _wkBankPads[NUM_BANKS];
  uint8_t _wkRootPads[7];
  uint8_t _wkModePads[7];
  uint8_t _wkChromPad;
  uint8_t _wkHoldPad;
  uint8_t _wkPlayStopPad;
  uint8_t _wkOctavePads[4];

  // Grid state (rebuilt before each draw)
  uint8_t _roleMap[NUM_KEYS];        // PadRoleCode per pad
  char    _roleLabels[NUM_KEYS][6];  // 5-char + null

  // Navigation state
  InputParser _input;
  uint8_t _gridRow;       // 0-3 (4 rows of 12)
  uint8_t _gridCol;       // 0-11
  bool    _editing;       // true = pool navigation mode
  uint8_t _poolLine;      // 0=none, 1=bank, 2=scale, 3=arp
  uint8_t _poolIdx;       // index within current pool line
  bool    _confirmSteal;  // true = waiting for y/n steal confirmation
  uint8_t _stealFromPad;  // pad that currently owns the role being stolen
  bool    _confirmDefaults; // true = waiting for y/n defaults confirmation

  // Touch detection baselines
  uint16_t _refBaselines[NUM_KEYS];

  // Helpers
  void buildRoleMap();
  PadRole getRoleForPad(uint8_t pad) const;
  uint8_t findPadWithRole(uint8_t line, uint8_t index) const;
  void    assignRole(uint8_t pad, uint8_t line, uint8_t index);
  void    clearRole(uint8_t pad);
  void    resetToDefaults();
  bool    saveAll();

  // Pool line sizes
  static const uint8_t POOL_BANK_COUNT  = 8;
  static const uint8_t POOL_SCALE_COUNT = 15;
  static const uint8_t POOL_ARP_COUNT   = 6;

  uint8_t poolLineSize(uint8_t line) const;

  // Display
  void drawScreen();
  void drawGrid();
  void drawPool();
  void drawDescription();
  void drawHelpLine();

  // Pool label helpers
  const char* poolItemLabel(uint8_t line, uint8_t index) const;
  const char* poolItemColor(uint8_t line) const;
};

#endif // TOOL_PAD_ROLES_H
