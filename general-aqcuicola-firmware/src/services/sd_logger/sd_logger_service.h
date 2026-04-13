#pragma once

#include <Arduino.h>

class Logger;
class TimeService;

class SdLoggerService {
 public:
  explicit SdLoggerService(Logger& logger);

  void begin();
  void setTimeService(TimeService* service);
  bool isReady() const;

  void logUartSample(uint8_t tank, float ph, float o2, float tempC);
  void logLevelTemp(uint8_t tank, float level, float tempC);

 private:
  bool ensureReady();
  bool ensureLogsDir();
  bool initSd();
  bool appendLine(const char* path, const String& line, bool waitForMutex);
  String formatTimestamp() const;

  Logger& logger_;
  TimeService* timeService_ = nullptr;
  bool ready_ = false;
  bool readyLogged_ = false;
};
