#include "modem_http.h"

#include <SD.h>

#include "modem_error.h"
#include "modem_http_sd_copy.h"
#include "modem_http_session.h"
#include "modem_http_storage.h"
#include "modem_manager.h"
#include "modem_parsers.h"

static const char kModemLocalPrefix[] = "C:/";

ModemHttp::ModemHttp(ModemManager& modem) : modem_(modem) {}

bool ModemHttp::get(const char* url, uint16_t readLen) {
  if (!url || strlen(url) == 0) {
    return modem_.fail("http", ModemErrorCode::InvalidArgument,
                       "http url empty", modem_.state());
  }

  String urlStr(url);
  urlStr.trim();
  bool useTls = urlStr.startsWith("https://");
  uint32_t actionTimeout = useTls ? 90000UL : 60000UL;

  if (!modem_.data().ensureNetOpen()) {
    return modem_.fail("http", ModemErrorCode::NetOpenFailed,
                       "http netopen not ready", modem_.state());
  }

  String host;
  String path;
  ModemParsers::parseHttpUrl(url, host, path);
  if (host.length() == 0) {
    return modem_.fail("http", ModemErrorCode::InvalidArgument,
                       "http host missing", modem_.state());
  }

  int status = 0;
  int length = 0;

  modem_.transitionTo(ModemState::HttpBusy, "http get");
  httpDrainActionUrc(modem_);
  modem_.at().exec(2000L, GF("+HTTPTERM"));

  if (!httpInit(modem_)) {
    return modem_.fail("http", ModemErrorCode::HttpInitFailed,
                       "http init failed", ModemState::Error);
  }

  if (!httpSetUrl(modem_, url)) {
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    return modem_.fail("http", ModemErrorCode::HttpInitFailed,
                       "http para url failed", ModemState::Error);
  }

  if (!httpActionGet(modem_, actionTimeout, status, length, nullptr)) {
    modem_.logWarn("http", "httpaction timeout");
    modem_.logWarn("http", "read skipped, no action result");
    httpTerm(modem_);
    return modem_.fail("http", ModemErrorCode::HttpActionTimeout,
                       "httpaction timeout", ModemState::Error);
  }

  modem_.logInfo("http", String("action status: ") + status);
  modem_.logInfo("http", String("action length: ") + length);

  if (status == 200 && length > 0 && readLen > 0) {
    uint16_t toRead = readLen;
    if (length < static_cast<int>(readLen)) {
      toRead = static_cast<uint16_t>(length);
    }

    modem_.logInfo("http", "HTTPREAD");
    modem_.at().exec(10000L, GF("+HTTPREAD=0,"), toRead);
  } else {
    modem_.logWarn("http", "read skipped, no action result");
  }

  httpTerm(modem_);
  modem_.transitionTo(ModemState::NetOpenReady, "http get done");

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
      modem_.logInfo("http", line);
    }
  };
  auto logValue = [&](const String& label, int value) {
    if (logSink) {
      logSink(false, label + ": " + String(value));
    } else {
      modem_.logInfo("http", label + ": " + String(value));
    }
  };
  if (!url || strlen(url) == 0) {
    lastHttpLength_ = -1;
    return modem_.fail("http", ModemErrorCode::InvalidArgument,
                       "http url empty", modem_.state());
  }
  size_t fsReadChunkSize = httpClampFsReadChunk(static_cast<size_t>(chunkSize));

  if (!modem_.data().ensureNetOpen()) {
    lastHttpLength_ = -1;
    return modem_.fail("http", ModemErrorCode::NetOpenFailed,
                       "http netopen not ready", modem_.state());
  }

  modem_.transitionTo(ModemState::HttpBusy, "http download");
  httpDrainActionUrc(modem_);
  modem_.at().exec(2000L, GF("+HTTPTERM"));

  String urlStr(url);
  urlStr.trim();
  bool useTls = urlStr.startsWith("https://");
  uint32_t actionTimeout = useTls ? 90000UL : 60000UL;
  if (useTls) {
    modem_.logInfo("http", "HTTPS detected");
    if (!httpConfigureSsl(modem_)) {
      modem_.at().exec(2000L, GF("+HTTPTERM"));
      lastHttpLength_ = -1;
      return modem_.fail("http", ModemErrorCode::HttpSslConfigFailed,
                         "http ssl cfg failed", ModemState::Error);
    }
  }

  if (!httpInit(modem_)) {
    lastHttpLength_ = -1;
    return modem_.fail("http", ModemErrorCode::HttpInitFailed,
                       "http init failed", ModemState::Error);
  }

  if (useTls) {
    modem_.logInfo("http", "HTTPPARA SSLCFG");
    if (!modem_.at().exec(5000L, GF("+HTTPPARA=\"SSLCFG\",0")).ok) {
      modem_.at().exec(2000L, GF("+HTTPTERM"));
      lastHttpLength_ = -1;
      return modem_.fail("http", ModemErrorCode::HttpInitFailed,
                         "http para sslcfg failed", ModemState::Error);
    }
  }

  if (!httpSetUrl(modem_, url)) {
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    lastHttpLength_ = -1;
    return modem_.fail("http", ModemErrorCode::HttpInitFailed,
                       "http para url failed", ModemState::Error);
  }

  if (!httpSetTimeouts(modem_, 60, 60)) {
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    lastHttpLength_ = -1;
    return modem_.fail("http", ModemErrorCode::HttpInitFailed,
                       "http para timeouts failed", ModemState::Error);
  }

  int status = 0;
  int length = 0;
  if (!httpActionGet(modem_, actionTimeout, status, length, logSink)) {
    modem_.logWarn("http", "httpaction timeout");
    modem_.logWarn("http", "read skipped, no action result");
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    lastHttpLength_ = -1;
    return modem_.fail("http", ModemErrorCode::HttpActionTimeout,
                       "httpaction timeout", ModemState::Error);
  }

  logValue("[http] action status", status);
  logValue("[http] action length", length);
  lastHttpLength_ = length;

  if (status != 200 || length <= 0) {
    logLine("[http] read loop skipped");
    httpTerm(modem_);
    if (length <= 0) {
      lastHttpLength_ = -1;
    }
    return modem_.fail("http", ModemErrorCode::HttpActionFailed,
                       "http status not ok", ModemState::Error);
  }

  String targetName = httpResolveLocalFilename(sdPath);
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
  bool readFileUrcOk = httpWaitReadFileResult(modem_, logSink, readFileErr,
                                              120000UL);
  httpTerm(modem_);
  if (!readFileCmdOk || !readFileUrcOk || readFileErr != 0) {
    logLine("[http] HTTPREADFILE failed");
    return modem_.fail("http", ModemErrorCode::HttpReadFailed,
                       "http readfile failed", ModemState::Error);
  }

  logLine("[http] HTTPREADFILE success");
  const char* sdTarget =
      (sdPath && strlen(sdPath) > 0) ? sdPath : "/firmware.bin";
  logLine(String("[http] sd target path: ") + sdTarget);
  if (!httpCopyModemFileToSd(modem_, modemPath.c_str(), sdTarget, length,
                             fsReadChunkSize, logSink, sdRecoverFn,
                             static_cast<size_t>(flushThreshold),
                             sdRemountFn)) {
    return modem_.fail("http", ModemErrorCode::HttpFileCopyFailed,
                       "modem file copy failed", ModemState::Error);
  }
  modem_.transitionTo(ModemState::NetOpenReady, "http download done");
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
      modem_.setLastError(ModemErrorCode::HttpReadFailed,
                          "http read header failed");
      downloadOk = false;
      restoreLogging();
      break;
    }

    logLine("[http] chunk header parsed");

    if (dataLen == 0) {
      modem_.logLine("[http] HTTPREAD empty");
      modem_.setLastError(ModemErrorCode::HttpReadFailed, "http read empty");
      downloadOk = false;
      restoreLogging();
      break;
    }

    if (dataLen > remaining) {
      modem_.logLine("[http] HTTPREAD size mismatch");
      modem_.setLastError(ModemErrorCode::HttpReadFailed,
                          "http size mismatch");
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
        modem_.setLastError(ModemErrorCode::SdOpenFailed,
                            "http sd open failed");
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
      modem_.setLastError(ModemErrorCode::HttpReadFailed,
                          "http read data failed");
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
        modem_.setLastError(ModemErrorCode::UnexpectedResponse,
                            "http read ok missing");
        downloadOk = false;
        break;
      }
    }
    if (!endSeen) {
      modem_.logLine("[http] chunk tail missing");
      modem_.setLastError(ModemErrorCode::UnexpectedResponse,
                          "http read ok missing");
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
      modem_.setLastError(ModemErrorCode::SdRenameFailed,
                          "http rename backup failed");
      cleanupTemp();
      setOtaDownloadState(OtaStateIdle, 0, 0);
      return false;
    }
  }

  if (!SD.rename(tempPath, sdPath)) {
    modem_.setLastError(ModemErrorCode::SdRenameFailed,
                        "http rename temp failed");
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

bool ModemHttp::downloadToModemFile(const char* url, const char* modemPath,
                                   ModemLogSink logSink) {
  lastDownloadFailedAfterTemp_ = false;
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem_.logInfo("http", line);
    }
  };
  auto logValue = [&](const String& label, int value) {
    if (logSink) {
      logSink(false, label + ": " + String(value));
    } else {
      modem_.logInfo("http", label + ": " + String(value));
    }
  };
  if (!url || strlen(url) == 0) {
    lastHttpLength_ = -1;
    return modem_.fail("http", ModemErrorCode::InvalidArgument,
                       "http url empty", modem_.state());
  }

  if (!modem_.data().ensureNetOpen()) {
    lastHttpLength_ = -1;
    return modem_.fail("http", ModemErrorCode::NetOpenFailed,
                       "http netopen not ready", modem_.state());
  }

  modem_.transitionTo(ModemState::HttpBusy, "http download");
  httpDrainActionUrc(modem_);
  modem_.at().exec(2000L, GF("+HTTPTERM"));

  String urlStr(url);
  urlStr.trim();
  bool useTls = urlStr.startsWith("https://");
  uint32_t actionTimeout = useTls ? 90000UL : 60000UL;
  if (useTls) {
    modem_.logInfo("http", "HTTPS detected");
    if (!httpConfigureSsl(modem_)) {
      modem_.at().exec(2000L, GF("+HTTPTERM"));
      lastHttpLength_ = -1;
      return modem_.fail("http", ModemErrorCode::HttpSslConfigFailed,
                         "http ssl cfg failed", ModemState::Error);
    }
  }

  if (!httpInit(modem_)) {
    lastHttpLength_ = -1;
    return modem_.fail("http", ModemErrorCode::HttpInitFailed,
                       "http init failed", ModemState::Error);
  }

  if (useTls) {
    modem_.logInfo("http", "HTTPPARA SSLCFG");
    if (!modem_.at().exec(5000L, GF("+HTTPPARA=\"SSLCFG\",0")).ok) {
      modem_.at().exec(2000L, GF("+HTTPTERM"));
      lastHttpLength_ = -1;
      return modem_.fail("http", ModemErrorCode::HttpInitFailed,
                         "http para sslcfg failed", ModemState::Error);
    }
  }

  if (!httpSetUrl(modem_, url)) {
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    lastHttpLength_ = -1;
    return modem_.fail("http", ModemErrorCode::HttpInitFailed,
                       "http para url failed", ModemState::Error);
  }

  if (!httpSetTimeouts(modem_, 60, 60)) {
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    lastHttpLength_ = -1;
    return modem_.fail("http", ModemErrorCode::HttpInitFailed,
                       "http para timeouts failed", ModemState::Error);
  }

  int status = 0;
  int length = 0;
  if (!httpActionGet(modem_, actionTimeout, status, length, logSink)) {
    modem_.logWarn("http", "httpaction timeout");
    modem_.logWarn("http", "read skipped, no action result");
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    lastHttpLength_ = -1;
    return modem_.fail("http", ModemErrorCode::HttpActionTimeout,
                       "httpaction timeout", ModemState::Error);
  }

  logValue("[http] action status", status);
  logValue("[http] action length", length);
  lastHttpLength_ = length;

  if (status != 200 || length <= 0) {
    logLine("[http] read loop skipped");
    httpTerm(modem_);
    if (length <= 0) {
      lastHttpLength_ = -1;
    }
    return modem_.fail("http", ModemErrorCode::HttpActionFailed,
                       "http status not ok", ModemState::Error);
  }

  String targetName = httpResolveLocalFilename(modemPath);
  String modemPathResolved = String(kModemLocalPrefix) + targetName;
  logLine("[http] starting HTTPREADFILE download");
  logValue("[http] expected size", length);
  logLine(String("[http] target filename: ") + targetName);
  logLine("[http] target storage: local");
  logLine("[http] saving response with HTTPREADFILE");

  bool readFileCmdOk = modem_.at().exec(120000L, GF("+HTTPREADFILE=\""),
                                        targetName.c_str(), GF("\",1"))
                           .ok;
  int readFileErr = -1;
  bool readFileUrcOk = httpWaitReadFileResult(modem_, logSink, readFileErr,
                                              120000UL);
  httpTerm(modem_);
  if (!readFileCmdOk || !readFileUrcOk || readFileErr != 0) {
    logLine("[http] HTTPREADFILE failed");
    return modem_.fail("http", ModemErrorCode::HttpReadFailed,
                       "http readfile failed", ModemState::Error);
  }

  logLine("[http] HTTPREADFILE success");
  logLine(String("[http] modem target path: ") + modemPathResolved);
  modem_.transitionTo(ModemState::NetOpenReady, "http download done");
  return true;
}

bool ModemHttp::verifyFileOnSd(const char* path, size_t expectedSize,
                               size_t previewBytes, ModemLogSink logSink) {
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem_.logInfo("http", line);
    }
  };
  auto logValue = [&](const String& label, int value) {
    if (logSink) {
      logSink(false, label + ": " + String(value));
    } else {
      modem_.logInfo("http", label + ": " + String(value));
    }
  };

  if (!path || strlen(path) == 0) {
    logLine("[http] verify path empty");
    modem_.setLastError(ModemErrorCode::InvalidArgument, "verify path empty");
    return false;
  }

  if (!SD.exists(path)) {
    logLine("[http] verify file missing");
    modem_.setLastError(ModemErrorCode::SdVerificationFailed,
                        "verify file missing");
    return false;
  }

  File file = SD.open(path, FILE_READ);
  if (!file) {
    logLine("[http] verify open failed");
    modem_.setLastError(ModemErrorCode::SdOpenFailed, "verify open failed");
    return false;
  }

  size_t size = file.size();
  logValue("[http] verify size expected", static_cast<int>(expectedSize));
  logValue("[http] verify size actual", static_cast<int>(size));
  if (size != expectedSize) {
    file.close();
    logLine("[http] verify size mismatch");
    modem_.setLastError(ModemErrorCode::SdVerificationFailed,
                        "verify size mismatch");
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
