---
name: repare
description: "Applique un fix sur un bug firmware ILLPAD V2. Diagnostique, applique le fix, compile, verifie. Utiliser apres /diagnostique ou directement avec une description du bug. Triggers: repare, fix, fixe, corrige, reparer, fixer."
user-invocable: true
model-invocable: true
---

# /repare — Fix firmware ILLPAD V2

Diagnostique, applique le fix, compile, verifie. Peut etre utilise directement ou apres un `/diagnostique`.

## Mode

**mode = "fix"** — toujours. Pour diagnostic read-only, utiliser `/diagnostique`.

## CRITICAL RULES

- **Apply the fix, then compile** with `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
- **NEVER modify** `CapacitiveKeyboard.cpp/.h` (musically calibrated, DO NOT TOUCH)
- **After fixing**: if any of the 5 documented flows changed, update `docs/architecture-briefing.md`

## Phase 1 — COMPRENDRE

Read these files IN ORDER before touching anything else:

1. `docs/architecture-briefing.md` — the 5 data flows, invariants, bug patterns, dirty flags
2. `.claude/CLAUDE.md` — full spec (only if architecture-briefing doesn't cover the area)

Then identify:
- **Which flow(s)** are involved? (Pad->MIDI, Arp Tick, Bank Switch, Scale Change, Pot->Param)
- **Which files** are in the chain? (use File Map by Domain, section 5)
- **Does it match a known bug pattern?** (section 4: stale state, orphan notes, MIDI flood, timing burst, tick contamination, button confusion)

Tell the user what you found:
> "Flow concerne : **[flow name]**. Je commence par [starting point] parce que [reason]."

## Phase 2 — TRIER

Evaluate complexity BEFORE diving into code:

| Level | Criteria | Pipeline adaptation |
|-------|----------|-------------------|
| **TRIVIAL** | Obvious cause, 1 file, 1-3 lines (typo, wrong constant, off-by-one) | Skip detailed trace, go straight to fix |
| **STANDARD** | 1 flow, incorrect logic, <50 lines | Full pipeline |
| **CROSS-CUTTING** | Multi-flow, timing, dual-core, or touches invariants | Full pipeline + parallel agents to verify adjacent flows |

Tell the user your assessment:
> "Complexite : **[TRIVIAL/STANDARD/CROSS-CUTTING]** — [one-line justification]"

## Phase 3 — TRACER

Follow the code path of the identified flow. Use the "Key files" references in architecture-briefing section 1.

```
For each step in the flow:
  1. Read the relevant code
  2. Check: does this step do what architecture-briefing says it should?
  3. If mismatch found -> potential root cause
  4. If no mismatch -> move to next step
```

**Check known bug patterns** (architecture-briefing section 4):
- Stale state after bank switch -> check catch targets, per-bank params
- Orphan notes on mode change -> check `_lastResolvedNote` usage
- MIDI flood from noisy ADC -> check CC dirty flag, hysteresis
- Timing burst after clock glitch -> check ticksElapsed guard
- BLE/USB tick contamination -> check source filtering
- Button combo confusion -> check `resolveBindings` mask

**Isolate root cause** — ask yourself: "Is this the CAUSE or just a SYMPTOM?"

## Phase 4 — VERIFIER

Before applying any fix, check ALL of these:

### 4a. Invariant check (architecture-briefing section 3)

| # | Invariant | Question to ask |
|---|-----------|----------------|
| 1 | No orphan notes | Does the fix ensure noteOff always uses `_lastResolvedNote`? |
| 2 | Arp refcount atomicity | Does the fix maintain noteOff-before-noteOn scheduling? |
| 3 | No blocking on Core 1 | Does the fix avoid any blocking call (mutex, flash write)? |
| 4 | Core 0 never writes MIDI | Does the fix keep all MIDI output on Core 1? |
| 5 | Catch system | Does the fix preserve pot catch-before-change behavior? |
| 6 | Bank slots always alive | Does the fix keep all 8 banks instantiated? |

### 4b. Cross-flow impact

Does the fix touch code shared by another flow? If yes, trace that flow too.

### 4c. Dirty flags & event queues (architecture-briefing section 6)

Does the fix affect any dirty flag or event queue? Check producer/consumer/overflow behavior.

### 4d. For CROSS-CUTTING only

Launch parallel agents to verify the adjacent flows are not broken:

```
Agent per affected flow:
  "Read architecture-briefing.md section 1 flow [X].
   Then read the code path. Verify the flow still works correctly
   given this proposed change: [describe fix].
   Report: SAFE or RISK with explanation."
```

## Phase 5 — FIXER

1. Apply the fix (Edit tool)
2. Compile: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
3. If compilation fails: diagnose the error, fix, recompile
4. If any of the 5 documented flows changed: update `docs/architecture-briefing.md`
5. Present the result

## Phase 6 — RESUME PEDAGOGIQUE

ALWAYS end with this structured summary:

```markdown
## Debug Report

### Demarche
[Which flow I identified first and WHY. Where I started looking and WHY.
This teaches the user to think about debugging systematically.]

### Cause racine
[What was broken, in one clear sentence. Then a brief explanation of
WHY it was broken — the underlying mechanism.]

### Fix applique
[What DID change, with file:line references.]

### Regressions verifiees
[List each invariant checked with result. List each adjacent flow checked.]
- Invariant 1 (no orphan notes): OK
- Invariant 3 (no blocking Core 1): OK
- Flow adjacent (Arp Tick): OK — [one-line reason]

### Bonne pratique
[ONE debugging lesson that applies beyond this specific bug.]
```

## Red Flags — STOP if you catch yourself doing these

| Thought | What to do instead |
|---------|-------------------|
| "I'll just quickly fix this obvious thing" | TRIER first. Even obvious bugs deserve 10 seconds of triage. |
| "The bug is probably in [guess]" | Read the flow in architecture-briefing. Follow the chain. Don't guess. |
| "I'll fix the symptom for now" | Find the ROOT CAUSE. Symptom fixes create new bugs. |
| "This is too complex to verify all invariants" | Check them anyway. Skipping invariant checks is how regressions happen. |
| "The fix is small so it can't break anything" | Small fixes in shared code paths cause the worst regressions. Verify. |
