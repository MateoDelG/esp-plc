#include "services/time/time_service.h"

#include <sys/time.h>

#include "comms/ubidots/ubidots_service.h"
#include "core/logger.h"

namespace {
constexpr char kNtpServer[] = "pool.ntp.org";
constexpr uint32_t kSyncIntervalMs = 60U * 60U * 1000U;
constexpr uint32_t kRetryIntervalMs = 60U * 1000U;
constexpr uint32_t kNtpTimeoutMs = 15000U;
constexpr int kBogotaTzQuarterHours = -20;
constexpr char kTzBogota[] = "COT5";
}

TimeService::TimeService(Logger& logger, UbidotsService& ubidots)
  : logger_(logger), ubidots_(ubidots) {}

void TimeService::begin() {
  setTimezone();
  lastSyncMs_ = 0;
  lastAttemptMs_ = 0;
  synced_ = false;
}

void TimeService::update() {
  if (!ubidots_.isDataReady()) {
    return;
  }

  uint32_t now = millis();
  if (lastSyncMs_ != 0 && now - lastSyncMs_ < kSyncIntervalMs) {
    return;
  }
  if (lastAttemptMs_ != 0 && now - lastAttemptMs_ < kRetryIntervalMs) {
    return;
  }
  lastAttemptMs_ = now;

  if (fetchTime()) {
    lastSyncMs_ = now;
    synced_ = true;
  }
}

bool TimeService::isSynced() const {
  return synced_;
}

uint32_t TimeService::lastSyncMs() const {
  return lastSyncMs_;
}

String TimeService::localTimeString() const {
  time_t now = time(nullptr);
  if (now <= 0) {
    return String("--");
  }
  struct tm timeinfo;
  if (!localtime_r(&now, &timeinfo)) {
    return String("--");
  }
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

bool TimeService::fetchTime() {
  logger_.info("time: sync start");
  time_t epoch = 0;
  String clock;
  if (!ubidots_.modem().syncTimeWithNtp(kNtpServer, kBogotaTzQuarterHours,
                                       kNtpTimeoutMs, epoch, &clock)) {
    logger_.warn("time: ntp sync failed");
    return false;
  }
  if (epoch <= 0) {
    logger_.warn("time: epoch invalid");
    return false;
  }

  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  logger_.logf("time", "sync ok %s", localTimeString().c_str());
  return true;
}

void TimeService::setTimezone() {
  setenv("TZ", kTzBogota, 1);
  tzset();
}
