# ILLPAD V2 — Bug Routine Report

---
## Run — 2026-03-21 16:45

### 1. [ARP-04] Pattern UpDown no duplicate at boundaries — PASS
The UpDown pattern correctly avoids duplicating boundary notes. At `ArpEngine.cpp:256-262`, the reversed middle portion iterates from `upLen - 2` down to `1` (inclusive), skipping both the highest and lowest elements.

### 2. [CLK-05] Follow Transport gating — PASS
Start/Stop/Continue are correctly gated by `_followTransport` in `ClockManager.cpp:40,46,56`, while `onMidiClockTick()` (line 31) has no such check, ensuring clock ticks are always received.

### 3. [VT100-02] Color code correctness — PASS
All ANSI color codes across the 8 setup source files are correctly paired with VT_RESET within the same Serial.printf call, with no color bleed paths identified.

### 4. [ARCH-02] Rapid bank switching under load — PASS
Bank switching is well-protected — the arm/disarm debounce ensures exactly one switch per touch-release cycle, `allNotesOff` fires exactly once on the correct (old) channel, and `isForeground` transitions are gap-free.

### 5. [BTN-02] Play/stop pad dual role — PASS
The play/stop pad correctly acts as a transport toggle only when ARPEG + HOLD ON; in HOLD OFF mode it falls through to normal arp note processing, and in NORMAL mode it is processed as a regular music pad.

**Score: 5 PASS | 0 WARNING | 0 FAIL**

---
## Run — 2026-03-21 21:50

### 1. [MEM-02] No volatile for inter-core sync — PASS
Under `src/`, the only `volatile` mention is a comment in `main.cpp` forbidding it; inter-core handoff uses `std::atomic` (e.g. `s_active`, pot globals, `ClockManager`, `MidiTransport`, `NvsManager` flags), matching CLAUDE.md’s “not mutex, not volatile” / “`std::atomic` for inter-core sync — NEVER `volatile`” rules.

### 2. [NOTE-03] Orphan note prevention on scale change — PASS
`MidiEngine::noteOff` only sends the MIDI note stored at `noteOn` (`_lastResolvedNote`), and NORMAL scale edits call `allNotesOff()` first so active notes are released at the correct pitches before `ScaleConfig` changes.

### 3. [ARPT-02] MIDI Start bypasses quantize — PASS
After `0xFA`, `main.cpp` consumes the start and calls `ArpEngine::resetStepIndex()`, which sets `_waitingForQuantize = false` before `ArpScheduler.tick()` runs in the same loop iteration, matching the spec that MIDI Start bypasses quantize.

### 4. [NVS-01] NVS writes never block loop — FAIL
FAIL: `notifyIfDirty()` is non-blocking, but the codebase does not confine all NVS writes to `nvsTask`, and `notifyIfDirty()` also clears `_anyDirty`.

## 1. Spec says

From `.claude/CLAUDE.md` (NVS section):

> **NVS writes happen in a dedicated FreeRTOS task** (low priority). **Loop never blocks on flash.**

And in the loop order:

> `15. NvsManager.notifyIfDirty() ← non-blocking signal to NVS task`

## 2. Code does

**`notifyIfDirty()`** — Not only `xTaskNotifyGive`; it also assigns `_anyDirty = false`:

```79:84:src/managers/NvsManager.cpp
void NvsManager::notifyIfDirty() {
  if (_anyDirty && _taskHandle) {
    xTaskNotifyGive((TaskHandle_t)_taskHandle);
    _anyDirty = false;
  }
}
```

No `Preferences` / flash writes here.

**Dedicated task** — `nvsTask` waits on `ulTaskNotifyTake`, delays 50 ms, then calls `commitAll()` (where `Preferences` writes occur for queued runtime state):

```61:69:src/managers/NvsManager.cpp
void NvsManager::nvsTask(void* arg) {
  NvsManager* self = (NvsManager*)arg;
  for (;;) {
    // Wait for notification (indefinite block)
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Small delay to batch multiple dirty flags
    vTaskDelay(pdMS_TO_TICKS(50));
    self->commitAll();
  }
}
```

**Writes outside `nvsTask`** (direct `Preferences` + `put*` on the thread that runs setup tools — `SetupManager::run()` blocks inside `setup()` **before** `s_nvsManager.begin()` creates the NVS task):

| Location | What |
|----------|------|
| `src/core/CapacitiveKeyboard.cpp` | `saveCalibrationData()` — `prefs.putBytes(...)` |
| `src/setup/ToolCalibration.cpp` | calls `_keyboard->saveCalibrationData()` |
| `src/setup/ToolPadOrdering.cpp` | `prefs.putBytes(NOTEMAP_NVS_KEY, ...)` (comment: NVS task not running) |
| `src/setup/ToolPadRoles.cpp` | multiple `prefs.putBytes` / `putUChar` for bank/scale/arp pads |
| `src/setup/ToolBankConfig.cpp` | `prefs.putBytes` for bank types + quantize |
| `src/setup/ToolSettings.cpp` | `prefs.putBytes(SETTINGS_NVS_KEY, ...)` |
| `src/setup/ToolPotMapping.cpp` | `prefs.putBytes(POTMAP_NVS_KEY, ...)` |

`src/main.cpp` uses `Preferences` only to **read** BLE interval before transport init (`getBytes` / read-only open), not a write.

**Boot order** — NVS task starts only after setup can exit and after `loadAll`:

```410:414:src/main.cpp
  // NVS Manager — start task (after all loading done)
  s_nvsManager.begin();
  #if DEBUG_SERIAL
  Serial.println("[INIT] NvsManager OK.");
  #endif
```

So setup-mode saves intentionally bypass `nvsTask` (it does not exist during `SetupManager::run()`).

## 3. Scenario

For **normal performance** after boot, `loop()` only signals via `notifyIfDirty()`; persistent changes go through `commitAll()` in `nvsTask`, so the **musical** path matches the “don’t block `loop()` on flash” intent.

The **stated** rule “all Preferences writes happen in the dedicated NVS task” is still wrong in code: any save from setup tools or calibration runs synchronous flash on the setup thread. Effect there is **setup UX** (serial menu / LED updates can hitch for tens of ms), not typical stuck-note behavior, because `Arduino loop()` and MIDI/sensing are not in the same phase as that blocking `while` in `SetupManager::run()`.

Risk if the spec is read literally: a future change could add a `prefs.put*` on the hot path while assuming “only the NVS task ever writes.”

## 4. Suggested fix

- **Documentation**: Amend the spec to: runtime/autosave NVS writes go through `nvsTask`; **setup/calibration** use direct `Preferences` writes because the NVS task is not started until after setup exits (see `ToolPadOrdering.cpp` comment pattern).

- **Optional hardening** (if you want one writer): after `nvsTask` exists, replace direct setup `put*` with “queue blob + `xTaskNotifyGive`” and perform writes only in `nvsTask` (larger change; setup must not depend on the task before it exists, or you start the task earlier and defer writes).

**`notifyIfDirty` wording**: If the spec requires “only `xTaskNotifyGive`”, either accept `_anyDirty = false` as part of the signaling protocol or document that the function “notifies the NVS task and clears the published dirty flag.”

### 5. [MIDI-04] ILLPAD never sends Start/Stop/Continue — PASS
In master mode the firmware only transmits MIDI clock (`0xF8`) via `MidiTransport::sendClockTick()`; there are no call sites that send `0xFA`, `0xFB`, or `0xFC` (those bytes appear only on the inbound/USB-BLE receive paths into `ClockManager`).

**Score: 4 PASS | 0 WARNING | 1 FAIL | 0 ERROR**

---
## Run — 2026-03-21 22:15 (round 1/3)

### 1. [MEM-01] Double buffer atomics — PASS
`s_active` correctly uses `memory_order_release` on store (Core 0, `main.cpp:111`) and `memory_order_acquire` on load (Core 1, `main.cpp:438`), forming a valid happens-before relationship that guarantees all buffer writes are visible to the consumer before it reads them.

### 2. [NOTE-04] MIDI panic completeness — PASS
The `midiPanic()` routine in `main.cpp:124-140` correctly covers all three note sources: Phase 1 flushes all ArpEngine pending events and refcounts (all ARPEG banks, each on its own channel), Phase 2 clears MidiEngine tracked NORMAL-mode notes (foreground channel), and Phase 3 sends CC 123 (All Notes Off) on all 8 channels via both USB and BLE as a comprehensive safety net.

### 3. [ARPE-02] Refcount noteOn 0-to-1 only — PASS
The `refCountNoteOn` method at `ArpEngine.cpp:518-526` correctly gates the actual MIDI noteOn send behind `_noteRefCount[note] == 0`, and all arp noteOn paths (immediate in `tick` line 408, deferred in `processEvents` line 450) route through this method — no bypass exists.

### 4. [LED-01] Priority state machine order — PASS
The `update()` if/else chain in `LedController.cpp` matches the spec priority order exactly: boot (line 110) > chase (line 131) > error (line 142) > battery (line 150) > bargraph (line 175) > confirmation (line 187) > calibration (line 257) > normal (line 278).

### 5. [POT-03] Global target catch propagation — PASS
Global target catch propagation is correctly implemented — when a non-per-bank target (e.g., Tempo) is written through one binding, `PotRouter::applyBinding()` (lines 508-520 of `PotRouter.cpp`) propagates `storedValue` to all other bindings sharing the same target, pot index, and button mask, preventing stale catch state after bank type switch.

**Score: 5 PASS | 0 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-21 22:15 (round 2/3)

### 1. [CONV-02] Unsigned millis overflow safety — PASS
Every millis() and micros() comparison in the codebase consistently uses the overflow-safe unsigned subtraction pattern `(now - startTime) < duration`, with no instances of the unsafe `(startTime + duration) > now` form.

### 2. [ARCH-05] Background arp survives all foreground operations — PASS
Background ARPEG banks survive all foreground operations — the ArpScheduler unconditionally ticks all registered engines with no foreground awareness, BankManager.switchToBank() only flushes MidiEngine (NORMAL notes), ScaleManager only modifies the foreground slot's scale, pot updates only push to the foreground slot's ArpEngine, and NORMAL-mode note processing has zero coupling to the arp subsystem.

### 3. [CLK-01] PLL smoothing — PASS
ClockManager::updatePLL (`ClockManager.cpp:156-186`) implements a two-stage smoothing pipeline — a 24-sample circular buffer moving average of tick intervals followed by an IIR (exponential moving average) filter on the derived BPM, with a lower alpha for BLE (0.1) than USB (0.3), directly confirming the PLL reduces BLE jitter by averaging/filtering rather than using raw timestamps.

### 4. [NVS-04] NVS batching delay — PASS
The NVS task coalesces rapid dirty flags with a 50ms delay (`vTaskDelay(pdMS_TO_TICKS(50))` at `NvsManager.cpp:67`) after receiving a task notification and before calling `commitAll()`, allowing multiple dirty flags set in quick succession to be batched into a single flash commit.

### 5. [BOOT-02] Failure blink on step 3 — PASS
MPR121/keyboard failure at step 3 correctly triggers `showBootFailure(3)`, which renders LEDs 1-2 solid and LED 3 blinking rapidly (150ms toggle), inside an infinite loop that halts the system forever — fully matching the spec.

**Score: 5 PASS | 0 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-21 22:15 (round 3/3)

### 1. [ARPT-04] Shuffle offset formula — PASS
The shuffle offset formula in `ArpEngine.cpp:380` exactly matches the spec: `template[step%16] * depth * stepDuration / 100`, using `SHUFFLE_TEMPLATES[_shuffleTemplate][_shuffleStepCounter % 16] * _shuffleDepth * stepDurationUs / 100.0f`.

### 2. [MIDI-01] Channel routing correctness — PASS
NORMAL mode correctly sends on MidiEngine's current channel (set by BankManager to the foreground bank index on every switch), and ARPEG mode correctly sends on each ArpEngine's own stored channel (set at boot to the bank index, independent of foreground state), using MidiTransport directly rather than going through MidiEngine.

### 3. [BTN-01] Left and rear modifiers never cross — PASS
Left button only modifies right pots (indices 0-3) and rear button only modifies the rear pot (index 4) — the `resolveBindings()` method in `PotRouter.cpp:354-394` explicitly guards against cross-modifier paths by skipping all right-pot bindings when the rear button is held and ignoring the left button bit entirely when resolving the rear pot.

### 4. [ARP-06] Octave expansion — PASS
Octave expansion correctly builds a sequence of `positionCount * octaveRange` entries using encoding `pos + oct * 48`, decodes at tick time to recover the base note via `ScaleResolver::resolve()`, and transposes each octave by `+12` semitones with proper clamping (1-4 range, MIDI note > 127 fold-down).

### 5. [MEM-05] NVS dirty flag data race — WARNING
Compound structs `_pendingScale` (ScaleConfig, 3 bytes) and `_pendingArpPot` (ArpPotStore, 8 bytes) are written by the loop task and read by the NVS task on the same core at the same priority, with no synchronization — theoretically allowing torn reads via tick-interrupt preemption, though the impact is limited to corrupted NVS persistence (not live MIDI).

**Spec says** (from CLAUDE.md):
> "NVS writes happen in a **dedicated FreeRTOS task** (low priority). Loop never blocks on flash."

The NVS task is actually created at priority 1 (`NvsManager.cpp:52`), which is the **same** priority as the Arduino `loopTask`, not "low priority" as the spec claims.

**Code does**:

The loop task (Core 1, priority 1) writes compound structs without synchronization:

- `NvsManager.cpp:192`: `_pendingScale[bankIdx] = cfg;` — 3-byte struct copy compiled to multiple store instructions.
- `NvsManager.cpp:234-239`: Six individual field writes to `_pendingArpPot[bankIdx]` — widest race window.
- `NvsManager.cpp:173`: `_pendingArpPot[currentBank] = newArp;` — 8-byte struct copy from `tickPotDebounce`.

The NVS task (Core 1, priority 1) reads these without synchronization in `commitAll()`:
- `NvsManager.cpp:287`: `prefs.putBytes(key, &_pendingScale[i], sizeof(ScaleConfig));`
- `NvsManager.cpp:349`: `prefs.putBytes(key, &_pendingArpPot[i], sizeof(ArpPotStore));`

FreeRTOS enables time-slicing for equal-priority tasks, so the tick interrupt (1ms) can context-switch the loop task mid-struct-write to the NVS task, which then reads the partially written struct.

**Scenario**:

Musician switches from chromatic to D Dorian (sets `chromatic=false, root=3, mode=1`). `queueScaleWrite` begins writing `_pendingScale[bank]` but the tick interrupt fires mid-copy. NVS task wakes and reads a partially written struct. Stale `mode` is persisted to flash. On next power cycle, the device boots with a wrong scale. The musician hears wrong notes and must reconfigure. For `ArpPotStore` (6 individual field writes), a corrupted division or pattern value on reboot could cause the arp to run at unexpected speed or wrong pattern.

Practical risk is LOW — the 50ms batching delay reduces probability, but does not eliminate the race.

**Suggested fix**:

Simplest: lower the NVS task priority to 0 (matching the spec's stated "low priority"). Since both tasks are single-core, the NVS task would only run during `vTaskDelay(1)` in the loop, eliminating all race conditions:

```cpp
// In NvsManager.cpp:52, change priority from 1 to 0:
xTaskCreatePinnedToCore(nvsTask, "nvs", 4096, this, 0, (TaskHandle_t*)&_taskHandle, 1);
//                                                   ^ was 1, now 0 (below loopTask)
```

Alternative: use `portENTER_CRITICAL`/`portEXIT_CRITICAL` around the struct writes and reads (sub-microsecond overhead for 3-8 byte structs).

**Score: 4 PASS | 1 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-21 22:50 (round 1/5)

### 1. [MEM-03] Slow param atomics — PASS
All three pot-to-Core0 slow parameters (responseShape, slewRate, padSensitivity) are declared as `std::atomic<float>`, `std::atomic<uint16_t>`, and `std::atomic<uint8_t>` respectively, and consistently use `memory_order_relaxed` on both the store side (Core 1, main.cpp:696-698) and the load side (Core 0, main.cpp:97-99).

### 2. [NOTE-01] noteOff uses lastResolvedNote — PASS
Every noteOff code path (MidiEngine::noteOff, MidiEngine::allNotesOff, ArpEngine::refCountNoteOff, ArpEngine::flushPendingNoteOffs, ArpEngine::processEvents) uses stored note values (_lastResolvedNote for NORMAL, event queue notes and refcount array for ARPEG) and never calls ScaleResolver::resolve(). Only two resolve() call sites exist, both exclusively in noteOn/scheduling paths.

### 3. [ARP-01] Pile stores positions not notes — PASS
`_positions[]` and `_positionOrder[]` store padOrder ranks (0-47), never resolved MIDI notes. The pile API receives `s_padOrder[i]` from main.cpp, and MIDI note resolution is deferred to `tick()` time via `ScaleResolver::resolve()`.

### 4. [ARPE-04] Atomic noteOff-first scheduling — PASS
`ArpEngine::tick()` (ArpEngine.cpp:395-422) correctly schedules the noteOff event first, returns immediately if noteOff scheduling fails, and when noteOn scheduling subsequently fails, rolls back the already-scheduled noteOff by scanning the event queue in reverse and deactivating the matching entry.

### 5. [CLK-03] MIDI Start resets tick counter — PASS
MIDI Start (0xFA) correctly sets `_currentTick = 0` in `ClockManager::update()` (ClockManager.cpp:71), and main.cpp additionally resets arp step indices and scheduler sync for a full bar-1 restart.

**Score: 5 PASS | 0 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-21 22:50 (round 2/5)

### 1. [LED-02] Sine pulse integer math — PASS
The 64-entry sine LUT (`_sineTable[64]`) is precomputed in the constructor using `sinf()` (LedController.cpp:46), and all sine pulse rendering in `update()` uses strictly integer math — `uint8_t` and `uint16_t` operations with `/255` scaling, with no floating-point operations anywhere in the update path.

### 2. [NVS-02] Boot-time reads are pre-loop — PASS
All NVS reads occur in `setup()` before `loop()` starts — via `NvsManager::loadAll()`, `CapacitiveKeyboard::begin()`, or a direct settings pre-read for BLE interval. Runtime NVS writes use a non-blocking queue + dedicated FreeRTOS task pattern with no blocking reads.

### 3. [POT-01] Catch system prevents parameter jumps — PASS
The catch system correctly gates pot output behind a `POT_CATCH_WINDOW` (100 ADC units) proximity check — on any context switch (binding change, bank switch, or mapping reload), the binding is marked uncaught and the pot must physically reach within the window of the stored value before writes resume.

### 4. [BTN-04] Hold toggle ARPEG only — PASS
The hold toggle pad is correctly gated by `slot.type == BANK_ARPEG` in `ScaleManager.cpp:192`, ensuring it only activates on ARPEG banks. On NORMAL banks, the hold pad functions as a regular music pad.

### 5. [CONV-04] Debug macro zero overhead — PASS
All runtime `Serial` calls are properly guarded by `#if DEBUG_SERIAL` (or `#if DEBUG_HARDWARE`) using value-based preprocessor conditionals, ensuring true zero overhead in the MIDI-critical loop when disabled. The only unguarded Serial usage is boot-time-only calls in `setup()` and one fatal-error-path print.

**Score: 5 PASS | 0 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-21 22:50 (round 3/5)

### 1. [MIDI-02] BLE noteOn/Off bypass queue — PASS
NoteOn and noteOff bypass any application-level queue and are sent immediately through MidiTransport to the BLE stack (direct `pCharacteristic->notify()` calls), while aftertouch is rate-limited through a 64-entry ring buffer in MidiEngine with silent overflow drop.

### 2. [VT100-01] Screen clearing and cursor positioning — PASS
All setup tools use well-defined VT100 macros (`VT_CLEAR`, `VT_HOME`, `VT_CL`, etc.) centralized in `SetupUI.h`, with screen clearing and cursor positioning routed through `SetupUI::vtClear()`, `vtHome()`, `vtFrameStart()`, and `vtFrameEnd()` methods. No raw terminal escape sequences appear outside the centralized definitions.

### 3. [ARCH-01] Scale change during held arp notes — PASS
Scale change on a foreground ARPEG bank correctly flushes all pending MIDI events (old notes) via two-phase `flushPendingNoteOffs` (cancel events + sweep refcount) before `setScaleConfig` updates the scale, and the next `ArpScheduler.tick()` re-resolves pile positions to new MIDI notes — no stuck notes, no wrong notes, no rhythmic interruption.

### 4. [ARPT-01] Quantized start boundaries — PASS
Quantized start correctly snaps Beat to the next multiple of 24 ticks and Bar to the next multiple of 96 ticks, via `currentTick % boundary != 0` check in `ArpEngine::tick()` (ArpEngine.cpp:315-318). All nine division tick counts evenly divide into both 24 and 96, guaranteeing the boundary will always be reached.

### 5. [ARP-05] Pattern Order preserves insertion order — PASS
The ARP_ORDER pattern correctly uses `_positionOrder[]` (chronological insertion order) rather than `_positions[]` (sorted), as confirmed at `ArpEngine.cpp:232` with explicit branching and a clear comment documenting the distinction.

**Score: 5 PASS | 0 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-21 22:50 (round 4/5)

### 1. [MEM-04] Clock callback atomics — PASS
ClockManager uses correct release/acquire pairing on `_newTickAvailable` (release on store in `onMidiClockTick` line 35, acquire on load in `update` line 75), guaranteeing visibility of `_lastExternalTickUs` and `_lastSource` written before it. Transport flags use `acq_rel` exchange for consume. All orderings are correct for cross-task BLE callback scenarios.

### 2. [NOTE-02] allNotesOff sweep completeness — PASS
`MidiEngine::allNotesOff()` (MidiEngine.cpp:87-100) correctly iterates all 48 pads, sends a noteOff for each pad where `_lastResolvedNote[i] != 0xFF`, clears the entry to `0xFF`, resets corresponding `_lastSentPressure`, and drains the aftertouch ring buffer.

### 3. [ARPE-01] Event array cap at 36 — PASS
MAX_PENDING_EVENTS is correctly defined as 36 at `ArpEngine.h:17`, and overflow is handled gracefully — `scheduleEvent()` returns false on a full queue, `tick()` schedules noteOff before noteOn to guarantee atomicity, and if noteOff fails the step is skipped entirely (no stuck notes). `flushPendingNoteOffs()` provides a two-phase safety sweep ensuring zero stuck notes on stop/clear.

### 4. [CLK-04] MIDI Continue does not reset — PASS
MIDI Continue (0xFB) is correctly handled in `ClockManager::onMidiContinue()` (ClockManager.cpp:44-52) — it sets no flags, does not reset `_currentTick`, and includes an explicit comment confirming the intent. No `consumeContinueReceived()` method exists, so arps continue from their current position.

### 5. [POT-04] CC slot uses binding index — PASS
MIDI CC slot lookup correctly uses the binding index (`_ccBindingIdx[s] == idx`) rather than the CC number for slot identification, as confirmed in `rebuildBindings()` (PotRouter.cpp:210) and the value update path (PotRouter.cpp:480), preventing cross-context collision.

**Score: 5 PASS | 0 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-21 22:50 (round 5/5)

### 1. [NOTE-05] Aftertouch uses stored note — PASS
Poly-aftertouch correctly uses `_lastResolvedNote[padIndex]` (the note stored at noteOn time) rather than re-resolving. At `MidiEngine.cpp:109`, `updateAftertouch` reads the stored note and enqueues it in the ring buffer; the flushed event sends this stored value via `sendPolyAftertouch`.

### 2. [ARP-02] Sequence encoding — PASS
The `_sequence[]` array encodes pad-order positions as `pos + octave * 48` (ArpEngine.cpp:238) and decodes correctly at tick time using `pos = encoded % 48` and `octOffset = encoded / 48` (lines 342-343), with the maximum encoded value of 191 safely fitting in a `uint8_t` and an explicit bounds guard rejecting values above 191.

### 3. [ARP-03] Pattern Up correctness — PASS
The Up pattern correctly iterates through sorted ascending positions (`_positions[]`) at each octave level (0 through octaveRange-1), producing ascending notes within each octave before moving to the next higher octave.

### 4. [ARPT-03] Stop is always immediate — PASS
Both arp stop paths (play/stop pad via `ArpEngine::playStop()` and MIDI Stop 0xFC via the `consumeStopReceived()` handler in main.cpp) execute immediately without any quantize boundary check — quantize logic is exclusively applied to the start path.

### 5. [ARPT-05] Shuffle step counter resets — PASS
All three shuffle step counter reset conditions are correctly implemented: (1) play/stop toggle resets at ArpEngine.cpp:200, (2) pile 0-to-1 note transition resets at line 126, and (3) pattern change resets at line 72. A guard at line 69 prevents spurious resets when the same pattern value is pushed repeatedly.

**Score: 5 PASS | 0 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-21 23:30 (round 1/4)

### 1. [ARPE-03] Refcount noteOff 1-to-0 only — PASS
The `refCountNoteOff` method at `ArpEngine.cpp:535-542` correctly decrements the refcount first, then only sends MIDI noteOff when the count reaches exactly 0 (the 1-to-0 transition). The only other noteOff send is the emergency `flushPendingNoteOffs` sweep, which is a deliberate force-silence path.

### 2. [ARPE-05] flushPendingNoteOffs completeness — PASS
`flushPendingNoteOffs` correctly implements a two-phase flush — Phase 1 deactivates all 36 event queue slots, and Phase 2 sweeps all 128 `_noteRefCount` entries, sending MIDI noteOff and zeroing the refcount for every entry with count > 0. Zero stuck notes guaranteed.

### 3. [CLK-02] Clock source priority — PASS
Clock source priority USB > BLE > last known > internal is correctly implemented via source-aware conditional assignment on tick reception (ClockManager.cpp:83-87) and a 2-second timeout fallback chain that prefers last-known BPM over internal pot tempo (lines 107-129).

### 4. [LED-03] Tick flash consume-once — PASS
`consumeTickFlash()` correctly implements a consume-once boolean flag (set to true only when a step fires, consumed and cleared at line 558), and `LedController` holds the flash for `LED_TICK_FLASH_DURATION_MS` (30ms) using overflow-safe timestamp comparison.

### 5. [LED-04] Confirmation preemption — PASS
The `triggerConfirm` method unconditionally overwrites all confirmation state (`_confirmType`, `_confirmStart`, `_confirmParam`) with no queuing or priority logic, exactly matching the spec requirement that new confirmation preempts active one.

**Score: 5 PASS | 0 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-21 23:30 (round 2/4)

### 1. [NVS-03] Setup tool direct writes — PASS
All six setup tools use direct `Preferences` writes to NVS, and this is safe because `SetupManager::run()` is called at `main.cpp:251` (inside `setup()`) while `NvsManager::begin()` — which creates the NVS task — is not called until `main.cpp:411`, well after setup mode has exited.

### 2. [POT-02] Catch reset on bank switch — WARNING
Per-bank parameters correctly get `caught=false` on bank switch, but `storedValue` is not re-seeded to the new bank's parameter values, so catch comparison and bargraph display use the previous bank's stale value.

**Spec says**: "Catch resets on bank switch for per-bank params."

**Code does**:

In `main.cpp:464-485`, bank switch triggers:
1. `loadStoredPerBank(...)` (line 481) — correctly updates output values to the new bank's values
2. `resetPerBankCatch()` (line 485) — sets `caught = false` for per-bank bindings

However, `resetPerBankCatch()` at `PotRouter.cpp:531-537` only sets `caught = false` and does NOT update `storedValue`:

```cpp
void PotRouter::resetPerBankCatch() {
  for (uint8_t i = 0; i < _numBindings; i++) {
    if (isPerBankTarget(_bindings[i].target)) {
      _catch[i].caught = false;
    }
  }
}
```

The catch comparison at lines 407-416 uses `cs.storedValue` as the target the physical pot must cross. Since `storedValue` is not updated, it still reflects the PREVIOUS bank's parameter.

**Scenario**:

Musician is on Bank 1 (ARPEG) with gate length at 90%, switches to Bank 2 (ARPEG) which has gate length at 10%. The pot is physically near 90%. The pot "catches" almost immediately (because the pot is near the old 90% value, not the new 10% value), and the gate length jumps from 10% to 90% — exactly the parameter jump that catch is designed to prevent. The bargraph also shows the old bank's target position.

**Suggested fix**:

Re-seed `storedValue` from current output values inside `resetPerBankCatch`, or extract the norm-from-target computation from `seedCatchValues()` into a shared helper:

```cpp
void PotRouter::resetPerBankCatch() {
  for (uint8_t i = 0; i < _numBindings; i++) {
    if (isPerBankTarget(_bindings[i].target)) {
      _catch[i].storedValue = outputToAdc(_bindings[i].target);
      _catch[i].caught = false;
    }
  }
}
```

### 3. [POT-05] PB max one per context — PASS
The PB-max-one-per-context constraint is correctly enforced via auto-steal in `ToolPotMapping::run()` (ToolPotMapping.cpp:419-425), where assigning `TARGET_MIDI_PITCHBEND` locates any existing PB slot in the current context and orphans it to `TARGET_EMPTY`.

### 4. [BTN-03] Bank and scale share left button — PASS
Pad roles for bank (8), scale (15), and arp (6) are enforced to be distinct at configuration time. Tool 3 uses a `hitCount[]` array to detect collisions, marks them as `ROLE_COLLISION`, and `saveAll()` refuses to persist if collisions exist.

### 5. [BOOT-01] Progressive LED fill — PASS
Boot steps 1-8 each call `showBootProgress(N)` in `setup()`, and `LedController::update()` renders a progressive fill by lighting LEDs 0 through N-1, exactly matching the spec's "1 LED per step" pattern.

**Score: 4 PASS | 1 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-21 23:30 (round 3/4)

### 1. [BOOT-03] Setup mode detection window — PASS
Rear button held 3s during boot correctly triggers setup mode with chase pattern, and the detection window is properly located between boot step 3 (keyboard OK) and step 4 (MIDI Transport), matching the spec exactly.

### 2. [CONV-01] No runtime allocation — WARNING
One `std::vector` heap allocation occurs at runtime in a BLE connection callback, technically violating the "no new/delete at runtime" convention, though risk is low since it fires only on BLE connect events (not in the audio/MIDI loop).

**Spec says**: "No `new`/`delete` at runtime — static instantiation only"

**Code does**: At `MidiTransport.cpp:43`, the `onBleConnected()` callback creates a temporary `std::vector<uint16_t>` via `pServer->getPeerDevices()`:

```cpp
std::vector<uint16_t> peers = pServer->getPeerDevices();
```

This NimBLE API returns a vector by value, causing a heap allocation and deallocation each time a BLE device connects.

**Scenario**: The callback fires outside the critical MIDI loop path (only on BLE connection events). The ESP32's heap allocator could fragment over many connect/disconnect cycles. In practice, the vector is tiny (typically 1 element), so the risk of heap fragmentation affecting musical performance is very low. However, it is a technical violation.

**Suggested fix**: Document as a known exception with a comment, or cache the connection handle from the NimBLE callback parameter if the library supports it:

```cpp
// NOTE: getPeerDevices() returns std::vector (heap alloc). Accepted exception
// to the no-alloc rule — fires only on BLE connect, not in MIDI hot path.
std::vector<uint16_t> peers = pServer->getPeerDevices();
```

Everything else is clean — no `new`, `delete`, `malloc`, `free`, `String` objects, or smart pointers anywhere in `src/`.

### 3. [CONV-03] Naming conventions — WARNING
Minor naming convention deviations exist in 3 locations, but the most significant one (CapacitiveKeyboard) is protected by the DO NOT MODIFY directive.

**Spec says**: "s_ static globals, _ members, SCREAMING_SNAKE_CASE constants"

**Code does**:

1. `MidiTransport.cpp:17` — `static USBMIDI usbMidi(...)` should be `s_usbMidi`.
2. `CapacitiveKeyboard.h:52-71` — 15 private members lack `_` prefix. File is marked DO NOT MODIFY.
3. `ToolCalibration.cpp:12-13` — `sensitivityTargets[]` and `sensitivityNames[]` use camelCase instead of SCREAMING_SNAKE_CASE.

**Scenario**: Purely cosmetic. No runtime impact. Could cause confusion when searching for `s_` static globals during refactors.

**Suggested fix**:

```cpp
// MidiTransport.cpp:17
static USBMIDI s_usbMidi(DEVICE_NAME_USB);

// ToolCalibration.cpp:12-13
static const uint16_t SENSITIVITY_TARGETS[] = { 520, 620, 720 };
static const char* SENSITIVITY_NAMES[] = { ... };
```

### 4. [MIDI-03] Aftertouch throttle — PASS
Poly-aftertouch is correctly rate-limited per pad via `_aftertouchIntervalMs` (default 25ms) at `MidiEngine.cpp:119`, with an additional change-threshold gate and ring-buffer queuing with silent overflow drop, effectively preventing MIDI flooding.

### 5. [MIDI-05] Flush ordering — PASS
`MidiEngine.flush()` at line 664 of `main.cpp` is correctly positioned as the last MIDI operation in the critical path (step 11), after all note and arp processing. Post-flush MIDI sends (pitch bend, CC) are intentionally in the secondary section (step 12b).

**Score: 3 PASS | 2 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-21 23:30 (round 4/4)

### 1. [VT100-03] Refresh rate throttling — PASS
All setup tools implement appropriate refresh throttling — tools with continuous sensor data (ToolCalibration, ToolPadOrdering, ToolPadRoles) use time-based throttling at 200-500ms intervals, while input-driven tools use `screenDirty` flag patterns that inherently prevent flooding. SetupManager and ToolPotMapping combine both approaches.

### 2. [VT100-04] Button-as-ENTER input — PASS
The rear button is correctly mapped as ENTER/confirm input in setup mode. `SetupUI::readInput()` (SetupUI.cpp:257-265) implements rising-edge detection on `BTN_REAR_PIN`, returning `'\r'` on press, which all setup tools handle alongside serial `'\r'`/`'\n'` input.

### 3. [ARCH-03] Simultaneous USB and BLE MIDI consistency — PASS
All MIDI output is routed exclusively through `MidiTransport`, which unconditionally attempts both USB (`tud_mounted()` guard) and BLE (`BLEMidiServer.isConnected()` guard) with independent connection checks — neither transport's disconnection affects the other, and identical musical data is sent to both.

### 4. [ARCH-04] BankManager vs ArpEngine separation — PASS
BankManager and ArpEngine have clean separation of concerns — BankManager has zero ArpEngine references, and ArpEngine has zero BankManager references. All arp-bank coordination is mediated through `main.cpp` accessing `BankSlot.arpEngine`.

**Score: 4 PASS | 0 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-23 (RGB tag audit, round 1/3)

### 1. [RGB-01] Per-pixel brightness scaling — PASS
Both setPixel() and setPixelScaled() multiply each RGB channel by _brightness/255 manually. strip.setBrightness() is never called anywhere in src/.

### 2. [RGB-02] Single strip.show() per frame — PASS
Every execution path through update() calls _strip.show() exactly once — each early-return path has a single show before the return, and all fall-through paths converge on the single show at line 638.

### 3. [RGB-03] ConfirmType enum caller consistency — PASS
All callers use the new split types (CONFIRM_SCALE_ROOT/MODE/CHROM, CONFIRM_HOLD_ON/OFF, CONFIRM_PLAY/STOP). No references to old CONFIRM_SCALE or CONFIRM_HOLD exist anywhere under src/.

### 4. [RGB-04] Bargraph catch visualization — PASS
Not-caught mode renders dim bar (COL_WHITE_DIM/COL_BLUE_DIM) spanning realLevel LEDs plus a bright cursor at potLevel index. Caught mode renders full-brightness bar using realLevel (0-8 LED count), not potLevel (0-7 index).

### 5. [RGB-05] Bank switch blinks destination only — PASS
CONFIRM_BANK_SWITCH renders full normal display for all 8 LEDs, then overlays triple blink only on _currentBank LED without clearing or modifying other LEDs. Note: CLAUDE.md still says "ALL 8" — spec needs updating to match intentional design.

**Score: 5 PASS | 0 WARNING | 0 FAIL | 0 ERROR**

---
## Run — 2026-03-23 (RGB tag audit, round 2/3)

### 6. [RGB-06] Octave group mapping — PASS
Octave confirmation maps param 1-4 to LED pairs 0-1, 2-3, 4-5, 6-7 using formula baseLed=(param-1)*2, with clearPixels() ensuring all other LEDs are off.

### 7. [RGB-07] Play confirmation two-phase — PASS
Phase 0 fires immediate green ack via COL_PLAY_ACK, then phases 1-3 fire beat-synced flashes at intensities 128/191/255 (50%/75%/100%) of COL_ARP_PLAY, using ClockManager::getCurrentTick() with 24-tick beat boundaries.

### 8. [RGB-08] Fade-out confirmations — PASS
Both CONFIRM_HOLD_OFF and CONFIRM_STOP implement linear decay over LED_CONFIRM_FADE_MS (300ms) using setPixelScaled() with fadeScale decreasing from 255 to 0.

### 9. [RGB-09] Error uses LEDs 3-4 only — FAIL
CLAUDE.md spec says "all 8 blink 500ms" but code only blinks LEDs at indices 3 and 4. This is an intentional design change from brainstorming (user requested "just the two central LEDs") but CLAUDE.md was not updated.

**Spec says**: `"3. Error (all 8 blink 500ms — sensing task stall)"`

**Code does**: LedController.cpp:249-258 clears all pixels and only sets pixels 3 and 4 to COL_ERROR on blink state.

**Scenario**: Not a functional bug — the 2-LED error display is intentional. However the spec/code mismatch could confuse future audits.

**Suggested fix**: Update CLAUDE.md priority state machine to: `"4. Error (LEDs 4-5 blink red 500ms — sensing task stall)"`

### 10. [RGB-10] Setup comet ping-pong bounds — PASS
_cometPos cycles 0-13 correctly mapping to LED indices 0-7 forward then 6-1 backward. All trail computations produce indices within [0,7] with duplicate guards preventing overwritten pixels at endpoints.

**Score: 4 PASS | 0 WARNING | 1 FAIL | 0 ERROR**

---
## Run — 2026-03-23 (RGB tag audit, round 3/3)

### 11. [RGB-11] Battery gauge gradient no pulse — FAIL
CLAUDE.md says "8-LED bar with heartbeat pulse" but implementation is a solid gradient bar with no pulse. This is an intentional design change from brainstorming (user chose "simplifier" — solid bar only) but CLAUDE.md was not updated.

**Spec says**: `"4. Battery gauge (8-LED bar with heartbeat pulse, 3s)"`

**Code does**: LedController.cpp:260-271 displays a static solid bar using COL_BATTERY[i] per LED, no brightness modulation.

**Scenario**: Not a functional bug — the solid bar is intentional. Spec/code mismatch.

**Suggested fix**: Update CLAUDE.md to: `"5. Battery gauge (8-LED solid gradient bar, 3s)"`

### 12. [RGB-12] ScaleManager consumeScaleChange correctness — PASS
consumeScaleChange() returns the correct ScaleChangeType matching which pad was pressed (ROOT/MODE/CHROMATIC) and atomically clears to SCALE_CHANGE_NONE after consumption.

### 13. [RGB-13] PotRouter bargraph state consistency — PASS
Both caught and not-caught paths in applyBinding() correctly set _bargraphPotLevel, _bargraphCaught, and _bargraphDirty. Only TARGET_EMPTY/NONE skip bargraph (intentional).

### 14. [RGB-14] Context color resolution — PASS
LedController dynamically reads _slots[_currentBank].type to resolve context color (white for NORMAL, blue for ARPEG) in both bargraph and bank switch confirmation, with no hardcoded assumptions.

**Score: 3 PASS | 0 WARNING | 1 FAIL | 0 ERROR**

---
