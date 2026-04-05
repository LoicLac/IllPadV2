# LOOP Mode — Phase 5: Effects

**Goal**: Shuffle, chaos, and velocity patterns fully functional. The LOOP mode is complete.

**Prerequisite**: Phase 4 (PotRouter + LED) applied. Pots control LOOP params, LED shows state.

---

## Overview

Replace the 3 effect stubs in LoopEngine.cpp with real implementations. All the plumbing (PotRouter targets, param routing, NVS persistence) is already in place from Phase 4.

---

## Step 1 — LoopEngine.cpp: calcShuffleOffsetUs()

Replace stub with real implementation. Reference: design doc section "Effects > Shuffle".

```cpp
int32_t LoopEngine::calcShuffleOffsetUs(uint32_t eventOffsetUs, uint32_t recordDurationUs) {
    if (_shuffleDepth < 0.001f) return 0;

    uint32_t barDurationUs = recordDurationUs / _loopLengthBars;
    uint32_t posInBar = eventOffsetUs % barDurationUs;
    uint32_t stepDurationUs = barDurationUs / 16;
    uint8_t stepInBar = posInBar / stepDurationUs;
    if (stepInBar > 15) stepInBar = 15;

    int8_t templateVal = SHUFFLE_TEMPLATES[_shuffleTemplate][stepInBar];
    return (int32_t)((float)templateVal * _shuffleDepth * (float)stepDurationUs / 100.0f);
}
```

**Requires**: `#include "midi/GrooveTemplates.h"` in LoopEngine.cpp (already extracted, commit `0f31838`).

Verify that `SHUFFLE_TEMPLATES` is accessible — it's declared in `GrooveTemplates.h` as an `extern const` or `static const` array. If it's `static const` in the header, the include is sufficient.

---

## Step 2 — LoopEngine.cpp: calcChaosOffsetUs()

Replace stub. Reference: design doc section "Effects > Chaos / Jitter".

```cpp
int32_t LoopEngine::calcChaosOffsetUs(uint16_t eventIndex) {
    if (_chaosAmount < 0.001f) return 0;

    // Deterministic hash — same event always gets same jitter
    uint32_t hash = eventIndex * 7919;
    hash ^= (hash >> 13);
    hash *= 0x5bd1e995;
    hash ^= (hash >> 15);

    uint32_t barDurationUs = calcLoopDurationUs(_recordBpm) / _loopLengthBars;
    uint32_t stepDurationUs = barDurationUs / 16;
    int32_t maxOffsetUs = stepDurationUs / 2;

    int32_t offset = (int32_t)(hash % (maxOffsetUs * 2 + 1)) - maxOffsetUs;
    return (int32_t)((float)offset * _chaosAmount);
}
```

**Key property**: Deterministic — same event index always produces same jitter across loop iterations. This makes the groove feel consistent, not random.

---

## Step 3 — LoopEngine.cpp: applyVelocityPattern()

Replace stub. Reference: design doc section "Effects > Velocity Patterns".

```cpp
// 4 groove patterns (16 steps each, values 0-100 as percentage of original velocity)
static const uint8_t VEL_PATTERNS[4][16] = {
    {100,60,80,60, 100,60,80,60, 100,60,80,60, 100,60,80,60}, // accent 1&3
    {100,40,70,40, 90,40,70,40, 100,40,70,40, 90,40,70,40},   // strong downbeat
    {80,80,80,100, 80,80,80,100, 80,80,80,100, 80,80,80,100}, // backbeat
    {100,70,85,55, 95,65,80,50, 100,70,85,55, 95,65,80,50},   // swing feel
};

uint8_t LoopEngine::applyVelocityPattern(uint8_t origVel, uint32_t offsetUs,
                                          uint32_t recordDurationUs) {
    if (_velPatternDepth < 0.001f) return origVel;

    uint32_t barDurationUs = recordDurationUs / _loopLengthBars;
    uint8_t step = (offsetUs % barDurationUs) / (barDurationUs / 16);
    if (step > 15) step = 15;

    uint8_t patternVel = VEL_PATTERNS[_velPatternIdx][step];
    uint8_t scaledPattern = (uint8_t)((float)origVel * (float)patternVel / 100.0f);
    uint8_t result = (uint8_t)((float)origVel * (1.0f - _velPatternDepth) +
                               (float)scaledPattern * _velPatternDepth);
    return (result < 1) ? 1 : (result > 127) ? 127 : result;
}
```

---

## Step 4 — Verify tick() uses effects

The `tick()` method should already call these 3 methods in the cursor scan loop (written in Phase 2). Verify:

```cpp
    int32_t shuffleUs = calcShuffleOffsetUs(ev.offsetUs, recordDurationUs);
    int32_t chaosUs   = calcChaosOffsetUs(_cursorIdx);
    uint8_t vel       = applyVelocityPattern(ev.velocity, ev.offsetUs, recordDurationUs);
```

If Phase 2 used stubs that returned 0/identity, the call sites are already correct — the stubs are now replaced with real logic.

---

## Build Verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

## Test Verification (hardware)

1. Record a simple 1-bar loop (kick on 1, snare on 3)
2. **Shuffle**: hold left + turn pot R2 (shuffle depth)
   - At depth 0 → straight
   - At depth 0.5 → swing groove
   - At depth 1.0 → heavy shuffle
3. **Shuffle template**: hold left + turn pot R3
   - Templates 0-4 = positive only (classic)
   - Templates 5-9 = bipolar
4. **Chaos**: hold left + turn pot R1
   - At 0 → perfectly timed
   - At 0.3 → subtle humanize
   - At 1.0 → heavy jitter (but deterministic — same each cycle)
5. **Velocity pattern**: turn pot R3 (no hold)
   - 4 discrete positions → different accent patterns
6. **Velocity pattern depth**: turn pot R4 (no hold)
   - At 0 → original velocities
   - At 1.0 → full pattern applied
7. Verify all params survive bank switch + reboot (LoopPotStore)

---

## Files Modified

| File | Changes |
|---|---|
| `src/loop/LoopEngine.cpp` | Replace 3 effect stubs with real implementations, add VEL_PATTERNS LUT |

## Files NOT Modified

Everything else is already in place from Phase 4.

---

## LOOP Mode Complete

After Phase 5, the LOOP mode is fully functional:
- Record / play / overdub / clear
- Proportional tempo tracking
- Shuffle + chaos + velocity patterns
- Per-bank params persisted in NVS
- LED feedback for all states
- Setup UI for bank config + pad roles
- Pots control all params
- Background playback on bank switch
