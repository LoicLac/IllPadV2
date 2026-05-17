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

12 choix structurants validés par l'utilisateur (révisés post-relecture
2026-05-17) :

| # | Décision | Choix retenu |
|---|---|---|
| 1 | Centralisation | Module dédié `ViewerSerial` |
| 2 | Architecture transport | **B-full** : background task + queue FreeRTOS (vs B-lite direct non-blocking) |
| 3 | Bidirectionnel | **Prépare Phase 2** (hook parser extensible), reste passive Phase 1 |
| 4 | Dormance | **3c** : atomic flag + auto-resync à la transition false→true |
| 5 | Post-connect re-sync | **X1** silent auto-dump (= `?BOTH` interne implicite) |
| 6 | Priority levels | **P1** 2-level (HIGH / LOW) avec backpressure à 70% |
| 7 | Coalescing ARP +/-note | **Aucun** (révisé — aligné sur Q7b, droppable LOW priority sous backpressure) |
| 8 | Coalescing POT live | **Aucun** (first-emit guard actuel suffit, droppable LOW) |
| 9 | Globals split | **G3** : `[GLOBALS]` (runtime) + `[SETTINGS]` (NVS persist) |
| 10 | Tagging boot | **`[BOOT *]`** uniforme (informational) |
| 11 | Input command owner | **Module owne `pollCommands()`** (canal complet) |
| 12 | Fatal boot fail | **Event `[FATAL]`** always-on, parser-recognized |
| 13 | Task priority | **Strict inférieure au main loop** — posture safe d'entrée (priorité 0 ou `tskIDLE_PRIORITY+1`). Pas de préemption garantie. |
| 14 | Cross-worktree workflow | **Viewer pré-codé en avance** avec la spec, avant Phase 1.A firmware. Pas de gating entre sous-phases. |

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

### §3b — Task priority — posture safe d'entrée

**Contrainte non-négociable** : la task de drain ne doit JAMAIS préempter le
main loop Core 1. Le loop gère sensing → MIDI flush → arp scheduler → pot
pipeline, et toute préemption introduit du jitter MIDI audible.

Implémentation :
- Priorité task = `0` (idle priority) ou `tskIDLE_PRIORITY+1`. Strictement
  inférieure au main loop (qui tourne en priorité 1 sur Core 1).
- Pinned Core 1 (pas Core 0 — Core 0 est saturé par sensingTask priorité 1).
- Pas de `yield()` ou `taskYIELD()` manuel dans le hot-path d'émission (la
  préemption par le scheduler suffit, basée sur les priorités).
- Validation en bench : avant/après mesure du CPU loop (via `uxTaskGetSystemState`
  ou un proxy serial) — doit être inchangé.

Le coût side : si le main loop est saturé (ce qui est déjà le cas Core 0
sensing à ~92%), la task drain peut être starved indéfiniment, la queue se
remplit, des LOW events drop. **C'est le comportement voulu** : la priorité
absolue est le live MIDI, le viewer attendra.

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
  `[ARP] Play/Stop`, `[ARP] Play — relaunch`, `[ARP_GEN] MutationLevel`,
  `[GLOBALS]`, `[SETTINGS]`, post-connect auto-dump.
- **LOW** (droppable backpressure ≥ 70%) :
  `[POT]` live values, `[ARP] Bank N: +note/-note (M total)`, `[GEN] seed`,
  `[ARP] WARNING: Event queue full`.

### §7 — Pas de coalescing module-side (révisé — aligné Q7b)

Décision : **aucun coalescing au niveau du module**, ni pour POT live ni pour
ARP +/-note. Chaque event passe directement dans la queue avec sa priority.

Justifications :
- Le first-emit guard `s_dbg*` existant dans `debugOutput()` (`main.cpp:1299`)
  fait déjà le skip-if-unchanged pour les POT — un POT stable n'émet rien.
- En burst (rotation rapide de pot, ARP rolls rapides), les LOW events
  saturent la queue → backpressure kick in à 70% → les nouveaux LOW sont
  droppés. Le viewer perd quelques transitions intermédiaires mais reçoit
  la valeur finale lorsque le pot se stabilise ou que la queue se vide.
- Posture safe d'entrée : ajouter du coalescing complique la logique de drain
  (timer per-bank, snapshot dirty, etc.) pour résoudre un problème
  hypothétique. À ajouter plus tard si l'usage live le réclame.

**Réversibilité** : si l'usage révèle des bursts ARP qui saturent la queue de
façon visible (compteur de pile désynchronisé, viewer figé pendant un roll),
ajouter un coalesce 50ms ciblé sera trivial — pattern snapshot-per-bank +
flush timer dans la task drain. À ce moment, on documentera dans une révision
de spec.

Les events `[ARP] Play/Stop/Play — relaunch` sont HIGH priority, jamais
droppés sauf queue 100% pleine — l'utilisateur veut voir Play/Stop avec zéro
latence visible.

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
| `[INIT] *` (boot) | `[BOOT] *` | main.cpp:520, 533, 593, 655, 662, 668, 685, 715, 722, 767, 786, 797, 804, 819, 825 |
| `[INIT] Ready.` | `[BOOT] Ready.` | main.cpp:843 — **breaking change pour ModeDetector viewer** (coordination cf. §14b) |
| `[INIT] FATAL: *` | `[FATAL] *` | main.cpp:526 — promu en event always-on, cf. §14 |
| `[SETUP] Entering setup mode...` | **(inchangé)** | main.cpp:628 — parser-recognized setup mode marker, cf. §14b |
| `[KB] *` (boot) | `[BOOT KB] *` | CapacitiveKeyboard.cpp:204, 209, 214-216, 224, 226, 407, 417-419, 429-432, 452-454, 539-558, 563-578, 591, 674, 676-680, 698, 702-705 |
| `[KB]` runtime (logFullBaselineTable, UNGATED) | **(supprimé — orphan)** | CapacitiveKeyboard.cpp:752-759 (cf. §17 cleanup) |
| `[NVS] *` dans `loadAll()` | `[BOOT NVS] *` | NvsManager.cpp:172, 610, 626, 658, 674, 692, 710, 723, 751, 754, 769, 782, 793, 808, 820, 835, 850, 864, 882, 902, 920, 924, 936, 949, 980 |
| `[NVS] *` dans `loadBlob/saveBlob` + worker task | **(inchangé)** | NvsManager.cpp:513, 520, 539, 541, 547, 574, 580, 588, 1120 — appelé depuis boot ET runtime, ambigu → reste `[NVS]` generic |
| `[POT] Seed *` (PotFilter) | `[BOOT POT] Seed *` | PotFilter.cpp:136, 144 |
| `[POT] Rebuilt/initialized *` (PotRouter) | `[BOOT POT] Rebuilt/initialized *` | PotRouter.cpp:275, 364 |
| `[MIDI] USB/BLE MIDI initialized` (boot) | `[BOOT MIDI] *` | MidiTransport.cpp:92, 103, 107 — **uniquement begin() lines** |
| `[MIDI] USB/BLE connected/disconnected` (runtime) | **(inchangé)** | MidiTransport.cpp:62, 69, 123 — parser-recognized §1.14 |

**Note clé `[NVS]`** : la discrimination boot/runtime se fait par fonction
appelante. Le `[BOOT NVS]` rename ne s'applique QUE aux lignes situées dans
`NvsManager::loadAll()` et helpers boot-only. Les helpers `loadBlob`/`saveBlob`
+ le worker task save sont appelés depuis le runtime (NVS write debouncing,
runtime save errors) — leurs `Serial.printf("[NVS] ...")` restent inchangés
pour ne pas mislabeler les runtime events comme boot.

**Note clé `[KB]`** : `logFullBaselineTable()` (`CapacitiveKeyboard.cpp:751-760`)
est dead code (0 caller dans `src/`) ET UNGATED. Suppression complète (cf. §17),
pas de rename `[BOOT KB]` à faire.

### §11 — Ordre exact post-renaming

Reflète le flow réel de [`setup()`](../../../src/main.cpp:502) :

```
[BOOT] === ILLPAD48 V2 ===                  ← main.cpp:507 (banner)
[BOOT] I2C OK.                              ← main.cpp:520 (post-I2C init)
[BOOT KB] Starting capacitive keyboard init...
[BOOT KB] Calibration data loaded.
[BOOT KB] Valid calibration loaded from NVS.
[BOOT KB] Autoconfig complete.              ← CapacitiveKeyboard.cpp:591 (fin de begin())
[BOOT] Keyboard OK.                         ← main.cpp:533 (post-keyboard begin)
[BOOT POT] Seed 0: median=2048 (sorted=...)
[BOOT POT] Seed 1: ...
...
[BOOT POT] MCP3208 boot OK.                 ← PotFilter.cpp:144 (fin de PotFilter::begin)
[BOOT] Hold rear button to enter setup mode...  ← main.cpp:593
                                            ← wait window 0-3s, setup mode entry possible
                                              (si entrée setup : [SETUP] Entering setup mode...
                                               + VT100 prend le relais, jamais de Ready)
[BOOT MIDI] USB MIDI initialized.           ← MidiTransport.cpp:92
[BOOT MIDI] BLE MIDI initialized.           ← MidiTransport.cpp:103 (ou BLE disabled L107)
[BOOT] MIDI Transport OK.                   ← main.cpp:655
[CLOCK] ClockManager initialized (internal clock).
                                            ← ClockManager.cpp:24 (raw, dans clockManager.begin())
[BOOT] ClockManager OK.                     ← main.cpp:662
[BOOT] MIDI Engine OK.                      ← main.cpp:668
[BOOT NVS] Bank loaded: 3                   ← début bloc loadAll() lines
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
[BOOT] PotFilter + PotRouter OK.        ← main.cpp:819 (post-PotRouter::begin)
[BOOT] NvsManager OK.                   ← main.cpp:825 (post-NvsManager::begin)
[BOOT NVS] Task created (Core 1, priority 1).  ← NvsManager.cpp:172 (DANS NvsManager::begin, donc avant ce point)
[BOOT] ArpScheduler OK.                 ← main.cpp:767 (note : émis L767, donc avant PotFilter+PotRouter)
[BOOT] BankManager OK.                  ← main.cpp:786
[BOOT] ScaleManager OK.                 ← main.cpp:797
[BOOT] ControlPadManager OK.            ← main.cpp:804
[BOOT] Ready.                           ← main.cpp:843 — MARKER ModeDetector
                                        ← viewer::begin() ici (crée queue + task)
                                        ← À PARTIR D'ICI, tout via le module
[BANKS] count=8                         ← début boot dump via le module
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

**Note ordering réelle** : l'ordre listé ci-dessus reflète la séquence
serial telle qu'observée. Côté code, la séquence `[BOOT] ArpScheduler OK
→ BankManager → ScaleManager → ControlPadManager → PotFilter+PotRouter →
NvsManager → Ready` correspond aux appels `xManager.begin()` successifs
dans `setup()` — l'ordre listé reflète l'ordre d'émission, pas l'ordre
arbitraire d'ouverture des managers. À reverifier ligne-par-ligne lors de
1.B (rename).

**Note `[CLOCK]` init** : émis par `ClockManager::begin()` (ClockManager.cpp:24)
qui est appelé `setup()` AVANT `viewer::begin()`. Reste donc raw Serial.print
gated DEBUG_SERIAL — pas migré vers le module en Phase 1. Cohérent — la
ligne est parser-recognized (§1.13) et arrive bien chez le viewer.

**Note `[BOOT] Ready.` placement** : sentinel marker de transition boot →
runtime, le `viewer::begin()` est appelé JUSTE APRÈS sa transmission. Tous
les events suivants ([BANKS], [BANK], [STATE], [GLOBALS], [SETTINGS],
[READY]) passent par le module. Si Phase 1.A est appliquée sans 1.B
(scénario théorique — ne devrait pas exister puisque 1.A → 1.B est séquentiel),
on garderait `[INIT] Ready.` comme marker, et `viewer::begin()` au même
endroit.

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

### §14b — `[SETUP]` mode marker et reboot detection

Setup mode est accessible uniquement au boot (rear button held). Le firmware
émet `[SETUP] Entering setup mode...` ([main.cpp:628](../../../src/main.cpp:628))
juste avant de basculer tout le serial vers le rendu VT100 cockpit (escape
sequences ANSI). À partir de cet event, le canal serial **n'est plus** dans
le contrat protocole ligne-based — c'est un terminal VT100 destiné au mode
setup interactif.

**Émission** : reste raw Serial.print + `#if DEBUG_SERIAL`. Pas via le module
(setup mode entré avant `viewer::begin()`). Prefix `[SETUP]` inchangé pour
préserver la reconnaissance parser.

**Côté viewer (machine d'état)** :

1. À la réception de `[SETUP] Entering setup mode...` :
   - Geler la machine d'état Model (aucun update sur le stream suivant).
   - Afficher un état UI dédié, ex. `Firmware in setup mode (VT100 terminal active)`.
   - Optionnel : router le stream brut vers une fenêtre log, mais sans
     parsing protocole. Le terminal Python `ItermCode/vt100_serial_terminal.py`
     est l'outil interactif officiel — le viewer ne se substitue pas à lui.
2. Sortie de setup mode = `ESP.restart()` (Tool 0 confirmation). USB CDC
   disconnect brièvement (boot reset hardware), puis reconnect avec
   descripteur USB ré-énuméré.
3. Côté viewer, le parser détecte la déconnexion CDC (failed read sur
   JUCE SerialPort) :
   - Reset complet du Model.
   - Afficher UI `Waiting for firmware`.
   - À la reconnexion, traiter le flux comme un cold boot : recevoir les
     `[BOOT *]` puis `[BOOT] Ready.` puis le boot dump + `[GLOBALS]` +
     `[SETTINGS]` + `[READY]`.

**Important** : côté viewer, "CDC disconnect" est ambigu — peut être un reboot
firmware OU l'utilisateur a fermé l'app sur le Mac. Le reset Model est safe
dans les deux cas : à la reconnexion, le viewer re-recevra le dump complet
(boot auto-emit OU auto-resync §5 selon la voie).

**Impact viewer** :
- ~5 lignes JUCE pour gérer le `[SETUP]` marker + freeze Model.
- ~10 lignes JUCE pour détecter CDC disconnect + reset Model.
- 1 état UI supplémentaire `setup mode active` dans le header.

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

**Comptage** :
- ~22 **fonctions/blocs logiques** à migrer (1 fonction de migration par bloc).
- ~44 **lignes `Serial.printf/println` individuelles** touchées (principalement
  les 16 printfs séparés de `debugOutput()` et les 9 de `dumpBankState()` —
  chaque bloc reste 1 migration logique mais N lignes éditées).

**Helpers `?ALL` debug** ([main.cpp:359-447](../../../src/main.cpp:359)) :
`dumpLedSettings()`, `dumpColorSlots()`, `dumpPotMapping()` avec leurs prefixes
`[LED_DUMP]`, `[COLORS_DUMP]`, `[POTMAP_DUMP]` (6 lignes header/footer +
~30 lignes content). **Pas parser-recognized** côté viewer aujourd'hui, debug
seulement. **Décision** : restent en main.cpp, sont appelés directement par le
handler `?ALL` du module via callbacks ou un include. Pas de migration vers
`viewer::emit_*()` — le format vrac n'a pas de sémantique typed à exposer.

Total `Serial.print*` dans le firmware (estimation post-Phase 1) : ~300
aujourd'hui → ~280 après migration (les 22 blocs deviennent N appels module +
quelques lignes `[BOOT *]` raw Serial.print restantes). SetupUI.cpp (~80
calls) et CapacitiveKeyboard.cpp (~55 calls) restent inchangés en volume —
seuls leurs prefixes sont rebrandés `[BOOT KB]` etc.

### §17 — Cleanup UNGATED + dead code

| Élément | Action |
|---|---|
| Banner `=== ILLPAD48 V2 ===` UNGATED (I4) | Gated DEBUG_SERIAL, renommé `[BOOT] === ILLPAD48 V2 ===` |
| `[INIT] FATAL: Keyboard init failed!` UNGATED (I4) | Promu `[FATAL] Keyboard init failed!` always-on (cf. §14) |
| `[NVS] ArpPotStore raw/v0` UNGATED (I5) | Gated DEBUG_SERIAL, renommé `[BOOT NVS] ...` |
| `logFullBaselineTable()` orphan (I6) | **Suppression complète** (déclaration .h + définition .cpp). 12 lignes nettes. |
| Spec §3.2 documenté FIXÉE | À reporter dans la spec firmware-viewer-protocol.md côté worktree viewer (sync via `git merge main` plus tard) |

### §18 — Découpage en sous-phases (proposé pour le plan)

**Pré-Phase 1.A — Préparation viewer-juce** : avant toute migration firmware,
le viewer-juce est pré-codé selon cette spec :
- ModeDetector : `[BOOT] Ready.` au lieu de `[INIT] Ready.`.
- Parser branches pour `[BOOT *]`, `[GLOBALS]`, `[SETTINGS]`, `[FATAL]`,
  `[CLOCK] BPM=`, `[SETUP]` mode marker.
- UI : boot log panel (optionnel), [FATAL] overlay, setup-mode state in header.
- Handler `Model::applyGlobals()` + `Model::applySettings()`. Suppression du
  slot-scanning brittle dans `Model::applyState`.
- CDC disconnect handler (reset Model + UI "Waiting for firmware").

Tout est en place AVANT de lancer Phase 1.A firmware. Pas de gating
intermédiaire — le workflow firmware run sans pause entre les sous-phases.
Trade-off (cf. §24) : si un HW gate échoue, debug bilateral possible
(firmware + viewer).

Le plan d'implémentation (étape suivante) découpe Phase 1 firmware en
sous-tâches indépendamment commitables :

- **1.A — Plomberie module** : créer `ViewerSerial.{cpp,h}`, queue, task,
  atomic flag, `if (Serial)` dormance. Configuration HWCDC **dans cet ordre
  strict en début de `setup()`** :
  1. `Serial.setTxBufferSize(8192)` ← **doit être appelé AVANT `begin()`**
  2. `Serial.begin(115200)`
  3. `Serial.setTxTimeoutMs(0)` ← après `begin()`, pour bypasser l'auto-set
     à 100ms du framework
  Task créée en fin de `setup()` (avant le boot dump). Aucune migration
  d'emit dans cette sous-phase — `Serial.print*` raw inchangé partout.
  Compile gate + smoke test (viewer connecte sans regression).
- **1.B — Boot debug tagging + cleanup UNGATED** : renommage `[INIT/KB/NVS
  (loadAll only)/POT/MIDI init]` → `[BOOT *]`. Cleanup : banner gated,
  ArpPotStore v0 warning gated, `logFullBaselineTable()` supprimé. Pas de
  touche au module encore. Compile + smoke test (viewer reconnaît
  `[BOOT] Ready.` et continue à hydrater).
- **1.C — Migration emissions runtime vers le module** : un sous-système à
  la fois pour limiter le risque. Chaque sous-step est 1 commit avec compile
  gate + HW smoke test ciblé :
  - 1.C.1 — `[POT]` debugOutput + handlePotPipeline. Test : tourner chaque
    pot, viewer reflète.
  - 1.C.2 — `[BANK]` + `[STATE]` (BankManager + dumpBankState). Test :
    switch bank, viewer met à jour cells + active bank.
  - 1.C.3 — `[ARP]` + `[GEN]` (ArpEngine). **Pas de coalescing** (cf. §7
    révisé) — migration straightforward, +/-note LOW droppable, Play/Stop
    HIGH. Test : Play/Stop visible instantané, +note compteur correct sous
    burst.
  - 1.C.4 — `[SCALE]` + octave/mutation (ScaleManager). Test : change
    root/mode/chromatic + octave, viewer reflète.
  - 1.C.5 — `[CLOCK]` source + `[MIDI]` connect/disconnect. Test : plug/unplug
    USB et BLE, viewer header MIDI state correct.
  - 1.C.6 — `[PANIC]` (midiPanic). Test : triple-click rear, viewer overlay
    rouge 2.5s.
- **1.D — `[GLOBALS]` + `[SETTINGS]` + sentinel reset** : nouveau code. Inclut
  le nouveau getter `s_clockManager.getActiveSourceLabel()`. Émet au boot +
  auto-resync + `?BOTH/?ALL`. Reset `s_dbg*` sentinels sur `?BOTH/?ALL`.
  Compile + HW gate (header viewer populé immédiatement au boot).
- **1.E — `[CLOCK] BPM=` debounced** : nouveau emit dans `ClockManager` sur
  change ±1 BPM. HW gate avec source externe (DAW, hardware clock).
- **1.F — `[FATAL]` event** : promotion de `[INIT] FATAL` + Serial.flush().
  Émission UNGATED defensive. HW gate : forcer fail-init temporairement via
  commenter `kbOk` check, reboot, vérifier overlay. Restaurer code.
- **1.G — Sync spec viewer doc** : update `firmware-viewer-protocol.md` (côté
  worktree `viewer-juce`) avec les nouveaux events `[BOOT *]`, `[GLOBALS]`,
  `[SETTINGS]`, `[FATAL]`, `[SETUP]` mode marker, marquage §3.2 DONE.

**Estimation totale** : ~20h dev (firmware + viewer pré-coding).

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

### §24 — Côté viewer (workflow pré-codage)

**Workflow révisé** : le viewer-juce est **pré-codé en avance** avec toute
cette spec, AVANT le début de Phase 1.A firmware. Pas de gating
cross-worktree entre les sous-phases firmware — chaque HW gate teste la
chaîne complète end-to-end.

**Avantage** : aucune coordination synchrone, le workflow firmware run sans
pause, le viewer est déjà prêt à hydrater quoi que le firmware émette.

**Trade-off** : un HW gate qui échoue peut nécessiter du debug bilatéral
(firmware OR viewer ne respecte pas la spec). Le user accepte ce risque
car les changements côté viewer sont simples (~100 lignes au total).

**Inventaire pré-codage viewer-juce (à faire dans `../ILLPAD_V2-viewer/`)** :

| Code requis | Lignes estimées | Fichier viewer |
|---|---|---|
| ModeDetector : `[BOOT] Ready.` au lieu de `[INIT] Ready.` | ~1 ligne | `ModeDetector.cpp` |
| Parser branch `[BOOT *]` → boot log panel (optionnel) | ~10 lignes + UI panel ~30 lignes | `RuntimeParser.cpp` + UI |
| Parser branch `[GLOBALS]` + handler `Model::applyGlobals()` | ~15 + ~10 lignes | `RuntimeParser.cpp` + `Model.cpp` |
| Parser branch `[SETTINGS]` + handler `Model::applySettings()` | ~15 + ~10 lignes | `RuntimeParser.cpp` + `Model.cpp` |
| Suppression slot-scanning brittle dans `Model::applyState` | ~15 lignes nettes | `Model.cpp` |
| Parser branch `[CLOCK] BPM=` | ~5 + ~3 lignes | `RuntimeParser.cpp` + `Model.cpp` |
| Parser branch `[FATAL]` + UI overlay critique | ~5 + ~30 lignes JUCE | `RuntimeParser.cpp` + new component |
| Parser branch `[SETUP]` mode marker + freeze Model | ~5 lignes | `RuntimeParser.cpp` |
| CDC disconnect handler + reset Model + UI "Waiting for firmware" | ~10 lignes | `SerialReader.cpp` (ou équivalent) |

**Total estimé** : ~135 lignes côté viewer-juce, en grande partie dans
`RuntimeParser.cpp` + `Model.cpp`. UI overlay [FATAL] est le seul morceau
new component.

Toutes les coding changes côté viewer vivent sur la branche `viewer-juce`,
worktree `../ILLPAD_V2-viewer/`, conformément au CLAUDE.md projet "Branche
viewer-juce — exception au git workflow toujours main".

**Mise à jour spec viewer doc** : `firmware-viewer-protocol.md` est mis à
jour en fin de Phase 1 (sous-step 1.G) avec les nouveaux events. Pendant
Phase 1, le doc est temporairement désynchronisé du code — c'est attendu.

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
