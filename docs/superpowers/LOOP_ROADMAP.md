# LOOP — Roadmap d'orchestration multi-sessions

_Créé 2026-05-16. Sync à chaque fin de session._
_Référence d'orchestration pour les sessions de conception (Phases 2-6) puis d'exécution._

**Commit main de référence** : `c3d04ac` (Phase 1 close, sign-off).

> Ce roadmap ne re-documente pas les conventions globales et invariants
> projet : `~/.claude/CLAUDE.md` (user) + `.claude/CLAUDE.md` (projet)
> sont implicitement loadés à chaque session.

---

## §0 — Vérification environnement (OBLIGATOIRE avant toute action)

À exécuter **avant** lecture du reste du roadmap, **avant** toute lecture
de code, **avant** toute autre commande. Garantit qu'on est dans le bon
worktree (pas le viewer JUCE sibling `ILLPAD_V2-viewer/`).

```bash
# 1. Working directory = ILLPAD_V2 (pas -viewer ni autre)
pwd
# attendu : /Users/loic/Code/PROJECTS/ILLPAD_V2

# 2. Branche = main (pas viewer-juce, pas loop, pas une autre)
git branch --show-current
# attendu : main

# 3. Roadmap LOOP présent (preuve qu'on est dans le bon repo + bonne branche)
ls docs/superpowers/LOOP_ROADMAP.md
# attendu : docs/superpowers/LOOP_ROADMAP.md

# 4. platformio.ini présent (firmware ESP32 — le viewer JUCE n'a pas ce fichier)
ls platformio.ini
# attendu : platformio.ini
```

**Si l'une des 4 vérifs dévie** :

| Symptôme | Diagnostic | Action |
|---|---|---|
| `pwd` ≠ `/Users/loic/Code/PROJECTS/ILLPAD_V2` | Mauvais worktree (probablement `ILLPAD_V2-viewer/`) | **STOP**. Demander au user un `cd` explicite. Ne PAS faire `cd` de ta propre initiative. |
| branche ≠ `main` (ex. `viewer-juce`, `loop`, `feature/...`) | Mauvaise branche | **STOP**. Ne PAS `git checkout` sans autorisation explicite du user. |
| `docs/superpowers/LOOP_ROADMAP.md` absent | Mauvais repo OU mauvaise branche | **STOP**. Le roadmap est la preuve qu'on est sur main ILLPAD_V2. Son absence = mauvais endroit. |
| `platformio.ini` absent | Mauvais repo (probablement viewer JUCE qui utilise CMake) | **STOP**. |

Aucune lecture, aucune écriture, aucun grep, aucune autre commande tant
que les 4 vérifs ne sont pas toutes vertes.

---

## §1 — Stratégie globale

**Approche A** : 5 sessions de rédaction successives des plans Phase 2-6,
puis exécution phase-par-phase. Main gelé hors LOOP pendant toute la
séquence.

**Règle d'or spécifique LOOP** : le **code main est la source de vérité**,
PAS les snippets du plan loop branch. La branche `loop` (tag
`loop-archive-2026-05-16` → `b79d03b`) a 93 commits de retard sur main —
les snippets sont écrits contre des invariants obsolètes :
BankTypeStore v2 vs v4 actuel, `fgArpPlayMax` fusionné par v9 LED, etc.

**Cohérence d'interface LoopEngine** : la session 1 (Phase 2) doit
anticiper les besoins Phases 3-6 via une lecture rapide des plans
loop branch correspondants, même si elle ne les rédige pas. Cela évite
"ah merde Phase 4 a besoin d'un getter que j'aurais dû mettre en Phase 2".

**Format des plans rédigés** : suivre la convention des plans projet
existants — `docs/superpowers/plans/2026-04-21-loop-phase-1-plan.md`,
`2026-04-26-arpeg-gen-plan.md`, `2026-04-26-gesture-dispatcher-plan.md`.
Structure attendue : §0 décisions pré-actées (cross-refs spec) → §1 file
structure overview (table mapping Task↔fichiers) → §2 graphe dépendances
inter-tasks → §3 conventions de vérification firmware → Tasks N avec
sub-Steps numérotés en checkboxes `- [ ]` (pour
`superpowers:subagent-driven-development` / `executing-plans`) → cross-refs
spec/audit/briefing par Task → files modify/read-only par Task → commit
messages HEREDOC proposés → sync requirements (briefing, refs).

**Ne PAS invoquer le skill `superpowers:writing-plans`** — son template
générique ne capture pas les conventions embedded spécifiques projet (HW
Checkpoints, NVS Zero Migration Policy, invariants 1-7, Performance
Budget, pipeline créatif 6 étapes). Lire son contenu sans l'invoquer si
tu veux comparer / compléter, mais le format des plans existants reste
la source de vérité.

---

## §2 — Sources de référence LOOP

| Source | Rôle |
|---|---|
| Code main au commit courant | Référence absolue |
| `STATUS.md` racine | Statut projet global (section LOOP renvoie ici) |
| Ce roadmap | Orchestration multi-sessions, statut/décisions cumulatifs |
| `docs/superpowers/specs/2026-04-19-loop-mode-design.md` | Spec LOOP VALIDÉE (MAJ 2026-05-16 v9) |
| `docs/superpowers/plans/2026-04-21-loop-phase-1-plan.md` | Plan Phase 1 (exécuté commits `a84c955`→`c3d04ac`) |
| `docs/superpowers/reports/rapport_audit_loop_spec.md` | Rapport audit spec — 8 tranchages §28 |
| `docs/superpowers/specs/2026-04-26-gesture-dispatcher-design.md` Parties 8+9 (§22-§32) | **Lecture obligatoire avant session 1 (Phase 2)** — invariant "buffer LOOP sacré", anti-patterns ARPEG→LOOP, 6 actions modifiant le buffer, checklist 6 points pré-implémentation |
| `docs/reference/nvs-reference.md` | LoopPadStore / LoopPotStore DECLARED Phase 1 |
| Tag `loop-archive-2026-05-16` → `b79d03b` | Archive branche `loop` (référence intention seulement) |

**Lecture archive loop branch** (jamais merge) :

```bash
# Plans Phase 2-6 (référence intention, snippets écrits contre base obsolète)
git show loop-archive-2026-05-16:docs/plans/loop-phase2-engine-wiring.md
git show loop-archive-2026-05-16:docs/plans/loop-phase3-setup-tools.md
git show loop-archive-2026-05-16:docs/plans/loop-phase4-potrouter-led.md
git show loop-archive-2026-05-16:docs/plans/loop-phase5-effects.md
git show loop-archive-2026-05-16:docs/plans/loop-phase6-slot-drive.md

# Handoff + audits (capture des 24 AUDIT FIXES de Phase 1+2)
git show loop-archive-2026-05-16:docs/superpowers/handoff/phase1-to-phase2.md
git show loop-archive-2026-05-16:docs/superpowers/audits/2026-04-06-loop-plans-deep-audit.md
git show loop-archive-2026-05-16:docs/superpowers/audits/2026-04-06-loop-plans-verification-pass.md

# Implémentation LoopEngine référentielle (à adapter aux invariants main)
git show loop-archive-2026-05-16:src/loop/LoopEngine.h
git show loop-archive-2026-05-16:src/loop/LoopEngine.cpp
```

---

## §3 — Roadmap des sessions

| # | Type | Output | Statut |
|---|---|---|---|
| 1 | Rédaction | `docs/superpowers/plans/YYYY-MM-DD-loop-phase-2-plan.md` | PENDING |
| 2 | Rédaction | `docs/superpowers/plans/YYYY-MM-DD-loop-phase-3-plan.md` | PENDING |
| 3 | Rédaction | `docs/superpowers/plans/YYYY-MM-DD-loop-phase-4-plan.md` | PENDING |
| 4 | Rédaction | `docs/superpowers/plans/YYYY-MM-DD-loop-phase-5-plan.md` | PENDING |
| 5 | Rédaction | `docs/superpowers/plans/YYYY-MM-DD-loop-phase-6-plan.md` | PENDING |
| 6 | Audit (opt) | `docs/superpowers/audits/YYYY-MM-DD-loop-plans-cross-audit.md` | PENDING |
| 7+ | Exécution | Implémentation phase-par-phase | PENDING |

**Workflow début de session N** :
1. Lire ce roadmap intégral
2. Identifier session courante en §3
3. Lire sources §2 selon scope session
4. Exécuter (rédaction ou implémentation)
5. Update §3 (statut), §4 (insights découverts), §5 (questions résolues / nouvelles)

---

## §4 — Insights & décisions actées (cumulatif)

### Phase 1 LOOP redo sur main — close 2026-05-16

Commits : `a84c955`, `1b0ac8c`, `68855e3`, `48b96fb`, `8c0d68b`, `c3d04ac`.

- Tasks 5/6 LOOP P1 originales **skippées** : champs `fgArpPlayMax` / lignes Tool 8 `LINE_*_FG_PCT` supprimés par v9 LED brightness déjà sur main
- `LoopPadStore __attribute__((packed))` 23 B strict validé HW (Q1 §28 spec)
- EVT_WAITING brightness baseline = `_fgIntensity` post v9 (amende plan original qui référait `_fgArpStopMax` fusionné)
- `LoopPadStore` descriptor index 12 ajouté à `NVS_DESCRIPTORS[]` mais hors range `TOOL_NVS_LAST[2]` (=4) → dormant, pas check au boot, pas de badge "!" stuck. Phase 3 (Tool 3 b1) étendra `TOOL_NVS_LAST[2]` à 12 quand un writer existera.

### Arc gesture-dispatcher — refonte abandonnée

- Mars-avril : design d'une refonte qui aurait unifié `BankManager::update` + `ScaleManager::processScalePads` + `handlePlayStopPad`
- En designant, identification de 8 findings dont 5 bugs ARPEG réels (pile préservée Stop / LEFT release / fingers down, auto-Play 1er press, LEFT+hold pad toggle multi-bank)
- 5 commits de fix appliqués directement (`2dc80d9`, `4799918`, `5aa15fc`, `7432047`, `00f88e4`) — **sans refonte**
- Statut F1-F8 réévalué : 3 résolus, 4 latents catégorisés acceptables
- Décision finale : refonte plus nécessaire, code "suffisamment bon"
- Commits doc finaux (`bac8fd3`, `feef02c`) capturent invariants + garde-fous pour LOOP P2 (Parties 8-9 spec gesture)

**Conséquences architecture** :
- `BankManager::update()` + `ScaleManager::processScalePads()` restent inchangés en tant que callsites
- LoopEngine Phase 2 s'intègre à cette architecture telle quelle (pas de dispatcher hypothétique)
- `handleLoopControls()` reste une fonction autonome dans main.cpp

**Conséquence Task 4 LOOP P1** : l'amendement du plan disait "skip, gesture-dispatcher portera" → caduc. Task 4 (BankManager double-tap LOOP consume + ScaleManager early-return LOOP) **reste à câbler — confirmé prérequis défensif Phase 2 (Step 0/1)**.

### Acquis structurels base main

- BankType enum a 4 valeurs : `BANK_NORMAL=0, BANK_ARPEG=1, BANK_LOOP=2, BANK_ARPEG_GEN=3, BANK_ANY=0xFF`
- `isArpType()` helper utilisé partout — LOOP doit utiliser sa propre détection `type == BANK_LOOP`, ne pas s'attacher à `isArpType`
- BankTypeStore v4 (n'inclut PAS encore `loopQuantize[]` — décision Phase 3 conception)
- `_fgIntensity` unifié remplace 4 anciens champs FG (v9), `W_WEIGHT = 65` HW-tuné

### Découvertes session conception roadmap (2026-05-16)

- Plan Phase 2 sur loop branch a **oublié Step 11 midiPanic LOOP flush** — à inclure dans plan main
- Pads test LOOP : 47/46/45 validés HW vs 30/31/32 problématiques (déclenchement accidentel pendant drumming)
- Plans Phases 3-6 loop branch restent en archive (consultables via `git show`), **pas portés sur main**
- 24 AUDIT FIXES capturés sur loop branch (B1/B2/B7/D-PLAN-1/A1/B-PLAN-1/D1/V1 + 16 autres) à incorporer aux plans main au fil des rédactions

### Décisions actées (questions tranchées roadmap conception)

| # | Question | Décision |
|---|---|---|
| Q1 | `MAX_LOOP_BANKS` | **4** (≈42 KB SRAM pour 4 LoopEngine, ample marge sur 320 KB) |
| Q2 | `handleLoopControls` intégration dispatcher | N/A — pas de dispatcher, reste autonome |
| Q3 | LoopTestConfig pads | 47/46/45 confirmés (validés HW) |
| Q4 | HW Checkpoints A/B/C/D | À réadapter en session Phase 2 (scope réduit vs loop branch) |
| Q5 | Step 11 midiPanic LOOP | Reporté Phase 4+ (trivial, ~10 lignes) — pas bloquant Phase 2 |
| Q6 | Tool 4 BankConfig cycle 7-way (NORMAL / ARPEG-Imm / ARPEG-Beat / ARPEG_GEN-Imm / ARPEG_GEN-Beat / LOOP-Free / LOOP-Beat / LOOP-Bar) | OK |
| Q7 | BankTypeStore bump v5 si ajout `loopQuantize[]` | **Politique projet : bumper systématiquement quand layout change** (NVS Zero Migration Policy). À acter Phase 3 conception selon décision d'ajouter `loopQuantize[]` à BankTypeStore (option A) vs Store séparé. |
| Q8 | LittleFS mount path | `/littlefs/loops/slotNN.lpb` confirmé |

---

## §5 — Décisions pendantes / questions ouvertes

| # | Question | À trancher en session |
|---|---|---|
| P1 | Découpage commits Phase 2 (objectif 8-12 commits) | Session 1 conception |
| P2 | Inclure Task 4 LOOP P1 en Step 0 du plan Phase 2 | Session 1 conception |
| P3 | Définition exacte des HW Checkpoints A/B/C/D Phase 2 (post-réduction scope vs loop branch) | Session 1 conception |
| P4 | Interface publique LoopEngine — quels getters/setters pour satisfaire Phases 3-6 (LED state, Setup Tools, PotRouter, slot drive) | Session 1 conception |
| P5 | `loopQuantize` storage : ajout `BankTypeStore` v4→v5 OU Store dédié `LoopBankConfigStore` | Session 2 (Phase 3 conception) |
| P6 | 4 décisions §31 spec gesture (auto-Play interaction, slot save annulation, combo CLEAR+slot, preBankSwitch signature) | Session 1 (Phase 2) ou session 5 (Phase 6) selon scope |

---

## §6 — Anti-patterns spécifiques LOOP à éviter

Compléments aux règles génériques de CLAUDE.md global/projet.

- **Appliquer snippets plan loop branch sans cross-checker code main actuel** — line numbers et signatures référent une base pré-divergence (93 commits décalage)
- **Supposer un bug identifié sur loop branch est encore présent sur main** — toujours vérifier le code main avant de porter un fix (cas évité : PotFilter bootstrap `9d2763e` jamais porté)
- **Faire confiance aux commit messages d'avril pour juger l'état mai** — la base a tellement évolué que les commit messages anciens peuvent décrire un état qui n'existe plus
- **Amender un plan en supposant une refonte future sans valider que la refonte arrive vraiment** — Task 4 LOOP P1 a été marquée "portée par gesture-dispatcher" qui n'existera jamais. Toujours valider l'existence du porteur prévu
- **Biais "économie de session"** — préférer une approche allégée alors que la valeur de l'approche lourde est documentée. Si la cohérence inter-phase exige une session lourde, l'accepter (cas observé cette session : roadmap 5 sessions au lieu de gap notes léger)
