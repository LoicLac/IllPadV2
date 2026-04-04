# NVS — Considerations pour l'implementation WiFi

> Notes a prendre en compte lors de l'implementation du WiFi setup (SetupWsHandler).
> Ref : docs/NVS/nvs-unification-spec.md (section 9), docs/NVS/nvs-unification-implementation.md

---

## 1. CRC16 anti-corruption

### Le probleme

`loadBlob` valide magic + version + taille, mais pas l'integrite des donnees. Un save interrompu (coupure batterie mid-write NVS) peut produire un blob avec le bon header mais des donnees partiellement ecrites. Ce risque est identique en VT100 et en WiFi.

### Pourquoi pas maintenant

- Ajouter un champ `crc16` a chaque Store struct bumpe toutes les versions et casse la NVS existante
- On est en phase stabilisation — le risque de corruption est rare sur ESP32 (wear leveling + journaling interne NVS)
- Le WiFi spec doit definir sa strategie d'integrite globale (CRC NVS + confirmation WebSocket + validation JSON) avant de decider

### Proposition d'implementation

Quand le WiFi sera pret :

1. Ajouter `uint16_t crc16;` en **dernier champ** de chaque Store struct (avant le padding d'alignement)
2. Calculer le CRC sur `[magic..dernier_champ_avant_crc]` avant chaque `saveBlob`
3. Verifier le CRC dans `readAndValidateBlob` (apres magic/version, avant memcpy dans out)
4. Bumper la version de chaque struct (+1)
5. Les validate functions ne changent pas — le CRC est verifie dans le helper, pas dans la validation metier

**Cout** : +2 octets par struct, ~20 lignes dans `readAndValidateBlob` + `saveBlob`, 1 fonction CRC16 (~15 lignes). Pas de changement dans les Tools.

**Alternative** : ne pas ajouter de CRC si le WiFi handler fait une relecture + comparaison apres chaque save (read-after-write verify). Plus lourd en I/O mais ne touche pas aux structs.

---

## 2. Thread safety NVS

### Le probleme

`loadBlob`/`saveBlob`/`checkBlob` sont des fonctions statiques sans mutex. `Preferences` (ESP-IDF NVS wrapper) n'est **pas thread-safe**. Si le VT100 setup (Core 1, loop task) et le WiFi handler (Core 0 ou Core 1, task HTTP/WS) appellent ces fonctions en meme temps → race condition → corruption.

Le runtime async (NvsManager dirty flags + FreeRTOS task) est deja protege par le pattern atomique, mais la couche setup ne l'est pas.

### Options

**Option A — Exclusion mutuelle des modes setup (recommandee pour le proto)**

Un seul mode setup actif a la fois : soit VT100 (serial), soit WiFi. Le firmware refuse d'entrer en WiFi setup si le VT100 est actif, et vice versa. Pas de mutex necessaire.

Implementation : un `std::atomic<uint8_t> s_setupMode` (0=aucun, 1=VT100, 2=WiFi) verifie a l'entree de chaque mode.

**Option B — Mutex dans les helpers**

Ajouter un `static SemaphoreHandle_t s_nvsMutex` dans `readAndValidateBlob`/`saveBlob`. Chaque helper prend le mutex, fait son travail, le rend.

```cpp
static SemaphoreHandle_t s_nvsMutex = xSemaphoreCreateMutex();

bool NvsManager::saveBlob(const char* ns, const char* key, const void* data, size_t size) {
  if (xSemaphoreTake(s_nvsMutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
  // ... save logic ...
  xSemaphoreGive(s_nvsMutex);
  return ok;
}
```

Plus robuste mais ajoute de la latence et de la complexite. Surtout utile si on veut supporter VT100 + WiFi simultanes (peu probable sur un proto).

### Recommandation

Option A pour le proto. Option B si le produit final doit supporter les deux modes en parallele.

---

## 3. Fonctions validate — deja pretes

Les fonctions `validateXxxStore()` (ajoutees dans `KeyboardData.h` par le plan NVS unification) sont directement utilisables par le WiFi handler :

```cpp
void SetupWsHandler::handleSettingsUpdate(const JsonObject& cmd) {
  SettingsStore wk;
  if (!NvsManager::loadBlob("illpad_set", "settings", EEPROM_MAGIC, SETTINGS_VERSION, &wk, sizeof(wk)))
    wk = settingsDefaults();
  applyJsonToSettings(cmd, wk);       // champs modifies depuis le JSON
  validateSettingsStore(wk);           // bornes validees — meme fonction que loadAll et Tool 5
  wk.magic = EEPROM_MAGIC;
  wk.version = SETTINGS_VERSION;
  NvsManager::saveBlob("illpad_set", "settings", &wk, sizeof(wk));
}
```

Pas de logique de validation a dupliquer. C'est l'objectif du point 7 des Design Principles du plan NVS unification.

---

## 4. Descriptor table — deja prete

`NVS_DESCRIPTORS[]` permet au WiFi handler d'iterer sur tous les stores pour renvoyer un status JSON :

```cpp
void SetupWsHandler::handleStatusRequest() {
  JsonObject status;
  for (uint8_t i = 0; i < NVS_DESCRIPTOR_COUNT; i++) {
    status[NVS_DESCRIPTORS[i].ns] = NvsManager::checkBlob(
      NVS_DESCRIPTORS[i].ns, NVS_DESCRIPTORS[i].key,
      NVS_DESCRIPTORS[i].magic, NVS_DESCRIPTORS[i].version,
      NVS_DESCRIPTORS[i].size);
  }
  sendJson(status);
}
```
