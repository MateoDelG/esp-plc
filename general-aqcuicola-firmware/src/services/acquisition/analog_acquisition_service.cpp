#include "services/acquisition/analog_acquisition_service.h"

AnalogAcquisitionService::AnalogAcquisitionService(Logger& logger)
  : logger_(logger), ads_(kAds1115I2cAddress) {
  for (uint8_t i = 0; i < 4; ++i) {
    snapshot_.channels[i].channel = i;
  }
}

bool AnalogAcquisitionService::begin() {
  ads_.setGain(kAds1115DefaultGain);
  ads_.setDataRate(kAds1115DefaultRate);
  ads_.setAveraging(kAds1115DefaultAveraging);

  if (!ads_.begin()) {
    snapshot_.adsConnected = false;
    ready_ = false;
    logger_.error("ads1115: init failed");
    return false;
  }

  snapshot_.adsConnected = true;
  ready_ = true;
  logger_.info("ads1115: ready");
  return true;
}

void AnalogAcquisitionService::update() {
  if (!ready_) {
    return;
  }

  uint32_t now = millis();
  if (now - lastReadMs_ < kAds1115ReadIntervalMs) {
    return;
  }
  lastReadMs_ = now;

  snapshot_.adsConnected = ads_.isConnected();
  snapshot_.enabledMask = enabledMask_;

  for (uint8_t ch = 0; ch < 4; ++ch) {
    if (!isChannelEnabled(ch)) {
      markInvalid(ch);
      continue;
    }

    bool ok = readChannel(ch);
    if (ok) {
      lastReadOk_[ch] = true;
    } else if (lastReadOk_[ch]) {
      logger_.warn("ads1115: read failed");
      lastReadOk_[ch] = false;
    }
  }
}

void AnalogAcquisitionService::setEnabledMask(uint8_t mask) {
  enabledMask_ = mask & 0x0F;
}

bool AnalogAcquisitionService::isChannelEnabled(uint8_t channel) const {
  if (channel > 3) {
    return false;
  }
  return (enabledMask_ & (1U << channel)) != 0;
}

bool AnalogAcquisitionService::isReady() const {
  return ready_;
}

const AnalogSnapshot& AnalogAcquisitionService::data() const {
  return snapshot_;
}

bool AnalogAcquisitionService::readChannel(uint8_t channel) {
  float volts = 0.0f;
  if (!ads_.readSingle(channel, volts)) {
    markInvalid(channel);
    return false;
  }

  AnalogChannelReading& reading = snapshot_.channels[channel];
  reading.raw = ads_.lastRaw();
  reading.volts = volts;
  reading.timestampMs = millis();
  reading.valid = true;
  return true;
}

void AnalogAcquisitionService::markInvalid(uint8_t channel) {
  if (channel > 3) {
    return;
  }
  AnalogChannelReading& reading = snapshot_.channels[channel];
  reading.valid = false;
  reading.timestampMs = millis();
}
