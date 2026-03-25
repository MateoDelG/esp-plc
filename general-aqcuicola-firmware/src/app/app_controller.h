#pragma once

#include "app/app_state.h"
#include "comms/wifi/wifi_manager.h"
#include "comms/ubidots/ubidots_service.h"
#include "core/logger.h"
#include "models/device_status.h"
#include "services/ota/ota_service.h"
#include "services/telemetry/telemetry_service.h"
#include "services/console/console_service.h"

class AppController {
 public:
  AppController();

  void begin();
  void update();

  const DeviceStatus& status() const;
  AppState state() const;

 private:
  void setState(AppState state);

  Logger logger_;
  WifiManager wifiManager_;
  OtaService otaService_;
  UbidotsService ubidotsService_;
  TelemetryService telemetryService_;
  ConsoleService consoleService_;
  DeviceStatus status_;
  AppState state_;
};
