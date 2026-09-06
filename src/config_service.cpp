#include "config_service.hpp"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "app_config.hpp"

ConfigService Settings;

namespace {

constexpr char kNamespace[] = "doorbell";

SemaphoreHandle_t asSemaphore(void* value) {
  return static_cast<SemaphoreHandle_t>(value);
}

void copyPreference(Preferences& preferences, const char* key, const char* fallback,
                    char* destination, size_t destinationSize) {
  const String value = preferences.getString(key, fallback);
  strlcpy(destination, value.c_str(), destinationSize);
}

bool validPassword(const char* password) {
  const size_t length = strlen(password);
  return length >= 8 && length <= 63;
}

bool validFrameSize(uint8_t value) {
  switch (value) {
    case 1:   // QQVGA 160x120
    case 3:   // HQVGA 240x176
    case 5:   // QVGA 320x240
    case 6:   // CIF 400x296
    case 8:   // VGA 640x480
    case 9:   // SVGA 800x600
    case 10:  // XGA 1024x768
    case 11:  // HD 1280x720
    case 12:  // SXGA 1280x1024
    case 13:  // UXGA 1600x1200
      return true;
    default:
      return false;
  }
}

}  // namespace

bool ConfigService::begin() {
  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) {
    Serial.println("[config] Cannot create settings mutex");
    return false;
  }

  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    Serial.println("[config] Cannot open NVS; using firmware defaults");
    strlcpy(config_.wifiSsid, AppConfig::kWifiSsid, sizeof(config_.wifiSsid));
    strlcpy(config_.wifiPassword, AppConfig::kWifiPassword, sizeof(config_.wifiPassword));
    strlcpy(config_.accessPointPassword, AppConfig::kAccessPointPassword,
            sizeof(config_.accessPointPassword));
    return false;
  }

  copyPreference(preferences, "wifi_ssid", AppConfig::kWifiSsid, config_.wifiSsid,
                 sizeof(config_.wifiSsid));
  copyPreference(preferences, "wifi_pass", AppConfig::kWifiPassword, config_.wifiPassword,
                 sizeof(config_.wifiPassword));
  copyPreference(preferences, "ap_pass", AppConfig::kAccessPointPassword,
                 config_.accessPointPassword, sizeof(config_.accessPointPassword));
  copyPreference(preferences, "photo_dir", "/photos", config_.photoDirectory,
                 sizeof(config_.photoDirectory));
  copyPreference(preferences, "video_dir", "/videos", config_.videoDirectory,
                 sizeof(config_.videoDirectory));
  config_.storageEnabled = preferences.getBool("sd_enabled", true);
  config_.minimumFreeMb = preferences.getUInt("min_free_mb", 128);
  config_.cameraProfile = preferences.getUChar("cam_profile", 0);
  config_.frameSize = preferences.getUChar("cam_size", 8);
  config_.jpegQuality = preferences.getUChar("jpg_quality", 12);
  config_.frameRate = preferences.getUChar("cam_fps", 10);
  config_.brightness = preferences.getChar("brightness", 0);
  config_.contrast = preferences.getChar("contrast", 0);
  config_.saturation = preferences.getChar("saturation", 0);
  config_.specialEffect = preferences.getUChar("effect", 0);
  config_.whiteBalance = preferences.getBool("whitebal", true);
  config_.awbGain = preferences.getBool("awb_gain", true);
  config_.whiteBalanceMode = preferences.getUChar("wb_mode", 0);
  config_.autoExposure = preferences.getBool("aec", true);
  config_.aec2 = preferences.getBool("aec2", false);
  config_.exposureLevel = preferences.getChar("ae_level", 0);
  config_.exposureValue = preferences.getUShort("aec_value", 300);
  config_.autoGain = preferences.getBool("agc", true);
  config_.gainValue = preferences.getUChar("agc_gain", 0);
  config_.gainCeiling = preferences.getUChar("gain_ceil", 2);
  config_.blackPixelCorrection = preferences.getBool("bpc", false);
  config_.whitePixelCorrection = preferences.getBool("wpc", true);
  config_.rawGamma = preferences.getBool("raw_gma", true);
  config_.lensCorrection = preferences.getBool("lenc", true);
  config_.downsize = preferences.getBool("dcw", true);
  config_.horizontalMirror = preferences.getBool("hmirror", false);
  config_.verticalFlip = preferences.getBool("vflip", false);
  config_.colorBar = preferences.getBool("colorbar", false);
  config_.flashEnabled = preferences.getBool("flash", false);
  config_.flashIntensity = preferences.getUChar("flash_level", 128);
  config_.radarEnabled = preferences.getBool("radar_enabled", false);
  config_.radarAutoPhoto = preferences.getBool("radar_photo", true);
  config_.radarTriggerMode = preferences.getUChar("radar_trigger", 0);
  config_.radarCooldownSeconds = preferences.getUShort("radar_cool", 30);
  preferences.end();

  // Recover gracefully if NVS contains values written by an older/broken firmware.
  if (!validPassword(config_.accessPointPassword)) {
    strlcpy(config_.accessPointPassword, AppConfig::kAccessPointPassword,
            sizeof(config_.accessPointPassword));
  }
  if (!validMediaDirectory(config_.photoDirectory)) {
    strlcpy(config_.photoDirectory, "/photos", sizeof(config_.photoDirectory));
  }
  if (!validMediaDirectory(config_.videoDirectory)) {
    strlcpy(config_.videoDirectory, "/videos", sizeof(config_.videoDirectory));
  }
  if (!validFrameSize(config_.frameSize)) config_.frameSize = 8;
  config_.cameraProfile = constrain(config_.cameraProfile, 0, 3);
  config_.jpegQuality = constrain(config_.jpegQuality, 4, 63);
  config_.frameRate = constrain(config_.frameRate, 1, 30);
  config_.brightness = constrain(config_.brightness, -2, 2);
  config_.contrast = constrain(config_.contrast, -2, 2);
  config_.saturation = constrain(config_.saturation, -2, 2);
  config_.specialEffect = constrain(config_.specialEffect, 0, 6);
  config_.whiteBalanceMode = constrain(config_.whiteBalanceMode, 0, 4);
  config_.exposureLevel = constrain(config_.exposureLevel, -2, 2);
  config_.exposureValue = constrain(config_.exposureValue, 0, 1200);
  config_.gainValue = constrain(config_.gainValue, 0, 30);
  config_.gainCeiling = constrain(config_.gainCeiling, 0, 6);
  config_.radarTriggerMode = constrain(config_.radarTriggerMode, 0, 2);
  config_.radarCooldownSeconds = constrain(config_.radarCooldownSeconds, 1, 3600);

  Serial.println("[config] Runtime settings loaded from NVS");
  return true;
}

RuntimeConfig ConfigService::snapshot() {
  RuntimeConfig result;
  if (mutex_ != nullptr && xSemaphoreTake(asSemaphore(mutex_), pdMS_TO_TICKS(250)) == pdTRUE) {
    result = config_;
    xSemaphoreGive(asSemaphore(mutex_));
  } else {
    result = config_;
  }
  return result;
}

bool ConfigService::save(const RuntimeConfig& candidate, String& error) {
  const size_t wifiPasswordLength = strlen(candidate.wifiPassword);
  if (candidate.wifiSsid[0] != '\0' && wifiPasswordLength > 0 && wifiPasswordLength < 8) {
    error = "Parola Wi-Fi trebuie sa fie goala pentru o retea deschisa sau sa aiba minimum 8 caractere.";
    return false;
  }
  if (!validPassword(candidate.accessPointPassword)) {
    error = "Parola access point trebuie sa aiba intre 8 si 63 de caractere.";
    return false;
  }
  if (!validMediaDirectory(candidate.photoDirectory) ||
      !validMediaDirectory(candidate.videoDirectory)) {
    error = "Directoarele trebuie sa inceapa cu / si sa contina doar litere, cifre, -, _ si /.";
    return false;
  }
  if (strcmp(candidate.photoDirectory, candidate.videoDirectory) == 0) {
    error = "Directoarele pentru fotografii si filme trebuie sa fie diferite.";
    return false;
  }
  if (candidate.minimumFreeMb > 16384) {
    error = "Pragul de spatiu liber nu poate depasi 16384 MB.";
    return false;
  }
  if (candidate.cameraProfile > 3 || !validFrameSize(candidate.frameSize) ||
      candidate.jpegQuality < 4 || candidate.jpegQuality > 63 || candidate.frameRate < 1 ||
      candidate.frameRate > 30 || candidate.brightness < -2 || candidate.brightness > 2 ||
      candidate.contrast < -2 || candidate.contrast > 2 || candidate.saturation < -2 ||
      candidate.saturation > 2 || candidate.specialEffect > 6 ||
      candidate.whiteBalanceMode > 4 || candidate.exposureLevel < -2 ||
      candidate.exposureLevel > 2 || candidate.exposureValue > 1200 ||
      candidate.gainValue > 30 || candidate.gainCeiling > 6 ||
      candidate.radarTriggerMode > 2 || candidate.radarCooldownSeconds < 1 ||
      candidate.radarCooldownSeconds > 3600) {
    error = "Una dintre setarile camerei este in afara intervalului permis.";
    return false;
  }

  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    error = "Memoria NVS nu poate fi deschisa.";
    return false;
  }

  bool success = true;
  success &= preferences.putString("wifi_ssid", candidate.wifiSsid) > 0 ||
             candidate.wifiSsid[0] == '\0';
  success &= preferences.putString("wifi_pass", candidate.wifiPassword) > 0 ||
             candidate.wifiPassword[0] == '\0';
  success &= preferences.putString("ap_pass", candidate.accessPointPassword) > 0;
  success &= preferences.putBool("sd_enabled", candidate.storageEnabled) > 0;
  success &= preferences.putString("photo_dir", candidate.photoDirectory) > 0;
  success &= preferences.putString("video_dir", candidate.videoDirectory) > 0;
  success &= preferences.putUInt("min_free_mb", candidate.minimumFreeMb) > 0;
  success &= preferences.putUChar("cam_profile", candidate.cameraProfile) > 0;
  success &= preferences.putUChar("cam_size", candidate.frameSize) > 0;
  success &= preferences.putUChar("jpg_quality", candidate.jpegQuality) > 0;
  success &= preferences.putUChar("cam_fps", candidate.frameRate) > 0;
  success &= preferences.putChar("brightness", candidate.brightness) > 0;
  success &= preferences.putChar("contrast", candidate.contrast) > 0;
  success &= preferences.putChar("saturation", candidate.saturation) > 0;
  success &= preferences.putUChar("effect", candidate.specialEffect) > 0;
  success &= preferences.putBool("whitebal", candidate.whiteBalance) > 0;
  success &= preferences.putBool("awb_gain", candidate.awbGain) > 0;
  success &= preferences.putUChar("wb_mode", candidate.whiteBalanceMode) > 0;
  success &= preferences.putBool("aec", candidate.autoExposure) > 0;
  success &= preferences.putBool("aec2", candidate.aec2) > 0;
  success &= preferences.putChar("ae_level", candidate.exposureLevel) > 0;
  success &= preferences.putUShort("aec_value", candidate.exposureValue) > 0;
  success &= preferences.putBool("agc", candidate.autoGain) > 0;
  success &= preferences.putUChar("agc_gain", candidate.gainValue) > 0;
  success &= preferences.putUChar("gain_ceil", candidate.gainCeiling) > 0;
  success &= preferences.putBool("bpc", candidate.blackPixelCorrection) > 0;
  success &= preferences.putBool("wpc", candidate.whitePixelCorrection) > 0;
  success &= preferences.putBool("raw_gma", candidate.rawGamma) > 0;
  success &= preferences.putBool("lenc", candidate.lensCorrection) > 0;
  success &= preferences.putBool("dcw", candidate.downsize) > 0;
  success &= preferences.putBool("hmirror", candidate.horizontalMirror) > 0;
  success &= preferences.putBool("vflip", candidate.verticalFlip) > 0;
  success &= preferences.putBool("colorbar", candidate.colorBar) > 0;
  success &= preferences.putBool("flash", candidate.flashEnabled) > 0;
  success &= preferences.putUChar("flash_level", candidate.flashIntensity) > 0;
  success &= preferences.putBool("radar_enabled", candidate.radarEnabled) > 0;
  success &= preferences.putBool("radar_photo", candidate.radarAutoPhoto) > 0;
  success &= preferences.putUChar("radar_trigger", candidate.radarTriggerMode) > 0;
  success &= preferences.putUShort("radar_cool", candidate.radarCooldownSeconds) > 0;
  preferences.end();

  if (!success) {
    error = "Una dintre valorile NVS nu a putut fi salvata.";
    return false;
  }

  if (mutex_ != nullptr && xSemaphoreTake(asSemaphore(mutex_), pdMS_TO_TICKS(1000)) == pdTRUE) {
    config_ = candidate;
    xSemaphoreGive(asSemaphore(mutex_));
  } else {
    error = "Configuratia este ocupata; incearca din nou.";
    return false;
  }
  return true;
}

bool ConfigService::validMediaDirectory(const char* path) {
  if (path == nullptr || path[0] != '/' || path[1] == '\0' || strlen(path) >= 32 ||
      strstr(path, "..") != nullptr || path[strlen(path) - 1] == '/') {
    return false;
  }
  for (const char* cursor = path + 1; *cursor != '\0'; ++cursor) {
    const char c = *cursor;
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '/')) {
      return false;
    }
  }
  return true;
}
