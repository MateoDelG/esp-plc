// Core modem operations
#ifndef MODEM_CORE_H
#define MODEM_CORE_H

#include <Arduino.h>

#include "modem_types.h"

class ModemManager;

class ModemCore {
 public:
  explicit ModemCore(ModemManager& modem);

  bool powerOn();
  bool powerOff();
  bool restart();

  bool begin();
  bool waitForNetwork();

  String getCpsiInfo();
  int16_t getSignalStrengthPercent();
  ModemInfo getNetworkInfo();

 private:
  ModemManager& modem_;
};

#endif
