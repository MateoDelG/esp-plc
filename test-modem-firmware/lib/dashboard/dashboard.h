#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include "log_buffer.h"

enum class DashboardSource {
  Usb,
  Modem,
};

class Dashboard {
 public:
  void begin(WebServer& server, WebSocketsServer& ws);
  void loop();
  void pushLine(DashboardSource src, const String& line);

 private:
  void handleDashboard();
  const char* sourceLabel(DashboardSource src) const;

  WebServer* server_ = nullptr;
  WebSocketsServer* ws_ = nullptr;
  LogBuffer modemBuffer_{100};
};

#endif
