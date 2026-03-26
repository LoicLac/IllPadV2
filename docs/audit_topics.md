# ILLPAD V2 — Audit Topic Pool

## Memory Safety
- [MEM-01] Double buffer atomics: verify s_active uses memory_order_release on store (Core 0) and memory_order_acquire on load (Core 1), forming a correct happens-before relationship
- [MEM-02] No volatile for inter-core sync: verify zero uses of volatile for shared state between cores — all inter-core data uses std::atomic
- [MEM-03] Slow param atomics: verify pot-to-Core0 parameters (shape, slew, sensitivity) use std::atomic with memory_order_relaxed on both store and load
- [MEM-04] Clock callback atomics: verify ClockManager atomic operations from BLE/USB callbacks (different task context) use correct release/acquire pairing with _newTickAvailable as the synchronization point
- [MEM-05] NVS dirty flag data race: verify _pending* compound structs (ScaleConfig, ArpPotStore) shared between loop and NVS task cannot cause partial reads — assess actual risk on ESP32

## Note Integrity
- [NOTE-01] noteOff uses lastResolvedNote: verify every noteOff code path uses the stored _lastResolvedNote value and never calls ScaleResolver::resolve()
- [NOTE-02] allNotesOff sweep completeness: verify MidiEngine::allNotesOff() iterates all 48 pads and sends noteOff for any with _lastResolvedNote != 0xFF, then clears the array
- [NOTE-03] Orphan note prevention on scale change: verify that changing scale while NORMAL pads are held does not produce orphan notes (noteOff must use old note, not re-resolved new note)
- [NOTE-04] MIDI panic completeness: verify the panic routine covers all note sources — MidiEngine stored notes, all ArpEngine refcounts, and CC123 safety net on all channels
- [NOTE-05] Aftertouch uses stored note: verify poly-aftertouch sends on _lastResolvedNote[padIndex], not a re-resolved value

## Arpeggiator Core
- [ARP-01] Pile stores positions not notes: verify _positions[] and _positionOrder[] store padOrder ranks (0-47), never resolved MIDI notes
- [ARP-02] Sequence encoding: verify _sequence[] encodes as (position + octave * 48) and decodes correctly with modulo/division
- [ARP-03] Pattern Up correctness: verify Up pattern produces ascending order through positions then ascending through octaves
- [ARP-04] Pattern UpDown no duplicate at boundaries: verify UpDown pattern does not repeat the highest or lowest note at the direction change point
- [ARP-05] Pattern Order preserves insertion order: verify ARP_ORDER pattern uses _positionOrder[] (chronological) not _positions[] (sorted)
- [ARP-06] Octave expansion: verify 1-4 octave range correctly expands sequence length and each octave transposes by +12 semitones at resolution time

## Arp Timing
- [ARPT-01] Quantized start boundaries: verify Beat snaps to next multiple of 24 ticks and Bar to next multiple of 96 ticks
- [ARPT-02] MIDI Start bypasses quantize: verify 0xFA reception sets _waitingForQuantize = false so arp fires immediately
- [ARPT-03] Stop is always immediate: verify arp stop (both play/stop pad and MIDI Stop 0xFC) never waits for a quantize boundary
- [ARPT-04] Shuffle offset formula: verify offset = template[step%16] * depth * stepDuration / 100, matching the spec exactly
- [ARPT-05] Shuffle step counter resets: verify counter resets on all 3 conditions — play/stop toggle, pile 0-to-1 note, pattern change

## Arp Event System
- [ARPE-01] Event array cap at 36: verify MAX_PENDING_EVENTS = 36 and that overflow is handled gracefully (skip step, no stuck notes)
- [ARPE-02] Refcount noteOn 0-to-1 only: verify MIDI noteOn is sent only when _noteRefCount transitions from 0 to 1, not on subsequent increments
- [ARPE-03] Refcount noteOff 1-to-0 only: verify MIDI noteOff is sent only when _noteRefCount transitions from 1 to 0, not on higher-count decrements
- [ARPE-04] Atomic noteOff-first scheduling: verify tick() schedules the noteOff event before the noteOn event, and rolls back noteOff if noteOn scheduling fails
- [ARPE-05] flushPendingNoteOffs completeness: verify flush cancels all active events AND sweeps all 128 refcount entries, sending noteOff for any with count > 0

## Clock & Transport
- [CLK-01] PLL smoothing: verify ClockManager PLL reduces BLE jitter by averaging/filtering incoming tick intervals rather than using raw timestamps
- [CLK-02] Clock source priority: verify USB > BLE > last known > internal fallback ordering is implemented
- [CLK-03] MIDI Start resets tick counter: verify 0xFA sets _currentTick = 0 for bar-1 restart
- [CLK-04] MIDI Continue does not reset: verify 0xFB resumes without resetting tick counter (arps continue from position)
- [CLK-05] Follow Transport gating: verify Start/Stop/Continue are ignored when followTransport = false, but clock ticks (0xF8) are always received regardless

## LED System
- [LED-01] Priority state machine order: verify update() if/else chain matches spec priority exactly — boot > chase > error > battery > bargraph > confirmation > calibration > normal
- [LED-02] Sine pulse integer math: verify the 64-entry LUT is precomputed at boot and update() uses only integer math (no float) for sine pulse rendering
- [LED-03] Tick flash consume-once: verify consumeTickFlash() returns true exactly once per arp step and the flash is held for LED_TICK_FLASH_DURATION_MS
- [LED-04] Confirmation preemption: verify new confirmation blink unconditionally overwrites any active confirmation (no queuing, no priority between confirmation types)

## NVS & Persistence
- [NVS-01] NVS writes never block loop: verify notifyIfDirty() only calls xTaskNotifyGive and all Preferences/nvs writes happen in the dedicated NVS task
- [NVS-02] Boot-time reads are pre-loop: verify all NVS reads happen in setup() before loop() starts — no blocking reads at runtime
- [NVS-03] Setup tool direct writes: verify setup tools use direct Preferences writes (NVS task not yet running) and this is safe because setup runs in setup() before the NVS task is created
- [NVS-04] NVS batching delay: verify the NVS task coalesces rapid dirty flags with a delay (e.g., 50ms) before committing to flash, reducing write wear

## Pot System
- [POT-01] Catch system prevents parameter jumps: verify a pot must physically reach within POT_CATCH_WINDOW of the stored value before it takes effect after a context switch
- [POT-02] Catch reset on bank switch: verify per-bank parameters get caught=false on bank switch, forcing re-catch at the new bank's value
- [POT-03] Global target catch propagation: verify writing a global target (e.g., Tempo) propagates storedValue to all same-target bindings on the same pot, preventing stale catch after bank type switch
- [POT-04] CC slot uses binding index: verify MIDI CC slot lookup uses binding index (not CC number) to avoid cross-context collision
- [POT-05] PB max one per context: verify at most one MIDI Pitchbend binding exists per context (NORMAL/ARPEG), with auto-steal if a second is assigned

## Button Logic
- [BTN-01] Left and rear modifiers never cross: verify left button only modifies right pots, rear button only modifies rear pot — no cross-modifier paths exist
- [BTN-02] Play/stop pad dual role: verify play/stop pad is only a transport toggle when ARPEG + HOLD ON — in HOLD OFF mode and NORMAL mode it acts as a regular music pad
- [BTN-03] Bank and scale share left button: verify pad roles for bank (8) and scale (15) and arp (6) are distinct — no pad can have two roles simultaneously
- [BTN-04] Hold toggle ARPEG only: verify hold toggle pad only works on ARPEG banks and is ignored on NORMAL banks

## Boot Sequence
- [BOOT-01] Progressive LED fill: verify boot steps 1-8 each light one additional LED, creating a progressive fill pattern
- [BOOT-02] Failure blink on step 3: verify MPR121/keyboard failure causes step 3 LED to blink rapidly while steps 1-2 stay solid, and the system halts forever
- [BOOT-03] Setup mode detection window: verify rear button held 3s during boot triggers setup mode with chase pattern, and this window exists between step 3 and step 4

## Conventions & Style
- [CONV-01] No runtime allocation: verify zero uses of new/delete/malloc/free at runtime — all objects are statically instantiated
- [CONV-02] Unsigned millis overflow safety: verify every millis() comparison uses (now - startTime) < duration pattern, never (startTime + duration) > now
- [CONV-03] Naming conventions: verify s_ prefix for static globals, _ prefix for members, SCREAMING_SNAKE_CASE for constants throughout the codebase
- [CONV-04] Debug macro zero overhead: verify DEBUG_SERIAL=0 produces zero overhead — no string formatting, no function calls, no side effects when disabled

## MIDI Output
- [MIDI-01] Channel routing correctness: verify NORMAL mode sends on MidiEngine's current channel (foreground bank) and ARPEG mode sends on the engine's own stored channel (may be background)
- [MIDI-02] BLE noteOn/Off bypass queue: verify noteOn and noteOff messages bypass any BLE queue and are sent immediately, while aftertouch overflow is tolerated
- [MIDI-03] Aftertouch throttle: verify poly-aftertouch is rate-limited per pad by _aftertouchIntervalMs to prevent MIDI flooding
- [MIDI-04] ILLPAD never sends Start/Stop/Continue: verify the ILLPAD only sends clock ticks (0xF8) in master mode — never 0xFA, 0xFB, or 0xFC
- [MIDI-05] Flush ordering: verify MidiEngine.flush() is the last MIDI operation in the critical path (step 11 in loop order), after all note and arp processing

## Serial & VT100
- [VT100-01] Screen clearing and cursor positioning: verify setup tools use proper VT100 escape sequences for clear screen, cursor home, and cursor positioning — no raw terminal writes that could garble display
- [VT100-02] Color code correctness: verify ANSI color codes are correctly paired (every color-on has a reset), preventing color bleed across lines or after tool exit
- [VT100-03] Refresh rate throttling: verify setup tool display refresh is throttled (e.g., 200-500ms intervals) to prevent serial flooding and screen flickering
- [VT100-04] Button-as-ENTER input: verify physical button press is correctly mapped as ENTER/confirm input in setup mode, working alongside serial input

## RGB LED (WS2812 RGB NeoPixel, NEO_GRB)
- [RGB-01] Per-pixel brightness scaling: verify setPixel() and setPixelScaled() multiply each RGB channel by _brightness/255 manually — strip.setBrightness() must NOT be called anywhere (it's lossy and only rescales on change)
- [RGB-02] Single strip.show() per frame: verify update() calls _strip.show() exactly once per execution path — no double-show, and every early return path includes a show() call
- [RGB-03] ConfirmType enum caller consistency: verify every caller of triggerConfirm() uses the new split types (CONFIRM_SCALE_ROOT/MODE/CHROM, CONFIRM_HOLD_ON/OFF, CONFIRM_PLAY/STOP) — no references to old CONFIRM_SCALE or CONFIRM_HOLD remain
- [RGB-04] Bargraph catch visualization: verify not-caught mode renders dim bar (COL_WHITE_DIM/COL_BLUE_DIM) for realLevel + bright cursor at potLevel index, and caught mode renders full bar using realLevel (0-8 count), not potLevel (0-7 index)
- [RGB-05] Bank switch blinks destination only: verify CONFIRM_BANK_SWITCH renders normal display for all LEDs first, then overlays the blink on _currentBank LED only — other LEDs must NOT be cleared
- [RGB-06] Octave group mapping: verify octave confirmation maps param 1→LEDs 0-1, param 2→LEDs 2-3, param 3→LEDs 4-5, param 4→LEDs 6-7 using formula startLed=(param-1)*2, all other LEDs off
- [RGB-07] Play confirmation two-phase: verify phase 0 fires immediate green ack (COL_PLAY_ACK), then phases 1-3 fire beat-synced flashes at 50%/75%/100% of COL_ARP_PLAY using ClockManager tick detection (24 ticks per beat)
- [RGB-08] Fade-out confirmations: verify CONFIRM_HOLD_OFF and CONFIRM_STOP use linear decay over LED_CONFIRM_FADE_MS (300ms) with setPixelScaled, producing a visible fade rather than instant off
- [RGB-09] Error uses LEDs 3-4 only: verify error mode sets only _strip pixels at indices 3 and 4 (physical LEDs 4-5), not all 8, and all other pixels are cleared
- [RGB-10] Setup comet ping-pong bounds: verify _cometPos cycles 0-13 correctly mapping to LED indices 0-7 forward then 6-1 backward, with trail at -1 (40%) and -2 (10%) never accessing out-of-bounds indices
- [RGB-11] Battery gauge gradient no pulse: verify battery display uses COL_BATTERY[i] per LED with no heartbeat/triangle pulse — solid bar only, for BAT_DISPLAY_DURATION_MS
- [RGB-12] ScaleManager consumeScaleChange correctness: verify consumeScaleChange() returns the correct type (ROOT/MODE/CHROMATIC) matching which pad was pressed, and clears to SCALE_CHANGE_NONE after consumption
- [RGB-13] PotRouter bargraph state consistency: verify _bargraphPotLevel (0-7 range) and _bargraphCaught are set in both caught and not-caught code paths of applyBinding(), and that _bargraphDirty is set in both paths
- [RGB-14] Context color resolution: verify LedController reads _slots[_currentBank].type to determine NORMAL (white) or ARPEG (blue) for bargraph and bank switch confirmation colors — no hardcoded color assumption

## Abstract / Architectural
- [ARCH-01] Scale change during held arp notes: verify that changing scale on a foreground ARPEG bank while notes are in the pile correctly flushes pending MIDI events (old notes) and re-resolves at next tick with new scale — no stuck notes, no wrong notes audible
- [ARCH-02] Rapid bank switching under load: verify switching banks rapidly (faster than loop cycle) does not corrupt foreground state, lose noteOffs, or cause double-allNotesOff — exactly one bank is foreground after any switch sequence
- [ARCH-03] Simultaneous USB and BLE MIDI consistency: verify the same MIDI data is sent on both transports and that BLE connection/disconnection does not affect USB output or vice versa
- [ARCH-04] BankManager vs ArpEngine separation: verify BankManager has zero knowledge of arp logic (no ArpEngine references) and ArpEngine has zero knowledge of bank selection (no BankManager references) — clean separation of concerns
- [ARCH-05] Background arp survives all foreground operations: verify a background ARPEG bank continues playing uninterrupted through: foreground bank switch, foreground scale change, foreground pot adjustment, and foreground NORMAL note playing
