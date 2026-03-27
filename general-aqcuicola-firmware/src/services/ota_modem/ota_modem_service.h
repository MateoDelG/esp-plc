#pragma once

#include <Arduino.h>

#include "comms/ubidots/ubidots_service.h"
#include "config/ota_modem_config.h"
#include "core/logger.h"
#include "services/ota_manager/ota_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class OtaModemService {
 public:
  explicit OtaModemService(Logger& logger);

  void setModem(ModemManager* modem);
  void setUbidots(UbidotsService* ubidots);
  bool start();
  bool isBusy() const;

 private:
  enum class SdProbeMode : uint8_t { None = 0, Light, Stress };

  static void taskEntry(void* param);
  void taskLoop();

  bool initSdWithRetry(const char* context, SdProbeMode probeMode);
  bool sdWriteProbe(const char* path, uint32_t probeSize, const char* label);
  bool sdLightWriteProbe(const char* path);
  bool sdStressWriteProbe(const char* path);
  bool recoverSd();
  bool remountSd();
  void finalizeSdAfterOta();

  static void logSink(bool isTx, const String& line);
  static void onCopyToMemory(void* context);

  Logger& logger_;
  ModemManager* modem_ = nullptr;
  UbidotsService* ubidots_ = nullptr;
  OtaManager otaManager_;
  TaskHandle_t task_ = nullptr;
  bool busy_ = false;
  bool sdMounted_ = false;
  bool sdValidated_ = false;

  static OtaModemService* active_;
};
