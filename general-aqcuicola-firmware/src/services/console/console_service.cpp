#include "services/console/console_service.h"

#include "services/console/pages/dashboard_page.h"
#include "services/console/pages/dashboard_style.h"
#include "services/console/pages/dashboard_script.h"
#include "services/console/pages/console_fragment.h"
#include "services/acquisition/analog_acquisition_service.h"
#include "comms/uart1_master/uart1_master.h"

ConsoleService* ConsoleService::active_ = nullptr;

ConsoleService::ConsoleService() : server_(kHttpPort), ws_(kWsPort) {}

void ConsoleService::begin() {
  if (!logQueue_) {
    logQueue_ = xQueueCreate(kQueueDepth, sizeof(char*));
  }

  server_.on("/", [this]() {
    String page;
    page.reserve(16000);
    page += "<!doctype html><html lang='en'><head><meta charset='utf-8'/>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'/>";
    page += "<title>Aquaculture Dashboard</title>";
    page += kDashboardStyle;
    page += "</head><body>";
    page += kDashboardPage;
    page += kConsoleFragment;
    page += kDashboardScript;
    page += "</body></html>";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "text/html", page);
  });

  server_.on("/api/dashboard", HTTP_GET, [this]() {
    String payload;
    payload.reserve(256);
    payload += "{";
    payload += "\"phTank1\":" + String(latestTelemetry_.phTank1, 2) + ",";
    payload += "\"phTank2\":" + String(latestTelemetry_.phTank2, 2) + ",";
    payload += "\"o2Tank1\":" + String(latestTelemetry_.o2Tank1, 2) + ",";
    payload += "\"o2Tank2\":" + String(latestTelemetry_.o2Tank2, 2) + ",";
    payload += "\"tempTank1\":" + String(latestTelemetry_.tempTank1, 2) + ",";
    payload += "\"tempTank2\":" + String(latestTelemetry_.tempTank2, 2) + ",";
    payload += "\"levelTank1\":" + String(latestTelemetry_.levelTank1, 2) + ",";
    payload += "\"levelTank2\":" + String(latestTelemetry_.levelTank2, 2) + ",";
    payload += "\"stateBlowers\":" + String(latestTelemetry_.stateBlowers ? "true" : "false") + ",";
    payload += "\"timestampMs\":" + String(millis());
    payload += "}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/analog", HTTP_GET, [this]() {
    String payload;
    payload.reserve(256);
    payload += "{";
    payload += "\"enabledMask\":" + String(latestAnalog_.enabledMask) + ",";
    payload += "\"channels\":[";
    for (uint8_t i = 0; i < 4; ++i) {
      const AnalogChannelReading& ch = latestAnalog_.channels[i];
      payload += "{";
      payload += "\"ch\":" + String(ch.channel) + ",";
      payload += "\"raw\":" + String(ch.raw) + ",";
      payload += "\"volts\":" + String(ch.volts, 3) + ",";
      payload += "\"valid\":" + String(ch.valid ? "true" : "false") + ",";
      payload += "\"ts\":" + String(ch.timestampMs);
      payload += "}";
      if (i < 3) {
        payload += ",";
      }
    }
    payload += "]}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/analog/enable", HTTP_POST, [this]() {
    String body = server_.arg("plain");
    int idx = body.indexOf("enabledMask");
    uint8_t mask = latestAnalog_.enabledMask;
    if (idx >= 0) {
      int colon = body.indexOf(':', idx);
      if (colon >= 0) {
        int end = body.indexOf('}', colon);
        if (end < 0) {
          end = body.length();
        }
        String value = body.substring(colon + 1, end);
        value.replace("\"", "");
        value.trim();
        int parsed = value.toInt();
        if (parsed >= 0 && parsed <= 15) {
          mask = static_cast<uint8_t>(parsed);
        }
      }
    }

    latestAnalog_.enabledMask = mask;
    if (analogService_) {
      analogService_->setEnabledMask(mask);
    }
    String payload;
    payload.reserve(128);
    payload += "{";
    payload += "\"enabledMask\":" + String(mask);
    payload += "}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/blowers", HTTP_GET, [this]() {
    bool a0Enabled = (latestAnalog_.enabledMask & 0x01) != 0;
    bool a1Enabled = (latestAnalog_.enabledMask & 0x02) != 0;
    const AnalogChannelReading& ch0 = latestAnalog_.channels[0];
    const AnalogChannelReading& ch1 = latestAnalog_.channels[1];

    bool a0Valid = a0Enabled && ch0.valid;
    bool a1Valid = a1Enabled && ch1.valid;

    String payload;
    payload.reserve(192);
    payload += "{";
    payload += "\"a0\":{";
    payload += "\"volts\":" + String(a0Valid ? ch0.volts : 0.0f, 3) + ",";
    payload += "\"threshold\":" + String(blowerThresholdA0_, 3) + ",";
    payload += "\"valid\":" + String(a0Valid ? "true" : "false");
    payload += "},";
    payload += "\"a1\":{";
    payload += "\"volts\":" + String(a1Valid ? ch1.volts : 0.0f, 3) + ",";
    payload += "\"threshold\":" + String(blowerThresholdA1_, 3) + ",";
    payload += "\"valid\":" + String(a1Valid ? "true" : "false");
    payload += "},";
    payload += "\"state\":" + String(blowerState_ ? "true" : "false") + ",";
    payload += "\"belowThreshold\":" + String(blowerBelowThreshold_ ? "true" : "false");
    payload += ",";
    payload += "\"delaySec\":" + String(blowerDelaySec_);
    payload += "}";

    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/blowers", HTTP_POST, [this]() {
    String body = server_.arg("plain");

    auto parseField = [&](const char* key, float current) -> float {
      int idx = body.indexOf(key);
      if (idx < 0) {
        return current;
      }
      int colon = body.indexOf(':', idx);
      if (colon < 0) {
        return current;
      }
      int end = body.indexOf(',', colon);
      if (end < 0) {
        end = body.indexOf('}', colon);
      }
      if (end < 0) {
        end = body.length();
      }
      String value = body.substring(colon + 1, end);
      value.replace("\"", "");
      value.trim();
      return value.toFloat();
    };

    blowerThresholdA0_ = parseField("a0", blowerThresholdA0_);
    blowerThresholdA1_ = parseField("a1", blowerThresholdA1_);
    int idxDelay = body.indexOf("delaySec");
    if (idxDelay >= 0) {
      int colon = body.indexOf(':', idxDelay);
      if (colon >= 0) {
        int end = body.indexOf(',', colon);
        if (end < 0) {
          end = body.indexOf('}', colon);
        }
        if (end < 0) {
          end = body.length();
        }
        String value = body.substring(colon + 1, end);
        value.replace("\"", "");
        value.trim();
        int parsed = value.toInt();
        if (parsed >= 1 && parsed <= 600) {
          blowerDelaySec_ = static_cast<uint16_t>(parsed);
        }
      }
    }

    if (blowerThresholdA0Ref_) {
      *blowerThresholdA0Ref_ = blowerThresholdA0_;
    }
    if (blowerThresholdA1Ref_) {
      *blowerThresholdA1Ref_ = blowerThresholdA1_;
    }
    if (blowerDelaySecRef_) {
      *blowerDelaySecRef_ = blowerDelaySec_;
    }

    String payload;
    payload.reserve(128);
    payload += "{";
    payload += "\"a0\":" + String(blowerThresholdA0_, 3) + ",";
    payload += "\"a1\":" + String(blowerThresholdA1_, 3);
    payload += ",";
    payload += "\"delaySec\":" + String(blowerDelaySec_);
    payload += "}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/uart/cmd", HTTP_POST, [this]() {
    String op = server_.arg("op");
    op.trim();
    if (!uartMaster_) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"NO_UART\"}");
      return;
    }
    Uart1Master::Op cmd;
    if (op == "get_status") {
      cmd = Uart1Master::Op::GetStatus;
    } else if (op == "get_last") {
      cmd = Uart1Master::Op::GetLast;
    } else if (op == "auto_measure") {
      cmd = Uart1Master::Op::AutoMeasure;
    } else {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"BAD_OP\"}");
      return;
    }

    bool queued = uartMaster_->enqueue(cmd);
    if (!queued) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"BUSY\"}");
      return;
    }
    server_.send(200, "application/json", "{\"ok\":true}");
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

void ConsoleService::setTelemetry(const TelemetryPacket& data) {
  latestTelemetry_ = data;
}

void ConsoleService::setAnalogSnapshot(const AnalogSnapshot& snapshot) {
  latestAnalog_ = snapshot;
}

void ConsoleService::setAnalogControl(AnalogAcquisitionService* service) {
  analogService_ = service;
}

void ConsoleService::setBlowerThresholdRefs(float* a0, float* a1) {
  blowerThresholdA0Ref_ = a0;
  blowerThresholdA1Ref_ = a1;
  if (blowerThresholdA0Ref_) {
    blowerThresholdA0_ = *blowerThresholdA0Ref_;
  }
  if (blowerThresholdA1Ref_) {
    blowerThresholdA1_ = *blowerThresholdA1Ref_;
  }
}

void ConsoleService::setBlowerDelayRef(uint16_t* seconds) {
  blowerDelaySecRef_ = seconds;
  if (blowerDelaySecRef_) {
    blowerDelaySec_ = *blowerDelaySecRef_;
  }
}

void ConsoleService::setUartMaster(Uart1Master* master) {
  uartMaster_ = master;
}

void ConsoleService::setBlowerStatus(bool state, bool belowThreshold) {
  blowerState_ = state;
  blowerBelowThreshold_ = belowThreshold;
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
