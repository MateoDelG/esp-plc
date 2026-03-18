#ifndef MODEM_TAP_STREAM_H
#define MODEM_TAP_STREAM_H

#include <Arduino.h>

#include <functional>

class ModemTapStream : public Stream {
 public:
  using LineCallback = std::function<void(bool isTx, const String& line)>;

  ModemTapStream(Stream& serial, LineCallback callback);

  void setCallback(LineCallback callback);

  int available() override;
  int read() override;
  int peek() override;
  void flush() override;

  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buffer, size_t size) override;

 private:
  void handleByte(bool isTx, char c);
  void emitLine(bool isTx, String& buffer, bool& overflowed);
  bool isMqttCloseOkLine(const String& line) const;

  static const size_t kMaxLineLength = 256;

  Stream& serial_;
  LineCallback callback_;
  String txBuffer_;
  String rxBuffer_;
  bool txOverflowed_ = false;
  bool rxOverflowed_ = false;
  bool lastCloseOk_ = false;
};

#endif
