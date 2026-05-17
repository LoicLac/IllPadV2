# Viewer bidirectionnel Phase 2 — Plan d'implémentation firmware

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implémenter 5 write commands viewer→firmware (`!CLOCKMODE` + 4 ARPEG_GEN per-bank), 2 nouveaux events (`[BANK_SETTINGS]`, `[ERROR] cmd=`), et un mécanisme de persistence NVS asynchrone debounce 500 ms — sans bloquer Core 1 loop, sans casser la non-régression Tool 5 / Tool 8 / setup mode.

**Architecture:** Phase 2.A étend `NvsManager` avec un système queue+debounce pour `SettingsStore` et `BankTypeStore` (parallèle au pattern pot existant), tous deux flushés via le NVS background task sans blocage Core 1. Phase 2.B étend `ViewerSerial::pollCommands()` avec un dispatcher `dispatchWriteCommand` et 2 handlers (`handleClockMode`, `handleArpGenParam`), plus un nouvel `emitBankSettings` et l'extension du boot dump / auto-resync. Aucune modification de `ClockManager.cpp` ni `ArpEngine.cpp` — les setters existants sont déjà thread-safe Core 1 mono-threaded.

**Tech Stack:** ESP32-S3 N8R16, Arduino framework, PlatformIO, FreeRTOS, NVS (Preferences API), DEBUG_SERIAL gating, namespace `viewer` (anonymous internal), `std::atomic` pour dirty flags inter-task.

**Spec source:** [`docs/superpowers/specs/2026-05-17-viewer-bidirectional-phase2-design.md`](../specs/2026-05-17-viewer-bidirectional-phase2-design.md) (838 lignes, validée 2026-05-17 + amendée par cross-audit 2026-05-17 commits `1d65f9d` et `74b5fa6`).

**Build:**
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1              # compile
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload    # flash
~/.platformio/penv/bin/pio device monitor -b 115200                # serial monitor
```

---

## File Structure

| Fichier | Responsabilité |
|---|---|
| `src/managers/NvsManager.h` | Phase 2 : private members (dirty/pending/lastChange/_loadedBankType), public API (`queueSettingsWrite`, `queueBankTypeFromCache`, `getLoadedBankType`, `setLoadedBankType`), private save methods (`saveSettings`, `saveBankType`). |
| `src/managers/NvsManager.cpp` | Phase 2 : init constructor (list + body), bodies des queue/getter/setter, populate `_loadedBankType` dans `loadAll`, bodies `saveSettings`/`saveBankType`, extension `commitAll`, extension `tickPotDebounce` (renommé `tickDebounce`). |
| `src/main.cpp` | Update call site `s_nvsManager.tickPotDebounce` → `tickDebounce` (1 site ligne 1363). |
| `src/viewer/ViewerSerial.h` | Phase 2 : déclaration `emitBankSettings(uint8_t)`. |
| `src/viewer/ViewerSerial.cpp` | Phase 2 : impl `emitBankSettings`, buffer `cmdBuf` 16→24, flag `s_cmdOverflow` + emit `too_long`, namespace anonyme : `enum ArpGenArg`, `dispatchWriteCommand`, `handleClockMode`, `handleArpGenParam`. Extension boot dump + auto-resync + `?BOTH`/`?ALL`/`?STATE` avec boucle `[BANK_SETTINGS]`. |

Aucun nouveau fichier créé. Toutes les modifs sont des additions / extensions à des fichiers existants.

---

## Task 1: Pre-flight — Read spec + verify branch state

**Files:**
- Read: `docs/superpowers/specs/2026-05-17-viewer-bidirectional-phase2-design.md`
- Read: `STATUS.md` (focus courant)

- [ ] **Step 1: Re-read spec firmware Phase 2 in full**

Don't trust memory. Read all 838 lines. Confirm :
- §3 ranges (BONUS 10..20, MARGIN 3..12, PROX 4..20, ECART 1..12)
- §6.1 ordre handler `!CLOCKMODE` (runtime → s_settings → queue → emitSettings → emitClockSource)
- §6.2 format `[BANK_SETTINGS] bank=N bonus=X margin=Y prox=Z ecart=W`
- §7.2 8 codes d'erreur
- §10.1 init constructor obligatoire
- §10.5 placement EN TÊTE des checks Phase 2 dans `tickPotDebounce`
- §19 acceptations findings audit (R1, R3, R4, Y1, Y2, Y3)

- [ ] **Step 2: Verify branch + clean working tree**

Run:
```bash
cd /Users/loic/Code/PROJECTS/ILLPAD_V2
git status -sb
git rev-parse --abbrev-ref HEAD
```
Expected: branch `main`, clean tree (ou seulement docs `.md` non commités si déjà en cours de session).

- [ ] **Step 3: Verify build clean baseline**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning. Note RAM/Flash baseline pour comparaison post-Phase 2 :
```
RAM:   [==        ]  XX.X% (used ...)
Flash: [==        ]  XX.X% (used ...)
```

- [ ] **Step 4: Confirm spec line refs still valid**

Spot-check (les line numbers cités dans la spec doivent matcher le code actuel) :
```bash
grep -n "bool _masterMode" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/midi/ClockManager.h          # expect line 66
grep -n "void setMasterMode" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/midi/ClockManager.cpp     # expect line 252
grep -n "static char    cmdBuf" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/viewer/ViewerSerial.cpp # expect line 282
grep -n "if (!_potRightPendingSave" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/NvsManager.cpp # expect line 258
grep -n "viewer::pollCommands();" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp             # expect line 1292
grep -n "s_nvsManager.tickPotDebounce" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/main.cpp        # expect line 1363
```
Expected: tous les line numbers matchent. Si divergence, **arrêter** et re-vérifier le commit courant vs spec.

---

## Task 2: NvsManager — Add private members + init constructor

**Files:**
- Modify: `src/managers/NvsManager.h:115` (insert after `_anyDirty` field)
- Modify: `src/managers/NvsManager.h:128` (insert after `_loadedEcart[NUM_BANKS]`)
- Modify: `src/managers/NvsManager.cpp:32-33` (extend constructor list initializer)
- Modify: `src/managers/NvsManager.cpp:37-42` (extend constructor body loop)

- [ ] **Step 1: Add dirty flags + pending data in `NvsManager.h` private section**

Open `src/managers/NvsManager.h`. After line 114 (`std::atomic<bool> _anyDirty;`) and BEFORE line 116 (`// Pending data (...)`), insert :

```cpp
  // Phase 2 : Settings + BankType queue (dirty + pending + debounce)
  std::atomic<bool> _settingsDirty;
  std::atomic<bool> _bankTypeDirty;
```

Then after line 128 (`uint8_t _loadedEcart[NUM_BANKS];`) and BEFORE line 129 (`uint16_t _pendingTempo;`), insert :

```cpp
  // Phase 2 : cache BankType (parallèle aux _loadedQuantize[], _loadedScaleGroup[]).
  // Peuplé par loadAll() depuis bts.types[], modifié par setLoadedBankType().
  // Lu par saveBankType() pour reconstruire le BankTypeStore complet.
  uint8_t  _loadedBankType[NUM_BANKS];
  // Phase 2 : Pending struct + debounce timers (séparés du pot debounce).
  SettingsStore _pendingSettings;
  uint32_t      _settingsLastChangeMs;
  uint32_t      _bankTypeLastChangeMs;
  bool          _settingsPendingSave;
  bool          _bankTypePendingSave;
```

- [ ] **Step 2: Extend constructor list initializer in `NvsManager.cpp`**

Open `src/managers/NvsManager.cpp`. After line 32 (`, _anyPadPressed(false)`) and BEFORE line 33 (`{`), insert :

```cpp
  // Phase 2 init (cf spec §10.1)
  , _settingsDirty(false)
  , _bankTypeDirty(false)
  , _settingsLastChangeMs(0)
  , _bankTypeLastChangeMs(0)
  , _settingsPendingSave(false)
  , _bankTypePendingSave(false)
```

- [ ] **Step 3: Extend constructor body loop in `NvsManager.cpp`**

Inside the constructor body, after the existing loop at lines 37-42 that initializes `_loadedBonusPile/MarginWalk/Proximity/Ecart`, add a NEW loop. Locate this block :

```cpp
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _loadedBonusPile[i]  = 15;
    _loadedMarginWalk[i] = 7;
    _loadedProximity[i]  = 4;
    _loadedEcart[i]      = 5;
  }
```

Insert immediately after the closing `}` of this loop :

```cpp
  // Phase 2 : default BankType cache (overridden by loadAll()).
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _loadedBankType[i] = BANK_NORMAL;
  }
```

- [ ] **Step 4: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning. RAM may grow by ~50 bytes (8 bytes `_loadedBankType` + ~28 bytes pending struct + 16 bytes timers/bools).

- [ ] **Step 5: Auto-review**

Run:
```bash
grep -n "_settingsDirty\|_bankTypeDirty\|_settingsPendingSave\|_bankTypePendingSave\|_settingsLastChangeMs\|_bankTypeLastChangeMs\|_loadedBankType\|_pendingSettings" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/NvsManager.h /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/NvsManager.cpp
```
Expected: header lists all fields declared. cpp constructor inits each one explicitly.

- [ ] **Step 6: Commit**

```bash
cd /Users/loic/Code/PROJECTS/ILLPAD_V2
git add src/managers/NvsManager.h src/managers/NvsManager.cpp
git commit -m "nvs(phase2): add Settings/BankType queue members + constructor init

Adds private fields for viewer-driven Phase 2 NVS writes :
- _settingsDirty, _bankTypeDirty (atomic dirty flags)
- _pendingSettings (copy buffer)
- _settingsLastChangeMs, _bankTypeLastChangeMs (debounce timers)
- _settingsPendingSave, _bankTypePendingSave (pending flags)
- _loadedBankType[NUM_BANKS] (cache parallel to _loadedQuantize)

Constructor initializes all fields explicitly (UB guard) and seeds
_loadedBankType defaults to BANK_NORMAL (overridden by loadAll())."
```

---

## Task 3: NvsManager — Add getLoadedBankType / setLoadedBankType

**Files:**
- Modify: `src/managers/NvsManager.h:48` (insert after `setLoadedScaleGroup` declaration)
- Modify: `src/managers/NvsManager.cpp:1005` (insert after `setLoadedScaleGroup` impl)

- [ ] **Step 1: Add public API declarations in `NvsManager.h`**

In `NvsManager.h`, locate the `setLoadedScaleGroup` declaration around line 47 :
```cpp
  uint8_t getLoadedScaleGroup(uint8_t bank) const;
  void    setLoadedScaleGroup(uint8_t bank, uint8_t group);
```

Insert immediately after these 2 lines :
```cpp
  // Phase 2 : BankType cache (parallèle à _loadedQuantize[], _loadedScaleGroup[]).
  // Peuplé par loadAll() puis utilisé par saveBankType() pour reconstruire le blob.
  uint8_t getLoadedBankType(uint8_t bank) const;   // returns BANK_NORMAL if bank out of range
  void    setLoadedBankType(uint8_t bank, uint8_t type);
```

- [ ] **Step 2: Add bodies in `NvsManager.cpp`**

In `NvsManager.cpp`, locate `setLoadedScaleGroup` body (around line 1002-1004) :
```cpp
void NvsManager::setLoadedScaleGroup(uint8_t bank, uint8_t group) {
  if (bank < NUM_BANKS && group <= NUM_SCALE_GROUPS) _loadedScaleGroup[bank] = group;
}
```

Insert immediately after the closing `}` :
```cpp

uint8_t NvsManager::getLoadedBankType(uint8_t bank) const {
  if (bank >= NUM_BANKS) return BANK_NORMAL;
  return _loadedBankType[bank];
}

void NvsManager::setLoadedBankType(uint8_t bank, uint8_t type) {
  if (bank < NUM_BANKS && type <= BANK_ARPEG_GEN) _loadedBankType[bank] = type;
}
```

Note: `BANK_ARPEG_GEN = 3` is the max valid value per `KeyboardData.h:345-350` (`BANK_ANY = 0xFF` is a sentinel, not a real BankType, so clamp at `<=3`).

- [ ] **Step 3: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning.

- [ ] **Step 4: Commit**

```bash
git add src/managers/NvsManager.h src/managers/NvsManager.cpp
git commit -m "nvs(phase2): add getLoadedBankType / setLoadedBankType API

Parallel to _loadedQuantize / _loadedScaleGroup pattern. Read-only at
boot (loadAll populates), writeable runtime via setLoadedBankType (no
runtime callers Phase 2 — kept for future BankType-change runtime path
ou Tool 5 setup mirror). Range check : type <= BANK_ARPEG_GEN (=3).
BANK_ANY (=0xFF) is a sentinel, not a valid persisted type."
```

---

## Task 4: NvsManager — Populate _loadedBankType in loadAll

**Files:**
- Modify: `src/managers/NvsManager.cpp:648-655` (success path : add `_loadedBankType[i] = bts.types[i];`)
- Modify: `src/managers/NvsManager.cpp:664-672` (defaults path : add `_loadedBankType[i] = banks[i].type;`)

- [ ] **Step 1: Add to success path (NVS load OK)**

In `NvsManager.cpp`, locate the success path inside `loadAll()` :

```cpp
      validateBankTypeStore(bts);
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        banks[i].type        = (BankType)bts.types[i];
        _loadedQuantize[i]   = bts.quantize[i];
        _loadedScaleGroup[i] = bts.scaleGroup[i];
        _loadedBonusPile[i]  = bts.bonusPilex10[i];
        _loadedMarginWalk[i] = bts.marginWalk[i];
        _loadedProximity[i]  = bts.proximityFactorx10[i];
        _loadedEcart[i]      = bts.ecart[i];
      }
```

Inside the `for` loop body, after `banks[i].type = (BankType)bts.types[i];`, add :
```cpp
        _loadedBankType[i]   = bts.types[i];
```

Final block becomes :
```cpp
      validateBankTypeStore(bts);
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        banks[i].type        = (BankType)bts.types[i];
        _loadedBankType[i]   = bts.types[i];
        _loadedQuantize[i]   = bts.quantize[i];
        _loadedScaleGroup[i] = bts.scaleGroup[i];
        _loadedBonusPile[i]  = bts.bonusPilex10[i];
        _loadedMarginWalk[i] = bts.marginWalk[i];
        _loadedProximity[i]  = bts.proximityFactorx10[i];
        _loadedEcart[i]      = bts.ecart[i];
      }
```

- [ ] **Step 2: Add to defaults path (NVS load fail / fresh boot)**

Just after, locate the `else` branch (defaults factory) :
```cpp
    } else {
      // Premier boot / NVS vierge / v3->v4 : defaults usine = 4 NORMAL + 4 ARPEG,
      // group A sur banques 1,2 (NORMAL) et 5,6 (ARPEG). Identique au reset 'd' de Tool 4.
      // ARPEG_GEN params : bonusPilex10=15, marginWalk=7, proximity=4 (=0.4), ecart=5.
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        banks[i].type        = (i < 4) ? BANK_NORMAL : BANK_ARPEG;
        _loadedQuantize[i]   = DEFAULT_ARP_START_MODE;
        _loadedScaleGroup[i] = (i == 0 || i == 1 || i == 4 || i == 5) ? 1 : 0;
        _loadedBonusPile[i]  = 15;
        _loadedMarginWalk[i] = 7;
        _loadedProximity[i]  = 4;
        _loadedEcart[i]      = 5;
      }
```

Inside the `for` loop body, after `banks[i].type = (i < 4) ? BANK_NORMAL : BANK_ARPEG;`, add :
```cpp
        _loadedBankType[i]   = (i < 4) ? BANK_NORMAL : BANK_ARPEG;
```

- [ ] **Step 3: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning.

- [ ] **Step 4: Auto-review**

Run:
```bash
grep -n "_loadedBankType\[" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/NvsManager.cpp
```
Expected: at least 3 occurrences (constructor body + loadAll success + loadAll defaults). NO orphan write site.

- [ ] **Step 5: Commit**

```bash
git add src/managers/NvsManager.cpp
git commit -m "nvs(phase2): populate _loadedBankType in loadAll (success + defaults paths)

Cache _loadedBankType[NUM_BANKS] mirrors bts.types[] loaded from NVS.
Success path: copy from validated store. Defaults path: same logic as
banks[i].type assignment (4 NORMAL + 4 ARPEG factory layout).

Synchronizes with the s_banks[i].type pointer for runtime, but lives
inside NvsManager so saveBankType() can rebuild the blob without
dereferencing main.cpp globals (pattern coherent with _loadedQuantize)."
```

---

## Task 5: NvsManager — Add queueSettingsWrite + queueBankTypeFromCache

**Files:**
- Modify: `src/managers/NvsManager.h:35` (insert after `queueBankWrite` declaration)
- Modify: `src/managers/NvsManager.cpp:330` (insert after `queueBankWrite` body)

- [ ] **Step 1: Add public API declarations in `NvsManager.h`**

In `NvsManager.h`, locate around line 19 the queue declarations group, and add 2 new declarations at the end of that group (just before line 32 `// --- Blocking reads...`). Locate :
```cpp
  void queuePadOrderWrite(const uint8_t* order);
```

Insert immediately after this line :
```cpp

  // Phase 2 : viewer-driven write commands (debounced 500ms via tickDebounce).
  // Caller (handler !CLOCKMODE) must update s_settings BEFORE calling this.
  void queueSettingsWrite(const SettingsStore& settings);
  // Phase 2 : viewer-driven BankTypeStore write. Caller (handler ARPEG_GEN)
  // must have called setLoadedBonusPile/MarginWalk/etc. BEFORE this — the
  // blob is reconstructed from internal _loadedX[] arrays at save time.
  void queueBankTypeFromCache();
```

- [ ] **Step 2: Add bodies in `NvsManager.cpp`**

In `NvsManager.cpp`, locate the end of the queue methods group around line 398 (after `queuePadOrderWrite` impl) :

```cpp
void NvsManager::queuePadOrderWrite(const uint8_t* order) {
  memcpy(_pendingPadOrder, order, NUM_KEYS);
  _padOrderDirty = true;
  _anyDirty = true;
}
```

Insert immediately after the closing `}` :
```cpp

void NvsManager::queueSettingsWrite(const SettingsStore& settings) {
  // Phase 2 : copy snapshot, arm debounce timer.
  // _settingsDirty NOT set here — tickDebounce will set it after 500ms of
  // inactivity so commitAll() doesn't fire prematurely (stream slider viewer).
  _pendingSettings = settings;
  _settingsLastChangeMs = millis();
  _settingsPendingSave = true;
}

void NvsManager::queueBankTypeFromCache() {
  // Phase 2 : arm debounce timer only. The actual blob is reconstructed
  // from _loadedX[] arrays at save time (cf saveBankType). Caller must
  // have updated setLoadedBonusPile / setLoadedMarginWalk / etc. before
  // calling this.
  _bankTypeLastChangeMs = millis();
  _bankTypePendingSave = true;
}
```

- [ ] **Step 3: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning.

- [ ] **Step 4: Commit**

```bash
git add src/managers/NvsManager.h src/managers/NvsManager.cpp
git commit -m "nvs(phase2): add queueSettingsWrite / queueBankTypeFromCache API

Both methods arm a debounce timer (500ms) instead of marking dirty
immediately — actual mark happens in tickDebounce after timeout. Avoids
NVS flash wear under stream slider viewer (firmware debounces, viewer
can spam !BONUS=N as fast as user drags).

queueSettingsWrite copies the SettingsStore snapshot into _pendingSettings.
queueBankTypeFromCache rebuilds the blob from _loadedX[] arrays at save
time (no pending struct duplicating state)."
```

---

## Task 6: NvsManager — Add saveSettings + saveBankType + extend commitAll

**Files:**
- Modify: `src/managers/NvsManager.h:161` (insert before `};` private section ends)
- Modify: `src/managers/NvsManager.cpp:498` (insert in `commitAll` after existing saves)
- Modify: `src/managers/NvsManager.cpp:1125` (append after `savePadOrder` impl)

- [ ] **Step 1: Add private save declarations in `NvsManager.h`**

In `NvsManager.h`, locate the private save methods around line 156-161 :
```cpp
  // Internal save methods
  void saveBank();
  void savePotParams();
  void saveTempo();
  void saveLedBrightness();
  void savePadSensitivity();
  void savePadOrder();
};
```

Insert immediately before the closing `};` :
```cpp
  // Phase 2 : Settings + BankType saves (called from commitAll via NVS task).
  void saveSettings();
  void saveBankType();
};
```

- [ ] **Step 2: Extend `commitAll()` body in `NvsManager.cpp`**

In `NvsManager.cpp`, locate the existing `commitAll()` tail around lines 494-499 :
```cpp
  if (_potDirty)         { savePotParams();     _potDirty = false; }
  if (_tempoDirty)       { saveTempo();         _tempoDirty = false; }
  if (_ledBrightDirty)   { saveLedBrightness(); _ledBrightDirty = false; }
  if (_padSensDirty)     { savePadSensitivity(); _padSensDirty = false; }
  if (_padOrderDirty)    { savePadOrder();       _padOrderDirty = false; }
}
```

Insert 2 new `if` blocks BEFORE the closing `}` :
```cpp
  if (_potDirty)         { savePotParams();     _potDirty = false; }
  if (_tempoDirty)       { saveTempo();         _tempoDirty = false; }
  if (_ledBrightDirty)   { saveLedBrightness(); _ledBrightDirty = false; }
  if (_padSensDirty)     { savePadSensitivity(); _padSensDirty = false; }
  if (_padOrderDirty)    { savePadOrder();       _padOrderDirty = false; }
  // Phase 2 : viewer-driven NVS writes (debounced 500ms via tickDebounce).
  if (_settingsDirty)    { saveSettings();      _settingsDirty = false; }
  if (_bankTypeDirty)    { saveBankType();      _bankTypeDirty = false; }
}
```

- [ ] **Step 3: Add `saveSettings` + `saveBankType` impl at end of file**

In `NvsManager.cpp`, locate the end of `savePadOrder()` around line 1125 :
```cpp
void NvsManager::savePadOrder() {
  Preferences prefs;
  if (prefs.begin(NOTEMAP_NVS_NAMESPACE, false)) {
    NoteMapStore nms;
    nms.magic = EEPROM_MAGIC;
    nms.version = NOTEMAP_VERSION;
    nms.reserved = 0;
    memcpy(nms.noteMap, _pendingPadOrder, NUM_KEYS);
    prefs.putBytes(NOTEMAP_NVS_KEY, &nms, sizeof(NoteMapStore));
    prefs.end();
    #if DEBUG_SERIAL
    Serial.println("[NVS] Saved pad order.");
    #endif
  }
}

```

Append AFTER this closing `}` (at the very end of the file, before EOF) :
```cpp

// =================================================================
// Phase 2 — Settings + BankType saves (viewer-driven, NVS task)
// =================================================================
// Both called from commitAll() on the NVS background task (Core 1).
// _anyPadPressed guard (commitAll head) defers writes during live play.

void NvsManager::saveSettings() {
  // Direct saveBlob — NVS task is background, so the blocking flash write
  // does not stall Core 1 main loop. Pattern matches existing savePotParams.
  NvsManager::saveBlob(SETTINGS_NVS_NAMESPACE, SETTINGS_NVS_KEY,
                       &_pendingSettings, sizeof(_pendingSettings));
  #if DEBUG_SERIAL
  Serial.println("[NVS] Saved settings.");
  #endif
}

void NvsManager::saveBankType() {
  // Rebuild BankTypeStore from internal _loadedX[] arrays at save time
  // (single source of truth, no _pendingBankType duplicating state).
  // _loadedBankType[i] populated by loadAll() + maintained by Phase 2 path
  // (currently no runtime BankType change — palier rouge cf spec §17).
  BankTypeStore bts;
  bts.magic    = EEPROM_MAGIC;
  bts.version  = BANKTYPE_VERSION;
  bts.reserved = 0;
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    bts.types[i]              = _loadedBankType[i];
    bts.quantize[i]           = _loadedQuantize[i];
    bts.scaleGroup[i]         = _loadedScaleGroup[i];
    bts.bonusPilex10[i]       = _loadedBonusPile[i];
    bts.marginWalk[i]         = _loadedMarginWalk[i];
    bts.proximityFactorx10[i] = _loadedProximity[i];
    bts.ecart[i]              = _loadedEcart[i];
  }
  validateBankTypeStore(bts);
  NvsManager::saveBlob(BANKTYPE_NVS_NAMESPACE, BANKTYPE_NVS_KEY_V2,
                       &bts, sizeof(bts));
  #if DEBUG_SERIAL
  Serial.println("[NVS] Saved bank type.");
  #endif
}
```

- [ ] **Step 4: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning. Flash usage may grow ~150-200 bytes (2 saveBlob calls).

- [ ] **Step 5: Auto-review**

Run:
```bash
grep -n "saveSettings\|saveBankType" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/NvsManager.h /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/NvsManager.cpp
```
Expected: header declares both, cpp defines both + commitAll calls them.

Run:
```bash
grep -n "SETTINGS_NVS_NAMESPACE\|BANKTYPE_NVS_NAMESPACE\|BANKTYPE_NVS_KEY_V2\|BANKTYPE_VERSION" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/core/KeyboardData.h
```
Expected: all symbols defined. They are : `SETTINGS_NVS_NAMESPACE` (KeyboardData.h:92), `BANKTYPE_NVS_NAMESPACE` + `BANKTYPE_NVS_KEY_V2` + `BANKTYPE_VERSION` (search ces lignes pour confirmer leur emplacement).

- [ ] **Step 6: Commit**

```bash
git add src/managers/NvsManager.h src/managers/NvsManager.cpp
git commit -m "nvs(phase2): add saveSettings + saveBankType + commitAll extension

Two new private save methods called from commitAll() on the NVS background
task (no Core 1 blocking — pattern matches savePotParams / saveTempo).

saveSettings : direct saveBlob of _pendingSettings.
saveBankType : rebuild BankTypeStore from _loadedX[] arrays then saveBlob.

commitAll() now flushes both after the existing saves. _anyPadPressed
guard (head of commitAll) defers all writes during live play."
```

---

## Task 7: NvsManager — Extend tickPotDebounce (placement EN TÊTE)

**Files:**
- Modify: `src/managers/NvsManager.cpp:208-215` (insert new debounce block at the very top of function body)

- [ ] **Step 1: Add Phase 2 debounce checks AT TOP of `tickPotDebounce`**

In `NvsManager.cpp`, locate the function signature around line 208 :
```cpp
void NvsManager::tickPotDebounce(uint32_t now, bool rearDirty, bool rightDirty,
                                  const PotRouter& potRouter,
                                  uint8_t currentBank, BankType currentType) {
  // -------------------------------------------------------------------
  // Rear pot (tempo, LED brightness, pad sensitivity) — 2 s debounce.
```

Insert immediately after the opening `{` (and BEFORE the existing rear pot comment block ligne 211-216) :
```cpp
void NvsManager::tickPotDebounce(uint32_t now, bool rearDirty, bool rightDirty,
                                  const PotRouter& potRouter,
                                  uint8_t currentBank, BankType currentType) {
  // -------------------------------------------------------------------
  // Phase 2 : Settings + BankType debounce (500ms).
  // PLACEMENT CRITIQUE — en tête, AVANT le right-pot early return ligne 258.
  // Sinon ces checks ne seraient exécutés que 10s après un mouvement de
  // pot droit → NVS save settings/banktype silencieusement cassé.
  // Cf spec §10.5.
  // -------------------------------------------------------------------
  if (_settingsPendingSave && (now - _settingsLastChangeMs) >= 500) {
    _settingsDirty = true;
    _anyDirty = true;
    _settingsPendingSave = false;
  }
  if (_bankTypePendingSave && (now - _bankTypeLastChangeMs) >= 500) {
    _bankTypeDirty = true;
    _anyDirty = true;
    _bankTypePendingSave = false;
  }

  // -------------------------------------------------------------------
  // Rear pot (tempo, LED brightness, pad sensitivity) — 2 s debounce.
```

- [ ] **Step 2: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning.

- [ ] **Step 3: Auto-review**

Run:
```bash
grep -n "_settingsPendingSave\|_bankTypePendingSave" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/NvsManager.cpp
```
Expected: 6 occurrences total :
- 2 reads in `tickPotDebounce` (the checks just added)
- 2 writes in `tickPotDebounce` (the `= false;` after timeout)
- 2 writes in queue methods (`queueSettingsWrite` + `queueBankTypeFromCache`)

Verify the placement: the new checks come BEFORE the `if (rearDirty)` block.

```bash
sed -n '208,225p' /Users/loic/Code/PROJECTS/ILLPAD_V2/src/managers/NvsManager.cpp
```
Expected output shows the Phase 2 checks block immediately after the function signature opening brace.

- [ ] **Step 4: Commit**

```bash
git add src/managers/NvsManager.cpp
git commit -m "nvs(phase2): extend tickPotDebounce with Settings+BankType timeout checks (EN TÊTE)

Placement CRITIQUE — les checks Phase 2 sont placés en tête de fonction,
AVANT le right-pot early return ligne 258. Sinon ils ne s'exécuteraient
que 10s après un mouvement de pot droit, ce qui casserait silencieusement
la persistence NVS settings/banktype (le user toggle ClockMode dans le
viewer mais ne touche pas un pot droit → NVS jamais flush → reboot perd
la modif).

Le rear-pot debounce existant n'a pas d'early return séquentiel — il OK
de l'avoir après. Le right-pot early return reste là où il était."
```

---

## Task 8: Rename tickPotDebounce → tickDebounce

**Files:**
- Modify: `src/managers/NvsManager.h:92` (rename declaration)
- Modify: `src/managers/NvsManager.cpp:208` (rename definition)
- Modify: `src/main.cpp:1363` (rename call site)

- [ ] **Step 1: Rename declaration in `NvsManager.h`**

In `NvsManager.h`, locate around line 88-94 :
```cpp
  // Debounce: call with current millis, pot dirty states (rear + right tracked
  // separately so the rear pot saves faster — tempo/LED/PadSens are global
  // params adjusted via intentional gestures, not live-twiddled), pot router
  // for value snapshot, and current bank context for per-bank params.
  void tickPotDebounce(uint32_t now, bool rearDirty, bool rightDirty,
                        const PotRouter& potRouter,
                        uint8_t currentBank, BankType currentType);
```

Replace with :
```cpp
  // Debounce tick : called from loop() every iter.
  // Handles 3 debounce groups (independent timers) :
  //  - Rear pot (2s) : tempo, LED brightness, pad sensitivity
  //  - Right pots (10s) : shape/slew/deadzone, per-bank velocity/pitch/arp pots
  //  - Phase 2 (500ms) : viewer-driven settings + bankType writes
  // Args : pot dirty states, pot router snapshot, current bank context.
  void tickDebounce(uint32_t now, bool rearDirty, bool rightDirty,
                     const PotRouter& potRouter,
                     uint8_t currentBank, BankType currentType);
```

- [ ] **Step 2: Rename definition in `NvsManager.cpp`**

In `NvsManager.cpp`, locate the function definition around lines 204-210 :
```cpp
// =================================================================
// tickPotDebounce — split debounce: 2s for rear pot (tempo/LED/PadSens),
// 10s for right pots (per-bank params live-twiddled in performance).
// =================================================================
void NvsManager::tickPotDebounce(uint32_t now, bool rearDirty, bool rightDirty,
                                  const PotRouter& potRouter,
                                  uint8_t currentBank, BankType currentType) {
```

Replace with :
```cpp
// =================================================================
// tickDebounce — 3-group debounce (pot rear 2s / pot right 10s / Phase 2 500ms).
// Phase 2 checks placed in head — see spec §10.5 for the early-return rationale.
// =================================================================
void NvsManager::tickDebounce(uint32_t now, bool rearDirty, bool rightDirty,
                               const PotRouter& potRouter,
                               uint8_t currentBank, BankType currentType) {
```

- [ ] **Step 3: Rename call site in `main.cpp`**

In `main.cpp`, locate line 1363-1364 :
```cpp
  s_nvsManager.tickPotDebounce(now, rearDirty, rightDirty, s_potRouter,
                                s_bankManager.getCurrentBank(), s_bankManager.getCurrentSlot().type);
```

Replace `tickPotDebounce` with `tickDebounce` :
```cpp
  s_nvsManager.tickDebounce(now, rearDirty, rightDirty, s_potRouter,
                             s_bankManager.getCurrentBank(), s_bankManager.getCurrentSlot().type);
```

- [ ] **Step 4: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning. If a stray call site exists (search regression), the linker errors with `undefined reference to NvsManager::tickPotDebounce(...)`. In that case grep for the orphan call.

- [ ] **Step 5: Auto-review**

Run:
```bash
grep -rn "tickPotDebounce\|tickDebounce" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/
```
Expected: exactly 3 lines, all matching `tickDebounce` (header decl, cpp def, main.cpp call). Zero references to the old `tickPotDebounce` name.

- [ ] **Step 6: Commit**

```bash
git add src/managers/NvsManager.h src/managers/NvsManager.cpp src/main.cpp
git commit -m "nvs(phase2): rename tickPotDebounce → tickDebounce + update main call site

Le nom tickPotDebounce n'est plus représentatif depuis Phase 2 qui ajoute
2 groupes de debounce orthogonaux au pot (settings + bankType). Renommage
in-place + update 1 site dans main.cpp (1363). Signature inchangée."
```

---

## Task 9: HW gate Phase 2.A — Boot + non-régression Tool 5 / Tool 8

**Files:** none (HW validation only — no code change).

- [ ] **Step 1: Flash firmware**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload
```
Then open serial monitor :
```bash
~/.platformio/penv/bin/pio device monitor -b 115200
```

- [ ] **Step 2: Verify boot dump**

Boot log should contain (no regression vs Phase 1) :
- `[BOOT NVS] Bank types + quantize + scale groups + ARPEG_GEN params loaded (v4 store).` OR `[BOOT NVS] BankTypeStore absent/invalide ...` (defaults path).
- `[BOOT NVS] Settings: profile=N, atRate=N, bleInt=N, clock=N, dblTap=N`
- `[BOOT NVS] LED settings + color slots loaded.`
- No new `[FATAL]` messages.
- No new warnings.

Expected: identical boot trace to baseline (no new events emitted on Phase 2.A path — runtime is unchanged, only internal NvsManager fields exist).

- [ ] **Step 3: Verify Tool 5 (ARPEG_GEN config) non-regression**

Enter setup mode (hold rear button at boot). Navigate to Tool 5 ("Bank Config"). Verify :
- All bank types show correct (NORMAL/ARPEG/LOOP/ARPEG_GEN per user config).
- For ARPEG_GEN banks : BONUS / MARGIN / PROX / ECART display matching `_loadedBonusPile / _loadedMarginWalk / _loadedProximity / _loadedEcart` (= what boot loaded).
- Modify one ARPEG_GEN bank's BONUS value, exit Tool 5 (save).
- Exit setup → firmware reboots.
- Re-enter setup, Tool 5 → verify the modified value persists.

Expected: Tool 5 behavior IDENTICAL to pre-Phase 2.A. The new `_loadedBankType[]` cache does not interfere (it's read-only at this stage).

- [ ] **Step 4: Verify Tool 8 (Settings) non-regression**

In setup mode, navigate to Tool 8 ("Settings"). Verify :
- ClockMode shows current mode (slave/master).
- PanicReconnect / DoubleTapMs / AftertouchRate / BleInterval / BatAdcFull all show NVS-loaded values.
- Modify ClockMode (slave→master), exit Tool 8 (save).
- Exit setup → firmware reboots.
- Re-enter setup, Tool 8 → verify ClockMode persisted.

Expected: Tool 8 behavior IDENTICAL to pre-Phase 2.A. The new `_pendingSettings` field is unused yet (no Phase 2 handler exists).

- [ ] **Step 5: Verify runtime (no setup mode)**

Exit setup, reboot to runtime. Play pads on various bank types :
- NORMAL : notes play.
- ARPEG : arp plays, +note/-note in viewer log.
- ARPEG_GEN (if configured) : arp plays with mutation, [BANK_SETTINGS] NOT YET emitted (Phase 2.B not implemented).
- LOOP : skip (Phase 1 LOOP non implémenté runtime).
- Pot moves : tempo / LED bright / pad sens / per-bank arp params still respond + viewer logs [POT].

Expected: zero regression. All Phase 1 events still emitted correctly. CPU usage Core 0 ~92% (unchanged), Core 1 ~16%.

- [ ] **Step 6: Mark Phase 2.A complete**

If all checks pass, Phase 2.A is HW-validated. No commit (no code change in this task). Move to Phase 2.B.

If any check fails, **stop and diagnose**. Likely causes :
- `_loadedBankType[]` not populated → check Task 4 success/defaults paths.
- Constructor inits incomplete → check Task 2 (UB if a `_settingsPendingSave` is true at boot → spurious save trigger).
- `tickDebounce` rename not propagated → linker error, grep regression.

---

## Task 10: ViewerSerial — Add emitBankSettings

**Files:**
- Modify: `src/viewer/ViewerSerial.h:74` (insert after `emitSettings` declaration)
- Modify: `src/viewer/ViewerSerial.cpp:689` (insert after `emitSettings` impl)

- [ ] **Step 1: Add declaration in `ViewerSerial.h`**

In `ViewerSerial.h`, locate the Phase 1.D section around lines 72-74 :
```cpp
// --- Phase 1.D : [GLOBALS]/[SETTINGS] events ---
void emitGlobals();
void emitSettings();
```

Insert immediately after :
```cpp

// --- Phase 2 : [BANK_SETTINGS] event ---
// Émet [BANK_SETTINGS] bank=N bonus=X margin=Y prox=Z ecart=W
// Lit depuis s_nvsManager.getLoadedBonusPile(idx) etc. NE PAS appeler pour
// les banks dont type != BANK_ARPEG_GEN (no-op silencieux : retourne tôt).
// Émis dans : boot dump auto-resync (1 event par ARPEG_GEN), ?BOTH/?ALL,
// ?STATE foreground (si ARPEG_GEN), post-write !BONUS/MARGIN/PROX/ECART.
void emitBankSettings(uint8_t bankIdx);
```

- [ ] **Step 2: Add impl in `ViewerSerial.cpp`**

In `ViewerSerial.cpp`, locate the end of `emitSettings()` around line 688-689 :
```cpp
void emitSettings() {
  #if DEBUG_SERIAL
  // BleInterval emis en numerique (0..3) pour matcher le viewer pre-code
  // qui parse std::stoi(*v). Mapping interne :
  //   0=BLE_OFF, 1=BLE_LOW_LATENCY, 2=BLE_NORMAL, 3=BLE_BATTERY_SAVER.
  emit(PRIO_HIGH, "[SETTINGS] ClockMode=%s PanicReconnect=%u DoubleTapMs=%u "
                  "AftertouchRate=%u BleInterval=%u BatAdcFull=%u\n",
       s_settings.clockMode == CLOCK_MASTER ? "master" : "slave",
       s_settings.panicOnReconnect,
       s_settings.doubleTapMs,
       s_settings.aftertouchRate,
       s_settings.bleInterval,
       s_settings.batAdcAtFull);
  #endif
}
```

Insert immediately after the closing `}` :
```cpp

// =================================================================
// Phase 2 — [BANK_SETTINGS] event
// Émet bank=N bonus=X margin=Y prox=Z ecart=W pour les banks ARPEG_GEN.
// No-op silencieux pour les autres bank types (le firmware n'émet jamais
// [BANK_SETTINGS] pour une bank non-ARPEG_GEN — cf spec §6.2).
// =================================================================

void emitBankSettings(uint8_t bankIdx) {
  #if DEBUG_SERIAL
  if (bankIdx >= NUM_BANKS) return;
  if (s_banks[bankIdx].type != BANK_ARPEG_GEN) return;
  emit(PRIO_HIGH,
       "[BANK_SETTINGS] bank=%u bonus=%u margin=%u prox=%u ecart=%u\n",
       bankIdx + 1,
       s_nvsManager.getLoadedBonusPile(bankIdx),
       s_nvsManager.getLoadedMarginWalk(bankIdx),
       s_nvsManager.getLoadedProximityFactor(bankIdx),
       s_nvsManager.getLoadedEcart(bankIdx));
  #else
  (void)bankIdx;
  #endif
}
```

- [ ] **Step 3: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning.

- [ ] **Step 4: Auto-review**

Run:
```bash
grep -n "emitBankSettings" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/viewer/ViewerSerial.h /Users/loic/Code/PROJECTS/ILLPAD_V2/src/viewer/ViewerSerial.cpp
```
Expected: 2 occurrences (1 decl, 1 def). No callers yet — that's Task 11.

- [ ] **Step 5: Commit**

```bash
git add src/viewer/ViewerSerial.h src/viewer/ViewerSerial.cpp
git commit -m "viewer(phase2): add emitBankSettings(bankIdx) for ARPEG_GEN banks

Émet [BANK_SETTINGS] bank=N bonus=X margin=Y prox=Z ecart=W via
s_nvsManager.getLoadedX(idx). No-op silencieux si bank type != ARPEG_GEN
ou idx out of range (le firmware n'émet jamais cet event pour les autres
bank types).

Pas encore branché — sera consommé Task 11 (boot dump + auto-resync +
?BOTH/?ALL/?STATE) et Task 16 (post-write !BONUS/MARGIN/PROX/ECART)."
```

---

## Task 11: ViewerSerial — Extend boot dump + auto-resync + queries

**Files:**
- Modify: `src/viewer/ViewerSerial.cpp:79-100` (auto-resync block in taskBody)
- Modify: `src/viewer/ViewerSerial.cpp:288-313` (pollCommands : ?STATE, ?BOTH, ?ALL)

- [ ] **Step 1: Extend auto-resync block in `taskBody()`**

In `ViewerSerial.cpp`, locate the auto-resync block around lines 88-100 :
```cpp
    if (!wasConnected && nowConnected) {
      // Auto-resync : viewer vient de connecter (cold open apres firmware
      // deja boote). Re-emit le boot dump complet + [GLOBALS]/[SETTINGS] +
      // resetDbgSentinels pour forcer le re-emit des [POT] params au tick
      // debugOutput() suivant. Resout les races du boot dump (LED_Bright /
      // PadSens stuck sur sentinel 0xFF, viewer cells partial).
      // Push les events dans la queue — ils seront draines par les iters
      // suivants de cette meme task.
      emitBanksHeader(NUM_BANKS);
      for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
      for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
      emitGlobals();
      emitSettings();
      resetDbgSentinels();
      emitReady(s_bankManager.getCurrentBank() + 1);
    }
```

Insert a NEW loop after the `emitState` loop and BEFORE `emitGlobals()` :
```cpp
    if (!wasConnected && nowConnected) {
      // Auto-resync : viewer vient de connecter (cold open apres firmware
      // deja boote). Re-emit le boot dump complet + [GLOBALS]/[SETTINGS] +
      // resetDbgSentinels pour forcer le re-emit des [POT] params au tick
      // debugOutput() suivant. Resout les races du boot dump (LED_Bright /
      // PadSens stuck sur sentinel 0xFF, viewer cells partial).
      // Push les events dans la queue — ils seront draines par les iters
      // suivants de cette meme task.
      emitBanksHeader(NUM_BANKS);
      for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
      for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
      // Phase 2 : [BANK_SETTINGS] pour chaque bank ARPEG_GEN (no-op autre type).
      for (uint8_t i = 0; i < NUM_BANKS; i++) emitBankSettings(i);
      emitGlobals();
      emitSettings();
      resetDbgSentinels();
      emitReady(s_bankManager.getCurrentBank() + 1);
    }
```

- [ ] **Step 2: Extend `?STATE` branch in pollCommands**

In `ViewerSerial.cpp`, locate the `?STATE` branch around lines 288-290 :
```cpp
      if (strcmp(cmdBuf, "?STATE") == 0) {
        emitState(s_bankManager.getCurrentBank());
        emitReady(s_bankManager.getCurrentBank() + 1);
      }
```

Replace with :
```cpp
      if (strcmp(cmdBuf, "?STATE") == 0) {
        emitState(s_bankManager.getCurrentBank());
        // Phase 2 : émet [BANK_SETTINGS] si foreground ARPEG_GEN (no-op sinon).
        emitBankSettings(s_bankManager.getCurrentBank());
        emitReady(s_bankManager.getCurrentBank() + 1);
      }
```

- [ ] **Step 3: Extend `?BOTH` branch in pollCommands**

Locate the `?BOTH` branch around lines 295-302 :
```cpp
      } else if (strcmp(cmdBuf, "?BOTH") == 0) {
        emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
        emitGlobals();
        emitSettings();
        resetDbgSentinels();
        emitReady(s_bankManager.getCurrentBank() + 1);
      }
```

Replace with :
```cpp
      } else if (strcmp(cmdBuf, "?BOTH") == 0) {
        emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
        // Phase 2 : [BANK_SETTINGS] pour chaque bank ARPEG_GEN.
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBankSettings(i);
        emitGlobals();
        emitSettings();
        resetDbgSentinels();
        emitReady(s_bankManager.getCurrentBank() + 1);
      }
```

- [ ] **Step 4: Extend `?ALL` branch in pollCommands**

Locate the `?ALL` branch around lines 303-313 :
```cpp
      } else if (strcmp(cmdBuf, "?ALL") == 0) {
        emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
        emitGlobals();
        emitSettings();
        resetDbgSentinels();
        dumpLedSettings();
        dumpColorSlots();
        dumpPotMapping();
        emitReady(s_bankManager.getCurrentBank() + 1);
      }
```

Replace with :
```cpp
      } else if (strcmp(cmdBuf, "?ALL") == 0) {
        emitBanksHeader(NUM_BANKS);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
        // Phase 2 : [BANK_SETTINGS] pour chaque bank ARPEG_GEN.
        for (uint8_t i = 0; i < NUM_BANKS; i++) emitBankSettings(i);
        emitGlobals();
        emitSettings();
        resetDbgSentinels();
        dumpLedSettings();
        dumpColorSlots();
        dumpPotMapping();
        emitReady(s_bankManager.getCurrentBank() + 1);
      }
```

- [ ] **Step 5: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning.

- [ ] **Step 6: Commit**

```bash
git add src/viewer/ViewerSerial.cpp
git commit -m "viewer(phase2): emit [BANK_SETTINGS] in boot dump / auto-resync / queries

4 sites :
- taskBody() auto-resync (transition !wasConnected → nowConnected)
- pollCommands ?STATE (foreground bank only)
- pollCommands ?BOTH (all 8 banks, ARPEG_GEN only)
- pollCommands ?ALL (idem ?BOTH + LED/colors/potmap dumps)

emitBankSettings est no-op pour les bank types non-ARPEG_GEN, donc
itérer les 8 banks est safe (le viewer ne voit que les events utiles).

?BANKS pas modifié — [BANK_SETTINGS] vit dans la couche [STATE]/runtime,
pas dans le bank header layer."
```

---

## Task 12: HW gate G1 — Boot dump + auto-resync [BANK_SETTINGS]

**Files:** none (HW validation only).

- [ ] **Step 1: Pre-conditions**

Ensure at least 1 bank is configured as `BANK_ARPEG_GEN` via Tool 5 :
- Boot firmware → hold rear → setup mode → Tool 5
- Set at least bank 5 (or any) to TYPE = `ARPEG-Imm` or `ARPEG-Beat` (these are ARPEG_GEN variants per Tool 5 labeling).
- Note current BONUS / MARGIN / PROX / ECART values for that bank.
- Exit setup → reboot.

- [ ] **Step 2: Flash + connect viewer**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload
```

Open viewer (JUCE app) — should auto-connect to ILLPAD.

In the serial monitor (or viewer log panel), boot dump should now include `[BANK_SETTINGS]` lines :
```
[BANKS] count=8
[BANK] idx=1 ...
[BANK] idx=2 ...
...
[STATE] bank=1 ...
[STATE] bank=2 ...
...
[BANK_SETTINGS] bank=5 bonus=15 margin=7 prox=4 ecart=5    ← NOUVEAU (si bank 5 ARPEG_GEN)
[GLOBALS] Tempo=...
[SETTINGS] ClockMode=...
[READY] current=1
```

Expected: 1 to 4 `[BANK_SETTINGS]` lines (1 per ARPEG_GEN bank configured). Values match Tool 5 settings.

- [ ] **Step 3: Disconnect + reconnect viewer (auto-resync path)**

Quit viewer app, wait 2s, relaunch. Should auto-detect ILLPAD and trigger auto-resync.

Expected: same boot dump replayed (BANKS / BANK / STATE / BANK_SETTINGS / GLOBALS / SETTINGS / READY), including all `[BANK_SETTINGS]` events for ARPEG_GEN banks.

- [ ] **Step 4: ?BOTH manual resync**

In viewer, click "Resync" button (sends `?BOTH\n`).

Expected: full dump including `[BANK_SETTINGS]` events.

- [ ] **Step 5: Verify negative case (non-ARPEG_GEN bank in ?STATE)**

Switch foreground to a NORMAL or ARPEG bank (not ARPEG_GEN). Manually send `?STATE` (via terminal or viewer command).

Expected: `[STATE] bank=N mode=NORMAL ...` + `[READY]`, NO `[BANK_SETTINGS]` event (foreground is not ARPEG_GEN). `emitBankSettings` returned early.

- [ ] **Step 6: Mark G1 passed**

If all 4 sub-steps pass, Phase 2.B emission paths are validated. No commit needed (HW gate).

If any step fails, diagnose :
- No `[BANK_SETTINGS]` in boot dump → check Task 10 emitBankSettings impl (s_nvsManager / s_banks access), check Task 11 loop placement.
- Wrong values → check Task 4 _loadedBankType population (Task 11 reads from s_nvsManager.getLoadedX which depends on _loadedX[] populated by loadAll).
- Spurious events for non-ARPEG_GEN banks → check emitBankSettings early return.

---

## Task 13: ViewerSerial — Buffer 24 + overflow flag + too_long emit

**Files:**
- Modify: `src/viewer/ViewerSerial.cpp:282-326` (pollCommands : cmdBuf size + overflow handling)

- [ ] **Step 1: Modify cmdBuf size + add s_cmdOverflow flag**

In `ViewerSerial.cpp`, locate `pollCommands()` body around lines 281-283 :
```cpp
  static char    cmdBuf[16];
  static uint8_t cmdLen = 0;
```

Replace with :
```cpp
  static char    cmdBuf[24];
  static uint8_t cmdLen = 0;
  static bool    s_cmdOverflow = false;  // Phase 2 : flag set when cmdLen overflows
```

- [ ] **Step 2: Modify newline handler to emit [ERROR] too_long if overflow**

In `pollCommands()`, locate the newline handler around line 286-287 :
```cpp
    if (c == '\n' || c == '\r') {
      cmdBuf[cmdLen] = '\0';
      if (strcmp(cmdBuf, "?STATE") == 0) {
```

Replace with :
```cpp
    if (c == '\n' || c == '\r') {
      cmdBuf[cmdLen] = '\0';
      // Phase 2 : si overflow détecté pendant la réception, émettre too_long
      // avant le dispatch normal. Le cmd tronqué donne au viewer le contexte
      // pour son toast. Reset état + skip le dispatch (cmdBuf est tronqué,
      // pas exploitable).
      if (s_cmdOverflow) {
        emit(PRIO_HIGH, "[ERROR] cmd=%.20s... code=too_long\n", cmdBuf);
        cmdLen = 0;
        s_cmdOverflow = false;
        continue;
      }
      if (strcmp(cmdBuf, "?STATE") == 0) {
```

- [ ] **Step 3: Modify overflow path to set s_cmdOverflow instead of silent discard**

In `pollCommands()`, locate the overflow discard around lines 318-323 :
```cpp
    } else if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    } else {
      // overflow — discard buffer
      cmdLen = 0;
    }
```

Replace with :
```cpp
    } else if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    } else {
      // Phase 2 : overflow — set flag, keep cmdBuf comme tronqué (le \n
      // déclenchera l'emit [ERROR] too_long avec les 20 premiers chars).
      s_cmdOverflow = true;
    }
```

- [ ] **Step 4: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning.

- [ ] **Step 5: Auto-review**

Run:
```bash
grep -n "s_cmdOverflow\|cmdBuf\[24\]\|too_long" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/viewer/ViewerSerial.cpp
```
Expected: 5+ occurrences (flag decl, set in overflow, check at \n, emit too_long, reset).

- [ ] **Step 6: Commit**

```bash
git add src/viewer/ViewerSerial.cpp
git commit -m "viewer(phase2): cmdBuf 16→24 chars + overflow flag + emit too_long

Buffer agrandi à 24 chars pour accepter \"!MARGIN=12 BANK=8\\0\" (18 chars
+ headroom).

Path overflow : l'impl actuelle discard silencieusement → Phase 2 ajoute
un flag s_cmdOverflow set quand cmdLen atteint la capacité, et sur \\n,
émet [ERROR] cmd=<20 chars>... code=too_long puis reset. Sans ce path,
le code too_long de la spec §7.2 ne serait jamais émis."
```

---

## Task 14: ViewerSerial — Add dispatchWriteCommand skeleton + handler stubs

**Files:**
- Modify: `src/viewer/ViewerSerial.cpp:36-243` (anonymous namespace : add enum + functions)
- Modify: `src/viewer/ViewerSerial.cpp:314-316` (pollCommands : wire dispatch)

- [ ] **Step 1: Add `enum ArpGenArg` + forward declarations in anonymous namespace**

In `ViewerSerial.cpp`, locate the start of the anonymous namespace around line 36 :
```cpp
namespace {

// Event in queue : 1 byte prio + 1 byte len + 254 bytes payload = 256 bytes
struct QueuedEvent {
```

Insert AFTER `namespace {` and BEFORE `// Event in queue` :
```cpp
namespace {

// Phase 2 : enum tagging the 4 ARPEG_GEN per-bank arguments. Used by
// handleArpGenParam to dispatch range check + setter + setLoadedX without
// 4 copies of the same code.
enum ArpGenArg : uint8_t {
  ARG_BONUS  = 0,   // x10 [10..20]
  ARG_MARGIN = 1,   // [3..12]
  ARG_PROX   = 2,   // x10 [4..20]
  ARG_ECART  = 3,   // [1..12]
};

// Phase 2 forward declarations (definitions below).
static void dispatchWriteCommand(const char* cmd);
static void handleClockMode(const char* valStr, const char* origCmd);
static void handleArpGenParam(ArpGenArg arg, const char* valStr, int bank1, const char* origCmd);

// Event in queue : 1 byte prio + 1 byte len + 254 bytes payload = 256 bytes
struct QueuedEvent {
```

- [ ] **Step 2: Add `dispatchWriteCommand` definition at end of anonymous namespace**

In `ViewerSerial.cpp`, locate the end of the anonymous namespace around lines 240-243 :
```cpp
    case TARGET_EMPTY:
    case TARGET_NONE:
    default:
      snprintf(buf, bufSize, "---"); break;
  }
}

}  // namespace
```

Insert AFTER the closing `}` of `formatTargetValueForBank` and BEFORE the closing `}  // namespace` :
```cpp
    case TARGET_EMPTY:
    case TARGET_NONE:
    default:
      snprintf(buf, bufSize, "---"); break;
  }
}

// =================================================================
// Phase 2 — Write commands dispatcher
// =================================================================
// Format : !KEY=VALUE[ BANK=K]\n (cf spec §5).
// Buffer reçu garanti <= 23 chars + '\0' (Task 13 path too_long).
// Strategy : copy → strtok_r split on space → split on '=' → dispatch by key.
// Le tampon scratch (tmp[24]) est mutable par strtok_r. cmd reste intact
// pour les emits d'erreur.

static void dispatchWriteCommand(const char* cmd) {
  // 1. Copy to scratch buffer (strtok_r mutates).
  char tmp[24];
  size_t n = strlen(cmd);
  if (n >= sizeof(tmp)) {
    // Should not happen (Task 13 path too_long catches before we arrive here),
    // mais defense-in-depth pour ne pas overflow strtok_r.
    emit(PRIO_HIGH, "[ERROR] cmd=%.20s... code=too_long\n", cmd);
    return;
  }
  memcpy(tmp, cmd, n + 1);

  // 2. Tokenize on space : tok1 = "!KEY=VAL", tok2 = "BANK=K" or nullptr.
  char* save = nullptr;
  char* tok1 = strtok_r(tmp, " ", &save);
  char* tok2 = strtok_r(nullptr, " ", &save);

  // 3. Split tok1 on '=' : key (after '!'), valStr.
  if (!tok1) return;  // empty command, drop silently
  char* eq = strchr(tok1, '=');
  if (!eq) {
    emit(PRIO_HIGH, "[ERROR] cmd=%s code=parse_error\n", cmd);
    return;
  }
  *eq = '\0';
  const char* key = tok1 + 1;   // skip '!'
  const char* valStr = eq + 1;

  // 4. Parse optional BANK=K (per-bank commands).
  int bank1 = -1;   // -1 = absent
  if (tok2) {
    char* eq2 = strchr(tok2, '=');
    if (!eq2 || strncmp(tok2, "BANK", 4) != 0) {
      emit(PRIO_HIGH, "[ERROR] cmd=%s code=parse_error\n", cmd);
      return;
    }
    bank1 = atoi(eq2 + 1);
  }

  // 5. Dispatch by key.
  if      (strcmp(key, "CLOCKMODE") == 0) handleClockMode(valStr, cmd);
  else if (strcmp(key, "BONUS")     == 0) handleArpGenParam(ARG_BONUS,  valStr, bank1, cmd);
  else if (strcmp(key, "MARGIN")    == 0) handleArpGenParam(ARG_MARGIN, valStr, bank1, cmd);
  else if (strcmp(key, "PROX")      == 0) handleArpGenParam(ARG_PROX,   valStr, bank1, cmd);
  else if (strcmp(key, "ECART")     == 0) handleArpGenParam(ARG_ECART,  valStr, bank1, cmd);
  else {
    emit(PRIO_HIGH, "[ERROR] cmd=%s code=unknown_command\n", cmd);
  }
}

// =================================================================
// Phase 2 — Handler stubs (filled by Tasks 15 + 16)
// =================================================================
// Stubs émettent unknown_command pour qu'on puisse compiler + tester le
// dispatcher avant de remplir les bodies.

static void handleClockMode(const char* valStr, const char* origCmd) {
  (void)valStr;
  emit(PRIO_HIGH, "[ERROR] cmd=%s code=unknown_command\n", origCmd);
}

static void handleArpGenParam(ArpGenArg arg, const char* valStr, int bank1, const char* origCmd) {
  (void)arg; (void)valStr; (void)bank1;
  emit(PRIO_HIGH, "[ERROR] cmd=%s code=unknown_command\n", origCmd);
}

}  // namespace
```

- [ ] **Step 3: Wire dispatchWriteCommand into pollCommands**

In `ViewerSerial.cpp`, locate the `!` branch in pollCommands around line 314-315 :
```cpp
      } else if (cmdBuf[0] == '!') {
        emit(PRIO_HIGH, "[ERROR] write commands not yet implemented (Phase 2)\n");
      }
```

Replace with :
```cpp
      } else if (cmdBuf[0] == '!') {
        dispatchWriteCommand(cmdBuf);
      }
```

- [ ] **Step 4: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning. The stubs emit `unknown_command` for any valid `!*` command — that's intentional, Tasks 15-16 fill the bodies.

- [ ] **Step 5: Auto-review**

Run:
```bash
grep -n "dispatchWriteCommand\|handleClockMode\|handleArpGenParam" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/viewer/ViewerSerial.cpp
```
Expected: forward decls (3) + impls (3) + 1 call site = 7 occurrences. No external file references to these helpers (anonymous namespace = TU-local).

- [ ] **Step 6: Commit**

```bash
git add src/viewer/ViewerSerial.cpp
git commit -m "viewer(phase2): add dispatchWriteCommand skeleton + handler stubs

Wires the ! branch of pollCommands to a new dispatcher in the anonymous
namespace. Parses !KEY=VAL[ BANK=K] via strtok_r, dispatches by key to
handleClockMode / handleArpGenParam (currently stubs that emit
unknown_command — bodies filled Tasks 15-16).

enum ArpGenArg tags BONUS/MARGIN/PROX/ECART for handleArpGenParam (one
function handles all 4 ARPEG_GEN per-bank args via range/setter switches).

Parser cases :
- empty tok1 → silent drop
- no '=' in tok1 → parse_error
- tok2 doesn't start with BANK= → parse_error
- key not in CLOCKMODE/BONUS/MARGIN/PROX/ECART → unknown_command"
```

---

## Task 15: ViewerSerial — Implement handleClockMode

**Files:**
- Modify: `src/viewer/ViewerSerial.cpp` (replace handleClockMode stub body)

- [ ] **Step 1: Replace `handleClockMode` stub with full impl**

In `ViewerSerial.cpp`, locate the stub :
```cpp
static void handleClockMode(const char* valStr, const char* origCmd) {
  (void)valStr;
  emit(PRIO_HIGH, "[ERROR] cmd=%s code=unknown_command\n", origCmd);
}
```

Replace entirely with :
```cpp
static void handleClockMode(const char* valStr, const char* origCmd) {
  // 1. Parse + validate value (master|slave string).
  bool isMaster;
  if      (strcmp(valStr, "master") == 0) isMaster = true;
  else if (strcmp(valStr, "slave")  == 0) isMaster = false;
  else {
    emit(PRIO_HIGH, "[ERROR] cmd=%s code=invalid_value expected=master|slave\n", origCmd);
    return;
  }

  // 2. Apply runtime (ClockManager state — drains pending ticks if master).
  s_clockManager.setMasterMode(isMaster);

  // 3. Update s_settings BEFORE emitSettings() reads it (cf spec §6.1 ordre strict).
  s_settings.clockMode = isMaster ? CLOCK_MASTER : CLOCK_SLAVE;

  // 4. Queue NVS save (debounced 500ms via tickDebounce).
  s_nvsManager.queueSettingsWrite(s_settings);

  // 5. Emit confirmation events dans l'ordre §6.1 : [SETTINGS] puis [CLOCK] Source.
  // [CLOCK] Source: synchronise device.clockSource côté viewer — sinon le badge
  // resterait stale jusqu'au prochain tick externe / timeout / Resync manuel.
  emitSettings();
  emitClockSource(s_clockManager.getActiveSourceLabel(), 0.0f);
}
```

- [ ] **Step 2: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning.

`s_clockManager`, `s_settings`, `s_nvsManager` are external linkage (cf ViewerSerial.cpp:18-23). `CLOCK_MASTER` / `CLOCK_SLAVE` are defined in `HardwareConfig.h:265-268` (included transitively via `KeyboardData.h` ligne 4 of ViewerSerial.cpp). `setMasterMode` declared in `ClockManager.h:23`. `getActiveSourceLabel` declared in `ClockManager.h:30`.

- [ ] **Step 3: Commit**

```bash
git add src/viewer/ViewerSerial.cpp
git commit -m "viewer(phase2): implement handleClockMode

Parse master|slave → ClockManager.setMasterMode + s_settings.clockMode
update (BEFORE emitSettings reads it) + NVS queue + emit [SETTINGS] +
[CLOCK] Source: (synchro badge clock source viewer — fix R2 audit).

Erreur invalid_value si ni master ni slave."
```

---

## Task 16: ViewerSerial — Implement handleArpGenParam

**Files:**
- Modify: `src/viewer/ViewerSerial.cpp` (replace handleArpGenParam stub body)

- [ ] **Step 1: Replace `handleArpGenParam` stub with full impl**

In `ViewerSerial.cpp`, locate the stub :
```cpp
static void handleArpGenParam(ArpGenArg arg, const char* valStr, int bank1, const char* origCmd) {
  (void)arg; (void)valStr; (void)bank1;
  emit(PRIO_HIGH, "[ERROR] cmd=%s code=unknown_command\n", origCmd);
}
```

Replace entirely with :
```cpp
static void handleArpGenParam(ArpGenArg arg, const char* valStr, int bank1, const char* origCmd) {
  // 1. Parse value (integer).
  // atoi returns 0 on parse failure — we accept 0 as a value and let range
  // check below catch invalid (no range starts at 0 : BONUS≥10, MARGIN≥3,
  // PROX≥4, ECART≥1, so 0 always triggers out_of_range).
  int val = atoi(valStr);

  // 2. Range check per arg (cf spec §3).
  int rangeLo, rangeHi;
  switch (arg) {
    case ARG_BONUS:  rangeLo = 10; rangeHi = 20; break;
    case ARG_MARGIN: rangeLo = 3;  rangeHi = 12; break;
    case ARG_PROX:   rangeLo = 4;  rangeHi = 20; break;
    case ARG_ECART:  rangeLo = 1;  rangeHi = 12; break;
    default:
      // unreachable (enum closed)
      emit(PRIO_HIGH, "[ERROR] cmd=%s code=unknown_command\n", origCmd);
      return;
  }
  if (val < rangeLo || val > rangeHi) {
    emit(PRIO_HIGH, "[ERROR] cmd=%s code=out_of_range range=%d..%d\n",
         origCmd, rangeLo, rangeHi);
    return;
  }

  // 3. Validate BANK=K présence + range.
  if (bank1 < 0) {
    emit(PRIO_HIGH, "[ERROR] cmd=%s code=missing_bank\n", origCmd);
    return;
  }
  if (bank1 < 1 || bank1 > NUM_BANKS) {
    emit(PRIO_HIGH, "[ERROR] cmd=%s code=invalid_bank range=1..%d\n",
         origCmd, NUM_BANKS);
    return;
  }
  uint8_t bankIdx = (uint8_t)(bank1 - 1);

  // 4. Validate bank type == BANK_ARPEG_GEN.
  // Critical : une bank NORMAL/LOOP n'a pas d'arpEngine assigné (main.cpp:510
  // seul isArpType inclut ARPEG_GEN). Bank ARPEG (classic) a un engine mais
  // n'utilise pas _bonusPilex10 etc → palier rouge spec §17.
  static const char* TYPE_NAMES[] = { "NORMAL", "ARPEG", "LOOP", "ARPEG_GEN" };
  if (s_banks[bankIdx].type != BANK_ARPEG_GEN) {
    uint8_t t = (uint8_t)s_banks[bankIdx].type;
    const char* got = (t < 4) ? TYPE_NAMES[t] : "?";
    emit(PRIO_HIGH, "[ERROR] cmd=%s code=bank_type_mismatch expected=ARPEG_GEN got=%s\n",
         origCmd, got);
    return;
  }

  // 5. Defense-in-depth : arpEngine non-null (should be true for ARPEG_GEN).
  if (!s_banks[bankIdx].arpEngine) {
    emit(PRIO_HIGH, "[ERROR] cmd=%s code=bank_type_mismatch expected=ARPEG_GEN got=null_engine\n",
         origCmd);
    return;
  }

  // 6. Apply runtime setter + NVS cache update + NVS queue.
  // 4-way dispatch over ArpGenArg. setter writes _xxx privée dans ArpEngine
  // (dynamic read at next pickNextDegree step — pas de _sequenceGenDirty).
  // setLoadedX écrit le cache NvsManager pour saveBankType. queueBankTypeFromCache
  // arme le timer 500ms (le save effectif arrive via tickDebounce → commitAll).
  ArpEngine* eng = s_banks[bankIdx].arpEngine;
  switch (arg) {
    case ARG_BONUS:
      eng->setBonusPile((uint8_t)val);
      s_nvsManager.setLoadedBonusPile(bankIdx, (uint8_t)val);
      break;
    case ARG_MARGIN:
      eng->setMarginWalk((uint8_t)val);
      s_nvsManager.setLoadedMarginWalk(bankIdx, (uint8_t)val);
      break;
    case ARG_PROX:
      eng->setProximityFactor((uint8_t)val);
      s_nvsManager.setLoadedProximityFactor(bankIdx, (uint8_t)val);
      break;
    case ARG_ECART:
      eng->setEcart((uint8_t)val);
      s_nvsManager.setLoadedEcart(bankIdx, (uint8_t)val);
      break;
  }
  s_nvsManager.queueBankTypeFromCache();

  // 7. Emit confirmation [BANK_SETTINGS] (lit depuis _loadedX[] qu'on vient de mettre à jour).
  emitBankSettings(bankIdx);
}
```

- [ ] **Step 2: Compile**

Run:
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: exit 0, no new warning.

`ArpEngine` is forward-declared via include of `KeyboardData.h` (which declares `BankSlot.arpEngine` as `ArpEngine*`). Methods `setBonusPile`, `setMarginWalk`, `setProximityFactor`, `setEcart` declared in `ArpEngine.h:81-84`. `s_nvsManager.setLoadedX` methods declared in `NvsManager.h:52-58`.

Verify: `ArpEngine.h` is already included by `ViewerSerial.cpp:5` (`#include "../arp/ArpEngine.h"`).

- [ ] **Step 3: Auto-review**

Run:
```bash
grep -n "setBonusPile\|setMarginWalk\|setProximityFactor\|setEcart" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/viewer/ViewerSerial.cpp
```
Expected: 4 occurrences (one per ARG case).

Run:
```bash
grep -n "setLoadedBonusPile\|setLoadedMarginWalk\|setLoadedProximityFactor\|setLoadedEcart" /Users/loic/Code/PROJECTS/ILLPAD_V2/src/viewer/ViewerSerial.cpp
```
Expected: 4 occurrences (one per ARG case).

- [ ] **Step 4: Commit**

```bash
git add src/viewer/ViewerSerial.cpp
git commit -m "viewer(phase2): implement handleArpGenParam (4 ARPEG_GEN args)

Single function dispatches BONUS/MARGIN/PROX/ECART via ArpGenArg enum.
Ordre strict (cf spec §8) : parse value → range check → BANK= presence
+ range → type ARPEG_GEN → arpEngine != nullptr → apply setter + setLoadedX
+ queueBankTypeFromCache → emit [BANK_SETTINGS].

Erreurs émises selon §7.2 : out_of_range, missing_bank, invalid_bank,
bank_type_mismatch (avec expected=ARPEG_GEN got=NORMAL|ARPEG|LOOP|?).

ArpEngine setters sont dynamic-write (pas de _sequenceGenDirty), donc
le change prend effet au prochain pickNextDegree (maybeMutate ou
addPadPosition). Cf spec §19 Y1 (lock mutationLevel=1 = pas audible
jusqu'à pile change — by design, indicateur viewer)."
```

---

## Task 17: HW gate G2 — !CLOCKMODE round trip

**Files:** none (HW validation only).

- [ ] **Step 1: Flash + connect**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

Open viewer.

- [ ] **Step 2: Send `!CLOCKMODE=master`**

In the serial monitor (or via viewer terminal), type :
```
!CLOCKMODE=master
```
(press Enter — `\n` terminator).

Expected viewer log :
```
[SETTINGS] ClockMode=master PanicReconnect=... DoubleTapMs=... AftertouchRate=... BleInterval=... BatAdcFull=...
[CLOCK] Source: internal
```
Viewer header should now show ClockMode=master + clock source=internal.

- [ ] **Step 3: Send `!CLOCKMODE=slave`**

Type :
```
!CLOCKMODE=slave
```

Expected viewer log :
```
[SETTINGS] ClockMode=slave ...
[CLOCK] Source: internal
```
(Or `usb`/`ble` if an external source had been locked and is still active — depends on session state.)

- [ ] **Step 4: Verify NVS persistence after 500ms**

After sending the command, wait at least 1s (debounce 500ms + NVS task wake delay 50ms + commitAll). Serial monitor should show :
```
[NVS] Saved settings.
```

(This is the line emitted by `saveSettings()` in `NvsManager.cpp` Task 6.)

- [ ] **Step 5: Reboot and verify**

Power cycle or trigger reboot. After boot dump, verify `[SETTINGS]` line shows the last mode sent.

Expected: persistence OK. If you sent `!CLOCKMODE=master` last, boot dump shows `ClockMode=master`.

- [ ] **Step 6: Send invalid value**

Type :
```
!CLOCKMODE=foo
```

Expected viewer log :
```
[ERROR] cmd=!CLOCKMODE=foo code=invalid_value expected=master|slave
```
No `[SETTINGS]` emitted (rejected before).

- [ ] **Step 7: Mark G2 passed**

If all sub-steps pass, `!CLOCKMODE` round trip is validated.

Failures :
- No `[SETTINGS]` emitted → check Task 15 emit order (emitSettings before emitClockSource).
- No `[CLOCK] Source:` emitted → check `s_clockManager.getActiveSourceLabel()` returns valid string (cf ClockManager.cpp:269).
- NVS not saved → check Task 7 tickDebounce placement EN TÊTE (Bloquant B1).

---

## Task 18: HW gate G3 — !BONUS/MARGIN/PROX/ECART round trip

**Files:** none (HW validation only).

- [ ] **Step 1: Pre-condition**

At least 1 bank configured as `BANK_ARPEG_GEN` via Tool 5 (cf Task 12 pre-condition). Note its index K (1-based).

- [ ] **Step 2: Send `!BONUS=18 BANK=K`**

Replace K by your ARPEG_GEN bank (e.g. 5).

Expected viewer log :
```
[BANK_SETTINGS] bank=5 bonus=18 margin=7 prox=4 ecart=5
```
(values reflect current state, bonus updated to 18).

Press pads of that bank to play it. The walk should audibly become more pile-biased (bonus_pile = 18/10 = 1.8, higher weight on pile degrees during mutation).

- [ ] **Step 3: Send `!MARGIN=12 BANK=K`**

Expected viewer log :
```
[BANK_SETTINGS] bank=5 bonus=18 margin=12 prox=4 ecart=5
```
Audibly : the walk now drifts further above/below the pile range (margin = 12 max).

- [ ] **Step 4: Send `!PROX=20 BANK=K`**

Expected viewer log :
```
[BANK_SETTINGS] bank=5 bonus=18 margin=12 prox=20 ecart=5
```
Audibly : the walk becomes more erratic (prox_factor = 2.0, less proximity bias).

- [ ] **Step 5: Send `!ECART=12 BANK=K`**

Expected viewer log :
```
[BANK_SETTINGS] bank=5 bonus=18 margin=12 prox=20 ecart=12
```
Audibly : larger step jumps between consecutive notes.

- [ ] **Step 6: Wait 1s + verify NVS save**

Serial monitor should show :
```
[NVS] Saved bank type.
```
(emitted by `saveBankType()` after 500ms debounce + 50ms task delay).

- [ ] **Step 7: Reboot + verify persistence**

Power cycle. Boot dump should show `[BANK_SETTINGS] bank=5 bonus=18 margin=12 prox=20 ecart=12` for bank K.

- [ ] **Step 8: Verify non-ARPEG_GEN error**

Send to a NORMAL bank (e.g. bank 1 if it's NORMAL) :
```
!BONUS=15 BANK=1
```

Expected viewer log :
```
[ERROR] cmd=!BONUS=15 BANK=1 code=bank_type_mismatch expected=ARPEG_GEN got=NORMAL
```
No `[BANK_SETTINGS]` emitted. Runtime + NVS unchanged.

- [ ] **Step 9: Mark G3 passed**

Failures :
- Walk audibly identical → check ArpEngine reads `_bonusPilex10` / `_marginWalk` / etc dynamically (cf ArpEngine.cpp:212+). Likely impl is correct, may need to ensure `mutationLevel ≥ 2` (lock = no audible change, spec §19 Y1).
- `[BANK_SETTINGS]` not emitted post-write → check Task 16 step 7 (`emitBankSettings(bankIdx)`).
- Persistence fails → check Task 7 tickDebounce + Task 6 saveBankType.

---

## Task 19: HW gate G4 — Stream test anti-flood (NVS debounce)

**Files:** none (HW validation only).

- [ ] **Step 1: Pre-condition**

Bank K configured ARPEG_GEN. Note current BONUS value.

- [ ] **Step 2: Send 10 commands rapidly**

Within ~500ms, send (use a script or fast typing) :
```
!BONUS=10 BANK=K
!BONUS=11 BANK=K
!BONUS=12 BANK=K
!BONUS=13 BANK=K
!BONUS=14 BANK=K
!BONUS=15 BANK=K
!BONUS=16 BANK=K
!BONUS=17 BANK=K
!BONUS=18 BANK=K
!BONUS=19 BANK=K
```

- [ ] **Step 3: Verify viewer log shows 10 [BANK_SETTINGS]**

Expected: 10 `[BANK_SETTINGS]` events, one per write, all showing increasing bonus values. The viewer Model gets re-hydrated each time.

- [ ] **Step 4: Verify NVS saved ONCE after ~500ms idle**

Stop sending. Wait 1s. Serial monitor should show :
```
[NVS] Saved bank type.
```
**Exactly ONCE**, not 10× (debounce coalesces).

- [ ] **Step 5: Reboot + verify last value persisted**

Power cycle. Boot dump should show `bonus=19` (the last value sent).

- [ ] **Step 6: Mark G4 passed**

Failures :
- 10× `[NVS] Saved bank type.` → tickDebounce timer not resetting properly. Verify Task 5 `queueBankTypeFromCache()` updates `_bankTypeLastChangeMs = millis()` each call.
- 0 saves → tickDebounce not running (check `tickDebounce` rename Task 8 + main.cpp:1363 call site).

---

## Task 20: HW gate G5 — All errors

**Files:** none (HW validation only).

- [ ] **Step 1: Send each error case from spec §7.3**

Send each line, verify exact viewer response :

| Sent | Expected viewer event |
|---|---|
| `!BONUS=999 BANK=2` | `[ERROR] cmd=!BONUS=999 BANK=2 code=out_of_range range=10..20` |
| `!CLOCKMODE=foo` | `[ERROR] cmd=!CLOCKMODE=foo code=invalid_value expected=master\|slave` |
| `!BONUS=15` | `[ERROR] cmd=!BONUS=15 code=missing_bank` |
| `!BONUS=15 BANK=9` | `[ERROR] cmd=!BONUS=15 BANK=9 code=invalid_bank range=1..8` |
| `!BONUS=15 BANK=N` (N is a NORMAL bank) | `[ERROR] cmd=!BONUS=15 BANK=N code=bank_type_mismatch expected=ARPEG_GEN got=NORMAL` |
| `!XYZ=1` | `[ERROR] cmd=!XYZ=1 code=unknown_command` |
| `!CLOCKMODE` (no `=`) | `[ERROR] cmd=!CLOCKMODE code=parse_error` |

- [ ] **Step 2: Verify too_long path**

Send a long string (>23 chars) :
```
!MARGIN=12 BANK=8GARBAGE_LONG_STRING_TO_OVERFLOW
```

Expected viewer event :
```
[ERROR] cmd=!MARGIN=12 BANK=8GAR... code=too_long
```
(20 chars truncated + `...`).

- [ ] **Step 3: Verify runtime + NVS unchanged after each error**

After each error, send `?BOTH` or check runtime state. Expected: no parameter changed. No `[NVS] Saved ...` log line.

- [ ] **Step 4: Mark G5 passed**

Failures :
- Wrong error code → check Task 16 switch over ArgError type.
- Wrong format → check spec §7.3 exact format vs emit format string in handlers.
- too_long not emitted → check Task 13 s_cmdOverflow handling.

---

## Task 21: HW gate G6 — Safety pads pressed

**Files:** none (HW validation only).

- [ ] **Step 1: Pre-condition**

Bank K = ARPEG_GEN, no pads pressed yet.

- [ ] **Step 2: Hold a pad on bank K**

Press and HOLD a non-control pad. ArpEngine starts ticking (notes playing).

- [ ] **Step 3: Send !BONUS=20 BANK=K**

Send the command WHILE keeping the pad pressed.

Expected viewer log :
```
[BANK_SETTINGS] bank=K bonus=20 ...
```
Immediate re-emit.

Audibly : the next mutation (per `mutationLevel`) reflects the new bonus.

But serial monitor should NOT show `[NVS] Saved bank type.` yet — `_anyPadPressed` guard defers the save.

- [ ] **Step 4: Release all pads**

After pad release, wait ~500ms + 50ms task delay. Now :
```
[NVS] Saved bank type.
```
Should appear.

- [ ] **Step 5: Mark G6 passed**

Failures :
- `[NVS] Saved` while pads pressed → `_anyPadPressed` guard broken. Check NvsManager.cpp:405-408 still in place.
- `[NVS] Saved` never emits even after release → check Task 7 tickDebounce + Task 6 commitAll extension.

---

## Task 22: HW gate G7 — Non-régression Tool 5 / Tool 8

**Files:** none (HW validation only).

- [ ] **Step 1: Modify bank K params via viewer**

Send :
```
!BONUS=20 BANK=K
!MARGIN=10 BANK=K
!PROX=15 BANK=K
!ECART=8 BANK=K
```
Wait 1s for NVS save.

- [ ] **Step 2: Reboot + enter setup mode**

Power cycle. Hold rear at boot → setup mode.

- [ ] **Step 3: Tool 5 verification**

Navigate to Tool 5. Bank K row should display :
- BONUS = 2.0 (= 20/10)
- MARGIN = 10
- PROX = 1.5 (= 15/10)
- ECART = 8

Expected: Tool 5 reads from `_loadedX[]` which was persisted by Phase 2 saveBankType. The viewer-side write reflects in Tool 5 setup mode.

- [ ] **Step 4: Modify in Tool 5 + save**

In Tool 5, change BONUS to 1.0 (= 10) for bank K. Exit Tool 5 (save).

Exit setup → reboot.

- [ ] **Step 5: Verify viewer reflects Tool 5 change**

After boot dump, viewer should show :
```
[BANK_SETTINGS] bank=K bonus=10 margin=10 prox=15 ecart=8
```

(Bonus updated to 10, others kept from viewer-side write.)

- [ ] **Step 6: Tool 8 verification**

Pre : send `!CLOCKMODE=master` via viewer. Wait 1s NVS save. Reboot. Enter setup → Tool 8.

ClockMode should display `master`. Change to `slave` in Tool 8, save, exit setup, reboot.

Viewer boot dump should show :
```
[SETTINGS] ClockMode=slave ...
```

Expected: Tool 8 ↔ viewer settings.clockMode are bidirectionally consistent.

- [ ] **Step 7: Mark G7 passed**

Failures :
- Tool 5 shows stale values → `_loadedBonusPile/MarginWalk/etc` not synced post viewer-write. Check Task 16 setLoadedX calls.
- Tool 8 shows wrong value → `s_settings.clockMode` not propagated. Check Task 15.
- Round trip viewer→Tool→viewer breaks → check `_loadedBankType[i]` populated correctly in Task 4 loadAll. saveBankType should re-include ALL fields, not just the modified one.

---

## Task 23: STATUS.md update + final commit

**Files:**
- Modify: `STATUS.md` (focus courant section + add Phase 2 history table)

- [ ] **Step 1: Update STATUS.md focus courant**

Open `STATUS.md`. Locate the "Focus courant" line (currently mentioning Phase 2 viewer bidirectionnel as "Prochaine étape immédiate").

Replace with :
```markdown
**Focus courant** : **Viewer serial Phase 2 firmware CLOSE** (handler !CLOCKMODE + 4 ARPEG_GEN per-bank, NvsManager debounce 500 ms, [BANK_SETTINGS] event, [ERROR] cmd= retour d'erreur). Spec viewer-juce à implémenter (séparée). Phase 2 LOOP toujours en file d'attente.
```

- [ ] **Step 2: Add Phase 2 firmware history table**

After the "Viewer serial centralization Phase 1" section, add a new section :

```markdown
## Viewer bidirectionnel Phase 2 firmware — historique commits (2026-05-17)

Commits firmware sur `main`. Spec : [docs/superpowers/specs/2026-05-17-viewer-bidirectional-phase2-design.md](docs/superpowers/specs/2026-05-17-viewer-bidirectional-phase2-design.md). Plan : [docs/superpowers/plans/2026-05-17-viewer-bidirectional-phase2-firmware-plan.md](docs/superpowers/plans/2026-05-17-viewer-bidirectional-phase2-firmware-plan.md).

| Phase | Task | Commit | Description |
|---|---|---|---|
| 2.A | 2 | `<hash>` | NvsManager : private members + constructor init |
| 2.A | 3 | `<hash>` | NvsManager : getLoadedBankType / setLoadedBankType |
| 2.A | 4 | `<hash>` | NvsManager : populate _loadedBankType in loadAll |
| 2.A | 5 | `<hash>` | NvsManager : queueSettingsWrite / queueBankTypeFromCache |
| 2.A | 6 | `<hash>` | NvsManager : saveSettings + saveBankType + commitAll |
| 2.A | 7 | `<hash>` | NvsManager : tickPotDebounce extend Phase 2 EN TÊTE |
| 2.A | 8 | `<hash>` | NvsManager : rename tickPotDebounce → tickDebounce |
| 2.B | 10 | `<hash>` | ViewerSerial : emitBankSettings |
| 2.B | 11 | `<hash>` | ViewerSerial : emit [BANK_SETTINGS] in boot dump + queries |
| 2.B | 13 | `<hash>` | ViewerSerial : cmdBuf 24 + overflow + too_long |
| 2.B | 14 | `<hash>` | ViewerSerial : dispatchWriteCommand skeleton + stubs |
| 2.B | 15 | `<hash>` | ViewerSerial : handleClockMode impl |
| 2.B | 16 | `<hash>` | ViewerSerial : handleArpGenParam impl (4 ARPEG_GEN args) |

**HW Checkpoints validés** : G1 (boot dump + auto-resync) → G2 (!CLOCKMODE) → G3 (!BONUS/MARGIN/PROX/ECART) → G4 (stream test anti-flood) → G5 (errors) → G6 (safety pads pressed) → G7 (non-régression Tool 5 / Tool 8).

**Audit cross-source post-livraison** : à faire si nouveau drift firmware-viewer protocole.
```

- [ ] **Step 3: Replace `<hash>` placeholders with actual commit hashes**

Run:
```bash
git log --oneline -20
```

Copy the actual hashes for each Task commit and paste them into the table.

- [ ] **Step 4: Update Follow-ups ouverts section**

Locate the "Follow-ups ouverts" section. Remove the "Phase 2 viewer bidirectionnel" bullet (now closed). Add a new line :

```markdown
- **Spec viewer Phase 2** : implémentation côté `viewer-juce` (parser [BANK_SETTINGS] + [ERROR] cmd=, CommandSender, UI ClockMode toggle + 4 sliders ARPEG_GEN per-bank, error toast, lock indicator). Spec : [`../ILLPAD_V2-viewer/docs/2026-05-17-viewer-juce-phase2-bidirectional-spec.md`](../ILLPAD_V2-viewer/docs/2026-05-17-viewer-juce-phase2-bidirectional-spec.md). Firmware ready et HW-validated.
```

- [ ] **Step 5: Final commit**

```bash
git add STATUS.md
git commit -m "status: Phase 2 viewer bidirectionnel firmware CLOSE

Spec exécutée de bout en bout (13 commits firmware Tasks 2-16). HW
gates G1-G7 validés. Module ViewerSerial accepte !CLOCKMODE + 4 ARPEG_GEN
per-bank avec NVS debounce 500ms, [BANK_SETTINGS] event, [ERROR] cmd=
retour d'erreur.

Reste à faire : impl spec viewer-juce (parser + UI Phase 2)."
```

---

## Plan Self-Review

**1. Spec coverage** :
- §3 (5 commands) → Tasks 15+16
- §4 (mapping runtime/NVS) → Tasks 15+16
- §5 (syntaxe) → Task 14 (dispatcher)
- §6.1 (emit ordre !CLOCKMODE) → Task 15
- §6.2 (BANK_SETTINGS) → Task 10
- §6.4 (no re-emit on error) → Tasks 15+16 (early return)
- §7 (errors) → Task 14 (parse errors), Task 16 (semantic errors)
- §8 (dispatcher) → Tasks 13+14
- §9 (multi-thread) → N/A (no mutex needed, Core 1 mono-thread)
- §10.1 (fields) → Task 2
- §10.2 (API) → Tasks 3+5
- §10.3 (bodies queue) → Task 5
- §10.4 (_loadedBankType cache) → Tasks 2+3+4
- §10.5 (tickDebounce extension EN TÊTE) → Tasks 7+8
- §10.6 (commitAll + saveSettings/saveBankType) → Task 6
- §10.7 (_anyPadPressed) → inherited (no task needed, validated G6)
- §11 (boot dump + auto-resync) → Task 11
- §12 (emitBankSettings) → Task 10
- §13 (NvsManager.h API) → Tasks 3+5+6
- §14 (compile gates) → every code task
- §15 G1-G7 (HW gates) → Tasks 12, 17-22
- §19 acceptations → no task (documented decisions, no impl)

All spec sections covered.

**2. Placeholder scan** :
- No "TBD", "TODO", "implement later" anywhere.
- No "Similar to Task N" — code repeated when needed.
- No vague "add validation" — each validation is shown with code.
- One placeholder: `<hash>` in Task 23 table — explicitly marked to fill at execution time. Acceptable (depends on actual commit hashes from preceding tasks).

**3. Type consistency** :
- `ArpGenArg` enum used consistently in Tasks 14+16 (ARG_BONUS/MARGIN/PROX/ECART).
- `s_clockManager.setMasterMode(bool)` — signature matches ClockManager.h:23.
- `s_nvsManager.queueSettingsWrite(const SettingsStore&)` — matches Task 5 declaration.
- `s_nvsManager.setLoadedBonusPile(uint8_t, uint8_t)` — matches NvsManager.h:52.
- `emitBankSettings(uint8_t bankIdx)` — Task 10 decl + impl + Task 11 callers + Task 16 caller all use same signature.
- `tickDebounce` (renamed from `tickPotDebounce`) — same signature, single rename.

All types consistent.

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-05-17-viewer-bidirectional-phase2-firmware-plan.md`.**

Two execution options :

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task (or per logical group : Phase 2.A = Tasks 2-9, Phase 2.B emit = Tasks 10-12, Phase 2.B dispatcher = Tasks 13-16, HW gates = Tasks 17-22, final = Task 23). Two-stage review between tasks.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints (typically per Phase 2.A → HW Task 9 → Phase 2.B → HW Tasks 17-22).

Which approach ?
