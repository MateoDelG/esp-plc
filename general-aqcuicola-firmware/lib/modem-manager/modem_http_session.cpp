#include "modem_http_session.h"

#include "modem_manager.h"
#include "modem_parsers.h"
#include "modem_retry.h"

void httpDrainActionUrc(ModemManager& modem) {
  String line;
  uint32_t start = millis();
  while (millis() - start < 200UL) {
    if (!modem.at().waitUrc(UrcType::HttpAction, 20, line)) {
      break;
    }
    modem.logInfo("http", "stale httpaction discarded");
    modem.logDebug("http", String("raw: ") + line);
  }
}

bool httpInit(ModemManager& modem) {
  modem.logInfo("http", "HTTPINIT");
  return modem.at().exec(5000L, GF("+HTTPINIT")).ok;
}

void httpTerm(ModemManager& modem) {
  modem.logInfo("http", "HTTPTERM");
  modem.at().exec(5000L, GF("+HTTPTERM"));
}

bool httpConfigureSsl(ModemManager& modem) {
  if (!modem.at().exec(5000L, GF("+CSSLCFG=\"sslversion\",0,3")).ok) {
    return false;
  }
  if (!modem.at().exec(5000L, GF("+CSSLCFG=\"authmode\",0,0")).ok) {
    return false;
  }
  if (!modem.at().exec(5000L, GF("+CSSLCFG=\"ignorelocaltime\",0,1")).ok) {
    return false;
  }
  if (!modem.at().exec(5000L, GF("+CSSLCFG=\"enableSNI\",0,1")).ok) {
    return false;
  }
  modem.at().exec(5000L, GF("+CSSLCFG=\"negotiatetime\",0,120"));
  modem.logInfo("http", "SSL profile configured");
  return true;
}

bool httpSetUrl(ModemManager& modem, const char* url) {
  modem.logInfo("http", "HTTPPARA URL");
  return modem.at().exec(5000L, GF("+HTTPPARA=\"URL\",\""), url, GF("\""))
      .ok;
}

bool httpSetTimeouts(ModemManager& modem, uint16_t connectTo,
                     uint16_t recvTo) {
  modem.logInfo("http", "HTTPPARA CONNECTTO");
  if (!modem.at().exec(5000L, GF("+HTTPPARA=\"CONNECTTO\","), connectTo).ok) {
    return false;
  }
  modem.logInfo("http", "HTTPPARA RECVTO");
  return modem.at().exec(5000L, GF("+HTTPPARA=\"RECVTO\","), recvTo).ok;
}

bool httpActionGet(ModemManager& modem, uint32_t timeoutMs, int& status,
                   int& length, ModemLogSink logSink) {
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem.logInfo("http", line);
    }
  };

  RetryPolicy policy;
  policy.attempts = 2;
  policy.delayMs = 0;
  policy.label = "httpaction";

  return retry(modem, policy, "http", [&]() {
    if (!modem.at().exec(10000L, GF("+HTTPACTION=0")).ok) {
      return false;
    }
    logLine("waiting action urc");
    String urcLine;
    if (!modem.at().waitUrc(UrcType::HttpAction, timeoutMs, urcLine)) {
      return false;
    }
    if (ModemParsers::parseHttpAction(urcLine, status, length)) {
      return true;
    }
    logLine("parse HTTPACTION failed");
    logLine(String("raw: ") + urcLine);
    return false;
  });
}

bool httpWaitReadFileResult(ModemManager& modem, ModemLogSink logSink,
                            int& errOut, uint32_t timeoutMs) {
  auto logLine = [&](const String& line) {
    if (logSink) {
      logSink(false, line);
    } else {
      modem.logInfo("http", line);
    }
  };
  logLine("waiting for HTTPREADFILE result");
  String line;
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (!modem.at().readLine(line, 500)) {
      continue;
    }
    if (line.length() == 0) {
      continue;
    }
    if (!line.startsWith("+HTTPREADFILE:")) {
      continue;
    }
    int idx = line.indexOf(':');
    if (idx < 0) {
      continue;
    }
    String value = line.substring(idx + 1);
    value.trim();
    errOut = value.toInt();
    logLine(String("HTTPREADFILE result: ") + errOut);
    return true;
  }
  logLine("HTTPREADFILE result timeout");
  return false;
}
