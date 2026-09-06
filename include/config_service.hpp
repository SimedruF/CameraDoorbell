#pragma once

#include <Arduino.h>

struct RuntimeConfig {
  char wifiSsid[33] = {};
  char wifiPassword[65] = {};
  char accessPointPassword[65] = {};
  bool storageEnabled = true;
  char photoDirectory[32] = "/photos";
  char videoDirectory[32] = "/videos";
  uint32_t minimumFreeMb = 128;

  uint8_t cameraProfile = 0;       // 0 manual/day, 1 stream, 2 photo, 3 night.
  uint8_t frameSize = 8;           // FRAMESIZE_VGA.
  uint8_t jpegQuality = 12;        // Lower means better quality.
  uint8_t frameRate = 10;
  int8_t brightness = 0;
  int8_t contrast = 0;
  int8_t saturation = 0;
  uint8_t specialEffect = 0;
  bool whiteBalance = true;
  bool awbGain = true;
  uint8_t whiteBalanceMode = 0;
  bool autoExposure = true;
  bool aec2 = false;
  int8_t exposureLevel = 0;
  uint16_t exposureValue = 300;
  bool autoGain = true;
  uint8_t gainValue = 0;
  uint8_t gainCeiling = 2;
  bool blackPixelCorrection = false;
  bool whitePixelCorrection = true;
  bool rawGamma = true;
  bool lensCorrection = true;
  bool downsize = true;
  bool horizontalMirror = false;
  bool verticalFlip = false;
  bool colorBar = false;
  bool flashEnabled = false;
  uint8_t flashIntensity = 128;

  bool radarEnabled = false;
  bool radarAutoPhoto = true;
  uint8_t radarTriggerMode = 0;  // 0 any presence, 1 moving, 2 stationary.
  uint16_t radarCooldownSeconds = 30;
};

class ConfigService {
 public:
  bool begin();
  RuntimeConfig snapshot();
  bool save(const RuntimeConfig& candidate, String& error);

  static bool validMediaDirectory(const char* path);

 private:
  void* mutex_ = nullptr;
  RuntimeConfig config_;
};

extern ConfigService Settings;
