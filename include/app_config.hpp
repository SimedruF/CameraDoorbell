#pragma once

#include <Arduino.h>
#include <esp_camera.h>

#if __has_include("secrets.hpp")
#include "secrets.hpp"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef DOORBELL_AP_PASSWORD
#define DOORBELL_AP_PASSWORD "change-me-now"
#endif

namespace AppConfig {

inline constexpr char kDeviceName[] = "camera-doorbell";
inline constexpr char kWifiSsid[] = WIFI_SSID;
inline constexpr char kWifiPassword[] = WIFI_PASSWORD;
inline constexpr char kAccessPointPassword[] = DOORBELL_AP_PASSWORD;

inline constexpr uint32_t kWifiConnectTimeoutMs = 15000;
inline constexpr uint32_t kFrameIntervalMs = 100;  // Target: 10 FPS.
inline constexpr framesize_t kFrameSize = FRAMESIZE_VGA;
inline constexpr int kJpegQuality = 12;

inline constexpr char kNtpServer1[] = "pool.ntp.org";
inline constexpr char kNtpServer2[] = "time.cloudflare.com";
inline constexpr long kGmtOffsetSeconds = 0;  // Media filenames use UTC.
inline constexpr int kDaylightOffsetSeconds = 0;

inline constexpr uint16_t kWebPort = 80;
inline constexpr uint16_t kStreamPort = 81;
inline constexpr size_t kStorageQueueLength = 4;
inline constexpr uint32_t kValidEpochThreshold = 1704067200UL;  // 2024-01-01 UTC.

}  // namespace AppConfig

