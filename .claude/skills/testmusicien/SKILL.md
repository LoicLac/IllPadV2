---
name: testmusicien
description: "Stress-test le firmware ILLPAD V2 avec 5 musiciens virtuels en parallele. Chaque agent adopte un profil musicien distinct et explore le code a la recherche de bugs, edge cases, et comportements inattendus. Produit un tableau de bugs priorise avec evaluation de difficulte. Utilise ce skill quand l'utilisateur dit 'testmusicien', 'test musicien', 'stress test', 'lance les musiciens', 'fais jouer des musiciens', 'teste comme un musicien', ou toute demande de review orientee usage musical plutot que spec technique."
---

# /testmusicien — Stress-test par 5 musiciens virtuels

Tu lances 5 agents en parallele qui se comportent comme des musiciens utilisant l'ILLPAD dans des contextes differents. Ils lisent le code et cherchent des bugs qu'un musicien rencontrerait en jouant.

## Pourquoi ce skill existe

Le `/audit` existant verifie que le code correspond a la spec. Ce skill fait l'inverse : il part de l'usage musical et cherche ce que la spec n'a pas prevu. Les meilleurs bugs viennent de "je faisais X et Y en meme temps et ca a plante" — pas de "la spec dit A mais le code fait B".

## Optimisation tokens

Les agents sont lances en **Sonnet** (pas Opus) — suffisant pour du bug-hunting, ~5x moins cher.
- **NE PAS relire `.claude/CLAUDE.md`** — les agents l'ont deja en system prompt automatiquement.
- Lire uniquement `docs/architecture-briefing.md` comme contexte supplementaire (flows, invariants).
- Quand l'utilisateur cible des fichiers modifies, passer `git diff --name-only` pour que les agents se concentrent sur le delta.

## Phase 1 — Dispatch des 5 musiciens

Lance exactement 5 agents en parallele dans un seul message. Chaque agent a un profil et des workflows specifiques a explorer.

### Les 5 profils

| # | Profil | Focus principal | Workflows a tester |
|---|--------|----------------|-------------------|
| 1 | **Live performer** | Performance live, expressivite | Bank switching rapide, aftertouch sous charge, scale changes en jouant, pitch bend per-bank, velocity variation |
| 2 | **Arp wizard** | Arpeggiateur pousse a fond | 4 arps simultanes, HOLD ON/OFF transitions, play/stop pad, shuffle extreme, quantized start, scale change sur arp background, gate extremes |
| 3 | **Pot tweaker** | Pots, MIDI CC, controle parametrique | Catch system, MIDI CC output bruit/hysteresis, pitchbend steal, bargraph display, button combos, Tool 6 mapping, empty slots |
| 4 | **Sync freak** | MIDI clock, transport, multi-device | Clock source priority, PLL, transport Start/Stop/Continue, master/slave, USB+BLE simultane, tempo pot en master, fallback clock |
| 5 | **Setup tester** | Setup mode, LEDs, NVS, boot | Boot sequence, setup mode entry, pad ordering, pad roles collision, LED priority, sine pulse, NVS first boot/corruption, battery, multi-bank LED, Tool 5 validation |

### Prompt template pour chaque agent

Utilise ce template exact, en remplissant les details du profil :

```
You are a {PROFILE_NAME} stress-testing the ILLPAD48 V2 capacitive MIDI controller codebase. Your focus: **{FOCUS}**.

IMPORTANT: You already have CLAUDE.md in your system prompt — do NOT re-read it.
Start by reading `docs/architecture-briefing.md` for runtime data flows and invariants.
Then go straight to the source files relevant to your profile.

Your musician profile: {PROFILE_DESCRIPTION}

Test these workflows by reading the code and finding bugs/issues:
{NUMBERED_WORKFLOW_LIST}

## Reporting Rules

For each issue found, report using this EXACT format (one per issue):

### BUG: [short title]
- **File**: path/to/file.cpp:LINE
- **Severity**: CRITICAL | MEDIUM | LOW
- **What**: Description of the bug (what the code does wrong)
- **Trigger**: How a musician would trigger it (concrete scenario)
- **Fix hint**: One-line suggestion for the fix approach

Important:
- Only report REAL bugs you verified in the code. No speculation.
- "By design" or "design choice" is NOT a bug — skip it.
- Spec/code mismatches (CLAUDE.md says X, code does Y) are bugs — report them with severity LOW and tag [SPEC MISMATCH] in the title.
- Do NOT suggest improvements or refactoring. Only bugs.
- Read every relevant file. Don't guess, verify in code.
```

### Agent configuration

- `model`: `"sonnet"` (suffisant pour du bug-hunting, economise ~5x en tokens)
- `subagent_type`: `"general-purpose"` (ils ont besoin de lire beaucoup de fichiers)
- `description`: `"Musician N: {profile_name}"`
- Tous lances dans le meme message (parallele)

## Phase 2 — Collecte et deduplication

Quand les 5 agents terminent :

1. **Collecte** : extraire tous les bugs au format structure
2. **Deduplication** : si deux musiciens rapportent le meme bug (meme fichier, meme ligne, meme cause), ne garder qu'une entree avec mention "trouve par musicien X et Y"
3. **Spec mismatches** : les separer dans leur propre section (ce ne sont pas des bugs runtime)

## Phase 3 — Evaluation et tableau final

Evaluer la difficulte de fix de CHAQUE bug. Pour ca, lire le code concerne et estimer :

| Difficulte | Critere |
|-----------|---------|
| TRIVIAL | 1-3 lignes, aucun risque de regression |
| FACILE | 5-20 lignes, logique isolee, risque faible |
| MOYEN | 20-50 lignes, touche a plusieurs fichiers ou a une logique delicate |
| DIFFICILE | 50+ lignes, risque d'orphan notes / stuck notes / regression timing |

### Tableau de sortie

Produire UN SEUL tableau markdown, trie par severite puis difficulte :

```markdown
## Bugs trouves par les 5 musiciens

| # | Severite | Bug | Fichier:ligne | Musicien(s) | Difficulte | Lignes est. |
|---|----------|-----|--------------|-------------|------------|-------------|
| 1 | CRITICAL | ... | ... | 1,4 | FACILE | ~15 |
| 2 | MEDIUM | ... | ... | 2 | TRIVIAL | 2 |
| ... |

### Spec mismatches (CLAUDE.md a mettre a jour)
| # | Ce que dit la spec | Ce que fait le code | Fichier:ligne |
|---|---|---|---|
| 1 | ... | ... | ... |
```

### Ce que tu dis a l'utilisateur

Apres le tableau, ajouter un resume :
- Nombre total de bugs (hors spec mismatches)
- Repartition par severite
- Repartition par difficulte
- Les 3 fixes les plus impactants (rapport severite/difficulte)

## Regles importantes

- **STOP apres le tableau.** Ne pas fixer les bugs. L'utilisateur decide quoi fixer.
- Ne pas compiler. Ne pas modifier de fichiers source.
- Ne pas lancer `/audit` — ce skill est independant.
- Les 5 agents doivent etre lances dans le MEME message (vrai parallelisme).
- L'evaluation de difficulte se fait APRES la collecte, pas par les agents musiciens (ils ne sont pas qualifies pour estimer la difficulte d'un fix).
