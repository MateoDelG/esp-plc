#include "modem_tap_stream.h"

ModemTapStream::ModemTapStream(Stream& serial, LineCallback callback)
    : serial_(serial), callback_(callback) {
  txBuffer_.reserve(kMaxLineLength);
  rxBuffer_.reserve(kMaxLineLength);
}

void ModemTapStream::setCallback(LineCallback callback) {
  callback_ = callback;
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

  callback_(isTx, buffer);
  buffer = "";
  overflowed = false;
}
