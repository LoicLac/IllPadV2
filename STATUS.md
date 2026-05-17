# ILLPAD V2 — Status

_Sync : 2026-05-17. Lu en début de session, gardé à jour au fil de l'eau._

**Focus courant** : Refacto Tool 5 **CLOSE**. LOOP Phase 1 **CLOSE**. ARPEG_GEN feature complete. Viewer serial Phase 1 **CLOSE**. **Viewer bidirectionnel Phase 2 firmware CODE COMPLETE — HW gates G2-G7 en attente du viewer-juce Phase 2 codé** (14 commits firmware sur `main`, build clean RAM 16.9% / Flash 21.9%, HW gates Phase 2.A + G1 validés). **Prochaine étape immédiate : impl spec viewer Phase 2** sur branche `viewer-juce` (parser `[BANK_SETTINGS]` + `[ERROR] cmd=`, `CommandSender`, UI toggle ClockMode + 4 sliders ARPEG_GEN + error toast + lock indicator). Reprise firmware = HW gates G2-G7 via terminal serial OU viewer Phase 2 fonctionnel. Phase 2 LOOP en file d'attente après.

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

## Viewer serial centralization Phase 1 — historique commits (2026-05-17)

11 commits firmware (`main`) + 1 commit viewer (`viewer-juce` worktree). Plan exécuté de bout en bout en single session avec build-gate uniquement, fixes HW appliqués au fil de l'eau quand observés.

| Phase | Commit | Description |
|---|---|---|
| 1.A | `fedeb81` | Plomberie module `src/viewer/ViewerSerial.{cpp,h}` : FreeRTOS xQueue 32×256B, task Core 1 prio 0, atomic flag dormance via `if (Serial)`, `Serial.setTxTimeoutMs(0)` |
| 1.B | `1c46e0e` | Boot debug tagging `[BOOT *]` (72 sites) + cleanup UNGATED (banner, ArpPotStore v0) + `[INIT] FATAL` → `[FATAL]` always-on + `logFullBaselineTable` dead code retiré |
| 1.C.1 | `01e8a17` | Migration `[POT]` events (17 sites) via `viewer::emitPot()` PRIO_LOW |
| 1.C.2 | `8399b61` | Migration `[BANKS]/[BANK]/[STATE]/[READY]` + déplacement `dumpBanksGlobal/dumpBankState/formatTargetValueForBank` vers le module + race fixes (seed atomic, wait-retry, auto-resync hook) |
| 1.C.3 | `b03dc14` | Migration `[ARP]/[GEN]` (8 sites) — `+/-note` PRIO_LOW droppable, `Play/Stop` PRIO_HIGH |
| 1.C.4 | `c6957b8` | Migration `[SCALE]/[ARP]/[ARP_GEN] octave` (5 sites) + ROOT_NAMES/MODE_NAMES déplacées dans le module |
| 1.C.5 | `cd0171d` | Migration `[CLOCK] source` + `[MIDI]` runtime connect/disconnect (8 sites) |
| 1.C.6 | `70dff60` | Migration `[PANIC]` (1 site) |
| 1.D | `681c2a2` | `[GLOBALS]` + `[SETTINGS]` + `resetDbgSentinels` + `pollCommands` complet + auto-resync chain (résout race boot dump observée Phase 1.C.2) |
| 1.E | `978d93b` | `[CLOCK] BPM=` debounced ±1 BPM (résout audit R3 — viewer reflète PLL BPM externe en mode slave) |
| fix | `6a78d02` | Chunked write dans task drain — résout drop des longues lignes pendant cold-USB-connect (issue HW boot dump partiel ~1.5s post-flash) |
| fix | `9d6a40f` | Throttle 200ms sur `[CLOCK] BPM=` — anti-flood pendant convergence PLL (max 5 events/sec en regime ripple, 0 en stable) |

**HW Checkpoint** validé en fin de session — boot dump complet, bank switch, scale change, ARPEG live, external MIDI clock (USB slave). Tempo widget viewer side fixé séparément par utilisateur dans le worktree `viewer-juce` (switch `clockSource == internal ? tempoBpm : externalBpm` dans le header component).

**Post-impl fixes non-prévus dans le plan** (acquis HW discovery) :
- PRIO_LOW/PRIO_HIGH au lieu de LOW/HIGH (collision macros Arduino `esp32-hal-gpio.h:41-42`)
- Pas de `Serial.setTxBufferSize` (existe sur HWCDC, pas sur USBCDC — projet utilise USBCDC via `ARDUINO_USB_MODE=0`). TX ring TinyUSB reste au default sdkconfig.
- Chunked write avec timeout 5s ultimate + stream resync `\n` (au lieu de drop-after-100ms initial)
- Seed atomique `s_viewerConnected.store((bool)Serial)` à la fin de `viewer::begin()` (race au boot)
- `[CLOCK] BPM=` debounce double : delta ≥1 BPM ET throttle ≥200ms

## Viewer bidirectionnel Phase 2 firmware — historique commits (2026-05-17)

Spec : [docs/superpowers/specs/2026-05-17-viewer-bidirectional-phase2-design.md](docs/superpowers/specs/2026-05-17-viewer-bidirectional-phase2-design.md) (838 lignes, validée + cross-audit fixes commits `1d65f9d` + `74b5fa6`).
Plan : [docs/superpowers/plans/2026-05-17-viewer-bidirectional-phase2-firmware-plan.md](docs/superpowers/plans/2026-05-17-viewer-bidirectional-phase2-firmware-plan.md) (2219 lignes, 23 tasks).
Spec viewer parallèle (`viewer-juce`) : `../ILLPAD_V2-viewer/docs/2026-05-17-viewer-juce-phase2-bidirectional-spec.md` (commits viewer `6425b27` + `b18e586` lock indicator).

| Phase | Task | Commit | Description |
|---|---|---|---|
| 2.A | 2 | `d1de1f6` | NvsManager : private members (Settings/BankType dirty + pending + debounce + _loadedBankType) + constructor init |
| 2.A | 3 | `6cfb276` | NvsManager : getLoadedBankType / setLoadedBankType API |
| 2.A | 4 | `5a63857` | NvsManager : populate _loadedBankType in loadAll (success + defaults) |
| 2.A | 5 | `db4eba0` | NvsManager : queueSettingsWrite / queueBankTypeFromCache (debounced 500ms) |
| 2.A | 6 | `d7f3b00` | NvsManager : saveSettings + saveBankType + commitAll extension |
| 2.A | 7 | `9b4b7d2` | NvsManager : extend tickPotDebounce EN TÊTE (anti-bug B1 audit) |
| 2.A | 8 | `d4d3e54` | NvsManager : rename tickPotDebounce → tickDebounce + main.cpp call site |
| 2.B | 10 | `e6420a3` | ViewerSerial : emitBankSettings(bankIdx) for ARPEG_GEN banks |
| 2.B | 11 | `672eff3` | ViewerSerial : emit [BANK_SETTINGS] in auto-resync + ?STATE/?BOTH/?ALL |
| 2.B | 11 fix | `016f287` | ViewerSerial : fix omission Task 11 — boot dump explicit main.cpp manquait emitBankSettings (HW gate G1 discovery) |
| 2.B | 13 | `bb25b94` | ViewerSerial : cmdBuf 16→24 chars + s_cmdOverflow flag + emit too_long |
| 2.B | 14 | `baf6381` | ViewerSerial : dispatchWriteCommand skeleton + handler stubs |
| 2.B | 15 | `9249ce8` | ViewerSerial : handleClockMode impl (parse master|slave + emit [SETTINGS]+[CLOCK] Source) |
| 2.B | 16 | `8d5d6a2` | ViewerSerial : handleArpGenParam impl (4 ARPEG_GEN args BONUS/MARGIN/PROX/ECART) |

**HW Checkpoints** :
- **Phase 2.A** validé 2026-05-17 — boot + Tool 5 / Tool 8 non-régression OK (confirmation user "ok" sur boot trace).
- **G1** validé 2026-05-17 — boot dump `[BANK_SETTINGS] bank=N bonus=X margin=Y prox=Z ecart=W` émis pour chaque bank ARPEG_GEN (3 + 4 sur instrument courant, valeurs reflètent Tool 5 settings).
- **G2-G7 EN ATTENTE** — dépendent du viewer-juce Phase 2 fonctionnel (parser `[BANK_SETTINGS]` + `[ERROR] cmd=` + TX path via `OutputQueue`). Possible aussi via terminal serial brut (envoi `!CMD=val\n` manuel) — non testé.

**Finding discovery HW gate G1** : le plan Task 11 a omis 1 site (boot dump explicit dans `main.cpp:646-651`, distinct de l'auto-resync de `taskBody()` qui ne kick PAS quand Serial est connecté au boot — la seed atomic `s_viewerConnected.store((bool)Serial)` empêche la transition false→true). Fix appliqué commit `016f287`. À noter pour audits futurs : le plan était précis sur 3 sites (auto-resync + ?BOTH/?ALL + ?STATE) mais le 4e site était hors radar.

**Bug spawn-task ouvert** : Tool 7 PotMapping setup affiche stale values vs runtime (probable struct version bump non propagé). Antérieur à Phase 2 viewer. À debugger séparément.

## Follow-ups ouverts

- **Viewer Phase 2 impl** : à coder sur branche `viewer-juce` selon la spec déjà validée. ~150 lignes JUCE (parser + Model + CommandSender + UI). Estimation 4-6h dev incluant tests Catch2.
- **HW gates G2-G7 firmware** : à exécuter post-viewer Phase 2 OU manuellement via terminal serial pour validation isolée. Liste détaillée plan §G2-G7.
- **Tool 7 PotMapping bug** : spawn-task séparée (cf section ci-dessus).
- **Progrès LOOP** (orchestration légère, jalons restants) : [docs/superpowers/LOOP_PROGRESS.md](docs/superpowers/LOOP_PROGRESS.md). **Étape courante : Phase 2 LOOP à rédiger from scratch** (Refacto Tool 5 livré 2026-05-17, prêt pour LOOP runtime). À enchaîner après viewer Phase 2 + HW gates G2-G7.

## Sources

- Spec ARPEG_GEN : [docs/superpowers/specs/2026-04-25-arpeg-gen-design.md](docs/superpowers/specs/2026-04-25-arpeg-gen-design.md)
- Spec LOOP : [docs/superpowers/specs/2026-04-19-loop-mode-design.md](docs/superpowers/specs/2026-04-19-loop-mode-design.md) (VALIDÉE, MAJ 2026-05-17 — Q6 inversée post refacto Tool 5)
- Spec Tool 5 refacto (pré-Phase 2 LOOP) : [docs/superpowers/specs/2026-05-17-tool5-bank-config-refactor-design.md](docs/superpowers/specs/2026-05-17-tool5-bank-config-refactor-design.md) (VALIDÉE 2026-05-17)
- Spec viewer serial centralization Phase 1 : [docs/superpowers/specs/2026-05-17-viewer-serial-centralization-design.md](docs/superpowers/specs/2026-05-17-viewer-serial-centralization-design.md) (CLOSE 2026-05-17)
- Plan viewer serial Phase 1 firmware : [docs/superpowers/plans/2026-05-17-viewer-serial-phase1-firmware-plan.md](docs/superpowers/plans/2026-05-17-viewer-serial-phase1-firmware-plan.md) (EXÉCUTÉ 2026-05-17)
- Spec parser viewer (cross-worktree `viewer-juce`) : `../ILLPAD_V2-viewer/ILLPADViewer/docs/firmware-viewer-protocol.md` (mis à jour 2026-05-17 commit viewer-juce `b3079ce`)
- Progrès LOOP (jalons restants) : [docs/superpowers/LOOP_PROGRESS.md](docs/superpowers/LOOP_PROGRESS.md) (suivi léger, MAJ par jalon)
- Invariants buffer LOOP : [docs/reference/loop-buffer-invariants.md](docs/reference/loop-buffer-invariants.md) (extrait des Parties 8-9 du gesture-dispatcher-design archivé)
- Plan ARPEG_GEN (archivé) : [docs/archive/2026-04-26-arpeg-gen-plan.md](docs/archive/2026-04-26-arpeg-gen-plan.md)
- Plan LOOP Phase 1 (archivé) : [docs/archive/2026-04-21-loop-phase-1-plan.md](docs/archive/2026-04-21-loop-phase-1-plan.md)
- Rapport audit LOOP spec 8 tranchages (archivé) : [docs/archive/rapport_audit_loop_spec.md](docs/archive/rapport_audit_loop_spec.md)
- Branche `loop` archivée (référence Phase 1 + Phase 2 ~90 %) : tag `loop-archive-2026-05-16` → `b79d03b`
- Arp reference (incl. §13 Generative mode) : [docs/reference/arp-reference.md](docs/reference/arp-reference.md)
- NVS reference (BankTypeStore v4, ArpPotStore v1) : [docs/reference/nvs-reference.md](docs/reference/nvs-reference.md)
- Project CLAUDE.md : [.claude/CLAUDE.md](.claude/CLAUDE.md) — invariants, build, conventions, VT100 policy, setup boot-only
