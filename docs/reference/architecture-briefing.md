# ILLPAD V2 — Architecture Briefing

Quick-start guide for agents and new sessions. Complements CLAUDE.md (the spec) with runtime knowledge: how data flows, what calls what, and where the traps are.

**Keep this file updated** when flows change. If you modify a function in the chain below, update the relevant section.

---

## 1. Five Critical Data Flows

### Pad Touch → MIDI NoteOn
```
Core 0: MPR121 I2C poll → pressure pipeline → s_buffers[writeIdx]
        → s_active.store(writeIdx, release)                    [INTER-CORE]
Core 1: state = s_buffers[s_active.load(acquire)]
        → MIDI block runs ONLY if !bankManager.isHolding() && !scaleManager.isHolding()
        → edge detect (pressed && !wasPressed)
        → velocity = baseVelocity ± random(variation)
        → MidiEngine::noteOn(padIndex, vel, padOrder, scale)
          → ScaleResolver::resolve() → MIDI note
          → _lastResolvedNote[padIndex] = note                 [STORED FOR NOTEOFF]
          → MidiTransport::sendNoteOn (USB + BLE simultaneously)

Left-button release safety (detect s_wasHolding → !holding edge):
  NORMAL:      for all pads not pressed: MidiEngine::noteOff(i)        [idempotent]
  ARPEG HOLD OFF: for all pads not pressed (skip holdPad):
                  ArpEngine::removePadPosition(s_padOrder[i])          [idempotent]
  — prevents stuck notes / stuck pile positions when pads released during hold
```
Key files: `main.cpp:handlePadInput()` (line 569, dispatches to `processNormalMode()` line 464 and `processArpMode()` line 499), `handleLeftReleaseCleanup()` (line 545), `MidiEngine.cpp:47-57`, `ScaleResolver.cpp`, `MidiTransport.cpp:196-214`

### Arp Tick → MIDI NoteOn
```
ClockManager::update() → _currentTick++
→ ArpScheduler::tick()
  → tickAccum += ticksElapsed
  → while (tickAccum >= divisor):
      synthTick = currentTick - tickAccum
      ArpEngine::tick(synthTick, globalTick=currentTick)
        → switch (currentState()):             [state dispatch]
            IDLE: flush + stop, return
            HELD_STOPPED: return
            WAITING_QUANTIZE: check boundary, return or proceed
            PLAYING: auto-play transition if HOLD OFF
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
Key files: `ClockManager.cpp:181-203` (generateTicks), `ArpScheduler.cpp:98-131` (tick), `ArpScheduler.cpp:140-146` (processEvents), `ArpEngine.cpp:259-265` (currentState), `ArpEngine.cpp:273-315` (tick), `ArpEngine.cpp:329-434` (executeStep), `ArpEngine.cpp:447-462` (processEvents)

### Bank Switch (all side effects in order)
```
1. sendPitchBend(8192) on OLD channel         [BankManager.cpp:122]
2. allNotesOff() on OLD channel               [BankManager.cpp:123]
3. Drain aftertouch ring buffer               [MidiEngine.cpp:98-99]
4. Update foreground flags                    [BankManager.cpp:127-129]
5. setChannel(newBank)                        [BankManager.cpp:133]
6. sendPitchBend(newBank.pitchBendOffset)     [BankManager.cpp:134-135]
   — NORMAL banks only. ARPEG banks: no pitch bend sent (no aftertouch, spec)
7. LED: setCurrentBank + triggerConfirm       [BankManager.cpp:139-141]
— back in handleManagerUpdates() [main.cpp:609] —
8. queueBankWrite() to NVS                   [main.cpp:616]
9. reloadPerBankParams(newSlot)               [main.cpp:617]
   → loadStoredPerBank() into PotRouter       [reloadPerBankParams:600-603]
10. seedCatchValues(keepGlobalCatch=true)      [reloadPerBankParams:604]
    — reseeds stored values; global targets keep catch state (tempo, shape…)
    — per-bank targets lose catch (will be uncaught by step 11)
11. resetPerBankCatch() — uncatch per-bank only [reloadPerBankParams:605]
```

### Scale Change
```
ScaleManager::processScalePads() detects root/mode/chromatic pad press
  → NORMAL: allNotesOff() immediately (prevents orphan notes)
  → ARPEG: NO flush — pending events carry resolved notes, next tick re-resolves
  → Set flag: _scaleChangeType = ROOT|MODE|CHROMATIC
handleManagerUpdates() consumes flag:
  → NVS queue + ArpEngine::setScaleConfig() + LED confirm
```
Key files: `ScaleManager.cpp:123-227`, `main.cpp:handleManagerUpdates()` (line 609), `reloadPerBankParams()` (line 587)

### Pot → Parameter
```
PotFilter::updateAll(): oversample 16× → adaptive EMA → deadband gate → edge snap → sleep/wake
  State machine per pot: ACTIVE → SETTLING → SLEEPING (peek every 50ms)
  Output: getStable() (0-4095), hasMoved() (bool)
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
  → consumeCC/consumePitchBend → MidiTransport sends
```
Key files: `PotFilter.cpp` (updateAll), `PotRouter.cpp:318-327` (update), `334-381` (resolveBindings), `386-546` (applyBinding), `main.cpp:handlePotPipeline()` (line 707), `pushParamsToEngine()` (line 694)

---

## 2. Inter-Core Communication

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

## 3. Invariants (things that MUST stay true)

1. **No orphan notes**: noteOff ALWAYS uses `_lastResolvedNote[padIndex]`, never re-resolves. Scale changes cannot produce stuck notes. Left-button release sweeps all pads on the `s_wasHolding → !holding` edge: NORMAL banks call `noteOff(i)` for every unpressed pad; ARPEG HOLD OFF banks call `removePadPosition(s_padOrder[i])` for every unpressed pad except the holdPad.

2. **Arp refcount atomicity**: noteOff is scheduled BEFORE noteOn. If noteOn fails (queue full), noteOff is cancelled. MIDI noteOn sent only on refcount 0→1, noteOff only on 1→0.

3. **No blocking on Core 1**: NVS writes happen in a background FreeRTOS task. Core 1 only sets dirty flags.

4. **Core 0 never writes MIDI**: All MIDI output happens on Core 1. Core 0 only reads slow params (relaxed atomics).

5. **Catch system**: pot must physically reach the stored value before it can change a parameter. Prevents jumps on bank switch or context change.

6. **Bank slots always alive**: all 8 banks exist, only foreground receives pad input. ARPEG engines run in background regardless of which bank is selected.

---

## 4. Where Things Break (common bug patterns)

| Pattern | Example | Where to look |
|---------|---------|---------------|
| Stale state after bank switch | Catch targets from old bank | `main.cpp` handleManagerUpdates(), `PotRouter::resetPerBankCatch` |
| Orphan notes on mode change | noteOff uses wrong note number | `_lastResolvedNote` usage in `MidiEngine.cpp` |
| MIDI flood from noisy ADC | CC toggling ±1 at boundary | `PotRouter.cpp` CC dirty check, hysteresis |
| Timing burst after clock glitch | ArpScheduler fires many steps | `ArpScheduler.cpp` ticksElapsed guard (capped at 24 ticks = 1 quarter note) |
| BLE/USB tick contamination | PLL mixes intervals from 2 sources | `ClockManager.cpp` per-source atomic counters (`_pendingUsbTicks`, `_pendingBleTicks`) + per-source timestamps — only the active source feeds the PLL |
| Button combo confusion | Rear+left held simultaneously | `PotRouter::resolveBindings` mask checks |
| BLE noteOff drop under AT congestion | 8+ pads with AT released simultaneously — BLE buffer may be full of AT from previous cycle, noteOff dropped → stuck notes. USB unaffected. Safety net: `midiPanic()` (triple-click rear). | `MidiTransport.cpp` BLE send path, `MidiEngine.cpp:138` flush() |
| Rapid bank switch with held pad | 4+ bank switches in <500ms while pad held — each switch sends allNotesOff + re-triggers noteOn, producing rapid click artifacts. Physically unlikely (hold+switch = same hand). | `BankManager.cpp:117` switchToBank(), `main.cpp:967` s_lastKeys sync. **To verify on hardware.** |

---

## 5. File Map by Domain

| Domain | Primary files | Also touches |
|--------|--------------|-------------|
| **Pad sensing** | `CapacitiveKeyboard.cpp/.h` (DO NOT MODIFY) | `main.cpp` sensingTask() |
| **MIDI output** | `MidiEngine.cpp/.h`, `MidiTransport.cpp/.h` | `main.cpp` handlePadInput() |
| **Note resolution** | `ScaleResolver.cpp/.h` | `MidiEngine.cpp:51`, `ArpEngine.cpp:374` |
| **Groove templates** | `midi/GrooveTemplates.h` | `ArpEngine.cpp` (shared, future LoopEngine) |
| **Bank management** | `BankManager.cpp/.h` | `main.cpp` handleManagerUpdates() (post-switch) |
| **Scale/hold/octave** | `ScaleManager.cpp/.h` | `main.cpp` handleManagerUpdates() (flag consumption) |
| **Arpeggiator** | `ArpEngine.cpp/.h`, `ArpScheduler.cpp/.h` | `main.cpp` handlePadInput() (pile management) |
| **Clock/PLL** | `ClockManager.cpp/.h` | `MidiTransport.cpp:28-30,129` (tick callbacks) |
| **Pots/catch** | `PotRouter.cpp/.h` | `main.cpp` handlePotPipeline() |
| **LEDs** | `LedController.cpp/.h` | `HardwareConfig.h` (colors, timing constants) |
| **NVS** | `NvsManager.cpp/.h` | `KeyboardData.h` (store structs, versions) |
| **Battery** | `BatteryMonitor.cpp/.h` | `HardwareConfig.h` (ADC pin, thresholds), `LedController.cpp` (setBatteryLow) |
| **Setup mode** | `SetupManager.cpp`, `Tool*.cpp` | `main.cpp` setup() (entry detection) |

---

## 6. Dirty Flags & Event Queues

### Dirty flags (set → consumed pattern)
| Flag | Set by | Consumed by | Purpose |
|------|--------|-------------|---------|
| `_sequenceDirty` | addPad, removePad, setPattern, setOctaveRange | `rebuildSequence()` at next tick | Arp sequence rebuild |
| `_tickFlash` | `ArpEngine::tick()` | `LedController::update()` via `consumeTickFlash()` | LED beat flash |
| `hasMoved(p)` | `PotFilter::updateAll()` deadband | `applyBinding()` per frame | Pot movement gate |
| `_ccDirty[s]` | `applyBinding()` CC value change | `consumeCC()` in main loop | MIDI CC send |
| `_midiPbDirty` | `applyBinding()` PB value change | `consumePitchBend()` in main loop | MIDI PB send |
| `_dirty` (pot) | Any parameter write | `clearDirty()` after NVS debounce | NVS save trigger |
| `_scaleChangeType` | `processScalePads()` | `consumeScaleChange()` (auto-clear) | Scale change flag |
| `_octaveChanged` | Octave pad press | `hasOctaveChanged()` (auto-clear) | Octave change flag |
| `_holdToggled` | Hold pad press | `hasHoldToggled()` (auto-clear) | Hold toggle flag |

### Event queues
| Queue | Size | Producer | Consumer | Overflow behavior |
|-------|------|----------|----------|-------------------|
| Aftertouch ring | 64 entries | `updateAftertouch()` | `flush()` (16/frame max) | Silent drop. **Capacity math**: at default 25ms rate, N pads generate N events/25ms. flush drains 16/frame × ~40 frames/25ms = 640 drain capacity. Safe up to ~48 pads. At minimum rate (10ms): N events/10ms vs 16×10=160 drain → safe up to ~40 pads. Real-world: ≤10 fingers, never saturates. |
| Arp events | 64 per engine | `tick()` (noteOn/Off pairs) | `processEvents()` every frame | noteOff fail → skip entire step (safe); noteOn fail → cancel orphaned noteOff (safe) |
| NVS writes | per-field dirty flags | Main loop | Background FreeRTOS task | Coalesced (latest wins) |
