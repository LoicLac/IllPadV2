# ILLPAD V2 — Status

_Sync : 2026-05-16. Lu en début de session, gardé à jour au fil de l'eau._

**Focus courant** : LOOP Phase 1 sur main **CLOSE** (5 commits HW-validés). ARPEG_GEN feature complete. Prochaine étape : conception plan LOOP Phase 2 (LoopEngine + wiring).

## ARPEG_GEN — historique commits

| Phase | Tasks | Commit | Description |
|---|---|---|---|
| 2 | 1-4 | `738b640` | BankType enum + BankTypeStore v3 + ArpPotStore v1 + ArpPattern 15→6 |
| 3 | 5 | `f44a855` | isArpType cascade dans tous les call-sites BANK_ARPEG strict |
| 4 | 6-7 | `1c4d7cf` | _engineMode + Tool 5 cycle 5 états + 1-ligne minimal |
| 5-6 + WIP | 8-15 | `db0edc4` | Engine GENERATIVE core + pot routing TARGET_GEN_POSITION + Option B live mutations + setGenPosition walk-extend |
| 8 (advanced) | 19 | `af89f72` | ScaleManager pad oct → setMutationLevel pour ARPEG_GEN |
| 7 | 16-18 | `43f6a57` | Tool 5 4-fields field-focus + 2-line layout + INFO panel |
| 8 | 20-21 | `c766b24` | LED finitions read-back + docs arp/nvs/patterns refs |
| Extension | 22 | `332a75f` | proximity_factor + ecart per-bank Tool 5 (BankTypeStore v4, 6-fields cycle, 3-lignes layout, TABLE retune 8 valeurs {2,3,4,8,12,16,32,64}) |

## LOOP Phase 1 — historique commits

Redo sur main après archivage de la branche `loop` (tag `loop-archive-2026-05-16` → `b79d03b`). Plan original ramené de 7 Tasks à 4 effectives (Tasks 5/6 obsolètes par v9 LED brightness, Task 4 portée par refonte gesture-dispatcher).

| Task | Commit | Description |
|---|---|---|
| 1 reste | `a84c955` | LedController::renderBankLoop stub (v9 _fgIntensity, CSLOT_MODE_LOOP) + ToolBankConfig drawDescription accepte BANK_LOOP |
| 2 | `1b0ac8c` | LoopPadStore 23 B packed (3 controls + 16 slots) + validator + descriptor index 12 hors range Tool 3 |
| 3 | `68855e3` | LoopPotStore per-bank 12 B (5 effets : shuffleDepth/Tpl, chaos, velPattern/Depth) + namespace `illpad_lpot` |
| 7 (amendé v9) | `48b96fb` | EVT_WAITING mode-invariant : colorA VERB_PLAY green, colorB CONFIRM_OK white hardcoded, BG-aware scaling, brightness = `_fgIntensity` |
| doc sync | `8c0d68b` | nvs-reference : LoopPadStore + LoopPotStore PLANNED → DECLARED Phase 1 |

**Skippés intentionnellement** : Task 4 (gesture-dispatcher refonte porte la responsabilité), Tasks 5 + 6 (champs `fgArpPlayMax` / lignes Tool 8 `FG_PCT` supprimés par v9 LED brightness déjà sur main).

## Tool 5 ARPEG_GEN — paramètres exposés

| Champ | Range | Default | Effet |
|---|---|---|---|
| TYPE | NORMAL / ARPEG-Imm / ARPEG-Beat / ARPEG_GEN-Imm / ARPEG_GEN-Beat | - | Type de bank |
| GROUP | -, A, B, C, D | - | Scale group |
| BONUS pile | 1.0..2.0 | 1.5 | Multiplicateur poids walk sur degrés de la pile (mutation) |
| MARGIN | 3..12 | 7 | Combien la mélodie peut dériver au-dessus/dessous du range pile |
| PROX | 0.4..2.0 | 0.4 | Pente du falloff exponentiel walk (petit = step-wise, grand = erratique) |
| ECART | 1..12 | 5 | Saut max entre 2 steps consécutifs (override absolu de R2+hold) |

R2+hold pilote uniquement la longueur de séquence (`_genPosition` 0..7 → seqLen 2..64).

Pad oct 1-4 : pour ARPEG_GEN = mutation level (1=lock, 2=1/16, 3=1/8, 4=1/4). Pour ARPEG = octave range littéral (1-4 octaves).

## Comportements live validés HW

- Bank ARPEG_GEN joue effectivement, pile vivante, walk + bonus_pile audible
- Option B : pile add 1→N déclenche 3 mutations forcées (feedback live, ressenti « joué »)
- setGenPosition extension : R2+hold étend la séquence via walk continu (pas de trail monotone)
- R2+hold balaye 8 longueurs distinctes avec hystérésis ±1.5%×4095 ADC (pas de flicker frontière)
- Tool 5 cycle 6-fields + persistance reboot via BankTypeStore v4
- Bank switch ARPEG ↔ ARPEG_GEN ↔ NORMAL propre, pots reseed catch
- Scale change transpose la séquence GEN audiblement (degrés stockés, re-resolved à chaque tick)
- ARPEG classique inchangé musicalement (non-régression confirmée)

### LOOP Phase 1 (2026-05-16)

- Build clean (RAM 16.8 %, Flash 21.7 %)
- Boot normal : badges T1-T8 tous "ok" (descriptor 12 LoopPadStore hors range Tool 3, pas check au boot via `printMainMenu`)
- NORMAL / ARPEG / ARPEG_GEN inchangés (renderBankLoop dormant, jamais appelé — cycle Tool 5 ne propose pas LOOP)
- EVT_WAITING refacto invisible en runtime (aucun callsite Phase 1, premier émetteur sera LoopEngine Phase 2)

## Follow-ups ouverts

- **LOOP Phase 2 à concevoir** : plan main à rédiger (la version Phase 2 sur la branche `loop` archivée sert de référence pour les snippets LoopEngine, mais à adapter à la base actuelle — refonte gesture-dispatcher en cours, BankTypeStore v4, v9 LED unifié). Pré-lecture obligatoire : gesture-dispatcher design Parties 8 + 9 (commits `bac8fd3`, `feef02c`).
- **Tasks LOOP P1 skippées à reconsidérer en Phase 2** : Task 4 (BankManager double-tap LOOP consume, ScaleManager early-return LOOP) sera portée par la refonte gesture-dispatcher quand Phase 2 LoopEngine existera.

## Sources

- Plan ARPEG_GEN : [docs/superpowers/plans/2026-04-26-arpeg-gen-plan.md](docs/superpowers/plans/2026-04-26-arpeg-gen-plan.md)
- Spec ARPEG_GEN : [docs/superpowers/specs/2026-04-25-arpeg-gen-design.md](docs/superpowers/specs/2026-04-25-arpeg-gen-design.md)
- Plan LOOP Phase 1 : [docs/superpowers/plans/2026-04-21-loop-phase-1-plan.md](docs/superpowers/plans/2026-04-21-loop-phase-1-plan.md)
- Spec LOOP : [docs/superpowers/specs/2026-04-19-loop-mode-design.md](docs/superpowers/specs/2026-04-19-loop-mode-design.md) (VALIDÉE, MAJ 2026-05-16 LED v9)
- Rapport audit LOOP spec (8 tranchages) : [docs/superpowers/reports/rapport_audit_loop_spec.md](docs/superpowers/reports/rapport_audit_loop_spec.md)
- Branche `loop` archivée (référence Phase 1 + Phase 2 ~90 %) : tag `loop-archive-2026-05-16` → `b79d03b`
- Arp reference (incl. §13 Generative mode) : [docs/reference/arp-reference.md](docs/reference/arp-reference.md)
- NVS reference (BankTypeStore v4, ArpPotStore v1) : [docs/reference/nvs-reference.md](docs/reference/nvs-reference.md)
- Project CLAUDE.md : [.claude/CLAUDE.md](.claude/CLAUDE.md) — invariants, build, conventions, VT100 policy, setup boot-only
