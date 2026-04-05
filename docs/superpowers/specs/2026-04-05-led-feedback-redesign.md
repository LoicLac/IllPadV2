# LED Feedback Redesign

## Objectif

Simplifier le feedback visuel des 8 LEDs SK6812 RGBW pour qu'un musicien comprenne l'etat complet de l'instrument en < 1 seconde. Supprimer la confusion introduite par les animations superposees (sine pulse + tick flash + confirmations exclusives).

## Principes

1. **Spatial > temporel** : quelle LED compte plus que quelle animation.
2. **Solide par defaut** : les animations ne servent qu'a signaler un evenement ponctuel ou un etat "en attente".
3. **Overlay only** : aucune confirmation ne doit effacer la barre. Le contexte bank reste toujours visible.
4. **Le tick flash EST le feedback rythmique** : pas besoin d'autre animation quand l'arp joue.

---

## 1. Etats LED normaux (display permanent)

### Normal banks

| Etat | Couleur | Intensite | Animation |
|---|---|---|---|
| Normal BG | `CSLOT_NORMAL_BG` | `normalBgIntensity` | Solide |
| Normal FG | `CSLOT_NORMAL_FG` | `normalFgIntensity` | Solide |

Inchange.

### Arpeg banks

| Etat | Couleur | Intensite | Animation |
|---|---|---|---|
| Arpeg BG (stopped ou idle) | `CSLOT_ARPEG_BG` | `bgArpStopMin` | Solide |
| Arpeg BG playing | `CSLOT_ARPEG_BG` | `bgArpPlayMin` + tick flash | Solide entre ticks, flash `CSLOT_TICK_FLASH` a `tickFlashBg`% |
| Arpeg FG idle (pile vide) | `CSLOT_ARPEG_FG` | `fgArpStopMin` | Solide |
| Arpeg FG stopped (notes chargees) | `CSLOT_ARPEG_FG` | pulse `fgArpStopMin`<->`fgArpStopMax` | Pulse lent a `pulsePeriodMs` (seul usage restant du pulse) |
| Arpeg FG playing | `CSLOT_ARPEG_FG` | `fgArpPlayMax` + tick flash | Solide entre ticks, flash `CSLOT_TICK_FLASH` a `tickFlashFg`% |

### Changements vs actuel

- **Supprime** : sine pulse sur BG stopped, BG playing, FG playing.
- **Conserve** : sine pulse sur FG stopped avec notes ("en attente, pret a jouer").
- **FG playing** : `fgArpPlayMax` = intensite solide entre les tick flashes. `fgArpPlayMin` n'est plus utilise (pas de pulse).
- **BG** : intensites fixes (`bgArpStopMin` pour stopped, `bgArpPlayMin` pour playing). Les `*Max` ne sont plus utilises.

### Distinction FG idle vs FG stopped-loaded

`renderBankArpeg()` doit distinguer :
- `hasNotes() == false` -> solide a `fgArpStopMin` (meme intensite que BG = dim)
- `hasNotes() == true && !isPlaying()` -> pulse lent min<->max (breathing = "charge, en attente")
- `isPlaying()` -> solide bright + tick flash

---

## 2. Confirmations (toutes overlay)

Toutes les confirmations sont des overlays sur `renderNormalDisplay()`. Aucune ne fait `clearPixels()`. Elles overrident uniquement la LED du bank courant (`_currentBank`).

### Bank Switch

**Inchange.** Deja overlay. Blinks sur LED destination, timings configurables (`bankBlinks`, `bankDurationMs`, `bankBrightnessPct`). Couleur `CSLOT_BANK_SWITCH`.

### Scale Root / Mode / Chromatic

**Change : exclusif -> overlay.** Meme pattern de blinks (blinks configurables, duree configurable), mais en overlay sur `renderNormalDisplay()` au lieu de `clearPixels()`.

- Root : `CSLOT_SCALE_ROOT`, `scaleRootBlinks`, `scaleRootDurationMs`
- Mode : `CSLOT_SCALE_MODE`, `scaleModeBlinks`, `scaleModeDurationMs`
- Chromatic : `CSLOT_SCALE_CHROM`, `scaleChromBlinks`, `scaleChromDurationMs`

### Octave

**Change : 2 LEDs -> 1 LED, meme pattern que scale.** Blinks overlay sur LED du bank courant. Le parametre `_confirmParam` (octave 1-4) n'est plus utilise pour le mapping LED. Meme pattern : `octaveBlinks` blinks sur `octaveDurationMs`. Couleur `CSLOT_OCTAVE`.

### Hold ON / OFF

**Change : exclusif -> overlay, effet latch (fade directionnel).**

- **Hold ON** : fade IN sur la LED du bank courant. Couleur `CSLOT_HOLD`. Rampe de 0% a 100% sur `holdFadeMs`. L'overlay disparait apres la rampe (retour au display normal = le hold est actif mais pas visuellement permanent).
- **Hold OFF** : fade OUT sur la LED du bank courant. Couleur `CSLOT_HOLD`. Rampe de 100% a 0% sur `holdFadeMs`.

La direction du fade indique l'action (monter = verrouiller, descendre = deverrouiller).

Settings Tool 7 :
- `holdFadeMs` : duree du fade (shared ON/OFF, range 100-600ms, default 300ms).
- `holdOnFlashMs` : **supprime** (remplace par le fade IN qui utilise `holdFadeMs`).

### Play

**Change : 4 flashs beat-sync -> double blink overlay.**

Double blink (2 flashs rapides) sur LED du bank courant. Couleur `CSLOT_PLAY_ACK`. Duree totale courte (les tick flashes prennent le relais immediatement apres).

Settings Tool 7 : reutilise `bankBlinks` et `bankDurationMs` ? Non : un couple dedie est plus flexible.
- `playBlinks` : nombre de flashs (1-3, default 2)
- `playDurationMs` : duree totale (100-500ms, default 200ms)

Note : `playBeatCount` et le systeme beat-sync sont **supprimes**.

### Stop

**Change : fade exclusif -> double blink overlay.**

Meme pattern que Play mais avec couleur `CSLOT_STOP`.
- `stopBlinks` : nombre de flashs (1-3, default 2)
- `stopDurationMs` : duree totale (100-500ms, default 200ms)

Note : `stopFadeMs` est **supprime** (remplace par blink pattern).

---

## 3. Bargraph Tempo

### Comportement

Quand l'utilisateur tourne le pot tempo (ou hold+pot si le tempo est sur un slot hold) : le bargraph standard s'affiche (barre proportionnelle 0-8 LEDs pour 10-260 BPM) **plus** la derniere LED allumee pulse au rythme du BPM affiche.

### Detail

- Barre : `realLevel = (bpm - 10) / (260 - 10) * 8.0`. Meme rendu que le bargraph pot existant (fractional tip).
- Pulse tempo : la LED au sommet de la barre clignote on/off au rythme du BPM : periode = 60000 / bpm ms, duty 50%. Cela donne un feedback tactile du tempo choisi.
- Le bargraph tempo utilise les memes couleurs que le bargraph standard (couleur du bank type courant).
- Duree de remanence : meme `potBarDurationMs` que les autres bargraphs.

### Implementation

`showPotBargraph()` recoit un flag supplementaire ou une nouvelle methode `showTempoBargraph(bpm, realLevel, potLevel, caught)`. Le flag `_potBarIsTempo` active le pulse de la LED tip dans `renderBargraph()`.

---

## 4. Impact sur Tool 7

### Settings supprimes

| Setting | Raison |
|---|---|
| `holdOnFlashMs` | Remplace par fade IN utilisant `holdFadeMs` |
| `playBeatCount` | Remplace par `playBlinks` |
| `stopFadeMs` | Remplace par `stopBlinks` + `stopDurationMs` |

### Settings ajoutes

| Setting | Type | Range | Default | Description |
|---|---|---|---|---|
| `playBlinks` | uint8_t | 1-3 | 2 | Nombre de flashs play overlay |
| `playDurationMs` | uint16_t | 100-500 | 200 | Duree totale play overlay |
| `stopBlinks` | uint8_t | 1-3 | 2 | Nombre de flashs stop overlay |
| `stopDurationMs` | uint16_t | 100-500 | 200 | Duree totale stop overlay |

### Settings inchanges

- Tous les bank switch settings
- Tous les scale settings (root/mode/chrom blinks + duration)
- `octaveBlinks`, `octaveDurationMs`
- `holdFadeMs` (reutilise pour fade IN et OUT)
- `pulsePeriodMs` (utilise uniquement pour FG arp stopped-loaded)
- `tickFlashDurationMs`, `tickFlashFg`, `tickFlashBg`
- `gammaTenths`
- Toutes les intensites (`normalFg/Bg`, `fgArpStop/PlayMin/Max`, `bgArpStop/PlayMin`)
- Tous les 13 color slots

### LedSettingsStore

Version bump necessaire (`LED_SETTINGS_VERSION` 2 -> 3). Les champs supprimes (`holdOnFlashMs`, `playBeatCount`, `stopFadeMs`) sont remplaces par les nouveaux (`playBlinks`, `playDurationMs`, `stopBlinks`, `stopDurationMs`). La taille du struct change — `loadAll()` detecte l'ancienne version et applique les defaults pour les nouveaux champs.

### UI Tool 7

- Page CONFIRM : les lignes Hold, Play/Stop, Octave changent de contenu
  - Hold : une seule ligne "Fade duration" (plus de "ON flash" / "OFF fade" separes)
  - Play : "Blinks" + "Duration" (plus de "Beat count")
  - Stop : "Blinks" + "Duration" (plus de "Fade duration")
  - Octave : description mise a jour ("blinks on current bank LED")
- Page COLOR : descriptions mises a jour pour Stop ("double blink on play/stop")
- Previews ('b') a adapter pour les nouveaux patterns

---

## 5. Fichiers impactes

| Fichier | Changement |
|---|---|
| `LedController.h` | Nouveau flag `_potBarIsTempo`, `_potBarBpm`. Supprime `_playFlashPhase`, `_playLastBeatTick`, `_fadeStartTime`. Ajoute `_holdFadeDirection`. |
| `LedController.cpp` | `renderBankArpeg()` : supprimer pulse playing, ajouter condition idle/stopped/playing. `renderConfirmation()` : scale/hold/octave -> state-only (comme play/stop). `renderNormalDisplay()` : ajouter overlays scale/hold/octave. `renderBargraph()` : pulse tempo. |
| `HardwareConfig.h` | Rien (constantes OK). |
| `KeyboardData.h` | `LedSettingsStore` v3 : remplacer champs, bump version. `validateLedSettingsStore()` : adapter. |
| `NvsManager.cpp` | Defaults pour nouveaux champs. Migration v2->v3. |
| `ToolLedSettings.cpp` | UI CONFIRM page : adapter lignes Hold/Play/Stop/Octave. Previews. |
| `PotRouter.cpp` | Detecter pot tempo et passer le BPM au bargraph. |
| `main.cpp` | Appel `showTempoBargraph()` depuis `handlePotPipeline()`. |

---

## 6. Ce qui ne change PAS

- Bargraph pot standard (parfait tel quel)
- Battery gauge et battery low blink
- Boot sequence
- Setup comet et chase
- Error state
- Calibration mode
- Preview API (Tool 7)
- Le systeme de color slots (13 slots, `resolveColorSlot()`)
- Tick flash pipeline (ArpEngine -> consumeTickFlash -> LedController)
- Le code deja applique ce matin (CONFIRM_PLAY/STOP overlay dans renderNormalDisplay) sera remplace par le nouveau pattern double-blink
