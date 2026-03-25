#include "modem_data_session.h"

#include "modem_manager.h"
#include "modem_parsers.h"
#include "modem_retry.h"

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
        modem_.logInfo("data", "netopen active");
        modem_.transitionTo(ModemState::NetOpenReady, "netopen active");
        return true;
      }
    } else {
      consecutiveOk = 0;
    }
    modem_.idle(300);
  }

  return modem_.fail("data", ModemErrorCode::NetOpenTimeout,
                     "netopen timeout", modem_.state());
}

bool ModemDataSession::ensureNetOpen() {
  const ModemConfig& config = modem_.config();
  if (!config.apn.apn || strlen(config.apn.apn) == 0) {
    return modem_.fail("data", ModemErrorCode::InvalidArgument, "apn not set",
                       modem_.state());
  }

  if (isNetOpenNow()) {
    modem_.logInfo("data", "NETOPEN already active");
    modem_.transitionTo(ModemState::NetOpenReady, "netopen already active");
    return true;
  }

  modem_.at().exec(5000L, GF("+CGDCONT=1,\"IP\",\""), config.apn.apn,
                   GF("\""));

  modem_.logInfo("data", "NETOPEN start");
  RetryPolicy policy;
  policy.attempts = 2;
  policy.delayMs = 0;
  policy.label = "netopen";

  bool ok = retry(modem_, policy, "data", [&]() {
    AtResult res = modem_.at().exec(8000L, GF("+NETOPEN"));
    if (ModemParsers::responseHasAlreadyOpened(res.raw)) {
      modem_.logInfo("data", "NETOPEN already opened");
      modem_.transitionTo(ModemState::NetOpenReady, "netopen already opened");
      return true;
    }

    String urcLine;
    if (modem_.at().waitUrc(UrcType::NetOpen, 15000UL, urcLine)) {
      int status = ModemParsers::parseNetOpenStatus(urcLine);
      if (status == 1) {
        modem_.logInfo("data", "netopen active");
        modem_.transitionTo(ModemState::NetOpenReady, "netopen urc active");
        return true;
      }
    }
    return false;
  });

  if (!ok) {
    return modem_.fail("data", ModemErrorCode::NetOpenTimeout,
                       "netopen urc timeout", modem_.state());
  }
  return true;
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
    return modem_.fail("data", ModemErrorCode::InvalidArgument, "apn not set",
                       modem_.state());
  }

  modem_.logInfo("data", String("apn: ") + config.apn.apn);

  if (hasDataSession()) {
    modem_.logInfo("data", "data already ready");
    modem_.transitionTo(ModemState::DataSessionReady, "data already ready");
    return true;
  }

  modem_.at().exec(5000L, GF("+CGDCONT=1,\"IP\",\""), config.apn.apn,
                   GF("\""));

  if (!ensureNetOpen()) {
    return false;
  }

  if (hasDataSession()) {
    modem_.transitionTo(ModemState::DataSessionReady, "data session ready");
    return true;
  }

  return modem_.fail("data", ModemErrorCode::DataSessionFailed,
                     "data session not ready", modem_.state());
}
