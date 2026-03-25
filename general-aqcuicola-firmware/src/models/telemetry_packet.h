#pragma once

struct TelemetryPacket {
  float levelTank1 = 50.0f;
  float levelTank2 = 60.0f;
  float o2Tank1 = 7.1f;
  float o2Tank2 = 7.3f;
  float phTank1 = 7.0f;
  float phTank2 = 7.2f;
  float tempTank1 = 24.5f;
  float tempTank2 = 25.1f;
  bool stateBlowers = true;
};
