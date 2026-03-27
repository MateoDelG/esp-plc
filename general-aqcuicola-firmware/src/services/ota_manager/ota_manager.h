#pragma once

#include <Arduino.h>

class OtaManager {
 public:
  void begin();
  using OtaStageCallback = void (*)(void* context);
  bool installFromSd(const char* path, OtaStageCallback afterWriteCb = nullptr,
                     void* context = nullptr);
};
