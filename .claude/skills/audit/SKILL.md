---
name: audit
description: Run automated code review against ILLPAD V2 codebase. Dispatches parallel agents to verify testable claims from the spec. Usage: /audit (5 random topics), /audit N (N rounds of 5), /audit ALL (every topic), /audit TAG (all topics matching prefix, e.g. /audit RGB, /audit ARP, /audit NOTE).
user-invocable: true
---

# /audit — Automated Code Review

You are executing the /audit skill for the ILLPAD V2 project. Follow these instructions exactly.

## Parse Arguments

Read the argument string passed to the skill:
- No argument or empty → `mode = "single"`, `rounds = 1`, `tagFilter = null`
- A number N (e.g., "3") → `mode = "multi"`, `rounds = N`, `tagFilter = null`
- "ALL" (case-insensitive) → `mode = "all"`, `tagFilter = null`
- A tag prefix (e.g., "NOTE", "ARP", "RGB", "LED") → `mode = "tag"`, `tagFilter = uppercase argument`
  - Match rule: topic ID starts with `tagFilter` (e.g., "ARP" matches ARP-01, ARPT-01, ARPE-01)
  - If tag matches zero topics, print `No topics found matching tag "[TAG]".` and stop

## Step 1: Read the Topic Pool

Read the file `docs/audit_topics.md`. Parse all lines matching the pattern:
```
- [ID] Name: claim text
```
Build a list of topic objects: `{id, name, claim}`. Ignore comment lines (`<!-- -->`), headings, and blank lines.

## Step 2: Filter Pool & Read Recent Report

**Tag filtering** (mode = "tag"):
- Filter the topic pool to only topics whose ID starts with `tagFilter`
- Example: tag "ARP" keeps ARP-01..06, ARPT-01..05, ARPE-01..05 (all start with "ARP")
- Example: tag "RGB" keeps RGB-01..14 only
- Skip recent-report dedup — run ALL matching topics (like "all" mode but scoped)

**ALL mode**: Skip recent-report dedup — every topic will be run.

**Single/multi mode**:
- Read `docs/Bug_Routine.md` if it exists
- Extract all topic IDs from lines matching `### N. [TOPIC-ID]`
- These are the "recently audited" set — exclude them from selection

## Step 3: Execute Rounds

Maintain a session dedup set (starts empty). For each round:

### 3a. Pick Topics

- Remove from the pool: recently audited IDs (step 2) + session dedup set
- If mode is "all" or "tag": pick the next 5 topics sequentially from the (filtered) pool (no randomization)
- Otherwise: pick 5 topics randomly from the remaining pool
- If fewer than 5 remain, pick all remaining
- If zero remain, append to report: `> All topics exhausted — no topics to audit.` and stop
- Add picked IDs to session dedup set

### 3b. Dispatch 5 Parallel Agents

Launch all picked topics as parallel agents using the Agent tool. For EACH agent:

- `subagent_type`: `"superpowers:code-reviewer"`
- `description`: `"Audit [TOPIC-ID]"`
- `prompt`: Use this exact template, filling in the topic details:

```
You are auditing the ILLPAD V2 firmware codebase (ESP32-S3 MIDI controller).

**Topic**: [TOPIC-ID] — TOPIC_NAME
**Claim to verify**: CLAIM_TEXT

Read the project's CLAUDE.md for full spec context. Then search the codebase to verify or refute this claim.

## Reporting Rules

**If PASS**: Report one summary sentence. No code needed.

**If WARNING or FAIL**, you MUST include ALL of the following:
1. **Spec says**: Quote the relevant claim from CLAUDE.md
2. **Code does**: Describe what the code actually does, with exact file:line references and code snippets for every relevant code path
3. **Scenario**: Describe the concrete failure/risk scenario — this is a live musical instrument, so explain what the musician would experience (stuck notes, wrong notes, timing glitch, audio dropout, display corruption, etc.)
4. **Suggested fix**: Provide a code-level recommendation with a code snippet

For WARNING: explain why it's risky but not definitively broken.
For FAIL: explain why it's definitively wrong and what specific user action or MIDI message triggers the bug.

## Output Format

Start your response with exactly one of these lines:
PASS: one-line summary
WARNING: one-line summary
FAIL: one-line summary

Then provide the detailed findings below (for WARNING/FAIL only).
```

### 3c. Collect Results

Wait for all agents to complete. For each result:
- Parse the first line to extract verdict (PASS/WARNING/FAIL)
- If an agent failed or returned no parseable verdict, mark as ERROR

### 3d. Append to Report

Read `docs/Bug_Routine.md` (create it if it doesn't exist — start with `# ILLPAD V2 — Bug Routine Report\n\n`).

Append a block in this exact format:

```
---
## Run — YYYY-MM-DD HH:MM (round X/Y)
```

For single-round mode, omit the `(round X/Y)` part.

For each topic result, append:

**PASS**:
```
### N. [TOPIC-ID] Topic Name — PASS
Summary line from agent.
```

**WARNING**:
```
### N. [TOPIC-ID] Topic Name — WARNING
Full agent output (spec says, code does, scenario, suggested fix — all of it).
```

**FAIL**:
```
### N. [TOPIC-ID] Topic Name — FAIL
Full agent output (spec says, code does, scenario, suggested fix — all of it).
```

**ERROR**:
```
### N. [TOPIC-ID] Topic Name — ERROR
Agent failed or timed out. Topic not reviewed.
```

End the block with:
```
**Score: X PASS | Y WARNING | Z FAIL | W ERROR**
```

### 3e. Report to User

After each round, print a brief summary to the user:
```
Round N/M complete: X PASS | Y WARNING | Z FAIL
```

If there were any FAIL results, list their topic IDs.

## Step 4: Final Summary

After all rounds complete, print:
```
Audit complete. N rounds, M topics reviewed.
Results appended to docs/Bug_Routine.md
FAIL: [list of FAIL topic IDs, or "none"]
WARNING: [list of WARNING topic IDs, or "none"]
```

## Important Rules

- Do NOT modify any source files. This is read-only review.
- Do NOT compile or build. No `pio run`.
- Do NOT make web requests.
- Each agent gets the FULL claim text from the topic pool — do not abbreviate.
- Preserve all detail from WARNING/FAIL agents in the report — the user needs full context for debugging.
- Use the current date/time for the report header.
