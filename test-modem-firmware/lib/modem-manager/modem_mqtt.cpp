#include "modem_mqtt.h"

#include "modem_manager.h"
#include "modem_parsers.h"

ModemMqtt::ModemMqtt(ModemManager& modem) : modem_(modem) {}

static bool isMqttOkCode(int code) {
  return code == 0 || code == 11 || code == 14 || code == 19 || code == 20;
}

void ModemMqtt::resetService() {
  String line;

  modem_.at().exec(3000L, GF("+CMQTTDISC=0,60"));
  if (modem_.at().waitUrc(UrcType::MqttDisc, 3000UL, line)) {
    int code = -1;
    if (ModemParsers::parseMqttResultCode(line, "+CMQTTDISC:", code) &&
        !isMqttOkCode(code)) {
      modem_.logValue("[mqtt] disc urc", line);
    }
  }

  modem_.at().exec(3000L, GF("+CMQTTREL=0"));
  if (modem_.at().waitUrc(UrcType::MqttRel, 3000UL, line)) {
    int code = -1;
    if (ModemParsers::parseMqttResultCode(line, "+CMQTTREL:", code) &&
        !isMqttOkCode(code)) {
      modem_.logValue("[mqtt] rel urc", line);
    }
  }

  modem_.at().exec(3000L, GF("+CMQTTSTOP"));
  if (modem_.at().waitUrc(UrcType::MqttStop, 6000UL, line)) {
    int code = -1;
    if (ModemParsers::parseMqttResultCode(line, "+CMQTTSTOP:", code) &&
        !isMqttOkCode(code)) {
      modem_.logValue("[mqtt] stop urc", line);
    }
  }

  while (modem_.urc().pop(UrcType::MqttStop, line)) {
  }
  while (modem_.urc().pop(UrcType::MqttRel, line)) {
  }
  while (modem_.urc().pop(UrcType::MqttDisc, line)) {
  }

  connected_ = false;
  acquired_ = false;
  started_ = false;
  tlsConfigured_ = false;
  delay(1500);
}

bool ModemMqtt::ensureStarted() {
  if (started_) {
    return true;
  }

  bool didReset = needsReset_;
  if (needsReset_) {
    resetService();
    needsReset_ = false;
  }

  String drain;
  while (modem_.urc().pop(UrcType::MqttStop, drain)) {
  }
  while (modem_.urc().pop(UrcType::MqttRel, drain)) {
  }
  while (modem_.urc().pop(UrcType::MqttDisc, drain)) {
  }

  if (!didReset) {
    modem_.at().exec(3000L, GF("+CMQTTSTOP"));
    String stopLine;
    modem_.at().waitUrc(UrcType::MqttStop, 5000UL, stopLine);
    delay(1200);
  }

  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    modem_.at().exec(10000L, GF("+CMQTTSTART"));

    String urcLine;
    if (modem_.at().waitUrc(UrcType::MqttStart, 10000UL, urcLine)) {
      int code = -1;
      int colon = urcLine.indexOf(':');
      if (colon >= 0) {
        String codeStr = urcLine.substring(colon + 1);
        codeStr.trim();
        code = codeStr.toInt();
      }
      if (code == 0 || code == 23) {
        started_ = true;
        modem_.setState(ModemState::MqttStarted);
        return true;
      }
    }

    resetService();
  }

  modem_.logLine("[mqtt] start urc timeout");
  modem_.setLastError(-40, "mqtt start failed");
  return false;
}

bool ModemMqtt::acquire(const char* clientId) {
  if (!clientId || strlen(clientId) == 0) {
    return false;
  }
  if (acquired_) {
    return true;
  }

  AtResult res = modem_.at().exec(10000L, GF("+CMQTTACCQ=0,\""), clientId,
                                  GF("\",1"));
  if (res.ok || res.raw.indexOf("+CMQTTACCQ: 0,0") >= 0) {
    acquired_ = true;
    return true;
  }

  modem_.logValue("[mqtt] accq raw", res.raw);
  modem_.setLastError(-41, "mqtt accq failed");
  return false;
}

bool ModemMqtt::connect(const char* host, uint16_t port, const char* clientId,
                        const char* user, const char* pass, bool useTls) {
  if (!host || strlen(host) == 0 || !clientId || strlen(clientId) == 0) {
    return false;
  }
  if (connected_) {
    return true;
  }

  if (!ensureStarted()) {
    modem_.logLine("[mqtt] start failed");
    return false;
  }

  if (!acquire(clientId)) {
    modem_.logLine("[mqtt] acquire failed");
    return false;
  }

  return connectBroker(host, port, user, pass, useTls);
}

bool ModemMqtt::connectBroker(const char* host, uint16_t port,
                              const char* user, const char* pass,
                              bool useTls) {
  if (!host || strlen(host) == 0) {
    return false;
  }
  if (connected_) {
    return true;
  }

  if (!modem_.data().ensureDataSession()) {
    modem_.logLine("[mqtt] data not ready");
    return false;
  }

  if (!modem_.data().ensureNetOpen()) {
    modem_.logLine("[mqtt] netopen not ready");
    return false;
  }

  if (useTls && !tlsConfigured_) {
    modem_.at().exec(3000L, GF("+CSSLCFG=\"sslversion\",0,4"));
    modem_.at().exec(3000L, GF("+CSSLCFG=\"authmode\",0,0"));
    modem_.at().exec(3000L, GF("+CSSLCFG=\"enableSNI\",0,1"));
    modem_.at().exec(3000L, GF("+CMQTTSSLCFG=0,0"));
    tlsConfigured_ = true;
  }

  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    if (user && pass && strlen(user) > 0) {
      modem_.at().exec(15000L, GF("+CMQTTCONNECT=0,\"tcp://"), host, GF(":"),
                       port, GF("\",60,1,\""), user, GF("\",\""), pass,
                       GF("\""));
    } else {
      modem_.at().exec(15000L, GF("+CMQTTCONNECT=0,\"tcp://"), host, GF(":"),
                       port, GF("\",60,1"));
    }

    String urcLine;
    if (modem_.at().waitUrc(UrcType::MqttConnect, 15000UL, urcLine)) {
      int code = -1;
      if (ModemParsers::parseMqttResultCode(urcLine, "+CMQTTCONNECT:", code) &&
          code == 0) {
        connected_ = true;
        modem_.setState(ModemState::MqttConnected);
        return true;
      }
    }
  }

  modem_.logLine("[mqtt] connect urc timeout");
  modem_.setLastError(-42, "mqtt connect failed");
  connected_ = false;
  return false;
}

bool ModemMqtt::ensureConnected(const char* host, uint16_t port,
                                const char* clientId, uint8_t retries,
                                uint32_t retryDelayMs, const char* user,
                                const char* pass, bool useTls) {
  if (!host || strlen(host) == 0) {
    return false;
  }
  if (connected_) {
    return true;
  }

  if (!ensureStarted()) {
    modem_.logLine("[mqtt] start failed");
    return false;
  }

  if (!acquire(clientId)) {
    modem_.logLine("[mqtt] acquire failed");
    return false;
  }

  for (uint8_t attempt = 0; attempt < retries; ++attempt) {
    modem_.logLine(String("[mqtt] broker connect attempt ") +
                   (attempt + 1) + "/" + retries);

    if (connectBroker(host, port, user, pass, useTls)) {
      return true;
    }

    if (connected_) {
      return true;
    }

    delay(retryDelayMs);
  }

  return false;
}

bool ModemMqtt::publish(const char* topic, const char* payload, int qos,
                        bool retain) {
  if (!connected_ || !topic || !payload) {
    return false;
  }

  size_t topicLen = strlen(topic);
  size_t payloadLen = strlen(payload);

  if (!modem_.at().execPromptedData(5000L, topic, topicLen,
                                    GF("+CMQTTTOPIC=0,"), topicLen)) {
    modem_.logLine("[mqtt] topic payload failed");
    return false;
  }

  delay(30);

  if (!modem_.at().execPromptedData(5000L, payload, payloadLen,
                                    GF("+CMQTTPAYLOAD=0,"), payloadLen)) {
    modem_.logLine("[mqtt] message payload failed");
    return false;
  }

  delay(30);

  int mqttQos = constrain(qos, 0, 2);
  int mqttRetain = retain ? 1 : 0;

  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    modem_.at().exec(10000L, GF("+CMQTTPUB=0,"), mqttQos, GF(",60,"),
                     mqttRetain);

    String urcLine;
    if (modem_.at().waitUrc(UrcType::MqttPub, 10000UL, urcLine)) {
      int code = -1;
      if (ModemParsers::parseMqttResultCode(urcLine, "+CMQTTPUB:", code) &&
          code == 0) {
        return true;
      }
    }
  }

  modem_.logLine("[mqtt] publish urc timeout");
  return false;
}

bool ModemMqtt::publishText(const char* topic, const char* text, int qos,
                            bool retain) {
  return publish(topic, text, qos, retain);
}

bool ModemMqtt::publishJson(const char* topic, const char* json, int qos,
                            bool retain) {
  return publish(topic, json, qos, retain);
}

bool ModemMqtt::publishFloat(const char* topic, float value, int qos,
                             bool retain, uint8_t decimals) {
  String payload(value, static_cast<unsigned int>(decimals));
  return publish(topic, payload.c_str(), qos, retain);
}

bool ModemMqtt::subscribe(const char* topic, int qos) {
  if (!connected_ || !topic) {
    return false;
  }

  size_t topicLen = strlen(topic);
  int mqttQos = constrain(qos, 0, 2);

  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    if (!modem_.at().execPromptedData(5000L, topic, topicLen,
                                      GF("+CMQTTSUBTOPIC=0,"), topicLen,
                                      GF(","), mqttQos)) {
      modem_.logLine("[mqtt] sub topic failed");
      delay(1000);
      continue;
    }

    modem_.at().exec(10000L, GF("+CMQTTSUB=0,0"));

    String urcLine;
    if (modem_.at().waitUrc(UrcType::MqttSub, 10000UL, urcLine)) {
      int code = -1;
      if (ModemParsers::parseMqttResultCode(urcLine, "+CMQTTSUB:", code) &&
          code == 0) {
        return true;
      }
    }

    modem_.logLine("[mqtt] sub urc timeout");
    delay(1000);
  }

  return false;
}

bool ModemMqtt::subscribeTopic(const char* topic, int qos) {
  return subscribe(topic, qos);
}

bool ModemMqtt::pollIncoming(String& topicOut, String& payloadOut) {
  topicOut = "";
  payloadOut = "";

  String urcLine;
  if (!modem_.urc().pop(UrcType::MqttRxStart, urcLine)) {
    String line;
    if (!modem_.at().readLineNonBlocking(line)) {
      return false;
    }
    UrcType found = UrcStore::classify(line);
    if (found != UrcType::MqttRxStart) {
      if (found != UrcType::None) {
        modem_.urc().push(line);
      }
      return false;
    }
    urcLine = line;
  }

  uint16_t topicLen = 0;
  uint16_t payloadLen = 0;
  if (!ModemParsers::parseRxStart(urcLine, topicLen, payloadLen)) {
    return false;
  }

  if (!modem_.at().waitUrc(UrcType::MqttRxTopic, 5000UL, urcLine)) {
    return false;
  }
  if (!modem_.at().readExact(topicLen, topicOut, 5000)) {
    return false;
  }

  if (!modem_.at().waitUrc(UrcType::MqttRxPayload, 5000UL, urcLine)) {
    return false;
  }
  if (!modem_.at().readExact(payloadLen, payloadOut, 5000)) {
    return false;
  }

  modem_.at().waitUrc(UrcType::MqttRxEnd, 2000UL, urcLine);

  while (modem_.tapStream().available()) {
    char t = static_cast<char>(modem_.tapStream().peek());
    if (t == '\r' || t == '\n') {
      modem_.tapStream().read();
    } else {
      break;
    }
  }

  return true;
}

void ModemMqtt::disconnect() {
  if (connected_) {
    modem_.at().exec(5000L, GF("+CMQTTDISC=0,60"));
    String discLine;
    if (modem_.at().waitUrc(UrcType::MqttDisc, 3000UL, discLine)) {
      int code = -1;
      if (ModemParsers::parseMqttResultCode(discLine, "+CMQTTDISC:", code) &&
          !isMqttOkCode(code)) {
        modem_.logValue("[mqtt] disc urc", discLine);
      }
    }
  }

  if (acquired_) {
    modem_.at().exec(5000L, GF("+CMQTTREL=0"));
    String relLine;
    if (modem_.at().waitUrc(UrcType::MqttRel, 3000UL, relLine)) {
      int code = -1;
      if (ModemParsers::parseMqttResultCode(relLine, "+CMQTTREL:", code) &&
          !isMqttOkCode(code)) {
        modem_.logValue("[mqtt] rel urc", relLine);
      }
    }
  }

  if (started_) {
    modem_.at().exec(5000L, GF("+CMQTTSTOP"));
    String stopLine;
    if (modem_.at().waitUrc(UrcType::MqttStop, 5000UL, stopLine)) {
      int code = -1;
      if (ModemParsers::parseMqttResultCode(stopLine, "+CMQTTSTOP:", code) &&
          !isMqttOkCode(code)) {
        modem_.logValue("[mqtt] stop urc", stopLine);
      }
    }
  }

  connected_ = false;
  acquired_ = false;
  started_ = false;
  tlsConfigured_ = false;
  needsReset_ = true;
  delay(2000);
}
