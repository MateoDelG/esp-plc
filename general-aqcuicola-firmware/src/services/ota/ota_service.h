#pragma once

#include <Arduino.h>

#include "core/logger.h"

class OtaService {
 public:
  explicit OtaService(Logger& logger);

  bool begin();
  void update();
  bool isReady() const;

 private:
  Logger& logger_;
  bool ready_ = false;
};
