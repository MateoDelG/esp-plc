#include "services/watchdog/watchdog_service.h"

#include <esp_system.h>
#include <esp_task_wdt.h>

#include "core/logger.h"

namespace {
constexpr uint32_t kMinTimeoutSec = 30U;
constexpr uint32_t kModemTaskTimeoutMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kRxMqttSmsTimeoutMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kOtaModemTimeoutMs = 60UL * 1000UL;
constexpr uint32_t kCommLogIntervalMs = 60UL * 1000UL;

uint32_t ageSeconds(uint32_t lastMs, uint32_t now) {
  if (lastMs == 0) {
    return 0;
  }
  return (now - lastMs) / 1000UL;
}
}

WatchdogService::WatchdogService(Logger& logger) : logger_(logger) {}

void WatchdogService::begin() {
  esp_task_wdt_init(hwTimeoutSec_, true);
  esp_task_wdt_add(nullptr);
  lastFeedMs_ = millis();
  lastFeedLogMs_ = lastFeedMs_;
  feedCount_ = 0;
  started_ = true;
  logger_.info("wdt: started");
}

void WatchdogService::feed() {
  if (!started_) {
    return;
  }
  uint32_t now = millis();
  feedCount_++;
  if (lastFeedMs_ != 0 && now - lastFeedMs_ > swTimeoutMs_) {
    logger_.warn("wdt: sw timeout");
    esp_restart();
  }
  esp_task_wdt_reset();
  lastFeedMs_ = now;
  checkCommunicationHeartbeats(now);
  if (lastFeedLogMs_ != 0 && now - lastFeedLogMs_ >= 60000U) {
    logger_.logf("wdt", "feed count=%u", feedCount_);
    feedCount_ = 0;
    lastFeedLogMs_ = now;
  }
}

void WatchdogService::setTimeouts(uint16_t swSec, uint16_t hwSec) {
  if (swSec < kMinTimeoutSec) {
    swSec = kMinTimeoutSec;
  }
  if (hwSec < kMinTimeoutSec) {
    hwSec = kMinTimeoutSec;
  }
  if (hwSec < swSec) {
    hwSec = swSec;
  }
  swTimeoutMs_ = static_cast<uint32_t>(swSec) * 1000U;
  hwTimeoutSec_ = static_cast<int>(hwSec);
  esp_task_wdt_deinit();
  esp_task_wdt_init(hwTimeoutSec_, true);
  esp_task_wdt_add(nullptr);
}

void WatchdogService::registerCurrentTask() {
  esp_task_wdt_add(nullptr);
}

void WatchdogService::unregisterCurrentTask() {
  esp_task_wdt_delete(nullptr);
}

void WatchdogService::feedCurrentTask() {
  esp_task_wdt_add(nullptr);
  esp_task_wdt_reset();
}

void WatchdogService::markModemTask() {
  lastModemTaskMs_ = millis();
  modemTaskActive_ = true;
}

void WatchdogService::markRxTask() {
  lastRxTaskMs_ = millis();
  rxTaskActive_ = true;
}

void WatchdogService::markMqttActivity() {
  lastMqttActivityMs_ = millis();
  mqttActivityActive_ = true;
}

void WatchdogService::setMqttActivityActive(bool active) {
  mqttActivityActive_ = active;
  if (active && lastMqttActivityMs_ == 0) {
    lastMqttActivityMs_ = millis();
  }
}

void WatchdogService::markSmsActivity() {
  lastSmsActivityMs_ = millis();
  smsActivityActive_ = true;
}

void WatchdogService::setOtaModemActive(bool active) {
  otaModemActive_ = active;
  if (active) {
    markOtaModem();
  }
}

void WatchdogService::markOtaModem() {
  lastOtaModemMs_ = millis();
  otaModemActive_ = true;
}

bool WatchdogService::checkHeartbeat(const char* label, bool active,
                                     uint32_t lastMs, uint32_t timeoutMs,
                                     uint32_t now) {
  if (!active || lastMs == 0) {
    return true;
  }
  if (now - lastMs <= timeoutMs) {
    return true;
  }
  logger_.logf("wdt", "%s heartbeat timeout (%u sec)", label,
               static_cast<unsigned>((now - lastMs) / 1000UL));
  esp_restart();
  return false;
}

void WatchdogService::checkCommunicationHeartbeats(uint32_t now) {
  if (!checkHeartbeat("modemTask", modemTaskActive_, lastModemTaskMs_,
                      kModemTaskTimeoutMs, now)) {
    return;
  }
  if (!checkHeartbeat("mqttRxTask", rxTaskActive_, lastRxTaskMs_,
                      kRxMqttSmsTimeoutMs, now)) {
    return;
  }
  if (!checkHeartbeat("mqtt", mqttActivityActive_, lastMqttActivityMs_,
                      kRxMqttSmsTimeoutMs, now)) {
    return;
  }
  if (!checkHeartbeat("sms", smsActivityActive_, lastSmsActivityMs_,
                      kRxMqttSmsTimeoutMs, now)) {
    return;
  }
  if (!checkHeartbeat("otaModem", otaModemActive_, lastOtaModemMs_,
                      kOtaModemTimeoutMs, now)) {
    return;
  }

  if (lastCommLogMs_ == 0 || now - lastCommLogMs_ >= kCommLogIntervalMs) {
    logger_.logf("wdt", "hb modem=%u rx=%u mqtt=%u sms=%u ota=%u",
                 static_cast<unsigned>(ageSeconds(lastModemTaskMs_, now)),
                 static_cast<unsigned>(ageSeconds(lastRxTaskMs_, now)),
                 static_cast<unsigned>(ageSeconds(lastMqttActivityMs_, now)),
                 static_cast<unsigned>(ageSeconds(lastSmsActivityMs_, now)),
                 static_cast<unsigned>(ageSeconds(lastOtaModemMs_, now)));
    lastCommLogMs_ = now;
  }
}
