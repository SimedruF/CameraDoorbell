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
