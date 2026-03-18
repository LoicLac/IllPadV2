#include "SetupUI.h"
#include "../core/LedController.h"

SetupUI::SetupUI() : _leds(nullptr) {}

void SetupUI::begin(LedController* leds) { _leds = leds; }

void SetupUI::printMainMenu() {
  // TODO: Serial print setup menu
}

void SetupUI::printSubMenu(const char* title, const char* const* items, uint8_t count) {
  (void)title; (void)items; (void)count;
}

void SetupUI::printPrompt(const char* msg)  { (void)msg; }
void SetupUI::printConfirm(const char* msg) { (void)msg; }
void SetupUI::printError(const char* msg)   { (void)msg; }

void SetupUI::showToolActive(uint8_t toolIndex) { (void)toolIndex; }
void SetupUI::showPadFeedback(uint8_t padIndex) { (void)padIndex; }
void SetupUI::showCollision(uint8_t padIndex)   { (void)padIndex; }
void SetupUI::showSaved() {}
