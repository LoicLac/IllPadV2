# Known Bugs — ILLPAD V2

Tracker des bugs identifiés lors d'audits ou de tests, classés par sévérité et statut. Mise à jour au fil des audits et des fixes.

**Convention** :
- **ID** : préfixe par catégorie (`B-` runtime, `C-` cohérence, `D-` doc, `A-` bloquant). Numéro séquentiel.
- **Sévérité** : `low` / `med` / `high` / `blocker`
- **Status** : `TODO` (à traiter) / `WIP` (en cours) / `DONE` (résolu, conservé pour traçabilité) / `WONTFIX` (acté comme accepté)
- **Date** : ISO 2026-MM-DD de la dernière mise à jour

---

## Bugs ouverts

| ID | Fichier:ligne | Description courte | Sévérité | Status | Date |
|---|---|---|---|---|---|
| B-004 | `src/setup/ToolPotMapping.cpp:486-531` (`drawScreen()`) | Tool 6 : presser `d` (NAV_DEFAULTS) en mode navigation set bien `confirmDefaults = true` (ligne 734) et la branche d'attente y/n (ligne 600-621) consomme correctement le clavier, MAIS `drawScreen()` ne contient AUCUN rendu du prompt "Reset to defaults? (y/n)". Conséquence : l'utilisateur presse `d`, rien ne change visuellement, presse une autre touche par réflexe → cancel silencieux. La fonctionnalité reset-to-defaults est effectivement inaccessible. Pré-existant : commit `f7f9b0a` du 2026-03-23, antérieur à la branche `loop`. Détecté pendant Phase 1 LOOP Hardware Checkpoint C alors que B-002 stale NVS rendait Tool 6 plus visible. Fix : ajouter une branche `if (confirmDefaults) drawControlBar("Reset to defaults? [y/n]")` dans `drawScreen()`, similaire au pattern `_confirmSteal` ligne 514. Hors scope Phase 1 LOOP. | medium | TODO | 2026-04-08 |
| B-005 | `src/setup/ToolPotMapping.cpp:580-584, 655-670` (CC# live display) | Tool 6 : en mode CC-editing (`_ccEditing=true`), tourner le pot R1 (`_potCcNum` path ligne 581-583) OU presser `</>` (ligne 656-668) met à jour `_ccNumber` et set `screenDirty=true`, mais le rendu du CC# en cours de modification ne défile pas visuellement. La valeur n'apparaît qu'à la confirmation ENTER (`saveMapping` → flashSaved). `drawInfoPanel` ligne 467 affiche `CC#: [%d]` conditionnellement à `_ccEditing`, donc le path de render existe. Probablement : frame throttling ou flag `screenDirty` consommé par une autre branche du main loop avant le rendu. Pré-existant, aucun commit Phase 1 n'a touché `ToolPotMapping.cpp`. Fix : tracer le dispatch `if (screenDirty) drawScreen()` pour vérifier l'ordre + potentiel early-continue qui saute le render. | low | TODO | 2026-04-08 |
| B-006 | `src/setup/ToolPotMapping.cpp` (CC# pot turn serial log) | Tool 6 : en mode CC-editing, tourner le pot R1 ne produit aucun `[POT] CC=%d` ou équivalent dans le serial debug. Pas un bug fonctionnel, juste une absence de diagnostic. Lié à B-005 : sans log serial, impossible de vérifier côté firmware si le pot est bien lu. Fix : ajouter un `#if DEBUG_SERIAL Serial.printf("[POT] CC# live: %d\n", _ccNumber); #endif` dans le path ligne 581-583. Pré-existant, hors scope Phase 1. | low | TODO | 2026-04-08 |
| B-007 | `src/main.cpp:927-942` (`debugOutput()` per-bank prints) | Bank switch produit des lignes `[POT] BaseVel=.. VelVar=.. PitchBend=.. Gate=.. ShufDepth=.. Division=.. Pattern=.. ShufTpl=..` dans le serial debug alors qu'aucun pot n'a bougé physiquement. Cause : `reloadPerBankParams()` (main.cpp:588) → `PotRouter::loadStoredPerBank()` écrit les valeurs NVS per-bank du bank entrant dans les `_storedValue[]` internes. Les getters `getBaseVelocity()/getVelocityVariation()/getPitchBendOffset()/getGate()/etc.` retournent la `_storedValue` courante, donc le bloc dirty-detect `if (val != s_dbgVal) { print; s_dbgVal = val; }` (lignes 927-942) déclenche un print à chaque changement per-bank — VRAI sur bank switch mais sans rapport avec un mouvement de pot physique. Pas un bug fonctionnel : les valeurs affichées sont cohérentes (NVS per-bank du bank entrant, `PitchBend=8192` = default, `BaseVel=44` = user-configuré sur ce bank, etc.). Impact : serial bruyant pendant bank switching, peut masquer des vrais mouvements de pots simultanés, confusion à la lecture ("pourquoi le pot envoie-t-il BaseVel alors que je n'ai pas touché au pot ?"). Pré-existant depuis commit `2590e4d2` du 2026-03-23 (LoicLac), antérieur à la branche `loop`. Découvert pendant Phase 2 LOOP Hardware Checkpoint A alors que l'override LoopTestConfig forçait bank 8 en LOOP et révélait les prints `[POT] BaseVel=44 PitchBend=8192` correspondant aux valeurs NVS per-bank de l'ancien bank 8 ARPEG. Fix : 3 options non exclusives — (1) déplacer ces prints dans un `logBankParamReload(bank)` appelé depuis `reloadPerBankParams()` avec préfixe `[BANK RELOAD]` pour clarifier l'origine ; (2) skip un frame de dirty-detect après bank switch via un flag `s_suppressPotDebugFrames` ; (3) tag différent (`[BANK_PARAM]` vs `[POT]`) selon que la dirty est déclenchée par un `hasMoved()` PotFilter ou par un reload. Hors scope Phase 2. | low | TODO | 2026-04-08 |

---

## Bugs résolus (DONE — conservés pour traçabilité)

| ID | Fichier:ligne | Description courte | Résolu par | Date |
|---|---|---|---|---|
| B-001 | `src/arp/ArpEngine.cpp:284` (WAITING_QUANTIZE) | `globalTick % boundary != 0` exact-equality could skip quantize boundaries when `ClockManager::generateTicks()` caught up multiple ticks in one call (cap=4). User REC tap at quantize=BEAT/BAR could wait one extra full beat/bar. | commit c23eea4 — sentinel `_lastDispatchedGlobalTick` + crossing detection, pattern aligné avec LoopEngine plan Phase 2 Step 1c. | 2026-04-07 |
| B-002 | `src/arp/ArpEngine.cpp:296` (PLAYING auto-play HOLD OFF) | Même pattern `% boundary != 0` dans la branche auto-play HOLD OFF — premier note add avec quantize=BEAT/BAR pouvait sauter une beat. Identifié pendant audit fresh pass 2026-04-07 (B-CODE-1). | commit c23eea4 — déduplication via deferral à la branche WAITING_QUANTIZE (single source of truth pour le crossing detection). | 2026-04-07 |
| B-003 | NVS `illpad_pmap` (stale custom mapping) | NORMAL+hold-left : R2 et R3 cibles inversées (PITCH_BEND sur R2, AT_DEADZONE sur R3). Découvert pendant Phase 1 LOOP Hardware Checkpoint B. Code firmware vérifié intégralement (DEFAULT_MAPPING, rebuildBindings, resolveBindings, POT_PINS) — tout cohérent. Cause confirmée : `PotMappingStore` custom stocké en NVS d'une session Tool 6 antérieure qui override le default. | Phase 1 Commit 7 (`f8d9f0a`) — Step 7b ajoute `loopMap[8]` à `PotMappingStore`, taille passe de 36→52 bytes, `loadBlob` rejette le vieux blob 36-byte par size mismatch (Zero Migration Policy), DEFAULT_MAPPING rechargé. Confirmé hardware par l'utilisateur après reboot du build Phase 1 final. | 2026-04-07 |

---

## Décisions WONTFIX (acceptées explicitement)

| ID | Fichier:ligne | Description courte | Justification | Date |
|---|---|---|---|---|

*(vide pour le moment)*

---

## Notes méthodologiques

### Quand ajouter une entrée ici

- Bug identifié lors d'un audit mais hors scope de la session courante (ex : trouvé en auditant LOOP, mais qui touche ARPEG → tracker ici plutôt que d'élargir le scope)
- Bug observé en test hardware mais non reproduit immédiatement
- Bug pré-existant connu, qu'on accepte de ne pas fixer maintenant pour une raison documentée
- Bug identifié dans un patch d'audit qui ne peut pas être fixé immédiatement (ex : nécessite refactor)

### Quand NE PAS ajouter d'entrée ici

- Bug critique en cours d'investigation → utiliser `TodoWrite` ou plan dédié
- Bug fixé dans la même session que sa découverte → mentionner dans le commit message, pas la peine d'ouvrir/fermer ici
- Suggestion d'amélioration / refactor / optimisation sans symptôme observable → backlog ailleurs (pas ce fichier)

### Format d'une entrée détaillée (si la table compacte ne suffit pas)

Pour les bugs complexes qui méritent plus de contexte, ajouter une section H3 sous "Bugs ouverts (détails)" :

```markdown
### B-001 — ArpEngine quantize boundary missed on tick catch-up

**Découvert** : 2026-04-06 (audit LOOP plans pass 2)
**Reproduction** : non vérifiée hardware, identifié par lecture de code
**Symptôme attendu** : un user qui appuie REC en mode quantize BEAT/BAR voit
parfois l'action prendre une beat/bar de plus que prévu pour démarrer.
Conditions : pic de latence loop() (NVS commit, BLE callback burst).
**Cause racine** : `globalTick % boundary == 0` est trop strict ; la boucle
de catch-up dans `generateTicks()` peut sauter le multiple exact.
**Fix recommandé** : remplacer le check par une crossing detection
basée sur `_lastDispatchedGlobalTick / boundary`. Pattern déjà appliqué à
LoopEngine en pass-2 (Phase 2 Step 1c).
**Charge** : ~30 min (modification + test hardware au métronome).
```

(Pour B-001 ci-dessus, la description compacte de la table suffit. Section
détaillée à ajouter seulement si l'ETA est prêt à être attaqué.)
