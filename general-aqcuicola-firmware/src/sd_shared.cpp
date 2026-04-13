#include "sd_shared.h"

SemaphoreHandle_t sdSharedMutex() {
  static SemaphoreHandle_t mutex = nullptr;
  if (!mutex) {
    mutex = xSemaphoreCreateMutex();
  }
  return mutex;
}
