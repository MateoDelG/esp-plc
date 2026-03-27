#include "services/ota_modem/ota_modem_service.h"

#include <Update.h>

#include "config/ota_modem_codes.h"
#include "modem_http_storage.h"

OtaModemService* OtaModemService::active_ = nullptr;

OtaModemService::OtaModemService(Logger& logger) : logger_(logger) {}

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
      ubidots_->publishConsoleValue(kOtaFailCode);
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

  bool ok = modem_->http().downloadToModemFile(
    kHttpDownloadUrl,
    kHttpModemPath,
    logSink
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

  int size = modem_->http().lastHttpLength();
  if (size <= 0) {
    logger_.error("ota-modem: invalid download size");
    reportFail();
    resumeRx();
    if (ubidots_) {
      ubidots_->setOtaActive(false);
    }
    busy_ = false;
    task_ = nullptr;
    return;
  }

  logger_.info("ota-modem: install start");
  bool installed = installFromModemFile(kHttpModemPath,
                                        static_cast<size_t>(size));
  deleteModemFile(kHttpModemPath);
  if (installed) {
    logger_.info("ota-modem: install ok");
    if (ubidots_) {
      ubidots_->publishConsoleValue(kOtaSuccessCode);
    }
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

void OtaModemService::logSink(bool isTx, const String& line) {
  if (active_) {
    String msg = String(isTx ? "> " : "< ") + line;
    active_->logger_.info(msg.c_str());
  }
}

bool OtaModemService::installFromModemFile(const char* modemPath, size_t size) {
  if (!modemPath || size == 0) {
    return false;
  }

  int handle = -1;
  if (!fsOpenRead(*modem_, modemPath, handle, logSink)) {
    logger_.error("ota-modem: modem file open failed");
    return false;
  }

  if (!Update.begin(size)) {
    fsClose(*modem_, handle);
    logger_.error("ota-modem: update begin failed");
    return false;
  }

  size_t remaining = size;
  uint8_t buffer[kHttpDownloadChunkSize];
  bool ok = true;
  while (remaining > 0) {
    size_t toRead = remaining > kHttpDownloadChunkSize
                        ? kHttpDownloadChunkSize
                        : remaining;
    modem_->tinyGsm().sendAT(GF("+FSREAD="), handle, GF(","), toRead);

    int dataLen = -1;
    uint32_t headerStart = millis();
    while (millis() - headerStart < 8000UL) {
      String header;
      if (!modem_->at().readLine(header, 500)) {
        continue;
      }
      if (header.length() == 0) {
        continue;
      }
      if (header == "OK") {
        continue;
      }
      if (header == "ERROR") {
        ok = false;
        break;
      }
      if (parseFsConnectLine(header, dataLen) ||
          parseFsReadHeader(header, dataLen)) {
        break;
      }
    }

    if (!ok || dataLen <= 0) {
      ok = false;
      break;
    }

    if (static_cast<size_t>(dataLen) > toRead) {
      ok = false;
      break;
    }

    if (!modem_->at().readExactToBuffer(buffer,
                                        static_cast<size_t>(dataLen),
                                        60000UL)) {
      ok = false;
      break;
    }

    if (Update.write(buffer, static_cast<size_t>(dataLen)) !=
        static_cast<size_t>(dataLen)) {
      ok = false;
      break;
    }

    if (!waitFsReadTailOk(*modem_, logSink, 5000UL)) {
      ok = false;
      break;
    }

    remaining -= static_cast<size_t>(dataLen);
  }

  fsClose(*modem_, handle);

  if (!ok) {
    Update.abort();
    logger_.error("ota-modem: update write failed");
    return false;
  }

  if (!Update.end(true)) {
    logger_.error("ota-modem: update end failed");
    return false;
  }

  return true;
}

void OtaModemService::deleteModemFile(const char* modemPath) {
  if (!modemPath || !modem_) {
    return;
  }
  modem_->at().exec(3000L, GF("+FSDEL=\""), modemPath, GF("\""));
}
