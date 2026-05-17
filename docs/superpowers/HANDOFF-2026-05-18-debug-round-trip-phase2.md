# HANDOFF — Debug round trip viewer ↔ firmware Phase 2 (double log capture)

**Date émission** : 2026-05-18 (fin de session audit cross-source).
**À ouvrir dans** : nouvelle session Claude Code.
**Cwd recommandé** : `/Users/loic/Code/PROJECTS/ILLPAD_V2` (branche `main`, firmware).
Le code viewer est accessible en lecture via `/Users/loic/Code/PROJECTS/ILLPAD_V2-viewer/`. Pour modifier le viewer : `git -C /Users/loic/Code/PROJECTS/ILLPAD_V2-viewer` (branche `viewer-juce`, scope écriture `ILLPADViewer/**`).

---

## 1. Contexte (1 minute)

Phase 2 viewer bidirectionnel **CODE COMPLETE des deux côtés** :
- **Firmware** sur `main` : 14 commits (`d1de1f6` → `e181241`). Phase 2.A HW-validée (non-régression Tool 5/Tool 8) + G1 HW-validé (boot dump `[BANK_SETTINGS]` OK).
- **Viewer** sur `viewer-juce` : 9 commits (`c75b85c` → `a464670`). Parser + Model + CommandSender + UI (toggle ClockMode + 4 sliders ARPEG_GEN + ErrorToast + EventLogPanel split).

**Symptôme observé runtime** (cf commit viewer `984a000`) :
> TX visible côté viewer, aucun retour firmware (`[BANK_SETTINGS]` ou `[ERROR]`). Audit code-vs-code firmware/viewer ne révèle pas de bug structurel, donc on instrumente pour diff bytes envoyés vs reçus.

L'utilisateur clique le toggle/sliders viewer, le log TX confirme l'envoi (`Model::logTx`), mais le firmware ne répond pas avec les events attendus dans la fenêtre temporelle attendue.

## 2. Mission

Debug le round trip viewer ↔ firmware Phase 2 en utilisant les logs de capture pour :

1. **Confirmer ou réfuter B1 sur cas réel** (point de départ, cf §3).
2. **Identifier d'autres écarts** non détectés par l'audit code-vs-code (notamment sémantique runtime vs format strict, timing, encoding).
3. **Proposer un fix précis par finding**.

**Mode de session** : read-only au début (audit logs vs code), écriture seulement après autorisation explicite par finding. **Pas de patch préventif**.

## 3. Rapport d'audit cross-source 2026-05-18 (point de départ)

4 findings identifiés en lecture code-vs-code. **Le bug observé runtime est probablement (mais pas certainement) B1**.

### B1 — BLOQUANT — handleClockMode lowercase vs viewer parser uppercase

**Site firmware** : [`src/viewer/ViewerSerial.cpp:348`](../../src/viewer/ViewerSerial.cpp:348) (handler Phase 2 Task 15) :
```cpp
emitClockSource(s_clockManager.getActiveSourceLabel(), 0.0f);
```

**Source du label** : [`src/midi/ClockManager.cpp:269-277`](../../src/midi/ClockManager.cpp:269) :
```cpp
const char* ClockManager::getActiveSourceLabel() const {
  switch (_activeSource) {
    case SRC_USB:        return "usb";       // ← lowercase
    case SRC_BLE:        return "ble";       // ← lowercase
    case SRC_LAST_KNOWN: return "last";
    case SRC_INTERNAL:
    default:             return "internal";
  }
}
```

**Parser viewer** : `../ILLPAD_V2-viewer/ILLPADViewer/Source/serial/RuntimeParser.cpp:238-242` :
```cpp
if      (token == "USB")      c.source = ClockSource::USB;     // ← UPPERCASE attendu
else if (token == "BLE")      c.source = ClockSource::BLE;     // ← UPPERCASE attendu
else if (token == "internal") c.source = ClockSource::Internal;
else if (token == "last")     c.source = ClockSource::LastKnown;
else                          c.source = ClockSource::Internal;  // fallback silencieux
```

**Pattern firmware existant pré-Phase 2** : `ClockManager.cpp:110` émet `"USB"`/`"BLE"` **uppercase hardcoded**. Phase 2 a introduit `getActiveSourceLabel()` (lowercase) dans `handleClockMode` → discrepancy.

**Scenario qui déclenche le bug** :
- `!CLOCKMODE=master` : `setMasterMode(true)` force `_activeSource = SRC_INTERNAL` → label `"internal"` lowercase → viewer parser match `Internal` ✓ (pas de bug observable).
- `!CLOCKMODE=slave` AVEC source externe USB ou BLE active : `setMasterMode(false)` ne touche pas `_activeSource` (reste SRC_USB/SRC_BLE) → label `"usb"`/`"ble"` lowercase → viewer parser **fallback Internal** → badge clock source affiche stale `"internal"`.

**3 options de fix** :
- **A (firmware)** : modifier `getActiveSourceLabel()` pour retourner uppercase `"USB"`/`"BLE"` (cohérent avec pattern ClockManager.cpp:110) — risque : break `[GLOBALS] ClockSource=` qui utilise ce même label et où le viewer parse lowercase. À auditer.
- **B (firmware)** : ne pas utiliser `getActiveSourceLabel()` dans `handleClockMode`, émettre le label manuellement comme `ClockManager.cpp:110` (`_activeSource == SRC_USB ? "USB" : SRC_BLE ? "BLE" : ...`). Plus contained.
- **C (viewer)** : étendre le parser pour accepter aussi `"usb"`/`"ble"` lowercase. Forward-compat. Risque minimal.

### R1 — Runtime théorique — Tool 5 saveAll omet setLoadedBankType

**Site** : `src/setup/ToolBankConfig.cpp:200-208`. Le pattern `setLoadedX` est appliqué à tous les fields parallèles (`Quantize`, `ScaleGroup`, `BonusPile`, `MarginWalk`, `ProximityFactor`, `Ecart`) sauf `BankType`.

**Symptôme** : nul en pratique (setup mode → exit déclenche `ESP.restart()` → `loadAll()` recharge `_loadedBankType[]` depuis NVS). À fixer pour cohérence interne uniquement.

### I1 — Incohérence UX — sliders ArpGenStrip raw x10 vs spec décimal

**Site viewer** : `../ILLPAD_V2-viewer/ILLPADViewer/Source/ui/runtime/ArpGenStrip.cpp:40-43` :
```cpp
s.setRange ((double) rangeMin, (double) rangeMax, 1.0);  // 10..20 pour BONUS
s.setNumDecimalPlacesToDisplay (0);                       // ← 0 décimal
```
Spec viewer §3.8 dit : « BONUS range 10..20 (entier x10), **affichage formaté 1.0..2.0 (un décimal)** ». L'utilisateur voit la valeur firmware native (x10), pas la perception musicale documentée. UX, pas fonctionnel.

### M1 — Test viewer laxe — too_long

Test `../ILLPAD_V2-viewer/ILLPADViewer/Tests/test_RuntimeParser.cpp:573-583` attend cmd 24+3 chars (`!MARGIN=12 BANK=8 OVERFL...`). Firmware émet `%.20s...` = 20+3 chars. Le test passe mais ne reflète pas le format réel.

## 4. Outil de capture double log (viewer)

### 4.1 Localisation et commits

Le code de capture vit dans le **EventLogPanel** côté viewer — commits récents sur `viewer-juce` :
- `a0c0f97 debug(viewer): log TX commands in EventLog pour diag round trip` — ajoute `Model::logTx()` appelé par `CommandSender::pushLine`.
- `984a000 debug(viewer): split EventLogPanel parsed/raw + Save button + ASCII/HEX toggle` — split en 2 panneaux + bouton Save + toggle display HEX/ASCII.
- `a464670 debug(viewer): EventLogPanel Clear button — vide events + raw buffer` — bouton Clear.

Fichiers source : `../ILLPAD_V2-viewer/ILLPADViewer/Source/ui/runtime/EventLogPanel.{cpp,h}`.

### 4.2 Fichiers générés par Save

Bouton "Save" du panel viewer → 2 fichiers dans `/Users/loic/Documents/illpad-viewer-logs/` :
- `illpad-{ISO_TIMESTAMP}-parsed.log` — sémantique haute-niveau (events parsés via `Model::apply`, format `time | category | summary`).
- `illpad-{ISO_TIMESTAMP}-raw.log` — flux serial brut en **ASCII** (les bytes firmware sont déjà ASCII print).

**Les deux fichiers sont en ASCII** dans le filesystem. Le toggle HEX/ASCII du panel viewer est purement display (affichage runtime dans l'app JUCE), pas dans les fichiers sauvegardés.

### 4.3 Heuristique d'analyse

Workflow recommandé :

1. **parsed.log d'abord** — sémantique. Catégories à observer :
   - `TX` : commands envoyées par viewer (`!CLOCKMODE=...`, `!BONUS=...`, etc.).
   - `BANK_SETTINGS`, `SETTINGS`, `ERROR` : retours firmware attendus post-write.
   - `STATE`, `POT`, `CLOCK`, `ARP`, `GEN`, `MIDI` : flux runtime ambient.
   - **Cherche** : un `TX` immédiatement suivi (~10-50 ms) du retour firmware attendu. Si absent ou inattendu → discrepancy à investiguer.

2. **raw.log ensuite** — bytes serial bruts (ASCII print). Permet de voir le **format firmware exact** : encoding, terminator (`\n` vs `\r\n`), casing, espaces. Compare ce que tu attends (selon spec §6/§7) vs ce qui arrive.

3. **Hex de raw.log si besoin** — pour discriminer bytes invisibles (`\r\n` vs `\n`, espaces multiples, char non-ASCII em-dash `\xe2\x80\x94`, terminator manquant). Commande :
   ```bash
   xxd /Users/loic/Documents/illpad-viewer-logs/illpad-XXX-raw.log | less
   ```
   Ou demander à l'utilisateur de toggle HEX dans le viewer panel et copy/paste la vue.

## 5. Code firmware — sites émetteurs serial

### 5.1 Module central (Phase 1 viewer serial centralization, 2026-05-17)

**Tout passe par le module `src/viewer/ViewerSerial.{cpp,h}`** — créé en commit `fedeb81` (Phase 1.A 2026-05-17). 11 commits Phase 1 ont migré tous les call sites raw `Serial.print*` vers ce module.

Architecture (cf spec `docs/superpowers/specs/2026-05-17-viewer-serial-centralization-design.md`) :
- Queue FreeRTOS 32×256B sur Core 1 background task (`taskBody`).
- Dormance via atomic flag `s_viewerConnected` (drain silent quand viewer absent).
- Auto-resync à la transition `false→true` de `s_viewerConnected` (re-émet boot dump complet).
- API typed : fonctions `emit*` par event type (cf `ViewerSerial.h:30-78`).
- Boot dump explicit dans `src/main.cpp:646-651` (post-loadAll dans setup()) — distinct de l'auto-resync taskBody qui ne kick PAS quand Serial est déjà connecté au boot.

### 5.2 Émetteurs par event (tous dans `src/viewer/ViewerSerial.cpp`)

Sections :
- **Phase 1.C.1** `emitPot` — `[POT]` events
- **Phase 1.C.2** `emitBanksHeader/emitBank/emitState/emitReady/emitBankSwitch` — `[BANKS]/[BANK]/[STATE]/[READY]`
- **Phase 1.C.3** `emitArpNoteAdd/Remove/Play/Stop/QueueFull/GenSeed/GenSeedDegenerate` — `[ARP]/[GEN]`
- **Phase 1.C.4** `emitScale/emitArpOctave/emitArpGenMutation` — `[SCALE]/[ARP_GEN]`
- **Phase 1.C.5** `emitClockSource/emitMidiTransport` — `[CLOCK]/[MIDI]`
- **Phase 1.C.6** `emitPanic` — `[PANIC]`
- **Phase 1.D** `emitGlobals/emitSettings` — `[GLOBALS]/[SETTINGS]`
- **Phase 1.E** `emitClockBpm` — `[CLOCK] BPM=` debounced
- **Phase 2** `emitBankSettings` — `[BANK_SETTINGS]` (commit `e6420a3`)

### 5.3 Call sites principaux

- `src/main.cpp:646-651` — boot dump explicit (modifié par fix Task 11 `016f287` pour inclure `emitBankSettings`).
- `src/midi/ClockManager.cpp:110, 147, 168, 173, 187` — émissions `[CLOCK] Source:` natural sur transition source.
- `src/arp/ArpEngine.cpp` — events `[ARP] +note/-note/Play/Stop`, `[GEN] seed`.
- `src/managers/ScaleManager.cpp` — events `[SCALE]`.
- `src/midi/MidiTransport.cpp` — events `[MIDI] USB/BLE connected/disconnected`.

### 5.4 Logique Phase 2 récente (commits `d1de1f6` → `e181241`)

**Dispatcher write commands `!*` + handlers** dans `src/viewer/ViewerSerial.cpp` :
- Namespace anonyme ~lignes 38-50 : `enum ArpGenArg` + forward declarations `dispatchWriteCommand/handleClockMode/handleArpGenParam`.
- `dispatchWriteCommand` ~lignes 247-310 : parse `!KEY=VAL [BANK=K]` via `strtok_r` + dispatch par key.
- `handleClockMode` ~lignes 313-340 : parse master|slave + apply + emit `[SETTINGS]` puis `[CLOCK] Source:` (← **siège du finding B1**).
- `handleArpGenParam` ~lignes 342-440 : parse value + range + bank + type + apply setter + setLoadedX + queueBankTypeFromCache + emit `[BANK_SETTINGS]`.
- `pollCommands` lignes 275+ : input loop avec `cmdBuf[24]` + flag `s_cmdOverflow` + emit `[ERROR] code=too_long` sur overflow.

**Persistence NVS Phase 2** dans `src/managers/NvsManager.{cpp,h}` :
- Queue methods `queueSettingsWrite` / `queueBankTypeFromCache` (~lignes 410-425).
- Debounce 500ms checks **EN TÊTE** de `tickDebounce` (~lignes 225-240, renommée Task 8 commit `d4d3e54`).
- `saveSettings/saveBankType` (~lignes 1135-1180, fin de fichier).
- Cache `_loadedBankType[NUM_BANKS]` peuplé par `loadAll()` (~lignes 660, 678).

### 5.5 ClockManager (touché indirectement par B1)

`src/midi/ClockManager.cpp` :
- `setMasterMode(bool)` lignes 252-262 : drain ticks + force `_activeSource = SRC_INTERNAL` UNIQUEMENT si `master == true`. Cas `master == false` (slave) : ne touche pas `_activeSource`.
- `getActiveSourceLabel()` lignes 269-277 : retourne lowercase (`"usb"`, `"ble"`, `"last"`, `"internal"`) ← **suspect B1**.

## 6. Code viewer — sites réception et envoi

Worktree : `/Users/loic/Code/PROJECTS/ILLPAD_V2-viewer/`.

### 6.1 Réception (parser + Model)

- **Parser** : `ILLPADViewer/Source/serial/RuntimeParser.{cpp,h}`.
  - `parseRuntimeLine(std::string_view)` retourne `ParsedEvent` (variant).
  - **Suspect B1** lignes 230-247 : `[CLOCK] Source:` parsing case-sensitive (`"USB"`/`"BLE"` uppercase).
  - Phase 2 additions lignes 394-440 : `[BANK_SETTINGS]` et `[ERROR] cmd=` parsing.
- **Model** : `ILLPADViewer/Source/model/Model.{cpp,h}` + `model/BankInfo.h` + `model/DeviceState.h`.
  - `apply(ParsedEvent)` dispatch via `std::visit` (lignes 19-58).
  - Phase 2 handlers : `applyBankSettings` (Model.cpp:462-481), `applyError` (Model.cpp:483-505).
  - `applyClock` (Model.cpp:307-316) — utilise `device.clockSource = ClockSourceEnum::USB` etc selon parser.
  - `applyGlobals` (Model.cpp:362-389) — accepte lowercase `"usb"/"ble"/"internal"/"last"` (distinct du parser `[CLOCK] Source:` qui attend uppercase USB/BLE).

### 6.2 Envoi (TX path)

- **SerialReader** : `ILLPADViewer/Source/serial/SerialReader.{cpp,h}`.
  - `run()` (méthode thread JUCE) drain `OutputQueue` après chaque read iter (lignes 145-150). Pas de retry si `sp_nonblocking_write` partial (cf audit Y3, accepté hors scope).
- **OutputQueue** : `ILLPADViewer/Source/serial/OutputQueue.h` — thread-safe via `juce::ScopedLock`.
- **CommandSender** : `ILLPADViewer/Source/serial/CommandSender.{cpp,h}`.
  - `sendClockMode/Bonus/Margin/Prox/Ecart` formatent les commands via `snprintf "!KEY=%d BANK=%d"` + push outQueue.
  - Setup mode gate dans `pushLine` (cpp:38-46) — bloque envoi si `mode != Runtime`.
  - Log TX via `model.logTx` (cpp:50) — alimente parsed.log catégorie `TX`.

### 6.3 UI (déclencheurs commands)

- `ILLPADViewer/Source/ui/runtime/TopStripBar.cpp` — toggle ClockMode (lignes 38-52), `onClick` envoie `sender.sendClockMode(!currentIsMaster)`.
- `ILLPADViewer/Source/ui/runtime/ArpGenStrip.{cpp,h}` — 4 sliders ARPEG_GEN, `onDragEnd` send (lignes 58-66). Refresh from model post-send (no optimistic update, spec §7.4).
- `ILLPADViewer/Source/ui/ErrorToast.{cpp,h}` — toast UI sur `device.lastError` change.
- `ILLPADViewer/Source/ui/runtime/EventLogPanel.{cpp,h}` — outil double log (Save / Clear / HEX toggle).

## 7. Lectures recommandées en début de session

Ordre :
1. **Ce handoff** (vue d'ensemble) — déjà fait si tu le lis.
2. **Spec firmware Phase 2** : [`docs/superpowers/specs/2026-05-17-viewer-bidirectional-phase2-design.md`](specs/2026-05-17-viewer-bidirectional-phase2-design.md) (838 lignes — focus §6.1 cas `!CLOCKMODE` + §7 errors + §19 acceptations audit).
3. **Spec viewer Phase 2** : `/Users/loic/Code/PROJECTS/ILLPAD_V2-viewer/docs/2026-05-17-viewer-juce-phase2-bidirectional-spec.md` (757 lignes — focus §3.6 CommandSender + §7 vigilance).
4. **Protocol doc** : `/Users/loic/Code/PROJECTS/ILLPAD_V2-viewer/ILLPADViewer/docs/firmware-viewer-protocol.md` (focus §1.13 `[CLOCK]`, §1.22 `[BANK_SETTINGS]`, §1.23 `[ERROR]`, §7 write commands).
5. **STATUS.md** : focus courant + section "Viewer bidirectionnel Phase 2 firmware — historique commits".

Ne pas tout relire — pointer où chercher selon le finding.

## 8. Workflow suggéré nouvelle session

1. **Lecture handoff + audit findings** (ce doc).
2. **Demander à l'utilisateur les 2 fichiers de capture** (parsed.log + raw.log) de sa dernière session de test. Ou le timestamp si plusieurs captures.
3. **Lire parsed.log d'abord** — chercher les TX viewer + retours firmware attendus.
4. **Confirmation B1 spécifiquement** — filtrer les lignes `[CLOCK] Source:` dans raw.log :
   - Après un `!CLOCKMODE=slave` (visible dans TX), le firmware émet quoi exactement ?
   - Si `[CLOCK] Source: usb` ou `[CLOCK] Source: ble` lowercase → **B1 confirmé**.
   - Vérifier dans parsed.log que viewer parse `ClockEvent` avec `source == Internal` (fallback) ou pas du tout.
5. **Autres écarts** — pour chaque TX visible dans parsed.log, vérifier qu'un retour firmware existe dans raw.log dans les 100ms suivantes. Comparer format reçu vs attendu par parser viewer.
6. **Proposition fix par finding** — 2-3 options listées, mode read-only jusqu'à autorisation explicite.

## 9. Conventions de session

- **Mode** : read-only au début, écriture après autorisation explicite par finding.
- **Git workflow projet** : `main` toujours, autocommit ON par défaut (cf CLAUDE.md global), pas de Co-Authored-By.
- **Pour modifier viewer** : `git -C /Users/loic/Code/PROJECTS/ILLPAD_V2-viewer` sur branche `viewer-juce`, scope écriture `ILLPADViewer/**`.
- **Build firmware** : `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1` (pio pas dans PATH).
- **Spawn-task ouverte non liée** : bug Tool 7 PotMapping setup affiche stale values vs runtime (cf spawn-task créée 2026-05-17). Non couvert ici.

## 10. Estimation charge

Dépend du nombre de findings réels :
- **Min** : 1 finding B1 confirmé + fix 1 ligne firmware (option B) = ~30-45 min.
- **Médian** : B1 + 1-2 autres findings non-bloquants + fix multi-side (firmware + viewer + spec) = ~2-3h.
- **Max** : multiples findings runtime non détectés par audit + investigation timing/concurrency = ~4-6h sur plusieurs sessions.

---

**Fin du handoff**. La session debug doit pouvoir partir directement de ce doc, demander les logs à l'utilisateur dès le démarrage, et traiter B1 en priorité.
