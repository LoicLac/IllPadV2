# /audit Skill — Automated Code Review for ILLPAD V2

## Overview

A workspace-scoped skill that dispatches parallel code-reviewer agents against a curated pool of testable claims derived from CLAUDE.md. Produces an append-only report in `docs/Bug_Routine.md` with actionable context for debugging.

## Files

| File | Purpose |
|---|---|
| `.claude/skills/audit/SKILL.md` | Skill definition (the full prompt Claude follows) |
| `docs/audit_topics.md` | 50+ testable claims, organized by category, editable by user |
| `docs/Bug_Routine.md` | Append-only report, git-tracked |

## Invocation

| Command | Behavior |
|---|---|
| `/audit` | 1 round: pick 5 random topics, launch 5 parallel agents, append results |
| `/audit 3` | 3 rounds of 5 (= 15 reviews). Each round picks 5 fresh topics. No repeats within invocation. |
| `/audit ALL` | Run every topic in the pool, batched in groups of 5 parallel agents. Bypasses report-based dedup. |

## Definitions

- **Session (invocation)**: A single `/audit` command execution. `/audit 3` is one invocation with 3 rounds. Dedup state (which topics were already picked) lives only for the duration of that invocation.
- **Round**: One batch of up to 5 parallel agents within an invocation.

## Round Execution Flow

Each round follows this sequence:

1. **Read pool**: Read `docs/audit_topics.md` to get all available topics. Parse lines matching `- [ID] Name: claim` format.
2. **Avoid repeats** (skipped for `/audit ALL`): Scan `docs/Bug_Routine.md` for all `[TOPIC-ID]` occurrences in headings (`### N. [ID]`). Exclude those topic IDs + any already picked in this invocation.
3. **Pick topics**: Select up to 5 topics randomly from the remaining pool. If fewer than 5 remain, pick all remaining. If zero remain, skip the round and append a notice: `> All topics exhausted — no topics to audit.`
4. **Dispatch parallel agents**: Each agent is launched via the Claude Code `Agent` tool with `subagent_type: "superpowers:code-reviewer"`. Each receives:
   - The topic ID and name
   - The testable claim (what the spec says)
   - Instructions for reporting depth (see Report Detail below)
   - Read-only access to the full codebase
5. **Collect results**: Wait for all agents to complete. If an agent fails or times out, record that topic as `ERROR` with a brief explanation. Do not retry.
6. **Append to report**: Write a formatted block to `docs/Bug_Routine.md`.

For `/audit N`: repeat steps 1-6 N times, each round picking fresh topics (dedup accumulates across rounds within the invocation).

For `/audit ALL`: ignore step 2 dedup. Compute total topics from pool, run ceil(total/5) rounds covering every topic exactly once.

## Report Format

Each round appends one block to `docs/Bug_Routine.md`. The score line is mandatory and reflects the actual count of topics in that round (may be less than 5 on final batch).

For multi-round invocations, include a round counter: `(round 2/3)`.

~~~~markdown
---
## Run — YYYY-MM-DD HH:MM (round 1/3)

### 1. [TOPIC-ID] Topic Name — PASS
Brief summary (1 line).

### 2. [TOPIC-ID] Topic Name — WARNING
**Spec says**: "quoted claim from CLAUDE.md"
**Code does**: description with `file.cpp:123` references and code snippets
**Risk**: concrete scenario explaining why this matters for a live musical instrument
**Suggested fix**: code-level recommendation

### 3. [TOPIC-ID] Topic Name — FAIL
**Spec says**: "quoted claim from CLAUDE.md"
**Code does**: description with `file.cpp:123` references and code snippets
**Why it's wrong**: concrete failure scenario (e.g., "DAW sends 0xFC, arp resumes on next tick because _playing is never set to false")
**Suggested fix**:

    // recommended code change (4-space indent to avoid nested fence issues)

### 4. [TOPIC-ID] Topic Name — ERROR
Agent failed or timed out. Topic not reviewed.

**Score: 2 PASS | 1 WARNING | 1 FAIL | 1 ERROR**
---
~~~~

## Report Detail Rules

**PASS**: One summary line. No code snippets needed.

**WARNING**: Must include all of:
- Exact file:line references for every relevant code path
- The spec claim that's violated or at risk (quoted from CLAUDE.md)
- What the code actually does (with code snippets)
- The concrete risk scenario — especially for a live musical instrument (stuck notes, audio glitches, timing drift, etc.)
- Recommended fix with code suggestion

**FAIL**: Must include all of the above, plus:
- Why it's definitively wrong (not just risky)
- The failure scenario in specific terms (what user action or MIDI message triggers it, what goes wrong, what the musician hears/experiences)

**ERROR**: One line explaining the agent failure. Topic remains in the pool for future runs.

## Topic Pool Format

`docs/audit_topics.md` is a flat markdown file with topics grouped by category:

```markdown
# ILLPAD V2 — Audit Topic Pool

## Category Name
- [CAT-01] Short name: testable claim describing exactly what to verify
- [CAT-02] Short name: another testable claim
```

Each topic line must be self-contained — an agent receiving just that line plus access to CLAUDE.md and the codebase should know exactly what to check.

Topics can be retired by commenting them out with `<!-- -->` to preserve history while excluding from the pool.

### Categories (seeded)

| Category | Focus | ~Count |
|---|---|---|
| Memory Safety | Atomics, volatile, inter-core sync, data races | 5 |
| Note Integrity | noteOn/Off pairing, orphan prevention, allNotesOff paths | 5 |
| Arpeggiator Core | Pile storage, sequence building, pattern logic, octave expansion | 6 |
| Arp Timing | Quantized start, shuffle, gate, event system, refcounting | 5 |
| Clock & Transport | PLL, master/slave, Start/Stop/Continue handling, follow transport | 5 |
| LED System | Priority state machine, confirmations, sine pulse, tick flash | 4 |
| NVS & Persistence | Non-blocking writes, dirty flags, namespace correctness, boot load | 4 |
| Pot System | Catch, global propagation, CC/PB output, binding rebuild, mapping store | 5 |
| Button Logic | Left hold combos, rear combos, modifier crossing prevention, play/stop pad | 4 |
| Boot Sequence | Progressive LED fill, failure blink, setup mode detection, step ordering | 3 |
| Conventions | Naming, no runtime alloc, unsigned millis, debug macros, stack safety | 4 |
| MIDI Output | Channel routing, BLE bypass, aftertouch throttle, panic, flush ordering | 5 |
| Serial & VT100 | Setup mode terminal rendering, escape sequences, input parsing, button-as-ENTER, refresh rates, screen clearing, color codes | 4 |
| Abstract / Architectural | Cross-module state consistency, separation of concerns, interaction edge cases (e.g., scale change + arp + bank switch simultaneously), foreground/background invariants, data flow integrity across subsystems | 5 |

Total: ~64 topics.

## Constraints

- **Read-only**: No code modifications, no compilation, no uploads
- **No external calls**: No web fetches, no network
- **Workspace-scoped**: Skill lives in `.claude/skills/audit/`, only available in this project
- **Agent type**: All review agents use `superpowers:code-reviewer` subagent type
- **Parallel limit**: 5 agents per round (matches what we've validated works well)

## What This Skill Does NOT Do

- Does not fix bugs (that's the user's job, informed by the report)
- Does not build or upload firmware
- Does not modify any source files
- Does not track state between sessions (avoids stale state files)
