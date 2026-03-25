#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>

using OtaLogSink = void (*)(const String& line);

class OtaManager {
 public:
  explicit OtaManager(OtaLogSink logSink = nullptr);

  void begin();
  void handle();
  bool installFromSd(const char* path);

 private:
  void logLine(const String& line);
  void logUpdateError(const char* prefix);

  OtaLogSink logSink_;
};

#endif
