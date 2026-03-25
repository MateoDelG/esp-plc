#include "modem_error.h"

const char* modemErrorToString(ModemErrorCode code) {
  switch (code) {
    case ModemErrorCode::None:
      return "none";
    case ModemErrorCode::InvalidArgument:
      return "invalid argument";
    case ModemErrorCode::ModemInitFailed:
      return "modem init failed";
    case ModemErrorCode::NetworkUnavailable:
      return "network unavailable";
    case ModemErrorCode::NetworkTimeout:
      return "network timeout";
    case ModemErrorCode::DataSessionFailed:
      return "data session failed";
    case ModemErrorCode::NetOpenFailed:
      return "netopen failed";
    case ModemErrorCode::NetOpenTimeout:
      return "netopen timeout";
    case ModemErrorCode::HttpInitFailed:
      return "http init failed";
    case ModemErrorCode::HttpActionFailed:
      return "http action failed";
    case ModemErrorCode::HttpActionTimeout:
      return "http action timeout";
    case ModemErrorCode::HttpReadFailed:
      return "http read failed";
    case ModemErrorCode::HttpFileCopyFailed:
      return "http file copy failed";
    case ModemErrorCode::HttpSslConfigFailed:
      return "http ssl config failed";
    case ModemErrorCode::SdOpenFailed:
      return "sd open failed";
    case ModemErrorCode::SdWriteFailed:
      return "sd write failed";
    case ModemErrorCode::SdVerificationFailed:
      return "sd verification failed";
    case ModemErrorCode::SdRenameFailed:
      return "sd rename failed";
    case ModemErrorCode::MqttStartFailed:
      return "mqtt start failed";
    case ModemErrorCode::MqttAcquireFailed:
      return "mqtt acquire failed";
    case ModemErrorCode::MqttConnectFailed:
      return "mqtt connect failed";
    case ModemErrorCode::MqttPublishFailed:
      return "mqtt publish failed";
    case ModemErrorCode::MqttSubscribeFailed:
      return "mqtt subscribe failed";
    case ModemErrorCode::Timeout:
      return "timeout";
    case ModemErrorCode::UnexpectedResponse:
      return "unexpected response";
    case ModemErrorCode::InternalError:
      return "internal error";
  }
  return "unknown error";
}

int modemErrorToInt(ModemErrorCode code) {
  return static_cast<int>(code);
}
