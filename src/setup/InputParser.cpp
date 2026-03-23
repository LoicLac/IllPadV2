#include "InputParser.h"
#include <Arduino.h>

InputParser::InputParser()
  : _escState(ESC_IDLE)
  , _escTime(0)
  , _lastArrowType(NAV_NONE)
  , _lastArrowTime(0)
  , _lastEnterTime(0)
{}

NavEvent InputParser::update() {
  NavEvent ev = { NAV_NONE, false, 0 };
  unsigned long now = millis();

  // Check escape timeout (ESC received but no '[' within 50ms)
  if (_escState == ESC_GOT_ESC && (now - _escTime) >= ESC_TIMEOUT_MS) {
    _escState = ESC_IDLE;
  }
  if (_escState == ESC_GOT_BRACKET && (now - _escTime) >= ESC_TIMEOUT_MS) {
    _escState = ESC_IDLE;
  }

  if (!Serial.available()) return ev;

  char c = (char)Serial.read();

  // Escape sequence state machine
  switch (_escState) {
    case ESC_IDLE:
      if (c == '\033') {
        _escState = ESC_GOT_ESC;
        _escTime = now;
        return ev;  // Wait for next byte
      }
      break;

    case ESC_GOT_ESC:
      if (c == '[') {
        _escState = ESC_GOT_BRACKET;
        return ev;  // Wait for direction byte
      }
      // Not a valid sequence — discard ESC, process char normally
      _escState = ESC_IDLE;
      break;

    case ESC_GOT_BRACKET:
      _escState = ESC_IDLE;
      switch (c) {
        case 'A': ev.type = NAV_UP;    break;
        case 'B': ev.type = NAV_DOWN;  break;
        case 'C': ev.type = NAV_RIGHT; break;
        case 'D': ev.type = NAV_LEFT;  break;
        default: return ev;  // Unknown sequence, ignore
      }
      // Acceleration detection for LEFT/RIGHT
      if (ev.type == NAV_LEFT || ev.type == NAV_RIGHT) {
        if (ev.type == _lastArrowType && (now - _lastArrowTime) < ACCEL_WINDOW_MS) {
          ev.accelerated = true;
        }
        _lastArrowType = ev.type;
        _lastArrowTime = now;
      }
      return ev;
  }

  // Normal character mapping (only reached from ESC_IDLE)
  switch (c) {
    case '\r':
    case '\n':
      // Debounce: terminals may send \r\n — ignore second within 50ms
      if ((now - _lastEnterTime) < ENTER_DEBOUNCE_MS) return ev;
      _lastEnterTime = now;
      ev.type = NAV_ENTER;
      break;
    case 'q':   ev.type = NAV_QUIT;     break;
    case 'd':   ev.type = NAV_DEFAULTS; break;
    case 't':   ev.type = NAV_TOGGLE;   break;
    default:
      ev.type = NAV_CHAR;
      ev.ch = c;
      break;
  }
  return ev;
}
