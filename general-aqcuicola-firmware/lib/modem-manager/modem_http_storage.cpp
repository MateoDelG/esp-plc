#include "modem_http_storage.h"

#include "modem_manager.h"

static const char kOtaLocalFilename[] = "firmware.bin";

String httpResolveLocalFilename(const char* path) {
  if (!path || strlen(path) == 0) {
    return String(kOtaLocalFilename);
  }
  String name(path);
  int slash = name.lastIndexOf('/');
  if (slash >= 0 && slash + 1 < static_cast<int>(name.length())) {
    name = name.substring(slash + 1);
  }
  name.trim();
  if (name.length() == 0) {
    return String(kOtaLocalFilename);
  }
  return name;
}

static bool parseFsOpenLine(const String& line, int& handleOut) {
  if (!line.startsWith("+FSOPEN:")) {
    return false;
  }
  int idx = line.indexOf(':');
  if (idx < 0) {
    return false;
  }
  String value = line.substring(idx + 1);
  value.trim();
  handleOut = value.toInt();
  return true;
}

bool parseFsReadHeader(const String& line, int& lengthOut) {
  if (!line.startsWith("+FSREAD:")) {
    return false;
  }
  int idx = line.indexOf(':');
  if (idx < 0) {
    return false;
  }
  String value = line.substring(idx + 1);
  value.trim();
  lengthOut = value.toInt();
  return true;
}

bool parseFsConnectLine(const String& line, int& lengthOut) {
  if (!line.startsWith("CONNECT")) {
    return false;
  }
  int space = line.indexOf(' ');
  if (space < 0) {
    return false;
  }
  String value = line.substring(space + 1);
  value.trim();
  lengthOut = value.toInt();
  return true;
}

bool fsOpenRead(ModemManager& modem, const char* filename, int& handle,
                ModemLogSink logSink) {
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem.logInfo("fs", line);
    }
  };
  logLine(String("opening modem file: ") + filename);
  modem.tinyGsm().sendAT(GF("+FSOPEN=\""), filename, GF("\",2"));
  uint32_t start = millis();
  bool gotHandle = false;
  while (millis() - start < 5000UL) {
    String line;
    if (!modem.at().readLine(line, 500)) {
      continue;
    }
    if (line.length() == 0) {
      continue;
    }
    if (line == "ERROR") {
      return false;
    }
    if (parseFsOpenLine(line, handle)) {
      gotHandle = true;
    }
    if (line == "OK") {
      if (gotHandle) {
        logLine(String("modem file open ok, handle=") + handle);
      }
      return gotHandle;
    }
  }
  return false;
}

bool fsClose(ModemManager& modem, int handle) {
  return modem.at().exec(2000L, GF("+FSCLOSE="), handle).ok;
}

bool waitFsReadTailOk(ModemManager& modem, ModemLogSink logSink,
                      uint32_t timeoutMs) {
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem.logInfo("fs", line);
    }
  };
  String line;
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (!modem.at().readLine(line, 500)) {
      continue;
    }
    if (line.length() == 0) {
      continue;
    }
    if (line == "OK") {
      return true;
    }
    if (line == "ERROR") {
      logLine("fsread tail error");
      return false;
    }
  }
  logLine("fsread tail timeout");
  return false;
}
