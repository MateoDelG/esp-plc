// HTTP operations
#ifndef MODEM_HTTP_H
#define MODEM_HTTP_H

#include <Arduino.h>

#include "modem_types.h"

class ModemManager;

class ModemHttp {
 public:
  explicit ModemHttp(ModemManager& modem);

  bool get(const char* url, uint16_t readLen = 64);
  bool downloadToFile(const char* url, const char* sdPath,
                      uint16_t chunkSize = 256,
                      ModemLogSink logSink = nullptr,
                      bool (*sdRecoverFn)() = nullptr);
  bool lastDownloadFailedAfterTemp() const {
    return lastDownloadFailedAfterTemp_;
  }
  int lastHttpLength() const { return lastHttpLength_; }
  bool verifyFileOnSd(const char* path, size_t expectedSize,
                      size_t previewBytes = 128,
                      ModemLogSink logSink = nullptr);

 private:
  ModemManager& modem_;
  int lastHttpLength_ = -1;
  bool lastDownloadFailedAfterTemp_ = false;
};

#endif
