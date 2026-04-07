// NTP sync via modem AT commands
#ifndef MODEM_NTP_H
#define MODEM_NTP_H

#include <Arduino.h>

class ModemManager;

class ModemNtp {
 public:
  explicit ModemNtp(ModemManager& modem);

  bool sync(const char* server, int tzQuarterHours, uint32_t timeoutMs,
            time_t& outEpoch, String* outClock);

 private:
  bool readClock(String& outClock, time_t& outEpoch, int& outYear);
  bool parseClock(const String& raw, String& outClock, time_t& outEpoch,
                  int& outYear);
  bool isYearValid(int year) const;

  ModemManager& modem_;
  bool ctzuEnabled_ = false;
};

#endif
