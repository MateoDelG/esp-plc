#include "modem_http.h"

#include "modem_manager.h"
#include "modem_parsers.h"

ModemHttp::ModemHttp(ModemManager& modem) : modem_(modem) {}

bool ModemHttp::get(const char* url, uint16_t readLen) {
  if (!url || strlen(url) == 0) {
    modem_.setLastError(-30, "http url empty");
    return false;
  }

  if (!modem_.data().ensureNetOpen()) {
    modem_.logLine("[http] netopen not ready");
    modem_.setLastError(-31, "http netopen not ready");
    return false;
  }

  String host;
  String path;
  ModemParsers::parseHttpUrl(url, host, path);
  if (host.length() == 0) {
    return false;
  }

  int status = 0;
  int length = 0;

  modem_.at().exec(2000L, GF("+HTTPTERM"));

  modem_.logLine("[http] HTTPINIT");
  if (!modem_.at().exec(5000L, GF("+HTTPINIT")).ok) {
    modem_.setLastError(-32, "http init failed");
    return false;
  }

  modem_.logLine("[http] HTTPPARA URL");
  if (!modem_.at().exec(5000L, GF("+HTTPPARA=\"URL\",\""), url,
                        GF("\""))
           .ok) {
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    modem_.setLastError(-33, "http para url failed");
    return false;
  }

  modem_.logLine("[http] HTTPACTION");
  if (!modem_.at().exec(10000L, GF("+HTTPACTION=0")).ok) {
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    modem_.setLastError(-34, "http action failed");
    return false;
  }

  String urcLine;
  if (!modem_.at().waitUrc(UrcType::HttpAction, 30000UL, urcLine)) {
    modem_.logLine("[http] HTTPACTION URC timeout");
    modem_.setLastError(-35, "http action urc timeout");
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    return false;
  }

  if (!ModemParsers::parseHttpAction(urcLine, status, length)) {
    modem_.logLine("[http] parse HTTPACTION failed");
    modem_.logValue("[http] raw", urcLine);
    modem_.setLastError(-36, "http action parse failed");
    modem_.at().exec(2000L, GF("+HTTPTERM"));
    return false;
  }

  modem_.logValue("[http] status", status);
  modem_.logValue("[http] length", length);

  if (length > 0 && readLen > 0) {
    uint16_t toRead = readLen;
    if (length < static_cast<int>(readLen)) {
      toRead = static_cast<uint16_t>(length);
    }

    modem_.logLine("[http] HTTPREAD");
    modem_.at().exec(10000L, GF("+HTTPREAD=0,"), toRead);
  }

  modem_.logLine("[http] HTTPTERM");
  modem_.at().exec(5000L, GF("+HTTPTERM"));

  return (status == 200 || status == 204 || status == 301 || status == 302);
}
