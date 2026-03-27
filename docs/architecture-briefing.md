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
Key files: `main.cpp:559-577`, `main.cpp:648-666` (left-release cleanup), `MidiEngine.cpp:47-57`, `ScaleResolver.cpp`, `MidiTransport.cpp:186-204`

### Arp Tick → MIDI NoteOn
```
ClockManager::update() → _currentTick++
→ ArpScheduler::tick()
  → tickAccum += ticksElapsed
  → while (tickAccum >= divisor):
      ArpEngine::tick()
        → resolve position → MIDI note via ScaleResolver
        → schedule noteOff FIRST (atomic pair)
        → if shuffle: schedule noteOn (delayed)
          else: refCountNoteOn() (immediate send)
→ ArpScheduler::processEvents()
  → for each pending event where time arrived:
      refCountNoteOn() or refCountNoteOff()
      → MIDI send only on refcount transitions (0→1, 1→0)
```
Key files: `ClockManager.cpp:108-134`, `ArpScheduler.cpp:98-131`, `ArpEngine.cpp:288-423`, `ArpEngine.cpp:436-536`

### Bank Switch (all side effects in order)
```
1. sendPitchBend(8192) on OLD channel         [BankManager.cpp:121]
2. allNotesOff() on OLD channel               [BankManager.cpp:122]
3. Drain aftertouch ring buffer               [MidiEngine.cpp:98-99]
4. Update foreground flags                    [BankManager.cpp:126-129]
5. setChannel(newBank)                        [BankManager.cpp:132]
6. sendPitchBend(newBank.pitchBendOffset)     [BankManager.cpp:133]
7. LED: setCurrentBank + triggerConfirm       [BankManager.cpp:137-140]
8. loadStoredPerBank() into PotRouter         [main.cpp:483-487]
9. seedCatchValues(keepGlobalCatch=true)      [main.cpp:488]
   — reseeds stored values; global targets keep catch state (tempo, shape…)
   — per-bank targets lose catch (will be uncaught by step 10)
10. resetPerBankCatch() — uncatch per-bank only [main.cpp:489]
11. queueBankWrite() to NVS                   [main.cpp:467]
```

### Scale Change
```
ScaleManager::processScalePads() detects root/mode/chromatic pad press
  → NORMAL: allNotesOff() immediately (prevents orphan notes)
  → ARPEG: NO flush — pending events carry resolved notes, next tick re-resolves
  → Set flag: _scaleChangeType = ROOT|MODE|CHROMATIC
main.cpp consumes flag:
  → NVS queue + ArpEngine::setScaleConfig() + LED confirm
```
Key files: `ScaleManager.cpp:123-227`, `main.cpp:492-506`

### Pot → Parameter
```
readAndSmooth(): analogRead → EMA (α=0.1) → deadband gate (±40 ADC)
resolveBindings(): button state + bank type → find best binding
applyBinding():
  if TARGET_LED_BRIGHTNESS: bypass catch, apply immediately
  if !caught: compare adc vs storedValue, show uncaught bargraph, WAIT
  if caught: convert ADC → parameter range, write output, show bargraph
  → Global targets: propagate storedValue across contexts
main.cpp: read getters → write to BankSlot/ArpEngine/atomics
  → consumeCC/consumePitchBend → MidiTransport sends
```
Key files: `PotRouter.cpp:329-552`, `main.cpp:662-716`

---

## 2. Inter-Core Communication

All lock-free. No mutex anywhere in runtime code.

| What | Type | Writer | Reader | Order |
|------|------|--------|--------|-------|
| Double buffer index | `atomic<uint8_t>` | Core 0 | Core 1 | release/acquire |
| Response shape | `atomic<float>` | Core 1 | Core 0 | relaxed |
| Slew rate | `atomic<uint16_t>` | Core 1 | Core 0 | relaxed |
| Pad sensitivity | `atomic<uint8_t>` | Core 1 | Core 0 | relaxed |
| Clock tick count | `atomic<uint8_t>` `_pendingTickCount` | NimBLE task / USB cb | Core 1 | release/acquire |
| Last tick time (any) | `atomic<uint32_t>` `_lastExternalTickUs` | NimBLE task / USB cb | Core 1 | release/acquire |
| Last tick time (USB) | `atomic<uint32_t>` `_lastUsbTickUs` | USB callback | Core 1 | relaxed |
| Last tick source | `atomic<uint8_t>` `_lastSource` | NimBLE task / USB cb | Core 1 | relaxed |

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
| Stale state after bank switch | Catch targets from old bank | `main.cpp` bank switch block, `PotRouter::resetPerBankCatch` |
| Orphan notes on mode change | noteOff uses wrong note number | `_lastResolvedNote` usage in `MidiEngine.cpp` |
| MIDI flood from noisy ADC | CC toggling ±1 at boundary | `PotRouter.cpp` CC dirty check, hysteresis |
| Timing burst after clock glitch | ArpScheduler fires many steps | `ArpScheduler.cpp` ticksElapsed guard (capped at 24 ticks = 1 quarter note) |
| BLE/USB tick contamination | PLL mixes intervals from 2 sources | `ClockManager.cpp` source filtering |
| Button combo confusion | Rear+left held simultaneously | `PotRouter::resolveBindings` mask checks |

---

## 5. File Map by Domain

| Domain | Primary files | Also touches |
|--------|--------------|-------------|
| **Pad sensing** | `CapacitiveKeyboard.cpp/.h` (DO NOT MODIFY) | `main.cpp:95-117` (sensing task) |
| **MIDI output** | `MidiEngine.cpp/.h`, `MidiTransport.cpp/.h` | `main.cpp:559-596` (note processing) |
| **Note resolution** | `ScaleResolver.cpp/.h` | `MidiEngine.cpp:51`, `ArpEngine.cpp:349` |
| **Bank management** | `BankManager.cpp/.h` | `main.cpp:460-489` (post-switch) |
| **Scale/hold/octave** | `ScaleManager.cpp/.h` | `main.cpp:491-532` (flag consumption) |
| **Arpeggiator** | `ArpEngine.cpp/.h`, `ArpScheduler.cpp/.h` | `main.cpp:587-634` (pile management) |
| **Clock/PLL** | `ClockManager.cpp/.h` | `MidiTransport.cpp:28-30,129` (tick callbacks) |
| **Pots/catch** | `PotRouter.cpp/.h` | `main.cpp:662-716` (consume outputs) |
| **LEDs** | `LedController.cpp/.h` | `HardwareConfig.h` (colors, timing constants) |
| **NVS** | `NvsManager.cpp/.h` | `KeyboardData.h` (store structs, versions) |
| **Battery** | `BatteryMonitor.cpp/.h` | `HardwareConfig.h` (ADC pin, thresholds), `LedController.cpp` (setBatteryLow) |
| **Setup mode** | `SetupManager.cpp`, `Tool*.cpp` | `main.cpp:208-260` (entry detection) |

---

## 6. Dirty Flags & Event Queues

### Dirty flags (set → consumed pattern)
| Flag | Set by | Consumed by | Purpose |
|------|--------|-------------|---------|
| `_sequenceDirty` | addPad, removePad, setPattern, setOctaveRange | `rebuildSequence()` at next tick | Arp sequence rebuild |
| `_tickFlash` | `ArpEngine::tick()` | `LedController::update()` via `consumeTickFlash()` | LED beat flash |
| `_moved[p]` | `readAndSmooth()` deadband | `applyBinding()` per frame | Pot movement gate |
| `_ccDirty[s]` | `applyBinding()` CC value change | `consumeCC()` in main loop | MIDI CC send |
| `_midiPbDirty` | `applyBinding()` PB value change | `consumePitchBend()` in main loop | MIDI PB send |
| `_dirty` (pot) | Any parameter write | `clearDirty()` after NVS debounce | NVS save trigger |
| `_scaleChangeType` | `processScalePads()` | `consumeScaleChange()` (auto-clear) | Scale change flag |
| `_octaveChanged` | Octave pad press | `hasOctaveChanged()` (auto-clear) | Octave change flag |
| `_holdToggled` | Hold pad press | `hasHoldToggled()` (auto-clear) | Hold toggle flag |

### Event queues
| Queue | Size | Producer | Consumer | Overflow behavior |
|-------|------|----------|----------|-------------------|
| Aftertouch ring | 64 entries | `updateAftertouch()` | `flush()` (16/frame max) | Silent drop |
| Arp events | 36 per engine | `tick()` (noteOn/Off pairs) | `processEvents()` every frame | Skip entire step |
| NVS writes | per-field dirty flags | Main loop | Background FreeRTOS task | Coalesced (latest wins) |
