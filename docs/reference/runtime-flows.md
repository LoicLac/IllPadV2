# Runtime Data Flows — ILLPAD V2

Five critical data flows that describe how the runtime moves state between
subsystems. Read the relevant flow when debugging a specific causal chain or
before adding a new consumer.

**Source of truth** :
- `src/main.cpp` (sensingTask, loop, dispatch helpers)
- `src/core/CapacitiveKeyboard.cpp` (pressure pipeline — DO NOT MODIFY)
- `src/core/MidiTransport.cpp` / `src/midi/MidiEngine.cpp`
- `src/managers/BankManager.cpp` / `ScaleManager.cpp` / `PotRouter.cpp`
- `src/arp/ArpEngine.cpp` / `ArpScheduler.cpp`
- `src/midi/ClockManager.cpp`
- `src/core/PotFilter.cpp` (MCP3208 SPI path)

Line numbers cited below correspond to the state at the time of writing.
When editing one of these flows, update this ref in the same commit
(keep-in-sync protocol — see
[`architecture-briefing.md`](architecture-briefing.md) §0).

---

## Loop execution order (Core 1)

Critical path first, secondary after. MIDI latency depends on this order.

```
 0. pollRuntimeCommands()                      ← DEBUG_SERIAL only — viewer-API
    (?STATE / ?BANKS / ?BOTH ; non-blocking Serial.available poll)
 1. Read double buffer (instant)              ← CRITICAL PATH START
 1b. Boot settle window (BOOT_SETTLE_MS=300ms) ← absorb pad state silently
 2. USB MIDI transport update (clock polling)
 3. Read buttons (left + rear)                 ← 20ms SW debounce on top of HW RC
── handleManagerUpdates(state, leftHeld) ──
 4. BankManager.update()                       ← left button
 5. ScaleManager.update()                      ← left button (same as bank)
 5b. Consume scale/octave/hold flags + LED confirmations
     (ARPEG scale change: no flush needed — events store resolved MIDI notes)
 6. ClockManager.update()                      ← PLL + tick generation
── handleHoldPad(state, leftHeld) ──
 7. Hold pad — toggles Play/Stop via ArpEngine::setCaptured()
    (FG only if !leftHeld ; scope étendu toutes banks ARPEG/GEN si leftHeld)
── ControlPadManager.update(state, leftHeld, channel) ──
 7b. Control pads — edge detection + per-mode CC emission
     (gate-vs-setter handoff on LEFT/bank edges ; CONTINUOUS DSP pipeline)
── handlePadInput(state, now) ──
 8. processNormalMode() or processArpMode()
 8b. Stuck-note cleanup on left-release edge
 8c. Edge state sync (s_lastKeys = keyIsPressed) ← before arp tick
 9. ArpScheduler.tick()                       ← all background arps
 9b. ArpScheduler.processEvents()             ← gate noteOff + shuffle noteOn
10. MidiEngine.flush()                         ← CRITICAL PATH END
── handlePotPipeline(leftHeld, rearHeld) ──
11. PotRouter.update()                         ← SECONDARY
    (PotFilter::updateAll via MCP3208 SPI + 5 pots)
11b. Send MIDI CC/PB if dirty                  ← from user-assigned pot mappings
12. BatteryMonitor.update()
13. LedController.update()                     ← multi-bank state + overlays
── handlePanicChecks(now, rearHeld) ──
14. BLE reconnect detection + manual triple-click rear (600ms window)
── NVS pot debounce + dirty signal ──
15. NvsManager.notifyIfDirty()                 ← non-blocking signal to NVS task
── debugOutput(leftHeld, rearHeld) ──
16. vTaskDelay(1)                              ← caps loop ~1 kHz
```

---

## 1. Pad touch → MIDI NoteOn

```
Core 0: MPR121 I2C poll → pressure pipeline → s_buffers[writeIdx]
        → s_active.store(writeIdx, release)                    [INTER-CORE]
Core 1: state = s_buffers[s_active.load(acquire)]
        → MIDI block runs ONLY if !bankManager.isHolding() && !scaleManager.isHolding()
        → edge detect (pressed && !wasPressed)
        → skip if s_controlPadManager.isControlPad(i) (control pads emit CC, not notes)
          (skip also in pile rebuild for ARPEG)
        → velocity = baseVelocity ± random(variation)
        → MidiEngine::noteOn(padIndex, vel, padOrder, scale)
          → ScaleResolver::resolve() → MIDI note
          → _lastResolvedNote[padIndex] = note                 [STORED FOR NOTEOFF]
          → MidiTransport::sendNoteOn (USB + BLE simultaneously)

Left-button release safety (detect s_wasHolding → !holding edge):
  NORMAL:      for all pads not pressed: MidiEngine::noteOff(i)        [idempotent]
  ARPEG HOLD OFF: for all pads not pressed (skip holdPad):
                  ArpEngine::removePadPosition(s_padOrder[i])          [idempotent]
```

Entry points :
- `main.cpp::handlePadInput()` [861-880] dispatches to
  `processNormalMode` [753-787] and `processArpMode` [789-837]
- `main.cpp::handleLeftReleaseCleanup()` [839-859]
- `MidiEngine::noteOn` [47] / `noteOff` [63]
- `ScaleResolver::resolve`
- `MidiTransport::sendNoteOn` [196-214]

Invariant : `_lastResolvedNote[padIndex]` stored at noteOn, reused at
noteOff — **never re-resolved**. See `CLAUDE.md` invariant #1 and
[`arp-reference.md`](arp-reference.md).

---

## 2. Arp tick → MIDI NoteOn

```
ClockManager::update() → _currentTick++
→ ArpScheduler::tick()
  → tickAccum += ticksElapsed
  → while (tickAccum >= divisor):
      synthTick = currentTick - tickAccum
      ArpEngine::tick(synthTick, globalTick=currentTick)
        → switch (currentState()):             [state dispatch]
            IDLE: flush + stop, return              (pile empty)
            WAITING_QUANTIZE: check boundary, return or proceed
            PLAYING: Play-mode captured or Stop-mode auto-play
        → executeStep():                       [note scheduling]
            → resolve position → MIDI note via ScaleResolver
            → schedule noteOff FIRST (atomic pair)
            → if shuffle: schedule noteOn (delayed)
              else: refCountNoteOn() (immediate send)
→ ArpScheduler::processEvents()
  → for each pending event where time arrived:
      refCountNoteOn() or refCountNoteOff()
      → MIDI send only on refcount transitions (0→1, 1→0)
```

Entry points :
- `ClockManager::generateTicks` [181-203]
- `ArpScheduler::tick` [98-131] + `processEvents` [140-146]
- `ArpEngine::currentState` [679-685]
- `ArpEngine::tick` [691-721]
- `ArpEngine::executeStep` [727-786]
- `ArpEngine::processEvents` [843-856]

Details (states, shuffle, quantize, Play/Stop) :
[`arp-reference.md`](arp-reference.md).

---

## 3. Bank switch (all side effects, in order)

```
switchToBank() calls [BankManager.cpp:188-231]:
1. sendPitchBend(8192) on OLD channel         [BankManager.cpp:195]
2. allNotesOff() on OLD channel               [BankManager.cpp:196]
   — internally drains AT ring buffer + resets AT rate limiter
     [MidiEngine.cpp:98-101]
3. Update foreground flags                    [BankManager.cpp:199-201]
4. setChannel(newBank)                        [BankManager.cpp:204]
5. sendPitchBend(newBank.pitchBendOffset)     [BankManager.cpp:205-206]
   — NORMAL banks only. ARPEG banks: no pitch bend sent (no aftertouch, spec)
6. LED: setCurrentBank + triggerEvent(EVT_BANK_SWITCH)  [BankManager.cpp:210-211]
7. Bank-select MIDI on canal 16 (DAW resync)  [BankManager.cpp:214-216]
   — sendBankSelectMidi(oldBank, false) + sendBankSelectMidi(newBank, true)
— back in handleManagerUpdates() [main.cpp:907] —
8. queueBankWrite() to NVS                    [main.cpp:914]
9. reloadPerBankParams(newSlot)               [main.cpp:915]
   → loadStoredPerBank() into PotRouter       [reloadPerBankParams:898-901]
10. seedCatchValues(keepGlobalCatch=true)      [reloadPerBankParams:902]
   — reseeds stored values; global targets keep catch state (tempo, shape…)
   — per-bank targets lose catch (will be uncaught by step 11)
11. resetPerBankCatch() — uncatch per-bank only [reloadPerBankParams:903]
12. ControlPadManager.update() detects bank switch edge on next frame,
    runs per-mode handoff (gate-family : CC=0 on old channel + re-sync
    on new channel ; setter : silent preserve of old-channel value).
```

### LEFT + bank pad : deferral and double-tap Play/Stop toggle

`BankManager::update()` uses rising-edge detection on bank pads while LEFT is
held, plus a per-pad timestamp (`_lastBankPadPressTime[b]`) for double-tap
tracking. Window = `_doubleTapMs` (100–250 ms, settings default 200 ms,
same value as Play-mode note double-tap).

```
Rising edge on bank pad b (while LEFT held):
  if 2nd tap within window AND _banks[b].type == BANK_ARPEG:
    // Same event chain as Hold pad. Only difference: BG target → keys=nullptr.
    ArpEngine::setCaptured(!wasCaptured, transport,
                           keys = (b == _currentBank) ? keyIsPressed : nullptr,
                           _holdPad):
      Stop → Play (captured=true):
        _pausedPile && pile>0 → relaunch from step 0, waitForQuantize if needed
        else                  → just flip _captured, _pausedPile=false
      Play → Stop (captured=false):
        anyFingerDown (excl. holdPad) → clearAllNotes() (live mode takes over)
        no fingers (or BG: keys==nullptr)
                                       → flushPendingNoteOffs, _playing=false,
                                         _waitingForQuantize=false, _pausedPile=true
    Toggle always fires — LED always updates.
    Always: consume press (timestamp=0), cancel pending switch, continue.
    LED: triggerEvent(EVT_STOP|EVT_PLAY, mask=1<<b) — FADE overlay on target
         pad LED (may be BG bank). Double-tap NEVER switches bank.

  _pausedPile semantics (persistent):
    _pausedPile=true means "Stop with pile kept — next Play relaunches from step 0".
    In Stop mode, first pad press wipes the paused pile before entering live mode
    (processArpMode in main.cpp).
    clearAllNotes(), setCaptured(true), and addPadPosition() all reset _pausedPile=false.

  2nd tap on NORMAL bank (wasRecent but no play/stop semantics):
    ignore — preserve pending switch and 1st-tap timestamp so the natural
    timeout still commits at T0 + _doubleTapMs. Re-arming would postpone the
    switch on every repeat tap.

  Else (1st tap):
    _lastBankPadPressTime[b] = now
    if b == _currentBank: continue
    arm pending switch: _pendingSwitchBank = b, _pendingSwitchTime = now
    (switch deferred ~doubleTapMs for ALL bank types — uniform tactile feel ;
     ARPEG also uses this window to detect the 2nd tap for Play/Stop toggle)
```

Pending switch resolution :
- **Natural timeout** (`now - _pendingSwitchTime >= _doubleTapMs`) : commit
  `switchToBank(target)`.
- **LEFT release while pending** : fast-forward — commit `switchToBank(target)`
  on the release edge.

`_switchedDuringHold` flag : set when a switch occurs while LEFT is held.
On LEFT release, `s_lastKeys` is snapshotted from current `keyIsPressed`
to prevent phantom noteOn/noteOff when resuming play on the new bank.

Entry points :
- `BankManager::update / switchToBank` (`_holdPad` set at boot via
  `setHoldPad()`)
- `ArpEngine::setCaptured` (called by both Hold pad in
  `main.cpp::handleHoldPad` and BankManager double-tap path)
- `LedController` handles `EVT_PLAY` / `EVT_STOP` overlays honoring
  `_confirmLedMask` → fades the target bank's LED, not just foreground.

---

## 4. Scale change

```
ScaleManager::processScalePads() detects root/mode/chromatic pad press
  → NORMAL: allNotesOff() immediately (prevents orphan notes)
  → ARPEG: NO flush — pending events carry resolved notes, next tick re-resolves
  → Set flag: _scaleChangeType = ROOT|MODE|CHROMATIC
handleManagerUpdates() consumes flag:
  → NVS queue + ArpEngine::setScaleConfig() + LED confirm
  → Group propagation: if currentBank.scaleGroup > 0, iterate all other banks
    in the same group: copy scSlot.scale, queueScaleWrite(i, scale),
    setScaleConfig() if ARPEG. No allNotesOff on propagated banks
    (NORMAL bg = no active notes, ARPEG re-resolves on next tick).
  → LED confirmation: triggerEvent(EVT_SCALE_*, ledMask) with bitmask of
    all group members — all group LEDs blink together (not just foreground).
```

Entry points :
- `ScaleManager::processScalePads` [114-212]
- `main.cpp::handleManagerUpdates` [907-970]
- `reloadPerBankParams` [883-904]

Scale groups are stored in `BankTypeStore.scaleGroup[]`
(0 = none, 1..NUM_SCALE_GROUPS = A..D), accessed via
`NvsManager::getLoadedScaleGroup()`. Leader-wins propagation at boot in
`NvsManager::loadAll()` ensures consistency across NVS per-bank scale blobs.

---

## 5. Pot → parameter

```
PotFilter::updateAll():
  MCP3208 SPI read per channel → deadband gate → edge snap → sleep/wake
  State machine per pot: ACTIVE → SETTLING → SLEEPING (peek every 50ms)
  Output: getStable() (0-4095 post-deadband), hasMoved() (bool)
PotRouter::update():
  PotFilter::updateAll()
  resolveBindings(): button state + bank type → find best binding
  for each pot with hasMoved(): applyBinding()
applyBinding():
  adc = PotFilter::getStable(potIndex)
  if TARGET_LED_BRIGHTNESS: bypass catch, apply immediately
  if !caught: compare adc vs storedValue, show uncaught bargraph, WAIT
  if caught: convert ADC → parameter range, write output, show bargraph
  → Global targets: propagate storedValue across contexts
handlePotPipeline(): read getters → write to BankSlot/ArpEngine/atomics
  → consumeCC → MidiTransport sends
```

Entry points :
- `PotFilter::updateAll` (MCP3208 SPI, no EMA, no oversampling — see
  [`pot-reference.md`](pot-reference.md))
- `PotRouter::update` [370-379] + `resolveBindings` [386-436] +
  `applyBinding` [441-624]
- `main.cpp::handlePotPipeline` [1058-1128] + `pushParamsToEngine` [1040-1055]

Bargraph : `PotRouter::hasBargraphUpdate()` always triggers
`LedController::showPotBargraph()` (tempo included — no pulsed variant).

Details : [`pot-reference.md`](pot-reference.md).

---

## Inter-core communication

All lock-free. No mutex anywhere in runtime code.

| What | Type | Writer | Reader | Order |
|------|------|--------|--------|-------|
| Double buffer index | `atomic<uint8_t>` | Core 0 | Core 1 | release/acquire |
| Response shape | `atomic<float>` | Core 1 | Core 0 | relaxed |
| Slew rate | `atomic<uint16_t>` | Core 1 | Core 0 | relaxed |
| Pad sensitivity | `atomic<uint8_t>` | Core 1 | Core 0 | relaxed |
| USB tick count | `atomic<uint8_t>` `_pendingUsbTicks` | USB callback | Core 1 | release/acquire |
| BLE tick count | `atomic<uint8_t>` `_pendingBleTicks` | NimBLE task | Core 1 | release/acquire |
| Last USB tick time | `atomic<uint32_t>` `_lastUsbTickUs` | USB callback | Core 1 | release/acquire (hot), relaxed (timeout) |
| Last BLE tick time | `atomic<uint32_t>` `_lastBleTickUs` | NimBLE task | Core 1 | release/acquire (hot), relaxed (timeout) |

---

## Dirty flags and event queues

### Dirty flags (set → consumed pattern)

| Flag | Set by | Consumed by | Purpose |
|------|--------|-------------|---------|
| `_sequenceDirty` | addPad, removePad, setPattern, setOctaveRange | `rebuildSequence()` at next tick | Arp sequence rebuild |
| `_tickFlash` | `ArpEngine::tick()` | `LedController::update()` via `consumeTickFlash()` | LED beat flash |
| `hasMoved(p)` | `PotFilter::updateAll()` deadband | `applyBinding()` per frame | Pot movement gate |
| `_ccDirty[s]` | `applyBinding()` CC value change | `consumeCC()` in main loop | MIDI CC send |
| `_dirty` (pot) | Any parameter write | `clearDirty()` after NVS debounce | NVS save trigger |
| `_scaleChangeType` | `processScalePads()` | `consumeScaleChange()` (auto-clear) | Scale change flag |
| `_octaveChanged` | Octave pad press | `hasOctaveChanged()` (auto-clear) | Octave change flag |
| `_holdToggled` | Hold pad press | `hasHoldToggled()` (auto-clear) | Hold toggle flag |

### Event queues

| Queue | Size | Producer | Consumer | Overflow behavior |
|-------|------|----------|----------|-------------------|
| Aftertouch ring | 64 entries | `updateAftertouch()` | `flush()` (16/frame max) | Silent drop. **Capacity math**: at default 25 ms rate, N pads generate N events/25 ms. flush drains 16/frame × ~40 frames/25 ms = 640 drain capacity. Safe up to ~48 pads. At minimum rate (10 ms): N events/10 ms vs 16×10=160 drain → safe up to ~40 pads. Real-world : ≤10 fingers, never saturates. |
| Arp events | 64 per engine | `tick()` (noteOn/Off pairs) | `processEvents()` every frame | noteOff fail → skip entire step (safe); noteOn fail → cancel orphaned noteOff (safe) |
| LOOP main buffer | 1024 events × 8 B per engine | `LoopEngine::capturePadEvent` (RECORDING **et** OVERDUBBING post OD-Sync immediate-merge) live-sort O(log n + n) | `LoopEngine::update` walks events sorted, fire via refcount | `insertEventSorted` retourne false → silent drop + `viewer::emitLoopBufferFull(ch, "main")` (m9 telemetry) |
| LOOP alternate buffer (OD-Sync) | 1024 events × 8 B per engine | `tapRec` snapshot à l'entrée OD (memcpy `_events` → `_eventsAlternate`) | `swapForUndoRedo` (Cancel mid-OD ou Undo/Redo toggle PLAYING/STOPPED) via diff swap par note | Reset à wipe (longPressClear) → `_alternateValid = false`. Cf [`Illpad_OD_Sync.md`](../superpowers/specs/Illpad_OD_Sync.md) §2 + §6.2. |
| LOOP swap temp global | 1024 events × 8 B (shared) | `g_swapTemp` static global dans LoopEngine.cpp | Utilisé temporairement par `swapForUndoRedo` pour 3-way memcpy swap | Invariant 11 (≤ 1 LOOP en REC/OD) garantit single-usage. Coût SRAM permanent 8 KB. |
| LOOP pending noteOffs | 64 per engine | `scheduleNoteOff` (gates ou stuck flush) | `drainPendingNoteOffs` chaque frame | Silent drop if queue full — fallback via `flushPendingNoteOffs` |
| NVS writes | per-field dirty flags | Main loop | Background FreeRTOS task | Coalesced (latest wins) |

---

## LOOP runtime flow (Phase 2)

Phase 2 LOOP (commits `6c0b4d8` → `284bec4`, 2026-05-19) ajoute un sous-système runtime parallèle à ARPEG : classe `LoopEngine` dans `src/loop/`, assignée à toute bank de type `BANK_LOOP` via pool statique `s_loopEngines[MAX_LOOP_BANKS=4]`. Plan archivé : [`docs/archive/2026-05-18-loop-phase-2-plan.md`](../archive/2026-05-18-loop-phase-2-plan.md).

### 1. Pad touch → LOOP MIDI

```
Pad press
  ↓
Core 0 sense → SharedKeyboardState
  ↓
Core 1 handlePadInput → switch(slot.type)
  ↓ case BANK_LOOP
processLoopMode(state, slot, now)
  ↓
  ├─ CLEAR pad held → notifyClearPressStart + isClearHoldFired → longPressClear
  ├─ REC pad rising edge → loopEngine->tapRec → state machine dispatch
  ├─ PLAY/STOP pad rising edge → loopEngine->tapPlayStop → state machine
  │   (FREE = immediate startPlayback/stopPlayback ; BEAT/BAR = WAITING_* + commitWaitingAction)
  └─ Musical pad edge → loopEngine->capturePadEvent
       ↓
       ├─ M8 live monitor (TOUS états) : refCountNoteOn / refCountNoteOff → MidiTransport
       ├─ M3 velocity strict baseVelocity (variation au playback seulement)
       ├─ _padHeldLive[pad] tracker set/reset (consommé par commitRecordingClose / closeRecordingImmediate / commitOverdubExit / onBackgroundTransition)
       └─ Buffer write conditionnel :
            • RECORDING → insertEventSorted live-sort dans _events[] (M2)
            • OVERDUBBING → insertEventSorted live-sort dans _events[] direct (OD-Sync immediate-merge post 2026-05-19 ; _overdubEvents supprimé) + _playNextEventIdx recompute pour éviter double-fire
            • Autres états → live monitor seul, pas de buffer write
```

### 2. LoopEngine::update tick → playback BPM-scaled

`update()` est appelé pour chaque LoopEngine assigné à chaque itération main loop (placement m3 audit : APRÈS `s_arpScheduler.processEvents`, AVANT `s_midiEngine.flush`, dans le critical path) :

```
LoopEngine::update(transport) chaque frame
  ↓
nowUs = micros()
  ↓
(1) WAITING_* boundary check :
    if _clock->getCurrentTick() >= _waitingTargetTick
      commitWaitingAction(transport, nowUs)
        ↓ WAITING_PLAY → startPlayback(transport, nowUs)  [B3 nowUs propagé]
        ↓ WAITING_STOP → stopPlayback(transport, true)
        ↓ set _waitingExit signal (consommé par main → triggerEvent EVT_PLAY/STOP clear overlay)
  ↓
(2) Drain pending noteOffs (gates, stuck-note safety)
  ↓
(3) Si PLAYING/OVERDUBBING/WAITING_STOP :
    B1 audit fix intégration incrémentale :
      delta = nowUs - _lastUpdateUs
      _scaledElapsedUs += delta × liveBpm / recordBpm
      _lastUpdateUs = nowUs
    ↓
    Wrap detection : while _scaledElapsedUs >= _loopDurationUs
      Fire tail events + soustraire _loopDurationUs + _wrapFlash=true + reset _playNextEventIdx
    ↓
    Fire events whose _events[idx].timestampUs <= _playPositionUs
      refCountNoteOn(transport, midiNote, applyVelocityVariation(velocity))  [M3 variation au playback]
      refCountNoteOff(transport, midiNote)
    ↓
    Bar crossing detection : _barFlash = true on bar index change
  ↓
(4) Sinon : garder _lastUpdateUs synchronisé (évite delta géant au prochain PLAYING)
```

### 3. Recording flow — Auto-Stop + master grid anchor

**Pivot 2026-05-19** (commits `89f6c11` + `edbdd2b`) : remplace bar-snap+rescale par **Auto-Stop** boundary-aware + master grid anchor. Cf [`docs/superpowers/specs/Illpad_Master_Sync.md`](../superpowers/specs/Illpad_Master_Sync.md).

```
tap REC depuis EMPTY → tapRec → startRecording
  ↓ state = RECORDING
  ↓ _eventCount = 0, _recordFirstPressDone = false
  ↓
musical pad press → capturePadEvent
  ↓ 1er noteOn : Master Sync anchor selon _quantize :
  │    FREE → _recordStartUs = micros() (tap-to-tap, hors grille)
  │    BEAT → _recordStartUs = _clock->getLastBeatWallTimeUs()
  │    BAR  → _recordStartUs = _clock->getLastBarWallTimeUs()
  ↓ _recordBpm = getSmoothedBPM() latché (invariant §23.5)
  ↓ _recordFirstPressDone = true
  ↓ live monitor MIDI (M8) + insertEventSorted live-sort buffer (M2)
  ↓
tap REC depuis RECORDING → tapRec :
  ├─ Si _quantize == FREE :
  │   ↓ closeRecordingImmediate
  │   ↓ _loopDurationUs = rawDurUs (pas de snap)
  │   ↓ flushHeldPadsAsNoteOffs(_loopDurationUs - 1)
  │   ↓ M6 clamp defense in depth
  │   ↓ flushPendingNoteOffs (B2 self-stop → STOPPED)
  │   ↓ startPlayback(transport, micros()) → state = PLAYING
  │
  └─ Si _quantize == BEAT/BAR :
      ↓ _recordingPendingCloseTick = computeNextBoundaryTick(_quantize)
      ↓ _recordingPendingClose = true   ← flag sur RECORDING (état machine inchangé)
      ↓ état reste RECORDING, capture continue (α decision BS-2)
      ↓
update() phase 0 (à chaque frame) :
  ↓ if (RECORDING && _recordingPendingClose && currentTick >= boundary tick) :
  │   commitRecordingClose
  │     ↓ boundaryWallTime = lastTickWallTimeUs - catchUpDiff × tickIntervalUs (sub-tick precision)
  │     ↓ _loopDurationUs = boundaryWallTime - _recordStartUs
  │     │   = multiple entier de quantize unit (BEAT=24t, BAR=96t) par construction
  │     ↓ flushHeldPadsAsNoteOffs(_loopDurationUs - 1)
  │     ↓ M6 clamp
  │     ↓ flushPendingNoteOffs (B2)
  │     ↓ startPlayback(transport, boundaryWallTime) → state = PLAYING
  │         _lastUpdateUs = boundaryWallTime → loop position 0 ancré au master tick
```

**Garantie cohérence math** : `loopDur = N × beatDur_recordBpm` → `wrapWallTime_liveBpm = N × beatDur_liveBpm` → wrap toujours sur master tick boundary. Loops ARP + LOOP + LOOP multi-bank phase-aligned via cette grille.

**FREE explicitement hors grille** : recordStart non-ancré, loop wraps dérivent vs master clock. Comportement intentionnel pour mode "free run".

**Gestes pendant PENDING_CLOSE** (BS-6 à BS-9) : capture continue, autres ignorés/refusés (PLAY/STOP ignoré, 2e tap REC ignoré, CLEAR refus `isLocked()`, bank switch silent deny).

### 4. Bank switch + multi-bank LOOP

```
LEFT + tap bank pad → BankManager::update
  ↓ M9 audit guard : check current.loopEngine->isLocked() (RECORDING/OVERDUBBING)
  │  → silent deny si locked (spec §23.2, invariant 11)
  ↓ switchToBank(target)
  ↓
main.cpp handleManagerUpdates (post bankSwitched=true) :
  ↓ Audit fix B-N1/R-N1 : if (s_banks[prevBank].type == BANK_LOOP)
  │    s_banks[prevBank].loopEngine->onBackgroundTransition(s_transport)
  │      ↓ Phase 1 : flush live press refcount via _padHeldLive[]
  │      ↓ Phase 2 : notifyClearPressEnd() reset CLEAR tracker
  │      ↓ NE CHANGE PAS _state (BG continue playback)
  ↓
LEFT + double-tap bank LOOP pad → BankManager LOOP branch
  ↓ loopEngine->tapPlayStop(transport, keys) — FG ou BG
  ↓ LED triggerEvent aligned (WAITING_* / EVT_PLAY / EVT_STOP)
```

### 5. Panic + global toggle

- `midiPanic()` Phase 1b : flush all LoopEngines via `flushPendingNoteOffs` (B2 self-stop → STOPPED), avant `MidiEngine::allNotesOff()` + CC 123 multi-channel.
- LEFT + hold pad simple tap → `toggleAllArpsAndLoops()` (rename Phase 2.J) : itère ARPEG + LOOP, toggle play/stop multi-bank, single LED `triggerEvent` avec bitmask.

### 6. Overdub flow — immediate-merge + 1-level Undo/Redo toggle

**Pivot 2026-05-19** (commits `eaf5674` C1 + `fc2ff9b` C2 + `33149b8` C3) : remplace deferred-merge avec buffer temporaire par immediate-merge + snapshot 1-level. Cf [`docs/superpowers/specs/Illpad_OD_Sync.md`](../superpowers/specs/Illpad_OD_Sync.md).

```
Entrée OD :
  tap REC depuis PLAYING/STOPPED/WAITING_STOP → tapRec :
    ↓ memcpy(_eventsAlternate, _events, sizeof(_events))  // snapshot pré-OD
    ↓ _eventsAlternateCount = _eventCount
    ↓ _alternateValid = true
    ↓ state = OVERDUBBING (state machine inchangée vs Phase 2)
    ↓ EVT_LOOP_OVERDUB trigger (LED Amber state-driven)

Capture pendant OD (immediate-merge) :
  pad press musical → capturePadEvent
    ↓ M8 live monitor refCountNoteOn (audible immédiat)
    ↓ _padHeldLive[pad] = true
    ↓ insertEventSorted DIRECT dans _events[] à _playPositionUs (pas de temp buffer)
    ↓ _playNextEventIdx recompute par binary search (évite double-fire ce cycle ;
      le M8 live monitor l'a déjà émis, l'event firera au cycle suivant via le walk)

  pad release → capturePadEvent velocity=0
    ↓ M8 live monitor refCountNoteOff
    ↓ _padHeldLive[pad] = false
    ↓ insertEventSorted noteOff dans _events[] (même mécanique)

Exit commit (tap REC) :
  tapRec depuis OVERDUBBING → commitOverdubExit :
    ↓ flushHeldPadsAsNoteOffs(_playPositionUs)  // B-N2 réincarnée
    │    pour chaque _padHeldLive[pad]==true : insert noteOff @ _playPositionUs
    │    (sans ça : noteOn capturé sans noteOff matching → refcount grows unbounded
    │     si user release après exit OD ; télémétrie viewer::emitLoopBufferFull
    │     "od_exit_flush" si buffer plein au flush)
    ↓ _playNextEventIdx recompute
    ↓ state = PLAYING (_eventsAlternate intact pour post-Undo)
    ↓ LED state-driven Amber → Green

Exit Cancel (tap CLEAR rising edge pendant OD) :
  processLoopMode CLEAR dispatch state-aware → cancelOverdub :
    ↓ swapForUndoRedo (cf §6.2 spec OD-Sync) :
    │    compute states "before" (_events) vs "after" (_eventsAlternate) à _playPositionUs
    │       pour chaque note N (0..127) via isNoteOnAt walk O(count)
    │    MIDI ciblé : noteOff/noteOn seulement si état change ET pas live-press
    │       (préserve couche base + live press par construction ; cf scénario K+SN+HH G3)
    │    swap _events ↔ _eventsAlternate via g_swapTemp static global 3-way memcpy
    │    swap _eventCount ↔ _eventsAlternateCount
    │    recompute _noteRefCount from new _events + _padHeldLive contribution
    │    recompute _playNextEventIdx par binary search
    ↓ state = PLAYING (OD-16 α : toujours PLAYING même depuis STOPPED-Q5)
    ↓ EVT_LOOP_CLEAR trigger (LED Option β cyan, PTN_NONE actuellement)

Undo/Redo post-exit (tap CLEAR court PLAYING/STOPPED) :
  falling edge CLEAR, release < clearLoopTimerMs, !_clearFired, _alternateValid :
    ↓ swapForUndoRedo (même mécanique que Cancel)
    ↓ Détection sens via diff _eventCount avant/après swap :
    │    countAfter < countBefore → couche retirée → EVT_STOP trigger (coral FADE Option β)
    │    countAfter > countBefore → couche réintégrée → EVT_PLAY trigger (green FADE Option β)
    ↓ state inchangé (reste PLAYING ou STOPPED)

Wipe étendu (long-press CLEAR PLAYING/STOPPED) :
  longPressClear :
    ↓ flushPendingNoteOffs (B2 self-stop)
    ↓ _eventCount = 0, _events slots active=false
    ↓ _eventsAlternateCount = 0, _eventsAlternate slots active=false
    ↓ _alternateValid = false (Undo no-op safe après wipe)
    ↓ state = EMPTY
    ↓ EVT_LOOP_CLEAR trigger
```

**Conséquence musicale** : live loop growth audible (chaque press OD rejoue au cycle suivant), Cancel mid-OD préserve K+SN si tu cancel la couche HH, Undo/Redo toggle permet le geste signature "mute HH pour breakdown puis ramener".

**Suppression Phase 2** : `_overdubEvents[128]`, `_overdubCount`, `mergeOverdub`, `abandonOverdub` supprimés. B-N2 logic réincarnée dans `commitOverdubExit`. Live press protection via `_padHeldLive` étendue au swap musical (cf §6.2 spec OD-Sync).
