# ILLPAD48 V2 — Design unifié du feedback LED

**Date** : 2026-04-19
**Statut** : brouillon issu de brainstorm, à valider
**Scope** : grammaire visuelle unifiée pour NORMAL / ARPEG / LOOP, refactor `LedController` + `LedSettingsStore` + `ColorSlotStore` + Tool 8, remplacement du placeholder §21 de la spec LOOP.
**Sources** : `docs/reference/architecture-briefing.md` §1 MM7, §2 Flow 3, §5 P5/P14 ; `docs/superpowers/specs/2026-04-19-loop-mode-design.md` §17/§21 ; `src/core/LedController.{h,cpp}` ; `src/core/KeyboardData.h`.
**Relation avec la spec LOOP** : ce document devient la source de vérité pour §21 et résout les items déférés de §26 (LED feedback WAITING_\*, write LittleFS, combo delete, color slot LOOP, convention couleur/pattern).

---

## Partie 1 — Cadre

### §1 — Intention

Le feedback LED d'ILLPAD48 a été construit par couches successives (ARPEG, confirms, SK6812 RGBW, color slots). Avec l'arrivée du mode LOOP et de ses 8 états nouveaux (EMPTY, RECORDING, PLAYING, OVERDUBBING, STOPPED, WAITING_PLAY, WAITING_STOP, WAITING_LOAD), plus 5 events transport (REC, OVERDUB, CLEAR, slot write, slot clear), le risque de confusion visuelle est réel. Le moment est venu de poser une grammaire complète et stable.

Ce document propose une **grammaire unifiée** fondée sur un slogan directeur et 9 patterns fixes, avec une séparation nette entre l'identité persistante des modes ("les noms") et les actions transitoires ("les verbes"). La grammaire doit permettre à un musicien de scanner la strip de 8 LEDs et d'identifier sans ambiguïté, en moins d'une seconde : quelle bank est FG, quelles banks jouent, quelles banks sont en attente d'un boundary, quelles banks enregistrent.

### §2 — Pain points résolus

1. **Illisibilité des états de bank** — aujourd'hui, ARPEG idle / stopped-loaded / playing se distinguent par des nuances d'intensité qui se lisent mal en BG. LOOP hérite du problème avec 4 états supplémentaires.
2. **Encodage couleur par mode uniquement** — blanc=NORMAL / bleu=ARPEG / jaune=LOOP sépare les *types* de banks mais ne permet pas de scanner un *état* transversalement (qui joue, qui attend, qui enregistre).
3. **Pulse lent mal utilisé** — la sine pulse existe seulement en FG-ARPEG-stopped-loaded. Décorative là où elle est, absente là où elle communiquerait fort (WAITING_\*).
4. **Config Tool 8 trop volumineuse** — 20 params visibles à plat, pensés en "tunes d'intensité" plutôt qu'en grammaire. Flash patterns (double-flash delete, ramp save) absents parce que pénibles à exposer.
5. **Pas de langage universel transport** — PLAY/STOP/REC/OVERDUB/CLEAR inventent chacun leur rendu selon le mode. Risque de 3 langages visuels concurrents pour le même geste mental.

### §3 — Scope et non-goals

**Dans le scope** :
- Grammaire visuelle des modes de jeu (NORMAL, ARPEG, LOOP) et des events transport
- Feedback des confirmations (bank switch, scale change, octave, hold, play/stop, save, load, delete, refuse)
- Feedback des états transitoires (WAITING_PLAY / STOP / LOAD)
- Encodage dimensionnel mode × état × FG/BG
- Refactor `LedController` + `LedSettingsStore` + `ColorSlotStore` + Tool 8

**Hors scope** :
- Implémentation effective (code). Seulement le design.
- Modifications `CapacitiveKeyboard` (intouchable).
- Redesign hardware (contraint à 8 LEDs SK6812 RGBW).
- Refonte des autres Tools (1-7) autre que l'impact du changement LED.

---

## Partie 2 — Critères de succès

Les critères ci-dessous ont été validés en début de brainstorm. Toute proposition du reste du document doit cocher les 6 critères.

| # | Critère | Rationale |
|---|---|---|
| **F1** | **Action = pattern invariant**. PLAY, STOP, REC, OVERDUB, CLEAR produisent le même pattern visuel quel que soit le mode. Le musicien apprend la grammaire une seule fois. | Pain point 5. Condition de réutilisabilité inter-modes. |
| **F2** | **État unique → pattern unique**. Chaque couple (mode, state, FG/BG) a un rendu sans ambigüité. | Pain point 1. Lisibilité brute. |
| **F3** | **Hiérarchie FG/BG immédiate**. La bank FG se distingue d'un BG d'un seul coup d'œil, peu importe le mode. | Multi-bank workflows (§14 spec LOOP). |
| **F4** | **Scannabilité de groupe**. Le musicien peut répondre en 1 coup d'œil à "quelles banks jouent ?", "quelles banks sont vides ?", "laquelle attend un boundary ?". | Pain point 2. |
| **T1** | **Fonctionnel > esthétique**. Tout choix visuel doit servir un des 4 critères fonctionnels avant d'optimiser l'apparence. | Ligne directrice projet : LED utile, pas décorative. |
| **T2** | **Zéro hardcode + couples structurants**. Tout reste paramétrable via `LedSettingsStore` / `ColorSlotStore` / Tool 8. Le Tool 8 structure sa vue et son workflow autour de couples de paramètres (palette stricte, pas d'override per-event ; si un pattern manque c'est qu'il manque dans la palette). | Invariant projet + pain point 4. |

---

## Partie 3 — Inventaire critique

### §4 — États persistants actuels (display normal)

Fonds permanents rendus par `LedController::renderBankNormal()` / `renderBankArpeg()`. Servent d'état de base de la strip.

| Mode × State × FG/BG | Pattern actuel | Source `LedSettingsStore` |
|---|---|---|
| NORMAL FG | SOLID white | `normalFgIntensity` |
| NORMAL BG | SOLID white dim | `normalBgIntensity` |
| ARPEG FG idle (pile vide) | SOLID blue dim | `fgArpStopMin` |
| ARPEG FG stopped-loaded | PULSE_SLOW blue | `fgArpStopMin` ↔ `fgArpStopMax`, `pulsePeriodMs` |
| ARPEG FG playing | SOLID blue bright + FLASH tick white | `fgArpPlayMax`, `tickFlashFg`, `tickFlashDurationMs` |
| ARPEG BG stopped | SOLID blue dim | `bgArpStopMin` |
| ARPEG BG playing | SOLID blue dim + FLASH tick white | `bgArpPlayMin`, `tickFlashBg` |

Champs legacy `fgArpPlayMin`, `bgArpStopMax`, `bgArpPlayMax` présents en NVS mais **unused** à l'exécution (hidden en Tool 8). Seront retirés au refactor.

### §5 — Confirmations actuelles (overlays temporaires)

Enum `ConfirmType` (7 valeurs), mixture de blink et fade. Rendu dans `LedController::renderNormalDisplay()` en overlay (jamais `clearPixels`).

| ConfirmType | Color slot | Pattern | Masque |
|---|---|---|---|
| `CONFIRM_BANK_SWITCH` | `CSLOT_BANK_SWITCH` | Blink on/off | FG LED |
| `CONFIRM_SCALE_ROOT` | `CSLOT_SCALE_ROOT` | Blink on/off | Scale group mask |
| `CONFIRM_SCALE_MODE` | `CSLOT_SCALE_MODE` | Blink on/off | Scale group mask |
| `CONFIRM_SCALE_CHROM` | `CSLOT_SCALE_CHROM` | Blink on/off | Scale group mask |
| `CONFIRM_HOLD_ON` | `CSLOT_HOLD_ON` | Fade IN 0→100% | Target LED (FG/BG) |
| `CONFIRM_HOLD_OFF` | `CSLOT_HOLD_OFF` | Fade OUT 100→0% | Target LED (FG/BG) |
| `CONFIRM_OCTAVE` | `CSLOT_OCTAVE` | Blink on/off | FG LED |

### §6 — États système (priorités supérieures)

Niveaux 1-8 de la priorité LED (voir MM7 du briefing). Inchangés par ce design :

- Boot progressive fill / failure blink
- Setup comet (violet ping-pong 8 LEDs)
- Chase pattern (calibration entry)
- Error (LEDs 3-4 blink red 500ms)
- Battery gauge (3s)
- Battery low overlay (3-blink burst sur FG)
- Pot bargraph + tempo bargraph
- Calibration mode

### §7 — Nouveautés LOOP core + Slot Drive

**États LOOP** — à définir par ce document :

| Mode × State × FG/BG | À couvrir |
|---|---|
| LOOP FG EMPTY | Aucun contenu, prêt à enregistrer |
| LOOP FG STOPPED-loaded | Contenu présent, silence |
| LOOP FG RECORDING | Capture active (pile vide, on écrit) |
| LOOP FG OVERDUBBING | Capture active avec boucle qui joue |
| LOOP FG PLAYING | Boucle joue |
| LOOP FG WAITING_PLAY / STOP / LOAD | En attente de boundary |
| LOOP BG EMPTY / STOPPED-loaded / PLAYING / WAITING_\* | Idem en BG |
| LOOP BG RECORDING / OVERDUBBING | **Impossible** — bank switch refusé pendant capture (§23 spec LOOP) |

**Events nouveaux** :

- `LOOP_SLOT_LOADED` (réussite)
- `LOOP_SLOT_WRITTEN` (réussite du save long-press — pattern composite ramp + suffix)
- `LOOP_SLOT_CLEARED` (réussite du delete combo — pattern composite ramp + suffix)
- `LOOP_SLOT_REFUSED` (refus : save sur occupé, load sur vide, save pendant PLAYING)
- `CLEAR_LOOP` (long-press 500ms qui vide la bank — pattern composite ramp + suffix)

---

## Partie 4 — Système proposé

### §8 — Slogan directeur et règles invariantes

**"Le nom au fond, le verbe en surface."**

- Le **nom** (mode de la bank) est la couleur et le pattern persistants du fond. Identité stable.
- Le **verbe** (action transport) est la couleur de l'overlay transitoire (tick flash, fade, ou oscillation de couleur).
- Cette règle est **sans exception**. RECORDING et OVERDUBBING n'altèrent pas le fond : ils ajoutent un tick coloré au verbe (rouge REC, orange OVERDUB) sur le fond jaune du mode LOOP.

**Règle "le tick perce"** :
- Les ticks (FLASH) et les ticks de wrap ne font **jamais** baisser l'intensité du fond. Le fond reste à sa valeur normale (`fgPct` en FG, `fgPct × bgFactor` en BG).
- Pendant le flash (durée 30-80 ms), le pixel est temporairement remplacé par la couleur verb à sa propre intensité : `fgPct` du FLASH en FG, `bgPct` propre du FLASH en BG (toujours supérieur au fond BG pour garantir la perception, typiquement 50-60%).

**Règle "couple structurant" (T2)** :
- Palette de patterns **stricte**. Si un event a besoin d'un rendu spécifique, on enrichit la palette (nouveau pattern), pas des overrides per-event.
- Chaque event = une card avec 3 champs exposés dans Tool 8 : `pattern`, `color_slot`, `brightness_fg` (le `brightness_bg` des ticks est exposé comme sous-param du pattern FLASH, pas per-event).

### §9 — Architecture à 3 couches

```
┌─────────────────────────────────────────────────────────────┐
│ COUCHE 1 — Palette de patterns (9 entrées fixes)            │
│   Chaque entrée = {pattern_id, params}                      │
│   ex. BLINK_SLOW {onMs, offMs, cycles}                      │
├─────────────────────────────────────────────────────────────┤
│ COUCHE 2 — Color slots (16 entrées, extensible)             │
│   Chaque slot = {preset_id, hue_offset}                     │
│   resolveColorSlot() → RGBW                                 │
├─────────────────────────────────────────────────────────────┤
│ COUCHE 3 — Event mapping (~20 events)                       │
│   Chaque event = {pattern_ref, color_slot_ref, fg_pct}      │
│   (bg_pct pour FLASH uniquement, porté par le pattern)      │
└─────────────────────────────────────────────────────────────┘

Deux tables indépendantes de mapping en Couche 3 :
  (a) bank_state_rendering[mode][state][fg/bg] → tuple de rendering
      (fond stable + overlay éventuel : tick flash ou crossfade)
  (b) event_rendering[event_id] → tuple de rendering
      (overlay temporaire déclenché par un geste utilisateur)
```

### §10 — Palette de 9 patterns (Couche 1)

Palette validée perceptuellement (voir mockup `patterns-palette-v3.html`). Chaque pattern a une sémantique claire et unique.

| # | Pattern ID | Effet visuel | Paramètres | Sémantique |
|---|---|---|---|---|
| 1 | `SOLID` | Intensité fixe | `pct` | Fond stable |
| 2 | `PULSE_SLOW` | Sine amplitude **modérée**, lent (≥2000ms). Jamais 0%↔100%. | `minPct`, `maxPct`, `periodMs` | "Pile pleine et prête à jouer" |
| 3 | `CROSSFADE_COLOR` | Fade **continu** entre deux color slots (ease-in-out, ~800ms) | `periodMs`, (les 2 slots sont portés par l'event) | "État en attente" |
| 4 | `BLINK_SLOW` | On/off régulier, 2-4 cycles, durée totale ~800ms | `onMs`, `offMs`, `cycles` | Confirm lourd (bank switch) |
| 5 | `BLINK_FAST` | Idem, durée totale ~400ms, 2-3 cycles | idem | Confirm léger (scale, octave), slot loaded, refuse (cycles=3) |
| 6 | `FADE` | Rampe lisse `startPct → endPct` en `durationMs` (400-1000ms) | `startPct`, `endPct`, `durationMs` | PLAY (0→70%), STOP (70→0%) |
| 7 | `FLASH` | Tick unique très court (30-80 ms). Le fond reste à son intensité normale. | `durationMs`, `fgPct`, `bgPct` | Tick step ARPEG, wrap LOOP (playing/rec/overdub) |
| 8 | `RAMP_HOLD` | Rampe `startPct → endPct` en `rampMs` (dérivée du timer métier), figée à `endPct` si hold maintenu, **suffix SPARK à la complétion**, annulée si relâché avant. | `startPct`, `endPct`, `rampMs` (dérivé), `suffixParams` | Long-press engagement (CLEAR loop 500ms, slot save 1000ms) |
| 9 | `SPARK` | **2 flashs très rapprochés** (~50/70/50 ms). Non-répétitif, blanc par défaut. | `onMs`, `gapMs` | Suffix de complétion universel après RAMP_HOLD |

**Règles structurelles de la palette** :
- `SPARK` est toujours blanc (sémantique "confirmé universel"). Pas d'override de couleur.
- `RAMP_HOLD.rampMs` n'est **pas** un paramètre indépendant dans `LedSettingsStore`. Il est **dérivé du timer métier** de l'event associé (cf §13). Les params du SPARK suffix (`onMs`, `gapMs`) restent éditables en Tool 8.
- `CROSSFADE_COLOR` porte un `periodMs` unique (800ms par défaut). Les 2 couleurs entre lesquelles il fade sont fournies par l'event consommateur (cf §11 WAITING).
- `FLASH` est le seul pattern qui expose 2 brightness (`fgPct`, `bgPct`) pour satisfaire la règle "le tick perce".

### §11 — Grammaire des couleurs (Couche 2)

**Couleurs des modes (les "noms")** — identité persistante du fond :

| Slot | Couleur cible | Preset + hue |
|---|---|---|
| `CSLOT_MODE_NORMAL` | Warm white | "Warm White" preset + hue 0 (à teinter si désiré) |
| `CSLOT_MODE_ARPEG` | Deep blue | "Deep Blue" preset + hue 0 |
| `CSLOT_MODE_LOOP` | Yellow | "Gold" preset + hue 0 (plus jaune pur qu'Amber) |

**Couleurs des actions transport (les "verbes")** — identité transitoire :

| Slot | Couleur cible | Events consommateurs |
|---|---|---|
| `CSLOT_VERB_PLAY` | Green | `FADE` fade-in PLAY, FLASH tick PLAYING (wrap/step), côté "play" du CROSSFADE WAITING |
| `CSLOT_VERB_REC` | Red | FLASH tick RECORDING (1× par bar) |
| `CSLOT_VERB_OVERDUB` | Orange | FLASH tick OVERDUBBING (1× par wrap) |
| `CSLOT_VERB_CLEAR_LOOP` | Cyan | `RAMP_HOLD` du CLEAR long-press sur pad CLEAR |
| `CSLOT_VERB_SLOT_CLEAR` | Orange (distinct de OVERDUB par teinte) | `RAMP_HOLD` du delete combo slot |
| `CSLOT_VERB_SAVE` | Magenta | `RAMP_HOLD` du slot save long-press |

**Note option γ — STOP sans slot dédié** : le FADE du STOP utilise la **couleur verb du state précédent** (`CSLOT_VERB_PLAY` vert en sortant d'un PLAYING, implicite en sortant d'un REC/OVERDUB). Pas de `CSLOT_VERB_STOP` créé. Sémantique "ce qui s'éteint est ce qui jouait".

**Couleurs des events setup/navigation** — famille distincte des modes et des verbes :

| Slot | Couleur cible |
|---|---|
| `CSLOT_BANK_SWITCH` | Couleur distincte, proposée : Cool White ou Ice Blue (différente du warm white NORMAL) |
| `CSLOT_SCALE_ROOT` | Amber |
| `CSLOT_SCALE_MODE` | Gold |
| `CSLOT_SCALE_CHROM` | Amber-pink (Coral) |
| `CSLOT_OCTAVE` | Violet |

La famille scale forme un dégradé chromatique cohérent (amber → gold → amber-pink → violet), visible de Tool 8 comme une gamme continue.

**Confirmation universelle** :

| Slot | Couleur cible |
|---|---|
| `CSLOT_CONFIRM_OK` | White (utilisé par le SPARK suffix partout) |

**Refus** :

Réutilise `CSLOT_VERB_REC` (rouge) via pattern `BLINK_FAST` cycles=3. Pas de slot dédié. Le musicien distingue "non" vs "REC" par le contexte et par le pattern (BLINK_FAST ≠ FLASH).

**Bilan slots** : 12 (avant) → 16 (après). Extension `COLOR_SLOT_COUNT` de 12 à 16, respectant l'invariant `sizeof(ColorSlotStore) ≤ 128`. Changement mineur en NVS (4 × 2 bytes = 8 bytes supplémentaires).

### §12 — Mapping events (Couche 3)

Table complète des events. Les entrées marquées en italique sont les retraits (renommage, fusion).

| Event | Pattern | Color slot | Paramètres | Masque |
|---|---|---|---|---|
| BANK_SWITCH | `BLINK_SLOW` | `CSLOT_BANK_SWITCH` | cycles=2, durée totale ~800ms, fgPct=100% | FG LED |
| SCALE_ROOT | `BLINK_FAST` | `CSLOT_SCALE_ROOT` | cycles=2, durée totale ~300ms | Scale group mask |
| SCALE_MODE | `BLINK_FAST` | `CSLOT_SCALE_MODE` | idem | Scale group mask |
| SCALE_CHROM | `BLINK_FAST` | `CSLOT_SCALE_CHROM` | idem | Scale group mask |
| OCTAVE | `BLINK_FAST` | `CSLOT_OCTAVE` | idem | FG LED |
| PLAY (all modes) | `FADE` 0→70% | `CSLOT_VERB_PLAY` | durationMs=500 | Target LED (FG ou BG via mask) |
| STOP (all modes) | `FADE` 70→0% | verb slot du state précédent (option γ) | durationMs=500 | idem |
| WAITING (PLAY/STOP/LOAD unifié) | `CROSSFADE_COLOR` | colorA = mode, colorB = `CSLOT_VERB_PLAY` | periodMs=800 | Target LED |
| LOOP_SLOT_LOADED | `BLINK_FAST` | `CSLOT_CONFIRM_OK` (white) | cycles=2 | FG LED |
| LOOP_SLOT_WRITTEN | `RAMP_HOLD` 0→100% + SPARK suffix | ramp = `CSLOT_VERB_SAVE`, suffix = white | rampMs = `slotSaveTimerMs` (1000ms), suffix SPARK ~150ms | FG LED |
| LOOP_SLOT_CLEARED | `RAMP_HOLD` 100→0% + SPARK suffix | ramp = `CSLOT_VERB_SLOT_CLEAR`, suffix = white | rampMs = `slotClearTimerMs` (TBD, ex. 800ms) | FG LED |
| CLEAR_LOOP (long-press) | `RAMP_HOLD` 100→0% + SPARK suffix | ramp = `CSLOT_VERB_CLEAR_LOOP` (cyan), suffix = white | rampMs = `clearLoopTimerMs` (500ms) | FG LED |
| LOOP_SLOT_REFUSED | `BLINK_FAST` cycles=3 | `CSLOT_VERB_REC` (rouge, réutilisé) | durée totale ~450ms | FG LED |
| ~~CONFIRM_HOLD_ON~~ | Remplacé par PLAY (même pattern, même event) | — | — | — |
| ~~CONFIRM_HOLD_OFF~~ | Remplacé par STOP | — | — | — |
| ~~CONFIRM_LOOP_REC (entry)~~ | Supprimé : le tick RECORDING joue le rôle d'entrée | — | — | — |

Les suppressions (`HOLD_ON/OFF`, `LOOP_REC` entry) sont des unifications : mêmes gestes, mêmes patterns, mêmes events. Pas de perte expressive.

### §13 — Couplage RAMP_HOLD ↔ timer métier

Invariant architectural : la durée d'une rampe `RAMP_HOLD` est **toujours** la même que le timer d'engagement métier qu'elle visualise. Une seule source de vérité par event.

| Event | Timer métier (source) | Lecture visuelle |
|---|---|---|
| CLEAR_LOOP | `SettingsStore::clearLoopTimerMs` (500 ms) | `RAMP_HOLD.rampMs = clearLoopTimerMs` |
| LOOP_SLOT_WRITTEN | `SettingsStore::slotSaveTimerMs` (1000 ms) | `RAMP_HOLD.rampMs = slotSaveTimerMs` |
| LOOP_SLOT_CLEARED | `SettingsStore::slotClearTimerMs` (TBD, proposé 800 ms) | `RAMP_HOLD.rampMs = slotClearTimerMs` |

Conséquences :
- Tool 8 **lit et affiche** ces durées (informatif) mais ne les édite pas. Une ligne "rampMs : dérivé de Tool 6" ou équivalent.
- Tool 6 (Settings) ou Tool 5 (Bank Config, à décider selon le timer) est la seule UI d'édition.
- Les params purement visuels de `RAMP_HOLD` (SPARK suffix `onMs`/`gapMs`/`cycles`) restent dans `LedSettingsStore` et sont éditables en Tool 8.

Pas de désync possible entre ce que l'utilisateur voit (ramp) et ce que le code mesure (timer).

### §14 — Priorité et overlays

La hiérarchie LED actuelle (9 niveaux, MM7 du briefing) est **préservée**. Le design ne touche ni le boot, ni le setup comet, ni la battery gauge, ni les bargraphs.

Les overlays d'events (§12) restent **au même niveau** que les confirmations actuelles (niveau "Confirmation tracking" dans la hiérarchie), rendus par-dessus le display normal en respectant l'invariant "overlays ne clearent jamais le fond" (§2 Flow 3 du briefing).

Cas de collision :
- Plusieurs events simultanés → le plus récent déclenché préempte l'actif (comportement actuel préservé).
- Event + battery low burst → le battery low continue à blinker par-dessus l'event en cours (cas rare, acceptable).
- WAITING_\* en cours + tentative de gesture destructeur (CLEAR long-press, slot save, etc.) → **interdit** par la logique métier (§17 spec LOOP). Aucune collision LED.

---

## Partie 5 — Couverture par mode

### §15 — Display normal NORMAL

Un seul state significatif (les banks NORMAL n'ont ni pile, ni engine, ni play/stop — elles se bornent à recevoir des presses de pad).

| State × FG/BG | Fond pattern | Fond color | Overlay |
|---|---|---|---|
| FG | `SOLID` 85% | `CSLOT_MODE_NORMAL` | — |
| BG | `SOLID` 85% × `bgFactor` | `CSLOT_MODE_NORMAL` | — |

### §16 — Display normal ARPEG

| State × FG/BG | Fond pattern | Fond color | Overlay |
|---|---|---|---|
| FG EMPTY (pile vide) | `SOLID` 25% | `CSLOT_MODE_ARPEG` | — |
| FG STOPPED-loaded | `PULSE_SLOW` 25↔55% periodMs=2500 | `CSLOT_MODE_ARPEG` | — |
| FG PLAYING | `SOLID` 70% | `CSLOT_MODE_ARPEG` | `FLASH` tick 30ms par step, couleur `CSLOT_VERB_PLAY` (vert), fgPct=100% |
| FG WAITING_\* | `CROSSFADE_COLOR` 800ms | colorA = `CSLOT_MODE_ARPEG`, colorB = `CSLOT_VERB_PLAY` | — |
| BG EMPTY | `SOLID` 25% × `bgFactor` | `CSLOT_MODE_ARPEG` | — |
| BG STOPPED-loaded | `PULSE_SLOW` × `bgFactor` | `CSLOT_MODE_ARPEG` | — |
| BG PLAYING | `SOLID` 70% × `bgFactor` | `CSLOT_MODE_ARPEG` | `FLASH` tick 30ms, `bgPct=55%` |
| BG WAITING_\* | `CROSSFADE_COLOR` × `bgFactor` | idem FG | — |

### §17 — Display normal LOOP

| State × FG/BG | Fond pattern | Fond color | Overlay |
|---|---|---|---|
| FG EMPTY | `SOLID` 25% | `CSLOT_MODE_LOOP` | — |
| FG STOPPED-loaded | `PULSE_SLOW` 25↔55% periodMs=2500 | `CSLOT_MODE_LOOP` | — |
| FG PLAYING | `SOLID` 70% | `CSLOT_MODE_LOOP` | `FLASH` tick 80ms par wrap, couleur `CSLOT_VERB_PLAY` (vert), fgPct=100% |
| FG RECORDING | `SOLID` 70% | `CSLOT_MODE_LOOP` | `FLASH` tick 80ms par bar, couleur `CSLOT_VERB_REC` (rouge), fgPct=100% |
| FG OVERDUBBING | `SOLID` 70% | `CSLOT_MODE_LOOP` | `FLASH` tick 80ms par wrap, couleur `CSLOT_VERB_OVERDUB` (orange), fgPct=100% |
| FG WAITING_\* | `CROSSFADE_COLOR` 800ms | colorA = `CSLOT_MODE_LOOP`, colorB = `CSLOT_VERB_PLAY` | — |
| BG EMPTY | `SOLID` 25% × `bgFactor` | `CSLOT_MODE_LOOP` | — |
| BG STOPPED-loaded | `PULSE_SLOW` × `bgFactor` | `CSLOT_MODE_LOOP` | — |
| BG PLAYING | `SOLID` 70% × `bgFactor` | `CSLOT_MODE_LOOP` | `FLASH` tick 80ms, `bgPct=55%` |
| BG RECORDING / OVERDUBBING | — | — | **Impossible** (bank switch refusé pendant capture, §23 spec LOOP) |
| BG WAITING_\* | `CROSSFADE_COLOR` × `bgFactor` | idem FG | — |

Remarque sur les ticks distincts playing/rec/overdub : la différence de couleur (vert/rouge/orange) à 100% fg et 55% bg rend les 3 états immédiatement distinguables, même entre banks LOOP BG simultanées (hypothèse : une PLAYING et une STOPPED). La scannabilité de groupe est garantie.

### §18 — Gestion du wrap LOOP

Le tick wrap LOOP est analogue au tick step ARPEG mais fire à une fréquence différente :
- ARPEG : 1 tick par step (selon division de la bank).
- LOOP : 1 tick par wrap de la boucle pendant PLAYING, 1 tick par bar pendant RECORDING, 1 tick par wrap pendant OVERDUBBING.

Implémentation : même mécanisme que `consumeTickFlash()` d'ArpEngine — un flag `consumeWrapFlash()` dans LoopEngine, consommé par LedController.

Durée du FLASH : 30ms pour ARPEG step (plus court car plus fréquent), 80ms pour LOOP wrap/bar/rec/od (moins fréquent, doit être plus visible). Paramètres séparés dans `LedSettingsStore`.

---

## Partie 6 — Intégration Tool 8

### §19 — Refactor en 3 pages thématiques

Le Tool 8 actuel est organisé en 2 pages (COLOR+TIMING / CONFIRM) avec ~20 params à plat. Il devient :

**Page 1 — PATTERNS** : édition des 9 patterns de la palette.
Chaque pattern est une card avec ses params propres. Structure verticale "une ligne par pattern, expansion horizontale des params".

```
PATTERN                PARAMS
─────────────────────────────────────────────────────
SOLID                  pct
PULSE_SLOW             minPct   maxPct   periodMs
CROSSFADE_COLOR        periodMs
BLINK_SLOW             onMs     offMs    cycles
BLINK_FAST             onMs     offMs    cycles
FADE                   durationMs  (start/end portés par les events)
FLASH                  durationMs  fgPct  bgPct
RAMP_HOLD              (rampMs : dérivé timer — lecture seule)
                       suffix: onMs  gapMs  cycles
SPARK                  onMs     gapMs
```

Le bgFactor global apparaît en début de page (param unique, applicable aux fonds).

**Page 2 — COLORS** : édition des 16 color slots.
Identique à la structure actuelle (preset + hueOffset par slot), avec la nouvelle nomenclature (MODE_\*, VERB_\*, BANK_SWITCH, SCALE_\*, OCTAVE, CONFIRM_OK). Livrée en grille chromatique pour voir la famille en un coup d'œil.

**Page 3 — EVENTS** : édition du mapping event → tuple.
Une card par event. Chaque card contient exactement 3 champs éditables : `pattern`, `color_slot`, `fgPct`. Pour les events avec couplage timer, un 4e champ en lecture seule affiche la `rampMs` dérivée.

```
EVENT              PATTERN         COLOR SLOT         FG%   (RAMP FROM TIMER)
──────────────────────────────────────────────────────────────────────────────
BANK_SWITCH        BLINK_SLOW      BANK_SWITCH        100%
SCALE_ROOT         BLINK_FAST      SCALE_ROOT         100%
…
CLEAR_LOOP         RAMP_HOLD       VERB_CLEAR_LOOP    100%  (500ms from Tool 6)
LOOP_SLOT_WRITTEN  RAMP_HOLD       VERB_SAVE          100%  (1000ms from Tool 6)
…
```

Navigation entre pages : touche dédiée (par ex. `p` / `c` / `e` en plus du cycle existant, à aligner avec les conventions VT100 du projet).

### §20 — Couples structurants dans l'UI

La card event est le couple fondamental : `{pattern, color, brightness}`. Trois champs, toujours. Le musicien édite un event en voyant immédiatement ces 3 dimensions liées.

Les patterns (Page 1) sont des couples internes par pattern : un pattern = son groupe de params interne (par ex. `BLINK_FAST` = `{onMs, offMs, cycles}` = un couple de 3 params cohérents). Chaque pattern a sa card avec ses params groupés.

Les color slots (Page 2) sont des couples `{preset, hueOffset}` — existant, conservé.

### §21 — Live preview

L'existant `previewSetPixel` (Tool 7 utilise LED 3-4) est étendu au Tool 8 refondu. Chaque card de Page 1 / Page 3 en édition active une preview live du pattern sur LED 3-4 : le musicien voit immédiatement l'effet.

Paramètre visible : touche `b` (preview blink) actuelle. Conservée, fonctionne avec tous les patterns de la palette.

### §22 — Migration NVS (Zero Migration Policy)

Application stricte de la policy CLAUDE.md (§NVS & Persistence — Zero Migration Policy). Les changements de struct entraînent un reset aux défauts au premier boot post-flash :

- `LedSettingsStore` : version bump (4+), restructuré pour refléter les 9 patterns + bgFactor global. Les champs legacy (`fgArpPlayMin`, `bgArpStopMax`, `bgArpPlayMax`) retirés.
- `ColorSlotStore` : `COLOR_SLOT_COUNT` 12 → 16, version bump 3 → 4 (version actuelle : `COLOR_SLOT_VERSION = 3` dans `KeyboardData.h`).

Conséquence acceptée : au premier boot après flash, tous les réglages LED (intensités, durées, couleurs) sont reset. Le musicien reconfigure via Tool 8 refondu. `Serial.printf` warning au boot signale chaque reset.

---

## Partie 7 — Ordre d'implémentation recommandé

### §23 — Phase 0 : Refactor LED isolé (avant LOOP core)

**Recommandation** : refactoriser le système LED dans sa cible finale **avant** d'implémenter le mode LOOP. Les events LOOP (§12) arrivent dans un système déjà unifié — zéro dette.

**Scope Phase 0** :

1. Refonte interne de `LedController` autour du modèle 3-couches :
   - Machine à état interne qui consomme (event_id, pattern_id, color, brightness, timer)
   - Moteur de rendering par pattern (SOLID, PULSE_SLOW, CROSSFADE_COLOR, BLINK_SLOW/FAST, FADE, FLASH, RAMP_HOLD, SPARK)
   - Retrait de la logique hardcoded actuelle (sine FG-stopped-loaded, fade HOLD_ON/OFF)

2. Refonte `LedSettingsStore` (version bump) :
   - Groupes de params par pattern
   - Retrait des champs legacy unused
   - Ajout `bgFactor` global
   - Suppression des durées dérivées du timer métier (passe en référence vers `SettingsStore`)

3. Extension `ColorSlotStore` (version bump) :
   - 12 → 16 slots
   - Nouvelle nomenclature alignée sur la grammaire (MODE_\*, VERB_\*, CONFIRM_OK, etc.)

4. Refactor Tool 8 :
   - 3 pages (PATTERNS / COLORS / EVENTS)
   - Cards event avec 3 champs exposés
   - Navigation et preview intégrés

5. Migration des events existants :
   - `BANK_SWITCH`, `SCALE_ROOT/MODE/CHROM`, `OCTAVE` migrés vers `BLINK_SLOW`/`BLINK_FAST` de la palette
   - `HOLD_ON`/`HOLD_OFF` fusionnés en events PLAY/STOP (mêmes patterns, couleur verb)
   - Tick step ARPEG migré vers pattern `FLASH` via table de mapping
   - Fond ARPEG migré vers combinaison (SOLID/PULSE_SLOW) selon state

6. Tests manuels :
   - Bank switch visuel : confirm BLINK_SLOW white sur FG
   - Scale root/mode/chrom : BLINK_FAST couleur distincte, masque scale group préservé
   - Octave : BLINK_FAST violet
   - HOLD_ON (ARPEG capture) : FADE 0→70% vert sur target bank (FG ou BG)
   - HOLD_OFF : FADE 70→0% vert
   - ARPEG playing : tick 30ms vert sur fond bleu
   - Boot, setup comet, battery gauge, bargraphs : inchangés

**Pas touché en Phase 0** : les engines (ArpEngine, MidiEngine, pressure pipeline), les managers (BankManager, ScaleManager, ControlPadManager), les autres tools (1-7).

### §24 — Phase suivante : LOOP core avec grammaire

Une fois Phase 0 mergée, le plan LOOP (phases 1-6 de §27 spec LOOP) peut commencer. Les nouveaux events LOOP (§7, §12) sont ajoutés à la table event_rendering sans modifier la grammaire. Le rendering bénéficie de la palette dès le jour 1.

### §25 — Option de repli

Si la refonte `LedController` s'avère plus complexe que prévu (par ex. interaction entre RAMP_HOLD en cours + battery low burst + bank state simultanés révèle une machine à état difficile à implémenter), on bascule sur l'option (c) du prompt : implémenter LOOP avec patterns ad-hoc hardcoded, puis grand refactor après LOOP stabilisé. La dette temporaire est acceptable comme chemin de repli, pas comme plan nominal.

---

## Partie 8 — Questions ouvertes

### §26 — Décisions à confirmer avant début d'implémentation

| # | Question | Proposition | Statut |
|---|---|---|---|
| 1 | Valeur précise de `bgFactor` global | 20-25% (à régler sur hardware pour voir le BG sans le noyer) | À valider sur prototype |
| 2 | `slotClearTimerMs` (delete combo slot) | 800 ms proposé (entre les 500 ms du CLEAR loop et les 1000 ms du slot save) | À valider avec le musicien |
| 3 | `CSLOT_BANK_SWITCH` exacte (cool white ? ice blue ?) | Cool White proposé pour maximum contraste avec warm white NORMAL | À valider sur hardware |
| 4 | Touches de navigation Tool 8 (3 pages) | `p` / `c` / `e` ou flèches ? | À aligner avec conventions VT100 projet |
| 5 | Preview LED 3-4 en Tool 8 vs 2 LEDs dédiées | LED 3-4 (même que Tool 7) pour cohérence | À acter |
| 6 | Couleurs hue offset des slots par défaut | Tous à 0 (presets purs) sauf si musicien préfère nuances | À valider sur hardware |
| 7 | Stockage NVS des timers dérivés (couplage §13) | **Impact spec LOOP** : `clearLoopTimerMs` (500ms), `slotSaveTimerMs` (1000ms), `slotClearTimerMs` (800ms proposé) sont aujourd'hui implicites (spec LOOP §9, §11, §13). Ce design exige qu'ils vivent en NVS pour respecter zéro-hardcode. Proposition : ajout à `SettingsStore` (Tool 6) avec validator + bump version. Alternative : store dédié "LoopTimersStore" si refactor Tool 5 Bank Config le justifie. | À trancher avec auteur spec LOOP |

### §27 — Questions déférées au plan d'implémentation

Le plan d'implémentation Phase 0 (séparé) devra préciser :
- Signatures exactes du `LedController` refactorisé
- Structure exacte de `LedSettingsStore` vN
- Refactor mechanics de Tool 8 (parseur de navigation, conservation du live preview)
- Points d'injection pour la grammaire dans `main.cpp` (handlers de bank switch, scale change, octave, play/stop ARPEG)
- Tests manuels à effectuer avec la `ItermCode/vt100_serial_terminal.py`

---

**Fin du document.** Relecture et validation attendues. Une fois approuvé, ce document devient la source de vérité pour §21 de la spec LOOP et le référentiel de la Phase 0 d'implémentation à venir.
