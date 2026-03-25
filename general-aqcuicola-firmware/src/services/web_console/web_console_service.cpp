#include "services/web_console/web_console_service.h"

WebConsoleService* WebConsoleService::active_ = nullptr;

WebConsoleService::WebConsoleService() : server_(kHttpPort), ws_(kWsPort) {}

void WebConsoleService::begin() {
  if (!logQueue_) {
    logQueue_ = xQueueCreate(kQueueDepth, sizeof(char*));
  }

  server_.on("/", [this]() {
    const char* page =
      "<!DOCTYPE html><html><head><meta charset='utf-8'/>"
      "<title>ESP Console</title>"
      "<style>body{font-family:monospace;background:#111;color:#ddd;margin:0;}"
      "#log{white-space:pre-wrap;padding:12px;height:100vh;box-sizing:border-box;"
      "overflow:auto;}button{position:fixed;top:10px;right:10px;}</style></head>"
      "<body><button onclick='clearLog()'>Clear</button><div id='log'></div>"
      "<script>"
      "const logEl=document.getElementById('log');"
      "const ws=new WebSocket('ws://'+location.hostname+':81/');"
      "ws.onmessage=(e)=>{logEl.textContent+=e.data+'\n';logEl.scrollTop=logEl.scrollHeight;};"
      "function clearLog(){logEl.textContent='';}"
      "</script></body></html>";
    server_.send(200, "text/html", page);
  });

  server_.begin();

  ws_.begin();
  ws_.onEvent([this](uint8_t clientId, WStype_t type, uint8_t* payload,
                     size_t length) {
    (void)payload;
    (void)length;
    handleSocketEvent(clientId, type, payload, length);
  });
}

void WebConsoleService::update() {
  server_.handleClient();
  ws_.loop();

  if (!logQueue_) {
    return;
  }

  char* line = nullptr;
  while (xQueueReceive(logQueue_, &line, 0) == pdTRUE) {
    if (line) {
      broadcast(line);
      free(line);
    }
  }
}

void WebConsoleService::broadcast(const char* line) {
  addToBuffer(line);
  ws_.broadcastTXT(line);
}

void WebConsoleService::enqueue(const char* line) {
  if (!logQueue_ || !line) {
    return;
  }

  size_t len = strnlen(line, kMaxLineLen);
  char* copy = static_cast<char*>(malloc(len + 1));
  if (!copy) {
    return;
  }
  memcpy(copy, line, len);
  copy[len] = '\0';

  if (xQueueSend(logQueue_, &copy, 0) != pdTRUE) {
    free(copy);
  }
}

void WebConsoleService::setActive(WebConsoleService* service) {
  active_ = service;
}

void WebConsoleService::logSink(const char* line) {
  if (active_) {
    active_->enqueue(line);
  }
}

void WebConsoleService::handleSocketEvent(uint8_t clientId, WStype_t type,
                                          uint8_t* payload, size_t length) {
  (void)payload;
  (void)length;
  if (type == WStype_CONNECTED) {
    sendBuffered(clientId);
  }
}

void WebConsoleService::sendBuffered(uint8_t clientId) {
  if (bufferCount_ == 0) {
    return;
  }

  size_t start = bufferCount_ < kBufferSize ? 0 : bufferIndex_;
  size_t count = bufferCount_ < kBufferSize ? bufferCount_ : kBufferSize;

  for (size_t i = 0; i < count; ++i) {
    size_t idx = (start + i) % kBufferSize;
    if (buffer_[idx].length() > 0) {
      ws_.sendTXT(clientId, buffer_[idx]);
    }
  }
}

void WebConsoleService::addToBuffer(const char* line) {
  buffer_[bufferIndex_] = line;
  bufferIndex_ = (bufferIndex_ + 1) % kBufferSize;
  if (bufferCount_ < kBufferSize) {
    bufferCount_++;
  }
}
