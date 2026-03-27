#include "services/pcf_io/pcf_io_service.h"

#include "config/pcf_io_config.h"

PcfIoService::PcfIoService(Logger& logger)
  : logger_(logger), manager_() {}

bool PcfIoService::begin() {
  bool inputOk = manager_.addInputExpander(PCF_INPUT_ADDR);
  bool outputOk = manager_.addOutputExpander(PCF_OUTPUT_ADDR);
  manager_.begin();

  bool outputsLow = manager_.writeAllOutputs(0);
  for (uint8_t i = 0; i < 8; ++i) {
    outputs_[i] = 0;
  }

  ready_ = inputOk && outputOk && (manager_.getLastError() == PcfGpioManager::OK);

  if (!inputOk || !outputOk) {
    logger_.warn("pcf: expander add failed");
  }
  if (!outputsLow) {
    logger_.warn("pcf: outputs low failed");
  }
  if (!ready_) {
    logger_.warn("pcf: init failed");
  } else {
    logger_.info("pcf: ready");
  }
  return ready_;
}

void PcfIoService::update() {}

bool PcfIoService::isReady() const {
  return ready_;
}

bool PcfIoService::readInputs(uint8_t* values, size_t length) {
  if (!values || length < 8) {
    return false;
  }
  for (uint8_t i = 0; i < 8; ++i) {
    int value = manager_.digitalReadInput(i);
    if (value < 0) {
      return false;
    }
    values[i] = static_cast<uint8_t>(value ? 1 : 0);
  }
  return true;
}

bool PcfIoService::getOutputs(uint8_t* values, size_t length) const {
  if (!values || length < 8) {
    return false;
  }
  for (uint8_t i = 0; i < 8; ++i) {
    values[i] = outputs_[i];
  }
  return true;
}

bool PcfIoService::setOutput(uint8_t pin, uint8_t value) {
  if (!ready_) {
    return false;
  }
  if (pin >= 8) {
    return false;
  }
  uint8_t v = value ? 1 : 0;
  if (!manager_.digitalWriteOutput(pin, v)) {
    return false;
  }
  outputs_[pin] = v;
  return true;
}
