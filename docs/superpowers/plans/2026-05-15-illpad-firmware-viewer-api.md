# ILLPAD Firmware — Viewer API Extensions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the ILLPAD firmware serial protocol with annotation, atomic state dumps, and a runtime request-state handler — to feed the new JUCE Viewer app with all the data it needs to populate its UI without ambiguity or waiting for incremental events.

**Architecture:** Targeted modifications to existing files, no refactoring of the runtime, no NVS bump, no new struct. All modifications gated by existing `#if DEBUG_SERIAL` (active by default). Each modification independently testable via `pio device monitor` before integrating the JUCE viewer.

**Tech Stack:** C++17, Arduino framework, PlatformIO, ESP32-S3 (`esp32-s3-devkitc-1` environment).

**Spec reference:** [`docs/superpowers/specs/2026-05-15-illpad-viewer-design.md`](../specs/2026-05-15-illpad-viewer-design.md) §5-§11.

**Design decisions (refonte 2026-05-15)** — actées suite à HW test A.2 et cross-audit logique viewer :

1. **Tagging philosophy** — approche B (parser-defined boundary). Le firmware garde ses tags existants `[INIT]`/`[KB]`/`[NVS]`/`[POT] Seed`/etc. ; le viewer parse seulement ce qu'il connaît et ignore le reste comme `UnknownEvent`. Pas de rename des emits existants.

2. **8× `[STATE]` au boot, pas 1×** — le viewer doit pouvoir afficher les 8 banks sans avoir à les switcher physiquement. Une fonction `dumpBankState(uint8_t idx)` lit les valeurs per-bank depuis `s_banks[N]` et `s_banks[N].arpEngine` (pas via PotRouter qui ne tracke que le foreground). Itérée 8× au boot + appelée 1× après chaque bank switch.

3. **Couverture complète des 17 PotTargets dans `[STATE]`** — ajout de `TARGET_MIDI_CC` (format `R1=CC74:42`) et `TARGET_MIDI_PITCHBEND` (format `R1=PB:8192`). Pour les MIDI CC sur banks non-courants, valeur affichée `:?` (pas de binding actif → pas de _ccValue lisible).

4. **`[POT]` tweaks couvrent aussi MIDI CC/PB** — ajout dans la boucle `consumeCC`/`consumePitchBend` de `main.cpp` d'un `Serial.printf` annoté slot (nouveau accessor `getSlotForCcNumber`). Sans ça, les tweaks de pots mappés MIDI CC sont silencieux côté viewer.

5. **`--:` toléré dans les `[POT]` lines** — pass criteria de A.2 relaxé : `--:` apparaît légitimement pour les targets non présents dans le user mapping du contexte courant (params globaux pilotés par binding fixe, ou targets arp affichées hors contexte ARPEG). Le viewer doit gérer `--:` comme "param global / hors-slot" plutôt que rejeter la ligne.

6. **Marker `[READY] current=N`** — émis une fois après le dump boot complet (`[BANKS]` + 8× `[BANK]` + 8× `[STATE]`), et à la fin de chaque réponse `?STATE`/`?BANKS`/`?BOTH`. Le viewer attend `[READY] current=N` pour dropper son loading. Si non reçu dans ~2-3 s après détection mode Runtime, le viewer pousse `?BOTH` (cas reconnect tardif).

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
| `src/managers/PotRouter.h` | Pot routing API | Add public `getSlotForTarget` + `getSlotForCcNumber` getters |
| `src/managers/PotRouter.cpp` | Pot routing impl | Implement both reverse-lookups (over `_mapping`) |
| `src/arp/ArpEngine.h` | Arp engine accessor | Add public `getOctaveRange()` const accessor |
| `src/main.cpp` | App entry + runtime loop | Annotate 15× `[POT]` internal-target lines + log MIDI CC/PB tweaks ; add `dumpBanksGlobal()`, `dumpBankState(idx)`, `pollRuntimeCommands()` ; emit `[READY] current=N` at end of boot + at end of each dump |
| `src/managers/BankManager.cpp` | Bank switch | Call `dumpBankState(currentBank)` after `[BANK]` log |

---

### Task A.1: Add `PotRouter::getSlotForTarget` accessor — **DONE 2026-05-15 (commit 3b25c07)**

Required by all subsequent tasks. Reverse-lookup in the user's saved mapping returns the slot index (0..7) that routes to a given `PotTarget` in a given context. Slot layout per `KeyboardData.h:566`: `[0]=R1 alone, [1]=R1+hold, [2]=R2 alone, [3]=R2+hold, [4]=R3 alone, [5]=R3+hold, [6]=R4 alone, [7]=R4+hold`. Returns `0xFF` if target not found in the context.

> **Companion accessor `getSlotForCcNumber`** : moved into Task A.2 (coupled with the MIDI CC tweak logging extension). When multiple slots map to `TARGET_MIDI_CC`, `getSlotForTarget` matches the first one — insufficient to identify which slot drove the CC. `getSlotForCcNumber(uint8_t cc, bool isArpContext)` disambiguates by ccNumber.

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

### Task A.2: Annotate `[POT]` log lines + cover MIDI CC/PB tweaks

Two coupled sub-jobs, single commit :

**A.2.a — Internal targets (15 lines)** : annotate every `Serial.printf("[POT] ...")` line in `main.cpp::debugOutput()` with the slot of origin (`R1`/`R1H`/`R2`/`R2H`/`R3`/`R3H`/`R4`/`R4H`), looked up via `PotRouter::getSlotForTarget` against the user mapping in the current context.

**A.2.b — MIDI CC/PB out tweaks (new emit)** : the `consumeCC`/`consumePitchBend` loop in `main.cpp` currently sends MIDI CC out without any `Serial.printf`. Add a `[POT]` log there so the viewer is notified when the user tweaks a pot mapped to `TARGET_MIDI_CC` or `TARGET_MIDI_PITCHBEND`. Requires a new `PotRouter::getSlotForCcNumber(uint8_t cc, bool isArpContext)` accessor that disambiguates by CC number (multiple slots may map `TARGET_MIDI_CC` with different `ccNumber`).

**Files:**
- Modify: `src/managers/PotRouter.h` (add `getSlotForCcNumber` declaration)
- Modify: `src/managers/PotRouter.cpp` (implement `getSlotForCcNumber`)
- Modify: `src/main.cpp` — `debugOutput` body (15 internal lines) + `consumeCC`/`consumePitchBend` block in `loop()` (new CC/PB emits)

- [ ] **Step 1: Add `potSlotName` helper at file scope in `main.cpp`**

Find a place after the static globals (e.g. just before `sensingTask`, around line 90). Add:

```cpp
// Slot-name lookup for viewer-API [POT] log annotation.
static const char* potSlotName(uint8_t slot) {
  static const char* NAMES[POT_MAPPING_SLOTS] = {
    "R1", "R1H", "R2", "R2H", "R3", "R3H", "R4", "R4H"
  };
  return slot < POT_MAPPING_SLOTS ? NAMES[slot] : "--";
}
```

This is file-scope (free function), not nested inside `debugOutput` — C++ does not allow nested named functions with their own `static` data.

- [ ] **Step 2: Add `getSlotForCcNumber` to `PotRouter`**

Declaration in `src/managers/PotRouter.h`, just after `getSlotForTarget` (added by Task A.1) :

```cpp
  // Reverse lookup for MIDI CC slots : returns the slot index (0..7) in the
  // given context whose target == TARGET_MIDI_CC AND ccNumber == cc, or
  // 0xFF if not found. Needed by [POT] log annotation when the user maps
  // multiple slots to MIDI CC with different CC numbers.
  uint8_t getSlotForCcNumber(uint8_t cc, bool isArpContext) const;
```

Implementation in `PotRouter.cpp`, right after `getSlotForTarget` :

```cpp
uint8_t PotRouter::getSlotForCcNumber(uint8_t cc, bool isArpContext) const {
  const PotMapping* map = isArpContext ? _mapping.arpegMap : _mapping.normalMap;
  for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
    if (map[i].target == TARGET_MIDI_CC && map[i].ccNumber == cc) return i;
  }
  return 0xFF;
}
```

- [ ] **Step 3: Determine current context inside `debugOutput`**

Inside the `#if DEBUG_SERIAL` block, just after the local static cache variables (around line 864), add:

```cpp
    // Derive current context (NORMAL mapping vs ARPEG mapping) from the
    // foreground bank type. `isArpType()` from KeyboardData.h excludes
    // BANK_LOOP (LOOP banks have no pot mapping at runtime — cf. PotRouter
    // rebuildBindings which does not create bindings for LOOP).
    BankType curType = s_banks[s_bankManager.getCurrentBank()].type;
    bool arpCtx = isArpType(curType);
```

- [ ] **Step 4: Rewrite each `[POT]` printf in `debugOutput()` to include the slot**

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

- [ ] **Step 5: Add `[POT]` log emit for MIDI CC and PB tweaks in `loop()`**

Today `main.cpp` (around line 847) consumes CC/PB from PotRouter and dispatches to MIDI transport silently. Add `Serial.printf` so the viewer sees these tweaks too. The CC slot is identified via `getSlotForCcNumber` ; the PB slot via `getSlotForTarget(TARGET_MIDI_PITCHBEND, ...)`.

Replace :

```cpp
  uint8_t ccNum, ccVal;
  while (s_potRouter.consumeCC(ccNum, ccVal)) {
    s_transport.sendCC(potSlot.channel, ccNum, ccVal);
  }
  uint16_t pbVal;
  if (s_potRouter.consumePitchBend(pbVal)) {
    s_transport.sendPitchBend(potSlot.channel, pbVal);
  }
```

by :

```cpp
  // Context for slot lookup (foreground bank).
  BankType ccCurType = s_banks[s_bankManager.getCurrentBank()].type;
  bool ccArpCtx = isArpType(ccCurType);

  uint8_t ccNum, ccVal;
  while (s_potRouter.consumeCC(ccNum, ccVal)) {
    s_transport.sendCC(potSlot.channel, ccNum, ccVal);
    #if DEBUG_SERIAL
    Serial.printf("[POT] %s: CC%u=%u\n",
                  potSlotName(s_potRouter.getSlotForCcNumber(ccNum, ccArpCtx)),
                  ccNum, ccVal);
    #endif
  }
  uint16_t pbVal;
  if (s_potRouter.consumePitchBend(pbVal)) {
    s_transport.sendPitchBend(potSlot.channel, pbVal);
    #if DEBUG_SERIAL
    Serial.printf("[POT] %s: PB=%u\n",
                  potSlotName(s_potRouter.getSlotForTarget(TARGET_MIDI_PITCHBEND, ccArpCtx)),
                  pbVal);
    #endif
  }
```

Note: `ccArpCtx` is computed locally (separate from `arpCtx` of `debugOutput`) because the CC/PB loop is in `loop()` outside `debugOutput`. The two locals can have the same name but live in different scopes.

- [ ] **Step 6: Build to verify zero new warnings**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

Expected: exit code 0, no `warning:` in output.

- [ ] **Step 7: Upload + HW smoke test**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

After boot, turn each of R1, R2, R3, R4 (alone and with rear-left button held). Verify each pot move emits a line of the form `[POT] R1: Tempo=120 BPM`, with the right slot name reflecting the current mapping and context.

**Pass criteria (relaxed vs initial plan)**:
- Each `[POT]` line is well-formed `[POT] <slot>: <target>=<value>`.
- Slot prefix is one of `R1:` / `R1H:` / `R2:` / `R2H:` / `R3:` / `R3H:` / `R4:` / `R4H:` for targets present in the user mapping of the current context.
- `--:` is acceptable for : (a) global params (`Shape`, `Slew`, `AT_Deadzone`, `LED_Bright`, `PadSens`) when not mapped to any slot in the current context ; (b) arp params (`Gate`, `ShufDepth`, `Division`, `Pattern`, `ShufTpl`, `GenPos`) when observed in a NORMAL context where they are not mapped ; (c) the boot dump's initial change-detect emit when sentinel `s_dbg*` first compares against the loaded value.
- Slot name changes when switching from a NORMAL bank to an ARPEG / ARPEG_GEN bank (different mapping context).
- If a slot is mapped to `TARGET_MIDI_CC` with `ccNumber=74`, tweaking it emits `[POT] R1: CC74=42` (not silent).

- [ ] **Step 8: Commit**

```bash
git add src/managers/PotRouter.h src/managers/PotRouter.cpp src/main.cpp
git commit -m "feat(viewer-api): annotate [POT] logs + cover MIDI CC/PB tweaks

Each [POT] line now prefixed with the slot name (R1/R1H/R2/.../R4H) derived
from the user mapping in the current context (NORMAL or ARPEG). New
PotRouter::getSlotForCcNumber accessor disambiguates among multiple
TARGET_MIDI_CC slots by CC number.

Additionally, the consumeCC/consumePitchBend loop in main.cpp now emits a
[POT] log for each MIDI CC/PB tweak — previously silent, leaving the
viewer blind to pots mapped to MIDI CC. Format: '[POT] R1: CC74=42' and
'[POT] R1: PB=8192'.

--: prefix is tolerated when a target has no slot in the current context
mapping (global params, or arp params on NORMAL banks) — viewer treats
these as 'global / off-grid'."
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
  // ORDER MATCHES enum BankType in KeyboardData.h:324-329
  // BANK_NORMAL=0, BANK_ARPEG=1, BANK_LOOP=2, BANK_ARPEG_GEN=3
  static const char* TYPE_NAMES[] = { "NORMAL", "ARPEG", "LOOP", "ARPEG_GEN" };
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

Note: requires `ArpEngine::getDivision()`, `getOctaveRange()`, `getMutationLevel()` accessors. Existing state: `getDivision()` and `getMutationLevel()` exist in `src/arp/ArpEngine.h`. **`getOctaveRange()` does NOT exist** — must be added (cf. Step 2 below).

- [ ] **Step 2: Add `getOctaveRange()` const accessor to `ArpEngine.h`**

Open `src/arp/ArpEngine.h`. Find the block of getters around lines 127-141 (where `getDivision()` and `getMutationLevel()` are declared). Add:

```cpp
  uint8_t getOctaveRange() const { return _octaveRange; }
```

The private field `_octaveRange` already exists (around line 147). This is purely a read-only public accessor.

Verify all three exist after the edit:

```
grep -n "getDivision\|getOctaveRange\|getMutationLevel" src/arp/ArpEngine.h
```

Expected: all three accessors listed.

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

### Task A.4: Add `dumpBankState(idx)` emitting `[STATE]` for any bank — iterated 8× at boot + 1× per bank switch

Refonte 2026-05-15 : la fonction est factorisée par **bank index** (au lieu de "current bank uniquement"). Au boot, on l'appelle 8 fois pour que le viewer reçoive l'état complet des 8 banks sans devoir physiquement les switcher. Après un bank switch, on l'appelle une fois pour le bank courant (les autres n'ont pas changé). Le `formatTargetValueForBank` helper lit les params **per-bank** depuis `s_banks[N]` / `s_banks[N].arpEngine` (pas via `s_potRouter` qui ne tracke que le foreground bank), et les params **globaux** depuis `s_potRouter`.

**Couverture complète des 17 PotTargets** (cf. enum `KeyboardData.h:522-554`) — ajouts par rapport au plan initial : `TARGET_MIDI_CC` (format `CC74:42`) et `TARGET_MIDI_PITCHBEND` (format `PB:8192`). Pour MIDI CC sur un bank **non-courant**, la valeur instantanée `_ccValue[]` du PotRouter ne reflète que le foreground bank — on émet `CC74:?` pour signaler "valeur indéterminée hors foreground". Pour le foreground bank, on lit `_ccValue[binding_idx]` qui est valide.

Format émis pour chaque bank :
```
[STATE] bank=N mode=X ch=N scale=ROOT:MODE [octave=N|mutationLevel=N] R1=X:Y R1H=X:Y ... R4H=X:Y
```

**Files:**
- Modify: `src/main.cpp` (factor `dumpBankState(idx)` + helper + iteration loop at boot)
- Modify: `src/managers/BankManager.cpp` (extern + call `dumpBankState(currentBank)` after `[BANK]` log)

- [ ] **Step 1: Add `formatTargetValueForBank` and `dumpBankState` functions in `main.cpp`**

Insert right after `dumpBanksGlobal()`. The helper takes a bank index and writes the formatted target:value into a caller-supplied buffer. Source of truth per target :
- **Global** : `s_potRouter.getX()` (Shape, Slew, AT_Deadzone, Tempo, LED_Bright, PadSens)
- **Per-bank static** : `s_banks[N].baseVelocity`/`velocityVariation`/`pitchBendOffset`
- **Per-bank arp** : `s_banks[N].arpEngine->getX()` (Division, Pattern, Gate, ShufDepth, ShufTpl, GenPos)
- **MIDI CC/PB current value** : `s_potRouter._ccValue[]` is only valid for foreground bank ; non-foreground banks emit `:?`

```cpp
#if DEBUG_SERIAL
// Format the "target:value" string for a given target, reading from the
// per-bank storage (s_banks[bankIdx] / arpEngine) for per-bank values, or
// from PotRouter for global values. `isForeground` selects between
// instantaneous PotRouter::_ccValue[] (foreground) and "?" (other banks).
static void formatTargetValueForBank(char* buf, size_t bufSize,
                                     PotTarget t, uint8_t bankIdx,
                                     uint8_t mappingCcNumber,
                                     bool isForeground) {
  static const char* DIV_NAMES[] = { "4/1","2/1","1/1","1/2","1/4","1/8","1/16","1/32","1/64" };
  static const char* PAT_NAMES[] = { "Up","Down","UpDown","Order","PedalUp","Converge" };

  BankSlot& b = s_banks[bankIdx];
  ArpEngine* eng = b.arpEngine;

  switch (t) {
    // --- Global params (PotRouter is the single source of truth) ---
    case TARGET_RESPONSE_SHAPE:
      snprintf(buf, bufSize, "Shape:%.2f", s_potRouter.getResponseShape()); break;
    case TARGET_SLEW_RATE:
      snprintf(buf, bufSize, "Slew:%u", s_potRouter.getSlewRate()); break;
    case TARGET_AT_DEADZONE:
      // Name MUST match the [POT] log table (Task A.2) — single canonical
      // target name across [POT] and [STATE] (cross-audit 2026-05-15 R1).
      snprintf(buf, bufSize, "AT_Deadzone:%u", s_potRouter.getAtDeadzone()); break;
    case TARGET_TEMPO_BPM:
      snprintf(buf, bufSize, "Tempo:%u", s_potRouter.getTempoBPM()); break;
    case TARGET_LED_BRIGHTNESS:
      snprintf(buf, bufSize, "LED_Bright:%u", s_potRouter.getLedBrightness()); break;
    case TARGET_PAD_SENSITIVITY:
      snprintf(buf, bufSize, "PadSens:%u", s_potRouter.getPadSensitivity()); break;

    // --- Per-bank static params (stored in BankSlot directly) ---
    case TARGET_BASE_VELOCITY:
      snprintf(buf, bufSize, "BaseVel:%u", b.baseVelocity); break;
    case TARGET_VELOCITY_VARIATION:
      snprintf(buf, bufSize, "VelVar:%u", b.velocityVariation); break;
    case TARGET_PITCH_BEND:
      snprintf(buf, bufSize, "PitchBend:%u", b.pitchBendOffset); break;

    // --- Per-bank arp params (stored in ArpEngine) ---
    case TARGET_GATE_LENGTH:
      if (eng) snprintf(buf, bufSize, "Gate:%.2f", eng->getGateLength());
      else     snprintf(buf, bufSize, "Gate:-");
      break;
    case TARGET_SHUFFLE_DEPTH:
      if (eng) snprintf(buf, bufSize, "ShufDepth:%.2f", eng->getShuffleDepth());
      else     snprintf(buf, bufSize, "ShufDepth:-");
      break;
    case TARGET_DIVISION: {
      if (eng) {
        uint8_t d = (uint8_t)eng->getDivision();
        snprintf(buf, bufSize, "Division:%s", d < 9 ? DIV_NAMES[d] : "?");
      } else {
        snprintf(buf, bufSize, "Division:-");
      }
      break;
    }
    case TARGET_PATTERN: {
      if (eng) {
        uint8_t p = (uint8_t)eng->getPattern();
        snprintf(buf, bufSize, "Pattern:%s", p < 6 ? PAT_NAMES[p] : "?");
      } else {
        snprintf(buf, bufSize, "Pattern:-");
      }
      break;
    }
    case TARGET_GEN_POSITION:
      if (eng) snprintf(buf, bufSize, "GenPos:%u", eng->getGenPosition());
      else     snprintf(buf, bufSize, "GenPos:-");
      break;
    case TARGET_SHUFFLE_TEMPLATE:
      if (eng) snprintf(buf, bufSize, "ShufTpl:%u", eng->getShuffleTemplate());
      else     snprintf(buf, bufSize, "ShufTpl:-");
      break;

    // --- MIDI CC / PB out (mapping-driven) ---
    // The current value lives in PotRouter::_ccValue[binding_idx], but
    // bindings only exist for the foreground bank. For non-foreground banks
    // we emit ':?' (value not observable without changing the runtime).
    case TARGET_MIDI_CC: {
      if (isForeground) {
        // Foreground : query PotRouter for the current value of this CC#.
        // Since no public accessor exists for per-CC value, we expose the
        // mapping ccNumber + a generic value lookup. As a V1 simplification,
        // emit ':?' here too — V2 may add `PotRouter::getCcValue(uint8_t cc)`
        // to give the live value. For now, the viewer learns the live value
        // from [POT] CC tweak events (Task A.2.b).
        snprintf(buf, bufSize, "CC%u:?", mappingCcNumber);
      } else {
        snprintf(buf, bufSize, "CC%u:?", mappingCcNumber);
      }
      break;
    }
    case TARGET_MIDI_PITCHBEND:
      // Same rationale as MIDI_CC : V1 simplification, viewer hydrates from
      // [POT] PB tweak events.
      snprintf(buf, bufSize, "PB:?"); break;

    case TARGET_EMPTY:
    case TARGET_NONE:
    default:
      snprintf(buf, bufSize, "---"); break;
  }
}

// Dump the [STATE] line for a specific bank (any bank, not just foreground).
// At boot, called 8× for full hydration. After bank switch, called 1× for
// the new foreground bank.
static void dumpBankState(uint8_t bankIdx) {
  // ORDER MATCHES enum BankType in KeyboardData.h:324-329
  // BANK_NORMAL=0, BANK_ARPEG=1, BANK_LOOP=2, BANK_ARPEG_GEN=3
  static const char* TYPE_NAMES[] = { "NORMAL", "ARPEG", "LOOP", "ARPEG_GEN" };
  static const char* SLOT_NAMES[] = { "R1","R1H","R2","R2H","R3","R3H","R4","R4H" };
  // ROOT_NAMES / MODE_NAMES MUST mirror src/managers/ScaleManager.cpp:9-13
  // and src/core/KeyboardData.h:338-342. Diatonic, 7 roots A..G, 7 modes.
  static const char* ROOT_NAMES[] = { "A","B","C","D","E","F","G" };
  static const char* MODE_NAMES[] = {
    "Ionian","Dorian","Phrygian","Lydian","Mixolydian","Aeolian","Locrian"
  };

  if (bankIdx >= NUM_BANKS) return;
  BankSlot& b = s_banks[bankIdx];
  uint8_t typeIdx = (uint8_t)b.type;
  const char* typeName = (typeIdx < 4) ? TYPE_NAMES[typeIdx] : "?";
  bool isForeground = (bankIdx == s_bankManager.getCurrentBank());

  Serial.printf("[STATE] bank=%d mode=%s ch=%d",
                bankIdx + 1, typeName, bankIdx + 1);

  // Scale
  if (b.scale.chromatic) {
    Serial.printf(" scale=Chromatic:%s",
                  b.scale.root < 7 ? ROOT_NAMES[b.scale.root] : "?");
  } else {
    Serial.printf(" scale=%s:%s",
                  b.scale.root < 7 ? ROOT_NAMES[b.scale.root] : "?",
                  b.scale.mode < 7 ? MODE_NAMES[b.scale.mode] : "?");
  }

  // Octave / mutationLevel (header-level fields, mirror [BANK] dump)
  if (b.type == BANK_ARPEG && b.arpEngine) {
    Serial.printf(" octave=%d", b.arpEngine->getOctaveRange());
  } else if (b.type == BANK_ARPEG_GEN && b.arpEngine) {
    Serial.printf(" mutationLevel=%d", b.arpEngine->getMutationLevel());
  }

  // 8 slots
  // BANK_LOOP has no PotRouter binding at runtime (cf. PotRouter::rebuildBindings
  // creates bindings only for NORMAL, ARPEG, ARPEG_GEN). Emit "---" everywhere.
  if (b.type == BANK_LOOP) {
    for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
      Serial.printf(" %s=---", SLOT_NAMES[i]);
    }
    Serial.println();
    return;
  }

  // ARPEG and ARPEG_GEN both use arpegMap ; NORMAL uses normalMap.
  bool arpCtx = isArpType(b.type);
  const PotMappingStore& m = s_potRouter.getMapping();
  const PotMapping* map = arpCtx ? m.arpegMap : m.normalMap;

  char valBuf[32];
  for (uint8_t i = 0; i < POT_MAPPING_SLOTS; i++) {
    PotTarget t = map[i].target;
    uint8_t cc  = map[i].ccNumber;
    formatTargetValueForBank(valBuf, sizeof(valBuf), t, bankIdx, cc, isForeground);
    Serial.printf(" %s=%s", SLOT_NAMES[i], valBuf);
  }
  Serial.println();
}
#endif
```

Verify field accessors on `BankSlot::scale` match the actual struct. Inspect `KeyboardData.h` for `BankSlot` and `ScaleConfig` field names. Field names verified 2026-05-15 : `chromatic`, `root`, `mode` (cf. `KeyboardData.h:338-342`).

- [ ] **Step 2: Expose `dumpBankState` so BankManager can call it**

In `src/managers/BankManager.cpp`, near the top after existing includes :

```cpp
#if DEBUG_SERIAL
extern void dumpBankState(uint8_t bankIdx);
#endif
```

- [ ] **Step 3: Iterate `dumpBankState` 8× at end of `setup()` (after `dumpBanksGlobal`)**

Replace the boot dump block from Task A.3 to iterate over all banks :

```cpp
  Serial.println("[INIT] Ready.");
  #if DEBUG_SERIAL
  dumpBanksGlobal();
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    dumpBankState(i);
  }
  #endif
```

Total boot dump : 1× `[BANKS] count=8` + 8× `[BANK]` + 8× `[STATE]` = 17 lines.

- [ ] **Step 4: Call `dumpBankState(currentBank)` after the `[BANK]` log in `BankManager::switchBank()`**

In `src/managers/BankManager.cpp`, find the `[BANK]` printf inside `#if DEBUG_SERIAL` block (around line 220) :

```cpp
  Serial.printf("[BANK] Bank %d (ch %d, %s)\n",
                _currentBank + 1, _currentBank + 1, typeLabel);
  dumpBankState(_currentBank);
  #endif
```

- [ ] **Step 5: Build to verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`

Expected: exit code 0, zero new warnings.

- [ ] **Step 6: Upload + HW smoke test**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

Verify at boot, after `[BANKS]` and 8× `[BANK]` lines, **8** `[STATE]` lines appear (one per bank, in order). Then trigger a bank switch (left button). Verify each `[BANK]` is **immediately followed** by **one** `[STATE]` line with the new bank's data.

**Pass criteria**:
- Boot: exactly 8 `[STATE]` lines after `[BANKS]`+8×`[BANK]`. Each has `bank=N` matching its position (1..8), all 8 slots present, scale field correct, `octave=`/`mutationLevel=` present only when applicable per type.
- ARPEG banks show `octave=N` ; ARPEG_GEN banks show `mutationLevel=N` ; NORMAL banks show neither ; LOOP banks emit all 8 slots as `---`.
- Per-bank values are read from the right source : `BaseVel` matches `s_banks[N].baseVelocity`, `Gate` matches `s_banks[N].arpEngine->getGateLength()`, etc. (verify by tweaking a pot on bank 2, then noting that bank 2's `[STATE]` reflects the change while banks 1,3..8 do not).
- Bank switch: `[BANK]` then exactly **one** `[STATE]` (not 8), no other event interleaved between them.
- Switching from NORMAL to ARPEG bank : 8 slot targets in `[STATE]` change (arpegMap activated).
- For slots mapped to `TARGET_MIDI_CC`, the slot value is `CCnn:?` (V1 simplification, live value comes via `[POT]` CC events).

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp src/managers/BankManager.cpp src/arp/ArpEngine.h
git commit -m "feat(viewer-api): add [STATE] dump for all 8 banks at boot + per switch

dumpBankState(idx) reads per-bank values from s_banks[N] / arpEngine
directly (not via PotRouter, which tracks foreground only) so it can
emit a complete [STATE] line for any bank. Boot dumps 8× (full
hydration of the viewer); bank switch dumps 1× (current bank only).

Covers the 17 PotTargets including MIDI_CC (format CC74:?) and
MIDI_PITCHBEND (format PB:?) — the live value of CC/PB is delivered to
the viewer via [POT] CC tweak events (Task A.2.b) rather than via
[STATE] for V1 (no per-CC live accessor in PotRouter)."
```

---

### Task A.5: Add runtime request-state handler + `[READY] current=N` marker emission

Polls `Serial.available()` in the main loop, accumulates a line until `\n`, recognizes 3 commands (`?STATE`, `?BANKS`, `?BOTH`), emits the corresponding dump, and finishes each dump with a single `[READY] current=N` line so the viewer knows the dump is complete. The same `[READY] current=N` is also emitted once at the end of `setup()` after the boot dump (covered in this task's Step 3).

**Files:**
- Modify: `src/main.cpp` (add handler + call from `loop()` + boot-completion `[READY] current=N`)

- [ ] **Step 1: Add `pollRuntimeCommands()` function**

Insert near `dumpBankState()`. Each command path ends with `emitReady()` which writes `[READY] current=N` (N = current bank, 1..8). The `current=N` field tells the viewer which of the 8 `[STATE]` lines in a `?BOTH` response represents the foreground bank. `?STATE` defaults to the current foreground bank (single line) ; `?BOTH` dumps everything (banks + 8 states + ready).

```cpp
#if DEBUG_SERIAL
// Emit a [READY] line, always carrying the current bank index so the viewer
// knows which of the 8 [STATE]s in a ?BOTH/boot dump refers to the foreground.
// Format: "[READY] current=N" (N in 1..8). The viewer parses the suffix and
// uses it to set Model.current.idx (which is otherwise not deducible from
// the [STATE] sequence alone).
static void emitReady() {
  Serial.printf("[READY] current=%u\n", s_bankManager.getCurrentBank() + 1);
}

static void pollRuntimeCommands() {
  static char  cmdBuf[16];
  static uint8_t cmdLen = 0;

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      cmdBuf[cmdLen] = '\0';
      if (strcmp(cmdBuf, "?STATE") == 0) {
        dumpBankState(s_bankManager.getCurrentBank());
        emitReady();
      } else if (strcmp(cmdBuf, "?BANKS") == 0) {
        dumpBanksGlobal();
        emitReady();
      } else if (strcmp(cmdBuf, "?BOTH") == 0) {
        dumpBanksGlobal();
        for (uint8_t i = 0; i < NUM_BANKS; i++) dumpBankState(i);
        emitReady();
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

- [ ] **Step 3: Emit `[READY] current=N` once at the end of `setup()` after the boot dump**

Extend the boot dump block from Task A.4 step 3 to terminate with `emitReady()` (which writes `[READY] current=N`) :

```cpp
  Serial.println("[INIT] Ready.");
  #if DEBUG_SERIAL
  dumpBanksGlobal();
  for (uint8_t i = 0; i < NUM_BANKS; i++) dumpBankState(i);
  emitReady();
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

Boot expectation : after `[INIT] Ready.`, observe `[BANKS] count=8` + 8× `[BANK]` + 8× `[STATE]` + `[READY] current=N` (in this exact order).

Then test commands. Most pio monitors accept typing followed by Enter to send :
- Type `?STATE` + Enter → expect 1× `[STATE]` then `[READY] current=N`.
- Type `?BANKS` + Enter → expect `[BANKS] count=8` + 8× `[BANK]` then `[READY] current=N`.
- Type `?BOTH` + Enter → expect `[BANKS]` + 8× `[BANK]` + 8× `[STATE]` then `[READY] current=N`.
- Type a non-command (`?FOO`) → expect no response (no `[READY] current=N` either).
- Type partial then garbage (`?BOT then garbage \n`) → expect no response.

**Pass criteria**:
- Boot sequence ends with exactly one `[READY] current=N` after the 8× `[STATE]`.
- Each valid command produces the expected dump immediately, terminated by exactly one `[READY] current=N`.
- Invalid commands produce no output (no `[READY] current=N`).
- Setup mode at boot still works (`InputParser` still claims the serial during setup — `pollRuntimeCommands` is in `loop()` which is gated by the runtime path, not the setup path).

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat(viewer-api): runtime ?STATE/?BANKS/?BOTH handler + [READY] marker

Non-blocking Serial poll in loop(). Recognizes 3 idempotent commands and
re-emits the corresponding dumps; each dump terminated by a single
[READY] line so the JUCE viewer can detect dump completion.

Boot setup() also terminates the initial dump (BANKS + 8×BANK + 8×STATE)
with [READY]. The viewer waits for [READY] within ~2-3s after detecting
runtime mode; if absent (late connect after firmware already booted), it
pushes ?BOTH to force a re-emit.

Setup mode unaffected (InputParser claims the serial during setup; the
runtime command handler runs only after setup exits)."
```

Full validation of the viewer API extensions before declaring Phase A complete.

- [ ] **Step 1: Run pio device monitor and walk through full scenario**

```bash
~/.platformio/penv/bin/pio device monitor -b 115200
```

Validate:

1. Boot: `[INIT] Ready.` followed by `[BANKS] count=8`, 8× `[BANK]`, **8× `[STATE]`** (one per bank), and finally `[READY] current=N`. All formats per spec §8 §9.
2. Switch bank via left button: `[BANK]` followed by **1× `[STATE]`** (current bank only) on every switch.
3. Turn each pot in NORMAL bank: each `[POT]` line annotated with correct slot (`R1:`/`R1H:`/`R2:`/`R2H:`/`R3:`/`R3H:`/`R4:`/`R4H:`) when target is mapped to a user slot, or `--:` when target is global / hors-slot (cf. Task A.2 pass criteria).
4. If a slot is mapped to `TARGET_MIDI_CC`, tweaking it produces `[POT] R1: CCnn=value` (not silent).
5. If a slot is mapped to `TARGET_MIDI_PITCHBEND`, tweaking it produces `[POT] R1: PB=value`.
6. Switch to ARPEG bank: `[STATE]` shows the arpegMap slots, different from normalMap. `octave=` field present, no `mutationLevel=`.
7. Switch to ARPEG_GEN bank: `[STATE]` includes `mutationLevel=`, no `octave=`. Slots like `R1H=Division:1/8`, `R2H=Pattern:Up`.
8. Type `?STATE` + Enter in monitor → 1× `[STATE]` (current bank) + `[READY] current=N`.
9. Type `?BANKS` + Enter → `[BANKS] count=8` + 8× `[BANK]` + `[READY] current=N`.
10. Type `?BOTH` + Enter → `[BANKS]` + 8× `[BANK]` + 8× `[STATE]` + `[READY] current=N` (mirrors the boot dump).
11. Triple-click rear button → `[PANIC]` line (unchanged from before).
12. Press scale pad on current bank → `[SCALE]` line (unchanged).
13. Reboot ILLPAD in setup mode (hold rear button at boot) → setup mode enters, no `[STATE]`/`[BANKS]`/`[READY] current=N` corruption (the dumps are gated by `#if DEBUG_SERIAL` but the setup parser intercepts the serial — verify no command leaks into setup keyboard input).

**Pass criteria**: every step matches spec. No surprises. No regression on existing logs. `[READY] current=N` appears exactly once after the boot dump and exactly once after each `?STATE`/`?BANKS`/`?BOTH` response.

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

## Risks & notes (non-blocking)

- **Boot-settle order** — `pollRuntimeCommands()` is called at the top of `loop()`, before the `BOOT_SETTLE_MS` (~300 ms) early-return. If a host sends a command during the settle window, `dumpBankState()` may run before logs from final init paths have all drained. Idempotent (no state corruption), but log ordering on early boot is not strictly guaranteed.
- **`Serial.printf` blocking on Core 1** — Project invariant 3 ("No blocking on Core 1") is technically at risk : the new boot dump now emits 8× `[STATE]` (~8× 200 bytes ≈ 1.6 KB ≈ ~140 ms worst case at 115200 baud if USB CDC backpressures). Boot blocking is acceptable (no arp playing yet). At bank switch, only 1× `[STATE]` (~200 bytes / ~17 ms) — monitor for audible jitter in HW gate.
- **`?BOTH` dump cost** — re-emits 1+8+8+1 = 18 lines (~1.7 KB). Sent on-demand only, viewer's reconnect fallback, not periodic. Acceptable.
- **`extern void dumpBankState(uint8_t)` in BankManager.cpp** — Hidden coupling that automated refactor tools (which scan headers) will not detect. If `dumpBankState` is ever renamed, search for the extern manually. Acceptable for this one-shot wiring.
- **`?STATE` and `?BANKS` not used by the JUCE viewer V1** — The viewer only sends `?BOTH`. The other two commands remain for manual debugging via `pio device monitor` (smoke-tested in Task A.5 step 5). Keep, low cost.
- **MIDI CC live value on non-foreground banks** — `formatTargetValueForBank` emits `CCnn:?` for `TARGET_MIDI_CC` because `PotRouter::_ccValue[]` is indexed by active binding (foreground only). The viewer hydrates live CC values from `[POT] R1: CC74=42` events (Task A.2.b). V2 may add `PotRouter::getCcValue(uint8_t cc, bool arpCtx)` if the viewer needs to know the last-known CC value at boot for non-foreground banks.

---

## Out of scope for this plan

- Sync of firmware ColorSlot → JUCE viewer palette (spec §25 — V1 uses fixed app palette).
- Tick synchronization (spec §26 — V1 tick is JUCE-local, non-synced).
- Any other firmware modification beyond the 4 features above. Per spec §5, "porte ouverte si V3 le nécessite" — escalate to a new spec entry if a real need surfaces during JUCE viewer integration.

---

**End of plan.**
