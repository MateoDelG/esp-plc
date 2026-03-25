#pragma once

#include <Arduino.h>

constexpr const char* kUbiToken = "BBUS-orL2zH4XNEKC0880tXUcuxdTWpX5R8";
constexpr const char* kUbiHost = "industrial.api.ubidots.com";
constexpr uint16_t kUbiPort = 8883;
constexpr bool kUbiUseTls = true;

constexpr const char* kUbiDeviceLabel = "aqcuicola-001";
constexpr const char* kUbiClientId = "aqcuicola-001";

constexpr uint32_t kUbiPublishIntervalMs = 60000;
constexpr uint32_t kUbiPollIntervalMs = 1000;
constexpr uint32_t kUbiReconnectIntervalMs = 5000;
constexpr uint8_t kUbiConnectRetries = 3;
constexpr uint32_t kUbiConnectRetryDelayMs = 2000;

inline String ubidotsPublishTopic() {
  return String("/v1.6/devices/") + kUbiDeviceLabel;
}

inline String ubidotsConsoleTopic() {
  return String("/v1.6/devices/") + kUbiDeviceLabel + "/console/lv";
}
