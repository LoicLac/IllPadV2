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
| B-003 | `src/setup/ToolControlPads.cpp::run` + `_draw` | Freeze/stall ESP sur long-press arrow en VALUE_EDIT (édition CC#, deadzone, globals). Saturation USB CDC : redraws à ~50 Hz × ~7-8 KB/frame = 200-350 KB/sec, dépasse la bande pratique CDC+Python+iTerm2. Pendant stall, comet LED figé, inputs ignorés. Risque similaire suspecté dans Tool 8 LED Settings (frames ~6-7 KB). Tool 5/6/7 frames plus petits, risque modéré. | high | TODO | 2026-04-19 |

---

## Bugs résolus (DONE — conservés pour traçabilité)

| ID | Fichier:ligne | Description courte | Résolu par | Date |
|---|---|---|---|---|
| B-001 | `src/arp/ArpEngine.cpp:284` (WAITING_QUANTIZE) | `globalTick % boundary != 0` exact-equality could skip quantize boundaries when `ClockManager::generateTicks()` caught up multiple ticks in one call (cap=4). User REC tap at quantize=BEAT/BAR could wait one extra full beat/bar. | commit c23eea4 — sentinel `_lastDispatchedGlobalTick` + crossing detection, pattern aligné avec LoopEngine plan Phase 2 Step 1c. | 2026-04-07 |
| B-002 | `src/arp/ArpEngine.cpp:296` (PLAYING auto-play HOLD OFF) | Même pattern `% boundary != 0` dans la branche auto-play HOLD OFF — premier note add avec quantize=BEAT/BAR pouvait sauter une beat. Identifié pendant audit fresh pass 2026-04-07 (B-CODE-1). | commit c23eea4 — déduplication via deferral à la branche WAITING_QUANTIZE (single source of truth pour le crossing detection). | 2026-04-07 |

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

---

## Bugs ouverts (détails)

### B-003 — Tool 4 freeze sur long-press arrow (USB CDC saturation)

**Découvert** : 2026-04-19 (hardware test V3, user report)
**Reproduction** : hardware confirmé. Entrer Tool 4 setup mode, créer un
control pad via pool, entrer `e` VALUE_EDIT sur champ CC number, tenir `>`
enfoncé plusieurs secondes → l'ESP cesse de répondre (comet LED figé,
inputs ignorés). Relâcher la touche : après quelques secondes le loop
reprend et rattrape.

**Symptôme** : freeze perçu. Ce n'est pas un crash (pas de reboot WDT).
L'ESP est bloqué dans `Serial.print` en attente que le host vide le
CDC buffer.

**Cause racine** : chaque event arrow dans `_handleValueEdit` →
`_adjustField` → `_screenDirty = true` → prochain loop iteration
`_draw()` émet un frame complet (~7-8 KB sur Serial : header + PAD GRID
4×12 + POOL + SELECTED 5 rows + GLOBALS 3 rows + INFO + control bar).

Setup mode loop iteration ~15-20 ms (dominée par `pollAllSensorData()`
I2C blocking). Avec autorepeat OS à ~30 Hz et dirty fire à chaque event,
le `_draw()` cadence à ~50 Hz max en condition idéale. Output sustained
= 50 × 7 KB = 350 KB/sec.

USB CDC ESP32-S3 + Python passthrough + iTerm2 render : bande pratique
observée ~200-400 KB/sec. Saturation intermittente → TinyUSB CDC IN
buffer se remplit → `Serial.write` bloque côté firmware → main loop stall.

Facteurs contributifs V3 : POOL section ajoutée (V3.B) + SELECTED rempli
en GRID_NAV (V2.C) + GLOBALS section (V2.C) + richer INFO (V2.C). Chaque
ajout ~200-500 bytes par frame. Cumulé : ~1-1.5 KB par frame vs le Tool 4
V1 original.

**Autres tools potentiellement concernés** (non testés) :
- Tool 8 LED Settings : ~6-7 KB par frame, 30+ params numériques
  éditables par arrow. Risque équivalent.
- Tool 5 Bank Config, Tool 6 Settings, Tool 7 Pot Mapping : frames plus
  petits (~3-4 KB). Risque modéré.
- Tool 3 Pad Roles : grid 4×12 inclus, mais pas de numeric edit par arrow
  (pool discret, presses isolées). Pas de long-press sustained. Non
  concerné en pratique.

**Pas la cause** :
- `_save()` sur chaque arrow : éliminé en V3.A (commit `e31d627`).
  Le freeze persiste après V3.A.
- `flashSaved()` bloquant : éliminé en V3.A (ne fire plus par arrow).
- InputParser drain `Serial.available() > 9` : fonctionne correctement,
  ne cause pas de freeze (c'est l'output qui sature, pas l'input).
- Python `vt100_serial_terminal.py` : conception saine,
  `_read_keyboard_input` correctement drain-limité à 8 bytes, `READ_SIZE
  = 4096` pour serial→stdout. Pas de modif recommandée côté Python.

**Fix recommandé** : rate-limit `_draw()` à 30-60 Hz max dans tous les
setup tools, via helper commun dans `SetupUI` ou pattern appliqué
localement dans chaque `run()`. Pattern :

```c
static uint32_t _lastDrawMs = 0;
const uint32_t MIN_FRAME_MS = 33;  // 30 Hz cap

if (_screenDirty && (millis() - _lastDrawMs >= MIN_FRAME_MS)) {
  _draw();
  _screenDirty = false;
  _lastDrawMs = millis();
}
```

Plafonne output à 30 × 7 KB = 210 KB/sec — sous la bande pratique.
Lag perçu max 33 ms entre mutation et affichage (invisible musicalement).
Comet LED continue d'animer (loop ne bloque plus sur Serial).

**Option complémentaire** (bonus si fluidité insuffisante) : coalesce
NAV_LEFT/RIGHT bursts dans le handler (lookahead `_input.update()` sur
N events arrow avant render).

**Charge estimée** : ~30 min (ajout du rate-limit dans `ToolControlPads::run`
et propagation préventive aux autres tools à frame volumineux, surtout
Tool 8). Test hardware : refaire le long-press sur Tool 4, vérifier que
le comet continue pendant la rafale et que l'affichage suit à ~30 Hz.

**Option NON recommandée** : toucher au Python terminal (risque
d'introduire des régressions plus difficiles à débugger que le stall ESP).
