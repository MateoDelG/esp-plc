#pragma once

#include <Arduino.h>

#include "PcfGpioManager.h"
#include "core/logger.h"

class PcfIoService {
 public:
  explicit PcfIoService(Logger& logger);

  bool begin();
  void update();
  bool isReady() const;

  bool readInputs(uint8_t* values, size_t length);
  bool getOutputs(uint8_t* values, size_t length) const;
  bool setOutput(uint8_t pin, uint8_t value);

 private:
  Logger& logger_;
  PcfGpioManager manager_;
  bool ready_ = false;
  uint8_t outputs_[8] = {0};
};
