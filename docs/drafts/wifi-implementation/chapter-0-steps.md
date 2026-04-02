# Chapter 0 — WiFi Common Infrastructure — Micro-etapes

> Exemple de decoupage pour comprendre la methode.
> Chaque etape : compile, boot, verification physique/visible.
> Ne passer a l'etape suivante que quand la precedente est verifiee.

---

## Etape 0.1 — Compile flag + WiFi AP nu

**Ce qu'on fait** :
- Ajouter `#define WIFI_STATUS_ENABLED 0` dans HardwareConfig.h
- Creer `src/wifi/WifiStatusServer.h/.cpp` — squelette minimal
- `begin()` = `WiFi.softAP("ILLPAD48", "illpad48")` + log IP
- Appel dans `setup()` derriere `#if WIFI_STATUS_ENABLED`
- Rien d'autre. Pas de HTTP, pas de WS.

**Taille** : ~30 lignes

**Verification** :
- Compiler avec `WIFI_STATUS_ENABLED 0` → firmware identique a avant (zero regression)
- Compiler avec `WIFI_STATUS_ENABLED 1` → boot normal + serial print `WiFi AP: 192.168.4.1`
- Sur ton telephone : le reseau "ILLPAD48" apparait dans la liste WiFi
- Te connecter au reseau → connecte (pas d'internet, normal)
- L'ILLPAD joue normalement (pads, MIDI, LEDs) → pas de regression

**Risque** : quasi nul. Le WiFi AP est isole, ne touche a rien.

---

## Etape 0.2 — Servir une page HTML statique

**Ce qu'on fait** :
- Ajouter `ESPAsyncWebServer` + `AsyncTCP` dans `lib_deps` de platformio.ini
- Dans WifiStatusServer : creer `AsyncWebServer` sur port 80
- Servir une page minimale sur GET `/` :
  ```html
  <h1>ILLPAD48</h1><p>WiFi OK</p>
  ```
  (en `const char[]` inline, pas encore PROGMEM gzippe)

**Taille** : ~15 lignes ajoutees

**Verification** :
- Connecte-toi au WiFi ILLPAD48 sur ton telephone/tablette
- Ouvre `http://192.168.4.1` dans le navigateur
- Tu vois "ILLPAD48 / WiFi OK"
- L'ILLPAD joue toujours normalement

**Risque** : faible. Premier contact avec ESPAsyncWebServer. Si ca compile pas → probleme de lib, on debug avant d'avancer.

---

## Etape 0.3 — WebSocket endpoint + heartbeat

**Ce qu'on fait** :
- Ajouter `AsyncWebSocket` sur `/ws`
- Callback `onEvent` : log connect/disconnect sur serial
- Timer heartbeat : envoyer `{"type":"hb"}` toutes les 3s a tous les clients
- Page HTML de l'etape 0.2 → ajouter 10 lignes de JS :
  ```js
  const ws = new WebSocket('ws://192.168.4.1/ws');
  ws.onmessage = e => document.body.innerHTML += '<br>' + e.data;
  ```

**Taille** : ~40 lignes ajoutees

**Verification** :
- Ouvre la page dans le navigateur
- Tu vois `{"type":"hb"}` apparaitre toutes les 3s
- Sur le serial monitor : "WS client connected" / "WS client disconnected"
- Ferme le navigateur → serial dit "disconnected"
- Rouvre → "connected" + heartbeats reprennent
- L'ILLPAD joue toujours normalement

**Risque** : faible. Le WS ne fait que push, pas de lecture de l'etat.

---

## Etape 0.4 — StatusPoller lit la bank active

**Ce qu'on fait** :
- Creer `src/wifi/StatusPoller.h/.cpp` — squelette
- `begin()` recoit une reference `const BankManager&`
- `update()` : compare `bankMgr.getCurrentBank()` avec `_lastBank`
  - Si different → envoie `{"type":"state","bank":N}`
  - Sinon → rien
- Appeler `statusPoller.update()` dans la loop, apres LedController, derriere `#if`

**Taille** : ~50 lignes (nouveau fichier)

**Verification** :
- Ouvre la page web
- Switch de bank sur l'ILLPAD (hold left + bank pad)
- Tu vois `{"type":"state","bank":3}` apparaitre dans le navigateur
- Switch encore → le numero change
- Le heartbeat continue en parallele
- Latence : quasi instantane (tu vois le message dans la seconde)

**Risque** : faible. Un seul getter const, read-only. C'est le premier pont entre les managers et le WiFi.

**Point important** : c'est ici qu'on valide que `statusPoller.update()` dans la loop ne degrade pas la latence MIDI. Si le MIDI devient moins reactif → probleme. On mesure avant/apres.

---

## Etape 0.5 — StatusPoller complet (snapshot)

**Ce qu'on fait** :
- Etendre le StatusPoller avec tous les champs du Snapshot :
  - banks (types, scales, velocity, PB)
  - arp (playing, hold, pattern, octave, notes)
  - tempo, BPM source
  - connections (USB, BLE)
  - batterie
  - button hold
- Chaque champ a son dirty check : compare avec `_lastSent`, envoie si different
- Throttle pots a 1Hz
- Sequence au connect : full snapshot

**Taille** : ~200 lignes (extension StatusPoller)

**Verification** (chaque action = un message visible dans le navigateur) :
- [ ] Switch bank → `state` change
- [ ] Change scale (hold left + root/mode pad) → `state` change
- [ ] Start/stop arp → `state` change
- [ ] Toggle hold → `state` change
- [ ] Tourne un pot → `pot` message (max 1/s)
- [ ] Branche/debranche BLE → `connections` change
- [ ] Appuie rear → `battery` message
- [ ] Hold left → `button_hold` message
- [ ] Reconnecte le navigateur → full snapshot recu

**Risque** : moyen. Beaucoup de getters a appeler. Certains n'existent pas encore (voir liste dans wifi-common.md). Il faudra les ajouter (one-liners const). Tester chaque champ un par un.

---

## Etape 0.6 — CommandHandler : premiere commande (arp_stop)

**Ce qu'on fait** :
- Creer `src/wifi/CommandHandler.h/.cpp`
- UNE seule commande : `{"cmd":"arp_stop","bank":2}`
- Queue dirty-flag : `_arpStopDirty[bank]` (atomic bool) + `_pendingArpStop[bank]`
- `queueCommand()` : parse JSON, set flag (appele depuis callback WS)
- `processQueue()` : check flag, appelle `arpEngine.stop()` (appele depuis loop)
- Dans le callback WS `onEvent` : si message texte → `commandHandler.queueCommand()`
- Dans la loop : `commandHandler.processQueue(arpEngines)`

**Taille** : ~80 lignes (nouveau fichier, volontairement minimal)

**Verification** :
- Un arp tourne sur bank 2
- Dans la console JS du navigateur, tape :
  ```js
  ws.send(JSON.stringify({cmd:"arp_stop",bank:2}));
  ```
- L'arp s'arrete sur l'ILLPAD (tu l'entends/vois sur les LEDs)
- Le StatusPoller detecte le changement et envoie `state` avec `playing:false`
- Refais-le sur bank 0, bank 7 → meme resultat
- Envoie `bank:99` → rien ne se passe (validation range)
- Envoie pendant que tu joues activement → pas de crash, pas de notes stuck

**Risque** : **CRITIQUE**. C'est la premiere ecriture depuis le WiFi vers les managers.
On verifie specifiquement :
- Pas de notes stuck apres le stop
- Pas de crash si on spamme la commande
- Pas de degradation MIDI pendant l'envoi
- Le flag atomic fonctionne (la commande est appliquee au bon moment)

**Si ca marche** : le pattern queue est valide. Toutes les commandes suivantes utilisent le meme mecanisme.

---

## Etape 0.7 — CommandHandler : toutes les commandes Live

**Ce qu'on fait** :
- Etendre CommandHandler avec toutes les commandes :
  - arp_start, arp_stop, arp_hold
  - scale_root, scale_mode, scale_chrom
  - arp_octave, arp_gate, arp_shuffle_depth, arp_shuffle_template, arp_division, arp_pattern
  - velocity_base, velocity_var, pitch_bend
- Validation : ranges, bank type (arp commands sur bank NORMAL → rejet)
- Regle de souverainete : commandes pot sur bank active → rejet silencieux

**Taille** : ~200 lignes (extension CommandHandler)

**Verification** (chaque commande testee depuis la console JS) :
- [ ] arp_start → arp demarre, LED confirme
- [ ] arp_hold toggle → hold change
- [ ] scale_root → notes changent (tu entends la transposition)
- [ ] scale_mode → idem
- [ ] scale_chrom → bascule chromatique
- [ ] arp_gate sur bank background → gate change (audible)
- [ ] arp_gate sur bank active → rejetee (rien ne change)
- [ ] velocity_base → prochaines notes ont la nouvelle velocity
- [ ] pitch_bend → offset change
- [ ] Commande invalide (JSON mal forme) → rien ne crash

**Risque** : moyen. Le pattern est valide (prouve a 0.6), mais chaque commande touche un manager different. Tester un par un.

---

## Etape 0.8 — Page Live V1 (vraie page HTML)

**Ce qu'on fait** :
- Remplacer la page de test par la vraie page Live (HTML/CSS/JS)
- Gzipper en PROGMEM (`status_page.h`)
- La page affiche l'etat (status read-only pour l'instant)
- Pas encore de controles interactifs (juste l'affichage)

**Taille** : ~150 lignes C++ (gzip tool + PROGMEM serve) + la page HTML

**Verification** :
- Ouvre la page → tu vois le dashboard avec header, banks, colonnes, pots
- Switch de bank → l'affichage suit
- Change scale → l'affichage suit
- Start/stop arp → l'affichage suit
- Tourne un pot → la valeur change (throttled 1Hz)

**Risque** : faible cote firmware (la plomberie est en place). Le risque est cote HTML/JS (bugs d'affichage), mais ca ne touche pas la stabilite de l'ILLPAD.

---

## Etape 0.9 — Controles interactifs sur la page Live

**Ce qu'on fait** :
- Rendre les elements cliquables (les boutons envoient les commandes JSON via WS)
- Ajouter le mode follow (la page suit la bank foreground)

**Taille** : ~100 lignes JS ajoutees

**Verification** :
- Tap "Stop" sur bank arp → l'arp s'arrete
- Tap root → la root change (audible)
- Tap sur une bank background → les details changent dans les colonnes
- Mode follow ON → quand tu switch bank sur l'ILLPAD, la page suit
- Mode follow OFF → la page reste sur la bank selectionnee

**Risque** : faible. Le CommandHandler est deja teste. C'est juste du JS cote client.

---

## Etape 0.10 — NVS auto-persist

**Ce qu'on fait** :
- Verifier que les changements web passent par le meme chemin NVS que les pots
- Normalement c'est deja le cas (CommandHandler appelle les memes setters que la loop)
- Si les managers ne marquent pas leurs dirty flags quand appeles depuis processQueue → corriger

**Taille** : potentiellement 0 lignes (si ca marche deja)

**Verification** :
- Change la scale de bank 3 depuis la tablette
- Reboote l'ILLPAD
- Bank 3 a la nouvelle scale → NVS a persiste
- Change la gate de bank 2 background depuis la tablette
- Reboote → bank 2 a la nouvelle gate

**Risque** : faible. Mais c'est une verification importante — si ca ne persiste pas, il faut comprendre pourquoi.

---

## Resume

| Etape | Quoi | Lignes | Risque | Verification |
|---|---|---|---|---|
| 0.1 | WiFi AP nu | ~30 | Nul | Reseau visible sur telephone |
| 0.2 | Page HTML statique | ~15 | Faible | Page visible dans navigateur |
| 0.3 | WebSocket + heartbeat | ~40 | Faible | Heartbeats dans navigateur |
| 0.4 | StatusPoller (1 champ) | ~50 | Faible | Bank switch visible dans navigateur |
| 0.5 | StatusPoller complet | ~200 | Moyen | Checklist 9 actions |
| 0.6 | CommandHandler (1 cmd) | ~80 | **CRITIQUE** | arp_stop depuis console JS |
| 0.7 | CommandHandler complet | ~200 | Moyen | Checklist toutes commandes |
| 0.8 | Page Live HTML reelle | ~150 | Faible | Dashboard visuel complet |
| 0.9 | Controles interactifs | ~100 | Faible | Tap → action sur ILLPAD |
| 0.10 | NVS persist | ~0 | Faible | Reboot → valeurs conservees |

**Total** : ~10 etapes, ~900 lignes, chacune verifiable independamment.

**Rythme typique** : 1-3 etapes par session Claude. Les etapes faciles (0.1-0.3) peuvent se chainer. L'etape critique (0.6) merite une session dediee.
