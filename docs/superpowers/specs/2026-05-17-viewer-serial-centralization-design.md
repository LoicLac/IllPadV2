# ILLPAD48 V2 — Centralisation du canal serial viewer

**Date** : 2026-05-17
**Statut** : VALIDÉ pour plan d'implémentation. Brainstorm itéré 8 rounds avec
sortie convergente — pas de décision restante côté design.
**Scope Phase 1** : centralisation des emit serial → viewer dans un module dédié,
non-blocking transport, dormance auto, résolution des findings audit 2026-05-17,
tagging `[BOOT *]` pour les boot debug, préparation des hooks bidirectionnels.
**Hors scope** : implémentation des commands write Phase 2 (master/slave,
ARPEG_GEN params) — inventaire et code Phase 2 dans un design séparé.

**Sources** :
- Audit cross-source 2026-05-17 (read-only firmware vs spec parser viewer) —
  contenu reproduit §1 ci-dessous (non commité comme doc séparé).
- [`ILLPAD_V2-viewer/ILLPADViewer/docs/firmware-viewer-protocol.md`](../../../../ILLPAD_V2-viewer/ILLPADViewer/docs/firmware-viewer-protocol.md) —
  spec parser viewer, source de vérité pour les formats reconnus.
- État du code `main` au 2026-05-17 (commits jusqu'à `1dbbcb8`).
- Conversation de brainstorm 2026-05-17 (cette session).

**Note de cross-référence** : la spec parser viewer §3.1, §3.2, §3.3, §3.4
seront partiellement déprécié par ce design — §3.2 est déjà DONE (fix livré
par commit `6b86fcb` sur `main`). Les autres seront marqués IMPLEMENTED en
suite de Phase 1.

---

## Partie 1 — Contexte

### §1 — Findings audit 2026-05-17 (résumé)

Audit cross-source `spec-code` entre la spec parser viewer et tous les sites
d'émission serial du firmware runtime (boot + loop). Résultats catégorisés :

- **3 runtime** :
  - **R1** — Blocking USB CDC jusqu'à 100 ms par `Serial.print*` (HWCDC default
    `tx_timeout_ms = 100` quand USB plugged, jamais surchargé). Risque MIDI
    timing sous load viewer (host slow / app crash).
  - **R2** — Pas de source unique de vérité pour les params globaux : le viewer
    s'appuie sur le first-emit guard `s_dbg*` (commit `6b86fcb`) + le slot-scan
    de `[STATE]`. Brittle. Spec §3.1 (`[GLOBALS]`) reste valide.
  - **R3** — `[CLOCK]` ne ré-émet jamais le BPM externe ; le viewer reflète
    indéfiniment la valeur du pot Tempo interne en mode external sync.
- **6 incohérences** :
  - **I1** — Bank switch émet 2 lignes (`[BANK] Bank N (...)` + `[STATE] bank=N`)
    là où 1 suffirait.
  - **I2** — `[ARP] -note` émis uniquement par double-tap ou release LEFT, jamais
    par release pad normal (pile sacrée). Pas documenté côté spec.
  - **I3** — `[ARP] Stop — fingers down` reconnu par le parser mais 0 occurrence
    dans le firmware (branche supprimée 2026-05-15). Dead branch viewer.
  - **I4** — Banner `=== ILLPAD48 V2 ===` UNGATED ([main.cpp:506-507](../../../src/main.cpp:506)).
  - **I5** — `[NVS] ArpPotStore raw/v0` UNGATED ([NvsManager.cpp:751](../../../src/managers/NvsManager.cpp:751)).
  - **I6** — `logFullBaselineTable()` UNGATED + orphan (0 caller dans `src/`).
- **1 stale** : spec parser viewer §3.2 déjà FIXÉE (`s_firstEmit` uniform via
  commit `6b86fcb`).
- **4 manquants** :
  - **M1** — Aucun event `[GLOBALS]` (spec §3.1).
  - **M2** — Aucune ré-émission `ClockSource` au boot dump / `?BOTH`.
  - **M3** — `?BOTH` ne reset pas les sentinels `s_dbg*` → les params globaux
    ne sont pas re-émis sur Resync (spec §3.4).
  - **M4** — Pas d'event d'erreur runtime générique (NVS write fail, etc.).

Vérifié OK :
- DEBUG_SERIAL gating bulk correct dans 10/11 fichiers émetteurs.
- USB MIDI vs USB CDC : endpoints TinyUSB distincts, pas de blocage croisé
  transport.
- Core 0 sensingTask : 0 `Serial.print*`.
- `pollRuntimeCommands()` non-bloquant ([main.cpp:461-495](../../../src/main.cpp:461)).
- Format `[STATE]` conforme spec §1.3.

### §2 — Décisions tranchées en brainstorm

8 choix structurants validés par l'utilisateur :

| # | Décision | Choix retenu |
|---|---|---|
| 1 | Centralisation | Module dédié `ViewerSerial` |
| 2 | Architecture transport | **B-full** : background task + queue FreeRTOS (vs B-lite direct non-blocking) |
| 3 | Bidirectionnel | **Prépare Phase 2** (hook parser extensible), reste passive Phase 1 |
| 4 | Dormance | **3c** : atomic flag + auto-resync à la transition false→true |
| 5 | Post-connect re-sync | **X1** silent auto-dump (= `?BOTH` interne implicite) |
| 6 | Priority levels | **P1** 2-level (HIGH / LOW) avec backpressure à 70% |
| 7 | Coalescing ARP +/-note | **50 ms** (synthétise `[ARP] Bank N: pile=Y`) |
| 8 | Coalescing POT live | **Non** (first-emit guard actuel suffit) |
| 9 | Globals split | **G3** : `[GLOBALS]` (runtime) + `[SETTINGS]` (NVS persist) |
| 10 | Tagging boot | **`[BOOT *]`** uniforme (informational) |
| 11 | Input command owner | **Module owne `pollCommands()`** (canal complet) |
| 12 | Fatal boot fail | **Event `[FATAL]`** always-on, parser-recognized |

---

## Partie 2 — Architecture

### §3 — Module `ViewerSerial` : positionnement

Fichiers nouveaux :
- `src/viewer/ViewerSerial.h` — API publique typée (emit_*, poll_commands, begin).
- `src/viewer/ViewerSerial.cpp` — implémentation + task FreeRTOS + queue.

Dépendances entrantes (qui inclut `ViewerSerial.h`) :
- `src/main.cpp` (boot dump, runtime poll, debugOutput, pot pipeline, panic).
- `src/managers/BankManager.cpp` (bank switch emit).
- `src/managers/ScaleManager.cpp` (scale/octave emit).
- `src/arp/ArpEngine.cpp` (+/-note, Play/Stop, gen seed, queue full).
- `src/midi/ClockManager.cpp` (source change, BPM change).
- `src/core/MidiTransport.cpp` (USB/BLE connect/disconnect).

Dépendances sortantes (que le module appelle pour les handlers Phase 2 hooks) :
- `s_potRouter` (lecture params globaux pour `[GLOBALS]`).
- `s_clockManager` (lecture clockSource label pour `[GLOBALS]`).
- `s_nvsManager` (lecture settings pour `[SETTINGS]`).
- `s_bankManager` (lecture bank state pour `[STATE]`).
- Phase 2 only : setters de ces mêmes managers.

### §4 — Channels distincts sur le même Serial

Trois flux logiques, **même Serial peripheral** (USB CDC) :

| Channel | Prefixes | Owner | Threading | Gating |
|---|---|---|---|---|
| **Viewer protocol** | `[BANKS]`, `[BANK]`, `[STATE]`, `[READY]`, `[POT]`, `[SCALE]`, `[ARP]`, `[ARP_GEN]`, `[GEN]`, `[CLOCK]`, `[MIDI]`, `[PANIC]`, `[GLOBALS]`, `[SETTINGS]` | `ViewerSerial` task | Core 1 task, drain queue | `#if DEBUG_SERIAL` au sein du module |
| **Boot debug** | `[BOOT]`, `[BOOT KB]`, `[BOOT NVS]`, `[BOOT POT]`, `[BOOT MIDI]` | Raw Serial.print dans `setup()` | Core 1 main thread (avant que la task soit créée) | `#if DEBUG_SERIAL` au site |
| **Hardware debug** | `[HW]` | Raw Serial.print dans `debugOutput()` | Core 1 main thread | `#if DEBUG_HARDWARE` (≠ DEBUG_SERIAL) |
| **Fatal** | `[FATAL]` | Raw Serial.print + flush direct | Core 1 main thread | Always-on (UNGATED, intentionnel) |

Les 4 channels partagent le `tx_lock` semaphore HWCDC. Chaque `Serial.write(buf, n)`
est atomique au niveau ligne. Pas d'interleaving inter-channel.

### §5 — Dormance et auto-resync (3c + X1)

État représenté par 1 atomic :
```
std::atomic<bool> s_viewerConnected{false};
```

Mise à jour dans la task de drain :
```
loop:
  bool now = (bool)Serial;   // tud_cdc_connected() && tud_ready()
  bool was = s_viewerConnected.exchange(now, memory_order_acq_rel);
  if (!was && now) {
    // false → true transition : viewer vient de connecter
    autoResync();             // équivalent ?BOTH interne (cf. §6 drain)
  }
  if (!now) {
    drainSilent();            // vide la queue sans écrire
    vTaskDelay(100ms);
    continue;
  }
  drainOne(timeout=10ms);
```

Hot-path lecture :
```
inline bool isViewerConnected() {
  return s_viewerConnected.load(memory_order_acquire);
}
```
Coût ≈ 1-2 cycles. Tous les `emit_xxx()` checkent cet atomic avant de push
dans la queue → quand viewer absent, zéro push, zéro CPU.

Auto-resync (X1) émet, sans `[VIEWER_READY]` event explicite :
```
[BANKS] count=8
8× [BANK] idx=...
8× [STATE] bank=...
[GLOBALS] ...
[SETTINGS] ...
[READY] current=N
```
+ déclencher la réinitialisation des sentinels `s_dbg*` pour forcer la
ré-émission de tous les `[POT]` au prochain tick de loop.

### §6 — Queue, priority, backpressure

Queue = FreeRTOS StreamBuffer (ring de bytes, multi-writer thread-safe), taille
**8 KB**.

Format de message stocké : ligne complète déjà formatée + 1 byte de prefix
priority. Le prefix permet à la task de scanner la queue pour drop sélectif
sous backpressure (le StreamBuffer ne supporte pas le re-ordering, mais on peut
drop l'event en cours de pop si LOW + backpressure).

Logique de push (caller's thread) :
```
emit_xxx(prio, fmt, ...):
  if (!isViewerConnected()) return;
  char buf[256];
  int n = vsnprintf(buf, sizeof(buf), fmt, ...);
  if (n <= 0) return;
  size_t freeSpace = xStreamBufferSpacesAvailable(queue);
  if (prio == LOW && freeSpace < queueSize * 0.3) return;  // backpressure
  if (freeSpace < n + 2) return;                            // queue plein hard
  xStreamBufferSend(queue, &prio, 1, 0);
  xStreamBufferSend(queue, buf, n, 0);
```

Logique de drain (task) :
```
loop:
  uint8_t prio;
  xStreamBufferReceive(queue, &prio, 1, portMAX_DELAY);
  char line[256];
  int n = xStreamBufferReceive(queue, line, sizeof(line), 10ms);
  Serial.write(line, n);
```

Priority levels :
- **HIGH** (jamais droppable sauf queue 100%) :
  `[PANIC]`, `[READY]`, `[BANK]`, `[STATE]`, `[SCALE]`, `[CLOCK]`, `[MIDI]`,
  `[ARP] Play/Stop`, `[GLOBALS]`, `[SETTINGS]`, post-connect auto-dump.
- **LOW** (droppable backpressure ≥ 70%) :
  `[POT]` live values, `[ARP_GEN] MutationLevel`, `[GEN] seed`, `[ARP] pile=`
  (synthétisé par coalesce, cf. §7).

### §7 — Coalescing ARP +/-note (50 ms)

Les events `[ARP] Bank N: +note (M total)` et `-note (M total)` ne sont **pas
poussés directement** dans la queue à chaque appel. Ils alimentent un buffer
per-bank :

```
struct ArpPileSnapshot {
  uint8_t count;
  bool    dirty;
  bool    lastDeltaWasAdd;   // pour choisir +note vs -note à l'émission
  uint32_t lastEmitMs;
};
ArpPileSnapshot s_arpPile[NUM_BANKS];
```

Chaque `emit_arp_note_add(bank, count)` :
```
s_arpPile[bank].count = count;
s_arpPile[bank].lastDeltaWasAdd = true;
s_arpPile[bank].dirty = true;
```
Pareil pour `emit_arp_note_remove(bank, count)` avec `lastDeltaWasAdd = false`.

La task de drain check, à chaque itération, les snapshots dirty avec
`millis() - lastEmitMs >= 50`. Pour chacun, elle émet :
```
[ARP] Bank N: +note (M total)    si lastDeltaWasAdd
[ARP] Bank N: -note (M total)    sinon
```
puis `dirty = false` et `lastEmitMs = millis()`.

**Format protocole inchangé** — le firmware émet toujours `+note (M total)` /
`-note (M total)`. Le viewer voit moins d'events sous burst rapide (au max 1
par 50ms par bank) mais avec la valeur finale correcte. Aucun change parser
viewer.

Les events `[ARP] Play/Stop/Play — relaunch` restent immédiats (HIGH priority,
pas de coalesce — l'utilisateur veut voir Play/Stop avec zéro latence).

---

## Partie 3 — API publique

### §8 — Emit functions (typed)

```cpp
namespace viewer {

void begin();             // crée queue + task, à appeler en fin de setup()
void pollCommands();      // à appeler en tête de loop()

bool isConnected();       // lecture atomic, cheap

// Boot dump (appelé à la fin de setup() ET sur auto-resync post-connect)
void emitBanksHeader(uint8_t count);
void emitBank(uint8_t idx, BankType type, uint8_t chan, char group,
              ArpDivision div, bool playing, uint8_t octaveOrMutation);
void emitState(uint8_t bankIdx);  // dump complet du slot, lit depuis s_banks + s_potRouter
void emitGlobals();               // lit s_potRouter + s_clockManager
void emitSettings();              // lit s_nvsManager.getLoadedSettings()
void emitReady(uint8_t currentBank);

// Runtime events
void emitBankSwitch(uint8_t newBank);              // émet [BANK] Bank N + [STATE]
void emitScale(ScaleChangeType kind, const ScaleConfig& scale);
void emitArpOctave(uint8_t bank, uint8_t octave);
void emitArpGenMutation(uint8_t bank, uint8_t level);
void emitArpNoteAdd(uint8_t bank, uint8_t pileCount);     // alimente le coalesce 50ms
void emitArpNoteRemove(uint8_t bank, uint8_t pileCount);  // alimente le coalesce 50ms
void emitArpPlay(uint8_t bank, uint8_t pileCount, bool relaunch);
void emitArpStop(uint8_t bank, uint8_t pileCount);
void emitArpQueueFull();
void emitGenSeed(uint16_t seqLen, uint8_t eInit, uint8_t pileCount,
                 int8_t lo, int8_t hi);
void emitClockSource(const char* srcLabel, float bpmIfLastKnown);  // bpm = 0 si non last-known
void emitClockBpm(float bpm, const char* srcLabel);   // [CLOCK] BPM=X src=usb (M2)
void emitMidiTransport(const char* transport, bool connected);
void emitPanic();
void emitPot(uint8_t slot, PotTarget target, const char* valueStr,
             const char* unit, Priority prio);

}  // namespace viewer
```

`Priority` enum interne : `HIGH`, `LOW`.

### §9 — Input command parser

```cpp
namespace viewer {

void pollCommands();  // non-bloquant, scan Serial.available() + dispatch

}
```

Commands reconnues Phase 1 (refactor des `pollRuntimeCommands()` existants) :
- `?STATE` → re-émet `[STATE]` du foreground bank + `[READY]`
- `?BANKS` → re-émet `[BANKS] + 8× [BANK]` + `[READY]`
- `?BOTH` → re-émet `[BANKS] + 8× [BANK] + 8× [STATE] + [GLOBALS] + [SETTINGS]`
  + reset sentinels `s_dbg*` + `[READY]` *(spec §3.4 fix)*
- `?ALL` → comme `?BOTH` + `[LED_DUMP]` + `[COLORS_DUMP]` + `[POTMAP_DUMP]`

Hook Phase 2 :
```cpp
// dans dispatchCommand(const char* cmd):
if (cmd[0] == '!') {
  parseWriteCommand(cmd);  // Phase 2 : !CLOCKMODE=slave, !BONUS=N, etc.
}
```

`parseWriteCommand` est défini en Phase 1 comme stub vide (ou retourne
`[ERROR] Phase 2 not implemented`). Phase 2 le remplit avec les handlers.

---

## Partie 4 — Boot sequence

### §10 — Tagging `[BOOT *]`

Renommage uniforme :

| Ancien prefix | Nouveau prefix | Sites |
|---|---|---|
| `=== ILLPAD48 V2 ===` | `[BOOT] === ILLPAD48 V2 ===` | main.cpp:507 |
| `[INIT] *` | `[BOOT] *` | main.cpp:520, 533, 593, 628, 655, 662, 668, 685, 715, 722, 767, 786, 797, 804, 819, 825 |
| `[INIT] Ready.` | `[BOOT] Ready.` | main.cpp:843 — **breaking change pour ModeDetector viewer** |
| `[INIT] FATAL: *` | `[FATAL] *` | main.cpp:526 — promu en event always-on, cf. §14 |
| `[KB] *` | `[BOOT KB] *` | CapacitiveKeyboard.cpp:204, 209, 214-216, 224, 226, 407, 417-419, 429-432, 452-454, 539-558, 563-578, 591, 674, 676-680, 698, 702-705, 752-759 |
| `[NVS] *` (boot calls) | `[BOOT NVS] *` | NvsManager.cpp:172, 513, 520, 539, 541, 547, 574, 580, 588, 610, 626, 658, 674, 692, 710, 723, 751, 754, 769, 782, 793, 808, 820, 835, 850, 864, 882, 902, 920, 924, 936, 949, 980, 1120 |
| `[POT] Seed *` (PotFilter) | `[BOOT POT] Seed *` | PotFilter.cpp:136, 144 |
| `[POT] Rebuilt/initialized *` (PotRouter) | `[BOOT POT] Rebuilt/initialized *` | PotRouter.cpp:275, 364 |
| `[MIDI] USB MIDI initialized` etc. (boot) | `[BOOT MIDI] *` | MidiTransport.cpp:62, 69, 92, 103, 107, 123 — **uniquement begin() lines** |

**Note clé** : `[NVS]` runtime emissions (write failures rares) restent
`[NVS]` (pas de `BOOT` prefix). Distinction par contexte d'appel — pas par
prefix. Si `loadAll()` est appelé → boot. Si `notifyIfDirty()` ou
`workerTaskFunc()` → runtime.

**Pour MidiTransport** : `onBleConnected()` / `onBleDisconnected()` / `tud_mounted` change emissions ne sont **pas** boot — elles restent
`[MIDI] BLE connected` / `[MIDI] USB connected` (parser viewer §1.14).
Seules les lignes `*MIDI initialized.* /* MIDI disabled (USB only).*` deviennent
`[BOOT MIDI] *`.

### §11 — Ordre exact post-renaming

```
[BOOT] === ILLPAD48 V2 ===
[BOOT] I2C OK.
[BOOT] Keyboard OK.
[BOOT] Hold rear button to enter setup mode...
[BOOT KB] Starting capacitive keyboard init...
[BOOT KB] Calibration data loaded.
[BOOT KB] Valid calibration loaded from NVS.
[BOOT POT] Seed 0: median=2048 (sorted=...)
[BOOT POT] MCP3208 boot OK.
[BOOT MIDI] USB MIDI initialized.
[BOOT MIDI] BLE MIDI initialized.
[BOOT] MIDI Transport OK.
[BOOT] ClockManager OK.
[BOOT] MIDI Engine OK.
[BOOT NVS] Bank loaded: 3
[BOOT NVS] Scale bank 1: chrom=0 root=2 mode=0
[BOOT NVS] Bank types + quantize + scale groups + ARPEG_GEN params loaded (v4 store).
[BOOT NVS] Velocity params loaded.
[BOOT NVS] Pitch bend offsets loaded.
[BOOT NVS] Arp pot params loaded (v1).
[BOOT NVS] Tempo: 120 BPM
[BOOT NVS] Pot params: shape=0.50 slew=120 dz=8
[BOOT NVS] LED brightness: 200
[BOOT NVS] Pad sensitivity: 15
[BOOT NVS] Pad order loaded.
[BOOT NVS] Bank pads loaded.
[BOOT NVS] Scale pads loaded (v2 store).
[BOOT NVS] Arp pads loaded (v2 store): hold=23 oct=25,26,27,28
[BOOT NVS] Settings: profile=0, atRate=10, bleInt=2, clock=0, dblTap=400
[BOOT NVS] LED settings + color slots loaded.
[BOOT NVS] PotRouter loaded: tempo=120 shape=0.50 slew=120 dz=8 bright=200 sens=15
[BOOT NVS] loaded 4 control pad(s)
[BOOT NVS] Pot mapping loaded.
[BOOT NVS] Pot filter config loaded.
[BOOT NVS] Task created (Core 1, priority 1).
[BOOT POT] Rebuilt 16 bindings from mapping (4 CC slots)
[BOOT POT] 16 bindings, 5 pots initialized
[BOOT] PotFilter + PotRouter OK.
[BOOT] BankManager OK.
[BOOT] ScaleManager OK.
[BOOT] ControlPadManager OK.
[BOOT] NvsManager OK.
[BOOT] ArpScheduler OK.
[BOOT] Ready.                          ← marker ModeDetector
[CLOCK] ClockManager initialized (internal clock).
                                       ← viewer::begin() ici, crée queue + task
[BANKS] count=8                        ← début boot dump via le module
[BANK] idx=1 type=...
[BANK] idx=2 ...
...
[STATE] bank=1 ...
[STATE] bank=2 ...
...
[GLOBALS] Tempo=120 LED_Bright=200 PadSens=15 ClockSource=internal
[SETTINGS] ClockMode=slave PanicReconnect=1 DoubleTapMs=400 AftertouchRate=10 BleInterval=2
[READY] current=3
```

**Note** : `[CLOCK] ClockManager initialized` est émis par `clockManager.begin()`
qui s'exécute *avant* `viewer::begin()`. C'est donc encore raw Serial.print
gated DEBUG_SERIAL. Cohérent — le viewer reçoit l'event dans tous les cas.

---

## Partie 5 — Protocole

### §12 — `[GLOBALS]` (G3 — runtime instantané)

Format key=value, ordre libre, parser-friendly :
```
[GLOBALS] Tempo=120 LED_Bright=200 PadSens=15 ClockSource=internal
```

Fields Phase 1 (4) :
- `Tempo` = `s_potRouter.getTempoBPM()` — cohérent avec les `[POT] -- : Tempo=N`
  events émis quand le pot Tempo bouge. Le viewer affiche la valeur de
  consigne, pas la PLL BPM réelle quand external sync. La PLL BPM externe est
  émise séparément via `[CLOCK] BPM=` (cf. §15 spec §3.3 fix) — c'est un fait
  distinct, à représenter séparément côté viewer Model.
- `LED_Bright` = `s_potRouter.getLedBrightness()`
- `PadSens` = `s_potRouter.getPadSensitivity()`
- `ClockSource` ∈ `{internal, usb, ble, last}` — nouveau getter
  `s_clockManager.getActiveSourceLabel()`.

Emission :
- Boot dump (fin de setup, après [READY] de boot).
- Post-connect auto-resync.
- Réponse `?BOTH` et `?ALL`.
- **Optionnel** : ré-émis sur change (debounced) — à trancher dans le plan.
  Probablement pas nécessaire en Phase 1 car les changes des fields
  individuels sont déjà émis via `[POT]` et `[CLOCK]`.

### §13 — `[SETTINGS]` (G3 — config NVS persistante)

Format key=value :
```
[SETTINGS] ClockMode=slave PanicReconnect=1 DoubleTapMs=400 AftertouchRate=10 BleInterval=2 BatAdcFull=4095
```

Fields Phase 1 (lecture seule — Phase 2 ajoutera les write commands) :
- `ClockMode` ∈ `{slave, master}` = `s_settings.clockMode`
- `PanicReconnect` ∈ `{0, 1}` = `s_settings.panicOnReconnect`
- `DoubleTapMs` = `s_settings.doubleTapMs`
- `AftertouchRate` = `s_settings.aftertouchRate`
- `BleInterval` ∈ `{0, 1, 2, 3}` = `s_settings.bleInterval` (BLE_OFF/LOW/NORMAL/SAVER)
- `BatAdcFull` = `s_settings.batAdcAtFull`

Emission :
- Boot dump.
- Post-connect auto-resync.
- Réponse `?BOTH` et `?ALL`.
- Phase 2 : après chaque commande write `!SETTING=val` réussie (confirmation).

**Note critique** : `[SETTINGS]` peut être étendu en Phase 2 avec d'autres
fields (ARPEG_GEN per-bank — sera plutôt dans un nouveau event
`[BANK_SETTINGS] bank=N ...` ou folded dans `[STATE]`, décision à trancher
en Phase 2).

### §14 — `[FATAL]` event (always-on, parser-recognized)

Nouveau format protocole :
```
[FATAL] Keyboard init failed!
[FATAL] NVS corrupted at <key>
```

Format : `[FATAL] <human-readable message>`. Pas de structure interne — c'est
un dernier message avant que le firmware reste bloqué en LED error.

Émission :
- **UNGATED** (always, même DEBUG_SERIAL=0) — exception au contrat "silence
  complète" justifiée par la criticité.
- Raw Serial.print + Serial.flush(), pas via le module (le module n'est pas
  forcément créé à ce point).
- Émis avant l'appel à `s_leds.showBootFailure(N)` pour que le user voie
  l'erreur en serial.

Impact viewer : 1 nouveau parser branch (~5 lignes) + 1 overlay critique
rouge persistant (différent du PANIC qui dure 2.5s).

### §15 — Spec parser viewer §3 fixes embedded

| Finding spec | Phase 1 action |
|---|---|
| §3.1 `[GLOBALS]` event | **DONE** dans cette spec §12 |
| §3.2 sentinel collision | **DONE** par commit `6b86fcb` — spec doc à mettre à jour |
| §3.3 `[CLOCK]` BPM change | **DONE** : nouveau `emitClockBpm(bpm, src)` debounced ±1 BPM |
| §3.4 `?BOTH` self-sufficient | **DONE** : `?BOTH` inclut `[GLOBALS]` + `[SETTINGS]` + reset sentinels |
| §3.5 `[POT]` slot field | **No-op** : déjà OK |
| §3.6 string literals stable | **No-op** : décrit le contrat, pas un fix |

---

## Partie 6 — Migration

### §16 — Sites d'émission à migrer (inventaire)

À migrer vers `viewer::emit_*()` (= passe par la task + queue) :

| Site | Ancien call | Nouveau call |
|---|---|---|
| main.cpp:172 (dumpBanksGlobal header) | `Serial.printf("[BANKS] count=%d\n", ...)` | `viewer::emitBanksHeader(NUM_BANKS)` |
| main.cpp:180-196 (dumpBanksGlobal body) | `Serial.printf("[BANK] idx=...\n")` ×8 | `viewer::emitBank(idx, ...)` ×8 |
| main.cpp:291-348 (dumpBankState) | 9 `Serial.printf` | refactoré en `viewer::emitState(idx)` |
| main.cpp:454-456 (emitReady) | `Serial.printf("[READY] current=%u\n", ...)` | `viewer::emitReady(current)` |
| main.cpp:1205, 1214 (handlePotPipeline CC/PB) | `Serial.printf("[POT] %s: CC=...\n")` | `viewer::emitPot(slot, target, val, unit, LOW)` |
| main.cpp:1276-1413 (debugOutput) | 16 `Serial.printf` for params | 16 `viewer::emitPot(...)` (LOW priority) |
| main.cpp:151 (midiPanic) | `Serial.println("[PANIC] ...")` | `viewer::emitPanic()` |
| BankManager.cpp:237-239 | `Serial.printf("[BANK] Bank ...")` + `dumpBankState()` | `viewer::emitBankSwitch(idx)` |
| ScaleManager.cpp:140, 162, 180 | `Serial.printf("[SCALE] ...")` | `viewer::emitScale(...)` |
| ScaleManager.cpp:206, 211 | `Serial.printf("[ARP_GEN]/[ARP] Octave...")` | `viewer::emitArpOctave(...)` / `viewer::emitArpGenMutation(...)` |
| ArpEngine.cpp:323, 344 | `Serial.printf("[GEN] seed ...")` | `viewer::emitGenSeed(...)` |
| ArpEngine.cpp:436 | `Serial.printf("[ARP] Bank N: +note ...")` | `viewer::emitArpNoteAdd(bank, count)` |
| ArpEngine.cpp:473 | `Serial.printf("[ARP] Bank N: -note ...")` | `viewer::emitArpNoteRemove(bank, count)` |
| ArpEngine.cpp:536, 540 | `Serial.printf("[ARP] Bank N: Play ...")` | `viewer::emitArpPlay(bank, count, relaunch)` |
| ArpEngine.cpp:556 | `Serial.printf("[ARP] Bank N: Stop ...")` | `viewer::emitArpStop(bank, count)` |
| ArpEngine.cpp:896 | `Serial.println("[ARP] WARNING: Event queue full...")` | `viewer::emitArpQueueFull()` |
| ClockManager.cpp:98, 135, 156, 161, 175 | `Serial.printf/println("[CLOCK] Source: ...")` | `viewer::emitClockSource(label, bpm)` |
| MidiTransport.cpp:62, 69 (BLE connect/disconn) | `Serial.println("[MIDI] BLE ...")` | `viewer::emitMidiTransport("BLE", state)` |
| MidiTransport.cpp:123 (USB connect/disconn) | `Serial.printf("[MIDI] USB ...")` | `viewer::emitMidiTransport("USB", state)` |

**Phase 1 = 30 sites migrés**. Total des `Serial.print*` dans le firmware passe
de ~300 à ~270 (les 30 migrés deviennent des calls module). Plus une trentaine
de boot sites renommés `[BOOT *]` mais restant raw Serial.print.

### §17 — Cleanup UNGATED + dead code

| Élément | Action |
|---|---|
| Banner `=== ILLPAD48 V2 ===` UNGATED (I4) | Gated DEBUG_SERIAL, renommé `[BOOT] === ILLPAD48 V2 ===` |
| `[INIT] FATAL: Keyboard init failed!` UNGATED (I4) | Promu `[FATAL] Keyboard init failed!` always-on (cf. §14) |
| `[NVS] ArpPotStore raw/v0` UNGATED (I5) | Gated DEBUG_SERIAL, renommé `[BOOT NVS] ...` |
| `logFullBaselineTable()` orphan (I6) | **Suppression complète** (déclaration .h + définition .cpp). 12 lignes nettes. |
| Spec §3.2 documenté FIXÉE | À reporter dans la spec firmware-viewer-protocol.md côté worktree viewer (sync via `git merge main` plus tard) |

### §18 — Découpage en sous-phases (proposé pour le plan)

Le plan d'implémentation (étape suivante) découpera Phase 1 en sous-tâches
indépendamment commitables :

- **1.A — Plomberie module** : créer `ViewerSerial.{cpp,h}`, queue, task,
  atomic flag, `if (Serial)` dormance, `setTxTimeoutMs(0) + setTxBufferSize(8192)`.
  Aucune migration d'emit. Compile gate.
- **1.B — Boot debug tagging** : renommage `[INIT/KB/NVS/POT/MIDI init]` →
  `[BOOT *]`. Cleanup UNGATED (banner, ArpPotStore v0, logFullBaselineTable).
  Pas de touche au module encore. Compile + HW gate (viewer affiche les nouveaux
  prefixes — test visuel).
- **1.C — Migration emissions runtime** : un sous-système à la fois pour limiter
  le risque :
  - 1.C.1 — `[POT]` debugOutput + handlePotPipeline.
  - 1.C.2 — `[BANK]` + `[STATE]` (BankManager + dumpBankState).
  - 1.C.3 — `[ARP]` + `[GEN]` (ArpEngine).
  - 1.C.4 — `[SCALE]` + `[ARP_GEN]` octave (ScaleManager).
  - 1.C.5 — `[CLOCK]` + `[MIDI]` (ClockManager + MidiTransport).
  - 1.C.6 — `[PANIC]` (midiPanic).
  Chaque sous-step est un commit avec compile gate + HW smoke test.
- **1.D — `[GLOBALS]` + `[SETTINGS]` + sentinel reset** : nouveau code (pas
  migration). Émet au boot + auto-resync + `?BOTH/?ALL`. Inclut le nouveau
  getter `s_clockManager.getActiveSourceLabel()`. Compile + HW gate.
- **1.E — `[CLOCK] BPM=` debounced** : nouveau emit dans `ClockManager::updatePLL()`
  ou `processIncomingTicks()`. Compile + HW gate avec source externe.
- **1.F — `[FATAL]` event** : promotion de `[INIT] FATAL` + parser branch
  côté viewer (sur la branche `viewer-juce`). Coordination cross-worktree.
- **1.G — Sync spec viewer doc** : update `firmware-viewer-protocol.md` côté
  viewer-juce avec les nouveaux events `[BOOT *]`, `[GLOBALS]`, `[SETTINGS]`,
  `[FATAL]`, marquage §3.2 DONE.

---

## Partie 7 — Phase 2 hooks (préparation, pas implémentation)

### §19 — Input parser extensible

`viewer::pollCommands()` reconnait déjà `?XXX`. Le hook Phase 2 est :
```cpp
static void dispatchCommand(const char* cmd) {
  if (cmd[0] == '?') {
    dispatchQueryCommand(cmd);
  } else if (cmd[0] == '!') {
    dispatchWriteCommand(cmd);  // Phase 1 : stub, Phase 2 : impl
  }
}

static void dispatchWriteCommand(const char* cmd) {
  // Phase 1 : Serial.println("[ERROR] write commands not yet implemented");
  // Phase 2 : strncmp branches pour !CLOCKMODE=*, !PANIC_RECONNECT=*, etc.
}
```

### §20 — Confirmation pattern

Après chaque command write réussi, le firmware re-émet l'event correspondant
pour confirmer :
- `!CLOCKMODE=slave` → re-émet `[SETTINGS] ClockMode=slave ...`
- `!BONUS=3 BANK=2` → re-émet `[STATE] bank=2 ... bonusPile=3 ...` (si on
  ajoute bonusPile au [STATE] en Phase 2)

Erreur : `[ERROR] code=X cmd=<orig>`. À spécifier en Phase 2.

### §21 — Inventaire Phase 2 (à faire en début de Phase 2)

Pour mémoire :
- **Palier vert** (no side-effect) : ClockMode, PanicReconnect, AftertouchRate,
  DoubleTapMs.
- **ARPEG_GEN per-bank** (Tool 5) : bonusPile, marginWalk, proximityFactor,
  ecart.
- **Palier rouge exclu** : pot mapping, color slots, LED settings, padOrder,
  bankPads, scale pads, hold pad, octave pads, BankType.

L'inventaire complet (incluant les conflits potentiels avec catch logic pour
les pot-controlled params si on étend) est à faire en Phase 2.

---

## Partie 8 — Tests et validation

### §22 — Compile gates

Chaque sous-step Phase 1.A → 1.G doit compiler avec :
```
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Exit 0, zéro nouveau warning.

### §23 — Runtime validation HW

Smoke tests par sous-step :
- 1.A : firmware boot, viewer connect/disconnect/reconnect — vérifier dormance
  (mesurer CPU Core 1 sous load viewer absent : doit être identique à viewer
  présent moins le coût du drain).
- 1.B : viewer affiche les `[BOOT *]` en debug log + reconnaît `[BOOT] Ready.`
  comme transition setup→runtime.
- 1.C.* : chaque sous-step → fonctionnalité correspondante toujours visible
  côté viewer (bank switch, pot move, arp note, scale change, clock source).
  Aucun event perdu en bench (viewer toujours lisant rapide).
- 1.D : `?BOTH` retourne `[GLOBALS]` + `[SETTINGS]` + `[POT]` complets.
  Resync button viewer marche entièrement.
- 1.E : start firmware avec USB clock externe à 100 BPM, change la source à
  150 BPM, vérifier que viewer header reflète 150 BPM (via `[CLOCK] BPM=`).
- 1.F : débrancher MPR121 #1 → reboot → viewer reçoit `[FATAL]` overlay.
- 1.G : viewer parser cross-audit avec spec doc cohérent.

### §24 — Côté viewer (impact à coder dans le worktree `viewer-juce`)

| Phase 1 step | Code viewer requis |
|---|---|
| 1.A plomberie | **Aucun** |
| 1.B boot tagging | ModeDetector : guette `[BOOT] Ready.` au lieu de `[INIT] Ready.` (~1 ligne). **Optionnel** : new parse branch `[BOOT *]` pour boot log panel UI (~30 lignes + UI). Sans, les boot lines restent UnknownEvent comme aujourd'hui — pas de régression. |
| 1.C migration | **Aucun** (format identique) |
| 1.D `[GLOBALS]`/`[SETTINGS]` | New parse branches (~15 lignes) + handler dans Model.cpp pour hydrater `device.*` (~10 lignes). Suppression du slot-scanning brittle dans `Model::applyState` (~15 lignes nettes). |
| 1.E `[CLOCK] BPM=` | New parse branch (~5 lignes) + handler dans Model.cpp (~3 lignes). |
| 1.F `[FATAL]` | New parse branch (~5 lignes) + UI overlay critique (~30 lignes JUCE component). |
| 1.G sync doc | Update `firmware-viewer-protocol.md` (édition pure doc). |

Toutes les coding changes côté viewer vivent sur la branche `viewer-juce`,
worktree `../ILLPAD_V2-viewer/`, conformément au CLAUDE.md projet "Branche
viewer-juce — exception au git workflow toujours main".

---

## Partie 9 — Hors scope

### §25 — Hors scope Phase 1

- Implémentation des commands write `!CMD=val` (Phase 2).
- Inventaire complet des params Phase 2 — fait au début de Phase 2.
- Tests unitaires firmware (architecture Arduino n'en a pas — bench manuel).
- Refonte du format `[STATE]` pour inclure les ARPEG_GEN per-bank params
  (Phase 2 décide : new event vs extension `[STATE]`).
- Renommage interne des sous-systèmes (BankManager, etc.) — pas touché.

### §26 — Hors scope Phase 2

- Pot mapping / color slots / LED settings (palier rouge, trop interconnecté).
- padOrder, bankPads, scale pads, hold pad, octave pads (setup-only par
  construction, cf. CLAUDE.md projet "Setup mode boot-only").
- BankType changes runtime (commute tous les invariants arp/scale).
- Logging persistant côté firmware (NVS limit + scope creep).

### §27 — Hors scope total

- Migration vers un transport autre que USB CDC (Bluetooth GATT, Ethernet) —
  pas dans le projet ILLPAD.
- Format binaire / JSON / Protocol Buffers — le format ASCII line-based reste
  le contrat. Forward-compat via key=value.

---

## Annexe — Glossaire

- **Boot debug** : émissions serial pendant `setup()`, prefixe `[BOOT *]`,
  informational uniquement. Le viewer peut les afficher dans un boot log
  panel ou les ignorer.
- **Viewer protocol events** : émissions runtime parser-recognized, prefixes
  `[BANKS]/[BANK]/[STATE]/[POT]/[SCALE]/[ARP]/[ARP_GEN]/[GEN]/[CLOCK]/[MIDI]/[PANIC]/[READY]/[GLOBALS]/[SETTINGS]/[FATAL]`.
- **Hardware debug** : émissions runtime gated DEBUG_HARDWARE, prefixe `[HW]`,
  pour bench dev, non viewer.
- **Backpressure** : drop policy LOW priority events quand queue > 70% pleine.
- **Coalesce** : combiner plusieurs events similaires en un seul snapshot
  émis périodiquement (ex. ARP +/-note throttle 50ms).
- **Dormance** : module idle quand `if (Serial)` retourne false (viewer pas
  connecté). Zéro overhead Core 1.
- **Auto-resync** : re-émission automatique du dump complet à la transition
  viewer-disconnected → viewer-connected (X1 silent dump).
