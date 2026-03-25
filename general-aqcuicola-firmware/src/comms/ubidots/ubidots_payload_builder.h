#pragma once

#include <Arduino.h>

#include "models/telemetry_packet.h"

class UbidotsPayloadBuilder {
 public:
  static String build(const TelemetryPacket& data);
};
