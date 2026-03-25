#include "services/ota/ota_service.h"

#include <ArduinoOTA.h>

#include "config/app_config.h"

OtaService::OtaService(Logger& logger) : logger_(logger) {}

bool OtaService::begin() {
  if (ready_) {
    return true;
  }

  ArduinoOTA.setHostname(kDeviceHostname);
  if (kOtaPassword != nullptr && kOtaPassword[0] != '\0') {
    ArduinoOTA.setPassword(kOtaPassword);
  }

  ArduinoOTA.onStart([this]() {
    logger_.info("ota: start");
  });

  ArduinoOTA.onEnd([this]() {
    logger_.info("ota: end");
  });

  ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
    uint8_t percent = (progress * 100) / total;
    logger_.logf("ota", "progress: %u%%", percent);
  });

  ArduinoOTA.onError([this](ota_error_t error) {
    switch (error) {
      case OTA_AUTH_ERROR:
        logger_.error("ota: auth error");
        break;
      case OTA_BEGIN_ERROR:
        logger_.error("ota: begin error");
        break;
      case OTA_CONNECT_ERROR:
        logger_.error("ota: connect error");
        break;
      case OTA_RECEIVE_ERROR:
        logger_.error("ota: receive error");
        break;
      case OTA_END_ERROR:
        logger_.error("ota: end error");
        break;
      default:
        logger_.error("ota: unknown error");
        break;
    }
  });

  ArduinoOTA.begin();
  ready_ = true;
  logger_.info("ota: ready");
  return true;
}

void OtaService::update() {
  if (ready_) {
    ArduinoOTA.handle();
  }
}

bool OtaService::isReady() const {
  return ready_;
}
