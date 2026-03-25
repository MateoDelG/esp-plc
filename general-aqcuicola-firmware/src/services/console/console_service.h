#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "models/analog_snapshot.h"
#include "services/console/log_buffer.h"
#include "models/telemetry_packet.h"

class AnalogAcquisitionService;

class ConsoleService {
 public:
  ConsoleService();

  void begin();
  void update();
  void enqueue(const char* line);
  void setTelemetry(const TelemetryPacket& data);
  void setAnalogSnapshot(const AnalogSnapshot& snapshot);
  void setAnalogControl(AnalogAcquisitionService* service);

  static void setActive(ConsoleService* service);
  static void logSink(const char* line);

 private:
  void handleSocketEvent(uint8_t clientId, WStype_t type, uint8_t* payload,
                         size_t length);
  void sendBuffered(uint8_t clientId);

  static constexpr uint16_t kHttpPort = 80;
  static constexpr uint16_t kWsPort = 81;
  static constexpr size_t kQueueDepth = 64;
  static constexpr size_t kMaxLineLen = 256;

  WebServer server_;
  WebSocketsServer ws_;
  LogBuffer buffer_{200};
  QueueHandle_t logQueue_ = nullptr;
  TelemetryPacket latestTelemetry_;
  AnalogSnapshot latestAnalog_;
  AnalogAcquisitionService* analogService_ = nullptr;

  static ConsoleService* active_;
};
