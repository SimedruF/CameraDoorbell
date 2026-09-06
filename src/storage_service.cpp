#include "storage_service.hpp"

#include <FS.h>
#include <SD_MMC.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <time.h>

#include "app_config.hpp"
#include "camera_service.hpp"
#include "config_service.hpp"

StorageService Storage;

namespace {

QueueHandle_t asQueue(void* value) {
  return static_cast<QueueHandle_t>(value);
}

SemaphoreHandle_t asSemaphore(void* value) {
  return static_cast<SemaphoreHandle_t>(value);
}

bool sendFile(httpd_req_t* request, File& file) {
  uint8_t buffer[4096];
  while (file.available()) {
    const size_t count = file.read(buffer, sizeof(buffer));
    if (count == 0 ||
        httpd_resp_send_chunk(request, reinterpret_cast<const char*>(buffer), count) != ESP_OK) {
      return false;
    }
  }
  return httpd_resp_send_chunk(request, nullptr, 0) == ESP_OK;
}

void sendServiceUnavailable(httpd_req_t* request, const char* message) {
  httpd_resp_set_status(request, "503 Service Unavailable");
  httpd_resp_set_type(request, "text/plain");
  httpd_resp_sendstr(request, message);
}

const char* contentTypeFor(const char* path) {
  const char* extension = strrchr(path, '.');
  if (extension == nullptr) return "application/octet-stream";
  if (strcasecmp(extension, ".jpg") == 0 || strcasecmp(extension, ".jpeg") == 0) {
    return "image/jpeg";
  }
  if (strcasecmp(extension, ".avi") == 0) return "video/x-msvideo";
  if (strcasecmp(extension, ".mjpeg") == 0 || strcasecmp(extension, ".mjpg") == 0) {
    return "multipart/x-mixed-replace";
  }
  if (strcasecmp(extension, ".json") == 0) return "application/json";
  if (strcasecmp(extension, ".txt") == 0 || strcasecmp(extension, ".log") == 0) {
    return "text/plain; charset=utf-8";
  }
  return "application/octet-stream";
}

bool sendJsonEscaped(httpd_req_t* request, const char* value) {
  if (httpd_resp_send_chunk(request, "\"", 1) != ESP_OK) return false;
  const char* segment = value;
  for (const char* cursor = value;; ++cursor) {
    const char c = *cursor;
    if (c != '\\' && c != '\"' && c != '\n' && c != '\r' && c != '\t' && c != '\0') {
      continue;
    }
    if (cursor > segment &&
        httpd_resp_send_chunk(request, segment, static_cast<ssize_t>(cursor - segment)) != ESP_OK) {
      return false;
    }
    if (c == '\0') break;
    const char* escaped = c == '\\' ? "\\\\" : c == '\"' ? "\\\"" :
                          c == '\n' ? "\\n" : c == '\r' ? "\\r" : "\\t";
    if (httpd_resp_send_chunk(request, escaped, 2) != ESP_OK) return false;
    segment = cursor + 1;
  }
  return httpd_resp_send_chunk(request, "\"", 1) == ESP_OK;
}

bool timestampFromFilename(const char* path, char* output, size_t outputSize) {
  const char* name = strrchr(path, '/');
  name = name == nullptr ? path : name + 1;
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (sscanf(name, "%4d%2d%2dT%2d%2d%2dZ", &year, &month, &day, &hour, &minute,
             &second) != 6 ||
      year < 2020 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour > 23 || minute > 59 || second > 60) {
    output[0] = '\0';
    return false;
  }
  snprintf(output, outputSize, "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, day,
           hour, minute, second);
  return true;
}

const char* savedTimestamp(File& file, char* output, size_t outputSize) {
  // FAT commonly reports 1980 when the ESP32 clock was not synchronized yet.
  const time_t modified = file.getLastWrite();
  constexpr time_t kPlausibleTimestamp = 1577836800;  // 2020-01-01 UTC.
  if (modified >= kPlausibleTimestamp) {
    tm utc = {};
    gmtime_r(&modified, &utc);
    strftime(output, outputSize, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return "fat";
  }
  if (timestampFromFilename(file.path(), output, outputSize)) {
    return "filename";
  }
  output[0] = '\0';
  return "none";
}

}  // namespace

bool StorageService::begin() {
  mutex_ = xSemaphoreCreateMutex();
  queue_ = xQueueCreate(AppConfig::kStorageQueueLength, sizeof(Command));
  if (mutex_ == nullptr || queue_ == nullptr) {
    Serial.println("[storage] Cannot create synchronization objects");
    return false;
  }

  const RuntimeConfig settings = Settings.snapshot();
  if (!settings.storageEnabled) {
    Serial.println("[storage] Disabled in configuration");
    return false;
  }

  // true selects SDMMC 1-bit mode: GPIO2 DATA0, GPIO14 CLK, GPIO15 CMD.
  if (!SD_MMC.begin("/sdcard", true, false)) {
    Serial.println("[storage] microSD mount failed");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("[storage] No microSD card detected");
    SD_MMC.end();
    return false;
  }

  mounted_ = ensureDirectory(settings.photoDirectory) && ensureDirectory(settings.videoDirectory);
  if (!mounted_) {
    Serial.println("[storage] Cannot create configured media directories");
    return false;
  }

  const BaseType_t created =
      xTaskCreatePinnedToCore(taskEntry, "storage", 4096, this, 2, nullptr, 0);
  if (created != pdPASS) {
    mounted_ = false;
    Serial.println("[storage] Cannot create storage task");
    return false;
  }

  Serial.printf("[storage] microSD ready: %llu MB total\n",
                static_cast<unsigned long long>(SD_MMC.totalBytes() / (1024ULL * 1024ULL)));
  return true;
}

bool StorageService::requestPhotoSave() {
  if (!mounted_ || queue_ == nullptr) {
    return false;
  }
  const Command command = Command::SavePhoto;
  return xQueueSend(asQueue(queue_), &command, 0) == pdTRUE;
}

void StorageService::taskEntry(void* context) {
  static_cast<StorageService*>(context)->taskLoop();
}

void StorageService::taskLoop() {
  Command command;
  for (;;) {
    if (xQueueReceive(asQueue(queue_), &command, portMAX_DELAY) == pdTRUE &&
        command == Command::SavePhoto) {
      savePhoto();
    }
  }
}

bool StorageService::savePhoto() {
  const RuntimeConfig settings = Settings.snapshot();
  if (!settings.storageEnabled) {
    return false;
  }

  uint64_t freeBytes = 0;
  if (xSemaphoreTake(asSemaphore(mutex_), pdMS_TO_TICKS(1000)) != pdTRUE) {
    Serial.println("[storage] Photo skipped: microSD busy");
    return false;
  }
  freeBytes = SD_MMC.totalBytes() - SD_MMC.usedBytes();
  xSemaphoreGive(asSemaphore(mutex_));
  if (freeBytes < static_cast<uint64_t>(settings.minimumFreeMb) * 1024ULL * 1024ULL) {
    Serial.printf("[storage] Photo skipped: less than %lu MB free\n",
                  static_cast<unsigned long>(settings.minimumFreeMb));
    return false;
  }

  FrameCopy frame;
  if (!Camera.copyLatest(frame, pdMS_TO_TICKS(500))) {
    Serial.println("[storage] No camera frame available for photo");
    return false;
  }
  const size_t photoLength = frame.length;

  char directory[48];
  char finalPath[96];
  char temporaryPath[96];
  if (!buildPhotoPaths(directory, sizeof(directory), finalPath, sizeof(finalPath),
                       temporaryPath, sizeof(temporaryPath))) {
    Camera.release(frame);
    return false;
  }

  bool success = false;
  if (xSemaphoreTake(asSemaphore(mutex_), pdMS_TO_TICKS(2000)) == pdTRUE) {
    if (ensureDirectory(directory)) {
      SD_MMC.remove(temporaryPath);
      File file = SD_MMC.open(temporaryPath, FILE_WRITE);
      if (file) {
        const size_t written = file.write(frame.data, frame.length);
        file.flush();
        file.close();

        if (written == frame.length) {
          success = SD_MMC.rename(temporaryPath, finalPath);
        }
      }

      if (success) {
        strlcpy(latestPhoto_, finalPath, sizeof(latestPhoto_));
      } else {
        SD_MMC.remove(temporaryPath);
      }
    }
    xSemaphoreGive(asSemaphore(mutex_));
  }

  Camera.release(frame);
  if (success) {
    Serial.printf("[storage] Photo saved: %s (%u bytes)\n", finalPath,
                  static_cast<unsigned>(photoLength));
  } else {
    Serial.printf("[storage] Failed to save photo: %s\n", finalPath);
  }
  return success;
}

bool StorageService::buildPhotoPaths(char* directory, size_t directorySize,
                                     char* finalPath, size_t finalPathSize,
                                     char* temporaryPath, size_t temporaryPathSize) {
  const RuntimeConfig settings = Settings.snapshot();
  const time_t now = time(nullptr);

  if (now >= static_cast<time_t>(AppConfig::kValidEpochThreshold)) {
    tm utc = {};
    gmtime_r(&now, &utc);
    snprintf(directory, directorySize, "%s/%04d/%02d/%02d", settings.photoDirectory,
             utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
    snprintf(finalPath, finalPathSize, "%s/%04d%02d%02dT%02d%02d%02dZ_%08lx.jpg",
             directory, utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
             utc.tm_min, utc.tm_sec, static_cast<unsigned long>(esp_random()));
  } else {
    snprintf(directory, directorySize, "%s/unsynced", settings.photoDirectory);
    snprintf(finalPath, finalPathSize, "%s/boot_%010lu_%08lx.jpg", directory,
             static_cast<unsigned long>(millis()),
             static_cast<unsigned long>(esp_random()));
  }

  snprintf(temporaryPath, temporaryPathSize, "%s.tmp", finalPath);
  return true;
}

bool StorageService::ensureDirectory(const char* path) {
  if (SD_MMC.exists(path)) {
    return true;
  }

  char partial[64];
  if (strlcpy(partial, path, sizeof(partial)) >= sizeof(partial)) {
    return false;
  }
  for (char* cursor = partial + 1; *cursor != '\0'; ++cursor) {
    if (*cursor != '/') {
      continue;
    }
    *cursor = '\0';
    const bool componentReady = SD_MMC.exists(partial) || SD_MMC.mkdir(partial);
    *cursor = '/';
    if (!componentReady) {
      return false;
    }
  }
  return SD_MMC.exists(partial) || SD_MMC.mkdir(partial);
}

StorageStatus StorageService::status() {
  StorageStatus result;
  result.mounted = mounted_;
  if (!mounted_ || mutex_ == nullptr) {
    return result;
  }

  if (xSemaphoreTake(asSemaphore(mutex_), pdMS_TO_TICKS(250)) == pdTRUE) {
    result.totalBytes = SD_MMC.totalBytes();
    result.usedBytes = SD_MMC.usedBytes();
    strlcpy(result.latestPhoto, latestPhoto_, sizeof(result.latestPhoto));
    xSemaphoreGive(asSemaphore(mutex_));
  }
  return result;
}

bool StorageService::streamLatestPhoto(void* opaqueRequest) {
  auto* request = static_cast<httpd_req_t*>(opaqueRequest);
  if (!mounted_ || mutex_ == nullptr) {
    sendServiceUnavailable(request, "microSD unavailable");
    return false;
  }

  if (xSemaphoreTake(asSemaphore(mutex_), pdMS_TO_TICKS(1000)) != pdTRUE) {
    sendServiceUnavailable(request, "microSD busy");
    return false;
  }

  if (latestPhoto_[0] == '\0') {
    xSemaphoreGive(asSemaphore(mutex_));
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "No saved photo yet");
    return false;
  }

  File file = SD_MMC.open(latestPhoto_, FILE_READ);
  if (!file) {
    xSemaphoreGive(asSemaphore(mutex_));
    httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot read saved photo");
    return false;
  }

  httpd_resp_set_type(request, "image/jpeg");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  const bool success = sendFile(request, file);
  file.close();
  xSemaphoreGive(asSemaphore(mutex_));
  return success;
}

bool StorageService::listDirectory(const char* path, void* opaqueRequest) {
  auto* request = static_cast<httpd_req_t*>(opaqueRequest);
  if (!mounted_ || mutex_ == nullptr) {
    sendServiceUnavailable(request, "microSD unavailable");
    return false;
  }
  if (!safePath(path)) {
    httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid path");
    return false;
  }
  if (xSemaphoreTake(asSemaphore(mutex_), pdMS_TO_TICKS(1000)) != pdTRUE) {
    sendServiceUnavailable(request, "microSD busy");
    return false;
  }

  File directory = SD_MMC.open(path, FILE_READ);
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    xSemaphoreGive(asSemaphore(mutex_));
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Directory not found");
    return false;
  }

  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  bool success = httpd_resp_send_chunk(request, "{\"path\":", HTTPD_RESP_USE_STRLEN) == ESP_OK;
  if (success) success = sendJsonEscaped(request, path);
  if (success) {
    success = httpd_resp_send_chunk(request, ",\"entries\":[", HTTPD_RESP_USE_STRLEN) == ESP_OK;
  }

  bool first = true;
  size_t count = 0;
  File entry;
  while (success && count < 250 && (entry = directory.openNextFile())) {
    const char* entryPath = entry.path();
    char savedAt[32];
    const char* dateSource = savedTimestamp(entry, savedAt, sizeof(savedAt));
    char prefix[176];
    const int prefixLength = snprintf(prefix, sizeof(prefix),
                                      "%s{\"directory\":%s,\"size\":%llu,"
                                      "\"saved_at\":\"%s\",\"date_source\":\"%s\",\"path\":",
                                      first ? "" : ",", entry.isDirectory() ? "true" : "false",
                                      static_cast<unsigned long long>(entry.size()), savedAt,
                                      dateSource);
    success = httpd_resp_send_chunk(request, prefix, prefixLength) == ESP_OK;
    if (success) success = sendJsonEscaped(request, entryPath);
    if (success) success = httpd_resp_send_chunk(request, "}", 1) == ESP_OK;
    first = false;
    ++count;
    entry.close();
  }
  directory.close();

  if (success) {
    char suffix[48];
    snprintf(suffix, sizeof(suffix), "],\"truncated\":%s}", count >= 250 ? "true" : "false");
    success = httpd_resp_send_chunk(request, suffix, HTTPD_RESP_USE_STRLEN) == ESP_OK;
  }
  if (success) success = httpd_resp_send_chunk(request, nullptr, 0) == ESP_OK;
  xSemaphoreGive(asSemaphore(mutex_));
  return success;
}

bool StorageService::streamFile(const char* path, void* opaqueRequest, bool download) {
  auto* request = static_cast<httpd_req_t*>(opaqueRequest);
  if (!mounted_ || mutex_ == nullptr) {
    sendServiceUnavailable(request, "microSD unavailable");
    return false;
  }
  if (!safePath(path)) {
    httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid path");
    return false;
  }
  if (xSemaphoreTake(asSemaphore(mutex_), pdMS_TO_TICKS(1000)) != pdTRUE) {
    sendServiceUnavailable(request, "microSD busy");
    return false;
  }

  File file = SD_MMC.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    xSemaphoreGive(asSemaphore(mutex_));
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "File not found");
    return false;
  }

  httpd_resp_set_type(request, contentTypeFor(path));
  httpd_resp_set_hdr(request, "Cache-Control", "private, max-age=60");
  if (download) {
    const char* name = strrchr(path, '/');
    name = name == nullptr ? path : name + 1;
    char disposition[128];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", name);
    httpd_resp_set_hdr(request, "Content-Disposition", disposition);
  }
  const bool success = sendFile(request, file);
  file.close();
  xSemaphoreGive(asSemaphore(mutex_));
  return success;
}

bool StorageService::safePath(const char* path) {
  if (path == nullptr || path[0] != '/' || strstr(path, "..") != nullptr ||
      strchr(path, '\\') != nullptr || strchr(path, '\r') != nullptr ||
      strchr(path, '\n') != nullptr) {
    return false;
  }
  return strlen(path) < 160;
}
