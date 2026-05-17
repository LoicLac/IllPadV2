# ILLPAD48 V2 — Viewer bidirectionnel Phase 2 (write commands firmware)

**Date** : 2026-05-17
**Statut** : VALIDÉ pour plan d'implémentation. Brainstorm condensé avec
décisions arbitrées par l'utilisateur (Q1-Q7 + scope reduction).
**Scope Phase 2 firmware** : implémentation des write commands `!CMD=val`
acceptées par `viewer::pollCommands()`, retour de confirmation (events de
re-emit) et d'erreur (`[ERROR] cmd=...`), persistence NVS asynchrone debounce
500 ms, extension boot dump / auto-resync avec un nouvel event
`[BANK_SETTINGS]` per-bank ARPEG_GEN.
**Hors scope firmware** : tout ce qui touche le viewer-juce (parser
`[BANK_SETTINGS]`, sender TX path, UI controls Phase 2) — fait l'objet d'une
spec séparée côté worktree `viewer-juce` (voir §17).

**Sources** :
- Phase 1 viewer serial centralization : spec
  [`2026-05-17-viewer-serial-centralization-design.md`](2026-05-17-viewer-serial-centralization-design.md)
  §19-21 (hooks Phase 2 prévus).
- Handoff session [`HANDOFF-phase2-viewer-bidirectional.md`](../HANDOFF-phase2-viewer-bidirectional.md).
- Brainstorm session 2026-05-17 (cette session) : décisions Q1-Q7
  consolidées ci-dessous.
- État `main` au 2026-05-17 (commits jusqu'à `91408ca`).

---

## Partie 1 — Contexte

### §1 — Phase 1 acquis et hooks en place

Phase 1 livrée 2026-05-17 (11 commits firmware + 1 viewer) : module
`src/viewer/ViewerSerial.{cpp,h}` centralise tous les events runtime vers le
viewer JUCE. `viewer::pollCommands()` reconnaît déjà les query commands
`?STATE/?BANKS/?BOTH/?ALL` et intercepte tout `!*` avec un stub :

```cpp
} else if (cmdBuf[0] == '!') {
  emit(PRIO_HIGH, "[ERROR] write commands not yet implemented (Phase 2)\n");
}
```
([`ViewerSerial.cpp:314`](../../../src/viewer/ViewerSerial.cpp))

Phase 2 remplace ce stub par un vrai dispatcher, et étend le boot dump /
auto-resync avec les nouveaux events `[BANK_SETTINGS]`.

### §2 — Décisions tranchées en brainstorm

7 choix structurants validés par l'utilisateur :

| # | Décision | Choix retenu |
|---|---|---|
| Q1 | Syntaxe per-bank | **`!BONUS=N BANK=K`** (key=value, tokens séparés par espace) |
| Q2 | Confirmation event ARPEG_GEN | **Nouvel event dédié `[BANK_SETTINGS]`** (pas d'extension de `[STATE]`) |
| Q3 | Error format | **`[ERROR] cmd=... code=... [range=...]`** (key=value, parsable + lisible) |
| Q4 | NVS auto-save | **Auto-save toujours** après write success (cohérent Tool 5/Tool 8) |
| Q5 | Scope commands | **5 commands** : `!CLOCKMODE` + 4 ARPEG_GEN per-bank — pas `!PANIC_RECONNECT`, `!AFTERTOUCH_RATE`, `!DOUBLE_TAP_MS` |
| Q6 | Cross-worktree | Spec viewer rédigée après cette spec, viewer-juce développé sur sa branche |
| Q7 | NVS architecture | **`NvsManager` étendu** : `queueSettingsWrite()` + `queueBankTypeFromCache()` + debounce 500 ms + flush via NVS background task (pas de `saveBlob` synchrone dans `pollCommands`) |

**Bonus écarté** : `!SAVE_NOW` (force flush NVS immédiat) — non implémenté
Phase 2, le debounce 500 ms suffit en usage normal.

---

## Partie 2 — Inventaire des write commands

### §3 — 5 commands palier vert

| Command | Range valeur | BANK= requis ? | Bank type requis |
|---|---|---|---|
| `!CLOCKMODE=master\|slave` | string literal | non | n/a |
| `!BONUS=N BANK=K` | `N ∈ [10..20]` (x10 → bonus_pile ∈ [1.0..2.0]) | oui (`K ∈ [1..8]`) | **BANK_ARPEG_GEN** |
| `!MARGIN=N BANK=K` | `N ∈ [3..12]` | oui | **BANK_ARPEG_GEN** |
| `!PROX=N BANK=K` | `N ∈ [4..20]` (x10 → proximity ∈ [0.4..2.0]) | oui | **BANK_ARPEG_GEN** |
| `!ECART=N BANK=K` | `N ∈ [1..12]` | oui | **BANK_ARPEG_GEN** |

**Convention `BANK=K` 1-based** : cohérent avec `[BANK] idx=N`, `[STATE]
bank=N`. Internement, `K-1` indexe `s_banks[]`.

**Ranges sourcées** :
- `BONUS x10 ∈ [10..20]` : [`NvsManager.cpp:1012`](../../../src/managers/NvsManager.cpp)
  (`if (... && x10 >= 10 && x10 <= 20)`).
- `MARGIN ∈ [3..12]` : [`NvsManager.cpp:1021`](../../../src/managers/NvsManager.cpp).
- `PROX x10 ∈ [4..20]` : [`NvsManager.cpp:1030`](../../../src/managers/NvsManager.cpp).
- `ECART ∈ [1..12]` : [`NvsManager.cpp:1039`](../../../src/managers/NvsManager.cpp).
- `clockMode ∈ {0,1}` : [`KeyboardData.h:674`](../../../src/core/KeyboardData.h).

### §4 — Mapping runtime + NVS pour chaque command

| Command | Runtime setter | NVS update (cache + persist) | Events émis post-write |
|---|---|---|---|
| `!CLOCKMODE` | `s_clockManager.setMasterMode(value == "master")` | `s_settings.clockMode = CLOCK_MASTER/SLAVE` + `nvs.queueSettingsWrite(s_settings)` | `[SETTINGS] ...` + **`[CLOCK] Source: <label>`** (cf §6.1) |
| `!BONUS=N BANK=K` | `s_banks[K-1].arpEngine->setBonusPile(N)` | `nvs.setLoadedBonusPile(K-1, N)` + `nvs.queueBankTypeFromCache()` | `[BANK_SETTINGS] bank=K ...` |
| `!MARGIN=N BANK=K` | `s_banks[K-1].arpEngine->setMarginWalk(N)` | `nvs.setLoadedMarginWalk(K-1, N)` + `nvs.queueBankTypeFromCache()` | `[BANK_SETTINGS] bank=K ...` |
| `!PROX=N BANK=K` | `s_banks[K-1].arpEngine->setProximityFactor(N)` | `nvs.setLoadedProximityFactor(K-1, N)` + `nvs.queueBankTypeFromCache()` | `[BANK_SETTINGS] bank=K ...` |
| `!ECART=N BANK=K` | `s_banks[K-1].arpEngine->setEcart(N)` | `nvs.setLoadedEcart(K-1, N)` + `nvs.queueBankTypeFromCache()` | `[BANK_SETTINGS] bank=K ...` |

**Note `!CLOCKMODE` side-effects** : `setMasterMode(false)` (slave) ne
re-établit pas de lock externe automatique — il bascule le source selection
dans `ClockManager` ; si aucune source externe n'est présente, le BPM cible
sera celui du pot Tempo interne. La transition est immédiate, pas de panic
note off. Cohérent avec Tool 8 setup.

**Note `!CLOCKMODE` cohérence visuelle viewer** : `setMasterMode(true)` force
`_activeSource = SRC_INTERNAL` ([ClockManager.cpp:258](../../../src/midi/ClockManager.cpp))
mais **n'émet pas** lui-même de `[CLOCK] Source:`. Le viewer header
(`device.clockSource`) reflèterait l'ancienne source jusqu'au prochain
tick externe / timeout / Resync manuel. **Fix Phase 2** : le handler
`!CLOCKMODE` émet explicitement `viewer::emitClockSource(s_clockManager.getActiveSourceLabel(), 0.0f)`
après `setMasterMode`, pour synchroniser le badge clock source côté
viewer dans la même réponse. Cf §6.1 pour l'ordre exact d'émission.

**Note `setLoadedX()` → `queueBankTypeFromCache()`** : les setters de
`NvsManager` modifient l'array interne `_loadedX[NUM_BANKS]`. La méthode
nouvelle `queueBankTypeFromCache()` (Phase 2) consulte ces arrays + les
`types[]/quantize[]/scaleGroup[]` actuels, reconstruit un `BankTypeStore`
complet, le marque dirty, et le NVS task le persiste via `saveBlob`. Cf §10.

---

## Partie 3 — Protocole

### §5 — Syntaxe write commands

Format général :
```
!KEY=VALUE[ BANK=K]\n
```

Règles parser :
- Préfixe `!` indique write command (vs `?` pour query).
- Tokens séparés par **un seul espace** (`' '`). Pas de tabulation, pas de
  multiples espaces tolérés (parse_error sinon).
- **Tout-majuscule** : `!BONUS`, pas `!bonus`. Cohérent avec les events
  `[BANK_SETTINGS]` qui utilisent `bank=N bonus=X` en minuscule pour les
  payloads (key=value reading side).
- Valeur numérique : entier décimal positif. Pas de `0x`, pas de signe.
- Pour `!CLOCKMODE` uniquement, valeur string : `master` ou `slave` (en
  minuscule).
- Terminé par `\n` ou `\r`. Buffer d'entrée capacité **24 chars** (`!MARGIN=12
  BANK=8` = 17 chars + headroom) — cf §8.

Commands valides :
```
!CLOCKMODE=master
!CLOCKMODE=slave
!BONUS=18 BANK=2
!MARGIN=7 BANK=2
!PROX=12 BANK=2
!ECART=5 BANK=2
```

### §6 — Confirmation events

#### §6.1 — `!CLOCKMODE` succès → re-emit `[SETTINGS]` + `[CLOCK] Source:`

**Deux events émis dans cet ordre strict**, séparément (PRIO_HIGH les deux) :

1. **`[SETTINGS]`** — format existant (Phase 1.D, [`ViewerSerial.cpp:675-689`](../../../src/viewer/ViewerSerial.cpp:675)) :
```
[SETTINGS] ClockMode=master PanicReconnect=1 DoubleTapMs=150 AftertouchRate=20 BleInterval=2 BatAdcFull=3000\n
```
Pas de nouveau format. Le viewer parse déjà cette ligne via `applySettings` qui
met à jour `device.settings.clockMode`.

2. **`[CLOCK] Source:`** — format existant Phase 1 ([`ViewerSerial.cpp:629-639`](../../../src/viewer/ViewerSerial.cpp:629)) :
```
[CLOCK] Source: internal\n
```
Émis pour synchroniser `device.clockSource` côté viewer (badge clock source
du header). Sinon le viewer afficherait l'ancienne source jusqu'au prochain
tick externe / timeout (cas slave→master avec source USB/BLE active : badge
reste « usb »/« ble » alors que firmware est en master internal).

**Ordre d'exécution dans le handler** :
```cpp
// 1. Validate value (parse master|slave string)
// 2. Apply runtime
s_clockManager.setMasterMode(isMaster);
// 3. Update s_settings (BEFORE emitSettings reads it)
s_settings.clockMode = isMaster ? CLOCK_MASTER : CLOCK_SLAVE;
// 4. Queue NVS save (debounced 500ms)
s_nvsManager.queueSettingsWrite(s_settings);
// 5. Emit confirmations (order : SETTINGS puis CLOCK Source)
viewer::emitSettings();
viewer::emitClockSource(s_clockManager.getActiveSourceLabel(), 0.0f);
```

`getActiveSourceLabel()` ([ClockManager.cpp:269-277](../../../src/midi/ClockManager.cpp:269))
retourne `"internal"` immédiatement après `setMasterMode(true)` (force
`_activeSource = SRC_INTERNAL`). En cas `setMasterMode(false)`, retourne la
source courante (probablement `"internal"` aussi si pas de tick externe
encore reçu, ou `"usb"`/`"ble"` si une source était précédemment locked et
n'a pas timeout).

#### §6.2 — `!BONUS/MARGIN/PROX/ECART BANK=K` succès → emit `[BANK_SETTINGS]`

**Nouveau event Phase 2** :
```
[BANK_SETTINGS] bank=2 bonus=18 margin=7 prox=12 ecart=5\n
```

Format :
- `bank=K` (1-based, cohérent avec `[BANK]/[STATE]`)
- `bonus=N` : bonus_pile x10 (10..20)
- `margin=N` : marginWalk degrés (3..12)
- `prox=N` : proximityFactor x10 (4..20)
- `ecart=N` : ecart absolu (1..12)
- Toutes les 4 fields toujours présentes (pas de subset).
- Longueur typique ~48 B (incl. `\n`).

**Émis uniquement pour les banks de type `BANK_ARPEG_GEN`** :
- Au boot dump : itère les 8 banks, n'émet `[BANK_SETTINGS]` que pour celles
  configurées en `BANK_ARPEG_GEN` (0 à 4 events selon Tool 5 user config).
- Après un write `!BONUS/MARGIN/PROX/ECART BANK=K` réussi : re-emit du
  `[BANK_SETTINGS]` de la bank `K`. (Pas de re-emit en cas de
  `bank_type_mismatch` — l'erreur est protectrice, la bank n'est pas
  ARPEG_GEN.)
- Sur `?BOTH`/`?ALL` : idem boot dump (émet pour chaque ARPEG_GEN).
- Sur `?STATE` (foreground bank) : émet `[BANK_SETTINGS]` si le foreground
  est ARPEG_GEN, sinon pas d'event (cohérent avec « `?STATE` = foreground
  uniquement »).

Le firmware n'émet jamais `[BANK_SETTINGS]` pour une bank non-ARPEG_GEN — le
viewer peut donc parser sans condition sur le type, le payload est toujours
valide quand il arrive.

#### §6.3 — Ordre des emits post-write

Une command réussie déclenche, dans cet ordre, sur la même file `xQueueSend`
(PRIO_HIGH) :
1. **Re-emit du confirmation event** (`[SETTINGS]` ou `[BANK_SETTINGS]`)
2. C'est tout — pas de `[READY]` post-write (le viewer n'a pas besoin de
   re-synchroniser, il a juste un ack ciblé).

Pas de `[ACK]` event séparé : le re-emit est lui-même l'ack. Si le viewer
reçoit le `[SETTINGS]` ou `[BANK_SETTINGS]` après un envoi `!*`, il sait que
le write a réussi et que les values reflètent l'état firmware.

#### §6.4 — Erreur → emit `[ERROR]` uniquement

En cas d'erreur, **aucun** re-emit confirmation, **uniquement** un event
`[ERROR]` (cf §7). Le runtime firmware n'est pas modifié, NVS pas dirty.

### §7 — Format des erreurs

#### §7.1 — Format général

```
[ERROR] cmd=<original_cmd> code=<error_code> [<extra_keys>]\n
```

- `cmd=<original_cmd>` : la ligne reçue, sans `\n` (length-limited à **20
  chars** max — si overflow, tronqué + suffixe `...`). Si la ligne contient
  un espace, **garder le format brut** (pas de quotes, pas d'escape) — c'est
  acceptable car le viewer parse ligne complète.
- `code=<error_code>` : un des codes ci-dessous, lowercase snake_case.
- `<extra_keys>` : optionnels, key=value séparés par espace. Liste fixe par
  code (cf table §7.2).

#### §7.2 — Codes d'erreur

| Code | Quand émis | Extra keys |
|---|---|---|
| `unknown_command` | `!XXX=...` non reconnu | — |
| `parse_error` | format invalid (pas de `=`, tokens corrompus, valeur non numérique pour N) | — |
| `too_long` | ligne >= 24 chars (buffer overflow protection) | — |
| `out_of_range` | valeur numérique parsée mais hors plage | `range=N..M` |
| `invalid_value` | `!CLOCKMODE=foo` (ni master ni slave) | `expected=master\|slave` |
| `missing_bank` | per-bank command sans `BANK=K` | — |
| `invalid_bank` | `BANK=K` invalide (`K=0` ou `K>8`) | `range=1..8` |
| `bank_type_mismatch` | bank valide mais type ≠ BANK_ARPEG_GEN | `expected=ARPEG_GEN got=<actual_type>` |

#### §7.3 — Exemples

```
[ERROR] cmd=!BONUS=999 BANK=2 code=out_of_range range=10..20
[ERROR] cmd=!CLOCKMODE=foo code=invalid_value expected=master|slave
[ERROR] cmd=!BONUS=15 code=missing_bank
[ERROR] cmd=!BONUS=15 BANK=9 code=invalid_bank range=1..8
[ERROR] cmd=!BONUS=15 BANK=2 code=bank_type_mismatch expected=ARPEG_GEN got=NORMAL
[ERROR] cmd=!XYZ=1 code=unknown_command
[ERROR] cmd=!CLOCKMODE code=parse_error
[ERROR] cmd=!MARGIN=12 BANK=8GAR... code=too_long
```

**Priority** : PRIO_HIGH (jamais droppable — l'erreur doit toujours
remonter).

---

## Partie 4 — Architecture firmware

### §8 — Dispatcher `viewer::pollCommands`

Modification minimale du `pollCommands()` existant ([`ViewerSerial.cpp:275`](../../../src/viewer/ViewerSerial.cpp)) :

1. **Buffer `cmdBuf` agrandi** : `16` → `24` chars (16 ne suffit pas pour
   `!MARGIN=12 BANK=8\0` = 18 chars).
2. **Path overflow reception** : l'impl actuelle [ViewerSerial.cpp:318-322](../../../src/viewer/ViewerSerial.cpp:318)
   discard silencieusement (`cmdLen = 0`) en cas d'overflow. Phase 2 : ajouter
   un flag statique `s_cmdOverflow` set à `true` quand `cmdLen` atteint la
   capacité ; sur réception `\n`, si `s_cmdOverflow == true`, émettre
   `[ERROR] cmd=<premiers 20 chars>... code=too_long` puis reset
   (`cmdLen = 0; s_cmdOverflow = false;`). Le `cmd=` tronqué donne au viewer
   un contexte minimal pour le toast. **Sans ce path, `code=too_long` ne
   serait jamais émis** — l'utilisateur enverrait une ligne corrompue et le
   firmware répondrait silencieusement.
3. **Branche `cmdBuf[0] == '!'`** : remplacer le stub `emit("[ERROR] write
   commands not yet implemented")` par un appel à `dispatchWriteCommand(cmdBuf)`.

Squelette `dispatchWriteCommand` :

```cpp
static void dispatchWriteCommand(const char* cmd) {
  // 1. Copy to scratch buffer (strtok_r mutates)
  char tmp[24];
  size_t n = strlen(cmd);
  if (n >= sizeof(tmp)) {
    emit(PRIO_HIGH, "[ERROR] cmd=%.20s... code=too_long\n", cmd);
    return;
  }
  memcpy(tmp, cmd, n + 1);

  // 2. Tokenize on space
  char* save = nullptr;
  char* tok1 = strtok_r(tmp, " ", &save);   // "!BONUS=18"
  char* tok2 = strtok_r(nullptr, " ", &save); // "BANK=2" or nullptr

  // 3. Split tok1 on '='
  if (!tok1) return;
  char* eq = strchr(tok1, '=');
  if (!eq) {
    emit(PRIO_HIGH, "[ERROR] cmd=%s code=parse_error\n", cmd);
    return;
  }
  *eq = '\0';
  const char* key = tok1 + 1;   // skip '!'
  const char* valStr = eq + 1;

  // 4. Parse optional BANK=K
  int bank1 = -1;
  if (tok2) {
    char* eq2 = strchr(tok2, '=');
    if (!eq2 || strncmp(tok2, "BANK", 4) != 0) {
      emit(PRIO_HIGH, "[ERROR] cmd=%s code=parse_error\n", cmd);
      return;
    }
    bank1 = atoi(eq2 + 1);
  }

  // 5. Dispatch by key
  if      (strcmp(key, "CLOCKMODE") == 0) handleClockMode(valStr, cmd);
  else if (strcmp(key, "BONUS")     == 0) handleArpGenParam(ARG_BONUS,  valStr, bank1, cmd);
  else if (strcmp(key, "MARGIN")    == 0) handleArpGenParam(ARG_MARGIN, valStr, bank1, cmd);
  else if (strcmp(key, "PROX")      == 0) handleArpGenParam(ARG_PROX,   valStr, bank1, cmd);
  else if (strcmp(key, "ECART")     == 0) handleArpGenParam(ARG_ECART,  valStr, bank1, cmd);
  else {
    emit(PRIO_HIGH, "[ERROR] cmd=%s code=unknown_command\n", cmd);
  }
}
```

**Handlers individuels** : un par command, ordre **strict** des checks pour
éviter null deref et garantir l'atomicité (échec n'affecte ni runtime ni NVS) :

1. **Parse value** + range check → `[ERROR] code=out_of_range range=N..M` ou
   `code=invalid_value expected=...` (cas `!CLOCKMODE`).
2. **Per-bank commands uniquement** (`!BONUS/MARGIN/PROX/ECART`) :
   - **a)** Présence `BANK=K` → sinon `code=missing_bank`.
   - **b)** Range `K ∈ [1..8]` → sinon `code=invalid_bank range=1..8`.
   - **c)** Vérif type : `s_banks[K-1].type == BANK_ARPEG_GEN` → sinon
     `code=bank_type_mismatch expected=ARPEG_GEN got=<actual>`. **Critique** :
     une bank NORMAL/LOOP n'a pas d'`arpEngine` assigné ([main.cpp:507-510](../../../src/main.cpp:507) :
     seul `isArpType(...)` reçoit un pointeur).
   - **d)** Guard défensif `s_banks[K-1].arpEngine != nullptr` → sinon
     `code=bank_type_mismatch` (couvre l'éventualité d'un bank ARPEG_GEN sans
     engine assigné — ne devrait pas arriver vu §isArpType inclut ARPEG_GEN,
     mais defense-in-depth).
3. **Apply setter runtime** (`setMasterMode` / `setBonusPile` / etc.).
4. **Pour `!CLOCKMODE` uniquement** : modifier `s_settings.clockMode` AVANT
   d'appeler `emitSettings()` (cf §6.1 ordre strict).
5. **Queue NVS save** : `nvs.queueSettingsWrite(s_settings)` ou
   `nvs.setLoadedX(K-1, N) + nvs.queueBankTypeFromCache()`.
6. **Émettre confirmation events** dans l'ordre §6.1/§6.2 :
   - `!CLOCKMODE` → `emitSettings()` puis `emitClockSource(...)`.
   - `!BONUS/MARGIN/PROX/ECART` → `emitBankSettings(K-1)`.
7. **En cas d'erreur à n'importe quelle étape** : émettre `[ERROR]` et
   **return immédiatement** sans modifier le runtime ni le NVS.

### §9 — Sécurité multi-thread

`pollCommands()` est appelé depuis le main `loop()` Core 1
([`main.cpp:1292`](../../../src/main.cpp)). Les setters appelés (`setMasterMode`,
`setBonusPile`, etc.) sont déjà appelés depuis Core 1 dans le code existant
(setup mode, runtime adjustments via pots) — donc pas de nouveau risque de
race. Les writes touchent uniquement Core 1 state.

**Pas de mutex à ajouter** : les setters internes sont déjà safe pour appel
Core 1 mono-threaded.

### §10 — `NvsManager` extension (queue + debounce settings/bankType)

#### §10.1 — Nouveaux dirty flags et pending data

Ajouts dans `NvsManager.h` (section private) :
```cpp
// Phase 2 : Settings + BankType queue (dirty + pending + debounce)
std::atomic<bool> _settingsDirty;
SettingsStore     _pendingSettings;
uint32_t          _settingsLastChangeMs;
bool              _settingsPendingSave;

std::atomic<bool> _bankTypeDirty;
// Pas de _pendingBankType : on resérialise depuis _loadedX[] arrays au moment
// du save (single source of truth, évite désync).
uint32_t          _bankTypeLastChangeMs;
bool              _bankTypePendingSave;

// Phase 2 : cache BankType (parallèle aux _loadedQuantize[], _loadedScaleGroup[]).
// Peuplé par loadAll() depuis bts.types[], modifié par setLoadedBankType()
// (Tool 5 setup OU futur runtime). Lu par saveBankType() pour reconstruire
// le BankTypeStore complet.
uint8_t _loadedBankType[NUM_BANKS];
```

**Init constructor obligatoire** ([NvsManager.cpp:12-32](../../../src/managers/NvsManager.cpp:12)) :
ajouter dans la list initializer :
```cpp
, _settingsDirty(false)
, _bankTypeDirty(false)
, _settingsLastChangeMs(0)
, _bankTypeLastChangeMs(0)
, _settingsPendingSave(false)
, _bankTypePendingSave(false)
```
Et dans le body constructor (après la boucle `for (uint8_t i = 0; i < NUM_BANKS; i++)` ligne 37-42 qui init `_loadedBonusPile/MarginWalk/Proximity/Ecart`) :
```cpp
for (uint8_t i = 0; i < NUM_BANKS; i++) {
  _loadedBankType[i] = BANK_NORMAL;   // défaut, surchargé par loadAll()
}
```

**Sans ces inits**, les fields non-init sont UB en C++ (notamment
`_settingsPendingSave` pourrait être `true` au boot → trigger save spurious
au premier tick de loop).

#### §10.2 — Nouvelles méthodes publiques

```cpp
// Phase 2 : viewer-driven settings write (debounced 500ms)
void queueSettingsWrite(const SettingsStore& settings);

// Phase 2 : viewer-driven BankTypeStore write (debounced 500ms).
// Reads from internal _loadedX[] arrays — caller must have called
// setLoadedBonusPile()/setLoadedMarginWalk()/etc. before this.
// types[]/quantize[]/scaleGroup[] are sourced from internal arrays too
// (setLoadedQuantizeMode + setLoadedScaleGroup already public).
// _loadedBankType[NUM_BANKS] : NEW internal array to cache BankType[]
// (currently only s_banks[i].type holds it ; cf §10.4).
void queueBankTypeFromCache();
```

#### §10.3 — Body des nouvelles méthodes

```cpp
void NvsManager::queueSettingsWrite(const SettingsStore& settings) {
  _pendingSettings = settings;
  _settingsLastChangeMs = millis();
  _settingsPendingSave = true;
  // _settingsDirty NOT set here — tickPotDebounce will set it after 500ms
  // (so commitAll() doesn't fire prematurely).
}

void NvsManager::queueBankTypeFromCache() {
  _bankTypeLastChangeMs = millis();
  _bankTypePendingSave = true;
}
```

#### §10.4 — Mini-extension : cache `_loadedBankType[]`

Aujourd'hui, `BankType` est stocké uniquement dans `s_banks[i].type`. Pour
que `queueBankTypeFromCache()` puisse reconstruire le `BankTypeStore` sans
toucher à `s_banks`, on ajoute un cache interne `_loadedBankType[NUM_BANKS]`
dans `NvsManager`, peuplé au boot par `loadAll()` (déjà fait via les
`_loadedX[]` arrays existants — il manque juste l'array `BankType`).

**Alternative envisagée** : laisser `saveBankType()` lire `s_banks[].type`
directement via un getter. Mais ça crée une dépendance circulaire
NvsManager → main.cpp globals, et NvsManager n'a pas accès à `s_banks` par
design (les autres queue methods passent les valeurs en paramètre).

**Décision** : ajouter `_loadedBankType[NUM_BANKS]` + getter/setter, peuplé
en `loadAll()` à partir du `bts.types[]` chargé. Cohérent avec le pattern
`_loadedQuantize[]`, `_loadedScaleGroup[]`, etc.

#### §10.5 — `tick()` debounce (extension de `tickPotDebounce`)

`tickPotDebounce` ([`NvsManager.h:92`](../../../src/managers/NvsManager.h)) est
déjà appelé depuis le `loop()`. On étend son body pour aussi gérer settings
et bankType.

**Placement critique — anti-bug bloquant** : la fonction actuelle a un
**early return** ligne 258-260 :
```cpp
if (!_potRightPendingSave || (now - _potRightLastChangeMs < POT_NVS_SAVE_DEBOUNCE_MS)) {
  return;   // ← bloque tout le code qui suit tant que right-pot pas pending+timeout
}
_potRightPendingSave = false;
// ... global pot params, per-bank velocity/pitch/arp ...
```

Si les checks Phase 2 sont ajoutés **après** cet early return, ils ne
s'exécutent QUE 10s après un mouvement de pot droit. Conséquence : settings
et bankType ne seraient **jamais persistés** en usage normal (l'utilisateur
ne touche pas forcément un pot droit après un toggle viewer). **NVS save
silencieusement cassé**.

**Solution** : placer les checks Phase 2 **EN TÊTE** de `tickPotDebounce`,
avant tout le code existant (le rear-pot debounce a déjà ce style en
séquentiel, pas d'early return). Code complet :

```cpp
void NvsManager::tickPotDebounce(uint32_t now, bool rearDirty, bool rightDirty,
                                  const PotRouter& potRouter,
                                  uint8_t currentBank, BankType currentType) {
  // -------------------------------------------------------------------
  // Phase 2 : Settings + BankType debounce — EN TÊTE pour ne pas être
  // bloqué par l'early return du right-pot ci-dessous.
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
  // Rear pot debounce (existant, inchangé)
  // -------------------------------------------------------------------
  // ... code existant ligne 217-247 ...

  // -------------------------------------------------------------------
  // Right pots debounce (existant, inchangé, avec son early return)
  // -------------------------------------------------------------------
  // ... code existant ligne 254-319 ...
}
```

**Renommage inclus dans Phase 2** : `tickPotDebounce` → `tickDebounce` (le
nom n'est plus représentatif maintenant qu'il gère pot + settings + bank
type). Renommage in-place dans `NvsManager.h/.cpp` + update call site dans
`main.cpp` (1 site grep-confirmé). Signature inchangée.

#### §10.6 — `commitAll()` extension

Ajouts dans `commitAll()` ([`NvsManager.cpp:403`](../../../src/managers/NvsManager.cpp))
après les saves existants :

```cpp
if (_settingsDirty) { saveSettings(); _settingsDirty = false; }
if (_bankTypeDirty) { saveBankType(); _bankTypeDirty = false; }
```

Nouvelles méthodes privées :
```cpp
void NvsManager::saveSettings() {
  // Direct saveBlob (NVS task background = pas de blocage loop)
  NvsManager::saveBlob(SETTINGS_NVS_NAMESPACE, SETTINGS_NVS_KEY,
                       &_pendingSettings, sizeof(_pendingSettings));
}

void NvsManager::saveBankType() {
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
}
```

**Note importante** : `commitAll()` est appelé sur le NVS background task
Core 1, pas sur le main loop. Le `saveBlob` bloquant ne pénalise donc pas le
loop principal — il s'exécute en parallèle.

#### §10.7 — Safety : `_anyPadPressed` guard

`commitAll()` skip déjà toutes les writes si `_anyPadPressed` est true (cf
[`NvsManager.cpp:405-408`](../../../src/managers/NvsManager.cpp)). Les nouveaux
saves Phase 2 héritent automatiquement de cette guard — pas de modification
nécessaire. Si l'utilisateur joue pendant qu'il modifie un slider viewer,
les writes sont retardées jusqu'au release.

### §11 — Extension boot dump + auto-resync

Modification dans `viewer::taskBody()` ([`ViewerSerial.cpp:79-153`](../../../src/viewer/ViewerSerial.cpp))
auto-resync (`if (!wasConnected && nowConnected)`) :

```cpp
emitBanksHeader(NUM_BANKS);
for (uint8_t i = 0; i < NUM_BANKS; i++) emitBank(i);
for (uint8_t i = 0; i < NUM_BANKS; i++) emitState(i);
// Phase 2 : [BANK_SETTINGS] pour chaque bank ARPEG_GEN
for (uint8_t i = 0; i < NUM_BANKS; i++) {
  if (s_banks[i].type == BANK_ARPEG_GEN) emitBankSettings(i);
}
emitGlobals();
emitSettings();
resetDbgSentinels();
emitReady(s_bankManager.getCurrentBank() + 1);
```

Modification dans `pollCommands()` pour `?BOTH` et `?ALL` : ajouter la même
boucle (`if BANK_ARPEG_GEN → emitBankSettings`) après les `emitState()`.

Pour `?STATE` (foreground bank uniquement) : appeler `emitBankSettings(s_bankManager.getCurrentBank())`
après le `emitState(...)`. Le no-op interne si type != ARPEG_GEN garantit
qu'aucun event n'est émis pour les autres bank types. Coût ~48 B quand
applicable, cohérent avec « `?STATE` = état complet du foreground ».

Pour `?BANKS` : pas de modification (l'event `[BANK_SETTINGS]` n'est pas un
sous-event de `[BANK]`, il vit dans la couche `[STATE]`/runtime).

---

## Partie 5 — API publique additions

### §12 — `ViewerSerial.h` API additions

Nouvelle fonction publique :
```cpp
namespace viewer {
// --- Phase 2 : [BANK_SETTINGS] event ---
// Émet [BANK_SETTINGS] bank=N bonus=X margin=Y prox=Z ecart=W
// Lit depuis _nvs.getLoadedBonusPile(idx) etc. NE PAS appeler pour les
// banks dont type != BANK_ARPEG_GEN (no-op silencieux : retourne tôt).
void emitBankSettings(uint8_t bankIdx);
}
```

Implementation (`ViewerSerial.cpp`) :
```cpp
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

### §13 — `NvsManager.h` API additions

```cpp
// Phase 2 : viewer-driven write commands (debounced 500ms)
void queueSettingsWrite(const SettingsStore& settings);
void queueBankTypeFromCache();

// Phase 2 : BankType cache (parallel to _loadedQuantize[], etc.)
uint8_t getLoadedBankType(uint8_t bank) const;
void    setLoadedBankType(uint8_t bank, uint8_t type);
```

---

## Partie 6 — Tests et validation

### §14 — Compile gates

Chaque sous-task Phase 2 doit compiler avec :
```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Exit 0, zéro nouveau warning.

### §15 — HW runtime gates

Smoke tests par groupe :

**G1 — Boot dump + auto-resync (`[BANK_SETTINGS]` émis)** :
- Configurer au moins 1 bank en BANK_ARPEG_GEN via Tool 5, reboot.
- Connecter viewer après boot → vérifier que `[BANK_SETTINGS] bank=N bonus=X
  margin=Y prox=Z ecart=W` arrive dans le boot dump pour chaque bank
  ARPEG_GEN.
- Disconnect viewer + reconnect → idem (auto-resync path).

**G2 — `!CLOCKMODE` round trip** :
- Envoyer `!CLOCKMODE=master\n` → viewer reçoit `[SETTINGS] ClockMode=master ...`.
- Envoyer `!CLOCKMODE=slave\n` → idem inversé.
- Reboot → `[SETTINGS]` boot dump reflète le dernier mode envoyé (NVS
  persisté après ~500 ms debounce).

**G3 — `!BONUS/MARGIN/PROX/ECART` round trip per bank ARPEG_GEN** :
- Pour chaque bank ARPEG_GEN configurée, envoyer une command valide → viewer
  reçoit `[BANK_SETTINGS]` avec la new value.
- Vérifier audiblement que le bank en jeu reflète le change (pile mutation
  density changes for BONUS, séquence width for MARGIN, etc.).
- Reboot → persistence OK (next boot dump reflète last value).

**G4 — Stream test (anti-flood)** :
- Envoyer `!BONUS=N BANK=2` avec N variant rapidement (10, 11, 12, ..., 20)
  sans pause (simule un slider viewer 10+ events/sec).
- Vérifier que :
  - Runtime suit en temps réel (audio change immediate).
  - `[BANK_SETTINGS]` est émis pour chaque write.
  - **NVS save n'a lieu qu'après 500 ms** d'inactivité (vérif via DEBUG :
    "[NVS] BankType saved" doit apparaître 1×, pas 11×).

**G5 — Errors** : envoyer chaque cas listé §7.3 → vérifier que le `[ERROR]`
correspondant arrive, et que le runtime + NVS sont inchangés.

**G6 — Safety pads pressed** : appuyer sur des pads + envoyer `!BONUS=20
BANK=2`. Le `[BANK_SETTINGS]` doit arriver immédiatement (re-emit synchrone),
mais le NVS save doit être différé jusqu'à release de tous les pads
(`_anyPadPressed` guard). Verifier en debug log.

**G7 — Non-régression Tool 5 / Tool 8** :
- Entrer setup mode après modif viewer → vérifier que Tool 5 affiche les
  values modifiées par le viewer (working struct sourced from `_loadedX[]`).
- Modifier dans Tool 5 + save → reboot → viewer voit les Tool-5-edited values.

---

## Partie 7 — Cross-worktree et hors scope

### §16 — Workflow viewer-juce (spec parallèle)

Le viewer-juce n'a actuellement **aucun TX path** (`grep -r Serial.*write
Source/` = 0). Phase 2 viewer-side demande :
- Parser branches `[BANK_SETTINGS]` + `[ERROR] cmd=...`.
- Module sender (`OutgoingQueue` ou `SerialWriter`) pour pousser des `!CMD`
  depuis l'UI.
- UI controls : toggle ClockMode (header / TopStripBar), 4 sliders ARPEG_GEN
  per-bank (BankRow ARPEG_GEN ou panel Settings).
- Tests Catch2 pour le parser des nouveaux events.

Cette spec viewer sera rédigée dans `../ILLPAD_V2-viewer/ILLPADViewer/docs/2026-05-17-phase2-bidirectional-viewer-design.md`
**après** validation utilisateur de la spec firmware. Workflow Phase 1
(pré-codage parallèle) maintenu : firmware et viewer landent leurs changes
en parallèle, HW gate finale teste l'end-to-end.

### §17 — Hors scope Phase 2

- **Palier rouge** (déjà documenté dans handoff §): pot mapping, color
  slots, LED settings, padOrder, bankPads, scale pads, hold pad, octave
  pads, BankType changes runtime. Setup-only par construction (cf CLAUDE.md
  projet "Setup mode boot-only").
- **`!PANIC_RECONNECT`, `!AFTERTOUCH_RATE`, `!DOUBLE_TAP_MS`** : initialement
  listés palier vert dans le handoff, **retirés du scope Phase 2** par
  arbitrage user (utilité live discutable, ajout trivial si réintroduit
  ultérieurement — le pattern dispatcher est extensible).
- **`!SAVE_NOW`** : force flush NVS immédiat, écarté (cf §2).
- **Query `?BANK_SETTINGS=N`** : pas de query dédiée pour lire un bank
  settings spécifique en runtime. Le viewer utilise `?STATE` /
  `?BOTH` / boot dump auto-resync. Si besoin futur, trivial à ajouter.
- **Event de progress** (`[NVS] saving...` / `[NVS] saved bank_type`) : pas
  d'event d'état NVS Phase 2. Le viewer ne montre pas l'état du debounce
  buffer côté firmware. Optionnel future.

### §18 — Hors scope total

- Migration vers format binaire pour write commands (BLE / Protobuf) : le
  format ASCII line-based reste le contrat.
- Logging persistant côté firmware des write commands reçues (NVS limit +
  scope creep).
- Authentification / handshake viewer ↔ firmware : single-host USB CDC, pas
  de menace cross-network.

### §19 — Acceptations findings audit (cross-audit 2026-05-17)

Findings audit identifiés et **acceptés par décision utilisateur** sans fix
code/spec. Documentés ici pour la trace de décision et l'éventuelle revue
future.

| Finding | Sévérité audit | Décision | Justification |
|---|---|---|---|
| **R1** data race `_masterMode` non-atomique entre Core 1 (`setMasterMode` Phase 2) et NimBLE task (`onMidiClockTick` BLE callback) | Runtime | **Accepté par construction** | Conditionnel à `bleInterval != BLE_OFF` ET device BLE master connecté envoyant des ticks 0xF8 ET toggle au moment exact d'un tick. Worst case observable : 1-2 ticks parasites mitigés par `_prevTickUs = 0` reset ([ClockManager.cpp:259](../../../src/midi/ClockManager.cpp:259)). Sur ESP32-S3, `bool` aligné = byte-atomic au niveau hardware (UB C++ strict mais pas de symptôme runtime). Si Loïc force BLE_OFF dans Tool 8, le finding disparaît complètement. |
| **R3** master mode runtime n'envoie pas MIDI START/STOP (0xFA/0xFC), seulement ticks 0xF8 | Runtime / Question ouverte | **By design** | Comportement firmware existant et documenté ([MidiTransport.h:30](../../../src/core/MidiTransport.h:30) « ticks only, never sends Start/Stop »). Phase 2 ne change rien. Les slaves externes qui requirent un START explicite ne tickent pas — choix assumé. |
| **R4** transition slave→master en plein live = saut instantané de tempo externe vers `_internalBPM` (pot Tempo) | Runtime | **Attendu côté musicien** | Sémantique master = pot Tempo prend la main. Pas de fade transition. Le musicien comprend cette transition (cohérent avec Tool 8 setup mode classique). |
| **Y1** sliders ARPEG_GEN inaudibles en `mutationLevel=1` (lock) jusqu'à la prochaine pile change | YAGNI / Runtime | **By design + indicateur viewer** | Comportement attendu (lock = séquence figée). Le viewer affichera un **indicateur visuel « lock »** au niveau du bank ARPEG_GEN pour signaler le contexte au musicien — pas de gray-out des sliders ni de tooltip warning. Sliders restent fully fonctionnels (valeurs persistées immédiatement, audibles dès sortie de lock OU prochain `addPadPosition` qui déclenche les 3 mutations forcées [ArpEngine.cpp:410](../../../src/arp/ArpEngine.cpp:410)). À documenter côté spec viewer §3.8. |
| **Y2** race théorique saveBlob lit `_pendingSettings` byte-by-byte pendant que Core 1 peut `queueSettingsWrite` (rewrite) | YAGNI | **Accepté — pattern existant** | Même pattern que `_pendingBank`, `_pendingArpPot`, `_pendingScale[]` existants pré-Phase 2. Fenêtre de race ~5-20ms entre `xTaskNotifyGive` et fin `prefs.putBytes`. En pratique, l'utilisateur ne pousse pas 2 toggles ClockMode en <20ms. Si fixé pour Phase 2 seul, divergence du pattern — cohérence projet > optimisation isolée. |
| **Y3** `SerialReader::run` ne retry pas si `sp_nonblocking_write` partial | YAGNI | **Hors scope Phase 2** | Code viewer existant Phase 1 ([SerialReader.cpp:148-149](../../../../ILLPAD_V2-viewer/ILLPADViewer/Source/serial/SerialReader.cpp:148)). Commands Phase 2 ≤18 bytes, USB CDC TX buffer ≥64 bytes → write partial improbable. À noter pour robustesse future si on observe des drops. |

**R2** (badge clock source stale après `!CLOCKMODE`) a été **fixé** dans la
spec aux §4 / §6.1 — émission explicite `viewer::emitClockSource(...)` après
`setMasterMode` + `emitSettings`. Voir commit `1d65f9d`.

Findings audit **également fixés dans la spec** (commit dédié) :
- **B1** placement checks Phase 2 dans `tickPotDebounce` (§10.5) — anti-bug
  bloquant NVS persistence.
- **I1** longueur truncation `cmd=` alignée à 20 chars (§7.1, §7.3, §8).
- **I2** §8 squelette dispatcher avec ordre explicit type+null check.
- **M1** init constructor des nouveaux fields NvsManager (§10.1).
- **M2** path overflow reception dans pollCommands (§8 point 2).

---

## Annexe — Glossaire

- **Write command** : ligne entrante `!KEY=VAL [BANK=K]\n` du viewer vers le
  firmware (vs `?XXX` query, vs events de sortie `[XXX]`).
- **Palier vert** : commands sans side-effect majeur (no live audio break,
  no setup re-init, no MIDI panic).
- **Debounce 500 ms** : délai d'inactivité avant flush NVS, par dirty
  resource (Settings et BankType ont des timers indépendants). Évite wear
  flash sur stream slider viewer.
- **Re-emit confirmation** : après un write success, re-émettre l'event
  d'état correspondant (`[SETTINGS]` ou `[BANK_SETTINGS]`) comme ack
  implicite. Pas d'event `[ACK]` séparé.
- **`BankTypeStore` v4** : layout NVS partagé par Tool 5 (setup) et Phase 2
  (runtime). Magic `EEPROM_MAGIC`, version `BANKTYPE_VERSION` (4 au
  2026-05-17).
- **Auto-resync** : path firmware émettant un boot dump complet quand le
  viewer reconnecte après firmware déjà boot (`s_viewerConnected` false →
  true transition). Cf Phase 1.D commit `681c2a2`.
