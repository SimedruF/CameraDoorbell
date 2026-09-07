#include "radar_service.hpp"

#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ld2410.h>

#include "board_pins.hpp"
#include "config_service.hpp"
#include "storage_service.hpp"

RadarService Radar;

namespace {

HardwareSerial radarSerial(1);
ld2410 radarDevice;

SemaphoreHandle_t asSemaphore(void* value) {
  return static_cast<SemaphoreHandle_t>(value);
}

bool triggerIsActive(const RuntimeConfig& config, bool presence, bool moving,
                     bool stationary) {
  if (config.radarTriggerMode == 1) return moving;
  if (config.radarTriggerMode == 2) return stationary;
  return presence;
}

}  // namespace

bool RadarService::begin() {
  const RuntimeConfig config = Settings.snapshot();
  status_.enabled = config.radarEnabled;
  if (!config.radarEnabled) {
    Serial.println("[radar] HLK-LD2410C disabled");
    return true;
  }

  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) {
    Serial.println("[radar] Cannot create status mutex");
    return false;
  }

  // RX-only: configure radar parameters through Bluetooth. Using GPIO3 means
  // radar TX must be disconnected while the ESP32-CAM is being programmed.
  radarSerial.setRxBufferSize(512);
  radarSerial.begin(256000, SERIAL_8N1, BoardPins::kRadarReceive, -1);
  radarDevice.begin(radarSerial, false);

  const BaseType_t created =
      xTaskCreatePinnedToCore(taskEntry, "ld2410", 4096, this, 3, nullptr, 0);
  if (created != pdPASS) {
    Serial.println("[radar] Cannot create radar task");
    radarSerial.end();
    return false;
  }

  Serial.printf("[radar] Waiting for HLK-LD2410C on GPIO%d at 256000 baud\n",
                BoardPins::kRadarReceive);
  return true;
}

void RadarService::taskEntry(void* context) {
  static_cast<RadarService*>(context)->taskLoop();
}

void RadarService::taskLoop() {
  const RuntimeConfig config = Settings.snapshot();
  bool previousTrigger = false;
  uint32_t lastTriggerAt = 0;
  uint32_t lastFrameAt = 0;

  for (;;) {
    radarDevice.read();
    const uint32_t now = millis();
    if (radarDevice.isConnected()) lastFrameAt = now;
    // The sensor normally reports close to the library's 100 ms timeout.
    // Keep the public connection state stable across individual frame gaps.
    const bool connected = lastFrameAt != 0 && now - lastFrameAt <= 1000;
    const bool presence = connected && radarDevice.presenceDetected();
    const bool moving = connected && radarDevice.movingTargetDetected();
    const bool stationary = connected && radarDevice.stationaryTargetDetected();
    const bool trigger = triggerIsActive(config, presence, moving, stationary);

    bool photoQueued = false;
    bool videoQueued = false;
    if (trigger && !previousTrigger && config.radarAutoPhoto &&
        (lastTriggerAt == 0 || now - lastTriggerAt >= config.radarCooldownSeconds * 1000UL)) {
      photoQueued = Storage.requestPhotoSave();
      if (config.radarAutoVideo) videoQueued = Storage.requestVideoStart();
      lastTriggerAt = now;
      Serial.printf("[radar] Presence event; photo %s, video %s\n",
                    photoQueued ? "queued" : "not queued",
                    videoQueued ? "queued" : "not queued");
    } else if (trigger && !previousTrigger && config.radarAutoVideo &&
               (lastTriggerAt == 0 ||
                now - lastTriggerAt >= config.radarCooldownSeconds * 1000UL)) {
      videoQueued = Storage.requestVideoStart();
      lastTriggerAt = now;
      Serial.printf("[radar] Presence event; video %s\n",
                    videoQueued ? "queued" : "not queued");
    }

    if (xSemaphoreTake(asSemaphore(mutex_), pdMS_TO_TICKS(20)) == pdTRUE) {
      const bool risingEdge = trigger && !previousTrigger;
      status_.enabled = true;
      status_.connected = connected;
      status_.presence = presence;
      status_.moving = moving;
      status_.stationary = stationary;
      status_.movingDistanceCm = connected ? radarDevice.movingTargetDistance() : 0;
      status_.movingEnergy = connected ? radarDevice.movingTargetEnergy() : 0;
      status_.stationaryDistanceCm = connected ? radarDevice.stationaryTargetDistance() : 0;
      status_.stationaryEnergy = connected ? radarDevice.stationaryTargetEnergy() : 0;
      status_.detectionDistanceCm = connected ? radarDevice.detectionDistance() : 0;
      status_.lastFrameAgeMs = lastFrameAt == 0 ? UINT32_MAX : now - lastFrameAt;
      if (risingEdge) ++status_.eventSequence;
      if (photoQueued) ++status_.photosTriggered;
      if (videoQueued) ++status_.videosTriggered;
      xSemaphoreGive(asSemaphore(mutex_));
    }

    previousTrigger = trigger;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

RadarStatus RadarService::status() {
  RadarStatus result = status_;
  if (mutex_ != nullptr && xSemaphoreTake(asSemaphore(mutex_), pdMS_TO_TICKS(50)) == pdTRUE) {
    result = status_;
    xSemaphoreGive(asSemaphore(mutex_));
  }
  return result;
}
