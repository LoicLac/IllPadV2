# Bank Pad Double-Tap → Pause/Resume Arp (design)

**Date**: 2026-04-18
**Status**: approved in brainstorming
**Scope**: runtime behavior only — no NVS changes, no new setup tool

## 1. Goal

While holding the **LEFT button** (the "shift" button — not the hold pad), a **double-tap** on any bank pad toggles **play/pause** on the arp of that bank, **whether the bank is FG or BG**. The double-tap does **not** change the current bank.

Single-tap behavior is preserved (bank switch for different bank, no-op if already current).

## 2. Decisions (from brainstorming)

| # | Topic | Choice |
|---|---|---|
| 1 | Stop semantics | Toggle play/pause via `_pausedPile` (pile preserved, frozen) |
| 2 | Switch vs double-tap conflict | Switch is **deferred** by `s_doubleTapMs` — no rollback |
| 3 | LED feedback | Reuse `CONFIRM_HOLD_ON` (resume, fade IN, deep blue) / `CONFIRM_HOLD_OFF` (pause, fade OUT) with target LED of the bank, via existing `ledMask` mechanism |
| 4 | Deferral scope | Conditional — only when target bank is ARPEG. NORMAL target = instant switch |
| 5 | Timing window | Reuse `s_doubleTapMs` (100–250 ms, default 150 ms, configured in Tool 5) |
| 6 | Pile empty (IDLE) | No-op silencieux — no LED, switch proceeds naturally |
| 7 | `WAITING_QUANTIZE` state | Treated as playing → pause cancels the wait |
| 8 | LEFT released during pending | Fast-forward commit the pending switch |
| 9 | Architecture | All in `BankManager`; new `ArpEngine::togglePauseResume()` API |

## 3. Contract (user-facing)

| Bank target | 1st tap (LEFT held) | 2nd tap within `s_doubleTapMs` | Observable latency |
|---|---|---|---|
| NORMAL, different from FG | Switch immediate | — | none |
| NORMAL, == FG | No-op | — | none |
| ARPEG, different from FG | Pending switch armed (invisible) | Toggle pause/resume on target bank + cancel pending switch | ≈150 ms on single-tap |
| ARPEG, == FG | No-op | Toggle pause/resume on FG | none |

### Toggle semantics (ARPEG, pile non-vide)

| Before | Action | After |
|---|---|---|
| `_playing=true` | Pause | `_playing=false`, `_pausedPile=true`, `flushPendingNoteOffs` |
| `_waitingForQuantize=true` | Pause (cancels wait) | `_playing=false`, `_waitingForQuantize=false`, `_pausedPile=true` |
| `_pausedPile=true, _positionCount>0` | Resume | `_playing=true`, `_stepIndex=-1`, `_shuffleStepCounter=0`, `_waitingForQuantize=(quantizeMode != IMMEDIATE)`, `_pausedPile=false` |
| `_positionCount==0` (IDLE) | No-op | unchanged |

### LED confirmations
- Pause → `CONFIRM_HOLD_OFF` fade OUT 100→0% (deep blue, `_holdOffFadeMs` ≈ 300 ms) on LED of targeted bank
- Resume → `CONFIRM_HOLD_ON` fade IN 0→100% on LED of targeted bank

## 4. Architecture

### New API: `ArpEngine::togglePauseResume(MidiTransport&)`

Returns `enum class TogglePauseResumeResult { NoOp, Paused, Resumed }`.
- `NoOp`: pile empty, nothing happens.
- `Paused`: transitioned from playing/waiting-quantize → paused. Caller should trigger `CONFIRM_HOLD_OFF`.
- `Resumed`: transitioned from paused → playing. Caller should trigger `CONFIRM_HOLD_ON`.

### State added to `BankManager`

```cpp
uint32_t _lastBankPadPressTime[NUM_BANKS];  // timestamp ms of last rising edge per bank pad
int8_t   _pendingSwitchBank;                 // -1 = none, else target bank 0..7
uint32_t _pendingSwitchTime;                 // timestamp ms when pending armed
uint8_t  _doubleTapMs;                       // cached from settings (injected)
MidiTransport* _transport;                   // injected via begin()
```

### Extended `BankManager::begin()`

Add `MidiTransport* transport` parameter. Cached internally for `ArpEngine::togglePauseResume()`.

Add `setDoubleTapMs(uint8_t ms)` method (called once at boot from settings, same as `s_doubleTapMs` wiring).

### LedController extension

In `renderNormalDisplay()`, the `CONFIRM_HOLD_ON` / `CONFIRM_HOLD_OFF` cases currently force rendering on `_currentBank`. Change to the same pattern already used for `CONFIRM_SCALE_*`:

```cpp
uint8_t mask = (_confirmLedMask != 0) ? _confirmLedMask : (1 << _currentBank);
for (uint8_t led = 0; led < NUM_LEDS; led++) {
  if (!(mask & (1 << led))) continue;
  // render fade on this led
}
```

Zero behavioral change for existing callers (they don't pass `ledMask` → default to `_currentBank`). New callers pass `1 << targetBank`.

## 5. Data flow in `BankManager::update()`

Called every Core 1 loop iteration.

```
1. Detect bank pad press edges
   For each b in 0..NUM_BANKS:
     wasPressed = (keyIsPressed[_bankPads[b]] && previousState)
     isPressed  = keyIsPressed[_bankPads[b]]
     if isPressed && !wasPressed:  // rising edge
       → edge detected for bank b

2. On rising edge for bank b (btnLeftHeld only):
   now = millis()
   wasRecent = (_lastBankPadPressTime[b] != 0) &&
               ((now - _lastBankPadPressTime[b]) < _doubleTapMs)

   // Try double-tap toggle first
   if wasRecent && _banks[b].type == BANK_ARPEG && _banks[b].arpEngine:
     result = _banks[b].arpEngine->togglePauseResume(*_transport)
     if result != NoOp:
       // Confirmed double-tap: consume it
       _lastBankPadPressTime[b] = 0      // prevent triple-tap chaining
       _pendingSwitchBank = -1            // cancel any pending switch
       _leds->triggerConfirm(
         (result == Paused) ? CONFIRM_HOLD_OFF : CONFIRM_HOLD_ON,
         0, 1 << b)
       return
     // result == NoOp: fall through to 1st-tap logic (pile empty case)

   // 1st-tap logic (or NoOp fallthrough)
   _lastBankPadPressTime[b] = now
   if b == _currentBank:
     return  // already current, no switch
   if _banks[b].type == BANK_ARPEG:
     // Defer switch ~doubleTapMs to allow double-tap detection
     _pendingSwitchBank = b
     _pendingSwitchTime = now
   else:
     // NORMAL: instant switch (no double-tap feature on NORMAL)
     switchToBank(b)

3. Pending switch expiration
   if _pendingSwitchBank >= 0:
     if (now - _pendingSwitchTime) >= _doubleTapMs:
       switchToBank(_pendingSwitchBank)
       _pendingSwitchBank = -1

4. LEFT released (btnLeftHeld went true → false):
   if _pendingSwitchBank >= 0:
     switchToBank(_pendingSwitchBank)  // fast-forward
     _pendingSwitchBank = -1
```

**Arm/disarm** (existing `_armed` flag) still applies for bank pad debouncing, but is evaluated per-edge rather than in the old tight loop.

## 6. Edge cases

| Case | Behavior |
|---|---|
| LEFT not held at all | No edge detection runs ; regular pad behavior applies ; `_lastBankPadPressTime` not updated |
| LEFT released between 1st and 2nd tap | Fast-forward pending switch on release ; 2nd tap (if any) does not trigger (LEFT not held) |
| 3rd rapid tap after a double-tap | `_lastBankPadPressTime[b]` was zeroed at double-tap ; 3rd tap is treated as a new 1st tap |
| Tap on bank A then tap on bank B within window | B's `_lastBankPadPressTime[B]` is 0 → no double-tap detected ; B's tap is a new 1st tap. Any pending switch from A is canceled (replaced by B's pending or A's instant switch already happened for NORMAL) |
| Target bank ARPEG but pile empty | `togglePauseResume` returns NoOp ; no LED ; no pending-switch cancellation. The 2nd tap falls through to normal 1st-tap logic (re-arms pending switch if b != current, or no-op if already current). User still gets the switch as intended. |
| Quick double-tap on NORMAL bank different from FG | 1st tap = instant switch (NORMAL rule) ; 2nd tap lands on bank that is now FG ; treated as "tap on == FG" → no-op. No bad effect. |
| Bank type changes between 1st and 2nd tap (impossible at runtime — would need Tool 4 re-entry which reboots) | Not applicable |
| `_doubleTapMs` changed via Tool 5 mid-session | Applied via `setDoubleTapMs()` call point (same hook as existing `s_doubleTapMs`). No change expected mid-song. |
| Rapid bank switches (existing known concern: 4+ switches <500ms while pad held) | Same as before — not worsened. Deferral adds 150ms delay but debounces naturally. |

## 7. Testing plan

Manual on hardware (no unit tests — embedded firmware, no rig).

1. **Basic pause/resume, FG ARPEG**
   - Bank 1 = ARPEG, hold a few pads → arp playing
   - LEFT + double-tap bank 1 pad → `CONFIRM_HOLD_OFF` fade OUT, arp stops, noteOffs flushed
   - LEFT + double-tap bank 1 pad again → `CONFIRM_HOLD_ON` fade IN, arp resumes from step 0
2. **Pause on BG ARPEG**
   - Bank 1 = ARPEG playing, switch to bank 2 (NORMAL)
   - LEFT + double-tap bank 1 pad → fade OUT on bank 1 LED (BG), arp bank 1 stops, no switch occurs
   - Check : current bank still 2, play a pad on bank 2 → normal note
3. **Resume on BG ARPEG**
   - Continue from #2: bank 1 paused in BG
   - LEFT + double-tap bank 1 → fade IN on bank 1 LED (BG), arp resumes on channel 1, still no switch
4. **Switch with latency on NEW ARPEG**
   - On bank 1, LEFT + single-tap bank 3 (ARPEG) → after ~150ms switch occurs with `CONFIRM_BANK_SWITCH`
5. **Snappy switch on NEW NORMAL**
   - On bank 1, LEFT + single-tap bank 2 (NORMAL) → instant switch, no latency
6. **Double-tap same bank == FG**
   - On bank 1 (ARPEG, FG), LEFT + double-tap bank 1 → fade OUT, arp paused, no switch
7. **LEFT release during pending**
   - LEFT + press bank 3 (ARPEG) → pending armed
   - Release LEFT before 150ms → switch commits immediately
8. **Pile empty → no-op**
   - Bank 1 = ARPEG, pile empty, LEFT + double-tap bank 1 → no fade, no effect
9. **WAITING_QUANTIZE pause**
   - Bank 1 ARPEG, quantize = Bar. Capture pile (hold pad). Within the quantize wait, LEFT + double-tap bank 1 → fade OUT, pause. Resume plays a new WAITING_QUANTIZE period.
10. **MIDI clean on pause**
    - BLE MIDI monitor tool open. Pause during playing → no stuck notes on channel. All noteOffs received.

## 8. Non-goals / out of scope

- Unit tests (no rig)
- Persistence of pause state across reboot (ephemeral)
- Configurable fade duration separate from `_holdOnFadeMs / _holdOffFadeMs`
- CLAUDE.md rewrite regarding `CONFIRM_PLAY / CONFIRM_STOP` (doc↔code divergence — to be handled in separate audit task)
- Changes to hold pad (`_captured`) mechanism
- Changes to play/stop pad (still only active in HOLD ON mode)

## 9. Files touched

| File | Change |
|---|---|
| `src/arp/ArpEngine.h` | `enum class TogglePauseResumeResult` + `togglePauseResume()` declaration |
| `src/arp/ArpEngine.cpp` | Implement `togglePauseResume()` |
| `src/managers/BankManager.h` | New members, new `begin()` signature, new `setDoubleTapMs()` method |
| `src/managers/BankManager.cpp` | Edge detection, double-tap logic, pending switch logic, toggle call |
| `src/core/LedController.cpp` | `renderNormalDisplay()`: HOLD_ON/HOLD_OFF cases use `ledMask` like SCALE_* |
| `src/main.cpp` | `BankManager::begin()` call passes `&s_midiTransport`; `setDoubleTapMs()` called after settings load |
| `docs/reference/architecture-briefing.md` | New sub-section under Flow 3 (Bank Switch) documenting deferral + double-tap |
