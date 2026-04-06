#pragma once

#include <Arduino.h>

struct TelemetryPacket {
  float levelTank1 = NAN;
  float levelTank2 = NAN;
  float o2Tank1 = NAN;
  float o2Tank2 = NAN;
  float phTank1 = NAN;
  float phTank2 = NAN;
  float tempTank1 = NAN;
  float tempTank2 = NAN;
  int stateBlowers = 1;
};
