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

  const TelemetryPacket& data() const;
  bool lastPublishOk() const;

 private:
  Logger& logger_;
  UbidotsService& ubidots_;
  TelemetryPacket data_;
  bool lastPublishOk_ = false;
  uint32_t lastPublishMs_ = 0;
};
