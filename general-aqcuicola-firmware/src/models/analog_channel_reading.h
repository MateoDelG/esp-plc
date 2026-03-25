#pragma once

#include <Arduino.h>

struct AnalogChannelReading {
  uint8_t channel = 0;
  int16_t raw = 0;
  float volts = 0.0f;
  uint32_t timestampMs = 0;
  bool valid = false;
};
