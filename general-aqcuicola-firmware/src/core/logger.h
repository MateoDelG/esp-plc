#pragma once

#include <Arduino.h>

class Logger {
 public:
  using LogSink = void (*)(const char* line);

  void begin(uint32_t baud);
  void setSink(LogSink sink);
  void info(const char* message);
  void warn(const char* message);
  void error(const char* message);
  void logf(const char* level, const char* format, ...);

 private:
  void logWithLevel(const char* level, const char* message);

  LogSink sink_ = nullptr;
};
