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

*(aucun bug ouvert pour le moment)*

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
