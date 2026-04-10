#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "core/logger.h"

class WifiManager {
 public:
  explicit WifiManager(Logger& logger);

  bool begin();
  bool connect();
  bool connectWith(const String& ssid, const String& pass);
  bool connectOrStartAp(const String& ssid, const String& pass);
  void update(bool autoReconnect);
  bool startAp(const char* ssid, const char* pass);
  void stopAp();
  bool isApActive() const;
  void setCredentials(const String& ssid, const String& pass);
  String ssid() const;
  String password() const;
  bool isConnected() const;
  IPAddress localIp() const;

 private:
  Logger& logger_;
  String ssid_;
  String pass_;
};
