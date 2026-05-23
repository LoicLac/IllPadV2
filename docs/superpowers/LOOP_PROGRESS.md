# LOOP — Progrès et étapes restantes

_Doc de suivi léger. **Pas un plan figé**. Mis à jour à chaque jalon
franchi. Détails dans les specs et plans dédiés (référencés dans le
tableau)._

**Dernière maj** : 2026-05-19 (Master Sync + OD-Sync **CLOSE** — Phase 2 LOOP CLOSE en base, deux pivots musicaux livrés single-session 2026-05-19).

---

## Vue d'ensemble

LOOP = 4e type de bank ILLPAD V2 (loop percussif, jusqu'à `MAX_LOOP_BANKS = 4`
banks simultanées). **Statut** : Phase 1 + Refacto Tool 5 + Phase 2 + Master
Sync + OD-Sync CLOSE post commits `33149b8` (2026-05-19). **Reste à livrer** :
Phase 3 (Tool 3 b1 setup + Tool 4 ext refus collision LOOP control) puis
Phases 4-6.

## Tableau d'étapes

| Étape | Statut | Sortie attendue | Spec | Plan |
|---|---|---|---|---|
| Phase 1 LOOP | ✅ **CLOSE** (commits `a84c955`→`2624b12`) | Enum `BANK_LOOP=2`, `LoopPadStore`+`LoopPotStore` NVS, guards défensifs (BankManager double-tap LOOP consume + ScaleManager early-return), LED stub `renderBankLoop`, EVT_WAITING mode-invariant | [spec LOOP §27 P1](specs/2026-04-19-loop-mode-design.md) | [archive](../archive/2026-04-21-loop-phase-1-plan.md) |
| Refacto Tool 5 | ✅ **CLOSE** (commits `69e19ed`→`e2857c5`) | Tableau matriciel Tool 5 (banks×params, nav 2D, INFO 3 états, `PARAM_TABLE` déclarative), validator `quantize[]` discriminé par type (ARP 0..1 / LOOP 0..2), labels NORM/ARP_N/LOOP/ARP_G, INFO long form, `MAX_LOOP_BANKS = 2`, pas de bump NVS. Task 4 no-op (helpers NvsManager utilisés par boot path). | [spec Tool 5 refacto](specs/2026-05-17-tool5-bank-config-refactor-design.md) | [plan](plans/2026-05-17-tool5-bank-config-refactor-plan.md) |
| Phase 2 LOOP | ✅ **CLOSE** (commits `6c0b4d8` → `284bec4`, HW gates G1-G9 validés 2026-05-19) | `LoopEngine` state machine 7 états (EMPTY/RECORDING/PLAYING/OVERDUBBING/STOPPED + WAITING_PLAY/WAITING_STOP), recording µs + bar-snap 25 % deadzone (depuis remplacée par Master Sync), playback BPM-scaled intégration incrémentale (B1), refcount noteOn/Off, `processLoopMode` dispatch BANK_LOOP, overdub merge atomique (M4) + Q5 STOPPED-loaded REC = PLAYING+OVERDUB (depuis remplacé par OD-Sync immediate-merge), quantize Beat/Bar via ClockManager, LedController renderBankLoop state-driven, BankManager double-tap LOOP + toggleAllArpsAndLoops + midiPanic flush + M9 bank switch guard + audit-fix B-N1/B-N2/R-N1 (`onBackgroundTransition`). **★ Premier son MIDI LOOP audible HW commit `d345f01` (gate G5).** | [spec LOOP §27 P2 — CLOSE](specs/2026-04-19-loop-mode-design.md) + plan archivé : [`archive/2026-05-18-loop-phase-2-plan.md`](../archive/2026-05-18-loop-phase-2-plan.md) |
| Master Sync | ✅ **CLOSE** (commits `89f6c11` ClockManager + `edbdd2b` LoopEngine + `a7a461a` doc-sync, HW gates G1-G9 validés 2026-05-19) | Pivot algorithmique post-audit musical : remplace bar-snap+rescale destructeur par **Auto-Stop boundary-aware** + master grid anchor. ARP+LOOP+LOOP cross-bank désormais sur la **même grille master clock** (ARP Beat + LOOP BEAT/BAR synchronisés). `_recordStartUs` ancré à `lastBeatWallTime`/`lastBarWallTime` selon quantize. `tapRec` sur RECORDING entre en `_recordingPendingClose` (flag), capture continue jusqu'au boundary master tick, puis `commitRecordingClose` atomic. FREE = explicitement hors grille (tap-to-tap strict). `stopRecording` entière supprimée. 9 décisions brainstorm BS-1 à BS-9 ancrées dans la spec. | [spec Master Sync](specs/Illpad_Master_Sync.md) + plan archivé : [`archive/2026-05-19-master-sync-implementation-plan.md`](../archive/2026-05-19-master-sync-implementation-plan.md) + design pivot archivé : [`archive/2026-05-19-loop-algo-pivot-design.md`](../archive/2026-05-19-loop-algo-pivot-design.md) |
| **OD-Sync** | ✅ **CLOSE** (commits `eaf5674` C1 buffer/helpers + `fc2ff9b` C2 immediate-merge + `33149b8` C3 diff swap+CLEAR dispatch+LED Option β, HW gates G1-G15 validés 2026-05-19) | Pivot algorithmique post-audit musical, complément Master Sync : **immediate-merge overdub** + **snapshot 1-level Undo/Redo toggle** avec diff swap musical par note (préserve couche base + live press par construction). Pendant OD, pad presses insérés direct dans `_events[]` → loop growth live audible au cycle suivant. Tap REC → exit commit (B-N2 logic réincarnée). Tap CLEAR pendant OD → Cancel (swap + state PLAYING). Tap CLEAR court pendant PLAYING/STOPPED → Undo/Redo toggle (geste signature K+SN+HH mute layer live). Long-press CLEAR pendant PLAYING/STOPPED → wipe étendu (reset alternate). LED Option β = réutilisation EVT_LOOP_CLEAR / EVT_STOP / EVT_PLAY existants (events dédiés EVT_LOOP_OD_* différés Phase 4). Suppression `_overdubEvents[128]` + `mergeOverdub` + `abandonOverdub`. Ajout `_eventsAlternate[1024]` (snapshot) + `g_swapTemp[1024]` static global (B1 fix post-review, anti stack overflow Arduino-ESP32 default 8 KB). 16 décisions BS+OD-1 à OD-16 ancrées dans la spec. | [spec OD-Sync](specs/Illpad_OD_Sync.md) + plan archivé : [`archive/2026-05-19-od-sync-implementation-plan.md`](../archive/2026-05-19-od-sync-implementation-plan.md) |
| Phase 3 LOOP | 🔄 Spec refondue 2026-05-23, plan d'impl à venir | **Tool PAD ROLE** (fusion Tool 3 + Tool 4 en 4 pages BANK/ARPEG/LOOP/CC, règle unique ABSORBANT/CONTEXTUEL). Code 3.A/3.B/3.C livré sur main, conservé. | [tool-pad-role-design.md](specs/2026-05-23-tool-pad-role-design.md) | — (session suivante) |
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
| [`specs/2026-04-19-loop-mode-design.md`](specs/2026-04-19-loop-mode-design.md) | Spec LOOP parent (design haut niveau, invariants, §27 Phase 1-6) — amendée 2026-05-19 Master Sync + OD-Sync |
| [`specs/Illpad_Master_Sync.md`](specs/Illpad_Master_Sync.md) | Spec Master Sync self-suffisante (Auto-Stop close-record + master grid anchor) |
| [`specs/Illpad_OD_Sync.md`](specs/Illpad_OD_Sync.md) | Spec OD-Sync self-suffisante (immediate-merge + snapshot 1-level Undo/Redo) |
| [`specs/2026-05-17-tool5-bank-config-refactor-design.md`](specs/2026-05-17-tool5-bank-config-refactor-design.md) | Spec refacto Tool 5 (D1-D12) |
| [`../reference/loop-buffer-invariants.md`](../reference/loop-buffer-invariants.md) | Invariants buffer LOOP (anti-patterns ARPEG→LOOP, étendu OD-Sync swap diff) |
| [`../../STATUS.md`](../../STATUS.md) | Focus courant projet (LOOP + ARPEG_GEN + viewer) |
| [`../archive/`](../archive/) | Historique : ancienne loop branch, plans exécutés (Phase 2 LOOP + Master Sync + OD-Sync), design pivot, LOOP_ROADMAP archivé. **Ne pas lire sauf demande explicite.** |

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
