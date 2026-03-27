#pragma once

#include <Arduino.h>

constexpr const char* kHttpDownloadUrl =
  "https://raw.githubusercontent.com/MateoDelG/tests-ota-esp/master/firmware.bin";
constexpr const char* kHttpDownloadPath = "/firmware.bin";
constexpr uint16_t kHttpDownloadChunkSize = 512;

constexpr uint16_t kOtaConsoleBeforeDownload = 210;
constexpr uint16_t kOtaConsoleAfterDownload = 220;
constexpr uint16_t kOtaConsoleAfterCopyToMemory = 230;
constexpr uint16_t kOtaConsoleBeforeReboot = 200;

constexpr int8_t kSdMiso = 2;
constexpr int8_t kSdMosi = 15;
constexpr int8_t kSdSclk = 14;
constexpr int8_t kSdCs = 13;
constexpr uint32_t kSdSpiClockHz = 800000;
constexpr uint32_t kSdFlushThreshold = 8192;
constexpr uint32_t kSdProbeSizeLight = 16384;
constexpr uint32_t kSdProbeSizeStress = 1048576;
constexpr size_t kSdProbeBlock = 4096;
constexpr uint8_t kSdInitRetries = 3;
constexpr uint16_t kSdInitPreSpiDelayMs = 250;
constexpr uint16_t kSdInitPostSpiDelayMs = 150;
constexpr uint16_t kSdInitRetryGapMs = 400;
constexpr uint16_t kSdShutdownDelayMs = 300;
