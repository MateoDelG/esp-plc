#include "modem_data_session.h"

#include "modem_manager.h"
#include "modem_parsers.h"

ModemDataSession::ModemDataSession(ModemManager& modem) : modem_(modem) {}

bool ModemDataSession::isNetOpenNow() {
  AtResult res = modem_.at().exec(3000L, GF("+NETOPEN?"));
  int status = ModemParsers::parseNetOpenStatus(res.raw);
  if (status == 1) {
    return true;
  }
  if (status == 0) {
    return false;
  }

  String extra = modem_.at().readRaw(1000L);
  String full = res.raw + extra;
  status = ModemParsers::parseNetOpenStatus(full);
  return status == 1;
}

bool ModemDataSession::waitNetOpen(uint32_t timeoutMs) {
  uint32_t start = millis();
  uint8_t consecutiveOk = 0;

  while (millis() - start < timeoutMs) {
    if (isNetOpenNow()) {
      consecutiveOk++;
      if (consecutiveOk >= 2) {
        modem_.logLine("[data] netopen active");
        return true;
      }
    } else {
      consecutiveOk = 0;
    }
    delay(300);
  }

  modem_.setLastError(-20, "netopen timeout");
  modem_.logLine("[data] netopen timeout");
  return false;
}

bool ModemDataSession::ensureNetOpen() {
  const ModemConfig& config = modem_.config();
  if (!config.apn.apn || strlen(config.apn.apn) == 0) {
    modem_.setLastError(-21, "apn not set");
    modem_.logLine("[data] apn not set");
    return false;
  }

  if (isNetOpenNow()) {
    modem_.logLine("[data] NETOPEN already active");
    return true;
  }

  modem_.at().exec(5000L, GF("+CGDCONT=1,\"IP\",\""), config.apn.apn,
                   GF("\""));

  modem_.logLine("[data] NETOPEN start");
  AtResult res = modem_.at().exec(8000L, GF("+NETOPEN"));
  if (ModemParsers::responseHasAlreadyOpened(res.raw)) {
    modem_.logLine("[data] NETOPEN already opened");
    return waitNetOpen(8000UL);
  }

  return waitNetOpen(15000UL);
}

bool ModemDataSession::hasDataSession() {
  bool attached = false;
  String ip;

  AtResult att = modem_.at().exec(2000L, GF("+CGATT?"));
  if (att.ok) {
    attached = ModemParsers::parseCgattAttached(att.raw);
  }

  AtResult addr = modem_.at().exec(2000L, GF("+CGPADDR=1"));
  if (addr.ok) {
    ModemParsers::parseCgpaddrIp(addr.raw, ip);
  }

  bool netOpen = isNetOpenNow();
  return attached && ip.length() > 0 && netOpen;
}

bool ModemDataSession::ensureDataSession() {
  const ModemConfig& config = modem_.config();
  if (!config.apn.apn || strlen(config.apn.apn) == 0) {
    modem_.setLastError(-21, "apn not set");
    modem_.logLine("[data] apn not set");
    return false;
  }

  modem_.logValue("[data] apn", config.apn.apn);

  if (hasDataSession()) {
    modem_.logLine("[data] data already ready");
    modem_.setState(ModemState::DataReady);
    return true;
  }

  modem_.at().exec(5000L, GF("+CGDCONT=1,\"IP\",\""), config.apn.apn,
                   GF("\""));

  if (!ensureNetOpen()) {
    return false;
  }

  if (hasDataSession()) {
    modem_.setState(ModemState::DataReady);
    return true;
  }

  modem_.setLastError(-22, "data session not ready");
  return false;
}
