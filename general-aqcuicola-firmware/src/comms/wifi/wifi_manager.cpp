#include "comms/wifi/wifi_manager.h"

#include "config/app_config.h"
#include "config/network_config.h"

WifiManager::WifiManager(Logger& logger) : logger_(logger) {}

bool WifiManager::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(kDeviceHostname);

  bool configOk = WiFi.config(
    kLocalIp,
    kGatewayIp,
    kSubnetMask,
    kPrimaryDns,
    kSecondaryDns
  );

  if (configOk) {
    logger_.info("wifi: static ip configured");
  } else {
    logger_.warn("wifi: static ip config failed");
  }

  return connect();
}

bool WifiManager::connect() {
  logger_.info("wifi: connecting");
  WiFi.begin(kWifiSsid, kWifiPassword);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectTimeoutMs) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    logger_.logf("wifi", "connected: %s", ip.c_str());
    return true;
  }

  logger_.warn("wifi: connect timeout");
  return false;
}

bool WifiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

IPAddress WifiManager::localIp() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP();
  }
  return IPAddress();
}
