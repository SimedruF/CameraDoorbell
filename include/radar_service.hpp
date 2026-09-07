#pragma once

#include <Arduino.h>

struct RadarStatus {
  bool enabled = false;
  bool connected = false;
  bool presence = false;
  bool moving = false;
  bool stationary = false;
  uint16_t movingDistanceCm = 0;
  uint8_t movingEnergy = 0;
  uint16_t stationaryDistanceCm = 0;
  uint8_t stationaryEnergy = 0;
  uint16_t detectionDistanceCm = 0;
  uint32_t lastFrameAgeMs = UINT32_MAX;
  uint32_t eventSequence = 0;
  uint32_t photosTriggered = 0;
  uint32_t videosTriggered = 0;
};

class RadarService {
 public:
  bool begin();
  RadarStatus status();

 private:
  static void taskEntry(void* context);
  void taskLoop();

  void* mutex_ = nullptr;
  RadarStatus status_;
};

extern RadarService Radar;
