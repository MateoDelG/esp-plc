#include "core/logger.h"

#include <cstdarg>
#include <cstdio>

void Logger::begin(uint32_t baud) {
  Serial.begin(baud);
  delay(50);
}

void Logger::setSink(LogSink sink) {
  sink_ = sink;
}

void Logger::info(const char* message) {
  logWithLevel("info", message);
}

void Logger::warn(const char* message) {
  logWithLevel("warn", message);
}

void Logger::error(const char* message) {
  logWithLevel("error", message);
}

void Logger::logf(const char* level, const char* format, ...) {
  char buffer[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  logWithLevel(level, buffer);
}

void Logger::logWithLevel(const char* level, const char* message) {
  String line;
  line.reserve(strlen(level) + strlen(message) + 4);
  line += "[";
  line += level;
  line += "] ";
  line += message;
  Serial.println(line);
  if (sink_) {
    sink_(line.c_str());
  }
}
