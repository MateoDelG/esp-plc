#include "services/watchdog/watchdog_service.h"

#include <esp_system.h>
#include <esp_task_wdt.h>

#include "core/logger.h"

namespace {
constexpr uint32_t kMinTimeoutSec = 30U;
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
