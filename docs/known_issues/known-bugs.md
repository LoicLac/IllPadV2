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
| B-001 | `src/arp/ArpEngine.cpp:284` | `WAITING_QUANTIZE` boundary check uses `globalTick % boundary != 0` exact-equality. If `ClockManager::generateTicks()` catches up multiple ticks in one call (up to 4 — see `ClockManager.cpp:181-203`), a quantize boundary can be skipped entirely. The pending start waits one full beat (or bar) for the next opportunity. Same pattern as the LoopEngine bug fixed in 2026-04-06 pass-2 audit (B1 finding). | med | TODO | 2026-04-06 |

---

## Bugs résolus (DONE — conservés pour traçabilité)

| ID | Fichier:ligne | Description courte | Résolu par | Date |
|---|---|---|---|---|

*(vide pour le moment — sera peuplé au fil des fixes)*

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
