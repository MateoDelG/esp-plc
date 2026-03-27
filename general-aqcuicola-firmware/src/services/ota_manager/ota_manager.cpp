#include "services/ota_manager/ota_manager.h"

#include <SD.h>
#include <Update.h>

void OtaManager::begin() {}

bool OtaManager::installFromSd(const char* path, OtaStageCallback afterWriteCb,
                               void* context) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }

  size_t size = file.size();
  if (size == 0) {
    file.close();
    return false;
  }

  if (!Update.begin(size)) {
    file.close();
    return false;
  }

  uint8_t buffer[1024];
  while (file.available()) {
    size_t len = file.read(buffer, sizeof(buffer));
    if (len == 0) {
      break;
    }
    if (Update.write(buffer, len) != len) {
      file.close();
      Update.abort();
      return false;
    }
  }

  if (afterWriteCb) {
    afterWriteCb(context);
  }

  bool ok = Update.end(true);
  file.close();
  return ok;
}
