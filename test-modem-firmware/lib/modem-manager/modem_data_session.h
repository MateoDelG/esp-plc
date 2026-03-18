// Data session helpers
#ifndef MODEM_DATA_SESSION_H
#define MODEM_DATA_SESSION_H

#include <Arduino.h>

class ModemManager;

class ModemDataSession {
 public:
  explicit ModemDataSession(ModemManager& modem);

  bool ensureDataSession();
  bool hasDataSession();

  bool ensureNetOpen();
  bool isNetOpenNow();

 private:
  bool waitNetOpen(uint32_t timeoutMs);

  ModemManager& modem_;
};

#endif
