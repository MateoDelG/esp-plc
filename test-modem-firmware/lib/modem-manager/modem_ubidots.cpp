#include "modem_ubidots.h"

#include "modem_manager.h"
#include "modem_parsers.h"

static const char kUbidotsHost[] = "industrial.api.ubidots.com";
static const uint16_t kUbidotsPort = 8883;

ModemUbidots::ModemUbidots(ModemManager& modem) : modem_(modem) {}

bool ModemUbidots::connect(const char* token, const char* clientId) {
  if (!token || strlen(token) == 0) {
    return false;
  }

  modem_.logLine("[ubidots] mqtt connect");
  return modem_.mqtt().ensureConnected(kUbidotsHost, kUbidotsPort, clientId, 3,
                                       2000, token, token);
}

bool ModemUbidots::publishValue(const char* token, const char* deviceLabel,
                                const char* variableLabel, float value,
                                bool disconnectAfter) {
  if (!token || !deviceLabel || !variableLabel) {
    return false;
  }

  if (!connect(token)) {
    modem_.logLine("[ubidots] connect failed");
    return false;
  }

  String topic = String("/v1.6/devices/") + deviceLabel;
  String payload = String("{\"") + variableLabel + "\":" +
                   String(value, 2) + "}";

  modem_.logValue("[ubidots] topic", topic);
  modem_.logValue("[ubidots] payload", payload);

  bool ok = modem_.mqtt().publishJson(topic.c_str(), payload.c_str(), 1, false);

  if (ok) {
    modem_.logLine("[ubidots] publish ok");
  } else {
    modem_.logLine("[ubidots] publish failed");
  }

  if (disconnectAfter) {
    modem_.mqtt().disconnect();
  }

  return ok;
}

bool ModemUbidots::subscribeVariable(const char* deviceLabel,
                                     const char* variableLabel) {
  if (!deviceLabel || !variableLabel) {
    return false;
  }

  String topic = String("/v1.6/devices/") + deviceLabel + "/" +
                 variableLabel + "/lv";
  return modem_.mqtt().subscribeTopic(topic.c_str(), 1);
}

bool ModemUbidots::pollVariable(const char* deviceLabel,
                                const char* variableLabel, String& valueOut) {
  valueOut = "";
  String topic;
  String payload;
  if (!modem_.mqtt().pollIncoming(topic, payload)) {
    return false;
  }

  String device;
  String variable;
  if (!ModemParsers::parseUbidotsLvTopic(topic, device, variable)) {
    return false;
  }

  if (device != deviceLabel || variable != variableLabel) {
    return false;
  }

  valueOut = payload;
  modem_.logLine(String("[ubidots] ") + variableLabel + ": " + payload);
  return true;
}

bool ModemUbidots::pollVariableFloat(const char* deviceLabel,
                                     const char* variableLabel,
                                     float& valueOut) {
  valueOut = 0.0f;
  String payload;
  if (!pollVariable(deviceLabel, variableLabel, payload)) {
    return false;
  }

  return ModemParsers::parseFloatValue(payload, valueOut);
}
