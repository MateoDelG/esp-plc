#include "modem_manager.h"

#include "modem_parsers.h"

ModemManager::ModemManager(const ModemConfig& config)
    : config_(config),
#ifdef DUMP_AT_COMMANDS
      tapStream_(config.serialAT, config.serialMon),
#else
      tapStream_(config.serialAT, [this](bool isTx, const String& line) {
        ModemLogSink sink = config_.modemLogSink;
        if (sink) {
          sink(isTx, line);
        }
      }),
#endif
      modem_(tapStream_),
      urc_(),
      at_(modem_, tapStream_, urc_),
      core_(*this),
      data_(*this),
      http_(*this),
      mqtt_(*this) {}

void ModemManager::setLogsEnabled(bool enabled) { config_.enableLogs = enabled; }

bool ModemManager::powerOn() { return core_.powerOn(); }

bool ModemManager::powerOff() { return core_.powerOff(); }

bool ModemManager::restart() { return core_.restart(); }

bool ModemManager::begin() { return core_.begin(); }

bool ModemManager::waitForNetwork() { return core_.waitForNetwork(); }

bool ModemManager::ensureDataSession() { return data_.ensureDataSession(); }

bool ModemManager::disconnectGprs() { return modem_.gprsDisconnect(); }

bool ModemManager::ping(const char* host, uint8_t count, uint32_t timeoutMs) {
  if (!host || strlen(host) == 0) {
    return false;
  }

  String ip = host;
  AtResult dns = at_.exec(20000L, GF("+CDNSGIP=\""), host, GF("\""));
  if (dns.ok) {
    ModemParsers::parseCdnsgipIp(dns.raw, ip);
  } else {
    ip = "8.8.8.8";
  }

  if (ip.length() == 0) {
    ip = "8.8.8.8";
  }

  uint32_t waitMs = timeoutMs * static_cast<uint32_t>(count) + 5000U;
  AtResult pingRes =
      at_.exec(waitMs, GF("+CPING=\""), ip.c_str(), GF("\","), count);

  if (!pingRes.ok) {
    pingRes = at_.exec(waitMs, GF("+PING=\""), ip.c_str(), GF("\""));
  }

  if (!pingRes.ok) {
    return false;
  }

  if (pingRes.raw.indexOf("+CPING:") < 0 &&
      pingRes.raw.indexOf("+PING:") < 0) {
    return false;
  }

  if (pingRes.raw.indexOf(",0,0,0") >= 0 ||
      pingRes.raw.indexOf(",0,0,0,0") >= 0) {
    return false;
  }

  return true;
}

bool ModemManager::httpGetTest(const char* url, uint16_t readLen) {
  return http_.get(url, readLen);
}

bool ModemManager::httpGet(const char* url, uint16_t readLen) {
  return http_.get(url, readLen);
}

bool ModemManager::isNetworkConnected() {
  return modem_.isNetworkConnected();
}

bool ModemManager::isGprsConnected() { return modem_.isGprsConnected(); }

bool ModemManager::hasDataSession() { return data_.hasDataSession(); }

bool ModemManager::ensureNetOpen() { return data_.ensureNetOpen(); }

String ModemManager::getCpsiInfo() { return core_.getCpsiInfo(); }

int16_t ModemManager::getSignalStrengthPercent() {
  return core_.getSignalStrengthPercent();
}

ModemInfo ModemManager::getNetworkInfo() { return core_.getNetworkInfo(); }

void ModemManager::logLine(const String& message) {
  if (!config_.enableLogs) {
    return;
  }
  config_.serialMon.println(message);
}

void ModemManager::logValue(const String& label, const String& value) {
  if (!config_.enableLogs) {
    return;
  }
  config_.serialMon.print(label);
  config_.serialMon.print(": ");
  config_.serialMon.println(value);
}

void ModemManager::logValue(const String& label, int value) {
  if (!config_.enableLogs) {
    return;
  }
  config_.serialMon.print(label);
  config_.serialMon.print(": ");
  config_.serialMon.println(value);
}

void ModemManager::setRxLoggingEnabled(bool enabled) {
#ifndef DUMP_AT_COMMANDS
  tapStream_.setRxLoggingEnabled(enabled);
#else
  (void)enabled;
#endif
}

void ModemManager::setTxLoggingEnabled(bool enabled) {
#ifndef DUMP_AT_COMMANDS
  tapStream_.setTxLoggingEnabled(enabled);
#else
  (void)enabled;
#endif
}

void ModemManager::setLastError(int code, const String& message) {
  lastError_.code = code;
  lastError_.message = message;
}

Stream& ModemManager::tapStream() { return tapStream_; }
