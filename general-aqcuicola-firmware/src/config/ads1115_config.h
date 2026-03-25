#pragma once

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

constexpr uint8_t kAds1115I2cAddress = 0x48;
constexpr adsGain_t kAds1115DefaultGain = GAIN_FOUR;
constexpr uint16_t kAds1115DefaultRate = RATE_ADS1115_128SPS;
constexpr uint8_t kAds1115DefaultAveraging = 7;
constexpr uint32_t kAds1115ReadIntervalMs = 1000;
constexpr uint8_t kAds1115DefaultEnabledMask = 0x0F;
