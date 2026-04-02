# ILLPAD48 — WiFi Setup Page (Spec)

> Page Setup web : remplacement du terminal VT100 pour les Tools 1-7.
> Servie par l'ESP32 en mode setup (boot + rear button hold 3s).
> Depend de `wifi-common.md` pour l'infra WiFi/WS/CommandHandler.
> Derniere mise a jour : 30 mars 2026.

---

## 1. Vue d'Ensemble

### Objectif

Remplacer le terminal VT100 serial comme chemin principal de configuration. Le VT100 reste en fallback expert (coexistence permanente, mais un seul canal actif a la fois — voir `wifi-common.md` Section 7).

### Acces

L'ILLPAD entre en mode setup par le geste physique habituel (boot + rear button hold 3s). L'ESP32 sert la page Setup sur `/` a la place de la page Live.

### Structure

Page HTML unique avec **onglets internes** (un par Tool). Pas de rechargement entre les Tools — le WebSocket reste connecte.

```
┌──────────────────────────────────────────────────────┐
│  ILLPAD48 SETUP                            [Reboot]  │
├───┬───┬───┬───┬───┬───┬───┬──────────────────────────┤
│ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │                          │
├───┴───┴───┴───┴───┴───┴───┤                          │
│                             │                          │
│    Contenu du Tool          │                          │
│    selectionne              │                          │
│                             │                          │
└─────────────────────────────┴──────────────────────────┘
```

---

## 2. Protocole WebSocket — Setup

### Sequence au Connect (mode setup)

Apres la sequence commune (voir `wifi-common.md` Section 4), le serveur envoie :

```json
{
  "type": "setup_state",
  "tool": "all",
  "calibration": { "maxDelta": [0, 0, ...], "calibrated": [false, false, ...] },
  "padOrder": [0, 1, 2, ...],
  "padRoles": { "bankPads": [...], "rootPads": [...], ... },
  "bankConfig": [{ "type": "N", "quantize": "Immediate" }, ...],
  "settings": { "doubleTap": 150, "bargraphDuration": 3000, ... },
  "potMapping": { "normal": [...], "arpeg": [...] },
  "ledSettings": { "colors": [...], "intensities": {...}, "timings": {...} }
}
```

Ce snapshot initial permet a la page Setup d'afficher l'etat actuel de tous les Tools sans aller-retour.

### Messages ESP32 → Client (specifiques setup)

| Message | Contenu | Frequence |
|---|---|---|
| `cal_pressure` | `{ pad, raw, max }` | ~20Hz par pad touche |
| `pad_touch` | `{ pad, position }` | Immediat sur touch |
| `setup_state` | Snapshot partiel ou complet | Au connect + apres save |

### Commandes Client → ESP32 (specifiques setup)

Voir catalogues par Tool ci-dessous.

---

## 3. Tool 1 — Pressure Calibration

### Principe

L'utilisateur touche chaque pad. L'ESP32 streame les valeurs de pression brutes par WebSocket. La WebUI affiche des barres en temps reel.

### Streaming Pression

```json
{ "type": "cal_pressure", "pad": 14, "raw": 847, "max": 1023 }
```

Envoye a ~20Hz par pad en cours de contact. Latence WiFi (~10-50ms) acceptable — l'utilisateur regarde une barre monter/descendre, pas un instrument temps reel.

### UX WebUI

- 48 cellules (grille ou SVG) avec barres de pression en temps reel
- Indicateur maxDelta par pad (valeur la plus haute atteinte)
- Couleur : gris (pas calibre) → vert (calibre)
- Tap sur une cellule = "Confirm pad" (valide la calibration pour ce pad)
- Bouton "Reset all" pour recommencer
- Bouton "Save" pour ecrire les seuils en NVS

### Commandes

```json
{ "cmd": "cal_confirm", "pad": 14 }
{ "cmd": "cal_reset" }
{ "cmd": "cal_save" }
```

---

## 4. Tool 2 — Pad Ordering

### Principe

L'utilisateur touche les pads du plus grave au plus aigu. L'ESP32 detecte le touch et assigne un numero d'ordre (position 0-47).

### Detection Touch

```json
{ "type": "pad_touch", "pad": 14, "position": 7 }
```

Envoye immediatement quand un pad est touche.

### UX WebUI

- SVG des 48 pads
- Chaque pad montre son numero d'ordre assigne
- Pads non assignes = gris
- Pads assignes = colores, avec numero visible
- Progression : "12 / 48 pads assignes"
- Bouton "Undo last" pour annuler le dernier
- Bouton "Reset all" pour recommencer
- Bouton "Save" pour ecrire en NVS

### Commandes

```json
{ "cmd": "order_undo" }
{ "cmd": "order_reset" }
{ "cmd": "order_save" }
```

Note : l'assignation se fait par touch physique sur le pad, pas par commande web. Le web ne fait que recevoir les events et afficher l'etat.

---

## 5. Tool 3 — Pad Roles

### Principe

Assigner 29 roles de controle aux 48 pads : 8 bank, 7 root, 7 mode, 1 chrom, 1 hold, 1 play/stop, 4 octave.

### UX WebUI

- SVG des 48 pads avec couleurs par role (meme palette que la vue SVG Live) :
  - Bank = bleu (#5AA8F0)
  - Root = ambre (#E5A035)
  - Mode = corail (#E8764A)
  - Chrom = blanc (#aaa)
  - Hold/Octave = teal (#30D1A2)
  - Play/Stop = magenta (#D453B0)
  - Musical (pas de role) = gris (#555)
- Panneau lateral avec les roles disponibles, regroupes par categorie
- Deux methodes d'assignation :
  1. Tap sur un pad dans le SVG + tap sur un role dans le panneau
  2. Tap sur un pad physique (ESP32 envoie `pad_touch`) + tap sur un role dans le panneau
- Verification des collisions en temps reel (un pad ne peut avoir qu'un seul role)
- Pads musicaux (19) : pas de role, affichent la note resolue
- Bouton "Save" pour ecrire en NVS

### Commandes

```json
{ "cmd": "role_assign", "pad": 14, "role": "bank", "index": 3 }
{ "cmd": "role_clear", "pad": 14 }
{ "cmd": "role_save" }
```

- `role` : `"bank"`, `"root"`, `"mode"`, `"chrom"`, `"hold"`, `"play_stop"`, `"octave"`
- `index` : position dans le groupe (bank 0-7, root 0-6, mode 0-6, octave 0-3). Pas de index pour chrom/hold/play_stop.

---

## 6. Tool 4 — Bank Config

### Principe

Configurer le type (NORMAL/ARPEG) et le mode de quantize pour chaque bank.

### UX WebUI

- 8 cartes (une par bank), chacune avec :
  - Toggle NORMAL / ARPEG
  - Select quantize mode : Immediate / Beat / Bar (visible uniquement pour ARPEG)
- Contrainte : max 4 banks ARPEG. Si on depasse, le toggle est desactive avec message explicatif.
- Bouton "Save" pour ecrire en NVS

### Commandes

```json
{ "cmd": "bank_type", "bank": 2, "type": "A" }
{ "cmd": "bank_quantize", "bank": 2, "mode": "Beat" }
{ "cmd": "bank_save" }
```

---

## 7. Tool 5 — Settings

### Principe

Formulaire de parametres generaux.

### UX WebUI

Formulaire avec sections regroupees :

**Performance** :
- Aftertouch rate (slider)
- Double-tap duration : 100-250ms (slider, par pas de 10ms)

**Connectivite** :
- BLE connection interval (select)
- Clock mode : Master / Slave (toggle)
- Panic on BLE reconnect : On / Off (toggle)

**Affichage** :
- Pot bargraph duration : 1-10s (slider, par pas de 500ms)

**Batterie** :
- Battery ADC calibration (procedure guidee)

**WiFi** :
- WiFi enable : On / Off (toggle)
- WiFi TX power : Low / Medium / High (select)
- SSID : champ texte editable
- Password : champ texte editable

**Profil** :
- Profile preset (select + save/load)

Bouton "Save" global pour ecrire en NVS.

### Commandes

```json
{ "cmd": "setting", "key": "double_tap", "value": 150 }
{ "cmd": "setting", "key": "clock_mode", "value": "master" }
{ "cmd": "setting", "key": "wifi_ssid", "value": "ILLPAD48" }
{ "cmd": "settings_save" }
```

---

## 8. Tool 6 — Pot Mapping

### Principe

Assigner des parametres aux 8 slots pot (4 pots × 2 layers) par contexte (NORMAL / ARPEG).

### UX WebUI

- 2 onglets : NORMAL / ARPEG
- Grille 4 colonnes (pot right 1-4) × 2 lignes (seul / +hold left) = 8 cellules par contexte
- Chaque cellule = dropdown avec le pool de params disponibles
  - Vert = disponible
  - Gris = deja assigne ailleurs
- **MIDI CC** : choisir CC dans le dropdown → champ CC# apparait (0-127)
- **MIDI Pitchbend** : max un par contexte. Si un 2e est assigne, le 1er est libere automatiquement.
- **Slot vide** : option "— empty —" dans le dropdown
- Bouton "Reset to defaults" par contexte
- Le pot arriere (rear) est affiche en read-only (brightness / sensitivity, non configurable)
- Bouton "Save" pour ecrire en NVS

### Commandes

```json
{ "cmd": "pot_assign", "context": "ARPEG", "slot": 3, "target": "shuffle_depth" }
{ "cmd": "pot_cc", "context": "NORMAL", "slot": 2, "cc": 74 }
{ "cmd": "pot_pb", "context": "NORMAL", "slot": 5 }
{ "cmd": "pot_clear", "context": "ARPEG", "slot": 3 }
{ "cmd": "pot_reset_defaults", "context": "ARPEG" }
{ "cmd": "pot_save" }
```

- `slot` : 0-7 (0=r1 seul, 1=r1+left, 2=r2 seul, 3=r2+left, 4=r3 seul, 5=r3+left, 6=r4 seul, 7=r4+left)
- `target` : `"tempo"`, `"shape"`, `"slew"`, `"at_deadzone"`, `"gate"`, `"shuffle_depth"`, `"shuffle_template"`, `"division"`, `"pattern"`, `"velocity_base"`, `"velocity_var"`, `"pitch_bend"`, `"midi_cc"`, `"midi_pb"`, `"empty"`

---

## 9. Tool 7 — LED Settings

### Principe

Configurer les couleurs, intensites, timings, et confirmations des 8 LEDs SK6812 RGBW.

### UX WebUI

2 sections (comme les 2 pages VT100 actuelles), navigables par sous-onglets :

**Section 1 — Couleurs + Timing** :

- 13 slots couleur : chaque slot = preset (dropdown des couleurs de base) + hue offset (slider -180 → +180)
- Intensites 0-100% pour chaque etat LED :
  - Current NORMAL, Current ARPEG stopped (min/max), Current ARPEG playing (min/max, flash)
  - Background NORMAL, Background ARPEG stopped (min/max), Background ARPEG playing (min/max, flash)
- Timings :
  - Pulse period (ms)
  - Flash duration (ms)
- **Preview live** : les modifications sont envoyees en temps reel a l'ESP32 qui les applique sur les LEDs physiques (LEDs 3-4 comme zone de preview, comme le VT100 actuel)

**Section 2 — Confirmations** :

- 10 types de confirmation (bank switch, scale root, scale mode, scale chrom, hold on, hold off, play, stop, octave)
- Pour chaque type : couleur (color picker), pattern (select), duree (slider)
- Bouton "Test" par type → declenche le blink de confirmation sur les LEDs physiques

### Commandes

```json
// Couleurs
{ "cmd": "led_color", "slot": 3, "preset": "blue", "hue": 15 }

// Intensites
{ "cmd": "led_intensity", "state": "arp_bg_min", "value": 8 }
{ "cmd": "led_intensity", "state": "arp_fg_flash", "value": 100 }

// Timings
{ "cmd": "led_timing", "param": "pulse_period", "value": 1500 }
{ "cmd": "led_timing", "param": "flash_duration", "value": 30 }

// Preview / Test
{ "cmd": "led_preview_color", "slot": 3 }
{ "cmd": "led_preview_confirm", "type": "CONFIRM_BANK_SWITCH" }

// Save
{ "cmd": "led_save" }
```

---

## 10. Roadmap d'Implementation (Setup)

Le Setup web est implemente apres la page Live (phases 1-2 dans `wifi-common.md`).

### Phase 3 — Squelette Setup
- SetupWsHandler
- Detection canal actif (web vs serial)
- Page Setup HTML (shell + onglets, contenu placeholder)
- Tool 4 (Bank Config) — le plus simple, pour valider l'archi
- Tool 5 (Settings)

### Phase 4 — Setup Complet
- Tool 3 (Pad Roles) — SVG interactif
- Tool 6 (Pot Mapping)
- Tool 7 (LED Settings) — avec preview live

### Phase 5 — Setup Touch
- Tool 2 (Pad Ordering) — streaming touch detection
- Tool 1 (Pressure Calibration) — streaming pression ~20Hz
