# Bug Audit — Setup Tools — Worklog

Source: audit RTF 2026-04-04
Updated: 2026-04-04 (bugs 5,6,7,11,12,13 done)

## Legende
- DONE = verifie, plus rien a faire
- A FAIRE = confirme present, fix prevu
- A TESTER = fix applique, besoin de validation hardware
- A VERIFIER = status incertain, necessite investigation hardware

---

## DONE

| # | Bug | Resolution |
|---|-----|-----------|
| 1 | Freeze fleches Tool 7 | seedPotsForCursor() desactive le pot apres arrow. Pas un bug — design intentionnel. |
| 2 | Fleches mortes Tool 7 | Meme mecanisme que bug 1. Arrows modifient presetId puis re-seed. |
| 4 | Pipeline pot unifie | PotFilter Phase 1 implemente. 16x oversampling, deadband 20, sleep/wake, NVS. |
| 5 | `q` = reboot immediat | Supprime le mapping NAV_QUIT → '0' au menu principal. `q` ne fait plus rien au menu. |
| 6 | Pas de confirmation reboot | Ajout prompt `y/n` avant ESP.restart(). Toute touche sauf `y` = annule. |
| 7 | needsReboot inutile | Supprime flag, condition, affichage dans ToolSettings + commentaires reboot-only dans ToolLedSettings et HardwareConfig. |
| 12 | BLE ON/OFF pas clair | Labels reformates `ON ─ ...` / `OFF (USB only)`. Param renomme `BLE:`. |
| 11 | Tool 4 navigation | Flat cycle LEFT/RIGHT: NORMAL → ARPEG-Immediate → Beat → Bar → NORMAL. Plus de sous-nav DOWN/UP. ENTER save, q cancel. |
| 13 | Cal tool validation globale | Free-play mode: auto-capture, double-tap 200ms reset, 4-tier delta coloring, ENTER global validate with confirm prompt. Docs: `docs/archive/bug-13-cal-free-mode/` |

---

## A FAIRE — A VERIFIER hardware

### Bug 3 — Preset ne reinit pas le hue (Tool 7) — A VERIFIER

**Deux chemins de code changent le preset :**

1. **Navigation arrows (main loop)** — `ToolLedSettings.cpp` lignes 1200-1213 :
   Change `slot.presetId` ET fait `slot.hueOffset = 0` (ligne 1209) + `seedPotsForCursor()`.
   → Ce chemin est **OK**.

2. **adjustColorField()** — `ToolLedSettings.cpp` lignes 224-229 :
   Change `slot.presetId` mais ne reset **PAS** hueOffset.
   ```cpp
   case 0: {  // Preset: wrap 0..COLOR_PRESET_COUNT-1
     int val = (int)slot.presetId + dir;
     if (val < 0) val = COLOR_PRESET_COUNT - 1;
     if (val >= COLOR_PRESET_COUNT) val = 0;
     slot.presetId = (uint8_t)val;
     break;  // ← PAS de hueOffset = 0 ici
   }
   ```

**Storage** : `ColorSlot { uint8_t presetId; int8_t hueOffset; }` dans `KeyboardData.h`.

**A verifier** : est-ce que `adjustColorField()` case 0 est encore appele pour changer le preset, ou est-ce un dead path remplace par la nav arrows? Si c'est un dead path, le bug est deja fixe. Sinon, ajouter `slot.hueOffset = 0;` dans le case 0.

---

### Bug 8 — Tick preview a 120 BPM — A VERIFIER

**Fichier** : `ToolLedSettings.cpp`

**Ligne 907** — preview tick flash :
```cpp
bool flashing = ((elapsed % 500) < flashMs) && (elapsed < 1500);
```

L'intervalle est **deja hardcode a 500ms** (= 120 BPM). Le `flashMs` est la duree du flash (10-100ms), pas l'intervalle entre ticks. Le preview simule 3 ticks a 120 BPM pendant 1.5s.

**A verifier sur hardware** : est-ce que le preview est visuellement satisfaisant? L'audit RTF dit que le tick preview "utilise le pulse period, pas un vrai tempo" — mais le code montre le contraire. Si le preview est OK, ce bug est deja fixe.

---

## A FAIRE — UX features (1-2 sessions)

### Bug 9 — Undo queue Tool 3 (ToolPadRoles) + extension Tool 2

**Tool 3 (ToolPadRoles)** — `ToolPadRoles.cpp` :
- **Aucun undo existant** (confirme par recherche)
- L'assignation se fait via `assignRole(pad, line, index)` (lignes 206-228)
- Le clear se fait via `clearRole(pad)` (lignes 230-247)
- Le flow principal est a lignes 770-825 : touch pad → NAV_ENTER → assignRole

**Roles** (enum `PadRoleCode`, lignes 13-22) :
```
ROLE_NONE, ROLE_BANK, ROLE_ROOT, ROLE_MODE,
ROLE_OCTAVE, ROLE_HOLD, ROLE_PLAYSTOP
```

**Pool sizes** (lignes 95-100) : 8 bank + 7 root + 8 mode (7+1 chrom) + 4 octave + 1 hold + 1 playstop = **29 roles sur 48 pads**.

**Donnees pour undo** : `{ uint8_t padIndex, uint8_t oldLine, uint8_t oldIndex }` — permet de restaurer l'assignation precedente.

---

**Tool 2 (ToolPadOrdering)** — `ToolPadOrdering.cpp` :
- **Undo EXISTE DEJA** via touche 'u' (lignes 303-310) :
  ```cpp
  assignedCount--;
  uint8_t undoneKey = assignHistory[assignedCount];
  assigned[undoneKey] = false;
  orderMap[undoneKey] = 0xFF;
  ```
- Utilise `assignHistory[assignedCount]` comme stack LIFO simple

**Conclusion** : Tool 2 a deja un undo. Le travail est seulement pour Tool 3.
Pattern reutilisable `UndoRing<T, N>` si pertinent, mais Tool 2 n'en a pas besoin (son undo LIFO est suffisant).

---

### Bug 10 — Pad ordering ancien rank en gris (Tool 2)

**Fichier** : `ToolPadOrdering.cpp`

**Affichage actuel pendant ORD_MEASUREMENT** :
- Grid rendu via `drawCellGrid()` avec `assigned[]` et `orderMap[]` (lignes 244-245)
- Pad deja assigne → texte magenta avec position (ligne 257-258) :
  ```
  VT_MAGENTA "Key %d" VT_RESET VT_DIM " -- already assigned at position %d."
  ```
- Pad non encore assigne : pas de rank affiche dans la grille

**Fix** : dans `drawCellGrid()`, pour les pads non encore reassignes (`assigned[pad] == false`) qui avaient un ancien rank dans `existingOrder[]`, afficher ce rank en DIM/gris dans la cellule. Permet de voir la position precedente quand on ne veut changer qu'un pad sur 48.

---

## Hors scope bugs — Implementations planifiees

Ces items ne sont pas des bugs mais des features planifiees dans `docs/drafts/potfilter-implementation-plan.md` :

- **Phase 2** : SetupPotInput dual-mode (RELATIVE/ABSOLUTE) + pot universel dans T3/T4/T5/T6/T7
- **Phase 3** : Page Monitor temps reel dans T6 (raw vs stable vs delta, tuning live, save NVS)
