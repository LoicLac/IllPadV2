# ILLPAD Firmware — Viewer API Extensions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the ILLPAD firmware serial protocol with annotation, atomic state dumps, and a runtime request-state handler — to feed the new JUCE Viewer app with all the data it needs to populate its UI without ambiguity or waiting for incremental events.

**Architecture:** Four targeted modifications to existing files, no refactoring, no NVS bump, no new struct. Total ~90 lines added. All modifications gated by existing `#if DEBUG_SERIAL` (active by default). Each modification independently testable via `pio device monitor` before integrating the JUCE viewer.

**Tech Stack:** C++17, Arduino framework, PlatformIO, ESP32-S3 (`esp32-s3-devkitc-1` environment).

**Spec reference:** [`docs/superpowers/specs/2026-05-15-illpad-viewer-design.md`](../specs/2026-05-15-illpad-viewer-design.md) §5-§11.

---

## Build & test commands

- Build: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
- Upload: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload`
- Monitor: `~/.platformio/penv/bin/pio device monitor -b 115200`

## Testing philosophy

Firmware embedded — no auto framework. Each task validated by:
1. Build succeeds with **zero new warnings** (`pio run` exit code 0, no `warning:` in output).
2. Upload to device succeeds.
3. Observe expected output on serial monitor.
4. Trigger the action (boot, switch bank, turn pot, send command) and verify behavior.

User-side commits at each task per autocommit policy. HW gate before each commit.

## File map

| File | Purpose | Modification kind |
|---|---|---|
| `src/managers/PotRouter.h` | Pot routing API | Add public `getSlotForTarget` getter |
| `src/managers/PotRouter.cpp` | Pot routing impl | Implement `getSlotForTarget` (reverse lookup in `_mapping`) |
| `src/main.cpp` | App entry + runtime loop | Annotate 15× `[POT]` lines, add `dumpBanksGlobal()`, add `dumpRuntimeState()`, add `pollRuntimeCommands()` |
| `src/managers/BankManager.cpp` | Bank switch | Call `dumpRuntimeState()` after `[BANK]` log |

---

### Task A.1: Add `PotRouter::getSlotForTarget` accessor

Required by all subsequent tasks. Reverse-lookup in the user's saved mapping returns the slot index (0..7) that routes to a given `PotTarget` in a given context. Slot layout per `KeyboardData.h:566`: `[0]=R1 alone, [1]=R1+hold, [2]=R2 alone, [3]=R2+hold, [4]=R3 alone, [5]=R3+hold, [6]=R4 alone, [7]=R4+hold`. Returns `0xFF` if target not found in the context.

**Files:**
- Modify: `src/managers/PotRouter.h` (add public method declaration)
- Modify: `src/managers/PotRouter.cpp` (add implementation)

- [ ] **Step 1: Declare `getSlotForTarget` in `PotRouter.h`**

Add this line in the public section, right after `isPerBankTarget` declaration (around line 169) — actually move it to the public section if it's currently private. Find the public getters section (around line 58) and add:

```cpp
  // Reverse lookup : returns the slot index (0..7) in the given context
  // that routes to `t`, or 0xFF if not found. Caller passes context.
  uint8_t getSlotForTarget(PotTarget t, bool isArpContext) const;
```

- [ ] **Step 2: Implement `getSlotForTarget` in `PotRouter.cpp`**

Add at the end of the file (before the closing `}` if any, or just after the last function):

```cpp
uint8_t PotRouter::getSlotForTarget(PotTarget t, bool isArpContext) const {
  const PotMapping* map = isArpContext ? _mapping.arpegMap : _mapping.normalMap;
  for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
    if (map[i].target == t) return i;
  }
  return 0xFF;  // not found
}
```

- [ ] **Step 3: Build to verify it compiles**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

Expected: exit code 0, zero new warnings.

- [ ] **Step 4: Commit**

```bash
git add src/managers/PotRouter.h src/managers/PotRouter.cpp
git commit -m "feat(potrouter): add getSlotForTarget reverse lookup

Needed by viewer API extensions to annotate [POT] log lines with the
slot of origin (R1/R1H/R2/.../R4H). Pure read-only accessor on the
user-saved mapping, gated by context (NORMAL vs ARPEG)."
```

---

### Task A.2: Annotate `[POT]` log lines with slot of origin

Modify all 15 `Serial.printf("[POT] ...")` lines in `main.cpp::debugOutput()` (lines 884-905) so each line is prefixed with the slot name (`R1`, `R1H`, `R2`, `R2H`, `R3`, `R3H`, `R4`, `R4H`). The slot is reverse-looked up via `PotRouter::getSlotForTarget` based on the parameter and the current bank's context.

A small static helper formats `slot index → slot name`. The context is derived from the foreground bank's type.

**Files:**
- Modify: `src/main.cpp` lines 846-905 (the `debugOutput` function)

- [ ] **Step 1: Add slot-name helper at top of `debugOutput()` body**

Locate `static void debugOutput(bool leftHeld, bool rearHeld) {` (around line 847). Just inside the function, **before** the `#if DEBUG_SERIAL`, add:

```cpp
  static const char* slotName(uint8_t slot) {
    static const char* NAMES[POT_MAPPING_SLOTS] = {
      "R1", "R1H", "R2", "R2H", "R3", "R3H", "R4", "R4H"
    };
    return slot < POT_MAPPING_SLOTS ? NAMES[slot] : "--";
  }
```

Actually `static const char*` can't be inside a function this way for the named-function nested style. Replace with a file-scope helper. Find an appropriate place near the top of `main.cpp` (after `static const char*` definitions) and add:

```cpp
static const char* potSlotName(uint8_t slot) {
  static const char* NAMES[POT_MAPPING_SLOTS] = {
    "R1", "R1H", "R2", "R2H", "R3", "R3H", "R4", "R4H"
  };
  return slot < POT_MAPPING_SLOTS ? NAMES[slot] : "--";
}
```

- [ ] **Step 2: Determine current context inside `debugOutput`**

Inside the `#if DEBUG_SERIAL` block, just after the local static cache variables (around line 864), add:

```cpp
    // Derive current context (NORMAL mapping vs ARPEG mapping) from the
    // foreground bank type. ARPEG_GEN and LOOP also use the arpeg map.
    BankType curType = s_banks[s_bankManager.getCurrentBank()].type;
    bool arpCtx = (curType == BANK_ARPEG) || (curType == BANK_ARPEG_GEN) || (curType == BANK_LOOP);
```

- [ ] **Step 3: Rewrite each `[POT]` printf to include the slot**

Replace each of the 15 lines in the block. For example:

```cpp
    if ((int)(shape * 100) != (int)(s_dbgShape * 100)) { Serial.printf("[POT] Shape=%.2f\n", shape); s_dbgShape = shape; }
```

becomes:

```cpp
    if ((int)(shape * 100) != (int)(s_dbgShape * 100)) {
      Serial.printf("[POT] %s: Shape=%.2f\n",
                    potSlotName(s_potRouter.getSlotForTarget(TARGET_RESPONSE_SHAPE, arpCtx)),
                    shape);
      s_dbgShape = shape;
    }
```

Apply the same pattern to every `[POT]` line, using the matching `PotTarget` enum:

| Existing line | `PotTarget` |
|---|---|
| `Shape=%.2f` | `TARGET_RESPONSE_SHAPE` |
| `Slew=%u` | `TARGET_SLEW_RATE` |
| `AT_Deadzone=%u` | `TARGET_AT_DEADZONE` |
| `Tempo=%u BPM` | `TARGET_TEMPO_BPM` |
| `LED_Bright=%u` | `TARGET_LED_BRIGHTNESS` |
| `PadSens=%u` | `TARGET_PAD_SENSITIVITY` |
| `BaseVel=%u` | `TARGET_BASE_VELOCITY` |
| `VelVar=%u` | `TARGET_VELOCITY_VARIATION` |
| `PitchBend=%u` | `TARGET_PITCH_BEND` |
| `Gate=%.2f` | `TARGET_GATE_LENGTH` |
| `ShufDepth=%.2f` | `TARGET_SHUFFLE_DEPTH` |
| `Division=%s` | `TARGET_DIVISION` |
| `Pattern=%s` | `TARGET_PATTERN` |
| `GenPos=%u` | `TARGET_GEN_POSITION` |
| `ShufTpl=%u` | `TARGET_SHUFFLE_TEMPLATE` |

Final format for each line: `[POT] <SLOT>: <param>=<value> [unit]\n`.

- [ ] **Step 4: Build to verify zero new warnings**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

Expected: exit code 0, no `warning:` in output.

- [ ] **Step 5: Upload + HW smoke test**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

After boot, turn each of R1, R2, R3, R4 (alone and with rear-left button held). Verify each pot move emits a line of the form `[POT] R1: Tempo=120 BPM`, with the right slot name reflecting the current mapping and context.

**Pass criteria**: every observed `[POT]` line starts with one of `R1:`, `R1H:`, `R2:`, `R2H:`, `R3:`, `R3H:`, `R4:`, `R4H:`. No `--:` (would indicate target not found in mapping). Slot name changes when switching from a NORMAL bank to an ARPEG bank (different mapping).

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat(viewer-api): annotate [POT] logs with slot of origin

Each runtime [POT] line now prefixed with the slot name (R1/R1H/R2/.../R4H)
derived from the user mapping in the current context (NORMAL or ARPEG).
Allows the JUCE viewer to place values in the correct cadran without
guessing. No behavior change, log format only."
```

---

### Task A.3: Add `dumpBanksGlobal()` emitting `[BANKS]` once at boot

Emits a `[BANKS] count=8` header followed by 8 `[BANK] idx=N ...` lines. Includes type, channel, scaleGroup (resolved name letter), and conditional `division`, `playing`, `octave`, `mutationLevel` fields per the spec §8.

**Files:**
- Modify: `src/main.cpp` (add function + call site)

- [ ] **Step 1: Add `dumpBanksGlobal()` function**

Insert near the other helper functions in `main.cpp` (e.g., just before `setup()`). The function reads from `s_banks[]`, `s_bankManager`, `s_nvsManager`:

```cpp
#if DEBUG_SERIAL
static void dumpBanksGlobal() {
  static const char* TYPE_NAMES[] = { "NORMAL", "ARPEG", "ARPEG_GEN", "LOOP" };
  static const char* DIV_NAMES[]  = { "4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/32","1/64" };

  Serial.printf("[BANKS] count=%d\n", NUM_BANKS);
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    BankSlot& b = s_banks[i];
    uint8_t group = s_nvsManager.getLoadedScaleGroup(i);
    char groupChar = (group == 0) ? '0' : (char)('A' + group - 1);
    const char* typeName = (b.type < 4) ? TYPE_NAMES[b.type] : "?";

    Serial.printf("[BANK] idx=%d type=%s ch=%d group=%c",
                  i + 1, typeName, i + 1, groupChar);

    bool isArp = (b.type == BANK_ARPEG) || (b.type == BANK_ARPEG_GEN);
    if (isArp && b.arpEngine) {
      ArpDivision d = b.arpEngine->getDivision();
      Serial.printf(" division=%s playing=%s",
                    (uint8_t)d < 9 ? DIV_NAMES[(uint8_t)d] : "?",
                    b.arpEngine->isPlaying() ? "true" : "false");
      if (b.type == BANK_ARPEG) {
        Serial.printf(" octave=%d", b.arpEngine->getOctaveRange());
      } else { // ARPEG_GEN
        Serial.printf(" mutationLevel=%d", b.arpEngine->getMutationLevel());
      }
    }
    Serial.println();
  }
}
#endif
```

Note: requires `ArpEngine::getDivision()`, `getOctaveRange()`, `getMutationLevel()` accessors. Verify they exist; if not, add as trivial getters in the same task.

- [ ] **Step 2: Verify required `ArpEngine` getters exist**

Run: `grep -n "getDivision\|getOctaveRange\|getMutationLevel" src/arp/ArpEngine.h`

Expected: all three accessors are declared. If any missing, add it as a trivial const getter exposing the existing private field. Then build to verify.

- [ ] **Step 3: Call `dumpBanksGlobal()` at end of `setup()`**

Find `Serial.println("[INIT] Ready.");` in `setup()` (around line 484). Immediately after that line:

```cpp
  Serial.println("[INIT] Ready.");
  #if DEBUG_SERIAL
  dumpBanksGlobal();
  #endif
```

- [ ] **Step 4: Build to verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

Expected: exit code 0, zero new warnings.

- [ ] **Step 5: Upload + HW smoke test**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

Watch the boot output. After `[INIT] Ready.`, expect to see:

```
[BANKS] count=8
[BANK] idx=1 type=... ch=1 group=... [division=... playing=... octave=...]
... (8 lines)
```

**Pass criteria**: 1 line `[BANKS] count=8` followed by exactly 8 `[BANK]` lines. ARPEG banks have `division=`, `playing=`, `octave=` fields. ARPEG_GEN banks have `division=`, `playing=`, `mutationLevel=`. NORMAL and LOOP banks have only `idx`, `type`, `ch`, `group`.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp src/arp/ArpEngine.h
git commit -m "feat(viewer-api): add [BANKS] global dump at boot

Emits a [BANKS] count=8 header + 8 [BANK] lines after [INIT] Ready.
Includes type, channel, scaleGroup, plus division/playing/octave or
mutationLevel for arp banks. Allows the JUCE viewer to populate its
ALL BANKS panel without waiting for events."
```

---

### Task A.4: Add `dumpRuntimeState()` emitting `[STATE]` at boot + bank switch

Emits one atomic `[STATE]` line per spec §9: `bank, mode, ch, scale, [octave|mutationLevel], R1..R4H = TARGET:VALUE`. Slots reverse-looked-up via `getSlotForTarget`; values formatted to match each `PotTarget`'s current value.

**Files:**
- Modify: `src/main.cpp` (add function + boot call site)
- Modify: `src/managers/BankManager.cpp` (call site after `[BANK]` log)
- Modify: `src/managers/BankManager.h` (forward-declare or extern the helper)

- [ ] **Step 1: Add `dumpRuntimeState()` function in `main.cpp`**

Insert right after `dumpBanksGlobal()`. The function looks up the current bank, current mapping context, and for each of the 8 slots, finds what target it routes to and the current value of that target. Uses a helper to format `target:value` for each slot:

```cpp
#if DEBUG_SERIAL
static void formatSlotValue(char* buf, size_t bufSize, PotTarget t) {
  static const char* DIV_NAMES[]  = { "4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/32","1/64" };
  static const char* PAT_NAMES[]  = { "Up","Down","UpDown","Order","PedalUp","Converge" };

  switch (t) {
    case TARGET_RESPONSE_SHAPE:
      snprintf(buf, bufSize, "Shape:%.2f", s_potRouter.getResponseShape()); break;
    case TARGET_SLEW_RATE:
      snprintf(buf, bufSize, "Slew:%u", s_potRouter.getSlewRate()); break;
    case TARGET_AT_DEADZONE:
      snprintf(buf, bufSize, "ATDz:%u", s_potRouter.getAtDeadzone()); break;
    case TARGET_TEMPO_BPM:
      snprintf(buf, bufSize, "Tempo:%u", s_potRouter.getTempoBPM()); break;
    case TARGET_LED_BRIGHTNESS:
      snprintf(buf, bufSize, "LEDBright:%u", s_potRouter.getLedBrightness()); break;
    case TARGET_PAD_SENSITIVITY:
      snprintf(buf, bufSize, "PadSens:%u", s_potRouter.getPadSensitivity()); break;
    case TARGET_BASE_VELOCITY:
      snprintf(buf, bufSize, "BaseVel:%u", s_potRouter.getBaseVelocity()); break;
    case TARGET_VELOCITY_VARIATION:
      snprintf(buf, bufSize, "VelVar:%u", s_potRouter.getVelocityVariation()); break;
    case TARGET_PITCH_BEND:
      snprintf(buf, bufSize, "PitchBend:%u", s_potRouter.getPitchBend()); break;
    case TARGET_GATE_LENGTH:
      snprintf(buf, bufSize, "Gate:%.2f", s_potRouter.getGateLength()); break;
    case TARGET_SHUFFLE_DEPTH:
      snprintf(buf, bufSize, "ShufDepth:%.2f", s_potRouter.getShuffleDepth()); break;
    case TARGET_DIVISION: {
      uint8_t d = (uint8_t)s_potRouter.getDivision();
      snprintf(buf, bufSize, "Division:%s", d < 9 ? DIV_NAMES[d] : "?"); break;
    }
    case TARGET_PATTERN: {
      uint8_t p = (uint8_t)s_potRouter.getPattern();
      snprintf(buf, bufSize, "Pattern:%s", p < 6 ? PAT_NAMES[p] : "?"); break;
    }
    case TARGET_GEN_POSITION:
      snprintf(buf, bufSize, "GenPos:%u", s_potRouter.getGenPosition()); break;
    case TARGET_SHUFFLE_TEMPLATE:
      snprintf(buf, bufSize, "ShufTpl:%u", s_potRouter.getShuffleTemplate()); break;
    case TARGET_EMPTY:
    case TARGET_NONE:
    default:
      snprintf(buf, bufSize, "---"); break;
  }
}

static void dumpRuntimeState() {
  static const char* TYPE_NAMES[] = { "NORMAL", "ARPEG", "ARPEG_GEN", "LOOP" };
  static const char* SLOT_NAMES[] = { "R1","R1H","R2","R2H","R3","R3H","R4","R4H" };
  static const char* ROOT_NAMES[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
  static const char* MODE_NAMES[] = { "Major","Minor","Dorian","Phrygian","Lydian","Mixolydian","Locrian" };

  uint8_t bankIdx = s_bankManager.getCurrentBank();
  BankSlot& b = s_banks[bankIdx];
  const char* typeName = (b.type < 4) ? TYPE_NAMES[b.type] : "?";

  Serial.printf("[STATE] bank=%d mode=%s ch=%d",
                bankIdx + 1, typeName, bankIdx + 1);

  // Scale
  if (b.scale.chromatic) {
    Serial.printf(" scale=Chromatic:%s",
                  ROOT_NAMES[b.scale.root]);
  } else {
    Serial.printf(" scale=%s:%s",
                  ROOT_NAMES[b.scale.root],
                  MODE_NAMES[b.scale.mode]);
  }

  // Octave / mutationLevel
  if (b.type == BANK_ARPEG && b.arpEngine) {
    Serial.printf(" octave=%d", b.arpEngine->getOctaveRange());
  } else if (b.type == BANK_ARPEG_GEN && b.arpEngine) {
    Serial.printf(" mutationLevel=%d", b.arpEngine->getMutationLevel());
  }

  // 8 slots
  bool arpCtx = (b.type == BANK_ARPEG) || (b.type == BANK_ARPEG_GEN) || (b.type == BANK_LOOP);
  const PotMappingStore& m = s_potRouter.getMapping();
  const PotMapping* map = arpCtx ? m.arpegMap : m.normalMap;

  char valBuf[32];
  for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
    PotTarget t = map[i].target;
    formatSlotValue(valBuf, sizeof(valBuf), t);
    Serial.printf(" %s=%s", SLOT_NAMES[i], valBuf);
  }
  Serial.println();
}
#endif
```

Verify field accessors on `BankSlot::scale` match the actual struct. Inspect `KeyboardData.h` for `BankSlot` and `ScaleConfig` field names. Adapt `b.scale.root`, `b.scale.mode`, `b.scale.chromatic` to actual names.

- [ ] **Step 2: Verify field names in `BankSlot::scale`**

Run: `grep -n "struct BankSlot\|struct ScaleConfig\|chromatic\|root\b\|mode\b" src/core/KeyboardData.h | head -20`

If field names differ from `root`, `mode`, `chromatic`, adjust the snippet in step 1 before continuing.

- [ ] **Step 3: Expose `dumpRuntimeState` so BankManager can call it**

Add a forward declaration accessible from `BankManager.cpp`. Two options: (a) put `dumpRuntimeState` declaration in a new tiny header `src/main_helpers.h`, (b) declare `extern void dumpRuntimeState();` directly in `BankManager.cpp`. Option (b) is simpler:

In `src/managers/BankManager.cpp`, near the top after existing includes:

```cpp
#if DEBUG_SERIAL
extern void dumpRuntimeState();
#endif
```

- [ ] **Step 4: Call `dumpRuntimeState()` at end of `setup()` (after `dumpBanksGlobal`)**

Add right after the `dumpBanksGlobal()` call from Task A.3:

```cpp
  #if DEBUG_SERIAL
  dumpBanksGlobal();
  dumpRuntimeState();
  #endif
```

- [ ] **Step 5: Call `dumpRuntimeState()` after the `[BANK]` log in `BankManager::switchBank()`**

In `src/managers/BankManager.cpp`, find the `[BANK]` printf inside `#if DEBUG_SERIAL` block (around line 220):

```cpp
  Serial.printf("[BANK] Bank %d (ch %d, %s)\n",
                _currentBank + 1, _currentBank + 1, typeLabel);
  #endif
```

becomes:

```cpp
  Serial.printf("[BANK] Bank %d (ch %d, %s)\n",
                _currentBank + 1, _currentBank + 1, typeLabel);
  dumpRuntimeState();
  #endif
```

- [ ] **Step 6: Build to verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

Expected: exit code 0, zero new warnings.

- [ ] **Step 7: Upload + HW smoke test**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

Verify at boot, after `[BANKS]` and 8× `[BANK]` lines, **one** `[STATE]` line appears with the current bank's data. Then trigger a bank switch (left button). Verify each `[BANK]` is **immediately followed** by a `[STATE]` line with the new bank's data.

**Pass criteria**:
- Boot: `[STATE]` line present, `bank=N` matches current bank, all 8 slots present, scale field correct, `octave=` or `mutationLevel=` present only when applicable.
- Bank switch: `[BANK]` then `[STATE]` on every switch, no other event interleaved between them.
- Switching from NORMAL to ARPEG bank: 8 slot targets in `[STATE]` change (arpegMap activated).

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp src/managers/BankManager.cpp src/managers/BankManager.h
git commit -m "feat(viewer-api): add [STATE] dump at boot + bank switch

Emits one atomic line with the foreground bank's full state: type, ch,
scale, octave/mutationLevel, and the 8 slot mappings with their current
values. Triggered once at boot (after [BANKS]) and after each [BANK]
switch event. Allows the JUCE viewer to refresh its current-bank panel
on every switch without waiting for incremental pot events."
```

---

### Task A.5: Add runtime request-state handler

Polls `Serial.available()` in the main loop, accumulates a line until `\n`, recognizes 3 commands (`?STATE`, `?BANKS`, `?BOTH`), emits the corresponding dump. Idempotent. ~25 lines total.

**Files:**
- Modify: `src/main.cpp` (add handler + call from `loop()`)

- [ ] **Step 1: Add `pollRuntimeCommands()` function**

Insert near `dumpRuntimeState()`:

```cpp
#if DEBUG_SERIAL
static void pollRuntimeCommands() {
  static char  cmdBuf[16];
  static uint8_t cmdLen = 0;

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      cmdBuf[cmdLen] = '\0';
      if (strcmp(cmdBuf, "?STATE") == 0) {
        dumpRuntimeState();
      } else if (strcmp(cmdBuf, "?BANKS") == 0) {
        dumpBanksGlobal();
      } else if (strcmp(cmdBuf, "?BOTH") == 0) {
        dumpBanksGlobal();
        dumpRuntimeState();
      }
      cmdLen = 0;
    } else if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    } else {
      // overflow — discard buffer
      cmdLen = 0;
    }
  }
}
#endif
```

- [ ] **Step 2: Call `pollRuntimeCommands()` from `loop()`**

Find the main `void loop()` in `main.cpp`. Add a call near the top, before other work (the handler is non-blocking and cheap):

```cpp
void loop() {
  #if DEBUG_SERIAL
  pollRuntimeCommands();
  #endif
  // ... existing loop body
}
```

- [ ] **Step 3: Build to verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

Expected: exit code 0, zero new warnings.

- [ ] **Step 4: Upload + HW smoke test**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

After boot completes, type each command in the monitor (most pio monitors accept typing with Enter to send):
- Type `?STATE` + Enter → expect `[STATE] ...` line.
- Type `?BANKS` + Enter → expect `[BANKS] count=8` + 8× `[BANK]` lines.
- Type `?BOTH` + Enter → expect `[BANKS]` + 8× `[BANK]` + `[STATE]`.
- Type a non-command (`?FOO`) → expect no response.
- Type partial then garbage (`?BOT then garbage \n`) → expect no response (line too long or unrecognized).

**Pass criteria**: each valid command produces the expected dump immediately. No false trigger from garbage input. Setup mode entered at boot still works (no conflict with `InputParser` — setup mode is boot-only, runtime command handler not invoked then).

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat(viewer-api): add runtime ?STATE / ?BANKS / ?BOTH handler

Non-blocking poll of Serial in loop(). Recognizes 3 idempotent commands
to re-emit the boot dumps on demand. Allows the JUCE viewer to fully
populate its UI when it connects to an already-booted ILLPAD, or to
manually resync via UI button. Boot-only setup mode unaffected
(InputParser still claims the serial during setup)."
```

---

### Task A.6: End-to-end smoke test

Full validation of the viewer API extensions before declaring Phase A complete.

- [ ] **Step 1: Run pio device monitor and walk through full scenario**

```bash
~/.platformio/penv/bin/pio device monitor -b 115200
```

Validate:

1. Boot: `[INIT] Ready.` followed by `[BANKS] count=8`, 8× `[BANK]`, 1× `[STATE]`. All formats per spec §8 §9.
2. Switch bank via left button: `[BANK]` followed by `[STATE]` on every switch.
3. Turn each pot in NORMAL bank: each `[POT]` line annotated with correct slot (R1/R1H/R2/R2H/R3/R3H/R4/R4H).
4. Switch to ARPEG bank: `[STATE]` shows the arpegMap slots, different from normalMap.
5. Turn pots in ARPEG bank: `[POT]` annotated with the arpegMap context slots.
6. Switch to ARPEG_GEN bank if configured: `[STATE]` includes `mutationLevel=` field, no `octave=`.
7. Type `?STATE` in monitor → re-emit current `[STATE]`.
8. Type `?BANKS` → re-emit `[BANKS] count=8` + 8× `[BANK]`.
9. Type `?BOTH` → emit `[BANKS]` + 8× `[BANK]` + `[STATE]`.
10. Triple-click rear button → `[PANIC]` line (unchanged from before).
11. Press scale pad on current bank → `[SCALE]` line (unchanged).
12. Reboot ILLPAD in setup mode (hold rear button at boot) → setup mode enters, no `[STATE]` or `[BANKS]` corruption (the dumps are gated by `#if DEBUG_SERIAL` but the setup parser intercepts the serial — verify no command leaks into setup keyboard input).

**Pass criteria**: every step matches spec. No surprises. No regression on existing logs.

- [ ] **Step 2: Verify build size impact**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | tail -10`

Inspect `RAM:` and `Flash:` line. Expected: minor increase (a few hundred bytes flash, ~100 bytes RAM for static buffers). No alarming jump.

- [ ] **Step 3: Final commit (none if previous tasks all committed)**

Verify `git status` is clean. If any unstaged changes from intermediate fixes, commit them now:

```bash
git status
git diff
# only commit if necessary
```

---

## Out of scope for this plan

- Sync of firmware ColorSlot → JUCE viewer palette (spec §25 — V1 uses fixed app palette).
- Tick synchronization (spec §26 — V1 tick is JUCE-local, non-synced).
- Any other firmware modification beyond the 4 features above. Per spec §5, "porte ouverte si V3 le nécessite" — escalate to a new spec entry if a real need surfaces during JUCE viewer integration.

---

**End of plan.**
