# Setup Tools — live runtime apply post-save superflu

**Statut** : connu, non critique, potentiellement à traiter
**Date d'identification** : 2026-04-07
**Re-vérifié** : 2026-04-18 (scope majoritairement valide ; 3 points obsolètes corrigés : Tool 3 compte 7→6 après suppression playStopPad, `seedCatchValues(true)` désormais utilisé par bank switch, `SetupUI::showSaved` supprimée)
**Impact runtime** : nul — zéro cycle dans la main loop, zéro impact latence MIDI/BLE/Core 0
**Impact mémoire** : négligeable — ~36 octets SRAM permanents, ~200-400 octets flash
**Risque de régression si non traité** : aucun (le code mort ne tourne jamais en jeu)
**Bénéfice principal du traitement** : simplification d'API publique + cohérence architecturale

---

## Résumé en une phrase

Plusieurs `Tool*::run()` de setup mode poussent leurs valeurs vers le runtime (`PotRouter`, `BankSlot`, `LedController`, `CapacitiveKeyboard`, `NvsManager` cache) **après** le `saveBlob()` NVS, alors que setup mode est boot-only et que `NvsManager::loadAll()` restaure intégralement ces mêmes valeurs au prochain reboot.

## Contexte technique

- `SetupManager::run()` (`src/setup/SetupManager.cpp:76-167`) est un `while(true)` blocking dont la **seule sortie est `ESP.restart()`** (ligne 147).
- Commentaire ligne 56 : *"No restore needed — setup always ends with ESP.restart()"*.
- `NvsManager::loadAll()` (`src/managers/NvsManager.cpp:502-781`) recharge **tous** les blobs et scalars au boot : padOrder, bankPads, scalePads, arpPads, bankTypes+quantize, settings, ledSettings, colorSlots, potMapping, potFilter, etc.
- **Conséquence** : tout `_runtime->apply(...)` exécuté **après** un `saveBlob()` pour pousser la valeur vers un sous-système Core 1 est strictement redondant pour le cas post-reboot.

## Inventaire des findings

### Côté setup (Tools)

| Tool | Fichier:ligne | Code | Sous-système touché | Verdict |
|---|---|---|---|---|
| 1 Calibration | — | — | — | **Aucun finding net.** Pattern "runtime as save buffer" légitime (`saveCalibrationData()` lit les membres runtime). |
| 2 Pad Ordering | `ToolPadOrdering.cpp:37` | `memcpy(_padOrder, orderMap, NUM_KEYS);` | runtime `padOrder[48]` | **Mort.** `_padOrder` n'est lu nulle part en setup mode après ce memcpy. |
| 3 Pad Roles | `ToolPadRoles.cpp:269, 284-286, 300-301` | 6 copies vers `_bankPads, _rootPads, _modePads, *_chromaticPad, *_holdPad, _octavePads` | runtime BankManager/ScaleManager/Arp pads | **6 lignes mortes.** Re-entrée Tool 3 relit déjà depuis NVS lignes 544-569. (*_playStopPad retiré avec le rôle playStopPad — commit 958e6b1*) |
| 4 Bank Config | `ToolBankConfig.cpp:38-43` | `_banks[i].type = types[i];` + `_nvs->setLoadedQuantizeMode(i, ...)` + `_nvs->setLoadedScaleGroup(i, ...)` | runtime `BankSlot[8].type` + NvsManager `_loadedQuantize` + `_loadedScaleGroup` cache | **5 lignes mortes** (v2 ajoute `setLoadedScaleGroup`). Re-entrée Tool 4 relit déjà depuis NVS lignes 82-92. |
| 5 Settings | `ToolSettings.cpp:94` | `_keyboard->setBaselineProfile(toSave.baselineProfile);` | runtime MPR121 profile | **Mort.** `main.cpp:330` réapplique au boot. |
| 6 Pot Mapping | `ToolPotMapping.cpp:160` | `_potRouter->applyMapping(_wk);` | runtime PotRouter binding table | **Architecturalement redondant** pour le post-reboot, mais **load-bearing** pour la re-entrée intra-session (Tool 6 lit `_potRouter->getMapping()` + cancel/restore). Refactor nécessaire. |
| 7 LED Settings | `ToolLedSettings.cpp:365` | `_leds->loadColorSlots(_cwk);` | LedController `_colXxx` membres | **Mort.** `LedController::update()` retourne immédiatement quand `_previewMode == true` ; preview Tool 7 utilise `resolveColorSlot(_cwk.slots[id])` directement. |
| 7 LED Settings | `ToolLedSettings.cpp:356` | `_leds->loadLedSettings(_wk);` | LedController membres + `_gammaLut[]` | **Partiellement load-bearing.** Seul `rebuildGammaLut(s.gammaTenths)` est utile en preview. Réductible à un appel ciblé. |

### Côté runtime (setters publics morts ou sous-utilisés)

| Symbole | Fichier:ligne | Caller setup | Caller runtime | Verdict |
|---|---|---|---|---|
| `PotRouter::applyMapping()` | `PotRouter.cpp:128-132` | Tool 6 (1) | aucun | **Mort si Tool 6 retire son call.** Doublon de `loadMapping` (différence : `seedCatchValues()` à la fin). Le commentaire ligne 232 dit explicitement *"Extracted for use by both begin() and applyMapping()"*. |
| `NvsManager::setLoadedQuantizeMode()` | `NvsManager.h:43` + `.cpp:838` | Tool 4 (1) | aucun | **Mort si Tool 4 supprimé.** Le cache `_loadedQuantize` est lu uniquement par `ArpEngine::init()` au boot — aucune raison d'exposer un setter public. |
| `CapacitiveKeyboard::setCalibrationMaxDelta()` | `CapacitiveKeyboard.h:45` + `.cpp:771` | Tool 1 (2) | aucun | **Mort si Tool 1 refactor save flow** (passer les deltas en paramètre à `saveCalibrationData()`). |
| `CapacitiveKeyboard::setAutoReconfigEnabled()` réactivations | `ToolCalibration.cpp:385, 390, 413, 428` | Tool 1 (4 × `true`) | aucun | **4 réactivations mortes** — runtime reset au reboot. Le setter survit pour la désactivation pendant la mesure. |

*Note : `PotRouter::seedCatchValues(bool keepGlobalCatch)` était listé ici dans la v2026-04-07. Désormais caduc — `main.cpp:597` appelle `seedCatchValues(true)` sur le chemin bank switch. Le paramètre est justifié.*

## Patterns absents (signal positif)

Vérifié et **pas trouvé** :

- Pas de mutex/lock autour des structures de config setup.
- Pas de `std::atomic` sur les configs Tool (les atomics du code sont sur le double-buffer Core 0/1 et les pots, légitimes).
- Pas de "dirty flag rebuild" runtime sur les configs Tool.
- Pas de double-buffer config / pending swap.
- Pas de validation defensive runtime au-delà de `validateXxxStore()` à l'entrée NVS.

→ Le runtime est **déjà architecturé en supposant que les configs Tool sont immutables post-boot**. Les setters publics qui contredisent ça sont des résidus, pas le pattern dominant.

## Estimation chiffrée

| Catégorie | Lignes |
|---|---:|
| Lignes mortes directes côté setup | ~14-21 |
| Plumbing récupérable côté setup (members, ctor inits, begin params, SetupManager pass-through) | ~40-46 |
| Setters publics morts côté runtime | ~20-25 |
| **Total combiné si toute la chaîne est tirée** | **~75-90 lignes** |

## Pourquoi ce n'est pas urgent

1. **Zéro impact runtime.** Le code mort ne s'exécute jamais pendant le jeu — la main loop Core 1 ne touche à rien de tout ça. Pas de gain de cycles, pas de gain de latence MIDI.
2. **Mémoire négligeable.** ~36 octets SRAM permanents (membres pointeurs morts dans Tools) sur 320 KB total = 0.01%. Flash ~400 octets sur 8 MB = 0.005%.
3. **Aucun bug fonctionnel observable.** Le pattern actuel "save NVS + apply live" produit le même résultat que "save NVS + reboot" parce que setup mode termine toujours par reboot. L'utilisateur ne voit aucune différence.
4. **CLAUDE.md** dit explicitement *"prefer safe over economical [...] only truly constrained resource is per-cycle CPU time on Core 0"* — cet audit ne touche **ni** Core 0, **ni** SRAM contrainte, **ni** BLE bandwidth.

## Pourquoi le traiter quand même (un jour)

1. **Réduction de surface API publique** des classes runtime (`PotRouter`, `NvsManager`, `CapacitiveKeyboard`) — moins de setters publics qui n'ont aucun caller hors setup, moins d'invariants à vérifier mentalement.
2. **Cohérence architecturale** — aligner les Tools avec le postulat boot-only que le runtime applique déjà partout.
3. **Lisibilité** — éliminer le doublon `loadMapping`/`applyMapping` qui suggère à tort qu'il existe deux cas d'usage distincts.
4. **Pédagogique** — un futur lecteur (toi dans 6 mois, Claude dans une autre session) ne perd pas de temps à se demander *"dans quel cas `applyMapping` sert ? pourquoi pas juste `loadMapping` ?"*.
5. **Trait architectural** — Tool 3 et Tool 4 lisent leur état initial *à la fois* depuis runtime *et* depuis NVS (NVS écrase runtime). Tool 6 lit *uniquement* depuis runtime. Cette inconsistance est la source du couplage `applyMapping`. Aligner Tool 6 sur le pattern Tool 3/4 résoudrait le finding et homogénéiserait le code.

## Findings additionnels notés en passant (hors scope strict)

1. **`SetupUI::showToolActive`, `showPadFeedback`, `showCollision`** (`SetupUI.cpp:614, 618, 623`) : 3 méthodes déclarées + définies, **jamais appelées** dans tout `src/`. Dead code pur. (*`showSaved` a été supprimée depuis la v2026-04-07.*)
2. **`ToolPadRoles` lignes 535-540** : copie depuis runtime arrays, immédiatement écrasée par NVS reload lignes 544-569. Le runtime read est uniquement un fallback NVS-empty. Architecturalement redondant.
3. **`ToolBankConfig` lignes 74-78** : même pattern que Tool 3 — lit depuis runtime, écrasé par NVS reload lignes 82-92.

---

---

## Références

- Audit original : conversation du 2026-04-07 (méthode read-only, croisé avec `NvsManager::loadAll`).
- `CLAUDE.md` : section *"NVS & Persistence — Zero Migration Policy"* (autorise les changements libres) + section *"Performance Budget"* (confirme que SRAM/flash sont généreux, seul Core 0 CPU est contraint).
- Memory : `project_setup_boot_only.md` (Setup mode boot-only, configs runtime-immutables).
