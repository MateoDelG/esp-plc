#include "modem_at.h"

#include "modem_manager.h"

template <typename StepFn>
static bool pollUntil(ModemManager* manager, uint32_t timeoutMs,
                      uint32_t idleDelayMs, StepFn stepFn) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (stepFn()) {
      return true;
    }
    if (idleDelayMs > 0) {
      if (manager) {
        manager->idle(idleDelayMs);
      } else {
        delay(idleDelayMs);
      }
    }
  }
  return false;
}

static bool waitPromptFromStream(Stream& stream, uint32_t timeoutMs,
                                 uint32_t idleDelayMs, ModemManager* manager) {
  return pollUntil(manager, timeoutMs, idleDelayMs, [&]() {
    while (stream.available()) {
      char c = static_cast<char>(stream.read());
      if (c == '>') {
        return true;
      }
    }
    return false;
  });
}

AtClient::AtClient(TinyGsm& modem, Stream& stream, UrcStore& urcStore,
                   ModemManager* manager)
    : modem_(modem), stream_(stream), urcStore_(urcStore), manager_(manager) {
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
  if (waitPromptFromStream(stream_, 50UL, 2, manager_)) {
    return true;
  }

  int res = modem_.waitResponse(timeoutMs, GF(">"), GF("ERROR"),
                                GF("+CME ERROR"));
  if (res == 1) {
    return true;
  }

  return waitPromptFromStream(stream_, 3000UL, 5, manager_);
}

bool AtClient::waitUrc(UrcType type, uint32_t timeoutMs, String& lineOut) {
  if (urcStore_.pop(type, lineOut)) {
    return true;
  }

  return pollUntil(manager_, timeoutMs, 0, [&]() {
    String line;
    if (!readLine(line, 250)) {
      return false;
    }
    if (line.length() == 0) {
      return false;
    }
    UrcType found = UrcStore::classify(line);
    if (found != UrcType::None) {
      urcStore_.push(line);
    }
    if (found == type) {
      lineOut = line;
      return true;
    }
    return false;
  });
}

bool AtClient::readLine(String& lineOut, uint32_t timeoutMs) {
  return pollUntil(manager_, timeoutMs, 5, [&]() {
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
  });
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
  pollUntil(manager_, timeoutMs, 5, [&]() {
    while (stream_.available()) {
      char c = static_cast<char>(stream_.read());
      output += c;
    }
    return false;
  });
  return output;
}

bool AtClient::readExact(size_t length, String& out, uint32_t timeoutMs) {
  out = "";
  out.reserve(length + 4);
  pollUntil(manager_, timeoutMs, 5, [&]() {
    while (stream_.available() && out.length() < length) {
      char c = static_cast<char>(stream_.read());
      out += c;
    }
    return out.length() >= length;
  });
  return out.length() == length;
}

bool AtClient::readExactToFile(size_t length, File& file, uint32_t timeoutMs) {
  size_t written = 0;
  uint8_t buffer[128];

  pollUntil(manager_, timeoutMs, 5, [&]() {
    if (written >= length) {
      return true;
    }
    size_t available = stream_.available();
    if (available == 0) {
      return false;
    }

    size_t remaining = length - written;
    size_t toRead = available;
    if (toRead > sizeof(buffer)) {
      toRead = sizeof(buffer);
    }
    if (toRead > remaining) {
      toRead = remaining;
    }

    size_t readCount = stream_.readBytes(buffer, toRead);
    if (readCount == 0) {
      return false;
    }

    size_t writeCount = file.write(buffer, readCount);
    if (writeCount != readCount) {
      return true;
    }
    written += readCount;
    return written >= length;
  });

  return written == length;
}

bool AtClient::readExactToBuffer(uint8_t* buffer, size_t length,
                                 uint32_t timeoutMs) {
  if (!buffer || length == 0) {
    return false;
  }
  size_t readTotal = 0;

  pollUntil(manager_, timeoutMs, 5, [&]() {
    if (readTotal >= length) {
      return true;
    }
    size_t available = stream_.available();
    if (available == 0) {
      return false;
    }

    size_t remaining = length - readTotal;
    size_t toRead = available;
    if (toRead > remaining) {
      toRead = remaining;
    }

    size_t readCount =
        stream_.readBytes(buffer + readTotal, static_cast<size_t>(toRead));
    if (readCount == 0) {
      return false;
    }
    readTotal += readCount;
    return readTotal >= length;
  });

  return readTotal == length;
}
