// AT command utility
#ifndef MODEM_AT_H
#define MODEM_AT_H

#include <Arduino.h>
#include <FS.h>

#include "modem_manager_config.h"

#include <TinyGsmClient.h>

#include "modem_types.h"
#include "modem_urc.h"

class ModemManager;

class AtClient {
 public:
  AtClient(TinyGsm& modem, Stream& stream, UrcStore& urcStore,
           ModemManager* manager = nullptr);

  template <typename... Args>
  AtResult exec(uint32_t timeoutMs, Args... args) {
    modem_.sendAT(args...);
    AtResult result = waitOk(timeoutMs);
    return result;
  }

  template <typename... Args>
  bool execPromptedData(uint32_t timeoutMs, const char* data, size_t length,
                        Args... args) {
    modem_.sendAT(args...);
    if (!waitForPrompt(timeoutMs)) {
      return false;
    }
    stream_.write(reinterpret_cast<const uint8_t*>(data), length);
    AtResult result = waitOk(timeoutMs);
    return result.ok;
  }

  AtResult waitOk(uint32_t timeoutMs);
  bool waitForPrompt(uint32_t timeoutMs);
  bool waitUrc(UrcType type, uint32_t timeoutMs, String& lineOut);

  bool readLine(String& lineOut, uint32_t timeoutMs);
  bool readLineNonBlocking(String& lineOut);
  String readRaw(uint32_t timeoutMs);

  bool readExact(size_t length, String& out, uint32_t timeoutMs);
  bool readExactToFile(size_t length, File& file, uint32_t timeoutMs);
  bool readExactToBuffer(uint8_t* buffer, size_t length, uint32_t timeoutMs);

 private:
  TinyGsm& modem_;
  Stream& stream_;
  UrcStore& urcStore_;
  ModemManager* manager_ = nullptr;
  String lineBuffer_;
};

#endif
