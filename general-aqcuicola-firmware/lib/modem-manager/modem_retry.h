// Centralized retry policy helpers
#ifndef MODEM_RETRY_H
#define MODEM_RETRY_H

#include <Arduino.h>

#include "modem_manager.h"

struct RetryPolicy {
  uint8_t attempts = 1;
  uint32_t delayMs = 0;
  const char* label = nullptr;
  bool logEachAttempt = true;
};

template <typename Fn>
bool retry(ModemManager& modem, const RetryPolicy& policy, const char* subsystem,
           Fn fn) {
  if (policy.attempts == 0) {
    return false;
  }

  for (uint8_t attempt = 0; attempt < policy.attempts; ++attempt) {
    if (policy.logEachAttempt) {
      String label = policy.label ? policy.label : "retry";
      modem.logInfo(subsystem,
                    label + " attempt " + String(attempt + 1) + "/" +
                        String(policy.attempts));
    }

    if (fn()) {
      return true;
    }

    if (attempt + 1 < policy.attempts && policy.delayMs > 0) {
      modem.idle(policy.delayMs);
    }
  }

  return false;
}

#endif
