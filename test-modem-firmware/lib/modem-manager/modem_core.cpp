#include "modem_core.h"

#include "modem_manager.h"
#include "modem_parsers.h"

ModemCore::ModemCore(ModemManager& modem) : modem_(modem) {}

bool ModemCore::powerOn() {
  const ModemConfig& config = modem_.config();
  if (config.pins.pwrKey < 0) {
    return true;
  }

  pinMode(config.pins.pwrKey, OUTPUT);
  digitalWrite(config.pins.pwrKey, LOW);
  delay(1000);
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
  delay(1500);
  digitalWrite(config.pins.pwrKey, HIGH);
  return true;
}

bool ModemCore::restart() {
  powerOff();
  delay(1000);
  powerOn();
  return true;
}

bool ModemCore::begin() {
  modem_.setState(ModemState::Booting);
  modem_.logLine("[core] init start");

  powerOn();

  ModemConfig& config = modem_.config();
  if (config.pins.rx >= 0 && config.pins.tx >= 0) {
    config.serialAT.begin(config.baud, SERIAL_8N1, config.pins.rx,
                          config.pins.tx);
  } else {
    config.serialAT.begin(config.baud);
  }

  if (config.initDelayMs > 0) {
    delay(config.initDelayMs);
  }

  bool inited = false;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (modem_.tinyGsm().init(config.simPin)) {
      inited = true;
      break;
    }
    modem_.logLine("[core] init failed, retrying");
    restart();
    delay(2000);
  }

  if (!inited) {
    modem_.setLastError(-10, "modem init failed");
    modem_.logLine("[core] init failed");
    return false;
  }

  modem_.at().exec(2000L, GF("V1"));
  modem_.at().exec(2000L, GF("+CMEE=2"));

  modem_.setState(ModemState::Ready);
  return waitForNetwork();
}

bool ModemCore::waitForNetwork() {
  ModemConfig& config = modem_.config();
  for (uint8_t attempt = 0; attempt < config.networkRetries; ++attempt) {
    if (modem_.tinyGsm().waitForNetwork(config.networkTimeoutMs)) {
      modem_.logLine("[core] network ready");
      modem_.setState(ModemState::Registered);
      return true;
    }
    modem_.logLine("[core] network wait timeout");
  }

  uint32_t start = millis();
  while (millis() - start < config.networkTimeoutMs) {
    if (modem_.tinyGsm().isNetworkConnected()) {
      modem_.logLine("[core] network connected");
      modem_.setState(ModemState::Registered);
      return true;
    }
    delay(500);
  }

  modem_.setLastError(-11, "network not available");
  modem_.logLine("[core] network not available");
  return false;
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
  return info;
}
