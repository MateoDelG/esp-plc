#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "log_buffer.h"

class ConsoleService {
public:
  ConsoleService();

  void begin();
  void update();
  void clear();

  static void logSink(const String& message);
  static ConsoleService* instance();

private:
  struct LogMessage {
    char text[LogBuffer::kLineMax];
  };

  static ConsoleService* instance_;

  static const size_t kQueueDepth = 64;

  WebSocketsServer ws_;
  QueueHandle_t queue_ = nullptr;
  LogBuffer buffer_;

  void onWsEvent_(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  void broadcastLine_(const char* line);
};
