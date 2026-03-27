#include "services/ota_modem/ota_modem_service.h"

#include <SD.h>
#include <SPI.h>

OtaModemService* OtaModemService::active_ = nullptr;

OtaModemService::OtaModemService(Logger& logger) : logger_(logger) {
  otaManager_.begin();
}

void OtaModemService::setModem(ModemManager* modem) {
  modem_ = modem;
}

void OtaModemService::setUbidots(UbidotsService* ubidots) {
  ubidots_ = ubidots;
}

bool OtaModemService::isBusy() const {
  return busy_;
}

bool OtaModemService::start() {
  if (busy_ || task_ != nullptr) {
    return false;
  }
  if (!modem_) {
    logger_.error("ota-modem: modem not set");
    return false;
  }

  active_ = this;
  busy_ = true;
  xTaskCreatePinnedToCore(taskEntry, "otaModemTask", 12288, this, 2, &task_, 1);
  return true;
}

void OtaModemService::taskEntry(void* param) {
  auto* self = static_cast<OtaModemService*>(param);
  if (self) {
    self->taskLoop();
  }
  vTaskDelete(nullptr);
}

void OtaModemService::taskLoop() {
  logger_.info("ota-modem: download start");

  bool failReported = false;
  auto reportFail = [&]() {
    if (failReported) {
      return;
    }
    failReported = true;
    if (ubidots_) {
      ubidots_->publishConsoleValue(299);
    }
    logger_.warn("ota-modem: publish fail code 299");
  };

  auto pauseRx = [&]() {
    if (ubidots_) {
      ubidots_->setRxPaused(true);
    }
  };
  auto resumeRx = [&]() {
    if (ubidots_) {
      ubidots_->setRxPaused(false);
    }
  };

  if (ubidots_) {
    ubidots_->setOtaActive(true);
  }

  pauseRx();

  if (!modem_->ensureDataSession()) {
    logger_.error("ota-modem: data session failed");
    reportFail();
    resumeRx();
    if (ubidots_) {
      ubidots_->setOtaActive(false);
    }
    busy_ = false;
    task_ = nullptr;
    return;
  }

  if (!initSdWithRetry("ota", SdProbeMode::Light)) {
    logger_.error("ota-modem: sd init failed");
    reportFail();
    resumeRx();
    if (ubidots_) {
      ubidots_->setOtaActive(false);
    }
    busy_ = false;
    task_ = nullptr;
    return;
  }


  bool ok = modem_->http().downloadToFile(
    kHttpDownloadUrl,
    kHttpDownloadPath,
    kHttpDownloadChunkSize,
    logSink,
    []() { return active_ ? active_->recoverSd() : false; },
    kSdFlushThreshold,
    []() { return active_ ? active_->remountSd() : false; }
  );

  if (!ok) {
    logger_.error("ota-modem: download failed");
    reportFail();
    resumeRx();
    if (ubidots_) {
      ubidots_->setOtaActive(false);
    }
    busy_ = false;
    task_ = nullptr;
    return;
  }

  logger_.info("ota-modem: download ok");

  logger_.info("ota-modem: install start");
  bool installed = otaManager_.installFromSd(kHttpDownloadPath);
  if (installed) {
    logger_.info("ota-modem: install ok");
    if (ubidots_) {
      ubidots_->publishConsoleValue(kOtaConsoleBeforeReboot);
    }
    finalizeSdAfterOta();
    delay(300);
    ESP.restart();
  } else {
    logger_.error("ota-modem: install failed");
    reportFail();
  }

  resumeRx();
  if (ubidots_) {
    ubidots_->setOtaActive(false);
  }

  busy_ = false;
  task_ = nullptr;
}

bool OtaModemService::initSdWithRetry(const char* context, SdProbeMode probeMode) {
  for (uint8_t attempt = 1; attempt <= kSdInitRetries; ++attempt) {
    logger_.logf("sd", "init %s attempt %u/%u", context, attempt, kSdInitRetries);
    SD.end();
    SPI.end();
    delay(kSdInitPreSpiDelayMs);
    SPI.begin(kSdSclk, kSdMiso, kSdMosi, kSdCs);
    delay(kSdInitPostSpiDelayMs);
    if (!SD.begin(kSdCs, SPI, kSdSpiClockHz)) {
      sdMounted_ = false;
      sdValidated_ = false;
      if (attempt < kSdInitRetries) {
        delay(kSdInitRetryGapMs);
      }
      continue;
    }
    sdMounted_ = true;
    sdValidated_ = false;
    if (probeMode == SdProbeMode::None) {
      return true;
    }
    bool probeOk = (probeMode == SdProbeMode::Light)
      ? sdLightWriteProbe("/sdprobe.tmp")
      : sdStressWriteProbe("/sdprobe.tmp");
    if (!probeOk) {
      sdValidated_ = false;
      if (attempt < kSdInitRetries) {
        delay(kSdInitRetryGapMs);
      }
      continue;
    }
    sdValidated_ = true;
    return true;
  }
  sdMounted_ = false;
  sdValidated_ = false;
  SD.end();
  SPI.end();
  return false;
}

bool OtaModemService::sdWriteProbe(const char* path, uint32_t probeSize,
                                  const char* label) {
  if (SD.exists(path)) {
    if (!SD.remove(path)) {
      return false;
    }
    if (SD.exists(path)) {
      return false;
    }
  }

  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }

  static uint8_t buffer[kSdProbeBlock];
  for (size_t i = 0; i < kSdProbeBlock; ++i) {
    buffer[i] = static_cast<uint8_t>(0xA5 ^ (i & 0xFF));
  }

  size_t remaining = probeSize;
  size_t totalWritten = 0;
  while (remaining > 0) {
    size_t chunk = remaining > kSdProbeBlock ? kSdProbeBlock : remaining;
    size_t written = file.write(buffer, chunk);
    if (written != chunk) {
      file.close();
      return false;
    }
    totalWritten += written;
    remaining -= written;
  }

  file.flush();
  file.close();

  File check = SD.open(path, FILE_READ);
  if (!check) {
    return false;
  }
  bool readOk = true;
  size_t size = check.size();
  if (size != probeSize) {
    check.close();
    return false;
  }
  if (!check.seek(0)) {
    readOk = false;
  }

  if (readOk) {
    static uint8_t readBuffer[kSdProbeBlock];
    size_t remainingRead = probeSize;
    size_t offset = 0;
    while (remainingRead > 0) {
      size_t chunk = remainingRead > kSdProbeBlock ? kSdProbeBlock : remainingRead;
      size_t readCount = check.readBytes(reinterpret_cast<char*>(readBuffer), chunk);
      if (readCount != chunk) {
        readOk = false;
        break;
      }
      for (size_t i = 0; i < readCount; ++i) {
        uint8_t expected = static_cast<uint8_t>(0xA5 ^ ((offset + i) & 0xFF));
        if (readBuffer[i] != expected) {
          readOk = false;
          break;
        }
      }
      if (!readOk) {
        break;
      }
      remainingRead -= readCount;
      offset += readCount;
    }
  }

  check.close();
  bool cleanupOk = SD.remove(path);
  return readOk && cleanupOk;
}

bool OtaModemService::sdLightWriteProbe(const char* path) {
  return sdWriteProbe(path, kSdProbeSizeLight, "light");
}

bool OtaModemService::sdStressWriteProbe(const char* path) {
  return sdWriteProbe(path, kSdProbeSizeStress, "stress");
}

bool OtaModemService::recoverSd() {
  bool ok = initSdWithRetry("recovery", SdProbeMode::Stress);
  return ok;
}

bool OtaModemService::remountSd() {
  bool ok = initSdWithRetry("remount", SdProbeMode::Light);
  return ok;
}

void OtaModemService::finalizeSdAfterOta() {
  SD.end();
  sdMounted_ = false;
  sdValidated_ = false;
  delay(kSdShutdownDelayMs);
}

void OtaModemService::logSink(bool isTx, const String& line) {
  if (active_) {
    String msg = String(isTx ? "> " : "< ") + line;
    active_->logger_.info(msg.c_str());
  }
}

void OtaModemService::onCopyToMemory(void* context) {
  auto* self = static_cast<OtaModemService*>(context);
  if (!self || !self->ubidots_) {
    return;
  }
  self->ubidots_->publishConsoleValue(kOtaConsoleAfterCopyToMemory);
}
