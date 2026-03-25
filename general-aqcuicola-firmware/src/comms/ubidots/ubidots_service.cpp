#include "comms/ubidots/ubidots_service.h"

#include "comms/ubidots/ubidots_payload_builder.h"
#include "config/modem_config.h"
#include "config/ubidots_config.h"

UbidotsService::UbidotsService(Logger& logger)
  : logger_(logger), modem_(makeModemConfig(logger_)) {}

bool UbidotsService::begin() {
  if (modemTask_ != nullptr) {
    return true;
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
  if (!modemReady_ || !dataReady_ || mqttBusy_ || !isConnected()) {
    return;
  }

  uint32_t now = millis();
  if (now - lastPollMs_ < kUbiPollIntervalMs) {
    return;
  }
  lastPollMs_ = now;

  String topic;
  String payload;
  if (modem_.mqtt().pollIncoming(topic, payload)) {
    String consoleTopic = ubidotsConsoleTopic();
    if (topic == consoleTopic) {
      consoleMessage_.topic = topic;
      consoleMessage_.payload = payload;
      consoleMessage_.timestampMs = millis();
      consoleMessage_.isNew = true;
      logger_.logf("ubidots", "console: %s", payload.c_str());
    }
  }
}

bool UbidotsService::publishTelemetry(const TelemetryPacket& data) {
  if (!modemReady_ || !dataReady_ || mqttBusy_ || !isConnected()) {
    lastPublishOk_ = false;
    return false;
  }

  String topic = ubidotsPublishTopic();
  String payload = UbidotsPayloadBuilder::build(data);
  bool ok = modem_.mqtt().publishJson(topic.c_str(), payload.c_str(), 1, false);
  lastPublishOk_ = ok;
  if (ok) {
    logger_.info("ubidots: publish ok");
  } else {
    logger_.warn("ubidots: publish failed");
  }
  return ok;
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

bool UbidotsService::hasNewConsoleMessage() const {
  return consoleMessage_.isNew;
}

const ConsoleMessage& UbidotsService::latestConsoleMessage() const {
  return consoleMessage_;
}

void UbidotsService::ackConsoleMessage() {
  consoleMessage_.isNew = false;
}

bool UbidotsService::ensureConnected() {
  return isConnected();
}

bool UbidotsService::ensureSubscribed() {
  return consoleSubscribed_;
}

void UbidotsService::modemTaskEntry(void* param) {
  auto* self = static_cast<UbidotsService*>(param);
  if (self) {
    self->modemTaskLoop();
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
    if (!dataReady_) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    if (!modem_.mqtt().isConnected()) {
      uint32_t now = millis();
      if (now - lastConnectAttemptMs_ < kUbiReconnectIntervalMs) {
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      lastConnectAttemptMs_ = now;

      mqttBusy_ = true;
      logger_.info("ubidots: mqtt connect");
      bool started = modem_.mqtt().ensureStarted();
      if (!started) {
        logger_.warn("ubidots: mqtt start failed");
        mqttBusy_ = false;
        continue;
      }

      bool connected = modem_.mqtt().connect(
        kUbiHost,
        kUbiPort,
        kUbiClientId,
        kUbiToken,
        kUbiToken,
        kUbiUseTls
      );

      if (connected) {
        logger_.info("ubidots: mqtt connected");
        String topic = ubidotsConsoleTopic();
        consoleSubscribed_ = modem_.mqtt().subscribe(topic.c_str(), 1);
        if (consoleSubscribed_) {
          logger_.info("ubidots: console subscribed");
        } else {
          logger_.warn("ubidots: console subscribe failed");
        }
      } else {
        logger_.warn("ubidots: mqtt connect failed");
        consoleSubscribed_ = false;
      }
      mqttBusy_ = false;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
