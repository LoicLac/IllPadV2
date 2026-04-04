# Bug Audit — Setup Tools — Worklog

Source: audit RTF 2026-04-04
Updated: 2026-04-04 (post PotFilter Phase 1)

## Legende
- DONE = verifie, plus rien a faire
- A FAIRE = confirme present, fix prevu
- A TESTER = fix applique, besoin de validation hardware

---

## DONE

| # | Bug | Resolution |
|---|-----|-----------|
| 1 | Freeze fleches Tool 7 | seedPotsForCursor() desactive le pot apres arrow. Pas un bug — design intentionnel. |
| 2 | Fleches mortes Tool 7 | Meme mecanisme que bug 1. Arrows modifient presetId puis re-seed. |
| 4 | Pipeline pot unifie | PotFilter Phase 1 implemente. 16x oversampling, deadband 20, sleep/wake, NVS. |

---

## A FAIRE — Quick wins (1 session)

### Bug 3 — Preset ne reinit pas le hue
`adjustColorField()` dans ToolLedSettings.cpp : changer de preset ne remet pas `hueOffset` a 0.
Fix : ajouter `slot.hueOffset = 0` apres changement de preset.

### Bug 5 — `q` = reboot immediat
`SetupManager.cpp` ligne 69 : `NAV_QUIT → '0'` → reboot sans confirmation.
Fix : supprimer le mapping `q → '0'`. Garder `q` inactif au menu principal.

### Bug 6 — Pas de confirmation reboot
`SetupManager.cpp` ligne 115 : `case '0'` → `ESP.restart()` apres 300ms.
Fix : ajouter prompt `y/n` avant reboot. Si `n`, retour au menu.

### Bug 7 — Supprimer needsReboot partout
Le flag est inutile : on reboot TOUJOURS pour quitter le setup.
Fichiers : ToolSettings.cpp (lignes 183, 267-270, 358-361, 122-123, 129-130), ToolLedSettings.cpp (ligne 1325), HardwareConfig.h (ligne 55).

### Bug 8 — Tick preview a 120 BPM
Fix : hardcoder `tickPreviewIntervalMs = 500` (120 BPM) pour la preview.

---

## A FAIRE — UX display (1 session)

### Bug 11 — Tool 4 navigation
Simplifier : un seul cycle LEFT/RIGHT qui traverse type + quantize, ENTER pour save.

### Bug 12 — BLE ON/OFF pas clair
Afficher d'abord ON/OFF, puis l'intervalle comme sous-parametre.

---

## A FAIRE — UX features (1-2 sessions)

### Bug 9 — Undo queue Tools 2+3
Pattern reutilisable `UndoRing<T, N>` pour Tool 3 (pad roles) ET Tool 2 (pad ordering).

### Bug 10 — Pad ordering ancien rank en gris
Afficher `orderMap[pad]` en DIM/gris pour les pads non encore reassignes.

---

## A FAIRE — Session dediee

### Bug 13 — Cal tool validation globale
Refactorer `CAL_MEASUREMENT` en mode libre. A traiter separement.

---

## Hors scope bugs — Implementations planifiees

Ces items ne sont pas des bugs mais des features planifiees dans `docs/plans/potfilter-implementation-plan.md` :

- **Phase 2** : SetupPotInput dual-mode (RELATIVE/ABSOLUTE) + pot universel dans T3/T4/T5/T6/T7
- **Phase 3** : Page Monitor temps reel dans T6 (raw vs stable vs delta, tuning live, save NVS)
