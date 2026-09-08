#include "camera_service.hpp"

#include <esp_camera.h>
#include <esp_heap_caps.h>

#include "app_config.hpp"
#include "board_pins.hpp"
#include "config_service.hpp"

CameraService Camera;

namespace {

constexpr uint32_t kCameraTaskStackBytes = 8192;
constexpr uint8_t kFailuresBeforeRecovery = 5;
constexpr uint32_t kDriverRetryDelayMs = 2000;

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

  driverReady_ = initializeDriver();

  const BaseType_t created = xTaskCreatePinnedToCore(
      taskEntry, "camera_capture", kCameraTaskStackBytes, this, 4, nullptr, 1);
  if (created != pdPASS) {
    Serial.println("[camera] Cannot create capture task");
    if (driverReady_) esp_camera_deinit();
    driverReady_ = false;
    return false;
  }

  if (!driverReady_) {
    Serial.println("[camera] Driver unavailable; background recovery enabled");
  }
  return true;
}

bool CameraService::initializeDriver() {
  const RuntimeConfig settings = Settings.snapshot();

  // A software reset does not remove power from the OV2640. Pulse PWDN so a
  // camera left in an invalid state by a panic starts from a known state.
  pinMode(BoardPins::kCameraPwdn, OUTPUT);
  digitalWrite(BoardPins::kCameraPwdn, HIGH);
  delay(30);
  digitalWrite(BoardPins::kCameraPwdn, LOW);
  delay(30);

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
  config.frame_size = static_cast<framesize_t>(settings.frameSize);
  config.jpeg_quality = settings.jpegQuality;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  const esp_err_t error = esp_camera_init(&config);
  if (error != ESP_OK) {
    Serial.printf("[camera] Initialization failed: 0x%x\n", error);
    driverReady_ = false;
    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    sensor->set_framesize(sensor, static_cast<framesize_t>(settings.frameSize));
    sensor->set_quality(sensor, settings.jpegQuality);
    sensor->set_brightness(sensor, settings.brightness);
    sensor->set_contrast(sensor, settings.contrast);
    sensor->set_saturation(sensor, settings.saturation);
    sensor->set_special_effect(sensor, settings.specialEffect);
    sensor->set_whitebal(sensor, settings.whiteBalance);
    sensor->set_awb_gain(sensor, settings.awbGain);
    sensor->set_wb_mode(sensor, settings.whiteBalanceMode);
    sensor->set_exposure_ctrl(sensor, settings.autoExposure);
    sensor->set_aec2(sensor, settings.aec2);
    sensor->set_ae_level(sensor, settings.exposureLevel);
    sensor->set_aec_value(sensor, settings.exposureValue);
    sensor->set_gain_ctrl(sensor, settings.autoGain);
    sensor->set_agc_gain(sensor, settings.gainValue);
    sensor->set_gainceiling(sensor, static_cast<gainceiling_t>(settings.gainCeiling));
    sensor->set_bpc(sensor, settings.blackPixelCorrection);
    sensor->set_wpc(sensor, settings.whitePixelCorrection);
    sensor->set_raw_gma(sensor, settings.rawGamma);
    sensor->set_lenc(sensor, settings.lensCorrection);
    sensor->set_dcw(sensor, settings.downsize);
    sensor->set_hmirror(sensor, settings.horizontalMirror);
    sensor->set_vflip(sensor, settings.verticalFlip);
    sensor->set_colorbar(sensor, settings.colorBar);
  }

  driverReady_ = true;
  Serial.printf("[camera] OV2640 ready (JPEG, frame size=%u, quality=%u, %u FPS)\n",
                settings.frameSize, settings.jpegQuality, settings.frameRate);
  return true;
}

void CameraService::taskEntry(void* context) {
  static_cast<CameraService*>(context)->captureLoop();
}

void CameraService::captureLoop() {
  const RuntimeConfig settings = Settings.snapshot();
  const TickType_t frameInterval = pdMS_TO_TICKS(1000U / settings.frameRate);
  TickType_t lastWake = xTaskGetTickCount();
  uint8_t warmupFrames = 3;
  uint8_t consecutiveFailures = 0;
  bool recovering = false;
  uint32_t lastStackReportAt = millis();

  for (;;) {
    if (!driverReady_) {
      ready_ = false;
      if (initializeDriver()) {
        if (recovering) ++recoveries_;
        recovering = false;
        consecutiveFailures = 0;
        warmupFrames = 3;
        lastWake = xTaskGetTickCount();
      } else {
        recovering = true;
        vTaskDelay(pdMS_TO_TICKS(kDriverRetryDelayMs));
      }
      continue;
    }

    camera_fb_t* frame = esp_camera_fb_get();
    if (frame == nullptr) {
      ++captureFailures_;
      ++consecutiveFailures;
      Serial.printf("[camera] Capture failed (%u/%u)\n", consecutiveFailures,
                    kFailuresBeforeRecovery);
      if (consecutiveFailures >= kFailuresBeforeRecovery) {
        ready_ = false;
        Serial.println("[camera] Reinitializing OV2640 after repeated capture failures");
        esp_camera_deinit();
        driverReady_ = false;
        recovering = true;
        consecutiveFailures = 0;
      }
      lastWake = xTaskGetTickCount();
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    consecutiveFailures = 0;

    if (frame->format == PIXFORMAT_JPEG && warmupFrames == 0) {
      if (!publish(frame->buf, frame->len)) {
        Serial.println("[camera] Cannot publish frame to PSRAM");
      }
    } else if (warmupFrames > 0) {
      --warmupFrames;
    }

    esp_camera_fb_return(frame);

    if (millis() - lastStackReportAt >= 30000) {
      lastStackReportAt = millis();
      stackHighWatermark_ = uxTaskGetStackHighWaterMark(nullptr);
      Serial.printf("[camera] minimum free stack=%lu bytes\n",
                    static_cast<unsigned long>(stackHighWatermark_));
    }
    vTaskDelayUntil(&lastWake, frameInterval);
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
