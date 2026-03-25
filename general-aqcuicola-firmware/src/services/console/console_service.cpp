#include "services/console/console_service.h"

ConsoleService* ConsoleService::active_ = nullptr;

ConsoleService::ConsoleService() : server_(kHttpPort), ws_(kWsPort) {}

void ConsoleService::begin() {
  if (!logQueue_) {
    logQueue_ = xQueueCreate(kQueueDepth, sizeof(char*));
  }

  server_.on("/", [this]() {
    const char* page =
      "<!doctype html><html lang='en'><head><meta charset='utf-8'/>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
      "<title>Device Console</title>"
      "<style>body{margin:0;background:#101214;color:#e7ecef;font-family:"
      "ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,'Liberation Mono',"
      "'Courier New',monospace;}header{padding:12px 16px;border-bottom:1px solid"
      "#242a2f;color:#9aa4ad;display:flex;justify-content:space-between;}"
      "#console{padding:12px 16px;white-space:pre-wrap;min-height:calc(100vh-52px);"
      "box-sizing:border-box;}#status{font-size:12px;}</style></head><body>"
      "<header><div>Console</div><div id='status'>connecting...</div></header>"
      "<div id='console'>Waiting for messages...</div>"
      "<script>"
      "var statusEl=document.getElementById('status');"
      "var consoleEl=document.getElementById('console');"
      "var ws=new WebSocket('ws://'+location.hostname+':81/');"
      "ws.onopen=function(){statusEl.textContent='connected';};"
      "ws.onerror=function(){statusEl.textContent='error';};"
      "ws.onclose=function(){statusEl.textContent='disconnected';};"
      "ws.onmessage=function(e){"
      "if(consoleEl.textContent==='Waiting for messages...'){consoleEl.textContent='';}"
      "consoleEl.textContent+=e.data+'\\n';"
      "window.scrollTo(0,document.body.scrollHeight);" 
      "};"
      "</script>"
      "</body></html>";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
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

void ConsoleService::update() {
  server_.handleClient();
  ws_.loop();

  if (!logQueue_) {
    return;
  }

  char* line = nullptr;
  while (xQueueReceive(logQueue_, &line, 0) == pdTRUE) {
    if (line) {
      buffer_.push(String(line));
      ws_.broadcastTXT(line);
      free(line);
    }
  }
}

void ConsoleService::enqueue(const char* line) {
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

void ConsoleService::setActive(ConsoleService* service) {
  active_ = service;
}

void ConsoleService::logSink(const char* line) {
  if (active_) {
    active_->enqueue(line);
  }
}

void ConsoleService::handleSocketEvent(uint8_t clientId, WStype_t type,
                                       uint8_t* payload, size_t length) {
  (void)payload;
  (void)length;
  if (type == WStype_CONNECTED) {
    sendBuffered(clientId);
  }
}

void ConsoleService::sendBuffered(uint8_t clientId) {
  size_t count = buffer_.size();
  for (size_t i = 0; i < count; ++i) {
    String line = buffer_.get(i);
    if (line.length() > 0) {
      ws_.sendTXT(clientId, line);
    }
  }
}
