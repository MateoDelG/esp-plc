#include "modem_log.h"

const char* modemLogLevelName(ModemLogLevel level) {
  switch (level) {
    case ModemLogLevel::Debug:
      return "DEBUG";
    case ModemLogLevel::Info:
      return "INFO";
    case ModemLogLevel::Warn:
      return "WARN";
    case ModemLogLevel::Error:
      return "ERROR";
  }
  return "INFO";
}

String modemFormatLog(ModemLogLevel level, const char* subsystem,
                      const String& message) {
  const char* name = modemLogLevelName(level);
  String line;
  line.reserve(message.length() + 16);
  line += "[";
  line += name;
  line += "]";
  if (subsystem && strlen(subsystem) > 0) {
    line += "[";
    line += subsystem;
    line += "] ";
  } else {
    line += " ";
  }
  line += message;
  return line;
}
