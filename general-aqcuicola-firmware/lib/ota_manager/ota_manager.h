#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>

using OtaLogSink = void (*)(const String& line);

class OtaManager {
 public:
  explicit OtaManager(OtaLogSink logSink = nullptr);

  void begin();
  void handle();
  using OtaStageCallback = void (*)(void* context);
  bool installFromSd(const char* path, OtaStageCallback afterWriteCb = nullptr,
                     void* context = nullptr);

 private:
  void logLine(const String& line);
  void logUpdateError(const char* prefix);

  OtaLogSink logSink_;
};

#endif
