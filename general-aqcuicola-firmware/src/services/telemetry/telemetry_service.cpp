#include "services/telemetry/telemetry_service.h"

#include "config/ubidots_config.h"

TelemetryService::TelemetryService(Logger& logger, UbidotsService& ubidots)
  : logger_(logger), ubidots_(ubidots), data_() {}

void TelemetryService::begin() {
  lastPublishMs_ = 0;
  if (publishIntervalMs_ == 0) {
    publishIntervalMs_ = kUbiPublishIntervalMs;
  }
}

void TelemetryService::update() {
  if (ubidots_.isOtaMode()) {
    return;
  }

  uint32_t now = millis();
  if (publishNowPending_ && ubidots_.isConnected()) {
    publishNowPending_ = false;
    lastPublishOk_ = ubidots_.publishTelemetry(data_);
    if (lastPublishOk_) {
      lastPublishMs_ = now;
      logger_.info("telemetry: pending publish sent");
    } else {
      publishNowPending_ = true;
      logger_.warn("telemetry: pending publish failed");
    }
    return;
  }

  if (now - lastPublishMs_ < publishIntervalMs_) {
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

bool TelemetryService::publishNow() {
  if (ubidots_.isOtaMode()) {
    lastPublishOk_ = false;
    logger_.warn("telemetry: publish-now skipped, ota mode");
    return false;
  }

  if (!ubidots_.isConnected()) {
    lastPublishOk_ = false;
    publishNowPending_ = true;
    logger_.warn("telemetry: publish-now skipped, ubidots disconnected");
    return false;
  }

  lastPublishOk_ = ubidots_.publishTelemetry(data_);
  if (lastPublishOk_) {
    publishNowPending_ = false;
    lastPublishMs_ = millis();
    logger_.info("telemetry: published by console");
  } else {
    publishNowPending_ = true;
    logger_.warn("telemetry: publish-now failed");
  }
  return lastPublishOk_;
}

void TelemetryService::setBlowersState(bool state) {
  data_.stateBlowers = state ? 1 : 0;
}

void TelemetryService::setPublishIntervalMs(uint32_t intervalMs) {
  if (intervalMs == 0) {
    publishIntervalMs_ = kUbiPublishIntervalMs;
    return;
  }
  publishIntervalMs_ = intervalMs;
}

void TelemetryService::updatePhO2FromUart(bool hasTank1, float ph1, float o2_1,
                                         bool hasTank2, float ph2, float o2_2) {
  if (hasTank1) {
    data_.phTank1 = ph1;
    data_.o2Tank1 = o2_1;
  }
  if (hasTank2) {
    data_.phTank2 = ph2;
    data_.o2Tank2 = o2_2;
  }
}

void TelemetryService::updateLevelTempFromEspNow(bool hasTank1, float level1,
                                                 float temp1, bool hasTank2,
                                                 float level2, float temp2) {
  if (hasTank1) {
    data_.levelTank1 = level1;
    data_.tempTank1 = temp1;
  }
  if (hasTank2) {
    data_.levelTank2 = level2;
    data_.tempTank2 = temp2;
  }
}

const TelemetryPacket& TelemetryService::data() const {
  return data_;
}

bool TelemetryService::lastPublishOk() const {
  return lastPublishOk_;
}
