#include "app/app_controller.h"

#include "config/app_config.h"
#include "config/ota_modem_codes.h"

#include <cmath>

AppController::AppController()
  : logger_(),
    wifiManager_(logger_),
    otaService_(logger_),
    ubidotsService_(logger_),
    telemetryService_(logger_, ubidotsService_),
    consoleService_(),
    analogService_(logger_),
    otaModemService_(logger_),
    uart1Master_(logger_),
    pcfIoService_(logger_),
    espNowService_(logger_),
    timeService_(logger_, ubidotsService_),
    sdLoggerService_(logger_),
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

  if (!loadDashboardConfig(dashboardConfig_, &logger_)) {
    logger_.warn("cfg: dashboard defaults");
  }
  blowerThresholdA0_ = dashboardConfig_.blowerThresholdA0;
  blowerThresholdA1_ = dashboardConfig_.blowerThresholdA1;
  blowerNotifyDelaySec_ = dashboardConfig_.blowerNotifyDelaySec;
  blowerAlarmEnabled_ = dashboardConfig_.blowerAlarmEnabled;
  uartAutoEnabled_ = dashboardConfig_.uartAutoEnabled;
  uartAutoIntervalMs_ =
    static_cast<uint32_t>(dashboardConfig_.uartAutoIntervalMin) * 60000U;
  espNowAutoEnabled_ = dashboardConfig_.espNowAutoEnabled;
  espNowAutoIntervalMs_ =
    static_cast<uint32_t>(dashboardConfig_.espNowAutoIntervalMin) * 60000U;
  telemetryService_.setPublishIntervalMs(
    static_cast<uint32_t>(dashboardConfig_.ubidotsPublishIntervalMin) * 60000U);
  analogService_.setEnabledMask(dashboardConfig_.adsEnabledMask);

  consoleService_.begin();
  ConsoleService::setActive(&consoleService_);
  logger_.setSink(ConsoleService::logSink);
  consoleService_.setLogger(&logger_);
  consoleService_.setDashboardConfig(&dashboardConfig_);
  consoleService_.setTelemetryService(&telemetryService_);
  consoleService_.setTimeService(&timeService_);
  consoleService_.setAnalogControl(&analogService_);
  consoleService_.setBlowerThresholdRefs(&blowerThresholdA0_, &blowerThresholdA1_);
  consoleService_.setBlowerDelayRef(&blowerNotifyDelaySec_);
  consoleService_.setUartMaster(&uart1Master_);
  consoleService_.setPcfIoService(&pcfIoService_);
  consoleService_.setBlowerAlarmRef(&blowerAlarmEnabled_);
  uart1Master_.setTelemetryService(&telemetryService_);
  uart1Master_.setSdLogger(&sdLoggerService_);
  consoleService_.setEspNowService(&espNowService_);
  consoleService_.setUartAutoRefs(&uartAutoEnabled_, &uartAutoIntervalMs_,
                                  &uartAutoLastMs_);
  consoleService_.setEspNowAutoRefs(&espNowAutoEnabled_, &espNowAutoIntervalMs_,
                                    &espNowAutoLastMs_);
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

  timeService_.begin();
  sdLoggerService_.begin();
  sdLoggerService_.setTimeService(&timeService_);


  otaModemService_.setModem(&ubidotsService_.modem());
  otaModemService_.setUbidots(&ubidotsService_);

  telemetryService_.begin();
  analogService_.begin();
  pcfIoService_.begin();
  uart1Master_.begin();
  espNowService_.setTelemetryService(&telemetryService_);
  espNowService_.setSdLogger(&sdLoggerService_);
  espNowService_.begin();
  setState(AppState::Running);
  logger_.info("version: 1.10");

}

void AppController::update() {
  ubidotsService_.update();
  if (status_.wifiConnected && otaService_.isReady()) {
    otaService_.update();
  }

  consoleService_.update();

  pcfIoService_.update();
  espNowService_.update();

  telemetryService_.update();
  timeService_.update();
  consoleService_.setTelemetry(telemetryService_.data());
  analogService_.update();
  consoleService_.setAnalogSnapshot(analogService_.data());

  const AnalogSnapshot& analog = analogService_.data();
  bool a0Enabled = (analog.enabledMask & 0x01) != 0;
  bool a1Enabled = (analog.enabledMask & 0x02) != 0;
  const AnalogChannelReading& ch0 = analog.channels[0];
  const AnalogChannelReading& ch1 = analog.channels[1];

  bool a0Ok = a0Enabled && ch0.valid && ch0.volts >= blowerThresholdA0_;
  bool a1Ok = a1Enabled && ch1.valid && ch1.volts >= blowerThresholdA1_;
  uint8_t activeCount = static_cast<uint8_t>((a0Enabled ? 1 : 0) + (a1Enabled ? 1 : 0));
  uint8_t okCount = static_cast<uint8_t>((a0Ok ? 1 : 0) + (a1Ok ? 1 : 0));
  bool verifierState = (activeCount > 0) && (okCount == activeCount);
  bool belowThreshold = (activeCount > 0) && (okCount != activeCount);
  telemetryService_.setBlowersState(verifierState);
  consoleService_.setBlowerStatus(verifierState, belowThreshold);

  uint32_t delayMs = static_cast<uint32_t>(blowerNotifyDelaySec_) * 1000U;
  if (uartAutoEnabled_ && uartAutoIntervalMs_ > 0) {
    uint32_t now = millis();
    if (now - uartAutoLastMs_ >= uartAutoIntervalMs_) {
      if (uart1Master_.enqueue(Uart1Master::Op::GetLast) &&
          uart1Master_.enqueue(Uart1Master::Op::AutoMeasure)) {
        uartAutoLastMs_ = now;
      }
    }
  }

  if (espNowAutoEnabled_ && espNowAutoIntervalMs_ > 0) {
    uint32_t now = millis();
    if (now - espNowAutoLastMs_ >= espNowAutoIntervalMs_) {
      bool sent = false;
      if (espNowService_.requestTank(1)) {
        sent = true;
      }
      if (espNowService_.requestTank(2)) {
        sent = true;
      }
      if (sent) {
        espNowAutoLastMs_ = now;
      }
    }
  }

  if (pcfIoService_.isReady()) {
    bool alarmOn = blowerAlarmEnabled_ && !verifierState &&
      (millis() - blowerCandidateStartMs_ >= delayMs);
    uint32_t nowMs = millis();

    if (alarmOn != blowerAlarmCycleActive_) {
      blowerAlarmCycleActive_ = alarmOn;
      blowerAlarmPulseOn_ = alarmOn;
      blowerAlarmPhaseStartMs_ = nowMs;
      if (pcfIoService_.setOutput(0, alarmOn ? 1 : 0)) {
        blowerAlarmOutput_ = alarmOn;
      }
    }

    if (blowerAlarmCycleActive_) {
      uint32_t elapsed = nowMs - blowerAlarmPhaseStartMs_;
      if (blowerAlarmPulseOn_) {
        if (elapsed >= 10000U) {
          blowerAlarmPulseOn_ = false;
          blowerAlarmPhaseStartMs_ = nowMs;
        }
      } else {
        if (elapsed >= 25000U) {
          blowerAlarmPulseOn_ = true;
          blowerAlarmPhaseStartMs_ = nowMs;
        }
      }

      if (blowerAlarmOutput_ != blowerAlarmPulseOn_) {
        if (pcfIoService_.setOutput(0, blowerAlarmPulseOn_ ? 1 : 0)) {
          blowerAlarmOutput_ = blowerAlarmPulseOn_;
        }
      }
    }
  }

  if (verifierState != blowerCandidateState_) {
    blowerCandidateState_ = verifierState;
    blowerCandidateStartMs_ = millis();
  }
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
      if (command == kOtaTriggerCode) {
        if (!otaModemService_.start()) {
          logger_.warn("ota-modem: already running");
        }
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
