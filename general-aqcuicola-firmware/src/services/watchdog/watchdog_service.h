#pragma once

#include <Arduino.h>

class Logger;

class WatchdogService {
 public:
  explicit WatchdogService(Logger& logger);

  void begin();
  void feed();
  void setTimeouts(uint16_t swSec, uint16_t hwSec);

 private:
  Logger& logger_;
  uint32_t lastFeedMs_ = 0;
  bool started_ = false;
  uint32_t swTimeoutMs_ = 60000U;
  int hwTimeoutSec_ = 90;
  uint32_t lastFeedLogMs_ = 0;
  uint32_t feedCount_ = 0;
};
