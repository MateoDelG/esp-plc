// Modem file storage helpers
#ifndef MODEM_HTTP_STORAGE_H
#define MODEM_HTTP_STORAGE_H

#include <Arduino.h>

#include "modem_types.h"

class ModemManager;

String httpResolveLocalFilename(const char* path);
bool fsOpenRead(ModemManager& modem, const char* filename, int& handle,
                ModemLogSink logSink);
bool fsClose(ModemManager& modem, int handle);
bool waitFsReadTailOk(ModemManager& modem, ModemLogSink logSink,
                      uint32_t timeoutMs);
bool parseFsReadHeader(const String& line, int& lengthOut);
bool parseFsConnectLine(const String& line, int& lengthOut);

#endif
