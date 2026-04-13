#include "ota_manager.h"

#include <ArduinoOTA.h>
#include <SD.h>
#include <Update.h>

#include "sd_shared.h"

static const size_t kOtaProgressBytesStep = 32768;
static const uint8_t kOtaProgressPercentStep = 5;
static const uint8_t kEspImageMagic = 0xE9;

struct SdLockGuard {
  SemaphoreHandle_t mutex = nullptr;
  bool locked = false;
  SdLockGuard() {
    mutex = sdSharedMutex();
    if (mutex && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
      locked = true;
    }
  }
  ~SdLockGuard() {
    if (locked) {
      xSemaphoreGive(mutex);
    }
  }
  bool ok() const { return locked; }
};

static bool validateHeader(File& file) {
  uint8_t header[4] = {0};
  size_t readBytes = file.read(header, sizeof(header));
  if (readBytes != sizeof(header)) {
    return false;
  }
  if (header[0] != kEspImageMagic) {
    return false;
  }
  if (header[1] == 0) {
    return false;
  }
  return true;
}

OtaManager::OtaManager(OtaLogSink logSink) : logSink_(logSink) {}

void OtaManager::begin() {
  ArduinoOTA.onStart([this]() { logLine("OTA start"); });
  ArduinoOTA.onEnd([this]() { logLine("OTA end"); });
  ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
    uint8_t percent = total > 0
                           ? static_cast<uint8_t>((progress * 100U) / total)
                           : 0;
    logLine(String("OTA progress: ") + percent + "%");
  });
  ArduinoOTA.onError([this](ota_error_t error) {
    logLine(String("OTA error: ") + error);
  });
  ArduinoOTA.begin();
}

void OtaManager::handle() { ArduinoOTA.handle(); }

bool OtaManager::installFromSd(const char* path, OtaStageCallback afterWriteCb,
                               void* context) {
  SdLockGuard lock;
  if (!lock.ok()) {
    logLine("[ota] sd mutex unavailable");
    return false;
  }
  logLine("[ota] install start");
  logLine("[ota] ota partition required");

  if (!SD.exists(path)) {
    logLine("[ota] file missing");
    logLine("[ota] install failed");
    return false;
  }
  logLine("[ota] file exists");

  File file = SD.open(path, FILE_READ);
  if (!file) {
    logLine("[ota] file open failed");
    logLine("[ota] install failed");
    return false;
  }

  size_t size = file.size();
  logLine(String("[ota] file size: ") + size);
  if (size == 0) {
    logLine("[ota] file size invalid");
    file.close();
    logLine("[ota] install failed");
    return false;
  }

  if (!validateHeader(file)) {
    logLine("[ota] header invalid");
    file.close();
    logLine("[ota] install failed");
    return false;
  }
  logLine("[ota] header ok");

  if (!file.seek(0)) {
    logLine("[ota] file seek failed");
    file.close();
    logLine("[ota] install failed");
    return false;
  }

  uint32_t freeSpace = ESP.getFreeSketchSpace();
  logLine(String("[ota] free sketch space: ") + freeSpace);
  if (size > freeSpace) {
    logLine("[ota] insufficient space for update");
  }
  if (!Update.begin(size)) {
    logUpdateError("[ota] update begin failed");
    logLine("[ota] check ota partition scheme");
    file.close();
    logLine("[ota] install failed");
    return false;
  }
  logLine("[ota] update begin ok");

  size_t writtenTotal = 0;
  size_t lastLoggedBytes = 0;
  uint8_t lastLoggedPercent = 0;
  static uint8_t buffer[4096];

  while (file.available()) {
    size_t readLen = file.read(buffer, sizeof(buffer));
    if (readLen == 0) {
      break;
    }

    size_t written = Update.write(buffer, readLen);
    if (written != readLen) {
      logUpdateError("[ota] update write failed");
      Update.abort();
      file.close();
      logLine("[ota] install failed");
      return false;
    }

    writtenTotal += written;
    uint8_t percent = size > 0
                           ? static_cast<uint8_t>((writtenTotal * 100U) / size)
                           : 0;
    bool shouldLog = false;
    if (writtenTotal - lastLoggedBytes >= kOtaProgressBytesStep) {
      shouldLog = true;
    } else if (percent >= lastLoggedPercent + kOtaProgressPercentStep) {
      shouldLog = true;
    }
    if (shouldLog) {
      lastLoggedBytes = writtenTotal;
      lastLoggedPercent = percent;
      logLine(String("[ota] progress: ") + writtenTotal + " / " + size +
              " (" + percent + "%)");
    }
  }

  logLine(String("[ota] bytes written: ") + writtenTotal);
  if (writtenTotal != size) {
    logLine("[ota] update size mismatch");
    Update.abort();
    file.close();
    logLine("[ota] install failed");
    return false;
  }

  if (afterWriteCb) {
    afterWriteCb(context);
  }

  if (!Update.end(true)) {
    logUpdateError("[ota] update end failed");
    file.close();
    logLine("[ota] install failed");
    return false;
  }

  file.close();
  logLine("[ota] update end ok");
  logLine("[ota] rebooting");
  delay(1000);
  ESP.restart();
  return true;
}

void OtaManager::logLine(const String& line) {
  if (logSink_) {
    logSink_(line);
  } else {
    Serial.println(line);
  }
}

void OtaManager::logUpdateError(const char* prefix) {
  String message(prefix);
  message += ": ";
  message += Update.errorString();
  message += " (";
  message += Update.getError();
  message += ")";
  logLine(message);
}
