#pragma once

#include <Arduino.h>

#include "core/logger.h"
#include "models/console_message.h"
#include "models/telemetry_packet.h"
#include "modem_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class UbidotsService {
 public:
  explicit UbidotsService(Logger& logger);

  bool begin();
  void update();

  bool publishTelemetry(const TelemetryPacket& data);
  bool isConnected() const;
  bool isConsoleSubscribed() const;
  bool isModemReady() const;
  bool isDataReady() const;
  bool lastPublishOk() const;

  bool hasNewConsoleMessage() const;
  const ConsoleMessage& latestConsoleMessage() const;
  void ackConsoleMessage();

 private:
  bool ensureConnected();
  bool ensureSubscribed();

  Logger& logger_;
  ModemManager modem_;
  ConsoleMessage consoleMessage_;
  volatile bool modemReady_ = false;
  volatile bool dataReady_ = false;
  volatile bool consoleSubscribed_ = false;
  volatile bool lastPublishOk_ = false;
  volatile bool mqttBusy_ = false;
  uint32_t lastPollMs_ = 0;
  uint32_t lastConnectAttemptMs_ = 0;
  TaskHandle_t modemTask_ = nullptr;

  static void modemTaskEntry(void* param);
  void modemTaskLoop();
};
