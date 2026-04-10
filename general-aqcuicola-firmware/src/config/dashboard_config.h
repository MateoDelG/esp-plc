#pragma once

#include <Arduino.h>

class Logger;

struct DashboardConfig {
  float blowerThresholdA0 = 0.5f;
  float blowerThresholdA1 = 0.5f;
  uint16_t blowerNotifyDelaySec = 10;
  bool blowerAlarmEnabled = false;
  uint8_t adsEnabledMask = 0x0F;
  bool uartAutoEnabled = false;
  uint16_t uartAutoIntervalMin = 5;
  bool espNowAutoEnabled = false;
  uint16_t espNowAutoIntervalMin = 5;
  uint16_t ubidotsPublishIntervalMin = 5;
  uint16_t wdtSwSeconds = 60;
  uint16_t wdtHwSeconds = 90;
  String wifiSsid;
  String wifiPass;
  bool wifiAutoReconnect = true;
};

bool loadDashboardConfig(DashboardConfig& config, Logger* logger);
bool saveDashboardConfig(const DashboardConfig& config, Logger* logger);
