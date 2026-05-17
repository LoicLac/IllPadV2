// src/viewer/ViewerSerial.cpp
#include "ViewerSerial.h"
#include "../core/HardwareConfig.h"  // DEBUG_SERIAL
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace viewer {

namespace {

// Event in queue : 1 byte prio + 1 byte len + 254 bytes payload = 256 bytes
struct QueuedEvent {
  uint8_t prio;
  uint8_t len;
  char    line[254];
};

// Queue sizing per spec §6 : 32 slots × 256 bytes = 8 KB total
constexpr UBaseType_t QUEUE_DEPTH      = 32;
constexpr UBaseType_t TASK_PRIORITY    = 0;     // idle priority — safe vs main loop prio 1
constexpr uint32_t    TASK_STACK_BYTES = 4096;  // 4 KB stack — convention projet (cf. NvsManager.cpp:170, main.cpp:834)
constexpr BaseType_t  TASK_CORE        = 1;     // Core 1 (Core 0 saturé par sensingTask)

QueueHandle_t       s_queue           = nullptr;
TaskHandle_t        s_task            = nullptr;
std::atomic<bool>   s_viewerConnected{false};

void taskBody(void* /*arg*/) {
  QueuedEvent ev;
  for (;;) {
    bool nowConnected = (bool)Serial;
    bool wasConnected = s_viewerConnected.exchange(nowConnected, std::memory_order_acq_rel);

    if (!wasConnected && nowConnected) {
      // Phase 1.A : auto-resync hook empty. Phase 1.D wires it.
    }

    if (!nowConnected) {
      // Viewer absent — drain silently
      while (xQueueReceive(s_queue, &ev, 0) == pdPASS) { /* discard */ }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Drain with 10ms wait — if no event in 10ms, loop and re-check connection
    if (xQueueReceive(s_queue, &ev, pdMS_TO_TICKS(10)) == pdPASS) {
      // Serial.write peut retourner < ev.len si le ring buffer USB CDC est
      // plein malgre setTxTimeoutMs(0) (viewer host slow). Drop la ligne
      // entiere plutot qu'emettre une ligne tronquee qui confondrait le
      // parser viewer. Le viewer perdra cet event mais restera coherent.
      const uint8_t* buf = reinterpret_cast<const uint8_t*>(ev.line);
      if (Serial.availableForWrite() >= ev.len) {
        Serial.write(buf, ev.len);
      }
      // else : drop silencieux. La prochaine resync (auto-resync ou ?BOTH)
      // re-emettra l'etat complet.
    }
  }
}

}  // namespace

void begin() {
  s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(QueuedEvent));
  if (!s_queue) {
    // [FATAL] always-on (parser-recognized). Le module ne pourra rien emettre
    // ensuite (s_queue null = tous les emit() return immediatement). Le firmware
    // reste fonctionnel cote MIDI/LED/jeu live, juste invisible au viewer.
    Serial.println("[FATAL] viewer queue create failed");
    Serial.flush();
    return;
  }
  // NOTE: xTaskCreatePinnedToCore sur Arduino ESP32 attend la stack size **en bytes**
  // (le wrapper convertit vers words côté IDF). Cf. NvsManager.cpp:170 et main.cpp:834
  // qui passent 4096 directement. NE PAS diviser par sizeof(StackType_t).
  BaseType_t taskOk = xTaskCreatePinnedToCore(
    taskBody, "viewer",
    TASK_STACK_BYTES,                  // 4096 bytes direct
    nullptr, TASK_PRIORITY, &s_task, TASK_CORE);
  if (taskOk != pdPASS) {
    Serial.println("[FATAL] viewer task create failed");
    Serial.flush();
  }
}

void pollCommands() {
  // Phase 1.A : stub. Phase 1.D migrera pollRuntimeCommands ici.
  // L'existant pollRuntimeCommands() reste dans main.cpp pendant Phase 1.A-1.C.
}

bool isConnected() {
  return s_viewerConnected.load(std::memory_order_acquire);
}

}  // namespace viewer
