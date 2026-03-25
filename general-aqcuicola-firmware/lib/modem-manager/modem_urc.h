// URC storage and matching
#ifndef MODEM_URC_H
#define MODEM_URC_H

#include <Arduino.h>

enum class UrcType : uint8_t {
  None = 0,
  NetOpen,
  HttpAction,
  MqttStart,
  MqttConnect,
  MqttDisc,
  MqttRel,
  MqttPub,
  MqttSub,
  MqttStop,
  MqttRxStart,
  MqttRxTopic,
  MqttRxPayload,
  MqttRxEnd,
};

struct UrcEvent {
  UrcType type = UrcType::None;
  String line;
  uint32_t tsMs = 0;
};

class UrcStore {
 public:
  UrcStore();

  static UrcType classify(const String& line);

  void push(const String& line);
  void pushFromResponse(const String& response);

  bool pop(UrcType type, String& lineOut);

 private:
  static const size_t kMaxEvents = 16;
  UrcEvent events_[kMaxEvents];
  size_t head_ = 0;
  size_t count_ = 0;
};

#endif
