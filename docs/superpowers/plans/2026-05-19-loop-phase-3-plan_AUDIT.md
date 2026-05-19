# Audit adversarial — Plan Phase 3 LOOP

**Date audit** : 2026-05-19
**Plan audité** : [`2026-05-19-loop-phase-3-plan.md`](2026-05-19-loop-phase-3-plan.md) (2100 lignes)
**Posture** : STOP-and-find. Pas de complaisance — biais par défaut LLM = compliance, à combattre.
**Méthode** : relecture critique + cross-check sur le code existant (ToolPadRoles.cpp, ToolControlPads.cpp, SetupManager.cpp, NvsManager.cpp) pour valider les hypothèses du plan.
**Convention de sévérité** (cf SESSION_PROTOCOL.md §"Convention nommage audit-fix") :
- **B-N** Bloquant Nouveau : bug invariant-violating, doit être fixé avant commit phase.
- **M** Majeur : bug fonctionnel sérieux ou approximation qui mène à drift.
- **m** Mineur : optimisation, clean-up, clarification.

---

## Synthèse

| Catégorie | Count | Findings |
|---|---|---|
| B-N | 1 | B-N1 (Tool 3 begin signature change documenté mais call site précis manquant) |
| M | 7 | M1-M7 (refacto Tool 3 zones de survol, pool extension, flash infrastructure, drift swap-to-pool comportement, validator wire path, collision check emplacement, ARPEG existing behavior misrepresented) |
| m | 11 | m1-m11 (clarifications, précisions, ambiguïtés mineures) |

**Total** : 19 findings. Audit non-complaisant. Recommandation : intégrer les fix dans le plan avant exécution.

---

## Bloquants nouveaux (B-N)

### B-N1 — Plan ne mentionne pas le call site précis de `_toolRoles.begin()`

**Localisation** : Plan Task 8, Step 2.

**Constat** : le plan dit « grep `_padRolesTool.begin` ou `padRolesTool.begin` dans `SetupManager.cpp` pour adapter » et ne tranche pas. Le grep réel montre :
- `SetupManager.cpp:30` : `_toolRoles.begin(keyboard, leds, &_ui, ...)` — **1 seul call site**.
- L'instance est nommée `_toolRoles` (pas `_padRolesTool` comme le plan le suppose).

**Impact** : si l'exécutant suit verbatim le pattern de recherche du plan, il ne trouve rien. Risque de partir en exploration ou de manquer la modification du call site → la signature étendue de `begin()` casse le build.

**Fix** : Task 8 Step 2 doit explicitement nommer :
- Variable : `_toolRoles` (pas `_padRolesTool`).
- Fichier + ligne : `src/setup/SetupManager.cpp:30`.
- Action : ajouter `&_nvsManager` (ou pointer équivalent) en 4ème argument.

Adapter `SetupManager.h` ligne 9 ou équivalent pour s'assurer que `NvsManager` est accessible (probablement déjà via membre `_nvsManager` dans SetupManager).

---

## Majeurs (M)

### M1 — Plan **prétend ajouter** un swap-to-pool R3 ARPEG qui **existe déjà** (Task 15)

**Localisation** : Plan Task 15, Step 2.

**Constat** : le plan dit « Ajouter check collision intra-ctx avec swap-to-pool R3 ». Le code existant `ToolPadRoles.cpp` lignes 759-766 montre :

```cpp
// Steal silencieux : si le role est deja pris par un autre pad,
// on le libere directement sans demander confirmation.
uint8_t owner = findPadWithRole(_poolLine, _poolIdx);
if (owner < NUM_KEYS && owner != (uint8_t)pad) {
  clearRole(owner);
}
clearRole((uint8_t)pad);
assignRole((uint8_t)pad, _poolLine, _poolIdx);
```

→ le swap-to-pool **existe déjà** pour ARPEG roles, et il est **silencieux** (pas de flash). C'est le commentaire explicit du code (« Steal silencieux »).

**Impact** : Task 15 telle qu'écrite est ambiguë — soit no-op (le swap-to-pool fonctionne déjà), soit changement de comportement (ajout de flash msg sur évincement). Risque que l'exécutant duplique le mécanisme ou casse l'existing.

**Fix** : reformuler Task 15 :
- Section explicite « Le swap-to-pool ARPEG existe déjà silencieusement (cf ToolPadRoles.cpp:759-766). Phase 3.D ajoute un flash msg cosmétique au moment du swap pour informer l'user. Pas d'autre changement métier. »
- Verbatim du flash msg : `"Previous role returned to pool."` ou équivalent.
- Pattern d'insertion : modifier le if-block lignes 762-764 pour appeler `_setFlash` avant `clearRole(owner)`.

### M2 — Tool 3 n'a **pas** d'infrastructure `_setFlash` — Task 12 le mentionne sans tâche explicite

**Localisation** : Plan Task 12 (Step 2) + Tasks 15, 20, 24, 25 (qui dépendent de `_setFlash`).

**Constat** : grep `_setFlash` dans `ToolPadRoles.cpp` retourne 0. Tool 4 (`ToolControlPads.cpp:58-59`) a le pattern :
- `char _flashMsg[80]` + `uint32_t _flashExpireMs` membres.
- Helper `_setFlash(const char* msg)` (ligne ~108).
- Check `_flashActive()` + render dans `_draw()` ou `_drawControlBar()`.

Le plan dit « pattern à introduire si manquant » mais ne dédie pas de task à ça. C'est inséré dans Task 12 en passing.

**Impact** : si l'exécutant skip cette infrastructure, toutes les tâches suivantes (Tasks 12, 15, 20, 24, 25) qui dépendent du flash msg sont cassées. Critique pour UX collision feedback.

**Fix** : ajouter une **sous-task explicite** dans Task 11 (ou nouvelle Task 11.5) « Add `_setFlash` infrastructure to Tool 3 ». Verbatim code à fournir :

```cpp
// ToolPadRoles.h — add members
char     _flashMsg[80];
uint32_t _flashExpireMs;

// Helpers
void _setFlash(const char* msg);
bool _flashActive() const;

// ToolPadRoles.cpp
void ToolPadRoles::_setFlash(const char* msg) {
  strncpy(_flashMsg, msg, sizeof(_flashMsg) - 1);
  _flashMsg[sizeof(_flashMsg) - 1] = '\0';
  _flashExpireMs = millis() + 2500;  // 2.5s timeout, alignment Tool 4
}

bool ToolPadRoles::_flashActive() const {
  return _flashExpireMs > millis();
}
```

Render dans `drawControlBar()` ou `drawScreen()` selon le pattern Tool 4. Init `_flashMsg[0] = '\0'` + `_flashExpireMs = 0` dans constructor.

### M3 — Plan ne précise pas la **stratégie de stub** pour `_drawGridArpeg` / `_drawGridLoop` en Phase 3.C

**Localisation** : Plan Task 11, Step 2 (refacto `drawGrid`).

**Constat** : le plan dit « `_drawGridArpeg` et `_drawGridLoop` peuvent être stubs (appellent l'ancien `drawGrid` logic monolithique en attendant Task 13 et Task 17) ». **Comment** est ambigu :
- Option A : extraire l'ancien `drawGrid` body dans une fonction `_drawGridLegacy()` que `_drawGridArpeg/Loop` appellent.
- Option B : `_drawGridArpeg = no-op` (écran vide) pendant Phase 3.C, sous-page ARPEG/LOOP cassées entre Tasks 11 et 13-22.
- Option C : conditionner le code monolithique selon `_activeSubPage` dans une seule fonction `drawGrid()`.

**Impact** : Option B = non-régression Phase 3.C cassée (HW gate G2 inclut « TAB cycle nav » — si ARPEG est vide post-TAB, c'est un fail UX). Option A est la bonne mais le plan ne le précise pas.

**Fix** : Task 11 Step 2 doit explicitement choisir **Option A** :
- Extraire l'existing `drawGrid` body dans une nouvelle méthode privée `_drawGridLegacy()`.
- `_drawGridArpeg()` et `_drawGridLoop()` appellent `_drawGridLegacy()` comme stub Phase 3.C.
- `_drawGridNorm()` est la première vraie implémentation contextuelle.
- Tasks 13 et 18 remplaceront les stubs par les vraies implémentations.

Préciser même chose pour `_drawPoolArpeg/Loop` et `_drawPoolLegacy`.

### M4 — Plan **manque** une task pour étendre `POOL_LINE_COUNT` + `poolLineSize` dispatcher

**Localisation** : Plan Task 19 utilise `_poolLine == 6` (LOOP controls) et `_poolLine == 7` (LOOP slots). Plan Task 7 ne mentionne pas l'extension.

**Constat** : `ToolPadRoles.h:95` déclare `POOL_LINE_COUNT = 6` (0=clear, 1=bank, 2=root, 3=mode, 4=octave, 5=hold). Pour ajouter LOOP controls (line 6) et LOOP slots (line 7) :
- Étendre `POOL_LINE_COUNT` à 8.
- Étendre `poolLineSize(line)` switch case (cpp:95-105) pour gérer line 6 et 7.
- Étendre `poolItemLabel(line, index)` (cpp:106-120) pour les nouveaux labels.
- Étendre `findPadWithRole(line, index)` (cpp:173-194) pour scanner LoopPadStore.
- Étendre `assignRole(pad, line, index)` (cpp:195-215) pour écrire dans `_wkLoopPad`.
- Étendre `clearRole(pad)` (cpp:216-233) pour clear LoopPadStore entries.

C'est un travail substantiel non-dédié dans une task explicite.

**Impact** : sans cette extension, le pattern existing `clearRole(owner) + clearRole(pad) + assignRole(pad, line, index)` ligne 759-766 ne fonctionne **pas** pour LOOP. Tool 3 sous-page LOOP est cassé.

**Fix** : ajouter une **task explicite** dans Phase 3.E (avant Task 18) — « Task 17.5 — Extend pool dispatcher for LOOP lines (6 controls + 7 slots) ». Verbatim :

```cpp
// ToolPadRoles.h — bump
static const uint8_t POOL_LINE_COUNT = 8;   // 0=clear, 1=bank, 2=root, 3=mode, 4=octave, 5=hold, 6=LOOP-ctrl, 7=LOOP-slot
static const uint8_t POOL_LOOP_CTRL_COUNT = 3;
static const uint8_t POOL_LOOP_SLOT_COUNT = 16;

// ToolPadRoles.cpp — extend
uint8_t ToolPadRoles::poolLineSize(uint8_t line) const {
  switch (line) {
    case 1: return POOL_BANK_COUNT;
    case 2: return POOL_ROOT_COUNT;
    case 3: return POOL_MODE_COUNT;
    case 4: return POOL_OCTAVE_COUNT;
    case 5: return POOL_HOLD_COUNT;
    case 6: return POOL_LOOP_CTRL_COUNT;   // NEW Phase 3
    case 7: return POOL_LOOP_SLOT_COUNT;   // NEW Phase 3
    default: return 0;
  }
}
```

Idem pour `poolItemLabel`, `findPadWithRole`, `assignRole`, `clearRole`. Code complet à inclure dans la nouvelle task.

### M5 — Plan ne tranche pas l'emplacement précis du validator `validateLoopPadStore` wire

**Localisation** : Plan Task 17, Step 3.

**Constat** : le plan dit « Vérifier que `validateLoopPadStore()` est appelé après chaque `loadBlob` LoopPadStore. Si pas, ajouter l'appel. » sans préciser **où**.

Grep dans le code actuel — la déclaration n'existe pas encore (Phase 1 a déclaré la struct mais pas le validator avec defaults). Donc Task 17 introduit le validator + son call site.

**Impact** : si le call site n'est pas wired correctement, les defaults 30/31/32 ne sont jamais appliqués. Conséquence : LoopPadStore reste à 0xFF après boot factory NVS vide. Bank LOOP cassée car LoopEngine ne sait pas quel pad est REC.

**Fix** : Task 17 doit explicitement spécifier l'emplacement du call :
- Probablement dans `NvsManager::loadAll()`, après le `loadBlob` du LoopPadStore (lookup le code Phase 2 pour le point précis ; ex : `NvsManager.cpp` autour des autres validators).
- Cohérent avec le pattern des autres validators (`validateScalePadStore`, `validateArpPadStore`, `validateControlPadStore`).

Préciser via verbatim :

```cpp
// NvsManager.cpp loadAll() — section LoopPadStore
if (prefs.begin(LOOPPAD_NVS_NAMESPACE, true)) {
  size_t sz = prefs.getBytes(LOOPPAD_NVS_KEY, &_loadedLoopPad, sizeof(LoopPadStore));
  if (sz != sizeof(LoopPadStore) || _loadedLoopPad.magic != EEPROM_MAGIC) {
    // NVS invalid : leave to defaults via validator
    memset(&_loadedLoopPad, 0xFF, sizeof(LoopPadStore));
    _loadedLoopPad.magic = EEPROM_MAGIC;
    _loadedLoopPad.version = LOOPPAD_VERSION;
  }
  prefs.end();
}
validateLoopPadStore(_loadedLoopPad);   // <-- Phase 3 : applies defaults 30/31/32
```

Le validator (Step 2 Task 17) couvre déjà la logique. Le wire est l'ajout d'1 ligne. Préciser le fichier + zone.

### M6 — Plan ne tranche pas l'emplacement du collision check secondaire post-loadAll (Task 26)

**Localisation** : Plan Task 26, Step 6.

**Constat** : le plan dit « Dans `NvsManager::loadAll()` (à la fin, post-validators) ou dans `main.cpp` post-boot ». Ambigu.

**Impact** : si placé dans `main.cpp`, c'est dépendant du boot sequencing exact. Si placé dans `NvsManager::loadAll()`, c'est garanti tôt mais c'est un comportement métier dans NvsManager (logique discutable).

**Fix** : trancher **NvsManager::loadAll()** à la fin, juste avant le `return` ou la fin de la méthode. Cohérence avec le pattern de logging au boot. Aussi : ce check n'est pas critique runtime (warning Serial only) — accepter le couplage léger.

Mettre à jour Task 26 Step 6 pour spécifier exactement la zone d'insertion (e.g., « après le dernier validate*Store(), avant la fin de loadAll() »).

### M7 — Existing behavior change non-documenté : « Steal silencieux » → « Steal avec flash »

**Localisation** : Plan Task 15 (ARPEG), Task 20 (LOOP), Task 24 (cross-context dim).

**Constat** : le code Tool 3 actuel ligne 759-766 fait un **steal silencieux** intentionnel (commentaire explicit). Le plan introduit des flash msgs sur le steal sans déclarer ce changement.

**Impact** : changement de comportement UX visible. Si l'user était habitué au steal silencieux, le flash msg peut être perçu comme intrusif. Pas un blocker mais cohérence projet à maintenir.

**Fix** : déclarer explicitement dans le spec design Phase 3 §14 « Phase 3 introduit des flash msgs sur swap-to-pool pour améliorer la lisibilité UX. Le pattern silencieux d'origine est remplacé. » + commit message Phase 3.D / 3.E doit mentionner ce changement.

Spec design actuelle ne le dit pas — à ajouter en doc-sync Phase 3.H si pas avant.

---

## Mineurs (m)

### m1 — Helper `findBankIdxForPad` : forward-declare struct dans `.h` puis impl `.cpp`

**Localisation** : Plan Task 1 (declaration) + Task 2 (impl).

**Constat** : `struct BankSlot` forward declaration dans `KeyboardData.h` permet de déclarer la fonction mais pas de l'inline (car accès `.pad` impossible sans définition). Plan reconnaît implicitement en mettant l'impl dans `NvsManager.cpp`.

**Fix** : préciser dans Task 1 Step 2 que la déclaration est `extern` (ou simplement `int8_t findBankIdxForPad(const BankSlot* slots, uint8_t pad);` sans `inline`).

### m2 — Task 2 : choix du fichier d'impl ambigu

**Localisation** : Plan Task 2.

**Constat** : « OU si pas approprié : créer un mini-fichier `src/core/PadHelpers.cpp` avec juste cette fonction. » Indécis.

**Fix** : trancher **NvsManager.cpp** (déjà inclut BankManager.h via le projet, ou ajouter include) — cohérent avec retrait de `applyDevSeedLoopPadsIfSafe` même fichier.

### m3 — Task 3 : liste exacte des getters NvsManager requis

**Localisation** : Plan Task 3, Step 1.

**Constat** : le plan dit « grep getters existants » et « add missing ». Pas de liste fixée.

**Fix** : préciser la liste complète des getters requis :
- `const LoopPadStore& getLoadedLoopPadStore() const` (existe Phase 2).
- `const ScalePadStore& getLoadedScalePads() const` (à vérifier).
- `const ArpPadStore& getLoadedArpPads() const` (à vérifier).
- `const ControlPadStore& getLoadedControlPadStore() const` (à vérifier).
- Setter `setLoadedLoopPad(const LoopPadStore&)` + `saveLoopPad()` (Task 21, à ajouter).
- Accès `BankSlot[]` via API existing (probablement `getLoadedBankSlots()` ou équivalent).

Faire un grep + cocher avant Phase 3.A commit.

### m4 — Task 5 : confirmer no-op (padIndex non éditable post-add)

**Localisation** : Plan Task 5.

**Constat** : ToolControlPads.h:42 commentaire « 0-2 in VALUE_EDIT (CC/Channel/Deadzone) » — padIndex non éditable. Task 5 est sans doute no-op.

**Fix** : marquer Task 5 comme « confirmé no-op après audit ToolControlPads » + skip explicit OU laisser comme defensive check (commentaire dans le code).

### m5 — Task 6 : adapter le `continue` au pattern existing `_drawGrid` Tool 4

**Localisation** : Plan Task 6, Step 2.

**Constat** : le plan suppose une boucle for pad par pad avec `continue` skip. Vérifier que `_drawGrid` Tool 4 a effectivement cette structure.

**Fix** : Task 6 Step 1 (lecture) doit confirmer la structure de `_drawGrid` avant édition.

### m6 — Task 9 : TAB ASCII 0x09, code parser simple

**Localisation** : Plan Task 9, Step 2.

**Constat** : « si InputParser ne parse pas TAB → étendre » mais TAB est l'ASCII 0x09 standard, qui est probablement passé tel quel au parser.

**Fix** : préciser que TAB est un seul byte 0x09 (pas une séquence escape), facilement détectable. Vérifier rapidement le parser existing ; si pas déjà géré, ajouter 2-3 lignes.

### m7 — Task 10 : API SetupUI à vérifier (`setInverse`, `moveCursor`)

**Localisation** : Plan Task 10, Step 2.

**Constat** : le plan utilise `_ui->setInverse(true)` et `_ui->moveCursor(1, 1)`. SetupUI peut ne pas avoir ces noms exacts.

**Fix** : Task 10 Step 1 (read SetupUI.h) doit confirmer l'API. Adapter au pattern existing du projet (probablement `_ui->print` + couleurs VT100 brutes ou wrapper similar).

### m8 — Task 13/18 : ARPEG/LOOP cross-context dim marker — préciser visuel

**Localisation** : Plan Task 13 (ARPEG), Task 18 (LOOP).

**Constat** : code partiel «  ... render dim with marker like "·R" ... ». Pas verbatim complet.

**Fix** : verbatim complet pour les markers cross-context. Décision : `·R0..R6` / `·M0..M6` / `·Ch` / `·Hd` / `·O1..O4` pour roles ARPEG vu en LOOP. `·sN` pour slots LOOP vus en ARPEG. ASCII `.` si UTF-8 `·` pose problème (cf m9).

### m9 — UTF-8 vs ASCII `·` middle dot

**Localisation** : Plan Task 18 et Task 24.

**Constat** : `·` est UTF-8 (2 bytes : 0xC2 0xB7). Le terminal Python `vt100_serial_terminal.py` doit le supporter (probable). Mais le test VT100 raw du firmware (cells de 5-char width) peut être affecté si on assume 1 byte = 1 col.

**Fix** : tester le rendu UTF-8 dans le terminal Python. Fallback ASCII `.` si problème. Décider avant Phase 3.F.

### m10 — Task 20 : wiring `assignLoopRole` depuis le dispatcher

**Localisation** : Plan Task 20.

**Constat** : `assignLoopRole(pad, line, index)` introduit mais pas explicitement wired. Le path existing `assignRole(pad, line, index)` est appelé depuis `run()` ligne 766.

**Fix** : préciser dans Task 20 que `assignRole` est étendu avec un dispatch :

```cpp
void ToolPadRoles::assignRole(uint8_t pad, uint8_t line, uint8_t index) {
  if (_activeSubPage == SUB_LOOP && (line == 6 || line == 7)) {
    assignLoopRole(pad, line, index);
    return;
  }
  // ... existing logic for NORM (line 1) and ARPEG (lines 2-5) ...
}
```

Idem pour `clearRole` (dispatch vers `clearLoopRole` si pad est slot/control LOOP).

### m11 — Pattern P15 (TAB nav) — décision pré-exécution

**Localisation** : Plan Task 27, Step 4.

**Constat** : « Évaluer si pattern P15 mérite d'être ajouté ». Indécis.

**Fix** : décider **avant** Phase 3.H : OUI ajouter P15 si Tool 7 Phase 4 va aussi utiliser TAB nav (probable per spec §27 P4 et conventions Phase 3.C). Sinon : skip.

Reco : ajouter P15. Tool 7 utilisera le pattern aussi.

---

## Risques généraux Phase 3

### R1 — Refacto Tool 3 sans tests automatisés

Tool 3 actuel = 788 lignes monolithique. Refacto en 3 sous-pages = restructuration substantielle. Pas de tests automatisés C++ dans le projet — la non-régression dépend **uniquement** des HW gates G2/G3 manuels.

**Mitigation** : HW gates G2 (sous-page NORM) et G3 (sous-page ARPEG non-régression) sont structurés pour catcher les regressions critiques. Mais subtilités UX peuvent passer.

**Action** : préciser dans le manifeste session que l'exécutant doit tester chaque sous-page après chaque task substantielle (pas seulement à la fin de la phase).

### R2 — Drift entre code et spec design

Plan Phase 3 dépend du spec design Phase 3 + spec LOOP §5 + decisions §10 spec design. Si l'exécutant écrit du code qui s'écarte (par exemple oublie R4 inter-context coexist), la phase peut commiter du code subtilement faux qui passe les HW gates simples mais casse des edge cases live.

**Mitigation** : HW Gate G5 (Phase 3.F) liste EC1-EC9 scenarios explicit. Exécutant doit tester chaque.

**Action** : ajouter dans le manifeste un rappel « avant chaque HW gate, relire la matrice de compatibilité §12 spec design et confirmer les checks ».

### R3 — Validator + dev seed coupling

Phase 3.E (Task 17) ajoute le validator avec defaults. Phase 3.G (Task 26) retire le dev seed M7. Entre 3.E et 3.G, le validator + le dev seed coexistent et tous deux essaient d'appliquer 30/31/32 (Phase 2) vs 30/31/32 (Phase 3).

**Mitigation** : conflit cosmétique uniquement (mêmes valeurs). Trace boot double si DEBUG_SERIAL actif.

**Action** : préciser dans Task 17 commentaire code que le dev seed sera retiré 3.G — éviter confusion future.

---

## Findings à intégrer dans le plan avant exécution

**Critique (B-N + M1-M5)** — fix obligatoire avant commit Phase 3 start :

1. **B-N1** : Task 8 Step 2 — nommer `_toolRoles` + `SetupManager.cpp:30`.
2. **M1** : Task 15 reformuler « add flash on existing silent steal », pas « add swap mechanism ».
3. **M2** : ajouter task explicite `_setFlash` infrastructure Tool 3 avant Task 12.
4. **M3** : Task 11 trancher Option A (`_drawGridLegacy()` extracté + appelé par stubs).
5. **M4** : ajouter Task 17.5 « Extend pool dispatcher for LOOP lines 6 (controls) + 7 (slots) ».
6. **M5** : Task 17 préciser emplacement validator wire dans `loadAll()`.

**Recommandé (M6-M7 + m1-m11)** — ajouter pour propreté :

7. **M6** : Task 26 trancher emplacement collision check dans `loadAll()`.
8. **M7** : déclarer le changement de comportement « silent steal → flash steal » dans spec + commits.
9-19. **m1-m11** : clarifications inline.

**Aucun finding ne bloque structurellement** — Phase 3 reste exécutable avec les fix intégrés.

---

## Convention de doc-sync audit ↔ plan

Per project CLAUDE.md « Audits vivent à côté du doc audité, suffixés `_AUDIT`. Doc source et audit portent une petite note de cross-référence en header. »

**Action** : ajouter en header du plan Phase 3 une note :

```markdown
**Audit adversarial** : [`2026-05-19-loop-phase-3-plan_AUDIT.md`](2026-05-19-loop-phase-3-plan_AUDIT.md) (19 findings : 1 B-N + 7 M + 11 m).
Findings critiques (B-N1, M1-M5) intégrés dans les tasks avant exécution.
```

Et en header de ce doc audit, reprendre le pattern symétrique (déjà présent ligne 4).

---

**Audit complet 2026-05-19**. Pas de complaisance, 19 findings identifiés. Posture critique maintenue.
**Next** : intégrer fix B-N1 + M1-M5 dans le plan + manifeste session.
