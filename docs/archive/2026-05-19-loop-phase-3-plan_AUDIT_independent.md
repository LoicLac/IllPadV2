# Audit indépendant — Plan Phase 3 LOOP (sub-agent contradictoire)

> **ARCHIVÉ 2026-05-23** — Audit indépendant du plan caduc. Phase 3 LOOP refondue en Tool PAD ROLE. Voir [`tool-pad-role-design.md`](../../superpowers/specs/2026-05-23-tool-pad-role-design.md). Le contenu (B-N2, B-N3, M8, M14 critiques) reste pertinent comme leçons à appliquer au nouveau plan d'impl.

**Date** : 2026-05-19
**Plan audité** : [`2026-05-19-loop-phase-3-plan.md`](2026-05-19-loop-phase-3-plan.md)
**Audit auto précédent** : [`2026-05-19-loop-phase-3-plan_AUDIT.md`](2026-05-19-loop-phase-3-plan_AUDIT.md) (19 findings)
**Posture** : audit contradictoire indépendant, sans complaisance, mission de chasse aux défauts manqués + challenge des conclusions actées.
**Méthode** : sub-agent général-purpose modèle `opus`, lecture systématique du code réel + cross-check des hypothèses du plan.
**Verifs empiriques** : confirmées par l'auteur post-rapport sub-agent (cf §"Verifs empiriques" en fin).

---

## Synthèse

| Catégorie | Count | IDs |
|---|---|---|
| B-N additionnels | 2 | B-N2, B-N3 |
| M additionnels | 7 | M8-M14 |
| m additionnels | 6 | m12-m17 |
| Challenges audit v1 | 6 | upgrade M5→B-N3, upgrade m3→M8, upgrade m7→M14, conserve M1+M6+m1-m11, downgrade M3 reformuler |

**Verdict global** : plan **non-exécutable en l'état** même avec fix audit v1 intégrés. 3 bloquants (B-N1 v1 sous-spec + B-N2 + B-N3) causent build break ou invariant violation.

---

## Bloquants nouveaux (B-N)

### B-N2 — `struct BankSlot` ne contient pas de champ `pad` → helper `findBankIdxForPad` ne compile pas

**Localisation** : Plan §13 spec design + Plan Task 1 step 2 + Task 2 step 3.

**Constat** : la spec design §13 ligne 327 propose :
```cpp
inline int8_t findBankIdxForPad(const BankSlot* slots, uint8_t pad) {
  for (uint8_t i = 0; i < NUM_BANKS; i++) {
    if (slots[i].pad == pad) return (int8_t)i;
  }
  return -1;
}
```

**Vérification empirique** : `src/core/KeyboardData.h:369-379` montre la struct BankSlot avec 9 champs :
```cpp
struct BankSlot {
  uint8_t     channel;
  BankType    type;
  ScaleConfig scale;
  ArpEngine*  arpEngine;
  LoopEngine* loopEngine;
  bool        isForeground;
  uint8_t     baseVelocity;
  uint8_t     velocityVariation;
  uint16_t    pitchBendOffset;
};
```

**Pas de champ `pad`**. Le mapping bank→pad vit dans une variable locale `bankPads[NUM_BANKS]` dans `main.cpp:352`, passée par référence à `_toolRoles.begin()` et `s_bankManager.setBankPads()`.

**Impact** : build break à Phase 3.A Task 2 si exécutant suit verbatim. Bloquant.

**Fix** : signature correcte `int8_t findBankIdxForPad(const uint8_t* bankPads, uint8_t pad)`. Itération sur `bankPads[i]` (les pads physiques assignés aux 8 banks), pas sur `slots[i].pad`. Implémentation **inline** dans `KeyboardData.h` (pas besoin d'externalisation dans NvsManager.cpp comme proposé Task 2).

### B-N3 — Validator `validateLoopPadStore` non appelé sur branche else (NVS vide) → invariant 12 cassé après retrait dev seed

**Localisation** : Plan Task 17 step 3 + Task 26 retrait dev seed.

**Constat** : Plan Task 17 dit « Vérifier que `validateLoopPadStore()` est appelé après chaque `loadBlob` LoopPadStore ». **Vérification empirique** `NvsManager.cpp:1006-1022` :

```cpp
// === LoopPadStore (Phase 1 declared, Phase 2 loaded) ===
{
  LoopPadStore tmp;
  if (loadBlob(LOOPPAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY,
               EEPROM_MAGIC, LOOPPAD_VERSION, &tmp, sizeof(tmp))) {
    validateLoopPadStore(tmp);                    // line 1011 — branche IF
    _loadedLoopPad = tmp;
    #if DEBUG_SERIAL
    Serial.printf("[BOOT NVS] LoopPadStore loaded : ...\n", ...);
    #endif
  } else {
    #if DEBUG_SERIAL
    Serial.println("[BOOT NVS] LoopPadStore not found, using Phase 2 dev defaults (pads 32/33/34)");
    #endif
    // ← AUCUN validator, _loadedLoopPad reste init state (probable 0xFF)
  }
}
```

**Impact** : aujourd'hui Phase 2 la branche else est couverte par `applyDevSeedLoopPadsIfSafe()` (Task 26 le retire). Sans validator sur la branche else **après retrait du dev seed**, `_loadedLoopPad.recPad` reste à son init state (0xFF probable) au boot factory NVS vide. Invariant 12 (recPad/PS/CLEAR jamais 0xFF runtime) cassé. HW gate G6 (« boot factory NVS vide → defaults appliqués → bank LOOP runtime OK ») **fail**.

**Fix** : Plan Task 17 step 3 et Task 26 doivent expliciter que le validator est appelé **dans les deux branches**, OU que `_loadedLoopPad` est pré-init à 0xFF avant le `loadBlob` et le validator déplacé **hors du if** :

```cpp
// Pattern recommandé (Task 17 step 3 + Task 26)
{
  LoopPadStore tmp;
  // Pré-init aux sentinels 0xFF pour que validator puisse appliquer defaults
  memset(&tmp, 0xFF, sizeof(tmp));
  tmp.magic = EEPROM_MAGIC;
  tmp.version = LOOPPAD_VERSION;
  // reserved = 0 (cosmetic)
  tmp.reserved = 0;

  bool loaded = loadBlob(LOOPPAD_NVS_NAMESPACE, LOOPPAD_NVS_KEY,
                          EEPROM_MAGIC, LOOPPAD_VERSION, &tmp, sizeof(tmp));
  validateLoopPadStore(tmp);   // appelé toujours, applique defaults sur 0xFF
  _loadedLoopPad = tmp;

  #if DEBUG_SERIAL
  if (loaded) {
    Serial.printf("[BOOT NVS] LoopPadStore loaded : rec=%u playStop=%u clear=%u\n", ...);
  } else {
    Serial.printf("[BOOT NVS] LoopPadStore empty, defaults applied : rec=%u playStop=%u clear=%u\n", ...);
  }
  #endif
}
```

---

## Majeurs (M)

### M8 — Getters NvsManager `getLoadedScalePads()` / `getLoadedArpPads()` n'existent pas (audit v1 m3 sous-évalué)

**Localisation** : Plan Task 3 step 1-2 + helpers §13 spec.

**Constat** : Spec §13 propose helpers `scaleRoleAtPad(const ScalePadStore& s, uint8_t pad)` et `arpRoleAtPad(const ArpPadStore& s, uint8_t pad)`. Le plan Task 3 dit « audit getters existants... add missing ».

**Vérification empirique** : grep `_loadedScalePad` et `_loadedArpPad` dans `NvsManager.h/.cpp` → **0 résultats**. Les Scale/Arp pads ne sont **pas cachés** dans NvsManager.

Au lieu : `main.cpp:367` déclare `uint8_t rootPads[7], modePads[7]` comme **variables locales** + passe par référence à `NvsManager.loadAll(...)` et `s_scaleManager.setRootPads(...)`. NvsManager fait juste le load/save NVS via `loadBlob`, ne cache pas.

Idem pour `chromaticPad`, `holdPad`, `octavePads[]` (variables locales main.cpp).

**Impact** : Tasks 11, 13, 18, 20, 23 qui appellent `_nvs->getLoadedScalePads()` ou `_nvs->getLoadedArpPads()` → build break (méthode inexistante).

**Fix** : audit v1 m3 propose « ajouter getters manquants ». C'est **mauvaise solution** — créerait un cache redondant NvsManager qui ne sert qu'à Tool 3. Solution alignée architecture existing :

Refondre les helpers §13 pour prendre les **arrays managers** directement :
```cpp
inline ScaleRoleResult scaleRoleAtPad(const uint8_t* rootPads, const uint8_t* modePads,
                                       uint8_t chromaticPad, uint8_t pad);
inline ArpRoleResult arpRoleAtPad(uint8_t holdPad, const uint8_t* octavePads, uint8_t pad);
inline int8_t findBankIdxForPad(const uint8_t* bankPads, uint8_t pad);
```

Tool 3 a déjà ces arrays comme members (cf `ToolPadRoles.h:46-51`) — passé par référence dans `_toolRoles.begin()`. Aucun changement de signature `begin()` nécessaire pour Scale/Arp/Bank. Seul l'ajout du `NvsManager* _nvs` member (B-N1 v1) pour accéder à LoopPadStore + ControlPadStore (qui SONT cachés en NvsManager).

### M9 — Drift valeur dev seed (32/33/34 Phase 2) vs Phase 3 defaults (30/31/32)

**Localisation** : spec §16, plan Task 17.

**Constat** : Phase 2 a livré `applyDevSeedLoopPadsIfSafe()` qui seed pads 32/33/34. Phase 3 introduit defaults 30/31/32 dans validator. Le plan Task 26 retire la fonction Phase 2 ; Task 17 wire le validator Phase 3. Entre Task 17 (3.E) et Task 26 (3.G), les deux mécanismes coexistent :

- Si NVS existant Phase 2 a recPad=32, PS=33, CLEAR=34 (assigné via dev seed), validator Phase 3 ne re-applique pas defaults (recPad != 0xFF).
- Si user reset NVS entre 3.E et 3.G, validator applique 30/31/32. Mais dev seed va aussi essayer si appel encore en place.

**Impact** : cosmétique uniquement (mêmes pads logiques disponibles). Trace boot double si DEBUG_SERIAL. Pas un bug fonctionnel.

**Fix** : commentaire dans Task 17 expliquant la coexistence transitoire entre 3.E et 3.G + plan d'ordre stable (Task 17 active validator avant Task 26 retire dev seed — pas l'inverse). Ou bien : déplacer Task 26 avant Task 17 (refacto séquence). Reco : conserver ordre, doc commentaire.

### M10 — Dispatch `assignRole → assignLoopRole` brise pattern existing ARPEG swap

**Localisation** : Plan addendum v1 M4 override (Task 17.5 step 6).

**Constat** : audit v1 M4 propose extension `assignRole` :
```cpp
} else if (line == 6) {
  assignLoopRole(pad, line, index);   // route to LOOP-specific handler
} else if (line == 7) {
  assignLoopRole(pad, line, index);
}
```

**Problème** : pour ARPEG (line 2-5), le swap-to-pool est fait dans `run()` ligne 759-766 **avant** l'appel `assignRole`. Pour LOOP (line 6-7), le swap-to-pool est fait **dans** `assignLoopRole`. Pattern **dual** :
- ARPEG : caller (`run()`) responsible du swap, callee (`assignRole`) juste écrit.
- LOOP : caller juste appelle, callee (`assignLoopRole`) responsible du swap.

**Impact** : drift conceptuel. Future maintenance peut casser l'invariant en oubliant qui fait le swap.

**Fix** : 2 options :
- (a) Unifier : `assignLoopRole` SOLE responsible du swap intra-LOOP, et `run()` ne fait PAS de swap pré-appel pour line 6-7. Cohérent : pour LOOP le swap est interne. **Pas de changement requis dans `run()` 759-766 pour ARPEG.**
- (b) Refacto : déplacer le swap-to-pool ARPEG dans `assignRole` aussi, retirer du `run()`. Plus de symétrie mais refacto plus large.

Reco : (a). Documenter dans `assignLoopRole` que swap intra-LOOP est inclus.

### M11 — Tool 3 `saveAll()` retourne bool — comportement partial-fail non spécifié

**Localisation** : Plan Task 21 step 2 + invariant 13 spec.

**Constat** : Plan Task 21 étend `saveAll()` pour persister `_wkLoopPad` via `_nvs->saveLoopPad()`. La fonction `saveAll` actuelle retourne bool (cf signature ToolPadRoles.h:87). Le pattern existing ligne 753-769 :
```cpp
if (saveAll()) {
  _ui->flashSaved();
  _editing = false;
}
```

Si l'extension Task 21 a un save séquentiel (bank → scale → arp → loop), un fail partiel laisse NVS dans état **incohérent** (bank saved, loop pas saved → user voit nouveau bank pad mais pas nouveau LOOP control).

**Impact** : invariant 13 (max 3 rôles coexistent par pad) peut briefly violer si saveAll partial-fail entre deux modifs user. Peu probable en pratique (NVS rare-fail) mais à anticiper.

**Fix** : `saveAll` doit être **atomique** ou **idempotent**. Options :
- (a) Pre-commit validation : check working state valide avant aucun save.
- (b) Rollback explicit : si un save fail, restaurer les writes précédents.
- (c) Best-effort + log : accepter partial-fail, trace serial warning.

Reco : (c) avec warning. Setup mode = rare-action, NVS sain en pratique. Mais documenter dans Task 21 le comportement.

### M12 — Hard-constraint exit incomplet : `clearAllRoles()` doit aussi être protégée

**Localisation** : Plan Task 20 step 3 + spec §15.

**Constat** : Plan Task 20 ajoute hard-constraint exit (refuse exit si LOOP controls == 0xFF). Mais le code existing `ToolPadRoles.cpp:234-242` `clearAllRoles()` :
```cpp
void ToolPadRoles::clearAllRoles() {
  memset(_wkBankPads, 0xFF, sizeof(_wkBankPads));
  memset(_wkRootPads, 0xFF, sizeof(_wkRootPads));
  memset(_wkModePads, 0xFF, sizeof(_wkModePads));
  _wkChromPad = 0xFF;
  _wkHoldPad = 0xFF;
  memset(_wkOctavePads, 0xFF, sizeof(_wkOctavePads));
}
```

Si Phase 3 étend `clearAllRoles()` pour inclure `_wkLoopPad` (cohérent avec scope), cela cassera l'invariant 12 (REC/PS/CLEAR jamais 0xFF). User confirme `clearAllRoles` (via confirmation y/n existing) → LOOP controls deviennent 0xFF → invariant violé jusqu'à ce que user les ré-assigne.

**Impact** : path d'attaque user. Si user fait clearAllRoles + exit immédiat, LOOP controls 0xFF en NVS. Reboot → validator applique defaults 30/31/32. OK en pratique grace au validator. Mais entre clearAllRoles et reboot, l'état working est incohérent.

**Fix** : `clearAllRoles()` Phase 3 doit **préserver** les 3 LOOP controls (ne pas les reset à 0xFF). Slots OK reset. Verbatim à ajouter Task 20 :
```cpp
void ToolPadRoles::clearAllRoles() {
  // ... existing memsets for ARPEG roles ...

  // Phase 3 : LOOP controls invariant 12 — never clear
  // Slots OK to clear (tolerate 0xFF)
  memset(_wkLoopPad.slotPads, 0xFF, sizeof(_wkLoopPad.slotPads));
  // recPad / playStopPad / clearPad preserved
}
```

### M13 — B-N1 audit v1 sous-spec : member `_nvs` Tool 3 absent + propagation

**Localisation** : Plan addendum v1 B-N1 override (Task 8 step 2).

**Constat** : audit v1 B-N1 spécifie le call site `_toolRoles.begin()` (SetupManager.cpp:30) mais omet :
1. Ajouter `NvsManager* _nvs` member dans `ToolPadRoles.h`.
2. Init `_nvs(nullptr)` dans constructor init list.
3. Assigner `_nvs = nvs` dans `begin()` body.
4. Vérifier que SetupManager a accès à un pointeur NvsManager (membre `_nvsManager`).

**Impact** : sans ces 4 éléments dérivés, le simple changement de signature ne compile pas.

**Fix** : étendre B-N1 verbatim pour inclure les 4 dérivés.

### M14 — SetupUI API `setInverse` / `moveCursor` n'existent pas (audit v1 m7 sous-évalué)

**Localisation** : Plan Task 10 step 2.

**Constat** : Plan Task 10 utilise `_ui->setInverse(true)` et `_ui->moveCursor(1, 1)`. **Vérification empirique** : `SetupUI.h:147,165` montre l'API réelle :
```cpp
void drawFrameLine(const char* fmt, ...);
void drawCellGrid(GridMode mode, ...);
```

`setInverse` et `moveCursor` n'existent pas. Le rendu inverse est fait via **inline VT100 escapes** dans le format string (cf `ToolPadRoles.cpp:339` : `VT_CYAN VT_BOLD "> " VT_RESET`).

**Impact** : build break Task 10. Audit v1 m7 a signalé l'absence à vérifier mais en sévérité m → sous-évalué.

**Fix** : refondre Task 10 step 2 verbatim pour utiliser `drawFrameLine` + VT100 escapes :
```cpp
void ToolPadRoles::_drawSubPageHeader() {
  const char* labels[SUB_COUNT] = { "NORM", "ARPEG", "LOOP" };
  char buf[128]; int pos = 0;
  pos += snprintf(buf + pos, sizeof(buf) - pos, "Pad Roles  [");
  for (uint8_t i = 0; i < SUB_COUNT; i++) {
    if (i == _activeSubPage) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, VT_REVERSE VT_BOLD "%s" VT_RESET, labels[i]);
    } else {
      pos += snprintf(buf + pos, sizeof(buf) - pos, VT_DIM "%s" VT_RESET, labels[i]);
    }
    if (i < SUB_COUNT - 1) pos += snprintf(buf + pos, sizeof(buf) - pos, "|");
  }
  pos += snprintf(buf + pos, sizeof(buf) - pos, "]");
  _ui->drawFrameLine("%s", buf);
}
```

---

## Mineurs (m)

### m12 — Grid labels alignement 4-char width

**Constat** : labels existing `GRID_HOLD_LABELS = { " Hld" }` = 4 chars avec leading space (alignement 5-char cell). Plan propose `"Hd"`, `"REC"`, `"PS_"`, `"CLR"`, `"S00"` (3-4 chars sans alignment).

**Fix** : utiliser ` Hld`, ` REC`, ` PS_`, ` CLR`, ` S00..S15` (leading space). Cohérent avec pattern existing.

### m13 — Réutiliser constants `GRID_*_LABELS` existing

**Constat** : Tasks 13, 18 redéfinissent des labels en inline (ex `"Hd"`, `"Oc"`) au lieu de réutiliser `GRID_HOLD_LABELS[0]` et `GRID_OCTAVE_LABELS[i]`.

**Fix** : réutiliser les constants existants. Cohérent avec convention §3 spec (« noms code conservés »).

### m14 — Pattern `buildRoleMap` factorisation préférable à `drawGrid` extraction

**Constat** : Audit v1 M3 propose `_drawGridLegacy()` extraction. Mais `drawGrid()` Tool 3 actuel est 3 lignes (`ToolPadRoles.cpp:314-318`) — il dispatch simplement `_roleMap[NUM_KEYS]` qui est construit par `buildRoleMap()` (cpp:121-148). Le vrai factorisation doit cibler `buildRoleMap` :
```cpp
void buildRoleMap() {
  switch (_activeSubPage) {
    case SUB_NORM:  _buildRoleMapNorm();  break;
    case SUB_ARPEG: _buildRoleMapArpeg(); break;
    case SUB_LOOP:  _buildRoleMapLoop();  break;
  }
}
```

**Fix** : reformuler M3 override : factorisation dans `buildRoleMap` au lieu de `drawGrid`. Plus aligné avec l'architecture existing.

### m15 — UTF-8 `·` vs ASCII `.` pour cross-context dim marker

**Constat** : Plan Task 18, 24 utilisent `·` (UTF-8). Terminal Python `vt100_serial_terminal.py` doit supporter UTF-8 (probable) mais cells VT100 5-char width assument 1 byte = 1 col.

**Fix** : trancher **ASCII `.`** pour cohérence. UTF-8 `·` est 2 bytes mais 1 col affiché — peut casser alignement si le rendu compte les bytes.

### m16 — Tool 3 `_setFlash` render emplacement

**Constat** : audit v1 M2 propose render flash dans `drawControlBar`. Mais overwrite control bar = UX dégradée (perte de hints clavier).

**Fix** : ligne flash dédiée appelée entre `drawInfoPanel()` et `drawControlBar()`. Pattern Tool 4.

### m17 — Hardconstraint defense in depth : tester sortie Tool 3 même si invariant tient

**Constat** : Tasks 20/22 testent hard-constraint exit seulement si LOOP control == 0xFF. Avec defaults validator Phase 3, ce cas est unreachable en pratique. Le test est défensif uniquement.

**Fix** : documenter dans Task 22 HW gate G4 que le test « hard-constraint exit » est défense en profondeur, peut être skippé si pas reproductible.

---

## Challenges audit v1

| ID audit v1 | Action | Justification |
|---|---|---|
| B-N1 | **conserver mais étendre** (M13) | Spécification incomplète, manque 4 dérivés (member, init list, begin assign, vérif SetupManager) |
| M1 | **conserver** | Bien calibré : flash sur silent steal existing |
| M2 | **conserver** + ajustement render (m16) | Bien calibré, juste raffiner emplacement render |
| M3 | **DOWNGRADE et REFORMULER** (m14) | Factorisation doit cibler `buildRoleMap`, pas `drawGrid` |
| M4 | **conserver** + ajustement M10 | Bien calibré sur le besoin d'extension pool dispatcher, mais le `assignRole → assignLoopRole` dispatch est dissonant (M10) |
| M5 | **UPGRADE → B-N3** | Pas juste « préciser emplacement », le validator ne s'applique PAS du tout sur la branche else |
| M6 | **conserver** | Bien calibré : NvsManager::loadAll() à la fin |
| M7 | **merger dans M1** | Doublon |
| m1 | **conserver** | OK, mais B-N2 plus important |
| m2 | **conserver** | OK |
| m3 | **UPGRADE → M (devient M8)** | Getters Scale/Arp pads inexistants ≠ « audit + add missing », c'est un design issue |
| m4 | **conserver** | Confirmé no-op |
| m5 | **conserver** | OK |
| m6 | **conserver** | OK, TAB 0x09 simple |
| m7 | **UPGRADE → M (devient M14)** | API SetupUI fantôme, pas juste « à vérifier » |
| m8-m9 | **conserver** | OK, UTF-8 → ASCII fallback |
| m10 | **conserver + ajustement M10** | Dispatch idée OK, mais pattern dual à corriger |
| m11 | **conserver** | OK, P15 = oui |

---

## Verifs empiriques (post-rapport sub-agent)

L'auteur du plan a confirmé les 4 claims critiques du sub-agent par grep direct sur le code :

| Finding | Verif | Résultat |
|---|---|---|
| B-N2 (struct `BankSlot::pad`) | `grep struct BankSlot` → KeyboardData.h:369-379 | ✓ Confirmé : pas de champ `pad` (9 champs : channel, type, scale, arpEngine, loopEngine, isForeground, baseVelocity, velocityVariation, pitchBendOffset) |
| B-N3 (validator branche else) | Read NvsManager.cpp:1006-1022 | ✓ Confirmé : validator dans branche IF uniquement (ligne 1011), branche ELSE (ligne 1017) couverte par dev seed M7 (à retirer Task 26) |
| M8 (getters Scale/Arp) | `grep _loadedScalePad\|_loadedArpPad NvsManager.h` | ✓ Confirmé : 0 résultats. Variables locales main.cpp:367 (rootPads/modePads) |
| M14 (SetupUI API) | `grep moveCursor\|setInverse SetupUI.h` | ✓ Confirmé : 0 résultats. API existing : `drawFrameLine` (l.147), `drawCellGrid` (l.165) |

---

## Recommandation finale

**Patcher le plan AVANT session EXEC** via option (c) : refondation spec design §13 + addendum v2 plan.

### Patches critiques (B-N) — bloquent build / invariant

1. **B-N2** : spec design §13 + plan Task 1 → signature `findBankIdxForPad(const uint8_t* bankPads, uint8_t pad)`.
2. **B-N3** : plan Task 17 step 3 → validator dans les deux branches via pré-init 0xFF + appel hors du if.
3. **B-N1 (étendu via M13)** : plan Task 8 step 2 → 4 dérivés (member `_nvs`, init list, begin assign, SetupManager vérif).

### Patches majeurs (M)

4. **M8** : spec design §13 + plan Tasks 3, 11, 13, 18, 20 → helpers prennent arrays au lieu de stores ; Task 3 retire « add getters Scale/Arp » (inutile).
5. **M9** : plan Task 17 commentaire transitoire dev seed.
6. **M10** : plan addendum v1 M4 override → unifier swap-to-pool intra-LOOP dans `assignLoopRole` (option a) ou refacto `assignRole` (option b).
7. **M11** : plan Task 21 → documenter comportement partial-fail saveAll.
8. **M12** : plan Task 20 → préserver les 3 LOOP controls dans `clearAllRoles()`.
9. **M13** : étendre B-N1 audit v1 (déjà dans patches critiques #3).
10. **M14** : plan Task 10 step 2 → refonte verbatim avec `drawFrameLine` + VT100 escapes.

### Patches mineurs (m)

11-15. m12-m17 : ajustements alignement labels, pattern factorisation `buildRoleMap`, ASCII fallback, render flash dédié, doc hard-constraint defense in depth.

### Estimation effort

~2h PREP supplémentaire. 1-2 commits patches sur :
- Spec design (§13 + §22 + §2 hors scope clarifier no cache NvsManager).
- Plan (addendum v2 cumulatif).
- Audit v1 (marquer upgrades).
- Manifeste / prompt EXEC (mineure update).

---

**Audit indépendant 2026-05-19**. Posture sans complaisance maintenue. **Plan non-exécutable en l'état**, patches critiques B-N2 + B-N3 + B-N1 étendu requis. Option (c) refondation spec design recommandée et actée.
