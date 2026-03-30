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

  // Check escape timeout (stale partial sequence from previous calls)
  if (_escState != ESC_IDLE && (now - _escTime) >= ESC_TIMEOUT_MS) {
    _escState = ESC_IDLE;
  }

  if (!Serial.available()) return ev;

  // Drain stale bytes: if many key repeats queued, skip to the last one.
  // This prevents the event queue from falling behind when the user holds
  // an arrow key and the serial buffer fills faster than we can redraw.
  // We keep at most the last complete escape sequence.
  int avail = Serial.available();
  if (avail > 6 && _escState == ESC_IDLE) {
    // Drop all but the last 6 bytes (enough for 2 escape sequences)
    while (Serial.available() > 6) {
      Serial.read();
    }
  }

  char c = (char)Serial.read();

  // --- Escape sequence state machine ---
  // Arrow sequences may arrive split across loop iterations (ESC, then [, then A/B/C/D).
  // Handle incrementally instead of assuming all bytes are present at once.
  if (_escState == ESC_IDLE) {
    if (c == '\033') {
      _escState = ESC_GOT_ESC;
      _escTime = now;
      return ev;
    }
  } else if (_escState == ESC_GOT_ESC) {
    // Accept both CSI introducers:
    //   ESC [ A/B/C/D  (common)
    //   ESC O A/B/C/D  (application cursor mode)
    if (c == '[' || c == 'O') {
      _escState = ESC_GOT_BRACKET;
      _escTime = now;
      return ev;
    }

    // Invalid continuation: drop escape context and process this byte normally.
    _escState = ESC_IDLE;
  } else if (_escState == ESC_GOT_BRACKET) {
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

  // --- Normal character mapping ---
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
