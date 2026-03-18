#ifndef SETUP_UI_H
#define SETUP_UI_H

#include <stdint.h>

class LedController;

class SetupUI {
public:
  SetupUI();

  void begin(LedController* leds);

  // Serial menu helpers
  void printMainMenu();
  void printSubMenu(const char* title, const char* const* items, uint8_t count);
  void printPrompt(const char* msg);
  void printConfirm(const char* msg);
  void printError(const char* msg);

  // LED feedback during setup
  void showToolActive(uint8_t toolIndex);  // Light corresponding LED
  void showPadFeedback(uint8_t padIndex);  // Flash to confirm pad touch
  void showCollision(uint8_t padIndex);    // Red blink = collision
  void showSaved();                         // Quick flash = saved

private:
  LedController* _leds;
};

#endif // SETUP_UI_H
