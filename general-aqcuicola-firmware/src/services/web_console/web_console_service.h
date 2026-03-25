#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class WebConsoleService {
 public:
  WebConsoleService();

  void begin();
  void update();
  void broadcast(const char* line);
  void enqueue(const char* line);

  static void setActive(WebConsoleService* service);
  static void logSink(const char* line);

 private:
  void handleSocketEvent(uint8_t clientId, WStype_t type, uint8_t* payload,
                         size_t length);
  void sendBuffered(uint8_t clientId);
  void addToBuffer(const char* line);

  static constexpr uint16_t kHttpPort = 80;
  static constexpr uint16_t kWsPort = 81;
  static constexpr size_t kBufferSize = 200;
  static constexpr size_t kQueueDepth = 64;
  static constexpr size_t kMaxLineLen = 256;

  WebServer server_;
  WebSocketsServer ws_;
  String buffer_[kBufferSize];
  size_t bufferIndex_ = 0;
  size_t bufferCount_ = 0;
  QueueHandle_t logQueue_ = nullptr;

  static WebConsoleService* active_;
};
