#include "config/modem_config.h"

#include "core/logger.h"

namespace {
Logger* gLogger = nullptr;

void modemLogSink(bool isTx, const String& line) {
  if (!gLogger) {
    return;
  }
  gLogger->logf("modem", "%s %s", isTx ? "tx" : "rx", line.c_str());
}
}  // namespace

ModemConfig makeModemConfig(Logger& logger) {
  gLogger = &logger;

  static const char kApn[] = "internet.comcel.com.co";
  static const char kUser[] = "comcel";
  static const char kPass[] = "comcel";

  static const int8_t kPinTx = 26;
  static const int8_t kPinRx = 27;
  static const int8_t kPwrKey = 4;

  ModemConfig config{
    Serial2,
    Serial,
    ModemPins{ kPinTx, kPinRx, kPwrKey, -1 },
    ModemApnConfig{ kApn, kUser, kPass },
    "",
    115200,
    5000,
    60000,
    3,
    true,
    modemLogSink
  };

  return config;
}
