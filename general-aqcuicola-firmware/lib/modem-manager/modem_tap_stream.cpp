#include "modem_tap_stream.h"

ModemTapStream::ModemTapStream(Stream& serial, LineCallback callback)
    : serial_(serial), callback_(callback) {
  txBuffer_.reserve(kMaxLineLength);
  rxBuffer_.reserve(kMaxLineLength);
}

void ModemTapStream::setCallback(LineCallback callback) {
  callback_ = callback;
}

void ModemTapStream::setRxLoggingEnabled(bool enabled) {
  if (rxLoggingEnabled_ == enabled) {
    return;
  }
  rxLoggingEnabled_ = enabled;
  if (!rxLoggingEnabled_) {
    rxBuffer_ = "";
    rxOverflowed_ = false;
  }
}

void ModemTapStream::setTxLoggingEnabled(bool enabled) {
  if (txLoggingEnabled_ == enabled) {
    return;
  }
  txLoggingEnabled_ = enabled;
  if (!txLoggingEnabled_) {
    txBuffer_ = "";
    txOverflowed_ = false;
  }
}

int ModemTapStream::available() { return serial_.available(); }

int ModemTapStream::read() {
  int value = serial_.read();
  if (value >= 0) {
    handleByte(false, static_cast<char>(value));
  }
  return value;
}

int ModemTapStream::peek() { return serial_.peek(); }

void ModemTapStream::flush() { serial_.flush(); }

size_t ModemTapStream::write(uint8_t b) {
  handleByte(true, static_cast<char>(b));
  return serial_.write(b);
}

size_t ModemTapStream::write(const uint8_t* buffer, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    handleByte(true, static_cast<char>(buffer[i]));
  }
  return serial_.write(buffer, size);
}

void ModemTapStream::handleByte(bool isTx, char c) {
  if (c == '\r') {
    return;
  }

  if (isTx && !txLoggingEnabled_) {
    return;
  }
  if (!isTx && !rxLoggingEnabled_) {
    return;
  }

  String& buffer = isTx ? txBuffer_ : rxBuffer_;
  bool& overflowed = isTx ? txOverflowed_ : rxOverflowed_;

  if (c == '\n') {
    emitLine(isTx, buffer, overflowed);
    return;
  }

  if (buffer.length() < kMaxLineLength) {
    buffer += c;
  } else {
    overflowed = true;
  }
}

void ModemTapStream::emitLine(bool isTx, String& buffer, bool& overflowed) {
  if (!callback_) {
    buffer = "";
    overflowed = false;
    return;
  }

  String line = buffer;
  if (lastCloseOk_ && line == "ERROR") {
    lastCloseOk_ = false;
    buffer = "";
    overflowed = false;
    return;
  }
  if (line.length() > 0) {
    lastCloseOk_ = isMqttCloseOkLine(line);
  }
  if (overflowed) {
    if (line.length() == 0) {
      line = "[truncated]";
    } else {
      line += " [truncated]";
    }
  }
  callback_(isTx, line);
  buffer = "";
  overflowed = false;
}

bool ModemTapStream::isMqttCloseOkLine(const String& line) const {
  if (!line.startsWith("+CMQTT")) {
    return false;
  }
  if (!(line.startsWith("+CMQTTDISC:") || line.startsWith("+CMQTTREL:") ||
        line.startsWith("+CMQTTSTOP:"))) {
    return false;
  }

  int comma = line.lastIndexOf(',');
  if (comma < 0) {
    return false;
  }
  String codeStr = line.substring(comma + 1);
  codeStr.trim();
  int code = codeStr.toInt();
  return code == 0 || code == 11 || code == 14 || code == 19 || code == 20;
}
