#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <PCF8574.h>

class PcfGpioManager {
public:
  enum Error {
    OK = 0,
    ERR_MAX_EXPANDERS,
    ERR_INIT_FAILED,
    ERR_INPUT_OUT_OF_RANGE,
    ERR_OUTPUT_OUT_OF_RANGE,
    ERR_NOT_READY
  };

  explicit PcfGpioManager(TwoWire *wire = &Wire, int sda = -1, int scl = -1);

  void begin();

  bool addInputExpander(uint8_t address);
  bool addOutputExpander(uint8_t address);

  int digitalReadInput(uint16_t inputPin);

  bool digitalWriteOutput(uint16_t outputPin, uint8_t value);
  bool toggleOutput(uint16_t outputPin);
  bool writeAllOutputs(uint8_t value);

  uint16_t getInputCount() const;
  uint16_t getOutputCount() const;
  uint8_t getInputExpanderCount() const;
  uint8_t getOutputExpanderCount() const;

  Error getLastError() const;

private:
  struct Expander {
    uint8_t address = 0;
    PCF8574 *dev = nullptr;
    bool initialized = false;
    uint8_t outputState = 0;
  };

  static const uint8_t MAX_INPUT_EXPANDERS = 8;
  static const uint8_t MAX_OUTPUT_EXPANDERS = 8;

  Expander _inputs[MAX_INPUT_EXPANDERS];
  Expander _outputs[MAX_OUTPUT_EXPANDERS];
  uint8_t _inputCount = 0;
  uint8_t _outputCount = 0;
  bool _begun = false;

  TwoWire *_wire = nullptr;
  int _sda = -1;
  int _scl = -1;
  Error _lastError = OK;

  bool initExpander(Expander &exp, bool outputMode);
  bool validateInputPin(uint16_t pin, uint8_t &expIdx, uint8_t &bit);
  bool validateOutputPin(uint16_t pin, uint8_t &expIdx, uint8_t &bit);
  void setError(Error e);
};
