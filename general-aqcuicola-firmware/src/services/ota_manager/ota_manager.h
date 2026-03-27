#pragma once

#include <Arduino.h>

class OtaManager {
 public:
  void begin();
  bool installFromSd(const char* path);
};
