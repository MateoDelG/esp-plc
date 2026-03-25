// HTTP session helpers
#ifndef MODEM_HTTP_SESSION_H
#define MODEM_HTTP_SESSION_H

#include <Arduino.h>

#include "modem_types.h"

class ModemManager;

void httpDrainActionUrc(ModemManager& modem);
bool httpInit(ModemManager& modem);
void httpTerm(ModemManager& modem);
bool httpConfigureSsl(ModemManager& modem);
bool httpSetUrl(ModemManager& modem, const char* url);
bool httpSetTimeouts(ModemManager& modem, uint16_t connectTo,
                     uint16_t recvTo);
bool httpActionGet(ModemManager& modem, uint32_t timeoutMs, int& status,
                   int& length, ModemLogSink logSink);
bool httpWaitReadFileResult(ModemManager& modem, ModemLogSink logSink,
                            int& errOut, uint32_t timeoutMs);

#endif
