# ILLPAD48 V2 — Viewer JUCE unifié (runtime + setup VT100) : design haut niveau

**Date** : 2026-05-15
**Statut** : **VALIDÉ** pour plan d'implémentation. Brainstorm conclu cette session.
**Scope** : nouvelle application desktop macOS standalone, écrite en JUCE, qui héberge à la fois un dashboard runtime (lecture des paramètres pots, banks, events de l'ILLPAD en direct) et le setup VT100 embarqué (remplace `vt100_serial_terminal.py` + iTerm2). Trajectoire V3 directe (pas de V1 intermédiaire). Inclut une extension côté firmware ILLPAD acquise pendant le brainstorm (~90 lignes, sans refacto).

**Sources** :
- État du code `main` au 2026-05-15.
- [`.claude/CLAUDE.md`](../../../.claude/CLAUDE.md) — invariants projet, politique zéro-migration NVS.
- [`src/main.cpp`](../../../src/main.cpp), [`src/managers/BankManager.cpp`](../../../src/managers/BankManager.cpp), [`src/managers/PotRouter.cpp`](../../../src/managers/PotRouter.cpp), [`src/managers/ScaleManager.cpp`](../../../src/managers/ScaleManager.cpp), [`src/arp/ArpEngine.cpp`](../../../src/arp/ArpEngine.cpp), [`src/core/KeyboardData.h`](../../../src/core/KeyboardData.h), [`src/core/MidiTransport.cpp`](../../../src/core/MidiTransport.cpp), [`src/midi/ClockManager.cpp`](../../../src/midi/ClockManager.cpp), [`src/managers/NvsManager.cpp`](../../../src/managers/NvsManager.cpp).
- [`ItermCode/vt100_serial_terminal.py`](../../../ItermCode/vt100_serial_terminal.py) — terminal Python existant (sera remplacé).
- Repo `CirclePI` (référence locale du workflow JUCE de Loïc) — convention de cadrage CLAUDE.md pour code JUCE, structure `cpp/` desktop standalone.
- Conversation de brainstorm 2026-05-15 (cette session).

---

## Partie 1 — Cadre

### §1 — Intention

Le ILLPAD48 V2 émet sur son port USB CDC un flux serial structuré pendant le jeu live : valeurs des paramètres pots routés, switches de bank, état arpégiateurs, changements de scale, source d'horloge, état BLE/USB MIDI. Aujourd'hui ce flux n'est consultable que via `pio device monitor` (texte brut, peu lisible en live) ou via `ItermCode/vt100_serial_terminal.py` lors du setup mode boot.

L'intention : disposer d'**une seule application desktop macOS standalone**, lançable indépendamment d'Ableton, qui agit comme un cockpit de monitoring permanent au-dessus du DAW pendant les performances. Cette application unifie aussi le **setup mode VT100** (calibration, mapping pots, configuration banks) qui aujourd'hui passe par iTerm2 + script Python — soit deux outils externes pour une expérience qui devrait être intégrée.

Le viewer ne contrôle rien sur l'ILLPAD pendant le runtime (read-only). En setup mode il transmet uniquement les touches utilisateur (flèches, entrée, etc.) au firmware comme le ferait n'importe quel terminal.

### §2 — État actuel et docs concernées

- `vt100_serial_terminal.py` (401 lignes) délègue tout le rendu VT100 à iTerm2. Il fait du bridging serial ↔ stdin/stdout, gère le DEC 2026 sync, le auto-resize via `CSI 8 t`, les touches atomiques. Sera remplacé par le viewer JUCE — pas modifié, archivé après V3.
- Le firmware émet déjà sur serial des lignes structurées préfixées par catégorie (`[INIT]`, `[BANK]`, `[POT]`, `[ARP]`, `[GEN]`, `[SCALE]`, `[CLOCK]`, `[MIDI]`, `[PANIC]`, `[HW]`, `[KB]`). Toutes sont sous `#if DEBUG_SERIAL` (activé par défaut, `HardwareConfig.h:10`). `[HW]` sous `#if DEBUG_HARDWARE` (désactivé par défaut).
- Setup mode actuel : boot-only via maintien du bouton arrière, sortie = reboot. Voir [`docs/reference/setup-tools-conventions.md`](../../reference/setup-tools-conventions.md) et [`docs/reference/vt100-design-guide.md`](../../reference/vt100-design-guide.md).

Aucune doc reference existante ne couvre la couche viewer — c'est une nouvelle surface.

### §3 — Périmètre

**Ce document couvre** :
- L'extension de l'API serial du firmware (modifs acquises pendant le brainstorm) : dump initial des 8 banks, dump d'état contextuel au bank switch, annotation des `[POT]` avec le slot d'origine, handler runtime de re-dump à la demande.
- L'architecture de l'app JUCE V3 (couches 0-3, threading, sélection des libs externes).
- L'UX et le UI complet (comportement fenêtre, layout runtime, layout setup VT100 embarqué, look & feel, palette, font, sizing, resize scale-to-fit).
- La gestion bidirectionnelle du port serial (lecture event-driven + écriture pour les touches setup).
- La détection automatique du mode (runtime vs setup) et la bascule entre les deux dans la même app.
- L'intégration libserialport (auto-detect VID/PID Espressif) et libvterm (émulateur VT100 production-grade).
- La distribution macOS, persistance config, signing.
- La couverture de tests (unit tests Layer 1, smoke test E2E manuel).

**Ce document ne couvre pas** :
- Le code, les signatures de fonctions, le pseudocode d'implémentation.
- Le découpage par phase d'implémentation (sera le `plan` qui suit cette spec).
- Les valeurs exactes de constantes (timeouts, intervalles, etc.) — déléguées au plan.
- La V2 du viewer (animations avancées, glow, light theme, item menu bar complémentaire, notifications macOS natives).
- Le portage Windows / Linux (architecture cross-platform en théorie, mais cible Mac d'abord).

### §4 — Trajectoire

Deux trajectoires discutées :
- **A — V1 viewer runtime seul d'abord, puis V3 ajout setup VT100** : feedback rapide, mais V1 reworké pour insérer le ModeSwitcher rétroactivement.
- **B — V3 direct** : une seule passe d'architecture, pas de livrable intermédiaire, durée totale plus courte.

**Choisi : B**. L'architecture est dessinée d'emblée pour héberger les deux modes via un `ModeSwitcher` racine. Pas de viewer V1 livré séparément.

---

## Partie 2 — Extension API serial (firmware)

### §5 — Statut des modifs firmware

Toutes les modifs ci-dessous sont **acquises** pour V3 (consensus brainstorm). Coût total côté firmware : ~90 lignes, zéro refacto, pas de bump de struct NVS, pas de modif du PotRouter au-delà d'un getter, pas de modif du parser setup mode. Une **porte ouverte** reste si l'intégration libvterm révèle un besoin micro (caractère spécifique, etc.) — à acter au cas par cas avec justification.

### §6 — Ce qui sort déjà et reste inchangé

Tous les events listés ci-dessous sortent aujourd'hui dans le format indiqué. Le viewer les consomme comme events incrémentaux qu'il applique sur son état local.

| Préfixe | Format | Trigger |
|---|---|---|
| `[BANK]` | `Bank N (ch N, NORMAL\|ARPEG\|ARPEG_GEN\|LOOP)` | switch de bank en runtime |
| `[SCALE]` | `Root NAME (mode NAME)` / `Mode NAME (root NAME)` / `Chromatic (root NAME)` | appui pad de scale |
| `[ARP]` | `Bank N: +note (M total)` / `-note (M total)` | pile arp grossit/réduit |
| `[ARP]` | `Bank N: Play (pile M notes)` / `Play — relaunch paused pile (M notes)` | démarrage arp |
| `[ARP]` | `Bank N: Stop — pile kept (M notes)` / `Stop — fingers down, pile cleared` | arrêt arp |
| `[ARP]` | `Octave N` | changement octave (bank ARPEG) |
| `[ARP_GEN]` | `MutationLevel N` | changement mutation (bank ARPEG_GEN) |
| `[ARP]` | `WARNING: Event queue full — event dropped` | saturation queue |
| `[GEN]` | `seed seqLen=N (pile=1 note D, repetition)` / `seed seqLen=N E_init=N pile=N lo=D hi=D` | seed séquence générative |
| `[CLOCK]` | `Source: USB\|BLE\|internal (...)` | bascule source horloge |
| `[MIDI]` | `BLE connected\|disconnected`, `USB connected\|disconnected` | état transport |
| `[PANIC]` | `All notes off on all channels` | triple-click rear button < 600 ms |

### §7 — Modifié — annotation slot dans `[POT]`

Aujourd'hui :
```
[POT] Tempo=120 BPM
[POT] BaseVel=80
```

Après modif :
```
[POT] R1: Tempo=120 BPM
[POT] R1H: BaseVel=80
```

Format : `[POT] <SLOT>: <param>=<value> [unit]` où `<SLOT>` ∈ {`R1`, `R1H`, `R2`, `R2H`, `R3`, `R3H`, `R4`, `R4H`}. Reverse-lookup dans `PotRouter._mapping` du slot pointant vers le `PotTarget` qui vient de changer, dans le contexte courant (NORMAL ou ARPEG). Coût : ~15 lignes, plus un nouveau getter `PotRouter::getSlotForTarget(PotTarget)`.

Bénéfice : le viewer sait dans quel cadran de la grille 8 slots placer la valeur sans deviner.

### §8 — Ajouté — dump `[BANKS]` au boot

Émis **une fois** après `[INIT] Ready.`.

Format multi-lignes :
```
[BANKS] count=8
[BANK] idx=1 type=NORMAL    ch=1 group=A
[BANK] idx=2 type=NORMAL    ch=2 group=A
[BANK] idx=3 type=ARPEG     ch=3 group=B division=1/8  playing=true  octave=2
[BANK] idx=4 type=ARPEG     ch=4 group=B division=1/16 playing=false octave=3
[BANK] idx=5 type=ARPEG_GEN ch=5 group=0 division=1/4  playing=true  mutationLevel=2
[BANK] idx=6 type=NORMAL    ch=6 group=A
[BANK] idx=7 type=LOOP      ch=7 group=0
[BANK] idx=8 type=ARPEG     ch=8 group=B division=1/2  playing=false octave=1
```

Détail des champs :

| Champ | Présent si | Source |
|---|---|---|
| `idx` | toujours | 1..8 |
| `type` | toujours | `BankSlot::type` |
| `ch` | toujours | canal MIDI 1..8 |
| `group` | toujours | `NvsManager::getLoadedScaleGroup()` — 0 = none, 1..4 = A..D |
| `division` | type ∈ {ARPEG, ARPEG_GEN} | nom court selon `_potRouter._division` per-bank |
| `playing` | type ∈ {ARPEG, ARPEG_GEN} | `ArpEngine::isPlaying()` |
| `octave` | type = ARPEG | `ArpEngine` octave courante |
| `mutationLevel` | type = ARPEG_GEN | `ArpEngine` mutation courante |

Le viewer construit son tableau ALL BANKS à partir de ce dump. Coût : ~20 lignes (lecture directe d'état déjà en mémoire).

### §9 — Ajouté — dump `[STATE]` au boot et au bank switch

Émis au boot après le dump `[BANKS]`, et après chaque ligne `[BANK]` (= à chaque switch de bank en runtime).

Format atomique sur une ligne :
```
[STATE] bank=3 mode=ARPEG ch=3 scale=D:Minor octave=2 R1=Tempo:120 R1H=BaseVel:80 R2=Gate:0.50 R2H=VelVar:20 R3=Division:1/8 R3H=PitchBend:64 R4=Pattern:Up R4H=ShufDepth:0.30
```

Détail des champs :

| Champ | Notes |
|---|---|
| `bank` | 1..8 |
| `mode` | `NORMAL\|ARPEG\|ARPEG_GEN\|LOOP` |
| `ch` | 1..8 |
| `scale` | `ROOT:MODE` (ex `C:Major`) ou `Chromatic:ROOT` (ex `Chromatic:D`) |
| `octave` | présent si mode=ARPEG (1..4) |
| `mutationLevel` | présent si mode=ARPEG_GEN (1..4) |
| `Rx` / `RxH` | 8 slots, chacun `TARGET:VALUE` ou `---` si slot vide |

Le `TARGET` utilise le nom court déjà émis dans les `[POT]` (Tempo, Gate, Division, Pattern, etc.). La `VALUE` suit le format actuel des `[POT]` (numérique, fraction, string énum). Coût : ~30 lignes.

### §10 — Ajouté — handler runtime de request state

Le firmware accepte en runtime 3 commandes texte courtes lues sur le port serial (le setup mode étant boot-only, le port serial est libre en runtime — pas de conflit avec `InputParser`).

| Commande reçue | Réponse |
|---|---|
| `?STATE\n` | Ré-émet la ligne `[STATE]` courante |
| `?BANKS\n` | Ré-émet le dump `[BANKS] count=8 ...` |
| `?BOTH\n` | Émet les deux (banks puis state) |

Implémentation : ~20 lignes dans la boucle principale, lecture `Serial.available()` + buffer jusqu'au `\n` + comparaison exacte des 3 strings. Pas d'autre commande. Pas de parser bidirectionnel "intelligent" — 3 requêtes idempotentes uniquement.

Bénéfice : le viewer peut se connecter à un ILLPAD déjà booté et reconstituer son état complet sans attendre un bank switch ou un pot move. Aussi : bouton "Resync" dans l'UI viewer pour les rares cas de désynchronisation.

### §11 — Coût total et bande passante

- **Coût ajouté côté firmware** : ~90 lignes (15 annotation `[POT]` + 20 dump `[BANKS]` + 30 dump `[STATE]` + 20 handler request + ~5 ajustements). Zéro refacto.
- **Bande passante à 115200 baud (~14 KB/s)** :
  - `[BANKS]` au boot : ~700 octets, une fois → négligeable.
  - `[STATE]` au bank switch : ~200 octets, 1-2/sec max → < 1 %.
  - `[POT]` annoté : ~30 octets, 10-30/sec max → < 1 %.
  - Events sporadiques : négligeable.
- Très loin de saturer.

---

## Partie 3 — Architecture JUCE V3

### §12 — Couches

```
┌──────────────────────────────────────────────────────────────┐
│ Layer 3 — View (JUCE Components, MessageThread)              │
│   MainComponent → ModeSwitcher                                │
│     ├─ RuntimeMode → HeaderBar, TransportBar,                 │
│     │                CurrentBankPanel + 8× PotCell,           │
│     │                AllBanksPanel + 8× BankRow,              │
│     │                EventLogPanel                            │
│     └─ SetupMode   → AppChrome + TerminalView (VT100 fidèle) │
│                       + KeyboardInput → output queue          │
├──────────────────────────────────────────────────────────────┤
│ Layer 2 — Model                                              │
│   RuntimeModel : DeviceState, BanksInfo[8], CurrentBankState,│
│                  PotSlots[8], EventBuffer                    │
│   TerminalModel : VTerm screen state (size, cursor, attrs,   │
│                   damage rects)                              │
│   ModeDetector : examine bytes/lignes, décide actif/cible    │
├──────────────────────────────────────────────────────────────┤
│ Layer 1 — Parsers (co-existants, routés par ModeDetector)    │
│   RuntimeParser : ligne [TAG] payload → ParsedEvent variant  │
│   TerminalDriver : bytes bruts → vterm_input_write (libvterm)│
│   ITermSeqInterceptor : pré-parser DEC 2026 + CSI 8 t        │
├──────────────────────────────────────────────────────────────┤
│ Layer 0 — SerialReader (thread dédié, libserialport)         │
│   - sp_get_port_by_name ou auto-detect VID/PID Espressif     │
│   - sp_wait event-driven (latence ~1 ms)                     │
│   - Bidirectionnel : sp_blocking_read + sp_nonblocking_write │
│   - Output queue thread-safe (UI → serial pour setup keys)   │
│   - Reconnect auto sur ENODEV / disconnect                   │
└──────────────────────────────────────────────────────────────┘
```

### §13 — Threading

- **MessageThread** (JUCE standard) : Layer 2 et 3 (Model et UI). Règle d'or JUCE : toute manipulation Component sur MessageThread uniquement.
- **SerialThread** (un `juce::Thread` custom) : Layer 0. Lecture event-driven via `sp_wait`. Écriture déclenchée par drain de l'output queue.
- **Communication Layer 0 → Layer 2** : queue thread-safe (mutex simple OK en V1, ~10-30 events/sec max). Un `juce::AsyncUpdater` côté Layer 2 draine la queue sur le MessageThread, parse chaque ligne ou byte chunk via Layer 1, applique sur le Model, notifie les Listeners.
- **Communication Layer 3 → Layer 0** (mode setup, touches user) : output queue thread-safe distincte. Layer 3 push, Layer 0 SerialThread la draine à chaque itération.

### §14 — Layer 0 — SerialReader

**Lib : libserialport (sigrok)**. Raisons :
- Mature et éprouvée à grande échelle (OpenOCD, PulseView, sigrok).
- API `sp_wait` nativement event-driven → latence fine indispensable pour la nav VT100 en setup.
- Énumération des ports par VID/PID → auto-détection ILLPAD au démarrage de l'app.

**Auto-detect** :
1. Au démarrage et à chaque tentative de reconnect : `sp_list_ports()`.
2. Pour chaque port USB : `sp_get_port_usb_vid_pid()` → match `VID = 0x303A` (Espressif Systems). PID exact à vérifier sur le matos en intégration (typiquement `0x1001` pour CDC ESP32-S3).
3. Si exactement un match → connect direct.
4. Sinon, prefer la dernière sélection persistée (`lastSerialPort` dans config).
5. Sinon UI demande choix manuel via menu déroulant.

**Boucle de lecture** :
```
while (running) {
    sp_event_set *evts = ...;
    sp_wait(evts, timeout_ms);       // event-driven, dort vraiment
    int n = sp_nonblocking_read(port, buf, BUF_SIZE);
    if (n > 0) { route_bytes_to_layer1(buf, n); }
    drain_output_queue();
    handle_errors();                  // ENODEV → mark disconnected, reconnect loop
}
```

**Reconnect** : sur `ENODEV` (port disparu) ou erreur I/O, le Layer 0 marque l'état `disconnected` via le Model. UI passe en `●` orange "Reconnecting". Tentative de réouverture toutes les 1 sec (même nom de port en priorité, fallback auto-detect si autre VID/PID Espressif disponible). Succès → `●` vert "Connected" + envoi automatique de `?BOTH\n`.

**Bidirectionnel** : `sp_nonblocking_write` pour les sequences de touches en mode setup. Chaque touche encodée en escape sequence est poussée **atomiquement** (un seul `sp_nonblocking_write` par séquence — pas de splitting de bytes).

### §15 — Layer 1 — RuntimeParser

Pure C++17, aucune dépendance JUCE. Fonctions pures, testables isolément.

Forme :
```
ParsedEvent parseLine(std::string_view line);

using ParsedEvent = std::variant<
  BanksHeader, BankInfo, StateEvent,
  PotEvent, BankSwitchEvent, ScaleEvent,
  ArpEvent, ArpGenEvent, GenEvent,
  ClockEvent, MidiEvent, PanicEvent,
  UnknownEvent
>;
```

Parsing manuel (split sur espaces + `:` + `=`), pas de regex pour la perf et la simplicité du debug. Les formats sont stables et bien définis dans §6-§10. Estimation ~300 lignes pour couvrir l'ensemble.

### §16 — Layer 1 — TerminalDriver (libvterm)

**Lib : libvterm (neovim)**. Raisons :
- Production-grade, utilisé par neovim et plusieurs émulateurs de terminal modernes.
- API callback (damage rects, cursor move, scroll region, term props) → s'intègre naturellement à JUCE.
- Pure C, vendoré ou linké en lib externe.
- Couverture xterm-compatible large (le firmware ILLPAD émet du xterm-compatible majoritairement).

Setup typique :
```
VTerm *vt = vterm_new(rows, cols);
vterm_set_utf8(vt, 1);
VTermScreen *scr = vterm_obtain_screen(vt);
VTermScreenCallbacks cbs = {
    .damage      = on_damage,        // → invalidate JUCE rect
    .moverect    = on_moverect,      // → scroll region
    .movecursor  = on_movecursor,
    .settermprop = on_termprop,
    .bell        = on_bell,
    .resize      = on_resize,
};
vterm_screen_set_callbacks(scr, &cbs, this);
vterm_screen_reset(scr, 1);
```

À chaque chunk reçu en mode SETUP : passe par `ITermSeqInterceptor` (cf §17) puis `vterm_input_write(vt, bytes, len)`. libvterm invoque les callbacks. La TerminalView Layer 3 lit ensuite `vterm_screen_get_cell(scr, pos, &cell)` pour redessiner les zones invalidées.

### §17 — Layer 1 — ITermSeqInterceptor

Pré-parser placé en amont de `vterm_input_write`. Identifie et intercepte 3 sequences iTerm-spécifiques que libvterm ne traite pas correctement :

| Séquence | Sens | Action |
|---|---|---|
| `ESC[?2026h` / `ESC[?2026l` | DEC 2026 synchronized output (begin/end atomic frame) | Buffer en bytes entre les deux, flush en un seul `repaint()` JUCE à `2026l`. Évite le tearing visuel. |
| `CSI 8 ; rows ; cols t` | ITERM_RESIZE — firmware demande dimensions logiques du terminal | Mettre à jour la grille logique du VTerm via `vterm_set_size(rows, cols)`. **La fenêtre OS ne change pas de taille** — c'est l'utilisateur qui contrôle la fenêtre. Les cellules sont retaillées dynamiquement pour remplir la TerminalView (cf §31). |
| `OSC 1337 ; ProgressBar=end ST` | ITERM_PROGRESS_END | Ignorer (cosmétique). |

Les autres séquences passent telles quelles à libvterm. ~80-100 lignes.

### §18 — Layer 2 — Modèle

**RuntimeModel** (active en mode RUNTIME) :
- `DeviceState` : tempo BPM, clock source enum (USB/BLE/INTERNAL), BLE MIDI connected bool, USB MIDI connected bool, last panic timestamp.
- `BanksInfo[8]` : pour chaque bank, type, channel, group (0..4), scale root+mode (string), division enum (si arp), playing bool (si arp), octave int (si ARPEG), mutationLevel int (si ARPEG_GEN).
- `CurrentBankState` : idx, type, ch, scale, octave/mutationLevel, et `PotSlot slots[8]` (R1, R1H, R2, R2H, R3, R3H, R4, R4H) chacun {slotName, target enum, displayValue string, isEmpty bool, lastChangeTimestamp pour highlight transitoire}.
- `EventBuffer` : ring buffer ~500 lignes, chaque entrée {timestamp, catégorie, résumé string}.

**TerminalModel** (active en mode SETUP) : encapsule libvterm `VTerm *` + `VTermScreen *`, expose accesseurs (cell at pos, cursor pos, screen size, attributes). Damage tracking via callbacks.

**ModeDetector** : state machine `UNKNOWN → RUNTIME ou SETUP`. Cf §19.

Notification via `juce::ChangeBroadcaster` ou Listener pattern selon granularité (un broadcaster par "axe" : DeviceState, CurrentBank, BanksInfo, EventBuffer, Terminal).

### §19 — ModeDetector

État machine :
- État initial : `UNKNOWN`.
- Si byte vu : `\x1B[` (CSI) ou `\x1B]` (OSC) → bascule `SETUP`.
- Si ligne complète matchant `^\[[A-Z_]+\]` (genre `[INIT]`, `[BANKS]`, `[POT]`) → bascule `RUNTIME`.
- Boot serial garbage : on ignore tous les bytes jusqu'au premier marqueur clair (timer de grâce 500 ms ou jusqu'au premier `\n` propre).
- Loss de port + reconnect → reset à `UNKNOWN`.

Une fois locked dans `SETUP` ou `RUNTIME`, on y reste jusqu'au prochain reboot détecté. Le `ModeSwitcher` Layer 3 écoute le changement de mode et déclenche la transition visuelle (fade-out / fade-in ~200 ms).

### §20 — Stack technique

| Composant | Choix | Version cible |
|---|---|---|
| Framework UI | JUCE | 8.x (ou dernière stable au moment du dev) |
| Serial | libserialport (sigrok) | 0.1.1+ |
| VT100 emulator | libvterm (neovim) | 0.3.x+ |
| Tests | Catch2 v3 | 3.5+ |
| Build | CMake | 3.22+ |
| Compiler | clang++ via Xcode CLT | C++17 minimum |
| Font UI + terminal | IBM Plex Mono (OFL) | dernière |

Dépendances vendored en sous-module git ou source dans `vendor/` :
- `libserialport` — buildé en static lib via CMake.
- `libvterm` — buildé en static lib via CMake (le project upstream a un Makefile, on l'enveloppe).
- `IBMPlexMono-*.ttf` — embeddé via JUCE BinaryData.

---

## Partie 4 — UX et UI

### §21 — Comportement de fenêtre

- Fenêtre standard macOS : présente dans le Dock, dans App Switcher. Pas un agent invisible.
- **Always-on-top toggleable**, par défaut `ON`. Toggle via menu app + bouton dans header. Cmd-T raccourci.
- Redimensionnable. Taille de départ premier lancement : **900 × 600**.
- Position et taille persistées entre sessions via `PropertiesFile` JUCE (`~/Library/Application Support/ILLPADViewer/state.xml`).
- **Aspect ratio fixé 3:2** par défaut (drag par un coin → préserve le ratio). Pas de mode "ratio libre" en V1.
- **Resize scale-to-fit** : design size logique 900 × 600. Au resize, calcul `scale = min(window.w / 900, window.h / 600)` et application via `MainComponent::setTransform(AffineTransform::scale(scale))`. Polices, frames, padding scalent tous ensemble (cf §28).
- Cmd-H / Cmd-M (hide / minimize) standards macOS.
- Pas de raccourci clavier global show/hide en V1 (Accessibility permissions complexes).

### §22 — Layout runtime

```
┌──────────────────────────────────────────────────────────────┐
│ ILLPAD Viewer    /dev/cu.usbmodem-XXX  ● Connected  BLE: ●  │
│                                          [Resync]  [📌]      │  ← header (44 px)
├──────────────────────────────────────────────────────────────┤
│ ♩ 120 BPM      Clock: ● USB                                  │  ← transport bar (48 px)
├──────────────────────────────────────────────────────────────┤
│ ┌──────────────────────────────────┐  ┌──────────────────┐   │
│ │ BANK 3   ARPEG   ch 3            │  │ ALL BANKS        │   │
│ │ D Minor   octave 2                │  │                  │   │
│ │                                   │  │ ▶ 3 ARP   1/8 ●PLAY│   │
│ │ ┌──────┬──────┬──────┬──────┐    │  │   1 NRM         │   │
│ │ │ R1   │ R2   │ R3   │ R4   │    │  │   2 NRM         │   │
│ │ │Tempo │Gate  │Div   │Pat   │    │  │   4 ARP   1/16 STOP│   │
│ │ │ 120  │ 0.50 │ 1/8  │ Up   │    │  │   5 GEN   1/4 ●PLAY│   │
│ │ ├──────┼──────┼──────┼──────┤    │  │   6 NRM         │   │
│ │ │ R1H  │ R2H  │ R3H  │ R4H  │    │  │   7 LOOP        │   │
│ │ │Velo  │VelVar│PB    │Shuf  │    │  │   8 ARP   1/2 STOP│   │
│ │ │ 80   │  20  │  64  │ 0.30 │    │  ├──────────────────┤   │
│ │ └──────┴──────┴──────┴──────┘    │  │ EVENTS           │   │
│ └──────────────────────────────────┘  │ 12:43 BANK → B3  │   │
│                                       │ 12:42 POT R1=120 │   │
│                                       │ 12:42 SCALE→D Min│   │
│                                       │ 12:41 ARP +note  │   │
│                                       │ …                │   │
│                                       └──────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

**Header (44 px)** : nom app, port serial actif (cliquable → menu de sélection), état connexion (●vert/●orange/●rouge), état BLE MIDI (●connected / ○disconnected), bouton Resync, toggle pin (always-on-top).

**Transport bar (48 px)** : Tempo en gros (28 pt), source horloge avec indicateur coloré. Devient rouge background plein écran pendant 2 sec sur réception `[PANIC]`, puis fade vers normal.

**Panneau bank courante (~⅔ largeur, plein hauteur sous transport bar)** :
- Bandeau identité : numéro de bank en 48 pt (Plex Mono Bold), badge mode coloré par type (cf §25), canal MIDI, ligne scale + octave/mutationLevel (selon mode).
- Grille 8 cadrans 4 colonnes × 2 lignes. Chaque PotCell : slot label (R1, R1H, …) en 10 pt, target name en 11 pt secondary, valeur en 24 pt monospace primary. Slot vide → texte grisé `—`. Cellule récemment modifiée → flash background `#58A6FF` opacity 15 % pendant 300 ms.

**Panneau ALL BANKS (~⅓ largeur, partie haute)** : 8 entrées verticales, chacune `[▶] N TYPE [division] [PLAY/STOP]`. Bank courante surlignée (background `#21262D` + barre verticale gauche 3 px de la couleur du type + ▶ marker). Voir §26 pour Play/Stop.

**Panneau EVENTS (~⅓ largeur, partie basse)** : log scrollant des derniers events parsés (BANK, SCALE, ARP, GEN, CLOCK, MIDI, PANIC, ARP queue full). Format compact : `HH:MM:SS CAT résumé`. Buffer ~500 lignes en mémoire. Auto-scroll vers le bas tant que l'utilisateur ne scroll pas manuellement (pattern terminal classique).

### §23 — Layout setup mode

Lorsque `ModeDetector` bascule en `SETUP`, le `ModeSwitcher` affiche :

```
┌──────────────────────────────────────────────────────────────┐
│ ILLPAD Viewer    /dev/cu.usbmodem-XXX  ● Connected           │
│                  [Exit Setup mode (reboot)]                  │  ← chrome simplifiée (44 px)
├──────────────────────────────────────────────────────────────┤
│                                                              │
│         ┌────────────────────────────────────────┐           │
│         │ ╭─ ILLPAD48 SETUP CONSOLE ──────────╮ │           │
│         │ │                                    │ │           │
│         │ │  TerminalView (rendu VT100 fidèle  │ │           │
│         │ │  cellule par cellule via libvterm  │ │           │
│         │ │  screen state)                     │ │           │
│         │ │                                    │ │           │
│         │ │                                    │ │           │
│         │ ╰────────────────────────────────────╯ │           │
│         └────────────────────────────────────────┘           │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

- Le header app reste visible mais simplifié : port + statut + un bouton "Exit Setup mode (reboot)" qui pourrait envoyer une séquence d'exit si le firmware le supporte (à vérifier dans `InputParser.cpp`), sinon affiche un texte explicatif (l'utilisateur fait Ctrl+] ou Échap au clavier).
- Le transport bar disparaît.
- La TerminalView occupe le reste de la fenêtre avec padding 12 px autour pour ne pas coller aux bords de la chrome.
- Background autour : `#0D1117` (chrome app). Background du terminal : `#000000` pur (fidèle VT100).
- Le focus clavier est automatiquement dirigé vers la TerminalView ; les touches sont encodées en séquences atomiques (cf §27) et poussées dans l'output queue Layer 0.

### §24 — Look & feel — palette globale

Theme dark unique (pas de toggle light en V1). Choix intentionnel : fatigue oculaire moindre en live, contraste fort sur valeurs, intégration discrète au-dessus d'Ableton (qui est lui-même dark).

| Rôle | CSS | Notes |
|---|---|---|
| BG primary | `#0D1117` | fond principal app |
| BG secondary | `#161B22` | panels, sections |
| BG tertiary | `#21262D` | hover, sélection, bank courante surlignée |
| Text primary | `#E6EDF3` | titres, valeurs |
| Text secondary | `#8B949E` | labels, descriptions |
| Border / divider | `#30363D` | séparateurs fins |
| Accent neutre | `#58A6FF` | focus, highlight cell change |

### §25 — Couleurs par type de bank

Palette fixe app (pas de sync firmware en V1, cf §31). Aligne avec les défauts firmware ColorSlot quand applicable.

| Type | CSS | Origine / aligned with |
|---|---|---|
| NORMAL | `#F5DEB3` (warm white / wheat) | `CSLOT_MODE_NORMAL` firmware |
| ARPEG | `#7DD3FC` (ice blue / sky-300) | `CSLOT_MODE_ARPEG` firmware |
| ARPEG_GEN | `#3B82F6` (deep blue / blue-500) | distinction V3, pas de CSLOT firmware |
| LOOP | `#F59E0B` (amber / gold) | `CSLOT_MODE_LOOP` firmware |

Utilisation :
- Badge mode coloré (chip background) dans le bandeau bank courante.
- Barre verticale gauche 3 px du panneau bank courante.
- Sigle de type dans ALL BANKS (3 lettres : NRM / ARP / GEN / LOOP) colorées.

### §26 — États Play / Stop / tick visuel

- **PLAY** écrit en toutes lettres, couleur fixe `#22C55E` (vert), à droite du numéro de bank dans ALL BANKS.
- **STOP** écrit en toutes lettres, couleur fixe `#6B7280` (gris).
- **Pastille pulsante à gauche** : présente uniquement en PLAY → cercle plein vert qui pulse à la subdivision (`tempo BPM × subdivision`). En STOP, pas de pastille.
- **Tick local** : calculé côté JUCE Timer multi-instances (une par bank ARP/ARPEG_GEN en Play). Intervalle = `60000 / (BPM × division_multiplier)` ms. Animation : fade-in vif 50 ms vers blanc + fade-out 100 ms vers vert. **Non synchronisé** à l'horloge maîtresse ILLPAD (dérive possible long terme, acceptable V1).

### §27 — PANIC flash

À réception de `[PANIC] All notes off on all channels` :
- La transport bar passe en background rouge `#EF4444` pendant **2 secondes**, texte "PANIC — All Notes Off" en blanc bold, puis fade-out 500 ms vers normal.
- L'event reste dans le scroll EVENTS (badge rouge sur la ligne correspondante).
- Aucun son émis (pas de bell).

### §28 — Connection status

| État | Indicateur | Couleur |
|---|---|---|
| Connected | `●` | `#22C55E` (vert) — clignote 1 fois doucement à la connexion |
| Reconnecting | `●` | `#F59E0B` (orange) — pulse ~1 Hz |
| Disconnected | `●` | `#EF4444` (rouge) — statique |

### §29 — Police

**IBM Plex Mono (OFL)**, une seule font pour tout l'UI runtime ET le terminal setup. Support parfait des box-drawing chars Unicode (utilisés dans le rendu VT100 firmware). Embeddée via JUCE BinaryData.

Variantes embarquées : Regular, Medium, Bold.

### §30 — Sizing

Toutes les tailles ci-dessous sont **logiques** (référence design 900 × 600). Au resize scale-to-fit, multipliées par le facteur de scale.

| Élément | Taille logique |
|---|---|
| Fenêtre par défaut | 900 × 600 |
| Header app | 44 px |
| Transport bar | 48 px |
| Tempo display (BPM) | 28 pt Bold |
| Bank number current (panneau bank courante) | 48 pt Bold |
| Badge mode courante (chip + label) | 14 pt + padding 8 px |
| PotCell — target name | 11 pt secondary |
| PotCell — valeur | 24 pt primary |
| PotCell — slot label (R1, R1H, …) | 10 pt secondary |
| ALL BANKS row | 13 pt (badges + chiffres) |
| Event log line | 11 pt |
| Padding sections | 16-24 px |
| Padding interne PotCell | 12 px |
| TerminalView padding | 12 px autour |

### §31 — Resize scale-to-fit — cas spécial setup mode

Le grid logique du terminal VT100 est **imposé par le firmware** (typiquement 50 lignes × 120 colonnes via `CSI 8 t`). Avec le scale-to-fit, ce grid logique est respecté tel quel ; chaque cellule est **taillée dynamiquement** pour remplir la zone allouée de la TerminalView dans la fenêtre :

```
cellW = TerminalView.width  / cols
cellH = TerminalView.height / rows
```

Le texte reste lisible (font vectorielle, scaling propre). La **fenêtre OS ne change pas de taille** à la demande du firmware — contrairement au comportement actuel de `vt100_serial_terminal.py` qui propage `CSI 8 t` à iTerm2 et déclenche un resize de window. Ici, l'utilisateur contrôle la fenêtre, le terminal s'adapte. Pas de letterboxing. Cohérent avec le modèle mental "tout grossit comme une image".

### §32 — États hover / focus (V1 minimal)

- Header port name : underline au hover, cursor pointer, click → menu déroulant des ports.
- Resync button : background hover `#21262D`.
- Toggle pin : icône change d'opacité au hover.
- Bank row dans ALL BANKS : pas de hover V1 (read-only).
- Cellule PotCell récemment modifiée : flash background `#58A6FF` opacity 15 % pendant 300 ms, puis fade out.

---

## Partie 5 — Distribution et persistance

### §33 — Build system : CMake

- Cohérent avec le workflow JUCE moderne.
- Plus simple pour intégrer libserialport et libvterm comme dépendances vendored ou submodules.
- JUCE supporte officiellement CMake (`juce_add_gui_app(...)`).

Alternative Projucer reste possible pour cohérence avec `circlePI/cpp/mac/` ; à acter dans le plan d'implémentation.

### §34 — Format livrable

- `.app` standalone macOS bundle.
- Architecture cible V1 : `arm64` (Apple Silicon). Universal `arm64 + x86_64` envisageable via flag CMake si besoin, coût additionnel négligeable.
- Bundle ID : `com.loiclachaize.illpadviewer` (ou domaine au choix).
- Version : `1.0.0` au démarrage, bumpée à chaque release.

### §35 — Signing & notarisation

**V1 — Ad-hoc signing** : `codesign --sign - --deep ILLPADViewer.app`. Suffit pour usage personnel ; Gatekeeper averti à la première ouverture (workaround clic-droit → Ouvrir).

**Distribution propre (V2)** : Developer ID + notarisation Apple (compte $99/an). À activer si/quand le viewer est distribué à un futur acheteur d'ILLPAD.

### §36 — Permissions macOS

USB CDC serial : aucune permission spéciale requise pour un .app codesigned (ad-hoc ou full). L'app a accès direct à `/dev/cu.usbmodem-*`. Pas de sandbox V1 (compliquerait l'accès serial sans bénéfice). Aucune permission Privacy/TCC à demander (pas microphone, caméra, etc.).

### §37 — Persistance config

Fichier `~/Library/Application Support/ILLPADViewer/state.xml` via JUCE `PropertiesFile`.

Champs persistés :

| Clé | Type | Default |
|---|---|---|
| `lastSerialPort` | string | empty → auto-detect VID/PID |
| `windowX`, `windowY` | int | center screen |
| `windowWidth`, `windowHeight` | int | 900 × 600 |
| `alwaysOnTop` | bool | true |
| `firstRun` | bool | true → onboarding minimal au premier launch |

Sauvegarde à chaque changement matériel (close de l'app, déplacement fenêtre, toggle, sélection port).

### §38 — Onboarding premier lancement

- `firstRun = true` : message court "Connect your ILLPAD via USB and it will be auto-detected" puis dismiss définitif (flag mis à false).
- Si pas d'ILLPAD détecté à un moment : afficher "Waiting for ILLPAD..." avec bouton "Select port manually" qui ouvre la liste.

### §39 — Installation et mise à jour

- **V1** : drag-and-drop manuel de `ILLPADViewer.app` dans `/Applications/`. Release manuelle par remplacement du bundle.
- **V2 (hors scope)** : framework Sparkle (auto-update) si distribution à des tiers.

---

## Partie 6 — Tests

### §40 — Framework : Catch2 v3

- Tests Layer 1 (Parser/ModeDetector/ITermSeqInterceptor) sont pure C++ sans JUCE → Catch2 s'intègre nativement.
- `main()` automatique, boilerplate minimal.
- CMake target dédié `illpad_viewer_tests` qui compile uniquement Layer 1 + Catch2. Build et run indépendants de l'app principale.

### §41 — Couverture Layer 1

**RuntimeParser** :
- Table de paires `(input_line, expected_event)` couvrant toutes les catégories (§6 à §10) y compris `[BANKS]`, `[STATE]`, `[POT]` annoté.
- Edge cases : ligne tronquée, ligne corrompue, ligne vide, slot vide `R3H=---`, champs optionnels absents (`octave` sur NORMAL, etc.).

**ModeDetector** :
- Table de séquences bytes → mode attendu après traitement.
- Reset state machine au reboot simulé (loss + reconnect).
- Boot serial garbage suivi d'une ligne propre.

**ITermSeqInterceptor** :
- Table de séquences ANSI iTerm-spécifiques → événements interceptés.
- Sequences non interceptées passent telles quelles à libvterm.

### §42 — Tests non automatisés

- **Layer 0 (SerialReader)** : test manuel — brancher, vérifier auto-detect VID/PID, débrancher, vérifier reconnect. Mesurer latence touche → écho VT100 (<50 ms cible).
- **Layer 2 + 3** : smoke test E2E manuel (§43).

### §43 — Smoke test E2E manuel — checklist V1

À exécuter avant chaque release locale :

1. Lancer app, ILLPAD débranchée → état "Waiting for ILLPAD".
2. Brancher ILLPAD → auto-detect → status `● Connected` → `[BANKS]` + `[STATE]` reçus → UI populée intégralement.
3. Tourner R1 sur ILLPAD → cellule R1 PotCell se met à jour + highlight flash 300 ms.
4. Switch bank → `[BANK]` reçu → marker ▶ bouge dans ALL BANKS + panneau bank courante intégralement remis à jour via `[STATE]`.
5. Bank ARP : appuyer pad → `[ARP] +note` reçu → status PLAY + pastille verte pulse à la subdivision visible à l'œil.
6. Stop arp (relâcher pads en mode non-hold) → "STOP" en gris, pastille disparue.
7. Triple-click rear button → flash rouge transport bar ~2 sec puis fade out.
8. Débrancher câble en cours → status passe `● Reconnecting` orange.
9. Rebrancher → auto-reconnect → ré-envoi `?BOTH` → UI re-populée.
10. Reboot ILLPAD en setup mode (hold rear button au boot) → ModeDetector bascule SETUP → TerminalView affiche le cockpit VT100 fidèle.
11. Naviguer dans setup avec flèches → latence imperceptible, rendu fidèle, pas de tearing (DEC 2026 OK).
12. Quitter setup → reboot ILLPAD → ModeDetector bascule RUNTIME → UI re-populée.
13. Toggle always-on-top → fenêtre flotte/déflotte correctement au-dessus d'Ableton.
14. Resize fenêtre → tout scale proportionnellement (fonts, frames, padding).
15. Quitter app → relancer → position fenêtre + port + always-on-top persistés.

### §44 — Hors scope V1 (tests)

- CI/CD automatique (single dev, projet perso). À envisager si distribution V2.
- Tests d'intégration UI auto (XCTest, etc.).
- Tests de performance / profiling.
- Tests pixel-perfect du rendu (golden images).

---

## Partie 7 — Plan d'attaque

### §45 — Ordre de développement V3

1. **Foundation** : structure projet CMake, intégration JUCE 8, vendoring libserialport et libvterm en static libs, embeddage IBM Plex Mono. App vide qui compile et lance une fenêtre noire 900×600.
2. **Layer 0 — SerialReader** : ouverture port (manual port name d'abord), boucle `sp_wait`, lecture bytes vers stdout pour validation, écriture test depuis stdin. Auto-detect VID/PID dans un second temps.
3. **Layer 1 — RuntimeParser** + tests Catch2 unitaires.
4. **Layer 2 — RuntimeModel** : DeviceState, BanksInfo, CurrentBankState, PotSlots, EventBuffer + ChangeBroadcaster.
5. **Layer 3 — RuntimeMode UI** : HeaderBar, TransportBar, CurrentBankPanel + PotCell, AllBanksPanel + BankRow, EventLogPanel. AppLookAndFeel pour palette + Plex Mono.
6. **MainComponent racine + always-on-top + persistance config** : fonctionnel sur le mode RUNTIME seul. Bouton Resync. Reconnect auto.
7. **Layer 1 — ITermSeqInterceptor** + tests Catch2.
8. **Layer 1 — TerminalDriver (libvterm)** : intégration callbacks, exposition screen state.
9. **Layer 2 — TerminalModel + ModeDetector** + tests Catch2 unitaires (sur ModeDetector).
10. **Layer 3 — SetupMode UI** : TerminalView (rendu cell par cell), KeyboardInput → output queue, AppChrome simplifié.
11. **ModeSwitcher** : transitions fade-out / fade-in, lock state machine, reset au reconnect.
12. **Resize scale-to-fit** : application sur MainComponent, aspect ratio fixé, clamp min size.
13. **Polish** : highlight flash cellule change, tick pulsant Play, PANIC flash, onboarding firstRun, About panel.
14. **Codesign ad-hoc** + smoke test E2E intégral.

### §46 — Modifs firmware (en parallèle ou avant)

Indépendant du dev JUCE. Les modifs firmware (§7-§10) peuvent être livrées avant le viewer pour permettre les tests d'intégration. Ordre suggéré :
1. Annotation slot dans `[POT]` (§7).
2. Dump `[BANKS]` au boot (§8).
3. Dump `[STATE]` au boot et au bank switch (§9).
4. Handler runtime de request state (§10).

Chaque modif est testable individuellement via `pio device monitor` avant intégration côté viewer.

---

## Partie 8 — Hors scope V1 (explicite)

- Item menu bar complémentaire pour events critiques (envisagé puis écarté — peut revenir en V2 si besoin se confirme).
- Notifications macOS natives sur PANIC / queue full / disconnect.
- Plusieurs instances / plusieurs fenêtres simultanées.
- Mode compact / mini-bandeau.
- Light theme toggle.
- Palette user-defined.
- Sync app ↔ firmware ColorSlot (couleurs configurables au lieu de la palette fixe §25).
- Tick visuel synchronisé à l'horloge maîtresse ILLPAD (V1 : tick local non-synchronisé).
- Animations avancées (glow, microtransitions ease-in/out poussés).
- Filtre par catégorie dans EventLogPanel.
- Export du log.
- Auto-update via Sparkle.
- Notarisation Developer ID.
- Portage Windows / Linux.
- Raccourci clavier global show/hide.
- Tests pixel-perfect / CI auto.

---

## Partie 9 — Annexes

### §47 — Glossaire

- **Bank** : un des 8 emplacements du ILLPAD48, chacun avec son type, canal MIDI, scale, etc. Une seule bank est *foreground* (jouable au clavier) à un moment donné ; les autres restent *background* mais leurs arp engines tournent toujours.
- **Type de bank** : NORMAL, ARPEG (arpégiateur classique 6 patterns), ARPEG_GEN (arpégiateur génératif), LOOP (placeholder Phase 1).
- **Slot pot** : un des 8 paramètres routés simultanément par le PotRouter dans le contexte actif (NORMAL ou ARPEG). Layout fixe : R1, R1H, R2, R2H, R3, R3H, R4, R4H où `H` = hold-left modifier. Mapping configurable au boot via Tool 7.
- **Per-bank target** : paramètre dont la valeur diffère selon la bank active. Liste exhaustive dans `PotRouter::isPerBankTarget()`.
- **Setup mode** : mode boot-only accessible en maintenant le bouton arrière au démarrage, utilisé pour calibration et configuration. Sortie = reboot.
- **Scale group** : assignation A/B/C/D ou aucun. Les banks dans le même groupe partagent leur scale ; un changement de scale sur une bank du groupe se propage silencieusement aux autres.
- **Foreground / background bank** : seule la bank foreground reçoit l'input pad. Tous les arp engines (background ou foreground) tournent en parallèle quand assignés à une bank ARP*.
- **MessageThread (JUCE)** : thread principal de l'UI ; toute manipulation Component se fait dessus.

### §48 — Références code firmware

- `src/main.cpp:148-484` — boot sequence, init, panic trigger.
- `src/main.cpp:846-907` — `debugOutput()`, source des `[POT]` runtime (sous `#if DEBUG_SERIAL`).
- `src/managers/BankManager.cpp:200-223` — switch bank, source du `[BANK]`.
- `src/managers/PotRouter.cpp:658-675` — `isPerBankTarget()`.
- `src/managers/NvsManager.cpp:655-673` — scale group "leader wins" au boot.
- `src/managers/NvsManager.cpp:971-978` — `getLoadedScaleGroup` / `setLoadedScaleGroup`.
- `src/arp/ArpEngine.h:113`, `src/arp/ArpEngine.cpp:575` — `isPlaying()` accessor.
- `src/arp/ArpEngine.cpp:320-346, 436-568, 909` — sources `[GEN]`, `[ARP]` events.
- `src/managers/ScaleManager.cpp:125-220` — sources `[SCALE]`, `[ARP] Octave`, `[ARP_GEN] MutationLevel`.
- `src/midi/ClockManager.cpp:98-175` — source `[CLOCK]`.
- `src/core/MidiTransport.cpp:62-127` — sources `[MIDI]`.
- `src/main.cpp:125-143` — `midiPanic()`, source `[PANIC]`.
- `src/core/KeyboardData.h:183-217` — `ColorSlotStore`, `CSLOT_*` enum.
- `src/core/KeyboardData.h:500-508` — `NUM_SCALE_GROUPS`, `BankTypeStore.scaleGroup`.
- `src/core/KeyboardData.h:558-581` — `PotMapping`, `PotMappingStore`, `POT_MAPPING_SLOTS = 8`.

### §49 — Références externes

- JUCE — https://juce.com — framework GUI/audio C++.
- libserialport (sigrok) — https://sigrok.org/wiki/Libserialport — lib serial port event-driven.
- libvterm (neovim) — https://github.com/neovim/libvterm — VT100 emulator.
- Catch2 v3 — https://github.com/catchorg/Catch2 — test framework.
- IBM Plex Mono (OFL) — https://www.ibm.com/plex/ — police monospace.

---

**Fin du document.**
