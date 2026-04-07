#pragma once

#include <Arduino.h>

class Logger;
class UbidotsService;

class TimeService {
 public:
  TimeService(Logger& logger, UbidotsService& ubidots);

  void begin();
  void update();

  bool isSynced() const;
  uint32_t lastSyncMs() const;
  String localTimeString() const;

 private:
  bool fetchTime();
  void setTimezone();

  Logger& logger_;
  UbidotsService& ubidots_;
  bool synced_ = false;
  uint32_t lastSyncMs_ = 0;
  uint32_t lastAttemptMs_ = 0;
};
