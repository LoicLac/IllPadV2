#ifndef INPUT_PARSER_H
#define INPUT_PARSER_H

#include <stdint.h>

enum NavType : uint8_t {
  NAV_NONE = 0,
  NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT,
  NAV_ENTER,
  NAV_QUIT,        // 'q'
  NAV_DEFAULTS,    // 'd'
  NAV_TOGGLE,      // 't'
  NAV_CHAR,        // all other chars — raw char in NavEvent.ch
};

struct NavEvent {
  NavType type;
  bool    accelerated;  // true if rapid LEFT/RIGHT repeat < 120ms
  char    ch;           // raw character (valid when type == NAV_CHAR)
};

class InputParser {
public:
  InputParser();

  // Call every loop iteration. Returns parsed event or NAV_NONE.
  NavEvent update();

private:
  // Escape sequence state machine
  enum EscState : uint8_t { ESC_IDLE, ESC_GOT_ESC, ESC_GOT_BRACKET };
  EscState _escState;
  unsigned long _escTime;       // millis() when ESC received

  // Acceleration detection
  NavType  _lastArrowType;
  unsigned long _lastArrowTime;

  // Enter debounce (\r\n sends two bytes — ignore second within 50ms)
  unsigned long _lastEnterTime;

  static const unsigned long ESC_TIMEOUT_MS = 50;
  static const unsigned long ACCEL_WINDOW_MS = 120;
  static const unsigned long ENTER_DEBOUNCE_MS = 50;
};

#endif
