#include "comms/uart1_master/uart1_master.h"

#include <ArduinoJson.h>

namespace {
constexpr uint32_t kUartBaud = 115200;
constexpr uint8_t kUartRxPin = 19;
constexpr uint8_t kUartTxPin = 23;
constexpr size_t kMaxLineLen = 1024;
constexpr uint32_t kResponseTimeoutMs = 10000;
constexpr uint32_t kIdleDelayMs = 20;
constexpr size_t kQueueDepth = 4;
}

Uart1Master::Uart1Master(Logger& logger) : logger_(logger) {}

void Uart1Master::begin() {
  if (!queue_) {
    queue_ = xQueueCreate(kQueueDepth, sizeof(Op));
  }
  Serial1.begin(kUartBaud, SERIAL_8N1, kUartRxPin, kUartTxPin);
  if (!task_) {
    xTaskCreatePinnedToCore(taskEntry, "uart1Master", 4096, this, 1, &task_, 1);
  }
}

bool Uart1Master::enqueue(Op op) {
  if (!queue_) {
    return false;
  }
  return xQueueSend(queue_, &op, 0) == pdTRUE;
}

void Uart1Master::taskEntry(void* param) {
  auto* self = static_cast<Uart1Master*>(param);
  if (self) {
    self->taskLoop();
  }
  vTaskDelete(nullptr);
}

void Uart1Master::taskLoop() {
  for (;;) {
    Op op;
    if (xQueueReceive(queue_, &op, pdMS_TO_TICKS(200)) != pdTRUE) {
      vTaskDelay(pdMS_TO_TICKS(kIdleDelayMs));
      continue;
    }

    if (!sendCommand(op)) {
      logger_.warn("uart1: send failed");
      continue;
    }

    String line;
    if (!readNdjsonLine(line, kResponseTimeoutMs)) {
      logger_.warn("uart1: timeout");
      continue;
    }

    logResponsePretty(line);
  }
}

bool Uart1Master::sendCommand(Op op) {
  const char* opName = nullptr;
  switch (op) {
    case Op::GetStatus:
      opName = "get_status";
      break;
    case Op::GetLast:
      opName = "get_last";
      break;
    case Op::AutoMeasure:
      opName = "auto_measure";
      break;
    default:
      return false;
  }

  String payload;
  payload.reserve(48);
  payload += "{\"op\":\"";
  payload += opName;
  payload += "\"}\n";
  Serial1.print(payload);
  logger_.logf("uart1", "tx: %s", payload.c_str());
  return true;
}

bool Uart1Master::readNdjsonLine(String& line, uint32_t timeoutMs) {
  line = "";
  line.reserve(kMaxLineLen);
  uint32_t start = millis();
  bool started = false;
  while (millis() - start < timeoutMs) {
    while (Serial1.available()) {
      char c = static_cast<char>(Serial1.read());
      if (!started) {
        if (c == '{') {
          started = true;
          line += c;
        }
        continue;
      }
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        return line.length() > 0;
      }
      if (line.length() >= kMaxLineLen) {
        logger_.warn("uart1: line too long");
        return false;
      }
      line += c;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return false;
}

void Uart1Master::logResponsePretty(const String& line) {
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    logger_.logf("uart1", "rx raw: %s", line.c_str());
    logger_.logf("uart1", "rx json error: %s", err.c_str());
    return;
  }

  logPrettyJson(doc, "[UART] RX pretty: ");
}

void Uart1Master::logPrettyJson(const JsonDocument& doc, const char* prefix) {
  String pretty;
  pretty.reserve(512);
  serializeJsonPretty(doc, pretty);

  size_t start = 0;
  bool first = true;
  while (start <= pretty.length()) {
    int end = pretty.indexOf('\n', start);
    if (end < 0) {
      end = static_cast<int>(pretty.length());
    }
    String line = pretty.substring(start, end);
    if (first) {
      String message;
      message.reserve(strlen(prefix) + line.length() + 1);
      message += prefix;
      message += line;
      logger_.info(message.c_str());
      first = false;
    } else {
      logger_.info(line.c_str());
    }
    if (end >= static_cast<int>(pretty.length())) {
      break;
    }
    start = static_cast<size_t>(end + 1);
  }
}
