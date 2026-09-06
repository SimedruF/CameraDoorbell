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
