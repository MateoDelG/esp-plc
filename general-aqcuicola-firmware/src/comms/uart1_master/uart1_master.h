#pragma once

#include <Arduino.h>

#include <ArduinoJson.h>

#include "core/logger.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

class TelemetryService;
class SdLoggerService;

class Uart1Master {
 public:
  enum class Op : uint8_t {
    GetStatus = 0,
    GetLast,
    AutoMeasure
  };

  explicit Uart1Master(Logger& logger);

  void begin();
  bool enqueue(Op op);
  void setTelemetryService(TelemetryService* telemetry);
  void setSdLogger(SdLoggerService* logger);

 private:
  static void taskEntry(void* param);
  void taskLoop();

  bool sendCommand(Op op);
  bool readNdjsonLine(String& line, uint32_t timeoutMs);
  void logResponsePretty(const String& line);
  void logPrettyJson(const JsonDocument& doc, const char* prefix);
  void updateTelemetryFromDoc(const JsonDocument& doc);

  Logger& logger_;
  TelemetryService* telemetry_ = nullptr;
  SdLoggerService* sdLogger_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
};
