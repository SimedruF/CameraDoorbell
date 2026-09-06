#pragma once

#include <Arduino.h>

struct StorageStatus {
  bool mounted = false;
  uint64_t totalBytes = 0;
  uint64_t usedBytes = 0;
  char latestPhoto[96] = {};
};

class StorageService {
 public:
  bool begin();
  bool requestPhotoSave();
  StorageStatus status();
  bool streamLatestPhoto(void* request);
  bool listDirectory(const char* path, void* request);
  bool streamFile(const char* path, void* request, bool download);

 private:
  enum class Command : uint8_t { SavePhoto };

  static void taskEntry(void* context);
  void taskLoop();
  bool savePhoto();
  bool buildPhotoPaths(char* directory, size_t directorySize,
                       char* finalPath, size_t finalPathSize,
                       char* temporaryPath, size_t temporaryPathSize);
  bool ensureDirectory(const char* path);
  static bool safePath(const char* path);

  void* queue_ = nullptr;
  void* mutex_ = nullptr;
  bool mounted_ = false;
  char latestPhoto_[96] = {};
};

extern StorageService Storage;
