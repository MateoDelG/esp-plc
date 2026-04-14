#include "console_service.h"

#include <string.h>

ConsoleService* ConsoleService::instance_ = nullptr;

ConsoleService::ConsoleService() : ws_(81) {}

void ConsoleService::begin() {
  instance_ = this;

  if (!queue_) {
    queue_ = xQueueCreate(kQueueDepth, sizeof(LogMessage));
  }

  ws_.begin();
  ws_.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    onWsEvent_(num, type, payload, length);
  });
}

void ConsoleService::update() {
  ws_.loop();

  if (!queue_) return;

  LogMessage msg;
  while (xQueueReceive(queue_, &msg, 0) == pdTRUE) {
    buffer_.push(msg.text);
    broadcastLine_(msg.text);
  }
}

void ConsoleService::clear() {
  buffer_.clear();
  if (queue_) {
    xQueueReset(queue_);
  }
}

void ConsoleService::logSink(const String& message) {
  if (!instance_ || !instance_->queue_) return;

  LogMessage msg;
  strncpy(msg.text, message.c_str(), LogBuffer::kLineMax - 1);
  msg.text[LogBuffer::kLineMax - 1] = '\0';

  xQueueSend(instance_->queue_, &msg, 0);
}

ConsoleService* ConsoleService::instance() {
  return instance_;
}

void ConsoleService::onWsEvent_(uint8_t num, WStype_t type, uint8_t*, size_t) {
  if (type != WStype_CONNECTED) return;

  size_t count = buffer_.size();
  for (size_t i = 0; i < count; ++i) {
    const char* line = buffer_.get(i);
    if (line && line[0] != '\0') {
      ws_.sendTXT(num, line);
    }
  }
}

void ConsoleService::broadcastLine_(const char* line) {
  if (!line || line[0] == '\0') return;
  ws_.broadcastTXT(line);
}
