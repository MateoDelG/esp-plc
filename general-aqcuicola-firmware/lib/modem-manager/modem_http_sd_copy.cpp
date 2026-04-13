#include "modem_http_sd_copy.h"

#include <SD.h>

#include "sd_shared.h"
#include "modem_http_storage.h"
#include "modem_manager.h"

static const size_t kFsProgressStep = 4096;
static const size_t kFsReadChunkMin = 64;
static const size_t kFsReadChunkDefault = 256;
static const size_t kFsReadChunkMax = 1024;

struct SdLockGuard {
  SemaphoreHandle_t mutex = nullptr;
  bool locked = false;
  ~SdLockGuard() { release(); }
  bool acquire() {
    if (!mutex) {
      mutex = sdSharedMutex();
    }
    if (mutex && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
      locked = true;
      return true;
    }
    return false;
  }
  void release() {
    if (locked) {
      xSemaphoreGive(mutex);
      locked = false;
    }
  }
};

size_t httpClampFsReadChunk(size_t requested) {
  size_t fsReadChunkSize = requested > 0 ? requested : kFsReadChunkDefault;
  if (fsReadChunkSize < kFsReadChunkMin) {
    fsReadChunkSize = kFsReadChunkMin;
  }
  if (fsReadChunkSize > kFsReadChunkMax) {
    fsReadChunkSize = kFsReadChunkMax;
  }
  return fsReadChunkSize;
}

bool httpCopyModemFileToSd(ModemManager& modem, const char* filename,
                           const char* sdPath, int expectedSize,
                           size_t fsReadChunkSize, ModemLogSink logSink,
                           bool (*sdRecoverFn)(), size_t fsFlushThreshold,
                           bool (*sdRemountFn)()) {
  SdLockGuard lock;
  if (!lock.acquire()) {
    return false;
  }
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem.logInfo("http", line);
    }
  };
  (void)fsFlushThreshold;
  logLine("copying modem file to SD");
  logLine(String("requested chunk: ") + fsReadChunkSize);
  logLine("flush threshold: final flush only");

  String tempPath = String(sdPath) + ".part";
  const char* sdTempPath = tempPath.c_str();
  logLine(String("temp path: ") + sdTempPath);

  auto cleanupStaleTempIfPresent = [&](const char* phase) {
    if (!SD.exists(sdTempPath)) {
      return true;
    }
    logLine(String(phase) + " stale temp cleanup start");
    if (!SD.remove(sdTempPath)) {
      logLine(String(phase) + " stale temp remove failed");
      return false;
    }
    if (SD.exists(sdTempPath)) {
      logLine(String(phase) + " stale temp still present");
      return false;
    }
    logLine(String(phase) + " stale temp removed");
    return true;
  };

  if (SD.exists(sdTempPath)) {
    logLine(String("removing stale temp file: ") + sdTempPath);
    if (!cleanupStaleTempIfPresent("pre-copy")) {
      logLine("copy aborted");
      return false;
    }
  }
  bool sdFailed = false;
  auto cleanupTemp = [&](bool afterRecovery) {
    if (sdFailed && !afterRecovery) {
      logLine("temp cleanup skipped because SD is unavailable");
      return false;
    }
    if (!SD.exists(sdTempPath)) {
      return true;
    }
    if (afterRecovery) {
      return cleanupStaleTempIfPresent("post-recovery");
    }
    logLine(String("temp cleanup: ") + sdTempPath);
    if (SD.remove(sdTempPath)) {
      logLine("temp remove ok");
      return true;
    }
    logLine("temp remove failed");
    return false;
  };

  File out = SD.open(sdTempPath, FILE_WRITE);
  if (!out) {
    logLine("sd open failed");
    return false;
  }
  logLine("temp file open ok");

  int handle = -1;
  if (!fsOpenRead(modem, filename, handle, logSink)) {
    logLine("modem file open failed");
    out.close();
    cleanupTemp(false);
    logLine("copy aborted");
    return false;
  }

  const int openHandle = handle;
  int remaining = expectedSize;
  size_t totalWritten = 0;
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
        logLine("fail: header error");
        restoreLogging();
        logLine(String("close handle: ") + openHandle);
        fsClose(modem, openHandle);
        out.close();
        cleanupTemp(false);
        logLine("copy aborted");
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
      logLine("fail: header timeout");
      restoreLogging();
      logLine(String("close handle: ") + openHandle);
      fsClose(modem, openHandle);
      out.close();
      cleanupTemp(false);
      logLine("copy aborted");
      return false;
    }

    if (static_cast<size_t>(dataLen) > fsReadChunkSize) {
      logLine("fail: data length exceeds chunk");
      restoreLogging();
      logLine(String("close handle: ") + openHandle);
      fsClose(modem, openHandle);
      out.close();
      cleanupTemp(false);
      logLine("copy aborted");
      return false;
    }

    bool readOk = modem.at().readExactToBuffer(buffer,
                                               static_cast<size_t>(dataLen),
                                               60000UL);
    if (!readOk) {
      logLine("fail: binary read");
      restoreLogging();
      logLine(String("close handle: ") + openHandle);
      fsClose(modem, openHandle);
      out.close();
      cleanupTemp(false);
      logLine("copy aborted");
      return false;
    }

    size_t writeCount = out.write(buffer, static_cast<size_t>(dataLen));
    if (writeCount != static_cast<size_t>(dataLen)) {
      logLine("first SD write failure detected");
      logLine("fail: sd write mismatch");
      logLine(String("write requested: ") + dataLen);
      logLine(String("write actual: ") + writeCount);
      logLine(String("totalWritten: ") + totalWritten);
      logLine(String("expected size: ") + expectedSize);
      sdFailed = true;
      logLine("aborting copy path immediately");
      out.close();
      restoreLogging();
      logLine(String("close handle: ") + openHandle);
      fsClose(modem, openHandle);
      logLine("temp cleanup deferred until recovery");
      bool remountOk = false;
      if (sdRecoverFn) {
        logLine("remount after write mismatch");
        // Callbacks may remount SD and must run outside the shared mutex.
        lock.release();
        remountOk = sdRecoverFn();
        if (!lock.acquire()) {
          return false;
        }
        logLine(String("remount ") + (remountOk ? "ok" : "fail"));
      }
      if (remountOk) {
        cleanupTemp(true);
      }
      logLine("copy aborted");
      return false;
    }

    if (!waitFsReadTailOk(modem, logSink, 5000UL)) {
      restoreLogging();
      logLine(String("close handle: ") + openHandle);
      fsClose(modem, openHandle);
      out.close();
      cleanupTemp(false);
      logLine("copy aborted");
      return false;
    }
    remaining -= dataLen;
    totalWritten += static_cast<size_t>(dataLen);
    sinceProgress += static_cast<size_t>(dataLen);
    if (sinceProgress >= kFsProgressStep) {
      logLine(String("copied ") + totalWritten + " / " + expectedSize);
      sinceProgress = 0;
    }
    restoreLogging();
  }

  logLine("final flush start");
  logLine(String("final flush totalWritten=") + totalWritten);
  out.flush();
  logLine(String("final flush done, position=") + out.position());
  out.close();
  logLine("sd file close ok");
  logLine("waiting before temp reopen");
  modem.idle(200);
  logLine(String("close handle: ") + openHandle);
  bool closeOk = fsClose(modem, openHandle);
  if (!closeOk) {
    logLine("fail: close handle");
    cleanupTemp(false);
    logLine("copy aborted");
    return false;
  }
  logLine(String("total copied: ") + totalWritten);
  logLine(String("expected size: ") + expectedSize);
  logLine(String("verifying temp path: ") + sdTempPath);
  File check = SD.open(sdTempPath, FILE_READ);
  if (!check) {
    logLine("temp reopen failed, retrying");
    modem.idle(100);
    check = SD.open(sdTempPath, FILE_READ);
  }
  if (!check) {
    if (sdRemountFn) {
      logLine("remount before temp verify");
      logLine("remount begin");
      // Callbacks may remount SD and must run outside the shared mutex.
      lock.release();
      bool remountOk = sdRemountFn();
      if (!lock.acquire()) {
        return false;
      }
      logLine(String("remount ") + (remountOk ? "ok" : "failed"));
      if (remountOk) {
        check = SD.open(sdTempPath, FILE_READ);
        if (check) {
          logLine("temp reopen after remount ok");
        } else {
          logLine("temp reopen after remount failed");
        }
      }
    }
  }
  size_t sdSize = 0;
  if (check) {
    sdSize = check.size();
    logLine(String("temp size: ") + sdSize);
    logLine(String("expected size: ") + expectedSize);
    check.close();
  } else {
    logLine("temp verify failed after remount");
    logLine("skipping temp verification because SD is unavailable");
    sdFailed = true;
    cleanupTemp(false);
    return false;
  }
  if (totalWritten != static_cast<size_t>(expectedSize) ||
      sdSize != static_cast<size_t>(expectedSize)) {
    logLine("fail: temp size mismatch");
    logLine(String("totalWritten: ") + totalWritten);
    logLine(String("expected size: ") + expectedSize);
    logLine(String("temp size: ") + sdSize);
    cleanupTemp(false);
    logLine("copy aborted");
    return false;
  }

  if (SD.exists(sdPath)) {
    SD.remove(sdPath);
  }
  if (!SD.rename(sdTempPath, sdPath)) {
    logLine("fail: rename temp to final");
    cleanupTemp(false);
    logLine("copy aborted");
    lock.release();
    return false;
  }
  logLine("modem file copy success");
  lock.release();
  return true;
}
