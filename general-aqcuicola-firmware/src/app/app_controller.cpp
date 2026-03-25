#include "app/app_controller.h"

#include "config/app_config.h"

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
}

void AppController::update() {
  if (status_.wifiConnected && otaService_.isReady()) {
    otaService_.update();
  }

  consoleService_.update();

  telemetryService_.update();
  consoleService_.setTelemetry(telemetryService_.data());
  analogService_.update();
  consoleService_.setAnalogSnapshot(analogService_.data());

  status_.modemReady = ubidotsService_.isModemReady();
  status_.ubidotsConnected = ubidotsService_.isConnected();
  status_.cloudConnected = ubidotsService_.isDataReady() && status_.ubidotsConnected;
  status_.consoleSubscribed = ubidotsService_.isConsoleSubscribed();
  status_.lastPublishOk = telemetryService_.lastPublishOk();
  if (ubidotsService_.hasNewConsoleMessage()) {
    status_.lastConsoleMessageMs = ubidotsService_.latestConsoleMessage().timestampMs;
    ubidotsService_.ackConsoleMessage();
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
