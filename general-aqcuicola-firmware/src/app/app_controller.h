#pragma once

#include "app/app_state.h"
#include "comms/wifi/wifi_manager.h"
#include "comms/ubidots/ubidots_service.h"
#include "core/logger.h"
#include "models/device_status.h"
#include "services/acquisition/analog_acquisition_service.h"
#include "services/ota/ota_service.h"
#include "services/ota_modem/ota_modem_service.h"
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
  AnalogAcquisitionService analogService_;
  OtaModemService otaModemService_;
  DeviceStatus status_;
  AppState state_;
  float blowerThresholdA0_ = 0.3f;
  float blowerThresholdA1_ = 0.3f;
  uint16_t blowerNotifyDelaySec_ = 10;
  bool blowerCandidateState_ = false;
  uint32_t blowerCandidateStartMs_ = 0;
  bool blowerStableState_ = false;
  bool blowerLastPublishedState_ = false;
  bool blowerHasPublished_ = false;
};
