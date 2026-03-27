#pragma once

#include <Arduino.h>

#include "comms/ubidots/ubidots_service.h"
#include "config/ota_modem_config.h"
#include "core/logger.h"

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
  static void taskEntry(void* param);
  void taskLoop();
  bool installFromModemFile(const char* modemPath, size_t size);
  void deleteModemFile(const char* modemPath);
  static void logSink(bool isTx, const String& line);

  Logger& logger_;
  ModemManager* modem_ = nullptr;
  UbidotsService* ubidots_ = nullptr;
  TaskHandle_t task_ = nullptr;
  bool busy_ = false;

  static OtaModemService* active_;
};
