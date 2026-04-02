# Extreme Shuffle + Gate > 1.0 — Research Notes

## Summary

The arp engine already supports gate > 1.0 (overlap) and reference-counted notes.
The refcount system, event queue, and scheduler are all designed for it.
**But the pot is capped at 1.0** — `adcToFloat()` clamps to [0.0, 1.0].
Unlocking gate > 1.0 is a one-line change that opens up layered note effects.

### Current state
- Gate range: 0.0 — 1.0 (pot hardware limit, not engine limit)
- Shuffle: 5 templates, depth 0.0 — 1.0 (working)
- Overlap: code ready (refcount), never triggered
- Queue: 36 slots per engine, sufficient at gate 1.0, tight at gate > 2.0 with shuffle

### What changes
- Extend gate range to 0.05 — 2.0 (or 3.0 for extreme effects)
- Increase event queue to 48 or 64 slots
- No other code changes needed — refcount, scheduling, processEvents all handle it

---

## How overlap works (existing code, ArpEngine.cpp:339-439)

Each `executeStep()` schedules events with timestamps:

```
noteOnTime  = micros() + shuffleOffset
noteOffTime = noteOnTime + (stepDuration * gateLength)
```

When gate > 1.0, the noteOff of step N fires AFTER the noteOn of step N+1.
Multiple notes ring simultaneously. The refcount system handles same-note collisions:

```
refcount 0→1: send MIDI noteOn    (first instance)
refcount 1→2: silent              (already ringing)
refcount 2→1: silent              (still ringing)
refcount 1→0: send MIDI noteOff   (last instance ends)
```

Different notes at different positions in the sequence simply overlap — no refcount
interaction, both ring independently. This is the desired musical effect.

---

## Timing calculations

All calculations below use: **120 BPM, division 1/16, stepDuration = 125ms**

### Gate = 1.0 (current max) — no real overlap

```
Step 0: ON at T+0ms,     OFF at T+125ms
Step 1: ON at T+125ms,   OFF at T+250ms
Step 2: ON at T+250ms,   OFF at T+375ms
```

Each note dies exactly when the next one starts. Zero overlap window.
With shuffle, the noteOn is delayed but the noteOff moves with it — still no overlap.

### Gate = 1.5 — 2 notes overlap

```
gateUs = 125ms * 1.5 = 187.5ms

Step 0: ON at T+0ms,     OFF at T+187ms
Step 1: ON at T+125ms,   OFF at T+312ms
Step 2: ON at T+250ms,   OFF at T+437ms

T+125ms → T+187ms : notes 0 + 1 ringing (62ms overlap)
T+250ms → T+312ms : notes 1 + 2 ringing (62ms overlap)
```

Consistent 2-note polyphony. Clean, musical, predictable.

### Gate = 2.0 — 2-3 notes overlap

```
gateUs = 125ms * 2.0 = 250ms

Step 0: ON at T+0ms,     OFF at T+250ms
Step 1: ON at T+125ms,   OFF at T+375ms
Step 2: ON at T+250ms,   OFF at T+500ms
Step 3: ON at T+375ms,   OFF at T+625ms

T+0ms   → T+125ms : note 0 only
T+125ms → T+250ms : notes 0 + 1 (2 notes)
T+250ms → T+375ms : notes 1 + 2 (2 notes, note 0 just ended)
```

Steady 2-note overlap. At boundaries, briefly 1 note.

### Gate = 3.0 — 3-4 notes overlap (extreme)

```
gateUs = 125ms * 3.0 = 375ms

Step 0: ON at T+0ms,     OFF at T+375ms
Step 1: ON at T+125ms,   OFF at T+500ms
Step 2: ON at T+250ms,   OFF at T+625ms
Step 3: ON at T+375ms,   OFF at T+750ms

T+250ms → T+375ms : notes 0 + 1 + 2 ringing (3 notes)
T+375ms → T+500ms : notes 1 + 2 + 3 ringing (3 notes, note 0 just ended)
```

Sliding window of 3 simultaneous notes. With different scale positions,
this creates a moving chord that evolves with the arpeggio pattern.

---

## Shuffle + gate > 1.0 combined

This is where it gets interesting. Shuffle displaces noteOn timing unevenly.
Combined with long gate, notes cluster and spread in unpredictable ways.

### Template 3 (Ramp: 0, 25, 50, 75), depth 1.0, gate 2.0, div 1/16, 120 BPM

```
Step 0: shuffle  0% → ON at T+0ms,       OFF at T+250ms
Step 1: shuffle 25% → ON at T+156ms,     OFF at T+406ms
Step 2: shuffle 50% → ON at T+312ms,     OFF at T+562ms
Step 3: shuffle 75% → ON at T+468ms,     OFF at T+718ms
Step 4: shuffle  0% → ON at T+500ms,     OFF at T+750ms   [cycle restarts]
Step 5: shuffle 25% → ON at T+656ms,     OFF at T+906ms

Overlap map:
  T+0ms   → T+156ms  : note 0 alone
  T+156ms → T+250ms  : notes 0 + 1
  T+250ms → T+312ms  : note 1 alone
  T+312ms → T+406ms  : notes 1 + 2
  T+406ms → T+468ms  : note 2 alone
  T+468ms → T+500ms  : notes 2 + 3
  T+500ms → T+562ms  : notes 3 + 4 + (2 fading)  ← 3 notes briefly
```

The ramp shuffle creates an accelerando effect within each 4-step group,
with notes bunching up then spreading out. Gate 2.0 makes them bleed into
each other. The result is a rhythmic canon with variable density.

### Template 4 (Triplet: 0, 66, 33, 66), depth 1.0, gate 2.5, div 1/16, 120 BPM

```
gateUs = 312ms

Step 0: shuffle  0% → ON at T+0ms,       OFF at T+312ms
Step 1: shuffle 66% → ON at T+207ms,     OFF at T+519ms
Step 2: shuffle 33% → ON at T+291ms,     OFF at T+603ms
Step 3: shuffle 66% → ON at T+457ms,     OFF at T+769ms

Overlap map:
  T+207ms → T+312ms  : notes 0 + 1 + 2 cramming in (3 notes, 105ms)
  T+312ms → T+457ms  : notes 1 + 2 (note 0 gone)
  T+457ms → T+519ms  : notes 1 + 2 + 3 (3 notes again, 62ms)
```

The triplet shuffle creates irregular 3-note clusters — almost like
strummed chords that shift in voicing with each cycle.

### Template 0 (Classic swing: 0, 50, 0, 50), depth 1.0, gate 3.0, div 1/8, 140 BPM

```
stepDuration = 2,500,000 * 12 / 140 = 214ms
gateUs = 642ms

Step 0: shuffle  0% → ON at T+0ms,       OFF at T+642ms
Step 1: shuffle 50% → ON at T+321ms,     OFF at T+963ms
Step 2: shuffle  0% → ON at T+428ms,     OFF at T+1070ms
Step 3: shuffle 50% → ON at T+749ms,     OFF at T+1391ms

T+428ms → T+642ms  : notes 0 + 1 + 2 (3 notes, 214ms window)
T+749ms → T+963ms  : notes 1 + 2 + 3 (3 notes, 214ms window)
```

Wide gate + swing = sustained 3-note chords with a heavy swing feel.
Slower divisions make the overlap more dramatic and audible.

---

## Event queue sizing

Each step in flight occupies slots until its noteOff fires:
- Without shuffle: 1 slot (noteOff only, noteOn is immediate)
- With shuffle: 2 slots (noteOn delayed + noteOff)

Slots occupied simultaneously = notes_in_flight * slots_per_note:

| Gate  | Notes in flight | Without shuffle | With shuffle |
|-------|-----------------|-----------------|--------------|
| 1.0   | 1               | 1               | 2            |
| 1.5   | 2               | 2               | 4            |
| 2.0   | 2-3             | 3               | 6            |
| 2.5   | 3               | 3               | 6            |
| 3.0   | 3-4             | 4               | 8            |

With burst catch-up (24-tick guard, div 1/64 worst case = 12 steps burst):

| Gate  | Shuffle | Normal slots | Burst slots (12 steps) | vs 36 | vs 64  |
|-------|---------|--------------|------------------------|-------|--------|
| 1.0   | yes     | 2            | 24                     | 67%   | 38%    |
| 2.0   | yes     | 6            | 6 + 24 = 30            | 83%   | 47%    |
| 3.0   | yes     | 8            | 8 + 24 = 32            | 89%   | 50%    |
| 3.0   | yes     | 8            | 8 + 24 = 32 (burst+overlap) | **89%** | **50%** |

At 64 slots: even gate 3.0 + shuffle + worst burst = 50% occupancy. Safe margin.
At 36 slots: gate 3.0 + burst = 89%, dangerously close to drops.

**Recommendation: 64 slots if gate goes up to 3.0.**
Cost: 64 * 8 bytes * 4 engines = 2KB. Negligible on 320KB SRAM.

---

## What to change (code)

### 1. Gate range (PotRouter.cpp, 1 line)

Current: `adcToFloat()` returns 0.0 — 1.0
Target: 0.05 — 2.0 (or 3.0)

Options for the mapping curve:
- **Linear 0.05 — 2.0**: simple, uniform resolution across the range
- **Linear 0.05 — 3.0**: maximum range, less precision in the musical zone (0.1 — 1.0)
- **Piecewise**: first 75% of pot = 0.05 — 1.0 (musical), last 25% = 1.0 — 3.0 (extreme)

The piecewise approach gives fine control in the normal zone and
access to overlap territory in the last quarter-turn of the pot.

### 2. Event queue size (ArpEngine.h, 1 line)

```
static const uint8_t MAX_PENDING_EVENTS = 64;  // was 36
```

### 3. NVS storage (KeyboardData.h)

Gate is stored as float in the ArpPotStore per bank. The float already
supports > 1.0 — no struct change needed. But NVS version should bump
if the expected range changes (so defaults are rewritten on first boot).

### 4. Pot bargraph display

The bargraph shows pot level as a bar on 8 LEDs. Currently 0.0 — 1.0 fills
all 8 LEDs. With extended range, options:
- Scale bar to new range (gate 1.0 = LED 4, gate 2.0 = LED 8) — loses precision
- Keep bar as-is, LEDs fully lit = gate 1.0, blink pattern above 1.0 — visual distinction
- Two-color: green below 1.0, amber/red above — immediate feedback on overlap zone

### 5. Tool 4 / Setup display (optional)

Show gate range in bank config so the user knows what "extreme" looks like.

---

## Musical territory unlocked

| Gate   | Shuffle depth | Character                                          |
|--------|---------------|----------------------------------------------------|
| 0.1    | 0.0           | Staccato arpeggio, clean                           |
| 0.5    | 0.0           | Standard arp, notes breathe                        |
| 1.0    | 0.0           | Legato, notes touch end-to-end                     |
| 1.0    | 0.5-1.0       | Swing/groove, no overlap, rhythmic displacement    |
| **1.3** | 0.0          | **Light overlap — 2 notes bleed, pad-like**        |
| **1.5** | 0.5          | **Groovy overlap — swing + 2-note sustain**        |
| **2.0** | 0.0          | **Steady 2-note canon, shifting harmony**          |
| **2.0** | 1.0          | **Rhythmic clusters, 2-3 notes bunching**          |
| **3.0** | 0.0          | **3-note sliding chord, dense texture**            |
| **3.0** | 1.0          | **Chaotic cluster arp, maximum density**            |

The combination of pattern (Up/Down/Random/Order), octave range (1-4),
scale mode, gate > 1.0, and shuffle template/depth creates a massive
parameter space for generative and textural effects that no commercial
arpeggiator offers in this form.

---

## Bipolar Shuffle — notes that rush AND drag

### The problem

Current shuffle only delays notes (positive %). The `if (pct > 0)` guard
at ArpEngine.cpp:395 blocks negative values. But the template array is
`int8_t` — it already supports -128 to +127. The capability is there,
just gated off.

Real musicians don't just drag notes — they **rush** certain beats.
A jazz player anticipates the 2 and 4. A drummer pushes into fills.
The tension between early and late notes is what makes groove feel alive.

### How it works (Option C — signed offset, fire-from-past)

The noteOn is scheduled with a signed offset. If negative, `fireTimeUs`
is in the past. `processEvents()` already uses `(int32_t)(nowUs - fireTimeUs) >= 0`,
so past events fire immediately at the next loop iteration (~1ms later).

The perceived anticipation: if template says -50% at 1/16 120 BPM,
the note "should" arrive 62ms early. It actually arrives ~1ms after
scheduling. The musician hears it 61ms early instead of 62ms. Inaudible
difference.

```
int32_t offsetUs = (int32_t)((float)pct * depth * (float)stepDurationUs / 100.0f);
uint32_t noteOnTime  = nowUs + offsetUs;       // wraps correctly (unsigned + signed)
uint32_t noteOffTime = noteOnTime + gateUs;

// Safety: if noteOff ended up in the past (short gate + big negative offset),
// clamp to minimum 1ms from now
if ((int32_t)(noteOffTime - nowUs) < 1000) noteOffTime = nowUs + 1000;
```

### Edge case: noteOff in the past

With gate 0.3 and offset -75% at 1/16 120 BPM:
```
noteOnTime  = now - 93ms    (past)
noteOffTime = (now - 93ms) + 37ms = now - 56ms  (ALSO past!)
```

Both fire at the same processEvents() call — zero-length note.
The guard above clamps noteOffTime to `now + 1ms`. The note plays
for 1ms minimum. Musically: a click/ghost note, which is actually
a valid percussive effect at extreme settings.

### Code change: ~10 lines in executeStep()

1. Remove `if (pct > 0)` guard
2. Change `uint32_t shuffleOffsetUs` → `int32_t shuffleOffsetUs`
3. Compute offset for any non-zero pct (positive or negative)
4. Add noteOff past-time guard
5. When offset != 0 (positive OR negative): schedule in queue
6. When offset == 0: fire immediate (existing optimization, unchanged)

No changes needed in processEvents(), scheduleEvent(), or refcount logic.

### Bipolar template examples

```
// Current templates (positive only, unchanged):
0: Classic swing    {0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50, 0, 50}
1: Light/heavy      {0, 33, 0, 66, 0, 33, 0, 66, 0, 33, 0, 66, 0, 33, 0, 66}
2: Backbeat push    {0, 0, 50, 0, 0, 0, 50, 0, 0, 0, 50, 0, 0, 0, 50, 0}
3: Ramp             {0, 25, 50, 75, 0, 25, 50, 75, 0, 25, 50, 75, 0, 25, 50, 75}
4: Triplet feel     {0, 66, 33, 66, 0, 66, 33, 66, 0, 66, 33, 66, 0, 66, 33, 66}

// New bipolar templates:
5: Push-pull        {-25, 25, -25, 25, -25, 25, -25, 25, -25, 25, -25, 25, -25, 25, -25, 25}
6: Jazz anticipation{0, -20, -30, -15, 0, -20, -30, -15, 0, -20, -30, -15, 0, -20, -30, -15}
7: Drunken          {-15, 40, -30, 60, -10, 50, -40, 20, -15, 40, -30, 60, -10, 50, -40, 20}
8: Tension build    {0, 0, -10, -20, 0, 0, -20, -40, 0, 0, -30, -60, 0, 0, -10, -20}
9: Human micro      {-5, 8, -3, 12, -7, 4, -2, 15, -8, 6, -4, 10, -6, 3, -1, 11}
```

**Push-pull**: every note alternates early/late. Jittery, nervous energy.
**Jazz anticipation**: downbeats on grid, off-beats arrive early. Classic jazz phrasing.
**Drunken**: asymmetric, some early some very late. Stumbling, unpredictable groove.
**Tension build**: progressive anticipation within each 4-step group, resets.
**Human micro**: subtle ±5-15%, simulates micro-timing imperfections of a real player.

### Musical territory with bipolar + gate > 1.0

| Gate  | Template          | Depth | Character                                      |
|-------|-------------------|-------|------------------------------------------------|
| 1.0   | Jazz anticipation | 0.5   | Tight arp with forward-leaning groove          |
| 1.5   | Push-pull         | 1.0   | Notes jitter around grid, 2-note overlap       |
| 2.0   | Drunken           | 0.7   | Chaotic overlap, notes pile up unpredictably   |
| 2.0   | Human micro       | 1.0   | Organic feel, subtle overlap, "played by hand" |
| 3.0   | Tension build     | 1.0   | Clusters that tighten then release             |

---

## Tool 8 — Shuffle Template Editor (new setup tool)

User-editable shuffle templates via the VT100 setup terminal.
The 5 built-in templates stay as presets. User templates are stored in NVS.

### Concept

```
┌─────────────────────────────────────────────────────────┐
│  SHUFFLE TEMPLATE EDITOR                        [t] 6   │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  Template:  ◄ 6: User A ►                               │
│                                                         │
│  Step:  1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16  │
│       [ 0][-20][-30][-15][ 0][-20][-30][-15][ 0][-20][-30][-15][ 0][-20][-30][-15]│
│                                                         │
│         ·       ▼   ▼▼  ▼         ▼   ▼▼  ▼         ▼   ▼▼  ▼         ▼   ▼▼  ▼  │
│  ──────────────────────── grid ────────────────────────  │
│         ·   ▲       ·         ▲       ·         ▲       ·         ▲       │
│                                                         │
│  ◄ ► select step    ▲ ▼ adjust value (-99..+99)         │
│  [p] preview (tap tempo)    [c] copy template           │
│  [r] reset to zeros         [d] set as default          │
│  [i] import preset (0-4)    [q] save & exit             │
│                                                         │
│  ── Presets ──                                          │
│  0: Classic swing   1: Light/heavy   2: Backbeat push   │
│  3: Ramp            4: Triplet feel                     │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### UX flow

1. `< >` to select template slot (5 presets read-only + N user slots)
2. `← →` to move cursor across the 16 steps
3. `▲ ▼` to adjust value (-99 to +99) for selected step
4. Visual: steps above grid line = delayed (▼ arrows), below = early (▲ arrows)
5. `p` = tap-tempo preview: plays the template at current BPM via the foreground arp
6. `i` = import a preset as starting point for editing
7. `q` = save to NVS and exit

### Storage

New NVS namespace `illpad_shuf`:
- N user templates × 16 bytes (int8_t[16]) + name (8 chars) + magic/version
- Template selection per bank stored in existing `illpad_apot` (shuffleTemplate field
  already exists, just extend range: 0-4 = presets, 5+ = user)

### Pot integration

The pot (hold+right3) currently selects from 5 discrete values.
Extend to 5 + N user templates. The discrete mapping in PotRouter
already supports arbitrary counts — just change the divisor.

### Preview mode

When `p` is pressed, the tool sends the current template's groove
via the foreground arp engine at the current BPM and division,
using a simple ascending 4-note pattern. The musician hears the
groove immediately without leaving setup mode. This is critical for
designing templates by ear rather than by numbers.


