#include "services/sd_logger/sd_logger_service.h"

#include <SD.h>
#include <SPI.h>

#include "core/logger.h"
#include "sd_shared.h"
#include "services/time/time_service.h"

namespace {
#ifndef SD_MISO
#define SD_MISO 2
#endif
#ifndef SD_MOSI
#define SD_MOSI 15
#endif
#ifndef SD_SCLK
#define SD_SCLK 14
#endif
#ifndef SD_CS
#define SD_CS 13
#endif

constexpr uint8_t kSdCsPin = SD_CS;
constexpr uint32_t kSdSpiHz = 100000;
constexpr char kLogsDir[] = "/logs";
constexpr char kUartLogPath[] = "/logs/uart_ph_o2_temp.jsonl";
constexpr char kLevelLogPath[] = "/logs/level_temp.jsonl";

SPIClass& sdSpi() {
  static SPIClass spiSd(HSPI);
  return spiSd;
}
}

SdLoggerService::SdLoggerService(Logger& logger) : logger_(logger) {}

void SdLoggerService::begin() {
  SemaphoreHandle_t mutex = sdSharedMutex();
  if (!mutex) {
    logger_.warn("sd: mutex create failed");
    ready_ = false;
    readyLogged_ = true;
    return;
  }
  if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
    logger_.warn("sd: mutex take failed");
    ready_ = false;
    readyLogged_ = true;
    return;
  }
  ready_ = initSd();
  if (!ready_) {
    logger_.warn("sd: init failed");
    readyLogged_ = true;
    xSemaphoreGive(mutex);
    return;
  }
  ensureLogsDir();
  xSemaphoreGive(mutex);
  logger_.info("sd: ready");
}

void SdLoggerService::setTimeService(TimeService* service) {
  timeService_ = service;
}

bool SdLoggerService::isReady() const {
  return ready_;
}

bool SdLoggerService::ensureReady() {
  if (ready_) {
    return true;
  }
  if (!readyLogged_) {
    logger_.warn("sd: not ready");
    readyLogged_ = true;
  }
  return false;
}

bool SdLoggerService::ensureLogsDir() {
  if (SD.exists(kLogsDir)) {
    return true;
  }
  return SD.mkdir(kLogsDir);
}

bool SdLoggerService::initSd() {
  SPIClass& spi = sdSpi();
  spi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  spi.setFrequency(kSdSpiHz);
  return SD.begin(kSdCsPin, spi, kSdSpiHz);
}

bool SdLoggerService::appendLine(const char* path, const String& line,
                                 bool waitForMutex) {
  SemaphoreHandle_t mutex = sdSharedMutex();
  if (!mutex) {
    return false;
  }
  if (waitForMutex) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
      return false;
    }
  } else {
    if (xSemaphoreTake(mutex, 0) != pdTRUE) {
      return false;
    }
  }

  if (!ensureLogsDir()) {
    xSemaphoreGive(mutex);
    return false;
  }
  File file = SD.open(path, FILE_APPEND);
  if (!file) {
    // Avoid SD.end() or re-init here; other modules may be using SD.
    logger_.warn("sd: open failed");
    xSemaphoreGive(mutex);
    return false;
  }
  size_t written = file.println(line);
  file.close();
  xSemaphoreGive(mutex);
  if (written == 0) {
    logger_.warn("sd: write failed");
    return false;
  }
  return true;
}

String SdLoggerService::formatTimestamp() const {
  if (timeService_ && timeService_->isSynced()) {
    return timeService_->localTimeString();
  }
  return String("--");
}

void SdLoggerService::logUartSample(uint8_t tank, float ph, float o2, float tempC) {
  if (!ensureReady()) {
    return;
  }
  String line;
  line.reserve(160);
  line += "{\"ts\":\"";
  line += formatTimestamp();
  line += "\",\"tank\":";
  line += String(tank);
  line += ",\"ph\":";
  line += String(ph, 3);
  line += ",\"o2\":";
  line += String(o2, 3);
  line += ",\"temp\":";
  line += String(tempC, 2);
  line += "}";
  if (appendLine(kUartLogPath, line, true)) {
    logger_.logf("sd", "datalogger uart tank=%u ph=%.3f o2=%.3f temp=%.2f",
                 static_cast<unsigned>(tank), ph, o2, tempC);
  }
}

void SdLoggerService::logLevelTemp(uint8_t tank, float level, float tempC) {
  if (!ensureReady()) {
    return;
  }
  String line;
  line.reserve(140);
  line += "{\"ts\":\"";
  line += formatTimestamp();
  line += "\",\"tank\":";
  line += String(tank);
  line += ",\"level\":";
  line += String(level, 2);
  line += ",\"temp\":";
  line += String(tempC, 2);
  line += "}";
  if (appendLine(kLevelLogPath, line, false)) {
    logger_.logf("sd", "datalogger level tank=%u level=%.2f temp=%.2f",
                 static_cast<unsigned>(tank), level, tempC);
  }
}
