#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "core/logger.h"

class WifiManager {
 public:
  explicit WifiManager(Logger& logger);

  bool begin();
  bool connect();
  bool isConnected() const;
  IPAddress localIp() const;

 private:
  Logger& logger_;
};
