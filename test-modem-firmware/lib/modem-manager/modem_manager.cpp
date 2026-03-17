#include "modem_manager.h"

#include <Arduino.h>

static int16_t csqToPercent(int16_t csq) {
  if (csq < 0 || csq == 99) {
    return 0;
  }
  if (csq > 31) {
    csq = 31;
  }
  return map(csq, 0, 31, 0, 100);
}

static bool parseCgattAttached(const String& response) {
  int idx = response.indexOf("+CGATT:");
  if (idx < 0) {
    return false;
  }
  String value = response.substring(idx + 7);
  value.trim();
  return value.startsWith("1");
}

static bool parseCgpaddrIp(const String& response, String& ipOut) {
  int idx = response.indexOf("+CGPADDR:");
  if (idx < 0) {
    return false;
  }

  int comma = response.indexOf(',', idx);
  if (comma < 0) {
    return false;
  }

  String ip = response.substring(comma + 1);
  ip.trim();

  if (ip.length() == 0) {
    return false;
  }

  ipOut = ip;
  return true;
}

static bool responseHasNetOpen1(const String& response) {
  return response.indexOf("+NETOPEN: 1") >= 0;
}

static bool responseHasNetOpen0(const String& response) {
  return response.indexOf("+NETOPEN: 0") >= 0;
}

static bool responseHasAlreadyOpened(const String& response) {
  return response.indexOf("Network is already opened") >= 0 ||
         response.indexOf("+IP ERROR: Network is already opened") >= 0;
}

static bool parseCdnsgipIp(const String& response, String& ipOut) {
  int idx = response.indexOf("+CDNSGIP:");
  if (idx < 0) {
    return false;
  }

  int firstQuote = response.indexOf('"', idx);
  if (firstQuote < 0) {
    return false;
  }
  int secondQuote = response.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) {
    return false;
  }
  int thirdQuote = response.indexOf('"', secondQuote + 1);
  if (thirdQuote < 0) {
    return false;
  }
  int fourthQuote = response.indexOf('"', thirdQuote + 1);
  if (fourthQuote < 0) {
    return false;
  }

  String ip = response.substring(thirdQuote + 1, fourthQuote);
  ip.trim();
  if (ip.length() == 0) {
    return false;
  }

  ipOut = ip;
  return true;
}

static bool parseHttpAction(const String& response, int& status, int& length) {
  int idx = response.indexOf("+HTTPACTION:");
  if (idx < 0) {
    return false;
  }

  int firstComma = response.indexOf(',', idx);
  if (firstComma < 0) {
    return false;
  }

  int secondComma = response.indexOf(',', firstComma + 1);
  if (secondComma < 0) {
    return false;
  }

  String statusStr = response.substring(firstComma + 1, secondComma);
  String lenStr = response.substring(secondComma + 1);
  statusStr.trim();
  lenStr.trim();

  status = statusStr.toInt();
  length = lenStr.toInt();
  return true;
}

static void parseHttpUrl(const char* url, String& hostOut, String& pathOut) {
  String u = url ? String(url) : String();
  u.trim();

  if (u.startsWith("http://")) {
    u.remove(0, 7);
  }

  int slash = u.indexOf('/');
  if (slash < 0) {
    hostOut = u;
    pathOut = "/";
  } else {
    hostOut = u.substring(0, slash);
    pathOut = u.substring(slash);
    if (pathOut.length() == 0) {
      pathOut = "/";
    }
  }
}

ModemManager::ModemManager(const ModemConfig& config)
    : config_(config),
      tapStream_(config.serialAT, config.modemLogSink),
      modem_(tapStream_),
      mqtt_(*this) {}






ModemMqtt::ModemMqtt(ModemManager& modem) : modem_(modem) {}

bool ModemMqtt::start() {
  if (started_) {
    return true;
  }

  String response;
  modem_.modem_.sendAT(GF("+CMQTTSTART"));
  int res = modem_.modem_.waitResponse(10000L, response);

  // En la práctica, si responde OK, el servicio suele arrancar y la URC
  // puede llegar aparte o perderse del buffer de waitResponse.
  if (res == 1 || response.indexOf("OK") >= 0 ||
      response.indexOf("+CMQTTSTART: 0") >= 0) {
    started_ = true;
    return true;
  }

  // Si ya estaba iniciado, también lo tratamos como éxito.
  if (response.indexOf("+CMQTTSTART: 23") >= 0 ||
      response.indexOf("already") >= 0 ||
      response.indexOf("ERROR") >= 0) {
    started_ = true;
    return true;
  }

  modem_.logValue("[mqtt] start raw", response);
  return false;
}

bool ModemMqtt::acquire(const char* clientId) {
  if (!clientId || strlen(clientId) == 0) {
    return false;
  }

  String response;
  modem_.modem_.sendAT(GF("+CMQTTACCQ=0,\""), clientId, GF("\",1"));
  int res = modem_.modem_.waitResponse(10000L, response);

  if (res == 1 && response.indexOf("OK") >= 0) {
    acquired_ = true;
    return true;
  }

  if (response.indexOf("+CMQTTACCQ: 0,0") >= 0) {
    acquired_ = true;
    return true;
  }

  modem_.logValue("[mqtt] accq raw", response);
  return false;
}

bool ModemMqtt::connectBroker(const char* host, uint16_t port,
                              const char* user, const char* pass) {
  if (!host || strlen(host) == 0) {
    return false;
  }

  if (connected_) {
    return true;
  }

  if (!modem_.isDataReady() && !modem_.connectGprs()) {
    modem_.logLine("[mqtt] data not ready");
    return false;
  }

  if (!modem_.waitNetOpen(5000UL)) {
    modem_.logLine("[mqtt] netopen not ready");
    return false;
  }

  // TLS setup
  modem_.modem_.sendAT(GF("+CSSLCFG=\"sslversion\",0,4"));
  modem_.modem_.waitResponse(3000L);

  modem_.modem_.sendAT(GF("+CSSLCFG=\"authmode\",0,0"));
  modem_.modem_.waitResponse(3000L);

  modem_.modem_.sendAT(GF("+CSSLCFG=\"enableSNI\",0,1"));
  modem_.modem_.waitResponse(3000L);

  // No trates ERROR aquí como fatal; puede ya estar asociado
  String response;
  modem_.modem_.sendAT(GF("+CMQTTSSLCFG=0,0"));
  modem_.modem_.waitResponse(3000L, response);

  response = "";
  if (user && pass && strlen(user) > 0) {
    modem_.modem_.sendAT(GF("+CMQTTCONNECT=0,\"tcp://"), host, GF(":"),
                         port, GF("\",60,1,\""), user, GF("\",\""),
                         pass, GF("\""));
  } else {
    modem_.modem_.sendAT(GF("+CMQTTCONNECT=0,\"tcp://"), host, GF(":"),
                         port, GF("\",60,1"));
  }

  // 1) Esperar respuesta inmediata al comando
  modem_.modem_.waitResponse(15000L, response);

  // 2) Si el éxito ya vino ahí mismo
  if (response.indexOf("+CMQTTCONNECT: 0,0") >= 0) {
    connected_ = true;
    return true;
  }

  // 3) La URC puede llegar más tarde: leer directo del stream
  uint32_t startMs = millis();
  while (millis() - startMs < 15000UL) {
    while (modem_.tapStream_.available()) {
      char c = static_cast<char>(modem_.tapStream_.read());
      response += c;
    }

    if (response.indexOf("+CMQTTCONNECT: 0,0") >= 0) {
      connected_ = true;
      return true;
    }

    // Si llegó otro código distinto de 0,0, salimos
    int idx = response.indexOf("+CMQTTCONNECT: 0,");
    if (idx >= 0 && response.indexOf("+CMQTTCONNECT: 0,0") < 0) {
      break;
    }

    delay(20);
  }

  modem_.logValue("[mqtt] connect raw", response);
  connected_ = false;
  return false;
}

bool ModemMqtt::sendPayload(const char* data, size_t length) {
  if (!data || length == 0) {
    return false;
  }

  int res = modem_.modem_.waitResponse(5000L, GF(">"), GF("ERROR"),
                                       GF("+CME ERROR"));
  if (res != 1) {
    return false;
  }

  modem_.tapStream_.write(reinterpret_cast<const uint8_t*>(data), length);

  String response;
  modem_.modem_.waitResponse(5000L, response);

  return response.indexOf("OK") >= 0;
}
bool ModemMqtt::connect(const char* host, uint16_t port, const char* clientId,
                        uint8_t retries, uint32_t retryDelayMs,
                        const char* user, const char* pass) {
  if (!host || strlen(host) == 0) {
    return false;
  }

  if (connected_) {
    return true;
  }

  if (!start()) {
    modem_.logLine("[mqtt] start failed");
    return false;
  }

  if (!acquire(clientId)) {
    modem_.logLine("[mqtt] acquire failed");
    return false;
  }

  for (uint8_t attempt = 0; attempt < retries; ++attempt) {
    modem_.logLine(String("[mqtt] broker connect attempt ") +
                   (attempt + 1) + "/" + retries);

    if (connectBroker(host, port, user, pass)) {
      return true;
    }

    if (connected_) {
      return true;
    }

    delay(retryDelayMs);
  }

  return false;
}

bool ModemMqtt::publishJson(const char* topic, const char* json, int qos,
                            bool retain) {
  if (!connected_ || !topic || !json) {
    return false;
  }

  size_t topicLen = strlen(topic);
  size_t payloadLen = strlen(json);

  modem_.modem_.sendAT(GF("+CMQTTTOPIC=0,"), topicLen);
  if (!sendPayload(topic, topicLen)) {
    modem_.logLine("[mqtt] topic payload failed");
    return false;
  }

  delay(30);

  modem_.modem_.sendAT(GF("+CMQTTPAYLOAD=0,"), payloadLen);
  if (!sendPayload(json, payloadLen)) {
    modem_.logLine("[mqtt] message payload failed");
    return false;
  }

  delay(30);

  String response;
  int mqttQos = constrain(qos, 0, 2);
  int mqttRetain = retain ? 1 : 0;

  modem_.modem_.sendAT(GF("+CMQTTPUB=0,"), mqttQos, GF(",60,"), mqttRetain);

  // Respuesta inmediata
  modem_.modem_.waitResponse(10000L, response);

  // Si el éxito ya vino aquí mismo
  if (response.indexOf("+CMQTTPUB: 0,0") >= 0) {
    return true;
  }

  // Si vino OK, la URC puede llegar después
  if (response.indexOf("OK") >= 0 || response.length() == 0) {
    uint32_t startMs = millis();
    while (millis() - startMs < 10000UL) {
      while (modem_.tapStream_.available()) {
        char c = static_cast<char>(modem_.tapStream_.read());
        response += c;
      }

      if (response.indexOf("+CMQTTPUB: 0,0") >= 0) {
        return true;
      }

      int idx = response.indexOf("+CMQTTPUB: 0,");
      if (idx >= 0 && response.indexOf("+CMQTTPUB: 0,0") < 0) {
        break;
      }

      delay(20);
    }
  }

  modem_.logValue("[mqtt] publish raw", response);
  return false;
}
void ModemMqtt::disconnect() {
  if (connected_) {
    modem_.modem_.sendAT(GF("+CMQTTDISC=0,60"));
    modem_.modem_.waitResponse(5000L);
    connected_ = false;
  }

  if (acquired_) {
    modem_.modem_.sendAT(GF("+CMQTTREL=0"));
    modem_.modem_.waitResponse(5000L);
    acquired_ = false;
  }

  if (started_) {
    modem_.modem_.sendAT(GF("+CMQTTSTOP"));
    modem_.modem_.waitResponse(5000L);
    started_ = false;
  }
}

void ModemManager::setLogsEnabled(bool enabled) { config_.enableLogs = enabled; }

bool ModemManager::powerOn() {
  if (config_.pins.pwrKey < 0) {
    return true;
  }

  pinMode(config_.pins.pwrKey, OUTPUT);
  digitalWrite(config_.pins.pwrKey, LOW);
  delay(1000);
  digitalWrite(config_.pins.pwrKey, HIGH);
  return true;
}

bool ModemManager::powerOff() {
  if (config_.pins.pwrKey < 0) {
    return true;
  }

  pinMode(config_.pins.pwrKey, OUTPUT);
  digitalWrite(config_.pins.pwrKey, LOW);
  delay(1500);
  digitalWrite(config_.pins.pwrKey, HIGH);
  return true;
}

bool ModemManager::restart() {
  powerOff();
  delay(1000);
  powerOn();
  return true;
}

bool ModemManager::begin() {
  logLine("[modem] init start");

  powerOn();

  if (config_.pins.rx >= 0 && config_.pins.tx >= 0) {
    config_.serialAT.begin(config_.baud, SERIAL_8N1, config_.pins.rx,
                           config_.pins.tx);
  } else {
    config_.serialAT.begin(config_.baud);
  }

  if (config_.initDelayMs > 0) {
    delay(config_.initDelayMs);
  }

  bool inited = false;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (modem_.init(config_.simPin)) {
      inited = true;
      break;
    }
    logLine("[modem] init failed, retrying");
    restart();
    delay(2000);
  }

  if (!inited) {
    logLine("[modem] init failed");
    return false;
  }

  // AQUÍ van los cambios
  modem_.sendAT(GF("V1"));
  modem_.waitResponse(2000L);

  modem_.sendAT(GF("+CMEE=2"));
  modem_.waitResponse(2000L);

  return waitForNetwork();
}

bool ModemManager::waitForNetwork() {
  for (uint8_t attempt = 0; attempt < config_.networkRetries; ++attempt) {
    if (modem_.waitForNetwork(config_.networkTimeoutMs)) {
      logLine("[modem] network ready");
      return true;
    }
    logLine("[modem] network wait timeout");
  }

  uint32_t start = millis();
  while (millis() - start < config_.networkTimeoutMs) {
    if (modem_.isNetworkConnected()) {
      logLine("[modem] network connected");
      return true;
    }
    delay(500);
  }

  logLine("[modem] network not available");
  return false;
}

bool ModemManager::isNetOpenNow() {
  String response1;
  String response2;

  modem_.sendAT(GF("+NETOPEN?"));
  modem_.waitResponse(3000L, response1);

  if (responseHasNetOpen1(response1)) {
    return true;
  }
  if (responseHasNetOpen0(response1)) {
    return false;
  }

  // Con ATV0 a veces llega primero "0" y luego la URC real.
  delay(150);
  modem_.waitResponse(1000L, response2);

  String full = response1 + response2;

  if (responseHasNetOpen1(full)) {
    return true;
  }
  if (responseHasNetOpen0(full)) {
    return false;
  }

  return false;
}

bool ModemManager::waitNetOpen(uint32_t timeoutMs) {
  uint32_t start = millis();
  uint8_t consecutiveOk = 0;

  while (millis() - start < timeoutMs) {
    if (isNetOpenNow()) {
      consecutiveOk++;
      if (consecutiveOk >= 2) {
        logLine("[modem] netopen active");
        return true;
      }
    } else {
      consecutiveOk = 0;
    }
    delay(300);
  }

  logLine("[modem] netopen timeout");
  return false;
}

bool ModemManager::openSocketService() {
  if (!config_.apn.apn || strlen(config_.apn.apn) == 0) {
    logLine("[modem] apn not set");
    return false;
  }

  if (isNetOpenNow()) {
    logLine("[modem] NETOPEN already active");
    return true;
  }

  modem_.sendAT(GF("+CGDCONT=1,\"IP\",\""), config_.apn.apn, GF("\""));
  modem_.waitResponse(5000L);

  logLine("[modem] NETOPEN start");

  String response;
  modem_.sendAT(GF("+NETOPEN"));
  modem_.waitResponse(8000L, response);

  if (responseHasAlreadyOpened(response)) {
    logLine("[modem] NETOPEN already opened");
    return waitNetOpen(8000UL);
  }

  return waitNetOpen(15000UL);
}

bool ModemManager::isDataReady() {
  bool attached = false;
  String ip;
  String response;

  modem_.sendAT(GF("+CGATT?"));
  if (modem_.waitResponse(2000L, response) == 1) {
    attached = parseCgattAttached(response);
  }

  response = "";
  modem_.sendAT(GF("+CGPADDR=1"));
  if (modem_.waitResponse(2000L, response) == 1) {
    parseCgpaddrIp(response, ip);
  }

  bool netOpen = isNetOpenNow();
  return attached && ip.length() > 0 && netOpen;
}

bool ModemManager::connectGprs() {
  if (!config_.apn.apn || strlen(config_.apn.apn) == 0) {
    logLine("[modem] apn not set");
    return false;
  }

  logValue("[modem] apn", config_.apn.apn);

  if (isDataReady()) {
    logLine("[modem] data already ready");
    return true;
  }

  modem_.sendAT(GF("+CGDCONT=1,\"IP\",\""), config_.apn.apn, GF("\""));
  modem_.waitResponse(5000L);

  if (!openSocketService()) {
    return false;
  }

  return isDataReady();
}

bool ModemManager::ping(const char* host, uint8_t count, uint32_t timeoutMs) {
  if (!host || strlen(host) == 0) {
    return false;
  }

  String ip = host;
  String response;

  modem_.sendAT(GF("+CDNSGIP=\""), host, GF("\""));
  if (modem_.waitResponse(20000L, response) == 1) {
    parseCdnsgipIp(response, ip);
  } else {
    ip = "8.8.8.8";
  }

  if (ip.length() == 0) {
    ip = "8.8.8.8";
  }

  uint32_t waitMs = timeoutMs * static_cast<uint32_t>(count) + 5000U;
  response = "";

  modem_.sendAT(GF("+CPING=\""), ip.c_str(), GF("\","), count);
  int res = modem_.waitResponse(waitMs, response);

  if (res != 1) {
    response = "";
    modem_.sendAT(GF("+PING=\""), ip.c_str(), GF("\""));
    res = modem_.waitResponse(waitMs, response);
  }

  if (res != 1) {
    return false;
  }

  if (response.indexOf("+CPING:") < 0 && response.indexOf("+PING:") < 0) {
    return false;
  }

  if (response.indexOf(",0,0,0") >= 0 || response.indexOf(",0,0,0,0") >= 0) {
    return false;
  }

  return true;
}

bool ModemManager::httpGetTest(const char* url, uint16_t readLen) {
  return httpGetNative(url, readLen);
}

bool ModemManager::httpGetNative(const char* url, uint16_t readLen) {
  if (!url || strlen(url) == 0) {
    return false;
  }

  if (!waitNetOpen(5000UL)) {
    logLine("[http] netopen not ready");
    return false;
  }

  String host;
  String path;
  parseHttpUrl(url, host, path);
  if (host.length() == 0) {
    return false;
  }

  int status = 0;
  int length = 0;

  // Limpiar sesión HTTP previa si existía
  modem_.sendAT(GF("+HTTPTERM"));
  modem_.waitResponse(2000L);

  logLine("[http] HTTPINIT");
  modem_.sendAT(GF("+HTTPINIT"));
  if (modem_.waitResponse(5000L) != 1) {
    return false;
  }

  logLine("[http] HTTPPARA URL");
  modem_.sendAT(GF("+HTTPPARA=\"URL\",\""), url, GF("\""));
  if (modem_.waitResponse(5000L) != 1) {
    modem_.sendAT(GF("+HTTPTERM"));
    modem_.waitResponse(2000L);
    return false;
  }

  logLine("[http] HTTPACTION");
  modem_.sendAT(GF("+HTTPACTION=0"));

  // 1) Esperar OK inmediato del comando
  if (modem_.waitResponse(10000L) != 1) {
    modem_.sendAT(GF("+HTTPTERM"));
    modem_.waitResponse(2000L);
    return false;
  }

  // 2) Leer manualmente del stream hasta encontrar la URC +HTTPACTION
  String response;
  uint32_t start = millis();
  while (millis() - start < 30000UL) {
    while (tapStream_.available()) {
      char c = static_cast<char>(tapStream_.read());
      response += c;
    }

    if (response.indexOf("+HTTPACTION:") >= 0) {
      break;
    }

    delay(20);
  }

  if (!parseHttpAction(response, status, length)) {
    logLine("[http] parse HTTPACTION failed");
    logValue("[http] raw", response);

    modem_.sendAT(GF("+HTTPTERM"));
    modem_.waitResponse(2000L);
    return false;
  }

  logValue("[http] status", status);
  logValue("[http] length", length);

  if (length > 0 && readLen > 0) {
    uint16_t toRead = readLen;
    if (length < static_cast<int>(readLen)) {
      toRead = static_cast<uint16_t>(length);
    }

    logLine("[http] HTTPREAD");
    modem_.sendAT(GF("+HTTPREAD=0,"), toRead);
    modem_.waitResponse(10000L);
  }

  logLine("[http] HTTPTERM");
  modem_.sendAT(GF("+HTTPTERM"));
  modem_.waitResponse(5000L);

  return (status == 200 || status == 204 || status == 301 || status == 302);
}

bool ModemManager::disconnectGprs() { return modem_.gprsDisconnect(); }

bool ModemManager::isNetworkConnected() { return modem_.isNetworkConnected(); }

bool ModemManager::isGprsConnected() { return modem_.isGprsConnected(); }

String ModemManager::getCpsiInfo() {
  String response;
  modem_.sendAT(GF("+CPSI?"));
  if (modem_.waitResponse(2000L, response) != 1) {
    return String();
  }

  response.replace("\r\nOK\r\n", "");
  response.replace("OK", "");
  response.trim();
  return response;
}

int16_t ModemManager::getSignalStrengthPercent() {
  int16_t csq = modem_.getSignalQuality();
  return csqToPercent(csq);
}

ModemInfo ModemManager::getNetworkInfo() {
  ModemInfo info;
  info.modemName = modem_.getModemName();
  info.modemInfo = modem_.getModemInfo();
  info.imei = modem_.getIMEI();
  info.iccid = modem_.getSimCCID();
  info.operatorName = modem_.getOperator();
  info.localIp = modem_.getLocalIP();
  info.cpsi = getCpsiInfo();
  info.signalQuality = modem_.getSignalQuality();
  info.signalPercent = csqToPercent(info.signalQuality);
  info.simStatus = modem_.getSimStatus();
  info.networkConnected = modem_.isNetworkConnected();
  info.gprsConnected = modem_.isGprsConnected();
  return info;
}

void ModemManager::logLine(const String& message) {
  if (!config_.enableLogs) {
    return;
  }
  config_.serialMon.println(message);
}

void ModemManager::logValue(const String& label, const String& value) {
  if (!config_.enableLogs) {
    return;
  }
  config_.serialMon.print(label);
  config_.serialMon.print(": ");
  config_.serialMon.println(value);
}

void ModemManager::logValue(const String& label, int value) {
  if (!config_.enableLogs) {
    return;
  }
  config_.serialMon.print(label);
  config_.serialMon.print(": ");
  config_.serialMon.println(value);
}
