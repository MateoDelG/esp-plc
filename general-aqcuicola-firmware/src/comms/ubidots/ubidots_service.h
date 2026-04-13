#pragma once

#include <Arduino.h>

#include "core/logger.h"
#include "models/console_message.h"
#include "models/telemetry_packet.h"
#include "modem_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

class UbidotsService {
 public:
  explicit UbidotsService(Logger& logger);

  bool begin();
  void update();

  bool publishTelemetry(const TelemetryPacket& data);
  bool publishBlowersState(uint8_t value);
  bool publishConsoleValue(uint16_t value);
  void setOtaMode(bool enabled);
  bool isOtaMode() const;
  void setOtaActive(bool active);
  bool isOtaActive() const;
  uint8_t drainIncoming(uint8_t maxMessages, uint32_t maxMs);
  bool hasPendingConsoleMessage() const;
  bool popConsoleMessage(ConsoleMessage& out);
  uint8_t pendingConsoleCount() const;
  void setRxPaused(bool paused);
  bool isConnected() const;
  ModemManager& modem();
  bool isConsoleSubscribed() const;
  bool isModemReady() const;
  bool isDataReady() const;
  bool lastPublishOk() const;

 private:
  bool ensureConnected();
  bool ensureSubscribed();
  void pushConsoleMessage(const String& topic, const String& payload);
  void handlePublishFailure(const char* label);
  void handleMqttConnectFailure();
  void resetAccqBackoff();
  bool lockMqtt(const char* label);
  void unlockMqtt();
  void checkUrcOverflow();
  static void rxTaskEntry(void* param);
  void rxTaskLoop();

  Logger& logger_;
  ModemManager modem_;
  volatile bool modemReady_ = false;
  volatile bool dataReady_ = false;
  volatile bool consoleSubscribed_ = false;
  volatile bool lastPublishOk_ = false;
  volatile bool mqttBusy_ = false;
  volatile bool otaMode_ = false;
  volatile bool otaActive_ = false;
  static constexpr uint8_t kConsoleQueueSize = 32;
  ConsoleMessage consoleQueue_[kConsoleQueueSize];
  uint8_t consoleHead_ = 0;
  uint8_t consoleTail_ = 0;
  uint8_t consoleCount_ = 0;
  uint32_t urcOverflowLast_ = 0;
  bool publishDisabled_ = false;
  bool publishDisabledLogged_ = false;
  uint32_t lastPollMs_ = 0;
  uint32_t lastConnectAttemptMs_ = 0;
  uint32_t connectBackoffMs_ = 0;
  uint8_t accqFailCount_ = 0;
  uint32_t accqFailStartMs_ = 0;
  TaskHandle_t modemTask_ = nullptr;
  TaskHandle_t rxTask_ = nullptr;
  volatile bool rxTaskActive_ = false;
  volatile bool rxPaused_ = false;
  SemaphoreHandle_t mqttMutex_ = nullptr;
  bool modemRestartPending_ = false;
  bool espRestartPending_ = false;
  uint32_t modemRestartStartMs_ = 0;

  static void modemTaskEntry(void* param);
  void modemTaskLoop();
};
