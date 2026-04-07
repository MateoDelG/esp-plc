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
      at_(modem_, tapStream_, urc_, this),
      core_(*this),
      data_(*this),
      http_(*this),
      mqtt_(*this),
      ntp_(*this) {}

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

bool ModemManager::syncTimeWithNtp(const char* server, int tzQuarterHours,
                                   uint32_t timeoutMs, time_t& outEpoch,
                                   String* outClock) {
  if (!data_.ensureNetOpen()) {
    logWarn("ntp", "netopen not ready");
    return false;
  }
  return ntp_.sync(server, tzQuarterHours, timeoutMs, outEpoch, outClock);
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
  if (message.startsWith("[")) {
    int end = message.indexOf(']');
    if (end > 1) {
      String subsystem = message.substring(1, end);
      String rest = message.substring(end + 1);
      rest.trim();
      if (rest.length() > 0) {
        logInfo(subsystem.c_str(), rest);
        return;
      }
    }
  }
  logInfo("core", message);
}

void ModemManager::logValue(const String& label, const String& value) {
  logInfo("core", label + ": " + value);
}

void ModemManager::logValue(const String& label, int value) {
  logInfo("core", label + ": " + String(value));
}

void ModemManager::log(ModemLogLevel level, const char* subsystem,
                       const String& message) {
  if (!config_.enableLogs) {
    return;
  }
  String line = modemFormatLog(level, subsystem, message);
  config_.serialMon.println(line);
}

void ModemManager::logDebug(const char* subsystem, const String& message) {
  log(ModemLogLevel::Debug, subsystem, message);
}

void ModemManager::logInfo(const char* subsystem, const String& message) {
  log(ModemLogLevel::Info, subsystem, message);
}

void ModemManager::logWarn(const char* subsystem, const String& message) {
  log(ModemLogLevel::Warn, subsystem, message);
}

void ModemManager::logError(const char* subsystem, const String& message) {
  log(ModemLogLevel::Error, subsystem, message);
}

void ModemManager::idle(uint32_t ms) {
  delay(ms);
  yield();
}

void ModemManager::idleStep() { yield(); }

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

void ModemManager::setLastError(ModemErrorCode code, const String& message) {
  lastError_.errorCode = code;
  lastError_.code = modemErrorToInt(code);
  lastError_.message = message;
}

void ModemManager::setLastError(ModemErrorCode code) {
  setLastError(code, modemErrorToString(code));
}

void ModemManager::setLastError(int legacyCode, const String& message) {
  lastError_.errorCode = ModemErrorCode::InternalError;
  lastError_.code = legacyCode;
  lastError_.message = message;
}

void ModemManager::setState(ModemState state, const char* reason) {
  transitionTo(state, reason);
}

bool ModemManager::transitionTo(ModemState state, const char* reason) {
  if (state == state_) {
    if (reason && reason[0] != '\0') {
      logDebug("state", String(stateName(state_)) + " (no-op): " + reason);
      return true;
    }
    logWarn("state", String("ignored same-state transition: ") +
                       stateName(state_));
    return false;
  }

  if (state == ModemState::Error || state == ModemState::Recovery) {
    ModemState prev = state_;
    state_ = state;
    if (reason && reason[0] != '\0') {
      logDebug("state", String(stateName(prev)) + " -> " + stateName(state) +
                          ": " + reason);
    } else {
      logDebug("state",
               String(stateName(prev)) + " -> " + stateName(state));
    }
    return true;
  }

  if (!canTransitionTo(state)) {
    logWarn("state", String("invalid transition: ") + stateName(state_) +
                       " -> " + stateName(state));
    return false;
  }

  ModemState prev = state_;
  state_ = state;
  if (reason && reason[0] != '\0') {
    logDebug("state", String(stateName(prev)) + " -> " + stateName(state) +
                        ": " + reason);
  } else {
    logDebug("state", String(stateName(prev)) + " -> " + stateName(state));
  }
  return true;
}

bool ModemManager::canTransitionTo(ModemState state) const {
  switch (state_) {
    case ModemState::Off:
      return state == ModemState::PoweringOn;
    case ModemState::PoweringOn:
      return state == ModemState::Booting;
    case ModemState::Booting:
      return state == ModemState::Ready;
    case ModemState::Ready:
      return state == ModemState::WaitingNetwork;
    case ModemState::WaitingNetwork:
      return state == ModemState::Registered;
    case ModemState::Registered:
      return state == ModemState::DataSessionReady ||
             state == ModemState::NetOpenReady || state == ModemState::HttpBusy ||
             state == ModemState::MqttStarting;
    case ModemState::DataSessionReady:
      return state == ModemState::NetOpenReady || state == ModemState::HttpBusy ||
             state == ModemState::MqttStarting;
    case ModemState::NetOpenReady:
      return state == ModemState::HttpBusy || state == ModemState::MqttStarting ||
             state == ModemState::MqttStarted;
    case ModemState::HttpBusy:
      return state == ModemState::NetOpenReady;
    case ModemState::MqttStarting:
      return state == ModemState::MqttStarted;
    case ModemState::MqttStarted:
      return state == ModemState::MqttConnecting ||
             state == ModemState::MqttConnected || state == ModemState::HttpBusy;
    case ModemState::MqttConnecting:
      return state == ModemState::MqttConnected;
    case ModemState::MqttConnected:
      return state == ModemState::NetOpenReady || state == ModemState::MqttStarted ||
             state == ModemState::HttpBusy;
    case ModemState::Recovery:
      return state == ModemState::Ready || state == ModemState::Registered ||
             state == ModemState::NetOpenReady;
    case ModemState::Error:
      return state == ModemState::Recovery;
  }
  return false;
}

const char* ModemManager::stateName(ModemState state) const {
  switch (state) {
    case ModemState::Off:
      return "off";
    case ModemState::PoweringOn:
      return "powering_on";
    case ModemState::Booting:
      return "booting";
    case ModemState::Ready:
      return "ready";
    case ModemState::WaitingNetwork:
      return "waiting_network";
    case ModemState::Registered:
      return "registered";
    case ModemState::DataSessionReady:
      return "data_session_ready";
    case ModemState::NetOpenReady:
      return "netopen_ready";
    case ModemState::HttpBusy:
      return "http_busy";
    case ModemState::MqttStarting:
      return "mqtt_starting";
    case ModemState::MqttStarted:
      return "mqtt_started";
    case ModemState::MqttConnecting:
      return "mqtt_connecting";
    case ModemState::MqttConnected:
      return "mqtt_connected";
    case ModemState::Recovery:
      return "recovery";
    case ModemState::Error:
      return "error";
  }
  return "unknown";
}

bool ModemManager::isNetworkUsable() const {
  return state_ == ModemState::Registered ||
         state_ == ModemState::DataSessionReady ||
         state_ == ModemState::NetOpenReady ||
         state_ == ModemState::HttpBusy ||
         state_ == ModemState::MqttStarting ||
         state_ == ModemState::MqttStarted ||
         state_ == ModemState::MqttConnecting ||
         state_ == ModemState::MqttConnected;
}

bool ModemManager::isReadyForHttp() const {
  return state_ == ModemState::NetOpenReady ||
         state_ == ModemState::DataSessionReady ||
         state_ == ModemState::Registered;
}

bool ModemManager::isReadyForMqtt() const {
  return state_ == ModemState::NetOpenReady ||
         state_ == ModemState::DataSessionReady ||
         state_ == ModemState::Registered ||
         state_ == ModemState::MqttStarted;
}

bool ModemManager::fail(const char* subsystem, ModemErrorCode code,
                        const char* message, ModemState state) {
  String msg = message ? String(message) : String(modemErrorToString(code));
  setLastError(code, msg);
  transitionTo(state, msg.c_str());
  logError(subsystem, msg);
  return false;
}

bool ModemManager::fail(const char* subsystem, ModemErrorCode code,
                        const String& message, ModemState state) {
  setLastError(code, message);
  transitionTo(state, message.c_str());
  logError(subsystem, message);
  return false;
}

Stream& ModemManager::tapStream() { return tapStream_; }
