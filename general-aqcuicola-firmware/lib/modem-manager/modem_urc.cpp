#include "modem_urc.h"

UrcStore::UrcStore() = default;

UrcType UrcStore::classify(const String& line) {
  if (line.startsWith("+NETOPEN:")) {
    return UrcType::NetOpen;
  }
  if (line.startsWith("+HTTPACTION:")) {
    return UrcType::HttpAction;
  }
  if (line.startsWith("+CMQTTSTART:")) {
    return UrcType::MqttStart;
  }
  if (line.startsWith("+CMQTTCONNECT:")) {
    return UrcType::MqttConnect;
  }
  if (line.startsWith("+CMQTTDISC:")) {
    return UrcType::MqttDisc;
  }
  if (line.startsWith("+CMQTTREL:")) {
    return UrcType::MqttRel;
  }
  if (line.startsWith("+CMQTTPUB:")) {
    return UrcType::MqttPub;
  }
  if (line.startsWith("+CMQTTSUB:")) {
    return UrcType::MqttSub;
  }
  if (line.startsWith("+CMQTTSTOP:")) {
    return UrcType::MqttStop;
  }
  if (line.startsWith("+CMQTTRXSTART:")) {
    return UrcType::MqttRxStart;
  }
  if (line.startsWith("+CMQTTRXTOPIC:")) {
    return UrcType::MqttRxTopic;
  }
  if (line.startsWith("+CMQTTRXPAYLOAD:")) {
    return UrcType::MqttRxPayload;
  }
  if (line.startsWith("+CMQTTRXEND:")) {
    return UrcType::MqttRxEnd;
  }
  if (line.startsWith("+CNTP:")) {
    return UrcType::Cntp;
  }
  return UrcType::None;
}

void UrcStore::push(const String& line) {
  UrcType type = classify(line);
  if (type == UrcType::None) {
    return;
  }

  size_t index = (head_ + count_) % kMaxEvents;
  events_[index].type = type;
  events_[index].line = line;
  events_[index].tsMs = millis();

  if (count_ < kMaxEvents) {
    ++count_;
  } else {
    head_ = (head_ + 1) % kMaxEvents;
    ++overflowCount_;
  }
}

void UrcStore::pushFromResponse(const String& response) {
  if (response.length() == 0) {
    return;
  }

  int start = 0;
  while (start < response.length()) {
    int end = response.indexOf('\n', start);
    if (end < 0) {
      end = response.length();
    }
    String line = response.substring(start, end);
    line.replace("\r", "");
    line.trim();
    if (line.length() > 0) {
      push(line);
    }
    start = end + 1;
  }
}

bool UrcStore::pop(UrcType type, String& lineOut) {
  if (count_ == 0) {
    return false;
  }

  for (size_t i = 0; i < count_; ++i) {
    size_t index = (head_ + i) % kMaxEvents;
    if (events_[index].type == type) {
      lineOut = events_[index].line;

      for (size_t j = i; j + 1 < count_; ++j) {
        size_t from = (head_ + j + 1) % kMaxEvents;
        size_t to = (head_ + j) % kMaxEvents;
        events_[to] = events_[from];
      }

      count_--;
      if (count_ == 0) {
        head_ = 0;
      }
      return true;
    }
  }

  return false;
}

bool UrcStore::has(UrcType type) const {
  if (count_ == 0) {
    return false;
  }
  for (size_t i = 0; i < count_; ++i) {
    size_t index = (head_ + i) % kMaxEvents;
    if (events_[index].type == type) {
      return true;
    }
  }
  return false;
}

uint32_t UrcStore::overflowCount() const {
  return overflowCount_;
}
