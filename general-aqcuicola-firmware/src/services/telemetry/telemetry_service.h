#pragma once

#include <Arduino.h>

#include "comms/ubidots/ubidots_service.h"
#include "core/logger.h"
#include "models/telemetry_packet.h"

class TelemetryService {
 public:
  TelemetryService(Logger& logger, UbidotsService& ubidots);

  void begin();
  void update();
  void setBlowersState(bool state);
  void setPublishIntervalMs(uint32_t intervalMs);
  void updatePhO2FromUart(bool hasTank1, float ph1, float o2_1,
                          bool hasTank2, float ph2, float o2_2);
  void updateLevelTempFromEspNow(bool hasTank1, float level1, float temp1,
                                 bool hasTank2, float level2, float temp2);

  const TelemetryPacket& data() const;
  bool lastPublishOk() const;

 private:
  Logger& logger_;
  UbidotsService& ubidots_;
  TelemetryPacket data_;
  bool lastPublishOk_ = false;
  uint32_t lastPublishMs_ = 0;
  uint32_t publishIntervalMs_ = 0;
};
