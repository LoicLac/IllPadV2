# Audit de cohérence — Spec LOOP révisée (2026-04-19, post Phase 0.1)

**Date** : 2026-04-20
**Auditeur** : session Claude read-only, indépendante de la session patch
**Scope** : 3 axes — cohérence interne spec, drift spec ↔ code `main`, dépendances Phase 1→6
**Sources auditées** :
- `docs/superpowers/specs/2026-04-19-loop-mode-design.md` (599 lignes, statut VALIDÉ)
- `docs/superpowers/specs/2026-04-19-led-feedback-unified-design.md` (compagnon LED)
- `docs/superpowers/specs/2026-04-20-tool8-ux-respec-design.md` (compagnon Tool 8)
- Code source `main` au commit `5a9697c` (principalement `src/core/*.{h,cpp}`, `src/setup/ToolLedSettings.cpp`, `src/setup/ToolLedPreview.*`, `src/managers/NvsManager.cpp`, `src/managers/PotRouter.{h,cpp}`)
- Références `docs/reference/{architecture-briefing, nvs-reference, setup-tools-conventions}.md`

**Posture** : non-complaisance active. L'auditeur part du principe que le patch de session précédente (commit `5a9697c`, qui a fait passer la spec de "brouillon" à "VALIDÉ") est potentiellement affecté par du biais de défense. Chaque claim `✅ DONE` a été vérifié par grep direct dans le code.

**Verdict global** : spec **techniquement publiable** pour plan Phase 1, mais **8 items à trancher** avant rédaction. Aucun bloquant runtime. 2 drifts réels entre spec Tool 8 et code (à corriger côté code ou côté spec). 1 pré-wiring incomplet (WAITING colorA hardcodé ARPEG). 5 ambiguïtés spec à clarifier. Les commits référencés existent tous.

**Mise à jour 2026-04-20** : **les 8 items ont été tranchés** en session brainstorming post-audit. Voir [spec LOOP §28 "Décisions pré-plan Phase 1"](../specs/2026-04-19-loop-mode-design.md#28--tranchage-des-8-questions-résiduelles-2026-04-20). Tous les findings Axe 1 et Axe 2 ayant une action ont été encodés dans la spec et les refs. **Statut final : prêt à rédaction plan Phase 1.**

---

## §1 — Résumé exécutif

### Top 5 findings

1. **[spec-code-drift]** `Tool 8 respec §9 Defaults` promet "ARPEG FG brightness 80 %" **et** "LOOP FG brightness 80 %" comme deux params distincts. Code : `LINE_LOOP_FG_PCT` lit/écrit `_lwk.fgArpPlayMax` — **le même field que LINE_ARPEG_FG_PCT**. Toucher "LOOP FG brightness" en Tool 8 modifie silencieusement ARPEG FG. Comment code avoue "(no separate field yet)". À trancher avant consommation runtime LOOP.
   → **Résolu Q4 / spec §28** : rename field `fgArpPlayMax` → `fgPlayMax`, déplacement ligne Tool 8 en TRANSPORT (ARPEG + LOOP shared explicite). Step Phase 1 dédié spécifié §27.

2. **[spec-code-drift]** `EVENT_RENDER_DEFAULT[EVT_WAITING]` hardcode `colorA = CSLOT_MODE_ARPEG` ([LedGrammar.cpp:30](src/core/LedGrammar.cpp:30)). LED spec §17 exige `colorA = CSLOT_MODE_LOOP` pour WAITING sur bank LOOP. Commentaire "colorB supplied by LOOP callsite" mais le code (LedController.cpp:575-576) hardcode aussi colorB sur `entry.colorSlot`. **Pré-wiring WAITING incomplet** pour LOOP — à compléter Phase 4 LED wiring (étendre `triggerEvent` ou scinder EVT_WAITING).
   → **Résolu Q3 / spec §28** : pas de scission, 1 event unique mode-invariant, colorA = `CSLOT_VERB_PLAY` vert (éditable Tool 8), colorB = `CSLOT_CONFIRM_OK` blanc (hardcodé triggerEvent), brightness = `fgArpStopMax` × bgFactor BG. Step Phase 1 dédié.

3. **[spec-contradiction]** `nvs-reference.md:107` annonce `LoopPadStore size=8B`, mais spec LOOP §20 exige 3 controls + 16 slots = 19 pads → 19B data + 4B header = **~24B minimum**. La taille 8B est incohérente avec le contenu prévu. Non-bloquant mais à corriger en début de Phase 1.
   → **Résolu Q1 / spec §28** : 23 B strict packed. nvs-reference.md corrigé. Spec §20 mise à jour.

4. **[spec-unclear]** §17 Table gestes concurrents traite EMPTY → REC → RECORDING (§7) et PLAYING → REC → OVERDUBBING (§8), mais **ne définit pas** ce que fait un `tap REC` depuis `STOPPED-loaded` (boucle chargée, non-WAITING). Trois interprétations possibles (re-record par-dessus ; overdub lancé directement ; ignoré). Gap à combler avant plan Phase 2.
   → **Résolu Q5 / spec §8** : STOPPED-loaded + tap REC → PLAYING + OVERDUBBING simultanés (option a).

5. **[spec-unclear]** §21 LOOP réfère à "**LED spec §4.3**" pour la période hardcoded 800ms WAITING. **La LED spec n'a pas de §4.3**. Le contenu cherché est dans **Tool 8 respec §4.3** "Éléments NON exposés". Renvoi cross-doc cassé, plusieurs fois dans le spec. Minor, à renuméroter.
   → **Résolu / spec §21** : renvoi corrigé vers "Tool 8 respec §4.3".

### Statistiques findings

| Sévérité | Axe 1 (interne) | Axe 2 (code) | Total |
|---|---|---|---|
| `spec-contradiction` | 1 | 1 | 2 |
| `spec-code-drift` | — | 3 | 3 |
| `spec-unclear` | 5 | 1 | 6 |
| `phase-ordering-risk` | 1 | — | 1 |
| `spec-ok` vérifiés | 12 | 10 | 22 |

---

## §2 — Axe 1 : Cohérence interne de la spec

### F1.1 — [spec-unclear] §21 renvoi cassé "LED spec §4.3"

**Extrait** (loop-mode-design.md §21) :
> **WAITING_*** : pattern `CROSSFADE_COLOR` entre `CSLOT_MODE_LOOP` (jaune) et `CSLOT_VERB_PLAY` (vert), period hardcoded 800 ms (LED spec §4.3). ✅ Pattern implémenté.

**Problème** : La spec LED (`2026-04-19-led-feedback-unified-design.md`) a une structure §1→§27 sans §4.3. Le contenu "WAITING period hardcoded 800 ms, constant dans renderPattern" se trouve dans `2026-04-20-tool8-ux-respec-design.md` §4.3 "Éléments NON exposés".

**Impact** : un lecteur qui suit le renvoi se perd. Non-bloquant. L'info factuelle (800ms hardcoded) est confirmée par le code ([LedController.cpp:651](src/core/LedController.cpp:651)).

**Action** : remplacer "LED spec §4.3" par "Tool 8 respec §4.3" ou par "LED spec §10 (palette CROSSFADE_COLOR)". Même renvoi à vérifier en §17 ("voir LED spec §10 / §17").

---

### F1.2 — [spec-unclear] Comportement REC depuis STOPPED-loaded non défini

**Extrait** :
- §7 (Enregistrer un premier loop) couvre EMPTY → tap REC → RECORDING.
- §8 (Overdub) couvre PLAYING → tap REC → OVERDUBBING.
- §17 Table gestes concurrents couvre WAITING_PLAY/STOP/LOAD + tap REC.

**Gap** : aucun passage ne dit ce qui arrive sur STOPPED-loaded + tap REC (boucle enregistrée, en pause, non-WAITING). Possibilités :
- (a) "Continuer à jouer là où on s'était arrêté + ajouter overdub" (= démarrer PLAYING + OVERDUBBING simultanément)
- (b) "Repartir en RECORDING à zéro" (effacement silencieux du contenu — incohérent avec invariant 4 "pas d'écrasement implicite")
- (c) "Ignoré" (le musicien doit passer par PLAY/STOP d'abord pour déclencher OVERDUBBING)

**Impact** : plan Phase 2 ne peut implémenter LoopEngine tant que ce comportement n'est pas tranché.

**Recommandation** : poser une ligne explicite dans §17 ou §9 : "STOPPED → tap REC → …".

---

### F1.3 — [spec-unclear] §17 "Tap PLAY/STOP dans doubleTapMs pendant WAITING_LOAD"

**Extrait** :
> | Tap PLAY/STOP **dans** doubleTapMs (double-tap bypass) | → PLAYING immédiat | → STOPPED immédiat | **Annule load + STOPPED immédiat (1er tap annule load, 2e tap = bypass)** |

**Problème** : La formulation décompose un double-tap (2 taps rapides, perçus comme 1 geste) en "1er tap annule load, 2e tap = bypass". Mais le double-tap est par définition **atomique** — le 1er tap seul déclencherait la ligne du dessous ("tap hors doubleTapMs"). La phrase actuelle suggère 2 taps discrets avec effets cumulés, ce qui contredit la sémantique double-tap.

**Impact** : ambiguïté d'implémentation. LoopEngine va-t-il buffer 2 taps pour les cumuler, ou va-t-il détecter un seul double-tap comme "annule load + STOP" ?

**Recommandation** : reformuler en "double-tap = annule load ET commit STOPPED immédiat (bypass quantize)".

---

### F1.4 — [spec-contradiction] §21 "Pattern implémenté" vs état réel du WAITING mode-aware

**Extrait** (§21) :
> **WAITING_*** : pattern `CROSSFADE_COLOR` entre `CSLOT_MODE_LOOP` (jaune) et `CSLOT_VERB_PLAY` (vert) … ✅ Pattern implémenté.

**Problème** : La claim "Pattern implémenté" est vraie pour le **pattern CROSSFADE_COLOR au sens moteur** ([LedController.cpp:788-800](src/core/LedController.cpp:788)), mais **pas** au sens "WAITING d'une bank LOOP produit le bon rendu". Le mapping `EVENT_RENDER_DEFAULT[EVT_WAITING]` hardcode `colorA = CSLOT_MODE_ARPEG` comme "placeholder" — voir F2.2 ci-dessous. Une bank LOOP qui trigger EVT_WAITING aujourd'hui afficherait un crossfade bleu→vert, pas jaune→vert.

**Impact** : la checkmark ✅ est techniquement correcte (moteur pattern opérationnel) mais sémantiquement trompeuse si le lecteur comprend "feedback LOOP WAITING prêt à l'emploi". Le câblage mode-aware arrive Phase 4 (spec §27 Phase 4 : "rendu `renderBankLoop` complet, mapping `EVT_LOOP_*` → patterns concrets").

**Recommandation** : préciser "Pattern implémenté ; colorA LOOP-specific câblée Phase 4 (cf §27)".

---

### F1.5 — [spec-unclear] §19 nomenclature HOLD_ON/HOLD_OFF obsolète

**Extrait** (§19) :
> Le feedback LED (confirm HOLD_ON / HOLD_OFF) s'applique sur le LED de la bank cible, pas sur le LED FG

**Problème** : `CONFIRM_HOLD_ON/OFF` a été **retiré** par la LED spec §12 : "CONFIRM_HOLD_ON / Remplacé par PLAY (même pattern, même event) / —". Les events actuels sont `EVT_PLAY` + `EVT_STOP`. Le spec LOOP §19 utilise encore la nomenclature pré-refactor.

**Impact** : cohérence documentaire. Un lecteur qui cherche `CONFIRM_HOLD_ON` dans le code récent ne trouve plus.

**Recommandation** : s/HOLD_ON / HOLD_OFF/PLAY / STOP/ dans §19 (et vérifier autres occurences).

---

### F1.6 — [phase-ordering-risk] Ambiguïté Phase 1 vs Phase 5 pour LoopPotStore

**Extrait** §20 + §27 :
> §20 : `LoopPotStore` ❌ TODO Phase 1-5 …
> §27 Phase 1 : "structs LoopPadStore / **LoopPotStore** + validators + NVS descriptors …"
> §27 Phase 5 : "Effets : shuffle (shared templates avec ARPEG), chaos … bloc pots LOOP complet."

**Problème** : "TODO Phase 1-5" laisse ambigu le découpage. Phase 1 déclare-t-elle le struct complet (layout NVS figé dès Phase 1 pour éviter 2 resets user), ou s'étale-t-elle sur 5 phases ?

**Impact** : Zero Migration Policy impose de figer le layout NVS le plus tôt possible. Un bump Phase 1 → Phase 5 du struct = 2 resets user.

**Recommandation** : rendre explicite "Phase 1 : struct déclaré + defaults + validator + descriptor (layout NVS final, pas de consommateur runtime). Phase 5 : câblage effets (shuffle/chaos/velpattern) consomme le store."

---

### F1.7 — [spec-unclear] §26 questions ouvertes non bloquantes mais impactantes

Trois items marqués "encore à trancher" :

- **`PendingEvent` factorisé ou dupliqué** entre ArpEngine et LoopEngine : impacte Phase 2 architecture.
- **SRAM pour 2 banks LOOP (18.8 KB)** : "à vérifier par mesure lors de Phase 2" — retour d'info post-implémentation, acceptable.
- **Max banks LOOP = 2** : confirmé §14 + §25 mais "à figer dans le plan" — redondant si spec VALIDÉ.

**Recommandation** : le premier item (`PendingEvent`) mérite une décision architecturale avant Phase 2 (impact signatures publiques). Les deux autres peuvent rester "à figer dans le plan Phase 2".

---

### F1.8 — [spec-unclear] §25 SRAM 2 banks vs "bank switch refusé pendant recording"

**Extrait** §25 : "SRAM ~18.8 KB (2 banks × 9.4 KB)"
**Extrait** §23 invariant 2 : "Bank switch refusé pendant RECORDING / OVERDUBBING"
**Extrait** §8 : "Bank switch refusé pendant RECORDING et OVERDUBBING. Le musicien doit clore avant de changer de bank."

**Cohérence** : OK sur le principe — 2 banks LOOP peuvent exister simultanément en PLAYING/STOPPED, recording est exclusif sur 1 bank à la fois.

**Question implicite non tranchée** : peut-on avoir 1 bank LOOP en RECORDING et 1 autre en PLAYING simultanément ? §14 le laisse entendre ("les autres banks LOOP en background continuent à jouer normalement") mais sans le dire explicitement. **Sévérité faible** — à préciser dans §14 : "une seule bank LOOP peut être en RECORDING/OVERDUBBING à un instant t ; les autres peuvent jouer en BG".

---

### F1.9 — [spec-ok vérifié] §20 `BankTypeStore v2` avec `scaleGroup[8]` est bien en place

Cohérent avec [KeyboardData.h:316-318](src/core/KeyboardData.h) (enum BankType = NORMAL/ARPEG seulement, BANK_LOOP annoncé Phase 1). Validé ✅.

---

## §3 — Axe 2 : Cohérence spec vs code sur `main`

### F2.1 — [spec-ok] ColorSlotStore v5 + 16 slots intégralement en place

Vérifié [KeyboardData.h:167,171-192,504](src/core/KeyboardData.h) :

| Slot (enum ID) | Code | Couleur preset par défaut |
|---|---|---|
| `CSLOT_MODE_NORMAL = 0` | ✅ | Warm White (preset 0) |
| `CSLOT_MODE_ARPEG = 1` | ✅ | Ice Blue (preset 3) |
| `CSLOT_MODE_LOOP = 2` | ✅ | Gold (preset 7) |
| `CSLOT_VERB_PLAY = 3` | ✅ | Green (preset 11) |
| `CSLOT_VERB_REC = 4` | ✅ | Coral (preset 8) |
| `CSLOT_VERB_OVERDUB = 5` | ✅ | Amber (preset 6) |
| `CSLOT_VERB_CLEAR_LOOP = 6` | ✅ | Cyan (preset 5) |
| `CSLOT_VERB_SLOT_CLEAR = 7` | ✅ | Amber+hue (preset 6) |
| `CSLOT_VERB_SAVE = 8` | ✅ | Magenta (preset 10) |
| `CSLOT_BANK_SWITCH = 9` | ✅ | Pure White (preset 0) |
| `CSLOT_SCALE_ROOT = 10` | ✅ | Amber |
| `CSLOT_SCALE_MODE = 11` | ✅ | Gold |
| `CSLOT_SCALE_CHROM = 12` | ✅ | Coral |
| `CSLOT_OCTAVE = 13` | ✅ | Violet |
| `CSLOT_CONFIRM_OK = 14` | ✅ | Pure White |
| `CSLOT_VERB_STOP = 15` | ✅ | Coral (Phase 0.1 respec) |

`COLOR_SLOT_VERSION = 5`, `COLOR_SLOT_COUNT = 16`. Alignement strict avec spec §21 et Tool 8 respec §7.1.

---

### F2.2 — [spec-code-drift] WAITING pré-wiring incomplet pour bank LOOP

**Citation code** ([src/core/LedGrammar.cpp:30](src/core/LedGrammar.cpp:30)) :
```cpp
/* EVT_WAITING           */ { PTN_CROSSFADE_COLOR, CSLOT_MODE_ARPEG,  100 },
  // placeholder colorA ; colorB supplied by LOOP callsite
```

**Citation code** ([src/core/LedController.cpp:571-576](src/core/LedController.cpp:571)) :
```cpp
_eventOverlay.patternId = entry.patternId;
_eventOverlay.fgPct     = entry.fgPct;
_eventOverlay.ledMask   = ledMask;
_eventOverlay.startTime = millis();
_eventOverlay.colorA    = colorForSlot(entry.colorSlot);
_eventOverlay.colorB    = _eventOverlay.colorA;  // used only by CROSSFADE_COLOR
```

**Spec LED §17 (ARPEG)** : `colorA = CSLOT_MODE_ARPEG, colorB = CSLOT_VERB_PLAY`.
**Spec LED §17 (LOOP)** : `colorA = CSLOT_MODE_LOOP, colorB = CSLOT_VERB_PLAY`.

**Problème** : le mapping default force `colorA = CSLOT_MODE_ARPEG` pour tous les triggers EVT_WAITING (ARPEG **et** LOOP). Le commentaire promet "colorB supplied by LOOP callsite" mais l'API `triggerEvent(EventId, uint8_t ledMask)` n'expose **aucun** override de couleur. `colorB` est même systématiquement écrasé par `colorA` à la ligne 576.

**Impact** : dès que Phase 2 LoopEngine émet un WAITING_LOAD/PLAY/STOP, l'affichage sera `CSLOT_MODE_ARPEG → CSLOT_MODE_ARPEG` (monochrome, pas un crossfade). De plus, la colorA sera bleu ARPEG, pas jaune LOOP. Rendu **incorrect** dès activation Phase 2, sans Phase 4 LED wiring.

**Action recommandée** (à trancher Phase 4) :
- Option (a) : étendre `triggerEvent` avec paramètres optionnels `(ColorSlotId colorA, ColorSlotId colorB)` pour override par callsite.
- Option (b) : scinder `EVT_WAITING` en `EVT_WAITING_ARPEG` (colorA=MODE_ARPEG) et `EVT_WAITING_LOOP` (colorA=MODE_LOOP). Option propre mais consomme 2 events (EVT_COUNT passe de 17 à 18).
- Option (c) : injecter `CSLOT_MODE_*` dans `_eventOverlay.colorB` depuis LedController::renderBankLoop/renderBankArpeg selon bank courante.

Option (b) la plus propre vis-à-vis du principe "un event = un rendu fixe".

---

### F2.3 — [spec-code-drift] LOOP FG brightness partage le field ARPEG

**Citation code** ([src/setup/ToolLedSettings.cpp:480](src/setup/ToolLedSettings.cpp:480)) :
```cpp
case LINE_LOOP_FG_PCT:  return _lwk.fgArpPlayMax; // LOOP reuses ARPEG FG pct (no separate field yet)
```

**Spec Tool 8 respec §9 Defaults** (extrait) :
```
| ARPEG FG brightness | 80 % |
| LOOP FG brightness  | 80 % |
```

Deux lignes distinctes dans les defaults. UI Tool 8 (§4.1 layout) expose `FG brightness` dans section ARPEG **et** dans section LOOP.

**Problème** : le field `LedSettingsStore::fgArpPlayMax` est unique. Les deux lignes UI lisent/écrivent la même variable. Si le musicien règle ARPEG à 70% puis LOOP à 50%, ARPEG bascule silencieusement à 50%.

**Impact** : moyenne. Invisible tant que Phase 1-2 ne déclenche pas de rendu LOOP (fgArpPlayMax n'est utilisé aujourd'hui que par `renderBankArpeg`). Deviendra trompeur dès que `renderBankLoop` (Phase 2/4) lira ce field.

**Action recommandée** (à trancher avant Phase 2) :
- Ajouter `uint8_t fgLoopIntensity` (+1 byte) dans `LedSettingsStore`, bump v7→v8, reset user (acceptable Zero Migration Policy). Mettre à jour `LINE_LOOP_FG_PCT` pour pointer sur ce nouveau field.

Alternatives moins propres : partage assumé documenté dans Tool 8 respec §9 avec une ligne "FG brightness (partagé ARPEG+LOOP)", ou refactor global en `fgPlayMax` (nom neutre).

---

### F2.4 — [spec-code-drift] nvs-reference.md `LoopPadStore` size=8B inconsistant

**Citation nvs-reference.md:107** :
```
| `LoopPadStore` | `illpad_lpad` | `pads` | 0xBEEF | 1 | 8B | **PLANNED** — not yet in code |
```

**Spec LOOP §20** : "3 control pads (REC, PLAY/STOP, CLEAR) + 16 slot pads" = 19 pads.

**Problème** : 19 × uint8 = 19B data + 4B header (magic 2 + version 1 + reserved 1) = **~23B minimum**. La valeur 8B annoncée dans nvs-reference est stale (probablement un copier-coller de BankPadStore=12B raboté par erreur).

**Impact** : faible tant que le struct n'est pas implémenté. Devient bloquant dès rédaction Phase 1 si le plan recopie naïvement les 8B.

**Action recommandée** : corriger nvs-reference.md à ~28B (en laissant marge pour padding/alignement) ou marquer "TBD lors de Phase 1".

---

### F2.5 — [spec-code-drift minor] `renderPreviewPattern` signature divergente

**Tool 8 respec §6.6** :
```cpp
void renderPreviewPattern(const PatternInstance& inst,
                          uint8_t ledMask,
                          unsigned long now);
```

**Code** ([src/core/LedController.h:137](src/core/LedController.h:137)) :
```cpp
void renderPreviewPattern(const PatternInstance& inst, unsigned long now);
```

**Problème** : `uint8_t ledMask` retiré en implémentation. Le masque est maintenant transporté via `inst.ledMask` (visible dans PatternInstance struct public [LedController.h:121-131](src/core/LedController.h:121)).

**Impact** : fonctionnellement équivalent, juste un écart de signature. Minor.

**Action** : soit mettre à jour la Tool 8 respec §6.6 (rétroactif, ~1 ligne), soit accepter comme drift intentionnel post-implémentation.

---

### F2.6 — [spec-ok] SettingsStore v11 + 3 timers LOOP en place

Vérifié [KeyboardData.h:68,86-88,608-610](src/core/KeyboardData.h) :
- `SETTINGS_VERSION = 11` ✅
- `clearLoopTimerMs` default 500, clamp [200, 1500] ✅
- `slotSaveTimerMs` default 1000, clamp [500, 2000] ✅
- `slotClearTimerMs` default 800, clamp [400, 1500] ✅
- Tool 6 cases 8/9/10 en édition ([ToolSettings.cpp:79-100](src/setup/ToolSettings.cpp:79)) ✅
- Tool 8 single-view shared fields ([ToolLedSettings.cpp:481-483](src/setup/ToolLedSettings.cpp:481)) ✅

---

### F2.7 — [spec-ok] LedSettingsStore v7 + tick durations cachées

Vérifié [KeyboardData.h:263,283-285](src/core/KeyboardData.h) et [LedController.cpp:24-25,884-885](src/core/LedController.cpp) :
- `LED_SETTINGS_VERSION = 7` ✅
- `tickBeatDurationMs` default 30, clamp [5, 500] — consommé par ARPEG step ✅
- `tickBarDurationMs` default 60, clamp [5, 500] — caché dans `_tickBarDurationMs`, non consommé runtime ✅
- `tickWrapDurationMs` default 100, clamp [5, 500] — caché dans `_tickWrapDurationMs`, non consommé runtime ✅

Tool 8 single-view expose les 3 durées ([ToolLedSettings.cpp:493-495](src/setup/ToolLedSettings.cpp:493)). Les caches attendent `consumeBarFlash()` / `consumeWrapFlash()` de LoopEngine (Phase 1+) comme promis §21.

---

### F2.8 — [spec-ok] EVT_LOOP_* réservés + default PTN_NONE

Vérifié [LedGrammar.h:59-65,66](src/core/LedGrammar.h:59) et [LedGrammar.cpp:34-40](src/core/LedGrammar.cpp:34) :
```
EVT_LOOP_REC           = 10,  → { PTN_NONE, CSLOT_VERB_REC,         0 }
EVT_LOOP_OVERDUB       = 11,  → { PTN_NONE, CSLOT_VERB_OVERDUB,     0 }
EVT_LOOP_SLOT_LOADED   = 12,  → { PTN_NONE, CSLOT_CONFIRM_OK,       0 }
EVT_LOOP_SLOT_WRITTEN  = 13,  → { PTN_NONE, CSLOT_VERB_SAVE,        0 }
EVT_LOOP_SLOT_CLEARED  = 14,  → { PTN_NONE, CSLOT_VERB_SLOT_CLEAR,  0 }
EVT_LOOP_SLOT_REFUSED  = 15,  → { PTN_NONE, CSLOT_VERB_REC,         0 }
EVT_LOOP_CLEAR         = 16,  → { PTN_NONE, CSLOT_VERB_CLEAR_LOOP,  0 }
EVT_COUNT              = 17.
```

Correspondance exacte avec spec §21. `PTN_NONE` sentinel court-circuite le rendu ([LedController.cpp:565-567](src/core/LedController.cpp:565)). Câblage Phase 4 à venir.

---

### F2.9 — [spec-ok] ToolLedPreview helper et renderPreviewPattern

Vérifiés :
- `src/setup/ToolLedPreview.{h,cpp}` existent (commit `290839d`) ✅
- `LedController::renderPreviewPattern` public wrapper implémenté ([LedController.cpp:1057-1058](src/core/LedController.cpp:1057)) ✅
- PatternInstance promu public dans LedController.h ([ligne 121](src/core/LedController.h:121)) ✅
- Tool 8 orchestrateur utilise le helper (ToolLedSettings.cpp lignes ~880-1093) ✅

---

### F2.10 — [spec-ok] Tool 8 single-view 6 sections

Vérifié [ToolLedSettings.cpp:303-331](src/setup/ToolLedSettings.cpp:303) :
- SEC_NORMAL / SEC_ARPEG / SEC_LOOP / SEC_TRANSPORT / SEC_CONFIRMATIONS / SEC_GLOBAL ✅
- ~40 LINE_* lignes navigables
- Nav paradigm géométrique (4 flèches) par type : COLOR / SINGLE / MULTI ([commit cc379f5](https://github.com/…/commit/cc379f5))

Aligné avec Tool 8 respec §4.1 et §5.

---

### F2.11 — [spec-ok] Commits Phase 0 + Phase 0.1 tous présents sur main

Vérifiés par `git show --stat` :

| Commit | Phase | Objet | Présent `main` |
|---|---|---|---|
| `5c3e57c` | 0 step 0.1 | LedGrammar foundation | ✅ |
| `1233818` | 0 step 0.2 | LedSettingsStore v6 | ✅ |
| `dec9391` | 0 step 0.3 | SettingsStore v11 (+3 LOOP timers) | ✅ |
| `1869fd8` | 0 step 0.4 | LedController pattern engine | ✅ |
| `dc4727c` | 0 step 0.5 | triggerEvent + ARPEG tick | ✅ |
| `ac8d18c` | 0 step 0.6 | ColorSlotStore v4 (15 slots) | ✅ |
| `3ab8458` | 0 step 0.7 | Tool 6 expose LOOP timers | ✅ |
| `c6d6416` → `b3ae4a6` | 0 step 0.8a-e | Tool 8 legacy 3-page | ✅ |
| `8511b0d` | 0 step 0.9 | dual-path cleanup | ✅ |
| `16dbe8a` | post-audit 0 | bgFactor + SPARK wiring | ✅ |
| `cad7530` | 0.1 step 1 | ColorSlotStore v5 + LedSettingsStore v7 | ✅ |
| `39d2deb` | 0.1 step 2 | renderPreviewPattern wrapper + EVT_STOP default | ✅ |
| `290839d` | 0.1 step 3 | ToolLedPreview helper | ✅ |
| `cc379f5` | 0.1 step 4 | Tool 8 single-view 6 sections | ✅ |
| `6ac9ff3` | 0.1 step 5 | sync briefing + conventions + nvs-reference | ✅ |
| `5a9697c` | 0.1 bis | spec LOOP patch → VALIDÉ | ✅ |

Tous les 16 commits référencés dans la spec existent. ✅

---

### F2.12 — [spec-ok / minor cosmetic] Commentaire stale ToolLedPreview.cpp

**Citation code** ([ToolLedPreview.cpp:143-144](src/setup/ToolLedPreview.cpp:143)) :
```cpp
// Waiting : crossfade continuously between mode color (current bank at setup
// entry) and target color (the edited color). Period hardcoded 1500 ms
```

**Code réel** ligne 151 : `const uint16_t periodMs = 800;`

Commit message `290839d` mentionne "1500 ms" (transient), commit `cc379f5` a réduit à 800ms. Le commentaire ligne 143-144 n'a pas été mis à jour.

**Impact** : purement cosmétique (un lecteur se demande pourquoi 1500 vs 800). **Non-bloquant pour l'audit**, à fixer en tâche séparée.

---

## §4 — Axe 3 : Dépendances Phase 1→6 et recommandation

### §4.1 — Dépendances identifiées

#### Prérequis durs

- **`BANK_LOOP` enum extension (Phase 1)** : prérequis de toute Phase 2+. Aucune pièce LOOP ne peut compiler sans.
- **`LoopPadStore` + `LoopPotStore` + NVS descriptors (Phase 1)** : prérequis Phase 3 Tool 3 b1 (sous-page LOOP lit/écrit LoopPadStore) et Phase 3 Tool 7 (context LOOP lit/écrit LoopPotStore).
- **`LoopEngine` skeleton (Phase 2)** : prérequis Phase 4 LED wiring (émet `consumeBarFlash/WrapFlash` flags que LedController attend) et Phase 5 Effets (applique shuffle/chaos/velpattern au playback LoopEngine).
- **State machine WAITING_* (Phase 2)** : prérequis Phase 4 `EVT_WAITING` câblage mode-aware (voir F2.2).
- **Tool 5 3-way type cycle (Phase 3)** : prérequis Phase 6 (`BANK_LOOP` doit être assignable à une bank avant de pouvoir sauver un slot), aussi prérequis Phase 2 pour `loopQuantize` per-bank.
- **Tool 3 b1 refactor (Phase 3)** : prérequis Phase 6 (assignation des 16 slot pads).
- **`PotMappingStore` 3 contexts (Phase 4)** : prérequis Phase 5 (effets = pots routés vers LoopEngine).

#### Parallélisations possibles

- **Phase 3** : Tool 3 b1, Tool 5, Tool 7, Tool 4 extension sont **indépendants** entre eux (partagent juste Phase 1 stores). Peuvent être commits séparés dans la même PR.
- **Phase 4** : PotRouter 3 contexts et LED wiring sont **séparables** — deux sous-PR possibles, l'un pilote les effets (consomme Phase 5), l'autre pilote l'affichage (consomme Phase 2).
- **Phase 5** : chaque effet (shuffle, chaos, velpattern) est indépendant — commits séparés possibles une fois Phase 2 + Phase 4 PotRouter posés.

#### Ordres forcés

```
1 ──→ 2 ──→ 3 (skeleton + guards + stores avant toute UI)
2 ──→ 4 (LED wiring consomme flags LoopEngine)
3 Tool 3 + Tool 5 ──→ 6 (slots + 3-way requis avant Slot Drive)
4 PotRouter ──→ 5 (pots LOOP routés dans 3e contexte)
5 ─?─→ 6 (slots embarquent params effets, mais pas strictement requis)
```

---

### §4.2 — Features "gros risque" méritant sous-plan dédié

Trois features à mon sens trop lourdes pour un plan d'implémentation unique :

| Feature | Phase | Raison |
|---|---|---|
| **State machine WAITING_*** | 2 | 3 nouveaux états transitoires avec 15+ transitions spécifiées §17 (bank switch commit, double-tap bypass, quantize boundaries). La table §17 est dense et laisse 2 ambiguïtés (F1.2, F1.3). Sous-plan = design détaillé state machine + tests unitaires. |
| **Tool 3 b1 contextuel** | 3 | Refonte structurelle d'un Tool mature. 3 sous-pages + 5 règles de collision + validation cross-contexte. Risque de régression sur ARPEG roles existants. Sous-plan = migration incrémentale en 4-5 commits. |
| **Slot Drive LittleFS** | 6 | Repartitionnement flash + format fichier binaire + gestion atomique + UX 3 gestes (tap court / long press / combo delete). Zero Migration = reset global au flash de ce firmware. Sous-plan = partition + format + serialize + deserialize + gestes en PR distinctes avec tests HW à chaque étape. |

---

### §4.3 — Recommandation ordre d'implémentation

**L'ordre §27 Phase 1→6 tel que sketché est VALIDE**. Aucun ré-ordonnancement nécessaire. Les dépendances identifiées ci-dessus le confirment.

**Ajustements recommandés pour la rédaction du plan Phase 1** :

1. **Clarifier §27 Phase 1 scope** : "déclarer struct LoopPadStore + LoopPotStore + validators + NVS descriptors + defaults compile-time. **Pas de consommateur runtime** à ce stade. Layout NVS figé pour éviter 2e reset user." (cf F1.6)

2. **Phase 1 doit inclure** :
   - Extension `BankType` (+ `BANK_LOOP = 2`, reclasser `BANK_ANY` si pertinent)
   - `LoopPadStore` (~28B, pas 8B — cf F2.4)
   - `LoopPotStore` per-bank (spec §20 dit 8B per-bank × 8 banks)
   - Guards `BankManager::switchToBank()` / `ScaleManager` / `MidiTransport::sendPitchBend/AT` pour ignorer LOOP banks
   - Tool 5 3-way minimal (NORMAL → ARPEG → LOOP cycle, avec refus si déjà 2 LOOP)

3. **Phase 2 doit trancher** :
   - `PendingEvent` factorisé ou dupliqué (F1.7)
   - WAITING colorA mode-aware — option (a), (b), ou (c) de F2.2
   - Comportement STOPPED-loaded + tap REC (F1.2)

4. **Phase 4 doit inclure un commit préparatoire** :
   - Fix EVT_WAITING pour LOOP (F2.2) — avant que LoopEngine émette le premier WAITING_*.
   - Option : ajouter `uint8_t fgLoopIntensity` à `LedSettingsStore v8` (F2.3). Bumper dans le même commit que les autres fields LOOP éventuels pour regrouper les resets user.

---

## §5 — Questions à trancher avant rédaction du plan Phase 1

**Toutes les 8 questions ont été tranchées** en session brainstorming 2026-04-20. Voir [spec LOOP §28 pour justification détaillée](../specs/2026-04-19-loop-mode-design.md#28--tranchage-des-8-questions-résiduelles-2026-04-20).

| # | Question | Décision | Ref spec |
|---|---|---|---|
| Q1 | `LoopPadStore` size réelle ? | **23 B strict packed** | §20, §28 |
| Q2 | `PendingEvent` factorisé ou dupliqué ? | **Dupliqué** (indépendance ArpEngine + LoopEngine) | §27 Phase 2, §28 |
| Q3 | `EVT_WAITING` mode-aware ? | **1 event unique** ; colorA verb PLAY (éditable), colorB blanc (hardcodé triggerEvent), brightness = fgArpStopMax × bgFactor BG | §21, §27 Phase 1 step, §28 |
| Q4 | LOOP FG brightness : field séparé ? | **Partagé + rename** `fgArpPlayMax` → `fgPlayMax`, Tool 8 ligne déplacée en TRANSPORT | §27 Phase 1 step, §28 |
| Q5 | STOPPED-loaded + tap REC ? | **PLAYING + OVERDUBBING simultanés** | §8, §28 |
| Q6 | Refactor Tool 5 colonnes ? | **Phase 3 minimal, deferred** | §27 Phase 3, §28 |
| Q7 | Tool 4 extension ? | **Phase 3 bundle** (avec Tool 3 b1) | §27 Phase 3, §28 |
| Q8 | Max 1 bank REC/OD simultané ? | **Oui**, expliciter invariant 11 §23 | §23 invariant 11, §28 |

---

## §6 — Zones auditées OK (preuve de couverture)

Pour documenter que la conclusion "aucun bloquant" n'est pas due à un survol :

### Axe 1 — sections lues intégralement et cross-référencées

- §1 Intention musicale, §2 Périmètre, §3 LOOP core
- §4 Slot Drive, §5 Refactor Tool 3 b1 + règles de collision
- §6 Configurer bank LOOP, §7 Enregistrer, §8 Overdub, §9 Play/Stop/Clear, §10 Effets
- §11 Save slot, §12 Rappel slot, §13 Supprimer slot, §14 Multi-banks
- §15 Cohabitation NORMAL/ARPEG/ControlPads, §16 Clock, §17 Quantization + table gestes concurrents
- §18 Hiérarchie pads, §19 LEFT+double-tap LOOP
- §20 NVS et persistence, §21 LED system renvoi, §22 Pot routing
- §23 Invariants (10 items), §24 Non-goals (13 items), §25 Budget ressources
- §26 Questions tranchées (22) + ouvertes (4) + déférées, §27 Suite Phase 1→6

**Cross-checks effectués** :
- Nomenclature events : EVT_* dans spec vs LED spec §12 vs LedGrammar.h — cohérent sauf HOLD_ON/HOLD_OFF (F1.5).
- Règles d'état : §17 table vs §9 Play/Stop/Clear vs §23 invariants — cohérent sauf STOPPED-loaded+REC (F1.2).
- Timers : §9/§11/§13 valeurs vs §20 ranges vs LED spec §13 couplage — cohérent.
- Contexts pad : §5 vs §18 vs §15 — cohérent.
- Couleurs : §21 vs LED spec §11 vs ColorSlotStore code — cohérent.

### Axe 2 — greps exécutés et valeurs lues

- `COLOR_SLOT_*` / `CSLOT_*` dans KeyboardData.h ✅
- `LED_SETTINGS_VERSION` / `tickBeat/Bar/WrapDurationMs` dans 3 fichiers ✅
- `SETTINGS_VERSION` / `clearLoopTimerMs` / `slotSaveTimerMs` / `slotClearTimerMs` ✅
- `EVT_LOOP_*` / `PatternId` / `PTN_NONE` / `EVENT_RENDER_DEFAULT` ✅
- `BankType` / `BANK_NORMAL` / `BANK_ARPEG` / `BANK_LOOP` (absent) ✅
- `renderPreviewPattern` signature + body ✅
- `renderBankNormal` / `renderBankArpeg` / `renderBankLoop` (absent) ✅
- `LoopPadStore` / `LoopPotStore` / `LoopEngine` / `consumeBarFlash` / `consumeWrapFlash` (tous absents) ✅
- `PotMappingStore` (2 contexts actuels, Phase 4 prévu) ✅
- `LINE_LOOP_*` / `LINE_TRANSPORT_*` / `SEC_*` Tool 8 single-view ✅

### Axe 3 — dépendances tracées

- 7 prérequis durs identifiés + 3 parallélisations + 4 ordres forcés
- 3 features "gros risque" flaggées
- Ordre §27 Phase 1→6 validé, 4 ajustements mineurs recommandés pour Phase 1/2/4

---

**Fin du rapport.** Attend revue avant intégration au workflow du plan Phase 1.
