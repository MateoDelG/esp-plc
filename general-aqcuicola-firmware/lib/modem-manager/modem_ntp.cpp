#include "modem_ntp.h"

#include <ctime>
#include <cstdio>

#include "modem_manager.h"

ModemNtp::ModemNtp(ModemManager& modem) : modem_(modem) {}

bool ModemNtp::sync(const char* server, int tzQuarterHours, uint32_t timeoutMs,
                    time_t& outEpoch, String* outClock) {
  outEpoch = 0;
  if (!server || strlen(server) == 0) {
    modem_.logWarn("ntp", "server missing");
    return false;
  }

  modem_.logInfo("ntp", "cmee=2");
  modem_.at().exec(2000L, GF("+CMEE=2"));

  String before;
  time_t beforeEpoch = 0;
  int beforeYear = 0;
  if (readClock(before, beforeEpoch, beforeYear)) {
    modem_.logInfo("ntp", String("cclk before: ") + before);
    if (isYearValid(beforeYear)) {
      modem_.logInfo("ntp", "cclk accepted (year ok)");
      outEpoch = beforeEpoch;
      if (outClock) {
        *outClock = before;
      }
      return true;
    }
    modem_.logWarn("ntp", String("cclk invalid year=") + String(beforeYear));
  } else {
    modem_.logWarn("ntp", "cclk before failed");
  }

  modem_.logInfo("ntp", String("cntp config: ") + server);
  AtResult cfg = modem_.at().exec(5000L, GF("+CNTP=\""), server, GF("\","),
                                  tzQuarterHours);
  if (!cfg.ok) {
    modem_.logWarn("ntp", "cntp config failed");
  } else {
    modem_.logInfo("ntp", "cntp start");
    AtResult run = modem_.at().exec(timeoutMs, GF("+CNTP"));
    if (!run.ok) {
      modem_.logWarn("ntp", "cntp failed");
    } else {
      String cntpLine;
      if (!modem_.at().waitUrc(UrcType::Cntp, timeoutMs, cntpLine)) {
        modem_.logWarn("ntp", "cntp urc timeout");
      } else {
        int code = -1;
        int idx = cntpLine.indexOf(':');
        if (idx >= 0) {
          String codeStr = cntpLine.substring(idx + 1);
          codeStr.trim();
          code = codeStr.toInt();
        }
        modem_.logInfo("ntp", String("cntp result=") + String(code));
        if (code == 0) {
          String after;
          time_t epoch = 0;
          int year = 0;
          if (readClock(after, epoch, year)) {
            modem_.logInfo("ntp", String("cclk after: ") + after);
            if (isYearValid(year)) {
              modem_.logInfo("ntp", String("epoch: ") +
                                     String(static_cast<unsigned long>(epoch)));
              outEpoch = epoch;
              if (outClock) {
                *outClock = after;
              }
              return true;
            }
            modem_.logWarn("ntp", String("cclk invalid year=") + String(year));
          } else {
            modem_.logWarn("ntp", "cclk after failed");
          }
        } else {
          modem_.logWarn("ntp", "cntp error");
        }
      }
    }
  }

  if (!ctzuEnabled_) {
    modem_.logInfo("ntp", "ctzu enabled");
    AtResult ctzu = modem_.at().exec(5000L, GF("+CTZU=1"));
    if (ctzu.ok) {
      ctzuEnabled_ = true;
      delay(2000);
    } else {
      modem_.logWarn("ntp", "ctzu failed");
    }
  }

  String after;
  time_t epoch = 0;
  int year = 0;
  if (readClock(after, epoch, year)) {
    modem_.logInfo("ntp", String("cclk after: ") + after);
    if (isYearValid(year)) {
      modem_.logInfo("ntp", String("epoch: ") +
                             String(static_cast<unsigned long>(epoch)));
      outEpoch = epoch;
      if (outClock) {
        *outClock = after;
      }
      return true;
    }
    modem_.logWarn("ntp", String("cclk invalid year=") + String(year));
  } else {
    modem_.logWarn("ntp", "cclk after failed");
  }

  return false;
}

bool ModemNtp::readClock(String& outClock, time_t& outEpoch, int& outYear) {
  AtResult res = modem_.at().exec(3000L, GF("+CCLK?"));
  if (!res.ok) {
    return false;
  }
  return parseClock(res.raw, outClock, outEpoch, outYear);
}

bool ModemNtp::parseClock(const String& raw, String& outClock, time_t& outEpoch,
                          int& outYear) {
  int idx = raw.indexOf("+CCLK:");
  if (idx < 0) {
    return false;
  }
  int firstQuote = raw.indexOf('"', idx);
  if (firstQuote < 0) {
    return false;
  }
  int secondQuote = raw.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) {
    return false;
  }
  String stamp = raw.substring(firstQuote + 1, secondQuote);
  int yy = 0, mo = 0, dd = 0, hh = 0, mm = 0, ss = 0;
  if (sscanf(stamp.c_str(), "%d/%d/%d,%d:%d:%d", &yy, &mo, &dd, &hh, &mm, &ss) < 6) {
    return false;
  }

  outYear = 2000 + yy;
  struct tm tmTime;
  tmTime.tm_year = outYear - 1900;
  tmTime.tm_mon = mo - 1;
  tmTime.tm_mday = dd;
  tmTime.tm_hour = hh;
  tmTime.tm_min = mm;
  tmTime.tm_sec = ss;
  tmTime.tm_isdst = -1;
  time_t epoch = mktime(&tmTime);
  outClock = stamp;
  outEpoch = epoch > 0 ? epoch : 0;
  return true;
}

bool ModemNtp::isYearValid(int year) const {
  return year >= 2026;
}
