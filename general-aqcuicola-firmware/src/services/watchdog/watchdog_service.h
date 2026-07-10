#pragma once

#include <Arduino.h>

class Logger;

class WatchdogService {
 public:
  explicit WatchdogService(Logger& logger);

  void begin();
  void feed();
  void setTimeouts(uint16_t swSec, uint16_t hwSec);
  void registerCurrentTask();
  void unregisterCurrentTask();
  void feedCurrentTask();
  void markModemTask();
  void markRxTask();
  void markMqttActivity();
  void setMqttActivityActive(bool active);
  void markSmsActivity();
  void setOtaModemActive(bool active);
  void markOtaModem();

 private:
  void checkCommunicationHeartbeats(uint32_t now);
  bool checkHeartbeat(const char* label, bool active, uint32_t lastMs,
                      uint32_t timeoutMs, uint32_t now);

  Logger& logger_;
  uint32_t lastFeedMs_ = 0;
  bool started_ = false;
  uint32_t swTimeoutMs_ = 60000U;
  int hwTimeoutSec_ = 90;
  uint32_t lastFeedLogMs_ = 0;
  uint32_t feedCount_ = 0;
  volatile bool modemTaskActive_ = false;
  volatile bool rxTaskActive_ = false;
  volatile bool mqttActivityActive_ = false;
  volatile bool smsActivityActive_ = false;
  volatile bool otaModemActive_ = false;
  volatile uint32_t lastModemTaskMs_ = 0;
  volatile uint32_t lastRxTaskMs_ = 0;
  volatile uint32_t lastMqttActivityMs_ = 0;
  volatile uint32_t lastSmsActivityMs_ = 0;
  volatile uint32_t lastOtaModemMs_ = 0;
  uint32_t lastCommLogMs_ = 0;
};
