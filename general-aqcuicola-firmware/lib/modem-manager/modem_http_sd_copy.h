// HTTP modem file copy to SD helpers
#ifndef MODEM_HTTP_SD_COPY_H
#define MODEM_HTTP_SD_COPY_H

#include <Arduino.h>

#include "modem_types.h"

class ModemManager;

size_t httpClampFsReadChunk(size_t requested);
bool httpCopyModemFileToSd(ModemManager& modem, const char* filename,
                           const char* sdPath, int expectedSize,
                           size_t fsReadChunkSize, ModemLogSink logSink,
                           bool (*sdRecoverFn)(), size_t fsFlushThreshold,
                           bool (*sdRemountFn)());

#endif
