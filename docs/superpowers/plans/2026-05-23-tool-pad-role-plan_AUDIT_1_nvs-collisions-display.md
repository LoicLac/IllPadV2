# Audit 1 — NVS, Collisions, Affichage
## Plan Tool PAD ROLE — `2026-05-23-tool-pad-role-plan.md`

**Date audit :** 2026-05-24
**Auditeur :** Sous-agent indépendant (Audit 1)
**Cross-référence plan source :** `docs/superpowers/plans/2026-05-23-tool-pad-role-plan.md`
**Cross-référence spec source :** `docs/superpowers/specs/2026-05-23-tool-pad-role-design.md`
**Posture :** Critique non-complaisante. Double vigilance (travail LLM session antérieure).

---

## Périmètre couvert

| Axe | Scope |
|-----|-------|
| **Axe 1 — NVS** | Structs Store, versions, validators, NvsManager API, descripteurs, badge T3, zero-migration policy |
| **Axe 2 — Collisions** | Logique ABSORBANT×ABSORBANT, ABSORBANT×CONTEXTUEL modale, CONTEXTUEL×CONTEXTUEL swap, `_padNeighborInfo`, `_handleOverwriteModaleApply` |
| **Axe 3 — Visual collisions** | Affichage cellule §8.1, flash retrait §12.11, wording modale §10, info panel langue musicien |
| **Axe 4 — Pages** | Layout 4 pages BANK/ARPEG/LOOP/CC, grid 6×8, pool, control bar, `drawCellGrid` switch cases |

Fichiers lus (read-only) :

- `docs/superpowers/plans/2026-05-23-tool-pad-role-plan.md` (3677 lignes, intégral)
- `docs/superpowers/specs/2026-05-23-tool-pad-role-design.md` (744 lignes, intégral)
- `src/core/KeyboardData.h`
- `src/managers/NvsManager.h` + `NvsManager.cpp`
- `src/setup/ToolPadRoles.h` + `ToolPadRoles.cpp`
- `src/setup/SetupUI.cpp`
- `src/setup/SetupManager.h` + `SetupManager.cpp`
- `src/setup/ToolControlPads.h`
- `src/managers/BankManager.h`
- `src/managers/ScaleManager.h`
- `src/arp/ArpEngine.h`
- `src/main.cpp`

---

## AXE 1 — NVS et persistance

### Bloquants

**B1.1 — `saveAll()` ne persiste pas `LoopPadStore`**

- Fichier : `src/setup/ToolPadRoles.cpp:366–414`
- Citation : `saveAll()` sauvegarde `BankPadStore`, `ScalePadStore`, `ArpPadStore` — aucun appel `_nvs->saveBlob(12, ...)` pour `LoopPadStore`.
- Impact : Les HW gates G3 (Phase 3.D) et G4 (Phase 3.E) testent la page LOOP avec des modifications qui seront silencieusement perdues au reboot. L'utilisateur valide un comportement non persisté. Le plan identifie ce problème en §12.12 mais reporte le fix à 3.F.3 — les gates intermédiaires restent dans un état où le test HW ne valide pas ce qui comptera en production.
- Sévérité : **Bloquant pour les gates G3/G4** (validation HW sans persistance = validation d'un état fantôme).

**B1.2 — Phase 3.A intégralement non exécutée**

- `KeyboardData.h:523` : `#define ARPPAD_VERSION 2` — non bumped à 3.
- `KeyboardData.h:529` : `uint8_t holdPad;` dans `ArpPadStore` — non renommé en `arpPlayStopPad`.
- `KeyboardData.h:611` : `enum class ArpRoleKind : uint8_t { NONE, HOLD, OCTAVE };` — `PLAY_STOP` absent.
- `NvsManager.h:44–46` : signature `loadAll(uint8_t& holdPad, ...)` — non renommée.
- `NvsManager.cpp:932–946` : variables `holdPad`, `aps.holdPad` — legacy.
- `BankManager.h:38,67` : `setHoldPad()`, `_holdPad` — non renommés.
- `ScaleManager.h:33,53` : `setHoldPad()`, `_holdPad` — orphan link (plan §12.8 prescrit suppression en 3.A).
- `main.cpp:402–404,635–655` : `holdPad` variable, appels `setHoldPad()` — tous legacy.
- Impact : L'absence de bump de version `ARPPAD_VERSION 2→3` signifie que les données ArpPad NVS existantes (chargées avec `holdPad`) seront interprétées comme valides lors du premier boot post-implémentation 3.A — aucune migration ne sera déclenchée, le champ `holdPad` sera lu comme `arpPlayStopPad` sans avertissement. La zero-migration policy exige que le bump de version soit atomique avec le renommage du champ.
- Sévérité : **Bloquant** — absence de bump = violation directe de la zero-migration policy du projet.

### Runtime

**R1.1 — Descripteur 12 (LoopPadStore) hors de toutes les plages TOOL_NVS**

- Fichier : `src/core/KeyboardData.h:1013–1014`
- Citation : `TOOL_NVS_FIRST[] = { 0, 1, 2, 5, 6, 7, 8, 10 }`, `TOOL_NVS_LAST[] = { 0, 1, 4, 5, 6, 7, 9, 11 }` — T3=[2..4].
- Descripteur 12 : `KeyboardData.h:1007`, commentaire "T3 LOOP" — mais la plage T3 couvre [2..4] (BankPad=2, ScalePad=3, ArpPad=4). Le descripteur 12 est hors plage.
- Impact : `printMainMenu()` dans `SetupUI.cpp:384–431` itère sur `TOOL_NVS_FIRST[t]..TOOL_NVS_LAST[t]` pour calculer le badge santé NVS du Tool 3. Le LoopPadStore ne sera jamais vérifié par ce badge — une corruption ou version mismatch de LoopPad passera silencieusement sans marquer T3 en rouge.
- Le plan §12.6 identifie ce problème et propose un check ad-hoc ("vérification standalone du descripteur 12"), mais ne spécifie pas le code exact ni la phase exacte du patch.
- Sévérité : **Runtime** — le badge de santé NVS est trompeur post-implémentation.

**R1.2 — `applyDevSeedLoopPadsIfSafe()` toujours active en production**

- Fichier : `src/managers/NvsManager.cpp:1196–1230` + `src/main.cpp:410`
- Citation : `s_nvsManager.applyDevSeedLoopPadsIfSafe();` — appel actif au boot.
- Impact : En production, cette fonction écrase les LoopPads NVS de l'utilisateur par des seeds hardcodés si certaines conditions sont réunies. Le plan §12.H.2 prescrit la suppression, mais tant qu'elle n'est pas exécutée, tout reboot en production peut écraser silencieusement la configuration LOOP de l'utilisateur.
- Sévérité : **Runtime** (données utilisateur silencieusement écrasées au boot).

### Incohérences plan ↔ spec ↔ code

**I1.1 — Spec §13.2 dit "bump de 1 à 2", code est déjà à v2**

- Fichier spec : `docs/superpowers/specs/2026-05-23-tool-pad-role-design.md` §13.2
- Fichier code : `src/core/KeyboardData.h:523` — `#define ARPPAD_VERSION 2`
- Le plan identifie correctement que le bump cible est 2→3. La spec est une version stale qui dit "1→2". Divergence spec ↔ plan sur le numéro de version cible.
- Impact : Quiconque lit la spec en référence pour implémenter le bump bumpe de 1 à 2 (no-op sur une base déjà à 2, zero-migration non déclenchée).

**I1.2 — `ScaleManager::_holdPad` orphan link non résolu**

- Fichier : `src/managers/ScaleManager.h:33,53`
- Le champ `_holdPad` dans `ScaleManager` est setté (`setHoldPad()`) mais n'est jamais lu (le scale manager n'a aucun usage du hold pad). Le plan §12.8 prescrit la suppression en 3.A. En l'état : code mort qui respecte la forme d'invariant 7 (Setup/Runtime coherence) mais sans substance — orphan link légitimement identifié.
- Impact : Sans suppression, le renommage de `setHoldPad()` en 3.A créera une signature orpheline dans le runtime qui appellera `scaleManager.setAirplostopPad()` (ou équivalent) sur une cible qui ne fait rien.

### Éléments vérifiés OK — Axe 1

- `validateArpPadStore()` (`KeyboardData.h:843`) : bounds check `holdPad >= NUM_KEYS` → reset à 23 — logique correcte, sera applicable au champ renommé après 3.A.
- `NVS_DESCRIPTORS[]` : 13 entrées (0–12), structure correcte. La convention namespace/key est cohérente avec les patterns existants.
- Zero-migration policy : le mécanisme `loadBlob()` avec comparaison taille/version est implémenté correctement dans NvsManager — la politique est bien supportée par l'infrastructure, le problème est uniquement l'absence de bump en 3.A.
- Propagation §6.6 (working copies en mémoire, commit unique à la sortie) : pattern cohérent avec le code existant de ToolPadRoles.

---

## AXE 2 — Logique de collision

### Bloquants

*Aucun bloquant strict sur l'axe collisions — le code actuel est pré-implémentation (placeholders attendus). Les items suivants sont des incohérences plan ↔ plan ou plan ↔ spec qui bloqueront l'implémentation si non résolus.*

### Incohérences plan ↔ spec ↔ code

**I2.1 — Contradiction interne plan : flash retrait §12.11 vs HW Gate G2 §5.4**

- Fichier plan : `2026-05-23-tool-pad-role-plan.md`
- §12.11 (M4, "M14 v4") : "Flash retrait dès 3.C.2 — retirer le flash sur refus LOOP control (REC/PS/CLR) introduit dans Tool 3 phase 3, car LOOP control sera ABSORBANT dans le nouveau Tool PAD ROLE."
- §5.4 HW Gate G2, step 5 : "Vérifier que le flash d'avertissement est préservé pour les pads LOOP REC/PS/CLR."
- Impact : G2 demande explicitement de valider que le flash est *présent*. §12.11 demande de le *retirer*. Un implémenteur qui suit G2 d'abord puis lit §12.11 doit revenir en arrière — ou pire, valide G2 avec flash présent (conforme G2) puis oublie §12.11. Critère de gate contradictoire avec micro-décision plan.

**I2.2 — Wording modale : spec §10.2 vs plan `_formatOverwriteWording`**

- Spec §10.2 : exemple de wording "ROOT A de ARPEG" (avec suffixe de page source).
- Plan `_formatOverwriteWording` (Phase 3.G) : génère "ROOT A" uniquement — le suffixe "de ARPEG" n'apparaît pas dans le template.
- Impact : Si l'implémenteur suit le plan, la modale affiche "ROOT A — ce pad perd ce rôle" sans indiquer la page source. L'utilisateur ne sait pas si ROOT A est un rôle ARPEG ou LOOP. La spec est plus informative. Divergence non résolue.

**I2.3 — Comportement ARPEG contextuel dans pool ENTER actuel : placeholder incorrect**

- Fichier : `src/setup/ToolPadRoles.cpp:888–920`
- Le handler actuel traite les ARPEG contextuels avec un `clearRole()` silencieux (steal sans modale). Ce n'est pas le comportement final (la modale doit s'afficher pour ABSORBANT×CONTEXTUEL). Le plan identifie cela implicitement (le code sera remplacé par les nouvelles pages), mais aucune note explicite dans le plan ne dit "ce placeholder est architecturalement incohérent avec la spec §7 matrice d'édition".
- Impact : Les tests intermédiaires (HW gates G1/G2) valident le comportement *avant* collision logic — les gates ne testent pas la collision matrix — mais si un test informel est fait sur le code intermédiaire, le comportement observable est différent de la spec finale. Risque de fausse validation.

### Éléments vérifiés OK — Axe 2

- La matrice de collision spec §7 (ABSORBANT×ABSORBANT refus silencieux, ABSORBANT×CONTEXTUEL modale bloquante, CONTEXTUEL×CONTEXTUEL swap intra-AC / coexistence inter-AC) est clairement spécifiée et non contredite par le plan dans ses sections finales (Phase 3.G).
- `_padNeighborInfo` (spec §5.3.2) : concept central bien défini dans la spec, retourne `{ hasAbsorbant, hasContextuels[], acMap }`. La logique de cross-store lookup via `NvsManager*` est architecturalement correcte (passage du pointeur `_nvs` à `begin()`).
- Swap-to-pool intra-AC §7.2 : logique de silent steal quand on réassigne un CONTEXTUEL dans la même AC — correctement spécifiée, pas de contradiction identifiée.
- Hard-constraint exit §6.5 : obligation 8 banks assignés pour quitter le tool — spécifié dans le plan, cohérent avec la logique existante de Tool 3.

---

## AXE 3 — Feedback visuel collisions

### Bloquants

*Aucun bloquant strict pré-implémentation. Les items suivants sont des code paths orphelins ou des incohérences qui se manifesteront à l'implémentation.*

### Incohérences plan ↔ spec ↔ code

**I3.1 — Case 6 dans `drawCellGrid` GRID_ROLES : code orphelin**

- Fichier : `src/setup/SetupUI.cpp` — switch GRID_ROLES
- Case 6 : `"Play/Stop"` avec couleur `BRIGHT_RED` — code existant déjà dans la base.
- Problème : `PadRoleCode` dans `ToolPadRoles.h:15–23` a : `ROLE_NONE=0, ROLE_BANK=1, ROLE_ROOT=2, ROLE_MODE=3, ROLE_OCTAVE=4, ROLE_HOLD=5, ROLE_COLLISION=0xFF`. Aucune valeur 6. Le case 6 de `SetupUI.cpp` est un code orphelin — aucun caller actuel ne peut l'émettre.
- Le plan §3.A prescrit de renommer `ROLE_HOLD=5` en `ROLE_PLAY_STOP=5` et d'ajouter `ROLE_CC=6`. Mais en l'état, le case 6 dans `SetupUI` ne correspond à rien dans l'enum.
- Impact : Post-implémentation 3.A, si `ROLE_CC=6` est ajouté et mappé sur le case 6 de `SetupUI`, le label sera "Play/Stop" pour les CC pads — incohérence silencieuse. Le label doit être "CC" ou "MIDI CC". Le case 6 actuel a été écrit pour le play/stop pad, pas pour les CC pads.

**I3.2 — Info panels HW gates : wording en anglais**

- Fichier plan : Phase 3.C §5.2 "HW Gate G1", §5.4 "HW Gate G2".
- Les descriptions de validation en gate utilisent de l'anglais technique ("verify that...", "check that...").
- Politique projet (`docs/reference/vt100-design-guide.md` + `.claude/CLAUDE.md`) : info panels en français, langue musicien.
- Impact : Si les exemples de wording des gates influencent le contenu final des info panels (copier-coller partiel par l'implémenteur), le résultat sera des panels incohérents en langue. Risque faible mais identifiable.

**I3.3 — Refus silencieux ABSORBANT×ABSORBANT : absence de définition "silencieux"**

- Spec §7 matrice : "ABSORBANT×ABSORBANT → refus silencieux (no-op)."
- Plan : confirme "refus sans flash, sans modale."
- Problème : aucune spec de feedback négatif minimal n'est définie — pas de blink LED, pas de message transitoire, rien. Pour un instrument live, un refus totalement invisible peut créer une confusion utilisateur (le pad ne réagit pas, l'utilisateur ne sait pas si la touche a été reçue).
- Ce n'est pas une incohérence plan ↔ spec (les deux disent la même chose), c'est une lacune de spec potentiellement intentionnelle mais non justifiée dans les documents.

### Stale

**S3.1 — Flash 3.B (LOOP control refus) : stale dans le code existant**

- Fichier : `src/setup/ToolPadRoles.cpp:888–920` — le handler contient un flash pour refus LOOP REC/PS/CLR.
- Plan §12.11 : "retirer dès 3.C.2."
- Ce flash est hors-spec pour le nouveau Tool PAD ROLE (les contrôles LOOP seront ABSORBANT → refus silencieux). Le code actuel exhibe le comportement inverse de ce que la spec finale requiert.
- Impact : Gate G2 (§5.4 step 5) demande de "valider que le flash est préservé" — critère stale par rapport à §12.11. (Cf. également I2.1.)

### Éléments vérifiés OK — Axe 3

- Flash infrastructure `_setFlash()` / `_flashActive()` / `_drawFlash()` : pattern calqué sur ToolControlPads.cpp:854, correctement déclaré dans `ToolPadRoles.h:132–134`. Buffer 80 chars, expire timestamp — cohérent.
- Modale spec §10 : wording "Ce pad est actuellement libre — voulez-vous lui assigner X ?" et variante écrasement — vocabulaire musicien, non technique. Correct à la spec.
- Affichage cellule spec §8.1 : 4 chars max, truncation, couleur par rôle (BANK=BLUE, ROOT=GREEN, MODE=CYAN, OCTAVE=YELLOW, PLAY_STOP=MAGENTA, CC=BRIGHT_RED, collision=RED) — palette cohérente et non contradictoire entre spec et plan.

---

## AXE 4 — Affichage pages

### Incohérences plan ↔ spec ↔ code

**I4.1 — `SubPage` enum : 3 valeurs, plan prescrit 4 pages**

- Fichier : `src/setup/ToolPadRoles.h:32–37`
- Citation : `enum SubPage { SUB_NORM=0, SUB_ARPEG=1, SUB_LOOP=2, SUB_COUNT=3 }`
- Plan Phase 3.C : 4 pages `BANK / ARPEG / LOOP / CC`. L'enum courant n'a pas `SUB_BANK` (remplace `SUB_NORM`) ni `SUB_CC`.
- État pré-implémentation attendu — mais le plan ne spécifie pas explicitement le renommage `SUB_NORM → SUB_BANK`. Il nomme la page "BANK" mais l'enum doit être mis à jour en cohérence. À clarifier : le plan prescrit-il `SUB_NORM=0` (kept) renommé en `SUB_BANK=0`, ou un enum entièrement refait ?

**I4.2 — Pool nav circulaire : phase de fix non décidée**

- Fichier plan : §12.5
- Citation : "Pool nav circulaire — à corriger en 3.F.1 ou 3.B" (libellé approximatif, sans décision).
- Impact : Si l'implémenteur de 3.B n'inclut pas ce fix (il n'est pas dans le tasklist de 3.B), et si l'implémenteur de 3.F ne le voit pas non plus (il n'est pas dans le tasklist de 3.F), le fix tombe dans un vide de responsabilité.

**I4.3 — `_applyDefaultsCc()` : body ambiguïsement spécifié**

- Fichier plan : Phase 3.C section CC page helpers.
- La spec de `_applyDefaultsCc()` dit alternativement "void _wkCc" ou "reuse `_resetAllCc()`" — deux approches non réconciliées dans le plan. L'implémenteur doit choisir sans guidance.

**I4.4 — `_drawInfoX()` bodies absents du plan**

- Fichier plan : Phase 3.D/3.E/3.F info panel sections.
- Référence : "cf chat iter 2 passe 3" — le contenu des info panels n'est pas dans le plan, il est délégué à une session de chat antérieure.
- Impact : Aucun implémenteur (humain ou agent) ne peut reconstruire le contenu des info panels depuis le plan seul. Les infos sont dans un contexte de session non persisté. Risque élevé d'incohérence de langue/ton si l'implémenteur improvise.

**I4.5 — `POOL_LINE_COUNT = 6` : non mis à jour pour les nouvelles pages**

- Fichier : `src/setup/ToolPadRoles.h:113`
- Citation : `static const uint8_t POOL_LINE_COUNT = 6;`
- Plan prescrit un pool étendu pour le Tool PAD ROLE fusionné (10 lignes ou plus selon les pages). La constante est stale.

**I4.6 — Fichiers split `.cpp` absents**

- Fichiers attendus : `ToolPadRoles_Bank.cpp`, `ToolPadRoles_Cc.cpp`, `ToolPadRoles_Arpeg.cpp`, `ToolPadRoles_Loop.cpp` — aucun n'existe.
- État pré-implémentation attendu — pas un finding en soi. Mais le plan doit spécifier explicitement quand ces fichiers sont créés (quelle phase) pour que le système de build PlatformIO les détecte. Non spécifié clairement dans le plan.

### Stale

**S4.1 — `_toolRoles` (SetupManager) : non renommé en `_toolPadRoles`**

- Fichier : `src/setup/SetupManager.h:45`
- Plan §12.7 : prescrit renommage en 3.A.
- État pré-implémentation attendu mais identifié pour traçabilité.

**S4.2 — `ToolControlPads` encore présent dans SetupManager**

- Fichier : `src/setup/SetupManager.h:46`, `SetupManager.cpp:36,98–108`
- `ToolControlPads _toolControlPads` et dispatch `case '4': _toolControlPads.run()` — sera supprimé en 3.C.1b.
- État pré-implémentation attendu.

### Éléments vérifiés OK — Axe 4

- `_drawSubPageHeader()` / `_handleTab()` / `_setFlash()` / `_flashActive()` / `_drawFlash()` : déclarés dans `ToolPadRoles.h:130–134`, infrastructure de base correcte.
- `drawCellGrid` switch GRID_CONTROLPAD (`SetupUI.cpp:547–556`) : cases 1-4 (MOM/LATCH/RET0/HOLD) cohérents avec l'enum `ControlMode` actuel. La couleur default=DIM pour les cases non assignés est correcte.
- Architecture split fichiers (pattern ToolControlPads.cpp → ToolPadRoles_X.cpp) : principe architecturalement sain et aligné avec la taille attendue du code fusionné.
- Propagation working copy → NVS (pattern §6.6) : design correct — un seul `saveAll()` en sortie de tool, pas de NVS writes intermédiaires.

---

## SYNTHÈSE GLOBALE

### Verdict par axe

| Axe | Bloquants | Runtime | Incohérences | Stale | Verdict |
|-----|-----------|---------|--------------|-------|---------|
| **1 — NVS** | 2 | 2 | 2 | — | Rouge — 2 bloquants actifs |
| **2 — Collisions** | 0 | 0 | 3 | — | Jaune — contradictions plan↔plan et plan↔spec |
| **3 — Visual** | 0 | 0 | 3 | 1 | Jaune — case 6 orphelin à risque |
| **4 — Pages** | 0 | 0 | 6 | 2 | Jaune — plusieurs lacunes de spec |

### Top 5 findings par impact

**#1 — B1.1 : `saveAll()` ne persiste pas `LoopPadStore`** (`ToolPadRoles.cpp:366–414`)
Les HW gates G3 et G4 valident un état fantôme. La validation hardware ne garantit pas la robustesse en production. Doit être résolu avant G3, pas en 3.F.3.

**#2 — B1.2 : Phase 3.A non exécutée — `ARPPAD_VERSION` non bumped** (`KeyboardData.h:523`)
Violation directe de la zero-migration policy. Un boot post-3.A avec données NVS existantes chargera `holdPad` comme `arpPlayStopPad` sans avertissement. Le champ est un seul octet à la même position — la corruption est silencieuse.

**#3 — R1.2 : `applyDevSeedLoopPadsIfSafe()` active en production** (`main.cpp:410`)
Risque d'écrasement des LoopPads utilisateur à chaque reboot sur conditions. Suppression prescrite en 3.H.2 mais aucun garde-fou intermédiaire.

**#4 — I2.1 : Contradiction interne plan — flash LOOP control : §12.11 vs HW Gate G2 §5.4**
Un critère de gate demande de valider la présence d'un comportement que §12.11 demande de supprimer. Résultats de gate G2 non fiables si le critère step 5 n'est pas corrigé.

**#5 — I3.1 : Case 6 `drawCellGrid` GRID_ROLES orphelin, label incorrect pour ROLE_CC**
Post-implémentation 3.A, si `ROLE_CC=6` est assigné, l'affichage SetupUI affichera "Play/Stop" pour les CC pads. Incohérence silencieuse d'affichage en production.

### Notes

- **R1.1 (descripteur 12 hors plages TOOL_NVS)** : le patch §12.6 est non spécifié (phase et code absents). À formaliser avant 3.F.3 pour que le badge T3 soit fiable dès le déploiement.
- **I4.4 (`_drawInfoX()` absents du plan)** : le renvoi à "chat iter 2 passe 3" est une lacune de spec documentaire réelle. Recommander de capturer ces contenus dans le plan avant l'implémentation des phases 3.D/3.E/3.F.
- **I2.3 (refus ABSORBANT×ABSORBANT totalement silencieux)** : lacune de spec potentiellement intentionnelle. Non bloquant si intentionnel, à confirmer.
- L'état général du code correspond à l'état pré-exécution attendu. Les bloquants identifiés ne sont pas des régressions — ils sont des préconditions d'implémentation manquantes qui créeront des problèmes si l'ordre des phases n'est pas respecté scrupuleusement (notamment : 3.A doit être complète et bumper ARPPAD_VERSION *avant* tout test HW ArpPad, et LoopPadStore doit être persisté *avant* G3).
