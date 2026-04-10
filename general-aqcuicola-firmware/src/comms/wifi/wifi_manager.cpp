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
  const char* ssid = ssid_.length() > 0 ? ssid_.c_str() : kWifiSsid;
  const char* pass = pass_.length() > 0 ? pass_.c_str() : kWifiPassword;
  WiFi.begin(ssid, pass);

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

bool WifiManager::connectWith(const String& ssid, const String& pass) {
  setCredentials(ssid, pass);
  WiFi.disconnect(true);
  delay(200);
  return connect();
}

bool WifiManager::connectOrStartAp(const String& ssid, const String& pass) {
  setCredentials(ssid, pass);
  bool ok = connect();
  if (!ok) {
    startAp("Aquaculture control", "12345678");
  }
  return ok;
}

void WifiManager::update(bool autoReconnect) {
  if (isConnected()) {
    if (isApActive()) {
      stopAp();
    }
    return;
  }
  if (!autoReconnect) {
    return;
  }
  static uint32_t lastAttempt = 0;
  uint32_t now = millis();
  if (lastAttempt != 0 && now - lastAttempt < 30000U) {
    return;
  }
  lastAttempt = now;
  if (connect()) {
    if (isApActive()) {
      stopAp();
    }
    return;
  }
  if (!isApActive()) {
    startAp("Aquaculture control", "12345678");
  }
}

bool WifiManager::startAp(const char* ssid, const char* pass) {
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(kLocalIp, kLocalIp, kSubnetMask);
  bool ok = WiFi.softAP(ssid, pass);
  if (ok) {
    logger_.logf("wifi", "ap started: %s", WiFi.softAPIP().toString().c_str());
  } else {
    logger_.warn("wifi: ap start failed");
  }
  return ok;
}

void WifiManager::stopAp() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  logger_.info("wifi: ap stopped");
}

bool WifiManager::isApActive() const {
  return WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA;
}

void WifiManager::setCredentials(const String& ssid, const String& pass) {
  ssid_ = ssid;
  pass_ = pass;
}

String WifiManager::ssid() const {
  return ssid_;
}

String WifiManager::password() const {
  return pass_;
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
