# Viewer JUCE — Plan de pré-codage Phase 1 firmware viewer centralisation

**Date** : 2026-05-17
**Audience** : codeur viewer JUCE (ILLPAD_V2-viewer / branche `viewer-juce`).
**Worktree** : `../ILLPAD_V2-viewer/`.
**À lire avant** :
- [`docs/superpowers/specs/2026-05-17-viewer-serial-centralization-design.md`](../specs/2026-05-17-viewer-serial-centralization-design.md) — la spec firmware-centric complète. §24 décrit l'inventaire viewer en
  résumé.
- [`ILLPAD_V2-viewer/ILLPADViewer/docs/firmware-viewer-protocol.md`](../../../../ILLPAD_V2-viewer/ILLPADViewer/docs/firmware-viewer-protocol.md) — protocole actuel.

**Status** : pré-codage à exécuter **AVANT** le début de Phase 1.A firmware.
Pas de coordination synchrone — le viewer arrive prêt à recevoir tous les
nouveaux formats. Si un HW gate firmware échoue après ça, debug bilatéral.

**Estimation** : ~135 lignes de code JUCE total, ~4-6h dev incluant tests
Catch2 et smoke run.

---

## §1 — Contexte

Le firmware ILLPAD est en train d'être centralisé côté serial (un module
`ViewerSerial` qui gère queue + dormance + non-blocking + priorité). Cette
refonte introduit :

- Nouveaux events protocole : `[GLOBALS]`, `[SETTINGS]`, `[FATAL]`,
  `[CLOCK] BPM=`.
- Renaming des boot debug : `[INIT] *` → `[BOOT] *`, `[KB] *` → `[BOOT KB] *`,
  `[NVS] *` (boot only) → `[BOOT NVS] *`, etc.
- Nouveau marker setup mode : `[SETUP] Entering setup mode...`.
- Comportement transport : non-blocking USB CDC, dormance quand viewer absent,
  auto-resync à la reconnection.

Côté viewer, il faut :
1. Updater `ModeDetector` pour reconnaître `[BOOT] Ready.` (rename de
   `[INIT] Ready.`).
2. Ajouter parser branches pour les nouveaux events.
3. Hydrater le `Model` avec les nouvelles données.
4. Gérer les transitions setup mode / CDC disconnect proprement.
5. Optionnel : UI panel pour les `[BOOT *]` events.
6. Nouvel overlay critique pour `[FATAL]`.

**Aucune compat backward firmware-side n'est garantie** — le firmware Phase 1
émet seulement les nouveaux formats. Le viewer doit être prêt avant.

---

## §2 — Format de référence des nouveaux events

Les exemples ci-dessous sont les sorties firmware exactes à parser. Forme
générale : ligne ASCII terminée par `\n`, prefix entre crochets, key=value
ordre libre.

### 2.1 — `[GLOBALS]` — params device-wide runtime

```
[GLOBALS] Tempo=120 LED_Bright=200 PadSens=15 ClockSource=internal
```

Fields :
- `Tempo` : uint16, 40-300 (BPM consigne pot Tempo, pas la PLL externe)
- `LED_Bright` : uint8, 0-255
- `PadSens` : uint8, 5-30
- `ClockSource` : enum string `{internal, usb, ble, last}`

**Forward-compat** : si firmware ajoute des keys nouvelles (`Shape=`, `Slew=`,
etc. en Phase 2), le parser viewer doit ignorer silencieusement les keys
inconnues — pas d'erreur, pas de log. Implémentation : itérer sur les tokens
`<key>=<val>`, dispatcher uniquement les keys connues.

**Émis par firmware** :
- À la fin du boot dump (après les 8× `[STATE]`).
- À chaque réponse `?BOTH` / `?ALL`.
- Au post-connect auto-resync (transition viewer-disconnected → connected).

### 2.2 — `[SETTINGS]` — config NVS persistante

```
[SETTINGS] ClockMode=slave PanicReconnect=1 DoubleTapMs=400 AftertouchRate=10 BleInterval=2 BatAdcFull=4095
```

Fields :
- `ClockMode` : enum string `{slave, master}` (master = firmware émet MIDI
  clock sur USB/BLE ; slave = firmware reçoit MIDI clock)
- `PanicReconnect` : uint8 `{0, 1}` (envoyer All-Notes-Off à la reconnexion BLE)
- `DoubleTapMs` : uint16 (fenêtre double-tap pour Play/Stop, ms)
- `AftertouchRate` : uint8 (taux de poly aftertouch, Hz par pad)
- `BleInterval` : uint8 enum `{0=OFF, 1=LOW, 2=NORMAL, 3=SAVER}` (BLE
  connection interval)
- `BatAdcFull` : uint16 0-4095 (calibration tension batterie pleine)

Même forward-compat que `[GLOBALS]`.

**Émis par firmware** : même points que `[GLOBALS]`.

### 2.3 — `[FATAL]` — erreur critique boot, always-on

```
[FATAL] Keyboard init failed!
[FATAL] NVS corrupted at <key>
```

Format : `[FATAL] <human message>`. Pas de structure interne. Texte libre.

**Émis par firmware** : UNGATED (même DEBUG_SERIAL=0). Dernier message avant
LED error blink loop. Le firmware ne sortira plus rien après.

Côté viewer : afficher overlay critique persistant rouge. Texte = la portion
après `[FATAL] `. Ne pas confondre avec `[PANIC]` (qui dure 2.5s).

### 2.4 — `[CLOCK] BPM=` — BPM externe émis sur change

```
[CLOCK] BPM=145 src=usb
```

Fields :
- `BPM` : float (PLL stabilisée, valeur réelle de l'horloge externe)
- `src` : enum string `{usb, ble}` (source de l'horloge externe)

Émis quand le PLL ClockManager observe un change ±1 BPM debounced.
**Distingué de `[CLOCK] Source: ...`** qui est émis sur transition source,
pas sur change BPM dans une source stable.

**Émis par firmware** : runtime, à chaque change BPM > 1 BPM (debounce).

### 2.5 — `[BOOT *]` — boot debug informational

Tous les events émis pendant `setup()` avant `[BOOT] Ready.` portent un préfixe
`[BOOT]` ou `[BOOT <subsystem>]` :

```
[BOOT] === ILLPAD48 V2 ===
[BOOT] I2C OK.
[BOOT KB] Starting capacitive keyboard init...
[BOOT KB] Autoconfig complete.
[BOOT] Keyboard OK.
[BOOT POT] Seed 0: median=2048
[BOOT POT] MCP3208 boot OK.
[BOOT] Hold rear button to enter setup mode...
[BOOT MIDI] USB MIDI initialized.
[BOOT MIDI] BLE MIDI initialized.
[BOOT] MIDI Transport OK.
[CLOCK] ClockManager initialized (internal clock).      ← reste [CLOCK], parser-recognized
[BOOT] ClockManager OK.
[BOOT] MIDI Engine OK.
[BOOT NVS] Bank loaded: 3
...
[BOOT NVS] Pot mapping loaded.
[BOOT POT] Rebuilt 16 bindings from mapping (4 CC slots)
[BOOT POT] 16 bindings, 5 pots initialized
[BOOT] PotFilter + PotRouter OK.
...
[BOOT] Ready.                                            ← marker ModeDetector
[BANKS] count=8                                          ← protocole runtime à partir d'ici
...
```

**Côté viewer** : optionnellement parsé et affiché dans un "boot log panel".
Si non parsé, les lignes restent `UnknownEvent` — pas de régression
fonctionnelle. Mais l'utilisateur perd le diagnostic visuel "où ça a planté
si boot fail".

### 2.6 — `[SETUP]` — marker entrée setup mode (inchangé)

```
[SETUP] Entering setup mode...
```

Émis JUSTE AVANT que le firmware bascule en mode VT100 cockpit (escape
sequences ANSI). À partir de ce point, **le canal serial est un terminal
VT100**, plus du protocole.

**Côté viewer** : voir §3.5 ci-dessous (freeze Model + UI dédié + CDC reset
au reboot post-setup-mode-exit).

---

## §3 — Inventaire des changements viewer

Liste ordonnée par priorité d'exécution (1 = à faire en premier).

### 3.1 — ModeDetector : update marker `[BOOT] Ready.`

**Fichier** : à localiser dans `Source/serial/ModeDetector.{cpp,h}` ou
équivalent (~1 fichier).

**Change** : la chaîne littérale `[INIT] Ready.` devient `[BOOT] Ready.`.
1 ligne.

**Avant** :
```cpp
constexpr const char* RUNTIME_MARKER = "[INIT] Ready.";
```

**Après** :
```cpp
constexpr const char* RUNTIME_MARKER = "[BOOT] Ready.";
```

**Test** : ajouter un cas Catch2 qui injecte les nouveaux `[BOOT *]` lines +
`[BOOT] Ready.` et vérifie que ModeDetector détecte la transition runtime.

**Impact si non fait** : viewer reste indéfiniment en boot mode après firmware
Phase 1.B. Pas de hydratation runtime.

### 3.2 — Parser : nouveau branch `[GLOBALS]`

**Fichier** : `Source/serial/RuntimeParser.cpp` (~10 lignes).

**Branch parser** :
```cpp
if (line.startsWith("[GLOBALS] ")) {
  return parseGlobals(line.substring(10));
}
```

**Parser fonction** : itère sur les tokens `key=value`, remplit une struct
`GlobalsEvent`. Forward-compat = ignore les keys inconnues.

```cpp
struct GlobalsEvent {
  std::optional<uint16_t> tempoBpm;
  std::optional<uint8_t>  ledBrightness;
  std::optional<uint8_t>  padSensitivity;
  std::optional<std::string> clockSource;
};

GlobalsEvent parseGlobals(const String& body) {
  GlobalsEvent ev;
  for (auto& token : tokenize(body, ' ')) {
    auto eq = token.indexOf('=');
    if (eq < 0) continue;
    String key = token.substring(0, eq);
    String val = token.substring(eq + 1);
    if (key == "Tempo")        ev.tempoBpm = val.getIntValue();
    else if (key == "LED_Bright") ev.ledBrightness = val.getIntValue();
    else if (key == "PadSens")    ev.padSensitivity = val.getIntValue();
    else if (key == "ClockSource") ev.clockSource = val.toStdString();
    // unknown keys = silently ignored (forward-compat)
  }
  return ev;
}
```

**Test** : Catch2 cases golden / forward-compat (extra keys) / malformed
(missing `=`).

### 3.3 — Parser : nouveau branch `[SETTINGS]`

**Fichier** : `Source/serial/RuntimeParser.cpp` (~15 lignes).

Analogue à `[GLOBALS]`. Struct :

```cpp
struct SettingsEvent {
  std::optional<std::string> clockMode;   // "slave" | "master"
  std::optional<uint8_t>     panicReconnect;
  std::optional<uint16_t>    doubleTapMs;
  std::optional<uint8_t>     aftertouchRate;
  std::optional<uint8_t>     bleInterval;
  std::optional<uint16_t>    batAdcFull;
};
```

Même structure de parsing. Même forward-compat.

**Test** : idem `[GLOBALS]`.

### 3.4 — Parser : nouveau branch `[FATAL]`

**Fichier** : `Source/serial/RuntimeParser.cpp` (~5 lignes).

```cpp
if (line.startsWith("[FATAL] ")) {
  FatalEvent ev{line.substring(8).toStdString()};
  return ev;
}
```

Struct simple :
```cpp
struct FatalEvent {
  std::string message;
};
```

**Test** : Catch2 cas `"[FATAL] Keyboard init failed!"` → `ev.message ==
"Keyboard init failed!"`.

### 3.5 — Parser : nouveau branch `[CLOCK] BPM=`

**Fichier** : `Source/serial/RuntimeParser.cpp` (~5 lignes).

**Distingué de `[CLOCK] Source: ...`** par le keyword suivant : `BPM=` vs
`Source:`.

```cpp
if (line.startsWith("[CLOCK] BPM=")) {
  // Format: [CLOCK] BPM=145.0 src=usb
  ClockBpmEvent ev;
  // ... parse ev.bpm, ev.source
  return ev;
}
```

Struct :
```cpp
struct ClockBpmEvent {
  float bpm;
  std::string source;  // "usb" | "ble"
};
```

**Test** : Catch2 cas + cas BPM décimal (`BPM=120.5`) + cas missing src.

### 3.6 — Parser : nouveau branch `[SETUP]` mode marker

**Fichier** : `Source/serial/RuntimeParser.cpp` (~5 lignes).

```cpp
if (line.startsWith("[SETUP] ")) {
  return SetupModeEnterEvent{};
}
```

Pas de fields — c'est un signal de transition. Le viewer Model doit freezer
sur réception (§3.10 ci-dessous).

**Test** : Catch2 cas string complète.

### 3.7 — Parser : optionnel — branch `[BOOT *]` boot log

**Fichier** : `Source/serial/RuntimeParser.cpp` (~10 lignes) + UI panel ~30
lignes.

```cpp
if (line.startsWith("[BOOT")) {  // [BOOT], [BOOT KB], [BOOT NVS], etc.
  BootLogEvent ev{line.toStdString()};
  return ev;
}
```

**Optionnel** : tu peux skipper ça pour Phase 1 et y revenir plus tard. Sans
parsing, les `[BOOT *]` restent UnknownEvent (= comportement actuel pour
`[INIT] *`, `[KB] *`, `[NVS] *`). Le viewer hydrate quand même via le boot
dump qui suit `[BOOT] Ready.`.

**Si tu le fais** : nouveau panel UI "Boot log" qui affiche les `BootLogEvent`
dans l'ordre, collapse au reçu de `[READY]`.

### 3.8 — Model : `applyGlobals(GlobalsEvent)`

**Fichier** : `Source/model/Model.cpp` (~10 lignes).

```cpp
void Model::applyGlobals(const GlobalsEvent& ev) {
  if (ev.tempoBpm)         device.tempoBpm = *ev.tempoBpm;
  if (ev.ledBrightness)    device.ledBrightness = *ev.ledBrightness;
  if (ev.padSensitivity)   device.padSensitivity = *ev.padSensitivity;
  if (ev.clockSource)      device.clockSource = parseClockSource(*ev.clockSource);
  // notifier les listeners UI...
}
```

### 3.9 — Model : `applySettings(SettingsEvent)`

**Fichier** : `Source/model/Model.cpp` (~10 lignes).

Analogue à `applyGlobals`. Stocker dans `device.settings.*` (nouveau sous-
struct si nécessaire). Le UI Phase 2 affichera ces valeurs.

**Pour Phase 1** : suffit de stocker, pas besoin de UI dédié — le user voit
`ClockMode=slave` etc. dans le log si log activé. Phase 2 ajoutera l'affichage
+ commands write.

### 3.10 — Model : `applySetupModeEnter()` — freeze

**Fichier** : `Source/model/Model.cpp` (~5 lignes).

```cpp
void Model::applySetupModeEnter() {
  device.mode = DeviceMode::SetupMode;
  // freeze all subsequent updates: parser doit checker device.mode avant
  // d'apply quoi que ce soit. Sinon les VT100 garbage corrompent le state.
}
```

Côté parser : ajouter early-return si `device.mode == SetupMode`.

```cpp
void RuntimeParser::dispatch(const Event& ev) {
  if (model.device.mode == DeviceMode::SetupMode &&
      !std::holds_alternative<CdcDisconnectEvent>(ev)) {
    // En setup mode, ignorer tous les events sauf la déconnexion CDC
    return;
  }
  // ... dispatch normal
}
```

### 3.11 — Model : `applyFatal(FatalEvent)`

**Fichier** : `Source/model/Model.cpp` (~3 lignes).

```cpp
void Model::applyFatal(const FatalEvent& ev) {
  device.fatalMessage = ev.message;
  device.fatalActive = true;
  // déclencher l'overlay UI
}
```

### 3.12 — Model : suppression du slot-scanning brittle dans `applyState`

**Fichier** : `Source/model/Model.cpp` (~15 lignes nettes supprimées).

**Contexte** : aujourd'hui, `Model::applyState` (lignes 80-100 environ) scanne
les 8 slots de la `[STATE]` line pour détecter si un Tempo / LED_Bright /
PadSens y est exposé, et hydrate `device.*` depuis ces valeurs. Cette logique
est brittle (dépend du user mapping).

**Avec `[GLOBALS]`** : la valeur arrive direct via `applyGlobals`. La logique
slot-scanning devient redondante.

**Action** : supprimer les blocs `if (slot.target == TARGET_TEMPO_BPM) {
device.tempoBpm = ...; }` (et équivalents pour LED_Bright / PadSens) dans
`applyState`. ~15 lignes nettes en moins.

### 3.13 — UI : nouveau overlay critique `[FATAL]`

**Fichier** : nouveau component JUCE, ex. `Source/ui/FatalOverlay.{cpp,h}`
(~30 lignes).

**Comportement** :
- Activé quand `model.device.fatalActive == true`.
- Overlay full-window rouge avec le texte `model.device.fatalMessage`.
- Persistant — pas de timeout (différent de PANIC qui dure 2.5s).
- Le user doit reboot le firmware physiquement pour clear.

**Style** : grosse police, rouge vif, fond semi-transparent.

### 3.14 — UI : state `setup mode active` dans le header

**Fichier** : header existant (~3 lignes).

Quand `model.device.mode == SetupMode`, afficher un badge dans le header :
"FIRMWARE IN SETUP MODE" (texte stable, pas blink) en plus du reste.

### 3.15 — Détection CDC disconnect → reset Model

**Fichier** : `Source/serial/SerialReader.cpp` ou équivalent (~10 lignes).

**Comportement** : JUCE `SerialPort::read()` retourne -1 ou exception quand
le port est fermé/disconnecté côté hardware (reboot firmware OU câble
débranché OU app firmware-side fermé le CDC).

**Action à la détection** :
```cpp
void SerialReader::onDisconnect() {
  model.reset();                       // wipe tout le state
  model.device.mode = DeviceMode::WaitingForFirmware;
  // l'UI affiche "Waiting for firmware"
  scheduleReconnectAttempt();          // si tu as auto-reconnect
}
```

À la reconnexion, le parser recevra le boot dump complet :
`[BOOT *]` × N → `[BOOT] Ready.` → `[BANKS]` → `[BANK]` × 8 → `[STATE]` × 8 →
`[GLOBALS]` → `[SETTINGS]` → `[READY]`.

Et le model.device.mode passera de `WaitingForFirmware` à `Runtime` à la
réception de `[BOOT] Ready.` via le ModeDetector.

---

## §4 — Tests Catch2 à ajouter

**Fichier** : `ILLPADViewer/Tests/test_RuntimeParser.cpp` (suivre conventions
existantes du projet).

Cas à couvrir, 1 par section §3.2-3.6 :

| Test case | Format input | Vérification |
|---|---|---|
| `parse globals minimum 4 keys` | `[GLOBALS] Tempo=120 LED_Bright=200 PadSens=15 ClockSource=internal` | tous les 4 fields populés correctement |
| `parse globals forward-compat unknown keys` | `[GLOBALS] Tempo=120 NewKey=42 LED_Bright=200` | Tempo + LED_Bright populés, NewKey ignoré sans erreur |
| `parse globals malformed (missing =)` | `[GLOBALS] Tempo120 LED_Bright=200` | Tempo absent, LED_Bright populé |
| `parse settings ClockMode slave/master` | `[SETTINGS] ClockMode=master` | clockMode == "master" |
| `parse fatal text libre` | `[FATAL] Keyboard init failed!` | message == "Keyboard init failed!" |
| `parse clock bpm float + src` | `[CLOCK] BPM=145.5 src=usb` | bpm == 145.5f, source == "usb" |
| `parse clock bpm distinct de Source` | `[CLOCK] Source: USB` puis `[CLOCK] BPM=120 src=usb` | 1er = ClockSourceEvent, 2e = ClockBpmEvent (pas de confusion) |
| `parse setup mode marker` | `[SETUP] Entering setup mode...` | SetupModeEnterEvent dispatched |
| `parse boot log line` (si §3.7 implémenté) | `[BOOT KB] Autoconfig complete.` | BootLogEvent avec raw == ligne complète |
| `ModeDetector transition` | `[BOOT] Ready.` après séquence `[BOOT *]` | détection transition runtime |

**Test setup mode freeze** : injecter `[SETUP] Entering...` puis simuler des
events random suivants → vérifier que `model.device.mode == SetupMode` reste
stable et les events runtime ne sont pas appliqués.

**Test CDC disconnect** : simuler une lecture qui throw / retourne -1 →
vérifier que `model.reset()` est appelé.

---

## §5 — Sequencing recommandé

Suivre ce process :

1. **3.1 ModeDetector** (1 ligne) — sinon le viewer ne reconnaît plus le
   marker runtime. À faire en premier.
2. **3.2-3.6 Parser branches** — chacun indépendant, ajout par ordre de
   simplicité : `[FATAL]` (3.4), `[SETUP]` (3.6), puis `[GLOBALS]` (3.2),
   `[SETTINGS]` (3.3), `[CLOCK] BPM=` (3.5).
3. **3.7 Boot log** — optionnel, peut être skip pour Phase 1.
4. **3.8-3.12 Model handlers** — un par event. 3.12 (slot-scan removal)
   à faire APRÈS 3.8 (applyGlobals) pour ne pas casser l'hydratation
   pendant la transition.
5. **3.13-3.14 UI** — `[FATAL]` overlay + setup mode badge header.
6. **3.15 CDC disconnect** — orthogonal, peut être fait à n'importe quel
   moment.
7. **Tests** — ajouter en parallèle des changes (TDD si possible).

**Smoke test final** : à la fin de tout le précodage, faire un build complet
+ run le viewer sur le firmware AVANT Phase 1.A (= firmware actuel main). Le
viewer doit toujours fonctionner exactement comme avant — les nouveaux
branches parser ne sont jamais déclenchés car le firmware n'émet pas encore
ces formats. Si régression, c'est un bug dans le précode.

---

## §6 — Hors scope (pas pour Phase 1 précode)

- **Phase 2 write commands** (`!CLOCKMODE=slave` etc.) : ajouter le UI viewer
  pour push commands sera fait en Phase 2 séparée. Le precode Phase 1
  prépare la lecture (`[GLOBALS]`, `[SETTINGS]`), pas l'écriture.
- **Phase 2 events `[BANK_SETTINGS]` ou extension `[STATE]`** pour ARPEG_GEN
  per-bank : à designer en Phase 2.
- **`[ERROR] code=...`** event pour Phase 2 write command failure : pas en
  Phase 1.
- **Refonte de l'UI** pour afficher tous les `[SETTINGS]` fields : Phase 1
  parse + stocke, Phase 2 affichera. Pas de UI Settings panel à coder
  maintenant.
- **Refactor du parser** vers state machine ou autres patterns : hors
  scope, reste sur l'architecture if/else-chain actuelle.
- **Tests Catch2 pour les events existants** : pas touché.

---

## §7 — Points de vigilance

- **Préserver le format ASCII line-based exact** : pas de tolerance sur les
  espaces (un seul espace entre tokens), pas de trailing whitespace, `\n`
  line terminator. Les tests Catch2 doivent couvrir le cas exact que le
  firmware émet.
- **Pas de toucher au parser existant** sauf 3.1 (ModeDetector) et 3.12
  (Model::applyState slot-scan removal). Tous les autres event types
  ([BANK], [STATE], [POT], [SCALE], [ARP], [CLOCK Source:], [MIDI], [PANIC],
  [READY]) restent inchangés en Phase 1.
- **forward-compat key=value** : crucial. Si firmware Phase 2 ajoute des
  keys, le viewer Phase 1 doit les ignorer silencieusement, pas crasher ou
  loger erreur. Test explicite à inclure.
- **`device.mode = SetupMode` freeze** : doit court-circuiter TOUTES les
  application Model des events sauf CDC disconnect. Sinon corruption de
  state via VT100 garbage qui serait parser-misinterpreté.

---

## §8 — Cross-référence firmware

Pendant que tu codes ces changes viewer, le firmware sera en parallèle codé
sur la branche `main` selon le plan d'implémentation Phase 1 (1.A → 1.G) à
écrire ensuite ([`docs/superpowers/specs/2026-05-17-viewer-serial-centralization-design.md`](../specs/2026-05-17-viewer-serial-centralization-design.md) §18).

Les sous-phases firmware qui déclenchent un HW gate dépendant du viewer :

| Phase firmware | Test depend de quel changement viewer |
|---|---|
| 1.A plomberie | Aucun — viewer doit continuer à marcher comme avant |
| 1.B boot tagging | **3.1 ModeDetector** (transition runtime) + optionnel 3.7 |
| 1.C migration | Aucun — format identique |
| 1.D `[GLOBALS]`/`[SETTINGS]` | **3.2 + 3.3 + 3.8 + 3.9 + 3.12** (hydratation propre) |
| 1.E `[CLOCK] BPM=` | **3.5** (parser) + handler Model si tu veux afficher la BPM externe |
| 1.F `[FATAL]` | **3.4 + 3.11 + 3.13** (overlay) |
| 1.G sync doc | Pas viewer-side, juste update `firmware-viewer-protocol.md` |

Si un sous-step firmware échoue son HW gate, vérifier d'abord que le code
viewer correspondant est bien en place et compilé.

---

**Fin du doc**. Pour questions / clarifications : référer à la spec firmware
ou ouvrir une nouvelle session de brainstorm.
