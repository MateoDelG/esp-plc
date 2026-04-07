#include "services/console/console_service.h"

#include "services/console/pages/dashboard_page.h"
#include "services/console/pages/dashboard_style.h"
#include "services/console/pages/dashboard_script.h"
#include "services/console/pages/console_fragment.h"
#include "services/acquisition/analog_acquisition_service.h"
#include "config/dashboard_config.h"
#include "core/logger.h"
#include "services/telemetry/telemetry_service.h"
#include "services/time/time_service.h"
#include <SD.h>

#include <cmath>
#include <Wire.h>
#include <WiFi.h>
#include "services/espnow/espnow_service.h"
#include "comms/uart1_master/uart1_master.h"
#include "services/pcf_io/pcf_io_service.h"

namespace {
float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

uint16_t clampU16(int value, uint16_t minValue, uint16_t maxValue) {
  if (value < static_cast<int>(minValue)) {
    return minValue;
  }
  if (value > static_cast<int>(maxValue)) {
    return maxValue;
  }
  return static_cast<uint16_t>(value);
}

uint16_t clampU16Min(int value, uint16_t minValue) {
  if (value < static_cast<int>(minValue)) {
    return minValue;
  }
  return static_cast<uint16_t>(value);
}

uint8_t clampU8(int value, uint8_t minValue, uint8_t maxValue) {
  if (value < static_cast<int>(minValue)) {
    return minValue;
  }
  if (value > static_cast<int>(maxValue)) {
    return maxValue;
  }
  return static_cast<uint8_t>(value);
}

uint16_t clampU16Max(int value, uint16_t minValue, uint16_t maxValue) {
  if (value < static_cast<int>(minValue)) {
    return minValue;
  }
  if (value > static_cast<int>(maxValue)) {
    return maxValue;
  }
  return static_cast<uint16_t>(value);
}

bool readLastLines(const char* path, uint8_t maxLines, String& out) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  size_t size = file.size();
  if (size == 0) {
    file.close();
    return true;
  }
  String buffer;
  buffer.reserve(512);
  int linesFound = 0;
  size_t pos = size;
  while (pos > 0 && linesFound <= maxLines) {
    size_t chunk = pos > 128 ? 128 : pos;
    pos -= chunk;
    file.seek(pos);
    char temp[128];
    size_t readLen = file.readBytes(temp, chunk);
    for (int i = static_cast<int>(readLen) - 1; i >= 0; --i) {
      if (temp[i] == '\n') {
        linesFound++;
        if (linesFound > maxLines) {
          pos += static_cast<size_t>(i + 1);
          goto done;
        }
      }
    }
  }
done:
  file.seek(pos);
  buffer = file.readString();
  file.close();
  out = buffer;
  return true;
}

bool clearLogsDir() {
  if (!SD.exists("/logs")) {
    return SD.mkdir("/logs");
  }
  File dir = SD.open("/logs");
  if (!dir) {
    return false;
  }
  File entry = dir.openNextFile();
  while (entry) {
    const char* name = entry.name();
    entry.close();
    if (name && strlen(name) > 0) {
      SD.remove(name);
    }
    entry = dir.openNextFile();
  }
  dir.close();
  return true;
}
}  // namespace

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
    auto appendFloat = [&](const char* key, float value, bool trailingComma) {
      payload += "\"";
      payload += key;
      payload += "\":";
      if (std::isfinite(value)) {
        payload += String(value, 2);
      } else {
        payload += "null";
      }
      if (trailingComma) {
        payload += ",";
      }
    };

    appendFloat("phTank1", latestTelemetry_.phTank1, true);
    appendFloat("phTank2", latestTelemetry_.phTank2, true);
    appendFloat("o2Tank1", latestTelemetry_.o2Tank1, true);
    appendFloat("o2Tank2", latestTelemetry_.o2Tank2, true);
    appendFloat("tempTank1", latestTelemetry_.tempTank1, true);
    appendFloat("tempTank2", latestTelemetry_.tempTank2, true);
    appendFloat("levelTank1", latestTelemetry_.levelTank1, true);
    appendFloat("levelTank2", latestTelemetry_.levelTank2, true);
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
        mask = clampU8(parsed, 0, 15);
      }
    }

    latestAnalog_.enabledMask = mask;
    if (analogService_) {
      analogService_->setEnabledMask(mask);
    }
    if (dashboardConfig_) {
      if (dashboardConfig_->adsEnabledMask != mask) {
        dashboardConfig_->adsEnabledMask = mask;
        saveDashboardConfig(*dashboardConfig_, logger_);
      }
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
    payload += "\"alarmEnabled\":" + String(blowerAlarmEnabled_ ? "true" : "false");
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

    blowerThresholdA0_ = clampFloat(parseField("a0", blowerThresholdA0_), 0.1f, 1.0f);
    blowerThresholdA1_ = clampFloat(parseField("a1", blowerThresholdA1_), 0.1f, 1.0f);
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
        blowerDelaySec_ = clampU16(parsed, 1, 600);
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

    int idxAlarm = body.indexOf("alarmEnabled");
    if (idxAlarm >= 0) {
      int colon = body.indexOf(':', idxAlarm);
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
        blowerAlarmEnabled_ = parsed != 0;
      }
    }
    if (blowerAlarmEnabledRef_) {
      *blowerAlarmEnabledRef_ = blowerAlarmEnabled_;
    }

    if (dashboardConfig_) {
      bool changed = false;
      if (fabsf(dashboardConfig_->blowerThresholdA0 - blowerThresholdA0_) > 0.0001f) {
        dashboardConfig_->blowerThresholdA0 = blowerThresholdA0_;
        changed = true;
      }
      if (fabsf(dashboardConfig_->blowerThresholdA1 - blowerThresholdA1_) > 0.0001f) {
        dashboardConfig_->blowerThresholdA1 = blowerThresholdA1_;
        changed = true;
      }
      if (dashboardConfig_->blowerNotifyDelaySec != blowerDelaySec_) {
        dashboardConfig_->blowerNotifyDelaySec = blowerDelaySec_;
        changed = true;
      }
      if (dashboardConfig_->blowerAlarmEnabled != blowerAlarmEnabled_) {
        dashboardConfig_->blowerAlarmEnabled = blowerAlarmEnabled_;
        changed = true;
      }
      if (changed) {
        saveDashboardConfig(*dashboardConfig_, logger_);
      }
    }

    String payload;
    payload.reserve(128);
    payload += "{";
    payload += "\"a0\":" + String(blowerThresholdA0_, 3) + ",";
    payload += "\"a1\":" + String(blowerThresholdA1_, 3);
    payload += ",";
    payload += "\"delaySec\":" + String(blowerDelaySec_);
    payload += ",";
    payload += "\"alarmEnabled\":" + String(blowerAlarmEnabled_ ? "true" : "false");
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

  server_.on("/api/uart/auto", HTTP_GET, [this]() {
    String payload;
    payload.reserve(96);
    payload += "{\"ok\":true,\"enabled\":";
    payload += String(uartAutoEnabled_ ? "true" : "false");
    payload += ",\"intervalMin\":";
    uint32_t minutes = uartAutoIntervalMs_ / 60000U;
    if (minutes == 0) {
      minutes = 1;
    }
    payload += String(minutes);
    payload += "}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/uart/auto", HTTP_POST, [this]() {
    String body = server_.arg("plain");

    auto parseField = [&](const char* key, int current) -> int {
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
      return value.toInt();
    };

    int enabled = parseField("enabled", uartAutoEnabled_ ? 1 : 0);
    int currentIntervalMin = static_cast<int>(uartAutoIntervalMs_ / 60000U);
    if (currentIntervalMin < 1) {
      currentIntervalMin = 1;
    }
    int intervalMinRaw = parseField("intervalMin", currentIntervalMin);
    if (enabled != 0 && enabled != 1) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"BAD_ARG\"}");
      return;
    }
    uint16_t intervalMin = clampU16Min(intervalMinRaw, 1);

    uartAutoEnabled_ = enabled != 0;
    uartAutoIntervalMs_ = static_cast<uint32_t>(intervalMin) * 60000U;
    if (uartAutoEnabledRef_) {
      *uartAutoEnabledRef_ = uartAutoEnabled_;
    }
    if (uartAutoIntervalMsRef_) {
      *uartAutoIntervalMsRef_ = uartAutoIntervalMs_;
    }
    if (uartAutoLastMsRef_) {
      *uartAutoLastMsRef_ = uartAutoLastMs_;
    }

    if (dashboardConfig_) {
      bool changed = false;
      if (dashboardConfig_->uartAutoEnabled != uartAutoEnabled_) {
        dashboardConfig_->uartAutoEnabled = uartAutoEnabled_;
        changed = true;
      }
      if (dashboardConfig_->uartAutoIntervalMin != intervalMin) {
        dashboardConfig_->uartAutoIntervalMin = intervalMin;
        changed = true;
      }
      if (changed) {
        saveDashboardConfig(*dashboardConfig_, logger_);
      }
    }

    server_.send(200, "application/json", "{\"ok\":true}");
  });

  server_.on("/api/pcf/state", HTTP_GET, [this]() {
    if (!pcfIoService_) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"NO_PCF\"}");
      return;
    }

    uint8_t inputs[8] = {0};
    uint8_t outputs[8] = {0};
    bool inputOk = pcfIoService_->readInputs(inputs, 8);
    bool outputOk = pcfIoService_->getOutputs(outputs, 8);
    if (!inputOk || !outputOk) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"READ_FAIL\"}");
      return;
    }

    String payload;
    payload.reserve(128);
    payload += "{\"ok\":true,\"di\":[";
    for (uint8_t i = 0; i < 8; ++i) {
      payload += String(inputs[i]);
      if (i < 7) {
        payload += ",";
      }
    }
    payload += "],\"do\":[";
    for (uint8_t i = 0; i < 8; ++i) {
      payload += String(outputs[i]);
      if (i < 7) {
        payload += ",";
      }
    }
    payload += "]}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/pcf/do", HTTP_POST, [this]() {
    if (!pcfIoService_) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"NO_PCF\"}");
      return;
    }
    String body = server_.arg("plain");

    auto parseField = [&](const char* key, int current) -> int {
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
      return value.toInt();
    };

    int pin = parseField("pin", -1);
    int value = parseField("value", -1);
    if (pin < 0 || pin > 7 || (value != 0 && value != 1)) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"BAD_ARG\"}");
      return;
    }

    if (!pcfIoService_->setOutput(static_cast<uint8_t>(pin),
                                  static_cast<uint8_t>(value))) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"WRITE_FAIL\"}");
      return;
    }

    server_.send(200, "application/json", "{\"ok\":true}");
  });

  server_.on("/api/i2c/scan", HTTP_GET, [this]() {
    Wire.begin();
    String payload;
    payload.reserve(192);
    payload += "{\"ok\":true,\"devices\":[";
    bool first = true;
    for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
      Wire.beginTransmission(addr);
      uint8_t err = Wire.endTransmission();
      if (err == 0) {
        if (!first) {
          payload += ",";
        }
        payload += String(addr);
        first = false;
      }
    }
    payload += "]}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/espnow/config", HTTP_GET, [this]() {
    if (!espNowService_) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"NO_ESPNOW\"}");
      return;
    }
    String payload;
    payload.reserve(192);
    payload += "{\"ok\":true,\"tank1\":\"";
    payload += espNowService_->getTankMac(1);
    payload += "\",\"tank2\":\"";
    payload += espNowService_->getTankMac(2);
    payload += "\",\"self\":\"";
    payload += WiFi.macAddress();
    payload += "\",\"channel\":";
    payload += String(espNowService_->channel());
    payload += "}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/espnow/config", HTTP_POST, [this]() {
    if (!espNowService_) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"NO_ESPNOW\"}");
      return;
    }
    String body = server_.arg("plain");

    auto parseField = [&](const char* key, String current) -> String {
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
      return value;
    };

    int tank = parseField("tank", "").toInt();
    String mac = parseField("mac", "");
    if (tank < 1 || tank > 2 || mac.length() == 0) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"BAD_ARG\"}");
      return;
    }
    if (!espNowService_->setTankMac(static_cast<uint8_t>(tank), mac)) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"BAD_MAC\"}");
      return;
    }
    server_.send(200, "application/json", "{\"ok\":true}");
  });

  server_.on("/api/espnow/request", HTTP_POST, [this]() {
    if (!espNowService_) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"NO_ESPNOW\"}");
      return;
    }
    String body = server_.arg("plain");
    int idx = body.indexOf("tank");
    if (idx < 0) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"BAD_ARG\"}");
      return;
    }
    int colon = body.indexOf(':', idx);
    if (colon < 0) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"BAD_ARG\"}");
      return;
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
    int tank = value.toInt();
    if (tank < 1 || tank > 2) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"BAD_ARG\"}");
      return;
    }
    if (!espNowService_->requestTank(static_cast<uint8_t>(tank))) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"SEND_FAIL\"}");
      return;
    }
    server_.send(200, "application/json", "{\"ok\":true}");
  });

  server_.on("/api/espnow/auto", HTTP_GET, [this]() {
    String payload;
    payload.reserve(96);
    payload += "{\"ok\":true,\"enabled\":";
    payload += String(espNowAutoEnabled_ ? "true" : "false");
    payload += ",\"intervalMin\":";
    uint32_t minutes = espNowAutoIntervalMs_ / 60000U;
    if (minutes == 0) {
      minutes = 1;
    }
    payload += String(minutes);
    payload += "}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/espnow/auto", HTTP_POST, [this]() {
    String body = server_.arg("plain");

    auto parseField = [&](const char* key, int current) -> int {
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
      return value.toInt();
    };

    int enabled = parseField("enabled", espNowAutoEnabled_ ? 1 : 0);
    int currentIntervalMin = static_cast<int>(espNowAutoIntervalMs_ / 60000U);
    if (currentIntervalMin < 1) {
      currentIntervalMin = 1;
    }
    int intervalMinRaw = parseField("intervalMin", currentIntervalMin);
    if (enabled != 0 && enabled != 1) {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"BAD_ARG\"}");
      return;
    }
    uint16_t intervalMin = clampU16Min(intervalMinRaw, 1);

    espNowAutoEnabled_ = enabled != 0;
    espNowAutoIntervalMs_ = static_cast<uint32_t>(intervalMin) * 60000U;
    if (espNowAutoEnabledRef_) {
      *espNowAutoEnabledRef_ = espNowAutoEnabled_;
    }
    if (espNowAutoIntervalMsRef_) {
      *espNowAutoIntervalMsRef_ = espNowAutoIntervalMs_;
    }
    if (espNowAutoLastMsRef_) {
      *espNowAutoLastMsRef_ = espNowAutoLastMs_;
    }

    if (dashboardConfig_) {
      bool changed = false;
      if (dashboardConfig_->espNowAutoEnabled != espNowAutoEnabled_) {
        dashboardConfig_->espNowAutoEnabled = espNowAutoEnabled_;
        changed = true;
      }
      if (dashboardConfig_->espNowAutoIntervalMin != intervalMin) {
        dashboardConfig_->espNowAutoIntervalMin = intervalMin;
        changed = true;
      }
      if (changed) {
        saveDashboardConfig(*dashboardConfig_, logger_);
      }
    }

    server_.send(200, "application/json", "{\"ok\":true}");
  });

  server_.on("/api/ubidots/interval", HTTP_GET, [this]() {
    uint16_t minutes = 5;
    if (dashboardConfig_) {
      minutes = dashboardConfig_->ubidotsPublishIntervalMin;
    }
    String payload;
    payload.reserve(96);
    payload += "{\"ok\":true,\"intervalMin\":";
    payload += String(minutes);
    payload += "}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/ubidots/interval", HTTP_POST, [this]() {
    String body = server_.arg("plain");

    auto parseField = [&](const char* key, int current) -> int {
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
      return value.toInt();
    };

    uint16_t currentMin = 5;
    if (dashboardConfig_) {
      currentMin = dashboardConfig_->ubidotsPublishIntervalMin;
    }
    int intervalMinRaw = parseField("intervalMin", currentMin);
    uint16_t intervalMin = clampU16Max(intervalMinRaw, 1, 1440);

    if (dashboardConfig_) {
      if (dashboardConfig_->ubidotsPublishIntervalMin != intervalMin) {
        dashboardConfig_->ubidotsPublishIntervalMin = intervalMin;
        saveDashboardConfig(*dashboardConfig_, logger_);
      }
    }
    if (telemetryService_) {
      telemetryService_->setPublishIntervalMs(
        static_cast<uint32_t>(intervalMin) * 60000U);
    }

    String payload;
    payload.reserve(96);
    payload += "{\"ok\":true,\"intervalMin\":";
    payload += String(intervalMin);
    payload += "}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/time", HTTP_GET, [this]() {
    String payload;
    payload.reserve(160);
    payload += "{\"ok\":true,";
    if (timeService_) {
      payload += "\"synced\":" + String(timeService_->isSynced() ? "true" : "false") + ",";
      payload += "\"local\":\"" + timeService_->localTimeString() + "\",";
      payload += "\"lastSyncMs\":" + String(timeService_->lastSyncMs());
    } else {
      payload += "\"synced\":false,\"local\":\"--\",\"lastSyncMs\":0";
    }
    payload += "}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/sd/logs", HTTP_GET, [this]() {
    String type = server_.arg("type");
    String path;
    if (type == "uart") {
      path = "/logs/uart_ph_o2_temp.jsonl";
    } else if (type == "level") {
      path = "/logs/level_temp.jsonl";
    } else {
      server_.send(200, "application/json", "{\"ok\":false,\"error\":\"BAD_TYPE\"}");
      return;
    }

    String content;
    bool ok = readLastLines(path.c_str(), 10, content);
    String payload;
    payload.reserve(256);
    payload += "{\"ok\":";
    payload += ok ? "true" : "false";
    payload += ",\"lines\":[";
    if (ok && content.length() > 0) {
      int start = 0;
      int count = 0;
      while (start < content.length() && count < 10) {
        int end = content.indexOf('\n', start);
        if (end < 0) {
          end = content.length();
        }
        String line = content.substring(start, end);
        line.trim();
        if (line.length() > 0) {
          if (count > 0) {
            payload += ",";
          }
          payload += "\"";
          line.replace("\\", "\\\\");
          line.replace("\"", "\\\"");
          payload += line;
          payload += "\"";
          count++;
        }
        start = end + 1;
      }
    }
    payload += "]}";
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", payload);
  });

  server_.on("/api/sd/clear", HTTP_POST, [this]() {
    bool ok = clearLogsDir();
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
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

void ConsoleService::setBlowerAlarmRef(bool* enabled) {
  blowerAlarmEnabledRef_ = enabled;
  if (blowerAlarmEnabledRef_) {
    blowerAlarmEnabled_ = *blowerAlarmEnabledRef_;
  }
}

void ConsoleService::setUartMaster(Uart1Master* master) {
  uartMaster_ = master;
}

void ConsoleService::setPcfIoService(PcfIoService* service) {
  pcfIoService_ = service;
}

void ConsoleService::setEspNowService(EspNowService* service) {
  espNowService_ = service;
}

void ConsoleService::setTelemetryService(TelemetryService* service) {
  telemetryService_ = service;
}

void ConsoleService::setTimeService(TimeService* service) {
  timeService_ = service;
}


void ConsoleService::setUartAutoRefs(bool* enabled, uint32_t* intervalMs,
                                     uint32_t* lastMs) {
  uartAutoEnabledRef_ = enabled;
  uartAutoIntervalMsRef_ = intervalMs;
  uartAutoLastMsRef_ = lastMs;
  if (uartAutoEnabledRef_) {
    uartAutoEnabled_ = *uartAutoEnabledRef_;
  }
  if (uartAutoIntervalMsRef_) {
    uartAutoIntervalMs_ = *uartAutoIntervalMsRef_;
  }
  if (uartAutoLastMsRef_) {
    uartAutoLastMs_ = *uartAutoLastMsRef_;
  }
}

void ConsoleService::setEspNowAutoRefs(bool* enabled, uint32_t* intervalMs,
                                       uint32_t* lastMs) {
  espNowAutoEnabledRef_ = enabled;
  espNowAutoIntervalMsRef_ = intervalMs;
  espNowAutoLastMsRef_ = lastMs;
  if (espNowAutoEnabledRef_) {
    espNowAutoEnabled_ = *espNowAutoEnabledRef_;
  }
  if (espNowAutoIntervalMsRef_) {
    espNowAutoIntervalMs_ = *espNowAutoIntervalMsRef_;
  }
  if (espNowAutoLastMsRef_) {
    espNowAutoLastMs_ = *espNowAutoLastMsRef_;
  }
}

void ConsoleService::setLogger(Logger* logger) {
  logger_ = logger;
}

void ConsoleService::setDashboardConfig(DashboardConfig* config) {
  dashboardConfig_ = config;
}

void ConsoleService::setBlowerStatus(bool state, bool belowThreshold) {
  blowerState_ = state;
  blowerBelowThreshold_ = belowThreshold;
}

void ConsoleService::setActive(ConsoleService* service) {
  active_ = service;
}

TimeService* ConsoleService::activeTimeService() {
  if (!active_) {
    return nullptr;
  }
  return active_->timeService_;
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
