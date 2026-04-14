#ifndef MODEM_SMS_H
#define MODEM_SMS_H

#include <Arduino.h>

#include "modem_types.h"

class ModemManager;

class SmsHandler {
 public:
  explicit SmsHandler(ModemManager& modem);

  bool begin();

  bool parseIncoming(const String& line, String& outText);

  static bool isResetCommand(const String& text);
  static bool isUpdateCommand(const String& text);

 private:
  ModemManager& modem_;
};

#endif
