#pragma once

#include <Arduino.h>

// OTA modem status/trigger codes.
constexpr uint16_t kOtaTriggerCode = 201;   // Console trigger for modem OTA.
constexpr uint16_t kOtaSuccessCode = 200;   // Sent before reboot on success.
constexpr uint16_t kOtaFailCode = 299;      // Sent once on any OTA failure.
constexpr uint16_t kResetCode = 999;        // Console code sent before SMS reset reboot.
constexpr uint16_t kUpdateCode = 202;        // Console code sent before SMS update reboot.
