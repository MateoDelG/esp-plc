#include "app/app_controller.h"

#include "config/app_config.h"

#include <cmath>

AppController::AppController()
  : logger_(),
    wifiManager_(logger_),
    otaService_(logger_),
    ubidotsService_(logger_),
    telemetryService_(logger_, ubidotsService_),
    consoleService_(),
    analogService_(logger_),
    status_(),
    state_(AppState::Boot) {}

void AppController::begin() {
  logger_.begin(kSerialBaud);
  logger_.info("boot");

  setState(AppState::ConnectingWifi);
  bool wifiOk = wifiManager_.begin();
  status_.lastWifiAttemptOk = wifiOk;
  status_.wifiConnected = wifiManager_.isConnected();
  status_.localIp = wifiManager_.localIp();

  if (!status_.wifiConnected) {
    setState(AppState::Error);
    logger_.error("wifi: not connected");
    return;
  }

  consoleService_.begin();
  ConsoleService::setActive(&consoleService_);
  logger_.setSink(ConsoleService::logSink);
  consoleService_.setAnalogControl(&analogService_);
  consoleService_.setBlowerThresholdRefs(&blowerThresholdA0_, &blowerThresholdA1_);
  consoleService_.setBlowerDelayRef(&blowerNotifyDelaySec_);
  logger_.info("console: ready");

  setState(AppState::WifiReady);
  bool otaOk = otaService_.begin();
  status_.otaReady = otaOk;

  if (!otaOk) {
    setState(AppState::Error);
    logger_.error("ota: init failed");
    return;
  }

  setState(AppState::OtaReady);

  bool ubidotsOk = ubidotsService_.begin();
  if (!ubidotsOk) {
    logger_.warn("ubidots: task start failed");
  } else {
    logger_.info("ubidots: task started");
  }

  telemetryService_.begin();
  analogService_.begin();
  setState(AppState::Running);
  logger_.info("version: 1.08");

}

void AppController::update() {
  ubidotsService_.update();
  if (status_.wifiConnected && otaService_.isReady()) {
    otaService_.update();
  }

  consoleService_.update();

  telemetryService_.update();
  consoleService_.setTelemetry(telemetryService_.data());
  analogService_.update();
  consoleService_.setAnalogSnapshot(analogService_.data());

  const AnalogSnapshot& analog = analogService_.data();
  bool a0Enabled = (analog.enabledMask & 0x01) != 0;
  bool a1Enabled = (analog.enabledMask & 0x02) != 0;
  const AnalogChannelReading& ch0 = analog.channels[0];
  const AnalogChannelReading& ch1 = analog.channels[1];

  bool belowThreshold = false;
  if (a0Enabled && ch0.valid && ch0.volts < blowerThresholdA0_) {
    belowThreshold = true;
  }
  if (a1Enabled && ch1.valid && ch1.volts < blowerThresholdA1_) {
    belowThreshold = true;
  }

  bool verifierState = belowThreshold ? false : telemetryService_.data().stateBlowers;
  consoleService_.setBlowerStatus(verifierState, belowThreshold);

  if (verifierState != blowerCandidateState_) {
    blowerCandidateState_ = verifierState;
    blowerCandidateStartMs_ = millis();
  }

  uint32_t delayMs = static_cast<uint32_t>(blowerNotifyDelaySec_) * 1000U;
  if (verifierState != blowerStableState_ &&
      millis() - blowerCandidateStartMs_ >= delayMs) {
    blowerStableState_ = verifierState;
    if (!blowerHasPublished_ || blowerStableState_ != blowerLastPublishedState_) {
      uint8_t value = blowerStableState_ ? 1 : 0;
      if (ubidotsService_.publishBlowersState(value)) {
        blowerLastPublishedState_ = blowerStableState_;
        blowerHasPublished_ = true;
      }
    }
  }

  status_.modemReady = ubidotsService_.isModemReady();
  status_.ubidotsConnected = ubidotsService_.isConnected();
  status_.cloudConnected = ubidotsService_.isDataReady() && status_.ubidotsConnected;
  status_.consoleSubscribed = ubidotsService_.isConsoleSubscribed();
  status_.lastPublishOk = telemetryService_.lastPublishOk();
  ConsoleMessage msg;
  while (ubidotsService_.popConsoleMessage(msg)) {
    if (ubidotsService_.isOtaMode()) {
      continue;
    }
    status_.lastConsoleMessageMs = msg.timestampMs;
    String payload = msg.payload;
    payload.trim();
    String number;
    number.reserve(16);
    bool started = false;
    bool seenDot = false;
    for (size_t i = 0; i < payload.length(); ++i) {
      char c = payload[i];
      if (c >= '0' && c <= '9') {
        number += c;
        started = true;
        continue;
      }
      if (c == '.' && started && !seenDot) {
        number += c;
        seenDot = true;
        continue;
      }
      if (started) {
        break;
      }
    }

    logger_.logf("console", "console raw(len=%u): %s",
                 static_cast<unsigned>(payload.length()), payload.c_str());
    logger_.logf("console", "console token: %s", number.c_str());
    if (number.length() > 0) {
      float raw = number.toFloat();
      int command = static_cast<int>(roundf(raw));
      logger_.logf("console", "console payload: %s", payload.c_str());
      logger_.logf("console", "console value: %.3f -> %d", raw, command);
      if (command == 101) {
        ubidotsService_.publishConsoleValue(200);
      }
    }
  }
}

const DeviceStatus& AppController::status() const {
  return status_;
}

AppState AppController::state() const {
  return state_;
}

void AppController::setState(AppState state) {
  state_ = state;
}
