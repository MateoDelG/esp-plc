#pragma once

#include <Arduino.h>

#include "core/logger.h"

#include <esp_now.h>

class TelemetryService;

class EspNowService {
 public:
  explicit EspNowService(Logger& logger);

  void begin();
  void update();
  bool isReady() const;
  uint8_t channel() const;

  void setTelemetryService(TelemetryService* telemetry);
  bool setTankMac(uint8_t tank, const String& mac);
  String getTankMac(uint8_t tank) const;
  bool requestTank(uint8_t tank);

 private:
  static void onRecv(const uint8_t* mac, const uint8_t* data, int len);
  static void onSent(const uint8_t* mac, esp_now_send_status_t status);

  bool initEspNow();
  bool loadMacs();
  bool registerPeer(uint8_t tank);
  bool parseMac(const String& mac, uint8_t out[6]) const;
  String formatMac(const uint8_t mac[6]) const;
  bool isSameMac(const uint8_t* a, const uint8_t* b) const;
  void handleRx(const uint8_t* mac, const uint8_t* data, int len);

  static constexpr uint32_t kResponseTimeoutMs = 3000U;

  Logger& logger_;
  TelemetryService* telemetry_ = nullptr;
  bool ready_ = false;
  uint8_t channel_ = 0;
  uint8_t tankMacs_[2][6] = {{0}};
  bool hasMac_[2] = {false, false};
  uint32_t seq_ = 0;
  uint32_t lastRequestMs_[2] = {0, 0};
  bool pendingResponse_[2] = {false, false};
  bool timeoutApplied_[2] = {false, false};

  static EspNowService* active_;
};
