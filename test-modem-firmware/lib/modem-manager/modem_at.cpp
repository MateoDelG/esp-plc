#include "modem_at.h"

AtClient::AtClient(TinyGsm& modem, Stream& stream, UrcStore& urcStore)
    : modem_(modem), stream_(stream), urcStore_(urcStore) {
  lineBuffer_.reserve(256);
}

AtResult AtClient::waitOk(uint32_t timeoutMs) {
  String response;
  int res = modem_.waitResponse(timeoutMs, response);

  AtResult result;
  result.timeoutMs = timeoutMs;
  result.raw = response;
  result.ok = (res == 1);
  result.error = response.indexOf("ERROR") >= 0 ||
                 response.indexOf("+CME ERROR") >= 0;

  urcStore_.pushFromResponse(response);
  return result;
}

bool AtClient::waitForPrompt(uint32_t timeoutMs) {
  uint32_t drainStart = millis();
  while (millis() - drainStart < 50UL) {
    while (stream_.available()) {
      char c = static_cast<char>(stream_.read());
      if (c == '>') {
        return true;
      }
    }
    delay(2);
  }

  int res = modem_.waitResponse(timeoutMs, GF(">"), GF("ERROR"),
                                GF("+CME ERROR"));
  if (res == 1) {
    return true;
  }

  uint32_t start = millis();
  while (millis() - start < 3000UL) {
    while (stream_.available()) {
      char c = static_cast<char>(stream_.read());
      if (c == '>') {
        return true;
      }
    }
    delay(5);
  }

  return false;
}

bool AtClient::waitUrc(UrcType type, uint32_t timeoutMs, String& lineOut) {
  if (urcStore_.pop(type, lineOut)) {
    return true;
  }

  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    String line;
    if (!readLine(line, 250)) {
      continue;
    }
    if (line.length() == 0) {
      continue;
    }
    UrcType found = UrcStore::classify(line);
    if (found != UrcType::None) {
      urcStore_.push(line);
    }
    if (found == type) {
      lineOut = line;
      return true;
    }
  }

  return false;
}

bool AtClient::readLine(String& lineOut, uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (stream_.available()) {
      char c = static_cast<char>(stream_.read());
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        lineOut = lineBuffer_;
        lineBuffer_ = "";
        lineOut.trim();
        return true;
      }
      if (lineBuffer_.length() < 512) {
        lineBuffer_ += c;
      }
    }
    delay(5);
  }

  return false;
}

bool AtClient::readLineNonBlocking(String& lineOut) {
  while (stream_.available()) {
    char c = static_cast<char>(stream_.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      lineOut = lineBuffer_;
      lineBuffer_ = "";
      lineOut.trim();
      return true;
    }
    if (lineBuffer_.length() < 512) {
      lineBuffer_ += c;
    }
  }
  return false;
}

String AtClient::readRaw(uint32_t timeoutMs) {
  String output;
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (stream_.available()) {
      char c = static_cast<char>(stream_.read());
      output += c;
    }
    delay(5);
  }
  return output;
}

bool AtClient::readExact(size_t length, String& out, uint32_t timeoutMs) {
  out = "";
  out.reserve(length + 4);
  uint32_t start = millis();
  while (out.length() < length && millis() - start < timeoutMs) {
    while (stream_.available() && out.length() < length) {
      char c = static_cast<char>(stream_.read());
      out += c;
    }
    delay(5);
  }
  return out.length() == length;
}
