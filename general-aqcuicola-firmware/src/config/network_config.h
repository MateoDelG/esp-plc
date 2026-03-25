#pragma once

#include <Arduino.h>

constexpr const char* kWifiSsid = "Delga";
constexpr const char* kWifiPassword = "Delga1213";

const IPAddress kLocalIp(192, 168, 1, 180);
const IPAddress kGatewayIp(192, 168, 1, 1);
const IPAddress kSubnetMask(255, 255, 255, 0);
const IPAddress kPrimaryDns(192, 168, 1, 1);
const IPAddress kSecondaryDns(8, 8, 8, 8);
