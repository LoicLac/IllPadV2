# Bug Report — 2026-03-29

Session de stress-test : 3 agents (LedController, Setup Tools 1-7, NvsManager+PotRouter).
Périmètre : code non commité (21 fichiers modifiés, +241/-101 lignes depuis commit `510cad9`).

---

## Bugs runtime

### BUG-01 — `_lastToolName` dangling pointer : flash animation lit du stack libéré
- **Sévérité** : CRITICAL
- **Difficulté** : FACILE (~10 lignes)
- **Fichiers** : `src/setup/SetupUI.h:176` + 4 tools
- **What** : `SetupUI::_lastToolName` est un `const char*` brut. Plusieurs tools passent un `char[]` alloué sur la stack locale de leur bloc de rendu à `drawConsoleHeader()`. Quand `flashSaved()` est appelé ultérieurement (hors du bloc de rendu), `_lastToolName` pointe vers de la mémoire libérée. Comportement indéfini : le header de l'animation flash peut afficher du garbage.
- **Tools affectés** :
  - Tool 2 `ToolPadOrdering.cpp:237` — `char info[32]` dans le bloc `ORD_MEASUREMENT`, `flashSaved()` appelé ligne 303
  - Tool 4 `ToolBankConfig.cpp:236` — `char headerRight[32]` dans le bloc `if (screenDirty)`, `flashSaved()` appelé lignes 127, 201, 220
  - Tool 6 `ToolPotMapping.cpp:480` — `char headerText[48]` dans `drawScreen()`, `flashSaved()` appelé lignes 204, 226, 588, 611, 652
  - Tool 7 `ToolLedSettings.cpp:661` — `char headerBuf[64]` dans le bloc `if (screenDirty)`, `flashSaved()` appelé lignes 585, 647
- **Trigger** : Dans l'un des 4 tools, effectuer une modification et appuyer sur Entrée pour sauvegarder. L'animation flash de 300ms lit la mémoire du stack frame libéré.
- **Fix hint** : Changer `_lastToolName` de `const char*` en `char _lastToolName[64]` dans `SetupUI.h` et `strncpy` dans `drawConsoleHeader()`.

---

### BUG-02 — Division par zéro sur blink counts (scale root/mode/chrom/octave)
- **Sévérité** : MEDIUM
- **Difficulté** : TRIVIAL (~4 lignes)
- **Fichier** : `src/core/LedController.cpp:358, 372, 386, 505`
- **What** : `loadLedSettings()` charge `scaleRootBlinks`, `scaleModeBlinks`, `scaleChromBlinks`, `octaveBlinks` sans guard contre zéro. Ces valeurs sont utilisées comme diviseurs dans `_durationMs / (blinks * 2)`. Si un count est 0, division par zéro → hardware exception ESP32-S3 (crash). Le champ `_bankBlinks` est lui protégé (ligne 740 : `(s.bankBlinks > 0) ? s.bankBlinks : 3`) mais pas les quatre autres.
- **Trigger** : NVS corrompue avec magic/version valides mais un blink count à 0. Prochain changement de scale root/mode/chromatic ou d'octave déclenche le crash. Non atteignable via Tool 7 (min 1 enforced), mais atteignable par bit-flip NVS.
- **Fix hint** : Appliquer le même pattern que `_bankBlinks` aux 4 champs non gardés dans `loadLedSettings()`.

---

### BUG-03 — Tool 4 : cancel-edit restaure depuis le runtime au lieu du NVS chargé
- **Sévérité** : MEDIUM
- **Difficulté** : FACILE (~10 lignes)
- **Fichier** : `src/setup/ToolBankConfig.cpp:143`
- **What** : Sur 'q' (cancel), `wkTypes[cursor]` est restauré depuis `_banks[cursor].type` (runtime BankSlot). Or le tool charge ses données depuis NVS en début de session (lignes 71-93) précisément parce que le setup peut démarrer avant `NvsManager::loadAll()`. Si le setup est ouvert avant `loadAll`, `_banks[cursor].type` contient les defaults power-on, pas les données NVS. Annuler un edit écrase la copie de travail NVS avec la valeur par défaut.
- **Trigger** : Sauvegarder une config non-default (ex : bank 2 en ARPEG), rebooter, entrer en setup rapidement. Ouvrir Tool 4 — affichage correct (ARPEG). Commencer à éditer bank 2, appuyer 'q' pour annuler. Bank 2 revient à NORMAL au lieu d'ARPEG.
- **Fix hint** : Sauvegarder une copie des `wkTypes[]`/`wkQuantize[]` chargés depuis NVS à l'initialisation et restaurer depuis cette copie sur cancel (comme `SettingsStore original = wk` dans Tool 5).

---

### BUG-04 — `nvsSaved` hardcodé `true` à l'entrée (Tools 5, 6, 7)
- **Sévérité** : MEDIUM
- **Difficulté** : TRIVIAL (~6 lignes)
- **Fichiers** : `src/setup/ToolSettings.cpp:204`, `src/setup/ToolPotMapping.cpp:547`, `src/setup/ToolLedSettings.cpp:562`
- **What** : Dans les 3 tools, le flag `nvsSaved` / `_nvsSaved` est mis à `true` inconditionnellement à l'entrée, sans vérifier si le load NVS a effectivement trouvé des données valides. Sur un device neuf (NVS vide), le header affiche `NVS:OK` à tort.
- **Trigger** : Ouvrir Tool 5, 6 ou 7 sur un device factory-fresh. Le badge NVS affiche `NVS:OK` malgré l'absence de données sauvegardées.
- **Fix hint** : Initialiser le flag à `false` et le mettre à `true` uniquement si le load NVS retourne des données avec magic + version corrects.

---

### BUG-05 — `flashSaved()` affiche le badge NVS du render précédent au 1er save
- **Sévérité** : LOW
- **Difficulté** : FACILE (~5 lignes)
- **Fichier** : `src/setup/SetupUI.cpp:220`
- **What** : `flashSaved()` utilise `_lastNvsSaved` copié lors du dernier `drawConsoleHeader()`. Les tools appellent `flashSaved()` après avoir mis à jour `nvsSaved` mais avant le prochain render. Au 1er save d'une session (où `nvsSaved` était `false`), l'animation de 300ms clignote avec le badge `NVS:--`, puis le render suivant affiche `NVS:OK`.
- **Trigger** : Entrer dans un tool sur device vierge (badge `NVS:--`), configurer quelque chose, sauvegarder. L'animation flash montre `NVS:--` pendant 300ms avant de passer à `NVS:OK`.
- **Fix hint** : Mettre à jour `_lastNvsSaved` avant d'appeler `flashSaved()`, ou ajouter un helper `setNvsSaved(bool)` que les tools appellent avant le flash.

---

### BUG-06 — Dernier beat flash de `CONFIRM_PLAY` tronqué (~1ms au lieu de 30ms)
- **Sévérité** : LOW
- **Difficulté** : MOYEN (~15 lignes)
- **Fichier** : `src/core/LedController.cpp:456-460`
- **What** : Quand le dernier beat fire dans le path clock-synced, `_playFlashPhase` est incrémenté et comparé à `_playBeatCount` dans la même frame. Si égal, `_confirmType = CONFIRM_NONE` est assigné immédiatement. `_fadeStartTime = now` vient d'être set, mais dès la prochaine itération de loop, tout le bloc confirmation est skipé. Le dernier flash est visible ~1ms (une itération) au lieu de `_tickFlashDurationMs` (défaut 30ms).
- **Trigger** : Lancer un arp avec clock source externe. Le flash du dernier beat est imperceptible. Avec `_playBeatCount = 1`, le seul beat flash est quasi-invisible.
- **Fix hint** : Ne pas assigner `CONFIRM_NONE` immédiatement. Poser un flag `_playEnding = true` et effacer la confirmation seulement quand le timer `_tickFlashDurationMs` expire.

---

### BUG-07 — Quantification sévère du sine pulse à bas `pulsePeriodMs`
- **Sévérité** : LOW
- **Difficulté** : FACILE (~3 lignes)
- **Fichier** : `src/core/LedController.cpp:560`
- **What** : Le step LUT est calculé par `uint8_t lutStep = _pulsePeriodMs / 256` (division entière). Pour `_pulsePeriodMs = 500` (minimum Tool 7), `lutStep = 1`, période effective = 256ms au lieu de 500ms (erreur 48.8%). Toutes les valeurs 500-511ms donnent la même période de 256ms. L'animation respire presque deux fois plus vite qu'attendu.
- **Trigger** : Régler Pulse Period à 500ms dans Tool 7. L'animation est visiblement trop rapide, et les valeurs 500 et 512 donnent des vitesses dramatiquement différentes.
- **Fix hint** : Remplacer par `uint8_t sineIdx = (uint8_t)((uint32_t)(now % _pulsePeriodMs) * 256 / _pulsePeriodMs);` pour un mapping direct sans quantification.

---

### BUG-08 — Tool 3 : badge NVS basé sur longueur données, pas validité magic/version
- **Sévérité** : LOW
- **Difficulté** : TRIVIAL (~2 lignes)
- **Fichier** : `src/setup/ToolPadRoles.cpp:569`
- **What** : `_nvsSaved = (len > 0)` est assigné avant la vérification magic/version (ligne 573). Si NVS contient des données d'une ancienne version firmware (magic ou version différents), les données sont rejetées mais le badge affiche `NVS:OK`.
- **Trigger** : Mise à jour firmware avec `BANKPAD_VERSION` incrémenté. Entrer Tool 3 : badge `NVS:OK` mais les rôles sont les defaults (anciennes données rejetées).
- **Fix hint** : Déplacer `_nvsSaved = true` à l'intérieur du `if (bps.magic == EEPROM_MAGIC && bps.version == BANKPAD_VERSION)`.

---

### BUG-09 — `ArpPotStore` : `octaveRange` et `shuffleTemplate` non validés au load NVS
- **Sévérité** : LOW
- **Difficulté** : TRIVIAL (~4 lignes)
- **Fichier** : `src/managers/NvsManager.cpp:500-505`
- **What** : Le load de `ArpPotStore` valide `division` et `pattern` contre leurs plages enum, mais pas `octaveRange` (valide : 1-4) ni `shuffleTemplate` (valide : 0-4). Un `shuffleTemplate >= 5` corrompu passe dans `_pendingArpPot[i]` et est seedé dans PotRouter via `loadStoredPerBank()`, créant un décalage catch entre PotRouter et ArpEngine (qui rejette la valeur invalide).
- **Trigger** : NVS avec `ArpPotStore.shuffleTemplate >= 5` (corruption). Après boot, PotRouter croit que le template est à 5+, ArpEngine reste à 0. Le pot shuffle ne catch jamais correctement.
- **Fix hint** : Ajouter la validation `octaveRange` (clamp 1-4) et `shuffleTemplate` (clamp 0-4) dans le bloc de validation existant.

---

## Spec mismatches

| # | Ce que dit la spec (CLAUDE.md) | Ce que fait le code | Fichier:ligne |
|---|---|---|---|
| SM-01 | "writing to one propagates storedValue to all same-target bindings" | Propage seulement aux bindings avec même `target` + même `potIndex` + même `buttonMask` — pas cross-pot | `src/managers/PotRouter.cpp:540-548` |

---

## Récapitulatif

| Total bugs | CRITICAL | MEDIUM | LOW |
|---|---|---|---|
| 9 | 1 | 3 | 5 |

| Difficulté | Count |
|---|---|
| TRIVIAL | 4 |
| FACILE | 4 |
| MOYEN | 1 |

**Top 3 fixes prioritaires** (rapport sévérité/difficulté) :
1. **BUG-01** `_lastToolName` dangling pointer — CRITICAL / FACILE — un seul fix `SetupUI.h` corrige 4 tools
2. **BUG-02** Division par zéro blink counts — MEDIUM / TRIVIAL — 4 guards, empêche un crash ESP32
3. **BUG-04** `nvsSaved` hardcodé true — MEDIUM / TRIVIAL — 6 lignes sur 3 tools
