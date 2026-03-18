// Shared modem types and config
#ifndef MODEM_TYPES_H
#define MODEM_TYPES_H

#include <Arduino.h>
#include <Stream.h>

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

struct AtResult {
  bool ok = false;
  bool error = false;
  uint32_t timeoutMs = 0;
  String raw;
};

enum class ModemState : uint8_t {
  Off = 0,
  Booting,
  Ready,
  Registered,
  DataReady,
  MqttStarted,
  MqttConnected,
};

struct ModemError {
  int code = 0;
  String message;
};

#endif
