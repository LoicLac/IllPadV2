# Phase 1 → Phase 2 Handoff — LOOP Mode

**From**: Phase 1 (skeleton + guards)
**To**: Phase 2 (engine + wiring)
**Branch**: `loop` (NOT `main`)
**Base commit**: `1eb4ab9` (docs prompts, first commit on loop branch)
**Tip commit after Phase 1**: `5842ae7` (nvs-reference update)
**Author session**: Opus 1M, 2026-04-07 / 2026-04-08

---

## TL;DR for Phase 2 implementer

1. **You are on branch `loop`, not `main`.** All Phase 1-6 work lives here. Do NOT merge to main until all phases are done and tested.
2. **Read the Phase 2 plan IN FULL** (`docs/plans/loop-phase2-engine-wiring.md`), not just this handoff. This handoff summarizes Phase 1 state; the plan is the source of truth for Phase 2 work.
3. **Read `docs/plans/loop-phase1-skeleton-guards.md` too** for context on what's already in place, especially the audit fixes that are now part of the codebase.
4. **Start by re-reading `.claude/CLAUDE.md`, `docs/reference/architecture-briefing.md`, `docs/reference/nvs-reference.md`, and `docs/known_issues/known-bugs.md`** before touching code. The implementer prompt enforces this.
5. **Do NOT push automatically** after commits. The user approves each push manually.

---

## 1. Phase 1 commits (14 total, reverse-chronological)

```
5842ae7 docs(nvs-reference): sync Phase 1 LOOP structural changes
a595ec5 docs(known-bugs): add B-005/B-006 Tool 6 CC editing UX gaps
9d2763e fix(nvs): bootstrap PotFilterStore on first boot when absent
02f257f loop(phase1): defer LoopPadStore descriptor to Phase 3 + track B-004
706caaf docs(known-bugs): mark B-003 as DONE (resolved by Phase 1 Commit 7)
f8d9f0a loop(phase1): main.cpp loopEngine init nullptr + PotMappingStore.loopMap
328d9ef docs(known-bugs): add B-003 NORMAL R2/R3 pot binding swap
f1c69c2 loop(phase1): ScaleManager LOOP early return + _lastScaleKeys sync
9fcfbe3 loop(phase1): BankManager LOOP guards (stub) + MidiTransport pointer
2ddb4a1 loop(phase1): LedController CONFIRM_LOOP_REC + showClearRamp + LoopEngine fwd
c5fb7dd loop(phase1): NVS validation + descriptors + NvsManager LOOP quantize cache
2da053d loop(phase1): add LOOP HW constants + LoopPadStore/LoopPotStore structs
c47bfa8 loop(phase1): add BANK_LOOP type enum + BankSlot.loopEngine pointer
1eb4ab9 docs(prompts): add LOOP phase 1 implementation + verification prompts
```

Chronological summary:
- `1eb4ab9` — Phase 1 prompts (pre-session, already on `loop`)
- `c47bfa8` → `f1c69c2` — 6 structural commits that implement Steps 1-6 of the plan
- `328d9ef` — in-session discovery of B-003 pot binding swap, noted for diagnosis
- `f8d9f0a` — Step 7 + 7b (final structural commit), which incidentally resolved B-003 by invalidating the stale PotMappingStore blob
- `706caaf` — B-003 marked DONE after hardware confirmation
- `02f257f` — audit fix: Bug A revert (LoopPadStore descriptor deferred to Phase 3) + B-004 tracking
- `9d2763e` — pre-existing bug fix (X2): PotFilterStore bootstrap writer
- `a595ec5` — B-005 / B-006 tracking (Tool 6 CC editing UX)
- `5842ae7` — `nvs-reference.md` sync

## 2. Files modified in Phase 1

| File | Changes |
|---|---|
| `src/core/KeyboardData.h` | BANK_LOOP enum, LoopEngine forward-decl, BankSlot.loopEngine, MAX_LOOP_BANKS, LoopPadStore (32B v2), LoopPotStore (8B), NVS defines (LOOP_PAD/LOOPPOT), validateLoopPadStore, validateBankTypeStore extended (BUG #1 gated clamps + loopCount + loopQuantize), BankTypeStore extended (loopQuantize[], v1→v2, 20→28B), PotMappingStore extended (loopMap[8], 36→52B). **NVS_DESCRIPTORS entry for LoopPadStore NOT added** (deferred to Phase 3, see `02f257f`). |
| `src/core/HardwareConfig.h` | LoopQuantMode enum + DEFAULT_LOOP_QUANT_MODE. 5 LOOP bank state colors (FREE/QUANTIZED/REC/OVD/DIM). LED FG/BG LOOP intensities (8 constants). LED tick flash hierarchy (4 BOOST + 3 DUR + 2 WAIT_QUANT). LOOP slot drive constants (LONG_PRESS_MS, LOAD_MIN_MS, RAMP_DURATION_MS, LOOP_SLOT_COUNT=16, 4 slot drive colors). |
| `src/core/LedController.h` | `class LoopEngine;` forward-decl. CONFIRM_LOOP_REC=10 enum value. `showClearRamp(uint8_t pct)` inline stub. |
| `src/core/LedController.cpp` | `renderConfirmation()` CONFIRM_LOOP_REC expiry case (200ms, no rendering — audit C1 fix). |
| `src/managers/BankManager.h` | `class MidiTransport;` forward-decl. `begin()` signature gains `MidiTransport*`. `_transport` private member. |
| `src/managers/BankManager.cpp` | `#include "../core/MidiTransport.h"`. `begin()` body stores `_transport`. `switchToBank()` has 2 inert LOOP guard blocks with Phase 2 TODOs in comments (audit BUG #2 encoded as comment). Debug print 3-way array (NORMAL/ARPEG/LOOP). |
| `src/managers/ScaleManager.cpp` | `processScalePads()` LOOP early return with full `_lastScaleKeys` sync (root/mode/chrom + hold + octave pads — audit B6 fix). |
| `src/managers/NvsManager.h` | `_loadedLoopQuantize[NUM_BANKS]` private member. `getLoadedLoopQuantizeMode(bank)` / `setLoadedLoopQuantizeMode(bank,mode)` public getter/setter. |
| `src/managers/NvsManager.cpp` | `loadAll()` BankTypeStore block: memset `_loadedLoopQuantize` to default, copy `bts.loopQuantize[i]` alongside `_loadedQuantize[i]`. Getter/setter bodies. Commit `9d2763e` also added the PotFilterStore bootstrap writer in the same file (X2 fix for pre-existing bug). |
| `src/main.cpp` | `s_banks[i].loopEngine = nullptr` init in BankSlot init loop (audit GAP #5). `s_bankManager.begin()` call updated with `&s_transport` trailing arg. |
| `docs/reference/nvs-reference.md` | Descriptor count 10→11. Store table updated with final sizes + versions. `validatePotFilterStore` documented. Phase 3 placeholder noted for LoopPadStore descriptor. |
| `docs/known_issues/known-bugs.md` | B-003 added then marked DONE. B-004 (Tool 6 reset-to-defaults not rendered), B-005 (Tool 6 CC# live scroll missing), B-006 (Tool 6 CC# no serial log) tracked as pre-existing TODO. |

## 3. Deviations from the Phase 1 plan

These are the places where the implementer had to deviate from the written plan. Phase 2 implementer should be aware:

1. **Commit ordering reshuffled** — The plan described 7 steps but the implementer grouped them into 7 commits based on logical cohesion instead. Specifically:
   - Step 4a/4b (HardwareConfig LOOP colors + intensities) were moved out of Commit 4 and inlined into Commit 2 alongside Step 2 + Step 7c-1, so all HardwareConfig.h LOOP additions land in one cohesive commit. Commit 4 stayed lean with just Step 4c (LedController stubs).
   - Step 7c-1 (slot drive constants in HardwareConfig.h) was inlined into Commit 2 rather than placed in its own final commit, because `LOOP_SLOT_COUNT` must be visible to `LoopPadStore.slotPads[]` at compile time (the plan itself noted this flexibility).

2. **Step 3e `_loadedLoopQuantize` memset location** — The plan said to memset in the constructor. The code puts the existing `_loadedQuantize` memset in `loadAll()`, not the constructor. The implementer followed the existing pattern (memset next to the one that was already there) instead of the plan's literal instruction. Both places work, but the result is that you'll find the line in `NvsManager.cpp` around the BankTypeStore load, not in the constructor.

3. **Plan typo fix — TOOL_NVS_LAST T6** — The plan's Step 3d gave `TOOL_NVS_LAST = { 0, 1, 5, 6, 7, 8, 11 }`. The correct value after inserting LoopPadStore at index 5 should be `{ 0, 1, 5, 6, 7, 9, 11 }` because PotFilter shifted from index 8 to index 9 (so T6 becomes [8..9], not [8..8]). The implementer applied the fix at edit time. **This is moot now** since commit `02f257f` reverted the descriptor insertion entirely (see deviation 4), restoring both arrays to their pre-Commit-3 values. But if Phase 3 re-adds the descriptor, the T6 LAST must become 9, not 8.

4. **Step 3c/3d reverted in commit `02f257f`** — The plan added a descriptor entry for `LoopPadStore` in `NVS_DESCRIPTORS[]` and shifted TOOL_NVS_FIRST/LAST accordingly. This broke the Tool 3 menu badge (stuck at "!") because `SetupUI::printMainMenu` checks all descriptors in T3's range, and `LoopPadStore` has no writer until Phase 3 (ToolPadRoles refactor to the b1 contextual architecture). The fix deferred the descriptor entry entirely. The struct, validation, and namespace defines all stay in `KeyboardData.h` — they are inert without a descriptor entry pointing at them. **Phase 3 must re-add the descriptor entry at the same time it adds the writer.**

5. **Commit `9d2763e` is NOT a Phase 1 scope item** — During Checkpoint C testing, the user discovered Tool 6 NVS badge stuck at "!" even after re-saving PotMapping. Investigation showed that `PotFilterStore` has a descriptor but no writer anywhere in the codebase — pre-existing bug since commit `94d4ed2` (2026-04-04). Bug only became visible because Phase 1's Tool 6 testing exposed it. Fixed inline with a minimal bootstrap writer in `NvsManager::loadAll`. Scope creep, accepted by user explicitly ("X2 option"). Phase 2 implementer should understand this commit exists outside the normal Phase 1 scope.

## 4. Audit fixes that are now in the code

Each was described in `docs/plans/loop-phase1-skeleton-guards.md` as an "AUDIT FIX" or "AUDIT NOTE" and is now materialized in the code. Phase 2 implementer should not accidentally undo any of these:

| Fix | Location in code | Description |
|---|---|---|
| **BUG #1** | `KeyboardData.h` `validateBankTypeStore` | Clamp each type on its own bank type (not cross-contaminated by earlier arpCount overflow). Gated `if (s.types[i] == BANK_ARPEG)` then check count — same pattern for LOOP. |
| **BUG #2** | `BankManager.cpp` `switchToBank` | LOOP guards check `loopEngine != nullptr` as the OUTER gate (Phase 1 stub), but the comment documents the correct Phase 2 pattern: `loopEngine->isRecording()` as the INNER check. Phase 2 must add the state check, not remove the nullptr check. |
| **BUG #3** | `KeyboardData.h` `PotMappingStore` | `loopMap[POT_MAPPING_SLOTS]` added. Size 36→52. No version bump (Zero Migration Policy handles the size mismatch rejection). |
| **GAP #5** | `main.cpp:197` BankSlot init loop | `s_banks[i].loopEngine = nullptr` added explicitly alongside arpEngine. |
| **A1** | `NvsManager.h` / `.cpp` `_loadedLoopQuantize` + getter/setter | Phase 2 Step 4a will use `s_nvsManager.getLoadedLoopQuantizeMode(i)`, NOT the nonexistent `s_bankTypeStore.loopQuantize[i]`. **This is a correctness-critical rename** — the plan for Phase 2 Step 4a still has the old reference in some drafts; double-check when you implement it. |
| **B6** | `ScaleManager.cpp` `processScalePads` LOOP early return | Syncs 15 scale pads + 1 hold pad + 4 octave pads into `_lastScaleKeys` (20 entries total) before returning. Phase 2 may not touch this file; if it does, preserve the full sync. |
| **C1** | `LedController.cpp` `renderConfirmation` | `CONFIRM_LOOP_REC` expiry case with hardcoded 200ms, no overlay rendering. Phase 4 adds the rendering. Phase 2 calls `triggerConfirm(CONFIRM_LOOP_REC)` and this expiry case prevents the default branch from silently clearing it. |
| **C2** | `KeyboardData.h` `LoopPadStore` | Single definition at final layout (32 bytes, version 2). Phase 1 step 7c-2 and 7c-3 in the plan are consolidated into step 2b. No second definition exists. |

## 5. Critical reminders for Phase 2

### 5.1 `_pendingKeyIsPressed` / `_pendingPadOrder` / `_pendingBpm` member declarations

Audit `D-PLAN-1` (discovered in Phase 2 plan review) requires that these members be declared in LoopEngine. Phase 2 plan was patched in commit `1366918` to include the declarations. **Verify they are in the plan you read before implementing** — if a snippet references these members without declaring them, add the declarations.

### 5.2 Branch discipline

You are on branch `loop`. Do NOT switch branches. Do NOT merge to main. Do NOT push automatically after commits. The user approves each push manually.

```bash
git branch --show-current   # should print "loop"
git log main..HEAD --oneline # should show Phase 1 commits (this file at the top)
```

### 5.3 Plan-vs-code verification on every edit

The plan has been audited multiple times but is NOT the source of truth — the code is. For every snippet in the Phase 2 plan:
1. Read the target file IN FULL before editing
2. Verify line numbers and function signatures haven't drifted
3. Verify that audit fixes from Phase 1 are still in place and not being accidentally overwritten
4. Apply the edit with full context, not just the 5 lines around the change point

### 5.4 Build + hardware checkpoint discipline

- Build (`~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`) after each commit group, read the output in full, track RAM/Flash metrics
- STOP at every hardware checkpoint listed in the Phase 2 plan, present the exact test scenarios to the user, wait for explicit OK
- Do NOT batch commits or skip verification

### 5.5 Scope discipline

- Follow the Phase 2 plan strictly. No "tant qu'on y est" refactors.
- If you encounter a pre-existing bug that's orthogonal to Phase 2, track it in `known-bugs.md` and keep moving.
- The user explicitly approved X2 (pre-existing bug fix in `9d2763e`) during Phase 1. Do NOT assume similar approval is implicit in Phase 2 — always ask.

## 6. Hardware tests that passed in Phase 1

### Checkpoint A (after Step 5 — BankManager guards)
- Boot bar LED 1→8 OK
- Bank switch NORMAL → ARPEG → NORMAL via hold-left OK
- ARPEG pile add/remove, background continuation across bank switch OK
- NORMAL pad play + pitch bend pot OK
- Serial debug clean, no crash

### Checkpoint B (after Step 6 — ScaleManager early return)
- ARPEG hold-left + scale pads (root/mode/chromatic) OK
- ARPEG hold-left + octave pad OK
- ARPEG hold-left + hold pad toggle OK
- NORMAL hold-left + scale pads with allNotesOff OK
- No phantom edge after bank switch with held pad

### Checkpoint C (after Step 7 + Bug A revert + X2 bootstrap)
- Boot #1 after firmware upload showed `[NVS] Pot filter config seeded with defaults.`
- Boot #2 onwards: all NVS badges "ok" in setup mode menu
- Tool 3 badge "ok" (confirmed after Bug A revert)
- Tool 6 badge "ok" (after X2 bootstrap took effect)
- Full usage cycle: ARPEG/NORMAL/scale change/bank switch/pots/reboot/persistence OK

### Issues discovered during testing (all tracked in known-bugs.md)
- **B-003 DONE**: Stale NVS PotMappingStore (pre-Phase-1) was causing R2/R3 PITCH_BEND swap. Resolved automatically by Commit 7 (PotMappingStore size grew from 36→52, old blob rejected).
- **B-004 TODO**: Tool 6 `d` (reset to defaults) prompt never rendered. Pre-existing. Out of scope.
- **B-005 TODO**: Tool 6 CC# value doesn't scroll visually in editing mode. Pre-existing. Out of scope.
- **B-006 TODO**: Tool 6 no serial log on CC# pot turn. Pre-existing. Out of scope.
- **PotFilterStore bootstrap** (not assigned an ID): Pre-existing hole fixed via X2 in commit `9d2763e` (scope creep, user-approved).

## 7. User preferences observed during Phase 1

These patterns emerged while working with the user (Loïc). Phase 2 implementer should apply them:

1. **Commit messages orientés "why"** — HEREDOC format, 1-2 sentence summary leading with the intent, then bullet list of what. No `Co-Authored-By` line. No emojis.
2. **Present diffs BEFORE applying** for non-trivial edits, wait for explicit OK. For a group of validated similar edits, the user may say "ok for all Phase X" — take that authorization seriously and apply the whole group without asking again.
3. **Build after each logical group** — not after each edit, but after each commit's worth of edits. Read the full output, note RAM/Flash.
4. **Hardware checkpoints are non-negotiable** — STOP, present exact test scenarios, wait for explicit OK. Do NOT batch checkpoints.
5. **Known bugs tracked in `docs/known_issues/known-bugs.md`** with full diagnostic info (file:line, description, suspected cause, resolution path). User reviews these entries and relies on them for future sessions.
6. **No auto-push** — the user approves every push manually. Do NOT push on your own initiative.
7. **Scope strict** — follow the plan. Out-of-scope findings go to `known-bugs.md`. Exceptions require explicit user approval (e.g., the X2 fix in Phase 1).
8. **Read before editing** — the user expects the implementer to read the target file in full, not just apply plan snippets blindly.
9. **Anti-complaisance in diagnostic** — when the user reports a bug, verify claims via code reading, don't assume the plan or a previous session was right. Present the evidence.
10. **French conversation by default**, but technical terms stay in English (bank, slot, bootstrap, etc.). Code comments and commit messages in English.

## 8. NVS state after Phase 1 deploy

If the user flashes Phase 1 firmware on top of a main-branch firmware, the following NVS changes occur at first boot:

- **BankTypeStore** reset to defaults (size 20→28). User re-enters Tool 4 bank types.
- **PotMappingStore** reset to defaults (size 36→52). User re-enters Tool 6 if customized.
- **LedSettingsStore** was also reset on the user's device at Phase 1 Checkpoint B (v2→v3 from commit `aa8990f` on main, unrelated to Phase 1 but observed in the same serial).
- **PotFilterStore** bootstrapped with defaults at first boot (commit `9d2763e`).
- **All other stores** preserved: CalDataStore, NoteMapStore, BankPadStore, ScalePadStore, ArpPadStore, SettingsStore, per-bank scale/velocity/pitchbend/arp pots, tempo, LED brightness, pad sensitivity, color slots.

Phase 2 will NOT touch any NVS structures. Users who already flashed Phase 1 will not need to re-enter anything when flashing Phase 2. Users who skip Phase 1 and flash Phase 2 directly will see the same initial NVS reset behavior.

## 9. Build metrics baseline

End of Phase 1 (commit `5842ae7`):
- RAM: 16.1% (52732 / 327680 bytes)
- Flash: 20.6% (689209 / 3342336 bytes)

Phase 2 adds the LoopEngine class, LoopEvent ring, ArpScheduler registration, main.cpp wiring for LoopEngine tick/processEvents. Expect RAM growth from per-bank LoopEngine instances (probably static arrays sized MAX_LOOP_BANKS=2) and Flash growth from LoopEngine code. If RAM jumps unexpectedly above ~20%, verify you haven't duplicated the pattern MAX_LOOP_BANKS-many times when you should have reused once.

## 10. Pointers

- **Phase 2 plan**: `docs/plans/loop-phase2-engine-wiring.md`
- **Phase 2 plan audit fixes (search for "AUDIT FIX")**: read them before touching code
- **Spec reference**: `docs/plans/loop-plan-1-6-spec.md` (if you don't remember the overall LOOP architecture)
- **Slot drive spec**: `docs/superpowers/specs/2026-04-06-loop-slot-drive-design.md` (Phase 6 prerequisites)
- **Architecture briefing**: `docs/reference/architecture-briefing.md` (runtime data flows, invariants, dirty flags — will need an update in Phase 2 since BankManager/ScaleManager are in the critical path)
- **VT100 design guide**: `docs/reference/vt100-design-guide.md` (Phase 3 only, not Phase 2)
- **NVS reference**: `docs/reference/nvs-reference.md` (just updated in commit `5842ae7`)
- **Known bugs**: `docs/known_issues/known-bugs.md`

Good luck. The code is clean, the tests all passed, B-003 was resolved as a bonus. Phase 2 should build on solid ground.
