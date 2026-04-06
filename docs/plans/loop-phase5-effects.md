# LOOP Mode — Phase 5: Effects

**Goal**: Shuffle, chaos, and velocity patterns fully functional. The LOOP mode is complete.

**Prerequisite**: Phase 4 (PotRouter + LED) applied. Pots control LOOP params, LED shows state.

---

## Overview

Replace the 3 effect stubs in LoopEngine.cpp with real implementations. All the plumbing (PotRouter targets, param routing, NVS persistence) is already in place from Phase 4.

---

## Step 1 — LoopEngine.cpp: calcShuffleOffsetUs()

Replace the Phase 2 stub with the real implementation below.

```cpp
int32_t LoopEngine::calcShuffleOffsetUs(uint32_t eventOffsetUs, uint32_t recordDurationUs) {
    if (_shuffleDepth < 0.001f) return 0;
    // B-PLAN-2 defensive guard (audit 2026-04-07): tick() upstream guards on
    // liveDurationUs/recordDurationUs == 0, but the helpers should also be
    // defensive (consistency with the rest of LoopEngine).
    if (_loopLengthBars == 0) return 0;

    uint32_t barDurationUs = recordDurationUs / _loopLengthBars;
    if (barDurationUs < 16) return 0;  // step would be 0, no shuffle possible
    uint32_t posInBar = eventOffsetUs % barDurationUs;
    uint32_t stepDurationUs = barDurationUs / 16;
    uint8_t stepInBar = posInBar / stepDurationUs;
    if (stepInBar > 15) stepInBar = 15;

    int8_t templateVal = SHUFFLE_TEMPLATES[_shuffleTemplate][stepInBar];
    return (int32_t)((float)templateVal * _shuffleDepth * (float)stepDurationUs / 100.0f);
}
```

**Requires**: `#include "../midi/GrooveTemplates.h"` in LoopEngine.cpp (already extracted, commit `0f31838`).

`SHUFFLE_TEMPLATES` is `static const int8_t [10][16]` in the header — the include is sufficient. 10 templates (0-4 positive-only classic, 5-9 bipolar), 16 steps each. Same formula as ArpEngine (identical core math, only step-index derivation differs: position-based here vs counter-based in arp).

---

## Step 2 — LoopEngine.cpp: calcChaosOffsetUs()

Replace the Phase 2 stub with the real implementation below.

```cpp
int32_t LoopEngine::calcChaosOffsetUs(uint32_t eventOffsetUs) {
    if (_chaosAmount < 0.001f) return 0;

    // Deterministic hash — same event always gets same jitter across loop cycles.
    // Hash on offsetUs (stable across overdub merges — index would shift).
    // +1 offset: without it, offsetUs 0 hashes to 0 (all operations absorb zero).
    uint32_t hash = (eventOffsetUs + 1) * 7919;
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

**Key properties**:
- **Deterministic** — same event always produces same jitter across loop iterations (groove feels consistent, not random).
- **Stable across overdubs** — hashed on `offsetUs` (temporal position), not buffer index. Buffer indices shift after `mergeOverdub()`, but `offsetUs` never changes. The groove doesn't change when the user adds notes via overdub.

---

## Step 3 — LoopEngine.cpp: applyVelocityPattern()

Replace the Phase 2 stub with the real implementation below.

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
    // B-PLAN-2 defensive guard (audit 2026-04-07).
    if (_loopLengthBars == 0) return origVel;

    uint32_t barDurationUs = recordDurationUs / _loopLengthBars;
    if (barDurationUs < 16) return origVel;  // step would be 0
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

The `tick()` method should already call these 3 methods in the cursor scan loop (written in Phase 2). Both noteOn AND noteOff go through `schedulePending` with shuffle/chaos offsets applied (Design #1 redesign — preserves gate length on sustained sounds).

Verify the cursor scan inner loop matches:

```cpp
    while (_cursorIdx < _eventCount && _events[_cursorIdx].offsetUs <= positionUs) {
        const LoopEvent& ev = _events[_cursorIdx];
        int32_t shuffleUs = calcShuffleOffsetUs(ev.offsetUs, recordDurationUs);
        int32_t chaosUs   = calcChaosOffsetUs(ev.offsetUs);

        if (ev.velocity > 0) {
            uint8_t vel = applyVelocityPattern(ev.velocity, ev.offsetUs, recordDurationUs);
            schedulePending(now + shuffleUs + chaosUs, padToNote(ev.padIndex), vel);
        } else {
            // noteOff also through pending — shuffle/chaos preserves gate
            schedulePending(now + shuffleUs + chaosUs, padToNote(ev.padIndex), 0);
        }
        _cursorIdx++;
    }
```

If Phase 2 used stubs that returned 0/identity, the call sites are already correct — the stubs are now replaced with real logic. The `applyVelocityPattern` is only called for noteOn (velocity > 0) — no wasted computation on noteOff events.

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
