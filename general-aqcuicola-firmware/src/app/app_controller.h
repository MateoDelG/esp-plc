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
#include "comms/uart1_master/uart1_master.h"
#include "services/pcf_io/pcf_io_service.h"
#include "services/espnow/espnow_service.h"
#include "config/dashboard_config.h"

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
  Uart1Master uart1Master_;
  PcfIoService pcfIoService_;
  EspNowService espNowService_;
  DashboardConfig dashboardConfig_;
  DeviceStatus status_;
  AppState state_;
  float blowerThresholdA0_ = 0.5f;
  float blowerThresholdA1_ = 0.5f;
  uint16_t blowerNotifyDelaySec_ = 10;
  bool blowerCandidateState_ = false;
  uint32_t blowerCandidateStartMs_ = 0;
  bool blowerStableState_ = false;
  bool blowerLastPublishedState_ = false;
  bool blowerHasPublished_ = false;
  bool blowerAlarmEnabled_ = false;
  bool blowerAlarmOutput_ = false;
  bool blowerAlarmCycleActive_ = false;
  bool blowerAlarmPulseOn_ = false;
  uint32_t blowerAlarmPhaseStartMs_ = 0;
  bool uartAutoEnabled_ = false;
  uint32_t uartAutoIntervalMs_ = 5U * 60U * 1000U;
  uint32_t uartAutoLastMs_ = 0;
  bool espNowAutoEnabled_ = false;
  uint32_t espNowAutoIntervalMs_ = 5U * 60U * 1000U;
  uint32_t espNowAutoLastMs_ = 0;
};
