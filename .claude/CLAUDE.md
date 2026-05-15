# ILLPAD48 V2 — 48-Pad Capacitive MIDI Controller

**Objet unique, pas un prototype.** Vendu, joué live, traité comme une œuvre :
qualité finale attendue, finition soignée, fiabilité non négociable. Le mot
"prototype" est interdit ici — il autorise le bricolage. « Ça suffit pour un
proto » ne s'applique jamais.

---

## NVS & Persistence — Zero Migration Policy

Cette règle n'est PAS une permission de bricolage. Elle découle d'une
contrainte propre : le user est à côté de l'instrument, peut entrer en setup
mode et re-saisir ses préférences en 2 minutes via les Tools 1-8. Cela rend
une stratégie "reset sur incompatibilité" tenable sans dette technique liée
aux anciennes versions de struct. La règle s'applique **uniquement au NVS** :

1. **No NVS migration code.** Quand une Store struct change (field, taille,
   ordre), bumper la version. Toute donnée NVS qui rate `loadBlob()`
   (taille / magic / version invalide) est silencieusement rejetée et
   remplacée par les defaults compile-time. Pas de shim, pas de path
   d'upgrade.
2. **Les valeurs user sont re-saisies après update touchant le NVS.** Un
   `Serial.printf` d'avertissement au boot suffit.

Conséquence : jamais de version stale "pour compat", jamais de padding
"pour préserver un layout", jamais de struct dupliquée "pour supporter
l'ancien format". Change la struct, bump la version, passe à la suite.

**Ce qui n'est JAMAIS autorisé**, en dehors du NVS : code approximatif,
gestion d'erreur bâclée, workarounds hardware non documentés, "ça marche
sur mon poste", regressions ignorées.

---

## Build

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1              # build
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload    # upload
~/.platformio/penv/bin/pio device monitor -b 115200                # monitor
```

`pio` is NOT in PATH — always use the full path.

---

## Invariants (things that MUST stay true)

1. **No orphan notes** : noteOff ALWAYS uses `_lastResolvedNote[padIndex]`,
   never re-resolves. Scale changes cannot produce stuck notes. Left-button
   release sweeps all pads on the `s_wasHolding → !holding` edge : NORMAL
   banks call `noteOff(i)` for every unpressed pad ; ARPEG HOLD OFF banks
   call `removePadPosition(s_padOrder[i])` for every unpressed pad except
   the holdPad.
2. **Arp refcount atomicity** : noteOff is scheduled BEFORE noteOn. If
   noteOn fails (queue full), noteOff is cancelled. MIDI noteOn sent only
   on refcount 0 → 1, noteOff only on 1 → 0.
3. **No blocking on Core 1** : NVS writes happen in a background FreeRTOS
   task. Core 1 only sets dirty flags.
4. **Core 0 never writes MIDI** : all MIDI output happens on Core 1. Core 0
   only reads slow params (relaxed atomics).
5. **Catch system** : pot must physically reach the stored value before it
   can change a parameter. Prevents jumps on bank switch or context change.
6. **Bank slots always alive** : all 8 banks exist, only foreground
   receives pad input. ARPEG engines run in background regardless of which
   bank is selected.
7. **Setup/Runtime coherence** : every runtime parameter that persists
   across reboots has a 4-link chain Runtime ↔ Store ↔ Tool ↔ NVS. If any
   link is modified (field added, range widened, enum extended), ALL links
   must be updated in the same commit. An orphan link (Store field with no
   Tool access, or Tool case for a removed target) is a bug, not tech debt.

---

## CRITICAL — DO NOT MODIFY

- **`src/core/CapacitiveKeyboard.cpp/.h`** — pressure pipeline is musically
  calibrated. Do not change.
- **Pressure tuning constants** in `src/core/HardwareConfig.h` (thresholds,
  smoothing, slew, I2C clock).
- **`platformio.ini`** (unless adding `lib_deps`).

---

## Documentation index — read BEFORE modifying

This repository ships a set of specialized reference documents in
`docs/reference/`. When you touch a subsystem, load the corresponding ref
first. Maintain it in the same commit (keep-in-sync protocol).

**Navigation start** : always begin at
[`docs/reference/architecture-briefing.md`](../docs/reference/architecture-briefing.md)
§0 Scope Triage. It routes you to the right subset of refs based on the
task (tight modification / broad exploration).

### When you touch… read and keep in sync

| Subsystem / topic | Primary ref |
|---|---|
| Any runtime flow (pad → MIDI, arp, bank switch, scale, pot) | [`runtime-flows.md`](../docs/reference/runtime-flows.md) |
| Reusable patterns (P1–P14) before inventing a new mechanism | [`patterns-catalog.md`](../docs/reference/patterns-catalog.md) |
| NVS code (any Store struct, any `loadBlob`/`saveBlob`) | [`nvs-reference.md`](../docs/reference/nvs-reference.md) |
| LED visual / event grammar / pattern / color slot | [`led-reference.md`](../docs/reference/led-reference.md) |
| Arp engine, scheduler, pile, Play/Stop, shuffle | [`arp-reference.md`](../docs/reference/arp-reference.md) |
| ARPEG_GEN params musicien-facing (BONUS/MARGIN/PROX/ECART, scenarios live) | [`fonction_regen.md`](../docs/reference/fonction_regen.md) |
| Pot hardware, filter, catch, Tool 7 mapping | [`pot-reference.md`](../docs/reference/pot-reference.md) |
| Hardware wiring, pins, MCP3208, components | [`hardware-connections.md`](../docs/reference/hardware-connections.md) |
| Boot code, setup mode entry, LED boot feedback | [`boot-sequence.md`](../docs/reference/boot-sequence.md) |
| Any setup Tool UI / behavior | [`setup-tools-conventions.md`](../docs/reference/setup-tools-conventions.md) |
| VT100 aesthetic / frame / colors / cockpit primitives | [`vt100-design-guide.md`](../docs/reference/vt100-design-guide.md) |

### Side-car : Python terminal

`ItermCode/vt100_serial_terminal.py` is the only way to interact with setup
mode. When `src/setup/` code changes input handling, escape sequences, line
endings, or VT100 rendering, verify and update the terminal script in the
same commit. The two must stay synchronized (arrow key atomic send, line
ending normalization, DEC 2026 sync support).

---

## VT100 Setup Console — politique

Le terminal VT100 du setup mode est un **argument de vente esthétique** et
sert de **mini-manuel utilisateur** à l'instrument. Conséquences directes :

- **Ne jamais simplifier** le rendu visuel pour économiser du code. La
  qualité visuelle du cockpit (frames, voyants, segmented readouts, colour
  semantics) est un livrable, pas un détail.
- Les descriptions / hints / panels INFO doivent être **unifiés en ton et
  en niveau de détail** — un user qui parcourt les Tools doit recevoir un
  manuel cohérent, pas des bouts de doc pour développeurs.
- Toute modification d'un Tool doit re-vérifier que la description info /
  control bar reste cohérente avec le reste de la console.

Spec technique du rendu : voir [`vt100-design-guide.md`](../docs/reference/vt100-design-guide.md).

---

## Setup mode — boot-only par construction

Le setup mode est accessible **uniquement au boot** (entrée par bouton).
La sortie du setup mode déclenche un reboot. Conséquences :

- Toutes les valeurs configurées via Tools 1-8 (padOrder, roles, scale
  pads, hold pad, octave pads, control pads, color slots, etc.) sont
  **runtime-immutables** : elles ne changent jamais entre deux boots.
- **Inutile d'écrire les Tools pour gérer du runtime** (pas de hot-reload,
  pas de sync entre Tool et runtime). Cette complexité serait gaspillée.
- **Sûr de cacher des valeurs résolues** au boot (pad-index lookup table,
  scale-degree resolved arrays, etc.). Le cache reste valide toute la
  session jusqu'au prochain reboot, pas besoin d'invalidation.
- Cf invariant 7 (Setup/Runtime coherence) : la chaîne Runtime ↔ Store ↔
  Tool ↔ NVS doit rester complète sous peine d'orphan link.

---

## Conventions

- `s_` static globals, `_` members, `SCREAMING_SNAKE_CASE` constants.
- `#if DEBUG_SERIAL` for debug output (1 = all messages, 0 = complete
  silence, zero overhead).
- `#if DEBUG_HARDWARE` for runtime pot/button state logging (separate from
  DEBUG_SERIAL).
- No `new`/`delete` at runtime — static instantiation only.
- Unsigned arithmetic for `millis()` (overflow-safe subtraction pattern :
  `(now - startTime) < duration`).
- Small stack allocations (FreeRTOS task stacks are limited).
- C++17, Arduino framework, PlatformIO.
- `std::atomic` for inter-core sync — **NEVER** `volatile`.

---

## Performance Budget

| Resource | Usage | Note |
|---|---|---|
| Core 0 | ~92 % | Sensing — unchanged, this is the bottleneck |
| Core 1 | ~16 % | Plenty of headroom |
| BLE MIDI | 30–50 % worst case | noteOn/Off bypass queue. Aftertouch overflow tolerated. |
| SRAM | ~16 % | ~51 KB / 320 KB |

### Budget Philosophy — prefer safe over economical

**SRAM, PSRAM, and flash budgets are generous.** The ESP32-S3-N8R16 has
320 KB SRAM, 16 MB PSRAM, 8 MB flash. The only truly constrained resource
is **per-cycle CPU time on Core 0** (~92 % used by sensing).

**Prefer safe/generous over economical** when it comes to :
- Buffer sizes (pending queues, event buffers — bump them rather than risk
  drops under load).
- Defense-in-depth guards (even when upstream already guards).
- Static allocations for always-alive patterns (e.g. `s_arpEngines[4]`
  always allocated).
- State duplication that simplifies invariants (e.g. tracking flags that
  avoid subtle edge cases).

**Do NOT economize** on :
- SRAM bytes for safer margins (a few hundred bytes for a bigger pending
  queue is fine).
- PSRAM (still barely touched).

**DO economize** on :
- Core 0 CPU cycles (sensing is the bottleneck).
- BLE MIDI bandwidth in worst case (noteOn/Off bypass queue, aftertouch
  overflow tolerated).
- Flash writes (NVS has wear limits — use dirty flag debouncing).
