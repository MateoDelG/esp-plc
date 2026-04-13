#include "modem_mqtt_simcom.h"

#include "modem_manager.h"
#include "modem_parsers.h"
#include "modem_retry.h"

#ifndef UBIDOTS_RX_DEBUG
#define UBIDOTS_RX_DEBUG 0
#endif

static const uint32_t kMqttRxStageTimeoutMs = 2500;
static const uint8_t kRxBurstLines = 8;
static const uint32_t kRxBurstBudgetMs = 20;

SimcomMqttClient::SimcomMqttClient(ModemManager& modem) : modem_(modem) {}

static bool isMqttOkCode(int code) {
  return code == 0 || code == 11 || code == 14 || code == 19 || code == 20;
}

static bool readFilteredPayload(ModemManager& modem, size_t length,
                                String& out, uint32_t timeoutMs) {
  out = "";
  out.reserve(length + 4);
  uint32_t startMs = millis();
  Stream& stream = modem.tapStream();
  while (out.length() < length && millis() - startMs < timeoutMs) {
    if (!stream.available()) {
      modem.idle(5);
      continue;
    }
    char c = static_cast<char>(stream.read());
    if (c == '\r' || c == '\n') {
      continue;
    }
    if (c == '+') {
      while (millis() - startMs < timeoutMs) {
        if (!stream.available()) {
          modem.idle(5);
          continue;
        }
        char drop = static_cast<char>(stream.read());
        if (drop == '\n') {
          break;
        }
      }
      continue;
    }
    out += c;
  }
  return out.length() == length;
}

static void drainUrcBurst(ModemManager& modem) {
  uint32_t startMs = millis();
  uint8_t lines = 0;
  while (lines < kRxBurstLines && millis() - startMs < kRxBurstBudgetMs) {
    String line;
    if (!modem.at().readLineNonBlocking(line)) {
      break;
    }
    if (line.length() == 0) {
      continue;
    }
    UrcType type = UrcStore::classify(line);
    if (type != UrcType::None) {
      modem.urc().push(line);
      ++lines;
    }
  }
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

void SimcomMqttClient::markPublishFailure(const char* step) {
  if (step && strlen(step) > 0) {
    modem_.logWarn("mqtt", String("publish failure at ") + step + ", force reset");
  } else {
    modem_.logWarn("mqtt", "publish failure, force reset");
  }
  connected_ = false;
  acquired_ = false;
  started_ = false;
  tlsConfigured_ = false;
  needsReset_ = true;
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
      if (code == 0 || code == 1 || code == 23) {
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

  if (accqNextAttemptMs_ != 0 && millis() < accqNextAttemptMs_) {
    return false;
  }

  AtResult res;
  if (execAccq(clientId, res)) {
    acquired_ = true;
    resetAccqFailures();
    return true;
  }

  modem_.logWarn("mqtt", "accq raw: " + res.raw);
  recordAccqFailure();
  bool fullRecovery = accqFailCount_ >= 3;
  if (performAccqRecovery(clientId, fullRecovery)) {
    resetAccqFailures();
    return true;
  }

  needsReset_ = true;
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

  serverType_ = useTls ? 1 : 0;

  if (!ensureStarted()) {
    return modem_.fail("mqtt", ModemErrorCode::MqttStartFailed,
                       "start failed", modem_.state());
  }

  if (!acquire(clientId)) {
    return modem_.fail("mqtt", ModemErrorCode::MqttAcquireFailed,
                       "acquire failed", modem_.state());
  }

  return connectBroker(host, port, clientId, user, pass, useTls);
}

bool SimcomMqttClient::connectBroker(const char* host, uint16_t port,
                                    const char* clientId, const char* user,
                                    const char* pass, bool useTls) {
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
      if (code == 19) {
        return false;
      }
    }
    return false;
  });

  bool code19Detected = !ok;
  if (code19Detected) {
    modem_.logWarn("mqtt", "connect code 19, attempting stack recovery");
    if (performConnectStackRecovery(clientId)) {
      modem_.logWarn("mqtt", "stack recovery ok, reattempting connect");
      connected_ = false;
      ok = retry(modem_, policy, "mqtt", [&]() {
        if (user && pass && strlen(user) > 0) {
          modem_.at().exec(15000L, GF("+CMQTTCONNECT=0,\"tcp://"), host,
                           GF(":"), port, GF("\",60,1,\""), user, GF("\",\""),
                           pass, GF("\""));
        } else {
          modem_.at().exec(15000L, GF("+CMQTTCONNECT=0,\"tcp://"), host,
                           GF(":"), port, GF("\",60,1"));
        }
        String urcLine;
        if (modem_.at().waitUrc(UrcType::MqttConnect, 15000UL, urcLine)) {
          int code = -1;
          if (ModemParsers::parseMqttResultCode(urcLine, "+CMQTTCONNECT:",
                                                code) &&
              code == 0) {
            connected_ = true;
            modem_.transitionTo(ModemState::MqttConnected, "mqtt connected");
            return true;
          }
        }
        return false;
      });
      if (ok) {
        return true;
      }
    }
    modem_.logWarn("mqtt", "stack recovery failed, attempting direct reconnect");
    resetService();
    if (ensureStarted()) {
      AtResult accqRes;
      if (execAccq(clientId, accqRes)) {
        acquired_ = true;
        modem_.logWarn("mqtt", "direct reconnect: accq ok, retrying connect");
        RetryPolicy oneShot;
        oneShot.attempts = 1;
        oneShot.delayMs = 0;
        oneShot.label = "mqtt connect direct";
        ok = retry(modem_, oneShot, "mqtt", [&]() {
          if (user && pass && strlen(user) > 0) {
            modem_.at().exec(15000L, GF("+CMQTTCONNECT=0,\"tcp://"), host,
                             GF(":"), port, GF("\",60,1,\""), user, GF("\",\""),
                             pass, GF("\""));
          } else {
            modem_.at().exec(15000L, GF("+CMQTTCONNECT=0,\"tcp://"), host,
                             GF(":"), port, GF("\",60,1"));
          }
          String urcLine;
          if (modem_.at().waitUrc(UrcType::MqttConnect, 15000UL, urcLine)) {
            int code = -1;
            if (ModemParsers::parseMqttResultCode(urcLine, "+CMQTTCONNECT:",
                                                  code) &&
                code == 0) {
              connected_ = true;
              modem_.transitionTo(ModemState::MqttConnected, "mqtt connected");
              return true;
            }
          }
          return false;
        });
        if (ok) {
          return true;
        }
      }
    }
    modem_.logWarn("mqtt", "direct reconnect failed, escalating");
    markNeedsModemRestart();
  }

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

  serverType_ = useTls ? 1 : 0;

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
    if (connectBroker(host, port, clientId, user, pass, useTls)) {
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
    markPublishFailure("topic");
    return modem_.fail("mqtt", ModemErrorCode::MqttPublishFailed,
                       "topic payload failed", modem_.state());
  }

  modem_.idle(30);

  if (!modem_.at().execPromptedData(5000L, payload, payloadLen,
                                    GF("+CMQTTPAYLOAD=0,"), payloadLen)) {
    markPublishFailure("payload");
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
    markPublishFailure("publish");
    return modem_.fail("mqtt", ModemErrorCode::MqttPublishFailed,
                       "publish urc timeout", modem_.state());
  }
  return true;
}

bool SimcomMqttClient::execAccq(const char* clientId, AtResult& out) {
  out = modem_.at().exec(10000L, GF("+CMQTTACCQ=0,\""), clientId, GF("\","),
                         serverType_);
  if (out.ok || out.raw.indexOf("+CMQTTACCQ: 0,0") >= 0) {
    return true;
  }
  return false;
}

void SimcomMqttClient::recordAccqFailure() {
  uint32_t now = millis();
  if (accqFailStartMs_ == 0) {
    accqFailStartMs_ = now;
  }
  if (accqFailCount_ < 255) {
    ++accqFailCount_;
  }
  if (accqFailCount_ == 1) {
    accqBackoffMs_ = 2000;
  } else if (accqFailCount_ == 2) {
    accqBackoffMs_ = 5000;
  } else {
    accqBackoffMs_ = 10000;
  }
  accqNextAttemptMs_ = now + accqBackoffMs_;
}

void SimcomMqttClient::resetAccqFailures() {
  accqFailCount_ = 0;
  accqFailStartMs_ = 0;
  accqBackoffMs_ = 2000;
  accqNextAttemptMs_ = 0;
}

bool SimcomMqttClient::queryAccqOccupied(uint8_t index, bool& occupied) {
  occupied = false;
  AtResult res = modem_.at().exec(3000L, GF("+CMQTTACCQ?"));
  if (!res.ok && res.raw.length() == 0) {
    return false;
  }

  int start = 0;
  while (start < res.raw.length()) {
    int end = res.raw.indexOf('\n', start);
    if (end < 0) {
      end = res.raw.length();
    }
    String line = res.raw.substring(start, end);
    line.trim();
    start = end + 1;
    if (!line.startsWith("+CMQTTACCQ:")) {
      continue;
    }
    int colon = line.indexOf(':');
    if (colon < 0) {
      continue;
    }
    String rest = line.substring(colon + 1);
    rest.trim();
    int comma = rest.indexOf(',');
    if (comma < 0) {
      continue;
    }
    int idx = rest.substring(0, comma).toInt();
    if (idx != static_cast<int>(index)) {
      continue;
    }
    int firstQuote = rest.indexOf('"');
    int secondQuote = rest.indexOf('"', firstQuote + 1);
    if (firstQuote >= 0 && secondQuote > firstQuote) {
      String clientId = rest.substring(firstQuote + 1, secondQuote);
      occupied = clientId.length() > 0;
    } else {
      occupied = false;
    }
    return true;
  }
  return false;
}

bool SimcomMqttClient::performAccqRecovery(const char* clientId, bool full) {
  if (full) {
    modem_.logWarn("mqtt", "accq recovery: full");
    bool occupied1 = false;
    if (queryAccqOccupied(1, occupied1) && occupied1) {
      modem_.logWarn("mqtt", "accq recovery: rel index 1");
      modem_.at().exec(3000L, GF("+CMQTTREL=1"));
    }
    modem_.at().exec(3000L, GF("+CMQTTDISC=0,120"));
  } else {
    modem_.logWarn("mqtt", "accq recovery: quick");
  }

  modem_.at().exec(3000L, GF("+CMQTTREL=0"));
  modem_.at().exec(3000L, GF("+CMQTTSTOP"));

  started_ = false;
  acquired_ = false;
  connected_ = false;
  tlsConfigured_ = false;

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
    if (code == 0 || code == 1 || code == 23) {
      started_ = true;
    }
  }

  if (!started_) {
    modem_.logWarn("mqtt", "accq recovery: start failed");
    return false;
  }

  AtResult accqRes;
  if (execAccq(clientId, accqRes)) {
    acquired_ = true;
    return true;
  }
  modem_.logWarn("mqtt", "accq recovery failed: " + accqRes.raw);
  return false;
}

bool SimcomMqttClient::performConnectStackRecovery(const char* clientId) {
  modem_.logWarn("mqtt", "connect stack recovery: disconnecting");
  modem_.at().exec(3000L, GF("+CMQTTDISC=0,120"));
  modem_.logWarn("mqtt", "connect stack recovery: releasing");
  modem_.at().exec(3000L, GF("+CMQTTREL=0"));
  modem_.logWarn("mqtt", "connect stack recovery: stopping");
  modem_.at().exec(3000L, GF("+CMQTTSTOP"));

  started_ = false;
  acquired_ = false;
  connected_ = false;
  tlsConfigured_ = false;

  modem_.logWarn("mqtt", "connect stack recovery: starting");
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
    if (code != 0 && code != 1 && code != 23) {
      modem_.logWarn("mqtt", "connect stack recovery: start failed");
      return false;
    }
    started_ = true;
  } else {
    modem_.logWarn("mqtt", "connect stack recovery: start urc timeout");
    return false;
  }

  AtResult accqRes;
  if (execAccq(clientId, accqRes)) {
    acquired_ = true;
    modem_.logWarn("mqtt", "connect stack recovery: success");
    return true;
  }
  modem_.logWarn("mqtt", "connect stack recovery: accq failed");
  return false;
}

void SimcomMqttClient::markNeedsModemRestart() {
  needsModemRestart_ = true;
}

void SimcomMqttClient::markNeedsEspRestart() {
  needsEspRestart_ = true;
}

bool SimcomMqttClient::needsModemRestart() const {
  return needsModemRestart_;
}

bool SimcomMqttClient::needsEspRestart() const {
  return needsEspRestart_;
}

void SimcomMqttClient::clearEscalation() {
  needsModemRestart_ = false;
  needsEspRestart_ = false;
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

  drainUrcBurst(modem_);

  auto resetState = [&]() {
    rxState_ = RxState::Idle;
    rxTopicLen_ = 0;
    rxPayloadLen_ = 0;
    rxTopic_ = "";
    rxPayload_ = "";
    rxStageStartMs_ = 0;
  };

  if (rxState_ != RxState::Idle &&
      millis() - rxStageStartMs_ > kMqttRxStageTimeoutMs) {
    modem_.logWarn("mqtt", "rx stage timeout");
    resetState();
    return false;
  }

  for (uint8_t guard = 0; guard < 4; ++guard) {
    switch (rxState_) {
      case RxState::Idle: {
        String line;
        if (!modem_.urc().pop(UrcType::MqttRxStart, line)) {
          return false;
        }
        uint16_t topicLen = 0;
        uint16_t payloadLen = 0;
        if (!ModemParsers::parseRxStart(line, topicLen, payloadLen) ||
            topicLen == 0 || payloadLen == 0) {
          modem_.logWarn("mqtt", "rxstart parse failed: " + line);
          resetState();
          return false;
        }
        rxTopicLen_ = topicLen;
        rxPayloadLen_ = payloadLen;
        rxStageStartMs_ = millis();
        rxState_ = RxState::StartSeen;
        continue;
      }
      case RxState::StartSeen: {
        String line;
        if (!modem_.urc().pop(UrcType::MqttRxTopic, line)) {
          return false;
        }
        if (!modem_.at().readExact(rxTopicLen_, rxTopic_, 5000)) {
          modem_.logWarn("mqtt", "rxtopic read failed");
          resetState();
          return false;
        }
        rxStageStartMs_ = millis();
        rxState_ = RxState::TopicRead;
        continue;
      }
      case RxState::TopicRead: {
        String line;
        if (!modem_.urc().pop(UrcType::MqttRxPayload, line)) {
          return false;
        }
        if (!readFilteredPayload(modem_, rxPayloadLen_, rxPayload_, 5000)) {
          modem_.logWarn("mqtt", "rxpayload read failed");
          resetState();
          return false;
        }
        rxStageStartMs_ = millis();
        rxState_ = RxState::PayloadRead;
        continue;
      }
      case RxState::PayloadRead: {
        String line;
        if (!modem_.urc().pop(UrcType::MqttRxEnd, line)) {
          return false;
        }
        topicOut = rxTopic_;
        payloadOut = rxPayload_;
        resetState();

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
      default:
        resetState();
        return false;
    }
  }

  return false;
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
