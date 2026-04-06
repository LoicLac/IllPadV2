# Setup Tools — live runtime apply post-save superflu

**Statut** : connu, non critique, potentiellement à traiter
**Date d'identification** : 2026-04-07
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
| 3 Pad Roles | `ToolPadRoles.cpp:311, 326-328, 343-345` | 7 copies vers `_bankPads, _rootPads, _modePads, *_chromaticPad, *_holdPad, *_playStopPad, _octavePads` | runtime BankManager/ScaleManager/Arp pads | **7 lignes mortes.** Re-entrée Tool 3 relit déjà depuis NVS lignes 599-630. |
| 4 Bank Config | `ToolBankConfig.cpp:36-39` | `_banks[i].type = types[i];` + `_nvs->setLoadedQuantizeMode(i, ...)` | runtime `BankSlot[8].type` + NvsManager `_loadedQuantize` cache | **4 lignes mortes.** Re-entrée Tool 4 relit déjà depuis NVS lignes 76-86. |
| 5 Settings | `ToolSettings.cpp:150` | `_keyboard->setBaselineProfile(toSave.baselineProfile);` | runtime MPR121 profile | **Mort.** `main.cpp:340` réapplique au boot. |
| 6 Pot Mapping | `ToolPotMapping.cpp:160` | `_potRouter->applyMapping(_wk);` | runtime PotRouter binding table | **Architecturalement redondant** pour le post-reboot, mais **load-bearing** pour la re-entrée intra-session (Tool 6 lit `_potRouter->getMapping()` à la ligne 540 + cancel/restore lignes 700, 793). Refactor nécessaire. |
| 7 LED Settings | `ToolLedSettings.cpp:388` | `_leds->loadColorSlots(_cwk);` | LedController `_colXxx` membres | **Mort.** `LedController::update()` retourne immédiatement quand `_previewMode == true` ; preview Tool 7 utilise `resolveColorSlot(_cwk.slots[id])` directement. |
| 7 LED Settings | `ToolLedSettings.cpp:379` | `_leds->loadLedSettings(_wk);` | LedController membres + `_gammaLut[]` | **Partiellement load-bearing.** Seul `rebuildGammaLut(s.gammaTenths)` est utile en preview. Réductible à un appel ciblé. |

### Côté runtime (setters publics morts ou sous-utilisés)

| Symbole | Fichier:ligne | Caller setup | Caller runtime | Verdict |
|---|---|---|---|---|
| `PotRouter::applyMapping()` | `PotRouter.cpp:128-132` | Tool 6 (1) | aucun | **Mort si Tool 6 retire son call.** Doublon de `loadMapping` (différence : `seedCatchValues()` à la fin). Le commentaire ligne 232 dit explicitement *"Extracted for use by both begin() and applyMapping()"*. |
| `PotRouter::seedCatchValues(bool keepGlobalCatch)` | `PotRouter.cpp:234-301` | indirect | `begin()` (1) | Le paramètre `keepGlobalCatch` n'a aucun caller qui passe `true`. Sur-spécification. |
| `NvsManager::setLoadedQuantizeMode()` | `NvsManager.h:43` + `.cpp:788` | Tool 4 (1) | aucun | **Mort si Tool 4 supprimé.** Le cache `_loadedQuantize` est lu uniquement par `ArpEngine::init()` au boot — aucune raison d'exposer un setter public. |
| `CapacitiveKeyboard::setCalibrationMaxDelta()` | `CapacitiveKeyboard.h:45` + `.cpp:771` | Tool 1 (2) | aucun | **Mort si Tool 1 refactor save flow** (passer les deltas en paramètre à `saveCalibrationData()`). |
| `CapacitiveKeyboard::setAutoReconfigEnabled()` réactivations | `ToolCalibration.cpp:373, 378, 401, 416` | Tool 1 (4 × `true`) | aucun | **4 réactivations mortes** — runtime reset au reboot. Le setter survit pour la désactivation pendant la mesure. |

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

1. **`SetupUI::showToolActive`, `showPadFeedback`, `showCollision`, `showSaved`** (`SetupUI.cpp:495-511`) : 4 méthodes déclarées + définies, **jamais appelées** dans tout `src/`. `showSaved` est commentée comme *"legacy"*. Dead code pur.
2. **`ToolPadRoles` lignes 591-597** : copie depuis runtime arrays, immédiatement écrasée par NVS reload lignes 599-630. Le runtime read est uniquement un fallback NVS-empty. Architecturalement redondant.
3. **`ToolBankConfig` lignes 68-71** : même pattern que Tool 3 — lit depuis runtime, écrasé par NVS reload lignes 76-86.

---

## Prompt pour session de résolution

Le bloc ci-dessous est conçu pour démarrer une session Claude Code dédiée à la résolution complète de cette dette. Copier-coller tel quel.

```
Je veux traiter une dette technique connue documentée dans
docs/known_issues/2026-04-07-setup-tools-live-runtime-apply-superflu.md

Lis ce doc en entier d'abord, puis applique la méthode ci-dessous.

## Contexte rapide
Setup mode est boot-only (sortie = ESP.restart() exclusif), donc tout
"live runtime apply" exécuté APRÈS un saveBlob() NVS dans les Tools est
superflu : NvsManager::loadAll() restaure tout au prochain boot. Le doc
identifie ~14-21 lignes mortes directes + ~40 lignes de plumbing +
~20-25 lignes de setters publics morts côté runtime.

Important : c'est un PROTOTYPE. CLAUDE.md dit "no NVS migration code,
no backwards compatibility shims, change freely". Pas de garde-fou à
maintenir.

## Méthode (séquentielle, NE PAS sauter d'étapes)

### Phase 1 — Vérification que le doc est toujours à jour (read-only)
1. Relire SetupManager::run() pour confirmer que la sortie est toujours
   ESP.restart() exclusif (le doc pourrait être périmé).
2. Pour chaque finding du tableau "Inventaire côté setup" du doc :
   - Ouvrir le fichier:ligne référencé
   - Confirmer que la ligne existe encore et fait bien ce qui est décrit
   - Confirmer que le caller-set listé n'a pas changé (grep)
3. Pour chaque setter runtime listé :
   - Grep le symbole
   - Confirmer le nombre exact de callers et leur localisation
4. Si un finding ne tient plus (code refactoré entre temps), le rayer
   de la liste de travail. Ne PAS supposer que le doc est intégralement
   correct.

### Phase 2 — Stratégie de découpage (alignement utilisateur AVANT code)
Présenter à l'utilisateur un plan en lots logiques :

  Lot 1 (trivial, sans refactor) :
    - Tool 2 ligne 37 + plumbing _padOrder
    - Tool 3 lignes 311, 326-328, 343-345 (7 lignes post-save apply)
    - Tool 4 lignes 36-39 (4 lignes saveConfig)
    - Tool 5 ligne 150 + plumbing _keyboard
    - Tool 7 ligne 388 (loadColorSlots)
    - SetupUI dead methods (showToolActive, showPadFeedback,
      showCollision, showSaved)
    - NvsManager::setLoadedQuantizeMode (header + impl)

  Lot 2 (refactor surgical Tool 7) :
    - Remplacer Tool 7 ligne 379 par appel ciblé à
      _leds->rebuildGammaLut(_wk.gammaTenths) (rendre rebuildGammaLut
      public si nécessaire — il l'est déjà ligne 54 de LedController.h)

  Lot 3 (refactor Tool 6, plus risqué) :
    - Tool 6 : remplacer la lecture initiale ligne 540 par un
      NvsManager::loadBlob (pattern Tool 3/4)
    - Tool 6 : remplacer le cancel-restore lignes 700, 793 par un
      snapshot local (_savedMap member)
    - Tool 6 ligne 160 : supprimer l'applyMapping
    - PotRouter : supprimer applyMapping() + simplifier seedCatchValues
      (retirer le param keepGlobalCatch s'il n'a plus aucun usage utile)

  Lot 4 (optionnel — refactor Tool 1) :
    - Refactorer saveCalibrationData() pour prendre les deltas en
      paramètre
    - Supprimer setCalibrationMaxDelta()
    - Supprimer les setAutoReconfigEnabled(true) réactivations (4)

  Lot 5 (optionnel — alignement Tool 3/4 sur lecture NVS pure) :
    - Supprimer les copies depuis runtime arrays au début de
      ToolPadRoles::run() (591-597) et ToolBankConfig::run() (68-71)
    - Conséquence : Tool 3 perd ses 7 membres pointeurs runtime + tout
      le plumbing (begin params, SetupManager pass-through, main.cpp args)

DEMANDER à l'utilisateur quels lots traiter dans cette session. Ne PAS
tout faire d'un coup sans accord. Recommander de commencer par Lot 1
qui est le moins risqué et déjà ~25-30 lignes.

### Phase 3 — Exécution lot par lot
Pour CHAQUE lot validé par l'utilisateur :

1. Lire INTÉGRALEMENT chaque fichier impacté avant d'éditer (pas
   d'edit à l'aveugle — règle CLAUDE.md "lire avant de proposer").
2. Présenter le diff exact avant d'appliquer (lignes supprimées,
   lignes modifiées, signatures impactées). Attendre OK explicite.
3. Appliquer les edits.
4. Build : ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
5. Si build échoue, diagnostiquer (probablement un caller manqué
   dans un .h ou un pass-through SetupManager). Réparer.
6. Demander à l'utilisateur de tester manuellement le lot :
   - Pour les Tools touchés : entrer setup mode, ouvrir le Tool,
     modifier une valeur, sauver, quitter, rebooter, vérifier que la
     valeur est toujours là.
   - Pour PotRouter : tester que les pots fonctionnent toujours après
     un changement de mapping en setup mode 6.
   - Pour LedController : tester Tool 7 preview + sauvegarde + reboot.
7. Si OK, marquer le lot comme done. Sinon, revert et investiguer.

### Phase 4 — Mise à jour de la documentation
Après que tous les lots validés sont passés :

1. Mettre à jour
   docs/known_issues/2026-04-07-setup-tools-live-runtime-apply-superflu.md
   pour refléter ce qui a été fait (rayer les findings traités, garder
   les findings non traités). Si tout est traité, déplacer le doc dans
   docs/archive/ avec un préfixe "DONE-".
2. Vérifier si docs/reference/architecture-briefing.md mentionne un
   des patterns supprimés (PotRouter::applyMapping notamment) — si oui,
   mettre à jour.
3. Vérifier si CLAUDE.md mentionne un des symboles supprimés (peu
   probable mais à vérifier).

## Pièges connus à éviter

1. **Tool 6 est piégeux.** Sa cancel/restore logic LIT depuis
   _potRouter->getMapping(). Si tu supprimes applyMapping() sans
   refactorer le cancel/restore, la session courante de setup mode
   peut afficher des valeurs incohérentes (cancel restaure depuis
   l'état boot au lieu de l'état pré-edit). Faire le refactor en
   bloc, pas par étape.

2. **Tool 1 pattern "runtime as save buffer".** saveCalibrationData()
   lit le membre _calibrationMaxDelta de CapacitiveKeyboard. Si tu
   retires setCalibrationMaxDelta sans refactorer saveCalibrationData
   pour prendre les deltas en paramètre, le save écrira des zéros.
   Changer les deux ensemble.

3. **Tool 3/4 fallback NVS-empty.** Les copies depuis runtime arrays au
   début de run() (591-597 pour Tool 3, 68-71 pour Tool 4) sont
   load-bearing dans le cas où NVS est vide (firmware fraîchement
   flashé). Si tu les supprimes, fournir un défaut explicite (par
   exemple appeler resetToDefaults() avant la NVS reload). Sinon, les
   working copies seraient en garbage memory au premier boot.

4. **Pas d'optimisation gratuite hors scope.** CLAUDE.md règle stricte :
   ne pas refactorer du code adjacent qui n'est pas dans le scope du
   lot. Pas de "tant qu'on y est". Si tu vois quelque chose, le noter
   en fin de session pour un futur lot, ne pas l'exécuter.

5. **Git workflow utilisateur.** Toujours sur main, jamais de branche.
   commit + push uniquement sur autorisation explicite. Présenter la
   liste des fichiers + le message HEREDOC AVANT git add. Ne pas
   utiliser git add . / -A.

6. **Build avant chaque commit.** Le user n'upload pas le firmware
   automatiquement (préférence connue) — proposer un build, demander
   confirmation pour l'upload si le user veut tester sur le device.

## Budget mental
- Lot 1 seul : ~30 lignes touchées, ~5 fichiers, faisable en une session.
- Lots 1+2 : ~35 lignes, ~6 fichiers.
- Tous lots : ~75-90 lignes, ~10 fichiers, ~2 sessions.

Recommandation : commencer par Lot 1 dans cette session, valider sur
le device, puis décider si on enchaîne sur le 2 ou si on remet à plus
tard.
```

---

## Références

- Audit original : conversation du 2026-04-07 (méthode read-only, croisé avec `NvsManager::loadAll`).
- `CLAUDE.md` : section *"NVS & Persistence — Zero Migration Policy"* (autorise les changements libres) + section *"Performance Budget"* (confirme que SRAM/flash sont généreux, seul Core 0 CPU est contraint).
- Memory : `project_setup_boot_only.md` (Setup mode boot-only, configs runtime-immutables).
