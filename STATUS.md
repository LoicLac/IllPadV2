# ILLPAD V2 — Status

_Sync : 2026-05-17. Lu en début de session, gardé à jour au fil de l'eau._

**Focus courant** : Refacto Tool 5 **CLOSE** (3 commits Tasks 1/2/3 — Task 4 no-op car helpers NvsManager utilisés par boot path). LOOP Phase 1 sur main **CLOSE**. ARPEG_GEN feature complete. **Prochaine étape : rédiger plan Phase 2 LOOP from scratch** (LoopEngine + recording µs + playback BPM + premier son MIDI LOOP). Phase 2 LOOP démarre maintenant.

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

Redo sur main après archivage de la branche `loop` (tag `loop-archive-2026-05-16` → `b79d03b`). Plan original ramené de 7 Tasks à 5 effectives (Tasks 5/6 obsolètes par v9 LED brightness).

| Task | Commit | Description |
|---|---|---|
| 1 reste | `a84c955` | LedController::renderBankLoop stub (v9 _fgIntensity, CSLOT_MODE_LOOP) + ToolBankConfig drawDescription accepte BANK_LOOP |
| 2 | `1b0ac8c` | LoopPadStore 23 B packed (3 controls + 16 slots) + validator + descriptor index 12 hors range Tool 3 |
| 3 | `68855e3` | LoopPotStore per-bank 12 B (5 effets : shuffleDepth/Tpl, chaos, velPattern/Depth) + namespace `illpad_lpot` |
| 7 (amendé v9) | `48b96fb` | EVT_WAITING mode-invariant : colorA VERB_PLAY green, colorB CONFIRM_OK white hardcoded, BG-aware scaling, brightness = `_fgIntensity` |
| doc sync | `8c0d68b` | nvs-reference : LoopPadStore + LoopPotStore PLANNED → DECLARED Phase 1 |
| 4 défensif | `2624b12` | BankManager double-tap LOOP consume + ScaleManager early-return BANK_LOOP (refonte gesture-dispatcher abandonnée → câblé directement) |

**Skippés intentionnellement** : Tasks 5 + 6 (champs `fgArpPlayMax` / lignes Tool 8 `FG_PCT` supprimés par v9 LED brightness déjà sur main).

## Refacto Tool 5 — historique commits (2026-05-17)

| Task | Commit | Description |
|---|---|---|
| 1 | `69e19ed` | `MAX_LOOP_BANKS = 2` (KeyboardData.h) + validator `quantize[]` contextuel (ARP 0..1 / LOOP 0..2) — pas de bump NVS |
| 2 | `22e788e` | Header `ToolBankConfig.h` refacto (struct `Tool5Working` + `ParamDescriptor` + extern `PARAM_TABLE`) — compile WIP rouge attendu |
| 3 | `e2857c5` | Body `ToolBankConfig.cpp` rewrite intégral (tableau matriciel banks×params, nav 2D, INFO 3 états, `PARAM_TABLE` déclarative 7 entrées, labels NORM/ARP_N/LOOP/ARP_G, CELL_W=7, INFO long form "Arpeggio normal/generatif") |
| 4 | _skippé_ | Helpers `getLoadedBonusPile`/`MarginWalk`/`ProximityFactor`/`Ecart` **conservés** : callsites runtime trouvés (`main.cpp:765-769` push values into `ArpEngine` au boot). Spec §16 anticipait ce cas. |
| 5 | _ce commit_ | Doc sync : architecture-briefing §4 Table 1 + STATUS.md focus courant |

**HW Checkpoint A validé** (rendering + nav 2D + édition + persistance NVS reboot + non-régression ARPEG_GEN/NORMAL/ARPEG) post Task 3 — feedback user "tout ok passe a la suite" après rename ARPG→ARP_N + AGEN→ARP_G + INFO long form.

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

### LOOP Phase 1 Task 4 défensif (2026-05-17, commit `2624b12`)

- Build clean inchangé (RAM 16.8 %, Flash 21.7 %)
- Aucun observable runtime tant qu'une bank LOOP n'existe pas (impossible Phase 1, Tool 5 ne propose pas LOOP)
- Guards défensifs prêts pour Phase 2 : double-tap LOOP n'entraînera pas de bank switch parasite, scale change no-op sur bank LOOP

## Follow-ups ouverts

- **Progrès LOOP** (orchestration légère, jalons restants) : [docs/superpowers/LOOP_PROGRESS.md](docs/superpowers/LOOP_PROGRESS.md). Vue d'ensemble + tableau d'étapes + dépendances. Pas un plan figé — mis à jour à chaque jalon franchi. **Étape courante : Phase 2 LOOP à rédiger from scratch** (Refacto Tool 5 livré 2026-05-17, prêt pour LOOP runtime).

## Sources

- Spec ARPEG_GEN : [docs/superpowers/specs/2026-04-25-arpeg-gen-design.md](docs/superpowers/specs/2026-04-25-arpeg-gen-design.md)
- Spec LOOP : [docs/superpowers/specs/2026-04-19-loop-mode-design.md](docs/superpowers/specs/2026-04-19-loop-mode-design.md) (VALIDÉE, MAJ 2026-05-17 — Q6 inversée post refacto Tool 5)
- Spec Tool 5 refacto (pré-Phase 2 LOOP) : [docs/superpowers/specs/2026-05-17-tool5-bank-config-refactor-design.md](docs/superpowers/specs/2026-05-17-tool5-bank-config-refactor-design.md) (VALIDÉE 2026-05-17)
- Progrès LOOP (jalons restants) : [docs/superpowers/LOOP_PROGRESS.md](docs/superpowers/LOOP_PROGRESS.md) (suivi léger, MAJ par jalon)
- Invariants buffer LOOP : [docs/reference/loop-buffer-invariants.md](docs/reference/loop-buffer-invariants.md) (extrait des Parties 8-9 du gesture-dispatcher-design archivé)
- Plan ARPEG_GEN (archivé) : [docs/archive/2026-04-26-arpeg-gen-plan.md](docs/archive/2026-04-26-arpeg-gen-plan.md)
- Plan LOOP Phase 1 (archivé) : [docs/archive/2026-04-21-loop-phase-1-plan.md](docs/archive/2026-04-21-loop-phase-1-plan.md)
- Rapport audit LOOP spec 8 tranchages (archivé) : [docs/archive/rapport_audit_loop_spec.md](docs/archive/rapport_audit_loop_spec.md)
- Branche `loop` archivée (référence Phase 1 + Phase 2 ~90 %) : tag `loop-archive-2026-05-16` → `b79d03b`
- Arp reference (incl. §13 Generative mode) : [docs/reference/arp-reference.md](docs/reference/arp-reference.md)
- NVS reference (BankTypeStore v4, ArpPotStore v1) : [docs/reference/nvs-reference.md](docs/reference/nvs-reference.md)
- Project CLAUDE.md : [.claude/CLAUDE.md](.claude/CLAUDE.md) — invariants, build, conventions, VT100 policy, setup boot-only
