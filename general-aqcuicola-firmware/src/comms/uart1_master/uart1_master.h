#pragma once

#include <Arduino.h>

#include <ArduinoJson.h>

#include "core/logger.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

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

 private:
  static void taskEntry(void* param);
  void taskLoop();

  bool sendCommand(Op op);
  bool readNdjsonLine(String& line, uint32_t timeoutMs);
  void logResponsePretty(const String& line);
  void logPrettyJson(const JsonDocument& doc, const char* prefix);

  Logger& logger_;
  QueueHandle_t queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
};
