# LOOP — Progrès et étapes restantes

_Doc de suivi léger. **Pas un plan figé**. Mis à jour à chaque jalon
franchi. Détails dans les specs et plans dédiés (référencés dans le
tableau)._

**Dernière maj** : 2026-05-17 (Refacto Tool 5 livré, commits `69e19ed` → `e2857c5`)

---

## Vue d'ensemble

LOOP = 4e type de bank ILLPAD V2 (loop percussif, jusqu'à `MAX_LOOP_BANKS`
banks simultanées). Phase 1 (skeleton + stores NVS + guards défensifs)
close depuis commit `2624b12`. Reste à livrer : Refacto Tool 5 (pré-requis
UI propre), puis Phases 2-6 LOOP runtime.

## Tableau d'étapes

| Étape | Statut | Sortie attendue | Spec | Plan |
|---|---|---|---|---|
| Phase 1 LOOP | ✅ **CLOSE** (commits `a84c955`→`2624b12`) | Enum `BANK_LOOP=2`, `LoopPadStore`+`LoopPotStore` NVS, guards défensifs (BankManager double-tap LOOP consume + ScaleManager early-return), LED stub `renderBankLoop`, EVT_WAITING mode-invariant | [spec LOOP §27 P1](specs/2026-04-19-loop-mode-design.md) | [archive](../archive/2026-04-21-loop-phase-1-plan.md) |
| Refacto Tool 5 | ✅ **CLOSE** (commits `69e19ed`→`e2857c5`) | Tableau matriciel Tool 5 (banks×params, nav 2D, INFO 3 états, `PARAM_TABLE` déclarative), validator `quantize[]` discriminé par type (ARP 0..1 / LOOP 0..2), labels NORM/ARP_N/LOOP/ARP_G, INFO long form, `MAX_LOOP_BANKS = 2`, pas de bump NVS. Task 4 no-op (helpers NvsManager utilisés par boot path). | [spec Tool 5 refacto](specs/2026-05-17-tool5-bank-config-refactor-design.md) | [plan](plans/2026-05-17-tool5-bank-config-refactor-plan.md) |
| Phase 2 LOOP | ⏳ **À rédiger** from scratch (post Refacto Tool 5) | `LoopEngine` (state machine EMPTY/RECORDING/PLAYING/OVERDUBBING/STOPPED + WAITING_*), recording µs, playback scalé BPM, refcount, `processLoopMode` + `handleLoopControls` main wiring. Premier son MIDI LOOP | [spec LOOP §27 P2](specs/2026-04-19-loop-mode-design.md) + [invariants buffer](../reference/loop-buffer-invariants.md) | — |
| Phase 3 LOOP | ⏳ Pending | **Tool 3 b1 refactor** (3 sous-pages Banks/ARPEG/LOOP, 5 règles collision) + **Tool 4 ext** (refus ControlPad sur pad LOOP control, bi-directionnel) | [spec LOOP §5 + §27 P3](specs/2026-04-19-loop-mode-design.md) | — |
| Phase 4 LOOP | ⏳ Pending | **PotRouter 3 contexts** + **Tool 7 ext** (page LOOP, sans `t`) + **LED wiring complet** (`renderBankLoop` complet + `EVT_LOOP_*` patterns + `consumeBarFlash`/`consumeWrapFlash`) | [spec LOOP §22 + §27 P4](specs/2026-04-19-loop-mode-design.md) | — |
| Phase 5 LOOP | ⏳ Pending | **Effets** : shuffle templates (shared ARPEG), chaos re-seed, velocity patterns (4 LUTs), vel pattern depth. Câblage `LoopPotStore` runtime | [spec LOOP §10 + §27 P5](specs/2026-04-19-loop-mode-design.md) | — |
| Phase 6 LOOP | ⏳ Pending | **Slot Drive** : partition LittleFS 512 KB, format `LoopSlot` binaire, serialize/deserialize, `handleLoopSlots` (load/save/delete), Tool 3 slot section (16 slot pads hold-left) | [spec LOOP §4, §11-§13 + §27 P6](specs/2026-04-19-loop-mode-design.md) | — |

## Ordre / dépendances

```
Refacto Tool 5  (pré-requis UI propre)
    │
    ▼
Phase 2 LOOP  (LoopEngine — premier son LOOP)
    │
    ▼
Phase 3 LOOP  (Tool 3 b1 + Tool 4 ext — config pads LOOP)
    │
    ▼
Phase 4 LOOP  (PotRouter + Tool 7 + LED wiring)
    │
    ▼
Phase 5 LOOP  (Effets — shuffle/chaos/vel)
    │
    ▼
Phase 6 LOOP  (Slot Drive LittleFS)
```

Dépendances dures : chaque phase suppose la précédente livrée HW-validée.

## Sources actives

| Ressource | Rôle |
|---|---|
| [`specs/2026-04-19-loop-mode-design.md`](specs/2026-04-19-loop-mode-design.md) | Spec LOOP VALIDÉE (design haut niveau, invariants, §27 Phase 1-6) |
| [`specs/2026-05-17-tool5-bank-config-refactor-design.md`](specs/2026-05-17-tool5-bank-config-refactor-design.md) | Spec refacto Tool 5 (D1-D12) |
| [`../reference/loop-buffer-invariants.md`](../reference/loop-buffer-invariants.md) | Invariants buffer LOOP (anti-patterns ARPEG→LOOP, checklist pré-impl) |
| [`../../STATUS.md`](../../STATUS.md) | Focus courant projet (LOOP + ARPEG_GEN + viewer) |
| [`../archive/`](../archive/) | Historique : ancienne loop branch, P2 archive-based jeté, LOOP_ROADMAP archivé |

## Légende statuts

- ✅ **CLOSE** — livré sur main, HW validé
- 📋 **Plan rédigé** — code à exécuter
- ⏳ **Pending** — pas encore commencé (spec OK, plan à rédiger)
- 🔄 **En cours** — partiellement livré (rare, à éviter)

## Politique de mise à jour

1. **Quand mettre à jour** : au passage de chaque jalon (statut change) ou si une nouvelle étape s'insère.
2. **Quoi ajouter** : le statut, le commit de référence, et le lien plan si rédigé entre temps.
3. **Quoi NE PAS ajouter** :
   - Historique cumulatif de décisions (vit dans les specs/plans).
   - "Découvertes session N", "Q1-QN actées", "P1-PN pendantes" — c'était la dérive de l'ancien LOOP_ROADMAP archivé. Refus explicite.
   - Tasks détaillées (vivent dans les plans dédiés).
   - Workflow d'orchestration multi-sessions, §0 environment check, etc.
4. **Si une phase est repensée fondamentalement** (comme Phase 2 archive-based qu'on a jetée 2026-05-17) : noter le changement bref + lien archive. Ne pas raconter l'histoire.
5. **Garder ≤ 1-2 pages**. Si ça grossit, c'est qu'on accumule — élaguer.

---

**Référence courte pour démarrer une session LOOP** : lire ce fichier (vue d'ensemble + tableau) → identifier l'étape courante → ouvrir la spec et le plan correspondants. C'est tout. Pas besoin de re-naviguer un historique multi-sessions.
