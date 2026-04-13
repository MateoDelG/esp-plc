#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Shared mutex for all SD access. The global SD object is process-wide and
// must be serialized across modules (logger, OTA, console, modem, etc.).
SemaphoreHandle_t sdSharedMutex();
