// HTTP operations
#ifndef MODEM_HTTP_H
#define MODEM_HTTP_H

#include <Arduino.h>

class ModemManager;

class ModemHttp {
 public:
  explicit ModemHttp(ModemManager& modem);

  bool get(const char* url, uint16_t readLen = 64);

 private:
  ModemManager& modem_;
};

#endif
