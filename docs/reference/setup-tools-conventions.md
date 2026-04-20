# Setup Tools — Behavioral Conventions

Complements [`vt100-design-guide.md`](vt100-design-guide.md) (visual conventions) with **interaction and behavioral** rules for setup tools. Read before adding a new tool or modifying an existing one. Deviation requires documented justification.

> **Authority** : tools that conform to these conventions are considered canonical ; tools that deviate are bugs in waiting. When in doubt, imitate [`ToolPadRoles.cpp`](../../src/setup/ToolPadRoles.cpp) which respects all conventions below.

---

## 1. Save Policy — save at commit, never during navigation

### 1.1 Principle

A save **event** in NVS is a **commit of user intent**, not a stream of keystrokes. Navigation through values (arrow keys, pool cursor moves) must not trigger NVS writes.

### 1.2 Canonical save triggers

| Trigger | Example |
|---|---|
| ENTER in pool/sub-editor (explicit commit) | Tool 3 assigning a role |
| `y` in y/n confirmation prompt | Tool 3 reset-to-defaults, clear-all |
| Explicit dedicated save key (if the tool has one) | None currently in-repo |
| Tool exit only if dirty state is pending (late-commit pattern) | Acceptable for numeric field editors — save once at exit of sub-state, not each `</>` |

### 1.3 Anti-patterns (DO NOT DO)

- Calling `_save()` inside an arrow-key handler
- Calling `NvsManager::saveBlob` inside a value-adjust function that fires on every `<`/`>` press
- "Auto-save after every field change" — this is **not** the Tool 3 pattern even if it looks superficially like it

### 1.4 Why

- `SetupUI::flashSaved()` is **blocking ~300 ms** (see §3 below). Per-arrow calls freeze the tool, freeze the LED comet, queue up input events.
- NVS flash has finite write endurance (~10⁴–10⁵ cycles per sector). A user cycling a 0-127 value would perform ~100 writes in seconds.
- A save per keystroke implicitly means "every keystroke is a commit". It erases the difference between exploration (trying values) and decision (this is the value I want).

### 1.5 Reference implementation

See [`ToolPadRoles.cpp`](../../src/setup/ToolPadRoles.cpp) `run()` loop :
- `saveAll()` is called from exactly 4 sites : pool ENTER with `[---] clear`, pool ENTER with assignment, confirm YES defaults, confirm YES clear-all.
- Arrow-key navigation in grid and in pool never touches NVS.

---

## 2. `flashSaved()` and NVS Cost

### 2.1 Cost

`SetupUI::flashSaved()` (`SetupUI.cpp:231-243`) does :
- 3 iterations × (50 ms reverse-ON + 50 ms reverse-OFF) = **300 ms blocking total**
- `Serial.flush()` between each half-frame (dozens of bytes)
- `LedController::playValidation()` at the end (LED blink animation)

During these 300 ms the tool's `run()` loop cannot iterate → `_leds->update()` is not called → the setup comet freezes → input events buffer in the UART.

### 2.2 Rule

- Call `flashSaved()` **only** on a real NVS commit, and **at most once** per user gesture.
- Never inside a loop that fires per-arrow or per-frame.
- If multiple small edits coalesce into one commit (e.g., "save dirty state on exit"), fire `flashSaved()` once at the commit point.

### 2.3 Rendering note

`flashSaved()` updates only row 2 of the header (the console title bar). It does not redraw the full screen. No need to pair it with `screenDirty = true`.

---

## 3. LED Ownership

### 3.1 `setupComet` owner : `SetupManager`

- Started by [`SetupManager::run`](../../src/setup/SetupManager.cpp) at the top of setup mode (line ~51).
- Persists across all tool invocations. Tools **do not** start or stop it.
- Driven by `_leds->update()` called every frame by each tool's `run()` loop body.

### 3.2 Exception : tools that drive the LEDs for preview

Tool 8 (`ToolLedSettings`) legitimately **stops** the comet during its preview mode (via `previewBegin`). Because of this, the `case '8':` dispatch in [`SetupManager.cpp:126`](../../src/setup/SetupManager.cpp) restarts the comet after `_toolLedSettings.run()` returns.

**No other tool should stop or restart the comet.** If a new tool truly needs to drive LEDs (unusual), it must mirror Tool 8's pattern :
- Stop comet at preview-begin
- Restart comet at preview-end OR
- Document a corresponding restart in the `SetupManager::run` case statement

### 3.3 Anti-pattern

Calling `_leds->startSetupComet()` at `run()` entry is a **bug** — the comet is already running. Calling `_leds->stopSetupComet()` at `run()` exit without a matching restart in `SetupManager` is a **bug** — the main menu loses its comet until another tool re-starts it.

---

## 4. Navigation Paradigms

Three canonical paradigms. Pick the one that matches the parameter's domain shape. Do not mix them in ways that aren't in the reference tools.

### 4.1 Grid cursor (4×12 pad matrix)

- Cursor position = `(row, col)` or flat pad index 0-47.
- Arrow keys move cursor, wrapping or clamping per tool.
- Hardware pad touch → jump cursor to touched pad (see §9).
- ENTER triggers edit sub-state (pool or field editor).

### 4.2 Pool (enum selector)

- Fixed set of **≤ ~12 options per line**, possibly organized in multiple labeled lines.
- Cursor walks the pool with arrow keys (UP/DOWN = line, LEFT/RIGHT = index in line).
- ENTER commits selection, triggers save, and returns to grid.
- Each option is **visually color-coded** per §5 of this doc.

Reference : [`ToolPadRoles`](../../src/setup/ToolPadRoles.cpp) pool with 5 role-category lines.

### 4.3 Field editor (continuous or wide-range value)

- N fields laid out vertically, each with a value range too wide for a pool (e.g., CC# 0-127, ADC thresholds).
- Cursor walks fields with UP/DOWN, adjusts value with `</>`.
- Accelerated stepping (×10) on rapid repeat via `NavEvent.accelerated`.
- **Save on exit of sub-state**, not per adjustment (§1).
- ENTER or `q` returns to grid.

Reference : [`ToolSettings`](../../src/setup/ToolSettings.cpp) (Tool 6) for numeric params.

**Note — relation with §4.4**: §4.3 uses `↑↓` for field navigation (vertical
list of fields) and `←→` for value adjust. §4.4 uses the opposite mapping
because fields are laid out **horizontally on one row** rather than stacked
vertically. Both are "arrows follow the visual layout" — the layout differs.
Tool 6 currently uses §4.3 for its continuous fields. If a future Tool 6
refactor revisits field layout (unlikely Phase 0.1), it should adopt §4.4
only if the fields become multi-column. Otherwise §4.3 remains canonical for
vertical lists.

### 4.4 Multi-value row — geometric visual navigation (Phase 0.1 — Tool 8 canonical)

When a single visual row carries **2+ distinct numeric fields** side by side
(e.g. `brightness 100 %  duration 500 ms`), use the following paradigm:

- `←→` moves **focus** horizontally between the fields on the row (no value change).
- `↑↓` **adjusts** the value of the field currently under focus.
  - Default step: ±1 fine. `ev.accelerated` → ±10 coarse.
  - Each field has its own range + step, looked up via a per-line metadata table.
- `ENTER` commits all fields on the row (one `flashSaved`) and returns to NAV.
- `q` cancels, restoring the full row's pre-edit values from a snapshot backup.

**Geometric principle** (why ←→ = focus, ↑↓ = adjust, contrary to §4.3):
arrows follow the **visual layout of the row**. The row is laid out
horizontally, so horizontal arrows traverse it horizontally (between fields).
Vertical arrows are reserved for "amplitude of value" — the universal
vertical-as-magnitude intuition (up = more, down = less). This gives a
**single mental model across every edit mode in Tool 8**: vertical arrows
always adjust, regardless of paradigm (color, single, multi).

**Exception — color row `[preset] +hue`**: the 2 components belong to the same
logical entity (color = preset + hue shift), so there is no "focus" to cycle.
All 4 arrows act simultaneously on the same entity :
- `←→` cycles the preset (traverse the preset pool).
- `↑↓` cycles the hue offset (adjust magnitude of the hue shift).

Vertical-as-magnitude still holds. Horizontal as "alternate dimension of the
same concept" (preset family) is the degenerate case.

Reference implementation: [`ToolLedSettings`](../../src/setup/ToolLedSettings.cpp)
(Tool 8, Phase 0.1 respec 2026-04-20).

### 4.5 Choice rule

| Parameter domain | Paradigm |
|---|---|
| Enum with ≤ 12 options (mode, preset, color) | Pool (§4.2) |
| Continuous 0-127 / 0-16383 / similar, vertical list | Field editor (§4.3) |
| Multi-value row laid out horizontally | Geometric visual nav (§4.4) |
| Tool has both types | Pool for enums + field editor sub-state for numerics. Never auto-save during field adjust. |

---

## 5. Pool Legend Obligation

### 5.1 Rule

**Any tool whose grid uses semantic color encoding MUST include a pool or legend zone that shows the color → meaning mapping.**

Without this, grid colors are symbols without a key — the user must memorize or enter a sub-state to decode them. This breaks the "VT100 as mini-manual" principle (per project memory `project_vt100_manual.md`).

### 5.2 Minimum legend

If the tool does not use the pool for selection (e.g., edit mode is a field editor, not a pool selector), it still needs a legend row that displays each color variant with its label :

```
╟── LEGEND ──────────────────────────────────────────────────────────╢
║  [yellow-m] MOMENTARY   [magenta-l] LATCH   [orange-z] RET0   ...  ║
```

### 5.3 Reference

[`ToolPadRoles`](../../src/setup/ToolPadRoles.cpp) uses the **same pool** for both roles : (a) legend (always visible, colored per category), (b) selector (when editing). This dual-purpose is elegant and should be imitated when possible.

---

## 6. Standard Keybindings

### 6.1 Reserved (always the same meaning across all tools)

| Key | Meaning |
|---|---|
| `q`, `Q` | Back / cancel / exit (context-sensitive : back to prev sub-state OR exit tool if top-level) |
| `↑ ↓ ← →` | Navigate cursor (grid, pool, field list) |
| `ENTER` | Confirm / commit / enter sub-state |
| `y`, `Y` | Confirm YES (in y/n prompt) |
| `n`, `N`, or any other key | Confirm NO / cancel prompt (loose confirm) |
| `d`, `D` | Reset to defaults (with y/n confirm) |

**Exception — Tool 8 Phase 0.1** : `d` resets the current line to its default
**without y/n confirm** and commits immediately to NVS. The reset is
line-scoped (one numeric field or one color slot), not tool-wide, so the blast
radius is minimal. This deviation is user-validated (rapport Phase 0 §4.3 U9).
Any future tool needing tool-wide `d` reset must restore the y/n confirm.

### 6.2 Conventional (documented shared meaning when used)

| Key | Meaning | Example |
|---|---|---|
| `r`, `R` | Clear all roles / entries (with y/n confirm) | Tool 3 clear-all |
| `t`, `T` | Toggle context / page | Tool 7 NORMAL/ARPEG toggle (Tool 8 Phase 0.1 no longer uses `t` — single-view supersedes the legacy page model) |
| `x`, `X` | Remove individual item (with y/n confirm) | Tool 4 remove pad |

### 6.3 Custom keys

Tool-specific keys must be **displayed in the control bar** (bottom of frame) and appear nowhere else. No hidden shortcuts.

### 6.4 Two-step exit convention

From a nested sub-state (pool editing, field editor, confirmation), `q` returns to the previous level. Exit from top-level (grid navigation) requires a second `q`.

**Implementation note** : snapshot `_uiMode` at the top of each loop iteration ; evaluate exit condition against the snapshot, not against the post-handler value :

```cpp
while (true) {
  NavEvent ev = _input.update();
  UIMode modeAtStart = _uiMode;     // snapshot
  ...
  switch (_uiMode) { /* dispatch may mutate _uiMode */ }

  if (ev.type == NAV_QUIT && modeAtStart == UI_GRID_NAV) break;
  ...
}
```

Without the snapshot, `q` in a sub-state that returns to grid-nav is immediately caught by the exit check and the tool exits in one keypress.

---

## 7. Screen Rendering Discipline

### 7.1 `screenDirty` flag

Every tool uses a `bool screenDirty` (or `_screenDirty`) flag :

1. Any state change that affects visible content sets `screenDirty = true`.
2. Render block at the end of the loop checks `if (screenDirty) { drawScreen(); screenDirty = false; }`.
3. Never render outside the `screenDirty` gate (or a time gate, §7.2).

### 7.2 Time-based refresh (live data)

Tools that display live sensor data (Tool 1 measurement phase, Tool 2 touch detection) may **augment** the dirty flag with a time-based refresh (e.g., every 200 ms). The time gate does not replace the dirty flag ; it complements it for animated data that has no discrete "change event".

### 7.3 Frame atomicity (DEC 2026)

All screen redraws are wrapped in `vtFrameStart()` / `vtFrameEnd()` (DEC 2026 synchronized output). This prevents tearing. Do not emit ANSI escapes outside this wrapper.

---

## 8. Confirmation Sub-States

### 8.1 Inline prompt rule

Confirmations (y/n) **must** render inline in the existing frame, typically in the INFO section :

```
╟── INFO ──────────────────────────────────────────────────────────╢
║  Remove control pad #4? (y/n)                                    ║
```

Control bar should display `CBAR_CONFIRM_ANY` or `CBAR_CONFIRM_STRICT` templates.

### 8.2 Never clear the screen

Confirmations do not clear the screen, do not render a modal block, do not hide the context. The user must still see what they're confirming about (grid, pad, current value).

### 8.3 Parser

Use [`SetupUI::parseConfirm(ev)`](../../src/setup/SetupUI.cpp) which returns `CONFIRM_PENDING / YES / NO`. This centralizes the y/any-key-cancels (loose) semantic. For strict y/n (Tool 1 calibration, reboot), each tool may own its handler.

---

## 9. Hardware Pad Input in Setup

### 9.1 Jump-cursor pattern

When the tool has a 4×12 grid and expects the user to identify a pad, capacitive touch should jump the cursor to the touched pad.

Canonical pattern (from [`ToolPadRoles.cpp:590-606`](../../src/setup/ToolPadRoles.cpp)) :

```cpp
while (true) {
  _leds->update();
  _keyboard->pollAllSensorData();

  int detected = detectActiveKey(*_keyboard, _refBaselines);
  if (detected >= 0) {
    // ... update cursor to detected pad ...
    screenDirty = true;
  }

  NavEvent ev = _input.update();
  ...
}
```

### 9.2 Baselines

Tools capture reference baselines at `run()` entry via [`captureBaselines(*_keyboard, _refBaselines)`](../../src/setup/SetupCommon.h) before the main loop. Without baselines, `detectActiveKey` cannot distinguish touch from noise.

### 9.3 LED feedback

On detected touch, call `SetupUI::showPadFeedback(padIdx)` to flash the corresponding LED. Non-blocking.

---

## 10. Loop Skeleton

Every tool's `run()` function follows this skeleton :

```cpp
void ToolX::run() {
  if (!ctx_valid()) return;

  _load();                             // read NVS into working copy
  _refreshBadge();                     // NVS OK/fail state for header
  reset_ui_state();
  captureBaselines(*_keyboard, _refBaselines);  // if grid-based

  _ui->vtClear();
  bool screenDirty = true;

  while (true) {
    _leds->update();
    _keyboard->pollAllSensorData();

    NavEvent ev = _input.update();
    UIMode modeAtStart = _uiMode;      // snapshot for two-step exit

    // Touch-jump if applicable (§9)
    // Dispatch to handler based on _uiMode
    // Handler may set screenDirty = true or mutate _uiMode

    if (ev.type == NAV_QUIT && modeAtStart == TOP_LEVEL_STATE) {
      _ui->vtClear();
      return;
    }

    if (screenDirty) {
      drawScreen();
      screenDirty = false;
    }

    delay(5);
  }
}
```

### 10.1 Notes

- **No `startSetupComet` / `stopSetupComet`** calls inside the tool (unless Tool-8-style preview, §3.2).
- **No `_save()` inside navigation or value-adjust handlers** — only from commit handlers (§1).
- `delay(5)` at the end of the loop gives FreeRTOS a chance to yield ; required.
- `_ui->vtClear()` before the loop (one-time) clears the menu that was on screen at tool entry.
- `_ui->vtClear()` on exit (just before `return`) clears the tool so the main menu redraws cleanly.

---

## 11. Checklist for a new setup tool

Before marking a new setup tool "done" :

- [ ] Save is triggered only on commit events (§1). Arrows do not save.
- [ ] `flashSaved()` is called at most once per user gesture (§2).
- [ ] No `startSetupComet` / `stopSetupComet` calls unless the tool does LED preview (§3.2).
- [ ] If the tool uses a colored grid, a pool or legend zone is visible explaining the color map (§5).
- [ ] Standard keybindings respected (§6). Custom keys listed in control bar.
- [ ] Two-step exit via snapshot pattern works correctly (§6.4).
- [ ] All state changes set `screenDirty` ; no render outside the flag (§7).
- [ ] Confirmations are inline in INFO, never block the screen (§8).
- [ ] `detectActiveKey` + baseline capture used for pad jump-cursor (§9).
- [ ] Loop skeleton matches §10, including `delay(5)` and `vtClear` at exit.
- [ ] Audited against [`ToolPadRoles.cpp`](../../src/setup/ToolPadRoles.cpp) for convention parity.

---

## 12. History

Created after the V2 Tool 4 audit (2026-04-19) revealed multiple convention violations that had not been caught by spec review or code review :

- Save on every `</>` press (flood NVS + freeze comet 300 ms per keystroke)
- `startSetupComet` / `stopSetupComet` spurious calls
- No pool legend for a colored grid
- Nav paradigm divergent from Tool 3 reference

Root cause : spec claimed "pattern Tool 3 auto-save" from approximate memory, without re-reading Tool 3's save granularity. These behavioral rules were implicit in code but never documented — now they are.

Deviation from this document requires documented justification in the tool's design spec and the deviation pointed out in the code review.
