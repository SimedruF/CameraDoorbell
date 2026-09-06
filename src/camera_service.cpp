#include "camera_service.hpp"

#include <esp_camera.h>
#include <esp_heap_caps.h>

#include "app_config.hpp"
#include "board_pins.hpp"

CameraService Camera;

namespace {

uint8_t* resizePsramBuffer(uint8_t* current, size_t required) {
  uint8_t* resized = static_cast<uint8_t*>(
      heap_caps_realloc(current, required, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (resized == nullptr) {
    resized = static_cast<uint8_t*>(heap_caps_realloc(current, required, MALLOC_CAP_8BIT));
  }
  return resized;
}

}  // namespace

bool CameraService::begin() {
  if (!psramFound()) {
    Serial.println("[camera] PSRAM was not detected; refusing to start VGA streaming");
    return false;
  }

  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) {
    Serial.println("[camera] Cannot create frame mutex");
    return false;
  }

  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = BoardPins::kCameraY2;
  config.pin_d1 = BoardPins::kCameraY3;
  config.pin_d2 = BoardPins::kCameraY4;
  config.pin_d3 = BoardPins::kCameraY5;
  config.pin_d4 = BoardPins::kCameraY6;
  config.pin_d5 = BoardPins::kCameraY7;
  config.pin_d6 = BoardPins::kCameraY8;
  config.pin_d7 = BoardPins::kCameraY9;
  config.pin_xclk = BoardPins::kCameraXclk;
  config.pin_pclk = BoardPins::kCameraPclk;
  config.pin_vsync = BoardPins::kCameraVsync;
  config.pin_href = BoardPins::kCameraHref;
  config.pin_sccb_sda = BoardPins::kCameraSiod;
  config.pin_sccb_scl = BoardPins::kCameraSioc;
  config.pin_pwdn = BoardPins::kCameraPwdn;
  config.pin_reset = BoardPins::kCameraReset;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = AppConfig::kFrameSize;
  config.jpeg_quality = AppConfig::kJpegQuality;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  const esp_err_t error = esp_camera_init(&config);
  if (error != ESP_OK) {
    Serial.printf("[camera] Initialization failed: 0x%x\n", error);
    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    sensor->set_framesize(sensor, AppConfig::kFrameSize);
  }

  const BaseType_t created =
      xTaskCreatePinnedToCore(taskEntry, "camera_capture", 4096, this, 4, nullptr, 1);
  if (created != pdPASS) {
    Serial.println("[camera] Cannot create capture task");
    esp_camera_deinit();
    return false;
  }

  Serial.println("[camera] OV2640 ready (JPEG, VGA, two PSRAM frame buffers)");
  return true;
}

void CameraService::taskEntry(void* context) {
  static_cast<CameraService*>(context)->captureLoop();
}

void CameraService::captureLoop() {
  TickType_t lastWake = xTaskGetTickCount();
  uint8_t warmupFrames = 3;

  for (;;) {
    camera_fb_t* frame = esp_camera_fb_get();
    if (frame == nullptr) {
      Serial.println("[camera] Capture failed");
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    if (frame->format == PIXFORMAT_JPEG && warmupFrames == 0) {
      if (!publish(frame->buf, frame->len)) {
        Serial.println("[camera] Cannot publish frame to PSRAM");
      }
    } else if (warmupFrames > 0) {
      --warmupFrames;
    }

    esp_camera_fb_return(frame);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(AppConfig::kFrameIntervalMs));
  }
}

bool CameraService::publish(const uint8_t* data, size_t length) {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(250)) != pdTRUE) {
    return false;
  }

  bool success = true;
  if (latestCapacity_ < length) {
    uint8_t* resized = resizePsramBuffer(latestData_, length);
    if (resized == nullptr) {
      success = false;
    } else {
      latestData_ = resized;
      latestCapacity_ = length;
    }
  }

  if (success) {
    memcpy(latestData_, data, length);
    latestLength_ = length;
    capturedAtMs_ = millis();
    ++sequence_;
    ready_ = true;
  }

  xSemaphoreGive(mutex_);
  return success;
}

bool CameraService::copyLatest(FrameCopy& destination, TickType_t timeout) {
  if (!ready_ || xSemaphoreTake(mutex_, timeout) != pdTRUE) {
    return false;
  }

  bool success = latestData_ != nullptr && latestLength_ > 0;
  if (success && destination.capacity < latestLength_) {
    uint8_t* resized = resizePsramBuffer(destination.data, latestLength_);
    if (resized == nullptr) {
      success = false;
    } else {
      destination.data = resized;
      destination.capacity = latestLength_;
    }
  }

  if (success) {
    memcpy(destination.data, latestData_, latestLength_);
    destination.length = latestLength_;
    destination.sequence = sequence_;
    destination.capturedAtMs = capturedAtMs_;
  }

  xSemaphoreGive(mutex_);
  return success;
}

void CameraService::release(FrameCopy& frame) {
  if (frame.data != nullptr) {
    heap_caps_free(frame.data);
  }
  frame = {};
}

uint32_t CameraService::latestFrameAgeMs() const {
  if (!ready_) {
    return UINT32_MAX;
  }
  return millis() - capturedAtMs_;
}
