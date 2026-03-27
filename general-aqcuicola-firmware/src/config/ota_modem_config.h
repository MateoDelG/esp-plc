#pragma once

#include <Arduino.h>

constexpr const char* kHttpDownloadUrl =
  "https://raw.githubusercontent.com/MateoDelG/tests-ota-esp/master/firmware.bin";
constexpr const char* kHttpModemPath = "C:/firmware.bin";
constexpr uint16_t kHttpDownloadChunkSize = 512;

constexpr uint16_t kOtaConsoleBeforeReboot = 200;
