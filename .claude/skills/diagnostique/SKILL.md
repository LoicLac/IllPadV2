---
name: diagnostique
description: "Diagnostic read-only de bugs firmware ILLPAD V2. Analyse le code, trace le flow, identifie la cause racine, propose un fix SANS modifier de fichier. Triggers: diagnostique, diagnose, bug, probleme, stuck notes, bloque, ghost notes, notes bloquees, crash, glitch."
user-invocable: true
model-invocable: true
---

# /diagnostique — Diagnostic firmware ILLPAD V2

Diagnostic read-only. Trouve la cause racine, propose un fix, **ne modifie AUCUN fichier source**.

## Mode

**mode = "diagnose"** — toujours. Pour appliquer un fix, utiliser `/repare`.

## CRITICAL RULES

- **NEVER modify source files.** Present the fix, wait for user approval.
- **NEVER modify** `CapacitiveKeyboard.cpp/.h` (musically calibrated, DO NOT TOUCH)

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
- Symptom: "notes stay stuck after bank switch"
- Cause: "allNotesOff() runs AFTER setChannel(), so noteOff goes to wrong channel"

## Phase 4 — VERIFIER

Before proposing any fix, check ALL of these:

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

## Phase 5 — PROPOSER

Present the fix proposal clearly:

```markdown
## Proposition de fix

**Cause racine** : [one sentence]
**Fichier(s)** : [file:line refs]
**Changement** : [describe what changes]

[code snippet showing the fix]

**Invariants verifies** : [list which passed]
**Flows adjacents** : [list which were checked, or "aucun impact"]
```

Then STOP. Do not modify any file. Wait for user to say `/repare`.

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

### Fix propose
[What SHOULD change, with file:line references.]

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
| "Let me modify the code to see if..." | You MUST NOT touch code. Read and reason. |
| "The bug is probably in [guess]" | Read the flow in architecture-briefing. Follow the chain. Don't guess. |
| "I'll fix the symptom for now" | Find the ROOT CAUSE. Symptom fixes create new bugs. |
| "This is too complex to verify all invariants" | Check them anyway. Skipping invariant checks is how regressions happen. |
| "The fix is small so it can't break anything" | Small fixes in shared code paths cause the worst regressions. Verify. |
