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
class Uart1Master;
class PcfIoService;
class EspNowService;
class Logger;
struct DashboardConfig;
class TelemetryService;

class ConsoleService {
 public:
  ConsoleService();

  void begin();
  void update();
  void enqueue(const char* line);
  void setTelemetry(const TelemetryPacket& data);
  void setAnalogSnapshot(const AnalogSnapshot& snapshot);
  void setAnalogControl(AnalogAcquisitionService* service);
  void setBlowerThresholdRefs(float* a0, float* a1);
  void setBlowerStatus(bool state, bool belowThreshold);
  void setBlowerDelayRef(uint16_t* seconds);
  void setBlowerAlarmRef(bool* enabled);
  void setUartMaster(Uart1Master* master);
  void setPcfIoService(PcfIoService* service);
  void setEspNowService(EspNowService* service);
  void setTelemetryService(TelemetryService* service);
  void setUartAutoRefs(bool* enabled, uint32_t* intervalMs, uint32_t* lastMs);
  void setEspNowAutoRefs(bool* enabled, uint32_t* intervalMs, uint32_t* lastMs);
  void setLogger(Logger* logger);
  void setDashboardConfig(DashboardConfig* config);

  static void setActive(ConsoleService* service);
  static void logSink(const char* line);

 private:
  void handleSocketEvent(uint8_t clientId, WStype_t type, uint8_t* payload,
                         size_t length);
  void sendBuffered(uint8_t clientId);

  static constexpr uint16_t kHttpPort = 80;
  static constexpr uint16_t kWsPort = 81;
  static constexpr size_t kQueueDepth = 64;
  static constexpr size_t kMaxLineLen = LogBuffer::kLineMax;

  WebServer server_;
  WebSocketsServer ws_;
  LogBuffer buffer_{200};
  QueueHandle_t logQueue_ = nullptr;
  TelemetryPacket latestTelemetry_;
  AnalogSnapshot latestAnalog_;
  AnalogAcquisitionService* analogService_ = nullptr;
  float blowerThresholdA0_ = 0.3f;
  float blowerThresholdA1_ = 0.3f;
  float* blowerThresholdA0Ref_ = nullptr;
  float* blowerThresholdA1Ref_ = nullptr;
  uint16_t blowerDelaySec_ = 10;
  uint16_t* blowerDelaySecRef_ = nullptr;
  bool blowerAlarmEnabled_ = false;
  bool* blowerAlarmEnabledRef_ = nullptr;
  bool blowerState_ = false;
  bool blowerBelowThreshold_ = false;
  Uart1Master* uartMaster_ = nullptr;
  PcfIoService* pcfIoService_ = nullptr;
  EspNowService* espNowService_ = nullptr;
  TelemetryService* telemetryService_ = nullptr;
  Logger* logger_ = nullptr;
  DashboardConfig* dashboardConfig_ = nullptr;
  bool uartAutoEnabled_ = false;
  bool* uartAutoEnabledRef_ = nullptr;
  uint32_t uartAutoIntervalMs_ = 5U * 60U * 1000U;
  uint32_t* uartAutoIntervalMsRef_ = nullptr;
  uint32_t uartAutoLastMs_ = 0;
  uint32_t* uartAutoLastMsRef_ = nullptr;
  bool espNowAutoEnabled_ = false;
  bool* espNowAutoEnabledRef_ = nullptr;
  uint32_t espNowAutoIntervalMs_ = 5U * 60U * 1000U;
  uint32_t* espNowAutoIntervalMsRef_ = nullptr;
  uint32_t espNowAutoLastMs_ = 0;
  uint32_t* espNowAutoLastMsRef_ = nullptr;

  static ConsoleService* active_;
};
