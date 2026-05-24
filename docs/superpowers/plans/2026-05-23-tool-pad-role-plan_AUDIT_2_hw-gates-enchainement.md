# Audit 2 — Plan Tool PAD ROLE : enchaînement + allègement HW gates

**Date** : 2026-05-24
**Auditeur** : sub-agent sonnet-4-6, posture critique non-complaisante
**Sources** :
- Plan : `docs/superpowers/plans/2026-05-23-tool-pad-role-plan.md` (HEAD `c7c1eaa`, 3677 L)
- Spec : `docs/superpowers/specs/2026-05-23-tool-pad-role-design.md`
- Code firmware actuel HEAD `c7c1eaa` : `src/setup/ToolPadRoles.{cpp,h}`, `src/core/KeyboardData.h`, `src/main.cpp`
- CLAUDE.md projet : invariants 1-7

**Cross-ref** : audit 1 (NVS/collisions/affichage) + audit 3 (broad view LOOP) — focus complémentaire (enchaînement + HW gates).

---

## Axe A — Enchaînement logique du plan

### Cartographie des dépendances entre phases

| Phase | Dépend de | Livre pour | Risque dépendance |
|---|---|---|---|
| 3.A | — (initiale) | 3.B (nom `_wkArpPlayStopPad`), 3.C.2 (`_padNeighborInfo` utilise `arpRoleAtPad` renommé), 3.E.1 (labels `ROLE_PLAY_STOP`) | Faible — refacto pur compilateur |
| 3.B | 3.A (enum `ROLE_PLAY_STOP`, `SUB_BANK`) | 3.C (dispatch `SUB_CC`), 3.D (`SUB_BANK` dispath), 3.E (`SUB_ARPEG`), 3.F (`SUB_LOOP`) | Moyen — switch `buildRoleMap` 4 cas doit compiler sans les 4 impl |
| 3.C.1a | 3.B (fichiers `.cpp` stub créés) | 3.C.1b (`_drawPageCc`, handlers `*Cc` doivent exister) | Élevé — code mort non câblé : si un handler est absent, 3.C.1b échoue à link |
| 3.C.1b | 3.C.1a (handlers tous définis) | 3.C.2 (dispatch active SUB_CC via `_drawPageCc`) | Élevé — suppression physique ToolControlPads.{cpp,h} ; si référence résiduelle, compile casse |
| 3.C.2 | 3.C.1b (`_padNeighborInfo` consomme `_wkArpPlayStopPad` renommé en 3.A, `_wkLoopPad` chargé en begin) | 3.D.2 (palette map=7/8 partagée), 3.E.1 (même palette), 3.G.1 (`_formatOverwriteWording` réutilise `_formatRoleNameMusician`) | Faible — helpers purement read |
| 3.D.1 | 3.C.2 (`_padNeighborInfo` disponible), 3.B (stub `_buildRoleMapBank` → remplacé ici) | 3.D.2 (`_handleEnterBank` no-op remplacé en 3.G.2), 3.G.2 (`_handleEnterPoolBank` no-op remplacé) | Moyen — `saveAll()` en 3.D.1 ne sauve pas LoopPadStore (cf §12.12 audit B-N6) ; risque data loss si 3.F avant 3.H |
| 3.D.2 | 3.D.1 (palette map=7/8 déjà définie en 3.C.2) | 3.E.1 (hard-constraint exit global dans `run()` NAV_QUIT — déjà actif), 3.G.2 (wiring modale dans `_handleEnterPoolBank`) | Faible |
| 3.E.1 | 3.D.1 (`_padNeighborInfo` disponible), 3.B (stub `_buildRoleMapArpeg` → remplacé), 3.A (statics labels ARPEG uses `ROLE_PLAY_STOP`) | 3.E.2 (handlers branchés), 3.F.3 (coexistence cross-AC dépend du rendu ARPEG correct) | Moyen — legacy fallback `run()` reste pour SUB_ARPEG jusqu'en 3.E.2 : si un keypress ENTER arrive en 3.E.1, comportement legacy (non-spec) |
| 3.E.2 | 3.E.1 (`_drawPageArpeg` dispatch actif) | 3.E.3 (pool cursor logique, `_clearRolesArpegOnly` utilisé par `_applyDefaultsArpeg`) | Faible |
| 3.E.3 | 3.E.2 (`_clearRolesArpegOnly` défini), 3.D.2 (`_confirmDefaults` dispatch 4-cas nécessite stubs `_applyDefaultsBank/Loop/Cc`) | 3.F.3 (`_applyDefaultsLoop` stub créé en 3.E.3), 3.G.1 (unification helpers formatage) | **Moyen-élevé** : stubs `_applyDefaultsBank/Loop/Cc` déclarés mais non définis → si compilateur voit appel stub vide et fonction appelée en runtime, comportement vide silencieux (pas un crash, mais fonctionnalité `d` cassée sur 2 pages jusqu'en 3.F.3 / 3.H rétroactif) |
| 3.F.1 | 3.E.3 (stub `_applyDefaultsLoop` en place), 3.B (stub `_buildRoleMapLoop` → remplacé) | 3.F.2 (handlers branchés), 3.F.3 (coexistence end-to-end) | Moyen — `POOL_LINE_COUNT = 10` rétroactif doit ne pas casser nav pool pages BANK/ARPEG/CC (skip silencieux lignes 6-9 requis §12.5) |
| 3.F.2 | 3.F.1 | 3.F.3 (test coexistence cross-AC exige que les handlers LOOP fonctionnent) | Faible |
| 3.F.3 | 3.F.2, 3.E.3 (`_applyDefaultsArpeg` doit être déjà présent) | 3.G.2 (modale cible les rôles CONTEXTUELs, doit inclure LOOP correctement), 3.H.2 (`_buildRoleMapLegacy` retiré ici) | Faible |
| 3.G.1 | 3.E.2 (`_clearRolesArpegOnly`), 3.F.2 (`_clearRolesLoopOnly`), 3.C.2 (`_formatRoleNameMusician`), 3.D.2 (palette map=7/8 active) | 3.G.2 (wiring dans BANK + CC) | Faible — modale est self-contained |
| 3.G.2 | 3.G.1 (struct `PendingOverwrite`, flag `_confirmOverwrite`, `_handleOverwriteModaleApply`) | 3.H.1 (palette visuelle finalisée sur fond de modale), 3.H.2 (ménage code mort) | Faible |
| 3.H.1 | 3.G.2 (toutes features actives, audit palette HW cohérent) | 3.H.2 (ménage final) | Très faible |
| 3.H.2 | 3.F.3 (`_buildRoleMapLegacy` mort depuis que 3.F.1 remplace dernier stub), 3.G.2 (flash 3.B retiré en 3.C.2, pas en 3.H.2) | 3.I (doc-sync avec code propre) | Faible |
| 3.I.1-3.I.3 | 3.H.2 | — | Non bloquant doc |

---

### Findings enchaînement

#### Bloquants (2)

**B-E1 — 3.E.3 stubs `_applyDefaultsBank/Loop/Cc` vides créent un piège runtime silencieux**

Localisation : §7.3.2 Patch 5, §7.3.4 Mini-audit point 1.

Le plan crée en 3.E.3 des stubs vides pour `_applyDefaultsBank()`, `_applyDefaultsLoop()`, `_applyDefaultsCc()` "pour que le compile passe". En runtime, si l'utilisateur tape `d` + `y` sur page BANK (avant 3.D rétroactif réellement appliqué) ou sur page LOOP (avant 3.F.3), le stub vide s'exécute silencieusement : aucun défaut appliqué, aucun message d'erreur, le tool affiche "saved" alors que rien n'a été fait. C'est un comportement trompeur, non détectable au HW gate G4 car G4 ne teste `d` que pour ARPEG.

Le plan note §7.3.4.1 "bodies remplis dans 3.D.2 (rétroactif), 3.F.3, 3.C.2 (rétroactif)". Le mot "rétroactif" masque un danger : si une session EXEC oublie de revenir appliquer le body rétroactif, le stub reste vide, et le HW gate G4 ne l'attrape pas (il ne teste que ARPEG `d`).

**Recommandation** : reformuler la stratégie. Les stubs doivent soit (a) être des stubs actifs avec un `_setFlash("defaults non implementes - TODO")` pour signaler le problème en runtime, soit (b) être protégés par un `#error TODO` qui fait échouer le build si non implémentés. Option (b) inapplicable car multi-pass. Option (a) est correcte et ne coûte rien.

Impact : si non corrigé, `d` page BANK et `d` page LOOP avant leurs implémentations respectives ne font rien et le HW gate correspondant (G3, G5) ne le détecte pas si le testeur ne vérifie pas explicitement le comportement post-`d`.

---

**B-E2 — `_wkLoopPad` absent du `saveAll()` actuel et absent de 3.D/3.E ; risque data loss avant 3.F.3**

Localisation : §12.12 audit B-N6, `ToolPadRoles.cpp:366-415` (code actuel lu).

`saveAll()` (code réel HEAD) ne persiste que BankPadStore + ScalePadStore + ArpPadStore. LoopPadStore est absent. §12.12 prévoit d'ajouter `_nvs->saveLoopPad()` en 3.F.3 Patch 4.

Problème de séquençage : en 3.D (page BANK) et 3.E (page ARPEG), les phases utilisent `saveAll()` après chaque assignement. Si le testeur a configuré des pads LOOP via le legacy Tool 3 (ou dev seed M7) avant 3.A, puis navigue en page BANK et fait ENTER + save, `saveAll()` ne persiste pas LoopPadStore. Pire : `run()` charge `_wkLoopPad` depuis `_nvs->getLoadedLoopPadStore()` au begin(), mais si NVS est écrit partiellement (BankPad + ScalePad + ArpPad sans LOOP), le reboot relit le vieux LoopPadStore NVS. Pas de perte directe — mais confusion possible si l'utilisateur HW teste les pages ARPEG et LOOP successivement.

La vraie menace est en 3.F.2 : `_handleEnterPoolLoop` appelle `saveAll()` qui (avant le patch 3.F.3 §12.12) ne sauve pas LoopPadStore. Les changements LOOP semblent persistés (flashSaved), mais disparaissent au reboot. Ce bug actif entre 3.F.2 et 3.F.3 fait échouer le HW gate G5 de manière cryptique.

**Recommandation** : déplacer le patch `saveAll()` LoopPadStore de 3.F.3 à 3.F.1 (début de la phase LOOP, avant les handlers). Cela évite la fenêtre 3.F.2 cassée.

---

#### Optimisations (4)

**O-E1 — Dépendance 3.F.1 → `POOL_LINE_COUNT = 10` casse nav pool pages BANK/CC : correctif requis mais non localisé**

Localisation : §12.5 audit B-N2, §8.1.2 Patch 1.

`POOL_LINE_COUNT` passe de 6 à 10 en 3.F.1. La nav circulaire pool (ToolPadRoles.cpp:842,848) itère de 0 à `POOL_LINE_COUNT - 1`. En page BANK (1 ligne pool utile) et en page CC (sous-state machine propre, non impacté), le skip silencieux §12.5 doit être ajouté.

§12.5 dit "à intégrer dans Phase 3.F.1 ou 3.B". C'est 3.F.1 le bon endroit, mais le patch §12.5 n'est pas intégré dans le corps de §8.1.2 : il figure seulement dans §12.5 comme note de révision. Un exécutant de 3.F.1 qui ne lit pas §12.5 cassera la nav pool page BANK.

**Recommandation** : intégrer explicitement le correctif `do { ... } while (poolLineSize == 0)` dans §8.1.2 Patch 4 (nouveau patch) de 3.F.1.

---

**O-E2 — Permutation 3.C et 3.D : CC avant BANK est défendable mais crée une dépendance optique inverse**

Localisation : §1.4 séquence S3.

La séquence CC (3.C) avant BANK (3.D) est justifiée "absorption pure, scope strict" mais crée une asymétrie : la palette map=7/8 est introduite en 3.C.2 (page CC) et réutilisée en 3.D.2 (page BANK). Si BANK était en premier, la palette serait introduite là où elle a la plus grande importance conceptuelle (le "interdit" visible en BANK absorbante est le cas principal).

Ce n'est pas un bloquant — la séquence S3 fonctionne. Mais l'ordre pourrait être 3.C/3.D sans impact sur les dépendances réelles (palette 7/8 est dans `SetupUI.h`, partagée). Mentionné pour traçabilité.

---

**O-E3 — `_applyDefaultsCc` pas précisément spécifié : réutilise `_resetAllCc` ou vide `_wkCc` ?**

Localisation : §7.3.2 Patch 5 note "`_applyDefaultsCc()` → rétroactif 3.C.2 (vide _wkCc) ou réutilisation `_resetAllCc` migré depuis Tool 4 `_handleConfirmDefaultsCc`".

Deux implémentations sont proposées avec un "ou" : (a) vide `_wkCc.count = 0` directement, (b) réutilise `_resetAllCc` de Tool 4. Ces deux options ont des effets différents — Tool 4 `_handleConfirmDefaultsCc` réinitialise aussi `_ccGlobalFieldIdx` et appelle `_loadCc()` pour rechager les defaults. La page CC hérite d'une sémantique `d` qui doit être cohérente avec le modèle §15.1 ("defaults factory de la page").

**Recommandation** : trancher explicitement dans le plan plutôt que laisser "ou". Pour cohérence §15, la sémantique page CC de `d` devrait être "reset à factory vide (count=0)" — pas "reload depuis NVS". Sinon `d` dans CC ne fait rien si NVS est déjà vide.

---

**O-E4 — Fragilité de la décision §1.8 "stub coexistence 3.E.3 testée 3.F.3" en présence de régressions HW gates intermédiaires**

Localisation : §1.7, §7.0, §8.3.4.

La coexistence cross-AC est codée côté ARPEG en 3.E.3 mais non testée. Le plan suppose que 3.F.3 valide les deux codes simultanément (mini-audit binôme 3.E.3 / 3.F.3). Si le code 3.E.3 contient un bug silencieux (ex. `_clearRolesArpegOnly` efface aussi les rôles LOOP par erreur), 3.F.3 sera le seul moment où ce bug est visible — mais G5 ne teste pas la corrective failure mode de 3.E.3 isolément.

Ce n'est pas bloquant si le testeur est attentif en G5 step 6 (coexistence cross-AC scenario A). Mais il serait plus robuste que G4 inclue une vérification visuelle de la coexistence ARPEG-seul (TAB LOOP, vérifier que le pad portant Root A ARPEG affiche `--` côté LOOP — ce que G4 ne demande pas explicitement).

---

#### Manquants (2)

**M-E1 — Pas de vérification que le wiring `_ccScreenDirty` vs `screenDirty` local est spécifié**

Localisation : §5.1.5 Mini-audit 3.C.1a point 4.

Le plan signale en mini-audit que Tool 4 utilise `_screenDirty` member et que ToolPadRoles utilise `screenDirty` local dans `run()`. La convention retenue est `_ccScreenDirty` membre dédié. Mais aucun patch dans §5.1.2 ni §5.2.2 ne mentionne l'ajout de `bool _ccScreenDirty` dans `ToolPadRoles.h` ni son initialisation dans le constructeur.

Si `_ccScreenDirty` est référencé dans `ToolPadRoles_Cc.cpp` (migré depuis Tool 4 `_screenDirty`) sans être déclaré, le compile échoue. C'est un bloquant build potentiel en 3.C.1b.

**Recommandation** : ajouter `bool _ccScreenDirty;` dans §5.1.3 Patch 1 header + initialisation dans constructeur.

---

**M-E2 — `poolLineSize()` pour lignes 6-9 LOOP non spécifiée : implémentation à définir en 3.F.1**

Localisation : §8.2.4 Mini-audit 3.F.2 point 1, §12.5.

`poolLineSize()` actuelle (ToolPadRoles.cpp legacy) ne couvre que lignes 0-5. Le plan §8.2.4 note "À vérifier en pré-exécution 3.F.2" mais ne fournit pas l'implémentation dans §8.1.2 (3.F.1). L'absence du correctif de `poolLineSize` pour lignes 6-9 couplée au skip silencieux §12.5 rend la nav pool LOOP inutilisable si les deux patches ne sont pas écrits en 3.F.1.

**Recommandation** : ajouter dans §8.1.2 Patch 4 (bis) un patch `poolLineSize()` extension pour lignes 6-9 avec dispatch `_activeSubPage == SUB_LOOP`.

---

### Éléments vérifiés OK (8)

1. Dépendance 3.A → 3.B : refacto pur compilateur. `ROLE_HOLD → ROLE_PLAY_STOP` attrapé. Séquençage correct.
2. Dépendance 3.B → 3.C.1a : création fichiers `.cpp` stubs avec `_buildRoleMapLegacy` delegation. Correcte — les stubs buildent sans les vraies impl.
3. Dépendance 3.C.2 → 3.D.2 : palette map=7/8 dans `SetupUI.h` partagée. Cohérente cross-phase.
4. Décision §1.5 (`_buildRoleMapLegacy` retiré en 3.H.2) : logique — dernier consommateur (default case switch) ne disparaît qu'après 3.F.1 qui remplace le dernier stub. Chronologie correcte.
5. Décision §1.3 (rename P-EARLY) : toujours défendable. Le code actuel HEAD confirme que `holdPad` / `ROLE_HOLD` sont partout non-renommés — le rename est bien un prérequis réel à toutes les phases.
6. §12.7 (rename `_toolRoles → _toolPadRoles`) inclus en 3.A : cohérent — évite de propager l'ancien nom dans 3.B+ où le code lit SetupManager.
7. §12.8 (suppression orphan link `ScaleManager::_holdPad`) : correction invariant 7 correctement localisée en 3.A. Code actuel HEAD confirme la présence de `s_scaleManager.setHoldPad(holdPad)` ligne 655 main.cpp — le fix est nécessaire.
8. §12.11 (retrait flash 3.B dès 3.C.2) : cohérence no-op silencieux uniforme. Décision correcte — le flash produisait une UX bicéphale pendant 4-5 phases.

---

## Axe B — HW gates : revue détaillée

### G0 — Phase 3.A (§3.5)

**Tests actuels (10 steps)** :
1. Build clean
2. Upload + monitor
3. Boot : message NVS mismatch v2→v3
4. Badge T3 menu
5. Entrer Tool 3, pool NORM, placer "Hld" pad 23, Save. Reboot.
6. Boot : log "arp pads loaded (v3 store): arpPlayStop=23"
7. Foreground bank ARPEG. Jouer 2-3 notes (pile peuplée).
8. Tap pad 23 → ARPEG démarre
9. Tap pad 23 → stop, pile préservée
10. Tap pad musical → relance step 0

**Invariants testés** : invariant 1 (no orphan notes — pile préservée au stop), invariant 7 (NVS bump cohérent). Aucun invariant 2 (arp refcount) direct, mais ARPEG Play/Stop implique le path noteOn/Off.

**Volume** : 10 steps, ~5-8 min setup + ~10 min exécution.

**Verdict step-by-step** :

| Step | Catégorie | Action recommandée | Justification |
|---|---|---|---|
| 1 | Garder | — | Build gate obligatoire |
| 2 | Garder | — | Upload requis, pas automatisable |
| 3 | Garder | — | Valide NVS version bump (observable : message serial) |
| 4 | Gaspillage léger | Fusionner avec step 6 | Badge T3 au menu est secondaire ; la vraie validation est le reload NVS étape 6. Peut être observé en passant. |
| 5 | Simplifiable | Reformuler : "placer P/S sur un pad libre (ex. pad 23) via pool NORM, ENTER, Save, Reboot" | Wording actuel dit "sélectionner 'Hld'" — label legacy ; après 3.A le label UI reste "Hld" (délibéré §3.3 scope strict), OK, mais le testeur doit savoir que c'est normal |
| 6 | Garder | — | Valide invariant critique : NVS v3 persisté correctement avec nom de champ `arpPlayStop=23` |
| 7 | Garder | — | Prérequis aux steps 8-10 : pile peuplée |
| 8 | Garder | — | Invalide Play/Stop toggle fonctionnel |
| 9 | Garder | — | Invalide invariant 1 (pile sacrée fix F1) — test le plus critique |
| 10 | Gaspillage | Retirer | Step 10 (tap pad musical → relance step 0) est le fix F1 du 2026-05-15, déjà stable. Non modifié par 3.A. La regression est impossible sauf si `handleArpPlayStopPad` est modifié — ce qui n'est pas le cas en 3.A. |

**Tests manquants** : vérifier que `Tool 4` est encore fonctionnel (assignation CC basique sur un pad libre) — mentionné dans les critères mais absent de la procédure. À insérer comme step 5.5 : "ENTER Tool 4 depuis menu, assigner CC0 sur pad libre, save, confirmer présence en runtime MIDI."

**Recommandation G0 allégée** : retirer step 10, fusionner step 4 en observation passante step 5, ajouter step 5.5 Tool 4 check. Résultat : 9 steps → 8 steps actifs.

---

### G1 — Phase 3.B (§4.4)

**Tests actuels (9 steps)** :
1. Boot OK, RAM/Flash inchangés
2. Menu : Tool 3 + Tool 4 sélectionnables
3. Entrer Tool 3 : header "TOOL 3: PAD ROLE" + sub-page BANK highlighted + 4 labels
4. TAB : cycle BANK → ARPEG → LOOP → CC → BANK, contenu legacy identique par page
5. Nav arrows OK
6. ENTER ouvre pool (comportement actuel). Pool 5 lignes legacy.
7. Save d'un rôle. Reload reboot. Persiste.
8. q exit OK depuis chaque page
9. Tool 4 toujours fonctionnel

**Invariants testés** : navigation TAB correcte, persistance NVS non régressée, Tool 4 non cassé.

**Verdict step-by-step** :

| Step | Catégorie | Action recommandée | Justification |
|---|---|---|---|
| 1 | Faisabilité partielle | Reformuler : "Build clean, 0 warning" | "RAM/Flash inchangés à variance compile près" n'est pas observable simplement sans tools dédiés. La vraie gate est : build clean + boot OK |
| 2 | Garder | — | Valide que Tool 4 survit à 3.B (critique pour 3.C.1b) |
| 3 | Garder | — | Header + 4 labels = observable précis |
| 4 | Garder, simplifier | Tester TAB cycle complet (4 tours) — pas besoin de valider le "contenu legacy identique" en détail ; seul le cycle et le header highlighted importent | Le contenu legacy n'est pas cassable par 3.B (stubs delegating legacy) |
| 5 | Gaspillage | Fusionner avec step 4 | Nav arrows est testé implicitement en se déplaçant pour vérifier step 4 |
| 6 | Garder | — | Valide que pool legacy fonctionne (non régressé) |
| 7 | Garder | — | NVS persistance : invariant critique |
| 8 | Gaspillage | Simplifier : "q exit depuis page BANK uniquement" | `q` depuis chaque page = 4 tests redondants ; le code exit est dans l'orchestrateur, pas page-specific en 3.B |
| 9 | Garder | — | Tool 4 non cassé par 3.B = prérequis absolu pour 3.C.1b |

**Tests manquants** : vérifier que `SUB_NORM` n'est plus référencé (auto-review grep déjà prévu dans §4.3 A — mais pas dans la procédure HW). Peut rester en auto-review.

**Recommandation G1 allégée** : fusionner steps 4+5, simplifier step 1, simplifier step 8. 9 steps → 7 steps.

---

### G2 — Phase 3.C (§5.4, combiné 3.C.1a + 3.C.1b + 3.C.2)

**Tests actuels (8 steps)** :
1. Boot, badge T3 = état combiné NVS. Tool 4 absent du menu.
2. Entrer Tool 3 → TAB 3× pour page CC
3. Vérifier cell display §8.1 (BANK ambre+, ARPEG ■■, LOOP control R/P/C, libres ---)
4. Info panel langue musicien (3 cas : pad BANK, pad ARPEG Root A, pad LOOP REC)
5. Tentatives ENTER (no-op BANK, no-op ARPEG, flash 3.B LOOP — **attention : §12.11 dit flash LOOP retiré en 3.C.2** → incohérence dans la procédure)
6. Test non-régression Tool 4 (ENTER pad 5, MODE_PICK, MOM, VALUE_EDIT, GLOBAL_EDIT, CONFIRM_REMOVE, CONFIRM_DEFAULTS)
7. q exit → retour menu
8. Reboot, persistance

**Invariants testés** : absorption Tool 4 (non-régression), cell display §8.1, refus cross-page (invariant 7 Setup/Runtime coherence — page CC ne peut assigner sur BANK/contextuels).

**Faisabilité** : step 5 contient une **incohérence documentée** : la procédure §5.4 step 5 dit "ENTER sur pad 32 (LOOP REC) → flash 'Pad is LOOP REC/PS/CLR'..." mais §12.11 audit M4 prescrit de retirer ce flash dès 3.C.2 pour cohérence. La procédure et la décision §12.11 sont contradictoires. Critère PASS non observable de façon fiable.

**Verdict step-by-step** :

| Step | Catégorie | Action recommandée | Justification |
|---|---|---|---|
| 1 | Garder | — | Badge T3 + absence Tool 4 = critères observables |
| 2 | Garder | — | Navigation vers page CC valide TAB hérité 3.B |
| 3 | Garder | — | Cell display §8.1 : test critique page CC, observable sans ambiguïté |
| 4 | Simplifier | Tester 2 cas info panel (BANK pad + contextuel pad) au lieu de 3 | 3 cas sont longs à setup ; 2 suffisent pour valider `_formatRoleNameMusician` + `_padNeighborInfo` |
| 5 | Faisabilité KO | Reformuler step 5 : "ENTER sur pad 0 (BANK) → no-op silencieux. ENTER sur pad 8 (ARPEG Root) → no-op silencieux. ENTER sur pad 32 (LOOP REC) → **no-op silencieux** (flash retiré §12.11)." | §12.11 prescrit retrait flash. Le critère doit refléter la décision actée. |
| 6 | Garder | — | Non-régression Tool 4 = test critique. Simplifier : tester uniquement ENTER (MODE_PICK) + VALUE_EDIT + q (exit). GLOBAL_EDIT + CONFIRM_REMOVE + CONFIRM_DEFAULTS peuvent être regroupés en "fonctionnalités avancées CC OK" |
| 7 | Gaspillage | Fusionner avec step 8 | Exit + reboot = séquentiel naturel, 1 observation |
| 8 | Garder | — | Persistance NVS : invariant critique |

**Incohérence bloquante step 5** : la procédure doit être corrigée pour aligner sur §12.11. Actuellement le critère "flash 3.B preserved" dans les critères §5.4 est contradictoire avec l'audit §12.11 actée.

**Tests manquants** : vérifier que `NVS T3 badge agrégé` reflète bien ControlPad (descriptor 5 dans TOOL_NVS_LAST[2] — cf §12.6 check ad-hoc descriptor 12). Le badge T3 devrait être visible en step 1, mais le critère explicite "badge T3 reflète ControlPad" n'est pas dans la procédure step-by-step.

**Recommandation G2 allégée** : corriger step 5 (retirer mention flash LOOP), simplifier step 4 à 2 cas, fusionner steps 7+8. 8 steps → 6 steps actifs.

---

### G3 — Phase 3.D (§6.3, combiné 3.D.1 + 3.D.2)

**Tests actuels (13 steps)** :
1. Boot, badge T3 `--`
2. Entrer Tool 3, page BANK par défaut
3. Vérifier rendu initial (GRID libres + pad 10 CC ambre+ + pad 20 ■■ + pads 32/33/34 ■■)
4. Hard-constraint test : q → flash
5. Assigner Bank 1 pad 0 (ENTER + ENTER Bk1) → save, cell Bk1 blanc, pool Bk1 dim
6. §7.4 strict test (dégage direct) : curseur pad 0, ENTER → dégage + pad 0 vide + Bk1 vert menthe
7. Ré-assigner Bank 1 pad 0, puis Banks 2-8 pads 1-7
8. §8.1 cross-page test CC : curseur pad 10, cell CC00 ambre+, INFO, ENTER no-op
9. §8.1 cross-page test ARPEG : curseur pad 20, cell ■■, INFO, ENTER no-op
10. §8.1 cross-page test LOOP : curseur pad 32, cell ■■, INFO, ENTER no-op
11. q exit → OK (8 banks assignées)
12. Reboot, persistance
13. §9.2 strict test (no silent steal) : ENTER pad 5, pool Bk2 dim, ENTER → no-op

**Invariants testés** : §6.5 hard-constraint exit, §7.4 dégage direct (ABSORBANT BANK), §9.2 pas de silent steal, §8.1 cell display cross-page, invariant 7 (4-link chain BANK ↔ Store ↔ Tool ↔ NVS).

**Setup préalable** : assigner CC0 pad 10 et Root A pad 20 via pages précédentes (G2). Plus dev seed M7 LOOP REC=32, PS=33, CLR=34. Setup non trivial.

**Verdict step-by-step** :

| Step | Catégorie | Action recommandée | Justification |
|---|---|---|---|
| 1 | Garder | — | Badge T3 `--` = NVS reset après 3.A : observable |
| 2 | Garder | — | Page BANK par défaut : valide `_activeSubPage(SUB_BANK)` |
| 3 | Garder, simplifier | Vérifier 2 des 3 types de cell (CC ambre+ + ■■ neutre) plutôt que les 3 categories | Pads 32/33/34 LOOP = mêmes que pads ■■ ARPEG. 2 types couvrent §8.1 |
| 4 | Garder | — | Hard-constraint §6.5 : invariant critique observable |
| 5 | Garder | — | Assignement BANK : fonctionnalité de base |
| 6 | Garder | — | §7.4 strict (dégage direct) : déviation UX legacy significative, doit être explicitement testée |
| 7 | Simplifier | Assigner Banks 2-8 rapidement (pas de vérification individuelle) | Steps 1-6 ont déjà validé le mécanisme d'assignement |
| 8 | Garder | — | CC ambre+ : critique §8.1 |
| 9 | Garder | — | ■■ neutre ARPEG : critique §8.1 |
| 10 | Gaspillage | Fusionner avec step 9 | ■■ neutre LOOP = comportement identique à ■■ neutre ARPEG. Un seul test suffit |
| 11 | Garder | — | Exit après contrainte satisfaite |
| 12 | Garder | — | NVS persistance : invariant critique |
| 13 | Garder | — | §9.2 no silent steal : critique ABSORBANT, comportement spécifique non testé ailleurs |

**Tests manquants** : le cas extrême §12.10 (`d` ARPEG puis `d` BANK → exit deadlock) mentionné comme ajout à G3 dans §12.10 n'est pas dans la procédure §6.3. À ajouter comme step 7.5 : "Test cas extrême deadlock : `d` → y (apply BANK defaults) → q → vérifier que hard-constraint bloque si un conflit cross-page a empêché l'assignement".

**Recommandation G3 allégée** : fusionner steps 3 (2 types instead of 3), step 10 avec 9, simplifier step 7. Ajouter step 7.5 deadlock. 13 steps → 11 steps.

---

### G4 — Phase 3.E (§7.4, combiné 3.E.1 + 3.E.2 + 3.E.3)

**Tests actuels (11 steps)** :
1. Boot. Entrer Tool 3 → TAB page ARPEG.
2. Vérifier rendu §8.1 (banks ambre+, CC ambre+, LOOP control --)
3. Vérifier pool 4 lignes (Root/Mode/Oct/PL/S vert menthe)
4. Test `d` defaults ARPEG (prompt + y → pads 8-28 assignés)
5. Test conflict skip silencieux (CC1 pad 8, re-`d`, Root A skipped)
6. Test swap intra-AC §7.2 (ENTER pad 8, pool Root, cursor B, ENTER → steal silencieux)
7. Test refus absorbants (ENTER pad 0, ENTER pad 10 → no-op)
8. Test info panel langue musicien (3 curseurs : Root B, P/S, CC0)
9. Test `[---] clear role` page-scoped (ENTER pad 23, `[---]`, P/S retiré, LOOP pad 32 inchangé)
10. q exit → hard-constraint OK
11. Reboot, persistance

**Invariants testés** : §7.2 swap intra-AC silencieux, §8.1 cell display page ARPEG, skip silencieux §15.5, refus absorbants, invariant 1 (no orphan notes — clear page-scoped ne touche pas LOOP).

**Faisabilité issue** : step 5 requiert de sortir de Tool 3, aller en page CC assigner CC1 pad 8, puis revenir page ARPEG — soit environ 2-3 min de manipulation. C'est le seul test qui valide le skip silencieux, et c'est nécessaire. Mais le setup préalable dit "8 banks assignées (depuis 3.D test G3), pad 10 = CC0 (depuis 3.C test G2), Dev seed M7 LOOP REC/PS/CLR sur 32/33/34". Step 5 nécessite en plus d'assigner CC1 sur pad 8 — le plan ne le liste pas dans le setup préalable. **Oubli**.

**Verdict step-by-step** :

| Step | Catégorie | Action recommandée | Justification |
|---|---|---|---|
| 1 | Garder | — |
| 2 | Simplifier | Vérifier banks ambre+ (1 exemple) + LOOP -- (1 exemple) | 2 types couvrent §8.1 page ARPEG |
| 3 | Garder | — | Pool 4 lignes vert menthe : critique §9.2 assignable |
| 4 | Garder | — | `d` defaults : critique §15. Observer pads 8-28 en couleurs pêche/cyan/vert/pourpre |
| 5 | Garder, corriger setup | Ajouter CC1 pad 8 dans setup préalable G4 | Test nécessaire pour §15.5 skip silencieux. Oubli dans setup préalable. |
| 6 | Garder | — | §7.2 swap silencieux : comportement nouveau, non évident, critique |
| 7 | Simplifier | Tester 1 absorbant (BANK) plutôt que 2 | BANK et CC ont même comportement (no-op) en page ARPEG |
| 8 | Simplifier | Tester 2 cas info panel (Root B + curseur LOOP REC invisible) | 3 cas info panel = lourd pour un test secondaire |
| 9 | Garder | — | `[---]` page-scoped préservation cross-page : valide invariant §15.3 critique |
| 10 | Garder | — |
| 11 | Garder | — |

**Tests manquants** : vérification que le comportement coexistence cross-AC ARPEG-LOOP est partiellement validé en G4 (TAB page LOOP depuis page ARPEG, vérifier que le pad portant Root B affiche `--` en page LOOP). Non requis pour G4 complet mais améliorerait la détection précoce d'un bug 3.E.3.

**Recommandation G4 allégée** : corriger oubli setup step 5, simplifier steps 2, 7, 8. Ajouter observation cross-tab facultative. 11 steps → 9 steps actifs.

---

### G5 — Phase 3.F (§8.4, combiné 3.F.1 + 3.F.2 + 3.F.3)

**Tests actuels (12 steps)** :
1. Boot. Tool 3 → TAB page LOOP.
2. Vérifier rendu §8.1 (banks/CC ambre+, ARPEG --)
3. Vérifier pool 4 lignes LOOP (REC/PL/S/CLR/Slots vert menthe)
4. Test `d` defaults LOOP (prompt + y → REC=32, PS=33, CLR=34)
5. Test skip silencieux conflits cross-page (CC1 pad 32, re-`d`, REC skipped)
6. Test coexistence cross-AC §14.1 scenario A (pad 8 invisible page LOOP, ENTER pool Slot 0, Slot 0 jaune, TAB ARPEG Root A inchangé)
7. Test PL/S unifié §14.1 (PS LOOP pad 23 = PS ARPEG pad 23, info panel "geste unifié")
8. Test swap intra-AC §7.2 (ENTER pad 32 REC, pool Slot 0, ENTER → steal, REC retiré)
9. Test refus absorbants (ENTER pad 0 Bank, ENTER pad 10 CC0 → no-op)
10. Test `[---]` clear page-scoped (pad 8 Slot 0, `[---]` → Root A préservé)
11. q exit → hard-constraint OK
12. Reboot, persistance

**Invariants testés** : §7.3 coexistence cross-AC, §14.1 PL/S unifié, §7.2 swap intra-AC, §8.1 cell display page LOOP, §15.3 clear page-scoped, saveAll() LoopPadStore — **mais saveAll() LoopPadStore n'est pas ajouté avant 3.F.3 si B-E2 n'est pas corrigé** : risk de test step 12 (persistance) échouant.

**Faisabilité** : step 7 (PL/S unifié §14.1) requiert de préassigner PS ARPEG pad 23 (G4 default), dégager PS LOOP pad 33 via `[---] clear`, puis assigner PS LOOP pad 23. C'est 4-5 manipulations supplémentaires. Le test est nécessaire (cas unique, invariant §14.1), mais la procédure ne détaille pas ces pre-steps clairement.

**Verdict step-by-step** :

| Step | Catégorie | Action recommandée | Justification |
|---|---|---|---|
| 1 | Garder | — |
| 2 | Simplifier | Vérifier 1 bank ambre+ + 1 ARPEG -- | Même pattern que pages précédentes |
| 3 | Garder | — |
| 4 | Garder | — |
| 5 | Garder | — | Skip silencieux LOOP : équivalent step 5 G4 mais côté LOOP |
| 6 | **Garder, critique** | — | Coexistence cross-AC end-to-end : test le plus important du plan. Observable (cell LOOP jaune + TAB ARPEG inchangée). Critère binaire. |
| 7 | Garder, détailler | Ajouter les pre-steps explicites dans procédure | PL/S unifié §14.1 = invariant unique à cette phase. Ne pas simplifier. Détailler les pre-steps dans la procédure. |
| 8 | Simplifier | Vérifier 1 swap (REC → Slot) suffit, tester steal visual | Symétrique G4 step 6 |
| 9 | Gaspillage | Retirer | Testé en G4. Page LOOP et ARPEG partagent le même code de refus absorbants. |
| 10 | Garder | — | Clear page-scoped LOOP préservant ARPEG : valide invariant §15.3 côté LOOP |
| 11 | Garder | — |
| 12 | **Garder, prérequis B-E2** | Conditionner à la correction B-E2 (saveAll LOOP en 3.F.1 pas 3.F.3) | Sans la correction, step 12 échouera systématiquement entre 3.F.2 et 3.F.3 |

**Tests manquants** : vérifier que `_wkLoopPad.playStopPad` (PL/S LOOP) et `_wkArpPlayStopPad` (PL/S ARPEG) sont correctement comparés dans l'info panel pour le cas "geste unifié". Le test step 7 valide visuellement mais pas le log serial. Pas critique.

**Recommandation G5 allégée** : simplifier steps 2, 8, retirer step 9, détailler step 7. 12 steps → 10 steps. Correction B-E2 est prérequis critique pour step 12.

---

### G6 — Phase 3.G (§9.3, combiné 3.G.1 + 3.G.2)

**Tests actuels (16 steps)** :
1. Pré-test coexistence Root D + Slot 3 pad 22 (setup)
2. TAB page BANK. Cell pad 22 ■■
3. Info panel pad 22 (Root D + Slot 3)
4. ENTER pad 22 → pool ouvert ligne Bank. Cursor Bk5.
5. **ENTER Bk5 → modale apparaît** avec wording exact §10.2
6. Vérifier wording exact (quotes, "et", "devront", "Y/N ?")
7. Test `n` annulation → pad 22 inchangé, pool fermé
8. Re-ENTER pad 22, pool Bk5, ENTER → modale réapparaît
9. Test `y` confirmation → modale disparaît, pad 22 = Bk5
10. Test propagation §6.6 fine (TAB ARPEG : pool Root D vert menthe, pad 22 ambre+. TAB LOOP : pool Slot 3 vert menthe. TAB CC : pad 22 ambre+.)
11. Test modale page CC + contextuels (Root C pad 30, retour CC, MODE_PICK, MOM, ENTER → modale)
12. Test modale LOOP control (PL/S LOOP pad 33, CC MODE_PICK, MOM → modale. PAS de flash 3.B)
13. Test modale 3-4 rôles (Root A + Octave 2 + Slot 5 pad 40 → page BANK → Bk7 → modale 3 rôles)
14. Test refus permanent CC + BANK (ENTER pad 0 depuis CC → no-op. ENTER pad 30 depuis BANK → no-op)
15. q exit → hard-constraint OK
16. Reboot, persistance

**Invariants testés** : modale §10 complète (wording, effet y, annulation n), propagation §6.6, refus absorbant-absorbant, LOOP control via modale (pas flash).

**Faisabilité** : setup pre-test step 1 (Root D + Slot 3 sur pad 22) = 2 TABs + 2 ENTERs + pool nav. Gérable. Step 13 (Root A + Octave 2 + Slot 5 pad 40) = 3 pages × TAB nav + assign = ~5-7 min. Lourd.

**Verdict step-by-step** :

| Step | Catégorie | Action recommandée | Justification |
|---|---|---|---|
| 1 | Garder | — | Setup nécessaire pour test modale |
| 2 | Garder | — | Cell ■■ = précondition observable |
| 3 | Garder | — | Info panel valide `_padNeighborInfo` + formatage |
| 4 | Garder | — |
| 5 | **Garder, critique** | — | Modale : test le plus important de G6 |
| 6 | Garder | — | Wording exact §10.2 : critère observable précis |
| 7 | Garder | — | Annulation `n` : invariant (pad inchangé) |
| 8 | Gaspillage | Fusionner avec step 5 (réutiliser même setup, re-ENTER direct) | Step 8 = répétition de step 4. Inutile si step 9 suit immédiatement |
| 9 | Garder | — |
| 10 | **Garder, critique** | — | Propagation §6.6 fine cross-page : invariant unique à cette phase. Observer les 3 TABs. |
| 11 | Garder | — | Modale page CC : valide wiring 3.G.2 côté CC |
| 12 | Garder | — | LOOP control via modale (retrait flash §12.11) : observable binaire (modale vs flash) |
| 13 | Faisabilité lourde | Simplifier : placer Root A + Slot 5 pad 40 seulement (2 rôles max), pas 3 | Test modale avec 3 rôles apporte peu vs 2 rôles. Le code `_formatOverwriteWording` traite les cas 1/2/3/4 ; tester 2 suffit + tester 1 (step 11). |
| 14 | Simplifier | Tester 1 cas (CC + BANK) au lieu de 2 | Les deux cas partagent le même refus permanent ; 1 cas valide le pattern |
| 15 | Garder | — |
| 16 | Garder | — |

**Tests manquants** : vérifier que `_editing = false` après modale (retour grid nav, pas pool nav) — étape 9 ouvre vers step 10 directement sans mentionner l'état de navigation. À ajouter : après `y`, vérifier que la barre de contrôle affiche le mode grid (pas pool).

**Recommandation G6 allégée** : fusionner steps 7+8 (non, 8+9), simplifier step 13 à 2 rôles, simplifier step 14 à 1 cas. Ajouter note état nav après `y`. 16 steps → 12 steps.

---

### G7 — Phase 3.H (§10.3, combiné 3.H.1 + 3.H.2)

**Tests actuels (9 steps)** :
1. Flash effacement NVS (touche `e` au boot maintenu) → premier boot factory
2. Boot. Pas de message M7. LOOP REC/PS/CLR pads 0xFF.
3. Entrer Tool 3 → page LOOP. Cell pads 32/33/34 → `--` (vides)
4. `d` defaults LOOP → REC=32, PS=33, CLR=34
5. Save + reboot. Persiste.
6. Audits palette HW : ambre+ saturé, vert menthe pool, Root pêche, Mode cyan, Octave pourpre, PL/S vert, REC rouge, CLR bleu foncé, Slots jaune, curseur inverse
7. Si audit dévie : swap macros ANSI, rebuild, re-test
8. Test toutes pages BANK/ARPEG/LOOP/CC : pas de régression cell display, info panels, modale, `d` defaults
9. Test scenarios §14 spec entiers (live mixed ARPEG+LOOP, re-attribution bank, collision CC)

**Invariants testés** : retrait dev seed M7, palette finale observée HW, non-régression toutes features 3.A-3.G.

**Faisabilité** :

- Step 1 (flash NVS via `e` au boot) : procédure non triviale. La touche `e` au boot est-elle documentée ? Dans le code actuel, ce mécanisme est géré dans le setup gate au boot. À confirmer que la touche `e` au boot existe bien dans le firmware actuel HEAD. **Non vérifié dans ce plan.**
- Step 6 : 10 couleurs à auditer visuellement. Subjectif mais inévitable.
- Step 9 ("Test scenarios §14 spec entiers") : vague. §14.1 scenario A nécessite une config live ARPEG+LOOP running — implique un reboot + config + play MIDI + DAW actif. ~10-15 min.

**Verdict step-by-step** :

| Step | Catégorie | Action recommandée | Justification |
|---|---|---|---|
| 1 | Faisabilité à vérifier | Confirmer que flash NVS via `e` au boot est implémenté dans le firmware HEAD (chercher dans main.cpp le handler boot `e`) | Non documenté dans ce plan. Si absent, procédure inexécutable. |
| 2 | Garder | — | Observable binaire (présence/absence message M7) |
| 3 | Garder | — |
| 4 | Garder | — | `d` defaults LOOP = équivalent fonctionnel M7 : observable |
| 5 | Garder | — |
| 6 | Garder | — | Palette HW : inévitable, subjectif mais unique test pour ces macros |
| 7 | Garder | — | Conditionnel au résultat step 6 |
| 8 | Simplifier | Tester 2 pages au hasard (ex. BANK + LOOP) + modale 1 cas + `d` 1 page | Test de non-régression complet de toutes les features = test de régression full-suite. En pratique, si le build passe et les HW gates G0-G6 sont OK, G7 step 8 est surtout une smoke-check. |
| 9 | Faisabilité KO | Reformuler : "Tester scenario §14.1 A (PL/S unifié déjà testé G5) — skip, déjà couvert. Tester scenario §14.2 B uniquement (modale re-attribution bank déjà couverte G6 step 5-9)". | "Scenarios §14 entiers" = redondance avec G4/G5/G6. Les scenarios sont déjà couverts par les gates précédents. G7 step 9 doit se limiter à une smoke live "instrument joue normalement". |

**Tests manquants** : vérifier que `clearRole(pad)` legacy est bien absent (§10.2.4 Hard-assert C) et que `_buildRoleMapLegacy` est bien absent (Hard-assert B) — ces checks sont dans les hard-asserts auto-review mais pas dans la procédure HW. Peuvent rester auto-review.

Vérifier que flash NVS au boot existe : `grep -n "boot.*nvs.*reset\|nvs.*clear.*boot\|setup.*gate.*erase" src/main.cpp` — à exécuter en pré-exécution.

**Recommandation G7 allégée** : clarifier faisabilité step 1, simplifier step 8, reformuler step 9. 9 steps → 7 steps actifs.

---

### Synthèse HW gates

| Gate | Steps actuels | Steps recommandés | Gain estimé | Findings sévérité |
|---|---:|---:|:---|---|
| G0 | 10 | 8 | ~20% | Gaspillage (step 10), Manquant (Tool 4 check step 5.5) |
| G1 | 9 | 7 | ~22% | Gaspillage (steps 4+5, step 8) |
| G2 | 8 | 6 | ~25% | Faisabilité KO (step 5 flash/no-flash), Bloquant incohérence §12.11 |
| G3 | 13 | 11 | ~15% | Manquant (step 7.5 deadlock test) |
| G4 | 11 | 9 | ~18% | Manquant (oubli setup step 5 CC1 pad 8) |
| G5 | 12 | 10 | ~17% | Gaspillage (step 9 refus absorbants redondant), Bloquant B-E2 (saveAll LOOP) |
| G6 | 16 | 12 | ~25% | Gaspillage (step 8), Faisabilité lourde (step 13 à 3 rôles) |
| G7 | 9 | 7 | ~22% | Faisabilité à vérifier (step 1 flash NVS), Faisabilité KO (step 9 vague) |
| **Total** | **88** | **70** | **~20%** | |

### Éléments vérifiés OK (6)

1. Chaque HW gate teste un ensemble cohérent de phases (pas de test orphelin ou anachronique). La corrélation gate → phase est rigoureuse.
2. Les HW gates G4 et G5 couvrent les 4 invariants CLAUDE.md applicables (1, 3, 4, 6 bank slots alive). Invariant 2 (arp refcount) couvert en G0 (Play/Stop toggle).
3. Le pattern "setup préalable cumulatif" (état issu du gate précédent) est réaliste et économise du temps de re-configuration.
4. G6 test propagation §6.6 est le seul endroit où cette invariant est testée : bien placé, ne peut pas être raccourci.
5. G7 audit palette est le seul endroit pour valider les macros ANSI HW : bien placé en fin de plan.
6. Les hard-asserts auto-review (§X.3/§X.4) par phase couvrent les invariants statiques (présence/absence symboles) indépendamment des HW gates — la séparation auto-review / HW gate est correcte.

---

## Synthèse globale

### Volume de findings par sévérité

| Sévérité | Axe A Enchaînement | Axe B HW gates | Total |
|---|---:|---:|---:|
| Bloquants | 2 | 1 (G2 step 5 incohérence §12.11) | **3** |
| Gaspillage | — | ~10 steps identifiés sur 8 gates | **~10 points** |
| Faisabilité | — | 2 (G7 step 1 non documenté, G7 step 9 vague) | **2** |
| Manquants | 2 | 3 (G0 Tool 4 check, G3 deadlock test, G4 oubli setup) | **5** |
| Optimisations | 4 | — | **4** |

### Verdict global

**Boucle iter 3.6** requise pour les 3 bloquants. Les 2 bloquants enchaînement (B-E1 stubs vides silencieux, B-E2 saveAll LOOP trop tard) et le 1 bloquant HW gates (G2 incohérence step 5 flash) doivent être corrigés avant toute session EXEC. Les autres findings sont des améliorations de qualité sans impact sur l'exécutabilité du plan.

### Top 5 allègements prioritaires (gain temps + observabilité)

1. **G2 step 5** : corriger mention flash LOOP → no-op silencieux (§12.11 actée). Impact : supprime un critère PASS non observable.
2. **G6 steps 8+13** : fusionner step 8 (redondant), simplifier step 13 à 2 rôles. Impact : ~15 min économisées.
3. **B-E2 saveAll LOOP** : déplacer patch `saveAll()` de 3.F.3 à 3.F.1. Impact : supprime une fenêtre de bug persistance entre 3.F.2 et 3.F.3, évite G5 step 12 échouant.
4. **G7 step 9** : reformuler "scenarios §14 entiers" en smoke-check live 5 min. Impact : supprime 10-15 min de re-test redondant avec G4/G5/G6.
5. **G4/G5 steps 7/9** : retirer 1 test refus absorbants redondant par gate (pattern identique cross-pages, 1 test G4 suffit). Impact : ~5 min économisées.

### Notes / observations

- **`ArpRoleKind` dans le code actuel HEAD** : l'enum (KeyboardData.h:611) a `NONE, HOLD, OCTAVE` — pas `PLAY_STOP`. Le plan §3.A prévoit de renommer `HOLD → PLAY_STOP`. C'est confirmé nécessaire par lecture du code. La procédure G0 step 3 dit "message arpPlayStop=23" — ce message sera dans `NvsManager.cpp` post-rename. OK.
- **`scaleRoleAtPad`** (KeyboardData.h:597) prend `(rootPads, modePads, chromaticPad, pad)` — 4 paramètres. Le plan §5.3.2 Patch 2 montre `arpRoleAtPad(_wkArpPlayStopPad, _wkOctavePads, pad)` — 3 paramètres après rename. Cohérent avec la signature actuelle `arpRoleAtPad(holdPad, octavePads, pad)`.
- **`_ccScreenDirty` non déclaré** (finding M-E1) : aucune déclaration dans §5.1.2 Patch 1. Créer ce member est obligatoire pour que `ToolPadRoles_Cc.cpp` compile.
- **Flash NVS au boot (G7 step 1)** : mécanisme non vérifié dans le code. Si la touche `e` au boot n'existe pas, step 1 est infaisable. Vérifier dans `main.cpp` en pré-exécution.
- **`saveAll()` ne sauve pas LoopPadStore** (B-E2) : confirmé par lecture du code réel `ToolPadRoles.cpp:366-415`. Le bug est réel, pas hypothétique.
