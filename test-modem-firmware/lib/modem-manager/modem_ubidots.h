// Ubidots helpers
#ifndef MODEM_UBIDOTS_H
#define MODEM_UBIDOTS_H

#include <Arduino.h>

class ModemManager;

class ModemUbidots {
 public:
  explicit ModemUbidots(ModemManager& modem);

  bool connect(const char* token, const char* clientId = "esp001");
  bool publishValue(const char* token, const char* deviceLabel,
                    const char* variableLabel, float value,
                    bool disconnectAfter = true);
  bool subscribeVariable(const char* deviceLabel, const char* variableLabel);
  bool pollVariable(const char* deviceLabel, const char* variableLabel,
                    String& valueOut);

 private:
  ModemManager& modem_;
};

#endif
