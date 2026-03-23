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

  char c = (char)Serial.read();

  // --- ESC detected: consume full sequence immediately ---
  // At 115200 baud, ESC [ A arrives in ~0.26ms — all bytes are in the buffer.
  // Reading ahead avoids losing sequences when the tool loop is slow.
  if (c == '\033' && _escState == ESC_IDLE) {
    // Wait briefly for the rest of the sequence (max 5ms)
    unsigned long escStart = now;
    while (!Serial.available() && (millis() - escStart) < 5) { /* spin */ }
    if (!Serial.available()) return ev;  // Stale ESC, discard

    char c2 = (char)Serial.read();
    if (c2 != '[') {
      // Not an escape sequence — discard ESC, process c2 as normal char
      c = c2;
      // Fall through to normal char mapping below
    } else {
      // Got ESC [ — wait for direction byte
      while (!Serial.available() && (millis() - escStart) < 5) { /* spin */ }
      if (!Serial.available()) return ev;  // Incomplete, discard

      char c3 = (char)Serial.read();
      switch (c3) {
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
  }

  // --- Handle partial sequences from previous calls (legacy fallback) ---
  if (_escState == ESC_GOT_ESC) {
    _escState = ESC_IDLE;
    if (c == '[') {
      _escState = ESC_GOT_BRACKET;
      return ev;
    }
    // Not '[' — fall through to normal char mapping
  } else if (_escState == ESC_GOT_BRACKET) {
    _escState = ESC_IDLE;
    switch (c) {
      case 'A': ev.type = NAV_UP;    break;
      case 'B': ev.type = NAV_DOWN;  break;
      case 'C': ev.type = NAV_RIGHT; break;
      case 'D': ev.type = NAV_LEFT;  break;
      default: return ev;
    }
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
