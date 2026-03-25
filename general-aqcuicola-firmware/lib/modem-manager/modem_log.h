// Structured logging helpers
#ifndef MODEM_LOG_H
#define MODEM_LOG_H

#include <Arduino.h>

enum class ModemLogLevel : uint8_t {
  Debug = 0,
  Info,
  Warn,
  Error,
};

const char* modemLogLevelName(ModemLogLevel level);
String modemFormatLog(ModemLogLevel level, const char* subsystem,
                      const String& message);

#endif
