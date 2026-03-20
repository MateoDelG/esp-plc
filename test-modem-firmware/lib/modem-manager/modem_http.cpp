#include "modem_http.h"

#include <SD.h>

#include "modem_manager.h"
#include "modem_parsers.h"

static void drainHttpActionUrc(ModemManager& modem) {
  String line;
  uint32_t start = millis();
  while (millis() - start < 200UL) {
    if (!modem.at().waitUrc(UrcType::HttpAction, 20, line)) {
      break;
    }
    modem.logLine("[http] stale httpaction discarded");
    modem.logValue("[http] raw", line);
  }
}

ModemHttp::ModemHttp(ModemManager& modem) : modem_(modem) {}

static const char kOtaLocalFilename[] = "firmware.bin";
static const char kModemLocalPrefix[] = "C:/";
static const size_t kFsProgressStep = 4096;
static const size_t kFsReadChunkMin = 64;
static const size_t kFsReadChunkDefault = 256;
static const size_t kFsReadChunkMax = 1024;

static String resolveLocalFilename(const char* path) {
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

static bool parseFsReadHeader(const String& line, int& lengthOut) {
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

static bool parseFsConnectLine(const String& line, int& lengthOut) {
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

static bool parseHttpReadFileLine(const String& line, int& errOut) {
  if (!line.startsWith("+HTTPREADFILE:")) {
    return false;
  }
  int idx = line.indexOf(':');
  if (idx < 0) {
    return false;
  }
  String value = line.substring(idx + 1);
  value.trim();
  errOut = value.toInt();
  return true;
}

static bool fsOpenRead(ModemManager& modem, const char* filename, int& handle,
                       ModemLogSink logSink) {
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem.logLine(line);
    }
  };
  logLine(String("[fs] opening modem file: ") + filename);
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
        logLine(String("[fs] modem file open ok, handle=") + handle);
      }
      return gotHandle;
    }
  }
  return false;
}

static bool fsClose(ModemManager& modem, int handle) {
  return modem.at().exec(2000L, GF("+FSCLOSE="), handle).ok;
}

static bool waitFsReadTailOk(ModemManager& modem, ModemLogSink logSink,
                             uint32_t timeoutMs) {
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem.logLine(line);
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
      logLine("[fs] fsread tail error");
      return false;
    }
  }
  logLine("[fs] fsread tail timeout");
  return false;
}

static bool waitHttpReadFileResult(ModemManager& modem, ModemLogSink logSink,
                                   int& errOut, uint32_t timeoutMs) {
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem.logLine(line);
    }
  };
  logLine("[http] waiting for HTTPREADFILE result");
  String line;
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (!modem.at().readLine(line, 500)) {
      continue;
    }
    if (line.length() == 0) {
      continue;
    }
    if (parseHttpReadFileLine(line, errOut)) {
      logLine(String("[http] HTTPREADFILE result: ") + errOut);
      return true;
    }
  }
  logLine("[http] HTTPREADFILE result timeout");
  return false;
}

static bool copyModemFileToSd(ModemManager& modem, const char* filename,
                              const char* sdPath, int expectedSize,
                              size_t fsReadChunkSize, ModemLogSink logSink,
                              bool (*sdRecoverFn)(), size_t fsFlushThreshold,
                              bool (*sdRemountFn)()) {
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem.logLine(line);
    }
  };
  logLine("[http] copying modem file to SD");
  logLine(String("[fs] requested chunk: ") + fsReadChunkSize);
  size_t flushThreshold = fsFlushThreshold;
  if (flushThreshold == 0) {
    logLine("[fs] flush threshold: 0 (final flush only)");
  } else {
    logLine(String("[fs] flush threshold: ") + flushThreshold);
  }

  String tempPath = String(sdPath) + ".part";
  const char* sdTempPath = tempPath.c_str();
  logLine(String("[fs] temp path: ") + sdTempPath);

  if (SD.exists(sdTempPath)) {
    logLine(String("[fs] removing stale temp file: ") + sdTempPath);
    if (!SD.remove(sdTempPath)) {
      logLine("[fs] stale temp remove failed");
      logLine("[fs] copy aborted");
      return false;
    }
    logLine("[fs] stale temp remove ok");
  }
  bool sdFailed = false;
  auto cleanupTemp = [&](bool afterRecovery) {
    if (sdFailed && !afterRecovery) {
      logLine("[fs] temp cleanup skipped because SD is unavailable");
      return false;
    }
    if (!SD.exists(sdTempPath)) {
      return true;
    }
    if (afterRecovery) {
      logLine("[fs] retrying stale temp cleanup after SD recovery");
    } else {
      logLine(String("[fs] temp cleanup: ") + sdTempPath);
    }
    if (SD.remove(sdTempPath)) {
      logLine(afterRecovery ? "[fs] post-recovery temp remove ok"
                            : "[fs] temp remove ok");
      return true;
    }
    logLine(afterRecovery ? "[fs] post-recovery temp remove failed"
                          : "[fs] temp remove failed");
    return false;
  };

  File out = SD.open(sdTempPath, FILE_WRITE);
  if (!out) {
    logLine("[http] sd open failed");
    return false;
  }
  logLine("[fs] temp file open ok");

  int handle = -1;
  if (!fsOpenRead(modem, filename, handle, logSink)) {
    logLine("[http] modem file open failed");
    out.close();
    cleanupTemp(false);
    logLine("[fs] copy aborted");
    return false;
  }

  const int openHandle = handle;
  int remaining = expectedSize;
  size_t totalWritten = 0;
  size_t sinceFlush = 0;
  size_t sinceProgress = 0;
  uint8_t buffer[kFsReadChunkMax];

  while (remaining > 0) {
    int toRead = remaining > static_cast<int>(fsReadChunkSize)
                     ? static_cast<int>(fsReadChunkSize)
                     : remaining;
    modem.setTxLoggingEnabled(false);
    modem.setRxLoggingEnabled(false);
    modem.tinyGsm().sendAT(GF("+FSREAD="), openHandle, GF(","), toRead);
    auto restoreLogging = [&]() {
      modem.setTxLoggingEnabled(true);
      modem.setRxLoggingEnabled(true);
    };

    String header;
    int dataLen = -1;
    uint32_t headerStart = millis();
    while (millis() - headerStart < 5000UL) {
      if (!modem.at().readLine(header, 500)) {
        continue;
      }
      if (header.length() == 0) {
        continue;
      }
      if (header == "OK") {
        continue;
      }
      if (header == "ERROR") {
        logLine("[fs] fail: header error");
        restoreLogging();
        logLine(String("[fs] close handle: ") + openHandle);
        fsClose(modem, openHandle);
        out.close();
        cleanupTemp(false);
        logLine("[fs] copy aborted");
        return false;
      }
      if (parseFsConnectLine(header, dataLen)) {
        break;
      }
      if (parseFsReadHeader(header, dataLen)) {
        break;
      }
    }

    if (dataLen <= 0) {
      logLine("[fs] fail: header timeout");
      restoreLogging();
      logLine(String("[fs] close handle: ") + openHandle);
      fsClose(modem, openHandle);
      out.close();
      cleanupTemp(false);
      logLine("[fs] copy aborted");
      return false;
    }

    if (static_cast<size_t>(dataLen) > fsReadChunkSize) {
      logLine("[fs] fail: data length exceeds chunk");
      restoreLogging();
      logLine(String("[fs] close handle: ") + openHandle);
      fsClose(modem, openHandle);
      out.close();
      cleanupTemp(false);
      logLine("[fs] copy aborted");
      return false;
    }

    bool readOk = modem.at().readExactToBuffer(buffer,
                                               static_cast<size_t>(dataLen),
                                               60000UL);
    if (!readOk) {
      logLine("[fs] fail: binary read");
      restoreLogging();
      logLine(String("[fs] close handle: ") + openHandle);
      fsClose(modem, openHandle);
      out.close();
      cleanupTemp(false);
      logLine("[fs] copy aborted");
      return false;
    }

    size_t writeCount = out.write(buffer, static_cast<size_t>(dataLen));
    if (writeCount != static_cast<size_t>(dataLen)) {
      logLine("[fs] fail: sd write mismatch");
      logLine(String("[fs] write requested: ") + dataLen);
      logLine(String("[fs] write actual: ") + writeCount);
      logLine(String("[fs] totalWritten: ") + totalWritten);
      logLine(String("[fs] sinceFlush: ") + sinceFlush);
      logLine(String("[fs] expected size: ") + expectedSize);
      sdFailed = true;
      out.close();
      restoreLogging();
      logLine(String("[fs] close handle: ") + openHandle);
      fsClose(modem, openHandle);
      logLine("[fs] temp cleanup deferred until recovery");
      bool remountOk = false;
      if (sdRecoverFn) {
        logLine("[sd] remount after write mismatch");
        remountOk = sdRecoverFn();
        logLine(String("[sd] remount ") + (remountOk ? "ok" : "fail"));
      }
      if (remountOk) {
        cleanupTemp(true);
      }
      logLine("[fs] copy aborted");
      return false;
    }

    if (!waitFsReadTailOk(modem, logSink, 5000UL)) {
      restoreLogging();
      logLine(String("[fs] close handle: ") + openHandle);
      fsClose(modem, openHandle);
      out.close();
      cleanupTemp(false);
      logLine("[fs] copy aborted");
      return false;
    }
    remaining -= dataLen;
    sinceFlush += static_cast<size_t>(dataLen);
    totalWritten += static_cast<size_t>(dataLen);
    sinceProgress += static_cast<size_t>(dataLen);
    if (sinceProgress >= kFsProgressStep) {
      logLine(String("[fs] copied ") + totalWritten + " / " + expectedSize);
      sinceProgress = 0;
    }
    if (flushThreshold > 0 && sinceFlush >= flushThreshold) {
      logLine(String("[fs] flush start, bytesSinceFlush=") + sinceFlush +
              ", totalWritten=" + totalWritten);
      out.flush();
      size_t posAfter = out.position();
      logLine(String("[fs] flush done, position=") + posAfter);
      if (posAfter != totalWritten) {
        logLine("[fs] fail: flush position mismatch");
        sdFailed = true;
        out.close();
        restoreLogging();
        logLine(String("[fs] close handle: ") + openHandle);
        fsClose(modem, openHandle);
        logLine("[fs] temp cleanup deferred until recovery");
        bool remountOk = false;
        if (sdRecoverFn) {
          logLine("[sd] remount after flush mismatch");
          remountOk = sdRecoverFn();
          logLine(String("[sd] remount ") + (remountOk ? "ok" : "fail"));
        }
        if (remountOk) {
          cleanupTemp(true);
        }
        logLine("[fs] copy aborted");
        return false;
      }
      sinceFlush = 0;
    }
    restoreLogging();
  }

  logLine("[fs] final flush start");
  logLine(String("[fs] final flush totalWritten=") + totalWritten);
  out.flush();
  logLine(String("[fs] final flush done, position=") + out.position());
  out.close();
  logLine("[fs] sd file close ok");
  logLine("[fs] waiting before temp reopen");
  delay(200);
  logLine(String("[fs] close handle: ") + openHandle);
  bool closeOk = fsClose(modem, openHandle);
  if (!closeOk) {
    logLine("[fs] fail: close handle");
    cleanupTemp(false);
    logLine("[fs] copy aborted");
    return false;
  }
  logLine(String("[fs] total copied: ") + totalWritten);
  logLine(String("[fs] expected size: ") + expectedSize);
  logLine(String("[fs] verifying temp path: ") + sdTempPath);
  File check = SD.open(sdTempPath, FILE_READ);
  if (!check) {
    logLine("[fs] temp reopen failed, retrying");
    delay(100);
    check = SD.open(sdTempPath, FILE_READ);
  }
  if (!check) {
    if (sdRemountFn) {
      logLine("[sd] remount before temp verify");
      logLine("[sd] remount begin");
      bool remountOk = sdRemountFn();
      logLine(String("[sd] remount ") + (remountOk ? "ok" : "failed"));
      if (remountOk) {
        check = SD.open(sdTempPath, FILE_READ);
        if (check) {
          logLine("[fs] temp reopen after remount ok");
        } else {
          logLine("[fs] temp reopen after remount failed");
        }
      }
    }
  }
  size_t sdSize = 0;
  if (check) {
    sdSize = check.size();
    logLine(String("[fs] temp size: ") + sdSize);
    logLine(String("[fs] expected size: ") + expectedSize);
    check.close();
  } else {
    logLine("[fs] temp verify failed after remount");
    logLine("[fs] skipping temp verification because SD is unavailable");
    sdFailed = true;
    cleanupTemp(false);
    return false;
  }
  if (totalWritten != static_cast<size_t>(expectedSize) ||
      sdSize != static_cast<size_t>(expectedSize)) {
    logLine("[fs] fail: temp size mismatch");
    logLine(String("[fs] totalWritten: ") + totalWritten);
    logLine(String("[fs] expected size: ") + expectedSize);
    logLine(String("[fs] temp size: ") + sdSize);
    cleanupTemp(false);
    logLine("[fs] copy aborted");
    return false;
  }

  if (SD.exists(sdPath)) {
    SD.remove(sdPath);
  }
  if (!SD.rename(sdTempPath, sdPath)) {
    logLine("[fs] fail: rename temp to final");
    cleanupTemp(false);
    logLine("[fs] copy aborted");
    return false;
  }
  logLine("[http] modem file copy success");
  return true;
}

bool ModemHttp::get(const char* url, uint16_t readLen) {
  if (!url || strlen(url) == 0) {
    modem_.setLastError(-30, "http url empty");
    return false;
  }

  String urlStr(url);
  urlStr.trim();
  bool useTls = urlStr.startsWith("https://");
  uint32_t actionTimeout = useTls ? 90000UL : 60000UL;

  if (!modem_.data().ensureNetOpen()) {
    modem_.logLine("[http] netopen not ready");
    modem_.setLastError(-31, "http netopen not ready");
    return false;
  }

  String host;
  String path;
  ModemParsers::parseHttpUrl(url, host, path);
  if (host.length() == 0) {
    return false;
  }

  int status = 0;
  int length = 0;

  drainHttpActionUrc(modem_);
  modem_.at().exec(2000L, GF("+HTTPTERM"));

  modem_.logLine("[http] HTTPINIT");
  if (!modem_.at().exec(5000L, GF("+HTTPINIT")).ok) {
    modem_.setLastError(-32, "http init failed");
    return false;
  }

  modem_.logLine("[http] HTTPPARA URL");
  if (!modem_.at().exec(5000L, GF("+HTTPPARA=\"URL\",\""), url,
                        GF("\""))
           .ok) {
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    modem_.setLastError(-33, "http para url failed");
    return false;
  }

  modem_.logLine("[http] HTTPACTION");
  String urcLine;
  bool ok = false;
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    if (!modem_.at().exec(10000L, GF("+HTTPACTION=0")).ok) {
      continue;
    }
    modem_.logLine("[http] waiting action urc");
    if (!modem_.at().waitUrc(UrcType::HttpAction, actionTimeout, urcLine)) {
      continue;
    }
    if (ModemParsers::parseHttpAction(urcLine, status, length)) {
      ok = true;
      break;
    }
    modem_.logLine("[http] parse HTTPACTION failed");
    modem_.logValue("[http] raw", urcLine);
  }

  if (!ok) {
    modem_.logLine("[http] httpaction timeout");
    modem_.logLine("[http] read skipped, no action result");
    modem_.setLastError(-35, "http action urc timeout");
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    return false;
  }

  modem_.logValue("[http] action status", status);
  modem_.logValue("[http] action length", length);

  if (status == 200 && length > 0 && readLen > 0) {
    uint16_t toRead = readLen;
    if (length < static_cast<int>(readLen)) {
      toRead = static_cast<uint16_t>(length);
    }

    modem_.logLine("[http] HTTPREAD");
    modem_.at().exec(10000L, GF("+HTTPREAD=0,"), toRead);
  } else {
    modem_.logLine("[http] read skipped, no action result");
  }

  modem_.logLine("[http] HTTPTERM");
  modem_.at().exec(5000L, GF("+HTTPTERM"));

  return (status == 200 || status == 204 || status == 301 || status == 302);
}

bool ModemHttp::downloadToFile(const char* url, const char* sdPath,
                               uint16_t chunkSize, ModemLogSink logSink,
                               bool (*sdRecoverFn)(), uint16_t flushThreshold,
                               bool (*sdRemountFn)()) {
  lastDownloadFailedAfterTemp_ = false;
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem_.logLine(line);
    }
  };
  auto logValue = [&](const String& label, int value) {
    if (logSink) {
      logSink(false, label + ": " + String(value));
    } else {
      modem_.logValue(label, value);
    }
  };
  if (!url || strlen(url) == 0) {
    modem_.setLastError(-60, "http url empty");
    lastHttpLength_ = -1;
    return false;
  }
  size_t fsReadChunkSize = chunkSize > 0 ? static_cast<size_t>(chunkSize)
                                         : kFsReadChunkDefault;
  if (fsReadChunkSize < kFsReadChunkMin) {
    fsReadChunkSize = kFsReadChunkMin;
  }
  if (fsReadChunkSize > kFsReadChunkMax) {
    fsReadChunkSize = kFsReadChunkMax;
  }

  if (!modem_.data().ensureNetOpen()) {
    modem_.logLine("[http] netopen not ready");
    modem_.setLastError(-62, "http netopen not ready");
    lastHttpLength_ = -1;
    return false;
  }

  drainHttpActionUrc(modem_);
  modem_.at().exec(2000L, GF("+HTTPTERM"));

  String urlStr(url);
  urlStr.trim();
  bool useTls = urlStr.startsWith("https://");
  uint32_t actionTimeout = useTls ? 90000UL : 60000UL;
  if (useTls) {
    modem_.logLine("[http] HTTPS detected");
    if (!modem_.at().exec(5000L, GF("+CSSLCFG=\"sslversion\",0,3")).ok) {
      modem_.setLastError(-63, "http ssl cfg failed");
      modem_.at().exec(2000L, GF("+HTTPTERM"));
      lastHttpLength_ = -1;
      return false;
    }
    if (!modem_.at().exec(5000L, GF("+CSSLCFG=\"authmode\",0,0")).ok) {
      modem_.setLastError(-64, "http ssl cfg failed");
      modem_.at().exec(2000L, GF("+HTTPTERM"));
      lastHttpLength_ = -1;
      return false;
    }
    if (!modem_.at().exec(5000L, GF("+CSSLCFG=\"ignorelocaltime\",0,1")).ok) {
      modem_.setLastError(-65, "http ssl cfg failed");
      modem_.at().exec(2000L, GF("+HTTPTERM"));
      lastHttpLength_ = -1;
      return false;
    }
    if (!modem_.at().exec(5000L, GF("+CSSLCFG=\"enableSNI\",0,1")).ok) {
      modem_.setLastError(-66, "http ssl cfg failed");
      modem_.at().exec(2000L, GF("+HTTPTERM"));
      lastHttpLength_ = -1;
      return false;
    }
    modem_.at().exec(5000L, GF("+CSSLCFG=\"negotiatetime\",0,120"));
    modem_.logLine("[http] SSL profile configured");
  }

  modem_.logLine("[http] HTTPINIT");
  if (!modem_.at().exec(5000L, GF("+HTTPINIT")).ok) {
    modem_.setLastError(-67, "http init failed");
    lastHttpLength_ = -1;
    return false;
  }

  if (useTls) {
    modem_.logLine("[http] HTTPPARA SSLCFG");
    if (!modem_.at().exec(5000L, GF("+HTTPPARA=\"SSLCFG\",0")).ok) {
      modem_.setLastError(-68, "http para sslcfg failed");
      modem_.at().exec(2000L, GF("+HTTPTERM"));
      lastHttpLength_ = -1;
      return false;
    }
  }

  modem_.logLine("[http] HTTPPARA URL");
  if (!modem_.at().exec(5000L, GF("+HTTPPARA=\"URL\",\""), url,
                        GF("\""))
           .ok) {
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    modem_.setLastError(-69, "http para url failed");
    lastHttpLength_ = -1;
    return false;
  }

  modem_.logLine("[http] HTTPPARA CONNECTTO");
  if (!modem_.at().exec(5000L, GF("+HTTPPARA=\"CONNECTTO\",60")).ok) {
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    modem_.setLastError(-70, "http para connectto failed");
    lastHttpLength_ = -1;
    return false;
  }

  modem_.logLine("[http] HTTPPARA RECVTO");
  if (!modem_.at().exec(5000L, GF("+HTTPPARA=\"RECVTO\",60")).ok) {
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    modem_.setLastError(-71, "http para recvto failed");
    lastHttpLength_ = -1;
    return false;
  }

  modem_.logLine("[http] HTTPACTION");
  String urcLine;
  int status = 0;
  int length = 0;
  bool ok = false;
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    if (!modem_.at().exec(10000L, GF("+HTTPACTION=0")).ok) {
      continue;
    }
    modem_.logLine("[http] waiting action urc");
    if (!modem_.at().waitUrc(UrcType::HttpAction, actionTimeout, urcLine)) {
      continue;
    }
    if (ModemParsers::parseHttpAction(urcLine, status, length)) {
      ok = true;
      break;
    }
    modem_.logLine("[http] parse HTTPACTION failed");
    modem_.logValue("[http] raw", urcLine);
  }

  if (!ok) {
    modem_.logLine("[http] httpaction timeout");
    modem_.logLine("[http] read skipped, no action result");
    modem_.setLastError(-72, "http action urc timeout");
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    lastHttpLength_ = -1;
    return false;
  }

  logValue("[http] action status", status);
  logValue("[http] action length", length);
  lastHttpLength_ = length;

  if (status != 200 || length <= 0) {
    logLine("[http] read loop skipped");
    modem_.setLastError(-73, "http status not ok");
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    if (length <= 0) {
      lastHttpLength_ = -1;
    }
    return false;
  }

  String targetName = resolveLocalFilename(sdPath);
  String modemPath = String(kModemLocalPrefix) + targetName;
  logLine("[http] starting HTTPREADFILE download");
  logValue("[http] expected size", length);
  logLine(String("[http] target filename: ") + targetName);
  logLine("[http] target storage: local");
  logLine("[http] saving response with HTTPREADFILE");

  bool readFileCmdOk = modem_.at().exec(120000L, GF("+HTTPREADFILE=\""),
                                        targetName.c_str(), GF("\",1"))
                           .ok;
  int readFileErr = -1;
  bool readFileUrcOk = waitHttpReadFileResult(modem_, logSink, readFileErr,
                                              120000UL);
  modem_.logLine("[http] HTTPTERM");
  modem_.at().exec(2000L, GF("+HTTPTERM"));
  if (!readFileCmdOk || !readFileUrcOk || readFileErr != 0) {
    modem_.setLastError(-78, "http readfile failed");
    logLine("[http] HTTPREADFILE failed");
    return false;
  }

  logLine("[http] HTTPREADFILE success");
  const char* sdTarget =
      (sdPath && strlen(sdPath) > 0) ? sdPath : "/firmware.bin";
  logLine(String("[http] sd target path: ") + sdTarget);
  if (!copyModemFileToSd(modem_, modemPath.c_str(), sdTarget, length,
                         fsReadChunkSize, logSink, sdRecoverFn,
                         static_cast<size_t>(flushThreshold), sdRemountFn)) {
    return false;
  }
  return true;

#if 0

  const char* tempPath = kOtaTempPath;
  File file;
  bool tempOpened = false;
  size_t sinceFlush = 0;
  bool remountOk = true;

  auto cleanupTemp = [&]() {
    if (SD.remove(tempPath)) {
      logLine("[http] removed partial temp file");
    }
  };

  auto closeTemp = [&](bool doFlush) {
    if (!tempOpened) {
      return;
    }
    if (doFlush) {
      file.flush();
    }
    logLine("[http] closing temp file");
    file.close();
  };

  int offset = 0;
  int remaining = length;
  size_t totalWritten = 0;
  bool downloadOk = true;
  uint32_t lastLogMs = millis();

  if (remaining <= 0) {
    downloadOk = false;
  } else {
    setOtaDownloadState(OtaStateDownloading, static_cast<uint32_t>(length), 0);
  }

  logLine("[http] waiting first chunk before creating temp file");

  while (remaining > 0) {
    uint16_t toRead = chunkSize;
    if (remaining < static_cast<int>(toRead)) {
      toRead = static_cast<uint16_t>(remaining);
    }

    modem_.setTxLoggingEnabled(false);
    modem_.setRxLoggingEnabled(false);
    modem_.tinyGsm().sendAT(GF("+HTTPREAD="), offset, GF(","), toRead);
    auto restoreLogging = [&]() {
      modem_.setTxLoggingEnabled(true);
      modem_.setRxLoggingEnabled(true);
    };

    String header;
    int dataLen = -1;
    uint32_t headerStart = millis();
    while (millis() - headerStart < 10000UL) {
      if (!modem_.at().readLine(header, 500)) {
        continue;
      }
      if (header.length() == 0) {
        continue;
      }
      if (header == "OK") {
        continue;
      }
      if (ModemParsers::parseHttpReadHeader(header, dataLen)) {
        break;
      }
    }

    if (dataLen < 0) {
      modem_.logLine("[http] HTTPREAD header failed");
      modem_.logValue("[http] raw", header);
      modem_.setLastError(-75, "http read header failed");
      downloadOk = false;
      restoreLogging();
      break;
    }

    logLine("[http] chunk header parsed");

    if (dataLen == 0) {
      modem_.logLine("[http] HTTPREAD empty");
      modem_.setLastError(-76, "http read empty");
      downloadOk = false;
      restoreLogging();
      break;
    }

    if (dataLen > remaining) {
      modem_.logLine("[http] HTTPREAD size mismatch");
      modem_.setLastError(-77, "http size mismatch");
      downloadOk = false;
      restoreLogging();
      break;
    }

    if (!tempOpened) {
      logLine(String("[http] first chunk received: ") + dataLen);
      logLine("[http] temp path: /fw.part");
      logLine("[http] opening temp file after first chunk");
      SD.remove(tempPath);
      file = SD.open(tempPath, FILE_WRITE);
      if (!file) {
        logLine("[http] temp file open failed");
        modem_.setLastError(-74, "http sd open failed");
        downloadOk = false;
        restoreLogging();
        break;
      }
      tempOpened = true;
      logLine("[http] temp file open ok");
    }

    bool readOk = modem_.at().readExactToFile(static_cast<size_t>(dataLen),
                                              file, 20000UL);
    if (!readOk) {
      modem_.logLine("[http] HTTPREAD data failed");
      modem_.setLastError(-78, "http read data failed");
      downloadOk = false;
      restoreLogging();
      break;
    }
    logLine("[http] chunk data read ok");
    totalWritten += static_cast<size_t>(dataLen);
    sinceFlush += static_cast<size_t>(dataLen);
    if (sinceFlush >= kOtaFlushThreshold) {
      file.flush();
      logLine("[http] periodic flush");
      sinceFlush = 0;
      setOtaDownloadState(OtaStateDownloading, static_cast<uint32_t>(length),
                          static_cast<uint32_t>(totalWritten));
    }
    if (millis() - lastLogMs >= 5000UL) {
      int percent = length > 0
                        ? static_cast<int>((totalWritten * 100U) /
                                           static_cast<size_t>(length))
                        : 0;
      logLine(String("[http] progress: ") + totalWritten + "/" + length +
              " (" + percent + "%)");
      lastLogMs = millis();
    }

    bool endSeen = false;
    uint32_t tailStart = millis();
    while (millis() - tailStart < 5000UL) {
      String tail;
      if (!modem_.at().readLine(tail, 300)) {
        continue;
      }
      if (tail.length() == 0) {
        continue;
      }
      if (tail == "OK") {
        logLine("[http] chunk tail OK received");
        endSeen = true;
        break;
      }
      int tailLen = -1;
      if (ModemParsers::parseHttpReadHeader(tail, tailLen)) {
        if (tailLen == 0) {
          logLine("[http] chunk tail zero-length header received");
          endSeen = true;
          break;
        }
        modem_.logLine("[http] HTTPREAD unexpected header");
        modem_.logValue("[http] raw", tail);
        modem_.setLastError(-79, "http read ok missing");
        downloadOk = false;
        break;
      }
    }
    if (!endSeen) {
      modem_.logLine("[http] chunk tail missing");
      modem_.setLastError(-79, "http read ok missing");
      downloadOk = false;
    }
    restoreLogging();
    if (!downloadOk) {
      break;
    }

    offset += dataLen;
    remaining -= dataLen;
  }

  if (!downloadOk) {
    closeTemp(true);
    modem_.logLine("[http] HTTPTERM");
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    if (tempOpened) {
      cleanupTemp();
      lastDownloadFailedAfterTemp_ = true;
    }
    logLine("[sd] remount after failure");
    if (sdRecoverFn) {
      remountOk = sdRecoverFn();
    }
    if (!remountOk) {
      setOtaRecoveryPending(true);
      logLine("[ota] download failed, recovery pending");
    } else {
      setOtaDownloadState(OtaStateIdle, 0, 0);
    }
    return false;
  }

  closeTemp(true);
  modem_.logLine("[http] HTTPTERM");
  modem_.at().exec(2000L, GF("+HTTPTERM"));

  setOtaDownloadState(OtaStateValidating, static_cast<uint32_t>(length),
                      static_cast<uint32_t>(totalWritten));

  if (!verifyFileOnSd(tempPath, static_cast<size_t>(length), 128, logSink)) {
    if (tempOpened) {
      cleanupTemp();
      lastDownloadFailedAfterTemp_ = true;
    }
    setOtaDownloadState(OtaStateIdle, 0, 0);
    return false;
  }

  logLine("[ota] promoting temp to final");
  logLine("[ota] backup path: /firmware.bak");
  if (SD.exists(kOtaBackupPath)) {
    SD.remove(kOtaBackupPath);
  }
  if (SD.exists(sdPath)) {
    if (!SD.rename(sdPath, kOtaBackupPath)) {
      modem_.setLastError(-83, "http rename backup failed");
      cleanupTemp();
      setOtaDownloadState(OtaStateIdle, 0, 0);
      return false;
    }
  }

  if (!SD.rename(tempPath, sdPath)) {
    modem_.setLastError(-83, "http rename temp failed");
    logLine("[ota] rollback from backup");
    if (SD.exists(kOtaBackupPath)) {
      SD.rename(kOtaBackupPath, sdPath);
    }
    cleanupTemp();
    setOtaDownloadState(OtaStateIdle, 0, 0);
    return false;
  }

  setOtaDownloadState(OtaStateReady, static_cast<uint32_t>(length),
                      static_cast<uint32_t>(totalWritten));
  logLine("[http] final file ready");

  return true;
#endif
}

bool ModemHttp::verifyFileOnSd(const char* path, size_t expectedSize,
                               size_t previewBytes, ModemLogSink logSink) {
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem_.logLine(line);
    }
  };
  auto logValue = [&](const String& label, int value) {
    if (logSink) {
      logSink(false, label + ": " + String(value));
    } else {
      modem_.logValue(label, value);
    }
  };

  if (!path || strlen(path) == 0) {
    logLine("[http] verify path empty");
    return false;
  }

  if (!SD.exists(path)) {
    logLine("[http] verify file missing");
    return false;
  }

  File file = SD.open(path, FILE_READ);
  if (!file) {
    logLine("[http] verify open failed");
    return false;
  }

  size_t size = file.size();
  logValue("[http] verify size expected", static_cast<int>(expectedSize));
  logValue("[http] verify size actual", static_cast<int>(size));
  if (size != expectedSize) {
    file.close();
    logLine("[http] verify size mismatch");
    return false;
  }

  size_t toRead = previewBytes;
  if (toRead > size) {
    toRead = size;
  }

  logValue("[http] verify preview bytes", static_cast<int>(toRead));
  const size_t chunk = 64;
  char buffer[chunk];
  size_t remaining = toRead;
  while (remaining > 0) {
    size_t readNow = remaining > chunk ? chunk : remaining;
    size_t readCount = file.readBytes(buffer, readNow);
    if (readCount == 0) {
      break;
    }
    String line;
    line.reserve(readCount + 16);
    for (size_t i = 0; i < readCount; ++i) {
      char c = buffer[i];
      if (c == '\r' || c == '\n' || (c >= 32 && c <= 126)) {
        line += c;
      }
    }
    if (line.length() == 0) {
      line = ".";
    }
    logLine(String("[http] preview: ") + line);
    remaining -= readCount;
  }

  file.close();
  logLine("[http] verify ok");
  return true;
}
