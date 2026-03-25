#include "PcfGpioManager.h"

PcfGpioManager::PcfGpioManager(TwoWire *wire, int sda, int scl)
  : _wire(wire), _sda(sda), _scl(scl) {}

void PcfGpioManager::begin() {
  _begun = true;
  for (uint8_t i = 0; i < _inputCount; i++) {
    initExpander(_inputs[i], false);
  }
  for (uint8_t i = 0; i < _outputCount; i++) {
    initExpander(_outputs[i], true);
  }
}

bool PcfGpioManager::addInputExpander(uint8_t address) {
  if (_inputCount >= MAX_INPUT_EXPANDERS) {
    setError(ERR_MAX_EXPANDERS);
    return false;
  }

  Expander &exp = _inputs[_inputCount];
  exp.address = address;
  #if defined(ESP32)
  if (_sda >= 0 && _scl >= 0) {
    exp.dev = new PCF8574(_wire, address, _sda, _scl);
  } else {
    exp.dev = new PCF8574(_wire, address);
  }
  #else
  exp.dev = new PCF8574(_wire, address);
  #endif
  _inputCount++;

  if (_begun) {
    return initExpander(exp, false);
  }
  return true;
}

bool PcfGpioManager::addOutputExpander(uint8_t address) {
  if (_outputCount >= MAX_OUTPUT_EXPANDERS) {
    setError(ERR_MAX_EXPANDERS);
    return false;
  }

  Expander &exp = _outputs[_outputCount];
  exp.address = address;
  #if defined(ESP32)
  if (_sda >= 0 && _scl >= 0) {
    exp.dev = new PCF8574(_wire, address, _sda, _scl);
  } else {
    exp.dev = new PCF8574(_wire, address);
  }
  #else
  exp.dev = new PCF8574(_wire, address);
  #endif
  _outputCount++;

  if (_begun) {
    return initExpander(exp, true);
  }
  return true;
}

bool PcfGpioManager::initExpander(Expander &exp, bool outputMode) {
  if (!exp.dev) {
    setError(ERR_INIT_FAILED);
    exp.initialized = false;
    return false;
  }

  for (uint8_t pin = 0; pin < 8; pin++) {
    if (outputMode) {
      exp.dev->pinMode(pin, OUTPUT, LOW);
    } else {
      exp.dev->pinMode(pin, INPUT);
    }
  }

  if (!exp.dev->begin()) {
    setError(ERR_INIT_FAILED);
    exp.initialized = false;
    return false;
  }

  if (outputMode) {
    for (uint8_t pin = 0; pin < 8; pin++) {
      exp.dev->digitalWrite(pin, LOW);
    }
    exp.outputState = 0;
  }

  exp.initialized = true;
  setError(OK);
  return true;
}

int PcfGpioManager::digitalReadInput(uint16_t inputPin) {
  uint8_t expIdx = 0;
  uint8_t bit = 0;
  if (!validateInputPin(inputPin, expIdx, bit)) return -1;

  Expander &exp = _inputs[expIdx];
  if (!exp.initialized) {
    setError(ERR_NOT_READY);
    return -1;
  }

  uint8_t value = exp.dev->digitalRead(bit);
  setError(OK);
  return value ? 1 : 0;
}

bool PcfGpioManager::digitalWriteOutput(uint16_t outputPin, uint8_t value) {
  uint8_t expIdx = 0;
  uint8_t bit = 0;
  if (!validateOutputPin(outputPin, expIdx, bit)) return false;

  Expander &exp = _outputs[expIdx];
  if (!exp.initialized) {
    setError(ERR_NOT_READY);
    return false;
  }

  uint8_t mask = (uint8_t)(1 << bit);
  if (value) {
    exp.outputState |= mask;
    exp.dev->digitalWrite(bit, HIGH);
  } else {
    exp.outputState &= (uint8_t)~mask;
    exp.dev->digitalWrite(bit, LOW);
  }

  setError(OK);
  return true;
}

bool PcfGpioManager::toggleOutput(uint16_t outputPin) {
  uint8_t expIdx = 0;
  uint8_t bit = 0;
  if (!validateOutputPin(outputPin, expIdx, bit)) return false;

  Expander &exp = _outputs[expIdx];
  if (!exp.initialized) {
    setError(ERR_NOT_READY);
    return false;
  }

  uint8_t mask = (uint8_t)(1 << bit);
  exp.outputState ^= mask;
  exp.dev->digitalWrite(bit, (exp.outputState & mask) ? HIGH : LOW);

  setError(OK);
  return true;
}

bool PcfGpioManager::writeAllOutputs(uint8_t value) {
  if (_outputCount == 0) {
    setError(ERR_OUTPUT_OUT_OF_RANGE);
    return false;
  }

  uint8_t v = value ? 0xFF : 0x00;
  for (uint8_t i = 0; i < _outputCount; i++) {
    Expander &exp = _outputs[i];
    if (!exp.initialized) {
      setError(ERR_NOT_READY);
      return false;
    }
    for (uint8_t pin = 0; pin < 8; pin++) {
      exp.dev->digitalWrite(pin, value ? HIGH : LOW);
    }
    exp.outputState = v;
  }

  setError(OK);
  return true;
}

uint16_t PcfGpioManager::getInputCount() const {
  return (uint16_t)(_inputCount * 8);
}

uint16_t PcfGpioManager::getOutputCount() const {
  return (uint16_t)(_outputCount * 8);
}

uint8_t PcfGpioManager::getInputExpanderCount() const {
  return _inputCount;
}

uint8_t PcfGpioManager::getOutputExpanderCount() const {
  return _outputCount;
}

PcfGpioManager::Error PcfGpioManager::getLastError() const {
  return _lastError;
}

bool PcfGpioManager::validateInputPin(uint16_t pin, uint8_t &expIdx, uint8_t &bit) {
  uint16_t total = getInputCount();
  if (pin >= total) {
    setError(ERR_INPUT_OUT_OF_RANGE);
    return false;
  }
  expIdx = (uint8_t)(pin / 8);
  bit = (uint8_t)(pin % 8);
  return true;
}

bool PcfGpioManager::validateOutputPin(uint16_t pin, uint8_t &expIdx, uint8_t &bit) {
  uint16_t total = getOutputCount();
  if (pin >= total) {
    setError(ERR_OUTPUT_OUT_OF_RANGE);
    return false;
  }
  expIdx = (uint8_t)(pin / 8);
  bit = (uint8_t)(pin % 8);
  return true;
}

void PcfGpioManager::setError(Error e) {
  _lastError = e;
}
