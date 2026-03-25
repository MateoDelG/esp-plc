#pragma once

#include "models/analog_channel_reading.h"

struct AnalogSnapshot {
  AnalogChannelReading channels[4];
  bool adsConnected = false;
  uint8_t enabledMask = 0;
};
