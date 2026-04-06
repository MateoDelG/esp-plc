#include "services/espnow/espnow_service.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>

#include <cstdio>
#include <cstring>
#include <cmath>

#include "services/telemetry/telemetry_service.h"

namespace {
constexpr char kPrefsNamespace[] = "espnow";
constexpr char kKeyTank1[] = "t1_mac";
constexpr char kKeyTank2[] = "t2_mac";
}

EspNowService* EspNowService::active_ = nullptr;

EspNowService::EspNowService(Logger& logger) : logger_(logger) {}

void EspNowService::begin() {
  active_ = this;
  WiFi.mode(WIFI_STA);
  logger_.logf("req", "STA MAC: %s", WiFi.macAddress().c_str());
  logger_.logf("req", "Channel: %u", WiFi.channel());
  channel_ = static_cast<uint8_t>(WiFi.channel());
  if (channel_ == 0) {
    channel_ = 1;
  }
  loadMacs();
  ready_ = initEspNow();
}

void EspNowService::update() {
  uint8_t chan = static_cast<uint8_t>(WiFi.channel());
  if (chan == 0 || chan == channel_) {
    return;
  }
  uint8_t prev = channel_;
  channel_ = chan;
  esp_now_deinit();
  ready_ = initEspNow();
  logger_.logf("espnow", "channel change %u -> %u", static_cast<unsigned>(prev),
               static_cast<unsigned>(channel_));
}

bool EspNowService::isReady() const {
  return ready_;
}

uint8_t EspNowService::channel() const {
  return channel_;
}

void EspNowService::setTelemetryService(TelemetryService* telemetry) {
  telemetry_ = telemetry;
}

bool EspNowService::setTankMac(uint8_t tank, const String& mac) {
  if (tank < 1 || tank > 2) {
    return false;
  }
  uint8_t parsed[6];
  if (!parseMac(mac, parsed)) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return false;
  }
  if (tank == 1) {
    prefs.putString(kKeyTank1, mac);
  } else {
    prefs.putString(kKeyTank2, mac);
  }
  prefs.end();

  uint8_t idx = static_cast<uint8_t>(tank - 1);
  memcpy(tankMacs_[idx], parsed, sizeof(parsed));
  hasMac_[idx] = true;
  if (ready_) {
    registerPeer(tank);
  }
  return true;
}

String EspNowService::getTankMac(uint8_t tank) const {
  if (tank < 1 || tank > 2) {
    return String();
  }
  uint8_t idx = static_cast<uint8_t>(tank - 1);
  if (!hasMac_[idx]) {
    return String();
  }
  return formatMac(tankMacs_[idx]);
}

bool EspNowService::requestTank(uint8_t tank) {
  if (!ready_ || tank < 1 || tank > 2) {
    return false;
  }
  uint8_t idx = static_cast<uint8_t>(tank - 1);
  if (!hasMac_[idx]) {
    return false;
  }
  String macStr = formatMac(tankMacs_[idx]);
  logger_.logf("req", "send GET_STATUS to %s", macStr.c_str());
  const char* payload = "GET_STATUS";
  esp_err_t res = esp_now_send(tankMacs_[idx],
                               reinterpret_cast<const uint8_t*>(payload),
                               strlen(payload));
  if (res != ESP_OK) {
    logger_.warn("espnow: send failed");
    return false;
  }
  logger_.logf("espnow", "request tank %u GET_STATUS", static_cast<unsigned>(tank));
  return true;
}

void EspNowService::onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (active_) {
    active_->handleRx(mac, data, len);
  }
}

void EspNowService::onSent(const uint8_t* mac, esp_now_send_status_t status) {
  (void)mac;
  if (active_) {
    String macStr = active_->formatMac(mac);
    active_->logger_.logf("req", "send_cb to %s status=%d", macStr.c_str(),
                          static_cast<int>(status));
  }
}

bool EspNowService::initEspNow() {
  esp_err_t res = esp_now_init();
  logger_.logf("req", "esp_now_init: %d", static_cast<int>(res));
  if (res != ESP_OK) {
    logger_.warn("espnow: init failed");
    return false;
  }
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);
  registerPeer(1);
  registerPeer(2);
  logger_.logf("espnow", "ready on channel %u", static_cast<unsigned>(channel_));
  return true;
}

bool EspNowService::loadMacs() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    return false;
  }
  String t1 = prefs.getString(kKeyTank1, "");
  String t2 = prefs.getString(kKeyTank2, "");
  prefs.end();

  if (t1.length() > 0) {
    uint8_t parsed[6];
    if (parseMac(t1, parsed)) {
      memcpy(tankMacs_[0], parsed, sizeof(parsed));
      hasMac_[0] = true;
    }
  }
  if (t2.length() > 0) {
    uint8_t parsed[6];
    if (parseMac(t2, parsed)) {
      memcpy(tankMacs_[1], parsed, sizeof(parsed));
      hasMac_[1] = true;
    }
  }
  return true;
}

bool EspNowService::registerPeer(uint8_t tank) {
  if (tank < 1 || tank > 2) {
    return false;
  }
  uint8_t idx = static_cast<uint8_t>(tank - 1);
  if (!hasMac_[idx]) {
    return false;
  }
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, tankMacs_[idx], 6);
  peer.channel = channel_;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_STA;
  if (esp_now_is_peer_exist(peer.peer_addr)) {
    esp_now_del_peer(peer.peer_addr);
  }
  String macStr = formatMac(peer.peer_addr);
  esp_err_t res = esp_now_add_peer(&peer);
  logger_.logf("req", "add_peer mac=%s ch=%u ifidx=%d res=%d", macStr.c_str(),
               static_cast<unsigned>(peer.channel), static_cast<int>(peer.ifidx),
               static_cast<int>(res));
  if (res != ESP_OK) {
    logger_.warn("espnow: add peer failed");
    return false;
  }
  return true;
}

bool EspNowService::parseMac(const String& mac, uint8_t out[6]) const {
  if (mac.length() != 17) {
    return false;
  }
  int values[6];
  if (sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x",
             &values[0], &values[1], &values[2],
             &values[3], &values[4], &values[5]) != 6) {
    return false;
  }
  for (uint8_t i = 0; i < 6; ++i) {
    if (values[i] < 0 || values[i] > 255) {
      return false;
    }
    out[i] = static_cast<uint8_t>(values[i]);
  }
  return true;
}

String EspNowService::formatMac(const uint8_t mac[6]) const {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

bool EspNowService::isSameMac(const uint8_t* a, const uint8_t* b) const {
  return memcmp(a, b, 6) == 0;
}

void EspNowService::handleRx(const uint8_t* mac, const uint8_t* data, int len) {
  if (len <= 0) {
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err) {
    logger_.logf("espnow", "rx json error: %s", err.c_str());
    return;
  }

  JsonObjectConst dataObj = doc["data"].as<JsonObjectConst>();
  if (dataObj.isNull()) {
    return;
  }
  JsonVariantConst levelVal = dataObj["level_cm"];
  JsonVariantConst tempVal = dataObj["temp_c"];
  if (levelVal.isNull() || tempVal.isNull()) {
    return;
  }

  float level = levelVal.as<float>();
  float temp = tempVal.as<float>();
  if (!std::isfinite(level) || !std::isfinite(temp)) {
    return;
  }

  bool hasTank1 = false;
  bool hasTank2 = false;
  float level1 = 0.0f;
  float temp1 = 0.0f;
  float level2 = 0.0f;
  float temp2 = 0.0f;

  if (hasMac_[0] && isSameMac(mac, tankMacs_[0])) {
    hasTank1 = true;
    level1 = level;
    temp1 = temp;
  } else if (hasMac_[1] && isSameMac(mac, tankMacs_[1])) {
    hasTank2 = true;
    level2 = level;
    temp2 = temp;
  }

  if (telemetry_ && (hasTank1 || hasTank2)) {
    telemetry_->updateLevelTempFromEspNow(hasTank1, level1, temp1,
                                          hasTank2, level2, temp2);
  }

  logger_.logf("espnow", "rx level=%.2f temp=%.2f", level, temp);
}
