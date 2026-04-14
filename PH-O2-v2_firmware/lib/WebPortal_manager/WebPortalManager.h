#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>

#include "pumps_manager.h"
#include "level_sensors_manager.h"
#include "uart_manager.h"
#include "eeprom_manager.h"

class WebPortalManager {
public:
  using ActionHandler = bool (*)(const String& action, const String& value, String& outMessage);

  WebPortalManager(PumpsManager* pumps,
                   LevelSensorsManager* levels,
                   UartProto::UARTManager* uart,
                   ConfigStore* eeprom);

  bool begin();
  void loop();

  void log(const String& line);
  void clearLogs();

  void setActionHandler(ActionHandler handler);
  bool isStarted() const { return started_; }

private:
  void handleIndex_();
  void handleStatus_();
  void handleAction_();

  void sendJson_(JsonDocument& doc);

  WebServer server_;
  PumpsManager* pumps_ = nullptr;
  LevelSensorsManager* levels_ = nullptr;
  UartProto::UARTManager* uart_ = nullptr;
  ConfigStore* eeprom_ = nullptr;
  ActionHandler actionHandler_ = nullptr;

  bool started_ = false;
};
