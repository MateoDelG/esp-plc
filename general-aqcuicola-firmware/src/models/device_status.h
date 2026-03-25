#pragma once

#include <Arduino.h>

struct DeviceStatus {
  bool wifiConnected = false;
  bool otaReady = false;
  bool lastWifiAttemptOk = false;
  bool modemReady = false;
  bool cloudConnected = false;
  bool ubidotsConnected = false;
  bool consoleSubscribed = false;
  bool lastPublishOk = false;
  uint32_t lastConsoleMessageMs = 0;
  IPAddress localIp;
};
