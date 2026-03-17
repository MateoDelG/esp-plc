#ifndef MODEM_MANAGER_H
#define MODEM_MANAGER_H

#include <Arduino.h>
#include <Stream.h>

#include "modem_manager_config.h"

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
#endif

#include <TinyGsmClient.h>

using ModemLogSink = void (*)(bool isTx, const String& line);

struct ModemPins {
  int8_t tx;
  int8_t rx;
  int8_t pwrKey;
  int8_t dtr;
};

struct ModemApnConfig {
  const char* apn;
  const char* user;
  const char* pass;
};

struct ModemConfig {
  HardwareSerial& serialAT;
  Stream& serialMon;
  ModemPins pins;
  ModemApnConfig apn;
  const char* simPin;
  uint32_t baud;
  uint32_t initDelayMs;
  uint32_t networkTimeoutMs;
  uint8_t networkRetries;
  bool enableLogs;
  ModemLogSink modemLogSink;
};

struct ModemInfo {
  String modemName;
  String modemInfo;
  String imei;
  String iccid;
  String operatorName;
  String localIp;
  String cpsi;
  int16_t signalQuality = 0;
  int16_t signalPercent = 0;
  int simStatus = 0;
  bool networkConnected = false;
  bool gprsConnected = false;
};

class TapStream : public Stream {
 public:
  TapStream(Stream& inner, ModemLogSink sink) : inner_(inner), sink_(sink) {}

  int available() override { return inner_.available(); }

  int read() override {
    int c = inner_.read();
    if (c >= 0) {
      rxBuffer_ += static_cast<char>(c);
      flushLines(false, rxBuffer_);
    }
    return c;
  }

  int peek() override { return inner_.peek(); }

  void flush() override { inner_.flush(); }

  size_t write(uint8_t c) override {
    txBuffer_ += static_cast<char>(c);
    flushLines(true, txBuffer_);
    return inner_.write(c);
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      txBuffer_ += static_cast<char>(buffer[i]);
    }
    flushLines(true, txBuffer_);
    return inner_.write(buffer, size);
  }

 private:
  void flushLines(bool isTx, String& buf) {
    if (!sink_) {
      return;
    }

    int newlineIndex = -1;
    while ((newlineIndex = buf.indexOf('\n')) >= 0) {
      String line = buf.substring(0, newlineIndex);
      line.replace("\r", "");
      sink_(isTx, line);
      buf.remove(0, newlineIndex + 1);
    }
  }

  Stream& inner_;
  ModemLogSink sink_;
  String txBuffer_;
  String rxBuffer_;
};

class ModemManager;

class ModemMqtt {
 public:
  explicit ModemMqtt(ModemManager& modem);

  bool start();
  bool acquire(const char* clientId);
  bool connectBroker(const char* host, uint16_t port,
                     const char* user = nullptr, const char* pass = nullptr);
  bool connect(const char* host, uint16_t port, const char* clientId,
               uint8_t retries = 3, uint32_t retryDelayMs = 2000,
               const char* user = nullptr, const char* pass = nullptr);
  bool publishJson(const char* topic, const char* json, int qos = 0,
                   bool retain = false);
  void disconnect();

  bool isConnected() const { return connected_; }

 private:
  bool sendPayload(const char* data, size_t length);

  ModemManager& modem_;
  bool started_ = false;
  bool acquired_ = false;
  bool connected_ = false;
};

class ModemManager {
 public:
  explicit ModemManager(const ModemConfig& config);

  void setLogsEnabled(bool enabled);

  bool powerOn();
  bool powerOff();
  bool restart();

  bool begin();
  bool waitForNetwork();

  bool connectGprs();
  bool disconnectGprs();

  bool ping(const char* host, uint8_t count = 4, uint32_t timeoutMs = 1000);

  bool httpGetTest(const char* url, uint16_t readLen = 64);
  bool httpGetNative(const char* url, uint16_t readLen = 64);

  bool isNetworkConnected();
  bool isGprsConnected();

  bool isNetOpenNow();
  bool waitNetOpen(uint32_t timeoutMs);
  bool openSocketService();
  bool isDataReady();

  String getCpsiInfo();
  int16_t getSignalStrengthPercent();
  ModemInfo getNetworkInfo();

  ModemMqtt& mqtt() { return mqtt_; }

 private:
  friend class ModemMqtt;

  void logLine(const String& message);
  void logValue(const String& label, const String& value);
  void logValue(const String& label, int value);

  ModemConfig config_;

#ifdef DUMP_AT_COMMANDS
  StreamDebugger tapStream_;
#else
  TapStream tapStream_;
#endif

  TinyGsm modem_;
  ModemMqtt mqtt_;
};

#endif  // MODEM_MANAGER_H
