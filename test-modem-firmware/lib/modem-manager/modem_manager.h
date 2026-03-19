#ifndef MODEM_MANAGER_H
#define MODEM_MANAGER_H

#include <Arduino.h>

#include "modem_manager_config.h"
#include <TinyGsmClient.h>

#include "modem_at.h"
#include "modem_core.h"
#include "modem_data_session.h"
#include "modem_http.h"
#include "modem_mqtt.h"
#include "modem_types.h"
#include "modem_urc.h"
#include "modem_tap_stream.h"

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
#endif

class ModemManager {
 public:
  explicit ModemManager(const ModemConfig& config);

  void setLogsEnabled(bool enabled);

  bool powerOn();
  bool powerOff();
  bool restart();

  bool begin();
  bool waitForNetwork();

  bool ensureDataSession();
  bool disconnectGprs();

  bool ping(const char* host, uint8_t count = 4, uint32_t timeoutMs = 1000);

  bool httpGetTest(const char* url, uint16_t readLen = 64);
  bool httpGet(const char* url, uint16_t readLen = 64);

  bool isNetworkConnected();
  bool isGprsConnected();

  bool hasDataSession();
  bool ensureNetOpen();

  String getCpsiInfo();
  int16_t getSignalStrengthPercent();
  ModemInfo getNetworkInfo();

  ModemCore& core() { return core_; }
  ModemDataSession& data() { return data_; }
  ModemHttp& http() { return http_; }
  ModemMqtt& mqtt() { return mqtt_; }

  ModemState state() const { return state_; }
  ModemError lastError() const { return lastError_; }

  // Compatibility wrappers (legacy names)
  bool connectGprs() { return ensureDataSession(); }
  bool openSocketService() { return ensureNetOpen(); }
  bool isDataReady() { return hasDataSession(); }
  bool httpGetNative(const char* url, uint16_t readLen = 64) {
    return httpGet(url, readLen);
  }

  void logLine(const String& message);
  void logValue(const String& label, const String& value);
  void logValue(const String& label, int value);

  void setRxLoggingEnabled(bool enabled);
  void setTxLoggingEnabled(bool enabled);

  void setLastError(int code, const String& message);
  void setState(ModemState state) { state_ = state; }

  // Internal accessors for modules
  ModemConfig& config() { return config_; }
  TinyGsm& tinyGsm() { return modem_; }
  AtClient& at() { return at_; }
  UrcStore& urc() { return urc_; }
  Stream& tapStream();

 private:
  ModemConfig config_;

#ifdef DUMP_AT_COMMANDS
  StreamDebugger tapStream_;
#else
  ModemTapStream tapStream_;
#endif

  TinyGsm modem_;
  UrcStore urc_;
  AtClient at_;

  ModemCore core_;
  ModemDataSession data_;
  ModemHttp http_;
  ModemMqtt mqtt_;

  ModemState state_ = ModemState::Off;
  ModemError lastError_;
};

#endif
