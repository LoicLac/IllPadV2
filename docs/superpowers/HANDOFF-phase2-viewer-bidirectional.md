# Handoff — Phase 2 viewer bidirectionnel (write commands)

**Date émission** : 2026-05-17 — fin de session Phase 1 viewer serial.
**À ouvrir dans** : nouvelle session Claude Code, worktree `/Users/loic/Code/PROJECTS/ILLPAD_V2` (branche `main`).
**Prérequis lecture** : ce doc + spec firmware-viewer + plan Phase 1 + commits récents.

---

## Contexte (1 minute)

Phase 1 viewer serial centralization vient d'être livrée — 11 commits firmware + 1 viewer entre `fedeb81` et `9d6a40f` sur `main`. Le module `src/viewer/ViewerSerial.{cpp,h}` centralise tous les events runtime émis vers le viewer JUCE (worktree `../ILLPAD_V2-viewer/` branche `viewer-juce`). HW gate validé : boot dump complet, bank switch, scale, ARPEG live, external MIDI clock USB slave.

**Phase 2 = bidirectionnel** : permettre au viewer JUCE d'envoyer des commandes `!CMD=val` au firmware pour modifier certains params runtime, sans passer par setup mode.

## Hooks déjà en place côté firmware (Phase 1)

Dans `src/viewer/ViewerSerial.cpp`, `viewer::pollCommands()` reconnaît déjà `?STATE/?BANKS/?BOTH/?ALL` (read-only) ET intercepte tout `!*` avec un stub :
```cpp
} else if (cmdBuf[0] == '!') {
  emit(PRIO_HIGH, "[ERROR] write commands not yet implemented (Phase 2)\n");
}
```
Phase 2 remplace ce stub par un dispatcher.

Le viewer-juce pré-codé a déjà les hooks UI/Model côté envoi — à vérifier en début de Phase 2 brainstorm (worktree `../ILLPAD_V2-viewer/`, voir `Source/serial/` et `Source/model/`).

## Palier vert identifié (brainstorm Phase 1 round 4)

Params runtime-mutables sans side-effect majeur (palier vert) :
- **`!CLOCKMODE=slave|master`** → `s_clockManager.setMasterMode()` + `s_settings.clockMode = CLOCK_MASTER/SLAVE` + NVS save queue
- **`!PANIC_RECONNECT=0|1`** → `s_panicOnReconnect` global + `s_settings.panicOnReconnect` + NVS
- **`!AFTERTOUCH_RATE=N`** → `s_midiEngine.setAftertouchRate(N)` + `s_settings.aftertouchRate` + NVS
- **`!DOUBLE_TAP_MS=N`** → `s_bankManager.setDoubleTapMs(N)` + `s_doubleTapMs` + `s_settings.doubleTapMs` + NVS

ARPEG_GEN per-bank (palier vert, requires `BANK=N` qualifier) :
- **`!BONUS=val BANK=N`** → `s_banks[N].arpEngine->setBonusPile(val)` + NVS via NvsManager
- **`!MARGIN=val BANK=N`** → idem `setMarginWalk`
- **`!PROX=val BANK=N`** → idem `setProximityFactor`
- **`!ECART=val BANK=N`** → idem `setEcart`

**Palier rouge exclu** (validé Phase 1 brainstorm) : pot mapping, color slots, LED settings, padOrder, bankPads, scale pads, hold pad, octave pads, BankType.

## Questions ouvertes pour le brainstorm Phase 2

À résoudre dans l'ordre :

### Q1 — Format syntaxique des commandes per-bank

Trois variantes :
- A : `!BONUS=3 BANK=2` (key=value avec BANK comme key séparé)
- B : `!BANK.2.BONUS=3` (dotted path)
- C : `!BONUS=3:2` (positional, BONUS=value:bank)

A est cohérent avec les `[GLOBALS]`/`[SETTINGS]` events qui sont key=value libre. B est plus parser-strict. C est plus compact mais sémantique floue.

Reco probable : **A** (cohérence avec read events).

### Q2 — Confirmation pattern

Après un write réussi, le firmware re-émet l'event correspondant pour confirmer côté viewer :
- `!CLOCKMODE=master` succès → `[SETTINGS] ClockMode=master ...`
- `!BONUS=3 BANK=2` succès → `[STATE] bank=2 ...` (mais [STATE] n'inclut pas bonusPile actuellement)

Décision needed : ajouter les ARPEG_GEN per-bank params à `[STATE]` (extension protocole) OU émettre un event dédié `[BANK_SETTINGS] bank=N bonus=X margin=Y prox=Z ecart=W` (séparation propre).

### Q3 — Error handling

Format de réponse en cas d'erreur :
- `[ERROR] code=invalid_value cmd=!BONUS=999 BANK=2 range=1.0-2.0` (verbose)
- `[ERROR] !BONUS=999: out of range` (humain)
- `[ERROR] EINVAL` (terse)

Le viewer doit pouvoir parser facilement. Reco probable : format humain avec key error explicite, le viewer affiche en toast/notification.

Types d'erreurs à supporter :
- Unknown command
- Invalid value (parse fail, NaN, etc.)
- Out of range
- BANK= missing pour command per-bank
- BANK= out of range (>= 8)
- BANK type incompatible (ex. `!BONUS` sur bank NORMAL — pas d'ArpEngine)

### Q4 — NVS persistence policy

3 options :
- Auto-save toujours après write success (cohérent avec setup mode)
- Auto-save seulement pour les params explicitement persistants (`s_settings.*` fields)
- Pas d'auto-save : le viewer envoie séparément `!SAVE_SETTINGS` pour persister

Reco probable : option 1 (auto-save toujours, cohérent avec la sémantique setup mode où une modif via Tool 8 est persistée immédiatement).

### Q5 — Scope vs YAGNI

Le palier vert liste 8 commands au total. Faut-il toutes les implémenter en Phase 2, ou commencer par un sous-ensemble (ex. ClockMode + 4 ARPEG_GEN seuls) et étendre en Phase 2.b si besoin ?

Reco probable : **toutes en Phase 2** car le pattern dispatcher est identique pour toutes (parse → setter → confirm event → NVS queue). Le travail incremental n'apporte rien.

### Q6 — Cross-worktree workflow

Phase 1 a utilisé "viewer pré-codé en avance" sans gating cross-worktree. Pour Phase 2, le viewer-juce a aussi besoin d'évoluer (UI pour envoyer les write commands). Workflow possible :
- Spec firmware + parser viewer en parallèle
- Plan firmware exécuté en série
- Tests HW finaux après les deux côtés ready

Le user (Loïc) gère manuellement les changes viewer (worktree séparé).

## Process suggéré nouvelle session

1. **Ouvrir nouvelle session Claude Code** dans `/Users/loic/Code/PROJECTS/ILLPAD_V2`.
2. **Lire** : ce doc + `STATUS.md` (focus courant section + viewer Phase 1 commits) + `docs/superpowers/specs/2026-05-17-viewer-serial-centralization-design.md` §19-21 (hooks Phase 2).
3. **Invoquer `superpowers:brainstorming`** pour Phase 2 viewer bidirectionnel.
4. Répondre aux questions Q1-Q6 ci-dessus en séquence avec l'utilisateur.
5. Écrire la spec Phase 2 : `docs/superpowers/specs/<YYYY-MM-DD>-viewer-bidirectional-phase2-design.md`.
6. Écrire le plan Phase 2 : `docs/superpowers/plans/<YYYY-MM-DD>-viewer-bidirectional-phase2-firmware-plan.md`.
7. Exécuter via `superpowers:subagent-driven-development` (pattern Phase 1).

## Lectures de référence

- **Plan firmware Phase 1** : `docs/superpowers/plans/2026-05-17-viewer-serial-phase1-firmware-plan.md` — surtout §19-21 (hooks Phase 2 explicits).
- **Spec firmware Phase 1** : `docs/superpowers/specs/2026-05-17-viewer-serial-centralization-design.md` — §19 input parser extensible + §20 confirmation pattern + §21 inventaire Phase 2.
- **Protocol parser viewer** : `../ILLPAD_V2-viewer/ILLPADViewer/docs/firmware-viewer-protocol.md` — sections §1.17 `[GLOBALS]` + §1.18 `[SETTINGS]` (format de référence pour les events de confirmation).
- **STATUS.md** : section "Viewer serial centralization Phase 1" — historique commits + post-impl fixes.

## Précautions

- Le firmware tourne sur ESP32-S3 avec capacitive sensing Core 0 à ~92% CPU. Toute modif doit préserver MIDI live et la posture safe-default (cf. CLAUDE.md projet "embedded code mindset").
- Les write commands en runtime modifient des params utilisés en live → tester systématiquement aucun side-effect sur sensing/MIDI/LED.
- NVS write a un wear limit. Si plusieurs commands consécutives (ex. viewer slider envoie 50 events/sec), il faut debouncer la NVS save (déjà fait pour pot via `tickPotDebounce` dans NvsManager). Pattern à dupliquer pour les commands viewer ou écrire un debounce générique.
- `s_settings` est désormais à external linkage (non-static) dans main.cpp depuis Phase 1.D — Phase 2 peut le modifier depuis le module sans drama.

---

**Estimation charge** : ~10-15h dev sur 1-2 sessions. Brainstorm + spec + plan ~3-4h. Implementation 4-6 commands ~4-6h. HW gate + viewer-side coordination ~2-3h.
