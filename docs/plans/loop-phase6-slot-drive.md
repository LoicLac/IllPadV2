# LOOP Mode — Phase 6: Slot Drive (16 persistent slots via LittleFS)

**Goal**: Implement the LOOP Slot Drive — a 16-slot persistent storage system for LOOP recordings, accessible via hold-left + slot pad gestures, backed by LittleFS in a custom flash partition.

**Prerequisite**: Phases 1-5 applied. Phase 1 has the structural prereqs (`LoopPadStore.slotPads[16]`, `LOOPPAD_VERSION 2`, slot constants in HardwareConfig.h). Phase 3 has the Tool 3 refactor toward the b1 contextual architecture (3 sub-pages).

**Design reference**: this plan implements the spec at `docs/superpowers/specs/2026-04-06-loop-slot-drive-design.md`. Each step references the relevant spec section.

---

## Overview

This phase adds:

1. **Custom partition table** (`partitions_illpad.csv`) with a 512 KB LittleFS partition for the slot drive
2. **`LoopSlotStore`** — a new class encapsulating all LittleFS interaction (mount, save, load, delete, occupancy bitmask)
3. **`LoopEngine::serializeToBuffer / deserializeFromBuffer`** — extension of the engine to convert state to/from a binary buffer (with hard-cut + beat quantize on PLAYING load)
4. **`LedController` extensions** — 4 new confirms (LOAD/SAVE/REFUSE/DELETE) + `showSlotSaveRamp()`
5. **`handleLoopSlots()` in main.cpp** — the gesture handler (rising edge / long press / falling edge / delete combo with `s_clearConsumedByCombo` coordination)
6. **`ToolPadRoles` LOOP sub-page extension** — 16 slot pad lines added to the LOOP context's pool
7. **Boot sequence reorder** — LittleFS becomes step 1, the LED hardware step is removed (silent prerequisite)
8. **Manual + VT100 documentation updates**

---

## Step 1 — Custom partition table for LittleFS

> **DESIGN REF**: spec §1.5 (LittleFS choice), §2.7 (boot sequence).

### 1a. Create `partitions_illpad.csv` at the project root

```csv
# Name,    Type, SubType, Offset,   Size,     Flags
nvs,       data, nvs,     0x9000,   0x5000,
otadata,   data, ota,     0xe000,   0x2000,
app0,      app,  ota_0,   0x10000,  0x340000,
app1,      app,  ota_1,   0x350000, 0x340000,
spiffs,    data, spiffs,  0x690000, 0x96000,
loopfs,    data, spiffs,  0x726000, 0x80000,
coredump,  data, coredump,0x7A6000, 0x10000,
```

**Layout rationale**:
- `nvs` (20 KB) — unchanged from default
- `otadata` (8 KB) — unchanged
- `app0` / `app1` (3.25 MB each) — OTA-ready application slots, slightly smaller than default to make room
- `spiffs` (600 KB) — preserved general-purpose data partition, in case future tools need it
- `loopfs` (512 KB) — **new dedicated LittleFS partition for the slot drive**
- `coredump` (64 KB) — at the very end, unchanged

**Why a separate `loopfs` partition** instead of reusing the default `spiffs`: isolation. If the loop drive grows or shrinks in the future, only this partition needs resizing — the general-purpose spiffs is untouched. Mount failure of one does not affect the other.

> **AUDIT NOTE**: subtype `spiffs` is used for both spiffs and littlefs partitions on ESP32-Arduino. The actual filesystem driver is selected by the code that mounts it (`LittleFS.begin()` vs `SPIFFS.begin()`).

### 1b. Update `platformio.ini`

Add at the end of the `[env:esp32-s3-devkitc-1]` section:

```ini
board_build.partitions = partitions_illpad.csv
```

### 1c. Add LittleFS to `lib_deps`

LittleFS is bundled with the ESP32-Arduino core (it's not a separate library), so **no `lib_deps` change is needed**. The header is `#include <LittleFS.h>`.

> **AUDIT NOTE**: verify LittleFS is available in your installed `espressif32` platform version. As of espressif32 6.x, LittleFS is part of the core. If your platform is older, add `LittleFS_esp32` to lib_deps.

### Build verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected: clean build. The new partition table is loaded but no code uses LittleFS yet.

### Test verification

1. Flash the firmware. The first flash after partition resize **erases the entire flash** including NVS — this is expected per the Zero Migration Policy.
2. After reflash, all user-stored values reset to defaults (calibration, pad order, bank config, etc.). The serial debug output should show NVS load failures with default fallback messages — this is the expected reset behavior.
3. All NORMAL/ARPEG/LOOP behavior remains identical (the partition table change has no runtime impact yet).

---

## Step 2 — Create `LoopSlotStore` class

> **DESIGN REF**: spec §2.1 (interface), §2.2 (file format).

### 2a. Create `src/loop/LoopSlotStore.h`

```cpp
#ifndef LOOP_SLOT_STORE_H
#define LOOP_SLOT_STORE_H

#include <stdint.h>
#include "../core/HardwareConfig.h"

class LoopEngine;
class MidiTransport;

// =================================================================
// LoopSlotHeader — 32-byte file header for /loops/slotNN.lpb
// =================================================================
// Layout matches spec §2.2. Total = 32 bytes (room for future fields).

#define LOOP_SLOT_MAGIC    0x1F00   // distinct from EEPROM_MAGIC=0xBEEF, COLOR_SLOT_MAGIC=0xC010
#define LOOP_SLOT_VERSION  1

struct LoopSlotHeader {
  uint16_t magic;              // LOOP_SLOT_MAGIC
  uint8_t  version;            // LOOP_SLOT_VERSION
  uint8_t  reserved;
  uint16_t eventCount;         // number of LoopEvent entries that follow
  uint16_t loopBars;           // _loopLengthBars
  float    recordBpm;          // _recordBpm
  uint16_t shuffleDepthRaw;    // 0-4095
  uint8_t  shuffleTemplate;    // 0-9
  uint8_t  velPatternIdx;      // 0-3
  uint16_t chaosRaw;           // 0-4095
  uint16_t velPatternDepthRaw; // 0-4095
  uint8_t  baseVelocity;       // 1-127
  uint8_t  velocityVariation;  // 0-100
  uint8_t  reserved2[8];       // padding to 32 bytes (room for future fields)
};
static_assert(sizeof(LoopSlotHeader) == 32, "LoopSlotHeader must be 32 bytes");

// =================================================================
// LoopSlotStore — encapsulates all LittleFS interaction
// =================================================================

class LoopSlotStore {
public:
  static const uint8_t SLOT_COUNT = LOOP_SLOT_COUNT;   // = 16, defined in HardwareConfig.h

  LoopSlotStore();

  // Mount LittleFS, scan occupancy, cleanup .tmp orphans.
  // Returns false if mount fails.
  bool begin();

  // O(1) — bitmask cached
  bool isSlotOccupied(uint8_t slotIdx) const;

  // Save current LoopEngine state to slot. Returns false on:
  //  - eventCount == 0 (refus EMPTY)
  //  - LittleFS write failure
  //  - slot index out of range
  bool saveSlot(uint8_t slotIdx, const LoopEngine& eng);

  // Load slot into LoopEngine. Hard-cut + quantize-snap if PLAYING.
  // Returns false if slot is missing/corrupt.
  // On corruption, marks slot as unoccupied in the bitmask.
  // currentBPM is the live tempo (NOT _recordBpm) — required for the
  // PLAYING quantize-snap to align _playStartUs on the live beat.
  // AUDIT FIX B4 2026-04-06.
  bool loadSlot(uint8_t slotIdx, LoopEngine& eng,
                MidiTransport& transport, uint32_t globalTick,
                float currentBPM);

  // Delete slot file. Returns true on success or if slot already empty.
  bool deleteSlot(uint8_t slotIdx);

  bool isMounted() const { return _mounted; }

private:
  bool     _mounted;
  uint16_t _occupancyBitmask;   // bit i set = slot i occupied

  // Compose path string into a fixed buffer (no malloc).
  // Result format: "/littlefs/loops/slot05.lpb" or "/littlefs/loops/slot05.tmp"
  // Buffer is 32 bytes to comfortably fit the full path + nul terminator
  // (17 chars base + NUL = 18, rounded up to 32 for alignment/future headroom).
  static void slotPath(uint8_t slotIdx, bool tmp, char out[32]);

  void rescanOccupancy();
  void cleanupOrphanTmpFiles();

  // Static reusable serialization buffer (one shared instance).
  // Size = 32 (header) + 1024 × 8 (max events) = 8224 bytes.
  static uint8_t _serializeBuffer[8224];
};

#endif // LOOP_SLOT_STORE_H
```

### 2b. Create `src/loop/LoopSlotStore.cpp`

```cpp
#include "LoopSlotStore.h"
#include "LoopEngine.h"
#include "../core/MidiTransport.h"
#include "../core/HardwareConfig.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

// Static buffer instance
uint8_t LoopSlotStore::_serializeBuffer[8224];

LoopSlotStore::LoopSlotStore()
  : _mounted(false)
  , _occupancyBitmask(0)
{
}

void LoopSlotStore::slotPath(uint8_t slotIdx, bool tmp, char out[32]) {
  // AUDIT FIX (Q3, 2026-04-06): mount at /littlefs, not /. Prevents future
  // collision if SPIFFS (or any other FS) is mounted at /. Matches the
  // ESP32-Arduino convention. Path format: "/littlefs/loops/slotNN.lpb"
  //
  // VERIFY ON BUILD (Q1, 2026-04-06 pass 2): the LittleFS API on espressif32
  // platforms may interpret file paths in two different ways depending on the
  // version installed:
  //   - Some versions use the basePath prefix (this code: "/littlefs/loops/...")
  //   - Other versions use paths relative to the partition root ("/loops/...")
  // If the FIRST hardware test of saveSlot/loadSlot/exists fails with "file
  // not found" or "open failed" despite the partition being mounted, the most
  // likely cause is that this version of the platform expects the relative
  // form. In that case, change the format string here from
  //   "/littlefs/loops/slot%02u.%s"
  // to
  //   "/loops/slot%02u.%s"
  // and update the corresponding `LittleFS.exists("/littlefs/loops")` /
  // `LittleFS.mkdir("/littlefs/loops")` calls in begin() to drop the prefix.
  // Test on the actual installed espressif32 version before assuming.
  snprintf(out, 32, "/littlefs/loops/slot%02u.%s", slotIdx, tmp ? "tmp" : "lpb");
}

bool LoopSlotStore::begin() {
  if (_mounted) return true;

  // Mount LittleFS on the loopfs partition at /littlefs (Q3 fix).
  // The partition label is "loopfs" per partitions_illpad.csv (Step 1a).
  // LittleFS.begin(formatOnFail=true, basePath="/littlefs", maxOpenFiles=10, partitionLabel="loopfs")
  if (!LittleFS.begin(true, "/littlefs", 10, "loopfs")) {
    #if DEBUG_SERIAL
    Serial.println("[SLOT] FATAL: LittleFS mount failed on loopfs partition");
    #endif
    return false;
  }

  // Ensure /littlefs/loops directory exists
  if (!LittleFS.exists("/littlefs/loops")) {
    LittleFS.mkdir("/littlefs/loops");
  }

  cleanupOrphanTmpFiles();
  rescanOccupancy();

  _mounted = true;
  #if DEBUG_SERIAL
  Serial.printf("[SLOT] LittleFS mounted at /littlefs, occupancy=0x%04X\n", _occupancyBitmask);
  #endif
  return true;
}

void LoopSlotStore::rescanOccupancy() {
  _occupancyBitmask = 0;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    char path[32];   // Q3 fix: 32 bytes to fit /littlefs/loops/slotNN.lpb
    slotPath(i, false, path);
    if (LittleFS.exists(path)) {
      _occupancyBitmask |= (uint16_t)(1U << i);
    }
  }
}

void LoopSlotStore::cleanupOrphanTmpFiles() {
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    char path[32];   // Q3 fix: 32 bytes
    slotPath(i, true, path);
    if (LittleFS.exists(path)) {
      LittleFS.remove(path);
      #if DEBUG_SERIAL
      Serial.printf("[SLOT] Cleaned orphan tmp: %s\n", path);
      #endif
    }
  }
}

bool LoopSlotStore::isSlotOccupied(uint8_t slotIdx) const {
  if (slotIdx >= SLOT_COUNT) return false;
  return (_occupancyBitmask & (uint16_t)(1U << slotIdx)) != 0;
}

bool LoopSlotStore::saveSlot(uint8_t slotIdx, const LoopEngine& eng) {
  if (!_mounted || slotIdx >= SLOT_COUNT) return false;

  // Serialize engine state to the static buffer
  size_t bytesWritten = eng.serializeToBuffer(_serializeBuffer, sizeof(_serializeBuffer));
  if (bytesWritten == 0) {
    // serializeToBuffer returns 0 on EMPTY (eventCount == 0) or buffer too small
    #if DEBUG_SERIAL
    Serial.printf("[SLOT] saveSlot(%u) refused: serializeToBuffer returned 0\n", slotIdx);
    #endif
    return false;
  }

  char tmpPath[32], finalPath[32];   // Q3 fix: 32 bytes for /littlefs/loops/...
  slotPath(slotIdx, true, tmpPath);
  slotPath(slotIdx, false, finalPath);

  // Write to .tmp first
  File f = LittleFS.open(tmpPath, "w");
  if (!f) {
    #if DEBUG_SERIAL
    Serial.printf("[SLOT] saveSlot(%u): failed to open %s for write\n", slotIdx, tmpPath);
    #endif
    return false;
  }
  size_t written = f.write(_serializeBuffer, bytesWritten);
  f.close();
  if (written != bytesWritten) {
    #if DEBUG_SERIAL
    Serial.printf("[SLOT] saveSlot(%u): partial write (%u/%u)\n", slotIdx, (unsigned)written, (unsigned)bytesWritten);
    #endif
    LittleFS.remove(tmpPath);
    return false;
  }

  // Atomic rename .tmp -> .lpb
  // If finalPath already exists, LittleFS.rename will fail — remove first.
  if (LittleFS.exists(finalPath)) {
    LittleFS.remove(finalPath);
  }
  if (!LittleFS.rename(tmpPath, finalPath)) {
    #if DEBUG_SERIAL
    Serial.printf("[SLOT] saveSlot(%u): rename failed\n", slotIdx);
    #endif
    LittleFS.remove(tmpPath);
    return false;
  }

  _occupancyBitmask |= (uint16_t)(1U << slotIdx);
  #if DEBUG_SERIAL
  Serial.printf("[SLOT] saveSlot(%u): %u bytes written\n", slotIdx, (unsigned)bytesWritten);
  #endif
  return true;
}

bool LoopSlotStore::loadSlot(uint8_t slotIdx, LoopEngine& eng,
                              MidiTransport& transport, uint32_t globalTick,
                              float currentBPM) {
  if (!_mounted || slotIdx >= SLOT_COUNT) return false;
  if (!isSlotOccupied(slotIdx)) return false;

  char path[32];
  slotPath(slotIdx, false, path);

  File f = LittleFS.open(path, "r");
  if (!f) {
    #if DEBUG_SERIAL
    Serial.printf("[SLOT] loadSlot(%u): file open failed\n", slotIdx);
    #endif
    _occupancyBitmask &= (uint16_t)~(1U << slotIdx);   // mark unoccupied
    return false;
  }
  size_t fileSize = f.size();
  if (fileSize > sizeof(_serializeBuffer)) {
    #if DEBUG_SERIAL
    Serial.printf("[SLOT] loadSlot(%u): file too large (%u > %u)\n", slotIdx,
                  (unsigned)fileSize, (unsigned)sizeof(_serializeBuffer));
    #endif
    f.close();
    _occupancyBitmask &= (uint16_t)~(1U << slotIdx);
    return false;
  }
  size_t readBytes = f.read(_serializeBuffer, fileSize);
  f.close();
  if (readBytes != fileSize) {
    #if DEBUG_SERIAL
    Serial.printf("[SLOT] loadSlot(%u): short read (%u/%u)\n", slotIdx,
                  (unsigned)readBytes, (unsigned)fileSize);
    #endif
    _occupancyBitmask &= (uint16_t)~(1U << slotIdx);
    return false;
  }

  // Deserialize into the engine. The engine validates magic/version itself.
  // currentBPM is forwarded for the PLAYING quantize-snap (B4 fix).
  if (!eng.deserializeFromBuffer(_serializeBuffer, fileSize, transport, globalTick, currentBPM)) {
    #if DEBUG_SERIAL
    Serial.printf("[SLOT] loadSlot(%u): deserialize failed (corrupt)\n", slotIdx);
    #endif
    _occupancyBitmask &= (uint16_t)~(1U << slotIdx);
    return false;
  }

  #if DEBUG_SERIAL
  Serial.printf("[SLOT] loadSlot(%u): %u bytes read OK\n", slotIdx, (unsigned)fileSize);
  #endif
  return true;
}

bool LoopSlotStore::deleteSlot(uint8_t slotIdx) {
  if (!_mounted || slotIdx >= SLOT_COUNT) return false;

  char path[32];   // Q3 fix: 32 bytes
  slotPath(slotIdx, false, path);

  if (LittleFS.exists(path)) {
    LittleFS.remove(path);
  }
  _occupancyBitmask &= (uint16_t)~(1U << slotIdx);
  #if DEBUG_SERIAL
  Serial.printf("[SLOT] deleteSlot(%u) OK\n", slotIdx);
  #endif
  return true;
}
```

### Build verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected: clean build. The class is defined but not yet instantiated.

### Test verification

No runtime change yet — code compiled but not called.

---

## Step 3 — Extend `LoopEngine` with serialize / deserialize

> **DESIGN REF**: spec §2.3 (serialize/deserialize semantics, hard-cut + quantize-snap on PLAYING).

### 3a. Add public method declarations to `src/loop/LoopEngine.h`

In the public section, after the existing setters:

```cpp
  // === Serialization (Phase 6 - LOOP slot drive) ===
  // Serialize the current state to a pre-allocated buffer.
  // bufSize must be >= sizeof(LoopSlotHeader) + _eventCount * sizeof(LoopEvent).
  // Returns: number of bytes written, or 0 on error
  // (eventCount == 0 = refusal, buffer too small).
  size_t serializeToBuffer(uint8_t* buf, size_t bufSize) const;

  // Deserialize from buffer. Hard-cut: flushes refcount + pending queue,
  // replaces events. If _state == PLAYING, applies quantize-snap to align
  // _playStartUs on the last beat USING THE LIVE BPM (currentBPM), not the
  // recorded BPM — so the snap aligns with the current playback grid.
  // EMPTY → STOPPED. STOPPED stays STOPPED.
  // Returns: true if OK, false if magic/version mismatch.
  // AUDIT FIX B4 2026-04-06: currentBPM parameter added.
  bool deserializeFromBuffer(const uint8_t* buf, size_t bufSize,
                              MidiTransport& transport, uint32_t globalTick,
                              float currentBPM);

  // === Velocity getters for slot load writeback (AUDIT FIX B5 2026-04-06) ===
  // After deserializeFromBuffer loads a slot, _baseVelocity and
  // _velocityVariation hold the slot's stored values. handleLoopSlots reads
  // them via these getters and writes them into BankSlot.baseVelocity /
  // velocityVariation BEFORE calling reloadPerBankParams. This implements
  // the "preset complet" semantics from Q2: loading a slot also restores
  // its velocity params on the bank.
  uint8_t getBaseVelocity() const      { return _baseVelocity; }
  uint8_t getVelocityVariation() const { return _velocityVariation; }
```

### 3b. Implement `serializeToBuffer` in `src/loop/LoopEngine.cpp`

Add at the end of the file:

```cpp
#include "LoopSlotStore.h"   // for LoopSlotHeader + LOOP_SLOT_MAGIC + LOOP_SLOT_VERSION

size_t LoopEngine::serializeToBuffer(uint8_t* buf, size_t bufSize) const {
  // Refuse empty loops — saving an EMPTY state has no musical value.
  if (_eventCount == 0) return 0;

  size_t needed = sizeof(LoopSlotHeader) + (size_t)_eventCount * sizeof(LoopEvent);
  if (bufSize < needed) return 0;

  // Fill header
  LoopSlotHeader hdr;
  memset(&hdr, 0, sizeof(hdr));
  hdr.magic              = LOOP_SLOT_MAGIC;
  hdr.version            = LOOP_SLOT_VERSION;
  hdr.eventCount         = _eventCount;
  hdr.loopBars           = _loopLengthBars;
  hdr.recordBpm          = _recordBpm;
  hdr.shuffleDepthRaw    = (uint16_t)(_shuffleDepth * 4095.0f);
  hdr.shuffleTemplate    = _shuffleTemplate;
  hdr.velPatternIdx      = _velPatternIdx;
  hdr.chaosRaw           = (uint16_t)(_chaosAmount * 4095.0f);
  hdr.velPatternDepthRaw = (uint16_t)(_velPatternDepth * 4095.0f);
  hdr.baseVelocity       = _baseVelocity;
  hdr.velocityVariation  = _velocityVariation;

  memcpy(buf, &hdr, sizeof(hdr));
  memcpy(buf + sizeof(hdr), _events, (size_t)_eventCount * sizeof(LoopEvent));

  return needed;
}
```

### 3c. Implement `deserializeFromBuffer` in `src/loop/LoopEngine.cpp`

Add right after `serializeToBuffer`:

```cpp
bool LoopEngine::deserializeFromBuffer(const uint8_t* buf, size_t bufSize,
                                        MidiTransport& transport,
                                        uint32_t globalTick,
                                        float currentBPM) {
  // Defense in depth: refuse to deserialize while a recording session is in
  // progress. The current sole caller (handleLoopSlots) already enforces this
  // via its `recording` early-return guard, but a future caller (debug, web UI,
  // MIDI sysex import) might not. Without this guard, deserialize would
  // hard-flush the active notes and replace the events buffer mid-recording,
  // silently corrupting the session and orphaning held pads (their _liveNote[]
  // entries would still point to MIDI notes that no longer exist in any
  // playing buffer). AUDIT FIX B2 2026-04-06 pass 2.
  if (_state == RECORDING || _state == OVERDUBBING) {
    #if DEBUG_SERIAL
    Serial.println("[LOOP] deserializeFromBuffer refused: recording in progress");
    #endif
    return false;
  }

  // Validate header presence
  if (bufSize < sizeof(LoopSlotHeader)) return false;

  LoopSlotHeader hdr;
  memcpy(&hdr, buf, sizeof(hdr));

  // Validate magic + version
  if (hdr.magic != LOOP_SLOT_MAGIC) return false;
  if (hdr.version != LOOP_SLOT_VERSION) return false;

  // Validate eventCount + buffer size consistency
  if (hdr.eventCount > MAX_LOOP_EVENTS) return false;
  size_t expectedSize = sizeof(LoopSlotHeader) + (size_t)hdr.eventCount * sizeof(LoopEvent);
  if (bufSize < expectedSize) return false;

  // ---- Hard cut: flush all active notes + pending queue ----
  flushActiveNotes(transport, /*hard=*/true);

  // ---- Replace events ----
  _eventCount     = hdr.eventCount;
  _loopLengthBars = hdr.loopBars;
  _recordBpm      = hdr.recordBpm;
  if (_recordBpm < 10.0f) _recordBpm = 10.0f;   // safety floor
  memcpy(_events, buf + sizeof(LoopSlotHeader),
         (size_t)hdr.eventCount * sizeof(LoopEvent));

  // ---- Replace LOOP params ----
  _shuffleDepth     = (float)hdr.shuffleDepthRaw / 4095.0f;
  _shuffleTemplate  = hdr.shuffleTemplate;
  if (_shuffleTemplate >= NUM_SHUFFLE_TEMPLATES) _shuffleTemplate = 0;
  _velPatternIdx    = hdr.velPatternIdx;
  if (_velPatternIdx > 3) _velPatternIdx = 0;
  _chaosAmount      = (float)hdr.chaosRaw / 4095.0f;
  _velPatternDepth  = (float)hdr.velPatternDepthRaw / 4095.0f;
  _baseVelocity     = hdr.baseVelocity;
  if (_baseVelocity < 1) _baseVelocity = 1;
  _velocityVariation = hdr.velocityVariation;
  if (_velocityVariation > 100) _velocityVariation = 100;

  // ---- Reset playback cursors ----
  _cursorIdx          = 0;
  _lastPositionUs     = 0;
  _lastBeatIdx        = 0xFFFFFFFF;
  _beatFlash          = false;
  _barFlash           = false;
  _wrapFlash          = false;
  _lastRecordBeatTick = 0xFFFFFFFF;

  // ---- State-dependent timing setup ----
  if (_state == PLAYING) {
    // PLAYING: hard-cut + quantize-snap on the last beat passed.
    // AUDIT FIX (B4, 2026-04-06): use the LIVE BPM (currentBPM) here, NOT
    // _recordBpm. (now - _playStartUs) is in real time, so usSinceLastBeat
    // must also be in real time, which means usPerTick = 60e6 / liveBPM / 24.
    // Using _recordBpm would offset the snap by ~8% of a beat per 20% BPM
    // divergence — audible. The next tick() applies the live timebase scaling
    // via positionUs = elapsedUs * recordDur / liveDur.
    uint32_t tickInBeat = globalTick % 24;
    float    bpm        = (currentBPM < 10.0f) ? 10.0f : currentBPM;  // safety floor
    uint32_t usPerBeat  = (uint32_t)(60000000.0f / bpm);
    uint32_t usPerTick  = usPerBeat / 24;
    uint32_t usSinceLastBeat = tickInBeat * usPerTick;
    _playStartUs = micros() - usSinceLastBeat;
    // _state remains PLAYING — next tick() resumes playback
  } else {
    // EMPTY/STOPPED → STOPPED with the loaded events ready to play.
    // The user presses PLAY/STOP to start.
    // currentBPM unused in this branch (no quantize-snap needed).
    (void)currentBPM;
    _playStartUs = micros();
    _state = STOPPED;
  }

  _pendingAction = PENDING_NONE;

  return true;
}
```

> **AUDIT NOTE**: the `NUM_SHUFFLE_TEMPLATES` constant comes from `src/midi/GrooveTemplates.h` (= 10). Make sure that header is already included by `LoopEngine.cpp` (Phase 5 added it for `SHUFFLE_TEMPLATES`). If not, add `#include "../midi/GrooveTemplates.h"`.

### Build verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected: clean build. The new methods are defined but no caller exists yet.

### Test verification

No runtime change yet.

---

## Step 4 — `LedController` extensions for slot feedback

> **DESIGN REF**: spec §2.6 (LedController modifications), §1.6 (gestures color codes).

### 4a. Add 4 new ConfirmType enum values in `src/core/LedController.h`

Replace the existing enum (line 16-27):

```cpp
enum ConfirmType : uint8_t {
  CONFIRM_NONE             = 0,
  CONFIRM_BANK_SWITCH      = 1,
  CONFIRM_SCALE_ROOT       = 2,
  CONFIRM_SCALE_MODE       = 3,
  CONFIRM_SCALE_CHROM      = 4,
  CONFIRM_HOLD_ON          = 5,
  CONFIRM_HOLD_OFF         = 6,
  CONFIRM_PLAY             = 7,
  CONFIRM_STOP             = 8,
  CONFIRM_OCTAVE           = 9,
  CONFIRM_LOOP_REC         = 10,    // already added in Phase 1 Step 4c-1
  CONFIRM_LOOP_SLOT_LOAD   = 11,    // <-- ADD (Phase 6)
  CONFIRM_LOOP_SLOT_SAVE   = 12,    // <-- ADD
  CONFIRM_LOOP_SLOT_REFUSE = 13,    // <-- ADD
  CONFIRM_LOOP_SLOT_DELETE = 14,    // <-- ADD
};
```

### 4b. Add `showSlotSaveRamp` declaration + private state in `LedController.h`

In the public section, near `showClearRamp`:

```cpp
  void showSlotSaveRamp(uint8_t pct);  // 0-100, white-to-green ramp on current bank LED
```

In the private section, near `_clearRampPct`:

```cpp
  uint8_t _slotRampPct = 0;
  bool    _showingSlotRamp = false;
```

### 4c. Implement `showSlotSaveRamp` in `src/core/LedController.cpp`

Add right after `showClearRamp`:

```cpp
void LedController::showSlotSaveRamp(uint8_t pct) {
    _slotRampPct = pct;
    _showingSlotRamp = (pct > 0);
}
```

### 4d. Render slot save ramp in `renderNormalDisplay()`

Find the `_showingClearRamp` block (added in Phase 4 Step 11c) and add a parallel block right after it:

```cpp
  // Slot save ramp overlay (LOOP only)
  if (_showingSlotRamp) {
      setPixel(_currentBank, COL_LOOP_SLOT_SAVE, _slotRampPct);
      _showingSlotRamp = false;  // auto-reset — handleLoopSlots calls every frame during hold
  }
```

### 4e. Add expiry handling for the 4 new confirms in `renderConfirmation()`

Find the existing switch (around line 388, after `CONFIRM_LOOP_REC` from Phase 4) and add:

```cpp
    case CONFIRM_LOOP_SLOT_LOAD:
      if (elapsed >= 200) { _confirmType = CONFIRM_NONE; return false; }
      return true;
    case CONFIRM_LOOP_SLOT_SAVE:
      if (elapsed >= 200) { _confirmType = CONFIRM_NONE; return false; }
      return true;
    case CONFIRM_LOOP_SLOT_REFUSE:
      if (elapsed >= 200) { _confirmType = CONFIRM_NONE; return false; }
      return true;
    case CONFIRM_LOOP_SLOT_DELETE:
      if (elapsed >= 400) { _confirmType = CONFIRM_NONE; return false; }
      return true;
```

(DELETE has 400 ms because it renders as a double blink.)

### 4f. Add rendering for the 4 new confirms in `renderNormalDisplay()` overlay section

Find the existing CONFIRM overlay switch (around line 562, after `CONFIRM_LOOP_REC`) and add:

```cpp
      case CONFIRM_LOOP_SLOT_LOAD: {
        // Single green blink — 200 ms total (100 on, 100 off)
        bool on = ((elapsed / 100) % 2 == 0);
        if (on) setPixel(_currentBank, COL_LOOP_SLOT_LOAD, 100);
        break;
      }
      case CONFIRM_LOOP_SLOT_SAVE: {
        // Single white blink — 200 ms total
        bool on = ((elapsed / 100) % 2 == 0);
        if (on) setPixel(_currentBank, COL_LOOP_SLOT_SAVE, 100);
        break;
      }
      case CONFIRM_LOOP_SLOT_REFUSE: {
        // Single red blink — 200 ms total
        bool on = ((elapsed / 100) % 2 == 0);
        if (on) setPixel(_currentBank, COL_LOOP_SLOT_REFUSE, 100);
        break;
      }
      case CONFIRM_LOOP_SLOT_DELETE: {
        // Double red blink — 400 ms total (4 × 100 ms phases)
        uint16_t phase = (elapsed / 100) % 4;
        bool on = (phase == 0 || phase == 2);
        if (on) setPixel(_currentBank, COL_LOOP_SLOT_DELETE, 100);
        break;
      }
```

### Build verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected: clean build. The new confirms are defined and renderable, but no caller triggers them yet.

### Test verification

No runtime change yet — code compiled but not called.

---

## Step 5 — Boot sequence reorder + LittleFS mount in setup()

> **DESIGN REF**: spec §2.7 (boot sequence). The LED hardware step is **removed** ; LittleFS becomes step 1. We stay in 8 boot steps.

### 5a. Instantiate `LoopSlotStore` in `main.cpp`

Near the other static globals (around line 60, after `s_arpScheduler`):

```cpp
#include "loop/LoopSlotStore.h"

// ... existing globals ...
static LoopSlotStore s_loopSlotStore;
```

### 5b. Reorder `setup()` boot sequence

Find the early boot section in `setup()` (lines 155-180 of main.cpp). The current order is:

```
LedController::begin() + showBootProgress(1)  // Step 1: LED hardware
Wire.begin() + showBootProgress(2)            // Step 2: I2C
s_keyboard.begin() + showBootProgress(3)      // Step 3: Keyboard
```

Replace with:

```cpp
  // LedController begin — silent prerequisite (no boot step LED).
  // If this fails, no LED will light up at all — that IS the diagnostic.
  s_leds.begin();

  // Step 1: LittleFS mount + slot drive scan
  if (!s_loopSlotStore.begin()) {
    Serial.println("[INIT] FATAL: LittleFS mount failed!");
    s_leds.showBootFailure(1);  // Step 1 blinks = LittleFS failed
    for (;;) { s_leds.update(); delay(10); }
  }
  s_leds.showBootProgress(1);   // Step 1: LittleFS mounted
  s_leds.update();
  #if DEBUG_SERIAL
  Serial.println("[INIT] LittleFS OK.");
  #endif

  // Step 2: I2C
  Wire.begin(SDA_PIN, SCL_PIN, I2C_CLOCK_HZ);
  s_leds.showBootProgress(2);
  s_leds.update();
  #if DEBUG_SERIAL
  Serial.println("[INIT] I2C OK.");
  #endif

  // Step 3: Keyboard (4× MPR121)
  bool kbOk = s_keyboard.begin();
  if (!kbOk) {
    Serial.println("[INIT] FATAL: Keyboard init failed!");
    s_leds.showBootFailure(3);
    for (;;) { s_leds.update(); delay(10); }
  }
  s_leds.showBootProgress(3);
  s_leds.update();
  #if DEBUG_SERIAL
  Serial.println("[INIT] Keyboard OK.");
  #endif
```

**Steps 4-8 remain unchanged** — they keep their current numbering (MIDI Transport / NVS / Arp+Loop / Managers / All systems go).

### Build verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected: clean build.

### Test verification (hardware)

1. Flash and watch the boot LED bar:
   - LED 1 lights → LittleFS mounted OK
   - LED 2 → I2C OK
   - LED 3 → Keyboard OK
   - ... up to LED 8 (full bar) → all systems go
2. Serial monitor shows `[INIT] LittleFS OK.` between the early prints.
3. Power cycle 5 times to verify no transient mount failure.
4. **Failure simulation** (optional): edit `partitions_illpad.csv` to remove the `loopfs` partition, reflash. Boot should freeze with LED 1 blinking red. Restore the partition, reflash to recover.

---

## Step 6 — `handleLoopSlots()` gesture handler in main.cpp

> **DESIGN REF**: spec §2.4 (handler), §2.5 (coordination), §3 (data flows).

### 6a. Add the persistent state declarations in main.cpp

Near the existing LOOP control statics (`s_recPad`, `s_loopPlayPad`, etc., around line 76):

```cpp
// LOOP slot drive state (Phase 6)
static bool     s_slotLastState[LOOP_SLOT_COUNT];
static uint32_t s_slotPressStart[LOOP_SLOT_COUNT];
static bool     s_slotConsumed[LOOP_SLOT_COUNT];
static bool     s_clearConsumedByCombo = false;
```

And initialize them in `setup()` right after the `memset(s_loopSlotPads, 0xFF, ...)` line (added by Phase 1 Step 7c-5 / Phase 2 Step 3c):

```cpp
  // LOOP slot drive state init
  memset(s_slotLastState, 0, sizeof(s_slotLastState));
  memset(s_slotPressStart, 0, sizeof(s_slotPressStart));
  memset(s_slotConsumed, 0, sizeof(s_slotConsumed));
  s_clearConsumedByCombo = false;
```

### 6b. Add the `handleLoopSlots()` function in main.cpp

Add right after `handleLoopControls()` (added in Phase 2 Step 6a):

```cpp
static void handleLoopSlots(const SharedKeyboardState& state, bool leftHeld, uint32_t now) {
    // === Global guards ===
    BankSlot& slot = s_bankManager.getCurrentSlot();
    bool isLoopFg = (slot.type == BANK_LOOP && slot.loopEngine != nullptr);
    bool recording = isLoopFg && (slot.loopEngine->isRecording());

    // Skip total if any condition fails. Reset edge state to avoid phantom edges.
    if (!leftHeld || !isLoopFg || recording) {
        for (uint8_t i = 0; i < LOOP_SLOT_COUNT; i++) {
            s_slotLastState[i] = false;
            s_slotConsumed[i] = false;
        }
        return;
    }

    LoopEngine* eng = slot.loopEngine;
    bool clearPressed = (s_clearPad < NUM_KEYS) ? state.keyIsPressed[s_clearPad] : false;

    // Process each slot pad
    for (uint8_t i = 0; i < LOOP_SLOT_COUNT; i++) {
        uint8_t padIdx = s_loopSlotPads[i];
        if (padIdx >= NUM_KEYS) continue;   // unassigned slot pad

        bool pressed = state.keyIsPressed[padIdx];

        // === Rising edge ===
        if (pressed && !s_slotLastState[i]) {
            if (clearPressed) {
                // Combo delete
                if (s_loopSlotStore.isSlotOccupied(i)) {
                    s_loopSlotStore.deleteSlot(i);
                    s_leds.triggerConfirm(CONFIRM_LOOP_SLOT_DELETE);
                } else {
                    s_leds.triggerConfirm(CONFIRM_LOOP_SLOT_REFUSE);
                }
                s_slotConsumed[i] = true;
                s_clearConsumedByCombo = true;
            } else {
                s_slotPressStart[i] = now;
                s_slotConsumed[i] = false;
            }
            s_slotLastState[i] = true;
            continue;
        }

        // === Held (still pressed since rising edge) ===
        if (pressed && s_slotLastState[i] && !s_slotConsumed[i]) {
            uint32_t elapsed = now - s_slotPressStart[i];

            if (elapsed >= LOOP_SLOT_LONG_PRESS_MS) {
                // Save action
                if (s_loopSlotStore.isSlotOccupied(i)) {
                    s_leds.triggerConfirm(CONFIRM_LOOP_SLOT_REFUSE);
                } else if (eng->getEventCount() == 0) {
                    s_leds.triggerConfirm(CONFIRM_LOOP_SLOT_REFUSE);
                } else {
                    if (s_loopSlotStore.saveSlot(i, *eng)) {
                        s_leds.triggerConfirm(CONFIRM_LOOP_SLOT_SAVE);
                    } else {
                        s_leds.triggerConfirm(CONFIRM_LOOP_SLOT_REFUSE);
                    }
                }
                s_slotConsumed[i] = true;
            } else {
                // Show ramp during the press hold
                uint8_t pct = (uint8_t)((uint32_t)elapsed * 100 / LOOP_SLOT_LONG_PRESS_MS);
                s_leds.showSlotSaveRamp(pct);
            }
            continue;
        }

        // === Falling edge ===
        if (!pressed && s_slotLastState[i]) {
            if (!s_slotConsumed[i]) {
                uint32_t elapsed = now - s_slotPressStart[i];
                if (elapsed >= LOOP_SLOT_LOAD_MIN_MS) {
                    // Tap valid (between 300 and 1000 ms) — load
                    if (s_loopSlotStore.isSlotOccupied(i)) {
                        uint32_t globalTick = s_clockManager.getCurrentTick();
                        // B4 fix: pass live BPM for the PLAYING quantize-snap
                        // inside deserializeFromBuffer.
                        float currentBPM = s_clockManager.getSmoothedBPMFloat();
                        if (s_loopSlotStore.loadSlot(i, *eng, s_transport, globalTick, currentBPM)) {
                            // B5 fix: propagate the loaded velocity params from
                            // the engine to BankSlot BEFORE reloadPerBankParams,
                            // so PotRouter is seeded with the loaded values
                            // (preset complet semantics — see Q2 audit decision).
                            // F2 fix (2026-04-06 pass 2): reuse the `slot` reference
                            // already declared at the top of handleLoopSlots — both
                            // point to the same BankSlot, no need to call
                            // s_bankManager.getCurrentSlot() again.
                            slot.baseVelocity      = eng->getBaseVelocity();
                            slot.velocityVariation = eng->getVelocityVariation();
                            // Re-arm the catch system on PotRouter for per-bank LOOP
                            // params via the existing reloadPerBankParams() helper
                            // (same path as a bank switch). It reads BankSlot
                            // (now with loaded velocity values) and the engine's
                            // shuffle/template/etc, pushes them into PotRouter,
                            // then calls seedCatchValues + resetPerBankCatch.
                            reloadPerBankParams(slot);
                            s_leds.triggerConfirm(CONFIRM_LOOP_SLOT_LOAD);
                        } else {
                            // Load failed (corrupt) — bitmask was already updated
                            s_leds.triggerConfirm(CONFIRM_LOOP_SLOT_REFUSE);
                        }
                    } else {
                        // Slot empty — refuse
                        s_leds.triggerConfirm(CONFIRM_LOOP_SLOT_REFUSE);
                    }
                }
                // else elapsed < 300 ms = silent cancellation, no action
            }
            s_slotConsumed[i] = false;
            s_slotLastState[i] = false;
            continue;
        }

        // No edge change — pressed unchanged
        s_slotLastState[i] = pressed;
    }
}
```

### 6c. Coordinate `handleLoopControls()` with `s_clearConsumedByCombo`

Find the existing CLEAR pad section in `handleLoopControls()` (Phase 2 Step 6a). The relevant block looks like:

```cpp
  // --- CLEAR pad: long press (500ms) + LED ramp ---
  bool clearPressed = (s_clearPad < NUM_KEYS) ? state.keyIsPressed[s_clearPad] : false;
  if (s_clearPad < NUM_KEYS && ls != LoopEngine::EMPTY) {
    if (clearPressed && !s_lastClearState) {
      s_clearPressStart = now;
      s_clearFired = false;
    }
    if (clearPressed && !s_clearFired) {
      // ... ramp + dispatch ...
    }
  }
  s_lastClearState = clearPressed;
```

Wrap the inner block with the combo flag check:

```cpp
  // --- CLEAR pad: long press (500ms) + LED ramp ---
  // Coordinated with handleLoopSlots() via s_clearConsumedByCombo:
  // when a slot pad combo delete fires, the clear pad is "consumed" until
  // it is released.
  bool clearPressed = (s_clearPad < NUM_KEYS) ? state.keyIsPressed[s_clearPad] : false;

  // Reset combo flag when clear pad is released
  if (!clearPressed) {
      s_clearConsumedByCombo = false;
  }

  if (s_clearPad < NUM_KEYS && ls != LoopEngine::EMPTY && !s_clearConsumedByCombo) {
    if (clearPressed && !s_lastClearState) {
      s_clearPressStart = now;
      s_clearFired = false;
    }
    if (clearPressed && !s_clearFired) {
      uint32_t held = now - s_clearPressStart;
      if (held < CLEAR_LONG_PRESS_MS) {
        uint8_t ramp = (uint8_t)((uint32_t)held * 100 / CLEAR_LONG_PRESS_MS);
        s_leds.showClearRamp(ramp);
      } else {
        // Ramp complete — dispatch based on current state
        if (ls == LoopEngine::OVERDUBBING) {
          eng->cancelOverdub();
        } else {
          eng->clear(s_transport);
          s_leds.triggerConfirm(CONFIRM_STOP);
        }
        s_clearFired = true;
      }
    }
  }
  // Always update edge state — even when ls == EMPTY — to avoid false rising
  // edges when state transitions while the pad is held.
  s_lastClearState = clearPressed;
```

### 6d. Wire `handleLoopSlots()` into the main loop

In `loop()`, find the call sequence (Phase 2 Step 8a) and add the new call right after `handleLoopControls()`:

```cpp
  handlePlayStopPad(state, holdBeforeUpdate, bankSwitched);
  handleLoopControls(state, now);
  handleLoopSlots(state, leftHeld, now);   // <-- ADD (Phase 6)
  handlePadInput(state, now);
```

> **AUDIT NOTE**: `leftHeld` must be in scope at this call site. In the existing pipeline, the variable is read once near the top (around line ~950) as `bool leftHeld = (digitalRead(BTN_LEFT_PIN) == LOW);` or is passed via `BankManager::isHolding()`. Verify the variable name in the actual main.cpp loop and pass the correct one.

### Build verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected: clean build.

### Test verification (hardware)

At this point, slots can be saved and loaded **but only via test pads** (no Tool 3 UI yet for the 16 slot pads). For initial verification, hard-code a slot pad in `s_loopSlotPads[0] = 30;` (or similar unused pad) right after the memset in setup() and remove it after the test passes:

1. Bank LOOP, record a small loop, stop.
2. Hold left + long press (1s) on pad 30 → white ramp visible, then white blink (save OK).
3. Reboot → loop is gone from runtime (state EMPTY).
4. Bank LOOP again, hold left + tap (300-1000 ms) on pad 30 → green blink (load OK), state becomes STOPPED.
5. Press LOOP play/stop → loop plays back exactly as saved.
6. Hold left + long press on pad 30 again → red blink (refus, slot occupied).
7. Hold clear + tap pad 30 → red double blink (delete OK).
8. Hold left + tap pad 30 → red blink (refus, slot empty).
9. Verify clear pad alone still works for clear/cancelOverdub.

Remove the test wiring (`s_loopSlotPads[0] = 30;`) before continuing.

---

## Step 7 — Tool 3 LOOP sub-page extension (16 slot roles)

> **DESIGN REF**: spec §1.4 (architecture des rôles), §7.3 (Phase 6 scope). The Tool 3 b1 contextual refactor was already done in Phase 3 Step 2-pre. Phase 6 extends the LOOP sub-page with the 16 slot pad lines.

### 7a. Add 16 slot role enum values to `PadRoleCode`

> **AUDIT FIX (A3, 2026-04-06)**: this enum was already extended in Phase 3
> Step 2a from the original `ROLE_NONE..ROLE_PLAYSTOP/COLLISION` baseline to
> include `ROLE_ARPEG_PLAYSTOP` (renamed) and `ROLE_LOOP_REC/PS/CLR`. Phase 6
> ADDS 16 more values between `ROLE_LOOP_CLR` and `ROLE_COLLISION`. This is
> a **REPLACE** of the existing enum block, not an additive append. Do NOT
> leave the Phase 3 enum intact and copy the new full enum below it — that
> would produce a duplicate `enum PadRoleCode` definition (compile error).
> Modify the existing enum body in place.

**REPLACE** the enum body in `src/setup/ToolPadRoles.h` (the same block that
Phase 3 Step 2a left at 10 values + ROLE_COLLISION) with this final version :

```cpp
enum PadRoleCode : uint8_t {
  ROLE_NONE            = 0,
  ROLE_BANK            = 1,
  ROLE_ROOT            = 2,
  ROLE_MODE            = 3,
  ROLE_OCTAVE          = 4,
  ROLE_HOLD            = 5,
  ROLE_ARPEG_PLAYSTOP  = 6,
  ROLE_LOOP_REC        = 7,
  ROLE_LOOP_PS         = 8,
  ROLE_LOOP_CLR        = 9,
  ROLE_LOOP_SLOT_0     = 10,    // <-- ADD (Phase 6)
  ROLE_LOOP_SLOT_1     = 11,
  ROLE_LOOP_SLOT_2     = 12,
  ROLE_LOOP_SLOT_3     = 13,
  ROLE_LOOP_SLOT_4     = 14,
  ROLE_LOOP_SLOT_5     = 15,
  ROLE_LOOP_SLOT_6     = 16,
  ROLE_LOOP_SLOT_7     = 17,
  ROLE_LOOP_SLOT_8     = 18,
  ROLE_LOOP_SLOT_9     = 19,
  ROLE_LOOP_SLOT_10    = 20,
  ROLE_LOOP_SLOT_11    = 21,
  ROLE_LOOP_SLOT_12    = 22,
  ROLE_LOOP_SLOT_13    = 23,
  ROLE_LOOP_SLOT_14    = 24,
  ROLE_LOOP_SLOT_15    = 25,
  ROLE_COLLISION       = 0xFF
};
```

### 7b. Add `_wkLoopSlotPads[16]` working copy and live pointer

In `ToolPadRoles.h` private section (near `_wkLoopRecPad`):

```cpp
  uint8_t _wkLoopSlotPads[LOOP_SLOT_COUNT];  // 16 slot pad assignments
  uint8_t* _loopSlotPads;                    // live pointer (main.cpp s_loopSlotPads)
```

In the constructor body (`ToolPadRoles.cpp`):

```cpp
  memset(_wkLoopSlotPads, 0xFF, sizeof(_wkLoopSlotPads));
  _loopSlotPads = nullptr;
```

### 7c. Extend `begin()` signature with `loopSlotPads` pointer

In `ToolPadRoles.h`:

```cpp
  void begin(CapacitiveKeyboard* keyboard, LedController* leds, SetupUI* ui,
             uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
             uint8_t& chromaticPad, uint8_t& holdPad, uint8_t& arpPlayStopPad,
             uint8_t* octavePads,
             uint8_t& recPad, uint8_t& loopPlayPad, uint8_t& clearPad,
             uint8_t* loopSlotPads);   // <-- ADD
```

In `ToolPadRoles.cpp` `begin()` body:

```cpp
  _loopSlotPads = loopSlotPads;
```

### 7d. Thread `loopSlotPads` through SetupManager

In `src/setup/SetupManager.h`:

```cpp
  void begin(CapacitiveKeyboard* keyboard, LedController* leds,
             NvsManager* nvs, BankSlot* banks,
             uint8_t* padOrder, uint8_t* bankPads,
             uint8_t* rootPads, uint8_t* modePads,
             uint8_t& chromaticPad, uint8_t& holdPad, uint8_t& playStopPad,
             uint8_t* octavePads, PotRouter* potRouter,
             uint8_t& recPad, uint8_t& loopPlayPad, uint8_t& clearPad,
             uint8_t* loopSlotPads);   // <-- ADD (Phase 6)
```

In `src/setup/SetupManager.cpp`:

```cpp
  _toolRoles.begin(keyboard, leds, &_ui,
                   bankPads, rootPads, modePads,
                   chromaticPad, holdPad, playStopPad,
                   octavePads,
                   recPad, loopPlayPad, clearPad,
                   loopSlotPads);   // <-- ADD
```

### 7e. Wire main.cpp `s_loopSlotPads` to SetupManager::begin()

In main.cpp setup() at the existing `s_setupManager.begin(...)` call:

```cpp
  s_setupManager.begin(&s_keyboard, &s_leds, &s_nvsManager,
                       s_banks, s_padOrder, bankPads,
                       rootPads, modePads, chromaticPad,
                       holdPad, s_playStopPad, octavePads,
                       &s_potRouter,
                       s_recPad, s_loopPlayPad, s_clearPad,
                       s_loopSlotPads);   // <-- ADD
```

### 7f. Extend `buildRoleMap()` LOOP branch with 16 slots

In `ToolPadRoles.cpp`, find the LOOP context block in `buildRoleMap()` (added in Phase 3 Step 2-pre-3) and append:

```cpp
  // === Loop context ===
  if (_activeSubPage == 2) {
    setRole(_wkLoopRecPad,      ROLE_LOOP_REC, GRID_LOOP_REC_LABELS[0]);
    setRole(_wkLoopPlayStopPad, ROLE_LOOP_PS,  GRID_LOOP_PS_LABELS[0]);
    setRole(_wkLoopClearPad,    ROLE_LOOP_CLR, GRID_LOOP_CLR_LABELS[0]);
    // 16 slot pads
    for (uint8_t i = 0; i < LOOP_SLOT_COUNT; i++) {
      setRole(_wkLoopSlotPads[i], (uint8_t)(ROLE_LOOP_SLOT_0 + i), GRID_LOOP_SLOT_LABELS[i]);
    }
  }
```

### 7g. Add slot pad labels (top of `ToolPadRoles.cpp`)

Near the existing `GRID_LOOP_REC_LABELS` (Phase 3 Step 2g):

```cpp
static const char* GRID_LOOP_SLOT_LABELS[LOOP_SLOT_COUNT] = {
  "Sl01","Sl02","Sl03","Sl04","Sl05","Sl06","Sl07","Sl08",
  "Sl09","Sl10","Sl11","Sl12","Sl13","Sl14","Sl15","Sl16"
};

static const char* POOL_LOOP_SLOT_LABELS[LOOP_SLOT_COUNT] = {
  "S01","S02","S03","S04","S05","S06","S07","S08",
  "S09","S10","S11","S12","S13","S14","S15","S16"
};
```

### 7h. Extend per-sub-page pool linearization with 16 slot lines

> **AUDIT FIX (A3, 2026-04-06)**: this sub-step REPLACES four file-scope/static
> definitions that were created in Phase 3 Step 2-pre-8 with placeholder
> 4-line versions. Phase 6 needs the full 20-line version (4 LOOP control
> lines + 16 slot lines). Each item below is a **REPLACE in place** — do
> NOT leave the Phase 3 versions and add the Phase 6 versions next to them
> (would produce duplicate definitions and / or array length mismatches).
>
> The 4 items to **REPLACE** are:
>   1. `POOL_OFFSETS_LOOP[]` (file-scope static — `ToolPadRoles.cpp` near
>      the other `POOL_OFFSETS_*` tables)
>   2. `TOTAL_POOL_LOOP` (file-scope constant in the same area)
>   3. `MAP_LOOP[]` (static local inside `linearToPool()`, in the
>      `subPage == 2` branch)
>   4. `REV_LOOP[]` (static local inside `poolToLinear()`, in the
>      `subPage == 2` branch) — AND its guard `if (line < 10)` becomes
>      `if (line < 26)`
>
> Modify the existing definitions in place. Do NOT duplicate.

#### REPLACE 1: `POOL_OFFSETS_LOOP[]` and `TOTAL_POOL_LOOP`

In `ToolPadRoles.cpp` near the other per-sub-page offset tables, **REPLACE**
the Phase 3 placeholder version (`{0, 1, 2, 3, 4}` + `TOTAL_POOL_LOOP = 4`)
with the final 20-line version :

```cpp
// Sub-page 2 (Loop): Clear(1) + LoopRec(1) + LoopPS(1) + LoopClr(1) + 16 Slots = 20
// 16 slots → 16 pool lines, each with a single item
// Pool line numbering: 0=clear, 1=LoopRec(7), 2=LoopPS(8), 3=LoopClr(9), 4..19=Slot0..15
static const uint8_t POOL_OFFSETS_LOOP[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20
};   // 21 offsets for 20 line entries
static const uint8_t TOTAL_POOL_LOOP = 20;
```

#### REPLACE 2: `MAP_LOOP[]` static local in `linearToPool()`

Inside `linearToPool()`, the `subPage == 2` branch contains a static local
`MAP_LOOP[]`. **REPLACE** the Phase 3 4-entry version with the 20-entry
version :

```cpp
// Sub-page 2 (Loop): 0=clear, 1..3=LoopRec/PS/Clr (global lines 7/8/9),
//                    4..19=Slot0..15 (global lines 10..25)
static const uint8_t MAP_LOOP[] = {
    0,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25
};
```

#### REPLACE 3: `REV_LOOP[]` static local in `poolToLinear()` + guard

Inside `poolToLinear()`, the `subPage == 2` branch contains a static local
`REV_LOOP[]` AND a guard `if (line < 10)`. **REPLACE both** with the
26-entry version + the new guard :

```cpp
} else if (subPage == 2) {
    static const uint8_t REV_LOOP[] = {
        0,         // line 0 (clear) → local 0
        0xFF,      // line 1 (bank) — invalid in LOOP sub-page
        0xFF,      // line 2 (root)
        0xFF,      // line 3 (mode)
        0xFF,      // line 4 (octave)
        0xFF,      // line 5 (hold)
        0xFF,      // line 6 (arpPS)
        1,         // line 7 (loopRec) → local 1
        2,         // line 8 (loopPS)  → local 2
        3,         // line 9 (loopClr) → local 3
        4,  5,  6,  7,  8,  9, 10, 11,   // lines 10-17 (Slot0..7)  → local 4-11
       12, 13, 14, 15, 16, 17, 18, 19    // lines 18-25 (Slot8..15) → local 12-19
    };
    if (line < 26) localIdx = REV_LOOP[line];   // was: line < 10
}
```

**Sanity check**: `MAP_LOOP[]` has 20 entries, matching `TOTAL_POOL_LOOP = 20`
and `POOL_OFFSETS_LOOP[]` having 21 entries (20 lines + 1 sentinel). `REV_LOOP[]`
has 26 entries (covering global lines 0..25). All four pieces consistent.

### 7i. Extend `drawPool()` LOOP branch with 16 slot lines

Find the `_activeSubPage == 2` branch in `drawPool()` (Phase 3 Step 2-pre-6) and append after the 3 existing LOOP control lines:

```cpp
  else if (_activeSubPage == 2) {
    // Loop sub-page
    drawPoolLine(7, "Loop Rec:",  POOL_LOOP_REC_LABELS, POOL_LOOP_REC_COUNT, VT_BRIGHT_RED);
    drawPoolLine(8, "Loop P/S:",  POOL_LOOP_PS_LABELS,  POOL_LOOP_PS_COUNT,  VT_BRIGHT_RED);
    drawPoolLine(9, "Loop Clr:",  POOL_LOOP_CLR_LABELS, POOL_LOOP_CLR_COUNT, VT_BRIGHT_RED);
    // 16 slot lines, two per row for visual compactness
    // Each pool line has a single label, but we render them 8 per visual row
    // by emitting 2 line numbers in 1 console line. Simpler: render one per
    // console row, accepting 16 extra rows.
    for (uint8_t i = 0; i < LOOP_SLOT_COUNT; i++) {
      char labelBuf[16];
      snprintf(labelBuf, sizeof(labelBuf), "Slot %02u:", i + 1);
      static const char* singleLabel[1];
      singleLabel[0] = POOL_LOOP_SLOT_LABELS[i];
      drawPoolLine(10 + i, labelBuf, singleLabel, 1, VT_MAGENTA);
    }
  }
```

> **AUDIT NOTE**: 16 extra console rows is significant in the VT100 terminal. The user can scroll via pot navigation (already supported). If vertical space is too tight, group slots 2 per row by extending `drawPoolLine` with a "row of N items" variant — left as an optimization deferred to user feedback.

### 7j. Extend `getRoleForPad()` LOOP branch with 16 slots

In `ToolPadRoles.cpp`, find the `getRoleForPad` LOOP context block (Phase 3 Step 2-pre-1) and append:

```cpp
  // === Loop context ===
  if (context == BANK_LOOP) {
    if (_wkLoopRecPad == pad)      return {7, 0};
    if (_wkLoopPlayStopPad == pad) return {8, 0};
    if (_wkLoopClearPad == pad)    return {9, 0};
    for (uint8_t i = 0; i < LOOP_SLOT_COUNT; i++) {
      if (_wkLoopSlotPads[i] == pad) return {(uint8_t)(10 + i), 0};
    }
  }
```

### 7k. Extend `findPadWithRole()` for slot lines

Find the existing switch in `findPadWithRole` and add (after case 9 for `LoopClr`):

```cpp
    default:
      if (line >= 10 && line < 10 + LOOP_SLOT_COUNT) {
        return _wkLoopSlotPads[line - 10];
      }
      break;
```

### 7l. Extend `assignRole()` for slot lines

Same pattern in `assignRole`:

```cpp
    default:
      if (line >= 10 && line < 10 + LOOP_SLOT_COUNT) {
        _wkLoopSlotPads[line - 10] = pad;
      }
      break;
```

### 7m. Extend `clearRole()` for slot pads

In the `_activeSubPage == 2` block:

```cpp
  if (_activeSubPage == 2) {
    if (_wkLoopRecPad == pad)      _wkLoopRecPad      = 0xFF;
    if (_wkLoopPlayStopPad == pad) _wkLoopPlayStopPad = 0xFF;
    if (_wkLoopClearPad == pad)    _wkLoopClearPad    = 0xFF;
    for (uint8_t i = 0; i < LOOP_SLOT_COUNT; i++) {
      if (_wkLoopSlotPads[i] == pad) _wkLoopSlotPads[i] = 0xFF;
    }
  }
```

### 7n. Extend `clearAllRoles()` for slot pads

In the `_activeSubPage == 2` block:

```cpp
  if (_activeSubPage == 2) {
    _wkLoopRecPad      = 0xFF;
    _wkLoopPlayStopPad = 0xFF;
    _wkLoopClearPad    = 0xFF;
    memset(_wkLoopSlotPads, 0xFF, sizeof(_wkLoopSlotPads));
  }
```

### 7o. Extend `isPadOccupiedInContext()` for slot pads

In the `_activeSubPage == 2` block:

```cpp
  } else if (_activeSubPage == 2) {
    if (_wkLoopRecPad == pad) return true;
    if (_wkLoopPlayStopPad == pad) return true;
    if (_wkLoopClearPad == pad) return true;
    for (uint8_t i = 0; i < LOOP_SLOT_COUNT; i++) {
      if (_wkLoopSlotPads[i] == pad) return true;
    }
  }
```

### 7p. Extend `saveAll()` LoopPadStore block with slot pads

Replace the Phase 3 Step 2-pre-12 LoopPadStore save block with:

```cpp
  // 4. LoopPadStore (Phase 6: full — 3 control pads + 16 slot pads)
  LoopPadStore lps;
  memset(&lps, 0, sizeof(lps));
  lps.magic       = EEPROM_MAGIC;
  lps.version     = LOOPPAD_VERSION;
  lps.recPad      = _wkLoopRecPad;
  lps.playStopPad = _wkLoopPlayStopPad;
  lps.clearPad    = _wkLoopClearPad;
  memcpy(lps.slotPads, _wkLoopSlotPads, sizeof(lps.slotPads));
  if (NvsManager::saveBlob(LOOP_PAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY, &lps, sizeof(lps))) {
    *_loopRecPad      = _wkLoopRecPad;
    *_loopPlayStopPad = _wkLoopPlayStopPad;
    *_loopClearPad    = _wkLoopClearPad;
    if (_loopSlotPads) memcpy(_loopSlotPads, _wkLoopSlotPads, sizeof(_wkLoopSlotPads));
  } else {
    allOk = false;
  }
```

### 7q. Extend `run()` live-to-wk copy with slot pads

In the entry section of `run()` where the existing Phase 3 fields are copied:

```cpp
  if (_loopSlotPads) memcpy(_wkLoopSlotPads, _loopSlotPads, sizeof(_wkLoopSlotPads));
```

### 7r. Extend NVS load logic in `run()` with slot pads

Find the LoopPadStore load block (Phase 3 Step 2m) and update:

```cpp
  LoopPadStore lps;
  bool lpOk = NvsManager::loadBlob(LOOP_PAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY,
                                    EEPROM_MAGIC, LOOPPAD_VERSION, &lps, sizeof(lps));
  if (lpOk) {
    validateLoopPadStore(lps);
    _wkLoopRecPad      = lps.recPad;
    _wkLoopPlayStopPad = lps.playStopPad;
    _wkLoopClearPad    = lps.clearPad;
    memcpy(_wkLoopSlotPads, lps.slotPads, sizeof(_wkLoopSlotPads));
  }
```

### 7s. Extend `printRoleDescription` with slot pad cases

In `printRoleDescription`, after case 9 (LoopClr):

```cpp
    default:
      if (line >= 10 && line < 10 + LOOP_SLOT_COUNT) {
        uint8_t slotIdx = line - 10;
        _ui->drawFrameLine(VT_MAGENTA "Loop Slot %u" VT_RESET "  " VT_DIM "--  LOOP banks only (hold + tap=load, hold + long press=save)" VT_RESET, slotIdx + 1);
        _ui->drawFrameLine(VT_DIM "Persistent storage in flash. Survives reboot. 16 total slots." VT_RESET);
        _ui->drawFrameLine(VT_DIM "Delete: hold + clear pad + slot pad. Save refused if slot occupied — delete first." VT_RESET);
      }
      break;
```

### 7t. Update `poolItemLabel` for slot pads

In `poolItemLabel`:

```cpp
    default:
      if (line >= 10 && line < 10 + LOOP_SLOT_COUNT && index == 0) {
        return POOL_LOOP_SLOT_LABELS[line - 10];
      }
      return "---";
```

### 7u. Update `poolLineSize` for slot lines

In `poolLineSize`:

```cpp
    default:
      if (line >= 10 && line < 10 + LOOP_SLOT_COUNT) return 1;
      return 0;
```

### 7v. Extend `NvsManager::loadAll()` to read slotPads into main.cpp statics

In `src/managers/NvsManager.cpp`, find the LoopPadStore load block (added in Phase 3 Step 4a) and update it to also populate the s_loopSlotPads array via the pointer chain:

The function signature must be extended to accept the slot pads pointer:

```cpp
void NvsManager::loadAll(BankSlot* banks, uint8_t& currentBank, uint8_t* padOrder,
                          uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
                          uint8_t& chromaticPad, uint8_t& holdPad,
                          uint8_t& playStopPad, uint8_t* octavePads,
                          PotRouter& potRouter, SettingsStore& outSettings,
                          uint8_t& recPad, uint8_t& loopPlayPad, uint8_t& clearPad,
                          uint8_t* loopSlotPads)   // <-- ADD
```

> **AUDIT NOTE**: the `recPad`/`loopPlayPad`/`clearPad` parameters were already added by Phase 3 Step 4a. Phase 6 only adds `loopSlotPads`.

In the load block:

```cpp
  // LoopPadStore — pad assignments for LOOP control + 16 slot pads
  {
    LoopPadStore lps;
    if (loadBlob(LOOP_PAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY,
                 EEPROM_MAGIC, LOOPPAD_VERSION, &lps, sizeof(lps))) {
      validateLoopPadStore(lps);
      recPad      = lps.recPad;
      loopPlayPad = lps.playStopPad;
      clearPad    = lps.clearPad;
      if (loopSlotPads) {
        memcpy(loopSlotPads, lps.slotPads, LOOP_SLOT_COUNT);
      }
    }
    // No else: defaults (0xFF) already in place
  }
```

In `main.cpp setup()`, the call to `loadAll()` must be updated:

```cpp
  s_nvsManager.loadAll(s_banks, currentBank, s_padOrder, bankPads,
                        rootPads, modePads, chromaticPad, holdPad,
                        s_playStopPad, octavePads, s_potRouter, s_settings,
                        s_recPad, s_loopPlayPad, s_clearPad,
                        s_loopSlotPads);   // <-- ADD
```

> **AUDIT NOTE**: the `s_recPad`/`s_loopPlayPad`/`s_clearPad` arguments were already added by Phase 3. Phase 6 only adds `s_loopSlotPads`.

### Build verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected: clean build.

### Test verification (hardware)

1. Flash, enter setup mode (rear button at boot).
2. Tool 3 → toggle to Loop sub-page (`[t]` key twice from Banks).
3. Header shows "TOOL 3: LOOP ROLES".
4. Pool shows: Loop Rec / Loop P/S / Loop Clr + 16 Slot lines (Sl01 to Sl16).
5. Assign 4-6 unused pads to slots 1-6 (the rest remain unassigned).
6. Save (any save key in Tool 3) → reboot → assignments persist.
7. Exit setup, switch to LOOP bank.
8. Hold-left + tap on a slot pad → blink (load if occupied, refus if empty).
9. Verify the pads not assigned to a slot still play music normally on LOOP, ARPEG, and NORMAL banks.
10. Check that a slot pad mapped to (e.g.) Slot 3 in LOOP can also be mapped to Root C in ARPEG (cross-context allowed).
11. Verify it acts as Root C when bank foreground is ARPEG, and as Slot 3 when bank foreground is LOOP.

---

## Step 8 — Manual + VT100 documentation updates

### 8a. Update `docs/manual/loop-workflow.html` (or equivalent manual file)

Add a new section "Slot Drive" describing:
- The 16 persistent slots
- Save/load/delete gestures
- The hard-cut + beat quantize behavior on PLAYING load
- The "Loop content survives reboot" change vs the old "ephemeral" wording

### 8b. Update Tool 3 description text

In the description shown in setup mode for the LOOP control pads (printRoleDescription cases 7/8/9), the existing text mentions "Loop content is ephemeral — lost on reboot". This is **no longer accurate**. Update the case 9 (Loop Clear) description:

```cpp
    case 9:  // Loop Clear
      _ui->drawFrameLine(VT_BRIGHT_RED "Loop Clear" VT_RESET "  " VT_DIM "--  LOOP banks only (long press)" VT_RESET);
      _ui->drawFrameLine(VT_DIM "Hold 500ms to clear the runtime loop. Saved slots are NOT affected." VT_RESET);
      _ui->drawFrameLine(VT_DIM "Use slot save (hold-left + long press on slot pad) to persist a loop." VT_RESET);
      break;
```

### 8c. Update ToolBankConfig description for LOOP banks

In Phase 3 Step 1f's `drawDescription` for LOOP banks (around line 310 of `ToolBankConfig.cpp`), append:

```cpp
    _ui->drawFrameLine(VT_DIM "16 persistent slots available via hold-left + slot pad. See Tool 3 LOOP page." VT_RESET);
```

### Build verification

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected: clean build.

---

## Step 9 — End-to-end hardware tests

> **DESIGN REF**: spec §8 (test strategy, levels 1-7).

Run the full test plan from spec §8 in order. Documenting each test below as a checklist:

### Level 1 — Build & boot

- [ ] **B1** Build clean, zéro warning lié au slot system
- [ ] **B2** Premier boot après partition resize : NVS reset attendu, defaults apply
- [ ] **B3** LittleFS mount réussi : step 1 LED visible
- [ ] **B4** LittleFS mount échoue (test : volontairement supprimer la partition LittleFS) : step 1 blink rouge, halt
- [ ] **B5** Cleanup `.tmp` orphelins : créer `/loops/slot05.tmp` via Python serial, reboot, vérifier suppression

### Level 2 — LoopSlotStore unit-level

- [ ] **S1** Bitmask occupancy au boot
- [ ] **S2** `saveSlot()` sur loop EMPTY : refus (eventCount == 0)
- [ ] **S3** `saveSlot()` sur loop avec 1 event : fichier créé, taille 40 octets
- [ ] **S4** `saveSlot()` sur loop max events (1024) : taille 8224 octets, < 200 ms
- [ ] **S5** `loadSlot()` sur slot inexistant : retourne false
- [ ] **S6** `loadSlot()` sur slot valide : events restaurés byte-pour-byte
- [ ] **S7** `loadSlot()` sur fichier corrompu (modifier le magic via Python) : retourne false, bitmask mis à false
- [ ] **S8** `deleteSlot()` sur slot occupé : fichier supprimé
- [ ] **S9** `deleteSlot()` sur slot vide : retourne true
- [ ] **S10** Power loss simulation pendant save : reboot, ancien `.lpb` toujours valide

### Level 3 — LoopEngine serialize/deserialize round-trip

- [ ] **R1** Round-trip simple : record, save, clear, load → loop joue pareil
- [ ] **R2** Round-trip avec params modifiés : params chargés écrasent les modifications
- [ ] **R3** Round-trip de l'état STOPPED
- [ ] **R4** Round-trip de l'état PLAYING avec overdub : overdub disparaît au load
- [ ] **R5** Hard-cut + quantize PLAYING : pas de clic audible, beat aligné

### Level 4 — Tool 3 refactor contextuel (b1)

- [ ] **T1** Tool 3 affiche 3 sous-pages navigables
- [ ] **T2** Sous-page Banks : 8 bank pads, collisions internes interdites
- [ ] **T3** Sous-page Arpeg : 21 rôles, collisions internes interdites
- [ ] **T4** Sous-page Loop : 3 LOOP ctrl + 16 slot pads, collisions internes interdites
- [ ] **T5** Collision inter-contexte autorisée (Root C ARPEG + Slot 5 LOOP)
- [ ] **T6** Collision bank ⊥ loop interdite
- [ ] **T7** Sur ARPEG foreground, pads "slot 5" jouent comme musique
- [ ] **T8** Sur LOOP foreground, pad mappé Root C/Slot 5 réagit comme Slot 5

### Level 5 — Geste utilisateur complet

- [ ] **G1** Save sur slot vide
- [ ] **G2** Save sur slot occupé : refus
- [ ] **G3** Save sur loop EMPTY : refus
- [ ] **G4** Load sur slot occupé en STOPPED
- [ ] **G5** Load sur slot vide : refus
- [ ] **G6** Load sur slot occupé en PLAYING : hard-cut + beat aligné
- [ ] **G7** Tap < 300 ms : annulation silencieuse
- [ ] **G8** Long press > 2 s : save normal puis idle
- [ ] **G9** Delete combo (clear puis slot) : double rouge
- [ ] **G10** Delete combo inverse (slot puis clear) : pas de delete
- [ ] **G11** Delete sur slot vide : refus

### Level 6 — Verrous & invariants

- [ ] **V1** Recording lock : aucune action slot pendant RECORDING
- [ ] **V2** Overdubbing lock : idem
- [ ] **V3** Foreground non-LOOP : aucune action slot
- [ ] **V4** Bank switch pendant slot tracking : pas d'action après switch
- [ ] **V5** Catch re-arm après load : bargraph montre uncaught
- [ ] **V6** Tempo runtime préservé après load
- [ ] **V7** `_recordBpm` préservé après load (verify via debug serial)

### Level 7 — Performance & stabilité

- [ ] **P1** Save d'un slot full (1024 events) : pas de glitch audio
- [ ] **P2** 16 saves consécutifs (remplir tous les slots)
- [ ] **P3** 100 cycles save/delete/save sur le même slot
- [ ] **P4** Reboot stress : 20 reboots, drive plein, tous les slots toujours présents

---

## Files Created

| File | Content |
|---|---|
| `partitions_illpad.csv` | Custom partition table with `loopfs` 512 KB partition |
| `src/loop/LoopSlotStore.h` | LoopSlotStore class + LoopSlotHeader format + magic/version |
| `src/loop/LoopSlotStore.cpp` | LittleFS mount, save/load/delete, occupancy bitmask, .tmp cleanup |

## Files Modified

| File | Changes |
|---|---|
| `platformio.ini` | `board_build.partitions = partitions_illpad.csv` |
| `src/loop/LoopEngine.h` | `serializeToBuffer()`, `deserializeFromBuffer(..., float currentBPM)` declarations (B4); inline `getBaseVelocity()` / `getVelocityVariation()` getters (B5) |
| `src/loop/LoopEngine.cpp` | Both methods implemented; quantize-snap on PLAYING uses `currentBPM` not `_recordBpm` (B4 fix); include `LoopSlotStore.h` |
| `src/core/LedController.h` | 4 new ConfirmType values; `showSlotSaveRamp()` decl + `_slotRampPct/_showingSlotRamp` members |
| `src/core/LedController.cpp` | `showSlotSaveRamp()` impl; expiry + rendering of 4 confirms; slot save ramp overlay in `renderNormalDisplay()` |
| `src/main.cpp` | `s_loopSlotStore` instance; boot reorder (LittleFS step 1 mounted at `/littlefs`, LED hardware silent — Q3); `s_slotLastState/PressStart/Consumed/clearConsumedByCombo` statics + init; `handleLoopSlots()` function with **`currentBPM = s_clockManager.getSmoothedBPMFloat()` retrieval and `loadSlot(..., currentBPM)` call (B4)**, **`slot.baseVelocity/variation` writeback from `eng->getBaseVelocity/Variation()` before `reloadPerBankParams` (B5 + Q2)**; pipeline call after `handleLoopControls()`; `handleLoopControls()` coordination via `s_clearConsumedByCombo`; `loadAll()` extra arg |
| `src/loop/LoopSlotStore.h` | `loadSlot(..., float currentBPM)` signature (B4); `slotPath()` buffer size 24→32 to fit `/littlefs/loops/...` (Q3) |
| `src/loop/LoopSlotStore.cpp` | `loadSlot()` body forwards `currentBPM` to `deserializeFromBuffer()` (B4); `LittleFS.begin(true, "/littlefs", 10, "loopfs")` and all path strings prefixed `/littlefs/loops/` (Q3); all `char path[24]` → `char path[32]` (Q3) |
| `src/setup/ToolPadRoles.h` | `ROLE_LOOP_SLOT_0..15` enum values (REPLACE in place — A3); `_wkLoopSlotPads[16]`, `_loopSlotPads` member; `begin()` extra param |
| `src/setup/ToolPadRoles.cpp` | Slot label tables; LOOP sub-page POOL_OFFSETS_LOOP / TOTAL_POOL_LOOP / MAP_LOOP / REV_LOOP **REPLACED in place** with the 20-line / 26-entry versions (A3 fix — do NOT duplicate from Phase 3); `linearToPool/poolToLinear` slot mapping; `buildRoleMap` slot loop; `drawPool` 16 slot lines; `getRoleForPad/findPadWithRole/assignRole/clearRole/clearAllRoles/isPadOccupiedInContext/saveAll/run` slot extensions; `printRoleDescription` slot case; `poolItemLabel/poolLineSize` slot defaults; case 9 description text update |
| `src/setup/SetupManager.h` | `begin()` extra `loopSlotPads` param |
| `src/setup/SetupManager.cpp` | Forward `loopSlotPads` to `_toolRoles.begin()` |
| `src/managers/NvsManager.h` | `loadAll()` extra `loopSlotPads` param |
| `src/managers/NvsManager.cpp` | `loadAll()` LoopPadStore section reads `slotPads[16]` into the pointer |
| `src/setup/ToolBankConfig.cpp` | LOOP description appends "16 persistent slots" line |
| `docs/manual/loop-workflow.html` | New Slot Drive section (manual update) |

## Files NOT Modified

| File | Why |
|---|---|
| `src/managers/PotRouter.cpp` | Catch re-arm uses existing `loadStoredPerBank` + `loadStoredPerBankLoop` |
| `src/managers/BankManager.cpp` | Bank switch lock already handles LOOP recording |
| `src/arp/*` | Untouched |
| `src/midi/*` | Untouched |

---

## LOOP Slot Drive — Phase 6 Complete

After Phase 6, the LOOP Slot Drive is fully functional:

- 16 persistent slots survive reboot
- Save / load / delete via hold-left + slot pad gestures
- Tool 3 LOOP sub-page allows mapping any 16 pads to slots
- Hard-cut + beat quantize on PLAYING load (no audible click, beat aligned)
- Catch system re-arms after load (per existing PotRouter pattern)
- Recording lock denies all slot operations during RECORDING/OVERDUBBING
- LED feedback: green (load), white (save + ramp), red (refuse), red double (delete)
- Web UI hook ready: `LittleFS.serveStatic("/loops", ...)` exposes the drive in one line

The full LOOP feature set is complete: recording, overdubbing, persistent slots, effects, contextual setup UI.
