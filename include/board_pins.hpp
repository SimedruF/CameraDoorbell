#pragma once

namespace BoardPins {

// AI-Thinker ESP32-CAM / OV2640.
inline constexpr int kCameraPwdn = 32;
inline constexpr int kCameraReset = -1;
inline constexpr int kCameraXclk = 0;
inline constexpr int kCameraSiod = 26;
inline constexpr int kCameraSioc = 27;
inline constexpr int kCameraY9 = 35;
inline constexpr int kCameraY8 = 34;
inline constexpr int kCameraY7 = 39;
inline constexpr int kCameraY6 = 36;
inline constexpr int kCameraY5 = 21;
inline constexpr int kCameraY4 = 19;
inline constexpr int kCameraY3 = 18;
inline constexpr int kCameraY2 = 5;
inline constexpr int kCameraVsync = 25;
inline constexpr int kCameraHref = 23;
inline constexpr int kCameraPclk = 22;

// SD_MMC 1-bit mode uses GPIO2 (DATA0), GPIO14 (CLK), GPIO15 (CMD).
// GPIO4, GPIO12 and GPIO13 are therefore available to the application.
inline constexpr int kSdData0 = 2;
inline constexpr int kSdClock = 14;
inline constexpr int kSdCommand = 15;

inline constexpr int kFlashLed = 4;
inline constexpr int kDoorbellButton = 12;  // Active HIGH; keep LOW during boot.
inline constexpr int kRfTransmit = 13;      // FS1000A DATA, direct connection.

}  // namespace BoardPins

