#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct FrameCopy {
  uint8_t* data = nullptr;
  size_t length = 0;
  size_t capacity = 0;
  uint32_t sequence = 0;
  uint32_t capturedAtMs = 0;
};

class CameraService {
 public:
  bool begin();
  bool copyLatest(FrameCopy& destination, TickType_t timeout = pdMS_TO_TICKS(250));
  void release(FrameCopy& frame);

  bool ready() const { return ready_; }
  uint32_t latestSequence() const { return sequence_; }
  uint32_t latestFrameAgeMs() const;
  uint32_t captureFailures() const { return captureFailures_; }
  uint32_t recoveries() const { return recoveries_; }
  uint32_t stackHighWatermark() const { return stackHighWatermark_; }

 private:
  static void taskEntry(void* context);
  void captureLoop();
  bool initializeDriver();
  bool publish(const uint8_t* data, size_t length);

  SemaphoreHandle_t mutex_ = nullptr;
  uint8_t* latestData_ = nullptr;
  size_t latestLength_ = 0;
  size_t latestCapacity_ = 0;
  volatile uint32_t sequence_ = 0;
  volatile uint32_t capturedAtMs_ = 0;
  volatile uint32_t captureFailures_ = 0;
  volatile uint32_t recoveries_ = 0;
  volatile uint32_t stackHighWatermark_ = 0;
  volatile bool driverReady_ = false;
  volatile bool ready_ = false;
};

extern CameraService Camera;
