#include "services/telemetry/telemetry_service.h"

#include "config/ubidots_config.h"

TelemetryService::TelemetryService(Logger& logger, UbidotsService& ubidots)
  : logger_(logger), ubidots_(ubidots), data_() {}

void TelemetryService::begin() {
  lastPublishMs_ = 0;
}

void TelemetryService::update() {
  if (ubidots_.isOtaMode()) {
    return;
  }

  uint32_t now = millis();
  if (now - lastPublishMs_ < kUbiPublishIntervalMs) {
    return;
  }

  lastPublishMs_ = now;
  if (!ubidots_.isConnected()) {
    return;
  }
  lastPublishOk_ = ubidots_.publishTelemetry(data_);
  if (lastPublishOk_) {
    logger_.info("telemetry: published");
  } else {
    logger_.warn("telemetry: publish failed");
  }
}

const TelemetryPacket& TelemetryService::data() const {
  return data_;
}

bool TelemetryService::lastPublishOk() const {
  return lastPublishOk_;
}
