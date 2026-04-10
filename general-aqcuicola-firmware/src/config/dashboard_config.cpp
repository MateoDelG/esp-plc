#include "config/dashboard_config.h"

#include <Preferences.h>

#include "core/logger.h"

namespace {
constexpr char kPrefsNamespace[] = "dashboard";
constexpr char kKeyBlowerA0[] = "blower_a0";
constexpr char kKeyBlowerA1[] = "blower_a1";
constexpr char kKeyBlowerDelay[] = "blower_delay";
constexpr char kKeyBlowerAlarm[] = "blower_alarm";
constexpr char kKeyAdsMask[] = "ads_mask";
constexpr char kKeyUartAutoEnabled[] = "uart_auto_en";
constexpr char kKeyUartAutoInterval[] = "uart_auto_min";
constexpr char kKeyEspNowAutoEnabled[] = "espnow_auto_en";
constexpr char kKeyEspNowAutoInterval[] = "espnow_auto_min";
constexpr char kKeyUbidotsInterval[] = "ubidots_min";
constexpr char kKeyWdtSwSec[] = "wdt_sw_sec";
constexpr char kKeyWdtHwSec[] = "wdt_hw_sec";
constexpr char kKeyWifiSsid[] = "wifi_ssid";
constexpr char kKeyWifiPass[] = "wifi_pass";
constexpr char kKeyWifiAuto[] = "wifi_auto";

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

uint16_t clampU16(uint16_t value, uint16_t minValue, uint16_t maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

uint8_t clampU8(uint8_t value, uint8_t minValue, uint8_t maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

void sanitize(DashboardConfig& config) {
  config.blowerThresholdA0 = clampFloat(config.blowerThresholdA0, 0.1f, 1.0f);
  config.blowerThresholdA1 = clampFloat(config.blowerThresholdA1, 0.1f, 1.0f);
  config.blowerNotifyDelaySec = clampU16(config.blowerNotifyDelaySec, 1, 600);
  config.adsEnabledMask = clampU8(config.adsEnabledMask, 0, 15);
  config.uartAutoIntervalMin = clampU16(config.uartAutoIntervalMin, 1, 60000);
  config.espNowAutoIntervalMin = clampU16(config.espNowAutoIntervalMin, 1, 60000);
  config.ubidotsPublishIntervalMin = clampU16(config.ubidotsPublishIntervalMin, 1, 1440);
  config.wdtSwSeconds = clampU16(config.wdtSwSeconds, 30, 3600);
  config.wdtHwSeconds = clampU16(config.wdtHwSeconds, 30, 3600);
  if (config.wdtHwSeconds < config.wdtSwSeconds) {
    config.wdtHwSeconds = config.wdtSwSeconds;
  }
  if (config.wifiSsid.length() > 32) {
    config.wifiSsid = config.wifiSsid.substring(0, 32);
  }
  if (config.wifiPass.length() > 64) {
    config.wifiPass = config.wifiPass.substring(0, 64);
  }
}

void logConfig(Logger* logger, const char* action, const DashboardConfig& config) {
  if (!logger) {
    return;
  }
  logger->logf("cfg",
               "dashboard %s a0=%.2f a1=%.2f delay=%u alarm=%u ads=0x%X "
               "uart=%u/%u espnow=%u/%u ubidots=%u wdt=%u/%u wifi=%s/%u",
               action,
               static_cast<double>(config.blowerThresholdA0),
               static_cast<double>(config.blowerThresholdA1),
               static_cast<unsigned>(config.blowerNotifyDelaySec),
               config.blowerAlarmEnabled ? 1U : 0U,
               static_cast<unsigned>(config.adsEnabledMask),
               config.uartAutoEnabled ? 1U : 0U,
               static_cast<unsigned>(config.uartAutoIntervalMin),
               config.espNowAutoEnabled ? 1U : 0U,
               static_cast<unsigned>(config.espNowAutoIntervalMin),
               static_cast<unsigned>(config.ubidotsPublishIntervalMin),
               static_cast<unsigned>(config.wdtSwSeconds),
               static_cast<unsigned>(config.wdtHwSeconds),
               config.wifiSsid.c_str(),
               config.wifiAutoReconnect ? 1U : 0U);
}
}  // namespace

bool loadDashboardConfig(DashboardConfig& config, Logger* logger) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    if (logger) {
      logger->warn("cfg: dashboard prefs open failed");
    }
    sanitize(config);
    return false;
  }

  config.blowerThresholdA0 = prefs.getFloat(kKeyBlowerA0, config.blowerThresholdA0);
  config.blowerThresholdA1 = prefs.getFloat(kKeyBlowerA1, config.blowerThresholdA1);
  config.blowerNotifyDelaySec =
    prefs.getUShort(kKeyBlowerDelay, config.blowerNotifyDelaySec);
  config.blowerAlarmEnabled = prefs.getBool(kKeyBlowerAlarm, config.blowerAlarmEnabled);
  config.adsEnabledMask = prefs.getUChar(kKeyAdsMask, config.adsEnabledMask);
  config.uartAutoEnabled = prefs.getBool(kKeyUartAutoEnabled, config.uartAutoEnabled);
  config.uartAutoIntervalMin =
    prefs.getUShort(kKeyUartAutoInterval, config.uartAutoIntervalMin);
  config.espNowAutoEnabled =
    prefs.getBool(kKeyEspNowAutoEnabled, config.espNowAutoEnabled);
  config.espNowAutoIntervalMin =
    prefs.getUShort(kKeyEspNowAutoInterval, config.espNowAutoIntervalMin);
  config.ubidotsPublishIntervalMin =
    prefs.getUShort(kKeyUbidotsInterval, config.ubidotsPublishIntervalMin);
  config.wdtSwSeconds = prefs.getUShort(kKeyWdtSwSec, config.wdtSwSeconds);
  config.wdtHwSeconds = prefs.getUShort(kKeyWdtHwSec, config.wdtHwSeconds);
  config.wifiSsid = prefs.getString(kKeyWifiSsid, config.wifiSsid);
  config.wifiPass = prefs.getString(kKeyWifiPass, config.wifiPass);
  config.wifiAutoReconnect =
    prefs.getBool(kKeyWifiAuto, config.wifiAutoReconnect);

  prefs.end();
  sanitize(config);
  logConfig(logger, "loaded", config);
  return true;
}

bool saveDashboardConfig(const DashboardConfig& config, Logger* logger) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    if (logger) {
      logger->warn("cfg: dashboard prefs open failed");
    }
    return false;
  }

  DashboardConfig sanitized = config;
  sanitize(sanitized);

  prefs.putFloat(kKeyBlowerA0, sanitized.blowerThresholdA0);
  prefs.putFloat(kKeyBlowerA1, sanitized.blowerThresholdA1);
  prefs.putUShort(kKeyBlowerDelay, sanitized.blowerNotifyDelaySec);
  prefs.putBool(kKeyBlowerAlarm, sanitized.blowerAlarmEnabled);
  prefs.putUChar(kKeyAdsMask, sanitized.adsEnabledMask);
  prefs.putBool(kKeyUartAutoEnabled, sanitized.uartAutoEnabled);
  prefs.putUShort(kKeyUartAutoInterval, sanitized.uartAutoIntervalMin);
  prefs.putBool(kKeyEspNowAutoEnabled, sanitized.espNowAutoEnabled);
  prefs.putUShort(kKeyEspNowAutoInterval, sanitized.espNowAutoIntervalMin);
  prefs.putUShort(kKeyUbidotsInterval, sanitized.ubidotsPublishIntervalMin);
  prefs.putUShort(kKeyWdtSwSec, sanitized.wdtSwSeconds);
  prefs.putUShort(kKeyWdtHwSec, sanitized.wdtHwSeconds);
  prefs.putString(kKeyWifiSsid, sanitized.wifiSsid);
  prefs.putString(kKeyWifiPass, sanitized.wifiPass);
  prefs.putBool(kKeyWifiAuto, sanitized.wifiAutoReconnect);
  prefs.end();

  logConfig(logger, "saved", sanitized);
  return true;
}
