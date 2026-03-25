#include "modem_core.h"

#include "modem_manager.h"
#include "modem_parsers.h"
#include "modem_retry.h"

ModemCore::ModemCore(ModemManager& modem) : modem_(modem) {}

bool ModemCore::powerOn() {
  const ModemConfig& config = modem_.config();
  if (config.pins.pwrKey < 0) {
    return true;
  }

  pinMode(config.pins.pwrKey, OUTPUT);
  digitalWrite(config.pins.pwrKey, LOW);
  modem_.idle(1000);
  digitalWrite(config.pins.pwrKey, HIGH);
  return true;
}

bool ModemCore::powerOff() {
  const ModemConfig& config = modem_.config();
  if (config.pins.pwrKey < 0) {
    return true;
  }

  pinMode(config.pins.pwrKey, OUTPUT);
  digitalWrite(config.pins.pwrKey, LOW);
  modem_.idle(1500);
  digitalWrite(config.pins.pwrKey, HIGH);
  return true;
}

bool ModemCore::restart() {
  powerOff();
  modem_.idle(1000);
  powerOn();
  return true;
}

bool ModemCore::begin() {
  modem_.transitionTo(ModemState::Booting, "begin");
  modem_.logInfo("core", "init start");

  modem_.transitionTo(ModemState::PoweringOn, "power on");
  powerOn();

  ModemConfig& config = modem_.config();
  if (config.pins.rx >= 0 && config.pins.tx >= 0) {
    config.serialAT.begin(config.baud, SERIAL_8N1, config.pins.rx,
                          config.pins.tx);
  } else {
    config.serialAT.begin(config.baud);
  }

  if (config.initDelayMs > 0) {
    modem_.idle(config.initDelayMs);
  }

  RetryPolicy initPolicy;
  initPolicy.attempts = 3;
  initPolicy.delayMs = 0;
  initPolicy.label = "modem init";

  bool inited = retry(modem_, initPolicy, "core", [&]() {
    if (modem_.tinyGsm().init(config.simPin)) {
      return true;
    }
    modem_.logWarn("core", "init failed, retrying");
    restart();
    modem_.idle(2000);
    return false;
  });

  if (!inited) {
    return modem_.fail("core", ModemErrorCode::ModemInitFailed, "init failed",
                       ModemState::Error);
  }

  modem_.at().exec(2000L, GF("V1"));
  modem_.at().exec(2000L, GF("+CMEE=2"));

  modem_.transitionTo(ModemState::Ready, "init ok");
  return waitForNetwork();
}

bool ModemCore::waitForNetwork() {
  ModemConfig& config = modem_.config();
  modem_.transitionTo(ModemState::WaitingNetwork, "wait for network");
  RetryPolicy netPolicy;
  netPolicy.attempts = config.networkRetries;
  netPolicy.delayMs = 0;
  netPolicy.label = "wait for network";

  bool networkReady = retry(modem_, netPolicy, "core", [&]() {
    if (modem_.tinyGsm().waitForNetwork(config.networkTimeoutMs)) {
      modem_.logInfo("core", "network ready");
      modem_.transitionTo(ModemState::Registered, "registered");
      return true;
    }
    modem_.logWarn("core", "network wait timeout");
    return false;
  });

  if (networkReady) {
    return true;
  }

  uint32_t start = millis();
  while (millis() - start < config.networkTimeoutMs) {
    if (modem_.tinyGsm().isNetworkConnected()) {
      modem_.logInfo("core", "network connected");
      modem_.transitionTo(ModemState::Registered, "connected");
      return true;
    }
    modem_.idle(500);
  }

  return modem_.fail("core", ModemErrorCode::NetworkUnavailable,
                     "network not available", ModemState::Error);
}

String ModemCore::getCpsiInfo() {
  AtResult res = modem_.at().exec(2000L, GF("+CPSI?"));
  if (!res.ok) {
    return String();
  }

  String response = res.raw;
  response.replace("\r\nOK\r\n", "");
  response.replace("OK", "");
  response.trim();
  ModemParsers::sanitizeInfoText(response);
  return response;
}

int16_t ModemCore::getSignalStrengthPercent() {
  int16_t csq = modem_.tinyGsm().getSignalQuality();
  return ModemParsers::csqToPercent(csq);
}

ModemInfo ModemCore::getNetworkInfo() {
  ModemInfo info;
  info.modemName = modem_.tinyGsm().getModemName();
  info.modemInfo = modem_.tinyGsm().getModemInfo();
  info.imei = modem_.tinyGsm().getIMEI();
  info.iccid = modem_.tinyGsm().getSimCCID();
  info.operatorName = modem_.tinyGsm().getOperator();
  info.localIp = modem_.tinyGsm().getLocalIP();
  info.cpsi = getCpsiInfo();
  info.signalQuality = modem_.tinyGsm().getSignalQuality();
  info.signalPercent = ModemParsers::csqToPercent(info.signalQuality);
  info.simStatus = modem_.tinyGsm().getSimStatus();
  info.networkConnected = modem_.tinyGsm().isNetworkConnected();
  info.gprsConnected = modem_.tinyGsm().isGprsConnected();

  ModemParsers::sanitizeInfoText(info.modemName);
  ModemParsers::sanitizeInfoText(info.modemInfo);
  ModemParsers::sanitizeInfoText(info.imei);
  ModemParsers::sanitizeInfoText(info.iccid);
  ModemParsers::sanitizeInfoText(info.operatorName);
  ModemParsers::sanitizeInfoText(info.localIp);
  ModemParsers::sanitizeInfoText(info.cpsi);
  return info;
}
