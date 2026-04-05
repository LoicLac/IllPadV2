# ToolPotMapping Refactor — Spec (future work)

**Status**: Spec only — not part of LOOP mode implementation phases.
**When**: After LOOP mode is fully shipped and tested.
**Why**: ToolPotMapping currently has a binary `_contextNormal` bool that can't handle 3 contexts.

---

## Current State

```cpp
// ToolPotMapping.h:34
bool _contextNormal;  // true=NORMAL, false=ARPEG
```

Used in 7 places:
- Line 70 (ctor): init to `true`
- Line 95 (`buildPool()`): select NORMAL_PARAMS or ARPEG_PARAMS
- Lines 120-121 (`currentMap()`): select normalMap or arpegMap
- Line 540 (NVS load): loaded
- Line 545 (run entry): reset to true
- Lines 602-607 (defaults): copy appropriate context
- Line 722 (NAV_TOGGLE 't'): binary flip `!_contextNormal`

---

## Required Changes

### 1. Replace bool with index

```cpp
// ToolPotMapping.h
uint8_t _contextIdx;  // 0=NORMAL, 1=ARPEG, 2=LOOP
```

### 2. Add LOOP parameter pool

```cpp
// ToolPotMapping.cpp
static const PotTarget LOOP_PARAMS[] = {
    TARGET_TEMPO_BPM,           // shared global
    TARGET_BASE_VELOCITY,       // shared per-bank
    TARGET_VEL_PATTERN,         // LOOP per-bank
    TARGET_VEL_PATTERN_DEPTH,   // LOOP per-bank
    TARGET_SHUFFLE_DEPTH,       // shared per-bank
    TARGET_SHUFFLE_TEMPLATE,    // shared per-bank
    TARGET_CHAOS,               // LOOP per-bank
    TARGET_VELOCITY_VARIATION   // shared per-bank
};
static const uint8_t LOOP_PARAM_COUNT = 8;
```

### 3. Update buildPool() — 3-way select

```cpp
void ToolPotMapping::buildPool() {
    _poolCount = 0;
    const PotTarget* params;
    uint8_t paramCount;
    switch (_contextIdx) {
        case 0: params = NORMAL_PARAMS; paramCount = NORMAL_PARAM_COUNT; break;
        case 1: params = ARPEG_PARAMS;  paramCount = ARPEG_PARAM_COUNT;  break;
        case 2: params = LOOP_PARAMS;   paramCount = LOOP_PARAM_COUNT;   break;
        default: return;
    }
    for (uint8_t i = 0; i < paramCount && _poolCount < MAX_POOL; i++)
        _pool[_poolCount++] = params[i];
    if (_poolCount < MAX_POOL) _pool[_poolCount++] = TARGET_MIDI_CC;
    if (_poolCount < MAX_POOL) _pool[_poolCount++] = TARGET_MIDI_PITCHBEND;
    if (_poolCount < MAX_POOL) _pool[_poolCount++] = TARGET_EMPTY;
}
```

### 4. Update currentMap() — 3-way select

```cpp
PotMapping* ToolPotMapping::currentMap() {
    switch (_contextIdx) {
        case 0: return _wkMapping.normalMap;
        case 1: return _wkMapping.arpegMap;
        case 2: return _wkMapping.loopMap;
        default: return _wkMapping.normalMap;
    }
}
```

### 5. Update toggle 't' — cyclic

```cpp
// Was: _contextNormal = !_contextNormal;
_contextIdx = (_contextIdx + 1) % 3;
```

### 6. Update control bar label

```cpp
static const char* CTX_NAMES[] = {"NORMAL", "ARPEG", "LOOP"};
static const char* CTX_COLORS[] = {"", VT_CYAN, VT_MAGENTA};

// In control bar:
snprintf(ctrlBuf, sizeof(ctrlBuf),
         VT_DIM "[t] %s%s" VT_RESET VT_DIM "  ..." VT_RESET,
         CTX_COLORS[(_contextIdx + 1) % 3],
         CTX_NAMES[(_contextIdx + 1) % 3]);
```

### 7. Update defaults section

```cpp
// Line ~602: when 'd' pressed, reset current context to defaults
const PotMapping* src;
switch (_contextIdx) {
    case 0: src = DEFAULT_MAPPING.normalMap; break;
    case 1: src = DEFAULT_MAPPING.arpegMap;  break;
    case 2: src = DEFAULT_MAPPING.loopMap;   break;
}
memcpy(currentMap(), src, sizeof(PotMapping) * POT_MAPPING_SLOTS);
```

---

## Dependencies

- PotMappingStore already has `loopMap[8]` (added in Phase 1)
- PotRouter already has LOOP context in rebuildBindings (added in Phase 4)
- LOOP_PARAMS targets already defined (added in Phase 4)

## Scope

This is a pure UI refactor. No runtime behavior changes. The default LOOP mapping already works correctly via PotRouter's DEFAULT_MAPPING — this refactor only enables the user to customize it in Tool 6.

---

## Effort Estimate

~7 edits in ToolPotMapping.cpp, 1 in .h. No new files. No NVS changes.
