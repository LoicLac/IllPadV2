# ILLPAD V2 — Bug Routine Report

---
## Run — 2026-03-27 19:04 (audit ALL, rounds 1-15 of 17)

75 topics reviewed. **72 PASS | 2 WARNING | 1 FAIL**

---

### 1. [MEM-05] NVS dirty flag data race — WARNING

Non-atomic compound struct sharing between loop and NVS task has a theoretical torn-read window, but practical risk is negligible on this architecture.

**Spec says**: "NVS writes happen in a dedicated FreeRTOS task (low priority). Loop never blocks on flash."

**Code does**: The NVS task and the loop task both run on Core 1 at FreeRTOS priority 1 (`src/managers/NvsManager.cpp:52`). The queue methods (e.g., `queueScaleWrite` at line 190, `queueArpPotWrite` at line 230) write to plain (non-atomic) compound structs (`_pendingScale[]`, `_pendingArpPot[]`), then set `std::atomic<bool>` dirty flags. The NVS task's `commitAll()` (line 267) reads these same structs via `prefs.putBytes()` after checking the atomic dirty flags.

The dirty flags themselves are properly atomic. The issue is that the compound struct writes/reads are not atomic. For example:

- `_pendingScale[bankIdx] = cfg;` (line 192) — a 3-4 byte struct assignment
- `_pendingArpPot[bankIdx] = newArp;` (line 173) — an 8-byte struct assignment

These are read by `commitAll()` at lines 287 and 349 without synchronization beyond the atomic dirty flag.

**Scenario**: A tick interrupt fires during `commitAll()`'s `prefs.putBytes(&_pendingArpPot[i], sizeof(ArpPotStore))` internal memcpy, preempting mid-copy of the 8-byte struct. The loop task then overwrites `_pendingArpPot[currentBank]` with new values. When `commitAll()` resumes, it has already copied some old bytes and will copy the remaining new bytes, resulting in a torn struct written to flash. Consequence: on next reboot, one bank might load slightly inconsistent arp params. Very low probability (~nanosecond window), benign failure mode.

**Suggested fix** (optional, correctness-by-construction):
```cpp
// In NvsManager.h:
portMUX_TYPE _arpPotMux = portMUX_INITIALIZER_UNLOCKED;

// In queueArpPotWrite:
portENTER_CRITICAL(&_arpPotMux);
_pendingArpPot[bankIdx] = { ... };
portEXIT_CRITICAL(&_arpPotMux);

// In commitAll:
portENTER_CRITICAL(&_arpPotMux);
ArpPotStore localCopy = _pendingArpPot[i];
portEXIT_CRITICAL(&_arpPotMux);
prefs.putBytes(key, &localCopy, sizeof(ArpPotStore));
```

---

### 2. [CONV-03] Naming conventions — WARNING

Naming conventions are mostly followed with a few minor inconsistencies.

**Spec says**: `s_` prefix for static globals, `_` prefix for members, `SCREAMING_SNAKE_CASE` for constants.

**Code does**:
1. `static USBMIDI usbMidi(...)` at `src/core/MidiTransport.cpp:17` — should be `s_usbMidi`.
2. `sensitivityTargets` and `sensitivityNames` in `src/setup/ToolCalibration.cpp:13-14` — `static const` arrays using camelCase instead of `SCREAMING_SNAKE_CASE`.
3. Several `static const` string arrays in `ToolSettings.cpp` use `s_` prefix (`s_profileNames`, `s_bleNames`, etc.) instead of `SCREAMING_SNAKE_CASE`.
4. `CapacitiveKeyboard.h` uses bare camelCase for private members — but this file is DO NOT MODIFY (V1 port), so accepted exception.

**Scenario**: No runtime impact. Cosmetic inconsistency only.

**Suggested fixes**:
- Rename `usbMidi` to `s_usbMidi` in `MidiTransport.cpp`
- Rename `sensitivityTargets` to `SENSITIVITY_TARGETS` and `sensitivityNames` to `SENSITIVITY_NAMES` in `ToolCalibration.cpp`

---

### 3. [VT100-04] Button-as-ENTER input — FAIL

Physical button is not mapped as ENTER/confirm input in setup mode.

**Spec says**: "VT100 terminal, serial input + button = ENTER" — a physical button press should function as an ENTER/confirm input alongside serial terminal input.

**Code does**: `InputParser::update()` (`src/setup/InputParser.cpp:12-86`) only reads from `Serial`. No `digitalRead()` of any button GPIO is performed. The `NAV_ENTER` event is only produced from serial `\r`/`\n` characters.

**Scenario**: A user in setup mode who prefers the physical button (or has limited terminal access) cannot confirm any selection. All 6 tools rely on `NAV_ENTER` events for confirmation, but this event can never come from a button press.

**Suggested fix**: Add rear button polling inside `InputParser::update()`. After the `Serial.available()` check, poll `digitalRead(BTN_REAR_PIN)` with edge detection and debounce, and return `NAV_ENTER` on a rising-edge (release) event:

```cpp
// In InputParser::update(), after serial handling:
bool rearNow = (digitalRead(BTN_REAR_PIN) == LOW);
if (!rearNow && _rearWasPressed) {  // release edge
    _rearWasPressed = false;
    return {NAV_ENTER, 0};
}
_rearWasPressed = rearNow;
```

---

**Topics not audited** (rounds 16-17 skipped): RGB-12, RGB-13, RGB-14, ARCH-01, ARCH-02, ARCH-03, ARCH-04, ARCH-05
