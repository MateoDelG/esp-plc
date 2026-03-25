// Centralized modem error model
#ifndef MODEM_ERROR_H
#define MODEM_ERROR_H

#include <Arduino.h>

enum class ModemErrorCode : uint16_t {
  None = 0,
  InvalidArgument = 10,
  ModemInitFailed = 20,
  NetworkUnavailable = 30,
  NetworkTimeout = 31,
  DataSessionFailed = 40,
  NetOpenFailed = 41,
  NetOpenTimeout = 42,
  HttpInitFailed = 60,
  HttpActionFailed = 61,
  HttpActionTimeout = 62,
  HttpReadFailed = 63,
  HttpFileCopyFailed = 64,
  HttpSslConfigFailed = 65,
  SdOpenFailed = 70,
  SdWriteFailed = 71,
  SdVerificationFailed = 72,
  SdRenameFailed = 73,
  MqttStartFailed = 80,
  MqttAcquireFailed = 81,
  MqttConnectFailed = 82,
  MqttPublishFailed = 83,
  MqttSubscribeFailed = 84,
  Timeout = 90,
  UnexpectedResponse = 91,
  InternalError = 99,
};

const char* modemErrorToString(ModemErrorCode code);
int modemErrorToInt(ModemErrorCode code);

#endif
