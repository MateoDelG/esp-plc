#include "comms/ubidots/ubidots_service.h"

#include "comms/ubidots/ubidots_payload_builder.h"
#include "config/modem_config.h"
#include "config/ubidots_config.h"
#include "config/ota_modem_codes.h"
#include "modem_parsers.h"

#ifndef UBIDOTS_RX_DEBUG
#define UBIDOTS_RX_DEBUG 1
#endif

UbidotsService::UbidotsService(Logger& logger)
  : logger_(logger), modem_(makeModemConfig(logger_)) {
  connectBackoffMs_ = kUbiReconnectIntervalMs;
}

bool UbidotsService::begin() {
  if (modemTask_ != nullptr) {
    return true;
  }

  if (mqttMutex_ == nullptr) {
    mqttMutex_ = xSemaphoreCreateMutex();
    if (!mqttMutex_) {
      logger_.warn("ubidots: mqtt mutex create failed");
    }
  }

  logger_.info("ubidots: init modem task");
  xTaskCreatePinnedToCore(
    modemTaskEntry,
    "modemTask",
    8192,
    this,
    2,
    &modemTask_,
    1
  );
  return true;
}

void UbidotsService::update() {
  if (otaMode_ || !modemReady_ || !dataReady_ || mqttBusy_ || !isConnected()) {
    return;
  }

  checkUrcOverflow();

  if (rxTaskActive_) {
    return;
  }

  uint32_t now = millis();
  if (now - lastPollMs_ < kUbiPollIntervalMs) {
    return;
  }
  lastPollMs_ = now;

  if (!lockMqtt("poll")) {
    logger_.warn("ubidots: poll skipped, mqtt locked");
    return;
  }
  String topic;
  String payload;
  String consoleTopic = ubidotsConsoleTopic();
  uint8_t drained = 0;
  while (modem_.mqtt().pollIncoming(topic, payload)) {
    ++drained;
    if (topic == consoleTopic) {
#if UBIDOTS_RX_DEBUG
      String hexLine;
      hexLine.reserve(payload.length() * 3);
      for (size_t i = 0; i < payload.length(); ++i) {
        uint8_t b = static_cast<uint8_t>(payload[i]);
        char hi = "0123456789ABCDEF"[(b >> 4) & 0x0F];
        char lo = "0123456789ABCDEF"[b & 0x0F];
        hexLine += hi;
        hexLine += lo;
        if (i + 1 < payload.length()) {
          hexLine += ' ';
        }
      }
      logger_.logf("ubidots", "console hex(%u): %s",
                   static_cast<unsigned>(payload.length()), hexLine.c_str());
#endif
      pushConsoleMessage(topic, payload);
      logger_.logf("ubidots", "console: %s", payload.c_str());
    }
  }
  unlockMqtt();
  if (drained > 1) {
    logger_.logf("ubidots", "drained %u mqtt messages", drained);
  }
}

bool UbidotsService::publishTelemetry(const TelemetryPacket& data) {
  if (otaActive_) {
    lastPublishOk_ = false;
    return false;
  }
  if (publishDisabled_) {
    if (!publishDisabledLogged_) {
      logger_.warn("ubidots: publish disabled (test)");
      publishDisabledLogged_ = true;
    }
    lastPublishOk_ = false;
    return false;
  }
  if (otaMode_ || !modemReady_ || !dataReady_ || mqttBusy_ || !isConnected()) {
    lastPublishOk_ = false;
    return false;
  }

  if (!lockMqtt("publish")) {
    logger_.warn("ubidots: publish skipped, mqtt locked");
    lastPublishOk_ = false;
    return false;
  }

  String topic = ubidotsPublishTopic();
  String payload = UbidotsPayloadBuilder::build(data);
  bool ok = modem_.mqtt().publishJson(topic.c_str(), payload.c_str(), 1, false);
  unlockMqtt();
  lastPublishOk_ = ok;
  if (ok) {
    logger_.info("ubidots: publish ok");
  } else {
    logger_.warn("ubidots: publish failed");
    handlePublishFailure("publish");
  }
  return ok;
}

bool UbidotsService::publishBlowersState(uint8_t value) {
  if (otaActive_) {
    return false;
  }
  if (publishDisabled_) {
    if (!publishDisabledLogged_) {
      logger_.warn("ubidots: publish disabled (test)");
      publishDisabledLogged_ = true;
    }
    return false;
  }
  if (otaMode_ || !modemReady_ || !dataReady_ || mqttBusy_ || !isConnected()) {
    return false;
  }


  if (!lockMqtt("publish-blowers")) {
    logger_.warn("ubidots: blowers publish skipped, mqtt locked");
    return false;
  }

  String topic = ubidotsPublishTopic();
  String payload = String("{\"state-blowers\":") + String(value) + "}";
  bool ok = modem_.mqtt().publishJson(topic.c_str(), payload.c_str(), 1, false);
  unlockMqtt();
  if (ok) {
    logger_.info("ubidots: blowers state published");
  } else {
    logger_.warn("ubidots: blowers publish failed");
    handlePublishFailure("publish-blowers");
  }
  return ok;
}

bool UbidotsService::publishConsoleValue(uint16_t value) {
  if (otaActive_ && value != kOtaSuccessCode && value != kOtaFailCode) {
    return false;
  }
  if (publishDisabled_) {
    if (!publishDisabledLogged_) {
      logger_.warn("ubidots: publish disabled (test)");
      publishDisabledLogged_ = true;
    }
    return false;
  }
  if (otaMode_ || !modemReady_ || !dataReady_ || mqttBusy_ || !isConnected()) {
    return false;
  }


  if (!lockMqtt("publish-console")) {
    logger_.warn("ubidots: console publish skipped, mqtt locked");
    return false;
  }

  String topic = ubidotsPublishTopic();
  String payload = String("{\"console\":") + String(value) + "}";
  bool ok = modem_.mqtt().publishJson(topic.c_str(), payload.c_str(), 1, false);
  unlockMqtt();
  if (ok) {
    logger_.info("ubidots: console value published");
  } else {
    logger_.warn("ubidots: console publish failed");
    handlePublishFailure("publish-console");
  }
  return ok;
}

uint8_t UbidotsService::drainIncoming(uint8_t maxMessages, uint32_t maxMs) {
  if (otaMode_ || !modemReady_ || !dataReady_ || mqttBusy_ || !isConnected()) {
    return 0;
  }

  checkUrcOverflow();

  if (!lockMqtt("drain")) {
    logger_.warn("ubidots: drain skipped, mqtt locked");
    return 0;
  }

  String consoleTopic = ubidotsConsoleTopic();
  String topic;
  String payload;
  uint8_t drained = 0;
  uint32_t startMs = millis();
  uint32_t deadlineMs = startMs + maxMs;
  bool extended = false;
  uint32_t frameDeadlineMs = 0;
  enum class DrainExit { None, MaxMessages, MaxTime, NoPending, FrameTimeout };
  DrainExit exitReason = DrainExit::None;
  auto hasPendingRx = [&]() {
    return modem_.urc().has(UrcType::MqttRxStart) ||
           modem_.urc().has(UrcType::MqttRxTopic) ||
           modem_.urc().has(UrcType::MqttRxPayload);
  };

  while (drained < maxMessages && millis() < deadlineMs) {
    if (!modem_.mqtt().pollIncoming(topic, payload)) {
      if (hasPendingRx()) {
        if (frameDeadlineMs == 0) {
          frameDeadlineMs = millis() + kUbiFrameTimeoutMs;
        } else if (millis() >= frameDeadlineMs) {
          exitReason = DrainExit::FrameTimeout;
          break;
        }
        if (millis() >= deadlineMs && !extended) {
          extended = true;
          deadlineMs += kUbiDrainExtendMs;
          logger_.warn("ubidots: drain extended (pending rx)");
        }
        modem_.idle(10);
        continue;
      }
      exitReason = DrainExit::NoPending;
      break;
    }
    ++drained;
    frameDeadlineMs = 0;
    if (topic == consoleTopic) {
      String hexLine;
      hexLine.reserve(payload.length() * 3);
      for (size_t i = 0; i < payload.length(); ++i) {
        uint8_t b = static_cast<uint8_t>(payload[i]);
        char hi = "0123456789ABCDEF"[(b >> 4) & 0x0F];
        char lo = "0123456789ABCDEF"[b & 0x0F];
        hexLine += hi;
        hexLine += lo;
        if (i + 1 < payload.length()) {
          hexLine += ' ';
        }
      }
      logger_.logf("ubidots", "console hex(%u): %s",
                   static_cast<unsigned>(payload.length()), hexLine.c_str());
      pushConsoleMessage(topic, payload);
      logger_.logf("ubidots", "console: %s", payload.c_str());
    }
  }

  if (drained >= maxMessages) {
    exitReason = DrainExit::MaxMessages;
  } else if (millis() >= deadlineMs && exitReason == DrainExit::None) {
    exitReason = DrainExit::MaxTime;
  }
  unlockMqtt();
  if (drained > 0) {
    logger_.logf("ubidots", "post-publish drain %u", drained);
  }
  switch (exitReason) {
    case DrainExit::MaxMessages:
      logger_.warn("ubidots: drain reached max messages");
      break;
    case DrainExit::MaxTime:
      logger_.warn("ubidots: drain reached max time");
      break;
    case DrainExit::NoPending:
      if (drained == 0) {
        logger_.logf("ubidots", "drain exit: no pending rx");
      }
      break;
    case DrainExit::FrameTimeout:
      logger_.warn("ubidots: drain frame timeout");
      break;
    case DrainExit::None:
      break;
  }
  return drained;
}

ModemManager& UbidotsService::modem() {
  return modem_;
}

bool UbidotsService::isConnected() const {
  return modem_.mqtt().isConnected();
}

bool UbidotsService::isConsoleSubscribed() const {
  return consoleSubscribed_;
}

bool UbidotsService::isModemReady() const {
  return modemReady_;
}

bool UbidotsService::isDataReady() const {
  return dataReady_;
}

bool UbidotsService::lastPublishOk() const {
  return lastPublishOk_;
}

bool UbidotsService::hasPendingConsoleMessage() const {
  return consoleCount_ > 0;
}

bool UbidotsService::popConsoleMessage(ConsoleMessage& out) {
  if (consoleCount_ == 0) {
    return false;
  }
  out = consoleQueue_[consoleHead_];
  consoleHead_ = static_cast<uint8_t>((consoleHead_ + 1) % kConsoleQueueSize);
  --consoleCount_;
  return true;
}

uint8_t UbidotsService::pendingConsoleCount() const {
  return consoleCount_;
}

bool UbidotsService::ensureConnected() {
  return isConnected();
}

bool UbidotsService::ensureSubscribed() {
  return consoleSubscribed_;
}

void UbidotsService::pushConsoleMessage(const String& topic,
                                        const String& payload) {
  if (consoleCount_ == kConsoleQueueSize) {
    logger_.logf("ubidots", "console queue full (%u), drop oldest",
                 static_cast<unsigned>(kConsoleQueueSize));
    consoleHead_ = static_cast<uint8_t>((consoleHead_ + 1) % kConsoleQueueSize);
    --consoleCount_;
  }
  ConsoleMessage& slot = consoleQueue_[consoleTail_];
  slot.topic = topic;
  slot.payload = payload;
  slot.timestampMs = millis();
  slot.isNew = true;
  consoleTail_ = static_cast<uint8_t>((consoleTail_ + 1) % kConsoleQueueSize);
  ++consoleCount_;
}

void UbidotsService::handlePublishFailure(const char* label) {
  if (otaMode_ || otaActive_ || !modemReady_ || !dataReady_) {
    return;
  }
  mqttBusy_ = true;
  if (lockMqtt(label)) {
    logger_.warn("ubidots: mqtt disconnect (publish failure)");
    modem_.mqtt().disconnect();
    unlockMqtt();
  } else {
    logger_.warn("ubidots: mqtt disconnect skipped, locked");
  }
  consoleSubscribed_ = false;
  mqttBusy_ = false;
}

void UbidotsService::handleMqttConnectFailure() {
  ModemError err = modem_.lastError();
  if (err.errorCode != ModemErrorCode::MqttAcquireFailed) {
    return;
  }

  uint32_t now = millis();
  if (accqFailStartMs_ == 0) {
    accqFailStartMs_ = now;
  }
  if (accqFailCount_ < 255) {
    ++accqFailCount_;
  }

  if (accqFailCount_ == 1) {
    connectBackoffMs_ = 2000;
  } else if (accqFailCount_ == 2) {
    connectBackoffMs_ = 5000;
  } else {
    connectBackoffMs_ = 10000;
  }

  if (now - accqFailStartMs_ >= 300000UL) {
    logger_.error("ubidots: mqtt accq failed 5 min, restarting");
    ESP.restart();
  }
}

void UbidotsService::resetAccqBackoff() {
  accqFailCount_ = 0;
  accqFailStartMs_ = 0;
  connectBackoffMs_ = kUbiReconnectIntervalMs;
}

void UbidotsService::modemTaskEntry(void* param) {
  auto* self = static_cast<UbidotsService*>(param);
  if (self) {
    self->modemTaskLoop();
  }
  vTaskDelete(nullptr);
}

void UbidotsService::rxTaskEntry(void* param) {
  auto* self = static_cast<UbidotsService*>(param);
  if (self) {
    self->rxTaskLoop();
  }
  vTaskDelete(nullptr);
}

void UbidotsService::modemTaskLoop() {
  bool modemReady = false;
  for (uint8_t attempt = 1; attempt <= 3; ++attempt) {
    logger_.logf("modem", "starting modem (attempt %u/3)", attempt);
    if (modem_.begin()) {
      modemReady = true;
      break;
    }
    logger_.warn("modem: init failed, retrying");
    vTaskDelay(pdMS_TO_TICKS(15000));
  }

  if (!modemReady) {
    logger_.error("modem: init failed after retries");
    return;
  }

  modemReady_ = true;
  logger_.info("modem: initialized");

  if (modem_.ensureDataSession()) {
    dataReady_ = true;
    logger_.info("modem: data session ready");
  } else {
    logger_.warn("modem: data session failed");
  }

  for (;;) {
    if (otaMode_) {
      if (modem_.mqtt().isConnected()) {
        mqttBusy_ = true;
        logger_.info("ubidots: mqtt disconnect (ota)");
        if (lockMqtt("disconnect")) {
          modem_.mqtt().disconnect();
          unlockMqtt();
        } else {
          logger_.warn("ubidots: mqtt disconnect skipped, locked");
        }
        consoleSubscribed_ = false;
        mqttBusy_ = false;
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (!dataReady_) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    if (!modem_.mqtt().isConnected()) {
      uint32_t now = millis();
      if (now - lastConnectAttemptMs_ < connectBackoffMs_) {
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      lastConnectAttemptMs_ = now;

      mqttBusy_ = true;
      logger_.info("ubidots: mqtt connect");
      bool started = false;
      if (lockMqtt("start")) {
        started = modem_.mqtt().ensureStarted();
        unlockMqtt();
      } else {
        logger_.warn("ubidots: mqtt start skipped, locked");
      }
      if (!started) {
        logger_.warn("ubidots: mqtt start failed");
        mqttBusy_ = false;
        continue;
      }

      bool connected = false;
      if (lockMqtt("connect")) {
        connected = modem_.mqtt().connect(
          kUbiHost,
          kUbiPort,
          kUbiClientId,
          kUbiToken,
          kUbiToken,
          kUbiUseTls
        );
        unlockMqtt();
      } else {
        logger_.warn("ubidots: mqtt connect skipped, locked");
      }

      if (connected) {
        logger_.info("ubidots: mqtt connected");
        resetAccqBackoff();
        String topic = ubidotsConsoleTopic();
        if (lockMqtt("subscribe")) {
          consoleSubscribed_ = modem_.mqtt().subscribe(topic.c_str(), 1);
          unlockMqtt();
        } else {
          logger_.warn("ubidots: mqtt subscribe skipped, locked");
          consoleSubscribed_ = false;
        }
        if (consoleSubscribed_) {
          logger_.info("ubidots: console subscribed");
          if (!rxTask_) {
            rxTaskActive_ = true;
            xTaskCreatePinnedToCore(
              rxTaskEntry,
              "mqttRxTask",
              4096,
              this,
              1,
              &rxTask_,
              1
            );
            logger_.info("ubidots: rx task started");
          }
        } else {
          logger_.warn("ubidots: console subscribe failed");
        }
      } else {
        logger_.warn("ubidots: mqtt connect failed");
        consoleSubscribed_ = false;
        handleMqttConnectFailure();
      }
      mqttBusy_ = false;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
void UbidotsService::setOtaMode(bool enabled) {
  otaMode_ = enabled;
  if (enabled && modem_.mqtt().isConnected()) {
    mqttBusy_ = true;
    logger_.info("ubidots: mqtt disconnect (ota)");
    if (lockMqtt("disconnect")) {
      modem_.mqtt().disconnect();
      unlockMqtt();
    } else {
      logger_.warn("ubidots: mqtt disconnect skipped, locked");
    }
    consoleSubscribed_ = false;
    mqttBusy_ = false;
  }
}

void UbidotsService::checkUrcOverflow() {
  uint32_t overflow = modem_.urc().overflowCount();
  if (overflow != urcOverflowLast_) {
    uint32_t delta = overflow - urcOverflowLast_;
    logger_.logf("ubidots", "urc overflow +%u (total %u)",
                 static_cast<unsigned>(delta),
                 static_cast<unsigned>(overflow));
    urcOverflowLast_ = overflow;
  }
}

bool UbidotsService::lockMqtt(const char* label) {
  if (!mqttMutex_) {
    return true;
  }
  if (xSemaphoreTake(mqttMutex_, pdMS_TO_TICKS(kUbiMqttLockTimeoutMs)) != pdTRUE) {
    if (label) {
      logger_.logf("ubidots", "mqtt lock timeout (%s)", label);
    } else {
      logger_.warn("ubidots: mqtt lock timeout");
    }
    return false;
  }
  return true;
}

void UbidotsService::unlockMqtt() {
  if (mqttMutex_) {
    xSemaphoreGive(mqttMutex_);
  }
}

bool UbidotsService::isOtaMode() const {
  return otaMode_;
}

void UbidotsService::setOtaActive(bool active) {
  otaActive_ = active;
}

bool UbidotsService::isOtaActive() const {
  return otaActive_;
}

void UbidotsService::rxTaskLoop() {
  String consoleTopic = ubidotsConsoleTopic();
  const uint8_t kBurstMax = 6;
  const uint32_t kBurstBudgetMs = 20;
  uint32_t rxCount = 0;

  enum class RxStage : uint8_t { Idle = 0, StartSeen, TopicRead, PayloadRead };
  RxStage stage = RxStage::Idle;
  uint16_t topicLen = 0;
  uint16_t payloadLen = 0;
  String topic;
  String payload;
  uint32_t stageStartMs = 0;

  auto resetStage = [&]() {
    stage = RxStage::Idle;
    topicLen = 0;
    payloadLen = 0;
    topic = "";
    payload = "";
    stageStartMs = 0;
  };

  auto readFiltered = [&](size_t length, String& out, uint32_t timeoutMs) {
    out = "";
    out.reserve(length + 4);
    uint32_t startMs = millis();
    Stream& stream = modem_.tapStream();
    while (out.length() < length && millis() - startMs < timeoutMs) {
      if (!stream.available()) {
        modem_.idle(5);
        continue;
      }
      char c = static_cast<char>(stream.read());
      if (c == '\r' || c == '\n') {
        continue;
      }
      if (c == '+') {
        while (millis() - startMs < timeoutMs) {
          if (!stream.available()) {
            modem_.idle(5);
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
  };

  for (;;) {
    if (rxPaused_) {
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    if (otaMode_ || !modemReady_ || !dataReady_ || !isConnected()) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    checkUrcOverflow();

    if (lockMqtt("rx")) {
      uint32_t burstStart = millis();
      uint8_t framesThisCycle = 0;
      while (framesThisCycle < kBurstMax && millis() - burstStart < kBurstBudgetMs) {
        String line;
        if (!modem_.at().readLineNonBlocking(line)) {
          break;
        }
        if (line.length() == 0) {
          continue;
        }

        UrcType type = UrcStore::classify(line);
        if (type != UrcType::None &&
            type != UrcType::MqttRxStart &&
            type != UrcType::MqttRxTopic &&
            type != UrcType::MqttRxPayload &&
            type != UrcType::MqttRxEnd) {
          modem_.urc().push(line);
          continue;
        }

        if (type == UrcType::MqttRxStart) {
          uint16_t tLen = 0;
          uint16_t pLen = 0;
          if (!ModemParsers::parseRxStart(line, tLen, pLen) ||
              tLen == 0 || pLen == 0) {
            logger_.warn("ubidots: rxstart parse failed");
            resetStage();
            continue;
          }
          topicLen = tLen;
          payloadLen = pLen;
          stage = RxStage::StartSeen;
          stageStartMs = millis();
          continue;
        }

        if (stage == RxStage::StartSeen && type == UrcType::MqttRxTopic) {
          if (!modem_.at().readExact(topicLen, topic, 2000)) {
            logger_.warn("ubidots: rxtopic read failed");
            resetStage();
            continue;
          }
          stage = RxStage::TopicRead;
          stageStartMs = millis();
          continue;
        }

        if (stage == RxStage::TopicRead && type == UrcType::MqttRxPayload) {
          if (!readFiltered(payloadLen, payload, 2000)) {
            logger_.warn("ubidots: rxpayload read failed");
            resetStage();
            continue;
          }
          stage = RxStage::PayloadRead;
          stageStartMs = millis();
          continue;
        }

        if (stage == RxStage::PayloadRead && type == UrcType::MqttRxEnd) {
#if UBIDOTS_RX_DEBUG
          String hexLine;
          hexLine.reserve(payload.length() * 3);
          for (size_t i = 0; i < payload.length(); ++i) {
            uint8_t b = static_cast<uint8_t>(payload[i]);
            char hi = "0123456789ABCDEF"[(b >> 4) & 0x0F];
            char lo = "0123456789ABCDEF"[b & 0x0F];
            hexLine += hi;
            hexLine += lo;
            if (i + 1 < payload.length()) {
              hexLine += ' ';
            }
          }
          logger_.logf("ubidots", "console hex(%u): %s",
                       static_cast<unsigned>(payload.length()), hexLine.c_str());
#endif
          if (topic == consoleTopic) {
            pushConsoleMessage(topic, payload);
            logger_.logf("ubidots", "console: %s", payload.c_str());
            ++rxCount;
            if ((rxCount % 20U) == 0U) {
              logger_.logf("ubidots", "rx frames: %u",
                           static_cast<unsigned>(rxCount));
            }
          }
          resetStage();
          ++framesThisCycle;
          continue;
        }

        if (stage != RxStage::Idle &&
            millis() - stageStartMs > kUbiFrameTimeoutMs) {
          logger_.warn("ubidots: rx frame timeout");
          resetStage();
        }
      }
      unlockMqtt();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void UbidotsService::setRxPaused(bool paused) {
  rxPaused_ = paused;
  if (paused) {
    logger_.info("ubidots: rx paused");
  } else {
    logger_.info("ubidots: rx resumed");
  }
}
