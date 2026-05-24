# Plan d'implémentation — Tool PAD ROLE

**Date début rédaction** : 2026-05-23
**Status** : ✅ **Iter 2 complète + audit adversarial intégré** — Toutes les phases 3.A à 3.I détaillées. **9 corrections audit actées** (5 bloquants B-N + 4 majeurs cruciaux) cristallisées dans §12.4-§12.12. Prochaine étape : **iter 3** (vérification mécanique snippets vs code réel par sub-agents — line numbers, signatures, presence helpers).
**Spec source-of-truth** : [`specs/2026-05-23-tool-pad-role-design.md`](../specs/2026-05-23-tool-pad-role-design.md) (18 sections, validée 2026-05-23)
**Code de base acquis sur `main`** :
- `002400c` (Phase 3.A spec ancienne — helpers cross-store inline `KeyboardData.h`)
- `cd3b3c9` (Phase 3.B spec ancienne — Tool 4 ext refus collision LOOP control)
- `97db63a` (Phase 3.C spec ancienne — Tool 3 TAB nav + sous-page NORM + `_setFlash` infra)

> ⚠️ Les commits dits "3.A/3.B/3.C" du livré conservé portent un nommage **différent** du découpage de ce plan (qui repart en 3.A nouveau = rename `holdPad`). À ne pas confondre.

**Approche du plan** : itérations par couches successives. À chaque passe, raffinement + mini-audit faisabilité + fractionnement si une phase devient trop dense. Snippets ancrés vs code réel viendront en iter 3 (vérification mécanique par sub-agents).

**Cross-refs disciplinaires** :
- [`SESSION_PROTOCOL.md`](../SESSION_PROTOCOL.md) — 9 règles strictes session EXEC, templates manifest + checkpoint + commit gate
- [`.claude/CLAUDE.md`](../../../.claude/CLAUDE.md) — invariants projet, build pio, zero-migration NVS policy, VT100 standard non-négociable
- `~/.claude/CLAUDE.md` — préférences user globales (non-complaisance audit, scope strict, pas de footer Co-Authored-By)

---

## §-1 Index

Navigation pour amendements futurs.

- [§0 Contexte et objectif](#§0-contexte-et-objectif)
- [§1 Décisions globales (iter 1)](#§1-décisions-globales-iter-1)
- [§2 Récap sous-phases × HW gates × volumes](#§2-récap-sous-phases--hw-gates--volumes)
- [§3 Phase 3.A — Rename + bump NVS](#§3-phase-3a--rename--bump-nvs) — détaillée
- [§4 Phase 3.B — Squelette 4 pages](#§4-phase-3b--squelette-4-pages) — détaillée
- [§5 Phase 3.C — Page CC](#§5-phase-3c--page-cc-absorption-tool-4) — détaillée (3.C.1a + 3.C.1b + 3.C.2)
- [§6 Phase 3.D — Page BANK](#§6-phase-3d--page-bank) — détaillée (3.D.1 + 3.D.2)
- [§7 Phase 3.E — Page ARPEG](#§7-phase-3e--page-arpeg-complète) — détaillée (3.E.1 + 3.E.2 + 3.E.3)
- [§8 Phase 3.F — Page LOOP](#§8-phase-3f--page-loop-complète) — détaillée (3.F.1 + 3.F.2 + 3.F.3)
- [§9 Phase 3.G — Modale d'écrasement](#§9-phase-3g--modale-décrasement) — détaillée (3.G.1 + 3.G.2)
- [§10 Phase 3.H — Palette + finitions](#§10-phase-3h--palette--finitions) — détaillée (3.H.1 + 3.H.2)
- [§11 Phase 3.I — Doc-sync](#§11-phase-3i--doc-sync-clôture-phase-3-loop) — détaillée (3.I.1 + 3.I.2 + 3.I.3)
- [§12 Audit findings et révisions rétroactives](#§12-audit-findings-et-révisions-rétroactives)
- [§13 Placeholders / macros à raffiner en 3.H.1](#§13-placeholders--macros-à-raffiner-en-3h1)
- [§14 Anti-régression cross-phase](#§14-anti-régression-cross-phase)
- [§15 Conventions defaults par page](#§15-conventions-defaults-par-page) — nouveau passe 4

---

## §0 Contexte et objectif

### §0.1 Périmètre

Tool PAD ROLE est la refonte de la configuration des rôles de pad en setup mode VT100. Il **fusionne** Tool 3 (Pad Roles : banks, scale, ARPEG modificateurs, hold pad) et Tool 4 (Control Pads : ControlPads MIDI CC) en **un seul module** à **4 pages** navigables via TAB : `BANK / ARPEG / LOOP / CC`.

Il consomme la spec [`2026-05-23-tool-pad-role-design.md`](../specs/2026-05-23-tool-pad-role-design.md) qui pose :
- Un **concept unique** : deux classes de rôles ABSORBANT et CONTEXTUEL.
- Une **règle unique de compatibilité** §4 : un pad est valide ssi (un seul ABSORBANT) ou (rôles CONTEXTUELs aux ACs disjoints).
- **Propagation immédiate cross-page** §6.6 : état de travail unique en mémoire, NVS commit unique à la sortie.
- Un **layout par page** §7 §8 §9 avec cell display, info panel langue musicien, pool assignable vert menthe.
- Une **modale d'écrasement** §10 quand un absorbant veut occuper un pad portant des contextuels.

### §0.2 État du livré conservé

Trois commits **acquis sur `main`** préservent une partie du travail spec ancienne. Pas de rollback ; le plan étend et adapte :

| Commit | Apport | Survit dans Tool PAD ROLE |
|---|---|---|
| `002400c` | Helpers cross-store inline `KeyboardData.h` (`findBankIdxForPad`, `scaleRoleAtPad`, `arpRoleAtPad`, `isLoopControlPad`, `findLoopSlotIdx`, `findControlPadEntryIdx`), `NvsManager::getLoadedLoopPadStore` + setter + `saveLoopPad` | Helpers réutilisés tels quels par le helper `_padNeighborInfo` créé en 3.C.2. Signature `arpRoleAtPad` retouche en 3.A (param `holdPad → arpPlayStopPad`). |
| `cd3b3c9` | Tool 4 extension : refus assignement ControlPad sur pad LOOP control. Dev seed M7 déplacé pre-setup gate. | Code de refus migre en page CC absorbée. Dev seed M7 retiré en 3.H.2 (UI propre livrée). |
| `97db63a` | Tool 3 TAB nav (3 sous-pages stub) + `_setFlash` infra + sous-page NORM (bank slot move + refus collision destination cross-store) | TAB nav étendu à 4 pages en 3.B. `_setFlash` réutilisé tel quel. Sous-page NORM → page BANK incarnée en 3.D. Refus cross-store dans bank slot move conservé. |

Renommages techniques imposés :
- `SUB_NORM → SUB_BANK` (cosmétique, 3.B)
- Ajout `SUB_CC` (3.B)
- `holdPad → arpPlayStopPad` (~50 sites, bump `ARPPAD_VERSION = 2 → 3`, 3.A) — orthogonal à la fusion, isolé en première phase pour servir de filet de sécurité runtime ARPEG.

### §0.3 Méthode de plan adoptée

Le plan suit une approche **par couches successives de précisions** :

1. **Iter 1** — squelette grosse maille : décisions structurantes (organisation code, séquence des phases, position du rename). Aboutit à **20 sous-phases / 8 HW gates G0-G7**.
2. **Iter 2** — détail par passes, une grappe de sous-phases à la fois : sites concrets vs code lu, patches par fichier, hard-asserts, critères HW gate, mini-audit faisabilité, décisions à acter. Si une phase déborde (cible ≤500 L touchées), fractionner. Si une décision pré-mûre apparaît contradictoire, le mini-audit la lève (cf §12 audit findings).
3. **Iter 3** *(à venir)* — vérification mécanique snippets vs code réel par sub-agents (line numbers, signatures, presence des helpers, etc.) avant exécution. Le sub-agent n'a pas accès au chat — il vérifie contre des refs ancrées dans ce doc.

### §0.4 Pourquoi ce doc existe

Le chat session est volatil. Une perte de session ou une compression de contexte évaporerait les ~26 décisions actées et 5 révisions rétroactives. Ce doc cristallise l'état du plan pour :
- Permettre une **audit indépendant** post-iter 2 (sub-agent contradictoire) sur la base écrite, pas sur le chat.
- Permettre l'**exécution future** (session EXEC) de lire un plan stable avec tous les arbitrages.
- Servir de **trace pour relectures futures** ("pourquoi ce choix en page BANK ?").
- Détecter **dérives entre passes** : la rédaction écrite force la cohérence et débusque les contradictions latentes (palette map=5/6 détectée comme conflit en passe 3, révisée à map=7/8 — cf §12).

Le doc est **vivant** durant l'iter 2 : à chaque passe, j'incrémente ou révise les sections concernées + j'ajoute aux audit findings §12.

---

## §1 Décisions globales (iter 1)

Huit décisions structurantes tranchées en iter 1, avant tout détail par sous-phase. Elles fixent le cadre de toutes les passes iter 2.

### §1.1 — Fusion Tool 3 + Tool 4 en un module unique

**Décision** : Tool 4 absorbé par Tool 3, devient une page (page CC) du nouveau module `ToolPadRoles`.

**Rationale** : la fusion architecturale est cohérente avec la spec §6.1 (Tool PAD ROLE absorbe Tool 4). Évite la dispersion logique (helpers cross-store séparés cf §1.2 de la spec). Le module physique `ToolControlPads.{cpp,h}` disparaît en 3.C.1b.

### §1.2 — Organisation code : (c) sous-fichiers sans abstraction

**Décision** : 1 header `ToolPadRoles.h` + 5 fichiers `.cpp` :
- `ToolPadRoles.cpp` — orchestrateur (constructor, begin, run, drawScreen dispatch, _handleTab, _drawSubPageHeader, _setFlash, _drawFlash, saveAll, baselines)
- `ToolPadRoles_Bank.cpp` — page BANK
- `ToolPadRoles_Cc.cpp` — page CC (absorbe Tool 4)
- `ToolPadRoles_Arpeg.cpp` — page ARPEG
- `ToolPadRoles_Loop.cpp` — page LOOP

**Rationale** : fragmenter par page rend chaque phase EXEC modifier 1-2 fichiers de ~500-700 L au lieu d'un fichier monolithique de ~3000 L. Lecture VT100 ardue ; navigation par fichier réduit le coût cognitif. Découpage des phases naturel. Pas de scalabilité requise (4 pages connues, instrument livré final). Pas d'abstraction virtuelle (vtable inutile, pas de pattern précédent dans le projet). Coût initial = créer 4 nouveaux .cpp et étendre 1 header — borné.

**Alternative rejetée** : (a) monolithique 3000 L unique — VT100 lourd, search/replace large risqué, line numbers stale entre passes iter 2.

### §1.3 — Position rename `holdPad → arpPlayStopPad` : Phase 3.A (P-EARLY)

**Décision** : refacto pur en première phase, avant tout autre travail. ~50 sites cross-modules. Bump `ARPPAD_VERSION = 2 → 3`. NVS user invalidé, re-saisie pad PL/S au reboot suivant (zero-migration policy projet).

**Rationale** : isole le rename comme refacto attrapé intégralement par le compilateur. Sert de **filet de sécurité runtime ARPEG** — si quelque chose casse dans `handleHoldPad`, `setCaptured`, propagation `s_holdPad`, on le voit AVANT d'investir dans le squelette de la fusion. Toutes les phases suivantes héritent du nom canonique `arpPlayStopPad` sans dichotomie label-UI vs identifiant-code. Le sur-coût d'environ 10 sites dans `ToolPadRoles.cpp` qui seront refaits massivement après (transitoires) est minime vs le bénéfice cognitif.

**Alternative rejetée** : (P-LATE) rename en toute fin — codé tout le module avec `holdPad` legacy puis grand rename final → diff massif en fin de session fatiguée, contradiction wording UI vs identifiant pendant la majeure partie du plan, gate HW final risqué.

### §1.4 — Séquence d'incarnation des 4 pages : (S3) raffinée

**Décision** :
1. **3.A** Rename (prérequis, [G0])
2. **3.B** Squelette 4 pages (plomberie, [G1])
3. **3.C** Page CC (absorption pure, [G2])
4. **3.D** Page BANK (architecturale, [G3])
5. **3.E** Page ARPEG ([G4])
6. **3.F** Page LOOP ([G5])
7. **3.G** Modale d'écrasement cross-page + propagation §6.6 fine ([G6])
8. **3.H** Palette + finitions ([G7])
9. **3.I** Doc-sync (pas de HW)

**Rationale** : la page CC est traitée tôt comme **palier architectural** (absorption Tool 4) sans nouvelle UX → scope strict, test de non-régression Tool 4. Page BANK ensuite (extension naturelle du code 3.C livré). ARPEG + LOOP forment la paire CONTEXTUELS — coexistence cross-AC testable end-to-end en 3.F.3 quand les 2 pages existent. Modale d'écrasement en **phase dédiée 3.G** quand toutes les pages produisent les rôles à écraser → test end-to-end possible. Palette finale en 3.H quand tous les codes couleur ont été émis par les phases précédentes (audit cohérence visuelle global).

**Alternative rejetée** : (S1) ordre TAB strict BANK → ARPEG → LOOP → CC — CC en dernier absorbe Tool 4 tardivement, risque architectural concentré en fin de session.

### §1.5 — Retrait `_buildRoleMapLegacy` en Phase 3.H.2 (L1)

**Décision** : `_buildRoleMapLegacy` (helper de fallback hérité du livré 3.C) survit pendant tout le plan ; les 4 stubs `_buildRoleMapBank/Arpeg/Loop/Cc` créés en 3.B delegent à `_buildRoleMapLegacy` jusqu'à ce que chaque page incarne sa vraie impl. Quand la dernière page (LOOP en 3.F) remplace son stub, `_buildRoleMapLegacy` devient code mort → retiré en 3.H.2 dans la même phase que le retrait dev seed M7.

**Rationale** : pattern progressif — chaque phase remplace un stub par sa vraie impl, sans interrompre les autres. Le retrait final est groupé avec le ménage code mort. Pas de coût supplémentaire.

### §1.6 — Fractionnement final : 20 sous-phases / 8 HW gates G0-G7

**Décision** : phases groupées par HW gate pour permettre validation utilisateur entre paliers. Sous-fractionnement quand une phase déborde la cible ≤500 L touchées (3.C.1 fractionnée en 3.C.1a + 3.C.1b cf §12 audit). Voir [§2 récap](#§2-récap-sous-phases--hw-gates--volumes).

**Rationale** : steps vraiment vérifiables et safe (cf demande user iter 1.5). Chaque HW gate teste un ensemble cohérent de sous-phases. Diff inspection à granularité fine en cas d'échec.

### §1.7 — Stub coexistence cross-AC en 3.E.3 (test reporté 3.F.3)

**Décision** : la coexistence cross-AC ARPEG ↔ LOOP (pad porte Root D ARPEG + Slot 5 LOOP, §14.1 scenario A) est codée en 3.E.3 mais **non testée** car la page LOOP n'existe pas encore. Le test complet end-to-end intervient en 3.F.3 quand les deux pages sont incarnées.

**Rationale** : éviter un détour de structure (la page ARPEG ne peut pas réellement tester sa coexistence cross-AC sans la page LOOP). Le code stub est écrit selon spec §7.3, le test HW vient plus tard. Mini-audit en 3.F.3 valide le binôme.

### §1.8 — Placeholder modale en 3.C.2 et 3.D.2 = refus dur sans flash

**Décision** : tant que la modale d'écrasement 3.G n'est pas codée, les pages absorbantes (CC en 3.C.2, BANK en 3.D.2) qui devraient déclencher la modale §10 effectuent un **no-op silencieux** (refus dur sans flash) à la place. Cell display §8.1 + info panel signalent l'état du pad (■■ neutre ou ambre+ saturé), l'ENTER d'assignment ne fait simplement rien.

**Rationale** : pas de flash temporaire à coder puis retirer (anti dette). L'UX placeholder est austère mais conforme spec — focus refused = no action. La modale 3.G remplacera ce no-op par l'arbitrage interactif.

---

## §2 Récap sous-phases × HW gates × volumes

| Phase | Périmètre | HW gate | Volume L estimé |
|---|---|---|---|
| **3.A** | Rename `holdPad → arpPlayStopPad` + bump `ARPPAD_VERSION = 2 → 3` (~50 sites cross-modules) | **G0** | ~200 |
| **3.B** | Squelette 4 pages : ajout `SUB_CC`, rename `SUB_NORM → SUB_BANK`, switch 4 cas, header 4 labels, création 4 fichiers `.cpp` stubs delegating `_buildRoleMapLegacy` | **G1** | ~250 |
| **3.C.1a** | Migration mécanique Tool 4 → `ToolPadRoles_Cc.cpp` (renames `ToolControlPads::*`→`ToolPadRoles::*Cc`, members `_X`→`_ccX`, adaptation `_cursorPad`→`_gridRow*12+_gridCol`). **Non-câblé**, Tool 4 reste actif. | — | ~800 |
| **3.C.1b** | Câblage orchestrateur : dispatch `run()` sur `_ccUiMode` quand `_activeSubPage == SUB_CC`, `drawScreen` dispatch `_drawPageCc`, `begin()` étendu avec `BankSlot* banks`. Retrait Tool 4 du menu SetupManager. Suppression physique `ToolControlPads.{cpp,h}`. `TOOL_NVS_LAST[2] = 5` (T3 absorbe ControlPad descriptor). | — | ~300 |
| **3.C.2** | Cell display §8.1 colonne CC : helper `_padNeighborInfo` (cache cross-store), `_formatRoleNameMusician`, palette `GRID_CONTROLPAD` étendue map=7 (ambre+ saturé) / map=8 (neutre ■■), refus dur silencieux pour pads avec contextuels/BANK absorbant. | **G2** | ~200 |
| **3.D.1** | Page BANK essentielle : `_buildRoleMapBank`, `_drawPageBank` (grid + pool 1 ligne + info + control bar), `_handleEnterBank` (spec §7.4 strict : ENTER sur bank assignée = dégage direct), `_handleEnterPoolBank` (spec §9.2 : refus si entry déjà assignée, pas de silent steal). | — | ~350 |
| **3.D.2** | Hard-constraint exit §6.5 dans `run()` `NAV_QUIT` (globale, applicable depuis toutes pages), cell display §8.1 colonne BANK (CC absorbant ambre+, contextuels ■■), palette `GRID_ROLES` étendue map=7/8, info panel langue musicien §8.5. Refus dur silencieux pour tous les cas (placeholder modale 3.G). | **G3** | ~150 |
| **3.E.1** | Page ARPEG rendu : `_buildRoleMapArpeg` (5 rôles : Root × 7 / Mode × 7 / Chromatic / Octave × 4 / PL/S), `_drawPageArpeg` (grid + pool 4-5 lignes + info placeholder + control bar), couleurs spec §11.1 (Root pêche, Mode cyan, Octave pourpre, PL/S vert). | — | ~300-400 |
| **3.E.2** | Page ARPEG matrice édition §7.1 : assign / dégage / swap-to-pool intra-AC silencieux §7.2 (changement Root A pad X → pad Y, retour pool sans modale), refus absorbants ambre+ (BANK / CC). | — | ~250-300 |
| **3.E.3** | Page ARPEG coexistence stub cross-AC LOOP (codée mais non testable, cf §1.7) + info panel langue musicien complet ("Root C", "Mode Mixolydian", "Octave 2", "PL/S ARPEG") + reset global page §7.5. | **G4** | ~150-200 |
| **3.F.1** | Page LOOP rendu : `_buildRoleMapLoop` (REC / PL/S / CLR / Slots × 16), `_drawPageLoop` (grid + pool 3-4 lignes + info + control bar), couleurs §11.1 (REC rouge, CLR bleu foncé, PL/S vert même que ARPEG = geste unifié §14.1, Slots jaune). | — | ~300-400 |
| **3.F.2** | Page LOOP matrice édition §7.1 : swap intra-AC LOOP silencieux, refus absorbants, dégage, reset global. | — | ~250-300 |
| **3.F.3** | Coexistence cross-AC ARPEG ↔ LOOP testable end-to-end (pad porte Root D + Slot 5, cell verte/jaune dans pages respectives, info panel cross-page). Mini-audit binôme 3.E.3 / 3.F.3. | **G5** | ~150-200 |
| **3.G.1** | Modale d'écrasement implémentation : méthode membre commune `_handleOverwriteModale(uint8_t pad, ...)`, wording français langue musicien §10.2-10.3 (cas 1 rôle / 2 rôles / combinaisons via `_formatRoleNameMusician` extension verbose), parseConfirm y/n, effet y (rôles écrasés retournent aux pools respectifs avec vert menthe assignable). | — | ~150-200 |
| **3.G.2** | Wiring page BANK + page CC : remplacement du no-op silencieux placeholder (3.D.2, 3.C.2) par appel à `_handleOverwriteModale`. Tests propagation §6.6 fine (scenario §14.2 : place Root D + Slot 3 sur pad 22, TAB BANK, ENTER B5, modale, y, vérif propagation TAB ARPEG/LOOP). | **G6** | ~100-150 |
| **3.H.1** | Audits palette : teinte exacte ambre+ saturé §11.6 (vs ambient `#ffaa33`), curseur inverse fg/bg 4 chars, vert menthe vs vert PL/S §11.5 (lisibilité HW), cell width 4 vs 5 chars (décision finale vs code `SetupUI::drawCellGrid`). Ajustements `VT_BG_AMBER_SAT` / `VT_MINT_GREEN` / `VT_NEUTRAL_BAR` finalisés. | — | ~100-200 |
| **3.H.2** | Retrait dev seed M7 (`applyDevSeedLoopPadsIfSafe` + call site `main.cpp:410`), retrait `_buildRoleMapLegacy` (code mort, dernier consommateur retiré en 3.F.3), ménage code transitoire. | **G7** | ~50-100 |
| **3.I.1** | Doc-sync refs prioritaires (4 fichiers) : `setup-tools-conventions.md`, `vt100-design-guide.md`, `nvs-reference.md`, `arp-reference.md`. Refonte mentions Tool 3 / Tool 4 → Tool PAD ROLE 4 pages + ABSORBANT/CONTEXTUEL + règle unique §4. | — | ~doc |
| **3.I.2** | Doc-sync refs secondaires + spec parent : `runtime-flows.md`, `loop-buffer-invariants.md`, `architecture-briefing.md`, spec parent LOOP §5 cartouche refonte confirmée, `fonction_regen.md` (note `holdPad` rename, refonte HOLD ON/OFF différée §17.2 spec). | — | ~doc |
| **3.I.3** | Doc-sync `STATUS.md` focus + `LOOP_PROGRESS.md` tableau Phase 3 → CLOSE + archivage `docs/superpowers/specs/2026-05-19-loop-phase-3-design.md` + plan caduc + audits + EXEC prompt + manifest vers `docs/archive/` + suppression `HANDOFF-2026-05-20-loop-phase-3-spec-rework.md`. Décision : archiver ou supprimer `HANDOFF-2026-05-23-tool-pad-role-plan.md` (consommé par ce plan livré). | — | ~doc |

**Total estimé** : ~3000-4000 L code touchées (migration + nouveau code + adaptations) + ~doc-sync 10 fichiers. **20 sous-phases. 8 HW gates G0-G7.**

Comparaison ordre de grandeur Phase 2 LOOP : 38 tasks / 9 HW gates / 1 session EXEC plusieurs heures. Phase 3 PAD ROLE = volume comparable mais réparti sur 4 pages distinctes + une grosse migration architecturale.

---

## §3 Phase 3.A — Rename + bump NVS

### §3.1 Objectif

Refacto pur, atomique. Rename `holdPad → arpPlayStopPad` partout dans le code C++. Bump `ARPPAD_VERSION = 2 → 3` qui invalide silencieusement les NVS user existants (zero-migration policy projet). Le pad PL/S ARPEG est re-saisi par l'utilisateur au reboot suivant via Tool 3 actuel (sous-page NORM legacy, toujours en place avant 3.B).

Justification position 3.A : décidé en iter 1 §1.3 — refacto isolé qui sert de **filet de sécurité runtime ARPEG** avant d'investir dans le squelette de fusion.

### §3.2 Inventaire des sites (recensement complet via grep et lecture code)

| Fichier | Sites | Type |
|---|---|---|
| `KeyboardData.h` | 5 | enum `ArpRoleKind::HOLD` L611 → `PLAY_STOP` ; struct field `ArpPadStore.holdPad` L529 → `arpPlayStopPad` ; signature `arpRoleAtPad` L614 + body L617 ; validator `validateArpPadStore` L842-843 ; bump `ARPPAD_VERSION = 2 → 3` L523 |
| `ArpEngine.h` | 1 | signature `setCaptured(..., uint8_t holdPadIdx)` L112-113 + comment L108-111 |
| `ArpEngine.cpp` | 3 | signature impl L514-515 + comment L535-536 + `(void)holdPadIdx` L538 |
| `NvsManager.h` | 1 | signature `loadAll(... uint8_t& holdPad, ...)` L42-47 |
| `NvsManager.cpp` | 4 | impl signature L669-674 + bloc Arp control pads L932-946 (3 sites : `aps.holdPad`, lecture, trace serial) |
| `BankManager.h` | 2 | méthode `setHoldPad` L37-38 + field `_holdPad` L67 |
| `BankManager.cpp` | 4 | init `_holdPad(0xFF)` L21 + impl L53-54 + consommation `setCaptured(..., _holdPad)` L91 |
| `ScaleManager.h` | 2 | méthode L33 + field L53 → **SUPPRIMER** (orphan link §12.8) |
| `ScaleManager.cpp` | 2 | init L15 + impl L50-51 → **SUPPRIMER** (orphan link §12.8) |
| `SetupManager.h` | 1 | field L45 `_toolRoles → _toolPadRoles` (§12.7) |
| `SetupManager.cpp` | 2 | call `_toolRoles.begin(...)` L32, dispatch `case '3'` (`_toolRoles.run()`) (§12.7) |
| `main.cpp` | ~14 | `s_holdPad` L87 ; var locale L370 ; call `loadAll` L403 ; call `setupManager.begin` L458 ; sync `s_holdPad = holdPad` L635 ; `s_bankManager.setHoldPad` L642 ; `s_scaleManager.setHoldPad` L655 ; skip L765 + L894 ; `setCaptured(...)` L795 + L1129 + L1134 + L1176 ; fct `handleHoldPad` + body L1156-1181 ; call L1567 |
| `SetupManager.h` | 1 | signature `begin(... uint8_t& holdPad, ...)` L29 |
| `SetupManager.cpp` | 2 | impl signature L19 + propagation L34 vers `_toolPadRoles.begin(...)` |
| `ToolPadRoles.h` | 6 | `ROLE_HOLD = 5` L21 → `ROLE_PLAY_STOP = 5` + commentaires L27 L88 + signature `begin` L48 + pointer `_holdPad` L62 + working copy `_wkHoldPad` L70 + `POOL_HOLD_COUNT` L112 |
| `ToolPadRoles.cpp` | ~17 | symbol renames : `GRID_HOLD_LABELS` L33 + `POOL_HOLD_LABELS` L52 + constructor L62-69 + begin L93+L102 + poolLineSize/poolItemLabel L141+L152 + setRole L198 + getRoleForPad L276 + findPadWithRole L296 + assignRole L318 + clearRole L335 + clearAllRoles L346 + resetToDefaults L355 + saveAll L403+L407 + run() L651+L679. **Strings UI legacy conservées** : `" Hld"`, `"Hold:"`, wording `"HOLD toggle"` info panel — refondues en 3.E.1. |

**Total** : ~50-55 sites cumulés. Refacto pure intégralement attrapée par le compilateur.

### §3.3 Patches groupés (ordre suggéré, build casse jusqu'au dernier, commit unique)

| # | Fichier | Sites |
|---|---|---|
| 1 | `KeyboardData.h` | 5 (enum + struct + signature + validator + bump version) |
| 2 | `ArpEngine.{cpp,h}` | 4 (signature + body + comment) |
| 3 | `NvsManager.{cpp,h}` | 5 (signature + bloc Arp load + trace) |
| 4 | `BankManager.{cpp,h}` | 6 (méthode + field + init + consommation L91) |
| 5 | `ScaleManager.{cpp,h}` | 4+ (méthode + field + init + consommation `processScalePads` à compléter au grep pré-exécution) |
| 6 | `main.cpp` | ~14 |
| 7 | `SetupManager.{cpp,h}` | 3 |
| 8 | `ToolPadRoles.{cpp,h}` | ~23 (sites transitoires, strings UI legacy preserved) |

**Règle scope strict 3.A** : les labels UI affichés (`" Hld"`, `"Hold:"`, wording info panel `"HOLD toggle"`, `"HOLD OFF/ON"` descriptions) sont **conservés tels quels en 3.A**. Seuls les identifiants C++ (`_wkHoldPad`, `GRID_HOLD_LABELS`, `ROLE_HOLD`, etc.) sont renommés. La refonte des strings UI vers `"PL/S"` / `"PL/S ARPEG"` etc. est faite en 3.E.1 (page ARPEG incarnée).

### §3.4 Hard-asserts auto-review

```bash
# A. Aucun identifiant C++ holdPad résiduel (strings UI ignorées)
grep -rn -E "\b(holdPad|HoldPad|s_holdPad|_holdPad|_wkHoldPad|s_lastHoldPadState|setHoldPad|handleHoldPad|HOLD_LABELS|HOLD_COUNT|ROLE_HOLD|ArpRoleKind::HOLD)\b" src/
# attendu : 0 lignes

# B. Version NVS bumpée
grep "ARPPAD_VERSION" src/core/KeyboardData.h
# attendu : "const uint8_t ARPPAD_VERSION = 3;"

# C. Field NVS renommé
grep -E "\barpPlayStopPad\b" src/core/KeyboardData.h
# attendu : ≥ 4 matches (struct field + enum + signature + validator)

# D. ArpRoleKind::PLAY_STOP cohérent partout
grep -rn "ArpRoleKind::HOLD" src/
# attendu : 0 lignes
grep -rn "ArpRoleKind::PLAY_STOP" src/
# attendu : matches > 0
```

### §3.5 HW Gate G0

**Pré-requis** : instrument allumé, MIDI vers DAW pour confirmation.

**Procédure** :
1. Build clean (`~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`), 0 warning.
2. Upload (`pio run -t upload`), monitor (`pio device monitor -b 115200`).
3. Vérifier au boot : `[BOOT NVS] ArpPadStore raw/v0 detecte - reset v1 applique...` ou message équivalent (mismatch v2→v3 → defaults factory appliqués).
4. Entrer Tool 3 (sous-page NORM legacy). Pool, ligne "Hold:" → sélectionner "Hld", placer sur pad 23. Save. Observer en passant : badge T3 sur menu = état partiel OK (ArpPad reset post-bump). Reboot.
5. **Tool 4 non-régression check** (5.5 fusionné ici) : depuis menu setup, entrer Tool 4 → ENTER pad libre → MODE_PICK MOM → ENTER (assign CC0) → q exit. Tool 4 fonctionne comme avant.
6. Au boot suivant : `[BOOT NVS] Arp pads loaded (v3 store): arpPlayStop=23 oct=...` attendu.
7. Foreground bank ARPEG (Tool 5 si nécessaire). Jouer 2-3 notes (pile peuplée).
8. Tap pad 23 → moteur ARPEG démarre, arpège joue (DAW reçoit notes).
9. Tap pad 23 → moteur stop, pile préservée (DAW : pas de nouvelles notes). **(Allègement : step 10 retiré — `tap pad musical → relance step 0` est le fix F1 2026-05-15 stable, non touché par 3.A, vérification redondante.)**

**Critères** :
- ✓ Boot OK, logs serial NVS v3 cohérents
- ✓ Save+reload Tool 3 actuel persiste avec NVS v3
- ✓ ARPEG Play/Stop via pad PL/S fonctionne (toggle)
- ✓ Pile sacrée préservée au Stop (fix F1 du 2026-05-15)
- ✓ Pas de stuck note, pas de comportement bizarre dans `handleArpPlayStopPad`
- ✓ Tool 4 toujours fonctionnel (step 5 — suppression en 3.C.1b, pas en 3.A)

### §3.6 Mini-audit faisabilité

**Risques identifiés** :

1. **Volume sous-estimé en iter 1** : ~50-55 sites vs ~30 estimés. Pas catastrophique, refacto pur. Phase reste atomique.
2. **Wording UI legacy conservé en 3.A** : refacto symbol ≠ design label. Refonte des strings en 3.E.1. À documenter dans le commit message.
3. **Finding B-N3 non applicable** : cf [§12.3](#§123--b-n3-nvsmanager-pré-init-aps--vérifié-non-applicable-passe-1).
4. **Sites `ScaleManager.cpp::processScalePads` non greppés** : compléter le grep en pré-exécution :
   ```bash
   grep -n "_holdPad" src/managers/ScaleManager.cpp
   ```
   Si nouveaux sites détectés, étendre patch 5.

### §3.7 Décisions actées 3.A (enrichi audit pré-EXEC)

1. Rename atomique, commit unique en fin de phase. ~50-55 sites groupés en 8 patches par fichier.
2. Wording UI legacy preserved (`" Hld"`, `"Hold:"`, `"HOLD toggle"`) — refondu en 3.E.1.
3. Bump `ARPPAD_VERSION = 2 → 3`, NVS user invalidé, re-saisie pad PL/S au reboot.
4. HW Gate G0 valide runtime ARPEG (Play/Stop + pile sacrée fix F1).
5. Tool 4 reste fonctionnel pendant 3.A (suppression en 3.C.1b).
6. **Rename `_toolRoles → _toolPadRoles` dans SetupManager** (~5 sites) — cohérence sémantique avec nom du module. Cf §12.7.
7. **Suppression orphan link `ScaleManager::_holdPad`** (field + setter + call site `main.cpp:655`) au lieu de renommer. Corrige violation invariant 7. Cf §12.8.

---

## §4 Phase 3.B — Squelette 4 pages

### §4.1 Objectif

Étendre l'infrastructure 3.C livré pour supporter 4 pages au lieu de 3. Préparer les hooks pour les pages spécifiques à venir (3.C-3.F). Phase de plomberie : pas de nouvelle UX, juste la structure.

### §4.2 Patches

**Patch 1 — `ToolPadRoles.h`** :

a. Enum `SubPage` L31-37 — refonte avec rename + ajout :
```cpp
enum SubPage : uint8_t {
  SUB_BANK  = 0,   // 8 bank pads (was SUB_NORM in 3.C livré)
  SUB_ARPEG = 1,   // Root × 7, Mode × 7, Chromatic, Octave × 4, PL/S ARPEG
  SUB_LOOP  = 2,   // REC, PL/S, CLR, Slots × 16
  SUB_CC    = 3,   // CC MIDI ControlPads (Tool 4 absorbed)
  SUB_COUNT = 4
};
```

b. Enum `PadRoleCode` L15-23 — extensions :
```cpp
enum PadRoleCode : uint8_t {
  ROLE_NONE       = 0,
  ROLE_BANK       = 1,
  ROLE_ROOT       = 2,
  ROLE_MODE       = 3,
  ROLE_OCTAVE     = 4,
  ROLE_PLAY_STOP  = 5,    // was ROLE_HOLD (renamed in 3.A)
  ROLE_CC         = 6,    // new — for page CC cell display
  ROLE_COLLISION  = 0xFF
};
```

c. Ajout déclarations méthodes par page (interface anticipée pour phases 3.C-3.F) :
```cpp
// Per-page methods (defined in ToolPadRoles_Bank.cpp / _Cc.cpp / _Arpeg.cpp / _Loop.cpp)
void _buildRoleMapBank();
void _buildRoleMapCc();
void _buildRoleMapArpeg();
void _buildRoleMapLoop();
// Note : _drawPageX et _handleEnterX seront déclarés au moment où chaque page est incarnée (3.C.1+ / 3.D.1+ / etc.) pour éviter link errors sur stubs non-définis.
```

**Patch 2 — `ToolPadRoles.cpp`** :

a. Constructor L65 : `_activeSubPage(SUB_NORM)` → `_activeSubPage(SUB_BANK)`.

b. Header console L608 : `"TOOL 3: PAD ROLES"` → `"TOOL 3: PAD ROLE"` (singulier).

c. `_drawSubPageHeader` L237 : `const char* labels[SUB_COUNT] = { "NORM", "ARPEG", "LOOP" };` → `{ "BANK", "ARPEG", "LOOP", "CC" }`.

d. Switch `buildRoleMap()` L164-170 — extension 4 cas :
```cpp
void ToolPadRoles::buildRoleMap() {
  switch (_activeSubPage) {
    case SUB_BANK:  _buildRoleMapBank();  break;
    case SUB_ARPEG: _buildRoleMapArpeg(); break;
    case SUB_LOOP:  _buildRoleMapLoop();  break;
    case SUB_CC:    _buildRoleMapCc();    break;
    default:        _buildRoleMapLegacy(); break;
  }
}
```

e. **Déplacement** des stubs `_buildRoleMapNorm/Arpeg/Loop` actuels (L207-219) :
- `_buildRoleMapNorm()` → renommé `_buildRoleMapBank()` et **déplacé** dans `ToolPadRoles_Bank.cpp`.
- `_buildRoleMapArpeg()`, `_buildRoleMapLoop()` → déplacés dans leurs fichiers respectifs.
- Nouveau `_buildRoleMapCc()` créé comme stub dans `ToolPadRoles_Cc.cpp`.

f. **`_buildRoleMapLegacy` conservé** dans `ToolPadRoles.cpp` (helper de fallback, retiré en 3.H.2 selon L1).

**Patch 3 — Création 4 fichiers `.cpp` avec stubs initiaux** :

`ToolPadRoles_Bank.cpp` :
```cpp
#include "ToolPadRoles.h"

// Phase 3.B stub — delegates to legacy until Phase 3.D incarnates page BANK.
void ToolPadRoles::_buildRoleMapBank() {
  _buildRoleMapLegacy();
}
```

`ToolPadRoles_Cc.cpp`, `ToolPadRoles_Arpeg.cpp`, `ToolPadRoles_Loop.cpp` : symétriques, chaque stub delegate à `_buildRoleMapLegacy`.

**Patch 4 — `SetupManager.{cpp,h}`** : **pas touché en 3.B**. Tool 4 reste accessible. Suppression en 3.C.1b.

### §4.3 Hard-asserts auto-review

```bash
# A. SUB_NORM n'existe plus
grep -rn "SUB_NORM" src/
# attendu : 0 lignes

# B. 4 fichiers ToolPadRoles_*.cpp existent
ls src/setup/ToolPadRoles_*.cpp | wc -l
# attendu : 4

# C. Chaque nouveau fichier a son stub delegating
grep -c "_buildRoleMapLegacy" src/setup/ToolPadRoles_*.cpp
# attendu : 1 par fichier (4 total)

# D. Switch buildRoleMap a 4 cas SUB_*
grep -cE "case SUB_(BANK|ARPEG|LOOP|CC)" src/setup/ToolPadRoles.cpp
# attendu : 4

# E. Header sub-page contient les 4 labels
grep -E '"BANK".*"ARPEG".*"LOOP".*"CC"' src/setup/ToolPadRoles.cpp
# attendu : 1 match
```

### §4.4 HW Gate G1

**Procédure** :
1. Build clean (0 warning), boot OK.
2. Menu setup : Tool 3 sélectionnable, Tool 4 reste sélectionnable (transition).
3. Entrer Tool 3. Header affiche **"TOOL 3: PAD ROLE"** + ligne sub-page `Sub-page [BANK|ARPEG|LOOP|CC] [TAB] cycle`. BANK est highlighted reverse+bold, autres dim.
4. **TAB nav + arrows** (fusionné 4+5) : TAB cycle BANK → ARPEG → LOOP → CC → BANK (4 tours). Chaque page affiche contenu legacy identique. Nav arrows testées implicitement en se déplaçant.
5. ENTER ouvre pool (comportement actuel). Pool toujours 5 lignes legacy.
6. Save d'un rôle (ex assignment Root B sur pad libre). Reload reboot. Persiste.
7. q exit depuis page BANK uniquement (code exit dans orchestrateur, pas page-specific en 3.B — test redondant sur 4 pages).
8. Tool 4 sélectionné depuis menu : fonctionne comme avant.

**Critères** :
- ✓ TAB cycle 4 pages, header correct
- ✓ Save/reload via Tool 3 fonctionne (legacy preserved)
- ✓ Tool 4 reste fonctionnel
- ✓ No crash, 0 warning compile

### §4.5 Mini-audit faisabilité

**Risques identifiés** :

1. **`_buildRoleMapLegacy` survie jusqu'à 3.H.2** : pendant 3.C-3.F, certaines pages incarnent leur vraie impl (qui remplace le stub) mais `_buildRoleMapLegacy` reste référencé par les stubs des autres pages. Pas un bug. Retrait final groupé en 3.H.2.

2. **Header "TOOL 3: PAD ROLE"** : spec §6.1 dit "Tool PAD ROLE", code actuel "TOOL 3: PAD ROLES". Décision : "TOOL 3: PAD ROLE" — singulier, garde le numéro descriptor T3. Cohérence avec convention vt100-design-guide §2.2 "TOOL N: NAME".

3. **`ROLE_CC = 6` nouveau** : déclaré en 3.B, utilisé en 3.C.2+ pour cell display §8.1 colonne CC. En 3.B il existe mais n'est jamais émis par les builders (stubs delegating legacy). OK — déclaration en avance.

4. **Stubs `_drawPageX` non déclarés en 3.B** : déclarés au moment où chaque page est incarnée (3.D.1 / 3.C.1 / 3.E.1 / 3.F.1). Évite link errors sur méthodes déclarées sans définition.

5. **`drawGrid`/`drawPool`/`drawInfoPanel` legacy** : conservés en 3.B (toutes pages affichent contenu legacy via `_buildRoleMapLegacy`). La spécialisation par page intervient en 3.C.1b+ (dispatch `drawScreen`).

### §4.6 Décisions actées 3.B

1. Enum `SubPage` : `SUB_NORM → SUB_BANK` (rename), ajout `SUB_CC`, `SUB_COUNT = 4`.
2. Enum `PadRoleCode` : ajout `ROLE_CC = 6`.
3. Création 4 fichiers `.cpp` avec stubs delegating `_buildRoleMapLegacy`.
4. `_drawPageX` non déclarés en 3.B.
5. SetupManager non touché en 3.B.
6. Header "TOOL 3: PAD ROLE" (singulier).

---

## §5 Phase 3.C — Page CC (absorption Tool 4)

### §5.0 Vue d'ensemble

Phase grosse fractionnée en 3 sous-phases pour isoler les risques :
- **3.C.1a** : migration mécanique pure du code Tool 4 → `ToolPadRoles_Cc.cpp`. **Non-câblée** (Tool 4 actuel reste actif, page CC affiche encore stub legacy).
- **3.C.1b** : câblage orchestrateur + suppression physique Tool 4.
- **3.C.2** : nouvelle UX cell display §8.1 colonne CC + helpers `_padNeighborInfo` / `_formatRoleNameMusician`.

HW Gate G2 unique après 3.C.2 valide l'ensemble.

Volume cumulé : ~1300 L touchées (~800 migration + ~300 câblage + ~200 UX).

### §5.1 Phase 3.C.1a — Migration mécanique pure

#### §5.1.1 Objectif

Déplacer le code de `ToolControlPads.{cpp,h}` (~903 lignes) dans `ToolPadRoles_Cc.cpp`. **Comportement runtime strictement identique** au Tool 4 actuel. Tool 4 reste fonctionnel pendant cette sous-phase (câblage en 3.C.1b).

#### §5.1.2 Members ToolControlPads à fusionner

| Tool 4 actuel | Devient dans ToolPadRoles |
|---|---|
| `UIMode _uiMode` | `CcUiMode _ccUiMode` (enum renommée) |
| `uint8_t _cursorPad` | utilisé via `_gridRow * 12 + _gridCol` existants |
| `uint8_t _fieldIdx` | `uint8_t _ccFieldIdx` |
| `uint8_t _poolIdx` | `uint8_t _ccPoolIdx` |
| `uint8_t _globalFieldIdx` | `uint8_t _ccGlobalFieldIdx` |
| `bool _propEditDirty` | `bool _ccPropEditDirty` |
| `bool _globalEditDirty` | `bool _ccGlobalEditDirty` |
| `bool _wkDirty` | `bool _ccWkDirty` |
| `ControlPadStore _wk` | `ControlPadStore _wkCc` |
| `bool _nvsSaved` | partagé avec `ToolPadRoles::_nvsSaved` existant (NVS T3 badge agrégé) |

**Members déjà partagés** (pas de duplication) : `_keyboard`, `_leds`, `_ui`, `_nvs`, `_input`, `_refBaselines`, `_flashMsg[80]`, `_flashExpireMs`, `_setFlash()`, `_flashActive()`.

**Nouveau** : `BankSlot* _banks` (pour `_currentBankFromBanks()`).

#### §5.1.3 Patches

**Patch 1 — `ToolPadRoles.h`** : ajout members + enum + déclarations méthodes
```cpp
// --- Page CC sub-state machine (formerly ToolControlPads::UIMode) ---
enum CcUiMode : uint8_t {
  UI_CC_GRID_NAV         = 0,
  UI_CC_MODE_PICK        = 1,
  UI_CC_VALUE_EDIT       = 2,
  UI_CC_CONFIRM_REMOVE   = 3,
  UI_CC_CONFIRM_DEFAULTS = 4,
  UI_CC_GLOBAL_EDIT      = 5,
};

// --- Page CC state members ---
BankSlot*       _banks;            // new
ControlPadStore _wkCc;
CcUiMode        _ccUiMode;
uint8_t         _ccFieldIdx;
uint8_t         _ccPoolIdx;
uint8_t         _ccGlobalFieldIdx;
bool            _ccPropEditDirty;
bool            _ccGlobalEditDirty;
bool            _ccWkDirty;
bool            _ccScreenDirty;     // migration _screenDirty member Tool 4 (cf §5.1.5 point 4)

// --- Page CC methods (defined in ToolPadRoles_Cc.cpp) ---
void _handleGridNavCc(const NavEvent& ev);
void _handleModePickCc(const NavEvent& ev);
void _handleValueEditCc(const NavEvent& ev);
void _handleConfirmRemoveCc(const NavEvent& ev);
void _handleConfirmDefaultsCc(const NavEvent& ev);
void _handleGlobalEditCc(const NavEvent& ev);
void _drawPageCc();
void _drawGridCc();
void _drawPoolCc();
void _drawSelectedCc();
void _drawGlobalsCc();
void _drawInfoCc();
void _drawControlBarCc();
uint8_t _poolIdxFromEntryCc(const ControlPadEntry& e) const;
void    _applyPoolIdxToEntryCc(uint8_t idx, ControlPadEntry& e) const;
void    _adjustGlobalFieldCc(int8_t delta);
uint8_t _currentBankFromBanksCc() const;
int8_t  _findSlotCc(uint8_t padIdx) const;
bool    _addSlotCc(uint8_t padIdx);
void    _removeSlotForPadCc(uint8_t padIdx);
void    _resetAllCc();
void    _adjustFieldCc(int8_t delta);
bool    _isFieldGreyedCc(uint8_t fieldIdx) const;
void    _saveCc();
void    _loadCc();
void    _refreshBadgeCc();
```

Modifier signature `begin()` pour ajouter `BankSlot* banks` :
```cpp
void begin(CapacitiveKeyboard* keyboard, LedController* leds,
           SetupUI* ui, NvsManager* nvs, BankSlot* banks,
           uint8_t* bankPads, uint8_t* rootPads, uint8_t* modePads,
           uint8_t& chromaticPad, uint8_t& arpPlayStopPad,
           uint8_t* octavePads);
```

**Patch 2 — `ToolPadRoles_Cc.cpp`** : migration code Tool 4

Migration brute des méthodes `ToolControlPads::*` → `ToolPadRoles::*Cc`. Adaptations mécaniques (sed) :
- `_uiMode` → `_ccUiMode`
- `UI_GRID_NAV` → `UI_CC_GRID_NAV` (etc., 6 enum values)
- `_cursorPad` → `(uint8_t)(_gridRow * 12 + _gridCol)` (~15 sites à adapter sans automatisme — **source de bug logique potentielle, non attrapée par compilateur**)
- `_fieldIdx` → `_ccFieldIdx` (etc., 7 members)
- `_wk` → `_wkCc`
- `_findSlot` → `_findSlotCc` (etc., 7 helpers)
- `_setFlash`, `_flashActive`, `_refBaselines`, `_input` : **conservés tels quels** (déjà partagés sur ToolPadRoles)

**Adaptation `_loadCc`** : Tool 4 actuel L862-876 charge depuis `NvsManager::loadBlob` direct. Adapté pour utiliser le cache `_nvs->getLoadedControlPadStore()` (déjà peuplé par `loadAll` au boot) — économie d'une IO flash.
```cpp
void ToolPadRoles::_loadCc() {
  _wkCc = _nvs->getLoadedControlPadStore();
  validateControlPadStore(_wkCc);  // defensive
}
```

**`run()` de Tool 4** L33-90 : **NON migré**. La `run()` de `ToolPadRoles` reste l'unique orchestrateur. Le dispatch CC vient en 3.C.1b.

**Patch 3 — `ToolPadRoles::begin()` body** : ajout chargement `_wkCc`
```cpp
if (_nvs) {
  _wkLoopPad = _nvs->getLoadedLoopPadStore();
  _wkCc      = _nvs->getLoadedControlPadStore();  // new
  validateControlPadStore(_wkCc);
}
```

#### §5.1.4 Hard-asserts 3.C.1a

```bash
# A. ToolPadRoles_Cc.cpp existe avec tous les handlers
grep -c "ToolPadRoles::_handle" src/setup/ToolPadRoles_Cc.cpp
# attendu : ≥ 6

# B. Tool 4 actuel intact
ls src/setup/ToolControlPads.{cpp,h}
# attendu : 2 fichiers présents

# C. Aucun member non préfixé dans _Cc.cpp (collision check)
grep -E "\b_uiMode\b" src/setup/ToolPadRoles_Cc.cpp
# attendu : 0 matches (doit être _ccUiMode partout)
grep -E "\b_cursorPad\b" src/setup/ToolPadRoles_Cc.cpp
# attendu : 0 matches (doit être _gridRow*12+_gridCol)

# D. _wkCc présent
grep "_wkCc" src/setup/ToolPadRoles.h src/setup/ToolPadRoles.cpp
# attendu : ≥ 3 matches
```

#### §5.1.5 Mini-audit 3.C.1a

**Risques** :

1. **Oubli member non préfixé `_X` → `_ccX`** : si nom existe ailleurs (peu probable mais possible), bug runtime invisible compile-time. Hard-asserts attrapent les cas connus mais pas tous.

2. **Adaptation `_cursorPad` → `_gridRow * 12 + _gridCol`** : ~15 sites. Bugs de logique non attrapés par compilateur (ex `_cursorPad >= 12` → `_gridRow >= 1` mais comparison subtile). À tester exhaustivement nav grid au HW gate G2.

3. **Two-step exit snapshot** : Tool 4 fait `UIMode modeAtStart = _uiMode;`. Migration → `CcUiMode modeAtStart = _ccUiMode;`. Si oublié, `q` depuis sub-edit sort directement du tool. Pattern setup-tools-conventions §6.4 — invariant à préserver (cf §14.3).

4. **`_screenDirty` Tool 4 vs `screenDirty` local ToolPadRoles** : Tool 4 utilise member `_screenDirty`. ToolPadRoles utilise local dans `run()`. Convention 3.C.1a : utiliser `_ccScreenDirty` member dédié (pour ne pas confondre avec local `screenDirty` de `run()`).

5. **`flashSaved()` (blocking 300ms) vs `_setFlash()` (non blocking)** : `_saveCc` doit conserver les 2 appels distincts (cf [§14.1](#§141--pattern-save-per-commit-tool-4-conservé-en-page-cc-3c1b-et-après)).

### §5.2 Phase 3.C.1b — Câblage orchestrateur

#### §5.2.1 Objectif

Brancher le code 3.C.1a sur l'orchestrateur `ToolPadRoles::run()` et `drawScreen()`. Supprimer Tool 4 physiquement.

#### §5.2.2 Patches

**Patch 1 — `ToolPadRoles.cpp` run()** : extension dispatch SUB_CC
```cpp
while (true) {
  _leds->update();
  _keyboard->pollAllSensorData();
  NavEvent ev = _input.update();

  // Flash expiry common
  if (_flashActive() && millis() > _flashExpireMs) {
    _flashMsg[0] = '\0';
    _flashExpireMs = 0;
    screenDirty = true;
  }

  // TAB cycle (blocked during any sub-edit)
  bool inAnySubEdit = _editing || _confirmDefaults || _confirmClearAll
                     || (_activeSubPage == SUB_CC && _ccUiMode != UI_CC_GRID_NAV);
  if (ev.type == NAV_CHAR && ev.ch == '\t' && !inAnySubEdit) {
    _handleTab();
    screenDirty = true;
  }

  // Dispatch input to active page
  if (_activeSubPage == SUB_CC) {
    CcUiMode modeAtStart = _ccUiMode;  // snapshot for two-step exit
    switch (_ccUiMode) {
      case UI_CC_GRID_NAV:         _handleGridNavCc(ev);         break;
      case UI_CC_MODE_PICK:        _handleModePickCc(ev);        break;
      case UI_CC_VALUE_EDIT:       _handleValueEditCc(ev);       break;
      case UI_CC_CONFIRM_REMOVE:   _handleConfirmRemoveCc(ev);   break;
      case UI_CC_CONFIRM_DEFAULTS: _handleConfirmDefaultsCc(ev); break;
      case UI_CC_GLOBAL_EDIT:      _handleGlobalEditCc(ev);      break;
    }
    if (ev.type == NAV_QUIT && modeAtStart == UI_CC_GRID_NAV) {
      _ui->vtClear();
      return;
    }
  } else {
    // Legacy dispatch pour BANK/ARPEG/LOOP (extraction progressive 3.D/3.E/3.F)
    // [code legacy L723-925 reste tant que pages BANK/ARPEG/LOOP pas incarnées]
  }

  if (screenDirty) {
    screenDirty = false;
    buildRoleMap();
    drawScreen();
  }
  delay(5);
}
```

**Patch 2 — `ToolPadRoles.cpp` drawScreen()** : dispatch SUB_CC
```cpp
void ToolPadRoles::drawScreen() {
  _ui->vtFrameStart();
  _ui->drawConsoleHeader("TOOL 3: PAD ROLE", _nvsSaved);
  _drawSubPageHeader();
  _ui->drawFrameEmpty();

  if (_activeSubPage == SUB_CC) {
    _drawPageCc();
  } else {
    // Legacy fallback pour BANK/ARPEG/LOOP (jusqu'à 3.D/3.E/3.F)
    _ui->drawSection("GRID"); drawGrid(); _ui->drawFrameEmpty();
    _ui->drawSection("POOL"); drawPool(); _ui->drawFrameEmpty();
    _ui->drawSection("INFO"); drawInfoPanel(); _ui->drawFrameEmpty();
    _drawFlash(); drawControlBar();
  }
  _ui->vtFrameEnd();
}
```

**Patch 3 — `ToolPadRoles_Cc.cpp`** : ajout `_drawPageCc()` body (migré depuis Tool 4 `_draw`)
```cpp
void ToolPadRoles::_drawPageCc() {
  _ui->drawSection("PAD GRID");
  _drawGridCc();
  _ui->drawFrameEmpty();
  _ui->drawSection("POOL");
  _drawPoolCc();
  _ui->drawFrameEmpty();
  _ui->drawSection("SELECTED");
  _drawSelectedCc();
  _ui->drawSection("GLOBALS");
  _drawGlobalsCc();
  _ui->drawFrameEmpty();
  _ui->drawSection("INFO");
  _drawInfoCc();
  _drawControlBarCc();
}
```

**Patch 4 — `SetupManager.{cpp,h}`** : retrait Tool 4 + ajout `banks` propagation

`SetupManager.h` :
- L10 : `#include "ToolControlPads.h"` → **supprimer**
- L46 : `ToolControlPads _toolControlPads;` → **supprimer**
- L29 : signature `begin(...)` ajouter `BankSlot* banks` argument

`SetupManager.cpp` :
- L19, L34 : impl signature + propagation `_toolPadRoles.begin(..., banks, ...)`
- L36 : `_toolControlPads.begin(...)` → **supprimer**
- L104-105 : `case '4': _toolControlPads.run();` → **supprimer**
- `printMainMenu` : retrait de la ligne Tool 4 dans l'itération (à coordonner avec `SetupUI.cpp`)

**Patch 5 — Suppression physique** :
```bash
rm src/setup/ToolControlPads.h
rm src/setup/ToolControlPads.cpp
```

**Patch 6 — `KeyboardData.h` TOOL_NVS mapping** : T3 absorbe ControlPad descriptor
```cpp
// T3 étendu pour inclure ControlPad (descriptor 5), T4 = range vide
static constexpr uint8_t TOOL_NVS_FIRST[] = { 0, 1, 2, 5, 6, 7, 8, 10 };
static constexpr uint8_t TOOL_NVS_LAST[]  = { 0, 1, 5, 4, 6, 7, 9, 11 };
//                                             ^      ^
//                                       T3=[2..5]  T4=range vide (FIRST > LAST)
```

#### §5.2.3 Hard-asserts 3.C.1b

```bash
# A. ToolControlPads physiquement absent
ls src/setup/ToolControlPads.* 2>&1 | grep "No such"
# attendu : match (file absent)

# B. case '4' retiré
grep "case '4'" src/setup/SetupManager.cpp
# attendu : 0 matches

# C. ToolControlPads non référencé
grep -rn "ToolControlPads\|toolControlPads" src/
# attendu : 0 matches

# D. _toolPadRoles.begin reçoit banks
grep "_toolPadRoles.begin" src/setup/SetupManager.cpp
# attendu : contient "banks" en argument

# E. TOOL_NVS_LAST T3 inclut descriptor 5
grep -A2 "TOOL_NVS_LAST" src/core/KeyboardData.h
# attendu : TOOL_NVS_LAST[2] = 5

# F. drawScreen dispatch SUB_CC
grep "_activeSubPage == SUB_CC" src/setup/ToolPadRoles.cpp
# attendu : ≥ 1 match
```

### §5.3 Phase 3.C.2 — Cell display §8.1 colonne CC

#### §5.3.1 Objectif

Ajouter la **nouvelle UX cell display §8.1** à la page CC migrée. Tool 4 actuel n'affichait que les rôles propres (CC slots + R/P/C LOOP control 3.B). Spec §8.1 demande aussi :
- Pad portant BANK (cross-page absorbant) → cell `Bk<n>` dim fond ambre+ saturé
- Pad portant 1-2 rôles CONTEXTUEL (ARPEG modificateur ou LOOP slot) → cell `■■` neutre

Création des helpers `_padNeighborInfo` et `_formatRoleNameMusician` réutilisables (consommés aussi par 3.D, 3.E, 3.F, 3.G).

#### §5.3.2 Patches

**Patch 1 — `ToolPadRoles.h`** : struct + helpers
```cpp
struct PadNeighborInfo {
  int8_t          bankIdx;         // -1 si pas BANK, sinon 0..7
  bool            hasCc;
  ScaleRoleResult scaleRole;       // {NONE,...} si pas de scale role
  ArpRoleResult   arpRole;
  int8_t          loopSlotIdx;     // -1 si pas slot, sinon 0..15
  bool            isLoopRec;
  bool            isLoopPlayStop;
  bool            isLoopClear;
};

PadNeighborInfo _padNeighborInfo(uint8_t pad) const;
void _formatRoleNameMusician(const PadNeighborInfo& info, char* out, size_t cap) const;
// Note : extension verbose pour modale en 3.G.1.
```

**Patch 2 — `ToolPadRoles.cpp`** : impl helpers
```cpp
PadNeighborInfo ToolPadRoles::_padNeighborInfo(uint8_t pad) const {
  PadNeighborInfo info{};
  info.bankIdx        = findBankIdxForPad(_wkBankPads, pad);
  info.scaleRole      = scaleRoleAtPad(_wkRootPads, _wkModePads, _wkChromPad, pad);
  info.arpRole        = arpRoleAtPad(_wkArpPlayStopPad, _wkOctavePads, pad);  // renamed 3.A
  info.loopSlotIdx    = findLoopSlotIdx(_wkLoopPad, pad);
  info.isLoopRec      = (_wkLoopPad.recPad      == pad);
  info.isLoopPlayStop = (_wkLoopPad.playStopPad == pad);
  info.isLoopClear    = (_wkLoopPad.clearPad    == pad);
  info.hasCc          = (findControlPadEntryIdx(_wkCc, pad) >= 0);
  return info;
}
```

**Patch 3 — `ToolPadRoles_Cc.cpp`** : extension `_drawGridCc` + `_drawInfoCc`

Ordre des checks dans `_drawGridCc` :
1. CC propre (priorité absolue page CC, code Tool 4 existant)
2. LOOP control REC/PS/CLEAR (label R/P/C, code 3.B preserved pour AFFICHAGE GRID — mais voir ci-dessous pour ENTER)
3. BANK absorbant cross-page → `Bk<n>` dim ambre+, `_roleMap[i] = 7`
4. Contextuel ARPEG (modificateur) ou LOOP slot → `■■` neutre, `_roleMap[i] = 8`
5. Vide → `---`

Refus dur silencieux dans `_handleModePickCc` lors d'ENTER assign : si pad porte BANK / contextuels (**incluant LOOP REC/PS/CLEAR**) → no-op silencieux uniforme. **Le flash 3.B "Pad is LOOP REC/PS/CLR..." est RETIRÉ dès 3.C.2** (cf §12.11 audit M4) — cohérence no-op silencieux uniforme placeholder modale 3.G.

L'affichage grid label R/P/C des LOOP control reste preserved pour info visuelle, mais le comportement ENTER est aligné avec autres contextuels (no-op silencieux jusqu'à modale 3.G.2).

**Patch 4 — `SetupUI.h`** : macros couleur placeholders
```cpp
#define VT_BG_AMBER_SAT   "\033[48;5;130m"  // placeholder, refined in 3.H.1
#define VT_NEUTRAL_BAR    " ■■ "             // UTF-8 BLACK SQUARE x2
```

**Patch 5 — `SetupUI.cpp`** : extension switch inline `GRID_CONTROLPAD` cases 7 (ambre+) / 8 (neutre) — cf §12.4 audit B-N1

Refonte du switch `case GRID_CONTROLPAD` actuel (`SetupUI.cpp:550-556`) — extension avec cases 7/8 (pas de table `COLORS_CONTROLPAD[]`, c'est un switch inline).

```cpp
// SetupUI.cpp switch GRID_CONTROLPAD — extension 3.C.2
switch (roleMap[key]) {
  case 1:    modeColor = VT_BRIGHT_YELLOW;        break;  // MOM
  case 2:    modeColor = VT_MAGENTA;              break;  // LATCH
  case 3:    modeColor = VT_ORANGE;               break;  // CONT + RET0
  case 4:    modeColor = VT_BRIGHT_WHITE;         break;  // CONT + HOLD
  case 7:    modeColor = VT_DIM VT_BG_AMBER_SAT;  break;  // ABSORBANT cross-page (NEW 3.C.2 §12.4)
  case 8:    modeColor = VT_DIM;                  break;  // neutre ■■ cross-page (NEW 3.C.2 §12.4)
  default:   modeColor = VT_DIM;                  break;  // unassigned
}
```

**Patch 6 — `ToolPadRoles_Cc.cpp` `_drawInfoCc` body** : langue musicien §15.2 (F7 audit 1 I4.4 — clôture dette doc)

Body concret (symétrique aux _drawInfoBank / _drawInfoArpeg / _drawInfoLoop) :
```cpp
void ToolPadRoles::_drawInfoCc() const {
  uint8_t pad = _gridRow * 12 + _gridCol;
  PadNeighborInfo info = _padNeighborInfo(pad);
  char line1[80], line2[80] = {0};

  // Ligne 1 : rôle propre CC (entry MIDI CC sur ce pad)
  if (info.hasCc) {
    int8_t ccSlot = findControlPadEntryIdx(_wkCc, pad);
    if (ccSlot >= 0) {
      const ControlPadEntry& e = _wkCc.entries[ccSlot];
      static const char* modeLabels[5] = {"MOM","LATCH","CONT+RET0","CONT+HOLD","?"};
      uint8_t mIdx = (e.mode < 4) ? e.mode : 4;
      snprintf(line1, sizeof(line1), "Pad %u : CC%u ch%u %s",
               pad + 1, e.ccNumber, e.channel + 1, modeLabels[mIdx]);
    }
  } else {
    snprintf(line1, sizeof(line1), "Pad %u : libre pour CC", pad + 1);
  }

  // Ligne 2 : voisins cross-page (absorbant BANK ou contextuels ARPEG/LOOP)
  if (info.bankIdx >= 0) {
    snprintf(line2, sizeof(line2), "  + Bank %d (page BANK, absorbant — interdit ici)",
             info.bankIdx + 1);
  } else {
    char neighborBuf[60] = {0};
    _formatRoleNameMusician(info, neighborBuf, sizeof(neighborBuf));
    if (neighborBuf[0]) {
      snprintf(line2, sizeof(line2), "  + %s (contextuel — modale a l'assign §10)", neighborBuf);
    }
  }

  _ui->drawFrameLine(line1);
  if (line2[0]) _ui->drawFrameLine(line2);
}
```

#### §5.3.3 Hard-asserts 3.C.2

```bash
# A. _padNeighborInfo defined and used
grep -c "_padNeighborInfo" src/setup/ToolPadRoles.h src/setup/ToolPadRoles.cpp src/setup/ToolPadRoles_Cc.cpp
# attendu : ≥ 3

# B. _formatRoleNameMusician défini
grep -c "_formatRoleNameMusician" src/setup/ToolPadRoles.h src/setup/ToolPadRoles.cpp
# attendu : ≥ 2

# C. Macros couleur définies
grep -E "VT_BG_AMBER_SAT|VT_NEUTRAL_BAR" src/setup/SetupUI.h
# attendu : 2 matches

# D. GRID_CONTROLPAD palette étendue à 9 entries (0..8)
grep -A12 "case GRID_CONTROLPAD" src/setup/SetupUI.cpp
# attendu : présence map=7 (VT_BG_AMBER_SAT) et map=8 (neutre)

# E. _roleMap = 7/8 utilisés dans _Cc.cpp
grep -cE "_roleMap\[.*\] = [78]" src/setup/ToolPadRoles_Cc.cpp
# attendu : ≥ 2

# F. _drawGridCc utilise les helpers cross-store
grep -cE "findBankIdxForPad|scaleRoleAtPad|arpRoleAtPad|findLoopSlotIdx|info\." src/setup/ToolPadRoles_Cc.cpp
# attendu : ≥ 5
```

### §5.4 HW Gate G2 (combiné 3.C.1a + 3.C.1b + 3.C.2)

**Setup préalable** :
- Via Tool 3 sous-page NORM legacy avant 3.B (ou page BANK legacy fallback en 3.C), assigner Bank 1 sur pad 0, Bank 2 sur pad 1
- Via pool ARPEG legacy : assigner Root A sur pad 8
- Dev seed M7 actif : LOOP REC=32, PS=33, CLR=34

**Procédure** :
1. Boot. Vérifier badge T3 sur menu = état combiné NVS (BankPad + ScalePad + ArpPad + ControlPad). Tool 4 absent du menu.
2. Entrer Tool 3 → TAB 3× pour atteindre page CC.
3. Vérifier cell display §8.1 :
   - Pad 0, 1 (BANK) : `Bk1`, `Bk2` dim fond ambre+ saturé
   - Pad 8 (ARPEG Root A) : ` ■■ ` neutre dim
   - Pad 32, 33, 34 (LOOP REC/PS/CLR) : ` R `, ` P `, ` C ` (code 3.B preserved)
   - Pads libres : `---`
4. Info panel langue musicien :
   - Curseur pad 0 : "Pad #1 : BANK 1 - interdit ici. Move bank assignment in page BANK first."
   - Curseur pad 8 : "Pad #9 : Root A (ARPEG). Assignment overwrites these roles (Phase 3.G modale)."
   - Curseur pad 32 : "Pad #33 : LOOP REC (Tool 3 b1) - cannot assign CC here" (3.B preserved)
5. **Test refus uniforme cross-page** (cohérent §12.11 — flash 3.B retiré dès 3.C.2) :
   - ENTER sur pad 0 (BANK) → no-op silencieux (placeholder modale 3.G)
   - ENTER sur pad 8 (ARPEG) → no-op silencieux
   - ENTER sur pad 32 (LOOP REC) → **no-op silencieux** (PAS de flash, alignement no-op uniforme §12.11)
   Critère : aucun flash, aucun message, retour direct UI_CC_GRID_NAV pour les 3 cas.
6. Test non-régression Tool 4 (essentiel) :
   - ENTER sur pad 5 (libre) → MODE_PICK pool ouvert
   - Sélectionner MOM, ENTER → assigne CC0 momentary, save (flashSaved), label `00m`
   - Tester `e` (VALUE_EDIT) + `d` (CONFIRM_DEFAULTS) — fonctionnels comme Tool 4 actuel
   - (`g` GLOBAL_EDIT et `x` CONFIRM_REMOVE : smoke-check uniquement, regroupés en "fonctionnalités avancées CC OK")
7. q exit + reboot, persistance OK (fusionné en 1 observation séquentielle)

**Critères** :
- ✓ Tool 4 retiré du menu (`case '4'` absent)
- ✓ Page CC absorption Tool 4 non-régression (tous comportements préservés)
- ✓ Cell display §8.1 colonne CC : BANK ambre+, contextuels ■■, LOOP control R/P/C, CC propre normal
- ✓ Info panel langue musicien correct (cross-page + LOOP control)
- ✓ Refus no-op silencieux uniforme cross-page (BANK + contextuels + LOOP control — placeholder modale 3.G, flash 3.B retiré §12.11)
- ✓ NVS T3 badge agrégé reflète ControlPad (descriptor 5 inclus dans range T3=[2..5]) ET LoopPadStore (descriptor 12 via check ad-hoc §12.6)
- ✓ Palette GRID_CONTROLPAD map=7/8 cohérente

---

## §6 Phase 3.D — Page BANK

### §6.0 Vue d'ensemble

Phase fractionnée en 2 sous-phases :
- **3.D.1** : code page BANK essentiel (rendu + matrice édition + ENTER selon spec §7.4 strict). Extension du code 3.C livré (sous-page NORM devenue SUB_BANK en 3.B).
- **3.D.2** : hard-constraint exit §6.5 + cell display §8.1 colonne BANK + palette `GRID_ROLES` étendue map=7/8.

HW Gate G3 unique après 3.D.2.

Volume cumulé : ~500 L touchées (~350 + ~150).

### §6.1 Phase 3.D.1 — Code page BANK essentiel

#### §6.1.1 Objectif

Remplacer le stub `_buildRoleMapBank()` (3.B) par la vraie impl. Migrer la logique bank slot move (code 3.C livré ToolPadRoles.cpp:878-919) depuis l'orchestrateur vers `ToolPadRoles_Bank.cpp`. Ajouter pool 1 ligne + info panel langue musicien. Respect spec §7.4 strict pour ENTER (dégage direct sur bank assignée).

#### §6.1.2 Patches

**Patch 1 — `ToolPadRoles.h`** : déclarations méthodes page BANK
```cpp
// --- Page BANK methods (defined in ToolPadRoles_Bank.cpp) ---
void _drawPageBank();
void _drawGridBank();
void _drawPoolBank();
void _drawInfoBank();
void _drawControlBarBank();
void _handleEnterBank();          // ENTER from grid nav (spec §7.4 strict)
void _handleEnterPoolBank();      // ENTER from pool nav
void _applyDefaultsBank();        // body en 3.D.2 (rétroactif passe 4 — cf §15)
void _clearRolesBankOnly(uint8_t pad);  // helper page-scoped (§15.3)
```

**Patch 2 — `SetupUI.h`** : macro placeholder vert menthe
```cpp
#define VT_MINT_GREEN  "\033[38;5;121m"  // placeholder Phase 3.D.1, refined in 3.H.1
```

**Patch 3 — `ToolPadRoles_Bank.cpp`** : remplacement du stub 3.B par vraie impl

a. `_buildRoleMapBank` (sans §8.1 cross-page, vient en 3.D.2)
```cpp
void ToolPadRoles::_buildRoleMapBank() {
  memset(_roleMap, ROLE_NONE, NUM_KEYS);
  for (int i = 0; i < NUM_KEYS; i++) memcpy(_roleLabels[i], " -- ", 5);

  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    uint8_t pad = _wkBankPads[i];
    if (pad < NUM_KEYS) {
      _roleMap[pad] = ROLE_BANK;
      snprintf(_roleLabels[pad], 6, "Bk%u", i + 1);  // §15.2 grid label = pool label
    }
  }
  // §8.1 cross-page : 3.D.2
}
```

b. `_drawPageBank`, `_drawGridBank`, `_drawPoolBank`, `_drawControlBarBank` : patterns identiques aux sections du code legacy (legacy Tool 3 sous-page NORM livré commit `97db63a`), adaptés au scope BANK.

`_drawInfoBank` body concret (F7 audit 1 I4.4 — clôture dette doc) :
```cpp
void ToolPadRoles::_drawInfoBank() const {
  uint8_t pad = _gridRow * 12 + _gridCol;
  PadNeighborInfo info = _padNeighborInfo(pad);
  char line1[80], line2[80] = {0};

  // Ligne 1 : rôle propre BANK
  if (info.bankIdx >= 0) {
    snprintf(line1, sizeof(line1), "Pad %u : Bank %d", pad + 1, info.bankIdx + 1);
  } else {
    snprintf(line1, sizeof(line1), "Pad %u : libre pour Bank", pad + 1);
  }

  // Ligne 2 : voisins cross-page (absorbant CC ou contextuels ARPEG/LOOP)
  if (info.hasCc) {
    int8_t ccSlot = findControlPadEntryIdx(_wkCc, pad);
    if (ccSlot >= 0) {
      snprintf(line2, sizeof(line2), "  + CC%u (page CC, absorbant — interdit ici)",
               _wkCc.entries[ccSlot].ccNumber);
    }
  } else {
    char neighborBuf[60] = {0};
    _formatRoleNameMusician(info, neighborBuf, sizeof(neighborBuf));
    if (neighborBuf[0]) {
      snprintf(line2, sizeof(line2), "  + %s (contextuel — modale a l'assign §10)", neighborBuf);
    }
  }

  _ui->drawFrameLine(line1);
  if (line2[0]) _ui->drawFrameLine(line2);
}
```

c. `_handleEnterBank` — **respect spec §7.4 strict** :
```cpp
void ToolPadRoles::_handleEnterBank() {
  uint8_t pad = _gridRow * 12 + _gridCol;
  PadNeighborInfo info = _padNeighborInfo(pad);

  // Refus dur si autre absorbant ou contextuels (placeholder modale 3.G)
  if (info.hasCc || info.scaleRole.kind != ScaleRoleKind::NONE
      || info.arpRole.kind != ArpRoleKind::NONE
      || info.loopSlotIdx >= 0
      || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear) {
    return;  // no-op silencieux
  }

  // §7.1 + §7.4 STRICT : pad porte bank propre → dégage direct (no pool)
  if (info.bankIdx >= 0) {
    _wkBankPads[info.bankIdx] = 0xFF;
    if (saveAll()) _ui->flashSaved();
    return;  // stays in grid nav, pad redevient vide
  }

  // Pad vide : ouvre pool
  _editing = true;
  _poolLine = 1;
  _poolIdx = 0;
}
```

d. `_handleEnterPoolBank` — **respect spec §9.2 strict (pas de silent steal)** :
```cpp
void ToolPadRoles::_handleEnterPoolBank() {
  uint8_t pad = _gridRow * 12 + _gridCol;

  if (_poolLine == 0) {
    // [---] clear (legacy pattern, redondant en page BANK mais conservé pour cohérence cross-page)
    clearRole(pad);
    if (saveAll()) _ui->flashSaved();
    _editing = false;
    return;
  }

  // §9.2 STRICT : refus si entry déjà assignée à un autre pad (pas de silent steal)
  uint8_t targetBank = _poolIdx;
  if (_wkBankPads[targetBank] < NUM_KEYS && _wkBankPads[targetBank] != pad) {
    return;  // no-op, entry assignée est dim, ENTER ne fait rien
  }

  _wkBankPads[targetBank] = pad;
  if (saveAll()) _ui->flashSaved();
  _editing = false;
}
```

**Patch 4 — `ToolPadRoles.cpp` orchestrateur** : extension `run()` et `drawScreen` pour SUB_BANK

a. `drawScreen()` dispatch :
```cpp
if (_activeSubPage == SUB_CC) {
  _drawPageCc();
} else if (_activeSubPage == SUB_BANK) {
  _drawPageBank();
} else {
  // Legacy fallback pour ARPEG/LOOP (jusqu'à 3.E/3.F)
  // ... [code legacy] ...
}
```

b. `run()` dispatch SUB_BANK :
```cpp
} else if (_activeSubPage == SUB_BANK) {
  if (ev.type == NAV_ENTER) {
    if (!_editing) {
      _handleEnterBank();
    } else {
      _handleEnterPoolBank();
    }
    screenDirty = true;
  }
  // arrow nav grid + pool (preserved from legacy 3.C)
}
```

#### §6.1.3 Hard-asserts 3.D.1

```bash
# A. _buildRoleMapBank n'appelle plus _buildRoleMapLegacy
grep -A5 "void ToolPadRoles::_buildRoleMapBank" src/setup/ToolPadRoles_Bank.cpp | grep "_buildRoleMapLegacy"
# attendu : 0 matches

# B. _drawPageBank défini avec sections GRID/POOL/INFO
grep -cE "drawSection.*GRID|drawSection.*POOL|drawSection.*INFO" src/setup/ToolPadRoles_Bank.cpp
# attendu : ≥ 3

# C. _handleEnterBank + _handleEnterPoolBank définis
grep -c "ToolPadRoles::_handleEnter" src/setup/ToolPadRoles_Bank.cpp
# attendu : ≥ 2

# D. VT_MINT_GREEN défini
grep "VT_MINT_GREEN" src/setup/SetupUI.h
# attendu : 1 match

# E. drawScreen + run dispatchent SUB_BANK
grep -c "_activeSubPage == SUB_BANK" src/setup/ToolPadRoles.cpp
# attendu : ≥ 2

# F. §7.4 strict : ENTER sur bank assignée fait clear inline (pas pool open)
grep -B2 -A8 "info.bankIdx >= 0" src/setup/ToolPadRoles_Bank.cpp
# attendu : block "dégage direct" visible (assign 0xFF + saveAll + return)
```

#### §6.1.4 Mini-audit 3.D.1

**Risques** :

1. **Spec §7.4 strict change muscle memory user** : ENTER sur bank assignée dégage au lieu d'ouvrir pool. Loïc a écrit la spec lui-même, comportement intentionnel. UX explicit > UX magic legacy. Acté en passe 3 cf [§12.2](#§122--spec-§74-stricte-dégage-direct-vs-ux-legacy-silent-steal-tranché-passe-3).

2. **`saveAll()` legacy preserved** : page BANK sauve aussi ScalePad + ArpPad (3 NVS writes au lieu d'1). Acceptable transitoire, optimisation différée à 3.H (cf [§14.1](#§141--pattern-save-per-commit-tool-4-conservé-en-page-cc-3c1b-et-après)).

3. **Branche `else` legacy dans `run()`** : sert ARPEG/LOOP jusqu'à 3.E/3.F. ~150 L de code legacy qui restent. Extraction progressive aux phases suivantes.

4. **`[---] clear role` redondant en page BANK avec §7.4 strict** : conservé pour cohérence cross-page (autres pages l'utilisent). N'apparait pas comme un problème user (deux voies de dégage, l'une plus rapide).

### §6.2 Phase 3.D.2 — Hard-constraint exit + cell display §8.1 BANK

#### §6.2.1 Objectif

Compléter la page BANK avec les éléments §8.1 (cell display cross-page) et §6.5 (hard-constraint exit). Étendre la palette `GRID_ROLES` avec map=7/8 cohérent avec `GRID_CONTROLPAD` (cf §12.1 audit finding).

#### §6.2.2 Patches

**Patch 1 — `ToolPadRoles.cpp` `run()` `NAV_QUIT` handler** : hard-constraint §6.5
```cpp
if (ev.type == NAV_QUIT) {
  if (_editing) {
    _editing = false;
    screenDirty = true;
  } else if (_activeSubPage == SUB_CC && _ccUiMode != UI_CC_GRID_NAV) {
    // Two-step exit page CC sub-edit (déjà géré par dispatch CC)
  } else {
    // §6.5 Hard-constraint : 8 banks must all be assigned
    bool allBanksAssigned = true;
    for (uint8_t i = 0; i < NUM_BANKS; i++) {
      if (_wkBankPads[i] >= NUM_KEYS) {
        allBanksAssigned = false;
        break;
      }
    }
    if (!allBanksAssigned) {
      _setFlash("Les 8 banks doivent etre assignees pour sortir");
      screenDirty = true;
      // do NOT return, stay in tool
    } else {
      _ui->vtClear();
      return;  // exit OK
    }
  }
}
```

**Patch 2 — `ToolPadRoles_Bank.cpp` `_buildRoleMapBank` extension §8.1** :
```cpp
void ToolPadRoles::_buildRoleMapBank() {
  memset(_roleMap, ROLE_NONE, NUM_KEYS);
  for (int i = 0; i < NUM_KEYS; i++) memcpy(_roleLabels[i], " -- ", 5);

  // (1) Bank rôles propres (priorité)
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    uint8_t pad = _wkBankPads[i];
    if (pad < NUM_KEYS) {
      _roleMap[pad] = ROLE_BANK;
      snprintf(_roleLabels[pad], 6, "Bk%u", i + 1);  // §15.2 grid label = pool label
    }
  }

  // (2) §8.1 cross-page : pads sans bank propre
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    if (_roleMap[i] != ROLE_NONE) continue;
    PadNeighborInfo info = _padNeighborInfo(i);

    // CC absorbant cross-page → CC<n> dim ambre+
    if (info.hasCc) {
      int8_t ccSlot = findControlPadEntryIdx(_wkCc, i);
      if (ccSlot >= 0) {
        uint8_t cc = _wkCc.entries[ccSlot].ccNumber;
        if (cc < 100) snprintf(_roleLabels[i], 6, "CC%02u", cc);
        else          snprintf(_roleLabels[i], 6, "C%u",   cc);
        _roleMap[i] = 7;
      }
      continue;
    }

    // Contextuels (ARPEG ou LOOP) → ■■ neutre
    bool hasContextual = (info.scaleRole.kind != ScaleRoleKind::NONE)
                       || (info.arpRole.kind != ArpRoleKind::NONE)
                       || (info.loopSlotIdx >= 0)
                       || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear;
    if (hasContextual) {
      strncpy(_roleLabels[i], " ■■ ", 5);
      _roleLabels[i][5] = '\0';
      _roleMap[i] = 8;
    }
  }
}
```

**Patch 3 — `ToolPadRoles_Bank.cpp` `_drawInfoBank` extension §8.5** : info panel langue musicien pour CC + contextuels (cf chat iter 2 passe 3 pour body complet).

**Patch 4 — `SetupUI.cpp` extension palette `GRID_ROLES`** : map=7 (ambre+) et map=8 (neutre)
```cpp
const char* COLORS_ROLES[] = {
  VT_DIM,                  // 0 = ROLE_NONE
  VT_BLUE,                 // 1 = ROLE_BANK
  VT_GREEN,                // 2 = ROLE_ROOT (legacy color, ajusté en 3.E.1 vers pêche §11.1)
  VT_CYAN,                 // 3 = ROLE_MODE
  VT_YELLOW,               // 4 = ROLE_OCTAVE (legacy, ajusté en 3.E.1 vers pourpre §11.1)
  VT_MAGENTA,              // 5 = ROLE_PLAY_STOP (legacy magenta, ajusté en 3.E.1 vers vert §11.1)
  VT_RED,                  // 6 = ROLE_COLLISION
  VT_DIM VT_BG_AMBER_SAT,  // 7 = absorbant cross-page (NEW 3.D.2)
  VT_DIM,                  // 8 = neutre ■■ cross-page (NEW 3.D.2)
};
```

#### §6.2.3 Hard-asserts 3.D.2

```bash
# A. Hard-constraint exit message présent
grep "8 banks doivent etre assignees" src/setup/ToolPadRoles.cpp
# attendu : 1 match

# B. _buildRoleMapBank gère bankIdx + hasCc + contextuels
grep -cE "info\.bankIdx|info\.hasCc|hasContextual" src/setup/ToolPadRoles_Bank.cpp
# attendu : ≥ 6

# C. GRID_ROLES palette étendue à 9 entries (0..8)
grep -A12 "COLORS_ROLES\[\]" src/setup/SetupUI.cpp
# attendu : présence VT_BG_AMBER_SAT (map=7) et entry neutre (map=8)

# D. _roleMap = 7/8 utilisés dans _Bank.cpp ET _Cc.cpp (cohérence cross-page)
grep -cE "_roleMap\[.*\] = [78]" src/setup/ToolPadRoles_Bank.cpp src/setup/ToolPadRoles_Cc.cpp
# attendu : ≥ 4
```

### §6.3 HW Gate G3 (combiné 3.D.1 + 3.D.2)

**Setup préalable** :
- NVS reset 3.A appliqué (defaults : 8 banks pas assignées au boot suivant)
- En page CC (3.C livré + 3.C.2) : assigner CC0 sur pad 10
- En sous-page ARPEG legacy : assigner Root A sur pad 20 (pool legacy 5 lignes encore actif sur SUB_ARPEG en 3.D)
- Dev seed M7 actif : LOOP REC=32, PS=33, CLR=34

**Procédure** :
1. Boot, badge T3 affiche `--`
2. Entrer Tool 3, page BANK par défaut
3. Vérifier rendu initial :
   - GRID : tous pads vides sauf pad 10 = `CC00` dim ambre+ saturé, pad 20 = ` ■■ ` neutre, pads 32/33/34 = ` ■■ ` neutre
   - POOL : ligne `Bank: Bk1 Bk2 Bk3 Bk4 Bk5 Bk6 Bk7 Bk8` toutes en vert menthe
   - INFO : curseur sur pad 0 → "Pad 1 — no role assigned"
4. **Hard-constraint test** : `q` → flash "Les 8 banks doivent etre assignees pour sortir" 2.5s, reste dans le tool
5. Assigner Bank 1 sur pad 0 (ENTER + ENTER Bk1) → save, cell `Bk1` blanc, pool Bk1 dim
6. **§7.4 strict test (dégage direct)** :
   - Curseur sur pad 0 (Bank 1 assignée), ENTER → dégage immédiat (no pool ouvert), pad 0 redevient ` -- `, pool Bk1 redevient vert menthe
   - Pas de retour au pool nav, reste en grid nav (visible par control bar)
7. Ré-assigner Bank 1 sur pad 0, puis assigner rapidement Banks 2-8 sur pads 1-7 (procédure assignement déjà validée steps 5-6)
7.5. **Test `d` defaults BANK** (validation stub reserved §7.3.2 Patch 5 remplacé par body réel 3.D.2) :
   - `d` → confirm prompt "Restaurer les defauts de cette page ? (y/n)"
   - `y` → 8 banks assignées pads 0-7 (§15.4 factory), pool entries dim
   - INFO curseur pad 0 → "Bank 1"
   Critère : 8 banks assignées (PAS de flash "reserved" — preuve que le body 3.D.2 a remplacé le stub)
8. **§8.1 cross-page test CC** : curseur pad 10 → cell `CC00` dim ambre+, INFO "Pad 11 : CC0 - interdit ici", ENTER → no-op silencieux
9. **§8.1 cross-page test contextuels** (1 cas suffit, ■■ neutre identique ARPEG/LOOP) : curseur pad 20 → cell ` ■■ ` neutre, INFO "Pad 21 : Root A (ARPEG)", ENTER → no-op silencieux. (Pad 32 LOOP REC : même comportement, vérification rapide en passant.)
10. q exit → exit OK (8 banks assignées)
11. Reboot, persistance OK
12. **§9.2 strict test (no silent steal)** :
    - ENTER sur pad 5 (libre) → pool, naviguer cursor sur Bk2 (déjà assignée pad 1, dim)
    - ENTER sur Bk2 → no-op silencieux (pas de silent steal), pad 5 reste libre, pool inchangé
    - Bk2 reste sur pad 1

**Critères** :
- ✓ Page BANK rendu correct
- ✓ Hard-constraint exit §6.5 actif
- ✓ Cell display §8.1 (CC ambre+, contextuels ■■)
- ✓ Info panel langue musicien
- ✓ **§7.4 strict** : ENTER sur bank assignée = dégage direct
- ✓ **§9.2 strict** : pas de silent steal dans le pool
- ✓ Refus dur sans flash (placeholder modale)
- ✓ Save persiste au reboot
- ✓ Palette GRID_ROLES + GRID_CONTROLPAD cohérentes (map=7/8)

### §6.4 Mini-audit 3.D (combiné)

**Risques résiduels** :

1. **Couleurs legacy ROLE_ROOT/MODE/OCTAVE/PLAY_STOP** dans GRID_ROLES restent legacy (vert/cyan/jaune/magenta) tant que page ARPEG pas incarnée. Spec §11.1 demande pêche/cyan/pourpre/vert. Migration en 3.E.1.

2. **`_padNeighborInfo` recalcul par cell × 48 + 1 par info panel** : ~49 lookups par drawScreen. ~200 µs estimé. Négligeable. Cache différé à 3.H.

3. **Hard-constraint flash 2.5s** : suffisant pour le user de lire. Pattern `_setFlash` existant (cf code 3.C livré).

4. **§9.2 strict assignment refusal "no silent steal"** : workflow swap 4 keypresses au lieu de 2 (legacy). Acceptable car (a) swap rare en pratique (b) UX explicit (c) spec écrite par Loïc lui-même.

### §6.5 Décisions actées 3.D (combiné, enrichi passe 4)

1. **Spec §7.4 strict** : ENTER sur bank assignée = dégage direct, no pool ouvert.
2. **Spec §9.2 strict** : pas de silent steal dans `_handleEnterPoolBank`, refus si entry déjà assignée.
3. **No-op silencieux pour TOUS les refus** (CC absorbant + contextuels) — placeholder modale 3.G.
4. **Hard-constraint exit globale** dans `run()` `NAV_QUIT` (toutes pages).
5. **Palette GRID_ROLES map=7/8** alignée avec GRID_CONTROLPAD (cf §12.1).
6. **`saveAll()` legacy preserved** en page BANK (optimisation différée).
7. **Couleurs legacy GRID_ROLES preserved** jusqu'à 3.E.1.
8. **`[---] clear role` page-scoped** en page BANK via `_clearRolesBankOnly(pad)` (rétroactif passe 4, cf §15.3). Préserve Scale/ARPEG/LOOP/CC sur le pad.
9. **`VT_MINT_GREEN` placeholder** `\033[38;5;121m`, raffiné en 3.H.1.
10. **Labels grid = labels pool §15.2** : `Bk1`..`Bk8` (was `-B<n>-` legacy). Rétroactif passe 4.
11. **`_applyDefaultsBank()` page-scoped (§15)** : 8 banks → pads 0-7 avec skip silencieux pads occupés par CC ou contextuels. Rétroactif passe 4. Hard-constraint exit §6.5 informe user des banks manquantes après skip.
12. **`d` raccourci page-scoped** : déclenche `_applyDefaultsBank()` via flag `_confirmDefaults` orchestrateur (§15.1). Wording prompt "Restaurer les defauts de cette page ? (y/n)".
13. **`r` raccourci legacy supprimé** (§15.1, pas dans modèle user).

### §6.6 Snippets rétroactifs passe 4 — `_applyDefaultsBank` + `_clearRolesBankOnly`

**Body `_clearRolesBankOnly` (helper page-scoped §15.3)** :
```cpp
void ToolPadRoles::_clearRolesBankOnly(uint8_t pad) {
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (_wkBankPads[i] == pad) _wkBankPads[i] = 0xFF;
  }
}
```

Utilisé en remplacement de `clearRole(pad)` legacy dans `_handleEnterPoolBank` quand `_poolLine == 0` ([---] clear role).

**Body `_applyDefaultsBank` (§15.4 valeurs hardcoded legacy)** :
```cpp
void ToolPadRoles::_applyDefaultsBank() {
  // §15.5 skip silencieux : pads occupés par CC ou contextuels cross-page
  //                        restent inchangés (placeholder modale 3.G)
  // §15.4 valeurs hardcoded legacy bank i → pad i

  // Clear current bank assignments
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    _wkBankPads[i] = 0xFF;
  }

  // Apply defaults factory avec skip silencieux
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    uint8_t pad = i;  // default factory : bank i → pad i
    PadNeighborInfo info = _padNeighborInfo(pad);

    // Skip CC absorbant (§3 ABSORBANT + ABSORBANT incompatible, §4 règle unique)
    if (info.hasCc) continue;

    // Skip contextuels (ARPEG modificateur ou LOOP slot/control)
    // §7.1 BANK + contextuels = modale 3.G. Sans modale, no-op silencieux cohérent
    // décision iter 1 (placeholder modale = refus dur sans flash).
    if (info.scaleRole.kind != ScaleRoleKind::NONE
        || info.arpRole.kind != ArpRoleKind::NONE
        || info.loopSlotIdx >= 0
        || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear) {
      continue;
    }

    _wkBankPads[i] = pad;
  }
}
```

**Note skip silencieux** : si pad 5 porte Root D ARPEG, `d` BANK ne place pas Bank 6 sur pad 5. Bank 6 reste unassigned dans `_wkBankPads[]`. Hard-constraint exit §6.5 refusera l'exit du tool jusqu'à ce que user re-saisisse Bank 6 manuellement (résolvant aussi le conflict cross-page via la modale 3.G livrée).

**Note post-3.G** : quand modale d'écrasement 3.G est livrée, `_applyDefaultsBank()` skip silencieux pourrait être remplacé par "modale globale écrasement N rôles ? (y/n)" — décision à acter post-3.G.

---

## §7 Phase 3.E — Page ARPEG complète

### §7.0 Vue d'ensemble

Page ARPEG = première page **CONTEXTUELLE** (vs BANK/CC absorbantes). 5 rôles spec : Root × 7, Mode × 7, Chromatic × 1, Octave × 4, PL/S ARPEG × 1. Pool 4 lignes. Couleurs spec §11.1 nouvelles (Root pêche, Mode/Chrom cyan, Octave pourpre, PL/S vert).

Matrice §7.1/§7.2 introduit le **swap-to-pool intra-AC silencieux** : différent de la page BANK §7.4 strict (cf [§12.2](#§122--spec-§74-stricte-dégage-direct-vs-ux-legacy-silent-steal-tranché-passe-3) et [§14.6](#§146--sémantique-enter-divergente-pages-absorbantes-vs-contextuelles)).

3 sous-phases avec HW Gate G4 unique :
- **3.E.1** Rendu : `_buildRoleMapArpeg`, 5 méthodes draw, refonte palette `GRID_ROLES` couleurs §11.1, refonte wording UI labels Hold→PL/S, migration statics legacy vers `ToolPadRoles_Arpeg.cpp`
- **3.E.2** Matrice édition §7.1 ARPEG : assign, swap intra-AC silencieux §7.2, refus absorbants no-op, helper `_clearRolesArpegOnly(pad)` page-scoped
- **3.E.3** Info panel langue musicien complet (§8.5) + **`d` raccourci `_applyDefaultsArpeg()` page-scoped** (cf [§15](#§15-conventions-defaults-par-page))

Volume cumulé : ~700-1000 L.

### §7.1 Phase 3.E.1 — Rendu page ARPEG

#### §7.1.1 Objectif

Remplacer le stub `_buildRoleMapArpeg()` (3.B delegating legacy) par sa vraie impl. Créer `_drawPageArpeg`, `_drawGridArpeg`, `_drawPoolArpeg` (4 lignes), `_drawInfoArpeg` placeholder, `_drawControlBarArpeg`. Refonte palette `GRID_ROLES` selon §11.1. Refonte wording UI labels (`Hld` → `P/S`, identiques grid/pool selon convention [§15.2](#§152--labels-grid-identiques-aux-labels-pool-info-en-toutes-lettres)).

#### §7.1.2 Patches

**Patch 1 — `ToolPadRoles.h`** : déclarations méthodes
```cpp
// --- Page ARPEG methods (defined in ToolPadRoles_Arpeg.cpp) ---
void _drawPageArpeg();
void _drawGridArpeg();
void _drawPoolArpeg();
void _drawInfoArpeg();           // placeholder en 3.E.1, complet en 3.E.3
void _drawControlBarArpeg();
void _handleEnterArpeg();        // body en 3.E.2
void _handleEnterPoolArpeg();    // body en 3.E.2
void _applyDefaultsArpeg();      // body en 3.E.3
void _clearRolesArpegOnly(uint8_t pad);  // helper page-scoped, body en 3.E.2
```

**Patch 2 — `SetupUI.h`** : macros couleur §11.1
```cpp
#define VT_PEACH   "\033[38;5;216m"  // Phase 3.E.1 placeholder §11.1 (Root), refined 3.H.1
#define VT_PURPLE  "\033[38;5;141m"  // Phase 3.E.1 placeholder §11.1 (Octave), refined 3.H.1
```

**Patch 3 — `SetupUI.cpp`** : extension switch inline `GRID_ROLES` §11.1 (cf §12.4 audit B-N1 — pas de table `COLORS_ROLES[]`, c'est un switch inline)

Refonte du switch `case GRID_ROLES` actuel (`SetupUI.cpp:534-543`) — modifier cases 2/4/5 vers couleurs spec §11.1 + **retirer le case 6 legacy `"Play/Stop" VT_BRIGHT_RED` orphelin** (audit 1 finding I3.1 : enum n'avait pas de valeur 6, label "Play/Stop" était mort. Avec `ROLE_CC=6` ajouté en 3.B, `_buildRoleMapX` ne l'émet jamais en GRID_ROLES — les pads CC en pages BANK/ARPEG/LOOP sont mappés via case 7 "ABSORBANT cross-page"). Cases 7/8 déjà ajoutés en 3.C.2 (§12.4).

```cpp
// SetupUI.cpp switch GRID_ROLES — refonte 3.E.1 (couleurs §11.1)
switch (roleMap[key]) {
  case 1:    color = VT_BLUE;                  break;  // BANK (legacy, audit 3.H.1 vs spec §11.1 "blanc")
  case 2:    color = VT_PEACH;                 break;  // ROOT (NEW 3.E.1, was VT_GREEN)
  case 3:    color = VT_CYAN;                  break;  // MODE (+ Chromatic, inchangé)
  case 4:    color = VT_PURPLE;                break;  // OCTAVE (NEW 3.E.1, was VT_YELLOW)
  case 5:    color = VT_GREEN;                 break;  // PLAY_STOP unifié ARPEG+LOOP (NEW 3.E.1, was VT_MAGENTA pour ROLE_HOLD)
  // case 6 retiré : ROLE_CC n'est pas émis en GRID_ROLES (mappé case 7 ABSORBANT par _buildRoleMapX, ou rendu par GRID_CONTROLPAD séparé en page CC)
  case 7:    color = VT_DIM VT_BG_AMBER_SAT;   break;  // ABSORBANT cross-page (NEW 3.C.2 §12.4)
  case 8:    color = VT_DIM;                   break;  // neutre ■■ cross-page (NEW 3.C.2 §12.4, BANK/CC seulement, pas ARPEG)
  case 0xFF: color = VT_RED;                   break;  // COLLISION (inchangé legacy)
  default:   color = VT_DIM;                   break;
}
```

Cases 9/10/11 ajoutés en 3.F.1 (`ROLE_REC`/`ROLE_CLR`/`ROLE_SLOT`) — cf §8.1.2 Patch 3.

**Patch 4 — `ToolPadRoles_Arpeg.cpp`** : remplacement stub + statics + body

Statics labels (grid = pool selon §15.2) :
```cpp
// Phase 3.E.1 — labels identiques grid/pool (convention §15.2)
static const char* GRID_ROOT_LABELS[7]      = { "A", "B", "C", "D", "E", "F", "G" };
static const char* GRID_MODE_LABELS[8]      = { "Ion", "Dor", "Phr", "Lyd", "Mix", "Aeo", "Loc", "Chr" };
static const char* GRID_OCTAVE_LABELS[4]    = { "Oct1", "Oct2", "Oct3", "Oct4" };
static const char* GRID_PLAY_STOP_LABELS[1] = { "P/S" };

static const char* POOL_ROOT_LABELS[]      = { "A", "B", "C", "D", "E", "F", "G" };
static const char* POOL_MODE_LABELS[]      = { "Ion", "Dor", "Phr", "Lyd", "Mix", "Aeo", "Loc", "Chr" };
static const char* POOL_OCTAVE_LABELS[]    = { "Oct1", "Oct2", "Oct3", "Oct4" };
static const char* POOL_PLAY_STOP_LABELS[] = { "P/S" };
```

`_buildRoleMapArpeg` body (remplace stub 3.B) :
```cpp
void ToolPadRoles::_buildRoleMapArpeg() {
  memset(_roleMap, ROLE_NONE, NUM_KEYS);
  for (int i = 0; i < NUM_KEYS; i++) memcpy(_roleLabels[i], " -- ", 5);

  // (1) Rôles ARPEG propres (priorité page ARPEG)
  for (uint8_t i = 0; i < 7; i++) {
    if (_wkRootPads[i] < NUM_KEYS) {
      _roleMap[_wkRootPads[i]] = ROLE_ROOT;
      strncpy(_roleLabels[_wkRootPads[i]], GRID_ROOT_LABELS[i], 5);
      _roleLabels[_wkRootPads[i]][5] = '\0';
    }
  }
  for (uint8_t i = 0; i < 7; i++) {
    if (_wkModePads[i] < NUM_KEYS) {
      _roleMap[_wkModePads[i]] = ROLE_MODE;
      strncpy(_roleLabels[_wkModePads[i]], GRID_MODE_LABELS[i], 5);
      _roleLabels[_wkModePads[i]][5] = '\0';
    }
  }
  if (_wkChromPad < NUM_KEYS) {
    _roleMap[_wkChromPad] = ROLE_MODE;
    strncpy(_roleLabels[_wkChromPad], GRID_MODE_LABELS[7], 5);  // "Chr"
    _roleLabels[_wkChromPad][5] = '\0';
  }
  for (uint8_t i = 0; i < 4; i++) {
    if (_wkOctavePads[i] < NUM_KEYS) {
      _roleMap[_wkOctavePads[i]] = ROLE_OCTAVE;
      strncpy(_roleLabels[_wkOctavePads[i]], GRID_OCTAVE_LABELS[i], 5);
      _roleLabels[_wkOctavePads[i]][5] = '\0';
    }
  }
  if (_wkArpPlayStopPad < NUM_KEYS) {
    _roleMap[_wkArpPlayStopPad] = ROLE_PLAY_STOP;
    strncpy(_roleLabels[_wkArpPlayStopPad], GRID_PLAY_STOP_LABELS[0], 5);
    _roleLabels[_wkArpPlayStopPad][5] = '\0';
  }

  // (2) §8.1 page ARPEG : absorbant cross-page → label dim ambre+
  //     CONTEXTUEL cross-page LOOP → invisible (cell reste " -- " §8.1)
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    if (_roleMap[i] != ROLE_NONE) continue;
    PadNeighborInfo info = _padNeighborInfo(i);

    if (info.bankIdx >= 0) {
      snprintf(_roleLabels[i], 6, "Bk%u", info.bankIdx + 1);  // §15.2 label = pool
      _roleMap[i] = 7;
      continue;
    }
    if (info.hasCc) {
      int8_t ccSlot = findControlPadEntryIdx(_wkCc, i);
      if (ccSlot >= 0) {
        uint8_t cc = _wkCc.entries[ccSlot].ccNumber;
        if (cc < 100) snprintf(_roleLabels[i], 6, "CC%02u", cc);
        else          snprintf(_roleLabels[i], 6, "C%u",   cc);
        _roleMap[i] = 7;
      }
      continue;
    }
    // LOOP voisins → invisible §8.1
  }
}
```

`_drawPageArpeg`, `_drawPoolArpeg` (4 lignes Root/Mode/Octave/PL/S), `_drawInfoArpeg` placeholder, `_drawGridArpeg`, `_drawControlBarArpeg` : bodies similaires aux patterns BANK (3.D.1) adaptés ARPEG.

**Patch 5 — `ToolPadRoles.cpp` orchestrateur** : extension `drawScreen()` dispatch SUB_ARPEG
```cpp
if (_activeSubPage == SUB_CC)         _drawPageCc();
else if (_activeSubPage == SUB_BANK)  _drawPageBank();
else if (_activeSubPage == SUB_ARPEG) _drawPageArpeg();  // NEW 3.E.1
else                                   { /* legacy fallback LOOP */ }
```

`run()` legacy dispatch reste pour SUB_ARPEG en 3.E.1 (handlers nouveaux branchés en 3.E.2).

**Patch 6 — `ToolPadRoles.cpp` legacy** : retrait des statics labels ARPEG migrés vers `_Arpeg.cpp`.

#### §7.1.3 Hard-asserts 3.E.1

```bash
# A. _buildRoleMapArpeg n'appelle plus _buildRoleMapLegacy
grep -A5 "void ToolPadRoles::_buildRoleMapArpeg" src/setup/ToolPadRoles_Arpeg.cpp | grep "_buildRoleMapLegacy"
# attendu : 0 matches

# B. Statics labels ARPEG migrés
grep -c "GRID_ROOT_LABELS\|GRID_MODE_LABELS\|GRID_OCTAVE_LABELS\|GRID_PLAY_STOP_LABELS" src/setup/ToolPadRoles_Arpeg.cpp
# attendu : ≥ 4

# C. Statics labels ARPEG retirés de ToolPadRoles.cpp
grep -c "GRID_ROOT_LABELS\|GRID_MODE_LABELS" src/setup/ToolPadRoles.cpp
# attendu : 0

# D. Macros VT_PEACH / VT_PURPLE définies
grep -E "VT_PEACH|VT_PURPLE" src/setup/SetupUI.h
# attendu : 2 matches

# E. drawScreen dispatch SUB_ARPEG
grep "_activeSubPage == SUB_ARPEG" src/setup/ToolPadRoles.cpp
# attendu : ≥ 1 match

# F. case 6 legacy "Play/Stop" retiré du switch GRID_ROLES (§12.4 audit + audit 1 I3.1)
grep -A1 "case 6:" src/setup/SetupUI.cpp | grep "Play/Stop"
# attendu : 0 matches (label legacy mort retiré)
```

#### §7.1.4 Mini-audit 3.E.1

1. **Couleurs Bank §11.1 dit "blanc" mais code VT_BLUE** : décision conserver legacy, audit 3.H.1.
2. **`_handleEnterArpeg` non branché en 3.E.1** : édition page ARPEG reste sur la branche legacy `else` jusqu'à 3.E.2.
3. **Cell display §8.1 page ARPEG : LOOP voisin invisible** : test G4 valide (pad portant uniquement Slot LOOP affiche `--`).

### §7.2 Phase 3.E.2 — Matrice édition §7.1 + §7.2

#### §7.2.1 Objectif

Brancher `_handleEnterArpeg` + `_handleEnterPoolArpeg` dans l'orchestrateur. Implémenter matrice §7.1 ARPEG :
- Pad vide → assign
- Rôle ARPEG propre intra-AC → swap-to-pool silencieux §7.2 (ancien rôle retourne pool sans modale)
- Rôle LOOP cross-AC voisin → assign par-dessus (coexistence §7.3, pas modale)
- Absorbant (BANK/CC) → no-op silencieux (focus refused §8.1)

Helper `_clearRolesArpegOnly(pad)` : retire uniquement les rôles ARPEG du pad, préserve cross-page (cf [§15.3](#§153--clear-role-page-scoped-via-_clearrolesxonly)).

#### §7.2.2 Patches

**Patch 1 — `ToolPadRoles_Arpeg.cpp`** : ajout helpers + handlers
```cpp
void ToolPadRoles::_clearRolesArpegOnly(uint8_t pad) {
  for (uint8_t i = 0; i < 7; i++) {
    if (_wkRootPads[i] == pad) _wkRootPads[i] = 0xFF;
  }
  for (uint8_t i = 0; i < 7; i++) {
    if (_wkModePads[i] == pad) _wkModePads[i] = 0xFF;
  }
  if (_wkChromPad == pad) _wkChromPad = 0xFF;
  for (uint8_t i = 0; i < 4; i++) {
    if (_wkOctavePads[i] == pad) _wkOctavePads[i] = 0xFF;
  }
  if (_wkArpPlayStopPad == pad) _wkArpPlayStopPad = 0xFF;
}

void ToolPadRoles::_handleEnterArpeg() {
  uint8_t pad = _gridRow * 12 + _gridCol;
  PadNeighborInfo info = _padNeighborInfo(pad);

  // §8.1 refus absorbants (no-op silencieux)
  if (info.bankIdx >= 0 || info.hasCc) return;

  // §7.1 ARPEG : ENTER ouvre pool (différent de BANK §7.4 strict)
  _editing = true;
  PadRole role = getRoleForPad(pad);
  if (role.line >= 2 && role.line <= 5) {
    _poolLine = role.line;
    _poolIdx = role.index;
  } else {
    _poolLine = 2;  // Root par défaut
    _poolIdx = 0;
  }
}

void ToolPadRoles::_handleEnterPoolArpeg() {
  uint8_t pad = _gridRow * 12 + _gridCol;

  if (_poolLine == 0) {
    // [---] clear role page-scoped (§15.3)
    _clearRolesArpegOnly(pad);
    if (saveAll()) _ui->flashSaved();
    _editing = false;
    return;
  }

  // Defensif
  PadNeighborInfo info = _padNeighborInfo(pad);
  if (info.bankIdx >= 0 || info.hasCc) {
    _editing = false;
    return;
  }

  // §7.2 swap-to-pool intra-AC silencieux
  uint8_t roleOwnerPad = findPadWithRole(_poolLine, _poolIdx);
  if (roleOwnerPad < NUM_KEYS && roleOwnerPad != pad) {
    _clearRolesArpegOnly(roleOwnerPad);  // steal silencieux
  }
  _clearRolesArpegOnly(pad);  // libère pad cible (intra-AC)
  assignRole(pad, _poolLine, _poolIdx);

  // §7.3 coexistence cross-AC LOOP : pas d'action sur _wkLoopPad
  if (saveAll()) _ui->flashSaved();
  _editing = false;
}
```

**Patch 2 — `ToolPadRoles.cpp` orchestrateur** : extension `run()` dispatch SUB_ARPEG
```cpp
} else if (_activeSubPage == SUB_ARPEG) {
  if (ev.type == NAV_ENTER) {
    if (!_editing) _handleEnterArpeg();
    else            _handleEnterPoolArpeg();
    screenDirty = true;
  }
  // arrow nav preserved from legacy
}
```

#### §7.2.3 Hard-asserts 3.E.2

```bash
# A. _handleEnterArpeg + _handleEnterPoolArpeg définis
grep -c "ToolPadRoles::_handleEnter.*Arpeg" src/setup/ToolPadRoles_Arpeg.cpp
# attendu : ≥ 2

# B. _clearRolesArpegOnly défini et utilisé
grep -c "_clearRolesArpegOnly" src/setup/ToolPadRoles_Arpeg.cpp
# attendu : ≥ 3 (definition + 2 usages)

# C. Swap intra-AC §7.2 visible
grep -E "_clearRolesArpegOnly\(roleOwnerPad\)|_clearRolesArpegOnly\(pad\)" src/setup/ToolPadRoles_Arpeg.cpp
# attendu : ≥ 2

# D. Refus absorbants dans handlers
grep -cE "info\.bankIdx >= 0|info\.hasCc" src/setup/ToolPadRoles_Arpeg.cpp
# attendu : ≥ 2

# E. run() dispatch SUB_ARPEG branché
grep -c "_activeSubPage == SUB_ARPEG" src/setup/ToolPadRoles.cpp
# attendu : ≥ 2 (drawScreen + run)
```

#### §7.2.4 Mini-audit 3.E.2

1. **§7.2 swap silencieux vs §7.4 strict BANK** : sémantique divergente intentionnelle. Acté [§14.6](#§146--sémantique-enter-divergente-pages-absorbantes-vs-contextuelles).
2. **`_clearRolesArpegOnly` création** : helper page-scoped (§15.3) remplace `clearRole(pad)` legacy cross-catégorie pour opérations ARPEG.
3. **Coexistence cross-AC §7.3 LOOP** : code prêt (n'agit pas sur `_wkLoopPad`). Test complet end-to-end en 3.F.3.

### §7.3 Phase 3.E.3 — Info panel + `d` ARPEG + finalisation

#### §7.3.1 Objectif

Compléter `_drawInfoArpeg` langue musicien (§8.5 + §15.2). Implémenter `_applyDefaultsArpeg()` page-scoped (§15) avec valeurs hardcoded legacy et skip silencieux des conflits cross-page. Dispatch `_confirmDefaults` orchestrateur. Suppression `clearAllRoles()` et `resetToDefaults()` legacy.

#### §7.3.2 Patches

**Patch 1 — `ToolPadRoles_Arpeg.cpp` `_drawInfoArpeg` complet** : langue musicien §15.2 (F7 audit 1 I4.4 — clôture dette doc)

Body concret (symétrique au `_drawInfoLoop` §8.3.2 Patch 1) :
```cpp
void ToolPadRoles::_drawInfoArpeg() const {
  uint8_t pad = _gridRow * 12 + _gridCol;
  PadNeighborInfo info = _padNeighborInfo(pad);
  char line1[80], line2[80] = {0};

  // Ligne 1 : rôle propre ARPEG (Root/Mode/Chrom/Octave/PL/S)
  static const char* rootNames[7] = {"A","B","C","D","E","F","G"};
  static const char* modeNamesShort[8] = {"Ion","Dor","Phr","Lyd","Mix","Aeo","Loc","Chr"};
  static const char* modeNamesLong[8]  = {
    "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian", "Chromatic"
  };

  if (info.scaleRole.kind == ScaleRoleKind::ROOT) {
    snprintf(line1, sizeof(line1), "Pad %u : Root %s", pad + 1, rootNames[info.scaleRole.idx]);
  } else if (info.scaleRole.kind == ScaleRoleKind::MODE) {
    snprintf(line1, sizeof(line1), "Pad %u : Mode %s (%s)",
             pad + 1, modeNamesLong[info.scaleRole.idx], modeNamesShort[info.scaleRole.idx]);
  } else if (info.scaleRole.kind == ScaleRoleKind::CHROM) {
    snprintf(line1, sizeof(line1), "Pad %u : Chromatic", pad + 1);
  } else if (info.arpRole.kind == ArpRoleKind::OCTAVE) {
    snprintf(line1, sizeof(line1), "Pad %u : Octave %u", pad + 1, info.arpRole.idx + 1);
  } else if (info.arpRole.kind == ArpRoleKind::PLAY_STOP) {
    // PL/S unifié §14.1 — détection partage avec LOOP
    if (_wkArpPlayStopPad == _wkLoopPad.playStopPad && _wkLoopPad.playStopPad < NUM_KEYS) {
      snprintf(line1, sizeof(line1),
               "Pad %u : Play/Stop ARPEG + LOOP (geste unifie §14.1)", pad + 1);
    } else {
      snprintf(line1, sizeof(line1), "Pad %u : Play/Stop ARPEG", pad + 1);
    }
  } else {
    snprintf(line1, sizeof(line1), "Pad %u : libre pour ARPEG", pad + 1);
  }

  // Ligne 2 : voisins cross-page (absorbants ou contextuels LOOP)
  if (info.bankIdx >= 0) {
    snprintf(line2, sizeof(line2), "  + Bank %d (page BANK, absorbant — interdit ici)",
             info.bankIdx + 1);
  } else if (info.hasCc) {
    int8_t ccSlot = findControlPadEntryIdx(_wkCc, pad);
    if (ccSlot >= 0) {
      snprintf(line2, sizeof(line2), "  + CC%u (page CC, absorbant — interdit ici)",
               _wkCc.entries[ccSlot].ccNumber);
    }
  } else {
    // Voisins contextuels LOOP → coexistence §7.3 (informatif)
    char loopNeighborBuf[60] = {0};
    _formatLoopNeighbor(info, loopNeighborBuf, sizeof(loopNeighborBuf));
    if (loopNeighborBuf[0]) {
      snprintf(line2, sizeof(line2), "  + %s (page LOOP, coexistence §7.3)", loopNeighborBuf);
    }
  }

  _ui->drawFrameLine(line1);
  if (line2[0]) _ui->drawFrameLine(line2);
}
```

Helpers `_formatArpegRole`, `_formatLoopNeighbor`, `_drawArpegRoleDescription` : utility wrappers compacts pour réutilisation par modale (§9.1.2 `_formatOverwriteWording`).

**Patch 2 — `ToolPadRoles_Arpeg.cpp` `_applyDefaultsArpeg`** : restore defaults §15
```cpp
void ToolPadRoles::_applyDefaultsArpeg() {
  // §15.5 : skip silencieux des pads occupés par absorbants cross-page
  // §15.4 : valeurs hardcoded legacy (à valider/swap post-HW Loïc)

  // Clear current ARPEG roles (only ARPEG, preserve cross-page)
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    _clearRolesArpegOnly(i);
  }

  auto tryAssign = [&](uint8_t pad, uint8_t* slot) {
    if (pad >= NUM_KEYS) return;
    PadNeighborInfo info = _padNeighborInfo(pad);
    if (info.bankIdx >= 0 || info.hasCc) return;  // §15.5 skip silencieux
    *slot = pad;
  };

  for (uint8_t i = 0; i < 7; i++)  tryAssign(8 + i,  &_wkRootPads[i]);
  for (uint8_t i = 0; i < 7; i++)  tryAssign(15 + i, &_wkModePads[i]);
  tryAssign(22, &_wkChromPad);
  tryAssign(23, &_wkArpPlayStopPad);
  for (uint8_t i = 0; i < 4; i++)  tryAssign(25 + i, &_wkOctavePads[i]);
}
```

**Patch 3 — `ToolPadRoles.cpp` orchestrateur** : dispatch `d` raccourci page-scoped (§15.1)
```cpp
if (ev.type == NAV_DEFAULTS && !_editing) {  // 'd' key (NAV_DEFAULTS = 'd')
  _confirmDefaults = true;
  screenDirty = true;
}

if (_confirmDefaults) {
  ConfirmResult r = SetupUI::parseConfirm(ev);
  if (r == CONFIRM_YES) {
    switch (_activeSubPage) {
      case SUB_BANK:  _applyDefaultsBank();   break;
      case SUB_ARPEG: _applyDefaultsArpeg();  break;
      case SUB_LOOP:  _applyDefaultsLoop();   break;
      case SUB_CC:    _applyDefaultsCc();     break;
    }
    if (saveAll()) _ui->flashSaved();
    _confirmDefaults = false;
    screenDirty = true;
  } else if (r == CONFIRM_NO) {
    _confirmDefaults = false;
    screenDirty = true;
  }
}
```

Info panel confirm prompt (français, cohérent modale §10) :
```cpp
if (_confirmDefaults) {
  _ui->drawFrameLine(VT_YELLOW "Restaurer les defauts de cette page ? (y/n)" VT_RESET);
}
```

**Patch 4 — `ToolPadRoles.cpp` legacy retraits** :
- `clearAllRoles()` supprimé (`r` raccourci retiré §15.1)
- `resetToDefaults()` legacy supprimé (remplacé par `_applyDefaultsPage*()` page-scoped)
- `_confirmClearAll` flag legacy retiré (le `r` n'existe plus)
- `clearRole(pad)` legacy conservé temporairement → utilisé pour migrations transitoires, retiré en 3.H.2

**Patch 5 — Stubs `_applyDefaultsBank/Loop/Cc` — "reserved actifs"** (cohérent CLAUDE.md projet "qualité finale, pas de body vide silencieux") :

Déclarés dans `ToolPadRoles.h`, bodies stubs **avec feedback observable** en 3.E.3. Si bodies réels manquent à la phase d'EXEC correspondante, `d` page X affiche un flash explicite au lieu d'un `flashSaved` trompeur. Bodies réels remplacent ces stubs :
- `_applyDefaultsBank()` → body réel en 3.D.2 (rétroactif §6.6 cf §6 mise à jour)
- `_applyDefaultsLoop()` → body réel en 3.F.3
- `_applyDefaultsCc()` → body réel en 3.C.2 (vide `_wkCc.count = 0` + memset, équivalent migré `_resetAllCc` Tool 4)

Stubs reserved actifs (3.E.3) :
```cpp
void ToolPadRoles::_applyDefaultsBank() {
  // Stub reserved actif — body réel livré en 3.D.2 (§6.6)
  _setFlash("Defaults BANK reserved (livre en 3.D.2)");
}
void ToolPadRoles::_applyDefaultsLoop() {
  // Stub reserved actif — body réel livré en 3.F.3
  _setFlash("Defaults LOOP reserved (livre en 3.F.3)");
}
void ToolPadRoles::_applyDefaultsCc() {
  // Stub reserved actif — body réel livré en 3.C.2
  _setFlash("Defaults CC reserved (livre en 3.C.2)");
}
```

**Décision actée** (cohérent CLAUDE.md projet "qualité finale, pas de body vide silencieux qui ferait passer `flashSaved` trompeur"). Le `d` page X ne peut pas faire `if (saveAll()) _ui->flashSaved()` trompeur en cas d'oubli body réel — le flash explicite domine et signale au testeur qu'un travail est manquant.

**Hard-assert cross-phase** (validation post-EXEC complète) :
```bash
grep -E "_setFlash.*reserved" src/setup/ToolPadRoles.cpp src/setup/ToolPadRoles_*.cpp
# attendu : 0 matches après 3.D.2 + 3.C.2 + 3.F.3 livrés (flash stubs disparus, bodies réels en place)
```

#### §7.3.3 Hard-asserts 3.E.3

```bash
# A. _drawInfoArpeg utilise helpers langue musicien
grep -cE "_formatArpegRole|_formatLoopNeighbor|_drawArpegRoleDescription" src/setup/ToolPadRoles_Arpeg.cpp
# attendu : ≥ 4

# B. _applyDefaultsArpeg avec tryAssign + skip silencieux
grep -B2 -A8 "_applyDefaultsArpeg" src/setup/ToolPadRoles_Arpeg.cpp | grep -E "tryAssign|info\.bankIdx|info\.hasCc"
# attendu : ≥ 3 matches

# C. Dispatch _confirmDefaults orchestrateur 4 cas SUB_*
grep -cE "_applyDefaultsBank|_applyDefaultsArpeg|_applyDefaultsLoop|_applyDefaultsCc" src/setup/ToolPadRoles.cpp
# attendu : ≥ 4

# D. Wording confirm prompt français
grep "Restaurer les defauts" src/setup/ToolPadRoles.cpp
# attendu : 1 match

# E. clearAllRoles() legacy supprimé
grep "clearAllRoles" src/setup/ToolPadRoles.cpp src/setup/ToolPadRoles.h
# attendu : 0 matches

# F. resetToDefaults() legacy supprimé
grep "resetToDefaults" src/setup/ToolPadRoles.cpp src/setup/ToolPadRoles.h
# attendu : 0 matches

# G. _confirmClearAll legacy supprimé (r raccourci retiré)
grep "_confirmClearAll" src/setup/ToolPadRoles.cpp src/setup/ToolPadRoles.h
# attendu : 0 matches
```

#### §7.3.4 Mini-audit 3.E.3

1. **`_applyDefaultsBank/Loop/Cc` stubs en 3.E.3** : déclarés vides pour que compile passe. Bodies remplis dans 3.D.2 (rétroactif), 3.F.3, 3.C.2 (rétroactif). À acter dans §6 + §5 + §8 mise à jour.

2. **`_confirmDefaults` flag sémantique** : was cross-page reset (Tool 3 legacy), devient page-scoped defaults. Documentation dans commit message.

3. **Skip silencieux `d` ARPEG** : si pad 8 (default Root A) porte Bank 1, default Root A skipped. Pool ARPEG affiche Root A vert menthe libre. User assigne manuellement. Acceptable §15.5.

4. **`r` raccourci retiré** : convention setup-tools-conventions §6.2 "r : Clear all roles (with y/n confirm)" — déviation documentée dans Tool PAD ROLE (`r` non supporté, le bouton `d` suffit per modèle user §15).

### §7.4 HW Gate G4 (combiné 3.E.1 + 3.E.2 + 3.E.3)

**Setup préalable** :
- 8 banks assignées (depuis 3.D test G3)
- pad 10 = CC0 (depuis 3.C test G2)
- **pad 8 = CC1 (NEW : ajouté manuellement avant G4, requis pour test skip silencieux step 5 — oubli setup audit 2)**
- Dev seed M7 LOOP REC/PS/CLR sur pads 32/33/34

**Procédure** :
1. Boot. Entrer Tool 3 → TAB jusqu'à page ARPEG.
2. Vérifier rendu §8.1 (2 types suffisent — pattern identique cross-pages) : pad 0 (Bank 1) `Bk1` dim ambre+, pad 32 (LOOP REC) `--` invisible. (Pad 10 CC0 ambre+ et pad 33/34 invisibles : observation passante.)
3. Vérifier pool 4 lignes : Root `A B C D E F G`, Mode `Ion Dor Phr Lyd Mix Aeo Loc Chr`, Oct `Oct1 Oct2 Oct3 Oct4`, PL/S `P/S` — tous vert menthe.
4. Test `d` defaults ARPEG :
   - `d` → confirm prompt "Restaurer les defauts de cette page ? (y/n)"
   - `y` → defaults factory appliqués :
     - Pads 8 (skipped — CC1 préassigné §15.5), 9-14 : Root B C D E F G (pêche)
     - Pads 15-21 : Mode Ion..Loc (cyan)
     - Pad 22 : Chr (cyan)
     - Pad 23 : P/S (vert)
     - Pads 25-28 : Oct1..Oct4 (pourpre)
   - Banks et CC inchangés
5. Test conflict skip silencieux **(setup CC1 pad 8 fait au préalable G4 setup)** : observation immédiate de step 4 — Root A pad 8 skipped, pool ARPEG ligne Root affiche `A` vert menthe libre.
6. Test swap intra-AC §7.2 :
   - ENTER sur pad 9 (Root B, post-d) → pool ligne Root, cursor sur "B"
   - Naviguer cursor sur "C" (déjà pad 10... non, pad 10 = CC0. Cursor sur "D" (déjà pad 11))
   - ENTER → silent steal : pad 9 = Root D, pad 11 = `--`, pool Root B vert menthe
7. Test refus absorbants (1 cas suffit, pattern symétrique) : ENTER sur pad 0 (Bank 1) → no-op silencieux. (Pad 10 CC0 : même comportement, vérification rapide.)
8. Test info panel langue musicien (2 cas — F8 wording suffixe ARPEG) :
   - Curseur pad 9 (Root D post-swap) → "Pad 10 — Root D de ARPEG"
   - Curseur pad 32 → "Pad 33 — free for ARPEG (REC LOOP en page LOOP — coexistence possible)"
9. Test `[---] clear role` page-scoped :
   - ENTER sur pad 23, `[---]`, ENTER → P/S retiré pad 23, dev seed LOOP pad 32 inchangé
10. q exit → hard-constraint OK + reboot persistance (fusionné)

**Critères** :
- ✓ Cell display §8.1 (absorbant ambre+, contextuel LOOP invisible)
- ✓ Pool 4 lignes couleurs §11.1 + vert menthe
- ✓ `d` defaults ARPEG factory + skip silencieux
- ✓ Swap intra-AC §7.2 silencieux
- ✓ Refus absorbants no-op
- ✓ Info panel langue musicien en toutes lettres
- ✓ `[---] clear role` page-scoped
- ✓ Save persiste reboot

### §7.5 Décisions actées 3.E

1. Wording UI labels grid=pool (§15.2) : `A`/`B`/.../`G`, `Ion`/`Dor`/.../`Chr`, `Oct1`-`Oct4`, `P/S`.
2. Macros `VT_PEACH`, `VT_PURPLE` placeholders §11.1.
3. Palette `GRID_ROLES` : Root pêche, Octave pourpre, PL/S vert (Mode cyan inchangé).
4. Statics labels ARPEG migrés vers `ToolPadRoles_Arpeg.cpp`.
5. §7.2 swap intra-AC silencieux en ARPEG (vs §7.4 strict BANK — divergent intentionnel §14.6).
6. `_clearRolesArpegOnly(pad)` page-scoped (§15.3).
7. `_applyDefaultsArpeg()` page-scoped avec skip silencieux (§15.5).
8. Flag `_confirmDefaults` réutilisé sémantique page-scoped.
9. `clearAllRoles()` + `resetToDefaults()` + `_confirmClearAll` legacy supprimés (`r` raccourci retiré §15.1).
10. 3 helpers formatage info panel : `_formatArpegRole`, `_formatLoopNeighbor`, `_formatRoleNameMusician` (unification possible 3.G.1).

---

## §8 Phase 3.F — Page LOOP complète

### §8.0 Vue d'ensemble

Page LOOP = **2ème page CONTEXTUELLE**, symétrique à ARPEG (mêmes patterns swap intra-AC §7.2). 4 catégories de rôles : REC, PL/S LOOP, CLR, Slots × 16. Pool 4 lignes. Couleurs §11.1 : REC rouge, CLR bleu foncé, PL/S LOOP vert (même qu'ARPEG = geste unifié §14.1), Slots jaune.

3 sous-phases avec HW Gate G5 unique :
- **3.F.1** Rendu : `_buildRoleMapLoop`, 5 méthodes draw, palette `GRID_ROLES` étendue codes 9/10/11, macro `VT_DARK_BLUE`, migration statics LOOP
- **3.F.2** Matrice édition §7.1 LOOP : assign, swap intra-AC silencieux §7.2, refus absorbants, helper `_clearRolesLoopOnly(pad)` page-scoped
- **3.F.3** Info panel langue musicien + `_applyDefaultsLoop()` page-scoped (§15) + **coexistence cross-AC ARPEG-LOOP testable end-to-end** (HW gate G5 scenario §14.1)

Volume cumulé : ~750-950 L.

### §8.1 Phase 3.F.1 — Rendu page LOOP

#### §8.1.1 Objectif

Remplacer le stub `_buildRoleMapLoop()` (3.B delegating legacy) par sa vraie impl. Créer `_drawPageLoop`, `_drawGridLoop`, `_drawPoolLoop` (4 lignes), `_drawInfoLoop` placeholder, `_drawControlBarLoop`. Étendre palette `GRID_ROLES` avec codes 9/10/11. Migrer statics labels LOOP vers `ToolPadRoles_Loop.cpp`. Labels grid = pool (§15.2) : `REC`, `P/S`, `CLR`, `S0`..`S15`.

#### §8.1.2 Patches

**Patch 1 — `ToolPadRoles.h`** : déclarations méthodes + nouveaux ROLE codes + extension pool counts
```cpp
// --- Page LOOP methods (defined in ToolPadRoles_Loop.cpp) ---
void _drawPageLoop();
void _drawGridLoop();
void _drawPoolLoop();
void _drawInfoLoop();             // placeholder en 3.F.1, complet en 3.F.3
void _drawControlBarLoop();
void _handleEnterLoop();          // body en 3.F.2
void _handleEnterPoolLoop();      // body en 3.F.2
void _applyDefaultsLoop();        // body en 3.F.3
void _clearRolesLoopOnly(uint8_t pad);  // helper page-scoped (§15.3)
```

Extension enum `PadRoleCode` :
```cpp
enum PadRoleCode : uint8_t {
  ROLE_NONE       = 0,
  ROLE_BANK       = 1,
  ROLE_ROOT       = 2,
  ROLE_MODE       = 3,
  ROLE_OCTAVE     = 4,
  ROLE_PLAY_STOP  = 5,    // PL/S ARPEG ET PL/S LOOP unifiés §11.1 (vert §14.1)
  ROLE_CC         = 6,
  // map=7 = absorbant cross-page, map=8 = neutre ■■ cross-page
  ROLE_REC        = 9,    // NEW 3.F.1 (LOOP REC, rouge §11.1)
  ROLE_CLR        = 10,   // NEW 3.F.1 (LOOP CLEAR, bleu foncé §11.1)
  ROLE_SLOT       = 11,   // NEW 3.F.1 (LOOP Slot, jaune §11.1)
  ROLE_COLLISION  = 0xFF
};
```

Pool extension :
```cpp
static const uint8_t POOL_SLOT_COUNT  = 16;
static const uint8_t POOL_LINE_COUNT  = 10;   // 0=clear, 1=bank, 2-5=ARPEG, 6-9=LOOP
```

**Patch 2 — `SetupUI.h`** : macro `VT_DARK_BLUE`
```cpp
#define VT_DARK_BLUE  "\033[38;5;19m"   // Phase 3.F.1 placeholder §11.1 (LOOP CLEAR), refined 3.H.1
```

**Patch 3 — `SetupUI.cpp`** : extension palette `GRID_ROLES` à 12 entries
```cpp
const char* COLORS_ROLES[] = {
  VT_DIM,                   // 0 = ROLE_NONE
  VT_BLUE,                  // 1 = ROLE_BANK (legacy, audit 3.H.1)
  VT_PEACH,                 // 2 = ROLE_ROOT (3.E.1)
  VT_CYAN,                  // 3 = ROLE_MODE (+ Chromatic)
  VT_PURPLE,                // 4 = ROLE_OCTAVE (3.E.1)
  VT_GREEN,                 // 5 = ROLE_PLAY_STOP (PL/S unifié §11.1)
  VT_RED,                   // 6 = ROLE_COLLISION (existant)
  VT_DIM VT_BG_AMBER_SAT,   // 7 = absorbant cross-page (3.C.2/3.D.2)
  VT_DIM,                   // 8 = neutre ■■ cross-page (3.C.2/3.D.2)
  VT_RED,                   // 9 = ROLE_REC (NEW 3.F.1, §11.1 rouge)
  VT_DARK_BLUE,             // 10 = ROLE_CLR (NEW 3.F.1, §11.1 bleu foncé)
  VT_YELLOW,                // 11 = ROLE_SLOT (NEW 3.F.1, §11.1 jaune)
};
```

**Patch 4 — `ToolPadRoles_Loop.cpp`** : remplacement stub + statics + body

Statics labels (§15.2 grid = pool identiques) :
```cpp
// Phase 3.F.1 — labels identiques grid/pool (convention §15.2)
static const char* GRID_REC_LABELS[1]   = { "REC" };
static const char* GRID_PS_LABELS[1]    = { "P/S" };  // identique ARPEG (PL/S unifié §11.1)
static const char* GRID_CLR_LABELS[1]   = { "CLR" };
static const char* GRID_SLOT_LABELS[16] = {
  "S0", "S1", "S2",  "S3",  "S4",  "S5",  "S6",  "S7",
  "S8", "S9", "S10", "S11", "S12", "S13", "S14", "S15"
};

static const char* POOL_REC_LABELS[]  = { "REC" };
static const char* POOL_PS_LABELS[]   = { "P/S" };
static const char* POOL_CLR_LABELS[]  = { "CLR" };
static const char* POOL_SLOT_LABELS[16] = {
  "S0", "S1", "S2",  "S3",  "S4",  "S5",  "S6",  "S7",
  "S8", "S9", "S10", "S11", "S12", "S13", "S14", "S15"
};
```

`_buildRoleMapLoop` body :
```cpp
void ToolPadRoles::_buildRoleMapLoop() {
  memset(_roleMap, ROLE_NONE, NUM_KEYS);
  for (int i = 0; i < NUM_KEYS; i++) memcpy(_roleLabels[i], " -- ", 5);

  // (1) Rôles LOOP propres (priorité page LOOP)
  if (_wkLoopPad.recPad < NUM_KEYS) {
    _roleMap[_wkLoopPad.recPad] = ROLE_REC;
    strncpy(_roleLabels[_wkLoopPad.recPad], GRID_REC_LABELS[0], 5);
    _roleLabels[_wkLoopPad.recPad][5] = '\0';
  }
  if (_wkLoopPad.playStopPad < NUM_KEYS) {
    _roleMap[_wkLoopPad.playStopPad] = ROLE_PLAY_STOP;
    strncpy(_roleLabels[_wkLoopPad.playStopPad], GRID_PS_LABELS[0], 5);
    _roleLabels[_wkLoopPad.playStopPad][5] = '\0';
  }
  if (_wkLoopPad.clearPad < NUM_KEYS) {
    _roleMap[_wkLoopPad.clearPad] = ROLE_CLR;
    strncpy(_roleLabels[_wkLoopPad.clearPad], GRID_CLR_LABELS[0], 5);
    _roleLabels[_wkLoopPad.clearPad][5] = '\0';
  }
  for (uint8_t i = 0; i < 16; i++) {
    if (_wkLoopPad.slotPads[i] < NUM_KEYS) {
      _roleMap[_wkLoopPad.slotPads[i]] = ROLE_SLOT;
      strncpy(_roleLabels[_wkLoopPad.slotPads[i]], GRID_SLOT_LABELS[i], 5);
      _roleLabels[_wkLoopPad.slotPads[i]][5] = '\0';
    }
  }

  // (2) §8.1 page LOOP : absorbant cross-page → label dim ambre+
  //     CONTEXTUEL cross-AC ARPEG voisin → invisible (cell reste " -- " §8.1)
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    if (_roleMap[i] != ROLE_NONE) continue;
    PadNeighborInfo info = _padNeighborInfo(i);

    if (info.bankIdx >= 0) {
      snprintf(_roleLabels[i], 6, "Bk%u", info.bankIdx + 1);  // §15.2 label
      _roleMap[i] = 7;
      continue;
    }
    if (info.hasCc) {
      int8_t ccSlot = findControlPadEntryIdx(_wkCc, i);
      if (ccSlot >= 0) {
        uint8_t cc = _wkCc.entries[ccSlot].ccNumber;
        if (cc < 100) snprintf(_roleLabels[i], 6, "CC%02u", cc);
        else          snprintf(_roleLabels[i], 6, "C%u",   cc);
        _roleMap[i] = 7;
      }
      continue;
    }
    // ARPEG voisins → invisible §8.1
  }
}
```

`_drawPageLoop`, `_drawPoolLoop` (4 lignes : REC, PL/S, CLR, Slots), `_drawInfoLoop` placeholder, `_drawGridLoop`, `_drawControlBarLoop` : bodies similaires aux patterns ARPEG (3.E.1).

**Patch 5 — `ToolPadRoles.cpp` orchestrateur** : extension `drawScreen()` dispatch SUB_LOOP
```cpp
if (_activeSubPage == SUB_CC)         _drawPageCc();
else if (_activeSubPage == SUB_BANK)  _drawPageBank();
else if (_activeSubPage == SUB_ARPEG) _drawPageArpeg();
else if (_activeSubPage == SUB_LOOP)  _drawPageLoop();   // NEW 3.F.1
```

`run()` legacy dispatch reste pour SUB_LOOP en 3.F.1 (handlers branchés en 3.F.2). **Plus de branche `else` legacy fallback** après 3.F.1 — toutes les pages ont leur dispatch propre. `_buildRoleMapLegacy` n'est plus consommé que par le `default` switch (chemin mort, retiré en 3.H.2 selon L1).

**Patch 6 — `ToolPadRoles.cpp` `saveAll()` extension LoopPadStore** (déplacé depuis §8.3 / §12.12 audit M6) :

**Rationale** : `saveAll()` legacy ne persiste pas `LoopPadStore`. Si l'extension est livrée seulement en 3.F.3, la fenêtre 3.F.2 (handlers LOOP branchés, `saveAll()` appelé après chaque ENTER) montre un comportement `flashSaved` trompeur — changements LOOP perdus au reboot. L'extension doit donc précéder le branchement des handlers (3.F.2). Livraison en 3.F.1 = bonne fenêtre.

```cpp
// ToolPadRoles.cpp saveAll() — après les 3 saves existants BankPad+ScalePad+ArpPad :
// 4. LoopPadStore (NEW 3.F.1, déplacé de 3.F.3 §12.12 audit M6 pour éviter
//    fenêtre data loss entre 3.F.2 et 3.F.3)
_nvs->setLoadedLoopPad(_wkLoopPad);
if (!_nvs->saveLoopPad()) allOk = false;
```

Vérification API NvsManager : `setLoadedLoopPad(const LoopPadStore&)` inline `NvsManager.h:88`, `saveLoopPad()` `NvsManager.cpp:1246-1252` retourne `bool`. **Signatures conformes — patch applicable sans modification API.**

**Note CC** : `_saveCc` legacy (pattern save-per-commit Tool 4, conservé §14.1) persiste déjà `ControlPadStore` via `NvsManager::saveBlob` direct. Pas besoin d'extension `saveAll` pour CC.

**Patch 7 — `ToolPadRoles.cpp` `poolLineSize()` extension page-scoped + skip silencieux nav circulaire** (audit B-E2 + §12.5) :

Avec `POOL_LINE_COUNT = 10` (Patch 1), la nav circulaire pool (`ToolPadRoles.cpp:842,848`) cyclerait sur les lignes 6-9 même en pages BANK/ARPEG/CC où ces lignes sont vides. `poolLineSize()` doit retourner page-scoped pour lignes 6-9 ; nav circulaire doit skipper silencieusement les lignes vides.

a. `poolLineSize()` extension (test interne `_activeSubPage` — minimise diff vs signature avec param) :
```cpp
uint8_t ToolPadRoles::poolLineSize(uint8_t line) const {
  // Lignes 0-5 : page-agnostic legacy
  switch (line) {
    case 0: return 1;                // [---] clear role (toujours valide)
    case 1: return POOL_BANK_COUNT;  // Banks
    case 2: return POOL_ROOT_COUNT;  // Roots ARPEG
    case 3: return POOL_MODE_COUNT;  // Modes ARPEG
    case 4: return POOL_OCTAVE_COUNT;// Octaves ARPEG
    case 5: return POOL_HOLD_COUNT;  // PL/S unifié
  }
  // Lignes 6-9 : page LOOP uniquement (NEW 3.F.1)
  if (_activeSubPage == SUB_LOOP) {
    switch (line) {
      case 6: return 1;              // REC
      case 7: return 1;              // PL/S (peut être identique pad ARPEG, §14.1)
      case 8: return 1;              // CLR
      case 9: return POOL_SLOT_COUNT;// 16 Slots
    }
  }
  return 0;  // hors page LOOP : lignes 6-9 inactives
}
```

b. Skip silencieux nav circulaire (`run()` lignes 842 et 848) :
```cpp
// UP arrow pool nav (L842)
do {
  if (_poolLine == 0) _poolLine = POOL_LINE_COUNT - 1;
  else _poolLine--;
} while (poolLineSize(_poolLine) == 0);
uint8_t sz = poolLineSize(_poolLine);
if (sz > 0 && _poolIdx >= sz) _poolIdx = sz - 1;

// DOWN arrow pool nav (L848)
do {
  if (_poolLine == POOL_LINE_COUNT - 1) _poolLine = 0;
  else _poolLine++;
} while (poolLineSize(_poolLine) == 0);
sz = poolLineSize(_poolLine);
if (sz > 0 && _poolIdx >= sz) _poolIdx = sz - 1;
```

**Note** : la ligne 0 (`[---] clear role`) retourne toujours `1` — elle est valide et toujours atteignable, ce qui évite une boucle infinie. La ligne 5 (PL/S) retourne `POOL_HOLD_COUNT = 1` (le pad PL/S est unique) et reste valide en page ARPEG/LOOP (PL/S unifié §14.1).

#### §8.1.3 Hard-asserts 3.F.1

```bash
# A. _buildRoleMapLoop n'appelle plus _buildRoleMapLegacy
grep -A5 "void ToolPadRoles::_buildRoleMapLoop" src/setup/ToolPadRoles_Loop.cpp | grep "_buildRoleMapLegacy"
# attendu : 0 matches

# B. Statics labels LOOP migrés
grep -cE "GRID_REC_LABELS|GRID_CLR_LABELS|GRID_PS_LABELS|GRID_SLOT_LABELS" src/setup/ToolPadRoles_Loop.cpp
# attendu : ≥ 4

# C. ROLE codes nouveaux définis
grep -E "ROLE_REC.*9|ROLE_CLR.*10|ROLE_SLOT.*11" src/setup/ToolPadRoles.h
# attendu : 3 matches

# D. VT_DARK_BLUE défini
grep "VT_DARK_BLUE" src/setup/SetupUI.h
# attendu : 1 match

# E. drawScreen dispatch SUB_LOOP
grep "_activeSubPage == SUB_LOOP" src/setup/ToolPadRoles.cpp
# attendu : ≥ 1 match

# F. Palette GRID_ROLES étendue à 12 entries
grep -A14 "COLORS_ROLES\[\]" src/setup/SetupUI.cpp
# attendu : présence VT_RED (map=9), VT_DARK_BLUE (map=10), VT_YELLOW (map=11)

# G. POOL_LINE_COUNT = 10
grep "POOL_LINE_COUNT" src/setup/ToolPadRoles.h
# attendu : "POOL_LINE_COUNT = 10"

# H. saveAll() persiste LoopPadStore (Patch 6 déplacé de 3.F.3, audit B-E2 + §12.12)
grep -E "_nvs->saveLoopPad|setLoadedLoopPad.*_wkLoopPad" src/setup/ToolPadRoles.cpp
# attendu : ≥ 2 (un appel setLoaded + un saveLoopPad dans saveAll body)

# I. poolLineSize() page-scoped LOOP (Patch 7 audit M-E2 + §12.5)
grep "_activeSubPage == SUB_LOOP" src/setup/ToolPadRoles.cpp
# attendu : ≥ 2 (un dans poolLineSize, un dans drawScreen)

# J. Skip silencieux nav circulaire pool (Patch 7 §12.5)
grep -c "while (poolLineSize" src/setup/ToolPadRoles.cpp
# attendu : ≥ 2 (UP + DOWN nav)
```

#### §8.1.4 Mini-audit 3.F.1

1. **`VT_RED` partagé `ROLE_COLLISION` (map=6) et `ROLE_REC` (map=9)** : pas de conflit visuel (états mutuellement exclusifs). Audit 3.H.1 confirmera.

2. **`ROLE_PLAY_STOP = 5` partagé ARPEG + LOOP** : intentionnel §11.1/§14.1 geste unifié.

3. **Labels Slots 16 entries** : `S0  S1  ... S15` + padding "Slot:" début = ~75-80 chars. Tient sur 80 chars. À vérifier HW.

4. **Branche `else` legacy fallback retirée** : toutes les pages ont leur dispatch. `_buildRoleMapLegacy` reste comme code mort dans le `default` switch (retiré 3.H.2).

5. **`POOL_LINE_COUNT = 10` étendu** : impact rétroactif sur `poolLineSize()` et `poolItemLabel()` legacy consultés par autres pages. Vérifier que dispatchs internes par page restent cohérents.

### §8.2 Phase 3.F.2 — Matrice édition §7.1 + §7.2 LOOP

#### §8.2.1 Objectif

Brancher `_handleEnterLoop` + `_handleEnterPoolLoop`. Implémenter matrice §7.1 LOOP symétrique à ARPEG :
- Pad vide → assign
- Rôle LOOP propre intra-AC → swap-to-pool silencieux §7.2
- Rôle ARPEG cross-AC voisin → assign par-dessus (coexistence §7.3)
- Absorbant (BANK/CC) → no-op silencieux

Helper `_clearRolesLoopOnly(pad)` page-scoped (§15.3).

#### §8.2.2 Patches

**Patch 1 — `ToolPadRoles_Loop.cpp`** : helpers + handlers
```cpp
void ToolPadRoles::_clearRolesLoopOnly(uint8_t pad) {
  if (_wkLoopPad.recPad      == pad) _wkLoopPad.recPad      = 0xFF;
  if (_wkLoopPad.playStopPad == pad) _wkLoopPad.playStopPad = 0xFF;
  if (_wkLoopPad.clearPad    == pad) _wkLoopPad.clearPad    = 0xFF;
  for (uint8_t i = 0; i < 16; i++) {
    if (_wkLoopPad.slotPads[i] == pad) _wkLoopPad.slotPads[i] = 0xFF;
  }
}

void ToolPadRoles::_handleEnterLoop() {
  uint8_t pad = _gridRow * 12 + _gridCol;
  PadNeighborInfo info = _padNeighborInfo(pad);

  // §8.1 refus absorbants no-op
  if (info.bankIdx >= 0 || info.hasCc) return;

  // §7.1 LOOP : ENTER ouvre pool
  _editing = true;

  // Position cursor sur rôle LOOP actuel si présent
  if (_wkLoopPad.recPad == pad) {
    _poolLine = 6; _poolIdx = 0;
  } else if (_wkLoopPad.playStopPad == pad) {
    _poolLine = 7; _poolIdx = 0;
  } else if (_wkLoopPad.clearPad == pad) {
    _poolLine = 8; _poolIdx = 0;
  } else {
    int8_t slotIdx = findLoopSlotIdx(_wkLoopPad, pad);
    if (slotIdx >= 0) {
      _poolLine = 9; _poolIdx = (uint8_t)slotIdx;
    } else {
      _poolLine = 6; _poolIdx = 0;  // REC par défaut
    }
  }
}

void ToolPadRoles::_handleEnterPoolLoop() {
  uint8_t pad = _gridRow * 12 + _gridCol;

  if (_poolLine == 0) {
    // [---] clear role page-scoped (§15.3)
    _clearRolesLoopOnly(pad);
    if (saveAll()) _ui->flashSaved();
    _editing = false;
    return;
  }

  PadNeighborInfo info = _padNeighborInfo(pad);
  if (info.bankIdx >= 0 || info.hasCc) {
    _editing = false;
    return;
  }

  // §7.2 swap-to-pool intra-AC silencieux : libérer ancien pad propriétaire
  uint8_t roleOwnerPad = 0xFF;
  switch (_poolLine) {
    case 6: roleOwnerPad = _wkLoopPad.recPad;      break;
    case 7: roleOwnerPad = _wkLoopPad.playStopPad; break;
    case 8: roleOwnerPad = _wkLoopPad.clearPad;    break;
    case 9: if (_poolIdx < 16) roleOwnerPad = _wkLoopPad.slotPads[_poolIdx]; break;
  }
  if (roleOwnerPad < NUM_KEYS && roleOwnerPad != pad) {
    _clearRolesLoopOnly(roleOwnerPad);  // silent steal
  }

  // Libère pad cible des rôles LOOP existants (intra-AC swap)
  _clearRolesLoopOnly(pad);

  // Assigne nouveau rôle LOOP
  switch (_poolLine) {
    case 6: _wkLoopPad.recPad      = pad; break;
    case 7: _wkLoopPad.playStopPad = pad; break;
    case 8: _wkLoopPad.clearPad    = pad; break;
    case 9: if (_poolIdx < 16) _wkLoopPad.slotPads[_poolIdx] = pad; break;
  }

  // §7.3 coexistence cross-AC ARPEG : pas d'action sur _wkRootPads/etc.
  if (saveAll()) _ui->flashSaved();
  _editing = false;
}
```

**Patch 2 — `ToolPadRoles.cpp` orchestrateur** : extension `run()` dispatch SUB_LOOP
```cpp
} else if (_activeSubPage == SUB_LOOP) {
  if (ev.type == NAV_ENTER) {
    if (!_editing) _handleEnterLoop();
    else            _handleEnterPoolLoop();
    screenDirty = true;
  }
}
```

#### §8.2.3 Hard-asserts 3.F.2

```bash
# A. _handleEnterLoop + _handleEnterPoolLoop définis
grep -c "ToolPadRoles::_handleEnter.*Loop" src/setup/ToolPadRoles_Loop.cpp
# attendu : ≥ 2

# B. _clearRolesLoopOnly défini et utilisé
grep -c "_clearRolesLoopOnly" src/setup/ToolPadRoles_Loop.cpp
# attendu : ≥ 3 (definition + ≥ 2 usages)

# C. Swap intra-AC §7.2 silent steal
grep -E "_clearRolesLoopOnly\(roleOwnerPad\)|_clearRolesLoopOnly\(pad\)" src/setup/ToolPadRoles_Loop.cpp
# attendu : ≥ 2

# D. Refus absorbants defensif
grep -cE "info\.bankIdx >= 0|info\.hasCc" src/setup/ToolPadRoles_Loop.cpp
# attendu : ≥ 2

# E. run() dispatch SUB_LOOP branché
grep -c "_activeSubPage == SUB_LOOP" src/setup/ToolPadRoles.cpp
# attendu : ≥ 2 (drawScreen + run)
```

#### §8.2.4 Mini-audit 3.F.2

1. **`POOL_LINE_COUNT = 10` impact rétroactif** : `poolLineSize()` legacy renvoie 0 pour lignes > 5 (avant 3.F). À étendre pour lignes 6-9 (REC=1, PS=1, CLR=1, Slots=16). Mais `poolLineSize` est probablement page-scoped via dispatch. À vérifier en pré-exécution 3.F.2.

2. **§7.2 swap silencieux symétrique ARPEG** : pattern identique, bonne réutilisation cognitive.

3. **Coexistence cross-AC ARPEG-LOOP §7.3** : code prêt (n'agit pas sur `_wkRootPads/etc.`). Test G5 valide end-to-end.

4. **`_handleEnterLoop` ouvre pool ligne REC par défaut** : choix pragmatique (premier rôle LOOP). Alternative ligne Slots non retenue.

### §8.3 Phase 3.F.3 — Info panel + `_applyDefaultsLoop` + coexistence testable

#### §8.3.1 Objectif

Compléter `_drawInfoLoop` langue musicien (§8.5 + §15.2). Implémenter `_applyDefaultsLoop()` page-scoped (§15) avec valeurs hardcoded legacy (REC=32, PS=33, CLR=34, Slots vides) et skip silencieux. Détection PL/S unifié §14.1 si même pad ARPEG+LOOP.

#### §8.3.2 Patches

**Patch 1 — `ToolPadRoles_Loop.cpp` `_drawInfoLoop` complet** : langue musicien §15.2

Body concret (closes l'audit 1 finding I4.4 "renvoi à chat iter 2 passe 3") :
```cpp
void ToolPadRoles::_drawInfoLoop() const {
  uint8_t pad = _gridRow * 12 + _gridCol;
  PadNeighborInfo info = _padNeighborInfo(pad);
  char line1[80], line2[80] = {0};

  // Ligne 1 : rôle propre LOOP (REC / PS / CLR / Slot)
  if (info.isLoopRec) {
    snprintf(line1, sizeof(line1), "Pad %u : REC LOOP — record toggle bank courant", pad + 1);
  } else if (info.isLoopPlayStop) {
    // PL/S unifié §14.1 — détection partage avec ARPEG
    if (_wkLoopPad.playStopPad == _wkArpPlayStopPad) {
      snprintf(line1, sizeof(line1),
               "Pad %u : Play/Stop LOOP + ARPEG (geste unifie §14.1)", pad + 1);
    } else {
      snprintf(line1, sizeof(line1), "Pad %u : Play/Stop LOOP", pad + 1);
    }
  } else if (info.isLoopClear) {
    snprintf(line1, sizeof(line1), "Pad %u : CLEAR LOOP — long-press vide bank", pad + 1);
  } else if (info.loopSlotIdx >= 0) {
    // F11 audit 3 Q2-F1 : prévenir user que slots = Phase 6
    snprintf(line1, sizeof(line1),
             "Pad %u : Slot %d LOOP (UI configuree, runtime Phase 6 Slot Drive)",
             pad + 1, info.loopSlotIdx);
  } else {
    snprintf(line1, sizeof(line1), "Pad %u : libre pour LOOP", pad + 1);
  }

  // Ligne 2 : voisins cross-page (absorbants ou contextuels ARPEG)
  char neighborBuf[60] = {0};
  if (info.bankIdx >= 0) {
    snprintf(line2, sizeof(line2), "  + Bank %d (page BANK, absorbant — interdit ici)",
             info.bankIdx + 1);
  } else if (info.hasCc) {
    int8_t ccSlot = findControlPadEntryIdx(_wkCc, pad);
    if (ccSlot >= 0) {
      snprintf(line2, sizeof(line2), "  + CC%u (page CC, absorbant — interdit ici)",
               _wkCc.entries[ccSlot].ccNumber);
    }
  } else {
    // Voisins contextuels ARPEG → coexistence §7.3 (informatif)
    _formatArpegNeighbor(info, neighborBuf, sizeof(neighborBuf));
    if (neighborBuf[0]) {
      snprintf(line2, sizeof(line2), "  + %s (page ARPEG, coexistence §7.3)", neighborBuf);
    }
  }

  _ui->drawFrameLine(line1);
  if (line2[0]) _ui->drawFrameLine(line2);
}
```

Helpers symétriques 3.E.3 : `_formatLoopRole`, `_formatArpegNeighbor` (formate Root/Mode/Chrom/Octave/PL/S ARPEG en string court pour voisin info panel).

**Note F11 audit 3 Q2-F1** : les Slots LOOP sont configurables UI mais n'ont aucun consumer runtime (Phase 6 Slot Drive). L'info panel prévient explicitement l'utilisateur — pas de surprise UX silencieuse.

**Patch 2 — `ToolPadRoles_Loop.cpp` `_applyDefaultsLoop`** : restore defaults §15
```cpp
void ToolPadRoles::_applyDefaultsLoop() {
  // §15.5 skip silencieux des pads occupés par absorbants ou contextuels ARPEG
  // §15.4 valeurs hardcoded legacy : REC=32, PS=33, CLR=34, Slots=vide
  // Note : skip contextuels ARPEG = choix UX conservateur (§7.3 autorise coexistence,
  //        mais default factory ne crée pas auto de coexistence, user décide manuellement)

  // Clear current LOOP roles (page-scoped)
  for (uint8_t i = 0; i < NUM_KEYS; i++) {
    _clearRolesLoopOnly(i);
  }

  auto tryAssignLoopSlot = [&](uint8_t pad, uint8_t* slot) {
    if (pad >= NUM_KEYS) return;
    PadNeighborInfo info = _padNeighborInfo(pad);
    if (info.bankIdx >= 0 || info.hasCc) return;  // skip absorbants
    // Skip contextuels ARPEG (choix conservateur cf §15.5)
    if (info.scaleRole.kind != ScaleRoleKind::NONE
        || info.arpRole.kind != ArpRoleKind::NONE) {
      return;
    }
    *slot = pad;
  };

  tryAssignLoopSlot(32, &_wkLoopPad.recPad);
  tryAssignLoopSlot(33, &_wkLoopPad.playStopPad);
  tryAssignLoopSlot(34, &_wkLoopPad.clearPad);
  // Slots 0..15 restent 0xFF (cleared par _clearRolesLoopOnly ci-dessus)
}
```

**Patch 3 — `ToolPadRoles.cpp` orchestrateur** : `_applyDefaultsLoop` body fourni (le switch dispatch préparé en 3.E.3 trouve désormais une vraie impl).

#### §8.3.3 Hard-asserts 3.F.3

```bash
# A. _drawInfoLoop utilise helpers langue musicien
grep -cE "_formatLoopRole|_formatArpegNeighbor" src/setup/ToolPadRoles_Loop.cpp
# attendu : ≥ 2

# B. _applyDefaultsLoop avec tryAssign + skip silencieux
grep -B2 -A8 "_applyDefaultsLoop" src/setup/ToolPadRoles_Loop.cpp | grep -E "tryAssign|info\.bankIdx|scaleRole|arpRole"
# attendu : ≥ 3

# C. Dispatch _confirmDefaults inclut _applyDefaultsLoop
grep "_applyDefaultsLoop" src/setup/ToolPadRoles.cpp
# attendu : ≥ 1 match

# D. PL/S unifié §14.1 detection
grep "_wkLoopPad.playStopPad == _wkArpPlayStopPad" src/setup/ToolPadRoles_Loop.cpp
# attendu : ≥ 1 (info panel cas unifié)
```

#### §8.3.4 Mini-audit 3.F.3

1. **`_applyDefaultsLoop` skip silencieux contextuels ARPEG** : choix UX conservateur (vs coexistence §7.3 autorisée). User peut créer coexistence manuellement post-`d`.

2. **Info panel PL/S unifié §14.1** : nouveau wording cross-page. À acter rétroactif §7.3 ARPEG si pas déjà mentionné (symétrie info panel).

3. **Coexistence cross-AC testable end-to-end en G5** : tout est code-side prêt. Tests scenarios §14.1 (Root D + Slot 5 sur même pad, PL/S unifié) valides ici.

### §8.4 HW Gate G5 (combiné 3.F.1 + 3.F.2 + 3.F.3)

**Setup préalable** :
- 8 banks assignées (3.D test G3)
- pad 10 = CC0 (3.C test G2)
- ARPEG configuré : Root A pad 8, ..., PL/S pad 23, Octave pads 25-28 (3.E test G4)
- Dev seed M7 actif (retrait en 3.H.2)

**Procédure** :
1. Boot. Entrer Tool 3 → TAB jusqu'à page LOOP.
2. Vérifier rendu §8.1 :
   - Pads 0-7 (banks) : `Bk1`..`Bk8` dim ambre+
   - Pad 10 (CC0) : `CC00` dim ambre+
   - Pads 8-14 (Root ARPEG), 15-21 (Mode), 22 (Chr), 23 (PS ARPEG), 25-28 (Octave) : `--` (invisible §8.1)
3. Vérifier pool 4 lignes LOOP :
   - Ligne REC : `REC` (vert menthe)
   - Ligne PL/S : `P/S` (vert menthe)
   - Ligne CLR : `CLR` (vert menthe)
   - Ligne Slots : `S0 S1 ... S15` (vert menthe)
4. Test `d` defaults LOOP :
   - `d` → "Restaurer les defauts de cette page ? (y/n)"
   - `y` → REC pad 32 (rouge), PS pad 33 (vert), CLR pad 34 (bleu foncé), Slots tous vides
5. **Test skip silencieux conflits cross-page** :
   - Pré-test : page CC, assigner CC1 sur pad 32. Retour page LOOP, `d` LOOP.
   - REC default skipped (pad 32 reste CC1), PS/CLR sur 33/34 OK
   - Pool REC : vert menthe libre
6. **Test coexistence cross-AC §14.1 scenario A** :
   - Curseur pad 8 (Root A ARPEG). Cell `--` page LOOP.
   - Info panel : "Pad 9 — free for LOOP (Root A ARPEG en page ARPEG — coexistence cross-AC possible)"
   - ENTER → pool ligne REC. Naviguer Slots, cursor `S0`, ENTER → Slot 0 LOOP pad 8 (cell jaune `S0`)
   - TAB page ARPEG → cell pad 8 reste `A` couleur pêche (Root A inchangé)
   - Info panel page ARPEG : "Pad 9 — Root A (+ Slot 0 LOOP)"
7. **Test PL/S unifié §14.1** (test critique unique à cette phase — pre-steps détaillés) :
   - **Pre-step 7a** : ARPEG PL/S pad 23 (default G4). TAB page LOOP.
   - **Pre-step 7b** : naviguer pad 33 (PS LOOP default), ENTER → pool ouvert, naviguer ligne `[---] clear role`, ENTER → PS LOOP dégagé pad 33, pool PS vert menthe libre.
   - **Pre-step 7c** : naviguer pad 23 (porte déjà PS ARPEG visible page LOOP via _drawInfoLoop — coexistence §7.3 partagée canal PL/S), ENTER → pool ouvert, naviguer ligne PS, cursor `P/S`, ENTER → PS LOOP assigné pad 23.
   - **Vérification** : cell pad 23 `P/S` vert sur page LOOP ET page ARPEG (couleur unifiée §11.1)
   - Info panel LOOP : "Pad 24 — Play/Stop LOOP + ARPEG (geste unifié §14.1)" — wording exact du body §8.3.2 Patch 1
   - Info panel ARPEG (TAB ARPEG) : confirme "Pad 24 — Play/Stop ARPEG (+ Play/Stop LOOP en page LOOP — geste unifié §14.1)"
8. Test swap intra-AC §7.2 (1 swap suffit, symétrique G4 step 6) : ENTER pad 32 (REC), pool ligne Slots `S0`, ENTER → Slot 0 sur pad 32, REC retiré, pool REC vert menthe.
9. **(Step 9 audit 2 retiré : test refus absorbants redondant avec G4 step 7 — page LOOP et ARPEG partagent le même code de refus absorbants côté orchestrateur.)**
10. Test `[---] clear role` page-scoped :
    - ENTER pad 8 (Slot 0 LOOP), `[---]` clear, ENTER → Slot 0 retiré, Root A ARPEG **conservé** (§15.3 page-scoped)
11. q exit → hard-constraint OK → exit
12. **Reboot, persistance OK — prérequis F1 audit B-E2 : `saveAll()` extension LoopPadStore est livrée en 3.F.1 (déplacée de 3.F.3, cf §8.1.2 Patch 6 + §12.12). Sans ce déplacement, ce step échouerait cryptiquement.**

**Critères** :
- ✓ Cell display §8.1 page LOOP
- ✓ Pool 4 lignes couleurs §11.1
- ✓ `d` defaults LOOP + skip silencieux
- ✓ Swap intra-AC §7.2 silencieux
- ✓ Refus absorbants no-op
- ✓ **Coexistence cross-AC ARPEG-LOOP end-to-end** (Slot LOOP + Root ARPEG sur même pad)
- ✓ **PL/S unifié §14.1** (cell verte sur 2 pages, info panel "geste unifié")
- ✓ Info panel langue musicien
- ✓ `[---] clear role` page-scoped (préserve cross-page)
- ✓ Save persiste reboot

### §8.5 Décisions actées 3.F

1. Labels Slots `S0`..`S15` (compact §15.2 grid = pool).
2. Macro `VT_DARK_BLUE` placeholder §11.1 (raffiné 3.H.1).
3. ROLE codes nouveaux 9/10/11 (REC, CLR, SLOT) + `ROLE_PLAY_STOP=5` partagé ARPEG+LOOP §11.1.
4. `POOL_LINE_COUNT = 10` étendu (4 pages × leurs lignes propres).
5. Statics labels LOOP migrés vers `ToolPadRoles_Loop.cpp`.
6. `_handleEnterLoop` ouvre pool ligne REC par défaut si pad libre.
7. §7.2 swap intra-AC silencieux LOOP (symétrique ARPEG).
8. `_clearRolesLoopOnly(pad)` page-scoped (§15.3).
9. `_applyDefaultsLoop()` skip silencieux contextuels ARPEG + absorbants (§15.5 conservateur).
10. PL/S unifié §14.1 : info panel mention "geste unifié" si même pad ARPEG+LOOP.
11. Branche `else` legacy fallback retirée (toutes pages ont dispatch propre).
12. Helpers formatage : `_formatLoopRole`, `_formatArpegNeighbor` (symétriques ARPEG).

---

## §9 Phase 3.G — Modale d'écrasement

### §9.0 Vue d'ensemble

3.G est la **pièce centrale** qui débloque §6.6 propagation fine et **remplace les placeholders no-op silencieux** des pages absorbantes BANK (§6) et CC (§5). Apparaît selon §7.1 quand un absorbant tente d'occuper un pad portant 1 à 4 rôles CONTEXTUELs (max théorique §4.2). Wording français langue musicien §10.2-10.3.

2 sous-phases avec HW Gate G6 unique :
- **3.G.1** Modale implémentation : struct `PendingOverwrite`, flag `_confirmOverwrite`, helper `_formatOverwriteWording`, helper `_handleOverwriteModaleApply`, dispatch `_confirmOverwrite` dans `run()`, rendu `_drawOverwriteModale` overlay INFO section
- **3.G.2** Wiring page BANK + page CC : remplacement no-op silencieux placeholders par appel modale + retrait flash 3.B "Pad is LOOP REC/PS/CLR" (LOOP control = contextuel M·L, modale uniforme §10) + test propagation §6.6 fine (scenarios §14.2)

Volume cumulé : ~300-400 L.

### §9.1 Phase 3.G.1 — Modale implémentation

#### §9.1.1 Objectif

Implémenter le mécanisme modale d'écrasement §10 : struct + flag + helper formatage wording français + helper effet `y` + dispatch dans `run()` + rendu overlay INFO. Ne touche pas encore aux pages (wiring en 3.G.2).

#### §9.1.2 Patches

**Patch 1 — `ToolPadRoles.h`** : enum + struct + state members + déclarations
```cpp
enum OverwriteAction : uint8_t {
  OVERWRITE_ACTION_NONE = 0,
  OVERWRITE_ACTION_BANK_ASSIGN,   // page BANK : assign bank N sur pad
  OVERWRITE_ACTION_CC_CREATE,     // page CC : créer CC entry sur pad
};

struct PendingOverwrite {
  OverwriteAction action;
  uint8_t         pad;
  uint8_t         bankIdx;  // valide si action == BANK_ASSIGN
};

// --- Modale d'écrasement state (§10) ---
PendingOverwrite _pendingOverwrite;
bool             _confirmOverwrite;

// --- Modale methods (defined in ToolPadRoles.cpp orchestrateur) ---
void _handleOverwriteModaleApply();     // effet 'y' : clear contextuels + assign absorbant
void _formatOverwriteWording(char* out, size_t cap);  // génère wording §10.2
void _drawOverwriteModale();            // overlay INFO section quand _confirmOverwrite
```

**Patch 2 — `ToolPadRoles.cpp` orchestrateur** : `_formatOverwriteWording`

Construit le wording français §10.2-10.3 dynamiquement selon `_padNeighborInfo(pad)` :
```cpp
void ToolPadRoles::_formatOverwriteWording(char* out, size_t cap) {
  PadNeighborInfo info = _padNeighborInfo(_pendingOverwrite.pad);

  // Static buffers locaux pour 4 rôles max (§4.2)
  char roleBufs[4][32];
  uint8_t roleCount = 0;

  // Modificateurs ARPEG (Root/Mode/Chrom) — suffixe " de ARPEG" pour clarté UX cross-page (F8 audit 1 I2.2 + spec §10.2)
  if (info.scaleRole.kind == ScaleRoleKind::ROOT) {
    static const char* rootNames[7] = {"A","B","C","D","E","F","G"};
    snprintf(roleBufs[roleCount], 32, "ROOT %s de ARPEG", rootNames[info.scaleRole.idx]);
    roleCount++;
  } else if (info.scaleRole.kind == ScaleRoleKind::MODE) {
    static const char* modeNames[7] = {"Ion","Dor","Phr","Lyd","Mix","Aeo","Loc"};  // §15.2
    snprintf(roleBufs[roleCount], 32, "MODE %s de ARPEG", modeNames[info.scaleRole.idx]);
    roleCount++;
  } else if (info.scaleRole.kind == ScaleRoleKind::CHROM) {
    snprintf(roleBufs[roleCount], 32, "CHROMATIC de ARPEG");
    roleCount++;
  }

  // Octave + PL/S ARPEG (suffixe " de ARPEG" cohérent §10.2)
  if (info.arpRole.kind == ArpRoleKind::OCTAVE) {
    snprintf(roleBufs[roleCount], 32, "OCTAVE %u de ARPEG", info.arpRole.idx + 1);
    roleCount++;
  }
  if (info.arpRole.kind == ArpRoleKind::PLAY_STOP) {
    snprintf(roleBufs[roleCount], 32, "PLAY/STOP de ARPEG");
    roleCount++;
  }

  // Transports LOOP + Slot
  if (info.isLoopRec)       { snprintf(roleBufs[roleCount], 32, "REC de LOOP");       roleCount++; }
  if (info.isLoopPlayStop)  { snprintf(roleBufs[roleCount], 32, "PLAY/STOP de LOOP"); roleCount++; }
  if (info.isLoopClear)     { snprintf(roleBufs[roleCount], 32, "CLEAR de LOOP");     roleCount++; }
  if (info.loopSlotIdx >= 0) { snprintf(roleBufs[roleCount], 32, "SLOT de LOOP");     roleCount++; }
  //                                                              ^ sans numéro §10.3

  // Label absorbant à assigner
  char absorbantLabel[16];
  if (_pendingOverwrite.action == OVERWRITE_ACTION_BANK_ASSIGN) {
    snprintf(absorbantLabel, sizeof(absorbantLabel), "B%u", _pendingOverwrite.bankIdx + 1);
  } else {  // CC_CREATE
    snprintf(absorbantLabel, sizeof(absorbantLabel), "CC");
  }

  // Construction phrase selon roleCount
  if (roleCount == 1) {
    snprintf(out, cap,
             "En placant %s sur ce pad, \"%s\" devra etre reattribue. Y/N ?",
             absorbantLabel, roleBufs[0]);
  } else if (roleCount == 2) {
    snprintf(out, cap,
             "En placant %s sur ce pad, \"%s\" et \"%s\" devront etre reattribues. Y/N ?",
             absorbantLabel, roleBufs[0], roleBufs[1]);
  } else if (roleCount == 3) {
    snprintf(out, cap,
             "En placant %s sur ce pad, \"%s\", \"%s\" et \"%s\" devront etre reattribues. Y/N ?",
             absorbantLabel, roleBufs[0], roleBufs[1], roleBufs[2]);
  } else if (roleCount == 4) {
    snprintf(out, cap,
             "En placant %s sur ce pad, \"%s\", \"%s\", \"%s\" et \"%s\" devront etre reattribues. Y/N ?",
             absorbantLabel, roleBufs[0], roleBufs[1], roleBufs[2], roleBufs[3]);
  } else {
    // Edge case roleCount == 0 (ne devrait pas arriver, modale appelée seulement si contextuels)
    snprintf(out, cap, "Assigner %s ici ? Y/N ?", absorbantLabel);
  }
}
```

**Patch 3 — `ToolPadRoles.cpp` orchestrateur** : `_handleOverwriteModaleApply` (effet `y`)
```cpp
void ToolPadRoles::_handleOverwriteModaleApply() {
  PadNeighborInfo info = _padNeighborInfo(_pendingOverwrite.pad);

  // Étape 1 : retirer rôles contextuels → retournent à leurs pools respectifs
  if (info.scaleRole.kind != ScaleRoleKind::NONE
      || info.arpRole.kind != ArpRoleKind::NONE) {
    _clearRolesArpegOnly(_pendingOverwrite.pad);
  }
  if (info.loopSlotIdx >= 0 || info.isLoopRec
      || info.isLoopPlayStop || info.isLoopClear) {
    _clearRolesLoopOnly(_pendingOverwrite.pad);
  }

  // Étape 2 : assigner l'absorbant selon contexte
  switch (_pendingOverwrite.action) {
    case OVERWRITE_ACTION_BANK_ASSIGN:
      // Silent steal éventuel (modale acceptée explicitement, OK)
      for (uint8_t i = 0; i < NUM_BANKS; i++) {
        if (_wkBankPads[i] == _pendingOverwrite.pad) _wkBankPads[i] = 0xFF;
      }
      if (_wkBankPads[_pendingOverwrite.bankIdx] < NUM_KEYS
          && _wkBankPads[_pendingOverwrite.bankIdx] != _pendingOverwrite.pad) {
        _wkBankPads[_pendingOverwrite.bankIdx] = 0xFF;
      }
      _wkBankPads[_pendingOverwrite.bankIdx] = _pendingOverwrite.pad;
      break;
    case OVERWRITE_ACTION_CC_CREATE:
      _addSlotCc(_pendingOverwrite.pad);  // crée CC entry avec defaults
      break;
    case OVERWRITE_ACTION_NONE:
      break;
  }

  // Étape 3 : save NVS
  if (saveAll()) _ui->flashSaved();
}
```

**Patch 4 — `ToolPadRoles.cpp` orchestrateur** : dispatch `_confirmOverwrite` dans `run()`
```cpp
// Dispatch modale (avant les autres dispatchs page)
if (_confirmOverwrite) {
  ConfirmResult r = SetupUI::parseConfirm(ev);
  if (r == CONFIRM_YES) {
    _handleOverwriteModaleApply();
    _confirmOverwrite = false;
    _pendingOverwrite.action = OVERWRITE_ACTION_NONE;
    _editing = false;
    screenDirty = true;
  } else if (r == CONFIRM_NO) {
    _confirmOverwrite = false;
    _pendingOverwrite.action = OVERWRITE_ACTION_NONE;
    _editing = false;
    screenDirty = true;
  }
  // CONFIRM_PENDING : reste dans modale
  if (screenDirty) {
    screenDirty = false;
    buildRoleMap();
    drawScreen();
  }
  delay(5);
  continue;  // skip autres dispatchs tant que modale active
}
```

**Patch 5 — `ToolPadRoles.cpp` orchestrateur** : `_drawOverwriteModale` + intégration `drawScreen()`
```cpp
void ToolPadRoles::_drawOverwriteModale() {
  char wording[256];
  _formatOverwriteWording(wording, sizeof(wording));
  // Override INFO section (pattern setup-tools-conventions §8.2 : modale inline)
  _ui->drawFrameLine(VT_YELLOW "%s" VT_RESET, wording);
}

void ToolPadRoles::drawScreen() {
  _ui->vtFrameStart();
  _ui->drawConsoleHeader("TOOL 3: PAD ROLE", _nvsSaved);
  _drawSubPageHeader();
  _ui->drawFrameEmpty();

  // Rendu page courante TOUJOURS (modale inline §8.2 ne clear pas écran)
  if (_activeSubPage == SUB_CC)         _drawPageCc();
  else if (_activeSubPage == SUB_BANK)  _drawPageBank();
  else if (_activeSubPage == SUB_ARPEG) _drawPageArpeg();
  else if (_activeSubPage == SUB_LOOP)  _drawPageLoop();

  // Overlay modale dans INFO section si actif (override la ligne INFO déjà rendue)
  if (_confirmOverwrite) {
    _drawOverwriteModale();
  }

  _ui->vtFrameEnd();
}
```

**Note pattern overlay** : `_drawPage*()` rend déjà l'INFO section avec le wording normal du pad courant. `_drawOverwriteModale()` ajoute une ligne au-dessus (override). Le user voit le contexte (grid + pool + info pad) + la question modale. Conforme §8.2.

#### §9.1.3 Hard-asserts 3.G.1

```bash
# A. Struct + enum + flag définis
grep -E "OverwriteAction|PendingOverwrite|_confirmOverwrite" src/setup/ToolPadRoles.h
# attendu : ≥ 4 matches

# B. _formatOverwriteWording défini
grep -c "_formatOverwriteWording" src/setup/ToolPadRoles.h src/setup/ToolPadRoles.cpp
# attendu : ≥ 2

# C. _handleOverwriteModaleApply utilise clear helpers + assign absorbant
grep -B2 -A12 "_handleOverwriteModaleApply" src/setup/ToolPadRoles.cpp | grep -cE "_clearRolesArpegOnly|_clearRolesLoopOnly|_wkBankPads|_addSlotCc"
# attendu : ≥ 4

# D. Wording français §10.2 présent (singulier + pluriel)
grep -c "devra etre reattribue\|devront etre reattribues" src/setup/ToolPadRoles.cpp
# attendu : ≥ 2

# E. Labels rôles §10.3 langue musicien
grep -cE "ROOT %s|MODE %s|OCTAVE %u|PLAY/STOP de ARPEG|REC de LOOP|CLEAR de LOOP|SLOT de LOOP|CHROMATIC" src/setup/ToolPadRoles.cpp
# attendu : ≥ 6

# F. parseConfirm legacy utilisé
grep -c "SetupUI::parseConfirm" src/setup/ToolPadRoles.cpp
# attendu : ≥ 2 (modale overwrite + defaults legacy)

# G. _drawOverwriteModale dans drawScreen
grep -c "_drawOverwriteModale\|_confirmOverwrite" src/setup/ToolPadRoles.cpp
# attendu : ≥ 4
```

#### §9.1.4 Mini-audit 3.G.1

1. **`_formatOverwriteWording` ~80 L** : helper substantiel mais isolé. Static buffers locaux (no heap), 4 rôles max acceptable §4.2.
2. **Silent steal en modale BANK acté** : différent §9.2 strict non-modale. La modale a été acceptée explicitement, donc steal silencieux d'autres pads bank cohérent.
3. **Pattern "inline INFO" modale** : conforme setup-tools-conventions §8.2 (modale ne clear pas écran). Override INFO section après `_drawPage*` normal.
4. **`continue;` dans dispatch run()** : skip autres handlers tant que modale active. Modale = unique focus.
5. **`_editing = false` à sortie modale** : conservateur (si on était dans pool quand modale ouverte, on revient à grid nav après modale).

### §9.2 Phase 3.G.2 — Wiring page BANK + CC + propagation §6.6

#### §9.2.1 Objectif

Remplacer les placeholders no-op silencieux dans `_handleEnterPoolBank` (3.D.2) et `_handleModePickCc` (3.C.2) par appel modale. Retirer le flash 3.B "Pad is LOOP REC/PS/CLR" (LOOP control = contextuel M·L, modale uniforme §10). Test propagation §6.6 fine en G6.

#### §9.2.2 Patches

**Patch 1 — `ToolPadRoles_Bank.cpp` `_handleEnterPoolBank`** : remplacement no-op par modale

Avant (3.D.2) :
```cpp
PadNeighborInfo info = _padNeighborInfo(pad);
if (info.hasCc || info.scaleRole.kind != ScaleRoleKind::NONE
    || info.arpRole.kind != ArpRoleKind::NONE
    || info.loopSlotIdx >= 0
    || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear) {
  return;  // no-op silencieux placeholder modale
}
```

Après (3.G.2) :
```cpp
PadNeighborInfo info = _padNeighborInfo(pad);

// CC absorbant → refus permanent (§4 + §7.1 ABSORBANT exclusif, pas de modale)
if (info.hasCc) {
  _editing = false;
  return;
}

// Contextuels (ARPEG/LOOP) → modale d'écrasement §10
bool hasContextual = (info.scaleRole.kind != ScaleRoleKind::NONE
                   || info.arpRole.kind != ArpRoleKind::NONE
                   || info.loopSlotIdx >= 0
                   || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear);
if (hasContextual) {
  _pendingOverwrite.action  = OVERWRITE_ACTION_BANK_ASSIGN;
  _pendingOverwrite.pad     = pad;
  _pendingOverwrite.bankIdx = _poolIdx;  // bank choisie dans le pool
  _confirmOverwrite = true;
  _editing = false;  // sortie pool, modale prend le relais
  return;
}

// Sinon : assignement standard (fall-through au code existant 3.D.1)
```

**Note refus CC absorbant** : §3.1 dit CC absorbe totalement (insensible SHIFT, insensible bank-type). §7.1 BANK + CC = refus permanent (deux absorbants ne coexistent jamais §4). Pas de modale. Refus dur silencieux conservé.

**Patch 2 — `ToolPadRoles_Cc.cpp` `_handleModePickCc`** : remplacement placeholder par modale

Avant (3.C.2) :
```cpp
// Refus dur silencieux pour BANK / contextuels (placeholder modale 3.G).
// LOOP control (REC/PS/CLR) fusionné avec contextuels — §12.11 audit M4 a
// retiré le flash 3.B dès 3.C.2 (cohérence no-op silencieux uniforme).
if (info.bankIdx >= 0
    || info.scaleRole.kind != ScaleRoleKind::NONE
    || info.arpRole.kind != ArpRoleKind::NONE
    || info.loopSlotIdx >= 0
    || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear) {
  _ccUiMode = UI_CC_GRID_NAV;
  _screenDirty = true;
  break;
}
```

Après (3.G.2) :
```cpp
// BANK absorbant → refus permanent (§7.1 CC + BANK = interdit, pas de modale)
if (info.bankIdx >= 0) {
  _ccUiMode = UI_CC_GRID_NAV;
  _screenDirty = true;
  break;
}

// Contextuels (ARPEG/LOOP incl. LOOP control) → modale d'écrasement §10
bool hasContextual = (info.scaleRole.kind != ScaleRoleKind::NONE
                   || info.arpRole.kind != ArpRoleKind::NONE
                   || info.loopSlotIdx >= 0
                   || info.isLoopRec || info.isLoopPlayStop || info.isLoopClear);
if (hasContextual) {
  _pendingOverwrite.action = OVERWRITE_ACTION_CC_CREATE;
  _pendingOverwrite.pad    = (uint8_t)(_gridRow * 12 + _gridCol);
  _confirmOverwrite = true;
  _ccUiMode = UI_CC_GRID_NAV;  // sortie ModePick, modale prend le relais
  _screenDirty = true;
  break;
}
```

**Note flash 3.B** : le bloc `if (info.isLoopRec || info.isLoopPlayStop || info.isLoopClear) { _setFlash(...); }` a déjà été **retiré en 3.C.2** (§12.11 audit M4). 3.G.2 ne re-touche pas à cette branche : LOOP control fait partie de la branche `hasContextual` unifiée ci-dessus, donc passe automatiquement par la modale §10 uniforme.

**Patch 3 — `ToolPadRoles_Cc.cpp` `_drawInfoCc`** : retrait info LOOP control "cannot assign CC here"

Le code 3.C livré (`ToolControlPads.cpp:762-771`) affichait info "Pad : LOOP REC/PS/CLR - cannot assign CC here". En 3.G.2, ce pad porte un rôle CONTEXTUEL qui sera passé via modale. Info panel doit refléter cela :
```cpp
// Avant (3.C livré + 3.C.2) :
if (isLoopControlPad(lp, _cursorPad)) {
  _ui->drawFrameLine("Pad #%d : LOOP %s - cannot assign CC here", ...);
  _ui->drawFrameLine("Move LOOP %s in Tool 3 first to free this pad.");
  return;
}

// Après (3.G.2) — supprimé ce bloc spécial, traité comme contextuel §15.5 :
// La détection LOOP control est faite via _formatRoleNameMusician (§15.2) dans le cas générique
// "rôles CONTEXTUEL voisins" déjà géré en 3.C.2.
```

**Patch 4 — Test propagation §6.6 fine** (pas un patch code, scenario HW G6)

Pas de modification code-side : la propagation §6.6 est automatique (working copies partagées entre pages). Le test HW G6 valide visuellement.

#### §9.2.3 Hard-asserts 3.G.2

```bash
# A. Page BANK : _pendingOverwrite assigné dans _handleEnterPoolBank
grep -B2 -A5 "OVERWRITE_ACTION_BANK_ASSIGN" src/setup/ToolPadRoles_Bank.cpp
# attendu : ≥ 1 match avec _confirmOverwrite = true

# B. Page CC : _pendingOverwrite assigné dans _handleModePickCc
grep -B2 -A5 "OVERWRITE_ACTION_CC_CREATE" src/setup/ToolPadRoles_Cc.cpp
# attendu : ≥ 1 match avec _confirmOverwrite = true

# C. Flash 3.B "Pad is LOOP REC/PS/CLR" retiré
grep "Pad is LOOP REC/PS/CLR" src/setup/ToolPadRoles_Cc.cpp
# attendu : 0 matches

# D. Refus CC absorbant en page BANK reste permanent (pas modale)
grep -B2 -A5 "info\.hasCc" src/setup/ToolPadRoles_Bank.cpp | grep -c "_confirmOverwrite"
# attendu : 0 matches (CC + BANK = pas de modale)

# E. Refus BANK absorbant en page CC reste permanent
grep -B2 -A5 "info\.bankIdx >= 0" src/setup/ToolPadRoles_Cc.cpp | grep -c "_confirmOverwrite"
# attendu : 0 matches

# F. Info panel CC : retrait du bloc "LOOP REC/PS/CLR - cannot assign"
grep "cannot assign CC here" src/setup/ToolPadRoles_Cc.cpp
# attendu : 0 matches (info contextuelle générique §15.2 prend le relais)
```

#### §9.2.4 Mini-audit 3.G.2

1. **CC + BANK = refus permanent (pas modale)** : cohérent §4 ABSORBANT exclusif. Modale ne propose pas un arbitrage qui violerait l'invariant.
2. **LOOP control via modale (retrait flash 3.B)** : LOOP transports sont CONTEXTUEL M·L, donc spec §10 application uniforme. Régression UX mineure (flash informatif disparu) compensée par wording modale précis ("PLAY/STOP de LOOP devra etre reattribue").
3. **`_addSlotCc` post-écrasement** : crée nouveau CC entry avec defaults. L'user peut ensuite éditer ses paramètres (channel, mode, etc.) via `e` VALUE_EDIT.
4. **`_editing = false` / `_ccUiMode = UI_CC_GRID_NAV` à modale opening** : sortie sub-state cohérente.
5. **Propagation §6.6 testable end-to-end** : tout code-side prêt. G6 valide visuellement.

### §9.3 HW Gate G6 (combiné 3.G.1 + 3.G.2)

**Setup préalable** :
- Toutes pages configurées (depuis G3/G4/G5)
- ARPEG : Root D pad 22 (re-assigné pour test)
- LOOP : Slot 3 pad 22 (coexistence cross-AC §7.3 sur pad 22)

**Procédure** :
1. Pré-test : page ARPEG → ENTER pad 22, pool Root D, ENTER. Page LOOP → ENTER pad 22, pool Slot 3, ENTER (coexistence cross-AC OK).
2. TAB page BANK. Cell pad 22 : ` ■■ ` neutre dim (§8.1 contextuels cross-page invisible grille).
3. Info panel curseur pad 22 : "Pad 23 : Root D (ARPEG) + Slot 3 (LOOP). Assignment overwrites these roles (Phase 3.G modale)."
4. ENTER pad 22 → pool ouvert ligne Bank. Cursor cible Bk5.
5. **ENTER Bk5 → modale apparaît** (overlay INFO) :
   ```
   En placant B5 sur ce pad, "ROOT D de ARPEG" et "SLOT de LOOP" devront etre reattribues. Y/N ?
   ```
6. Vérifier wording exact §10.2 + §10.3 (quotes, "et", "devront etre reattribues", "Y/N ?")
7. **Test `n` annulation** : `n` → modale disparaît, pad 22 inchangé (Root D + Slot 3 conservés), pool fermé, retour grid nav.
8. Re-ENTER pad 22, pool Bk5, ENTER → modale réapparaît.
9. **Test `y` confirmation** : `y` → modale disparaît, pad 22 affiche `Bk5` blanc, Root D + Slot 3 retirés.
10. **Test propagation §6.6 fine** :
    - TAB page ARPEG : pool Root D **vert menthe** (libre, retournée pool), pad 22 affiche `Bk5` dim ambre+ (cross-page absorbant)
    - TAB page LOOP : pool Slot 3 **vert menthe** (libre), pad 22 affiche `Bk5` dim ambre+
    - TAB page CC : pool inchangé, pad 22 affiche `Bk5` dim ambre+
11. **Test modale page CC + contextuels** :
    - TAB page CC. Pré-test : placer Root C ARPEG pad 30 (TAB ARPEG, ENTER pad 30, pool Root C, ENTER).
    - Retour page CC. ENTER pad 30 → MODE_PICK pool. Sélectionner MOM, ENTER → modale "En placant CC sur ce pad, "ROOT C de ARPEG" devra etre reattribue. Y/N ?"
    - `y` → Root C dégagé, CC entry créée sur pad 30 avec defaults.
12. **Test modale LOOP control (retrait flash 3.B)** :
    - Pad 33 porte PL/S LOOP (default). Page CC, ENTER pad 33, MODE_PICK pool, ENTER MOM → modale "En placant CC sur ce pad, "PLAY/STOP de LOOP" devra etre reattribue. Y/N ?"
    - **PAS de flash** "Pad is LOOP REC/PS/CLR" (retiré 3.G.2)
    - `y` → PL/S LOOP dégagé, CC entry créé.
13. **Test modale 2 rôles** (allègement audit 2 G6 — passer de 3 à 2 rôles, le wording avec 3 rôles est extrapolation directe du template `roleCount == 3`) :
    - Pré-test : placer Root A + Slot 5 sur pad 40 (coexistence §7.3 cross-AC)
    - Page BANK, ENTER pad 40, pool Bk7, ENTER → modale "En placant B7 sur ce pad, "ROOT A de ARPEG" et "SLOT de LOOP" devront etre reattribues. Y/N ?"
    - Vérifier wording 2 rôles correct. (Le cas 3-4 rôles est couvert par template `_formatOverwriteWording` — extrapolation logique, pas besoin de test HW séparé.)
14. **Test refus permanent CC + BANK** (1 cas suffit, le pattern est symétrique) : Page CC, ENTER pad 0 (Bank 1) → no-op silencieux (pas de modale). Critère : aucune modale apparaît pour ABSORBANT × ABSORBANT.
15. q exit + reboot, persistance OK (fusionné)

**Critères** :
- ✓ Modale apparaît page BANK + contextuels
- ✓ Modale apparaît page CC + contextuels
- ✓ Wording français §10.2-10.3 correct (1, 2, 3, 4 rôles)
- ✓ Labels §10.3 langue musicien (ROOT X, MODE Y, OCTAVE N, PLAY/STOP de ARPEG/LOOP, REC/CLEAR/SLOT de LOOP, CHROMATIC)
- ✓ `y` applique : rôles retournent pools, absorbant assigné, save
- ✓ `n` / Escape annule : pad inchangé, retour état pré-modale
- ✓ **Propagation §6.6 fine** : pools cross-page reflètent immédiatement libérations
- ✓ **CC + BANK absorbants = refus permanent** (pas modale)
- ✓ **LOOP control via modale** (flash 3.B retiré)
- ✓ Save persiste reboot

### §9.4 Décisions actées pour 3.G

1. Wording français modale §10.2 ("En placant X sur ce pad, ... devra/devront etre reattribue/reattribues. Y/N ?").
2. Identification rôles §10.3 langue musicien (Mode aligné §15.2 "Ion/Dor/Phr/Lyd/Mix/Aeo/Loc").
3. Struct `PendingOverwrite` + flag `_confirmOverwrite` membre orchestrateur.
4. `SetupUI::parseConfirm` legacy (any-key-cancels).
5. Modale "inline INFO" override (pattern §8.2 setup-tools-conventions).
6. CC + BANK absorbants = refus permanent (pas modale, §4 exclusif).
7. LOOP control (REC/PS/CLR) via modale (CONTEXTUEL M·L cohérent §10).
8. Flash 3.B "Pad is LOOP REC/PS/CLR" retiré.
9. Silent steal en modale BANK acté (l'user a confirmé l'écrasement).
10. `_addSlotCc` réutilisé pour effet `y` page CC (création slot avec defaults).
11. Info panel CC "cannot assign CC here" pour LOOP control retiré (info contextuelle générique §15.2 prend le relais).

---

## §10 Phase 3.H — Palette + finitions

### §10.0 Vue d'ensemble

Phase finale code Phase 3 LOOP. Audits palette visuels HW + ménage code mort transitoire accumulé pendant 3.A-3.G.

2 sous-phases avec HW Gate G7 unique :
- **3.H.1** Audits palette + ajustements ANSI éventuels (teinte ambre+ saturé §11.6, vert menthe vs vert PL/S §11.5, audit ARPEG/LOOP couleurs §11.1)
- **3.H.2** Retrait dev seed M7 + `_buildRoleMapLegacy` + `clearRole(pad)` legacy + statics BANK legacy résiduels + default case switch. **HW Gate G7**.

Volume cumulé : ~50-130 L code (audits + suppressions).

### §10.1 Phase 3.H.1 — Audits palette + ajustements

#### §10.1.1 Objectif

Audits visuels HW de toutes les macros couleur placeholders introduites pendant 3.C-3.F. Swaps ANSI éventuels si lisibilité insuffisante. **Pas de modification structurelle code** — uniquement valeurs ANSI dans `SetupUI.h`.

**Décision Q1 cell width** : (b) conserver legacy 5 chars dans `drawCellGrid`. Doc-sync 3.I aligne spec §8.3 + vt100-design-guide §1.8 sur la valeur réelle 5. Pas de modification code en 3.H.1.

#### §10.1.2 Audits HW à effectuer

| Macro | Placeholder actuel | Audit HW visuel | Action si insuffisant |
|---|---|---|---|
| `VT_BG_AMBER_SAT` | `\033[48;5;130m` | Doit être visiblement distinct du fg ambient `#ffaa33` (Amber Phosphor P3) | Swap vers `\033[48;5;94m` ou `\033[48;5;52m` ou `\033[48;5;88m` |
| `VT_MINT_GREEN` | `\033[38;5;121m` | Doit être visiblement distinct de `VT_GREEN \033[32m` (PL/S grille §11.5) | Swap vers `\033[38;5;42m` ou `\033[38;5;48m` |
| `VT_PEACH` | `\033[38;5;216m` | Cohérence palette §11.1 (Root distinct de Bank/Mode/Octave/PL/S) | Swap vers `\033[38;5;217m` ou `\033[38;5;180m` |
| `VT_PURPLE` | `\033[38;5;141m` | Cohérence palette §11.1 (Octave distinct) | Swap vers `\033[38;5;135m` ou `\033[38;5;165m` |
| `VT_DARK_BLUE` | `\033[38;5;19m` | Doit être visiblement distinct de `VT_BLUE \033[34m` (Bank) | Swap vers `\033[38;5;17m` ou `\033[38;5;18m` |

**Bonus audit § 11.1 Bank "blanc" vs legacy VT_BLUE** : la spec §11.1 dit "Bank: blanc". Code legacy utilise `VT_BLUE`. Audit HW : conserver `VT_BLUE` ou swap `VT_BRIGHT_WHITE` ? Décision pragmatique = conserver legacy si Bank reste reconnaissable, sinon swap.

#### §10.1.3 Patches (selon audit HW)

**Patch éventuel — `SetupUI.h`** : ajustement valeurs ANSI
```cpp
// 3.H.1 — valeurs finales post-audit HW G7
#define VT_PEACH        "\033[38;5;216m"  // ou swap si audit dévie
#define VT_PURPLE       "\033[38;5;141m"
#define VT_MINT_GREEN   "\033[38;5;121m"
#define VT_DARK_BLUE    "\033[38;5;19m"
#define VT_BG_AMBER_SAT "\033[48;5;130m"
```

Volume code : 0 à ~30 L touchées selon audit HW (5 macros au max).

#### §10.1.4 Hard-asserts 3.H.1

```bash
# A. Toutes macros couleur définies (pas changées de nom, swap valeurs OK)
grep -cE "VT_PEACH|VT_PURPLE|VT_MINT_GREEN|VT_DARK_BLUE|VT_BG_AMBER_SAT|VT_NEUTRAL_BAR" src/setup/SetupUI.h
# attendu : ≥ 6
```

Pas de hard-assert verbeux : audit HW pur, validation visuelle.

#### §10.1.5 Mini-audit 3.H.1

1. **Subjectivité audit visuel** : décision finale = Loïc HW G7. Pas d'oracle technique.
2. **Impact swap macros** : préprocesseur uniquement, pas de refacto code consommateur.
3. **Cell width 4 vs 5** : décision Q1 → (b) conserver 5 chars. Doc-sync 3.I aligne refs.

### §10.2 Phase 3.H.2 — Retrait dev seed M7 + `_buildRoleMapLegacy` + ménage

#### §10.2.1 Objectif

Suppression code mort transitoire accumulé pendant 3.A-3.G. Ordre par fichier (`NvsManager` → `main` → `ToolPadRoles`). Décision Q3 = ordre groupé par fichier.

#### §10.2.2 Items à retirer

**`NvsManager.{cpp,h}`** :
- `void NvsManager::applyDevSeedLoopPadsIfSafe()` (NvsManager.cpp:1196-1230) — fonction entière
- Déclaration `void applyDevSeedLoopPadsIfSafe();` dans `NvsManager.h`

**`main.cpp`** :
- Ligne 410 : `s_nvsManager.applyDevSeedLoopPadsIfSafe();` + commentaire associé L406-410

**`ToolPadRoles.{cpp,h}`** :
- Fonction `_buildRoleMapLegacy()` (legacy fallback, plus de consommateur après 3.F.1)
- Fonction `clearRole(uint8_t pad)` legacy cross-catégorie (remplacé par `_clearRolesXOnly` page-scoped)
- Statics `GRID_BANK_LABELS` + `POOL_BANK_LABELS` résiduels dans `ToolPadRoles.cpp` (migrés vers `ToolPadRoles_Bank.cpp` en 3.D.1)
- Default case du switch `buildRoleMap()` : `default: _buildRoleMapLegacy(); break;` (code mort)
- Déclarations correspondantes dans `ToolPadRoles.h`

#### §10.2.3 Patches

**Patch 1 — `NvsManager.{cpp,h}`** : suppression dev seed M7
```cpp
// Supprimer entièrement (NvsManager.cpp:1196-1230) :
void NvsManager::applyDevSeedLoopPadsIfSafe() { /* ... */ }

// Supprimer déclaration NvsManager.h:99 :
void applyDevSeedLoopPadsIfSafe();
```

**Patch 2 — `main.cpp`** : suppression appel dev seed
```cpp
// Supprimer L406-410 (commentaire + appel) :
// // M7 dev seed (Phase 2 testing, retiré Phase 3.G) — DOIT être appelé AVANT setup gate.
// // [...]
// s_nvsManager.applyDevSeedLoopPadsIfSafe();
```

**Patch 3 — `ToolPadRoles.{cpp,h}`** : retrait `_buildRoleMapLegacy` + `clearRole` + statics BANK résiduels + default case
```cpp
// ToolPadRoles.cpp : supprimer void ToolPadRoles::_buildRoleMapLegacy() { ... }
// ToolPadRoles.cpp : supprimer void ToolPadRoles::clearRole(uint8_t pad) { ... }
// ToolPadRoles.cpp : supprimer statics GRID_BANK_LABELS[] et POOL_BANK_LABELS[]
// ToolPadRoles.cpp buildRoleMap() : supprimer default case
// ToolPadRoles.h : supprimer void _buildRoleMapLegacy(); + void clearRole(uint8_t pad);
```

#### §10.2.4 Hard-asserts 3.H.2

```bash
# A. applyDevSeedLoopPadsIfSafe complètement retiré
grep -c "applyDevSeedLoopPadsIfSafe" src/
# attendu : 0

# B. _buildRoleMapLegacy retiré
grep -c "_buildRoleMapLegacy" src/
# attendu : 0

# C. clearRole legacy retiré
grep -nE "void ToolPadRoles::clearRole\(uint8_t pad\)|void clearRole\(uint8_t pad\);" src/setup/ToolPadRoles.{cpp,h}
# attendu : 0 matches

# D. Statics BANK legacy retirés de ToolPadRoles.cpp orchestrateur
grep -c "GRID_BANK_LABELS\|POOL_BANK_LABELS" src/setup/ToolPadRoles.cpp
# attendu : 0 (vivent dans _Bank.cpp via 3.D.1)

# E. default case switch buildRoleMap supprimé
grep -A8 "void ToolPadRoles::buildRoleMap" src/setup/ToolPadRoles.cpp | grep "default:"
# attendu : 0 matches

# F. Build clean (compile + 0 warning)
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 2>&1 | grep -E "warning|error"
# attendu : 0 warnings, 0 errors

# G. Pre-vérification : aucun consommateur résiduel de clearRole legacy avant suppression
# (à exécuter AVANT patch 3, pas après)
grep -rn "clearRole(" src/ | grep -v "_clearRoles" | grep -v "//"
# attendu : 0 matches (sinon, migrer consommateurs vers _clearRolesXOnly correspondant)
```

#### §10.2.5 Mini-audit 3.H.2

1. **`clearRole` legacy peut avoir consommateurs résiduels** : grep G ci-dessus en pré-exécution. Si reste, migrer vers `_clearRolesXOnly(pad)`.
2. **`_applyDefaultsLoop` remplace dev seed M7 fonctionnellement** : équivalent factory pads 32/33/34. Régression UX légère au premier boot (LOOP pads vides vs M7 seed auto) cohérente §15 user-driven.
3. **Default case switch** : suppression cosmétique, switch couvre désormais 100% des cas via 4 `case SUB_*`.
4. **Build clean obligatoire** : 0 warning, 0 error post-retraits. Tout `#include` orphelin à nettoyer si nécessaire.

### §10.3 HW Gate G7

**Procédure** :
1. Pré-test : **reset factory ArpPad + LoopPad via outils existants** (F9 audit 2 — option A retenue, pas de touche `e` au boot dans le firmware actuel) :
   - Sur instrument démarré, entrer Tool 3 → page ARPEG → `d` → `y` (rétablit factory ARPEG).
   - TAB page LOOP → `d` → `y` (rétablit factory LOOP REC=32/PS=33/CLR=34).
   - q exit, reboot pour observer boot propre.
2. Boot. Vérifier :
   - **Pas** de message `[BOOT] LOOP dev seed applied: rec=32 playStop=33 clear=34` (M7 retiré 3.H.2)
   - LOOP REC/PS/CLR pads présents à 32/33/34 (post-`d` step 1)
3. Entrer Tool 3 → page LOOP. Cell pads 32/33/34 affichent `REC` rouge, `P/S` vert, `CLR` bleu foncé (couleurs §11.1).
4. **Audits palette HW (Q2 3.H.1)** :
   - Ambre+ saturé bien distinguable du fg ambient (`#ffaa33`)
   - Vert menthe pool vs vert PL/S grille bien distinguables
   - Root pêche, Mode cyan, Octave pourpre, PL/S vert distinguables §11.1
   - LOOP REC rouge, CLR bleu foncé, Slots jaune distinguables
   - Curseur inverse fg/bg cohérent grid + pool
5. Si audit dévie : swap macros ANSI dans `SetupUI.h`, rebuild, re-test (cycle).
6. **Smoke-check non-régression** (allègement audit 2 — pas de full test scenarios §14, déjà couvert G4/G5/G6) :
   - Tester 2 pages au hasard (ex. BANK + LOOP) : cell display OK, info panel OK
   - 1 modale (re-attribution bank sur pad portant Slot LOOP) : wording correct, `y` propage
   - `d` LOOP → factory OK
7. **Live smoke** (5 min) : 1 scenario réel `§14.1 A` (PL/S unifié déjà testé G5) — vérifier que l'instrument joue normalement en bank ARPEG, switch vers bank LOOP, REC/PS/CLR fonctionnels. Validation finale d'usage.

**Critères** :
- ✓ Dev seed M7 retiré (premier boot post-`d` LOOP step 1 : log absence M7)
- ✓ `d` defaults LOOP remplace fonctionnellement M7 (step 1)
- ✓ `_buildRoleMapLegacy` + `clearRole` legacy + statics BANK résiduels retirés (auto-review §10.2.4)
- ✓ Build clean (0 warning, 0 error)
- ✓ Audits palette validés Loïc HW (5 macros) — step 4
- ✓ Smoke-check 2 pages + 1 modale + live (steps 6-7) sans régression
- ✓ Save persiste reboot

### §10.4 Décisions actées pour 3.H

1. **Q1 cell width** : (b) conserver legacy 5 chars, doc-sync 3.I aligne refs.
2. **Q2 audit palette** : HW visuel par Loïc en G7. Swaps ANSI si nécessaires.
3. **Q3 ordre ménage** : groupé par fichier (`NvsManager` → `main` → `ToolPadRoles`).
4. Dev seed M7 retiré + remplacé fonctionnellement par `d` page LOOP.
5. `_buildRoleMapLegacy` retiré (L1 décision iter 1 §1.5).
6. `clearRole(pad)` legacy retiré (remplacé par `_clearRolesXOnly` page-scoped §15.3).
7. Statics `GRID_BANK_LABELS` + `POOL_BANK_LABELS` résiduels dans `ToolPadRoles.cpp` retirés (vivent dans `_Bank.cpp` depuis 3.D.1).
8. Default case `buildRoleMap()` switch retiré (4 cas SUB_* exhaustifs).
9. Macros couleur placeholders deviennent valeurs finales (ou swaps post-audit HW G7).

---

## §11 Phase 3.I — Doc-sync (clôture Phase 3 LOOP)

### §11.0 Vue d'ensemble

Phase finale, **pas de HW gate** (pure documentation). 10 fichiers touchés + 6 archivages + 2 suppressions handoffs.

3 sous-phases :
- **3.I.1** 4 refs prioritaires (`setup-tools-conventions.md`, `vt100-design-guide.md`, `nvs-reference.md`, `arp-reference.md`)
- **3.I.2** Refs secondaires + spec parent LOOP §5 (runtime-flows, loop-buffer-invariants, architecture-briefing, fonction_regen, spec parent)
- **3.I.3** STATUS.md + LOOP_PROGRESS.md + archives + suppression handoffs

Volume cumulé : ~600-1000 lignes patches doc cumulées.

### §11.1 Phase 3.I.1 — Refs prioritaires (4 fichiers)

#### §11.1.1 Objectif

Refonte des mentions Tool 3 / Tool 4 → Tool PAD ROLE 4 pages + concept ABSORBANT/CONTEXTUEL + règle unique §4 + bump NVS ARPPAD v2→v3. Cartouches MAJ 2026-05-23 résolues (mentions retirées car refonte intégrée).

#### §11.1.2 Patches par fichier

**`docs/reference/setup-tools-conventions.md`** :
- Retirer cartouche MAJ 2026-05-23
- §1 Save Policy : adapter pattern "save à la sortie tool" §6.6 (commentaire transitoire `_saveCc` legacy preserved via §14.1 anti-régression).
- §4 Navigation Paradigms : ajouter §4.5 "Multi-page tool" (TAB cycle, dispatch par `_activeSubPage`).
- §5 Pool Legend : référencer pool 4 pages (Bank 1 ligne / ARPEG 4 lignes / LOOP 4 lignes / CC héritée Tool 4).
- §6 Keybindings : déviation documentée — `r` raccourci retiré (cf §15.1 plan), `d` raccourci page-scoped (cf §15.1).
- §10 Loop Skeleton : adapter pour 4 pages dispatch (`if (_activeSubPage == SUB_X) _drawPageX()`).
- §11 Checklist : nouveaux items "modale d'écrasement §10", "propagation §6.6", "`d` page-scoped".

**`docs/reference/vt100-design-guide.md`** :
- Retirer cartouche MAJ 2026-05-23
- §1.8 Cell Grid : confirmer 5 chars (décision Q1 3.H.1 (b)) — pas de modification, alignement explicite.
- §2.3 Pad Role Categories : refonte 6 → 11 codes :
  - 1 = BANK (legacy VT_BLUE, audit §11.1 "blanc")
  - 2 = ROOT (NEW VT_PEACH §11.1)
  - 3 = MODE (VT_CYAN, incl. Chromatic)
  - 4 = OCTAVE (NEW VT_PURPLE §11.1)
  - 5 = PLAY_STOP (NEW VT_GREEN §11.1, partagé ARPEG+LOOP)
  - 6 = COLLISION (VT_RED)
  - 7 = absorbant cross-page (NEW VT_DIM VT_BG_AMBER_SAT)
  - 8 = neutre ■■ cross-page (NEW VT_DIM)
  - 9 = REC LOOP (NEW VT_RED §11.1)
  - 10 = CLR LOOP (NEW VT_DARK_BLUE §11.1)
  - 11 = SLOT LOOP (NEW VT_YELLOW §11.1)
- §2.4 Grid Color Rules : extension table avec map=7/8/9/10/11.
- §2.5 Files : ajout `ToolPadRoles_{Bank,Cc,Arpeg,Loop}.cpp`, retrait `ToolControlPads.{cpp,h}`.

**`docs/reference/nvs-reference.md`** :
- Retirer cartouche MAJ 2026-05-23
- §"V2 Stores" `ArpPadStore` : rename `holdPad → arpPlayStopPad`, version 2 → 3.
- §"Tool to descriptor mapping" : T3 absorbe ControlPad descriptor (T3 = [2..5] désormais), T4 = range vide (FIRST > LAST). Ajouter note : "T3 mapping = [2..5] + descriptor 12 (LoopPadStore, check ad-hoc dans `printMainMenu`)" — cf §12.6 audit B-N3 (descriptor 12 hors range T3 mais santé NVS reflétée dans le badge T3).
- §"Phase 0.1 Notes" : ajout Phase 3 PAD ROLE note (ARPPAD v2→v3 zero-migration, dev seed M7 retiré 3.H.2, T4 menu retiré 3.C.1b).

**`docs/reference/arp-reference.md`** :
- Retirer cartouche MAJ 2026-05-23
- §3 Play/Stop : remplacer toutes occurrences `holdPad` par `arpPlayStopPad`. Signature `setCaptured(..., uint8_t arpPlayStopPadIdx)`.
- §11 Adding new arp pattern : pas d'impact direct.
- Refs section : adapter pointers vers `ToolPadRoles_Arpeg.cpp` au lieu de `ToolPadRoles.cpp` legacy.

#### §11.1.3 Hard-asserts 3.I.1

```bash
# A. Cartouches MAJ 2026-05-23 résolues (retirées car refonte intégrée)
grep -l "MAJ 2026-05-23" docs/reference/setup-tools-conventions.md docs/reference/vt100-design-guide.md docs/reference/nvs-reference.md docs/reference/arp-reference.md
# attendu : 0 fichiers

# B. holdPad résiduel
grep -l "\bholdPad\b" docs/reference/setup-tools-conventions.md docs/reference/vt100-design-guide.md docs/reference/nvs-reference.md docs/reference/arp-reference.md
# attendu : 0 fichiers

# C. Mention Tool PAD ROLE présente
grep -l "Tool PAD ROLE\|TOOL PAD ROLE" docs/reference/setup-tools-conventions.md docs/reference/vt100-design-guide.md docs/reference/nvs-reference.md docs/reference/arp-reference.md
# attendu : 4 fichiers

# D. Mention 4 pages BANK/ARPEG/LOOP/CC dans setup-tools-conventions + vt100-design-guide
grep -lE "BANK.*ARPEG.*LOOP.*CC|4 pages" docs/reference/setup-tools-conventions.md docs/reference/vt100-design-guide.md
# attendu : 2 fichiers

# E. ARPPAD_VERSION 3 dans nvs-reference
grep "ARPPAD.*v[23]\|ARPPAD.*Version.*3" docs/reference/nvs-reference.md
# attendu : ≥ 1 match (mention v3)

# F. Note descriptor 12 check ad-hoc dans nvs-reference (§12.6)
grep "descriptor 12.*check ad-hoc\|check ad-hoc.*printMainMenu" docs/reference/nvs-reference.md
# attendu : ≥ 1 match
```

#### §11.1.4 Mini-audit 3.I.1

1. **Volume substantiel** : 4 fichiers, refonte multiple sections par fichier. Risque oubli.
2. **Cross-référence cohérence** : grep cross-refs `holdPad` doit retourner 0 fichiers après 3.I.1.
3. **Cartouches retirées vs alignement** : la cartouche dit "sera refondue à la livraison" — son retrait signifie "refonte effectuée". Vérification visuelle requise.

### §11.2 Phase 3.I.2 — Refs secondaires + spec parent LOOP §5

#### §11.2.1 Patches par fichier

**`docs/reference/runtime-flows.md`** :
- Mentions `holdPad` / `_holdPad` → `arpPlayStopPad` / `_arpPlayStopPad`.
- Diagram flow handlePlayStop (was handleHoldPad) si présent.

**`docs/reference/loop-buffer-invariants.md`** :
- Référence `HOLD_PAD` → `ARP_PLAY_STOP_PAD`.

**`docs/reference/architecture-briefing.md`** :
- Tableau ownership pad stores : Tool 3 + Tool 4 → Tool PAD ROLE (avec 4 pages BANK/ARPEG/LOOP/CC).
- Templates "new mode of play" / "new pad role category" : adapter au modèle ABSORBANT/CONTEXTUEL §3 + règle unique §4.
- §0 Scope Triage : mise à jour routing vers refs PAD ROLE.

**`docs/reference/fonction_regen.md`** :
- Note `holdPad` rename appliqué (référence cross-spec).
- Branches HOLD ON/OFF : note "refonte différée §17.2 spec PAD ROLE — fonction_regen.md sera auditée en session ultérieure dédiée".

**`docs/superpowers/specs/2026-04-19-loop-mode-design.md`** (spec parent LOOP) :
- §5 cartouche refonte confirmée (déjà fait livraison spec PAD ROLE 2026-05-23) — vérifier cross-pointer à jour.
- **§27 Phase 3 tableau d'étapes** : remplacer ancien framing "Refactor Tool 3 vers b1 contextuel + Extension Tool 4" par "**Tool PAD ROLE** (fusion Tool 3 + Tool 4 en 4 pages BANK/ARPEG/LOOP/CC, règle unique ABSORBANT/CONTEXTUEL, refonte 2026-05-23)". Cross-pointer vers `specs/2026-05-23-tool-pad-role-design.md`. (F10 audit 3 Q3-F1)
- **§28 Q7 décision "Tool 4 extension (refus ControlPad sur pad LOOP control)"** : remplacer par "Q7 RÉSOLU 2026-05-23 — absorption Tool 4 dans Tool PAD ROLE page CC ; refus cross-page par règle unique ABSORBANT/CONTEXTUEL §4 spec PAD ROLE."
- §15, §18, §19 : vérifier cohérence avec spec PAD ROLE actuelle (aucune contradiction attendue).

**`docs/superpowers/specs/2026-05-23-tool-pad-role-design.md`** (spec PAD ROLE elle-même) :
- §10.3 Mode wording aligné §15.2 plan : remplacer "MODE Maj", "MODE Min", "MODE Dor" par "MODE Ion", "MODE Dor", "MODE Phr", "MODE Lyd", "MODE Mix", "MODE Aeo", "MODE Loc" — cf §12.9 audit M2 (cohérence cross-doc : la spec doit refléter le wording final choisi en passe 4).

#### §11.2.2 Hard-asserts 3.I.2

```bash
# A. holdPad résiduel cross-refs
grep -rn "\bholdPad\b\|\b_holdPad\b\|HOLD_PAD" docs/reference/
# attendu : 0 matches

# B. Spec parent LOOP §5 cartouche refonte vers PAD ROLE
grep "2026-05-23-tool-pad-role-design" docs/superpowers/specs/2026-04-19-loop-mode-design.md
# attendu : ≥ 1 match (cross-pointer confirmé)

# C. fonction_regen.md note différée
grep "refonte différée\|differée\|§17.2" docs/reference/fonction_regen.md
# attendu : ≥ 1 match

# D. Spec PAD ROLE §10.3 wording aligné (§12.9)
grep "MODE Ion\|MODE Dor\|MODE Phr" docs/superpowers/specs/2026-05-23-tool-pad-role-design.md
# attendu : ≥ 1 match (mention Ion/Dor/Phr)
grep "MODE Maj\|MODE Min" docs/superpowers/specs/2026-05-23-tool-pad-role-design.md
# attendu : 0 matches (anciens wordings retirés)

# E. Spec parent §27 + Q7 nouveau framing Tool PAD ROLE (F10 audit 3 Q3-F1)
grep "Tool PAD ROLE" docs/superpowers/specs/2026-04-19-loop-mode-design.md
# attendu : ≥ 2 matches (§27 + §28 Q7 mis à jour)
grep "Tool 3 b1\|Tool 4 ext" docs/superpowers/specs/2026-04-19-loop-mode-design.md
# attendu : 0 matches (anciens framings retirés ou marqués RÉSOLU)
```

#### §11.2.3 Mini-audit 3.I.2

1. **fonction_regen.md non-refondu** : note "refonte différée" intentionnelle (cf spec §17.2). Pas une omission.
2. **Spec parent LOOP §5** : cross-pointer existant depuis livraison spec PAD ROLE. Vérification only.

### §11.3 Phase 3.I.3 — STATUS + LOOP_PROGRESS + archives + suppressions

#### §11.3.1 Patches

**`STATUS.md`** :
- Focus courant : "Phase 3 LOOP CLOSE — Tool PAD ROLE livré"
- Mise à jour résumé commits :
  - 3.A rename + bump NVS (G0 ✓)
  - 3.B squelette 4 pages (G1 ✓)
  - 3.C.1a + 3.C.1b + 3.C.2 page CC absorption (G2 ✓)
  - 3.D.1 + 3.D.2 page BANK (G3 ✓)
  - 3.E.1 + 3.E.2 + 3.E.3 page ARPEG (G4 ✓)
  - 3.F.1 + 3.F.2 + 3.F.3 page LOOP + coexistence cross-AC (G5 ✓)
  - 3.G.1 + 3.G.2 modale d'écrasement + propagation §6.6 (G6 ✓)
  - 3.H.1 + 3.H.2 palette + ménage (G7 ✓)
  - 3.I.1 + 3.I.2 + 3.I.3 doc-sync
- Caveat ARP behaviour desync inchangé (existant Phase 2)

**`docs/superpowers/LOOP_PROGRESS.md`** :
- Tableau Phase 3 → ✅ **CLOSE** avec commits livrés
- Mention features livrées :
  - Tool PAD ROLE 4 pages BANK/ARPEG/LOOP/CC fusionnés
  - Concept ABSORBANT/CONTEXTUEL + règle unique §4
  - `d` raccourci page-scoped + valeurs hardcoded legacy (à valider post-HW)
  - `[---] clear role` page-scoped (`_clearRolesXOnly`)
  - Modale d'écrasement §10 wording français langue musicien
  - Propagation §6.6 fine cross-page
  - Cell display §8.1 (absorbant ambre+, contextuels ■■ ou invisible selon page)
  - Coexistence cross-AC ARPEG-LOOP §7.3 + PL/S unifié §14.1
  - NVS ARPPAD v2→v3 (holdPad → arpPlayStopPad refacto)
  - Dev seed M7 retiré
- Dépendance Phase 4 (PotRouter + Tool 7 ext + LED wiring) reste open

**Archives `docs/archive/`** (déjà déplacées le 2026-05-23 — vérification seulement, pas de `git mv`) :
- `2026-05-19-loop-phase-3-design.md` (présent dans `docs/archive/`)
- `2026-05-19-loop-phase-3-plan.md` (présent)
- `2026-05-19-loop-phase-3-plan_AUDIT.md` (présent)
- `2026-05-19-loop-phase-3-plan_AUDIT_independent.md` (présent)
- `2026-05-19-loop-phase-3-exec-prompt.md` (présent)
- `2026-05-19-loop-phase-3-session-manifest.md` (présent)

Action 3.I.3 : `ls docs/archive/2026-05-19-loop-phase-3-*.md` doit retourner 6 matches. Si manquant, faire le `git mv` correspondant.

**Suppressions** (décision Q4 = (a) supprimer) :
- `docs/superpowers/HANDOFF-2026-05-20-loop-phase-3-spec-rework.md` (§16.3 spec — consommé ; vérifier absence)
- `docs/superpowers/HANDOFF-2026-05-23-tool-pad-role-plan.md` (consommé par ce plan livré)
- `docs/superpowers/HANDOFF-2026-05-23-tool-pad-role-iter3.md` (consommé par cycle iter 3 → EXEC)

#### §11.3.2 Hard-asserts 3.I.3

```bash
# A. STATUS.md focus Phase 3 CLOSE
grep "Phase 3 LOOP.*CLOSE\|Tool PAD ROLE.*livré\|Tool PAD ROLE.*CLOSE" STATUS.md
# attendu : ≥ 1 match

# B. LOOP_PROGRESS Phase 3 CLOSE
grep "Phase 3.*CLOSE\|Tool PAD ROLE.*CLOSE" docs/superpowers/LOOP_PROGRESS.md
# attendu : ≥ 1 match

# C. Archives bien déplacées
ls docs/superpowers/specs/2026-05-19-loop-phase-3-design.md 2>&1 | grep "No such"
# attendu : match (déplacé)
ls docs/archive/2026-05-19-loop-phase-3-design.md
# attendu : match

# D. Plan caduc + audits déplacés
ls docs/archive/2026-05-19-loop-phase-3-plan.md docs/archive/2026-05-19-loop-phase-3-plan_AUDIT.md docs/archive/2026-05-19-loop-phase-3-plan_AUDIT_independent.md
# attendu : 3 matches

# E. Suppression HANDOFF-2026-05-20
ls docs/superpowers/HANDOFF-2026-05-20-loop-phase-3-spec-rework.md 2>&1 | grep "No such"
# attendu : match

# F. Suppression HANDOFF-2026-05-23 (décision Q4 a)
ls docs/superpowers/HANDOFF-2026-05-23-tool-pad-role-plan.md 2>&1 | grep "No such"
# attendu : match (supprimé)

# G. Suppression HANDOFF-2026-05-23-iter3 (cycle iter 3 consommé)
ls docs/superpowers/HANDOFF-2026-05-23-tool-pad-role-iter3.md 2>&1 | grep "No such"
# attendu : match (supprimé)
```

#### §11.3.3 Mini-audit 3.I.3

1. **Suppressions handoffs irréversibles** : décision Q4 = (a) retient le scope minimal (cohérent §16.3 spec). Si besoin de référence historique, le doc plan lui-même (ce fichier) cite les handoffs dans §0.2.
2. **Archives via `git mv`** : préserve l'historique git. Commit unique pour 3.I.3.
3. **STATUS + LOOP_PROGRESS** : trace livraison finale. Phase 4 reste open (next phase, hors scope ce plan).

---

## §12 Audit findings et révisions rétroactives

Findings détectés en cours d'iter 2 qui ont **modifié des décisions actées dans une passe antérieure**. Important pour traçabilité.

### §12.1 — Palette map cross-page 5/6 → 7/8 (révisé en passe 3 pour 3.C.2 + 3.D.2)

**Origine** : en passe 2, j'avais proposé d'étendre `GRID_CONTROLPAD` avec `map=5` (ambre+ saturé) et `map=6` (neutre ■■) pour le cell display §8.1 colonne CC.

**Détection** : en préparant 3.D.2 (passe 3), j'ai constaté que `GRID_ROLES` utilise déjà `map=5` (`ROLE_PLAY_STOP` historiquement magenta, héritage `ROLE_HOLD`) — collision impossible.

**Révision** : codes décalés à **`map=7` (ambre+ saturé)** et **`map=8` (neutre ■■)** pour **toutes les palettes GRID_*** (cohérence cross-page).

**Impact rétroactif** : la passe 2 (3.C.2) doit être actée avec les codes 7/8 dès l'écriture du patch, pas 5/6. Si une session EXEC future lit ce plan, elle applique 7/8 pour 3.C.2 ET 3.D.2.

### §12.2 — Spec §7.4 stricte (dégage direct) vs UX legacy silent steal (tranché passe 3)

**Origine** : en passe 3 j'avais initialement proposé de conserver l'UX legacy en page BANK (ENTER ouvre pool, dégage via `[---] clear role`, silent steal pour swap).

**Détection** : sur question user "combien coûte de respecter la spec vs risques réel", analyse détaillée révélant que :
- Coût technique du respect ≈ ~5 L net (~15 ajoutées, ~10 retirées du legacy)
- Risque non-respect = divergence spec ↔ code dès la livraison, incohérence avec philosophie ABSORBANT/CONTEXTUEL (BANK absorbant exclut le swap-to-pool intra-AC).
- UX explicit > UX magic (silent steal cachait des déplacements).

**Révision** : **respecter spec §7.1 + §7.4 + §9.2 strict en page BANK** :
- ENTER sur pad portant bank → dégage direct (no pool ouvert)
- ENTER sur pad vide → ouvre pool
- ENTER sur entry pool assignée à un autre pad → no-op (pas de silent steal)
- `[---] clear role` conservé pour cohérence cross-page mais redondant en pratique en page BANK

**Note importante** : §7.2 swap-to-pool intra-AC reste applicable aux pages ARPEG et LOOP (CONTEXTUELs). Pas de symétrie avec BANK absorbant.

### §12.4 — B-N1 Switches inline `drawCellGrid` au lieu de tables `COLORS_*[]`

**Origine** : audit adversarial 2026-05-23 détecte que les §5.3.2 Patch 4-5, §6.2.2 Patch 4, §7.1.2 Patch 3, §8.1.2 Patch 3 prescrivent l'extension de tables `COLORS_ROLES[]` et `COLORS_CONTROLPAD[]` qui **n'existent pas dans le code**. `SetupUI::drawCellGrid()` utilise des switches inline (`SetupUI.cpp:534-543` GRID_ROLES, L550-556 GRID_CONTROLPAD).

**Décision pré-EXEC** : (b) **Étendre les switches inline** avec cases 7/8/9/10/11 plutôt que refactor en table indexée. Moins de scope (~15 L vs 30+), reste local au switch, pas de refacto cross-tools.

**Impact rétroactif sur les patches** :
- §5.3.2 Patch 5 "extension palette `GRID_CONTROLPAD` map=7/8" → **interpréter comme extension du switch case GRID_CONTROLPAD avec cases 7 (`VT_DIM VT_BG_AMBER_SAT`) et 8 (`VT_DIM`)** dans `SetupUI.cpp:550-556`.
- §6.2.2 Patch 4 → extension switch case GRID_ROLES, cases 7/8.
- §7.1.2 Patch 3 → extension switch case GRID_ROLES, modifier cases 2/4/5 vers `VT_PEACH`/`VT_PURPLE`/`VT_GREEN` + ajouter cases 7/8.
- §8.1.2 Patch 3 → extension switch case GRID_ROLES, ajouter cases 9/10/11 (`VT_RED`/`VT_DARK_BLUE`/`VT_YELLOW`).
- Hard-asserts §5.3.3 D, §6.2.3 C, §7.1.3 F, §8.1.3 F : remplacer `grep "COLORS_*\[\]"` par `grep "case GRID_*"` + vérifier présence des nouvelles cases.

### §12.5 — B-N2 Skip silencieux lignes vides nav circulaire pool

**Origine** : audit adversarial 2026-05-23 détecte que `POOL_LINE_COUNT 6→10` (§8.1.2) casse la nav circulaire pool (`ToolPadRoles.cpp:842,848`) — UX cassée en page BANK (1 ligne pool) qui cyclerait sur 9 lignes vides.

**Décision pré-EXEC** : (a) **Skip silencieux des lignes vides** dans la nav circulaire pool. Moins invasif que refacto page-scoped.

**Impact rétroactif sur les patches** : dans `ToolPadRoles.cpp::run()` la nav pool circulaire (lignes legacy L842 + L848) doit être enrichie pour skipper les lignes où `poolLineSize(_poolLine) == 0` :
```cpp
// Avant :
if (_poolLine == 0) _poolLine = POOL_LINE_COUNT - 1;
else _poolLine--;
uint8_t sz = poolLineSize(_poolLine);
if (sz > 0 && _poolIdx >= sz) _poolIdx = sz - 1;

// Après (skip lines vides) :
do {
  if (_poolLine == 0) _poolLine = POOL_LINE_COUNT - 1;
  else _poolLine--;
} while (poolLineSize(_poolLine) == 0 && _poolLine != 0);  // 0 = "[---] clear role" toujours valide
uint8_t sz = poolLineSize(_poolLine);
if (sz > 0 && _poolIdx >= sz) _poolIdx = sz - 1;
```
Idem côté DOWN (L848). ~10 L extra cumulées. À intégrer dans Phase 3.F.1 ou 3.B (selon que POOL_LINE_COUNT=10 est déclaré tôt ou tard).

**Note** : `poolLineSize()` doit être page-scoped en dispatch (par page courante). Le legacy actuel renvoie 0 pour lignes > 5. Pour les nouvelles lignes 6-9 (LOOP), `poolLineSize` doit retourner 1/1/1/16 selon `_activeSubPage == SUB_LOOP`, sinon 0. À ajouter en 3.F.1.

### §12.6 — B-N3 Check ad-hoc descriptor 12 LoopPadStore dans `printMainMenu`

**Origine** : audit adversarial 2026-05-23 détecte que descriptor 12 = LoopPadStore (`KeyboardData.h:1007`) appartient logiquement à T3 page LOOP mais le plan §5.2.2 ne touche qu'à `TOOL_NVS_LAST[2] = 5` (ControlPad). Badge T3 ne reflète pas santé LoopPadStore — régression invariant.

**Décision pré-EXEC** : (b) **Check ad-hoc descriptor 12 dans `printMainMenu`** plutôt que refactor `TOOL_NVS_FIRST/LAST` ou déplacer descriptor.

**Impact rétroactif sur les patches** :
- §5.2.2 Patch 6 (KeyboardData.h TOOL_NVS) : conserver tel quel (T3 = [2..5] absorbe ControlPad, T4 = range vide).
- **Nouveau patch §5.2.2 Patch 6bis** : modifier `SetupUI::printMainMenu` (vérifier path exact dans `SetupUI.cpp` pré-EXEC) pour Tool 3 — après check range [2..5], ajouter check spécial descriptor 12 (`NvsManager::checkBlob(LOOPPAD_NVS_NAMESPACE, ...)`). Badge T3 = AND logique des 2 checks. ~10 L extra.
- §11.1.2 nvs-reference.md : ajouter note "T3 mapping = [2..5] + descriptor 12 (check ad-hoc dans printMainMenu)".

### §12.7 — B-N4 Rename `_toolRoles → _toolPadRoles` en Phase 3.A

**Origine** : audit adversarial 2026-05-23 détecte que le member dans `SetupManager` s'appelle `_toolRoles` (`SetupManager.h:45`), pas `_toolPadRoles` comme le plan suppose systématiquement.

**Décision pré-EXEC** : (b) **Rename code `_toolRoles → _toolPadRoles` comme item Phase 3.A**. Sémantiquement cohérent (le module s'appelle ToolPadRoles, member doit s'aligner). Coût ~5 sites code.

**Impact rétroactif sur Phase 3.A** :
- Ajouter au §3.2 Inventaire `SetupManager.{cpp,h}` 5 sites : `SetupManager.h:45` field rename, `SetupManager.cpp:32` `_toolRoles.begin(...)`, `SetupManager.cpp` `case '3'` dispatch (`_toolRoles.run()`), éventuellement printMainMenu si reference.
- §3.3 Patches ajouter Patch 8 : `SetupManager.{cpp,h}` rename `_toolRoles → _toolPadRoles` (~5 sites).
- §3.4 Hard-asserts ajouter : `grep "_toolRoles" src/setup/SetupManager.{cpp,h}` attendu 0 matches après rename.

### §12.8 — B-N5 Suppression orphan link `ScaleManager::_holdPad`

**Origine** : audit adversarial 2026-05-23 détecte que `ScaleManager::_holdPad` est un orphan link (field initialisé L15 + setter L51 + jamais lu en lecture). Viole invariant projet 7 "Setup/Runtime coherence".

**Décision pré-EXEC** : (a) **Supprimer field + setter + call site** au lieu de renommer. Corrige violation invariant + retire ~6 L code mort.

**Impact rétroactif sur Phase 3.A** :
- §3.2 Inventaire `ScaleManager.{cpp,h}` : **passer de "2+ sites à renommer" à "3 sites à supprimer"** :
  - `ScaleManager.h:33` méthode `setHoldPad(uint8_t pad)` → supprimer déclaration
  - `ScaleManager.h:53` field `uint8_t _holdPad;` → supprimer
  - `ScaleManager.cpp:15` init `_holdPad(23)` → supprimer de la liste init
  - `ScaleManager.cpp:50-51` impl `setHoldPad()` → supprimer
- §3.2 `main.cpp` : `s_scaleManager.setHoldPad(holdPad)` L655 → **supprimer** (au lieu de renommer).
- §3.4 Hard-asserts ajouter : `grep "_holdPad\|setHoldPad" src/managers/ScaleManager.{cpp,h}` attendu 0 matches.
- §3.7 Décisions ajouter : "Orphan link ScaleManager `_holdPad` supprimé (invariant 7)".

### §12.9 — M2 Mode wording aligné §15.2 Ion/Dor (patch spec §10.3 en 3.I)

**Origine** : audit adversarial 2026-05-23 détecte divergence spec §10.3 "MODE Maj/Min/Dor" vs plan §15.2 "Mode Ion/Dor/Phr/...". Décision passe 6 Q2 = aligner sur §15.2.

**Décision pré-EXEC** : (a) **Aligner sur §15.2 Ion/Dor** (cohérence cross-doc). Patcher spec §10.3 en doc-sync 3.I.

**Impact rétroactif** :
- §11.2.1 ou §11.3.1 doc-sync : ajouter patch `specs/2026-05-23-tool-pad-role-design.md` §10.3 — remplacer "MODE Maj, MODE Min, MODE Dor" par "MODE Ion, MODE Dor, MODE Phr, MODE Lyd, MODE Mix, MODE Aeo, MODE Loc" (cohérent §15.2 plan).

### §12.10 — M3 Documenter cas extrême exit deadlock `d` séquentiel

**Origine** : audit adversarial 2026-05-23 détecte que `d` BANK + skip silencieux peut créer un exit deadlock si tous les pads 0-7 sont occupés (scénario extrême après `d` ARPEG + `d` LOOP + `d` CC séquentiels).

**Décision pré-EXEC** : (a) **Documenter dans HW Gate G3 test** + accepter recovery user manuelle via `[---] clear role` pages precedentes.

**Impact rétroactif** :
- §6.3 HW Gate G3 procédure : ajouter step "Test cas extrême : (1) `d` ARPEG puis `d` BANK consécutifs, vérifier exit possible OU recovery via `[---] clear role` sur pages ARPEG. (2) Documenter pour user : 'si toutes les banks restent unassigned après `d`, dégager d'abord les rôles cross-page conflictuels via `[---] clear role`'."
- §15.5 ajouter note "Cas extrême : `d` séquentiel multi-pages peut produire exit deadlock. Recovery utilisateur via clear pages precedentes."

### §12.11 — M4 Retirer flash 3.B LOOP control dès 3.C.2 (pas 3.G.2)

**Origine** : audit adversarial 2026-05-23 détecte incohérence transitoire UX entre 3.C.2 (flash 3.B LOOP control conservé) et 3.G.2 (flash retiré, modale uniforme). Pendant ~4-5 commits, no-op silencieux pour ARPEG/contextuels mais flash informatif pour LOOP control = 2 UX différentes pour même refus catégorial.

**Décision pré-EXEC** : (a) **Retirer le flash dès 3.C.2** pour cohérence no-op silencieux uniforme placeholder.

**Impact rétroactif sur §5.3** :
- §5.3 Phase 3.C.2 patch `_handleModePickCc` : **retirer le bloc** preserving flash 3.B :
  ```cpp
  // À SUPPRIMER en 3.C.2 (cohérent décision audit B-M4) :
  if (isLoopControlPad(_nvs->getLoadedLoopPadStore(), _cursorPad)) {
    _setFlash("Pad is LOOP REC/PS/CLR - move in Tool 3 first");
    _uiMode = UI_CC_GRID_NAV;
    break;
  }
  ```
- Le LOOP control devient un cas de "contextuel" géré uniformément avec ARPEG modificateurs en no-op silencieux placeholder, puis modale en 3.G.2.

### §12.12 — M6 `saveAll()` extension explicite pour LoopPadStore — **déplacé 3.F.3 → 3.F.1** (iter 3.6 audit B-E2)

**Origine** : audit adversarial 2026-05-23 détecte que `saveAll()` legacy (`ToolPadRoles.cpp:366-415`) ne persiste pas LoopPadStore. Plan original mentionnait l'extension en 3.F.3.

**Révision iter 3.6 (audit B-E2 + audits 1 et 3 convergents)** : déplacement à 3.F.1. La fenêtre 3.F.2 (handlers LOOP branchés mais 3.F.3 pas encore livré) produit un `flashSaved` trompeur — les changements LOOP semblent persistés mais disparaissent au reboot. HW Gate G5 step de persistance échouerait cryptiquement entre 3.F.2 et 3.F.3.

**Décision pré-EXEC** : extension `saveAll()` livrée en **3.F.1** (avant le branchement des handlers en 3.F.2). Cohérent avec audit indépendant 3 ajouts (Q1-F1).

**Impact rétroactif sur §8.1 Phase 3.F.1** :
- §8.1.2 nouveau **Patch 6** : `ToolPadRoles.cpp::saveAll()` extension LoopPadStore (snippet cf §8.1.2 Patch 6 — déplacé ici).
- §8.1.3 hard-assert H ajouté : `grep -E "_nvs->saveLoopPad|setLoadedLoopPad.*_wkLoopPad" src/setup/ToolPadRoles.cpp` attendu ≥ 2.

**Note §8.3** : Phase 3.F.3 ne contient PLUS de Patch 4 saveAll (livré en amont). §8.3.2 reste avec Patches 1-3 (info panel, `_applyDefaultsLoop` body, dispatch update).

**Note CC** : `_saveCc` legacy (pattern save-per-commit Tool 4 conservé §14.1) persiste déjà ControlPadStore via `NvsManager::saveBlob` direct. Pas besoin d'extension `saveAll` pour CC (déjà sauvé en cours d'usage page CC).

### §12.13 — Items différables iter 3 (vérification mécanique sub-agents)

Findings M5, M7, M8, M9 + m1-m12 reportés à iter 3 (vérification mécanique snippets vs code par sub-agents). Pas bloquants EXEC. Cf rapport audit adversarial 2026-05-23 pour détails.

---

**Origine** : audit indépendant du plan caduc avait pointé B-N3 — validator doit être appelé hors du `if (loadBlob)` pour gérer le cas pré-init `0xFF`.

**Détection** : vérification en passe 1 que `NvsManager.cpp:932-946` ne réinitialise pas `aps` à 0xFF avant `loadBlob`. Mais en lisant `main.cpp:370`, la variable locale `holdPad` (futur `arpPlayStopPad`) est initialisée à 23 AVANT l'appel à `loadAll`. Si `loadBlob` échoue, `holdPad` conserve sa valeur initiale 23 (default factory). Le bug B-N3 ne s'applique pas dans ce cas précis.

**Conséquence** : aucun fix supplémentaire requis en 3.A. Refacto pure inchangée.

---

## §13 Placeholders / macros à raffiner en 3.H.1

Macros et valeurs ANSI placeholders introduites en cours d'iter 2 à raffiner en 3.H.1 :

| Macro / valeur | Phase d'intro | Valeur placeholder | À raffiner |
|---|---|---|---|
| `VT_BG_AMBER_SAT` | 3.C.2 | `"\033[48;5;130m"` (ANSI 256-color) | Teinte exacte ambre+ saturé §11.6 vs ambient `#ffaa33` |
| `VT_NEUTRAL_BAR` | 3.C.2 | `" ■■ "` (UTF-8 `\xE2\x96\xA0` × 2) | Cohérence cell width (4 chars cibles spec §8.3 vs 5 actuels code) |
| `VT_MINT_GREEN` | 3.D.1 | `"\033[38;5;121m"` (ANSI 256-color) | Distinguabilité visuelle vs `VT_GREEN` (PL/S grille §11.5) |
| Couleurs `GRID_ROLES` legacy | preserved jusqu'à 3.E.1 | `VT_GREEN` (Root), `VT_CYAN` (Mode), `VT_YELLOW` (Octave), `VT_MAGENTA` (Hold/PL/S) | Alignement spec §11.1 : Root pêche, Octave pourpre, PL/S vert (Mode reste cyan, Hold devient PL/S vert + magenta retiré). Migration en 3.E.1. |

---

## §14 Anti-régression cross-phase

Liste à mettre à jour à chaque passe : invariants à préserver d'une phase à l'autre.

### §14.1 — Pattern save-per-commit Tool 4 conservé en page CC (3.C.1b et après)

Tool 4 actuel sauve à chaque commit utilisateur (ENTER pool, value edit exit, etc.) via `_save()` → `NvsManager::saveBlob` direct. La migration en `_saveCc` (3.C.1a) conserve ce pattern.

Spec §6.6 demande "commit NVS final unique à la sortie du tool" — incohérence à acter. Harmonisation reportée à 3.G ou 3.H (non bloquant pour HW gate G2).

### §14.2 — Naming convention `_ccX` pour members page CC (3.C.1a et après)

Tous les members exclusifs à la page CC préfixés `_cc*` (`_ccUiMode`, `_ccFieldIdx`, `_ccPoolIdx`, `_ccGlobalFieldIdx`, `_ccPropEditDirty`, `_ccGlobalEditDirty`, `_ccWkDirty`, `_wkCc`).

Members partagés conservent leur nom legacy (`_setFlash`, `_refBaselines`, `_input`, `_flashMsg`, `_flashExpireMs`).

Convention étendue aux pages BANK/ARPEG/LOOP : préfixe `_bank*` / `_arpeg*` / `_loop*` si jamais members exclusifs nécessaires (à ce stade, pas anticipé pour BANK car réutilise `_editing`/`_poolLine`/`_poolIdx` legacy).

### §14.3 — Two-step exit (snapshot pattern)

Pattern setup-tools-conventions §6.4 : à conserver dans chaque page avec sub-state machine (CC `_ccUiMode`). Snapshot `modeAtStart` au début de la boucle dispatch, évaluation `NAV_QUIT` contre snapshot. Sans ça, `q` depuis sub-edit sort directement du tool entier.

### §14.4 — `_padNeighborInfo` est la source unique de vérité

Tout test cross-store (présence bank / CC / scale role / arp role / loop slot / loop control) doit passer par `_padNeighborInfo(pad)` (créé en 3.C.2). Pas de duplication ad-hoc des helpers `KeyboardData.h:572-633`.

### §14.5 — Hard-constraint exit globale

`run()` `NAV_QUIT` handler vérifie systématiquement que les 8 banks sont assignées (§6.5). Toute page qui pourrait modifier `_wkBankPads[]` (= page BANK uniquement, mais aussi propagation cross-page §6.6) doit garantir que la vérification reste valide.

### §14.6 — Sémantique ENTER divergente pages absorbantes vs contextuelles

**Pages ABSORBANTES (BANK, CC)** :
- ENTER grid sur pad porteur du rôle propre → **dégage direct** (§7.4 strict, BANK uniquement; CC utilise `x`)
- ENTER pool sur entry assignée à un autre pad → **no-op refus** (§9.2 strict, pas de silent steal)
- ENTER pad avec contextuels cross-page → **placeholder modale** (no-op silencieux 3.D.2/3.C.2, modale 3.G.1)

**Pages CONTEXTUELLES (ARPEG, LOOP)** :
- ENTER grid sur pad porteur du rôle propre → **ouvre pool** (legacy behavior preserved)
- ENTER pool sur entry assignée à un autre pad → **swap-to-pool silencieux §7.2** (silent steal intentionnel)
- ENTER pad avec rôle LOOP/ARPEG cross-AC voisin → **coexistence §7.3** (assign par-dessus, pas modale)
- ENTER pad avec absorbants cross-page (BANK/CC) → **no-op silencieux** (focus refused §8.1)

Cette divergence est **intentionnelle** : BANK absorbant exclut le swap-to-pool intra-AC, ARPEG/LOOP contextuels l'autorisent. Cohérent avec classification §3 ABSORBANT/CONTEXTUEL.

**Implémentation** :
- BANK : `_handleEnterBank` (dégage direct si pad porteur), `_handleEnterPoolBank` (refus si entry occupée)
- ARPEG : `_handleEnterArpeg` (ouvre pool), `_handleEnterPoolArpeg` (swap intra-AC silencieux)
- LOOP : `_handleEnterLoop` + `_handleEnterPoolLoop` symétriques à ARPEG
- CC : sub-state machine héritée de Tool 4 (`UI_CC_*`)

### §14.7 — Convention `d` defaults page-scoped + skip silencieux

Suite à décision passe 4 (cf [§15](#§15-conventions-defaults-par-page)) :

- Le bouton `d` (defaults) est **page-scoped** : `_applyDefaultsPage*()` par page.
- Le `d` n'écrase **jamais** les rôles cross-page (skip silencieux des pads occupés par absorbants).
- Aucun bouton "reset général cross-page" (`r` legacy retiré).
- Valeurs hardcoded defaults factory sont **temporaires** : Loïc validera post-HW pour la livraison finale.

Tout code qui touche `_wk*` arrays par défaut doit respecter ce scope page-courante.

---

## §15 Conventions defaults par page

Section dédiée à la convention `d` defaults adoptée en passe 4 (Choix 1 utilisateur 2026-05-23). Remplace les mécanismes legacy `r` (clearAllRoles) + `d` (resetToDefaults cross-catégorie) du Tool 3 actuel.

### §15.1 — Modèle utilisateur

Le user (Loïc) a clarifié sa vision en passe 4 :
> "Il n'y a pas défaut et reset, il y a juste un bouton défaut qui rappelle per page les rôles que je souhaite hardcoder. Pas de grille vide."

Convention dérivée :
- **1 raccourci `d`** par page, applique les defaults factory de la page.
- **Pas de `r`** raccourci. La notion "clear tout puis re-saisir" est supprimée du modèle utilisateur.
- Le `d` est l'**opération unique** de retour à une configuration de référence.

### §15.2 — Labels grid identiques aux labels pool, info en toutes lettres

Règle UI cohérente cross-pages :
- **Grid label = Pool label** : même string court pour la cell du grid et l'entry du pool.
  - Banks : `Bk1`..`Bk8` (was `-B<n>-` legacy)
  - Roots : `A`/`B`/`C`/`D`/`E`/`F`/`G`
  - Modes : `Ion`/`Dor`/`Phr`/`Lyd`/`Mix`/`Aeo`/`Loc`/`Chr`
  - Octaves : `Oct1`/`Oct2`/`Oct3`/`Oct4`
  - PL/S : `P/S`
  - CC : `CC<n>` (legacy preserved, sauf passage à `C<n>` pour CC ≥ 100)
  - Slots LOOP : `S<n>` (à acter en 3.F)
- **Info panel** : en toutes lettres + abréviation entre parenthèses si pertinent.
  - "Bank 1", "Bank 2", ..., "Bank 8"
  - "Root A", "Root B", ..., "Root G"
  - "Mode Ionian (Ion)", "Mode Dorian (Dor)", ..., "Mode Locrian (Loc)"
  - "Chromatic"
  - "Octave 1", ..., "Octave 4"
  - "Play/Stop ARPEG"
  - "REC LOOP", "Play/Stop LOOP", "CLEAR LOOP"
  - "Slot N LOOP" (avec numéro pour distinction, à acter en 3.F)
  - "CC<n> ch<m> mode<...>" (info Tool 4 héritée)

### §15.3 — `[---] clear role` page-scoped via `_clearRolesXOnly()`

Le `[---] clear role` du pool n'agit que sur la page courante :
- Page BANK : `_clearRolesBankOnly(pad)` retire la bank assignée à ce pad si présente. Préserve Scale/ARPEG/LOOP/CC.
- Page ARPEG : `_clearRolesArpegOnly(pad)` retire Root/Mode/Chrom/Octave/PL/S sur ce pad. Préserve Bank/CC/LOOP.
- Page LOOP : `_clearRolesLoopOnly(pad)` retire REC/PS/CLR/Slot sur ce pad. Préserve Bank/CC/ARPEG.
- Page CC : `_removeSlotForPadCc(pad)` legacy Tool 4 (déjà page-scoped CC par construction).

Remplace `clearRole(pad)` legacy qui était cross-catégorie. `clearRole` legacy survit dans `ToolPadRoles.cpp` jusqu'à 3.H.2 (retiré avec autres code mort).

### §15.4 — Valeurs hardcoded defaults factory (temporaires)

Valeurs actuelles legacy, conservées pour livraison initiale Tool PAD ROLE. À remplacer par valeurs validées Loïc post-HW (simple swap des constants, pas de changement structure code).

| Page | Pad → Rôle | Détail |
|---|---|---|
| **BANK** | Bank 1 → pad 0, Bank 2 → pad 1, ..., Bank 8 → pad 7 | `_wkBankPads[i] = i` |
| **ARPEG** | Root A → pad 8, Root B → 9, ..., Root G → 14 | `_wkRootPads[i] = 8 + i` |
| | Mode Ion → 15, Mode Dor → 16, ..., Mode Loc → 21 | `_wkModePads[i] = 15 + i` |
| | Chromatic → 22 | `_wkChromPad = 22` |
| | PL/S → 23 | `_wkArpPlayStopPad = 23` |
| | Octave 1 → 25, Oct 2 → 26, Oct 3 → 27, Oct 4 → 28 | `_wkOctavePads[i] = 25 + i` |
| **LOOP** | REC → pad 32 | `_wkLoopPad.recPad = 32` (was dev seed M7) |
| | PL/S → pad 33 | `_wkLoopPad.playStopPad = 33` |
| | CLR → pad 34 | `_wkLoopPad.clearPad = 34` |
| | Slots 0..15 → tous 0xFF (vide) | `_wkLoopPad.slotPads[i] = 0xFF` |
| **CC** | Tous vides | `_wkCc.count = 0; memset entries 0` |

**Note** : pad 24 reste libre par défaut (pas de Root/Mode/Chrom assigné dessus). C'est le pad entre Chromatic (22), PL/S (23), et Octave 1 (25). Cohérent legacy.

**Note dev seed M7** : Le dev seed M7 actuel (`applyDevSeedLoopPadsIfSafe`, NvsManager.cpp:1224-1226) seede REC=32, PS=33, CLR=34. Identique aux defaults factory ci-dessus. En 3.H.2, le dev seed est **retiré** car le bouton `d` page LOOP livre la même fonctionnalité de façon propre.

**Note Slots LOOP — F11 audit 3 Q2-F1** : les 16 slots restent `0xFF` (vides) par défaut factory. **Le wiring runtime Slot Drive est livré en Phase 6** — assigner des pads slots en Phase 3 PAD ROLE = pré-configuration UI, sans effet musical immédiat. Info panel `_drawInfoLoop` (3.F.3) prévient explicitement l'utilisateur (cf §8.3.2 Patch 1).

### §15.5 — Skip silencieux des conflits cross-page lors du `d`

Quand `d` est appliqué sur une page, les defaults factory tentent de s'assigner sur des pads spécifiques. Si un pad cible est occupé par un absorbant cross-page (BANK ou CC), le default correspondant est **skipped silencieusement** :

```cpp
auto tryAssign = [&](uint8_t pad, uint8_t* slot) {
  if (pad >= NUM_KEYS) return;
  PadNeighborInfo info = _padNeighborInfo(pad);
  if (info.bankIdx >= 0 || info.hasCc) return;  // skip silencieux
  *slot = pad;
};
```

Conséquence visible : si pad 8 (default Root A) porte Bank 1, l'entry Root A reste vert menthe (libre) dans le pool ARPEG après `d`. L'utilisateur doit assigner Root A manuellement ailleurs si désiré.

**Cas spécifique page BANK + hard-constraint exit §6.5** : si `d` BANK rencontre des conflits CC, certaines banks restent unassigned → hard-constraint exit refuse l'exit → user re-saisit manuellement les banks manquantes. Recovery naturelle.

### §15.6 — Convention validators NVS legacy conservée

Les validators `validateScalePadStore`, `validateArpPadStore`, `validateBankPadStore` conservent leur logique factory clamp legacy :
```cpp
if (s.holdPad >= NUM_KEYS) s.holdPad = 23;  // factory default
```

Ils servent de **filet de sécurité corruption NVS**. En usage normal (NVS valide), les valeurs sauvegardées sont dans range, validators no-op.

Au premier boot après NVS reset (zero-migration), `loadBlob` échoue, `aps` non-init, validators non appelés — mais les variables locales `main.cpp:370-371` portent les defaults boot identiques aux validators. Donc defaults factory effectifs au premier boot.

Cette approche est **cohérente avec §17.1 spec** : "defaults présents dans le code actuel conservés tels quels". Le bouton `d` page-scoped permet à l'utilisateur de revenir à ces defaults factory à tout moment.

### §15.7 — Wording confirm prompt `d`

Cohérent avec wording français modale §10 spec :
```
Restaurer les defauts de cette page ? (y/n)
```

Court, sans emoji, dans l'INFO section (pattern setup-tools-conventions §8). `y` applique, `n` ou Escape annule. parseConfirm legacy réutilisé.

### §15.8 — Plan refresh post-validation HW

Après livraison initiale et validation HW Loïc :
1. Loïc joue avec sa première config Tool PAD ROLE
2. Identifie les defaults stabilisés finaux
3. Fournit la liste à intégrer
4. Mise à jour `_applyDefaultsBank/Arpeg/Loop/Cc()` : swap des constants hardcoded (5-10 min code, ~20 lignes touchées)
5. Pas de changement de structure ni de feature. Pas de bump NVS.

Tracé pour livraison future : §15.4 valeurs marquées comme "temporaires legacy".

---

*Fin du contenu rédigé en passe 4 (cristallisée 2026-05-23). Les §8 à §11 (Phases 3.F à 3.I) seront étoffés dans les passes suivantes.*
