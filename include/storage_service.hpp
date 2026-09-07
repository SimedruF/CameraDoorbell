#pragma once

#include <Arduino.h>

struct StorageStatus {
  bool mounted = false;
  uint64_t totalBytes = 0;
  uint64_t usedBytes = 0;
  char latestPhoto[96] = {};
  char latestVideo[96] = {};
  bool recording = false;
  uint32_t videoFrames = 0;
  uint32_t videoDroppedFrames = 0;
  uint32_t videoBytes = 0;
  uint32_t videoElapsedMs = 0;
};

class StorageService {
 public:
  bool begin();
  bool requestPhotoSave();
  bool requestVideoStart();
  bool requestVideoStop();
  StorageStatus status();
  bool streamLatestPhoto(void* request);
  bool streamLatestVideo(void* request);
  bool listDirectory(const char* path, void* request);
  bool streamFile(const char* path, void* request, bool download);

 private:
  enum class Command : uint8_t { SavePhoto, StartVideo, StopVideo };

  static void taskEntry(void* context);
  void taskLoop();
  bool savePhoto();
  bool startVideo();
  bool appendVideoFrame();
  bool finishVideo(bool keepFile);
  bool buildPhotoPaths(char* directory, size_t directorySize,
                       char* finalPath, size_t finalPathSize,
                       char* temporaryPath, size_t temporaryPathSize);
  bool buildVideoPaths(char* directory, size_t directorySize,
                       char* finalPath, size_t finalPathSize,
                       char* temporaryPath, size_t temporaryPathSize);
  bool ensureDirectory(const char* path);
  static bool safePath(const char* path);

  void* queue_ = nullptr;
  void* mutex_ = nullptr;
  bool mounted_ = false;
  char latestPhoto_[96] = {};
  char latestVideo_[96] = {};
  void* videoState_ = nullptr;
  volatile bool videoActive_ = false;
  volatile uint32_t videoFrames_ = 0;
  volatile uint32_t videoDroppedFrames_ = 0;
  volatile uint32_t videoBytes_ = 0;
  volatile uint32_t videoStartedAtMs_ = 0;
  volatile uint32_t videoElapsedMs_ = 0;
};

extern StorageService Storage;
