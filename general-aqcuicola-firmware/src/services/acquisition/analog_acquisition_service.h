#pragma once

#include <Arduino.h>

#include "config/ads1115_config.h"
#include "core/logger.h"
#include "drivers/ads1115/ads1115_manager.h"
#include "models/analog_snapshot.h"

class AnalogAcquisitionService {
 public:
  explicit AnalogAcquisitionService(Logger& logger);

  bool begin();
  void update();

  void setEnabledMask(uint8_t mask);
  bool isChannelEnabled(uint8_t channel) const;
  bool isReady() const;
  const AnalogSnapshot& data() const;

 private:
  bool readChannel(uint8_t channel);
  void markInvalid(uint8_t channel);

  Logger& logger_;
  ADS1115Manager ads_;
  AnalogSnapshot snapshot_;
  uint32_t lastReadMs_ = 0;
  uint8_t enabledMask_ = kAds1115DefaultEnabledMask;
  bool ready_ = false;
  bool lastReadOk_[4] = {false, false, false, false};
};
