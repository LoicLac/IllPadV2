# Handoff — démarrer session iter 3 plan Tool PAD ROLE

**Date** : 2026-05-23
**Pour** : la prochaine session, qui exécute l'iter 3 (vérification mécanique snippets vs code réel par sub-agents)

---

## En une phrase

L'iter 2 du plan Tool PAD ROLE est cristallisée dans [`docs/superpowers/plans/2026-05-23-tool-pad-role-plan.md`](docs/superpowers/plans/2026-05-23-tool-pad-role-plan.md) (commit `7b21b62`, 3662 lignes), audit adversarial intégré (9 corrections actées §12.4-§12.12). L'iter 3 vérifie mécaniquement chaque snippet du plan contre le code réel actuel sur `main` (line numbers, signatures, présence helpers) et produit un rapport de cohérence par phase pour décider EXEC ready.

---

## Inputs à lire (ordre)

### 1. Plan source-of-truth (3662 lignes — référence centrale iter 3)

[`docs/superpowers/plans/2026-05-23-tool-pad-role-plan.md`](docs/superpowers/plans/2026-05-23-tool-pad-role-plan.md)

Lecture intégrale recommandée. Sections critiques :

- **§-1 Index + §0 Contexte + §2 Récap** : 20 sous-phases × 8 HW gates G0-G7
- **§1 Décisions globales iter 1** : 8 décisions structurantes (fusion Tool 3+4, sous-fichiers (c), rename `holdPad → arpPlayStopPad` en 3.A, séquence S3, etc.)
- **§3-§11 Détail par phase 3.A à 3.I** : sites concrets, patches groupés, hard-asserts, HW gates
- **§12 Audit findings (12 entries) — TRAITEMENT OBLIGATOIRE par les sub-agents** :
  - §12.1-§12.3 : findings antérieurs (palette map 5/6→7/8, spec §7.4 stricte tranchée, B-N3 pré-init non applicable)
  - **§12.4-§12.12 : 9 corrections audit pré-EXEC actées** — les sub-agents iter 3 doivent vérifier chaque section originale **APRÈS** application de la correction §12.X correspondante
  - Exemple critique : §5.3.2 Patch 5 dit "extension palette `GRID_CONTROLPAD` map=7/8" — §12.4 précise "interpréter comme extension switch inline dans `SetupUI.cpp:550-556`, table `COLORS_CONTROLPAD[]` n'existe pas". La vérification doit cibler le switch inline réel.
- **§14 Anti-régression cross-phase** : 7 invariants à préserver
- **§15 Conventions defaults par page** : modèle user `d` page-scoped + skip silencieux + valeurs hardcoded legacy temporaires

### 2. Spec source-of-truth (référence stable, ne pas modifier)

[`docs/superpowers/specs/2026-05-23-tool-pad-role-design.md`](docs/superpowers/specs/2026-05-23-tool-pad-role-design.md) — 18 sections, validée 2026-05-23.

À consulter ponctuellement pour confirmer que les patches du plan respectent la spec (concept ABSORBANT/CONTEXTUEL §3, règle unique §4, propagation §6.6, matrices §7, affichage §8, pool §9, modale §10, palette §11, scenarios §14, hors scope §17).

### 3. Code firmware actuel sur `main` (HEAD = `7b21b62`)

À vérifier contre chaque snippet/line range cité dans le plan :

- `src/setup/ToolPadRoles.{cpp,h}` (legacy + commits 3.A/3.B/3.C `002400c`/`cd3b3c9`/`97db63a` livrés)
- `src/setup/ToolControlPads.{cpp,h}` (Tool 4 à absorber en page CC)
- `src/setup/SetupManager.{cpp,h}` (notamment field `_toolRoles` cf §12.7 rename)
- `src/setup/SetupUI.{cpp,h}` (notamment `drawCellGrid` switches inline cf §12.4, `printMainMenu` check ad-hoc cf §12.6)
- `src/core/KeyboardData.h` (structs + helpers cross-store)
- `src/managers/NvsManager.{cpp,h}` (notamment `applyDevSeedLoopPadsIfSafe` L1196-1230, `saveLoopPad`, `TOOL_NVS_FIRST/LAST` L1013-1014, `NVS_DESCRIPTORS[12]` L1007 LoopPadStore cf §12.6)
- `src/managers/BankManager.{cpp,h}` (notamment `_holdPad` L21/53-54/91)
- `src/managers/ScaleManager.{cpp,h}` (notamment `_holdPad` orphan link L15/50-51 — §12.8 suppression actée)
- `src/arp/ArpEngine.{cpp,h}` (`setCaptured` signature L112-113 + body L514-545 fix F1)
- `src/main.cpp` (`s_holdPad`, `handleHoldPad`, sites `setCaptured`, init Tool 3/4)

### 4. CLAUDE.md projet

[`.claude/CLAUDE.md`](.claude/CLAUDE.md) — invariants firmware (NVS zero-migration policy, build pio path complet, conventions code, qualité finale non-prototype).

---

## Objectif iter 3 — vérification mécanique snippets

Pour chaque snippet code et line range cité dans le plan, vérifier mécaniquement contre le code réel sur `main` :

| Catégorie | Exemple | Outil |
|---|---|---|
| Line numbers exacts | `NvsManager.cpp:1196-1230` cité = `void NvsManager::applyDevSeedLoopPadsIfSafe()` réel | `Read` + comptage |
| Signatures réelles | `setCaptured(bool, MidiTransport&, const uint8_t*, uint8_t)` plan = `ArpEngine.h:112-113` réel | `Read` |
| Présence helpers | `findControlPadEntryIdx`, `isLoopControlPad`, `scaleRoleAtPad` cités existent dans `KeyboardData.h:572-633` | `Grep` |
| Hard-asserts viables | `grep "case GRID_CONTROLPAD" src/setup/SetupUI.cpp` retournera ≥ 1 match | `Bash grep` |
| Identifiants legacy | `_toolRoles` réel vs `_toolPadRoles` plan (§12.7 rename) | `Grep` |
| Pas de typo | `_wkArpPlayStopPad` cohérent avec rename Phase 3.A | `Grep` |

**Application impérative des corrections §12.4-§12.12** : les sub-agents vérifient les sections originales **après** application de la correction. Pour §5.3.2 (table `COLORS_CONTROLPAD[]`), la vérification cible le switch inline `SetupUI.cpp:550-556` selon §12.4, pas une table inexistante.

---

## Méthode — sub-agents parallèles par grappe

Découper iter 3 en 3 sub-agents parallèles (gain temps réel ~3×) :

- **Sub-agent A** — vérification §3-§5 : Phases 3.A (rename + bump NVS), 3.B (squelette), 3.C (page CC)
- **Sub-agent B** — vérification §6-§8 : Phases 3.D (page BANK), 3.E (page ARPEG), 3.F (page LOOP)
- **Sub-agent C** — vérification §9-§11 : Phases 3.G (modale), 3.H (palette + ménage), 3.I (doc-sync)

Chaque sub-agent reçoit en input :
- Le doc plan (sections de son scope + §12 audit corrections obligatoires)
- Accès `Read`/`Grep`/`Bash` sur le repo
- **Read-only strict** : aucune modification code ni doc

Modèle reco : **sonnet** (mécanique, pas profondeur conceptuelle requise — l'audit adversarial opus a déjà tranché les défauts conceptuels iter 2).

---

## Sortie attendue du sub-agent

Chaque sub-agent produit un rapport structuré par phase. Format reco :

```
# Rapport iter 3 — Sub-agent X (phases 3.Y-3.Z)

## Phase 3.Y

### Snippet/ancrage §plan:section
- **Cité** : <line range plan>
- **Réel** : <line range code>
- **Status** : OK / KO
- **Correction** : <ancres précises si KO>

### Hard-assert §plan:section
- **Commande** : `grep ...`
- **Attendu** : <résultat>
- **Réel** : <résultat exécution>
- **Status** : OK / KO

...
```

---

## Rapport iter 3 consolidé (à rédiger en session)

À partir des 3 sub-agents outputs, rédiger un rapport consolidé :

1. Récap par grappe A/B/C
2. Findings consolidés par phase 3.A à 3.I
3. Corrections à intégrer dans le doc plan (Edits ciblés Markdown)
4. **Verdict global** :
   - ≤3 corrections mineures cumulées : **EXEC-ready** → enchaîner Phase 3.A EXEC
   - ≥4 corrections OU ≥1 B-N supplémentaire : **boucle iter 3.5** (intégrer corrections + re-vérifier)
   - Refonte structurelle nécessaire (rare) : reprendre iter 2 sur sections affectées

---

## Hors scope explicite iter 3

- **Pas d'exécution code** — séparation PREP / EXEC stricte (cf [`SESSION_PROTOCOL.md`](docs/superpowers/SESSION_PROTOCOL.md) §"Workflow PREP → EXEC")
- **Pas de refonte plan** — les 9 corrections audit sont actées §12.4-§12.12, iter 3 confirme les ancrages, pas les décisions
- **Pas d'audit adversarial supplémentaire** — celui d'iter 2 a livré son rapport, corrections intégrées. Iter 3 = mécanique pure.
- **Pas de Phase 3.A EXEC** — c'est iter 4, après iter 3 clean

---

## Cross-refs disciplinaires

- [`docs/superpowers/SESSION_PROTOCOL.md`](docs/superpowers/SESSION_PROTOCOL.md) — 9 règles strictes session multi-tasks, templates manifest/HW gate/commit gate, convention nommage audit-fix B-N/M/m
- `~/.claude/CLAUDE.md` — préférences user (non-complaisance audit, scope strict, pas de footer Co-Authored-By, mode autocommit actif par défaut)
- [`.claude/CLAUDE.md`](.claude/CLAUDE.md) — invariants ILLPAD V2 (NVS zero-migration, build pio, VT100 standard non-négociable)

---

**Handoff prêt 2026-05-23**. Plan iter 2 solide post-audit. Iter 3 vérifie les ancrages mécaniques, puis EXEC.
