#pragma once

#include <Arduino.h>

struct ConsoleMessage {
  String topic;
  String payload;
  uint32_t timestampMs = 0;
  bool isNew = false;
};
