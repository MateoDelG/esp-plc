#include "modem_mqtt_simcom.h"

#include "modem_manager.h"
#include "modem_parsers.h"
#include "modem_retry.h"

SimcomMqttClient::SimcomMqttClient(ModemManager& modem) : modem_(modem) {}

static bool isMqttOkCode(int code) {
  return code == 0 || code == 11 || code == 14 || code == 19 || code == 20;
}

void SimcomMqttClient::resetService() {
  String line;

  modem_.at().exec(3000L, GF("+CMQTTDISC=0,60"));
  if (modem_.at().waitUrc(UrcType::MqttDisc, 3000UL, line)) {
    int code = -1;
    if (ModemParsers::parseMqttResultCode(line, "+CMQTTDISC:", code) &&
        !isMqttOkCode(code)) {
      modem_.logWarn("mqtt", "disc urc: " + line);
    }
  }

  modem_.at().exec(3000L, GF("+CMQTTREL=0"));
  if (modem_.at().waitUrc(UrcType::MqttRel, 3000UL, line)) {
    int code = -1;
    if (ModemParsers::parseMqttResultCode(line, "+CMQTTREL:", code) &&
        !isMqttOkCode(code)) {
      modem_.logWarn("mqtt", "rel urc: " + line);
    }
  }

  modem_.at().exec(3000L, GF("+CMQTTSTOP"));
  if (modem_.at().waitUrc(UrcType::MqttStop, 6000UL, line)) {
    int code = -1;
    if (ModemParsers::parseMqttResultCode(line, "+CMQTTSTOP:", code) &&
        !isMqttOkCode(code)) {
      modem_.logWarn("mqtt", "stop urc: " + line);
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
  modem_.idle(1500);
}

bool SimcomMqttClient::ensureStarted() {
  if (started_) {
    return true;
  }

  modem_.transitionTo(ModemState::MqttStarting, "mqtt ensure started");
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
    modem_.idle(1200);
  }

  RetryPolicy policy;
  policy.attempts = 2;
  policy.delayMs = 0;
  policy.label = "mqtt start";

  bool startedOk = retry(modem_, policy, "mqtt", [&]() {
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
        modem_.transitionTo(ModemState::MqttStarted, "mqtt started");
        return true;
      }
    }

    resetService();
    return false;
  });

  if (startedOk) {
    return true;
  }

  return modem_.fail("mqtt", ModemErrorCode::MqttStartFailed,
                     "start urc timeout", modem_.state());
}

bool SimcomMqttClient::acquire(const char* clientId) {
  if (!clientId || strlen(clientId) == 0) {
    return modem_.fail("mqtt", ModemErrorCode::InvalidArgument,
                       "mqtt client id empty", modem_.state());
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

  modem_.logWarn("mqtt", "accq raw: " + res.raw);
  return modem_.fail("mqtt", ModemErrorCode::MqttAcquireFailed,
                     "mqtt accq failed", modem_.state());
}

bool SimcomMqttClient::connect(const char* host, uint16_t port,
                               const char* clientId, const char* user,
                               const char* pass, bool useTls) {
  if (!host || strlen(host) == 0 || !clientId || strlen(clientId) == 0) {
    return modem_.fail("mqtt", ModemErrorCode::InvalidArgument,
                       "mqtt host/client empty", modem_.state());
  }
  if (connected_) {
    return true;
  }

  if (!ensureStarted()) {
    return modem_.fail("mqtt", ModemErrorCode::MqttStartFailed,
                       "start failed", modem_.state());
  }

  if (!acquire(clientId)) {
    return modem_.fail("mqtt", ModemErrorCode::MqttAcquireFailed,
                       "acquire failed", modem_.state());
  }

  return connectBroker(host, port, user, pass, useTls);
}

bool SimcomMqttClient::connectBroker(const char* host, uint16_t port,
                                     const char* user, const char* pass,
                                     bool useTls) {
  if (!host || strlen(host) == 0) {
    return modem_.fail("mqtt", ModemErrorCode::InvalidArgument,
                       "mqtt host empty", modem_.state());
  }
  if (connected_) {
    return true;
  }

  modem_.transitionTo(ModemState::MqttConnecting, "mqtt connect broker");
  if (!modem_.data().ensureDataSession()) {
    return modem_.fail("mqtt", ModemErrorCode::DataSessionFailed,
                       "data not ready", modem_.state());
  }

  if (!modem_.data().ensureNetOpen()) {
    return modem_.fail("mqtt", ModemErrorCode::NetOpenFailed,
                       "netopen not ready", modem_.state());
  }

  if (useTls && !tlsConfigured_) {
    modem_.at().exec(3000L, GF("+CSSLCFG=\"sslversion\",0,4"));
    modem_.at().exec(3000L, GF("+CSSLCFG=\"authmode\",0,0"));
    modem_.at().exec(3000L, GF("+CSSLCFG=\"enableSNI\",0,1"));
    modem_.at().exec(3000L, GF("+CMQTTSSLCFG=0,0"));
    tlsConfigured_ = true;
  }

  RetryPolicy policy;
  policy.attempts = 2;
  policy.delayMs = 0;
  policy.label = "mqtt connect";

  bool ok = retry(modem_, policy, "mqtt", [&]() {
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
        modem_.transitionTo(ModemState::MqttConnected, "mqtt connected");
        return true;
      }
    }
    return false;
  });

  if (!ok) {
    connected_ = false;
    return modem_.fail("mqtt", ModemErrorCode::MqttConnectFailed,
                       "connect urc timeout", modem_.state());
  }
  return true;
}

bool SimcomMqttClient::ensureConnected(const char* host, uint16_t port,
                                       const char* clientId, uint8_t retries,
                                       uint32_t retryDelayMs, const char* user,
                                       const char* pass, bool useTls) {
  if (!host || strlen(host) == 0) {
    return modem_.fail("mqtt", ModemErrorCode::InvalidArgument,
                       "mqtt host empty", modem_.state());
  }
  if (connected_) {
    return true;
  }

  if (!ensureStarted()) {
    return modem_.fail("mqtt", ModemErrorCode::MqttStartFailed,
                       "start failed", modem_.state());
  }

  if (!acquire(clientId)) {
    return modem_.fail("mqtt", ModemErrorCode::MqttAcquireFailed,
                       "acquire failed", modem_.state());
  }

  RetryPolicy policy;
  policy.attempts = retries;
  policy.delayMs = retryDelayMs;
  policy.label = "broker connect";

  return retry(modem_, policy, "mqtt", [&]() {
    if (connectBroker(host, port, user, pass, useTls)) {
      return true;
    }
    if (connected_) {
      return true;
    }
    return false;
  });
}

bool SimcomMqttClient::publish(const char* topic, const char* payload, int qos,
                               bool retain) {
  if (!connected_ || !topic || !payload) {
    return modem_.fail("mqtt", ModemErrorCode::InvalidArgument,
                       "mqtt publish args", modem_.state());
  }

  size_t topicLen = strlen(topic);
  size_t payloadLen = strlen(payload);

  if (!modem_.at().execPromptedData(5000L, topic, topicLen,
                                    GF("+CMQTTTOPIC=0,"), topicLen)) {
    return modem_.fail("mqtt", ModemErrorCode::MqttPublishFailed,
                       "topic payload failed", modem_.state());
  }

  modem_.idle(30);

  if (!modem_.at().execPromptedData(5000L, payload, payloadLen,
                                    GF("+CMQTTPAYLOAD=0,"), payloadLen)) {
    return modem_.fail("mqtt", ModemErrorCode::MqttPublishFailed,
                       "payload write failed", modem_.state());
  }

  modem_.idle(30);

  int mqttQos = constrain(qos, 0, 2);
  int mqttRetain = retain ? 1 : 0;

  RetryPolicy pubPolicy;
  pubPolicy.attempts = 2;
  pubPolicy.delayMs = 0;
  pubPolicy.label = "publish";

  bool pubOk = retry(modem_, pubPolicy, "mqtt", [&]() {
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
    return false;
  });

  if (!pubOk) {
    return modem_.fail("mqtt", ModemErrorCode::MqttPublishFailed,
                       "publish urc timeout", modem_.state());
  }
  return true;
}

bool SimcomMqttClient::subscribe(const char* topic, int qos) {
  if (!connected_ || !topic) {
    return modem_.fail("mqtt", ModemErrorCode::InvalidArgument,
                       "mqtt subscribe args", modem_.state());
  }

  size_t topicLen = strlen(topic);
  int mqttQos = constrain(qos, 0, 2);

  RetryPolicy subPolicy;
  subPolicy.attempts = 2;
  subPolicy.delayMs = 1000;
  subPolicy.label = "subscribe";

  bool subOk = retry(modem_, subPolicy, "mqtt", [&]() {
    if (!modem_.at().execPromptedData(5000L, topic, topicLen,
                                      GF("+CMQTTSUBTOPIC=0,"), topicLen,
                                      GF(","), mqttQos)) {
      modem_.logWarn("mqtt", "sub topic failed");
      return false;
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

    modem_.logWarn("mqtt", "sub urc timeout");
    return false;
  });

  if (!subOk) {
    return modem_.fail("mqtt", ModemErrorCode::MqttSubscribeFailed,
                       "subscribe failed", modem_.state());
  }
  return true;
}

bool SimcomMqttClient::pollIncoming(String& topicOut, String& payloadOut) {
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

void SimcomMqttClient::disconnect() {
  if (connected_) {
    modem_.at().exec(5000L, GF("+CMQTTDISC=0,60"));
    String discLine;
    if (modem_.at().waitUrc(UrcType::MqttDisc, 3000UL, discLine)) {
      int code = -1;
      if (ModemParsers::parseMqttResultCode(discLine, "+CMQTTDISC:", code) &&
          !isMqttOkCode(code)) {
        modem_.logWarn("mqtt", "disc urc: " + discLine);
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
        modem_.logWarn("mqtt", "rel urc: " + relLine);
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
        modem_.logWarn("mqtt", "stop urc: " + stopLine);
      }
    }
  }

  connected_ = false;
  acquired_ = false;
  started_ = false;
  tlsConfigured_ = false;
  needsReset_ = true;
  modem_.idle(2000);
}
